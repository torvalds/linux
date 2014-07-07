/*
 * Copyright (C) 2014 STMicroelectronics R&D Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

/*
 * Authors:
 * Stephen Gallimore <stephen.gallimore@st.com>,
 * Pankaj Dev <pankaj.dev@st.com>.
 */

#include <linux/slab.h>
#include <linux/of_address.h>
#include <linux/clk-provider.h>

#include "clkgen.h"

/*
 * Maximum input clock to the PLL before we divide it down by 2
 * although in reality in actual systems this has never been seen to
 * be used.
 */
#define QUADFS_NDIV_THRESHOLD 30000000

#define PLL_BW_GOODREF   (0L)
#define PLL_BW_VBADREF   (1L)
#define PLL_BW_BADREF    (2L)
#define PLL_BW_VGOODREF  (3L)

#define QUADFS_MAX_CHAN 4

struct stm_fs {
	unsigned long ndiv;
	unsigned long mdiv;
	unsigned long pe;
	unsigned long sdiv;
	unsigned long nsdiv;
};

static struct stm_fs fs216c65_rtbl[] = {
	{ .mdiv = 0x1f, .pe = 0x0,	.sdiv = 0x7,	.nsdiv = 0 },	/* 312.5 Khz */
	{ .mdiv = 0x17, .pe = 0x25ed,	.sdiv = 0x1,	.nsdiv = 0 },	/* 27    MHz */
	{ .mdiv = 0x1a, .pe = 0x7b36,	.sdiv = 0x2,	.nsdiv = 1 },	/* 36.87 MHz */
	{ .mdiv = 0x13, .pe = 0x0,	.sdiv = 0x2,	.nsdiv = 1 },	/* 48    MHz */
	{ .mdiv = 0x11, .pe = 0x1c72,	.sdiv = 0x1,	.nsdiv = 1 },	/* 108   MHz */
};

static struct stm_fs fs432c65_rtbl[] = {
	{ .mdiv = 0x1f, .pe = 0x0,	.sdiv = 0x7,	.nsdiv = 0 },	/* 625   Khz */
	{ .mdiv = 0x11, .pe = 0x1c72,	.sdiv = 0x2,	.nsdiv = 1 },	/* 108   MHz */
	{ .mdiv = 0x19, .pe = 0x121a,	.sdiv = 0x0,	.nsdiv = 1 },	/* 297   MHz */
};

static struct stm_fs fs660c32_rtbl[] = {
	{ .mdiv = 0x01, .pe = 0x2aaa,	.sdiv = 0x8,	.nsdiv = 0 },	/* 600   KHz */
	{ .mdiv = 0x02, .pe = 0x3d33,	.sdiv = 0x0,	.nsdiv = 0 },	/* 148.5 Mhz */
	{ .mdiv = 0x13, .pe = 0x5bcc,	.sdiv = 0x0,	.nsdiv = 1 },	/* 297   Mhz */
	{ .mdiv = 0x0e, .pe = 0x1025,	.sdiv = 0x0,	.nsdiv = 1 },	/* 333   Mhz */
	{ .mdiv = 0x0b, .pe = 0x715f,	.sdiv = 0x0,	.nsdiv = 1 },	/* 350   Mhz */
};

struct clkgen_quadfs_data {
	bool reset_present;
	bool bwfilter_present;
	bool lockstatus_present;
	bool nsdiv_present;
	struct clkgen_field ndiv;
	struct clkgen_field ref_bw;
	struct clkgen_field nreset;
	struct clkgen_field npda;
	struct clkgen_field lock_status;

	struct clkgen_field nsb[QUADFS_MAX_CHAN];
	struct clkgen_field en[QUADFS_MAX_CHAN];
	struct clkgen_field mdiv[QUADFS_MAX_CHAN];
	struct clkgen_field pe[QUADFS_MAX_CHAN];
	struct clkgen_field sdiv[QUADFS_MAX_CHAN];
	struct clkgen_field nsdiv[QUADFS_MAX_CHAN];

	const struct clk_ops *pll_ops;
	struct stm_fs *rtbl;
	u8 rtbl_cnt;
	int  (*get_rate)(unsigned long , struct stm_fs *,
			unsigned long *);
};

static const struct clk_ops st_quadfs_pll_c65_ops;
static const struct clk_ops st_quadfs_pll_c32_ops;
static const struct clk_ops st_quadfs_fs216c65_ops;
static const struct clk_ops st_quadfs_fs432c65_ops;
static const struct clk_ops st_quadfs_fs660c32_ops;

static int clk_fs216c65_get_rate(unsigned long, struct stm_fs *,
		unsigned long *);
static int clk_fs432c65_get_rate(unsigned long, struct stm_fs *,
		unsigned long *);
static int clk_fs660c32_dig_get_rate(unsigned long, struct stm_fs *,
		unsigned long *);
