/*************************************************************************/ /*!
@File
@Title          Services Firmware image utilities used at init time
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Services Firmware image utilities used at init time
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

/* The routines implemented here are built on top of an abstraction layer to
 * hide DDK/OS-specific details in case they are used outside of the DDK
 * (e.g. when trusted device is enabled).
 * Any new dependency should be added to rgxlayer.h.
 * Any new code should be built on top of the existing abstraction layer,
 * which should be extended when necessary. */
#include "rgxfwimageutils.h"


/************************************************************************
* FW Segments configuration
************************************************************************/
typedef struct _RGX_FW_SEGMENT_
{
	IMG_UINT32 ui32SegId;        /*!< Segment Id */
	IMG_UINT32 ui32SegStartAddr; /*!< Segment Start Addr */
	IMG_UINT32 ui32SegAllocSize; /*!< Amount of memory to allocate for that segment */
	IMG_UINT32 ui32FWMemOffset;  /*!< Offset of this segment in the collated FW mem allocation */
	const IMG_CHAR *pszSegName;
} RGX_FW_SEGMENT;

typedef struct _RGX_FW_SEGMENT_LIST_
{
	RGX_FW_SEGMENT *psRGXFWCodeSeg;
	RGX_FW_SEGMENT *psRGXFWDataSeg;
	IMG_UINT32 ui32CodeSegCount;
	IMG_UINT32 ui32DataSegCount;
} RGX_FW_SEGMENT_LIST;


#if defined(RGX_FEATURE_META) || defined(SUPPORT_KERNEL_SRVINIT)
static RGX_FW_SEGMENT asRGXMetaFWCodeSegments[] = {
/* Seg ID                 Seg Start Addr           Alloc size   FWMem offset  Name */
{RGXFW_SEGMMU_TEXT_ID,    RGXFW_BOOTLDR_META_ADDR, 0x31000,      0,           "Bootldr and Code"}, /* Has to be the first one to get the proper DevV addr */
};
static RGX_FW_SEGMENT asRGXMetaFWDataSegments[] = {
/* Seg ID                 Seg Start Addr           Alloc size   FWMem offset  Name */
{RGXFW_SEGMMU_DATA_ID,    0x38880000,              0x17000,      0,           "Local Shared and Data"},
};
#define RGXFW_META_NUM_CODE_SEGMENTS  (sizeof(asRGXMetaFWCodeSegments)/sizeof(asRGXMetaFWCodeSegments[0]))
#define RGXFW_META_NUM_DATA_SEGMENTS  (sizeof(asRGXMetaFWDataSegments)/sizeof(asRGXMetaFWDataSegments[0]))
#endif

#if defined(RGX_FEATURE_MIPS) || defined(SUPPORT_KERNEL_SRVINIT)
static RGX_FW_SEGMENT asRGXMipsFWCodeSegments[] = {
/* Seg ID   Seg Start Addr                         Alloc size                         FWMem offset                         Name */
{    0,     RGXMIPSFW_BOOT_NMI_CODE_VIRTUAL_BASE,  RGXMIPSFW_BOOT_NMI_CODE_SIZE,      RGXMIPSFW_BOOT_NMI_CODE_OFFSET,      "Bootldr and NMI code"},
{    1,     RGXMIPSFW_EXCEPTIONS_VIRTUAL_BASE,     RGXMIPSFW_EXCEPTIONSVECTORS_SIZE,  RGXMIPSFW_EXCEPTIONSVECTORS_OFFSET,  "Exception vectors"},
{    2,     RGXMIPSFW_CODE_VIRTUAL_BASE,           RGXMIPSFW_CODE_SIZE,               RGXMIPSFW_CODE_OFFSET,               "Text"},
};
static RGX_FW_SEGMENT asRGXMipsFWDataSegments[] = {
/* Seg ID   Seg Start Addr                         Alloc size                         FWMem offset                         Name */
{    3,     RGXMIPSFW_BOOT_NMI_DATA_VIRTUAL_BASE,  RGXMIPSFW_BOOT_NMI_DATA_SIZE,      RGXMIPSFW_BOOT_NMI_DATA_OFFSET,      "Bootldr and NMI data"},
{    4,     RGXMIPSFW_DATA_VIRTUAL_BASE,           RGXMIPSFW_DATA_SIZE,               RGXMIPSFW_DATA_OFFSET,               "Local Data"},
{    5,     RGXMIPSFW_STACK_VIRTUAL_BASE,          RGXMIPSFW_STACK_SIZE,              RGXMIPSFW_DATA_SIZE,                 "Stack"},
};

#define RGXFW_MIPS_NUM_CODE_SEGMENTS  (sizeof(asRGXMipsFWCodeSegments)/sizeof(asRGXMipsFWCodeSegments[0]))
#define RGXFW_MIPS_NUM_DATA_SEGMENTS  (sizeof(asRGXMipsFWDataSegments)/sizeof(asRGXMipsFWDataSegments[0]))
#endif

