/*
 * Copyright 2012 Freescale Semiconductor, Inc.
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/io.h>
#include <mach/common.h>
#include <mach/mx23.h>
#include "clk.h"

#define DIGCTRL			MX23_IO_ADDRESS(MX23_DIGCTL_BASE_ADDR)
#define CLKCTRL			MX23_IO_ADDRESS(MX23_CLKCTRL_BASE_ADDR)
#define PLLCTRL0		(CLKCTRL + 0x0000)
#define CPU			(CLKCTRL + 0x0020)
#define HBUS			(CLKCTRL + 0x0030)
#define XBUS			(CLKCTRL + 0x0040)
#define XTAL			(CLKCTRL + 0x0050)
#define PIX			(CLKCTRL + 0x0060)
#define SSP			(CLKCTRL + 0x0070)
#define GPMI			(CLKCTRL + 0x0080)
#define SPDIF			(CLKCTRL + 0x0090)
#define EMI			(CLKCTRL + 0x00a0)
#define SAIF			(CLKCTRL + 0x00c0)
#define TV			(CLKCTRL + 0x00d0)
#define ETM			(CLKCTRL + 0x00e0)
#define FRAC			(CLKCTRL + 0x00f0)
#define CLKSEQ			(CLKCTRL + 0x0110)

#define BP_CPU_INTERRUPT_WAIT	12
#define BP_CLKSEQ_BYPASS_SAIF	0
#define BP_CLKSEQ_BYPASS_SSP	5
#define BP_SAIF_DIV_FRAC_EN	16
#define BP_FRAC_IOFRAC		24

static void __init clk_misc_init(void)
{
	u32 val;

	/* Gate off cpu clock in WFI for power saving */
	__mxs_setl(1 << BP_CPU_INTERRUPT_WAIT, CPU);

	/* Clear BYPASS for SAIF */
	__mxs_clrl(1 << BP_CLKSEQ_BYPASS_SAIF, CLKSEQ);

	/* SAIF has to use frac div for functional operation */
	val = readl_relaxed(SAIF);
	val |= 1 << BP_SAIF_DIV_FRAC_EN;
	writel_relaxed(val, SAIF);

	/*
	 * Source ssp clock from ref_io than ref_xtal,
	 * as ref_xtal only provides 24 MHz as maximum.
	 */
	__mxs_clrl(1 << BP_CLKSEQ_BYPASS_SSP, CLKSEQ);

	/*
	 * 480 MHz seems too high to be ssp clock source directly,
	 * so set frac to get a 288 MHz ref_io.
	 */
	__mxs_clrl(0x3f << BP_FRAC_IOFRAC, FRAC);
	__mxs_setl(30 << BP_FRAC_IOFRAC, FRAC);
}

static struct clk_lookup uart_lookups[] __initdata = {
	{ .dev_id = "duart", },
	{ .dev_id = "mxs-auart.0", },
	{ .dev_id = "mxs-auart.1", },
	{ .dev_id = "8006c000.serial", },
	{ .dev_id = "8006e000.serial", },
	{ .dev_id = "80070000.serial", },
};

static struct clk_lookup hbus_lookups[] __initdata = {
	{ .dev_id = "mxs-dma-apbh", },
	{ .dev_id = "80004000.dma-apbh", },
};

static struct clk_lookup xbus_lookups[] __initdata = {
	{ .dev_id = "duart", .con_id = "apb_pclk"},
	{ .dev_id = "80070000.serial", .con_id = "apb_pclk"},
	{ .dev_id = "mxs-dma-apbx", },
	{ .dev_id = "80024000.dma-apbx", },
};

static struct clk_lookup ssp_lookups[] __initdata = {
	{ .dev_id = "mxs-mmc.0", },
	{ .dev_id = "mxs-mmc.1", },
	{ .dev_id = "80010000.ssp", },
	{ .dev_id = "80034000.ssp", },
};