/*
 * Values for all of the standalone instances of this clock
 * generator found in STiH415 and STiH416 SYSCFG register banks. Note
 * that the individual channel standby control bits (nsb) are in the
 * first register along with the PLL control bits.
 */
static struct clkgen_quadfs_data st_fs216c65_416 = {
	/* 416 specific */
	.npda	= CLKGEN_FIELD(0x0, 0x1, 14),
	.nsb	= { CLKGEN_FIELD(0x0, 0x1, 10),
		    CLKGEN_FIELD(0x0, 0x1, 11),
		    CLKGEN_FIELD(0x0, 0x1, 12),
		    CLKGEN_FIELD(0x0, 0x1, 13) },
	.nsdiv_present = true,
	.nsdiv	= { CLKGEN_FIELD(0x0, 0x1, 18),
		    CLKGEN_FIELD(0x0, 0x1, 19),
		    CLKGEN_FIELD(0x0, 0x1, 20),
		    CLKGEN_FIELD(0x0, 0x1, 21) },
	.mdiv	= { CLKGEN_FIELD(0x4, 0x1f, 0),
		    CLKGEN_FIELD(0x14, 0x1f, 0),
		    CLKGEN_FIELD(0x24, 0x1f, 0),
		    CLKGEN_FIELD(0x34, 0x1f, 0) },
	.en	= { CLKGEN_FIELD(0x10, 0x1, 0),
		    CLKGEN_FIELD(0x20, 0x1, 0),
		    CLKGEN_FIELD(0x30, 0x1, 0),
		    CLKGEN_FIELD(0x40, 0x1, 0) },
	.ndiv	= CLKGEN_FIELD(0x0, 0x1, 15),
	.bwfilter_present = true,
	.ref_bw = CLKGEN_FIELD(0x0, 0x3, 16),
	.pe	= { CLKGEN_FIELD(0x8, 0xffff, 0),
		    CLKGEN_FIELD(0x18, 0xffff, 0),
		    CLKGEN_FIELD(0x28, 0xffff, 0),
		    CLKGEN_FIELD(0x38, 0xffff, 0) },
	.sdiv	= { CLKGEN_FIELD(0xC, 0x7, 0),
		    CLKGEN_FIELD(0x1C, 0x7, 0),
		    CLKGEN_FIELD(0x2C, 0x7, 0),
		    CLKGEN_FIELD(0x3C, 0x7, 0) },
	.pll_ops	= &st_quadfs_pll_c65_ops,
	.rtbl		= fs216c65_rtbl,
	.rtbl_cnt	= ARRAY_SIZE(fs216c65_rtbl),
	.get_rate	= clk_fs216c65_get_rate,
};

static struct clkgen_quadfs_data st_fs432c65_416 = {
	.npda	= CLKGEN_FIELD(0x0, 0x1, 14),
	.nsb	= { CLKGEN_FIELD(0x0, 0x1, 10),
		    CLKGEN_FIELD(0x0, 0x1, 11),
		    CLKGEN_FIELD(0x0, 0x1, 12),
		    CLKGEN_FIELD(0x0, 0x1, 13) },
	.nsdiv_present = true,
	.nsdiv	= { CLKGEN_FIELD(0x0, 0x1, 18),
		   CLKGEN_FIELD(0x0, 0x1, 19),
		   CLKGEN_FIELD(0x0, 0x1, 20),
		   CLKGEN_FIELD(0x0, 0x1, 21) },
	.mdiv	= { CLKGEN_FIELD(0x4, 0x1f, 0),
		    CLKGEN_FIELD(0x14, 0x1f, 0),
		    CLKGEN_FIELD(0x24, 0x1f, 0),
		    CLKGEN_FIELD(0x34, 0x1f, 0) },
	.en	= { CLKGEN_FIELD(0x10, 0x1, 0),
		    CLKGEN_FIELD(0x20, 0x1, 0),
		    CLKGEN_FIELD(0x30, 0x1, 0),
		    CLKGEN_FIELD(0x40, 0x1, 0) },
	.ndiv	= CLKGEN_FIELD(0x0, 0x1, 15),
	.bwfilter_present = true,
	.ref_bw = CLKGEN_FIELD(0x0, 0x3, 16),
	.pe	= { CLKGEN_FIELD(0x8, 0xffff, 0),
		    CLKGEN_FIELD(0x18, 0xffff, 0),
		    CLKGEN_FIELD(0x28, 0xffff, 0),
		    CLKGEN_FIELD(0x38, 0xffff, 0) },
	.sdiv	= { CLKGEN_FIELD(0xC, 0x7, 0),
		    CLKGEN_FIELD(0x1C, 0x7, 0),
		    CLKGEN_FIELD(0x2C, 0x7, 0),
		    CLKGEN_FIELD(0x3C, 0x7, 0) },
	.pll_ops	= &st_quadfs_pll_c65_ops,
	.rtbl		= fs432c65_rtbl,
	.rtbl_cnt	= ARRAY_SIZE(fs432c65_rtbl),
	.get_rate	= clk_fs432c65_get_rate,
};