/*!
*******************************************************************************

 @Function      FindMMUSegment

 @Description   Given a 32 bit FW address attempt to find the corresponding
                pointer to FW allocation

 @Input         ui32OffsetIn      : 32 bit FW address
 @Input         pvHostFWCodeAddr  : Pointer to FW code
 @Input         pvHostFWDataAddr  : Pointer to FW data
 @Input         uiHostAddrOut     : CPU pointer equivalent to ui32OffsetIn

 @Return        PVRSRV_ERROR

******************************************************************************/
static PVRSRV_ERROR FindMMUSegment(IMG_UINT32 ui32OffsetIn,
                                   void *pvHostFWCodeAddr,
                                   void *pvHostFWDataAddr,
                                   void **uiHostAddrOut,
                                   RGX_FW_SEGMENT_LIST *psRGXFWSegList)
{
	RGX_FW_SEGMENT *psSegArr;
	IMG_UINT32 i;

	psSegArr = psRGXFWSegList->psRGXFWCodeSeg;
	for (i = 0; i < psRGXFWSegList->ui32CodeSegCount; i++)
	{
		if ((ui32OffsetIn >= psSegArr[i].ui32SegStartAddr) &&
		    (ui32OffsetIn < (psSegArr[i].ui32SegStartAddr + psSegArr[i].ui32SegAllocSize)))
		{
			*uiHostAddrOut = pvHostFWCodeAddr;
			goto found;
		}
	}

	psSegArr = psRGXFWSegList->psRGXFWDataSeg;
	for (i = 0; i < psRGXFWSegList->ui32DataSegCount; i++)
	{
		if ((ui32OffsetIn >= psSegArr[i].ui32SegStartAddr) &&
		   (ui32OffsetIn < (psSegArr[i].ui32SegStartAddr + psSegArr[i].ui32SegAllocSize)))
		{
			*uiHostAddrOut = pvHostFWDataAddr;
			goto found;
		}
	}

	return PVRSRV_ERROR_INIT_FAILURE;

found:
	/* Direct Mem write to mapped memory */
	ui32OffsetIn -= psSegArr[i].ui32SegStartAddr;
	ui32OffsetIn += psSegArr[i].ui32FWMemOffset;

	/* Add offset to pointer to FW allocation only if
	 * that allocation is available
	 */
	if (*uiHostAddrOut)
	{
		*(IMG_UINT8 **)uiHostAddrOut += ui32OffsetIn;
	}

	return PVRSRV_OK;
}

#if defined(RGX_FEATURE_META)  || defined(SUPPORT_KERNEL_SRVINIT)

/*!
*******************************************************************************

 @Function      RGXFWConfigureSegID

 @Description   Configures a single segment of the Segment MMU
                (base, limit and out_addr)

 @Input         hPrivate        : Implementation specific data
 @Input         ui64SegOutAddr  : Segment output base address (40 bit devVaddr)
 @Input         ui32SegBase     : Segment input base address (32 bit FW address)
 @Input         ui32SegLimit    : Segment size
 @Input         ui32SegID       : Segment ID
 @Input         pszName         : Segment name
 @Input         ppui32BootConf  : Pointer to bootloader data

 @Return        void

******************************************************************************/
static void RGXFWConfigureSegID(const void *hPrivate,
                                IMG_UINT64 ui64SegOutAddr,
                                IMG_UINT32 ui32SegBase,
                                IMG_UINT32 ui32SegLimit,
                                IMG_UINT32 ui32SegID,
                                const IMG_CHAR *pszName,
                                IMG_UINT32 **ppui32BootConf)
{
	IMG_UINT32 *pui32BootConf = *ppui32BootConf;
	IMG_UINT32 ui32SegOutAddr0  = ui64SegOutAddr & 0x00000000FFFFFFFFUL;
	IMG_UINT32 ui32SegOutAddr1  = (ui64SegOutAddr >> 32) & 0x00000000FFFFFFFFUL;

	/* META segments have a minimum size */
	IMG_UINT32 ui32LimitOff = (ui32SegLimit < RGXFW_SEGMMU_ALIGN) ?
	                          RGXFW_SEGMMU_ALIGN : ui32SegLimit;
	/* the limit is an offset, therefore off = size - 1 */
	ui32LimitOff -= 1;

	RGXCommentLogInit(hPrivate,
	                  "* FW %s - seg%d: meta_addr = 0x%08x, devv_addr = 0x%llx, limit = 0x%x",
	                  pszName, ui32SegID,
	                  ui32SegBase, (unsigned long long)ui64SegOutAddr,
	                  ui32LimitOff);

	ui32SegBase |= RGXFW_SEGMMU_ALLTHRS_WRITEABLE;

	*pui32BootConf++ = META_CR_MMCU_SEGMENTn_BASE(ui32SegID);
	*pui32BootConf++ = ui32SegBase;

	*pui32BootConf++ = META_CR_MMCU_SEGMENTn_LIMIT(ui32SegID);
	*pui32BootConf++ = ui32LimitOff;

	*pui32BootConf++ = META_CR_MMCU_SEGMENTn_OUTA0(ui32SegID);
	*pui32BootConf++ = ui32SegOutAddr0;

	*pui32BootConf++ = META_CR_MMCU_SEGMENTn_OUTA1(ui32SegID);
	*pui32BootConf++ = ui32SegOutAddr1;

	*ppui32BootConf = pui32BootConf;
}

