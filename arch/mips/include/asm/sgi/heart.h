/* SPDX-License-Identifier: GPL-2.0 */
/*
 * HEART chip definitions
 *
 * Copyright (C) 2004-2007 Stanislaw Skowronek <skylark@unaligned.org>
 *		 2009 Johannes Dickgreber <tanzy@gmx.de>
 *		 2007-2015 Joshua Kinard <kumba@gentoo.org>
 */
#ifndef __ASM_SGI_HEART_H
#define __ASM_SGI_HEART_H

#include <linux/types.h>
#include <linux/time.h>

/*
 * There are 8 DIMM slots on an IP30 system
 * board, which are grouped into four banks
 */
#define HEART_MEMORY_BANKS	4

/* HEART can support up to four CPUs */
#define HEART_MAX_CPUS		4

#define HEART_XKPHYS_BASE	((void *)(IO_BASE | 0x000000000ff00000ULL))

/**
 * struct ip30_heart_regs - struct that maps IP30 HEART registers.
 * @mode: HEART_MODE - Purpose Unknown, machine reset called from here.
 * @sdram_mode: HEART_SDRAM_MODE - purpose unknown.
 * @mem_refresh: HEART_MEM_REF - purpose unknown.
 * @mem_req_arb: HEART_MEM_REQ_ARB - purpose unknown.
 * @mem_cfg.q: union for 64bit access to HEART_MEMCFG - 4x 64bit registers.
 * @mem_cfg.l: union for 32bit access to HEART_MEMCFG - 8x 32bit registers.
 * @fc_mode: HEART_FC_MODE - Purpose Unknown, possibly for GFX flow control.
 * @fc_timer_limit: HEART_FC_TIMER_LIMIT - purpose unknown.
 * @fc_addr: HEART_FC0_ADDR, HEART_FC1_ADDR - purpose unknown.
 * @fc_credit_cnt: HEART_FC0_CR_CNT, HEART_FC1_CR_CNT - purpose unknown.
 * @fc_timer: HEART_FC0_TIMER, HEART_FC1_TIMER - purpose unknown.
 * @status: HEART_STATUS - HEART status information.
 * @bus_err_addr: HEART_BERR_ADDR - likely contains addr of recent SIGBUS.
 * @bus_err_misc: HEART_BERR_MISC - purpose unknown.
 * @mem_err_addr: HEART_MEMERR_ADDR - likely contains addr of recent mem err.
 * @mem_err_data: HEART_MEMERR_DATA - purpose unknown.
 * @piur_acc_err: HEART_PIUR_ACC_ERR - likely for access err to HEART regs.
 * @mlan_clock_div: HEART_MLAN_CLK_DIV - MicroLAN clock divider.
 * @mlan_ctrl: HEART_MLAN_CTL - MicroLAN control.
 * @__pad0: 0x0f40 bytes of padding -> next HEART register 0x01000.
 * @undefined: Undefined/diag register, write to it triggers PIUR_ACC_ERR.
 * @__pad1: 0xeff8 bytes of padding -> next HEART register 0x10000.
 * @imr: HEART_IMR0 to HEART_IMR3 - per-cpu interrupt mask register.
 * @set_isr: HEART_SET_ISR - set interrupt status register.
 * @clear_isr: HEART_CLR_ISR - clear interrupt status register.
 * @isr: HEART_ISR - interrupt status register (read-only).
 * @imsr: HEART_IMSR - purpose unknown.
 * @cause: HEART_CAUSE - HEART cause information.
 * @__pad2: 0xffb8 bytes of padding -> next HEART register 0x20000.
 * @count: HEART_COUNT - 52-bit counter.
 * @__pad3: 0xfff8 bytes of padding -> next HEART register 0x30000.
 * @compare: HEART_COMPARE - 24-bit compare.
 * @__pad4: 0xfff8 bytes of padding -> next HEART register 0x40000.
 * @trigger: HEART_TRIGGER - purpose unknown.
 * @__pad5: 0xfff8 bytes of padding -> next HEART register 0x50000.
 * @cpuid: HEART_PRID - contains CPU ID of CPU currently accessing HEART.
 * @__pad6: 0xfff8 bytes of padding -> next HEART register 0x60000.
 * @sync: HEART_SYNC - purpose unknown.
 *
 * HEART is the main system controller ASIC for IP30 system.  It incorporates
 * a memory controller, interrupt status/cause/set/clear management, basic
 * timer with count/compare, and other functionality.  For Linux, not all of
 * HEART's functions are fully understood.
 *
 * Implementation note: All HEART registers are 64bits-wide, but the mem_cfg
 * register only reports correct values if queried in 32bits.  Hence the need
 * for a union.  Even though mem_cfg.l has 8 array slots, we only ever query
 * up to 4 of those.  IP30 has 8 DIMM slots arranged into 4 banks, w/ 2 DIMMs
 * per bank.  Each 32bit read accesses one of these banks.  Perhaps HEART was
 * designed to address up to 8 banks (16 DIMMs)?  We may never know.
 */
