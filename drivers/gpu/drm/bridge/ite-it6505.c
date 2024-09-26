// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 */
#include <linux/bits.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/extcon.h>
#include <linux/fs.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/types.h>
#include <linux/wait.h>

#include <crypto/hash.h>

#include <drm/display/drm_dp_helper.h>
#include <drm/display/drm_hdcp_helper.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_crtc.h>
#include <drm/drm_edid.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>

#include <sound/hdmi-codec.h>

#define REG_IC_VER 0x04

#define REG_RESET_CTRL 0x05
#define VIDEO_RESET BIT(0)
#define AUDIO_RESET BIT(1)
#define ALL_LOGIC_RESET BIT(2)
#define AUX_RESET BIT(3)
#define HDCP_RESET BIT(4)

#define INT_STATUS_01 0x06
#define INT_MASK_01 0x09
#define INT_HPD_CHANGE 0
#define INT_RECEIVE_HPD_IRQ 1
#define INT_SCDT_CHANGE 2
#define INT_HDCP_FAIL 3
#define INT_HDCP_DONE 4
#define BIT_OFFSET(x) (((x) - INT_STATUS_01) * BITS_PER_BYTE)
#define BIT_INT_HPD INT_HPD_CHANGE
#define BIT_INT_HPD_IRQ INT_RECEIVE_HPD_IRQ
#define BIT_INT_SCDT INT_SCDT_CHANGE
#define BIT_INT_HDCP_FAIL INT_HDCP_FAIL
#define BIT_INT_HDCP_DONE INT_HDCP_DONE

#define INT_STATUS_02 0x07
#define INT_MASK_02 0x0A
#define INT_AUX_CMD_FAIL 0
#define INT_HDCP_KSV_CHECK 1
#define INT_AUDIO_FIFO_ERROR 2
#define BIT_INT_AUX_CMD_FAIL (BIT_OFFSET(0x07) + INT_AUX_CMD_FAIL)
#define BIT_INT_HDCP_KSV_CHECK (BIT_OFFSET(0x07) + INT_HDCP_KSV_CHECK)
#define BIT_INT_AUDIO_FIFO_ERROR (BIT_OFFSET(0x07) + INT_AUDIO_FIFO_ERROR)

#define INT_STATUS_03 0x08
#define INT_MASK_03 0x0B
#define INT_LINK_TRAIN_FAIL 4
#define INT_VID_FIFO_ERROR 5
#define INT_IO_LATCH_FIFO_OVERFLOW 7
#define BIT_INT_LINK_TRAIN_FAIL (BIT_OFFSET(0x08) + INT_LINK_TRAIN_FAIL)
#define BIT_INT_VID_FIFO_ERROR (BIT_OFFSET(0x08) + INT_VID_FIFO_ERROR)
#define BIT_INT_IO_FIFO_OVERFLOW (BIT_OFFSET(0x08) + INT_IO_LATCH_FIFO_OVERFLOW)

#define REG_SYSTEM_STS 0x0D
#define INT_STS BIT(0)
#define HPD_STS BIT(1)
#define VIDEO_STB BIT(2)

#define REG_LINK_TRAIN_STS 0x0E
#define LINK_STATE_CR BIT(2)
#define LINK_STATE_EQ BIT(3)
#define LINK_STATE_NORP BIT(4)

#define REG_BANK_SEL 0x0F
#define REG_CLK_CTRL0 0x10
#define M_PCLK_DELAY 0x03

#define REG_AUX_OPT 0x11
#define AUX_AUTO_RST BIT(0)
#define AUX_FIX_FREQ BIT(3)

#define REG_DATA_CTRL0 0x12
#define VIDEO_LATCH_EDGE BIT(4)
#define ENABLE_PCLK_COUNTER BIT(7)

#define REG_PCLK_COUNTER_VALUE 0x13

#define REG_501_FIFO_CTRL 0x15
#define RST_501_FIFO BIT(1)

#define REG_TRAIN_CTRL0 0x16
#define FORCE_LBR BIT(0)
#define LANE_COUNT_MASK 0x06
#define LANE_SWAP BIT(3)
#define SPREAD_AMP_5 BIT(4)
#define FORCE_CR_DONE BIT(5)
#define FORCE_EQ_DONE BIT(6)

#define REG_TRAIN_CTRL1 0x17
#define AUTO_TRAIN BIT(0)
#define MANUAL_TRAIN BIT(1)
#define FORCE_RETRAIN BIT(2)

#define REG_AUX_CTRL 0x23
#define CLR_EDID_FIFO BIT(0)
#define AUX_USER_MODE BIT(1)
#define AUX_NO_SEGMENT_WR BIT(6)
#define AUX_EN_FIFO_READ BIT(7)

#define REG_AUX_ADR_0_7 0x24
#define REG_AUX_ADR_8_15 0x25
#define REG_AUX_ADR_16_19 0x26
#define REG_AUX_OUT_DATA0 0x27

#define REG_AUX_CMD_REQ 0x2B
#define AUX_BUSY BIT(5)

#define REG_AUX_DATA_0_7 0x2C
#define REG_AUX_DATA_8_15 0x2D
#define REG_AUX_DATA_16_23 0x2E
#define REG_AUX_DATA_24_31 0x2F

#define REG_AUX_DATA_FIFO 0x2F

#define REG_AUX_ERROR_STS 0x9F
#define M_AUX_REQ_FAIL 0x03

#define REG_HDCP_CTRL1 0x38
#define HDCP_CP_ENABLE BIT(0)

#define REG_HDCP_TRIGGER 0x39
#define HDCP_TRIGGER_START  BIT(0)
#define HDCP_TRIGGER_CPIRQ  BIT(1)
#define HDCP_TRIGGER_KSV_DONE  BIT(4)
#define HDCP_TRIGGER_KSV_FAIL BIT(5)

#define REG_HDCP_CTRL2 0x3A
#define HDCP_AN_SEL BIT(0)
#define HDCP_AN_GEN BIT(1)
#define HDCP_HW_HPDIRQ_ACT BIT(2)
#define HDCP_EN_M0_READ BIT(5)

#define REG_M0_0_7 0x4C
#define REG_AN_0_7 0x4C
#define REG_SP_CTRL0 0x58
#define REG_IP_CTRL1 0x59
#define REG_IP_CTRL2 0x5A

#define REG_LINK_DRV 0x5C
#define DRV_HS BIT(1)

#define REG_DRV_LN_DATA_SEL 0x5D

#define REG_AUX 0x5E

#define REG_VID_BUS_CTRL0 0x60
#define IN_DDR BIT(2)
#define DDR_CD (0x01 << 6)

#define REG_VID_BUS_CTRL1 0x61
#define TX_FIFO_RESET BIT(1)

#define REG_INPUT_CTRL 0xA0
#define INPUT_HSYNC_POL BIT(0)
#define INPUT_VSYNC_POL BIT(2)
#define INPUT_INTERLACED BIT(4)

#define REG_INPUT_HTOTAL 0xA1
#define REG_INPUT_HACTIVE_START 0xA3
#define REG_INPUT_HACTIVE_WIDTH 0xA5
#define REG_INPUT_HFRONT_PORCH 0xA7
#define REG_INPUT_HSYNC_WIDTH 0xA9
#define REG_INPUT_VTOTAL 0xAB
#define REG_INPUT_VACTIVE_START 0xAD
#define REG_INPUT_VACTIVE_WIDTH 0xAF
#define REG_INPUT_VFRONT_PORCH 0xB1
#define REG_INPUT_VSYNC_WIDTH 0xB3

#define REG_AUDIO_SRC_CTRL 0xB8
#define M_AUDIO_I2S_EN 0x0F
#define EN_I2S0 BIT(0)
#define EN_I2S1 BIT(1)
#define EN_I2S2 BIT(2)
#define EN_I2S3 BIT(3)
#define AUDIO_FIFO_RESET BIT(7)

#define REG_AUDIO_FMT 0xB9
#define REG_AUDIO_FIFO_SEL 0xBA

#define REG_AUDIO_CTRL0 0xBB
#define AUDIO_FULL_PKT BIT(4)
#define AUDIO_16B_BOUND BIT(5)

#define REG_AUDIO_CTRL1 0xBC
#define REG_AUDIO_INPUT_FREQ 0xBE

#define REG_IEC958_STS0 0xBF
#define REG_IEC958_STS1 0xC0
#define REG_IEC958_STS2 0xC1
#define REG_IEC958_STS3 0xC2
#define REG_IEC958_STS4 0xC3

#define REG_HPD_IRQ_TIME 0xC9
#define REG_AUX_DEBUG_MODE 0xCA
#define REG_AUX_OPT2 0xCB
#define REG_HDCP_OPT 0xCE
#define REG_USER_DRV_PRE 0xCF

#define REG_DATA_MUTE_CTRL 0xD3
#define ENABLE_ENHANCED_FRAME BIT(0)
#define ENABLE_AUTO_VIDEO_FIFO_RESET BIT(1)
#define EN_VID_MUTE BIT(4)
#define EN_AUD_MUTE BIT(5)

#define REG_TIME_STMP_CTRL 0xD4
#define EN_ENHANCE_VID_STMP BIT(0)
#define EN_ENHANCE_AUD_STMP BIT(2)
#define M_STAMP_STEP 0x30
#define EN_SSC_GAT BIT(6)

#define REG_INFOFRAME_CTRL 0xE8
#define EN_AVI_PKT BIT(0)
#define EN_AUD_PKT BIT(1)
#define EN_MPG_PKT BIT(2)
#define EN_GEN_PKT BIT(3)
#define EN_VID_TIME_STMP BIT(4)
#define EN_AUD_TIME_STMP BIT(5)
#define EN_VID_CTRL_PKT (EN_AVI_PKT | EN_VID_TIME_STMP)
#define EN_AUD_CTRL_PKT (EN_AUD_PKT | EN_AUD_TIME_STMP)

#define REG_AUDIO_N_0_7 0xDE
#define REG_AUDIO_N_8_15 0xDF
#define REG_AUDIO_N_16_23 0xE0

#define REG_AVI_INFO_DB1 0xE9
#define REG_AVI_INFO_DB2 0xEA
#define REG_AVI_INFO_DB3 0xEB
#define REG_AVI_INFO_DB4 0xEC
#define REG_AVI_INFO_DB5 0xED
#define REG_AVI_INFO_SUM 0xF6

#define REG_AUD_INFOFRAM_DB1 0xF7
#define REG_AUD_INFOFRAM_DB2 0xF8
#define REG_AUD_INFOFRAM_DB3 0xF9
#define REG_AUD_INFOFRAM_DB4 0xFA
#define REG_AUD_INFOFRAM_SUM 0xFB

/* the following six registers are in bank1 */
#define REG_DRV_0_DB_800_MV 0x17E
#define REG_PRE_0_DB_800_MV 0x17F
#define REG_PRE_3P5_DB_800_MV 0x181
#define REG_SSC_CTRL0 0x188
#define REG_SSC_CTRL1 0x189
#define REG_SSC_CTRL2 0x18A

#define RBR DP_LINK_BW_1_62
#define HBR DP_LINK_BW_2_7
#define HBR2 DP_LINK_BW_5_4
#define HBR3 DP_LINK_BW_8_1

#define DPCD_V_1_1 0x11
#define MISC_VERB 0xF0
#define MISC_VERC 0x70
#define I2S_INPUT_FORMAT_STANDARD 0
#define I2S_INPUT_FORMAT_32BIT 1
#define I2S_INPUT_LEFT_JUSTIFIED 0
#define I2S_INPUT_RIGHT_JUSTIFIED 1
#define I2S_DATA_1T_DELAY 0
#define I2S_DATA_NO_DELAY 1
#define I2S_WS_LEFT_CHANNEL 0
#define I2S_WS_RIGHT_CHANNEL 1
#define I2S_DATA_MSB_FIRST 0
#define I2S_DATA_LSB_FIRST 1
#define WORD_LENGTH_16BIT 0
#define WORD_LENGTH_18BIT 1
#define WORD_LENGTH_20BIT 2
#define WORD_LENGTH_24BIT 3
#define DEBUGFS_DIR_NAME "it6505-debugfs"
#define READ_BUFFER_SIZE 400

/* Vendor option */
#define HDCP_DESIRED 1
#define MAX_LANE_COUNT 4
#define MAX_LINK_RATE HBR
#define AUTO_TRAIN_RETRY 3
#define MAX_HDCP_DOWN_STREAM_COUNT 10
#define MAX_CR_LEVEL 0x03
#define MAX_EQ_LEVEL 0x03
#define AUX_WAIT_TIMEOUT_MS 15
#define AUX_FIFO_MAX_SIZE 32
#define PIXEL_CLK_DELAY 1
#define PIXEL_CLK_INVERSE 0
#define ADJUST_PHASE_THRESHOLD 80000
#define DPI_PIXEL_CLK_MAX 95000
#define HDCP_SHA1_FIFO_LEN (MAX_HDCP_DOWN_STREAM_COUNT * 5 + 10)
#define DEFAULT_PWR_ON 0
#define DEFAULT_DRV_HOLD 0

#define AUDIO_SELECT I2S
#define AUDIO_TYPE LPCM
#define AUDIO_SAMPLE_RATE SAMPLE_RATE_48K
#define AUDIO_CHANNEL_COUNT 2
#define I2S_INPUT_FORMAT I2S_INPUT_FORMAT_32BIT
#define I2S_JUSTIFIED I2S_INPUT_LEFT_JUSTIFIED
#define I2S_DATA_DELAY I2S_DATA_1T_DELAY
#define I2S_WS_CHANNEL I2S_WS_LEFT_CHANNEL
#define I2S_DATA_SEQUENCE I2S_DATA_MSB_FIRST
#define AUDIO_WORD_LENGTH WORD_LENGTH_24BIT

enum aux_cmd_type {
	CMD_AUX_NATIVE_READ = 0x0,
	CMD_AUX_NATIVE_WRITE = 0x5,
	CMD_AUX_I2C_EDID_READ = 0xB,
};

enum aux_cmd_reply {
	REPLY_ACK,
	REPLY_NACK,
	REPLY_DEFER,
};

enum link_train_status {
	LINK_IDLE,
	LINK_BUSY,
	LINK_OK,
};

enum hdcp_state {
	HDCP_AUTH_IDLE,
	HDCP_AUTH_GOING,
	HDCP_AUTH_DONE,
};

struct it6505_platform_data {
	struct regulator *pwr18;
	struct regulator *ovdd;
	struct gpio_desc *gpiod_reset;
};

enum it6505_audio_select {
	I2S = 0,
	SPDIF,
};

enum it6505_audio_sample_rate {
	SAMPLE_RATE_24K = 0x6,
	SAMPLE_RATE_32K = 0x3,
	SAMPLE_RATE_48K = 0x2,
	SAMPLE_RATE_96K = 0xA,
	SAMPLE_RATE_192K = 0xE,
	SAMPLE_RATE_44_1K = 0x0,
	SAMPLE_RATE_88_2K = 0x8,
	SAMPLE_RATE_176_4K = 0xC,
};

enum it6505_audio_type {
	LPCM = 0,
	NLPCM,
	DSS,
};

struct it6505_audio_data {
	enum it6505_audio_select select;
	enum it6505_audio_sample_rate sample_rate;
	enum it6505_audio_type type;
	u8 word_length;
	u8 channel_count;
	u8 i2s_input_format;
	u8 i2s_justified;
	u8 i2s_data_delay;
	u8 i2s_ws_channel;
	u8 i2s_data_sequence;
};

struct it6505_audio_sample_rate_map {
	enum it6505_audio_sample_rate rate;
	int sample_rate_value;
};

struct it6505_drm_dp_link {
	unsigned char revision;
	unsigned int rate;
	unsigned int num_lanes;
	unsigned long capabilities;
};

struct debugfs_entries {
	char *name;
	const struct file_operations *fops;
};

