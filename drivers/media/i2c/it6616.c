// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 Rockchip Electronics Co. Ltd.
 *
 * it6616 HDMI to MIPI CSI-2 bridge driver.
 *
 * Author: Jau-Chih.Tseng@ite.com.tw
 *	   Jianwei Fan <jianwei.fan@rock-chips.com>
 * V0.0X01.0X00 first version.
 *
 */
// #define DEBUG
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/hdmi.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_graph.h>
#include <linux/rk-camera-module.h>
#include <linux/slab.h>
#include <linux/timer.h>
#include <linux/v4l2-dv-timings.h>
#include <linux/version.h>
#include <linux/videodev2.h>
#include <linux/workqueue.h>
#include <linux/compat.h>
#include <linux/regmap.h>
#include <media/v4l2-controls_rockchip.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-dv-timings.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fwnode.h>
#include <media/cec.h>

#define DRIVER_VERSION			KERNEL_VERSION(0, 0x01, 0x00)

static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "debug level (0-3)");

#define IT6616_NAME		"IT6616"
#define POLL_INTERVAL_MS	100
#define TIMER_100MS		105
#define IT6616_XVCLK_FREQ	27000000

#define IT6616_LINK_FREQ	400000000
#define IT6616_PIXEL_RATE	400000000
#define IT6616_MEDIA_BUS_FMT	MEDIA_BUS_FMT_UYVY8_2X8

#define I2C_ADR_HDMI            0x90
#define I2C_ADR_MIPI            0xAC
#define I2C_ADR_EDID            0xA8

//define in HDMI SPEC 2.0 PAGE 84
#define AUDIO_SAMPLING_1024K    0x35
#define AUDIO_SAMPLING_768K     0x09
#define AUDIO_SAMPLING_512K     0x3B
#define AUDIO_SAMPLING_384K     0x05
#define AUDIO_SAMPLING_256K     0x1B
#define AUDIO_SAMPLING_192K     0x0E
#define AUDIO_SAMPLING_176P4K   0x0C
#define AUDIO_SAMPLING_128K     0x2B
#define AUDIO_SAMPLING_96K      0x0A
#define AUDIO_SAMPLING_88P2K    0x08
#define AUDIO_SAMPLING_64K      0x0B
#define AUDIO_SAMPLING_48K      0x02
#define AUDIO_SAMPLING_44P1K    0x00
#define AUDIO_SAMPLING_32K      0x03

#define REG_RX_AVI_HB1          0x13
#define REG_RX_AVI_HB2          0x12
#define REG_RX_AVI_DB0          0x14
#define REG_RX_AVI_DB1          0x15
#define REG_RX_AVI_DB2          0x16
#define REG_RX_AVI_DB3          0x17
#define REG_RX_AVI_DB4          0x18
#define REG_RX_AVI_DB5          0x19
#define REG_RX_AVI_DB6          0x1A
#define REG_RX_AVI_DB7          0x1B
#define REG_RX_AVI_DB8          0x1C
#define REG_RX_AVI_DB9          0x1D
#define REG_RX_AVI_DB10         0x1E
#define REG_RX_AVI_DB11         0x1F
#define REG_RX_AVI_DB12         0x20
#define REG_RX_AVI_DB13         0x21
#define REG_RX_AVI_DB14         0x22
#define REG_RX_AVI_DB15         0x23

#define DP_REG_INT_STS_07       0x07
#define DP_REG_INT_STS_08       0x08
#define DP_REG_INT_STS_09       0x09
#define DP_REG_INT_STS_0A       0x0A
#define DP_REG_INT_STS_0B       0x0B
#define DP_REG_INT_STS_0C       0x0C
#define DP_REG_INT_STS_0D       0x0D
#define DP_REG_INT_STS_0E       0x0E
#define DP_REG_INT_STS_0F       0x0F
#define DP_REG_INT_STS_2B       0x2B
#define DP_REG_INT_STS_2C       0x2C
#define DP_REG_INT_STS_2D       0x2D

#define DP_REG_INT_MASK_07      0xD1
#define DP_REG_INT_MASK_08      0xD2
#define DP_REG_INT_MASK_09      0xD3
#define DP_REG_INT_MASK_0A      0xD4
#define DP_REG_INT_MASK_0B      0xD5
#define DP_REG_INT_MASK_0C      0xD6
#define DP_REG_INT_MASK_0D      0xD7
#define DP_REG_INT_MASK_0E      0xD8
#define DP_REG_INT_MASK_0F      0xD9
#define DP_REG_INT_MASK_10      0xDA
#define DP_REG_INT_MASK_11      0xDB
#define DP_REG_INT_MASK_2D      0xDC
#define DP_REG_INT_MASK_2C      0xDD
#define DP_REG_INT_MASK_2B      0xDE

#define BANK                    0x0F
#define BANKM                   0x07
#define FROM_CONFIG             0xFF

#define AUDIO_I2S_JUSTIFIED			AUDIO_I2S_MODE
// #define MIPI_TX_INTERFACE MIPI_DSI
#define MIPI_TX_INTERFACE			MIPI_CSI
#define MIPI_TX_LANE_SWAP			false
#define MIPI_TX_PN_SWAP				false
// #define MIPI_TX_DATA_TYPE DSI_RGB_24b
#define MIPI_TX_DATA_TYPE                       CSI_YCbCr4228b
#define MIPI_TX_ENABLE_AUTO_ADJUST_LANE_COUNT   false
/* HDMI_RX_VIDEO_STABLE_CONDITION_V_FRAME,
 * HDMI_RX_VIDEO_STABLE_CONDITION_CLOCK,
 * HDMI_RX_VIDEO_STABLE_CONDITION_H_LINE
 */
#define HDMI_RX_VIDEO_STABLE_CONDITION		HDMI_RX_VIDEO_STABLE_CONDITION_V_FRAME
/* MIPI_TX_NON_CONTINUOUS_CLOCK, MIPI_TX_CONTINUOUS_CLOCK */
#define MIPI_TX_ENABLE_CONTINUOUS_CLOCK		MIPI_TX_CONTINUOUS_CLOCK
#define MIPI_TX_ENABLE_DSI_SYNC_EVENT		false
#define MIPI_TX_ENABLE_DSI_EOTP_PACKET		false
#define MIPI_TX_ENABLE_INITIAL_FIRE_LP_CMD	true
#define DEFAULT_RS_LEVEL			0x9F
#define MIPI_TX_ENABLE_MANUAL_ADJUSTED_D_PHY	false
#define MIPI_TX_LPX				2
#define MIPI_TX_HS_PREPARE			0x02
#define MIPI_TX_HS_PREPARE_ZERO			0x04
#define MIPI_TX_HS_TRAIL			0x07

#define MAX_AUDIO_SAMPLING_FREQ_ERROR_COUNT	15
#define HDMI_RX_DISABLE_PIXEL_REPEAT		true
#define MIPI_TX_LANE_ADJUST_THRESHOLD		30
#define MIPI_TX_V_LPM_LENGTH			0x200
#define MIPI_TX_H_LPM_LENGTH			0x80
#define MIPI_TX_ENABLE_H_ENTER_LPM		false
#define MIPI_TX_ENABLE_HS_PRE_1T		true
#define MIPI_TX_ENABLE_PCLK_INV			false
#define MIPI_TX_ENABLE_MCLK_INV			true
#define MIPI_TX_ENABLE_BY_PASS			true
#define MIPI_TX_ENABLE_H_FIRE_PACKET		false
#define HDMI_RX_ENABLE_COLOR_UP_DN_FILTER	true
#define HDMI_RX_ENABLE_DITHER_FUNCTION		false
#define HDMI_RX_ENABLE_DITHER_FCNT_FUNCTION	false
#define HDMI_RX_COLOR_CLIP			true
#define HDMI_RX_CRCB_LIMIT			false
#define HDMI_RX_QUANT_4LB			true
#define HDMI_RX_AUTO_CSC_SELECT			false
#define LP_CMD_FIFO_SIZE			128

enum csc_matrix_type {
	CSCMtx_RGB2YUV_ITU601_16_235,
	CSCMtx_RGB2YUV_ITU601_00_255,
	CSCMtx_RGB2YUV_ITU709_16_235,
	CSCMtx_RGB2YUV_ITU709_00_255,
	CSCMtx_YUV2RGB_ITU601_16_235,
	CSCMtx_YUV2RGB_ITU601_00_255,
	CSCMtx_YUV2RGB_ITU709_16_235,
	CSCMtx_YUV2RGB_ITU709_00_255,
	CSCMtx_YUV2RGB_BT2020_00_255,
	CSCMtx_RGB_00_255_RGB_16_235,
	CSCMtx_RGB_16_235_RGB_00_255,
	CSCMtx_Unknown,
};

enum {
	MIPI_CSI,
	MIPI_DSI,
};

enum {
	VSC_COLOR_RGB = 0x00,
	VSC_COLOR_YUV444 = 0x01,
	VSC_COLOR_YUV422 = 0x02,
	VSC_COLOR_YUV420 = 0x03,
	VSC_COLOR_YONLY,
	VSC_COLOR_RAW,
	VSC_COLOR_RESERVE
};

enum {
	COLOR_RGB = 0x00,
	COLOR_YUV422 = 0x01,
	COLOR_YUV444 = 0x02,
	COLOR_YUV420 = 0x03,
	COLOR_RESERVE
};

enum {
	COLORIMETRY_BT601 = 0x00,
	COLORIMETRY_BT709 = 0x01,
	COLORIMETRY_xvYCC601 = 0x02,
	COLORIMETRY_xvYCC709 = 0x03,
	COLORIMETRY_sYCC601 = 0x04,
	COLORIMETRY_aYCC601 = 0x05,
	COLORIMETRY_BT2020YcCbcCrc = 0x06,
	COLORIMETRY_BT2020YCbCr = 0x07,
	COLORIMETRY_RESERVE
};

enum {
	COLORIMETRY_sRGB = 0x00,
	COLORIMETRY_fixRGB = 0x01,
	COLORIMETRY_scRGB = 0x02,
	COLORIMETRY_aRGB = 0x03,
	COLORIMETRY_DCIP3 = 0x04,
	COLORIMETRY_CUSTOM = 0x05,
	COLORIMETRY_BT2020RGB = 0x06

};

enum mipi_csi_data {
	CSI_RGB10b = 0x25,
	CSI_RGB888 = 0x24,
	CSI_RGB666 = 0x23,
	CSI_RGB565 = 0x22,
	CSI_RGB555 = 0x21,
	CSI_RGB444 = 0x20,
	CSI_YCbCr4208b = 0x1A,
	CSI_YCbCr4228b = 0x1E,
	CSI_YCbCr42210b = 0x1F,
	CSI_YCbCr42212b = 0x30
};

enum mipi_dsi_data {
	DSI_RGB_24b = 0x3E,
	DSI_RGB_30b = 0x0D,
	DSI_RGB_36b = 0x1D,
	DSI_RGB_18b = 0x1E,
	DSI_RGB_18b_L = 0x2E,
	DSI_YCbCr_16b = 0x2C,
	DSI_YCbCr_20b = 0x0C,
	DSI_YCbCr_24b = 0x1C
};

enum csc_select {
	CSC_BYPASS =  0x00,
	CSC_RGB2YUV = 0x02,
	CSC_YUV2RGB = 0x03,
};

enum av_mute_state {
	AV_MUTE_OFF,
	AV_MUTE_ON,
};

enum {
	AUDIO_OFF = 0x00,
	AUDIO_I2S = 0x01,
	AUDIO_SPDIF = 0x02,
};

enum {
	AUDIO_I2S_MODE,
	AUDIO_RIGHT_JUSTIFIED,
	AUDIO_LEFT_JUSTIFIED,
};

enum {
	MIPI_TX_NON_CONTINUOUS_CLOCK,
	MIPI_TX_CONTINUOUS_CLOCK,
};

enum {
	HDMI_RX_VIDEO_STABLE_CONDITION_V_FRAME,
	HDMI_RX_VIDEO_STABLE_CONDITION_CLOCK,
	HDMI_RX_VIDEO_STABLE_CONDITION_H_LINE,
};

enum dcs_cmd_name {
	ENTER_SLEEP_MODE,
	SET_DISPLAY_OFF,
	EXIT_SLEEP_MODE,
	SET_DISPLAY_ON,
	GET_DISPLAY_MODE,
	LONG_WRITE_CMD,
	LONG_WRITE_CMD1,
	DELAY,
};

enum mipi_lp_cmd_type {
	LP_CMD_LPDT = 0x87,
	LP_CMD_BTA = 0xFF,
};

enum mipi_packet_size {
	SHORT_PACKET,
	LONG_PACKET,
	UNKNOWN_PACKET,
};

enum mipi_tx_lp_cmd_header {
	NO_HEADER,
	CALC_HEADER,
};

static const s64 link_freq_menu_items[] = {
	IT6616_LINK_FREQ,
};

struct it6616_reg_set {
	u8 ucAddr;
	u8 andmask;
	u8 ucValue;
};

struct bus_config {
	u8 lane;
	u8 type;
	u8 reg23_p2m;
	u8 regb0_div[3];
};

union tx_p2m_delay {
	u8 tx_dsi_vsync_delay;
	u8 tx_csi_p2m_delay;
};

struct bus_para {
	struct bus_config cfg;
	u8 swap_pn;
	u8 swap_lan;
	u8 pclk_inv;
	u8 mclk_inv;
	u8 lpx_num;
	u8 mipi_tx_hs_prepare;
	u8 tx_sel_line_start;
	u8 tx_bypass;
	u8 tx_enable_hs_pre_1T;
	u8 tx_enable_h_enter_lpm;
	u8 tx_vsync_delay;
	union tx_p2m_delay p2m_delay;
	u16 tx_vlpm_length;
	u16 tx_hlpm_length;
};

struct mipi_bus {
	u8 lane_cnt;
	u8 data_type;
	u8 bus_type;
	u32 mbus_fmt_code;
	struct bus_para bus_para_config;
};

struct mipi_packet_size_data_id_map {
	u8 data_id;
	enum mipi_packet_size packet_size;
};

struct mipi_packet_size_data_id_map packet_size_data_id_map[] = {
	{0x05, SHORT_PACKET},/* dcs short write without parameter */
	{0x15, SHORT_PACKET},/* dcs short write with one parameter */
	{0x23, SHORT_PACKET},/* generic short write, 2 parameters */
	{0x29, LONG_PACKET},/* generic long write */
	{0x39, LONG_PACKET},/* dcs long write */
	{0x06, SHORT_PACKET},
	{0x16, SHORT_PACKET},
	{0x37, SHORT_PACKET},
	{0x03, SHORT_PACKET},
	{0x13, SHORT_PACKET},
	{0x04, SHORT_PACKET},
	{0x14, SHORT_PACKET},
	{0x24, SHORT_PACKET}
};

struct mipi_packet {
	u8 data_id;
	u8 word_count_l;
	u8 word_count_h;
	u8 ecc;
};

struct dcs_setting_entry {
	enum dcs_cmd_name cmd_name;
	enum mipi_lp_cmd_type cmd;
	u8 data_id;
	u8 count;
	u8 para_list[LP_CMD_FIFO_SIZE];
};

static const struct dcs_setting_entry dcs_table[] = {
	{	/* command 0x10 with no parameter, checksum: 0x2C */
		ENTER_SLEEP_MODE, LP_CMD_LPDT, 0x05, 2, {0x10, 0x00}
	}, {	/* command 0x28 with no parameter, checksum: 0x06 */
		SET_DISPLAY_OFF, LP_CMD_LPDT, 0x05, 2, {0x28, 0x00}
	}, {	/* command 0x11 with no parameter, checksum: 0x36 */
		EXIT_SLEEP_MODE, LP_CMD_LPDT, 0x05, 2, {0x11, 0x00}
	}, {	/* command 0x29 with no parameter, checksum: 0x1C */
		SET_DISPLAY_ON, LP_CMD_LPDT, 0x05, 2, {0x29, 0x00}
	}, {	/* checksum: 0x1A */
		GET_DISPLAY_MODE, LP_CMD_LPDT, 0x06, 2, {0x0D, 0x00}
	}, {	/* command 0x50 with 2 parameters */
		LONG_WRITE_CMD, LP_CMD_LPDT, 0x39, 3, {0x50, 0x5A, 0x09}
	}, {	/* command 0x80 with 16 parameters */
		LONG_WRITE_CMD1, LP_CMD_LPDT, 0x29, 17,
		{0x80, 0x5A, 0x51, 0xB5, 0x2A, 0x6C, 0x35,
		0x4B, 0x01, 0x40, 0xE1, 0x0D, 0x82, 0x20, 0x08, 0x30, 0x03}
	}
};

static const int code_to_rate_table[] = {
	44100, 0, 48000, 32000, 0, 384000, 0, 0, 88200,
	768000, 96000, 64000, 176400, 0, 192000, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 256000, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 128000, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 1024000, 0, 0, 0, 0, 0, 512000
};

struct color_format {
	unsigned char color_mode;
	unsigned char color_depth;
	unsigned char color_cea_range;
	unsigned char color_colorietry;
	unsigned char color_ex_colorietry;
	unsigned char content_type;
};

struct video_info {
	u16 h_active;
	u16 v_active;
	u16 h_total;
	u16 v_total;
	u32 pclk;
	u32 TMDSCLK;
	u32 frame_rate;
	u16 h_front_porch;
	u16 h_sync_w;
	u16 h_back_porch;
	u16 v_front_porch;
	u16 v_sync_w;
	u16 v_back_porch;
	u16 interlaced;
	u16 v_sync_pol;
	u16 h_sync_pol;
	u16 ColorDepth;
	u16 VIC;
};

struct audio_info {
	u32 n;
	u32 cts;
	u8 channel_status;
	u32 sample_freq;
	u32 force_sample_freq;
};

struct it6616 {
	struct i2c_client *hdmi_i2c;
	struct i2c_client *mipi_i2c;
	struct i2c_client *edid_i2c;
	struct regmap *hdmi_regmap;
	struct regmap *mipi_regmap;
	struct regmap *edid_regmap;
	u8 attr_hdmi_reg_bank;
	struct class *hdmirx_class;
	struct device *dev;
	struct device *classdev;
	struct v4l2_fwnode_bus_mipi_csi2 bus;
	struct v4l2_subdev sd;
	struct media_pad pad;
	struct v4l2_ctrl_handler hdl;
	struct i2c_client *i2c_client;
	struct mutex confctl_mutex;
	struct v4l2_ctrl *detect_tx_5v_ctrl;
	struct v4l2_ctrl *audio_sampling_rate_ctrl;
	struct v4l2_ctrl *audio_present_ctrl;
	struct v4l2_ctrl *link_freq;
	struct v4l2_ctrl *pixel_rate;
	struct v4l2_dv_timings timings;
	struct clk *xvclk;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *power_gpio;
	struct delayed_work work_i2c_poll;
	const char *module_facing;
	const char *module_name;
	const char *len_name;
	const struct it6616_mode *cur_mode;
	bool nosignal;
	bool is_audio_present;
	u32 mbus_fmt_code;
	u8 csi_lanes_in_use;
	u32 module_index;
	u32 audio_sampling_rate;
	bool hdmi_rx_video_stable;
	bool hdmi_rx_hdcp_state;
	bool mipi_tx_video_stable;
	bool mipi_tx_enable_manual_adjusted_d_phy;
	u8 audio_stable;
	u8 audio_interface;
	u8 rs_level;
	struct mipi_bus mipi;
	struct color_format color_fmt;
	struct video_info vinfo;
	struct audio_info ainfo;
	u8 edid_data[256];
	u16 edid_len;
	u8 audio_sampling_freq_error_count;
	u8 audio_i2s_justified;
	u8 mipi_tx_enable_auto_adjust_lane_count;
	u8 mipi_tx_enable_h_fire_packet;
	u8 mipi_tx_enable_initial_fire_lp_cmd;
	u8 hdmi_rx_disable_pixel_repeat;
	bool mipi_tx_enable_continuous_clock;
	u8 mipi_tx_enable_mipi_output;
	u8 hdmi_rx_video_stable_condition;
	bool	power_on;
	u32 rclk;
	u32 tx_mclk;
	u32 tx_rclk;
	u32 tx_pclk;
	u32 tx_mclk_ps;
	struct hdmi_avi_infoframe avi_if;
	struct hdmi_avi_infoframe avi_if_prev;
	enum hdmi_colorspace output_colorspace;
};

static const struct v4l2_dv_timings_cap it6616_timings_cap = {
	.type = V4L2_DV_BT_656_1120,
	.reserved = { 0 },
	V4L2_INIT_BT_TIMINGS(1, 10000, 1, 10000, 0, 400000000,
			V4L2_DV_BT_STD_CEA861 | V4L2_DV_BT_STD_DMT |
			V4L2_DV_BT_STD_GTF | V4L2_DV_BT_STD_CVT,
			V4L2_DV_BT_CAP_PROGRESSIVE | V4L2_DV_BT_CAP_INTERLACED |
			V4L2_DV_BT_CAP_REDUCED_BLANKING |
			V4L2_DV_BT_CAP_CUSTOM)
};

struct it6616_mode {
	u32 width;
	u32 height;
	struct v4l2_fract max_fps;
	u32 hts_def;
	u32 vts_def;
	u32 exp_def;
};

static const struct it6616_mode supported_modes[] = {
	{
		.width = 3840,
		.height = 2160,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.hts_def = 4400,
		.vts_def = 2250,
	}, {
		.width = 1920,
		.height = 1080,
		.max_fps = {
			.numerator = 10000,
			.denominator = 600000,
		},
		.hts_def = 2200,
		.vts_def = 1125,
	}, {
		.width = 1920,
		.height = 540,
		.max_fps = {
			.numerator = 10000,
			.denominator = 600000,
		},
		.hts_def = 2200,
		.vts_def = 562,
	}, {
		.width = 1280,
		.height = 720,
		.max_fps = {
			.numerator = 10000,
			.denominator = 600000,
		},
		.hts_def = 1650,
		.vts_def = 750,
	}, {
		.width = 720,
		.height = 576,
		.max_fps = {
			.numerator = 10000,
			.denominator = 500000,
		},
		.hts_def = 864,
		.vts_def = 625,
	}, {
		.width = 720,
		.height = 480,
		.max_fps = {
			.numerator = 10000,
			.denominator = 600000,
		},
		.hts_def = 858,
		.vts_def = 525,
	}, {
		.width = 720,
		.height = 288,
		.max_fps = {
			.numerator = 10000,
			.denominator = 500000,
		},
		.hts_def = 864,
		.vts_def = 312,
	}, {
		.width = 720,
		.height = 240,
		.max_fps = {
			.numerator = 10000,
			.denominator = 600000,
		},
		.hts_def = 858,
		.vts_def = 262,
	},
};

