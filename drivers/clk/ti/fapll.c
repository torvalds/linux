/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/math64.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/clk/ti.h>

/* FAPLL Control Register PLL_CTRL */
#define FAPLL_MAIN_MULT_N_SHIFT	16
#define FAPLL_MAIN_DIV_P_SHIFT	8
#define FAPLL_MAIN_LOCK		BIT(7)
#define FAPLL_MAIN_PLLEN	BIT(3)
#define FAPLL_MAIN_BP		BIT(2)
#define FAPLL_MAIN_LOC_CTL	BIT(0)

#define FAPLL_MAIN_MAX_MULT_N	0xffff
#define FAPLL_MAIN_MAX_DIV_P	0xff
#define FAPLL_MAIN_CLEAR_MASK	\
	((FAPLL_MAIN_MAX_MULT_N << FAPLL_MAIN_MULT_N_SHIFT) | \
	 (FAPLL_MAIN_DIV_P_SHIFT << FAPLL_MAIN_DIV_P_SHIFT) | \
	 FAPLL_MAIN_LOC_CTL)

/* FAPLL powerdown register PWD */
#define FAPLL_PWD_OFFSET	4

#define MAX_FAPLL_OUTPUTS	7
#define FAPLL_MAX_RETRIES	1000

#define to_fapll(_hw)		container_of(_hw, struct fapll_data, hw)
#define to_synth(_hw)		container_of(_hw, struct fapll_synth, hw)

/* The bypass bit is inverted on the ddr_pll.. */
#define fapll_is_ddr_pll(va)	(((u32)(va) & 0xffff) == 0x0440)

/*
 * The audio_pll_clk1 input is hard wired to the 27MHz bypass clock,
 * and the audio_pll_clk1 synthesizer is hardwared to 32KiHz output.
 */
#define is_ddr_pll_clk1(va)	(((u32)(va) & 0xffff) == 0x044c)
#define is_audio_pll_clk1(va)	(((u32)(va) & 0xffff) == 0x04a8)

/* Synthesizer divider register */
#define SYNTH_LDMDIV1		BIT(8)

/* Synthesizer frequency register */
#define SYNTH_LDFREQ		BIT(31)

#define SYNTH_PHASE_K		8
#define SYNTH_MAX_INT_DIV	0xf
#define SYNTH_MAX_DIV_M		0xff

struct fapll_data {
	struct clk_hw hw;
	void __iomem *base;
	const char *name;
	struct clk *clk_ref;
	struct clk *clk_bypass;
	struct clk_onecell_data outputs;
	bool bypass_bit_inverted;
};

struct fapll_synth {
	struct clk_hw hw;
	struct fapll_data *fd;
	int index;
	void __iomem *freq;
	void __iomem *div;
	const char *name;
	struct clk *clk_pll;
};

static bool ti_fapll_clock_is_bypass(struct fapll_data *fd)
{
	u32 v = readl_relaxed(fd->base);

	if (fd->bypass_bit_inverted)
		return !(v & FAPLL_MAIN_BP);
	else
		return !!(v & FAPLL_MAIN_BP);
}

static void ti_fapll_set_bypass(struct fapll_data *fd)
{
	u32 v = readl_relaxed(fd->base);

	if (fd->bypass_bit_inverted)
		v &= ~FAPLL_MAIN_BP;
	else
		v |= FAPLL_MAIN_BP;
	writel_relaxed(v, fd->base);
}

static void ti_fapll_clear_bypass(struct fapll_data *fd)
{
	u32 v = readl_relaxed(fd->base);

	if (fd->bypass_bit_inverted)
		v |= FAPLL_MAIN_BP;
	else
		v &= ~FAPLL_MAIN_BP;
	writel_relaxed(v, fd->base);
}

static int ti_fapll_wait_lock(struct fapll_data *fd)
{
	int retries = FAPLL_MAX_RETRIES;
	u32 v;

	while ((v = readl_relaxed(fd->base))) {
		if (v & FAPLL_MAIN_LOCK)
			return 0;

		if (retries-- <= 0)
			break;

		udelay(1);
	}

	pr_err("%s failed to lock\n", fd->name);

	return -ETIMEDOUT;
}

