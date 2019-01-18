/*
 * Copyright (c) 2016, Fuzhou Rockchip Electronics Co., Ltd.
 * Author: Lin Huang <hl@rock-chips.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include <dt-bindings/clock/rockchip-ddr.h>
#include <dt-bindings/display/rk_fb.h>
#include <dt-bindings/soc/rockchip-system-status.h>
#include <drm/drmP.h>
#include <drm/drm_modeset_lock.h>
#include <linux/arm-smccc.h>
#include <linux/clk.h>
#include <linux/cpu.h>
#include <linux/cpufreq.h>
#include <linux/delay.h>
#include <linux/devfreq.h>
#include <linux/devfreq_cooling.h>
#include <linux/devfreq-event.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/pm_opp.h>
#include <linux/pm_qos.h>
#include <linux/reboot.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/rockchip/rockchip_sip.h>
#include <linux/rwsem.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/suspend.h>
#include <linux/thermal.h>

#include <soc/rockchip/pm_domains.h>
#include <soc/rockchip/rkfb_dmc.h>
#include <soc/rockchip/rockchip_dmc.h>
#include <soc/rockchip/rockchip_sip.h>
#include <soc/rockchip/rockchip-system-status.h>
#include <soc/rockchip/rockchip_opp_select.h>
#include <soc/rockchip/scpi.h>
#include <uapi/drm/drm_mode.h>

#include "governor.h"

#define system_status_to_dmcfreq(nb) container_of(nb, struct rockchip_dmcfreq, \
						  status_nb)
#define reboot_to_dmcfreq(nb) container_of(nb, struct rockchip_dmcfreq, \
					   reboot_nb)
#define boost_to_dmcfreq(work) container_of(work, struct rockchip_dmcfreq, \
					    boost_work)
#define msch_rl_to_dmcfreq(work) container_of(to_delayed_work(work), \
					      struct rockchip_dmcfreq, \
					      msch_rl_work)
#define input_hd_to_dmcfreq(hd) container_of(hd, struct rockchip_dmcfreq, \
					     input_handler)

#define VIDEO_1080P_SIZE	(1920 * 1080)
#define FIQ_INIT_HANDLER	(0x1)
#define FIQ_CPU_TGT_BOOT	(0x0) /* to booting cpu */
#define FIQ_NUM_FOR_DCF		(143) /* NA irq map to fiq for dcf */
#define DTS_PAR_OFFSET		(4096)
#define MSCH_RL_DELAY_TIME	50 /* ms */

#define FALLBACK_STATIC_TEMPERATURE 55000

struct freq_map_table {
	unsigned int min;
	unsigned int max;
	unsigned long freq;
};

struct rl_map_table {
	unsigned int pn; /* panel number */
	unsigned int rl; /* readlatency */
};

struct video_info {
	unsigned int width;
	unsigned int height;
	unsigned int ishevc;
	unsigned int videoFramerate;
	unsigned int streamBitrate;
	struct list_head node;
};

struct share_params {
	u32 hz;
	u32 lcdc_type;
	u32 vop;
	u32 vop_dclk_mode;
	u32 sr_idle_en;
	u32 addr_mcu_el3;
	/*
	 * 1: need to wait flag1
	 * 0: never wait flag1
	 */
	u32 wait_flag1;
	/*
	 * 1: need to wait flag1
	 * 0: never wait flag1
	 */
	u32 wait_flag0;
	u32 complt_hwirq;
	 /* if need, add parameter after */
};

static struct share_params *ddr_psci_param;

/* hope this define can adapt all future platfor */
static const char * const px30_dts_timing[] = {
	"ddr2_speed_bin",
	"ddr3_speed_bin",
	"ddr4_speed_bin",
	"pd_idle",
	"sr_idle",
	"sr_mc_gate_idle",
	"srpd_lite_idle",
	"standby_idle",

	"auto_pd_dis_freq",
	"auto_sr_dis_freq",
	"ddr2_dll_dis_freq",
	"ddr3_dll_dis_freq",
	"ddr4_dll_dis_freq",
	"phy_dll_dis_freq",

	"ddr2_odt_dis_freq",
	"phy_ddr2_odt_dis_freq",
	"ddr2_drv",
	"ddr2_odt",
	"phy_ddr2_ca_drv",
	"phy_ddr2_ck_drv",
	"phy_ddr2_dq_drv",
	"phy_ddr2_odt",

	"ddr3_odt_dis_freq",
	"phy_ddr3_odt_dis_freq",
	"ddr3_drv",
	"ddr3_odt",
	"phy_ddr3_ca_drv",
	"phy_ddr3_ck_drv",
	"phy_ddr3_dq_drv",
	"phy_ddr3_odt",

	"phy_lpddr2_odt_dis_freq",
	"lpddr2_drv",
	"phy_lpddr2_ca_drv",
	"phy_lpddr2_ck_drv",
	"phy_lpddr2_dq_drv",
	"phy_lpddr2_odt",

	"lpddr3_odt_dis_freq",
	"phy_lpddr3_odt_dis_freq",
	"lpddr3_drv",
	"lpddr3_odt",
	"phy_lpddr3_ca_drv",
	"phy_lpddr3_ck_drv",
	"phy_lpddr3_dq_drv",
	"phy_lpddr3_odt",

	"lpddr4_odt_dis_freq",
	"phy_lpddr4_odt_dis_freq",
	"lpddr4_drv",
	"lpddr4_dq_odt",
	"lpddr4_ca_odt",
	"phy_lpddr4_ca_drv",
	"phy_lpddr4_ck_cs_drv",
	"phy_lpddr4_dq_drv",
	"phy_lpddr4_odt",

	"ddr4_odt_dis_freq",
	"phy_ddr4_odt_dis_freq",
	"ddr4_drv",
	"ddr4_odt",
	"phy_ddr4_ca_drv",
	"phy_ddr4_ck_drv",
	"phy_ddr4_dq_drv",
	"phy_ddr4_odt",
};

struct px30_ddr_dts_config_timing {
	unsigned int ddr2_speed_bin;
	unsigned int ddr3_speed_bin;
	unsigned int ddr4_speed_bin;
	unsigned int pd_idle;
	unsigned int sr_idle;
	unsigned int sr_mc_gate_idle;
	unsigned int srpd_lite_idle;
	unsigned int standby_idle;

	unsigned int auto_pd_dis_freq;
	unsigned int auto_sr_dis_freq;
	/* for ddr2 only */
	unsigned int ddr2_dll_dis_freq;
	/* for ddr3 only */
	unsigned int ddr3_dll_dis_freq;
	/* for ddr4 only */
	unsigned int ddr4_dll_dis_freq;
	unsigned int phy_dll_dis_freq;

	unsigned int ddr2_odt_dis_freq;
	unsigned int phy_ddr2_odt_dis_freq;
	unsigned int ddr2_drv;
	unsigned int ddr2_odt;
	unsigned int phy_ddr2_ca_drv;
	unsigned int phy_ddr2_ck_drv;
	unsigned int phy_ddr2_dq_drv;
	unsigned int phy_ddr2_odt;

	unsigned int ddr3_odt_dis_freq;
	unsigned int phy_ddr3_odt_dis_freq;
	unsigned int ddr3_drv;
	unsigned int ddr3_odt;
	unsigned int phy_ddr3_ca_drv;
	unsigned int phy_ddr3_ck_drv;
	unsigned int phy_ddr3_dq_drv;
	unsigned int phy_ddr3_odt;

	unsigned int phy_lpddr2_odt_dis_freq;
	unsigned int lpddr2_drv;
	unsigned int phy_lpddr2_ca_drv;
	unsigned int phy_lpddr2_ck_drv;
	unsigned int phy_lpddr2_dq_drv;
	unsigned int phy_lpddr2_odt;

	unsigned int lpddr3_odt_dis_freq;
	unsigned int phy_lpddr3_odt_dis_freq;
	unsigned int lpddr3_drv;
	unsigned int lpddr3_odt;
	unsigned int phy_lpddr3_ca_drv;
	unsigned int phy_lpddr3_ck_drv;
	unsigned int phy_lpddr3_dq_drv;
	unsigned int phy_lpddr3_odt;

	unsigned int lpddr4_odt_dis_freq;
	unsigned int phy_lpddr4_odt_dis_freq;
	unsigned int lpddr4_drv;
	unsigned int lpddr4_dq_odt;
	unsigned int lpddr4_ca_odt;
	unsigned int phy_lpddr4_ca_drv;
	unsigned int phy_lpddr4_ck_cs_drv;
	unsigned int phy_lpddr4_dq_drv;
	unsigned int phy_lpddr4_odt;

	unsigned int ddr4_odt_dis_freq;
	unsigned int phy_ddr4_odt_dis_freq;
	unsigned int ddr4_drv;
	unsigned int ddr4_odt;
	unsigned int phy_ddr4_ca_drv;
	unsigned int phy_ddr4_ck_drv;
	unsigned int phy_ddr4_dq_drv;
	unsigned int phy_ddr4_odt;

	unsigned int ca_skew[15];
	unsigned int cs0_skew[44];
	unsigned int cs1_skew[44];

	unsigned int available;
};

static const char * const rk1808_dts_ca_timing[] = {
	"a0_ddr3a9_de-skew",
	"a1_ddr3a14_de-skew",
	"a2_ddr3a13_de-skew",
	"a3_ddr3a11_de-skew",
	"a4_ddr3a2_de-skew",
	"a5_ddr3a4_de-skew",
	"a6_ddr3a3_de-skew",
	"a7_ddr3a6_de-skew",
	"a8_ddr3a5_de-skew",
	"a9_ddr3a1_de-skew",
	"a10_ddr3a0_de-skew",
	"a11_ddr3a7_de-skew",
	"a12_ddr3casb_de-skew",
	"a13_ddr3a8_de-skew",
	"a14_ddr3odt0_de-skew",
	"a15_ddr3ba1_de-skew",
	"a16_ddr3rasb_de-skew",
	"a17_ddr3null_de-skew",
	"ba0_ddr3ba2_de-skew",
	"ba1_ddr3a12_de-skew",
	"bg0_ddr3ba0_de-skew",
	"bg1_ddr3web_de-skew",
	"cke_ddr3cke_de-skew",
	"ck_ddr3ck_de-skew",
	"ckb_ddr3ckb_de-skew",
	"csb0_ddr3a10_de-skew",
	"odt0_ddr3a15_de-skew",
	"resetn_ddr3resetn_de-skew",
	"actn_ddr3csb0_de-skew",
	"csb1_ddr3csb1_de-skew",
	"odt1_ddr3odt1_de-skew",
};

static const char * const rk1808_dts_cs0_a_timing[] = {
	"cs0_dm0_rx_de-skew",
	"cs0_dm0_tx_de-skew",
	"cs0_dq0_rx_de-skew",
	"cs0_dq0_tx_de-skew",
	"cs0_dq1_rx_de-skew",
	"cs0_dq1_tx_de-skew",
	"cs0_dq2_rx_de-skew",
	"cs0_dq2_tx_de-skew",
	"cs0_dq3_rx_de-skew",
	"cs0_dq3_tx_de-skew",
	"cs0_dq4_rx_de-skew",
	"cs0_dq4_tx_de-skew",
	"cs0_dq5_rx_de-skew",
	"cs0_dq5_tx_de-skew",
	"cs0_dq6_rx_de-skew",
	"cs0_dq6_tx_de-skew",
	"cs0_dq7_rx_de-skew",
	"cs0_dq7_tx_de-skew",
	"cs0_dqs0p_rx_de-skew",
	"cs0_dqs0p_tx_de-skew",
	"cs0_dqs0n_tx_de-skew",
	"cs0_dm1_rx_de-skew",
	"cs0_dm1_tx_de-skew",
	"cs0_dq8_rx_de-skew",
	"cs0_dq8_tx_de-skew",
	"cs0_dq9_rx_de-skew",
	"cs0_dq9_tx_de-skew",
	"cs0_dq10_rx_de-skew",
	"cs0_dq10_tx_de-skew",
	"cs0_dq11_rx_de-skew",
	"cs0_dq11_tx_de-skew",
	"cs0_dq12_rx_de-skew",
	"cs0_dq12_tx_de-skew",
	"cs0_dq13_rx_de-skew",
	"cs0_dq13_tx_de-skew",
	"cs0_dq14_rx_de-skew",
	"cs0_dq14_tx_de-skew",
	"cs0_dq15_rx_de-skew",
	"cs0_dq15_tx_de-skew",
	"cs0_dqs1p_rx_de-skew",
	"cs0_dqs1p_tx_de-skew",
	"cs0_dqs1n_tx_de-skew",
	"cs0_dqs0n_rx_de-skew",
	"cs0_dqs1n_rx_de-skew",
};

static const char * const rk1808_dts_cs0_b_timing[] = {
	"cs0_dm2_rx_de-skew",
	"cs0_dm2_tx_de-skew",
	"cs0_dq16_rx_de-skew",
	"cs0_dq16_tx_de-skew",
	"cs0_dq17_rx_de-skew",
	"cs0_dq17_tx_de-skew",
	"cs0_dq18_rx_de-skew",
	"cs0_dq18_tx_de-skew",
	"cs0_dq19_rx_de-skew",
	"cs0_dq19_tx_de-skew",
	"cs0_dq20_rx_de-skew",
	"cs0_dq20_tx_de-skew",
	"cs0_dq21_rx_de-skew",
	"cs0_dq21_tx_de-skew",
	"cs0_dq22_rx_de-skew",
	"cs0_dq22_tx_de-skew",
	"cs0_dq23_rx_de-skew",
	"cs0_dq23_tx_de-skew",
	"cs0_dqs2p_rx_de-skew",
	"cs0_dqs2p_tx_de-skew",
	"cs0_dqs2n_tx_de-skew",
	"cs0_dm3_rx_de-skew",
	"cs0_dm3_tx_de-skew",
	"cs0_dq24_rx_de-skew",
	"cs0_dq24_tx_de-skew",
	"cs0_dq25_rx_de-skew",
	"cs0_dq25_tx_de-skew",
	"cs0_dq26_rx_de-skew",
	"cs0_dq26_tx_de-skew",
	"cs0_dq27_rx_de-skew",
	"cs0_dq27_tx_de-skew",
	"cs0_dq28_rx_de-skew",
	"cs0_dq28_tx_de-skew",
	"cs0_dq29_rx_de-skew",
	"cs0_dq29_tx_de-skew",
	"cs0_dq30_rx_de-skew",
	"cs0_dq30_tx_de-skew",
	"cs0_dq31_rx_de-skew",
	"cs0_dq31_tx_de-skew",
	"cs0_dqs3p_rx_de-skew",
	"cs0_dqs3p_tx_de-skew",
	"cs0_dqs3n_tx_de-skew",
	"cs0_dqs2n_rx_de-skew",
	"cs0_dqs3n_rx_de-skew",
};

static const char * const rk1808_dts_cs1_a_timing[] = {
	"cs1_dm0_rx_de-skew",
	"cs1_dm0_tx_de-skew",
	"cs1_dq0_rx_de-skew",
	"cs1_dq0_tx_de-skew",
	"cs1_dq1_rx_de-skew",
	"cs1_dq1_tx_de-skew",
	"cs1_dq2_rx_de-skew",
	"cs1_dq2_tx_de-skew",
	"cs1_dq3_rx_de-skew",
	"cs1_dq3_tx_de-skew",
	"cs1_dq4_rx_de-skew",
	"cs1_dq4_tx_de-skew",
	"cs1_dq5_rx_de-skew",
	"cs1_dq5_tx_de-skew",
	"cs1_dq6_rx_de-skew",
	"cs1_dq6_tx_de-skew",
	"cs1_dq7_rx_de-skew",
	"cs1_dq7_tx_de-skew",
	"cs1_dqs0p_rx_de-skew",
	"cs1_dqs0p_tx_de-skew",
	"cs1_dqs0n_tx_de-skew",
	"cs1_dm1_rx_de-skew",
	"cs1_dm1_tx_de-skew",
	"cs1_dq8_rx_de-skew",
	"cs1_dq8_tx_de-skew",
	"cs1_dq9_rx_de-skew",
	"cs1_dq9_tx_de-skew",
	"cs1_dq10_rx_de-skew",
	"cs1_dq10_tx_de-skew",
	"cs1_dq11_rx_de-skew",
	"cs1_dq11_tx_de-skew",
	"cs1_dq12_rx_de-skew",
	"cs1_dq12_tx_de-skew",
	"cs1_dq13_rx_de-skew",
	"cs1_dq13_tx_de-skew",
	"cs1_dq14_rx_de-skew",
	"cs1_dq14_tx_de-skew",
	"cs1_dq15_rx_de-skew",
	"cs1_dq15_tx_de-skew",
	"cs1_dqs1p_rx_de-skew",
	"cs1_dqs1p_tx_de-skew",
	"cs1_dqs1n_tx_de-skew",
	"cs1_dqs0n_rx_de-skew",
	"cs1_dqs1n_rx_de-skew",
};

static const char * const rk1808_dts_cs1_b_timing[] = {
	"cs1_dm2_rx_de-skew",
	"cs1_dm2_tx_de-skew",
	"cs1_dq16_rx_de-skew",
	"cs1_dq16_tx_de-skew",
	"cs1_dq17_rx_de-skew",
	"cs1_dq17_tx_de-skew",
	"cs1_dq18_rx_de-skew",
	"cs1_dq18_tx_de-skew",
	"cs1_dq19_rx_de-skew",
	"cs1_dq19_tx_de-skew",
	"cs1_dq20_rx_de-skew",
	"cs1_dq20_tx_de-skew",
	"cs1_dq21_rx_de-skew",
	"cs1_dq21_tx_de-skew",
	"cs1_dq22_rx_de-skew",
	"cs1_dq22_tx_de-skew",
	"cs1_dq23_rx_de-skew",
	"cs1_dq23_tx_de-skew",
	"cs1_dqs2p_rx_de-skew",
	"cs1_dqs2p_tx_de-skew",
	"cs1_dqs2n_tx_de-skew",
	"cs1_dm3_rx_de-skew",
	"cs1_dm3_tx_de-skew",
	"cs1_dq24_rx_de-skew",
	"cs1_dq24_tx_de-skew",
	"cs1_dq25_rx_de-skew",
	"cs1_dq25_tx_de-skew",
	"cs1_dq26_rx_de-skew",
	"cs1_dq26_tx_de-skew",
	"cs1_dq27_rx_de-skew",
	"cs1_dq27_tx_de-skew",
	"cs1_dq28_rx_de-skew",
	"cs1_dq28_tx_de-skew",
	"cs1_dq29_rx_de-skew",
	"cs1_dq29_tx_de-skew",
	"cs1_dq30_rx_de-skew",
	"cs1_dq30_tx_de-skew",
	"cs1_dq31_rx_de-skew",
	"cs1_dq31_tx_de-skew",
	"cs1_dqs3p_rx_de-skew",
	"cs1_dqs3p_tx_de-skew",
	"cs1_dqs3n_tx_de-skew",
	"cs1_dqs2n_rx_de-skew",
	"cs1_dqs3n_rx_de-skew",
};

