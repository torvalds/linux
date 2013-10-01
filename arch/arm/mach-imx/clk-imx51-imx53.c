/*
 * Copyright (C) 2011 Sascha Hauer, Pengutronix <s.hauer@pengutronix.de>
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
#include <linux/clk-provider.h>
#include <linux/of.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>

#include "crm-regs-imx5.h"
#include "clk.h"
#include "common.h"
#include "hardware.h"

/* Low-power Audio Playback Mode clock */
static const char *lp_apm_sel[] = { "osc", };

/* This is used multiple times */
static const char *standard_pll_sel[] = { "pll1_sw", "pll2_sw", "pll3_sw", "lp_apm", };
static const char *periph_apm_sel[] = { "pll1_sw", "pll3_sw", "lp_apm", };
static const char *main_bus_sel[] = { "pll2_sw", "periph_apm", };
static const char *per_lp_apm_sel[] = { "main_bus", "lp_apm", };
static const char *per_root_sel[] = { "per_podf", "ipg", };
static const char *esdhc_c_sel[] = { "esdhc_a_podf", "esdhc_b_podf", };
static const char *esdhc_d_sel[] = { "esdhc_a_podf", "esdhc_b_podf", };
static const char *ssi_apm_sels[] = { "ckih1", "lp_amp", "ckih2", };
static const char *ssi_clk_sels[] = { "pll1_sw", "pll2_sw", "pll3_sw", "ssi_apm", };
static const char *ssi3_clk_sels[] = { "ssi1_root_gate", "ssi2_root_gate", };
static const char *ssi_ext1_com_sels[] = { "ssi_ext1_podf", "ssi1_root_gate", };
static const char *ssi_ext2_com_sels[] = { "ssi_ext2_podf", "ssi2_root_gate", };
static const char *emi_slow_sel[] = { "main_bus", "ahb", };
static const char *usb_phy_sel_str[] = { "osc", "usb_phy_podf", };
static const char *mx51_ipu_di0_sel[] = { "di_pred", "osc", "ckih1", "tve_di", };
static const char *mx53_ipu_di0_sel[] = { "di_pred", "osc", "ckih1", "di_pll4_podf", "dummy", "ldb_di0_gate", };
static const char *mx53_ldb_di0_sel[] = { "pll3_sw", "pll4_sw", };
static const char *mx51_ipu_di1_sel[] = { "di_pred", "osc", "ckih1", "tve_di", "ipp_di1", };
static const char *mx53_ipu_di1_sel[] = { "di_pred", "osc", "ckih1", "tve_di", "ipp_di1", "ldb_di1_gate", };
static const char *mx53_ldb_di1_sel[] = { "pll3_sw", "pll4_sw", };
static const char *mx51_tve_ext_sel[] = { "osc", "ckih1", };
static const char *mx53_tve_ext_sel[] = { "pll4_sw", "ckih1", };
static const char *mx51_tve_sel[] = { "tve_pred", "tve_ext_sel", };
static const char *ipu_sel[] = { "axi_a", "axi_b", "emi_slow_gate", "ahb", };
static const char *gpu3d_sel[] = { "axi_a", "axi_b", "emi_slow_gate", "ahb" };
static const char *gpu2d_sel[] = { "axi_a", "axi_b", "emi_slow_gate", "ahb" };
static const char *vpu_sel[] = { "axi_a", "axi_b", "emi_slow_gate", "ahb", };
static const char *mx53_can_sel[] = { "ipg", "ckih1", "ckih2", "lp_apm", };
static const char *mx53_cko1_sel[] = {
	"cpu_podf", "pll1_sw", "pll2_sw", "pll3_sw",
	"emi_slow_podf", "pll4_sw", "nfc_podf", "dummy",
	"di_pred", "dummy", "dummy", "ahb",
	"ipg", "per_root", "ckil", "dummy",};
static const char *mx53_cko2_sel[] = {
	"dummy"/* dptc_core */, "dummy"/* dptc_perich */,
	"dummy", "esdhc_a_podf",
	"usboh3_podf", "dummy"/* wrck_clk_root */,
	"ecspi_podf", "dummy"/* pll1_ref_clk */,
	"esdhc_b_podf", "dummy"/* ddr_clk_root */,
	"dummy"/* arm_axi_clk_root */, "dummy"/* usb_phy_out */,
	"vpu_sel", "ipu_sel",
	"osc", "ckih1",
	"dummy", "esdhc_c_sel",
	"ssi1_root_podf", "ssi2_root_podf",
	"dummy", "dummy",
	"dummy"/* lpsr_clk_root */, "dummy"/* pgc_clk_root */,
	"dummy"/* tve_out */, "usb_phy_sel",
	"tve_sel", "lp_apm",
	"uart_root", "dummy"/* spdif0_clk_root */,
	"dummy", "dummy", };
static const char *mx51_spdif_xtal_sel[] = { "osc", "ckih", "ckih2", };
static const char *mx53_spdif_xtal_sel[] = { "osc", "ckih", "ckih2", "pll4_sw", };
static const char *spdif_sel[] = { "pll1_sw", "pll2_sw", "pll3_sw", "spdif_xtal_sel", };
static const char *spdif0_com_sel[] = { "spdif0_podf", "ssi1_root_gate", };
static const char *mx51_spdif1_com_sel[] = { "spdif1_podf", "ssi2_root_gate", };


