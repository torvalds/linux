/*
 * cbe_regs.h
 *
 * This file is intended to hold the various register definitions for CBE
 * on-chip system devices (memory controller, IO controller, etc...)
 *
 * (c) 2006 Benjamin Herrenschmidt <benh@kernel.crashing.org>, IBM Corp.
 */

#ifndef CBE_REGS_H
#define CBE_REGS_H

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


/*
 *
 * Pervasive unit register definitions
 *
 */

struct cbe_pmd_regs {
	u8 pad_0x0000_0x0800[0x0800 - 0x0000];			/* 0x0000 */

	/* Thermal Sensor Registers */
	u64  ts_ctsr1;						/* 0x0800 */
	u64  ts_ctsr2;						/* 0x0808 */
	u64  ts_mtsr1;						/* 0x0810 */
	u64  ts_mtsr2;						/* 0x0818 */
	u64  ts_itr1;						/* 0x0820 */
	u64  ts_itr2;						/* 0x0828 */
	u64  ts_gitr;						/* 0x0830 */
	u64  ts_isr;						/* 0x0838 */
	u64  ts_imr;						/* 0x0840 */
	u64  tm_cr1;						/* 0x0848 */
	u64  tm_cr2;						/* 0x0850 */
	u64  tm_simr;						/* 0x0858 */
	u64  tm_tpr;						/* 0x0860 */
	u64  tm_str1;						/* 0x0868 */
	u64  tm_str2;						/* 0x0870 */
	u64  tm_tsr;						/* 0x0878 */

	/* Power Management */
	u64  pm_control;					/* 0x0880 */
#define CBE_PMD_PAUSE_ZERO_CONTROL		0x10000
	u64  pm_status;						/* 0x0888 */

	/* Time Base Register */
	u64  tbr;						/* 0x0890 */

	u8   pad_0x0898_0x0c00 [0x0c00 - 0x0898];		/* 0x0898 */

	/* Fault Isolation Registers */
	u64  checkstop_fir;					/* 0x0c00 */
	u64  recoverable_fir;
	u64  spec_att_mchk_fir;
	u64  fir_mode_reg;
	u64  fir_enable_mask;

	u8   pad_0x0c28_0x1000 [0x1000 - 0x0c28];		/* 0x0c28 */
};

extern struct cbe_pmd_regs __iomem *cbe_get_pmd_regs(struct device_node *np);
extern struct cbe_pmd_regs __iomem *cbe_get_cpu_pmd_regs(int cpu);

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
	u64     iic_ir;						/* 0x0440 */
	u64     iic_is;						/* 0x0448 */

	u8	pad_0x0450_0x0500[0x0500 - 0x0450];		/* 0x0450 */

	/* IOC FIR */
	u64	ioc_fir_reset;					/* 0x0500 */
	u64	ioc_fir_set;
	u64	ioc_checkstop_enable;
	u64	ioc_fir_error_mask;
	u64	ioc_syserr_enable;
	u64	ioc_fir;

	u8	pad_0x0530_0x1000[0x1000 - 0x0530];		/* 0x0530 */
};

extern struct cbe_iic_regs __iomem *cbe_get_iic_regs(struct device_node *np);
extern struct cbe_iic_regs __iomem *cbe_get_cpu_iic_regs(int cpu);


/* Init this module early */
extern void cbe_regs_init(void);


#endif /* CBE_REGS_H */