static struct clk_lookup lcdif_lookups[] __initdata = {
	{ .dev_id = "imx23-fb", },
	{ .dev_id = "80030000.lcdif", },
};

static struct clk_lookup gpmi_lookups[] __initdata = {
	{ .dev_id = "imx23-gpmi-nand", },
	{ .dev_id = "8000c000.gpmi", },
};

static const char *sel_pll[]  __initconst = { "pll", "ref_xtal", };
static const char *sel_cpu[]  __initconst = { "ref_cpu", "ref_xtal", };
static const char *sel_pix[]  __initconst = { "ref_pix", "ref_xtal", };
static const char *sel_io[]   __initconst = { "ref_io", "ref_xtal", };
static const char *cpu_sels[] __initconst = { "cpu_pll", "cpu_xtal", };
static const char *emi_sels[] __initconst = { "emi_pll", "emi_xtal", };

enum imx23_clk {
	ref_xtal, pll, ref_cpu, ref_emi, ref_pix, ref_io, saif_sel,
	lcdif_sel, gpmi_sel, ssp_sel, emi_sel, cpu, etm_sel, cpu_pll,
	cpu_xtal, hbus, xbus, lcdif_div, ssp_div, gpmi_div, emi_pll,
	emi_xtal, etm_div, saif_div, clk32k_div, rtc, adc, spdif_div,
	clk32k, dri, pwm, filt, uart, ssp, gpmi, spdif, emi, saif,
	lcdif, etm, usb, usb_pwr,
	clk_max
};

static struct clk *clks[clk_max];

static enum imx23_clk clks_init_on[] __initdata = {
	cpu, hbus, xbus, emi, uart,
};

