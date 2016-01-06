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


#if defined(__KERNEL__)
#include "osfunc.h"
#include <linux/string.h>
#else
#include "services.h"
#include <string.h>
#endif

#if (defined(__arm64__) || defined(__aarch64__) || defined (PVRSRV_DEVMEM_SAFE_MEMSETCPY)) && !defined(__QNXNTO__)

#define ZERO_BUF_SIZE 1024

#if defined(__GNUC__)
#define PVRSRV_MEM_ALIGN __attribute__ ((aligned (0x8)))
#define PVRSRV_MEM_64BIT_ALIGN_MASK (0x7)
#define PVRSRV_MEM_32BIT_ALIGN_MASK (0x3)
#else
#error "PVRSRV Alignment macros need to be defined for this compiler"
#endif

/******************************************************************************
 Function Name      : OSDeviceMemCopy / PVRSRVDeviceMemCopy
 Inputs             : pvDst - pointer to the destination memory region
                      pvSrc - pointer to the source memory region
                      uiSize - size of the memory region that will be copied
 Outputs            :
 Returns            :
 Description        : This is a counterpart of standard memcpy function for
                      uncached memory. The reason for this function is that
                      when uncached memory is used and the pointers are not
                      64-bit aligned on Aarch64 architecture a memory
                      exception will be thrown. This applies both to user and
                      kernel space.
******************************************************************************/
IMG_EXPORT IMG_VOID PVRSRVDeviceMemCopy(
        IMG_VOID* pvDst,
        const IMG_VOID* pvSrc,
        IMG_SIZE_T uiSize)
{
	/* Use volatile to avoid compiler optimisations */
	volatile IMG_BYTE * pbySrc = (IMG_BYTE*)pvSrc;
	volatile IMG_BYTE * pbyDst = (IMG_BYTE*)pvDst;
	IMG_SIZE_T uiTailSize = uiSize;

	/* If both pointers have same alignment we can optimise. */
	if (((IMG_UINTPTR_T)pbySrc & PVRSRV_MEM_64BIT_ALIGN_MASK)
	        == ((IMG_UINTPTR_T)pbyDst & PVRSRV_MEM_64BIT_ALIGN_MASK))
	{
		IMG_SIZE_T uiAlignedSize;
		IMG_UINT uiHeadSize;

		uiHeadSize = (sizeof(void *)
		        - ((IMG_UINTPTR_T)pbySrc & PVRSRV_MEM_64BIT_ALIGN_MASK))
		        & PVRSRV_MEM_64BIT_ALIGN_MASK;

		/* For 64bit aligned pointers we will almost always (not if uiSize is 0)
		 * go in and use memcpy if the size is large enough. For other aligned
		 * pointers if size is large enough we will copy first few bytes to
		 * align those pointers to 64bit then use memcpy and after that copy
		 * remaining bytes.
		 * If uiSize is less then uiHeadSize we will skip to byte-by-byte
		 * copy since we can't use memcpy in such case. */
		if (uiSize > uiHeadSize)
		{
			uiSize -= uiHeadSize;
			uiTailSize = uiSize & PVRSRV_MEM_64BIT_ALIGN_MASK;
			uiAlignedSize = uiSize - uiTailSize;

			/* Copy few leading bytes to align pointer to 64bit boundary. */
			while (uiHeadSize--)
			{
				*pbyDst++ = *pbySrc++;
			}

			/* here pointers are already 64bit aligned so we can use memcpy. */
			memcpy((IMG_VOID*)pbyDst, (IMG_VOID*)pbySrc, uiAlignedSize);

			/* skip over copied data */
			pbyDst += uiAlignedSize;
			pbySrc += uiAlignedSize;
		}
	}
	/* If pointers are 32bit aligned but not aligned in relation to each
	 * other.*/
	else if ((((IMG_UINTPTR_T)pbySrc | (IMG_UINTPTR_T)pbyDst)
	        & PVRSRV_MEM_32BIT_ALIGN_MASK) == 0)
	{
		volatile IMG_UINT32 * pui32Src = (IMG_UINT32*)pbySrc;
		volatile IMG_UINT32 * pui32Dst = (IMG_UINT32*)pbyDst;
		IMG_SIZE_T uiAlignedSize;

		uiTailSize = uiSize & PVRSRV_MEM_32BIT_ALIGN_MASK;
		uiAlignedSize = uiSize - uiTailSize;

		/* do the 4 byte copy */
		uiSize = uiSize >> 2;
		while (uiSize--)
		{
			*pui32Dst++ = *pui32Src++;
		}

		pbyDst += uiAlignedSize;
		pbySrc += uiAlignedSize;
	}

	/* Copy either remaining memory if optimisation was performed but
	 * size was not aligned or all memory if we need to. */
	while (uiTailSize--)
	{
		*pbyDst++ = *pbySrc++;
	}

}