static struct clkgen_quadfs_data st_fs660c32_E_416 = {
	.npda	= CLKGEN_FIELD(0x0, 0x1, 14),
	.nsb	= { CLKGEN_FIELD(0x0, 0x1, 10),
		    CLKGEN_FIELD(0x0, 0x1, 11),
		    CLKGEN_FIELD(0x0, 0x1, 12),
		    CLKGEN_FIELD(0x0, 0x1, 13) },
	.nsdiv_present = true,
	.nsdiv	= { CLKGEN_FIELD(0x0, 0x1, 18),
		    CLKGEN_FIELD(0x0, 0x1, 19),
		    CLKGEN_FIELD(0x0, 0x1, 20),
		    CLKGEN_FIELD(0x0, 0x1, 21) },
	.mdiv	= { CLKGEN_FIELD(0x4, 0x1f, 0),
		    CLKGEN_FIELD(0x14, 0x1f, 0),
		    CLKGEN_FIELD(0x24, 0x1f, 0),
		    CLKGEN_FIELD(0x34, 0x1f, 0) },
	.en	= { CLKGEN_FIELD(0x10, 0x1, 0),
		    CLKGEN_FIELD(0x20, 0x1, 0),
		    CLKGEN_FIELD(0x30, 0x1, 0),
		    CLKGEN_FIELD(0x40, 0x1, 0) },
	.ndiv	= CLKGEN_FIELD(0x0, 0x7, 15),
	.pe	= { CLKGEN_FIELD(0x8, 0x7fff, 0),
		    CLKGEN_FIELD(0x18, 0x7fff, 0),
		    CLKGEN_FIELD(0x28, 0x7fff, 0),
		    CLKGEN_FIELD(0x38, 0x7fff, 0) },
	.sdiv	= { CLKGEN_FIELD(0xC, 0xf, 0),
		    CLKGEN_FIELD(0x1C, 0xf, 0),
		    CLKGEN_FIELD(0x2C, 0xf, 0),
		    CLKGEN_FIELD(0x3C, 0xf, 0) },
	.lockstatus_present = true,
	.lock_status = CLKGEN_FIELD(0xAC, 0x1, 0),
	.pll_ops	= &st_quadfs_pll_c32_ops,
	.rtbl		= fs660c32_rtbl,
	.rtbl_cnt	= ARRAY_SIZE(fs660c32_rtbl),
	.get_rate	= clk_fs660c32_dig_get_rate,
};

static struct clkgen_quadfs_data st_fs660c32_F_416 = {
	.npda	= CLKGEN_FIELD(0x0, 0x1, 14),
	.nsb	= { CLKGEN_FIELD(0x0, 0x1, 10),
		    CLKGEN_FIELD(0x0, 0x1, 11),
		    CLKGEN_FIELD(0x0, 0x1, 12),
		    CLKGEN_FIELD(0x0, 0x1, 13) },
	.nsdiv_present = true,
	.nsdiv	= { CLKGEN_FIELD(0x0, 0x1, 18),
		    CLKGEN_FIELD(0x0, 0x1, 19),
		    CLKGEN_FIELD(0x0, 0x1, 20),
		    CLKGEN_FIELD(0x0, 0x1, 21) },
	.mdiv	= { CLKGEN_FIELD(0x4, 0x1f, 0),
		    CLKGEN_FIELD(0x14, 0x1f, 0),
		    CLKGEN_FIELD(0x24, 0x1f, 0),
		    CLKGEN_FIELD(0x34, 0x1f, 0) },
	.en	= { CLKGEN_FIELD(0x10, 0x1, 0),
		    CLKGEN_FIELD(0x20, 0x1, 0),
		    CLKGEN_FIELD(0x30, 0x1, 0),
		    CLKGEN_FIELD(0x40, 0x1, 0) },
	.ndiv	= CLKGEN_FIELD(0x0, 0x7, 15),
	.pe	= { CLKGEN_FIELD(0x8, 0x7fff, 0),
		    CLKGEN_FIELD(0x18, 0x7fff, 0),
		    CLKGEN_FIELD(0x28, 0x7fff, 0),
		    CLKGEN_FIELD(0x38, 0x7fff, 0) },
	.sdiv	= { CLKGEN_FIELD(0xC, 0xf, 0),
		    CLKGEN_FIELD(0x1C, 0xf, 0),
		    CLKGEN_FIELD(0x2C, 0xf, 0),
		    CLKGEN_FIELD(0x3C, 0xf, 0) },
	.lockstatus_present = true,
	.lock_status = CLKGEN_FIELD(0xEC, 0x1, 0),
	.pll_ops	= &st_quadfs_pll_c32_ops,
	.rtbl		= fs660c32_rtbl,
	.rtbl_cnt	= ARRAY_SIZE(fs660c32_rtbl),
	.get_rate	= clk_fs660c32_dig_get_rate,
};

