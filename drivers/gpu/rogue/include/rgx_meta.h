/*************************************************************************/ /*!
@File
@Title          RGX META definitions
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    RGX META helper definitions
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

#if !defined (__RGX_META_H__)
#define __RGX_META_H__


/***** The META HW register definitions in the file are updated manually *****/


#include "img_defs.h"

/************************************************************************
* META registers and MACROS 
************************************************************************/
#define	META_CR_CTRLREG_BASE(T)					(0x04800000 + 0x1000*(T))

#define META_CR_TXPRIVEXT						(0x048000E8)
#define META_CR_TXPRIVEXT_MINIM_EN				(0x1<<7)

#define META_CR_SYSC_JTAG_THREAD				(0x04830030)
#define META_CR_SYSC_JTAG_THREAD_PRIV_EN		(0x00000004)

#define META_CR_PERF_COUNT0						(0x0480FFE0)
#define META_CR_PERF_COUNT1						(0x0480FFE8)
#define META_CR_PERF_COUNT_CTRL_SHIFT			(28)
#define META_CR_PERF_COUNT_CTRL_MASK			(0xF0000000)
#define META_CR_PERF_COUNT_CTRL_DCACHEHITS		(0x8 << META_CR_PERF_COUNT_CTRL_SHIFT)
#define META_CR_PERF_COUNT_CTRL_ICACHEHITS		(0x9 << META_CR_PERF_COUNT_CTRL_SHIFT)
#define META_CR_PERF_COUNT_CTRL_ICACHEMISS		(0xA << META_CR_PERF_COUNT_CTRL_SHIFT)
#define META_CR_PERF_COUNT_CTRL_ICORE			(0xD << META_CR_PERF_COUNT_CTRL_SHIFT)
#define META_CR_PERF_COUNT_THR_SHIFT			(24)
#define META_CR_PERF_COUNT_THR_MASK				(0x0F000000)
#define META_CR_PERF_COUNT_THR_0				(0x1 << META_CR_PERF_COUNT_THR_SHIFT)
#define META_CR_PERF_COUNT_THR_1				(0x2 << META_CR_PERF_COUNT_THR_1)

#define META_CR_TxVECINT_BHALT					(0x04820500)
#define META_CR_PERF_ICORE0						(0x0480FFD0)
#define META_CR_PERF_ICORE1						(0x0480FFD8)
#define META_CR_PERF_ICORE_DCACHEMISS			(0x8)

typedef enum
{
	META_PERF_CONF_NONE = 0,
	META_PERF_CONF_ICACHE = 1,
	META_PERF_CONF_DCACHE = 2,
	META_PERF_CONF_POLLS = 3,
} META_PERF_CONF;

#define META_CR_PERF_COUNT(CTRL, THR)			((META_CR_PERF_COUNT_CTRL_##CTRL << META_CR_PERF_COUNT_CTRL_SHIFT) | \
												 (THR << META_CR_PERF_COUNT_THR_SHIFT))

#define	META_CR_TXUXXRXDT_OFFSET				(META_CR_CTRLREG_BASE(0) + 0x0000FFF0)
#define	META_CR_TXUXXRXRQ_OFFSET				(META_CR_CTRLREG_BASE(0) + 0x0000FFF8)

#define META_CR_TXUXXRXRQ_DREADY_BIT			(0x80000000)	/* Poll for done */
#define META_CR_TXUXXRXRQ_RDnWR_BIT  			(0x00010000)	/* Set for read  */
#define META_CR_TXUXXRXRQ_TX_S       			(12)
#define META_CR_TXUXXRXRQ_RX_S       			(4)
#define META_CR_TXUXXRXRQ_UXX_S      			(0)

#define META_CR_TXUA0_ID						(0x3)			/* Address unit regs */
#define META_CR_TXUPC_ID						(0x5)			/* PC registers */

/* Macros to calculate register access values */
#define META_CR_CORE_REG(Thr, RegNum, Unit)	(((Thr)			<< META_CR_TXUXXRXRQ_TX_S ) | \
											 ((RegNum)		<< META_CR_TXUXXRXRQ_RX_S ) | \
											 ((Unit)		<< META_CR_TXUXXRXRQ_UXX_S))

#define META_CR_THR0_PC		META_CR_CORE_REG(0, 0, META_CR_TXUPC_ID)
#define META_CR_THR0_PCX	META_CR_CORE_REG(0, 1, META_CR_TXUPC_ID)
#define META_CR_THR0_SP		META_CR_CORE_REG(0, 0, META_CR_TXUA0_ID)

