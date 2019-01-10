/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __KIRIN_DRM_DSI_H__
#define __KIRIN_DRM_DSI_H__

#include <linux/clk.h>
#include <linux/component.h>
#include <linux/of_graph.h>
#include <linux/iopoll.h>
#include <video/mipi_display.h>
#include <linux/gpio/consumer.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>

#include <drm/drm_of.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_encoder_slave.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_panel.h>

#define ROUND(x, y) ((x) / (y) + ((x) % (y) * 10 / (y) >= 5 ? 1 : 0))
#define PHY_REF_CLK_RATE 19200000
#define PHY_REF_CLK_PERIOD_PS (1000000000 / (PHY_REF_CLK_RATE / 1000))

#define encoder_to_dsi(encoder) container_of(encoder, struct dw_dsi, encoder)
#define host_to_dsi(host) container_of(host, struct dw_dsi, host)
#define connector_to_dsi(connector)                                            \
	container_of(connector, struct dw_dsi, connector)

enum dsi_output_client { OUT_HDMI = 0, OUT_PANEL, OUT_MAX };

struct dsi_phy_range {
	u32 min_range_kHz;
	u32 max_range_kHz;
	u32 pll_vco_750M;
	u32 hstx_ckg_sel;
};

static const struct dsi_phy_range dphy_range_info[] = {
	{   46875,    62500,   1,    7 },
	{   62500,    93750,   0,    7 },
	{   93750,   125000,   1,    6 },
	{  125000,   187500,   0,    6 },
	{  187500,   250000,   1,    5 },
	{  250000,   375000,   0,    5 },
	{  375000,   500000,   1,    4 },
	{  500000,   750000,   0,    4 },
	{  750000,  1000000,   1,    0 },
	{ 1000000,  1500000,   0,    0 }
};

struct dsi_hw_ctx {
	void __iomem *base;
	char __iomem *peri_crg_base;

	struct clk *pclk;
	struct clk *dss_dphy0_ref_clk;
	struct clk *dss_dphy1_ref_clk;
	struct clk *dss_dphy0_cfg_clk;
	struct clk *dss_dphy1_cfg_clk;
	struct clk *dss_pclk_dsi0_clk;
	struct clk *dss_pclk_dsi1_clk;
};

struct mipi_panel_info {
	u8 dsi_version;
	u8 vc;
	u8 lane_nums;
	u8 lane_nums_select_support;
	u8 color_mode;
	u32 dsi_bit_clk; /* clock lane(p/n) */
	u32 burst_mode;
	u32 max_tx_esc_clk;
	u8 non_continue_en;

	u32 dsi_bit_clk_val1;
	u32 dsi_bit_clk_val2;
	u32 dsi_bit_clk_val3;
	u32 dsi_bit_clk_val4;
	u32 dsi_bit_clk_val5;
	u32 dsi_bit_clk_upt;
	/*uint32_t dsi_pclk_rate;*/

	u32 hs_wr_to_time;

	/* dphy config parameter adjust*/
	u32 clk_post_adjust;
	u32 clk_pre_adjust;
	u32 clk_pre_delay_adjust;
	u32 clk_t_hs_exit_adjust;
	u32 clk_t_hs_trial_adjust;
	u32 clk_t_hs_prepare_adjust;
	int clk_t_lpx_adjust;
	u32 clk_t_hs_zero_adjust;
	u32 data_post_delay_adjust;
	int data_t_lpx_adjust;
	u32 data_t_hs_prepare_adjust;
	u32 data_t_hs_zero_adjust;
	u32 data_t_hs_trial_adjust;
	u32 rg_vrefsel_vcm_adjust;

	/*only for Chicago<3660> use*/
	u32 rg_vrefsel_vcm_clk_adjust;
	u32 rg_vrefsel_vcm_data_adjust;
};

