// based off github:mavam/vast
// blob 19dfcb05358fe7dae22079f5ca03121df01021f2
#ifndef VAST_UTIL_INTERVAL_MAP_H
#define VAST_UTIL_INTERVAL_MAP_H

#include <cassert>
#include <map>
#include <tuple>
#include "vast/util/iterator.hpp"

namespace vast {
namespace util {

/// An associative data structure that maps half-open, *disjoint* intervals to
/// values.
template <typename Point, typename Value>
class range_map
{
  static_assert(std::is_arithmetic<Point>::value,
                "Point must be an arithmetic type");

  using map_type = std::map<Point, std::pair<Point, Value>>;
  using map_iterator = typename map_type::iterator;
  using map_const_iterator = typename map_type::const_iterator;

public:
  class const_iterator
    : public iterator_adaptor<
        const_iterator,
        map_const_iterator,
        std::bidirectional_iterator_tag,
        std::tuple<Point, Point, Value>,
        std::tuple<Point const&, Point const&, Value const&>
      >
  {
    using super = iterator_adaptor<
      const_iterator,
      map_const_iterator,
      std::bidirectional_iterator_tag,
      std::tuple<Point, Point, Value>,
      std::tuple<Point const&, Point const&, Value const&>
    >;

  public:
    using super::super;

  private:
    friend util::iterator_access;

    std::tuple<Point const&, Point const&, Value const&> dereference() const
    {
      return std::tie(this->base()->first,
                      this->base()->second.first,
                      this->base()->second.second);
    }
  };

  const_iterator begin() const
  {
    return const_iterator{map_.begin()};
  }

  const_iterator end() const
  {
    return const_iterator{map_.end()};
  }

  /// Associates a value with a right-open range.
  /// @param l The left endpoint of the interval.
  /// @param r The right endpoint of the interval.
  /// @param v The value r associated with *[l, r]*.
  /// @returns `true` on success.
  bool insert(Point l, Point r, Value v)
  {
    if (r<=l) return false;
    auto lb = map_.lower_bound(l);
    if (locate(l, lb) == map_.end() && (lb == map_.end() || r <= left(lb)))
      return map_.emplace(l, std::make_pair(r, std::move(v))).second;
    return false;
  }

  /// Inserts a value for a right-open range, updating existing adjacent
  /// intervals if it's possible to merge them. Two intervals can only be
  /// merged if they have the same values.
  /// @note If *[l,r]* reaches into an existing interval, injection fails.
  /// @param l The left endpoint of the interval.
  /// @param r The right endpoint of the interval.
  /// @param v The value r associated with *[l,r]*.
  /// @returns `true` on success.
  bool inject(Point l, Point r, Value v)
  {
    if (r<=l) return false;
    if (map_.empty())
      return emplace(l, r, std::move(v));
    auto i = map_.lower_bound(l);
    // Adjust position (i = this, p = prev, n = next).
    if (i == map_.end() || (i != map_.begin() && l != left(i)))
      --i;
    auto n = i;
    ++n;
    auto p = i;
    if (i != map_.begin())
      --p;
    else
      p = map_.end();
    // Assess the fit.
    auto fits_left = r <= left(i) && (p == map_.end() || l >= right(p));
    auto fits_right = l >= right(i) && (n == map_.end() || r <= left(n));
    if (fits_left)
    {
      auto right_merge = r == left(i) && v == value(i);
      auto left_merge = p != map_.end() && l == right(p) && v == value(p);
      if (left_merge && right_merge)
      {
        right(p) = right(i);
        map_.erase(i);
      }
      else if (left_merge)
      {
        right(p) = r;
      }
      else if (right_merge)
      {
        emplace(l, right(i), std::move(v));
        map_.erase(i);
      }
      else
      {
        emplace(l, r, std::move(v));
      }
      return true;
    }
    else if (fits_right)
    {
      auto right_merge = n != map_.end() && r == left(n) && v == value(n);
      auto left_merge = l == right(i) && v == value(i);
      if (left_merge && right_merge)
      {
        right(i) = right(n);
        map_.erase(n);
      }
      else if (left_merge)
      {
        right(i) = r;
      }
      else if (right_merge)
      {
        emplace(l, right(n), std::move(v));
        map_.erase(n);
      }
      else
      {
        emplace(l, r, std::move(v));
      }
      return true;
    }
    return false;
  }

