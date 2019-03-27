/*===-- __clang_cuda_complex_builtins - CUDA impls of runtime complex fns ---===
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 *===-----------------------------------------------------------------------===
 */

#ifndef __CLANG_CUDA_COMPLEX_BUILTINS
#define __CLANG_CUDA_COMPLEX_BUILTINS

// This header defines __muldc3, __mulsc3, __divdc3, and __divsc3.  These are
// libgcc functions that clang assumes are available when compiling c99 complex
// operations.  (These implementations come from libc++, and have been modified
// to work with CUDA.)

extern "C" inline __device__ double _Complex __muldc3(double __a, double __b,
                                                      double __c, double __d) {
  double __ac = __a * __c;
  double __bd = __b * __d;
  double __ad = __a * __d;
  double __bc = __b * __c;
  double _Complex z;
  __real__(z) = __ac - __bd;
  __imag__(z) = __ad + __bc;
  if (std::isnan(__real__(z)) && std::isnan(__imag__(z))) {
    int __recalc = 0;
    if (std::isinf(__a) || std::isinf(__b)) {
      __a = std::copysign(std::isinf(__a) ? 1 : 0, __a);
      __b = std::copysign(std::isinf(__b) ? 1 : 0, __b);
      if (std::isnan(__c))
        __c = std::copysign(0, __c);
      if (std::isnan(__d))
        __d = std::copysign(0, __d);
      __recalc = 1;
    }
    if (std::isinf(__c) || std::isinf(__d)) {
      __c = std::copysign(std::isinf(__c) ? 1 : 0, __c);
      __d = std::copysign(std::isinf(__d) ? 1 : 0, __d);
      if (std::isnan(__a))
        __a = std::copysign(0, __a);
      if (std::isnan(__b))
        __b = std::copysign(0, __b);
      __recalc = 1;
    }
    if (!__recalc && (std::isinf(__ac) || std::isinf(__bd) ||
                      std::isinf(__ad) || std::isinf(__bc))) {
      if (std::isnan(__a))
        __a = std::copysign(0, __a);
      if (std::isnan(__b))
        __b = std::copysign(0, __b);
      if (std::isnan(__c))
        __c = std::copysign(0, __c);
      if (std::isnan(__d))
        __d = std::copysign(0, __d);
      __recalc = 1;
    }
    if (__recalc) {
      // Can't use std::numeric_limits<double>::infinity() -- that doesn't have
      // a device overload (and isn't constexpr before C++11, naturally).
      __real__(z) = __builtin_huge_valf() * (__a * __c - __b * __d);
      __imag__(z) = __builtin_huge_valf() * (__a * __d + __b * __c);
    }
  }
  return z;
}

extern "C" inline __device__ float _Complex __mulsc3(float __a, float __b,
                                                     float __c, float __d) {
  float __ac = __a * __c;
  float __bd = __b * __d;
  float __ad = __a * __d;
  float __bc = __b * __c;
  float _Complex z;
  __real__(z) = __ac - __bd;
  __imag__(z) = __ad + __bc;
  if (std::isnan(__real__(z)) && std::isnan(__imag__(z))) {
    int __recalc = 0;
    if (std::isinf(__a) || std::isinf(__b)) {
      __a = std::copysign(std::isinf(__a) ? 1 : 0, __a);
      __b = std::copysign(std::isinf(__b) ? 1 : 0, __b);
      if (std::isnan(__c))
        __c = std::copysign(0, __c);
      if (std::isnan(__d))
        __d = std::copysign(0, __d);
      __recalc = 1;
    }
    if (std::isinf(__c) || std::isinf(__d)) {
      __c = std::copysign(std::isinf(__c) ? 1 : 0, __c);
      __d = std::copysign(std::isinf(__d) ? 1 : 0, __d);
      if (std::isnan(__a))
        __a = std::copysign(0, __a);
      if (std::isnan(__b))
        __b = std::copysign(0, __b);
      __recalc = 1;
    }
    if (!__recalc && (std::isinf(__ac) || std::isinf(__bd) ||
                      std::isinf(__ad) || std::isinf(__bc))) {
      if (std::isnan(__a))
        __a = std::copysign(0, __a);
      if (std::isnan(__b))
        __b = std::copysign(0, __b);
      if (std::isnan(__c))
        __c = std::copysign(0, __c);
      if (std::isnan(__d))
        __d = std::copysign(0, __d);
      __recalc = 1;
    }
    if (__recalc) {
      __real__(z) = __builtin_huge_valf() * (__a * __c - __b * __d);
      __imag__(z) = __builtin_huge_valf() * (__a * __d + __b * __c);
    }
  }
  return z;
}

