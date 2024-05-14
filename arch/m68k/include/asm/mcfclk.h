/* SPDX-License-Identifier: GPL-2.0 */
/*
 * mcfclk.h -- coldfire specific clock structure
 */


#ifndef mcfclk_h
#define mcfclk_h

struct clk;

struct clk_ops {
	void (*enable)(struct clk *);
	void (*disable)(struct clk *);
};

struct clk {
	struct clk_ops *clk_ops;
	unsigned long rate;
	unsigned long enabled;
	u8 slot;
};

#ifdef MCFPM_PPMCR0
extern struct clk_ops clk_ops0;
#ifdef MCFPM_PPMCR1
extern struct clk_ops clk_ops1;
#endif /* MCFPM_PPMCR1 */

extern struct clk_ops clk_ops2;

#define DEFINE_CLK(clk_bank, clk_name, clk_slot, clk_rate) \
static struct clk __clk_##clk_bank##_##clk_slot = { \
	.clk_ops = &clk_ops##clk_bank, \
	.rate = clk_rate, \
	.slot = clk_slot, \
}

void __clk_init_enabled(struct clk *);
void __clk_init_disabled(struct clk *);
#else
#define DEFINE_CLK(clk_ref, clk_name, clk_rate) \
        static struct clk clk_##clk_ref = { \
                .rate = clk_rate, \
        }
#endif /* MCFPM_PPMCR0 */

#endif /* mcfclk_h */