static int ti_fapll_enable(struct clk_hw *hw)
{
	struct fapll_data *fd = to_fapll(hw);
	u32 v = readl_relaxed(fd->base);

	v |= FAPLL_MAIN_PLLEN;
	writel_relaxed(v, fd->base);
	ti_fapll_wait_lock(fd);

	return 0;
}

static void ti_fapll_disable(struct clk_hw *hw)
{
	struct fapll_data *fd = to_fapll(hw);
	u32 v = readl_relaxed(fd->base);

	v &= ~FAPLL_MAIN_PLLEN;
	writel_relaxed(v, fd->base);
}

static int ti_fapll_is_enabled(struct clk_hw *hw)
{
	struct fapll_data *fd = to_fapll(hw);
	u32 v = readl_relaxed(fd->base);

	return v & FAPLL_MAIN_PLLEN;
}

static unsigned long ti_fapll_recalc_rate(struct clk_hw *hw,
					  unsigned long parent_rate)
{
	struct fapll_data *fd = to_fapll(hw);
	u32 fapll_n, fapll_p, v;
	u64 rate;

	if (ti_fapll_clock_is_bypass(fd))
		return parent_rate;

	rate = parent_rate;

	/* PLL pre-divider is P and multiplier is N */
	v = readl_relaxed(fd->base);
	fapll_p = (v >> 8) & 0xff;
	if (fapll_p)
		do_div(rate, fapll_p);
	fapll_n = v >> 16;
	if (fapll_n)
		rate *= fapll_n;

	return rate;
}

static u8 ti_fapll_get_parent(struct clk_hw *hw)
{
	struct fapll_data *fd = to_fapll(hw);

	if (ti_fapll_clock_is_bypass(fd))
		return 1;

	return 0;
}

static int ti_fapll_set_div_mult(unsigned long rate,
				 unsigned long parent_rate,
				 u32 *pre_div_p, u32 *mult_n)
{
	/*
	 * So far no luck getting decent clock with PLL divider,
	 * PLL does not seem to lock and the signal does not look
	 * right. It seems the divider can only be used together
	 * with the multiplier?
	 */
	if (rate < parent_rate) {
		pr_warn("FAPLL main divider rates unsupported\n");
		return -EINVAL;
	}

	*mult_n = rate / parent_rate;
	if (*mult_n > FAPLL_MAIN_MAX_MULT_N)
		return -EINVAL;
	*pre_div_p = 1;

	return 0;
}

static long ti_fapll_round_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long *parent_rate)
{
	u32 pre_div_p, mult_n;
	int error;

	if (!rate)
		return -EINVAL;

	error = ti_fapll_set_div_mult(rate, *parent_rate,
				      &pre_div_p, &mult_n);
	if (error)
		return error;

	rate = *parent_rate / pre_div_p;
	rate *= mult_n;

	return rate;
}

static int ti_fapll_set_rate(struct clk_hw *hw, unsigned long rate,
			     unsigned long parent_rate)
{
	struct fapll_data *fd = to_fapll(hw);
	u32 pre_div_p, mult_n, v;
	int error;

	if (!rate)
		return -EINVAL;

	error = ti_fapll_set_div_mult(rate, parent_rate,
				      &pre_div_p, &mult_n);
	if (error)
		return error;

	ti_fapll_set_bypass(fd);
	v = readl_relaxed(fd->base);
	v &= ~FAPLL_MAIN_CLEAR_MASK;
	v |= pre_div_p << FAPLL_MAIN_DIV_P_SHIFT;
	v |= mult_n << FAPLL_MAIN_MULT_N_SHIFT;
	writel_relaxed(v, fd->base);
	if (ti_fapll_is_enabled(hw))
		ti_fapll_wait_lock(fd);
	ti_fapll_clear_bypass(fd);

	return 0;
}

static const struct clk_ops ti_fapll_ops = {
	.enable = ti_fapll_enable,
	.disable = ti_fapll_disable,
	.is_enabled = ti_fapll_is_enabled,
	.recalc_rate = ti_fapll_recalc_rate,
	.get_parent = ti_fapll_get_parent,
	.round_rate = ti_fapll_round_rate,
	.set_rate = ti_fapll_set_rate,
};

