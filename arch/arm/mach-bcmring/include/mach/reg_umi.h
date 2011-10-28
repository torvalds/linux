/*****************************************************************************
* Copyright 2005 - 2008 Broadcom Corporation.  All rights reserved.
*
* Unless you and Broadcom execute a separate written software license
* agreement governing use of this software, this software is licensed to you
* under the terms of the GNU General Public License version 2, available at
* http://www.broadcom.com/licenses/GPLv2.php (the "GPL").
*
* Notwithstanding the above, under no circumstances may you combine this
* software in any way with any other Broadcom software provided under a
* license other than the GPL, without Broadcom's express prior written
* consent.
*****************************************************************************/

/*
*
*****************************************************************************
*
*  REG_UMI.h
*
*  PURPOSE:
*
*     This file contains definitions for the nand registers:
*
*  NOTES:
*
*****************************************************************************/

#if !defined(__ASM_ARCH_REG_UMI_H)
#define __ASM_ARCH_REG_UMI_H

/* ---- Include Files ---------------------------------------------------- */
#include <csp/reg.h>
#include <mach/csp/mm_io.h>

/* ---- Constants and Types ---------------------------------------------- */

/* Unified Memory Interface Ctrl Register */
#define HW_UMI_BASE       MM_IO_BASE_UMI

/* Flash bank 0 timing and control register */
#define REG_UMI_FLASH0_TCR         __REG32(HW_UMI_BASE  + 0x00)
/* Flash bank 1 timing and control register */
#define REG_UMI_FLASH1_TCR         __REG32(HW_UMI_BASE  + 0x04)
/* Flash bank 2 timing and control register */
#define REG_UMI_FLASH2_TCR         __REG32(HW_UMI_BASE  + 0x08)
/* MMD interface and control register */
#define REG_UMI_MMD_ICR            __REG32(HW_UMI_BASE  + 0x0c)
/* NAND timing and control register */
#define REG_UMI_NAND_TCR           __REG32(HW_UMI_BASE  + 0x18)
/* NAND ready/chip select register */
#define REG_UMI_NAND_RCSR          __REG32(HW_UMI_BASE  + 0x1c)
/* NAND ECC control & status register */
#define REG_UMI_NAND_ECC_CSR       __REG32(HW_UMI_BASE  + 0x20)
/* NAND ECC data register XXB2B1B0 */
#define REG_UMI_NAND_ECC_DATA      __REG32(HW_UMI_BASE  + 0x24)
/* BCH ECC Parameter N */
#define REG_UMI_BCH_N              __REG32(HW_UMI_BASE  + 0x40)
/* BCH ECC Parameter T */
#define REG_UMI_BCH_K              __REG32(HW_UMI_BASE  + 0x44)
/* BCH ECC Parameter K */
#define REG_UMI_BCH_T              __REG32(HW_UMI_BASE  + 0x48)
/* BCH ECC Contro Status */
#define REG_UMI_BCH_CTRL_STATUS    __REG32(HW_UMI_BASE  + 0x4C)
/* BCH WR ECC 31:0 */
#define REG_UMI_BCH_WR_ECC_0       __REG32(HW_UMI_BASE  + 0x50)
/* BCH WR ECC 63:32 */
#define REG_UMI_BCH_WR_ECC_1       __REG32(HW_UMI_BASE  + 0x54)
/* BCH WR ECC 95:64 */
#define REG_UMI_BCH_WR_ECC_2       __REG32(HW_UMI_BASE  + 0x58)
/* BCH WR ECC 127:96 */
#define REG_UMI_BCH_WR_ECC_3       __REG32(HW_UMI_BASE  + 0x5c)
/* BCH WR ECC 155:128 */
#define REG_UMI_BCH_WR_ECC_4       __REG32(HW_UMI_BASE  + 0x60)
/* BCH Read Error Location 1,0 */
#define REG_UMI_BCH_RD_ERR_LOC_1_0 __REG32(HW_UMI_BASE  + 0x64)
/* BCH Read Error Location 3,2 */
#define REG_UMI_BCH_RD_ERR_LOC_3_2 __REG32(HW_UMI_BASE  + 0x68)
/* BCH Read Error Location 5,4 */
#define REG_UMI_BCH_RD_ERR_LOC_5_4 __REG32(HW_UMI_BASE  + 0x6c)
/* BCH Read Error Location 7,6 */
#define REG_UMI_BCH_RD_ERR_LOC_7_6 __REG32(HW_UMI_BASE  + 0x70)
/* BCH Read Error Location 9,8 */
#define REG_UMI_BCH_RD_ERR_LOC_9_8 __REG32(HW_UMI_BASE  + 0x74)
/* BCH Read Error Location 11,10 */
#define REG_UMI_BCH_RD_ERR_LOC_B_A __REG32(HW_UMI_BASE  + 0x78)