#define META_CR_THR1_PC		META_CR_CORE_REG(1, 0, META_CR_TXUPC_ID)
#define META_CR_THR1_PCX	META_CR_CORE_REG(1, 1, META_CR_TXUPC_ID)
#define META_CR_THR1_SP		META_CR_CORE_REG(1, 0, META_CR_TXUA0_ID)

#define SP_ACCESS(Thread)	META_CR_CORE_REG(Thread, 0, META_CR_TXUA0_ID)
#define PC_ACCESS(Thread)	META_CR_CORE_REG(Thread, 0, META_CR_TXUPC_ID)

#define	META_CR_COREREG_ENABLE			(0x0000000)
#define	META_CR_COREREG_STATUS			(0x0000010)
#define	META_CR_COREREG_DEFR			(0x00000A0)

#define	META_CR_T0ENABLE_OFFSET			(META_CR_CTRLREG_BASE(0) + META_CR_COREREG_ENABLE)
#define	META_CR_T0STATUS_OFFSET			(META_CR_CTRLREG_BASE(0) + META_CR_COREREG_STATUS)
#define	META_CR_T0DEFR_OFFSET			(META_CR_CTRLREG_BASE(0) + META_CR_COREREG_DEFR)

#define	META_CR_T1ENABLE_OFFSET			(META_CR_CTRLREG_BASE(1) + META_CR_COREREG_ENABLE)
#define	META_CR_T1STATUS_OFFSET			(META_CR_CTRLREG_BASE(1) + META_CR_COREREG_STATUS)
#define	META_CR_T1DEFR_OFFSET			(META_CR_CTRLREG_BASE(1) + META_CR_COREREG_DEFR)

#define META_CR_TXENABLE_ENABLE_BIT		(0x00000001)   /* Set if running */
#define META_CR_TXSTATUS_PRIV			(0x00020000)   

#define META_MEM_GLOBAL_RANGE_BIT				(0x80000000)


/************************************************************************
* META LDR Format
************************************************************************/
/* Block header structure */
typedef struct 
{
	IMG_UINT32	ui32DevID;
	IMG_UINT32	ui32SLCode;
	IMG_UINT32	ui32SLData;
	IMG_UINT16	ui16PLCtrl;
	IMG_UINT16	ui16CRC;

} RGX_META_LDR_BLOCK_HDR;

/* High level data stream block  structure */
typedef struct 
{
	IMG_UINT16	ui16Cmd;
	IMG_UINT16	ui16Length;
	IMG_UINT32	ui32Next;
	IMG_UINT32	aui32CmdData[4];

} RGX_META_LDR_L1_DATA_BLK;

/* High level data stream block  structure */
typedef struct
{
	IMG_UINT16	ui16Tag;
	IMG_UINT16	ui16Length;
	IMG_UINT32	aui32BlockData[4];

} RGX_META_LDR_L2_DATA_BLK;

/* Config command structure */
typedef struct
{
	IMG_UINT32	ui32Type;
	IMG_UINT32	aui32BlockData[4];

} RGX_META_LDR_CFG_BLK;

/* Block type definitions */
#define RGX_META_LDR_COMMENT_TYPE_MASK			(0x0010)
#define RGX_META_LDR_BLK_IS_COMMENT(X)			((X & RGX_META_LDR_COMMENT_TYPE_MASK) != 0)

/* Command definitions
	Value	Name			Description
	0		LoadMem			Load memory with binary data.
	1		LoadCore		Load a set of core registers.
	2		LoadMMReg		Load a set of memory mapped registers.
	3		StartThreads	Set each thread PC and SP, then enable	threads.
	4		ZeroMem			Zeros a memory region.
	5		Config			Perform	a configuration command. */
#define RGX_META_LDR_CMD_MASK				(0x000F)

#define RGX_META_LDR_CMD_LOADMEM			(0x0000)
#define RGX_META_LDR_CMD_LOADCORE			(0x0001)
#define RGX_META_LDR_CMD_LOADMMREG			(0x0002)
#define RGX_META_LDR_CMD_START_THREADS		(0x0003)
#define RGX_META_LDR_CMD_ZEROMEM			(0x0004)
#define RGX_META_LDR_CMD_CONFIG			(0x0005)