/******************************************************************************
 Function Name      : OSDeviceMemSet / PVRSRVDeviceMemSet
 Inputs             : pvDest - pointer to destination memory
                      ui8Value - the 'set' value
                      uiSize - size of the memory block
 Outputs            :
 Returns            :
 Description        : This is a counterpart of standard memset function for
                      uncached memory. The reason for this function is that
                      when uncached memory is used and the pointer is not
                      64-bit aligned on Aarch64 architecture an memory
                      exception will be thrown. This applies both to user and
                      kernel space.
******************************************************************************/
IMG_EXPORT IMG_VOID PVRSRVDeviceMemSet(
        IMG_VOID *pvDest,
        IMG_UINT8 ui8Value,
        IMG_SIZE_T uiSize)
{
	/* Use volatile to avoid compiler optimisations */
	volatile IMG_BYTE * pbyDst = (IMG_BYTE*)pvDest;
	static IMG_BYTE gZeroBuf[ZERO_BUF_SIZE] PVRSRV_MEM_ALIGN = { 0 };

	/* Run workaround if one of the address or size is not aligned, or
	 * we are zeroing */
	if ((ui8Value == 0) || ((((IMG_SIZE_T)pbyDst | uiSize) & PVRSRV_MEM_64BIT_ALIGN_MASK) != 0))
	{
		IMG_UINT32 uiTailSize;

		/* Buffer address unaligned */
		if ((IMG_SIZE_T)pbyDst & PVRSRV_MEM_64BIT_ALIGN_MASK)
		{
			/* Increment the buffer pointer */
			for (; uiSize > 0 && ((IMG_SIZE_T)pbyDst & PVRSRV_MEM_64BIT_ALIGN_MASK); uiSize--)
			{
				*pbyDst++ = ui8Value;
			}
			/* Did loop stop because size is zero? */
			if (uiSize == 0) return;
		}

		/* Set the remaining part of the buffer */
		if (ui8Value)
		{
			/* Non-zero set */
			uiTailSize = (uiSize & PVRSRV_MEM_64BIT_ALIGN_MASK);

			memset((IMG_VOID*) pbyDst, (IMG_INT) ui8Value, (size_t) uiSize-uiTailSize);
			pbyDst += uiSize-uiTailSize;
		}
		else
		{
			/* Zero set */
			uiTailSize = (uiSize & PVRSRV_MEM_64BIT_ALIGN_MASK);
			uiSize -= uiTailSize;

			while (uiSize > 1024)
			{
				memcpy((IMG_VOID*) pbyDst, gZeroBuf, (size_t) ZERO_BUF_SIZE);
				pbyDst +=ZERO_BUF_SIZE;
				uiSize -= ZERO_BUF_SIZE;
			}
			memcpy((IMG_VOID*) pbyDst, gZeroBuf, (size_t) uiSize);
			pbyDst += uiSize;
		}

		/* Handle any tail bytes, loop skipped in tail is 0 */
		for (; uiTailSize > 0; uiTailSize--)
		{
			*pbyDst++ = ui8Value;
		}
	}
	/* Alignment fine, non-zero set, no need to work around device memory
	 * use with ARM64 libc */
	else
	{
		memset(pvDest, (IMG_INT) ui8Value, (size_t) uiSize);
	}
}

#else /* (defined(__arm64__) || defined(__aarch64__) || defined (PVRSRV_DEVMEM_SAFE_MEMSETCPY)) && !defined(__QNXNTO__) */

IMG_EXPORT IMG_VOID PVRSRVDeviceMemCopy(
        IMG_VOID*       pvDst,
        const IMG_VOID* pvSrc,
        IMG_SIZE_T      uiSize)
{
	memcpy(pvDst, pvSrc, uiSize);
}

IMG_EXPORT IMG_VOID PVRSRVDeviceMemSet(
        IMG_VOID *pvDest,
        IMG_UINT8 ui8Value,
        IMG_SIZE_T uiSize)
{
	memset(pvDest, ui8Value, uiSize);
}

#endif /* (defined(__arm64__) || defined(__aarch64__) || defined (PVRSRV_DEVMEM_SAFE_MEMSETCPY)) && !defined(__QNXNTO__) */
