// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2014 STMicroelectronics R&D Ltd
 */

/*
 * Authors:
 * Stephen Gallimore <stephen.gallimore@st.com>,
 * Pankaj Dev <pankaj.dev@st.com>.
 */

#include <linux/slab.h>
#include <linux/of_address.h>
#include <linux/clk.h>
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

struct clkgen_quadfs_data {
	bool reset_present;
	bool bwfilter_present;
	bool lockstatus_present;
	bool powerup_polarity;
	bool standby_polarity;
	bool nsdiv_present;
	bool nrst_present;
	struct clkgen_field ndiv;
	struct clkgen_field ref_bw;
	struct clkgen_field nreset;
	struct clkgen_field npda;
	struct clkgen_field lock_status;

	struct clkgen_field nrst[QUADFS_MAX_CHAN];
	struct clkgen_field nsb[QUADFS_MAX_CHAN];
	struct clkgen_field en[QUADFS_MAX_CHAN];
	struct clkgen_field mdiv[QUADFS_MAX_CHAN];
	struct clkgen_field pe[QUADFS_MAX_CHAN];
	struct clkgen_field sdiv[QUADFS_MAX_CHAN];
	struct clkgen_field nsdiv[QUADFS_MAX_CHAN];

	const struct clk_ops *pll_ops;
	int  (*get_params)(unsigned long, unsigned long, struct stm_fs *);
	int  (*get_rate)(unsigned long , const struct stm_fs *,
			unsigned long *);
};

static const struct clk_ops st_quadfs_pll_c32_ops;
static const struct clk_ops st_quadfs_fs660c32_ops;

static int clk_fs660c32_dig_get_params(unsigned long input,
		unsigned long output, struct stm_fs *fs);
static int clk_fs660c32_dig_get_rate(unsigned long, const struct stm_fs *,
		unsigned long *);

static const struct clkgen_quadfs_data st_fs660c32_C = {
	.nrst_present = true,
	.nrst	= { CLKGEN_FIELD(0x2f0, 0x1, 0),
		    CLKGEN_FIELD(0x2f0, 0x1, 1),
		    CLKGEN_FIELD(0x2f0, 0x1, 2),
		    CLKGEN_FIELD(0x2f0, 0x1, 3) },
	.npda	= CLKGEN_FIELD(0x2f0, 0x1, 12),
	.nsb	= { CLKGEN_FIELD(0x2f0, 0x1, 8),
		    CLKGEN_FIELD(0x2f0, 0x1, 9),
		    CLKGEN_FIELD(0x2f0, 0x1, 10),
		    CLKGEN_FIELD(0x2f0, 0x1, 11) },
	.nsdiv_present = true,
	.nsdiv	= { CLKGEN_FIELD(0x304, 0x1, 24),
		    CLKGEN_FIELD(0x308, 0x1, 24),
		    CLKGEN_FIELD(0x30c, 0x1, 24),
		    CLKGEN_FIELD(0x310, 0x1, 24) },
	.mdiv	= { CLKGEN_FIELD(0x304, 0x1f, 15),
		    CLKGEN_FIELD(0x308, 0x1f, 15),
		    CLKGEN_FIELD(0x30c, 0x1f, 15),
		    CLKGEN_FIELD(0x310, 0x1f, 15) },
	.en	= { CLKGEN_FIELD(0x2fc, 0x1, 0),
		    CLKGEN_FIELD(0x2fc, 0x1, 1),
		    CLKGEN_FIELD(0x2fc, 0x1, 2),
		    CLKGEN_FIELD(0x2fc, 0x1, 3) },
	.ndiv	= CLKGEN_FIELD(0x2f4, 0x7, 16),
	.pe	= { CLKGEN_FIELD(0x304, 0x7fff, 0),
		    CLKGEN_FIELD(0x308, 0x7fff, 0),
		    CLKGEN_FIELD(0x30c, 0x7fff, 0),
		    CLKGEN_FIELD(0x310, 0x7fff, 0) },
	.sdiv	= { CLKGEN_FIELD(0x304, 0xf, 20),
		    CLKGEN_FIELD(0x308, 0xf, 20),
		    CLKGEN_FIELD(0x30c, 0xf, 20),
		    CLKGEN_FIELD(0x310, 0xf, 20) },
	.lockstatus_present = true,
	.lock_status = CLKGEN_FIELD(0x2f0, 0x1, 24),
	.powerup_polarity = 1,
	.standby_polarity = 1,
	.pll_ops	= &st_quadfs_pll_c32_ops,
	.get_params	= clk_fs660c32_dig_get_params,
	.get_rate	= clk_fs660c32_dig_get_rate,
};

