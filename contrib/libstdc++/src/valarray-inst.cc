// Explicit instantiation file.

// Copyright (C) 2001, 2004, 2005 Free Software Foundation, Inc.
//
// This file is part of the GNU ISO C++ Library.  This library is free
// software; you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2, or (at your option)
// any later version.

// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License along
// with this library; see the file COPYING.  If not, write to the Free
// Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301,
// USA.

// As a special exception, you may use this file as part of a free software
// library without restriction.  Specifically, if other files instantiate
// templates or use macros or inline functions from this file, or you compile
// this file and link it with other files to produce an executable, this
// file does not by itself cause the resulting executable to be covered by
// the GNU General Public License.  This exception does not however
// invalidate any other reasons why the executable file might be covered by
// the GNU General Public License.

//
// ISO C++ 14882:
//

#include <valarray>

_GLIBCXX_BEGIN_NAMESPACE(std)

  // Some explicit instantiations.
  template void
     __valarray_fill(size_t* __restrict__, size_t, const size_t&);
  
  template void
     __valarray_copy(const size_t* __restrict__, size_t, size_t* __restrict__);
  
  template valarray<size_t>::valarray(size_t);
  template valarray<size_t>::valarray(const valarray<size_t>&);
  template valarray<size_t>::~valarray();
  template size_t valarray<size_t>::size() const;
  template size_t& valarray<size_t>::operator[](size_t);

  inline size_t
  __valarray_product(const valarray<size_t>& __a)
  {
    typedef const size_t* __restrict__ _Tp;
    const size_t __n = __a.size();
    // XXX: This ugly cast is necessary because
    //      valarray::operator[]() const return a VALUE!
    //      Try to get the committee to correct that gross error.
    valarray<size_t>& __t = const_cast<valarray<size_t>&>(__a);
    return __valarray_product(&__t[0], &__t[0] + __n);
  }
  
  // Map a gslice, described by its multidimensional LENGTHS
  // and corresponding STRIDES, to a linear array of INDEXES
  // for the purpose of indexing a flat, one-dimensional array
  // representation of a gslice_array.
  void
  __gslice_to_index(size_t __o, const valarray<size_t>& __l,
                    const valarray<size_t>& __s, valarray<size_t>& __i)
  {
    // There are as many dimensions as there are strides.
    size_t __n = __l.size();

    // Get a buffer to hold current multi-index as we go through
    // the gslice for the purpose of computing its linear-image.
    size_t* const __t = static_cast<size_t*>
      (__builtin_alloca(__n * sizeof (size_t)));
    __valarray_fill(__t, __n, size_t(0));

    // Note that this should match the product of all numbers appearing
    // in __l which describes the multidimensional sizes of the
    // the generalized slice.
    const size_t __z = __i.size();
    
    for (size_t __j = 0; __j < __z; ++__j)
      {
        // Compute the linear-index image of (t_0, ... t_{n-1}).
        // Normaly, we should use inner_product<>(), but we do it the
        // the hard way here to avoid link-time can of worms.
        size_t __a = __o;
        for (size_t __k = 0; __k < __n; ++__k)
          __a += __s[__k] * __t[__k];

        __i[__j] = __a;

        // Process the next multi-index.  The loop ought to be
        // backward since we're making a lexicagraphical visit.
        ++__t[__n - 1];
        for (size_t __k2 = __n - 1; __k2; --__k2)
          {
            if (__t[__k2] >= __l[__k2])
              {
                __t[__k2] = 0;
                ++__t[__k2 - 1];
              }
          }
      }
  }
  
  gslice::_Indexer::_Indexer(size_t __o, const valarray<size_t>& __l,
                             const valarray<size_t>& __s)
  : _M_count(1), _M_start(__o), _M_size(__l), _M_stride(__s),
    _M_index(__l.size() == 0 ? 0 : __valarray_product(__l))
  { __gslice_to_index(__o, __l, __s, _M_index); }  

_GLIBCXX_END_NAMESPACE