/* Config Command definitions
	Value	Name		Description
	0		Pause		Pause for x times 100 instructions
	1		Read		Read a value from register - No value return needed.
						Utilises effects of issuing reads to certain registers
	2		Write		Write to mem location
	3		MemSet		Set mem to value
	4		MemCheck	check mem for specific value.*/
#define RGX_META_LDR_CFG_PAUSE			(0x0000)
#define RGX_META_LDR_CFG_READ			(0x0001)
#define RGX_META_LDR_CFG_WRITE			(0x0002)
#define RGX_META_LDR_CFG_MEMSET			(0x0003)
#define RGX_META_LDR_CFG_MEMCHECK		(0x0004)

/************************************************************************
* RGX FW segmented MMU definitions
************************************************************************/
/* All threads can access the segment */
#define RGXFW_SEGMMU_ALLTHRS	(0xf << 8)
/* Writeable */
#define RGXFW_SEGMMU_WRITEABLE	(0x1 << 1)
/* All threads can access and writeable */
#define RGXFW_SEGMMU_ALLTHRS_WRITEABLE	(RGXFW_SEGMMU_ALLTHRS | RGXFW_SEGMMU_WRITEABLE)

/* Direct map regions mapping (8-10) */
#define RGXFW_SEGMMU_DMAP_ID_START			(8)
#define RGXFW_SEGMMU_DMAP_ADDR_START		(0x06000000U)
#define RGXFW_SEGMMU_DMAP_ADDR_META			(0x86000000U)
#define RGXFW_SEGMMU_DMAP_SIZE				(8*1024*1024) /* 8 MB */

/* Direct map region 11 used for mapping GPU memory */
#define RGXFW_SEGMMU_DMAP_GPU_ID			(11)
#define RGXFW_SEGMMU_DMAP_GPU_ADDR_START	(RGXFW_SEGMMU_DMAP_ADDR_START + 3*RGXFW_SEGMMU_DMAP_SIZE)

/* Segment IDs */
#define RGXFW_SEGMMU_TEXT_ID			(0)
#define RGXFW_SEGMMU_SHARED_ID			(1)
#define RGXFW_SEGMMU_BOOTLDR_ID			(2)
#define RGXFW_SEGMMU_DATA_ID			(3)

#define RGXFW_SEGMMU_META_DM_ID			(0x7)

#if defined(HW_ERN_45914)
/* SLC caching strategy is emitted through the segment MMU. All the segments configured 
   through this macro are CACHED in the SLC. The interface has been kept the same to 
   simplify the code changes. The bifdm argument is ignored (no longer relevant). */
#define RGXFW_SEGMMU_OUTADDR_TOP_S7(pers, coheren, mmu_ctx)     ( (((IMG_UINT64) ((pers)    & 0x3))  << 52) | \
                                                                  (((IMG_UINT64) ((mmu_ctx) & 0xFF)) << 44) | \
                                                                  (((IMG_UINT64) ((coheren) & 0x1))  << 40) )
#define RGXFW_SEGMMU_OUTADDR_TOP_S7_SLC_CACHED(mmu_ctx)         RGXFW_SEGMMU_OUTADDR_TOP_S7(0x3, 0x0, mmu_ctx)
#define RGXFW_SEGMMU_OUTADDR_TOP_S7_SLC_UNCACHED(mmu_ctx)       RGXFW_SEGMMU_OUTADDR_TOP_S7(0x0, 0x1, mmu_ctx)

// Temporarily use uncached for everything:
//#define RGXFW_SEGMMU_OUTADDR_TOP(mmu_ctx, bifdm)              RGXFW_SEGMMU_OUTADDR_TOP_S7_SLC_CACHED(mmu_ctx)
#define RGXFW_SEGMMU_OUTADDR_TOP(mmu_ctx, bifdm)                RGXFW_SEGMMU_OUTADDR_TOP_S7_SLC_UNCACHED(mmu_ctx | (bifdm&0x0))
#else
/* To configure the Page Catalog and BIF-DM fed into the BIF for Garten accesses through this segment */
#define RGXFW_SEGMMU_OUTADDR_TOP(pc, bifdm)			            ( (((IMG_UINT64) ((pc)    & 0xF)) << 44) | \
                                                                  (((IMG_UINT64) ((bifdm) & 0xF)) << 40) )
#endif

/* META segments have 4kB minimum size */
#define RGXFW_SEGMMU_ALIGN			(0x1000) 

