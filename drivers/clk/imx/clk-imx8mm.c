// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2017-2018 NXP.
 */

#include <dt-bindings/clock/imx8mm-clock.h>
#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/types.h>

#include "clk.h"

static u32 share_count_sai1;
static u32 share_count_sai2;
static u32 share_count_sai3;
static u32 share_count_sai4;
static u32 share_count_sai5;
static u32 share_count_sai6;
static u32 share_count_disp;
static u32 share_count_pdm;
static u32 share_count_nand;

static const char *pll_ref_sels[] = { "osc_24m", "dummy", "dummy", "dummy", };
static const char *audio_pll1_bypass_sels[] = {"audio_pll1", "audio_pll1_ref_sel", };
static const char *audio_pll2_bypass_sels[] = {"audio_pll2", "audio_pll2_ref_sel", };
static const char *video_pll1_bypass_sels[] = {"video_pll1", "video_pll1_ref_sel", };
static const char *dram_pll_bypass_sels[] = {"dram_pll", "dram_pll_ref_sel", };
static const char *gpu_pll_bypass_sels[] = {"gpu_pll", "gpu_pll_ref_sel", };
static const char *vpu_pll_bypass_sels[] = {"vpu_pll", "vpu_pll_ref_sel", };
static const char *arm_pll_bypass_sels[] = {"arm_pll", "arm_pll_ref_sel", };
static const char *sys_pll3_bypass_sels[] = {"sys_pll3", "sys_pll3_ref_sel", };

/* CCM ROOT */
static const char *imx8mm_a53_sels[] = {"osc_24m", "arm_pll_out", "sys_pll2_500m", "sys_pll2_1000m",
					"sys_pll1_800m", "sys_pll1_400m", "audio_pll1_out", "sys_pll3_out", };

static const char * const imx8mm_a53_core_sels[] = {"arm_a53_div", "arm_pll_out", };

static const char *imx8mm_m4_sels[] = {"osc_24m", "sys_pll2_200m", "sys_pll2_250m", "sys_pll1_266m",
				       "sys_pll1_800m", "audio_pll1_out", "video_pll1_out", "sys_pll3_out", };

static const char *imx8mm_vpu_sels[] = {"osc_24m", "arm_pll_out", "sys_pll2_500m", "sys_pll2_1000m",
					"sys_pll1_800m", "sys_pll1_400m", "audio_pll1_out", "vpu_pll_out", };

static const char *imx8mm_gpu3d_sels[] = {"osc_24m", "gpu_pll_out", "sys_pll1_800m", "sys_pll3_out",
					  "sys_pll2_1000m", "audio_pll1_out", "video_pll1_out", "audio_pll2_out", };

static const char *imx8mm_gpu2d_sels[] = {"osc_24m", "gpu_pll_out", "sys_pll1_800m", "sys_pll3_out",
					  "sys_pll2_1000m", "audio_pll1_out", "video_pll1_out", "audio_pll2_out", };

static const char *imx8mm_main_axi_sels[] = {"osc_24m", "sys_pll2_333m", "sys_pll1_800m", "sys_pll2_250m",
					     "sys_pll2_1000m", "audio_pll1_out", "video_pll1_out", "sys_pll1_100m",};

static const char *imx8mm_enet_axi_sels[] = {"osc_24m", "sys_pll1_266m", "sys_pll1_800m", "sys_pll2_250m",
					     "sys_pll2_200m", "audio_pll1_out", "video_pll1_out", "sys_pll3_out", };

static const char *imx8mm_nand_usdhc_sels[] = {"osc_24m", "sys_pll1_266m", "sys_pll1_800m", "sys_pll2_200m",
					       "sys_pll1_133m", "sys_pll3_out", "sys_pll2_250m", "audio_pll1_out", };

static const char *imx8mm_vpu_bus_sels[] = {"osc_24m", "sys_pll1_800m", "vpu_pll_out", "audio_pll2_out",
					    "sys_pll3_out", "sys_pll2_1000m", "sys_pll2_200m", "sys_pll1_100m", };

static const char *imx8mm_disp_axi_sels[] = {"osc_24m", "sys_pll2_1000m", "sys_pll1_800m", "sys_pll3_out",
					     "sys_pll1_40m", "audio_pll2_out", "clk_ext1", "clk_ext4", };

static const char *imx8mm_disp_apb_sels[] = {"osc_24m", "sys_pll2_125m", "sys_pll1_800m", "sys_pll3_out",
					     "sys_pll1_40m", "audio_pll2_out", "clk_ext1", "clk_ext3", };

static const char *imx8mm_disp_rtrm_sels[] = {"osc_24m", "sys_pll1_800m", "sys_pll2_200m", "sys_pll2_1000m",
					      "audio_pll1_out", "video_pll1_out", "clk_ext2", "clk_ext3", };

static const char *imx8mm_usb_bus_sels[] = {"osc_24m", "sys_pll2_500m", "sys_pll1_800m", "sys_pll2_100m",
					    "sys_pll2_200m", "clk_ext2", "clk_ext4", "audio_pll2_out", };

static const char *imx8mm_gpu_axi_sels[] = {"osc_24m", "sys_pll1_800m", "gpu_pll_out", "sys_pll3_out", "sys_pll2_1000m",
					    "audio_pll1_out", "video_pll1_out", "audio_pll2_out", };

static const char *imx8mm_gpu_ahb_sels[] = {"osc_24m", "sys_pll1_800m", "gpu_pll_out", "sys_pll3_out", "sys_pll2_1000m",
					    "audio_pll1_out", "video_pll1_out", "audio_pll2_out", };

static const char *imx8mm_noc_sels[] = {"osc_24m", "sys_pll1_800m", "sys_pll3_out", "sys_pll2_1000m", "sys_pll2_500m",
					"audio_pll1_out", "video_pll1_out", "audio_pll2_out", };

static const char *imx8mm_noc_apb_sels[] = {"osc_24m", "sys_pll1_400m", "sys_pll3_out", "sys_pll2_333m", "sys_pll2_200m",
					    "sys_pll1_800m", "audio_pll1_out", "video_pll1_out", };

static const char *imx8mm_ahb_sels[] = {"osc_24m", "sys_pll1_133m", "sys_pll1_800m", "sys_pll1_400m",
					"sys_pll2_125m", "sys_pll3_out", "audio_pll1_out", "video_pll1_out", };

static const char *imx8mm_audio_ahb_sels[] = {"osc_24m", "sys_pll2_500m", "sys_pll1_800m", "sys_pll2_1000m",
					      "sys_pll2_166m", "sys_pll3_out", "audio_pll1_out", "video_pll1_out", };

static const char *imx8mm_dram_alt_sels[] = {"osc_24m", "sys_pll1_800m", "sys_pll1_100m", "sys_pll2_500m",
					     "sys_pll2_1000m", "sys_pll3_out", "audio_pll1_out", "sys_pll1_266m", };

static const char *imx8mm_dram_apb_sels[] = {"osc_24m", "sys_pll2_200m", "sys_pll1_40m", "sys_pll1_160m",
					     "sys_pll1_800m", "sys_pll3_out", "sys_pll2_250m", "audio_pll2_out", };

static const char *imx8mm_vpu_g1_sels[] = {"osc_24m", "vpu_pll_out", "sys_pll1_800m", "sys_pll2_1000m",
					   "sys_pll1_100m", "sys_pll2_125m", "sys_pll3_out", "audio_pll1_out", };

static const char *imx8mm_vpu_g2_sels[] = {"osc_24m", "vpu_pll_out", "sys_pll1_800m", "sys_pll2_1000m",
					   "sys_pll1_100m", "sys_pll2_125m", "sys_pll3_out", "audio_pll1_out", };

static const char *imx8mm_disp_dtrc_sels[] = {"osc_24m", "dummy", "sys_pll1_800m", "sys_pll2_1000m",
					      "sys_pll1_160m", "video_pll1_out", "sys_pll3_out", "audio_pll2_out", };

static const char *imx8mm_disp_dc8000_sels[] = {"osc_24m", "dummy", "sys_pll1_800m", "sys_pll2_1000m",
						"sys_pll1_160m", "video_pll1_out", "sys_pll3_out", "audio_pll2_out", };

static const char *imx8mm_pcie1_ctrl_sels[] = {"osc_24m", "sys_pll2_250m", "sys_pll2_200m", "sys_pll1_266m",
					       "sys_pll1_800m", "sys_pll2_500m", "sys_pll2_333m", "sys_pll3_out", };

static const char *imx8mm_pcie1_phy_sels[] = {"osc_24m", "sys_pll2_100m", "sys_pll2_500m", "clk_ext1", "clk_ext2",
					      "clk_ext3", "clk_ext4", "sys_pll1_400m", };