struct rk1808_ddr_dts_config_timing {
	unsigned int ddr2_speed_bin;
	unsigned int ddr3_speed_bin;
	unsigned int ddr4_speed_bin;
	unsigned int pd_idle;
	unsigned int sr_idle;
	unsigned int sr_mc_gate_idle;
	unsigned int srpd_lite_idle;
	unsigned int standby_idle;

	unsigned int auto_pd_dis_freq;
	unsigned int auto_sr_dis_freq;
	/* for ddr2 only */
	unsigned int ddr2_dll_dis_freq;
	/* for ddr3 only */
	unsigned int ddr3_dll_dis_freq;
	/* for ddr4 only */
	unsigned int ddr4_dll_dis_freq;
	unsigned int phy_dll_dis_freq;

	unsigned int ddr2_odt_dis_freq;
	unsigned int phy_ddr2_odt_dis_freq;
	unsigned int ddr2_drv;
	unsigned int ddr2_odt;
	unsigned int phy_ddr2_ca_drv;
	unsigned int phy_ddr2_ck_drv;
	unsigned int phy_ddr2_dq_drv;
	unsigned int phy_ddr2_odt;

	unsigned int ddr3_odt_dis_freq;
	unsigned int phy_ddr3_odt_dis_freq;
	unsigned int ddr3_drv;
	unsigned int ddr3_odt;
	unsigned int phy_ddr3_ca_drv;
	unsigned int phy_ddr3_ck_drv;
	unsigned int phy_ddr3_dq_drv;
	unsigned int phy_ddr3_odt;

	unsigned int phy_lpddr2_odt_dis_freq;
	unsigned int lpddr2_drv;
	unsigned int phy_lpddr2_ca_drv;
	unsigned int phy_lpddr2_ck_drv;
	unsigned int phy_lpddr2_dq_drv;
	unsigned int phy_lpddr2_odt;

	unsigned int lpddr3_odt_dis_freq;
	unsigned int phy_lpddr3_odt_dis_freq;
	unsigned int lpddr3_drv;
	unsigned int lpddr3_odt;
	unsigned int phy_lpddr3_ca_drv;
	unsigned int phy_lpddr3_ck_drv;
	unsigned int phy_lpddr3_dq_drv;
	unsigned int phy_lpddr3_odt;

	unsigned int lpddr4_odt_dis_freq;
	unsigned int phy_lpddr4_odt_dis_freq;
	unsigned int lpddr4_drv;
	unsigned int lpddr4_dq_odt;
	unsigned int lpddr4_ca_odt;
	unsigned int phy_lpddr4_ca_drv;
	unsigned int phy_lpddr4_ck_cs_drv;
	unsigned int phy_lpddr4_dq_drv;
	unsigned int phy_lpddr4_odt;

	unsigned int ddr4_odt_dis_freq;
	unsigned int phy_ddr4_odt_dis_freq;
	unsigned int ddr4_drv;
	unsigned int ddr4_odt;
	unsigned int phy_ddr4_ca_drv;
	unsigned int phy_ddr4_ck_drv;
	unsigned int phy_ddr4_dq_drv;
	unsigned int phy_ddr4_odt;

	unsigned int ca_de_skew[31];
	unsigned int cs0_a_de_skew[44];
	unsigned int cs0_b_de_skew[44];
	unsigned int cs1_a_de_skew[44];
	unsigned int cs1_b_de_skew[44];

	unsigned int available;
};

static const char * const rk3128_dts_timing[] = {
	"ddr3_speed_bin",
	"pd_idle",
	"sr_idle",
	"auto_pd_dis_freq",
	"auto_sr_dis_freq",
	"ddr3_dll_dis_freq",
	"lpddr2_dll_dis_freq",
	"phy_dll_dis_freq",
	"ddr3_odt_dis_freq",
	"phy_ddr3_odt_disb_freq",
	"ddr3_drv",
	"ddr3_odt",
	"phy_ddr3_clk_drv",
	"phy_ddr3_cmd_drv",
	"phy_ddr3_dqs_drv",
	"phy_ddr3_odt",
	"lpddr2_drv",
	"phy_lpddr2_clk_drv",
	"phy_lpddr2_cmd_drv",
	"phy_lpddr2_dqs_drv"
};

struct rk3128_ddr_dts_config_timing {
	u32 ddr3_speed_bin;
	u32 pd_idle;
	u32 sr_idle;
	u32 auto_pd_dis_freq;
	u32 auto_sr_dis_freq;
	u32 ddr3_dll_dis_freq;
	u32 lpddr2_dll_dis_freq;
	u32 phy_dll_dis_freq;
	u32 ddr3_odt_dis_freq;
	u32 phy_ddr3_odt_disb_freq;
	u32 ddr3_drv;
	u32 ddr3_odt;
	u32 phy_ddr3_clk_drv;
	u32 phy_ddr3_cmd_drv;
	u32 phy_ddr3_dqs_drv;
	u32 phy_ddr3_odt;
	u32 lpddr2_drv;
	u32 phy_lpddr2_clk_drv;
	u32 phy_lpddr2_cmd_drv;
	u32 phy_lpddr2_dqs_drv;
	u32 available;
};

static const char * const rk3228_dts_timing[] = {
	"dram_spd_bin",
	"sr_idle",
	"pd_idle",
	"dram_dll_disb_freq",
	"phy_dll_disb_freq",
	"dram_odt_disb_freq",
	"phy_odt_disb_freq",
	"ddr3_drv",
	"ddr3_odt",
	"lpddr3_drv",
	"lpddr3_odt",
	"lpddr2_drv",
	"phy_ddr3_clk_drv",
	"phy_ddr3_cmd_drv",
	"phy_ddr3_dqs_drv",
	"phy_ddr3_odt",
	"phy_lp23_clk_drv",
	"phy_lp23_cmd_drv",
	"phy_lp23_dqs_drv",
	"phy_lp3_odt"
};

struct rk3228_ddr_dts_config_timing {
	u32 dram_spd_bin;
	u32 sr_idle;
	u32 pd_idle;
	u32 dram_dll_dis_freq;
	u32 phy_dll_dis_freq;
	u32 dram_odt_dis_freq;
	u32 phy_odt_dis_freq;
	u32 ddr3_drv;
	u32 ddr3_odt;
	u32 lpddr3_drv;
	u32 lpddr3_odt;
	u32 lpddr2_drv;
	u32 phy_ddr3_clk_drv;
	u32 phy_ddr3_cmd_drv;
	u32 phy_ddr3_dqs_drv;
	u32 phy_ddr3_odt;
	u32 phy_lp23_clk_drv;
	u32 phy_lp23_cmd_drv;
	u32 phy_lp23_dqs_drv;
	u32 phy_lp3_odt;
};

static const char * const rk3288_dts_timing[] = {
	"ddr3_speed_bin",
	"pd_idle",
	"sr_idle",

	"auto_pd_dis_freq",
	"auto_sr_dis_freq",
	/* for ddr3 only */
	"ddr3_dll_dis_freq",
	"phy_dll_dis_freq",

	"ddr3_odt_dis_freq",
	"phy_ddr3_odt_dis_freq",
	"ddr3_drv",
	"ddr3_odt",
	"phy_ddr3_drv",
	"phy_ddr3_odt",

	"lpddr2_drv",
	"phy_lpddr2_drv",

	"lpddr3_odt_dis_freq",
	"phy_lpddr3_odt_dis_freq",
	"lpddr3_drv",
	"lpddr3_odt",
	"phy_lpddr3_drv",
	"phy_lpddr3_odt"
};

struct rk3288_ddr_dts_config_timing {
	unsigned int ddr3_speed_bin;
	unsigned int pd_idle;
	unsigned int sr_idle;

	unsigned int auto_pd_dis_freq;
	unsigned int auto_sr_dis_freq;
	/* for ddr3 only */
	unsigned int ddr3_dll_dis_freq;
	unsigned int phy_dll_dis_freq;

	unsigned int ddr3_odt_dis_freq;
	unsigned int phy_ddr3_odt_dis_freq;
	unsigned int ddr3_drv;
	unsigned int ddr3_odt;
	unsigned int phy_ddr3_drv;
	unsigned int phy_ddr3_odt;

	unsigned int lpddr2_drv;
	unsigned int phy_lpddr2_drv;

	unsigned int lpddr3_odt_dis_freq;
	unsigned int phy_lpddr3_odt_dis_freq;
	unsigned int lpddr3_drv;
	unsigned int lpddr3_odt;
	unsigned int phy_lpddr3_drv;
	unsigned int phy_lpddr3_odt;

	unsigned int available;
};

/* hope this define can adapt all future platfor */
static const char * const rk3328_dts_timing[] = {
	"ddr3_speed_bin",
	"ddr4_speed_bin",
	"pd_idle",
	"sr_idle",
	"sr_mc_gate_idle",
	"srpd_lite_idle",
	"standby_idle",

	"auto_pd_dis_freq",
	"auto_sr_dis_freq",
	"ddr3_dll_dis_freq",
	"ddr4_dll_dis_freq",
	"phy_dll_dis_freq",

	"ddr3_odt_dis_freq",
	"phy_ddr3_odt_dis_freq",
	"ddr3_drv",
	"ddr3_odt",
	"phy_ddr3_ca_drv",
	"phy_ddr3_ck_drv",
	"phy_ddr3_dq_drv",
	"phy_ddr3_odt",

	"lpddr3_odt_dis_freq",
	"phy_lpddr3_odt_dis_freq",
	"lpddr3_drv",
	"lpddr3_odt",
	"phy_lpddr3_ca_drv",
	"phy_lpddr3_ck_drv",
	"phy_lpddr3_dq_drv",
	"phy_lpddr3_odt",

	"lpddr4_odt_dis_freq",
	"phy_lpddr4_odt_dis_freq",
	"lpddr4_drv",
	"lpddr4_dq_odt",
	"lpddr4_ca_odt",
	"phy_lpddr4_ca_drv",
	"phy_lpddr4_ck_cs_drv",
	"phy_lpddr4_dq_drv",
	"phy_lpddr4_odt",

	"ddr4_odt_dis_freq",
	"phy_ddr4_odt_dis_freq",
	"ddr4_drv",
	"ddr4_odt",
	"phy_ddr4_ca_drv",
	"phy_ddr4_ck_drv",
	"phy_ddr4_dq_drv",
	"phy_ddr4_odt",
};

static const char * const rk3328_dts_ca_timing[] = {
	"ddr3a1_ddr4a9_de-skew",
	"ddr3a0_ddr4a10_de-skew",
	"ddr3a3_ddr4a6_de-skew",
	"ddr3a2_ddr4a4_de-skew",
	"ddr3a5_ddr4a8_de-skew",
	"ddr3a4_ddr4a5_de-skew",
	"ddr3a7_ddr4a11_de-skew",
	"ddr3a6_ddr4a7_de-skew",
	"ddr3a9_ddr4a0_de-skew",
	"ddr3a8_ddr4a13_de-skew",
	"ddr3a11_ddr4a3_de-skew",
	"ddr3a10_ddr4cs0_de-skew",
	"ddr3a13_ddr4a2_de-skew",
	"ddr3a12_ddr4ba1_de-skew",
	"ddr3a15_ddr4odt0_de-skew",
	"ddr3a14_ddr4a1_de-skew",
	"ddr3ba1_ddr4a15_de-skew",
	"ddr3ba0_ddr4bg0_de-skew",
	"ddr3ras_ddr4cke_de-skew",
	"ddr3ba2_ddr4ba0_de-skew",
	"ddr3we_ddr4bg1_de-skew",
	"ddr3cas_ddr4a12_de-skew",
	"ddr3ckn_ddr4ckn_de-skew",
	"ddr3ckp_ddr4ckp_de-skew",
	"ddr3cke_ddr4a16_de-skew",
	"ddr3odt0_ddr4a14_de-skew",
	"ddr3cs0_ddr4act_de-skew",
	"ddr3reset_ddr4reset_de-skew",
	"ddr3cs1_ddr4cs1_de-skew",
	"ddr3odt1_ddr4odt1_de-skew",
};

static const char * const rk3328_dts_cs0_timing[] = {
	"cs0_dm0_rx_de-skew",
	"cs0_dm0_tx_de-skew",
	"cs0_dq0_rx_de-skew",
	"cs0_dq0_tx_de-skew",
	"cs0_dq1_rx_de-skew",
	"cs0_dq1_tx_de-skew",
	"cs0_dq2_rx_de-skew",
	"cs0_dq2_tx_de-skew",
	"cs0_dq3_rx_de-skew",
	"cs0_dq3_tx_de-skew",
	"cs0_dq4_rx_de-skew",
	"cs0_dq4_tx_de-skew",
	"cs0_dq5_rx_de-skew",
	"cs0_dq5_tx_de-skew",
	"cs0_dq6_rx_de-skew",
	"cs0_dq6_tx_de-skew",
	"cs0_dq7_rx_de-skew",
	"cs0_dq7_tx_de-skew",
	"cs0_dqs0_rx_de-skew",
	"cs0_dqs0p_tx_de-skew",
	"cs0_dqs0n_tx_de-skew",

	"cs0_dm1_rx_de-skew",
	"cs0_dm1_tx_de-skew",
	"cs0_dq8_rx_de-skew",
	"cs0_dq8_tx_de-skew",
	"cs0_dq9_rx_de-skew",
	"cs0_dq9_tx_de-skew",
	"cs0_dq10_rx_de-skew",
	"cs0_dq10_tx_de-skew",
	"cs0_dq11_rx_de-skew",
	"cs0_dq11_tx_de-skew",
	"cs0_dq12_rx_de-skew",
	"cs0_dq12_tx_de-skew",
	"cs0_dq13_rx_de-skew",
	"cs0_dq13_tx_de-skew",
	"cs0_dq14_rx_de-skew",
	"cs0_dq14_tx_de-skew",
	"cs0_dq15_rx_de-skew",
	"cs0_dq15_tx_de-skew",
	"cs0_dqs1_rx_de-skew",
	"cs0_dqs1p_tx_de-skew",
	"cs0_dqs1n_tx_de-skew",

	"cs0_dm2_rx_de-skew",
	"cs0_dm2_tx_de-skew",
	"cs0_dq16_rx_de-skew",
	"cs0_dq16_tx_de-skew",
	"cs0_dq17_rx_de-skew",
	"cs0_dq17_tx_de-skew",
	"cs0_dq18_rx_de-skew",
	"cs0_dq18_tx_de-skew",
	"cs0_dq19_rx_de-skew",
	"cs0_dq19_tx_de-skew",
	"cs0_dq20_rx_de-skew",
	"cs0_dq20_tx_de-skew",
	"cs0_dq21_rx_de-skew",
	"cs0_dq21_tx_de-skew",
	"cs0_dq22_rx_de-skew",
	"cs0_dq22_tx_de-skew",
	"cs0_dq23_rx_de-skew",
	"cs0_dq23_tx_de-skew",
	"cs0_dqs2_rx_de-skew",
	"cs0_dqs2p_tx_de-skew",
	"cs0_dqs2n_tx_de-skew",

	"cs0_dm3_rx_de-skew",
	"cs0_dm3_tx_de-skew",
	"cs0_dq24_rx_de-skew",
	"cs0_dq24_tx_de-skew",
	"cs0_dq25_rx_de-skew",
	"cs0_dq25_tx_de-skew",
	"cs0_dq26_rx_de-skew",
	"cs0_dq26_tx_de-skew",
	"cs0_dq27_rx_de-skew",
	"cs0_dq27_tx_de-skew",
	"cs0_dq28_rx_de-skew",
	"cs0_dq28_tx_de-skew",
	"cs0_dq29_rx_de-skew",
	"cs0_dq29_tx_de-skew",
	"cs0_dq30_rx_de-skew",
	"cs0_dq30_tx_de-skew",
	"cs0_dq31_rx_de-skew",
	"cs0_dq31_tx_de-skew",
	"cs0_dqs3_rx_de-skew",
	"cs0_dqs3p_tx_de-skew",
	"cs0_dqs3n_tx_de-skew",
};

