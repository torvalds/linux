/*
 * Linux network driver for Brocade Converged Network Adapter.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License (GPL) Version 2 as
 * published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */
/*
 * Copyright (c) 2005-2010 Brocade Communications Systems, Inc.
 * All rights reserved
 * www.brocade.com
 *
 * File for interrupt macros and functions
 */

#ifndef __BNA_HW_H__
#define __BNA_HW_H__

#include "bfi_ctreg.h"

/**
 *
 * SW imposed limits
 *
 */

#ifndef BNA_BIOS_BUILD

#define BFI_MAX_TXQ			64
#define BFI_MAX_RXQ			64
#define	BFI_MAX_RXF			64
#define BFI_MAX_IB			128
#define	BFI_MAX_RIT_SIZE		256
#define	BFI_RSS_RIT_SIZE		64
#define	BFI_NONRSS_RIT_SIZE		1
#define BFI_MAX_UCMAC			256
#define BFI_MAX_MCMAC			512
#define BFI_IBIDX_SIZE			4
#define BFI_MAX_VLAN			4095

/**
 * There are 2 free IB index pools:
 *	pool1: 120 segments of 1 index each
 *	pool8: 1 segment of 8 indexes
 */
#define BFI_IBIDX_POOL1_SIZE		116
#define	BFI_IBIDX_POOL1_ENTRY_SIZE	1
#define BFI_IBIDX_POOL2_SIZE		2
#define	BFI_IBIDX_POOL2_ENTRY_SIZE	2
#define	BFI_IBIDX_POOL8_SIZE		1
#define	BFI_IBIDX_POOL8_ENTRY_SIZE	8
#define	BFI_IBIDX_TOTAL_POOLS		3
#define	BFI_IBIDX_TOTAL_SEGS		119 /* (POOL1 + POOL2 + POOL8)_SIZE */
#define	BFI_IBIDX_MAX_SEGSIZE		8
#define init_ibidx_pool(name)						\
static struct bna_ibidx_pool name[BFI_IBIDX_TOTAL_POOLS] =		\
{									\
	{ BFI_IBIDX_POOL1_SIZE, BFI_IBIDX_POOL1_ENTRY_SIZE },		\
	{ BFI_IBIDX_POOL2_SIZE, BFI_IBIDX_POOL2_ENTRY_SIZE },		\
	{ BFI_IBIDX_POOL8_SIZE, BFI_IBIDX_POOL8_ENTRY_SIZE }		\
}

/**
 * There are 2 free RIT segment pools:
 *	Pool1: 192 segments of 1 RIT entry each
 *	Pool2: 1 segment of 64 RIT entry
 */
#define BFI_RIT_SEG_POOL1_SIZE		192
#define BFI_RIT_SEG_POOL1_ENTRY_SIZE	1
#define BFI_RIT_SEG_POOLRSS_SIZE	1
#define BFI_RIT_SEG_POOLRSS_ENTRY_SIZE	64
#define BFI_RIT_SEG_TOTAL_POOLS		2
#define BFI_RIT_TOTAL_SEGS		193 /* POOL1_SIZE + POOLRSS_SIZE */
#define init_ritseg_pool(name)						\
static struct bna_ritseg_pool_cfg name[BFI_RIT_SEG_TOTAL_POOLS] =	\
{									\
	{ BFI_RIT_SEG_POOL1_SIZE, BFI_RIT_SEG_POOL1_ENTRY_SIZE },	\
	{ BFI_RIT_SEG_POOLRSS_SIZE, BFI_RIT_SEG_POOLRSS_ENTRY_SIZE }	\
}

#else /* BNA_BIOS_BUILD */

#define BFI_MAX_TXQ			1
#define BFI_MAX_RXQ			1
#define	BFI_MAX_RXF			1
#define BFI_MAX_IB			2
#define	BFI_MAX_RIT_SIZE		2
#define	BFI_RSS_RIT_SIZE		64
#define	BFI_NONRSS_RIT_SIZE		1
#define BFI_MAX_UCMAC			1
#define BFI_MAX_MCMAC			8
#define BFI_IBIDX_SIZE			4
#define BFI_MAX_VLAN			4095
/* There is one free pool: 2 segments of 1 index each */
#define BFI_IBIDX_POOL1_SIZE		2
#define	BFI_IBIDX_POOL1_ENTRY_SIZE	1
#define	BFI_IBIDX_TOTAL_POOLS		1
#define	BFI_IBIDX_TOTAL_SEGS		2 /* POOL1_SIZE */
#define	BFI_IBIDX_MAX_SEGSIZE		1
#define init_ibidx_pool(name)						\
static struct bna_ibidx_pool name[BFI_IBIDX_TOTAL_POOLS] =		\
{									\
	{ BFI_IBIDX_POOL1_SIZE, BFI_IBIDX_POOL1_ENTRY_SIZE }		\
}

#define BFI_RIT_SEG_POOL1_SIZE		1
#define BFI_RIT_SEG_POOL1_ENTRY_SIZE	1
#define BFI_RIT_SEG_TOTAL_POOLS		1
#define BFI_RIT_TOTAL_SEGS		1 /* POOL1_SIZE */
#define init_ritseg_pool(name)						\
static struct bna_ritseg_pool_cfg name[BFI_RIT_SEG_TOTAL_POOLS] =	\
{									\
	{ BFI_RIT_SEG_POOL1_SIZE, BFI_RIT_SEG_POOL1_ENTRY_SIZE }	\
}

#endif /* BNA_BIOS_BUILD */

#define BFI_RSS_HASH_KEY_LEN		10

#define BFI_COALESCING_TIMER_UNIT	5	/* 5us */
#define BFI_MAX_COALESCING_TIMEO	0xFF	/* in 5us units */
#define BFI_MAX_INTERPKT_COUNT		0xFF
#define BFI_MAX_INTERPKT_TIMEO		0xF	/* in 0.5us units */
#define BFI_TX_COALESCING_TIMEO		20	/* 20 * 5 = 100us */
#define BFI_TX_INTERPKT_COUNT		32
#define	BFI_RX_COALESCING_TIMEO		12	/* 12 * 5 = 60us */
#define	BFI_RX_INTERPKT_COUNT		6	/* Pkt Cnt = 6 */
#define	BFI_RX_INTERPKT_TIMEO		3	/* 3 * 0.5 = 1.5us */

#define BFI_TXQ_WI_SIZE			64	/* bytes */
#define BFI_RXQ_WI_SIZE			8	/* bytes */
#define BFI_CQ_WI_SIZE			16	/* bytes */
#define BFI_TX_MAX_WRR_QUOTA		0xFFF

#define BFI_TX_MAX_VECTORS_PER_WI	4
#define BFI_TX_MAX_VECTORS_PER_PKT	0xFF
#define BFI_TX_MAX_DATA_PER_VECTOR	0xFFFF
#define BFI_TX_MAX_DATA_PER_PKT		0xFFFFFF

/* Small Q buffer size */
#define BFI_SMALL_RXBUF_SIZE		128

/* Defined separately since BFA_FLASH_DMA_BUF_SZ is in bfa_flash.c */
#define BFI_FLASH_DMA_BUF_SZ		0x010000 /* 64K DMA */
#define BFI_HW_STATS_SIZE		0x4000 /* 16K DMA */

/**
 *
 * HW register offsets, macros
 *
 */

/* DMA Block Register Host Window Start Address */
#define DMA_BLK_REG_ADDR		0x00013000

/* DMA Block Internal Registers */
#define DMA_CTRL_REG0			(DMA_BLK_REG_ADDR + 0x000)
#define DMA_CTRL_REG1			(DMA_BLK_REG_ADDR + 0x004)
#define DMA_ERR_INT_STATUS		(DMA_BLK_REG_ADDR + 0x008)
#define DMA_ERR_INT_ENABLE		(DMA_BLK_REG_ADDR + 0x00c)
#define DMA_ERR_INT_STATUS_SET		(DMA_BLK_REG_ADDR + 0x010)

/* APP Block Register Address Offset from BAR0 */
#define APP_BLK_REG_ADDR		0x00014000

/* Host Function Interrupt Mask Registers */
#define HOSTFN0_INT_MASK		(APP_BLK_REG_ADDR + 0x004)
#define HOSTFN1_INT_MASK		(APP_BLK_REG_ADDR + 0x104)
#define HOSTFN2_INT_MASK		(APP_BLK_REG_ADDR + 0x304)
#define HOSTFN3_INT_MASK		(APP_BLK_REG_ADDR + 0x404)

/**
 * Host Function PCIe Error Registers
 * Duplicates "Correctable" & "Uncorrectable"
 * registers in PCIe Config space.
 */
#define FN0_PCIE_ERR_REG		(APP_BLK_REG_ADDR + 0x014)
#define FN1_PCIE_ERR_REG		(APP_BLK_REG_ADDR + 0x114)
#define FN2_PCIE_ERR_REG		(APP_BLK_REG_ADDR + 0x314)
#define FN3_PCIE_ERR_REG		(APP_BLK_REG_ADDR + 0x414)