enum imx5_clks {
	dummy, ckil, osc, ckih1, ckih2, ahb, ipg, axi_a, axi_b, uart_pred,
	uart_root, esdhc_a_pred, esdhc_b_pred, esdhc_c_s, esdhc_d_s,
	emi_sel, emi_slow_podf, nfc_podf, ecspi_pred, ecspi_podf, usboh3_pred,
	usboh3_podf, usb_phy_pred, usb_phy_podf, cpu_podf, di_pred, tve_di_unused,
	tve_s, uart1_ipg_gate, uart1_per_gate, uart2_ipg_gate,
	uart2_per_gate, uart3_ipg_gate, uart3_per_gate, i2c1_gate, i2c2_gate,
	gpt_ipg_gate, pwm1_ipg_gate, pwm1_hf_gate, pwm2_ipg_gate, pwm2_hf_gate,
	gpt_hf_gate, fec_gate, usboh3_per_gate, esdhc1_ipg_gate, esdhc2_ipg_gate,
	esdhc3_ipg_gate, esdhc4_ipg_gate, ssi1_ipg_gate, ssi2_ipg_gate,
	ssi3_ipg_gate, ecspi1_ipg_gate, ecspi1_per_gate, ecspi2_ipg_gate,
	ecspi2_per_gate, cspi_ipg_gate, sdma_gate, emi_slow_gate, ipu_s,
	ipu_gate, nfc_gate, ipu_di1_gate, vpu_s, vpu_gate,
	vpu_reference_gate, uart4_ipg_gate, uart4_per_gate, uart5_ipg_gate,
	uart5_per_gate, tve_gate, tve_pred, esdhc1_per_gate, esdhc2_per_gate,
	esdhc3_per_gate, esdhc4_per_gate, usb_phy_gate, hsi2c_gate,
	mipi_hsc1_gate, mipi_hsc2_gate, mipi_esc_gate, mipi_hsp_gate,
	ldb_di1_div_3_5, ldb_di1_div, ldb_di0_div_3_5, ldb_di0_div,
	ldb_di1_gate, can2_serial_gate, can2_ipg_gate, i2c3_gate, lp_apm,
	periph_apm, main_bus, ahb_max, aips_tz1, aips_tz2, tmax1, tmax2,
	tmax3, spba, uart_sel, esdhc_a_sel, esdhc_b_sel, esdhc_a_podf,
	esdhc_b_podf, ecspi_sel, usboh3_sel, usb_phy_sel, iim_gate,
	usboh3_gate, emi_fast_gate, ipu_di0_gate,gpc_dvfs, pll1_sw, pll2_sw,
	pll3_sw, ipu_di0_sel, ipu_di1_sel, tve_ext_sel, mx51_mipi, pll4_sw,
	ldb_di1_sel, di_pll4_podf, ldb_di0_sel, ldb_di0_gate, usb_phy1_gate,
	usb_phy2_gate, per_lp_apm, per_pred1, per_pred2, per_podf, per_root,
	ssi_apm, ssi1_root_sel, ssi2_root_sel, ssi3_root_sel, ssi_ext1_sel,
	ssi_ext2_sel, ssi_ext1_com_sel, ssi_ext2_com_sel, ssi1_root_pred,
	ssi1_root_podf, ssi2_root_pred, ssi2_root_podf, ssi_ext1_pred,
	ssi_ext1_podf, ssi_ext2_pred, ssi_ext2_podf, ssi1_root_gate,
	ssi2_root_gate, ssi3_root_gate, ssi_ext1_gate, ssi_ext2_gate,
	epit1_ipg_gate, epit1_hf_gate, epit2_ipg_gate, epit2_hf_gate,
	can_sel, can1_serial_gate, can1_ipg_gate,
	owire_gate, gpu3d_s, gpu2d_s, gpu3d_gate, gpu2d_gate, garb_gate,
	cko1_sel, cko1_podf, cko1,
	cko2_sel, cko2_podf, cko2,
	srtc_gate, pata_gate, sata_gate, spdif_xtal_sel, spdif0_sel,
	spdif1_sel, spdif0_pred, spdif0_podf, spdif1_pred, spdif1_podf,
	spdif0_com_s, spdif1_com_sel, spdif0_gate, spdif1_gate, spdif_ipg_gate,
	ocram, clk_max
};

static struct clk *clk[clk_max];
static struct clk_onecell_data clk_data;

