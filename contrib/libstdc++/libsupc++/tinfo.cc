// Methods for type_info for -*- C++ -*- Run Time Type Identification.
// Copyright (C) 1994, 1995, 1996, 1997, 1998, 1999, 2000, 2001, 2002,
// 2003, 2004, 2005, 2006, 2007
// Free Software Foundation
//
// This file is part of GCC.
//
// GCC is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2, or (at your option)
// any later version.

// GCC is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with GCC; see the file COPYING.  If not, write to
// the Free Software Foundation, 51 Franklin Street, Fifth Floor,
// Boston, MA 02110-1301, USA. 

// As a special exception, you may use this file as part of a free software
// library without restriction.  Specifically, if other files instantiate
// templates or use macros or inline functions from this file, or you compile
// this file and link it with other files to produce an executable, this
// file does not by itself cause the resulting executable to be covered by
// the GNU General Public License.  This exception does not however
// invalidate any other reasons why the executable file might be covered by
// the GNU General Public License.

#include <bits/c++config.h>
#include <cstddef>
#include "tinfo.h"
#include "new"			// for placement new

// This file contains the minimal working set necessary to link with code
// that uses virtual functions and -frtti but does not actually use RTTI
// functionality.

std::type_info::
~type_info ()
{ }

std::bad_cast::~bad_cast() throw() { }
std::bad_typeid::~bad_typeid() throw() { }

const char* 
std::bad_cast::what() const throw()
{
  return "std::bad_cast";
}

const char* 
std::bad_typeid::what() const throw()
{
  return "std::bad_typeid";
}

#if !__GXX_MERGED_TYPEINFO_NAMES

// We can't rely on common symbols being shared between shared objects.
bool std::type_info::
operator== (const std::type_info& arg) const
{
  return (&arg == this) || (__builtin_strcmp (name (), arg.name ()) == 0);
}

#endif

namespace std {

// return true if this is a type_info for a pointer type
bool type_info::
__is_pointer_p () const
{
  return false;
}

// return true if this is a type_info for a function type
bool type_info::
__is_function_p () const
{
  return false;
}

// try and catch a thrown object.
bool type_info::
__do_catch (const type_info *thr_type, void **, unsigned) const
{
  return *this == *thr_type;
}

// upcast from this type to the target. __class_type_info will override
bool type_info::
__do_upcast (const abi::__class_type_info *, void **) const
{
  return false;
}

}

namespace {

using namespace std;
using namespace abi;

// Initial part of a vtable, this structure is used with offsetof, so we don't
// have to keep alignments consistent manually.
struct vtable_prefix 
{
  // Offset to most derived object.
  ptrdiff_t whole_object;

  // Additional padding if necessary.
#ifdef _GLIBCXX_VTABLE_PADDING
  ptrdiff_t padding1;               
#endif

  // Pointer to most derived type_info.
  const __class_type_info *whole_type;  

  // Additional padding if necessary.
#ifdef _GLIBCXX_VTABLE_PADDING
  ptrdiff_t padding2;               
#endif

  // What a class's vptr points to.
  const void *origin;               
};

template <typename T>
inline const T *
adjust_pointer (const void *base, ptrdiff_t offset)
{
  return reinterpret_cast <const T *>
    (reinterpret_cast <const char *> (base) + offset);
}

// ADDR is a pointer to an object.  Convert it to a pointer to a base,
// using OFFSET. IS_VIRTUAL is true, if we are getting a virtual base.
inline void const *
convert_to_base (void const *addr, bool is_virtual, ptrdiff_t offset)
{
  if (is_virtual)
    {
      const void *vtable = *static_cast <const void *const *> (addr);
      
      offset = *adjust_pointer<ptrdiff_t> (vtable, offset);
    }

  return adjust_pointer<void> (addr, offset);
}

// some predicate functions for __class_type_info::__sub_kind
inline bool contained_p (__class_type_info::__sub_kind access_path)
{
  return access_path >= __class_type_info::__contained_mask;
}
inline bool public_p (__class_type_info::__sub_kind access_path)
{
  return access_path & __class_type_info::__contained_public_mask;
}
inline bool virtual_p (__class_type_info::__sub_kind access_path)
{
  return (access_path & __class_type_info::__contained_virtual_mask);
}
inline bool contained_public_p (__class_type_info::__sub_kind access_path)
{
  return ((access_path & __class_type_info::__contained_public)
          == __class_type_info::__contained_public);
}
inline bool contained_nonpublic_p (__class_type_info::__sub_kind access_path)
{
  return ((access_path & __class_type_info::__contained_public)
          == __class_type_info::__contained_mask);
}
inline bool contained_nonvirtual_p (__class_type_info::__sub_kind access_path)
{
  return ((access_path & (__class_type_info::__contained_mask
                          | __class_type_info::__contained_virtual_mask))
          == __class_type_info::__contained_mask);
}

static const __class_type_info *const nonvirtual_base_type =
    static_cast <const __class_type_info *> (0) + 1;

} // namespace