/* Host Function Error Type Status Registers */
#define FN0_ERR_TYPE_STATUS_REG		(APP_BLK_REG_ADDR + 0x018)
#define FN1_ERR_TYPE_STATUS_REG		(APP_BLK_REG_ADDR + 0x118)
#define FN2_ERR_TYPE_STATUS_REG		(APP_BLK_REG_ADDR + 0x318)
#define FN3_ERR_TYPE_STATUS_REG		(APP_BLK_REG_ADDR + 0x418)

/* Host Function Error Type Mask Registers */
#define FN0_ERR_TYPE_MSK_STATUS_REG	(APP_BLK_REG_ADDR + 0x01c)
#define FN1_ERR_TYPE_MSK_STATUS_REG	(APP_BLK_REG_ADDR + 0x11c)
#define FN2_ERR_TYPE_MSK_STATUS_REG	(APP_BLK_REG_ADDR + 0x31c)
#define FN3_ERR_TYPE_MSK_STATUS_REG	(APP_BLK_REG_ADDR + 0x41c)

/* Catapult Host Semaphore Status Registers (App block) */
#define HOST_SEM_STS0_REG		(APP_BLK_REG_ADDR + 0x630)
#define HOST_SEM_STS1_REG		(APP_BLK_REG_ADDR + 0x634)
#define HOST_SEM_STS2_REG		(APP_BLK_REG_ADDR + 0x638)
#define HOST_SEM_STS3_REG		(APP_BLK_REG_ADDR + 0x63c)
#define HOST_SEM_STS4_REG		(APP_BLK_REG_ADDR + 0x640)
#define HOST_SEM_STS5_REG		(APP_BLK_REG_ADDR + 0x644)
#define HOST_SEM_STS6_REG		(APP_BLK_REG_ADDR + 0x648)
#define HOST_SEM_STS7_REG		(APP_BLK_REG_ADDR + 0x64c)

/* PCIe Misc Register */
#define PCIE_MISC_REG			(APP_BLK_REG_ADDR + 0x200)

/* Temp Sensor Control Registers */
#define TEMPSENSE_CNTL_REG		(APP_BLK_REG_ADDR + 0x250)
#define TEMPSENSE_STAT_REG		(APP_BLK_REG_ADDR + 0x254)

/* APP Block local error registers */
#define APP_LOCAL_ERR_STAT		(APP_BLK_REG_ADDR + 0x258)
#define APP_LOCAL_ERR_MSK		(APP_BLK_REG_ADDR + 0x25c)

/* PCIe Link Error registers */
#define PCIE_LNK_ERR_STAT		(APP_BLK_REG_ADDR + 0x260)
#define PCIE_LNK_ERR_MSK		(APP_BLK_REG_ADDR + 0x264)

/**
 * FCoE/FIP Ethertype Register
 * 31:16 -- Chip wide value for FIP type
 * 15:0  -- Chip wide value for FCoE type
 */
#define FCOE_FIP_ETH_TYPE		(APP_BLK_REG_ADDR + 0x280)

/**
 * Reserved Ethertype Register
 * 31:16 -- Reserved
 * 15:0  -- Other ethertype
 */
#define RESV_ETH_TYPE			(APP_BLK_REG_ADDR + 0x284)

/**
 * Host Command Status Registers
 * Each set consists of 3 registers :
 * clear, set, cmd
 * 16 such register sets in all
 * See catapult_spec.pdf for detailed functionality
 * Put each type in a single macro accessed by _num ?
 */
#define HOST_CMDSTS0_CLR_REG		(APP_BLK_REG_ADDR + 0x500)
#define HOST_CMDSTS0_SET_REG		(APP_BLK_REG_ADDR + 0x504)
#define HOST_CMDSTS0_REG		(APP_BLK_REG_ADDR + 0x508)
#define HOST_CMDSTS1_CLR_REG		(APP_BLK_REG_ADDR + 0x510)
#define HOST_CMDSTS1_SET_REG		(APP_BLK_REG_ADDR + 0x514)
#define HOST_CMDSTS1_REG		(APP_BLK_REG_ADDR + 0x518)
#define HOST_CMDSTS2_CLR_REG		(APP_BLK_REG_ADDR + 0x520)
#define HOST_CMDSTS2_SET_REG		(APP_BLK_REG_ADDR + 0x524)
#define HOST_CMDSTS2_REG		(APP_BLK_REG_ADDR + 0x528)
#define HOST_CMDSTS3_CLR_REG		(APP_BLK_REG_ADDR + 0x530)
#define HOST_CMDSTS3_SET_REG		(APP_BLK_REG_ADDR + 0x534)
#define HOST_CMDSTS3_REG		(APP_BLK_REG_ADDR + 0x538)
#define HOST_CMDSTS4_CLR_REG		(APP_BLK_REG_ADDR + 0x540)
#define HOST_CMDSTS4_SET_REG		(APP_BLK_REG_ADDR + 0x544)
#define HOST_CMDSTS4_REG		(APP_BLK_REG_ADDR + 0x548)
#define HOST_CMDSTS5_CLR_REG		(APP_BLK_REG_ADDR + 0x550)
#define HOST_CMDSTS5_SET_REG		(APP_BLK_REG_ADDR + 0x554)
#define HOST_CMDSTS5_REG		(APP_BLK_REG_ADDR + 0x558)
#define HOST_CMDSTS6_CLR_REG		(APP_BLK_REG_ADDR + 0x560)
#define HOST_CMDSTS6_SET_REG		(APP_BLK_REG_ADDR + 0x564)
#define HOST_CMDSTS6_REG		(APP_BLK_REG_ADDR + 0x568)
#define HOST_CMDSTS7_CLR_REG		(APP_BLK_REG_ADDR + 0x570)
#define HOST_CMDSTS7_SET_REG		(APP_BLK_REG_ADDR + 0x574)
#define HOST_CMDSTS7_REG		(APP_BLK_REG_ADDR + 0x578)
#define HOST_CMDSTS8_CLR_REG		(APP_BLK_REG_ADDR + 0x580)
#define HOST_CMDSTS8_SET_REG		(APP_BLK_REG_ADDR + 0x584)
#define HOST_CMDSTS8_REG		(APP_BLK_REG_ADDR + 0x588)
#define HOST_CMDSTS9_CLR_REG		(APP_BLK_REG_ADDR + 0x590)
#define HOST_CMDSTS9_SET_REG		(APP_BLK_REG_ADDR + 0x594)
#define HOST_CMDSTS9_REG		(APP_BLK_REG_ADDR + 0x598)
#define HOST_CMDSTS10_CLR_REG		(APP_BLK_REG_ADDR + 0x5A0)
#define HOST_CMDSTS10_SET_REG		(APP_BLK_REG_ADDR + 0x5A4)
#define HOST_CMDSTS10_REG		(APP_BLK_REG_ADDR + 0x5A8)
#define HOST_CMDSTS11_CLR_REG		(APP_BLK_REG_ADDR + 0x5B0)
#define HOST_CMDSTS11_SET_REG		(APP_BLK_REG_ADDR + 0x5B4)
#define HOST_CMDSTS11_REG		(APP_BLK_REG_ADDR + 0x5B8)
#define HOST_CMDSTS12_CLR_REG		(APP_BLK_REG_ADDR + 0x5C0)
#define HOST_CMDSTS12_SET_REG		(APP_BLK_REG_ADDR + 0x5C4)
#define HOST_CMDSTS12_REG		(APP_BLK_REG_ADDR + 0x5C8)
#define HOST_CMDSTS13_CLR_REG		(APP_BLK_REG_ADDR + 0x5D0)
#define HOST_CMDSTS13_SET_REG		(APP_BLK_REG_ADDR + 0x5D4)
#define HOST_CMDSTS13_REG		(APP_BLK_REG_ADDR + 0x5D8)
#define HOST_CMDSTS14_CLR_REG		(APP_BLK_REG_ADDR + 0x5E0)
#define HOST_CMDSTS14_SET_REG		(APP_BLK_REG_ADDR + 0x5E4)
#define HOST_CMDSTS14_REG		(APP_BLK_REG_ADDR + 0x5E8)
#define HOST_CMDSTS15_CLR_REG		(APP_BLK_REG_ADDR + 0x5F0)
#define HOST_CMDSTS15_SET_REG		(APP_BLK_REG_ADDR + 0x5F4)
#define HOST_CMDSTS15_REG		(APP_BLK_REG_ADDR + 0x5F8)

/**
 * LPU0 Block Register Address Offset from BAR0
 * Range 0x18000 - 0x18033
 */
#define LPU0_BLK_REG_ADDR		0x00018000

/**
 * LPU0 Registers
 * Should they be directly used from host,
 * except for diagnostics ?
 * CTL_REG : Control register
 * CMD_REG : Triggers exec. of cmd. in
 *           Mailbox memory
 */