static const u8 default_edid[] = {
	0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00,
	0x49, 0x78, 0x01, 0x88, 0x00, 0x88, 0x88, 0x88,
	0x1C, 0x1F, 0x01, 0x03, 0x80, 0x00, 0x00, 0x78,
	0x0A, 0x0D, 0xC9, 0xA0, 0x57, 0x47, 0x98, 0x27,
	0x12, 0x48, 0x4C, 0x00, 0x00, 0x00, 0x01, 0x01,
	0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
	0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x02, 0x3A,
	0x80, 0x18, 0x71, 0x38, 0x2D, 0x40, 0x58, 0x2C,
	0x45, 0x00, 0xC4, 0x8E, 0x21, 0x00, 0x00, 0x1E,
	0x01, 0x1D, 0x00, 0x72, 0x51, 0xD0, 0x1E, 0x20,
	0x6E, 0x28, 0x55, 0x00, 0xC4, 0x8E, 0x21, 0x00,
	0x00, 0x1E, 0x00, 0x00, 0x00, 0xFC, 0x00, 0x54,
	0x37, 0x34, 0x39, 0x2D, 0x66, 0x48, 0x44, 0x37,
	0x32, 0x30, 0x0A, 0x20, 0x00, 0x00, 0x00, 0xFD,
	0x00, 0x14, 0x78, 0x01, 0xFF, 0x1D, 0x00, 0x0A,
	0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x01, 0x64,

	0x02, 0x03, 0x1C, 0x71, 0x49, 0x90, 0x04, 0x02,
	0x5F, 0x11, 0x07, 0x05, 0x16, 0x22, 0x23, 0x09,
	0x07, 0x01, 0x83, 0x01, 0x00, 0x00, 0x65, 0x03,
	0x0C, 0x00, 0x10, 0x00, 0x8C, 0x0A, 0xD0, 0x8A,
	0x20, 0xE0, 0x2D, 0x10, 0x10, 0x3E, 0x96, 0x00,
	0x13, 0x8E, 0x21, 0x00, 0x00, 0x1E, 0xD8, 0x09,
	0x80, 0xA0, 0x20, 0xE0, 0x2D, 0x10, 0x10, 0x60,
	0xA2, 0x00, 0xC4, 0x8E, 0x21, 0x00, 0x00, 0x18,
	0x8C, 0x0A, 0xD0, 0x90, 0x20, 0x40, 0x31, 0x20,
	0x0C, 0x40, 0x55, 0x00, 0x48, 0x39, 0x00, 0x00,
	0x00, 0x18, 0x01, 0x1D, 0x80, 0x18, 0x71, 0x38,
	0x2D, 0x40, 0x58, 0x2C, 0x45, 0x00, 0xC0, 0x6C,
	0x00, 0x00, 0x00, 0x18, 0x01, 0x1D, 0x80, 0x18,
	0x71, 0x1C, 0x16, 0x20, 0x58, 0x2C, 0x25, 0x00,
	0xC0, 0x6C, 0x00, 0x00, 0x00, 0x18, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xB3,
};

static const struct bus_config it6616_csi_bus_cfg[] = {
	{4, CSI_RGB10b, 0x8F, {0xEE, 0xEE, 0xEE}},
	{4, CSI_RGB888, 0x23, {0x65, 0x22, 0x22}},
	{4, CSI_RGB666, 0x89, {0xE8, 0xE8, 0xE8}},
	{4, CSI_RGB565, 0x22, {0x63, 0x21, 0x00}},
	{4, CSI_RGB555, 0x22, {0x63, 0x21, 0x00}},
	{4, CSI_RGB444, 0x22, {0x63, 0x21, 0x00}},
	{4, CSI_YCbCr4208b, 0x23, {0x65, 0x22, 0x22}},
	{4, CSI_YCbCr4228b, 0x22, {0x63, 0x21, 0x00}},
	{4, CSI_YCbCr42210b, 0x45, {0x64, 0x64, 0x64}},
	{4, CSI_YCbCr42212b, 0x23, {0x65, 0x22, 0x22}},

	{2, CSI_RGB10b, 0x4F, {0x6E, 0x6E, 0x6E}},
	{2, CSI_RGB888, 0x13, {0x6B, 0x25, 0x25}},
	{2, CSI_RGB666, 0x49, {0x68, 0x68, 0x68}},
	{2, CSI_RGB565, 0x12, {0x67, 0x23, 0x01}},
	{2, CSI_RGB555, 0x12, {0x67, 0x23, 0x01}},
	{2, CSI_RGB444, 0x12, {0x67, 0x23, 0x01}},
	{2, CSI_YCbCr4208b, 0x13, {0x6B, 0x25, 0x25}},
	{2, CSI_YCbCr4228b, 0x12, {0x67, 0x23, 0x01}},
	{2, CSI_YCbCr42210b, 0x25, {0x69, 0x24, 0x24}},
	{2, CSI_YCbCr42212b, 0x13, {0x6B, 0x25, 0x25}},

	{1, CSI_RGB10b, 0x2F, {0x7D, 0x2E, 0x2E}},
	{1, CSI_RGB888, 0x03, {0x77, 0x2B, 0x05}},
	{1, CSI_RGB666, 0x29, {0x71, 0x28, 0x28}},
	{1, CSI_RGB565, 0x02, {0x6F, 0x27, 0x03}},
	{1, CSI_RGB555, 0x02, {0x6F, 0x27, 0x03}},
	{1, CSI_RGB444, 0x02, {0x6F, 0x27, 0x03}},
	{1, CSI_YCbCr4208b, 0x03, {0x77, 0x2B, 0x05}},
	{1, CSI_YCbCr4228b, 0x02, {0x6F, 0x27, 0x03}},
	{1, CSI_YCbCr42210b, 0x15, {0x73, 0x29, 0x04}},
	{1, CSI_YCbCr42212b,  0x03, {0x77, 0x2B, 0x05}},
	{0, 0, 0, {0, 0, 0}},
};

static const struct bus_config it6616_dsi_bus_cfg[] = {
	{4, DSI_RGB_36b, 0x19, {0x68, 0x68, 0x68}},
	{4, DSI_RGB_30b, 0x2F, {0xEE, 0xEE, 0xEE}},
	{4, DSI_RGB_24b, 0x03, {0x65, 0x22, 0x22}},
	{4, DSI_RGB_18b_L, 0x03, {0x65, 0x22, 0x22}},
	{4, DSI_RGB_18b, 0x29, {0xE8, 0xE8, 0xE8}},

	{2, DSI_RGB_36b, 0x19, {0x71, 0x28, 0x28}},
	{2, DSI_RGB_30b, 0x2F, {0x6E, 0x6E, 0x6E}},
	{2, DSI_RGB_24b, 0x03, {0x6B, 0x25, 0x25}},
	{2, DSI_RGB_18b_L, 0x03, {0x6B, 0x25, 0x25}},
	{2, DSI_RGB_18b, 0x29, {0x68, 0x68, 0x68}},

	{1, DSI_RGB_36b, 0x19, {0x31, 0x31, 0x08}},
	{1, DSI_RGB_30b, 0x2F, {0x7D, 0x2E, 0x2E}},
	{1, DSI_RGB_24b, 0x03, {0x77, 0x2B, 0x05}},
	{1, DSI_RGB_18b_L, 0x03, {0x77, 0x2B, 0x05}},
	{1, DSI_RGB_18b, 0x29, {0x71, 0x28, 0x28}},

	//add in D0 yuv422
	{4, DSI_YCbCr_16b, 0x02, {0x63, 0x21, 0x00}},
	{4, DSI_YCbCr_20b, 0x03, {0x65, 0x22, 0x22}},
	{4, DSI_YCbCr_24b, 0x03, {0x65, 0x22, 0x22}},

	{2, DSI_YCbCr_16b, 0x02, {0x67, 0x23, 0x01}},
	{2, DSI_YCbCr_20b, 0x03, {0x6B, 0x25, 0x25}},
	{2, DSI_YCbCr_24b, 0x03, {0x6B, 0x25, 0x25}},

	{1, DSI_YCbCr_16b, 0x02, {0x6F, 0x27, 0x03}},
	{1, DSI_YCbCr_20b, 0x03, {0x77, 0x2B, 0x05}},
	{1, DSI_YCbCr_24b, 0x03, {0x77, 0x2B, 0x05}},
	{0, 0, 0, {0, 0, 0}},
};

static const u8 mipi_color_space[][10] = {
	{
		CSI_RGB10b,
		CSI_RGB888,
		CSI_RGB666,
		CSI_RGB565,
		CSI_RGB555,
		CSI_RGB444,
		CSI_YCbCr4208b,
		CSI_YCbCr4228b,
		CSI_YCbCr42210b,
		CSI_YCbCr42212b
	}, {
		DSI_RGB_36b,
		DSI_RGB_30b,
		DSI_RGB_24b,
		DSI_RGB_18b_L,
		DSI_RGB_18b,
		DSI_YCbCr_16b,
		DSI_YCbCr_20b,
		DSI_YCbCr_24b,
		0xFF,
		0xFF
	}
};

static char *mipi_color_space_name[][10] = {
	{
		"CSI_RGB10b",
		"CSI_RGB888",
		"CSI_RGB666",
		"CSI_RGB565",
		"CSI_RGB555",
		"CSI_RGB444",
		"CSI_YCbCr4208b",
		"CSI_YCbCr4228b",
		"CSI_YCbCr42210b",
		"CSI_YCbCr42212b"
	}, {
		"DSI_RGB_36b",
		"DSI_RGB_30b",
		"DSI_RGB_24b",
		"DSI_RGB_18b_L",
		"DSI_RGB_18b",
		"DSI_YCbCr_16b",
		"DSI_YCbCr_20b",
		"DSI_YCbCr_24b",
		"can not find color space",
		"can not find color space"
	}
};

static const u8 csc_matrix[][22] = {
	{
		0x00, 0x80, 0x10, 0xB2, 0x04, 0x65, 0x02, 0xE9, 0x00, 0x93, 0x3C,
		0x18, 0x04, 0x55, 0x3F, 0x49, 0x3D, 0x9F, 0x3E, 0x18, 0x04, 0x00
	}, {
		0x10, 0x80, 0x10, 0x09, 0x04, 0x0E, 0x02, 0xC9, 0x00, 0x0F, 0x3D,
		0x84, 0x03, 0x6D, 0x3F, 0xAB, 0x3D, 0xD1, 0x3E, 0x84, 0x03, 0x00
	}, {
		0x00, 0x80, 0x10, 0xB8, 0x05, 0xB4, 0x01, 0x94, 0x00, 0x4A, 0x3C,
		0x17, 0x04, 0x9F, 0x3F, 0xD9, 0x3C, 0x10, 0x3F, 0x17, 0x04, 0x00
	}, {
		0x10, 0x80, 0x10, 0xEA, 0x04, 0x77, 0x01, 0x7F, 0x00, 0xD0, 0x3C,
		0x83, 0x03, 0xAD, 0x3F, 0x4B, 0x3D, 0x32, 0x3F, 0x83, 0x03, 0x00
	}, {
		0x00, 0x00, 0x00, 0x00, 0x08, 0x6B, 0x3A, 0x50, 0x3D, 0x00, 0x08,
		0xF5, 0x0A, 0x02, 0x00, 0x00, 0x08, 0xFD, 0x3F, 0xDA, 0x0D, 0x00
	}, {
		0x04, 0x00, 0xA7, 0x4F, 0x09, 0x81, 0x39, 0xDD, 0x3C, 0x4F, 0x09,
		0xC4, 0x0C, 0x01, 0x00, 0x4F, 0x09, 0xFD, 0x3F, 0x1F, 0x10, 0x00
	}, {
		0x00, 0x00, 0x00, 0x00, 0x08, 0x55, 0x3C, 0x88, 0x3E, 0x00, 0x08,
		0x51, 0x0C, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x84, 0x0E, 0x00
	}, {
		0x04, 0x00, 0xA7, 0x4F, 0x09, 0xBA, 0x3B, 0x4B, 0x3E, 0x4F, 0x09,
		0x57, 0x0E, 0x02, 0x00, 0x4F, 0x09, 0xFE, 0x3F, 0xE8, 0x10, 0x00
	}, {
		0x04, 0x00, 0xA7, 0x4F, 0x09, 0xCC, 0x3A, 0x7E, 0x3E, 0x4F, 0x09,
		0x69, 0x0D, 0x0B, 0x00, 0x4F, 0x09, 0xFE, 0x3F, 0x1D, 0x11, 0x00
	}, {
		0x10, 0x10, 0x00, 0xe0, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0xe0, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xe0, 0x06, 0x10
	}, {
		0xED, 0xED, 0x00, 0x50, 0x09, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x50, 0x09, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x50, 0x09, 0xED
	}
};

struct it6616_reg_set it6616_hdmi_init_table[] = {
	{0x0F, 0xFF, 0x00},
	{0x22, 0xFF, 0x08},
	{0x22, 0xFF, 0x17},
	{0x23, 0xFF, 0x1F},
	{0x2B, 0xFF, 0x1F},
	{0x24, 0xFF, 0xF8},
	{0x22, 0xFF, 0x10},
	{0x23, 0xFF, 0xA0},
	{0x2B, 0xFF, 0xA0},
	{0x24, 0xFF, 0x00},
	{0x2F, 0xFF, 0xAD},
	{0x34, 0xFF, 0x00},
	{0x0F, 0xFF, 0x03},
	{0xAA, 0xFF, 0xEC},
	{0x0F, 0xFF, 0x00},
	{0x0F, 0xFF, 0x03},
	{0xAC, 0xFF, 0x40},
	{0x0F, 0xFF, 0x00},
	{0x3A, 0xFF, 0x89},
	{0x43, 0xFF, 0x01},
	{0x0F, 0xFF, 0x04},
	{0x43, 0xFF, 0x01},
	{0x3A, 0xFF, 0x89},
	{0x0F, 0xFF, 0x03},
	{0xA8, 0xFF, 0x0B},
	{0x0F, 0xFF, 0x00},
	{0x4F, 0xFF, 0x84},
	{0x44, 0xFF, 0x19},
	{0x46, 0xFF, 0x15},
	{0x47, 0xFF, 0x88},
	{0xD9, 0xFF, 0x00},
	{0xF0, 0xFF, 0x78},
	{0xF1, 0xFF, 0x10},
	{0x0F, 0xFF, 0x03},
	{0x3A, 0xFF, 0x02},
	{0x0F, 0xFF, 0x00},
	{0x28, 0xFF, 0x88},
	{0x6E, 0xFF, 0x00},
	{0x77, 0xFF, 0x87},
	{0x7B, 0xFF, 0x00},
	{0x86, 0xFF, 0x00},
	{0x0F, 0xFF, 0x00},
	{0x36, 0xFF, 0x06},
	{0x8F, 0xFF, 0x41},
	{0x0F, 0xFF, 0x01},
	{0xC0, 0xFF, 0x42},
	{0xC4, 0x70, 3<<4},
	{0xC4, BIT(7), 0<<7},
	{0xC7, 0xFF, 0x7F},
	{0xC8, 0xFF, 0x1F},
	{0xC9, 0xFF, 0x90},
	{0xCA, 0xFF, 0x99},
	{0x0F, 0xFF, 0x00},
	{0x86, 0x0C, 0x08},
	{0x81, BIT(7), BIT(7)},
	{BANK, BANKM, 0x01},
	{0x10, 0xFF, 0x00},
	{0x11, 0xFF, 0x00},
	{0x12, 0xFF, 0x00},
	{0x13, 0xFF, 0x00},
	{0x28, 0xFF, 0x00},
	{0x29, 0xFF, 0x00},
	{0x2A, 0xFF, 0x00},
	{0x2B, 0xFF, 0x00},
	{0x2C, 0xFF, 0x00},
	{0xC0, 0xC0, 0x40},
	{BANK, BANKM, 0x03},
	{0xE3, 0xFF, 0x07},
	{0x27, 0xFF, DEFAULT_RS_LEVEL},
	{0x28, 0xFF, DEFAULT_RS_LEVEL},
	{0x29, 0xFF, DEFAULT_RS_LEVEL},
	{0xA7, BIT(6), BIT(6)},
	{0x21, BIT(2), BIT(2)},
	{0xF8, 0xFF, 0xC3},
	{0xF8, 0xFF, 0xA5},
	{BANK, BANKM, 0x01},
	{0x5F, 0xFF, 0x04},
	{0x58, 0xFF, 0x12},
	{0x58, 0xFF, 0x02},
	{0x5F, 0xFF, 0x00},
	{BANK, BANKM, 0x00},
	{0xF8, 0xFF, 0xFF},
	{BANK, BANKM, 0x04},
	{0x3C, BIT(5), 0x000},
	{BANK, BANKM, 0x00},
	{0x91, BIT(6), BIT(6)},
	{BANK, BANKM, 0x03},
	{0xF0, 0xFF, 0xC0},
	{BANK, BANKM, 0x00},
	{0x21, BIT(6), BIT(6)},
	{0xCE, 0x30, 0x00},
	{BANK, BANKM, 0x04},
	{0xCE, 0x30, 0x00},
	{0x42, 0xE0, 0xC0},
	{BANK, BANKM, 0x00},
	{0x42, 0xE0, 0xC0},
	{0x7B, BIT(4), BIT(4)},
	{0x3C, 0x21, 0x00},
	{0x3B, 0xFF, 0x23},
	{0xF6, 0xFF, 0x08},
	{BANK, BANKM, 0x04},
	{0x3C, 0x21, 0x00},
	{0x3B, 0xFF, 0x23},
	{BANK, BANKM, 0x00},
	{0x59, 0xFF, 0x00},
	{0xFF, 0xFF, 0xFF},
};

static struct it6616 *g_it6616;
static void it6616_format_change(struct v4l2_subdev *sd);
static int it6616_s_ctrl_detect_tx_5v(struct v4l2_subdev *sd);
static int it6616_s_dv_timings(struct v4l2_subdev *sd,
				struct v4l2_dv_timings *timings);

static inline struct it6616 *to_it6616(struct v4l2_subdev *sd)
{
	return container_of(sd, struct it6616, sd);
}

static u8 it6616_hdmi_read(struct regmap *regmap, u8 reg)
{
	unsigned int val;

	regmap_read(regmap, reg, &val);

	return (u8)val;
}

static int it6616_hdmi_write(struct regmap *regmap, u8 reg, u8 value)
{
	return regmap_write(regmap, reg, value);
}

static int it6616_hdmi_set(struct regmap *regmap, u8 reg, u8 mask, u8 value)
{
	return regmap_update_bits(regmap, reg, mask, value);
}

static u8 it6616_mipi_tx_read(struct regmap *regmap, u8 reg)
{
	unsigned int val;

	regmap_read(regmap, reg, &val);

	return (u8)val;
}

static int it6616_mipi_tx_write(struct regmap *regmap, u8 reg, u8 value)
{
	return regmap_write(regmap, reg, value);
}

static int it6616_mipi_tx_set_bits(struct regmap *regmap, u8 reg, u8 mask, u8 value)
{
	return regmap_update_bits(regmap, reg, mask, value);
}

static int it6616_hdmi_edid_read(struct regmap *regmap, u8 *edid, int start, int length)
{
	return regmap_bulk_read(regmap, start, edid, length);
}

static int it6616_hdmi_edid_write(struct regmap *regmap, u8 *edid, int start, int length)
{
	return regmap_bulk_write(regmap, start, edid, length);
}

static void it6616_hdmi_chgbank(struct regmap *regmap, u8 bank)
{
	it6616_hdmi_set(regmap, 0x0F, 0x07, (u8)(bank & 0x07));
}

static void it6616_hdim_write_table(struct regmap *regmap, struct it6616_reg_set *tdata)
{
	while (tdata->ucAddr != 0xff) {
		if (tdata->andmask == 0xff)
			it6616_hdmi_write(regmap, tdata->ucAddr, tdata->ucValue);
		else
			it6616_hdmi_set(regmap, tdata->ucAddr, tdata->andmask, tdata->ucValue);
		tdata++;
	}
}

static bool it6616_mipi_tx_get_video_stable(struct it6616 *it6616)
{
	struct regmap *mipi = it6616->mipi_regmap;
	u8 reg09h;

	reg09h = it6616_mipi_tx_read(mipi, 0x09);

	return !!(reg09h & 0x40);
}

static void it6616_mipitx_init_bus_para(struct it6616 *it6616)
{
	struct bus_para *bus_para = &it6616->mipi.bus_para_config;
	u8 lpx = 0x03, mipi_tx_hs_prepare = 0x01;

	bus_para->swap_pn = MIPI_TX_PN_SWAP;
	bus_para->swap_lan = MIPI_TX_LANE_SWAP;
	bus_para->pclk_inv = MIPI_TX_ENABLE_PCLK_INV;
	bus_para->mclk_inv = MIPI_TX_ENABLE_MCLK_INV;
	bus_para->lpx_num =
		it6616->mipi_tx_enable_manual_adjusted_d_phy ? MIPI_TX_LPX : lpx;
	bus_para->mipi_tx_hs_prepare =
		it6616->mipi_tx_enable_manual_adjusted_d_phy ?
		MIPI_TX_HS_PREPARE : mipi_tx_hs_prepare;
	bus_para->tx_sel_line_start = true;
	bus_para->tx_bypass = MIPI_TX_ENABLE_BY_PASS;
	bus_para->tx_enable_hs_pre_1T = MIPI_TX_ENABLE_HS_PRE_1T;
	bus_para->tx_vlpm_length = MIPI_TX_V_LPM_LENGTH;
	bus_para->tx_hlpm_length = MIPI_TX_H_LPM_LENGTH;
	bus_para->tx_enable_h_enter_lpm = MIPI_TX_ENABLE_H_ENTER_LPM;
}

