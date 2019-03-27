//===------------------------ valarray.cpp --------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "valarray"

_LIBCPP_BEGIN_NAMESPACE_STD

template valarray<size_t>::valarray(size_t);
template valarray<size_t>::~valarray();
template void valarray<size_t>::resize(size_t, size_t);

void
gslice::__init(size_t __start)
{
    valarray<size_t> __indices(__size_.size());
    size_t __k = __size_.size() != 0;
    for (size_t __i = 0; __i < __size_.size(); ++__i)
        __k *= __size_[__i];
    __1d_.resize(__k);
    if (__1d_.size())
    {
        __k = 0;
        __1d_[__k] = __start;
        while (true)
        {
            size_t __i = __indices.size() - 1;
            while (true)
            {
                if (++__indices[__i] < __size_[__i])
                {
                    ++__k;
                    __1d_[__k] = __1d_[__k-1] + __stride_[__i];
                    for (size_t __j = __i + 1; __j != __indices.size(); ++__j)
                        __1d_[__k] -= __stride_[__j] * (__size_[__j] - 1);
                    break;
                }
                else
                {
                    if (__i == 0)
                        return;
                    __indices[__i--] = 0;
                }
            }
        }
    }
}

_LIBCPP_END_NAMESPACE_STD
