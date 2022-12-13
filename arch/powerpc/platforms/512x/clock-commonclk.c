// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013 DENX Software Engineering
 *
 * Gerhard Sittig, <gsi@denx.de>
 *
 * common clock driver support for the MPC512x platform
 */

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>

#include <asm/mpc5121.h>
#include <dt-bindings/clock/mpc512x-clock.h>

#include "mpc512x.h"		/* our public mpc5121_clk_init() API */

/* helpers to keep the MCLK intermediates "somewhere" in our table */
enum {
	MCLK_IDX_MUX0,
	MCLK_IDX_EN0,
	MCLK_IDX_DIV0,
	MCLK_MAX_IDX,
};

#define NR_PSCS			12
#define NR_MSCANS		4
#define NR_SPDIFS		1
#define NR_OUTCLK		4
#define NR_MCLKS		(NR_PSCS + NR_MSCANS + NR_SPDIFS + NR_OUTCLK)

/* extend the public set of clocks by adding internal slots for management */
enum {
	/* arrange for adjacent numbers after the public set */
	MPC512x_CLK_START_PRIVATE = MPC512x_CLK_LAST_PUBLIC,
	/* clocks which aren't announced to the public */
	MPC512x_CLK_DDR,
	MPC512x_CLK_MEM,
	MPC512x_CLK_IIM,
	/* intermediates in div+gate combos or fractional dividers */
	MPC512x_CLK_DDR_UG,
	MPC512x_CLK_SDHC_x4,
	MPC512x_CLK_SDHC_UG,
	MPC512x_CLK_SDHC2_UG,
	MPC512x_CLK_DIU_x4,
	MPC512x_CLK_DIU_UG,
	MPC512x_CLK_MBX_BUS_UG,
	MPC512x_CLK_MBX_UG,
	MPC512x_CLK_MBX_3D_UG,
	MPC512x_CLK_PCI_UG,
	MPC512x_CLK_NFC_UG,
	MPC512x_CLK_LPC_UG,
	MPC512x_CLK_SPDIF_TX_IN,
	/* intermediates for the mux+gate+div+mux MCLK generation */
	MPC512x_CLK_MCLKS_FIRST,
	MPC512x_CLK_MCLKS_LAST = MPC512x_CLK_MCLKS_FIRST
				+ NR_MCLKS * MCLK_MAX_IDX,
	/* internal, symbolic spec for the number of slots */
	MPC512x_CLK_LAST_PRIVATE,
};

/* data required for the OF clock provider registration */
static struct clk *clks[MPC512x_CLK_LAST_PRIVATE];
static struct clk_onecell_data clk_data;

/* CCM register access */
static struct mpc512x_ccm __iomem *clkregs;
static DEFINE_SPINLOCK(clklock);

/* SoC variants {{{ */

/*
 * tell SoC variants apart as they are rather similar yet not identical,
 * cache the result in an enum to not repeatedly run the expensive OF test
 *
 * MPC5123 is an MPC5121 without the MBX graphics accelerator
 *
 * MPC5125 has many more differences: no MBX, no AXE, no VIU, no SPDIF,
 * no PATA, no SATA, no PCI, two FECs (of different compatibility name),
 * only 10 PSCs (of different compatibility name), two SDHCs, different
 * NFC IP block, output clocks, system PLL status query, different CPMF
 * interpretation, no CFM, different fourth PSC/CAN mux0 input -- yet
 * those differences can get folded into this clock provider support
 * code and don't warrant a separate highly redundant implementation
 */

static enum soc_type {
	MPC512x_SOC_MPC5121,
	MPC512x_SOC_MPC5123,
	MPC512x_SOC_MPC5125,
} soc;

static void __init mpc512x_clk_determine_soc(void)
{
	if (of_machine_is_compatible("fsl,mpc5121")) {
		soc = MPC512x_SOC_MPC5121;
		return;
	}
	if (of_machine_is_compatible("fsl,mpc5123")) {
		soc = MPC512x_SOC_MPC5123;
		return;
	}
	if (of_machine_is_compatible("fsl,mpc5125")) {
		soc = MPC512x_SOC_MPC5125;
		return;
	}
}

static bool __init soc_has_mbx(void)
{
	if (soc == MPC512x_SOC_MPC5121)
		return true;
	return false;
}

static bool __init soc_has_axe(void)
{
	if (soc == MPC512x_SOC_MPC5125)
		return false;
	return true;
}

static bool __init soc_has_viu(void)
{
	if (soc == MPC512x_SOC_MPC5125)
		return false;
	return true;
}

static bool __init soc_has_spdif(void)
{
	if (soc == MPC512x_SOC_MPC5125)
		return false;
	return true;
}

static bool __init soc_has_pata(void)
{
	if (soc == MPC512x_SOC_MPC5125)
		return false;
	return true;
}

static bool __init soc_has_sata(void)
{
	if (soc == MPC512x_SOC_MPC5125)
		return false;
	return true;
}

static bool __init soc_has_pci(void)
{
	if (soc == MPC512x_SOC_MPC5125)
		return false;
	return true;
}

static bool __init soc_has_fec2(void)
{
	if (soc == MPC512x_SOC_MPC5125)
		return true;
	return false;
}

static int __init soc_max_pscnum(void)
{
	if (soc == MPC512x_SOC_MPC5125)
		return 10;
	return 12;
}

static bool __init soc_has_sdhc2(void)
{
	if (soc == MPC512x_SOC_MPC5125)
		return true;
	return false;
}

static bool __init soc_has_nfc_5125(void)
{
	if (soc == MPC512x_SOC_MPC5125)
		return true;
	return false;
}

static bool __init soc_has_outclk(void)
{
	if (soc == MPC512x_SOC_MPC5125)
		return true;
	return false;
}

static bool __init soc_has_cpmf_0_bypass(void)
{
	if (soc == MPC512x_SOC_MPC5125)
		return true;
	return false;
}

static bool __init soc_has_mclk_mux0_canin(void)
{
	if (soc == MPC512x_SOC_MPC5125)
		return true;
	return false;
}

/* }}} SoC variants */
/* common clk API wrappers {{{ */