static int ti_fapll_synth_enable(struct clk_hw *hw)
{
	struct fapll_synth *synth = to_synth(hw);
	u32 v = readl_relaxed(synth->fd->base + FAPLL_PWD_OFFSET);

	v &= ~(1 << synth->index);
	writel_relaxed(v, synth->fd->base + FAPLL_PWD_OFFSET);

	return 0;
}

static void ti_fapll_synth_disable(struct clk_hw *hw)
{
	struct fapll_synth *synth = to_synth(hw);
	u32 v = readl_relaxed(synth->fd->base + FAPLL_PWD_OFFSET);

	v |= 1 << synth->index;
	writel_relaxed(v, synth->fd->base + FAPLL_PWD_OFFSET);
}

static int ti_fapll_synth_is_enabled(struct clk_hw *hw)
{
	struct fapll_synth *synth = to_synth(hw);
	u32 v = readl_relaxed(synth->fd->base + FAPLL_PWD_OFFSET);

	return !(v & (1 << synth->index));
}

/*
 * See dm816x TRM chapter 1.10.3 Flying Adder PLL fore more info
 */
static unsigned long ti_fapll_synth_recalc_rate(struct clk_hw *hw,
						unsigned long parent_rate)
{
	struct fapll_synth *synth = to_synth(hw);
	u32 synth_div_m;
	u64 rate;

	/* The audio_pll_clk1 is hardwired to produce 32.768KiHz clock */
	if (!synth->div)
		return 32768;

	/*
	 * PLL in bypass sets the synths in bypass mode too. The PLL rate
	 * can be also be set to 27MHz, so we can't use parent_rate to
	 * check for bypass mode.
	 */
	if (ti_fapll_clock_is_bypass(synth->fd))
		return parent_rate;

	rate = parent_rate;

	/*
	 * Synth frequency integer and fractional divider.
	 * Note that the phase output K is 8, so the result needs
	 * to be multiplied by SYNTH_PHASE_K.
	 */
	if (synth->freq) {
		u32 v, synth_int_div, synth_frac_div, synth_div_freq;

		v = readl_relaxed(synth->freq);
		synth_int_div = (v >> 24) & 0xf;
		synth_frac_div = v & 0xffffff;
		synth_div_freq = (synth_int_div * 10000000) + synth_frac_div;
		rate *= 10000000;
		do_div(rate, synth_div_freq);
		rate *= SYNTH_PHASE_K;
	}

	/* Synth post-divider M */
	synth_div_m = readl_relaxed(synth->div) & SYNTH_MAX_DIV_M;

	return DIV_ROUND_UP_ULL(rate, synth_div_m);
}

static unsigned long ti_fapll_synth_get_frac_rate(struct clk_hw *hw,
						  unsigned long parent_rate)
{
	struct fapll_synth *synth = to_synth(hw);
	unsigned long current_rate, frac_rate;
	u32 post_div_m;

	current_rate = ti_fapll_synth_recalc_rate(hw, parent_rate);
	post_div_m = readl_relaxed(synth->div) & SYNTH_MAX_DIV_M;
	frac_rate = current_rate * post_div_m;

	return frac_rate;
}

static u32 ti_fapll_synth_set_frac_rate(struct fapll_synth *synth,
					unsigned long rate,
					unsigned long parent_rate)
{
	u32 post_div_m, synth_int_div = 0, synth_frac_div = 0, v;

	post_div_m = DIV_ROUND_UP_ULL((u64)parent_rate * SYNTH_PHASE_K, rate);
	post_div_m = post_div_m / SYNTH_MAX_INT_DIV;
	if (post_div_m > SYNTH_MAX_DIV_M)
		return -EINVAL;
	if (!post_div_m)
		post_div_m = 1;

	for (; post_div_m < SYNTH_MAX_DIV_M; post_div_m++) {
		synth_int_div = DIV_ROUND_UP_ULL((u64)parent_rate *
						 SYNTH_PHASE_K *
						 10000000,
						 rate * post_div_m);
		synth_frac_div = synth_int_div % 10000000;
		synth_int_div /= 10000000;

		if (synth_int_div <= SYNTH_MAX_INT_DIV)
			break;
	}

	if (synth_int_div > SYNTH_MAX_INT_DIV)
		return -EINVAL;

	v = readl_relaxed(synth->freq);
	v &= ~0x1fffffff;
	v |= (synth_int_div & SYNTH_MAX_INT_DIV) << 24;
	v |= (synth_frac_div & 0xffffff);
	v |= SYNTH_LDFREQ;
	writel_relaxed(v, synth->freq);

	return post_div_m;
}

