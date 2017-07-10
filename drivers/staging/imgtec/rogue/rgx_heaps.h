/*************************************************************************/ /*!
@File
@Title          RGX heap definitions
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
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

#if !defined(__RGX_HEAPS_H__)
#define __RGX_HEAPS_H__

#include "km/rgxdefs_km.h"
#include "log2.h"
#include "pvr_debug.h"

/* RGX Heap IDs, note: not all heaps are available to clients */
/* N.B.  Old heap identifiers are deprecated now that the old memory
   management is. New heap identifiers should be suitably renamed */
#define RGX_UNDEFINED_HEAP_ID					(~0LU)			/*!< RGX Undefined Heap ID */
#define RGX_GENERAL_SVM_HEAP_ID					0				/*!< RGX General SVM (shared virtual memory) Heap ID */
#define RGX_GENERAL_HEAP_ID						1				/*!< RGX General Heap ID */
#define RGX_GENERAL_NON4K_HEAP_ID				2				/*!< RGX General none-4K Heap ID */
#define RGX_RGNHDR_BRN_63142__ID				3				/*!< RGX General Heap ID */
#define RGX_PDSCODEDATA_HEAP_ID					4				/*!< RGX PDS Code/Data Heap ID */
#define RGX_USCCODE_HEAP_ID						5				/*!< RGX USC Code Heap ID */
#define RGX_FIRMWARE_HEAP_ID					6				/*!< RGX Firmware Heap ID */
#define RGX_TQ3DPARAMETERS_HEAP_ID				7				/*!< RGX Firmware Heap ID */
#define RGX_BIF_TILING_HEAP_1_ID				8				/*!< RGX BIF Tiling Heap 1 ID */
#define RGX_BIF_TILING_HEAP_2_ID				9				/*!< RGX BIF Tiling Heap 2 ID */
#define RGX_BIF_TILING_HEAP_3_ID				10				/*!< RGX BIF Tiling Heap 3 ID */
#define RGX_BIF_TILING_HEAP_4_ID				11				/*!< RGX BIF Tiling Heap 4 ID */
#define RGX_HWBRN37200_HEAP_ID					12				/*!< RGX HWBRN37200 */
#define RGX_DOPPLER_HEAP_ID						13				/*!< Doppler Heap ID */
#define RGX_DOPPLER_OVERFLOW_HEAP_ID			14				/*!< Doppler Overflow Heap ID */
#define RGX_SERVICES_SIGNALS_HEAP_ID			15				/*!< Services Signals Heap ID */
#define RGX_SIGNALS_HEAP_ID						16				/*!< Signals Heap ID */
#define RGX_TDM_TPU_YUV_COEFFS_HEAP_ID          17
#define RGX_GUEST_FIRMWARE_HEAP_ID				18				/*!< Additional OSIDs Firmware */
#define RGX_MAX_HEAP_ID     	(RGX_GUEST_FIRMWARE_HEAP_ID + RGXFW_NUM_OS)	/*!< Max Valid Heap ID */

/*
  Identify heaps by their names
*/
#define RGX_GENERAL_SVM_HEAP_IDENT		"General SVM"			/*!< RGX General SVM (shared virtual memory) Heap Identifier */
#define RGX_GENERAL_HEAP_IDENT 			"General"               /*!< RGX General Heap Identifier */
#define RGX_GENERAL_NON4K_HEAP_IDENT	"General NON-4K"        /*!< RGX General non-4K Heap Identifier */
#define RGX_RGNHDR_BRN_63142_HEAP_IDENT "RgnHdr BRN63142"       /*!< RGX RgnHdr BRN63142 Heap Identifier */
#define RGX_PDSCODEDATA_HEAP_IDENT 		"PDS Code and Data"     /*!< RGX PDS Code/Data Heap Identifier */
#define RGX_USCCODE_HEAP_IDENT			"USC Code"              /*!< RGX USC Code Heap Identifier */
#define RGX_TQ3DPARAMETERS_HEAP_IDENT	"TQ3DParameters"        /*!< RGX TQ 3D Parameters Heap Identifier */
#define RGX_BIF_TILING_HEAP_1_IDENT	    "BIF Tiling Heap l"	    /*!< RGX BIF Tiling Heap 1 identifier */
#define RGX_BIF_TILING_HEAP_2_IDENT	    "BIF Tiling Heap 2"	    /*!< RGX BIF Tiling Heap 2 identifier */
#define RGX_BIF_TILING_HEAP_3_IDENT	    "BIF Tiling Heap 3"	    /*!< RGX BIF Tiling Heap 3 identifier */
#define RGX_BIF_TILING_HEAP_4_IDENT	    "BIF Tiling Heap 4"	    /*!< RGX BIF Tiling Heap 4 identifier */
#define RGX_DOPPLER_HEAP_IDENT			"Doppler"				/*!< Doppler Heap Identifier */
#define RGX_DOPPLER_OVERFLOW_HEAP_IDENT	"Doppler Overflow"		/*!< Doppler Heap Identifier */
#define RGX_SERVICES_SIGNALS_HEAP_IDENT	"Services Signals"		/*!< Services Signals Heap Identifier */
#define RGX_SIGNALS_HEAP_IDENT	        "Signals"		        /*!< Signals Heap Identifier */
#define RGX_VISTEST_HEAP_IDENT			"VisTest"				/*!< VisTest heap */
#define RGX_TDM_TPU_YUV_COEFFS_HEAP_IDENT "TDM TPU YUV Coeffs"

