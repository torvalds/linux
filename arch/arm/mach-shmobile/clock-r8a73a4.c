/*
 * r8a73a4 clock framework support
 *
 * Copyright (C) 2013  Renesas Solutions Corp.
 * Copyright (C) 2013  Magnus Damm
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/sh_clk.h>
#include <linux/clkdev.h>
#include <mach/clock.h>
#include <mach/common.h>

#define CPG_BASE 0xe6150000
#define CPG_LEN 0x270

#define SMSTPCR2 0xe6150138
#define SMSTPCR3 0xe615013c
#define SMSTPCR5 0xe6150144

#define FRQCRA		0xE6150000
#define FRQCRB		0xE6150004
#define FRQCRC		0xE61500E0
#define VCLKCR1		0xE6150008
#define VCLKCR2		0xE615000C
#define VCLKCR3		0xE615001C
#define VCLKCR4		0xE6150014
#define VCLKCR5		0xE6150034
#define ZBCKCR		0xE6150010
#define SD0CKCR		0xE6150074
#define SD1CKCR		0xE6150078
#define SD2CKCR		0xE615007C
#define MMC0CKCR	0xE6150240
#define MMC1CKCR	0xE6150244
#define FSIACKCR	0xE6150018
#define FSIBCKCR	0xE6150090
#define MPCKCR		0xe6150080
#define SPUVCKCR	0xE6150094
#define HSICKCR		0xE615026C
#define M4CKCR		0xE6150098
#define PLLECR		0xE61500D0
#define PLL0CR		0xE61500D8
#define PLL1CR		0xE6150028
#define PLL2CR		0xE615002C
#define PLL2SCR		0xE61501F4
#define PLL2HCR		0xE61501E4
#define CKSCR		0xE61500C0

#define CPG_MAP(o) ((o - CPG_BASE) + cpg_mapping.base)

static struct clk_mapping cpg_mapping = {
	.phys   = CPG_BASE,
	.len    = CPG_LEN,
};

static struct clk extalr_clk = {
	.rate	= 32768,
	.mapping	= &cpg_mapping,
};

static struct clk extal1_clk = {
	.rate	= 26000000,
	.mapping	= &cpg_mapping,
};

static struct clk extal2_clk = {
	.rate	= 48000000,
	.mapping	= &cpg_mapping,
};

static struct sh_clk_ops followparent_clk_ops = {
	.recalc	= followparent_recalc,
};

static struct clk main_clk = {
	/* .parent will be set r8a73a4_clock_init */
	.ops	= &followparent_clk_ops,
};

SH_CLK_RATIO(div2,	1, 2);
SH_CLK_RATIO(div4,	1, 4);

SH_FIXED_RATIO_CLK(main_div2_clk,	main_clk,		div2);
SH_FIXED_RATIO_CLK(extal1_div2_clk,	extal1_clk,		div2);
SH_FIXED_RATIO_CLK(extal2_div2_clk,	extal2_clk,		div2);
SH_FIXED_RATIO_CLK(extal2_div4_clk,	extal2_clk,		div4);

/* External FSIACK/FSIBCK clock */
static struct clk fsiack_clk = {
};

static struct clk fsibck_clk = {
};

/*
 *		PLL clocks
 */
static struct clk *pll_parent_main[] = {
	[0] = &main_clk,
	[1] = &main_div2_clk
};

static struct clk *pll_parent_main_extal[8] = {
	[0] = &main_div2_clk,
	[1] = &extal2_div2_clk,
	[3] = &extal2_div4_clk,
	[4] = &main_clk,
	[5] = &extal2_clk,
};

static unsigned long pll_recalc(struct clk *clk)
{
	unsigned long mult = 1;

	if (ioread32(CPG_MAP(PLLECR)) & (1 << clk->enable_bit))
		mult = (((ioread32(clk->mapped_reg) >> 24) & 0x7f) + 1);

	return clk->parent->rate * mult;
}

static int pll_set_parent(struct clk *clk, struct clk *parent)
{
	u32 val;
	int i, ret;

	if (!clk->parent_table || !clk->parent_num)
		return -EINVAL;

	/* Search the parent */
	for (i = 0; i < clk->parent_num; i++)
		if (clk->parent_table[i] == parent)
			break;

	if (i == clk->parent_num)
		return -ENODEV;

	ret = clk_reparent(clk, parent);
	if (ret < 0)
		return ret;

	val = ioread32(clk->mapped_reg) &
		~(((1 << clk->src_width) - 1) << clk->src_shift);

	iowrite32(val | i << clk->src_shift, clk->mapped_reg);

	return 0;
}