static const char * const rk3328_dts_cs1_timing[] = {
	"cs1_dm0_rx_de-skew",
	"cs1_dm0_tx_de-skew",
	"cs1_dq0_rx_de-skew",
	"cs1_dq0_tx_de-skew",
	"cs1_dq1_rx_de-skew",
	"cs1_dq1_tx_de-skew",
	"cs1_dq2_rx_de-skew",
	"cs1_dq2_tx_de-skew",
	"cs1_dq3_rx_de-skew",
	"cs1_dq3_tx_de-skew",
	"cs1_dq4_rx_de-skew",
	"cs1_dq4_tx_de-skew",
	"cs1_dq5_rx_de-skew",
	"cs1_dq5_tx_de-skew",
	"cs1_dq6_rx_de-skew",
	"cs1_dq6_tx_de-skew",
	"cs1_dq7_rx_de-skew",
	"cs1_dq7_tx_de-skew",
	"cs1_dqs0_rx_de-skew",
	"cs1_dqs0p_tx_de-skew",
	"cs1_dqs0n_tx_de-skew",

	"cs1_dm1_rx_de-skew",
	"cs1_dm1_tx_de-skew",
	"cs1_dq8_rx_de-skew",
	"cs1_dq8_tx_de-skew",
	"cs1_dq9_rx_de-skew",
	"cs1_dq9_tx_de-skew",
	"cs1_dq10_rx_de-skew",
	"cs1_dq10_tx_de-skew",
	"cs1_dq11_rx_de-skew",
	"cs1_dq11_tx_de-skew",
	"cs1_dq12_rx_de-skew",
	"cs1_dq12_tx_de-skew",
	"cs1_dq13_rx_de-skew",
	"cs1_dq13_tx_de-skew",
	"cs1_dq14_rx_de-skew",
	"cs1_dq14_tx_de-skew",
	"cs1_dq15_rx_de-skew",
	"cs1_dq15_tx_de-skew",
	"cs1_dqs1_rx_de-skew",
	"cs1_dqs1p_tx_de-skew",
	"cs1_dqs1n_tx_de-skew",

	"cs1_dm2_rx_de-skew",
	"cs1_dm2_tx_de-skew",
	"cs1_dq16_rx_de-skew",
	"cs1_dq16_tx_de-skew",
	"cs1_dq17_rx_de-skew",
	"cs1_dq17_tx_de-skew",
	"cs1_dq18_rx_de-skew",
	"cs1_dq18_tx_de-skew",
	"cs1_dq19_rx_de-skew",
	"cs1_dq19_tx_de-skew",
	"cs1_dq20_rx_de-skew",
	"cs1_dq20_tx_de-skew",
	"cs1_dq21_rx_de-skew",
	"cs1_dq21_tx_de-skew",
	"cs1_dq22_rx_de-skew",
	"cs1_dq22_tx_de-skew",
	"cs1_dq23_rx_de-skew",
	"cs1_dq23_tx_de-skew",
	"cs1_dqs2_rx_de-skew",
	"cs1_dqs2p_tx_de-skew",
	"cs1_dqs2n_tx_de-skew",

	"cs1_dm3_rx_de-skew",
	"cs1_dm3_tx_de-skew",
	"cs1_dq24_rx_de-skew",
	"cs1_dq24_tx_de-skew",
	"cs1_dq25_rx_de-skew",
	"cs1_dq25_tx_de-skew",
	"cs1_dq26_rx_de-skew",
	"cs1_dq26_tx_de-skew",
	"cs1_dq27_rx_de-skew",
	"cs1_dq27_tx_de-skew",
	"cs1_dq28_rx_de-skew",
	"cs1_dq28_tx_de-skew",
	"cs1_dq29_rx_de-skew",
	"cs1_dq29_tx_de-skew",
	"cs1_dq30_rx_de-skew",
	"cs1_dq30_tx_de-skew",
	"cs1_dq31_rx_de-skew",
	"cs1_dq31_tx_de-skew",
	"cs1_dqs3_rx_de-skew",
	"cs1_dqs3p_tx_de-skew",
	"cs1_dqs3n_tx_de-skew",
};

struct rk3328_ddr_dts_config_timing {
	unsigned int ddr3_speed_bin;
	unsigned int ddr4_speed_bin;
	unsigned int pd_idle;
	unsigned int sr_idle;
	unsigned int sr_mc_gate_idle;
	unsigned int srpd_lite_idle;
	unsigned int standby_idle;

	unsigned int auto_pd_dis_freq;
	unsigned int auto_sr_dis_freq;
	/* for ddr3 only */
	unsigned int ddr3_dll_dis_freq;
	/* for ddr4 only */
	unsigned int ddr4_dll_dis_freq;
	unsigned int phy_dll_dis_freq;

	unsigned int ddr3_odt_dis_freq;
	unsigned int phy_ddr3_odt_dis_freq;
	unsigned int ddr3_drv;
	unsigned int ddr3_odt;
	unsigned int phy_ddr3_ca_drv;
	unsigned int phy_ddr3_ck_drv;
	unsigned int phy_ddr3_dq_drv;
	unsigned int phy_ddr3_odt;

	unsigned int lpddr3_odt_dis_freq;
	unsigned int phy_lpddr3_odt_dis_freq;
	unsigned int lpddr3_drv;
	unsigned int lpddr3_odt;
	unsigned int phy_lpddr3_ca_drv;
	unsigned int phy_lpddr3_ck_drv;
	unsigned int phy_lpddr3_dq_drv;
	unsigned int phy_lpddr3_odt;

	unsigned int lpddr4_odt_dis_freq;
	unsigned int phy_lpddr4_odt_dis_freq;
	unsigned int lpddr4_drv;
	unsigned int lpddr4_dq_odt;
	unsigned int lpddr4_ca_odt;
	unsigned int phy_lpddr4_ca_drv;
	unsigned int phy_lpddr4_ck_cs_drv;
	unsigned int phy_lpddr4_dq_drv;
	unsigned int phy_lpddr4_odt;

	unsigned int ddr4_odt_dis_freq;
	unsigned int phy_ddr4_odt_dis_freq;
	unsigned int ddr4_drv;
	unsigned int ddr4_odt;
	unsigned int phy_ddr4_ca_drv;
	unsigned int phy_ddr4_ck_drv;
	unsigned int phy_ddr4_dq_drv;
	unsigned int phy_ddr4_odt;

	unsigned int ca_skew[15];
	unsigned int cs0_skew[44];
	unsigned int cs1_skew[44];

	unsigned int available;
};

struct rk3328_ddr_de_skew_setting {
	unsigned int ca_de_skew[30];
	unsigned int cs0_de_skew[84];
	unsigned int cs1_de_skew[84];
};

struct rk3368_dram_timing {
	u32 dram_spd_bin;
	u32 sr_idle;
	u32 pd_idle;
	u32 dram_dll_dis_freq;
	u32 phy_dll_dis_freq;
	u32 dram_odt_dis_freq;
	u32 phy_odt_dis_freq;
	u32 ddr3_drv;
	u32 ddr3_odt;
	u32 lpddr3_drv;
	u32 lpddr3_odt;
	u32 lpddr2_drv;
	u32 phy_clk_drv;
	u32 phy_cmd_drv;
	u32 phy_dqs_drv;
	u32 phy_odt;
};

struct rk3399_dram_timing {
	unsigned int ddr3_speed_bin;
	unsigned int pd_idle;
	unsigned int sr_idle;
	unsigned int sr_mc_gate_idle;
	unsigned int srpd_lite_idle;
	unsigned int standby_idle;
	unsigned int auto_lp_dis_freq;
	unsigned int ddr3_dll_dis_freq;
	unsigned int phy_dll_dis_freq;
	unsigned int ddr3_odt_dis_freq;
	unsigned int ddr3_drv;
	unsigned int ddr3_odt;
	unsigned int phy_ddr3_ca_drv;
	unsigned int phy_ddr3_dq_drv;
	unsigned int phy_ddr3_odt;
	unsigned int lpddr3_odt_dis_freq;
	unsigned int lpddr3_drv;
	unsigned int lpddr3_odt;
	unsigned int phy_lpddr3_ca_drv;
	unsigned int phy_lpddr3_dq_drv;
	unsigned int phy_lpddr3_odt;
	unsigned int lpddr4_odt_dis_freq;
	unsigned int lpddr4_drv;
	unsigned int lpddr4_dq_odt;
	unsigned int lpddr4_ca_odt;
	unsigned int phy_lpddr4_ca_drv;
	unsigned int phy_lpddr4_ck_cs_drv;
	unsigned int phy_lpddr4_dq_drv;
	unsigned int phy_lpddr4_odt;
};

struct rockchip_dmcfreq {
	struct device *dev;
	struct devfreq *devfreq;
	struct devfreq_simple_ondemand_data ondemand_data;
	struct clk *dmc_clk;
	struct devfreq_event_dev **edev;
	struct mutex lock; /* serializes access to video_info_list */
	struct dram_timing *timing;
	struct regulator *vdd_center;
	struct notifier_block status_nb;
	struct notifier_block reboot_nb;
	struct notifier_block fb_nb;
	struct list_head video_info_list;
	struct freq_map_table *vop_bw_tbl;
	struct work_struct boost_work;
	struct input_handler input_handler;
	struct thermal_opp_info *opp_info;
	struct rl_map_table *vop_pn_rl_tbl;
	struct delayed_work msch_rl_work;

	unsigned long *nocp_bw;
	unsigned long rate, target_rate;
	unsigned long volt, target_volt;

	unsigned long min;
	unsigned long max;
	unsigned long auto_min_rate;
	unsigned long status_rate;
	unsigned long normal_rate;
	unsigned long video_1080p_rate;
	unsigned long video_4k_rate;
	unsigned long video_4k_10b_rate;
	unsigned long performance_rate;
	unsigned long dualview_rate;
	unsigned long hdmi_rate;
	unsigned long idle_rate;
	unsigned long suspend_rate;
	unsigned long reboot_rate;
	unsigned long boost_rate;
	unsigned long isp_rate;
	unsigned long low_power_rate;
	unsigned long vop_req_rate;

	unsigned int min_cpu_freq;
	unsigned int auto_freq_en;
	unsigned int system_status_en;
	unsigned int refresh;
	unsigned int last_refresh;
	unsigned int read_latency;
	int edev_count;
	int dfi_id;

	bool is_fixed;
	bool is_msch_rl_work_started;

	struct thermal_cooling_device *devfreq_cooling;
	u32 static_coefficient;
	s32 ts[4];
	struct thermal_zone_device *ddr_tz;

	unsigned int touchboostpulse_duration_val;
	u64 touchboostpulse_endtime;

	int (*set_auto_self_refresh)(u32 en);
	int (*set_msch_readlatency)(unsigned int rl);
};

static struct thermal_opp_device_data dmc_devdata = {
	.type = THERMAL_OPP_TPYE_DEV,
	.low_temp_adjust = rockchip_dev_low_temp_adjust,
	.high_temp_adjust = rockchip_dev_high_temp_adjust,
};

static struct pm_qos_request pm_qos;

static DECLARE_RWSEM(rockchip_dmcfreq_sem);

void rockchip_dmcfreq_lock(void)
{
	down_read(&rockchip_dmcfreq_sem);
}
EXPORT_SYMBOL(rockchip_dmcfreq_lock);

void rockchip_dmcfreq_unlock(void)
{
	up_read(&rockchip_dmcfreq_sem);
}
EXPORT_SYMBOL(rockchip_dmcfreq_unlock);

/*
 * function: packaging de-skew setting to px30_ddr_dts_config_timing,
 *           px30_ddr_dts_config_timing will pass to trust firmware, and
 *           used direct to set register.
 * input: de_skew
 * output: tim
 */
static void px30_de_skew_set_2_reg(struct rk3328_ddr_de_skew_setting *de_skew,
				   struct px30_ddr_dts_config_timing *tim)
{
	u32 n;
	u32 offset;
	u32 shift;

	memset_io(tim->ca_skew, 0, sizeof(tim->ca_skew));
	memset_io(tim->cs0_skew, 0, sizeof(tim->cs0_skew));
	memset_io(tim->cs1_skew, 0, sizeof(tim->cs1_skew));

	/* CA de-skew */
	for (n = 0; n < ARRAY_SIZE(de_skew->ca_de_skew); n++) {
		offset = n / 2;
		shift = n % 2;
		/* 0 => 4; 1 => 0 */
		shift = (shift == 0) ? 4 : 0;
		tim->ca_skew[offset] &= ~(0xf << shift);
		tim->ca_skew[offset] |= (de_skew->ca_de_skew[n] << shift);
	}

	/* CS0 data de-skew */
	for (n = 0; n < ARRAY_SIZE(de_skew->cs0_de_skew); n++) {
		offset = ((n / 21) * 11) + ((n % 21) / 2);
		shift = ((n % 21) % 2);
		if ((n % 21) == 20)
			shift = 0;
		else
			/* 0 => 4; 1 => 0 */
			shift = (shift == 0) ? 4 : 0;
		tim->cs0_skew[offset] &= ~(0xf << shift);
		tim->cs0_skew[offset] |= (de_skew->cs0_de_skew[n] << shift);
	}

	/* CS1 data de-skew */
	for (n = 0; n < ARRAY_SIZE(de_skew->cs1_de_skew); n++) {
		offset = ((n / 21) * 11) + ((n % 21) / 2);
		shift = ((n % 21) % 2);
		if ((n % 21) == 20)
			shift = 0;
		else
			/* 0 => 4; 1 => 0 */
			shift = (shift == 0) ? 4 : 0;
		tim->cs1_skew[offset] &= ~(0xf << shift);
		tim->cs1_skew[offset] |= (de_skew->cs1_de_skew[n] << shift);
	}
}

/*
 * function: packaging de-skew setting to rk3328_ddr_dts_config_timing,
 *           rk3328_ddr_dts_config_timing will pass to trust firmware, and
 *           used direct to set register.
 * input: de_skew
 * output: tim
 */
static void
rk3328_de_skew_setting_2_register(struct rk3328_ddr_de_skew_setting *de_skew,
				  struct rk3328_ddr_dts_config_timing *tim)
{
	u32 n;
	u32 offset;
	u32 shift;

	memset_io(tim->ca_skew, 0, sizeof(tim->ca_skew));
	memset_io(tim->cs0_skew, 0, sizeof(tim->cs0_skew));
	memset_io(tim->cs1_skew, 0, sizeof(tim->cs1_skew));

	/* CA de-skew */
	for (n = 0; n < ARRAY_SIZE(de_skew->ca_de_skew); n++) {
		offset = n / 2;
		shift = n % 2;
		/* 0 => 4; 1 => 0 */
		shift = (shift == 0) ? 4 : 0;
		tim->ca_skew[offset] &= ~(0xf << shift);
		tim->ca_skew[offset] |= (de_skew->ca_de_skew[n] << shift);
	}

	/* CS0 data de-skew */
	for (n = 0; n < ARRAY_SIZE(de_skew->cs0_de_skew); n++) {
		offset = ((n / 21) * 11) + ((n % 21) / 2);
		shift = ((n % 21) % 2);
		if ((n % 21) == 20)
			shift = 0;
		else
			/* 0 => 4; 1 => 0 */
			shift = (shift == 0) ? 4 : 0;
		tim->cs0_skew[offset] &= ~(0xf << shift);
		tim->cs0_skew[offset] |= (de_skew->cs0_de_skew[n] << shift);
	}

	/* CS1 data de-skew */
	for (n = 0; n < ARRAY_SIZE(de_skew->cs1_de_skew); n++) {
		offset = ((n / 21) * 11) + ((n % 21) / 2);
		shift = ((n % 21) % 2);
		if ((n % 21) == 20)
			shift = 0;
		else
			/* 0 => 4; 1 => 0 */
			shift = (shift == 0) ? 4 : 0;
		tim->cs1_skew[offset] &= ~(0xf << shift);
		tim->cs1_skew[offset] |= (de_skew->cs1_de_skew[n] << shift);
	}
}

static int rockchip_dmcfreq_target(struct device *dev, unsigned long *freq,
				   u32 flags)
{
	struct rockchip_dmcfreq *dmcfreq = dev_get_drvdata(dev);
	struct dev_pm_opp *opp;
	struct cpufreq_policy *policy;
	unsigned long old_clk_rate = dmcfreq->rate;
	unsigned long target_volt, target_rate;
	unsigned int cpu_cur, cpufreq_cur;
	bool is_cpufreq_changed = false;
	int err = 0;

	rcu_read_lock();

	opp = devfreq_recommended_opp(dev, freq, flags);
	if (IS_ERR(opp)) {
		rcu_read_unlock();
		return PTR_ERR(opp);
	}
	target_volt = dev_pm_opp_get_voltage(opp);

	rcu_read_unlock();

	target_rate = clk_round_rate(dmcfreq->dmc_clk, *freq);
	if ((long)target_rate <= 0)
		target_rate = *freq;

	if (dmcfreq->rate == target_rate) {
		if (dmcfreq->volt == target_volt)
			return 0;
		err = regulator_set_voltage(dmcfreq->vdd_center, target_volt,
					    INT_MAX);
		if (err) {
			dev_err(dev, "Cannot set voltage %lu uV\n",
				target_volt);
			return err;
		}
		dmcfreq->volt = target_volt;
		return 0;
	} else if (!dmcfreq->volt) {
		dmcfreq->volt = regulator_get_voltage(dmcfreq->vdd_center);
	}

	/*
	 * We need to prevent cpu hotplug from happening while a dmc freq rate
	 * change is happening.
	 *
	 * Do this before taking the policy rwsem to avoid deadlocks between the
	 * mutex that is locked/unlocked in cpu_hotplug_disable/enable. And it
	 * can also avoid deadlocks between the mutex that is locked/unlocked
	 * in get/put_online_cpus (such as store_scaling_max_freq()).
	 */
	get_online_cpus();

	/*
	 * Go to specified cpufreq and block other cpufreq changes since
	 * set_rate needs to complete during vblank.
	 */
	cpu_cur = smp_processor_id();
	policy = cpufreq_cpu_get(cpu_cur);
	if (!policy) {
		dev_err(dev, "cpu%d policy NULL\n", cpu_cur);
		goto cpufreq;
	}
	down_write(&policy->rwsem);
	cpufreq_cur = cpufreq_quick_get(cpu_cur);

	/* If we're thermally throttled; don't change; */
	if (dmcfreq->min_cpu_freq && cpufreq_cur < dmcfreq->min_cpu_freq) {
		if (policy->max >= dmcfreq->min_cpu_freq) {
			__cpufreq_driver_target(policy, dmcfreq->min_cpu_freq,
						CPUFREQ_RELATION_L);
			is_cpufreq_changed = true;
		} else {
			dev_dbg(dev, "CPU may too slow for DMC (%d MHz)\n",
				policy->max);
		}
	}

	/*
	 * If frequency scaling from low to high, adjust voltage first.
	 * If frequency scaling from high to low, adjust frequency first.
	 */
	if (old_clk_rate < target_rate) {
		err = regulator_set_voltage(dmcfreq->vdd_center, target_volt,
					    INT_MAX);
		if (err) {
			dev_err(dev, "Cannot set voltage %lu uV\n",
				target_volt);
			goto out;
		}
	}

	/*
	 * Writer in rwsem may block readers even during its waiting in queue,
	 * and this may lead to a deadlock when the code path takes read sem
	 * twice (e.g. one in vop_lock() and another in rockchip_pmu_lock()).
	 * As a (suboptimal) workaround, let writer to spin until it gets the
	 * lock.
	 */
	while (!down_write_trylock(&rockchip_dmcfreq_sem))
		cond_resched();
	dev_dbg(dev, "%lu-->%lu\n", old_clk_rate, target_rate);
	err = clk_set_rate(dmcfreq->dmc_clk, target_rate);
	up_write(&rockchip_dmcfreq_sem);
	if (err) {
		dev_err(dev, "Cannot set frequency %lu (%d)\n",
			target_rate, err);
		regulator_set_voltage(dmcfreq->vdd_center, dmcfreq->volt,
				      INT_MAX);
		goto out;
	}

	/*
	 * Check the dpll rate,
	 * There only two result we will get,
	 * 1. Ddr frequency scaling fail, we still get the old rate.
	 * 2. Ddr frequency scaling sucessful, we get the rate we set.
	 */
	dmcfreq->rate = clk_get_rate(dmcfreq->dmc_clk);

	/* If get the incorrect rate, set voltage to old value. */
	if (dmcfreq->rate != target_rate) {
		dev_err(dev, "Get wrong frequency, Request %lu, Current %lu\n",
			target_rate, dmcfreq->rate);
		regulator_set_voltage(dmcfreq->vdd_center, dmcfreq->volt,
				      INT_MAX);
		goto out;
	} else if (old_clk_rate > target_rate) {
		err = regulator_set_voltage(dmcfreq->vdd_center, target_volt,
					    INT_MAX);
		if (err) {
			dev_err(dev, "Cannot set vol %lu uV\n", target_volt);
			goto out;
		}
	}

	if (dmcfreq->devfreq)
		dmcfreq->devfreq->last_status.current_frequency = *freq;

	dmcfreq->volt = target_volt;
out:
	if (is_cpufreq_changed)
		__cpufreq_driver_target(policy, cpufreq_cur,
					CPUFREQ_RELATION_L);
	up_write(&policy->rwsem);
	cpufreq_cpu_put(policy);
cpufreq:
	put_online_cpus();
	return err;
}