static int it6616_get_dcs_ecc(int dcshead)
{
	int q0, q1, q2, q3, q4, q5;

	q0 = ((dcshead >> 0) & (0x01)) ^ ((dcshead >> 1) & (0x01)) ^
		((dcshead >> 2) & (0x01)) ^ ((dcshead >> 4) & (0x01)) ^
		((dcshead >> 5) & (0x01)) ^ ((dcshead >> 7) & (0x01)) ^
		((dcshead >> 10) & (0x01)) ^ ((dcshead >> 11) & (0x01)) ^
		((dcshead >> 13) & (0x01)) ^ ((dcshead >> 16) & (0x01)) ^
		((dcshead >> 20) & (0x01)) ^ ((dcshead >> 21) & (0x01)) ^
		((dcshead >> 22) & (0x01)) ^ ((dcshead >> 23) & (0x01));
	q1 = ((dcshead >> 0) & (0x01)) ^ ((dcshead >> 1) & (0x01)) ^
		((dcshead >> 3) & (0x01)) ^ ((dcshead >> 4) & (0x01)) ^
		((dcshead >> 6) & (0x01)) ^ ((dcshead >> 8) & (0x01)) ^
		((dcshead >> 10) & (0x01)) ^ ((dcshead >> 12) & (0x01)) ^
		((dcshead >> 14) & (0x01)) ^ ((dcshead >> 17) & (0x01)) ^
		((dcshead >> 20) & (0x01)) ^ ((dcshead >> 21) & (0x01)) ^
		((dcshead >> 22) & (0x01)) ^ ((dcshead >> 23) & (0x01));
	q2 = ((dcshead >> 0) & (0x01)) ^ ((dcshead >> 2) & (0x01)) ^
		((dcshead >> 3) & (0x01)) ^ ((dcshead >> 5) & (0x01)) ^
		((dcshead >> 6) & (0x01)) ^ ((dcshead >> 9) & (0x01)) ^
		((dcshead >> 11) & (0x01)) ^ ((dcshead >> 12) & (0x01)) ^
		((dcshead >> 15) & (0x01)) ^ ((dcshead >> 18) & (0x01)) ^
		((dcshead >> 20) & (0x01)) ^ ((dcshead >> 21) & (0x01)) ^
		((dcshead >> 22) & (0x01));
	q3 = ((dcshead >> 1) & (0x01)) ^ ((dcshead >> 2) & (0x01)) ^
		((dcshead >> 3) & (0x01)) ^ ((dcshead >> 7) & (0x01)) ^
		((dcshead >> 8) & (0x01)) ^ ((dcshead >> 9) & (0x01)) ^
		((dcshead >> 13) & (0x01)) ^ ((dcshead >> 14) & (0x01)) ^
		((dcshead >> 15) & (0x01)) ^ ((dcshead >> 19) & (0x01)) ^
		((dcshead >> 20) & (0x01)) ^ ((dcshead >> 21) & (0x01)) ^
		((dcshead >> 23) & (0x01));
	q4 = ((dcshead >> 4) & (0x01)) ^ ((dcshead >> 5) & (0x01)) ^
		((dcshead >> 6) & (0x01)) ^ ((dcshead >> 7) & (0x01)) ^
		((dcshead >> 8) & (0x01)) ^ ((dcshead >> 9) & (0x01)) ^
		((dcshead >> 16) & (0x01)) ^ ((dcshead >> 17) & (0x01)) ^
		((dcshead >> 18) & (0x01)) ^ ((dcshead >> 19) & (0x01)) ^
		((dcshead >> 20) & (0x01)) ^ ((dcshead >> 22) & (0x01)) ^
		((dcshead >> 23) & (0x01));
	q5 = ((dcshead >> 10) & (0x01)) ^ ((dcshead >> 11) & (0x01)) ^
		((dcshead >> 12) & (0x01)) ^ ((dcshead >> 13) & (0x01)) ^
		((dcshead >> 14) & (0x01)) ^ ((dcshead >> 15) & (0x01)) ^
		((dcshead >> 16) & (0x01)) ^ ((dcshead >> 17) & (0x01)) ^
		((dcshead >> 18) & (0x01)) ^ ((dcshead >> 19) & (0x01)) ^
		((dcshead >> 21) & (0x01)) ^ ((dcshead >> 22) & (0x01)) ^
		((dcshead >> 23) & (0x01));

	return (q0 + (q1 << 1) + (q2 << 2) + (q3 << 3) + (q4 << 4) + (q5 << 5));
}

static int it6616_dcs_crc8t(int crcq16b, const u8 crc8bin)
{
	int lfsrout = 0, lfsr[16], i;

	lfsr[15] = ((crc8bin >> 7) & 0x01) ^ ((crc8bin >> 3) & 0x01) ^
		   ((crcq16b >> 7) & 0x01) ^ ((crcq16b >> 3) & 0x01);
	lfsr[14] = ((crc8bin >> 6) & 0x01) ^ ((crc8bin >> 2) & 0x01) ^
		   ((crcq16b >> 6) & 0x01) ^ ((crcq16b >> 2) & 0x01);
	lfsr[13] = ((crc8bin >> 5) & 0x01) ^ ((crc8bin >> 1) & 0x01) ^
		   ((crcq16b >> 5) & 0x01) ^ ((crcq16b >> 1) & 0x01);
	lfsr[12] = ((crc8bin >> 4) & 0x01) ^ ((crc8bin >> 0) & 0x01) ^
		   ((crcq16b >> 4) & 0x01) ^ ((crcq16b >> 0) & 0x01);
	lfsr[11] = ((crc8bin >> 3) & 0x01) ^ ((crcq16b >> 3) & 0x01);
	lfsr[10] = ((crc8bin >> 7) & 0x01) ^ ((crc8bin >> 3) & 0x01) ^
		   ((crc8bin >> 2) & 0x01) ^ ((crcq16b >> 7) & 0x01) ^
		   ((crcq16b >> 3) & 0x01) ^ ((crcq16b >> 2) & 0x01);
	lfsr[9] = ((crc8bin >> 6) & 0x01) ^ ((crc8bin >> 2) & 0x01) ^
		  ((crc8bin >> 1) & 0x01) ^ ((crcq16b >> 6) & 0x01) ^
		  ((crcq16b >> 2) & 0x01) ^ ((crcq16b >> 1) & 0x01);
	lfsr[8] = ((crc8bin >> 5) & 0x01) ^ ((crc8bin >> 1) & 0x01) ^
		  ((crc8bin >> 0) & 0x01) ^ ((crcq16b >> 5) & 0x01) ^
		  ((crcq16b >> 1) & 0x01) ^ ((crcq16b >> 0) & 0x01);
	lfsr[7] = ((crc8bin >> 4) & 0x01) ^ ((crc8bin >> 0) & 0x01) ^
		  ((crcq16b >> 15) & 0x01) ^ ((crcq16b >> 4) & 0x01) ^
		  ((crcq16b >> 0) & 0x01);
	lfsr[6] = ((crc8bin >> 3) & 0x01) ^
		  ((crcq16b >> 14) & 0x01) ^ ((crcq16b >> 3) & 0x01);
	lfsr[5] = ((crc8bin >> 2) & 0x01) ^
		  ((crcq16b >> 13) & 0x01) ^ ((crcq16b >> 2) & 0x01);
	lfsr[4] = ((crc8bin >> 1) & 0x01) ^
		  ((crcq16b >> 12) & 0x01) ^ ((crcq16b >> 1) & 0x01);
	lfsr[3] = ((crc8bin >> 7) & 0x01) ^ ((crc8bin >> 3) & 0x01) ^
		  ((crc8bin >> 0) & 0x01) ^ ((crcq16b >> 11) & 0x01) ^
		  ((crcq16b >> 7) & 0x01) ^ ((crcq16b >> 3) & 0x01) ^
		  ((crcq16b >> 0) & 0x01);
	lfsr[2] = ((crc8bin >> 6) & 0x01) ^ ((crc8bin >> 2) & 0x01) ^
		  ((crcq16b >> 10) & 0x01) ^ ((crcq16b >> 6) & 0x01) ^
		  ((crcq16b >> 2) & 0x01);
	lfsr[1] = ((crc8bin >> 5) & 0x01) ^ ((crc8bin >> 1) & 0x01) ^
		  ((crcq16b >> 9) & 0x01) ^ ((crcq16b >> 5) & 0x01) ^
		  ((crcq16b >> 1) & 0x01);
	lfsr[0] = ((crc8bin >> 4) & 0x01) ^ ((crc8bin >> 0) & 0x01) ^
		  ((crcq16b >> 8) & 0x01) ^ ((crcq16b >> 4) & 0x01) ^
		  ((crcq16b >> 0) & 0x01);

	for (i = 0; i < ARRAY_SIZE(lfsr); i++)
		lfsrout = lfsrout + (lfsr[i] << i);

	return lfsrout;
}

static int it6616_get_dcs_crc(int bytenum, const u8 *crcbyte)
{
	int i, crctemp = 0xFFFF;

	for (i = 0; i <= bytenum - 1; i++)
		crctemp = it6616_dcs_crc8t(crctemp, crcbyte[i]);

	return crctemp;
}

static void it6616_mipi_tx_setup_long_packet_header(struct mipi_packet *pheader,
						u32 word_count)
{
	int header;

	pheader->word_count_h = word_count >> 8;
	pheader->word_count_l = (u8)word_count;
	header = pheader->data_id | pheader->word_count_h << 16 | pheader->word_count_l << 8;
	pheader->ecc = it6616_get_dcs_ecc(header);
}

static enum mipi_packet_size it6616_mipi_tx_get_packet_size(struct it6616 *it6616,
					const struct dcs_setting_entry *dcs_setting_table,
					enum dcs_cmd_name cmd_name)
{
	struct device *dev = &it6616->mipi_i2c->dev;
	u8 i, size = ARRAY_SIZE(packet_size_data_id_map);

	for (i = 0; i < size; i++) {
		if (dcs_setting_table[cmd_name].data_id == packet_size_data_id_map[i].data_id)
			break;
	}

	if (i == size) {
		if (dcs_setting_table[cmd_name].count == 0) {
			dev_err(dev, "error! cmd index: %d count = 0", cmd_name);
			return UNKNOWN_PACKET;
		} else if (dcs_setting_table[cmd_name].count < 3) {
			return SHORT_PACKET;
		} else {
			return LONG_PACKET;
		}
	}

	return packet_size_data_id_map[i].packet_size;
}

static void it6616_mipi_tx_get_packet_fire_state(struct it6616 *it6616)
{
	struct regmap *mipi = it6616->mipi_regmap;
	struct device *dev = &it6616->mipi_i2c->dev;
	int lp_cmd_fifo, link_data_fifo;

	lp_cmd_fifo = it6616_mipi_tx_read(mipi, 0x71) & 0x0F;
	link_data_fifo = it6616_mipi_tx_read(mipi, 0x72);

	if (lp_cmd_fifo != 0)
		dev_err(dev,
			"error! fire low power cmd fail, remain bytes not fire, reg0x71:0x%02x",
			lp_cmd_fifo);
	if (link_data_fifo != 0)
		dev_err(dev,
			"error! fire link0 low power data fail, remain %d bytes not fire, reg0x72:0x%02x",
			link_data_fifo, link_data_fifo);
}

static void it6616_mipi_tx_setup_packet(struct it6616 *it6616, struct mipi_packet *pheader,
			  const struct dcs_setting_entry *dcs_setting_table,
			  enum dcs_cmd_name cmd_name)
{
	struct device *dev = &it6616->mipi_i2c->dev;
	int short_cmd;
	enum mipi_packet_size packet_size;

	pheader->data_id = dcs_setting_table[cmd_name].data_id;
	packet_size = it6616_mipi_tx_get_packet_size(it6616, dcs_setting_table, cmd_name);

	if (packet_size == UNKNOWN_PACKET) {
		dev_err(dev, "error! unknown packet size and check dcs table parameter");
		return;
	}

	if (packet_size == SHORT_PACKET) {
		pheader->word_count_l = dcs_setting_table[cmd_name].para_list[0];
		pheader->word_count_h = dcs_setting_table[cmd_name].para_list[1];
		short_cmd = pheader->data_id | pheader->word_count_l << 8 |
			pheader->word_count_h << 16;
		pheader->ecc = it6616_get_dcs_ecc(short_cmd);
	}

	if (packet_size == LONG_PACKET)
		it6616_mipi_tx_setup_long_packet_header(pheader, dcs_setting_table[cmd_name].count);
}

static inline void it6616_mipi_tx_fire_packet(struct it6616 *it6616,
			const struct dcs_setting_entry *dcs_setting_table,
			enum dcs_cmd_name cmd_name)
{
	struct regmap *mipi = it6616->mipi_regmap;

	it6616_mipi_tx_write(mipi, 0x75, dcs_setting_table[cmd_name].cmd);
}

static void it6616_mipi_tx_setup_packet_process(struct it6616 *it6616,
			const struct dcs_setting_entry *dcs_setting_table,
			enum dcs_cmd_name cmd_name,
			enum mipi_tx_lp_cmd_header header_select)
{
	struct regmap *mipi = it6616->mipi_regmap;
	struct device *dev = &it6616->mipi_i2c->dev;
	struct v4l2_subdev *sd = &it6616->sd;
	struct mipi_packet packet;
	enum mipi_packet_size packet_size;
	u32 long_packet_checksum;
	int i, header_crc, data_count;

	if (!header_select) {
		dev_err(dev, "no header packet");

		for (i = 0; i < dcs_setting_table[cmd_name].count; i++) {
			it6616_mipi_tx_write(mipi, 0x73, dcs_setting_table[cmd_name].para_list[i]);
			v4l2_dbg(1, debug, sd, "data[%d]: 0x%02x ", i,
					dcs_setting_table[cmd_name].para_list[i]);
		}

		header_crc = 0;
		goto short_packet;
	}
	it6616_mipi_tx_setup_packet(it6616, &packet, dcs_setting_table, cmd_name);
	packet_size = it6616_mipi_tx_get_packet_size(it6616, dcs_setting_table, cmd_name);
	v4l2_dbg(1, debug, sd, "%s packet\n\r", packet_size == LONG_PACKET ? "long" : "short");

	for (i = 0; i < sizeof(packet); i++) {
		it6616_mipi_tx_write(mipi, 0x73, ((u8 *)(&packet))[i]);
		v4l2_dbg(1, debug, sd, "data[%d]: 0x%02x ", i, ((u8 *)(&packet))[i]);
		msleep(20);
	}

	if (packet_size == SHORT_PACKET) {
		header_crc = 2;
		goto short_packet;
	}

	header_crc = sizeof(packet) + 2;

	long_packet_checksum = it6616_get_dcs_crc(dcs_setting_table[cmd_name].count,
					dcs_setting_table[cmd_name].para_list);
	for (i = 0; i < dcs_setting_table[cmd_name].count; i++) {
		it6616_mipi_tx_write(mipi, 0x73, dcs_setting_table[cmd_name].para_list[i]);
		v4l2_dbg(1, debug, sd,
			"cmd para: 0x%02x", dcs_setting_table[cmd_name].para_list[i]);
	}

	it6616_mipi_tx_write(mipi, 0x73, (u8)long_packet_checksum);
	it6616_mipi_tx_write(mipi, 0x73, (u8)(long_packet_checksum >> 8));
	v4l2_dbg(1, debug, sd,
		"long_packet_checksum_l: 0x%02x long_packet_checksum_h: 0x%02x",
		(u8)long_packet_checksum, (u8)(long_packet_checksum >> 8));

short_packet:
	data_count = dcs_setting_table[cmd_name].count + header_crc;

	if (data_count == LP_CMD_FIFO_SIZE)
		data_count = 1;

	it6616_mipi_tx_write(mipi, 0x74, (it6616->mipi_tx_enable_h_fire_packet << 7) | data_count);
}

static void it6616_mipi_tx_write_lp_cmds(struct it6616 *it6616,
			const struct dcs_setting_entry *dcs_setting_table,
			int dcs_table_size, enum dcs_cmd_name start,
			int count, enum mipi_tx_lp_cmd_header header_select)
{
	struct regmap *mipi = it6616->mipi_regmap;
	struct device *dev = &it6616->mipi_i2c->dev;
	u8 header_size, i, data_count;
	u8 enable_force_lp_mode = !it6616_mipi_tx_get_video_stable(it6616);
	u8 lp_cmd_fifo_size[] = { LP_CMD_FIFO_SIZE };

	it6616_mipi_tx_set_bits(mipi, 0x70, 0x03, 0x03);
	it6616_mipi_tx_set_bits(mipi, 0x70, 0x03, 0x00);

	if (enable_force_lp_mode) {
		it6616_mipi_tx_set_bits(mipi, 0x05, 0x16, 0x16);
		it6616_mipi_tx_set_bits(mipi, 0x05, 0x16, 0x10);
		it6616_mipi_tx_set_bits(mipi, 0x70, 0x04, 0x04);
	}

	it6616_mipi_tx_write(mipi, 0x3D, 0x00);
	it6616_mipi_tx_write(mipi, 0x3E, enable_force_lp_mode ? 0x00 : 0x10);
	it6616_mipi_tx_write(mipi, 0x3F, enable_force_lp_mode ? 0x30 : 0x90);

	for (i = start; i < start + count; i++) {
		dev_dbg(dev, "cmd:%d tx reg09:0x%02x", i, it6616_mipi_tx_read(mipi, 0x09));
		if (i >= dcs_table_size)
			goto complete_write_dcs;
		if (dcs_setting_table[i].cmd_name == DELAY) {
			msleep(dcs_setting_table[i].cmd);
			continue;
		}

		header_size = header_select ?
			((it6616_mipi_tx_get_packet_size(it6616, dcs_setting_table, i) ==
			SHORT_PACKET) ? 2 : 6) : 0;
		data_count = dcs_setting_table[i].count + header_size;

		if (data_count > lp_cmd_fifo_size[0]) {
			dev_err(dev, "error! lp cmd: %d, exceed cmd fifo", i);
			continue;
		}

		it6616_mipi_tx_setup_packet_process(it6616, dcs_setting_table, i, header_select);
		it6616_mipi_tx_fire_packet(it6616, dcs_setting_table, i);

		if (enable_force_lp_mode)
			it6616_mipi_tx_get_packet_fire_state(it6616);
	}
	msleep(20);

complete_write_dcs:
	if (i >= dcs_table_size && (start + count > dcs_table_size))
		dev_err(dev, "error! exceed maximum dcs setting table index");

	if (enable_force_lp_mode) {
		it6616_mipi_tx_set_bits(mipi, 0x70, 0x04, 0x00);
		it6616_mipi_tx_set_bits(mipi, 0x05, 0x16, 0x00);
	}

	msleep(20);

	if (!enable_force_lp_mode)
		it6616_mipi_tx_get_packet_fire_state(it6616);
}

static void it6616_enter_bus_turn_around(struct it6616 *it6616)
{
	struct regmap *mipi = it6616->mipi_regmap;
	u8 enable_force_lp_mode = !it6616_mipi_tx_get_video_stable(it6616);

	if (enable_force_lp_mode)
		it6616_mipi_tx_set_bits(mipi, 0x70, 0x04, 0x04);

	it6616_mipi_tx_write(mipi, 0x3E, 0x10);
	it6616_mipi_tx_write(mipi, 0x3F, 0x90);

	it6616_mipi_tx_write(mipi, 0x74, it6616->mipi_tx_enable_h_fire_packet << 7);
	it6616_mipi_tx_write(mipi, 0x75, LP_CMD_BTA);

	if (enable_force_lp_mode)
		it6616_mipi_tx_set_bits(mipi, 0x70, 0x04, 0x00);
}

static __maybe_unused void it6616_mipi_read_panel(struct it6616 *it6616,
		const struct dcs_setting_entry *dcs_setting_table,
		int dcs_table_size, enum dcs_cmd_name cmd_name, u8 *buffer)
{
	struct regmap *mipi = it6616->mipi_regmap;
	int link0_data_count, i;

	it6616_mipi_tx_write_lp_cmds(it6616, dcs_setting_table,
			dcs_table_size, cmd_name, 1, CALC_HEADER);
	it6616_enter_bus_turn_around(it6616);
	msleep(20);
	link0_data_count = it6616_mipi_tx_read(mipi, 0x7A);

	for (i = 0; i < link0_data_count; i++)
		buffer[i] = it6616_mipi_tx_read(mipi, 0x79);
}

static int it6616_mipitx_get_bus_config(struct it6616 *it6616)
{
	struct mipi_bus *bus = &it6616->mipi;
	const struct bus_config *bus_config_table;
	struct bus_config *cfg = &bus->bus_para_config.cfg;
	struct device *dev = &it6616->mipi_i2c->dev;
	int i;

	bus_config_table = (bus->bus_type == MIPI_CSI) ?
			it6616_csi_bus_cfg : it6616_dsi_bus_cfg;

	for (i = 0; bus_config_table[i].lane; i++) {
		if (bus_config_table[i].lane == bus->lane_cnt &&
			bus_config_table[i].type == bus->data_type) {

			bus->bus_para_config.cfg = bus_config_table[i];
			dev_dbg(dev, "mipi_get_bus_config = %d (%s)",
				i, (bus->bus_type == MIPI_CSI) ? "MIPI_CSI" : "MIPI_DSI");
			dev_dbg(dev, "{%X, %X, %X, %X, %X ,%X}",
				cfg->lane,
				cfg->type,
				cfg->reg23_p2m,
				cfg->regb0_div[0],
				cfg->regb0_div[1],
				cfg->regb0_div[2]);

			return 0;
		}
	}

	dev_err(dev, "mipi_get_bus_config error");
	return -EINVAL;
}

static void it6616_mipitx_setup_dsi(struct it6616 *it6616)
{
	struct regmap *mipi = it6616->mipi_regmap;
	struct bus_para *bus = &it6616->mipi.bus_para_config;
	struct bus_config *cfg = &bus->cfg;
	struct device *dev = &it6616->mipi_i2c->dev;
	u8 mp_lane_num = cfg->lane - 1;
	u8 p2m_time_mul = 0x03;
	u8 p2m_time_div = 0x02;
	u32 mp_hs_pretime = 0x4a;
	u32 mp_hs_endtime = 0x09;
	u8 mp_vid_type = cfg->type;
	u32 mclk_ps = it6616->tx_mclk_ps, tx_mclk_mhz = it6616->tx_mclk / 1000;
	u32 mipi_tx_calc_hs_end_time = (6 * mclk_ps - 20 * 1000) / mclk_ps;
	u8 reg23;

	reg23 = cfg->reg23_p2m;

	p2m_time_mul = reg23 & 0x0F;
	p2m_time_div = (reg23 & 0xF0) >> 4;

	it6616_mipi_tx_set_bits(mipi, 0x28, 0x20, bus->tx_sel_line_start << 5);

	if (!it6616->mipi_tx_enable_manual_adjusted_d_phy) {
		if (mp_lane_num == 3) {
			mp_hs_pretime = (((145 + ((4 + bus->lpx_num) * 20)) * 1000 -
					((3 * mclk_ps) >> 1)) / mclk_ps);
			mp_hs_endtime = mipi_tx_calc_hs_end_time + 9;
		} else if (mp_lane_num == 1) {
			mp_hs_pretime = ((((145 + ((4 + bus->lpx_num) * 20)) * 1000 -
					((3 * mclk_ps) >> 1)) / mclk_ps) >> 1);
			mp_hs_endtime = (mipi_tx_calc_hs_end_time >> 1) + 9;
		} else {
			mp_hs_pretime = ((((145 + ((4 + bus->lpx_num) * 20)) * 1000 -
					((3 * mclk_ps) >> 1)) / mclk_ps) >> 2);
			mp_hs_endtime = (mipi_tx_calc_hs_end_time >> 2) + 9;
		}

		if (mipi_tx_calc_hs_end_time <= 0)
			mp_hs_endtime = 3;

		if (tx_mclk_mhz >= 300)
			mp_hs_pretime += 20;
		else if (tx_mclk_mhz >= 250)
			mp_hs_pretime += 18;
		else if (tx_mclk_mhz >= 200)
			mp_hs_pretime += 15;
		else if (tx_mclk_mhz >= 150)
			mp_hs_pretime += 12;
		else
			mp_hs_pretime += 9;
	} else {
		mp_hs_pretime = MIPI_TX_HS_PREPARE_ZERO;
		mp_hs_endtime = MIPI_TX_HS_TRAIL;
	}

	//dsi setting
	it6616_mipi_tx_set_bits(mipi, 0x5e, 0x03, (bus->tx_vlpm_length >> 8) & 0x03);
	it6616_mipi_tx_set_bits(mipi, 0x5d, 0xff, bus->tx_vlpm_length & 0xFF);
	it6616_mipi_tx_set_bits(mipi, 0x5e, 0x0c, (bus->tx_hlpm_length & 0x300) >> 6);
	it6616_mipi_tx_set_bits(mipi, 0x5f, 0xff, bus->tx_hlpm_length & 0xFF);
	it6616_mipi_tx_set_bits(mipi, 0x5e, 0x10, bus->tx_enable_h_enter_lpm << 4);
	it6616_mipi_tx_set_bits(mipi, 0x6a, 0xff, bus->p2m_delay.tx_dsi_vsync_delay);
	it6616_mipi_tx_set_bits(mipi, 0x6c, 0xff, mp_hs_pretime);
	it6616_mipi_tx_set_bits(mipi, 0x6d, 0xff, mp_hs_endtime);
	it6616_mipi_tx_set_bits(mipi, 0x5c, 0x03, (MIPI_TX_ENABLE_DSI_EOTP_PACKET << 1) |
			MIPI_TX_ENABLE_DSI_SYNC_EVENT);
	it6616_mipi_tx_set_bits(mipi, 0x5c, 0xf0, ((mp_vid_type & 0x30) << 2) |
			((mp_vid_type & 0x3) << 4));
	it6616_mipi_tx_set_bits(mipi, 0x60, 0x0f, p2m_time_mul);
	it6616_mipi_tx_set_bits(mipi, 0x61, 0x03, p2m_time_div);

	msleep(100);
	dev_info(dev,
		"hs_prepare_zero num: 0x%02x, hs_trail num: 0x%02x, dsi_vsync_delay: 0x%02x, mipi DSI TX setting done !!!",
		mp_hs_pretime, mp_hs_endtime, bus->p2m_delay.tx_dsi_vsync_delay);
}

