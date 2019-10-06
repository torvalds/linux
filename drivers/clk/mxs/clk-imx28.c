// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2012 Freescale Semiconductor, Inc.
 */

#include <linux/clk/mxs.h>
#include <linux/clkdev.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include "clk.h"

static void __iomem *clkctrl;
#define CLKCTRL clkctrl

#define PLL0CTRL0		(CLKCTRL + 0x0000)
#define PLL1CTRL0		(CLKCTRL + 0x0020)
#define PLL2CTRL0		(CLKCTRL + 0x0040)
#define CPU			(CLKCTRL + 0x0050)
#define HBUS			(CLKCTRL + 0x0060)
#define XBUS			(CLKCTRL + 0x0070)
#define XTAL			(CLKCTRL + 0x0080)
#define SSP0			(CLKCTRL + 0x0090)
#define SSP1			(CLKCTRL + 0x00a0)
#define SSP2			(CLKCTRL + 0x00b0)
#define SSP3			(CLKCTRL + 0x00c0)
#define GPMI			(CLKCTRL + 0x00d0)
#define SPDIF			(CLKCTRL + 0x00e0)
#define EMI			(CLKCTRL + 0x00f0)
#define SAIF0			(CLKCTRL + 0x0100)
#define SAIF1			(CLKCTRL + 0x0110)
#define LCDIF			(CLKCTRL + 0x0120)
#define ETM			(CLKCTRL + 0x0130)
#define ENET			(CLKCTRL + 0x0140)
#define FLEXCAN			(CLKCTRL + 0x0160)
#define FRAC0			(CLKCTRL + 0x01b0)
#define FRAC1			(CLKCTRL + 0x01c0)
#define CLKSEQ			(CLKCTRL + 0x01d0)

#define BP_CPU_INTERRUPT_WAIT	12
#define BP_SAIF_DIV_FRAC_EN	16
#define BP_ENET_DIV_TIME	21
#define BP_ENET_SLEEP		31
#define BP_CLKSEQ_BYPASS_SAIF0	0
#define BP_CLKSEQ_BYPASS_SSP0	3
#define BP_FRAC0_IO1FRAC	16
#define BP_FRAC0_IO0FRAC	24

static void __iomem *digctrl;
#define DIGCTRL digctrl
#define BP_SAIF_CLKMUX		10

/*
 * HW_SAIF_CLKMUX_SEL:
 *  DIRECT(0x0): SAIF0 clock pins selected for SAIF0 input clocks, and SAIF1
 *		clock pins selected for SAIF1 input clocks.
 *  CROSSINPUT(0x1): SAIF1 clock inputs selected for SAIF0 input clocks, and
 *		SAIF0 clock inputs selected for SAIF1 input clocks.
 *  EXTMSTR0(0x2): SAIF0 clock pin selected for both SAIF0 and SAIF1 input
 *		clocks.
 *  EXTMSTR1(0x3): SAIF1 clock pin selected for both SAIF0 and SAIF1 input
 *		clocks.
 */
int mxs_saif_clkmux_select(unsigned int clkmux)
{
	if (clkmux > 0x3)
		return -EINVAL;

	writel_relaxed(0x3 << BP_SAIF_CLKMUX, DIGCTRL + CLR);
	writel_relaxed(clkmux << BP_SAIF_CLKMUX, DIGCTRL + SET);

	return 0;
}