#define LPU0_MBOX_CTL_REG		(LPU0_BLK_REG_ADDR + 0x000)
#define LPU0_MBOX_CMD_REG		(LPU0_BLK_REG_ADDR + 0x004)
#define LPU0_MBOX_LINK_0REG		(LPU0_BLK_REG_ADDR + 0x008)
#define LPU1_MBOX_LINK_0REG		(LPU0_BLK_REG_ADDR + 0x00c)
#define LPU0_MBOX_STATUS_0REG		(LPU0_BLK_REG_ADDR + 0x010)
#define LPU1_MBOX_STATUS_0REG		(LPU0_BLK_REG_ADDR + 0x014)
#define LPU0_ERR_STATUS_REG		(LPU0_BLK_REG_ADDR + 0x018)
#define LPU0_ERR_SET_REG		(LPU0_BLK_REG_ADDR + 0x020)

/**
 * LPU1 Block Register Address Offset from BAR0
 * Range 0x18400 - 0x18433
 */
#define LPU1_BLK_REG_ADDR		0x00018400

/**
 * LPU1 Registers
 * Same as LPU0 registers above
 */
#define LPU1_MBOX_CTL_REG		(LPU1_BLK_REG_ADDR + 0x000)
#define LPU1_MBOX_CMD_REG		(LPU1_BLK_REG_ADDR + 0x004)
#define LPU0_MBOX_LINK_1REG		(LPU1_BLK_REG_ADDR + 0x008)
#define LPU1_MBOX_LINK_1REG		(LPU1_BLK_REG_ADDR + 0x00c)
#define LPU0_MBOX_STATUS_1REG		(LPU1_BLK_REG_ADDR + 0x010)
#define LPU1_MBOX_STATUS_1REG		(LPU1_BLK_REG_ADDR + 0x014)
#define LPU1_ERR_STATUS_REG		(LPU1_BLK_REG_ADDR + 0x018)
#define LPU1_ERR_SET_REG		(LPU1_BLK_REG_ADDR + 0x020)

/**
 * PSS Block Register Address Offset from BAR0
 * Range 0x18800 - 0x188DB
 */
#define PSS_BLK_REG_ADDR		0x00018800

/**
 * PSS Registers
 * For details, see catapult_spec.pdf
 * ERR_STATUS_REG : Indicates error in PSS module
 * RAM_ERR_STATUS_REG : Indicates RAM module that detected error
 */
#define ERR_STATUS_SET			(PSS_BLK_REG_ADDR + 0x018)
#define PSS_RAM_ERR_STATUS_REG		(PSS_BLK_REG_ADDR + 0x01C)

/**
 * PSS Semaphore Lock Registers, total 16
 * First read when unlocked returns 0,
 * and is set to 1, atomically.
 * Subsequent reads returns 1.
 * To clear set the value to 0.
 * Range : 0x20 to 0x5c
 */
#define PSS_SEM_LOCK_REG(_num)		\
	(PSS_BLK_REG_ADDR + 0x020 + ((_num) << 2))

/**
 * PSS Semaphore Status Registers,
 * corresponding to the lock registers above
 */
#define PSS_SEM_STATUS_REG(_num)		\
	(PSS_BLK_REG_ADDR + 0x060 + ((_num) << 2))

/**
 * Catapult CPQ Registers
 * Defines for Mailbox Registers
 * Used to send mailbox commands to firmware from
 * host. The data part is written to the MBox
 * memory, registers are used to indicate that
 * a commnad is resident in memory.
 *
 * Note : LPU0<->LPU1 mailboxes are not listed here
 */
#define CPQ_BLK_REG_ADDR		0x00019000

#define HOSTFN0_LPU0_MBOX1_CMD_STAT	(CPQ_BLK_REG_ADDR + 0x130)
#define HOSTFN0_LPU1_MBOX1_CMD_STAT	(CPQ_BLK_REG_ADDR + 0x134)
#define LPU0_HOSTFN0_MBOX1_CMD_STAT	(CPQ_BLK_REG_ADDR + 0x138)
#define LPU1_HOSTFN0_MBOX1_CMD_STAT	(CPQ_BLK_REG_ADDR + 0x13C)

#define HOSTFN1_LPU0_MBOX1_CMD_STAT	(CPQ_BLK_REG_ADDR + 0x140)
#define HOSTFN1_LPU1_MBOX1_CMD_STAT	(CPQ_BLK_REG_ADDR + 0x144)
#define LPU0_HOSTFN1_MBOX1_CMD_STAT	(CPQ_BLK_REG_ADDR + 0x148)
#define LPU1_HOSTFN1_MBOX1_CMD_STAT	(CPQ_BLK_REG_ADDR + 0x14C)

#define HOSTFN2_LPU0_MBOX1_CMD_STAT	(CPQ_BLK_REG_ADDR + 0x170)
#define HOSTFN2_LPU1_MBOX1_CMD_STAT	(CPQ_BLK_REG_ADDR + 0x174)
#define LPU0_HOSTFN2_MBOX1_CMD_STAT	(CPQ_BLK_REG_ADDR + 0x178)
#define LPU1_HOSTFN2_MBOX1_CMD_STAT	(CPQ_BLK_REG_ADDR + 0x17C)

#define HOSTFN3_LPU0_MBOX1_CMD_STAT	(CPQ_BLK_REG_ADDR + 0x180)
#define HOSTFN3_LPU1_MBOX1_CMD_STAT	(CPQ_BLK_REG_ADDR + 0x184)
#define LPU0_HOSTFN3_MBOX1_CMD_STAT	(CPQ_BLK_REG_ADDR + 0x188)
#define LPU1_HOSTFN3_MBOX1_CMD_STAT	(CPQ_BLK_REG_ADDR + 0x18C)

/* Host Function Force Parity Error Registers */
#define HOSTFN0_LPU_FORCE_PERR		(CPQ_BLK_REG_ADDR + 0x120)
#define HOSTFN1_LPU_FORCE_PERR		(CPQ_BLK_REG_ADDR + 0x124)
#define HOSTFN2_LPU_FORCE_PERR		(CPQ_BLK_REG_ADDR + 0x128)
#define HOSTFN3_LPU_FORCE_PERR		(CPQ_BLK_REG_ADDR + 0x12C)

/* LL Port[0|1] Halt Mask Registers */
#define LL_HALT_MSK_P0			(CPQ_BLK_REG_ADDR + 0x1A0)
#define LL_HALT_MSK_P1			(CPQ_BLK_REG_ADDR + 0x1B0)

/* LL Port[0|1] Error Mask Registers */
#define LL_ERR_MSK_P0			(CPQ_BLK_REG_ADDR + 0x1D0)
#define LL_ERR_MSK_P1			(CPQ_BLK_REG_ADDR + 0x1D4)

/* EMC FLI (Flash Controller) Block Register Address Offset from BAR0 */
#define FLI_BLK_REG_ADDR		0x0001D000

/* EMC FLI Registers */
#define FLI_CMD_REG			(FLI_BLK_REG_ADDR + 0x000)
#define FLI_ADDR_REG			(FLI_BLK_REG_ADDR + 0x004)
#define FLI_CTL_REG			(FLI_BLK_REG_ADDR + 0x008)
#define FLI_WRDATA_REG			(FLI_BLK_REG_ADDR + 0x00C)
#define FLI_RDDATA_REG			(FLI_BLK_REG_ADDR + 0x010)
#define FLI_DEV_STATUS_REG		(FLI_BLK_REG_ADDR + 0x014)
#define FLI_SIG_WD_REG			(FLI_BLK_REG_ADDR + 0x018)

/**
 * RO register
 * 31:16 -- Vendor Id
 * 15:0  -- Device Id
 */
#define FLI_DEV_VENDOR_REG		(FLI_BLK_REG_ADDR + 0x01C)
#define FLI_ERR_STATUS_REG		(FLI_BLK_REG_ADDR + 0x020)

/**
 * RAD (RxAdm) Block Register Address Offset from BAR0
 * RAD0 Range : 0x20000 - 0x203FF
 * RAD1 Range : 0x20400 - 0x207FF
 */
#define RAD0_BLK_REG_ADDR		0x00020000
#define RAD1_BLK_REG_ADDR		0x00020400

/* RAD0 Registers */
#define RAD0_CTL_REG			(RAD0_BLK_REG_ADDR + 0x000)
#define RAD0_PE_PARM_REG		(RAD0_BLK_REG_ADDR + 0x004)
#define RAD0_BCN_REG			(RAD0_BLK_REG_ADDR + 0x008)

/* Default function ID register */
#define RAD0_DEFAULT_REG		(RAD0_BLK_REG_ADDR + 0x00C)

/* Default promiscuous ID register */
#define RAD0_PROMISC_REG		(RAD0_BLK_REG_ADDR + 0x010)

#define RAD0_BCNQ_REG			(RAD0_BLK_REG_ADDR + 0x014)

/*
 * This register selects 1 of 8 PM Q's using
 * VLAN pri, for non-BCN packets without a VLAN tag
 */
#define RAD0_DEFAULTQ_REG		(RAD0_BLK_REG_ADDR + 0x018)

