// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2018 NXP.
 * Copyright (C) 2017 Pengutronix, Lucas Stach <kernel@pengutronix.de>
 */

#include <dt-bindings/clock/imx8mq-clock.h>
#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/platform_device.h>

#include "clk.h"

static u32 share_count_sai1;
static u32 share_count_sai2;
static u32 share_count_sai3;
static u32 share_count_sai4;
static u32 share_count_sai5;
static u32 share_count_sai6;
static u32 share_count_dcss;
static u32 share_count_nand;

static const char * const pll_ref_sels[] = { "osc_25m", "osc_27m", "dummy", "dummy", };
static const char * const arm_pll_bypass_sels[] = {"arm_pll", "arm_pll_ref_sel", };
static const char * const gpu_pll_bypass_sels[] = {"gpu_pll", "gpu_pll_ref_sel", };
static const char * const vpu_pll_bypass_sels[] = {"vpu_pll", "vpu_pll_ref_sel", };
static const char * const audio_pll1_bypass_sels[] = {"audio_pll1", "audio_pll1_ref_sel", };
static const char * const audio_pll2_bypass_sels[] = {"audio_pll2", "audio_pll2_ref_sel", };
static const char * const video_pll1_bypass_sels[] = {"video_pll1", "video_pll1_ref_sel", };

static const char * const sys3_pll_out_sels[] = {"sys3_pll1_ref_sel", };
static const char * const dram_pll_out_sels[] = {"dram_pll1_ref_sel", };
static const char * const video2_pll_out_sels[] = {"video2_pll1_ref_sel", };

/* CCM ROOT */
static const char * const imx8mq_a53_sels[] = {"osc_25m", "arm_pll_out", "sys2_pll_500m", "sys2_pll_1000m",
					"sys1_pll_800m", "sys1_pll_400m", "audio_pll1_out", "sys3_pll_out", };

static const char * const imx8mq_a53_core_sels[] = {"arm_a53_div", "arm_pll_out", };

static const char * const imx8mq_arm_m4_sels[] = {"osc_25m", "sys2_pll_200m", "sys2_pll_250m", "sys1_pll_266m",
					"sys1_pll_800m", "audio_pll1_out", "video_pll1_out", "sys3_pll_out", };

static const char * const imx8mq_vpu_sels[] = {"osc_25m", "arm_pll_out", "sys2_pll_500m", "sys2_pll_1000m",
					"sys1_pll_800m", "sys1_pll_400m", "audio_pll1_out", "vpu_pll_out", };

static const char * const imx8mq_gpu_core_sels[] = {"osc_25m", "gpu_pll_out", "sys1_pll_800m", "sys3_pll_out",
					     "sys2_pll_1000m", "audio_pll1_out", "video_pll1_out", "audio_pll2_out", };

static const char * const imx8mq_gpu_shader_sels[] = {"osc_25m", "gpu_pll_out", "sys1_pll_800m", "sys3_pll_out",
					       "sys2_pll_1000m", "audio_pll1_out", "video_pll1_out", "audio_pll2_out", };

static const char * const imx8mq_main_axi_sels[] = {"osc_25m", "sys2_pll_333m", "sys1_pll_800m", "sys2_pll_250m",
					     "sys2_pll_1000m", "audio_pll1_out", "video_pll1_out", "sys1_pll_100m",};

static const char * const imx8mq_enet_axi_sels[] = {"osc_25m", "sys1_pll_266m", "sys1_pll_800m", "sys2_pll_250m",
					     "sys2_pll_200m", "audio_pll1_out", "video_pll1_out", "sys3_pll_out", };

static const char * const imx8mq_nand_usdhc_sels[] = {"osc_25m", "sys1_pll_266m", "sys1_pll_800m", "sys2_pll_200m",
					       "sys1_pll_133m", "sys3_pll_out", "sys2_pll_250m", "audio_pll1_out", };

static const char * const imx8mq_vpu_bus_sels[] = {"osc_25m", "sys1_pll_800m", "vpu_pll_out", "audio_pll2_out", "sys3_pll_out", "sys2_pll_1000m", "sys2_pll_200m", "sys1_pll_100m", };

static const char * const imx8mq_disp_axi_sels[] = {"osc_25m", "sys2_pll_125m", "sys1_pll_800m", "sys3_pll_out", "sys1_pll_400m", "audio_pll2_out", "clk_ext1", "clk_ext4", };

static const char * const imx8mq_disp_apb_sels[] = {"osc_25m", "sys2_pll_125m", "sys1_pll_800m", "sys3_pll_out",
					     "sys1_pll_40m", "audio_pll2_out", "clk_ext1", "clk_ext3", };

static const char * const imx8mq_disp_rtrm_sels[] = {"osc_25m", "sys1_pll_800m", "sys2_pll_200m", "sys1_pll_400m",
					      "audio_pll1_out", "video_pll1_out", "clk_ext2", "clk_ext3", };

static const char * const imx8mq_usb_bus_sels[] = {"osc_25m", "sys2_pll_500m", "sys1_pll_800m", "sys2_pll_100m",
					    "sys2_pll_200m", "clk_ext2", "clk_ext4", "audio_pll2_out", };

static const char * const imx8mq_gpu_axi_sels[] = {"osc_25m", "sys1_pll_800m", "gpu_pll_out", "sys3_pll_out", "sys2_pll_1000m",
					    "audio_pll1_out", "video_pll1_out", "audio_pll2_out", };

static const char * const imx8mq_gpu_ahb_sels[] = {"osc_25m", "sys1_pll_800m", "gpu_pll_out", "sys3_pll_out", "sys2_pll_1000m",
					    "audio_pll1_out", "video_pll1_out", "audio_pll2_out", };

static const char * const imx8mq_noc_sels[] = {"osc_25m", "sys1_pll_800m", "sys3_pll_out", "sys2_pll_1000m", "sys2_pll_500m",
					"audio_pll1_out", "video_pll1_out", "audio_pll2_out", };

static const char * const imx8mq_noc_apb_sels[] = {"osc_25m", "sys1_pll_400m", "sys3_pll_out", "sys2_pll_333m", "sys2_pll_200m",
					    "sys1_pll_800m", "audio_pll1_out", "video_pll1_out", };

static const char * const imx8mq_ahb_sels[] = {"osc_25m", "sys1_pll_133m", "sys1_pll_800m", "sys1_pll_400m",
					"sys2_pll_125m", "sys3_pll_out", "audio_pll1_out", "video_pll1_out", };

static const char * const imx8mq_audio_ahb_sels[] = {"osc_25m", "sys2_pll_500m", "sys1_pll_800m", "sys2_pll_1000m",
						  "sys2_pll_166m", "sys3_pll_out", "audio_pll1_out", "video_pll1_out", };

static const char * const imx8mq_dsi_ahb_sels[] = {"osc_25m", "sys2_pll_100m", "sys1_pll_80m", "sys1_pll_800m",
						"sys2_pll_1000m", "sys3_pll_out", "clk_ext3", "audio_pll2_out"};

static const char * const imx8mq_dram_alt_sels[] = {"osc_25m", "sys1_pll_800m", "sys1_pll_100m", "sys2_pll_500m",
						"sys2_pll_250m", "sys1_pll_400m", "audio_pll1_out", "sys1_pll_266m", };

static const char * const imx8mq_dram_apb_sels[] = {"osc_25m", "sys2_pll_200m", "sys1_pll_40m", "sys1_pll_160m",
						"sys1_pll_800m", "sys3_pll_out", "sys2_pll_250m", "audio_pll2_out", };

static const char * const imx8mq_vpu_g1_sels[] = {"osc_25m", "vpu_pll_out", "sys1_pll_800m", "sys2_pll_1000m", "sys1_pll_100m", "sys2_pll_125m", "sys3_pll_out", "audio_pll1_out", };

static const char * const imx8mq_vpu_g2_sels[] = {"osc_25m", "vpu_pll_out", "sys1_pll_800m", "sys2_pll_1000m", "sys1_pll_100m", "sys2_pll_125m", "sys3_pll_out", "audio_pll1_out", };

static const char * const imx8mq_disp_dtrc_sels[] = {"osc_25m", "vpu_pll_out", "sys1_pll_800m", "sys2_pll_1000m", "sys1_pll_160m", "sys2_pll_100m", "sys3_pll_out", "audio_pll2_out", };

static const char * const imx8mq_disp_dc8000_sels[] = {"osc_25m", "vpu_pll_out", "sys1_pll_800m", "sys2_pll_1000m", "sys1_pll_160m", "sys2_pll_100m", "sys3_pll_out", "audio_pll2_out", };

