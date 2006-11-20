/*
 * cbe_regs.h
 *
 * This file is intended to hold the various register definitions for CBE
 * on-chip system devices (memory controller, IO controller, etc...)
 *
 * (C) Copyright IBM Corporation 2001,2006
 *
 * Authors: Maximino Aguilar (maguilar@us.ibm.com)
 *          David J. Erb (djerb@us.ibm.com)
 *
 * (c) 2006 Benjamin Herrenschmidt <benh@kernel.crashing.org>, IBM Corp.
 */

#ifndef CBE_REGS_H
#define CBE_REGS_H

#include <asm/cell-pmu.h>

/*
 *
 * Some HID register definitions
 *
 */

/* CBE specific HID0 bits */
#define HID0_CBE_THERM_WAKEUP	0x0000020000000000ul
#define HID0_CBE_SYSERR_WAKEUP	0x0000008000000000ul
#define HID0_CBE_THERM_INT_EN	0x0000000400000000ul
#define HID0_CBE_SYSERR_INT_EN	0x0000000200000000ul

#define MAX_CBE		2

/*
 *
 * Pervasive unit register definitions
 *
 */

union spe_reg {
	u64 val;
	u8 spe[8];
};

union ppe_spe_reg {
	u64 val;
	struct {
		u32 ppe;
		u32 spe;
	};
};


struct cbe_pmd_regs {
	/* Debug Bus Control */
	u64	pad_0x0000;					/* 0x0000 */

	u64	group_control;					/* 0x0008 */

	u8	pad_0x0010_0x00a8 [0x00a8 - 0x0010];		/* 0x0010 */

	u64	debug_bus_control;				/* 0x00a8 */

	u8	pad_0x00b0_0x0100 [0x0100 - 0x00b0];		/* 0x00b0 */

	u64	trace_aux_data;					/* 0x0100 */
	u64	trace_buffer_0_63;				/* 0x0108 */
	u64	trace_buffer_64_127;				/* 0x0110 */
	u64	trace_address;					/* 0x0118 */
	u64	ext_tr_timer;					/* 0x0120 */

	u8	pad_0x0128_0x0400 [0x0400 - 0x0128];		/* 0x0128 */

	/* Performance Monitor */
	u64	pm_status;					/* 0x0400 */
	u64	pm_control;					/* 0x0408 */
	u64	pm_interval;					/* 0x0410 */
	u64	pm_ctr[4];					/* 0x0418 */
	u64	pm_start_stop;					/* 0x0438 */
	u64	pm07_control[8];				/* 0x0440 */

	u8	pad_0x0480_0x0800 [0x0800 - 0x0480];		/* 0x0480 */

	/* Thermal Sensor Registers */
	union	spe_reg	ts_ctsr1;				/* 0x0800 */
	u64	ts_ctsr2;					/* 0x0808 */
	union	spe_reg	ts_mtsr1;				/* 0x0810 */
	u64	ts_mtsr2;					/* 0x0818 */
	union	spe_reg	ts_itr1;				/* 0x0820 */
	u64	ts_itr2;					/* 0x0828 */
	u64	ts_gitr;					/* 0x0830 */
	u64	ts_isr;						/* 0x0838 */
	u64	ts_imr;						/* 0x0840 */
	union	spe_reg	tm_cr1;					/* 0x0848 */
	u64	tm_cr2;						/* 0x0850 */
	u64	tm_simr;					/* 0x0858 */
	union	ppe_spe_reg tm_tpr;				/* 0x0860 */
	union	spe_reg	tm_str1;				/* 0x0868 */
	u64	tm_str2;					/* 0x0870 */
	union	ppe_spe_reg tm_tsr;				/* 0x0878 */

	/* Power Management */
	u64	pmcr;						/* 0x0880 */
#define CBE_PMD_PAUSE_ZERO_CONTROL	0x10000
	u64	pmsr;						/* 0x0888 */

	/* Time Base Register */
	u64	tbr;						/* 0x0890 */

	u8	pad_0x0898_0x0c00 [0x0c00 - 0x0898];		/* 0x0898 */

	/* Fault Isolation Registers */
	u64	checkstop_fir;					/* 0x0c00 */
	u64	recoverable_fir;				/* 0x0c08 */
	u64	spec_att_mchk_fir;				/* 0x0c10 */
	u64	fir_mode_reg;					/* 0x0c18 */
	u64	fir_enable_mask;				/* 0x0c20 */

	u8	pad_0x0c28_0x1000 [0x1000 - 0x0c28];		/* 0x0c28 */
};

extern struct cbe_pmd_regs __iomem *cbe_get_pmd_regs(struct device_node *np);
extern struct cbe_pmd_regs __iomem *cbe_get_cpu_pmd_regs(int cpu);

/*
 * PMU shadow registers
 *
 * Many of the registers in the performance monitoring unit are write-only,
 * so we need to save a copy of what we write to those registers.
 *
 * The actual data counters are read/write. However, writing to the counters
 * only takes effect if the PMU is enabled. Otherwise the value is stored in
 * a hardware latch until the next time the PMU is enabled. So we save a copy
 * of the counter values if we need to read them back while the PMU is
 * disabled. The counter_value_in_latch field is a bitmap indicating which
 * counters currently have a value waiting to be written.
 */