/* convenience wrappers around the common clk API */
static inline struct clk *mpc512x_clk_fixed(const char *name, int rate)
{
	return clk_register_fixed_rate(NULL, name, NULL, 0, rate);
}

static inline struct clk *mpc512x_clk_factor(
	const char *name, const char *parent_name,
	int mul, int div)
{
	int clkflags;

	clkflags = CLK_SET_RATE_PARENT;
	return clk_register_fixed_factor(NULL, name, parent_name, clkflags,
					 mul, div);
}

static inline struct clk *mpc512x_clk_divider(
	const char *name, const char *parent_name, u8 clkflags,
	u32 __iomem *reg, u8 pos, u8 len, int divflags)
{
	divflags |= CLK_DIVIDER_BIG_ENDIAN;
	return clk_register_divider(NULL, name, parent_name, clkflags,
				    reg, pos, len, divflags, &clklock);
}

static inline struct clk *mpc512x_clk_divtable(
	const char *name, const char *parent_name,
	u32 __iomem *reg, u8 pos, u8 len,
	const struct clk_div_table *divtab)
{
	u8 divflags;

	divflags = CLK_DIVIDER_BIG_ENDIAN;
	return clk_register_divider_table(NULL, name, parent_name, 0,
					  reg, pos, len, divflags,
					  divtab, &clklock);
}

static inline struct clk *mpc512x_clk_gated(
	const char *name, const char *parent_name,
	u32 __iomem *reg, u8 pos)
{
	int clkflags;
	u8 gateflags;

	clkflags = CLK_SET_RATE_PARENT;
	gateflags = CLK_GATE_BIG_ENDIAN;
	return clk_register_gate(NULL, name, parent_name, clkflags,
				 reg, pos, gateflags, &clklock);
}

static inline struct clk *mpc512x_clk_muxed(const char *name,
	const char **parent_names, int parent_count,
	u32 __iomem *reg, u8 pos, u8 len)
{
	int clkflags;
	u8 muxflags;

	clkflags = CLK_SET_RATE_PARENT;
	muxflags = CLK_MUX_BIG_ENDIAN;
	return clk_register_mux(NULL, name,
				parent_names, parent_count, clkflags,
				reg, pos, len, muxflags, &clklock);
}

/* }}} common clk API wrappers */

/* helper to isolate a bit field from a register */
static inline int get_bit_field(uint32_t __iomem *reg, uint8_t pos, uint8_t len)
{
	uint32_t val;

	val = in_be32(reg);
	val >>= pos;
	val &= (1 << len) - 1;
	return val;
}

/* get the SPMF and translate it into the "sys pll" multiplier */
static int __init get_spmf_mult(void)
{
	static int spmf_to_mult[] = {
		68, 1, 12, 16, 20, 24, 28, 32,
		36, 40, 44, 48, 52, 56, 60, 64,
	};
	int spmf;

	spmf = get_bit_field(&clkregs->spmr, 24, 4);
	return spmf_to_mult[spmf];
}

/*
 * get the SYS_DIV value and translate it into a divide factor
 *
 * values returned from here are a multiple of the real factor since the
 * divide ratio is fractional
 */
static int __init get_sys_div_x2(void)
{
	static int sysdiv_code_to_x2[] = {
		4, 5, 6, 7, 8, 9, 10, 14,
		12, 16, 18, 22, 20, 24, 26, 30,
		28, 32, 34, 38, 36, 40, 42, 46,
		44, 48, 50, 54, 52, 56, 58, 62,
		60, 64, 66,
	};
	int divcode;

	divcode = get_bit_field(&clkregs->scfr2, 26, 6);
	return sysdiv_code_to_x2[divcode];
}

/*
 * get the CPMF value and translate it into a multiplier factor
 *
 * values returned from here are a multiple of the real factor since the
 * multiplier ratio is fractional
 */
static int __init get_cpmf_mult_x2(void)
{
	static int cpmf_to_mult_x36[] = {
		/* 0b000 is "times 36" */
		72, 2, 2, 3, 4, 5, 6, 7,
	};
	static int cpmf_to_mult_0by[] = {
		/* 0b000 is "bypass" */
		2, 2, 2, 3, 4, 5, 6, 7,
	};

	int *cpmf_to_mult;
	int cpmf;

	cpmf = get_bit_field(&clkregs->spmr, 16, 4);
	if (soc_has_cpmf_0_bypass())
		cpmf_to_mult = cpmf_to_mult_0by;
	else
		cpmf_to_mult = cpmf_to_mult_x36;
	return cpmf_to_mult[cpmf];
}

/*
 * some of the clock dividers do scale in a linear way, yet not all of
 * their bit combinations are legal; use a divider table to get a
 * resulting set of applicable divider values
 */

/* applies to the IPS_DIV, and PCI_DIV values */
static const struct clk_div_table divtab_2346[] = {
	{ .val = 2, .div = 2, },
	{ .val = 3, .div = 3, },
	{ .val = 4, .div = 4, },
	{ .val = 6, .div = 6, },
	{ .div = 0, },
};

/* applies to the MBX_DIV, LPC_DIV, and NFC_DIV values */
static const struct clk_div_table divtab_1234[] = {
	{ .val = 1, .div = 1, },
	{ .val = 2, .div = 2, },
	{ .val = 3, .div = 3, },
	{ .val = 4, .div = 4, },
	{ .div = 0, },
};

static int __init get_freq_from_dt(char *propname)
{
	struct device_node *np;
	const unsigned int *prop;
	int val;

	val = 0;
	np = of_find_compatible_node(NULL, NULL, "fsl,mpc5121-immr");
	if (np) {
		prop = of_get_property(np, propname, NULL);
		if (prop)
			val = *prop;
	    of_node_put(np);
	}
	return val;
}

static void __init mpc512x_clk_preset_data(void)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(clks); i++)
		clks[i] = ERR_PTR(-ENODEV);
}

