/*
 * 2007+ Copyright (c) Evgeniy Polyakov <johnpol@2ka.mipt.ru>
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/mod_devicetable.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/mm.h>
#include <linux/dma-mapping.h>
#include <linux/scatterlist.h>
#include <linux/highmem.h>
#include <linux/interrupt.h>
#include <linux/crypto.h>
#include <linux/hw_random.h>
#include <linux/ktime.h>

#include <crypto/algapi.h>
#include <crypto/des.h>

#include <asm/kmap_types.h>

#undef dprintk

#define HIFN_TEST
//#define HIFN_DEBUG

#ifdef HIFN_DEBUG
#define dprintk(f, a...) 	printk(f, ##a)
#else
#define dprintk(f, a...)	do {} while (0)
#endif

static char hifn_pll_ref[sizeof("extNNN")] = "ext";
module_param_string(hifn_pll_ref, hifn_pll_ref, sizeof(hifn_pll_ref), 0444);
MODULE_PARM_DESC(hifn_pll_ref,
		 "PLL reference clock (pci[freq] or ext[freq], default ext)");

static atomic_t hifn_dev_number;

#define ACRYPTO_OP_DECRYPT	0
#define ACRYPTO_OP_ENCRYPT	1
#define ACRYPTO_OP_HMAC		2
#define ACRYPTO_OP_RNG		3

#define ACRYPTO_MODE_ECB		0
#define ACRYPTO_MODE_CBC		1
#define ACRYPTO_MODE_CFB		2
#define ACRYPTO_MODE_OFB		3

#define ACRYPTO_TYPE_AES_128	0
#define ACRYPTO_TYPE_AES_192	1
#define ACRYPTO_TYPE_AES_256	2
#define ACRYPTO_TYPE_3DES	3
#define ACRYPTO_TYPE_DES	4

#define PCI_VENDOR_ID_HIFN		0x13A3
#define PCI_DEVICE_ID_HIFN_7955		0x0020
#define	PCI_DEVICE_ID_HIFN_7956		0x001d

/* I/O region sizes */

#define HIFN_BAR0_SIZE			0x1000
#define HIFN_BAR1_SIZE			0x2000
#define HIFN_BAR2_SIZE			0x8000

/* DMA registres */

#define HIFN_DMA_CRA 			0x0C	/* DMA Command Ring Address */
#define HIFN_DMA_SDRA 			0x1C	/* DMA Source Data Ring Address */
#define HIFN_DMA_RRA			0x2C	/* DMA Result Ring Address */
#define HIFN_DMA_DDRA			0x3C	/* DMA Destination Data Ring Address */
#define HIFN_DMA_STCTL			0x40	/* DMA Status and Control */
#define HIFN_DMA_INTREN 		0x44	/* DMA Interrupt Enable */
#define HIFN_DMA_CFG1			0x48	/* DMA Configuration #1 */
#define HIFN_DMA_CFG2			0x6C	/* DMA Configuration #2 */
#define HIFN_CHIP_ID			0x98	/* Chip ID */

/*
 * Processing Unit Registers (offset from BASEREG0)
 */
#define	HIFN_0_PUDATA		0x00	/* Processing Unit Data */
#define	HIFN_0_PUCTRL		0x04	/* Processing Unit Control */
#define	HIFN_0_PUISR		0x08	/* Processing Unit Interrupt Status */
#define	HIFN_0_PUCNFG		0x0c	/* Processing Unit Configuration */
#define	HIFN_0_PUIER		0x10	/* Processing Unit Interrupt Enable */
#define	HIFN_0_PUSTAT		0x14	/* Processing Unit Status/Chip ID */
#define	HIFN_0_FIFOSTAT		0x18	/* FIFO Status */
#define	HIFN_0_FIFOCNFG		0x1c	/* FIFO Configuration */
#define	HIFN_0_SPACESIZE	0x20	/* Register space size */

/* Processing Unit Control Register (HIFN_0_PUCTRL) */
#define	HIFN_PUCTRL_CLRSRCFIFO	0x0010	/* clear source fifo */
#define	HIFN_PUCTRL_STOP	0x0008	/* stop pu */
#define	HIFN_PUCTRL_LOCKRAM	0x0004	/* lock ram */
#define	HIFN_PUCTRL_DMAENA	0x0002	/* enable dma */
#define	HIFN_PUCTRL_RESET	0x0001	/* Reset processing unit */

/* Processing Unit Interrupt Status Register (HIFN_0_PUISR) */
#define	HIFN_PUISR_CMDINVAL	0x8000	/* Invalid command interrupt */
#define	HIFN_PUISR_DATAERR	0x4000	/* Data error interrupt */
#define	HIFN_PUISR_SRCFIFO	0x2000	/* Source FIFO ready interrupt */
#define	HIFN_PUISR_DSTFIFO	0x1000	/* Destination FIFO ready interrupt */
#define	HIFN_PUISR_DSTOVER	0x0200	/* Destination overrun interrupt */
#define	HIFN_PUISR_SRCCMD	0x0080	/* Source command interrupt */
#define	HIFN_PUISR_SRCCTX	0x0040	/* Source context interrupt */
#define	HIFN_PUISR_SRCDATA	0x0020	/* Source data interrupt */
#define	HIFN_PUISR_DSTDATA	0x0010	/* Destination data interrupt */
#define	HIFN_PUISR_DSTRESULT	0x0004	/* Destination result interrupt */

/* Processing Unit Configuration Register (HIFN_0_PUCNFG) */
#define	HIFN_PUCNFG_DRAMMASK	0xe000	/* DRAM size mask */
#define	HIFN_PUCNFG_DSZ_256K	0x0000	/* 256k dram */
#define	HIFN_PUCNFG_DSZ_512K	0x2000	/* 512k dram */
#define	HIFN_PUCNFG_DSZ_1M	0x4000	/* 1m dram */
#define	HIFN_PUCNFG_DSZ_2M	0x6000	/* 2m dram */
#define	HIFN_PUCNFG_DSZ_4M	0x8000	/* 4m dram */
#define	HIFN_PUCNFG_DSZ_8M	0xa000	/* 8m dram */
#define	HIFN_PUNCFG_DSZ_16M	0xc000	/* 16m dram */
#define	HIFN_PUCNFG_DSZ_32M	0xe000	/* 32m dram */
#define	HIFN_PUCNFG_DRAMREFRESH	0x1800	/* DRAM refresh rate mask */
#define	HIFN_PUCNFG_DRFR_512	0x0000	/* 512 divisor of ECLK */
#define	HIFN_PUCNFG_DRFR_256	0x0800	/* 256 divisor of ECLK */
#define	HIFN_PUCNFG_DRFR_128	0x1000	/* 128 divisor of ECLK */
#define	HIFN_PUCNFG_TCALLPHASES	0x0200	/* your guess is as good as mine... */
#define	HIFN_PUCNFG_TCDRVTOTEM	0x0100	/* your guess is as good as mine... */
#define	HIFN_PUCNFG_BIGENDIAN	0x0080	/* DMA big endian mode */
#define	HIFN_PUCNFG_BUS32	0x0040	/* Bus width 32bits */
#define	HIFN_PUCNFG_BUS16	0x0000	/* Bus width 16 bits */
#define	HIFN_PUCNFG_CHIPID	0x0020	/* Allow chipid from PUSTAT */
#define	HIFN_PUCNFG_DRAM	0x0010	/* Context RAM is DRAM */
#define	HIFN_PUCNFG_SRAM	0x0000	/* Context RAM is SRAM */
#define	HIFN_PUCNFG_COMPSING	0x0004	/* Enable single compression context */
#define	HIFN_PUCNFG_ENCCNFG	0x0002	/* Encryption configuration */

/* Processing Unit Interrupt Enable Register (HIFN_0_PUIER) */
#define	HIFN_PUIER_CMDINVAL	0x8000	/* Invalid command interrupt */
#define	HIFN_PUIER_DATAERR	0x4000	/* Data error interrupt */
#define	HIFN_PUIER_SRCFIFO	0x2000	/* Source FIFO ready interrupt */
#define	HIFN_PUIER_DSTFIFO	0x1000	/* Destination FIFO ready interrupt */
#define	HIFN_PUIER_DSTOVER	0x0200	/* Destination overrun interrupt */
#define	HIFN_PUIER_SRCCMD	0x0080	/* Source command interrupt */
#define	HIFN_PUIER_SRCCTX	0x0040	/* Source context interrupt */
#define	HIFN_PUIER_SRCDATA	0x0020	/* Source data interrupt */
#define	HIFN_PUIER_DSTDATA	0x0010	/* Destination data interrupt */
#define	HIFN_PUIER_DSTRESULT	0x0004	/* Destination result interrupt */

/* Processing Unit Status Register/Chip ID (HIFN_0_PUSTAT) */
#define	HIFN_PUSTAT_CMDINVAL	0x8000	/* Invalid command interrupt */
#define	HIFN_PUSTAT_DATAERR	0x4000	/* Data error interrupt */
#define	HIFN_PUSTAT_SRCFIFO	0x2000	/* Source FIFO ready interrupt */
#define	HIFN_PUSTAT_DSTFIFO	0x1000	/* Destination FIFO ready interrupt */
#define	HIFN_PUSTAT_DSTOVER	0x0200	/* Destination overrun interrupt */
#define	HIFN_PUSTAT_SRCCMD	0x0080	/* Source command interrupt */
#define	HIFN_PUSTAT_SRCCTX	0x0040	/* Source context interrupt */
#define	HIFN_PUSTAT_SRCDATA	0x0020	/* Source data interrupt */
#define	HIFN_PUSTAT_DSTDATA	0x0010	/* Destination data interrupt */
#define	HIFN_PUSTAT_DSTRESULT	0x0004	/* Destination result interrupt */
#define	HIFN_PUSTAT_CHIPREV	0x00ff	/* Chip revision mask */
#define	HIFN_PUSTAT_CHIPENA	0xff00	/* Chip enabled mask */
#define	HIFN_PUSTAT_ENA_2	0x1100	/* Level 2 enabled */
#define	HIFN_PUSTAT_ENA_1	0x1000	/* Level 1 enabled */
#define	HIFN_PUSTAT_ENA_0	0x3000	/* Level 0 enabled */
#define	HIFN_PUSTAT_REV_2	0x0020	/* 7751 PT6/2 */
#define	HIFN_PUSTAT_REV_3	0x0030	/* 7751 PT6/3 */

/* FIFO Status Register (HIFN_0_FIFOSTAT) */
#define	HIFN_FIFOSTAT_SRC	0x7f00	/* Source FIFO available */
#define	HIFN_FIFOSTAT_DST	0x007f	/* Destination FIFO available */

/* FIFO Configuration Register (HIFN_0_FIFOCNFG) */
#define	HIFN_FIFOCNFG_THRESHOLD	0x0400	/* must be written as 1 */

/*
 * DMA Interface Registers (offset from BASEREG1)
 */
#define	HIFN_1_DMA_CRAR		0x0c	/* DMA Command Ring Address */
#define	HIFN_1_DMA_SRAR		0x1c	/* DMA Source Ring Address */
#define	HIFN_1_DMA_RRAR		0x2c	/* DMA Result Ring Address */
#define	HIFN_1_DMA_DRAR		0x3c	/* DMA Destination Ring Address */
#define	HIFN_1_DMA_CSR		0x40	/* DMA Status and Control */
#define	HIFN_1_DMA_IER		0x44	/* DMA Interrupt Enable */
#define	HIFN_1_DMA_CNFG		0x48	/* DMA Configuration */
#define	HIFN_1_PLL		0x4c	/* 795x: PLL config */
#define	HIFN_1_7811_RNGENA	0x60	/* 7811: rng enable */
#define	HIFN_1_7811_RNGCFG	0x64	/* 7811: rng config */
#define	HIFN_1_7811_RNGDAT	0x68	/* 7811: rng data */
#define	HIFN_1_7811_RNGSTS	0x6c	/* 7811: rng status */
#define	HIFN_1_7811_MIPSRST	0x94	/* 7811: MIPS reset */
#define	HIFN_1_REVID		0x98	/* Revision ID */
#define	HIFN_1_UNLOCK_SECRET1	0xf4
#define	HIFN_1_UNLOCK_SECRET2	0xfc
#define	HIFN_1_PUB_RESET	0x204	/* Public/RNG Reset */
#define	HIFN_1_PUB_BASE		0x300	/* Public Base Address */
#define	HIFN_1_PUB_OPLEN	0x304	/* Public Operand Length */
#define	HIFN_1_PUB_OP		0x308	/* Public Operand */
#define	HIFN_1_PUB_STATUS	0x30c	/* Public Status */
#define	HIFN_1_PUB_IEN		0x310	/* Public Interrupt enable */
#define	HIFN_1_RNG_CONFIG	0x314	/* RNG config */
#define	HIFN_1_RNG_DATA		0x318	/* RNG data */
#define	HIFN_1_PUB_MEM		0x400	/* start of Public key memory */
#define	HIFN_1_PUB_MEMEND	0xbff	/* end of Public key memory */