struct it6505 {
	struct drm_dp_aux aux;
	struct drm_bridge bridge;
	struct device *dev;
	struct it6505_drm_dp_link link;
	struct it6505_platform_data pdata;
	/*
	 * Mutex protects extcon and interrupt functions from interfering
	 * each other.
	 */
	struct mutex extcon_lock;
	struct mutex mode_lock; /* used to bridge_detect */
	struct mutex aux_lock; /* used to aux data transfers */
	struct regmap *regmap;
	struct drm_display_mode source_output_mode;
	struct drm_display_mode video_info;
	struct notifier_block event_nb;
	struct extcon_dev *extcon;
	struct work_struct extcon_wq;
	int extcon_state;
	enum drm_connector_status connector_status;
	enum link_train_status link_state;
	struct work_struct link_works;
	u8 dpcd[DP_RECEIVER_CAP_SIZE];
	u8 lane_count;
	u8 link_rate_bw_code;
	u8 sink_count;
	bool step_train;
	bool branch_device;
	bool enable_ssc;
	bool lane_swap_disabled;
	bool lane_swap;
	bool powered;
	bool hpd_state;
	u32 afe_setting;
	u32 max_dpi_pixel_clock;
	u32 max_lane_count;
	enum hdcp_state hdcp_status;
	struct delayed_work hdcp_work;
	struct work_struct hdcp_wait_ksv_list;
	struct completion extcon_completion;
	u8 auto_train_retry;
	bool hdcp_desired;
	bool is_repeater;
	u8 hdcp_down_stream_count;
	u8 bksvs[DRM_HDCP_KSV_LEN];
	u8 sha1_input[HDCP_SHA1_FIFO_LEN];
	bool enable_enhanced_frame;
	hdmi_codec_plugged_cb plugged_cb;
	struct device *codec_dev;
	struct delayed_work delayed_audio;
	struct it6505_audio_data audio;
	struct dentry *debugfs;

	/* it6505 driver hold option */
	bool enable_drv_hold;

	const struct drm_edid *cached_edid;

	int irq;
};

struct it6505_step_train_para {
	u8 voltage_swing[MAX_LANE_COUNT];
	u8 pre_emphasis[MAX_LANE_COUNT];
};

/*
 * Vendor option afe settings for different platforms
 * 0: without FPC cable
 * 1: with FPC cable
 */

static const u8 afe_setting_table[][3] = {
	{0x82, 0x00, 0x45},
	{0x93, 0x2A, 0x85}
};

static const struct it6505_audio_sample_rate_map audio_sample_rate_map[] = {
	{SAMPLE_RATE_24K, 24000},
	{SAMPLE_RATE_32K, 32000},
	{SAMPLE_RATE_48K, 48000},
	{SAMPLE_RATE_96K, 96000},
	{SAMPLE_RATE_192K, 192000},
	{SAMPLE_RATE_44_1K, 44100},
	{SAMPLE_RATE_88_2K, 88200},
	{SAMPLE_RATE_176_4K, 176400},
};

static const struct regmap_range it6505_bridge_volatile_ranges[] = {
	{ .range_min = 0, .range_max = 0x1FF },
};

static const struct regmap_access_table it6505_bridge_volatile_table = {
	.yes_ranges = it6505_bridge_volatile_ranges,
	.n_yes_ranges = ARRAY_SIZE(it6505_bridge_volatile_ranges),
};

static const struct regmap_range_cfg it6505_regmap_banks[] = {
	{
		.name = "it6505",
		.range_min = 0x00,
		.range_max = 0x1FF,
		.selector_reg = REG_BANK_SEL,
		.selector_mask = 0x1,
		.selector_shift = 0,
		.window_start = 0x00,
		.window_len = 0x100,
	},
};

static const struct regmap_config it6505_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.volatile_table = &it6505_bridge_volatile_table,
	.cache_type = REGCACHE_NONE,
	.ranges = it6505_regmap_banks,
	.num_ranges = ARRAY_SIZE(it6505_regmap_banks),
	.max_register = 0x1FF,
};

static int it6505_read(struct it6505 *it6505, unsigned int reg_addr)
{
	unsigned int value;
	int err;
	struct device *dev = it6505->dev;

	if (!it6505->powered)
		return -ENODEV;

	err = regmap_read(it6505->regmap, reg_addr, &value);
	if (err < 0) {
		dev_err(dev, "read failed reg[0x%x] err: %d", reg_addr, err);
		return err;
	}

	return value;
}

static int it6505_write(struct it6505 *it6505, unsigned int reg_addr,
			unsigned int reg_val)
{
	int err;
	struct device *dev = it6505->dev;

	if (!it6505->powered)
		return -ENODEV;

	err = regmap_write(it6505->regmap, reg_addr, reg_val);

	if (err < 0) {
		dev_err(dev, "write failed reg[0x%x] = 0x%x err = %d",
			reg_addr, reg_val, err);
		return err;
	}

	return 0;
}

static int it6505_set_bits(struct it6505 *it6505, unsigned int reg,
			   unsigned int mask, unsigned int value)
{
	int err;
	struct device *dev = it6505->dev;

	if (!it6505->powered)
		return -ENODEV;

	err = regmap_update_bits(it6505->regmap, reg, mask, value);
	if (err < 0) {
		dev_err(dev, "write reg[0x%x] = 0x%x mask = 0x%x failed err %d",
			reg, value, mask, err);
		return err;
	}

	return 0;
}

static void it6505_debug_print(struct it6505 *it6505, unsigned int reg,
			       const char *prefix)
{
	struct device *dev = it6505->dev;
	int val;

	if (!drm_debug_enabled(DRM_UT_DRIVER))
		return;

	val = it6505_read(it6505, reg);
	if (val < 0)
		DRM_DEV_DEBUG_DRIVER(dev, "%s reg[%02x] read error (%d)",
				     prefix, reg, val);
	else
		DRM_DEV_DEBUG_DRIVER(dev, "%s reg[%02x] = 0x%02x", prefix, reg,
				     val);
}

static int it6505_dpcd_read(struct it6505 *it6505, unsigned long offset)
{
	u8 value;
	int ret;
	struct device *dev = it6505->dev;

	ret = drm_dp_dpcd_readb(&it6505->aux, offset, &value);
	if (ret < 0) {
		dev_err(dev, "DPCD read failed [0x%lx] ret: %d", offset, ret);
		return ret;
	}
	return value;
}

static int it6505_dpcd_write(struct it6505 *it6505, unsigned long offset,
			     u8 datain)
{
	int ret;
	struct device *dev = it6505->dev;

	ret = drm_dp_dpcd_writeb(&it6505->aux, offset, datain);
	if (ret < 0) {
		dev_err(dev, "DPCD write failed [0x%lx] ret: %d", offset, ret);
		return ret;
	}
	return 0;
}

static int it6505_get_dpcd(struct it6505 *it6505, int offset, u8 *dpcd, int num)
{
	int ret;
	struct device *dev = it6505->dev;

	ret = drm_dp_dpcd_read(&it6505->aux, offset, dpcd, num);

	if (ret < 0)
		return ret;

	DRM_DEV_DEBUG_DRIVER(dev, "ret = %d DPCD[0x%x] = 0x%*ph", ret, offset,
			     num, dpcd);

	return 0;
}

static void it6505_dump(struct it6505 *it6505)
{
	unsigned int i, j;
	u8 regs[16];
	struct device *dev = it6505->dev;

	for (i = 0; i <= 0xff; i += 16) {
		for (j = 0; j < 16; j++)
			regs[j] = it6505_read(it6505, i + j);

		DRM_DEV_DEBUG_DRIVER(dev, "[0x%02x] = %16ph", i, regs);
	}
}

static bool it6505_get_sink_hpd_status(struct it6505 *it6505)
{
	int reg_0d;

	reg_0d = it6505_read(it6505, REG_SYSTEM_STS);

	if (reg_0d < 0)
		return false;

	return reg_0d & HPD_STS;
}

static int it6505_read_word(struct it6505 *it6505, unsigned int reg)
{
	int val0, val1;

	val0 = it6505_read(it6505, reg);
	if (val0 < 0)
		return val0;

	val1 = it6505_read(it6505, reg + 1);
	if (val1 < 0)
		return val1;

	return (val1 << 8) | val0;
}

static void it6505_calc_video_info(struct it6505 *it6505)
{
	struct device *dev = it6505->dev;
	int hsync_pol, vsync_pol, interlaced;
	int htotal, hdes, hdew, hfph, hsyncw;
	int vtotal, vdes, vdew, vfph, vsyncw;
	int rddata, i, pclk, sum = 0;

	usleep_range(10000, 15000);
	rddata = it6505_read(it6505, REG_INPUT_CTRL);
	hsync_pol = rddata & INPUT_HSYNC_POL;
	vsync_pol = (rddata & INPUT_VSYNC_POL) >> 2;
	interlaced = (rddata & INPUT_INTERLACED) >> 4;

	htotal = it6505_read_word(it6505, REG_INPUT_HTOTAL) & 0x1FFF;
	hdes = it6505_read_word(it6505, REG_INPUT_HACTIVE_START) & 0x1FFF;
	hdew = it6505_read_word(it6505, REG_INPUT_HACTIVE_WIDTH) & 0x1FFF;
	hfph = it6505_read_word(it6505, REG_INPUT_HFRONT_PORCH) & 0x1FFF;
	hsyncw = it6505_read_word(it6505, REG_INPUT_HSYNC_WIDTH) & 0x1FFF;

	vtotal = it6505_read_word(it6505, REG_INPUT_VTOTAL) & 0xFFF;
	vdes = it6505_read_word(it6505, REG_INPUT_VACTIVE_START) & 0xFFF;
	vdew = it6505_read_word(it6505, REG_INPUT_VACTIVE_WIDTH) & 0xFFF;
	vfph = it6505_read_word(it6505, REG_INPUT_VFRONT_PORCH) & 0xFFF;
	vsyncw = it6505_read_word(it6505, REG_INPUT_VSYNC_WIDTH) & 0xFFF;

	DRM_DEV_DEBUG_DRIVER(dev, "hsync_pol:%d, vsync_pol:%d, interlaced:%d",
			     hsync_pol, vsync_pol, interlaced);
	DRM_DEV_DEBUG_DRIVER(dev, "hactive_start:%d, vactive_start:%d",
			     hdes, vdes);

	for (i = 0; i < 3; i++) {
		it6505_set_bits(it6505, REG_DATA_CTRL0, ENABLE_PCLK_COUNTER,
				ENABLE_PCLK_COUNTER);
		usleep_range(10000, 15000);
		it6505_set_bits(it6505, REG_DATA_CTRL0, ENABLE_PCLK_COUNTER,
				0x00);
		rddata = it6505_read_word(it6505, REG_PCLK_COUNTER_VALUE) &
			 0xFFF;

		sum += rddata;
	}

	if (sum == 0) {
		DRM_DEV_DEBUG_DRIVER(dev, "calc video timing error");
		return;
	}

	sum /= 3;
	pclk = 13500 * 2048 / sum;
	it6505->video_info.clock = pclk;
	it6505->video_info.hdisplay = hdew;
	it6505->video_info.hsync_start = hdew + hfph;
	it6505->video_info.hsync_end = hdew + hfph + hsyncw;
	it6505->video_info.htotal = htotal;
	it6505->video_info.vdisplay = vdew;
	it6505->video_info.vsync_start = vdew + vfph;
	it6505->video_info.vsync_end = vdew + vfph + vsyncw;
	it6505->video_info.vtotal = vtotal;

	DRM_DEV_DEBUG_DRIVER(dev, DRM_MODE_FMT,
			     DRM_MODE_ARG(&it6505->video_info));
}

static int it6505_drm_dp_link_set_power(struct drm_dp_aux *aux,
					struct it6505_drm_dp_link *link,
					u8 mode)
{
	u8 value;
	int err;

	/* DP_SET_POWER register is only available on DPCD v1.1 and later */
	if (link->revision < DPCD_V_1_1)
		return 0;

	err = drm_dp_dpcd_readb(aux, DP_SET_POWER, &value);
	if (err < 0)
		return err;

	value &= ~DP_SET_POWER_MASK;
	value |= mode;

	err = drm_dp_dpcd_writeb(aux, DP_SET_POWER, value);
	if (err < 0)
		return err;

	if (mode == DP_SET_POWER_D0) {
		/*
		 * According to the DP 1.1 specification, a "Sink Device must
		 * exit the power saving state within 1 ms" (Section 2.5.3.1,
		 * Table 5-52, "Sink Control Field" (register 0x600).
		 */
		usleep_range(1000, 2000);
	}

	return 0;
}

static void it6505_clear_int(struct it6505 *it6505)
{
	it6505_write(it6505, INT_STATUS_01, 0xFF);
	it6505_write(it6505, INT_STATUS_02, 0xFF);
	it6505_write(it6505, INT_STATUS_03, 0xFF);
}

static void it6505_int_mask_enable(struct it6505 *it6505)
{
	it6505_write(it6505, INT_MASK_01, BIT(INT_HPD_CHANGE) |
		     BIT(INT_RECEIVE_HPD_IRQ) | BIT(INT_SCDT_CHANGE) |
		     BIT(INT_HDCP_FAIL) | BIT(INT_HDCP_DONE));

	it6505_write(it6505, INT_MASK_02, BIT(INT_AUX_CMD_FAIL) |
		     BIT(INT_HDCP_KSV_CHECK) | BIT(INT_AUDIO_FIFO_ERROR));

	it6505_write(it6505, INT_MASK_03, BIT(INT_LINK_TRAIN_FAIL) |
		     BIT(INT_VID_FIFO_ERROR) | BIT(INT_IO_LATCH_FIFO_OVERFLOW));
}

static void it6505_int_mask_disable(struct it6505 *it6505)
{
	it6505_write(it6505, INT_MASK_01, 0x00);
	it6505_write(it6505, INT_MASK_02, 0x00);
	it6505_write(it6505, INT_MASK_03, 0x00);
}

static void it6505_lane_termination_on(struct it6505 *it6505)
{
	int regcf;

	regcf = it6505_read(it6505, REG_USER_DRV_PRE);

	if (regcf == MISC_VERB)
		it6505_set_bits(it6505, REG_DRV_LN_DATA_SEL, 0x80, 0x00);

	if (regcf == MISC_VERC) {
		if (it6505->lane_swap) {
			switch (it6505->lane_count) {
			case 1:
			case 2:
				it6505_set_bits(it6505, REG_DRV_LN_DATA_SEL,
						0x0C, 0x08);
				break;
			default:
				it6505_set_bits(it6505, REG_DRV_LN_DATA_SEL,
						0x0C, 0x0C);
				break;
			}
		} else {
			switch (it6505->lane_count) {
			case 1:
			case 2:
				it6505_set_bits(it6505, REG_DRV_LN_DATA_SEL,
						0x0C, 0x04);
				break;
			default:
				it6505_set_bits(it6505, REG_DRV_LN_DATA_SEL,
						0x0C, 0x0C);
				break;
			}
		}
	}
}

static void it6505_lane_termination_off(struct it6505 *it6505)
{
	int regcf;

	regcf = it6505_read(it6505, REG_USER_DRV_PRE);

	if (regcf == MISC_VERB)
		it6505_set_bits(it6505, REG_DRV_LN_DATA_SEL, 0x80, 0x80);

	if (regcf == MISC_VERC)
		it6505_set_bits(it6505, REG_DRV_LN_DATA_SEL, 0x0C, 0x00);
}

static void it6505_lane_power_on(struct it6505 *it6505)
{
	it6505_set_bits(it6505, REG_LINK_DRV, 0xF1,
			(it6505->lane_swap ?
				 GENMASK(7, 8 - it6505->lane_count) :
				 GENMASK(3 + it6505->lane_count, 4)) |
				0x01);
}

static void it6505_lane_power_off(struct it6505 *it6505)
{
	it6505_set_bits(it6505, REG_LINK_DRV, 0xF0, 0x00);
}

static void it6505_lane_off(struct it6505 *it6505)
{
	it6505_lane_power_off(it6505);
	it6505_lane_termination_off(it6505);
}

static void it6505_aux_termination_on(struct it6505 *it6505)
{
	int regcf;

	regcf = it6505_read(it6505, REG_USER_DRV_PRE);

	if (regcf == MISC_VERB)
		it6505_lane_termination_on(it6505);

	if (regcf == MISC_VERC)
		it6505_set_bits(it6505, REG_DRV_LN_DATA_SEL, 0x80, 0x80);
}

