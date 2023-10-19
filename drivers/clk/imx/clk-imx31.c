// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2012 Sascha Hauer <kernel@pengutronix.de>
 */

#include <linux/module.h>
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/io.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <soc/imx/revision.h>
#include <soc/imx/timer.h>
#include <asm/irq.h>

#include "clk.h"

#define MX31_CCM_BASE_ADDR	0x53f80000
#define MX31_GPT1_BASE_ADDR	0x53f90000
#define MX31_INT_GPT		(NR_IRQS_LEGACY + 29)

#define MXC_CCM_CCMR		0x00
#define MXC_CCM_PDR0		0x04
#define MXC_CCM_PDR1		0x08
#define MXC_CCM_MPCTL		0x10
#define MXC_CCM_UPCTL		0x14
#define MXC_CCM_SRPCTL		0x18
#define MXC_CCM_CGR0		0x20
#define MXC_CCM_CGR1		0x24
#define MXC_CCM_CGR2		0x28
#define MXC_CCM_PMCR0		0x5c

static const char *mcu_main_sel[] = { "spll", "mpll", };
static const char *per_sel[] = { "per_div", "ipg", };
static const char *csi_sel[] = { "upll", "spll", };
static const char *fir_sel[] = { "mcu_main", "upll", "spll" };

enum mx31_clks {
	dummy, ckih, ckil, mpll, spll, upll, mcu_main, hsp, ahb, nfc, ipg,
	per_div, per, csi, fir, csi_div, usb_div_pre, usb_div_post, fir_div_pre,
	fir_div_post, sdhc1_gate, sdhc2_gate, gpt_gate, epit1_gate, epit2_gate,
	iim_gate, ata_gate, sdma_gate, cspi3_gate, rng_gate, uart1_gate,
	uart2_gate, ssi1_gate, i2c1_gate, i2c2_gate, i2c3_gate, hantro_gate,
	mstick1_gate, mstick2_gate, csi_gate, rtc_gate, wdog_gate, pwm_gate,
	sim_gate, ect_gate, usb_gate, kpp_gate, ipu_gate, uart3_gate,
	uart4_gate, uart5_gate, owire_gate, ssi2_gate, cspi1_gate, cspi2_gate,
	gacc_gate, emi_gate, rtic_gate, firi_gate, clk_max
};

static struct clk *clk[clk_max];
static struct clk_onecell_data clk_data;

