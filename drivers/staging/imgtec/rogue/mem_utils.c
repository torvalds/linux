/*************************************************************************/ /*!
@File
@Title          Memory manipulation functions
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Memory related functions
@License        Dual MIT/GPLv2

The contents of this file are subject to the MIT license as set out below.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

Alternatively, the contents of this file may be used under the terms of
the GNU General Public License Version 2 ("GPL") in which case the provisions
of GPL are applicable instead of those above.

If you wish to allow use of your version of this file only under the terms of
GPL, and not to allow others to use your version of this file under the terms
of the MIT license, indicate your decision by deleting the provisions above
and replace them with the notice and other provisions required by GPL as set
out in the file called "GPL-COPYING" included in this distribution. If you do
not delete the provisions above, a recipient may use your version of this file
under the terms of either the MIT license or GPL.

This License is also included in this distribution in the file called
"MIT-COPYING".

EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/ /**************************************************************************/

/* This workaround is only *required* on ARM64. Avoid building or including
 * it by default on other architectures, unless the 'safe memcpy' test flag
 * is enabled. (The code should work on other architectures.)
 */

#if defined(__arm64__) || defined(__aarch64__) || defined (PVRSRV_DEVMEM_TEST_SAFE_MEMSETCPY)

/* NOTE: This C file is compiled with -ffreestanding to avoid pattern matching
 *       by the compiler to stdlib functions, and it must only use the below
 *       headers. Do not include any IMG or services headers in this file.
 */
#include <stddef.h>

/* Prototypes to suppress warnings in -ffreestanding mode */
void DeviceMemCopy(void *pvDst, const void *pvSrc, size_t uSize);
void DeviceMemSet(void *pvDst, unsigned char ui8Value, size_t uSize);

/* This file is only intended to be used on platforms which use GCC or Clang,
 * due to its requirement on __attribute__((vector_size(n))), typeof() and
 * __SIZEOF__ macros.
 */
#if defined(__GNUC__)

#define MIN(a, b) \
 ({__typeof(a) _a = (a); __typeof(b) _b = (b); _a > _b ? _b : _a;})

#if !defined(DEVICE_MEMSETCPY_ALIGN_IN_BYTES)
#define DEVICE_MEMSETCPY_ALIGN_IN_BYTES __SIZEOF_LONG__
#endif
#if DEVICE_MEMSETCPY_ALIGN_IN_BYTES % 2 != 0
#error "DEVICE_MEMSETCPY_ALIGN_IN_BYTES must be a power of 2"
#endif
#if DEVICE_MEMSETCPY_ALIGN_IN_BYTES < 4
#error "DEVICE_MEMSETCPY_ALIGN_IN_BYTES must be equal or greater than 4"
#endif

#if __SIZEOF_POINTER__ != __SIZEOF_LONG__
#error No support for architectures where void* and long are sized differently
#endif

#if   __SIZEOF_LONG__ >  DEVICE_MEMSETCPY_ALIGN_IN_BYTES
/* Meaningless, and harder to do correctly */
# error Cannot handle DEVICE_MEMSETCPY_ALIGN_IN_BYTES < sizeof(long)
typedef unsigned long block_t;
#elif __SIZEOF_LONG__ <= DEVICE_MEMSETCPY_ALIGN_IN_BYTES
typedef unsigned int block_t
	__attribute__((vector_size(DEVICE_MEMSETCPY_ALIGN_IN_BYTES)));
# if defined(__arm64__) || defined(__aarch64__)
#  if   DEVICE_MEMSETCPY_ALIGN_IN_BYTES == 8
#   define DEVICE_MEMSETCPY_ARM64
#   define REGSZ "w"
#   define REGCL "w"
#   define BVCLB "r"
#  elif DEVICE_MEMSETCPY_ALIGN_IN_BYTES == 16
#   define DEVICE_MEMSETCPY_ARM64
#   define REGSZ "x"
#   define REGCL "x"
#   define BVCLB "r"
#  elif DEVICE_MEMSETCPY_ALIGN_IN_BYTES == 32
#   if defined(__ARM_NEON_FP)
#    define DEVICE_MEMSETCPY_ARM64
#    define REGSZ "q"
#    define REGCL "v"
#    define BVCLB "w"
#   endif
#  endif
#  if defined(DEVICE_MEMSETCPY_ARM64)
#   if defined(DEVICE_MEMSETCPY_ARM64_NON_TEMPORAL)
#    define NSHLD() __asm__ ("dmb nshld")
#    define NSHST() __asm__ ("dmb nshst")
#    define LDP "ldnp"
#    define STP "stnp"
#   else
#    define NSHLD()
#    define NSHST()
#    define LDP "ldp"
#    define STP "stp"
#   endif
 typedef unsigned int block_half_t
	__attribute__((vector_size(DEVICE_MEMSETCPY_ALIGN_IN_BYTES / 2)));
#  endif
# endif
#endif