static void it6505_aux_power_on(struct it6505 *it6505)
{
	it6505_set_bits(it6505, REG_AUX, 0x02, 0x02);
}

static void it6505_aux_on(struct it6505 *it6505)
{
	it6505_aux_power_on(it6505);
	it6505_aux_termination_on(it6505);
}

static void it6505_aux_reset(struct it6505 *it6505)
{
	it6505_set_bits(it6505, REG_RESET_CTRL, AUX_RESET, AUX_RESET);
	it6505_set_bits(it6505, REG_RESET_CTRL, AUX_RESET, 0x00);
}

static void it6505_reset_logic(struct it6505 *it6505)
{
	regmap_write(it6505->regmap, REG_RESET_CTRL, ALL_LOGIC_RESET);
	usleep_range(1000, 1500);
}

static bool it6505_aux_op_finished(struct it6505 *it6505)
{
	int reg2b = it6505_read(it6505, REG_AUX_CMD_REQ);

	if (reg2b < 0)
		return false;

	return (reg2b & AUX_BUSY) == 0;
}

static int it6505_aux_wait(struct it6505 *it6505)
{
	int status;
	unsigned long timeout;
	struct device *dev = it6505->dev;

	timeout = jiffies + msecs_to_jiffies(AUX_WAIT_TIMEOUT_MS) + 1;

	while (!it6505_aux_op_finished(it6505)) {
		if (time_after(jiffies, timeout)) {
			dev_err(dev, "Timed out waiting AUX to finish");
			return -ETIMEDOUT;
		}
		usleep_range(1000, 2000);
	}

	status = it6505_read(it6505, REG_AUX_ERROR_STS);
	if (status < 0) {
		dev_err(dev, "Failed to read AUX channel: %d", status);
		return status;
	}

	return 0;
}

static ssize_t it6505_aux_operation(struct it6505 *it6505,
				    enum aux_cmd_type cmd,
				    unsigned int address, u8 *buffer,
				    size_t size, enum aux_cmd_reply *reply)
{
	int i, ret;
	bool aux_write_check = false;

	if (!it6505_get_sink_hpd_status(it6505))
		return -EIO;

	/* set AUX user mode */
	it6505_set_bits(it6505, REG_AUX_CTRL, AUX_USER_MODE, AUX_USER_MODE);

aux_op_start:
	if (cmd == CMD_AUX_I2C_EDID_READ) {
		/* AUX EDID FIFO has max length of AUX_FIFO_MAX_SIZE bytes. */
		size = min_t(size_t, size, AUX_FIFO_MAX_SIZE);
		/* Enable AUX FIFO read back and clear FIFO */
		it6505_set_bits(it6505, REG_AUX_CTRL,
				AUX_EN_FIFO_READ | CLR_EDID_FIFO,
				AUX_EN_FIFO_READ | CLR_EDID_FIFO);

		it6505_set_bits(it6505, REG_AUX_CTRL,
				AUX_EN_FIFO_READ | CLR_EDID_FIFO,
				AUX_EN_FIFO_READ);
	} else {
		/* The DP AUX transmit buffer has 4 bytes. */
		size = min_t(size_t, size, 4);
		it6505_set_bits(it6505, REG_AUX_CTRL, AUX_NO_SEGMENT_WR,
				AUX_NO_SEGMENT_WR);
	}

	/* Start Address[7:0] */
	it6505_write(it6505, REG_AUX_ADR_0_7, (address >> 0) & 0xFF);
	/* Start Address[15:8] */
	it6505_write(it6505, REG_AUX_ADR_8_15, (address >> 8) & 0xFF);
	/* WriteNum[3:0]+StartAdr[19:16] */
	it6505_write(it6505, REG_AUX_ADR_16_19,
		     ((address >> 16) & 0x0F) | ((size - 1) << 4));

	if (cmd == CMD_AUX_NATIVE_WRITE)
		regmap_bulk_write(it6505->regmap, REG_AUX_OUT_DATA0, buffer,
				  size);

	/* Aux Fire */
	it6505_write(it6505, REG_AUX_CMD_REQ, cmd);

	ret = it6505_aux_wait(it6505);
	if (ret < 0)
		goto aux_op_err;

	ret = it6505_read(it6505, REG_AUX_ERROR_STS);
	if (ret < 0)
		goto aux_op_err;

	switch ((ret >> 6) & 0x3) {
	case 0:
		*reply = REPLY_ACK;
		break;
	case 1:
		*reply = REPLY_DEFER;
		ret = -EAGAIN;
		goto aux_op_err;
	case 2:
		*reply = REPLY_NACK;
		ret = -EIO;
		goto aux_op_err;
	case 3:
		ret = -ETIMEDOUT;
		goto aux_op_err;
	}

	/* Read back Native Write data */
	if (cmd == CMD_AUX_NATIVE_WRITE) {
		aux_write_check = true;
		cmd = CMD_AUX_NATIVE_READ;
		goto aux_op_start;
	}

	if (cmd == CMD_AUX_I2C_EDID_READ) {
		for (i = 0; i < size; i++) {
			ret = it6505_read(it6505, REG_AUX_DATA_FIFO);
			if (ret < 0)
				goto aux_op_err;
			buffer[i] = ret;
		}
	} else {
		for (i = 0; i < size; i++) {
			ret = it6505_read(it6505, REG_AUX_DATA_0_7 + i);
			if (ret < 0)
				goto aux_op_err;

			if (aux_write_check && buffer[size - 1 - i] != ret) {
				ret = -EINVAL;
				goto aux_op_err;
			}

			buffer[size - 1 - i] = ret;
		}
	}

	ret = i;

aux_op_err:
	if (cmd == CMD_AUX_I2C_EDID_READ) {
		/* clear AUX FIFO */
		it6505_set_bits(it6505, REG_AUX_CTRL,
				AUX_EN_FIFO_READ | CLR_EDID_FIFO,
				AUX_EN_FIFO_READ | CLR_EDID_FIFO);
		it6505_set_bits(it6505, REG_AUX_CTRL,
				AUX_EN_FIFO_READ | CLR_EDID_FIFO, 0x00);
	}

	/* Leave AUX user mode */
	it6505_set_bits(it6505, REG_AUX_CTRL, AUX_USER_MODE, 0);

	return ret;
}

static ssize_t it6505_aux_do_transfer(struct it6505 *it6505,
				      enum aux_cmd_type cmd,
				      unsigned int address, u8 *buffer,
				      size_t size, enum aux_cmd_reply *reply)
{
	int i, ret_size, ret = 0, request_size;

	mutex_lock(&it6505->aux_lock);
	for (i = 0; i < size; i += 4) {
		request_size = min((int)size - i, 4);
		ret_size = it6505_aux_operation(it6505, cmd, address + i,
						buffer + i, request_size,
						reply);
		if (ret_size < 0) {
			ret = ret_size;
			goto aux_op_err;
		}

		ret += ret_size;
	}

aux_op_err:
	mutex_unlock(&it6505->aux_lock);
	return ret;
}

static ssize_t it6505_aux_transfer(struct drm_dp_aux *aux,
				   struct drm_dp_aux_msg *msg)
{
	struct it6505 *it6505 = container_of(aux, struct it6505, aux);
	u8 cmd;
	bool is_i2c = !(msg->request & DP_AUX_NATIVE_WRITE);
	int ret;
	enum aux_cmd_reply reply;

	/* IT6505 doesn't support arbitrary I2C read / write. */
	if (is_i2c)
		return -EINVAL;

	switch (msg->request) {
	case DP_AUX_NATIVE_READ:
		cmd = CMD_AUX_NATIVE_READ;
		break;
	case DP_AUX_NATIVE_WRITE:
		cmd = CMD_AUX_NATIVE_WRITE;
		break;
	default:
		return -EINVAL;
	}

	ret = it6505_aux_do_transfer(it6505, cmd, msg->address, msg->buffer,
				     msg->size, &reply);
	if (ret < 0)
		return ret;

	switch (reply) {
	case REPLY_ACK:
		msg->reply = DP_AUX_NATIVE_REPLY_ACK;
		break;
	case REPLY_NACK:
		msg->reply = DP_AUX_NATIVE_REPLY_NACK;
		break;
	case REPLY_DEFER:
		msg->reply = DP_AUX_NATIVE_REPLY_DEFER;
		break;
	}

	return ret;
}

static int it6505_get_edid_block(void *data, u8 *buf, unsigned int block,
				 size_t len)
{
	struct it6505 *it6505 = data;
	struct device *dev = it6505->dev;
	enum aux_cmd_reply reply;
	int offset, ret, aux_retry = 100;

	it6505_aux_reset(it6505);
	DRM_DEV_DEBUG_DRIVER(dev, "block number = %d", block);

	for (offset = 0; offset < EDID_LENGTH;) {
		ret = it6505_aux_do_transfer(it6505, CMD_AUX_I2C_EDID_READ,
					     block * EDID_LENGTH + offset,
					     buf + offset, 8, &reply);

		if (ret < 0 && ret != -EAGAIN)
			return ret;

		switch (reply) {
		case REPLY_ACK:
			DRM_DEV_DEBUG_DRIVER(dev, "[0x%02x]: %8ph", offset,
					     buf + offset);
			offset += 8;
			aux_retry = 100;
			break;
		case REPLY_NACK:
			return -EIO;
		case REPLY_DEFER:
			msleep(20);
			if (!(--aux_retry))
				return -EIO;
		}
	}

	return 0;
}

static void it6505_variable_config(struct it6505 *it6505)
{
	it6505->link_rate_bw_code = HBR;
	it6505->lane_count = MAX_LANE_COUNT;
	it6505->link_state = LINK_IDLE;
	it6505->hdcp_desired = HDCP_DESIRED;
	it6505->auto_train_retry = AUTO_TRAIN_RETRY;
	it6505->audio.select = AUDIO_SELECT;
	it6505->audio.sample_rate = AUDIO_SAMPLE_RATE;
	it6505->audio.channel_count = AUDIO_CHANNEL_COUNT;
	it6505->audio.type = AUDIO_TYPE;
	it6505->audio.i2s_input_format = I2S_INPUT_FORMAT;
	it6505->audio.i2s_justified = I2S_JUSTIFIED;
	it6505->audio.i2s_data_delay = I2S_DATA_DELAY;
	it6505->audio.i2s_ws_channel = I2S_WS_CHANNEL;
	it6505->audio.i2s_data_sequence = I2S_DATA_SEQUENCE;
	it6505->audio.word_length = AUDIO_WORD_LENGTH;
	memset(it6505->sha1_input, 0, sizeof(it6505->sha1_input));
	memset(it6505->bksvs, 0, sizeof(it6505->bksvs));
}

static int it6505_send_video_infoframe(struct it6505 *it6505,
				       struct hdmi_avi_infoframe *frame)
{
	u8 buffer[HDMI_INFOFRAME_HEADER_SIZE + HDMI_AVI_INFOFRAME_SIZE];
	int err;
	struct device *dev = it6505->dev;

	err = hdmi_avi_infoframe_pack(frame, buffer, sizeof(buffer));
	if (err < 0) {
		dev_err(dev, "Failed to pack AVI infoframe: %d", err);
		return err;
	}

	err = it6505_set_bits(it6505, REG_INFOFRAME_CTRL, EN_AVI_PKT, 0x00);
	if (err)
		return err;

	err = regmap_bulk_write(it6505->regmap, REG_AVI_INFO_DB1,
				buffer + HDMI_INFOFRAME_HEADER_SIZE,
				frame->length);
	if (err)
		return err;

	err = it6505_set_bits(it6505, REG_INFOFRAME_CTRL, EN_AVI_PKT,
			      EN_AVI_PKT);
	if (err)
		return err;

	return 0;
}

static void it6505_get_extcon_property(struct it6505 *it6505)
{
	int err;
	union extcon_property_value property;
	struct device *dev = it6505->dev;

	if (it6505->extcon && !it6505->lane_swap_disabled) {
		err = extcon_get_property(it6505->extcon, EXTCON_DISP_DP,
					  EXTCON_PROP_USB_TYPEC_POLARITY,
					  &property);
		if (err) {
			dev_err(dev, "get property fail!");
			return;
		}
		it6505->lane_swap = property.intval;
	}
}

static void it6505_clk_phase_adjustment(struct it6505 *it6505,
					const struct drm_display_mode *mode)
{
	int clock = mode->clock;

	it6505_set_bits(it6505, REG_CLK_CTRL0, M_PCLK_DELAY,
			clock < ADJUST_PHASE_THRESHOLD ? PIXEL_CLK_DELAY : 0);
	it6505_set_bits(it6505, REG_DATA_CTRL0, VIDEO_LATCH_EDGE,
			PIXEL_CLK_INVERSE << 4);
}

static void it6505_link_reset_step_train(struct it6505 *it6505)
{
	it6505_set_bits(it6505, REG_TRAIN_CTRL0,
			FORCE_CR_DONE | FORCE_EQ_DONE, 0x00);
	it6505_dpcd_write(it6505, DP_TRAINING_PATTERN_SET,
			  DP_TRAINING_PATTERN_DISABLE);
}

static void it6505_init(struct it6505 *it6505)
{
	it6505_write(it6505, REG_AUX_OPT, AUX_AUTO_RST | AUX_FIX_FREQ);
	it6505_write(it6505, REG_AUX_CTRL, AUX_NO_SEGMENT_WR);
	it6505_write(it6505, REG_HDCP_CTRL2, HDCP_AN_SEL | HDCP_HW_HPDIRQ_ACT);
	it6505_write(it6505, REG_VID_BUS_CTRL0, IN_DDR | DDR_CD);
	it6505_write(it6505, REG_VID_BUS_CTRL1, 0x01);
	it6505_write(it6505, REG_AUDIO_CTRL0, AUDIO_16B_BOUND);

	/* chip internal setting, don't modify */
	it6505_write(it6505, REG_HPD_IRQ_TIME, 0xF5);
	it6505_write(it6505, REG_AUX_DEBUG_MODE, 0x4D);
	it6505_write(it6505, REG_AUX_OPT2, 0x17);
	it6505_write(it6505, REG_HDCP_OPT, 0x60);
	it6505_write(it6505, REG_DATA_MUTE_CTRL,
		     EN_VID_MUTE | EN_AUD_MUTE | ENABLE_AUTO_VIDEO_FIFO_RESET);
	it6505_write(it6505, REG_TIME_STMP_CTRL,
		     EN_SSC_GAT | EN_ENHANCE_VID_STMP | EN_ENHANCE_AUD_STMP);
	it6505_write(it6505, REG_INFOFRAME_CTRL, 0x00);
	it6505_write(it6505, REG_DRV_0_DB_800_MV,
		     afe_setting_table[it6505->afe_setting][0]);
	it6505_write(it6505, REG_PRE_0_DB_800_MV,
		     afe_setting_table[it6505->afe_setting][1]);
	it6505_write(it6505, REG_PRE_3P5_DB_800_MV,
		     afe_setting_table[it6505->afe_setting][2]);
	it6505_write(it6505, REG_SSC_CTRL0, 0x9E);
	it6505_write(it6505, REG_SSC_CTRL1, 0x1C);
	it6505_write(it6505, REG_SSC_CTRL2, 0x42);
}

static void it6505_video_disable(struct it6505 *it6505)
{
	it6505_set_bits(it6505, REG_DATA_MUTE_CTRL, EN_VID_MUTE, EN_VID_MUTE);
	it6505_set_bits(it6505, REG_INFOFRAME_CTRL, EN_VID_CTRL_PKT, 0x00);
	it6505_set_bits(it6505, REG_RESET_CTRL, VIDEO_RESET, VIDEO_RESET);
}

static void it6505_video_reset(struct it6505 *it6505)
{
	it6505_link_reset_step_train(it6505);
	it6505_set_bits(it6505, REG_DATA_MUTE_CTRL, EN_VID_MUTE, EN_VID_MUTE);
	it6505_set_bits(it6505, REG_INFOFRAME_CTRL, EN_VID_CTRL_PKT, 0x00);

	it6505_set_bits(it6505, REG_VID_BUS_CTRL1, TX_FIFO_RESET, TX_FIFO_RESET);
	it6505_set_bits(it6505, REG_VID_BUS_CTRL1, TX_FIFO_RESET, 0x00);

	it6505_set_bits(it6505, REG_501_FIFO_CTRL, RST_501_FIFO, RST_501_FIFO);
	it6505_set_bits(it6505, REG_501_FIFO_CTRL, RST_501_FIFO, 0x00);

	it6505_set_bits(it6505, REG_RESET_CTRL, VIDEO_RESET, VIDEO_RESET);
	usleep_range(1000, 2000);
	it6505_set_bits(it6505, REG_RESET_CTRL, VIDEO_RESET, 0x00);
}