/*
 * - receives the "bus frequency" from the caller (that's the IPS clock
 *   rate, the historical source of clock information)
 * - fetches the system PLL multiplier and divider values as well as the
 *   IPS divider value from hardware
 * - determines the REF clock rate either from the XTAL/OSC spec (if
 *   there is a device tree node describing the oscillator) or from the
 *   IPS bus clock (supported for backwards compatibility, such that
 *   setups without XTAL/OSC specs keep working)
 * - creates the "ref" clock item in the clock tree, such that
 *   subsequent code can create the remainder of the hierarchy (REF ->
 *   SYS -> CSB -> IPS) from the REF clock rate and the returned mul/div
 *   values
 */
static void __init mpc512x_clk_setup_ref_clock(struct device_node *np, int bus_freq,
					int *sys_mul, int *sys_div,
					int *ips_div)
{
	struct clk *osc_clk;
	int calc_freq;

	/* fetch mul/div factors from the hardware */
	*sys_mul = get_spmf_mult();
	*sys_mul *= 2;		/* compensate for the fractional divider */
	*sys_div = get_sys_div_x2();
	*ips_div = get_bit_field(&clkregs->scfr1, 23, 3);

	/* lookup the oscillator clock for its rate */
	osc_clk = of_clk_get_by_name(np, "osc");

	/*
	 * either descend from OSC to REF (and in bypassing verify the
	 * IPS rate), or backtrack from IPS and multiplier values that
	 * were fetched from hardware to REF and thus to the OSC value
	 *
	 * in either case the REF clock gets created here and the
	 * remainder of the clock tree can get spanned from there
	 */
	if (!IS_ERR(osc_clk)) {
		clks[MPC512x_CLK_REF] = mpc512x_clk_factor("ref", "osc", 1, 1);
		calc_freq = clk_get_rate(clks[MPC512x_CLK_REF]);
		calc_freq *= *sys_mul;
		calc_freq /= *sys_div;
		calc_freq /= 2;
		calc_freq /= *ips_div;
		if (bus_freq && calc_freq != bus_freq)
			pr_warn("calc rate %d != OF spec %d\n",
				calc_freq, bus_freq);
	} else {
		calc_freq = bus_freq;	/* start with IPS */
		calc_freq *= *ips_div;	/* IPS -> CSB */
		calc_freq *= 2;		/* CSB -> SYS */
		calc_freq *= *sys_div;	/* SYS -> PLL out */
		calc_freq /= *sys_mul;	/* PLL out -> REF == OSC */
		clks[MPC512x_CLK_REF] = mpc512x_clk_fixed("ref", calc_freq);
	}
}

/* MCLK helpers {{{ */

/*
 * helper code for the MCLK subtree setup
 *
 * the overview in section 5.2.4 of the MPC5121e Reference Manual rev4
 * suggests that all instances of the "PSC clock generation" are equal,
 * and that one might re-use the PSC setup for MSCAN clock generation
 * (section 5.2.5) as well, at least the logic if not the data for
 * description
 *
 * the details (starting at page 5-20) show differences in the specific
 * inputs of the first mux stage ("can clk in", "spdif tx"), and the
 * factual non-availability of the second mux stage (it's present yet
 * only one input is valid)
 *
 * the MSCAN clock related registers (starting at page 5-35) all
 * reference "spdif clk" at the first mux stage and don't mention any
 * "can clk" at all, which somehow is unexpected
 *
 * TODO re-check the document, and clarify whether the RM is correct in
 * the overview or in the details, and whether the difference is a
 * clipboard induced error or results from chip revisions
 *
 * it turns out that the RM rev4 as of 2012-06 talks about "can" for the
 * PSCs while RM rev3 as of 2008-10 talks about "spdif", so I guess that
 * first a doc update is required which better reflects reality in the
 * SoC before the implementation should follow while no questions remain
 */

/*
 * note that this declaration raises a checkpatch warning, but
 * it's the very data type dictated by <linux/clk-provider.h>,
 * "fixing" this warning will break compilation
 */
static const char *parent_names_mux0_spdif[] = {
	"sys", "ref", "psc-mclk-in", "spdif-tx",
};

static const char *parent_names_mux0_canin[] = {
	"sys", "ref", "psc-mclk-in", "can-clk-in",
};

enum mclk_type {
	MCLK_TYPE_PSC,
	MCLK_TYPE_MSCAN,
	MCLK_TYPE_SPDIF,
	MCLK_TYPE_OUTCLK,
};

struct mclk_setup_data {
	enum mclk_type type;
	bool has_mclk1;
	const char *name_mux0;
	const char *name_en0;
	const char *name_div0;
	const char *parent_names_mux1[2];
	const char *name_mclk;
};

#define MCLK_SETUP_DATA_PSC(id) { \
	MCLK_TYPE_PSC, 0, \
	"psc" #id "-mux0", \
	"psc" #id "-en0", \
	"psc" #id "_mclk_div", \
	{ "psc" #id "_mclk_div", "dummy", }, \
	"psc" #id "_mclk", \
}

#define MCLK_SETUP_DATA_MSCAN(id) { \
	MCLK_TYPE_MSCAN, 0, \
	"mscan" #id "-mux0", \
	"mscan" #id "-en0", \
	"mscan" #id "_mclk_div", \
	{ "mscan" #id "_mclk_div", "dummy", }, \
	"mscan" #id "_mclk", \
}

#define MCLK_SETUP_DATA_SPDIF { \
	MCLK_TYPE_SPDIF, 1, \
	"spdif-mux0", \
	"spdif-en0", \
	"spdif_mclk_div", \
	{ "spdif_mclk_div", "spdif-rx", }, \
	"spdif_mclk", \
}

#define MCLK_SETUP_DATA_OUTCLK(id) { \
	MCLK_TYPE_OUTCLK, 0, \
	"out" #id "-mux0", \
	"out" #id "-en0", \
	"out" #id "_mclk_div", \
	{ "out" #id "_mclk_div", "dummy", }, \
	"out" #id "_clk", \
}