int __init mx23_clocks_init(void)
{
	int i;

	clk_misc_init();

	clks[ref_xtal] = mxs_clk_fixed("ref_xtal", 24000000);
	clks[pll] = mxs_clk_pll("pll", "ref_xtal", PLLCTRL0, 16, 480000000);
	clks[ref_cpu] = mxs_clk_ref("ref_cpu", "pll", FRAC, 0);
	clks[ref_emi] = mxs_clk_ref("ref_emi", "pll", FRAC, 1);
	clks[ref_pix] = mxs_clk_ref("ref_pix", "pll", FRAC, 2);
	clks[ref_io] = mxs_clk_ref("ref_io", "pll", FRAC, 3);
	clks[saif_sel] = mxs_clk_mux("saif_sel", CLKSEQ, 0, 1, sel_pll, ARRAY_SIZE(sel_pll));
	clks[lcdif_sel] = mxs_clk_mux("lcdif_sel", CLKSEQ, 1, 1, sel_pix, ARRAY_SIZE(sel_pix));
	clks[gpmi_sel] = mxs_clk_mux("gpmi_sel", CLKSEQ, 4, 1, sel_io, ARRAY_SIZE(sel_io));
	clks[ssp_sel] = mxs_clk_mux("ssp_sel", CLKSEQ, 5, 1, sel_io, ARRAY_SIZE(sel_io));
	clks[emi_sel] = mxs_clk_mux("emi_sel", CLKSEQ, 6, 1, emi_sels, ARRAY_SIZE(emi_sels));
	clks[cpu] = mxs_clk_mux("cpu", CLKSEQ, 7, 1, cpu_sels, ARRAY_SIZE(cpu_sels));
	clks[etm_sel] = mxs_clk_mux("etm_sel", CLKSEQ, 8, 1, sel_cpu, ARRAY_SIZE(sel_cpu));
	clks[cpu_pll] = mxs_clk_div("cpu_pll", "ref_cpu", CPU, 0, 6, 28);
	clks[cpu_xtal] = mxs_clk_div("cpu_xtal", "ref_xtal", CPU, 16, 10, 29);
	clks[hbus] = mxs_clk_div("hbus", "cpu", HBUS, 0, 5, 29);
	clks[xbus] = mxs_clk_div("xbus", "ref_xtal", XBUS, 0, 10, 31);
	clks[lcdif_div] = mxs_clk_div("lcdif_div", "lcdif_sel", PIX, 0, 12, 29);
	clks[ssp_div] = mxs_clk_div("ssp_div", "ssp_sel", SSP, 0, 9, 29);
	clks[gpmi_div] = mxs_clk_div("gpmi_div", "gpmi_sel", GPMI, 0, 10, 29);
	clks[emi_pll] = mxs_clk_div("emi_pll", "ref_emi", EMI, 0, 6, 28);
	clks[emi_xtal] = mxs_clk_div("emi_xtal", "ref_xtal", EMI, 8, 4, 29);
	clks[etm_div] = mxs_clk_div("etm_div", "etm_sel", ETM, 0, 6, 29);
	clks[saif_div] = mxs_clk_frac("saif_div", "saif_sel", SAIF, 0, 16, 29);
	clks[clk32k_div] = mxs_clk_fixed_factor("clk32k_div", "ref_xtal", 1, 750);
	clks[rtc] = mxs_clk_fixed_factor("rtc", "ref_xtal", 1, 768);
	clks[adc] = mxs_clk_fixed_factor("adc", "clk32k", 1, 16);
	clks[spdif_div] = mxs_clk_fixed_factor("spdif_div", "pll", 1, 4);
	clks[clk32k] = mxs_clk_gate("clk32k", "clk32k_div", XTAL, 26);
	clks[dri] = mxs_clk_gate("dri", "ref_xtal", XTAL, 28);
	clks[pwm] = mxs_clk_gate("pwm", "ref_xtal", XTAL, 29);
	clks[filt] = mxs_clk_gate("filt", "ref_xtal", XTAL, 30);
	clks[uart] = mxs_clk_gate("uart", "ref_xtal", XTAL, 31);
	clks[ssp] = mxs_clk_gate("ssp", "ssp_div", SSP, 31);
	clks[gpmi] = mxs_clk_gate("gpmi", "gpmi_div", GPMI, 31);
	clks[spdif] = mxs_clk_gate("spdif", "spdif_div", SPDIF, 31);
	clks[emi] = mxs_clk_gate("emi", "emi_sel", EMI, 31);
	clks[saif] = mxs_clk_gate("saif", "saif_div", SAIF, 31);
	clks[lcdif] = mxs_clk_gate("lcdif", "lcdif_div", PIX, 31);
	clks[etm] = mxs_clk_gate("etm", "etm_div", ETM, 31);
	clks[usb] = mxs_clk_gate("usb", "usb_pwr", DIGCTRL, 2);
	clks[usb_pwr] = clk_register_gate(NULL, "usb_pwr", "pll", 0, PLLCTRL0, 18, 0, &mxs_lock);

	for (i = 0; i < ARRAY_SIZE(clks); i++)
		if (IS_ERR(clks[i])) {
			pr_err("i.MX23 clk %d: register failed with %ld\n",
				i, PTR_ERR(clks[i]));
			return PTR_ERR(clks[i]);
		}

	clk_register_clkdev(clks[clk32k], NULL, "timrot");
	clk_register_clkdevs(clks[hbus], hbus_lookups, ARRAY_SIZE(hbus_lookups));
	clk_register_clkdevs(clks[xbus], xbus_lookups, ARRAY_SIZE(xbus_lookups));
	clk_register_clkdevs(clks[uart], uart_lookups, ARRAY_SIZE(uart_lookups));
	clk_register_clkdevs(clks[ssp], ssp_lookups, ARRAY_SIZE(ssp_lookups));
	clk_register_clkdevs(clks[gpmi], gpmi_lookups, ARRAY_SIZE(gpmi_lookups));
	clk_register_clkdevs(clks[lcdif], lcdif_lookups, ARRAY_SIZE(lcdif_lookups));

	for (i = 0; i < ARRAY_SIZE(clks_init_on); i++)
		clk_prepare_enable(clks[clks_init_on[i]]);

	mxs_timer_init(MX23_INT_TIMER0);

	return 0;
}