static void it6616_mipitx_setup_csi(struct it6616 *it6616)//set_mptx
{
	struct regmap *hdmi = it6616->hdmi_regmap;
	struct regmap *mipi = it6616->mipi_regmap;
	struct bus_para *bus = &it6616->mipi.bus_para_config;
	struct bus_config *cfg = &bus->cfg;
	struct device *dev = &it6616->mipi_i2c->dev;
	u32 mp_hs_pretime = 0x4a;// int MPHSPreTime = 0x4a;//ori:11
	u32 mp_hs_endtime = 0x09;// int MPHSEndTime = 0x09;//ori:06
	u8 mp_vid_type = cfg->type;
	u32 mclk_ps = it6616->tx_mclk_ps, tx_mclk_mhz = it6616->tx_mclk / 1000;
	u32 mipi_tx_calc_hs_end_time = (6 * mclk_ps - 20 * 1000) / mclk_ps;
	u8 reg23, interlace, en_fs_fr_num = false;

	reg23 = cfg->reg23_p2m;

	interlace = (it6616_hdmi_read(hdmi, 0x98) & 0x02) >> 1;

	if (interlace)
		en_fs_fr_num = true;

	if (!it6616->mipi_tx_enable_manual_adjusted_d_phy) {
		mp_hs_pretime = (((145 + ((4 + bus->lpx_num) * 20)) * 1000 -
				((3 * mclk_ps) >> 1)) / mclk_ps);
		mp_hs_endtime = mipi_tx_calc_hs_end_time + 9;

		if (mipi_tx_calc_hs_end_time <= 0)
			mp_hs_endtime = 3;

		if (tx_mclk_mhz >= 300)
			mp_hs_pretime += 20;
		else if (tx_mclk_mhz >= 250)
			mp_hs_pretime += 18;
		else if (tx_mclk_mhz >= 200)
			mp_hs_pretime += 15;
		else if (tx_mclk_mhz >= 150)
			mp_hs_pretime += 12;
		else
			mp_hs_pretime += 9;
	} else {
		mp_hs_pretime = MIPI_TX_HS_PREPARE_ZERO;
		mp_hs_endtime = MIPI_TX_HS_TRAIL;
	}

	it6616_mipi_tx_write(mipi, 0x1F, mp_hs_endtime);
	it6616_mipi_tx_write(mipi, 0x22, mp_hs_pretime);
	it6616_mipi_tx_write(mipi, 0x24, 0x20);
	it6616_mipi_tx_write(mipi, 0x25, bus->p2m_delay.tx_csi_p2m_delay);
	it6616_mipi_tx_set_bits(mipi, 0x26, 0x20, (en_fs_fr_num << 5));
	it6616_mipi_tx_write(mipi, 0x27, 0x02);
	it6616_mipi_tx_write(mipi, 0x20, mp_vid_type);
	it6616_mipi_tx_write(mipi, 0x23, reg23);

	msleep(100);
	dev_info(dev,
		"hs_prepare_zero num: 0x%02x, hs_trail num: 0x%02x, tx_csi_p2m_delay: 0x%02x, mipi CSI TX setting done !!!",
		mp_hs_pretime, mp_hs_endtime, bus->p2m_delay.tx_csi_p2m_delay);
}

static u8 it6616_mipi_tx_find_color_space_name_index(struct it6616 *it6616)
{
	u8 i, csi_dsi_index = (it6616->mipi.bus_type == MIPI_CSI) ? 0 : 1;

	for (i = 0; i < ARRAY_SIZE(mipi_color_space[0]); i++) {
		if (it6616->mipi.data_type == mipi_color_space[csi_dsi_index][i])
			return i;
	}

	return 0xFF;
}

static void it6616_mipitx_output_disable(struct it6616 *it6616)
{
	struct regmap *mipi = it6616->mipi_regmap;

	it6616_mipi_tx_write(mipi, 0x05, 0x36);
}

static void it6616_mipi_tx_output_enable(struct it6616 *it6616)
{
	struct regmap *mipi = it6616->mipi_regmap;

	/* release reset */
	it6616_mipi_tx_write(mipi, 0x05, 0x00);
}

static void it6616_mipi_tx_non_continuous_clock_setup(struct it6616 *it6616)
{
	struct regmap *mipi = it6616->mipi_regmap;
	struct device *dev = &it6616->mipi_i2c->dev;

	/* enable non continuous clock */
	it6616_mipi_tx_set_bits(mipi, 0x44, 0x01, 0x00);

	dev_info(dev, "set mipi tx non continuous clock");
}

static void it6616_mipitx_output_setup(struct it6616 *it6616)
{
	struct regmap *mipi = it6616->mipi_regmap;
	struct bus_para *bus = &it6616->mipi.bus_para_config;
	struct bus_config *cfg = &bus->cfg;
	struct device *dev = &it6616->mipi_i2c->dev;
	u8 color_space_name_index = it6616_mipi_tx_find_color_space_name_index(it6616);
	u8 bus_type_index = ((it6616->mipi.bus_type == MIPI_CSI) ? 0 : 1);
	u8 regb0, mplldiv, mprediv;
	u32 mclk_MHz;
	u32 pclk = it6616->vinfo.pclk / 1000;

	if (it6616->mipi_tx_enable_auto_adjust_lane_count)
		it6616->mipi.lane_cnt = it6616->csi_lanes_in_use;

adjust_lane_count:
	dev_info(dev, "%s", bus_type_index ? "MIPI_DSI" : "MIPI_CSI");
	dev_info(dev, "color space: %s", (color_space_name_index != 0xFF) ?
		mipi_color_space_name[bus_type_index][color_space_name_index] : "not find match");
	dev_info(dev, "lan_num: %d, swap_pn: %d", it6616->mipi.lane_cnt, bus->swap_pn);
	dev_info(dev, "swap_lan: %d, pclk_inv: %d", bus->swap_lan, bus->pclk_inv);
	dev_info(dev, "mclk_inv: %d, lpx_num: %d", bus->mclk_inv, bus->lpx_num);

	it6616_mipitx_get_bus_config(it6616);

	if (pclk > 200)
		regb0 = cfg->regb0_div[0];
	else if (pclk > 100)
		regb0 = cfg->regb0_div[1];
	else
		regb0 = cfg->regb0_div[2];

	mprediv = regb0 >> 5;
	mplldiv = regb0 & 0x1F;

	dev_dbg(dev, "prediv: 0x%02x, plldiv: 0x%02x", mprediv, mplldiv);

	mclk_MHz = ((pclk) * (mplldiv + 1)) >> 1;
	mclk_MHz = mclk_MHz / (mprediv + 1);
	it6616->tx_mclk = mclk_MHz * 1000;
	it6616->tx_mclk_ps = (2000 * (mprediv + 1) * 1000) / ((pclk) * (mplldiv + 1));
	dev_info(dev,
		"mclk_ns: %d.%d ns, mclk: %d MHz, pclk: %d MHz",
		it6616->tx_mclk_ps / 1000, it6616->tx_mclk_ps % 1000,
		it6616->tx_mclk / 1000, pclk);

	if (it6616->mipi_tx_enable_auto_adjust_lane_count) {
		if (mclk_MHz < MIPI_TX_LANE_ADJUST_THRESHOLD) {
			it6616->mipi.lane_cnt = (it6616->mipi.lane_cnt == 4) ? 2 : 1;
			dev_info(dev, "mclk < %d MHz, adjust to lan_num: %d",
				MIPI_TX_LANE_ADJUST_THRESHOLD, it6616->mipi.lane_cnt);
			goto adjust_lane_count;
		}
	}

	if (it6616->tx_mclk > 310000)
		bus->mclk_inv = 0;

	it6616_mipi_tx_set_bits(mipi, 0x28, 0x0c, (bus->swap_pn << 3) | (bus->swap_lan << 2));
	it6616_mipi_tx_set_bits(mipi, 0x10, BIT(2), bus->pclk_inv << 2);

	switch (it6616_mipi_tx_read(mipi, 0x04)) {
	case 0xC0:
		it6616_mipi_tx_set_bits(mipi, 0x10, BIT(1), bus->mclk_inv << 1);
		break;
	default:
		it6616_mipi_tx_set_bits(mipi, 0x11, BIT(3), bus->mclk_inv << 3);
	}

	it6616_mipi_tx_set_bits(mipi, 0x8c, 0x40, 0x00);
	it6616_mipi_tx_set_bits(mipi, 0x47, 0xf0, bus->mipi_tx_hs_prepare << 4);
	it6616_mipi_tx_set_bits(mipi, 0x44, 0x04, bus->tx_enable_hs_pre_1T << 2);
	it6616_mipi_tx_set_bits(mipi, 0x21, 0x30, (it6616->mipi.lane_cnt - 1) << 4);

	dev_dbg(dev, "set hs_prepare num: 0x%02x, hs_lpx num: 0x%02x",
		bus->mipi_tx_hs_prepare, bus->lpx_num);

	if ((pclk < (10 * (mprediv + 1))) || (pclk > (100 * (mprediv + 1))))
		dev_err(dev,
			"MPTX PHY setting wrong, need to reset parameter for TXPHY!!!");

	if (it6616->mipi.bus_type == MIPI_CSI) {
		if (pclk >= (mclk_MHz * 2))
			bus->p2m_delay.tx_csi_p2m_delay = 0x02;
		else if (pclk >= mclk_MHz)
			bus->p2m_delay.tx_csi_p2m_delay = 0x04;
		else if (mclk_MHz >= (pclk * 2))
			bus->p2m_delay.tx_csi_p2m_delay = 0x0a;
		else if ((mclk_MHz * 2) >= (pclk * 3))
			bus->p2m_delay.tx_csi_p2m_delay = 0x08;
		else
			bus->p2m_delay.tx_csi_p2m_delay = 0x04;
		it6616_mipitx_setup_csi(it6616);
	} else {
		if (pclk >= (mclk_MHz * 2))
			bus->p2m_delay.tx_dsi_vsync_delay = 0x02;
		else if (pclk >= mclk_MHz)
			bus->p2m_delay.tx_dsi_vsync_delay = 0x04;
		else if (mclk_MHz >= (pclk * 2))
			bus->p2m_delay.tx_dsi_vsync_delay = 0x0a;
		else if ((mclk_MHz * 2) >= (pclk * 3))
			bus->p2m_delay.tx_dsi_vsync_delay = 0x08;
		else
			bus->p2m_delay.tx_dsi_vsync_delay = 0x04;
		it6616_mipitx_setup_dsi(it6616);
	}

	if (!it6616->mipi_tx_enable_continuous_clock)
		it6616_mipi_tx_non_continuous_clock_setup(it6616);

	/* setup mipi-tx-afe */
	it6616_mipi_tx_write(mipi, 0xb0, regb0);

	msleep(100);
}

static void it6616_enable_mipi(struct it6616 *it6616)
{
	it6616_mipitx_output_setup(it6616);
	it6616_mipi_tx_output_enable(it6616);
	it6616->mipi_tx_enable_mipi_output = 1;
}

static void it6616_disable_mipi(struct it6616 *it6616)
{
	it6616_mipitx_output_disable(it6616);
	it6616->mipi_tx_enable_mipi_output = 0;
}

static void it6616_mipi_tx_get_support_format(struct it6616 *it6616)
{
	struct regmap *mipi = it6616->mipi_regmap;
	struct device *dev = &it6616->mipi_i2c->dev;
	u8 mipi_intput_color = it6616->mipi.data_type;
	u8 color_space_name_index;
	u8 bus_type_index = ((it6616->mipi.bus_type == MIPI_CSI) ? 0 : 1);

	if (it6616->mipi.bus_type == MIPI_CSI) {
		switch (mipi_intput_color) {
		case CSI_RGB10b:
			dev_dbg(dev, "csi not support CSI_RGB10b");
			it6616->mipi.data_type = CSI_RGB888;
			break;

		case CSI_YCbCr42212b:
			dev_dbg(dev, "csi not support CSI_YCbCr42212b");
			it6616->mipi.data_type = CSI_YCbCr4228b;
			break;

		default:
			return;
		}

		color_space_name_index = it6616_mipi_tx_find_color_space_name_index(it6616);

		if (color_space_name_index != 0xFF)
			dev_dbg(dev,
				"will set %s",
				mipi_color_space_name[bus_type_index][color_space_name_index]);
		else
			dev_err(dev, "error not find match color space");
	} else {
		if (it6616_mipi_tx_read(mipi, 0x04) == 0xC0) {
			if ((mipi_intput_color == DSI_YCbCr_16b) ||
				(mipi_intput_color == DSI_YCbCr_20b) ||
				(mipi_intput_color == DSI_YCbCr_24b)) {
				mipi_intput_color = it6616->mipi.data_type = DSI_RGB_24b;
				dev_dbg(dev, "0xC0 MIPI DSI only support RGB, using: DSI_RGB_24b(%d)",
					mipi_intput_color);
			}
		}
	}
}

/*
 * mipi rx need pn swap and let first bit data(after SOT) will be rising edge
 * can use in it6616_mipitx_initial function before it6616_mipi_tx_write_lp_cmds
 */
static __maybe_unused void it6616_mipi_tx_rk_fix_first_bit_issue(struct it6616 *it6616)
{
	struct regmap *mipi = it6616->mipi_regmap;

	it6616_mipi_tx_write(mipi, 0x05, 0x00);
	it6616_mipi_tx_set_bits(mipi, 0x28, 0x08, 0x08);
	it6616_mipi_tx_set_bits(mipi, 0x70, 0x04, 0x04);
	it6616_mipi_tx_set_bits(mipi, 0x28, 0x08, 0x00);
	it6616_mipi_tx_set_bits(mipi, 0x70, 0x04, 0x00);
	it6616_mipi_tx_write(mipi, 0x05, 0x36);
}

static void it6616_mipitx_initial(struct it6616 *it6616)
{
	struct regmap *mipi = it6616->mipi_regmap;
	struct device *dev = &it6616->mipi_i2c->dev;
	struct bus_para *bus = &it6616->mipi.bus_para_config;

	it6616_mipi_tx_set_bits(mipi, 0x05, 0x09, 0x09);
	it6616_mipi_tx_write(mipi, 0x05, 0x36);

	it6616_mipi_tx_set_bits(mipi, 0x2A, 0x3c, 0x00);
	it6616_mipi_tx_set_bits(mipi, 0x3c, 0x20, 0x20);
	it6616_mipi_tx_set_bits(mipi, 0x6b, 0x01, it6616->mipi.bus_type);
	it6616_mipi_tx_set_bits(mipi, 0x10, 0x80, bus->tx_bypass << 7);
	it6616_mipi_tx_set_bits(mipi, 0xc1, 0x03, 0x03);
	it6616_mipi_tx_set_bits(mipi, 0xa8, 0x01, 0x00);
	it6616_mipi_tx_set_bits(mipi, 0x45, 0x0f, bus->lpx_num);

	if (it6616->mipi_tx_enable_initial_fire_lp_cmd) {
		it6616_mipi_tx_write_lp_cmds(it6616, dcs_table, ARRAY_SIZE(dcs_table),
						SET_DISPLAY_ON, 2, CALC_HEADER);
		it6616_mipi_tx_write(mipi, 0x05, 0x36);
	}

	dev_dbg(dev, "mipi_initial chip:0x%02x", it6616_mipi_tx_read(mipi, 0x04));
}

static bool it6616_hdmi_is_5v_on(struct it6616 *it6616)
{
	struct regmap *hdmi = it6616->hdmi_regmap;

	if (it6616_hdmi_read(hdmi, 0x13) & 0x01)
		return true;

	return false;
}

static bool it6616_hdmi_is_clock_stable(struct it6616 *it6616)
{
	struct regmap *hdmi = it6616->hdmi_regmap;

	if (it6616_hdmi_read(hdmi, 0x13) & 0x10)
		return true;

	return false;
}

static __maybe_unused bool it6616_hdmi_is_symbol_locked(struct it6616 *it6616)
{
	struct regmap *hdmi = it6616->hdmi_regmap;
	u8 reg14;

	reg14 = it6616_hdmi_read(hdmi, 0x14);
	if ((reg14 & 0x38) == 0x38)
		return true;

	return false;
}

static bool it6616_hdmi_is_scdt_on(struct it6616 *it6616)
{
	struct regmap *hdmi = it6616->hdmi_regmap;

	if (it6616_hdmi_read(hdmi, 0x19) & BIT(7))
		return true;

	return false;
}

static u8 it6616_hdmi_get_output_color_space(struct it6616 *it6616)
{
	u8 hdmi_output_color = 0;
	u8 mipi_intput_color = it6616->mipi.data_type;

	if (it6616->mipi.bus_type == MIPI_CSI) {
		switch (mipi_intput_color) {
		case CSI_RGB10b:
		case CSI_RGB888:
		case CSI_RGB666:
		case CSI_RGB565:
		case CSI_RGB555:
		case CSI_RGB444:
			hdmi_output_color = HDMI_COLORSPACE_RGB;
			break;
		case CSI_YCbCr4208b:
			hdmi_output_color = HDMI_COLORSPACE_YUV420;
			break;
		case CSI_YCbCr4228b:
		case CSI_YCbCr42210b:
		case CSI_YCbCr42212b:
			hdmi_output_color = HDMI_COLORSPACE_YUV422;
			break;
		}
	} else {
		switch (mipi_intput_color) {
		case DSI_RGB_36b:
		case DSI_RGB_30b:
		case DSI_RGB_24b:
		case DSI_RGB_18b_L:
		case DSI_RGB_18b:
			hdmi_output_color = HDMI_COLORSPACE_RGB;
			break;
		case DSI_YCbCr_16b:
		case DSI_YCbCr_20b:
		case DSI_YCbCr_24b:
			hdmi_output_color = HDMI_COLORSPACE_YUV422;
			break;
		}
	}
	it6616->output_colorspace = hdmi_output_color;

	return hdmi_output_color;
}

static void it6616_hdmi_edid_ram_get(struct it6616 *it6616, u8 *buf)
{
	struct regmap *hdmi = it6616->hdmi_regmap;
	struct regmap *edid = it6616->edid_regmap;

	it6616_hdmi_write(hdmi, 0x4B, (I2C_ADR_EDID | 0x01));
	it6616_hdmi_edid_read(edid, buf, 0, 256);
	it6616_hdmi_write(hdmi, 0x4B, (I2C_ADR_EDID));
}

static void it6616_hdmi_edid_ram_update_chksum(struct it6616 *it6616)
{
	struct regmap *hdmi = it6616->hdmi_regmap;
	u16 sum;
	u8 offset;
	int i;

	// cal block 0 sum
	sum = 0;
	for (i = 0 ; i < 127 ; i++)
		sum += it6616->edid_data[i];
	sum = (0x100 - sum) & 0xFF;

	it6616_hdmi_write(hdmi, 0xC9, sum);

	// cal block 1 sum
	sum = 0;
	offset = it6616_hdmi_read(hdmi, 0xC6);
	for (i = 128; i < 128 + 127; i++)
		sum += it6616->edid_data[i];

	sum -= it6616->edid_data[offset];
	sum -= it6616->edid_data[offset + 1];
	sum += it6616_hdmi_read(hdmi, 0xC7);
	sum += it6616_hdmi_read(hdmi, 0xC8);
	sum = (0x100 - sum) & 0xFF;

	it6616_hdmi_write(hdmi, 0xCA, sum);
}

static void it6616_hdmi_edid_ram_init(struct it6616 *it6616)
{
	struct regmap *hdmi = it6616->hdmi_regmap;
	struct regmap *edid = it6616->edid_regmap;
	unsigned int phy_addr_off;
	u16 addr;

	// write data to EDID RAM
	it6616_hdmi_write(hdmi, 0x4B, (I2C_ADR_EDID | 0x01));
	it6616_hdmi_edid_write(edid, it6616->edid_data, 0, it6616->edid_len);
	it6616_hdmi_write(hdmi, 0x4B, (I2C_ADR_EDID));

	// update physical address for VSDB
	addr = cec_get_edid_phys_addr(it6616->edid_data, it6616->edid_len, &phy_addr_off);
	if (addr != CEC_PHYS_ADDR_INVALID) {
		it6616_hdmi_write(hdmi, 0xC6, (u8)phy_addr_off); // VSDB start address
		it6616_hdmi_write(hdmi, 0xC7, (addr >> 8) & 0xFF); // addr AB
		it6616_hdmi_write(hdmi, 0xC8, addr & 0xFF); // addr CD
	}

	// recalculate block0/block1 checksum
	it6616_hdmi_edid_ram_update_chksum(it6616);

	it6616_hdmi_set(hdmi, 0xC5, 0x10, 0x10);
	msleep(20);
	it6616_hdmi_set(hdmi, 0xC5, 0x10, 0x00);
}

static void it6616_hdmi_video_reset(struct it6616 *it6616)
{
	struct regmap *hdmi = it6616->hdmi_regmap;

	it6616_hdmi_set(hdmi, 0x22, BIT(0), BIT(0));
	msleep(20);
	it6616_hdmi_set(hdmi, 0x22, BIT(0), 0x00);
	it6616_hdmi_set(hdmi, 0x10, BIT(1), BIT(1)); // clear vidstable change INT
	it6616_hdmi_set(hdmi, 0x12, BIT(7), BIT(7)); // clear vidstable change INT
}

static void it6616_hdmi_edid_ram_enable(struct it6616 *it6616, u8 enabled)
{
	struct regmap *hdmi = it6616->hdmi_regmap;

	if (enabled)
		it6616_hdmi_set(hdmi, 0xC5, 0x3F, 0x02);
	else
		it6616_hdmi_set(hdmi, 0xC5, 0x3F, 0x13);
}