/**
 * DOC: A Frequency Synthesizer that multiples its input clock by a fixed factor
 *
 * Traits of this clock:
 * prepare - clk_(un)prepare only ensures parent is (un)prepared
 * enable - clk_enable and clk_disable are functional & control the Fsyn
 * rate - inherits rate from parent. set_rate/round_rate/recalc_rate
 * parent - fixed parent.  No clk_set_parent support
 */

/**
 * struct st_clk_quadfs_pll - A pll which outputs a fixed multiplier of
 *                                  its parent clock, found inside a type of
 *                                  ST quad channel frequency synthesizer block
 *
 * @hw: handle between common and hardware-specific interfaces.
 * @ndiv: regmap field for the ndiv control.
 * @regs_base: base address of the configuration registers.
 * @lock: spinlock.
 *
 */
struct st_clk_quadfs_pll {
	struct clk_hw	hw;
	void __iomem	*regs_base;
	spinlock_t	*lock;
	struct clkgen_quadfs_data *data;
	u32 ndiv;
};

#define to_quadfs_pll(_hw) container_of(_hw, struct st_clk_quadfs_pll, hw)

static int quadfs_pll_enable(struct clk_hw *hw)
{
	struct st_clk_quadfs_pll *pll = to_quadfs_pll(hw);
	unsigned long flags = 0, timeout = jiffies + msecs_to_jiffies(10);

	if (pll->lock)
		spin_lock_irqsave(pll->lock, flags);

	/*
	 * Bring block out of reset if we have reset control.
	 */
	if (pll->data->reset_present)
		CLKGEN_WRITE(pll, nreset, 1);

	/*
	 * Use a fixed input clock noise bandwidth filter for the moment
	 */
	if (pll->data->bwfilter_present)
		CLKGEN_WRITE(pll, ref_bw, PLL_BW_GOODREF);


	CLKGEN_WRITE(pll, ndiv, pll->ndiv);

	/*
	 * Power up the PLL
	 */
	CLKGEN_WRITE(pll, npda, 1);

	if (pll->lock)
		spin_unlock_irqrestore(pll->lock, flags);

	if (pll->data->lockstatus_present)
		while (!CLKGEN_READ(pll, lock_status)) {
			if (time_after(jiffies, timeout))
				return -ETIMEDOUT;
			cpu_relax();
		}

	return 0;
}

static void quadfs_pll_disable(struct clk_hw *hw)
{
	struct st_clk_quadfs_pll *pll = to_quadfs_pll(hw);
	unsigned long flags = 0;

	if (pll->lock)
		spin_lock_irqsave(pll->lock, flags);

	/*
	 * Powerdown the PLL and then put block into soft reset if we have
	 * reset control.
	 */
	CLKGEN_WRITE(pll, npda, 0);

	if (pll->data->reset_present)
		CLKGEN_WRITE(pll, nreset, 0);

	if (pll->lock)
		spin_unlock_irqrestore(pll->lock, flags);
}

static int quadfs_pll_is_enabled(struct clk_hw *hw)
{
	struct st_clk_quadfs_pll *pll = to_quadfs_pll(hw);
	u32 npda = CLKGEN_READ(pll, npda);

	return !!npda;
}

int clk_fs660c32_vco_get_rate(unsigned long input, struct stm_fs *fs,
			   unsigned long *rate)
{
	unsigned long nd = fs->ndiv + 16; /* ndiv value */

	*rate = input * nd;

	return 0;
}

static unsigned long quadfs_pll_fs660c32_recalc_rate(struct clk_hw *hw,
					unsigned long parent_rate)
{
	struct st_clk_quadfs_pll *pll = to_quadfs_pll(hw);
	unsigned long rate = 0;
	struct stm_fs params;

	params.ndiv = CLKGEN_READ(pll, ndiv);
	if (clk_fs660c32_vco_get_rate(parent_rate, &params, &rate))
		pr_err("%s:%s error calculating rate\n",
		       __clk_get_name(hw->clk), __func__);

	pll->ndiv = params.ndiv;

	return rate;
}

int clk_fs660c32_vco_get_params(unsigned long input,
				unsigned long output, struct stm_fs *fs)
{
/* Formula
   VCO frequency = (fin x ndiv) / pdiv
   ndiv = VCOfreq * pdiv / fin
   */
	unsigned long pdiv = 1, n;

	/* Output clock range: 384Mhz to 660Mhz */
	if (output < 384000000 || output > 660000000)
		return -EINVAL;

	if (input > 40000000)
		/* This means that PDIV would be 2 instead of 1.
		   Not supported today. */
		return -EINVAL;

	input /= 1000;
	output /= 1000;

	n = output * pdiv / input;
	if (n < 16)
		n = 16;
	fs->ndiv = n - 16; /* Converting formula value to reg value */

	return 0;
}

