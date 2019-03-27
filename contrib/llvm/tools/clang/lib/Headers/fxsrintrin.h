/*===---- fxsrintrin.h - FXSR intrinsic ------------------------------------===
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
#error "Never use <fxsrintrin.h> directly; include <immintrin.h> instead."
#endif

#ifndef __FXSRINTRIN_H
#define __FXSRINTRIN_H

#define __DEFAULT_FN_ATTRS __attribute__((__always_inline__, __nodebug__,  __target__("fxsr")))

/// Saves the XMM, MMX, MXCSR and x87 FPU registers into a 512-byte
///    memory region pointed to by the input parameter \a __p.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> FXSAVE </c> instruction.
///
/// \param __p
///    A pointer to a 512-byte memory region. The beginning of this memory
///    region should be aligned on a 16-byte boundary.
static __inline__ void __DEFAULT_FN_ATTRS
_fxsave(void *__p)
{
  __builtin_ia32_fxsave(__p);
}

/// Restores the XMM, MMX, MXCSR and x87 FPU registers from the 512-byte
///    memory region pointed to by the input parameter \a __p. The contents of
///    this memory region should have been written to by a previous \c _fxsave
///    or \c _fxsave64 intrinsic.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> FXRSTOR </c> instruction.
///
/// \param __p
///    A pointer to a 512-byte memory region. The beginning of this memory
///    region should be aligned on a 16-byte boundary.
static __inline__ void __DEFAULT_FN_ATTRS
_fxrstor(void *__p)
{
  __builtin_ia32_fxrstor(__p);
}

#ifdef __x86_64__
/// Saves the XMM, MMX, MXCSR and x87 FPU registers into a 512-byte
///    memory region pointed to by the input parameter \a __p.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> FXSAVE64 </c> instruction.
///
/// \param __p
///    A pointer to a 512-byte memory region. The beginning of this memory
///    region should be aligned on a 16-byte boundary.
static __inline__ void __DEFAULT_FN_ATTRS
_fxsave64(void *__p)
{
  __builtin_ia32_fxsave64(__p);
}

/// Restores the XMM, MMX, MXCSR and x87 FPU registers from the 512-byte
///    memory region pointed to by the input parameter \a __p. The contents of
///    this memory region should have been written to by a previous \c _fxsave
///    or \c _fxsave64 intrinsic.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> FXRSTOR64 </c> instruction.
///
/// \param __p
///    A pointer to a 512-byte memory region. The beginning of this memory
///    region should be aligned on a 16-byte boundary.
static __inline__ void __DEFAULT_FN_ATTRS
_fxrstor64(void *__p)
{
  __builtin_ia32_fxrstor64(__p);
}
#endif

#undef __DEFAULT_FN_ATTRS

#endif