static struct mclk_setup_data mclk_psc_data[] = {
	MCLK_SETUP_DATA_PSC(0),
	MCLK_SETUP_DATA_PSC(1),
	MCLK_SETUP_DATA_PSC(2),
	MCLK_SETUP_DATA_PSC(3),
	MCLK_SETUP_DATA_PSC(4),
	MCLK_SETUP_DATA_PSC(5),
	MCLK_SETUP_DATA_PSC(6),
	MCLK_SETUP_DATA_PSC(7),
	MCLK_SETUP_DATA_PSC(8),
	MCLK_SETUP_DATA_PSC(9),
	MCLK_SETUP_DATA_PSC(10),
	MCLK_SETUP_DATA_PSC(11),
};

static struct mclk_setup_data mclk_mscan_data[] = {
	MCLK_SETUP_DATA_MSCAN(0),
	MCLK_SETUP_DATA_MSCAN(1),
	MCLK_SETUP_DATA_MSCAN(2),
	MCLK_SETUP_DATA_MSCAN(3),
};

static struct mclk_setup_data mclk_spdif_data[] = {
	MCLK_SETUP_DATA_SPDIF,
};

static struct mclk_setup_data mclk_outclk_data[] = {
	MCLK_SETUP_DATA_OUTCLK(0),
	MCLK_SETUP_DATA_OUTCLK(1),
	MCLK_SETUP_DATA_OUTCLK(2),
	MCLK_SETUP_DATA_OUTCLK(3),
};

/* setup the MCLK clock subtree of an individual PSC/MSCAN/SPDIF */
static void __init mpc512x_clk_setup_mclk(struct mclk_setup_data *entry, size_t idx)
{
	size_t clks_idx_pub, clks_idx_int;
	u32 __iomem *mccr_reg;	/* MCLK control register (mux, en, div) */
	int div;

	/* derive a few parameters from the component type and index */
	switch (entry->type) {
	case MCLK_TYPE_PSC:
		clks_idx_pub = MPC512x_CLK_PSC0_MCLK + idx;
		clks_idx_int = MPC512x_CLK_MCLKS_FIRST
			     + (idx) * MCLK_MAX_IDX;
		mccr_reg = &clkregs->psc_ccr[idx];
		break;
	case MCLK_TYPE_MSCAN:
		clks_idx_pub = MPC512x_CLK_MSCAN0_MCLK + idx;
		clks_idx_int = MPC512x_CLK_MCLKS_FIRST
			     + (NR_PSCS + idx) * MCLK_MAX_IDX;
		mccr_reg = &clkregs->mscan_ccr[idx];
		break;
	case MCLK_TYPE_SPDIF:
		clks_idx_pub = MPC512x_CLK_SPDIF_MCLK;
		clks_idx_int = MPC512x_CLK_MCLKS_FIRST
			     + (NR_PSCS + NR_MSCANS) * MCLK_MAX_IDX;
		mccr_reg = &clkregs->spccr;
		break;
	case MCLK_TYPE_OUTCLK:
		clks_idx_pub = MPC512x_CLK_OUT0_CLK + idx;
		clks_idx_int = MPC512x_CLK_MCLKS_FIRST
			     + (NR_PSCS + NR_MSCANS + NR_SPDIFS + idx)
			     * MCLK_MAX_IDX;
		mccr_reg = &clkregs->out_ccr[idx];
		break;
	default:
		return;
	}

	/*
	 * this was grabbed from the PPC_CLOCK implementation, which
	 * enforced a specific MCLK divider while the clock was gated
	 * during setup (that's a documented hardware requirement)
	 *
	 * the PPC_CLOCK implementation might even have violated the
	 * "MCLK <= IPS" constraint, the fixed divider value of 1
	 * results in a divider of 2 and thus MCLK = SYS/2 which equals
	 * CSB which is greater than IPS; the serial port setup may have
	 * adjusted the divider which the clock setup might have left in
	 * an undesirable state
	 *
	 * initial setup is:
	 * - MCLK 0 from SYS
	 * - MCLK DIV such to not exceed the IPS clock
	 * - MCLK 0 enabled
	 * - MCLK 1 from MCLK DIV
	 */
	div = clk_get_rate(clks[MPC512x_CLK_SYS]);
	div /= clk_get_rate(clks[MPC512x_CLK_IPS]);
	out_be32(mccr_reg, (0 << 16));
	out_be32(mccr_reg, (0 << 16) | ((div - 1) << 17));
	out_be32(mccr_reg, (1 << 16) | ((div - 1) << 17));

	/*
	 * create the 'struct clk' items of the MCLK's clock subtree
	 *
	 * note that by design we always create all nodes and won't take
	 * shortcuts here, because
	 * - the "internal" MCLK_DIV and MCLK_OUT signal in turn are
	 *   selectable inputs to the CFM while those who "actually use"
	 *   the PSC/MSCAN/SPDIF (serial drivers et al) need the MCLK
	 *   for their bitrate
	 * - in the absence of "aliases" for clocks we need to create
	 *   individual 'struct clk' items for whatever might get
	 *   referenced or looked up, even if several of those items are
	 *   identical from the logical POV (their rate value)
	 * - for easier future maintenance and for better reflection of
	 *   the SoC's documentation, it appears appropriate to generate
	 *   clock items even for those muxers which actually are NOPs
	 *   (those with two inputs of which one is reserved)
	 */
	clks[clks_idx_int + MCLK_IDX_MUX0] = mpc512x_clk_muxed(
			entry->name_mux0,
			soc_has_mclk_mux0_canin()
				? &parent_names_mux0_canin[0]
				: &parent_names_mux0_spdif[0],
			ARRAY_SIZE(parent_names_mux0_spdif),
			mccr_reg, 14, 2);
	clks[clks_idx_int + MCLK_IDX_EN0] = mpc512x_clk_gated(
			entry->name_en0, entry->name_mux0,
			mccr_reg, 16);
	clks[clks_idx_int + MCLK_IDX_DIV0] = mpc512x_clk_divider(
			entry->name_div0,
			entry->name_en0, CLK_SET_RATE_GATE,
			mccr_reg, 17, 15, 0);
	if (entry->has_mclk1) {
		clks[clks_idx_pub] = mpc512x_clk_muxed(
				entry->name_mclk,
				&entry->parent_names_mux1[0],
				ARRAY_SIZE(entry->parent_names_mux1),
				mccr_reg, 7, 1);
	} else {
		clks[clks_idx_pub] = mpc512x_clk_factor(
				entry->name_mclk,
				entry->parent_names_mux1[0],
				1, 1);
	}
}