static long quadfs_pll_fs660c32_round_rate(struct clk_hw *hw, unsigned long rate
		, unsigned long *prate)
{
	struct stm_fs params;

	if (!clk_fs660c32_vco_get_params(*prate, rate, &params))
		clk_fs660c32_vco_get_rate(*prate, &params, &rate);

	pr_debug("%s: %s new rate %ld [sdiv=0x%x,md=0x%x,pe=0x%x,nsdiv3=%u]\n",
		 __func__, __clk_get_name(hw->clk),
		 rate, (unsigned int)params.sdiv,
		 (unsigned int)params.mdiv,
		 (unsigned int)params.pe, (unsigned int)params.nsdiv);

	return rate;
}

static int quadfs_pll_fs660c32_set_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long parent_rate)
{
	struct st_clk_quadfs_pll *pll = to_quadfs_pll(hw);
	struct stm_fs params;
	long hwrate = 0;
	unsigned long flags = 0;

	if (!rate || !parent_rate)
		return -EINVAL;

	if (!clk_fs660c32_vco_get_params(parent_rate, rate, &params))
		clk_fs660c32_vco_get_rate(parent_rate, &params, &hwrate);

	pr_debug("%s: %s new rate %ld [ndiv=0x%x]\n",
		 __func__, __clk_get_name(hw->clk),
		 hwrate, (unsigned int)params.ndiv);

	if (!hwrate)
		return -EINVAL;

	pll->ndiv = params.ndiv;

	if (pll->lock)
		spin_lock_irqsave(pll->lock, flags);

	CLKGEN_WRITE(pll, ndiv, pll->ndiv);

	if (pll->lock)
		spin_unlock_irqrestore(pll->lock, flags);

	return 0;
}

static const struct clk_ops st_quadfs_pll_c65_ops = {
	.enable		= quadfs_pll_enable,
	.disable	= quadfs_pll_disable,
	.is_enabled	= quadfs_pll_is_enabled,
};

static const struct clk_ops st_quadfs_pll_c32_ops = {
	.enable		= quadfs_pll_enable,
	.disable	= quadfs_pll_disable,
	.is_enabled	= quadfs_pll_is_enabled,
	.recalc_rate	= quadfs_pll_fs660c32_recalc_rate,
	.round_rate	= quadfs_pll_fs660c32_round_rate,
	.set_rate	= quadfs_pll_fs660c32_set_rate,
};

static struct clk * __init st_clk_register_quadfs_pll(
		const char *name, const char *parent_name,
		struct clkgen_quadfs_data *quadfs, void __iomem *reg,
		spinlock_t *lock)
{
	struct st_clk_quadfs_pll *pll;
	struct clk *clk;
	struct clk_init_data init;

	/*
	 * Sanity check required pointers.
	 */
	if (WARN_ON(!name || !parent_name))
		return ERR_PTR(-EINVAL);

	pll = kzalloc(sizeof(*pll), GFP_KERNEL);
	if (!pll)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = quadfs->pll_ops;
	init.flags = CLK_IS_BASIC;
	init.parent_names = &parent_name;
	init.num_parents = 1;

	pll->data = quadfs;
	pll->regs_base = reg;
	pll->lock = lock;
	pll->hw.init = &init;

	clk = clk_register(NULL, &pll->hw);

	if (IS_ERR(clk))
		kfree(pll);

	return clk;
}

/**
 * DOC: A digital frequency synthesizer
 *
 * Traits of this clock:
 * prepare - clk_(un)prepare only ensures parent is (un)prepared
 * enable - clk_enable and clk_disable are functional
 * rate - set rate is functional
 * parent - fixed parent.  No clk_set_parent support
 */

/**
 * struct st_clk_quadfs_fsynth - One clock output from a four channel digital
 *                                  frequency synthesizer (fsynth) block.
 *
 * @hw: handle between common and hardware-specific interfaces
 *
 * @nsb: regmap field in the output control register for the digital
 *       standby of this fsynth channel. This control is active low so
 *       the channel is in standby when the control bit is cleared.
 *
 * @nsdiv: regmap field in the output control register for
 *          for the optional divide by 3 of this fsynth channel. This control
 *          is active low so the divide by 3 is active when the control bit is
 *          cleared and the divide is bypassed when the bit is set.
 */
struct st_clk_quadfs_fsynth {
	struct clk_hw	hw;
	void __iomem	*regs_base;
	spinlock_t	*lock;
	struct clkgen_quadfs_data *data;

	u32 chan;
	/*
	 * Cached hardware values from set_rate so we can program the
	 * hardware in enable. There are two reasons for this:
	 *
	 *  1. The registers may not be writable until the parent has been
	 *     enabled.
	 *
	 *  2. It restores the clock rate when a driver does an enable
	 *     on PM restore, after a suspend to RAM has lost the hardware
	 *     setup.
	 */
	u32 md;
	u32 pe;
	u32 sdiv;
	u32 nsdiv;
};

#define to_quadfs_fsynth(_hw) \
	container_of(_hw, struct st_clk_quadfs_fsynth, hw)