static const char * const imx8mq_pcie1_ctrl_sels[] = {"osc_25m", "sys2_pll_250m", "sys2_pll_200m", "sys1_pll_266m",
					       "sys1_pll_800m", "sys2_pll_500m", "sys2_pll_250m", "sys3_pll_out", };

static const char * const imx8mq_pcie1_phy_sels[] = {"osc_25m", "sys2_pll_100m", "sys2_pll_500m", "clk_ext1", "clk_ext2",
					      "clk_ext3", "clk_ext4", };

static const char * const imx8mq_pcie1_aux_sels[] = {"osc_25m", "sys2_pll_200m", "sys2_pll_500m", "sys3_pll_out",
					      "sys2_pll_100m", "sys1_pll_80m", "sys1_pll_160m", "sys1_pll_200m", };

static const char * const imx8mq_dc_pixel_sels[] = {"osc_25m", "video_pll1_out", "audio_pll2_out", "audio_pll1_out", "sys1_pll_800m", "sys2_pll_1000m", "sys3_pll_out", "clk_ext4", };

static const char * const imx8mq_lcdif_pixel_sels[] = {"osc_25m", "video_pll1_out", "audio_pll2_out", "audio_pll1_out", "sys1_pll_800m", "sys2_pll_1000m", "sys3_pll_out", "clk_ext4", };

static const char * const imx8mq_sai1_sels[] = {"osc_25m", "audio_pll1_out", "audio_pll2_out", "video_pll1_out", "sys1_pll_133m", "osc_27m", "clk_ext1", "clk_ext2", };

static const char * const imx8mq_sai2_sels[] = {"osc_25m", "audio_pll1_out", "audio_pll2_out", "video_pll1_out", "sys1_pll_133m", "osc_27m", "clk_ext2", "clk_ext3", };

static const char * const imx8mq_sai3_sels[] = {"osc_25m", "audio_pll1_out", "audio_pll2_out", "video_pll1_out", "sys1_pll_133m", "osc_27m", "clk_ext3", "clk_ext4", };

static const char * const imx8mq_sai4_sels[] = {"osc_25m", "audio_pll1_out", "audio_pll2_out", "video_pll1_out", "sys1_pll_133m", "osc_27m", "clk_ext1", "clk_ext2", };

static const char * const imx8mq_sai5_sels[] = {"osc_25m", "audio_pll1_out", "audio_pll2_out", "video_pll1_out", "sys1_pll_133m", "osc_27m", "clk_ext2", "clk_ext3", };

static const char * const imx8mq_sai6_sels[] = {"osc_25m", "audio_pll1_out", "audio_pll2_out", "video_pll1_out", "sys1_pll_133m", "osc_27m", "clk_ext3", "clk_ext4", };

static const char * const imx8mq_spdif1_sels[] = {"osc_25m", "audio_pll1_out", "audio_pll2_out", "video_pll1_out", "sys1_pll_133m", "osc_27m", "clk_ext2", "clk_ext3", };

static const char * const imx8mq_spdif2_sels[] = {"osc_25m", "audio_pll1_out", "audio_pll2_out", "video_pll1_out", "sys1_pll_133m", "osc_27m", "clk_ext3", "clk_ext4", };

static const char * const imx8mq_enet_ref_sels[] = {"osc_25m", "sys2_pll_125m", "sys2_pll_500m", "sys2_pll_100m",
					     "sys1_pll_160m", "audio_pll1_out", "video_pll1_out", "clk_ext4", };

static const char * const imx8mq_enet_timer_sels[] = {"osc_25m", "sys2_pll_100m", "audio_pll1_out", "clk_ext1", "clk_ext2",
					       "clk_ext3", "clk_ext4", "video_pll1_out", };

static const char * const imx8mq_enet_phy_sels[] = {"osc_25m", "sys2_pll_50m", "sys2_pll_125m", "sys2_pll_500m",
					     "audio_pll1_out", "video_pll1_out", "audio_pll2_out", };

static const char * const imx8mq_nand_sels[] = {"osc_25m", "sys2_pll_500m", "audio_pll1_out", "sys1_pll_400m",
					 "audio_pll2_out", "sys3_pll_out", "sys2_pll_250m", "video_pll1_out", };

static const char * const imx8mq_qspi_sels[] = {"osc_25m", "sys1_pll_400m", "sys1_pll_800m", "sys2_pll_500m",
					 "audio_pll2_out", "sys1_pll_266m", "sys3_pll_out", "sys1_pll_100m", };

static const char * const imx8mq_usdhc1_sels[] = {"osc_25m", "sys1_pll_400m", "sys1_pll_800m", "sys2_pll_500m",
					 "sys3_pll_out", "sys1_pll_266m", "audio_pll2_out", "sys1_pll_100m", };

static const char * const imx8mq_usdhc2_sels[] = {"osc_25m", "sys1_pll_400m", "sys1_pll_800m", "sys2_pll_500m",
					 "sys3_pll_out", "sys1_pll_266m", "audio_pll2_out", "sys1_pll_100m", };

static const char * const imx8mq_i2c1_sels[] = {"osc_25m", "sys1_pll_160m", "sys2_pll_50m", "sys3_pll_out", "audio_pll1_out",
					 "video_pll1_out", "audio_pll2_out", "sys1_pll_133m", };

static const char * const imx8mq_i2c2_sels[] = {"osc_25m", "sys1_pll_160m", "sys2_pll_50m", "sys3_pll_out", "audio_pll1_out",
					 "video_pll1_out", "audio_pll2_out", "sys1_pll_133m", };

static const char * const imx8mq_i2c3_sels[] = {"osc_25m", "sys1_pll_160m", "sys2_pll_50m", "sys3_pll_out", "audio_pll1_out",
					 "video_pll1_out", "audio_pll2_out", "sys1_pll_133m", };

static const char * const imx8mq_i2c4_sels[] = {"osc_25m", "sys1_pll_160m", "sys2_pll_50m", "sys3_pll_out", "audio_pll1_out",
					 "video_pll1_out", "audio_pll2_out", "sys1_pll_133m", };

static const char * const imx8mq_uart1_sels[] = {"osc_25m", "sys1_pll_80m", "sys2_pll_200m", "sys2_pll_100m",
					  "sys3_pll_out", "clk_ext2", "clk_ext4", "audio_pll2_out", };

static const char * const imx8mq_uart2_sels[] = {"osc_25m", "sys1_pll_80m", "sys2_pll_200m", "sys2_pll_100m",
					  "sys3_pll_out", "clk_ext2", "clk_ext3", "audio_pll2_out", };

static const char * const imx8mq_uart3_sels[] = {"osc_25m", "sys1_pll_80m", "sys2_pll_200m", "sys2_pll_100m",
					  "sys3_pll_out", "clk_ext2", "clk_ext4", "audio_pll2_out", };

static const char * const imx8mq_uart4_sels[] = {"osc_25m", "sys1_pll_80m", "sys2_pll_200m", "sys2_pll_100m",
					  "sys3_pll_out", "clk_ext2", "clk_ext3", "audio_pll2_out", };

static const char * const imx8mq_usb_core_sels[] = {"osc_25m", "sys1_pll_100m", "sys1_pll_40m", "sys2_pll_100m",
					     "sys2_pll_200m", "clk_ext2", "clk_ext3", "audio_pll2_out", };

static const char * const imx8mq_usb_phy_sels[] = {"osc_25m", "sys1_pll_100m", "sys1_pll_40m", "sys2_pll_100m",
					     "sys2_pll_200m", "clk_ext2", "clk_ext3", "audio_pll2_out", };

static const char * const imx8mq_gic_sels[] = {"osc_25m", "sys2_pll_200m", "sys1_pll_40m", "sys2_pll_100m",
					       "sys2_pll_200m", "clk_ext2", "clk_ext3", "audio_pll2_out" };

static const char * const imx8mq_ecspi1_sels[] = {"osc_25m", "sys2_pll_200m", "sys1_pll_40m", "sys1_pll_160m",
					   "sys1_pll_800m", "sys3_pll_out", "sys2_pll_250m", "audio_pll2_out", };

static const char * const imx8mq_ecspi2_sels[] = {"osc_25m", "sys2_pll_200m", "sys1_pll_40m", "sys1_pll_160m",
					   "sys1_pll_800m", "sys3_pll_out", "sys2_pll_250m", "audio_pll2_out", };