/* }}} MCLK helpers */

static void __init mpc512x_clk_setup_clock_tree(struct device_node *np, int busfreq)
{
	int sys_mul, sys_div, ips_div;
	int mul, div;
	size_t mclk_idx;
	int freq;

	/*
	 * developer's notes:
	 * - consider whether to handle clocks which have both gates and
	 *   dividers via intermediates or by means of composites
	 * - fractional dividers appear to not map well to composites
	 *   since they can be seen as a fixed multiplier and an
	 *   adjustable divider, while composites can only combine at
	 *   most one of a mux, div, and gate each into one 'struct clk'
	 *   item
	 * - PSC/MSCAN/SPDIF clock generation OTOH already is very
	 *   specific and cannot get mapped to composites (at least not
	 *   a single one, maybe two of them, but then some of these
	 *   intermediate clock signals get referenced elsewhere (e.g.
	 *   in the clock frequency measurement, CFM) and thus need
	 *   publicly available names
	 * - the current source layout appropriately reflects the
	 *   hardware setup, and it works, so it's questionable whether
	 *   further changes will result in big enough a benefit
	 */

	/* regardless of whether XTAL/OSC exists, have REF created */
	mpc512x_clk_setup_ref_clock(np, busfreq, &sys_mul, &sys_div, &ips_div);

	/* now setup the REF -> SYS -> CSB -> IPS hierarchy */
	clks[MPC512x_CLK_SYS] = mpc512x_clk_factor("sys", "ref",
						   sys_mul, sys_div);
	clks[MPC512x_CLK_CSB] = mpc512x_clk_factor("csb", "sys", 1, 2);
	clks[MPC512x_CLK_IPS] = mpc512x_clk_divtable("ips", "csb",
						     &clkregs->scfr1, 23, 3,
						     divtab_2346);
	/* now setup anything below SYS and CSB and IPS */

	clks[MPC512x_CLK_DDR_UG] = mpc512x_clk_factor("ddr-ug", "sys", 1, 2);

	/*
	 * the Reference Manual discusses that for SDHC only even divide
	 * ratios are supported because clock domain synchronization
	 * between 'per' and 'ipg' is broken;
	 * keep the divider's bit 0 cleared (per reset value), and only
	 * allow to setup the divider's bits 7:1, which results in that
	 * only even divide ratios can get configured upon rate changes;
	 * keep the "x4" name because this bit shift hack is an internal
	 * implementation detail, the "fractional divider with quarters"
	 * semantics remains
	 */
	clks[MPC512x_CLK_SDHC_x4] = mpc512x_clk_factor("sdhc-x4", "csb", 2, 1);
	clks[MPC512x_CLK_SDHC_UG] = mpc512x_clk_divider("sdhc-ug", "sdhc-x4", 0,
							&clkregs->scfr2, 1, 7,
							CLK_DIVIDER_ONE_BASED);
	if (soc_has_sdhc2()) {
		clks[MPC512x_CLK_SDHC2_UG] = mpc512x_clk_divider(
				"sdhc2-ug", "sdhc-x4", 0, &clkregs->scfr2,
				9, 7, CLK_DIVIDER_ONE_BASED);
	}

	clks[MPC512x_CLK_DIU_x4] = mpc512x_clk_factor("diu-x4", "csb", 4, 1);
	clks[MPC512x_CLK_DIU_UG] = mpc512x_clk_divider("diu-ug", "diu-x4", 0,
						       &clkregs->scfr1, 0, 8,
						       CLK_DIVIDER_ONE_BASED);

	/*
	 * the "power architecture PLL" was setup from data which was
	 * sampled from the reset config word, at this point in time the
	 * configuration can be considered fixed and read only (i.e. no
	 * longer adjustable, or no longer in need of adjustment), which
	 * is why we don't register a PLL here but assume fixed factors
	 */
	mul = get_cpmf_mult_x2();
	div = 2;	/* compensate for the fractional factor */
	clks[MPC512x_CLK_E300] = mpc512x_clk_factor("e300", "csb", mul, div);

	if (soc_has_mbx()) {
		clks[MPC512x_CLK_MBX_BUS_UG] = mpc512x_clk_factor(
				"mbx-bus-ug", "csb", 1, 2);
		clks[MPC512x_CLK_MBX_UG] = mpc512x_clk_divtable(
				"mbx-ug", "mbx-bus-ug", &clkregs->scfr1,
				14, 3, divtab_1234);
		clks[MPC512x_CLK_MBX_3D_UG] = mpc512x_clk_factor(
				"mbx-3d-ug", "mbx-ug", 1, 1);
	}
	if (soc_has_pci()) {
		clks[MPC512x_CLK_PCI_UG] = mpc512x_clk_divtable(
				"pci-ug", "csb", &clkregs->scfr1,
				20, 3, divtab_2346);
	}
	if (soc_has_nfc_5125()) {
		/*
		 * XXX TODO implement 5125 NFC clock setup logic,
		 * with high/low period counters in clkregs->scfr3,
		 * currently there are no users so it's ENOIMPL
		 */
		clks[MPC512x_CLK_NFC_UG] = ERR_PTR(-ENOTSUPP);
	} else {
		clks[MPC512x_CLK_NFC_UG] = mpc512x_clk_divtable(
				"nfc-ug", "ips", &clkregs->scfr1,
				8, 3, divtab_1234);
	}
	clks[MPC512x_CLK_LPC_UG] = mpc512x_clk_divtable("lpc-ug", "ips",
							&clkregs->scfr1, 11, 3,
							divtab_1234);

	clks[MPC512x_CLK_LPC] = mpc512x_clk_gated("lpc", "lpc-ug",
						  &clkregs->sccr1, 30);
	clks[MPC512x_CLK_NFC] = mpc512x_clk_gated("nfc", "nfc-ug",
						  &clkregs->sccr1, 29);
	if (soc_has_pata()) {
		clks[MPC512x_CLK_PATA] = mpc512x_clk_gated(
				"pata", "ips", &clkregs->sccr1, 28);
	}
	/* for PSCs there is a "registers" gate and a bitrate MCLK subtree */
	for (mclk_idx = 0; mclk_idx < soc_max_pscnum(); mclk_idx++) {
		char name[12];
		snprintf(name, sizeof(name), "psc%d", mclk_idx);
		clks[MPC512x_CLK_PSC0 + mclk_idx] = mpc512x_clk_gated(
				name, "ips", &clkregs->sccr1, 27 - mclk_idx);
		mpc512x_clk_setup_mclk(&mclk_psc_data[mclk_idx], mclk_idx);
	}
	clks[MPC512x_CLK_PSC_FIFO] = mpc512x_clk_gated("psc-fifo", "ips",
						       &clkregs->sccr1, 15);
	if (soc_has_sata()) {
		clks[MPC512x_CLK_SATA] = mpc512x_clk_gated(
				"sata", "ips", &clkregs->sccr1, 14);
	}
	clks[MPC512x_CLK_FEC] = mpc512x_clk_gated("fec", "ips",
						  &clkregs->sccr1, 13);
	if (soc_has_pci()) {
		clks[MPC512x_CLK_PCI] = mpc512x_clk_gated(
				"pci", "pci-ug", &clkregs->sccr1, 11);
	}
	clks[MPC512x_CLK_DDR] = mpc512x_clk_gated("ddr", "ddr-ug",
						  &clkregs->sccr1, 10);
	if (soc_has_fec2()) {
		clks[MPC512x_CLK_FEC2] = mpc512x_clk_gated(
				"fec2", "ips", &clkregs->sccr1, 9);
	}

	clks[MPC512x_CLK_DIU] = mpc512x_clk_gated("diu", "diu-ug",
						  &clkregs->sccr2, 31);
	if (soc_has_axe()) {
		clks[MPC512x_CLK_AXE] = mpc512x_clk_gated(
				"axe", "csb", &clkregs->sccr2, 30);
	}
	clks[MPC512x_CLK_MEM] = mpc512x_clk_gated("mem", "ips",
						  &clkregs->sccr2, 29);
	clks[MPC512x_CLK_USB1] = mpc512x_clk_gated("usb1", "csb",
						   &clkregs->sccr2, 28);
	clks[MPC512x_CLK_USB2] = mpc512x_clk_gated("usb2", "csb",
						   &clkregs->sccr2, 27);
	clks[MPC512x_CLK_I2C] = mpc512x_clk_gated("i2c", "ips",
						  &clkregs->sccr2, 26);
	/* MSCAN differs from PSC with just one gate for multiple components */
	clks[MPC512x_CLK_BDLC] = mpc512x_clk_gated("bdlc", "ips",
						   &clkregs->sccr2, 25);
	for (mclk_idx = 0; mclk_idx < ARRAY_SIZE(mclk_mscan_data); mclk_idx++)
		mpc512x_clk_setup_mclk(&mclk_mscan_data[mclk_idx], mclk_idx);
	clks[MPC512x_CLK_SDHC] = mpc512x_clk_gated("sdhc", "sdhc-ug",
						   &clkregs->sccr2, 24);
	/* there is only one SPDIF component, which shares MCLK support code */
	if (soc_has_spdif()) {
		clks[MPC512x_CLK_SPDIF] = mpc512x_clk_gated(
				"spdif", "ips", &clkregs->sccr2, 23);
		mpc512x_clk_setup_mclk(&mclk_spdif_data[0], 0);
	}
	if (soc_has_mbx()) {
		clks[MPC512x_CLK_MBX_BUS] = mpc512x_clk_gated(
				"mbx-bus", "mbx-bus-ug", &clkregs->sccr2, 22);
		clks[MPC512x_CLK_MBX] = mpc512x_clk_gated(
				"mbx", "mbx-ug", &clkregs->sccr2, 21);
		clks[MPC512x_CLK_MBX_3D] = mpc512x_clk_gated(
				"mbx-3d", "mbx-3d-ug", &clkregs->sccr2, 20);
	}
	clks[MPC512x_CLK_IIM] = mpc512x_clk_gated("iim", "csb",
						  &clkregs->sccr2, 19);
	if (soc_has_viu()) {
		clks[MPC512x_CLK_VIU] = mpc512x_clk_gated(
				"viu", "csb", &clkregs->sccr2, 18);
	}
	if (soc_has_sdhc2()) {
		clks[MPC512x_CLK_SDHC2] = mpc512x_clk_gated(
				"sdhc-2", "sdhc2-ug", &clkregs->sccr2, 17);
	}

	if (soc_has_outclk()) {
		size_t idx;	/* used as mclk_idx, just to trim line length */
		for (idx = 0; idx < ARRAY_SIZE(mclk_outclk_data); idx++)
			mpc512x_clk_setup_mclk(&mclk_outclk_data[idx], idx);
	}

	/*
	 * externally provided clocks (when implemented in hardware,
	 * device tree may specify values which otherwise were unknown)
	 */
	freq = get_freq_from_dt("psc_mclk_in");
	if (!freq)
		freq = 25000000;
	clks[MPC512x_CLK_PSC_MCLK_IN] = mpc512x_clk_fixed("psc_mclk_in", freq);
	if (soc_has_mclk_mux0_canin()) {
		freq = get_freq_from_dt("can_clk_in");
		clks[MPC512x_CLK_CAN_CLK_IN] = mpc512x_clk_fixed(
				"can_clk_in", freq);
	} else {
		freq = get_freq_from_dt("spdif_tx_in");
		clks[MPC512x_CLK_SPDIF_TX_IN] = mpc512x_clk_fixed(
				"spdif_tx_in", freq);
		freq = get_freq_from_dt("spdif_rx_in");
		clks[MPC512x_CLK_SPDIF_TX_IN] = mpc512x_clk_fixed(
				"spdif_rx_in", freq);
	}

	/* fixed frequency for AC97, always 24.567MHz */
	clks[MPC512x_CLK_AC97] = mpc512x_clk_fixed("ac97", 24567000);

	/*
	 * pre-enable those "internal" clock items which never get
	 * claimed by any peripheral driver, to not have the clock
	 * subsystem disable them late at startup
	 */
	clk_prepare_enable(clks[MPC512x_CLK_DUMMY]);
	clk_prepare_enable(clks[MPC512x_CLK_E300]);	/* PowerPC CPU */
	clk_prepare_enable(clks[MPC512x_CLK_DDR]);	/* DRAM */
	clk_prepare_enable(clks[MPC512x_CLK_MEM]);	/* SRAM */
	clk_prepare_enable(clks[MPC512x_CLK_IPS]);	/* SoC periph */
	clk_prepare_enable(clks[MPC512x_CLK_LPC]);	/* boot media */
}