static struct sh_clk_ops pll_clk_ops = {
	.recalc		= pll_recalc,
	.set_parent	= pll_set_parent,
};

#define PLL_CLOCK(name, p, pt, w, s, reg, e)		\
	static struct clk name = {			\
		.ops		= &pll_clk_ops,		\
		.flags		= CLK_ENABLE_ON_INIT,	\
		.parent		= p,			\
		.parent_table	= pt,			\
		.parent_num	= ARRAY_SIZE(pt),	\
		.src_width	= w,			\
		.src_shift	= s,			\
		.enable_reg	= (void __iomem *)reg,	\
		.enable_bit	= e,			\
		.mapping	= &cpg_mapping,		\
	}

PLL_CLOCK(pll0_clk,  &main_clk,      pll_parent_main,      1, 20, PLL0CR,  0);
PLL_CLOCK(pll1_clk,  &main_clk,      pll_parent_main,       1, 7, PLL1CR,  1);
PLL_CLOCK(pll2_clk,  &main_div2_clk, pll_parent_main_extal, 3, 5, PLL2CR,  2);
PLL_CLOCK(pll2s_clk, &main_div2_clk, pll_parent_main_extal, 3, 5, PLL2SCR, 4);
PLL_CLOCK(pll2h_clk, &main_div2_clk, pll_parent_main_extal, 3, 5, PLL2HCR, 5);

SH_FIXED_RATIO_CLK(pll1_div2_clk,	pll1_clk,	div2);

static atomic_t frqcr_lock;

/* Several clocks need to access FRQCRB, have to lock */
static bool frqcr_kick_check(struct clk *clk)
{
	return !(ioread32(CPG_MAP(FRQCRB)) & BIT(31));
}

static int frqcr_kick_do(struct clk *clk)
{
	int i;

	/* set KICK bit in FRQCRB to update hardware setting, check success */
	iowrite32(ioread32(CPG_MAP(FRQCRB)) | BIT(31), CPG_MAP(FRQCRB));
	for (i = 1000; i; i--)
		if (ioread32(CPG_MAP(FRQCRB)) & BIT(31))
			cpu_relax();
		else
			return 0;

	return -ETIMEDOUT;
}

static int zclk_set_rate(struct clk *clk, unsigned long rate)
{
	void __iomem *frqcrc;
	int ret;
	unsigned long step, p_rate;
	u32 val;

	if (!clk->parent || !__clk_get(clk->parent))
		return -ENODEV;

	if (!atomic_inc_and_test(&frqcr_lock) || !frqcr_kick_check(clk)) {
		ret = -EBUSY;
		goto done;
	}

	frqcrc = clk->mapped_reg + (FRQCRC - (u32)clk->enable_reg);

	p_rate = clk_get_rate(clk->parent);
	if (rate == p_rate) {
		val = 0;
	} else {
		step = DIV_ROUND_CLOSEST(p_rate, 32);
		val = 32 - rate / step;
	}

	iowrite32((ioread32(frqcrc) & ~(clk->div_mask << clk->enable_bit)) |
		  (val << clk->enable_bit), frqcrc);

	ret = frqcr_kick_do(clk);

done:
	atomic_dec(&frqcr_lock);
	__clk_put(clk->parent);
	return ret;
}

static long zclk_round_rate(struct clk *clk, unsigned long rate)
{
	/*
	 * theoretical rate = parent rate * multiplier / 32,
	 * where 1 <= multiplier <= 32. Therefore we should do
	 * multiplier = rate * 32 / parent rate
	 * rounded rate = parent rate * multiplier / 32.
	 * However, multiplication before division won't fit in 32 bits, so
	 * we sacrifice some precision by first dividing and then multiplying.
	 * To find the nearest divisor we calculate both and pick up the best
	 * one. This avoids 64-bit arithmetics.
	 */
	unsigned long step, mul_min, mul_max, rate_min, rate_max;

	rate_max = clk_get_rate(clk->parent);

	/* output freq <= parent */
	if (rate >= rate_max)
		return rate_max;

	step = DIV_ROUND_CLOSEST(rate_max, 32);
	/* output freq >= parent / 32 */
	if (step >= rate)
		return step;

	mul_min = rate / step;
	mul_max = DIV_ROUND_UP(rate, step);
	rate_min = step * mul_min;
	if (mul_max == mul_min)
		return rate_min;

	rate_max = step * mul_max;

	if (rate_max - rate <  rate - rate_min)
		return rate_max;

	return rate_min;
}