#define RAD0_ERR_STS			(RAD0_BLK_REG_ADDR + 0x01C)
#define RAD0_SET_ERR_STS		(RAD0_BLK_REG_ADDR + 0x020)
#define RAD0_ERR_INT_EN			(RAD0_BLK_REG_ADDR + 0x024)
#define RAD0_FIRST_ERR			(RAD0_BLK_REG_ADDR + 0x028)
#define RAD0_FORCE_ERR			(RAD0_BLK_REG_ADDR + 0x02C)

#define RAD0_IF_RCVD			(RAD0_BLK_REG_ADDR + 0x030)
#define RAD0_IF_RCVD_OCTETS_HIGH	(RAD0_BLK_REG_ADDR + 0x034)
#define RAD0_IF_RCVD_OCTETS_LOW		(RAD0_BLK_REG_ADDR + 0x038)
#define RAD0_IF_RCVD_VLAN		(RAD0_BLK_REG_ADDR + 0x03C)
#define RAD0_IF_RCVD_UCAST		(RAD0_BLK_REG_ADDR + 0x040)
#define RAD0_IF_RCVD_UCAST_OCTETS_HIGH	(RAD0_BLK_REG_ADDR + 0x044)
#define RAD0_IF_RCVD_UCAST_OCTETS_LOW   (RAD0_BLK_REG_ADDR + 0x048)
#define RAD0_IF_RCVD_UCAST_VLAN		(RAD0_BLK_REG_ADDR + 0x04C)
#define RAD0_IF_RCVD_MCAST		(RAD0_BLK_REG_ADDR + 0x050)
#define RAD0_IF_RCVD_MCAST_OCTETS_HIGH  (RAD0_BLK_REG_ADDR + 0x054)
#define RAD0_IF_RCVD_MCAST_OCTETS_LOW   (RAD0_BLK_REG_ADDR + 0x058)
#define RAD0_IF_RCVD_MCAST_VLAN		(RAD0_BLK_REG_ADDR + 0x05C)
#define RAD0_IF_RCVD_BCAST		(RAD0_BLK_REG_ADDR + 0x060)
#define RAD0_IF_RCVD_BCAST_OCTETS_HIGH  (RAD0_BLK_REG_ADDR + 0x064)
#define RAD0_IF_RCVD_BCAST_OCTETS_LOW   (RAD0_BLK_REG_ADDR + 0x068)
#define RAD0_IF_RCVD_BCAST_VLAN		(RAD0_BLK_REG_ADDR + 0x06C)
#define RAD0_DROPPED_FRAMES		(RAD0_BLK_REG_ADDR + 0x070)

#define RAD0_MAC_MAN_1H			(RAD0_BLK_REG_ADDR + 0x080)
#define RAD0_MAC_MAN_1L			(RAD0_BLK_REG_ADDR + 0x084)
#define RAD0_MAC_MAN_2H			(RAD0_BLK_REG_ADDR + 0x088)
#define RAD0_MAC_MAN_2L			(RAD0_BLK_REG_ADDR + 0x08C)
#define RAD0_MAC_MAN_3H			(RAD0_BLK_REG_ADDR + 0x090)
#define RAD0_MAC_MAN_3L			(RAD0_BLK_REG_ADDR + 0x094)
#define RAD0_MAC_MAN_4H			(RAD0_BLK_REG_ADDR + 0x098)
#define RAD0_MAC_MAN_4L			(RAD0_BLK_REG_ADDR + 0x09C)

#define RAD0_LAST4_IP			(RAD0_BLK_REG_ADDR + 0x100)

/* RAD1 Registers */
#define RAD1_CTL_REG			(RAD1_BLK_REG_ADDR + 0x000)
#define RAD1_PE_PARM_REG		(RAD1_BLK_REG_ADDR + 0x004)
#define RAD1_BCN_REG			(RAD1_BLK_REG_ADDR + 0x008)

/* Default function ID register */
#define RAD1_DEFAULT_REG		(RAD1_BLK_REG_ADDR + 0x00C)

/* Promiscuous function ID register */
#define RAD1_PROMISC_REG		(RAD1_BLK_REG_ADDR + 0x010)

#define RAD1_BCNQ_REG			(RAD1_BLK_REG_ADDR + 0x014)

/*
 * This register selects 1 of 8 PM Q's using
 * VLAN pri, for non-BCN packets without a VLAN tag
 */
#define RAD1_DEFAULTQ_REG		(RAD1_BLK_REG_ADDR + 0x018)

#define RAD1_ERR_STS			(RAD1_BLK_REG_ADDR + 0x01C)
#define RAD1_SET_ERR_STS		(RAD1_BLK_REG_ADDR + 0x020)
#define RAD1_ERR_INT_EN			(RAD1_BLK_REG_ADDR + 0x024)

/**
 * TXA Block Register Address Offset from BAR0
 * TXA0 Range : 0x21000 - 0x213FF
 * TXA1 Range : 0x21400 - 0x217FF
 */
#define TXA0_BLK_REG_ADDR		0x00021000
#define TXA1_BLK_REG_ADDR		0x00021400

/* TXA Registers */
#define TXA0_CTRL_REG			(TXA0_BLK_REG_ADDR + 0x000)
#define TXA1_CTRL_REG			(TXA1_BLK_REG_ADDR + 0x000)

/**
 * TSO Sequence # Registers (RO)
 * Total 8 (for 8 queues)
 * Holds the last seq.# for TSO frames
 * See catapult_spec.pdf for more details
 */
#define TXA0_TSO_TCP_SEQ_REG(_num)		\
	(TXA0_BLK_REG_ADDR + 0x020 + ((_num) << 2))

#define TXA1_TSO_TCP_SEQ_REG(_num)		\
	(TXA1_BLK_REG_ADDR + 0x020 + ((_num) << 2))

/**
 * TSO IP ID # Registers (RO)
 * Total 8 (for 8 queues)
 * Holds the last IP ID for TSO frames
 * See catapult_spec.pdf for more details
 */
#define TXA0_TSO_IP_INFO_REG(_num)		\
	(TXA0_BLK_REG_ADDR + 0x040 + ((_num) << 2))

#define TXA1_TSO_IP_INFO_REG(_num)		\
	(TXA1_BLK_REG_ADDR + 0x040 + ((_num) << 2))

/**
 * RXA Block Register Address Offset from BAR0
 * RXA0 Range : 0x21800 - 0x21BFF
 * RXA1 Range : 0x21C00 - 0x21FFF
 */
#define RXA0_BLK_REG_ADDR		0x00021800
#define RXA1_BLK_REG_ADDR		0x00021C00

/* RXA Registers */
#define RXA0_CTL_REG			(RXA0_BLK_REG_ADDR + 0x040)
#define RXA1_CTL_REG			(RXA1_BLK_REG_ADDR + 0x040)

/**
 * PPLB Block Register Address Offset from BAR0
 * PPLB0 Range : 0x22000 - 0x223FF
 * PPLB1 Range : 0x22400 - 0x227FF
 */
#define PLB0_BLK_REG_ADDR		0x00022000
#define PLB1_BLK_REG_ADDR		0x00022400

/**
 * PLB Registers
 * Holds RL timer used time stamps in RLT tagged frames
 */
#define PLB0_ECM_TIMER_REG		(PLB0_BLK_REG_ADDR + 0x05C)
#define PLB1_ECM_TIMER_REG		(PLB1_BLK_REG_ADDR + 0x05C)

/* Controls the rate-limiter on each of the priority class */
#define PLB0_RL_CTL			(PLB0_BLK_REG_ADDR + 0x060)
#define PLB1_RL_CTL			(PLB1_BLK_REG_ADDR + 0x060)

/**
 * Max byte register, total 8, 0-7
 * see catapult_spec.pdf for details
 */
#define PLB0_RL_MAX_BC(_num)			\
	(PLB0_BLK_REG_ADDR + 0x064 + ((_num) << 2))
#define PLB1_RL_MAX_BC(_num)			\
	(PLB1_BLK_REG_ADDR + 0x064 + ((_num) << 2))

/**
 * RL Time Unit Register for priority 0-7
 * 4 bits per priority
 * (2^rl_unit)*1us is the actual time period
 */
#define PLB0_RL_TU_PRIO			(PLB0_BLK_REG_ADDR + 0x084)
#define PLB1_RL_TU_PRIO			(PLB1_BLK_REG_ADDR + 0x084)

/**
 * RL byte count register,
 * bytes transmitted in (rl_unit*1)us time period
 * 1 per priority, 8 in all, 0-7.
 */
#define PLB0_RL_BYTE_CNT(_num)			\
	(PLB0_BLK_REG_ADDR + 0x088 + ((_num) << 2))
#define PLB1_RL_BYTE_CNT(_num)			\
	(PLB1_BLK_REG_ADDR + 0x088 + ((_num) << 2))