static const char *imx8mm_pcie1_aux_sels[] = {"osc_24m", "sys_pll2_200m", "sys_pll2_50m", "sys_pll3_out",
					      "sys_pll2_100m", "sys_pll1_80m", "sys_pll1_160m", "sys_pll1_200m", };

static const char *imx8mm_dc_pixel_sels[] = {"osc_24m", "video_pll1_out", "audio_pll2_out", "audio_pll1_out",
					     "sys_pll1_800m", "sys_pll2_1000m", "sys_pll3_out", "clk_ext4", };

static const char *imx8mm_lcdif_pixel_sels[] = {"osc_24m", "video_pll1_out", "audio_pll2_out", "audio_pll1_out",
						"sys_pll1_800m", "sys_pll2_1000m", "sys_pll3_out", "clk_ext4", };

static const char *imx8mm_sai1_sels[] = {"osc_24m", "audio_pll1_out", "audio_pll2_out", "video_pll1_out",
					 "sys_pll1_133m", "osc_hdmi", "clk_ext1", "clk_ext2", };

static const char *imx8mm_sai2_sels[] = {"osc_24m", "audio_pll1_out", "audio_pll2_out", "video_pll1_out",
					 "sys_pll1_133m", "osc_hdmi", "clk_ext2", "clk_ext3", };

static const char *imx8mm_sai3_sels[] = {"osc_24m", "audio_pll1_out", "audio_pll2_out", "video_pll1_out",
					 "sys_pll1_133m", "osc_hdmi", "clk_ext3", "clk_ext4", };

static const char *imx8mm_sai4_sels[] = {"osc_24m", "audio_pll1_out", "audio_pll2_out", "video_pll1_out",
					 "sys_pll1_133m", "osc_hdmi", "clk_ext1", "clk_ext2", };

static const char *imx8mm_sai5_sels[] = {"osc_24m", "audio_pll1_out", "audio_pll2_out", "video_pll1_out",
					 "sys_pll1_133m", "osc_hdmi", "clk_ext2", "clk_ext3", };

static const char *imx8mm_sai6_sels[] = {"osc_24m", "audio_pll1_out", "audio_pll2_out", "video_pll1_out",
					 "sys_pll1_133m", "osc_hdmi", "clk_ext3", "clk_ext4", };

static const char *imx8mm_spdif1_sels[] = {"osc_24m", "audio_pll1_out", "audio_pll2_out", "video_pll1_out",
					   "sys_pll1_133m", "osc_hdmi", "clk_ext2", "clk_ext3", };

static const char *imx8mm_spdif2_sels[] = {"osc_24m", "audio_pll1_out", "audio_pll2_out", "video_pll1_out",
					   "sys_pll1_133m", "osc_hdmi", "clk_ext3", "clk_ext4", };

static const char *imx8mm_enet_ref_sels[] = {"osc_24m", "sys_pll2_125m", "sys_pll2_50m", "sys_pll2_100m",
					     "sys_pll1_160m", "audio_pll1_out", "video_pll1_out", "clk_ext4", };

static const char *imx8mm_enet_timer_sels[] = {"osc_24m", "sys_pll2_100m", "audio_pll1_out", "clk_ext1", "clk_ext2",
					       "clk_ext3", "clk_ext4", "video_pll1_out", };

static const char *imx8mm_enet_phy_sels[] = {"osc_24m", "sys_pll2_50m", "sys_pll2_125m", "sys_pll2_200m",
					     "sys_pll2_500m", "video_pll1_out", "audio_pll2_out", };

static const char *imx8mm_nand_sels[] = {"osc_24m", "sys_pll2_500m", "audio_pll1_out", "sys_pll1_400m",
					 "audio_pll2_out", "sys_pll3_out", "sys_pll2_250m", "video_pll1_out", };

static const char *imx8mm_qspi_sels[] = {"osc_24m", "sys_pll1_400m", "sys_pll2_333m", "sys_pll2_500m",
					 "audio_pll2_out", "sys_pll1_266m", "sys_pll3_out", "sys_pll1_100m", };

static const char *imx8mm_usdhc1_sels[] = {"osc_24m", "sys_pll1_400m", "sys_pll1_800m", "sys_pll2_500m",
					   "sys_pll3_out", "sys_pll1_266m", "audio_pll2_out", "sys_pll1_100m", };

static const char *imx8mm_usdhc2_sels[] = {"osc_24m", "sys_pll1_400m", "sys_pll1_800m", "sys_pll2_500m",
					   "sys_pll3_out", "sys_pll1_266m", "audio_pll2_out", "sys_pll1_100m", };

static const char *imx8mm_i2c1_sels[] = {"osc_24m", "sys_pll1_160m", "sys_pll2_50m", "sys_pll3_out", "audio_pll1_out",
					 "video_pll1_out", "audio_pll2_out", "sys_pll1_133m", };

static const char *imx8mm_i2c2_sels[] = {"osc_24m", "sys_pll1_160m", "sys_pll2_50m", "sys_pll3_out", "audio_pll1_out",
					 "video_pll1_out", "audio_pll2_out", "sys_pll1_133m", };

static const char *imx8mm_i2c3_sels[] = {"osc_24m", "sys_pll1_160m", "sys_pll2_50m", "sys_pll3_out", "audio_pll1_out",
					 "video_pll1_out", "audio_pll2_out", "sys_pll1_133m", };

static const char *imx8mm_i2c4_sels[] = {"osc_24m", "sys_pll1_160m", "sys_pll2_50m", "sys_pll3_out", "audio_pll1_out",
					 "video_pll1_out", "audio_pll2_out", "sys_pll1_133m", };

static const char *imx8mm_uart1_sels[] = {"osc_24m", "sys_pll1_80m", "sys_pll2_200m", "sys_pll2_100m",
					  "sys_pll3_out", "clk_ext2", "clk_ext4", "audio_pll2_out", };

static const char *imx8mm_uart2_sels[] = {"osc_24m", "sys_pll1_80m", "sys_pll2_200m", "sys_pll2_100m",
					  "sys_pll3_out", "clk_ext2", "clk_ext3", "audio_pll2_out", };

static const char *imx8mm_uart3_sels[] = {"osc_24m", "sys_pll1_80m", "sys_pll2_200m", "sys_pll2_100m",
					  "sys_pll3_out", "clk_ext2", "clk_ext4", "audio_pll2_out", };

static const char *imx8mm_uart4_sels[] = {"osc_24m", "sys_pll1_80m", "sys_pll2_200m", "sys_pll2_100m",
					  "sys_pll3_out", "clk_ext2", "clk_ext3", "audio_pll2_out", };

static const char *imx8mm_usb_core_sels[] = {"osc_24m", "sys_pll1_100m", "sys_pll1_40m", "sys_pll2_100m",
					     "sys_pll2_200m", "clk_ext2", "clk_ext3", "audio_pll2_out", };

static const char *imx8mm_usb_phy_sels[] = {"osc_24m", "sys_pll1_100m", "sys_pll1_40m", "sys_pll2_100m",
					     "sys_pll2_200m", "clk_ext2", "clk_ext3", "audio_pll2_out", };

static const char *imx8mm_gic_sels[] = {"osc_24m", "sys_pll2_200m", "sys_pll1_40m", "sys_pll2_100m",
					"sys_pll1_800m", "clk_ext2", "clk_ext4", "audio_pll2_out" };

static const char *imx8mm_ecspi1_sels[] = {"osc_24m", "sys_pll2_200m", "sys_pll1_40m", "sys_pll1_160m",
					   "sys_pll1_800m", "sys_pll3_out", "sys_pll2_250m", "audio_pll2_out", };

static const char *imx8mm_ecspi2_sels[] = {"osc_24m", "sys_pll2_200m", "sys_pll1_40m", "sys_pll1_160m",
					   "sys_pll1_800m", "sys_pll3_out", "sys_pll2_250m", "audio_pll2_out", };

static const char *imx8mm_pwm1_sels[] = {"osc_24m", "sys_pll2_100m", "sys_pll1_160m", "sys_pll1_40m",
					 "sys_pll3_out", "clk_ext1", "sys_pll1_80m", "video_pll1_out", };

static const char *imx8mm_pwm2_sels[] = {"osc_24m", "sys_pll2_100m", "sys_pll1_160m", "sys_pll1_40m",
					 "sys_pll3_out", "clk_ext1", "sys_pll1_80m", "video_pll1_out", };

static const char *imx8mm_pwm3_sels[] = {"osc_24m", "sys_pll2_100m", "sys_pll1_160m", "sys_pll1_40m",
					 "sys_pll3_out", "clk_ext2", "sys_pll1_80m", "video_pll1_out", };

static const char *imx8mm_pwm4_sels[] = {"osc_24m", "sys_pll2_100m", "sys_pll1_160m", "sys_pll1_40m",
					 "sys_pll3_out", "clk_ext2", "sys_pll1_80m", "video_pll1_out", };