extern "C" inline __device__ double _Complex __divdc3(double __a, double __b,
                                                      double __c, double __d) {
  int __ilogbw = 0;
  // Can't use std::max, because that's defined in <algorithm>, and we don't
  // want to pull that in for every compile.  The CUDA headers define
  // ::max(float, float) and ::max(double, double), which is sufficient for us.
  double __logbw = std::logb(max(std::abs(__c), std::abs(__d)));
  if (std::isfinite(__logbw)) {
    __ilogbw = (int)__logbw;
    __c = std::scalbn(__c, -__ilogbw);
    __d = std::scalbn(__d, -__ilogbw);
  }
  double __denom = __c * __c + __d * __d;
  double _Complex z;
  __real__(z) = std::scalbn((__a * __c + __b * __d) / __denom, -__ilogbw);
  __imag__(z) = std::scalbn((__b * __c - __a * __d) / __denom, -__ilogbw);
  if (std::isnan(__real__(z)) && std::isnan(__imag__(z))) {
    if ((__denom == 0.0) && (!std::isnan(__a) || !std::isnan(__b))) {
      __real__(z) = std::copysign(__builtin_huge_valf(), __c) * __a;
      __imag__(z) = std::copysign(__builtin_huge_valf(), __c) * __b;
    } else if ((std::isinf(__a) || std::isinf(__b)) && std::isfinite(__c) &&
               std::isfinite(__d)) {
      __a = std::copysign(std::isinf(__a) ? 1.0 : 0.0, __a);
      __b = std::copysign(std::isinf(__b) ? 1.0 : 0.0, __b);
      __real__(z) = __builtin_huge_valf() * (__a * __c + __b * __d);
      __imag__(z) = __builtin_huge_valf() * (__b * __c - __a * __d);
    } else if (std::isinf(__logbw) && __logbw > 0.0 && std::isfinite(__a) &&
               std::isfinite(__b)) {
      __c = std::copysign(std::isinf(__c) ? 1.0 : 0.0, __c);
      __d = std::copysign(std::isinf(__d) ? 1.0 : 0.0, __d);
      __real__(z) = 0.0 * (__a * __c + __b * __d);
      __imag__(z) = 0.0 * (__b * __c - __a * __d);
    }
  }
  return z;
}

extern "C" inline __device__ float _Complex __divsc3(float __a, float __b,
                                                     float __c, float __d) {
  int __ilogbw = 0;
  float __logbw = std::logb(max(std::abs(__c), std::abs(__d)));
  if (std::isfinite(__logbw)) {
    __ilogbw = (int)__logbw;
    __c = std::scalbn(__c, -__ilogbw);
    __d = std::scalbn(__d, -__ilogbw);
  }
  float __denom = __c * __c + __d * __d;
  float _Complex z;
  __real__(z) = std::scalbn((__a * __c + __b * __d) / __denom, -__ilogbw);
  __imag__(z) = std::scalbn((__b * __c - __a * __d) / __denom, -__ilogbw);
  if (std::isnan(__real__(z)) && std::isnan(__imag__(z))) {
    if ((__denom == 0) && (!std::isnan(__a) || !std::isnan(__b))) {
      __real__(z) = std::copysign(__builtin_huge_valf(), __c) * __a;
      __imag__(z) = std::copysign(__builtin_huge_valf(), __c) * __b;
    } else if ((std::isinf(__a) || std::isinf(__b)) && std::isfinite(__c) &&
               std::isfinite(__d)) {
      __a = std::copysign(std::isinf(__a) ? 1 : 0, __a);
      __b = std::copysign(std::isinf(__b) ? 1 : 0, __b);
      __real__(z) = __builtin_huge_valf() * (__a * __c + __b * __d);
      __imag__(z) = __builtin_huge_valf() * (__b * __c - __a * __d);
    } else if (std::isinf(__logbw) && __logbw > 0 && std::isfinite(__a) &&
               std::isfinite(__b)) {
      __c = std::copysign(std::isinf(__c) ? 1 : 0, __c);
      __d = std::copysign(std::isinf(__d) ? 1 : 0, __d);
      __real__(z) = 0 * (__a * __c + __b * __d);
      __imag__(z) = 0 * (__b * __c - __a * __d);
    }
  }
  return z;
}

#endif // __CLANG_CUDA_COMPLEX_BUILTINS
