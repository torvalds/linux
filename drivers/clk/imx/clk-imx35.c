// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2012 Sascha Hauer, Pengutronix <s.hauer@pengutronix.de>
 */
#include <linux/mm.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/clkdev.h>
#include <linux/of.h>
#include <linux/err.h>
#include <soc/imx/revision.h>
#include <soc/imx/timer.h>
#include <asm/irq.h>

#include "clk.h"

#define MX35_CCM_BASE_ADDR	0x53f80000
#define MX35_GPT1_BASE_ADDR	0x53f90000
#define MX35_INT_GPT		(NR_IRQS_LEGACY + 29)

#define MXC_CCM_PDR0		0x04
#define MX35_CCM_PDR2		0x0c
#define MX35_CCM_PDR3		0x10
#define MX35_CCM_PDR4		0x14
#define MX35_CCM_MPCTL		0x1c
#define MX35_CCM_PPCTL		0x20
#define MX35_CCM_CGR0		0x2c
#define MX35_CCM_CGR1		0x30
#define MX35_CCM_CGR2		0x34
#define MX35_CCM_CGR3		0x38

struct arm_ahb_div {
	unsigned char arm, ahb, sel;
};

static struct arm_ahb_div clk_consumer[] = {
	{ .arm = 1, .ahb = 4, .sel = 0},
	{ .arm = 1, .ahb = 3, .sel = 1},
	{ .arm = 2, .ahb = 2, .sel = 0},
	{ .arm = 0, .ahb = 0, .sel = 0},
	{ .arm = 0, .ahb = 0, .sel = 0},
	{ .arm = 0, .ahb = 0, .sel = 0},
	{ .arm = 4, .ahb = 1, .sel = 0},
	{ .arm = 1, .ahb = 5, .sel = 0},
	{ .arm = 1, .ahb = 8, .sel = 0},
	{ .arm = 1, .ahb = 6, .sel = 1},
	{ .arm = 2, .ahb = 4, .sel = 0},
	{ .arm = 0, .ahb = 0, .sel = 0},
	{ .arm = 0, .ahb = 0, .sel = 0},
	{ .arm = 0, .ahb = 0, .sel = 0},
	{ .arm = 4, .ahb = 2, .sel = 0},
	{ .arm = 0, .ahb = 0, .sel = 0},
};

static char hsp_div_532[] = { 4, 8, 3, 0 };
static char hsp_div_400[] = { 3, 6, 3, 0 };

static struct clk_onecell_data clk_data;

static const char *std_sel[] = {"ppll", "arm"};
static const char *ipg_per_sel[] = {"ahb_per_div", "arm_per_div"};

enum mx35_clks {
	/*  0 */ ckih, mpll, ppll, mpll_075, arm, hsp, hsp_div, hsp_sel, ahb,
	/*  9 */ ipg, arm_per_div, ahb_per_div, ipg_per, uart_sel, uart_div,
	/* 15 */ esdhc_sel, esdhc1_div, esdhc2_div, esdhc3_div, spdif_sel,
	/* 20 */ spdif_div_pre, spdif_div_post, ssi_sel, ssi1_div_pre,
	/* 24 */ ssi1_div_post, ssi2_div_pre, ssi2_div_post, usb_sel, usb_div,
	/* 29 */ nfc_div, asrc_gate, pata_gate, audmux_gate, can1_gate,
	/* 34 */ can2_gate, cspi1_gate, cspi2_gate, ect_gate, edio_gate,
	/* 39 */ emi_gate, epit1_gate, epit2_gate, esai_gate, esdhc1_gate,
	/* 44 */ esdhc2_gate, esdhc3_gate, fec_gate, gpio1_gate, gpio2_gate,
	/* 49 */ gpio3_gate, gpt_gate, i2c1_gate, i2c2_gate, i2c3_gate,
	/* 54 */ iomuxc_gate, ipu_gate, kpp_gate, mlb_gate, mshc_gate,
	/* 59 */ owire_gate, pwm_gate, rngc_gate, rtc_gate, rtic_gate, scc_gate,
	/* 65 */ sdma_gate, spba_gate, spdif_gate, ssi1_gate, ssi2_gate,
	/* 70 */ uart1_gate, uart2_gate, uart3_gate, usbotg_gate, wdog_gate,
	/* 75 */ max_gate, admux_gate, csi_gate, csi_div, csi_sel, iim_gate,
	/* 81 */ gpu2d_gate, ckil, clk_max
};

static struct clk *clk[clk_max];