/* BIF tiling heaps have specific buffer requirements based on their XStride
 * configuration. This is detailed in the BIF tiling documentation and ensures
 * that the bits swapped by the BIF tiling algorithm do not result in addresses
 * outside the allocated buffer. The representation here reflects the diagram
 * in the BIF tiling documentation for tiling mode '0'.
 *
 * For tiling mode '1', the overall tile size does not change, width increases
 * to 2^9 but the height drops to 2^3.
 * This means the RGX_BIF_TILING_HEAP_ALIGN_LOG2_FROM_XSTRIDE macro can be
 * used for both modes.
 *
 * Previous TILING_HEAP_STRIDE macros are retired in preference to storing an
 * alignment to stride factor, derived from the tiling mode, with the tiling
 * heap configuration data.
 *
 * XStride is defined for a platform in sysconfig.h, but the resulting
 * alignment and stride factor can be queried through the
 * PVRSRVGetHeapLog2ImportAlignmentAndTilingStrideFactor() API.
 * For reference:
 *   Log2BufferStride = Log2Alignment - Log2AlignmentToTilingStrideFactor
 */
#define RGX_BIF_TILING_HEAP_ALIGN_LOG2_FROM_XSTRIDE(X)       (4+X+1+8)
#define RGX_BIF_TILING_HEAP_LOG2_ALIGN_TO_STRIDE_BASE              (4)

/*
 *  Supported log2 page size values for RGX_GENERAL_NON_4K_HEAP_ID
 */
#define RGX_HEAP_4KB_PAGE_SHIFT					(12)
#define RGX_HEAP_16KB_PAGE_SHIFT				(14)
#define RGX_HEAP_64KB_PAGE_SHIFT				(16)
#define RGX_HEAP_256KB_PAGE_SHIFT				(18)
#define RGX_HEAP_1MB_PAGE_SHIFT					(20)
#define RGX_HEAP_2MB_PAGE_SHIFT					(21)

/* Takes a log2 page size parameter and calculates a suitable page size
 * for the RGX heaps. Returns 0 if parameter is wrong.*/
static INLINE IMG_UINT32 RGXHeapDerivePageSize(IMG_UINT32 uiLog2PageSize)
{
	IMG_BOOL bFound = IMG_FALSE;

	/* OS page shift must be at least RGX_HEAP_4KB_PAGE_SHIFT,
	 * max RGX_HEAP_2MB_PAGE_SHIFT, non-zero and a power of two*/
	if ( uiLog2PageSize == 0 ||
		(uiLog2PageSize < RGX_HEAP_4KB_PAGE_SHIFT) ||
		(uiLog2PageSize > RGX_HEAP_2MB_PAGE_SHIFT))
	{
		PVR_DPF((PVR_DBG_ERROR,
				"%s: Provided incompatible log2 page size %u",
				__FUNCTION__,
				uiLog2PageSize));
		PVR_ASSERT(0);
		return 0;
	}

	do
	{
		switch (uiLog2PageSize)
		{
			case RGX_HEAP_4KB_PAGE_SHIFT:
			case RGX_HEAP_16KB_PAGE_SHIFT:
			case RGX_HEAP_64KB_PAGE_SHIFT:
			case RGX_HEAP_256KB_PAGE_SHIFT:
			case RGX_HEAP_1MB_PAGE_SHIFT:
			case RGX_HEAP_2MB_PAGE_SHIFT:
				/* All good, RGX page size equals given page size
				 * => use it as default for heaps */
				bFound = IMG_TRUE;
				break;
			default:
				/* We have to fall back to a smaller device
				 * page size than given page size because there
				 * is no exact match for any supported size. */
				uiLog2PageSize -= 1;
				break;
		}
	} while (!bFound);

	return uiLog2PageSize;
}


#endif /* __RGX_HEAPS_H__ */

