/*************************************************************************/ /*!
@File           device_connection.h
@Title
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description
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

#if !defined(DEVICE_CONNECTION_H)
#define DEVICE_CONNECTION_H

#include "img_types.h"
#include "img_defs.h"

#if defined(__KERNEL__)
typedef struct _PVRSRV_DEVICE_NODE_ *SHARED_DEV_CONNECTION;
#else
#include "connection.h"
typedef const struct PVRSRV_DEV_CONNECTION_TAG *SHARED_DEV_CONNECTION;
#endif

/******************************************************************************
 * Device capability flags and masks
 *
 * Following bitmask shows allocated ranges and values for our device
 * capability settings:
 *
 * 31 27  23  19  15  11   7   3  0
 * |...|...|...|...|...|...|...|...
 *                               ** CACHE_COHERENT                   [0x1..0x2]
 *                                x  PVRSRV_CACHE_COHERENT_DEVICE_FLAG
 *                               x.  PVRSRV_CACHE_COHERENT_CPU_FLAG
 *                             *... NONMAPPABLE_MEMORY                    [0x8]
 *                             x...  PVRSRV_NONMAPPABLE_MEMORY_PRESENT_FLAG
 *                            *.... PDUMP_IS_RECORDING                   [0x10]
 *                            x....  PVRSRV_PDUMP_IS_RECORDING
 *                      ***........ DEVMEM_SVM_ALLOC             [0x100..0x400]
 *                        x........  PVRSRV_DEVMEM_SVM_ALLOC_UNSUPPORTED
 *                       x.........  PVRSRV_DEVMEM_SVM_ALLOC_SUPPORTED
 *                      x..........  PVRSRV_DEVMEM_SVM_ALLOC_CANFAIL
 *                     *........... FBCDC_V3_1             [0x800]
 *                     x...........  FBCDC_V3_1_USED
 *                    *............ PVRSRV_SYSTEM_DMA
 *                    x............  PVRSRV_SYSTEM_DMA_USED
 *                   *............. TFBC_LOSSY_GROUP
 *                   x.............  TFBC_LOSSY_GROUP_1
 * |...|...|...|...|...|...|...|...
 *****************************************************************************/

/* Flag to be passed over the bridge during connection stating whether CPU cache coherent is available*/
#define PVRSRV_CACHE_COHERENT_SHIFT (0)
#define	PVRSRV_CACHE_COHERENT_DEVICE_FLAG (1U << PVRSRV_CACHE_COHERENT_SHIFT)
#define	PVRSRV_CACHE_COHERENT_CPU_FLAG (2U << PVRSRV_CACHE_COHERENT_SHIFT)
#define	PVRSRV_CACHE_COHERENT_EMULATE_FLAG (4U << PVRSRV_CACHE_COHERENT_SHIFT)
#define PVRSRV_CACHE_COHERENT_MASK (7U << PVRSRV_CACHE_COHERENT_SHIFT)

/* Flag to be passed over the bridge during connection stating whether CPU non-mappable memory is present */
#define PVRSRV_NONMAPPABLE_MEMORY_PRESENT_SHIFT (7)
#define PVRSRV_NONMAPPABLE_MEMORY_PRESENT_FLAG (1U << PVRSRV_NONMAPPABLE_MEMORY_PRESENT_SHIFT)

/* Flag to be passed over the bridge to indicate PDump activity */
#define PVRSRV_PDUMP_IS_RECORDING_SHIFT (4)
#define PVRSRV_PDUMP_IS_RECORDING (1U << PVRSRV_PDUMP_IS_RECORDING_SHIFT)

/* Flag to be passed over the bridge during connection stating SVM allocation availability */
#define PVRSRV_DEVMEM_SVM_ALLOC_SHIFT (8)
#define PVRSRV_DEVMEM_SVM_ALLOC_UNSUPPORTED (1U << PVRSRV_DEVMEM_SVM_ALLOC_SHIFT)
#define PVRSRV_DEVMEM_SVM_ALLOC_SUPPORTED (2U << PVRSRV_DEVMEM_SVM_ALLOC_SHIFT)
#define PVRSRV_DEVMEM_SVM_ALLOC_CANFAIL (4U << PVRSRV_DEVMEM_SVM_ALLOC_SHIFT)

/* Flag to be passed over the bridge during connection stating whether GPU uses FBCDC v3.1 */
#define PVRSRV_FBCDC_V3_1_USED_SHIFT (11)
#define PVRSRV_FBCDC_V3_1_USED (1U << PVRSRV_FBCDC_V3_1_USED_SHIFT)

/* Flag to be passed over the bridge during connection stating whether System has
   DMA transfer capability to and from device memory */
#define PVRSRV_SYSTEM_DMA_SHIFT (12)
#define PVRSRV_SYSTEM_DMA_USED (1U << PVRSRV_SYSTEM_DMA_SHIFT)

/* Flag to be passed over the bridge during connection stating whether GPU supports TFBC and is
   configured to use lossy compression control group 1 (25% / 37.5% / 50%) */
#define PVRSRV_TFBC_LOSSY_GROUP_SHIFT (13)
#define PVRSRV_TFBC_LOSSY_GROUP_1 (1U << PVRSRV_TFBC_LOSSY_GROUP_SHIFT)

static INLINE IMG_HANDLE GetBridgeHandle(SHARED_DEV_CONNECTION hDevConnection)
{
#if defined(__KERNEL__)
    return hDevConnection;
#else
    return hDevConnection->hServices;
#endif
}


#endif /* !defined(DEVICE_CONNECTION_H) */