static void __init mx5_clocks_common_init(unsigned long rate_ckil,
		unsigned long rate_osc, unsigned long rate_ckih1,
		unsigned long rate_ckih2)
{
	int i;

	clk[dummy] = imx_clk_fixed("dummy", 0);
	clk[ckil] = imx_obtain_fixed_clock("ckil", rate_ckil);
	clk[osc] = imx_obtain_fixed_clock("osc", rate_osc);
	clk[ckih1] = imx_obtain_fixed_clock("ckih1", rate_ckih1);
	clk[ckih2] = imx_obtain_fixed_clock("ckih2", rate_ckih2);

	clk[lp_apm] = imx_clk_mux("lp_apm", MXC_CCM_CCSR, 9, 1,
				lp_apm_sel, ARRAY_SIZE(lp_apm_sel));
	clk[periph_apm] = imx_clk_mux("periph_apm", MXC_CCM_CBCMR, 12, 2,
				periph_apm_sel, ARRAY_SIZE(periph_apm_sel));
	clk[main_bus] = imx_clk_mux("main_bus", MXC_CCM_CBCDR, 25, 1,
				main_bus_sel, ARRAY_SIZE(main_bus_sel));
	clk[per_lp_apm] = imx_clk_mux("per_lp_apm", MXC_CCM_CBCMR, 1, 1,
				per_lp_apm_sel, ARRAY_SIZE(per_lp_apm_sel));
	clk[per_pred1] = imx_clk_divider("per_pred1", "per_lp_apm", MXC_CCM_CBCDR, 6, 2);
	clk[per_pred2] = imx_clk_divider("per_pred2", "per_pred1", MXC_CCM_CBCDR, 3, 3);
	clk[per_podf] = imx_clk_divider("per_podf", "per_pred2", MXC_CCM_CBCDR, 0, 3);
	clk[per_root] = imx_clk_mux("per_root", MXC_CCM_CBCMR, 0, 1,
				per_root_sel, ARRAY_SIZE(per_root_sel));
	clk[ahb] = imx_clk_divider("ahb", "main_bus", MXC_CCM_CBCDR, 10, 3);
	clk[ahb_max] = imx_clk_gate2("ahb_max", "ahb", MXC_CCM_CCGR0, 28);
	clk[aips_tz1] = imx_clk_gate2("aips_tz1", "ahb", MXC_CCM_CCGR0, 24);
	clk[aips_tz2] = imx_clk_gate2("aips_tz2", "ahb", MXC_CCM_CCGR0, 26);
	clk[tmax1] = imx_clk_gate2("tmax1", "ahb", MXC_CCM_CCGR1, 0);
	clk[tmax2] = imx_clk_gate2("tmax2", "ahb", MXC_CCM_CCGR1, 2);
	clk[tmax3] = imx_clk_gate2("tmax3", "ahb", MXC_CCM_CCGR1, 4);
	clk[spba] = imx_clk_gate2("spba", "ipg", MXC_CCM_CCGR5, 0);
	clk[ipg] = imx_clk_divider("ipg", "ahb", MXC_CCM_CBCDR, 8, 2);
	clk[axi_a] = imx_clk_divider("axi_a", "main_bus", MXC_CCM_CBCDR, 16, 3);
	clk[axi_b] = imx_clk_divider("axi_b", "main_bus", MXC_CCM_CBCDR, 19, 3);
	clk[uart_sel] = imx_clk_mux("uart_sel", MXC_CCM_CSCMR1, 24, 2,
				standard_pll_sel, ARRAY_SIZE(standard_pll_sel));
	clk[uart_pred] = imx_clk_divider("uart_pred", "uart_sel", MXC_CCM_CSCDR1, 3, 3);
	clk[uart_root] = imx_clk_divider("uart_root", "uart_pred", MXC_CCM_CSCDR1, 0, 3);

	clk[esdhc_a_sel] = imx_clk_mux("esdhc_a_sel", MXC_CCM_CSCMR1, 20, 2,
				standard_pll_sel, ARRAY_SIZE(standard_pll_sel));
	clk[esdhc_b_sel] = imx_clk_mux("esdhc_b_sel", MXC_CCM_CSCMR1, 16, 2,
				standard_pll_sel, ARRAY_SIZE(standard_pll_sel));
	clk[esdhc_a_pred] = imx_clk_divider("esdhc_a_pred", "esdhc_a_sel", MXC_CCM_CSCDR1, 16, 3);
	clk[esdhc_a_podf] = imx_clk_divider("esdhc_a_podf", "esdhc_a_pred", MXC_CCM_CSCDR1, 11, 3);
	clk[esdhc_b_pred] = imx_clk_divider("esdhc_b_pred", "esdhc_b_sel", MXC_CCM_CSCDR1, 22, 3);
	clk[esdhc_b_podf] = imx_clk_divider("esdhc_b_podf", "esdhc_b_pred", MXC_CCM_CSCDR1, 19, 3);
	clk[esdhc_c_s] = imx_clk_mux("esdhc_c_sel", MXC_CCM_CSCMR1, 19, 1, esdhc_c_sel, ARRAY_SIZE(esdhc_c_sel));
	clk[esdhc_d_s] = imx_clk_mux("esdhc_d_sel", MXC_CCM_CSCMR1, 18, 1, esdhc_d_sel, ARRAY_SIZE(esdhc_d_sel));

	clk[emi_sel] = imx_clk_mux("emi_sel", MXC_CCM_CBCDR, 26, 1,
				emi_slow_sel, ARRAY_SIZE(emi_slow_sel));
	clk[emi_slow_podf] = imx_clk_divider("emi_slow_podf", "emi_sel", MXC_CCM_CBCDR, 22, 3);
	clk[nfc_podf] = imx_clk_divider("nfc_podf", "emi_slow_podf", MXC_CCM_CBCDR, 13, 3);
	clk[ecspi_sel] = imx_clk_mux("ecspi_sel", MXC_CCM_CSCMR1, 4, 2,
				standard_pll_sel, ARRAY_SIZE(standard_pll_sel));
	clk[ecspi_pred] = imx_clk_divider("ecspi_pred", "ecspi_sel", MXC_CCM_CSCDR2, 25, 3);
	clk[ecspi_podf] = imx_clk_divider("ecspi_podf", "ecspi_pred", MXC_CCM_CSCDR2, 19, 6);
	clk[usboh3_sel] = imx_clk_mux("usboh3_sel", MXC_CCM_CSCMR1, 22, 2,
				standard_pll_sel, ARRAY_SIZE(standard_pll_sel));
	clk[usboh3_pred] = imx_clk_divider("usboh3_pred", "usboh3_sel", MXC_CCM_CSCDR1, 8, 3);
	clk[usboh3_podf] = imx_clk_divider("usboh3_podf", "usboh3_pred", MXC_CCM_CSCDR1, 6, 2);
	clk[usb_phy_pred] = imx_clk_divider("usb_phy_pred", "pll3_sw", MXC_CCM_CDCDR, 3, 3);
	clk[usb_phy_podf] = imx_clk_divider("usb_phy_podf", "usb_phy_pred", MXC_CCM_CDCDR, 0, 3);
	clk[usb_phy_sel] = imx_clk_mux("usb_phy_sel", MXC_CCM_CSCMR1, 26, 1,
				usb_phy_sel_str, ARRAY_SIZE(usb_phy_sel_str));
	clk[cpu_podf] = imx_clk_divider("cpu_podf", "pll1_sw", MXC_CCM_CACRR, 0, 3);
	clk[di_pred] = imx_clk_divider("di_pred", "pll3_sw", MXC_CCM_CDCDR, 6, 3);
	clk[iim_gate] = imx_clk_gate2("iim_gate", "ipg", MXC_CCM_CCGR0, 30);
	clk[uart1_ipg_gate] = imx_clk_gate2("uart1_ipg_gate", "ipg", MXC_CCM_CCGR1, 6);
	clk[uart1_per_gate] = imx_clk_gate2("uart1_per_gate", "uart_root", MXC_CCM_CCGR1, 8);
	clk[uart2_ipg_gate] = imx_clk_gate2("uart2_ipg_gate", "ipg", MXC_CCM_CCGR1, 10);
	clk[uart2_per_gate] = imx_clk_gate2("uart2_per_gate", "uart_root", MXC_CCM_CCGR1, 12);
	clk[uart3_ipg_gate] = imx_clk_gate2("uart3_ipg_gate", "ipg", MXC_CCM_CCGR1, 14);
	clk[uart3_per_gate] = imx_clk_gate2("uart3_per_gate", "uart_root", MXC_CCM_CCGR1, 16);
	clk[i2c1_gate] = imx_clk_gate2("i2c1_gate", "per_root", MXC_CCM_CCGR1, 18);
	clk[i2c2_gate] = imx_clk_gate2("i2c2_gate", "per_root", MXC_CCM_CCGR1, 20);
	clk[pwm1_ipg_gate] = imx_clk_gate2("pwm1_ipg_gate", "ipg", MXC_CCM_CCGR2, 10);
	clk[pwm1_hf_gate] = imx_clk_gate2("pwm1_hf_gate", "per_root", MXC_CCM_CCGR2, 12);
	clk[pwm2_ipg_gate] = imx_clk_gate2("pwm2_ipg_gate", "ipg", MXC_CCM_CCGR2, 14);
	clk[pwm2_hf_gate] = imx_clk_gate2("pwm2_hf_gate", "per_root", MXC_CCM_CCGR2, 16);
	clk[gpt_ipg_gate] = imx_clk_gate2("gpt_ipg_gate", "ipg", MXC_CCM_CCGR2, 18);
	clk[gpt_hf_gate] = imx_clk_gate2("gpt_hf_gate", "per_root", MXC_CCM_CCGR2, 20);
	clk[fec_gate] = imx_clk_gate2("fec_gate", "ipg", MXC_CCM_CCGR2, 24);
	clk[usboh3_gate] = imx_clk_gate2("usboh3_gate", "ipg", MXC_CCM_CCGR2, 26);
	clk[usboh3_per_gate] = imx_clk_gate2("usboh3_per_gate", "usboh3_podf", MXC_CCM_CCGR2, 28);
	clk[esdhc1_ipg_gate] = imx_clk_gate2("esdhc1_ipg_gate", "ipg", MXC_CCM_CCGR3, 0);
	clk[esdhc2_ipg_gate] = imx_clk_gate2("esdhc2_ipg_gate", "ipg", MXC_CCM_CCGR3, 4);
	clk[esdhc3_ipg_gate] = imx_clk_gate2("esdhc3_ipg_gate", "ipg", MXC_CCM_CCGR3, 8);
	clk[esdhc4_ipg_gate] = imx_clk_gate2("esdhc4_ipg_gate", "ipg", MXC_CCM_CCGR3, 12);
	clk[ssi1_ipg_gate] = imx_clk_gate2("ssi1_ipg_gate", "ipg", MXC_CCM_CCGR3, 16);
	clk[ssi2_ipg_gate] = imx_clk_gate2("ssi2_ipg_gate", "ipg", MXC_CCM_CCGR3, 20);
	clk[ssi3_ipg_gate] = imx_clk_gate2("ssi3_ipg_gate", "ipg", MXC_CCM_CCGR3, 24);
	clk[ecspi1_ipg_gate] = imx_clk_gate2("ecspi1_ipg_gate", "ipg", MXC_CCM_CCGR4, 18);
	clk[ecspi1_per_gate] = imx_clk_gate2("ecspi1_per_gate", "ecspi_podf", MXC_CCM_CCGR4, 20);
	clk[ecspi2_ipg_gate] = imx_clk_gate2("ecspi2_ipg_gate", "ipg", MXC_CCM_CCGR4, 22);
	clk[ecspi2_per_gate] = imx_clk_gate2("ecspi2_per_gate", "ecspi_podf", MXC_CCM_CCGR4, 24);
	clk[cspi_ipg_gate] = imx_clk_gate2("cspi_ipg_gate", "ipg", MXC_CCM_CCGR4, 26);
	clk[sdma_gate] = imx_clk_gate2("sdma_gate", "ipg", MXC_CCM_CCGR4, 30);
	clk[emi_fast_gate] = imx_clk_gate2("emi_fast_gate", "dummy", MXC_CCM_CCGR5, 14);
	clk[emi_slow_gate] = imx_clk_gate2("emi_slow_gate", "emi_slow_podf", MXC_CCM_CCGR5, 16);
	clk[ipu_s] = imx_clk_mux("ipu_sel", MXC_CCM_CBCMR, 6, 2, ipu_sel, ARRAY_SIZE(ipu_sel));
	clk[ipu_gate] = imx_clk_gate2("ipu_gate", "ipu_sel", MXC_CCM_CCGR5, 10);
	clk[nfc_gate] = imx_clk_gate2("nfc_gate", "nfc_podf", MXC_CCM_CCGR5, 20);
	clk[ipu_di0_gate] = imx_clk_gate2("ipu_di0_gate", "ipu_di0_sel", MXC_CCM_CCGR6, 10);
	clk[ipu_di1_gate] = imx_clk_gate2("ipu_di1_gate", "ipu_di1_sel", MXC_CCM_CCGR6, 12);
	clk[gpu3d_s] = imx_clk_mux("gpu3d_sel", MXC_CCM_CBCMR, 4, 2, gpu3d_sel, ARRAY_SIZE(gpu3d_sel));
	clk[gpu2d_s] = imx_clk_mux("gpu2d_sel", MXC_CCM_CBCMR, 16, 2, gpu2d_sel, ARRAY_SIZE(gpu2d_sel));
	clk[gpu3d_gate] = imx_clk_gate2("gpu3d_gate", "gpu3d_sel", MXC_CCM_CCGR5, 2);
	clk[garb_gate] = imx_clk_gate2("garb_gate", "axi_a", MXC_CCM_CCGR5, 4);
	clk[gpu2d_gate] = imx_clk_gate2("gpu2d_gate", "gpu2d_sel", MXC_CCM_CCGR6, 14);
	clk[vpu_s] = imx_clk_mux("vpu_sel", MXC_CCM_CBCMR, 14, 2, vpu_sel, ARRAY_SIZE(vpu_sel));
	clk[vpu_gate] = imx_clk_gate2("vpu_gate", "vpu_sel", MXC_CCM_CCGR5, 6);
	clk[vpu_reference_gate] = imx_clk_gate2("vpu_reference_gate", "osc", MXC_CCM_CCGR5, 8);
	clk[uart4_ipg_gate] = imx_clk_gate2("uart4_ipg_gate", "ipg", MXC_CCM_CCGR7, 8);
	clk[uart4_per_gate] = imx_clk_gate2("uart4_per_gate", "uart_root", MXC_CCM_CCGR7, 10);
	clk[uart5_ipg_gate] = imx_clk_gate2("uart5_ipg_gate", "ipg", MXC_CCM_CCGR7, 12);
	clk[uart5_per_gate] = imx_clk_gate2("uart5_per_gate", "uart_root", MXC_CCM_CCGR7, 14);
	clk[gpc_dvfs] = imx_clk_gate2("gpc_dvfs", "dummy", MXC_CCM_CCGR5, 24);

	clk[ssi_apm] = imx_clk_mux("ssi_apm", MXC_CCM_CSCMR1, 8, 2, ssi_apm_sels, ARRAY_SIZE(ssi_apm_sels));
	clk[ssi1_root_sel] = imx_clk_mux("ssi1_root_sel", MXC_CCM_CSCMR1, 14, 2, ssi_clk_sels, ARRAY_SIZE(ssi_clk_sels));
	clk[ssi2_root_sel] = imx_clk_mux("ssi2_root_sel", MXC_CCM_CSCMR1, 12, 2, ssi_clk_sels, ARRAY_SIZE(ssi_clk_sels));
	clk[ssi3_root_sel] = imx_clk_mux("ssi3_root_sel", MXC_CCM_CSCMR1, 11, 1, ssi3_clk_sels, ARRAY_SIZE(ssi3_clk_sels));
	clk[ssi_ext1_sel] = imx_clk_mux("ssi_ext1_sel", MXC_CCM_CSCMR1, 28, 2, ssi_clk_sels, ARRAY_SIZE(ssi_clk_sels));
	clk[ssi_ext2_sel] = imx_clk_mux("ssi_ext2_sel", MXC_CCM_CSCMR1, 30, 2, ssi_clk_sels, ARRAY_SIZE(ssi_clk_sels));
	clk[ssi_ext1_com_sel] = imx_clk_mux("ssi_ext1_com_sel", MXC_CCM_CSCMR1, 0, 1, ssi_ext1_com_sels, ARRAY_SIZE(ssi_ext1_com_sels));
	clk[ssi_ext2_com_sel] = imx_clk_mux("ssi_ext2_com_sel", MXC_CCM_CSCMR1, 1, 1, ssi_ext2_com_sels, ARRAY_SIZE(ssi_ext2_com_sels));
	clk[ssi1_root_pred] = imx_clk_divider("ssi1_root_pred", "ssi1_root_sel", MXC_CCM_CS1CDR, 6, 3);
	clk[ssi1_root_podf] = imx_clk_divider("ssi1_root_podf", "ssi1_root_pred", MXC_CCM_CS1CDR, 0, 6);
	clk[ssi2_root_pred] = imx_clk_divider("ssi2_root_pred", "ssi2_root_sel", MXC_CCM_CS2CDR, 6, 3);
	clk[ssi2_root_podf] = imx_clk_divider("ssi2_root_podf", "ssi2_root_pred", MXC_CCM_CS2CDR, 0, 6);
	clk[ssi_ext1_pred] = imx_clk_divider("ssi_ext1_pred", "ssi_ext1_sel", MXC_CCM_CS1CDR, 22, 3);
	clk[ssi_ext1_podf] = imx_clk_divider("ssi_ext1_podf", "ssi_ext1_pred", MXC_CCM_CS1CDR, 16, 6);
	clk[ssi_ext2_pred] = imx_clk_divider("ssi_ext2_pred", "ssi_ext2_sel", MXC_CCM_CS2CDR, 22, 3);
	clk[ssi_ext2_podf] = imx_clk_divider("ssi_ext2_podf", "ssi_ext2_pred", MXC_CCM_CS2CDR, 16, 6);
	clk[ssi1_root_gate] = imx_clk_gate2("ssi1_root_gate", "ssi1_root_podf", MXC_CCM_CCGR3, 18);
	clk[ssi2_root_gate] = imx_clk_gate2("ssi2_root_gate", "ssi2_root_podf", MXC_CCM_CCGR3, 22);
	clk[ssi3_root_gate] = imx_clk_gate2("ssi3_root_gate", "ssi3_root_sel", MXC_CCM_CCGR3, 26);
	clk[ssi_ext1_gate] = imx_clk_gate2("ssi_ext1_gate", "ssi_ext1_com_sel", MXC_CCM_CCGR3, 28);
	clk[ssi_ext2_gate] = imx_clk_gate2("ssi_ext2_gate", "ssi_ext2_com_sel", MXC_CCM_CCGR3, 30);
	clk[epit1_ipg_gate] = imx_clk_gate2("epit1_ipg_gate", "ipg", MXC_CCM_CCGR2, 2);
	clk[epit1_hf_gate] = imx_clk_gate2("epit1_hf_gate", "per_root", MXC_CCM_CCGR2, 4);
	clk[epit2_ipg_gate] = imx_clk_gate2("epit2_ipg_gate", "ipg", MXC_CCM_CCGR2, 6);
	clk[epit2_hf_gate] = imx_clk_gate2("epit2_hf_gate", "per_root", MXC_CCM_CCGR2, 8);
	clk[owire_gate] = imx_clk_gate2("owire_gate", "per_root", MXC_CCM_CCGR2, 22);
	clk[srtc_gate] = imx_clk_gate2("srtc_gate", "per_root", MXC_CCM_CCGR4, 28);
	clk[pata_gate] = imx_clk_gate2("pata_gate", "ipg", MXC_CCM_CCGR4, 0);
	clk[spdif0_sel] = imx_clk_mux("spdif0_sel", MXC_CCM_CSCMR2, 0, 2, spdif_sel, ARRAY_SIZE(spdif_sel));
	clk[spdif0_pred] = imx_clk_divider("spdif0_pred", "spdif0_sel", MXC_CCM_CDCDR, 25, 3);
	clk[spdif0_podf] = imx_clk_divider("spdif0_podf", "spdif0_pred", MXC_CCM_CDCDR, 19, 6);
	clk[spdif0_com_s] = imx_clk_mux_flags("spdif0_com_sel", MXC_CCM_CSCMR2, 4, 1,
				spdif0_com_sel, ARRAY_SIZE(spdif0_com_sel), CLK_SET_RATE_PARENT);
	clk[spdif0_gate] = imx_clk_gate2("spdif0_gate", "spdif0_com_sel", MXC_CCM_CCGR5, 26);
	clk[spdif_ipg_gate] = imx_clk_gate2("spdif_ipg_gate", "ipg", MXC_CCM_CCGR5, 30);

	for (i = 0; i < ARRAY_SIZE(clk); i++)
		if (IS_ERR(clk[i]))
			pr_err("i.MX5 clk %d: register failed with %ld\n",
				i, PTR_ERR(clk[i]));

	clk_register_clkdev(clk[gpt_hf_gate], "per", "imx-gpt.0");
	clk_register_clkdev(clk[gpt_ipg_gate], "ipg", "imx-gpt.0");
	clk_register_clkdev(clk[uart1_per_gate], "per", "imx21-uart.0");
	clk_register_clkdev(clk[uart1_ipg_gate], "ipg", "imx21-uart.0");
	clk_register_clkdev(clk[uart2_per_gate], "per", "imx21-uart.1");
	clk_register_clkdev(clk[uart2_ipg_gate], "ipg", "imx21-uart.1");
	clk_register_clkdev(clk[uart3_per_gate], "per", "imx21-uart.2");
	clk_register_clkdev(clk[uart3_ipg_gate], "ipg", "imx21-uart.2");
	clk_register_clkdev(clk[uart4_per_gate], "per", "imx21-uart.3");
	clk_register_clkdev(clk[uart4_ipg_gate], "ipg", "imx21-uart.3");
	clk_register_clkdev(clk[uart5_per_gate], "per", "imx21-uart.4");
	clk_register_clkdev(clk[uart5_ipg_gate], "ipg", "imx21-uart.4");
	clk_register_clkdev(clk[ecspi1_per_gate], "per", "imx51-ecspi.0");
	clk_register_clkdev(clk[ecspi1_ipg_gate], "ipg", "imx51-ecspi.0");
	clk_register_clkdev(clk[ecspi2_per_gate], "per", "imx51-ecspi.1");
	clk_register_clkdev(clk[ecspi2_ipg_gate], "ipg", "imx51-ecspi.1");
	clk_register_clkdev(clk[cspi_ipg_gate], NULL, "imx35-cspi.2");
	clk_register_clkdev(clk[pwm1_ipg_gate], "pwm", "mxc_pwm.0");
	clk_register_clkdev(clk[pwm2_ipg_gate], "pwm", "mxc_pwm.1");
	clk_register_clkdev(clk[i2c1_gate], NULL, "imx21-i2c.0");
	clk_register_clkdev(clk[i2c2_gate], NULL, "imx21-i2c.1");
	clk_register_clkdev(clk[usboh3_per_gate], "per", "mxc-ehci.0");
	clk_register_clkdev(clk[usboh3_gate], "ipg", "mxc-ehci.0");
	clk_register_clkdev(clk[usboh3_gate], "ahb", "mxc-ehci.0");
	clk_register_clkdev(clk[usboh3_per_gate], "per", "mxc-ehci.1");
	clk_register_clkdev(clk[usboh3_gate], "ipg", "mxc-ehci.1");
	clk_register_clkdev(clk[usboh3_gate], "ahb", "mxc-ehci.1");
	clk_register_clkdev(clk[usboh3_per_gate], "per", "mxc-ehci.2");
	clk_register_clkdev(clk[usboh3_gate], "ipg", "mxc-ehci.2");
	clk_register_clkdev(clk[usboh3_gate], "ahb", "mxc-ehci.2");
	clk_register_clkdev(clk[usboh3_per_gate], "per", "imx-udc-mx51");
	clk_register_clkdev(clk[usboh3_gate], "ipg", "imx-udc-mx51");
	clk_register_clkdev(clk[usboh3_gate], "ahb", "imx-udc-mx51");
	clk_register_clkdev(clk[nfc_gate], NULL, "imx51-nand");
	clk_register_clkdev(clk[ssi1_ipg_gate], NULL, "imx-ssi.0");
	clk_register_clkdev(clk[ssi2_ipg_gate], NULL, "imx-ssi.1");
	clk_register_clkdev(clk[ssi3_ipg_gate], NULL, "imx-ssi.2");
	clk_register_clkdev(clk[sdma_gate], NULL, "imx35-sdma");
	clk_register_clkdev(clk[cpu_podf], NULL, "cpu0");
	clk_register_clkdev(clk[iim_gate], "iim", NULL);
	clk_register_clkdev(clk[dummy], NULL, "imx2-wdt.0");
	clk_register_clkdev(clk[dummy], NULL, "imx2-wdt.1");
	clk_register_clkdev(clk[dummy], NULL, "imx-keypad");
	clk_register_clkdev(clk[ipu_di1_gate], "di1", "imx-tve.0");
	clk_register_clkdev(clk[gpc_dvfs], "gpc_dvfs", NULL);
	clk_register_clkdev(clk[epit1_ipg_gate], "ipg", "imx-epit.0");
	clk_register_clkdev(clk[epit1_hf_gate], "per", "imx-epit.0");
	clk_register_clkdev(clk[epit2_ipg_gate], "ipg", "imx-epit.1");
	clk_register_clkdev(clk[epit2_hf_gate], "per", "imx-epit.1");

	/* Set SDHC parents to be PLL2 */
	clk_set_parent(clk[esdhc_a_sel], clk[pll2_sw]);
	clk_set_parent(clk[esdhc_b_sel], clk[pll2_sw]);

	/* move usb phy clk to 24MHz */
	clk_set_parent(clk[usb_phy_sel], clk[osc]);

	clk_prepare_enable(clk[gpc_dvfs]);
	clk_prepare_enable(clk[ahb_max]); /* esdhc3 */
	clk_prepare_enable(clk[aips_tz1]);
	clk_prepare_enable(clk[aips_tz2]); /* fec */
	clk_prepare_enable(clk[spba]);
	clk_prepare_enable(clk[emi_fast_gate]); /* fec */
	clk_prepare_enable(clk[emi_slow_gate]); /* eim */
	clk_prepare_enable(clk[mipi_hsc1_gate]);
	clk_prepare_enable(clk[mipi_hsc2_gate]);
	clk_prepare_enable(clk[mipi_esc_gate]);
	clk_prepare_enable(clk[mipi_hsp_gate]);
	clk_prepare_enable(clk[tmax1]);
	clk_prepare_enable(clk[tmax2]); /* esdhc2, fec */
	clk_prepare_enable(clk[tmax3]); /* esdhc1, esdhc4 */
}