static int rockchip_dmcfreq_get_dev_status(struct device *dev,
					   struct devfreq_dev_status *stat)
{
	struct rockchip_dmcfreq *dmcfreq = dev_get_drvdata(dev);
	struct devfreq_event_data edata;
	int i, j, ret = 0;

	if (dmcfreq->dfi_id >= 0) {
		ret = devfreq_event_get_event(dmcfreq->edev[dmcfreq->dfi_id],
					      &edata);
		if (ret < 0) {
			dev_err(dev, "failed to get dfi event\n");
			return ret;
		}
		stat->busy_time = edata.load_count;
		stat->total_time = edata.total_count;
	}

	for (i = 0, j = 0; i < dmcfreq->edev_count; i++) {
		if (i == dmcfreq->dfi_id)
			continue;
		ret = devfreq_event_get_event(dmcfreq->edev[i], &edata);
		if (ret < 0) {
			dev_err(dev, "failed to get event %s\n",
				dmcfreq->edev[i]->desc->name);
			return ret;
		}
		dmcfreq->nocp_bw[j++] = edata.load_count;
	}

	return 0;
}

static int rockchip_dmcfreq_get_cur_freq(struct device *dev,
					 unsigned long *freq)
{
	struct rockchip_dmcfreq *dmcfreq = dev_get_drvdata(dev);

	*freq = dmcfreq->rate;

	return 0;
}

static struct devfreq_dev_profile rockchip_devfreq_dmc_profile = {
	.polling_ms	= 50,
	.target		= rockchip_dmcfreq_target,
	.get_dev_status	= rockchip_dmcfreq_get_dev_status,
	.get_cur_freq	= rockchip_dmcfreq_get_cur_freq,
};

static int rockchip_dmcfreq_init_freq_table(struct device *dev,
					    struct devfreq_dev_profile *devp)
{
	int count;
	int i = 0;
	unsigned long freq = 0;
	struct dev_pm_opp *opp;

	rcu_read_lock();
	count = dev_pm_opp_get_opp_count(dev);
	if (count < 0) {
		rcu_read_unlock();
		return count;
	}
	rcu_read_unlock();

	devp->freq_table = kmalloc_array(count, sizeof(devp->freq_table[0]),
				GFP_KERNEL);
	if (!devp->freq_table)
		return -ENOMEM;

	rcu_read_lock();
	for (i = 0; i < count; i++, freq++) {
		opp = dev_pm_opp_find_freq_ceil(dev, &freq);
		if (IS_ERR(opp))
			break;

		devp->freq_table[i] = freq;
	}
	rcu_read_unlock();

	if (count != i)
		dev_warn(dev, "Unable to enumerate all OPPs (%d!=%d)\n",
			 count, i);

	devp->max_state = i;
	return 0;
}

static inline void reset_last_status(struct devfreq *devfreq)
{
	devfreq->last_status.total_time = 1;
	devfreq->last_status.busy_time = 1;
}

static void of_get_px30_timings(struct device *dev,
				struct device_node *np, uint32_t *timing)
{
	struct device_node *np_tim;
	u32 *p;
	struct px30_ddr_dts_config_timing *dts_timing;
	struct rk3328_ddr_de_skew_setting *de_skew;
	int ret = 0;
	u32 i;

	dts_timing =
		(struct px30_ddr_dts_config_timing *)(timing +
							DTS_PAR_OFFSET / 4);

	np_tim = of_parse_phandle(np, "ddr_timing", 0);
	if (!np_tim) {
		ret = -EINVAL;
		goto end;
	}
	de_skew = kmalloc(sizeof(*de_skew), GFP_KERNEL);
	if (!de_skew) {
		ret = -ENOMEM;
		goto end;
	}
	p = (u32 *)dts_timing;
	for (i = 0; i < ARRAY_SIZE(px30_dts_timing); i++) {
		ret |= of_property_read_u32(np_tim, px30_dts_timing[i],
					p + i);
	}
	p = (u32 *)de_skew->ca_de_skew;
	for (i = 0; i < ARRAY_SIZE(rk3328_dts_ca_timing); i++) {
		ret |= of_property_read_u32(np_tim, rk3328_dts_ca_timing[i],
					p + i);
	}
	p = (u32 *)de_skew->cs0_de_skew;
	for (i = 0; i < ARRAY_SIZE(rk3328_dts_cs0_timing); i++) {
		ret |= of_property_read_u32(np_tim, rk3328_dts_cs0_timing[i],
					p + i);
	}
	p = (u32 *)de_skew->cs1_de_skew;
	for (i = 0; i < ARRAY_SIZE(rk3328_dts_cs1_timing); i++) {
		ret |= of_property_read_u32(np_tim, rk3328_dts_cs1_timing[i],
					p + i);
	}
	if (!ret)
		px30_de_skew_set_2_reg(de_skew, dts_timing);
	kfree(de_skew);
end:
	if (!ret) {
		dts_timing->available = 1;
	} else {
		dts_timing->available = 0;
		dev_err(dev, "of_get_ddr_timings: fail\n");
	}

	of_node_put(np_tim);
}

static void of_get_rk1808_timings(struct device *dev,
				  struct device_node *np, uint32_t *timing)
{
	struct device_node *np_tim;
	u32 *p;
	struct rk1808_ddr_dts_config_timing *dts_timing;
	int ret = 0;
	u32 i;

	dts_timing =
		(struct rk1808_ddr_dts_config_timing *)(timing +
							DTS_PAR_OFFSET / 4);

	np_tim = of_parse_phandle(np, "ddr_timing", 0);
	if (!np_tim) {
		ret = -EINVAL;
		goto end;
	}

	p = (u32 *)dts_timing;
	for (i = 0; i < ARRAY_SIZE(px30_dts_timing); i++) {
		ret |= of_property_read_u32(np_tim, px30_dts_timing[i],
					p + i);
	}
	p = (u32 *)dts_timing->ca_de_skew;
	for (i = 0; i < ARRAY_SIZE(rk1808_dts_ca_timing); i++) {
		ret |= of_property_read_u32(np_tim, rk1808_dts_ca_timing[i],
					p + i);
	}
	p = (u32 *)dts_timing->cs0_a_de_skew;
	for (i = 0; i < ARRAY_SIZE(rk1808_dts_cs0_a_timing); i++) {
		ret |= of_property_read_u32(np_tim, rk1808_dts_cs0_a_timing[i],
					p + i);
	}
	p = (u32 *)dts_timing->cs0_b_de_skew;
	for (i = 0; i < ARRAY_SIZE(rk1808_dts_cs0_b_timing); i++) {
		ret |= of_property_read_u32(np_tim, rk1808_dts_cs0_b_timing[i],
					p + i);
	}
	p = (u32 *)dts_timing->cs1_a_de_skew;
	for (i = 0; i < ARRAY_SIZE(rk1808_dts_cs1_a_timing); i++) {
		ret |= of_property_read_u32(np_tim, rk1808_dts_cs1_a_timing[i],
					p + i);
	}
	p = (u32 *)dts_timing->cs1_b_de_skew;
	for (i = 0; i < ARRAY_SIZE(rk1808_dts_cs1_b_timing); i++) {
		ret |= of_property_read_u32(np_tim, rk1808_dts_cs1_b_timing[i],
					p + i);
	}

end:
	if (!ret) {
		dts_timing->available = 1;
	} else {
		dts_timing->available = 0;
		dev_err(dev, "of_get_ddr_timings: fail\n");
	}

	of_node_put(np_tim);
}

static void of_get_rk3128_timings(struct device *dev,
				  struct device_node *np, uint32_t *timing)
{
	struct device_node *np_tim;
	u32 *p;
	struct rk3128_ddr_dts_config_timing *dts_timing;
	struct share_params *init_timing;
	int ret = 0;
	u32 i;

	init_timing = (struct share_params *)timing;

	if (of_property_read_u32(np, "vop-dclk-mode",
				 &init_timing->vop_dclk_mode))
		init_timing->vop_dclk_mode = 0;

	p = timing + DTS_PAR_OFFSET / 4;
	np_tim = of_parse_phandle(np, "rockchip,ddr_timing", 0);
	if (!np_tim) {
		ret = -EINVAL;
		goto end;
	}
	for (i = 0; i < ARRAY_SIZE(rk3128_dts_timing); i++) {
		ret |= of_property_read_u32(np_tim, rk3128_dts_timing[i],
					p + i);
	}
end:
	dts_timing =
		(struct rk3128_ddr_dts_config_timing *)(timing +
							DTS_PAR_OFFSET / 4);
	if (!ret) {
		dts_timing->available = 1;
	} else {
		dts_timing->available = 0;
		dev_err(dev, "of_get_ddr_timings: fail\n");
	}

	of_node_put(np_tim);
}

static uint32_t of_get_rk3228_timings(struct device *dev,
				      struct device_node *np, uint32_t *timing)
{
	struct device_node *np_tim;
	u32 *p;
	int ret = 0;
	u32 i;

	p = timing + DTS_PAR_OFFSET / 4;
	np_tim = of_parse_phandle(np, "rockchip,dram_timing", 0);
	if (!np_tim) {
		ret = -EINVAL;
		goto end;
	}
	for (i = 0; i < ARRAY_SIZE(rk3228_dts_timing); i++) {
		ret |= of_property_read_u32(np_tim, rk3228_dts_timing[i],
					p + i);
	}
end:
	if (ret)
		dev_err(dev, "of_get_ddr_timings: fail\n");

	of_node_put(np_tim);
	return ret;
}

static void of_get_rk3288_timings(struct device *dev,
				  struct device_node *np, uint32_t *timing)
{
	struct device_node *np_tim;
	u32 *p;
	struct rk3288_ddr_dts_config_timing *dts_timing;
	struct share_params *init_timing;
	int ret = 0;
	u32 i;

	init_timing = (struct share_params *)timing;

	if (of_property_read_u32(np, "vop-dclk-mode",
				 &init_timing->vop_dclk_mode))
		init_timing->vop_dclk_mode = 0;

	p = timing + DTS_PAR_OFFSET / 4;
	np_tim = of_parse_phandle(np, "rockchip,ddr_timing", 0);
	if (!np_tim) {
		ret = -EINVAL;
		goto end;
	}
	for (i = 0; i < ARRAY_SIZE(rk3288_dts_timing); i++) {
		ret |= of_property_read_u32(np_tim, rk3288_dts_timing[i],
					p + i);
	}
end:
	dts_timing =
		(struct rk3288_ddr_dts_config_timing *)(timing +
							DTS_PAR_OFFSET / 4);
	if (!ret) {
		dts_timing->available = 1;
	} else {
		dts_timing->available = 0;
		dev_err(dev, "of_get_ddr_timings: fail\n");
	}

	of_node_put(np_tim);
}

static void of_get_rk3328_timings(struct device *dev,
				  struct device_node *np, uint32_t *timing)
{
	struct device_node *np_tim;
	u32 *p;
	struct rk3328_ddr_dts_config_timing *dts_timing;
	struct rk3328_ddr_de_skew_setting *de_skew;
	int ret = 0;
	u32 i;

	dts_timing =
		(struct rk3328_ddr_dts_config_timing *)(timing +
							DTS_PAR_OFFSET / 4);

	np_tim = of_parse_phandle(np, "ddr_timing", 0);
	if (!np_tim) {
		ret = -EINVAL;
		goto end;
	}
	de_skew = kmalloc(sizeof(*de_skew), GFP_KERNEL);
	if (!de_skew) {
		ret = -ENOMEM;
		goto end;
	}
	p = (u32 *)dts_timing;
	for (i = 0; i < ARRAY_SIZE(rk3328_dts_timing); i++) {
		ret |= of_property_read_u32(np_tim, rk3328_dts_timing[i],
					p + i);
	}
	p = (u32 *)de_skew->ca_de_skew;
	for (i = 0; i < ARRAY_SIZE(rk3328_dts_ca_timing); i++) {
		ret |= of_property_read_u32(np_tim, rk3328_dts_ca_timing[i],
					p + i);
	}
	p = (u32 *)de_skew->cs0_de_skew;
	for (i = 0; i < ARRAY_SIZE(rk3328_dts_cs0_timing); i++) {
		ret |= of_property_read_u32(np_tim, rk3328_dts_cs0_timing[i],
					p + i);
	}
	p = (u32 *)de_skew->cs1_de_skew;
	for (i = 0; i < ARRAY_SIZE(rk3328_dts_cs1_timing); i++) {
		ret |= of_property_read_u32(np_tim, rk3328_dts_cs1_timing[i],
					p + i);
	}
	if (!ret)
		rk3328_de_skew_setting_2_register(de_skew, dts_timing);
	kfree(de_skew);
end:
	if (!ret) {
		dts_timing->available = 1;
	} else {
		dts_timing->available = 0;
		dev_err(dev, "of_get_ddr_timings: fail\n");
	}

	of_node_put(np_tim);
}

static struct rk3368_dram_timing *of_get_rk3368_timings(struct device *dev,
							struct device_node *np)
{
	struct rk3368_dram_timing *timing = NULL;
	struct device_node *np_tim;
	int ret = 0;

	np_tim = of_parse_phandle(np, "ddr_timing", 0);
	if (np_tim) {
		timing = devm_kzalloc(dev, sizeof(*timing), GFP_KERNEL);
		if (!timing)
			goto err;

		ret |= of_property_read_u32(np_tim, "dram_spd_bin",
					    &timing->dram_spd_bin);
		ret |= of_property_read_u32(np_tim, "sr_idle",
					    &timing->sr_idle);
		ret |= of_property_read_u32(np_tim, "pd_idle",
					    &timing->pd_idle);
		ret |= of_property_read_u32(np_tim, "dram_dll_disb_freq",
					    &timing->dram_dll_dis_freq);
		ret |= of_property_read_u32(np_tim, "phy_dll_disb_freq",
					    &timing->phy_dll_dis_freq);
		ret |= of_property_read_u32(np_tim, "dram_odt_disb_freq",
					    &timing->dram_odt_dis_freq);
		ret |= of_property_read_u32(np_tim, "phy_odt_disb_freq",
					    &timing->phy_odt_dis_freq);
		ret |= of_property_read_u32(np_tim, "ddr3_drv",
					    &timing->ddr3_drv);
		ret |= of_property_read_u32(np_tim, "ddr3_odt",
					    &timing->ddr3_odt);
		ret |= of_property_read_u32(np_tim, "lpddr3_drv",
					    &timing->lpddr3_drv);
		ret |= of_property_read_u32(np_tim, "lpddr3_odt",
					    &timing->lpddr3_odt);
		ret |= of_property_read_u32(np_tim, "lpddr2_drv",
					    &timing->lpddr2_drv);
		ret |= of_property_read_u32(np_tim, "phy_clk_drv",
					    &timing->phy_clk_drv);
		ret |= of_property_read_u32(np_tim, "phy_cmd_drv",
					    &timing->phy_cmd_drv);
		ret |= of_property_read_u32(np_tim, "phy_dqs_drv",
					    &timing->phy_dqs_drv);
		ret |= of_property_read_u32(np_tim, "phy_odt",
					    &timing->phy_odt);
		if (ret) {
			devm_kfree(dev, timing);
			goto err;
		}
		of_node_put(np_tim);
		return timing;
	}

err:
	if (timing) {
		devm_kfree(dev, timing);
		timing = NULL;
	}
	of_node_put(np_tim);
	return timing;
}

static struct rk3399_dram_timing *of_get_rk3399_timings(struct device *dev,
							struct device_node *np)
{
	struct rk3399_dram_timing *timing = NULL;
	struct device_node *np_tim;
	int ret;