/**
 * RL Min factor register
 * 2 bits per priority,
 * 4 factors possible: 1, 0.5, 0.25, 0
 * 2'b00 - 0; 2'b01 - 0.25; 2'b10 - 0.5; 2'b11 - 1
 */
#define PLB0_RL_MIN_REG			(PLB0_BLK_REG_ADDR + 0x0A8)
#define PLB1_RL_MIN_REG			(PLB1_BLK_REG_ADDR + 0x0A8)

/**
 * RL Max factor register
 * 2 bits per priority,
 * 4 factors possible: 1, 0.5, 0.25, 0
 * 2'b00 - 0; 2'b01 - 0.25; 2'b10 - 0.5; 2'b11 - 1
 */
#define PLB0_RL_MAX_REG			(PLB0_BLK_REG_ADDR + 0x0AC)
#define PLB1_RL_MAX_REG			(PLB1_BLK_REG_ADDR + 0x0AC)

/* MAC SERDES Address Paging register */
#define PLB0_EMS_ADD_REG		(PLB0_BLK_REG_ADDR + 0xD0)
#define PLB1_EMS_ADD_REG		(PLB1_BLK_REG_ADDR + 0xD0)

/* LL EMS Registers */
#define LL_EMS0_BLK_REG_ADDR		0x00026800
#define LL_EMS1_BLK_REG_ADDR		0x00026C00

/**
 * BPC Block Register Address Offset from BAR0
 * BPC0 Range : 0x23000 - 0x233FF
 * BPC1 Range : 0x23400 - 0x237FF
 */
#define BPC0_BLK_REG_ADDR		0x00023000
#define BPC1_BLK_REG_ADDR		0x00023400

/**
 * PMM Block Register Address Offset from BAR0
 * PMM0 Range : 0x23800 - 0x23BFF
 * PMM1 Range : 0x23C00 - 0x23FFF
 */
#define PMM0_BLK_REG_ADDR		0x00023800
#define PMM1_BLK_REG_ADDR		0x00023C00

/**
 * HQM Block Register Address Offset from BAR0
 * HQM0 Range : 0x24000 - 0x243FF
 * HQM1 Range : 0x24400 - 0x247FF
 */
#define HQM0_BLK_REG_ADDR		0x00024000
#define HQM1_BLK_REG_ADDR		0x00024400

/**
 * HQM Control Register
 * Controls some aspects of IB
 * See catapult_spec.pdf for details
 */
#define HQM0_CTL_REG			(HQM0_BLK_REG_ADDR + 0x000)
#define HQM1_CTL_REG			(HQM1_BLK_REG_ADDR + 0x000)

/**
 * HQM Stop Q Semaphore Registers.
 * Only one Queue resource can be stopped at
 * any given time. This register controls access
 * to the single stop Q resource.
 * See catapult_spec.pdf for details
 */
#define HQM0_RXQ_STOP_SEM		(HQM0_BLK_REG_ADDR + 0x028)
#define HQM0_TXQ_STOP_SEM		(HQM0_BLK_REG_ADDR + 0x02C)
#define HQM1_RXQ_STOP_SEM		(HQM1_BLK_REG_ADDR + 0x028)
#define HQM1_TXQ_STOP_SEM		(HQM1_BLK_REG_ADDR + 0x02C)

/**
 * LUT Block Register Address Offset from BAR0
 * LUT0 Range : 0x25800 - 0x25BFF
 * LUT1 Range : 0x25C00 - 0x25FFF
 */
#define LUT0_BLK_REG_ADDR		0x00025800
#define LUT1_BLK_REG_ADDR		0x00025C00

/**
 * LUT Registers
 * See catapult_spec.pdf for details
 */
#define LUT0_ERR_STS			(LUT0_BLK_REG_ADDR + 0x000)
#define LUT1_ERR_STS			(LUT1_BLK_REG_ADDR + 0x000)
#define LUT0_SET_ERR_STS		(LUT0_BLK_REG_ADDR + 0x004)
#define LUT1_SET_ERR_STS		(LUT1_BLK_REG_ADDR + 0x004)

/**
 * TRC (Debug/Trace) Register Offset from BAR0
 * Range : 0x26000 -- 0x263FFF
 */
#define TRC_BLK_REG_ADDR		0x00026000

/**
 * TRC Registers
 * See catapult_spec.pdf for details of each
 */
#define TRC_CTL_REG			(TRC_BLK_REG_ADDR + 0x000)
#define TRC_MODS_REG			(TRC_BLK_REG_ADDR + 0x004)
#define TRC_TRGC_REG			(TRC_BLK_REG_ADDR + 0x008)
#define TRC_CNT1_REG			(TRC_BLK_REG_ADDR + 0x010)
#define TRC_CNT2_REG			(TRC_BLK_REG_ADDR + 0x014)
#define TRC_NXTS_REG			(TRC_BLK_REG_ADDR + 0x018)
#define TRC_DIRR_REG			(TRC_BLK_REG_ADDR + 0x01C)

/**
 * TRC Trigger match filters, total 10
 * Determines the trigger condition
 */
#define TRC_TRGM_REG(_num)		\
	(TRC_BLK_REG_ADDR + 0x040 + ((_num) << 2))

/**
 * TRC Next State filters, total 10
 * Determines the next state conditions
 */
#define TRC_NXTM_REG(_num)		\
	(TRC_BLK_REG_ADDR + 0x080 + ((_num) << 2))

/**
 * TRC Store Match filters, total 10
 * Determines the store conditions
 */
#define TRC_STRM_REG(_num)		\
	(TRC_BLK_REG_ADDR + 0x0C0 + ((_num) << 2))

/* DOORBELLS ACCESS */

/**
 * Catapult doorbells
 * Each doorbell-queue set has
 * 1 RxQ, 1 TxQ, 2 IBs in that order
 * Size of each entry in 32 bytes, even though only 1 word
 * is used. For Non-VM case each doorbell-q set is
 * separated by 128 bytes, for VM case it is separated
 * by 4K bytes
 * Non VM case Range : 0x38000 - 0x39FFF
 * VM case Range     : 0x100000 - 0x11FFFF
 * The range applies to both HQMs
 */
#define HQM_DOORBELL_BLK_BASE_ADDR	0x00038000
#define HQM_DOORBELL_VM_BLK_BASE_ADDR	0x00100000

/* MEMORY ACCESS */

/**
 * Catapult H/W Block Memory Access Address
 * To the host a memory space of 32K (page) is visible
 * at a time. The address range is from 0x08000 to 0x0FFFF
 */
#define HW_BLK_HOST_MEM_ADDR		0x08000

/**
 * Catapult LUT Memory Access Page Numbers
 * Range : LUT0 0xa0-0xa1
 *         LUT1 0xa2-0xa3
 */
#define LUT0_MEM_BLK_BASE_PG_NUM	0x000000A0
#define LUT1_MEM_BLK_BASE_PG_NUM	0x000000A2

/**
 * Catapult RxFn Database Memory Block Base Offset
 *
 * The Rx function database exists in LUT block.
 * In PCIe space this is accessible as a 256x32
 * bit block. Each entry in this database is 4
 * (4 byte) words. Max. entries is 64.
 * Address of an entry corresponding to a function
 * = base_addr + (function_no. * 16)
 */
#define RX_FNDB_RAM_BASE_OFFSET		0x0000B400

/**
 * Catapult TxFn Database Memory Block Base Offset Address
 *
 * The Tx function database exists in LUT block.
 * In PCIe space this is accessible as a 64x32
 * bit block. Each entry in this database is 1
 * (4 byte) word. Max. entries is 64.
 * Address of an entry corresponding to a function
 * = base_addr + (function_no. * 4)
 */
#define TX_FNDB_RAM_BASE_OFFSET		0x0000B800

/**
 * Catapult Unicast CAM Base Offset Address
 *
 * Exists in LUT memory space.
 * Shared by both the LL & FCoE driver.
 * Size is 256x48 bits; mapped to PCIe space
 * 512x32 bit blocks. For each address, bits
 * are written in the order : [47:32] and then
 * [31:0].
 */
#define UCAST_CAM_BASE_OFFSET		0x0000A800

/**
 * Catapult Unicast RAM Base Offset Address
 *
 * Exists in LUT memory space.
 * Shared by both the LL & FCoE driver.
 * Size is 256x9 bits.
 */
#define UCAST_RAM_BASE_OFFSET		0x0000B000

/**
 * Catapult Mulicast CAM Base Offset Address
 *
 * Exists in LUT memory space.
 * Shared by both the LL & FCoE driver.
 * Size is 256x48 bits; mapped to PCIe space
 * 512x32 bit blocks. For each address, bits
 * are written in the order : [47:32] and then
 * [31:0].
 */
#define MCAST_CAM_BASE_OFFSET		0x0000A000

/**
 * Catapult VLAN RAM Base Offset Address
 *
 * Exists in LUT memory space.
 * Size is 4096x66 bits; mapped to PCIe space as
 * 8192x32 bit blocks.
 * All the 4K entries are within the address range
 * 0x0000 to 0x8000, so in the first LUT page.
 */