/* DMA Status and Control Register (HIFN_1_DMA_CSR) */
#define	HIFN_DMACSR_D_CTRLMASK	0xc0000000	/* Destinition Ring Control */
#define	HIFN_DMACSR_D_CTRL_NOP	0x00000000	/* Dest. Control: no-op */
#define	HIFN_DMACSR_D_CTRL_DIS	0x40000000	/* Dest. Control: disable */
#define	HIFN_DMACSR_D_CTRL_ENA	0x80000000	/* Dest. Control: enable */
#define	HIFN_DMACSR_D_ABORT	0x20000000	/* Destinition Ring PCIAbort */
#define	HIFN_DMACSR_D_DONE	0x10000000	/* Destinition Ring Done */
#define	HIFN_DMACSR_D_LAST	0x08000000	/* Destinition Ring Last */
#define	HIFN_DMACSR_D_WAIT	0x04000000	/* Destinition Ring Waiting */
#define	HIFN_DMACSR_D_OVER	0x02000000	/* Destinition Ring Overflow */
#define	HIFN_DMACSR_R_CTRL	0x00c00000	/* Result Ring Control */
#define	HIFN_DMACSR_R_CTRL_NOP	0x00000000	/* Result Control: no-op */
#define	HIFN_DMACSR_R_CTRL_DIS	0x00400000	/* Result Control: disable */
#define	HIFN_DMACSR_R_CTRL_ENA	0x00800000	/* Result Control: enable */
#define	HIFN_DMACSR_R_ABORT	0x00200000	/* Result Ring PCI Abort */
#define	HIFN_DMACSR_R_DONE	0x00100000	/* Result Ring Done */
#define	HIFN_DMACSR_R_LAST	0x00080000	/* Result Ring Last */
#define	HIFN_DMACSR_R_WAIT	0x00040000	/* Result Ring Waiting */
#define	HIFN_DMACSR_R_OVER	0x00020000	/* Result Ring Overflow */
#define	HIFN_DMACSR_S_CTRL	0x0000c000	/* Source Ring Control */
#define	HIFN_DMACSR_S_CTRL_NOP	0x00000000	/* Source Control: no-op */
#define	HIFN_DMACSR_S_CTRL_DIS	0x00004000	/* Source Control: disable */
#define	HIFN_DMACSR_S_CTRL_ENA	0x00008000	/* Source Control: enable */
#define	HIFN_DMACSR_S_ABORT	0x00002000	/* Source Ring PCI Abort */
#define	HIFN_DMACSR_S_DONE	0x00001000	/* Source Ring Done */
#define	HIFN_DMACSR_S_LAST	0x00000800	/* Source Ring Last */
#define	HIFN_DMACSR_S_WAIT	0x00000400	/* Source Ring Waiting */
#define	HIFN_DMACSR_ILLW	0x00000200	/* Illegal write (7811 only) */
#define	HIFN_DMACSR_ILLR	0x00000100	/* Illegal read (7811 only) */
#define	HIFN_DMACSR_C_CTRL	0x000000c0	/* Command Ring Control */
#define	HIFN_DMACSR_C_CTRL_NOP	0x00000000	/* Command Control: no-op */
#define	HIFN_DMACSR_C_CTRL_DIS	0x00000040	/* Command Control: disable */
#define	HIFN_DMACSR_C_CTRL_ENA	0x00000080	/* Command Control: enable */
#define	HIFN_DMACSR_C_ABORT	0x00000020	/* Command Ring PCI Abort */
#define	HIFN_DMACSR_C_DONE	0x00000010	/* Command Ring Done */
#define	HIFN_DMACSR_C_LAST	0x00000008	/* Command Ring Last */
#define	HIFN_DMACSR_C_WAIT	0x00000004	/* Command Ring Waiting */
#define	HIFN_DMACSR_PUBDONE	0x00000002	/* Public op done (7951 only) */
#define	HIFN_DMACSR_ENGINE	0x00000001	/* Command Ring Engine IRQ */

/* DMA Interrupt Enable Register (HIFN_1_DMA_IER) */
#define	HIFN_DMAIER_D_ABORT	0x20000000	/* Destination Ring PCIAbort */
#define	HIFN_DMAIER_D_DONE	0x10000000	/* Destination Ring Done */
#define	HIFN_DMAIER_D_LAST	0x08000000	/* Destination Ring Last */
#define	HIFN_DMAIER_D_WAIT	0x04000000	/* Destination Ring Waiting */
#define	HIFN_DMAIER_D_OVER	0x02000000	/* Destination Ring Overflow */
#define	HIFN_DMAIER_R_ABORT	0x00200000	/* Result Ring PCI Abort */
#define	HIFN_DMAIER_R_DONE	0x00100000	/* Result Ring Done */
#define	HIFN_DMAIER_R_LAST	0x00080000	/* Result Ring Last */
#define	HIFN_DMAIER_R_WAIT	0x00040000	/* Result Ring Waiting */
#define	HIFN_DMAIER_R_OVER	0x00020000	/* Result Ring Overflow */
#define	HIFN_DMAIER_S_ABORT	0x00002000	/* Source Ring PCI Abort */
#define	HIFN_DMAIER_S_DONE	0x00001000	/* Source Ring Done */
#define	HIFN_DMAIER_S_LAST	0x00000800	/* Source Ring Last */
#define	HIFN_DMAIER_S_WAIT	0x00000400	/* Source Ring Waiting */
#define	HIFN_DMAIER_ILLW	0x00000200	/* Illegal write (7811 only) */
#define	HIFN_DMAIER_ILLR	0x00000100	/* Illegal read (7811 only) */
#define	HIFN_DMAIER_C_ABORT	0x00000020	/* Command Ring PCI Abort */
#define	HIFN_DMAIER_C_DONE	0x00000010	/* Command Ring Done */
#define	HIFN_DMAIER_C_LAST	0x00000008	/* Command Ring Last */
#define	HIFN_DMAIER_C_WAIT	0x00000004	/* Command Ring Waiting */
#define	HIFN_DMAIER_PUBDONE	0x00000002	/* public op done (7951 only) */
#define	HIFN_DMAIER_ENGINE	0x00000001	/* Engine IRQ */

/* DMA Configuration Register (HIFN_1_DMA_CNFG) */
#define	HIFN_DMACNFG_BIGENDIAN	0x10000000	/* big endian mode */
#define	HIFN_DMACNFG_POLLFREQ	0x00ff0000	/* Poll frequency mask */
#define	HIFN_DMACNFG_UNLOCK	0x00000800
#define	HIFN_DMACNFG_POLLINVAL	0x00000700	/* Invalid Poll Scalar */
#define	HIFN_DMACNFG_LAST	0x00000010	/* Host control LAST bit */
#define	HIFN_DMACNFG_MODE	0x00000004	/* DMA mode */
#define	HIFN_DMACNFG_DMARESET	0x00000002	/* DMA Reset # */
#define	HIFN_DMACNFG_MSTRESET	0x00000001	/* Master Reset # */

/* PLL configuration register */
#define HIFN_PLL_REF_CLK_HBI	0x00000000	/* HBI reference clock */
#define HIFN_PLL_REF_CLK_PLL	0x00000001	/* PLL reference clock */
#define HIFN_PLL_BP		0x00000002	/* Reference clock bypass */
#define HIFN_PLL_PK_CLK_HBI	0x00000000	/* PK engine HBI clock */
#define HIFN_PLL_PK_CLK_PLL	0x00000008	/* PK engine PLL clock */
#define HIFN_PLL_PE_CLK_HBI	0x00000000	/* PE engine HBI clock */
#define HIFN_PLL_PE_CLK_PLL	0x00000010	/* PE engine PLL clock */
#define HIFN_PLL_RESERVED_1	0x00000400	/* Reserved bit, must be 1 */
#define HIFN_PLL_ND_SHIFT	11		/* Clock multiplier shift */
#define HIFN_PLL_ND_MULT_2	0x00000000	/* PLL clock multiplier 2 */
#define HIFN_PLL_ND_MULT_4	0x00000800	/* PLL clock multiplier 4 */
#define HIFN_PLL_ND_MULT_6	0x00001000	/* PLL clock multiplier 6 */
#define HIFN_PLL_ND_MULT_8	0x00001800	/* PLL clock multiplier 8 */
#define HIFN_PLL_ND_MULT_10	0x00002000	/* PLL clock multiplier 10 */
#define HIFN_PLL_ND_MULT_12	0x00002800	/* PLL clock multiplier 12 */
#define HIFN_PLL_IS_1_8		0x00000000	/* charge pump (mult. 1-8) */
#define HIFN_PLL_IS_9_12	0x00010000	/* charge pump (mult. 9-12) */

#define HIFN_PLL_FCK_MAX	266		/* Maximum PLL frequency */

/* Public key reset register (HIFN_1_PUB_RESET) */
#define	HIFN_PUBRST_RESET	0x00000001	/* reset public/rng unit */

/* Public base address register (HIFN_1_PUB_BASE) */
#define	HIFN_PUBBASE_ADDR	0x00003fff	/* base address */

/* Public operand length register (HIFN_1_PUB_OPLEN) */
#define	HIFN_PUBOPLEN_MOD_M	0x0000007f	/* modulus length mask */
#define	HIFN_PUBOPLEN_MOD_S	0		/* modulus length shift */
#define	HIFN_PUBOPLEN_EXP_M	0x0003ff80	/* exponent length mask */
#define	HIFN_PUBOPLEN_EXP_S	7		/* exponent lenght shift */
#define	HIFN_PUBOPLEN_RED_M	0x003c0000	/* reducend length mask */
#define	HIFN_PUBOPLEN_RED_S	18		/* reducend length shift */

/* Public operation register (HIFN_1_PUB_OP) */
#define	HIFN_PUBOP_AOFFSET_M	0x0000007f	/* A offset mask */
#define	HIFN_PUBOP_AOFFSET_S	0		/* A offset shift */
#define	HIFN_PUBOP_BOFFSET_M	0x00000f80	/* B offset mask */
#define	HIFN_PUBOP_BOFFSET_S	7		/* B offset shift */
#define	HIFN_PUBOP_MOFFSET_M	0x0003f000	/* M offset mask */
#define	HIFN_PUBOP_MOFFSET_S	12		/* M offset shift */
#define	HIFN_PUBOP_OP_MASK	0x003c0000	/* Opcode: */
#define	HIFN_PUBOP_OP_NOP	0x00000000	/*  NOP */
#define	HIFN_PUBOP_OP_ADD	0x00040000	/*  ADD */
#define	HIFN_PUBOP_OP_ADDC	0x00080000	/*  ADD w/carry */
#define	HIFN_PUBOP_OP_SUB	0x000c0000	/*  SUB */
#define	HIFN_PUBOP_OP_SUBC	0x00100000	/*  SUB w/carry */
#define	HIFN_PUBOP_OP_MODADD	0x00140000	/*  Modular ADD */
#define	HIFN_PUBOP_OP_MODSUB	0x00180000	/*  Modular SUB */
#define	HIFN_PUBOP_OP_INCA	0x001c0000	/*  INC A */
#define	HIFN_PUBOP_OP_DECA	0x00200000	/*  DEC A */
#define	HIFN_PUBOP_OP_MULT	0x00240000	/*  MULT */
#define	HIFN_PUBOP_OP_MODMULT	0x00280000	/*  Modular MULT */
#define	HIFN_PUBOP_OP_MODRED	0x002c0000	/*  Modular RED */
#define	HIFN_PUBOP_OP_MODEXP	0x00300000	/*  Modular EXP */

/* Public status register (HIFN_1_PUB_STATUS) */
#define	HIFN_PUBSTS_DONE	0x00000001	/* operation done */
#define	HIFN_PUBSTS_CARRY	0x00000002	/* carry */

/* Public interrupt enable register (HIFN_1_PUB_IEN) */
#define	HIFN_PUBIEN_DONE	0x00000001	/* operation done interrupt */

/* Random number generator config register (HIFN_1_RNG_CONFIG) */
#define	HIFN_RNGCFG_ENA		0x00000001	/* enable rng */

#define HIFN_NAMESIZE			32
#define HIFN_MAX_RESULT_ORDER		5

#define	HIFN_D_CMD_RSIZE		24*4
#define	HIFN_D_SRC_RSIZE		80*4
#define	HIFN_D_DST_RSIZE		80*4
#define	HIFN_D_RES_RSIZE		24*4

#define HIFN_QUEUE_LENGTH		HIFN_D_CMD_RSIZE-5

#define AES_MIN_KEY_SIZE		16
#define AES_MAX_KEY_SIZE		32

#define HIFN_DES_KEY_LENGTH		8
#define HIFN_3DES_KEY_LENGTH		24
#define HIFN_MAX_CRYPT_KEY_LENGTH	AES_MAX_KEY_SIZE
#define HIFN_IV_LENGTH			8
#define HIFN_AES_IV_LENGTH		16
#define	HIFN_MAX_IV_LENGTH		HIFN_AES_IV_LENGTH

#define HIFN_MAC_KEY_LENGTH		64
#define HIFN_MD5_LENGTH			16
#define HIFN_SHA1_LENGTH		20
#define HIFN_MAC_TRUNC_LENGTH		12

#define	HIFN_MAX_COMMAND		(8 + 8 + 8 + 64 + 260)
#define	HIFN_MAX_RESULT			(8 + 4 + 4 + 20 + 4)
#define HIFN_USED_RESULT		12

struct hifn_desc
{
	volatile u32		l;
	volatile u32		p;
};

struct hifn_dma {
	struct hifn_desc	cmdr[HIFN_D_CMD_RSIZE+1];
	struct hifn_desc	srcr[HIFN_D_SRC_RSIZE+1];
	struct hifn_desc	dstr[HIFN_D_DST_RSIZE+1];
	struct hifn_desc	resr[HIFN_D_RES_RSIZE+1];

	u8			command_bufs[HIFN_D_CMD_RSIZE][HIFN_MAX_COMMAND];
	u8			result_bufs[HIFN_D_CMD_RSIZE][HIFN_MAX_RESULT];

	u64			test_src, test_dst;

	/*
	 *  Our current positions for insertion and removal from the descriptor
	 *  rings.
	 */
	volatile int		cmdi, srci, dsti, resi;
	volatile int		cmdu, srcu, dstu, resu;
	int			cmdk, srck, dstk, resk;
};

#define HIFN_FLAG_CMD_BUSY	(1<<0)
#define HIFN_FLAG_SRC_BUSY	(1<<1)
#define HIFN_FLAG_DST_BUSY	(1<<2)
#define HIFN_FLAG_RES_BUSY	(1<<3)
#define HIFN_FLAG_OLD_KEY	(1<<4)

#define HIFN_DEFAULT_ACTIVE_NUM	5

struct hifn_device
{
	char			name[HIFN_NAMESIZE];

	int			irq;

	struct pci_dev		*pdev;
	void __iomem		*bar[3];

	unsigned long		result_mem;
	dma_addr_t		dst;

	void			*desc_virt;
	dma_addr_t		desc_dma;

	u32			dmareg;

	void 			*sa[HIFN_D_RES_RSIZE];

	spinlock_t		lock;

	void 			*priv;

	u32			flags;
	int			active, started;
	struct delayed_work	work;
	unsigned long		reset;
	unsigned long		success;
	unsigned long		prev_success;

	u8			snum;

	struct tasklet_struct	tasklet;

	struct crypto_queue 	queue;
	struct list_head	alg_list;

	unsigned int		pk_clk_freq;

#ifdef CRYPTO_DEV_HIFN_795X_RNG
	unsigned int		rng_wait_time;
	ktime_t			rngtime;
	struct hwrng		rng;
#endif
};

#define	HIFN_D_LENGTH			0x0000ffff
#define	HIFN_D_NOINVALID		0x01000000
#define	HIFN_D_MASKDONEIRQ		0x02000000
#define	HIFN_D_DESTOVER			0x04000000
#define	HIFN_D_OVER			0x08000000
#define	HIFN_D_LAST			0x20000000
#define	HIFN_D_JUMP			0x40000000
#define	HIFN_D_VALID			0x80000000

struct hifn_base_command
{
	volatile u16		masks;
	volatile u16		session_num;
	volatile u16		total_source_count;
	volatile u16		total_dest_count;
};

