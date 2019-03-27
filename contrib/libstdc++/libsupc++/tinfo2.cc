// Methods for type_info for -*- C++ -*- Run Time Type Identification.

// Copyright (C) 1994, 1996, 1997, 1998, 1999, 2000, 2001, 2002
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

#include <cstddef>
#include "tinfo.h"
#include "new"			// for placement new

// We can't rely on having stdlib.h if we're freestanding.
extern "C" void abort ();

using std::type_info;

#if !__GXX_MERGED_TYPEINFO_NAMES

bool
type_info::before (const type_info &arg) const
{
  return __builtin_strcmp (name (), arg.name ()) < 0;
}

#endif

#include <cxxabi.h>

namespace __cxxabiv1 {

using namespace std;

// This has special meaning to the compiler, and will cause it
// to emit the type_info structures for the fundamental types which are
// mandated to exist in the runtime.
__fundamental_type_info::
~__fundamental_type_info ()
{}

__array_type_info::
~__array_type_info ()
{}

__function_type_info::
~__function_type_info ()
{}

__enum_type_info::
~__enum_type_info ()
{}

__pbase_type_info::
~__pbase_type_info ()
{}

__pointer_type_info::
~__pointer_type_info ()
{}

__pointer_to_member_type_info::
~__pointer_to_member_type_info ()
{}

bool __pointer_type_info::
__is_pointer_p () const
{
  return true;
}

bool __function_type_info::
__is_function_p () const
{
  return true;
}

bool __pbase_type_info::
__do_catch (const type_info *thr_type,
            void **thr_obj,
            unsigned outer) const
{
  if (*this == *thr_type)
    return true;      // same type
  if (typeid (*this) != typeid (*thr_type))
    return false;     // not both same kind of pointers
  
  if (!(outer & 1))
    // We're not the same and our outer pointers are not all const qualified
    // Therefore there must at least be a qualification conversion involved
    // But for that to be valid, our outer pointers must be const qualified.
    return false;
  
  const __pbase_type_info *thrown_type =
    static_cast <const __pbase_type_info *> (thr_type);
  
  if (thrown_type->__flags & ~__flags)
    // We're less qualified.
    return false;
  
  if (!(__flags & __const_mask))
    outer &= ~1;
  
  return __pointer_catch (thrown_type, thr_obj, outer);
}

inline bool __pbase_type_info::
__pointer_catch (const __pbase_type_info *thrown_type,
                 void **thr_obj,
                 unsigned outer) const
{
  return __pointee->__do_catch (thrown_type->__pointee, thr_obj, outer + 2);
}

bool __pointer_type_info::
__pointer_catch (const __pbase_type_info *thrown_type,
                 void **thr_obj,
                 unsigned outer) const
{
  if (outer < 2 && *__pointee == typeid (void))
    {
      // conversion to void
      return !thrown_type->__pointee->__is_function_p ();
    }
  
  return __pbase_type_info::__pointer_catch (thrown_type, thr_obj, outer);
}

bool __pointer_to_member_type_info::
__pointer_catch (const __pbase_type_info *thr_type,
                 void **thr_obj,
                 unsigned outer) const
{
  // This static cast is always valid, as our caller will have determined that
  // thr_type is really a __pointer_to_member_type_info.
  const __pointer_to_member_type_info *thrown_type =
    static_cast <const __pointer_to_member_type_info *> (thr_type);
  
  if (*__context != *thrown_type->__context)
    return false;     // not pointers to member of same class
  
  return __pbase_type_info::__pointer_catch (thrown_type, thr_obj, outer);
}

} // namespace std