/*
 * registers the set of public clocks (those listed in the dt-bindings/
 * header file) for OF lookups, keeps the intermediates private to us
 */
static void __init mpc5121_clk_register_of_provider(struct device_node *np)
{
	clk_data.clks = clks;
	clk_data.clk_num = MPC512x_CLK_LAST_PUBLIC + 1;	/* _not_ ARRAY_SIZE() */
	of_clk_add_provider(np, of_clk_src_onecell_get, &clk_data);
}

/*
 * temporary support for the period of time between introduction of CCF
 * support and the adjustment of peripheral drivers to OF based lookups
 */
static void __init mpc5121_clk_provide_migration_support(void)
{
	struct device_node *np;
	/*
	 * pre-enable those clock items which are not yet appropriately
	 * acquired by their peripheral driver
	 *
	 * the PCI clock cannot get acquired by its peripheral driver,
	 * because for this platform the driver won't probe(), instead
	 * initialization is done from within the .setup_arch() routine
	 * at a point in time where the clock provider has not been
	 * setup yet and thus isn't available yet
	 *
	 * so we "pre-enable" the clock here, to not have the clock
	 * subsystem automatically disable this item in a late init call
	 *
	 * this PCI clock pre-enable workaround only applies when there
	 * are device tree nodes for PCI and thus the peripheral driver
	 * has attached to bridges, otherwise the PCI clock remains
	 * unused and so it gets disabled
	 */
	clk_prepare_enable(clks[MPC512x_CLK_PSC3_MCLK]);/* serial console */
	np = of_find_compatible_node(NULL, "pci", "fsl,mpc5121-pci");
	of_node_put(np);
	if (np)
		clk_prepare_enable(clks[MPC512x_CLK_PCI]);
}

