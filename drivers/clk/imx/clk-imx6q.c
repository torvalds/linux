// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2011-2013 Freescale Semiconductor, Inc.
 * Copyright 2011 Linaro Ltd.
 */

#include <linux/init.h>
#include <linux/types.h>
#include <linux/bits.h>
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <soc/imx/revision.h>
#include <dt-bindings/clock/imx6qdl-clock.h>

#include "clk.h"

static const char *step_sels[]	= { "osc", "pll2_pfd2_396m", };
static const char *pll1_sw_sels[]	= { "pll1_sys", "step", };
static const char *periph_pre_sels[]	= { "pll2_bus", "pll2_pfd2_396m", "pll2_pfd0_352m", "pll2_198m", };
static const char *periph_clk2_sels[]	= { "pll3_usb_otg", "osc", "osc", "dummy", };
static const char *periph2_clk2_sels[]	= { "pll3_usb_otg", "pll2_bus", };
static const char *periph_sels[]	= { "periph_pre", "periph_clk2", };
static const char *periph2_sels[]	= { "periph2_pre", "periph2_clk2", };
static const char *axi_sels[]		= { "periph", "pll2_pfd2_396m", "periph", "pll3_pfd1_540m", };
static const char *audio_sels[]	= { "pll4_audio_div", "pll3_pfd2_508m", "pll3_pfd3_454m", "pll3_usb_otg", };
static const char *gpu_axi_sels[]	= { "axi", "ahb", };
static const char *pre_axi_sels[]	= { "axi", "ahb", };
static const char *gpu2d_core_sels[]	= { "axi", "pll3_usb_otg", "pll2_pfd0_352m", "pll2_pfd2_396m", };
static const char *gpu2d_core_sels_2[]	= { "mmdc_ch0_axi", "pll3_usb_otg", "pll2_pfd1_594m", "pll3_pfd0_720m",};
static const char *gpu3d_core_sels[]	= { "mmdc_ch0_axi", "pll3_usb_otg", "pll2_pfd1_594m", "pll2_pfd2_396m", };
static const char *gpu3d_shader_sels[] = { "mmdc_ch0_axi", "pll3_usb_otg", "pll2_pfd1_594m", "pll3_pfd0_720m", };
static const char *ipu_sels[]		= { "mmdc_ch0_axi", "pll2_pfd2_396m", "pll3_120m", "pll3_pfd1_540m", };
static const char *ldb_di_sels[]	= { "pll5_video_div", "pll2_pfd0_352m", "pll2_pfd2_396m", "mmdc_ch1_axi", "pll3_usb_otg", };
static const char *ipu_di_pre_sels[]	= { "mmdc_ch0_axi", "pll3_usb_otg", "pll5_video_div", "pll2_pfd0_352m", "pll2_pfd2_396m", "pll3_pfd1_540m", };
static const char *ipu1_di0_sels[]	= { "ipu1_di0_pre", "dummy", "dummy", "ldb_di0", "ldb_di1", };
static const char *ipu1_di1_sels[]	= { "ipu1_di1_pre", "dummy", "dummy", "ldb_di0", "ldb_di1", };
static const char *ipu2_di0_sels[]	= { "ipu2_di0_pre", "dummy", "dummy", "ldb_di0", "ldb_di1", };
static const char *ipu2_di1_sels[]	= { "ipu2_di1_pre", "dummy", "dummy", "ldb_di0", "ldb_di1", };
static const char *ipu1_di0_sels_2[]	= { "ipu1_di0_pre", "dummy", "dummy", "ldb_di0_podf", "ldb_di1_podf", };
static const char *ipu1_di1_sels_2[]	= { "ipu1_di1_pre", "dummy", "dummy", "ldb_di0_podf", "ldb_di1_podf", };
static const char *ipu2_di0_sels_2[]	= { "ipu2_di0_pre", "dummy", "dummy", "ldb_di0_podf", "ldb_di1_podf", };
static const char *ipu2_di1_sels_2[]	= { "ipu2_di1_pre", "dummy", "dummy", "ldb_di0_podf", "ldb_di1_podf", };
static const char *hsi_tx_sels[]	= { "pll3_120m", "pll2_pfd2_396m", };
static const char *pcie_axi_sels[]	= { "axi", "ahb", };
static const char *ssi_sels[]		= { "pll3_pfd2_508m", "pll3_pfd3_454m", "pll4_audio_div", };
static const char *usdhc_sels[]	= { "pll2_pfd2_396m", "pll2_pfd0_352m", };
static const char *enfc_sels[]	= { "pll2_pfd0_352m", "pll2_bus", "pll3_usb_otg", "pll2_pfd2_396m", };
static const char *enfc_sels_2[] = {"pll2_pfd0_352m", "pll2_bus", "pll3_usb_otg", "pll2_pfd2_396m", "pll3_pfd3_454m", "dummy", };
static const char *eim_sels[]		= { "pll2_pfd2_396m", "pll3_usb_otg", "axi", "pll2_pfd0_352m", };
static const char *eim_slow_sels[]      = { "axi", "pll3_usb_otg", "pll2_pfd2_396m", "pll2_pfd0_352m", };
static const char *vdo_axi_sels[]	= { "axi", "ahb", };
static const char *vpu_axi_sels[]	= { "axi", "pll2_pfd2_396m", "pll2_pfd0_352m", };
static const char *uart_sels[] = { "pll3_80m", "osc", };
static const char *ipg_per_sels[] = { "ipg", "osc", };
static const char *ecspi_sels[] = { "pll3_60m", "osc", };
static const char *can_sels[] = { "pll3_60m", "osc", "pll3_80m", };
static const char *cko1_sels[]	= { "pll3_usb_otg", "pll2_bus", "pll1_sys", "pll5_video_div",
				    "video_27m", "axi", "enfc", "ipu1_di0", "ipu1_di1", "ipu2_di0",
				    "ipu2_di1", "ahb", "ipg", "ipg_per", "ckil", "pll4_audio_div", };
static const char *cko2_sels[] = {
	"mmdc_ch0_axi", "mmdc_ch1_axi", "usdhc4", "usdhc1",
	"gpu2d_axi", "dummy", "ecspi_root", "gpu3d_axi",
	"usdhc3", "dummy", "arm", "ipu1",
	"ipu2", "vdo_axi", "osc", "gpu2d_core",
	"gpu3d_core", "usdhc2", "ssi1", "ssi2",
	"ssi3", "gpu3d_shader", "vpu_axi", "can_root",
	"ldb_di0", "ldb_di1", "esai_extal", "eim_slow",
	"uart_serial", "spdif", "asrc", "hsi_tx",
};
static const char *cko_sels[] = { "cko1", "cko2", };
static const char *lvds_sels[] = {
	"dummy", "dummy", "dummy", "dummy", "dummy", "dummy",
	"pll4_audio", "pll5_video", "pll8_mlb", "enet_ref",
	"pcie_ref_125m", "sata_ref_100m",  "usbphy1", "usbphy2",
	"dummy", "dummy", "dummy", "dummy", "osc",
};
static const char *pll_bypass_src_sels[] = { "osc", "lvds1_in", "lvds2_in", "dummy", };
static const char *pll1_bypass_sels[] = { "pll1", "pll1_bypass_src", };
static const char *pll2_bypass_sels[] = { "pll2", "pll2_bypass_src", };
static const char *pll3_bypass_sels[] = { "pll3", "pll3_bypass_src", };
static const char *pll4_bypass_sels[] = { "pll4", "pll4_bypass_src", };
static const char *pll5_bypass_sels[] = { "pll5", "pll5_bypass_src", };
static const char *pll6_bypass_sels[] = { "pll6", "pll6_bypass_src", };
static const char *pll7_bypass_sels[] = { "pll7", "pll7_bypass_src", };

static struct clk_hw **hws;
static struct clk_hw_onecell_data *clk_hw_data;

static struct clk_div_table clk_enet_ref_table[] = {
	{ .val = 0, .div = 20, },
	{ .val = 1, .div = 10, },
	{ .val = 2, .div = 5, },
	{ .val = 3, .div = 4, },
	{ /* sentinel */ }
};

static struct clk_div_table post_div_table[] = {
	{ .val = 2, .div = 1, },
	{ .val = 1, .div = 2, },
	{ .val = 0, .div = 4, },
	{ /* sentinel */ }
};

static struct clk_div_table video_div_table[] = {
	{ .val = 0, .div = 1, },
	{ .val = 1, .div = 2, },
	{ .val = 2, .div = 1, },
	{ .val = 3, .div = 4, },
	{ /* sentinel */ }
};

static unsigned int share_count_esai;
static unsigned int share_count_asrc;
static unsigned int share_count_ssi1;
static unsigned int share_count_ssi2;
static unsigned int share_count_ssi3;
static unsigned int share_count_mipi_core_cfg;
static unsigned int share_count_spdif;
static unsigned int share_count_prg0;
static unsigned int share_count_prg1;

static inline int clk_on_imx6q(void)
{
	return of_machine_is_compatible("fsl,imx6q");
}

static inline int clk_on_imx6qp(void)
{
	return of_machine_is_compatible("fsl,imx6qp");
}

static inline int clk_on_imx6dl(void)
{
	return of_machine_is_compatible("fsl,imx6dl");
}

static const int uart_clk_ids[] __initconst = {
	IMX6QDL_CLK_UART_IPG,
	IMX6QDL_CLK_UART_SERIAL,
};

static struct clk **uart_clks[ARRAY_SIZE(uart_clk_ids) + 1] __initdata;

static int ldb_di_sel_by_clock_id(int clock_id)
{
	switch (clock_id) {
	case IMX6QDL_CLK_PLL5_VIDEO_DIV:
		if (clk_on_imx6q() &&
		    imx_get_soc_revision() == IMX_CHIP_REVISION_1_0)
			return -ENOENT;
		return 0;
	case IMX6QDL_CLK_PLL2_PFD0_352M:
		return 1;
	case IMX6QDL_CLK_PLL2_PFD2_396M:
		return 2;
	case IMX6QDL_CLK_MMDC_CH1_AXI:
		return 3;
	case IMX6QDL_CLK_PLL3_USB_OTG:
		return 4;
	default:
		return -ENOENT;
	}
}

static void of_assigned_ldb_sels(struct device_node *node,
				 unsigned int *ldb_di0_sel,
				 unsigned int *ldb_di1_sel)
{
	struct of_phandle_args clkspec;
	int index, rc, num_parents;
	int parent, child, sel;

	num_parents = of_count_phandle_with_args(node, "assigned-clock-parents",
						 "#clock-cells");
	for (index = 0; index < num_parents; index++) {
		rc = of_parse_phandle_with_args(node, "assigned-clock-parents",
					"#clock-cells", index, &clkspec);
		if (rc < 0) {
			/* skip empty (null) phandles */
			if (rc == -ENOENT)
				continue;
			else
				return;
		}
		if (clkspec.np != node || clkspec.args[0] >= IMX6QDL_CLK_END) {
			pr_err("ccm: parent clock %d not in ccm\n", index);
			return;
		}
		parent = clkspec.args[0];

		rc = of_parse_phandle_with_args(node, "assigned-clocks",
				"#clock-cells", index, &clkspec);
		if (rc < 0)
			return;
		if (clkspec.np != node || clkspec.args[0] >= IMX6QDL_CLK_END) {
			pr_err("ccm: child clock %d not in ccm\n", index);
			return;
		}
		child = clkspec.args[0];

		if (child != IMX6QDL_CLK_LDB_DI0_SEL &&
		    child != IMX6QDL_CLK_LDB_DI1_SEL)
			continue;

		sel = ldb_di_sel_by_clock_id(parent);
		if (sel < 0) {
			pr_err("ccm: invalid ldb_di%d parent clock: %d\n",
			       child == IMX6QDL_CLK_LDB_DI1_SEL, parent);
			continue;
		}

		if (child == IMX6QDL_CLK_LDB_DI0_SEL)
			*ldb_di0_sel = sel;
		if (child == IMX6QDL_CLK_LDB_DI1_SEL)
			*ldb_di1_sel = sel;
	}
}