	np_tim = of_parse_phandle(np, "ddr_timing", 0);
	if (np_tim) {
		timing = devm_kzalloc(dev, sizeof(*timing), GFP_KERNEL);
		if (!timing)
			goto err;

		ret = of_property_read_u32(np_tim, "ddr3_speed_bin",
					   &timing->ddr3_speed_bin);
		ret |= of_property_read_u32(np_tim, "pd_idle",
					    &timing->pd_idle);
		ret |= of_property_read_u32(np_tim, "sr_idle",
					    &timing->sr_idle);
		ret |= of_property_read_u32(np_tim, "sr_mc_gate_idle",
					    &timing->sr_mc_gate_idle);
		ret |= of_property_read_u32(np_tim, "srpd_lite_idle",
					    &timing->srpd_lite_idle);
		ret |= of_property_read_u32(np_tim, "standby_idle",
					    &timing->standby_idle);
		ret |= of_property_read_u32(np_tim, "auto_lp_dis_freq",
					    &timing->auto_lp_dis_freq);
		ret |= of_property_read_u32(np_tim, "ddr3_dll_dis_freq",
					    &timing->ddr3_dll_dis_freq);
		ret |= of_property_read_u32(np_tim, "phy_dll_dis_freq",
					    &timing->phy_dll_dis_freq);
		ret |= of_property_read_u32(np_tim, "ddr3_odt_dis_freq",
					    &timing->ddr3_odt_dis_freq);
		ret |= of_property_read_u32(np_tim, "ddr3_drv",
					    &timing->ddr3_drv);
		ret |= of_property_read_u32(np_tim, "ddr3_odt",
					    &timing->ddr3_odt);
		ret |= of_property_read_u32(np_tim, "phy_ddr3_ca_drv",
					    &timing->phy_ddr3_ca_drv);
		ret |= of_property_read_u32(np_tim, "phy_ddr3_dq_drv",
					    &timing->phy_ddr3_dq_drv);
		ret |= of_property_read_u32(np_tim, "phy_ddr3_odt",
					    &timing->phy_ddr3_odt);
		ret |= of_property_read_u32(np_tim, "lpddr3_odt_dis_freq",
					    &timing->lpddr3_odt_dis_freq);
		ret |= of_property_read_u32(np_tim, "lpddr3_drv",
					    &timing->lpddr3_drv);
		ret |= of_property_read_u32(np_tim, "lpddr3_odt",
					    &timing->lpddr3_odt);
		ret |= of_property_read_u32(np_tim, "phy_lpddr3_ca_drv",
					    &timing->phy_lpddr3_ca_drv);
		ret |= of_property_read_u32(np_tim, "phy_lpddr3_dq_drv",
					    &timing->phy_lpddr3_dq_drv);
		ret |= of_property_read_u32(np_tim, "phy_lpddr3_odt",
					    &timing->phy_lpddr3_odt);
		ret |= of_property_read_u32(np_tim, "lpddr4_odt_dis_freq",
					    &timing->lpddr4_odt_dis_freq);
		ret |= of_property_read_u32(np_tim, "lpddr4_drv",
					    &timing->lpddr4_drv);
		ret |= of_property_read_u32(np_tim, "lpddr4_dq_odt",
					    &timing->lpddr4_dq_odt);
		ret |= of_property_read_u32(np_tim, "lpddr4_ca_odt",
					    &timing->lpddr4_ca_odt);
		ret |= of_property_read_u32(np_tim, "phy_lpddr4_ca_drv",
					    &timing->phy_lpddr4_ca_drv);
		ret |= of_property_read_u32(np_tim, "phy_lpddr4_ck_cs_drv",
					    &timing->phy_lpddr4_ck_cs_drv);
		ret |= of_property_read_u32(np_tim, "phy_lpddr4_dq_drv",
					    &timing->phy_lpddr4_dq_drv);
		ret |= of_property_read_u32(np_tim, "phy_lpddr4_odt",
					    &timing->phy_lpddr4_odt);
		if (ret) {
			devm_kfree(dev, timing);
			goto err;
		}
		of_node_put(np_tim);
		return timing;
	}

err:
	if (timing) {
		devm_kfree(dev, timing);
		timing = NULL;
	}
	of_node_put(np_tim);
	return timing;
}

static int rk_drm_get_lcdc_type(void)
{
	struct drm_device *drm;
	u32 lcdc_type = 0;

	drm = drm_device_get_by_name("rockchip");
	if (drm) {
		struct drm_connector *conn;

		list_for_each_entry(conn, &drm->mode_config.connector_list,
				    head) {
			if (conn->encoder) {
				lcdc_type = conn->connector_type;
				break;
			}
		}
	}
	switch (lcdc_type) {
	case DRM_MODE_CONNECTOR_DPI:
	case DRM_MODE_CONNECTOR_LVDS:
		lcdc_type = SCREEN_LVDS;
		break;
	case DRM_MODE_CONNECTOR_DisplayPort:
		lcdc_type = SCREEN_DP;
		break;
	case DRM_MODE_CONNECTOR_HDMIA:
	case DRM_MODE_CONNECTOR_HDMIB:
		lcdc_type = SCREEN_HDMI;
		break;
	case DRM_MODE_CONNECTOR_TV:
		lcdc_type = SCREEN_TVOUT;
		break;
	case DRM_MODE_CONNECTOR_eDP:
		lcdc_type = SCREEN_EDP;
		break;
	case DRM_MODE_CONNECTOR_DSI:
		lcdc_type = SCREEN_MIPI;
		break;
	default:
		lcdc_type = SCREEN_NULL;
		break;
	}

	return lcdc_type;
}

static int rockchip_ddr_set_auto_self_refresh(uint32_t en)
{
	struct arm_smccc_res res;

	ddr_psci_param->sr_idle_en = en;
	res = sip_smc_dram(SHARE_PAGE_TYPE_DDR, 0,
			   ROCKCHIP_SIP_CONFIG_DRAM_SET_AT_SR);

	return res.a0;
}

struct dmcfreq_wait_ctrl_t {
	wait_queue_head_t wait_wq;
	int wait_flag;
	int wait_en;
	int wait_time_out_ms;
	int dcf_en;
	struct regmap *regmap_dcf;
};

static struct dmcfreq_wait_ctrl_t wait_ctrl;

static irqreturn_t wait_complete_irq(int irqno, void *dev_id)
{
	struct dmcfreq_wait_ctrl_t *ctrl = dev_id;

	ctrl->wait_flag = 0;
	wake_up(&ctrl->wait_wq);
	return IRQ_HANDLED;
}

static irqreturn_t wait_dcf_complete_irq(int irqno, void *dev_id)
{
	struct arm_smccc_res res;
	struct dmcfreq_wait_ctrl_t *ctrl = dev_id;

	res = sip_smc_dram(SHARE_PAGE_TYPE_DDR, 0,
			   ROCKCHIP_SIP_CONFIG_DRAM_POST_SET_RATE);
	if (res.a0)
		pr_err("%s: dram post set rate error:%lx\n", __func__, res.a0);

	ctrl->wait_flag = 0;
	wake_up(&ctrl->wait_wq);
	return IRQ_HANDLED;
}

int rockchip_dmcfreq_wait_complete(void)
{
	if (!wait_ctrl.wait_en) {
		pr_err("%s: Do not support time out!\n", __func__);
		return 0;
	}
	wait_ctrl.wait_flag = -1;

	/*
	 * CPUs only enter WFI when idle to make sure that
	 * FIQn can quick response.
	 */
	pm_qos_update_request(&pm_qos, 0);

	if (wait_ctrl.dcf_en == 1) {
		/* start dcf */
		regmap_update_bits(wait_ctrl.regmap_dcf, 0x0, 0x1, 0x1);
	}

	wait_event_timeout(wait_ctrl.wait_wq, (wait_ctrl.wait_flag == 0),
			   msecs_to_jiffies(wait_ctrl.wait_time_out_ms));

	pm_qos_update_request(&pm_qos, PM_QOS_DEFAULT_VALUE);

	return 0;
}

static __maybe_unused int px30_dmc_init(struct platform_device *pdev,
					struct rockchip_dmcfreq *dmcfreq)
{
	struct arm_smccc_res res;
	u32 size;
	int ret;
	int complt_irq;
	u32 complt_hwirq;
	struct irq_data *complt_irq_data;

	res = sip_smc_dram(0, 0,
			   ROCKCHIP_SIP_CONFIG_DRAM_GET_VERSION);
	dev_notice(&pdev->dev, "current ATF version 0x%lx!\n", res.a1);
	if (res.a0 || res.a1 < 0x103) {
		dev_err(&pdev->dev,
			"trusted firmware need to update or is invalid!\n");
		return -ENXIO;
	}

	dev_notice(&pdev->dev, "read tf version 0x%lx!\n", res.a1);

	/*
	 * first 4KB is used for interface parameters
	 * after 4KB * N is dts parameters
	 */
	size = sizeof(struct px30_ddr_dts_config_timing);
	res = sip_smc_request_share_mem(DIV_ROUND_UP(size, 4096) + 1,
					SHARE_PAGE_TYPE_DDR);
	if (res.a0 != 0) {
		dev_err(&pdev->dev, "no ATF memory for init\n");
		return -ENOMEM;
	}
	ddr_psci_param = (struct share_params *)res.a1;
	of_get_px30_timings(&pdev->dev, pdev->dev.of_node,
			    (uint32_t *)ddr_psci_param);

	init_waitqueue_head(&wait_ctrl.wait_wq);
	wait_ctrl.wait_en = 1;
	wait_ctrl.wait_time_out_ms = 17 * 5;

	complt_irq = platform_get_irq_byname(pdev, "complete_irq");
	if (complt_irq < 0) {
		dev_err(&pdev->dev, "no IRQ for complete_irq: %d\n",
			complt_irq);
		return complt_irq;
	}

	ret = devm_request_irq(&pdev->dev, complt_irq, wait_complete_irq,
			       0, dev_name(&pdev->dev), &wait_ctrl);
	if (ret < 0) {
		dev_err(&pdev->dev, "cannot request complete_irq\n");
		return ret;
	}

	complt_irq_data = irq_get_irq_data(complt_irq);
	complt_hwirq = irqd_to_hwirq(complt_irq_data);
	ddr_psci_param->complt_hwirq = complt_hwirq;

	res = sip_smc_dram(SHARE_PAGE_TYPE_DDR, 0,
			   ROCKCHIP_SIP_CONFIG_DRAM_INIT);
	if (res.a0) {
		dev_err(&pdev->dev, "rockchip_sip_config_dram_init error:%lx\n",
			res.a0);
		return -ENOMEM;
	}

	dmcfreq->set_auto_self_refresh = rockchip_ddr_set_auto_self_refresh;

	return 0;
}

static __maybe_unused int rk1808_dmc_init(struct platform_device *pdev,
					  struct rockchip_dmcfreq *dmcfreq)
{
	struct arm_smccc_res res;
	u32 size;
	int ret;
	int complt_irq;
	struct device_node *node;

	res = sip_smc_dram(0, 0,
			   ROCKCHIP_SIP_CONFIG_DRAM_GET_VERSION);
	dev_notice(&pdev->dev, "current ATF version 0x%lx!\n", res.a1);
	if (res.a0 || res.a1 < 0x101) {
		dev_err(&pdev->dev,
			"trusted firmware need to update or is invalid!\n");
		return -ENXIO;
	}

	/*
	 * first 4KB is used for interface parameters
	 * after 4KB * N is dts parameters
	 */
	size = sizeof(struct rk1808_ddr_dts_config_timing);
	res = sip_smc_request_share_mem(DIV_ROUND_UP(size, 4096) + 1,
					SHARE_PAGE_TYPE_DDR);
	if (res.a0 != 0) {
		dev_err(&pdev->dev, "no ATF memory for init\n");
		return -ENOMEM;
	}
	ddr_psci_param = (struct share_params *)res.a1;
	of_get_rk1808_timings(&pdev->dev, pdev->dev.of_node,
			      (uint32_t *)ddr_psci_param);

	/* enable start dcf in kernel after dcf ready */
	node = of_parse_phandle(pdev->dev.of_node, "dcf_reg", 0);
	wait_ctrl.regmap_dcf = syscon_node_to_regmap(node);
	if (IS_ERR(wait_ctrl.regmap_dcf))
		return PTR_ERR(wait_ctrl.regmap_dcf);
	wait_ctrl.dcf_en = 1;

	init_waitqueue_head(&wait_ctrl.wait_wq);
	wait_ctrl.wait_en = 1;
	wait_ctrl.wait_time_out_ms = 17 * 5;

	complt_irq = platform_get_irq_byname(pdev, "complete_irq");
	if (complt_irq < 0) {
		dev_err(&pdev->dev, "no IRQ for complete_irq: %d\n",
			complt_irq);
		return complt_irq;
	}

	ret = devm_request_irq(&pdev->dev, complt_irq, wait_dcf_complete_irq,
			       0, dev_name(&pdev->dev), &wait_ctrl);
	if (ret < 0) {
		dev_err(&pdev->dev, "cannot request complete_irq\n");
		return ret;
	}

	res = sip_smc_dram(SHARE_PAGE_TYPE_DDR, 0,
			   ROCKCHIP_SIP_CONFIG_DRAM_INIT);
	if (res.a0) {
		dev_err(&pdev->dev, "rockchip_sip_config_dram_init error:%lx\n",
			res.a0);
		return -ENOMEM;
	}

	dmcfreq->set_auto_self_refresh = rockchip_ddr_set_auto_self_refresh;

	return 0;
}

static __maybe_unused int rk3128_dmc_init(struct platform_device *pdev,
					  struct rockchip_dmcfreq *dmcfreq)
{
	struct arm_smccc_res res;
	struct drm_device *drm = drm_device_get_by_name("rockchip");

	if (!drm) {
		dev_err(&pdev->dev, "Get drm_device fail\n");
		return -EPROBE_DEFER;
	}

	res = sip_smc_request_share_mem(DIV_ROUND_UP(sizeof(
					struct rk3128_ddr_dts_config_timing),
					4096) + 1, SHARE_PAGE_TYPE_DDR);
	if (res.a0) {
		dev_err(&pdev->dev, "no ATF memory for init\n");
		return -ENOMEM;
	}
	ddr_psci_param = (struct share_params *)res.a1;
	of_get_rk3128_timings(&pdev->dev, pdev->dev.of_node,
			      (uint32_t *)ddr_psci_param);

	ddr_psci_param->hz = 0;
	ddr_psci_param->lcdc_type = rk_drm_get_lcdc_type();
	res = sip_smc_dram(SHARE_PAGE_TYPE_DDR, 0,
			   ROCKCHIP_SIP_CONFIG_DRAM_INIT);

	if (res.a0) {
		dev_err(&pdev->dev, "rockchip_sip_config_dram_init error:%lx\n",
			res.a0);
		return -ENOMEM;
	}

	dmcfreq->set_auto_self_refresh = rockchip_ddr_set_auto_self_refresh;

	return 0;
}

static __maybe_unused int rk3228_dmc_init(struct platform_device *pdev,
					  struct rockchip_dmcfreq *dmcfreq)
{
	struct arm_smccc_res res;

	res = sip_smc_request_share_mem(DIV_ROUND_UP(sizeof(
					struct rk3228_ddr_dts_config_timing),
					4096) + 1, SHARE_PAGE_TYPE_DDR);
	if (res.a0) {
		dev_err(&pdev->dev, "no ATF memory for init\n");
		return -ENOMEM;
	}

	ddr_psci_param = (struct share_params *)res.a1;
	if (of_get_rk3228_timings(&pdev->dev, pdev->dev.of_node,
				  (uint32_t *)ddr_psci_param))
		return -ENOMEM;

	ddr_psci_param->hz = 0;
	res = sip_smc_dram(SHARE_PAGE_TYPE_DDR, 0,
			   ROCKCHIP_SIP_CONFIG_DRAM_INIT);

	if (res.a0) {
		dev_err(&pdev->dev, "rockchip_sip_config_dram_init error:%lx\n",
			res.a0);
		return -ENOMEM;
	}

	dmcfreq->set_auto_self_refresh = rockchip_ddr_set_auto_self_refresh;

	return 0;
}

static __maybe_unused int rk3288_dmc_init(struct platform_device *pdev,
					  struct rockchip_dmcfreq *dmcfreq)
{
	struct device *dev = &pdev->dev;
	struct clk *pclk_phy, *pclk_upctl, *dmc_clk;
	struct arm_smccc_res res;
	struct drm_device *drm = drm_device_get_by_name("rockchip");
	int ret;

	if (!drm) {
		dev_err(dev, "Get drm_device fail\n");
		return -EPROBE_DEFER;
	}

	dmc_clk = devm_clk_get(dev, "dmc_clk");
	if (IS_ERR(dmc_clk)) {
		dev_err(dev, "Cannot get the clk dmc_clk\n");
		return PTR_ERR(dmc_clk);
	}
	ret = clk_prepare_enable(dmc_clk);
	if (ret < 0) {
		dev_err(dev, "failed to prepare/enable dmc_clk\n");
		return ret;
	}

	pclk_phy = devm_clk_get(dev, "pclk_phy0");
	if (IS_ERR(pclk_phy)) {
		dev_err(dev, "Cannot get the clk pclk_phy0\n");
		return PTR_ERR(pclk_phy);
	}
	ret = clk_prepare_enable(pclk_phy);
	if (ret < 0) {
		dev_err(dev, "failed to prepare/enable pclk_phy0\n");
		return ret;
	}
	pclk_upctl = devm_clk_get(dev, "pclk_upctl0");
	if (IS_ERR(pclk_upctl)) {
		dev_err(dev, "Cannot get the clk pclk_upctl0\n");
		return PTR_ERR(pclk_upctl);
	}
	ret = clk_prepare_enable(pclk_upctl);
	if (ret < 0) {
		dev_err(dev, "failed to prepare/enable pclk_upctl1\n");
		return ret;
	}

	pclk_phy = devm_clk_get(dev, "pclk_phy1");
	if (IS_ERR(pclk_phy)) {
		dev_err(dev, "Cannot get the clk pclk_phy1\n");
		return PTR_ERR(pclk_phy);
	}
	ret = clk_prepare_enable(pclk_phy);
	if (ret < 0) {
		dev_err(dev, "failed to prepare/enable pclk_phy1\n");
		return ret;
	}
	pclk_upctl = devm_clk_get(dev, "pclk_upctl1");
	if (IS_ERR(pclk_upctl)) {
		dev_err(dev, "Cannot get the clk pclk_upctl1\n");
		return PTR_ERR(pclk_upctl);
	}
	ret = clk_prepare_enable(pclk_upctl);
	if (ret < 0) {
		dev_err(dev, "failed to prepare/enable pclk_upctl1\n");
		return ret;
	}

	res = sip_smc_request_share_mem(DIV_ROUND_UP(sizeof(
					struct rk3288_ddr_dts_config_timing),
					4096) + 1, SHARE_PAGE_TYPE_DDR);
	if (res.a0) {
		dev_err(&pdev->dev, "no ATF memory for init\n");
		return -ENOMEM;
	}

	ddr_psci_param = (struct share_params *)res.a1;
	of_get_rk3288_timings(&pdev->dev, pdev->dev.of_node,
			      (uint32_t *)ddr_psci_param);

