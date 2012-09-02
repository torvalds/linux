/*
 * Copyright (C) 2012 Sascha Hauer, Pengutronix <s.hauer@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#include <linux/mm.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/clkdev.h>
#include <linux/of.h>
#include <linux/err.h>

#include <mach/hardware.h>
#include <mach/common.h>

#include "crmregs-imx3.h"
#include "clk.h"

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

static const char *std_sel[] = {"ppll", "arm"};
static const char *ipg_per_sel[] = {"ahb_per_div", "arm_per_div"};

enum mx35_clks {
	ckih, mpll, ppll, mpll_075, arm, hsp, hsp_div, hsp_sel, ahb, ipg,
	arm_per_div, ahb_per_div, ipg_per, uart_sel, uart_div, esdhc_sel,
	esdhc1_div, esdhc2_div, esdhc3_div, spdif_sel, spdif_div_pre,
	spdif_div_post, ssi_sel, ssi1_div_pre, ssi1_div_post, ssi2_div_pre,
	ssi2_div_post, usb_sel, usb_div, nfc_div, asrc_gate, pata_gate,
	audmux_gate, can1_gate, can2_gate, cspi1_gate, cspi2_gate, ect_gate,
	edio_gate, emi_gate, epit1_gate, epit2_gate, esai_gate, esdhc1_gate,
	esdhc2_gate, esdhc3_gate, fec_gate, gpio1_gate, gpio2_gate, gpio3_gate,
	gpt_gate, i2c1_gate, i2c2_gate, i2c3_gate, iomuxc_gate, ipu_gate,
	kpp_gate, mlb_gate, mshc_gate, owire_gate, pwm_gate, rngc_gate,
	rtc_gate, rtic_gate, scc_gate, sdma_gate, spba_gate, spdif_gate,
	ssi1_gate, ssi2_gate, uart1_gate, uart2_gate, uart3_gate, usbotg_gate,
	wdog_gate, max_gate, admux_gate, csi_gate, csi_div, csi_sel, iim_gate,
	gpu2d_gate, clk_max
};

static struct clk *clk[clk_max];

int __init mx35_clocks_init()
{
	void __iomem *base = MX35_IO_ADDRESS(MX35_CCM_BASE_ADDR);
	u32 pdr0, consumer_sel, hsp_sel;
	struct arm_ahb_div *aad;
	unsigned char *hsp_div;
	int i;

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
	clk[mpll] = imx_clk_pllv1("mpll", "ckih", base + MX35_CCM_MPCTL);
	clk[ppll] = imx_clk_pllv1("ppll", "ckih", base + MX35_CCM_PPCTL);

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

	for (i = 0; i < ARRAY_SIZE(clk); i++)
		if (IS_ERR(clk[i]))
			pr_err("i.MX35 clk %d: register failed with %ld\n",
				i, PTR_ERR(clk[i]));

	clk_register_clkdev(clk[pata_gate], NULL, "pata_imx");
	clk_register_clkdev(clk[can1_gate], NULL, "flexcan.0");
	clk_register_clkdev(clk[can2_gate], NULL, "flexcan.1");
	clk_register_clkdev(clk[cspi1_gate], "per", "imx35-cspi.0");
	clk_register_clkdev(clk[cspi1_gate], "ipg", "imx35-cspi.0");
	clk_register_clkdev(clk[cspi2_gate], "per", "imx35-cspi.1");
	clk_register_clkdev(clk[cspi2_gate], "ipg", "imx35-cspi.1");
	clk_register_clkdev(clk[epit1_gate], NULL, "imx-epit.0");
	clk_register_clkdev(clk[epit2_gate], NULL, "imx-epit.1");
	clk_register_clkdev(clk[esdhc1_gate], "per", "sdhci-esdhc-imx35.0");
	clk_register_clkdev(clk[ipg], "ipg", "sdhci-esdhc-imx35.0");
	clk_register_clkdev(clk[ahb], "ahb", "sdhci-esdhc-imx35.0");
	clk_register_clkdev(clk[esdhc2_gate], "per", "sdhci-esdhc-imx35.1");
	clk_register_clkdev(clk[ipg], "ipg", "sdhci-esdhc-imx35.1");
	clk_register_clkdev(clk[ahb], "ahb", "sdhci-esdhc-imx35.1");
	clk_register_clkdev(clk[esdhc3_gate], "per", "sdhci-esdhc-imx35.2");
	clk_register_clkdev(clk[ipg], "ipg", "sdhci-esdhc-imx35.2");
	clk_register_clkdev(clk[ahb], "ahb", "sdhci-esdhc-imx35.2");
	/* i.mx35 has the i.mx27 type fec */
	clk_register_clkdev(clk[fec_gate], NULL, "imx27-fec.0");
	clk_register_clkdev(clk[gpt_gate], "per", "imx-gpt.0");
	clk_register_clkdev(clk[ipg], "ipg", "imx-gpt.0");
	clk_register_clkdev(clk[i2c1_gate], NULL, "imx-i2c.0");
	clk_register_clkdev(clk[i2c2_gate], NULL, "imx-i2c.1");
	clk_register_clkdev(clk[i2c3_gate], NULL, "imx-i2c.2");
	clk_register_clkdev(clk[ipu_gate], NULL, "ipu-core");
	clk_register_clkdev(clk[ipu_gate], NULL, "mx3_sdc_fb");
	clk_register_clkdev(clk[kpp_gate], NULL, "imx-keypad");
	clk_register_clkdev(clk[owire_gate], NULL, "mxc_w1");
	clk_register_clkdev(clk[sdma_gate], NULL, "imx35-sdma");
	clk_register_clkdev(clk[ipg], "ipg", "imx-ssi.0");
	clk_register_clkdev(clk[ssi1_div_post], "per", "imx-ssi.0");
	clk_register_clkdev(clk[ipg], "ipg", "imx-ssi.1");
	clk_register_clkdev(clk[ssi2_div_post], "per", "imx-ssi.1");
	/* i.mx35 has the i.mx21 type uart */
	clk_register_clkdev(clk[uart1_gate], "per", "imx21-uart.0");
	clk_register_clkdev(clk[ipg], "ipg", "imx21-uart.0");
	clk_register_clkdev(clk[uart2_gate], "per", "imx21-uart.1");
	clk_register_clkdev(clk[ipg], "ipg", "imx21-uart.1");
	clk_register_clkdev(clk[uart3_gate], "per", "imx21-uart.2");
	clk_register_clkdev(clk[ipg], "ipg", "imx21-uart.2");
	clk_register_clkdev(clk[usb_div], "per", "mxc-ehci.0");
	clk_register_clkdev(clk[ipg], "ipg", "mxc-ehci.0");
	clk_register_clkdev(clk[usbotg_gate], "ahb", "mxc-ehci.0");
	clk_register_clkdev(clk[usb_div], "per", "mxc-ehci.1");
	clk_register_clkdev(clk[ipg], "ipg", "mxc-ehci.1");
	clk_register_clkdev(clk[usbotg_gate], "ahb", "mxc-ehci.1");
	clk_register_clkdev(clk[usb_div], "per", "mxc-ehci.2");
	clk_register_clkdev(clk[ipg], "ipg", "mxc-ehci.2");
	clk_register_clkdev(clk[usbotg_gate], "ahb", "mxc-ehci.2");
	clk_register_clkdev(clk[usb_div], "per", "fsl-usb2-udc");
	clk_register_clkdev(clk[ipg], "ipg", "fsl-usb2-udc");
	clk_register_clkdev(clk[usbotg_gate], "ahb", "fsl-usb2-udc");
	clk_register_clkdev(clk[wdog_gate], NULL, "imx2-wdt.0");
	clk_register_clkdev(clk[nfc_div], NULL, "mxc_nand.0");
	clk_register_clkdev(clk[csi_gate], NULL, "mx3-camera.0");

	clk_prepare_enable(clk[spba_gate]);
	clk_prepare_enable(clk[gpio1_gate]);
	clk_prepare_enable(clk[gpio2_gate]);
	clk_prepare_enable(clk[gpio3_gate]);
	clk_prepare_enable(clk[iim_gate]);
	clk_prepare_enable(clk[emi_gate]);

	/*
	 * SCC is needed to boot via mmc after a watchdog reset. The clock code
	 * before conversion to common clk also enabled UART1 (which isn't
	 * handled here and not needed for mmc) and IIM (which is enabled
	 * unconditionally above).
	 */
	clk_prepare_enable(clk[scc_gate]);

	imx_print_silicon_rev("i.MX35", mx35_revision());

#ifdef CONFIG_MXC_USE_EPIT
	epit_timer_init(MX35_IO_ADDRESS(MX35_EPIT1_BASE_ADDR), MX35_INT_EPIT1);
#else
	mxc_timer_init(MX35_IO_ADDRESS(MX35_GPT1_BASE_ADDR), MX35_INT_GPT);
#endif

	return 0;
}