static bool pll6_bypassed(struct device_node *node)
{
	int index, ret, num_clocks;
	struct of_phandle_args clkspec;

	num_clocks = of_count_phandle_with_args(node, "assigned-clocks",
						"#clock-cells");
	if (num_clocks < 0)
		return false;

	for (index = 0; index < num_clocks; index++) {
		ret = of_parse_phandle_with_args(node, "assigned-clocks",
						 "#clock-cells", index,
						 &clkspec);
		if (ret < 0)
			return false;

		if (clkspec.np == node &&
		    clkspec.args[0] == IMX6QDL_PLL6_BYPASS)
			break;
	}

	/* PLL6 bypass is not part of the assigned clock list */
	if (index == num_clocks)
		return false;

	ret = of_parse_phandle_with_args(node, "assigned-clock-parents",
					 "#clock-cells", index, &clkspec);

	if (clkspec.args[0] != IMX6QDL_CLK_PLL6)
		return true;

	return false;
}

#define CCM_CCSR		0x0c
#define CCM_CS2CDR		0x2c

#define CCSR_PLL3_SW_CLK_SEL		BIT(0)

#define CS2CDR_LDB_DI0_CLK_SEL_SHIFT	9
#define CS2CDR_LDB_DI1_CLK_SEL_SHIFT	12

/*
 * The only way to disable the MMDC_CH1 clock is to move it to pll3_sw_clk
 * via periph2_clk2_sel and then to disable pll3_sw_clk by selecting the
 * bypass clock source, since there is no CG bit for mmdc_ch1.
 */
static void mmdc_ch1_disable(void __iomem *ccm_base)
{
	unsigned int reg;

	clk_set_parent(hws[IMX6QDL_CLK_PERIPH2_CLK2_SEL]->clk,
		       hws[IMX6QDL_CLK_PLL3_USB_OTG]->clk);

	/* Disable pll3_sw_clk by selecting the bypass clock source */
	reg = readl_relaxed(ccm_base + CCM_CCSR);
	reg |= CCSR_PLL3_SW_CLK_SEL;
	writel_relaxed(reg, ccm_base + CCM_CCSR);
}

static void mmdc_ch1_reenable(void __iomem *ccm_base)
{
	unsigned int reg;

	/* Enable pll3_sw_clk by disabling the bypass */
	reg = readl_relaxed(ccm_base + CCM_CCSR);
	reg &= ~CCSR_PLL3_SW_CLK_SEL;
	writel_relaxed(reg, ccm_base + CCM_CCSR);
}

/*
 * We have to follow a strict procedure when changing the LDB clock source,
 * otherwise we risk introducing a glitch that can lock up the LDB divider.
 * Things to keep in mind:
 *
 * 1. The current and new parent clock inputs to the mux must be disabled.
 * 2. The default clock input for ldb_di0/1_clk_sel is mmdc_ch1_axi, which
 *    has no CG bit.
 * 3. pll2_pfd2_396m can not be gated if it is used as memory clock.
 * 4. In the RTL implementation of the LDB_DI_CLK_SEL muxes the top four
 *    options are in one mux and the PLL3 option along with three unused
 *    inputs is in a second mux. There is a third mux with two inputs used
 *    to decide between the first and second 4-port mux:
 *
 *    pll5_video_div 0 --|\
 *    pll2_pfd0_352m 1 --| |_
 *    pll2_pfd2_396m 2 --| | `-|\
 *    mmdc_ch1_axi   3 --|/    | |
 *                             | |--
 *    pll3_usb_otg   4 --|\    | |
 *                   5 --| |_,-|/
 *                   6 --| |
 *                   7 --|/
 *
 * The ldb_di0/1_clk_sel[1:0] bits control both 4-port muxes at the same time.
 * The ldb_di0/1_clk_sel[2] bit controls the 2-port mux. The code below
 * switches the parent to the bottom mux first and then manipulates the top
 * mux to ensure that no glitch will enter the divider.
 */
static void init_ldb_clks(struct device_node *np, void __iomem *ccm_base)
{
	unsigned int reg;
	unsigned int sel[2][4];
	int i;

	reg = readl_relaxed(ccm_base + CCM_CS2CDR);
	sel[0][0] = (reg >> CS2CDR_LDB_DI0_CLK_SEL_SHIFT) & 7;
	sel[1][0] = (reg >> CS2CDR_LDB_DI1_CLK_SEL_SHIFT) & 7;

	sel[0][3] = sel[0][2] = sel[0][1] = sel[0][0];
	sel[1][3] = sel[1][2] = sel[1][1] = sel[1][0];

	of_assigned_ldb_sels(np, &sel[0][3], &sel[1][3]);

	for (i = 0; i < 2; i++) {
		/* Print a notice if a glitch might have been introduced already */
		if (sel[i][0] != 3) {
			pr_notice("ccm: possible glitch: ldb_di%d_sel already changed from reset value: %d\n",
				  i, sel[i][0]);
		}

		if (sel[i][0] == sel[i][3])
			continue;

		/* Only switch to or from pll2_pfd2_396m if it is disabled */
		if ((sel[i][0] == 2 || sel[i][3] == 2) &&
		    (clk_get_parent(hws[IMX6QDL_CLK_PERIPH_PRE]->clk) ==
		     hws[IMX6QDL_CLK_PLL2_PFD2_396M]->clk)) {
			pr_err("ccm: ldb_di%d_sel: couldn't disable pll2_pfd2_396m\n",
			       i);
			sel[i][3] = sel[i][2] = sel[i][1] = sel[i][0];
			continue;
		}

		/* First switch to the bottom mux */
		sel[i][1] = sel[i][0] | 4;

		/* Then configure the top mux before switching back to it */
		sel[i][2] = sel[i][3] | 4;

		pr_debug("ccm: switching ldb_di%d_sel: %d->%d->%d->%d\n", i,
			 sel[i][0], sel[i][1], sel[i][2], sel[i][3]);
	}

	if (sel[0][0] == sel[0][3] && sel[1][0] == sel[1][3])
		return;

	mmdc_ch1_disable(ccm_base);

	for (i = 1; i < 4; i++) {
		reg = readl_relaxed(ccm_base + CCM_CS2CDR);
		reg &= ~((7 << CS2CDR_LDB_DI0_CLK_SEL_SHIFT) |
			 (7 << CS2CDR_LDB_DI1_CLK_SEL_SHIFT));
		reg |= ((sel[0][i] << CS2CDR_LDB_DI0_CLK_SEL_SHIFT) |
			(sel[1][i] << CS2CDR_LDB_DI1_CLK_SEL_SHIFT));
		writel_relaxed(reg, ccm_base + CCM_CS2CDR);
	}

	mmdc_ch1_reenable(ccm_base);
}

#define CCM_ANALOG_PLL_VIDEO	0xa0
#define CCM_ANALOG_PFD_480	0xf0
#define CCM_ANALOG_PFD_528	0x100

#define PLL_ENABLE		BIT(13)

#define PFD0_CLKGATE		BIT(7)
#define PFD1_CLKGATE		BIT(15)
#define PFD2_CLKGATE		BIT(23)
#define PFD3_CLKGATE		BIT(31)

static void disable_anatop_clocks(void __iomem *anatop_base)
{
	unsigned int reg;

	/* Make sure PLL2 PFDs 0-2 are gated */
	reg = readl_relaxed(anatop_base + CCM_ANALOG_PFD_528);
	/* Cannot gate PFD2 if pll2_pfd2_396m is the parent of MMDC clock */
	if (clk_get_parent(hws[IMX6QDL_CLK_PERIPH_PRE]->clk) ==
	    hws[IMX6QDL_CLK_PLL2_PFD2_396M]->clk)
		reg |= PFD0_CLKGATE | PFD1_CLKGATE;
	else
		reg |= PFD0_CLKGATE | PFD1_CLKGATE | PFD2_CLKGATE;
	writel_relaxed(reg, anatop_base + CCM_ANALOG_PFD_528);

	/* Make sure PLL3 PFDs 0-3 are gated */
	reg = readl_relaxed(anatop_base + CCM_ANALOG_PFD_480);
	reg |= PFD0_CLKGATE | PFD1_CLKGATE | PFD2_CLKGATE | PFD3_CLKGATE;
	writel_relaxed(reg, anatop_base + CCM_ANALOG_PFD_480);

	/* Make sure PLL5 is disabled */
	reg = readl_relaxed(anatop_base + CCM_ANALOG_PLL_VIDEO);
	reg &= ~PLL_ENABLE;
	writel_relaxed(reg, anatop_base + CCM_ANALOG_PLL_VIDEO);
}

static struct clk_hw * __init imx6q_obtain_fixed_clk_hw(struct device_node *np,
							const char *name,
							unsigned long rate)
{
	struct clk *clk = of_clk_get_by_name(np, name);
	struct clk_hw *hw;

	if (IS_ERR(clk))
		hw = imx_obtain_fixed_clock_hw(name, rate);
	else
		hw = __clk_get_hw(clk);

	return hw;
}