static void it6505_update_video_parameter(struct it6505 *it6505,
					  const struct drm_display_mode *mode)
{
	it6505_clk_phase_adjustment(it6505, mode);
	it6505_video_disable(it6505);
}

static bool it6505_audio_input(struct it6505 *it6505)
{
	int reg05, regbe;

	reg05 = it6505_read(it6505, REG_RESET_CTRL);
	it6505_set_bits(it6505, REG_RESET_CTRL, AUDIO_RESET, 0x00);
	usleep_range(3000, 4000);
	regbe = it6505_read(it6505, REG_AUDIO_INPUT_FREQ);
	it6505_write(it6505, REG_RESET_CTRL, reg05);

	return regbe != 0xFF;
}

static void it6505_setup_audio_channel_status(struct it6505 *it6505)
{
	enum it6505_audio_sample_rate sample_rate = it6505->audio.sample_rate;
	u8 audio_word_length_map[] = { 0x02, 0x04, 0x03, 0x0B };

	/* Channel Status */
	it6505_write(it6505, REG_IEC958_STS0, it6505->audio.type << 1);
	it6505_write(it6505, REG_IEC958_STS1, 0x00);
	it6505_write(it6505, REG_IEC958_STS2, 0x00);
	it6505_write(it6505, REG_IEC958_STS3, sample_rate);
	it6505_write(it6505, REG_IEC958_STS4, (~sample_rate << 4) |
		     audio_word_length_map[it6505->audio.word_length]);
}

static void it6505_setup_audio_format(struct it6505 *it6505)
{
	/* I2S MODE */
	it6505_write(it6505, REG_AUDIO_FMT,
		     (it6505->audio.word_length << 5) |
		     (it6505->audio.i2s_data_sequence << 4) |
		     (it6505->audio.i2s_ws_channel << 3) |
		     (it6505->audio.i2s_data_delay << 2) |
		     (it6505->audio.i2s_justified << 1) |
		     it6505->audio.i2s_input_format);
	if (it6505->audio.select == SPDIF) {
		it6505_write(it6505, REG_AUDIO_FIFO_SEL, 0x00);
		/* 0x30 = 128*FS */
		it6505_set_bits(it6505, REG_AUX_OPT, 0xF0, 0x30);
	} else {
		it6505_write(it6505, REG_AUDIO_FIFO_SEL, 0xE4);
	}

	it6505_write(it6505, REG_AUDIO_CTRL0, 0x20);
	it6505_write(it6505, REG_AUDIO_CTRL1, 0x00);
}

static void it6505_enable_audio_source(struct it6505 *it6505)
{
	unsigned int audio_source_count;

	audio_source_count = BIT(DIV_ROUND_UP(it6505->audio.channel_count, 2))
				 - 1;

	audio_source_count |= it6505->audio.select << 4;

	it6505_write(it6505, REG_AUDIO_SRC_CTRL, audio_source_count);
}

static void it6505_enable_audio_infoframe(struct it6505 *it6505)
{
	struct device *dev = it6505->dev;
	u8 audio_info_ca[] = { 0x00, 0x00, 0x01, 0x03, 0x07, 0x0B, 0x0F, 0x1F };

	DRM_DEV_DEBUG_DRIVER(dev, "infoframe channel_allocation:0x%02x",
			     audio_info_ca[it6505->audio.channel_count - 1]);

	it6505_write(it6505, REG_AUD_INFOFRAM_DB1, it6505->audio.channel_count
		     - 1);
	it6505_write(it6505, REG_AUD_INFOFRAM_DB2, 0x00);
	it6505_write(it6505, REG_AUD_INFOFRAM_DB3,
		     audio_info_ca[it6505->audio.channel_count - 1]);
	it6505_write(it6505, REG_AUD_INFOFRAM_DB4, 0x00);
	it6505_write(it6505, REG_AUD_INFOFRAM_SUM, 0x00);

	/* Enable Audio InfoFrame */
	it6505_set_bits(it6505, REG_INFOFRAME_CTRL, EN_AUD_CTRL_PKT,
			EN_AUD_CTRL_PKT);
}

static void it6505_disable_audio(struct it6505 *it6505)
{
	it6505_set_bits(it6505, REG_DATA_MUTE_CTRL, EN_AUD_MUTE, EN_AUD_MUTE);
	it6505_set_bits(it6505, REG_AUDIO_SRC_CTRL, M_AUDIO_I2S_EN, 0x00);
	it6505_set_bits(it6505, REG_INFOFRAME_CTRL, EN_AUD_CTRL_PKT, 0x00);
	it6505_set_bits(it6505, REG_RESET_CTRL, AUDIO_RESET, AUDIO_RESET);
}

static void it6505_enable_audio(struct it6505 *it6505)
{
	struct device *dev = it6505->dev;
	int regbe;

	DRM_DEV_DEBUG_DRIVER(dev, "start");
	it6505_disable_audio(it6505);

	it6505_setup_audio_channel_status(it6505);
	it6505_setup_audio_format(it6505);
	it6505_enable_audio_source(it6505);
	it6505_enable_audio_infoframe(it6505);

	it6505_write(it6505, REG_AUDIO_N_0_7, 0x00);
	it6505_write(it6505, REG_AUDIO_N_8_15, 0x80);
	it6505_write(it6505, REG_AUDIO_N_16_23, 0x00);

	it6505_set_bits(it6505, REG_AUDIO_SRC_CTRL, AUDIO_FIFO_RESET,
			AUDIO_FIFO_RESET);
	it6505_set_bits(it6505, REG_AUDIO_SRC_CTRL, AUDIO_FIFO_RESET, 0x00);
	it6505_set_bits(it6505, REG_RESET_CTRL, AUDIO_RESET, 0x00);
	regbe = it6505_read(it6505, REG_AUDIO_INPUT_FREQ);
	DRM_DEV_DEBUG_DRIVER(dev, "regbe:0x%02x audio input fs: %d.%d kHz",
			     regbe, 6750 / regbe, (6750 % regbe) * 10 / regbe);
	it6505_set_bits(it6505, REG_DATA_MUTE_CTRL, EN_AUD_MUTE, 0x00);
}

static bool it6505_use_step_train_check(struct it6505 *it6505)
{
	if (it6505->link.revision >= 0x12)
		return it6505->dpcd[DP_TRAINING_AUX_RD_INTERVAL] >= 0x01;

	return true;
}

static void it6505_parse_link_capabilities(struct it6505 *it6505)
{
	struct device *dev = it6505->dev;
	struct it6505_drm_dp_link *link = &it6505->link;
	int bcaps;

	if (it6505->dpcd[0] == 0) {
		dev_err(dev, "DPCD is not initialized");
		return;
	}

	memset(link, 0, sizeof(*link));

	link->revision = it6505->dpcd[0];
	link->rate = drm_dp_bw_code_to_link_rate(it6505->dpcd[1]);
	link->num_lanes = it6505->dpcd[2] & DP_MAX_LANE_COUNT_MASK;

	if (it6505->dpcd[2] & DP_ENHANCED_FRAME_CAP)
		link->capabilities = DP_ENHANCED_FRAME_CAP;

	DRM_DEV_DEBUG_DRIVER(dev, "DPCD Rev.: %d.%d",
			     link->revision >> 4, link->revision & 0x0F);

	DRM_DEV_DEBUG_DRIVER(dev, "Sink max link rate: %d.%02d Gbps per lane",
			     link->rate / 100000, link->rate / 1000 % 100);

	it6505->link_rate_bw_code = drm_dp_link_rate_to_bw_code(link->rate);
	DRM_DEV_DEBUG_DRIVER(dev, "link rate bw code:0x%02x",
			     it6505->link_rate_bw_code);
	it6505->link_rate_bw_code = min_t(int, it6505->link_rate_bw_code,
					  MAX_LINK_RATE);

	it6505->lane_count = link->num_lanes;
	DRM_DEV_DEBUG_DRIVER(dev, "Sink support %d lanes training",
			     it6505->lane_count);
	it6505->lane_count = min_t(int, it6505->lane_count,
				   it6505->max_lane_count);

	it6505->branch_device = drm_dp_is_branch(it6505->dpcd);
	DRM_DEV_DEBUG_DRIVER(dev, "Sink %sbranch device",
			     it6505->branch_device ? "" : "Not ");

	it6505->enable_enhanced_frame = link->capabilities;
	DRM_DEV_DEBUG_DRIVER(dev, "Sink %sSupport Enhanced Framing",
			     it6505->enable_enhanced_frame ? "" : "Not ");

	it6505->enable_ssc = (it6505->dpcd[DP_MAX_DOWNSPREAD] &
				DP_MAX_DOWNSPREAD_0_5);
	DRM_DEV_DEBUG_DRIVER(dev, "Maximum Down-Spread: %s, %ssupport SSC!",
			     it6505->enable_ssc ? "0.5" : "0",
			     it6505->enable_ssc ? "" : "Not ");

	it6505->step_train = it6505_use_step_train_check(it6505);
	if (it6505->step_train)
		DRM_DEV_DEBUG_DRIVER(dev, "auto train fail, will step train");

	bcaps = it6505_dpcd_read(it6505, DP_AUX_HDCP_BCAPS);
	DRM_DEV_DEBUG_DRIVER(dev, "bcaps:0x%02x", bcaps);
	if (bcaps & DP_BCAPS_HDCP_CAPABLE) {
		it6505->is_repeater = (bcaps & DP_BCAPS_REPEATER_PRESENT);
		DRM_DEV_DEBUG_DRIVER(dev, "Support HDCP! Downstream is %s!",
				     it6505->is_repeater ? "repeater" :
				     "receiver");
	} else {
		DRM_DEV_DEBUG_DRIVER(dev, "Sink not support HDCP!");
		it6505->hdcp_desired = false;
	}
	DRM_DEV_DEBUG_DRIVER(dev, "HDCP %s",
			     it6505->hdcp_desired ? "desired" : "undesired");
}

static void it6505_setup_ssc(struct it6505 *it6505)
{
	it6505_set_bits(it6505, REG_TRAIN_CTRL0, SPREAD_AMP_5,
			it6505->enable_ssc ? SPREAD_AMP_5 : 0x00);
	if (it6505->enable_ssc) {
		it6505_write(it6505, REG_SSC_CTRL0, 0x9E);
		it6505_write(it6505, REG_SSC_CTRL1, 0x1C);
		it6505_write(it6505, REG_SSC_CTRL2, 0x42);
		it6505_write(it6505, REG_SP_CTRL0, 0x07);
		it6505_write(it6505, REG_IP_CTRL1, 0x29);
		it6505_write(it6505, REG_IP_CTRL2, 0x03);
		/* Stamp Interrupt Step */
		it6505_set_bits(it6505, REG_TIME_STMP_CTRL, M_STAMP_STEP,
				0x10);
		it6505_dpcd_write(it6505, DP_DOWNSPREAD_CTRL,
				  DP_SPREAD_AMP_0_5);
	} else {
		it6505_dpcd_write(it6505, DP_DOWNSPREAD_CTRL, 0x00);
		it6505_set_bits(it6505, REG_TIME_STMP_CTRL, M_STAMP_STEP,
				0x00);
	}
}

static inline void it6505_link_rate_setup(struct it6505 *it6505)
{
	it6505_set_bits(it6505, REG_TRAIN_CTRL0, FORCE_LBR,
			(it6505->link_rate_bw_code == RBR) ? FORCE_LBR : 0x00);
	it6505_set_bits(it6505, REG_LINK_DRV, DRV_HS,
			(it6505->link_rate_bw_code == RBR) ? 0x00 : DRV_HS);
}

static void it6505_lane_count_setup(struct it6505 *it6505)
{
	it6505_get_extcon_property(it6505);
	it6505_set_bits(it6505, REG_TRAIN_CTRL0, LANE_SWAP,
			it6505->lane_swap ? LANE_SWAP : 0x00);
	it6505_set_bits(it6505, REG_TRAIN_CTRL0, LANE_COUNT_MASK,
			(it6505->lane_count - 1) << 1);
}

static void it6505_link_training_setup(struct it6505 *it6505)
{
	struct device *dev = it6505->dev;

	if (it6505->enable_enhanced_frame)
		it6505_set_bits(it6505, REG_DATA_MUTE_CTRL,
				ENABLE_ENHANCED_FRAME, ENABLE_ENHANCED_FRAME);

	it6505_link_rate_setup(it6505);
	it6505_lane_count_setup(it6505);
	it6505_setup_ssc(it6505);
	DRM_DEV_DEBUG_DRIVER(dev,
			     "%s, %d lanes, %sable ssc, %sable enhanced frame",
			     it6505->link_rate_bw_code != RBR ? "HBR" : "RBR",
			     it6505->lane_count,
			     it6505->enable_ssc ? "en" : "dis",
			     it6505->enable_enhanced_frame ? "en" : "dis");
}

static bool it6505_link_start_auto_train(struct it6505 *it6505)
{
	int timeout = 500, link_training_state;
	bool state = false;

	mutex_lock(&it6505->aux_lock);
	it6505_set_bits(it6505, REG_TRAIN_CTRL0,
			FORCE_CR_DONE | FORCE_EQ_DONE, 0x00);
	it6505_write(it6505, REG_TRAIN_CTRL1, FORCE_RETRAIN);
	it6505_write(it6505, REG_TRAIN_CTRL1, AUTO_TRAIN);

	while (timeout > 0) {
		usleep_range(1000, 2000);
		link_training_state = it6505_read(it6505, REG_LINK_TRAIN_STS);

		if (link_training_state > 0 &&
		    (link_training_state & LINK_STATE_NORP)) {
			state = true;
			goto unlock;
		}

		timeout--;
	}
unlock:
	mutex_unlock(&it6505->aux_lock);

	return state;
}

static int it6505_drm_dp_link_configure(struct it6505 *it6505)
{
	u8 values[2];
	int err;
	struct drm_dp_aux *aux = &it6505->aux;

	values[0] = it6505->link_rate_bw_code;
	values[1] = it6505->lane_count;

	if (it6505->enable_enhanced_frame)
		values[1] |= DP_LANE_COUNT_ENHANCED_FRAME_EN;

	err = drm_dp_dpcd_write(aux, DP_LINK_BW_SET, values, sizeof(values));
	if (err < 0)
		return err;

	return 0;
}

static bool it6505_check_voltage_swing_max(u8 lane_voltage_swing_pre_emphasis)
{
	return ((lane_voltage_swing_pre_emphasis & 0x03) == MAX_CR_LEVEL);
}

static bool it6505_check_pre_emphasis_max(u8 lane_voltage_swing_pre_emphasis)
{
	return ((lane_voltage_swing_pre_emphasis & 0x03) == MAX_EQ_LEVEL);
}

static bool it6505_check_max_voltage_swing_reached(u8 *lane_voltage_swing,
						   u8 lane_count)
{
	u8 i;

	for (i = 0; i < lane_count; i++) {
		if (lane_voltage_swing[i] & DP_TRAIN_MAX_SWING_REACHED)
			return true;
	}

	return false;
}