#define	HIFN_BASE_CMD_COMP		0x0100	/* enable compression engine */
#define	HIFN_BASE_CMD_PAD		0x0200	/* enable padding engine */
#define	HIFN_BASE_CMD_MAC		0x0400	/* enable MAC engine */
#define	HIFN_BASE_CMD_CRYPT		0x0800	/* enable crypt engine */
#define	HIFN_BASE_CMD_DECODE		0x2000
#define	HIFN_BASE_CMD_SRCLEN_M		0xc000
#define	HIFN_BASE_CMD_SRCLEN_S		14
#define	HIFN_BASE_CMD_DSTLEN_M		0x3000
#define	HIFN_BASE_CMD_DSTLEN_S		12
#define	HIFN_BASE_CMD_LENMASK_HI	0x30000
#define	HIFN_BASE_CMD_LENMASK_LO	0x0ffff

/*
 * Structure to help build up the command data structure.
 */
struct hifn_crypt_command
{
	volatile u16 		masks;
	volatile u16 		header_skip;
	volatile u16 		source_count;
	volatile u16 		reserved;
};

#define	HIFN_CRYPT_CMD_ALG_MASK		0x0003		/* algorithm: */
#define	HIFN_CRYPT_CMD_ALG_DES		0x0000		/*   DES */
#define	HIFN_CRYPT_CMD_ALG_3DES		0x0001		/*   3DES */
#define	HIFN_CRYPT_CMD_ALG_RC4		0x0002		/*   RC4 */
#define	HIFN_CRYPT_CMD_ALG_AES		0x0003		/*   AES */
#define	HIFN_CRYPT_CMD_MODE_MASK	0x0018		/* Encrypt mode: */
#define	HIFN_CRYPT_CMD_MODE_ECB		0x0000		/*   ECB */
#define	HIFN_CRYPT_CMD_MODE_CBC		0x0008		/*   CBC */
#define	HIFN_CRYPT_CMD_MODE_CFB		0x0010		/*   CFB */
#define	HIFN_CRYPT_CMD_MODE_OFB		0x0018		/*   OFB */
#define	HIFN_CRYPT_CMD_CLR_CTX		0x0040		/* clear context */
#define	HIFN_CRYPT_CMD_KSZ_MASK		0x0600		/* AES key size: */
#define	HIFN_CRYPT_CMD_KSZ_128		0x0000		/*  128 bit */
#define	HIFN_CRYPT_CMD_KSZ_192		0x0200		/*  192 bit */
#define	HIFN_CRYPT_CMD_KSZ_256		0x0400		/*  256 bit */
#define	HIFN_CRYPT_CMD_NEW_KEY		0x0800		/* expect new key */
#define	HIFN_CRYPT_CMD_NEW_IV		0x1000		/* expect new iv */
#define	HIFN_CRYPT_CMD_SRCLEN_M		0xc000
#define	HIFN_CRYPT_CMD_SRCLEN_S		14

/*
 * Structure to help build up the command data structure.
 */
struct hifn_mac_command
{
	volatile u16 		masks;
	volatile u16 		header_skip;
	volatile u16 		source_count;
	volatile u16 		reserved;
};

#define	HIFN_MAC_CMD_ALG_MASK		0x0001
#define	HIFN_MAC_CMD_ALG_SHA1		0x0000
#define	HIFN_MAC_CMD_ALG_MD5		0x0001
#define	HIFN_MAC_CMD_MODE_MASK		0x000c
#define	HIFN_MAC_CMD_MODE_HMAC		0x0000
#define	HIFN_MAC_CMD_MODE_SSL_MAC	0x0004
#define	HIFN_MAC_CMD_MODE_HASH		0x0008
#define	HIFN_MAC_CMD_MODE_FULL		0x0004
#define	HIFN_MAC_CMD_TRUNC		0x0010
#define	HIFN_MAC_CMD_RESULT		0x0020
#define	HIFN_MAC_CMD_APPEND		0x0040
#define	HIFN_MAC_CMD_SRCLEN_M		0xc000
#define	HIFN_MAC_CMD_SRCLEN_S		14

/*
 * MAC POS IPsec initiates authentication after encryption on encodes
 * and before decryption on decodes.
 */
#define	HIFN_MAC_CMD_POS_IPSEC		0x0200
#define	HIFN_MAC_CMD_NEW_KEY		0x0800

struct hifn_comp_command
{
	volatile u16 		masks;
	volatile u16 		header_skip;
	volatile u16 		source_count;
	volatile u16 		reserved;
};

#define	HIFN_COMP_CMD_SRCLEN_M		0xc000
#define	HIFN_COMP_CMD_SRCLEN_S		14
#define	HIFN_COMP_CMD_ONE		0x0100	/* must be one */
#define	HIFN_COMP_CMD_CLEARHIST		0x0010	/* clear history */
#define	HIFN_COMP_CMD_UPDATEHIST	0x0008	/* update history */
#define	HIFN_COMP_CMD_LZS_STRIP0	0x0004	/* LZS: strip zero */
#define	HIFN_COMP_CMD_MPPC_RESTART	0x0004	/* MPPC: restart */
#define	HIFN_COMP_CMD_ALG_MASK		0x0001	/* compression mode: */
#define	HIFN_COMP_CMD_ALG_MPPC		0x0001	/*   MPPC */
#define	HIFN_COMP_CMD_ALG_LZS		0x0000	/*   LZS */

struct hifn_base_result
{
	volatile u16 		flags;
	volatile u16 		session;
	volatile u16 		src_cnt;		/* 15:0 of source count */
	volatile u16 		dst_cnt;		/* 15:0 of dest count */
};

#define	HIFN_BASE_RES_DSTOVERRUN	0x0200	/* destination overrun */
#define	HIFN_BASE_RES_SRCLEN_M		0xc000	/* 17:16 of source count */
#define	HIFN_BASE_RES_SRCLEN_S		14
#define	HIFN_BASE_RES_DSTLEN_M		0x3000	/* 17:16 of dest count */
#define	HIFN_BASE_RES_DSTLEN_S		12

struct hifn_comp_result
{
	volatile u16 		flags;
	volatile u16 		crc;
};

#define	HIFN_COMP_RES_LCB_M		0xff00	/* longitudinal check byte */
#define	HIFN_COMP_RES_LCB_S		8
#define	HIFN_COMP_RES_RESTART		0x0004	/* MPPC: restart */
#define	HIFN_COMP_RES_ENDMARKER		0x0002	/* LZS: end marker seen */
#define	HIFN_COMP_RES_SRC_NOTZERO	0x0001	/* source expired */

struct hifn_mac_result
{
	volatile u16 		flags;
	volatile u16 		reserved;
	/* followed by 0, 6, 8, or 10 u16's of the MAC, then crypt */
};

#define	HIFN_MAC_RES_MISCOMPARE		0x0002	/* compare failed */
#define	HIFN_MAC_RES_SRC_NOTZERO	0x0001	/* source expired */

struct hifn_crypt_result
{
	volatile u16 		flags;
	volatile u16 		reserved;
};

#define	HIFN_CRYPT_RES_SRC_NOTZERO	0x0001	/* source expired */

#ifndef HIFN_POLL_FREQUENCY
#define	HIFN_POLL_FREQUENCY	0x1
#endif

#ifndef HIFN_POLL_SCALAR
#define	HIFN_POLL_SCALAR	0x0
#endif

#define	HIFN_MAX_SEGLEN 	0xffff		/* maximum dma segment len */
#define	HIFN_MAX_DMALEN		0x3ffff		/* maximum dma length */

struct hifn_crypto_alg
{
	struct list_head	entry;
	struct crypto_alg	alg;
	struct hifn_device	*dev;
};

#define ASYNC_SCATTERLIST_CACHE	16

#define ASYNC_FLAGS_MISALIGNED	(1<<0)

struct ablkcipher_walk
{
	struct scatterlist	cache[ASYNC_SCATTERLIST_CACHE];
	u32			flags;
	int			num;
};

struct hifn_context
{
	u8			key[HIFN_MAX_CRYPT_KEY_LENGTH], *iv;
	struct hifn_device	*dev;
	unsigned int		keysize, ivsize;
	u8			op, type, mode, unused;
	struct ablkcipher_walk	walk;
	atomic_t		sg_num;
};

#define crypto_alg_to_hifn(a)	container_of(a, struct hifn_crypto_alg, alg)

static inline u32 hifn_read_0(struct hifn_device *dev, u32 reg)
{
	u32 ret;

	ret = readl((char *)(dev->bar[0]) + reg);

	return ret;
}

static inline u32 hifn_read_1(struct hifn_device *dev, u32 reg)
{
	u32 ret;

	ret = readl((char *)(dev->bar[1]) + reg);

	return ret;
}

static inline void hifn_write_0(struct hifn_device *dev, u32 reg, u32 val)
{
	writel(val, (char *)(dev->bar[0]) + reg);
}

static inline void hifn_write_1(struct hifn_device *dev, u32 reg, u32 val)
{
	writel(val, (char *)(dev->bar[1]) + reg);
}

static void hifn_wait_puc(struct hifn_device *dev)
{
	int i;
	u32 ret;

	for (i=10000; i > 0; --i) {
		ret = hifn_read_0(dev, HIFN_0_PUCTRL);
		if (!(ret & HIFN_PUCTRL_RESET))
			break;

		udelay(1);
	}

	if (!i)
		dprintk("%s: Failed to reset PUC unit.\n", dev->name);
}

static void hifn_reset_puc(struct hifn_device *dev)
{
	hifn_write_0(dev, HIFN_0_PUCTRL, HIFN_PUCTRL_DMAENA);
	hifn_wait_puc(dev);
}

static void hifn_stop_device(struct hifn_device *dev)
{
	hifn_write_1(dev, HIFN_1_DMA_CSR,
		HIFN_DMACSR_D_CTRL_DIS | HIFN_DMACSR_R_CTRL_DIS |
		HIFN_DMACSR_S_CTRL_DIS | HIFN_DMACSR_C_CTRL_DIS);
	hifn_write_0(dev, HIFN_0_PUIER, 0);
	hifn_write_1(dev, HIFN_1_DMA_IER, 0);
}

static void hifn_reset_dma(struct hifn_device *dev, int full)
{
	hifn_stop_device(dev);

	/*
	 * Setting poll frequency and others to 0.
	 */
	hifn_write_1(dev, HIFN_1_DMA_CNFG, HIFN_DMACNFG_MSTRESET |
			HIFN_DMACNFG_DMARESET | HIFN_DMACNFG_MODE);
	mdelay(1);

	/*
	 * Reset DMA.
	 */
	if (full) {
		hifn_write_1(dev, HIFN_1_DMA_CNFG, HIFN_DMACNFG_MODE);
		mdelay(1);
	} else {
		hifn_write_1(dev, HIFN_1_DMA_CNFG, HIFN_DMACNFG_MODE |
				HIFN_DMACNFG_MSTRESET);
		hifn_reset_puc(dev);
	}

	hifn_write_1(dev, HIFN_1_DMA_CNFG, HIFN_DMACNFG_MSTRESET |
			HIFN_DMACNFG_DMARESET | HIFN_DMACNFG_MODE);

	hifn_reset_puc(dev);
}

static u32 hifn_next_signature(u_int32_t a, u_int cnt)
{
	int i;
	u32 v;

	for (i = 0; i < cnt; i++) {

		/* get the parity */
		v = a & 0x80080125;
		v ^= v >> 16;
		v ^= v >> 8;
		v ^= v >> 4;
		v ^= v >> 2;
		v ^= v >> 1;

		a = (v & 1) ^ (a << 1);
	}

	return a;
}

static struct pci2id {
	u_short		pci_vendor;
	u_short		pci_prod;
	char		card_id[13];
} pci2id[] = {
	{
		PCI_VENDOR_ID_HIFN,
		PCI_DEVICE_ID_HIFN_7955,
		{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		  0x00, 0x00, 0x00, 0x00, 0x00 }
	},
	{
		PCI_VENDOR_ID_HIFN,
		PCI_DEVICE_ID_HIFN_7956,
		{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		  0x00, 0x00, 0x00, 0x00, 0x00 }
	}
};

#ifdef CRYPTO_DEV_HIFN_795X_RNG
static int hifn_rng_data_present(struct hwrng *rng, int wait)
{
	struct hifn_device *dev = (struct hifn_device *)rng->priv;
	s64 nsec;

	nsec = ktime_to_ns(ktime_sub(ktime_get(), dev->rngtime));
	nsec -= dev->rng_wait_time;
	if (nsec <= 0)
		return 1;
	if (!wait)
		return 0;
	ndelay(nsec);
	return 1;
}

static int hifn_rng_data_read(struct hwrng *rng, u32 *data)
{
	struct hifn_device *dev = (struct hifn_device *)rng->priv;

	*data = hifn_read_1(dev, HIFN_1_RNG_DATA);
	dev->rngtime = ktime_get();
	return 4;
}

static int hifn_register_rng(struct hifn_device *dev)
{
	/*
	 * We must wait at least 256 Pk_clk cycles between two reads of the rng.
	 */
	dev->rng_wait_time	= DIV_ROUND_UP(NSEC_PER_SEC, dev->pk_clk_freq) *
				  256;

	dev->rng.name		= dev->name;
	dev->rng.data_present	= hifn_rng_data_present,
	dev->rng.data_read	= hifn_rng_data_read,
	dev->rng.priv		= (unsigned long)dev;

	return hwrng_register(&dev->rng);
}

static void hifn_unregister_rng(struct hifn_device *dev)
{
	hwrng_unregister(&dev->rng);
}
#else
#define hifn_register_rng(dev)		0
#define hifn_unregister_rng(dev)
#endif

static int hifn_init_pubrng(struct hifn_device *dev)
{
	int i;

	hifn_write_1(dev, HIFN_1_PUB_RESET, hifn_read_1(dev, HIFN_1_PUB_RESET) |
			HIFN_PUBRST_RESET);

	for (i=100; i > 0; --i) {
		mdelay(1);

		if ((hifn_read_1(dev, HIFN_1_PUB_RESET) & HIFN_PUBRST_RESET) == 0)
			break;
	}

	if (!i)
		dprintk("Chip %s: Failed to initialise public key engine.\n",
				dev->name);
	else {
		hifn_write_1(dev, HIFN_1_PUB_IEN, HIFN_PUBIEN_DONE);
		dev->dmareg |= HIFN_DMAIER_PUBDONE;
		hifn_write_1(dev, HIFN_1_DMA_IER, dev->dmareg);

		dprintk("Chip %s: Public key engine has been sucessfully "
				"initialised.\n", dev->name);
	}

	/*
	 * Enable RNG engine.
	 */

	hifn_write_1(dev, HIFN_1_RNG_CONFIG,
			hifn_read_1(dev, HIFN_1_RNG_CONFIG) | HIFN_RNGCFG_ENA);
	dprintk("Chip %s: RNG engine has been successfully initialised.\n",
			dev->name);

#ifdef CRYPTO_DEV_HIFN_795X_RNG
	/* First value must be discarded */
	hifn_read_1(dev, HIFN_1_RNG_DATA);
	dev->rngtime = ktime_get();
#endif
	return 0;
}