/*!
*******************************************************************************

 @Function      RGXFWConfigureSegMMU

 @Description   Configures META's Segment MMU

 @Input         hPrivate             : Implementation specific data
 @Input         psFWCodeDevVAddrBase : FW code base device virtual address
 @Input         psFWDataDevVAddrBase : FW data base device virtual address
 @Input         ppui32BootConf       : Pointer to bootloader data

 @Return        void

******************************************************************************/
static void RGXFWConfigureSegMMU(const void       *hPrivate,
                                 IMG_DEV_VIRTADDR *psFWCodeDevVAddrBase,
                                 IMG_DEV_VIRTADDR *psFWDataDevVAddrBase,
                                 IMG_UINT32       **ppui32BootConf)
{
	IMG_UINT64 ui64SegOutAddr;
	IMG_UINT32 i;

	PVR_UNREFERENCED_PARAMETER(psFWCodeDevVAddrBase);

	/* Configure Segment MMU */
	RGXCommentLogInit(hPrivate, "********** FW configure Segment MMU **********");

	for (i = 0; i < RGXFW_META_NUM_DATA_SEGMENTS ; i++)
	{
		ui64SegOutAddr = (psFWDataDevVAddrBase->uiAddr |
		                  RGXFW_SEGMMU_OUTADDR_TOP(META_MMU_CONTEXT_MAPPING, RGXFW_SEGMMU_META_DM_ID)) +
		                  asRGXMetaFWDataSegments[i].ui32FWMemOffset;

		RGXFWConfigureSegID(hPrivate,
		                    ui64SegOutAddr,
		                    asRGXMetaFWDataSegments[i].ui32SegStartAddr,
		                    asRGXMetaFWDataSegments[i].ui32SegAllocSize,
		                    asRGXMetaFWDataSegments[i].ui32SegId,
		                    asRGXMetaFWDataSegments[i].pszSegName,
		                    ppui32BootConf); /*write the sequence to the bootldr */
	}
}