static void __init imx6q_clocks_init(struct device_node *ccm_node)
{
	struct device_node *np;
	void __iomem *anatop_base, *base;
	int ret;
	int i;

	clk_hw_data = kzalloc(struct_size(clk_hw_data, hws,
					  IMX6QDL_CLK_END), GFP_KERNEL);
	if (WARN_ON(!clk_hw_data))
		return;

	clk_hw_data->num = IMX6QDL_CLK_END;
	hws = clk_hw_data->hws;

	hws[IMX6QDL_CLK_DUMMY] = imx_clk_hw_fixed("dummy", 0);

	hws[IMX6QDL_CLK_CKIL] = imx6q_obtain_fixed_clk_hw(ccm_node, "ckil", 0);
	hws[IMX6QDL_CLK_CKIH] = imx6q_obtain_fixed_clk_hw(ccm_node, "ckih1", 0);
	hws[IMX6QDL_CLK_OSC] = imx6q_obtain_fixed_clk_hw(ccm_node, "osc", 0);

	/* Clock source from external clock via CLK1/2 PADs */
	hws[IMX6QDL_CLK_ANACLK1] = imx6q_obtain_fixed_clk_hw(ccm_node, "anaclk1", 0);
	hws[IMX6QDL_CLK_ANACLK2] = imx6q_obtain_fixed_clk_hw(ccm_node, "anaclk2", 0);

	np = of_find_compatible_node(NULL, NULL, "fsl,imx6q-anatop");
	anatop_base = base = of_iomap(np, 0);
	WARN_ON(!base);
	of_node_put(np);

	/* Audio/video PLL post dividers do not work on i.MX6q revision 1.0 */
	if (clk_on_imx6q() && imx_get_soc_revision() == IMX_CHIP_REVISION_1_0) {
		post_div_table[1].div = 1;
		post_div_table[2].div = 1;
		video_div_table[1].div = 1;
		video_div_table[3].div = 1;
	}

	hws[IMX6QDL_PLL1_BYPASS_SRC] = imx_clk_hw_mux("pll1_bypass_src", base + 0x00, 14, 2, pll_bypass_src_sels, ARRAY_SIZE(pll_bypass_src_sels));
	hws[IMX6QDL_PLL2_BYPASS_SRC] = imx_clk_hw_mux("pll2_bypass_src", base + 0x30, 14, 2, pll_bypass_src_sels, ARRAY_SIZE(pll_bypass_src_sels));
	hws[IMX6QDL_PLL3_BYPASS_SRC] = imx_clk_hw_mux("pll3_bypass_src", base + 0x10, 14, 2, pll_bypass_src_sels, ARRAY_SIZE(pll_bypass_src_sels));
	hws[IMX6QDL_PLL4_BYPASS_SRC] = imx_clk_hw_mux("pll4_bypass_src", base + 0x70, 14, 2, pll_bypass_src_sels, ARRAY_SIZE(pll_bypass_src_sels));
	hws[IMX6QDL_PLL5_BYPASS_SRC] = imx_clk_hw_mux("pll5_bypass_src", base + 0xa0, 14, 2, pll_bypass_src_sels, ARRAY_SIZE(pll_bypass_src_sels));
	hws[IMX6QDL_PLL6_BYPASS_SRC] = imx_clk_hw_mux("pll6_bypass_src", base + 0xe0, 14, 2, pll_bypass_src_sels, ARRAY_SIZE(pll_bypass_src_sels));
	hws[IMX6QDL_PLL7_BYPASS_SRC] = imx_clk_hw_mux("pll7_bypass_src", base + 0x20, 14, 2, pll_bypass_src_sels, ARRAY_SIZE(pll_bypass_src_sels));

	/*                                    type               name    parent_name        base         div_mask */
	hws[IMX6QDL_CLK_PLL1] = imx_clk_hw_pllv3(IMX_PLLV3_SYS,     "pll1", "osc", base + 0x00, 0x7f);
	hws[IMX6QDL_CLK_PLL2] = imx_clk_hw_pllv3(IMX_PLLV3_GENERIC, "pll2", "osc", base + 0x30, 0x1);
	hws[IMX6QDL_CLK_PLL3] = imx_clk_hw_pllv3(IMX_PLLV3_USB,     "pll3", "osc", base + 0x10, 0x3);
	hws[IMX6QDL_CLK_PLL4] = imx_clk_hw_pllv3(IMX_PLLV3_AV,      "pll4", "osc", base + 0x70, 0x7f);
	hws[IMX6QDL_CLK_PLL5] = imx_clk_hw_pllv3(IMX_PLLV3_AV,      "pll5", "osc", base + 0xa0, 0x7f);
	hws[IMX6QDL_CLK_PLL6] = imx_clk_hw_pllv3(IMX_PLLV3_ENET,    "pll6", "osc", base + 0xe0, 0x3);
	hws[IMX6QDL_CLK_PLL7] = imx_clk_hw_pllv3(IMX_PLLV3_USB,     "pll7", "osc", base + 0x20, 0x3);

	hws[IMX6QDL_PLL1_BYPASS] = imx_clk_hw_mux_flags("pll1_bypass", base + 0x00, 16, 1, pll1_bypass_sels, ARRAY_SIZE(pll1_bypass_sels), CLK_SET_RATE_PARENT);
	hws[IMX6QDL_PLL2_BYPASS] = imx_clk_hw_mux_flags("pll2_bypass", base + 0x30, 16, 1, pll2_bypass_sels, ARRAY_SIZE(pll2_bypass_sels), CLK_SET_RATE_PARENT);
	hws[IMX6QDL_PLL3_BYPASS] = imx_clk_hw_mux_flags("pll3_bypass", base + 0x10, 16, 1, pll3_bypass_sels, ARRAY_SIZE(pll3_bypass_sels), CLK_SET_RATE_PARENT);
	hws[IMX6QDL_PLL4_BYPASS] = imx_clk_hw_mux_flags("pll4_bypass", base + 0x70, 16, 1, pll4_bypass_sels, ARRAY_SIZE(pll4_bypass_sels), CLK_SET_RATE_PARENT);
	hws[IMX6QDL_PLL5_BYPASS] = imx_clk_hw_mux_flags("pll5_bypass", base + 0xa0, 16, 1, pll5_bypass_sels, ARRAY_SIZE(pll5_bypass_sels), CLK_SET_RATE_PARENT);
	hws[IMX6QDL_PLL6_BYPASS] = imx_clk_hw_mux_flags("pll6_bypass", base + 0xe0, 16, 1, pll6_bypass_sels, ARRAY_SIZE(pll6_bypass_sels), CLK_SET_RATE_PARENT);
	hws[IMX6QDL_PLL7_BYPASS] = imx_clk_hw_mux_flags("pll7_bypass", base + 0x20, 16, 1, pll7_bypass_sels, ARRAY_SIZE(pll7_bypass_sels), CLK_SET_RATE_PARENT);

	/* Do not bypass PLLs initially */
	clk_set_parent(hws[IMX6QDL_PLL1_BYPASS]->clk, hws[IMX6QDL_CLK_PLL1]->clk);
	clk_set_parent(hws[IMX6QDL_PLL2_BYPASS]->clk, hws[IMX6QDL_CLK_PLL2]->clk);
	clk_set_parent(hws[IMX6QDL_PLL3_BYPASS]->clk, hws[IMX6QDL_CLK_PLL3]->clk);
	clk_set_parent(hws[IMX6QDL_PLL4_BYPASS]->clk, hws[IMX6QDL_CLK_PLL4]->clk);
	clk_set_parent(hws[IMX6QDL_PLL5_BYPASS]->clk, hws[IMX6QDL_CLK_PLL5]->clk);
	clk_set_parent(hws[IMX6QDL_PLL6_BYPASS]->clk, hws[IMX6QDL_CLK_PLL6]->clk);
	clk_set_parent(hws[IMX6QDL_PLL7_BYPASS]->clk, hws[IMX6QDL_CLK_PLL7]->clk);

	hws[IMX6QDL_CLK_PLL1_SYS]      = imx_clk_hw_gate("pll1_sys",      "pll1_bypass", base + 0x00, 13);
	hws[IMX6QDL_CLK_PLL2_BUS]      = imx_clk_hw_gate("pll2_bus",      "pll2_bypass", base + 0x30, 13);
	hws[IMX6QDL_CLK_PLL3_USB_OTG]  = imx_clk_hw_gate("pll3_usb_otg",  "pll3_bypass", base + 0x10, 13);
	hws[IMX6QDL_CLK_PLL4_AUDIO]    = imx_clk_hw_gate("pll4_audio",    "pll4_bypass", base + 0x70, 13);
	hws[IMX6QDL_CLK_PLL5_VIDEO]    = imx_clk_hw_gate("pll5_video",    "pll5_bypass", base + 0xa0, 13);
	hws[IMX6QDL_CLK_PLL6_ENET]     = imx_clk_hw_gate("pll6_enet",     "pll6_bypass", base + 0xe0, 13);
	hws[IMX6QDL_CLK_PLL7_USB_HOST] = imx_clk_hw_gate("pll7_usb_host", "pll7_bypass", base + 0x20, 13);

	/*
	 * Bit 20 is the reserved and read-only bit, we do this only for:
	 * - Do nothing for usbphy clk_enable/disable
	 * - Keep refcount when do usbphy clk_enable/disable, in that case,
	 * the clk framework may need to enable/disable usbphy's parent
	 */
	hws[IMX6QDL_CLK_USBPHY1] = imx_clk_hw_gate("usbphy1", "pll3_usb_otg", base + 0x10, 20);
	hws[IMX6QDL_CLK_USBPHY2] = imx_clk_hw_gate("usbphy2", "pll7_usb_host", base + 0x20, 20);

	/*
	 * usbphy*_gate needs to be on after system boots up, and software
	 * never needs to control it anymore.
	 */
	hws[IMX6QDL_CLK_USBPHY1_GATE] = imx_clk_hw_gate("usbphy1_gate", "dummy", base + 0x10, 6);
	hws[IMX6QDL_CLK_USBPHY2_GATE] = imx_clk_hw_gate("usbphy2_gate", "dummy", base + 0x20, 6);

	/*
	 * The ENET PLL is special in that is has multiple outputs with
	 * different post-dividers that are all affected by the single bypass
	 * bit, so a single mux bit affects 3 independent branches of the clock
	 * tree. There is no good way to model this in the clock framework and
	 * dynamically changing the bypass bit, will yield unexpected results.
	 * So we treat any configuration that bypasses the ENET PLL as
	 * essentially static with the divider ratios reflecting the bypass
	 * status.
	 *
	 */
	if (!pll6_bypassed(ccm_node)) {
		hws[IMX6QDL_CLK_SATA_REF] = imx_clk_hw_fixed_factor("sata_ref", "pll6_enet", 1, 5);
		hws[IMX6QDL_CLK_PCIE_REF] = imx_clk_hw_fixed_factor("pcie_ref", "pll6_enet", 1, 4);
		hws[IMX6QDL_CLK_ENET_REF] = clk_hw_register_divider_table(NULL, "enet_ref", "pll6_enet", 0,
						base + 0xe0, 0, 2, 0, clk_enet_ref_table,
						&imx_ccm_lock);
	} else {
		hws[IMX6QDL_CLK_SATA_REF] = imx_clk_hw_fixed_factor("sata_ref", "pll6_enet", 1, 1);
		hws[IMX6QDL_CLK_PCIE_REF] = imx_clk_hw_fixed_factor("pcie_ref", "pll6_enet", 1, 1);
		hws[IMX6QDL_CLK_ENET_REF] = imx_clk_hw_fixed_factor("enet_ref", "pll6_enet", 1, 1);
	}

	hws[IMX6QDL_CLK_SATA_REF_100M] = imx_clk_hw_gate("sata_ref_100m", "sata_ref", base + 0xe0, 20);
	hws[IMX6QDL_CLK_PCIE_REF_125M] = imx_clk_hw_gate("pcie_ref_125m", "pcie_ref", base + 0xe0, 19);

	hws[IMX6QDL_CLK_LVDS1_SEL] = imx_clk_hw_mux("lvds1_sel", base + 0x160, 0, 5, lvds_sels, ARRAY_SIZE(lvds_sels));
	hws[IMX6QDL_CLK_LVDS2_SEL] = imx_clk_hw_mux("lvds2_sel", base + 0x160, 5, 5, lvds_sels, ARRAY_SIZE(lvds_sels));

	/*
	 * lvds1_gate and lvds2_gate are pseudo-gates.  Both can be
	 * independently configured as clock inputs or outputs.  We treat
	 * the "output_enable" bit as a gate, even though it's really just
	 * enabling clock output. Initially the gate bits are cleared, as
	 * otherwise the exclusive configuration gets locked in the setup done
	 * by software running before the clock driver, with no way to change
	 * it.
	 */
	writel(readl(base + 0x160) & ~0x3c00, base + 0x160);
	hws[IMX6QDL_CLK_LVDS1_GATE] = imx_clk_hw_gate_exclusive("lvds1_gate", "lvds1_sel", base + 0x160, 10, BIT(12));
	hws[IMX6QDL_CLK_LVDS2_GATE] = imx_clk_hw_gate_exclusive("lvds2_gate", "lvds2_sel", base + 0x160, 11, BIT(13));

	hws[IMX6QDL_CLK_LVDS1_IN] = imx_clk_hw_gate_exclusive("lvds1_in", "anaclk1", base + 0x160, 12, BIT(10));
	hws[IMX6QDL_CLK_LVDS2_IN] = imx_clk_hw_gate_exclusive("lvds2_in", "anaclk2", base + 0x160, 13, BIT(11));

	/*                                            name              parent_name        reg       idx */
	hws[IMX6QDL_CLK_PLL2_PFD0_352M] = imx_clk_hw_pfd("pll2_pfd0_352m", "pll2_bus",     base + 0x100, 0);
	hws[IMX6QDL_CLK_PLL2_PFD1_594M] = imx_clk_hw_pfd("pll2_pfd1_594m", "pll2_bus",     base + 0x100, 1);
	hws[IMX6QDL_CLK_PLL2_PFD2_396M] = imx_clk_hw_pfd("pll2_pfd2_396m", "pll2_bus",     base + 0x100, 2);
	hws[IMX6QDL_CLK_PLL3_PFD0_720M] = imx_clk_hw_pfd("pll3_pfd0_720m", "pll3_usb_otg", base + 0xf0,  0);
	hws[IMX6QDL_CLK_PLL3_PFD1_540M] = imx_clk_hw_pfd("pll3_pfd1_540m", "pll3_usb_otg", base + 0xf0,  1);
	hws[IMX6QDL_CLK_PLL3_PFD2_508M] = imx_clk_hw_pfd("pll3_pfd2_508m", "pll3_usb_otg", base + 0xf0,  2);
	hws[IMX6QDL_CLK_PLL3_PFD3_454M] = imx_clk_hw_pfd("pll3_pfd3_454m", "pll3_usb_otg", base + 0xf0,  3);

	/*                                                name         parent_name     mult div */
	hws[IMX6QDL_CLK_PLL2_198M] = imx_clk_hw_fixed_factor("pll2_198m", "pll2_pfd2_396m", 1, 2);
	hws[IMX6QDL_CLK_PLL3_120M] = imx_clk_hw_fixed_factor("pll3_120m", "pll3_usb_otg",   1, 4);
	hws[IMX6QDL_CLK_PLL3_80M]  = imx_clk_hw_fixed_factor("pll3_80m",  "pll3_usb_otg",   1, 6);
	hws[IMX6QDL_CLK_PLL3_60M]  = imx_clk_hw_fixed_factor("pll3_60m",  "pll3_usb_otg",   1, 8);
	hws[IMX6QDL_CLK_TWD]       = imx_clk_hw_fixed_factor("twd",       "arm",            1, 2);
	hws[IMX6QDL_CLK_GPT_3M]    = imx_clk_hw_fixed_factor("gpt_3m",    "osc",            1, 8);
	hws[IMX6QDL_CLK_VIDEO_27M] = imx_clk_hw_fixed_factor("video_27m", "pll3_pfd1_540m", 1, 20);
	if (clk_on_imx6dl() || clk_on_imx6qp()) {
		hws[IMX6QDL_CLK_GPU2D_AXI] = imx_clk_hw_fixed_factor("gpu2d_axi", "mmdc_ch0_axi_podf", 1, 1);
		hws[IMX6QDL_CLK_GPU3D_AXI] = imx_clk_hw_fixed_factor("gpu3d_axi", "mmdc_ch0_axi_podf", 1, 1);
	}

	hws[IMX6QDL_CLK_PLL4_POST_DIV] = clk_hw_register_divider_table(NULL, "pll4_post_div", "pll4_audio", CLK_SET_RATE_PARENT, base + 0x70, 19, 2, 0, post_div_table, &imx_ccm_lock);
	if (clk_on_imx6q() || clk_on_imx6qp())
		hws[IMX6QDL_CLK_PLL4_AUDIO_DIV] = imx_clk_hw_fixed_factor("pll4_audio_div", "pll4_post_div", 1, 1);
	else
		hws[IMX6QDL_CLK_PLL4_AUDIO_DIV] = clk_hw_register_divider(NULL, "pll4_audio_div", "pll4_post_div", CLK_SET_RATE_PARENT, base + 0x170, 15, 1, 0, &imx_ccm_lock);
	hws[IMX6QDL_CLK_PLL5_POST_DIV] = clk_hw_register_divider_table(NULL, "pll5_post_div", "pll5_video", CLK_SET_RATE_PARENT, base + 0xa0, 19, 2, 0, post_div_table, &imx_ccm_lock);
	hws[IMX6QDL_CLK_PLL5_VIDEO_DIV] = clk_hw_register_divider_table(NULL, "pll5_video_div", "pll5_post_div", CLK_SET_RATE_PARENT, base + 0x170, 30, 2, 0, video_div_table, &imx_ccm_lock);

	np = ccm_node;
	base = of_iomap(np, 0);
	WARN_ON(!base);

	/*                                              name                reg       shift width parent_names     num_parents */
	hws[IMX6QDL_CLK_STEP]             = imx_clk_hw_mux("step",	            base + 0xc,  8,  1, step_sels,	   ARRAY_SIZE(step_sels));
	hws[IMX6QDL_CLK_PLL1_SW]          = imx_clk_hw_mux("pll1_sw",	    base + 0xc,  2,  1, pll1_sw_sels,      ARRAY_SIZE(pll1_sw_sels));
	hws[IMX6QDL_CLK_PERIPH_PRE]       = imx_clk_hw_mux("periph_pre",       base + 0x18, 18, 2, periph_pre_sels,   ARRAY_SIZE(periph_pre_sels));
	hws[IMX6QDL_CLK_PERIPH2_PRE]      = imx_clk_hw_mux("periph2_pre",      base + 0x18, 21, 2, periph_pre_sels,   ARRAY_SIZE(periph_pre_sels));
	hws[IMX6QDL_CLK_PERIPH_CLK2_SEL]  = imx_clk_hw_mux("periph_clk2_sel",  base + 0x18, 12, 2, periph_clk2_sels,  ARRAY_SIZE(periph_clk2_sels));
	hws[IMX6QDL_CLK_PERIPH2_CLK2_SEL] = imx_clk_hw_mux("periph2_clk2_sel", base + 0x18, 20, 1, periph2_clk2_sels, ARRAY_SIZE(periph2_clk2_sels));
	hws[IMX6QDL_CLK_AXI_SEL]          = imx_clk_hw_mux("axi_sel",          base + 0x14, 6,  2, axi_sels,          ARRAY_SIZE(axi_sels));
	hws[IMX6QDL_CLK_ESAI_SEL]         = imx_clk_hw_mux("esai_sel",         base + 0x20, 19, 2, audio_sels,        ARRAY_SIZE(audio_sels));
	hws[IMX6QDL_CLK_ASRC_SEL]         = imx_clk_hw_mux("asrc_sel",         base + 0x30, 7,  2, audio_sels,        ARRAY_SIZE(audio_sels));
	hws[IMX6QDL_CLK_SPDIF_SEL]        = imx_clk_hw_mux("spdif_sel",        base + 0x30, 20, 2, audio_sels,        ARRAY_SIZE(audio_sels));
	if (clk_on_imx6q()) {
		hws[IMX6QDL_CLK_GPU2D_AXI]        = imx_clk_hw_mux("gpu2d_axi",        base + 0x18, 0,  1, gpu_axi_sels,      ARRAY_SIZE(gpu_axi_sels));
		hws[IMX6QDL_CLK_GPU3D_AXI]        = imx_clk_hw_mux("gpu3d_axi",        base + 0x18, 1,  1, gpu_axi_sels,      ARRAY_SIZE(gpu_axi_sels));
	}
	if (clk_on_imx6qp()) {
		hws[IMX6QDL_CLK_CAN_SEL]   = imx_clk_hw_mux("can_sel",	base + 0x20, 8,  2, can_sels, ARRAY_SIZE(can_sels));
		hws[IMX6QDL_CLK_ECSPI_SEL] = imx_clk_hw_mux("ecspi_sel",	base + 0x38, 18, 1, ecspi_sels,  ARRAY_SIZE(ecspi_sels));
		hws[IMX6QDL_CLK_IPG_PER_SEL] = imx_clk_hw_mux("ipg_per_sel", base + 0x1c, 6, 1, ipg_per_sels, ARRAY_SIZE(ipg_per_sels));
		hws[IMX6QDL_CLK_UART_SEL] = imx_clk_hw_mux("uart_sel", base + 0x24, 6, 1, uart_sels, ARRAY_SIZE(uart_sels));
		hws[IMX6QDL_CLK_GPU2D_CORE_SEL] = imx_clk_hw_mux("gpu2d_core_sel", base + 0x18, 16, 2, gpu2d_core_sels_2, ARRAY_SIZE(gpu2d_core_sels_2));
	} else if (clk_on_imx6dl()) {
		hws[IMX6QDL_CLK_MLB_SEL] = imx_clk_hw_mux("mlb_sel",   base + 0x18, 16, 2, gpu2d_core_sels,   ARRAY_SIZE(gpu2d_core_sels));
	} else {
		hws[IMX6QDL_CLK_GPU2D_CORE_SEL] = imx_clk_hw_mux("gpu2d_core_sel",   base + 0x18, 16, 2, gpu2d_core_sels,   ARRAY_SIZE(gpu2d_core_sels));
	}
	hws[IMX6QDL_CLK_GPU3D_CORE_SEL]   = imx_clk_hw_mux("gpu3d_core_sel",   base + 0x18, 4,  2, gpu3d_core_sels,   ARRAY_SIZE(gpu3d_core_sels));
	if (clk_on_imx6dl())
		hws[IMX6QDL_CLK_GPU2D_CORE_SEL] = imx_clk_hw_mux("gpu2d_core_sel", base + 0x18, 8,  2, gpu3d_shader_sels, ARRAY_SIZE(gpu3d_shader_sels));
	else
		hws[IMX6QDL_CLK_GPU3D_SHADER_SEL] = imx_clk_hw_mux("gpu3d_shader_sel", base + 0x18, 8,  2, gpu3d_shader_sels, ARRAY_SIZE(gpu3d_shader_sels));
	hws[IMX6QDL_CLK_IPU1_SEL]         = imx_clk_hw_mux("ipu1_sel",         base + 0x3c, 9,  2, ipu_sels,          ARRAY_SIZE(ipu_sels));
	hws[IMX6QDL_CLK_IPU2_SEL]         = imx_clk_hw_mux("ipu2_sel",         base + 0x3c, 14, 2, ipu_sels,          ARRAY_SIZE(ipu_sels));

	disable_anatop_clocks(anatop_base);

	imx_mmdc_mask_handshake(base, 1);

	if (clk_on_imx6qp()) {
		hws[IMX6QDL_CLK_LDB_DI0_SEL]      = imx_clk_hw_mux_flags("ldb_di0_sel", base + 0x2c, 9,  3, ldb_di_sels,      ARRAY_SIZE(ldb_di_sels), CLK_SET_RATE_PARENT);
		hws[IMX6QDL_CLK_LDB_DI1_SEL]      = imx_clk_hw_mux_flags("ldb_di1_sel", base + 0x2c, 12, 3, ldb_di_sels,      ARRAY_SIZE(ldb_di_sels), CLK_SET_RATE_PARENT);
	} else {
		/*
		 * The LDB_DI0/1_SEL muxes are registered read-only due to a hardware
		 * bug. Set the muxes to the requested values before registering the
		 * ldb_di_sel clocks.
		 */
		init_ldb_clks(np, base);

		hws[IMX6QDL_CLK_LDB_DI0_SEL]      = imx_clk_hw_mux_ldb("ldb_di0_sel", base + 0x2c, 9,  3, ldb_di_sels,      ARRAY_SIZE(ldb_di_sels));
		hws[IMX6QDL_CLK_LDB_DI1_SEL]      = imx_clk_hw_mux_ldb("ldb_di1_sel", base + 0x2c, 12, 3, ldb_di_sels,      ARRAY_SIZE(ldb_di_sels));
	}

	hws[IMX6QDL_CLK_IPU1_DI0_PRE_SEL] = imx_clk_hw_mux_flags("ipu1_di0_pre_sel", base + 0x34, 6,  3, ipu_di_pre_sels,   ARRAY_SIZE(ipu_di_pre_sels), CLK_SET_RATE_PARENT);
	hws[IMX6QDL_CLK_IPU1_DI1_PRE_SEL] = imx_clk_hw_mux_flags("ipu1_di1_pre_sel", base + 0x34, 15, 3, ipu_di_pre_sels,   ARRAY_SIZE(ipu_di_pre_sels), CLK_SET_RATE_PARENT);
	hws[IMX6QDL_CLK_IPU2_DI0_PRE_SEL] = imx_clk_hw_mux_flags("ipu2_di0_pre_sel", base + 0x38, 6,  3, ipu_di_pre_sels,   ARRAY_SIZE(ipu_di_pre_sels), CLK_SET_RATE_PARENT);
	hws[IMX6QDL_CLK_IPU2_DI1_PRE_SEL] = imx_clk_hw_mux_flags("ipu2_di1_pre_sel", base + 0x38, 15, 3, ipu_di_pre_sels,   ARRAY_SIZE(ipu_di_pre_sels), CLK_SET_RATE_PARENT);
	hws[IMX6QDL_CLK_HSI_TX_SEL]       = imx_clk_hw_mux("hsi_tx_sel",       base + 0x30, 28, 1, hsi_tx_sels,       ARRAY_SIZE(hsi_tx_sels));
	hws[IMX6QDL_CLK_PCIE_AXI_SEL]     = imx_clk_hw_mux("pcie_axi_sel",     base + 0x18, 10, 1, pcie_axi_sels,     ARRAY_SIZE(pcie_axi_sels));

	if (clk_on_imx6qp()) {
		hws[IMX6QDL_CLK_IPU1_DI0_SEL]     = imx_clk_hw_mux_flags("ipu1_di0_sel",     base + 0x34, 0,  3, ipu1_di0_sels_2,     ARRAY_SIZE(ipu1_di0_sels_2), CLK_SET_RATE_PARENT);
		hws[IMX6QDL_CLK_IPU1_DI1_SEL]     = imx_clk_hw_mux_flags("ipu1_di1_sel",     base + 0x34, 9,  3, ipu1_di1_sels_2,     ARRAY_SIZE(ipu1_di1_sels_2), CLK_SET_RATE_PARENT);
		hws[IMX6QDL_CLK_IPU2_DI0_SEL]     = imx_clk_hw_mux_flags("ipu2_di0_sel",     base + 0x38, 0,  3, ipu2_di0_sels_2,     ARRAY_SIZE(ipu2_di0_sels_2), CLK_SET_RATE_PARENT);
		hws[IMX6QDL_CLK_IPU2_DI1_SEL]     = imx_clk_hw_mux_flags("ipu2_di1_sel",     base + 0x38, 9,  3, ipu2_di1_sels_2,     ARRAY_SIZE(ipu2_di1_sels_2), CLK_SET_RATE_PARENT);
		hws[IMX6QDL_CLK_SSI1_SEL]         = imx_clk_hw_mux("ssi1_sel",   base + 0x1c, 10, 2, ssi_sels,          ARRAY_SIZE(ssi_sels));
		hws[IMX6QDL_CLK_SSI2_SEL]         = imx_clk_hw_mux("ssi2_sel",   base + 0x1c, 12, 2, ssi_sels,          ARRAY_SIZE(ssi_sels));
		hws[IMX6QDL_CLK_SSI3_SEL]         = imx_clk_hw_mux("ssi3_sel",   base + 0x1c, 14, 2, ssi_sels,          ARRAY_SIZE(ssi_sels));
		hws[IMX6QDL_CLK_USDHC1_SEL]       = imx_clk_hw_mux("usdhc1_sel", base + 0x1c, 16, 1, usdhc_sels,        ARRAY_SIZE(usdhc_sels));
		hws[IMX6QDL_CLK_USDHC2_SEL]       = imx_clk_hw_mux("usdhc2_sel", base + 0x1c, 17, 1, usdhc_sels,        ARRAY_SIZE(usdhc_sels));
		hws[IMX6QDL_CLK_USDHC3_SEL]       = imx_clk_hw_mux("usdhc3_sel", base + 0x1c, 18, 1, usdhc_sels,        ARRAY_SIZE(usdhc_sels));
		hws[IMX6QDL_CLK_USDHC4_SEL]       = imx_clk_hw_mux("usdhc4_sel", base + 0x1c, 19, 1, usdhc_sels,        ARRAY_SIZE(usdhc_sels));
		hws[IMX6QDL_CLK_ENFC_SEL]         = imx_clk_hw_mux("enfc_sel",         base + 0x2c, 15, 3, enfc_sels_2,         ARRAY_SIZE(enfc_sels_2));
		hws[IMX6QDL_CLK_EIM_SEL]          = imx_clk_hw_mux("eim_sel",      base + 0x1c, 27, 2, eim_sels,        ARRAY_SIZE(eim_sels));
		hws[IMX6QDL_CLK_EIM_SLOW_SEL]     = imx_clk_hw_mux("eim_slow_sel", base + 0x1c, 29, 2, eim_slow_sels,   ARRAY_SIZE(eim_slow_sels));
		hws[IMX6QDL_CLK_PRE_AXI]	  = imx_clk_hw_mux("pre_axi",	base + 0x18, 1,  1, pre_axi_sels,    ARRAY_SIZE(pre_axi_sels));
	} else {
		hws[IMX6QDL_CLK_IPU1_DI0_SEL]     = imx_clk_hw_mux_flags("ipu1_di0_sel",     base + 0x34, 0,  3, ipu1_di0_sels,     ARRAY_SIZE(ipu1_di0_sels), CLK_SET_RATE_PARENT);
		hws[IMX6QDL_CLK_IPU1_DI1_SEL]     = imx_clk_hw_mux_flags("ipu1_di1_sel",     base + 0x34, 9,  3, ipu1_di1_sels,     ARRAY_SIZE(ipu1_di1_sels), CLK_SET_RATE_PARENT);
		hws[IMX6QDL_CLK_IPU2_DI0_SEL]     = imx_clk_hw_mux_flags("ipu2_di0_sel",     base + 0x38, 0,  3, ipu2_di0_sels,     ARRAY_SIZE(ipu2_di0_sels), CLK_SET_RATE_PARENT);
		hws[IMX6QDL_CLK_IPU2_DI1_SEL]     = imx_clk_hw_mux_flags("ipu2_di1_sel",     base + 0x38, 9,  3, ipu2_di1_sels,     ARRAY_SIZE(ipu2_di1_sels), CLK_SET_RATE_PARENT);
		hws[IMX6QDL_CLK_SSI1_SEL]         = imx_clk_hw_fixup_mux("ssi1_sel",   base + 0x1c, 10, 2, ssi_sels,          ARRAY_SIZE(ssi_sels), imx_cscmr1_fixup);
		hws[IMX6QDL_CLK_SSI2_SEL]         = imx_clk_hw_fixup_mux("ssi2_sel",   base + 0x1c, 12, 2, ssi_sels,          ARRAY_SIZE(ssi_sels), imx_cscmr1_fixup);
		hws[IMX6QDL_CLK_SSI3_SEL]         = imx_clk_hw_fixup_mux("ssi3_sel",   base + 0x1c, 14, 2, ssi_sels,          ARRAY_SIZE(ssi_sels), imx_cscmr1_fixup);
		hws[IMX6QDL_CLK_USDHC1_SEL]       = imx_clk_hw_fixup_mux("usdhc1_sel", base + 0x1c, 16, 1, usdhc_sels,        ARRAY_SIZE(usdhc_sels), imx_cscmr1_fixup);
		hws[IMX6QDL_CLK_USDHC2_SEL]       = imx_clk_hw_fixup_mux("usdhc2_sel", base + 0x1c, 17, 1, usdhc_sels,        ARRAY_SIZE(usdhc_sels), imx_cscmr1_fixup);
		hws[IMX6QDL_CLK_USDHC3_SEL]       = imx_clk_hw_fixup_mux("usdhc3_sel", base + 0x1c, 18, 1, usdhc_sels,        ARRAY_SIZE(usdhc_sels), imx_cscmr1_fixup);
		hws[IMX6QDL_CLK_USDHC4_SEL]       = imx_clk_hw_fixup_mux("usdhc4_sel", base + 0x1c, 19, 1, usdhc_sels,        ARRAY_SIZE(usdhc_sels), imx_cscmr1_fixup);
		hws[IMX6QDL_CLK_ENFC_SEL]         = imx_clk_hw_mux("enfc_sel",         base + 0x2c, 16, 2, enfc_sels,         ARRAY_SIZE(enfc_sels));
		hws[IMX6QDL_CLK_EIM_SEL]          = imx_clk_hw_fixup_mux("eim_sel",      base + 0x1c, 27, 2, eim_sels,        ARRAY_SIZE(eim_sels), imx_cscmr1_fixup);
		hws[IMX6QDL_CLK_EIM_SLOW_SEL]     = imx_clk_hw_fixup_mux("eim_slow_sel", base + 0x1c, 29, 2, eim_slow_sels,   ARRAY_SIZE(eim_slow_sels), imx_cscmr1_fixup);
	}

	hws[IMX6QDL_CLK_VDO_AXI_SEL]      = imx_clk_hw_mux("vdo_axi_sel",      base + 0x18, 11, 1, vdo_axi_sels,      ARRAY_SIZE(vdo_axi_sels));
	hws[IMX6QDL_CLK_VPU_AXI_SEL]      = imx_clk_hw_mux("vpu_axi_sel",      base + 0x18, 14, 2, vpu_axi_sels,      ARRAY_SIZE(vpu_axi_sels));
	hws[IMX6QDL_CLK_CKO1_SEL]         = imx_clk_hw_mux("cko1_sel",         base + 0x60, 0,  4, cko1_sels,         ARRAY_SIZE(cko1_sels));
	hws[IMX6QDL_CLK_CKO2_SEL]         = imx_clk_hw_mux("cko2_sel",         base + 0x60, 16, 5, cko2_sels,         ARRAY_SIZE(cko2_sels));
	hws[IMX6QDL_CLK_CKO]              = imx_clk_hw_mux("cko",              base + 0x60, 8, 1,  cko_sels,          ARRAY_SIZE(cko_sels));

	/*                                          name         reg      shift width busy: reg, shift parent_names  num_parents */
	hws[IMX6QDL_CLK_PERIPH]  = imx_clk_hw_busy_mux("periph",  base + 0x14, 25,  1,   base + 0x48, 5,  periph_sels,  ARRAY_SIZE(periph_sels));
	hws[IMX6QDL_CLK_PERIPH2] = imx_clk_hw_busy_mux("periph2", base + 0x14, 26,  1,   base + 0x48, 3,  periph2_sels, ARRAY_SIZE(periph2_sels));

	/*                                                  name                parent_name          reg       shift width */
	hws[IMX6QDL_CLK_PERIPH_CLK2]      = imx_clk_hw_divider("periph_clk2",      "periph_clk2_sel",   base + 0x14, 27, 3);
	hws[IMX6QDL_CLK_PERIPH2_CLK2]     = imx_clk_hw_divider("periph2_clk2",     "periph2_clk2_sel",  base + 0x14, 0,  3);
	hws[IMX6QDL_CLK_IPG]              = imx_clk_hw_divider("ipg",              "ahb",               base + 0x14, 8,  2);
	hws[IMX6QDL_CLK_ESAI_PRED]        = imx_clk_hw_divider("esai_pred",        "esai_sel",          base + 0x28, 9,  3);
	hws[IMX6QDL_CLK_ESAI_PODF]        = imx_clk_hw_divider("esai_podf",        "esai_pred",         base + 0x28, 25, 3);
	hws[IMX6QDL_CLK_ASRC_PRED]        = imx_clk_hw_divider("asrc_pred",        "asrc_sel",          base + 0x30, 12, 3);
	hws[IMX6QDL_CLK_ASRC_PODF]        = imx_clk_hw_divider("asrc_podf",        "asrc_pred",         base + 0x30, 9,  3);
	hws[IMX6QDL_CLK_SPDIF_PRED]       = imx_clk_hw_divider("spdif_pred",       "spdif_sel",         base + 0x30, 25, 3);
	hws[IMX6QDL_CLK_SPDIF_PODF]       = imx_clk_hw_divider("spdif_podf",       "spdif_pred",        base + 0x30, 22, 3);

	if (clk_on_imx6qp()) {
		hws[IMX6QDL_CLK_IPG_PER] = imx_clk_hw_divider("ipg_per", "ipg_per_sel", base + 0x1c, 0, 6);
		hws[IMX6QDL_CLK_ECSPI_ROOT] = imx_clk_hw_divider("ecspi_root", "ecspi_sel", base + 0x38, 19, 6);
		hws[IMX6QDL_CLK_CAN_ROOT] = imx_clk_hw_divider("can_root", "can_sel", base + 0x20, 2, 6);
		hws[IMX6QDL_CLK_UART_SERIAL_PODF] = imx_clk_hw_divider("uart_serial_podf", "uart_sel", base + 0x24, 0, 6);
		hws[IMX6QDL_CLK_LDB_DI0_DIV_3_5] = imx_clk_hw_fixed_factor("ldb_di0_div_3_5", "ldb_di0", 2, 7);
		hws[IMX6QDL_CLK_LDB_DI1_DIV_3_5] = imx_clk_hw_fixed_factor("ldb_di1_div_3_5", "ldb_di1", 2, 7);
	} else {
		hws[IMX6QDL_CLK_ECSPI_ROOT] = imx_clk_hw_divider("ecspi_root", "pll3_60m", base + 0x38, 19, 6);
		hws[IMX6QDL_CLK_CAN_ROOT] = imx_clk_hw_divider("can_root", "pll3_60m", base + 0x20, 2, 6);
		hws[IMX6QDL_CLK_IPG_PER] = imx_clk_hw_fixup_divider("ipg_per", "ipg", base + 0x1c, 0, 6, imx_cscmr1_fixup);
		hws[IMX6QDL_CLK_UART_SERIAL_PODF] = imx_clk_hw_divider("uart_serial_podf", "pll3_80m",          base + 0x24, 0,  6);
		hws[IMX6QDL_CLK_LDB_DI0_DIV_3_5] = imx_clk_hw_fixed_factor("ldb_di0_div_3_5", "ldb_di0_sel", 2, 7);
		hws[IMX6QDL_CLK_LDB_DI1_DIV_3_5] = imx_clk_hw_fixed_factor("ldb_di1_div_3_5", "ldb_di1_sel", 2, 7);
	}

	if (clk_on_imx6dl())
		hws[IMX6QDL_CLK_MLB_PODF]  = imx_clk_hw_divider("mlb_podf",  "mlb_sel",    base + 0x18, 23, 3);
	else
		hws[IMX6QDL_CLK_GPU2D_CORE_PODF]  = imx_clk_hw_divider("gpu2d_core_podf",  "gpu2d_core_sel",    base + 0x18, 23, 3);
	hws[IMX6QDL_CLK_GPU3D_CORE_PODF]  = imx_clk_hw_divider("gpu3d_core_podf",  "gpu3d_core_sel",    base + 0x18, 26, 3);
	if (clk_on_imx6dl())
		hws[IMX6QDL_CLK_GPU2D_CORE_PODF]  = imx_clk_hw_divider("gpu2d_core_podf",     "gpu2d_core_sel",  base + 0x18, 29, 3);
	else
		hws[IMX6QDL_CLK_GPU3D_SHADER]     = imx_clk_hw_divider("gpu3d_shader",     "gpu3d_shader_sel",  base + 0x18, 29, 3);
	hws[IMX6QDL_CLK_IPU1_PODF]        = imx_clk_hw_divider("ipu1_podf",        "ipu1_sel",          base + 0x3c, 11, 3);
	hws[IMX6QDL_CLK_IPU2_PODF]        = imx_clk_hw_divider("ipu2_podf",        "ipu2_sel",          base + 0x3c, 16, 3);
	hws[IMX6QDL_CLK_LDB_DI0_PODF]     = imx_clk_hw_divider_flags("ldb_di0_podf", "ldb_di0_div_3_5", base + 0x20, 10, 1, 0);
	hws[IMX6QDL_CLK_LDB_DI1_PODF]     = imx_clk_hw_divider_flags("ldb_di1_podf", "ldb_di1_div_3_5", base + 0x20, 11, 1, 0);
	hws[IMX6QDL_CLK_IPU1_DI0_PRE]     = imx_clk_hw_divider("ipu1_di0_pre",     "ipu1_di0_pre_sel",  base + 0x34, 3,  3);
	hws[IMX6QDL_CLK_IPU1_DI1_PRE]     = imx_clk_hw_divider("ipu1_di1_pre",     "ipu1_di1_pre_sel",  base + 0x34, 12, 3);
	hws[IMX6QDL_CLK_IPU2_DI0_PRE]     = imx_clk_hw_divider("ipu2_di0_pre",     "ipu2_di0_pre_sel",  base + 0x38, 3,  3);
	hws[IMX6QDL_CLK_IPU2_DI1_PRE]     = imx_clk_hw_divider("ipu2_di1_pre",     "ipu2_di1_pre_sel",  base + 0x38, 12, 3);
	hws[IMX6QDL_CLK_HSI_TX_PODF]      = imx_clk_hw_divider("hsi_tx_podf",      "hsi_tx_sel",        base + 0x30, 29, 3);
	hws[IMX6QDL_CLK_SSI1_PRED]        = imx_clk_hw_divider("ssi1_pred",        "ssi1_sel",          base + 0x28, 6,  3);
	hws[IMX6QDL_CLK_SSI1_PODF]        = imx_clk_hw_divider("ssi1_podf",        "ssi1_pred",         base + 0x28, 0,  6);
	hws[IMX6QDL_CLK_SSI2_PRED]        = imx_clk_hw_divider("ssi2_pred",        "ssi2_sel",          base + 0x2c, 6,  3);
	hws[IMX6QDL_CLK_SSI2_PODF]        = imx_clk_hw_divider("ssi2_podf",        "ssi2_pred",         base + 0x2c, 0,  6);
	hws[IMX6QDL_CLK_SSI3_PRED]        = imx_clk_hw_divider("ssi3_pred",        "ssi3_sel",          base + 0x28, 22, 3);
	hws[IMX6QDL_CLK_SSI3_PODF]        = imx_clk_hw_divider("ssi3_podf",        "ssi3_pred",         base + 0x28, 16, 6);
	hws[IMX6QDL_CLK_USDHC1_PODF]      = imx_clk_hw_divider("usdhc1_podf",      "usdhc1_sel",        base + 0x24, 11, 3);
	hws[IMX6QDL_CLK_USDHC2_PODF]      = imx_clk_hw_divider("usdhc2_podf",      "usdhc2_sel",        base + 0x24, 16, 3);
	hws[IMX6QDL_CLK_USDHC3_PODF]      = imx_clk_hw_divider("usdhc3_podf",      "usdhc3_sel",        base + 0x24, 19, 3);
	hws[IMX6QDL_CLK_USDHC4_PODF]      = imx_clk_hw_divider("usdhc4_podf",      "usdhc4_sel",        base + 0x24, 22, 3);
	hws[IMX6QDL_CLK_ENFC_PRED]        = imx_clk_hw_divider("enfc_pred",        "enfc_sel",          base + 0x2c, 18, 3);
	hws[IMX6QDL_CLK_ENFC_PODF]        = imx_clk_hw_divider("enfc_podf",        "enfc_pred",         base + 0x2c, 21, 6);
	if (clk_on_imx6qp()) {
		hws[IMX6QDL_CLK_EIM_PODF]         = imx_clk_hw_divider("eim_podf",   "eim_sel",           base + 0x1c, 20, 3);
		hws[IMX6QDL_CLK_EIM_SLOW_PODF]    = imx_clk_hw_divider("eim_slow_podf", "eim_slow_sel",   base + 0x1c, 23, 3);
	} else {
		hws[IMX6QDL_CLK_EIM_PODF]         = imx_clk_hw_fixup_divider("eim_podf",   "eim_sel",           base + 0x1c, 20, 3, imx_cscmr1_fixup);
		hws[IMX6QDL_CLK_EIM_SLOW_PODF]    = imx_clk_hw_fixup_divider("eim_slow_podf", "eim_slow_sel",   base + 0x1c, 23, 3, imx_cscmr1_fixup);
	}

	hws[IMX6QDL_CLK_VPU_AXI_PODF]     = imx_clk_hw_divider("vpu_axi_podf",     "vpu_axi_sel",       base + 0x24, 25, 3);
	hws[IMX6QDL_CLK_CKO1_PODF]        = imx_clk_hw_divider("cko1_podf",        "cko1_sel",          base + 0x60, 4,  3);
	hws[IMX6QDL_CLK_CKO2_PODF]        = imx_clk_hw_divider("cko2_podf",        "cko2_sel",          base + 0x60, 21, 3);

	/*                                                        name                 parent_name    reg        shift width busy: reg, shift */
	hws[IMX6QDL_CLK_AXI]               = imx_clk_hw_busy_divider("axi",               "axi_sel",     base + 0x14, 16,  3,   base + 0x48, 0);
	hws[IMX6QDL_CLK_MMDC_CH0_AXI_PODF] = imx_clk_hw_busy_divider("mmdc_ch0_axi_podf", "periph",      base + 0x14, 19,  3,   base + 0x48, 4);
	if (clk_on_imx6qp()) {
		hws[IMX6QDL_CLK_MMDC_CH1_AXI_CG] = imx_clk_hw_gate("mmdc_ch1_axi_cg", "periph2", base + 0x4, 18);
		hws[IMX6QDL_CLK_MMDC_CH1_AXI_PODF] = imx_clk_hw_busy_divider("mmdc_ch1_axi_podf", "mmdc_ch1_axi_cg", base + 0x14, 3, 3, base + 0x48, 2);
	} else {
		hws[IMX6QDL_CLK_MMDC_CH1_AXI_PODF] = imx_clk_hw_busy_divider("mmdc_ch1_axi_podf", "periph2",     base + 0x14, 3,   3,   base + 0x48, 2);
	}
	hws[IMX6QDL_CLK_ARM]               = imx_clk_hw_busy_divider("arm",               "pll1_sw",     base + 0x10, 0,   3,   base + 0x48, 16);
	hws[IMX6QDL_CLK_AHB]               = imx_clk_hw_busy_divider("ahb",               "periph",      base + 0x14, 10,  3,   base + 0x48, 1);

	/*                                            name             parent_name          reg         shift */
	hws[IMX6QDL_CLK_APBH_DMA]     = imx_clk_hw_gate2("apbh_dma",      "usdhc3",            base + 0x68, 4);
	hws[IMX6QDL_CLK_ASRC]         = imx_clk_hw_gate2_shared("asrc",         "asrc_podf",   base + 0x68, 6, &share_count_asrc);
	hws[IMX6QDL_CLK_ASRC_IPG]     = imx_clk_hw_gate2_shared("asrc_ipg",     "ahb",         base + 0x68, 6, &share_count_asrc);
	hws[IMX6QDL_CLK_ASRC_MEM]     = imx_clk_hw_gate2_shared("asrc_mem",     "ahb",         base + 0x68, 6, &share_count_asrc);
	hws[IMX6QDL_CLK_CAAM_MEM]     = imx_clk_hw_gate2("caam_mem",      "ahb",               base + 0x68, 8);
	hws[IMX6QDL_CLK_CAAM_ACLK]    = imx_clk_hw_gate2("caam_aclk",     "ahb",               base + 0x68, 10);
	hws[IMX6QDL_CLK_CAAM_IPG]     = imx_clk_hw_gate2("caam_ipg",      "ipg",               base + 0x68, 12);
	hws[IMX6QDL_CLK_CAN1_IPG]     = imx_clk_hw_gate2("can1_ipg",      "ipg",               base + 0x68, 14);
	hws[IMX6QDL_CLK_CAN1_SERIAL]  = imx_clk_hw_gate2("can1_serial",   "can_root",          base + 0x68, 16);
	hws[IMX6QDL_CLK_CAN2_IPG]     = imx_clk_hw_gate2("can2_ipg",      "ipg",               base + 0x68, 18);
	hws[IMX6QDL_CLK_CAN2_SERIAL]  = imx_clk_hw_gate2("can2_serial",   "can_root",          base + 0x68, 20);
	hws[IMX6QDL_CLK_DCIC1]        = imx_clk_hw_gate2("dcic1",         "ipu1_podf",         base + 0x68, 24);
	hws[IMX6QDL_CLK_DCIC2]        = imx_clk_hw_gate2("dcic2",         "ipu2_podf",         base + 0x68, 26);
	hws[IMX6QDL_CLK_ECSPI1]       = imx_clk_hw_gate2("ecspi1",        "ecspi_root",        base + 0x6c, 0);
	hws[IMX6QDL_CLK_ECSPI2]       = imx_clk_hw_gate2("ecspi2",        "ecspi_root",        base + 0x6c, 2);
	hws[IMX6QDL_CLK_ECSPI3]       = imx_clk_hw_gate2("ecspi3",        "ecspi_root",        base + 0x6c, 4);
	hws[IMX6QDL_CLK_ECSPI4]       = imx_clk_hw_gate2("ecspi4",        "ecspi_root",        base + 0x6c, 6);
	if (clk_on_imx6dl())
		hws[IMX6DL_CLK_I2C4]  = imx_clk_hw_gate2("i2c4",          "ipg_per",           base + 0x6c, 8);
	else
		hws[IMX6Q_CLK_ECSPI5] = imx_clk_hw_gate2("ecspi5",        "ecspi_root",        base + 0x6c, 8);
	hws[IMX6QDL_CLK_ENET]         = imx_clk_hw_gate2("enet",          "ipg",               base + 0x6c, 10);
	hws[IMX6QDL_CLK_EPIT1]        = imx_clk_hw_gate2("epit1",         "ipg",               base + 0x6c, 12);
	hws[IMX6QDL_CLK_EPIT2]        = imx_clk_hw_gate2("epit2",         "ipg",               base + 0x6c, 14);
	hws[IMX6QDL_CLK_ESAI_EXTAL]   = imx_clk_hw_gate2_shared("esai_extal",   "esai_podf",   base + 0x6c, 16, &share_count_esai);
	hws[IMX6QDL_CLK_ESAI_IPG]     = imx_clk_hw_gate2_shared("esai_ipg",   "ahb",           base + 0x6c, 16, &share_count_esai);
	hws[IMX6QDL_CLK_ESAI_MEM]     = imx_clk_hw_gate2_shared("esai_mem", "ahb",             base + 0x6c, 16, &share_count_esai);
	hws[IMX6QDL_CLK_GPT_IPG]      = imx_clk_hw_gate2("gpt_ipg",       "ipg",               base + 0x6c, 20);
	hws[IMX6QDL_CLK_GPT_IPG_PER]  = imx_clk_hw_gate2("gpt_ipg_per",   "ipg_per",           base + 0x6c, 22);
	hws[IMX6QDL_CLK_GPU2D_CORE] = imx_clk_hw_gate2("gpu2d_core", "gpu2d_core_podf", base + 0x6c, 24);
	hws[IMX6QDL_CLK_GPU3D_CORE]   = imx_clk_hw_gate2("gpu3d_core",    "gpu3d_core_podf",   base + 0x6c, 26);
	hws[IMX6QDL_CLK_HDMI_IAHB]    = imx_clk_hw_gate2("hdmi_iahb",     "ahb",               base + 0x70, 0);
	hws[IMX6QDL_CLK_HDMI_ISFR]    = imx_clk_hw_gate2("hdmi_isfr",     "mipi_core_cfg",     base + 0x70, 4);
	hws[IMX6QDL_CLK_I2C1]         = imx_clk_hw_gate2("i2c1",          "ipg_per",           base + 0x70, 6);
	hws[IMX6QDL_CLK_I2C2]         = imx_clk_hw_gate2("i2c2",          "ipg_per",           base + 0x70, 8);
	hws[IMX6QDL_CLK_I2C3]         = imx_clk_hw_gate2("i2c3",          "ipg_per",           base + 0x70, 10);
	hws[IMX6QDL_CLK_IIM]          = imx_clk_hw_gate2("iim",           "ipg",               base + 0x70, 12);
	hws[IMX6QDL_CLK_ENFC]         = imx_clk_hw_gate2("enfc",          "enfc_podf",         base + 0x70, 14);
	hws[IMX6QDL_CLK_VDOA]         = imx_clk_hw_gate2("vdoa",          "vdo_axi",           base + 0x70, 26);
	hws[IMX6QDL_CLK_IPU1]         = imx_clk_hw_gate2("ipu1",          "ipu1_podf",         base + 0x74, 0);
	hws[IMX6QDL_CLK_IPU1_DI0]     = imx_clk_hw_gate2("ipu1_di0",      "ipu1_di0_sel",      base + 0x74, 2);
	hws[IMX6QDL_CLK_IPU1_DI1]     = imx_clk_hw_gate2("ipu1_di1",      "ipu1_di1_sel",      base + 0x74, 4);
	hws[IMX6QDL_CLK_IPU2]         = imx_clk_hw_gate2("ipu2",          "ipu2_podf",         base + 0x74, 6);
	hws[IMX6QDL_CLK_IPU2_DI0]     = imx_clk_hw_gate2("ipu2_di0",      "ipu2_di0_sel",      base + 0x74, 8);
	if (clk_on_imx6qp()) {
		hws[IMX6QDL_CLK_LDB_DI0]      = imx_clk_hw_gate2("ldb_di0",       "ldb_di0_sel",      base + 0x74, 12);
		hws[IMX6QDL_CLK_LDB_DI1]      = imx_clk_hw_gate2("ldb_di1",       "ldb_di1_sel",      base + 0x74, 14);
	} else {
		hws[IMX6QDL_CLK_LDB_DI0]      = imx_clk_hw_gate2("ldb_di0",       "ldb_di0_podf",      base + 0x74, 12);
		hws[IMX6QDL_CLK_LDB_DI1]      = imx_clk_hw_gate2("ldb_di1",       "ldb_di1_podf",      base + 0x74, 14);
	}
	hws[IMX6QDL_CLK_IPU2_DI1]     = imx_clk_hw_gate2("ipu2_di1",      "ipu2_di1_sel",      base + 0x74, 10);
	hws[IMX6QDL_CLK_HSI_TX]       = imx_clk_hw_gate2_shared("hsi_tx", "hsi_tx_podf",       base + 0x74, 16, &share_count_mipi_core_cfg);
	hws[IMX6QDL_CLK_MIPI_CORE_CFG] = imx_clk_hw_gate2_shared("mipi_core_cfg", "video_27m", base + 0x74, 16, &share_count_mipi_core_cfg);
	hws[IMX6QDL_CLK_MIPI_IPG]     = imx_clk_hw_gate2_shared("mipi_ipg", "ipg",             base + 0x74, 16, &share_count_mipi_core_cfg);

	if (clk_on_imx6dl())
		/*
		 * The multiplexer and divider of the imx6q clock gpu2d get
		 * redefined/reused as mlb_sys_sel and mlb_sys_clk_podf on imx6dl.
		 */
		hws[IMX6QDL_CLK_MLB] = imx_clk_hw_gate2("mlb",            "mlb_podf",   base + 0x74, 18);
	else
		hws[IMX6QDL_CLK_MLB] = imx_clk_hw_gate2("mlb",            "axi",               base + 0x74, 18);
	hws[IMX6QDL_CLK_MMDC_CH0_AXI] = imx_clk_hw_gate2_flags("mmdc_ch0_axi",  "mmdc_ch0_axi_podf", base + 0x74, 20, CLK_IS_CRITICAL);
	hws[IMX6QDL_CLK_MMDC_CH1_AXI] = imx_clk_hw_gate2("mmdc_ch1_axi",  "mmdc_ch1_axi_podf", base + 0x74, 22);
	hws[IMX6QDL_CLK_MMDC_P0_IPG]  = imx_clk_hw_gate2_flags("mmdc_p0_ipg",   "ipg",         base + 0x74, 24, CLK_IS_CRITICAL);
	hws[IMX6QDL_CLK_OCRAM]        = imx_clk_hw_gate2("ocram",         "ahb",               base + 0x74, 28);
	hws[IMX6QDL_CLK_OPENVG_AXI]   = imx_clk_hw_gate2("openvg_axi",    "axi",               base + 0x74, 30);
	hws[IMX6QDL_CLK_PCIE_AXI]     = imx_clk_hw_gate2("pcie_axi",      "pcie_axi_sel",      base + 0x78, 0);
	hws[IMX6QDL_CLK_PER1_BCH]     = imx_clk_hw_gate2("per1_bch",      "usdhc3",            base + 0x78, 12);
	hws[IMX6QDL_CLK_PWM1]         = imx_clk_hw_gate2("pwm1",          "ipg_per",           base + 0x78, 16);
	hws[IMX6QDL_CLK_PWM2]         = imx_clk_hw_gate2("pwm2",          "ipg_per",           base + 0x78, 18);
	hws[IMX6QDL_CLK_PWM3]         = imx_clk_hw_gate2("pwm3",          "ipg_per",           base + 0x78, 20);
	hws[IMX6QDL_CLK_PWM4]         = imx_clk_hw_gate2("pwm4",          "ipg_per",           base + 0x78, 22);
	hws[IMX6QDL_CLK_GPMI_BCH_APB] = imx_clk_hw_gate2("gpmi_bch_apb",  "usdhc3",            base + 0x78, 24);
	hws[IMX6QDL_CLK_GPMI_BCH]     = imx_clk_hw_gate2("gpmi_bch",      "usdhc4",            base + 0x78, 26);
	hws[IMX6QDL_CLK_GPMI_IO]      = imx_clk_hw_gate2("gpmi_io",       "enfc",              base + 0x78, 28);
	hws[IMX6QDL_CLK_GPMI_APB]     = imx_clk_hw_gate2("gpmi_apb",      "usdhc3",            base + 0x78, 30);
	hws[IMX6QDL_CLK_ROM]          = imx_clk_hw_gate2_flags("rom",     "ahb",               base + 0x7c, 0, CLK_IS_CRITICAL);
	hws[IMX6QDL_CLK_SATA]         = imx_clk_hw_gate2("sata",          "ahb",               base + 0x7c, 4);
	hws[IMX6QDL_CLK_SDMA]         = imx_clk_hw_gate2("sdma",          "ahb",               base + 0x7c, 6);
	hws[IMX6QDL_CLK_SPBA]         = imx_clk_hw_gate2("spba",          "ipg",               base + 0x7c, 12);
	hws[IMX6QDL_CLK_SPDIF]        = imx_clk_hw_gate2_shared("spdif",     "spdif_podf",     base + 0x7c, 14, &share_count_spdif);
	hws[IMX6QDL_CLK_SPDIF_GCLK]   = imx_clk_hw_gate2_shared("spdif_gclk", "ipg",           base + 0x7c, 14, &share_count_spdif);
	hws[IMX6QDL_CLK_SSI1_IPG]     = imx_clk_hw_gate2_shared("ssi1_ipg",      "ipg",        base + 0x7c, 18, &share_count_ssi1);
	hws[IMX6QDL_CLK_SSI2_IPG]     = imx_clk_hw_gate2_shared("ssi2_ipg",      "ipg",        base + 0x7c, 20, &share_count_ssi2);
	hws[IMX6QDL_CLK_SSI3_IPG]     = imx_clk_hw_gate2_shared("ssi3_ipg",      "ipg",        base + 0x7c, 22, &share_count_ssi3);
	hws[IMX6QDL_CLK_SSI1]         = imx_clk_hw_gate2_shared("ssi1",          "ssi1_podf",  base + 0x7c, 18, &share_count_ssi1);
	hws[IMX6QDL_CLK_SSI2]         = imx_clk_hw_gate2_shared("ssi2",          "ssi2_podf",  base + 0x7c, 20, &share_count_ssi2);
	hws[IMX6QDL_CLK_SSI3]         = imx_clk_hw_gate2_shared("ssi3",          "ssi3_podf",  base + 0x7c, 22, &share_count_ssi3);
	hws[IMX6QDL_CLK_UART_IPG]     = imx_clk_hw_gate2("uart_ipg",      "ipg",               base + 0x7c, 24);
	hws[IMX6QDL_CLK_UART_SERIAL]  = imx_clk_hw_gate2("uart_serial",   "uart_serial_podf",  base + 0x7c, 26);
	hws[IMX6QDL_CLK_USBOH3]       = imx_clk_hw_gate2("usboh3",        "ipg",               base + 0x80, 0);
	hws[IMX6QDL_CLK_USDHC1]       = imx_clk_hw_gate2("usdhc1",        "usdhc1_podf",       base + 0x80, 2);
	hws[IMX6QDL_CLK_USDHC2]       = imx_clk_hw_gate2("usdhc2",        "usdhc2_podf",       base + 0x80, 4);
	hws[IMX6QDL_CLK_USDHC3]       = imx_clk_hw_gate2("usdhc3",        "usdhc3_podf",       base + 0x80, 6);
	hws[IMX6QDL_CLK_USDHC4]       = imx_clk_hw_gate2("usdhc4",        "usdhc4_podf",       base + 0x80, 8);
	hws[IMX6QDL_CLK_EIM_SLOW]     = imx_clk_hw_gate2("eim_slow",      "eim_slow_podf",     base + 0x80, 10);
	hws[IMX6QDL_CLK_VDO_AXI]      = imx_clk_hw_gate2("vdo_axi",       "vdo_axi_sel",       base + 0x80, 12);
	hws[IMX6QDL_CLK_VPU_AXI]      = imx_clk_hw_gate2("vpu_axi",       "vpu_axi_podf",      base + 0x80, 14);
	if (clk_on_imx6qp()) {
		hws[IMX6QDL_CLK_PRE0] = imx_clk_hw_gate2("pre0",	       "pre_axi",	    base + 0x80, 16);
		hws[IMX6QDL_CLK_PRE1] = imx_clk_hw_gate2("pre1",	       "pre_axi",	    base + 0x80, 18);
		hws[IMX6QDL_CLK_PRE2] = imx_clk_hw_gate2("pre2",	       "pre_axi",         base + 0x80, 20);
		hws[IMX6QDL_CLK_PRE3] = imx_clk_hw_gate2("pre3",	       "pre_axi",	    base + 0x80, 22);
		hws[IMX6QDL_CLK_PRG0_AXI] = imx_clk_hw_gate2_shared("prg0_axi",  "ipu1_podf",  base + 0x80, 24, &share_count_prg0);
		hws[IMX6QDL_CLK_PRG1_AXI] = imx_clk_hw_gate2_shared("prg1_axi",  "ipu2_podf",  base + 0x80, 26, &share_count_prg1);
		hws[IMX6QDL_CLK_PRG0_APB] = imx_clk_hw_gate2_shared("prg0_apb",  "ipg",	    base + 0x80, 24, &share_count_prg0);
		hws[IMX6QDL_CLK_PRG1_APB] = imx_clk_hw_gate2_shared("prg1_apb",  "ipg",	    base + 0x80, 26, &share_count_prg1);
	}
	hws[IMX6QDL_CLK_CKO1]         = imx_clk_hw_gate("cko1",           "cko1_podf",         base + 0x60, 7);
	hws[IMX6QDL_CLK_CKO2]         = imx_clk_hw_gate("cko2",           "cko2_podf",         base + 0x60, 24);

	/*
	 * The gpt_3m clock is not available on i.MX6Q TO1.0.  Let's point it
	 * to clock gpt_ipg_per to ease the gpt driver code.
	 */
	if (clk_on_imx6q() && imx_get_soc_revision() == IMX_CHIP_REVISION_1_0)
		hws[IMX6QDL_CLK_GPT_3M] = hws[IMX6QDL_CLK_GPT_IPG_PER];

	imx_check_clk_hws(hws, IMX6QDL_CLK_END);

	of_clk_add_hw_provider(np, of_clk_hw_onecell_get, clk_hw_data);

	clk_hw_register_clkdev(hws[IMX6QDL_CLK_ENET_REF], "enet_ref", NULL);

	clk_set_rate(hws[IMX6QDL_CLK_PLL3_PFD1_540M]->clk, 540000000);
	if (clk_on_imx6dl())
		clk_set_parent(hws[IMX6QDL_CLK_IPU1_SEL]->clk, hws[IMX6QDL_CLK_PLL3_PFD1_540M]->clk);

	clk_set_parent(hws[IMX6QDL_CLK_IPU1_DI0_PRE_SEL]->clk, hws[IMX6QDL_CLK_PLL5_VIDEO_DIV]->clk);
	clk_set_parent(hws[IMX6QDL_CLK_IPU1_DI1_PRE_SEL]->clk, hws[IMX6QDL_CLK_PLL5_VIDEO_DIV]->clk);
	clk_set_parent(hws[IMX6QDL_CLK_IPU2_DI0_PRE_SEL]->clk, hws[IMX6QDL_CLK_PLL5_VIDEO_DIV]->clk);
	clk_set_parent(hws[IMX6QDL_CLK_IPU2_DI1_PRE_SEL]->clk, hws[IMX6QDL_CLK_PLL5_VIDEO_DIV]->clk);
	clk_set_parent(hws[IMX6QDL_CLK_IPU1_DI0_SEL]->clk, hws[IMX6QDL_CLK_IPU1_DI0_PRE]->clk);
	clk_set_parent(hws[IMX6QDL_CLK_IPU1_DI1_SEL]->clk, hws[IMX6QDL_CLK_IPU1_DI1_PRE]->clk);
	clk_set_parent(hws[IMX6QDL_CLK_IPU2_DI0_SEL]->clk, hws[IMX6QDL_CLK_IPU2_DI0_PRE]->clk);
	clk_set_parent(hws[IMX6QDL_CLK_IPU2_DI1_SEL]->clk, hws[IMX6QDL_CLK_IPU2_DI1_PRE]->clk);

	/*
	 * The gpmi needs 100MHz frequency in the EDO/Sync mode,
	 * We can not get the 100MHz from the pll2_pfd0_352m.
	 * So choose pll2_pfd2_396m as enfc_sel's parent.
	 */
	clk_set_parent(hws[IMX6QDL_CLK_ENFC_SEL]->clk, hws[IMX6QDL_CLK_PLL2_PFD2_396M]->clk);

	if (IS_ENABLED(CONFIG_USB_MXS_PHY)) {
		clk_prepare_enable(hws[IMX6QDL_CLK_USBPHY1_GATE]->clk);
		clk_prepare_enable(hws[IMX6QDL_CLK_USBPHY2_GATE]->clk);
	}

	/*
	 * Let's initially set up CLKO with OSC24M, since this configuration
	 * is widely used by imx6q board designs to clock audio codec.
	 */
	ret = clk_set_parent(hws[IMX6QDL_CLK_CKO2_SEL]->clk, hws[IMX6QDL_CLK_OSC]->clk);
	if (!ret)
		ret = clk_set_parent(hws[IMX6QDL_CLK_CKO]->clk, hws[IMX6QDL_CLK_CKO2]->clk);
	if (ret)
		pr_warn("failed to set up CLKO: %d\n", ret);

	/* Audio-related clocks configuration */
	clk_set_parent(hws[IMX6QDL_CLK_SPDIF_SEL]->clk, hws[IMX6QDL_CLK_PLL3_PFD3_454M]->clk);

	/* All existing boards with PCIe use LVDS1 */
	if (IS_ENABLED(CONFIG_PCI_IMX6))
		clk_set_parent(hws[IMX6QDL_CLK_LVDS1_SEL]->clk, hws[IMX6QDL_CLK_SATA_REF_100M]->clk);

	/*
	 * Initialize the GPU clock muxes, so that the maximum specified clock
	 * rates for the respective SoC are not exceeded.
	 */
	if (clk_on_imx6dl()) {
		clk_set_parent(hws[IMX6QDL_CLK_GPU3D_CORE_SEL]->clk,
			       hws[IMX6QDL_CLK_PLL2_PFD1_594M]->clk);
		clk_set_parent(hws[IMX6QDL_CLK_GPU2D_CORE_SEL]->clk,
			       hws[IMX6QDL_CLK_PLL2_PFD1_594M]->clk);
	} else if (clk_on_imx6q()) {
		clk_set_parent(hws[IMX6QDL_CLK_GPU3D_CORE_SEL]->clk,
			       hws[IMX6QDL_CLK_MMDC_CH0_AXI]->clk);
		clk_set_parent(hws[IMX6QDL_CLK_GPU3D_SHADER_SEL]->clk,
			       hws[IMX6QDL_CLK_PLL2_PFD1_594M]->clk);
		clk_set_parent(hws[IMX6QDL_CLK_GPU2D_CORE_SEL]->clk,
			       hws[IMX6QDL_CLK_PLL3_USB_OTG]->clk);
	}

	for (i = 0; i < ARRAY_SIZE(uart_clk_ids); i++) {
		int index = uart_clk_ids[i];

		uart_clks[i] = &hws[index]->clk;
	}

	imx_register_uart_clocks(uart_clks);
}
CLK_OF_DECLARE(imx6q, "fsl,imx6q-ccm", imx6q_clocks_init);
