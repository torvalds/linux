/*
 * Low level clock header file for Telechips TCC architecture
 * (C) 2010 Hans J. Koch <hjk@linutronix.de>
 *
 * Licensed under the GPL v2.
 */

#ifndef __ASM_ARCH_TCC_CLOCK_H__
#define __ASM_ARCH_TCC_CLOCK_H__

#ifndef __ASSEMBLY__

struct clk {
	struct clk *parent;
	/* id number of a root clock, 0 for normal clocks */
	int root_id;
	/* Reference count of clock enable/disable */
	int refcount;
	/* Address of associated BCLKCTRx register. Must be set. */
	void __iomem *bclkctr;
	/* Bit position for BCLKCTRx. Must be set. */
	int bclk_shift;
	/* Address of ACLKxxx register, if any. */
	void __iomem *aclkreg;
	/* get the current clock rate (always a fresh value) */
	unsigned long (*get_rate) (struct clk *);
	/* Function ptr to set the clock to a new rate. The rate must match a
	   supported rate returned from round_rate. Leave blank if clock is not
	   programmable */
	int (*set_rate) (struct clk *, unsigned long);
	/* Function ptr to round the requested clock rate to the nearest
	   supported rate that is less than or equal to the requested rate. */
	unsigned long (*round_rate) (struct clk *, unsigned long);
	/* Function ptr to enable the clock. Leave blank if clock can not
	   be gated. */
	int (*enable) (struct clk *);
	/* Function ptr to disable the clock. Leave blank if clock can not
	   be gated. */
	void (*disable) (struct clk *);
	/* Function ptr to set the parent clock of the clock. */
	int (*set_parent) (struct clk *, struct clk *);
};

int clk_register(struct clk *clk);
void clk_unregister(struct clk *clk);

#endif /* __ASSEMBLY__ */
#endif /* __ASM_ARCH_MXC_CLOCK_H__ */