	ddr_psci_param->hz = 0;
	ddr_psci_param->lcdc_type = rk_drm_get_lcdc_type();
	res = sip_smc_dram(SHARE_PAGE_TYPE_DDR, 0,
			   ROCKCHIP_SIP_CONFIG_DRAM_INIT);

	if (res.a0) {
		dev_err(&pdev->dev, "rockchip_sip_config_dram_init error:%lx\n",
			res.a0);
		return -ENOMEM;
	}

	dmcfreq->set_auto_self_refresh = rockchip_ddr_set_auto_self_refresh;

	return 0;
}

static __maybe_unused int rk3328_dmc_init(struct platform_device *pdev,
					  struct rockchip_dmcfreq *dmcfreq)
{
	struct arm_smccc_res res;
	u32 size;

	res = sip_smc_dram(0, 0,
			   ROCKCHIP_SIP_CONFIG_DRAM_GET_VERSION);
	dev_notice(&pdev->dev, "current ATF version 0x%lx!\n", res.a1);
	if (res.a0 || (res.a1 < 0x101)) {
		dev_err(&pdev->dev,
			"trusted firmware need to update or is invalid!\n");
		return -ENXIO;
	}

	dev_notice(&pdev->dev, "read tf version 0x%lx!\n", res.a1);

	/*
	 * first 4KB is used for interface parameters
	 * after 4KB * N is dts parameters
	 */
	size = sizeof(struct rk3328_ddr_dts_config_timing);
	res = sip_smc_request_share_mem(DIV_ROUND_UP(size, 4096) + 1,
					SHARE_PAGE_TYPE_DDR);
	if (res.a0 != 0) {
		dev_err(&pdev->dev, "no ATF memory for init\n");
		return -ENOMEM;
	}
	ddr_psci_param = (struct share_params *)res.a1;
	of_get_rk3328_timings(&pdev->dev, pdev->dev.of_node,
			      (uint32_t *)ddr_psci_param);

	res = sip_smc_dram(SHARE_PAGE_TYPE_DDR, 0,
			   ROCKCHIP_SIP_CONFIG_DRAM_INIT);
	if (res.a0) {
		dev_err(&pdev->dev, "rockchip_sip_config_dram_init error:%lx\n",
			res.a0);
		return -ENOMEM;
	}

	dmcfreq->set_auto_self_refresh = rockchip_ddr_set_auto_self_refresh;

	return 0;
}

static __maybe_unused int rk3368_dmc_init(struct platform_device *pdev,
					  struct rockchip_dmcfreq *dmcfreq)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = pdev->dev.of_node;
	struct arm_smccc_res res;
	struct rk3368_dram_timing *dram_timing;
	struct clk *pclk_phy, *pclk_upctl;
	struct drm_device *drm = drm_device_get_by_name("rockchip");
	int ret;
	u32 dram_spd_bin;
	u32 addr_mcu_el3;
	u32 dclk_mode;
	u32 lcdc_type;

	if (!drm) {
		dev_err(dev, "Get drm_device fail\n");
		return -EPROBE_DEFER;
	}

	pclk_phy = devm_clk_get(dev, "pclk_phy");
	if (IS_ERR(pclk_phy)) {
		dev_err(dev, "Cannot get the clk pclk_phy\n");
		return PTR_ERR(pclk_phy);
	}
	ret = clk_prepare_enable(pclk_phy);
	if (ret < 0) {
		dev_err(dev, "failed to prepare/enable pclk_phy\n");
		return ret;
	}
	pclk_upctl = devm_clk_get(dev, "pclk_upctl");
	if (IS_ERR(pclk_upctl)) {
		dev_err(dev, "Cannot get the clk pclk_upctl\n");
		return PTR_ERR(pclk_upctl);
	}
	ret = clk_prepare_enable(pclk_upctl);
	if (ret < 0) {
		dev_err(dev, "failed to prepare/enable pclk_upctl\n");
		return ret;
	}

	/*
	 * Get dram timing and pass it to arm trust firmware,
	 * the dram drvier in arm trust firmware will get these
	 * timing and to do dram initial.
	 */
	dram_timing = of_get_rk3368_timings(dev, np);
	if (dram_timing) {
		dram_spd_bin = dram_timing->dram_spd_bin;
		if (scpi_ddr_send_timing((u32 *)dram_timing,
					 sizeof(struct rk3368_dram_timing)))
			dev_err(dev, "send ddr timing timeout\n");
	} else {
		dev_err(dev, "get ddr timing from dts error\n");
		dram_spd_bin = DDR3_DEFAULT;
	}

	res = sip_smc_mcu_el3fiq(FIQ_INIT_HANDLER,
				 FIQ_NUM_FOR_DCF,
				 FIQ_CPU_TGT_BOOT);
	if ((res.a0) || (res.a1 == 0) || (res.a1 > 0x80000))
		dev_err(dev, "Trust version error, pls check trust version\n");
	addr_mcu_el3 = res.a1;

	if (of_property_read_u32(np, "vop-dclk-mode", &dclk_mode) == 0)
		scpi_ddr_dclk_mode(dclk_mode);

	lcdc_type = rk_drm_get_lcdc_type();

	if (scpi_ddr_init(dram_spd_bin, 0, lcdc_type,
			  addr_mcu_el3))
		dev_err(dev, "ddr init error\n");
	else
		dev_dbg(dev, ("%s out\n"), __func__);

	dmcfreq->set_auto_self_refresh = scpi_ddr_set_auto_self_refresh;

	return 0;
}

static int rk3399_set_msch_readlatency(unsigned int readlatency)
{
	struct arm_smccc_res res;

	arm_smccc_smc(ROCKCHIP_SIP_DRAM_FREQ, readlatency, 0,
		      ROCKCHIP_SIP_CONFIG_DRAM_SET_MSCH_RL,
		      0, 0, 0, 0, &res);

	return res.a0;
}

static __maybe_unused int rk3399_dmc_init(struct platform_device *pdev,
					  struct rockchip_dmcfreq *dmcfreq)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = pdev->dev.of_node;
	struct arm_smccc_res res;
	struct rk3399_dram_timing *dram_timing;
	int index, size;
	u32 *timing;

	/*
	 * Get dram timing and pass it to arm trust firmware,
	 * the dram drvier in arm trust firmware will get these
	 * timing and to do dram initial.
	 */
	dram_timing = of_get_rk3399_timings(dev, np);
	if (dram_timing) {
		timing = (u32 *)dram_timing;
		size = sizeof(struct rk3399_dram_timing) / 4;
		for (index = 0; index < size; index++) {
			arm_smccc_smc(ROCKCHIP_SIP_DRAM_FREQ, *timing++, index,
				      ROCKCHIP_SIP_CONFIG_DRAM_SET_PARAM,
				      0, 0, 0, 0, &res);
			if (res.a0) {
				dev_err(dev, "Failed to set dram param: %ld\n",
					res.a0);
				return -EINVAL;
			}
		}
	}

	arm_smccc_smc(ROCKCHIP_SIP_DRAM_FREQ, 0, 0,
		      ROCKCHIP_SIP_CONFIG_DRAM_INIT,
		      0, 0, 0, 0, &res);

	dmcfreq->set_msch_readlatency = rk3399_set_msch_readlatency;

	return 0;
}

static const struct of_device_id rockchip_dmcfreq_of_match[] = {
#ifdef CONFIG_CPU_PX30
	{ .compatible = "rockchip,px30-dmc", .data = px30_dmc_init },
#endif
#ifdef CONFIG_CPU_RK1808
	{ .compatible = "rockchip,rk1808-dmc", .data = rk1808_dmc_init },
#endif
#ifdef CONFIG_CPU_RK312X
	{ .compatible = "rockchip,rk3128-dmc", .data = rk3128_dmc_init },
#endif
#ifdef CONFIG_CPU_RK322X
	{ .compatible = "rockchip,rk3228-dmc", .data = rk3228_dmc_init },
#endif
#ifdef CONFIG_CPU_RK3288
	{ .compatible = "rockchip,rk3288-dmc", .data = rk3288_dmc_init },
#endif
#ifdef CONFIG_CPU_RK3308
	{ .compatible = "rockchip,rk3308-dmc", .data = NULL },
#endif
#ifdef CONFIG_CPU_RK3328
	{ .compatible = "rockchip,rk3328-dmc", .data = rk3328_dmc_init },
#endif
#ifdef CONFIG_CPU_RK3368
	{ .compatible = "rockchip,rk3368-dmc", .data = rk3368_dmc_init },
#endif
#ifdef CONFIG_CPU_RK3399
	{ .compatible = "rockchip,rk3399-dmc", .data = rk3399_dmc_init },
#endif
	{ },
};
MODULE_DEVICE_TABLE(of, rockchip_dmcfreq_of_match);

static int rockchip_get_freq_map_talbe(struct device_node *np, char *porp_name,
				       struct freq_map_table **table)
{
	struct freq_map_table *tbl;
	const struct property *prop;
	unsigned int temp_freq = 0;
	int count, i;

	prop = of_find_property(np, porp_name, NULL);
	if (!prop)
		return -EINVAL;

	if (!prop->value)
		return -ENODATA;

	count = of_property_count_u32_elems(np, porp_name);
	if (count < 0)
		return -EINVAL;

	if (count % 3)
		return -EINVAL;

	tbl = kzalloc(sizeof(*tbl) * (count / 3 + 1), GFP_KERNEL);
	if (!tbl)
		return -ENOMEM;

	for (i = 0; i < count / 3; i++) {
		of_property_read_u32_index(np, porp_name, 3 * i, &tbl[i].min);
		of_property_read_u32_index(np, porp_name, 3 * i + 1,
					   &tbl[i].max);
		of_property_read_u32_index(np, porp_name, 3 * i + 2,
					   &temp_freq);
		tbl[i].freq = temp_freq * 1000;
	}

	tbl[i].min = 0;
	tbl[i].max = 0;
	tbl[i].freq = CPUFREQ_TABLE_END;

	*table = tbl;

	return 0;
}

static int rockchip_get_rl_map_talbe(struct device_node *np, char *porp_name,
				     struct rl_map_table **table)
{
	struct rl_map_table *tbl;
	const struct property *prop;
	int count, i;

	prop = of_find_property(np, porp_name, NULL);
	if (!prop)
		return -EINVAL;

	if (!prop->value)
		return -ENODATA;

	count = of_property_count_u32_elems(np, porp_name);
	if (count < 0)
		return -EINVAL;

	if (count % 2)
		return -EINVAL;

	tbl = kzalloc(sizeof(*tbl) * (count / 2 + 1), GFP_KERNEL);
	if (!tbl)
		return -ENOMEM;

	for (i = 0; i < count / 2; i++) {
		of_property_read_u32_index(np, porp_name, 2 * i, &tbl[i].pn);
		of_property_read_u32_index(np, porp_name, 2 * i + 1,
					   &tbl[i].rl);
	}

	tbl[i].pn = 0;
	tbl[i].rl = CPUFREQ_TABLE_END;

	*table = tbl;

	return 0;
}

static int rockchip_get_system_status_rate(struct device_node *np,
					   char *porp_name,
					   struct rockchip_dmcfreq *dmcfreq)
{
	const struct property *prop;
	unsigned int status = 0, freq = 0;
	int count, i;

	prop = of_find_property(np, porp_name, NULL);
	if (!prop)
		return -ENODEV;

	if (!prop->value)
		return -ENODATA;

	count = of_property_count_u32_elems(np, porp_name);
	if (count < 0)
		return -EINVAL;

	if (count % 2)
		return -EINVAL;

	for (i = 0; i < count / 2; i++) {
		of_property_read_u32_index(np, porp_name, 2 * i,
					   &status);
		of_property_read_u32_index(np, porp_name, 2 * i + 1,
					   &freq);
		switch (status) {
		case SYS_STATUS_NORMAL:
			dmcfreq->normal_rate = freq * 1000;
			break;
		case SYS_STATUS_SUSPEND:
			dmcfreq->suspend_rate = freq * 1000;
			break;
		case SYS_STATUS_VIDEO_1080P:
			dmcfreq->video_1080p_rate = freq * 1000;
			break;
		case SYS_STATUS_VIDEO_4K:
			dmcfreq->video_4k_rate = freq * 1000;
			break;
		case SYS_STATUS_VIDEO_4K_10B:
			dmcfreq->video_4k_10b_rate = freq * 1000;
			break;
		case SYS_STATUS_PERFORMANCE:
			dmcfreq->performance_rate = freq * 1000;
			break;
		case SYS_STATUS_LCDC0 | SYS_STATUS_LCDC1:
			dmcfreq->dualview_rate = freq * 1000;
			break;
		case SYS_STATUS_HDMI:
			dmcfreq->hdmi_rate = freq * 1000;
			break;
		case SYS_STATUS_IDLE:
			dmcfreq->idle_rate = freq * 1000;
			break;
		case SYS_STATUS_REBOOT:
			dmcfreq->reboot_rate = freq * 1000;
			break;
		case SYS_STATUS_BOOST:
			dmcfreq->boost_rate = freq * 1000;
			break;
		case SYS_STATUS_ISP:
			dmcfreq->isp_rate = freq * 1000;
			break;
		case SYS_STATUS_LOW_POWER:
			dmcfreq->low_power_rate = freq * 1000;
			break;
		default:
			break;
		}
	}

	return 0;
}

static void rockchip_dmcfreq_update_target(struct rockchip_dmcfreq *dmcfreq)
{
	struct devfreq *df = dmcfreq->devfreq;

	mutex_lock(&df->lock);

	if (dmcfreq->last_refresh != dmcfreq->refresh) {
		if (dmcfreq->set_auto_self_refresh)
			dmcfreq->set_auto_self_refresh(dmcfreq->refresh);
		dmcfreq->last_refresh = dmcfreq->refresh;
	}

	update_devfreq(df);

	mutex_unlock(&df->lock);
}

static int rockchip_dmcfreq_system_status_notifier(struct notifier_block *nb,
						   unsigned long status,
						   void *ptr)
{
	struct rockchip_dmcfreq *dmcfreq = system_status_to_dmcfreq(nb);
	unsigned long target_rate = 0;
	unsigned int refresh = false;
	bool is_fixed = false;

	if (dmcfreq->dualview_rate && dmcfreq->isp_rate &&
	    (status & SYS_STATUS_ISP) &&
	    (status & SYS_STATUS_LCDC0) &&
	    (status & SYS_STATUS_LCDC1))
		return NOTIFY_OK;

	if (dmcfreq->dualview_rate && (status & SYS_STATUS_LCDC0) &&
	    (status & SYS_STATUS_LCDC1)) {
		target_rate = dmcfreq->dualview_rate;
		is_fixed = true;
		goto next;
	}

	if (dmcfreq->isp_rate && (status & SYS_STATUS_ISP)) {
		target_rate = dmcfreq->isp_rate;
		is_fixed = true;
		goto next;
	}

	if (dmcfreq->reboot_rate && (status & SYS_STATUS_REBOOT)) {
		target_rate = dmcfreq->reboot_rate;
		goto next;
	}

	if (dmcfreq->suspend_rate && (status & SYS_STATUS_SUSPEND)) {
		target_rate = dmcfreq->suspend_rate;
		refresh = true;
		goto next;
	}

	if (dmcfreq->low_power_rate && (status & SYS_STATUS_LOW_POWER)) {
		target_rate = dmcfreq->low_power_rate;
		goto next;
	}

	if (dmcfreq->performance_rate && (status & SYS_STATUS_PERFORMANCE)) {
		if (dmcfreq->performance_rate > target_rate)
			target_rate = dmcfreq->performance_rate;
	}

	if (dmcfreq->hdmi_rate && (status & SYS_STATUS_HDMI)) {
		if (dmcfreq->hdmi_rate > target_rate)
			target_rate = dmcfreq->hdmi_rate;
	}

	if (dmcfreq->video_4k_rate && (status & SYS_STATUS_VIDEO_4K)) {
		if (dmcfreq->video_4k_rate > target_rate)
			target_rate = dmcfreq->video_4k_rate;
	}

	if (dmcfreq->video_4k_10b_rate && (status & SYS_STATUS_VIDEO_4K_10B)) {
		if (dmcfreq->video_4k_10b_rate > target_rate)
			target_rate = dmcfreq->video_4k_10b_rate;
	}

	if (dmcfreq->video_1080p_rate && (status & SYS_STATUS_VIDEO_1080P)) {
		if (dmcfreq->video_1080p_rate > target_rate)
			target_rate = dmcfreq->video_1080p_rate;
	}

next:

	dev_dbg(&dmcfreq->devfreq->dev, "status=0x%x\n", (unsigned int)status);
	dmcfreq->refresh = refresh;
	dmcfreq->is_fixed = is_fixed;
	dmcfreq->status_rate = target_rate;
	rockchip_dmcfreq_update_target(dmcfreq);

	return NOTIFY_OK;
}

static int rockchip_dmcfreq_reboot_notifier(struct notifier_block *nb,
					    unsigned long action, void *ptr)
{
	struct rockchip_dmcfreq *dmcfreq = reboot_to_dmcfreq(nb);

	devfreq_monitor_stop(dmcfreq->devfreq);
	rockchip_set_system_status(SYS_STATUS_REBOOT);

	return NOTIFY_OK;
}