static int hifn_enable_crypto(struct hifn_device *dev)
{
	u32 dmacfg, addr;
	char *offtbl = NULL;
	int i;

	for (i = 0; i < sizeof(pci2id)/sizeof(pci2id[0]); i++) {
		if (pci2id[i].pci_vendor == dev->pdev->vendor &&
				pci2id[i].pci_prod == dev->pdev->device) {
			offtbl = pci2id[i].card_id;
			break;
		}
	}

	if (offtbl == NULL) {
		dprintk("Chip %s: Unknown card!\n", dev->name);
		return -ENODEV;
	}

	dmacfg = hifn_read_1(dev, HIFN_1_DMA_CNFG);

	hifn_write_1(dev, HIFN_1_DMA_CNFG,
			HIFN_DMACNFG_UNLOCK | HIFN_DMACNFG_MSTRESET |
			HIFN_DMACNFG_DMARESET | HIFN_DMACNFG_MODE);
	mdelay(1);
	addr = hifn_read_1(dev, HIFN_1_UNLOCK_SECRET1);
	mdelay(1);
	hifn_write_1(dev, HIFN_1_UNLOCK_SECRET2, 0);
	mdelay(1);

	for (i=0; i<12; ++i) {
		addr = hifn_next_signature(addr, offtbl[i] + 0x101);
		hifn_write_1(dev, HIFN_1_UNLOCK_SECRET2, addr);

		mdelay(1);
	}
	hifn_write_1(dev, HIFN_1_DMA_CNFG, dmacfg);

	dprintk("Chip %s: %s.\n", dev->name, pci_name(dev->pdev));

	return 0;
}

static void hifn_init_dma(struct hifn_device *dev)
{
	struct hifn_dma *dma = (struct hifn_dma *)dev->desc_virt;
	u32 dptr = dev->desc_dma;
	int i;

	for (i=0; i<HIFN_D_CMD_RSIZE; ++i)
		dma->cmdr[i].p = __cpu_to_le32(dptr +
				offsetof(struct hifn_dma, command_bufs[i][0]));
	for (i=0; i<HIFN_D_RES_RSIZE; ++i)
		dma->resr[i].p = __cpu_to_le32(dptr +
				offsetof(struct hifn_dma, result_bufs[i][0]));

	/*
	 * Setup LAST descriptors.
	 */
	dma->cmdr[HIFN_D_CMD_RSIZE].p = __cpu_to_le32(dptr +
			offsetof(struct hifn_dma, cmdr[0]));
	dma->srcr[HIFN_D_SRC_RSIZE].p = __cpu_to_le32(dptr +
			offsetof(struct hifn_dma, srcr[0]));
	dma->dstr[HIFN_D_DST_RSIZE].p = __cpu_to_le32(dptr +
			offsetof(struct hifn_dma, dstr[0]));
	dma->resr[HIFN_D_RES_RSIZE].p = __cpu_to_le32(dptr +
			offsetof(struct hifn_dma, resr[0]));

	dma->cmdu = dma->srcu = dma->dstu = dma->resu = 0;
	dma->cmdi = dma->srci = dma->dsti = dma->resi = 0;
	dma->cmdk = dma->srck = dma->dstk = dma->resk = 0;
}

/*
 * Initialize the PLL. We need to know the frequency of the reference clock
 * to calculate the optimal multiplier. For PCI we assume 66MHz, since that
 * allows us to operate without the risk of overclocking the chip. If it
 * actually uses 33MHz, the chip will operate at half the speed, this can be
 * overriden by specifying the frequency as module parameter (pci33).
 *
 * Unfortunately the PCI clock is not very suitable since the HIFN needs a
 * stable clock and the PCI clock frequency may vary, so the default is the
 * external clock. There is no way to find out its frequency, we default to
 * 66MHz since according to Mike Ham of HiFn, almost every board in existence
 * has an external crystal populated at 66MHz.
 */
static void hifn_init_pll(struct hifn_device *dev)
{
	unsigned int freq, m;
	u32 pllcfg;

	pllcfg = HIFN_1_PLL | HIFN_PLL_RESERVED_1;

	if (strncmp(hifn_pll_ref, "ext", 3) == 0)
		pllcfg |= HIFN_PLL_REF_CLK_PLL;
	else
		pllcfg |= HIFN_PLL_REF_CLK_HBI;

	if (hifn_pll_ref[3] != '\0')
		freq = simple_strtoul(hifn_pll_ref + 3, NULL, 10);
	else {
		freq = 66;
		printk(KERN_INFO "hifn795x: assuming %uMHz clock speed, "
				 "override with hifn_pll_ref=%.3s<frequency>\n",
		       freq, hifn_pll_ref);
	}

	m = HIFN_PLL_FCK_MAX / freq;

	pllcfg |= (m / 2 - 1) << HIFN_PLL_ND_SHIFT;
	if (m <= 8)
		pllcfg |= HIFN_PLL_IS_1_8;
	else
		pllcfg |= HIFN_PLL_IS_9_12;

	/* Select clock source and enable clock bypass */
	hifn_write_1(dev, HIFN_1_PLL, pllcfg |
		     HIFN_PLL_PK_CLK_HBI | HIFN_PLL_PE_CLK_HBI | HIFN_PLL_BP);

	/* Let the chip lock to the input clock */
	mdelay(10);

	/* Disable clock bypass */
	hifn_write_1(dev, HIFN_1_PLL, pllcfg |
		     HIFN_PLL_PK_CLK_HBI | HIFN_PLL_PE_CLK_HBI);

	/* Switch the engines to the PLL */
	hifn_write_1(dev, HIFN_1_PLL, pllcfg |
		     HIFN_PLL_PK_CLK_PLL | HIFN_PLL_PE_CLK_PLL);

	/*
	 * The Fpk_clk runs at half the total speed. Its frequency is needed to
	 * calculate the minimum time between two reads of the rng. Since 33MHz
	 * is actually 33.333... we overestimate the frequency here, resulting
	 * in slightly larger intervals.
	 */
	dev->pk_clk_freq = 1000000 * (freq + 1) * m / 2;
}

static void hifn_init_registers(struct hifn_device *dev)
{
	u32 dptr = dev->desc_dma;

	/* Initialization magic... */
	hifn_write_0(dev, HIFN_0_PUCTRL, HIFN_PUCTRL_DMAENA);
	hifn_write_0(dev, HIFN_0_FIFOCNFG, HIFN_FIFOCNFG_THRESHOLD);
	hifn_write_0(dev, HIFN_0_PUIER, HIFN_PUIER_DSTOVER);

	/* write all 4 ring address registers */
	hifn_write_1(dev, HIFN_1_DMA_CRAR, __cpu_to_le32(dptr +
				offsetof(struct hifn_dma, cmdr[0])));
	hifn_write_1(dev, HIFN_1_DMA_SRAR, __cpu_to_le32(dptr +
				offsetof(struct hifn_dma, srcr[0])));
	hifn_write_1(dev, HIFN_1_DMA_DRAR, __cpu_to_le32(dptr +
				offsetof(struct hifn_dma, dstr[0])));
	hifn_write_1(dev, HIFN_1_DMA_RRAR, __cpu_to_le32(dptr +
				offsetof(struct hifn_dma, resr[0])));

	mdelay(2);
#if 0
	hifn_write_1(dev, HIFN_1_DMA_CSR,
	    HIFN_DMACSR_D_CTRL_DIS | HIFN_DMACSR_R_CTRL_DIS |
	    HIFN_DMACSR_S_CTRL_DIS | HIFN_DMACSR_C_CTRL_DIS |
	    HIFN_DMACSR_D_ABORT | HIFN_DMACSR_D_DONE | HIFN_DMACSR_D_LAST |
	    HIFN_DMACSR_D_WAIT | HIFN_DMACSR_D_OVER |
	    HIFN_DMACSR_R_ABORT | HIFN_DMACSR_R_DONE | HIFN_DMACSR_R_LAST |
	    HIFN_DMACSR_R_WAIT | HIFN_DMACSR_R_OVER |
	    HIFN_DMACSR_S_ABORT | HIFN_DMACSR_S_DONE | HIFN_DMACSR_S_LAST |
	    HIFN_DMACSR_S_WAIT |
	    HIFN_DMACSR_C_ABORT | HIFN_DMACSR_C_DONE | HIFN_DMACSR_C_LAST |
	    HIFN_DMACSR_C_WAIT |
	    HIFN_DMACSR_ENGINE |
	    HIFN_DMACSR_PUBDONE);
#else
	hifn_write_1(dev, HIFN_1_DMA_CSR,
	    HIFN_DMACSR_C_CTRL_ENA | HIFN_DMACSR_S_CTRL_ENA |
	    HIFN_DMACSR_D_CTRL_ENA | HIFN_DMACSR_R_CTRL_ENA |
	    HIFN_DMACSR_D_ABORT | HIFN_DMACSR_D_DONE | HIFN_DMACSR_D_LAST |
	    HIFN_DMACSR_D_WAIT | HIFN_DMACSR_D_OVER |
	    HIFN_DMACSR_R_ABORT | HIFN_DMACSR_R_DONE | HIFN_DMACSR_R_LAST |
	    HIFN_DMACSR_R_WAIT | HIFN_DMACSR_R_OVER |
	    HIFN_DMACSR_S_ABORT | HIFN_DMACSR_S_DONE | HIFN_DMACSR_S_LAST |
	    HIFN_DMACSR_S_WAIT |
	    HIFN_DMACSR_C_ABORT | HIFN_DMACSR_C_DONE | HIFN_DMACSR_C_LAST |
	    HIFN_DMACSR_C_WAIT |
	    HIFN_DMACSR_ENGINE |
	    HIFN_DMACSR_PUBDONE);
#endif
	hifn_read_1(dev, HIFN_1_DMA_CSR);

	dev->dmareg |= HIFN_DMAIER_R_DONE | HIFN_DMAIER_C_ABORT |
	    HIFN_DMAIER_D_OVER | HIFN_DMAIER_R_OVER |
	    HIFN_DMAIER_S_ABORT | HIFN_DMAIER_D_ABORT | HIFN_DMAIER_R_ABORT |
	    HIFN_DMAIER_ENGINE;
	dev->dmareg &= ~HIFN_DMAIER_C_WAIT;

	hifn_write_1(dev, HIFN_1_DMA_IER, dev->dmareg);
	hifn_read_1(dev, HIFN_1_DMA_IER);
#if 0
	hifn_write_0(dev, HIFN_0_PUCNFG, HIFN_PUCNFG_ENCCNFG |
		    HIFN_PUCNFG_DRFR_128 | HIFN_PUCNFG_TCALLPHASES |
		    HIFN_PUCNFG_TCDRVTOTEM | HIFN_PUCNFG_BUS32 |
		    HIFN_PUCNFG_DRAM);
#else
	hifn_write_0(dev, HIFN_0_PUCNFG, 0x10342);
#endif
	hifn_init_pll(dev);

	hifn_write_0(dev, HIFN_0_PUISR, HIFN_PUISR_DSTOVER);
	hifn_write_1(dev, HIFN_1_DMA_CNFG, HIFN_DMACNFG_MSTRESET |
	    HIFN_DMACNFG_DMARESET | HIFN_DMACNFG_MODE | HIFN_DMACNFG_LAST |
	    ((HIFN_POLL_FREQUENCY << 16 ) & HIFN_DMACNFG_POLLFREQ) |
	    ((HIFN_POLL_SCALAR << 8) & HIFN_DMACNFG_POLLINVAL));
}

static int hifn_setup_base_command(struct hifn_device *dev, u8 *buf,
		unsigned dlen, unsigned slen, u16 mask, u8 snum)
{
	struct hifn_base_command *base_cmd;
	u8 *buf_pos = buf;

	base_cmd = (struct hifn_base_command *)buf_pos;
	base_cmd->masks = __cpu_to_le16(mask);
	base_cmd->total_source_count =
		__cpu_to_le16(slen & HIFN_BASE_CMD_LENMASK_LO);
	base_cmd->total_dest_count =
		__cpu_to_le16(dlen & HIFN_BASE_CMD_LENMASK_LO);

	dlen >>= 16;
	slen >>= 16;
	base_cmd->session_num = __cpu_to_le16(snum |
	    ((slen << HIFN_BASE_CMD_SRCLEN_S) & HIFN_BASE_CMD_SRCLEN_M) |
	    ((dlen << HIFN_BASE_CMD_DSTLEN_S) & HIFN_BASE_CMD_DSTLEN_M));

	return sizeof(struct hifn_base_command);
}

static int hifn_setup_crypto_command(struct hifn_device *dev,
		u8 *buf, unsigned dlen, unsigned slen,
		u8 *key, int keylen, u8 *iv, int ivsize, u16 mode)
{
	struct hifn_dma *dma = (struct hifn_dma *)dev->desc_virt;
	struct hifn_crypt_command *cry_cmd;
	u8 *buf_pos = buf;
	u16 cmd_len;

	cry_cmd = (struct hifn_crypt_command *)buf_pos;

	cry_cmd->source_count = __cpu_to_le16(dlen & 0xffff);
	dlen >>= 16;
	cry_cmd->masks = __cpu_to_le16(mode |
			((dlen << HIFN_CRYPT_CMD_SRCLEN_S) &
			 HIFN_CRYPT_CMD_SRCLEN_M));
	cry_cmd->header_skip = 0;
	cry_cmd->reserved = 0;

	buf_pos += sizeof(struct hifn_crypt_command);

	dma->cmdu++;
	if (dma->cmdu > 1) {
		dev->dmareg |= HIFN_DMAIER_C_WAIT;
		hifn_write_1(dev, HIFN_1_DMA_IER, dev->dmareg);
	}

	if (keylen) {
		memcpy(buf_pos, key, keylen);
		buf_pos += keylen;
	}
	if (ivsize) {
		memcpy(buf_pos, iv, ivsize);
		buf_pos += ivsize;
	}

	cmd_len = buf_pos - buf;

	return cmd_len;
}

static int hifn_setup_src_desc(struct hifn_device *dev, struct page *page,
		unsigned int offset, unsigned int size)
{
	struct hifn_dma *dma = (struct hifn_dma *)dev->desc_virt;
	int idx;
	dma_addr_t addr;

	addr = pci_map_page(dev->pdev, page, offset, size, PCI_DMA_TODEVICE);

	idx = dma->srci;

	dma->srcr[idx].p = __cpu_to_le32(addr);
	dma->srcr[idx].l = __cpu_to_le32(size) | HIFN_D_VALID |
			HIFN_D_MASKDONEIRQ | HIFN_D_NOINVALID | HIFN_D_LAST;

	if (++idx == HIFN_D_SRC_RSIZE) {
		dma->srcr[idx].l = __cpu_to_le32(HIFN_D_VALID |
				HIFN_D_JUMP |
				HIFN_D_MASKDONEIRQ | HIFN_D_LAST);
		idx = 0;
	}

	dma->srci = idx;
	dma->srcu++;

	if (!(dev->flags & HIFN_FLAG_SRC_BUSY)) {
		hifn_write_1(dev, HIFN_1_DMA_CSR, HIFN_DMACSR_S_CTRL_ENA);
		dev->flags |= HIFN_FLAG_SRC_BUSY;
	}

	return size;
}

