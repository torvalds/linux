/*
 * Copyright (C) 2012 Sascha Hauer <kernel@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation.
 */

#include <linux/module.h>
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/io.h>
#include <linux/err.h>
#include <linux/of.h>

#include <mach/hardware.h>
#include <mach/mx31.h>
#include <mach/common.h>

#include "clk.h"
#include "crmregs-imx3.h"

static const char *mcu_main_sel[] = { "spll", "mpll", };
static const char *per_sel[] = { "per_div", "ipg", };
static const char *csi_sel[] = { "upll", "spll", };
static const char *fir_sel[] = { "mcu_main", "upll", "spll" };

enum mx31_clks {
	ckih, ckil, mpll, spll, upll, mcu_main, hsp, ahb, nfc, ipg, per_div,
	per, csi, fir, csi_div, usb_div_pre, usb_div_post, fir_div_pre,
	fir_div_post, sdhc1_gate, sdhc2_gate, gpt_gate, epit1_gate, epit2_gate,
	iim_gate, ata_gate, sdma_gate, cspi3_gate, rng_gate, uart1_gate,
	uart2_gate, ssi1_gate, i2c1_gate, i2c2_gate, i2c3_gate, hantro_gate,
	mstick1_gate, mstick2_gate, csi_gate, rtc_gate, wdog_gate, pwm_gate,
	sim_gate, ect_gate, usb_gate, kpp_gate, ipu_gate, uart3_gate,
	uart4_gate, uart5_gate, owire_gate, ssi2_gate, cspi1_gate, cspi2_gate,
	gacc_gate, emi_gate, rtic_gate, firi_gate, clk_max
};

static struct clk *clk[clk_max];