static void __init clk_misc_init(void)
{
	u32 val;

	/* Gate off cpu clock in WFI for power saving */
	writel_relaxed(1 << BP_CPU_INTERRUPT_WAIT, CPU + SET);

	/* 0 is a bad default value for a divider */
	writel_relaxed(1 << BP_ENET_DIV_TIME, ENET + SET);

	/* Clear BYPASS for SAIF */
	writel_relaxed(0x3 << BP_CLKSEQ_BYPASS_SAIF0, CLKSEQ + CLR);

	/* SAIF has to use frac div for functional operation */
	val = readl_relaxed(SAIF0);
	val |= 1 << BP_SAIF_DIV_FRAC_EN;
	writel_relaxed(val, SAIF0);

	val = readl_relaxed(SAIF1);
	val |= 1 << BP_SAIF_DIV_FRAC_EN;
	writel_relaxed(val, SAIF1);

	/* Extra fec clock setting */
	val = readl_relaxed(ENET);
	val &= ~(1 << BP_ENET_SLEEP);
	writel_relaxed(val, ENET);

	/*
	 * Source ssp clock from ref_io than ref_xtal,
	 * as ref_xtal only provides 24 MHz as maximum.
	 */
	writel_relaxed(0xf << BP_CLKSEQ_BYPASS_SSP0, CLKSEQ + CLR);

	/*
	 * 480 MHz seems too high to be ssp clock source directly,
	 * so set frac0 to get a 288 MHz ref_io0 and ref_io1.
	 */
	val = readl_relaxed(FRAC0);
	val &= ~((0x3f << BP_FRAC0_IO0FRAC) | (0x3f << BP_FRAC0_IO1FRAC));
	val |= (30 << BP_FRAC0_IO0FRAC) | (30 << BP_FRAC0_IO1FRAC);
	writel_relaxed(val, FRAC0);
}

static const char *const sel_cpu[]  __initconst = { "ref_cpu", "ref_xtal", };
static const char *const sel_io0[]  __initconst = { "ref_io0", "ref_xtal", };
static const char *const sel_io1[]  __initconst = { "ref_io1", "ref_xtal", };
static const char *const sel_pix[]  __initconst = { "ref_pix", "ref_xtal", };
static const char *const sel_gpmi[] __initconst = { "ref_gpmi", "ref_xtal", };
static const char *const sel_pll0[] __initconst = { "pll0", "ref_xtal", };
static const char *const cpu_sels[] __initconst = { "cpu_pll", "cpu_xtal", };
static const char *const emi_sels[] __initconst = { "emi_pll", "emi_xtal", };
static const char *const ptp_sels[] __initconst = { "ref_xtal", "pll0", };

enum imx28_clk {
	ref_xtal, pll0, pll1, pll2, ref_cpu, ref_emi, ref_io0, ref_io1,
	ref_pix, ref_hsadc, ref_gpmi, saif0_sel, saif1_sel, gpmi_sel,
	ssp0_sel, ssp1_sel, ssp2_sel, ssp3_sel, emi_sel, etm_sel,
	lcdif_sel, cpu, ptp_sel, cpu_pll, cpu_xtal, hbus, xbus,
	ssp0_div, ssp1_div, ssp2_div, ssp3_div, gpmi_div, emi_pll,
	emi_xtal, lcdif_div, etm_div, ptp, saif0_div, saif1_div,
	clk32k_div, rtc, lradc, spdif_div, clk32k, pwm, uart, ssp0,
	ssp1, ssp2, ssp3, gpmi, spdif, emi, saif0, saif1, lcdif, etm,
	fec, can0, can1, usb0, usb1, usb0_phy, usb1_phy, enet_out,
	clk_max
};

static struct clk *clks[clk_max];
static struct clk_onecell_data clk_data;

static enum imx28_clk clks_init_on[] __initdata = {
	cpu, hbus, xbus, emi, uart,
};