static unsigned long zclk_recalc(struct clk *clk)
{
	void __iomem *frqcrc = FRQCRC - (u32)clk->enable_reg + clk->mapped_reg;
	unsigned int max = clk->div_mask + 1;
	unsigned long val = ((ioread32(frqcrc) >> clk->enable_bit) &
			     clk->div_mask);

	return DIV_ROUND_CLOSEST(clk_get_rate(clk->parent), max) *
		(max - val);
}

static struct sh_clk_ops zclk_ops = {
	.recalc = zclk_recalc,
	.set_rate = zclk_set_rate,
	.round_rate = zclk_round_rate,
};

static struct clk z_clk = {
	.parent = &pll0_clk,
	.div_mask = 0x1f,
	.enable_bit = 8,
	/* We'll need to access FRQCRB and FRQCRC */
	.enable_reg = (void __iomem *)FRQCRB,
	.ops = &zclk_ops,
};

static struct clk *main_clks[] = {
	&extalr_clk,
	&extal1_clk,
	&extal1_div2_clk,
	&extal2_clk,
	&extal2_div2_clk,
	&extal2_div4_clk,
	&main_clk,
	&main_div2_clk,
	&fsiack_clk,
	&fsibck_clk,
	&pll0_clk,
	&pll1_clk,
	&pll1_div2_clk,
	&pll2_clk,
	&pll2s_clk,
	&pll2h_clk,
	&z_clk,
};

/* DIV4 */
static void div4_kick(struct clk *clk)
{
	if (!WARN(!atomic_inc_and_test(&frqcr_lock), "FRQCR* lock broken!\n"))
		frqcr_kick_do(clk);
	atomic_dec(&frqcr_lock);
}

static int divisors[] = { 2, 3, 4, 6, 8, 12, 16, 18, 24, 0, 36, 48, 10};

static struct clk_div_mult_table div4_div_mult_table = {
	.divisors	= divisors,
	.nr_divisors	= ARRAY_SIZE(divisors),
};

static struct clk_div4_table div4_table = {
	.div_mult_table	= &div4_div_mult_table,
	.kick		= div4_kick,
};

enum {
	DIV4_I, DIV4_M3, DIV4_B, DIV4_M1, DIV4_M2,
	DIV4_ZX, DIV4_ZS, DIV4_HP,
	DIV4_NR };

static struct clk div4_clks[DIV4_NR] = {
	[DIV4_I]	= SH_CLK_DIV4(&pll1_clk, FRQCRA, 20, 0x0dff, CLK_ENABLE_ON_INIT),
	[DIV4_M3]	= SH_CLK_DIV4(&pll1_clk, FRQCRA, 12, 0x1dff, CLK_ENABLE_ON_INIT),
	[DIV4_B]	= SH_CLK_DIV4(&pll1_clk, FRQCRA,  8, 0x0dff, CLK_ENABLE_ON_INIT),
	[DIV4_M1]	= SH_CLK_DIV4(&pll1_clk, FRQCRA,  4, 0x1dff, 0),
	[DIV4_M2]	= SH_CLK_DIV4(&pll1_clk, FRQCRA,  0, 0x1dff, 0),
	[DIV4_ZX]	= SH_CLK_DIV4(&pll1_clk, FRQCRB, 12, 0x0dff, 0),
	[DIV4_ZS]	= SH_CLK_DIV4(&pll1_clk, FRQCRB,  8, 0x0dff, 0),
	[DIV4_HP]	= SH_CLK_DIV4(&pll1_clk, FRQCRB,  4, 0x0dff, 0),
};

enum {
	DIV6_ZB,
	DIV6_SDHI0, DIV6_SDHI1, DIV6_SDHI2,
	DIV6_MMC0, DIV6_MMC1,
	DIV6_VCK1, DIV6_VCK2, DIV6_VCK3, DIV6_VCK4, DIV6_VCK5,
	DIV6_FSIA, DIV6_FSIB,
	DIV6_MP, DIV6_M4, DIV6_HSI, DIV6_SPUV,
	DIV6_NR };