struct ip30_heart_regs {		/* 0x0ff00000 */
	u64 mode;			/* +  0x00000 */
	/* Memory */
	u64 sdram_mode;			/* +  0x00008 */
	u64 mem_refresh;		/* +  0x00010 */
	u64 mem_req_arb;		/* +  0x00018 */
	union {
		u64 q[HEART_MEMORY_BANKS];	/* readq() */
		u32 l[HEART_MEMORY_BANKS * 2];	/* readl() */
	} mem_cfg;			/* +  0x00020 */
	/* Flow control (gfx?) */
	u64 fc_mode;			/* +  0x00040 */
	u64 fc_timer_limit;		/* +  0x00048 */
	u64 fc_addr[2];			/* +  0x00050 */
	u64 fc_credit_cnt[2];		/* +  0x00060 */
	u64 fc_timer[2];		/* +  0x00070 */
	/* Status */
	u64 status;			/* +  0x00080 */
	/* Bus error */
	u64 bus_err_addr;		/* +  0x00088 */
	u64 bus_err_misc;		/* +  0x00090 */
	/* Memory error */
	u64 mem_err_addr;		/* +  0x00098 */
	u64 mem_err_data;		/* +  0x000a0 */
	/* Misc */
	u64 piur_acc_err;		/* +  0x000a8 */
	u64 mlan_clock_div;		/* +  0x000b0 */
	u64 mlan_ctrl;			/* +  0x000b8 */
	u64 __pad0[0x01e8];		/* +  0x000c0 + 0x0f40 */
	/* Undefined */
	u64 undefined;			/* +  0x01000 */
	u64 __pad1[0x1dff];		/* +  0x01008 + 0xeff8 */
	/* Interrupts */
	u64 imr[HEART_MAX_CPUS];	/* +  0x10000 */
	u64 set_isr;			/* +  0x10020 */
	u64 clear_isr;			/* +  0x10028 */
	u64 isr;			/* +  0x10030 */
	u64 imsr;			/* +  0x10038 */
	u64 cause;			/* +  0x10040 */
	u64 __pad2[0x1ff7];		/* +  0x10048 + 0xffb8 */
	/* Timer */
	u64 count;			/* +  0x20000 */
	u64 __pad3[0x1fff];		/* +  0x20008 + 0xfff8 */
	u64 compare;			/* +  0x30000 */
	u64 __pad4[0x1fff];		/* +  0x30008 + 0xfff8 */
	u64 trigger;			/* +  0x40000 */
	u64 __pad5[0x1fff];		/* +  0x40008 + 0xfff8 */
	/* Misc */
	u64 cpuid;			/* +  0x50000 */
	u64 __pad6[0x1fff];		/* +  0x50008 + 0xfff8 */
	u64 sync;			/* +  0x60000 */
};


