/*
 * Copyright (C) 2009 by Sascha Hauer, Pengutronix
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/clkdev.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>

#include "clk.h"
#include "common.h"
#include "hardware.h"
#include "mx25.h"

#define CRM_BASE	MX25_IO_ADDRESS(MX25_CRM_BASE_ADDR)

#define CCM_MPCTL	0x00
#define CCM_UPCTL	0x04
#define CCM_CCTL	0x08
#define CCM_CGCR0	0x0C
#define CCM_CGCR1	0x10
#define CCM_CGCR2	0x14
#define CCM_PCDR0	0x18
#define CCM_PCDR1	0x1C
#define CCM_PCDR2	0x20
#define CCM_PCDR3	0x24
#define CCM_RCSR	0x28
#define CCM_CRDR	0x2C
#define CCM_DCVR0	0x30
#define CCM_DCVR1	0x34
#define CCM_DCVR2	0x38
#define CCM_DCVR3	0x3c
#define CCM_LTR0	0x40
#define CCM_LTR1	0x44
#define CCM_LTR2	0x48
#define CCM_LTR3	0x4c
#define CCM_MCR		0x64

#define ccm(x)	(CRM_BASE + (x))

static struct clk_onecell_data clk_data;

static const char *cpu_sel_clks[] = { "mpll", "mpll_cpu_3_4", };
static const char *per_sel_clks[] = { "ahb", "upll", };

enum mx25_clks {
	dummy, osc, mpll, upll, mpll_cpu_3_4, cpu_sel, cpu, ahb, usb_div, ipg,
	per0_sel, per1_sel, per2_sel, per3_sel, per4_sel, per5_sel, per6_sel,
	per7_sel, per8_sel, per9_sel, per10_sel, per11_sel, per12_sel,
	per13_sel, per14_sel, per15_sel, per0, per1, per2, per3, per4, per5,
	per6, per7, per8, per9, per10, per11, per12, per13, per14, per15,
	csi_ipg_per, epit_ipg_per, esai_ipg_per, esdhc1_ipg_per, esdhc2_ipg_per,
	gpt_ipg_per, i2c_ipg_per, lcdc_ipg_per, nfc_ipg_per, owire_ipg_per,
	pwm_ipg_per, sim1_ipg_per, sim2_ipg_per, ssi1_ipg_per, ssi2_ipg_per,
	uart_ipg_per, ata_ahb, reserved1, csi_ahb, emi_ahb, esai_ahb, esdhc1_ahb,
	esdhc2_ahb, fec_ahb, lcdc_ahb, rtic_ahb, sdma_ahb, slcdc_ahb, usbotg_ahb,
	reserved2, reserved3, reserved4, reserved5, can1_ipg, can2_ipg,	csi_ipg,
	cspi1_ipg, cspi2_ipg, cspi3_ipg, dryice_ipg, ect_ipg, epit1_ipg, epit2_ipg,
	reserved6, esdhc1_ipg, esdhc2_ipg, fec_ipg, reserved7, reserved8, reserved9,
	gpt1_ipg, gpt2_ipg, gpt3_ipg, gpt4_ipg, reserved10, reserved11, reserved12,
	iim_ipg, reserved13, reserved14, kpp_ipg, lcdc_ipg, reserved15, pwm1_ipg,
	pwm2_ipg, pwm3_ipg, pwm4_ipg, rngb_ipg, reserved16, scc_ipg, sdma_ipg,
	sim1_ipg, sim2_ipg, slcdc_ipg, spba_ipg, ssi1_ipg, ssi2_ipg, tsc_ipg,
	uart1_ipg, uart2_ipg, uart3_ipg, uart4_ipg, uart5_ipg, reserved17,
	wdt_ipg, clk_max
};

static struct clk *clk[clk_max];

static int __init __mx25_clocks_init(unsigned long osc_rate)
{
	int i;

	clk[dummy] = imx_clk_fixed("dummy", 0);
	clk[osc] = imx_clk_fixed("osc", osc_rate);
	clk[mpll] = imx_clk_pllv1("mpll", "osc", ccm(CCM_MPCTL));
	clk[upll] = imx_clk_pllv1("upll", "osc", ccm(CCM_UPCTL));
	clk[mpll_cpu_3_4] = imx_clk_fixed_factor("mpll_cpu_3_4", "mpll", 3, 4);
	clk[cpu_sel] = imx_clk_mux("cpu_sel", ccm(CCM_CCTL), 14, 1, cpu_sel_clks, ARRAY_SIZE(cpu_sel_clks));
	clk[cpu] = imx_clk_divider("cpu", "cpu_sel", ccm(CCM_CCTL), 30, 2);
	clk[ahb] = imx_clk_divider("ahb", "cpu", ccm(CCM_CCTL), 28, 2);
	clk[usb_div] = imx_clk_divider("usb_div", "upll", ccm(CCM_CCTL), 16, 6); 
	clk[ipg] = imx_clk_fixed_factor("ipg", "ahb", 1, 2);
	clk[per0_sel] = imx_clk_mux("per0_sel", ccm(CCM_MCR), 0, 1, per_sel_clks, ARRAY_SIZE(per_sel_clks));
	clk[per1_sel] = imx_clk_mux("per1_sel", ccm(CCM_MCR), 1, 1, per_sel_clks, ARRAY_SIZE(per_sel_clks));
	clk[per2_sel] = imx_clk_mux("per2_sel", ccm(CCM_MCR), 2, 1, per_sel_clks, ARRAY_SIZE(per_sel_clks));
	clk[per3_sel] = imx_clk_mux("per3_sel", ccm(CCM_MCR), 3, 1, per_sel_clks, ARRAY_SIZE(per_sel_clks));
	clk[per4_sel] = imx_clk_mux("per4_sel", ccm(CCM_MCR), 4, 1, per_sel_clks, ARRAY_SIZE(per_sel_clks));
	clk[per5_sel] = imx_clk_mux("per5_sel", ccm(CCM_MCR), 5, 1, per_sel_clks, ARRAY_SIZE(per_sel_clks));
	clk[per6_sel] = imx_clk_mux("per6_sel", ccm(CCM_MCR), 6, 1, per_sel_clks, ARRAY_SIZE(per_sel_clks));
	clk[per7_sel] = imx_clk_mux("per7_sel", ccm(CCM_MCR), 7, 1, per_sel_clks, ARRAY_SIZE(per_sel_clks));
	clk[per8_sel] = imx_clk_mux("per8_sel", ccm(CCM_MCR), 8, 1, per_sel_clks, ARRAY_SIZE(per_sel_clks));
	clk[per9_sel] = imx_clk_mux("per9_sel", ccm(CCM_MCR), 9, 1, per_sel_clks, ARRAY_SIZE(per_sel_clks));
	clk[per10_sel] = imx_clk_mux("per10_sel", ccm(CCM_MCR), 10, 1, per_sel_clks, ARRAY_SIZE(per_sel_clks));
	clk[per11_sel] = imx_clk_mux("per11_sel", ccm(CCM_MCR), 11, 1, per_sel_clks, ARRAY_SIZE(per_sel_clks));
	clk[per12_sel] = imx_clk_mux("per12_sel", ccm(CCM_MCR), 12, 1, per_sel_clks, ARRAY_SIZE(per_sel_clks));
	clk[per13_sel] = imx_clk_mux("per13_sel", ccm(CCM_MCR), 13, 1, per_sel_clks, ARRAY_SIZE(per_sel_clks));
	clk[per14_sel] = imx_clk_mux("per14_sel", ccm(CCM_MCR), 14, 1, per_sel_clks, ARRAY_SIZE(per_sel_clks));
	clk[per15_sel] = imx_clk_mux("per15_sel", ccm(CCM_MCR), 15, 1, per_sel_clks, ARRAY_SIZE(per_sel_clks));
	clk[per0] = imx_clk_divider("per0", "per0_sel", ccm(CCM_PCDR0), 0, 6);
	clk[per1] = imx_clk_divider("per1", "per1_sel", ccm(CCM_PCDR0), 8, 6);
	clk[per2] = imx_clk_divider("per2", "per2_sel", ccm(CCM_PCDR0), 16, 6);
	clk[per3] = imx_clk_divider("per3", "per3_sel", ccm(CCM_PCDR0), 24, 6);
	clk[per4] = imx_clk_divider("per4", "per4_sel", ccm(CCM_PCDR1), 0, 6);
	clk[per5] = imx_clk_divider("per5", "per5_sel", ccm(CCM_PCDR1), 8, 6);
	clk[per6] = imx_clk_divider("per6", "per6_sel", ccm(CCM_PCDR1), 16, 6);
	clk[per7] = imx_clk_divider("per7", "per7_sel", ccm(CCM_PCDR1), 24, 6);
	clk[per8] = imx_clk_divider("per8", "per8_sel", ccm(CCM_PCDR2), 0, 6);
	clk[per9] = imx_clk_divider("per9", "per9_sel", ccm(CCM_PCDR2), 8, 6);
	clk[per10] = imx_clk_divider("per10", "per10_sel", ccm(CCM_PCDR2), 16, 6);
	clk[per11] = imx_clk_divider("per11", "per11_sel", ccm(CCM_PCDR2), 24, 6);
	clk[per12] = imx_clk_divider("per12", "per12_sel", ccm(CCM_PCDR3), 0, 6);
	clk[per13] = imx_clk_divider("per13", "per13_sel", ccm(CCM_PCDR3), 8, 6);
	clk[per14] = imx_clk_divider("per14", "per14_sel", ccm(CCM_PCDR3), 16, 6);
	clk[per15] = imx_clk_divider("per15", "per15_sel", ccm(CCM_PCDR3), 24, 6);
	clk[csi_ipg_per] = imx_clk_gate("csi_ipg_per", "per0", ccm(CCM_CGCR0), 0);
	clk[epit_ipg_per] = imx_clk_gate("epit_ipg_per", "per1", ccm(CCM_CGCR0),  1);
	clk[esai_ipg_per] = imx_clk_gate("esai_ipg_per", "per2", ccm(CCM_CGCR0),  2);
	clk[esdhc1_ipg_per] = imx_clk_gate("esdhc1_ipg_per", "per3", ccm(CCM_CGCR0),  3);
	clk[esdhc2_ipg_per] = imx_clk_gate("esdhc2_ipg_per", "per4", ccm(CCM_CGCR0),  4);
	clk[gpt_ipg_per] = imx_clk_gate("gpt_ipg_per", "per5", ccm(CCM_CGCR0),  5);
	clk[i2c_ipg_per] = imx_clk_gate("i2c_ipg_per", "per6", ccm(CCM_CGCR0),  6);
	clk[lcdc_ipg_per] = imx_clk_gate("lcdc_ipg_per", "per7", ccm(CCM_CGCR0),  7);
	clk[nfc_ipg_per] = imx_clk_gate("nfc_ipg_per", "per8", ccm(CCM_CGCR0),  8);
	clk[owire_ipg_per] = imx_clk_gate("owire_ipg_per", "per9", ccm(CCM_CGCR0),  9);
	clk[pwm_ipg_per] = imx_clk_gate("pwm_ipg_per", "per10", ccm(CCM_CGCR0),  10);
	clk[sim1_ipg_per] = imx_clk_gate("sim1_ipg_per", "per11", ccm(CCM_CGCR0),  11);
	clk[sim2_ipg_per] = imx_clk_gate("sim2_ipg_per", "per12", ccm(CCM_CGCR0),  12);
	clk[ssi1_ipg_per] = imx_clk_gate("ssi1_ipg_per", "per13", ccm(CCM_CGCR0), 13);
	clk[ssi2_ipg_per] = imx_clk_gate("ssi2_ipg_per", "per14", ccm(CCM_CGCR0), 14);
	clk[uart_ipg_per] = imx_clk_gate("uart_ipg_per", "per15", ccm(CCM_CGCR0), 15);
	clk[ata_ahb] = imx_clk_gate("ata_ahb", "ahb", ccm(CCM_CGCR0), 16);
	/* CCM_CGCR0(17): reserved */
	clk[csi_ahb] = imx_clk_gate("csi_ahb", "ahb", ccm(CCM_CGCR0), 18);
	clk[emi_ahb] = imx_clk_gate("emi_ahb", "ahb", ccm(CCM_CGCR0), 19);
	clk[esai_ahb] = imx_clk_gate("esai_ahb", "ahb", ccm(CCM_CGCR0), 20);
	clk[esdhc1_ahb] = imx_clk_gate("esdhc1_ahb", "ahb", ccm(CCM_CGCR0), 21);
	clk[esdhc2_ahb] = imx_clk_gate("esdhc2_ahb", "ahb", ccm(CCM_CGCR0), 22);
	clk[fec_ahb] = imx_clk_gate("fec_ahb", "ahb", ccm(CCM_CGCR0), 23);
	clk[lcdc_ahb] = imx_clk_gate("lcdc_ahb", "ahb", ccm(CCM_CGCR0), 24);
	clk[rtic_ahb] = imx_clk_gate("rtic_ahb", "ahb", ccm(CCM_CGCR0), 25);
	clk[sdma_ahb] = imx_clk_gate("sdma_ahb", "ahb", ccm(CCM_CGCR0), 26);
	clk[slcdc_ahb] = imx_clk_gate("slcdc_ahb", "ahb", ccm(CCM_CGCR0), 27);
	clk[usbotg_ahb] = imx_clk_gate("usbotg_ahb", "ahb", ccm(CCM_CGCR0), 28);
	/* CCM_CGCR0(29-31): reserved */
	/* CCM_CGCR1(0): reserved in datasheet, used as audmux in FSL kernel */
	clk[can1_ipg] = imx_clk_gate("can1_ipg", "ipg", ccm(CCM_CGCR1),  2);
	clk[can2_ipg] = imx_clk_gate("can2_ipg", "ipg", ccm(CCM_CGCR1),  3);
	clk[csi_ipg] = imx_clk_gate("csi_ipg", "ipg", ccm(CCM_CGCR1),  4);
	clk[cspi1_ipg] = imx_clk_gate("cspi1_ipg", "ipg", ccm(CCM_CGCR1),  5);
	clk[cspi2_ipg] = imx_clk_gate("cspi2_ipg", "ipg", ccm(CCM_CGCR1),  6);
	clk[cspi3_ipg] = imx_clk_gate("cspi3_ipg", "ipg", ccm(CCM_CGCR1),  7);
	clk[dryice_ipg] = imx_clk_gate("dryice_ipg", "ipg", ccm(CCM_CGCR1),  8);
	clk[ect_ipg] = imx_clk_gate("ect_ipg", "ipg", ccm(CCM_CGCR1),  9);
	clk[epit1_ipg] = imx_clk_gate("epit1_ipg", "ipg", ccm(CCM_CGCR1),  10);
	clk[epit2_ipg] = imx_clk_gate("epit2_ipg", "ipg", ccm(CCM_CGCR1),  11);
	/* CCM_CGCR1(12): reserved in datasheet, used as esai in FSL kernel */
	clk[esdhc1_ipg] = imx_clk_gate("esdhc1_ipg", "ipg", ccm(CCM_CGCR1), 13);
	clk[esdhc2_ipg] = imx_clk_gate("esdhc2_ipg", "ipg", ccm(CCM_CGCR1), 14);
	clk[fec_ipg] = imx_clk_gate("fec_ipg", "ipg", ccm(CCM_CGCR1), 15);
	/* CCM_CGCR1(16): reserved in datasheet, used as gpio1 in FSL kernel */
	/* CCM_CGCR1(17): reserved in datasheet, used as gpio2 in FSL kernel */
	/* CCM_CGCR1(18): reserved in datasheet, used as gpio3 in FSL kernel */
	clk[gpt1_ipg] = imx_clk_gate("gpt1_ipg", "ipg", ccm(CCM_CGCR1), 19);
	clk[gpt2_ipg] = imx_clk_gate("gpt2_ipg", "ipg", ccm(CCM_CGCR1), 20);
	clk[gpt3_ipg] = imx_clk_gate("gpt3_ipg", "ipg", ccm(CCM_CGCR1), 21);
	clk[gpt4_ipg] = imx_clk_gate("gpt4_ipg", "ipg", ccm(CCM_CGCR1), 22);
	/* CCM_CGCR1(23): reserved in datasheet, used as i2c1 in FSL kernel */
	/* CCM_CGCR1(24): reserved in datasheet, used as i2c2 in FSL kernel */
	/* CCM_CGCR1(25): reserved in datasheet, used as i2c3 in FSL kernel */
	clk[iim_ipg] = imx_clk_gate("iim_ipg", "ipg", ccm(CCM_CGCR1), 26);
	/* CCM_CGCR1(27): reserved in datasheet, used as iomuxc in FSL kernel */
	/* CCM_CGCR1(28): reserved in datasheet, used as kpp in FSL kernel */
	clk[kpp_ipg] = imx_clk_gate("kpp_ipg", "ipg", ccm(CCM_CGCR1), 28);
	clk[lcdc_ipg] = imx_clk_gate("lcdc_ipg", "ipg", ccm(CCM_CGCR1), 29);
	/* CCM_CGCR1(30): reserved in datasheet, used as owire in FSL kernel */
	clk[pwm1_ipg] = imx_clk_gate("pwm1_ipg", "ipg", ccm(CCM_CGCR1), 31);
	clk[pwm2_ipg] = imx_clk_gate("pwm2_ipg", "ipg", ccm(CCM_CGCR2),  0);
	clk[pwm3_ipg] = imx_clk_gate("pwm3_ipg", "ipg", ccm(CCM_CGCR2),  1);
	clk[pwm4_ipg] = imx_clk_gate("pwm4_ipg", "ipg", ccm(CCM_CGCR2),  2);
	clk[rngb_ipg] = imx_clk_gate("rngb_ipg", "ipg", ccm(CCM_CGCR2),  3);
	/* CCM_CGCR2(4): reserved in datasheet, used as rtic in FSL kernel */
	clk[scc_ipg] = imx_clk_gate("scc_ipg", "ipg", ccm(CCM_CGCR2),  5);
	clk[sdma_ipg] = imx_clk_gate("sdma_ipg", "ipg", ccm(CCM_CGCR2),  6);
	clk[sim1_ipg] = imx_clk_gate("sim1_ipg", "ipg", ccm(CCM_CGCR2),  7);
	clk[sim2_ipg] = imx_clk_gate("sim2_ipg", "ipg", ccm(CCM_CGCR2),  8);
	clk[slcdc_ipg] = imx_clk_gate("slcdc_ipg", "ipg", ccm(CCM_CGCR2),  9);
	clk[spba_ipg] = imx_clk_gate("spba_ipg", "ipg", ccm(CCM_CGCR2),  10);
	clk[ssi1_ipg] = imx_clk_gate("ssi1_ipg", "ipg", ccm(CCM_CGCR2), 11);
	clk[ssi2_ipg] = imx_clk_gate("ssi2_ipg", "ipg", ccm(CCM_CGCR2), 12);
	clk[tsc_ipg] = imx_clk_gate("tsc_ipg", "ipg", ccm(CCM_CGCR2), 13);
	clk[uart1_ipg] = imx_clk_gate("uart1_ipg", "ipg", ccm(CCM_CGCR2), 14);
	clk[uart2_ipg] = imx_clk_gate("uart2_ipg", "ipg", ccm(CCM_CGCR2), 15);
	clk[uart3_ipg] = imx_clk_gate("uart3_ipg", "ipg", ccm(CCM_CGCR2), 16);
	clk[uart4_ipg] = imx_clk_gate("uart4_ipg", "ipg", ccm(CCM_CGCR2), 17);
	clk[uart5_ipg] = imx_clk_gate("uart5_ipg", "ipg", ccm(CCM_CGCR2), 18);
	/* CCM_CGCR2(19): reserved in datasheet, but used as wdt in FSL kernel */
	clk[wdt_ipg] = imx_clk_gate("wdt_ipg", "ipg", ccm(CCM_CGCR2), 19);

	for (i = 0; i < ARRAY_SIZE(clk); i++)
		if (IS_ERR(clk[i]))
			pr_err("i.MX25 clk %d: register failed with %ld\n",
				i, PTR_ERR(clk[i]));

	clk_prepare_enable(clk[emi_ahb]);

	clk_register_clkdev(clk[ipg], "ipg", "imx-gpt.0");
	clk_register_clkdev(clk[gpt_ipg_per], "per", "imx-gpt.0");

	return 0;
}