  /// Removes a value given a point from a right-open range.
  /// @param p A point from a range that maps to a value.
  /// @returns `true` if the value associated with the interval containing *p*
  ///          has been successfully removed, and `false` if *p* does not map
  ///          to an existing value.
  bool erase(Point p)
  {
    auto i = locate(p, map_.lower_bound(p));
    if (i == map_.end())
      return false;
    map_.erase(i);
    return true;
  }

  /// Adjusts or erases ranges so that no values in the map overlap with [l,r)
  /// @param l The left endpoint of the interval.
  /// @param r The right endpoint of the interval.
  void erase(Point l, Point r)
  {
    if (r<=l) return;
    Point next_left=l;
    for (;;)
    {
      auto lb=map_.lower_bound(next_left);
      auto it=locate(next_left,lb);
      if (it==map_.end()) it=lb;
      if (it==map_.end() || left(it)>=r)
        break;
      next_left=right(it);
      
      if (l<=left(it) && r>=right(it))
      { // [l,r) overlaps [it) in its entirety
        map_.erase(it);
      }
      else if (left(it)<=l && right(it)>=r)
      { // [it) overlaps [l,r) in its entirety
        Point orig_r=right(it);
        right(it)=l;
        inject(r,orig_r,value(it));
        break;
      }
      else if (l<=left(it) && r>left(it))
      { // [l,r) overlaps [it) partially and starts before
        map_.emplace(r, std::make_pair(right(it), std::move(value(it))));
        map_.erase(it);
        break;
      }
      else if (l<right(it) && r>=right(it))
      { // [l,r) overlaps [it) partially and starts after
        right(it)=l;
      }
    }
  }

  /// Retrieves the value for a given point.
  /// @param p The point to lookup.
  /// @returns A pointer to the value associated with the half-open interval
  ///          *[a,b)* if *a <= p < b* and `nullptr` otherwise.
  Value const* lookup(Point const& p) const
  {
    auto i = locate(p, map_.lower_bound(p));
    return i != map_.end() ? &i->second.second : nullptr;
  }

  /// Retrieves value and interval for a given point.
  /// @param p The point to lookup.
  /// @returns A tuple with the last component holding a pointer to the value
  ///          associated with the half-open interval *[a,b)* if *a <= p < b*,
  ///          and `nullptr` otherwise. If the last component points to a
  ///          valid value, then the first two represent *[a,b)* and *[0,0)*
  ///          otherwise.
  std::tuple<Point, Point, Value const*> find(Point const& p) const
  {
    auto i = locate(p, map_.lower_bound(p));
    if (i == map_.end())
      return std::tuple<Point, Point, Value const*>{0, 0, nullptr};
    else
      return std::tuple<Point, Point, Value const*>{left(i), right(i), &i->second.second};
  }

  /// Retrieves the size of the range map.
  /// @returns The number of entries in the map.
  size_t size() const
  {
    return map_.size();
  }

  /// Checks whether the range map is empty.
  /// @returns `true` iff the map is empty.
  bool empty() const
  {
    return map_.empty();
  }

  /// Clears the range map.
  void clear()
  {
    return map_.clear();
  }

private:
  static auto left(map_const_iterator i) -> const decltype(i->first)&
  {
    return i->first;
  }

  static auto right(map_const_iterator i) -> const decltype(i->second.first)&
  {
    return i->second.first;
  }

  static auto right(map_iterator i) -> decltype(i->second.first)&
  {
    return i->second.first;
  }

  static auto value(map_const_iterator i) -> const decltype(i->second.second)&
  {
    return i->second.second;
  }

  // Finds the interval of a point.
  map_iterator locate(Point const& p, map_iterator lb)
  {
    if ((lb != map_.end() && p == left(lb)) ||
        (lb != map_.begin() && p < right(--lb)))
    //if (lb == map_.end())
    //  return map_.empty() ? lb : map_.rbegin().base();
    //else if (p == left(lb) || (lb != map_.begin() && p < right(--lb)))
      return lb;
    return map_.end();
  }

  // Finds the interval of a point.
  map_const_iterator locate(Point const& p, map_const_iterator lb) const
  {
    if ((lb != map_.end() && p == left(lb)) ||
        (lb != map_.begin() && p < right(--lb)))
    //if (lb == map_.end())
    //  return map_.empty() ? lb : map_.rbegin().base();
    //else if (p == left(lb) || (lb != map_.begin() && p < right(--lb)))
      return lb;
    return map_.end();
  }

  bool emplace(Point l, Point r, Value v)
  {
    auto pair = std::make_pair(std::move(r), std::move(v));
    return map_.emplace(std::move(l), std::move(pair)).second;
  }

  map_type map_;
};

} // namespace util
} // namespace vast

#endif