static void __init _mx31_clocks_init(void __iomem *base, unsigned long fref)
{
	clk[dummy] = imx_clk_fixed("dummy", 0);
	clk[ckih] = imx_clk_fixed("ckih", fref);
	clk[ckil] = imx_clk_fixed("ckil", 32768);
	clk[mpll] = imx_clk_pllv1(IMX_PLLV1_IMX31, "mpll", "ckih", base + MXC_CCM_MPCTL);
	clk[spll] = imx_clk_pllv1(IMX_PLLV1_IMX31, "spll", "ckih", base + MXC_CCM_SRPCTL);
	clk[upll] = imx_clk_pllv1(IMX_PLLV1_IMX31, "upll", "ckih", base + MXC_CCM_UPCTL);
	clk[mcu_main] = imx_clk_mux("mcu_main", base + MXC_CCM_PMCR0, 31, 1, mcu_main_sel, ARRAY_SIZE(mcu_main_sel));
	clk[hsp] = imx_clk_divider("hsp", "mcu_main", base + MXC_CCM_PDR0, 11, 3);
	clk[ahb] = imx_clk_divider("ahb", "mcu_main", base + MXC_CCM_PDR0, 3, 3);
	clk[nfc] = imx_clk_divider("nfc", "ahb", base + MXC_CCM_PDR0, 8, 3);
	clk[ipg] = imx_clk_divider("ipg", "ahb", base + MXC_CCM_PDR0, 6, 2);
	clk[per_div] = imx_clk_divider("per_div", "upll", base + MXC_CCM_PDR0, 16, 5);
	clk[per] = imx_clk_mux("per", base + MXC_CCM_CCMR, 24, 1, per_sel, ARRAY_SIZE(per_sel));
	clk[csi] = imx_clk_mux("csi_sel", base + MXC_CCM_CCMR, 25, 1, csi_sel, ARRAY_SIZE(csi_sel));
	clk[fir] = imx_clk_mux("fir_sel", base + MXC_CCM_CCMR, 11, 2, fir_sel, ARRAY_SIZE(fir_sel));
	clk[csi_div] = imx_clk_divider("csi_div", "csi_sel", base + MXC_CCM_PDR0, 23, 9);
	clk[usb_div_pre] = imx_clk_divider("usb_div_pre", "upll", base + MXC_CCM_PDR1, 30, 2);
	clk[usb_div_post] = imx_clk_divider("usb_div_post", "usb_div_pre", base + MXC_CCM_PDR1, 27, 3);
	clk[fir_div_pre] = imx_clk_divider("fir_div_pre", "fir_sel", base + MXC_CCM_PDR1, 24, 3);
	clk[fir_div_post] = imx_clk_divider("fir_div_post", "fir_div_pre", base + MXC_CCM_PDR1, 23, 6);
	clk[sdhc1_gate] = imx_clk_gate2("sdhc1_gate", "per", base + MXC_CCM_CGR0, 0);
	clk[sdhc2_gate] = imx_clk_gate2("sdhc2_gate", "per", base + MXC_CCM_CGR0, 2);
	clk[gpt_gate] = imx_clk_gate2("gpt_gate", "per", base + MXC_CCM_CGR0, 4);
	clk[epit1_gate] = imx_clk_gate2("epit1_gate", "per", base + MXC_CCM_CGR0, 6);
	clk[epit2_gate] = imx_clk_gate2("epit2_gate", "per", base + MXC_CCM_CGR0, 8);
	clk[iim_gate] = imx_clk_gate2("iim_gate", "ipg", base + MXC_CCM_CGR0, 10);
	clk[ata_gate] = imx_clk_gate2("ata_gate", "ipg", base + MXC_CCM_CGR0, 12);
	clk[sdma_gate] = imx_clk_gate2("sdma_gate", "ahb", base + MXC_CCM_CGR0, 14);
	clk[cspi3_gate] = imx_clk_gate2("cspi3_gate", "ipg", base + MXC_CCM_CGR0, 16);
	clk[rng_gate] = imx_clk_gate2("rng_gate", "ipg", base + MXC_CCM_CGR0, 18);
	clk[uart1_gate] = imx_clk_gate2("uart1_gate", "per", base + MXC_CCM_CGR0, 20);
	clk[uart2_gate] = imx_clk_gate2("uart2_gate", "per", base + MXC_CCM_CGR0, 22);
	clk[ssi1_gate] = imx_clk_gate2("ssi1_gate", "spll", base + MXC_CCM_CGR0, 24);
	clk[i2c1_gate] = imx_clk_gate2("i2c1_gate", "per", base + MXC_CCM_CGR0, 26);
	clk[i2c2_gate] = imx_clk_gate2("i2c2_gate", "per", base + MXC_CCM_CGR0, 28);
	clk[i2c3_gate] = imx_clk_gate2("i2c3_gate", "per", base + MXC_CCM_CGR0, 30);
	clk[hantro_gate] = imx_clk_gate2("hantro_gate", "per", base + MXC_CCM_CGR1, 0);
	clk[mstick1_gate] = imx_clk_gate2("mstick1_gate", "per", base + MXC_CCM_CGR1, 2);
	clk[mstick2_gate] = imx_clk_gate2("mstick2_gate", "per", base + MXC_CCM_CGR1, 4);
	clk[csi_gate] = imx_clk_gate2("csi_gate", "csi_div", base + MXC_CCM_CGR1, 6);
	clk[rtc_gate] = imx_clk_gate2("rtc_gate", "ipg", base + MXC_CCM_CGR1, 8);
	clk[wdog_gate] = imx_clk_gate2("wdog_gate", "ipg", base + MXC_CCM_CGR1, 10);
	clk[pwm_gate] = imx_clk_gate2("pwm_gate", "per", base + MXC_CCM_CGR1, 12);
	clk[sim_gate] = imx_clk_gate2("sim_gate", "per", base + MXC_CCM_CGR1, 14);
	clk[ect_gate] = imx_clk_gate2("ect_gate", "per", base + MXC_CCM_CGR1, 16);
	clk[usb_gate] = imx_clk_gate2("usb_gate", "ahb", base + MXC_CCM_CGR1, 18);
	clk[kpp_gate] = imx_clk_gate2("kpp_gate", "ipg", base + MXC_CCM_CGR1, 20);
	clk[ipu_gate] = imx_clk_gate2("ipu_gate", "hsp", base + MXC_CCM_CGR1, 22);
	clk[uart3_gate] = imx_clk_gate2("uart3_gate", "per", base + MXC_CCM_CGR1, 24);
	clk[uart4_gate] = imx_clk_gate2("uart4_gate", "per", base + MXC_CCM_CGR1, 26);
	clk[uart5_gate] = imx_clk_gate2("uart5_gate", "per", base + MXC_CCM_CGR1, 28);
	clk[owire_gate] = imx_clk_gate2("owire_gate", "per", base + MXC_CCM_CGR1, 30);
	clk[ssi2_gate] = imx_clk_gate2("ssi2_gate", "spll", base + MXC_CCM_CGR2, 0);
	clk[cspi1_gate] = imx_clk_gate2("cspi1_gate", "ipg", base + MXC_CCM_CGR2, 2);
	clk[cspi2_gate] = imx_clk_gate2("cspi2_gate", "ipg", base + MXC_CCM_CGR2, 4);
	clk[gacc_gate] = imx_clk_gate2("gacc_gate", "per", base + MXC_CCM_CGR2, 6);
	clk[emi_gate] = imx_clk_gate2("emi_gate", "ahb", base + MXC_CCM_CGR2, 8);
	clk[rtic_gate] = imx_clk_gate2("rtic_gate", "ahb", base + MXC_CCM_CGR2, 10);
	clk[firi_gate] = imx_clk_gate2("firi_gate", "upll", base+MXC_CCM_CGR2, 12);

	imx_check_clocks(clk, ARRAY_SIZE(clk));

	clk_set_parent(clk[csi], clk[upll]);
	clk_prepare_enable(clk[emi_gate]);
	clk_prepare_enable(clk[iim_gate]);
	mx31_revision();
	clk_disable_unprepare(clk[iim_gate]);
}

static void __init mx31_clocks_init_dt(struct device_node *np)
{
	struct device_node *osc_np;
	u32 fref = 26000000; /* default */
	void __iomem *ccm;

	for_each_compatible_node(osc_np, NULL, "fixed-clock") {
		if (!of_device_is_compatible(osc_np, "fsl,imx-osc26m"))
			continue;

		if (!of_property_read_u32(osc_np, "clock-frequency", &fref)) {
			of_node_put(osc_np);
			break;
		}
	}

	ccm = of_iomap(np, 0);
	if (!ccm)
		panic("%s: failed to map registers\n", __func__);

	_mx31_clocks_init(ccm, fref);

	clk_data.clks = clk;
	clk_data.clk_num = ARRAY_SIZE(clk);
	of_clk_add_provider(np, of_clk_src_onecell_get, &clk_data);
}

CLK_OF_DECLARE(imx31_ccm, "fsl,imx31-ccm", mx31_clocks_init_dt);