static void hifn_setup_res_desc(struct hifn_device *dev)
{
	struct hifn_dma *dma = (struct hifn_dma *)dev->desc_virt;

	dma->resr[dma->resi].l = __cpu_to_le32(HIFN_USED_RESULT |
			HIFN_D_VALID | HIFN_D_LAST);
	/*
	 * dma->resr[dma->resi].l = __cpu_to_le32(HIFN_MAX_RESULT | HIFN_D_VALID |
	 *					HIFN_D_LAST | HIFN_D_NOINVALID);
	 */

	if (++dma->resi == HIFN_D_RES_RSIZE) {
		dma->resr[HIFN_D_RES_RSIZE].l = __cpu_to_le32(HIFN_D_VALID |
				HIFN_D_JUMP | HIFN_D_MASKDONEIRQ | HIFN_D_LAST);
		dma->resi = 0;
	}

	dma->resu++;

	if (!(dev->flags & HIFN_FLAG_RES_BUSY)) {
		hifn_write_1(dev, HIFN_1_DMA_CSR, HIFN_DMACSR_R_CTRL_ENA);
		dev->flags |= HIFN_FLAG_RES_BUSY;
	}
}

static void hifn_setup_dst_desc(struct hifn_device *dev, struct page *page,
		unsigned offset, unsigned size)
{
	struct hifn_dma *dma = (struct hifn_dma *)dev->desc_virt;
	int idx;
	dma_addr_t addr;

	addr = pci_map_page(dev->pdev, page, offset, size, PCI_DMA_FROMDEVICE);

	idx = dma->dsti;
	dma->dstr[idx].p = __cpu_to_le32(addr);
	dma->dstr[idx].l = __cpu_to_le32(size |	HIFN_D_VALID |
			HIFN_D_MASKDONEIRQ | HIFN_D_NOINVALID | HIFN_D_LAST);

	if (++idx == HIFN_D_DST_RSIZE) {
		dma->dstr[idx].l = __cpu_to_le32(HIFN_D_VALID |
				HIFN_D_JUMP | HIFN_D_MASKDONEIRQ |
				HIFN_D_LAST | HIFN_D_NOINVALID);
		idx = 0;
	}
	dma->dsti = idx;
	dma->dstu++;

	if (!(dev->flags & HIFN_FLAG_DST_BUSY)) {
		hifn_write_1(dev, HIFN_1_DMA_CSR, HIFN_DMACSR_D_CTRL_ENA);
		dev->flags |= HIFN_FLAG_DST_BUSY;
	}
}

static int hifn_setup_dma(struct hifn_device *dev, struct page *spage, unsigned int soff,
		struct page *dpage, unsigned int doff, unsigned int nbytes, void *priv,
		struct hifn_context *ctx)
{
	struct hifn_dma *dma = (struct hifn_dma *)dev->desc_virt;
	int cmd_len, sa_idx;
	u8 *buf, *buf_pos;
	u16 mask;

	dprintk("%s: spage: %p, soffset: %u, dpage: %p, doffset: %u, nbytes: %u, priv: %p, ctx: %p.\n",
			dev->name, spage, soff, dpage, doff, nbytes, priv, ctx);

	sa_idx = dma->resi;

	hifn_setup_src_desc(dev, spage, soff, nbytes);

	buf_pos = buf = dma->command_bufs[dma->cmdi];

	mask = 0;
	switch (ctx->op) {
		case ACRYPTO_OP_DECRYPT:
			mask = HIFN_BASE_CMD_CRYPT | HIFN_BASE_CMD_DECODE;
			break;
		case ACRYPTO_OP_ENCRYPT:
			mask = HIFN_BASE_CMD_CRYPT;
			break;
		case ACRYPTO_OP_HMAC:
			mask = HIFN_BASE_CMD_MAC;
			break;
		default:
			goto err_out;
	}

	buf_pos += hifn_setup_base_command(dev, buf_pos, nbytes,
			nbytes, mask, dev->snum);

	if (ctx->op == ACRYPTO_OP_ENCRYPT || ctx->op == ACRYPTO_OP_DECRYPT) {
		u16 md = 0;

		if (ctx->keysize)
			md |= HIFN_CRYPT_CMD_NEW_KEY;
		if (ctx->iv && ctx->mode != ACRYPTO_MODE_ECB)
			md |= HIFN_CRYPT_CMD_NEW_IV;

		switch (ctx->mode) {
			case ACRYPTO_MODE_ECB:
				md |= HIFN_CRYPT_CMD_MODE_ECB;
				break;
			case ACRYPTO_MODE_CBC:
				md |= HIFN_CRYPT_CMD_MODE_CBC;
				break;
			case ACRYPTO_MODE_CFB:
				md |= HIFN_CRYPT_CMD_MODE_CFB;
				break;
			case ACRYPTO_MODE_OFB:
				md |= HIFN_CRYPT_CMD_MODE_OFB;
				break;
			default:
				goto err_out;
		}

		switch (ctx->type) {
			case ACRYPTO_TYPE_AES_128:
				if (ctx->keysize != 16)
					goto err_out;
				md |= HIFN_CRYPT_CMD_KSZ_128 |
					HIFN_CRYPT_CMD_ALG_AES;
				break;
			case ACRYPTO_TYPE_AES_192:
				if (ctx->keysize != 24)
					goto err_out;
				md |= HIFN_CRYPT_CMD_KSZ_192 |
					HIFN_CRYPT_CMD_ALG_AES;
				break;
			case ACRYPTO_TYPE_AES_256:
				if (ctx->keysize != 32)
					goto err_out;
				md |= HIFN_CRYPT_CMD_KSZ_256 |
					HIFN_CRYPT_CMD_ALG_AES;
				break;
			case ACRYPTO_TYPE_3DES:
				if (ctx->keysize != 24)
					goto err_out;
				md |= HIFN_CRYPT_CMD_ALG_3DES;
				break;
			case ACRYPTO_TYPE_DES:
				if (ctx->keysize != 8)
					goto err_out;
				md |= HIFN_CRYPT_CMD_ALG_DES;
				break;
			default:
				goto err_out;
		}

		buf_pos += hifn_setup_crypto_command(dev, buf_pos,
				nbytes, nbytes, ctx->key, ctx->keysize,
				ctx->iv, ctx->ivsize, md);
	}

	dev->sa[sa_idx] = priv;

	cmd_len = buf_pos - buf;
	dma->cmdr[dma->cmdi].l = __cpu_to_le32(cmd_len | HIFN_D_VALID |
			HIFN_D_LAST | HIFN_D_MASKDONEIRQ);

	if (++dma->cmdi == HIFN_D_CMD_RSIZE) {
		dma->cmdr[dma->cmdi].l = __cpu_to_le32(HIFN_MAX_COMMAND |
			HIFN_D_VALID | HIFN_D_LAST |
			HIFN_D_MASKDONEIRQ | HIFN_D_JUMP);
		dma->cmdi = 0;
	} else
		dma->cmdr[dma->cmdi-1].l |= __cpu_to_le32(HIFN_D_VALID);

	if (!(dev->flags & HIFN_FLAG_CMD_BUSY)) {
		hifn_write_1(dev, HIFN_1_DMA_CSR, HIFN_DMACSR_C_CTRL_ENA);
		dev->flags |= HIFN_FLAG_CMD_BUSY;
	}

	hifn_setup_dst_desc(dev, dpage, doff, nbytes);
	hifn_setup_res_desc(dev);

	return 0;

err_out:
	return -EINVAL;
}

static int ablkcipher_walk_init(struct ablkcipher_walk *w,
		int num, gfp_t gfp_flags)
{
	int i;

	num = min(ASYNC_SCATTERLIST_CACHE, num);
	sg_init_table(w->cache, num);

	w->num = 0;
	for (i=0; i<num; ++i) {
		struct page *page = alloc_page(gfp_flags);
		struct scatterlist *s;

		if (!page)
			break;

		s = &w->cache[i];

		sg_set_page(s, page, PAGE_SIZE, 0);
		w->num++;
	}

	return i;
}

static void ablkcipher_walk_exit(struct ablkcipher_walk *w)
{
	int i;

	for (i=0; i<w->num; ++i) {
		struct scatterlist *s = &w->cache[i];

		__free_page(sg_page(s));

		s->length = 0;
	}

	w->num = 0;
}

static int ablkcipher_add(void *daddr, unsigned int *drestp, struct scatterlist *src,
		unsigned int size, unsigned int *nbytesp)
{
	unsigned int copy, drest = *drestp, nbytes = *nbytesp;
	int idx = 0;
	void *saddr;

	if (drest < size || size > nbytes)
		return -EINVAL;

	while (size) {
		copy = min(drest, src->length);

		saddr = kmap_atomic(sg_page(src), KM_SOFTIRQ1);
		memcpy(daddr, saddr + src->offset, copy);
		kunmap_atomic(saddr, KM_SOFTIRQ1);

		size -= copy;
		drest -= copy;
		nbytes -= copy;
		daddr += copy;

		dprintk("%s: copy: %u, size: %u, drest: %u, nbytes: %u.\n",
				__func__, copy, size, drest, nbytes);

		src++;
		idx++;
	}

	*nbytesp = nbytes;
	*drestp = drest;

	return idx;
}

static int ablkcipher_walk(struct ablkcipher_request *req,
		struct ablkcipher_walk *w)
{
	unsigned blocksize =
		crypto_ablkcipher_blocksize(crypto_ablkcipher_reqtfm(req));
	unsigned alignmask =
		crypto_ablkcipher_alignmask(crypto_ablkcipher_reqtfm(req));
	struct scatterlist *src, *dst, *t;
	void *daddr;
	unsigned int nbytes = req->nbytes, offset, copy, diff;
	int idx, tidx, err;

	tidx = idx = 0;
	offset = 0;
	while (nbytes) {
		if (idx >= w->num && (w->flags & ASYNC_FLAGS_MISALIGNED))
			return -EINVAL;

		src = &req->src[idx];
		dst = &req->dst[idx];

		dprintk("\n%s: slen: %u, dlen: %u, soff: %u, doff: %u, offset: %u, "
				"blocksize: %u, nbytes: %u.\n",
				__func__, src->length, dst->length, src->offset,
				dst->offset, offset, blocksize, nbytes);

		if (src->length & (blocksize - 1) ||
				src->offset & (alignmask - 1) ||
				dst->length & (blocksize - 1) ||
				dst->offset & (alignmask - 1) ||
				offset) {
			unsigned slen = src->length - offset;
			unsigned dlen = PAGE_SIZE;

			t = &w->cache[idx];

			daddr = kmap_atomic(sg_page(t), KM_SOFTIRQ0);
			err = ablkcipher_add(daddr, &dlen, src, slen, &nbytes);
			if (err < 0)
				goto err_out_unmap;

			idx += err;

			copy = slen & ~(blocksize - 1);
			diff = slen & (blocksize - 1);

			if (dlen < nbytes) {
				/*
				 * Destination page does not have enough space
				 * to put there additional blocksized chunk,
				 * so we mark that page as containing only
				 * blocksize aligned chunks:
				 * 	t->length = (slen & ~(blocksize - 1));
				 * and increase number of bytes to be processed
				 * in next chunk:
				 * 	nbytes += diff;
				 */
				nbytes += diff;

				/*
				 * Temporary of course...
				 * Kick author if you will catch this one.
				 */
				printk(KERN_ERR "%s: dlen: %u, nbytes: %u,"
					"slen: %u, offset: %u.\n",
					__func__, dlen, nbytes, slen, offset);
				printk(KERN_ERR "%s: please contact author to fix this "
					"issue, generally you should not catch "
					"this path under any condition but who "
					"knows how did you use crypto code.\n"
					"Thank you.\n",	__func__);
				BUG();
			} else {
				copy += diff + nbytes;

				src = &req->src[idx];

				err = ablkcipher_add(daddr + slen, &dlen, src, nbytes, &nbytes);
				if (err < 0)
					goto err_out_unmap;

				idx += err;
			}

			t->length = copy;
			t->offset = offset;

			kunmap_atomic(daddr, KM_SOFTIRQ0);
		} else {
			nbytes -= src->length;
			idx++;
		}

		tidx++;
	}

	return tidx;

err_out_unmap:
	kunmap_atomic(daddr, KM_SOFTIRQ0);
	return err;
}

static int hifn_setup_session(struct ablkcipher_request *req)
{
	struct hifn_context *ctx = crypto_tfm_ctx(req->base.tfm);
	struct hifn_device *dev = ctx->dev;
	struct page *spage, *dpage;
	unsigned long soff, doff, flags;
	unsigned int nbytes = req->nbytes, idx = 0, len;
	int err = -EINVAL, sg_num;
	struct scatterlist *src, *dst, *t;
	unsigned blocksize =
		crypto_ablkcipher_blocksize(crypto_ablkcipher_reqtfm(req));
	unsigned alignmask =
		crypto_ablkcipher_alignmask(crypto_ablkcipher_reqtfm(req));

	if (ctx->iv && !ctx->ivsize && ctx->mode != ACRYPTO_MODE_ECB)
		goto err_out_exit;

	ctx->walk.flags = 0;

	while (nbytes) {
		src = &req->src[idx];
		dst = &req->dst[idx];

		if (src->length & (blocksize - 1) ||
				src->offset & (alignmask - 1) ||
				dst->length & (blocksize - 1) ||
				dst->offset & (alignmask - 1)) {
			ctx->walk.flags |= ASYNC_FLAGS_MISALIGNED;
		}

		nbytes -= src->length;
		idx++;
	}

	if (ctx->walk.flags & ASYNC_FLAGS_MISALIGNED) {
		err = ablkcipher_walk_init(&ctx->walk, idx, GFP_ATOMIC);
		if (err < 0)
			return err;
	}

	nbytes = req->nbytes;
	idx = 0;

	sg_num = ablkcipher_walk(req, &ctx->walk);

	atomic_set(&ctx->sg_num, sg_num);

	spin_lock_irqsave(&dev->lock, flags);
	if (dev->started + sg_num > HIFN_QUEUE_LENGTH) {
		err = -EAGAIN;
		goto err_out;
	}

	dev->snum++;
	dev->started += sg_num;

	while (nbytes) {
		src = &req->src[idx];
		dst = &req->dst[idx];
		t = &ctx->walk.cache[idx];

		if (t->length) {
			spage = dpage = sg_page(t);
			soff = doff = 0;
			len = t->length;
		} else {
			spage = sg_page(src);
			soff = src->offset;

			dpage = sg_page(dst);
			doff = dst->offset;

			len = dst->length;
		}

		idx++;

		err = hifn_setup_dma(dev, spage, soff, dpage, doff, nbytes,
				req, ctx);
		if (err)
			goto err_out;

		nbytes -= len;
	}

	dev->active = HIFN_DEFAULT_ACTIVE_NUM;
	spin_unlock_irqrestore(&dev->lock, flags);

	return 0;

err_out:
	spin_unlock_irqrestore(&dev->lock, flags);
err_out_exit:
	if (err && printk_ratelimit())
		dprintk("%s: iv: %p [%d], key: %p [%d], mode: %u, op: %u, "
				"type: %u, err: %d.\n",
			dev->name, ctx->iv, ctx->ivsize,
			ctx->key, ctx->keysize,
			ctx->mode, ctx->op, ctx->type, err);

	return err;
}