#define VLAN_RAM_BASE_OFFSET		0x00000000

/**
 * Catapult Tx Stats RAM Base Offset Address
 *
 * Exists in LUT memory space.
 * Size is 1024x33 bits;
 * Each Tx function has 64 bytes of space
 */
#define TX_STATS_RAM_BASE_OFFSET	0x00009000

/**
 * Catapult Rx Stats RAM Base Offset Address
 *
 * Exists in LUT memory space.
 * Size is 1024x33 bits;
 * Each Rx function has 64 bytes of space
 */
#define RX_STATS_RAM_BASE_OFFSET	0x00008000

/* Catapult RXA Memory Access Page Numbers */
#define RXA0_MEM_BLK_BASE_PG_NUM	0x0000008C
#define RXA1_MEM_BLK_BASE_PG_NUM	0x0000008D

/**
 * Catapult Multicast Vector Table Base Offset Address
 *
 * Exists in RxA memory space.
 * Organized as 512x65 bit block.
 * However for each entry 16 bytes allocated (power of 2)
 * Total size 512*16 bytes.
 * There are two logical divisions, 256 entries each :
 * a) Entries 0x00 to 0xff (256) -- Approx. MVT
 *    Offset 0x000 to 0xFFF
 * b) Entries 0x100 to 0x1ff (256) -- Exact MVT
 *    Offsets 0x1000 to 0x1FFF
 */
#define MCAST_APPROX_MVT_BASE_OFFSET	0x00000000
#define MCAST_EXACT_MVT_BASE_OFFSET	0x00001000

/**
 * Catapult RxQ Translate Table (RIT) Base Offset Address
 *
 * Exists in RxA memory space
 * Total no. of entries 64
 * Each entry is 1 (4 byte) word.
 * 31:12 -- Reserved
 * 11:0  -- Two 6 bit RxQ Ids
 */
#define FUNCTION_TO_RXQ_TRANSLATE	0x00002000

/* Catapult RxAdm (RAD) Memory Access Page Numbers */
#define RAD0_MEM_BLK_BASE_PG_NUM	0x00000086
#define RAD1_MEM_BLK_BASE_PG_NUM	0x00000087

/**
 * Catapult RSS Table Base Offset Address
 *
 * Exists in RAD memory space.
 * Each entry is 352 bits, but aligned on
 * 64 byte (512 bit) boundary. Accessed
 * 4 byte words, the whole entry can be
 * broken into 11 word accesses.
 */
#define RSS_TABLE_BASE_OFFSET		0x00000800

/**
 * Catapult CPQ Block Page Number
 * This value is written to the page number registers
 * to access the memory associated with the mailboxes.
 */
#define CPQ_BLK_PG_NUM			0x00000005

/**
 * Clarification :
 * LL functions are 2 & 3; can HostFn0/HostFn1
 * <-> LPU0/LPU1 memories be used ?
 */
/**
 * Catapult HostFn0/HostFn1 to LPU0/LPU1 Mbox memory
 * Per catapult_spec.pdf, the offset of the mbox
 * memory is in the register space at an offset of 0x200
 */
#define CPQ_BLK_REG_MBOX_ADDR		(CPQ_BLK_REG_ADDR + 0x200)

#define HOSTFN_LPU_MBOX			(CPQ_BLK_REG_MBOX_ADDR + 0x000)

/* Catapult LPU0/LPU1 to HostFn0/HostFn1 Mbox memory */
#define LPU_HOSTFN_MBOX			(CPQ_BLK_REG_MBOX_ADDR + 0x080)

/**
 * Catapult HQM Block Page Number
 * This is written to the page number register for
 * the appropriate function to access the memory
 * associated with HQM
 */
#define HQM0_BLK_PG_NUM			0x00000096
#define HQM1_BLK_PG_NUM			0x00000097

/**
 * Note that TxQ and RxQ entries are interlaced
 * the HQM memory, i.e RXQ0, TXQ0, RXQ1, TXQ1.. etc.
 */

#define HQM_RXTX_Q_RAM_BASE_OFFSET	0x00004000

/**
 * CQ Memory
 * Exists in HQM Memory space
 * Each entry is 16 (4 byte) words of which
 * only 12 words are used for configuration
 * Total 64 entries per HQM memory space
 */
#define HQM_CQ_RAM_BASE_OFFSET		0x00006000

/**
 * Interrupt Block (IB) Memory
 * Exists in HQM Memory space
 * Each entry is 8 (4 byte) words of which
 * only 5 words are used for configuration
 * Total 128 entries per HQM memory space
 */
#define HQM_IB_RAM_BASE_OFFSET		0x00001000

/**
 * Index Table (IT) Memory
 * Exists in HQM Memory space
 * Each entry is 1 (4 byte) word which
 * is used for configuration
 * Total 128 entries per HQM memory space
 */
#define HQM_INDX_TBL_RAM_BASE_OFFSET	0x00002000

/**
 * PSS Block Memory Page Number
 * This is written to the appropriate page number
 * register to access the CPU memory.
 * Also known as the PSS secondary memory (SMEM).
 * Range : 0x180 to 0x1CF
 * See catapult_spec.pdf for details
 */
#define PSS_BLK_PG_NUM			0x00000180

/**
 * Offsets of different instances of PSS SMEM
 * 2.5M of continuous 1T memory space : 2 blocks
 * of 1M each (32 pages each, page=32KB) and 4 smaller
 * blocks of 128K each (4 pages each, page=32KB)
 * PSS_LMEM_INST0 is used for firmware download
 */
#define PSS_LMEM_INST0			0x00000000
#define PSS_LMEM_INST1			0x00100000
#define PSS_LMEM_INST2			0x00200000
#define PSS_LMEM_INST3			0x00220000
#define PSS_LMEM_INST4			0x00240000
#define PSS_LMEM_INST5			0x00260000

#define BNA_PCI_REG_CT_ADDRSZ		(0x40000)

#define BNA_GET_PAGE_NUM(_base_page, _offset)   \
	((_base_page) + ((_offset) >> 15))

#define BNA_GET_PAGE_OFFSET(_offset)    \
	((_offset) & 0x7fff)

#define BNA_GET_MEM_BASE_ADDR(_bar0, _base_offset)	\
	((_bar0) + HW_BLK_HOST_MEM_ADDR		\
	  + BNA_GET_PAGE_OFFSET((_base_offset)))

#define BNA_GET_VLAN_MEM_ENTRY_ADDR(_bar0, _fn_id, _vlan_id)\
	(_bar0 + (HW_BLK_HOST_MEM_ADDR)  \
	+ (BNA_GET_PAGE_OFFSET(VLAN_RAM_BASE_OFFSET))	\
	+ (((_fn_id) & 0x3f) << 9)	  \
	+ (((_vlan_id) & 0xfe0) >> 3))

/**
 *
 *  Interrupt related bits, flags and macros
 *
 */

#define __LPU02HOST_MBOX0_STATUS_BITS 0x00100000
#define __LPU12HOST_MBOX0_STATUS_BITS 0x00200000
#define __LPU02HOST_MBOX1_STATUS_BITS 0x00400000
#define __LPU12HOST_MBOX1_STATUS_BITS 0x00800000

#define __LPU02HOST_MBOX0_MASK_BITS	0x00100000
#define __LPU12HOST_MBOX0_MASK_BITS	0x00200000
#define __LPU02HOST_MBOX1_MASK_BITS	0x00400000
#define __LPU12HOST_MBOX1_MASK_BITS	0x00800000

#define __LPU2HOST_MBOX_MASK_BITS			 \
	(__LPU02HOST_MBOX0_MASK_BITS | __LPU02HOST_MBOX1_MASK_BITS |	\
	  __LPU12HOST_MBOX0_MASK_BITS | __LPU12HOST_MBOX1_MASK_BITS)

#define __LPU2HOST_IB_STATUS_BITS	0x0000ffff

#define BNA_IS_LPU0_MBOX_INTR(_intr_status) \
	((_intr_status) & (__LPU02HOST_MBOX0_STATUS_BITS | \
			__LPU02HOST_MBOX1_STATUS_BITS))

#define BNA_IS_LPU1_MBOX_INTR(_intr_status) \
	((_intr_status) & (__LPU12HOST_MBOX0_STATUS_BITS | \
		__LPU12HOST_MBOX1_STATUS_BITS))

#define BNA_IS_MBOX_INTR(_intr_status)		\
	((_intr_status) &			\
	(__LPU02HOST_MBOX0_STATUS_BITS |	\
	 __LPU02HOST_MBOX1_STATUS_BITS |	\
	 __LPU12HOST_MBOX0_STATUS_BITS |	\
	 __LPU12HOST_MBOX1_STATUS_BITS))

#define __EMC_ERROR_STATUS_BITS		0x00010000
#define __LPU0_ERROR_STATUS_BITS	0x00020000
#define __LPU1_ERROR_STATUS_BITS	0x00040000
#define __PSS_ERROR_STATUS_BITS		0x00080000

#define __HALT_STATUS_BITS		0x01000000