static const struct clkgen_quadfs_data st_fs660c32_D = {
	.nrst_present = true,
	.nrst	= { CLKGEN_FIELD(0x2a0, 0x1, 0),
		    CLKGEN_FIELD(0x2a0, 0x1, 1),
		    CLKGEN_FIELD(0x2a0, 0x1, 2),
		    CLKGEN_FIELD(0x2a0, 0x1, 3) },
	.ndiv	= CLKGEN_FIELD(0x2a4, 0x7, 16),
	.pe	= { CLKGEN_FIELD(0x2b4, 0x7fff, 0),
		    CLKGEN_FIELD(0x2b8, 0x7fff, 0),
		    CLKGEN_FIELD(0x2bc, 0x7fff, 0),
		    CLKGEN_FIELD(0x2c0, 0x7fff, 0) },
	.sdiv	= { CLKGEN_FIELD(0x2b4, 0xf, 20),
		    CLKGEN_FIELD(0x2b8, 0xf, 20),
		    CLKGEN_FIELD(0x2bc, 0xf, 20),
		    CLKGEN_FIELD(0x2c0, 0xf, 20) },
	.npda	= CLKGEN_FIELD(0x2a0, 0x1, 12),
	.nsb	= { CLKGEN_FIELD(0x2a0, 0x1, 8),
		    CLKGEN_FIELD(0x2a0, 0x1, 9),
		    CLKGEN_FIELD(0x2a0, 0x1, 10),
		    CLKGEN_FIELD(0x2a0, 0x1, 11) },
	.nsdiv_present = true,
	.nsdiv	= { CLKGEN_FIELD(0x2b4, 0x1, 24),
		    CLKGEN_FIELD(0x2b8, 0x1, 24),
		    CLKGEN_FIELD(0x2bc, 0x1, 24),
		    CLKGEN_FIELD(0x2c0, 0x1, 24) },
	.mdiv	= { CLKGEN_FIELD(0x2b4, 0x1f, 15),
		    CLKGEN_FIELD(0x2b8, 0x1f, 15),
		    CLKGEN_FIELD(0x2bc, 0x1f, 15),
		    CLKGEN_FIELD(0x2c0, 0x1f, 15) },
	.en	= { CLKGEN_FIELD(0x2ac, 0x1, 0),
		    CLKGEN_FIELD(0x2ac, 0x1, 1),
		    CLKGEN_FIELD(0x2ac, 0x1, 2),
		    CLKGEN_FIELD(0x2ac, 0x1, 3) },
	.lockstatus_present = true,
	.lock_status = CLKGEN_FIELD(0x2A0, 0x1, 24),
	.powerup_polarity = 1,
	.standby_polarity = 1,
	.pll_ops	= &st_quadfs_pll_c32_ops,
	.get_params	= clk_fs660c32_dig_get_params,
	.get_rate	= clk_fs660c32_dig_get_rate,};

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
	CLKGEN_WRITE(pll, npda, !pll->data->powerup_polarity);

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
	CLKGEN_WRITE(pll, npda, pll->data->powerup_polarity);

	if (pll->data->reset_present)
		CLKGEN_WRITE(pll, nreset, 0);

	if (pll->lock)
		spin_unlock_irqrestore(pll->lock, flags);
}

static int quadfs_pll_is_enabled(struct clk_hw *hw)
{
	struct st_clk_quadfs_pll *pll = to_quadfs_pll(hw);
	u32 npda = CLKGEN_READ(pll, npda);

	return pll->data->powerup_polarity ? !npda : !!npda;
}

static int clk_fs660c32_vco_get_rate(unsigned long input, struct stm_fs *fs,
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
		       clk_hw_get_name(hw), __func__);

	pll->ndiv = params.ndiv;

	return rate;
}

