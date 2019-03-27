/*===---- mm_malloc.h - Allocating and Freeing Aligned Memory Blocks -------===
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

#ifndef __MM_MALLOC_H
#define __MM_MALLOC_H

#include <stdlib.h>

#ifdef _WIN32
#include <malloc.h>
#else
#ifndef __cplusplus
extern int posix_memalign(void **__memptr, size_t __alignment, size_t __size);
#else
// Some systems (e.g. those with GNU libc) declare posix_memalign with an
// exception specifier. Via an "egregious workaround" in
// Sema::CheckEquivalentExceptionSpec, Clang accepts the following as a valid
// redeclaration of glibc's declaration.
extern "C" int posix_memalign(void **__memptr, size_t __alignment, size_t __size);
#endif
#endif

#if !(defined(_WIN32) && defined(_mm_malloc))
static __inline__ void *__attribute__((__always_inline__, __nodebug__,
                                       __malloc__))
_mm_malloc(size_t __size, size_t __align)
{
  if (__align == 1) {
    return malloc(__size);
  }

  if (!(__align & (__align - 1)) && __align < sizeof(void *))
    __align = sizeof(void *);

  void *__mallocedMemory;
#if defined(__MINGW32__)
  __mallocedMemory = __mingw_aligned_malloc(__size, __align);
#elif defined(_WIN32)
  __mallocedMemory = _aligned_malloc(__size, __align);
#else
  if (posix_memalign(&__mallocedMemory, __align, __size))
    return 0;
#endif

  return __mallocedMemory;
}

static __inline__ void __attribute__((__always_inline__, __nodebug__))
_mm_free(void *__p)
{
  free(__p);
}
#endif

#endif /* __MM_MALLOC_H */