static void it6616_hdmi_rx_get_video_info(struct it6616 *it6616)
{
	struct regmap *hdmi = it6616->hdmi_regmap;
	struct device *dev = &it6616->hdmi_i2c->dev;
	u16 h_sync_pol, v_sync_pol, interlaced;
	u16 h_total, h_active, h_front_porch, h_sync_w;
	u16 v_total, v_active, v_front_porch, v_sync_w;
	u32 frame_rate;

	interlaced = (it6616_hdmi_read(hdmi, 0x98) & 0x02) >> 1;

	h_total = ((it6616_hdmi_read(hdmi, 0x9C) & 0x3F) << 8) + it6616_hdmi_read(hdmi, 0x9B);
	h_active = ((it6616_hdmi_read(hdmi, 0x9E) & 0x3F) << 8) + it6616_hdmi_read(hdmi, 0x9D);
	h_front_porch = ((it6616_hdmi_read(hdmi, 0xA1) & 0xF0) << 4) + it6616_hdmi_read(hdmi, 0xA0);
	h_sync_w = ((it6616_hdmi_read(hdmi, 0xA1) & 0x01) << 8) + it6616_hdmi_read(hdmi, 0x9F);

	v_total = ((it6616_hdmi_read(hdmi, 0xA3) & 0x3F) << 8) + it6616_hdmi_read(hdmi, 0xA2);
	v_active = ((it6616_hdmi_read(hdmi, 0xA5) & 0x3F) << 8) + it6616_hdmi_read(hdmi, 0xA4);
	v_front_porch = ((it6616_hdmi_read(hdmi, 0xA8) & 0xF0) << 4) + it6616_hdmi_read(hdmi, 0xA7);
	v_sync_w = ((it6616_hdmi_read(hdmi, 0xA8) & 0x01) << 8) + it6616_hdmi_read(hdmi, 0xA6);

	h_sync_pol = (it6616_hdmi_read(hdmi, 0xAA) & BIT(5)) >> 5;
	v_sync_pol = (it6616_hdmi_read(hdmi, 0xAA) & BIT(6)) >> 6;

	it6616->vinfo.h_active = h_active;
	it6616->vinfo.h_total = h_total;
	it6616->vinfo.h_front_porch = h_front_porch;
	it6616->vinfo.h_sync_w = h_sync_w;
	it6616->vinfo.h_back_porch = (h_total - h_active - h_front_porch - h_sync_w);
	it6616->vinfo.v_active = v_active;
	it6616->vinfo.v_total = v_total;
	it6616->vinfo.v_front_porch = v_front_porch;
	it6616->vinfo.v_sync_w = v_sync_w;
	it6616->vinfo.v_back_porch = v_total - v_active - v_front_porch - v_sync_w;
	it6616->vinfo.interlaced = (interlaced) & 0x01;
	it6616->vinfo.v_sync_pol = (v_sync_pol) & 0x01;
	it6616->vinfo.h_sync_pol = (h_sync_pol) & 0x01;

	frame_rate = (u32)(it6616->vinfo.pclk) * 1000;
	frame_rate /= it6616->vinfo.h_total;
	frame_rate /= it6616->vinfo.v_total;
	it6616->vinfo.frame_rate = frame_rate;

	if (it6616->avi_if.colorspace == HDMI_COLORSPACE_YUV420) {
		dev_dbg(dev, "HActive = %d\n", it6616->vinfo.h_active*2);
		dev_dbg(dev, "HTotal = %d\n", it6616->vinfo.h_total*2);
	} else {
		dev_dbg(dev, "HActive = %d\n", it6616->vinfo.h_active);
		dev_dbg(dev, "HTotal = %d\n", it6616->vinfo.h_total);
	}

	dev_dbg(dev, "VActive = %d\n", it6616->vinfo.v_active);
	dev_dbg(dev, "VTotal = %d\n", it6616->vinfo.v_total);

	if (it6616->avi_if.colorspace == HDMI_COLORSPACE_YUV420) {
		dev_dbg(dev, "HFrontPorch = %d\n", it6616->vinfo.h_front_porch*2);
		dev_dbg(dev, "HSyncWidth = %d\n", it6616->vinfo.h_sync_w*2);
		dev_dbg(dev, "HBackPorch = %d\n", it6616->vinfo.h_back_porch*2);
	} else {
		dev_dbg(dev, "HFrontPorch = %d\n", it6616->vinfo.h_front_porch);
		dev_dbg(dev, "HSyncWidth = %d\n", it6616->vinfo.h_sync_w);
		dev_dbg(dev, "HBackPorch = %d\n", it6616->vinfo.h_back_porch);
	}

	dev_dbg(dev, "VFrontPorch = %d\n", it6616->vinfo.v_front_porch);
	dev_dbg(dev, "VSyncWidth = %d\n", it6616->vinfo.v_sync_w);
	dev_dbg(dev, "VBackPorch = %d\n", it6616->vinfo.v_back_porch);
	dev_dbg(dev, "FrameRate = %u\n", it6616->vinfo.frame_rate);

	if (it6616->vinfo.interlaced)
		dev_dbg(dev, "ScanMode = InterLaced\n");
	else
		dev_dbg(dev, "ScanMode = Progressive\n");

	if (it6616->vinfo.v_sync_pol)
		dev_dbg(dev, "VSyncPol = Positive\n");
	else
		dev_dbg(dev, "VSyncPol = Negative\n");

	if (it6616->vinfo.h_sync_pol)
		dev_dbg(dev, "HSyncPol = Positive\n");
	else
		dev_dbg(dev, "HSyncPol = Negative");
}

static void it6616_hdmi_hpd_output(struct it6616 *it6616, u8 hpd)
{
	struct regmap *hdmi = it6616->hdmi_regmap;

	it6616_hdmi_chgbank(hdmi, 3);

	if (hpd)
		it6616_hdmi_set(hdmi, 0xAB, 0xC0, 0xC0); // SET PORT0 HPD HIGH
	else
		it6616_hdmi_set(hdmi, 0xAB, 0xC0, 0x40); // SET PORT0 HPD LOW

	it6616_hdmi_chgbank(hdmi, 0);
}

static void it6616_hdmi_hdcp_reset(struct it6616 *it6616)
{
	struct regmap *hdmi = it6616->hdmi_regmap;

	it6616_hdmi_set(hdmi, 0x23, 0x02, 0x02);
	it6616_hdmi_set(hdmi, 0x23, 0x02, 0x00);
}

static bool it6616_hdmi_get_hdcp_status(struct it6616 *it6616)
{
	struct regmap *hdmi = it6616->hdmi_regmap;

	return it6616_hdmi_read(hdmi, 0xCF) & BIT(5);
}

static bool it6616_get_hdcp_status(struct it6616 *it6616)
{
	bool hdcp_status;

	hdcp_status = it6616_hdmi_get_hdcp_status(it6616);

	return hdcp_status;
}

static enum av_mute_state it6616_hdmi_rx_get_av_mute_state(struct it6616 *it6616)
{
	struct regmap *hdmi = it6616->hdmi_regmap;
	bool av_mute_state;

	it6616_hdmi_chgbank(hdmi, 0);
	av_mute_state = !!(it6616_hdmi_read(hdmi, 0xAA) & BIT(3));

	return av_mute_state ? AV_MUTE_ON : AV_MUTE_OFF;
}

static void it6616_hdmi_rx_set_av_mute(struct it6616 *it6616, enum av_mute_state mute_state)
{
	struct regmap *hdmi = it6616->hdmi_regmap;

	it6616_hdmi_chgbank(hdmi, 0);

	switch (mute_state) {
	case AV_MUTE_OFF:
		it6616_hdmi_set(hdmi, 0x4F, BIT(5), 0x00);

		if (!it6616->mipi_tx_enable_mipi_output) {
			it6616_hdmi_rx_get_video_info(it6616);
			it6616_mipi_tx_get_support_format(it6616);
			it6616_enable_mipi(it6616);
		}
		break;
	case AV_MUTE_ON:
		it6616_hdmi_set(hdmi, 0x4F, BIT(5), BIT(5));
		it6616_disable_mipi(it6616);
		break;
	default:
		break;
	}
}

static void it6616_hdmi_update_rs(struct it6616 *it6616, u8 level)
{
	struct regmap *hdmi = it6616->hdmi_regmap;
	u8 rs_val = level;

	it6616_hdmi_chgbank(hdmi, 3);
	it6616_hdmi_set(hdmi, 0x26, 0x20, 0x00);
	it6616_hdmi_write(hdmi, 0x27, rs_val);
	it6616_hdmi_write(hdmi, 0x28, rs_val);
	it6616_hdmi_write(hdmi, 0x29, rs_val);
	it6616_hdmi_chgbank(hdmi, 0);
}

static void it6616_mipi_tx_calc_rclk(struct it6616 *it6616)
{
	struct regmap *mipi = it6616->mipi_regmap;
	struct device *dev = &it6616->mipi_i2c->dev;
	unsigned long sum = 0, ul100msCNT = 0;
	u8 i, retry = 3;

	for (i = 0; i < retry; i++) {
		it6616_mipi_tx_set_bits(mipi, 0xE0, 0x80, 0x80); // Enable RCLK 100ms count
		msleep(100);
		it6616_mipi_tx_set_bits(mipi, 0xE0, 0x80, 0x00); // Disable RCLK 100ms count

		ul100msCNT = it6616_mipi_tx_read(mipi, 0xE3);
		ul100msCNT = ((ul100msCNT << 8) | (it6616_mipi_tx_read(mipi, 0xE2)));
		ul100msCNT = ((ul100msCNT << 8) | (it6616_mipi_tx_read(mipi, 0xE1)));

		sum += ul100msCNT;
	}
	sum /= retry;

	it6616->tx_rclk = sum / TIMER_100MS;
	dev_dbg(dev, "mipi rclk = %d.%d MHz", it6616->tx_rclk / 1000,
		it6616->tx_rclk % 1000);
}

static void it6616_mipi_tx_calc_mclk(struct it6616 *it6616)
{
	struct regmap *mipi = it6616->mipi_regmap;
	struct device *dev = &it6616->mipi_i2c->dev;
	unsigned long sum = 0, ulCNT, tx_mclk;
	u8 i, retry = 3;

	for (i = 0; i < retry; i++) {
		it6616_mipi_tx_set_bits(mipi, 0xE7, 0x80, 0x80);
		msleep(20);
		it6616_mipi_tx_set_bits(mipi, 0xE7, 0x80, 0x00);

		ulCNT = it6616_mipi_tx_read(mipi, 0xE7) & 0x0F;
		ulCNT = (it6616_mipi_tx_read(mipi, 0xE6) | (ulCNT << 8));

		sum += ulCNT;
	}

	sum /= retry;

	//MCLK = 13500*2048/sum;
	//MCLK = 27000*2048/sum;
	tx_mclk = it6616->tx_rclk * 2048 / sum;
	dev_dbg(dev, "mipi mclk = %lu.%lu MHz", tx_mclk / 1000, tx_mclk % 1000);
}

static void it6616_mipi_tx_calc_pclk(struct it6616 *it6616)
{
	struct regmap *mipi = it6616->mipi_regmap;
	struct device *dev = &it6616->mipi_i2c->dev;
	unsigned long sum = 0, ulCNT;
	u8 i, retry = 3;

	for (i = 0; i < retry; i++) {
		it6616_mipi_tx_set_bits(mipi, 0xE5, 0x80, 0x80);
		msleep(20);
		it6616_mipi_tx_set_bits(mipi, 0xE5, 0x80, 0x00);

		ulCNT = it6616_mipi_tx_read(mipi, 0xE5) & 0x0F;
		ulCNT = it6616_mipi_tx_read(mipi, 0xE4) + (ulCNT << 8);

		sum += ulCNT;
	}

	sum /= retry;

	//PCLK = 13500*2048/sum;
	//PCLK = 27000*2048/sum;
	it6616->tx_pclk = it6616->tx_rclk * 2048 / sum;
	dev_dbg(dev, "mipi pclk = %u.%u MHz", it6616->tx_pclk / 1000,
		it6616->tx_pclk % 1000);
}

static u32 it6616_hdmi_rx_calc_rclk(struct it6616 *it6616)
{
	struct regmap *hdmi = it6616->hdmi_regmap;
	struct device *dev = &it6616->hdmi_i2c->dev;
	u32 rddata, rclk, sum = 0;
	int i, retry = 5;
	int t1usint;
	int t1usflt;

	for (i = 0; i < retry; i++) {
		it6616_hdmi_set(hdmi, 0x58, 0x80, 0x80);
		msleep(100);
		it6616_hdmi_set(hdmi, 0x58, 0x80, 0x00);

		rddata = it6616_hdmi_read(hdmi, 0x59);
		rddata += (it6616_hdmi_read(hdmi, 0x5A) << 8);
		rddata += (it6616_hdmi_read(hdmi, 0x5B) << 16);

		sum += rddata;
	}

	sum /= retry;
	rclk = sum / TIMER_100MS;

	dev_dbg(dev, "RCLK=%u KHz\n", rclk);

	t1usint = rclk / 1000;
	t1usflt = (rclk / 1000 - t1usint) * 256;
	it6616_hdmi_set(hdmi, 0x1E, 0x3F, t1usint & 0x3F);
	it6616_hdmi_write(hdmi, 0x1F, t1usflt);

	it6616->rclk = rclk;

	return rclk;
}

static u32 it6616_hdmi_rx_calc_pclk(struct it6616 *it6616)
{
	struct regmap *hdmi = it6616->hdmi_regmap;
	struct device *dev = &it6616->hdmi_i2c->dev;
	u32 retry = 5, rddata, i;
	u32 pclk, sump = 0;

__recal:
	for (i = 0; i < retry; i++) {
		msleep(20);
		it6616_hdmi_set(hdmi, 0x9A, BIT(7), 0x00);
		rddata = ((u32)(it6616_hdmi_read(hdmi, 0x9A) & 0x03) << 8) +
							it6616_hdmi_read(hdmi, 0x99);
		it6616_hdmi_set(hdmi, 0x9A, BIT(7), BIT(7));
		sump += rddata;
	}

	sump /= retry;

	if (sump) {
		pclk = it6616->rclk * 512 / sump; // 512=2*256 because of 1T 2 pixel
		dev_dbg(dev, "PCLK = %u.%03u MHz", pclk / 1000, pclk % 1000);
		it6616->vinfo.pclk = pclk;
	} else {
		dev_err(dev, "%s: sump == 0", __func__);
		goto __recal;
	}

	return pclk;
}

static void it6616_hdmi_rx_calc_tmds_clk(struct it6616 *it6616, u8 count)
{
	struct regmap *hdmi = it6616->hdmi_regmap;
	struct device *dev = &it6616->hdmi_i2c->dev;
	u32	sumt = 0;
	u8 rddata = 0, i;

	for (i = 0; i < count; i++) {
		msleep(20);
		rddata = it6616_hdmi_read(hdmi, 0x48) + 1;
		sumt += rddata;
	}

	if (sumt) {
		rddata = it6616_hdmi_read(hdmi, 0x43) & 0xE0;

		if (rddata & BIT(7))
			it6616->vinfo.TMDSCLK = (it6616->rclk * (u32)1024 * i) / sumt;
		else if (rddata & BIT(6))
			it6616->vinfo.TMDSCLK = (it6616->rclk * (u32)512  * i) / sumt;
		else if (rddata & BIT(5))
			it6616->vinfo.TMDSCLK = (it6616->rclk * (u32)256  * i) / sumt;
		if (rddata == 0x00)
			it6616->vinfo.TMDSCLK = (it6616->rclk * (u32)128  * i) / sumt;

		dev_dbg(dev, "TMDSCLK = %u.%03uMHz\n",
			it6616->vinfo.TMDSCLK / 1000, it6616->vinfo.TMDSCLK % 1000);
	} else {
		dev_err(dev, "%s - sumt==0\n", __func__);
	}
}

static void it6616_hdmi_receive_avi_infoframe_log(struct it6616 *it6616, u8 *buffer, size_t length)
{
	struct device *dev = &it6616->hdmi_i2c->dev;
	u8 i;

	dev_dbg(dev, "avi infoframe:");
	for (i = 0; i < length; i++)
		dev_err(dev, "0x%02x", buffer[i]);
}

static int it6616_hdmi_update_avi_infoframe(struct it6616 *it6616)
{
	struct regmap *hdmi = it6616->hdmi_regmap;
	struct hdmi_avi_infoframe *frame = &it6616->avi_if;
	u8 avi_packet[20] = { 0 };

	avi_packet[0] = HDMI_INFOFRAME_TYPE_AVI;
	it6616_hdmi_chgbank(hdmi, 2);
	avi_packet[1] = it6616_hdmi_read(hdmi, REG_RX_AVI_HB1);// version
	avi_packet[2] = it6616_hdmi_read(hdmi, REG_RX_AVI_HB2);// version
	regmap_bulk_read(hdmi, REG_RX_AVI_DB0, &avi_packet[3], 16);
	it6616_hdmi_chgbank(hdmi, 0);

	it6616_hdmi_receive_avi_infoframe_log(it6616, avi_packet, ARRAY_SIZE(avi_packet));

	return hdmi_infoframe_unpack((union hdmi_infoframe *)frame, avi_packet, sizeof(avi_packet));
}

static void it6616_hdmi_rx_setup_csc(struct it6616 *it6616)
{
	struct regmap *hdmi = it6616->hdmi_regmap;
	struct device *dev = &it6616->hdmi_i2c->dev;
	enum hdmi_colorspace color_in;
	enum hdmi_colorspace color_out;
	enum csc_select csc_select;
	enum csc_matrix_type csc_matrix_type;
	u8 reg6b = 0;
	u8 reg6e = ((HDMI_RX_AUTO_CSC_SELECT << 7) |
			(HDMI_RX_QUANT_4LB << 6) |
			(HDMI_RX_CRCB_LIMIT << 5) |
			(HDMI_RX_COLOR_CLIP << 4) |
			(HDMI_RX_ENABLE_DITHER_FCNT_FUNCTION << 2) |
			(HDMI_RX_ENABLE_DITHER_FUNCTION << 1) |
			HDMI_RX_ENABLE_COLOR_UP_DN_FILTER);

	color_in = it6616->avi_if.colorspace;
	color_out = it6616_hdmi_get_output_color_space(it6616);

	if (color_in == HDMI_COLORSPACE_RGB && color_out != HDMI_COLORSPACE_RGB)
		csc_select = CSC_RGB2YUV;
	else if (color_in != HDMI_COLORSPACE_RGB && color_out == HDMI_COLORSPACE_RGB)
		csc_select = CSC_YUV2RGB;
	else
		csc_select = CSC_BYPASS;

	switch (csc_select) {
	case CSC_RGB2YUV:
		dev_info(dev, "csc rgb2yuv");
		if (it6616->avi_if.colorimetry == HDMI_COLORIMETRY_ITU_709) {
			if (it6616->avi_if.quantization_range == HDMI_QUANTIZATION_RANGE_LIMITED)
				csc_matrix_type = CSCMtx_RGB2YUV_ITU709_16_235;
			else
				csc_matrix_type = CSCMtx_RGB2YUV_ITU709_00_255;
		} else {/* HDMI_COLORIMETRY_ITU_601 */
			if (it6616->avi_if.quantization_range == HDMI_QUANTIZATION_RANGE_LIMITED)
				csc_matrix_type = CSCMtx_RGB2YUV_ITU601_16_235;
			else
				csc_matrix_type = CSCMtx_RGB2YUV_ITU601_00_255;
		}
		break;

	case CSC_YUV2RGB:
		dev_info(dev, "csc yuv2rgb");

		if (it6616->avi_if.colorimetry == HDMI_COLORIMETRY_ITU_709)
			// when 709 format always to RGB full range
			csc_matrix_type = CSCMtx_YUV2RGB_ITU709_00_255;
		else if (it6616->avi_if.colorimetry == HDMI_COLORIMETRY_EXTENDED &&
			(it6616->avi_if.extended_colorimetry == 0x05 ||
			it6616->avi_if.extended_colorimetry == 0x06))
			// this Matrix is BT2020 YUV to BT2020 RGB, not normal limit/full range RGB
			csc_matrix_type = CSCMtx_YUV2RGB_BT2020_00_255; // for BT.2020 CSC
		else	/* Colormetry_ITU601 */
			csc_matrix_type = CSCMtx_YUV2RGB_ITU601_00_255;
		break;

	case CSC_BYPASS:
		dev_info(dev, "csc byass");
		break;
	}

	if (csc_select != CSC_BYPASS) {
		it6616_hdmi_chgbank(hdmi, 1);
		regmap_bulk_write(hdmi, 0x70, (u8 *)csc_matrix[csc_matrix_type],
						 sizeof(csc_matrix[0]));
		it6616_hdmi_chgbank(hdmi, 0);
	}
	it6616_hdmi_set(hdmi, 0x6E, 0xF7, reg6e);

	it6616_hdmi_set(hdmi, 0x6C, 0x03, csc_select);

	switch (color_in) {
	case HDMI_COLORSPACE_YUV422:
		reg6b = 1 << 4;
		break;
	case HDMI_COLORSPACE_YUV444:
		reg6b = 2 << 4;
		break;
	case HDMI_COLORSPACE_YUV420:
		reg6b = 3 << 4;
		break;
	case HDMI_COLORSPACE_RGB:
		reg6b = 0 << 4;
		break;
	default:
		dev_err(dev, "## unknown input color space %x\n", color_in);
		break;
	}

	switch (color_out) {
	case HDMI_COLORSPACE_YUV422:
		reg6b |= 1 << 2;
		break;
	case HDMI_COLORSPACE_YUV444:
		reg6b |= 2 << 2;
		break;
	case HDMI_COLORSPACE_YUV420:
		reg6b |= 3 << 2;
		break;
	case HDMI_COLORSPACE_RGB:
		reg6b |= 0 << 2;
		break;
	default:
		dev_err(dev, "## unknown output color space %x\n", color_out);
		break;
	}

	if (it6616->mipi.bus_type == MIPI_CSI) {
		switch (it6616->mipi.data_type) {
		case CSI_RGB10b:
			reg6b |= 1 << 0;
			break;
		}
	} else {
		switch (it6616->mipi.data_type) {
		case DSI_RGB_36b:
			reg6b |= 2 << 0;
			break;
		case DSI_RGB_30b:
			reg6b |= 1 << 0;
			break;
		}
	}

	it6616_hdmi_set(hdmi, 0x6B, 0x3F, reg6b);
}

static void it6616_hdmi_rx_reset_audio_logic(struct it6616 *it6616)
{
	struct regmap *hdmi = it6616->hdmi_regmap;
	u8 temp;

	it6616_hdmi_set(hdmi, 0x22, BIT(1), BIT(1)); // audio reset
	msleep(20);
	it6616_hdmi_set(hdmi, 0x22, BIT(1), 0x00);

	// RegFS_Set[5:0] : Software set sampling frequency R/W
	temp = it6616_hdmi_read(hdmi, 0x8A);
	it6616_hdmi_write(hdmi, 0x8A, temp);
	it6616_hdmi_write(hdmi, 0x8A, temp);
	it6616_hdmi_write(hdmi, 0x8A, temp);
	it6616_hdmi_write(hdmi, 0x8A, temp);
}

static void it6616_hdmi_rx_audio_setup_i2s_justified(struct it6616 *it6616, u8 i2s_justified)
{
	struct regmap *hdmi = it6616->hdmi_regmap;

	i2s_justified = (i2s_justified == FROM_CONFIG) ?
			it6616->audio_i2s_justified : i2s_justified;
	it6616_hdmi_set(hdmi, 0x0F, 0x03, 0x00);
	it6616_hdmi_set(hdmi, 0x82, 0x03, i2s_justified);
}