/* Segmented MMU registers (n = segment id) */
#define META_CR_MMCU_SEGMENTn_BASE(n)			(0x04850000 + (n)*0x10)
#define META_CR_MMCU_SEGMENTn_LIMIT(n)			(0x04850004 + (n)*0x10)
#define META_CR_MMCU_SEGMENTn_OUTA0(n)			(0x04850008 + (n)*0x10)
#define META_CR_MMCU_SEGMENTn_OUTA1(n)			(0x0485000C + (n)*0x10)

/* Win mode for data cache */
#define RGXFW__SEGMMU_DMAP_DC_WIN (0x3)
#define RGXFW__SEGMMU_DMAP_DC_SHIFT (0x6)

/************************************************************************
* RGX FW Bootloader defaults
************************************************************************/
#define RGXFW_BOOTLDR_META_ADDR		(0x40000000)
#define RGXFW_BOOTLDR_DEVV_ADDR_0	(0xC0000000)
#if defined(HW_ERN_45914)
#define RGXFW_BOOTLDR_DEVV_ADDR_1	(0x003000E1)
#else
#define RGXFW_BOOTLDR_DEVV_ADDR_1	(0x000007E1)
#endif
#define RGXFW_BOOTLDR_DEVV_ADDR		((((IMG_UINT64) RGXFW_BOOTLDR_DEVV_ADDR_1) << 32) | RGXFW_BOOTLDR_DEVV_ADDR_0)
#define RGXFW_BOOTLDR_LIMIT			(0x1FFFF000)

/* Bootloader configuration offset is in dwords (512 bytes) */
#define RGXFW_BOOTLDR_CONF_OFFSET	(0x80)

/************************************************************************
* RGX META Stack
************************************************************************/
#define RGX_META_STACK_SIZE  (0xC00)

/************************************************************************
* RGX META Core memory
************************************************************************/
#define RGX_META_COREMEM_BSS_SIZE    (0x800)
#define RGX_META_COREMEM_DATA_SIZE   (RGX_META_COREMEM_BSS_SIZE + RGX_META_STACK_SIZE)
#define RGX_META_COREMEM_CODE_SIZE   (RGX_META_COREMEM_SIZE - RGX_META_COREMEM_DATA_SIZE)
/* code and data both map to the same physical memory */
#define RGX_META_COREMEM_CODE_ADDR   (0x80000000)
#define RGX_META_COREMEM_DATA_ADDR   (0x82000000)
#define RGX_META_COREMEM_STACK_ADDR  (RGX_META_COREMEM_DATA_ADDR)
#define RGX_META_COREMEM_BSS_ADDR    (RGX_META_COREMEM_STACK_ADDR + RGX_META_STACK_SIZE)
/* because data and code share the same memory, base address for code is offset by the data */
#define RGX_META_COREMEM_CODE_BADDR  (RGX_META_COREMEM_CODE_ADDR + RGX_META_COREMEM_DATA_SIZE)

#define RGX_META_IS_COREMEM_CODE(A)  (((A) >= RGX_META_COREMEM_CODE_BADDR) && ((A) < (RGX_META_COREMEM_CODE_ADDR + RGX_META_COREMEM_SIZE)))
#define RGX_META_IS_COREMEM_DATA(A)  (((A) >= RGX_META_COREMEM_DATA_ADDR) && ((A) < (RGX_META_COREMEM_DATA_ADDR + RGX_META_COREMEM_DATA_SIZE)))

/************************************************************************
* 2nd thread
************************************************************************/
#define RGXFW_THR1_PC		(0x18930000)
#define RGXFW_THR1_SP		(0x78890000)

/************************************************************************
* META compatibility
************************************************************************/

#define META_CR_CORE_ID			(0x04831000)
#define META_CR_CORE_ID_VER_SHIFT	(16U)
#define META_CR_CORE_ID_VER_CLRMSK	(0XFF00FFFFU)

#if (RGX_FEATURE_META == MTP218)
#define RGX_CR_META_CORE_ID_VALUE 0x19
#elif (RGX_FEATURE_META == MTP219)
#define RGX_CR_META_CORE_ID_VALUE 0x1E
#elif (RGX_FEATURE_META == LTP218)
#define RGX_CR_META_CORE_ID_VALUE 0x1C
#elif (RGX_FEATURE_META == LTP217)
#define RGX_CR_META_CORE_ID_VALUE 0x1F
#else
#error "Unknown META ID"
#endif

#endif /*  __RGX_META_H__ */

/******************************************************************************
 End of file (rgx_meta.h)
******************************************************************************/