static void quadfs_fsynth_program_enable(struct st_clk_quadfs_fsynth *fs)
{
	/*
	 * Pulse the program enable register lsb to make the hardware take
	 * notice of the new md/pe values with a glitchless transition.
	 */
	CLKGEN_WRITE(fs, en[fs->chan], 1);
	CLKGEN_WRITE(fs, en[fs->chan], 0);
}

static void quadfs_fsynth_program_rate(struct st_clk_quadfs_fsynth *fs)
{
	unsigned long flags = 0;

	/*
	 * Ensure the md/pe parameters are ignored while we are
	 * reprogramming them so we can get a glitchless change
	 * when fine tuning the speed of a running clock.
	 */
	CLKGEN_WRITE(fs, en[fs->chan], 0);

	CLKGEN_WRITE(fs, mdiv[fs->chan], fs->md);
	CLKGEN_WRITE(fs, pe[fs->chan], fs->pe);
	CLKGEN_WRITE(fs, sdiv[fs->chan], fs->sdiv);

	if (fs->lock)
		spin_lock_irqsave(fs->lock, flags);

	if (fs->data->nsdiv_present)
		CLKGEN_WRITE(fs, nsdiv[fs->chan], fs->nsdiv);

	if (fs->lock)
		spin_unlock_irqrestore(fs->lock, flags);
}

static int quadfs_fsynth_enable(struct clk_hw *hw)
{
	struct st_clk_quadfs_fsynth *fs = to_quadfs_fsynth(hw);
	unsigned long flags = 0;

	pr_debug("%s: %s\n", __func__, __clk_get_name(hw->clk));

	quadfs_fsynth_program_rate(fs);

	if (fs->lock)
		spin_lock_irqsave(fs->lock, flags);

	CLKGEN_WRITE(fs, nsb[fs->chan], 1);

	if (fs->lock)
		spin_unlock_irqrestore(fs->lock, flags);

	quadfs_fsynth_program_enable(fs);

	return 0;
}

static void quadfs_fsynth_disable(struct clk_hw *hw)
{
	struct st_clk_quadfs_fsynth *fs = to_quadfs_fsynth(hw);
	unsigned long flags = 0;

	pr_debug("%s: %s\n", __func__, __clk_get_name(hw->clk));

	if (fs->lock)
		spin_lock_irqsave(fs->lock, flags);

	CLKGEN_WRITE(fs, nsb[fs->chan], 0);

	if (fs->lock)
		spin_unlock_irqrestore(fs->lock, flags);
}

static int quadfs_fsynth_is_enabled(struct clk_hw *hw)
{
	struct st_clk_quadfs_fsynth *fs = to_quadfs_fsynth(hw);
	u32 nsb = CLKGEN_READ(fs, nsb[fs->chan]);

	pr_debug("%s: %s enable bit = 0x%x\n",
		 __func__, __clk_get_name(hw->clk), nsb);

	return !!nsb;
}

#define P15			(uint64_t)(1 << 15)

static int clk_fs216c65_get_rate(unsigned long input, struct stm_fs *fs,
		unsigned long *rate)
{
	uint64_t res;
	unsigned long ns;
	unsigned long nd = 8; /* ndiv stuck at 0 => val = 8 */
	unsigned long s;
	long m;

	m = fs->mdiv - 32;
	s = 1 << (fs->sdiv + 1);
	ns = (fs->nsdiv ? 1 : 3);

	res = (uint64_t)(s * ns * P15 * (uint64_t)(m + 33));
	res = res - (s * ns * fs->pe);
	*rate = div64_u64(P15 * nd * input * 32, res);

	return 0;
}

static int clk_fs432c65_get_rate(unsigned long input, struct stm_fs *fs,
		unsigned long *rate)
{
	uint64_t res;
	unsigned long nd = 16; /* ndiv value; stuck at 0 (30Mhz input) */
	long m;
	unsigned long sd;
	unsigned long ns;

	m = fs->mdiv - 32;
	sd = 1 << (fs->sdiv + 1);
	ns = (fs->nsdiv ? 1 : 3);

	res = (uint64_t)(sd * ns * P15 * (uint64_t)(m + 33));
	res = res - (sd * ns * fs->pe);
	*rate = div64_u64(P15 * nd * input * 32, res);

	return 0;
}

#define P20		(uint64_t)(1 << 20)

static int clk_fs660c32_dig_get_rate(unsigned long input,
				struct stm_fs *fs, unsigned long *rate)
{
	unsigned long s = (1 << fs->sdiv);
	unsigned long ns;
	uint64_t res;

	/*
	 * 'nsdiv' is a register value ('BIN') which is translated
	 * to a decimal value according to following rules.
	 *
	 *     nsdiv      ns.dec
	 *       0        3
	 *       1        1
	 */
	ns = (fs->nsdiv == 1) ? 1 : 3;

	res = (P20 * (32 + fs->mdiv) + 32 * fs->pe) * s * ns;
	*rate = (unsigned long)div64_u64(input * P20 * 32, res);

	return 0;
}