namespace __cxxabiv1
{

__class_type_info::
~__class_type_info ()
{}

__si_class_type_info::
~__si_class_type_info ()
{}

__vmi_class_type_info::
~__vmi_class_type_info ()
{}

// __upcast_result is used to hold information during traversal of a class
// hierarchy when catch matching.
struct __class_type_info::__upcast_result
{
  const void *dst_ptr;        // pointer to caught object
  __sub_kind part2dst;        // path from current base to target
  int src_details;            // hints about the source type hierarchy
  const __class_type_info *base_type; // where we found the target,
                              // if in vbase the __class_type_info of vbase
                              // if a non-virtual base then 1
                              // else NULL
  __upcast_result (int d)
    :dst_ptr (NULL), part2dst (__unknown), src_details (d), base_type (NULL)
    {}
};

// __dyncast_result is used to hold information during traversal of a class
// hierarchy when dynamic casting.
struct __class_type_info::__dyncast_result
{
  const void *dst_ptr;        // pointer to target object or NULL
  __sub_kind whole2dst;       // path from most derived object to target
  __sub_kind whole2src;       // path from most derived object to sub object
  __sub_kind dst2src;         // path from target to sub object
  int whole_details;          // details of the whole class hierarchy
  
  __dyncast_result (int details_ = __vmi_class_type_info::__flags_unknown_mask)
    :dst_ptr (NULL), whole2dst (__unknown),
     whole2src (__unknown), dst2src (__unknown),
     whole_details (details_)
    {}

protected:
  __dyncast_result(const __dyncast_result&);
  