static long ti_fapll_synth_round_rate(struct clk_hw *hw, unsigned long rate,
				      unsigned long *parent_rate)
{
	struct fapll_synth *synth = to_synth(hw);
	struct fapll_data *fd = synth->fd;
	unsigned long r;

	if (ti_fapll_clock_is_bypass(fd) || !synth->div || !rate)
		return -EINVAL;

	/* Only post divider m available with no fractional divider? */
	if (!synth->freq) {
		unsigned long frac_rate;
		u32 synth_post_div_m;

		frac_rate = ti_fapll_synth_get_frac_rate(hw, *parent_rate);
		synth_post_div_m = DIV_ROUND_UP(frac_rate, rate);
		r = DIV_ROUND_UP(frac_rate, synth_post_div_m);
		goto out;
	}

	r = *parent_rate * SYNTH_PHASE_K;
	if (rate > r)
		goto out;

	r = DIV_ROUND_UP_ULL(r, SYNTH_MAX_INT_DIV * SYNTH_MAX_DIV_M);
	if (rate < r)
		goto out;

	r = rate;
out:
	return r;
}

static int ti_fapll_synth_set_rate(struct clk_hw *hw, unsigned long rate,
				   unsigned long parent_rate)
{
	struct fapll_synth *synth = to_synth(hw);
	struct fapll_data *fd = synth->fd;
	unsigned long frac_rate, post_rate = 0;
	u32 post_div_m = 0, v;

	if (ti_fapll_clock_is_bypass(fd) || !synth->div || !rate)
		return -EINVAL;

	/* Produce the rate with just post divider M? */
	frac_rate = ti_fapll_synth_get_frac_rate(hw, parent_rate);
	if (frac_rate < rate) {
		if (!synth->freq)
			return -EINVAL;
	} else {
		post_div_m = DIV_ROUND_UP(frac_rate, rate);
		if (post_div_m && (post_div_m <= SYNTH_MAX_DIV_M))
			post_rate = DIV_ROUND_UP(frac_rate, post_div_m);
		if (!synth->freq && !post_rate)
			return -EINVAL;
	}

	/* Need to recalculate the fractional divider? */
	if ((post_rate != rate) && synth->freq)
		post_div_m = ti_fapll_synth_set_frac_rate(synth,
							  rate,
							  parent_rate);

	v = readl_relaxed(synth->div);
	v &= ~SYNTH_MAX_DIV_M;
	v |= post_div_m;
	v |= SYNTH_LDMDIV1;
	writel_relaxed(v, synth->div);

	return 0;
}

static const struct clk_ops ti_fapll_synt_ops = {
	.enable = ti_fapll_synth_enable,
	.disable = ti_fapll_synth_disable,
	.is_enabled = ti_fapll_synth_is_enabled,
	.recalc_rate = ti_fapll_synth_recalc_rate,
	.round_rate = ti_fapll_synth_round_rate,
	.set_rate = ti_fapll_synth_set_rate,
};

static struct clk * __init ti_fapll_synth_setup(struct fapll_data *fd,
						void __iomem *freq,
						void __iomem *div,
						int index,
						const char *name,
						const char *parent,
						struct clk *pll_clk)
{
	struct clk_init_data *init;
	struct fapll_synth *synth;

	init = kzalloc(sizeof(*init), GFP_KERNEL);
	if (!init)
		return ERR_PTR(-ENOMEM);

	init->ops = &ti_fapll_synt_ops;
	init->name = name;
	init->parent_names = &parent;
	init->num_parents = 1;

	synth = kzalloc(sizeof(*synth), GFP_KERNEL);
	if (!synth)
		goto free;

	synth->fd = fd;
	synth->index = index;
	synth->freq = freq;
	synth->div = div;
	synth->name = name;
	synth->hw.init = init;
	synth->clk_pll = pll_clk;