struct cbe_pmd_shadow_regs {
	u32 group_control;
	u32 debug_bus_control;
	u32 trace_address;
	u32 ext_tr_timer;
	u32 pm_status;
	u32 pm_control;
	u32 pm_interval;
	u32 pm_start_stop;
	u32 pm07_control[NR_CTRS];

	u32 pm_ctr[NR_PHYS_CTRS];
	u32 counter_value_in_latch;
};

extern struct cbe_pmd_shadow_regs *cbe_get_pmd_shadow_regs(struct device_node *np);
extern struct cbe_pmd_shadow_regs *cbe_get_cpu_pmd_shadow_regs(int cpu);

/*
 *
 * IIC unit register definitions
 *
 */

struct cbe_iic_pending_bits {
	u32 data;
	u8 flags;
	u8 class;
	u8 source;
	u8 prio;
};

#define CBE_IIC_IRQ_VALID	0x80
#define CBE_IIC_IRQ_IPI		0x40

struct cbe_iic_thread_regs {
	struct cbe_iic_pending_bits pending;
	struct cbe_iic_pending_bits pending_destr;
	u64 generate;
	u64 prio;
};

struct cbe_iic_regs {
	u8	pad_0x0000_0x0400[0x0400 - 0x0000];		/* 0x0000 */

	/* IIC interrupt registers */
	struct	cbe_iic_thread_regs thread[2];			/* 0x0400 */

	u64	iic_ir;						/* 0x0440 */
#define CBE_IIC_IR_PRIO(x)      (((x) & 0xf) << 12)
#define CBE_IIC_IR_DEST_NODE(x) (((x) & 0xf) << 4)
#define CBE_IIC_IR_DEST_UNIT(x) ((x) & 0xf)
#define CBE_IIC_IR_IOC_0        0x0
#define CBE_IIC_IR_IOC_1S       0xb
#define CBE_IIC_IR_PT_0         0xe
#define CBE_IIC_IR_PT_1         0xf

	u64	iic_is;						/* 0x0448 */
#define CBE_IIC_IS_PMI		0x2

	u8	pad_0x0450_0x0500[0x0500 - 0x0450];		/* 0x0450 */

	/* IOC FIR */
	u64	ioc_fir_reset;					/* 0x0500 */
	u64	ioc_fir_set;					/* 0x0508 */
	u64	ioc_checkstop_enable;				/* 0x0510 */
	u64	ioc_fir_error_mask;				/* 0x0518 */
	u64	ioc_syserr_enable;				/* 0x0520 */
	u64	ioc_fir;					/* 0x0528 */

	u8	pad_0x0530_0x1000[0x1000 - 0x0530];		/* 0x0530 */
};

extern struct cbe_iic_regs __iomem *cbe_get_iic_regs(struct device_node *np);
extern struct cbe_iic_regs __iomem *cbe_get_cpu_iic_regs(int cpu);


struct cbe_mic_tm_regs {
	u8	pad_0x0000_0x0040[0x0040 - 0x0000];		/* 0x0000 */

	u64	mic_ctl_cnfg2;					/* 0x0040 */
#define CBE_MIC_ENABLE_AUX_TRC		0x8000000000000000LL
#define CBE_MIC_DISABLE_PWR_SAV_2	0x0200000000000000LL
#define CBE_MIC_DISABLE_AUX_TRC_WRAP	0x0100000000000000LL
#define CBE_MIC_ENABLE_AUX_TRC_INT	0x0080000000000000LL

	u64	pad_0x0048;					/* 0x0048 */

	u64	mic_aux_trc_base;				/* 0x0050 */
	u64	mic_aux_trc_max_addr;				/* 0x0058 */
	u64	mic_aux_trc_cur_addr;				/* 0x0060 */
	u64	mic_aux_trc_grf_addr;				/* 0x0068 */
	u64	mic_aux_trc_grf_data;				/* 0x0070 */

	u64	pad_0x0078;					/* 0x0078 */

	u64	mic_ctl_cnfg_0;					/* 0x0080 */
#define CBE_MIC_DISABLE_PWR_SAV_0	0x8000000000000000LL

	u64	pad_0x0088;					/* 0x0088 */

	u64	slow_fast_timer_0;				/* 0x0090 */
	u64	slow_next_timer_0;				/* 0x0098 */

	u8	pad_0x00a0_0x01c0[0x01c0 - 0x0a0];		/* 0x00a0 */

	u64	mic_ctl_cnfg_1;					/* 0x01c0 */
#define CBE_MIC_DISABLE_PWR_SAV_1	0x8000000000000000LL
	u64	pad_0x01c8;					/* 0x01c8 */

	u64	slow_fast_timer_1;				/* 0x01d0 */
	u64	slow_next_timer_1;				/* 0x01d8 */

	u8	pad_0x01e0_0x1000[0x1000 - 0x01e0];		/* 0x01e0 */
};

extern struct cbe_mic_tm_regs __iomem *cbe_get_mic_tm_regs(struct device_node *np);
extern struct cbe_mic_tm_regs __iomem *cbe_get_cpu_mic_tm_regs(int cpu);

/* Init this module early */
extern void cbe_regs_init(void);


#endif /* CBE_REGS_H */