static const char *imx8mm_gpt1_sels[] = {"osc_24m", "sys_pll2_100m", "sys_pll1_400m", "sys_pll1_40m",
					 "video_pll1_out", "sys_pll1_80m", "audio_pll1_out", "clk_ext1" };

static const char *imx8mm_wdog_sels[] = {"osc_24m", "sys_pll1_133m", "sys_pll1_160m", "vpu_pll_out",
					 "sys_pll2_125m", "sys_pll3_out", "sys_pll1_80m", "sys_pll2_166m", };

static const char *imx8mm_wrclk_sels[] = {"osc_24m", "sys_pll1_40m", "vpu_pll_out", "sys_pll3_out", "sys_pll2_200m",
					  "sys_pll1_266m", "sys_pll2_500m", "sys_pll1_100m", };

static const char *imx8mm_dsi_core_sels[] = {"osc_24m", "sys_pll1_266m", "sys_pll2_250m", "sys_pll1_800m",
					     "sys_pll2_1000m", "sys_pll3_out", "audio_pll2_out", "video_pll1_out", };

static const char *imx8mm_dsi_phy_sels[] = {"osc_24m", "sys_pll2_125m", "sys_pll2_100m", "sys_pll1_800m",
					    "sys_pll2_1000m", "clk_ext2", "audio_pll2_out", "video_pll1_out", };

static const char *imx8mm_dsi_dbi_sels[] = {"osc_24m", "sys_pll1_266m", "sys_pll2_100m", "sys_pll1_800m",
					    "sys_pll2_1000m", "sys_pll3_out", "audio_pll2_out", "video_pll1_out", };

static const char *imx8mm_usdhc3_sels[] = {"osc_24m", "sys_pll1_400m", "sys_pll1_800m", "sys_pll2_500m",
					   "sys_pll3_out", "sys_pll1_266m", "audio_pll2_out", "sys_pll1_100m", };

static const char *imx8mm_csi1_core_sels[] = {"osc_24m", "sys_pll1_266m", "sys_pll2_250m", "sys_pll1_800m",
					      "sys_pll2_1000m", "sys_pll3_out", "audio_pll2_out", "video_pll1_out", };

static const char *imx8mm_csi1_phy_sels[] = {"osc_24m", "sys_pll2_333m", "sys_pll2_100m", "sys_pll1_800m",
					     "sys_pll2_1000m", "clk_ext2", "audio_pll2_out", "video_pll1_out", };

static const char *imx8mm_csi1_esc_sels[] = {"osc_24m", "sys_pll2_100m", "sys_pll1_80m", "sys_pll1_800m",
					     "sys_pll2_1000m", "sys_pll3_out", "clk_ext3", "audio_pll2_out", };

static const char *imx8mm_csi2_core_sels[] = {"osc_24m", "sys_pll1_266m", "sys_pll2_250m", "sys_pll1_800m",
					      "sys_pll2_1000m", "sys_pll3_out", "audio_pll2_out", "video_pll1_out", };

static const char *imx8mm_csi2_phy_sels[] = {"osc_24m", "sys_pll2_333m", "sys_pll2_100m", "sys_pll1_800m",
					     "sys_pll2_1000m", "clk_ext2", "audio_pll2_out", "video_pll1_out", };

static const char *imx8mm_csi2_esc_sels[] = {"osc_24m", "sys_pll2_100m", "sys_pll1_80m", "sys_pll1_800m",
					     "sys_pll2_1000m", "sys_pll3_out", "clk_ext3", "audio_pll2_out", };

static const char *imx8mm_pcie2_ctrl_sels[] = {"osc_24m", "sys_pll2_250m", "sys_pll2_200m", "sys_pll1_266m",
					       "sys_pll1_800m", "sys_pll2_500m", "sys_pll2_333m", "sys_pll3_out", };

static const char *imx8mm_pcie2_phy_sels[] = {"osc_24m", "sys_pll2_100m", "sys_pll2_500m", "clk_ext1",
					      "clk_ext2", "clk_ext3", "clk_ext4", "sys_pll1_400m", };

static const char *imx8mm_pcie2_aux_sels[] = {"osc_24m", "sys_pll2_200m", "sys_pll2_50m", "sys_pll3_out",
					      "sys_pll2_100m", "sys_pll1_80m", "sys_pll1_160m", "sys_pll1_200m", };

static const char *imx8mm_ecspi3_sels[] = {"osc_24m", "sys_pll2_200m", "sys_pll1_40m", "sys_pll1_160m",
					   "sys_pll1_800m", "sys_pll3_out", "sys_pll2_250m", "audio_pll2_out", };

static const char *imx8mm_pdm_sels[] = {"osc_24m", "sys_pll2_100m", "audio_pll1_out", "sys_pll1_800m",
					"sys_pll2_1000m", "sys_pll3_out", "clk_ext3", "audio_pll2_out", };

static const char *imx8mm_vpu_h1_sels[] = {"osc_24m", "vpu_pll_out", "sys_pll1_800m", "sys_pll2_1000m",
					   "audio_pll2_out", "sys_pll2_125m", "sys_pll3_out", "audio_pll1_out", };

static const char *imx8mm_dram_core_sels[] = {"dram_pll_out", "dram_alt_root", };

static const char *imx8mm_clko1_sels[] = {"osc_24m", "sys_pll1_800m", "dummy", "sys_pll1_200m",
					  "audio_pll2_out", "sys_pll2_500m", "vpu_pll", "sys_pll1_80m", };
static const char *imx8mm_clko2_sels[] = {"osc_24m", "sys_pll2_200m", "sys_pll1_400m", "sys_pll2_166m",
					  "sys_pll3_out", "audio_pll1_out", "video_pll1_out", "osc_32k", };

static struct clk_hw_onecell_data *clk_hw_data;
static struct clk_hw **hws;

static const int uart_clk_ids[] = {
	IMX8MM_CLK_UART1_ROOT,
	IMX8MM_CLK_UART2_ROOT,
	IMX8MM_CLK_UART3_ROOT,
	IMX8MM_CLK_UART4_ROOT,
};
static struct clk **uart_hws[ARRAY_SIZE(uart_clk_ids) + 1];