static void it6616_hdmi_tx_audio_setup(struct it6616 *it6616)
{
	struct regmap *hdmi = it6616->hdmi_regmap;
	struct device *dev = &it6616->hdmi_i2c->dev;
	u32 sum = 0, cts_128;

	// RegForce_CTSMode : need to set to 1 for get the cts in the PKT,
	// 0 repersent nothing (HW using)
	// so set to 1 to get cts in the REG2C1/2C2/2C0
	it6616_hdmi_set(hdmi, 0x86, BIT(0), BIT(0));
	it6616_hdmi_rx_audio_setup_i2s_justified(it6616, FROM_CONFIG);
	it6616_hdmi_chgbank(hdmi, 2);
	it6616->ainfo.n = ((u32)it6616_hdmi_read(hdmi, 0xBE) << 12) +
			((u32)it6616_hdmi_read(hdmi, 0xBF) << 4) +
			((u32)it6616_hdmi_read(hdmi, 0xC0) & 0x0F);
	it6616->ainfo.cts = it6616_hdmi_read(hdmi, 0xC0) >> 4;
	it6616->ainfo.cts |= ((u32)it6616_hdmi_read(hdmi, 0xC1)) << 12;
	it6616->ainfo.cts |= ((u32)it6616_hdmi_read(hdmi, 0xC2)) << 4;
	it6616_hdmi_chgbank(hdmi, 0);

	if (it6616->ainfo.cts == 0) {
		dev_info(dev, "WARNING:cts = %u", it6616->ainfo.cts);
		return;
	}

	// in the hdmi2.0 page 84, need bit 24, 25, 26, 27, 30, 31
	// Audio_CH_Status : Audio Channel status decoder value[31:24]
	// and bit[24:27] = Audio Sampling Rate
	it6616->ainfo.channel_status = ((it6616_hdmi_read(hdmi, 0xB5) & 0xC0) >> 2) +
					(it6616_hdmi_read(hdmi, 0xB5) & 0x0F);
	cts_128 = 128 * it6616->ainfo.cts;
	sum = it6616->ainfo.n * it6616->vinfo.TMDSCLK;
	it6616->ainfo.sample_freq = sum / cts_128;

	dev_info(dev, "n = %u cts = %u\n", it6616->ainfo.n, it6616->ainfo.cts);
	dev_info(dev, "tmds clock = %d kHz\n", it6616->vinfo.TMDSCLK);
	dev_info(dev, "Audio_CH_Status[24:27 - 30:31][bit0~bit5] = 0x%02x\n",
		it6616->ainfo.channel_status);
	dev_info(dev, "sw clac sampling frequency = %d.%d kHz\n",
		it6616->ainfo.sample_freq, (sum % cts_128) * 100 / cts_128);

	if (it6616->ainfo.sample_freq > 25 && it6616->ainfo.sample_freq <= 38)
		it6616->ainfo.force_sample_freq = AUDIO_SAMPLING_32K;
	else if (it6616->ainfo.sample_freq > 38 && it6616->ainfo.sample_freq <= 45)
		it6616->ainfo.force_sample_freq = AUDIO_SAMPLING_44P1K;
	else if (it6616->ainfo.sample_freq > 45 && it6616->ainfo.sample_freq <= 58)
		it6616->ainfo.force_sample_freq = AUDIO_SAMPLING_48K;
	else if (it6616->ainfo.sample_freq > 58 && it6616->ainfo.sample_freq <= 78)
		it6616->ainfo.force_sample_freq = AUDIO_SAMPLING_64K;
	else if (it6616->ainfo.sample_freq > 78 && it6616->ainfo.sample_freq <= 91)
		it6616->ainfo.force_sample_freq = AUDIO_SAMPLING_88P2K;
	else if (it6616->ainfo.sample_freq > 91 && it6616->ainfo.sample_freq <= 106)
		it6616->ainfo.force_sample_freq = AUDIO_SAMPLING_96K;
	else if (it6616->ainfo.sample_freq > 106 && it6616->ainfo.sample_freq <= 166)
		it6616->ainfo.force_sample_freq = AUDIO_SAMPLING_128K;
	else if (it6616->ainfo.sample_freq > 166 && it6616->ainfo.sample_freq <= 182)
		it6616->ainfo.force_sample_freq = AUDIO_SAMPLING_176P4K;
	else if (it6616->ainfo.sample_freq > 182 && it6616->ainfo.sample_freq <= 202)
		it6616->ainfo.force_sample_freq = AUDIO_SAMPLING_192K;
	else if (it6616->ainfo.sample_freq > 224 && it6616->ainfo.sample_freq <= 320)
		it6616->ainfo.force_sample_freq = AUDIO_SAMPLING_256K;
	else if (it6616->ainfo.sample_freq > 320 && it6616->ainfo.sample_freq <= 448)
		it6616->ainfo.force_sample_freq = AUDIO_SAMPLING_384K;
	else if (it6616->ainfo.sample_freq > 448 && it6616->ainfo.sample_freq <= 638)
		it6616->ainfo.force_sample_freq = AUDIO_SAMPLING_512K;
	else if (it6616->ainfo.sample_freq > 638 && it6616->ainfo.sample_freq <= 894)
		it6616->ainfo.force_sample_freq = AUDIO_SAMPLING_768K;
	else if (it6616->ainfo.sample_freq > 894 && it6616->ainfo.sample_freq <= 1324)
		it6616->ainfo.force_sample_freq = AUDIO_SAMPLING_1024K;

	dev_info(dev, "Sampling_Frequency value 0x%02x", it6616->ainfo.force_sample_freq);

	if (it6616->ainfo.channel_status == it6616->ainfo.force_sample_freq) {
		dev_dbg(dev, "channel_status == force_sample_freq\n");
		if (it6616_hdmi_read(hdmi, 0x81) & BIT(6)) {
			// RegForce_FS : 0: Disable Force Audio FS mode
			it6616_hdmi_set(hdmi, 0x81, BIT(6), 0x00);
			it6616_hdmi_rx_reset_audio_logic(it6616);
		}
		it6616->audio_sampling_freq_error_count = 0;
		return;
	}

	it6616->audio_sampling_freq_error_count++;
	dev_dbg(dev, "it6616->audio_sampling_freq_error_count=%d\n",
		(int) it6616->audio_sampling_freq_error_count);

	/* exceed max error count , enable Force Sampling Mode */
	if (it6616->audio_sampling_freq_error_count > MAX_AUDIO_SAMPLING_FREQ_ERROR_COUNT) {
		it6616_hdmi_set(hdmi, 0x81, BIT(6), BIT(6));	// RegForce_FS : Force Audio FS mode
		// RegFS_Set[5:0] : Software set sampling frequency
		it6616_hdmi_set(hdmi, 0x8A, 0x3F, it6616->ainfo.force_sample_freq);

#if defined(Enable_Audio_Compatibility) && (Enable_Audio_Compatibility == 1)
		if (it6616->ainfo.sample_freq <= 182) {
			it6616_hdmi_set(hdmi, 0x89, 0x0C, 0x04);
			it6616_hdmi_set(hdmi, 0x86, 0x0C, 0x0C);
		} else {
			it6616_hdmi_set(hdmi, 0x89, 0x0C, 0x0C);
			it6616_hdmi_set(hdmi, 0x86, 0x0C, 0x04);
		}
#endif
		it6616->audio_sampling_freq_error_count = 0;
		it6616_hdmi_rx_reset_audio_logic(it6616);
	}
}

static void it6616_hdmi_tx_audio_output_enable(struct it6616 *it6616, u8 output_interface)
{
	struct regmap *hdmi = it6616->hdmi_regmap;
	struct device *dev = &it6616->hdmi_i2c->dev;

	it6616_hdmi_chgbank(hdmi, 1);

	switch (output_interface) {
	case AUDIO_OFF:
		dev_info(dev, "audio off");
		it6616_hdmi_write(hdmi, 0xC7, 0x7F); // SPDIF/I2S tri-state on
		break;

	case AUDIO_I2S:
	case AUDIO_SPDIF:
		dev_info(dev, "enable audio output");
		it6616_hdmi_write(hdmi, 0xC7, 0x00); // SPDIF/I2S tri-state off
		break;
	}

	it6616_hdmi_chgbank(hdmi, 0);
}

static void it6616_hdmi_audio_mute_clear(struct it6616 *it6616)
{
	struct regmap *hdmi = it6616->hdmi_regmap;

	it6616_hdmi_set(hdmi, 0x8C, BIT(4), BIT(4)); // set RegHWMuteClr
	it6616_hdmi_set(hdmi, 0x8C, BIT(4), 0x00); // clear RegHWMuteClr for clear H/W Mute
}


static void it6616_hdmi_rx_audio_process(struct it6616 *it6616)
{
	it6616_hdmi_tx_audio_output_enable(it6616, AUDIO_OFF);
	it6616_hdmi_rx_reset_audio_logic(it6616);
	it6616_hdmi_tx_audio_setup(it6616);
	it6616_hdmi_audio_mute_clear(it6616);
	it6616_hdmi_tx_audio_output_enable(it6616, it6616->audio_interface);
}

static void it6616_hdmi_initial(struct it6616 *it6616)
{
	struct regmap *hdmi = it6616->hdmi_regmap;
	struct device *dev = &it6616->hdmi_i2c->dev;

	it6616_hdmi_chgbank(hdmi, 0);
	it6616_hdim_write_table(hdmi, it6616_hdmi_init_table);

	it6616_hdmi_update_rs(it6616, DEFAULT_RS_LEVEL);
	it6616_hdmi_set(hdmi, 0x67, BIT(7), it6616->hdmi_rx_disable_pixel_repeat << 7);
	it6616_hdmi_set(hdmi, 0x69, BIT(6) | BIT(5),
			it6616->hdmi_rx_video_stable_condition << 5);
	dev_dbg(dev,
		"set hdmi rx video stable condition, reg0x69[6:5], 0x%02x",
		it6616_hdmi_read(hdmi, 0x69));
}

static void it6616_hdmi_irq_color_depth(struct it6616 *it6616)
{
	struct regmap *hdmi = it6616->hdmi_regmap;
	struct device *dev = &it6616->hdmi_i2c->dev;
	u8 input_color_depth;

	input_color_depth = (it6616_hdmi_read(hdmi, 0x98) >> 4) & 0x0F;
	dev_dbg(dev, "input color depth = %d bits\n", input_color_depth * 6);
}

static void it6616_hdmi_irq_new_avi_infoframe(struct it6616 *it6616)
{
	struct hdmi_avi_infoframe *avi_new;
	struct hdmi_avi_infoframe *avi_prev;

	if (it6616_hdmi_update_avi_infoframe(it6616) == 0) {
		avi_new = &it6616->avi_if;
		avi_prev = &it6616->avi_if_prev;
		hdmi_infoframe_log(KERN_INFO, &it6616->hdmi_i2c->dev,
					(union hdmi_infoframe *)avi_new);

		if (avi_prev->video_code != avi_new->video_code)
			avi_prev->video_code = avi_new->video_code;
		if (avi_prev->colorspace != avi_new->colorspace)
			avi_prev->colorspace = avi_new->colorspace;
	}
}

static void it6616_hdmi_hpd_trun_on(struct it6616 *it6616)
{
	it6616_hdmi_edid_ram_enable(it6616, 1);
	it6616_hdmi_video_reset(it6616);
	it6616_hdmi_rx_reset_audio_logic(it6616);
	it6616_hdmi_hpd_output(it6616, true);
}

static void it6616_hdmi_hpd_trun_off(struct it6616 *it6616)
{
	it6616_hdmi_hpd_output(it6616, false);
	it6616_hdmi_edid_ram_enable(it6616, 0);
}

static void it6616_mipitx_irq(struct it6616 *it6616)
{
	struct regmap *mipi = it6616->mipi_regmap;
	struct device *dev = &it6616->mipi_i2c->dev;
	u8 reg09h, reg0ah, reg0bh;

	reg09h = it6616_mipi_tx_read(mipi, 0x09);
	reg0ah = it6616_mipi_tx_read(mipi, 0x0A);
	reg0bh = it6616_mipi_tx_read(mipi, 0x0B);

	it6616_mipi_tx_write(mipi, 0x0A, reg0ah);
	it6616_mipi_tx_write(mipi, 0x0B, reg0bh);

	if (reg0bh & 0x10) {
		it6616->mipi_tx_video_stable = it6616_mipi_tx_get_video_stable(it6616);
		dev_info(dev, "mipi tx Video Stable Change ...");
		dev_info(dev, "mipi tx reg09 = 0x%02x, video %sstable",
			reg09h, it6616->mipi_tx_video_stable ? "" : "un");

		if (it6616->mipi_tx_video_stable) {
			it6616_mipi_tx_calc_rclk(it6616);
			it6616_mipi_tx_calc_mclk(it6616);
			it6616_mipi_tx_calc_pclk(it6616);
		}
	}

	if (reg0ah & 0x70) {
		if (reg0ah & 0x20)
			dev_err(dev, "Mipi Byte mismatch Err!!!\n");
		if (reg0ah & 0x40)
			dev_err(dev, "mipi P2M FIFO Err!!!\n");
	}
}

static void it6616_hdmi_irq(struct it6616 *it6616)
{
	struct regmap *hdmi = it6616->hdmi_regmap;
	struct device *dev = &it6616->hdmi_i2c->dev;

	u8 reg05h, reg06h, Reg08h, Reg09h;
	u8 Reg10h, Reg11h, Reg12h;
	u8 Reg13h, Reg14h, Reg15h;
	u8 Reg1Ah, Reg1Bh;

	reg05h = it6616_hdmi_read(hdmi, 0x05);
	it6616_hdmi_write(hdmi, 0x05, reg05h);
	reg06h = it6616_hdmi_read(hdmi, 0x06);
	it6616_hdmi_write(hdmi, 0x06, reg06h);
	Reg08h = it6616_hdmi_read(hdmi, 0x08);
	it6616_hdmi_write(hdmi, 0x08, Reg08h);
	Reg09h = it6616_hdmi_read(hdmi, 0x09);
	it6616_hdmi_write(hdmi, 0x09, Reg09h);

	Reg10h = it6616_hdmi_read(hdmi, 0x10);
	it6616_hdmi_write(hdmi, 0x10, Reg10h);

	Reg11h = it6616_hdmi_read(hdmi, 0x11);
	it6616_hdmi_write(hdmi, 0x11, Reg11h);

	Reg12h = it6616_hdmi_read(hdmi, 0x12);
	it6616_hdmi_write(hdmi, 0x12, Reg12h & 0x7F);

	Reg13h = it6616_hdmi_read(hdmi, 0x13);
	Reg14h = it6616_hdmi_read(hdmi, 0x14);
	Reg15h = it6616_hdmi_read(hdmi, 0x15);

	if (reg05h != 0x00) {
		if (reg05h & 0x10)
			dev_info(dev, "# hdmi mode chg #\n");
		if (reg05h & 0x20) {
			dev_err(dev, "# ECC Error #\n");
			it6616_hdmi_hdcp_reset(it6616);
		}
		if (reg05h & 0x40)
			dev_err(dev, "# Deskew Error #\n");
		if (reg05h & 0x80)
			dev_err(dev, "# H2VSkew Fail #\n");
		if (reg05h & 0x04) {
			dev_info(dev, "# Input Clock Change Detect #\n");
			if (it6616_hdmi_is_clock_stable(it6616))
				dev_info(dev, "# Clock Stable #\n");
			else
				dev_err(dev, "# Clock NOT Stable #\n");
		}

		if (reg05h & 0x02)
			dev_info(dev, "# Rx CKOn Detect #\n");

		if (reg05h & 0x01) {
			dev_info(dev, "# 5V state change INT #\n");
			it6616_hdmi_hdcp_reset(it6616);
			if (it6616_hdmi_is_5v_on(it6616))
				it6616_hdmi_hpd_trun_on(it6616);
			else
				it6616_hdmi_hpd_trun_off(it6616);
		}
	}

	if (reg06h != 0x00) {
		if (reg06h & 0x80)
			dev_err(dev, "# FSM Error  #\n");
		if (reg06h & 0x40)
			dev_err(dev, "# CH2 Symbol lock Rst #\n");
		if (reg06h & 0x20)
			dev_err(dev, "# CH1 Symbol lock Rst #\n");
		if (reg06h & 0x10)
			dev_err(dev, "# CH0 Symbol lock Rst #\n");
		if (reg06h & 0x08)
			dev_err(dev, "# CH2 CDR FIFO Aut0-Rst #\n");
		if (reg06h & 0x04)
			dev_err(dev, "# CH1 CDR FIFO Aut0-Rst #\n");
		if (reg06h & 0x02)
			dev_err(dev, "# CH0 CDR FIFO Aut0-Rst #\n");
		if (reg06h & 0x01)
			dev_info(dev, "# Symbol Lock State Change # ");
	}

	if (Reg09h != 0x00) {
		if (Reg09h & 0x01)
			dev_info(dev, "# HDCP Authentication Start #");
		if (Reg09h & 0x02) {
			dev_info(dev, "# HDCP Authentication Done #");
			it6616->hdmi_rx_hdcp_state = true;
		}
		if (Reg09h & 0x04) {
			it6616->hdmi_rx_hdcp_state = it6616_get_hdcp_status(it6616);
			dev_info(dev, "HDCP Encryption change interrupt!");
			dev_info(dev, "HDCP Encryption %s!",
				it6616->hdmi_rx_hdcp_state ? "ON" : "OFF");
		}
		if (Reg09h & 0x08) {
			dev_info(dev, "# HDCP Off #");
			it6616->hdmi_rx_hdcp_state = false;
		}
	}

	if (Reg12h != 0x00) {
		if (Reg12h & BIT(7)) {
			Reg1Ah = it6616_hdmi_read(hdmi, 0x1A);
			Reg1Bh = it6616_hdmi_read(hdmi, 0x1B) & 0x07;
			dev_info(dev, "# Video Parameters Change #\n");
			dev_info(dev, "# VidParaChange_Sts=Reg1Bh=0x%02X Reg1Ah=0x%02X\n",
				(int) Reg1Bh, (int) Reg1Ah);
			it6616_hdmi_rx_calc_tmds_clk(it6616, 5);
			it6616_hdmi_rx_calc_pclk(it6616);
			it6616_hdmi_rx_get_video_info(it6616);
			// only parameter change need to clear INT here ,
			//or register 1A/1B can't be read after clear.
			it6616_hdmi_write(hdmi, 0x12, 0x80);
		}

		if (Reg12h & BIT(6))
			dev_info(dev, "# 3D audio Valie Change #\n");
		if (Reg12h & BIT(5))
			dev_info(dev, "# DRM pkt Change #\n");
		if (Reg12h & 0x10)
			dev_info(dev, "# New Audio PKT Received #\n");
		if (Reg12h & 0x08)
			dev_info(dev, "# New ACP PKT Received #\n");
		if (Reg12h & 0x04)
			dev_info(dev, "# New SPD PKT Received #\n");
		if (Reg12h & 0x02)
			dev_info(dev, "# New MPEG InfoFrame Received #\n");
		if (Reg12h & 0x01) {
			dev_info(dev, "# New AVI InfoFrame Received #\n");
			it6616_hdmi_irq_new_avi_infoframe(it6616);
			it6616_hdmi_rx_setup_csc(it6616);
		}
	}

	if (Reg10h != 0x00) {
		if (Reg10h & 0x80) {
			dev_err(dev, "# Audio FIFO Error #");
			it6616_hdmi_rx_audio_process(it6616);
		}
		if (Reg10h & 0x40)
			dev_info(dev, "# Audio Auto Mute #");
		// todo: how about on/off flag at the same time ?
		if ((Reg10h & 0x20)) {
			dev_info(dev, "# PKT Left Mute #");
			it6616_hdmi_rx_set_av_mute(it6616, AV_MUTE_OFF);
		}
		if ((Reg10h & 0x10)) {
			dev_info(dev, "# Set Mute PKT Received #");
			it6616_hdmi_rx_set_av_mute(it6616, AV_MUTE_ON);
		}
		if (Reg10h & 0x08)
			dev_info(dev, "# Timer Counter Interrupt #");
		if (Reg10h & 0x04)
			dev_info(dev, "# Video Mode Changed #");
		if (Reg10h & 0x02) {
			it6616->hdmi_rx_video_stable = it6616_hdmi_is_scdt_on(it6616);
			dev_info(dev, "SCDT %s", it6616->hdmi_rx_video_stable ? "ON" : "OFF");

			if (it6616->hdmi_rx_video_stable) {
				it6616_hdmi_rx_calc_tmds_clk(it6616, 5);
				it6616_hdmi_rx_calc_pclk(it6616);
				it6616_hdmi_rx_get_video_info(it6616);
				it6616_hdmi_rx_audio_process(it6616);
				msleep(400);
				it6616_mipi_tx_get_support_format(it6616);
				it6616_enable_mipi(it6616);
			} else {
				it6616_disable_mipi(it6616);
			}
		}
		if (Reg10h & 0x01)
			dev_info(dev, "# Video Abnormal Interrupt #\n");
	}

	if (Reg11h != 0x00) {
		if (Reg11h & BIT(5))
			dev_info(dev, "# No Audio InfoFrame Received #\n");
		if (Reg11h & BIT(4))
			dev_info(dev, "# No AVI InfoFrame Received #\n");
		if (Reg11h & BIT(3)) {
			dev_info(dev, "# CD Detect #\n");
			it6616_hdmi_irq_color_depth(it6616);
		}
		if (Reg11h & BIT(1))
			it6616_hdmi_write(hdmi, 0x11, BIT(1));
		if (Reg11h & BIT(0))
			it6616_hdmi_write(hdmi, 0x11, BIT(0));
	}

	if (Reg13h != 0x00) {
		if (Reg13h & BIT(0))
			dev_dbg(dev, "# Port 0 power 5V detect #\n");
		if (Reg13h & BIT(1))
			dev_dbg(dev, "# Port 0 HDMI mode #\n");
		else
			dev_dbg(dev, "# Port 0 DVI mode #\n");
	}

	if (Reg14h != 0x00) {
		if (Reg14h & BIT(0))
			dev_dbg(dev, "# Port 0 IPLL clock is higher than 100MHz #\n");
	}

	if (Reg15h != 0x00) {
		if (Reg15h & BIT(6))
			dev_dbg(dev, "# Port 0 EDID Idle #\n");
		else
			dev_dbg(dev, "# Port 0 EDID active #\n");
	}
}

static int it6616_get_chip_id(struct it6616 *it6616)
{
	struct regmap *hdmi = it6616->hdmi_regmap;
	struct device *dev = &it6616->hdmi_i2c->dev;
	u8 chip_id[4] = {0x54, 0x49, 0x16, 0x66};
	int i, ret;

	for (i = 0; i < sizeof(chip_id); i++) {
		ret = it6616_hdmi_read(hdmi, i);

		if (ret != chip_id[i]) {
			dev_err(dev, "not 6616 reg[0x%02x]=0x%02x", i, ret);
			return -ENODEV;
		}
	}

	return 0;
}

static void it6616_poll_threaded_handler(struct it6616 *it6616)
{
	struct regmap *hdmi = it6616->hdmi_regmap;
	enum av_mute_state mute_state_config = !!(it6616_hdmi_read(hdmi, 0x4F) & BIT(5));
	enum av_mute_state current_mute_state;

	current_mute_state = it6616_hdmi_rx_get_av_mute_state(it6616);

	if (mute_state_config != current_mute_state)
		it6616_hdmi_rx_set_av_mute(it6616, current_mute_state);
}