static int hifn_test(struct hifn_device *dev, int encdec, u8 snum)
{
	int n, err;
	u8 src[16];
	struct hifn_context ctx;
	u8 fips_aes_ecb_from_zero[16] = {
		0x66, 0xE9, 0x4B, 0xD4,
		0xEF, 0x8A, 0x2C, 0x3B,
		0x88, 0x4C, 0xFA, 0x59,
		0xCA, 0x34, 0x2B, 0x2E};

	memset(src, 0, sizeof(src));
	memset(ctx.key, 0, sizeof(ctx.key));

	ctx.dev = dev;
	ctx.keysize = 16;
	ctx.ivsize = 0;
	ctx.iv = NULL;
	ctx.op = (encdec)?ACRYPTO_OP_ENCRYPT:ACRYPTO_OP_DECRYPT;
	ctx.mode = ACRYPTO_MODE_ECB;
	ctx.type = ACRYPTO_TYPE_AES_128;
	atomic_set(&ctx.sg_num, 1);

	err = hifn_setup_dma(dev,
			virt_to_page(src), offset_in_page(src),
			virt_to_page(src), offset_in_page(src),
			sizeof(src), NULL, &ctx);
	if (err)
		goto err_out;

	msleep(200);

	dprintk("%s: decoded: ", dev->name);
	for (n=0; n<sizeof(src); ++n)
		dprintk("%02x ", src[n]);
	dprintk("\n");
	dprintk("%s: FIPS   : ", dev->name);
	for (n=0; n<sizeof(fips_aes_ecb_from_zero); ++n)
		dprintk("%02x ", fips_aes_ecb_from_zero[n]);
	dprintk("\n");

	if (!memcmp(src, fips_aes_ecb_from_zero, sizeof(fips_aes_ecb_from_zero))) {
		printk(KERN_INFO "%s: AES 128 ECB test has been successfully "
				"passed.\n", dev->name);
		return 0;
	}

err_out:
	printk(KERN_INFO "%s: AES 128 ECB test has been failed.\n", dev->name);
	return -1;
}

static int hifn_start_device(struct hifn_device *dev)
{
	int err;

	hifn_reset_dma(dev, 1);

	err = hifn_enable_crypto(dev);
	if (err)
		return err;

	hifn_reset_puc(dev);

	hifn_init_dma(dev);

	hifn_init_registers(dev);

	hifn_init_pubrng(dev);

	return 0;
}

static int ablkcipher_get(void *saddr, unsigned int *srestp, unsigned int offset,
		struct scatterlist *dst, unsigned int size, unsigned int *nbytesp)
{
	unsigned int srest = *srestp, nbytes = *nbytesp, copy;
	void *daddr;
	int idx = 0;

	if (srest < size || size > nbytes)
		return -EINVAL;

	while (size) {

		copy = min(dst->length, srest);

		daddr = kmap_atomic(sg_page(dst), KM_IRQ0);
		memcpy(daddr + dst->offset + offset, saddr, copy);
		kunmap_atomic(daddr, KM_IRQ0);

		nbytes -= copy;
		size -= copy;
		srest -= copy;
		saddr += copy;
		offset = 0;

		dprintk("%s: copy: %u, size: %u, srest: %u, nbytes: %u.\n",
				__func__, copy, size, srest, nbytes);

		dst++;
		idx++;
	}

	*nbytesp = nbytes;
	*srestp = srest;

	return idx;
}

static void hifn_process_ready(struct ablkcipher_request *req, int error)
{
	struct hifn_context *ctx = crypto_tfm_ctx(req->base.tfm);
	struct hifn_device *dev;

	dprintk("%s: req: %p, ctx: %p.\n", __func__, req, ctx);

	dev = ctx->dev;
	dprintk("%s: req: %p, started: %d, sg_num: %d.\n",
		__func__, req, dev->started, atomic_read(&ctx->sg_num));

	if (--dev->started < 0)
		BUG();

	if (atomic_dec_and_test(&ctx->sg_num)) {
		unsigned int nbytes = req->nbytes;
		int idx = 0, err;
		struct scatterlist *dst, *t;
		void *saddr;

		if (ctx->walk.flags & ASYNC_FLAGS_MISALIGNED) {
			while (nbytes) {
				t = &ctx->walk.cache[idx];
				dst = &req->dst[idx];

				dprintk("\n%s: sg_page(t): %p, t->length: %u, "
					"sg_page(dst): %p, dst->length: %u, "
					"nbytes: %u.\n",
					__func__, sg_page(t), t->length,
					sg_page(dst), dst->length, nbytes);

				if (!t->length) {
					nbytes -= dst->length;
					idx++;
					continue;
				}

				saddr = kmap_atomic(sg_page(t), KM_IRQ1);

				err = ablkcipher_get(saddr, &t->length, t->offset,
						dst, nbytes, &nbytes);
				if (err < 0) {
					kunmap_atomic(saddr, KM_IRQ1);
					break;
				}

				idx += err;
				kunmap_atomic(saddr, KM_IRQ1);
			}

			ablkcipher_walk_exit(&ctx->walk);
		}

		req->base.complete(&req->base, error);
	}
}

static void hifn_check_for_completion(struct hifn_device *dev, int error)
{
	int i;
	struct hifn_dma *dma = (struct hifn_dma *)dev->desc_virt;

	for (i=0; i<HIFN_D_RES_RSIZE; ++i) {
		struct hifn_desc *d = &dma->resr[i];

		if (!(d->l & __cpu_to_le32(HIFN_D_VALID)) && dev->sa[i]) {
			dev->success++;
			dev->reset = 0;
			hifn_process_ready(dev->sa[i], error);
			dev->sa[i] = NULL;
		}

		if (d->l & __cpu_to_le32(HIFN_D_DESTOVER | HIFN_D_OVER))
			if (printk_ratelimit())
				printk("%s: overflow detected [d: %u, o: %u] "
						"at %d resr: l: %08x, p: %08x.\n",
					dev->name,
					!!(d->l & __cpu_to_le32(HIFN_D_DESTOVER)),
					!!(d->l & __cpu_to_le32(HIFN_D_OVER)),
					i, d->l, d->p);
	}
}

static void hifn_clear_rings(struct hifn_device *dev)
{
	struct hifn_dma *dma = (struct hifn_dma *)dev->desc_virt;
	int i, u;

	dprintk("%s: ring cleanup 1: i: %d.%d.%d.%d, u: %d.%d.%d.%d, "
			"k: %d.%d.%d.%d.\n",
			dev->name,
			dma->cmdi, dma->srci, dma->dsti, dma->resi,
			dma->cmdu, dma->srcu, dma->dstu, dma->resu,
			dma->cmdk, dma->srck, dma->dstk, dma->resk);

	i = dma->resk; u = dma->resu;
	while (u != 0) {
		if (dma->resr[i].l & __cpu_to_le32(HIFN_D_VALID))
			break;

		if (i != HIFN_D_RES_RSIZE)
			u--;

		if (++i == (HIFN_D_RES_RSIZE + 1))
			i = 0;
	}
	dma->resk = i; dma->resu = u;

	i = dma->srck; u = dma->srcu;
	while (u != 0) {
		if (i == HIFN_D_SRC_RSIZE)
			i = 0;
		if (dma->srcr[i].l & __cpu_to_le32(HIFN_D_VALID))
			break;
		i++, u--;
	}
	dma->srck = i; dma->srcu = u;

	i = dma->cmdk; u = dma->cmdu;
	while (u != 0) {
		if (dma->cmdr[i].l & __cpu_to_le32(HIFN_D_VALID))
			break;
		if (i != HIFN_D_CMD_RSIZE)
			u--;
		if (++i == (HIFN_D_CMD_RSIZE + 1))
			i = 0;
	}
	dma->cmdk = i; dma->cmdu = u;

	i = dma->dstk; u = dma->dstu;
	while (u != 0) {
		if (i == HIFN_D_DST_RSIZE)
			i = 0;
		if (dma->dstr[i].l & __cpu_to_le32(HIFN_D_VALID))
			break;
		i++, u--;
	}
	dma->dstk = i; dma->dstu = u;

	dprintk("%s: ring cleanup 2: i: %d.%d.%d.%d, u: %d.%d.%d.%d, "
			"k: %d.%d.%d.%d.\n",
			dev->name,
			dma->cmdi, dma->srci, dma->dsti, dma->resi,
			dma->cmdu, dma->srcu, dma->dstu, dma->resu,
			dma->cmdk, dma->srck, dma->dstk, dma->resk);
}

static void hifn_work(struct work_struct *work)
{
	struct delayed_work *dw = container_of(work, struct delayed_work, work);
	struct hifn_device *dev = container_of(dw, struct hifn_device, work);
	unsigned long flags;
	int reset = 0;
	u32 r = 0;

	spin_lock_irqsave(&dev->lock, flags);
	if (dev->active == 0) {
		struct hifn_dma *dma = (struct hifn_dma *)dev->desc_virt;

		if (dma->cmdu == 0 && (dev->flags & HIFN_FLAG_CMD_BUSY)) {
			dev->flags &= ~HIFN_FLAG_CMD_BUSY;
			r |= HIFN_DMACSR_C_CTRL_DIS;
		}
		if (dma->srcu == 0 && (dev->flags & HIFN_FLAG_SRC_BUSY)) {
			dev->flags &= ~HIFN_FLAG_SRC_BUSY;
			r |= HIFN_DMACSR_S_CTRL_DIS;
		}
		if (dma->dstu == 0 && (dev->flags & HIFN_FLAG_DST_BUSY)) {
			dev->flags &= ~HIFN_FLAG_DST_BUSY;
			r |= HIFN_DMACSR_D_CTRL_DIS;
		}
		if (dma->resu == 0 && (dev->flags & HIFN_FLAG_RES_BUSY)) {
			dev->flags &= ~HIFN_FLAG_RES_BUSY;
			r |= HIFN_DMACSR_R_CTRL_DIS;
		}
		if (r)
			hifn_write_1(dev, HIFN_1_DMA_CSR, r);
	} else
		dev->active--;

	if (dev->prev_success == dev->success && dev->started)
		reset = 1;
	dev->prev_success = dev->success;
	spin_unlock_irqrestore(&dev->lock, flags);

	if (reset) {
		dprintk("%s: r: %08x, active: %d, started: %d, "
				"success: %lu: reset: %d.\n",
			dev->name, r, dev->active, dev->started,
			dev->success, reset);

		if (++dev->reset >= 5) {
			dprintk("%s: really hard reset.\n", dev->name);
			hifn_reset_dma(dev, 1);
			hifn_stop_device(dev);
			hifn_start_device(dev);
			dev->reset = 0;
		}

		spin_lock_irqsave(&dev->lock, flags);
		hifn_check_for_completion(dev, -EBUSY);
		hifn_clear_rings(dev);
		dev->started = 0;
		spin_unlock_irqrestore(&dev->lock, flags);
	}

	schedule_delayed_work(&dev->work, HZ);
}

static irqreturn_t hifn_interrupt(int irq, void *data)
{
	struct hifn_device *dev = (struct hifn_device *)data;
	struct hifn_dma *dma = (struct hifn_dma *)dev->desc_virt;
	u32 dmacsr, restart;

	dmacsr = hifn_read_1(dev, HIFN_1_DMA_CSR);

	dprintk("%s: 1 dmacsr: %08x, dmareg: %08x, res: %08x [%d], "
			"i: %d.%d.%d.%d, u: %d.%d.%d.%d.\n",
		dev->name, dmacsr, dev->dmareg, dmacsr & dev->dmareg, dma->cmdi,
		dma->cmdu, dma->srcu, dma->dstu, dma->resu,
		dma->cmdi, dma->srci, dma->dsti, dma->resi);

	if ((dmacsr & dev->dmareg) == 0)
		return IRQ_NONE;

	hifn_write_1(dev, HIFN_1_DMA_CSR, dmacsr & dev->dmareg);

	if (dmacsr & HIFN_DMACSR_ENGINE)
		hifn_write_0(dev, HIFN_0_PUISR, hifn_read_0(dev, HIFN_0_PUISR));
	if (dmacsr & HIFN_DMACSR_PUBDONE)
		hifn_write_1(dev, HIFN_1_PUB_STATUS,
			hifn_read_1(dev, HIFN_1_PUB_STATUS) | HIFN_PUBSTS_DONE);

	restart = dmacsr & (HIFN_DMACSR_R_OVER | HIFN_DMACSR_D_OVER);
	if (restart) {
		u32 puisr = hifn_read_0(dev, HIFN_0_PUISR);

		if (printk_ratelimit())
			printk("%s: overflow: r: %d, d: %d, puisr: %08x, d: %u.\n",
				dev->name, !!(dmacsr & HIFN_DMACSR_R_OVER),
				!!(dmacsr & HIFN_DMACSR_D_OVER),
				puisr, !!(puisr & HIFN_PUISR_DSTOVER));
		if (!!(puisr & HIFN_PUISR_DSTOVER))
			hifn_write_0(dev, HIFN_0_PUISR, HIFN_PUISR_DSTOVER);
		hifn_write_1(dev, HIFN_1_DMA_CSR, dmacsr & (HIFN_DMACSR_R_OVER |
					HIFN_DMACSR_D_OVER));
	}

	restart = dmacsr & (HIFN_DMACSR_C_ABORT | HIFN_DMACSR_S_ABORT |
			HIFN_DMACSR_D_ABORT | HIFN_DMACSR_R_ABORT);
	if (restart) {
		if (printk_ratelimit())
			printk("%s: abort: c: %d, s: %d, d: %d, r: %d.\n",
				dev->name, !!(dmacsr & HIFN_DMACSR_C_ABORT),
				!!(dmacsr & HIFN_DMACSR_S_ABORT),
				!!(dmacsr & HIFN_DMACSR_D_ABORT),
				!!(dmacsr & HIFN_DMACSR_R_ABORT));
		hifn_reset_dma(dev, 1);
		hifn_init_dma(dev);
		hifn_init_registers(dev);
	}

	if ((dmacsr & HIFN_DMACSR_C_WAIT) && (dma->cmdu == 0)) {
		dprintk("%s: wait on command.\n", dev->name);
		dev->dmareg &= ~(HIFN_DMAIER_C_WAIT);
		hifn_write_1(dev, HIFN_1_DMA_IER, dev->dmareg);
	}

	tasklet_schedule(&dev->tasklet);
	hifn_clear_rings(dev);

	return IRQ_HANDLED;
}