int __init mx51_clocks_init(unsigned long rate_ckil, unsigned long rate_osc,
			unsigned long rate_ckih1, unsigned long rate_ckih2)
{
	int i;
	u32 val;
	struct device_node *np;

	clk[pll1_sw] = imx_clk_pllv2("pll1_sw", "osc", MX51_DPLL1_BASE);
	clk[pll2_sw] = imx_clk_pllv2("pll2_sw", "osc", MX51_DPLL2_BASE);
	clk[pll3_sw] = imx_clk_pllv2("pll3_sw", "osc", MX51_DPLL3_BASE);
	clk[ipu_di0_sel] = imx_clk_mux("ipu_di0_sel", MXC_CCM_CSCMR2, 26, 3,
				mx51_ipu_di0_sel, ARRAY_SIZE(mx51_ipu_di0_sel));
	clk[ipu_di1_sel] = imx_clk_mux("ipu_di1_sel", MXC_CCM_CSCMR2, 29, 3,
				mx51_ipu_di1_sel, ARRAY_SIZE(mx51_ipu_di1_sel));
	clk[tve_ext_sel] = imx_clk_mux_flags("tve_ext_sel", MXC_CCM_CSCMR1, 6, 1,
				mx51_tve_ext_sel, ARRAY_SIZE(mx51_tve_ext_sel), CLK_SET_RATE_PARENT);
	clk[tve_s] = imx_clk_mux("tve_sel", MXC_CCM_CSCMR1, 7, 1,
				mx51_tve_sel, ARRAY_SIZE(mx51_tve_sel));
	clk[tve_gate] = imx_clk_gate2("tve_gate", "tve_sel", MXC_CCM_CCGR2, 30);
	clk[tve_pred] = imx_clk_divider("tve_pred", "pll3_sw", MXC_CCM_CDCDR, 28, 3);
	clk[esdhc1_per_gate] = imx_clk_gate2("esdhc1_per_gate", "esdhc_a_podf", MXC_CCM_CCGR3, 2);
	clk[esdhc2_per_gate] = imx_clk_gate2("esdhc2_per_gate", "esdhc_b_podf", MXC_CCM_CCGR3, 6);
	clk[esdhc3_per_gate] = imx_clk_gate2("esdhc3_per_gate", "esdhc_c_sel", MXC_CCM_CCGR3, 10);
	clk[esdhc4_per_gate] = imx_clk_gate2("esdhc4_per_gate", "esdhc_d_sel", MXC_CCM_CCGR3, 14);
	clk[usb_phy_gate] = imx_clk_gate2("usb_phy_gate", "usb_phy_sel", MXC_CCM_CCGR2, 0);
	clk[hsi2c_gate] = imx_clk_gate2("hsi2c_gate", "ipg", MXC_CCM_CCGR1, 22);
	clk[mipi_hsc1_gate] = imx_clk_gate2("mipi_hsc1_gate", "ipg", MXC_CCM_CCGR4, 6);
	clk[mipi_hsc2_gate] = imx_clk_gate2("mipi_hsc2_gate", "ipg", MXC_CCM_CCGR4, 8);
	clk[mipi_esc_gate] = imx_clk_gate2("mipi_esc_gate", "ipg", MXC_CCM_CCGR4, 10);
	clk[mipi_hsp_gate] = imx_clk_gate2("mipi_hsp_gate", "ipg", MXC_CCM_CCGR4, 12);
	clk[spdif_xtal_sel] = imx_clk_mux("spdif_xtal_sel", MXC_CCM_CSCMR1, 2, 2,
				mx51_spdif_xtal_sel, ARRAY_SIZE(mx51_spdif_xtal_sel));
	clk[spdif1_sel] = imx_clk_mux("spdif1_sel", MXC_CCM_CSCMR2, 2, 2,
				spdif_sel, ARRAY_SIZE(spdif_sel));
	clk[spdif1_pred] = imx_clk_divider("spdif1_pred", "spdif1_sel", MXC_CCM_CDCDR, 16, 3);
	clk[spdif1_podf] = imx_clk_divider("spdif1_podf", "spdif1_pred", MXC_CCM_CDCDR, 9, 6);
	clk[spdif1_com_sel] = imx_clk_mux("spdif1_com_sel", MXC_CCM_CSCMR2, 5, 1,
				mx51_spdif1_com_sel, ARRAY_SIZE(mx51_spdif1_com_sel));
	clk[spdif1_gate] = imx_clk_gate2("spdif1_gate", "spdif1_com_sel", MXC_CCM_CCGR5, 28);

	for (i = 0; i < ARRAY_SIZE(clk); i++)
		if (IS_ERR(clk[i]))
			pr_err("i.MX51 clk %d: register failed with %ld\n",
				i, PTR_ERR(clk[i]));

	np = of_find_compatible_node(NULL, NULL, "fsl,imx51-ccm");
	clk_data.clks = clk;
	clk_data.clk_num = ARRAY_SIZE(clk);
	of_clk_add_provider(np, of_clk_src_onecell_get, &clk_data);

	mx5_clocks_common_init(rate_ckil, rate_osc, rate_ckih1, rate_ckih2);

	clk_register_clkdev(clk[hsi2c_gate], NULL, "imx21-i2c.2");
	clk_register_clkdev(clk[mx51_mipi], "mipi_hsp", NULL);
	clk_register_clkdev(clk[vpu_gate], NULL, "imx51-vpu.0");
	clk_register_clkdev(clk[fec_gate], NULL, "imx27-fec.0");
	clk_register_clkdev(clk[usb_phy_gate], "phy", "mxc-ehci.0");
	clk_register_clkdev(clk[esdhc1_ipg_gate], "ipg", "sdhci-esdhc-imx51.0");
	clk_register_clkdev(clk[dummy], "ahb", "sdhci-esdhc-imx51.0");
	clk_register_clkdev(clk[esdhc1_per_gate], "per", "sdhci-esdhc-imx51.0");
	clk_register_clkdev(clk[esdhc2_ipg_gate], "ipg", "sdhci-esdhc-imx51.1");
	clk_register_clkdev(clk[dummy], "ahb", "sdhci-esdhc-imx51.1");
	clk_register_clkdev(clk[esdhc2_per_gate], "per", "sdhci-esdhc-imx51.1");
	clk_register_clkdev(clk[esdhc3_ipg_gate], "ipg", "sdhci-esdhc-imx51.2");
	clk_register_clkdev(clk[dummy], "ahb", "sdhci-esdhc-imx51.2");
	clk_register_clkdev(clk[esdhc3_per_gate], "per", "sdhci-esdhc-imx51.2");
	clk_register_clkdev(clk[esdhc4_ipg_gate], "ipg", "sdhci-esdhc-imx51.3");
	clk_register_clkdev(clk[dummy], "ahb", "sdhci-esdhc-imx51.3");
	clk_register_clkdev(clk[esdhc4_per_gate], "per", "sdhci-esdhc-imx51.3");

	/* set the usboh3 parent to pll2_sw */
	clk_set_parent(clk[usboh3_sel], clk[pll2_sw]);

	/* set SDHC root clock to 166.25MHZ*/
	clk_set_rate(clk[esdhc_a_podf], 166250000);
	clk_set_rate(clk[esdhc_b_podf], 166250000);

	/* System timer */
	mxc_timer_init(MX51_IO_ADDRESS(MX51_GPT1_BASE_ADDR), MX51_INT_GPT);

	clk_prepare_enable(clk[iim_gate]);
	imx_print_silicon_rev("i.MX51", mx51_revision());
	clk_disable_unprepare(clk[iim_gate]);

	/*
	 * Reference Manual says: Functionality of CCDR[18] and CLPCR[23] is no
	 * longer supported. Set to one for better power saving.
	 *
	 * The effect of not setting these bits is that MIPI clocks can't be
	 * enabled without the IPU clock being enabled aswell.
	 */
	val = readl(MXC_CCM_CCDR);
	val |= 1 << 18;
	writel(val, MXC_CCM_CCDR);

	val = readl(MXC_CCM_CLPCR);
	val |= 1 << 23;
	writel(val, MXC_CCM_CLPCR);

	return 0;
}