int __init mx25_clocks_init(void)
{
	__mx25_clocks_init(24000000);

	/* i.mx25 has the i.mx21 type uart */
	clk_register_clkdev(clk[uart1_ipg], "ipg", "imx21-uart.0");
	clk_register_clkdev(clk[uart_ipg_per], "per", "imx21-uart.0");
	clk_register_clkdev(clk[uart2_ipg], "ipg", "imx21-uart.1");
	clk_register_clkdev(clk[uart_ipg_per], "per", "imx21-uart.1");
	clk_register_clkdev(clk[uart3_ipg], "ipg", "imx21-uart.2");
	clk_register_clkdev(clk[uart_ipg_per], "per", "imx21-uart.2");
	clk_register_clkdev(clk[uart4_ipg], "ipg", "imx21-uart.3");
	clk_register_clkdev(clk[uart_ipg_per], "per", "imx21-uart.3");
	clk_register_clkdev(clk[uart5_ipg], "ipg", "imx21-uart.4");
	clk_register_clkdev(clk[uart_ipg_per], "per", "imx21-uart.4");
	clk_register_clkdev(clk[ipg], "ipg", "mxc-ehci.0");
	clk_register_clkdev(clk[usbotg_ahb], "ahb", "mxc-ehci.0");
	clk_register_clkdev(clk[usb_div], "per", "mxc-ehci.0");
	clk_register_clkdev(clk[ipg], "ipg", "mxc-ehci.1");
	clk_register_clkdev(clk[usbotg_ahb], "ahb", "mxc-ehci.1");
	clk_register_clkdev(clk[usb_div], "per", "mxc-ehci.1");
	clk_register_clkdev(clk[ipg], "ipg", "mxc-ehci.2");
	clk_register_clkdev(clk[usbotg_ahb], "ahb", "mxc-ehci.2");
	clk_register_clkdev(clk[usb_div], "per", "mxc-ehci.2");
	clk_register_clkdev(clk[ipg], "ipg", "imx-udc-mx27");
	clk_register_clkdev(clk[usbotg_ahb], "ahb", "imx-udc-mx27");
	clk_register_clkdev(clk[usb_div], "per", "imx-udc-mx27");
	clk_register_clkdev(clk[nfc_ipg_per], NULL, "imx25-nand.0");
	/* i.mx25 has the i.mx35 type cspi */
	clk_register_clkdev(clk[cspi1_ipg], NULL, "imx35-cspi.0");
	clk_register_clkdev(clk[cspi2_ipg], NULL, "imx35-cspi.1");
	clk_register_clkdev(clk[cspi3_ipg], NULL, "imx35-cspi.2");
	clk_register_clkdev(clk[pwm1_ipg], "ipg", "mxc_pwm.0");
	clk_register_clkdev(clk[per10], "per", "mxc_pwm.0");
	clk_register_clkdev(clk[pwm1_ipg], "ipg", "mxc_pwm.1");
	clk_register_clkdev(clk[per10], "per", "mxc_pwm.1");
	clk_register_clkdev(clk[pwm1_ipg], "ipg", "mxc_pwm.2");
	clk_register_clkdev(clk[per10], "per", "mxc_pwm.2");
	clk_register_clkdev(clk[pwm1_ipg], "ipg", "mxc_pwm.3");
	clk_register_clkdev(clk[per10], "per", "mxc_pwm.3");
	clk_register_clkdev(clk[kpp_ipg], NULL, "imx-keypad");
	clk_register_clkdev(clk[tsc_ipg], NULL, "mx25-adc");
	clk_register_clkdev(clk[i2c_ipg_per], NULL, "imx21-i2c.0");
	clk_register_clkdev(clk[i2c_ipg_per], NULL, "imx21-i2c.1");
	clk_register_clkdev(clk[i2c_ipg_per], NULL, "imx21-i2c.2");
	clk_register_clkdev(clk[fec_ipg], "ipg", "imx25-fec.0");
	clk_register_clkdev(clk[fec_ahb], "ahb", "imx25-fec.0");
	clk_register_clkdev(clk[dryice_ipg], NULL, "imxdi_rtc.0");
	clk_register_clkdev(clk[lcdc_ipg_per], "per", "imx21-fb.0");
	clk_register_clkdev(clk[lcdc_ipg], "ipg", "imx21-fb.0");
	clk_register_clkdev(clk[lcdc_ahb], "ahb", "imx21-fb.0");
	clk_register_clkdev(clk[wdt_ipg], NULL, "imx2-wdt.0");
	clk_register_clkdev(clk[ssi1_ipg], NULL, "imx-ssi.0");
	clk_register_clkdev(clk[ssi2_ipg], NULL, "imx-ssi.1");
	clk_register_clkdev(clk[esdhc1_ipg_per], "per", "sdhci-esdhc-imx25.0");
	clk_register_clkdev(clk[esdhc1_ipg], "ipg", "sdhci-esdhc-imx25.0");
	clk_register_clkdev(clk[esdhc1_ahb], "ahb", "sdhci-esdhc-imx25.0");
	clk_register_clkdev(clk[esdhc2_ipg_per], "per", "sdhci-esdhc-imx25.1");
	clk_register_clkdev(clk[esdhc2_ipg], "ipg", "sdhci-esdhc-imx25.1");
	clk_register_clkdev(clk[esdhc2_ahb], "ahb", "sdhci-esdhc-imx25.1");
	clk_register_clkdev(clk[csi_ipg_per], "per", "imx25-camera.0");
	clk_register_clkdev(clk[csi_ipg], "ipg", "imx25-camera.0");
	clk_register_clkdev(clk[csi_ahb], "ahb", "imx25-camera.0");
	clk_register_clkdev(clk[dummy], "audmux", NULL);
	clk_register_clkdev(clk[can1_ipg], NULL, "flexcan.0");
	clk_register_clkdev(clk[can2_ipg], NULL, "flexcan.1");
	/* i.mx25 has the i.mx35 type sdma */
	clk_register_clkdev(clk[sdma_ipg], "ipg", "imx35-sdma");
	clk_register_clkdev(clk[sdma_ahb], "ahb", "imx35-sdma");
	clk_register_clkdev(clk[iim_ipg], "iim", NULL);

	mxc_timer_init(MX25_IO_ADDRESS(MX25_GPT1_BASE_ADDR), MX25_INT_GPT1);

	return 0;
}

int __init mx25_clocks_init_dt(void)
{
	struct device_node *np;
	void __iomem *base;
	int irq;
	unsigned long osc_rate = 24000000;

	/* retrieve the freqency of fixed clocks from device tree */
	for_each_compatible_node(np, NULL, "fixed-clock") {
		u32 rate;
		if (of_property_read_u32(np, "clock-frequency", &rate))
			continue;

		if (of_device_is_compatible(np, "fsl,imx-osc"))
			osc_rate = rate;
	}

	np = of_find_compatible_node(NULL, NULL, "fsl,imx25-ccm");
	clk_data.clks = clk;
	clk_data.clk_num = ARRAY_SIZE(clk);
	of_clk_add_provider(np, of_clk_src_onecell_get, &clk_data);

	__mx25_clocks_init(osc_rate);

	np = of_find_compatible_node(NULL, NULL, "fsl,imx25-gpt");
	base = of_iomap(np, 0);
	WARN_ON(!base);
	irq = irq_of_parse_and_map(np, 0);

	mxc_timer_init(base, irq);

	return 0;
}