/* For timer-related bits. */
#define HEART_NS_PER_CYCLE	80
#define HEART_CYCLES_PER_SEC	(NSEC_PER_SEC / HEART_NS_PER_CYCLE)


/*
 * XXX: Everything below this comment will either go away or be cleaned
 *      up to fit in better with Linux.  A lot of the bit definitions for
 *      HEART were derived from IRIX's sys/RACER/heart.h header file.
 */

/* HEART Masks */
#define HEART_ATK_MASK		0x0007ffffffffffff	/* HEART attack mask */
#define HEART_ACK_ALL_MASK	0xffffffffffffffff	/* Ack everything */
#define HEART_CLR_ALL_MASK	0x0000000000000000	/* Clear all */
#define HEART_BR_ERR_MASK	0x7ff8000000000000	/* BRIDGE error mask */
#define HEART_CPU0_ERR_MASK	0x8ff8000000000000	/* CPU0 error mask */
#define HEART_CPU1_ERR_MASK	0x97f8000000000000	/* CPU1 error mask */
#define HEART_CPU2_ERR_MASK	0xa7f8000000000000	/* CPU2 error mask */
#define HEART_CPU3_ERR_MASK	0xc7f8000000000000	/* CPU3 error mask */
#define HEART_ERR_MASK		0x1ff			/* HEART error mask */
#define HEART_ERR_MASK_START	51			/* HEART error start */
#define HEART_ERR_MASK_END	63			/* HEART error end */

/* Bits in the HEART_MODE register. */
#define HM_PROC_DISABLE_SHFT		60
#define HM_PROC_DISABLE_MSK		(0xfUL << HM_PROC_DISABLE_SHFT)
#define HM_PROC_DISABLE(x)		(0x1UL << (x) + HM_PROC_DISABLE_SHFT)
#define HM_MAX_PSR			(0x7UL << 57)
#define HM_MAX_IOSR			(0x7UL << 54)
#define HM_MAX_PEND_IOSR		(0x7UL << 51)
#define HM_TRIG_SRC_SEL_MSK		(0x7UL << 48)
#define HM_TRIG_HEART_EXC		(0x0UL << 48)
#define HM_TRIG_REG_BIT			(0x1UL << 48)
#define HM_TRIG_SYSCLK			(0x2UL << 48)
#define HM_TRIG_MEMCLK_2X		(0x3UL << 48)
#define HM_TRIG_MEMCLK			(0x4UL << 48)
#define HM_TRIG_IOCLK			(0x5UL << 48)
#define HM_PIU_TEST_MODE		(0xfUL << 40)
#define HM_GP_FLAG_MSK			(0xfUL << 36)
#define HM_GP_FLAG(x)			BIT((x) + 36)
#define HM_MAX_PROC_HYST		(0xfUL << 32)
#define HM_LLP_WRST_AFTER_RST		BIT(28)
#define HM_LLP_LINK_RST			BIT(27)
#define HM_LLP_WARM_RST			BIT(26)
#define HM_COR_ECC_LCK			BIT(25)
#define HM_REDUCED_PWR			BIT(24)
#define HM_COLD_RST			BIT(23)
#define HM_SW_RST			BIT(22)
#define HM_MEM_FORCE_WR			BIT(21)
#define HM_DB_ERR_GEN			BIT(20)
#define HM_SB_ERR_GEN			BIT(19)
#define HM_CACHED_PIO_EN		BIT(18)
#define HM_CACHED_PROM_EN		BIT(17)
#define HM_PE_SYS_COR_ERE		BIT(16)
#define HM_GLOBAL_ECC_EN		BIT(15)
#define HM_IO_COH_EN			BIT(14)
#define HM_INT_EN			BIT(13)
#define HM_DATA_CHK_EN			BIT(12)
#define HM_REF_EN			BIT(11)
#define HM_BAD_SYSWR_ERE		BIT(10)
#define HM_BAD_SYSRD_ERE		BIT(9)
#define HM_SYSSTATE_ERE			BIT(8)
#define HM_SYSCMD_ERE			BIT(7)
#define HM_NCOR_SYS_ERE			BIT(6)
#define HM_COR_SYS_ERE			BIT(5)
#define HM_DATA_ELMNT_ERE		BIT(4)
#define HM_MEM_ADDR_PROC_ERE		BIT(3)
#define HM_MEM_ADDR_IO_ERE		BIT(2)
#define HM_NCOR_MEM_ERE			BIT(1)
#define HM_COR_MEM_ERE			BIT(0)