/* REG_UMI_FLASH0/1/2_TCR, REG_UMI_SRAM0/1_TCR bits */
/* Enable wait pin during burst write or read */
#define REG_UMI_TCR_WAITEN              0x80000000
/* Enable mem ctrlr to work with ext mem of lower freq than AHB clk */
#define REG_UMI_TCR_LOWFREQ             0x40000000
/* 1=synch write, 0=async write */
#define REG_UMI_TCR_MEMTYPE_SYNCWRITE   0x20000000
/* 1=synch read, 0=async read */
#define REG_UMI_TCR_MEMTYPE_SYNCREAD    0x10000000
/* 1=page mode read, 0=normal mode read */
#define REG_UMI_TCR_MEMTYPE_PAGEREAD    0x08000000
/* page size/burst size (wrap only) */
#define REG_UMI_TCR_MEMTYPE_PGSZ_MASK   0x07000000
/* 4 word */
#define REG_UMI_TCR_MEMTYPE_PGSZ_4      0x00000000
/* 8 word */
#define REG_UMI_TCR_MEMTYPE_PGSZ_8      0x01000000
/* 16 word */
#define REG_UMI_TCR_MEMTYPE_PGSZ_16     0x02000000
/* 32 word */
#define REG_UMI_TCR_MEMTYPE_PGSZ_32     0x03000000
/* 64 word */
#define REG_UMI_TCR_MEMTYPE_PGSZ_64     0x04000000
/* 128 word */
#define REG_UMI_TCR_MEMTYPE_PGSZ_128    0x05000000
/* 256 word */
#define REG_UMI_TCR_MEMTYPE_PGSZ_256    0x06000000
/* 512 word */
#define REG_UMI_TCR_MEMTYPE_PGSZ_512    0x07000000
/* Page read access cycle / Burst write latency (n+2 / n+1) */
#define REG_UMI_TCR_TPRC_TWLC_MASK      0x00f80000
/* Bus turnaround cycle (n) */
#define REG_UMI_TCR_TBTA_MASK           0x00070000
/* Write pulse width cycle (n+1) */
#define REG_UMI_TCR_TWP_MASK            0x0000f800
/* Write recovery cycle (n+1) */
#define REG_UMI_TCR_TWR_MASK            0x00000600
/* Write address setup cycle (n+1) */
#define REG_UMI_TCR_TAS_MASK            0x00000180
/* Output enable delay cycle (n) */
#define REG_UMI_TCR_TOE_MASK            0x00000060
/* Read access cycle / Burst read latency (n+2 / n+1) */
#define REG_UMI_TCR_TRC_TLC_MASK        0x0000001f

/* REG_UMI_MMD_ICR bits */
/* Flash write protection pin control */
#define REG_UMI_MMD_ICR_FLASH_WP            0x8000
/* Extend hold time for sram0, sram1 csn (39 MHz operation) */
#define REG_UMI_MMD_ICR_XHCS                0x4000
/* Enable SDRAM 2 interface control */
#define REG_UMI_MMD_ICR_SDRAM2EN            0x2000
/* Enable merge of flash banks 0/1 to 512 MBit bank */
#define REG_UMI_MMD_ICR_INST512             0x1000
/* Enable merge of flash banks 1/2 to 512 MBit bank */
#define REG_UMI_MMD_ICR_DATA512             0x0800
/* Enable SDRAM interface control */
#define REG_UMI_MMD_ICR_SDRAMEN             0x0400
/* Polarity of busy state of Burst Wait Signal */
#define REG_UMI_MMD_ICR_WAITPOL             0x0200
/* Enable burst clock stopped when not accessing external burst flash/sram */
#define REG_UMI_MMD_ICR_BCLKSTOP            0x0100
/* Enable the peri1_csn to replace flash1_csn in 512 Mb flash mode */
#define REG_UMI_MMD_ICR_PERI1EN             0x0080
/* Enable the peri2_csn to replace sdram_csn */
#define REG_UMI_MMD_ICR_PERI2EN             0x0040
/* Enable the peri3_csn to replace sdram2_csn */
#define REG_UMI_MMD_ICR_PERI3EN             0x0020
/* Enable sram bank1 for H/W controlled MRS */
#define REG_UMI_MMD_ICR_MRSB1               0x0010
/* Enable sram bank0 for H/W controlled MRS */
#define REG_UMI_MMD_ICR_MRSB0               0x0008
/* Polarity for assert3ed state of H/W controlled MRS */
#define REG_UMI_MMD_ICR_MRSPOL              0x0004
/* 0: S/W controllable ZZ/MRS/CRE/P-Mode pin */
/* 1: H/W controlled ZZ/MRS/CRE/P-Mode, same timing as CS */
#define REG_UMI_MMD_ICR_MRSMODE             0x0002
/* MRS state for S/W controlled mode */
#define REG_UMI_MMD_ICR_MRSSTATE            0x0001