static struct clk *div6_parents[8] = {
	[0] = &pll1_div2_clk,
	[1] = &pll2s_clk,
	[3] = &extal2_clk,
	[4] = &main_div2_clk,
	[6] = &extalr_clk,
};

static struct clk *fsia_parents[4] = {
	[0] = &pll1_div2_clk,
	[1] = &pll2s_clk,
	[2] = &fsiack_clk,
};

static struct clk *fsib_parents[4] = {
	[0] = &pll1_div2_clk,
	[1] = &pll2s_clk,
	[2] = &fsibck_clk,
};

static struct clk *mp_parents[4] = {
	[0] = &pll1_div2_clk,
	[1] = &pll2s_clk,
	[2] = &extal2_clk,
	[3] = &extal2_clk,
};

static struct clk *m4_parents[2] = {
	[0] = &pll2s_clk,
};

static struct clk *hsi_parents[4] = {
	[0] = &pll2h_clk,
	[1] = &pll1_div2_clk,
	[3] = &pll2s_clk,
};

/*** FIXME ***
 * SH_CLK_DIV6_EXT() macro doesn't care .mapping
 * but, it is necessary on R-Car (= ioremap() base CPG)
 * The difference between
 * SH_CLK_DIV6_EXT() <--> SH_CLK_MAP_DIV6_EXT()
 * is only .mapping
 */
#define SH_CLK_MAP_DIV6_EXT(_reg, _flags, _parents,			\
			    _num_parents, _src_shift, _src_width)	\
{									\
	.enable_reg	= (void __iomem *)_reg,				\
	.enable_bit	= 0, /* unused */				\
	.flags		= _flags | CLK_MASK_DIV_ON_DISABLE,		\
	.div_mask	= SH_CLK_DIV6_MSK,				\
	.parent_table	= _parents,					\
	.parent_num	= _num_parents,					\
	.src_shift	= _src_shift,					\
	.src_width	= _src_width,					\
	.mapping	= &cpg_mapping,					\
}

static struct clk div6_clks[DIV6_NR] = {
	[DIV6_ZB] = SH_CLK_MAP_DIV6_EXT(ZBCKCR, CLK_ENABLE_ON_INIT,
				div6_parents, 2, 7, 1),
	[DIV6_SDHI0] = SH_CLK_MAP_DIV6_EXT(SD0CKCR, 0,
				div6_parents, 2, 6, 2),
	[DIV6_SDHI1] = SH_CLK_MAP_DIV6_EXT(SD1CKCR, 0,
				div6_parents, 2, 6, 2),
	[DIV6_SDHI2] = SH_CLK_MAP_DIV6_EXT(SD2CKCR, 0,
				div6_parents, 2, 6, 2),
	[DIV6_MMC0] = SH_CLK_MAP_DIV6_EXT(MMC0CKCR, 0,
				div6_parents, 2, 6, 2),
	[DIV6_MMC1] = SH_CLK_MAP_DIV6_EXT(MMC1CKCR, 0,
				div6_parents, 2, 6, 2),
	[DIV6_VCK1] = SH_CLK_MAP_DIV6_EXT(VCLKCR1, 0, /* didn't care bit[6-7] */
				div6_parents, ARRAY_SIZE(div6_parents), 12, 3),
	[DIV6_VCK2] = SH_CLK_MAP_DIV6_EXT(VCLKCR2, 0, /* didn't care bit[6-7] */
				div6_parents, ARRAY_SIZE(div6_parents), 12, 3),
	[DIV6_VCK3] = SH_CLK_MAP_DIV6_EXT(VCLKCR3, 0, /* didn't care bit[6-7] */
				div6_parents, ARRAY_SIZE(div6_parents), 12, 3),
	[DIV6_VCK4] = SH_CLK_MAP_DIV6_EXT(VCLKCR4, 0, /* didn't care bit[6-7] */
				div6_parents, ARRAY_SIZE(div6_parents), 12, 3),
	[DIV6_VCK5] = SH_CLK_MAP_DIV6_EXT(VCLKCR5, 0, /* didn't care bit[6-7] */
				div6_parents, ARRAY_SIZE(div6_parents), 12, 3),
	[DIV6_FSIA] = SH_CLK_MAP_DIV6_EXT(FSIACKCR, 0,
				fsia_parents, ARRAY_SIZE(fsia_parents), 6, 2),
	[DIV6_FSIB] = SH_CLK_MAP_DIV6_EXT(FSIBCKCR, 0,
				fsib_parents, ARRAY_SIZE(fsib_parents), 6, 2),
	[DIV6_MP] = SH_CLK_MAP_DIV6_EXT(MPCKCR, 0, /* it needs bit[9-11] control */
				mp_parents, ARRAY_SIZE(mp_parents), 6, 2),
	/* pll2s will be selected always for M4 */
	[DIV6_M4] = SH_CLK_MAP_DIV6_EXT(M4CKCR, 0, /* it needs bit[9] control */
				m4_parents, ARRAY_SIZE(m4_parents), 6, 1),
	[DIV6_HSI] = SH_CLK_MAP_DIV6_EXT(HSICKCR, 0, /* it needs bit[9] control */
				hsi_parents, ARRAY_SIZE(hsi_parents), 6, 2),
	[DIV6_SPUV] = SH_CLK_MAP_DIV6_EXT(SPUVCKCR, 0,
				mp_parents, ARRAY_SIZE(mp_parents), 6, 2),
};

