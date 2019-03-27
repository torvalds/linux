/* ===-------- vadefs.h ---------------------------------------------------===
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

/* Only include this if we are aiming for MSVC compatibility. */
#ifndef _MSC_VER
#include_next <vadefs.h>
#else

#ifndef __clang_vadefs_h
#define __clang_vadefs_h

#include_next <vadefs.h>

/* Override macros from vadefs.h with definitions that work with Clang. */
#ifdef _crt_va_start
#undef _crt_va_start
#define _crt_va_start(ap, param) __builtin_va_start(ap, param)
#endif
#ifdef _crt_va_end
#undef _crt_va_end
#define _crt_va_end(ap)          __builtin_va_end(ap)
#endif
#ifdef _crt_va_arg
#undef _crt_va_arg
#define _crt_va_arg(ap, type)    __builtin_va_arg(ap, type)
#endif

/* VS 2015 switched to double underscore names, which is an improvement, but now
 * we have to intercept those names too.
 */
#ifdef __crt_va_start
#undef __crt_va_start
#define __crt_va_start(ap, param) __builtin_va_start(ap, param)
#endif
#ifdef __crt_va_end
#undef __crt_va_end
#define __crt_va_end(ap)          __builtin_va_end(ap)
#endif
#ifdef __crt_va_arg
#undef __crt_va_arg
#define __crt_va_arg(ap, type)    __builtin_va_arg(ap, type)
#endif

#endif
#endif