#define __EMC_ERROR_MASK_BITS		0x00010000
#define __LPU0_ERROR_MASK_BITS		0x00020000
#define __LPU1_ERROR_MASK_BITS		0x00040000
#define __PSS_ERROR_MASK_BITS		0x00080000

#define __HALT_MASK_BITS		0x01000000

#define __ERROR_MASK_BITS		\
	(__EMC_ERROR_MASK_BITS | __LPU0_ERROR_MASK_BITS | \
	  __LPU1_ERROR_MASK_BITS | __PSS_ERROR_MASK_BITS | \
	  __HALT_MASK_BITS)

#define BNA_IS_ERR_INTR(_intr_status)	\
	((_intr_status) &		\
	(__EMC_ERROR_STATUS_BITS |	\
	 __LPU0_ERROR_STATUS_BITS |	\
	 __LPU1_ERROR_STATUS_BITS |	\
	 __PSS_ERROR_STATUS_BITS  |	\
	 __HALT_STATUS_BITS))

#define BNA_IS_MBOX_ERR_INTR(_intr_status)	\
	(BNA_IS_MBOX_INTR((_intr_status)) |	\
	 BNA_IS_ERR_INTR((_intr_status)))

#define BNA_IS_INTX_DATA_INTR(_intr_status)	\
	((_intr_status) & __LPU2HOST_IB_STATUS_BITS)

#define BNA_INTR_STATUS_MBOX_CLR(_intr_status)			\
do {								\
	(_intr_status) &= ~(__LPU02HOST_MBOX0_STATUS_BITS |	\
			__LPU02HOST_MBOX1_STATUS_BITS |		\
			__LPU12HOST_MBOX0_STATUS_BITS |		\
			__LPU12HOST_MBOX1_STATUS_BITS);		\
} while (0)

#define BNA_INTR_STATUS_ERR_CLR(_intr_status)		\
do {							\
	(_intr_status) &= ~(__EMC_ERROR_STATUS_BITS |	\
		__LPU0_ERROR_STATUS_BITS |		\
		__LPU1_ERROR_STATUS_BITS |		\
		__PSS_ERROR_STATUS_BITS  |		\
		__HALT_STATUS_BITS);			\
} while (0)

#define bna_intx_disable(_bna, _cur_mask)		\
{							\
	(_cur_mask) = readl((_bna)->regs.fn_int_mask);\
	writel(0xffffffff, (_bna)->regs.fn_int_mask);\
}

#define bna_intx_enable(bna, new_mask)			\
	writel((new_mask), (bna)->regs.fn_int_mask)

#define bna_mbox_intr_disable(bna)		\
	writel((readl((bna)->regs.fn_int_mask) | \
	     (__LPU2HOST_MBOX_MASK_BITS | __ERROR_MASK_BITS)), \
	     (bna)->regs.fn_int_mask)

#define bna_mbox_intr_enable(bna)		\
	writel((readl((bna)->regs.fn_int_mask) & \
	     ~(__LPU2HOST_MBOX_MASK_BITS | __ERROR_MASK_BITS)), \
	     (bna)->regs.fn_int_mask)

#define bna_intr_status_get(_bna, _status)				\
{									\
	(_status) = readl((_bna)->regs.fn_int_status);		\
	if ((_status)) {						\
		writel((_status) & ~(__LPU02HOST_MBOX0_STATUS_BITS |\
					  __LPU02HOST_MBOX1_STATUS_BITS |\
					  __LPU12HOST_MBOX0_STATUS_BITS |\
					  __LPU12HOST_MBOX1_STATUS_BITS), \
			      (_bna)->regs.fn_int_status);\
	}								\
}

#define bna_intr_status_get_no_clr(_bna, _status)		\
	(_status) = readl((_bna)->regs.fn_int_status)

#define bna_intr_mask_get(bna, mask)		\
	(*mask) = readl((bna)->regs.fn_int_mask)

#define bna_intr_ack(bna, intr_bmap)		\
	writel((intr_bmap), (bna)->regs.fn_int_status)

#define bna_ib_intx_disable(bna, ib_id)		\
	writel(readl((bna)->regs.fn_int_mask) | \
	    (1 << (ib_id)), \
	    (bna)->regs.fn_int_mask)

#define bna_ib_intx_enable(bna, ib_id)		\
	writel(readl((bna)->regs.fn_int_mask) & \
	    ~(1 << (ib_id)), \
	    (bna)->regs.fn_int_mask)

#define bna_mbox_msix_idx_set(_device) \
do {\
	writel(((_device)->vector & 0x000001FF), \
		(_device)->bna->pcidev.pci_bar_kva + \
		reg_offset[(_device)->bna->pcidev.pci_func].msix_idx);\
} while (0)

/**
 *
 * TxQ, RxQ, CQ related bits, offsets, macros
 *
 */

#define	BNA_Q_IDLE_STATE	0x00008001

#define BNA_GET_DOORBELL_BASE_ADDR(_bar0)	\
	((_bar0) + HQM_DOORBELL_BLK_BASE_ADDR)

#define BNA_GET_DOORBELL_ENTRY_OFFSET(_entry)		\
	((HQM_DOORBELL_BLK_BASE_ADDR)		\
	+ (_entry << 7))

#define BNA_DOORBELL_IB_INT_ACK(_timeout, _events) \
		(0x80000000 | ((_timeout) << 16) | (_events))

#define BNA_DOORBELL_IB_INT_DISABLE		(0x40000000)

/* TxQ Entry Opcodes */
#define BNA_TXQ_WI_SEND			(0x402)	/* Single Frame Transmission */
#define BNA_TXQ_WI_SEND_LSO		(0x403)	/* Multi-Frame Transmission */
#define BNA_TXQ_WI_EXTENSION		(0x104)	/* Extension WI */

/* TxQ Entry Control Flags */
#define BNA_TXQ_WI_CF_FCOE_CRC		(1 << 8)
#define BNA_TXQ_WI_CF_IPID_MODE		(1 << 5)
#define BNA_TXQ_WI_CF_INS_PRIO		(1 << 4)
#define BNA_TXQ_WI_CF_INS_VLAN		(1 << 3)
#define BNA_TXQ_WI_CF_UDP_CKSUM		(1 << 2)
#define BNA_TXQ_WI_CF_TCP_CKSUM		(1 << 1)
#define BNA_TXQ_WI_CF_IP_CKSUM		(1 << 0)

#define BNA_TXQ_WI_L4_HDR_N_OFFSET(_hdr_size, _offset) \
		(((_hdr_size) << 10) | ((_offset) & 0x3FF))

/*
 * Completion Q defines
 */
/* CQ Entry Flags */
#define	BNA_CQ_EF_MAC_ERROR	(1 <<  0)
#define	BNA_CQ_EF_FCS_ERROR	(1 <<  1)
#define	BNA_CQ_EF_TOO_LONG	(1 <<  2)
#define	BNA_CQ_EF_FC_CRC_OK	(1 <<  3)

#define	BNA_CQ_EF_RSVD1		(1 <<  4)
#define	BNA_CQ_EF_L4_CKSUM_OK	(1 <<  5)
#define	BNA_CQ_EF_L3_CKSUM_OK	(1 <<  6)
#define	BNA_CQ_EF_HDS_HEADER	(1 <<  7)

#define	BNA_CQ_EF_UDP		(1 <<  8)
#define	BNA_CQ_EF_TCP		(1 <<  9)
#define	BNA_CQ_EF_IP_OPTIONS	(1 << 10)
#define	BNA_CQ_EF_IPV6		(1 << 11)

#define	BNA_CQ_EF_IPV4		(1 << 12)
#define	BNA_CQ_EF_VLAN		(1 << 13)
#define	BNA_CQ_EF_RSS		(1 << 14)
#define	BNA_CQ_EF_RSVD2		(1 << 15)

#define	BNA_CQ_EF_MCAST_MATCH   (1 << 16)
#define	BNA_CQ_EF_MCAST		(1 << 17)
#define BNA_CQ_EF_BCAST		(1 << 18)
#define	BNA_CQ_EF_REMOTE	(1 << 19)

#define	BNA_CQ_EF_LOCAL		(1 << 20)

/**
 *
 * Data structures
 *
 */

enum txf_flags {
	BFI_TXF_CF_ENABLE		= 1 << 0,
	BFI_TXF_CF_VLAN_FILTER		= 1 << 8,
	BFI_TXF_CF_VLAN_ADMIT		= 1 << 9,
	BFI_TXF_CF_VLAN_INSERT		= 1 << 10,
	BFI_TXF_CF_RSVD1		= 1 << 11,
	BFI_TXF_CF_MAC_SA_CHECK		= 1 << 12,
	BFI_TXF_CF_VLAN_WI_BASED	= 1 << 13,
	BFI_TXF_CF_VSWITCH_MCAST	= 1 << 14,
	BFI_TXF_CF_VSWITCH_UCAST	= 1 << 15,
	BFI_TXF_CF_RSVD2		= 0x7F << 1
};