/*!
*******************************************************************************

 @Function      RGXFWConfigureMetaCaches

 @Description   Configure and enable the Meta instruction and data caches

 @Input         hPrivate          : Implementation specific data
 @Input         ui32NumThreads    : Number of FW threads in use
 @Input         ui32MainThreadID  : ID of the FW thread in use
                                    (only meaningful if ui32NumThreads == 1)
 @Input         ppui32BootConf    : Pointer to bootloader data

 @Return        void

******************************************************************************/
static void RGXFWConfigureMetaCaches(const void *hPrivate,
                                     IMG_UINT32 ui32NumThreads,
                                     IMG_UINT32 ui32MainThreadID,
                                     IMG_UINT32 **ppui32BootConf)
{
	IMG_UINT32 *pui32BootConf = *ppui32BootConf;
	IMG_UINT32 ui32DCacheT0, ui32ICacheT0;
	IMG_UINT32 ui32DCacheT1, ui32ICacheT1;
	IMG_UINT32 ui32DCacheT2, ui32ICacheT2;
	IMG_UINT32 ui32DCacheT3, ui32ICacheT3;

#define META_CR_MMCU_LOCAL_EBCTRL                        (0x04830600)
#define META_CR_MMCU_LOCAL_EBCTRL_ICWIN                  (0x3 << 14)
#define META_CR_MMCU_LOCAL_EBCTRL_DCWIN                  (0x3 << 6)
#define META_CR_SYSC_DCPART(n)                           (0x04830200 + (n)*0x8)
#define META_CR_SYSC_DCPARTX_CACHED_WRITE_ENABLE         (0x1 << 31)
#define META_CR_SYSC_ICPART(n)                           (0x04830220 + (n)*0x8)
#define META_CR_SYSC_XCPARTX_LOCAL_ADDR_OFFSET_TOP_HALF  (0x8 << 16)
#define META_CR_SYSC_XCPARTX_LOCAL_ADDR_FULL_CACHE       (0xF)
#define META_CR_SYSC_XCPARTX_LOCAL_ADDR_HALF_CACHE       (0x7)
#define META_CR_MMCU_DCACHE_CTRL                         (0x04830018)
#define META_CR_MMCU_ICACHE_CTRL                         (0x04830020)
#define META_CR_MMCU_XCACHE_CTRL_CACHE_HITS_EN           (0x1)

	RGXCommentLogInit(hPrivate, "********** Meta caches configuration *********");

	/* Initialise I/Dcache settings */
	ui32DCacheT0 = ui32DCacheT1 = META_CR_SYSC_DCPARTX_CACHED_WRITE_ENABLE;
	ui32DCacheT2 = ui32DCacheT3 = META_CR_SYSC_DCPARTX_CACHED_WRITE_ENABLE;
	ui32ICacheT0 = ui32ICacheT1 = ui32ICacheT2 = ui32ICacheT3 = 0;

	if (ui32NumThreads == 1)
	{
		if (ui32MainThreadID == 0)
		{
			ui32DCacheT0 |= META_CR_SYSC_XCPARTX_LOCAL_ADDR_FULL_CACHE;
			ui32ICacheT0 |= META_CR_SYSC_XCPARTX_LOCAL_ADDR_FULL_CACHE;
		}
		else
		{
			ui32DCacheT1 |= META_CR_SYSC_XCPARTX_LOCAL_ADDR_FULL_CACHE;
			ui32ICacheT1 |= META_CR_SYSC_XCPARTX_LOCAL_ADDR_FULL_CACHE;
		}
	}
	else
	{
		ui32DCacheT0 |= META_CR_SYSC_XCPARTX_LOCAL_ADDR_HALF_CACHE;
		ui32ICacheT0 |= META_CR_SYSC_XCPARTX_LOCAL_ADDR_HALF_CACHE;

		ui32DCacheT1 |= META_CR_SYSC_XCPARTX_LOCAL_ADDR_HALF_CACHE |
		                META_CR_SYSC_XCPARTX_LOCAL_ADDR_OFFSET_TOP_HALF;
		ui32ICacheT1 |= META_CR_SYSC_XCPARTX_LOCAL_ADDR_HALF_CACHE |
		                META_CR_SYSC_XCPARTX_LOCAL_ADDR_OFFSET_TOP_HALF;
	}

	/* Local region MMU enhanced bypass: WIN-3 mode for code and data caches */
	*pui32BootConf++ = META_CR_MMCU_LOCAL_EBCTRL;
	*pui32BootConf++ = META_CR_MMCU_LOCAL_EBCTRL_ICWIN |
	                   META_CR_MMCU_LOCAL_EBCTRL_DCWIN;

	RGXCommentLogInit(hPrivate, "Meta SP: [0x%08x] = 0x%08x",
	                  META_CR_MMCU_LOCAL_EBCTRL,
	                  META_CR_MMCU_LOCAL_EBCTRL_ICWIN | META_CR_MMCU_LOCAL_EBCTRL_DCWIN);

	/* Data cache partitioning thread 0 to 3 */
	*pui32BootConf++ = META_CR_SYSC_DCPART(0);
	*pui32BootConf++ = ui32DCacheT0;
	*pui32BootConf++ = META_CR_SYSC_DCPART(1);
	*pui32BootConf++ = ui32DCacheT1;
	*pui32BootConf++ = META_CR_SYSC_DCPART(2);
	*pui32BootConf++ = ui32DCacheT2;
	*pui32BootConf++ = META_CR_SYSC_DCPART(3);
	*pui32BootConf++ = ui32DCacheT3;

	RGXCommentLogInit(hPrivate, "Meta SP: [0x%08x] = 0x%08x",
	                  META_CR_SYSC_DCPART(0), ui32DCacheT0);
	RGXCommentLogInit(hPrivate, "Meta SP: [0x%08x] = 0x%08x",
	                  META_CR_SYSC_DCPART(1), ui32DCacheT1);
	RGXCommentLogInit(hPrivate, "Meta SP: [0x%08x] = 0x%08x",
	                  META_CR_SYSC_DCPART(2), ui32DCacheT2);
	RGXCommentLogInit(hPrivate, "Meta SP: [0x%08x] = 0x%08x",
	                  META_CR_SYSC_DCPART(3), ui32DCacheT3);

	/* Enable data cache hits */
	*pui32BootConf++ = META_CR_MMCU_DCACHE_CTRL;
	*pui32BootConf++ = META_CR_MMCU_XCACHE_CTRL_CACHE_HITS_EN;

	RGXCommentLogInit(hPrivate, "Meta SP: [0x%08x] = 0x%08x",
	                  META_CR_MMCU_DCACHE_CTRL,
	                  META_CR_MMCU_XCACHE_CTRL_CACHE_HITS_EN);

	/* Instruction cache partitioning thread 0 to 3 */
	*pui32BootConf++ = META_CR_SYSC_ICPART(0);
	*pui32BootConf++ = ui32ICacheT0;
	*pui32BootConf++ = META_CR_SYSC_ICPART(1);
	*pui32BootConf++ = ui32ICacheT1;
	*pui32BootConf++ = META_CR_SYSC_ICPART(2);
	*pui32BootConf++ = ui32ICacheT2;
	*pui32BootConf++ = META_CR_SYSC_ICPART(3);
	*pui32BootConf++ = ui32ICacheT3;

	RGXCommentLogInit(hPrivate, "Meta SP: [0x%08x] = 0x%08x",
	                  META_CR_SYSC_ICPART(0), ui32ICacheT0);
	RGXCommentLogInit(hPrivate, "Meta SP: [0x%08x] = 0x%08x",
	                  META_CR_SYSC_ICPART(1), ui32ICacheT1);
	RGXCommentLogInit(hPrivate, "Meta SP: [0x%08x] = 0x%08x",
	                  META_CR_SYSC_ICPART(2), ui32ICacheT2);
	RGXCommentLogInit(hPrivate, "Meta SP: [0x%08x] = 0x%08x",
	                  META_CR_SYSC_ICPART(3), ui32ICacheT3);

	/* Enable instruction cache hits */
	*pui32BootConf++ = META_CR_MMCU_ICACHE_CTRL;
	*pui32BootConf++ = META_CR_MMCU_XCACHE_CTRL_CACHE_HITS_EN;

	RGXCommentLogInit(hPrivate, "Meta SP: [0x%08x] = 0x%08x",
	                  META_CR_MMCU_ICACHE_CTRL,
	                  META_CR_MMCU_XCACHE_CTRL_CACHE_HITS_EN);

	*pui32BootConf++ = 0x040000C0;
	*pui32BootConf++ = 0;

	RGXCommentLogInit(hPrivate, "Meta SP: [0x%08x] = 0x%08x",
	                  0x040000C0, 0);

	*ppui32BootConf = pui32BootConf;
}