static int quadfs_fsynt_get_hw_value_for_recalc(struct st_clk_quadfs_fsynth *fs,
		struct stm_fs *params)
{
	/*
	 * Get the initial hardware values for recalc_rate
	 */
	params->mdiv	= CLKGEN_READ(fs, mdiv[fs->chan]);
	params->pe	= CLKGEN_READ(fs, pe[fs->chan]);
	params->sdiv	= CLKGEN_READ(fs, sdiv[fs->chan]);

	if (fs->data->nsdiv_present)
		params->nsdiv = CLKGEN_READ(fs, nsdiv[fs->chan]);
	else
		params->nsdiv = 1;

	/*
	 * If All are NULL then assume no clock rate is programmed.
	 */
	if (!params->mdiv && !params->pe && !params->sdiv)
		return 1;

	fs->md = params->mdiv;
	fs->pe = params->pe;
	fs->sdiv = params->sdiv;
	fs->nsdiv = params->nsdiv;

	return 0;
}

static long quadfs_find_best_rate(struct clk_hw *hw, unsigned long drate,
				unsigned long prate, struct stm_fs *params)
{
	struct st_clk_quadfs_fsynth *fs = to_quadfs_fsynth(hw);
	int (*clk_fs_get_rate)(unsigned long ,
				struct stm_fs *, unsigned long *);
	struct stm_fs prev_params;
	unsigned long prev_rate, rate = 0;
	unsigned long diff_rate, prev_diff_rate = ~0;
	int index;

	clk_fs_get_rate = fs->data->get_rate;

	for (index = 0; index < fs->data->rtbl_cnt; index++) {
		prev_rate = rate;

		*params = fs->data->rtbl[index];
		prev_params = *params;

		clk_fs_get_rate(prate, &fs->data->rtbl[index], &rate);

		diff_rate = abs(drate - rate);

		if (diff_rate > prev_diff_rate) {
			rate = prev_rate;
			*params = prev_params;
			break;
		}

		prev_diff_rate = diff_rate;

		if (drate == rate)
			return rate;
	}


	if (index == fs->data->rtbl_cnt)
		*params = prev_params;

	return rate;
}

static unsigned long quadfs_recalc_rate(struct clk_hw *hw,
		unsigned long parent_rate)
{
	struct st_clk_quadfs_fsynth *fs = to_quadfs_fsynth(hw);
	unsigned long rate = 0;
	struct stm_fs params;
	int (*clk_fs_get_rate)(unsigned long ,
				struct stm_fs *, unsigned long *);

	clk_fs_get_rate = fs->data->get_rate;

	if (quadfs_fsynt_get_hw_value_for_recalc(fs, &params))
		return 0;

	if (clk_fs_get_rate(parent_rate, &params, &rate)) {
		pr_err("%s:%s error calculating rate\n",
		       __clk_get_name(hw->clk), __func__);
	}

	pr_debug("%s:%s rate %lu\n", __clk_get_name(hw->clk), __func__, rate);

	return rate;
}

static long quadfs_round_rate(struct clk_hw *hw, unsigned long rate,
				     unsigned long *prate)
{
	struct stm_fs params;

	rate = quadfs_find_best_rate(hw, rate, *prate, &params);

	pr_debug("%s: %s new rate %ld [sdiv=0x%x,md=0x%x,pe=0x%x,nsdiv3=%u]\n",
		 __func__, __clk_get_name(hw->clk),
		 rate, (unsigned int)params.sdiv, (unsigned int)params.mdiv,
			 (unsigned int)params.pe, (unsigned int)params.nsdiv);

	return rate;
}


static void quadfs_program_and_enable(struct st_clk_quadfs_fsynth *fs,
		struct stm_fs *params)
{
	fs->md = params->mdiv;
	fs->pe = params->pe;
	fs->sdiv = params->sdiv;
	fs->nsdiv = params->nsdiv;

	/*
	 * In some integrations you can only change the fsynth programming when
	 * the parent entity containing it is enabled.
	 */
	quadfs_fsynth_program_rate(fs);
	quadfs_fsynth_program_enable(fs);
}

static int quadfs_set_rate(struct clk_hw *hw, unsigned long rate,
				  unsigned long parent_rate)
{
	struct st_clk_quadfs_fsynth *fs = to_quadfs_fsynth(hw);
	struct stm_fs params;
	long hwrate;
	int uninitialized_var(i);

	if (!rate || !parent_rate)
		return -EINVAL;

	memset(&params, 0, sizeof(struct stm_fs));

	hwrate = quadfs_find_best_rate(hw, rate, parent_rate, &params);
	if (!hwrate)
		return -EINVAL;

	quadfs_program_and_enable(fs, &params);

	return 0;
}



static const struct clk_ops st_quadfs_ops = {
	.enable		= quadfs_fsynth_enable,
	.disable	= quadfs_fsynth_disable,
	.is_enabled	= quadfs_fsynth_is_enabled,
	.round_rate	= quadfs_round_rate,
	.set_rate	= quadfs_set_rate,
	.recalc_rate	= quadfs_recalc_rate,
};