static bool
step_train_lane_voltage_para_set(struct it6505 *it6505,
				 struct it6505_step_train_para
				 *lane_voltage_pre_emphasis,
				 u8 *lane_voltage_pre_emphasis_set)
{
	u8 *voltage_swing = lane_voltage_pre_emphasis->voltage_swing;
	u8 *pre_emphasis = lane_voltage_pre_emphasis->pre_emphasis;
	u8 i;

	for (i = 0; i < it6505->lane_count; i++) {
		voltage_swing[i] &= 0x03;
		lane_voltage_pre_emphasis_set[i] = voltage_swing[i];
		if (it6505_check_voltage_swing_max(voltage_swing[i]))
			lane_voltage_pre_emphasis_set[i] |=
				DP_TRAIN_MAX_SWING_REACHED;

		pre_emphasis[i] &= 0x03;
		lane_voltage_pre_emphasis_set[i] |= pre_emphasis[i]
			<< DP_TRAIN_PRE_EMPHASIS_SHIFT;
		if (it6505_check_pre_emphasis_max(pre_emphasis[i]))
			lane_voltage_pre_emphasis_set[i] |=
				DP_TRAIN_MAX_PRE_EMPHASIS_REACHED;
		it6505_dpcd_write(it6505, DP_TRAINING_LANE0_SET + i,
				  lane_voltage_pre_emphasis_set[i]);

		if (lane_voltage_pre_emphasis_set[i] !=
		    it6505_dpcd_read(it6505, DP_TRAINING_LANE0_SET + i))
			return false;
	}

	return true;
}

static bool
it6505_step_cr_train(struct it6505 *it6505,
		     struct it6505_step_train_para *lane_voltage_pre_emphasis)
{
	u8 loop_count = 0, i = 0, j;
	u8 link_status[DP_LINK_STATUS_SIZE] = { 0 };
	u8 lane_level_config[MAX_LANE_COUNT] = { 0 };
	int pre_emphasis_adjust = -1, voltage_swing_adjust = -1;
	const struct drm_dp_aux *aux = &it6505->aux;

	it6505_dpcd_write(it6505, DP_DOWNSPREAD_CTRL,
			  it6505->enable_ssc ? DP_SPREAD_AMP_0_5 : 0x00);
	it6505_dpcd_write(it6505, DP_TRAINING_PATTERN_SET,
			  DP_TRAINING_PATTERN_1);

	while (loop_count < 5 && i < 10) {
		i++;
		if (!step_train_lane_voltage_para_set(it6505,
						      lane_voltage_pre_emphasis,
						      lane_level_config))
			continue;
		drm_dp_link_train_clock_recovery_delay(aux, it6505->dpcd);
		drm_dp_dpcd_read_link_status(&it6505->aux, link_status);

		if (drm_dp_clock_recovery_ok(link_status, it6505->lane_count)) {
			it6505_set_bits(it6505, REG_TRAIN_CTRL0, FORCE_CR_DONE,
					FORCE_CR_DONE);
			return true;
		}
		DRM_DEV_DEBUG_DRIVER(it6505->dev, "cr not done");

		if (it6505_check_max_voltage_swing_reached(lane_level_config,
							   it6505->lane_count))
			goto cr_train_fail;

		for (j = 0; j < it6505->lane_count; j++) {
			lane_voltage_pre_emphasis->voltage_swing[j] =
				drm_dp_get_adjust_request_voltage(link_status,
								  j) >>
				DP_TRAIN_VOLTAGE_SWING_SHIFT;
			lane_voltage_pre_emphasis->pre_emphasis[j] =
			drm_dp_get_adjust_request_pre_emphasis(link_status,
							       j) >>
					DP_TRAIN_PRE_EMPHASIS_SHIFT;
			if (voltage_swing_adjust ==
			     lane_voltage_pre_emphasis->voltage_swing[j] &&
			    pre_emphasis_adjust ==
			     lane_voltage_pre_emphasis->pre_emphasis[j]) {
				loop_count++;
				continue;
			}

			voltage_swing_adjust =
				lane_voltage_pre_emphasis->voltage_swing[j];
			pre_emphasis_adjust =
				lane_voltage_pre_emphasis->pre_emphasis[j];
			loop_count = 0;

			if (voltage_swing_adjust + pre_emphasis_adjust >
			    MAX_EQ_LEVEL)
				lane_voltage_pre_emphasis->voltage_swing[j] =
					MAX_EQ_LEVEL -
					lane_voltage_pre_emphasis
						->pre_emphasis[j];
		}
	}

cr_train_fail:
	it6505_dpcd_write(it6505, DP_TRAINING_PATTERN_SET,
			  DP_TRAINING_PATTERN_DISABLE);

	return false;
}

static bool
it6505_step_eq_train(struct it6505 *it6505,
		     struct it6505_step_train_para *lane_voltage_pre_emphasis)
{
	u8 loop_count = 0, i, link_status[DP_LINK_STATUS_SIZE] = { 0 };
	u8 lane_level_config[MAX_LANE_COUNT] = { 0 };
	const struct drm_dp_aux *aux = &it6505->aux;

	it6505_dpcd_write(it6505, DP_TRAINING_PATTERN_SET,
			  DP_TRAINING_PATTERN_2);

	while (loop_count < 6) {
		loop_count++;

		if (!step_train_lane_voltage_para_set(it6505,
						      lane_voltage_pre_emphasis,
						      lane_level_config))
			continue;

		drm_dp_link_train_channel_eq_delay(aux, it6505->dpcd);
		drm_dp_dpcd_read_link_status(&it6505->aux, link_status);

		if (!drm_dp_clock_recovery_ok(link_status, it6505->lane_count))
			goto eq_train_fail;

		if (drm_dp_channel_eq_ok(link_status, it6505->lane_count)) {
			it6505_dpcd_write(it6505, DP_TRAINING_PATTERN_SET,
					  DP_TRAINING_PATTERN_DISABLE);
			it6505_set_bits(it6505, REG_TRAIN_CTRL0, FORCE_EQ_DONE,
					FORCE_EQ_DONE);
			return true;
		}
		DRM_DEV_DEBUG_DRIVER(it6505->dev, "eq not done");

		for (i = 0; i < it6505->lane_count; i++) {
			lane_voltage_pre_emphasis->voltage_swing[i] =
				drm_dp_get_adjust_request_voltage(link_status,
								  i) >>
				DP_TRAIN_VOLTAGE_SWING_SHIFT;
			lane_voltage_pre_emphasis->pre_emphasis[i] =
			drm_dp_get_adjust_request_pre_emphasis(link_status,
							       i) >>
					DP_TRAIN_PRE_EMPHASIS_SHIFT;

			if (lane_voltage_pre_emphasis->voltage_swing[i] +
				    lane_voltage_pre_emphasis->pre_emphasis[i] >
			    MAX_EQ_LEVEL)
				lane_voltage_pre_emphasis->voltage_swing[i] =
					0x03 - lane_voltage_pre_emphasis
						       ->pre_emphasis[i];
		}
	}

eq_train_fail:
	it6505_dpcd_write(it6505, DP_TRAINING_PATTERN_SET,
			  DP_TRAINING_PATTERN_DISABLE);
	return false;
}

static bool it6505_link_start_step_train(struct it6505 *it6505)
{
	int err;
	struct it6505_step_train_para lane_voltage_pre_emphasis = {
		.voltage_swing = { 0 },
		.pre_emphasis = { 0 },
	};

	DRM_DEV_DEBUG_DRIVER(it6505->dev, "start");
	err = it6505_drm_dp_link_configure(it6505);

	if (err < 0)
		return false;
	if (!it6505_step_cr_train(it6505, &lane_voltage_pre_emphasis))
		return false;
	if (!it6505_step_eq_train(it6505, &lane_voltage_pre_emphasis))
		return false;
	return true;
}

static bool it6505_get_video_status(struct it6505 *it6505)
{
	int reg_0d;

	reg_0d = it6505_read(it6505, REG_SYSTEM_STS);

	if (reg_0d < 0)
		return false;

	return reg_0d & VIDEO_STB;
}

static void it6505_reset_hdcp(struct it6505 *it6505)
{
	it6505->hdcp_status = HDCP_AUTH_IDLE;
	/* Disable CP_Desired */
	it6505_set_bits(it6505, REG_HDCP_CTRL1, HDCP_CP_ENABLE, 0x00);
	it6505_set_bits(it6505, REG_RESET_CTRL, HDCP_RESET, HDCP_RESET);
}

static void it6505_start_hdcp(struct it6505 *it6505)
{
	struct device *dev = it6505->dev;

	DRM_DEV_DEBUG_DRIVER(dev, "start");
	it6505_reset_hdcp(it6505);
	queue_delayed_work(system_wq, &it6505->hdcp_work,
			   msecs_to_jiffies(2400));
}

static void it6505_stop_hdcp(struct it6505 *it6505)
{
	it6505_reset_hdcp(it6505);
	cancel_delayed_work(&it6505->hdcp_work);
}

static bool it6505_hdcp_is_ksv_valid(u8 *ksv)
{
	int i, ones = 0;

	/* KSV has 20 1's and 20 0's */
	for (i = 0; i < DRM_HDCP_KSV_LEN; i++)
		ones += hweight8(ksv[i]);
	if (ones != 20)
		return false;
	return true;
}

static void it6505_hdcp_part1_auth(struct it6505 *it6505)
{
	struct device *dev = it6505->dev;
	u8 hdcp_bcaps;

	it6505_set_bits(it6505, REG_RESET_CTRL, HDCP_RESET, 0x00);
	/* Disable CP_Desired */
	it6505_set_bits(it6505, REG_HDCP_CTRL1, HDCP_CP_ENABLE, 0x00);

	usleep_range(1000, 1500);
	hdcp_bcaps = it6505_dpcd_read(it6505, DP_AUX_HDCP_BCAPS);
	DRM_DEV_DEBUG_DRIVER(dev, "DPCD[0x68028]: 0x%02x",
			     hdcp_bcaps);

	if (!hdcp_bcaps)
		return;

	/* clear the repeater List Chk Done and fail bit */
	it6505_set_bits(it6505, REG_HDCP_TRIGGER,
			HDCP_TRIGGER_KSV_DONE | HDCP_TRIGGER_KSV_FAIL,
			0x00);

	/* Enable An Generator */
	it6505_set_bits(it6505, REG_HDCP_CTRL2, HDCP_AN_GEN, HDCP_AN_GEN);
	/* delay1ms(10);*/
	usleep_range(10000, 15000);
	/* Stop An Generator */
	it6505_set_bits(it6505, REG_HDCP_CTRL2, HDCP_AN_GEN, 0x00);

	it6505_set_bits(it6505, REG_HDCP_CTRL1, HDCP_CP_ENABLE, HDCP_CP_ENABLE);

	it6505_set_bits(it6505, REG_HDCP_TRIGGER, HDCP_TRIGGER_START,
			HDCP_TRIGGER_START);

	it6505->hdcp_status = HDCP_AUTH_GOING;
}

static int it6505_sha1_digest(struct it6505 *it6505, u8 *sha1_input,
			      unsigned int size, u8 *output_av)
{
	struct shash_desc *desc;
	struct crypto_shash *tfm;
	int err;
	struct device *dev = it6505->dev;

	tfm = crypto_alloc_shash("sha1", 0, 0);
	if (IS_ERR(tfm)) {
		dev_err(dev, "crypto_alloc_shash sha1 failed");
		return PTR_ERR(tfm);
	}
	desc = kzalloc(sizeof(*desc) + crypto_shash_descsize(tfm), GFP_KERNEL);
	if (!desc) {
		crypto_free_shash(tfm);
		return -ENOMEM;
	}

	desc->tfm = tfm;
	err = crypto_shash_digest(desc, sha1_input, size, output_av);
	if (err)
		dev_err(dev, "crypto_shash_digest sha1 failed");

	crypto_free_shash(tfm);
	kfree(desc);
	return err;
}

static int it6505_setup_sha1_input(struct it6505 *it6505, u8 *sha1_input)
{
	struct device *dev = it6505->dev;
	u8 binfo[2];
	int down_stream_count, i, err, msg_count = 0;

	err = it6505_get_dpcd(it6505, DP_AUX_HDCP_BINFO, binfo,
			      ARRAY_SIZE(binfo));

	if (err < 0) {
		dev_err(dev, "Read binfo value Fail");
		return err;
	}

	down_stream_count = binfo[0] & 0x7F;
	DRM_DEV_DEBUG_DRIVER(dev, "binfo:0x%*ph", (int)ARRAY_SIZE(binfo),
			     binfo);

	if ((binfo[0] & BIT(7)) || (binfo[1] & BIT(3))) {
		dev_err(dev, "HDCP max cascade device exceed");
		return 0;
	}

	if (!down_stream_count ||
	    down_stream_count > MAX_HDCP_DOWN_STREAM_COUNT) {
		dev_err(dev, "HDCP down stream count Error %d",
			down_stream_count);
		return 0;
	}

	for (i = 0; i < down_stream_count; i++) {
		err = it6505_get_dpcd(it6505, DP_AUX_HDCP_KSV_FIFO +
				      (i % 3) * DRM_HDCP_KSV_LEN,
				      sha1_input + msg_count,
				      DRM_HDCP_KSV_LEN);

		if (err < 0)
			return err;

		msg_count += 5;
	}

	it6505->hdcp_down_stream_count = down_stream_count;
	sha1_input[msg_count++] = binfo[0];
	sha1_input[msg_count++] = binfo[1];

	it6505_set_bits(it6505, REG_HDCP_CTRL2, HDCP_EN_M0_READ,
			HDCP_EN_M0_READ);

	err = regmap_bulk_read(it6505->regmap, REG_M0_0_7,
			       sha1_input + msg_count, 8);

	it6505_set_bits(it6505, REG_HDCP_CTRL2, HDCP_EN_M0_READ, 0x00);

	if (err < 0) {
		dev_err(dev, " Warning, Read M value Fail");
		return err;
	}

	msg_count += 8;

	return msg_count;
}

static bool it6505_hdcp_part2_ksvlist_check(struct it6505 *it6505)
{
	struct device *dev = it6505->dev;
	u8 av[5][4], bv[5][4];
	int i, err;

	i = it6505_setup_sha1_input(it6505, it6505->sha1_input);
	if (i <= 0) {
		dev_err(dev, "SHA-1 Input length error %d", i);
		return false;
	}

	it6505_sha1_digest(it6505, it6505->sha1_input, i, (u8 *)av);

	err = it6505_get_dpcd(it6505, DP_AUX_HDCP_V_PRIME(0), (u8 *)bv,
			      sizeof(bv));

	if (err < 0) {
		dev_err(dev, "Read V' value Fail");
		return false;
	}

	for (i = 0; i < 5; i++)
		if (bv[i][3] != av[i][0] || bv[i][2] != av[i][1] ||
		    bv[i][1] != av[i][2] || bv[i][0] != av[i][3])
			return false;

	DRM_DEV_DEBUG_DRIVER(dev, "V' all match!!");
	return true;
}

static void it6505_hdcp_wait_ksv_list(struct work_struct *work)
{
	struct it6505 *it6505 = container_of(work, struct it6505,
					     hdcp_wait_ksv_list);
	struct device *dev = it6505->dev;
	unsigned int timeout = 5000;
	u8 bstatus = 0;
	bool ksv_list_check;

	timeout /= 20;
	while (timeout > 0) {
		if (!it6505_get_sink_hpd_status(it6505))
			return;

		bstatus = it6505_dpcd_read(it6505, DP_AUX_HDCP_BSTATUS);

		if (bstatus & DP_BSTATUS_READY)
			break;

		msleep(20);
		timeout--;
	}

	if (timeout == 0) {
		DRM_DEV_DEBUG_DRIVER(dev, "timeout and ksv list wait failed");
		goto timeout;
	}

	ksv_list_check = it6505_hdcp_part2_ksvlist_check(it6505);
	DRM_DEV_DEBUG_DRIVER(dev, "ksv list ready, ksv list check %s",
			     ksv_list_check ? "pass" : "fail");
	if (ksv_list_check) {
		it6505_set_bits(it6505, REG_HDCP_TRIGGER,
				HDCP_TRIGGER_KSV_DONE, HDCP_TRIGGER_KSV_DONE);
		return;
	}
timeout:
	it6505_set_bits(it6505, REG_HDCP_TRIGGER,
			HDCP_TRIGGER_KSV_DONE | HDCP_TRIGGER_KSV_FAIL,
			HDCP_TRIGGER_KSV_DONE | HDCP_TRIGGER_KSV_FAIL);
}