static void hifn_flush(struct hifn_device *dev)
{
	unsigned long flags;
	struct crypto_async_request *async_req;
	struct hifn_context *ctx;
	struct ablkcipher_request *req;
	struct hifn_dma *dma = (struct hifn_dma *)dev->desc_virt;
	int i;

	spin_lock_irqsave(&dev->lock, flags);
	for (i=0; i<HIFN_D_RES_RSIZE; ++i) {
		struct hifn_desc *d = &dma->resr[i];

		if (dev->sa[i]) {
			hifn_process_ready(dev->sa[i],
				(d->l & __cpu_to_le32(HIFN_D_VALID))?-ENODEV:0);
		}
	}

	while ((async_req = crypto_dequeue_request(&dev->queue))) {
		ctx = crypto_tfm_ctx(async_req->tfm);
		req = container_of(async_req, struct ablkcipher_request, base);

		hifn_process_ready(req, -ENODEV);
	}
	spin_unlock_irqrestore(&dev->lock, flags);
}

static int hifn_setkey(struct crypto_ablkcipher *cipher, const u8 *key,
		unsigned int len)
{
	struct crypto_tfm *tfm = crypto_ablkcipher_tfm(cipher);
	struct hifn_context *ctx = crypto_tfm_ctx(tfm);
	struct hifn_device *dev = ctx->dev;

	if (len > HIFN_MAX_CRYPT_KEY_LENGTH) {
		crypto_ablkcipher_set_flags(cipher, CRYPTO_TFM_RES_BAD_KEY_LEN);
		return -1;
	}

	if (len == HIFN_DES_KEY_LENGTH) {
		u32 tmp[DES_EXPKEY_WORDS];
		int ret = des_ekey(tmp, key);
		
		if (unlikely(ret == 0) && (tfm->crt_flags & CRYPTO_TFM_REQ_WEAK_KEY)) {
			tfm->crt_flags |= CRYPTO_TFM_RES_WEAK_KEY;
			return -EINVAL;
		}
	}

	dev->flags &= ~HIFN_FLAG_OLD_KEY;

	memcpy(ctx->key, key, len);
	ctx->keysize = len;

	return 0;
}

static int hifn_handle_req(struct ablkcipher_request *req)
{
	struct hifn_context *ctx = crypto_tfm_ctx(req->base.tfm);
	struct hifn_device *dev = ctx->dev;
	int err = -EAGAIN;

	if (dev->started + DIV_ROUND_UP(req->nbytes, PAGE_SIZE) <= HIFN_QUEUE_LENGTH)
		err = hifn_setup_session(req);

	if (err == -EAGAIN) {
		unsigned long flags;

		spin_lock_irqsave(&dev->lock, flags);
		err = ablkcipher_enqueue_request(&dev->queue, req);
		spin_unlock_irqrestore(&dev->lock, flags);
	}

	return err;
}

static int hifn_setup_crypto_req(struct ablkcipher_request *req, u8 op,
		u8 type, u8 mode)
{
	struct hifn_context *ctx = crypto_tfm_ctx(req->base.tfm);
	unsigned ivsize;

	ivsize = crypto_ablkcipher_ivsize(crypto_ablkcipher_reqtfm(req));

	if (req->info && mode != ACRYPTO_MODE_ECB) {
		if (type == ACRYPTO_TYPE_AES_128)
			ivsize = HIFN_AES_IV_LENGTH;
		else if (type == ACRYPTO_TYPE_DES)
			ivsize = HIFN_DES_KEY_LENGTH;
		else if (type == ACRYPTO_TYPE_3DES)
			ivsize = HIFN_3DES_KEY_LENGTH;
	}

	if (ctx->keysize != 16 && type == ACRYPTO_TYPE_AES_128) {
		if (ctx->keysize == 24)
			type = ACRYPTO_TYPE_AES_192;
		else if (ctx->keysize == 32)
			type = ACRYPTO_TYPE_AES_256;
	}

	ctx->op = op;
	ctx->mode = mode;
	ctx->type = type;
	ctx->iv = req->info;
	ctx->ivsize = ivsize;

	/*
	 * HEAVY TODO: needs to kick Herbert XU to write documentation.
	 * HEAVY TODO: needs to kick Herbert XU to write documentation.
	 * HEAVY TODO: needs to kick Herbert XU to write documentation.
	 */

	return hifn_handle_req(req);
}

static int hifn_process_queue(struct hifn_device *dev)
{
	struct crypto_async_request *async_req;
	struct hifn_context *ctx;
	struct ablkcipher_request *req;
	unsigned long flags;
	int err = 0;

	while (dev->started < HIFN_QUEUE_LENGTH) {
		spin_lock_irqsave(&dev->lock, flags);
		async_req = crypto_dequeue_request(&dev->queue);
		spin_unlock_irqrestore(&dev->lock, flags);

		if (!async_req)
			break;

		ctx = crypto_tfm_ctx(async_req->tfm);
		req = container_of(async_req, struct ablkcipher_request, base);

		err = hifn_handle_req(req);
		if (err)
			break;
	}

	return err;
}

static int hifn_setup_crypto(struct ablkcipher_request *req, u8 op,
		u8 type, u8 mode)
{
	int err;
	struct hifn_context *ctx = crypto_tfm_ctx(req->base.tfm);
	struct hifn_device *dev = ctx->dev;

	err = hifn_setup_crypto_req(req, op, type, mode);
	if (err)
		return err;

	if (dev->started < HIFN_QUEUE_LENGTH &&	dev->queue.qlen)
		err = hifn_process_queue(dev);

	return err;
}

/*
 * AES ecryption functions.
 */
static inline int hifn_encrypt_aes_ecb(struct ablkcipher_request *req)
{
	return hifn_setup_crypto(req, ACRYPTO_OP_ENCRYPT,
			ACRYPTO_TYPE_AES_128, ACRYPTO_MODE_ECB);
}
static inline int hifn_encrypt_aes_cbc(struct ablkcipher_request *req)
{
	return hifn_setup_crypto(req, ACRYPTO_OP_ENCRYPT,
			ACRYPTO_TYPE_AES_128, ACRYPTO_MODE_CBC);
}
static inline int hifn_encrypt_aes_cfb(struct ablkcipher_request *req)
{
	return hifn_setup_crypto(req, ACRYPTO_OP_ENCRYPT,
			ACRYPTO_TYPE_AES_128, ACRYPTO_MODE_CFB);
}
static inline int hifn_encrypt_aes_ofb(struct ablkcipher_request *req)
{
	return hifn_setup_crypto(req, ACRYPTO_OP_ENCRYPT,
			ACRYPTO_TYPE_AES_128, ACRYPTO_MODE_OFB);
}

/*
 * AES decryption functions.
 */
static inline int hifn_decrypt_aes_ecb(struct ablkcipher_request *req)
{
	return hifn_setup_crypto(req, ACRYPTO_OP_DECRYPT,
			ACRYPTO_TYPE_AES_128, ACRYPTO_MODE_ECB);
}
static inline int hifn_decrypt_aes_cbc(struct ablkcipher_request *req)
{
	return hifn_setup_crypto(req, ACRYPTO_OP_DECRYPT,
			ACRYPTO_TYPE_AES_128, ACRYPTO_MODE_CBC);
}
static inline int hifn_decrypt_aes_cfb(struct ablkcipher_request *req)
{
	return hifn_setup_crypto(req, ACRYPTO_OP_DECRYPT,
			ACRYPTO_TYPE_AES_128, ACRYPTO_MODE_CFB);
}
static inline int hifn_decrypt_aes_ofb(struct ablkcipher_request *req)
{
	return hifn_setup_crypto(req, ACRYPTO_OP_DECRYPT,
			ACRYPTO_TYPE_AES_128, ACRYPTO_MODE_OFB);
}

/*
 * DES ecryption functions.
 */
static inline int hifn_encrypt_des_ecb(struct ablkcipher_request *req)
{
	return hifn_setup_crypto(req, ACRYPTO_OP_ENCRYPT,
			ACRYPTO_TYPE_DES, ACRYPTO_MODE_ECB);
}
static inline int hifn_encrypt_des_cbc(struct ablkcipher_request *req)
{
	return hifn_setup_crypto(req, ACRYPTO_OP_ENCRYPT,
			ACRYPTO_TYPE_DES, ACRYPTO_MODE_CBC);
}
static inline int hifn_encrypt_des_cfb(struct ablkcipher_request *req)
{
	return hifn_setup_crypto(req, ACRYPTO_OP_ENCRYPT,
			ACRYPTO_TYPE_DES, ACRYPTO_MODE_CFB);
}
static inline int hifn_encrypt_des_ofb(struct ablkcipher_request *req)
{
	return hifn_setup_crypto(req, ACRYPTO_OP_ENCRYPT,
			ACRYPTO_TYPE_DES, ACRYPTO_MODE_OFB);
}

/*
 * DES decryption functions.
 */
static inline int hifn_decrypt_des_ecb(struct ablkcipher_request *req)
{
	return hifn_setup_crypto(req, ACRYPTO_OP_DECRYPT,
			ACRYPTO_TYPE_DES, ACRYPTO_MODE_ECB);
}
static inline int hifn_decrypt_des_cbc(struct ablkcipher_request *req)
{
	return hifn_setup_crypto(req, ACRYPTO_OP_DECRYPT,
			ACRYPTO_TYPE_DES, ACRYPTO_MODE_CBC);
}
static inline int hifn_decrypt_des_cfb(struct ablkcipher_request *req)
{
	return hifn_setup_crypto(req, ACRYPTO_OP_DECRYPT,
			ACRYPTO_TYPE_DES, ACRYPTO_MODE_CFB);
}
static inline int hifn_decrypt_des_ofb(struct ablkcipher_request *req)
{
	return hifn_setup_crypto(req, ACRYPTO_OP_DECRYPT,
			ACRYPTO_TYPE_DES, ACRYPTO_MODE_OFB);
}

/*
 * 3DES ecryption functions.
 */
static inline int hifn_encrypt_3des_ecb(struct ablkcipher_request *req)
{
	return hifn_setup_crypto(req, ACRYPTO_OP_ENCRYPT,
			ACRYPTO_TYPE_3DES, ACRYPTO_MODE_ECB);
}
static inline int hifn_encrypt_3des_cbc(struct ablkcipher_request *req)
{
	return hifn_setup_crypto(req, ACRYPTO_OP_ENCRYPT,
			ACRYPTO_TYPE_3DES, ACRYPTO_MODE_CBC);
}
static inline int hifn_encrypt_3des_cfb(struct ablkcipher_request *req)
{
	return hifn_setup_crypto(req, ACRYPTO_OP_ENCRYPT,
			ACRYPTO_TYPE_3DES, ACRYPTO_MODE_CFB);
}
static inline int hifn_encrypt_3des_ofb(struct ablkcipher_request *req)
{
	return hifn_setup_crypto(req, ACRYPTO_OP_ENCRYPT,
			ACRYPTO_TYPE_3DES, ACRYPTO_MODE_OFB);
}

/*
 * 3DES decryption functions.
 */
static inline int hifn_decrypt_3des_ecb(struct ablkcipher_request *req)
{
	return hifn_setup_crypto(req, ACRYPTO_OP_DECRYPT,
			ACRYPTO_TYPE_3DES, ACRYPTO_MODE_ECB);
}
static inline int hifn_decrypt_3des_cbc(struct ablkcipher_request *req)
{
	return hifn_setup_crypto(req, ACRYPTO_OP_DECRYPT,
			ACRYPTO_TYPE_3DES, ACRYPTO_MODE_CBC);
}
static inline int hifn_decrypt_3des_cfb(struct ablkcipher_request *req)
{
	return hifn_setup_crypto(req, ACRYPTO_OP_DECRYPT,
			ACRYPTO_TYPE_3DES, ACRYPTO_MODE_CFB);
}
static inline int hifn_decrypt_3des_ofb(struct ablkcipher_request *req)
{
	return hifn_setup_crypto(req, ACRYPTO_OP_DECRYPT,
			ACRYPTO_TYPE_3DES, ACRYPTO_MODE_OFB);
}

struct hifn_alg_template
{
	char name[CRYPTO_MAX_ALG_NAME];
	char drv_name[CRYPTO_MAX_ALG_NAME];
	unsigned int bsize;
	struct ablkcipher_alg ablkcipher;
};

static struct hifn_alg_template hifn_alg_templates[] = {
	/*
	 * 3DES ECB, CBC, CFB and OFB modes.
	 */
	{
		.name = "cfb(des3_ede)", .drv_name = "hifn-3des", .bsize = 8,
		.ablkcipher = {
			.min_keysize	=	HIFN_3DES_KEY_LENGTH,
			.max_keysize	=	HIFN_3DES_KEY_LENGTH,
			.setkey		=	hifn_setkey,
			.encrypt	=	hifn_encrypt_3des_cfb,
			.decrypt	=	hifn_decrypt_3des_cfb,
		},
	},
	{
		.name = "ofb(des3_ede)", .drv_name = "hifn-3des", .bsize = 8,
		.ablkcipher = {
			.min_keysize	=	HIFN_3DES_KEY_LENGTH,
			.max_keysize	=	HIFN_3DES_KEY_LENGTH,
			.setkey		=	hifn_setkey,
			.encrypt	=	hifn_encrypt_3des_ofb,
			.decrypt	=	hifn_decrypt_3des_ofb,
		},
	},
	{
		.name = "cbc(des3_ede)", .drv_name = "hifn-3des", .bsize = 8,
		.ablkcipher = {
			.min_keysize	=	HIFN_3DES_KEY_LENGTH,
			.max_keysize	=	HIFN_3DES_KEY_LENGTH,
			.setkey		=	hifn_setkey,
			.encrypt	=	hifn_encrypt_3des_cbc,
			.decrypt	=	hifn_decrypt_3des_cbc,
		},
	},
	{
		.name = "ecb(des3_ede)", .drv_name = "hifn-3des", .bsize = 8,
		.ablkcipher = {
			.min_keysize	=	HIFN_3DES_KEY_LENGTH,
			.max_keysize	=	HIFN_3DES_KEY_LENGTH,
			.setkey		=	hifn_setkey,
			.encrypt	=	hifn_encrypt_3des_ecb,
			.decrypt	=	hifn_decrypt_3des_ecb,
		},
	},