/*
 * those macros are not exactly pretty, but they encapsulate a lot
 * of copy'n'paste heavy code which is even more ugly, and reduce
 * the potential for inconsistencies in those many code copies
 */
#define FOR_NODES(compatname) \
	for_each_compatible_node(np, NULL, compatname)

#define NODE_PREP do { \
	of_address_to_resource(np, 0, &res); \
	snprintf(devname, sizeof(devname), "%08x.%s", res.start, np->name); \
} while (0)

#define NODE_CHK(clkname, clkitem, regnode, regflag) do { \
	struct clk *clk; \
	clk = of_clk_get_by_name(np, clkname); \
	if (IS_ERR(clk)) { \
		clk = clkitem; \
		clk_register_clkdev(clk, clkname, devname); \
		if (regnode) \
			clk_register_clkdev(clk, clkname, np->name); \
		did_register |= DID_REG_ ## regflag; \
		pr_debug("clock alias name '%s' for dev '%s' pointer %p\n", \
			 clkname, devname, clk); \
	} else { \
		clk_put(clk); \
	} \
} while (0)

/*
 * register source code provided fallback results for clock lookups,
 * these get consulted when OF based clock lookup fails (that is in the
 * case of not yet adjusted device tree data, where clock related specs
 * are missing)
 */