static irqreturn_t it6616_intp_threaded_handler(int unused, void *data)
{
	struct it6616 *it6616 = (struct it6616 *)data;

	mutex_lock(&it6616->confctl_mutex);

	it6616_hdmi_irq(it6616);
	it6616_mipitx_irq(it6616);

	mutex_unlock(&it6616->confctl_mutex);

	return IRQ_HANDLED;
}

static int it6616_initial(struct it6616 *it6616)
{
	struct device *dev = &it6616->hdmi_i2c->dev;

	mutex_lock(&it6616->confctl_mutex);

	/* get device id */
	if (it6616_get_chip_id(it6616)) {
		dev_err(dev, "can not find it6616");
		return -ENODEV;
	}

	// init driver variables:
	it6616->edid_len = sizeof(default_edid);
	memcpy(it6616->edid_data, default_edid, it6616->edid_len);

	// mipi common settings:
	it6616->mipi.bus_type = MIPI_TX_INTERFACE;
	it6616->mipi.lane_cnt = it6616->csi_lanes_in_use;
	it6616->mipi.data_type = MIPI_TX_DATA_TYPE;

	it6616->mipi_tx_enable_auto_adjust_lane_count =
			MIPI_TX_ENABLE_AUTO_ADJUST_LANE_COUNT;
	it6616->mipi_tx_enable_h_fire_packet =
			MIPI_TX_ENABLE_H_FIRE_PACKET;
	it6616->mipi_tx_enable_initial_fire_lp_cmd =
			MIPI_TX_ENABLE_INITIAL_FIRE_LP_CMD;
	// hdmi settings:
	it6616->rs_level = DEFAULT_RS_LEVEL;
	it6616->audio_interface = AUDIO_I2S;
	it6616->audio_i2s_justified = AUDIO_I2S_JUSTIFIED;
	it6616->hdmi_rx_disable_pixel_repeat =
				HDMI_RX_DISABLE_PIXEL_REPEAT;
	it6616->hdmi_rx_video_stable_condition =
				HDMI_RX_VIDEO_STABLE_CONDITION;
	it6616->mipi_tx_enable_continuous_clock =
				MIPI_TX_ENABLE_CONTINUOUS_CLOCK;
	it6616->mipi_tx_enable_manual_adjusted_d_phy =
				MIPI_TX_ENABLE_MANUAL_ADJUSTED_D_PHY;
	it6616->hdmi_rx_video_stable = false;
	it6616->mipi_tx_video_stable = false;
	it6616->hdmi_rx_hdcp_state = false;
	it6616->mipi_tx_enable_mipi_output = false;

	it6616_hdmi_rx_calc_rclk(it6616);

	it6616_hdmi_initial(it6616);
	it6616_hdmi_edid_ram_init(it6616);
	it6616_mipitx_init_bus_para(it6616);
	it6616_mipitx_initial(it6616);

	mutex_unlock(&it6616->confctl_mutex);

	return 0;
}

static __maybe_unused void it6616_set_hpd(struct it6616 *it6616, u8 hpd)
{
	mutex_lock(&it6616->confctl_mutex);

	if (hpd)
		it6616_hdmi_hpd_trun_on(it6616);
	else
		it6616_hdmi_hpd_trun_off(it6616);

	mutex_unlock(&it6616->confctl_mutex);
}

static __maybe_unused void it6616_update_edid_data(struct it6616 *it6616, u8 *edid, int edid_len)
{
	mutex_lock(&it6616->confctl_mutex);

	memcpy(it6616->edid_data, edid, it6616->edid_len);
	it6616->edid_len = edid_len;
	it6616_hdmi_edid_ram_init(it6616);

	mutex_unlock(&it6616->confctl_mutex);
}

static const struct regmap_range it6616_hdmi_volatile_ranges[] = {
	{ .range_min = 0, .range_max = 0xff },
};

static const struct regmap_access_table it6616_hdmi_volatile_table = {
	.yes_ranges = it6616_hdmi_volatile_ranges,
	.n_yes_ranges = ARRAY_SIZE(it6616_hdmi_volatile_ranges),
};

static const struct regmap_config it6616_hdmi_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.volatile_table = &it6616_hdmi_volatile_table,
	.cache_type = REGCACHE_NONE,
};

static const struct regmap_range it6616_mipi_volatile_ranges[] = {
	{ .range_min = 0, .range_max = 0xff },
};

static const struct regmap_access_table it6616_mipi_volatile_table = {
	.yes_ranges = it6616_mipi_volatile_ranges,
	.n_yes_ranges = ARRAY_SIZE(it6616_mipi_volatile_ranges),
};

static const struct regmap_config it6616_mipi_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.volatile_table = &it6616_mipi_volatile_table,
	.cache_type = REGCACHE_NONE,
};

static const struct regmap_range it6616_edid_volatile_ranges[] = {
	{ .range_min = 0, .range_max = 0xff },
};

static const struct regmap_access_table it6616_edid_volatile_table = {
	.yes_ranges = it6616_edid_volatile_ranges,
	.n_yes_ranges = ARRAY_SIZE(it6616_edid_volatile_ranges),
};

static const struct regmap_config it6616_edid_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.volatile_table = &it6616_edid_volatile_table,
	.cache_type = REGCACHE_NONE,
};

static ssize_t attr_buffer_put(char *buf, char *reg_buf)
{
	int i = 0;
	char *str = buf, *end = buf + PAGE_SIZE;

	str += scnprintf(str, end - str,
	"     0x00 0x01 0x02 0x03 0x04 0x05 0x06 0x07");
	str += scnprintf(str, end - str,
	" 0x08 0x09 0x0A 0x0B 0x0C 0x0D 0x0E 0x0F\n");
	str += scnprintf(str, end - str,
	"=============================================");
	str += scnprintf(str, end - str,
	"=======================================");

	for (i = 0; i < 256; i++) {
		if (i % 16 == 0)
			str += scnprintf(str, end - str, "\n[%02X] ", i & 0xF0);
		str += scnprintf(str, end - str, "0x%02X ", reg_buf[i]);
	}
	str += scnprintf(str, end - str, "\n");

	return end - str;
}
static ssize_t edid_ram_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct it6616 *it6616 = dev_get_drvdata(dev);

	u8 reg_buf[256];

	dev_info(dev, "%s(%x)\n", __func__, it6616->attr_hdmi_reg_bank);

	mutex_lock(&it6616->confctl_mutex);
	it6616_hdmi_edid_ram_get(it6616, reg_buf);
	mutex_unlock(&it6616->confctl_mutex);

	return attr_buffer_put(buf, reg_buf);
}

static ssize_t hdmi_reg_store(struct device *dev,
			struct device_attribute *attr, const char *buf, size_t count)
{
	struct it6616 *it6616 = dev_get_drvdata(dev);
	int reg_bank;

	if (kstrtoint(buf, 10, &reg_bank) < 0)
		return -EINVAL;

	it6616->attr_hdmi_reg_bank = (u8) reg_bank;

	dev_info(dev, "%s() %d, %x\n",
		__func__, reg_bank, it6616->attr_hdmi_reg_bank);

	return count;
}


static ssize_t hdmi_reg_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct it6616 *it6616 = dev_get_drvdata(dev);
	struct regmap *hdmi = it6616->hdmi_regmap;
	int i;
	u8 reg_buf[256];

	dev_info(dev, "%s(%x)\n", __func__, it6616->attr_hdmi_reg_bank);

	mutex_lock(&it6616->confctl_mutex);
	it6616_hdmi_chgbank(hdmi, it6616->attr_hdmi_reg_bank);
	for (i = 0; i < 256; i++)
		reg_buf[i] = it6616_hdmi_read(hdmi, i);
	//regmap_bulk_read(dp, 0, reg_buf, 256);
	it6616_hdmi_chgbank(hdmi, 0);
	mutex_unlock(&it6616->confctl_mutex);

	return  attr_buffer_put(buf, reg_buf);
}

static ssize_t mipi_reg_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct it6616 *it6616 = dev_get_drvdata(dev);
	struct regmap *mipi = it6616->mipi_regmap;
	int i;
	u8 reg_buf[256];

	mutex_lock(&it6616->confctl_mutex);
	for (i = 0; i < 256; i++)
		reg_buf[i] = it6616_mipi_tx_read(mipi, i);
	//regmap_bulk_read(mipi, 0, reg_buf, 256);
	mutex_unlock(&it6616->confctl_mutex);

	return  attr_buffer_put(buf, reg_buf);
}

static ssize_t mipi_reg_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	struct it6616 *it6616 = dev_get_drvdata(dev);
	struct regmap *mipi = it6616->mipi_regmap;
	unsigned int addr, val;
	int ret;

	ret = sscanf(buf, "%X %X ", &addr, &val);
	if (ret) {
		dev_info(dev, "addr= %2.2X\n", addr);
		dev_info(dev, "val = %2.2X\n", val);
		if (((addr <= 0xFF) && (addr >= 0x00)) && ((val <= 0xFF) && (val >= 0x00)))
			regmap_write(mipi, addr, val);
	} else {
		dev_info(dev, "it6616_fwrite_mipi_reg , error[%s]\n", buf);
	}

	return size;
}

static DEVICE_ATTR_RW(mipi_reg);
static DEVICE_ATTR_RO(edid_ram);
static DEVICE_ATTR_RW(hdmi_reg);

static const struct attribute *it6616_attrs[] = {
	&dev_attr_hdmi_reg.attr,
	&dev_attr_mipi_reg.attr,
	&dev_attr_edid_ram.attr,
	NULL,
};

static inline bool tx_5v_power_present(struct v4l2_subdev *sd)
{
	struct it6616 *it6616 = to_it6616(sd);

	return it6616_hdmi_is_5v_on(it6616);
}

static inline bool no_signal(struct v4l2_subdev *sd)
{
	struct it6616 *it6616 = to_it6616(sd);

	v4l2_dbg(1, debug, sd, "%s no signal:%d\n", __func__,
			it6616->nosignal);

	return it6616->nosignal;
}

static inline bool audio_present(struct v4l2_subdev *sd)
{
	struct it6616 *it6616 = to_it6616(sd);

	return it6616->is_audio_present;
}

static int get_audio_sampling_rate(struct v4l2_subdev *sd)
{
	struct it6616 *it6616 = to_it6616(sd);

	if (no_signal(sd))
		return 0;

	return code_to_rate_table[it6616->ainfo.force_sample_freq];
}

static inline unsigned int fps_calc(const struct v4l2_bt_timings *t)
{
	if (!V4L2_DV_BT_FRAME_HEIGHT(t) || !V4L2_DV_BT_FRAME_WIDTH(t))
		return 0;

	return DIV_ROUND_CLOSEST((unsigned int)t->pixelclock,
			V4L2_DV_BT_FRAME_HEIGHT(t) * V4L2_DV_BT_FRAME_WIDTH(t));
}

static bool it6616_rcv_supported_res(struct v4l2_subdev *sd, u32 width,
				u32 height)
{
	u32 i;

	for (i = 0; i < ARRAY_SIZE(supported_modes); i++) {
		if ((supported_modes[i].width == width) &&
		    (supported_modes[i].height == height)) {
			break;
		}
	}

	if (i == ARRAY_SIZE(supported_modes)) {
		v4l2_err(sd, "%s do not support res wxh: %dx%d\n", __func__,
				width, height);
		return false;
	} else {
		return true;
	}
}

static int it6616_get_detected_timings(struct v4l2_subdev *sd,
				     struct v4l2_dv_timings *timings)
{
	struct it6616 *it6616 = to_it6616(sd);
	struct v4l2_bt_timings *bt = &timings->bt;
	u32 fps, htotal, vtotal;

	memset(timings, 0, sizeof(struct v4l2_dv_timings));
	it6616_hdmi_rx_get_video_info(it6616);

	it6616->nosignal = false;
	it6616->is_audio_present = tx_5v_power_present(sd) ? true : false;
	timings->type = V4L2_DV_BT_656_1120;
	bt->interlaced = it6616->vinfo.interlaced;
	bt->width = it6616->vinfo.h_active;
	bt->height = it6616->vinfo.v_active;
	bt->vsync = it6616->vinfo.v_sync_w;
	bt->hsync = it6616->vinfo.h_sync_w;
	bt->pixelclock = it6616->vinfo.pclk * 1000;
	bt->hfrontporch = it6616->vinfo.h_front_porch;
	bt->vfrontporch = it6616->vinfo.v_front_porch;
	bt->hbackporch = it6616->vinfo.h_back_porch;
	bt->vbackporch = it6616->vinfo.v_back_porch;
	htotal = it6616->vinfo.h_total;
	vtotal = it6616->vinfo.v_total;

	if (it6616->avi_if.colorspace == HDMI_COLORSPACE_YUV420) {
		bt->width = it6616->vinfo.h_active * 2;
		bt->hfrontporch = it6616->vinfo.h_front_porch * 2;
		bt->hbackporch = it6616->vinfo.h_back_porch * 2;
		bt->hsync = it6616->vinfo.h_sync_w * 2;
		htotal = it6616->vinfo.h_total * 2;
	}

	fps = fps_calc(bt);

	if (!it6616_rcv_supported_res(sd, bt->width, bt->height)) {
		it6616->nosignal = true;
		v4l2_err(sd, "%s: rcv err res, return no signal!\n", __func__);
		return -EINVAL;
	}

	/* for interlaced res*/
	if (bt->interlaced) {
		bt->height *= 2;
		bt->il_vsync = bt->vsync + 1;
	}

	v4l2_dbg(1, debug, sd, "act:%dx%d, total:%dx%d, pixclk:%d, fps:%d\n",
			bt->width, bt->height, htotal, vtotal, bt->pixelclock, fps);
	v4l2_dbg(1, debug, sd, "hfp:%d, hs:%d, hbp:%d, vfp:%d, vs:%d, vbp:%d\n",
			bt->hfrontporch, bt->hsync, bt->hbackporch,
			bt->vfrontporch, bt->vsync, bt->vbackporch);
	v4l2_dbg(1, debug, sd, "inerlaced:%d,\n", bt->interlaced);

	return 0;
}

static int it6616_s_ctrl_detect_tx_5v(struct v4l2_subdev *sd)
{
	struct it6616 *it6616 = to_it6616(sd);

	return v4l2_ctrl_s_ctrl(it6616->detect_tx_5v_ctrl,
			tx_5v_power_present(sd));
}

static int it6616_s_ctrl_audio_sampling_rate(struct v4l2_subdev *sd)
{
	struct it6616 *it6616 = to_it6616(sd);

	return v4l2_ctrl_s_ctrl(it6616->audio_sampling_rate_ctrl,
			get_audio_sampling_rate(sd));
}

static int it6616_s_ctrl_audio_present(struct v4l2_subdev *sd)
{
	struct it6616 *it6616 = to_it6616(sd);

	return v4l2_ctrl_s_ctrl(it6616->audio_present_ctrl,
			audio_present(sd));
}

static int it6616_update_controls(struct v4l2_subdev *sd)
{
	int ret = 0;

	ret |= it6616_s_ctrl_detect_tx_5v(sd);
	ret |= it6616_s_ctrl_audio_sampling_rate(sd);
	ret |= it6616_s_ctrl_audio_present(sd);

	return ret;
}

static bool it6616_match_dv_timings(const struct v4l2_dv_timings *t1,
				const struct v4l2_dv_timings *t2)
{
	if (t1->type != t2->type || t1->type != V4L2_DV_BT_656_1120)
		return false;
	if (t1->bt.width == t2->bt.width &&
		t1->bt.height == t2->bt.height &&
		t1->bt.interlaced == t2->bt.interlaced &&
		t1->bt.hfrontporch == t2->bt.hfrontporch &&
		t1->bt.hsync == t2->bt.hsync &&
		t1->bt.hbackporch == t2->bt.hbackporch &&
		t1->bt.vfrontporch == t2->bt.vfrontporch &&
		t1->bt.vsync == t2->bt.vsync &&
		t1->bt.vbackporch == t2->bt.vbackporch &&
		(!t1->bt.interlaced ||
		(t1->bt.il_vfrontporch == t2->bt.il_vfrontporch &&
		t1->bt.il_vsync == t2->bt.il_vsync &&
		t1->bt.il_vbackporch == t2->bt.il_vbackporch)))
		return true;
	return false;
}

static void it6616_format_change(struct v4l2_subdev *sd)
{
	struct it6616 *it6616 = to_it6616(sd);
	struct v4l2_dv_timings timings;
	const struct v4l2_event it6616_ev_fmt = {
		.type = V4L2_EVENT_SOURCE_CHANGE,
		.u.src_change.changes = V4L2_EVENT_SRC_CH_RESOLUTION,
	};

	it6616_get_detected_timings(sd, &timings);

	if (!it6616_match_dv_timings(&it6616->timings, &timings)) {
		/* automatically set timing rather than set by user */
		it6616_s_dv_timings(sd, &timings);
		v4l2_print_dv_timings(sd->name,
				"Format_change: New format: ",
				&timings, false);
		if (sd->devnode)
			v4l2_subdev_notify_event(sd, &it6616_ev_fmt);
	}
}

static int it6616_isr(struct v4l2_subdev *sd, u32 status, bool *handled)
{
	struct it6616 *it6616 = to_it6616(sd);
	static struct v4l2_dv_timings default_timing =
		V4L2_DV_BT_CEA_640X480P59_94;

	if (tx_5v_power_present(sd)) {
		it6616_poll_threaded_handler(it6616);
		it6616_intp_threaded_handler(0, it6616);
		it6616_format_change(sd);
	} else {
		it6616_s_dv_timings(sd, &default_timing);
		it6616->nosignal = true;
		v4l2_dbg(1, debug, sd, "%s: HDMI unplug!!!\n", __func__);
	}

	*handled = true;

	return 0;
}

static void it6616_work_i2c_poll(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct it6616 *it6616 = container_of(dwork,
			struct it6616, work_i2c_poll);
	bool handled;

	it6616_s_ctrl_detect_tx_5v(&it6616->sd);
	it6616_isr(&it6616->sd, 0, &handled);
	schedule_delayed_work(&it6616->work_i2c_poll,
			msecs_to_jiffies(POLL_INTERVAL_MS));
}

static int it6616_subscribe_event(struct v4l2_subdev *sd, struct v4l2_fh *fh,
				    struct v4l2_event_subscription *sub)
{
	switch (sub->type) {
	case V4L2_EVENT_SOURCE_CHANGE:
		return v4l2_src_change_event_subdev_subscribe(sd, fh, sub);
	case V4L2_EVENT_CTRL:
		return v4l2_ctrl_subdev_subscribe_event(sd, fh, sub);
	default:
		return -EINVAL;
	}
}

static int it6616_g_input_status(struct v4l2_subdev *sd, u32 *status)
{
	*status = 0;
	*status |= no_signal(sd) ? V4L2_IN_ST_NO_SIGNAL : 0;

	v4l2_dbg(1, debug, sd, "%s: status = 0x%x\n", __func__, *status);

	return 0;
}

static int it6616_s_dv_timings(struct v4l2_subdev *sd,
				 struct v4l2_dv_timings *timings)
{
	struct it6616 *it6616 = to_it6616(sd);

	if (!timings)
		return -EINVAL;

	if (debug)
		v4l2_print_dv_timings(sd->name, "s_dv_timings: ",
				timings, false);

	if (v4l2_match_dv_timings(&it6616->timings, timings, 0, false)) {
		v4l2_dbg(1, debug, sd, "%s: no change\n", __func__);
		return 0;
	}

	if (!v4l2_valid_dv_timings(timings,
				&it6616_timings_cap, NULL, NULL)) {
		v4l2_dbg(1, debug, sd, "%s: timings out of range\n", __func__);
		return -ERANGE;
	}

	it6616->timings = *timings;

	return 0;
}

static int it6616_g_dv_timings(struct v4l2_subdev *sd,
				struct v4l2_dv_timings *timings)
{
	struct it6616 *it6616 = to_it6616(sd);

	*timings = it6616->timings;

	return 0;
}

static int it6616_enum_dv_timings(struct v4l2_subdev *sd,
				struct v4l2_enum_dv_timings *timings)
{
	if (timings->pad != 0)
		return -EINVAL;

	return v4l2_enum_dv_timings_cap(timings,
			&it6616_timings_cap, NULL, NULL);
}

static int it6616_query_dv_timings(struct v4l2_subdev *sd,
				struct v4l2_dv_timings *timings)
{
	struct it6616 *it6616 = to_it6616(sd);

	*timings = it6616->timings;
	if (debug)
		v4l2_print_dv_timings(sd->name,
				"query_dv_timings: ", timings, false);

	if (!v4l2_valid_dv_timings(timings, &it6616_timings_cap, NULL,
				NULL)) {
		v4l2_dbg(1, debug, sd, "%s: timings out of range\n",
				__func__);

		return -ERANGE;
	}

	return 0;
}

static int it6616_dv_timings_cap(struct v4l2_subdev *sd,
				struct v4l2_dv_timings_cap *cap)
{
	if (cap->pad != 0)
		return -EINVAL;

	*cap = it6616_timings_cap;

	return 0;
}