static void it6505_hdcp_work(struct work_struct *work)
{
	struct it6505 *it6505 = container_of(work, struct it6505,
					     hdcp_work.work);
	struct device *dev = it6505->dev;
	int ret;
	u8 link_status[DP_LINK_STATUS_SIZE] = { 0 };

	DRM_DEV_DEBUG_DRIVER(dev, "start");

	if (!it6505_get_sink_hpd_status(it6505))
		return;

	ret = drm_dp_dpcd_read_link_status(&it6505->aux, link_status);
	DRM_DEV_DEBUG_DRIVER(dev, "ret: %d link_status: %*ph", ret,
			     (int)sizeof(link_status), link_status);

	if (ret < 0 || !drm_dp_channel_eq_ok(link_status, it6505->lane_count) ||
	    !it6505_get_video_status(it6505)) {
		DRM_DEV_DEBUG_DRIVER(dev, "link train not done or no video");
		return;
	}

	ret = it6505_get_dpcd(it6505, DP_AUX_HDCP_BKSV, it6505->bksvs,
			      ARRAY_SIZE(it6505->bksvs));
	if (ret < 0) {
		dev_err(dev, "fail to get bksv  ret: %d", ret);
		it6505_set_bits(it6505, REG_HDCP_TRIGGER,
				HDCP_TRIGGER_KSV_FAIL, HDCP_TRIGGER_KSV_FAIL);
	}

	DRM_DEV_DEBUG_DRIVER(dev, "bksv = 0x%*ph",
			     (int)ARRAY_SIZE(it6505->bksvs), it6505->bksvs);

	if (!it6505_hdcp_is_ksv_valid(it6505->bksvs)) {
		dev_err(dev, "Display Port bksv not valid");
		it6505_set_bits(it6505, REG_HDCP_TRIGGER,
				HDCP_TRIGGER_KSV_FAIL, HDCP_TRIGGER_KSV_FAIL);
	}

	it6505_hdcp_part1_auth(it6505);
}

static void it6505_show_hdcp_info(struct it6505 *it6505)
{
	struct device *dev = it6505->dev;
	int i;
	u8 *sha1 = it6505->sha1_input;

	DRM_DEV_DEBUG_DRIVER(dev, "hdcp_status: %d is_repeater: %d",
			     it6505->hdcp_status, it6505->is_repeater);
	DRM_DEV_DEBUG_DRIVER(dev, "bksv = 0x%*ph",
			     (int)ARRAY_SIZE(it6505->bksvs), it6505->bksvs);

	if (it6505->is_repeater) {
		DRM_DEV_DEBUG_DRIVER(dev, "hdcp_down_stream_count: %d",
				     it6505->hdcp_down_stream_count);
		DRM_DEV_DEBUG_DRIVER(dev, "sha1_input: 0x%*ph",
				     (int)ARRAY_SIZE(it6505->sha1_input),
				     it6505->sha1_input);
		for (i = 0; i < it6505->hdcp_down_stream_count; i++) {
			DRM_DEV_DEBUG_DRIVER(dev, "KSV_%d = 0x%*ph", i,
					     DRM_HDCP_KSV_LEN, sha1);
			sha1 += DRM_HDCP_KSV_LEN;
		}
		DRM_DEV_DEBUG_DRIVER(dev, "binfo: 0x%2ph M0: 0x%8ph",
				     sha1, sha1 + 2);
	}
}

static void it6505_stop_link_train(struct it6505 *it6505)
{
	it6505->link_state = LINK_IDLE;
	cancel_work_sync(&it6505->link_works);
	it6505_write(it6505, REG_TRAIN_CTRL1, FORCE_RETRAIN);
}

static void it6505_link_train_ok(struct it6505 *it6505)
{
	struct device *dev = it6505->dev;

	it6505->link_state = LINK_OK;
	/* disalbe mute enable avi info frame */
	it6505_set_bits(it6505, REG_DATA_MUTE_CTRL, EN_VID_MUTE, 0x00);
	it6505_set_bits(it6505, REG_INFOFRAME_CTRL,
			EN_VID_CTRL_PKT, EN_VID_CTRL_PKT);

	if (it6505_audio_input(it6505)) {
		DRM_DEV_DEBUG_DRIVER(dev, "Enable audio!");
		it6505_enable_audio(it6505);
	}

	if (it6505->hdcp_desired)
		it6505_start_hdcp(it6505);
}

static void it6505_link_step_train_process(struct it6505 *it6505)
{
	struct device *dev = it6505->dev;
	int ret, i, step_retry = 3;

	DRM_DEV_DEBUG_DRIVER(dev, "Start step train");

	if (it6505->sink_count == 0) {
		DRM_DEV_DEBUG_DRIVER(dev, "it6505->sink_count:%d, force eq",
				     it6505->sink_count);
		it6505_set_bits(it6505,	REG_TRAIN_CTRL0, FORCE_EQ_DONE,
				FORCE_EQ_DONE);
		return;
	}

	if (!it6505->step_train) {
		DRM_DEV_DEBUG_DRIVER(dev, "not support step train");
		return;
	}

	/* step training start here */
	for (i = 0; i < step_retry; i++) {
		it6505_link_reset_step_train(it6505);
		ret = it6505_link_start_step_train(it6505);
		DRM_DEV_DEBUG_DRIVER(dev, "step train %s, retry:%d times",
				     ret ? "pass" : "failed", i + 1);
		if (ret) {
			it6505_link_train_ok(it6505);
			return;
		}
	}

	DRM_DEV_DEBUG_DRIVER(dev, "training fail");
	it6505->link_state = LINK_IDLE;
	it6505_video_reset(it6505);
}

static void it6505_link_training_work(struct work_struct *work)
{
	struct it6505 *it6505 = container_of(work, struct it6505, link_works);
	struct device *dev = it6505->dev;
	int ret;

	DRM_DEV_DEBUG_DRIVER(dev, "it6505->sink_count: %d",
			     it6505->sink_count);

	if (!it6505_get_sink_hpd_status(it6505))
		return;

	it6505_link_training_setup(it6505);
	it6505_reset_hdcp(it6505);
	it6505_aux_reset(it6505);

	if (it6505->auto_train_retry < 1) {
		it6505_link_step_train_process(it6505);
		return;
	}

	ret = it6505_link_start_auto_train(it6505);
	DRM_DEV_DEBUG_DRIVER(dev, "auto train %s, auto_train_retry: %d",
			     ret ? "pass" : "failed", it6505->auto_train_retry);

	if (ret) {
		it6505->auto_train_retry = AUTO_TRAIN_RETRY;
		it6505_link_train_ok(it6505);
	} else {
		it6505->auto_train_retry--;
		it6505_dump(it6505);
	}

}

static void it6505_plugged_status_to_codec(struct it6505 *it6505)
{
	enum drm_connector_status status = it6505->connector_status;

	if (it6505->plugged_cb && it6505->codec_dev)
		it6505->plugged_cb(it6505->codec_dev,
				   status == connector_status_connected);
}

static void it6505_remove_edid(struct it6505 *it6505)
{
	drm_edid_free(it6505->cached_edid);
	it6505->cached_edid = NULL;
}

static int it6505_process_hpd_irq(struct it6505 *it6505)
{
	struct device *dev = it6505->dev;
	int ret, dpcd_sink_count, dp_irq_vector, bstatus;
	u8 link_status[DP_LINK_STATUS_SIZE];

	if (!it6505_get_sink_hpd_status(it6505)) {
		DRM_DEV_DEBUG_DRIVER(dev, "HPD_IRQ HPD low");
		it6505->sink_count = 0;
		return 0;
	}

	ret = it6505_dpcd_read(it6505, DP_SINK_COUNT);
	if (ret < 0)
		return ret;

	dpcd_sink_count = DP_GET_SINK_COUNT(ret);
	DRM_DEV_DEBUG_DRIVER(dev, "dpcd_sink_count: %d it6505->sink_count:%d",
			     dpcd_sink_count, it6505->sink_count);

	if (it6505->branch_device && dpcd_sink_count != it6505->sink_count) {
		memset(it6505->dpcd, 0, sizeof(it6505->dpcd));
		it6505->sink_count = dpcd_sink_count;
		it6505_reset_logic(it6505);
		it6505_int_mask_enable(it6505);
		it6505_init(it6505);
		it6505_remove_edid(it6505);
		return 0;
	}

	dp_irq_vector = it6505_dpcd_read(it6505, DP_DEVICE_SERVICE_IRQ_VECTOR);
	if (dp_irq_vector < 0)
		return dp_irq_vector;

	DRM_DEV_DEBUG_DRIVER(dev, "dp_irq_vector = 0x%02x", dp_irq_vector);

	if (dp_irq_vector & DP_CP_IRQ) {
		it6505_set_bits(it6505, REG_HDCP_TRIGGER, HDCP_TRIGGER_CPIRQ,
				HDCP_TRIGGER_CPIRQ);

		bstatus = it6505_dpcd_read(it6505, DP_AUX_HDCP_BSTATUS);
		if (bstatus < 0)
			return bstatus;

		DRM_DEV_DEBUG_DRIVER(dev, "Bstatus = 0x%02x", bstatus);
	}

	ret = drm_dp_dpcd_read_link_status(&it6505->aux, link_status);
	if (ret < 0) {
		dev_err(dev, "Fail to read link status ret: %d", ret);
		return ret;
	}

	DRM_DEV_DEBUG_DRIVER(dev, "link status = 0x%*ph",
			     (int)ARRAY_SIZE(link_status), link_status);

	if (!drm_dp_channel_eq_ok(link_status, it6505->lane_count)) {
		it6505->auto_train_retry = AUTO_TRAIN_RETRY;
		it6505_video_reset(it6505);
	}

	return 0;
}

static void it6505_irq_hpd(struct it6505 *it6505)
{
	struct device *dev = it6505->dev;
	int dp_sink_count;

	it6505->hpd_state = it6505_get_sink_hpd_status(it6505);
	DRM_DEV_DEBUG_DRIVER(dev, "hpd change interrupt, change to %s",
			     it6505->hpd_state ? "high" : "low");

	if (it6505->hpd_state) {
		wait_for_completion_timeout(&it6505->extcon_completion,
					    msecs_to_jiffies(1000));
		it6505_aux_on(it6505);
		if (it6505->dpcd[0] == 0) {
			it6505_get_dpcd(it6505, DP_DPCD_REV, it6505->dpcd,
					ARRAY_SIZE(it6505->dpcd));
			it6505_variable_config(it6505);
			it6505_parse_link_capabilities(it6505);
		}
		it6505->auto_train_retry = AUTO_TRAIN_RETRY;

		it6505_drm_dp_link_set_power(&it6505->aux, &it6505->link,
					     DP_SET_POWER_D0);
		dp_sink_count = it6505_dpcd_read(it6505, DP_SINK_COUNT);
		it6505->sink_count = DP_GET_SINK_COUNT(dp_sink_count);

		DRM_DEV_DEBUG_DRIVER(dev, "it6505->sink_count: %d",
				     it6505->sink_count);

		it6505_lane_termination_on(it6505);
		it6505_lane_power_on(it6505);

		/*
		 * for some dongle which issue HPD_irq
		 * when sink count change from  0->1
		 * it6505 not able to receive HPD_IRQ
		 * if HW never go into trainig done
		 */

		if (it6505->branch_device && it6505->sink_count == 0)
			schedule_work(&it6505->link_works);

		if (!it6505_get_video_status(it6505))
			it6505_video_reset(it6505);
	} else {
		memset(it6505->dpcd, 0, sizeof(it6505->dpcd));
		it6505_remove_edid(it6505);

		if (it6505->hdcp_desired)
			it6505_stop_hdcp(it6505);

		it6505_video_disable(it6505);
		it6505_disable_audio(it6505);
		it6505_stop_link_train(it6505);
		it6505_lane_off(it6505);
		it6505_link_reset_step_train(it6505);
	}

	if (it6505->bridge.dev)
		drm_helper_hpd_irq_event(it6505->bridge.dev);
}

static void it6505_irq_hpd_irq(struct it6505 *it6505)
{
	struct device *dev = it6505->dev;

	DRM_DEV_DEBUG_DRIVER(dev, "hpd_irq interrupt");

	if (it6505_process_hpd_irq(it6505) < 0)
		DRM_DEV_DEBUG_DRIVER(dev, "process hpd_irq fail!");
}

static void it6505_irq_scdt(struct it6505 *it6505)
{
	struct device *dev = it6505->dev;
	bool data;

	data = it6505_get_video_status(it6505);
	DRM_DEV_DEBUG_DRIVER(dev, "video stable change interrupt, %s",
			     data ? "stable" : "unstable");
	it6505_calc_video_info(it6505);
	it6505_link_reset_step_train(it6505);

	if (data)
		schedule_work(&it6505->link_works);
}

static void it6505_irq_hdcp_done(struct it6505 *it6505)
{
	struct device *dev = it6505->dev;

	DRM_DEV_DEBUG_DRIVER(dev, "hdcp done interrupt");
	it6505->hdcp_status = HDCP_AUTH_DONE;
	it6505_show_hdcp_info(it6505);
}

static void it6505_irq_hdcp_fail(struct it6505 *it6505)
{
	struct device *dev = it6505->dev;

	DRM_DEV_DEBUG_DRIVER(dev, "hdcp fail interrupt");
	it6505->hdcp_status = HDCP_AUTH_IDLE;
	it6505_show_hdcp_info(it6505);
	it6505_start_hdcp(it6505);
}

static void it6505_irq_aux_cmd_fail(struct it6505 *it6505)
{
	struct device *dev = it6505->dev;

	DRM_DEV_DEBUG_DRIVER(dev, "AUX PC Request Fail Interrupt");
}

static void it6505_irq_hdcp_ksv_check(struct it6505 *it6505)
{
	struct device *dev = it6505->dev;

	DRM_DEV_DEBUG_DRIVER(dev, "HDCP event Interrupt");
	schedule_work(&it6505->hdcp_wait_ksv_list);
}

static void it6505_irq_audio_fifo_error(struct it6505 *it6505)
{
	struct device *dev = it6505->dev;

	DRM_DEV_DEBUG_DRIVER(dev, "audio fifo error Interrupt");

	if (it6505_audio_input(it6505))
		it6505_enable_audio(it6505);
}

static void it6505_irq_link_train_fail(struct it6505 *it6505)
{
	struct device *dev = it6505->dev;

	DRM_DEV_DEBUG_DRIVER(dev, "link training fail interrupt");
	schedule_work(&it6505->link_works);
}

static bool it6505_test_bit(unsigned int bit, const unsigned int *addr)
{
	return 1 & (addr[bit / BITS_PER_BYTE] >> (bit % BITS_PER_BYTE));
}

static void it6505_irq_video_handler(struct it6505 *it6505, const int *int_status)
{
	struct device *dev = it6505->dev;
	int reg_0d, reg_int03;

	/*
	 * When video SCDT change with video not stable,
	 * Or video FIFO error, need video reset
	 */

	if ((!it6505_get_video_status(it6505) &&
	     (it6505_test_bit(INT_SCDT_CHANGE, (unsigned int *)int_status))) ||
	    (it6505_test_bit(BIT_INT_IO_FIFO_OVERFLOW,
			     (unsigned int *)int_status)) ||
	    (it6505_test_bit(BIT_INT_VID_FIFO_ERROR,
			     (unsigned int *)int_status))) {
		it6505->auto_train_retry = AUTO_TRAIN_RETRY;
		flush_work(&it6505->link_works);
		it6505_stop_hdcp(it6505);
		it6505_video_reset(it6505);

		usleep_range(10000, 11000);

		/*
		 * Clear FIFO error IRQ to prevent fifo error -> reset loop
		 * HW will trigger SCDT change IRQ again when video stable
		 */

		reg_int03 = it6505_read(it6505, INT_STATUS_03);
		reg_0d = it6505_read(it6505, REG_SYSTEM_STS);

		reg_int03 &= (BIT(INT_VID_FIFO_ERROR) | BIT(INT_IO_LATCH_FIFO_OVERFLOW));
		it6505_write(it6505, INT_STATUS_03, reg_int03);

		DRM_DEV_DEBUG_DRIVER(dev, "reg08 = 0x%02x", reg_int03);
		DRM_DEV_DEBUG_DRIVER(dev, "reg0D = 0x%02x", reg_0d);

		return;
	}

	if (it6505_test_bit(INT_SCDT_CHANGE, (unsigned int *)int_status))
		it6505_irq_scdt(it6505);
}