static const char * const imx8mq_pwm1_sels[] = {"osc_25m", "sys2_pll_100m", "sys1_pll_160m", "sys1_pll_40m",
					 "sys3_pll_out", "clk_ext1", "sys1_pll_80m", "video_pll1_out", };

static const char * const imx8mq_pwm2_sels[] = {"osc_25m", "sys2_pll_100m", "sys1_pll_160m", "sys1_pll_40m",
					 "sys3_pll_out", "clk_ext1", "sys1_pll_80m", "video_pll1_out", };

static const char * const imx8mq_pwm3_sels[] = {"osc_25m", "sys2_pll_100m", "sys1_pll_160m", "sys1_pll_40m",
					 "sys3_pll_out", "clk_ext2", "sys1_pll_80m", "video_pll1_out", };

static const char * const imx8mq_pwm4_sels[] = {"osc_25m", "sys2_pll_100m", "sys1_pll_160m", "sys1_pll_40m",
					 "sys3_pll_out", "clk_ext2", "sys1_pll_80m", "video_pll1_out", };

static const char * const imx8mq_gpt1_sels[] = {"osc_25m", "sys2_pll_100m", "sys1_pll_400m", "sys1_pll_40m",
					 "sys1_pll_80m", "audio_pll1_out", "clk_ext1", };

static const char * const imx8mq_wdog_sels[] = {"osc_25m", "sys1_pll_133m", "sys1_pll_160m", "vpu_pll_out",
					 "sys2_pll_125m", "sys3_pll_out", "sys1_pll_80m", "sys2_pll_166m", };

static const char * const imx8mq_wrclk_sels[] = {"osc_25m", "sys1_pll_40m", "vpu_pll_out", "sys3_pll_out", "sys2_pll_200m",
					  "sys1_pll_266m", "sys2_pll_500m", "sys1_pll_100m", };

static const char * const imx8mq_dsi_core_sels[] = {"osc_25m", "sys1_pll_266m", "sys2_pll_250m", "sys1_pll_800m",
					     "sys2_pll_1000m", "sys3_pll_out", "audio_pll2_out", "video_pll1_out", };

static const char * const imx8mq_dsi_phy_sels[] = {"osc_25m", "sys2_pll_125m", "sys2_pll_100m", "sys1_pll_800m",
					    "sys2_pll_1000m", "clk_ext2", "audio_pll2_out", "video_pll1_out", };

static const char * const imx8mq_dsi_dbi_sels[] = {"osc_25m", "sys1_pll_266m", "sys2_pll_100m", "sys1_pll_800m",
					    "sys2_pll_1000m", "sys3_pll_out", "audio_pll2_out", "video_pll1_out", };

static const char * const imx8mq_dsi_esc_sels[] = {"osc_25m", "sys2_pll_100m", "sys1_pll_80m", "sys1_pll_800m",
					    "sys2_pll_1000m", "sys3_pll_out", "clk_ext3", "audio_pll2_out", };

static const char * const imx8mq_csi1_core_sels[] = {"osc_25m", "sys1_pll_266m", "sys2_pll_250m", "sys1_pll_800m",
					      "sys2_pll_1000m", "sys3_pll_out", "audio_pll2_out", "video_pll1_out", };

static const char * const imx8mq_csi1_phy_sels[] = {"osc_25m", "sys2_pll_125m", "sys2_pll_100m", "sys1_pll_800m",
					     "sys2_pll_1000m", "clk_ext2", "audio_pll2_out", "video_pll1_out", };

static const char * const imx8mq_csi1_esc_sels[] = {"osc_25m", "sys2_pll_100m", "sys1_pll_80m", "sys1_pll_800m",
					     "sys2_pll_1000m", "sys3_pll_out", "clk_ext3", "audio_pll2_out", };

static const char * const imx8mq_csi2_core_sels[] = {"osc_25m", "sys1_pll_266m", "sys2_pll_250m", "sys1_pll_800m",
					      "sys2_pll_1000m", "sys3_pll_out", "audio_pll2_out", "video_pll1_out", };

static const char * const imx8mq_csi2_phy_sels[] = {"osc_25m", "sys2_pll_125m", "sys2_pll_100m", "sys1_pll_800m",
					     "sys2_pll_1000m", "clk_ext2", "audio_pll2_out", "video_pll1_out", };

static const char * const imx8mq_csi2_esc_sels[] = {"osc_25m", "sys2_pll_100m", "sys1_pll_80m", "sys1_pll_800m",
					     "sys2_pll_1000m", "sys3_pll_out", "clk_ext3", "audio_pll2_out", };

static const char * const imx8mq_pcie2_ctrl_sels[] = {"osc_25m", "sys2_pll_250m", "sys2_pll_200m", "sys1_pll_266m",
					       "sys1_pll_800m", "sys2_pll_500m", "sys2_pll_333m", "sys3_pll_out", };

static const char * const imx8mq_pcie2_phy_sels[] = {"osc_25m", "sys2_pll_100m", "sys2_pll_500m", "clk_ext1",
					      "clk_ext2", "clk_ext3", "clk_ext4", "sys1_pll_400m", };

static const char * const imx8mq_pcie2_aux_sels[] = {"osc_25m", "sys2_pll_200m", "sys2_pll_50m", "sys3_pll_out",
					      "sys2_pll_100m", "sys1_pll_80m", "sys1_pll_160m", "sys1_pll_200m", };

static const char * const imx8mq_ecspi3_sels[] = {"osc_25m", "sys2_pll_200m", "sys1_pll_40m", "sys1_pll_160m",
					   "sys1_pll_800m", "sys3_pll_out", "sys2_pll_250m", "audio_pll2_out", };
static const char * const imx8mq_dram_core_sels[] = {"dram_pll_out", "dram_alt_root", };

static const char * const imx8mq_clko1_sels[] = {"osc_25m", "sys1_pll_800m", "osc_27m", "sys1_pll_200m",
					  "audio_pll2_out", "sys2_pll_500m", "vpu_pll_out", "sys1_pll_80m", };
static const char * const imx8mq_clko2_sels[] = {"osc_25m", "sys2_pll_200m", "sys1_pll_400m", "sys2_pll_166m",
					  "sys3_pll_out", "audio_pll1_out", "video_pll1_out", "ckil", };

static struct clk_hw_onecell_data *clk_hw_data;
static struct clk_hw **hws;