/* Bits in the HEART_MEM_REF register. */
#define HEART_MEMREF_REFS(x)		((0xfUL & (x)) << 16)
#define HEART_MEMREF_PERIOD(x)		((0xffffUL & (x)))
#define HEART_MEMREF_REFS_VAL		HEART_MEMREF_REFS(8)
#define HEART_MEMREF_PERIOD_VAL		HEART_MEMREF_PERIOD(0x4000)
#define HEART_MEMREF_VAL		(HEART_MEMREF_REFS_VAL | \
					 HEART_MEMREF_PERIOD_VAL)

/* Bits in the HEART_MEM_REQ_ARB register. */
#define HEART_MEMARB_IODIS		(1  << 20)
#define HEART_MEMARB_MAXPMWRQS		(15 << 16)
#define HEART_MEMARB_MAXPMRRQS		(15 << 12)
#define HEART_MEMARB_MAXPMRQS		(15 << 8)
#define HEART_MEMARB_MAXRRRQS		(15 << 4)
#define HEART_MEMARB_MAXGBRRQS		(15)

/* Bits in the HEART_MEMCFG<x> registers. */
#define HEART_MEMCFG_VALID		0x80000000	/* Bank is valid */
#define HEART_MEMCFG_DENSITY		0x01c00000	/* Mem density */
#define HEART_MEMCFG_SIZE_MASK		0x003f0000	/* Mem size mask */
#define HEART_MEMCFG_ADDR_MASK		0x000001ff	/* Base addr mask */
#define HEART_MEMCFG_SIZE_SHIFT		16		/* Mem size shift */
#define HEART_MEMCFG_DENSITY_SHIFT	22		/* Density Shift */
#define HEART_MEMCFG_UNIT_SHIFT		25		/* Unit Shift, 32MB */

/* Bits in the HEART_STATUS register */
#define HEART_STAT_HSTL_SDRV		BIT(14)
#define HEART_STAT_FC_CR_OUT(x)		BIT((x) + 12)
#define HEART_STAT_DIR_CNNCT		BIT(11)
#define HEART_STAT_TRITON		BIT(10)
#define HEART_STAT_R4K			BIT(9)
#define HEART_STAT_BIG_ENDIAN		BIT(8)
#define HEART_STAT_PROC_SHFT		4
#define HEART_STAT_PROC_MSK		(0xfUL << HEART_STAT_PROC_SHFT)
#define HEART_STAT_PROC_ACTIVE(x)	(0x1UL << ((x) + HEART_STAT_PROC_SHFT))
#define HEART_STAT_WIDGET_ID		0xf