static irqreturn_t it6505_int_threaded_handler(int unused, void *data)
{
	struct it6505 *it6505 = data;
	struct device *dev = it6505->dev;
	static const struct {
		int bit;
		void (*handler)(struct it6505 *it6505);
	} irq_vec[] = {
		{ BIT_INT_HPD, it6505_irq_hpd },
		{ BIT_INT_HPD_IRQ, it6505_irq_hpd_irq },
		{ BIT_INT_HDCP_FAIL, it6505_irq_hdcp_fail },
		{ BIT_INT_HDCP_DONE, it6505_irq_hdcp_done },
		{ BIT_INT_AUX_CMD_FAIL, it6505_irq_aux_cmd_fail },
		{ BIT_INT_HDCP_KSV_CHECK, it6505_irq_hdcp_ksv_check },
		{ BIT_INT_AUDIO_FIFO_ERROR, it6505_irq_audio_fifo_error },
		{ BIT_INT_LINK_TRAIN_FAIL, it6505_irq_link_train_fail },
	};
	int int_status[3], i;

	if (it6505->enable_drv_hold || !it6505->powered)
		return IRQ_HANDLED;

	pm_runtime_get_sync(dev);

	int_status[0] = it6505_read(it6505, INT_STATUS_01);
	int_status[1] = it6505_read(it6505, INT_STATUS_02);
	int_status[2] = it6505_read(it6505, INT_STATUS_03);

	it6505_write(it6505, INT_STATUS_01, int_status[0]);
	it6505_write(it6505, INT_STATUS_02, int_status[1]);
	it6505_write(it6505, INT_STATUS_03, int_status[2]);

	DRM_DEV_DEBUG_DRIVER(dev, "reg06 = 0x%02x", int_status[0]);
	DRM_DEV_DEBUG_DRIVER(dev, "reg07 = 0x%02x", int_status[1]);
	DRM_DEV_DEBUG_DRIVER(dev, "reg08 = 0x%02x", int_status[2]);
	it6505_debug_print(it6505, REG_SYSTEM_STS, "");

	if (it6505_test_bit(irq_vec[0].bit, (unsigned int *)int_status))
		irq_vec[0].handler(it6505);

	if (it6505->hpd_state) {
		for (i = 1; i < ARRAY_SIZE(irq_vec); i++) {
			if (it6505_test_bit(irq_vec[i].bit, (unsigned int *)int_status))
				irq_vec[i].handler(it6505);
		}
		it6505_irq_video_handler(it6505, (unsigned int *)int_status);
	}

	pm_runtime_put_sync(dev);

	return IRQ_HANDLED;
}

static int it6505_poweron(struct it6505 *it6505)
{
	struct device *dev = it6505->dev;
	struct it6505_platform_data *pdata = &it6505->pdata;
	int err;

	DRM_DEV_DEBUG_DRIVER(dev, "it6505 start powered on");

	if (it6505->powered) {
		DRM_DEV_DEBUG_DRIVER(dev, "it6505 already powered on");
		return 0;
	}

	if (pdata->pwr18) {
		err = regulator_enable(pdata->pwr18);
		if (err) {
			DRM_DEV_DEBUG_DRIVER(dev, "Failed to enable VDD18: %d",
					     err);
			return err;
		}
	}

	if (pdata->ovdd) {
		/* time interval between IVDD and OVDD at least be 1ms */
		usleep_range(1000, 2000);
		err = regulator_enable(pdata->ovdd);
		if (err) {
			regulator_disable(pdata->pwr18);
			return err;
		}
	}
	/* time interval between OVDD and SYSRSTN at least be 10ms */
	if (pdata->gpiod_reset) {
		usleep_range(10000, 20000);
		gpiod_set_value_cansleep(pdata->gpiod_reset, 0);
		usleep_range(1000, 2000);
		gpiod_set_value_cansleep(pdata->gpiod_reset, 1);
		usleep_range(25000, 35000);
	}

	it6505->powered = true;
	it6505_reset_logic(it6505);
	it6505_int_mask_enable(it6505);
	it6505_init(it6505);
	it6505_lane_off(it6505);

	enable_irq(it6505->irq);

	return 0;
}

static int it6505_poweroff(struct it6505 *it6505)
{
	struct device *dev = it6505->dev;
	struct it6505_platform_data *pdata = &it6505->pdata;
	int err;

	DRM_DEV_DEBUG_DRIVER(dev, "it6505 start power off");

	if (!it6505->powered) {
		DRM_DEV_DEBUG_DRIVER(dev, "power had been already off");
		return 0;
	}

	disable_irq_nosync(it6505->irq);

	if (pdata->gpiod_reset)
		gpiod_set_value_cansleep(pdata->gpiod_reset, 0);

	if (pdata->pwr18) {
		err = regulator_disable(pdata->pwr18);
		if (err)
			return err;
	}

	if (pdata->ovdd) {
		err = regulator_disable(pdata->ovdd);
		if (err)
			return err;
	}

	it6505->powered = false;
	it6505->sink_count = 0;

	return 0;
}

static enum drm_connector_status it6505_detect(struct it6505 *it6505)
{
	struct device *dev = it6505->dev;
	enum drm_connector_status status = connector_status_disconnected;
	int dp_sink_count;

	DRM_DEV_DEBUG_DRIVER(dev, "it6505->sink_count:%d powered:%d",
			     it6505->sink_count, it6505->powered);

	mutex_lock(&it6505->mode_lock);

	if (!it6505->powered)
		goto unlock;

	if (it6505->enable_drv_hold) {
		status = it6505->hpd_state ? connector_status_connected :
					     connector_status_disconnected;
		goto unlock;
	}

	if (it6505->hpd_state) {
		it6505_drm_dp_link_set_power(&it6505->aux, &it6505->link,
					     DP_SET_POWER_D0);
		dp_sink_count = it6505_dpcd_read(it6505, DP_SINK_COUNT);
		it6505->sink_count = DP_GET_SINK_COUNT(dp_sink_count);
		DRM_DEV_DEBUG_DRIVER(dev, "it6505->sink_count:%d branch:%d",
				     it6505->sink_count, it6505->branch_device);

		if (it6505->branch_device) {
			status = (it6505->sink_count != 0) ?
				 connector_status_connected :
				 connector_status_disconnected;
		} else {
			status = connector_status_connected;
		}
	} else {
		it6505->sink_count = 0;
		memset(it6505->dpcd, 0, sizeof(it6505->dpcd));
	}

unlock:
	if (it6505->connector_status != status) {
		it6505->connector_status = status;
		it6505_plugged_status_to_codec(it6505);
	}

	mutex_unlock(&it6505->mode_lock);

	return status;
}

static int it6505_extcon_notifier(struct notifier_block *self,
				  unsigned long event, void *ptr)
{
	struct it6505 *it6505 = container_of(self, struct it6505, event_nb);

	schedule_work(&it6505->extcon_wq);
	return NOTIFY_DONE;
}

static void it6505_extcon_work(struct work_struct *work)
{
	struct it6505 *it6505 = container_of(work, struct it6505, extcon_wq);
	struct device *dev = it6505->dev;
	int state, ret;

	if (it6505->enable_drv_hold)
		return;

	mutex_lock(&it6505->extcon_lock);

	state = extcon_get_state(it6505->extcon, EXTCON_DISP_DP);
	DRM_DEV_DEBUG_DRIVER(dev, "EXTCON_DISP_DP = 0x%02x", state);

	if (state == it6505->extcon_state || unlikely(state < 0))
		goto unlock;
	it6505->extcon_state = state;
	if (state) {
		DRM_DEV_DEBUG_DRIVER(dev, "start to power on");
		msleep(100);
		ret = pm_runtime_get_sync(dev);

		/*
		 * On system resume, extcon_work can be triggered before
		 * pm_runtime_force_resume re-enables runtime power management.
		 * Handling the error here to make sure the bridge is powered on.
		 */
		if (ret < 0)
			it6505_poweron(it6505);

		complete_all(&it6505->extcon_completion);
	} else {
		DRM_DEV_DEBUG_DRIVER(dev, "start to power off");
		pm_runtime_put_sync(dev);
		reinit_completion(&it6505->extcon_completion);

		drm_helper_hpd_irq_event(it6505->bridge.dev);
		memset(it6505->dpcd, 0, sizeof(it6505->dpcd));
		DRM_DEV_DEBUG_DRIVER(dev, "power off it6505 success!");
	}

unlock:
	mutex_unlock(&it6505->extcon_lock);
}

static int it6505_use_notifier_module(struct it6505 *it6505)
{
	int ret;
	struct device *dev = it6505->dev;

	it6505->event_nb.notifier_call = it6505_extcon_notifier;
	INIT_WORK(&it6505->extcon_wq, it6505_extcon_work);
	ret = devm_extcon_register_notifier(it6505->dev,
					    it6505->extcon, EXTCON_DISP_DP,
					    &it6505->event_nb);
	if (ret) {
		dev_err(dev, "failed to register notifier for DP");
		return ret;
	}

	schedule_work(&it6505->extcon_wq);

	return 0;
}

static void it6505_remove_notifier_module(struct it6505 *it6505)
{
	if (it6505->extcon) {
		devm_extcon_unregister_notifier(it6505->dev,
						it6505->extcon,	EXTCON_DISP_DP,
						&it6505->event_nb);

		flush_work(&it6505->extcon_wq);
	}
}

static void __maybe_unused it6505_delayed_audio(struct work_struct *work)
{
	struct it6505 *it6505 = container_of(work, struct it6505,
					     delayed_audio.work);

	DRM_DEV_DEBUG_DRIVER(it6505->dev, "start");

	if (!it6505->powered)
		return;

	if (!it6505->enable_drv_hold)
		it6505_enable_audio(it6505);
}

static int __maybe_unused it6505_audio_setup_hw_params(struct it6505 *it6505,
						       struct hdmi_codec_params
						       *params)
{
	struct device *dev = it6505->dev;
	int i = 0;

	DRM_DEV_DEBUG_DRIVER(dev, "%s %d Hz, %d bit, %d channels\n", __func__,
			     params->sample_rate, params->sample_width,
			     params->cea.channels);

	if (!it6505->bridge.encoder)
		return -ENODEV;

	if (params->cea.channels <= 1 || params->cea.channels > 8) {
		DRM_DEV_DEBUG_DRIVER(dev, "channel number: %d not support",
				     it6505->audio.channel_count);
		return -EINVAL;
	}

	it6505->audio.channel_count = params->cea.channels;

	while (i < ARRAY_SIZE(audio_sample_rate_map) &&
	       params->sample_rate !=
		       audio_sample_rate_map[i].sample_rate_value) {
		i++;
	}
	if (i == ARRAY_SIZE(audio_sample_rate_map)) {
		DRM_DEV_DEBUG_DRIVER(dev, "sample rate: %d Hz not support",
				     params->sample_rate);
		return -EINVAL;
	}
	it6505->audio.sample_rate = audio_sample_rate_map[i].rate;

	switch (params->sample_width) {
	case 16:
		it6505->audio.word_length = WORD_LENGTH_16BIT;
		break;
	case 18:
		it6505->audio.word_length = WORD_LENGTH_18BIT;
		break;
	case 20:
		it6505->audio.word_length = WORD_LENGTH_20BIT;
		break;
	case 24:
	case 32:
		it6505->audio.word_length = WORD_LENGTH_24BIT;
		break;
	default:
		DRM_DEV_DEBUG_DRIVER(dev, "wordlength: %d bit not support",
				     params->sample_width);
		return -EINVAL;
	}

	return 0;
}

static void __maybe_unused it6505_audio_shutdown(struct device *dev, void *data)
{
	struct it6505 *it6505 = dev_get_drvdata(dev);

	if (it6505->powered)
		it6505_disable_audio(it6505);
}

static int __maybe_unused it6505_audio_hook_plugged_cb(struct device *dev,
						       void *data,
						       hdmi_codec_plugged_cb fn,
						       struct device *codec_dev)
{
	struct it6505 *it6505 = data;

	it6505->plugged_cb = fn;
	it6505->codec_dev = codec_dev;
	it6505_plugged_status_to_codec(it6505);

	return 0;
}

static inline struct it6505 *bridge_to_it6505(struct drm_bridge *bridge)
{
	return container_of(bridge, struct it6505, bridge);
}

static int it6505_bridge_attach(struct drm_bridge *bridge,
				enum drm_bridge_attach_flags flags)
{
	struct it6505 *it6505 = bridge_to_it6505(bridge);
	struct device *dev = it6505->dev;
	int ret;

	if (!(flags & DRM_BRIDGE_ATTACH_NO_CONNECTOR)) {
		DRM_ERROR("DRM_BRIDGE_ATTACH_NO_CONNECTOR must be supplied");
		return -EINVAL;
	}

	/* Register aux channel */
	it6505->aux.drm_dev = bridge->dev;

	ret = drm_dp_aux_register(&it6505->aux);

	if (ret < 0) {
		dev_err(dev, "Failed to register aux: %d", ret);
		return ret;
	}

	if (it6505->extcon) {
		ret = it6505_use_notifier_module(it6505);
		if (ret < 0) {
			dev_err(dev, "use notifier module failed");
			return ret;
		}
	}

	return 0;
}

static void it6505_bridge_detach(struct drm_bridge *bridge)
{
	struct it6505 *it6505 = bridge_to_it6505(bridge);

	flush_work(&it6505->link_works);
	it6505_remove_notifier_module(it6505);
}

static enum drm_mode_status
it6505_bridge_mode_valid(struct drm_bridge *bridge,
			 const struct drm_display_info *info,
			 const struct drm_display_mode *mode)
{
	struct it6505 *it6505 = bridge_to_it6505(bridge);

	if (mode->flags & DRM_MODE_FLAG_INTERLACE)
		return MODE_NO_INTERLACE;

	if (mode->clock > it6505->max_dpi_pixel_clock)
		return MODE_CLOCK_HIGH;

	it6505->video_info.clock = mode->clock;

	return MODE_OK;
}

static void it6505_bridge_atomic_enable(struct drm_bridge *bridge,
					struct drm_bridge_state *old_state)
{
	struct it6505 *it6505 = bridge_to_it6505(bridge);
	struct device *dev = it6505->dev;
	struct drm_atomic_state *state = old_state->base.state;
	struct hdmi_avi_infoframe frame;
	struct drm_crtc_state *crtc_state;
	struct drm_connector_state *conn_state;
	struct drm_display_mode *mode;
	struct drm_connector *connector;
	int ret;

	DRM_DEV_DEBUG_DRIVER(dev, "start");

	connector = drm_atomic_get_new_connector_for_encoder(state,
							     bridge->encoder);

	if (WARN_ON(!connector))
		return;

	conn_state = drm_atomic_get_new_connector_state(state, connector);

	if (WARN_ON(!conn_state))
		return;

	crtc_state = drm_atomic_get_new_crtc_state(state, conn_state->crtc);

	if (WARN_ON(!crtc_state))
		return;

	mode = &crtc_state->adjusted_mode;

	if (WARN_ON(!mode))
		return;

	ret = drm_hdmi_avi_infoframe_from_display_mode(&frame,
						       connector,
						       mode);
	if (ret)
		dev_err(dev, "Failed to setup AVI infoframe: %d", ret);

	it6505_update_video_parameter(it6505, mode);

	ret = it6505_send_video_infoframe(it6505, &frame);

	if (ret)
		dev_err(dev, "Failed to send AVI infoframe: %d", ret);

	it6505_int_mask_enable(it6505);
	it6505_video_reset(it6505);

	it6505_drm_dp_link_set_power(&it6505->aux, &it6505->link,
				     DP_SET_POWER_D0);
}

static void it6505_bridge_atomic_disable(struct drm_bridge *bridge,
					 struct drm_bridge_state *old_state)
{
	struct it6505 *it6505 = bridge_to_it6505(bridge);
	struct device *dev = it6505->dev;

	DRM_DEV_DEBUG_DRIVER(dev, "start");

	if (it6505->powered) {
		it6505_drm_dp_link_set_power(&it6505->aux, &it6505->link,
					     DP_SET_POWER_D3);
		it6505_video_disable(it6505);
	}
}