static void __init mpc5121_clk_provide_backwards_compat(void)
{
	enum did_reg_flags {
		DID_REG_PSC	= BIT(0),
		DID_REG_PSCFIFO	= BIT(1),
		DID_REG_NFC	= BIT(2),
		DID_REG_CAN	= BIT(3),
		DID_REG_I2C	= BIT(4),
		DID_REG_DIU	= BIT(5),
		DID_REG_VIU	= BIT(6),
		DID_REG_FEC	= BIT(7),
		DID_REG_USB	= BIT(8),
		DID_REG_PATA	= BIT(9),
	};

	int did_register;
	struct device_node *np;
	struct resource res;
	int idx;
	char devname[32];

	did_register = 0;

	FOR_NODES(mpc512x_select_psc_compat()) {
		NODE_PREP;
		idx = (res.start >> 8) & 0xf;
		NODE_CHK("ipg", clks[MPC512x_CLK_PSC0 + idx], 0, PSC);
		NODE_CHK("mclk", clks[MPC512x_CLK_PSC0_MCLK + idx], 0, PSC);
	}

	FOR_NODES("fsl,mpc5121-psc-fifo") {
		NODE_PREP;
		NODE_CHK("ipg", clks[MPC512x_CLK_PSC_FIFO], 1, PSCFIFO);
	}

	FOR_NODES("fsl,mpc5121-nfc") {
		NODE_PREP;
		NODE_CHK("ipg", clks[MPC512x_CLK_NFC], 0, NFC);
	}

	FOR_NODES("fsl,mpc5121-mscan") {
		NODE_PREP;
		idx = 0;
		idx += (res.start & 0x2000) ? 2 : 0;
		idx += (res.start & 0x0080) ? 1 : 0;
		NODE_CHK("ipg", clks[MPC512x_CLK_BDLC], 0, CAN);
		NODE_CHK("mclk", clks[MPC512x_CLK_MSCAN0_MCLK + idx], 0, CAN);
	}

	/*
	 * do register the 'ips', 'sys', and 'ref' names globally
	 * instead of inside each individual CAN node, as there is no
	 * potential for a name conflict (in contrast to 'ipg' and 'mclk')
	 */
	if (did_register & DID_REG_CAN) {
		clk_register_clkdev(clks[MPC512x_CLK_IPS], "ips", NULL);
		clk_register_clkdev(clks[MPC512x_CLK_SYS], "sys", NULL);
		clk_register_clkdev(clks[MPC512x_CLK_REF], "ref", NULL);
	}

	FOR_NODES("fsl,mpc5121-i2c") {
		NODE_PREP;
		NODE_CHK("ipg", clks[MPC512x_CLK_I2C], 0, I2C);
	}

	/*
	 * workaround for the fact that the I2C driver does an "anonymous"
	 * lookup (NULL name spec, which yields the first clock spec) for
	 * which we cannot register an alias -- a _global_ 'ipg' alias that
	 * is not bound to any device name and returns the I2C clock item
	 * is not a good idea
	 *
	 * so we have the lookup in the peripheral driver fail, which is
	 * silent and non-fatal, and pre-enable the clock item here such
	 * that register access is possible
	 *
	 * see commit b3bfce2b "i2c: mpc: cleanup clock API use" for
	 * details, adjusting s/NULL/"ipg"/ in i2c-mpc.c would make this
	 * workaround obsolete
	 */
	if (did_register & DID_REG_I2C)
		clk_prepare_enable(clks[MPC512x_CLK_I2C]);

	FOR_NODES("fsl,mpc5121-diu") {
		NODE_PREP;
		NODE_CHK("ipg", clks[MPC512x_CLK_DIU], 1, DIU);
	}

	FOR_NODES("fsl,mpc5121-viu") {
		NODE_PREP;
		NODE_CHK("ipg", clks[MPC512x_CLK_VIU], 0, VIU);
	}

	/*
	 * note that 2771399a "fs_enet: cleanup clock API use" did use the
	 * "per" string for the clock lookup in contrast to the "ipg" name
	 * which most other nodes are using -- this is not a fatal thing
	 * but just something to keep in mind when doing compatibility
	 * registration, it's a non-issue with up-to-date device tree data
	 */
	FOR_NODES("fsl,mpc5121-fec") {
		NODE_PREP;
		NODE_CHK("per", clks[MPC512x_CLK_FEC], 0, FEC);
	}
	FOR_NODES("fsl,mpc5121-fec-mdio") {
		NODE_PREP;
		NODE_CHK("per", clks[MPC512x_CLK_FEC], 0, FEC);
	}
	/*
	 * MPC5125 has two FECs: FEC1 at 0x2800, FEC2 at 0x4800;
	 * the clock items don't "form an array" since FEC2 was
	 * added only later and was not allowed to shift all other
	 * clock item indices, so the numbers aren't adjacent
	 */
	FOR_NODES("fsl,mpc5125-fec") {
		NODE_PREP;
		if (res.start & 0x4000)
			idx = MPC512x_CLK_FEC2;
		else
			idx = MPC512x_CLK_FEC;
		NODE_CHK("per", clks[idx], 0, FEC);
	}

	FOR_NODES("fsl,mpc5121-usb2-dr") {
		NODE_PREP;
		idx = (res.start & 0x4000) ? 1 : 0;
		NODE_CHK("ipg", clks[MPC512x_CLK_USB1 + idx], 0, USB);
	}

	FOR_NODES("fsl,mpc5121-pata") {
		NODE_PREP;
		NODE_CHK("ipg", clks[MPC512x_CLK_PATA], 0, PATA);
	}

	/*
	 * try to collapse diagnostics into a single line of output yet
	 * provide a full list of what is missing, to avoid noise in the
	 * absence of up-to-date device tree data -- backwards
	 * compatibility to old DTBs is a requirement, updates may be
	 * desirable or preferrable but are not at all mandatory
	 */
	if (did_register) {
		pr_notice("device tree lacks clock specs, adding fallbacks (0x%x,%s%s%s%s%s%s%s%s%s%s)\n",
			  did_register,
			  (did_register & DID_REG_PSC) ? " PSC" : "",
			  (did_register & DID_REG_PSCFIFO) ? " PSCFIFO" : "",
			  (did_register & DID_REG_NFC) ? " NFC" : "",
			  (did_register & DID_REG_CAN) ? " CAN" : "",
			  (did_register & DID_REG_I2C) ? " I2C" : "",
			  (did_register & DID_REG_DIU) ? " DIU" : "",
			  (did_register & DID_REG_VIU) ? " VIU" : "",
			  (did_register & DID_REG_FEC) ? " FEC" : "",
			  (did_register & DID_REG_USB) ? " USB" : "",
			  (did_register & DID_REG_PATA) ? " PATA" : "");
	} else {
		pr_debug("device tree has clock specs, no fallbacks added\n");
	}
}

/*
 * The "fixed-clock" nodes (which includes the oscillator node if the board's
 * DT provides one) has already been scanned by the of_clk_init() in
 * time_init().
 */
int __init mpc5121_clk_init(void)
{
	struct device_node *clk_np;
	int busfreq;

	/* map the clock control registers */
	clk_np = of_find_compatible_node(NULL, NULL, "fsl,mpc5121-clock");
	if (!clk_np)
		return -ENODEV;
	clkregs = of_iomap(clk_np, 0);
	WARN_ON(!clkregs);

	/* determine the SoC variant we run on */
	mpc512x_clk_determine_soc();

	/* invalidate all not yet registered clock slots */
	mpc512x_clk_preset_data();

	/*
	 * add a dummy clock for those situations where a clock spec is
	 * required yet no real clock is involved
	 */
	clks[MPC512x_CLK_DUMMY] = mpc512x_clk_fixed("dummy", 0);

	/*
	 * have all the real nodes in the clock tree populated from REF
	 * down to all leaves, either starting from the OSC node or from
	 * a REF root that was created from the IPS bus clock input
	 */
	busfreq = get_freq_from_dt("bus-frequency");
	mpc512x_clk_setup_clock_tree(clk_np, busfreq);

	/* register as an OF clock provider */
	mpc5121_clk_register_of_provider(clk_np);

	of_node_put(clk_np);

	/*
	 * unbreak not yet adjusted peripheral drivers during migration
	 * towards fully operational common clock support, and allow
	 * operation in the absence of clock related device tree specs
	 */
	mpc5121_clk_provide_migration_support();
	mpc5121_clk_provide_backwards_compat();

	return 0;
}