static int it6616_g_mbus_config(struct v4l2_subdev *sd,
			unsigned int pad, struct v4l2_mbus_config *cfg)
{
	struct it6616 *it6616 = to_it6616(sd);

	cfg->type = V4L2_MBUS_CSI2_DPHY;
	cfg->flags = V4L2_MBUS_CSI2_NONCONTINUOUS_CLOCK |
			V4L2_MBUS_CSI2_CHANNEL_0;

	switch (it6616->csi_lanes_in_use) {
	case 1:
		cfg->flags |= V4L2_MBUS_CSI2_1_LANE;
		break;
	case 2:
		cfg->flags |= V4L2_MBUS_CSI2_2_LANE;
		break;
	case 3:
		cfg->flags |= V4L2_MBUS_CSI2_3_LANE;
		break;
	case 4:
		cfg->flags |= V4L2_MBUS_CSI2_4_LANE;
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static int __it6616_start_stream(struct it6616 *it6616)
{
	it6616_mipitx_output_disable(it6616);
	usleep_range(1000, 2000);
	it6616_mipi_tx_output_enable(it6616);

	return 0;
}

static int __it6616_stop_stream(struct it6616 *it6616)
{
	it6616_mipitx_output_disable(it6616);

	return 0;
}

static int it6616_s_stream(struct v4l2_subdev *sd, int on)
{
	struct it6616 *it6616 = to_it6616(sd);
	struct i2c_client *client = it6616->i2c_client;
	int ret = 0;

	dev_info(&client->dev, "%s: on: %d, %dx%d@%d\n", __func__, on,
				it6616->cur_mode->width,
				it6616->cur_mode->height,
		DIV_ROUND_CLOSEST(it6616->cur_mode->max_fps.denominator,
				  it6616->cur_mode->max_fps.numerator));

	mutex_lock(&it6616->confctl_mutex);
	on = !!on;

	if (on) {
		ret = __it6616_start_stream(it6616);
		if (ret) {
			dev_err(it6616->dev, "Failed to start it6616 stream\n");
			goto unlock_and_return;
		}
	} else {
		__it6616_stop_stream(it6616);
	}


unlock_and_return:
	mutex_unlock(&it6616->confctl_mutex);
	return 0;
}

static int it6616_enum_mbus_code(struct v4l2_subdev *sd,
			struct v4l2_subdev_pad_config *cfg,
			struct v4l2_subdev_mbus_code_enum *code)
{
	switch (code->index) {
	case 0:
		code->code = IT6616_MEDIA_BUS_FMT;
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static int it6616_enum_frame_sizes(struct v4l2_subdev *sd,
				   struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	if (fse->code != IT6616_MEDIA_BUS_FMT)
		return -EINVAL;

	fse->min_width  = supported_modes[fse->index].width;
	fse->max_width  = supported_modes[fse->index].width;
	fse->max_height = supported_modes[fse->index].height;
	fse->min_height = supported_modes[fse->index].height;

	return 0;
}

static int it6616_get_fmt(struct v4l2_subdev *sd,
			struct v4l2_subdev_pad_config *cfg,
			struct v4l2_subdev_format *format)
{
	struct it6616 *it6616 = to_it6616(sd);

	mutex_lock(&it6616->confctl_mutex);
	format->format.code = it6616->mbus_fmt_code;
	format->format.width = it6616->timings.bt.width;
	format->format.height = it6616->timings.bt.height;
	format->format.field =
		it6616->timings.bt.interlaced ?
		V4L2_FIELD_INTERLACED : V4L2_FIELD_NONE;
	format->format.colorspace = V4L2_COLORSPACE_SRGB;
	mutex_unlock(&it6616->confctl_mutex);

	v4l2_dbg(1, debug, sd, "%s: fmt code:%d, w:%d, h:%d, field code:%d\n",
			__func__, format->format.code, format->format.width,
			format->format.height, format->format.field);

	return 0;
}

static int it6616_enum_frame_interval(struct v4l2_subdev *sd,
				struct v4l2_subdev_pad_config *cfg,
				struct v4l2_subdev_frame_interval_enum *fie)
{
	if (fie->index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	if (fie->code != IT6616_MEDIA_BUS_FMT)
		return -EINVAL;

	fie->width = supported_modes[fie->index].width;
	fie->height = supported_modes[fie->index].height;
	fie->interval = supported_modes[fie->index].max_fps;

	return 0;
}

static int it6616_get_reso_dist(const struct it6616_mode *mode,
				struct v4l2_mbus_framefmt *framefmt)
{
	return abs(mode->width - framefmt->width) +
		abs(mode->height - framefmt->height);
}

static const struct it6616_mode *
it6616_find_best_fit(struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt = &fmt->format;
	int dist;
	int cur_best_fit = 0;
	int cur_best_fit_dist = -1;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(supported_modes); i++) {
		dist = it6616_get_reso_dist(&supported_modes[i], framefmt);
		if (cur_best_fit_dist == -1 || dist < cur_best_fit_dist) {
			cur_best_fit_dist = dist;
			cur_best_fit = i;
		}
	}

	return &supported_modes[cur_best_fit];
}

static int it6616_set_fmt(struct v4l2_subdev *sd,
			struct v4l2_subdev_pad_config *cfg,
			struct v4l2_subdev_format *format)
{
	struct it6616 *it6616 = to_it6616(sd);
	const struct it6616_mode *mode;

	/* is overwritten by get_fmt */
	u32 code = format->format.code;
	int ret = it6616_get_fmt(sd, cfg, format);

	format->format.code = code;

	if (ret)
		return ret;

	switch (code) {
	case IT6616_MEDIA_BUS_FMT:
		break;

	default:
		return -EINVAL;
	}

	if (format->which == V4L2_SUBDEV_FORMAT_TRY)
		return 0;

	it6616->mbus_fmt_code = format->format.code;
	mode = it6616_find_best_fit(format);
	it6616->cur_mode = mode;

	return 0;
}

static int it6616_g_frame_interval(struct v4l2_subdev *sd,
			struct v4l2_subdev_frame_interval *fi)
{
	struct it6616 *it6616 = to_it6616(sd);
	const struct it6616_mode *mode = it6616->cur_mode;

	mutex_lock(&it6616->confctl_mutex);
	fi->interval = mode->max_fps;
	mutex_unlock(&it6616->confctl_mutex);

	return 0;
}

static void it6616_get_module_inf(struct it6616 *it6616,
				  struct rkmodule_inf *inf)
{
	memset(inf, 0, sizeof(*inf));
	strscpy(inf->base.sensor, IT6616_NAME, sizeof(inf->base.sensor));
	strscpy(inf->base.module, it6616->module_name, sizeof(inf->base.module));
	strscpy(inf->base.lens, it6616->len_name, sizeof(inf->base.lens));
}

static long it6616_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct it6616 *it6616 = to_it6616(sd);
	long ret = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		it6616_get_module_inf(it6616, (struct rkmodule_inf *)arg);
		break;
	case RKMODULE_GET_HDMI_MODE:
		*(int *)arg = RKMODULE_HDMIIN_MODE;
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}

static int it6616_s_power(struct v4l2_subdev *sd, int on)
{
	struct it6616 *it6616 = to_it6616(sd);
	int ret = 0;

	mutex_lock(&it6616->confctl_mutex);

	if (it6616->power_on == !!on)
		goto unlock_and_return;

	if (on)
		it6616->power_on = true;
	else
		it6616->power_on = false;

unlock_and_return:
	mutex_unlock(&it6616->confctl_mutex);

	return ret;
}

#ifdef CONFIG_COMPAT
static long it6616_compat_ioctl32(struct v4l2_subdev *sd,
				  unsigned int cmd, unsigned long arg)
{
	void __user *up = compat_ptr(arg);
	struct rkmodule_inf *inf;
	long ret;
	int *seq;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		inf = kzalloc(sizeof(*inf), GFP_KERNEL);
		if (!inf) {
			ret = -ENOMEM;
			return ret;
		}

		ret = it6616_ioctl(sd, cmd, inf);
		if (!ret) {
			ret = copy_to_user(up, inf, sizeof(*inf));
			if (ret)
				ret = -EFAULT;
		}
		kfree(inf);
		break;
	case RKMODULE_GET_HDMI_MODE:
		seq = kzalloc(sizeof(*seq), GFP_KERNEL);
		if (!seq) {
			ret = -ENOMEM;
			return ret;
		}

		ret = it6616_ioctl(sd, cmd, seq);
		if (!ret) {
			ret = copy_to_user(up, seq, sizeof(*seq));
			if (ret)
				ret = -EFAULT;
		}
		kfree(seq);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}
#endif

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int it6616_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct it6616 *it6616 = to_it6616(sd);
	struct v4l2_mbus_framefmt *try_fmt =
				v4l2_subdev_get_try_format(sd, fh->pad, 0);
	const struct it6616_mode *def_mode = &supported_modes[0];

	mutex_lock(&it6616->confctl_mutex);
	/* Initialize try_fmt */
	try_fmt->width = def_mode->width;
	try_fmt->height = def_mode->height;
	try_fmt->code = IT6616_MEDIA_BUS_FMT;
	try_fmt->field = V4L2_FIELD_NONE;
	mutex_unlock(&it6616->confctl_mutex);

	return 0;
}
#endif

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_internal_ops it6616_internal_ops = {
	.open = it6616_open,
};
#endif

static const struct v4l2_subdev_core_ops it6616_core_ops = {
	.s_power = it6616_s_power,
	.interrupt_service_routine = it6616_isr,
	.subscribe_event = it6616_subscribe_event,
	.unsubscribe_event = v4l2_event_subdev_unsubscribe,
	.ioctl = it6616_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = it6616_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_video_ops it6616_video_ops = {
	.g_input_status = it6616_g_input_status,
	.s_dv_timings = it6616_s_dv_timings,
	.g_dv_timings = it6616_g_dv_timings,
	.query_dv_timings = it6616_query_dv_timings,
	.s_stream = it6616_s_stream,
	.g_frame_interval = it6616_g_frame_interval,
};

static const struct v4l2_subdev_pad_ops it6616_pad_ops = {
	.enum_mbus_code = it6616_enum_mbus_code,
	.enum_frame_size = it6616_enum_frame_sizes,
	.enum_frame_interval = it6616_enum_frame_interval,
	.set_fmt = it6616_set_fmt,
	.get_fmt = it6616_get_fmt,
	.enum_dv_timings = it6616_enum_dv_timings,
	.dv_timings_cap = it6616_dv_timings_cap,
	.get_mbus_config = it6616_g_mbus_config,
};

static const struct v4l2_subdev_ops it6616_ops = {
	.core = &it6616_core_ops,
	.video = &it6616_video_ops,
	.pad = &it6616_pad_ops,
};

static const struct v4l2_ctrl_config it6616_ctrl_audio_sampling_rate = {
	.id = RK_V4L2_CID_AUDIO_SAMPLING_RATE,
	.name = "Audio sampling rate",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.min = 0,
	.max = 768000,
	.step = 1,
	.def = 0,
	.flags = V4L2_CTRL_FLAG_READ_ONLY,
};

static const struct v4l2_ctrl_config it6616_ctrl_audio_present = {
	.id = RK_V4L2_CID_AUDIO_PRESENT,
	.name = "Audio present",
	.type = V4L2_CTRL_TYPE_BOOLEAN,
	.min = 0,
	.max = 1,
	.step = 1,
	.def = 0,
	.flags = V4L2_CTRL_FLAG_READ_ONLY,
};

static void it6616_reset(struct it6616 *it6616)
{
	gpiod_set_value(it6616->reset_gpio, 0);
	usleep_range(2000, 2100);
	gpiod_set_value(it6616->reset_gpio, 1);
	usleep_range(120*1000, 121*1000);
	gpiod_set_value(it6616->reset_gpio, 0);
	usleep_range(300*1000, 310*1000);
}

static int it6616_init_v4l2_ctrls(struct it6616 *it6616)
{
	struct v4l2_subdev *sd;
	int ret;

	sd = &it6616->sd;
	ret = v4l2_ctrl_handler_init(&it6616->hdl, 5);
	if (ret)
		return ret;

	it6616->link_freq = v4l2_ctrl_new_int_menu(&it6616->hdl, NULL,
			V4L2_CID_LINK_FREQ,
			ARRAY_SIZE(link_freq_menu_items) - 1, 0,
			link_freq_menu_items);
	it6616->pixel_rate = v4l2_ctrl_new_std(&it6616->hdl, NULL,
			V4L2_CID_PIXEL_RATE,
			0, IT6616_PIXEL_RATE, 1, IT6616_PIXEL_RATE);

	it6616->detect_tx_5v_ctrl = v4l2_ctrl_new_std(&it6616->hdl,
			NULL, V4L2_CID_DV_RX_POWER_PRESENT,
			0, 1, 0, 0);

	it6616->audio_sampling_rate_ctrl =
		v4l2_ctrl_new_custom(&it6616->hdl,
				&it6616_ctrl_audio_sampling_rate, NULL);
	it6616->audio_present_ctrl = v4l2_ctrl_new_custom(&it6616->hdl,
			&it6616_ctrl_audio_present, NULL);

	sd->ctrl_handler = &it6616->hdl;
	if (it6616->hdl.error) {
		ret = it6616->hdl.error;
		v4l2_err(sd, "cfg v4l2 ctrls failed! ret:%d\n", ret);
		return ret;
	}

	__v4l2_ctrl_s_ctrl(it6616->link_freq, link_freq_menu_items[0]);
	__v4l2_ctrl_s_ctrl_int64(it6616->pixel_rate, IT6616_PIXEL_RATE);

	if (it6616_update_controls(sd)) {
		ret = -ENODEV;
		v4l2_err(sd, "update v4l2 ctrls failed! ret:%d\n", ret);
		return ret;
	}

	return 0;
}

#ifdef CONFIG_OF
static int it6616_probe_of(struct it6616 *it6616)
{
	struct device *dev = &it6616->i2c_client->dev;
	struct device_node *node = dev->of_node;
	struct v4l2_fwnode_endpoint endpoint = { .bus_type = 0 };
	struct device_node *ep;
	int ret;

	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
			&it6616->module_index);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
			&it6616->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
			&it6616->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
			&it6616->len_name);
	if (ret) {
		dev_err(dev, "could not get module information!\n");
		return -EINVAL;
	}

	it6616->power_gpio = devm_gpiod_get_optional(dev, "power",
			GPIOD_OUT_LOW);
	if (IS_ERR(it6616->power_gpio)) {
		dev_err(dev, "failed to get power gpio\n");
		ret = PTR_ERR(it6616->power_gpio);
		return ret;
	}

	it6616->reset_gpio = devm_gpiod_get_optional(dev, "reset",
			GPIOD_OUT_HIGH);
	if (IS_ERR(it6616->reset_gpio)) {
		dev_err(dev, "failed to get reset gpio\n");
		ret = PTR_ERR(it6616->reset_gpio);
		return ret;
	}

	ep = of_graph_get_next_endpoint(dev->of_node, NULL);
	if (!ep) {
		dev_err(dev, "missing endpoint node\n");
		return -EINVAL;
	}

	ret = v4l2_fwnode_endpoint_alloc_parse(of_fwnode_handle(ep), &endpoint);
	if (ret) {
		dev_err(dev, "failed to parse endpoint\n");
		goto put_node;
	}

	if (endpoint.bus_type != V4L2_MBUS_CSI2_DPHY ||
			endpoint.bus.mipi_csi2.num_data_lanes == 0) {
		dev_err(dev, "missing CSI-2 properties in endpoint\n");
		ret = -EINVAL;
		goto free_endpoint;
	}

	it6616->xvclk = devm_clk_get(dev, "xvclk");
	if (IS_ERR(it6616->xvclk)) {
		dev_err(dev, "failed to get xvclk\n");
		ret = -EINVAL;
		goto free_endpoint;
	}

	ret = clk_prepare_enable(it6616->xvclk);
	if (ret) {
		dev_err(dev, "Failed! to enable xvclk\n");
		goto free_endpoint;
	}

	it6616->csi_lanes_in_use = endpoint.bus.mipi_csi2.num_data_lanes;
	it6616->bus = endpoint.bus.mipi_csi2;

	gpiod_set_value(it6616->power_gpio, 1);
	it6616_reset(it6616);

	ret = 0;

free_endpoint:
	v4l2_fwnode_endpoint_free(&endpoint);
put_node:
	of_node_put(ep);
	return ret;
}
#else
static inline int it6616_probe_of(struct it6616 *state)
{
	return -ENODEV;
}
#endif

static ssize_t audio_present_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct it6616 *it6616 = g_it6616;

	return snprintf(buf, PAGE_SIZE, "%d\n",
			tx_5v_power_present(&it6616->sd) ? 1 : 0);
}

static ssize_t audio_rate_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct it6616 *it6616 = g_it6616;

	return snprintf(buf, PAGE_SIZE, "%d\n",
			code_to_rate_table[it6616->ainfo.force_sample_freq]);
}

static DEVICE_ATTR_RO(audio_present);
static DEVICE_ATTR_RO(audio_rate);

static int it6616_create_class_attr(struct it6616 *it6616)
{
	int ret = 0;

	it6616->hdmirx_class = class_create(THIS_MODULE, "hdmirx_it6616");
	if (IS_ERR(it6616->hdmirx_class)) {
		ret = -ENOMEM;
		dev_err(it6616->dev, "failed to create hdmirx_it6616 class!\n");
		return ret;
	}

	it6616->classdev = device_create(it6616->hdmirx_class, NULL,
					MKDEV(0, 0), NULL, "hdmirx_it6616");
	if (IS_ERR(it6616->classdev)) {
		ret = PTR_ERR(it6616->classdev);
		dev_err(it6616->dev, "Failed to create device\n");
		goto err1;
	}

	ret = device_create_file(it6616->classdev,
				&dev_attr_audio_present);
	if (ret) {
		dev_err(it6616->dev, "failed to create attr audio_present!\n");
		goto err1;
	}

	ret = device_create_file(it6616->classdev,
				&dev_attr_audio_rate);
	if (ret) {
		dev_err(it6616->dev,
			"failed to create attr audio_rate!\n");
		goto err;
	}

	return ret;

err:
	device_remove_file(it6616->classdev, &dev_attr_audio_present);
err1:
	class_destroy(it6616->hdmirx_class);
	return ret;
}

static void it6616_remove_class_attr(struct it6616 *it6616)
{
	device_remove_file(it6616->classdev, &dev_attr_audio_rate);
	device_remove_file(it6616->classdev, &dev_attr_audio_present);
	class_destroy(it6616->hdmirx_class);
}

static int it6616_probe(struct i2c_client *client,
			  const struct i2c_device_id *id)
{
	struct it6616 *it6616;
	struct v4l2_subdev *sd;
	struct device *dev = &client->dev;
	char facing[2];
	int err = 0;

	dev_info(dev, "driver version: %02x.%02x.%02x",
		DRIVER_VERSION >> 16,
		(DRIVER_VERSION & 0xff00) >> 8,
		DRIVER_VERSION & 0x00ff);

	it6616 = devm_kzalloc(dev, sizeof(struct it6616), GFP_KERNEL);
	if (!it6616)
		return -ENOMEM;

	sd = &it6616->sd;
	it6616->hdmi_i2c = client;
	it6616->mipi_i2c = i2c_new_dummy_device(client->adapter,
						I2C_ADR_MIPI >> 1);

	if (!it6616->mipi_i2c)
		return -EIO;

	it6616->edid_i2c = i2c_new_dummy_device(client->adapter,
						I2C_ADR_EDID >> 1);

	if (!it6616->edid_i2c)
		goto unregister_mipi_i2c;

	it6616->hdmi_regmap = devm_regmap_init_i2c(client,
				&it6616_hdmi_regmap_config);
	if (IS_ERR(it6616->hdmi_i2c)) {
		err = PTR_ERR(it6616->hdmi_i2c);
		goto unregister_edid_i2c;
	}

	it6616->mipi_regmap = devm_regmap_init_i2c(it6616->mipi_i2c,
						&it6616_mipi_regmap_config);
	if (IS_ERR(it6616->mipi_regmap)) {
		err = PTR_ERR(it6616->mipi_regmap);
		goto unregister_edid_i2c;
	}

	it6616->edid_regmap = devm_regmap_init_i2c(it6616->edid_i2c,
						&it6616_edid_regmap_config);
	if (IS_ERR(it6616->edid_regmap)) {
		err = PTR_ERR(it6616->edid_regmap);
		goto unregister_edid_i2c;
	}

	it6616->dev = dev;
	it6616->cur_mode = &supported_modes[0];
	it6616->i2c_client = client;
	it6616->mbus_fmt_code = IT6616_MEDIA_BUS_FMT;
	err = it6616_probe_of(it6616);
	if (err) {
		v4l2_err(sd, "it6616_parse_of failed! err:%d\n", err);
		return err;
	}
	it6616_reset(it6616);

	mutex_init(&it6616->confctl_mutex);

	err = it6616_initial(it6616);
	if (err) {
		dev_err(dev, "it6616_initial failed: %d", err);
		err = -ENODEV;
		goto unregister_edid_i2c;
	}

	err = sysfs_create_files(&client->dev.kobj, it6616_attrs);
	if (err) {
		dev_err(dev, "sysfs_create_files failed: %d", err);
		goto unregister_edid_i2c;
	}

	i2c_set_clientdata(client, it6616);
	usleep_range(3000, 6000);

	err = it6616_init_v4l2_ctrls(it6616);
	if (err)
		goto err_free_hdl;

	client->flags |= I2C_CLIENT_SCCB;
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	v4l2_i2c_subdev_init(sd, client, &it6616_ops);
	sd->internal_ops = &it6616_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE | V4L2_SUBDEV_FL_HAS_EVENTS;
#endif

#if defined(CONFIG_MEDIA_CONTROLLER)
	it6616->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	err = media_entity_pads_init(&sd->entity, 1, &it6616->pad);
	if (err < 0) {
		v4l2_err(sd, "media entity init failed! err:%d\n", err);
		goto err_free_hdl;
	}
#endif
	memset(facing, 0, sizeof(facing));
	if (strcmp(it6616->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 it6616->module_index, facing,
		 IT6616_NAME, dev_name(sd->dev));
	err = v4l2_async_register_subdev_sensor_common(sd);
	if (err < 0) {
		v4l2_err(sd, "v4l2 register subdev failed! err:%d\n", err);
		goto err_clean_entity;
	}

	err = it6616_create_class_attr(it6616);
	if (err) {
		dev_err(it6616->dev, "create class attr failed! err:%d\n", err);
		return -ENODEV;
	}

	INIT_DELAYED_WORK(&it6616->work_i2c_poll, it6616_work_i2c_poll);
	schedule_delayed_work(&it6616->work_i2c_poll, msecs_to_jiffies(500));
	err = v4l2_ctrl_handler_setup(sd->ctrl_handler);
	if (err) {
		v4l2_err(sd, "v4l2 ctrl handler setup failed! err:%d\n", err);
		goto err_work_queues;
	}

	v4l2_info(sd, "%s found @ 0x%x (%s)\n", client->name,
			client->addr << 1, client->adapter->name);
	g_it6616 = it6616;

	return 0;

err_work_queues:
	cancel_delayed_work_sync(&it6616->work_i2c_poll);
err_clean_entity:
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
err_free_hdl:
	v4l2_ctrl_handler_free(&it6616->hdl);
	mutex_destroy(&it6616->confctl_mutex);
unregister_edid_i2c:
	i2c_unregister_device(it6616->edid_i2c);
unregister_mipi_i2c:
	i2c_unregister_device(it6616->mipi_i2c);

	return err;
}

static int it6616_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct it6616 *it6616 = to_it6616(sd);

	cancel_delayed_work_sync(&it6616->work_i2c_poll);
	it6616_remove_class_attr(it6616);
	v4l2_async_unregister_subdev(sd);
	v4l2_device_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	v4l2_ctrl_handler_free(&it6616->hdl);
	sysfs_remove_files(&client->dev.kobj, it6616_attrs);
	mutex_destroy(&it6616->confctl_mutex);

	i2c_unregister_device(it6616->mipi_i2c);
	i2c_unregister_device(it6616->edid_i2c);
	clk_disable_unprepare(it6616->xvclk);

	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id it6616_of_match[] = {
	{ .compatible = "ite,it6616" },
	{},
};
MODULE_DEVICE_TABLE(of, it6616_of_match);
#endif

static struct i2c_driver it6616_driver = {
	.driver = {
		.name = IT6616_NAME,
		.of_match_table = of_match_ptr(it6616_of_match),
	},
	.probe = it6616_probe,
	.remove = it6616_remove,
};

static int __init it6616_driver_init(void)
{
	return i2c_add_driver(&it6616_driver);
}

static void __exit it6616_driver_exit(void)
{
	i2c_del_driver(&it6616_driver);
}

device_initcall_sync(it6616_driver_init);
module_exit(it6616_driver_exit);

MODULE_DESCRIPTION("ITE it6616 HDMI to CSI-2 bridge driver");
MODULE_AUTHOR("Jianwei Fan <jianwei.fan@rock-chips.com>");
MODULE_AUTHOR("Kenneth Hung<Kenneth.Hung@ite.com.tw>");
MODULE_LICENSE("GPL");