/* MSTP */
enum {
	MSTP217, MSTP216, MSTP207, MSTP206, MSTP204, MSTP203,
	MSTP315, MSTP314, MSTP313, MSTP312, MSTP305,
	MSTP522,
	MSTP_NR
};

static struct clk mstp_clks[MSTP_NR] = {
	[MSTP204] = SH_CLK_MSTP32(&div6_clks[DIV6_MP],	SMSTPCR2, 4, 0), /* SCIFA0 */
	[MSTP203] = SH_CLK_MSTP32(&div6_clks[DIV6_MP],	SMSTPCR2, 3, 0), /* SCIFA1 */
	[MSTP206] = SH_CLK_MSTP32(&div6_clks[DIV6_MP],	SMSTPCR2, 6, 0), /* SCIFB0 */
	[MSTP207] = SH_CLK_MSTP32(&div6_clks[DIV6_MP],	SMSTPCR2, 7, 0), /* SCIFB1 */
	[MSTP216] = SH_CLK_MSTP32(&div6_clks[DIV6_MP],	SMSTPCR2, 16, 0), /* SCIFB2 */
	[MSTP217] = SH_CLK_MSTP32(&div6_clks[DIV6_MP],	SMSTPCR2, 17, 0), /* SCIFB3 */
	[MSTP305] = SH_CLK_MSTP32(&div6_clks[DIV6_MMC1],SMSTPCR3, 5, 0), /* MMCIF1 */
	[MSTP312] = SH_CLK_MSTP32(&div6_clks[DIV6_SDHI2],SMSTPCR3, 12, 0), /* SDHI2 */
	[MSTP313] = SH_CLK_MSTP32(&div6_clks[DIV6_SDHI1],SMSTPCR3, 13, 0), /* SDHI1 */
	[MSTP314] = SH_CLK_MSTP32(&div6_clks[DIV6_SDHI0],SMSTPCR3, 14, 0), /* SDHI0 */
	[MSTP315] = SH_CLK_MSTP32(&div6_clks[DIV6_MMC0],SMSTPCR3, 15, 0), /* MMCIF0 */
	[MSTP522] = SH_CLK_MSTP32(&extal2_clk, SMSTPCR5, 22, 0), /* Thermal */
};

static struct clk_lookup lookups[] = {
	/* main clock */
	CLKDEV_CON_ID("extal1",			&extal1_clk),
	CLKDEV_CON_ID("extal1_div2",		&extal1_div2_clk),
	CLKDEV_CON_ID("extal2",			&extal2_clk),
	CLKDEV_CON_ID("extal2_div2",		&extal2_div2_clk),
	CLKDEV_CON_ID("extal2_div4",		&extal2_div4_clk),
	CLKDEV_CON_ID("fsiack",			&fsiack_clk),
	CLKDEV_CON_ID("fsibck",			&fsibck_clk),

	/* pll clock */
	CLKDEV_CON_ID("pll1",			&pll1_clk),
	CLKDEV_CON_ID("pll1_div2",		&pll1_div2_clk),
	CLKDEV_CON_ID("pll2",			&pll2_clk),
	CLKDEV_CON_ID("pll2s",			&pll2s_clk),
	CLKDEV_CON_ID("pll2h",			&pll2h_clk),

	/* CPU clock */
	CLKDEV_DEV_ID("cpufreq-cpu0",		&z_clk),