/*!
*******************************************************************************

 @Function      ProcessLDRCommandStream

 @Description   Process the output of the Meta toolchain in the .LDR format
                copying code and data sections into their final location and
                passing some information to the Meta bootloader

 @Input         hPrivate             : Implementation specific data
 @Input         pbLDR                : Pointer to FW blob
 @Input         pvHostFWCodeAddr     : Pointer to FW code
 @Input         pvHostFWDataAddr     : Pointer to FW data
 @Input         pvHostFWCorememAddr  : Pointer to FW coremem code
 @Input         ppui32BootConf       : Pointer to bootloader data

 @Return        PVRSRV_ERROR

******************************************************************************/
static PVRSRV_ERROR ProcessLDRCommandStream(const void *hPrivate,
                                            const IMG_BYTE* pbLDR,
                                            void* pvHostFWCodeAddr,
                                            void* pvHostFWDataAddr,
                                            void* pvHostFWCorememAddr,
                                            IMG_UINT32 **ppui32BootConf)
{
	RGX_META_LDR_BLOCK_HDR *psHeader = (RGX_META_LDR_BLOCK_HDR *) pbLDR;
	RGX_META_LDR_L1_DATA_BLK *psL1Data =
	    (RGX_META_LDR_L1_DATA_BLK*) ((IMG_UINT8 *) pbLDR + psHeader->ui32SLData);

	IMG_UINT32 *pui32BootConf  = *ppui32BootConf;
	IMG_UINT32 ui32CorememSize = RGXGetFWCorememSize(hPrivate);
	IMG_UINT32 ui32CorememCodeStartAddr = 0xFFFFFFFF;

	RGXCommentLogInit(hPrivate, "**********************************************");
	RGXCommentLogInit(hPrivate, "************** Begin LDR Parsing *************");
	RGXCommentLogInit(hPrivate, "**********************************************");

	while (psL1Data != NULL)
	{
		RGX_FW_SEGMENT_LIST sRGXFWSegList;
		sRGXFWSegList.psRGXFWCodeSeg = asRGXMetaFWCodeSegments;
		sRGXFWSegList.psRGXFWDataSeg = asRGXMetaFWDataSegments;
		sRGXFWSegList.ui32CodeSegCount = RGXFW_META_NUM_CODE_SEGMENTS;
		sRGXFWSegList.ui32DataSegCount = RGXFW_META_NUM_DATA_SEGMENTS;

		if (RGX_META_LDR_BLK_IS_COMMENT(psL1Data->ui16Cmd))
		{
			/* Don't process comment blocks */
			goto NextBlock;
		}

		switch (psL1Data->ui16Cmd & RGX_META_LDR_CMD_MASK)
		{
			case RGX_META_LDR_CMD_LOADMEM:
			{
				RGX_META_LDR_L2_DATA_BLK *psL2Block =
				    (RGX_META_LDR_L2_DATA_BLK*) (((IMG_UINT8 *) pbLDR) + psL1Data->aui32CmdData[1]);
				IMG_UINT32 ui32Offset = psL1Data->aui32CmdData[0];
				IMG_UINT32 ui32DataSize = psL2Block->ui16Length - 6 /* L2 Tag length and checksum */;
				void *pvWriteAddr;
				PVRSRV_ERROR eError;

				if (RGX_META_IS_COREMEM_CODE(ui32Offset, ui32CorememSize))
				{
					if (ui32Offset < ui32CorememCodeStartAddr)
					{
						if (ui32CorememCodeStartAddr == 0xFFFFFFFF)
						{
							/* Take the first coremem code address as the coremem code start address */
							ui32CorememCodeStartAddr = ui32Offset;

							/* Also check that there is a valid allocation for the coremem code */
							if (pvHostFWCorememAddr == NULL)
							{
								RGXErrorLogInit(hPrivate,
								                "ProcessLDRCommandStream: Coremem code found"
								                "but no coremem allocation available!");

								return PVRSRV_ERROR_INIT_FAILURE;
							}
						}
						else
						{
							/* The coremem addresses should be ordered in the LDR command stream */
							return PVRSRV_ERROR_INIT_FAILURE;
						}
					}

					/* Copy coremem data to buffer. The FW copies it to the actual coremem */
					ui32Offset -= ui32CorememCodeStartAddr;

					RGXMemCopy(hPrivate,
					           (void*)((IMG_UINT8 *)pvHostFWCorememAddr + ui32Offset),
					           psL2Block->aui32BlockData,
					           ui32DataSize);
				}
				else
				{
					/* Global range is aliased to local range */
					ui32Offset &= ~META_MEM_GLOBAL_RANGE_BIT;

					eError = FindMMUSegment(ui32Offset,
					                        pvHostFWCodeAddr,
					                        pvHostFWDataAddr,
					                        &pvWriteAddr,
					                        &sRGXFWSegList);

					if (eError != PVRSRV_OK)
					{
						RGXErrorLogInit(hPrivate,
						                "ProcessLDRCommandStream: Addr 0x%x (size: %d) not found in any segment",
						                ui32Offset, ui32DataSize);
						return eError;
					}

					/* Write to FW allocation only if available */
					if (pvWriteAddr)
					{
						RGXMemCopy(hPrivate,
						           pvWriteAddr,
						           psL2Block->aui32BlockData,
						           ui32DataSize);
					}
				}

				break;
			}
			case RGX_META_LDR_CMD_LOADCORE:
			case RGX_META_LDR_CMD_LOADMMREG:
			{
				return PVRSRV_ERROR_INIT_FAILURE;
			}
			case RGX_META_LDR_CMD_START_THREADS:
			{
				/* Don't process this block */
				break;
			}
			case RGX_META_LDR_CMD_ZEROMEM:
			{
				IMG_UINT32 ui32Offset = psL1Data->aui32CmdData[0];
				IMG_UINT32 ui32ByteCount = psL1Data->aui32CmdData[1];
				void *pvWriteAddr;
				PVRSRV_ERROR  eError;

				if (RGX_META_IS_COREMEM_DATA(ui32Offset, ui32CorememSize))
				{
					/* cannot zero coremem directly */
					break;
				}

				/* Global range is aliased to local range */
				ui32Offset &= ~META_MEM_GLOBAL_RANGE_BIT;

				eError = FindMMUSegment(ui32Offset,
				                        pvHostFWCodeAddr,
				                        pvHostFWDataAddr,
				                        &pvWriteAddr,
				                        &sRGXFWSegList);

				if (eError != PVRSRV_OK)
				{
					RGXErrorLogInit(hPrivate,
					                "ProcessLDRCommandStream: Addr 0x%x (size: %d) not found in any segment",
					                ui32Offset, ui32ByteCount);
					return eError;
				}

				/* Write to FW allocation only if available */
				if (pvWriteAddr)
				{
					RGXMemSet(hPrivate, pvWriteAddr, 0, ui32ByteCount);
				}

				break;
			}
			case RGX_META_LDR_CMD_CONFIG:
			{
				RGX_META_LDR_L2_DATA_BLK *psL2Block =
				    (RGX_META_LDR_L2_DATA_BLK*) (((IMG_UINT8 *) pbLDR) + psL1Data->aui32CmdData[0]);
				RGX_META_LDR_CFG_BLK *psConfigCommand = (RGX_META_LDR_CFG_BLK*) psL2Block->aui32BlockData;
				IMG_UINT32 ui32L2BlockSize = psL2Block->ui16Length - 6 /* L2 Tag length and checksum */;
				IMG_UINT32 ui32CurrBlockSize = 0;

				while (ui32L2BlockSize)
				{
					switch (psConfigCommand->ui32Type)
					{
						case RGX_META_LDR_CFG_PAUSE:
						case RGX_META_LDR_CFG_READ:
						{
							ui32CurrBlockSize = 8;
							return PVRSRV_ERROR_INIT_FAILURE;
						}
						case RGX_META_LDR_CFG_WRITE:
						{
							IMG_UINT32 ui32RegisterOffset = psConfigCommand->aui32BlockData[0];
							IMG_UINT32 ui32RegisterValue  = psConfigCommand->aui32BlockData[1];

							/* Only write to bootloader if we got a valid
							 * pointer to the FW code allocation
							 */
							if (pui32BootConf)
							{
								/* Do register write */
								*pui32BootConf++ = ui32RegisterOffset;
								*pui32BootConf++ = ui32RegisterValue;
							}

							RGXCommentLogInit(hPrivate, "Meta SP: [0x%08x] = 0x%08x",
							                  ui32RegisterOffset, ui32RegisterValue);

							ui32CurrBlockSize = 12;
							break;
						}
						case RGX_META_LDR_CFG_MEMSET:
						case RGX_META_LDR_CFG_MEMCHECK:
						{
							ui32CurrBlockSize = 20;
							return PVRSRV_ERROR_INIT_FAILURE;
						}
						default:
						{
							return PVRSRV_ERROR_INIT_FAILURE;
						}
					}
					ui32L2BlockSize -= ui32CurrBlockSize;
					psConfigCommand = (RGX_META_LDR_CFG_BLK*) (((IMG_UINT8*) psConfigCommand) + ui32CurrBlockSize);
				}

				break;
			}
			default:
			{
				return PVRSRV_ERROR_INIT_FAILURE;
			}
		}

NextBlock:

		if (psL1Data->ui32Next == 0xFFFFFFFF)
		{
			psL1Data = NULL;
		}
		else
		{
			psL1Data = (RGX_META_LDR_L1_DATA_BLK*) (((IMG_UINT8 *) pbLDR) + psL1Data->ui32Next);
		}
	}

	*ppui32BootConf = pui32BootConf;

	RGXCommentLogInit(hPrivate, "**********************************************");
	RGXCommentLogInit(hPrivate, "************** End Loader Parsing ************");
	RGXCommentLogInit(hPrivate, "**********************************************");

	return PVRSRV_OK;
}
#endif /* RGX_FEATURE_META */

