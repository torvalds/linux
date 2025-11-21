/* SPDX-License-Identifier: MIT */
/* Copyright © 2025 Intel Corporation */

#ifndef __INTEL_DSI_VBT_DEFS_H__
#define __INTEL_DSI_VBT_DEFS_H__

#include <linux/types.h>

/*
 * MIPI Sequence Block definitions
 *
 * Note the VBT spec has AssertReset / DeassertReset swapped from their
 * usual naming, we use the proper names here to avoid confusion when
 * reading the code.
 */
enum mipi_seq {
	MIPI_SEQ_END = 0,
	MIPI_SEQ_DEASSERT_RESET,	/* Spec says MipiAssertResetPin */
	MIPI_SEQ_INIT_OTP,
	MIPI_SEQ_DISPLAY_ON,
	MIPI_SEQ_DISPLAY_OFF,
	MIPI_SEQ_ASSERT_RESET,		/* Spec says MipiDeassertResetPin */
	MIPI_SEQ_BACKLIGHT_ON,		/* sequence block v2+ */
	MIPI_SEQ_BACKLIGHT_OFF,		/* sequence block v2+ */
	MIPI_SEQ_TEAR_ON,		/* sequence block v2+ */
	MIPI_SEQ_TEAR_OFF,		/* sequence block v3+ */
	MIPI_SEQ_POWER_ON,		/* sequence block v3+ */
	MIPI_SEQ_POWER_OFF,		/* sequence block v3+ */
	MIPI_SEQ_MAX
};

enum mipi_seq_element {
	MIPI_SEQ_ELEM_END = 0,
	MIPI_SEQ_ELEM_SEND_PKT,
	MIPI_SEQ_ELEM_DELAY,
	MIPI_SEQ_ELEM_GPIO,
	MIPI_SEQ_ELEM_I2C,		/* sequence block v2+ */
	MIPI_SEQ_ELEM_SPI,		/* sequence block v3+ */
	MIPI_SEQ_ELEM_PMIC,		/* sequence block v3+ */
	MIPI_SEQ_ELEM_MAX
};

#define MIPI_DSI_UNDEFINED_PANEL_ID	0
#define MIPI_DSI_GENERIC_PANEL_ID	1

struct mipi_config {
	u16 panel_id;

	/* General Params */
	struct {
		u32 enable_dithering:1;
		u32 rsvd1:1;
		u32 is_bridge:1;

		u32 panel_arch_type:2;
		u32 is_cmd_mode:1;

#define NON_BURST_SYNC_PULSE	0x1
#define NON_BURST_SYNC_EVENTS	0x2
#define BURST_MODE		0x3
		u32 video_transfer_mode:2;

		u32 cabc_supported:1;
#define PPS_BLC_PMIC   0
#define PPS_BLC_SOC    1
		u32 pwm_blc:1;

#define PIXEL_FORMAT_RGB565			0x1
#define PIXEL_FORMAT_RGB666			0x2
#define PIXEL_FORMAT_RGB666_LOOSELY_PACKED	0x3
#define PIXEL_FORMAT_RGB888			0x4
		u32 videomode_color_format:4;

#define ENABLE_ROTATION_0	0x0
#define ENABLE_ROTATION_90	0x1
#define ENABLE_ROTATION_180	0x2
#define ENABLE_ROTATION_270	0x3
		u32 rotation:2;
		u32 bta_disable:1;
		u32 rsvd2:15;
	} __packed;

	/* Port Desc */
	struct {
#define DUAL_LINK_NOT_SUPPORTED	0
#define DUAL_LINK_FRONT_BACK	1
#define DUAL_LINK_PIXEL_ALT	2
		u16 dual_link:2;
		u16 lane_cnt:2;
		u16 pixel_overlap:3;
		u16 rgb_flip:1;
#define DL_DCS_PORT_A			0x00
#define DL_DCS_PORT_C			0x01
#define DL_DCS_PORT_A_AND_C		0x02
		u16 dl_dcs_cabc_ports:2;
		u16 dl_dcs_backlight_ports:2;
		u16 port_sync:1;				/* 219-230 */
		u16 rsvd3:3;
	} __packed;

	/* DSI Controller Parameters */
	struct {
		u16 dsi_usage:1;
		u16 rsvd4:15;
	} __packed;

	u8 rsvd5;
	u32 target_burst_mode_freq;
	u32 dsi_ddr_clk;
	u32 bridge_ref_clk;

	/* LP Byte Clock */
	struct {
#define  BYTE_CLK_SEL_20MHZ		0
#define  BYTE_CLK_SEL_10MHZ		1
#define  BYTE_CLK_SEL_5MHZ		2
		u8 byte_clk_sel:2;
		u8 rsvd6:6;
	} __packed;

	/* DPhy Flags */
	struct {
		u16 dphy_param_valid:1;
		u16 eot_pkt_disabled:1;
		u16 enable_clk_stop:1;
		u16 blanking_packets_during_bllp:1;		/* 219+ */
		u16 lp_clock_during_lpm:1;			/* 219+ */
		u16 rsvd7:11;
	} __packed;

	u32 hs_tx_timeout;
	u32 lp_rx_timeout;
	u32 turn_around_timeout;
	u32 device_reset_timer;
	u32 master_init_timer;
	u32 dbi_bw_timer;
	u32 lp_byte_clk_val;

	/*  DPhy Params */
	struct {
		u32 prepare_cnt:6;
		u32 rsvd8:2;
		u32 clk_zero_cnt:8;
		u32 trail_cnt:5;
		u32 rsvd9:3;
		u32 exit_zero_cnt:6;
		u32 rsvd10:2;
	} __packed;

	u32 clk_lane_switch_cnt;
	u32 hl_switch_cnt;

	u32 rsvd11[6];

	/* timings based on dphy spec */
	u8 tclk_miss;
	u8 tclk_post;
	u8 rsvd12;
	u8 tclk_pre;
	u8 tclk_prepare;
	u8 tclk_settle;
	u8 tclk_term_enable;
	u8 tclk_trail;
	u16 tclk_prepare_clkzero;
	u8 rsvd13;
	u8 td_term_enable;
	u8 teot;
	u8 ths_exit;
	u8 ths_prepare;
	u16 ths_prepare_hszero;
	u8 rsvd14;
	u8 ths_settle;
	u8 ths_skip;
	u8 ths_trail;
	u8 tinit;
	u8 tlpx;
	u8 rsvd15[3];

	/* GPIOs */
	u8 panel_enable;
	u8 bl_enable;
	u8 pwm_enable;
	u8 reset_r_n;
	u8 pwr_down_r;
	u8 stdby_r_n;
} __packed;

/* all delays have a unit of 100us */
struct mipi_pps_data {
	u16 panel_on_delay;
	u16 bl_enable_delay;
	u16 bl_disable_delay;
	u16 panel_off_delay;
	u16 panel_power_cycle_delay;
} __packed;

#endif /* __INTEL_DSI_VBT_DEFS_H__ */