static int imx8mq_clocks_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	void __iomem *base;
	int err;

	clk_hw_data = kzalloc(struct_size(clk_hw_data, hws,
					  IMX8MQ_CLK_END), GFP_KERNEL);
	if (WARN_ON(!clk_hw_data))
		return -ENOMEM;

	clk_hw_data->num = IMX8MQ_CLK_END;
	hws = clk_hw_data->hws;

	hws[IMX8MQ_CLK_DUMMY] = imx_clk_hw_fixed("dummy", 0);
	hws[IMX8MQ_CLK_32K] = imx_obtain_fixed_clk_hw(np, "ckil");
	hws[IMX8MQ_CLK_25M] = imx_obtain_fixed_clk_hw(np, "osc_25m");
	hws[IMX8MQ_CLK_27M] = imx_obtain_fixed_clk_hw(np, "osc_27m");
	hws[IMX8MQ_CLK_EXT1] = imx_obtain_fixed_clk_hw(np, "clk_ext1");
	hws[IMX8MQ_CLK_EXT2] = imx_obtain_fixed_clk_hw(np, "clk_ext2");
	hws[IMX8MQ_CLK_EXT3] = imx_obtain_fixed_clk_hw(np, "clk_ext3");
	hws[IMX8MQ_CLK_EXT4] = imx_obtain_fixed_clk_hw(np, "clk_ext4");

	np = of_find_compatible_node(NULL, NULL, "fsl,imx8mq-anatop");
	base = of_iomap(np, 0);
	of_node_put(np);
	if (WARN_ON(!base))
		return -ENOMEM;

	hws[IMX8MQ_ARM_PLL_REF_SEL] = imx_clk_hw_mux("arm_pll_ref_sel", base + 0x28, 16, 2, pll_ref_sels, ARRAY_SIZE(pll_ref_sels));
	hws[IMX8MQ_GPU_PLL_REF_SEL] = imx_clk_hw_mux("gpu_pll_ref_sel", base + 0x18, 16, 2, pll_ref_sels, ARRAY_SIZE(pll_ref_sels));
	hws[IMX8MQ_VPU_PLL_REF_SEL] = imx_clk_hw_mux("vpu_pll_ref_sel", base + 0x20, 16, 2, pll_ref_sels, ARRAY_SIZE(pll_ref_sels));
	hws[IMX8MQ_AUDIO_PLL1_REF_SEL] = imx_clk_hw_mux("audio_pll1_ref_sel", base + 0x0, 16, 2, pll_ref_sels, ARRAY_SIZE(pll_ref_sels));
	hws[IMX8MQ_AUDIO_PLL2_REF_SEL] = imx_clk_hw_mux("audio_pll2_ref_sel", base + 0x8, 16, 2, pll_ref_sels, ARRAY_SIZE(pll_ref_sels));
	hws[IMX8MQ_VIDEO_PLL1_REF_SEL] = imx_clk_hw_mux("video_pll1_ref_sel", base + 0x10, 16, 2, pll_ref_sels, ARRAY_SIZE(pll_ref_sels));
	hws[IMX8MQ_SYS3_PLL1_REF_SEL]	= imx_clk_hw_mux("sys3_pll1_ref_sel", base + 0x48, 0, 2, pll_ref_sels, ARRAY_SIZE(pll_ref_sels));
	hws[IMX8MQ_DRAM_PLL1_REF_SEL]	= imx_clk_hw_mux("dram_pll1_ref_sel", base + 0x60, 0, 2, pll_ref_sels, ARRAY_SIZE(pll_ref_sels));
	hws[IMX8MQ_VIDEO2_PLL1_REF_SEL] = imx_clk_hw_mux("video2_pll1_ref_sel", base + 0x54, 0, 2, pll_ref_sels, ARRAY_SIZE(pll_ref_sels));

	hws[IMX8MQ_ARM_PLL_REF_DIV]	= imx_clk_hw_divider("arm_pll_ref_div", "arm_pll_ref_sel", base + 0x28, 5, 6);
	hws[IMX8MQ_GPU_PLL_REF_DIV]	= imx_clk_hw_divider("gpu_pll_ref_div", "gpu_pll_ref_sel", base + 0x18, 5, 6);
	hws[IMX8MQ_VPU_PLL_REF_DIV]	= imx_clk_hw_divider("vpu_pll_ref_div", "vpu_pll_ref_sel", base + 0x20, 5, 6);
	hws[IMX8MQ_AUDIO_PLL1_REF_DIV] = imx_clk_hw_divider("audio_pll1_ref_div", "audio_pll1_ref_sel", base + 0x0, 5, 6);
	hws[IMX8MQ_AUDIO_PLL2_REF_DIV] = imx_clk_hw_divider("audio_pll2_ref_div", "audio_pll2_ref_sel", base + 0x8, 5, 6);
	hws[IMX8MQ_VIDEO_PLL1_REF_DIV] = imx_clk_hw_divider("video_pll1_ref_div", "video_pll1_ref_sel", base + 0x10, 5, 6);

	hws[IMX8MQ_ARM_PLL] = imx_clk_hw_frac_pll("arm_pll", "arm_pll_ref_div", base + 0x28);
	hws[IMX8MQ_GPU_PLL] = imx_clk_hw_frac_pll("gpu_pll", "gpu_pll_ref_div", base + 0x18);
	hws[IMX8MQ_VPU_PLL] = imx_clk_hw_frac_pll("vpu_pll", "vpu_pll_ref_div", base + 0x20);
	hws[IMX8MQ_AUDIO_PLL1] = imx_clk_hw_frac_pll("audio_pll1", "audio_pll1_ref_div", base + 0x0);
	hws[IMX8MQ_AUDIO_PLL2] = imx_clk_hw_frac_pll("audio_pll2", "audio_pll2_ref_div", base + 0x8);
	hws[IMX8MQ_VIDEO_PLL1] = imx_clk_hw_frac_pll("video_pll1", "video_pll1_ref_div", base + 0x10);

	/* PLL bypass out */
	hws[IMX8MQ_ARM_PLL_BYPASS] = imx_clk_hw_mux_flags("arm_pll_bypass", base + 0x28, 14, 1, arm_pll_bypass_sels, ARRAY_SIZE(arm_pll_bypass_sels), CLK_SET_RATE_PARENT);
	hws[IMX8MQ_GPU_PLL_BYPASS] = imx_clk_hw_mux("gpu_pll_bypass", base + 0x18, 14, 1, gpu_pll_bypass_sels, ARRAY_SIZE(gpu_pll_bypass_sels));
	hws[IMX8MQ_VPU_PLL_BYPASS] = imx_clk_hw_mux("vpu_pll_bypass", base + 0x20, 14, 1, vpu_pll_bypass_sels, ARRAY_SIZE(vpu_pll_bypass_sels));
	hws[IMX8MQ_AUDIO_PLL1_BYPASS] = imx_clk_hw_mux("audio_pll1_bypass", base + 0x0, 14, 1, audio_pll1_bypass_sels, ARRAY_SIZE(audio_pll1_bypass_sels));
	hws[IMX8MQ_AUDIO_PLL2_BYPASS] = imx_clk_hw_mux("audio_pll2_bypass", base + 0x8, 14, 1, audio_pll2_bypass_sels, ARRAY_SIZE(audio_pll2_bypass_sels));
	hws[IMX8MQ_VIDEO_PLL1_BYPASS] = imx_clk_hw_mux("video_pll1_bypass", base + 0x10, 14, 1, video_pll1_bypass_sels, ARRAY_SIZE(video_pll1_bypass_sels));

	/* PLL OUT GATE */
	hws[IMX8MQ_ARM_PLL_OUT] = imx_clk_hw_gate("arm_pll_out", "arm_pll_bypass", base + 0x28, 21);
	hws[IMX8MQ_GPU_PLL_OUT] = imx_clk_hw_gate("gpu_pll_out", "gpu_pll_bypass", base + 0x18, 21);
	hws[IMX8MQ_VPU_PLL_OUT] = imx_clk_hw_gate("vpu_pll_out", "vpu_pll_bypass", base + 0x20, 21);
	hws[IMX8MQ_AUDIO_PLL1_OUT] = imx_clk_hw_gate("audio_pll1_out", "audio_pll1_bypass", base + 0x0, 21);
	hws[IMX8MQ_AUDIO_PLL2_OUT] = imx_clk_hw_gate("audio_pll2_out", "audio_pll2_bypass", base + 0x8, 21);
	hws[IMX8MQ_VIDEO_PLL1_OUT] = imx_clk_hw_gate("video_pll1_out", "video_pll1_bypass", base + 0x10, 21);

	hws[IMX8MQ_SYS1_PLL_OUT] = imx_clk_hw_fixed("sys1_pll_out", 800000000);
	hws[IMX8MQ_SYS2_PLL_OUT] = imx_clk_hw_fixed("sys2_pll_out", 1000000000);
	hws[IMX8MQ_SYS3_PLL_OUT] = imx_clk_hw_sscg_pll("sys3_pll_out", sys3_pll_out_sels, ARRAY_SIZE(sys3_pll_out_sels), 0, 0, 0, base + 0x48, CLK_IS_CRITICAL);
	hws[IMX8MQ_DRAM_PLL_OUT] = imx_clk_hw_sscg_pll("dram_pll_out", dram_pll_out_sels, ARRAY_SIZE(dram_pll_out_sels), 0, 0, 0, base + 0x60, CLK_IS_CRITICAL | CLK_GET_RATE_NOCACHE);
	hws[IMX8MQ_VIDEO2_PLL_OUT] = imx_clk_hw_sscg_pll("video2_pll_out", video2_pll_out_sels, ARRAY_SIZE(video2_pll_out_sels), 0, 0, 0, base + 0x54, 0);

	/* SYS PLL1 fixed output */
	hws[IMX8MQ_SYS1_PLL_40M] = imx_clk_hw_fixed_factor("sys1_pll_40m", "sys1_pll_out", 1, 20);
	hws[IMX8MQ_SYS1_PLL_80M] = imx_clk_hw_fixed_factor("sys1_pll_80m", "sys1_pll_out", 1, 10);
	hws[IMX8MQ_SYS1_PLL_100M] = imx_clk_hw_fixed_factor("sys1_pll_100m", "sys1_pll_out", 1, 8);
	hws[IMX8MQ_SYS1_PLL_133M] = imx_clk_hw_fixed_factor("sys1_pll_133m", "sys1_pll_out", 1, 6);
	hws[IMX8MQ_SYS1_PLL_160M] = imx_clk_hw_fixed_factor("sys1_pll_160m", "sys1_pll_out", 1, 5);
	hws[IMX8MQ_SYS1_PLL_200M] = imx_clk_hw_fixed_factor("sys1_pll_200m", "sys1_pll_out", 1, 4);
	hws[IMX8MQ_SYS1_PLL_266M] = imx_clk_hw_fixed_factor("sys1_pll_266m", "sys1_pll_out", 1, 3);
	hws[IMX8MQ_SYS1_PLL_400M] = imx_clk_hw_fixed_factor("sys1_pll_400m", "sys1_pll_out", 1, 2);
	hws[IMX8MQ_SYS1_PLL_800M] = imx_clk_hw_fixed_factor("sys1_pll_800m", "sys1_pll_out", 1, 1);

	/* SYS PLL2 fixed output */
	hws[IMX8MQ_SYS2_PLL_50M] = imx_clk_hw_fixed_factor("sys2_pll_50m", "sys2_pll_out", 1, 20);
	hws[IMX8MQ_SYS2_PLL_100M] = imx_clk_hw_fixed_factor("sys2_pll_100m", "sys2_pll_out", 1, 10);
	hws[IMX8MQ_SYS2_PLL_125M] = imx_clk_hw_fixed_factor("sys2_pll_125m", "sys2_pll_out", 1, 8);
	hws[IMX8MQ_SYS2_PLL_166M] = imx_clk_hw_fixed_factor("sys2_pll_166m", "sys2_pll_out", 1, 6);
	hws[IMX8MQ_SYS2_PLL_200M] = imx_clk_hw_fixed_factor("sys2_pll_200m", "sys2_pll_out", 1, 5);
	hws[IMX8MQ_SYS2_PLL_250M] = imx_clk_hw_fixed_factor("sys2_pll_250m", "sys2_pll_out", 1, 4);
	hws[IMX8MQ_SYS2_PLL_333M] = imx_clk_hw_fixed_factor("sys2_pll_333m", "sys2_pll_out", 1, 3);
	hws[IMX8MQ_SYS2_PLL_500M] = imx_clk_hw_fixed_factor("sys2_pll_500m", "sys2_pll_out", 1, 2);
	hws[IMX8MQ_SYS2_PLL_1000M] = imx_clk_hw_fixed_factor("sys2_pll_1000m", "sys2_pll_out", 1, 1);

	np = dev->of_node;
	base = devm_platform_ioremap_resource(pdev, 0);
	if (WARN_ON(IS_ERR(base)))
		return PTR_ERR(base);

	/* CORE */
	hws[IMX8MQ_CLK_A53_DIV] = imx8m_clk_hw_composite_core("arm_a53_div", imx8mq_a53_sels, base + 0x8000);
	hws[IMX8MQ_CLK_A53_CG] = hws[IMX8MQ_CLK_A53_DIV];
	hws[IMX8MQ_CLK_A53_SRC] = hws[IMX8MQ_CLK_A53_DIV];

	hws[IMX8MQ_CLK_M4_CORE] = imx8m_clk_hw_composite_core("arm_m4_core", imx8mq_arm_m4_sels, base + 0x8080);
	hws[IMX8MQ_CLK_VPU_CORE] = imx8m_clk_hw_composite_core("vpu_core", imx8mq_vpu_sels, base + 0x8100);
	hws[IMX8MQ_CLK_GPU_CORE] = imx8m_clk_hw_composite_core("gpu_core", imx8mq_gpu_core_sels, base + 0x8180);
	hws[IMX8MQ_CLK_GPU_SHADER] = imx8m_clk_hw_composite("gpu_shader", imx8mq_gpu_shader_sels, base + 0x8200);
	/* For backwards compatibility */
	hws[IMX8MQ_CLK_M4_SRC] = hws[IMX8MQ_CLK_M4_CORE];
	hws[IMX8MQ_CLK_M4_CG] = hws[IMX8MQ_CLK_M4_CORE];
	hws[IMX8MQ_CLK_M4_DIV] = hws[IMX8MQ_CLK_M4_CORE];
	hws[IMX8MQ_CLK_VPU_SRC] = hws[IMX8MQ_CLK_VPU_CORE];
	hws[IMX8MQ_CLK_VPU_CG] = hws[IMX8MQ_CLK_VPU_CORE];
	hws[IMX8MQ_CLK_VPU_DIV] = hws[IMX8MQ_CLK_VPU_CORE];
	hws[IMX8MQ_CLK_GPU_CORE_SRC] = hws[IMX8MQ_CLK_GPU_CORE];
	hws[IMX8MQ_CLK_GPU_CORE_CG] = hws[IMX8MQ_CLK_GPU_CORE];
	hws[IMX8MQ_CLK_GPU_CORE_DIV] = hws[IMX8MQ_CLK_GPU_CORE];
	hws[IMX8MQ_CLK_GPU_SHADER_SRC] = hws[IMX8MQ_CLK_GPU_SHADER];
	hws[IMX8MQ_CLK_GPU_SHADER_CG] = hws[IMX8MQ_CLK_GPU_SHADER];
	hws[IMX8MQ_CLK_GPU_SHADER_DIV] = hws[IMX8MQ_CLK_GPU_SHADER];

	/* CORE SEL */
	hws[IMX8MQ_CLK_A53_CORE] = imx_clk_hw_mux2("arm_a53_core", base + 0x9880, 24, 1, imx8mq_a53_core_sels, ARRAY_SIZE(imx8mq_a53_core_sels));

	/* BUS */
	hws[IMX8MQ_CLK_MAIN_AXI] = imx8m_clk_hw_composite_bus_critical("main_axi", imx8mq_main_axi_sels, base + 0x8800);
	hws[IMX8MQ_CLK_ENET_AXI] = imx8m_clk_hw_composite_bus("enet_axi", imx8mq_enet_axi_sels, base + 0x8880);
	hws[IMX8MQ_CLK_NAND_USDHC_BUS] = imx8m_clk_hw_composite_bus("nand_usdhc_bus", imx8mq_nand_usdhc_sels, base + 0x8900);
	hws[IMX8MQ_CLK_VPU_BUS] = imx8m_clk_hw_composite_bus("vpu_bus", imx8mq_vpu_bus_sels, base + 0x8980);
	hws[IMX8MQ_CLK_DISP_AXI] = imx8m_clk_hw_composite_bus("disp_axi", imx8mq_disp_axi_sels, base + 0x8a00);
	hws[IMX8MQ_CLK_DISP_APB] = imx8m_clk_hw_composite_bus("disp_apb", imx8mq_disp_apb_sels, base + 0x8a80);
	hws[IMX8MQ_CLK_DISP_RTRM] = imx8m_clk_hw_composite_bus("disp_rtrm", imx8mq_disp_rtrm_sels, base + 0x8b00);
	hws[IMX8MQ_CLK_USB_BUS] = imx8m_clk_hw_composite_bus("usb_bus", imx8mq_usb_bus_sels, base + 0x8b80);
	hws[IMX8MQ_CLK_GPU_AXI] = imx8m_clk_hw_composite_bus("gpu_axi", imx8mq_gpu_axi_sels, base + 0x8c00);
	hws[IMX8MQ_CLK_GPU_AHB] = imx8m_clk_hw_composite_bus("gpu_ahb", imx8mq_gpu_ahb_sels, base + 0x8c80);
	hws[IMX8MQ_CLK_NOC] = imx8m_clk_hw_composite_bus_critical("noc", imx8mq_noc_sels, base + 0x8d00);
	hws[IMX8MQ_CLK_NOC_APB] = imx8m_clk_hw_composite_bus_critical("noc_apb", imx8mq_noc_apb_sels, base + 0x8d80);

	/* AHB */
	/* AHB clock is used by the AHB bus therefore marked as critical */
	hws[IMX8MQ_CLK_AHB] = imx8m_clk_hw_composite_bus_critical("ahb", imx8mq_ahb_sels, base + 0x9000);
	hws[IMX8MQ_CLK_AUDIO_AHB] = imx8m_clk_hw_composite_bus("audio_ahb", imx8mq_audio_ahb_sels, base + 0x9100);

	/* IPG */
	hws[IMX8MQ_CLK_IPG_ROOT] = imx_clk_hw_divider2("ipg_root", "ahb", base + 0x9080, 0, 1);
	hws[IMX8MQ_CLK_IPG_AUDIO_ROOT] = imx_clk_hw_divider2("ipg_audio_root", "audio_ahb", base + 0x9180, 0, 1);

	/*
	 * DRAM clocks are manipulated from TF-A outside clock framework.
	 * The fw_managed helper sets GET_RATE_NOCACHE and clears SET_PARENT_GATE
	 * as div value should always be read from hardware
	 */
	hws[IMX8MQ_CLK_DRAM_CORE] = imx_clk_hw_mux2_flags("dram_core_clk", base + 0x9800, 24, 1, imx8mq_dram_core_sels, ARRAY_SIZE(imx8mq_dram_core_sels), CLK_IS_CRITICAL);
	hws[IMX8MQ_CLK_DRAM_ALT] = imx8m_clk_hw_fw_managed_composite("dram_alt", imx8mq_dram_alt_sels, base + 0xa000);
	hws[IMX8MQ_CLK_DRAM_APB] = imx8m_clk_hw_fw_managed_composite_critical("dram_apb", imx8mq_dram_apb_sels, base + 0xa080);

	/* IP */
	hws[IMX8MQ_CLK_VPU_G1] = imx8m_clk_hw_composite("vpu_g1", imx8mq_vpu_g1_sels, base + 0xa100);
	hws[IMX8MQ_CLK_VPU_G2] = imx8m_clk_hw_composite("vpu_g2", imx8mq_vpu_g2_sels, base + 0xa180);
	hws[IMX8MQ_CLK_DISP_DTRC] = imx8m_clk_hw_composite("disp_dtrc", imx8mq_disp_dtrc_sels, base + 0xa200);
	hws[IMX8MQ_CLK_DISP_DC8000] = imx8m_clk_hw_composite("disp_dc8000", imx8mq_disp_dc8000_sels, base + 0xa280);
	hws[IMX8MQ_CLK_PCIE1_CTRL] = imx8m_clk_hw_composite("pcie1_ctrl", imx8mq_pcie1_ctrl_sels, base + 0xa300);
	hws[IMX8MQ_CLK_PCIE1_PHY] = imx8m_clk_hw_composite("pcie1_phy", imx8mq_pcie1_phy_sels, base + 0xa380);
	hws[IMX8MQ_CLK_PCIE1_AUX] = imx8m_clk_hw_composite("pcie1_aux", imx8mq_pcie1_aux_sels, base + 0xa400);
	hws[IMX8MQ_CLK_DC_PIXEL] = imx8m_clk_hw_composite("dc_pixel", imx8mq_dc_pixel_sels, base + 0xa480);
	hws[IMX8MQ_CLK_LCDIF_PIXEL] = imx8m_clk_hw_composite("lcdif_pixel", imx8mq_lcdif_pixel_sels, base + 0xa500);
	hws[IMX8MQ_CLK_SAI1] = imx8m_clk_hw_composite("sai1", imx8mq_sai1_sels, base + 0xa580);
	hws[IMX8MQ_CLK_SAI2] = imx8m_clk_hw_composite("sai2", imx8mq_sai2_sels, base + 0xa600);
	hws[IMX8MQ_CLK_SAI3] = imx8m_clk_hw_composite("sai3", imx8mq_sai3_sels, base + 0xa680);
	hws[IMX8MQ_CLK_SAI4] = imx8m_clk_hw_composite("sai4", imx8mq_sai4_sels, base + 0xa700);
	hws[IMX8MQ_CLK_SAI5] = imx8m_clk_hw_composite("sai5", imx8mq_sai5_sels, base + 0xa780);
	hws[IMX8MQ_CLK_SAI6] = imx8m_clk_hw_composite("sai6", imx8mq_sai6_sels, base + 0xa800);
	hws[IMX8MQ_CLK_SPDIF1] = imx8m_clk_hw_composite("spdif1", imx8mq_spdif1_sels, base + 0xa880);
	hws[IMX8MQ_CLK_SPDIF2] = imx8m_clk_hw_composite("spdif2", imx8mq_spdif2_sels, base + 0xa900);
	hws[IMX8MQ_CLK_ENET_REF] = imx8m_clk_hw_composite("enet_ref", imx8mq_enet_ref_sels, base + 0xa980);
	hws[IMX8MQ_CLK_ENET_TIMER] = imx8m_clk_hw_composite("enet_timer", imx8mq_enet_timer_sels, base + 0xaa00);
	hws[IMX8MQ_CLK_ENET_PHY_REF] = imx8m_clk_hw_composite("enet_phy", imx8mq_enet_phy_sels, base + 0xaa80);
	hws[IMX8MQ_CLK_NAND] = imx8m_clk_hw_composite("nand", imx8mq_nand_sels, base + 0xab00);
	hws[IMX8MQ_CLK_QSPI] = imx8m_clk_hw_composite("qspi", imx8mq_qspi_sels, base + 0xab80);
	hws[IMX8MQ_CLK_USDHC1] = imx8m_clk_hw_composite("usdhc1", imx8mq_usdhc1_sels, base + 0xac00);
	hws[IMX8MQ_CLK_USDHC2] = imx8m_clk_hw_composite("usdhc2", imx8mq_usdhc2_sels, base + 0xac80);
	hws[IMX8MQ_CLK_I2C1] = imx8m_clk_hw_composite("i2c1", imx8mq_i2c1_sels, base + 0xad00);
	hws[IMX8MQ_CLK_I2C2] = imx8m_clk_hw_composite("i2c2", imx8mq_i2c2_sels, base + 0xad80);
	hws[IMX8MQ_CLK_I2C3] = imx8m_clk_hw_composite("i2c3", imx8mq_i2c3_sels, base + 0xae00);
	hws[IMX8MQ_CLK_I2C4] = imx8m_clk_hw_composite("i2c4", imx8mq_i2c4_sels, base + 0xae80);
	hws[IMX8MQ_CLK_UART1] = imx8m_clk_hw_composite("uart1", imx8mq_uart1_sels, base + 0xaf00);
	hws[IMX8MQ_CLK_UART2] = imx8m_clk_hw_composite("uart2", imx8mq_uart2_sels, base + 0xaf80);
	hws[IMX8MQ_CLK_UART3] = imx8m_clk_hw_composite("uart3", imx8mq_uart3_sels, base + 0xb000);
	hws[IMX8MQ_CLK_UART4] = imx8m_clk_hw_composite("uart4", imx8mq_uart4_sels, base + 0xb080);
	hws[IMX8MQ_CLK_USB_CORE_REF] = imx8m_clk_hw_composite("usb_core_ref", imx8mq_usb_core_sels, base + 0xb100);
	hws[IMX8MQ_CLK_USB_PHY_REF] = imx8m_clk_hw_composite("usb_phy_ref", imx8mq_usb_phy_sels, base + 0xb180);
	hws[IMX8MQ_CLK_GIC] = imx8m_clk_hw_composite_critical("gic", imx8mq_gic_sels, base + 0xb200);
	hws[IMX8MQ_CLK_ECSPI1] = imx8m_clk_hw_composite("ecspi1", imx8mq_ecspi1_sels, base + 0xb280);
	hws[IMX8MQ_CLK_ECSPI2] = imx8m_clk_hw_composite("ecspi2", imx8mq_ecspi2_sels, base + 0xb300);
	hws[IMX8MQ_CLK_PWM1] = imx8m_clk_hw_composite("pwm1", imx8mq_pwm1_sels, base + 0xb380);
	hws[IMX8MQ_CLK_PWM2] = imx8m_clk_hw_composite("pwm2", imx8mq_pwm2_sels, base + 0xb400);
	hws[IMX8MQ_CLK_PWM3] = imx8m_clk_hw_composite("pwm3", imx8mq_pwm3_sels, base + 0xb480);
	hws[IMX8MQ_CLK_PWM4] = imx8m_clk_hw_composite("pwm4", imx8mq_pwm4_sels, base + 0xb500);
	hws[IMX8MQ_CLK_GPT1] = imx8m_clk_hw_composite("gpt1", imx8mq_gpt1_sels, base + 0xb580);
	hws[IMX8MQ_CLK_WDOG] = imx8m_clk_hw_composite("wdog", imx8mq_wdog_sels, base + 0xb900);
	hws[IMX8MQ_CLK_WRCLK] = imx8m_clk_hw_composite("wrclk", imx8mq_wrclk_sels, base + 0xb980);
	hws[IMX8MQ_CLK_CLKO1] = imx8m_clk_hw_composite("clko1", imx8mq_clko1_sels, base + 0xba00);
	hws[IMX8MQ_CLK_CLKO2] = imx8m_clk_hw_composite("clko2", imx8mq_clko2_sels, base + 0xba80);
	hws[IMX8MQ_CLK_DSI_CORE] = imx8m_clk_hw_composite("dsi_core", imx8mq_dsi_core_sels, base + 0xbb00);
	hws[IMX8MQ_CLK_DSI_PHY_REF] = imx8m_clk_hw_composite("dsi_phy_ref", imx8mq_dsi_phy_sels, base + 0xbb80);
	hws[IMX8MQ_CLK_DSI_DBI] = imx8m_clk_hw_composite("dsi_dbi", imx8mq_dsi_dbi_sels, base + 0xbc00);
	hws[IMX8MQ_CLK_DSI_ESC] = imx8m_clk_hw_composite("dsi_esc", imx8mq_dsi_esc_sels, base + 0xbc80);
	hws[IMX8MQ_CLK_DSI_AHB] = imx8m_clk_hw_composite("dsi_ahb", imx8mq_dsi_ahb_sels, base + 0x9200);
	hws[IMX8MQ_CLK_DSI_IPG_DIV] = imx_clk_hw_divider2("dsi_ipg_div", "dsi_ahb", base + 0x9280, 0, 6);
	hws[IMX8MQ_CLK_CSI1_CORE] = imx8m_clk_hw_composite("csi1_core", imx8mq_csi1_core_sels, base + 0xbd00);
	hws[IMX8MQ_CLK_CSI1_PHY_REF] = imx8m_clk_hw_composite("csi1_phy_ref", imx8mq_csi1_phy_sels, base + 0xbd80);
	hws[IMX8MQ_CLK_CSI1_ESC] = imx8m_clk_hw_composite("csi1_esc", imx8mq_csi1_esc_sels, base + 0xbe00);
	hws[IMX8MQ_CLK_CSI2_CORE] = imx8m_clk_hw_composite("csi2_core", imx8mq_csi2_core_sels, base + 0xbe80);
	hws[IMX8MQ_CLK_CSI2_PHY_REF] = imx8m_clk_hw_composite("csi2_phy_ref", imx8mq_csi2_phy_sels, base + 0xbf00);
	hws[IMX8MQ_CLK_CSI2_ESC] = imx8m_clk_hw_composite("csi2_esc", imx8mq_csi2_esc_sels, base + 0xbf80);
	hws[IMX8MQ_CLK_PCIE2_CTRL] = imx8m_clk_hw_composite("pcie2_ctrl", imx8mq_pcie2_ctrl_sels, base + 0xc000);
	hws[IMX8MQ_CLK_PCIE2_PHY] = imx8m_clk_hw_composite("pcie2_phy", imx8mq_pcie2_phy_sels, base + 0xc080);
	hws[IMX8MQ_CLK_PCIE2_AUX] = imx8m_clk_hw_composite("pcie2_aux", imx8mq_pcie2_aux_sels, base + 0xc100);
	hws[IMX8MQ_CLK_ECSPI3] = imx8m_clk_hw_composite("ecspi3", imx8mq_ecspi3_sels, base + 0xc180);

	hws[IMX8MQ_CLK_ECSPI1_ROOT] = imx_clk_hw_gate4("ecspi1_root_clk", "ecspi1", base + 0x4070, 0);
	hws[IMX8MQ_CLK_ECSPI2_ROOT] = imx_clk_hw_gate4("ecspi2_root_clk", "ecspi2", base + 0x4080, 0);
	hws[IMX8MQ_CLK_ECSPI3_ROOT] = imx_clk_hw_gate4("ecspi3_root_clk", "ecspi3", base + 0x4090, 0);
	hws[IMX8MQ_CLK_ENET1_ROOT] = imx_clk_hw_gate4("enet1_root_clk", "enet_axi", base + 0x40a0, 0);
	hws[IMX8MQ_CLK_GPIO1_ROOT] = imx_clk_hw_gate4("gpio1_root_clk", "ipg_root", base + 0x40b0, 0);
	hws[IMX8MQ_CLK_GPIO2_ROOT] = imx_clk_hw_gate4("gpio2_root_clk", "ipg_root", base + 0x40c0, 0);
	hws[IMX8MQ_CLK_GPIO3_ROOT] = imx_clk_hw_gate4("gpio3_root_clk", "ipg_root", base + 0x40d0, 0);
	hws[IMX8MQ_CLK_GPIO4_ROOT] = imx_clk_hw_gate4("gpio4_root_clk", "ipg_root", base + 0x40e0, 0);
	hws[IMX8MQ_CLK_GPIO5_ROOT] = imx_clk_hw_gate4("gpio5_root_clk", "ipg_root", base + 0x40f0, 0);
	hws[IMX8MQ_CLK_GPT1_ROOT] = imx_clk_hw_gate4("gpt1_root_clk", "gpt1", base + 0x4100, 0);
	hws[IMX8MQ_CLK_I2C1_ROOT] = imx_clk_hw_gate4("i2c1_root_clk", "i2c1", base + 0x4170, 0);
	hws[IMX8MQ_CLK_I2C2_ROOT] = imx_clk_hw_gate4("i2c2_root_clk", "i2c2", base + 0x4180, 0);
	hws[IMX8MQ_CLK_I2C3_ROOT] = imx_clk_hw_gate4("i2c3_root_clk", "i2c3", base + 0x4190, 0);
	hws[IMX8MQ_CLK_I2C4_ROOT] = imx_clk_hw_gate4("i2c4_root_clk", "i2c4", base + 0x41a0, 0);
	hws[IMX8MQ_CLK_MU_ROOT] = imx_clk_hw_gate4("mu_root_clk", "ipg_root", base + 0x4210, 0);
	hws[IMX8MQ_CLK_OCOTP_ROOT] = imx_clk_hw_gate4("ocotp_root_clk", "ipg_root", base + 0x4220, 0);
	hws[IMX8MQ_CLK_PCIE1_ROOT] = imx_clk_hw_gate4("pcie1_root_clk", "pcie1_ctrl", base + 0x4250, 0);
	hws[IMX8MQ_CLK_PCIE2_ROOT] = imx_clk_hw_gate4("pcie2_root_clk", "pcie2_ctrl", base + 0x4640, 0);
	hws[IMX8MQ_CLK_PWM1_ROOT] = imx_clk_hw_gate4("pwm1_root_clk", "pwm1", base + 0x4280, 0);
	hws[IMX8MQ_CLK_PWM2_ROOT] = imx_clk_hw_gate4("pwm2_root_clk", "pwm2", base + 0x4290, 0);
	hws[IMX8MQ_CLK_PWM3_ROOT] = imx_clk_hw_gate4("pwm3_root_clk", "pwm3", base + 0x42a0, 0);
	hws[IMX8MQ_CLK_PWM4_ROOT] = imx_clk_hw_gate4("pwm4_root_clk", "pwm4", base + 0x42b0, 0);
	hws[IMX8MQ_CLK_QSPI_ROOT] = imx_clk_hw_gate4("qspi_root_clk", "qspi", base + 0x42f0, 0);
	hws[IMX8MQ_CLK_RAWNAND_ROOT] = imx_clk_hw_gate2_shared2("nand_root_clk", "nand", base + 0x4300, 0, &share_count_nand);
	hws[IMX8MQ_CLK_NAND_USDHC_BUS_RAWNAND_CLK] = imx_clk_hw_gate2_shared2("nand_usdhc_rawnand_clk", "nand_usdhc_bus", base + 0x4300, 0, &share_count_nand);
	hws[IMX8MQ_CLK_SAI1_ROOT] = imx_clk_hw_gate2_shared2("sai1_root_clk", "sai1", base + 0x4330, 0, &share_count_sai1);
	hws[IMX8MQ_CLK_SAI1_IPG] = imx_clk_hw_gate2_shared2("sai1_ipg_clk", "ipg_audio_root", base + 0x4330, 0, &share_count_sai1);
	hws[IMX8MQ_CLK_SAI2_ROOT] = imx_clk_hw_gate2_shared2("sai2_root_clk", "sai2", base + 0x4340, 0, &share_count_sai2);
	hws[IMX8MQ_CLK_SAI2_IPG] = imx_clk_hw_gate2_shared2("sai2_ipg_clk", "ipg_root", base + 0x4340, 0, &share_count_sai2);
	hws[IMX8MQ_CLK_SAI3_ROOT] = imx_clk_hw_gate2_shared2("sai3_root_clk", "sai3", base + 0x4350, 0, &share_count_sai3);
	hws[IMX8MQ_CLK_SAI3_IPG] = imx_clk_hw_gate2_shared2("sai3_ipg_clk", "ipg_root", base + 0x4350, 0, &share_count_sai3);
	hws[IMX8MQ_CLK_SAI4_ROOT] = imx_clk_hw_gate2_shared2("sai4_root_clk", "sai4", base + 0x4360, 0, &share_count_sai4);
	hws[IMX8MQ_CLK_SAI4_IPG] = imx_clk_hw_gate2_shared2("sai4_ipg_clk", "ipg_audio_root", base + 0x4360, 0, &share_count_sai4);
	hws[IMX8MQ_CLK_SAI5_ROOT] = imx_clk_hw_gate2_shared2("sai5_root_clk", "sai5", base + 0x4370, 0, &share_count_sai5);
	hws[IMX8MQ_CLK_SAI5_IPG] = imx_clk_hw_gate2_shared2("sai5_ipg_clk", "ipg_audio_root", base + 0x4370, 0, &share_count_sai5);
	hws[IMX8MQ_CLK_SAI6_ROOT] = imx_clk_hw_gate2_shared2("sai6_root_clk", "sai6", base + 0x4380, 0, &share_count_sai6);
	hws[IMX8MQ_CLK_SAI6_IPG] = imx_clk_hw_gate2_shared2("sai6_ipg_clk", "ipg_audio_root", base + 0x4380, 0, &share_count_sai6);
	hws[IMX8MQ_CLK_SNVS_ROOT] = imx_clk_hw_gate4("snvs_root_clk", "ipg_root", base + 0x4470, 0);
	hws[IMX8MQ_CLK_UART1_ROOT] = imx_clk_hw_gate4("uart1_root_clk", "uart1", base + 0x4490, 0);
	hws[IMX8MQ_CLK_UART2_ROOT] = imx_clk_hw_gate4("uart2_root_clk", "uart2", base + 0x44a0, 0);
	hws[IMX8MQ_CLK_UART3_ROOT] = imx_clk_hw_gate4("uart3_root_clk", "uart3", base + 0x44b0, 0);
	hws[IMX8MQ_CLK_UART4_ROOT] = imx_clk_hw_gate4("uart4_root_clk", "uart4", base + 0x44c0, 0);
	hws[IMX8MQ_CLK_USB1_CTRL_ROOT] = imx_clk_hw_gate4("usb1_ctrl_root_clk", "usb_bus", base + 0x44d0, 0);
	hws[IMX8MQ_CLK_USB2_CTRL_ROOT] = imx_clk_hw_gate4("usb2_ctrl_root_clk", "usb_bus", base + 0x44e0, 0);
	hws[IMX8MQ_CLK_USB1_PHY_ROOT] = imx_clk_hw_gate4("usb1_phy_root_clk", "usb_phy_ref", base + 0x44f0, 0);
	hws[IMX8MQ_CLK_USB2_PHY_ROOT] = imx_clk_hw_gate4("usb2_phy_root_clk", "usb_phy_ref", base + 0x4500, 0);
	hws[IMX8MQ_CLK_USDHC1_ROOT] = imx_clk_hw_gate4("usdhc1_root_clk", "usdhc1", base + 0x4510, 0);
	hws[IMX8MQ_CLK_USDHC2_ROOT] = imx_clk_hw_gate4("usdhc2_root_clk", "usdhc2", base + 0x4520, 0);
	hws[IMX8MQ_CLK_WDOG1_ROOT] = imx_clk_hw_gate4("wdog1_root_clk", "wdog", base + 0x4530, 0);
	hws[IMX8MQ_CLK_WDOG2_ROOT] = imx_clk_hw_gate4("wdog2_root_clk", "wdog", base + 0x4540, 0);
	hws[IMX8MQ_CLK_WDOG3_ROOT] = imx_clk_hw_gate4("wdog3_root_clk", "wdog", base + 0x4550, 0);
	hws[IMX8MQ_CLK_VPU_G1_ROOT] = imx_clk_hw_gate2_flags("vpu_g1_root_clk", "vpu_g1", base + 0x4560, 0, CLK_SET_RATE_PARENT | CLK_OPS_PARENT_ENABLE);
	hws[IMX8MQ_CLK_GPU_ROOT] = imx_clk_hw_gate4("gpu_root_clk", "gpu_core", base + 0x4570, 0);
	hws[IMX8MQ_CLK_VPU_G2_ROOT] = imx_clk_hw_gate2_flags("vpu_g2_root_clk", "vpu_g2", base + 0x45a0, 0, CLK_SET_RATE_PARENT | CLK_OPS_PARENT_ENABLE);
	hws[IMX8MQ_CLK_DISP_ROOT] = imx_clk_hw_gate2_shared2("disp_root_clk", "disp_dc8000", base + 0x45d0, 0, &share_count_dcss);
	hws[IMX8MQ_CLK_DISP_AXI_ROOT]  = imx_clk_hw_gate2_shared2("disp_axi_root_clk", "disp_axi", base + 0x45d0, 0, &share_count_dcss);
	hws[IMX8MQ_CLK_DISP_APB_ROOT]  = imx_clk_hw_gate2_shared2("disp_apb_root_clk", "disp_apb", base + 0x45d0, 0, &share_count_dcss);
	hws[IMX8MQ_CLK_DISP_RTRM_ROOT] = imx_clk_hw_gate2_shared2("disp_rtrm_root_clk", "disp_rtrm", base + 0x45d0, 0, &share_count_dcss);
	hws[IMX8MQ_CLK_TMU_ROOT] = imx_clk_hw_gate4("tmu_root_clk", "ipg_root", base + 0x4620, 0);
	hws[IMX8MQ_CLK_VPU_DEC_ROOT] = imx_clk_hw_gate2_flags("vpu_dec_root_clk", "vpu_bus", base + 0x4630, 0, CLK_SET_RATE_PARENT | CLK_OPS_PARENT_ENABLE);
	hws[IMX8MQ_CLK_CSI1_ROOT] = imx_clk_hw_gate4("csi1_root_clk", "csi1_core", base + 0x4650, 0);
	hws[IMX8MQ_CLK_CSI2_ROOT] = imx_clk_hw_gate4("csi2_root_clk", "csi2_core", base + 0x4660, 0);
	hws[IMX8MQ_CLK_SDMA1_ROOT] = imx_clk_hw_gate4("sdma1_clk", "ipg_root", base + 0x43a0, 0);
	hws[IMX8MQ_CLK_SDMA2_ROOT] = imx_clk_hw_gate4("sdma2_clk", "ipg_audio_root", base + 0x43b0, 0);

	hws[IMX8MQ_GPT_3M_CLK] = imx_clk_hw_fixed_factor("gpt_3m", "osc_25m", 1, 8);
	hws[IMX8MQ_CLK_DRAM_ALT_ROOT] = imx_clk_hw_fixed_factor("dram_alt_root", "dram_alt", 1, 4);

	hws[IMX8MQ_CLK_ARM] = imx_clk_hw_cpu("arm", "arm_a53_core",
					   hws[IMX8MQ_CLK_A53_CORE]->clk,
					   hws[IMX8MQ_CLK_A53_CORE]->clk,
					   hws[IMX8MQ_ARM_PLL_OUT]->clk,
					   hws[IMX8MQ_CLK_A53_DIV]->clk);

	imx_check_clk_hws(hws, IMX8MQ_CLK_END);

	err = of_clk_add_hw_provider(np, of_clk_hw_onecell_get, clk_hw_data);
	if (err < 0) {
		dev_err(dev, "failed to register hws for i.MX8MQ\n");
		goto unregister_hws;
	}

	imx_register_uart_clocks(4);

	return 0;

unregister_hws:
	imx_unregister_hw_clocks(hws, IMX8MQ_CLK_END);

	return err;
}

static const struct of_device_id imx8mq_clk_of_match[] = {
	{ .compatible = "fsl,imx8mq-ccm" },
	{ /* Sentinel */ },
};
MODULE_DEVICE_TABLE(of, imx8mq_clk_of_match);


static struct platform_driver imx8mq_clk_driver = {
	.probe = imx8mq_clocks_probe,
	.driver = {
		.name = "imx8mq-ccm",
		/*
		 * Disable bind attributes: clocks are not removed and
		 * reloading the driver will crash or break devices.
		 */
		.suppress_bind_attrs = true,
		.of_match_table = of_match_ptr(imx8mq_clk_of_match),
	},
};
module_platform_driver(imx8mq_clk_driver);

MODULE_AUTHOR("Abel Vesa <abel.vesa@nxp.com>");
MODULE_DESCRIPTION("NXP i.MX8MQ clock driver");
MODULE_LICENSE("GPL v2");