#if defined(RGX_FEATURE_MIPS) || defined(SUPPORT_KERNEL_SRVINIT)
/*!
*******************************************************************************

 @Function      ProcessELFCommandStream

 @Description   Process the output of the Mips toolchain in the .ELF format
                copying code and data sections into their final location

 @Input         hPrivate          : Implementation specific data
 @Input         pbELF             : Pointer to FW blob
 @Input         pvHostFWCodeAddr  : Pointer to FW code
 @Input         pvHostFWDataAddr  : Pointer to FW data

 @Return        PVRSRV_ERROR

******************************************************************************/
static PVRSRV_ERROR ProcessELFCommandStream(const void *hPrivate,
                                            const IMG_BYTE *pbELF,
                                            void *pvHostFWCodeAddr,
                                            void *pvHostFWDataAddr)
{
	IMG_UINT32 ui32Entry;
	RGX_MIPS_ELF_HDR *psHeader = (RGX_MIPS_ELF_HDR *)pbELF;
	RGX_MIPS_ELF_PROGRAM_HDR *psProgramHeader =
	    (RGX_MIPS_ELF_PROGRAM_HDR *)(pbELF + psHeader->ui32Ephoff);
	PVRSRV_ERROR eError;

	for (ui32Entry = 0; ui32Entry < psHeader->ui32Ephnum; ui32Entry++, psProgramHeader++)
	{
		void *pvWriteAddr;
		RGX_FW_SEGMENT_LIST sRGXFWSegList;
		sRGXFWSegList.psRGXFWCodeSeg = asRGXMipsFWCodeSegments;
		sRGXFWSegList.psRGXFWDataSeg = asRGXMipsFWDataSegments;
		sRGXFWSegList.ui32CodeSegCount = RGXFW_MIPS_NUM_CODE_SEGMENTS;
		sRGXFWSegList.ui32DataSegCount = RGXFW_MIPS_NUM_DATA_SEGMENTS;

		/* Only consider loadable entries in the ELF segment table */
		if (psProgramHeader->ui32Ptype != ELF_PT_LOAD) continue;

		eError = FindMMUSegment(psProgramHeader->ui32Pvaddr,
		                        pvHostFWCodeAddr,
		                        pvHostFWDataAddr,
		                        &pvWriteAddr,
		                        &sRGXFWSegList);

		if (eError != PVRSRV_OK)
		{
			RGXErrorLogInit(hPrivate,
			                "%s: Addr 0x%x (size: %d) not found in any segment",__func__,
			                psProgramHeader->ui32Pvaddr,
			                psProgramHeader->ui32Pfilesz);
			return eError;
		}

		/* Write to FW allocation only if available */
		if (pvWriteAddr)
		{
			RGXMemCopy(hPrivate,
			           pvWriteAddr,
			           (IMG_PBYTE)(pbELF + psProgramHeader->ui32Poffset),
			           psProgramHeader->ui32Pfilesz);

			RGXMemSet(hPrivate,
			          (IMG_PBYTE)pvWriteAddr + psProgramHeader->ui32Pfilesz,
			          0,
			          psProgramHeader->ui32Pmemsz - psProgramHeader->ui32Pfilesz);
		}
	}

	return PVRSRV_OK;
}
#endif /* RGX_FEATURE_MIPS */