enum ib_flags {
	BFI_IB_CF_MASTER_ENABLE		= (1 << 0),
	BFI_IB_CF_MSIX_MODE		= (1 << 1),
	BFI_IB_CF_COALESCING_MODE	= (1 << 2),
	BFI_IB_CF_INTER_PKT_ENABLE	= (1 << 3),
	BFI_IB_CF_INT_ENABLE		= (1 << 4),
	BFI_IB_CF_INTER_PKT_DMA		= (1 << 5),
	BFI_IB_CF_ACK_PENDING		= (1 << 6),
	BFI_IB_CF_RESERVED1		= (1 << 7)
};

enum rss_hash_type {
	BFI_RSS_T_V4_TCP		= (1 << 11),
	BFI_RSS_T_V4_IP			= (1 << 10),
	BFI_RSS_T_V6_TCP		= (1 <<  9),
	BFI_RSS_T_V6_IP			= (1 <<  8)
};
enum hds_header_type {
	BNA_HDS_T_V4_TCP	= (1 << 11),
	BNA_HDS_T_V4_UDP	= (1 << 10),
	BNA_HDS_T_V6_TCP	= (1 << 9),
	BNA_HDS_T_V6_UDP	= (1 << 8),
	BNA_HDS_FORCED		= (1 << 7),
};
enum rxf_flags {
	BNA_RXF_CF_SM_LG_RXQ			= (1 << 15),
	BNA_RXF_CF_DEFAULT_VLAN			= (1 << 14),
	BNA_RXF_CF_DEFAULT_FUNCTION_ENABLE	= (1 << 13),
	BNA_RXF_CF_VLAN_STRIP			= (1 << 12),
	BNA_RXF_CF_RSS_ENABLE			= (1 <<  8)
};
struct bna_chip_regs_offset {
	u32 page_addr;
	u32 fn_int_status;
	u32 fn_int_mask;
	u32 msix_idx;
};

struct bna_chip_regs {
	void __iomem *page_addr;
	void __iomem *fn_int_status;
	void __iomem *fn_int_mask;
};

struct bna_txq_mem {
	u32 pg_tbl_addr_lo;
	u32 pg_tbl_addr_hi;
	u32 cur_q_entry_lo;
	u32 cur_q_entry_hi;
	u32 reserved1;
	u32 reserved2;
	u32 pg_cnt_n_prd_ptr;	/* 31:16->total page count */
					/* 15:0 ->producer pointer (index?) */
	u32 entry_n_pg_size;	/* 31:16->entry size */
					/* 15:0 ->page size */
	u32 int_blk_n_cns_ptr;	/* 31:24->Int Blk Id;  */
					/* 23:16->Int Blk Offset */
					/* 15:0 ->consumer pointer(index?) */
	u32 cns_ptr2_n_q_state;	/* 31:16->cons. ptr 2; 15:0-> Q state */
	u32 nxt_qid_n_fid_n_pri;	/* 17:10->next */
					/* QId;9:3->FID;2:0->Priority */
	u32 wvc_n_cquota_n_rquota; /* 31:24->WI Vector Count; */
					/* 23:12->Cfg Quota; */
					/* 11:0 ->Run Quota */
	u32 reserved3[4];
};

struct bna_rxq_mem {
	u32 pg_tbl_addr_lo;
	u32 pg_tbl_addr_hi;
	u32 cur_q_entry_lo;
	u32 cur_q_entry_hi;
	u32 reserved1;
	u32 reserved2;
	u32 pg_cnt_n_prd_ptr;	/* 31:16->total page count */
					/* 15:0 ->producer pointer (index?) */
	u32 entry_n_pg_size;	/* 31:16->entry size */
					/* 15:0 ->page size */
	u32 sg_n_cq_n_cns_ptr;	/* 31:28->reserved; 27:24->sg count */
					/* 23:16->CQ; */
					/* 15:0->consumer pointer(index?) */
	u32 buf_sz_n_q_state;	/* 31:16->buffer size; 15:0-> Q state */
	u32 next_qid;		/* 17:10->next QId */
	u32 reserved3;
	u32 reserved4[4];
};

struct bna_rxtx_q_mem {
	struct bna_rxq_mem rxq;
	struct bna_txq_mem txq;
};

struct bna_cq_mem {
	u32 pg_tbl_addr_lo;
	u32 pg_tbl_addr_hi;
	u32 cur_q_entry_lo;
	u32 cur_q_entry_hi;

	u32 reserved1;
	u32 reserved2;
	u32 pg_cnt_n_prd_ptr;	/* 31:16->total page count */
					/* 15:0 ->producer pointer (index?) */
	u32 entry_n_pg_size;	/* 31:16->entry size */
					/* 15:0 ->page size */
	u32 int_blk_n_cns_ptr;	/* 31:24->Int Blk Id; */
					/* 23:16->Int Blk Offset */
					/* 15:0 ->consumer pointer(index?) */
	u32 q_state;		/* 31:16->reserved; 15:0-> Q state */
	u32 reserved3[2];
	u32 reserved4[4];
};

struct bna_ib_blk_mem {
	u32 host_addr_lo;
	u32 host_addr_hi;
	u32 clsc_n_ctrl_n_msix;	/* 31:24->coalescing; */
					/* 23:16->coalescing cfg; */
					/* 15:8 ->control; */
					/* 7:0 ->msix; */
	u32 ipkt_n_ent_n_idxof;
	u32 ipkt_cnt_cfg_n_unacked;

	u32 reserved[3];
};

struct bna_idx_tbl_mem {
	u32 idx;	  /* !< 31:16->res;15:0->idx; */
};

struct bna_doorbell_qset {
	u32 rxq[0x20 >> 2];
	u32 txq[0x20 >> 2];
	u32 ib0[0x20 >> 2];
	u32 ib1[0x20 >> 2];
};

struct bna_rx_fndb_ram {
	u32 rss_prop;
	u32 size_routing_props;
	u32 rit_hds_mcastq;
	u32 control_flags;
};

struct bna_tx_fndb_ram {
	u32 vlan_n_ctrl_flags;
};

/**
 * @brief
 *  Structure which maps to RxFn Indirection Table (RIT)
 *  Size : 1 word
 *  See catapult_spec.pdf, RxA for details
 */
struct bna_rit_mem {
	u32 rxq_ids;	/* !< 31:12->res;11:0->two 6 bit RxQ Ids */
};

/**
 * @brief
 *  Structure which maps to RSS Table entry
 *  Size : 16 words
 *  See catapult_spec.pdf, RAD for details
 */
struct bna_rss_mem {
	/*
	 * 31:12-> res
	 * 11:8 -> protocol type
	 *  7:0 -> hash index
	 */
	u32 type_n_hash;
	u32 hash_key[10];  /* !< 40 byte Toeplitz hash key */
	u32 reserved[5];
};

/* TxQ Vector (a.k.a. Tx-Buffer Descriptor) */
struct bna_dma_addr {
	u32		msb;
	u32		lsb;
};

struct bna_txq_wi_vector {
	u16		reserved;
	u16		length;		/* Only 14 LSB are valid */
	struct bna_dma_addr host_addr; /* Tx-Buf DMA addr */
};

typedef u16 bna_txq_wi_opcode_t;

typedef u16 bna_txq_wi_ctrl_flag_t;

/**
 *  TxQ Entry Structure
 *
 *  BEWARE:  Load values into this structure with correct endianess.
 */
struct bna_txq_entry {
	union {
		struct {
			u8 reserved;
			u8 num_vectors;	/* number of vectors present */
			bna_txq_wi_opcode_t opcode; /* Either */
						    /* BNA_TXQ_WI_SEND or */
						    /* BNA_TXQ_WI_SEND_LSO */
			bna_txq_wi_ctrl_flag_t flags; /* OR of all the flags */
			u16 l4_hdr_size_n_offset;
			u16 vlan_tag;
			u16 lso_mss;	/* Only 14 LSB are valid */
			u32 frame_length;	/* Only 24 LSB are valid */
		} wi;

		struct {
			u16 reserved;
			bna_txq_wi_opcode_t opcode; /* Must be */
						    /* BNA_TXQ_WI_EXTENSION */
			u32 reserved2[3];	/* Place holder for */
						/* removed vector (12 bytes) */
		} wi_ext;
	} hdr;
	struct bna_txq_wi_vector vector[4];
};
#define wi_hdr		hdr.wi
#define wi_ext_hdr  hdr.wi_ext

/* RxQ Entry Structure */
struct bna_rxq_entry {		/* Rx-Buffer */
	struct bna_dma_addr host_addr; /* Rx-Buffer DMA address */
};

typedef u32 bna_cq_e_flag_t;

/* CQ Entry Structure */
struct bna_cq_entry {
	bna_cq_e_flag_t flags;
	u16 vlan_tag;
	u16 length;
	u32 rss_hash;
	u8 valid;
	u8 reserved1;
	u8 reserved2;
	u8 rxq_id;
};

#endif /* __BNA_HW_H__ */