	return clk_register(NULL, &synth->hw);

free:
	kfree(synth);
	kfree(init);

	return ERR_PTR(-ENOMEM);
}

static void __init ti_fapll_setup(struct device_node *node)
{
	struct fapll_data *fd;
	struct clk_init_data *init = NULL;
	const char *parent_name[2];
	struct clk *pll_clk;
	int i;

	fd = kzalloc(sizeof(*fd), GFP_KERNEL);
	if (!fd)
		return;

	fd->outputs.clks = kzalloc(sizeof(struct clk *) *
				   MAX_FAPLL_OUTPUTS + 1,
				   GFP_KERNEL);
	if (!fd->outputs.clks)
		goto free;

	init = kzalloc(sizeof(*init), GFP_KERNEL);
	if (!init)
		goto free;

	init->ops = &ti_fapll_ops;
	init->name = node->name;

	init->num_parents = of_clk_get_parent_count(node);
	if (init->num_parents != 2) {
		pr_err("%pOFn must have two parents\n", node);
		goto free;
	}

	of_clk_parent_fill(node, parent_name, 2);
	init->parent_names = parent_name;

	fd->clk_ref = of_clk_get(node, 0);
	if (IS_ERR(fd->clk_ref)) {
		pr_err("%pOFn could not get clk_ref\n", node);
		goto free;
	}

	fd->clk_bypass = of_clk_get(node, 1);
	if (IS_ERR(fd->clk_bypass)) {
		pr_err("%pOFn could not get clk_bypass\n", node);
		goto free;
	}

	fd->base = of_iomap(node, 0);
	if (!fd->base) {
		pr_err("%pOFn could not get IO base\n", node);
		goto free;
	}

	if (fapll_is_ddr_pll(fd->base))
		fd->bypass_bit_inverted = true;

	fd->name = node->name;
	fd->hw.init = init;

	/* Register the parent PLL */
	pll_clk = clk_register(NULL, &fd->hw);
	if (IS_ERR(pll_clk))
		goto unmap;

	fd->outputs.clks[0] = pll_clk;
	fd->outputs.clk_num++;

	/*
	 * Set up the child synthesizers starting at index 1 as the
	 * PLL output is at index 0. We need to check the clock-indices
	 * for numbering in case there are holes in the synth mapping,
	 * and then probe the synth register to see if it has a FREQ
	 * register available.
	 */
	for (i = 0; i < MAX_FAPLL_OUTPUTS; i++) {
		const char *output_name;
		void __iomem *freq, *div;
		struct clk *synth_clk;
		int output_instance;
		u32 v;

		if (of_property_read_string_index(node, "clock-output-names",
						  i, &output_name))
			continue;

		if (of_property_read_u32_index(node, "clock-indices", i,
					       &output_instance))
			output_instance = i;

		freq = fd->base + (output_instance * 8);
		div = freq + 4;

		/* Check for hardwired audio_pll_clk1 */
		if (is_audio_pll_clk1(freq)) {
			freq = NULL;
			div = NULL;
		} else {
			/* Does the synthesizer have a FREQ register? */
			v = readl_relaxed(freq);
			if (!v)
				freq = NULL;
		}
		synth_clk = ti_fapll_synth_setup(fd, freq, div, output_instance,
						 output_name, node->name,
						 pll_clk);
		if (IS_ERR(synth_clk))
			continue;

		fd->outputs.clks[output_instance] = synth_clk;
		fd->outputs.clk_num++;

		clk_register_clkdev(synth_clk, output_name, NULL);
	}

	/* Register the child synthesizers as the FAPLL outputs */
	of_clk_add_provider(node, of_clk_src_onecell_get, &fd->outputs);
	/* Add clock alias for the outputs */

	kfree(init);

	return;

unmap:
	iounmap(fd->base);
free:
	if (fd->clk_bypass)
		clk_put(fd->clk_bypass);
	if (fd->clk_ref)
		clk_put(fd->clk_ref);
	kfree(fd->outputs.clks);
	kfree(fd);
	kfree(init);
}

CLK_OF_DECLARE(ti_fapll_clock, "ti,dm816-fapll-clock", ti_fapll_setup);