/* REG_UMI_NAND_TCR bits */
/* Enable software to control CS */
#define REG_UMI_NAND_TCR_CS_SWCTRL          0x80000000
/* 16-bit nand wordsize if set */
#define REG_UMI_NAND_TCR_WORD16             0x40000000
/* Bus turnaround cycle (n) */
#define REG_UMI_NAND_TCR_TBTA_MASK          0x00070000
/* Write pulse width cycle (n+1) */
#define REG_UMI_NAND_TCR_TWP_MASK           0x0000f800
/* Write recovery cycle (n+1) */
#define REG_UMI_NAND_TCR_TWR_MASK           0x00000600
/* Write address setup cycle (n+1) */
#define REG_UMI_NAND_TCR_TAS_MASK           0x00000180
/* Output enable delay cycle (n) */
#define REG_UMI_NAND_TCR_TOE_MASK           0x00000060
/* Read access cycle (n+2) */
#define REG_UMI_NAND_TCR_TRC_TLC_MASK       0x0000001f

/* REG_UMI_NAND_RCSR bits */
/* Status: Ready=1, Busy=0 */
#define REG_UMI_NAND_RCSR_RDY               0x02
/* Keep CS asserted during operation */
#define REG_UMI_NAND_RCSR_CS_ASSERTED       0x01

/* REG_UMI_NAND_ECC_CSR bits */
/* Interrupt status - read-only */
#define REG_UMI_NAND_ECC_CSR_NANDINT        0x80000000
/* Read: Status of ECC done, Write: clear ECC interrupt */
#define REG_UMI_NAND_ECC_CSR_ECCINT_RAW     0x00800000
/* Read: Status of R/B, Write: clear R/B interrupt */
#define REG_UMI_NAND_ECC_CSR_RBINT_RAW      0x00400000
/* 1 = Enable ECC Interrupt */
#define REG_UMI_NAND_ECC_CSR_ECCINT_ENABLE  0x00008000
/* 1 = Assert interrupt at rising edge of R/B_ */
#define REG_UMI_NAND_ECC_CSR_RBINT_ENABLE   0x00004000
/* Calculate ECC by 0=512 bytes, 1=256 bytes */
#define REG_UMI_NAND_ECC_CSR_256BYTE        0x00000080
/* Enable ECC in hardware */
#define REG_UMI_NAND_ECC_CSR_ECC_ENABLE     0x00000001

/* REG_UMI_BCH_CTRL_STATUS bits */
/* Shift to Indicate Number of correctable errors detected */
#define REG_UMI_BCH_CTRL_STATUS_NB_CORR_ERROR_SHIFT 20
/* Indicate Number of correctable errors detected */
#define REG_UMI_BCH_CTRL_STATUS_NB_CORR_ERROR 0x00F00000
/* Indicate Errors detected during read but uncorrectable */
#define REG_UMI_BCH_CTRL_STATUS_UNCORR_ERR    0x00080000
/* Indicate Errors detected during read and are correctable */
#define REG_UMI_BCH_CTRL_STATUS_CORR_ERR      0x00040000
/* Flag indicates BCH's ECC status of read process are valid */
#define REG_UMI_BCH_CTRL_STATUS_RD_ECC_VALID  0x00020000
/* Flag indicates BCH's ECC status of write process are valid */
#define REG_UMI_BCH_CTRL_STATUS_WR_ECC_VALID  0x00010000
/* Pause ECC calculation */
#define REG_UMI_BCH_CTRL_STATUS_PAUSE_ECC_DEC 0x00000010
/* Enable Interrupt */
#define REG_UMI_BCH_CTRL_STATUS_INT_EN        0x00000004
/* Enable ECC during read */
#define REG_UMI_BCH_CTRL_STATUS_ECC_RD_EN     0x00000002
/* Enable ECC during write */
#define REG_UMI_BCH_CTRL_STATUS_ECC_WR_EN     0x00000001
/* Mask for location */
#define REG_UMI_BCH_ERR_LOC_MASK              0x00001FFF
/* location within a byte */
#define REG_UMI_BCH_ERR_LOC_BYTE              0x00000007
/* location within a word */
#define REG_UMI_BCH_ERR_LOC_WORD              0x00000018
/* location within a page (512 byte) */
#define REG_UMI_BCH_ERR_LOC_PAGE              0x00001FE0
#define REG_UMI_BCH_ERR_LOC_ADDR(index)     (__REG32(HW_UMI_BASE + 0x64 + (index / 2)*4) >> ((index % 2) * 16))
#endif