PVRSRV_ERROR RGXGetFWImageAllocSize(const void *hPrivate,
                                    IMG_DEVMEM_SIZE_T *puiFWCodeAllocSize,
                                    IMG_DEVMEM_SIZE_T *puiFWDataAllocSize,
                                    IMG_DEVMEM_SIZE_T *puiFWCorememAllocSize)
{
	IMG_UINT32 i, ui32NumCodeSegments = 0, ui32NumDataSegments = 0;
	RGX_FW_SEGMENT *pasRGXFWCodeSegments = NULL, *pasRGXFWDataSegments = NULL;

#if defined(SUPPORT_KERNEL_SRVINIT)
	IMG_BOOL bMIPS = RGXDeviceHasFeatureInit(hPrivate, RGX_FEATURE_MIPS_BIT_MASK);
#elif defined(RGX_FEATURE_MIPS)
	IMG_BOOL bMIPS = IMG_TRUE;
#else
	IMG_BOOL bMIPS = IMG_FALSE;
#endif

#if defined(RGX_FEATURE_META) || defined(SUPPORT_KERNEL_SRVINIT)
	if (!bMIPS)
	{
		pasRGXFWCodeSegments = asRGXMetaFWCodeSegments;
		pasRGXFWDataSegments = asRGXMetaFWDataSegments;
		ui32NumCodeSegments = RGXFW_META_NUM_CODE_SEGMENTS;
		ui32NumDataSegments = RGXFW_META_NUM_DATA_SEGMENTS;
	}
#endif

#if defined(RGX_FEATURE_MIPS) || defined(SUPPORT_KERNEL_SRVINIT)
	if (bMIPS)
	{
		pasRGXFWCodeSegments = asRGXMipsFWCodeSegments;
		pasRGXFWDataSegments = asRGXMipsFWDataSegments;
		ui32NumCodeSegments = RGXFW_MIPS_NUM_CODE_SEGMENTS;
		ui32NumDataSegments = RGXFW_MIPS_NUM_DATA_SEGMENTS;
	}
#endif

	*puiFWCodeAllocSize = 0;
	*puiFWDataAllocSize = 0;
	*puiFWCorememAllocSize = 0;

	/* Calculate how much memory the FW needs for its code and data segments */

	for(i = 0; i < ui32NumCodeSegments; i++) {
		*puiFWCodeAllocSize += ((pasRGXFWCodeSegments + i)->ui32SegAllocSize);
	}

	for(i = 0; i < ui32NumDataSegments; i++) {
		*puiFWDataAllocSize += ((pasRGXFWDataSegments + i)->ui32SegAllocSize);
	}

	*puiFWCorememAllocSize = RGXGetFWCorememSize(hPrivate);

	if (*puiFWCorememAllocSize != 0)
	{
		*puiFWCorememAllocSize = *puiFWCorememAllocSize - RGX_META_COREMEM_DATA_SIZE;
	}

	if (bMIPS)
	{
		if ((*puiFWCodeAllocSize % RGXMIPSFW_PAGE_SIZE) != 0)
		{
			RGXErrorLogInit(hPrivate,
			                "%s: The MIPS FW code allocation is not"
			                " a multiple of the page size!", __func__);
			return PVRSRV_ERROR_INIT_FAILURE;
		}

		if ((*puiFWDataAllocSize % RGXMIPSFW_PAGE_SIZE) != 0)
		{
			RGXErrorLogInit(hPrivate,
			                "%s: The MIPS FW data allocation is not"
			                " a multiple of the page size!", __func__);
			return PVRSRV_ERROR_INIT_FAILURE;
		}
	}

	return PVRSRV_OK;
}


