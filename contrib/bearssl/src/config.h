/*
 * Copyright (c) 2016 Thomas Pornin <pornin@bolet.org>
 *
 * Permission is hereby granted, free of charge, to any person obtaining 
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be 
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, 
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND 
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef CONFIG_H__
#define CONFIG_H__

/*
 * This file contains compile-time flags that can override the
 * autodetection performed in relevant files. Each flag is a macro; it
 * deactivates the feature if defined to 0, activates it if defined to a
 * non-zero integer (normally 1). If the macro is not defined, then
 * autodetection applies.
 */

/*
 * When BR_64 is enabled, 64-bit integer types are assumed to be
 * efficient (i.e. the architecture has 64-bit registers and can
 * do 64-bit operations as fast as 32-bit operations).
 *
#define BR_64   1
 */

/*
 * When BR_LOMUL is enabled, then multiplications of 32-bit values whose
 * result are truncated to the low 32 bits are assumed to be
 * substantially more efficient than 32-bit multiplications that yield
 * 64-bit results. This is typically the case on low-end ARM Cortex M
 * systems (M0, M0+, M1, and arguably M3 and M4 as well).
 *
#define BR_LOMUL   1
 */

/*
 * When BR_SLOW_MUL is enabled, multiplications are assumed to be
 * substantially slow with regards to other integer operations, thus
 * making it worth to make more operations for a given task if it allows
 * using less multiplications.
 *
#define BR_SLOW_MUL   1
 */

/*
 * When BR_SLOW_MUL15 is enabled, short multplications (on 15-bit words)
 * are assumed to be substantially slow with regards to other integer
 * operations, thus making it worth to make more integer operations if
 * it allows using less multiplications.
 *
#define BR_SLOW_MUL15   1
 */

/*
 * When BR_CT_MUL31 is enabled, multiplications of 31-bit values (used
 * in the "i31" big integer implementation) use an alternate implementation
 * which is slower and larger than the normal multiplication, but should
 * ensure constant-time multiplications even on architectures where the
 * multiplication opcode takes a variable number of cycles to complete.
 *
#define BR_CT_MUL31   1
 */

/*
 * When BR_CT_MUL15 is enabled, multiplications of 15-bit values (held
 * in 32-bit words) use an alternate implementation which is slower and
 * larger than the normal multiplication, but should ensure
 * constant-time multiplications on most/all architectures where the
 * basic multiplication is not constant-time.
#define BR_CT_MUL15   1
 */

/*
 * When BR_NO_ARITH_SHIFT is enabled, arithmetic right shifts (with sign
 * extension) are performed with a sequence of operations which is bigger
 * and slower than a simple right shift on a signed value. This avoids
 * relying on an implementation-defined behaviour. However, most if not
 * all C compilers use sign extension for right shifts on signed values,
 * so this alternate macro is disabled by default.
#define BR_NO_ARITH_SHIFT   1
 */

/*
 * When BR_RDRAND is enabled, the SSL engine will use the RDRAND opcode
 * to automatically obtain quality randomness for seeding its internal
 * PRNG. Since that opcode is present only in recent x86 CPU, its
 * support is dynamically tested; if the current CPU does not support
 * it, then another random source will be used, such as /dev/urandom or
 * CryptGenRandom().
 *
#define BR_RDRAND   1
 */

/*
 * When BR_USE_URANDOM is enabled, the SSL engine will use /dev/urandom
 * to automatically obtain quality randomness for seedings its internal
 * PRNG.
 *
#define BR_USE_URANDOM   1
 */

/*
 * When BR_USE_WIN32_RAND is enabled, the SSL engine will use the Win32
 * (CryptoAPI) functions (CryptAcquireContext(), CryptGenRandom()...) to
 * automatically obtain quality randomness for seedings its internal PRNG.
 *
 * Note: if both BR_USE_URANDOM and BR_USE_WIN32_RAND are defined, the
 * former takes precedence.
 *
#define BR_USE_WIN32_RAND   1
 */

/*
 * When BR_USE_UNIX_TIME is enabled, the X.509 validation engine obtains
 * the current time from the OS by calling time(), and assuming that the
 * returned value (a 'time_t') is an integer that counts time in seconds
 * since the Unix Epoch (Jan 1st, 1970, 00:00 UTC).
 *
#define BR_USE_UNIX_TIME   1
 */

/*
 * When BR_USE_WIN32_TIME is enabled, the X.509 validation engine obtains
 * the current time from the OS by calling the Win32 function
 * GetSystemTimeAsFileTime().
 *
 * Note: if both BR_USE_UNIX_TIME and BR_USE_WIN32_TIME are defined, the
 * former takes precedence.
 *
#define BR_USE_WIN32_TIME   1
 */

/*
 * When BR_ARMEL_CORTEXM_GCC is enabled, some operations are replaced with
 * inline assembly which is shorter and/or faster. This should be used
 * only when all of the following are true:
 *   - target architecture is ARM in Thumb mode
 *   - target endianness is little-endian
 *   - compiler is GCC (or GCC-compatible for inline assembly syntax)
 *
 * This is meant for the low-end cores (Cortex M0, M0+, M1, M3).
 * Note: if BR_LOMUL is not explicitly enabled or disabled, then
 * enabling BR_ARMEL_CORTEXM_GCC also enables BR_LOMUL.
 *
#define BR_ARMEL_CORTEXM_GCC   1
 */

/*
 * When BR_AES_X86NI is enabled, the AES implementation using the x86 "NI"
 * instructions (dedicated AES opcodes) will be compiled. If this is not
 * enabled explicitly, then that AES implementation will be compiled only
 * if a compatible compiler is detected. If set explicitly to 0, the
 * implementation will not be compiled at all.
 *
#define BR_AES_X86NI   1
 */

/*
 * When BR_SSE2 is enabled, SSE2 intrinsics will be used for some
 * algorithm implementations that use them (e.g. chacha20_sse2). If this
 * is not enabled explicitly, then support for SSE2 intrinsics will be
 * automatically detected. If set explicitly to 0, then SSE2 code will
 * not be compiled at all.
 *
#define BR_SSE2   1
 */

/*
 * When BR_POWER8 is enabled, the AES implementation using the POWER ISA
 * 2.07 opcodes (available on POWER8 processors and later) is compiled.
 * If this is not enabled explicitly, then that implementation will be
 * compiled only if a compatible compiler is detected, _and_ the target
 * architecture is POWER8 or later.
 *
#define BR_POWER8   1
 */

/*
 * When BR_INT128 is enabled, then code using the 'unsigned __int64'
 * and 'unsigned __int128' types will be used to leverage 64x64->128
 * unsigned multiplications. This should work with GCC and compatible
 * compilers on 64-bit architectures.
 *
#define BR_INT128   1
 */

/*
 * When BR_UMUL128 is enabled, then code using the '_umul128()' and
 * '_addcarry_u64()' intrinsics will be used to implement 64x64->128
 * unsigned multiplications. This should work on Visual C on x64 systems.
 *
#define BR_UMUL128   1
 */

/*
 * When BR_LE_UNALIGNED is enabled, then the current architecture is
 * assumed to use little-endian encoding for integers, and to tolerate
 * unaligned accesses with no or minimal time penalty.
 *
#define BR_LE_UNALIGNED   1
 */

/*
 * When BR_BE_UNALIGNED is enabled, then the current architecture is
 * assumed to use big-endian encoding for integers, and to tolerate
 * unaligned accesses with no or minimal time penalty.
 *
#define BR_BE_UNALIGNED   1
 */

#endif