static struct clk * __init st_clk_register_quadfs_fsynth(
		const char *name, const char *parent_name,
		struct clkgen_quadfs_data *quadfs, void __iomem *reg, u32 chan,
		spinlock_t *lock)
{
	struct st_clk_quadfs_fsynth *fs;
	struct clk *clk;
	struct clk_init_data init;

	/*
	 * Sanity check required pointers, note that nsdiv3 is optional.
	 */
	if (WARN_ON(!name || !parent_name))
		return ERR_PTR(-EINVAL);

	fs = kzalloc(sizeof(*fs), GFP_KERNEL);
	if (!fs)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &st_quadfs_ops;
	init.flags = CLK_GET_RATE_NOCACHE | CLK_IS_BASIC;
	init.parent_names = &parent_name;
	init.num_parents = 1;

	fs->data = quadfs;
	fs->regs_base = reg;
	fs->chan = chan;
	fs->lock = lock;
	fs->hw.init = &init;

	clk = clk_register(NULL, &fs->hw);

	if (IS_ERR(clk))
		kfree(fs);

	return clk;
}

static struct of_device_id quadfs_of_match[] = {
	{
		.compatible = "st,stih416-quadfs216",
		.data = (void *)&st_fs216c65_416
	},
	{
		.compatible = "st,stih416-quadfs432",
		.data = (void *)&st_fs432c65_416
	},
	{
		.compatible = "st,stih416-quadfs660-E",
		.data = (void *)&st_fs660c32_E_416
	},
	{
		.compatible = "st,stih416-quadfs660-F",
		.data = (void *)&st_fs660c32_F_416
	},
	{}
};

static void __init st_of_create_quadfs_fsynths(
		struct device_node *np, const char *pll_name,
		struct clkgen_quadfs_data *quadfs, void __iomem *reg,
		spinlock_t *lock)
{
	struct clk_onecell_data *clk_data;
	int fschan;

	clk_data = kzalloc(sizeof(*clk_data), GFP_KERNEL);
	if (!clk_data)
		return;

	clk_data->clk_num = QUADFS_MAX_CHAN;
	clk_data->clks = kzalloc(QUADFS_MAX_CHAN * sizeof(struct clk *),
				 GFP_KERNEL);

	if (!clk_data->clks) {
		kfree(clk_data);
		return;
	}

	for (fschan = 0; fschan < QUADFS_MAX_CHAN; fschan++) {
		struct clk *clk;
		const char *clk_name;

		if (of_property_read_string_index(np, "clock-output-names",
						  fschan, &clk_name)) {
			break;
		}

		/*
		 * If we read an empty clock name then the channel is unused
		 */
		if (*clk_name == '\0')
			continue;

		clk = st_clk_register_quadfs_fsynth(clk_name, pll_name,
				quadfs, reg, fschan, lock);

		/*
		 * If there was an error registering this clock output, clean
		 * up and move on to the next one.
		 */
		if (!IS_ERR(clk)) {
			clk_data->clks[fschan] = clk;
			pr_debug("%s: parent %s rate %u\n",
				__clk_get_name(clk),
				__clk_get_name(clk_get_parent(clk)),
				(unsigned int)clk_get_rate(clk));
		}
	}

	of_clk_add_provider(np, of_clk_src_onecell_get, clk_data);
}

static void __init st_of_quadfs_setup(struct device_node *np)
{
	const struct of_device_id *match;
	struct clk *clk;
	const char *pll_name, *clk_parent_name;
	void __iomem *reg;
	spinlock_t *lock;

	match = of_match_node(quadfs_of_match, np);
	if (WARN_ON(!match))
		return;

	reg = of_iomap(np, 0);
	if (!reg)
		return;

	clk_parent_name = of_clk_get_parent_name(np, 0);
	if (!clk_parent_name)
		return;

	pll_name = kasprintf(GFP_KERNEL, "%s.pll", np->name);
	if (!pll_name)
		return;

	lock = kzalloc(sizeof(*lock), GFP_KERNEL);
	if (!lock)
		goto err_exit;

	spin_lock_init(lock);

	clk = st_clk_register_quadfs_pll(pll_name, clk_parent_name,
			(struct clkgen_quadfs_data *) match->data, reg, lock);
	if (IS_ERR(clk))
		goto err_exit;
	else
		pr_debug("%s: parent %s rate %u\n",
			__clk_get_name(clk),
			__clk_get_name(clk_get_parent(clk)),
			(unsigned int)clk_get_rate(clk));

	st_of_create_quadfs_fsynths(np, pll_name,
				    (struct clkgen_quadfs_data *)match->data,
				    reg, lock);

err_exit:
	kfree(pll_name); /* No longer need local copy of the PLL name */
}
CLK_OF_DECLARE(quadfs, "st,quadfs", st_of_quadfs_setup);