	/* DIV6 */
	CLKDEV_CON_ID("zb",			&div6_clks[DIV6_ZB]),
	CLKDEV_CON_ID("vck1",			&div6_clks[DIV6_VCK1]),
	CLKDEV_CON_ID("vck2",			&div6_clks[DIV6_VCK2]),
	CLKDEV_CON_ID("vck3",			&div6_clks[DIV6_VCK3]),
	CLKDEV_CON_ID("vck4",			&div6_clks[DIV6_VCK4]),
	CLKDEV_CON_ID("vck5",			&div6_clks[DIV6_VCK5]),
	CLKDEV_CON_ID("fsia",			&div6_clks[DIV6_FSIA]),
	CLKDEV_CON_ID("fsib",			&div6_clks[DIV6_FSIB]),
	CLKDEV_CON_ID("mp",			&div6_clks[DIV6_MP]),
	CLKDEV_CON_ID("m4",			&div6_clks[DIV6_M4]),
	CLKDEV_CON_ID("hsi",			&div6_clks[DIV6_HSI]),
	CLKDEV_CON_ID("spuv",			&div6_clks[DIV6_SPUV]),

	/* MSTP */
	CLKDEV_DEV_ID("sh-sci.0", &mstp_clks[MSTP204]),
	CLKDEV_DEV_ID("sh-sci.1", &mstp_clks[MSTP203]),
	CLKDEV_DEV_ID("sh-sci.2", &mstp_clks[MSTP206]),
	CLKDEV_DEV_ID("sh-sci.3", &mstp_clks[MSTP207]),
	CLKDEV_DEV_ID("sh-sci.4", &mstp_clks[MSTP216]),
	CLKDEV_DEV_ID("sh-sci.5", &mstp_clks[MSTP217]),
	CLKDEV_DEV_ID("rcar_thermal", &mstp_clks[MSTP522]),
	CLKDEV_DEV_ID("sh_mmcif.1", &mstp_clks[MSTP305]),
	CLKDEV_DEV_ID("ee220000.mmcif", &mstp_clks[MSTP305]),
	CLKDEV_DEV_ID("sh_mobile_sdhi.2", &mstp_clks[MSTP312]),
	CLKDEV_DEV_ID("ee140000.sdhi", &mstp_clks[MSTP312]),
	CLKDEV_DEV_ID("sh_mobile_sdhi.1", &mstp_clks[MSTP313]),
	CLKDEV_DEV_ID("ee120000.sdhi", &mstp_clks[MSTP313]),
	CLKDEV_DEV_ID("sh_mobile_sdhi.0", &mstp_clks[MSTP314]),
	CLKDEV_DEV_ID("ee100000.sdhi", &mstp_clks[MSTP314]),
	CLKDEV_DEV_ID("sh_mmcif.0", &mstp_clks[MSTP315]),
	CLKDEV_DEV_ID("ee200000.mmcif", &mstp_clks[MSTP315]),

	/* for DT */
	CLKDEV_DEV_ID("e61f0000.thermal", &mstp_clks[MSTP522]),
};

void __init r8a73a4_clock_init(void)
{
	void __iomem *reg;
	int k, ret = 0;
	u32 ckscr;

	atomic_set(&frqcr_lock, -1);

	reg = ioremap_nocache(CKSCR, PAGE_SIZE);
	BUG_ON(!reg);
	ckscr = ioread32(reg);
	iounmap(reg);

	switch ((ckscr >> 28) & 0x3) {
	case 0:
		main_clk.parent = &extal1_clk;
		break;
	case 1:
		main_clk.parent = &extal1_div2_clk;
		break;
	case 2:
		main_clk.parent = &extal2_clk;
		break;
	case 3:
		main_clk.parent = &extal2_div2_clk;
		break;
	}

	for (k = 0; !ret && (k < ARRAY_SIZE(main_clks)); k++)
		ret = clk_register(main_clks[k]);

	if (!ret)
		ret = sh_clk_div4_register(div4_clks, DIV4_NR, &div4_table);

	if (!ret)
		ret = sh_clk_div6_reparent_register(div6_clks, DIV6_NR);

	if (!ret)
		ret = sh_clk_mstp_register(mstp_clks, MSTP_NR);

	clkdev_add_table(lookups, ARRAY_SIZE(lookups));

	if (!ret)
		shmobile_clk_init();
	else
		panic("failed to setup r8a73a4 clocks\n");
}
