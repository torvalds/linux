/*
 * Copyright (C) 2014-2015 Freescale Semiconductor, Inc.
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <dt-bindings/clock/imx7d-clock.h>
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/types.h>

#include "clk.h"

static struct clk *clks[IMX7D_CLK_END];
static const char *arm_a7_sel[] = { "osc", "pll_arm_main_clk",
	"pll_enet_500m_clk", "pll_dram_main_clk",
	"pll_sys_main_clk", "pll_sys_pfd0_392m_clk", "pll_audio_main_clk",
	"pll_usb_main_clk", };

static const char *arm_m4_sel[] = { "osc", "pll_sys_main_240m_clk",
	"pll_enet_250m_clk", "pll_sys_pfd2_270m_clk",
	"pll_dram_533m_clk", "pll_audio_main_clk", "pll_video_main_clk",
	"pll_usb_main_clk", };

static const char *arm_m0_sel[] = { "osc", "pll_sys_main_120m_clk",
	"pll_enet_125m_clk", "pll_sys_pfd2_135m_clk",
	"pll_dram_533m_clk", "pll_audio_main_clk", "pll_video_main_clk",
	"pll_usb_main_clk", };

static const char *axi_sel[] = { "osc", "pll_sys_pfd1_332m_clk",
	"pll_dram_533m_clk", "pll_enet_250m_clk", "pll_sys_pfd5_clk",
	"pll_audio_main_clk", "pll_video_main_clk", "pll_sys_pfd7_clk", };

static const char *disp_axi_sel[] = { "osc", "pll_sys_pfd1_332m_clk",
	"pll_dram_533m_clk", "pll_enet_250m_clk", "pll_sys_pfd6_clk",
	"pll_sys_pfd7_clk", "pll_audio_main_clk", "pll_video_main_clk", };

static const char *enet_axi_sel[] = { "osc", "pll_sys_pfd2_270m_clk",
	"pll_dram_533m_clk", "pll_enet_250m_clk",
	"pll_sys_main_240m_clk", "pll_audio_main_clk", "pll_video_main_clk",
	"pll_sys_pfd4_clk", };

static const char *nand_usdhc_bus_sel[] = { "osc", "pll_sys_pfd2_270m_clk",
	"pll_dram_533m_clk", "pll_sys_main_240m_clk",
	"pll_sys_pfd2_135m_clk", "pll_sys_pfd6_clk", "pll_enet_250m_clk",
	"pll_audio_main_clk", };

static const char *ahb_channel_sel[] = { "osc", "pll_sys_pfd2_135m_clk",
	"pll_dram_533m_clk", "pll_sys_pfd0_392m_clk",
	"pll_enet_125m_clk", "pll_usb_main_clk", "pll_audio_main_clk",
	"pll_video_main_clk", };

static const char *dram_phym_sel[] = { "pll_dram_main_clk",
	"dram_phym_alt_clk", };

static const char *dram_sel[] = { "pll_dram_main_clk",
	"dram_alt_clk", };

static const char *dram_phym_alt_sel[] = { "osc", "pll_dram_533m_clk",
	"pll_sys_main_clk", "pll_enet_500m_clk",
	"pll_usb_main_clk", "pll_sys_pfd7_clk", "pll_audio_main_clk",
	"pll_video_main_clk", };

static const char *dram_alt_sel[] = { "osc", "pll_dram_533m_clk",
	"pll_sys_main_clk", "pll_enet_500m_clk",
	"pll_enet_250m_clk", "pll_sys_pfd0_392m_clk",
	"pll_audio_main_clk", "pll_sys_pfd2_270m_clk", };

static const char *usb_hsic_sel[] = { "osc", "pll_sys_main_clk",
	"pll_usb_main_clk", "pll_sys_pfd3_clk", "pll_sys_pfd4_clk",
	"pll_sys_pfd5_clk", "pll_sys_pfd6_clk", "pll_sys_pfd7_clk", };

static const char *pcie_ctrl_sel[] = { "osc", "pll_enet_250m_clk",
	"pll_sys_main_240m_clk", "pll_sys_pfd2_270m_clk",
	"pll_dram_533m_clk", "pll_enet_500m_clk",
	"pll_sys_pfd1_332m_clk", "pll_sys_pfd6_clk", };

static const char *pcie_phy_sel[] = { "osc", "pll_enet_100m_clk",
	"pll_enet_500m_clk", "ext_clk_1", "ext_clk_2", "ext_clk_3",
	"ext_clk_4", "pll_sys_pfd0_392m_clk", };

static const char *epdc_pixel_sel[] = { "osc", "pll_sys_pfd1_332m_clk",
	"pll_dram_533m_clk", "pll_sys_main_clk", "pll_sys_pfd5_clk",
	"pll_sys_pfd6_clk", "pll_sys_pfd7_clk", "pll_video_main_clk", };

static const char *lcdif_pixel_sel[] = { "osc", "pll_sys_pfd5_clk",
	"pll_dram_533m_clk", "ext_clk_3", "pll_sys_pfd4_clk",
	"pll_sys_pfd2_270m_clk", "pll_video_main_clk",
	"pll_usb_main_clk", };

static const char *mipi_dsi_sel[] = { "osc", "pll_sys_pfd5_clk",
	"pll_sys_pfd3_clk", "pll_sys_main_clk", "pll_sys_pfd0_196m_clk",
	"pll_dram_533m_clk", "pll_video_main_clk", "pll_audio_main_clk", };

static const char *mipi_csi_sel[] = { "osc", "pll_sys_pfd4_clk",
	"pll_sys_pfd3_clk", "pll_sys_main_clk", "pll_sys_pfd0_196m_clk",
	"pll_dram_533m_clk", "pll_video_main_clk", "pll_audio_main_clk", };

static const char *mipi_dphy_sel[] = { "osc", "pll_sys_main_120m_clk",
	"pll_dram_533m_clk", "pll_sys_pfd5_clk", "ref_1m_clk", "ext_clk_2",
	"pll_video_main_clk", "ext_clk_3", };

static const char *sai1_sel[] = { "osc", "pll_sys_pfd2_135m_clk",
	"pll_audio_main_clk", "pll_dram_533m_clk", "pll_video_main_clk",
	"pll_sys_pfd4_clk", "pll_enet_125m_clk", "ext_clk_2", };

static const char *sai2_sel[] = { "osc", "pll_sys_pfd2_135m_clk",
	"pll_audio_main_clk", "pll_dram_533m_clk", "pll_video_main_clk",
	"pll_sys_pfd4_clk", "pll_enet_125m_clk", "ext_clk_2", };

static const char *sai3_sel[] = { "osc", "pll_sys_pfd2_135m_clk",
	"pll_audio_main_clk", "pll_dram_533m_clk", "pll_video_main_clk",
	"pll_sys_pfd4_clk", "pll_enet_125m_clk", "ext_clk_3", };

static const char *spdif_sel[] = { "osc", "pll_sys_pfd2_135m_clk",
	"pll_audio_main_clk", "pll_dram_533m_clk", "pll_video_main_clk",
	"pll_sys_pfd4_clk", "pll_enet_125m_clk", "ext_3_clk", };

static const char *enet1_ref_sel[] = { "osc", "pll_enet_125m_clk",
	"pll_enet_50m_clk", "pll_enet_25m_clk",
	"pll_sys_main_120m_clk", "pll_audio_main_clk", "pll_video_main_clk",
	"ext_clk_4", };

static const char *enet1_time_sel[] = { "osc", "pll_enet_100m_clk",
	"pll_audio_main_clk", "ext_clk_1", "ext_clk_2", "ext_clk_3",
	"ext_clk_4", "pll_video_main_clk", };

static const char *enet2_ref_sel[] = { "osc", "pll_enet_125m_clk",
	"pll_enet_50m_clk", "pll_enet_25m_clk",
	"pll_sys_main_120m_clk", "pll_audio_main_clk", "pll_video_main_clk",
	"ext_clk_4", };

static const char *enet2_time_sel[] = { "osc", "pll_enet_100m_clk",
	"pll_audio_main_clk", "ext_clk_1", "ext_clk_2", "ext_clk_3",
	"ext_clk_4", "pll_video_main_clk", };

static const char *enet_phy_ref_sel[] = { "osc", "pll_enet_25m_clk",
	"pll_enet_50m_clk", "pll_enet_125m_clk",
	"pll_dram_533m_clk", "pll_audio_main_clk", "pll_video_main_clk",
	"pll_sys_pfd3_clk", };

static const char *eim_sel[] = { "osc", "pll_sys_pfd2_135m_clk",
	"pll_sys_main_120m_clk", "pll_dram_533m_clk",
	"pll_sys_pfd2_270m_clk", "pll_sys_pfd3_clk", "pll_enet_125m_clk",
	"pll_usb_main_clk", };

static const char *nand_sel[] = { "osc", "pll_sys_main_clk",
	"pll_dram_533m_clk", "pll_sys_pfd0_392m_clk", "pll_sys_pfd3_clk",
	"pll_enet_500m_clk", "pll_enet_250m_clk",
	"pll_video_main_clk", };

static const char *qspi_sel[] = { "osc", "pll_sys_pfd4_clk",
	"pll_dram_533m_clk", "pll_enet_500m_clk", "pll_sys_pfd3_clk",
	"pll_sys_pfd2_270m_clk", "pll_sys_pfd6_clk", "pll_sys_pfd7_clk", };

static const char *usdhc1_sel[] = { "osc", "pll_sys_pfd0_392m_clk",
	"pll_dram_533m_clk", "pll_enet_500m_clk", "pll_sys_pfd4_clk",
	"pll_sys_pfd2_270m_clk", "pll_sys_pfd6_clk", "pll_sys_pfd7_clk", };

static const char *usdhc2_sel[] = { "osc", "pll_sys_pfd0_392m_clk",
	"pll_dram_533m_clk", "pll_enet_500m_clk", "pll_sys_pfd4_clk",
	"pll_sys_pfd2_270m_clk", "pll_sys_pfd6_clk", "pll_sys_pfd7_clk", };

static const char *usdhc3_sel[] = { "osc", "pll_sys_pfd0_392m_clk",
	"pll_dram_533m_clk", "pll_enet_500m_clk", "pll_sys_pfd4_clk",
	"pll_sys_pfd2_270m_clk", "pll_sys_pfd6_clk", "pll_sys_pfd7_clk", };

static const char *can1_sel[] = { "osc", "pll_sys_main_120m_clk",
	"pll_dram_533m_clk", "pll_sys_main_clk",
	"pll_enet_40m_clk", "pll_usb_main_clk", "ext_clk_1",
	"ext_clk_4", };

static const char *can2_sel[] = { "osc", "pll_sys_main_120m_clk",
	"pll_dram_533m_clk", "pll_sys_main_clk",
	"pll_enet_40m_clk", "pll_usb_main_clk", "ext_clk_1",
	"ext_clk_3", };

static const char *i2c1_sel[] = { "osc", "pll_sys_main_120m_clk",
	"pll_enet_50m_clk", "pll_dram_533m_clk",
	"pll_audio_main_clk", "pll_video_main_clk", "pll_usb_main_clk",
	"pll_sys_pfd2_135m_clk", };

static const char *i2c2_sel[] = { "osc", "pll_sys_main_120m_clk",
	"pll_enet_50m_clk", "pll_dram_533m_clk",
	"pll_audio_main_clk", "pll_video_main_clk", "pll_usb_main_clk",
	"pll_sys_pfd2_135m_clk", };

static const char *i2c3_sel[] = { "osc", "pll_sys_main_120m_clk",
	"pll_enet_50m_clk", "pll_dram_533m_clk",
	"pll_audio_main_clk", "pll_video_main_clk", "pll_usb_main_clk",
	"pll_sys_pfd2_135m_clk", };

static const char *i2c4_sel[] = { "osc", "pll_sys_main_120m_clk",
	"pll_enet_50m_clk", "pll_dram_533m_clk",
	"pll_audio_main_clk", "pll_video_main_clk", "pll_usb_main_clk",
	"pll_sys_pfd2_135m_clk", };

static const char *uart1_sel[] = { "osc", "pll_sys_main_240m_clk",
	"pll_enet_40m_clk", "pll_enet_100m_clk",
	"pll_sys_main_clk", "ext_clk_2", "ext_clk_4",
	"pll_usb_main_clk", };

static const char *uart2_sel[] = { "osc", "pll_sys_main_240m_clk",
	"pll_enet_40m_clk", "pll_enet_100m_clk",
	"pll_sys_main_clk", "ext_clk_2", "ext_clk_3",
	"pll_usb_main_clk", };

static const char *uart3_sel[] = { "osc", "pll_sys_main_240m_clk",
	"pll_enet_40m_clk", "pll_enet_100m_clk",
	"pll_sys_main_clk", "ext_clk_2", "ext_clk_4",
	"pll_usb_main_clk", };

static const char *uart4_sel[] = { "osc", "pll_sys_main_240m_clk",
	"pll_enet_40m_clk", "pll_enet_100m_clk",
	"pll_sys_main_clk", "ext_clk_2", "ext_clk_3",
	"pll_usb_main_clk", };

static const char *uart5_sel[] = { "osc", "pll_sys_main_240m_clk",
	"pll_enet_40m_clk", "pll_enet_100m_clk",
	"pll_sys_main_clk", "ext_clk_2", "ext_clk_4",
	"pll_usb_main_clk", };

static const char *uart6_sel[] = { "osc", "pll_sys_main_240m_clk",
	"pll_enet_40m_clk", "pll_enet_100m_clk",
	"pll_sys_main_clk", "ext_clk_2", "ext_clk_3",
	"pll_usb_main_clk", };

static const char *uart7_sel[] = { "osc", "pll_sys_main_240m_clk",
	"pll_enet_40m_clk", "pll_enet_100m_clk",
	"pll_sys_main_clk", "ext_clk_2", "ext_clk_4",
	"pll_usb_main_clk", };

static const char *ecspi1_sel[] = { "osc", "pll_sys_main_240m_clk",
	"pll_enet_40m_clk", "pll_sys_main_120m_clk",
	"pll_sys_main_clk", "pll_sys_pfd4_clk", "pll_enet_250m_clk",
	"pll_usb_main_clk", };

static const char *ecspi2_sel[] = { "osc", "pll_sys_main_240m_clk",
	"pll_enet_40m_clk", "pll_sys_main_120m_clk",
	"pll_sys_main_clk", "pll_sys_pfd4_clk", "pll_enet_250m_clk",
	"pll_usb_main_clk", };

static const char *ecspi3_sel[] = { "osc", "pll_sys_main_240m_clk",
	"pll_enet_40m_clk", "pll_sys_main_120m_clk",
	"pll_sys_main_clk", "pll_sys_pfd4_clk", "pll_enet_250m_clk",
	"pll_usb_main_clk", };

static const char *ecspi4_sel[] = { "osc", "pll_sys_main_240m_clk",
	"pll_enet_40m_clk", "pll_sys_main_120m_clk",
	"pll_sys_main_clk", "pll_sys_pfd4_clk", "pll_enet_250m_clk",
	"pll_usb_main_clk", };

static const char *pwm1_sel[] = { "osc", "pll_enet_100m_clk",
	"pll_sys_main_120m_clk", "pll_enet_40m_clk", "pll_audio_main_clk",
	"ext_clk_1", "ref_1m_clk", "pll_video_main_clk", };

static const char *pwm2_sel[] = { "osc", "pll_enet_100m_clk",
	"pll_sys_main_120m_clk", "pll_enet_40m_clk", "pll_audio_main_clk",
	"ext_clk_1", "ref_1m_clk", "pll_video_main_clk", };

static const char *pwm3_sel[] = { "osc", "pll_enet_100m_clk",
	"pll_sys_main_120m_clk", "pll_enet_40m_clk", "pll_audio_main_clk",
	"ext_clk_2", "ref_1m_clk", "pll_video_main_clk", };

static const char *pwm4_sel[] = { "osc", "pll_enet_100m_clk",
	"pll_sys_main_120m_clk", "pll_enet_40m_clk", "pll_audio_main_clk",
	"ext_clk_2", "ref_1m_clk", "pll_video_main_clk", };

static const char *flextimer1_sel[] = { "osc", "pll_enet_100m_clk",
	"pll_sys_main_120m_clk", "pll_enet_40m_clk", "pll_audio_main_clk",
	"ext_clk_3", "ref_1m_clk", "pll_video_main_clk", };

static const char *flextimer2_sel[] = { "osc", "pll_enet_100m_clk",
	"pll_sys_main_120m_clk", "pll_enet_40m_clk", "pll_audio_main_clk",
	"ext_clk_3", "ref_1m_clk", "pll_video_main_clk", };

static const char *sim1_sel[] = { "osc", "pll_sys_pfd2_135m_clk",
	"pll_sys_main_120m_clk", "pll_dram_533m_clk",
	"pll_usb_main_clk", "pll_audio_main_clk", "pll_enet_125m_clk",
	"pll_sys_pfd7_clk", };

static const char *sim2_sel[] = { "osc", "pll_sys_pfd2_135m_clk",
	"pll_sys_main_120m_clk", "pll_dram_533m_clk",
	"pll_usb_main_clk", "pll_video_main_clk", "pll_enet_125m_clk",
	"pll_sys_pfd7_clk", };

static const char *gpt1_sel[] = { "osc", "pll_enet_100m_clk",
	"pll_sys_pfd0_392m_clk", "pll_enet_40m_clk", "pll_video_main_clk",
	"ref_1m_clk", "pll_audio_main_clk", "ext_clk_1", };

static const char *gpt2_sel[] = { "osc", "pll_enet_100m_clk",
	"pll_sys_pfd0_392m_clk", "pll_enet_40m_clk", "pll_video_main_clk",
	"ref_1m_clk", "pll_audio_main_clk", "ext_clk_2", };

static const char *gpt3_sel[] = { "osc", "pll_enet_100m_clk",
	"pll_sys_pfd0_392m_clk", "pll_enet_40m_clk", "pll_video_main_clk",
	"ref_1m_clk", "pll_audio_main_clk", "ext_clk_3", };

static const char *gpt4_sel[] = { "osc", "pll_enet_100m_clk",
	"pll_sys_pfd0_392m_clk", "pll_enet_40m_clk", "pll_video_main_clk",
	"ref_1m_clk", "pll_audio_main_clk", "ext_clk_4", };

static const char *trace_sel[] = { "osc", "pll_sys_pfd2_135m_clk",
	"pll_sys_main_120m_clk", "pll_dram_533m_clk",
	"pll_enet_125m_clk", "pll_usb_main_clk", "ext_clk_2",
	"ext_clk_3", };

static const char *wdog_sel[] = { "osc", "pll_sys_pfd2_135m_clk",
	"pll_sys_main_120m_clk", "pll_dram_533m_clk",
	"pll_enet_125m_clk", "pll_usb_main_clk", "ref_1m_clk",
	"pll_sys_pfd1_166m_clk", };

static const char *csi_mclk_sel[] = { "osc", "pll_sys_pfd2_135m_clk",
	"pll_sys_main_120m_clk", "pll_dram_533m_clk",
	"pll_enet_125m_clk", "pll_audio_main_clk", "pll_video_main_clk",
	"pll_usb_main_clk", };

static const char *audio_mclk_sel[] = { "osc", "pll_sys_pfd2_135m_clk",
	"pll_sys_main_120m_clk", "pll_dram_533m_clk",
	"pll_enet_125m_clk", "pll_audio_main_clk", "pll_video_main_clk",
	"pll_usb_main_clk", };

static const char *wrclk_sel[] = { "osc", "pll_enet_40m_clk",
	"pll_dram_533m_clk", "pll_usb_main_clk",
	"pll_sys_main_240m_clk", "pll_sys_pfd2_270m_clk",
	"pll_enet_500m_clk", "pll_sys_pfd7_clk", };

static const char *clko1_sel[] = { "osc", "pll_sys_main_clk",
	"pll_sys_main_240m_clk", "pll_sys_pfd0_196m_clk", "pll_sys_pfd3_clk",
	"pll_enet_500m_clk", "pll_dram_533m_clk", "ref_1m_clk", };

static const char *clko2_sel[] = { "osc", "pll_sys_main_240m_clk",
	"pll_sys_pfd0_392m_clk", "pll_sys_pfd1_166m_clk", "pll_sys_pfd4_clk",
	"pll_audio_main_clk", "pll_video_main_clk", "osc_32k_clk", };

static const char *lvds1_sel[] = { "pll_arm_main_clk",
	"pll_sys_main_clk", "pll_sys_pfd0_392m_clk", "pll_sys_pfd1_332m_clk",
	"pll_sys_pfd2_270m_clk", "pll_sys_pfd3_clk", "pll_sys_pfd4_clk",
	"pll_sys_pfd5_clk", "pll_sys_pfd6_clk", "pll_sys_pfd7_clk",
	"pll_audio_main_clk", "pll_video_main_clk", "pll_enet_500m_clk",
	"pll_enet_250m_clk", "pll_enet_125m_clk", "pll_enet_100m_clk",
	"pll_enet_50m_clk", "pll_enet_40m_clk", "pll_enet_25m_clk",
	"pll_dram_main_clk", };

static const char *pll_bypass_src_sel[] = { "osc", "dummy", };
static const char *pll_arm_bypass_sel[] = { "pll_arm_main", "pll_arm_main_src", };
static const char *pll_dram_bypass_sel[] = { "pll_dram_main", "pll_dram_main_src", };
static const char *pll_sys_bypass_sel[] = { "pll_sys_main", "pll_sys_main_src", };
static const char *pll_enet_bypass_sel[] = { "pll_enet_main", "pll_enet_main_src", };
static const char *pll_audio_bypass_sel[] = { "pll_audio_main", "pll_audio_main_src", };
static const char *pll_video_bypass_sel[] = { "pll_video_main", "pll_video_main_src", };

static struct clk_onecell_data clk_data;

static struct clk ** const uart_clks[] __initconst = {
	&clks[IMX7D_UART1_ROOT_CLK],
	&clks[IMX7D_UART2_ROOT_CLK],
	&clks[IMX7D_UART3_ROOT_CLK],
	&clks[IMX7D_UART4_ROOT_CLK],
	&clks[IMX7D_UART5_ROOT_CLK],
	&clks[IMX7D_UART6_ROOT_CLK],
	&clks[IMX7D_UART7_ROOT_CLK],
	NULL
};

static void __init imx7d_clocks_init(struct device_node *ccm_node)
{
	struct device_node *np;
	void __iomem *base;
	int i;

	clks[IMX7D_CLK_DUMMY] = imx_clk_fixed("dummy", 0);
	clks[IMX7D_OSC_24M_CLK] = of_clk_get_by_name(ccm_node, "osc");

	np = of_find_compatible_node(NULL, NULL, "fsl,imx7d-anatop");
	base = of_iomap(np, 0);
	WARN_ON(!base);

	clks[IMX7D_PLL_ARM_MAIN_SRC]  = imx_clk_mux("pll_arm_main_src", base + 0x60, 14, 2, pll_bypass_src_sel, ARRAY_SIZE(pll_bypass_src_sel));
	clks[IMX7D_PLL_DRAM_MAIN_SRC] = imx_clk_mux("pll_dram_main_src", base + 0x70, 14, 2, pll_bypass_src_sel, ARRAY_SIZE(pll_bypass_src_sel));
	clks[IMX7D_PLL_SYS_MAIN_SRC]  = imx_clk_mux("pll_sys_main_src", base + 0xb0, 14, 2, pll_bypass_src_sel, ARRAY_SIZE(pll_bypass_src_sel));
	clks[IMX7D_PLL_ENET_MAIN_SRC] = imx_clk_mux("pll_enet_main_src", base + 0xe0, 14, 2, pll_bypass_src_sel, ARRAY_SIZE(pll_bypass_src_sel));
	clks[IMX7D_PLL_AUDIO_MAIN_SRC] = imx_clk_mux("pll_audio_main_src", base + 0xf0, 14, 2, pll_bypass_src_sel, ARRAY_SIZE(pll_bypass_src_sel));
	clks[IMX7D_PLL_VIDEO_MAIN_SRC] = imx_clk_mux("pll_video_main_src", base + 0x130, 14, 2, pll_bypass_src_sel, ARRAY_SIZE(pll_bypass_src_sel));

	clks[IMX7D_PLL_ARM_MAIN]  = imx_clk_pllv3(IMX_PLLV3_SYS, "pll_arm_main", "pll_arm_main_src", base + 0x60, 0x7f);
	clks[IMX7D_PLL_DRAM_MAIN] = imx_clk_pllv3(IMX_PLLV3_SYS, "pll_dram_main", "pll_dram_main_src", base + 0x70, 0x7f);
	clks[IMX7D_PLL_SYS_MAIN]  = imx_clk_pllv3(IMX_PLLV3_GENERIC, "pll_sys_main", "pll_sys_main_src", base + 0xb0, 0x1);
	clks[IMX7D_PLL_ENET_MAIN] = imx_clk_pllv3(IMX_PLLV3_ENET_IMX7, "pll_enet_main", "pll_enet_main_src", base + 0xe0, 0x0);
	clks[IMX7D_PLL_AUDIO_MAIN] = imx_clk_pllv3(IMX_PLLV3_AV, "pll_audio_main", "pll_audio_main_src", base + 0xf0, 0x7f);
	clks[IMX7D_PLL_VIDEO_MAIN] = imx_clk_pllv3(IMX_PLLV3_AV, "pll_video_main", "pll_video_main_src", base + 0x130, 0x7f);

	clks[IMX7D_PLL_ARM_MAIN_BYPASS]  = imx_clk_mux_flags("pll_arm_main_bypass", base + 0x60, 16, 1, pll_arm_bypass_sel, ARRAY_SIZE(pll_arm_bypass_sel), CLK_SET_RATE_PARENT);
	clks[IMX7D_PLL_DRAM_MAIN_BYPASS] = imx_clk_mux_flags("pll_dram_main_bypass", base + 0x70, 16, 1, pll_dram_bypass_sel, ARRAY_SIZE(pll_dram_bypass_sel), CLK_SET_RATE_PARENT);
	clks[IMX7D_PLL_SYS_MAIN_BYPASS]  = imx_clk_mux_flags("pll_sys_main_bypass", base + 0xb0, 16, 1, pll_sys_bypass_sel, ARRAY_SIZE(pll_sys_bypass_sel), CLK_SET_RATE_PARENT);
	clks[IMX7D_PLL_ENET_MAIN_BYPASS] = imx_clk_mux_flags("pll_enet_main_bypass", base + 0xe0, 16, 1, pll_enet_bypass_sel, ARRAY_SIZE(pll_enet_bypass_sel), CLK_SET_RATE_PARENT);
	clks[IMX7D_PLL_AUDIO_MAIN_BYPASS] = imx_clk_mux_flags("pll_audio_main_bypass", base + 0xf0, 16, 1, pll_audio_bypass_sel, ARRAY_SIZE(pll_audio_bypass_sel), CLK_SET_RATE_PARENT);
	clks[IMX7D_PLL_VIDEO_MAIN_BYPASS] = imx_clk_mux_flags("pll_video_main_bypass", base + 0x130, 16, 1, pll_video_bypass_sel, ARRAY_SIZE(pll_video_bypass_sel), CLK_SET_RATE_PARENT);

	clk_set_parent(clks[IMX7D_PLL_ARM_MAIN_BYPASS], clks[IMX7D_PLL_ARM_MAIN]);
	clk_set_parent(clks[IMX7D_PLL_DRAM_MAIN_BYPASS], clks[IMX7D_PLL_DRAM_MAIN]);
	clk_set_parent(clks[IMX7D_PLL_SYS_MAIN_BYPASS], clks[IMX7D_PLL_SYS_MAIN]);
	clk_set_parent(clks[IMX7D_PLL_ENET_MAIN_BYPASS], clks[IMX7D_PLL_ENET_MAIN]);
	clk_set_parent(clks[IMX7D_PLL_AUDIO_MAIN_BYPASS], clks[IMX7D_PLL_AUDIO_MAIN]);
	clk_set_parent(clks[IMX7D_PLL_VIDEO_MAIN_BYPASS], clks[IMX7D_PLL_VIDEO_MAIN]);

	clks[IMX7D_PLL_ARM_MAIN_CLK] = imx_clk_gate("pll_arm_main_clk", "pll_arm_main_bypass", base + 0x60, 13);
	clks[IMX7D_PLL_DRAM_MAIN_CLK] = imx_clk_gate("pll_dram_main_clk", "pll_dram_main_bypass", base + 0x70, 13);
	clks[IMX7D_PLL_SYS_MAIN_CLK] = imx_clk_gate("pll_sys_main_clk", "pll_sys_main_bypass", base + 0xb0, 13);
	clks[IMX7D_PLL_AUDIO_MAIN_CLK] = imx_clk_gate("pll_audio_main_clk", "pll_audio_main_bypass", base + 0xf0, 13);
	clks[IMX7D_PLL_VIDEO_MAIN_CLK] = imx_clk_gate("pll_video_main_clk", "pll_video_main_bypass", base + 0x130, 13);

	clks[IMX7D_PLL_SYS_PFD0_392M_CLK] = imx_clk_pfd("pll_sys_pfd0_392m_clk", "pll_sys_main_clk", base + 0xc0, 0);
	clks[IMX7D_PLL_SYS_PFD1_332M_CLK] = imx_clk_pfd("pll_sys_pfd1_332m_clk", "pll_sys_main_clk", base + 0xc0, 1);
	clks[IMX7D_PLL_SYS_PFD2_270M_CLK] = imx_clk_pfd("pll_sys_pfd2_270m_clk", "pll_sys_main_clk", base + 0xc0, 2);

	clks[IMX7D_PLL_SYS_PFD3_CLK] = imx_clk_pfd("pll_sys_pfd3_clk", "pll_sys_main_clk", base + 0xc0, 3);
	clks[IMX7D_PLL_SYS_PFD4_CLK] = imx_clk_pfd("pll_sys_pfd4_clk", "pll_sys_main_clk", base + 0xd0, 0);
	clks[IMX7D_PLL_SYS_PFD5_CLK] = imx_clk_pfd("pll_sys_pfd5_clk", "pll_sys_main_clk", base + 0xd0, 1);
	clks[IMX7D_PLL_SYS_PFD6_CLK] = imx_clk_pfd("pll_sys_pfd6_clk", "pll_sys_main_clk", base + 0xd0, 2);
	clks[IMX7D_PLL_SYS_PFD7_CLK] = imx_clk_pfd("pll_sys_pfd7_clk", "pll_sys_main_clk", base + 0xd0, 3);

	clks[IMX7D_PLL_SYS_MAIN_480M] = imx_clk_fixed_factor("pll_sys_main_480m", "pll_sys_main_clk", 1, 1);
	clks[IMX7D_PLL_SYS_MAIN_240M] = imx_clk_fixed_factor("pll_sys_main_240m", "pll_sys_main_clk", 1, 2);
	clks[IMX7D_PLL_SYS_MAIN_120M] = imx_clk_fixed_factor("pll_sys_main_120m", "pll_sys_main_clk", 1, 4);
	clks[IMX7D_PLL_DRAM_MAIN_533M] = imx_clk_fixed_factor("pll_dram_533m", "pll_dram_main_clk", 1, 2);

	clks[IMX7D_PLL_SYS_MAIN_480M_CLK] = imx_clk_gate_dis("pll_sys_main_480m_clk", "pll_sys_main_480m", base + 0xb0, 4);
	clks[IMX7D_PLL_SYS_MAIN_240M_CLK] = imx_clk_gate_dis("pll_sys_main_240m_clk", "pll_sys_main_240m", base + 0xb0, 5);
	clks[IMX7D_PLL_SYS_MAIN_120M_CLK] = imx_clk_gate_dis("pll_sys_main_120m_clk", "pll_sys_main_120m", base + 0xb0, 6);
	clks[IMX7D_PLL_DRAM_MAIN_533M_CLK] = imx_clk_gate("pll_dram_533m_clk", "pll_dram_533m", base + 0x70, 12);

	clks[IMX7D_PLL_SYS_PFD0_196M] = imx_clk_fixed_factor("pll_sys_pfd0_196m", "pll_sys_pfd0_392m_clk", 1, 2);
	clks[IMX7D_PLL_SYS_PFD1_166M] = imx_clk_fixed_factor("pll_sys_pfd1_166m", "pll_sys_pfd1_332m_clk", 1, 2);
	clks[IMX7D_PLL_SYS_PFD2_135M] = imx_clk_fixed_factor("pll_sys_pfd2_135m", "pll_sys_pfd2_270m_clk", 1, 2);

	clks[IMX7D_PLL_SYS_PFD0_196M_CLK] = imx_clk_gate_dis("pll_sys_pfd0_196m_clk", "pll_sys_pfd0_196m", base + 0xb0, 26);
	clks[IMX7D_PLL_SYS_PFD1_166M_CLK] = imx_clk_gate_dis("pll_sys_pfd1_166m_clk", "pll_sys_pfd1_166m", base + 0xb0, 27);
	clks[IMX7D_PLL_SYS_PFD2_135M_CLK] = imx_clk_gate_dis("pll_sys_pfd2_135m_clk", "pll_sys_pfd2_135m", base + 0xb0, 28);

	clks[IMX7D_PLL_ENET_MAIN_CLK] = imx_clk_fixed_factor("pll_enet_main_clk", "pll_enet_main_bypass", 1, 1);
	clks[IMX7D_PLL_ENET_MAIN_500M] = imx_clk_fixed_factor("pll_enet_500m", "pll_enet_main_clk", 1, 2);
	clks[IMX7D_PLL_ENET_MAIN_250M] = imx_clk_fixed_factor("pll_enet_250m", "pll_enet_main_clk", 1, 4);
	clks[IMX7D_PLL_ENET_MAIN_125M] = imx_clk_fixed_factor("pll_enet_125m", "pll_enet_main_clk", 1, 8);
	clks[IMX7D_PLL_ENET_MAIN_100M] = imx_clk_fixed_factor("pll_enet_100m", "pll_enet_main_clk", 1, 10);
	clks[IMX7D_PLL_ENET_MAIN_50M] = imx_clk_fixed_factor("pll_enet_50m", "pll_enet_main_clk", 1, 20);
	clks[IMX7D_PLL_ENET_MAIN_40M] = imx_clk_fixed_factor("pll_enet_40m", "pll_enet_main_clk", 1, 25);
	clks[IMX7D_PLL_ENET_MAIN_25M] = imx_clk_fixed_factor("pll_enet_25m", "pll_enet_main_clk", 1, 40);

	clks[IMX7D_PLL_ENET_MAIN_500M_CLK] = imx_clk_gate("pll_enet_500m_clk", "pll_enet_500m", base + 0xe0, 12);
	clks[IMX7D_PLL_ENET_MAIN_250M_CLK] = imx_clk_gate("pll_enet_250m_clk", "pll_enet_250m", base + 0xe0, 11);
	clks[IMX7D_PLL_ENET_MAIN_125M_CLK] = imx_clk_gate("pll_enet_125m_clk", "pll_enet_125m", base + 0xe0, 10);
	clks[IMX7D_PLL_ENET_MAIN_100M_CLK] = imx_clk_gate("pll_enet_100m_clk", "pll_enet_100m", base + 0xe0, 9);
	clks[IMX7D_PLL_ENET_MAIN_50M_CLK]  = imx_clk_gate("pll_enet_50m_clk", "pll_enet_50m", base + 0xe0, 8);
	clks[IMX7D_PLL_ENET_MAIN_40M_CLK]  = imx_clk_gate("pll_enet_40m_clk", "pll_enet_40m", base + 0xe0, 7);
	clks[IMX7D_PLL_ENET_MAIN_25M_CLK]  = imx_clk_gate("pll_enet_25m_clk", "pll_enet_25m", base + 0xe0, 6);

	clks[IMX7D_LVDS1_OUT_SEL] = imx_clk_mux("lvds1_sel", base + 0x170, 0, 5, lvds1_sel, ARRAY_SIZE(lvds1_sel));
	clks[IMX7D_LVDS1_OUT_CLK] = imx_clk_gate_exclusive("lvds1_out", "lvds1_sel", base + 0x170, 5, BIT(6));

	np = ccm_node;
	base = of_iomap(np, 0);
	WARN_ON(!base);

	clks[IMX7D_ARM_A7_ROOT_SRC] = imx_clk_mux("arm_a7_src", base + 0x8000, 24, 3, arm_a7_sel, ARRAY_SIZE(arm_a7_sel));
	clks[IMX7D_ARM_M4_ROOT_SRC] = imx_clk_mux("arm_m4_src", base + 0x8080, 24, 3, arm_m4_sel, ARRAY_SIZE(arm_m4_sel));
	clks[IMX7D_ARM_M0_ROOT_SRC] = imx_clk_mux("arm_m0_src", base + 0x8100, 24, 3, arm_m0_sel, ARRAY_SIZE(arm_m0_sel));
	clks[IMX7D_MAIN_AXI_ROOT_SRC] = imx_clk_mux("axi_src", base + 0x8800, 24, 3, axi_sel, ARRAY_SIZE(axi_sel));
	clks[IMX7D_DISP_AXI_ROOT_SRC] = imx_clk_mux("disp_axi_src", base + 0x8880, 24, 3, disp_axi_sel, ARRAY_SIZE(disp_axi_sel));
	clks[IMX7D_ENET_AXI_ROOT_SRC] = imx_clk_mux("enet_axi_src", base + 0x8900, 24, 3, enet_axi_sel, ARRAY_SIZE(enet_axi_sel));
	clks[IMX7D_NAND_USDHC_BUS_ROOT_SRC] = imx_clk_mux("nand_usdhc_src", base + 0x8980, 24, 3, nand_usdhc_bus_sel, ARRAY_SIZE(nand_usdhc_bus_sel));
	clks[IMX7D_AHB_CHANNEL_ROOT_SRC] = imx_clk_mux("ahb_src", base + 0x9000, 24, 3, ahb_channel_sel, ARRAY_SIZE(ahb_channel_sel));
	clks[IMX7D_DRAM_PHYM_ROOT_SRC] = imx_clk_mux("dram_phym_src", base + 0x9800, 24, 1, dram_phym_sel, ARRAY_SIZE(dram_phym_sel));
	clks[IMX7D_DRAM_ROOT_SRC] = imx_clk_mux("dram_src", base + 0x9880, 24, 1, dram_sel, ARRAY_SIZE(dram_sel));
	clks[IMX7D_DRAM_PHYM_ALT_ROOT_SRC] = imx_clk_mux("dram_phym_alt_src", base + 0xa000, 24, 3, dram_phym_alt_sel, ARRAY_SIZE(dram_phym_alt_sel));
	clks[IMX7D_DRAM_ALT_ROOT_SRC]  = imx_clk_mux("dram_alt_src", base + 0xa080, 24, 3, dram_alt_sel, ARRAY_SIZE(dram_alt_sel));
	clks[IMX7D_USB_HSIC_ROOT_SRC] = imx_clk_mux("usb_hsic_src", base + 0xa100, 24, 3, usb_hsic_sel, ARRAY_SIZE(usb_hsic_sel));
	clks[IMX7D_PCIE_CTRL_ROOT_SRC] = imx_clk_mux("pcie_ctrl_src", base + 0xa180, 24, 3, pcie_ctrl_sel, ARRAY_SIZE(pcie_ctrl_sel));
	clks[IMX7D_PCIE_PHY_ROOT_SRC] = imx_clk_mux("pcie_phy_src", base + 0xa200, 24, 3, pcie_phy_sel, ARRAY_SIZE(pcie_phy_sel));
	clks[IMX7D_EPDC_PIXEL_ROOT_SRC] = imx_clk_mux("epdc_pixel_src", base + 0xa280, 24, 3, epdc_pixel_sel, ARRAY_SIZE(epdc_pixel_sel));
	clks[IMX7D_LCDIF_PIXEL_ROOT_SRC] = imx_clk_mux("lcdif_pixel_src", base + 0xa300, 24, 3, lcdif_pixel_sel, ARRAY_SIZE(lcdif_pixel_sel));
	clks[IMX7D_MIPI_DSI_ROOT_SRC] = imx_clk_mux("mipi_dsi_src", base + 0xa380, 24, 3,  mipi_dsi_sel, ARRAY_SIZE(mipi_dsi_sel));
	clks[IMX7D_MIPI_CSI_ROOT_SRC] = imx_clk_mux("mipi_csi_src", base + 0xa400, 24, 3, mipi_csi_sel, ARRAY_SIZE(mipi_csi_sel));
	clks[IMX7D_MIPI_DPHY_ROOT_SRC] = imx_clk_mux("mipi_dphy_src", base + 0xa480, 24, 3, mipi_dphy_sel, ARRAY_SIZE(mipi_dphy_sel));
	clks[IMX7D_SAI1_ROOT_SRC] = imx_clk_mux("sai1_src", base + 0xa500, 24, 3, sai1_sel, ARRAY_SIZE(sai1_sel));
	clks[IMX7D_SAI2_ROOT_SRC] = imx_clk_mux("sai2_src", base + 0xa580, 24, 3, sai2_sel, ARRAY_SIZE(sai2_sel));
	clks[IMX7D_SAI3_ROOT_SRC] = imx_clk_mux("sai3_src", base + 0xa600, 24, 3, sai3_sel, ARRAY_SIZE(sai3_sel));
	clks[IMX7D_SPDIF_ROOT_SRC] = imx_clk_mux("spdif_src", base + 0xa680, 24, 3, spdif_sel, ARRAY_SIZE(spdif_sel));
	clks[IMX7D_ENET1_REF_ROOT_SRC] = imx_clk_mux("enet1_ref_src", base + 0xa700, 24, 3, enet1_ref_sel, ARRAY_SIZE(enet1_ref_sel));
	clks[IMX7D_ENET1_TIME_ROOT_SRC] = imx_clk_mux("enet1_time_src", base + 0xa780, 24, 3, enet1_time_sel, ARRAY_SIZE(enet1_time_sel));
	clks[IMX7D_ENET2_REF_ROOT_SRC] = imx_clk_mux("enet2_ref_src", base + 0xa800, 24, 3, enet2_ref_sel, ARRAY_SIZE(enet2_ref_sel));
	clks[IMX7D_ENET2_TIME_ROOT_SRC] = imx_clk_mux("enet2_time_src", base + 0xa880, 24, 3, enet2_time_sel, ARRAY_SIZE(enet2_time_sel));
	clks[IMX7D_ENET_PHY_REF_ROOT_SRC] = imx_clk_mux("enet_phy_ref_src", base + 0xa900, 24, 3, enet_phy_ref_sel, ARRAY_SIZE(enet_phy_ref_sel));
	clks[IMX7D_EIM_ROOT_SRC] = imx_clk_mux("eim_src", base + 0xa980, 24, 3, eim_sel, ARRAY_SIZE(eim_sel));
	clks[IMX7D_NAND_ROOT_SRC] = imx_clk_mux("nand_src", base + 0xaa00, 24, 3, nand_sel, ARRAY_SIZE(nand_sel));
	clks[IMX7D_QSPI_ROOT_SRC] = imx_clk_mux("qspi_src", base + 0xaa80, 24, 3, qspi_sel, ARRAY_SIZE(qspi_sel));
	clks[IMX7D_USDHC1_ROOT_SRC] = imx_clk_mux("usdhc1_src", base + 0xab00, 24, 3, usdhc1_sel, ARRAY_SIZE(usdhc1_sel));
	clks[IMX7D_USDHC2_ROOT_SRC] = imx_clk_mux("usdhc2_src", base + 0xab80, 24, 3, usdhc2_sel, ARRAY_SIZE(usdhc2_sel));
	clks[IMX7D_USDHC3_ROOT_SRC] = imx_clk_mux("usdhc3_src", base + 0xac00, 24, 3, usdhc3_sel, ARRAY_SIZE(usdhc3_sel));
	clks[IMX7D_CAN1_ROOT_SRC] = imx_clk_mux("can1_src", base + 0xac80, 24, 3, can1_sel, ARRAY_SIZE(can1_sel));
	clks[IMX7D_CAN2_ROOT_SRC] = imx_clk_mux("can2_src", base + 0xad00, 24, 3, can2_sel, ARRAY_SIZE(can2_sel));
	clks[IMX7D_I2C1_ROOT_SRC] = imx_clk_mux("i2c1_src", base + 0xad80, 24, 3, i2c1_sel, ARRAY_SIZE(i2c1_sel));
	clks[IMX7D_I2C2_ROOT_SRC] = imx_clk_mux("i2c2_src", base + 0xae00, 24, 3, i2c2_sel, ARRAY_SIZE(i2c2_sel));
	clks[IMX7D_I2C3_ROOT_SRC] = imx_clk_mux("i2c3_src", base + 0xae80, 24, 3, i2c3_sel, ARRAY_SIZE(i2c3_sel));
	clks[IMX7D_I2C4_ROOT_SRC] = imx_clk_mux("i2c4_src", base + 0xaf00, 24, 3, i2c4_sel, ARRAY_SIZE(i2c4_sel));
	clks[IMX7D_UART1_ROOT_SRC] = imx_clk_mux("uart1_src", base + 0xaf80, 24, 3, uart1_sel, ARRAY_SIZE(uart1_sel));
	clks[IMX7D_UART2_ROOT_SRC] = imx_clk_mux("uart2_src", base + 0xb000, 24, 3, uart2_sel, ARRAY_SIZE(uart2_sel));
	clks[IMX7D_UART3_ROOT_SRC] = imx_clk_mux("uart3_src", base + 0xb080, 24, 3, uart3_sel, ARRAY_SIZE(uart3_sel));
	clks[IMX7D_UART4_ROOT_SRC] = imx_clk_mux("uart4_src", base + 0xb100, 24, 3, uart4_sel, ARRAY_SIZE(uart4_sel));
	clks[IMX7D_UART5_ROOT_SRC] = imx_clk_mux("uart5_src", base + 0xb180, 24, 3, uart5_sel, ARRAY_SIZE(uart5_sel));
	clks[IMX7D_UART6_ROOT_SRC] = imx_clk_mux("uart6_src", base + 0xb200, 24, 3, uart6_sel, ARRAY_SIZE(uart6_sel));
	clks[IMX7D_UART7_ROOT_SRC] = imx_clk_mux("uart7_src", base + 0xb280, 24, 3, uart7_sel, ARRAY_SIZE(uart7_sel));
	clks[IMX7D_ECSPI1_ROOT_SRC] = imx_clk_mux("ecspi1_src", base + 0xb300, 24, 3, ecspi1_sel, ARRAY_SIZE(ecspi1_sel));
	clks[IMX7D_ECSPI2_ROOT_SRC] = imx_clk_mux("ecspi2_src", base + 0xb380, 24, 3, ecspi2_sel, ARRAY_SIZE(ecspi2_sel));
	clks[IMX7D_ECSPI3_ROOT_SRC] = imx_clk_mux("ecspi3_src", base + 0xb400, 24, 3, ecspi3_sel, ARRAY_SIZE(ecspi3_sel));
	clks[IMX7D_ECSPI4_ROOT_SRC] = imx_clk_mux("ecspi4_src", base + 0xb480, 24, 3, ecspi4_sel, ARRAY_SIZE(ecspi4_sel));
	clks[IMX7D_PWM1_ROOT_SRC] = imx_clk_mux("pwm1_src", base + 0xb500, 24, 3, pwm1_sel, ARRAY_SIZE(pwm1_sel));
	clks[IMX7D_PWM2_ROOT_SRC] = imx_clk_mux("pwm2_src", base + 0xb580, 24, 3, pwm2_sel, ARRAY_SIZE(pwm2_sel));
	clks[IMX7D_PWM3_ROOT_SRC] = imx_clk_mux("pwm3_src", base + 0xb600, 24, 3, pwm3_sel, ARRAY_SIZE(pwm3_sel));
	clks[IMX7D_PWM4_ROOT_SRC] = imx_clk_mux("pwm4_src", base + 0xb680, 24, 3, pwm4_sel, ARRAY_SIZE(pwm4_sel));
	clks[IMX7D_FLEXTIMER1_ROOT_SRC] = imx_clk_mux("flextimer1_src", base + 0xb700, 24, 3, flextimer1_sel, ARRAY_SIZE(flextimer1_sel));
	clks[IMX7D_FLEXTIMER2_ROOT_SRC] = imx_clk_mux("flextimer2_src", base + 0xb780, 24, 3, flextimer2_sel, ARRAY_SIZE(flextimer2_sel));
	clks[IMX7D_SIM1_ROOT_SRC] = imx_clk_mux("sim1_src", base + 0xb800, 24, 3, sim1_sel, ARRAY_SIZE(sim1_sel));
	clks[IMX7D_SIM2_ROOT_SRC] = imx_clk_mux("sim2_src", base + 0xb880, 24, 3, sim2_sel, ARRAY_SIZE(sim2_sel));
	clks[IMX7D_GPT1_ROOT_SRC] = imx_clk_mux("gpt1_src", base + 0xb900, 24, 3, gpt1_sel, ARRAY_SIZE(gpt1_sel));
	clks[IMX7D_GPT2_ROOT_SRC] = imx_clk_mux("gpt2_src", base + 0xb980, 24, 3, gpt2_sel, ARRAY_SIZE(gpt2_sel));
	clks[IMX7D_GPT3_ROOT_SRC] = imx_clk_mux("gpt3_src", base + 0xba00, 24, 3, gpt3_sel, ARRAY_SIZE(gpt3_sel));
	clks[IMX7D_GPT4_ROOT_SRC] = imx_clk_mux("gpt4_src", base + 0xba80, 24, 3, gpt4_sel, ARRAY_SIZE(gpt4_sel));
	clks[IMX7D_TRACE_ROOT_SRC] = imx_clk_mux("trace_src", base + 0xbb00, 24, 3, trace_sel, ARRAY_SIZE(trace_sel));
	clks[IMX7D_WDOG_ROOT_SRC] = imx_clk_mux("wdog_src", base + 0xbb80, 24, 3, wdog_sel, ARRAY_SIZE(wdog_sel));
	clks[IMX7D_CSI_MCLK_ROOT_SRC] = imx_clk_mux("csi_mclk_src", base + 0xbc00, 24, 3, csi_mclk_sel, ARRAY_SIZE(csi_mclk_sel));
	clks[IMX7D_AUDIO_MCLK_ROOT_SRC] = imx_clk_mux("audio_mclk_src", base + 0xbc80, 24, 3, audio_mclk_sel, ARRAY_SIZE(audio_mclk_sel));
	clks[IMX7D_WRCLK_ROOT_SRC] = imx_clk_mux("wrclk_src", base + 0xbd00, 24, 3, wrclk_sel, ARRAY_SIZE(wrclk_sel));
	clks[IMX7D_CLKO1_ROOT_SRC] = imx_clk_mux("clko1_src", base + 0xbd80, 24, 3, clko1_sel, ARRAY_SIZE(clko1_sel));
	clks[IMX7D_CLKO2_ROOT_SRC] = imx_clk_mux("clko2_src", base + 0xbe00, 24, 3, clko2_sel, ARRAY_SIZE(clko2_sel));

	clks[IMX7D_ARM_A7_ROOT_CG] = imx_clk_gate("arm_a7_cg", "arm_a7_src", base + 0x8000, 28);
	clks[IMX7D_ARM_M4_ROOT_CG] = imx_clk_gate("arm_m4_cg", "arm_m4_src", base + 0x8080, 28);
	clks[IMX7D_ARM_M0_ROOT_CG] = imx_clk_gate("arm_m0_cg", "arm_m0_src", base + 0x8100, 28);
	clks[IMX7D_MAIN_AXI_ROOT_CG] = imx_clk_gate("axi_cg", "axi_src", base + 0x8800, 28);
	clks[IMX7D_DISP_AXI_ROOT_CG] = imx_clk_gate("disp_axi_cg", "disp_axi_src", base + 0x8880, 28);
	clks[IMX7D_ENET_AXI_ROOT_CG] = imx_clk_gate("enet_axi_cg", "enet_axi_src", base + 0x8900, 28);
	clks[IMX7D_NAND_USDHC_BUS_ROOT_CG] = imx_clk_gate("nand_usdhc_cg", "nand_usdhc_src", base + 0x8980, 28);
	clks[IMX7D_AHB_CHANNEL_ROOT_CG] = imx_clk_gate("ahb_cg", "ahb_src", base + 0x9000, 28);
	clks[IMX7D_DRAM_PHYM_ROOT_CG] = imx_clk_gate("dram_phym_cg", "dram_phym_src", base + 0x9800, 28);
	clks[IMX7D_DRAM_ROOT_CG] = imx_clk_gate("dram_cg", "dram_src", base + 0x9880, 28);
	clks[IMX7D_DRAM_PHYM_ALT_ROOT_CG] = imx_clk_gate("dram_phym_alt_cg", "dram_phym_alt_src", base + 0xa000, 28);
	clks[IMX7D_DRAM_ALT_ROOT_CG] = imx_clk_gate("dram_alt_cg", "dram_alt_src", base + 0xa080, 28);
	clks[IMX7D_USB_HSIC_ROOT_CG] = imx_clk_gate("usb_hsic_cg", "usb_hsic_src", base + 0xa100, 28);
	clks[IMX7D_PCIE_CTRL_ROOT_CG] = imx_clk_gate("pcie_ctrl_cg", "pcie_ctrl_src", base + 0xa180, 28);
	clks[IMX7D_PCIE_PHY_ROOT_CG] = imx_clk_gate("pcie_phy_cg", "pcie_phy_src", base + 0xa200, 28);
	clks[IMX7D_EPDC_PIXEL_ROOT_CG] = imx_clk_gate("epdc_pixel_cg", "epdc_pixel_src", base + 0xa280, 28);
	clks[IMX7D_LCDIF_PIXEL_ROOT_CG] = imx_clk_gate("lcdif_pixel_cg", "lcdif_pixel_src", base + 0xa300, 28);
	clks[IMX7D_MIPI_DSI_ROOT_CG] = imx_clk_gate("mipi_dsi_cg", "mipi_dsi_src", base + 0xa380, 28);
	clks[IMX7D_MIPI_CSI_ROOT_CG] = imx_clk_gate("mipi_csi_cg", "mipi_csi_src", base + 0xa400, 28);
	clks[IMX7D_MIPI_DPHY_ROOT_CG] = imx_clk_gate("mipi_dphy_cg", "mipi_dphy_src", base + 0xa480, 28);
	clks[IMX7D_SAI1_ROOT_CG] = imx_clk_gate("sai1_cg", "sai1_src", base + 0xa500, 28);
	clks[IMX7D_SAI2_ROOT_CG] = imx_clk_gate("sai2_cg", "sai2_src", base + 0xa580, 28);
	clks[IMX7D_SAI3_ROOT_CG] = imx_clk_gate("sai3_cg", "sai3_src", base + 0xa600, 28);
	clks[IMX7D_SPDIF_ROOT_CG] = imx_clk_gate("spdif_cg", "spdif_src", base + 0xa680, 28);
	clks[IMX7D_ENET1_REF_ROOT_CG] = imx_clk_gate("enet1_ref_cg", "enet1_ref_src", base + 0xa700, 28);
	clks[IMX7D_ENET1_TIME_ROOT_CG] = imx_clk_gate("enet1_time_cg", "enet1_time_src", base + 0xa780, 28);
	clks[IMX7D_ENET2_REF_ROOT_CG] = imx_clk_gate("enet2_ref_cg", "enet2_ref_src", base + 0xa800, 28);
	clks[IMX7D_ENET2_TIME_ROOT_CG] = imx_clk_gate("enet2_time_cg", "enet2_time_src", base + 0xa880, 28);
	clks[IMX7D_ENET_PHY_REF_ROOT_CG] = imx_clk_gate("enet_phy_ref_cg", "enet_phy_ref_src", base + 0xa900, 28);
	clks[IMX7D_EIM_ROOT_CG] = imx_clk_gate("eim_cg", "eim_src", base + 0xa980, 28);
	clks[IMX7D_NAND_ROOT_CG] = imx_clk_gate("nand_cg", "nand_src", base + 0xaa00, 28);
	clks[IMX7D_QSPI_ROOT_CG] = imx_clk_gate("qspi_cg", "qspi_src", base + 0xaa80, 28);
	clks[IMX7D_USDHC1_ROOT_CG] = imx_clk_gate("usdhc1_cg", "usdhc1_src", base + 0xab00, 28);
	clks[IMX7D_USDHC2_ROOT_CG] = imx_clk_gate("usdhc2_cg", "usdhc2_src", base + 0xab80, 28);
	clks[IMX7D_USDHC3_ROOT_CG] = imx_clk_gate("usdhc3_cg", "usdhc3_src", base + 0xac00, 28);
	clks[IMX7D_CAN1_ROOT_CG] = imx_clk_gate("can1_cg", "can1_src", base + 0xac80, 28);
	clks[IMX7D_CAN2_ROOT_CG] = imx_clk_gate("can2_cg", "can2_src", base + 0xad00, 28);
	clks[IMX7D_I2C1_ROOT_CG] = imx_clk_gate("i2c1_cg", "i2c1_src", base + 0xad80, 28);
	clks[IMX7D_I2C2_ROOT_CG] = imx_clk_gate("i2c2_cg", "i2c2_src", base + 0xae00, 28);
	clks[IMX7D_I2C3_ROOT_CG] = imx_clk_gate("i2c3_cg", "i2c3_src", base + 0xae80, 28);
	clks[IMX7D_I2C4_ROOT_CG] = imx_clk_gate("i2c4_cg", "i2c4_src", base + 0xaf00, 28);
	clks[IMX7D_UART1_ROOT_CG] = imx_clk_gate("uart1_cg", "uart1_src", base + 0xaf80, 28);
	clks[IMX7D_UART2_ROOT_CG] = imx_clk_gate("uart2_cg", "uart2_src", base + 0xb000, 28);
	clks[IMX7D_UART3_ROOT_CG] = imx_clk_gate("uart3_cg", "uart3_src", base + 0xb080, 28);
	clks[IMX7D_UART4_ROOT_CG] = imx_clk_gate("uart4_cg", "uart4_src", base + 0xb100, 28);
	clks[IMX7D_UART5_ROOT_CG] = imx_clk_gate("uart5_cg", "uart5_src", base + 0xb180, 28);
	clks[IMX7D_UART6_ROOT_CG] = imx_clk_gate("uart6_cg", "uart6_src", base + 0xb200, 28);
	clks[IMX7D_UART7_ROOT_CG] = imx_clk_gate("uart7_cg", "uart7_src", base + 0xb280, 28);
	clks[IMX7D_ECSPI1_ROOT_CG] = imx_clk_gate("ecspi1_cg", "ecspi1_src", base + 0xb300, 28);
	clks[IMX7D_ECSPI2_ROOT_CG] = imx_clk_gate("ecspi2_cg", "ecspi2_src", base + 0xb380, 28);
	clks[IMX7D_ECSPI3_ROOT_CG] = imx_clk_gate("ecspi3_cg", "ecspi3_src", base + 0xb400, 28);
	clks[IMX7D_ECSPI4_ROOT_CG] = imx_clk_gate("ecspi4_cg", "ecspi4_src", base + 0xb480, 28);
	clks[IMX7D_PWM1_ROOT_CG] = imx_clk_gate("pwm1_cg", "pwm1_src", base + 0xb500, 28);
	clks[IMX7D_PWM2_ROOT_CG] = imx_clk_gate("pwm2_cg", "pwm2_src", base + 0xb580, 28);
	clks[IMX7D_PWM3_ROOT_CG] = imx_clk_gate("pwm3_cg", "pwm3_src", base + 0xb600, 28);
	clks[IMX7D_PWM4_ROOT_CG] = imx_clk_gate("pwm4_cg", "pwm4_src", base + 0xb680, 28);
	clks[IMX7D_FLEXTIMER1_ROOT_CG] = imx_clk_gate("flextimer1_cg", "flextimer1_src", base + 0xb700, 28);
	clks[IMX7D_FLEXTIMER2_ROOT_CG] = imx_clk_gate("flextimer2_cg", "flextimer2_src", base + 0xb780, 28);
	clks[IMX7D_SIM1_ROOT_CG] = imx_clk_gate("sim1_cg", "sim1_src", base + 0xb800, 28);
	clks[IMX7D_SIM2_ROOT_CG] = imx_clk_gate("sim2_cg", "sim2_src", base + 0xb880, 28);
	clks[IMX7D_GPT1_ROOT_CG] = imx_clk_gate("gpt1_cg", "gpt1_src", base + 0xb900, 28);
	clks[IMX7D_GPT2_ROOT_CG] = imx_clk_gate("gpt2_cg", "gpt2_src", base + 0xb980, 28);
	clks[IMX7D_GPT3_ROOT_CG] = imx_clk_gate("gpt3_cg", "gpt3_src", base + 0xbA00, 28);
	clks[IMX7D_GPT4_ROOT_CG] = imx_clk_gate("gpt4_cg", "gpt4_src", base + 0xbA80, 28);
	clks[IMX7D_TRACE_ROOT_CG] = imx_clk_gate("trace_cg", "trace_src", base + 0xbb00, 28);
	clks[IMX7D_WDOG_ROOT_CG] = imx_clk_gate("wdog_cg", "wdog_src", base + 0xbb80, 28);
	clks[IMX7D_CSI_MCLK_ROOT_CG] = imx_clk_gate("csi_mclk_cg", "csi_mclk_src", base + 0xbc00, 28);
	clks[IMX7D_AUDIO_MCLK_ROOT_CG] = imx_clk_gate("audio_mclk_cg", "audio_mclk_src", base + 0xbc80, 28);
	clks[IMX7D_WRCLK_ROOT_CG] = imx_clk_gate("wrclk_cg", "wrclk_src", base + 0xbd00, 28);
	clks[IMX7D_CLKO1_ROOT_CG] = imx_clk_gate("clko1_cg", "clko1_src", base + 0xbd80, 28);
	clks[IMX7D_CLKO2_ROOT_CG] = imx_clk_gate("clko2_cg", "clko2_src", base + 0xbe00, 28);

	clks[IMX7D_MAIN_AXI_ROOT_PRE_DIV] = imx_clk_divider("axi_pre_div", "axi_cg", base + 0x8800, 16, 3);
	clks[IMX7D_DISP_AXI_ROOT_PRE_DIV] = imx_clk_divider("disp_axi_pre_div", "disp_axi_cg", base + 0x8880, 16, 3);
	clks[IMX7D_ENET_AXI_ROOT_PRE_DIV] = imx_clk_divider("enet_axi_pre_div", "enet_axi_cg", base + 0x8900, 16, 3);
	clks[IMX7D_NAND_USDHC_BUS_ROOT_PRE_DIV] = imx_clk_divider("nand_usdhc_pre_div", "nand_usdhc_cg", base + 0x8980, 16, 3);
	clks[IMX7D_AHB_CHANNEL_ROOT_PRE_DIV] = imx_clk_divider("ahb_pre_div", "ahb_cg", base + 0x9000, 16, 3);
	clks[IMX7D_DRAM_PHYM_ALT_ROOT_PRE_DIV] = imx_clk_divider("dram_phym_alt_pre_div", "dram_phym_alt_cg", base + 0xa000, 16, 3);
	clks[IMX7D_DRAM_ALT_ROOT_PRE_DIV] = imx_clk_divider("dram_alt_pre_div", "dram_alt_cg", base + 0xa080, 16, 3);
	clks[IMX7D_USB_HSIC_ROOT_PRE_DIV] = imx_clk_divider("usb_hsic_pre_div", "usb_hsic_cg", base + 0xa100, 16, 3);
	clks[IMX7D_PCIE_CTRL_ROOT_PRE_DIV] = imx_clk_divider("pcie_ctrl_pre_div", "pcie_ctrl_cg", base + 0xa180, 16, 3);
	clks[IMX7D_PCIE_PHY_ROOT_PRE_DIV] = imx_clk_divider("pcie_phy_pre_div", "pcie_phy_cg", base + 0xa200, 16, 3);
	clks[IMX7D_EPDC_PIXEL_ROOT_PRE_DIV] = imx_clk_divider("epdc_pixel_pre_div", "epdc_pixel_cg", base + 0xa280, 16, 3);
	clks[IMX7D_LCDIF_PIXEL_ROOT_PRE_DIV] = imx_clk_divider("lcdif_pixel_pre_div", "lcdif_pixel_cg", base + 0xa300, 16, 3);
	clks[IMX7D_MIPI_DSI_ROOT_PRE_DIV] = imx_clk_divider("mipi_dsi_pre_div", "mipi_dsi_cg", base + 0xa380, 16, 3);
	clks[IMX7D_MIPI_CSI_ROOT_PRE_DIV] = imx_clk_divider("mipi_csi_pre_div", "mipi_csi_cg", base + 0xa400, 16, 3);
	clks[IMX7D_MIPI_DPHY_ROOT_PRE_DIV] = imx_clk_divider("mipi_dphy_pre_div", "mipi_dphy_cg", base + 0xa480, 16, 3);
	clks[IMX7D_SAI1_ROOT_PRE_DIV] = imx_clk_divider("sai1_pre_div", "sai1_cg", base + 0xa500, 16, 3);
	clks[IMX7D_SAI2_ROOT_PRE_DIV] = imx_clk_divider("sai2_pre_div", "sai2_cg", base + 0xa580, 16, 3);
	clks[IMX7D_SAI3_ROOT_PRE_DIV] = imx_clk_divider("sai3_pre_div", "sai3_cg", base + 0xa600, 16, 3);
	clks[IMX7D_SPDIF_ROOT_PRE_DIV] = imx_clk_divider("spdif_pre_div", "spdif_cg", base + 0xa680, 16, 3);
	clks[IMX7D_ENET1_REF_ROOT_PRE_DIV] = imx_clk_divider("enet1_ref_pre_div", "enet1_ref_cg", base + 0xa700, 16, 3);
	clks[IMX7D_ENET1_TIME_ROOT_PRE_DIV] = imx_clk_divider("enet1_time_pre_div", "enet1_time_cg", base + 0xa780, 16, 3);
	clks[IMX7D_ENET2_REF_ROOT_PRE_DIV] = imx_clk_divider("enet2_ref_pre_div", "enet2_ref_cg", base + 0xa800, 16, 3);
	clks[IMX7D_ENET2_TIME_ROOT_PRE_DIV] = imx_clk_divider("enet2_time_pre_div", "enet2_time_cg", base + 0xa880, 16, 3);
	clks[IMX7D_ENET_PHY_REF_ROOT_PRE_DIV] = imx_clk_divider("enet_phy_ref_pre_div", "enet_phy_ref_cg", base + 0xa900, 16, 3);
	clks[IMX7D_EIM_ROOT_PRE_DIV] = imx_clk_divider("eim_pre_div", "eim_cg", base + 0xa980, 16, 3);
	clks[IMX7D_NAND_ROOT_PRE_DIV] = imx_clk_divider("nand_pre_div", "nand_cg", base + 0xaa00, 16, 3);
	clks[IMX7D_QSPI_ROOT_PRE_DIV] = imx_clk_divider("qspi_pre_div", "qspi_cg", base + 0xaa80, 16, 3);
	clks[IMX7D_USDHC1_ROOT_PRE_DIV] = imx_clk_divider("usdhc1_pre_div", "usdhc1_cg", base + 0xab00, 16, 3);
	clks[IMX7D_USDHC2_ROOT_PRE_DIV] = imx_clk_divider("usdhc2_pre_div", "usdhc2_cg", base + 0xab80, 16, 3);
	clks[IMX7D_USDHC3_ROOT_PRE_DIV] = imx_clk_divider("usdhc3_pre_div", "usdhc3_cg", base + 0xac00, 16, 3);
	clks[IMX7D_CAN1_ROOT_PRE_DIV] = imx_clk_divider("can1_pre_div", "can1_cg", base + 0xac80, 16, 3);
	clks[IMX7D_CAN2_ROOT_PRE_DIV] = imx_clk_divider("can2_pre_div", "can2_cg", base + 0xad00, 16, 3);
	clks[IMX7D_I2C1_ROOT_PRE_DIV] = imx_clk_divider("i2c1_pre_div", "i2c1_cg", base + 0xad80, 16, 3);
	clks[IMX7D_I2C2_ROOT_PRE_DIV] = imx_clk_divider("i2c2_pre_div", "i2c2_cg", base + 0xae00, 16, 3);
	clks[IMX7D_I2C3_ROOT_PRE_DIV] = imx_clk_divider("i2c3_pre_div", "i2c3_cg", base + 0xae80, 16, 3);
	clks[IMX7D_I2C4_ROOT_PRE_DIV] = imx_clk_divider("i2c4_pre_div", "i2c4_cg", base + 0xaf00, 16, 3);
	clks[IMX7D_UART1_ROOT_PRE_DIV] = imx_clk_divider("uart1_pre_div", "uart1_cg", base + 0xaf80, 16, 3);
	clks[IMX7D_UART2_ROOT_PRE_DIV] = imx_clk_divider("uart2_pre_div", "uart2_cg", base + 0xb000, 16, 3);
	clks[IMX7D_UART3_ROOT_PRE_DIV] = imx_clk_divider("uart3_pre_div", "uart3_cg", base + 0xb080, 16, 3);
	clks[IMX7D_UART4_ROOT_PRE_DIV] = imx_clk_divider("uart4_pre_div", "uart4_cg", base + 0xb100, 16, 3);
	clks[IMX7D_UART5_ROOT_PRE_DIV] = imx_clk_divider("uart5_pre_div", "uart5_cg", base + 0xb180, 16, 3);
	clks[IMX7D_UART6_ROOT_PRE_DIV] = imx_clk_divider("uart6_pre_div", "uart6_cg", base + 0xb200, 16, 3);
	clks[IMX7D_UART7_ROOT_PRE_DIV] = imx_clk_divider("uart7_pre_div", "uart7_cg", base + 0xb280, 16, 3);
	clks[IMX7D_ECSPI1_ROOT_PRE_DIV] = imx_clk_divider("ecspi1_pre_div", "ecspi1_cg", base + 0xb300, 16, 3);
	clks[IMX7D_ECSPI2_ROOT_PRE_DIV] = imx_clk_divider("ecspi2_pre_div", "ecspi2_cg", base + 0xb380, 16, 3);
	clks[IMX7D_ECSPI3_ROOT_PRE_DIV] = imx_clk_divider("ecspi3_pre_div", "ecspi3_cg", base + 0xb400, 16, 3);
	clks[IMX7D_ECSPI4_ROOT_PRE_DIV] = imx_clk_divider("ecspi4_pre_div", "ecspi4_cg", base + 0xb480, 16, 3);
	clks[IMX7D_PWM1_ROOT_PRE_DIV] = imx_clk_divider("pwm1_pre_div", "pwm1_cg", base + 0xb500, 16, 3);
	clks[IMX7D_PWM2_ROOT_PRE_DIV] = imx_clk_divider("pwm2_pre_div", "pwm2_cg", base + 0xb580, 16, 3);
	clks[IMX7D_PWM3_ROOT_PRE_DIV] = imx_clk_divider("pwm3_pre_div", "pwm3_cg", base + 0xb600, 16, 3);
	clks[IMX7D_PWM4_ROOT_PRE_DIV] = imx_clk_divider("pwm4_pre_div", "pwm4_cg", base + 0xb680, 16, 3);
	clks[IMX7D_FLEXTIMER1_ROOT_PRE_DIV] = imx_clk_divider("flextimer1_pre_div", "flextimer1_cg", base + 0xb700, 16, 3);
	clks[IMX7D_FLEXTIMER2_ROOT_PRE_DIV] = imx_clk_divider("flextimer2_pre_div", "flextimer2_cg", base + 0xb780, 16, 3);
	clks[IMX7D_SIM1_ROOT_PRE_DIV] = imx_clk_divider("sim1_pre_div", "sim1_cg", base + 0xb800, 16, 3);
	clks[IMX7D_SIM2_ROOT_PRE_DIV] = imx_clk_divider("sim2_pre_div", "sim2_cg", base + 0xb880, 16, 3);
	clks[IMX7D_GPT1_ROOT_PRE_DIV] = imx_clk_divider("gpt1_pre_div", "gpt1_cg", base + 0xb900, 16, 3);
	clks[IMX7D_GPT2_ROOT_PRE_DIV] = imx_clk_divider("gpt2_pre_div", "gpt2_cg", base + 0xb980, 16, 3);
	clks[IMX7D_GPT3_ROOT_PRE_DIV] = imx_clk_divider("gpt3_pre_div", "gpt3_cg", base + 0xba00, 16, 3);
	clks[IMX7D_GPT4_ROOT_PRE_DIV] = imx_clk_divider("gpt4_pre_div", "gpt4_cg", base + 0xba80, 16, 3);
	clks[IMX7D_TRACE_ROOT_PRE_DIV] = imx_clk_divider("trace_pre_div", "trace_cg", base + 0xbb00, 16, 3);
	clks[IMX7D_WDOG_ROOT_PRE_DIV] = imx_clk_divider("wdog_pre_div", "wdog_cg", base + 0xbb80, 16, 3);
	clks[IMX7D_CSI_MCLK_ROOT_PRE_DIV] = imx_clk_divider("csi_mclk_pre_div", "csi_mclk_cg", base + 0xbc00, 16, 3);
	clks[IMX7D_AUDIO_MCLK_ROOT_PRE_DIV] = imx_clk_divider("audio_mclk_pre_div", "audio_mclk_cg", base + 0xbc80, 16, 3);
	clks[IMX7D_WRCLK_ROOT_PRE_DIV] = imx_clk_divider("wrclk_pre_div", "wrclk_cg", base + 0xbd00, 16, 3);
	clks[IMX7D_CLKO1_ROOT_PRE_DIV] = imx_clk_divider("clko1_pre_div", "clko1_cg", base + 0xbd80, 16, 3);
	clks[IMX7D_CLKO2_ROOT_PRE_DIV] = imx_clk_divider("clko2_pre_div", "clko2_cg", base + 0xbe00, 16, 3);

	clks[IMX7D_ARM_A7_ROOT_DIV] = imx_clk_divider("arm_a7_div", "arm_a7_cg", base + 0x8000, 0, 3);
	clks[IMX7D_ARM_M4_ROOT_DIV] = imx_clk_divider("arm_m4_div", "arm_m4_cg", base + 0x8080, 0, 3);
	clks[IMX7D_ARM_M0_ROOT_DIV] = imx_clk_divider("arm_m0_div", "arm_m0_cg", base + 0x8100, 0, 3);
	clks[IMX7D_MAIN_AXI_ROOT_DIV] = imx_clk_divider("axi_post_div", "axi_pre_div", base + 0x8800, 0, 6);
	clks[IMX7D_DISP_AXI_ROOT_DIV] = imx_clk_divider("disp_axi_post_div", "disp_axi_pre_div", base + 0x8880, 0, 6);
	clks[IMX7D_ENET_AXI_ROOT_DIV] = imx_clk_divider("enet_axi_post_div", "enet_axi_pre_div", base + 0x8900, 0, 6);
	clks[IMX7D_NAND_USDHC_BUS_ROOT_DIV] = imx_clk_divider("nand_usdhc_post_div", "nand_usdhc_pre_div", base + 0x8980, 0, 6);
	clks[IMX7D_AHB_CHANNEL_ROOT_DIV] = imx_clk_divider("ahb_post_div", "ahb_pre_div", base + 0x9000, 0, 6);
	clks[IMX7D_DRAM_ROOT_DIV] = imx_clk_divider("dram_post_div", "dram_cg", base + 0x9880, 0, 3);
	clks[IMX7D_DRAM_PHYM_ALT_ROOT_DIV] = imx_clk_divider("dram_phym_alt_post_div", "dram_phym_alt_pre_div", base + 0xa000, 0, 3);
	clks[IMX7D_DRAM_ALT_ROOT_DIV] = imx_clk_divider("dram_alt_post_div", "dram_alt_pre_div", base + 0xa080, 0, 3);
	clks[IMX7D_USB_HSIC_ROOT_DIV] = imx_clk_divider("usb_hsic_post_div", "usb_hsic_pre_div", base + 0xa100, 0, 6);
	clks[IMX7D_PCIE_CTRL_ROOT_DIV] = imx_clk_divider("pcie_ctrl_post_div", "pcie_ctrl_pre_div", base + 0xa180, 0, 6);
	clks[IMX7D_PCIE_PHY_ROOT_DIV] = imx_clk_divider("pcie_phy_post_div", "pcie_phy_pre_div", base + 0xa200, 0, 6);
	clks[IMX7D_EPDC_PIXEL_ROOT_DIV] = imx_clk_divider("epdc_pixel_post_div", "epdc_pixel_pre_div", base + 0xa280, 0, 6);
	clks[IMX7D_LCDIF_PIXEL_ROOT_DIV] = imx_clk_divider("lcdif_pixel_post_div", "lcdif_pixel_pre_div", base + 0xa300, 0, 6);
	clks[IMX7D_MIPI_DSI_ROOT_DIV] = imx_clk_divider("mipi_dsi_post_div", "mipi_dsi_pre_div", base + 0xa380, 0, 6);
	clks[IMX7D_MIPI_CSI_ROOT_DIV] = imx_clk_divider("mipi_csi_post_div", "mipi_csi_pre_div", base + 0xa400, 0, 6);
	clks[IMX7D_MIPI_DPHY_ROOT_DIV] = imx_clk_divider("mipi_dphy_post_div", "mipi_csi_dphy_div", base + 0xa480, 0, 6);
	clks[IMX7D_SAI1_ROOT_DIV] = imx_clk_divider("sai1_post_div", "sai1_pre_div", base + 0xa500, 0, 6);
	clks[IMX7D_SAI2_ROOT_DIV] = imx_clk_divider("sai2_post_div", "sai2_pre_div", base + 0xa580, 0, 6);
	clks[IMX7D_SAI3_ROOT_DIV] = imx_clk_divider("sai3_post_div", "sai3_pre_div", base + 0xa600, 0, 6);
	clks[IMX7D_SPDIF_ROOT_DIV] = imx_clk_divider("spdif_post_div", "spdif_pre_div", base + 0xa680, 0, 6);
	clks[IMX7D_ENET1_REF_ROOT_DIV] = imx_clk_divider("enet1_ref_post_div", "enet1_ref_pre_div", base + 0xa700, 0, 6);
	clks[IMX7D_ENET1_TIME_ROOT_DIV] = imx_clk_divider("enet1_time_post_div", "enet1_time_pre_div", base + 0xa780, 0, 6);
	clks[IMX7D_ENET2_REF_ROOT_DIV] = imx_clk_divider("enet2_ref_post_div", "enet2_ref_pre_div", base + 0xa800, 0, 6);
	clks[IMX7D_ENET2_TIME_ROOT_DIV] = imx_clk_divider("enet2_time_post_div", "enet2_time_pre_div", base + 0xa880, 0, 6);
	clks[IMX7D_ENET_PHY_REF_ROOT_DIV] = imx_clk_divider("enet_phy_ref_post_div", "enet_phy_ref_pre_div", base + 0xa900, 0, 6);
	clks[IMX7D_EIM_ROOT_DIV] = imx_clk_divider("eim_post_div", "eim_pre_div", base + 0xa980, 0, 6);
	clks[IMX7D_NAND_ROOT_DIV] = imx_clk_divider("nand_post_div", "nand_pre_div", base + 0xaa00, 0, 6);
	clks[IMX7D_QSPI_ROOT_DIV] = imx_clk_divider("qspi_post_div", "qspi_pre_div", base + 0xaa80, 0, 6);
	clks[IMX7D_USDHC1_ROOT_DIV] = imx_clk_divider("usdhc1_post_div", "usdhc1_pre_div", base + 0xab00, 0, 6);
	clks[IMX7D_USDHC2_ROOT_DIV] = imx_clk_divider("usdhc2_post_div", "usdhc2_pre_div", base + 0xab80, 0, 6);
	clks[IMX7D_USDHC3_ROOT_DIV] = imx_clk_divider("usdhc3_post_div", "usdhc3_pre_div", base + 0xac00, 0, 6);
	clks[IMX7D_CAN1_ROOT_DIV] = imx_clk_divider("can1_post_div", "can1_pre_div", base + 0xac80, 0, 6);
	clks[IMX7D_CAN2_ROOT_DIV] = imx_clk_divider("can2_post_div", "can2_pre_div", base + 0xad00, 0, 6);
	clks[IMX7D_I2C1_ROOT_DIV] = imx_clk_divider("i2c1_post_div", "i2c1_pre_div", base + 0xad80, 0, 6);
	clks[IMX7D_I2C2_ROOT_DIV] = imx_clk_divider("i2c2_post_div", "i2c2_pre_div", base + 0xae00, 0, 6);
	clks[IMX7D_I2C3_ROOT_DIV] = imx_clk_divider("i2c3_post_div", "i2c3_pre_div", base + 0xae80, 0, 6);
	clks[IMX7D_I2C4_ROOT_DIV] = imx_clk_divider("i2c4_post_div", "i2c4_pre_div", base + 0xaf00, 0, 6);
	clks[IMX7D_UART1_ROOT_DIV] = imx_clk_divider("uart1_post_div", "uart1_pre_div", base + 0xaf80, 0, 6);
	clks[IMX7D_UART2_ROOT_DIV] = imx_clk_divider("uart2_post_div", "uart2_pre_div", base + 0xb000, 0, 6);
	clks[IMX7D_UART3_ROOT_DIV] = imx_clk_divider("uart3_post_div", "uart3_pre_div", base + 0xb080, 0, 6);
	clks[IMX7D_UART4_ROOT_DIV] = imx_clk_divider("uart4_post_div", "uart4_pre_div", base + 0xb100, 0, 6);
	clks[IMX7D_UART5_ROOT_DIV] = imx_clk_divider("uart5_post_div", "uart5_pre_div", base + 0xb180, 0, 6);
	clks[IMX7D_UART6_ROOT_DIV] = imx_clk_divider("uart6_post_div", "uart6_pre_div", base + 0xb200, 0, 6);
	clks[IMX7D_UART7_ROOT_DIV] = imx_clk_divider("uart7_post_div", "uart7_pre_div", base + 0xb280, 0, 6);
	clks[IMX7D_ECSPI1_ROOT_DIV] = imx_clk_divider("ecspi1_post_div", "ecspi1_pre_div", base + 0xb300, 0, 6);
	clks[IMX7D_ECSPI2_ROOT_DIV] = imx_clk_divider("ecspi2_post_div", "ecspi2_pre_div", base + 0xb380, 0, 6);
	clks[IMX7D_ECSPI3_ROOT_DIV] = imx_clk_divider("ecspi3_post_div", "ecspi3_pre_div", base + 0xb400, 0, 6);
	clks[IMX7D_ECSPI4_ROOT_DIV] = imx_clk_divider("ecspi4_post_div", "ecspi4_pre_div", base + 0xb480, 0, 6);
	clks[IMX7D_PWM1_ROOT_DIV] = imx_clk_divider("pwm1_post_div", "pwm1_pre_div", base + 0xb500, 0, 6);
	clks[IMX7D_PWM2_ROOT_DIV] = imx_clk_divider("pwm2_post_div", "pwm2_pre_div", base + 0xb580, 0, 6);
	clks[IMX7D_PWM3_ROOT_DIV] = imx_clk_divider("pwm3_post_div", "pwm3_pre_div", base + 0xb600, 0, 6);
	clks[IMX7D_PWM4_ROOT_DIV] = imx_clk_divider("pwm4_post_div", "pwm4_pre_div", base + 0xb680, 0, 6);
	clks[IMX7D_FLEXTIMER1_ROOT_DIV] = imx_clk_divider("flextimer1_post_div", "flextimer1_pre_div", base + 0xb700, 0, 6);
	clks[IMX7D_FLEXTIMER2_ROOT_DIV] = imx_clk_divider("flextimer2_post_div", "flextimer2_pre_div", base + 0xb780, 0, 6);
	clks[IMX7D_SIM1_ROOT_DIV] = imx_clk_divider("sim1_post_div", "sim1_pre_div", base + 0xb800, 0, 6);
	clks[IMX7D_SIM2_ROOT_DIV] = imx_clk_divider("sim2_post_div", "sim2_pre_div", base + 0xb880, 0, 6);
	clks[IMX7D_GPT1_ROOT_DIV] = imx_clk_divider("gpt1_post_div", "gpt1_pre_div", base + 0xb900, 0, 6);
	clks[IMX7D_GPT2_ROOT_DIV] = imx_clk_divider("gpt2_post_div", "gpt2_pre_div", base + 0xb980, 0, 6);
	clks[IMX7D_GPT3_ROOT_DIV] = imx_clk_divider("gpt3_post_div", "gpt3_pre_div", base + 0xba00, 0, 6);
	clks[IMX7D_GPT4_ROOT_DIV] = imx_clk_divider("gpt4_post_div", "gpt4_pre_div", base + 0xba80, 0, 6);
	clks[IMX7D_TRACE_ROOT_DIV] = imx_clk_divider("trace_post_div", "trace_pre_div", base + 0xbb00, 0, 6);
	clks[IMX7D_WDOG_ROOT_DIV] = imx_clk_divider("wdog_post_div", "wdog_pre_div", base + 0xbb80, 0, 6);
	clks[IMX7D_CSI_MCLK_ROOT_DIV] = imx_clk_divider("csi_mclk_post_div", "csi_mclk_pre_div", base + 0xbc00, 0, 6);
	clks[IMX7D_AUDIO_MCLK_ROOT_DIV] = imx_clk_divider("audio_mclk_post_div", "audio_mclk_pre_div", base + 0xbc80, 0, 6);
	clks[IMX7D_WRCLK_ROOT_DIV] = imx_clk_divider("wrclk_post_div", "wrclk_pre_div", base + 0xbd00, 0, 6);
	clks[IMX7D_CLKO1_ROOT_DIV] = imx_clk_divider("clko1_post_div", "clko1_pre_div", base + 0xbd80, 0, 6);
	clks[IMX7D_CLKO2_ROOT_DIV] = imx_clk_divider("clko2_post_div", "clko2_pre_div", base + 0xbe00, 0, 6);

	clks[IMX7D_ARM_A7_ROOT_CLK] = imx_clk_gate2("arm_a7_root_clk", "arm_a7_div", base + 0x4000, 0);
	clks[IMX7D_ARM_M4_ROOT_CLK] = imx_clk_gate2("arm_m4_root_clk", "arm_m4_div", base + 0x4010, 0);
	clks[IMX7D_ARM_M0_ROOT_CLK] = imx_clk_gate2("arm_m0_root_clk", "arm_m0_div", base + 0x4020, 0);
	clks[IMX7D_MAIN_AXI_ROOT_CLK] = imx_clk_gate2("main_axi_root_clk", "axi_post_div", base + 0x4040, 0);
	clks[IMX7D_DISP_AXI_ROOT_CLK] = imx_clk_gate2("disp_axi_root_clk", "disp_axi_post_div", base + 0x4050, 0);
	clks[IMX7D_ENET_AXI_ROOT_CLK] = imx_clk_gate2("enet_axi_root_clk", "enet_axi_post_div", base + 0x4060, 0);
	clks[IMX7D_OCRAM_CLK] = imx_clk_gate2("ocram_clk", "axi_post_div", base + 0x4110, 0);
	clks[IMX7D_OCRAM_S_CLK] = imx_clk_gate2("ocram_s_clk", "ahb_post_div", base + 0x4120, 0);
	clks[IMX7D_NAND_USDHC_BUS_ROOT_CLK] = imx_clk_gate2("nand_usdhc_root_clk", "nand_usdhc_post_div", base + 0x4130, 0);
	clks[IMX7D_AHB_CHANNEL_ROOT_CLK] = imx_clk_gate2("ahb_root_clk", "ahb_post_div", base + 0x4200, 0);
	clks[IMX7D_DRAM_ROOT_CLK] = imx_clk_gate2("dram_root_clk", "dram_post_div", base + 0x4130, 0);
	clks[IMX7D_DRAM_PHYM_ROOT_CLK] = imx_clk_gate2("dram_phym_root_clk", "dram_phym_cg", base + 0x4130, 0);
	clks[IMX7D_DRAM_PHYM_ALT_ROOT_CLK] = imx_clk_gate2("dram_phym_alt_root_clk", "dram_phym_alt_post_div", base + 0x4130, 0);
	clks[IMX7D_DRAM_ALT_ROOT_CLK] = imx_clk_gate2("dram_alt_root_clk", "dram_alt_post_div", base + 0x4130, 0);
	clks[IMX7D_USB_HSIC_ROOT_CLK] = imx_clk_gate2("usb_hsic_root_clk", "usb_hsic_post_div", base + 0x4420, 0);
	clks[IMX7D_PCIE_CTRL_ROOT_CLK] = imx_clk_gate2("pcie_ctrl_root_clk", "pcie_ctrl_post_div", base + 0x4600, 0);
	clks[IMX7D_PCIE_PHY_ROOT_CLK] = imx_clk_gate2("pcie_phy_root_clk", "pcie_phy_post_div", base + 0x4600, 0);
	clks[IMX7D_EPDC_PIXEL_ROOT_CLK] = imx_clk_gate2("epdc_pixel_root_clk", "epdc_pixel_post_div", base + 0x44a0, 0);
	clks[IMX7D_LCDIF_PIXEL_ROOT_CLK] = imx_clk_gate2("lcdif_pixel_root_clk", "lcdif_pixel_post_div", base + 0x44b0, 0);
	clks[IMX7D_MIPI_DSI_ROOT_CLK] = imx_clk_gate2("mipi_dsi_root_clk", "mipi_dsi_post_div", base + 0x4650, 0);
	clks[IMX7D_MIPI_CSI_ROOT_CLK] = imx_clk_gate2("mipi_csi_root_clk", "mipi_csi_post_div", base + 0x4640, 0);
	clks[IMX7D_MIPI_DPHY_ROOT_CLK] = imx_clk_gate2("mipi_dphy_root_clk", "mipi_dphy_post_div", base + 0x4660, 0);
	clks[IMX7D_SAI1_ROOT_CLK] = imx_clk_gate2("sai1_root_clk", "sai1_post_div", base + 0x48c0, 0);
	clks[IMX7D_SAI2_ROOT_CLK] = imx_clk_gate2("sai2_root_clk", "sai2_post_div", base + 0x48d0, 0);
	clks[IMX7D_SAI3_ROOT_CLK] = imx_clk_gate2("sai3_root_clk", "sai3_post_div", base + 0x48e0, 0);
	clks[IMX7D_SPDIF_ROOT_CLK] = imx_clk_gate2("spdif_root_clk", "spdif_post_div", base + 0x44d0, 0);
	clks[IMX7D_ENET1_REF_ROOT_CLK] = imx_clk_gate2("enet1_ref_root_clk", "enet1_ref_post_div", base + 0x44e0, 0);
	clks[IMX7D_ENET1_TIME_ROOT_CLK] = imx_clk_gate2("enet1_time_root_clk", "enet1_time_post_div", base + 0x44f0, 0);
	clks[IMX7D_ENET2_REF_ROOT_CLK] = imx_clk_gate2("enet2_ref_root_clk", "enet2_ref_post_div", base + 0x4500, 0);
	clks[IMX7D_ENET2_TIME_ROOT_CLK] = imx_clk_gate2("enet2_time_root_clk", "enet2_time_post_div", base + 0x4510, 0);
	clks[IMX7D_ENET_PHY_REF_ROOT_CLK] = imx_clk_gate2("enet_phy_ref_root_clk", "enet_phy_ref_post_div", base + 0x4520, 0);
	clks[IMX7D_EIM_ROOT_CLK] = imx_clk_gate2("eim_root_clk", "eim_post_div", base + 0x4160, 0);
	clks[IMX7D_NAND_ROOT_CLK] = imx_clk_gate2("nand_root_clk", "nand_post_div", base + 0x4140, 0);
	clks[IMX7D_QSPI_ROOT_CLK] = imx_clk_gate2("qspi_root_clk", "qspi_post_div", base + 0x4150, 0);
	clks[IMX7D_USDHC1_ROOT_CLK] = imx_clk_gate2("usdhc1_root_clk", "usdhc1_post_div", base + 0x46c0, 0);
	clks[IMX7D_USDHC2_ROOT_CLK] = imx_clk_gate2("usdhc2_root_clk", "usdhc2_post_div", base + 0x46d0, 0);
	clks[IMX7D_USDHC3_ROOT_CLK] = imx_clk_gate2("usdhc3_root_clk", "usdhc3_post_div", base + 0x46e0, 0);
	clks[IMX7D_CAN1_ROOT_CLK] = imx_clk_gate2("can1_root_clk", "can1_post_div", base + 0x4740, 0);
	clks[IMX7D_CAN2_ROOT_CLK] = imx_clk_gate2("can2_root_clk", "can2_post_div", base + 0x4750, 0);
	clks[IMX7D_I2C1_ROOT_CLK] = imx_clk_gate2("i2c1_root_clk", "i2c1_post_div", base + 0x4880, 0);
	clks[IMX7D_I2C2_ROOT_CLK] = imx_clk_gate2("i2c2_root_clk", "i2c2_post_div", base + 0x4890, 0);
	clks[IMX7D_I2C3_ROOT_CLK] = imx_clk_gate2("i2c3_root_clk", "i2c3_post_div", base + 0x48a0, 0);
	clks[IMX7D_I2C4_ROOT_CLK] = imx_clk_gate2("i2c4_root_clk", "i2c4_post_div", base + 0x48b0, 0);
	clks[IMX7D_UART1_ROOT_CLK] = imx_clk_gate2("uart1_root_clk", "uart1_post_div", base + 0x4940, 0);
	clks[IMX7D_UART2_ROOT_CLK] = imx_clk_gate2("uart2_root_clk", "uart2_post_div", base + 0x4950, 0);
	clks[IMX7D_UART3_ROOT_CLK] = imx_clk_gate2("uart3_root_clk", "uart3_post_div", base + 0x4960, 0);
	clks[IMX7D_UART4_ROOT_CLK] = imx_clk_gate2("uart4_root_clk", "uart4_post_div", base + 0x4970, 0);
	clks[IMX7D_UART5_ROOT_CLK] = imx_clk_gate2("uart5_root_clk", "uart5_post_div", base + 0x4980, 0);
	clks[IMX7D_UART6_ROOT_CLK] = imx_clk_gate2("uart6_root_clk", "uart6_post_div", base + 0x4990, 0);
	clks[IMX7D_UART7_ROOT_CLK] = imx_clk_gate2("uart7_root_clk", "uart7_post_div", base + 0x49a0, 0);
	clks[IMX7D_ECSPI1_ROOT_CLK] = imx_clk_gate2("ecspi1_root_clk", "ecspi1_post_div", base + 0x4780, 0);
	clks[IMX7D_ECSPI2_ROOT_CLK] = imx_clk_gate2("ecspi2_root_clk", "ecspi2_post_div", base + 0x4790, 0);
	clks[IMX7D_ECSPI3_ROOT_CLK] = imx_clk_gate2("ecspi3_root_clk", "ecspi3_post_div", base + 0x47a0, 0);
	clks[IMX7D_ECSPI4_ROOT_CLK] = imx_clk_gate2("ecspi4_root_clk", "ecspi4_post_div", base + 0x47b0, 0);
	clks[IMX7D_PWM1_ROOT_CLK] = imx_clk_gate2("pwm1_root_clk", "pwm1_post_div", base + 0x4840, 0);
	clks[IMX7D_PWM2_ROOT_CLK] = imx_clk_gate2("pwm2_root_clk", "pwm2_post_div", base + 0x4850, 0);
	clks[IMX7D_PWM3_ROOT_CLK] = imx_clk_gate2("pwm3_root_clk", "pwm3_post_div", base + 0x4860, 0);
	clks[IMX7D_PWM4_ROOT_CLK] = imx_clk_gate2("pwm4_root_clk", "pwm4_post_div", base + 0x4870, 0);
	clks[IMX7D_FLEXTIMER1_ROOT_CLK] = imx_clk_gate2("flextimer1_root_clk", "flextimer1_post_div", base + 0x4800, 0);
	clks[IMX7D_FLEXTIMER2_ROOT_CLK] = imx_clk_gate2("flextimer2_root_clk", "flextimer2_post_div", base + 0x4810, 0);
	clks[IMX7D_SIM1_ROOT_CLK] = imx_clk_gate2("sim1_root_clk", "sim1_post_div", base + 0x4900, 0);
	clks[IMX7D_SIM2_ROOT_CLK] = imx_clk_gate2("sim2_root_clk", "sim2_post_div", base + 0x4910, 0);
	clks[IMX7D_GPT1_ROOT_CLK] = imx_clk_gate2("gpt1_root_clk", "gpt1_post_div", base + 0x47c0, 0);
	clks[IMX7D_GPT2_ROOT_CLK] = imx_clk_gate2("gpt2_root_clk", "gpt2_post_div", base + 0x47d0, 0);
	clks[IMX7D_GPT3_ROOT_CLK] = imx_clk_gate2("gpt3_root_clk", "gpt3_post_div", base + 0x47e0, 0);
	clks[IMX7D_GPT4_ROOT_CLK] = imx_clk_gate2("gpt4_root_clk", "gpt4_post_div", base + 0x47f0, 0);
	clks[IMX7D_TRACE_ROOT_CLK] = imx_clk_gate2("trace_root_clk", "trace_post_div", base + 0x4300, 0);
	clks[IMX7D_WDOG1_ROOT_CLK] = imx_clk_gate2("wdog1_root_clk", "wdog_post_div", base + 0x49c0, 0);
	clks[IMX7D_WDOG2_ROOT_CLK] = imx_clk_gate2("wdog2_root_clk", "wdog_post_div", base + 0x49d0, 0);
	clks[IMX7D_WDOG3_ROOT_CLK] = imx_clk_gate2("wdog3_root_clk", "wdog_post_div", base + 0x49e0, 0);
	clks[IMX7D_WDOG4_ROOT_CLK] = imx_clk_gate2("wdog4_root_clk", "wdog_post_div", base + 0x49f0, 0);
	clks[IMX7D_CSI_MCLK_ROOT_CLK] = imx_clk_gate2("csi_mclk_root_clk", "csi_mclk_post_div", base + 0x4490, 0);
	clks[IMX7D_AUDIO_MCLK_ROOT_CLK] = imx_clk_gate2("audio_mclk_root_clk", "audio_mclk_post_div", base + 0x4790, 0);
	clks[IMX7D_WRCLK_ROOT_CLK] = imx_clk_gate2("wrclk_root_clk", "wrclk_post_div", base + 0x47a0, 0);
	clks[IMX7D_ADC_ROOT_CLK] = imx_clk_gate2("adc_root_clk", "ipg_root_clk", base + 0x4200, 0);

	clks[IMX7D_GPT_3M_CLK] = imx_clk_fixed_factor("gpt_3m", "osc", 1, 8);

	for (i = 0; i < ARRAY_SIZE(clks); i++)
		if (IS_ERR(clks[i]))
			pr_err("i.MX7D clk %d: register failed with %ld\n",
					i, PTR_ERR(clks[i]));

	clk_data.clks = clks;
	clk_data.clk_num = ARRAY_SIZE(clks);
	of_clk_add_provider(np, of_clk_src_onecell_get, &clk_data);

	/* TO BE FIXED LATER
	 * Enable all clock to bring up imx7, otherwise system will be halt and block
	 * the other part upstream Because imx7d clock design changed, clock framework
	 * need do a little modify.
	 * Dong Aisheng is working on this. After that, this part need be changed.
	 */
	for (i = 0; i < IMX7D_CLK_END; i++)
		clk_prepare_enable(clks[i]);

	/* use old gpt clk setting, gpt1 root clk must be twice as gpt counter freq */
	clk_set_parent(clks[IMX7D_GPT1_ROOT_SRC], clks[IMX7D_OSC_24M_CLK]);

	/*
	 * init enet clock source:
	 * 	AXI clock source is 250MHz
	 *	Phy refrence clock is 25MHz
	 *	1588 time clock source is 100MHz
	 */
	clk_set_parent(clks[IMX7D_ENET_AXI_ROOT_SRC], clks[IMX7D_PLL_ENET_MAIN_250M_CLK]);
	clk_set_parent(clks[IMX7D_ENET_PHY_REF_ROOT_SRC], clks[IMX7D_PLL_ENET_MAIN_25M_CLK]);
	clk_set_parent(clks[IMX7D_ENET1_TIME_ROOT_SRC], clks[IMX7D_PLL_ENET_MAIN_100M_CLK]);
	clk_set_parent(clks[IMX7D_ENET2_TIME_ROOT_SRC], clks[IMX7D_PLL_ENET_MAIN_100M_CLK]);

	/* set uart module clock's parent clock source that must be great then 80MHz */
	clk_set_parent(clks[IMX7D_UART1_ROOT_SRC], clks[IMX7D_OSC_24M_CLK]);

	imx_register_uart_clocks(uart_clks);

}
CLK_OF_DECLARE(imx7d, "fsl,imx7d-ccm", imx7d_clocks_init);