PVRSRV_ERROR RGXProcessFWImage(const void           *hPrivate,
                               const IMG_BYTE       *pbRGXFirmware,
                               void                 *pvFWCode,
                               void                 *pvFWData,
                               void                 *pvFWCorememCode,
                               IMG_DEV_VIRTADDR     *psFWCodeDevVAddrBase,
                               IMG_DEV_VIRTADDR     *psFWDataDevVAddrBase,
                               IMG_DEV_VIRTADDR     *psFWCorememDevVAddrBase,
                               RGXFWIF_DEV_VIRTADDR *psFWCorememFWAddr,
                               RGXFWIF_DEV_VIRTADDR *psRGXFwInit,
                               IMG_UINT32           ui32NumThreads,
                               IMG_UINT32           ui32MainThreadID)
{
	PVRSRV_ERROR eError = PVRSRV_OK;

#if defined(SUPPORT_KERNEL_SRVINIT)
	IMG_BOOL bMIPS = RGXDeviceHasFeatureInit(hPrivate, RGX_FEATURE_MIPS_BIT_MASK);
#elif defined(RGX_FEATURE_MIPS)
	IMG_BOOL bMIPS = IMG_TRUE;
#else
	IMG_BOOL bMIPS = IMG_FALSE;
#endif

#if defined(RGX_FEATURE_META) || defined(SUPPORT_KERNEL_SRVINIT)
	if (!bMIPS)
	{
		IMG_UINT32 *pui32BootConf = NULL;
		/* Skip bootloader configuration if a pointer to the FW code
		 * allocation is not available
		 */
		if (pvFWCode)
		{
			/* This variable points to the bootloader code which is mostly
			 * a sequence of <register address,register value> pairs
			 */
			pui32BootConf = ((IMG_UINT32*) pvFWCode) + RGXFW_BOOTLDR_CONF_OFFSET;

			/* Slave port and JTAG accesses are privileged */
			*pui32BootConf++ = META_CR_SYSC_JTAG_THREAD;
			*pui32BootConf++ = META_CR_SYSC_JTAG_THREAD_PRIV_EN;

			RGXFWConfigureSegMMU(hPrivate,
			                     psFWCodeDevVAddrBase,
			                     psFWDataDevVAddrBase,
			                     &pui32BootConf);
		}
	
		/* Process FW image data stream */
		eError = ProcessLDRCommandStream(hPrivate,
		                                 pbRGXFirmware,
		                                 pvFWCode,
		                                 pvFWData,
		                                 pvFWCorememCode,
		                                 &pui32BootConf);
		if (eError != PVRSRV_OK)
		{
			RGXErrorLogInit(hPrivate, "RGXProcessFWImage: Processing FW image failed (%d)", eError);
			return eError;
		}

		/* Skip bootloader configuration if a pointer to the FW code
		 * allocation is not available
		 */
		if (pvFWCode)
		{
			if ((ui32NumThreads == 0) || (ui32NumThreads > 2) || (ui32MainThreadID >= 2))
			{
				RGXErrorLogInit(hPrivate,
				                "ProcessFWImage: Wrong Meta threads configuration, using one thread only");

				ui32NumThreads = 1;
				ui32MainThreadID = 0;
			}

			RGXFWConfigureMetaCaches(hPrivate,
			                         ui32NumThreads,
			                         ui32MainThreadID,
			                         &pui32BootConf);

			/* Signal the end of the conf sequence */
			*pui32BootConf++ = 0x0;
			*pui32BootConf++ = 0x0;

			/* The FW main argv arguments start here */
			*pui32BootConf++ = psRGXFwInit->ui32Addr;

			if ((RGXGetFWCorememSize(hPrivate) != 0) && (psFWCorememFWAddr != NULL))
			{
				*pui32BootConf++ = psFWCorememFWAddr->ui32Addr;
			}
			else
			{
				*pui32BootConf++ = 0;
			}

#if defined(SUPPORT_KERNEL_SRVINIT)
			if (RGXDeviceHasFeatureInit(hPrivate, RGX_FEATURE_META_DMA_BIT_MASK))
#elif defined(RGX_FEATURE_META_DMA)
			if (IMG_TRUE)
#else
			if (IMG_FALSE)
#endif
			{
				*pui32BootConf++ = (IMG_UINT32) (psFWCorememDevVAddrBase->uiAddr >> 32);
				*pui32BootConf++ = (IMG_UINT32) psFWCorememDevVAddrBase->uiAddr;
			}
			else
			{
				*pui32BootConf++ = 0;
				*pui32BootConf++ = 0;
			}

		}
	}
#endif

#if defined(RGX_FEATURE_MIPS) || defined(SUPPORT_KERNEL_SRVINIT)
	if (bMIPS)
	{
		/* Process FW image data stream */
		eError = ProcessELFCommandStream(hPrivate,
		                                 pbRGXFirmware,
		                                 pvFWCode,
		                                 pvFWData);
		if (eError != PVRSRV_OK)
		{
			RGXErrorLogInit(hPrivate, "RGXProcessFWImage: Processing FW image failed (%d)", eError);
			return eError;
		}

		PVR_UNREFERENCED_PARAMETER(pvFWData); /* No need to touch the data segment in MIPS */
		PVR_UNREFERENCED_PARAMETER(pvFWCorememCode); /* Coremem N/A in MIPS */
		PVR_UNREFERENCED_PARAMETER(psFWCodeDevVAddrBase);
		PVR_UNREFERENCED_PARAMETER(psFWDataDevVAddrBase);
		PVR_UNREFERENCED_PARAMETER(psFWCorememDevVAddrBase);
		PVR_UNREFERENCED_PARAMETER(psFWCorememFWAddr);
		PVR_UNREFERENCED_PARAMETER(psRGXFwInit);
		PVR_UNREFERENCED_PARAMETER(ui32NumThreads);
		PVR_UNREFERENCED_PARAMETER(ui32MainThreadID);
	}
#endif

	return eError;
}