__attribute__((visibility("hidden")))
void DeviceMemCopy(void *pvDst, const void *pvSrc, size_t uSize)
{
	volatile const char *pcSrc = pvSrc;
	volatile char *pcDst = pvDst;
	size_t uPreambleBytes;
	int bBlockCopy = 0;

	size_t uSrcUnaligned = (size_t)pcSrc % sizeof(block_t);
	size_t uDstUnaligned = (size_t)pcDst % sizeof(block_t);

	if (!uSrcUnaligned && !uDstUnaligned)
	{
		/* Neither pointer is unaligned. Optimal case. */
		bBlockCopy = 1;
	}
	else
	{
		if (uSrcUnaligned == sizeof(int) && uDstUnaligned == sizeof(int))
		{
			/* Both pointers are at least 32-bit aligned, and we assume that
			 * the processor must handle all kinds of 32-bit load-stores.
			 * NOTE: Could we optimize this with a non-temporal version?
			 */
			if (uSize >= sizeof(int))
			{
				volatile int *piSrc = (int *)pcSrc;
				volatile int *piDst = (int *)pcDst;

				while (uSize >= sizeof(int))
				{
					*piDst++ = *piSrc++;
					uSize -= sizeof(int);
				}

				pcSrc = (char *)piSrc;
				pcDst = (char *)piDst;
			}
		}
		else if (uSrcUnaligned == uDstUnaligned)
		{
			/* Neither pointer is usefully aligned, but they are misaligned in
			 * the same way, so we can copy a preamble in a slow way, then
			 * optimize the rest.
			 */
			uPreambleBytes = MIN(sizeof(block_t) - uDstUnaligned, uSize);
			uSize -= uPreambleBytes;
			while (uPreambleBytes)
			{
				*pcDst++ = *pcSrc++;
				uPreambleBytes--;
			}

			bBlockCopy = 1;
		}
	}

	if (bBlockCopy && uSize >= sizeof(block_t))
	{
		volatile block_t *pSrc = (block_t *)pcSrc;
		volatile block_t *pDst = (block_t *)pcDst;

		NSHLD();

		while (uSize >= sizeof(block_t))
		{
#if defined(DEVICE_MEMSETCPY_ARM64)
			__asm__ (LDP " " REGSZ "0, " REGSZ "1, [%[pSrc]]\n\t"
			         STP " " REGSZ "0, " REGSZ "1, [%[pDst]]"
						:
						: [pSrc] "r" (pSrc), [pDst] "r" (pDst)
						: "memory", REGCL "0", REGCL "1");
#else
			*pDst = *pSrc;
#endif
			pDst++;
			pSrc++;
			uSize -= sizeof(block_t);
		}

		NSHST();

		pcSrc = (char *)pSrc;
		pcDst = (char *)pDst;
	}

	while (uSize)
	{
		*pcDst++ = *pcSrc++;
		uSize--;
	}
}

__attribute__((visibility("hidden")))
void DeviceMemSet(void *pvDst, unsigned char ui8Value, size_t uSize)
{
	volatile char *pcDst = pvDst;
	size_t uPreambleBytes;

	size_t uDstUnaligned = (size_t)pcDst % sizeof(block_t);

	if (uDstUnaligned)
	{
		uPreambleBytes = MIN(sizeof(block_t) - uDstUnaligned, uSize);
		uSize -= uPreambleBytes;
		while (uPreambleBytes)
		{
			*pcDst++ = ui8Value;
			uPreambleBytes--;
		}
	}

	if (uSize >= sizeof(block_t))
	{
		volatile block_t *pDst = (block_t *)pcDst;
#if defined(DEVICE_MEMSETCPY_ARM64)
		block_half_t bValue;
#else
		block_t bValue;
#endif
		size_t i;

		for (i = 0; i < sizeof(bValue) / sizeof(unsigned int); i++)
			bValue[i] = ui8Value << 24U |
			            ui8Value << 16U |
			            ui8Value <<  8U |
			            ui8Value;

		NSHLD();

		while (uSize >= sizeof(block_t))
		{
#if defined(DEVICE_MEMSETCPY_ARM64)
			__asm__ (STP " %" REGSZ "[bValue], %" REGSZ "[bValue], [%[pDst]]"
						:
						: [bValue] BVCLB (bValue), [pDst] "r" (pDst)
						: "memory");
#else
			*pDst = bValue;
#endif
			pDst++;
			uSize -= sizeof(block_t);
		}

		NSHST();

		pcDst = (char *)pDst;
	}

	while (uSize)
	{
		*pcDst++ = ui8Value;
		uSize--;
	}
}

#else /* !defined(__GNUC__) */

/* Potentially very slow (but safe) fallbacks for non-GNU C compilers */

void DeviceMemCopy(void *pvDst, const void *pvSrc, size_t uSize)
{
	volatile const char *pcSrc = pvSrc;
	volatile char *pcDst = pvDst;

	while (uSize)
	{
		*pcDst++ = *pcSrc++;
		uSize--;
	}
}

void DeviceMemSet(void *pvDst, unsigned char ui8Value, size_t uSize)
{
	volatile char *pcDst = pvDst;

	while (uSize)
	{
		*pcDst++ = ui8Value;
		uSize--;
	}
}

#endif /* !defined(__GNUC__) */

#endif /* defined(__arm64__) || defined(__aarch64__) || defined (PVRSRV_DEVMEM_TEST_SAFE_MEMSETCPY) */