static void __init mx28_clocks_init(struct device_node *np)
{
	struct device_node *dcnp;
	u32 i;

	dcnp = of_find_compatible_node(NULL, NULL, "fsl,imx28-digctl");
	digctrl = of_iomap(dcnp, 0);
	WARN_ON(!digctrl);
	of_node_put(dcnp);

	clkctrl = of_iomap(np, 0);
	WARN_ON(!clkctrl);

	clk_misc_init();

	clks[ref_xtal] = mxs_clk_fixed("ref_xtal", 24000000);
	clks[pll0] = mxs_clk_pll("pll0", "ref_xtal", PLL0CTRL0, 17, 480000000);
	clks[pll1] = mxs_clk_pll("pll1", "ref_xtal", PLL1CTRL0, 17, 480000000);
	clks[pll2] = mxs_clk_pll("pll2", "ref_xtal", PLL2CTRL0, 23, 50000000);
	clks[ref_cpu] = mxs_clk_ref("ref_cpu", "pll0", FRAC0, 0);
	clks[ref_emi] = mxs_clk_ref("ref_emi", "pll0", FRAC0, 1);
	clks[ref_io1] = mxs_clk_ref("ref_io1", "pll0", FRAC0, 2);
	clks[ref_io0] = mxs_clk_ref("ref_io0", "pll0", FRAC0, 3);
	clks[ref_pix] = mxs_clk_ref("ref_pix", "pll0", FRAC1, 0);
	clks[ref_hsadc] = mxs_clk_ref("ref_hsadc", "pll0", FRAC1, 1);
	clks[ref_gpmi] = mxs_clk_ref("ref_gpmi", "pll0", FRAC1, 2);
	clks[saif0_sel] = mxs_clk_mux("saif0_sel", CLKSEQ, 0, 1, sel_pll0, ARRAY_SIZE(sel_pll0));
	clks[saif1_sel] = mxs_clk_mux("saif1_sel", CLKSEQ, 1, 1, sel_pll0, ARRAY_SIZE(sel_pll0));
	clks[gpmi_sel] = mxs_clk_mux("gpmi_sel", CLKSEQ, 2, 1, sel_gpmi, ARRAY_SIZE(sel_gpmi));
	clks[ssp0_sel] = mxs_clk_mux("ssp0_sel", CLKSEQ, 3, 1, sel_io0, ARRAY_SIZE(sel_io0));
	clks[ssp1_sel] = mxs_clk_mux("ssp1_sel", CLKSEQ, 4, 1, sel_io0, ARRAY_SIZE(sel_io0));
	clks[ssp2_sel] = mxs_clk_mux("ssp2_sel", CLKSEQ, 5, 1, sel_io1, ARRAY_SIZE(sel_io1));
	clks[ssp3_sel] = mxs_clk_mux("ssp3_sel", CLKSEQ, 6, 1, sel_io1, ARRAY_SIZE(sel_io1));
	clks[emi_sel] = mxs_clk_mux("emi_sel", CLKSEQ, 7, 1, emi_sels, ARRAY_SIZE(emi_sels));
	clks[etm_sel] = mxs_clk_mux("etm_sel", CLKSEQ, 8, 1, sel_cpu, ARRAY_SIZE(sel_cpu));
	clks[lcdif_sel] = mxs_clk_mux("lcdif_sel", CLKSEQ, 14, 1, sel_pix, ARRAY_SIZE(sel_pix));
	clks[cpu] = mxs_clk_mux("cpu", CLKSEQ, 18, 1, cpu_sels, ARRAY_SIZE(cpu_sels));
	clks[ptp_sel] = mxs_clk_mux("ptp_sel", ENET, 19, 1, ptp_sels, ARRAY_SIZE(ptp_sels));
	clks[cpu_pll] = mxs_clk_div("cpu_pll", "ref_cpu", CPU, 0, 6, 28);
	clks[cpu_xtal] = mxs_clk_div("cpu_xtal", "ref_xtal", CPU, 16, 10, 29);
	clks[hbus] = mxs_clk_div("hbus", "cpu", HBUS, 0, 5, 31);
	clks[xbus] = mxs_clk_div("xbus", "ref_xtal", XBUS, 0, 10, 31);
	clks[ssp0_div] = mxs_clk_div("ssp0_div", "ssp0_sel", SSP0, 0, 9, 29);
	clks[ssp1_div] = mxs_clk_div("ssp1_div", "ssp1_sel", SSP1, 0, 9, 29);
	clks[ssp2_div] = mxs_clk_div("ssp2_div", "ssp2_sel", SSP2, 0, 9, 29);
	clks[ssp3_div] = mxs_clk_div("ssp3_div", "ssp3_sel", SSP3, 0, 9, 29);
	clks[gpmi_div] = mxs_clk_div("gpmi_div", "gpmi_sel", GPMI, 0, 10, 29);
	clks[emi_pll] = mxs_clk_div("emi_pll", "ref_emi", EMI, 0, 6, 28);
	clks[emi_xtal] = mxs_clk_div("emi_xtal", "ref_xtal", EMI, 8, 4, 29);
	clks[lcdif_div] = mxs_clk_div("lcdif_div", "lcdif_sel", LCDIF, 0, 13, 29);
	clks[etm_div] = mxs_clk_div("etm_div", "etm_sel", ETM, 0, 7, 29);
	clks[ptp] = mxs_clk_div("ptp", "ptp_sel", ENET, 21, 6, 27);
	clks[saif0_div] = mxs_clk_frac("saif0_div", "saif0_sel", SAIF0, 0, 16, 29);
	clks[saif1_div] = mxs_clk_frac("saif1_div", "saif1_sel", SAIF1, 0, 16, 29);
	clks[clk32k_div] = mxs_clk_fixed_factor("clk32k_div", "ref_xtal", 1, 750);
	clks[rtc] = mxs_clk_fixed_factor("rtc", "ref_xtal", 1, 768);
	clks[lradc] = mxs_clk_fixed_factor("lradc", "clk32k", 1, 16);
	clks[spdif_div] = mxs_clk_fixed_factor("spdif_div", "pll0", 1, 4);
	clks[clk32k] = mxs_clk_gate("clk32k", "clk32k_div", XTAL, 26);
	clks[pwm] = mxs_clk_gate("pwm", "ref_xtal", XTAL, 29);
	clks[uart] = mxs_clk_gate("uart", "ref_xtal", XTAL, 31);
	clks[ssp0] = mxs_clk_gate("ssp0", "ssp0_div", SSP0, 31);
	clks[ssp1] = mxs_clk_gate("ssp1", "ssp1_div", SSP1, 31);
	clks[ssp2] = mxs_clk_gate("ssp2", "ssp2_div", SSP2, 31);
	clks[ssp3] = mxs_clk_gate("ssp3", "ssp3_div", SSP3, 31);
	clks[gpmi] = mxs_clk_gate("gpmi", "gpmi_div", GPMI, 31);
	clks[spdif] = mxs_clk_gate("spdif", "spdif_div", SPDIF, 31);
	clks[emi] = mxs_clk_gate("emi", "emi_sel", EMI, 31);
	clks[saif0] = mxs_clk_gate("saif0", "saif0_div", SAIF0, 31);
	clks[saif1] = mxs_clk_gate("saif1", "saif1_div", SAIF1, 31);
	clks[lcdif] = mxs_clk_gate("lcdif", "lcdif_div", LCDIF, 31);
	clks[etm] = mxs_clk_gate("etm", "etm_div", ETM, 31);
	clks[fec] = mxs_clk_gate("fec", "hbus", ENET, 30);
	clks[can0] = mxs_clk_gate("can0", "ref_xtal", FLEXCAN, 30);
	clks[can1] = mxs_clk_gate("can1", "ref_xtal", FLEXCAN, 28);
	clks[usb0] = mxs_clk_gate("usb0", "usb0_phy", DIGCTRL, 2);
	clks[usb1] = mxs_clk_gate("usb1", "usb1_phy", DIGCTRL, 16);
	clks[usb0_phy] = clk_register_gate(NULL, "usb0_phy", "pll0", 0, PLL0CTRL0, 18, 0, &mxs_lock);
	clks[usb1_phy] = clk_register_gate(NULL, "usb1_phy", "pll1", 0, PLL1CTRL0, 18, 0, &mxs_lock);
	clks[enet_out] = clk_register_gate(NULL, "enet_out", "pll2", 0, ENET, 18, 0, &mxs_lock);

	for (i = 0; i < ARRAY_SIZE(clks); i++)
		if (IS_ERR(clks[i])) {
			pr_err("i.MX28 clk %d: register failed with %ld\n",
				i, PTR_ERR(clks[i]));
			return;
		}

	clk_data.clks = clks;
	clk_data.clk_num = ARRAY_SIZE(clks);
	of_clk_add_provider(np, of_clk_src_onecell_get, &clk_data);

	clk_register_clkdev(clks[enet_out], NULL, "enet_out");

	for (i = 0; i < ARRAY_SIZE(clks_init_on); i++)
		clk_prepare_enable(clks[clks_init_on[i]]);
}
CLK_OF_DECLARE(imx28_clkctrl, "fsl,imx28-clkctrl", mx28_clocks_init);
