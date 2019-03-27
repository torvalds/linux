/*===------------- avx512pfintrin.h - PF intrinsics ------------------------===
 *
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
#ifndef __IMMINTRIN_H
#error "Never use <avx512pfintrin.h> directly; include <immintrin.h> instead."
#endif

#ifndef __AVX512PFINTRIN_H
#define __AVX512PFINTRIN_H

/* Define the default attributes for the functions in this file. */
#define __DEFAULT_FN_ATTRS __attribute__((__always_inline__, __nodebug__, __target__("avx512pf")))

#define _mm512_mask_prefetch_i32gather_pd(index, mask, addr, scale, hint) \
  __builtin_ia32_gatherpfdpd((__mmask8)(mask), (__v8si)(__m256i)(index), \
                             (void const *)(addr), (int)(scale), \
                             (int)(hint))

#define _mm512_prefetch_i32gather_pd(index, addr, scale, hint) \
  __builtin_ia32_gatherpfdpd((__mmask8) -1, (__v8si)(__m256i)(index), \
                             (void const *)(addr), (int)(scale), \
                             (int)(hint))

#define _mm512_mask_prefetch_i32gather_ps(index, mask, addr, scale, hint) \
  __builtin_ia32_gatherpfdps((__mmask16)(mask), \
                             (__v16si)(__m512i)(index), (void const *)(addr), \
                             (int)(scale), (int)(hint))

#define _mm512_prefetch_i32gather_ps(index, addr, scale, hint) \
  __builtin_ia32_gatherpfdps((__mmask16) -1, \
                             (__v16si)(__m512i)(index), (void const *)(addr), \
                             (int)(scale), (int)(hint))

#define _mm512_mask_prefetch_i64gather_pd(index, mask, addr, scale, hint) \
  __builtin_ia32_gatherpfqpd((__mmask8)(mask), (__v8di)(__m512i)(index), \
                             (void const *)(addr), (int)(scale), \
                             (int)(hint))

#define _mm512_prefetch_i64gather_pd(index, addr, scale, hint) \
  __builtin_ia32_gatherpfqpd((__mmask8) -1, (__v8di)(__m512i)(index), \
                             (void const *)(addr), (int)(scale), \
                             (int)(hint))

#define _mm512_mask_prefetch_i64gather_ps(index, mask, addr, scale, hint) \
  __builtin_ia32_gatherpfqps((__mmask8)(mask), (__v8di)(__m512i)(index), \
                             (void const *)(addr), (int)(scale), (int)(hint))

#define _mm512_prefetch_i64gather_ps(index, addr, scale, hint) \
  __builtin_ia32_gatherpfqps((__mmask8) -1, (__v8di)(__m512i)(index), \
                             (void const *)(addr), (int)(scale), (int)(hint))

#define _mm512_prefetch_i32scatter_pd(addr, index, scale, hint) \
  __builtin_ia32_scatterpfdpd((__mmask8)-1, (__v8si)(__m256i)(index), \
                              (void *)(addr), (int)(scale), \
                              (int)(hint))

#define _mm512_mask_prefetch_i32scatter_pd(addr, mask, index, scale, hint) \
  __builtin_ia32_scatterpfdpd((__mmask8)(mask), (__v8si)(__m256i)(index), \
                              (void *)(addr), (int)(scale), \
                              (int)(hint))

#define _mm512_prefetch_i32scatter_ps(addr, index, scale, hint) \
  __builtin_ia32_scatterpfdps((__mmask16)-1, (__v16si)(__m512i)(index), \
                              (void *)(addr), (int)(scale), (int)(hint))

#define _mm512_mask_prefetch_i32scatter_ps(addr, mask, index, scale, hint) \
  __builtin_ia32_scatterpfdps((__mmask16)(mask), \
                              (__v16si)(__m512i)(index), (void *)(addr), \
                              (int)(scale), (int)(hint))

#define _mm512_prefetch_i64scatter_pd(addr, index, scale, hint) \
  __builtin_ia32_scatterpfqpd((__mmask8)-1, (__v8di)(__m512i)(index), \
                              (void *)(addr), (int)(scale), \
                              (int)(hint))

#define _mm512_mask_prefetch_i64scatter_pd(addr, mask, index, scale, hint) \
  __builtin_ia32_scatterpfqpd((__mmask8)(mask), (__v8di)(__m512i)(index), \
                              (void *)(addr), (int)(scale), \
                              (int)(hint))

#define _mm512_prefetch_i64scatter_ps(addr, index, scale, hint) \
  __builtin_ia32_scatterpfqps((__mmask8)-1, (__v8di)(__m512i)(index), \
                              (void *)(addr), (int)(scale), (int)(hint))

#define _mm512_mask_prefetch_i64scatter_ps(addr, mask, index, scale, hint) \
  __builtin_ia32_scatterpfqps((__mmask8)(mask), (__v8di)(__m512i)(index), \
                              (void *)(addr), (int)(scale), (int)(hint))

#undef __DEFAULT_FN_ATTRS

#endif