static void __init _mx35_clocks_init(void)
{
	void __iomem *base;
	u32 pdr0, consumer_sel, hsp_sel;
	struct arm_ahb_div *aad;
	unsigned char *hsp_div;

	base = ioremap(MX35_CCM_BASE_ADDR, SZ_4K);
	BUG_ON(!base);

	pdr0 = __raw_readl(base + MXC_CCM_PDR0);
	consumer_sel = (pdr0 >> 16) & 0xf;
	aad = &clk_consumer[consumer_sel];
	if (!aad->arm) {
		pr_err("i.MX35 clk: illegal consumer mux selection 0x%x\n", consumer_sel);
		/*
		 * We are basically stuck. Continue with a default entry and hope we
		 * get far enough to actually show the above message
		 */
		aad = &clk_consumer[0];
	}

	clk[ckih] = imx_clk_fixed("ckih", 24000000);
	clk[ckil] = imx_clk_fixed("ckil", 32768);
	clk[mpll] = imx_clk_pllv1(IMX_PLLV1_IMX35, "mpll", "ckih", base + MX35_CCM_MPCTL);
	clk[ppll] = imx_clk_pllv1(IMX_PLLV1_IMX35, "ppll", "ckih", base + MX35_CCM_PPCTL);

	clk[mpll] = imx_clk_fixed_factor("mpll_075", "mpll", 3, 4);

	if (aad->sel)
		clk[arm] = imx_clk_fixed_factor("arm", "mpll_075", 1, aad->arm);
	else
		clk[arm] = imx_clk_fixed_factor("arm", "mpll", 1, aad->arm);

	if (clk_get_rate(clk[arm]) > 400000000)
		hsp_div = hsp_div_532;
	else
		hsp_div = hsp_div_400;

	hsp_sel = (pdr0 >> 20) & 0x3;
	if (!hsp_div[hsp_sel]) {
		pr_err("i.MX35 clk: illegal hsp clk selection 0x%x\n", hsp_sel);
		hsp_sel = 0;
	}

	clk[hsp] = imx_clk_fixed_factor("hsp", "arm", 1, hsp_div[hsp_sel]);

	clk[ahb] = imx_clk_fixed_factor("ahb", "arm", 1, aad->ahb);
	clk[ipg] = imx_clk_fixed_factor("ipg", "ahb", 1, 2);

	clk[arm_per_div] = imx_clk_divider("arm_per_div", "arm", base + MX35_CCM_PDR4, 16, 6);
	clk[ahb_per_div] = imx_clk_divider("ahb_per_div", "ahb", base + MXC_CCM_PDR0, 12, 3);
	clk[ipg_per] = imx_clk_mux("ipg_per", base + MXC_CCM_PDR0, 26, 1, ipg_per_sel, ARRAY_SIZE(ipg_per_sel));

	clk[uart_sel] = imx_clk_mux("uart_sel", base + MX35_CCM_PDR3, 14, 1, std_sel, ARRAY_SIZE(std_sel));
	clk[uart_div] = imx_clk_divider("uart_div", "uart_sel", base + MX35_CCM_PDR4, 10, 6);

	clk[esdhc_sel] = imx_clk_mux("esdhc_sel", base + MX35_CCM_PDR4, 9, 1, std_sel, ARRAY_SIZE(std_sel));
	clk[esdhc1_div] = imx_clk_divider("esdhc1_div", "esdhc_sel", base + MX35_CCM_PDR3, 0, 6);
	clk[esdhc2_div] = imx_clk_divider("esdhc2_div", "esdhc_sel", base + MX35_CCM_PDR3, 8, 6);
	clk[esdhc3_div] = imx_clk_divider("esdhc3_div", "esdhc_sel", base + MX35_CCM_PDR3, 16, 6);

	clk[spdif_sel] = imx_clk_mux("spdif_sel", base + MX35_CCM_PDR3, 22, 1, std_sel, ARRAY_SIZE(std_sel));
	clk[spdif_div_pre] = imx_clk_divider("spdif_div_pre", "spdif_sel", base + MX35_CCM_PDR3, 29, 3); /* divide by 1 not allowed */ 
	clk[spdif_div_post] = imx_clk_divider("spdif_div_post", "spdif_div_pre", base + MX35_CCM_PDR3, 23, 6);

	clk[ssi_sel] = imx_clk_mux("ssi_sel", base + MX35_CCM_PDR2, 6, 1, std_sel, ARRAY_SIZE(std_sel));
	clk[ssi1_div_pre] = imx_clk_divider("ssi1_div_pre", "ssi_sel", base + MX35_CCM_PDR2, 24, 3);
	clk[ssi1_div_post] = imx_clk_divider("ssi1_div_post", "ssi1_div_pre", base + MX35_CCM_PDR2, 0, 6);
	clk[ssi2_div_pre] = imx_clk_divider("ssi2_div_pre", "ssi_sel", base + MX35_CCM_PDR2, 27, 3);
	clk[ssi2_div_post] = imx_clk_divider("ssi2_div_post", "ssi2_div_pre", base + MX35_CCM_PDR2, 8, 6);

	clk[usb_sel] = imx_clk_mux("usb_sel", base + MX35_CCM_PDR4, 9, 1, std_sel, ARRAY_SIZE(std_sel));
	clk[usb_div] = imx_clk_divider("usb_div", "usb_sel", base + MX35_CCM_PDR4, 22, 6);

	clk[nfc_div] = imx_clk_divider("nfc_div", "ahb", base + MX35_CCM_PDR4, 28, 4);

	clk[csi_sel] = imx_clk_mux("csi_sel", base + MX35_CCM_PDR2, 7, 1, std_sel, ARRAY_SIZE(std_sel));
	clk[csi_div] = imx_clk_divider("csi_div", "csi_sel", base + MX35_CCM_PDR2, 16, 6);

	clk[asrc_gate] = imx_clk_gate2("asrc_gate", "ipg", base + MX35_CCM_CGR0,  0);
	clk[pata_gate] = imx_clk_gate2("pata_gate", "ipg", base + MX35_CCM_CGR0,  2);
	clk[audmux_gate] = imx_clk_gate2("audmux_gate", "ipg", base + MX35_CCM_CGR0,  4);
	clk[can1_gate] = imx_clk_gate2("can1_gate", "ipg", base + MX35_CCM_CGR0,  6);
	clk[can2_gate] = imx_clk_gate2("can2_gate", "ipg", base + MX35_CCM_CGR0,  8);
	clk[cspi1_gate] = imx_clk_gate2("cspi1_gate", "ipg", base + MX35_CCM_CGR0, 10);
	clk[cspi2_gate] = imx_clk_gate2("cspi2_gate", "ipg", base + MX35_CCM_CGR0, 12);
	clk[ect_gate] = imx_clk_gate2("ect_gate", "ipg", base + MX35_CCM_CGR0, 14);
	clk[edio_gate] = imx_clk_gate2("edio_gate",   "ipg", base + MX35_CCM_CGR0, 16);
	clk[emi_gate] = imx_clk_gate2("emi_gate", "ipg", base + MX35_CCM_CGR0, 18);
	clk[epit1_gate] = imx_clk_gate2("epit1_gate", "ipg", base + MX35_CCM_CGR0, 20);
	clk[epit2_gate] = imx_clk_gate2("epit2_gate", "ipg", base + MX35_CCM_CGR0, 22);
	clk[esai_gate] = imx_clk_gate2("esai_gate",   "ipg", base + MX35_CCM_CGR0, 24);
	clk[esdhc1_gate] = imx_clk_gate2("esdhc1_gate", "esdhc1_div", base + MX35_CCM_CGR0, 26);
	clk[esdhc2_gate] = imx_clk_gate2("esdhc2_gate", "esdhc2_div", base + MX35_CCM_CGR0, 28);
	clk[esdhc3_gate] = imx_clk_gate2("esdhc3_gate", "esdhc3_div", base + MX35_CCM_CGR0, 30);

	clk[fec_gate] = imx_clk_gate2("fec_gate", "ipg", base + MX35_CCM_CGR1,  0);
	clk[gpio1_gate] = imx_clk_gate2("gpio1_gate", "ipg", base + MX35_CCM_CGR1,  2);
	clk[gpio2_gate] = imx_clk_gate2("gpio2_gate", "ipg", base + MX35_CCM_CGR1,  4);
	clk[gpio3_gate] = imx_clk_gate2("gpio3_gate", "ipg", base + MX35_CCM_CGR1,  6);
	clk[gpt_gate] = imx_clk_gate2("gpt_gate", "ipg", base + MX35_CCM_CGR1,  8);
	clk[i2c1_gate] = imx_clk_gate2("i2c1_gate", "ipg_per", base + MX35_CCM_CGR1, 10);
	clk[i2c2_gate] = imx_clk_gate2("i2c2_gate", "ipg_per", base + MX35_CCM_CGR1, 12);
	clk[i2c3_gate] = imx_clk_gate2("i2c3_gate", "ipg_per", base + MX35_CCM_CGR1, 14);
	clk[iomuxc_gate] = imx_clk_gate2("iomuxc_gate", "ipg", base + MX35_CCM_CGR1, 16);
	clk[ipu_gate] = imx_clk_gate2("ipu_gate", "hsp", base + MX35_CCM_CGR1, 18);
	clk[kpp_gate] = imx_clk_gate2("kpp_gate", "ipg", base + MX35_CCM_CGR1, 20);
	clk[mlb_gate] = imx_clk_gate2("mlb_gate", "ahb", base + MX35_CCM_CGR1, 22);
	clk[mshc_gate] = imx_clk_gate2("mshc_gate", "dummy", base + MX35_CCM_CGR1, 24);
	clk[owire_gate] = imx_clk_gate2("owire_gate", "ipg_per", base + MX35_CCM_CGR1, 26);
	clk[pwm_gate] = imx_clk_gate2("pwm_gate", "ipg_per", base + MX35_CCM_CGR1, 28);
	clk[rngc_gate] = imx_clk_gate2("rngc_gate", "ipg", base + MX35_CCM_CGR1, 30);

	clk[rtc_gate] = imx_clk_gate2("rtc_gate", "ipg", base + MX35_CCM_CGR2,  0);
	clk[rtic_gate] = imx_clk_gate2("rtic_gate", "ahb", base + MX35_CCM_CGR2,  2);
	clk[scc_gate] = imx_clk_gate2("scc_gate", "ipg", base + MX35_CCM_CGR2,  4);
	clk[sdma_gate] = imx_clk_gate2("sdma_gate", "ahb", base + MX35_CCM_CGR2,  6);
	clk[spba_gate] = imx_clk_gate2("spba_gate", "ipg", base + MX35_CCM_CGR2,  8);
	clk[spdif_gate] = imx_clk_gate2("spdif_gate", "spdif_div_post", base + MX35_CCM_CGR2, 10);
	clk[ssi1_gate] = imx_clk_gate2("ssi1_gate", "ssi1_div_post", base + MX35_CCM_CGR2, 12);
	clk[ssi2_gate] = imx_clk_gate2("ssi2_gate", "ssi2_div_post", base + MX35_CCM_CGR2, 14);
	clk[uart1_gate] = imx_clk_gate2("uart1_gate", "uart_div", base + MX35_CCM_CGR2, 16);
	clk[uart2_gate] = imx_clk_gate2("uart2_gate", "uart_div", base + MX35_CCM_CGR2, 18);
	clk[uart3_gate] = imx_clk_gate2("uart3_gate", "uart_div", base + MX35_CCM_CGR2, 20);
	clk[usbotg_gate] = imx_clk_gate2("usbotg_gate", "ahb", base + MX35_CCM_CGR2, 22);
	clk[wdog_gate] = imx_clk_gate2("wdog_gate", "ipg", base + MX35_CCM_CGR2, 24);
	clk[max_gate] = imx_clk_gate2("max_gate", "dummy", base + MX35_CCM_CGR2, 26);
	clk[admux_gate] = imx_clk_gate2("admux_gate", "ipg", base + MX35_CCM_CGR2, 30);

	clk[csi_gate] = imx_clk_gate2("csi_gate", "csi_div", base + MX35_CCM_CGR3,  0);
	clk[iim_gate] = imx_clk_gate2("iim_gate", "ipg", base + MX35_CCM_CGR3,  2);
	clk[gpu2d_gate] = imx_clk_gate2("gpu2d_gate", "ahb", base + MX35_CCM_CGR3,  4);

	imx_check_clocks(clk, ARRAY_SIZE(clk));

	clk_prepare_enable(clk[spba_gate]);
	clk_prepare_enable(clk[gpio1_gate]);
	clk_prepare_enable(clk[gpio2_gate]);
	clk_prepare_enable(clk[gpio3_gate]);
	clk_prepare_enable(clk[iim_gate]);
	clk_prepare_enable(clk[emi_gate]);
	clk_prepare_enable(clk[max_gate]);
	clk_prepare_enable(clk[iomuxc_gate]);

	/*
	 * SCC is needed to boot via mmc after a watchdog reset. The clock code
	 * before conversion to common clk also enabled UART1 (which isn't
	 * handled here and not needed for mmc) and IIM (which is enabled
	 * unconditionally above).
	 */
	clk_prepare_enable(clk[scc_gate]);

	imx_register_uart_clocks();

	imx_print_silicon_rev("i.MX35", mx35_revision());
}

static void __init mx35_clocks_init_dt(struct device_node *ccm_node)
{
	_mx35_clocks_init();

	clk_data.clks = clk;
	clk_data.clk_num = ARRAY_SIZE(clk);
	of_clk_add_provider(ccm_node, of_clk_src_onecell_get, &clk_data);
}
CLK_OF_DECLARE(imx35, "fsl,imx35-ccm", mx35_clocks_init_dt);