static int clk_fs660c32_vco_get_params(unsigned long input,
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

static long quadfs_pll_fs660c32_round_rate(struct clk_hw *hw,
					   unsigned long rate,
					   unsigned long *prate)
{
	struct stm_fs params;

	if (clk_fs660c32_vco_get_params(*prate, rate, &params))
		return rate;

	clk_fs660c32_vco_get_rate(*prate, &params, &rate);

	pr_debug("%s: %s new rate %ld [ndiv=%u]\n",
		 __func__, clk_hw_get_name(hw),
		 rate, (unsigned int)params.ndiv);

	return rate;
}

static int quadfs_pll_fs660c32_set_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long parent_rate)
{
	struct st_clk_quadfs_pll *pll = to_quadfs_pll(hw);
	struct stm_fs params;
	long hwrate = 0;
	unsigned long flags = 0;
	int ret;

	if (!rate || !parent_rate)
		return -EINVAL;

	ret = clk_fs660c32_vco_get_params(parent_rate, rate, &params);
	if (ret)
		return ret;

	clk_fs660c32_vco_get_rate(parent_rate, &params, &hwrate);

	pr_debug("%s: %s new rate %ld [ndiv=0x%x]\n",
		 __func__, clk_hw_get_name(hw),
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
	init.flags = CLK_GET_RATE_NOCACHE;
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

	pr_debug("%s: %s\n", __func__, clk_hw_get_name(hw));

	quadfs_fsynth_program_rate(fs);

	if (fs->lock)
		spin_lock_irqsave(fs->lock, flags);

	CLKGEN_WRITE(fs, nsb[fs->chan], !fs->data->standby_polarity);

	if (fs->data->nrst_present)
		CLKGEN_WRITE(fs, nrst[fs->chan], 0);

	if (fs->lock)
		spin_unlock_irqrestore(fs->lock, flags);

	quadfs_fsynth_program_enable(fs);

	return 0;
}

static void quadfs_fsynth_disable(struct clk_hw *hw)
{
	struct st_clk_quadfs_fsynth *fs = to_quadfs_fsynth(hw);
	unsigned long flags = 0;

	pr_debug("%s: %s\n", __func__, clk_hw_get_name(hw));

	if (fs->lock)
		spin_lock_irqsave(fs->lock, flags);

	CLKGEN_WRITE(fs, nsb[fs->chan], fs->data->standby_polarity);

	if (fs->lock)
		spin_unlock_irqrestore(fs->lock, flags);
}

static int quadfs_fsynth_is_enabled(struct clk_hw *hw)
{
	struct st_clk_quadfs_fsynth *fs = to_quadfs_fsynth(hw);
	u32 nsb = CLKGEN_READ(fs, nsb[fs->chan]);

	pr_debug("%s: %s enable bit = 0x%x\n",
		 __func__, clk_hw_get_name(hw), nsb);

	return fs->data->standby_polarity ? !nsb : !!nsb;
}

#define P20		(uint64_t)(1 << 20)

static int clk_fs660c32_dig_get_rate(unsigned long input,
				const struct stm_fs *fs, unsigned long *rate)
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


static int clk_fs660c32_get_pe(int m, int si, unsigned long *deviation,
		signed long input, unsigned long output, uint64_t *p,
		struct stm_fs *fs)
{
	unsigned long new_freq, new_deviation;
	struct stm_fs fs_tmp;
	uint64_t val;

	val = (uint64_t)output << si;

	*p = (uint64_t)input * P20 - (32LL  + (uint64_t)m) * val * (P20 / 32LL);

	*p = div64_u64(*p, val);

	if (*p > 32767LL)
		return 1;

	fs_tmp.mdiv = (unsigned long) m;
	fs_tmp.pe = (unsigned long)*p;
	fs_tmp.sdiv = si;
	fs_tmp.nsdiv = 1;

	clk_fs660c32_dig_get_rate(input, &fs_tmp, &new_freq);

	new_deviation = abs(output - new_freq);

	if (new_deviation < *deviation) {
		fs->mdiv = m;
		fs->pe = (unsigned long)*p;
		fs->sdiv = si;
		fs->nsdiv = 1;
		*deviation = new_deviation;
	}
	return 0;
}

static int clk_fs660c32_dig_get_params(unsigned long input,
		unsigned long output, struct stm_fs *fs)
{
	int si;	/* sdiv_reg (8 downto 0) */
	int m; /* md value */
	unsigned long new_freq, new_deviation;
	/* initial condition to say: "infinite deviation" */
	unsigned long deviation = ~0;
	uint64_t p, p1, p2;	/* pe value */
	int r1, r2;

	struct stm_fs fs_tmp;

	for (si = 0; (si <= 8) && deviation; si++) {

		/* Boundary test to avoid useless iteration */
		r1 = clk_fs660c32_get_pe(0, si, &deviation,
				input, output, &p1, fs);
		r2 = clk_fs660c32_get_pe(31, si, &deviation,
				input, output, &p2, fs);

		/* No solution */
		if (r1 && r2 && (p1 > p2))
			continue;

		/* Try to find best deviation */
		for (m = 1; (m < 31) && deviation; m++)
			clk_fs660c32_get_pe(m, si, &deviation,
					input, output, &p, fs);

	}

	if (deviation == ~0) /* No solution found */
		return -1;

	/* pe fine tuning if deviation not 0: +/- 2 around computed pe value */
	if (deviation) {
		fs_tmp.mdiv = fs->mdiv;
		fs_tmp.sdiv = fs->sdiv;
		fs_tmp.nsdiv = fs->nsdiv;

		if (fs->pe > 2)
			p2 = fs->pe - 2;
		else
			p2 = 0;

		for (; p2 < 32768ll && (p2 <= (fs->pe + 2)); p2++) {
			fs_tmp.pe = (unsigned long)p2;

			clk_fs660c32_dig_get_rate(input, &fs_tmp, &new_freq);

			new_deviation = abs(output - new_freq);

			/* Check if this is a better solution */
			if (new_deviation < deviation) {
				fs->pe = (unsigned long)p2;
				deviation = new_deviation;

			}
		}
	}
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
				const struct stm_fs *, unsigned long *);
	int (*clk_fs_get_params)(unsigned long, unsigned long, struct stm_fs *);
	unsigned long rate = 0;

	clk_fs_get_rate = fs->data->get_rate;
	clk_fs_get_params = fs->data->get_params;

	if (!clk_fs_get_params(prate, drate, params))
		clk_fs_get_rate(prate, params, &rate);

	return rate;
}

