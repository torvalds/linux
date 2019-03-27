/*===------------------------- movdirintrin.h ------------------------------===
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
#if !defined __X86INTRIN_H && !defined __IMMINTRIN_H
#error "Never use <movdirintrin.h> directly; include <x86intrin.h> instead."
#endif

#ifndef _MOVDIRINTRIN_H
#define _MOVDIRINTRIN_H

/* Move doubleword as direct store */
static __inline__ void
__attribute__((__always_inline__, __nodebug__,  __target__("movdiri")))
_directstoreu_u32 (void *__dst, unsigned int  __value)
{
  __builtin_ia32_directstore_u32((unsigned int *)__dst, (unsigned int)__value);
}

#ifdef __x86_64__

/* Move quadword as direct store */
static __inline__ void
__attribute__((__always_inline__, __nodebug__,  __target__("movdiri")))
_directstoreu_u64 (void *__dst, unsigned long __value)
{
  __builtin_ia32_directstore_u64((unsigned long *)__dst, __value);
}

#endif /* __x86_64__ */

/*
 * movdir64b - Move 64 bytes as direct store.
 * The destination must be 64 byte aligned, and the store is atomic.
 * The source address has no alignment requirement, and the load from
 * the source address is not atomic.
 */
static __inline__ void
__attribute__((__always_inline__, __nodebug__,  __target__("movdir64b")))
_movdir64b (void *__dst __attribute__((align_value(64))), const void *__src)
{
  __builtin_ia32_movdir64b(__dst, __src);
}

#endif /* _MOVDIRINTRIN_H */