static int rockchip_dmcfreq_fb_notifier(struct notifier_block *nb,
					unsigned long action, void *ptr)
{
	struct fb_event *event = ptr;

	switch (action) {
	case FB_EARLY_EVENT_BLANK:
		switch (*((int *)event->data)) {
		case FB_BLANK_UNBLANK:
			rockchip_clear_system_status(SYS_STATUS_SUSPEND);
			break;
		default:
			break;
		}
		break;
	case FB_EVENT_BLANK:
		switch (*((int *)event->data)) {
		case FB_BLANK_POWERDOWN:
			rockchip_set_system_status(SYS_STATUS_SUSPEND);
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}

	return NOTIFY_OK;
}

static ssize_t rockchip_dmcfreq_status_show(struct device *dev,
					    struct device_attribute *attr,
					    char *buf)
{
	unsigned int status = rockchip_get_system_status();

	return sprintf(buf, "0x%x\n", status);
}

static unsigned long rockchip_get_video_param(char **str)
{
	char *p;
	unsigned long val = 0;

	strsep(str, "=");
	p = strsep(str, ",");
	if (p) {
		if (kstrtoul(p, 10, &val))
			return 0;
	}

	return val;
}

/*
 * format:
 * 0,width=val,height=val,ishevc=val,videoFramerate=val,streamBitrate=val
 * 1,width=val,height=val,ishevc=val,videoFramerate=val,streamBitrate=val
 */
static struct video_info *rockchip_parse_video_info(const char *buf)
{
	struct video_info *video_info;
	const char *cp = buf;
	char *str;
	int ntokens = 0;

	while ((cp = strpbrk(cp + 1, ",")))
		ntokens++;
	if (ntokens != 5)
		return NULL;

	video_info = kzalloc(sizeof(*video_info), GFP_KERNEL);
	if (!video_info)
		return NULL;

	INIT_LIST_HEAD(&video_info->node);

	str = kstrdup(buf, GFP_KERNEL);
	strsep(&str, ",");
	video_info->width = rockchip_get_video_param(&str);
	video_info->height = rockchip_get_video_param(&str);
	video_info->ishevc = rockchip_get_video_param(&str);
	video_info->videoFramerate = rockchip_get_video_param(&str);
	video_info->streamBitrate = rockchip_get_video_param(&str);
	pr_debug("%c,width=%d,height=%d,ishevc=%d,videoFramerate=%d,streamBitrate=%d\n",
		 buf[0],
		 video_info->width,
		 video_info->height,
		 video_info->ishevc,
		 video_info->videoFramerate,
		 video_info->streamBitrate);
	kfree(str);

	return video_info;
}

static struct video_info *
rockchip_find_video_info(struct rockchip_dmcfreq *dmcfreq, const char *buf)
{
	struct video_info *info, *video_info;

	video_info = rockchip_parse_video_info(buf);

	if (!video_info)
		return NULL;

	mutex_lock(&dmcfreq->lock);
	list_for_each_entry(info, &dmcfreq->video_info_list, node) {
		if ((info->width == video_info->width) &&
		    (info->height == video_info->height) &&
		    (info->ishevc == video_info->ishevc) &&
		    (info->videoFramerate == video_info->videoFramerate) &&
		    (info->streamBitrate == video_info->streamBitrate)) {
			mutex_unlock(&dmcfreq->lock);
			kfree(video_info);
			return info;
		}
	}

	mutex_unlock(&dmcfreq->lock);
	kfree(video_info);

	return NULL;
}

static void rockchip_add_video_info(struct rockchip_dmcfreq *dmcfreq,
				    struct video_info *video_info)
{
	if (video_info) {
		mutex_lock(&dmcfreq->lock);
		list_add(&video_info->node, &dmcfreq->video_info_list);
		mutex_unlock(&dmcfreq->lock);
	}
}

static void rockchip_del_video_info(struct rockchip_dmcfreq *dmcfreq,
				    struct video_info *video_info)
{
	if (video_info) {
		mutex_lock(&dmcfreq->lock);
		list_del(&video_info->node);
		mutex_unlock(&dmcfreq->lock);
		kfree(video_info);
	}
}

static void rockchip_update_video_info(struct rockchip_dmcfreq *dmcfreq)
{
	struct video_info *video_info;
	int max_res = 0, max_stream_bitrate = 0, res = 0;

	mutex_lock(&dmcfreq->lock);
	if (list_empty(&dmcfreq->video_info_list)) {
		mutex_unlock(&dmcfreq->lock);
		rockchip_clear_system_status(SYS_STATUS_VIDEO);
		return;
	}

	list_for_each_entry(video_info, &dmcfreq->video_info_list, node) {
		res = video_info->width * video_info->height;
		if (res > max_res)
			max_res = res;
		if (video_info->streamBitrate > max_stream_bitrate)
			max_stream_bitrate = video_info->streamBitrate;
	}
	mutex_unlock(&dmcfreq->lock);

	if (max_res <= VIDEO_1080P_SIZE) {
		rockchip_set_system_status(SYS_STATUS_VIDEO_1080P);
	} else {
		if (max_stream_bitrate == 10)
			rockchip_set_system_status(SYS_STATUS_VIDEO_4K_10B);
		else
			rockchip_set_system_status(SYS_STATUS_VIDEO_4K);
	}
}

static ssize_t rockchip_dmcfreq_status_store(struct device *dev,
					     struct device_attribute *attr,
					     const char *buf,
					     size_t count)
{
	struct devfreq *devfreq = to_devfreq(dev);
	struct rockchip_dmcfreq *dmcfreq = dev_get_drvdata(devfreq->dev.parent);
	struct video_info *video_info;

	if (!count)
		return -EINVAL;

	switch (buf[0]) {
	case '0':
		/* clear video flag */
		video_info = rockchip_find_video_info(dmcfreq, buf);
		if (video_info) {
			rockchip_del_video_info(dmcfreq, video_info);
			rockchip_update_video_info(dmcfreq);
		}
		break;
	case '1':
		/* set video flag */
		video_info = rockchip_parse_video_info(buf);
		if (video_info) {
			rockchip_add_video_info(dmcfreq, video_info);
			rockchip_update_video_info(dmcfreq);
		}
		break;
	case 'L':
		/* clear low power flag */
		rockchip_clear_system_status(SYS_STATUS_LOW_POWER);
		break;
	case 'l':
		/* set low power flag */
		rockchip_set_system_status(SYS_STATUS_LOW_POWER);
		break;
	case 'p':
		/* set performance flag */
		rockchip_set_system_status(SYS_STATUS_PERFORMANCE);
		break;
	case 'n':
		/* clear performance flag */
		rockchip_clear_system_status(SYS_STATUS_PERFORMANCE);
		break;
	default:
		break;
	}

	return count;
}

static DEVICE_ATTR(system_status, 0644, rockchip_dmcfreq_status_show,
		   rockchip_dmcfreq_status_store);

static void rockchip_dmcfreq_set_msch_rl(struct rockchip_dmcfreq *dmcfreq,
					 unsigned int readlatency)

{
	down_read(&rockchip_dmcfreq_sem);
	dev_dbg(dmcfreq->dev, "rl 0x%x -> 0x%x\n",
		dmcfreq->read_latency, readlatency);
	if (!dmcfreq->set_msch_readlatency(readlatency))
		dmcfreq->read_latency = readlatency;
	else
		dev_err(dmcfreq->dev, "failed to set msch rl\n");
	up_read(&rockchip_dmcfreq_sem);
}

static void rockchip_dmcfreq_set_msch_rl_work(struct work_struct *work)
{
	struct rockchip_dmcfreq *dmcfreq = msch_rl_to_dmcfreq(work);

	rockchip_dmcfreq_set_msch_rl(dmcfreq, 0);
	dmcfreq->is_msch_rl_work_started = false;
}

static void rockchip_dmcfreq_msch_rl_init(struct rockchip_dmcfreq *dmcfreq)
{
	if (!dmcfreq->set_msch_readlatency)
		return;
	INIT_DELAYED_WORK(&dmcfreq->msch_rl_work,
			  rockchip_dmcfreq_set_msch_rl_work);
}

void rockchip_dmcfreq_vop_bandwidth_update(struct devfreq *devfreq,
					   unsigned int bw_mbyte,
					   unsigned int plane_num)
{
	struct device *dev;
	struct rockchip_dmcfreq *dmcfreq;
	unsigned long vop_last_rate, target = 0;
	unsigned int readlatency = 0;
	int i;

	if (!devfreq)
		return;

	dev = devfreq->dev.parent;
	dmcfreq = dev_get_drvdata(dev);
	if (!dmcfreq)
		return;

	if (!dmcfreq->vop_pn_rl_tbl || !dmcfreq->set_msch_readlatency)
		goto vop_bw_tbl;
	for (i = 0; dmcfreq->vop_pn_rl_tbl[i].rl != CPUFREQ_TABLE_END; i++) {
		if (plane_num >= dmcfreq->vop_pn_rl_tbl[i].pn)
			readlatency = dmcfreq->vop_pn_rl_tbl[i].rl;
	}
	dev_dbg(dmcfreq->dev, "pn=%u\n", plane_num);
	if (readlatency) {
		cancel_delayed_work_sync(&dmcfreq->msch_rl_work);
		dmcfreq->is_msch_rl_work_started = false;
		if (dmcfreq->read_latency != readlatency)
			rockchip_dmcfreq_set_msch_rl(dmcfreq, readlatency);
	} else if (dmcfreq->read_latency &&
		   !dmcfreq->is_msch_rl_work_started) {
		dmcfreq->is_msch_rl_work_started = true;
		schedule_delayed_work(&dmcfreq->msch_rl_work,
				      msecs_to_jiffies(MSCH_RL_DELAY_TIME));
	}

vop_bw_tbl:
	if (!dmcfreq->auto_freq_en || !dmcfreq->vop_bw_tbl)
		return;

	for (i = 0; dmcfreq->vop_bw_tbl[i].freq != CPUFREQ_TABLE_END; i++) {
		if (bw_mbyte >= dmcfreq->vop_bw_tbl[i].min)
			target = dmcfreq->vop_bw_tbl[i].freq;
	}

	dev_dbg(dmcfreq->dev, "bw=%u\n", bw_mbyte);

	if (!target || target == dmcfreq->vop_req_rate)
		return;

	vop_last_rate = dmcfreq->vop_req_rate;
	dmcfreq->vop_req_rate = target;

	if (target > vop_last_rate)
		rockchip_dmcfreq_update_target(dmcfreq);
}

int rockchip_dmcfreq_vop_bandwidth_request(struct devfreq *devfreq,
					   unsigned int bw_mbyte)
{
	struct device *dev;
	struct rockchip_dmcfreq *dmcfreq;
	unsigned long target = 0;
	int i;

	if (!devfreq)
		return 0;

	dev = devfreq->dev.parent;
	dmcfreq = dev_get_drvdata(dev);

	if (!dmcfreq || !dmcfreq->auto_freq_en || !dmcfreq->vop_bw_tbl)
		return 0;

	for (i = 0; dmcfreq->vop_bw_tbl[i].freq != CPUFREQ_TABLE_END; i++) {
		if (bw_mbyte <= dmcfreq->vop_bw_tbl[i].max) {
			target = dmcfreq->vop_bw_tbl[i].freq;
			break;
		}
	}
	if (target)
		return 0;
	else
		return -EINVAL;
}

static int devfreq_dmc_ondemand_func(struct devfreq *df,
				     unsigned long *freq)
{
	int err;
	struct devfreq_dev_status *stat;
	unsigned long long a, b;
	unsigned long max_freq = (df->max_freq) ? df->max_freq : UINT_MAX;
	struct rockchip_dmcfreq *dmcfreq = dev_get_drvdata(df->dev.parent);
	struct devfreq_simple_ondemand_data *data = &dmcfreq->ondemand_data;
	unsigned int upthreshold = data->upthreshold;
	unsigned int downdifferential = data->downdifferential;
	unsigned long target_freq = 0;
	u64 now;

	if (dmcfreq->auto_freq_en && !dmcfreq->is_fixed) {
		if (dmcfreq->status_rate)
			target_freq = dmcfreq->status_rate;
		else if (dmcfreq->auto_min_rate)
			target_freq = dmcfreq->auto_min_rate;
		now = ktime_to_us(ktime_get());
		if (now < dmcfreq->touchboostpulse_endtime)
			target_freq = max3(target_freq, dmcfreq->vop_req_rate,
					   dmcfreq->boost_rate);
		else
			target_freq = max(target_freq, dmcfreq->vop_req_rate);
	} else {
		if (dmcfreq->status_rate)
			target_freq = dmcfreq->status_rate;
		else if (dmcfreq->normal_rate)
			target_freq = dmcfreq->normal_rate;
		if (target_freq)
			*freq = target_freq;
		goto reset_last_status;
	}

	if (!upthreshold || !downdifferential)
		goto reset_last_status;

	if (upthreshold > 100 ||
	    upthreshold < downdifferential)
		goto reset_last_status;

	err = devfreq_update_stats(df);
	if (err)
		goto reset_last_status;

	stat = &df->last_status;

	/* Assume MAX if it is going to be divided by zero */
	if (stat->total_time == 0) {
		*freq = max_freq;
		return 0;
	}

	/* Prevent overflow */
	if (stat->busy_time >= (1 << 24) || stat->total_time >= (1 << 24)) {
		stat->busy_time >>= 7;
		stat->total_time >>= 7;
	}

	/* Set MAX if it's busy enough */
	if (stat->busy_time * 100 >
	    stat->total_time * upthreshold) {
		*freq = max_freq;
		return 0;
	}

	/* Set MAX if we do not know the initial frequency */
	if (stat->current_frequency == 0) {
		*freq = max_freq;
		return 0;
	}

	/* Keep the current frequency */
	if (stat->busy_time * 100 >
	    stat->total_time * (upthreshold - downdifferential)) {
		*freq = max(target_freq, stat->current_frequency);
		goto next;
	}

	/* Set the desired frequency based on the load */
	a = stat->busy_time;
	a *= stat->current_frequency;
	b = div_u64(a, stat->total_time);
	b *= 100;
	b = div_u64(b, (upthreshold - downdifferential / 2));
	*freq = max_t(unsigned long, target_freq, b);
	goto next;

reset_last_status:
	reset_last_status(df);
next:
	if (df->min_freq && *freq < df->min_freq)
		*freq = df->min_freq;
	if (df->max_freq && *freq > df->max_freq)
		*freq = df->max_freq;

	return 0;
}

static int devfreq_dmc_ondemand_handler(struct devfreq *devfreq,
					unsigned int event, void *data)
{
	struct rockchip_dmcfreq *dmcfreq = dev_get_drvdata(devfreq->dev.parent);

	if (!dmcfreq->auto_freq_en)
		return 0;

	switch (event) {
	case DEVFREQ_GOV_START:
		devfreq_monitor_start(devfreq);
		break;

	case DEVFREQ_GOV_STOP:
		devfreq_monitor_stop(devfreq);
		break;

	case DEVFREQ_GOV_INTERVAL:
		devfreq_interval_update(devfreq, (unsigned int *)data);
		break;

	case DEVFREQ_GOV_SUSPEND:
		devfreq_monitor_suspend(devfreq);
		break;

	case DEVFREQ_GOV_RESUME:
		devfreq_monitor_resume(devfreq);
		break;

	default:
		break;
	}

	return 0;
}

static struct devfreq_governor devfreq_dmc_ondemand = {
	.name = "dmc_ondemand",
	.get_target_freq = devfreq_dmc_ondemand_func,
	.event_handler = devfreq_dmc_ondemand_handler,
};

static int rockchip_dmcfreq_enable_event(struct rockchip_dmcfreq *dmcfreq)
{
	int i, ret;

	if (!dmcfreq->auto_freq_en)
		return 0;

	for (i = 0; i < dmcfreq->edev_count; i++) {
		ret = devfreq_event_enable_edev(dmcfreq->edev[i]);
		if (ret < 0) {
			dev_err(dmcfreq->dev,
				"failed to enable devfreq-event\n");
			return ret;
		}
	}

	return 0;
}

static int rockchip_dmcfreq_disable_event(struct rockchip_dmcfreq *dmcfreq)
{
	int i, ret;

	if (!dmcfreq->auto_freq_en)
		return 0;

	for (i = 0; i < dmcfreq->edev_count; i++) {
		ret = devfreq_event_disable_edev(dmcfreq->edev[i]);
		if (ret < 0) {
			dev_err(dmcfreq->dev,
				"failed to disable devfreq-event\n");
			return ret;
		}
	}

	return 0;
}

static int rockchip_get_edev_id(struct rockchip_dmcfreq *dmcfreq,
				const char *name)
{
	struct devfreq_event_dev *edev;
	int i;

	for (i = 0; i < dmcfreq->edev_count; i++) {
		edev = dmcfreq->edev[i];
		if (!strcmp(edev->desc->name, name))
			return i;
	}

	return -EINVAL;
}

static int rockchip_dmcfreq_get_event(struct rockchip_dmcfreq *dmcfreq)
{
	struct device *dev = dmcfreq->dev;
	struct device_node *events_np, *np = dev->of_node;
	int i, j, count, available_count = 0;

	count = devfreq_event_get_edev_count(dev);
	if (count < 0) {
		dev_dbg(dev, "failed to get count of devfreq-event dev\n");
		return 0;
	}
	for (i = 0; i < count; i++) {
		events_np = of_parse_phandle(np, "devfreq-events", i);
		if (!events_np)
			continue;
		if (of_device_is_available(events_np))
			available_count++;
		of_node_put(events_np);
	}
	if (!available_count) {
		dev_dbg(dev, "failed to get available devfreq-event\n");
		return 0;
	}
	dmcfreq->edev_count = available_count;
	dmcfreq->edev = devm_kzalloc(dev,
				     sizeof(*dmcfreq->edev) * available_count,
				     GFP_KERNEL);
	if (!dmcfreq->edev)
		return -ENOMEM;

	for (i = 0, j = 0; i < count; i++) {
		events_np = of_parse_phandle(np, "devfreq-events", i);
		if (!events_np)
			continue;
		if (of_device_is_available(events_np)) {
			of_node_put(events_np);
			if (j >= available_count) {
				dev_err(dev, "invalid event conut\n");
				return -EINVAL;
			}
			dmcfreq->edev[j] =
				devfreq_event_get_edev_by_phandle(dev, i);
			if (IS_ERR(dmcfreq->edev[j]))
				return -EPROBE_DEFER;
			j++;
		} else {
			of_node_put(events_np);
		}
	}
	dmcfreq->auto_freq_en = true;
	dmcfreq->dfi_id = rockchip_get_edev_id(dmcfreq, "dfi");
	if (dmcfreq->dfi_id >= 0)
		available_count--;
	if (available_count <= 0)
		return 0;
	dmcfreq->nocp_bw =
		devm_kzalloc(dev, sizeof(*dmcfreq->nocp_bw) * available_count,
			     GFP_KERNEL);
	if (!dmcfreq->nocp_bw)
		return -ENOMEM;

	return 0;
}

static int rockchip_dmcfreq_power_control(struct rockchip_dmcfreq *dmcfreq)
{
	struct device *dev = dmcfreq->dev;

	dmcfreq->vdd_center = devm_regulator_get_optional(dev, "center");
	if (IS_ERR(dmcfreq->vdd_center)) {
		dev_err(dev, "Cannot get the regulator \"center\"\n");
		return PTR_ERR(dmcfreq->vdd_center);
	}

	dmcfreq->dmc_clk = devm_clk_get(dev, "dmc_clk");
	if (IS_ERR(dmcfreq->dmc_clk)) {
		dev_err(dev, "Cannot get the clk dmc_clk\n");
		return PTR_ERR(dmcfreq->dmc_clk);
	}
	dmcfreq->rate = clk_get_rate(dmcfreq->dmc_clk);

	return 0;
}

static int rockchip_dmcfreq_dmc_init(struct platform_device *pdev,
				     struct rockchip_dmcfreq *dmcfreq)
{
	const struct of_device_id *match;
	int (*init)(struct platform_device *pdev,
		    struct rockchip_dmcfreq *data);
	int ret;

	match = of_match_node(rockchip_dmcfreq_of_match, pdev->dev.of_node);
	if (match) {
		init = match->data;
		if (init) {
			ret = init(pdev, dmcfreq);
			if (ret)
				return ret;
		}
	}

	return 0;
}

static void rockchip_dmcfreq_parse_dt(struct rockchip_dmcfreq *dmcfreq)
{
	struct device *dev = dmcfreq->dev;
	struct device_node *np = dev->of_node;

	if (!rockchip_get_system_status_rate(np, "system-status-freq",
					     dmcfreq))
		dmcfreq->system_status_en = true;
	of_property_read_u32(np, "min-cpu-freq", &dmcfreq->min_cpu_freq);

	of_property_read_u32(np, "upthreshold",
			     &dmcfreq->ondemand_data.upthreshold);
	of_property_read_u32(np, "downdifferential",
			     &dmcfreq->ondemand_data.downdifferential);
	if (dmcfreq->auto_freq_en)
		of_property_read_u32(np, "auto-freq-en",
				     &dmcfreq->auto_freq_en);
	of_property_read_u32(np, "auto-min-freq",
			     (u32 *)&dmcfreq->auto_min_rate);
	dmcfreq->auto_min_rate *= 1000;

	if (rockchip_get_freq_map_talbe(np, "vop-bw-dmc-freq",
					&dmcfreq->vop_bw_tbl))
		dev_err(dev, "failed to get vop bandwidth to dmc rate\n");
	if (rockchip_get_rl_map_talbe(np, "vop-pn-msch-readlatency",
				      &dmcfreq->vop_pn_rl_tbl))
		dev_err(dev, "failed to get vop pn to msch rl\n");

	of_property_read_u32(np, "touchboost_duration",
			     (u32 *)&dmcfreq->touchboostpulse_duration_val);
	if (dmcfreq->touchboostpulse_duration_val)
		dmcfreq->touchboostpulse_duration_val *= USEC_PER_MSEC;
	else
		dmcfreq->touchboostpulse_duration_val = 500 * USEC_PER_MSEC;
}

static int rockchip_dmcfreq_set_volt_only(struct rockchip_dmcfreq *dmcfreq)
{
	struct device *dev = dmcfreq->dev;
	struct dev_pm_opp *opp;
	unsigned long opp_volt, opp_rate = dmcfreq->rate;
	int ret;

	rcu_read_lock();
	opp = devfreq_recommended_opp(dev, &opp_rate, 0);
	if (IS_ERR(opp)) {
		rcu_read_unlock();
		return PTR_ERR(opp);
	}
	opp_volt = dev_pm_opp_get_voltage(opp);
	rcu_read_unlock();

	ret = regulator_set_voltage(dmcfreq->vdd_center, opp_volt, INT_MAX);
	if (ret) {
		dev_err(dev, "Cannot set voltage %lu uV\n", opp_volt);
		return ret;
	}

	return 0;
}

static int rockchip_dmcfreq_add_devfreq(struct rockchip_dmcfreq *dmcfreq)
{
	struct devfreq_dev_profile *devp = &rockchip_devfreq_dmc_profile;
	struct device *dev = dmcfreq->dev;
	struct dev_pm_opp *opp;
	unsigned long opp_rate = dmcfreq->rate;

	rcu_read_lock();
	opp = devfreq_recommended_opp(dev, &opp_rate, 0);
	if (IS_ERR(opp)) {
		rcu_read_unlock();
		return PTR_ERR(opp);
	}
	rcu_read_unlock();

	if (rockchip_dmcfreq_init_freq_table(dev, devp)) {
		dev_err(dev, "failed to set init freq talbe\n");
		return -EFAULT;
	}

	devp->initial_freq = dmcfreq->rate;
	dmcfreq->devfreq = devm_devfreq_add_device(dev, devp,
						   "dmc_ondemand",
						   &dmcfreq->ondemand_data);
	if (IS_ERR(dmcfreq->devfreq)) {
		dev_err(dev, "failed to add devfreq\n");
		return PTR_ERR(dmcfreq->devfreq);
	}

	devm_devfreq_register_opp_notifier(dev, dmcfreq->devfreq);

	dmcfreq->min = devp->freq_table[0];
	dmcfreq->max =
		devp->freq_table[devp->max_state ? devp->max_state - 1 : 0];
	dmcfreq->devfreq->min_freq = dmcfreq->min;
	dmcfreq->devfreq->max_freq = dmcfreq->max;
	dmcfreq->devfreq->last_status.current_frequency = opp_rate;

	reset_last_status(dmcfreq->devfreq);

	return 0;
}

static void rockchip_dmcfreq_register_notifier(struct rockchip_dmcfreq *dmcfreq)
{
	int ret;

	if (vop_register_dmc())
		dev_err(dmcfreq->dev, "fail to register notify to vop.\n");

	dmcfreq->status_nb.notifier_call =
		rockchip_dmcfreq_system_status_notifier;
	ret = rockchip_register_system_status_notifier(&dmcfreq->status_nb);
	if (ret)
		dev_err(dmcfreq->dev, "failed to register system_status nb\n");

	dmcfreq->reboot_nb.notifier_call = rockchip_dmcfreq_reboot_notifier;
	ret = register_reboot_notifier(&dmcfreq->reboot_nb);
	if (ret)
		dev_err(dmcfreq->dev, "failed to register reboot nb\n");

	dmcfreq->fb_nb.notifier_call = rockchip_dmcfreq_fb_notifier;
	ret = fb_register_client(&dmcfreq->fb_nb);
	if (ret)
		dev_err(dmcfreq->dev, "failed to register fb nb\n");

	dmc_devdata.data = dmcfreq->devfreq;
	dmcfreq->opp_info = rockchip_register_thermal_notifier(dmcfreq->dev,
							       &dmc_devdata);
	if (IS_ERR(dmcfreq->opp_info)) {
		dev_dbg(dmcfreq->dev, "without thermal notifier\n");
		dmcfreq->opp_info = NULL;
	}
}

static void rockchip_dmcfreq_add_interface(struct rockchip_dmcfreq *dmcfreq)
{
	if (sysfs_create_file(&dmcfreq->devfreq->dev.kobj,
			      &dev_attr_system_status.attr))
		dev_err(dmcfreq->dev,
			"failed to register system_status sysfs file\n");
}

static void rockchip_dmcfreq_boost_work(struct work_struct *work)
{
	struct rockchip_dmcfreq *dmcfreq = boost_to_dmcfreq(work);

	rockchip_dmcfreq_update_target(dmcfreq);
}

static void rockchip_dmcfreq_input_event(struct input_handle *handle,
					 unsigned int type,
					 unsigned int code,
					 int value)
{
	struct rockchip_dmcfreq *dmcfreq = handle->private;
	u64 now, endtime;

	if (type != EV_ABS && type != EV_KEY)
		return;

	now = ktime_to_us(ktime_get());
	endtime = now + dmcfreq->touchboostpulse_duration_val;
	if (endtime < (dmcfreq->touchboostpulse_endtime + 10 * USEC_PER_MSEC))
		return;
	dmcfreq->touchboostpulse_endtime = endtime;

	schedule_work(&dmcfreq->boost_work);
}

static int rockchip_dmcfreq_input_connect(struct input_handler *handler,
					  struct input_dev *dev,
					  const struct input_device_id *id)
{
	int error;
	struct input_handle *handle;
	struct rockchip_dmcfreq *dmcfreq = input_hd_to_dmcfreq(handler);

	handle = kzalloc(sizeof(*handle), GFP_KERNEL);
	if (!handle)
		return -ENOMEM;

	handle->dev = dev;
	handle->handler = handler;
	handle->name = "dmcfreq";
	handle->private = dmcfreq;

	error = input_register_handle(handle);
	if (error)
		goto err2;

	error = input_open_device(handle);
	if (error)
		goto err1;

	return 0;
err1:
	input_unregister_handle(handle);
err2:
	kfree(handle);
	return error;
}

static void rockchip_dmcfreq_input_disconnect(struct input_handle *handle)
{
	input_close_device(handle);
	input_unregister_handle(handle);
	kfree(handle);
}

static const struct input_device_id rockchip_dmcfreq_input_ids[] = {
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT |
			INPUT_DEVICE_ID_MATCH_ABSBIT,
		.evbit = { BIT_MASK(EV_ABS) },
		.absbit = { [BIT_WORD(ABS_MT_POSITION_X)] =
			BIT_MASK(ABS_MT_POSITION_X) |
			BIT_MASK(ABS_MT_POSITION_Y) },
	},
	{
		.flags = INPUT_DEVICE_ID_MATCH_KEYBIT |
			INPUT_DEVICE_ID_MATCH_ABSBIT,
		.keybit = { [BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH) },
		.absbit = { [BIT_WORD(ABS_X)] =
			BIT_MASK(ABS_X) | BIT_MASK(ABS_Y) },
	},
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT,
		.evbit = { BIT_MASK(EV_KEY) },
	},
	{ },
};