	/*
	 * DES ECB, CBC, CFB and OFB modes.
	 */
	{
		.name = "cfb(des)", .drv_name = "hifn-des", .bsize = 8,
		.ablkcipher = {
			.min_keysize	=	HIFN_DES_KEY_LENGTH,
			.max_keysize	=	HIFN_DES_KEY_LENGTH,
			.setkey		=	hifn_setkey,
			.encrypt	=	hifn_encrypt_des_cfb,
			.decrypt	=	hifn_decrypt_des_cfb,
		},
	},
	{
		.name = "ofb(des)", .drv_name = "hifn-des", .bsize = 8,
		.ablkcipher = {
			.min_keysize	=	HIFN_DES_KEY_LENGTH,
			.max_keysize	=	HIFN_DES_KEY_LENGTH,
			.setkey		=	hifn_setkey,
			.encrypt	=	hifn_encrypt_des_ofb,
			.decrypt	=	hifn_decrypt_des_ofb,
		},
	},
	{
		.name = "cbc(des)", .drv_name = "hifn-des", .bsize = 8,
		.ablkcipher = {
			.min_keysize	=	HIFN_DES_KEY_LENGTH,
			.max_keysize	=	HIFN_DES_KEY_LENGTH,
			.setkey		=	hifn_setkey,
			.encrypt	=	hifn_encrypt_des_cbc,
			.decrypt	=	hifn_decrypt_des_cbc,
		},
	},
	{
		.name = "ecb(des)", .drv_name = "hifn-des", .bsize = 8,
		.ablkcipher = {
			.min_keysize	=	HIFN_DES_KEY_LENGTH,
			.max_keysize	=	HIFN_DES_KEY_LENGTH,
			.setkey		=	hifn_setkey,
			.encrypt	=	hifn_encrypt_des_ecb,
			.decrypt	=	hifn_decrypt_des_ecb,
		},
	},

	/*
	 * AES ECB, CBC, CFB and OFB modes.
	 */
	{
		.name = "ecb(aes)", .drv_name = "hifn-aes", .bsize = 16,
		.ablkcipher = {
			.min_keysize	=	AES_MIN_KEY_SIZE,
			.max_keysize	=	AES_MAX_KEY_SIZE,
			.setkey		=	hifn_setkey,
			.encrypt	=	hifn_encrypt_aes_ecb,
			.decrypt	=	hifn_decrypt_aes_ecb,
		},
	},
	{
		.name = "cbc(aes)", .drv_name = "hifn-aes", .bsize = 16,
		.ablkcipher = {
			.min_keysize	=	AES_MIN_KEY_SIZE,
			.max_keysize	=	AES_MAX_KEY_SIZE,
			.setkey		=	hifn_setkey,
			.encrypt	=	hifn_encrypt_aes_cbc,
			.decrypt	=	hifn_decrypt_aes_cbc,
		},
	},
	{
		.name = "cfb(aes)", .drv_name = "hifn-aes", .bsize = 16,
		.ablkcipher = {
			.min_keysize	=	AES_MIN_KEY_SIZE,
			.max_keysize	=	AES_MAX_KEY_SIZE,
			.setkey		=	hifn_setkey,
			.encrypt	=	hifn_encrypt_aes_cfb,
			.decrypt	=	hifn_decrypt_aes_cfb,
		},
	},
	{
		.name = "ofb(aes)", .drv_name = "hifn-aes", .bsize = 16,
		.ablkcipher = {
			.min_keysize	=	AES_MIN_KEY_SIZE,
			.max_keysize	=	AES_MAX_KEY_SIZE,
			.setkey		=	hifn_setkey,
			.encrypt	=	hifn_encrypt_aes_ofb,
			.decrypt	=	hifn_decrypt_aes_ofb,
		},
	},
};

static int hifn_cra_init(struct crypto_tfm *tfm)
{
	struct crypto_alg *alg = tfm->__crt_alg;
	struct hifn_crypto_alg *ha = crypto_alg_to_hifn(alg);
	struct hifn_context *ctx = crypto_tfm_ctx(tfm);

	ctx->dev = ha->dev;

	return 0;
}

static int hifn_alg_alloc(struct hifn_device *dev, struct hifn_alg_template *t)
{
	struct hifn_crypto_alg *alg;
	int err;

	alg = kzalloc(sizeof(struct hifn_crypto_alg), GFP_KERNEL);
	if (!alg)
		return -ENOMEM;

	snprintf(alg->alg.cra_name, CRYPTO_MAX_ALG_NAME, "%s", t->name);
	snprintf(alg->alg.cra_driver_name, CRYPTO_MAX_ALG_NAME, "%s", t->drv_name);

	alg->alg.cra_priority = 300;
	alg->alg.cra_flags = CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC;
	alg->alg.cra_blocksize = t->bsize;
	alg->alg.cra_ctxsize = sizeof(struct hifn_context);
	alg->alg.cra_alignmask = 15;
	if (t->bsize == 8)
		alg->alg.cra_alignmask = 3;
	alg->alg.cra_type = &crypto_ablkcipher_type;
	alg->alg.cra_module = THIS_MODULE;
	alg->alg.cra_u.ablkcipher = t->ablkcipher;
	alg->alg.cra_init = hifn_cra_init;

	alg->dev = dev;

	list_add_tail(&alg->entry, &dev->alg_list);

	err = crypto_register_alg(&alg->alg);
	if (err) {
		list_del(&alg->entry);
		kfree(alg);
	}

	return err;
}

static void hifn_unregister_alg(struct hifn_device *dev)
{
	struct hifn_crypto_alg *a, *n;

	list_for_each_entry_safe(a, n, &dev->alg_list, entry) {
		list_del(&a->entry);
		crypto_unregister_alg(&a->alg);
		kfree(a);
	}
}

static int hifn_register_alg(struct hifn_device *dev)
{
	int i, err;

	for (i=0; i<ARRAY_SIZE(hifn_alg_templates); ++i) {
		err = hifn_alg_alloc(dev, &hifn_alg_templates[i]);
		if (err)
			goto err_out_exit;
	}

	return 0;

err_out_exit:
	hifn_unregister_alg(dev);
	return err;
}

static void hifn_tasklet_callback(unsigned long data)
{
	struct hifn_device *dev = (struct hifn_device *)data;

	/*
	 * This is ok to call this without lock being held,
	 * althogh it modifies some parameters used in parallel,
	 * (like dev->success), but they are used in process
	 * context or update is atomic (like setting dev->sa[i] to NULL).
	 */
	hifn_check_for_completion(dev, 0);
}

static int hifn_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	int err, i;
	struct hifn_device *dev;
	char name[8];

	err = pci_enable_device(pdev);
	if (err)
		return err;
	pci_set_master(pdev);

	err = pci_set_dma_mask(pdev, DMA_32BIT_MASK);
	if (err)
		goto err_out_disable_pci_device;

	snprintf(name, sizeof(name), "hifn%d",
			atomic_inc_return(&hifn_dev_number)-1);

	err = pci_request_regions(pdev, name);
	if (err)
		goto err_out_disable_pci_device;

	if (pci_resource_len(pdev, 0) < HIFN_BAR0_SIZE ||
	    pci_resource_len(pdev, 1) < HIFN_BAR1_SIZE ||
	    pci_resource_len(pdev, 2) < HIFN_BAR2_SIZE) {
		dprintk("%s: Broken hardware - I/O regions are too small.\n",
				pci_name(pdev));
		err = -ENODEV;
		goto err_out_free_regions;
	}

	dev = kzalloc(sizeof(struct hifn_device) + sizeof(struct crypto_alg),
			GFP_KERNEL);
	if (!dev) {
		err = -ENOMEM;
		goto err_out_free_regions;
	}

	INIT_LIST_HEAD(&dev->alg_list);

	snprintf(dev->name, sizeof(dev->name), "%s", name);
	spin_lock_init(&dev->lock);

	for (i=0; i<3; ++i) {
		unsigned long addr, size;

		addr = pci_resource_start(pdev, i);
		size = pci_resource_len(pdev, i);

		dev->bar[i] = ioremap_nocache(addr, size);
		if (!dev->bar[i])
			goto err_out_unmap_bars;
	}

	dev->result_mem = __get_free_pages(GFP_KERNEL, HIFN_MAX_RESULT_ORDER);
	if (!dev->result_mem) {
		dprintk("Failed to allocate %d pages for result_mem.\n",
				HIFN_MAX_RESULT_ORDER);
		goto err_out_unmap_bars;
	}
	memset((void *)dev->result_mem, 0, PAGE_SIZE*(1<<HIFN_MAX_RESULT_ORDER));

	dev->dst = pci_map_single(pdev, (void *)dev->result_mem,
			PAGE_SIZE << HIFN_MAX_RESULT_ORDER, PCI_DMA_FROMDEVICE);

	dev->desc_virt = pci_alloc_consistent(pdev, sizeof(struct hifn_dma),
			&dev->desc_dma);
	if (!dev->desc_virt) {
		dprintk("Failed to allocate descriptor rings.\n");
		goto err_out_free_result_pages;
	}
	memset(dev->desc_virt, 0, sizeof(struct hifn_dma));

	dev->pdev = pdev;
	dev->irq = pdev->irq;

	for (i=0; i<HIFN_D_RES_RSIZE; ++i)
		dev->sa[i] = NULL;

	pci_set_drvdata(pdev, dev);

	tasklet_init(&dev->tasklet, hifn_tasklet_callback, (unsigned long)dev);

	crypto_init_queue(&dev->queue, 1);

	err = request_irq(dev->irq, hifn_interrupt, IRQF_SHARED, dev->name, dev);
	if (err) {
		dprintk("Failed to request IRQ%d: err: %d.\n", dev->irq, err);
		dev->irq = 0;
		goto err_out_free_desc;
	}

	err = hifn_start_device(dev);
	if (err)
		goto err_out_free_irq;

	err = hifn_test(dev, 1, 0);
	if (err)
		goto err_out_stop_device;

	err = hifn_register_rng(dev);
	if (err)
		goto err_out_stop_device;

	err = hifn_register_alg(dev);
	if (err)
		goto err_out_unregister_rng;

	INIT_DELAYED_WORK(&dev->work, hifn_work);
	schedule_delayed_work(&dev->work, HZ);

	dprintk("HIFN crypto accelerator card at %s has been "
			"successfully registered as %s.\n",
			pci_name(pdev), dev->name);

	return 0;

err_out_unregister_rng:
	hifn_unregister_rng(dev);
err_out_stop_device:
	hifn_reset_dma(dev, 1);
	hifn_stop_device(dev);
err_out_free_irq:
	free_irq(dev->irq, dev->name);
	tasklet_kill(&dev->tasklet);
err_out_free_desc:
	pci_free_consistent(pdev, sizeof(struct hifn_dma),
			dev->desc_virt, dev->desc_dma);

err_out_free_result_pages:
	pci_unmap_single(pdev, dev->dst, PAGE_SIZE << HIFN_MAX_RESULT_ORDER,
			PCI_DMA_FROMDEVICE);
	free_pages(dev->result_mem, HIFN_MAX_RESULT_ORDER);

err_out_unmap_bars:
	for (i=0; i<3; ++i)
		if (dev->bar[i])
			iounmap(dev->bar[i]);

err_out_free_regions:
	pci_release_regions(pdev);

err_out_disable_pci_device:
	pci_disable_device(pdev);

	return err;
}

static void hifn_remove(struct pci_dev *pdev)
{
	int i;
	struct hifn_device *dev;

	dev = pci_get_drvdata(pdev);

	if (dev) {
		cancel_delayed_work(&dev->work);
		flush_scheduled_work();

		hifn_unregister_rng(dev);
		hifn_unregister_alg(dev);
		hifn_reset_dma(dev, 1);
		hifn_stop_device(dev);

		free_irq(dev->irq, dev->name);
		tasklet_kill(&dev->tasklet);

		hifn_flush(dev);

		pci_free_consistent(pdev, sizeof(struct hifn_dma),
				dev->desc_virt, dev->desc_dma);
		pci_unmap_single(pdev, dev->dst,
				PAGE_SIZE << HIFN_MAX_RESULT_ORDER,
				PCI_DMA_FROMDEVICE);
		free_pages(dev->result_mem, HIFN_MAX_RESULT_ORDER);
		for (i=0; i<3; ++i)
			if (dev->bar[i])
				iounmap(dev->bar[i]);

		kfree(dev);
	}

	pci_release_regions(pdev);
	pci_disable_device(pdev);
}

static struct pci_device_id hifn_pci_tbl[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_HIFN, PCI_DEVICE_ID_HIFN_7955) },
	{ PCI_DEVICE(PCI_VENDOR_ID_HIFN, PCI_DEVICE_ID_HIFN_7956) },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, hifn_pci_tbl);

static struct pci_driver hifn_pci_driver = {
	.name     = "hifn795x",
	.id_table = hifn_pci_tbl,
	.probe    = hifn_probe,
	.remove   = __devexit_p(hifn_remove),
};

static int __devinit hifn_init(void)
{
	unsigned int freq;
	int err;

	if (strncmp(hifn_pll_ref, "ext", 3) &&
	    strncmp(hifn_pll_ref, "pci", 3)) {
		printk(KERN_ERR "hifn795x: invalid hifn_pll_ref clock, "
				"must be pci or ext");
		return -EINVAL;
	}

	/*
	 * For the 7955/7956 the reference clock frequency must be in the
	 * range of 20MHz-100MHz. For the 7954 the upper bound is 66.67MHz,
	 * but this chip is currently not supported.
	 */
	if (hifn_pll_ref[3] != '\0') {
		freq = simple_strtoul(hifn_pll_ref + 3, NULL, 10);
		if (freq < 20 || freq > 100) {
			printk(KERN_ERR "hifn795x: invalid hifn_pll_ref "
					"frequency, must be in the range "
					"of 20-100");
			return -EINVAL;
		}
	}

	err = pci_register_driver(&hifn_pci_driver);
	if (err < 0) {
		dprintk("Failed to register PCI driver for %s device.\n",
				hifn_pci_driver.name);
		return -ENODEV;
	}

	printk(KERN_INFO "Driver for HIFN 795x crypto accelerator chip "
			"has been successfully registered.\n");

	return 0;
}

static void __devexit hifn_fini(void)
{
	pci_unregister_driver(&hifn_pci_driver);

	printk(KERN_INFO "Driver for HIFN 795x crypto accelerator chip "
			"has been successfully unregistered.\n");
}

module_init(hifn_init);
module_exit(hifn_fini);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Evgeniy Polyakov <johnpol@2ka.mipt.ru>");
MODULE_DESCRIPTION("Driver for HIFN 795x crypto accelerator chip.");
