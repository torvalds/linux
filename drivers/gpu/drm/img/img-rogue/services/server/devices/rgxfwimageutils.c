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
#include "pvrsrv.h"


/************************************************************************
* FW layout information
************************************************************************/
#define MAX_NUM_ENTRIES (8)
static RGX_FW_LAYOUT_ENTRY asRGXFWLayoutTable[MAX_NUM_ENTRIES];
static IMG_UINT32 ui32LayoutEntryNum;


static RGX_FW_LAYOUT_ENTRY* GetTableEntry(const void *hPrivate, RGX_FW_SECTION_ID eId)
{
	IMG_UINT32 i;

	for (i = 0; i < ui32LayoutEntryNum; i++)
	{
		if (asRGXFWLayoutTable[i].eId == eId)
		{
			return &asRGXFWLayoutTable[i];
		}
	}

	RGXErrorLog(hPrivate, "%s: id %u not found, returning entry 0\n",
	            __func__, eId);

	return &asRGXFWLayoutTable[0];
}

/*!
*******************************************************************************

 @Function      FindMMUSegment

 @Description   Given a 32 bit FW address attempt to find the corresponding
                pointer to FW allocation

 @Input         ui32OffsetIn             : 32 bit FW address
 @Input         pvHostFWCodeAddr         : Pointer to FW code
 @Input         pvHostFWDataAddr         : Pointer to FW data
 @Input         pvHostFWCorememCodeAddr  : Pointer to FW coremem code
 @Input         pvHostFWCorememDataAddr  : Pointer to FW coremem code
 @Input         uiHostAddrOut            : CPU pointer equivalent to ui32OffsetIn

 @Return        PVRSRV_ERROR

******************************************************************************/
static PVRSRV_ERROR FindMMUSegment(IMG_UINT32 ui32OffsetIn,
                                   void *pvHostFWCodeAddr,
                                   void *pvHostFWDataAddr,
                                   void *pvHostFWCorememCodeAddr,
                                   void *pvHostFWCorememDataAddr,
                                   void **uiHostAddrOut)
{
	IMG_UINT32 i;

	for (i = 0; i < ui32LayoutEntryNum; i++)
	{
		if ((ui32OffsetIn >= asRGXFWLayoutTable[i].ui32BaseAddr) &&
		    (ui32OffsetIn < (asRGXFWLayoutTable[i].ui32BaseAddr + asRGXFWLayoutTable[i].ui32AllocSize)))
		{
			switch (asRGXFWLayoutTable[i].eType)
			{
				case FW_CODE:
					*uiHostAddrOut = pvHostFWCodeAddr;
					break;

				case FW_DATA:
					*uiHostAddrOut = pvHostFWDataAddr;
					break;

				case FW_COREMEM_CODE:
					*uiHostAddrOut = pvHostFWCorememCodeAddr;
					break;

				case FW_COREMEM_DATA:
					*uiHostAddrOut = pvHostFWCorememDataAddr;
					break;

				default:
					return PVRSRV_ERROR_INIT_FAILURE;
			}

			goto found;
		}
	}

	return PVRSRV_ERROR_INIT_FAILURE;

found:
	if (*uiHostAddrOut == NULL)
	{
		return PVRSRV_OK;
	}

	/* Direct Mem write to mapped memory */
	ui32OffsetIn -= asRGXFWLayoutTable[i].ui32BaseAddr;
	ui32OffsetIn += asRGXFWLayoutTable[i].ui32AllocOffset;

	/* Add offset to pointer to FW allocation only if
	 * that allocation is available
	 */
	if (*uiHostAddrOut)
	{
		*(IMG_UINT8 **)uiHostAddrOut += ui32OffsetIn;
	}

	return PVRSRV_OK;
}

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
                                IMG_UINT32 **ppui32BootConf)
{
	IMG_UINT32 *pui32BootConf = *ppui32BootConf;
	IMG_UINT32 ui32SegOutAddr0 = ui64SegOutAddr & 0x00000000FFFFFFFFUL;
	IMG_UINT32 ui32SegOutAddr1 = (ui64SegOutAddr >> 32) & 0x00000000FFFFFFFFUL;

	/* META segments have a minimum size */
	IMG_UINT32 ui32LimitOff = (ui32SegLimit < RGXFW_SEGMMU_ALIGN) ?
	                          RGXFW_SEGMMU_ALIGN : ui32SegLimit;
	/* the limit is an offset, therefore off = size - 1 */
	ui32LimitOff -= 1;

	RGXCommentLog(hPrivate,
	              "* Seg%d: meta_addr = 0x%08x, devv_addr = 0x%" IMG_UINT64_FMTSPECx ", limit = 0x%x",
	              ui32SegID,
	              ui32SegBase,
	              ui64SegOutAddr,
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
	IMG_UINT64 ui64SegOutAddrTop;
	IMG_UINT32 i;

	PVR_UNREFERENCED_PARAMETER(psFWCodeDevVAddrBase);

	/* Configure Segment MMU */
	RGXCommentLog(hPrivate, "********** FW configure Segment MMU **********");

	if (RGX_DEVICE_HAS_FEATURE(hPrivate, SLC_VIVT))
	{
		ui64SegOutAddrTop = RGXFW_SEGMMU_OUTADDR_TOP_VIVT_SLC_CACHED(MMU_CONTEXT_MAPPING_FWPRIV);
	}
	else
	{
		ui64SegOutAddrTop = RGXFW_SEGMMU_OUTADDR_TOP_SLC(MMU_CONTEXT_MAPPING_FWPRIV, RGXFW_SEGMMU_META_BIFDM_ID);
	}

	for (i = 0; i < ui32LayoutEntryNum; i++)
	{
		/*
		 * FW code is using the bootloader segment which is already configured on boot.
		 * FW coremem code and data don't use the segment MMU.
		 * Only the FW data segment needs to be configured.
		 */

		if (asRGXFWLayoutTable[i].eType == FW_DATA)
		{
			IMG_UINT64 ui64SegOutAddr;
			IMG_UINT32 ui32SegId = RGXFW_SEGMMU_DATA_ID;

			ui64SegOutAddr = (psFWDataDevVAddrBase->uiAddr | ui64SegOutAddrTop) +
			                  asRGXFWLayoutTable[i].ui32AllocOffset;

			RGXFWConfigureSegID(hPrivate,
			                    ui64SegOutAddr,
			                    asRGXFWLayoutTable[i].ui32BaseAddr,
			                    asRGXFWLayoutTable[i].ui32AllocSize,
			                    ui32SegId,
			                    ppui32BootConf); /*write the sequence to the bootldr */

			break;
		}
	}
}

/*!
*******************************************************************************

 @Function      RGXFWConfigureMetaCaches

 @Description   Configure and enable the Meta instruction and data caches

 @Input         hPrivate          : Implementation specific data
 @Input         ui32NumThreads    : Number of FW threads in use
 @Input         ppui32BootConf    : Pointer to bootloader data

 @Return        void

******************************************************************************/
static void RGXFWConfigureMetaCaches(const void *hPrivate,
                                     IMG_UINT32 ui32NumThreads,
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

	RGXCommentLog(hPrivate, "********** Meta caches configuration *********");

	/* Initialise I/Dcache settings */
	ui32DCacheT0 = ui32DCacheT1 = META_CR_SYSC_DCPARTX_CACHED_WRITE_ENABLE;
	ui32DCacheT2 = ui32DCacheT3 = META_CR_SYSC_DCPARTX_CACHED_WRITE_ENABLE;
	ui32ICacheT0 = ui32ICacheT1 = ui32ICacheT2 = ui32ICacheT3 = 0;

	if (ui32NumThreads == 1)
	{
		ui32DCacheT0 |= META_CR_SYSC_XCPARTX_LOCAL_ADDR_FULL_CACHE;
		ui32ICacheT0 |= META_CR_SYSC_XCPARTX_LOCAL_ADDR_FULL_CACHE;
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

	RGXCommentLog(hPrivate, "Meta SP: [0x%08x] = 0x%08x",
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

	RGXCommentLog(hPrivate, "Meta SP: [0x%08x] = 0x%08x",
	              META_CR_SYSC_DCPART(0), ui32DCacheT0);
	RGXCommentLog(hPrivate, "Meta SP: [0x%08x] = 0x%08x",
	              META_CR_SYSC_DCPART(1), ui32DCacheT1);
	RGXCommentLog(hPrivate, "Meta SP: [0x%08x] = 0x%08x",
	              META_CR_SYSC_DCPART(2), ui32DCacheT2);
	RGXCommentLog(hPrivate, "Meta SP: [0x%08x] = 0x%08x",
	              META_CR_SYSC_DCPART(3), ui32DCacheT3);

	/* Enable data cache hits */
	*pui32BootConf++ = META_CR_MMCU_DCACHE_CTRL;
	*pui32BootConf++ = META_CR_MMCU_XCACHE_CTRL_CACHE_HITS_EN;

	RGXCommentLog(hPrivate, "Meta SP: [0x%08x] = 0x%08x",
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

	RGXCommentLog(hPrivate, "Meta SP: [0x%08x] = 0x%08x",
	              META_CR_SYSC_ICPART(0), ui32ICacheT0);
	RGXCommentLog(hPrivate, "Meta SP: [0x%08x] = 0x%08x",
	              META_CR_SYSC_ICPART(1), ui32ICacheT1);
	RGXCommentLog(hPrivate, "Meta SP: [0x%08x] = 0x%08x",
	              META_CR_SYSC_ICPART(2), ui32ICacheT2);
	RGXCommentLog(hPrivate, "Meta SP: [0x%08x] = 0x%08x",
	              META_CR_SYSC_ICPART(3), ui32ICacheT3);

	/* Enable instruction cache hits */
	*pui32BootConf++ = META_CR_MMCU_ICACHE_CTRL;
	*pui32BootConf++ = META_CR_MMCU_XCACHE_CTRL_CACHE_HITS_EN;

	RGXCommentLog(hPrivate, "Meta SP: [0x%08x] = 0x%08x",
	              META_CR_MMCU_ICACHE_CTRL,
	              META_CR_MMCU_XCACHE_CTRL_CACHE_HITS_EN);

	*pui32BootConf++ = 0x040000C0;
	*pui32BootConf++ = 0;

	RGXCommentLog(hPrivate, "Meta SP: [0x%08x] = 0x%08x", 0x040000C0, 0);

	*ppui32BootConf = pui32BootConf;
}

/*!
*******************************************************************************

 @Function      ProcessLDRCommandStream

 @Description   Process the output of the Meta toolchain in the .LDR format
                copying code and data sections into their final location and
                passing some information to the Meta bootloader

 @Input         hPrivate                 : Implementation specific data
 @Input         pbLDR                    : Pointer to FW blob
 @Input         pvHostFWCodeAddr         : Pointer to FW code
 @Input         pvHostFWDataAddr         : Pointer to FW data
 @Input         pvHostFWCorememCodeAddr  : Pointer to FW coremem code
 @Input         pvHostFWCorememDataAddr  : Pointer to FW coremem data
 @Input         ppui32BootConf           : Pointer to bootloader data

 @Return        PVRSRV_ERROR

******************************************************************************/
PVRSRV_ERROR ProcessLDRCommandStream(const void *hPrivate,
                                     const IMG_BYTE* pbLDR,
                                     void* pvHostFWCodeAddr,
                                     void* pvHostFWDataAddr,
                                     void* pvHostFWCorememCodeAddr,
                                     void* pvHostFWCorememDataAddr,
                                     IMG_UINT32 **ppui32BootConf)
{
	RGX_META_LDR_BLOCK_HDR *psHeader = (RGX_META_LDR_BLOCK_HDR *) pbLDR;
	RGX_META_LDR_L1_DATA_BLK *psL1Data =
	    (RGX_META_LDR_L1_DATA_BLK*) ((IMG_UINT8 *) pbLDR + psHeader->ui32SLData);

	IMG_UINT32 *pui32BootConf  = ppui32BootConf ? *ppui32BootConf : NULL;
	IMG_UINT32 ui32CorememSize = RGXGetFWCorememSize(hPrivate);

	RGXCommentLog(hPrivate, "**********************************************");
	RGXCommentLog(hPrivate, "************** Begin LDR Parsing *************");
	RGXCommentLog(hPrivate, "**********************************************");

	while (psL1Data != NULL)
	{
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

				if (!RGX_META_IS_COREMEM_CODE(ui32Offset, ui32CorememSize) &&
				    !RGX_META_IS_COREMEM_DATA(ui32Offset, ui32CorememSize))
				{
					/* Global range is aliased to local range */
					ui32Offset &= ~META_MEM_GLOBAL_RANGE_BIT;
				}

				eError = FindMMUSegment(ui32Offset,
				                        pvHostFWCodeAddr,
				                        pvHostFWDataAddr,
				                        pvHostFWCorememCodeAddr,
				                        pvHostFWCorememDataAddr,
				                        &pvWriteAddr);

				if (eError != PVRSRV_OK)
				{
					RGXErrorLog(hPrivate,
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
				                        pvHostFWCorememCodeAddr,
				                        pvHostFWCorememDataAddr,
				                        &pvWriteAddr);

				if (eError != PVRSRV_OK)
				{
					RGXErrorLog(hPrivate,
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

							RGXCommentLog(hPrivate, "Meta SP: [0x%08x] = 0x%08x",
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

	if (pui32BootConf)
	{
		*ppui32BootConf = pui32BootConf;
	}

	RGXCommentLog(hPrivate, "**********************************************");
	RGXCommentLog(hPrivate, "************** End Loader Parsing ************");
	RGXCommentLog(hPrivate, "**********************************************");

	return PVRSRV_OK;
}

/*!
*******************************************************************************

 @Function      ProcessELFCommandStream

 @Description   Process a file in .ELF format copying code and data sections
                into their final location

 @Input         hPrivate                 : Implementation specific data
 @Input         pbELF                    : Pointer to FW blob
 @Input         pvHostFWCodeAddr         : Pointer to FW code
 @Input         pvHostFWDataAddr         : Pointer to FW data
 @Input         pvHostFWCorememCodeAddr  : Pointer to FW coremem code
 @Input         pvHostFWCorememDataAddr  : Pointer to FW coremem data

 @Return        PVRSRV_ERROR

******************************************************************************/
PVRSRV_ERROR ProcessELFCommandStream(const void *hPrivate,
                                     const IMG_BYTE *pbELF,
                                     void *pvHostFWCodeAddr,
                                     void *pvHostFWDataAddr,
                                     void* pvHostFWCorememCodeAddr,
                                     void* pvHostFWCorememDataAddr)
{
	IMG_UINT32 ui32Entry;
	IMG_ELF_HDR *psHeader = (IMG_ELF_HDR *)pbELF;
	IMG_ELF_PROGRAM_HDR *psProgramHeader =
	    (IMG_ELF_PROGRAM_HDR *)(pbELF + psHeader->ui32Ephoff);
	PVRSRV_ERROR eError;

	for (ui32Entry = 0; ui32Entry < psHeader->ui32Ephnum; ui32Entry++, psProgramHeader++)
	{
		void *pvWriteAddr;

		/* Only consider loadable entries in the ELF segment table */
		if (psProgramHeader->ui32Ptype != ELF_PT_LOAD) continue;

		eError = FindMMUSegment(psProgramHeader->ui32Pvaddr,
		                        pvHostFWCodeAddr,
		                        pvHostFWDataAddr,
		                        pvHostFWCorememCodeAddr,
		                        pvHostFWCorememDataAddr,
		                        &pvWriteAddr);

		if (eError != PVRSRV_OK)
		{
			RGXErrorLog(hPrivate,
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

IMG_UINT32 RGXGetFWImageSectionOffset(const void *hPrivate, RGX_FW_SECTION_ID eId)
{
	RGX_FW_LAYOUT_ENTRY *psEntry = GetTableEntry(hPrivate, eId);

	return psEntry->ui32AllocOffset;
}

IMG_UINT32 RGXGetFWImageSectionMaxSize(const void *hPrivate, RGX_FW_SECTION_ID eId)
{
	RGX_FW_LAYOUT_ENTRY *psEntry = GetTableEntry(hPrivate, eId);

	return psEntry->ui32MaxSize;
}

IMG_UINT32 RGXGetFWImageSectionAllocSize(const void *hPrivate, RGX_FW_SECTION_ID eId)
{
	RGX_FW_LAYOUT_ENTRY *psEntry = GetTableEntry(hPrivate, eId);

	return psEntry->ui32AllocSize;
}

IMG_UINT32 RGXGetFWImageSectionAddress(const void *hPrivate, RGX_FW_SECTION_ID eId)
{
	RGX_FW_LAYOUT_ENTRY *psEntry = GetTableEntry(hPrivate, eId);

	return psEntry->ui32BaseAddr;
}

PVRSRV_ERROR RGXGetFWImageAllocSize(const void *hPrivate,
                                    const IMG_BYTE    *pbRGXFirmware,
                                    const IMG_UINT32  ui32RGXFirmwareSize,
                                    IMG_DEVMEM_SIZE_T *puiFWCodeAllocSize,
                                    IMG_DEVMEM_SIZE_T *puiFWDataAllocSize,
                                    IMG_DEVMEM_SIZE_T *puiFWCorememCodeAllocSize,
                                    IMG_DEVMEM_SIZE_T *puiFWCorememDataAllocSize)
{
	RGX_FW_INFO_HEADER *psInfoHeader;
	const IMG_BYTE *pbRGXFirmwareInfo;
	const IMG_BYTE *pbRGXFirmwareLayout;
	IMG_UINT32 i;

	if (pbRGXFirmware == NULL || ui32RGXFirmwareSize == 0 || ui32RGXFirmwareSize <= FW_BLOCK_SIZE)
	{
		RGXErrorLog(hPrivate, "%s: Invalid FW binary at %p, size %u",
		            __func__, pbRGXFirmware, ui32RGXFirmwareSize);
		return PVRSRV_ERROR_INVALID_PARAMS;
	}


	/*
	 * Acquire pointer to the FW info header within the FW image.
	 * The format of the header in the FW image might not be the one expected
	 * by the driver, but the driver should still be able to correctly read
	 * the information below, as long as new/incompatible elements are added
	 * at the end of the header (they will be ignored by the driver).
	 */

	pbRGXFirmwareInfo = pbRGXFirmware + ui32RGXFirmwareSize - FW_BLOCK_SIZE;
	psInfoHeader = (RGX_FW_INFO_HEADER*)pbRGXFirmwareInfo;

	/* If any of the following checks fails, the FW will likely not work properly */

	if (psInfoHeader->ui32InfoVersion != FW_INFO_VERSION)
	{
		RGXErrorLog(hPrivate, "%s: FW info version mismatch (expected: %u, found: %u)",
		            __func__,
		            (IMG_UINT32) FW_INFO_VERSION,
		            psInfoHeader->ui32InfoVersion);
	}

	if (psInfoHeader->ui32HeaderLen != sizeof(RGX_FW_INFO_HEADER))
	{
		RGXErrorLog(hPrivate, "%s: FW info header sizes mismatch (expected: %u, found: %u)",
		            __func__,
		            (IMG_UINT32) sizeof(RGX_FW_INFO_HEADER),
		            psInfoHeader->ui32HeaderLen);
	}

	if (psInfoHeader->ui32LayoutEntrySize != sizeof(RGX_FW_LAYOUT_ENTRY))
	{
		RGXErrorLog(hPrivate, "%s: FW layout entry sizes mismatch (expected: %u, found: %u)",
		            __func__,
		            (IMG_UINT32) sizeof(RGX_FW_LAYOUT_ENTRY),
		            psInfoHeader->ui32LayoutEntrySize);
	}

	if (psInfoHeader->ui32LayoutEntryNum > MAX_NUM_ENTRIES)
	{
		RGXErrorLog(hPrivate, "%s: Not enough storage for the FW layout table (max: %u entries, found: %u)",
		            __func__,
		            MAX_NUM_ENTRIES,
		            psInfoHeader->ui32LayoutEntryNum);
	}

#if defined(RGX_FEATURE_MIPS_BIT_MASK)
	if (RGX_DEVICE_HAS_FEATURE(hPrivate, MIPS))
	{
		if (psInfoHeader->ui32FwPageSize != RGXGetOSPageSize(hPrivate))
		{
			RGXErrorLog(hPrivate, "%s: FW page size mismatch (expected: %u, found: %u)",
			            __func__,
			            (IMG_UINT32) RGXGetOSPageSize(hPrivate),
			            psInfoHeader->ui32FwPageSize);
			return PVRSRV_ERROR_INVALID_PARAMS;
		}
	}
#endif

	ui32LayoutEntryNum = psInfoHeader->ui32LayoutEntryNum;


	/*
	 * Copy FW layout table from FW image to local array.
	 * One entry is copied at a time and the copy is limited to what the driver
	 * expects to find in it. Assuming that new/incompatible elements
	 * are added at the end of each entry, the loop below adapts the table
	 * in the FW image into the format expected by the driver.
	 */

	pbRGXFirmwareLayout = pbRGXFirmwareInfo + psInfoHeader->ui32HeaderLen;

	for (i = 0; i < ui32LayoutEntryNum; i++)
	{
		RGX_FW_LAYOUT_ENTRY *psOutEntry = &asRGXFWLayoutTable[i];

		RGX_FW_LAYOUT_ENTRY *psInEntry = (RGX_FW_LAYOUT_ENTRY*)
			(pbRGXFirmwareLayout + i * psInfoHeader->ui32LayoutEntrySize);

		RGXMemCopy(hPrivate,
		           (void*)psOutEntry,
		           (void*)psInEntry,
		           sizeof(RGX_FW_LAYOUT_ENTRY));
	}


	/* Calculate how much memory the FW needs for its code and data segments */

	*puiFWCodeAllocSize = 0;
	*puiFWDataAllocSize = 0;
	*puiFWCorememCodeAllocSize = 0;
	*puiFWCorememDataAllocSize = 0;

	for (i = 0; i < ui32LayoutEntryNum; i++)
	{
		switch (asRGXFWLayoutTable[i].eType)
		{
			case FW_CODE:
				*puiFWCodeAllocSize += asRGXFWLayoutTable[i].ui32AllocSize;
				break;

			case FW_DATA:
				*puiFWDataAllocSize += asRGXFWLayoutTable[i].ui32AllocSize;
				break;

			case FW_COREMEM_CODE:
				*puiFWCorememCodeAllocSize += asRGXFWLayoutTable[i].ui32AllocSize;
				break;

			case FW_COREMEM_DATA:
				*puiFWCorememDataAllocSize += asRGXFWLayoutTable[i].ui32AllocSize;
				break;

			default:
				RGXErrorLog(hPrivate, "%s: Unknown FW section type %u\n",
				            __func__, asRGXFWLayoutTable[i].eType);
				break;
		}
	}

	return PVRSRV_OK;
}


PVRSRV_ERROR RGXProcessFWImage(const void *hPrivate,
                               const IMG_BYTE *pbRGXFirmware,
                               void *pvFWCode,
                               void *pvFWData,
                               void *pvFWCorememCode,
                               void *pvFWCorememData,
                               RGX_FW_BOOT_PARAMS *puFWParams)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	IMG_BOOL bMIPS = IMG_FALSE;
	IMG_BOOL bRISCV = RGX_DEVICE_HAS_FEATURE(hPrivate, RISCV_FW_PROCESSOR);
	IMG_BOOL bMETA;

#if defined(RGX_FEATURE_MIPS_BIT_MASK)
	bMIPS = RGX_DEVICE_HAS_FEATURE(hPrivate, MIPS);
#endif
	bMETA = !bMIPS && !bRISCV;

	if (bMETA)
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
			                     &puFWParams->sMeta.sFWCodeDevVAddr,
			                     &puFWParams->sMeta.sFWDataDevVAddr,
			                     &pui32BootConf);
		}

		/* Process FW image data stream */
		eError = ProcessLDRCommandStream(hPrivate,
		                                 pbRGXFirmware,
		                                 pvFWCode,
		                                 pvFWData,
		                                 pvFWCorememCode,
		                                 pvFWCorememData,
		                                 &pui32BootConf);
		if (eError != PVRSRV_OK)
		{
			RGXErrorLog(hPrivate, "RGXProcessFWImage: Processing FW image failed (%d)", eError);
			return eError;
		}

		/* Skip bootloader configuration if a pointer to the FW code
		 * allocation is not available
		 */
		if (pvFWCode)
		{
			IMG_UINT32 ui32NumThreads   = puFWParams->sMeta.ui32NumThreads;

			if ((ui32NumThreads == 0) || (ui32NumThreads > 2))
			{
				RGXErrorLog(hPrivate,
				            "ProcessFWImage: Wrong Meta threads configuration, using one thread only");

				ui32NumThreads = 1;
			}

			RGXFWConfigureMetaCaches(hPrivate,
			                         ui32NumThreads,
			                         &pui32BootConf);

			/* Signal the end of the conf sequence */
			*pui32BootConf++ = 0x0;
			*pui32BootConf++ = 0x0;

			if (puFWParams->sMeta.uiFWCorememCodeSize && (puFWParams->sMeta.sFWCorememCodeFWAddr.ui32Addr != 0))
			{
				*pui32BootConf++ = puFWParams->sMeta.sFWCorememCodeFWAddr.ui32Addr;
				*pui32BootConf++ = puFWParams->sMeta.uiFWCorememCodeSize;
			}
			else
			{
				*pui32BootConf++ = 0;
				*pui32BootConf++ = 0;
			}

			if (RGX_DEVICE_HAS_FEATURE(hPrivate, META_DMA))
			{
				*pui32BootConf++ = (IMG_UINT32) (puFWParams->sMeta.sFWCorememCodeDevVAddr.uiAddr >> 32);
				*pui32BootConf++ = (IMG_UINT32) puFWParams->sMeta.sFWCorememCodeDevVAddr.uiAddr;
			}
			else
			{
				*pui32BootConf++ = 0;
				*pui32BootConf++ = 0;
			}
		}
	}
#if defined(RGXMIPSFW_MAX_NUM_PAGETABLE_PAGES)
	else if (bMIPS)
	{
		/* Process FW image data stream */
		eError = ProcessELFCommandStream(hPrivate,
		                                 pbRGXFirmware,
		                                 pvFWCode,
		                                 pvFWData,
		                                 NULL,
		                                 NULL);
		if (eError != PVRSRV_OK)
		{
			RGXErrorLog(hPrivate, "RGXProcessFWImage: Processing FW image failed (%d)", eError);
			return eError;
		}

		if (pvFWData)
		{
			RGXMIPSFW_BOOT_DATA *psBootData = (RGXMIPSFW_BOOT_DATA*)
				/* To get a pointer to the bootloader configuration data start from a pointer to the FW image... */
				IMG_OFFSET_ADDR(pvFWData,
				/* ... jump to the boot/NMI data page... */
				(RGXGetFWImageSectionOffset(NULL, MIPS_BOOT_DATA)
				/* ... and then jump to the bootloader data offset within the page */
				+ RGXMIPSFW_BOOTLDR_CONF_OFFSET));

			/* Rogue Registers physical address */
			psBootData->ui64RegBase = puFWParams->sMips.sGPURegAddr.uiAddr;

			/* MIPS Page Table physical address */
			psBootData->ui32PTLog2PageSize = puFWParams->sMips.ui32FWPageTableLog2PageSize;
			psBootData->ui32PTNumPages     = puFWParams->sMips.ui32FWPageTableNumPages;
			psBootData->aui64PTPhyAddr[0U] = puFWParams->sMips.asFWPageTableAddr[0U].uiAddr;
			psBootData->aui64PTPhyAddr[1U] = puFWParams->sMips.asFWPageTableAddr[1U].uiAddr;
			psBootData->aui64PTPhyAddr[2U] = puFWParams->sMips.asFWPageTableAddr[2U].uiAddr;
			psBootData->aui64PTPhyAddr[3U] = puFWParams->sMips.asFWPageTableAddr[3U].uiAddr;

			/* MIPS Stack Pointer Physical Address */
			psBootData->ui64StackPhyAddr = puFWParams->sMips.sFWStackAddr.uiAddr;

			/* Reserved for future use */
			psBootData->ui32Reserved1 = 0;
			psBootData->ui32Reserved2 = 0;
		}
	}
#endif /* #if defined(RGXMIPSFW_MAX_NUM_PAGETABLE_PAGES) */
	else
	{
		/* Process FW image data stream */
		eError = ProcessELFCommandStream(hPrivate,
		                                 pbRGXFirmware,
		                                 pvFWCode,
		                                 pvFWData,
		                                 pvFWCorememCode,
		                                 pvFWCorememData);
		if (eError != PVRSRV_OK)
		{
			RGXErrorLog(hPrivate, "RGXProcessFWImage: Processing FW image failed (%d)", eError);
			return eError;
		}

		if (pvFWData)
		{
			RGXRISCVFW_BOOT_DATA *psBootData = (RGXRISCVFW_BOOT_DATA*)
				IMG_OFFSET_ADDR(pvFWData, RGXRISCVFW_BOOTLDR_CONF_OFFSET);

			psBootData->ui64CorememCodeDevVAddr = puFWParams->sRISCV.sFWCorememCodeDevVAddr.uiAddr;
			psBootData->ui32CorememCodeFWAddr   = puFWParams->sRISCV.sFWCorememCodeFWAddr.ui32Addr;
			psBootData->ui32CorememCodeSize     = puFWParams->sRISCV.uiFWCorememCodeSize;

			psBootData->ui64CorememDataDevVAddr = puFWParams->sRISCV.sFWCorememDataDevVAddr.uiAddr;
			psBootData->ui32CorememDataFWAddr   = puFWParams->sRISCV.sFWCorememDataFWAddr.ui32Addr;
			psBootData->ui32CorememDataSize     = puFWParams->sRISCV.uiFWCorememDataSize;
		}
	}

	return eError;
}