static void __init mx51_clocks_init_dt(struct device_node *np)
{
	mx51_clocks_init(0, 0, 0, 0);
}
CLK_OF_DECLARE(imx51_ccm, "fsl,imx51-ccm", mx51_clocks_init_dt);

static void __init mx53_clocks_init(struct device_node *np)
{
	int i, irq;
	unsigned long r;
	void __iomem *base;

	clk[pll1_sw] = imx_clk_pllv2("pll1_sw", "osc", MX53_DPLL1_BASE);
	clk[pll2_sw] = imx_clk_pllv2("pll2_sw", "osc", MX53_DPLL2_BASE);
	clk[pll3_sw] = imx_clk_pllv2("pll3_sw", "osc", MX53_DPLL3_BASE);
	clk[pll4_sw] = imx_clk_pllv2("pll4_sw", "osc", MX53_DPLL4_BASE);

	clk[ldb_di1_div_3_5] = imx_clk_fixed_factor("ldb_di1_div_3_5", "ldb_di1_sel", 2, 7);
	clk[ldb_di1_div] = imx_clk_divider_flags("ldb_di1_div", "ldb_di1_div_3_5", MXC_CCM_CSCMR2, 11, 1, 0);
	clk[ldb_di1_sel] = imx_clk_mux_flags("ldb_di1_sel", MXC_CCM_CSCMR2, 9, 1,
				mx53_ldb_di1_sel, ARRAY_SIZE(mx53_ldb_di1_sel), CLK_SET_RATE_PARENT);
	clk[di_pll4_podf] = imx_clk_divider("di_pll4_podf", "pll4_sw", MXC_CCM_CDCDR, 16, 3);
	clk[ldb_di0_div_3_5] = imx_clk_fixed_factor("ldb_di0_div_3_5", "ldb_di0_sel", 2, 7);
	clk[ldb_di0_div] = imx_clk_divider_flags("ldb_di0_div", "ldb_di0_div_3_5", MXC_CCM_CSCMR2, 10, 1, 0);
	clk[ldb_di0_sel] = imx_clk_mux_flags("ldb_di0_sel", MXC_CCM_CSCMR2, 8, 1,
				mx53_ldb_di0_sel, ARRAY_SIZE(mx53_ldb_di0_sel), CLK_SET_RATE_PARENT);
	clk[ldb_di0_gate] = imx_clk_gate2("ldb_di0_gate", "ldb_di0_div", MXC_CCM_CCGR6, 28);
	clk[ldb_di1_gate] = imx_clk_gate2("ldb_di1_gate", "ldb_di1_div", MXC_CCM_CCGR6, 30);
	clk[ipu_di0_sel] = imx_clk_mux("ipu_di0_sel", MXC_CCM_CSCMR2, 26, 3,
				mx53_ipu_di0_sel, ARRAY_SIZE(mx53_ipu_di0_sel));
	clk[ipu_di1_sel] = imx_clk_mux("ipu_di1_sel", MXC_CCM_CSCMR2, 29, 3,
				mx53_ipu_di1_sel, ARRAY_SIZE(mx53_ipu_di1_sel));
	clk[tve_ext_sel] = imx_clk_mux_flags("tve_ext_sel", MXC_CCM_CSCMR1, 6, 1,
				mx53_tve_ext_sel, ARRAY_SIZE(mx53_tve_ext_sel), CLK_SET_RATE_PARENT);
	clk[tve_gate] = imx_clk_gate2("tve_gate", "tve_pred", MXC_CCM_CCGR2, 30);
	clk[tve_pred] = imx_clk_divider("tve_pred", "tve_ext_sel", MXC_CCM_CDCDR, 28, 3);
	clk[esdhc1_per_gate] = imx_clk_gate2("esdhc1_per_gate", "esdhc_a_podf", MXC_CCM_CCGR3, 2);
	clk[esdhc2_per_gate] = imx_clk_gate2("esdhc2_per_gate", "esdhc_c_sel", MXC_CCM_CCGR3, 6);
	clk[esdhc3_per_gate] = imx_clk_gate2("esdhc3_per_gate", "esdhc_b_podf", MXC_CCM_CCGR3, 10);
	clk[esdhc4_per_gate] = imx_clk_gate2("esdhc4_per_gate", "esdhc_d_sel", MXC_CCM_CCGR3, 14);
	clk[usb_phy1_gate] = imx_clk_gate2("usb_phy1_gate", "usb_phy_sel", MXC_CCM_CCGR4, 10);
	clk[usb_phy2_gate] = imx_clk_gate2("usb_phy2_gate", "usb_phy_sel", MXC_CCM_CCGR4, 12);
	clk[can_sel] = imx_clk_mux("can_sel", MXC_CCM_CSCMR2, 6, 2,
				mx53_can_sel, ARRAY_SIZE(mx53_can_sel));
	clk[can1_serial_gate] = imx_clk_gate2("can1_serial_gate", "can_sel", MXC_CCM_CCGR6, 22);
	clk[can1_ipg_gate] = imx_clk_gate2("can1_ipg_gate", "ipg", MXC_CCM_CCGR6, 20);
	clk[ocram] = imx_clk_gate2("ocram", "ahb", MXC_CCM_CCGR6, 2);
	clk[can2_serial_gate] = imx_clk_gate2("can2_serial_gate", "can_sel", MXC_CCM_CCGR4, 8);
	clk[can2_ipg_gate] = imx_clk_gate2("can2_ipg_gate", "ipg", MXC_CCM_CCGR4, 6);
	clk[i2c3_gate] = imx_clk_gate2("i2c3_gate", "per_root", MXC_CCM_CCGR1, 22);
	clk[sata_gate] = imx_clk_gate2("sata_gate", "ipg", MXC_CCM_CCGR4, 2);

	clk[cko1_sel] = imx_clk_mux("cko1_sel", MXC_CCM_CCOSR, 0, 4,
				mx53_cko1_sel, ARRAY_SIZE(mx53_cko1_sel));
	clk[cko1_podf] = imx_clk_divider("cko1_podf", "cko1_sel", MXC_CCM_CCOSR, 4, 3);
	clk[cko1] = imx_clk_gate2("cko1", "cko1_podf", MXC_CCM_CCOSR, 7);

	clk[cko2_sel] = imx_clk_mux("cko2_sel", MXC_CCM_CCOSR, 16, 5,
				mx53_cko2_sel, ARRAY_SIZE(mx53_cko2_sel));
	clk[cko2_podf] = imx_clk_divider("cko2_podf", "cko2_sel", MXC_CCM_CCOSR, 21, 3);
	clk[cko2] = imx_clk_gate2("cko2", "cko2_podf", MXC_CCM_CCOSR, 24);
	clk[spdif_xtal_sel] = imx_clk_mux("spdif_xtal_sel", MXC_CCM_CSCMR1, 2, 2,
				mx53_spdif_xtal_sel, ARRAY_SIZE(mx53_spdif_xtal_sel));

	for (i = 0; i < ARRAY_SIZE(clk); i++)
		if (IS_ERR(clk[i]))
			pr_err("i.MX53 clk %d: register failed with %ld\n",
				i, PTR_ERR(clk[i]));

	clk_data.clks = clk;
	clk_data.clk_num = ARRAY_SIZE(clk);
	of_clk_add_provider(np, of_clk_src_onecell_get, &clk_data);

	mx5_clocks_common_init(0, 0, 0, 0);

	clk_register_clkdev(clk[vpu_gate], NULL, "imx53-vpu.0");
	clk_register_clkdev(clk[i2c3_gate], NULL, "imx21-i2c.2");
	clk_register_clkdev(clk[fec_gate], NULL, "imx25-fec.0");
	clk_register_clkdev(clk[usb_phy1_gate], "usb_phy1", "mxc-ehci.0");
	clk_register_clkdev(clk[esdhc1_ipg_gate], "ipg", "sdhci-esdhc-imx53.0");
	clk_register_clkdev(clk[dummy], "ahb", "sdhci-esdhc-imx53.0");
	clk_register_clkdev(clk[esdhc1_per_gate], "per", "sdhci-esdhc-imx53.0");
	clk_register_clkdev(clk[esdhc2_ipg_gate], "ipg", "sdhci-esdhc-imx53.1");
	clk_register_clkdev(clk[dummy], "ahb", "sdhci-esdhc-imx53.1");
	clk_register_clkdev(clk[esdhc2_per_gate], "per", "sdhci-esdhc-imx53.1");
	clk_register_clkdev(clk[esdhc3_ipg_gate], "ipg", "sdhci-esdhc-imx53.2");
	clk_register_clkdev(clk[dummy], "ahb", "sdhci-esdhc-imx53.2");
	clk_register_clkdev(clk[esdhc3_per_gate], "per", "sdhci-esdhc-imx53.2");
	clk_register_clkdev(clk[esdhc4_ipg_gate], "ipg", "sdhci-esdhc-imx53.3");
	clk_register_clkdev(clk[dummy], "ahb", "sdhci-esdhc-imx53.3");
	clk_register_clkdev(clk[esdhc4_per_gate], "per", "sdhci-esdhc-imx53.3");

	/* set SDHC root clock to 200MHZ*/
	clk_set_rate(clk[esdhc_a_podf], 200000000);
	clk_set_rate(clk[esdhc_b_podf], 200000000);

	clk_prepare_enable(clk[iim_gate]);
	imx_print_silicon_rev("i.MX53", mx53_revision());
	clk_disable_unprepare(clk[iim_gate]);

	r = clk_round_rate(clk[usboh3_per_gate], 54000000);
	clk_set_rate(clk[usboh3_per_gate], r);

	np = of_find_compatible_node(NULL, NULL, "fsl,imx53-gpt");
	base = of_iomap(np, 0);
	WARN_ON(!base);
	irq = irq_of_parse_and_map(np, 0);
	mxc_timer_init(base, irq);
}
CLK_OF_DECLARE(imx53_ccm, "fsl,imx53-ccm", mx53_clocks_init);