static int imx8mm_clocks_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	void __iomem *base;
	int ret, i;

	clk_hw_data = kzalloc(struct_size(clk_hw_data, hws,
					  IMX8MM_CLK_END), GFP_KERNEL);
	if (WARN_ON(!clk_hw_data))
		return -ENOMEM;

	clk_hw_data->num = IMX8MM_CLK_END;
	hws = clk_hw_data->hws;

	hws[IMX8MM_CLK_DUMMY] = imx_clk_hw_fixed("dummy", 0);
	hws[IMX8MM_CLK_24M] = imx_obtain_fixed_clk_hw(np, "osc_24m");
	hws[IMX8MM_CLK_32K] = imx_obtain_fixed_clk_hw(np, "osc_32k");
	hws[IMX8MM_CLK_EXT1] = imx_obtain_fixed_clk_hw(np, "clk_ext1");
	hws[IMX8MM_CLK_EXT2] = imx_obtain_fixed_clk_hw(np, "clk_ext2");
	hws[IMX8MM_CLK_EXT3] = imx_obtain_fixed_clk_hw(np, "clk_ext3");
	hws[IMX8MM_CLK_EXT4] = imx_obtain_fixed_clk_hw(np, "clk_ext4");

	np = of_find_compatible_node(NULL, NULL, "fsl,imx8mm-anatop");
	base = of_iomap(np, 0);
	of_node_put(np);
	if (WARN_ON(!base))
		return -ENOMEM;

	hws[IMX8MM_AUDIO_PLL1_REF_SEL] = imx_clk_hw_mux("audio_pll1_ref_sel", base + 0x0, 0, 2, pll_ref_sels, ARRAY_SIZE(pll_ref_sels));
	hws[IMX8MM_AUDIO_PLL2_REF_SEL] = imx_clk_hw_mux("audio_pll2_ref_sel", base + 0x14, 0, 2, pll_ref_sels, ARRAY_SIZE(pll_ref_sels));
	hws[IMX8MM_VIDEO_PLL1_REF_SEL] = imx_clk_hw_mux("video_pll1_ref_sel", base + 0x28, 0, 2, pll_ref_sels, ARRAY_SIZE(pll_ref_sels));
	hws[IMX8MM_DRAM_PLL_REF_SEL] = imx_clk_hw_mux("dram_pll_ref_sel", base + 0x50, 0, 2, pll_ref_sels, ARRAY_SIZE(pll_ref_sels));
	hws[IMX8MM_GPU_PLL_REF_SEL] = imx_clk_hw_mux("gpu_pll_ref_sel", base + 0x64, 0, 2, pll_ref_sels, ARRAY_SIZE(pll_ref_sels));
	hws[IMX8MM_VPU_PLL_REF_SEL] = imx_clk_hw_mux("vpu_pll_ref_sel", base + 0x74, 0, 2, pll_ref_sels, ARRAY_SIZE(pll_ref_sels));
	hws[IMX8MM_ARM_PLL_REF_SEL] = imx_clk_hw_mux("arm_pll_ref_sel", base + 0x84, 0, 2, pll_ref_sels, ARRAY_SIZE(pll_ref_sels));
	hws[IMX8MM_SYS_PLL3_REF_SEL] = imx_clk_hw_mux("sys_pll3_ref_sel", base + 0x114, 0, 2, pll_ref_sels, ARRAY_SIZE(pll_ref_sels));

	hws[IMX8MM_AUDIO_PLL1] = imx_clk_hw_pll14xx("audio_pll1", "audio_pll1_ref_sel", base, &imx_1443x_pll);
	hws[IMX8MM_AUDIO_PLL2] = imx_clk_hw_pll14xx("audio_pll2", "audio_pll2_ref_sel", base + 0x14, &imx_1443x_pll);
	hws[IMX8MM_VIDEO_PLL1] = imx_clk_hw_pll14xx("video_pll1", "video_pll1_ref_sel", base + 0x28, &imx_1443x_pll);
	hws[IMX8MM_DRAM_PLL] = imx_clk_hw_pll14xx("dram_pll", "dram_pll_ref_sel", base + 0x50, &imx_1443x_dram_pll);
	hws[IMX8MM_GPU_PLL] = imx_clk_hw_pll14xx("gpu_pll", "gpu_pll_ref_sel", base + 0x64, &imx_1416x_pll);
	hws[IMX8MM_VPU_PLL] = imx_clk_hw_pll14xx("vpu_pll", "vpu_pll_ref_sel", base + 0x74, &imx_1416x_pll);
	hws[IMX8MM_ARM_PLL] = imx_clk_hw_pll14xx("arm_pll", "arm_pll_ref_sel", base + 0x84, &imx_1416x_pll);
	hws[IMX8MM_SYS_PLL1] = imx_clk_hw_fixed("sys_pll1", 800000000);
	hws[IMX8MM_SYS_PLL2] = imx_clk_hw_fixed("sys_pll2", 1000000000);
	hws[IMX8MM_SYS_PLL3] = imx_clk_hw_pll14xx("sys_pll3", "sys_pll3_ref_sel", base + 0x114, &imx_1416x_pll);

	/* PLL bypass out */
	hws[IMX8MM_AUDIO_PLL1_BYPASS] = imx_clk_hw_mux_flags("audio_pll1_bypass", base, 16, 1, audio_pll1_bypass_sels, ARRAY_SIZE(audio_pll1_bypass_sels), CLK_SET_RATE_PARENT);
	hws[IMX8MM_AUDIO_PLL2_BYPASS] = imx_clk_hw_mux_flags("audio_pll2_bypass", base + 0x14, 16, 1, audio_pll2_bypass_sels, ARRAY_SIZE(audio_pll2_bypass_sels), CLK_SET_RATE_PARENT);
	hws[IMX8MM_VIDEO_PLL1_BYPASS] = imx_clk_hw_mux_flags("video_pll1_bypass", base + 0x28, 16, 1, video_pll1_bypass_sels, ARRAY_SIZE(video_pll1_bypass_sels), CLK_SET_RATE_PARENT);
	hws[IMX8MM_DRAM_PLL_BYPASS] = imx_clk_hw_mux_flags("dram_pll_bypass", base + 0x50, 16, 1, dram_pll_bypass_sels, ARRAY_SIZE(dram_pll_bypass_sels), CLK_SET_RATE_PARENT);
	hws[IMX8MM_GPU_PLL_BYPASS] = imx_clk_hw_mux_flags("gpu_pll_bypass", base + 0x64, 28, 1, gpu_pll_bypass_sels, ARRAY_SIZE(gpu_pll_bypass_sels), CLK_SET_RATE_PARENT);
	hws[IMX8MM_VPU_PLL_BYPASS] = imx_clk_hw_mux_flags("vpu_pll_bypass", base + 0x74, 28, 1, vpu_pll_bypass_sels, ARRAY_SIZE(vpu_pll_bypass_sels), CLK_SET_RATE_PARENT);
	hws[IMX8MM_ARM_PLL_BYPASS] = imx_clk_hw_mux_flags("arm_pll_bypass", base + 0x84, 28, 1, arm_pll_bypass_sels, ARRAY_SIZE(arm_pll_bypass_sels), CLK_SET_RATE_PARENT);
	hws[IMX8MM_SYS_PLL3_BYPASS] = imx_clk_hw_mux_flags("sys_pll3_bypass", base + 0x114, 28, 1, sys_pll3_bypass_sels, ARRAY_SIZE(sys_pll3_bypass_sels), CLK_SET_RATE_PARENT);

	/* PLL out gate */
	hws[IMX8MM_AUDIO_PLL1_OUT] = imx_clk_hw_gate("audio_pll1_out", "audio_pll1_bypass", base, 13);
	hws[IMX8MM_AUDIO_PLL2_OUT] = imx_clk_hw_gate("audio_pll2_out", "audio_pll2_bypass", base + 0x14, 13);
	hws[IMX8MM_VIDEO_PLL1_OUT] = imx_clk_hw_gate("video_pll1_out", "video_pll1_bypass", base + 0x28, 13);
	hws[IMX8MM_DRAM_PLL_OUT] = imx_clk_hw_gate("dram_pll_out", "dram_pll_bypass", base + 0x50, 13);
	hws[IMX8MM_GPU_PLL_OUT] = imx_clk_hw_gate("gpu_pll_out", "gpu_pll_bypass", base + 0x64, 11);
	hws[IMX8MM_VPU_PLL_OUT] = imx_clk_hw_gate("vpu_pll_out", "vpu_pll_bypass", base + 0x74, 11);
	hws[IMX8MM_ARM_PLL_OUT] = imx_clk_hw_gate("arm_pll_out", "arm_pll_bypass", base + 0x84, 11);
	hws[IMX8MM_SYS_PLL3_OUT] = imx_clk_hw_gate("sys_pll3_out", "sys_pll3_bypass", base + 0x114, 11);

	/* SYS PLL1 fixed output */
	hws[IMX8MM_SYS_PLL1_40M_CG] = imx_clk_hw_gate("sys_pll1_40m_cg", "sys_pll1", base + 0x94, 27);
	hws[IMX8MM_SYS_PLL1_80M_CG] = imx_clk_hw_gate("sys_pll1_80m_cg", "sys_pll1", base + 0x94, 25);
	hws[IMX8MM_SYS_PLL1_100M_CG] = imx_clk_hw_gate("sys_pll1_100m_cg", "sys_pll1", base + 0x94, 23);
	hws[IMX8MM_SYS_PLL1_133M_CG] = imx_clk_hw_gate("sys_pll1_133m_cg", "sys_pll1", base + 0x94, 21);
	hws[IMX8MM_SYS_PLL1_160M_CG] = imx_clk_hw_gate("sys_pll1_160m_cg", "sys_pll1", base + 0x94, 19);
	hws[IMX8MM_SYS_PLL1_200M_CG] = imx_clk_hw_gate("sys_pll1_200m_cg", "sys_pll1", base + 0x94, 17);
	hws[IMX8MM_SYS_PLL1_266M_CG] = imx_clk_hw_gate("sys_pll1_266m_cg", "sys_pll1", base + 0x94, 15);
	hws[IMX8MM_SYS_PLL1_400M_CG] = imx_clk_hw_gate("sys_pll1_400m_cg", "sys_pll1", base + 0x94, 13);
	hws[IMX8MM_SYS_PLL1_OUT] = imx_clk_hw_gate("sys_pll1_out", "sys_pll1", base + 0x94, 11);

	hws[IMX8MM_SYS_PLL1_40M] = imx_clk_hw_fixed_factor("sys_pll1_40m", "sys_pll1_40m_cg", 1, 20);
	hws[IMX8MM_SYS_PLL1_80M] = imx_clk_hw_fixed_factor("sys_pll1_80m", "sys_pll1_80m_cg", 1, 10);
	hws[IMX8MM_SYS_PLL1_100M] = imx_clk_hw_fixed_factor("sys_pll1_100m", "sys_pll1_100m_cg", 1, 8);
	hws[IMX8MM_SYS_PLL1_133M] = imx_clk_hw_fixed_factor("sys_pll1_133m", "sys_pll1_133m_cg", 1, 6);
	hws[IMX8MM_SYS_PLL1_160M] = imx_clk_hw_fixed_factor("sys_pll1_160m", "sys_pll1_160m_cg", 1, 5);
	hws[IMX8MM_SYS_PLL1_200M] = imx_clk_hw_fixed_factor("sys_pll1_200m", "sys_pll1_200m_cg", 1, 4);
	hws[IMX8MM_SYS_PLL1_266M] = imx_clk_hw_fixed_factor("sys_pll1_266m", "sys_pll1_266m_cg", 1, 3);
	hws[IMX8MM_SYS_PLL1_400M] = imx_clk_hw_fixed_factor("sys_pll1_400m", "sys_pll1_400m_cg", 1, 2);
	hws[IMX8MM_SYS_PLL1_800M] = imx_clk_hw_fixed_factor("sys_pll1_800m", "sys_pll1_out", 1, 1);

	/* SYS PLL2 fixed output */
	hws[IMX8MM_SYS_PLL2_50M_CG] = imx_clk_hw_gate("sys_pll2_50m_cg", "sys_pll2", base + 0x104, 27);
	hws[IMX8MM_SYS_PLL2_100M_CG] = imx_clk_hw_gate("sys_pll2_100m_cg", "sys_pll2", base + 0x104, 25);
	hws[IMX8MM_SYS_PLL2_125M_CG] = imx_clk_hw_gate("sys_pll2_125m_cg", "sys_pll2", base + 0x104, 23);
	hws[IMX8MM_SYS_PLL2_166M_CG] = imx_clk_hw_gate("sys_pll2_166m_cg", "sys_pll2", base + 0x104, 21);
	hws[IMX8MM_SYS_PLL2_200M_CG] = imx_clk_hw_gate("sys_pll2_200m_cg", "sys_pll2", base + 0x104, 19);
	hws[IMX8MM_SYS_PLL2_250M_CG] = imx_clk_hw_gate("sys_pll2_250m_cg", "sys_pll2", base + 0x104, 17);
	hws[IMX8MM_SYS_PLL2_333M_CG] = imx_clk_hw_gate("sys_pll2_333m_cg", "sys_pll2", base + 0x104, 15);
	hws[IMX8MM_SYS_PLL2_500M_CG] = imx_clk_hw_gate("sys_pll2_500m_cg", "sys_pll2", base + 0x104, 13);
	hws[IMX8MM_SYS_PLL2_OUT] = imx_clk_hw_gate("sys_pll2_out", "sys_pll2", base + 0x104, 11);

	hws[IMX8MM_SYS_PLL2_50M] = imx_clk_hw_fixed_factor("sys_pll2_50m", "sys_pll2_50m_cg", 1, 20);
	hws[IMX8MM_SYS_PLL2_100M] = imx_clk_hw_fixed_factor("sys_pll2_100m", "sys_pll2_100m_cg", 1, 10);
	hws[IMX8MM_SYS_PLL2_125M] = imx_clk_hw_fixed_factor("sys_pll2_125m", "sys_pll2_125m_cg", 1, 8);
	hws[IMX8MM_SYS_PLL2_166M] = imx_clk_hw_fixed_factor("sys_pll2_166m", "sys_pll2_166m_cg", 1, 6);
	hws[IMX8MM_SYS_PLL2_200M] = imx_clk_hw_fixed_factor("sys_pll2_200m", "sys_pll2_200m_cg", 1, 5);
	hws[IMX8MM_SYS_PLL2_250M] = imx_clk_hw_fixed_factor("sys_pll2_250m", "sys_pll2_250m_cg", 1, 4);
	hws[IMX8MM_SYS_PLL2_333M] = imx_clk_hw_fixed_factor("sys_pll2_333m", "sys_pll2_333m_cg", 1, 3);
	hws[IMX8MM_SYS_PLL2_500M] = imx_clk_hw_fixed_factor("sys_pll2_500m", "sys_pll2_500m_cg", 1, 2);
	hws[IMX8MM_SYS_PLL2_1000M] = imx_clk_hw_fixed_factor("sys_pll2_1000m", "sys_pll2_out", 1, 1);

	np = dev->of_node;
	base = devm_platform_ioremap_resource(pdev, 0);
	if (WARN_ON(IS_ERR(base)))
		return PTR_ERR(base);

	/* Core Slice */
	hws[IMX8MM_CLK_A53_SRC] = imx_clk_hw_mux2("arm_a53_src", base + 0x8000, 24, 3, imx8mm_a53_sels, ARRAY_SIZE(imx8mm_a53_sels));
	hws[IMX8MM_CLK_A53_CG] = imx_clk_hw_gate3("arm_a53_cg", "arm_a53_src", base + 0x8000, 28);
	hws[IMX8MM_CLK_A53_DIV] = imx_clk_hw_divider2("arm_a53_div", "arm_a53_cg", base + 0x8000, 0, 3);

	hws[IMX8MM_CLK_M4_CORE] = imx8m_clk_hw_composite_core("arm_m4_core", imx8mm_m4_sels, base + 0x8080);
	hws[IMX8MM_CLK_VPU_CORE] = imx8m_clk_hw_composite_core("vpu_core", imx8mm_vpu_sels, base + 0x8100);
	hws[IMX8MM_CLK_GPU3D_CORE] = imx8m_clk_hw_composite_core("gpu3d_core", imx8mm_gpu3d_sels, base + 0x8180);
	hws[IMX8MM_CLK_GPU2D_CORE] = imx8m_clk_hw_composite_core("gpu2d_core", imx8mm_gpu2d_sels, base + 0x8200);

	/* For backwards compatibility */
	hws[IMX8MM_CLK_M4_SRC] = hws[IMX8MM_CLK_M4_CORE];
	hws[IMX8MM_CLK_M4_CG] = hws[IMX8MM_CLK_M4_CORE];
	hws[IMX8MM_CLK_M4_DIV] = hws[IMX8MM_CLK_M4_CORE];
	hws[IMX8MM_CLK_VPU_SRC] = hws[IMX8MM_CLK_VPU_CORE];
	hws[IMX8MM_CLK_VPU_CG] = hws[IMX8MM_CLK_VPU_CORE];
	hws[IMX8MM_CLK_VPU_DIV] = hws[IMX8MM_CLK_VPU_CORE];
	hws[IMX8MM_CLK_GPU3D_SRC] = hws[IMX8MM_CLK_GPU3D_CORE];
	hws[IMX8MM_CLK_GPU3D_CG] = hws[IMX8MM_CLK_GPU3D_CORE];
	hws[IMX8MM_CLK_GPU3D_DIV] = hws[IMX8MM_CLK_GPU3D_CORE];
	hws[IMX8MM_CLK_GPU2D_SRC] = hws[IMX8MM_CLK_GPU2D_CORE];
	hws[IMX8MM_CLK_GPU2D_CG] = hws[IMX8MM_CLK_GPU2D_CORE];
	hws[IMX8MM_CLK_GPU2D_DIV] = hws[IMX8MM_CLK_GPU2D_CORE];

	/* CORE SEL */
	hws[IMX8MM_CLK_A53_CORE] = imx_clk_hw_mux2("arm_a53_core", base + 0x9880, 24, 1, imx8mm_a53_core_sels, ARRAY_SIZE(imx8mm_a53_core_sels));

	/* BUS */
	hws[IMX8MM_CLK_MAIN_AXI] = imx8m_clk_hw_composite_critical("main_axi",  imx8mm_main_axi_sels, base + 0x8800);
	hws[IMX8MM_CLK_ENET_AXI] = imx8m_clk_hw_composite("enet_axi", imx8mm_enet_axi_sels, base + 0x8880);
	hws[IMX8MM_CLK_NAND_USDHC_BUS] = imx8m_clk_hw_composite_critical("nand_usdhc_bus", imx8mm_nand_usdhc_sels, base + 0x8900);
	hws[IMX8MM_CLK_VPU_BUS] = imx8m_clk_hw_composite("vpu_bus", imx8mm_vpu_bus_sels, base + 0x8980);
	hws[IMX8MM_CLK_DISP_AXI] = imx8m_clk_hw_composite("disp_axi", imx8mm_disp_axi_sels, base + 0x8a00);
	hws[IMX8MM_CLK_DISP_APB] = imx8m_clk_hw_composite("disp_apb", imx8mm_disp_apb_sels, base + 0x8a80);
	hws[IMX8MM_CLK_DISP_RTRM] = imx8m_clk_hw_composite("disp_rtrm", imx8mm_disp_rtrm_sels, base + 0x8b00);
	hws[IMX8MM_CLK_USB_BUS] = imx8m_clk_hw_composite("usb_bus", imx8mm_usb_bus_sels, base + 0x8b80);
	hws[IMX8MM_CLK_GPU_AXI] = imx8m_clk_hw_composite("gpu_axi", imx8mm_gpu_axi_sels, base + 0x8c00);
	hws[IMX8MM_CLK_GPU_AHB] = imx8m_clk_hw_composite("gpu_ahb", imx8mm_gpu_ahb_sels, base + 0x8c80);
	hws[IMX8MM_CLK_NOC] = imx8m_clk_hw_composite_critical("noc", imx8mm_noc_sels, base + 0x8d00);
	hws[IMX8MM_CLK_NOC_APB] = imx8m_clk_hw_composite_critical("noc_apb", imx8mm_noc_apb_sels, base + 0x8d80);

	/* AHB */
	hws[IMX8MM_CLK_AHB] = imx8m_clk_hw_composite_critical("ahb", imx8mm_ahb_sels, base + 0x9000);
	hws[IMX8MM_CLK_AUDIO_AHB] = imx8m_clk_hw_composite("audio_ahb", imx8mm_audio_ahb_sels, base + 0x9100);

	/* IPG */
	hws[IMX8MM_CLK_IPG_ROOT] = imx_clk_hw_divider2("ipg_root", "ahb", base + 0x9080, 0, 1);
	hws[IMX8MM_CLK_IPG_AUDIO_ROOT] = imx_clk_hw_divider2("ipg_audio_root", "audio_ahb", base + 0x9180, 0, 1);

	/*
	 * DRAM clocks are manipulated from TF-A outside clock framework.
	 * Mark with GET_RATE_NOCACHE to always read div value from hardware
	 */
	hws[IMX8MM_CLK_DRAM_ALT] = __imx8m_clk_hw_composite("dram_alt", imx8mm_dram_alt_sels, base + 0xa000, CLK_GET_RATE_NOCACHE);
	hws[IMX8MM_CLK_DRAM_APB] = __imx8m_clk_hw_composite("dram_apb", imx8mm_dram_apb_sels, base + 0xa080, CLK_IS_CRITICAL | CLK_GET_RATE_NOCACHE);

	/* IP */
	hws[IMX8MM_CLK_VPU_G1] = imx8m_clk_hw_composite("vpu_g1", imx8mm_vpu_g1_sels, base + 0xa100);
	hws[IMX8MM_CLK_VPU_G2] = imx8m_clk_hw_composite("vpu_g2", imx8mm_vpu_g2_sels, base + 0xa180);
	hws[IMX8MM_CLK_DISP_DTRC] = imx8m_clk_hw_composite("disp_dtrc", imx8mm_disp_dtrc_sels, base + 0xa200);
	hws[IMX8MM_CLK_DISP_DC8000] = imx8m_clk_hw_composite("disp_dc8000", imx8mm_disp_dc8000_sels, base + 0xa280);
	hws[IMX8MM_CLK_PCIE1_CTRL] = imx8m_clk_hw_composite("pcie1_ctrl", imx8mm_pcie1_ctrl_sels, base + 0xa300);
	hws[IMX8MM_CLK_PCIE1_PHY] = imx8m_clk_hw_composite("pcie1_phy", imx8mm_pcie1_phy_sels, base + 0xa380);
	hws[IMX8MM_CLK_PCIE1_AUX] = imx8m_clk_hw_composite("pcie1_aux", imx8mm_pcie1_aux_sels, base + 0xa400);
	hws[IMX8MM_CLK_DC_PIXEL] = imx8m_clk_hw_composite("dc_pixel", imx8mm_dc_pixel_sels, base + 0xa480);
	hws[IMX8MM_CLK_LCDIF_PIXEL] = imx8m_clk_hw_composite("lcdif_pixel", imx8mm_lcdif_pixel_sels, base + 0xa500);
	hws[IMX8MM_CLK_SAI1] = imx8m_clk_hw_composite("sai1", imx8mm_sai1_sels, base + 0xa580);
	hws[IMX8MM_CLK_SAI2] = imx8m_clk_hw_composite("sai2", imx8mm_sai2_sels, base + 0xa600);
	hws[IMX8MM_CLK_SAI3] = imx8m_clk_hw_composite("sai3", imx8mm_sai3_sels, base + 0xa680);
	hws[IMX8MM_CLK_SAI4] = imx8m_clk_hw_composite("sai4", imx8mm_sai4_sels, base + 0xa700);
	hws[IMX8MM_CLK_SAI5] = imx8m_clk_hw_composite("sai5", imx8mm_sai5_sels, base + 0xa780);
	hws[IMX8MM_CLK_SAI6] = imx8m_clk_hw_composite("sai6", imx8mm_sai6_sels, base + 0xa800);
	hws[IMX8MM_CLK_SPDIF1] = imx8m_clk_hw_composite("spdif1", imx8mm_spdif1_sels, base + 0xa880);
	hws[IMX8MM_CLK_SPDIF2] = imx8m_clk_hw_composite("spdif2", imx8mm_spdif2_sels, base + 0xa900);
	hws[IMX8MM_CLK_ENET_REF] = imx8m_clk_hw_composite("enet_ref", imx8mm_enet_ref_sels, base + 0xa980);
	hws[IMX8MM_CLK_ENET_TIMER] = imx8m_clk_hw_composite("enet_timer", imx8mm_enet_timer_sels, base + 0xaa00);
	hws[IMX8MM_CLK_ENET_PHY_REF] = imx8m_clk_hw_composite("enet_phy", imx8mm_enet_phy_sels, base + 0xaa80);
	hws[IMX8MM_CLK_NAND] = imx8m_clk_hw_composite("nand", imx8mm_nand_sels, base + 0xab00);
	hws[IMX8MM_CLK_QSPI] = imx8m_clk_hw_composite("qspi", imx8mm_qspi_sels, base + 0xab80);
	hws[IMX8MM_CLK_USDHC1] = imx8m_clk_hw_composite("usdhc1", imx8mm_usdhc1_sels, base + 0xac00);
	hws[IMX8MM_CLK_USDHC2] = imx8m_clk_hw_composite("usdhc2", imx8mm_usdhc2_sels, base + 0xac80);
	hws[IMX8MM_CLK_I2C1] = imx8m_clk_hw_composite("i2c1", imx8mm_i2c1_sels, base + 0xad00);
	hws[IMX8MM_CLK_I2C2] = imx8m_clk_hw_composite("i2c2", imx8mm_i2c2_sels, base + 0xad80);
	hws[IMX8MM_CLK_I2C3] = imx8m_clk_hw_composite("i2c3", imx8mm_i2c3_sels, base + 0xae00);
	hws[IMX8MM_CLK_I2C4] = imx8m_clk_hw_composite("i2c4", imx8mm_i2c4_sels, base + 0xae80);
	hws[IMX8MM_CLK_UART1] = imx8m_clk_hw_composite("uart1", imx8mm_uart1_sels, base + 0xaf00);
	hws[IMX8MM_CLK_UART2] = imx8m_clk_hw_composite("uart2", imx8mm_uart2_sels, base + 0xaf80);
	hws[IMX8MM_CLK_UART3] = imx8m_clk_hw_composite("uart3", imx8mm_uart3_sels, base + 0xb000);
	hws[IMX8MM_CLK_UART4] = imx8m_clk_hw_composite("uart4", imx8mm_uart4_sels, base + 0xb080);
	hws[IMX8MM_CLK_USB_CORE_REF] = imx8m_clk_hw_composite("usb_core_ref", imx8mm_usb_core_sels, base + 0xb100);
	hws[IMX8MM_CLK_USB_PHY_REF] = imx8m_clk_hw_composite("usb_phy_ref", imx8mm_usb_phy_sels, base + 0xb180);
	hws[IMX8MM_CLK_GIC] = imx8m_clk_hw_composite_critical("gic", imx8mm_gic_sels, base + 0xb200);
	hws[IMX8MM_CLK_ECSPI1] = imx8m_clk_hw_composite("ecspi1", imx8mm_ecspi1_sels, base + 0xb280);
	hws[IMX8MM_CLK_ECSPI2] = imx8m_clk_hw_composite("ecspi2", imx8mm_ecspi2_sels, base + 0xb300);
	hws[IMX8MM_CLK_PWM1] = imx8m_clk_hw_composite("pwm1", imx8mm_pwm1_sels, base + 0xb380);
	hws[IMX8MM_CLK_PWM2] = imx8m_clk_hw_composite("pwm2", imx8mm_pwm2_sels, base + 0xb400);
	hws[IMX8MM_CLK_PWM3] = imx8m_clk_hw_composite("pwm3", imx8mm_pwm3_sels, base + 0xb480);
	hws[IMX8MM_CLK_PWM4] = imx8m_clk_hw_composite("pwm4", imx8mm_pwm4_sels, base + 0xb500);
	hws[IMX8MM_CLK_GPT1] = imx8m_clk_hw_composite("gpt1", imx8mm_gpt1_sels, base + 0xb580);
	hws[IMX8MM_CLK_WDOG] = imx8m_clk_hw_composite("wdog", imx8mm_wdog_sels, base + 0xb900);
	hws[IMX8MM_CLK_WRCLK] = imx8m_clk_hw_composite("wrclk", imx8mm_wrclk_sels, base + 0xb980);
	hws[IMX8MM_CLK_CLKO1] = imx8m_clk_hw_composite("clko1", imx8mm_clko1_sels, base + 0xba00);
	hws[IMX8MM_CLK_CLKO2] = imx8m_clk_hw_composite("clko2", imx8mm_clko2_sels, base + 0xba80);
	hws[IMX8MM_CLK_DSI_CORE] = imx8m_clk_hw_composite("dsi_core", imx8mm_dsi_core_sels, base + 0xbb00);
	hws[IMX8MM_CLK_DSI_PHY_REF] = imx8m_clk_hw_composite("dsi_phy_ref", imx8mm_dsi_phy_sels, base + 0xbb80);
	hws[IMX8MM_CLK_DSI_DBI] = imx8m_clk_hw_composite("dsi_dbi", imx8mm_dsi_dbi_sels, base + 0xbc00);
	hws[IMX8MM_CLK_USDHC3] = imx8m_clk_hw_composite("usdhc3", imx8mm_usdhc3_sels, base + 0xbc80);
	hws[IMX8MM_CLK_CSI1_CORE] = imx8m_clk_hw_composite("csi1_core", imx8mm_csi1_core_sels, base + 0xbd00);
	hws[IMX8MM_CLK_CSI1_PHY_REF] = imx8m_clk_hw_composite("csi1_phy_ref", imx8mm_csi1_phy_sels, base + 0xbd80);
	hws[IMX8MM_CLK_CSI1_ESC] = imx8m_clk_hw_composite("csi1_esc", imx8mm_csi1_esc_sels, base + 0xbe00);
	hws[IMX8MM_CLK_CSI2_CORE] = imx8m_clk_hw_composite("csi2_core", imx8mm_csi2_core_sels, base + 0xbe80);
	hws[IMX8MM_CLK_CSI2_PHY_REF] = imx8m_clk_hw_composite("csi2_phy_ref", imx8mm_csi2_phy_sels, base + 0xbf00);
	hws[IMX8MM_CLK_CSI2_ESC] = imx8m_clk_hw_composite("csi2_esc", imx8mm_csi2_esc_sels, base + 0xbf80);
	hws[IMX8MM_CLK_PCIE2_CTRL] = imx8m_clk_hw_composite("pcie2_ctrl", imx8mm_pcie2_ctrl_sels, base + 0xc000);
	hws[IMX8MM_CLK_PCIE2_PHY] = imx8m_clk_hw_composite("pcie2_phy", imx8mm_pcie2_phy_sels, base + 0xc080);
	hws[IMX8MM_CLK_PCIE2_AUX] = imx8m_clk_hw_composite("pcie2_aux", imx8mm_pcie2_aux_sels, base + 0xc100);
	hws[IMX8MM_CLK_ECSPI3] = imx8m_clk_hw_composite("ecspi3", imx8mm_ecspi3_sels, base + 0xc180);
	hws[IMX8MM_CLK_PDM] = imx8m_clk_hw_composite("pdm", imx8mm_pdm_sels, base + 0xc200);
	hws[IMX8MM_CLK_VPU_H1] = imx8m_clk_hw_composite("vpu_h1", imx8mm_vpu_h1_sels, base + 0xc280);

	/* CCGR */
	hws[IMX8MM_CLK_ECSPI1_ROOT] = imx_clk_hw_gate4("ecspi1_root_clk", "ecspi1", base + 0x4070, 0);
	hws[IMX8MM_CLK_ECSPI2_ROOT] = imx_clk_hw_gate4("ecspi2_root_clk", "ecspi2", base + 0x4080, 0);
	hws[IMX8MM_CLK_ECSPI3_ROOT] = imx_clk_hw_gate4("ecspi3_root_clk", "ecspi3", base + 0x4090, 0);
	hws[IMX8MM_CLK_ENET1_ROOT] = imx_clk_hw_gate4("enet1_root_clk", "enet_axi", base + 0x40a0, 0);
	hws[IMX8MM_CLK_GPIO1_ROOT] = imx_clk_hw_gate4("gpio1_root_clk", "ipg_root", base + 0x40b0, 0);
	hws[IMX8MM_CLK_GPIO2_ROOT] = imx_clk_hw_gate4("gpio2_root_clk", "ipg_root", base + 0x40c0, 0);
	hws[IMX8MM_CLK_GPIO3_ROOT] = imx_clk_hw_gate4("gpio3_root_clk", "ipg_root", base + 0x40d0, 0);
	hws[IMX8MM_CLK_GPIO4_ROOT] = imx_clk_hw_gate4("gpio4_root_clk", "ipg_root", base + 0x40e0, 0);
	hws[IMX8MM_CLK_GPIO5_ROOT] = imx_clk_hw_gate4("gpio5_root_clk", "ipg_root", base + 0x40f0, 0);
	hws[IMX8MM_CLK_GPT1_ROOT] = imx_clk_hw_gate4("gpt1_root_clk", "gpt1", base + 0x4100, 0);
	hws[IMX8MM_CLK_I2C1_ROOT] = imx_clk_hw_gate4("i2c1_root_clk", "i2c1", base + 0x4170, 0);
	hws[IMX8MM_CLK_I2C2_ROOT] = imx_clk_hw_gate4("i2c2_root_clk", "i2c2", base + 0x4180, 0);
	hws[IMX8MM_CLK_I2C3_ROOT] = imx_clk_hw_gate4("i2c3_root_clk", "i2c3", base + 0x4190, 0);
	hws[IMX8MM_CLK_I2C4_ROOT] = imx_clk_hw_gate4("i2c4_root_clk", "i2c4", base + 0x41a0, 0);
	hws[IMX8MM_CLK_MU_ROOT] = imx_clk_hw_gate4("mu_root_clk", "ipg_root", base + 0x4210, 0);
	hws[IMX8MM_CLK_OCOTP_ROOT] = imx_clk_hw_gate4("ocotp_root_clk", "ipg_root", base + 0x4220, 0);
	hws[IMX8MM_CLK_PCIE1_ROOT] = imx_clk_hw_gate4("pcie1_root_clk", "pcie1_ctrl", base + 0x4250, 0);
	hws[IMX8MM_CLK_PWM1_ROOT] = imx_clk_hw_gate4("pwm1_root_clk", "pwm1", base + 0x4280, 0);
	hws[IMX8MM_CLK_PWM2_ROOT] = imx_clk_hw_gate4("pwm2_root_clk", "pwm2", base + 0x4290, 0);
	hws[IMX8MM_CLK_PWM3_ROOT] = imx_clk_hw_gate4("pwm3_root_clk", "pwm3", base + 0x42a0, 0);
	hws[IMX8MM_CLK_PWM4_ROOT] = imx_clk_hw_gate4("pwm4_root_clk", "pwm4", base + 0x42b0, 0);
	hws[IMX8MM_CLK_QSPI_ROOT] = imx_clk_hw_gate4("qspi_root_clk", "qspi", base + 0x42f0, 0);
	hws[IMX8MM_CLK_NAND_ROOT] = imx_clk_hw_gate2_shared2("nand_root_clk", "nand", base + 0x4300, 0, &share_count_nand);
	hws[IMX8MM_CLK_NAND_USDHC_BUS_RAWNAND_CLK] = imx_clk_hw_gate2_shared2("nand_usdhc_rawnand_clk", "nand_usdhc_bus", base + 0x4300, 0, &share_count_nand);
	hws[IMX8MM_CLK_SAI1_ROOT] = imx_clk_hw_gate2_shared2("sai1_root_clk", "sai1", base + 0x4330, 0, &share_count_sai1);
	hws[IMX8MM_CLK_SAI1_IPG] = imx_clk_hw_gate2_shared2("sai1_ipg_clk", "ipg_audio_root", base + 0x4330, 0, &share_count_sai1);
	hws[IMX8MM_CLK_SAI2_ROOT] = imx_clk_hw_gate2_shared2("sai2_root_clk", "sai2", base + 0x4340, 0, &share_count_sai2);
	hws[IMX8MM_CLK_SAI2_IPG] = imx_clk_hw_gate2_shared2("sai2_ipg_clk", "ipg_audio_root", base + 0x4340, 0, &share_count_sai2);
	hws[IMX8MM_CLK_SAI3_ROOT] = imx_clk_hw_gate2_shared2("sai3_root_clk", "sai3", base + 0x4350, 0, &share_count_sai3);
	hws[IMX8MM_CLK_SAI3_IPG] = imx_clk_hw_gate2_shared2("sai3_ipg_clk", "ipg_audio_root", base + 0x4350, 0, &share_count_sai3);
	hws[IMX8MM_CLK_SAI4_ROOT] = imx_clk_hw_gate2_shared2("sai4_root_clk", "sai4", base + 0x4360, 0, &share_count_sai4);
	hws[IMX8MM_CLK_SAI4_IPG] = imx_clk_hw_gate2_shared2("sai4_ipg_clk", "ipg_audio_root", base + 0x4360, 0, &share_count_sai4);
	hws[IMX8MM_CLK_SAI5_ROOT] = imx_clk_hw_gate2_shared2("sai5_root_clk", "sai5", base + 0x4370, 0, &share_count_sai5);
	hws[IMX8MM_CLK_SAI5_IPG] = imx_clk_hw_gate2_shared2("sai5_ipg_clk", "ipg_audio_root", base + 0x4370, 0, &share_count_sai5);
	hws[IMX8MM_CLK_SAI6_ROOT] = imx_clk_hw_gate2_shared2("sai6_root_clk", "sai6", base + 0x4380, 0, &share_count_sai6);
	hws[IMX8MM_CLK_SAI6_IPG] = imx_clk_hw_gate2_shared2("sai6_ipg_clk", "ipg_audio_root", base + 0x4380, 0, &share_count_sai6);
	hws[IMX8MM_CLK_SNVS_ROOT] = imx_clk_hw_gate4("snvs_root_clk", "ipg_root", base + 0x4470, 0);
	hws[IMX8MM_CLK_UART1_ROOT] = imx_clk_hw_gate4("uart1_root_clk", "uart1", base + 0x4490, 0);
	hws[IMX8MM_CLK_UART2_ROOT] = imx_clk_hw_gate4("uart2_root_clk", "uart2", base + 0x44a0, 0);
	hws[IMX8MM_CLK_UART3_ROOT] = imx_clk_hw_gate4("uart3_root_clk", "uart3", base + 0x44b0, 0);
	hws[IMX8MM_CLK_UART4_ROOT] = imx_clk_hw_gate4("uart4_root_clk", "uart4", base + 0x44c0, 0);
	hws[IMX8MM_CLK_USB1_CTRL_ROOT] = imx_clk_hw_gate4("usb1_ctrl_root_clk", "usb_bus", base + 0x44d0, 0);
	hws[IMX8MM_CLK_GPU3D_ROOT] = imx_clk_hw_gate4("gpu3d_root_clk", "gpu3d_core", base + 0x44f0, 0);
	hws[IMX8MM_CLK_USDHC1_ROOT] = imx_clk_hw_gate4("usdhc1_root_clk", "usdhc1", base + 0x4510, 0);
	hws[IMX8MM_CLK_USDHC2_ROOT] = imx_clk_hw_gate4("usdhc2_root_clk", "usdhc2", base + 0x4520, 0);
	hws[IMX8MM_CLK_WDOG1_ROOT] = imx_clk_hw_gate4("wdog1_root_clk", "wdog", base + 0x4530, 0);
	hws[IMX8MM_CLK_WDOG2_ROOT] = imx_clk_hw_gate4("wdog2_root_clk", "wdog", base + 0x4540, 0);
	hws[IMX8MM_CLK_WDOG3_ROOT] = imx_clk_hw_gate4("wdog3_root_clk", "wdog", base + 0x4550, 0);
	hws[IMX8MM_CLK_VPU_G1_ROOT] = imx_clk_hw_gate4("vpu_g1_root_clk", "vpu_g1", base + 0x4560, 0);
	hws[IMX8MM_CLK_GPU_BUS_ROOT] = imx_clk_hw_gate4("gpu_root_clk", "gpu_axi", base + 0x4570, 0);
	hws[IMX8MM_CLK_VPU_H1_ROOT] = imx_clk_hw_gate4("vpu_h1_root_clk", "vpu_h1", base + 0x4590, 0);
	hws[IMX8MM_CLK_VPU_G2_ROOT] = imx_clk_hw_gate4("vpu_g2_root_clk", "vpu_g2", base + 0x45a0, 0);
	hws[IMX8MM_CLK_PDM_ROOT] = imx_clk_hw_gate2_shared2("pdm_root_clk", "pdm", base + 0x45b0, 0, &share_count_pdm);
	hws[IMX8MM_CLK_PDM_IPG]  = imx_clk_hw_gate2_shared2("pdm_ipg_clk", "ipg_audio_root", base + 0x45b0, 0, &share_count_pdm);
	hws[IMX8MM_CLK_DISP_ROOT] = imx_clk_hw_gate2_shared2("disp_root_clk", "disp_dc8000", base + 0x45d0, 0, &share_count_disp);
	hws[IMX8MM_CLK_DISP_AXI_ROOT]  = imx_clk_hw_gate2_shared2("disp_axi_root_clk", "disp_axi", base + 0x45d0, 0, &share_count_disp);
	hws[IMX8MM_CLK_DISP_APB_ROOT]  = imx_clk_hw_gate2_shared2("disp_apb_root_clk", "disp_apb", base + 0x45d0, 0, &share_count_disp);
	hws[IMX8MM_CLK_DISP_RTRM_ROOT] = imx_clk_hw_gate2_shared2("disp_rtrm_root_clk", "disp_rtrm", base + 0x45d0, 0, &share_count_disp);
	hws[IMX8MM_CLK_USDHC3_ROOT] = imx_clk_hw_gate4("usdhc3_root_clk", "usdhc3", base + 0x45e0, 0);
	hws[IMX8MM_CLK_TMU_ROOT] = imx_clk_hw_gate4("tmu_root_clk", "ipg_root", base + 0x4620, 0);
	hws[IMX8MM_CLK_VPU_DEC_ROOT] = imx_clk_hw_gate4("vpu_dec_root_clk", "vpu_bus", base + 0x4630, 0);
	hws[IMX8MM_CLK_SDMA1_ROOT] = imx_clk_hw_gate4("sdma1_clk", "ipg_root", base + 0x43a0, 0);
	hws[IMX8MM_CLK_SDMA2_ROOT] = imx_clk_hw_gate4("sdma2_clk", "ipg_audio_root", base + 0x43b0, 0);
	hws[IMX8MM_CLK_SDMA3_ROOT] = imx_clk_hw_gate4("sdma3_clk", "ipg_audio_root", base + 0x45f0, 0);
	hws[IMX8MM_CLK_GPU2D_ROOT] = imx_clk_hw_gate4("gpu2d_root_clk", "gpu2d_core", base + 0x4660, 0);
	hws[IMX8MM_CLK_CSI1_ROOT] = imx_clk_hw_gate4("csi1_root_clk", "csi1_core", base + 0x4650, 0);

	hws[IMX8MM_CLK_GPT_3M] = imx_clk_hw_fixed_factor("gpt_3m", "osc_24m", 1, 8);

	hws[IMX8MM_CLK_DRAM_ALT_ROOT] = imx_clk_hw_fixed_factor("dram_alt_root", "dram_alt", 1, 4);
	hws[IMX8MM_CLK_DRAM_CORE] = imx_clk_hw_mux2_flags("dram_core_clk", base + 0x9800, 24, 1, imx8mm_dram_core_sels, ARRAY_SIZE(imx8mm_dram_core_sels), CLK_IS_CRITICAL);

	hws[IMX8MM_CLK_ARM] = imx_clk_hw_cpu("arm", "arm_a53_core",
					   hws[IMX8MM_CLK_A53_CORE]->clk,
					   hws[IMX8MM_CLK_A53_CORE]->clk,
					   hws[IMX8MM_ARM_PLL_OUT]->clk,
					   hws[IMX8MM_CLK_A53_DIV]->clk);

	clk_hw_set_parent(hws[IMX8MM_CLK_A53_SRC], hws[IMX8MM_SYS_PLL1_800M]);
	clk_hw_set_parent(hws[IMX8MM_CLK_A53_CORE], hws[IMX8MM_ARM_PLL_OUT]);

	imx_check_clk_hws(hws, IMX8MM_CLK_END);

	ret = of_clk_add_hw_provider(np, of_clk_hw_onecell_get, clk_hw_data);
	if (ret < 0) {
		dev_err(dev, "failed to register clks for i.MX8MM\n");
		goto unregister_hws;
	}

	for (i = 0; i < ARRAY_SIZE(uart_clk_ids); i++) {
		int index = uart_clk_ids[i];

		uart_hws[i] = &hws[index]->clk;
	}

	imx_register_uart_clocks(uart_hws);

	return 0;

unregister_hws:
	imx_unregister_hw_clocks(hws, IMX8MM_CLK_END);

	return ret;
}

static const struct of_device_id imx8mm_clk_of_match[] = {
	{ .compatible = "fsl,imx8mm-ccm" },
	{ /* Sentinel */ },
};
MODULE_DEVICE_TABLE(of, imx8mm_clk_of_match);

static struct platform_driver imx8mm_clk_driver = {
	.probe = imx8mm_clocks_probe,
	.driver = {
		.name = "imx8mm-ccm",
		/*
		 * Disable bind attributes: clocks are not removed and
		 * reloading the driver will crash or break devices.
		 */
		.suppress_bind_attrs = true,
		.of_match_table = of_match_ptr(imx8mm_clk_of_match),
	},
};
module_platform_driver(imx8mm_clk_driver);