static void it6505_bridge_atomic_pre_enable(struct drm_bridge *bridge,
					    struct drm_bridge_state *old_state)
{
	struct it6505 *it6505 = bridge_to_it6505(bridge);
	struct device *dev = it6505->dev;

	DRM_DEV_DEBUG_DRIVER(dev, "start");

	pm_runtime_get_sync(dev);
}

static void it6505_bridge_atomic_post_disable(struct drm_bridge *bridge,
					      struct drm_bridge_state *old_state)
{
	struct it6505 *it6505 = bridge_to_it6505(bridge);
	struct device *dev = it6505->dev;

	DRM_DEV_DEBUG_DRIVER(dev, "start");

	pm_runtime_put_sync(dev);
}

static enum drm_connector_status
it6505_bridge_detect(struct drm_bridge *bridge)
{
	struct it6505 *it6505 = bridge_to_it6505(bridge);

	return it6505_detect(it6505);
}

static const struct drm_edid *it6505_bridge_edid_read(struct drm_bridge *bridge,
						      struct drm_connector *connector)
{
	struct it6505 *it6505 = bridge_to_it6505(bridge);
	struct device *dev = it6505->dev;

	if (!it6505->cached_edid) {
		it6505->cached_edid = drm_edid_read_custom(connector,
							   it6505_get_edid_block,
							   it6505);

		if (!it6505->cached_edid) {
			DRM_DEV_DEBUG_DRIVER(dev, "failed to get edid!");
			return NULL;
		}
	}

	return drm_edid_dup(it6505->cached_edid);
}

static const struct drm_bridge_funcs it6505_bridge_funcs = {
	.atomic_duplicate_state = drm_atomic_helper_bridge_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_bridge_destroy_state,
	.atomic_reset = drm_atomic_helper_bridge_reset,
	.attach = it6505_bridge_attach,
	.detach = it6505_bridge_detach,
	.mode_valid = it6505_bridge_mode_valid,
	.atomic_enable = it6505_bridge_atomic_enable,
	.atomic_disable = it6505_bridge_atomic_disable,
	.atomic_pre_enable = it6505_bridge_atomic_pre_enable,
	.atomic_post_disable = it6505_bridge_atomic_post_disable,
	.detect = it6505_bridge_detect,
	.edid_read = it6505_bridge_edid_read,
};

static __maybe_unused int it6505_bridge_resume(struct device *dev)
{
	struct it6505 *it6505 = dev_get_drvdata(dev);

	return it6505_poweron(it6505);
}

static __maybe_unused int it6505_bridge_suspend(struct device *dev)
{
	struct it6505 *it6505 = dev_get_drvdata(dev);

	it6505_remove_edid(it6505);

	return it6505_poweroff(it6505);
}

static const struct dev_pm_ops it6505_bridge_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend, pm_runtime_force_resume)
	SET_RUNTIME_PM_OPS(it6505_bridge_suspend, it6505_bridge_resume, NULL)
};

static int it6505_init_pdata(struct it6505 *it6505)
{
	struct it6505_platform_data *pdata = &it6505->pdata;
	struct device *dev = it6505->dev;

	/* 1.0V digital core power regulator  */
	pdata->pwr18 = devm_regulator_get(dev, "pwr18");
	if (IS_ERR(pdata->pwr18)) {
		dev_err(dev, "pwr18 regulator not found");
		return PTR_ERR(pdata->pwr18);
	}

	pdata->ovdd = devm_regulator_get(dev, "ovdd");
	if (IS_ERR(pdata->ovdd)) {
		dev_err(dev, "ovdd regulator not found");
		return PTR_ERR(pdata->ovdd);
	}

	pdata->gpiod_reset = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(pdata->gpiod_reset)) {
		dev_err(dev, "gpiod_reset gpio not found");
		return PTR_ERR(pdata->gpiod_reset);
	}

	return 0;
}

static int it6505_get_data_lanes_count(const struct device_node *endpoint,
				       const unsigned int min,
				       const unsigned int max)
{
	int ret;

	ret = of_property_count_u32_elems(endpoint, "data-lanes");
	if (ret < 0)
		return ret;

	if (ret < min || ret > max)
		return -EINVAL;

	return ret;
}

static void it6505_parse_dt(struct it6505 *it6505)
{
	struct device *dev = it6505->dev;
	struct device_node *np = dev->of_node, *ep = NULL;
	int len;
	u64 link_frequencies;
	u32 data_lanes[4];
	u32 *afe_setting = &it6505->afe_setting;
	u32 *max_lane_count = &it6505->max_lane_count;
	u32 *max_dpi_pixel_clock = &it6505->max_dpi_pixel_clock;

	it6505->lane_swap_disabled =
		device_property_read_bool(dev, "no-laneswap");

	if (it6505->lane_swap_disabled)
		it6505->lane_swap = false;

	if (device_property_read_u32(dev, "afe-setting", afe_setting) == 0) {
		if (*afe_setting >= ARRAY_SIZE(afe_setting_table)) {
			dev_err(dev, "afe setting error, use default");
			*afe_setting = 0;
		}
	} else {
		*afe_setting = 0;
	}

	ep = of_graph_get_endpoint_by_regs(np, 1, 0);
	of_node_put(ep);

	if (ep) {
		len = it6505_get_data_lanes_count(ep, 1, 4);

		if (len > 0 && len != 3) {
			of_property_read_u32_array(ep, "data-lanes",
						   data_lanes, len);
			*max_lane_count = len;
		} else {
			*max_lane_count = MAX_LANE_COUNT;
			dev_err(dev, "error data-lanes, use default");
		}
	} else {
		*max_lane_count = MAX_LANE_COUNT;
		dev_err(dev, "error endpoint, use default");
	}

	ep = of_graph_get_endpoint_by_regs(np, 0, 0);
	of_node_put(ep);

	if (ep) {
		len = of_property_read_variable_u64_array(ep,
							  "link-frequencies",
							  &link_frequencies, 0,
							  1);
		if (len >= 0) {
			do_div(link_frequencies, 1000);
			if (link_frequencies > 297000) {
				dev_err(dev,
					"max pixel clock error, use default");
				*max_dpi_pixel_clock = DPI_PIXEL_CLK_MAX;
			} else {
				*max_dpi_pixel_clock = link_frequencies;
			}
		} else {
			dev_err(dev, "error link frequencies, use default");
			*max_dpi_pixel_clock = DPI_PIXEL_CLK_MAX;
		}
	} else {
		dev_err(dev, "error endpoint, use default");
		*max_dpi_pixel_clock = DPI_PIXEL_CLK_MAX;
	}

	DRM_DEV_DEBUG_DRIVER(dev, "using afe_setting: %u, max_lane_count: %u",
			     it6505->afe_setting, it6505->max_lane_count);
	DRM_DEV_DEBUG_DRIVER(dev, "using max_dpi_pixel_clock: %u kHz",
			     it6505->max_dpi_pixel_clock);
}

static ssize_t receive_timing_debugfs_show(struct file *file, char __user *buf,
					   size_t len, loff_t *ppos)
{
	struct it6505 *it6505 = file->private_data;
	struct drm_display_mode *vid;
	u8 read_buf[READ_BUFFER_SIZE];
	u8 *str = read_buf, *end = read_buf + READ_BUFFER_SIZE;
	ssize_t ret, count;

	if (!it6505)
		return -ENODEV;

	it6505_calc_video_info(it6505);
	vid = &it6505->video_info;
	str += scnprintf(str, end - str, "---video timing---\n");
	str += scnprintf(str, end - str, "PCLK:%d.%03dMHz\n",
			 vid->clock / 1000, vid->clock % 1000);
	str += scnprintf(str, end - str, "HTotal:%d\n", vid->htotal);
	str += scnprintf(str, end - str, "HActive:%d\n", vid->hdisplay);
	str += scnprintf(str, end - str, "HFrontPorch:%d\n",
			 vid->hsync_start - vid->hdisplay);
	str += scnprintf(str, end - str, "HSyncWidth:%d\n",
			 vid->hsync_end - vid->hsync_start);
	str += scnprintf(str, end - str, "HBackPorch:%d\n",
			 vid->htotal - vid->hsync_end);
	str += scnprintf(str, end - str, "VTotal:%d\n", vid->vtotal);
	str += scnprintf(str, end - str, "VActive:%d\n", vid->vdisplay);
	str += scnprintf(str, end - str, "VFrontPorch:%d\n",
			 vid->vsync_start - vid->vdisplay);
	str += scnprintf(str, end - str, "VSyncWidth:%d\n",
			 vid->vsync_end - vid->vsync_start);
	str += scnprintf(str, end - str, "VBackPorch:%d\n",
			 vid->vtotal - vid->vsync_end);

	count = str - read_buf;
	ret = simple_read_from_buffer(buf, len, ppos, read_buf, count);

	return ret;
}

static int force_power_on_off_debugfs_write(void *data, u64 value)
{
	struct it6505 *it6505 = data;

	if (!it6505)
		return -ENODEV;

	if (value)
		it6505_poweron(it6505);
	else
		it6505_poweroff(it6505);

	return 0;
}

static int enable_drv_hold_debugfs_show(void *data, u64 *buf)
{
	struct it6505 *it6505 = data;

	if (!it6505)
		return -ENODEV;

	*buf = it6505->enable_drv_hold;

	return 0;
}

static int enable_drv_hold_debugfs_write(void *data, u64 drv_hold)
{
	struct it6505 *it6505 = data;

	if (!it6505)
		return -ENODEV;

	it6505->enable_drv_hold = drv_hold;

	if (it6505->enable_drv_hold) {
		it6505_int_mask_disable(it6505);
	} else {
		it6505_clear_int(it6505);
		it6505_int_mask_enable(it6505);

		if (it6505->powered) {
			it6505->connector_status =
					it6505_get_sink_hpd_status(it6505) ?
					connector_status_connected :
					connector_status_disconnected;
		} else {
			it6505->connector_status =
					connector_status_disconnected;
		}
	}

	return 0;
}

static const struct file_operations receive_timing_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = receive_timing_debugfs_show,
	.llseek = default_llseek,
};

DEFINE_DEBUGFS_ATTRIBUTE(fops_force_power, NULL,
			 force_power_on_off_debugfs_write, "%llu\n");

DEFINE_DEBUGFS_ATTRIBUTE(fops_enable_drv_hold, enable_drv_hold_debugfs_show,
			 enable_drv_hold_debugfs_write, "%llu\n");

static const struct debugfs_entries debugfs_entry[] = {
	{ "receive_timing", &receive_timing_fops },
	{ "force_power_on_off", &fops_force_power },
	{ "enable_drv_hold", &fops_enable_drv_hold },
	{ NULL, NULL },
};

static void debugfs_create_files(struct it6505 *it6505)
{
	int i = 0;

	while (debugfs_entry[i].name && debugfs_entry[i].fops) {
		debugfs_create_file(debugfs_entry[i].name, 0644,
				    it6505->debugfs, it6505,
				    debugfs_entry[i].fops);
		i++;
	}
}

static void debugfs_init(struct it6505 *it6505)
{
	struct device *dev = it6505->dev;

	it6505->debugfs = debugfs_create_dir(DEBUGFS_DIR_NAME, NULL);

	if (IS_ERR(it6505->debugfs)) {
		dev_err(dev, "failed to create debugfs root");
		return;
	}

	debugfs_create_files(it6505);
}

static void it6505_debugfs_remove(struct it6505 *it6505)
{
	debugfs_remove_recursive(it6505->debugfs);
}

static void it6505_shutdown(struct i2c_client *client)
{
	struct it6505 *it6505 = dev_get_drvdata(&client->dev);

	if (it6505->powered)
		it6505_lane_off(it6505);
}

static int it6505_i2c_probe(struct i2c_client *client)
{
	struct it6505 *it6505;
	struct device *dev = &client->dev;
	struct extcon_dev *extcon;
	int err;

	it6505 = devm_kzalloc(&client->dev, sizeof(*it6505), GFP_KERNEL);
	if (!it6505)
		return -ENOMEM;

	mutex_init(&it6505->extcon_lock);
	mutex_init(&it6505->mode_lock);
	mutex_init(&it6505->aux_lock);

	it6505->bridge.of_node = client->dev.of_node;
	it6505->connector_status = connector_status_disconnected;
	it6505->dev = &client->dev;
	i2c_set_clientdata(client, it6505);

	/* get extcon device from DTS */
	extcon = extcon_get_edev_by_phandle(dev, 0);
	if (PTR_ERR(extcon) == -EPROBE_DEFER)
		return -EPROBE_DEFER;
	if (IS_ERR(extcon)) {
		dev_err(dev, "can not get extcon device!");
		return PTR_ERR(extcon);
	}

	it6505->extcon = extcon;

	it6505->regmap = devm_regmap_init_i2c(client, &it6505_regmap_config);
	if (IS_ERR(it6505->regmap)) {
		dev_err(dev, "regmap i2c init failed");
		err = PTR_ERR(it6505->regmap);
		return err;
	}

	err = it6505_init_pdata(it6505);
	if (err) {
		dev_err(dev, "Failed to initialize pdata: %d", err);
		return err;
	}

	it6505_parse_dt(it6505);

	it6505->irq = client->irq;

	if (!it6505->irq) {
		dev_err(dev, "Failed to get INTP IRQ");
		err = -ENODEV;
		return err;
	}

	err = devm_request_threaded_irq(&client->dev, it6505->irq, NULL,
					it6505_int_threaded_handler,
					IRQF_TRIGGER_LOW | IRQF_ONESHOT |
					IRQF_NO_AUTOEN,
					"it6505-intp", it6505);
	if (err) {
		dev_err(dev, "Failed to request INTP threaded IRQ: %d", err);
		return err;
	}

	INIT_WORK(&it6505->link_works, it6505_link_training_work);
	INIT_WORK(&it6505->hdcp_wait_ksv_list, it6505_hdcp_wait_ksv_list);
	INIT_DELAYED_WORK(&it6505->hdcp_work, it6505_hdcp_work);
	init_completion(&it6505->extcon_completion);
	memset(it6505->dpcd, 0, sizeof(it6505->dpcd));
	it6505->powered = false;
	it6505->enable_drv_hold = DEFAULT_DRV_HOLD;

	if (DEFAULT_PWR_ON)
		it6505_poweron(it6505);

	DRM_DEV_DEBUG_DRIVER(dev, "it6505 device name: %s", dev_name(dev));
	debugfs_init(it6505);
	pm_runtime_enable(dev);

	it6505->aux.name = "DP-AUX";
	it6505->aux.dev = dev;
	it6505->aux.transfer = it6505_aux_transfer;
	drm_dp_aux_init(&it6505->aux);

	it6505->bridge.funcs = &it6505_bridge_funcs;
	it6505->bridge.type = DRM_MODE_CONNECTOR_DisplayPort;
	it6505->bridge.ops = DRM_BRIDGE_OP_DETECT | DRM_BRIDGE_OP_EDID |
			     DRM_BRIDGE_OP_HPD;
	drm_bridge_add(&it6505->bridge);

	return 0;
}

static void it6505_i2c_remove(struct i2c_client *client)
{
	struct it6505 *it6505 = i2c_get_clientdata(client);

	drm_bridge_remove(&it6505->bridge);
	drm_dp_aux_unregister(&it6505->aux);
	it6505_debugfs_remove(it6505);
	it6505_poweroff(it6505);
	it6505_remove_edid(it6505);
}

static const struct i2c_device_id it6505_id[] = {
	{ "it6505", 0 },
	{ }
};

MODULE_DEVICE_TABLE(i2c, it6505_id);

static const struct of_device_id it6505_of_match[] = {
	{ .compatible = "ite,it6505" },
	{ }
};

static struct i2c_driver it6505_i2c_driver = {
	.driver = {
		.name = "it6505",
		.of_match_table = it6505_of_match,
		.pm = &it6505_bridge_pm_ops,
	},
	.probe = it6505_i2c_probe,
	.remove = it6505_i2c_remove,
	.shutdown = it6505_shutdown,
	.id_table = it6505_id,
};

module_i2c_driver(it6505_i2c_driver);

MODULE_AUTHOR("Allen Chen <allen.chen@ite.com.tw>");
MODULE_DESCRIPTION("IT6505 DisplayPort Transmitter driver");
MODULE_LICENSE("GPL v2");