  __dyncast_result&
  operator=(const __dyncast_result&);
};

bool __class_type_info::
__do_catch (const type_info *thr_type,
            void **thr_obj,
            unsigned outer) const
{
  if (*this == *thr_type)
    return true;
  if (outer >= 4)
    // Neither `A' nor `A *'.
    return false;
  return thr_type->__do_upcast (this, thr_obj);
}

bool __class_type_info::
__do_upcast (const __class_type_info *dst_type,
             void **obj_ptr) const
{
  __upcast_result result (__vmi_class_type_info::__flags_unknown_mask);
  
  __do_upcast (dst_type, *obj_ptr, result);
  if (!contained_public_p (result.part2dst))
    return false;
  *obj_ptr = const_cast <void *> (result.dst_ptr);
  return true;
}

inline __class_type_info::__sub_kind __class_type_info::
__find_public_src (ptrdiff_t src2dst,
                   const void *obj_ptr,
                   const __class_type_info *src_type,
                   const void *src_ptr) const
{
  if (src2dst >= 0)
    return adjust_pointer <void> (obj_ptr, src2dst) == src_ptr
            ? __contained_public : __not_contained;
  if (src2dst == -2)
    return __not_contained;
  return __do_find_public_src (src2dst, obj_ptr, src_type, src_ptr);
}

__class_type_info::__sub_kind __class_type_info::
__do_find_public_src (ptrdiff_t,
                      const void *obj_ptr,
                      const __class_type_info *,
                      const void *src_ptr) const
{
  if (src_ptr == obj_ptr)
    // Must be our type, as the pointers match.
    return __contained_public;
  return __not_contained;
}

__class_type_info::__sub_kind __si_class_type_info::
__do_find_public_src (ptrdiff_t src2dst,
                      const void *obj_ptr,
                      const __class_type_info *src_type,
                      const void *src_ptr) const
{
  if (src_ptr == obj_ptr && *this == *src_type)
    return __contained_public;
  return __base_type->__do_find_public_src (src2dst, obj_ptr, src_type, src_ptr);
}

__class_type_info::__sub_kind __vmi_class_type_info::
__do_find_public_src (ptrdiff_t src2dst,
                      const void *obj_ptr,
                      const __class_type_info *src_type,
                      const void *src_ptr) const
{
  if (obj_ptr == src_ptr && *this == *src_type)
    return __contained_public;
  
  for (std::size_t i = __base_count; i--;)
    {
      if (!__base_info[i].__is_public_p ())
        continue; // Not public, can't be here.
      
      const void *base = obj_ptr;
      ptrdiff_t offset = __base_info[i].__offset ();
      bool is_virtual = __base_info[i].__is_virtual_p ();
      
      if (is_virtual)
        {
          if (src2dst == -3)
            continue; // Not a virtual base, so can't be here.
        }
      base = convert_to_base (base, is_virtual, offset);
      
      __sub_kind base_kind = __base_info[i].__base_type->__do_find_public_src
                              (src2dst, base, src_type, src_ptr);
      if (contained_p (base_kind))
        {
          if (is_virtual)
            base_kind = __sub_kind (base_kind | __contained_virtual_mask);
          return base_kind;
        }
    }
  
  return __not_contained;
}

bool __class_type_info::
__do_dyncast (ptrdiff_t,
              __sub_kind access_path,
              const __class_type_info *dst_type,
              const void *obj_ptr,
              const __class_type_info *src_type,
              const void *src_ptr,
              __dyncast_result &__restrict result) const
{
  if (obj_ptr == src_ptr && *this == *src_type)
    {
      // The src object we started from. Indicate how we are accessible from
      // the most derived object.
      result.whole2src = access_path;
      return false;
    }
  if (*this == *dst_type)
    {
      result.dst_ptr = obj_ptr;
      result.whole2dst = access_path;
      result.dst2src = __not_contained;
      return false;
    }
  return false;
}

bool __si_class_type_info::
__do_dyncast (ptrdiff_t src2dst,
              __sub_kind access_path,
              const __class_type_info *dst_type,
              const void *obj_ptr,
              const __class_type_info *src_type,
              const void *src_ptr,
              __dyncast_result &__restrict result) const
{
  if (*this == *dst_type)
    {
      result.dst_ptr = obj_ptr;
      result.whole2dst = access_path;
      if (src2dst >= 0)
        result.dst2src = adjust_pointer <void> (obj_ptr, src2dst) == src_ptr
              ? __contained_public : __not_contained;
      else if (src2dst == -2)
        result.dst2src = __not_contained;
      return false;
    }
  if (obj_ptr == src_ptr && *this == *src_type)
    {
      // The src object we started from. Indicate how we are accessible from
      // the most derived object.
      result.whole2src = access_path;
      return false;
    }
  return __base_type->__do_dyncast (src2dst, access_path, dst_type, obj_ptr,
                             src_type, src_ptr, result);
}

// This is a big hairy function. Although the run-time behaviour of
// dynamic_cast is simple to describe, it gives rise to some non-obvious
// behaviour. We also desire to determine as early as possible any definite
// answer we can get. Because it is unknown what the run-time ratio of
// succeeding to failing dynamic casts is, we do not know in which direction
// to bias any optimizations. To that end we make no particular effort towards
// early fail answers or early success answers. Instead we try to minimize
// work by filling in things lazily (when we know we need the information),
// and opportunisticly take early success or failure results.
bool __vmi_class_type_info::
__do_dyncast (ptrdiff_t src2dst,
              __sub_kind access_path,
              const __class_type_info *dst_type,
              const void *obj_ptr,
              const __class_type_info *src_type,
              const void *src_ptr,
              __dyncast_result &__restrict result) const
{
  if (result.whole_details & __flags_unknown_mask)
    result.whole_details = __flags;
  
  if (obj_ptr == src_ptr && *this == *src_type)
    {
      // The src object we started from. Indicate how we are accessible from
      // the most derived object.
      result.whole2src = access_path;
      return false;
    }
  if (*this == *dst_type)
    {
      result.dst_ptr = obj_ptr;
      result.whole2dst = access_path;
      if (src2dst >= 0)
        result.dst2src = adjust_pointer <void> (obj_ptr, src2dst) == src_ptr
              ? __contained_public : __not_contained;
      else if (src2dst == -2)
        result.dst2src = __not_contained;
      return false;
    }

  bool result_ambig = false;
  for (std::size_t i = __base_count; i--;)
    {
      __dyncast_result result2 (result.whole_details);
      void const *base = obj_ptr;
      __sub_kind base_access = access_path;
      ptrdiff_t offset = __base_info[i].__offset ();
      bool is_virtual = __base_info[i].__is_virtual_p ();
      
      if (is_virtual)
        base_access = __sub_kind (base_access | __contained_virtual_mask);
      base = convert_to_base (base, is_virtual, offset);

      if (!__base_info[i].__is_public_p ())
        {
          if (src2dst == -2 &&
              !(result.whole_details
                & (__non_diamond_repeat_mask | __diamond_shaped_mask)))
            // The hierarchy has no duplicate bases (which might ambiguate
            // things) and where we started is not a public base of what we
            // want (so it cannot be a downcast). There is nothing of interest
            // hiding in a non-public base.
            continue;
          base_access = __sub_kind (base_access & ~__contained_public_mask);
        }
      
      bool result2_ambig
          = __base_info[i].__base_type->__do_dyncast (src2dst, base_access,
                                             dst_type, base,
                                             src_type, src_ptr, result2);
      result.whole2src = __sub_kind (result.whole2src | result2.whole2src);
      if (result2.dst2src == __contained_public
          || result2.dst2src == __contained_ambig)
        {
          result.dst_ptr = result2.dst_ptr;
          result.whole2dst = result2.whole2dst;
          result.dst2src = result2.dst2src;
          // Found a downcast which can't be bettered or an ambiguous downcast
          // which can't be disambiguated
          return result2_ambig;
        }
      
      if (!result_ambig && !result.dst_ptr)
        {
          // Not found anything yet.
          result.dst_ptr = result2.dst_ptr;
          result.whole2dst = result2.whole2dst;
          result_ambig = result2_ambig;
          if (result.dst_ptr && result.whole2src != __unknown
              && !(__flags & __non_diamond_repeat_mask))
            // Found dst and src and we don't have repeated bases.
            return result_ambig;
        }
      else if (result.dst_ptr && result.dst_ptr == result2.dst_ptr)
        {
          // Found at same address, must be via virtual.  Pick the most
          // accessible path.
          result.whole2dst =
              __sub_kind (result.whole2dst | result2.whole2dst);
        }
      else if ((result.dst_ptr != 0 && result2.dst_ptr != 0)
	       || (result.dst_ptr != 0 && result2_ambig)
	       || (result2.dst_ptr != 0 && result_ambig))
        {
          // Found two different DST_TYPE bases, or a valid one and a set of
          // ambiguous ones, must disambiguate. See whether SRC_PTR is
          // contained publicly within one of the non-ambiguous choices. If it
          // is in only one, then that's the choice. If it is in both, then
          // we're ambiguous and fail. If it is in neither, we're ambiguous,
          // but don't yet fail as we might later find a third base which does
          // contain SRC_PTR.
        
          __sub_kind new_sub_kind = result2.dst2src;
          __sub_kind old_sub_kind = result.dst2src;
          
          if (contained_p (result.whole2src)
              && (!virtual_p (result.whole2src)
                  || !(result.whole_details & __diamond_shaped_mask)))
            {
              // We already found SRC_PTR as a base of most derived, and
              // either it was non-virtual, or the whole hierarchy is
              // not-diamond shaped. Therefore if it is in either choice, it
              // can only be in one of them, and we will already know.
              if (old_sub_kind == __unknown)
                old_sub_kind = __not_contained;
              if (new_sub_kind == __unknown)
                new_sub_kind = __not_contained;
            }
          else
            {
              if (old_sub_kind >= __not_contained)
                ;// already calculated
              else if (contained_p (new_sub_kind)
                       && (!virtual_p (new_sub_kind)
                           || !(__flags & __diamond_shaped_mask)))
                // Already found inside the other choice, and it was
                // non-virtual or we are not diamond shaped.
                old_sub_kind = __not_contained;
              else
                old_sub_kind = dst_type->__find_public_src
                                (src2dst, result.dst_ptr, src_type, src_ptr);
          
              if (new_sub_kind >= __not_contained)
                ;// already calculated
              else if (contained_p (old_sub_kind)
                       && (!virtual_p (old_sub_kind)
                           || !(__flags & __diamond_shaped_mask)))
                // Already found inside the other choice, and it was
                // non-virtual or we are not diamond shaped.
                new_sub_kind = __not_contained;
              else
                new_sub_kind = dst_type->__find_public_src
                                (src2dst, result2.dst_ptr, src_type, src_ptr);
            }
          
          // Neither sub_kind can be contained_ambig -- we bail out early
          // when we find those.
          if (contained_p (__sub_kind (new_sub_kind ^ old_sub_kind)))
            {
              // Only on one choice, not ambiguous.
              if (contained_p (new_sub_kind))
                {
                  // Only in new.
                  result.dst_ptr = result2.dst_ptr;
                  result.whole2dst = result2.whole2dst;
                  result_ambig = false;
                  old_sub_kind = new_sub_kind;
                }
              result.dst2src = old_sub_kind;
              if (public_p (result.dst2src))
                return false; // Can't be an ambiguating downcast for later discovery.
              if (!virtual_p (result.dst2src))
                return false; // Found non-virtually can't be bettered
            }
          else if (contained_p (__sub_kind (new_sub_kind & old_sub_kind)))
            {
              // In both.
              result.dst_ptr = NULL;
              result.dst2src = __contained_ambig;
              return true;  // Fail.
            }
          else
            {
              // In neither publicly, ambiguous for the moment, but keep
              // looking. It is possible that it was private in one or
              // both and therefore we should fail, but that's just tough.
              result.dst_ptr = NULL;
              result.dst2src = __not_contained;
              result_ambig = true;
            }
        }
      
      if (result.whole2src == __contained_private)
        // We found SRC_PTR as a private non-virtual base, therefore all
        // cross casts will fail. We have already found a down cast, if
        // there is one.
        return result_ambig;
    }

  return result_ambig;
}

bool __class_type_info::
__do_upcast (const __class_type_info *dst, const void *obj,
             __upcast_result &__restrict result) const
{
  if (*this == *dst)
    {
      result.dst_ptr = obj;
      result.base_type = nonvirtual_base_type;
      result.part2dst = __contained_public;
      return true;
    }
  return false;
}

bool __si_class_type_info::
__do_upcast (const __class_type_info *dst, const void *obj_ptr,
             __upcast_result &__restrict result) const
{
  if (__class_type_info::__do_upcast (dst, obj_ptr, result))
    return true;
  
  return __base_type->__do_upcast (dst, obj_ptr, result);
}

bool __vmi_class_type_info::
__do_upcast (const __class_type_info *dst, const void *obj_ptr,
             __upcast_result &__restrict result) const
{
  if (__class_type_info::__do_upcast (dst, obj_ptr, result))
    return true;
  
  int src_details = result.src_details;
  if (src_details & __flags_unknown_mask)
    src_details = __flags;
  
  for (std::size_t i = __base_count; i--;)
    {
      __upcast_result result2 (src_details);
      const void *base = obj_ptr;
      ptrdiff_t offset = __base_info[i].__offset ();
      bool is_virtual = __base_info[i].__is_virtual_p ();
      bool is_public = __base_info[i].__is_public_p ();
      
      if (!is_public && !(src_details & __non_diamond_repeat_mask))
        // original cannot have an ambiguous base, so skip private bases
        continue;

      if (base)
        base = convert_to_base (base, is_virtual, offset);
      
      if (__base_info[i].__base_type->__do_upcast (dst, base, result2))
        {
          if (result2.base_type == nonvirtual_base_type && is_virtual)
            result2.base_type = __base_info[i].__base_type;
          if (contained_p (result2.part2dst) && !is_public)
            result2.part2dst = __sub_kind (result2.part2dst & ~__contained_public_mask);
          
          if (!result.base_type)
            {
              result = result2;
              if (!contained_p (result.part2dst))
                return true; // found ambiguously
              
              if (result.part2dst & __contained_public_mask)
                {
                  if (!(__flags & __non_diamond_repeat_mask))
                    return true;  // cannot have an ambiguous other base
                }
              else
                {
                  if (!virtual_p (result.part2dst))
                    return true; // cannot have another path
                  if (!(__flags & __diamond_shaped_mask))
                    return true; // cannot have a more accessible path
                }
            }
          else if (result.dst_ptr != result2.dst_ptr)
            {
              // Found an ambiguity.
	      result.dst_ptr = NULL;
	      result.part2dst = __contained_ambig;
	      return true;
            }
          else if (result.dst_ptr)
            {
              // Ok, found real object via a virtual path.
              result.part2dst
                  = __sub_kind (result.part2dst | result2.part2dst);
            }
          else
            {
              // Dealing with a null pointer, need to check vbase
              // containing each of the two choices.
              if (result2.base_type == nonvirtual_base_type
                  || result.base_type == nonvirtual_base_type
                  || !(*result2.base_type == *result.base_type))
                {
                  // Already ambiguous, not virtual or via different virtuals.
                  // Cannot match.
                  result.part2dst = __contained_ambig;
                  return true;
                }
              result.part2dst
                  = __sub_kind (result.part2dst | result2.part2dst);
            }
        }
    }
  return result.part2dst != __unknown;
}

// this is the external interface to the dynamic cast machinery
extern "C" void *
__dynamic_cast (const void *src_ptr,    // object started from
                const __class_type_info *src_type, // type of the starting object
                const __class_type_info *dst_type, // desired target type
                ptrdiff_t src2dst) // how src and dst are related
{
  const void *vtable = *static_cast <const void *const *> (src_ptr);
  const vtable_prefix *prefix =
      adjust_pointer <vtable_prefix> (vtable, 
				      -offsetof (vtable_prefix, origin));
  const void *whole_ptr =
      adjust_pointer <void> (src_ptr, prefix->whole_object);
  const __class_type_info *whole_type = prefix->whole_type;
  __class_type_info::__dyncast_result result;
  
  whole_type->__do_dyncast (src2dst, __class_type_info::__contained_public,
                            dst_type, whole_ptr, src_type, src_ptr, result);
  if (!result.dst_ptr)
    return NULL;
  if (contained_public_p (result.dst2src))
    // Src is known to be a public base of dst.
    return const_cast <void *> (result.dst_ptr);
  if (contained_public_p (__class_type_info::__sub_kind (result.whole2src & result.whole2dst)))
    // Both src and dst are known to be public bases of whole. Found a valid
    // cross cast.
    return const_cast <void *> (result.dst_ptr);
  if (contained_nonvirtual_p (result.whole2src))
    // Src is known to be a non-public nonvirtual base of whole, and not a
    // base of dst. Found an invalid cross cast, which cannot also be a down
    // cast
    return NULL;
  if (result.dst2src == __class_type_info::__unknown)
    result.dst2src = dst_type->__find_public_src (src2dst, result.dst_ptr,
                                                  src_type, src_ptr);
  if (contained_public_p (result.dst2src))
    // Found a valid down cast
    return const_cast <void *> (result.dst_ptr);
  // Must be an invalid down cast, or the cross cast wasn't bettered
  return NULL;
}

} // namespace __cxxabiv1