int __init mx31_clocks_init(unsigned long fref)
{
	void __iomem *base = MX31_IO_ADDRESS(MX31_CCM_BASE_ADDR);
	int i;

	clk[ckih] = imx_clk_fixed("ckih", fref);
	clk[ckil] = imx_clk_fixed("ckil", 32768);
	clk[mpll] = imx_clk_pllv1("mpll", "ckih", base + MXC_CCM_MPCTL);
	clk[spll] = imx_clk_pllv1("spll", "ckih", base + MXC_CCM_SRPCTL);
	clk[upll] = imx_clk_pllv1("upll", "ckih", base + MXC_CCM_UPCTL);
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

	for (i = 0; i < ARRAY_SIZE(clk); i++)
		if (IS_ERR(clk[i]))
			pr_err("imx31 clk %d: register failed with %ld\n",
				i, PTR_ERR(clk[i]));

	clk_register_clkdev(clk[gpt_gate], "per", "imx-gpt.0");
	clk_register_clkdev(clk[ipg], "ipg", "imx-gpt.0");
	clk_register_clkdev(clk[cspi1_gate], NULL, "imx31-cspi.0");
	clk_register_clkdev(clk[cspi2_gate], NULL, "imx31-cspi.1");
	clk_register_clkdev(clk[cspi3_gate], NULL, "imx31-cspi.2");
	clk_register_clkdev(clk[pwm_gate], "pwm", NULL);
	clk_register_clkdev(clk[wdog_gate], NULL, "imx2-wdt.0");
	clk_register_clkdev(clk[rtc_gate], NULL, "mxc_rtc");
	clk_register_clkdev(clk[epit1_gate], "epit", NULL);
	clk_register_clkdev(clk[epit2_gate], "epit", NULL);
	clk_register_clkdev(clk[nfc], NULL, "mxc_nand.0");
	clk_register_clkdev(clk[ipu_gate], NULL, "ipu-core");
	clk_register_clkdev(clk[ipu_gate], NULL, "mx3_sdc_fb");
	clk_register_clkdev(clk[kpp_gate], NULL, "imx-keypad");
	clk_register_clkdev(clk[usb_div_post], "per", "mxc-ehci.0");
	clk_register_clkdev(clk[usb_gate], "ahb", "mxc-ehci.0");
	clk_register_clkdev(clk[ipg], "ipg", "mxc-ehci.0");
	clk_register_clkdev(clk[usb_div_post], "per", "mxc-ehci.1");
	clk_register_clkdev(clk[usb_gate], "ahb", "mxc-ehci.1");
	clk_register_clkdev(clk[ipg], "ipg", "mxc-ehci.1");
	clk_register_clkdev(clk[usb_div_post], "per", "mxc-ehci.2");
	clk_register_clkdev(clk[usb_gate], "ahb", "mxc-ehci.2");
	clk_register_clkdev(clk[ipg], "ipg", "mxc-ehci.2");
	clk_register_clkdev(clk[usb_div_post], "per", "fsl-usb2-udc");
	clk_register_clkdev(clk[usb_gate], "ahb", "fsl-usb2-udc");
	clk_register_clkdev(clk[ipg], "ipg", "fsl-usb2-udc");
	clk_register_clkdev(clk[csi_gate], NULL, "mx3-camera.0");
	/* i.mx31 has the i.mx21 type uart */
	clk_register_clkdev(clk[uart1_gate], "per", "imx21-uart.0");
	clk_register_clkdev(clk[ipg], "ipg", "imx21-uart.0");
	clk_register_clkdev(clk[uart2_gate], "per", "imx21-uart.1");
	clk_register_clkdev(clk[ipg], "ipg", "imx21-uart.1");
	clk_register_clkdev(clk[uart3_gate], "per", "imx21-uart.2");
	clk_register_clkdev(clk[ipg], "ipg", "imx21-uart.2");
	clk_register_clkdev(clk[uart4_gate], "per", "imx21-uart.3");
	clk_register_clkdev(clk[ipg], "ipg", "imx21-uart.3");
	clk_register_clkdev(clk[uart5_gate], "per", "imx21-uart.4");
	clk_register_clkdev(clk[ipg], "ipg", "imx21-uart.4");
	clk_register_clkdev(clk[i2c1_gate], NULL, "imx-i2c.0");
	clk_register_clkdev(clk[i2c2_gate], NULL, "imx-i2c.1");
	clk_register_clkdev(clk[i2c3_gate], NULL, "imx-i2c.2");
	clk_register_clkdev(clk[owire_gate], NULL, "mxc_w1.0");
	clk_register_clkdev(clk[sdhc1_gate], NULL, "mxc-mmc.0");
	clk_register_clkdev(clk[sdhc2_gate], NULL, "mxc-mmc.1");
	clk_register_clkdev(clk[ssi1_gate], NULL, "imx-ssi.0");
	clk_register_clkdev(clk[ssi2_gate], NULL, "imx-ssi.1");
	clk_register_clkdev(clk[firi_gate], "firi", NULL);
	clk_register_clkdev(clk[ata_gate], NULL, "pata_imx");
	clk_register_clkdev(clk[rtic_gate], "rtic", NULL);
	clk_register_clkdev(clk[rng_gate], NULL, "mxc_rnga");
	clk_register_clkdev(clk[sdma_gate], NULL, "imx31-sdma");
	clk_register_clkdev(clk[iim_gate], "iim", NULL);

	clk_set_parent(clk[csi], clk[upll]);
	clk_prepare_enable(clk[emi_gate]);
	clk_prepare_enable(clk[iim_gate]);
	mx31_revision();
	clk_disable_unprepare(clk[iim_gate]);

	mxc_timer_init(MX31_IO_ADDRESS(MX31_GPT1_BASE_ADDR), MX31_INT_GPT);

	return 0;
}

#ifdef CONFIG_OF
int __init mx31_clocks_init_dt(void)
{
	struct device_node *np;
	u32 fref = 26000000; /* default */

	for_each_compatible_node(np, NULL, "fixed-clock") {
		if (!of_device_is_compatible(np, "fsl,imx-osc26m"))
			continue;

		if (!of_property_read_u32(np, "clock-frequency", &fref))
			break;
	}

	return mx31_clocks_init(fref);
}
#endif