/* Bits in the HEART_CAUSE register */
#define HC_PE_SYS_COR_ERR_MSK		(0xfUL << 60)
#define HC_PE_SYS_COR_ERR(x)		BIT((x) + 60)
#define HC_PIOWDB_OFLOW			BIT(44)
#define HC_PIORWRB_OFLOW		BIT(43)
#define HC_PIUR_ACC_ERR			BIT(42)
#define HC_BAD_SYSWR_ERR		BIT(41)
#define HC_BAD_SYSRD_ERR		BIT(40)
#define HC_SYSSTATE_ERR_MSK		(0xfUL << 36)
#define HC_SYSSTATE_ERR(x)		BIT((x) + 36)
#define HC_SYSCMD_ERR_MSK		(0xfUL << 32)
#define HC_SYSCMD_ERR(x)		BIT((x) + 32)
#define HC_NCOR_SYSAD_ERR_MSK		(0xfUL << 28)
#define HC_NCOR_SYSAD_ERR(x)		BIT((x) + 28)
#define HC_COR_SYSAD_ERR_MSK		(0xfUL << 24)
#define HC_COR_SYSAD_ERR(x)		BIT((x) + 24)
#define HC_DATA_ELMNT_ERR_MSK		(0xfUL << 20)
#define HC_DATA_ELMNT_ERR(x)		BIT((x) + 20)
#define HC_WIDGET_ERR			BIT(16)
#define HC_MEM_ADDR_ERR_PROC_MSK	(0xfUL << 4)
#define HC_MEM_ADDR_ERR_PROC(x)	BIT((x) + 4)
#define HC_MEM_ADDR_ERR_IO		BIT(2)
#define HC_NCOR_MEM_ERR			BIT(1)
#define HC_COR_MEM_ERR			BIT(0)

/*
 * HEART has 64 interrupt vectors available to it, subdivided into five
 * priority levels.  They are numbered 0 to 63.
 */
#define HEART_NUM_IRQS			64

/*
 * These are the five interrupt priority levels and their corresponding
 * CPU IPx interrupt pins.
 *
 * Level 4 - Error Interrupts.
 * Level 3 - HEART timer interrupt.
 * Level 2 - CPU IPI, CPU debug, power putton, general device interrupts.
 * Level 1 - General device interrupts.
 * Level 0 - General device GFX flow control interrupts.
 */
#define HEART_L4_INT_MASK		0xfff8000000000000ULL	/* IP6 */
#define HEART_L3_INT_MASK		0x0004000000000000ULL	/* IP5 */
#define HEART_L2_INT_MASK		0x0003ffff00000000ULL	/* IP4 */
#define HEART_L1_INT_MASK		0x00000000ffff0000ULL	/* IP3 */
#define HEART_L0_INT_MASK		0x000000000000ffffULL	/* IP2 */

/* HEART L0 Interrupts (Low Priority) */
#define HEART_L0_INT_GENERIC		 0
#define HEART_L0_INT_FLOW_CTRL_HWTR_0	 1
#define HEART_L0_INT_FLOW_CTRL_HWTR_1	 2

/* HEART L2 Interrupts (High Priority) */
#define HEART_L2_INT_RESCHED_CPU_0	46
#define HEART_L2_INT_RESCHED_CPU_1	47
#define HEART_L2_INT_CALL_CPU_0		48
#define HEART_L2_INT_CALL_CPU_1		49

/* HEART L3 Interrupts (Compare/Counter Timer) */
#define HEART_L3_INT_TIMER		50

/* HEART L4 Interrupts (Errors) */
#define HEART_L4_INT_XWID_ERR_9		51
#define HEART_L4_INT_XWID_ERR_A		52
#define HEART_L4_INT_XWID_ERR_B		53
#define HEART_L4_INT_XWID_ERR_C		54
#define HEART_L4_INT_XWID_ERR_D		55
#define HEART_L4_INT_XWID_ERR_E		56
#define HEART_L4_INT_XWID_ERR_F		57
#define HEART_L4_INT_XWID_ERR_XBOW	58
#define HEART_L4_INT_CPU_BUS_ERR_0	59
#define HEART_L4_INT_CPU_BUS_ERR_1	60
#define HEART_L4_INT_CPU_BUS_ERR_2	61
#define HEART_L4_INT_CPU_BUS_ERR_3	62
#define HEART_L4_INT_HEART_EXCP		63

extern struct ip30_heart_regs __iomem *heart_regs;

#define heart_read	____raw_readq
#define heart_write	____raw_writeq

#endif /* __ASM_SGI_HEART_H */