struct mipi_phy_params {
	u32 clk_t_lpx;
	u32 clk_t_hs_prepare;
	u32 clk_t_hs_zero;
	u32 clk_t_hs_trial;
	u32 clk_t_wakeup;
	u32 data_t_lpx;
	u32 data_t_hs_prepare;
	u32 data_t_hs_zero;
	u32 data_t_hs_trial;
	u32 data_t_ta_go;
	u32 data_t_ta_get;
	u32 data_t_wakeup;
	u32 hstx_ckg_sel;
	u32 pll_fbd_div5f;
	u32 pll_fbd_div1f;
	u32 pll_fbd_2p;
	u32 pll_enbwt;
	u32 pll_fbd_p;
	u32 pll_fbd_s;
	u32 pll_pre_div1p;
	u32 pll_pre_p;
	u32 pll_vco_750M;
	u32 pll_lpf_rs;
	u32 pll_lpf_cs;
	u32 clk_division;
	/********for hikey620************/
	u32 clklp2hs_time;
	u32 clkhs2lp_time;
	u32 lp2hs_time;
	u32 hs2lp_time;
	u32 clk_to_data_delay;
	u32 data_to_clk_delay;
	u32 lane_byte_clk_kHz;
	/*****************/

	/****for hikey960*****/
	u64 lane_byte_clk;

	u32 clk_lane_lp2hs_time;
	u32 clk_lane_hs2lp_time;
	u32 data_lane_lp2hs_time;
	u32 data_lane_hs2lp_time;
	u32 clk2data_delay;
	u32 data2clk_delay;

	u32 clk_pre_delay;
	u32 clk_post_delay;
	u32 data_pre_delay;
	u32 data_post_delay;
	u32 phy_stop_wait_time;
	u32 rg_vrefsel_vcm;

	u32 rg_pll_enswc;
	u32 rg_pll_chp;

	u32 pll_register_override;		/*0x1E[0]*/
	u32 pll_power_down;			/*0x1E[1]*/
	u32 rg_band_sel;				/*0x1E[2]*/
	u32 rg_phase_gen_en;		/*0x1E[3]*/
	u32 reload_sel;				/*0x1E[4]*/
	u32 rg_pll_cp_p;				/*0x1E[7:5]*/
	u32 rg_pll_refsel;				/*0x16[1:0]*/
	u32 rg_pll_cp;				/*0x16[7:5]*/
	u32 load_command;
	/*********/
};

struct ldi_panel_info {
	u32 h_back_porch;
	u32 h_front_porch;
	u32 h_pulse_width;

	/*
	 * note: vbp > 8 if used overlay compose,
	 * also lcd vbp > 8 in lcd power on sequence
	 */
	u32 v_back_porch;
	u32 v_front_porch;
	u32 v_pulse_width;

	u8 hsync_plr;
	u8 vsync_plr;
	u8 pixelclk_plr;
	u8 data_en_plr;

	/* for cabc */
	u8 dpi0_overlap_size;
	u8 dpi1_overlap_size;
};

struct dw_dsi_client {
	u32 lanes;
	u32 phy_clock; /* in kHz */
	enum mipi_dsi_pixel_format format;
	unsigned long mode_flags;
};

struct dw_dsi {
	struct drm_encoder encoder;
	struct drm_bridge *bridge;
	struct drm_panel *panel;
	struct mipi_dsi_host host;
	struct drm_connector connector; /* connector for panel */
	struct drm_display_mode cur_mode;
	struct dsi_hw_ctx *ctx;
	struct mipi_phy_params phy;
	struct mipi_panel_info mipi;
	struct ldi_panel_info ldi;
	u32 lanes;
	enum mipi_dsi_pixel_format format;
	unsigned long mode_flags;
	struct gpio_desc *gpio_mux;
	struct dw_dsi_client client[OUT_MAX];
	enum dsi_output_client cur_client;
	bool enable;
};

struct dsi_data {
	struct dw_dsi dsi;
	struct dsi_hw_ctx ctx;
};

enum kirin_dsi_version {
	KIRIN620_DSI = 0,
	KIRIN960_DSI
};

/* display controller init/cleanup ops */
struct kirin_dsi_ops {
	enum kirin_dsi_version version;
	int (*parse_dt)(struct platform_device *pdev, struct dw_dsi *dsi);
	int (*host_init)(struct device *dev, struct dw_dsi *dsi);
	void (*encoder_enable)(struct drm_encoder *encoder);
	enum drm_mode_status (*encoder_valid)(
		struct drm_encoder *encoder,
		const struct drm_display_mode *mode);
};

#ifdef CONFIG_DRM_HISI_KIRIN960
extern const struct kirin_dsi_ops kirin_dsi_960;
#endif
#ifdef CONFIG_DRM_HISI_KIRIN620
extern const struct kirin_dsi_ops kirin_dsi_620;
#endif

#endif /* __KIRIN_DRM_DSI_H__ */