static void rockchip_dmcfreq_boost_init(struct rockchip_dmcfreq *dmcfreq)
{
	if (!dmcfreq->boost_rate)
		return;
	INIT_WORK(&dmcfreq->boost_work, rockchip_dmcfreq_boost_work);
	dmcfreq->input_handler.event = rockchip_dmcfreq_input_event;
	dmcfreq->input_handler.connect = rockchip_dmcfreq_input_connect;
	dmcfreq->input_handler.disconnect = rockchip_dmcfreq_input_disconnect;
	dmcfreq->input_handler.name = "dmcfreq";
	dmcfreq->input_handler.id_table = rockchip_dmcfreq_input_ids;
	if (input_register_handler(&dmcfreq->input_handler))
		dev_err(dmcfreq->dev, "failed to register input handler\n");
}

static unsigned long model_static_power(struct devfreq *devfreq,
					unsigned long voltage)
{
	struct device *dev = devfreq->dev.parent;
	struct rockchip_dmcfreq *dmcfreq = dev_get_drvdata(dev);

	int temperature;
	unsigned long temp;
	unsigned long temp_squared, temp_cubed, temp_scaling_factor;
	const unsigned long voltage_cubed = (voltage * voltage * voltage) >> 10;

	if (!IS_ERR_OR_NULL(dmcfreq->ddr_tz) && dmcfreq->ddr_tz->ops->get_temp) {
		int ret;

		ret =
		    dmcfreq->ddr_tz->ops->get_temp(dmcfreq->ddr_tz,
						   &temperature);
		if (ret) {
			dev_warn_ratelimited(dev,
					     "failed to read temp for ddr thermal zone: %d\n",
					     ret);
			temperature = FALLBACK_STATIC_TEMPERATURE;
		}
	} else {
		temperature = FALLBACK_STATIC_TEMPERATURE;
	}

	/*
	 * Calculate the temperature scaling factor. To be applied to the
	 * voltage scaled power.
	 */
	temp = temperature / 1000;
	temp_squared = temp * temp;
	temp_cubed = temp_squared * temp;
	temp_scaling_factor = (dmcfreq->ts[3] * temp_cubed)
	    + (dmcfreq->ts[2] * temp_squared)
	    + (dmcfreq->ts[1] * temp)
	    + dmcfreq->ts[0];

	return (((dmcfreq->static_coefficient * voltage_cubed) >> 20)
		* temp_scaling_factor) / 1000000;
}

static struct devfreq_cooling_power ddr_cooling_power_data = {
	.get_static_power = model_static_power,
	.dyn_power_coeff = 120,
};

static int ddr_power_model_simple_init(struct rockchip_dmcfreq *dmcfreq)
{
	struct device_node *power_model_node;
	const char *tz_name;
	u32 temp;

	power_model_node = of_get_child_by_name(dmcfreq->dev->of_node,
						"ddr_power_model");
	if (!power_model_node) {
		dev_err(dmcfreq->dev, "could not find power_model node\n");
		return -ENODEV;
	}

	if (of_property_read_string(power_model_node, "thermal-zone", &tz_name)) {
		dev_err(dmcfreq->dev, "ts in power_model not available\n");
		return -EINVAL;
	}

	dmcfreq->ddr_tz = thermal_zone_get_zone_by_name(tz_name);
	if (IS_ERR(dmcfreq->ddr_tz)) {
		pr_warn_ratelimited
		    ("Error getting ddr thermal zone (%ld), not yet ready?\n",
		     PTR_ERR(dmcfreq->ddr_tz));
		dmcfreq->ddr_tz = NULL;

		return -EPROBE_DEFER;
	}

	if (of_property_read_u32(power_model_node, "static-power-coefficient",
				 &dmcfreq->static_coefficient)) {
		dev_err(dmcfreq->dev,
			"static-power-coefficient not available\n");
		return -EINVAL;
	}
	if (of_property_read_u32(power_model_node, "dynamic-power-coefficient",
				 &temp)) {
		dev_err(dmcfreq->dev,
			"dynamic-power-coefficient not available\n");
		return -EINVAL;
	}
	ddr_cooling_power_data.dyn_power_coeff = (unsigned long)temp;

	if (of_property_read_u32_array
	    (power_model_node, "ts", (u32 *)dmcfreq->ts, 4)) {
		dev_err(dmcfreq->dev, "ts in power_model not available\n");
		return -EINVAL;
	}

	return 0;
}

static void
rockchip_dmcfreq_register_cooling_device(struct rockchip_dmcfreq *dmcfreq)
{
	int ret;

	ret = ddr_power_model_simple_init(dmcfreq);
	if (ret)
		return;
	dmcfreq->devfreq_cooling =
		of_devfreq_cooling_register_power(dmcfreq->dev->of_node,
						  dmcfreq->devfreq,
						  &ddr_cooling_power_data);
	if (IS_ERR_OR_NULL(dmcfreq->devfreq_cooling)) {
		ret = PTR_ERR(dmcfreq->devfreq_cooling);
		dev_err(dmcfreq->dev,
			"Failed to register cooling device (%d)\n",
			ret);
	}
}

static int rockchip_dmcfreq_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rockchip_dmcfreq *data;
	int ret;

	data = devm_kzalloc(dev, sizeof(struct rockchip_dmcfreq), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->dev = dev;
	mutex_init(&data->lock);
	INIT_LIST_HEAD(&data->video_info_list);

	ret = rockchip_dmcfreq_get_event(data);
	if (ret)
		return ret;

	ret = rockchip_dmcfreq_power_control(data);
	if (ret)
		return ret;

	ret = rockchip_dmcfreq_dmc_init(pdev, data);
	if (ret)
		return ret;

	ret = rockchip_init_opp_table(dev, NULL, "ddr_leakage", "center");
	if (ret)
		return ret;

	rockchip_dmcfreq_parse_dt(data);
	if (!data->system_status_en && !data->auto_freq_en) {
		dev_info(dev, "don't add devfreq feature\n");
		return rockchip_dmcfreq_set_volt_only(data);
	}

	pm_qos_add_request(&pm_qos, PM_QOS_CPU_DMA_LATENCY,
			   PM_QOS_DEFAULT_VALUE);
	platform_set_drvdata(pdev, data);

	ret = devfreq_add_governor(&devfreq_dmc_ondemand);
	if (ret)
		return ret;
	ret = rockchip_dmcfreq_enable_event(data);
	if (ret)
		return ret;
	ret = rockchip_dmcfreq_add_devfreq(data);
	if (ret) {
		rockchip_dmcfreq_disable_event(data);
		return ret;
	}

	rockchip_dmcfreq_register_notifier(data);
	rockchip_dmcfreq_add_interface(data);
	rockchip_dmcfreq_boost_init(data);
	rockchip_dmcfreq_msch_rl_init(data);
	rockchip_dmcfreq_register_cooling_device(data);

	rockchip_set_system_status(SYS_STATUS_NORMAL);

	return 0;
}

static __maybe_unused int rockchip_dmcfreq_suspend(struct device *dev)
{
	struct rockchip_dmcfreq *dmcfreq = dev_get_drvdata(dev);
	int ret = 0;

	if (!dmcfreq)
		return 0;

	ret = rockchip_dmcfreq_disable_event(dmcfreq);
	if (ret)
		return ret;

	ret = devfreq_suspend_device(dmcfreq->devfreq);
	if (ret < 0) {
		dev_err(dev, "failed to suspend the devfreq devices\n");
		return ret;
	}

	return 0;
}

static __maybe_unused int rockchip_dmcfreq_resume(struct device *dev)
{
	struct rockchip_dmcfreq *dmcfreq = dev_get_drvdata(dev);
	int ret = 0;

	if (!dmcfreq)
		return 0;

	ret = rockchip_dmcfreq_enable_event(dmcfreq);
	if (ret)
		return ret;

	ret = devfreq_resume_device(dmcfreq->devfreq);
	if (ret < 0) {
		dev_err(dev, "failed to resume the devfreq devices\n");
		return ret;
	}
	return ret;
}

static SIMPLE_DEV_PM_OPS(rockchip_dmcfreq_pm, rockchip_dmcfreq_suspend,
			 rockchip_dmcfreq_resume);
static struct platform_driver rockchip_dmcfreq_driver = {
	.probe	= rockchip_dmcfreq_probe,
	.driver = {
		.name	= "rockchip-dmc",
		.pm	= &rockchip_dmcfreq_pm,
		.of_match_table = rockchip_dmcfreq_of_match,
	},
};
module_platform_driver(rockchip_dmcfreq_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Lin Huang <hl@rock-chips.com>");
MODULE_DESCRIPTION("rockchip dmcfreq driver with devfreq framework");