static unsigned long quadfs_recalc_rate(struct clk_hw *hw,
		unsigned long parent_rate)
{
	struct st_clk_quadfs_fsynth *fs = to_quadfs_fsynth(hw);
	unsigned long rate = 0;
	struct stm_fs params;
	int (*clk_fs_get_rate)(unsigned long ,
				const struct stm_fs *, unsigned long *);

	clk_fs_get_rate = fs->data->get_rate;

	if (quadfs_fsynt_get_hw_value_for_recalc(fs, &params))
		return 0;

	if (clk_fs_get_rate(parent_rate, &params, &rate)) {
		pr_err("%s:%s error calculating rate\n",
		       clk_hw_get_name(hw), __func__);
	}

	pr_debug("%s:%s rate %lu\n", clk_hw_get_name(hw), __func__, rate);

	return rate;
}

static long quadfs_round_rate(struct clk_hw *hw, unsigned long rate,
				     unsigned long *prate)
{
	struct stm_fs params;

	rate = quadfs_find_best_rate(hw, rate, *prate, &params);

	pr_debug("%s: %s new rate %ld [sdiv=0x%x,md=0x%x,pe=0x%x,nsdiv3=%u]\n",
		 __func__, clk_hw_get_name(hw),
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
		unsigned long flags, spinlock_t *lock)
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
	init.flags = flags | CLK_GET_RATE_NOCACHE;
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
	clk_data->clks = kcalloc(QUADFS_MAX_CHAN, sizeof(struct clk *),
				 GFP_KERNEL);

	if (!clk_data->clks) {
		kfree(clk_data);
		return;
	}

	for (fschan = 0; fschan < QUADFS_MAX_CHAN; fschan++) {
		struct clk *clk;
		const char *clk_name;
		unsigned long flags = 0;

		if (of_property_read_string_index(np, "clock-output-names",
						  fschan, &clk_name)) {
			break;
		}

		/*
		 * If we read an empty clock name then the channel is unused
		 */
		if (*clk_name == '\0')
			continue;

		of_clk_detect_critical(np, fschan, &flags);

		clk = st_clk_register_quadfs_fsynth(clk_name, pll_name,
						    quadfs, reg, fschan,
						    flags, lock);

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

static void __init st_of_quadfs_setup(struct device_node *np,
		struct clkgen_quadfs_data *data)
{
	struct clk *clk;
	const char *pll_name, *clk_parent_name;
	void __iomem *reg;
	spinlock_t *lock;

	reg = of_iomap(np, 0);
	if (!reg)
		return;

	clk_parent_name = of_clk_get_parent_name(np, 0);
	if (!clk_parent_name)
		return;

	pll_name = kasprintf(GFP_KERNEL, "%pOFn.pll", np);
	if (!pll_name)
		return;

	lock = kzalloc(sizeof(*lock), GFP_KERNEL);
	if (!lock)
		goto err_exit;

	spin_lock_init(lock);

	clk = st_clk_register_quadfs_pll(pll_name, clk_parent_name, data,
			reg, lock);
	if (IS_ERR(clk))
		goto err_exit;
	else
		pr_debug("%s: parent %s rate %u\n",
			__clk_get_name(clk),
			__clk_get_name(clk_get_parent(clk)),
			(unsigned int)clk_get_rate(clk));

	st_of_create_quadfs_fsynths(np, pll_name, data, reg, lock);

err_exit:
	kfree(pll_name); /* No longer need local copy of the PLL name */
}

static void __init st_of_quadfs660C_setup(struct device_node *np)
{
	st_of_quadfs_setup(np, (struct clkgen_quadfs_data *) &st_fs660c32_C);
}
CLK_OF_DECLARE(quadfs660C, "st,quadfs-pll", st_of_quadfs660C_setup);

static void __init st_of_quadfs660D_setup(struct device_node *np)
{
	st_of_quadfs_setup(np, (struct clkgen_quadfs_data *) &st_fs660c32_D);
}
CLK_OF_DECLARE(quadfs660D, "st,quadfs", st_of_quadfs660D_setup);
