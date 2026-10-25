// SPDX-License-Identifier: GPL-2.0-only
/*
 * it6625 - ite HDMI to MIPI bridge
 */
#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/hdmi.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_graph.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/time64.h>
#include <linux/timer.h>
#include <linux/unaligned.h>
#include <linux/units.h>
#include <linux/v4l2-dv-timings.h>
#include <linux/videodev2.h>
#include <linux/workqueue.h>

#include <media/cec.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-dv-timings.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fwnode.h>
#include <uapi/linux/it6625.h>

static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "debug level (0-3)");

#define REG_CHIP_ID_0 0x00
#define REG_CHIP_ID_1 0x01
#define REG_FW_VER_MAJOR 0x03
#define REG_FW_VER_MINOR 0x04
#define REG_PROTOCOL_VERSION 0x05
#define B_PVER_MINOR  BIT(0)
#define B_PVER_MAJOR  BIT(4)

#define REG_CMD_SET 0x10
#define CMD_SET_CEC_LA 0xC0
#define CMD_SET_CEC_ENABLE 0xC1

#define REG_EDID_START 0x20
#define REG_CEC_RX_DATA 0x20
#define REG_CEC_TX_DATA 0x30

#define REG_H_ACTIVE_1 0x50
#define REG_H_ACTIVE_0 0x51
#define REG_V_ACTIVE_1 0x52
#define REG_V_ACTIVE_0 0x53
#define REG_H_TOTAL_1 0x54
#define REG_H_TOTAL_0 0x55
#define REG_V_TOTAL_1 0x56
#define REG_V_TOTAL_0 0x57
#define REG_VID_PCLK 0x58
#define REG_H_FP_1 0x5C
#define REG_H_FP_0 0x5D
#define REG_H_SW_1 0x5E
#define REG_H_SW_0 0x5F
#define REG_H_BP_1 0x60
#define REG_H_BP_0 0x61
#define REG_V_FP_1 0x62
#define REG_V_FP_0 0x63
#define REG_V_SW_1 0x64
#define REG_V_SW_0 0x65
#define REG_V_BP_1 0x66
#define REG_V_BP_0 0x67
#define REG_VID_INFO 0x68
#define B_INTERLACE  BIT(0)
#define B_HSWPOL  BIT(1)
#define B_VSWPOL  BIT(2)
#define B_PIXEL_REP  BIT(4)

#define REG_VIC 0x69
#define REG_HDMI_VIDEO_INFO 0x6A
#define B_COLOR_MODE  BIT(0)
#define B_COLOR_DEPTH  BIT(4)
#define REG_HDMI_AUDIO_INFO1 0x6B
#define B_AUD_FS  BIT(0)

#define REG_HDMI_AUDIO_INFO2 0x6C
#define B_AUD_CH  BIT(0)
#define B_AUD_WL  BIT(4)

#define REG_HDMI_AUDIO_INFO3 0x6D
#define B_AUD_TYPE  BIT(0)
#define B_AUD_MS  BIT(2)
#define B_AUD_3D  BIT(3)

#define REG_AUDIO_FMT 0x6E
#define B_I2S_WL  BIT(0)
#define B_I2S_ALN BIT(2)
#define B_I2S_DLY BIT(3)
#define B_I2S_LR  BIT(4)
#define B_I2S_SFT BIT(5)
#define B_AUD_OUT_IF  BIT(6)

#define REG_IF_LATCH_HB 0x6F
#define REG_IF_DATA 0x70
#define REG_EMP_DATA 0xA0
#define REG_AVI_DATA 0xAE

#define REG_TX_STATUS 0xE0

#define REG_RX_STATUS 0xE2
#define B_RX_5V BIT(0)
#define B_RX_HPD BIT(1)
#define B_RX_STABLE BIT(2)
#define B_RX_HDMI BIT(3)
#define B_RX_AVMUTE BIT(4)
#define B_RX_AUD_ON BIT(5)

#define REG_RX_HDCP_STS 0xE3
#define B_HDCP1_AUTH_START BIT(0)
#define B_HDCP2_AUTH_START BIT(1)
#define B_HDCP_AUTH_DONE BIT(2)
#define B_HDCP_ENC BIT(3)

#define REG_CEC_STATUS 0xE4
#define B_CEC_TX_DONE BIT(0)
#define B_CEC_TX_NACK BIT(1)

#define REG_CEC_RX_DATA_LEN 0xE5
#define REG_CEC_TX_DATA_LEN 0xE6

#define REG_SYS_MIPI_INT 0xEB
#define B_MIPI_OUTPUT_ENABLE BIT(0)
#define B_MIPI_VIDEO_UNSTABLE BIT(1)

#define REG_RX_INT_STATUS1 0xEC
#define B_HDMI_5V_CHG   BIT(0)
#define B_HDMI_VID_CHG  BIT(1)
#define B_HDMI_AUD_CHG  BIT(2)
#define B_HDMI_CP_CHG   BIT(3)
#define B_HDMI_IF_LATCH BIT(4)
#define B_HDMI_NO_IF_LATCH  BIT(5)
#define B_HDMI_EMP      BIT(6)
#define B_HDMI_NO_EMP   BIT(7)

#define REG_RX_INT_STATUS2 0xED
#define B_HDMI_AVI      BIT(0)
#define B_HDMI_NO_AVI   BIT(1)
#define B_HDMI_VSIF     BIT(2)
#define B_HDMI_NO_VSIF  BIT(3)
#define B_HDMI_AVMUTE_CHG  BIT(4)

#define REG_CHIP_CONTROL 0xF0
#define B_HDMI_RESET  BIT(5)
#define B_FW_START  BIT(6)

#define REG_MIPI_CFG 0xF1
#define M_MIPI_LANE  0x03
#define B_MIPI_SPLIT  BIT(2)
#define B_MIPI_SPLIT_CFG  BIT(3)
#define B_MIPI_DPHY  BIT(4)
#define B_MIPI_USE_DSI  BIT(5)
#define B_MIPI_CONTINU_CLK  BIT(6)

#define REG_MIPI_DATA_TYPE 0xF2
#define CSI_RGB444          0x20
#define CSI_RGB555          0x21
#define CSI_RGB565          0x22
#define CSI_RGB666          0x23
#define CSI_RGB888          0x24
#define CSI_YUV420_8b_L     0x1A
#define CSI_YUV420_8b       0x1C
#define CSI_YUV420_10b      0x1D
#define CSI_YUV422_8b       0x1E
#define CSI_YUV422_10b      0x1F
#define CSI_RGB_10b         0x30
#define CSI_RGB_12b         0x31
#define CSI_YUV422_12b      0x32
#define CSI_YUV420_10b_L    0x33
#define CSI_YUV420_12b      0x34
#define CSI_YUV444_8b       0x35
#define CSI_YUV444_10b      0x36
#define CSI_YUV444_12b      0x37

#define REG_MIPI_CONTROL 0xF3
#define B_MIPI_OUTPUT    BIT(0)

#define REG_RX_CFG 0xF4
#define B_MANUAL_HPD    BIT(0)
#define B_HPD_HIGH    BIT(1)
#define B_HPD_TOGGLE    BIT(3)

#define REG_CSC_CFG 0xF5
#define B_DYNAMIC_RANGE BIT(0)

#define REG_MISC_CFG 0xF6
#define B_BAUD_RATE BIT(0)
#define B_DEBUG_MSG BIT(1)

#define REG_HPD_DELAY 0xF7
#define B_DELAY_COUNT BIT(0)
#define B_DELAY_UNIT BIT(7)

#define REG_INFO_BANK_SEL 0xFD
#define CTL_BANK_EDID_READ 1
#define CTL_BANK_EDID_WRITE 5

#define REG_HOST_CTRL_INT 0xFE
#define B_CMD_SET BIT(4)
#define B_CEC_SEND_DATA BIT(5)
#define B_CONFIG_UPDATE BIT(6)
#define B_IF_BANK BIT(7)

#define REG_MCU_INTERRUPT 0xFF
#define B_SYS_INT_ACTIVE BIT(0)
#define B_CEC_RX_RECEIVED BIT(1)
#define B_CEC_TX_UPDATE BIT(2)

#define EDID_NUM_BLOCKS_MAX 4
#define EDID_BLOCK_SIZE 128

#define I2C_MAX_XFER_SIZE  8
#define POLL_INTERVAL_CEC_MS	10
#define POLL_INTERVAL_MS	40

#define AUD32K   0x03
#define AUD44K   0x00
#define AUD48K   0x02
#define AUD64K   0x0B
#define AUD88K   0x08
#define AUD96K   0x0A
#define AUD128K  0x2B
#define AUD176K  0x0C
#define AUD192K  0x0E
#define AUD256K  0x1B
#define AUD352K  0x0D
#define AUD384K  0x05
#define AUD512K  0x3B
#define AUD705K  0x2D
#define AUD768K  0x09
#define AUD1024K 0x35
#define AUD1411K 0x1D
#define AUD1536K 0x15

enum it6625_chip_type {
	IT6625_CHIP = 0,
	IT6626_CHIP = 1,
};

struct it6625 {
	struct device *dev;
	struct i2c_client *i2c_client;
	struct regmap *it6625_regmap;
	enum it6625_chip_type chip_type;

	/* Serializes MCU transactions. */
	struct mutex it6625_lock;
	/* serializes the complete VIDIOC_S_EDID sequence against itself */
	struct mutex edid_lock;
	/* serializes a full InfoFrame debugfs read transaction against itself */
	struct mutex if_read_lock;
	/*
	 * protects if_active/if_type/if_snapshot/if_snapshot_err/
	 * if_snapshot_done and the REG_MCU_INTERRUPT/REG_RX_INT_STATUS1/2/
	 * REG_IF_LATCH_HB/REG_IF_DATA register group between the interrupt
	 * path and the InfoFrame debugfs callback. Never held across
	 * wait_for_completion_timeout(). Nests outside it6625_lock.
	 */
	struct mutex if_state_lock;

	struct v4l2_subdev sd;
	struct video_device *vdev;
	struct media_pad pad;
	struct v4l2_ctrl_handler hdl;

	/* controls */
	struct v4l2_ctrl *ctrl_5v_detect;
	struct v4l2_ctrl *ctrl_audio_sampling_rate;
	struct v4l2_ctrl *ctrl_audio_present;
	struct v4l2_ctrl *ctrl_link_freq;

	struct delayed_work hpd_delayed_work;

	struct timer_list timer;
	struct work_struct polling_work;

	struct v4l2_dv_timings timings;

	u8 csi_lanes;
	u8 port_num;
	enum v4l2_mbus_type bus_type;
	/* number of EDID blocks currently loaded, protected by edid_lock */
	u8 edid_blocks;

	struct gpio_desc *reset_gpio;

	struct cec_adapter *cec_adap;

	struct dentry *debugfs_dir;

	struct v4l2_debugfs_if *infoframes;
	struct completion if_latched;
	/* true while an InfoFrame debugfs request is outstanding */
	bool if_active;
	/* HDMI packet-type byte currently armed in REG_IF_LATCH_HB */
	u32 if_type;
	/* driver-private copy of REG_IF_DATA, captured at the genuine latch */
	u8 if_snapshot[31];
	/* it6625_read_bytes() result for the if_snapshot capture */
	int if_snapshot_err;
	/* set just before complete(), to disambiguate timeout vs. capture */
	bool if_snapshot_done;
};

/*
 * Reported link frequency for every topology except the reference
 * exception below: D-PHY (any lane count) and one-/two-trio C-PHY.
 */
static const s64 it6625_link_freq_default[] = {
	445500000,
};

/*
 * IT6626 C-PHY, three trios: the confirmed hardware max C-PHY
 * capability, tested single-port/three-trio.
 */
static const s64 it6626_cphy_3trio_link_freq[] = {
	2500000000LL,
};

/*
 * Shared cap for every topology except the reference exception below:
 * IT6625 (no C-PHY support at all), IT6626 running D-PHY, and IT6626
 * running C-PHY with fewer than three trios. This is conservative
 * scoping, not a claim that one-/two-trio C-PHY can't also support a
 * higher rate -- they're simply unvalidated.
 */
static const struct v4l2_dv_timings_cap it6625_timings_cap = {
	.type = V4L2_DV_BT_656_1120,
	/* keep this initialization for compatibility with GCC < 4.4.6 */
	.reserved = { 0 },

	V4L2_INIT_BT_TIMINGS(640, 3840, 480, 2160, 27000000, 300000000,
			     V4L2_DV_BT_STD_CEA861 | V4L2_DV_BT_STD_DMT |
			     V4L2_DV_BT_STD_GTF | V4L2_DV_BT_STD_CVT,
			     V4L2_DV_BT_CAP_PROGRESSIVE | V4L2_DV_BT_CAP_INTERLACED |
			     V4L2_DV_BT_CAP_REDUCED_BLANKING | V4L2_DV_BT_CAP_CUSTOM)
};

/*
 * IT6626 C-PHY, three trios: raised pixel-clock ceiling (594 MHz vs
 * the shared 300 MHz cap) for this topology's higher C-PHY capability.
 */
static const struct v4l2_dv_timings_cap it6626_cphy_3trio_timings_cap = {
	.type = V4L2_DV_BT_656_1120,
	.reserved = { 0 },

	V4L2_INIT_BT_TIMINGS(640, 3840, 480, 2160, 27000000, 594000000,
			     V4L2_DV_BT_STD_CEA861 | V4L2_DV_BT_STD_DMT |
			     V4L2_DV_BT_STD_GTF | V4L2_DV_BT_STD_CVT,
			     V4L2_DV_BT_CAP_PROGRESSIVE | V4L2_DV_BT_CAP_INTERLACED |
			     V4L2_DV_BT_CAP_REDUCED_BLANKING | V4L2_DV_BT_CAP_CUSTOM)
};

static const struct v4l2_dv_timings_cap *
it6625_get_timings_cap(struct it6625 *it6625)
{
	if (it6625->chip_type == IT6626_CHIP &&
	    it6625->bus_type == V4L2_MBUS_CSI2_CPHY &&
	    it6625->csi_lanes == 3)
		return &it6626_cphy_3trio_timings_cap;

	return &it6625_timings_cap;
}

static const struct it6625_format_info {
	u8 csi_format;
	u32 mbus_fmt_code;
} it6625_formats[] = {
	{ CSI_YUV422_8b, MEDIA_BUS_FMT_UYVY8_1X16 },
	{ CSI_RGB888, MEDIA_BUS_FMT_RGB888_1X24 },
	{ CSI_YUV444_8b, MEDIA_BUS_FMT_YUV8_1X24 },
};

static inline int it6625_csi_format_idx(u8 csi_format)
{
	for (unsigned int i = 0; i < ARRAY_SIZE(it6625_formats); i++) {
		if (it6625_formats[i].csi_format == csi_format)
			return i;
	}

	return -EINVAL;
}

static inline int it6625_csi_mbus_code_idx(u32 mbus_fmt_code)
{
	for (unsigned int i = 0; i < ARRAY_SIZE(it6625_formats); i++) {
		if (it6625_formats[i].mbus_fmt_code == mbus_fmt_code)
			return i;
	}

	return -EINVAL;
}

static inline struct it6625 *sd_to_6625(struct v4l2_subdev *sd)
{
	return container_of(sd, struct it6625, sd);
}

static const struct regmap_config it6625_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0xff,
	.cache_type = REGCACHE_NONE,
	.max_raw_read = I2C_MAX_XFER_SIZE,
	.max_raw_write = I2C_MAX_XFER_SIZE,
};

static int it6625_regmap_i2c_init(struct i2c_client *client,
				  struct it6625 *it6625)
{
	it6625->i2c_client = client;
	it6625->dev = &client->dev;

	it6625->it6625_regmap = devm_regmap_init_i2c(it6625->i2c_client,
						     &it6625_regmap_config);
	if (IS_ERR(it6625->it6625_regmap))
		return PTR_ERR(it6625->it6625_regmap);

	return 0;
}

static int it6625_read_byte(struct it6625 *it6625, u8 reg)
{
	struct device *dev = it6625->dev;
	unsigned int val;
	int err;

	err = regmap_read(it6625->it6625_regmap, reg, &val);
	if (err < 0) {
		dev_err(dev, "reg[0x%x] read failed err: %d", reg, err);
		return err;
	}

	return val;
}

static int it6625_write_byte(struct it6625 *it6625, u8 reg, u8 val)
{
	struct device *dev = it6625->dev;
	int err;

	err = regmap_write(it6625->it6625_regmap, reg, val);
	if (err < 0) {
		dev_err(dev, "reg[0x%x] write failed err: %d", reg, err);
		return err;
	}

	return 0;
}

static int it6625_set_bits(struct it6625 *it6625, u8 reg, u8 mask, u8 val)
{
	struct device *dev = it6625->dev;
	int err;

	err = regmap_update_bits(it6625->it6625_regmap, reg, mask, val);
	if (err < 0) {
		dev_err(dev, "reg[0x%x] set bits failed err: %d", reg, err);
		return err;
	}

	return 0;
}

static int it6625_read_bytes(struct it6625 *it6625, u8 reg, u8 *buf, int len)
{
	struct device *dev = it6625->dev;
	int err;

	err = regmap_bulk_read(it6625->it6625_regmap, reg, buf, len);
	if (err < 0) {
		dev_err(dev, "reg[0x%x] read failed err: %d", reg, err);
		return err;
	}

	return 0;
}

static int it6625_write_bytes(struct it6625 *it6625, u8 reg, u8 *buf, int len)
{
	struct device *dev = it6625->dev;
	int err;

	err = regmap_bulk_write(it6625->it6625_regmap, reg, buf, len);
	if (err < 0) {
		dev_err(dev, "reg[0x%x] write failed err: %d", reg, err);
		return err;
	}

	return 0;
}

static int it6625_wait_for_status(struct it6625 *it6625, u8 reg, u8 val,
				  int timeout_ms)
{
	struct device *dev = it6625->dev;
	int status;
	int rval;
	int sleep_ms = 10;
	int timeout_round_ms = DIV_ROUND_UP(timeout_ms, sleep_ms) * sleep_ms;

	status = read_poll_timeout(it6625_read_byte, rval, rval == val,
				   sleep_ms * USEC_PER_MSEC,
				   timeout_round_ms * USEC_PER_MSEC,
				   false, it6625, reg);

	dev_dbg(dev, "%s status = %d %d", __func__, status, rval);
	if (status < 0) {
		dev_err(dev, "%s err status = %d", __func__, status);
		return -ETIMEDOUT;
	}

	return 0;
}

static void it6625_write_command(struct it6625 *it6625, u8 *cmds, int cmd_len)
{
	it6625_write_bytes(it6625, REG_CMD_SET, cmds, cmd_len);
	it6625_write_byte(it6625, REG_HOST_CTRL_INT, B_CMD_SET);
	it6625_wait_for_status(it6625, REG_HOST_CTRL_INT, 0x00, 25);
}

static int it6625_update_config(struct it6625 *it6625)
{
	int err;

	err = it6625_set_bits(it6625, REG_HOST_CTRL_INT,
			      B_CONFIG_UPDATE, B_CONFIG_UPDATE);
	if (err < 0)
		return err;

	return it6625_wait_for_status(it6625, REG_HOST_CTRL_INT, 0x00, 25);
}

static int it6625_set_bank(struct it6625 *it6625, u8 bank)
{
	int err;

	err = it6625_write_byte(it6625, REG_INFO_BANK_SEL, bank);
	if (err < 0)
		return err;

	err = it6625_write_byte(it6625, REG_HOST_CTRL_INT, B_IF_BANK);
	if (err < 0)
		return err;

	return it6625_wait_for_status(it6625, REG_HOST_CTRL_INT, 0x00, 25);
}

static inline bool is_hdmi(struct it6625 *it6625)
{
	int val;

	val = it6625_read_byte(it6625, REG_RX_STATUS);
	return val < 0 ? false : val & B_RX_HDMI;
}

static inline bool hdmi_5v_power_present(struct it6625 *it6625)
{
	int val;

	val = it6625_read_byte(it6625, REG_RX_STATUS);
	return val < 0 ? false : val & B_RX_5V;
}

static inline bool no_signal(struct it6625 *it6625)
{
	int val;

	val = it6625_read_byte(it6625, REG_RX_STATUS);
	return val < 0 ? true : !(val & B_RX_STABLE);
}

static inline bool audio_present(struct it6625 *it6625)
{
	int val;

	val = it6625_read_byte(it6625, REG_RX_STATUS);
	return val < 0 ? false : val & B_RX_AUD_ON;
}

static int get_audio_sampling_rate(struct it6625 *it6625)
{
	static const struct fs_id_map {
		u8 fs_id;
		u32 freq;
	} s_fsid_map[] = {
		{ AUD32K,    32000 },
		{ AUD44K,    44100 },
		{ AUD48K,    48000 },
		{ AUD64K,    64000 },
		{ AUD88K,    88200 },
		{ AUD96K,    96000 },

		{ AUD128K,  128000 },
		{ AUD176K,  176400 },
		{ AUD192K,  192000 },
		{ AUD256K,  256000 },
		{ AUD352K,  352800 },
		{ AUD384K,  384000 },

		{ AUD512K,  512000 },
		{ AUD705K,  705600 },
		{ AUD768K,  768000 },
		{ AUD1024K, 1024000 },
		{ AUD1411K, 1411200 },
		{ AUD1536K, 1536000 },
	};
	int fs_id;
	int i, freq = 0;

	if (no_signal(it6625) || !audio_present(it6625))
		return 0;

	guard(mutex)(&it6625->it6625_lock);

	fs_id = it6625_read_byte(it6625, REG_HDMI_AUDIO_INFO1);
	if (fs_id < 0)
		return 0;

	for (i = 0; i < ARRAY_SIZE(s_fsid_map); i++) {
		if (s_fsid_map[i].fs_id == fs_id) {
			freq = s_fsid_map[i].freq;
			break;
		}
	}

	return freq;
}

static u64 it6625_get_pclk(struct it6625 *it6625)
{
	u8 ck[4];
	u32 pclk;
	int ret;

	ret = it6625_read_bytes(it6625, REG_VID_PCLK, ck, sizeof(ck));
	if (ret < 0) {
		dev_err(it6625->dev, "failed to read pixel clock");
		return 0;
	}

	pclk = get_unaligned_be32(ck);

	v4l2_dbg(1, debug, &it6625->sd, "%s: pclk=%u (%08x)",
		 __func__, pclk, pclk);

	return (u64)pclk * HZ_PER_KHZ;
}

static int it6625_read_edid(struct it6625 *it6625, u8 *edid, int start_block,
			    int num_blocks)
{
	struct device *dev = it6625->dev;
	unsigned int i, bank_ctrl;
	int err = 0;

	if (!edid) {
		dev_err(dev, "edid buffer is NULL");
		return -EINVAL;
	}

	if (start_block < 0 || num_blocks <= 0 ||
	    start_block > EDID_NUM_BLOCKS_MAX ||
	    num_blocks > EDID_NUM_BLOCKS_MAX ||
	    start_block + num_blocks > EDID_NUM_BLOCKS_MAX) {
		dev_err(dev,
			"invalid block range: start_block=%d, num_blocks=%d",
			start_block, num_blocks);
		return -EINVAL;
	}

	guard(mutex)(&it6625->it6625_lock);
	for (i = 0; i < num_blocks; i++) {
		bank_ctrl = CTL_BANK_EDID_READ + start_block + i;
		err = it6625_set_bank(it6625, bank_ctrl);
		if (err < 0)
			break;

		err = it6625_read_bytes(it6625, REG_EDID_START,
					edid + (i * 128), 128);
		if (err < 0)
			break;
	}

	it6625_set_bank(it6625, 0);

	return err < 0 ? err : num_blocks;
}

static int it6625_write_edid(struct it6625 *it6625, u8 *edid, int start_block,
			     int num_blocks)
{
	struct device *dev = it6625->dev;
	unsigned int i, bank_ctrl;
	int err = 0;

	if (start_block < 0 || num_blocks <= 0 ||
	    start_block > EDID_NUM_BLOCKS_MAX ||
	    num_blocks > EDID_NUM_BLOCKS_MAX ||
	    start_block + num_blocks > EDID_NUM_BLOCKS_MAX) {
		dev_err(dev,
			"invalid block range: start_block=%d, num_blocks=%d",
			start_block, num_blocks);
		return -EINVAL;
	}

	guard(mutex)(&it6625->it6625_lock);
	for (i = 0; i < num_blocks; i++) {
		bank_ctrl = CTL_BANK_EDID_WRITE + start_block + i;
		err = it6625_set_bank(it6625, bank_ctrl);
		if (err < 0)
			break;

		err = it6625_write_bytes(it6625, REG_EDID_START,
					 edid + (i * 128), 128);
		if (err < 0)
			break;

		err = it6625_update_config(it6625);
		if (err < 0)
			break;
	}

	it6625_set_bank(it6625, 0);

	return err < 0 ? err : num_blocks;
}

static void it6625_enable_auto_hpd(struct it6625 *it6625)
{
	dev_dbg(it6625->dev, "%s: auto HPD", __func__);
	guard(mutex)(&it6625->it6625_lock);
	it6625_set_bits(it6625, REG_RX_CFG, 0x03, 0x00);
	it6625_update_config(it6625);
}

static void it6625_disable_hpd(struct it6625 *it6625)
{
	cancel_delayed_work_sync(&it6625->hpd_delayed_work);

	guard(mutex)(&it6625->it6625_lock);
	it6625_set_bits(it6625, REG_RX_CFG, 0x03, 0x01);
	it6625_update_config(it6625);
}

static void it6625_enable_hpd(struct it6625 *it6625)
{
	schedule_delayed_work(&it6625->hpd_delayed_work, V4L2_SET_EDID_HPD_LOW_JIFFIES);
}

static void it6625_hpd_delayed_work(struct work_struct *work)
{
	struct it6625 *it6625 =
		container_of(work, struct it6625, hpd_delayed_work.work);
	int val = 0;

	guard(mutex)(&it6625->it6625_lock);
	it6625_set_bits(it6625, REG_RX_CFG, 0x03, val);
	it6625_update_config(it6625);
}

/* REG_H_ACTIVE_1..REG_V_ACTIVE_0 */
struct it6625_active_size_regs {
	__be16 h_active;
	__be16 v_active;
};

static_assert(sizeof(struct it6625_active_size_regs) == 4);

/* REG_H_FP_1..REG_V_BP_0 */
struct it6625_porch_regs {
	__be16 hfrontporch;
	__be16 hsync;
	__be16 hbackporch;
	__be16 vfrontporch;
	__be16 vsync;
	__be16 vbackporch;
};

static_assert(sizeof(struct it6625_porch_regs) == 12);

static int it6625_get_detected_timings(struct it6625 *it6625,
				       struct v4l2_dv_timings *timings)
{
	struct v4l2_bt_timings *bt = &timings->bt;
	struct it6625_active_size_regs active;
	struct it6625_porch_regs porch;
	int val;

	if (no_signal(it6625)) {
		dev_err(it6625->dev, "no signal detected");
		return -ENOLINK;
	}

	guard(mutex)(&it6625->it6625_lock);

	memset(timings, 0, sizeof(struct v4l2_dv_timings));
	timings->type = V4L2_DV_BT_656_1120;
	val = it6625_read_byte(it6625, REG_VID_INFO);
	if (val < 0) {
		dev_err(it6625->dev, "failed to read video info");
		return -EIO;
	}

	bt->interlaced = val & B_INTERLACE ?
			 V4L2_DV_INTERLACED : V4L2_DV_PROGRESSIVE;

	if (it6625_read_bytes(it6625, REG_H_ACTIVE_1, (u8 *)&active, sizeof(active)) < 0)
		return -EIO;

	bt->width = be16_to_cpu(active.h_active);
	bt->height = be16_to_cpu(active.v_active);

	if (it6625_read_bytes(it6625, REG_H_FP_1, (u8 *)&porch, sizeof(porch)) < 0)
		return -EIO;

	bt->hfrontporch = be16_to_cpu(porch.hfrontporch);
	bt->hsync = be16_to_cpu(porch.hsync);
	bt->hbackporch = be16_to_cpu(porch.hbackporch);
	bt->vfrontporch = be16_to_cpu(porch.vfrontporch);
	bt->vsync = be16_to_cpu(porch.vsync);
	bt->vbackporch = be16_to_cpu(porch.vbackporch);

	bt->pixelclock = it6625_get_pclk(it6625);
	if (bt->interlaced == V4L2_DV_INTERLACED) {
		bt->height *= 2;
		bt->il_vsync = bt->vsync + 1;
	}

	return 0;
}

static void it6625_show_avi_infoframe(struct it6625 *it6625)
{
	struct device *dev = it6625->dev;
	union hdmi_infoframe frame;
	u8 buffer[HDMI_INFOFRAME_SIZE(AVI)];
	u8 ver, len;
	int ret;

	if (!is_hdmi(it6625)) {
		dev_err(dev, "not HDMI signal, skip AVI infoframe log");
		return;
	}

	ret = it6625_read_bytes(it6625, REG_AVI_DATA, buffer + 1,
				HDMI_INFOFRAME_SIZE(AVI) - 1);
	if (ret < 0) {
		dev_err(dev, "failed to read AVI infoframe data");
		return;
	}

	len = buffer[1];
	ver = buffer[2];

	buffer[0] = HDMI_INFOFRAME_TYPE_AVI;
	buffer[1] = ver;
	buffer[2] = len;

	ret = hdmi_infoframe_unpack(&frame, buffer, sizeof(buffer));
	if (ret < 0) {
		dev_err(dev, "unpack of AVI infoframe failed");
		return;
	}

	hdmi_infoframe_log(KERN_INFO, dev, &frame);
}

static int it6625_s_ctrl_detect_hdmi_5v(struct v4l2_subdev *sd)
{
	struct it6625 *it6625 = sd_to_6625(sd);

	return v4l2_ctrl_s_ctrl(it6625->ctrl_5v_detect,
				hdmi_5v_power_present(it6625));
}

static int it6625_s_ctrl_audio_sampling_rate(struct v4l2_subdev *sd)
{
	struct it6625 *it6625 = sd_to_6625(sd);

	return v4l2_ctrl_s_ctrl(it6625->ctrl_audio_sampling_rate,
				get_audio_sampling_rate(it6625));
}

static int it6625_s_ctrl_audio_present(struct v4l2_subdev *sd)
{
	struct it6625 *it6625 = sd_to_6625(sd);

	return v4l2_ctrl_s_ctrl(it6625->ctrl_audio_present,
				audio_present(it6625));
}

static int it6625_v4l2_sd_ctrl_update(struct v4l2_subdev *sd)
{
	int ret;

	ret = it6625_s_ctrl_detect_hdmi_5v(sd);
	if (ret)
		return ret;

	ret = it6625_s_ctrl_audio_sampling_rate(sd);
	if (ret)
		return ret;

	return it6625_s_ctrl_audio_present(sd);
}

static int it6625_enable_stream_locked(struct it6625 *it6625, bool enable)
{
	struct v4l2_subdev *sd = &it6625->sd;
	int val;
	int err;

	lockdep_assert_held(&it6625->it6625_lock);

	v4l2_dbg(3, debug, sd, "%s: %sable",
		 __func__, enable ? "en" : "dis");

	val = enable ? B_MIPI_OUTPUT : 0;
	err = it6625_set_bits(it6625, REG_MIPI_CONTROL, B_MIPI_OUTPUT, val);
	if (err < 0)
		return err;

	return it6625_update_config(it6625);
}

static int it6625_set_mipi_config_locked(struct it6625 *it6625, u32 cfg_val)
{
	u8 mipi_data_type;
	int err;

	lockdep_assert_held(&it6625->it6625_lock);

	dev_dbg(it6625->dev, "mipi_data_type = 0x%x", cfg_val);

	mipi_data_type = cfg_val & 0xFF;
	err = it6625_write_byte(it6625, REG_MIPI_DATA_TYPE, mipi_data_type);
	if (err < 0)
		return err;

	return it6625_update_config(it6625);
}

static inline unsigned int fps_from_bt_timings(const struct v4l2_bt_timings *t)
{
	if (!V4L2_DV_BT_FRAME_HEIGHT(t) || !V4L2_DV_BT_FRAME_WIDTH(t))
		return 0;

	return DIV_ROUND_CLOSEST((unsigned int)t->pixelclock,
				 V4L2_DV_BT_FRAME_HEIGHT(t) *
				 V4L2_DV_BT_FRAME_WIDTH(t));
}

static int it6625_initial_setup(struct it6625 *it6625)
{
	struct v4l2_subdev *sd = &it6625->sd;
	struct v4l2_subdev_state *state;
	struct v4l2_mbus_framefmt *fmt;
	int idx;
	int val = 0;
	int err;

	state = v4l2_subdev_lock_and_get_active_state(sd);
	fmt = v4l2_subdev_state_get_format(state, 0);
	idx = it6625_csi_mbus_code_idx(fmt->code);
	if (idx < 0)
		idx = 0;
	v4l2_subdev_unlock_state(state);

	guard(mutex)(&it6625->it6625_lock);

	/*
	 * REG_MIPI_CFG[0:2] lane count field: 1 lane -> 0, 2 lanes -> 1,
	 * 3 lanes (C-PHY only) -> 3, 4 lanes (D-PHY only) -> 3.
	 */
	switch (it6625->csi_lanes) {
	case 1:
		val = FIELD_PREP(M_MIPI_LANE, 0);
		break;
	case 2:
		val = FIELD_PREP(M_MIPI_LANE, 1);
		break;
	default:
		val = FIELD_PREP(M_MIPI_LANE, 3);
		break;
	}

	if (it6625->bus_type == V4L2_MBUS_CSI2_DPHY)
		val |= FIELD_PREP(B_MIPI_DPHY, 1);

	if (it6625->port_num == 2)
		val |= FIELD_PREP(B_MIPI_SPLIT, 1);

	err = it6625_write_byte(it6625, REG_MIPI_CFG, val);
	if (err)
		return err;

	err = it6625_write_byte(it6625, REG_MIPI_DATA_TYPE,
				it6625_formats[idx].csi_format);
	if (err)
		return err;

	err = it6625_write_byte(it6625, REG_MIPI_CONTROL, 0x00);
	if (err)
		return err;

	err = it6625_write_byte(it6625, REG_RX_CFG, 0x00);
	if (err)
		return err;

	err = it6625_set_bits(it6625, REG_HOST_CTRL_INT, B_CONFIG_UPDATE,
			      B_CONFIG_UPDATE);
	if (err)
		return err;

	return it6625_wait_for_status(it6625, REG_HOST_CTRL_INT, 0x00, 25);
}

static int it6625_cec_adap_enable(struct cec_adapter *adap, bool enable)
{
	struct it6625 *it6625 = adap->priv;
	u8 cmds[2] = { CMD_SET_CEC_ENABLE, enable ? 1 : 0 };

	guard(mutex)(&it6625->it6625_lock);
	it6625_write_command(it6625, cmds, sizeof(cmds));

	return 0;
}

static void it6625_cec_reset_la(struct it6625 *it6625, bool keep_enabled)
{
	u8 cmds[2];

	if (keep_enabled) {
		cmds[0] = CMD_SET_CEC_LA;
		cmds[1] = CEC_LOG_ADDR_UNREGISTERED;
	} else {
		cmds[0] = CMD_SET_CEC_ENABLE;
		cmds[1] = 0;
	}

	guard(mutex)(&it6625->it6625_lock);
	it6625_write_command(it6625, cmds, sizeof(cmds));
}

static int it6625_cec_adap_log_addr(struct cec_adapter *adap, u8 log_addr)
{
	struct it6625 *it6625 = adap->priv;
	u8 cmds[2] = { CMD_SET_CEC_LA, log_addr };

	dev_dbg(it6625->dev, "%s: la=%d", __func__, log_addr);

	if (log_addr == CEC_LOG_ADDR_INVALID) {
		it6625_cec_reset_la(it6625, adap->is_enabled);
		return 0;
	}

	guard(mutex)(&it6625->it6625_lock);
	it6625_write_command(it6625, cmds, sizeof(cmds));

	return 0;
}

static int it6625_cec_adap_transmit(struct cec_adapter *adap, u8 attempts,
				    u32 signal_free_time, struct cec_msg *msg)
{
	struct it6625 *it6625 = adap->priv;

	guard(mutex)(&it6625->it6625_lock);
	it6625_write_bytes(it6625, REG_CEC_TX_DATA, msg->msg, msg->len);
	it6625_write_byte(it6625, REG_CEC_TX_DATA_LEN, msg->len);
	it6625_set_bits(it6625, REG_HOST_CTRL_INT, B_CEC_SEND_DATA, B_CEC_SEND_DATA);
	it6625_wait_for_status(it6625, REG_HOST_CTRL_INT, 0x00, 25);

	return 0;
}

static const struct cec_adap_ops it6625_cec_adap_ops = {
	.adap_enable = it6625_cec_adap_enable,
	.adap_log_addr = it6625_cec_adap_log_addr,
	.adap_transmit = it6625_cec_adap_transmit,
};

static void it6625_cec_handler(struct it6625 *it6625, u8 intstatus)
{
	struct cec_msg rxmsg = {};
	int val = 0;

	if (intstatus & B_CEC_RX_RECEIVED) {
		scoped_guard(mutex, &it6625->it6625_lock) {
			val = it6625_read_byte(it6625, REG_CEC_RX_DATA_LEN);
			if (val > 0 && val <= CEC_MAX_MSG_SIZE)
				it6625_read_bytes(it6625, REG_CEC_RX_DATA, &rxmsg.msg[0], val);
			it6625_write_byte(it6625, REG_CEC_RX_DATA_LEN, 0);
		}

		if (val > 0 && val <= CEC_MAX_MSG_SIZE) {
			rxmsg.len = val;
			cec_received_msg(it6625->cec_adap, &rxmsg);
		} else {
			dev_err(it6625->dev, "invalid CEC RX length %d", val);
		}
	}

	if (intstatus & B_CEC_TX_UPDATE) {
		val = it6625_read_byte(it6625, REG_CEC_STATUS);
		if (val < 0) {
			dev_err(it6625->dev, "read CEC status failed");
			return;
		}

		if (val & BIT(0)) {
			cec_transmit_attempt_done(it6625->cec_adap,
						  CEC_TX_STATUS_OK);
		} else if (val & BIT(1)) {
			cec_transmit_attempt_done(it6625->cec_adap,
						  CEC_TX_STATUS_NACK);
		} else {
			dev_info(it6625->dev, "unknown CEC status %02X", val);
			cec_transmit_attempt_done(it6625->cec_adap,
						  CEC_TX_STATUS_NACK);
		}
	}
}

static void it6625_irq_format_change(struct it6625 *it6625)
{
	struct v4l2_subdev *sd = &it6625->sd;
	struct v4l2_dv_timings timings;
	const struct v4l2_event it6625_ev_fmt = {
		.type = V4L2_EVENT_SOURCE_CHANGE,
		.u.src_change.changes = V4L2_EVENT_SRC_CH_RESOLUTION,
	};
	int ret;

	if (no_signal(it6625)) {
		if (sd->devnode)
			v4l2_subdev_notify_event(sd, &it6625_ev_fmt);
		return;
	}

	ret = it6625_get_detected_timings(it6625, &timings);
	if (ret < 0) {
		v4l2_dbg(1, debug, sd, "Failed to get detected timings");
		return;
	}

	if (sd->devnode)
		v4l2_subdev_notify_event(sd, &it6625_ev_fmt);
}

static void it6625_irq_hdmi_audio_change(struct it6625 *it6625)
{
	struct v4l2_subdev *sd = &it6625->sd;

	it6625_s_ctrl_audio_sampling_rate(sd);
	it6625_s_ctrl_audio_present(sd);
}

static void it6625_get_timings(struct it6625 *it6625,
			       struct v4l2_dv_timings *timings)
{
	guard(mutex)(&it6625->it6625_lock);
	*timings = it6625->timings;
}

/*
 * Project a DV-timings struct's width/height/field onto a pad format.
 * Caller must hold it6625_lock and, separately, whichever subdev
 * state fmt belongs to.
 */
static void it6625_fill_timings_format(const struct v4l2_dv_timings *timings,
				       struct v4l2_mbus_framefmt *fmt)
{
	fmt->width = timings->bt.width;
	fmt->height = timings->bt.height;
	fmt->field = timings->bt.interlaced == V4L2_DV_INTERLACED ?
		     V4L2_FIELD_INTERLACED : V4L2_FIELD_NONE;
}

static void it6625_clear_timings(struct it6625 *it6625)
{
	struct v4l2_subdev *sd = &it6625->sd;
	struct v4l2_subdev_state *state = v4l2_subdev_lock_and_get_active_state(sd);
	struct v4l2_mbus_framefmt *fmt = v4l2_subdev_state_get_format(state, 0);

	scoped_guard(mutex, &it6625->it6625_lock) {
		memset(&it6625->timings, 0, sizeof(it6625->timings));
		it6625_fill_timings_format(&it6625->timings, fmt);
	}

	v4l2_subdev_unlock_state(state);
}

static void it6625_irq_hdmi_5v_change(struct it6625 *it6625)
{
	struct v4l2_subdev *sd = &it6625->sd;
	int ret;

	it6625_clear_timings(it6625);
	ret = it6625_v4l2_sd_ctrl_update(sd);
	if (ret)
		dev_err(it6625->dev, "%s: failed to update controls: %d", __func__, ret);
}

static void it6625_irq_hdcp_change(struct it6625 *it6625)
{
	struct v4l2_subdev *sd = &it6625->sd;
	u8 cp_sts;

	cp_sts = it6625_read_byte(it6625, REG_RX_HDCP_STS);
	v4l2_info(sd, "HDCP change to %02X", cp_sts);
}

static void it6625_irq_infoframe_latch(struct it6625 *it6625)
{
	if (!it6625->if_active)
		return;

	it6625->if_active = false;

	scoped_guard(mutex, &it6625->it6625_lock) {
		it6625->if_snapshot_err =
			it6625_read_bytes(it6625, REG_IF_DATA,
					  it6625->if_snapshot, 31);
	}
	it6625->if_snapshot_done = true;
	complete(&it6625->if_latched);
}

static void it6625_irq_emata_packet_latch(struct it6625 *it6625)
{
	u8 extend_packet[14];

	it6625_read_bytes(it6625, REG_EMP_DATA, extend_packet, 13);
}

static void it6625_irq_avmute_change(struct it6625 *it6625)
{
	struct v4l2_subdev *sd = &it6625->sd;
	int val;
	u8 avmute;

	val = it6625_read_byte(it6625, REG_RX_STATUS);
	if (val < 0)
		return;

	avmute = (val & B_RX_AVMUTE) ? 1 : 0;
	v4l2_info(sd, "AVMute change to %d", avmute);
}

static void it6625_dispatch_int_status1(struct it6625 *it6625, int int_sts1)
{
	if (int_sts1 & B_HDMI_5V_CHG)
		it6625_irq_hdmi_5v_change(it6625);

	if (int_sts1 & B_HDMI_VID_CHG)
		it6625_irq_format_change(it6625);

	if (int_sts1 & B_HDMI_AUD_CHG)
		it6625_irq_hdmi_audio_change(it6625);

	if (int_sts1 & B_HDMI_CP_CHG)
		it6625_irq_hdcp_change(it6625);

	if (int_sts1 & B_HDMI_IF_LATCH)
		it6625_irq_infoframe_latch(it6625);

	if (int_sts1 & B_HDMI_EMP)
		it6625_irq_emata_packet_latch(it6625);
}

static void it6625_dispatch_int_status2(struct it6625 *it6625, int int_sts2)
{
	struct v4l2_subdev *sd = &it6625->sd;

	if (int_sts2 & B_HDMI_AVI)
		it6625_show_avi_infoframe(it6625);

	if (int_sts2 & B_HDMI_NO_AVI)
		v4l2_info(sd, "AVI-info stopped");

	if (int_sts2 & B_HDMI_AVMUTE_CHG)
		it6625_irq_avmute_change(it6625);
}

/* caller must already hold if_state_lock */
static int it6625_drain_rx_int_status(struct it6625 *it6625)
{
	int val1, val2, err;

	val1 = it6625_read_byte(it6625, REG_RX_INT_STATUS1);
	val2 = it6625_read_byte(it6625, REG_RX_INT_STATUS2);

	err = 0;
	if (val1 < 0)
		err = val1;
	else if (val2 < 0)
		err = val2;

	if (val1 >= 0) {
		int werr = it6625_write_byte(it6625, REG_RX_INT_STATUS1, 0x00);

		if (werr && !err)
			err = werr;
		it6625_dispatch_int_status1(it6625, val1);
	}

	if (val2 >= 0) {
		int werr = it6625_write_byte(it6625, REG_RX_INT_STATUS2, 0x00);

		if (werr && !err)
			err = werr;
		it6625_dispatch_int_status2(it6625, val2);
	}

	return err;
}

/* caller must already hold if_state_lock */
static int it6625_drain_interrupts(struct it6625 *it6625, bool *had_event)
{
	struct v4l2_subdev *sd = &it6625->sd;
	int val, err;

	val = it6625_read_byte(it6625, REG_MCU_INTERRUPT);
	if (val < 0) {
		if (had_event)
			*had_event = false;
		return val;
	}
	if (had_event)
		*had_event = val > 0;
	if (val == 0)
		return 0;

	err = it6625_write_byte(it6625, REG_MCU_INTERRUPT, val);

	v4l2_dbg(1, debug, sd, "%s: INT = 0x%02X", __func__, val);

	if (val & (B_CEC_RX_RECEIVED | B_CEC_TX_UPDATE) && it6625->cec_adap)
		it6625_cec_handler(it6625, val & (B_CEC_RX_RECEIVED | B_CEC_TX_UPDATE));

	if (val & B_SYS_INT_ACTIVE) {
		int child_err = it6625_drain_rx_int_status(it6625);

		if (child_err && !err)
			err = child_err;
	}

	return err;
}

static bool it6625_interrupt_handler(struct it6625 *it6625)
{
	bool had_event = false;

	guard(mutex)(&it6625->if_state_lock);
	it6625_drain_interrupts(it6625, &had_event);
	return had_event;
}

static irqreturn_t it6625_irq_handler(int unused, void *data)
{
	struct it6625 *it6625 = data;

	return it6625_interrupt_handler(it6625) ? IRQ_HANDLED : IRQ_NONE;
}

static void it6625_irq_poll_timer(struct timer_list *t)
{
	struct it6625 *it6625 = timer_container_of(it6625, t, timer);
	unsigned int msecs;

	schedule_work(&it6625->polling_work);
	/*
	 * If CEC is present, then we need to poll more frequently,
	 * otherwise we will miss CEC messages.
	 */
	msecs = it6625->cec_adap ? POLL_INTERVAL_CEC_MS : POLL_INTERVAL_MS;
	mod_timer(&it6625->timer, jiffies + msecs_to_jiffies(msecs));
}

static void it6625_polling_work(struct work_struct *work)
{
	struct it6625 *it6625 = container_of(work, struct it6625,
					polling_work);

	it6625_interrupt_handler(it6625);
}

static int it6625_log_status(struct v4l2_subdev *sd)
{
	struct it6625 *it6625 = sd_to_6625(sd);
	struct v4l2_subdev_state *state;
	struct v4l2_dv_timings timings, configured_timings;
	struct v4l2_bt_timings bt;
	u32 mbus_fmt_code;

	if (it6625_get_detected_timings(it6625, &timings))
		v4l2_info(sd, "No video detected");
	else
		v4l2_print_dv_timings(sd->name, "Detected format: ", &timings,
				      true);

	it6625_get_timings(it6625, &configured_timings);
	v4l2_print_dv_timings(sd->name, "Configured format: ",
			      &configured_timings, true);

	/*
	 * VIDIOC_LOG_STATUS isn't core-locked, so take both locks
	 * ourselves; snapshot together so the reported pair was
	 * actually configured together.
	 */
	state = v4l2_subdev_lock_and_get_active_state(sd);
	scoped_guard(mutex, &it6625->it6625_lock) {
		mbus_fmt_code = v4l2_subdev_state_get_format(state, 0)->code;
		bt = it6625->timings.bt;
	}
	v4l2_subdev_unlock_state(state);

	v4l2_info(sd, "CSI format: %#x @ %uHz", mbus_fmt_code, fps_from_bt_timings(&bt));

	it6625_show_avi_infoframe(it6625);

	return 0;
}

static int it6625_isr(struct v4l2_subdev *sd, u32 status, bool *handled)
{
	struct it6625 *it6625 = sd_to_6625(sd);

	schedule_work(&it6625->polling_work);
	*handled = true;

	return 0;
}

static int it6625_subscribe_event(struct v4l2_subdev *sd, struct v4l2_fh *fh,
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

static int it6625_g_input_status(struct v4l2_subdev *sd, u32 *status)
{
	struct it6625 *it6625 = sd_to_6625(sd);
	bool val;

	val = no_signal(it6625);
	*status = 0;
	*status |= val ? V4L2_IN_ST_NO_SIGNAL : 0;
	*status |= val ? V4L2_IN_ST_NO_SYNC : 0;

	v4l2_dbg(1, debug, sd, "%s: status = 0x%x", __func__, *status);

	return 0;
}

static int
it6625_update_timings_if_changed(struct it6625 *it6625,
				 const struct v4l2_dv_timings *timings)
{
	struct v4l2_subdev *sd = &it6625->sd;
	struct v4l2_subdev_state *state;
	struct v4l2_mbus_framefmt *fmt;
	int ret;

	/* .s_dv_timings isn't core-locked, so take both locks ourselves */
	state = v4l2_subdev_lock_and_get_active_state(sd);
	fmt = v4l2_subdev_state_get_format(state, 0);

	scoped_guard(mutex, &it6625->it6625_lock) {
		if (v4l2_match_dv_timings(&it6625->timings, timings, 0, false)) {
			ret = 0;
		} else if (!v4l2_valid_dv_timings(timings,
						  it6625_get_timings_cap(it6625),
						  NULL, NULL)) {
			ret = -ERANGE;
		} else {
			it6625->timings = *timings;
			it6625_fill_timings_format(&it6625->timings, fmt);
			ret = 1;
		}
	}

	v4l2_subdev_unlock_state(state);

	return ret;
}

static int it6625_enum_dv_timings(struct v4l2_subdev *sd,
				  struct v4l2_enum_dv_timings *timings)
{
	struct it6625 *it6625 = sd_to_6625(sd);

	if (timings->pad != 0)
		return -EINVAL;

	return v4l2_enum_dv_timings_cap(timings,
					it6625_get_timings_cap(it6625), NULL, NULL);
}

static int it6625_dv_timings_cap(struct v4l2_subdev *sd,
				 struct v4l2_dv_timings_cap *cap)
{
	struct it6625 *it6625 = sd_to_6625(sd);

	if (cap->pad != 0)
		return -EINVAL;

	*cap = *it6625_get_timings_cap(it6625);

	return 0;
}

static int it6625_enable_streams(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *state,
				 u32 pad, u64 streams_mask)
{
	struct it6625 *it6625 = sd_to_6625(sd);

	guard(mutex)(&it6625->it6625_lock);
	return it6625_enable_stream_locked(it6625, true);
}

static int it6625_disable_streams(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *state,
				  u32 pad, u64 streams_mask)
{
	struct it6625 *it6625 = sd_to_6625(sd);

	guard(mutex)(&it6625->it6625_lock);
	return it6625_enable_stream_locked(it6625, false);
}

static int it6625_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	dev_dbg(sd->dev, "%s: index=%d", __func__, code->index);

	if (code->index >= ARRAY_SIZE(it6625_formats))
		return -EINVAL;

	dev_dbg(sd->dev, "%s: code=0x%08x", __func__,
		it6625_formats[code->index].mbus_fmt_code);
	code->code = it6625_formats[code->index].mbus_fmt_code;

	return 0;
}

static int it6625_get_mbus_config(struct v4l2_subdev *sd,
				  unsigned int pad,
				  struct v4l2_mbus_config *cfg)
{
	struct it6625 *it6625 = sd_to_6625(sd);

	if (pad != 0)
		return -EINVAL;

	cfg->type = it6625->bus_type;
	cfg->bus.mipi_csi2.flags = 0;
	cfg->bus.mipi_csi2.num_data_lanes = it6625->csi_lanes;

	return 0;
}

static int it6625_pad_s_dv_timings(struct v4l2_subdev *sd, unsigned int pad,
				   struct v4l2_dv_timings *timings)
{
	struct it6625 *it6625 = sd_to_6625(sd);
	int ret;

	if (pad != 0)
		return -EINVAL;

	if (!timings)
		return -EINVAL;

	if (debug)
		v4l2_print_dv_timings(sd->name, __func__, timings, false);

	ret = it6625_update_timings_if_changed(it6625, timings);
	if (ret == -ERANGE) {
		v4l2_dbg(1, debug, sd, "%s: timings out of range", __func__);
		return ret;
	}

	if (ret == 0)
		v4l2_dbg(1, debug, sd, "%s: no change", __func__);

	return 0;
}

static int it6625_pad_g_dv_timings(struct v4l2_subdev *sd, unsigned int pad,
				   struct v4l2_dv_timings *timings)
{
	struct it6625 *it6625 = sd_to_6625(sd);

	if (pad != 0)
		return -EINVAL;

	it6625_get_timings(it6625, timings);

	return 0;
}

static int it6625_pad_query_dv_timings(struct v4l2_subdev *sd,
				       unsigned int pad,
				       struct v4l2_dv_timings *timings)
{
	struct it6625 *it6625 = sd_to_6625(sd);
	int ret;

	if (pad != 0)
		return -EINVAL;

	ret = it6625_get_detected_timings(it6625, timings);
	if (ret)
		return ret;

	if (debug)
		v4l2_print_dv_timings(sd->name, __func__, timings, false);

	if (!v4l2_valid_dv_timings(timings, it6625_get_timings_cap(it6625), NULL, NULL)) {
		v4l2_dbg(1, debug, sd, "%s: timings out of range", __func__);
		return -ERANGE;
	}

	return 0;
}

static inline u32 format_to_colorspace(u8 csi_format)
{
	switch (csi_format) {
	case CSI_RGB444:
	case CSI_RGB555:
	case CSI_RGB565:
	case CSI_RGB666:
	case CSI_RGB888:
	case CSI_RGB_10b:
	case CSI_RGB_12b:
		return V4L2_COLORSPACE_SRGB;
	case CSI_YUV420_8b_L:
	case CSI_YUV420_8b:
	case CSI_YUV420_10b:
	case CSI_YUV422_8b:
	case CSI_YUV422_10b:
	case CSI_YUV422_12b:
	case CSI_YUV420_10b_L:
	case CSI_YUV420_12b:
	case CSI_YUV444_8b:
	case CSI_YUV444_10b:
	case CSI_YUV444_12b:
		return V4L2_COLORSPACE_REC709;
	default:
		return 0;
	}
}

static int it6625_set_fmt(struct v4l2_subdev *sd,
			  const struct v4l2_subdev_client_info *ci,
			  struct v4l2_subdev_state *sd_state,
			  struct v4l2_subdev_format *format)
{
	struct it6625 *it6625 = sd_to_6625(sd);
	struct v4l2_mbus_framefmt *fmt;
	u8 csi_format;
	int idx;
	int ret;

	if (format->pad != 0)
		return -EINVAL;

	idx = it6625_csi_mbus_code_idx(format->format.code);
	if (idx < 0) {
		v4l2_dbg(1, debug, sd,
			 "%s: unsupported format code 0x%x, falling back to default",
			 __func__, format->format.code);
		idx = 0;
	}

	csi_format = it6625_formats[idx].csi_format;

	/* fmt already carries width/height/field for this state; leave alone */
	fmt = v4l2_subdev_state_get_format(sd_state, format->pad);

	if (format->which == V4L2_SUBDEV_FORMAT_ACTIVE) {
		if (v4l2_subdev_is_streaming(sd))
			return -EBUSY;

		guard(mutex)(&it6625->it6625_lock);

		ret = it6625_enable_stream_locked(it6625, false);
		if (ret)
			return ret;

		ret = it6625_set_mipi_config_locked(it6625, csi_format);
		if (ret)
			return ret;
	}

	/*
	 * TRY: commit unconditionally. ACTIVE: commit only after the
	 * hardware programming above actually succeeded.
	 */
	fmt->code = it6625_formats[idx].mbus_fmt_code;
	fmt->colorspace = format_to_colorspace(csi_format);
	format->format = *fmt;

	if (format->which == V4L2_SUBDEV_FORMAT_TRY)
		v4l2_dbg(1, debug, sd, "%s: try format code = 0x%x",
			 __func__, format->format.code);

	return 0;
}

static int it6625_g_edid(struct v4l2_subdev *sd,
			 struct v4l2_subdev_edid *edid)
{
	struct it6625 *it6625 = sd_to_6625(sd);
	int err;

	if (edid->pad != 0)
		return -EINVAL;

	memset(edid->reserved, 0, sizeof(edid->reserved));

	guard(mutex)(&it6625->edid_lock);

	if (edid->start_block == 0 && edid->blocks == 0) {
		edid->blocks = it6625->edid_blocks;
		return 0;
	}

	if (it6625->edid_blocks == 0)
		return -ENODATA;

	if (edid->start_block >= it6625->edid_blocks || edid->blocks == 0)
		return -EINVAL;

	if (edid->blocks > it6625->edid_blocks - edid->start_block)
		edid->blocks = it6625->edid_blocks - edid->start_block;

	err = it6625_read_edid(it6625, edid->edid, edid->start_block,
			       edid->blocks);

	return (err < 0) ? err : 0;
}

static int it6625_s_edid(struct v4l2_subdev *sd,
			 struct v4l2_subdev_edid *edid)
{
	struct it6625 *it6625 = sd_to_6625(sd);
	u16 parent_pa = CEC_PHYS_ADDR_INVALID;
	int err;

	if (edid->pad != 0) {
		v4l2_err(sd, "invalid pad %d", edid->pad);
		return -EINVAL;
	}

	memset(edid->reserved, 0, sizeof(edid->reserved));

	if (edid->start_block != 0) {
		v4l2_err(sd, "start_block must be 0 for set edid");
		return -EINVAL;
	}

	if (edid->blocks > EDID_NUM_BLOCKS_MAX) {
		v4l2_err(sd, "too many edid blocks: %d", edid->blocks);
		edid->blocks = EDID_NUM_BLOCKS_MAX;
		return -E2BIG;
	}

	if (edid->blocks != 0) {
		u16 pa = v4l2_get_edid_phys_addr(edid->edid,
						 edid->blocks * 128, NULL);
		err = v4l2_phys_addr_validate(pa, &parent_pa, NULL);
		if (err) {
			v4l2_err(sd, "invalid CEC physical address in EDID");
			return err;
		}
	}

	guard(mutex)(&it6625->edid_lock);

	it6625_disable_hpd(it6625);
	cec_phys_addr_invalidate(it6625->cec_adap);
	it6625->edid_blocks = 0;

	if (edid->blocks == 0)
		return 0;

	err = it6625_write_edid(it6625, edid->edid,
				edid->start_block, edid->blocks);
	if (err < 0) {
		v4l2_err(sd, "write edid failed");
		return err;
	}

	it6625->edid_blocks = edid->blocks;
	cec_s_phys_addr(it6625->cec_adap, parent_pa, false);

	if (hdmi_5v_power_present(it6625)) {
		it6625_enable_hpd(it6625);
		it6625_s_ctrl_detect_hdmi_5v(sd);
	} else {
		it6625_enable_auto_hpd(it6625);
	}

	return 0;
}

static const struct v4l2_subdev_core_ops it6625_core_ops = {
	.log_status = it6625_log_status,
	.interrupt_service_routine = it6625_isr,
	.subscribe_event = it6625_subscribe_event,
	.unsubscribe_event = v4l2_event_subdev_unsubscribe,
};

static const struct v4l2_subdev_video_ops it6625_video_ops = {
	.g_input_status = it6625_g_input_status,
	.s_stream = v4l2_subdev_s_stream_helper,
};

static const struct v4l2_subdev_pad_ops it6625_pad_ops = {
	.enum_mbus_code = it6625_enum_mbus_code,
	.set_fmt = it6625_set_fmt,
	.get_fmt = v4l2_subdev_get_fmt,
	.get_edid = it6625_g_edid,
	.set_edid = it6625_s_edid,
	.enum_dv_timings = it6625_enum_dv_timings,
	.dv_timings_cap = it6625_dv_timings_cap,
	.get_mbus_config = it6625_get_mbus_config,
	.s_dv_timings = it6625_pad_s_dv_timings,
	.g_dv_timings = it6625_pad_g_dv_timings,
	.query_dv_timings = it6625_pad_query_dv_timings,
	.enable_streams = it6625_enable_streams,
	.disable_streams = it6625_disable_streams,
};

static const struct v4l2_subdev_ops it6625_ops = {
	.core = &it6625_core_ops,
	.video = &it6625_video_ops,
	.pad = &it6625_pad_ops,
};

static int it6625_init_state(struct v4l2_subdev *sd,
			     struct v4l2_subdev_state *sd_state)
{
	struct it6625 *it6625 = sd_to_6625(sd);
	struct v4l2_mbus_framefmt *fmt = v4l2_subdev_state_get_format(sd_state, 0);

	scoped_guard(mutex, &it6625->it6625_lock)
		it6625_fill_timings_format(&it6625->timings, fmt);

	fmt->code = it6625_formats[0].mbus_fmt_code;
	fmt->colorspace = format_to_colorspace(it6625_formats[0].csi_format);

	return 0;
}

static const struct v4l2_subdev_internal_ops it6625_internal_ops = {
	.init_state = it6625_init_state,
};

static const struct v4l2_ctrl_config it6625_ctrl_audio_sampling_rate = {
	.id = V4L2_CID_IT6625_AUDIO_SAMPLING_RATE,
	.name = "Audio Sampling Rate",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.min = 0,
	.max = 1536000,
	.step = 1,
	.def = 0,
	.flags = V4L2_CTRL_FLAG_READ_ONLY,
};

static const struct v4l2_ctrl_config it6625_ctrl_audio_present = {
	.id = V4L2_CID_IT6625_AUDIO_PRESENT,
	.name = "Audio Present",
	.type = V4L2_CTRL_TYPE_BOOLEAN,
	.min = 0,
	.max = 1,
	.step = 1,
	.def = 0,
	.flags = V4L2_CTRL_FLAG_READ_ONLY,
};

static int it6625_v4l2_init_controls(struct v4l2_subdev *sd)
{
	struct it6625 *it6625 = sd_to_6625(sd);
	struct v4l2_ctrl_handler *hdl = &it6625->hdl;
	bool cphy_3trio = it6625->bus_type == V4L2_MBUS_CSI2_CPHY &&
			   it6625->csi_lanes == 3;

	v4l2_ctrl_handler_init(hdl, 4);

	it6625->ctrl_5v_detect =
		v4l2_ctrl_new_std(hdl, NULL, V4L2_CID_DV_RX_POWER_PRESENT,
				  0, 1, 0, 0);

	it6625->ctrl_audio_sampling_rate =
		v4l2_ctrl_new_custom(hdl,
				     &it6625_ctrl_audio_sampling_rate,
				     NULL);
	it6625->ctrl_audio_present =
		v4l2_ctrl_new_custom(hdl, &it6625_ctrl_audio_present, NULL);
	it6625->ctrl_link_freq =
		v4l2_ctrl_new_int_menu(hdl, NULL, V4L2_CID_LINK_FREQ, 0, 0,
				       cphy_3trio ? it6626_cphy_3trio_link_freq :
						    it6625_link_freq_default);
	if (hdl->error) {
		v4l2_err(sd, "Failed to initialize controls");
		v4l2_ctrl_handler_free(hdl);
		return hdl->error;
	}

	sd->ctrl_handler = hdl;

	return 0;
}

static void it6625_regdump_print(struct seq_file *s, const u8 *reg_buf)
{
	seq_puts(s, "     0x00 0x01 0x02 0x03 0x04 0x05 0x06 0x07 0x08 0x09 0x0A 0x0B 0x0C 0x0D 0x0E 0x0F\n");

	for (unsigned int i = 0; i < 256; i++) {
		if (i % 16 == 0)
			seq_printf(s, "[%02X] ", i & 0xF0);
		seq_printf(s, "0x%02X ", reg_buf[i]);
		if (i % 16 == 15)
			seq_putc(s, '\n');
	}
}

static int it6625_mipi_reg_show(struct seq_file *s, void *data)
{
	struct it6625 *it6625 = s->private;
	u8 reg_buf[256];
	int ret;

	scoped_guard(mutex, &it6625->it6625_lock)
		ret = it6625_read_bytes(it6625, 0x00, reg_buf, sizeof(reg_buf));
	if (ret < 0)
		return ret;
	it6625_regdump_print(s, reg_buf);

	return 0;
}

static int it6625_mipi_reg_open(struct inode *inode, struct file *file)
{
	return single_open(file, it6625_mipi_reg_show, inode->i_private);
}

static ssize_t it6625_mipi_reg_write(struct file *file,
				     const char __user *user_buf,
				     size_t count, loff_t *ppos)
{
	struct it6625 *it6625 = file_inode(file)->i_private;
	char buf[32] = {};
	unsigned int addr, val;
	ssize_t len;
	loff_t pos = 0;

	len = simple_write_to_buffer(buf, sizeof(buf) - 1, &pos, user_buf, count);
	if (len < 0)
		return len;
	buf[len] = '\0';

	if (sscanf(buf, "%X %X", &addr, &val) != 2)
		return -EINVAL;

	scoped_guard(mutex, &it6625->it6625_lock)
		it6625_write_byte(it6625, addr, val);

	return count;
}

static const struct file_operations it6625_mipi_reg_fops = {
	.owner = THIS_MODULE,
	.open = it6625_mipi_reg_open,
	.read = seq_read,
	.write = it6625_mipi_reg_write,
	.llseek = seq_lseek,
	.release = single_release,
};

/*
 * Maps a V4L2_DEBUGFS_IF_* framework flag to the actual HDMI packet-type
 * header byte REG_IF_LATCH_HB expects to arm that InfoFrame type.
 */
static int it6625_if_packet_type(u32 type)
{
	switch (type) {
	case V4L2_DEBUGFS_IF_AVI:
		return 0x82;
	case V4L2_DEBUGFS_IF_AUDIO:
		return 0x84;
	case V4L2_DEBUGFS_IF_SPD:
		return 0x83;
	case V4L2_DEBUGFS_IF_HDMI:
		return 0x81;
	default:
		return -EINVAL;
	}
}

static ssize_t it6625_debugfs_if_read(u32 type, void *priv, struct file *filp,
				      char __user *ubuf, size_t count,
				      loff_t *ppos)
{
	struct v4l2_subdev *sd = priv;
	struct it6625 *it6625 = sd_to_6625(sd);
	u8 buf[32] = {};
	int packet_type;
	int err, err_reset;
	bool captured;
	int len;

	packet_type = it6625_if_packet_type(type);
	if (packet_type < 0)
		return 0;

	guard(mutex)(&it6625->if_read_lock);

	scoped_guard(mutex, &it6625->if_state_lock) {
		scoped_guard(mutex, &it6625->it6625_lock)
			err = it6625_write_byte(it6625, REG_IF_LATCH_HB, 0);
		if (err)
			return err;

		err = it6625_drain_interrupts(it6625, NULL);
		if (err)
			return err;

		reinit_completion(&it6625->if_latched);

		it6625->if_active = true;
		it6625->if_type = packet_type;
		it6625->if_snapshot_done = false;
		it6625->if_snapshot_err = 0;

		scoped_guard(mutex, &it6625->it6625_lock)
			err = it6625_write_byte(it6625, REG_IF_LATCH_HB,
						packet_type);
		if (err) {
			scoped_guard(mutex, &it6625->it6625_lock)
				it6625_write_byte(it6625, REG_IF_LATCH_HB, 0);
			it6625->if_active = false;
			return err;
		}
	}

	wait_for_completion_timeout(&it6625->if_latched, msecs_to_jiffies(100));

	scoped_guard(mutex, &it6625->if_state_lock) {
		captured = it6625->if_snapshot_done;
		it6625->if_active = false;

		scoped_guard(mutex, &it6625->it6625_lock)
			err_reset = it6625_write_byte(it6625, REG_IF_LATCH_HB, 0);

		if (!captured) {
			if (err_reset)
				return err_reset;
			return 0;
		}

		if (!it6625->if_snapshot_err) {
			buf[0] = packet_type;
			memcpy(&buf[1], it6625->if_snapshot, 31);
		}

		err = it6625->if_snapshot_err ? it6625->if_snapshot_err : err_reset;
		if (err)
			return err;
	}

	len = buf[2] ? buf[2] + 4 : -ENOENT;
	if (len > (int)sizeof(buf))
		len = -ENOENT;
	if (len < 0)
		return 0;
	return simple_read_from_buffer(ubuf, count, ppos, buf, len);
}

static void it6625_debugfs_init(struct it6625 *it6625, struct i2c_client *client)
{
	it6625->debugfs_dir = debugfs_create_dir(dev_name(&client->dev), NULL);

	debugfs_create_file("mipi_reg", 0600, it6625->debugfs_dir, it6625,
			    &it6625_mipi_reg_fops);

	it6625->infoframes = v4l2_debugfs_if_alloc(it6625->debugfs_dir,
						   V4L2_DEBUGFS_IF_AVI | V4L2_DEBUGFS_IF_AUDIO |
						   V4L2_DEBUGFS_IF_SPD | V4L2_DEBUGFS_IF_HDMI,
						   &it6625->sd, it6625_debugfs_if_read);
}

static void it6625_init_data(struct it6625 *it6625)
{
	static struct v4l2_dv_timings default_timing =
			V4L2_DV_BT_CEA_1920X1080P60;

	it6625->timings = default_timing;
	/* firmware ships with a verified 2-block default EDID in EDID RAM */
	it6625->edid_blocks = 2;
}

static int it6625_parse_endpoint(struct it6625 *it6625)
{
	struct device *dev = it6625->dev;
	/*
	 * Pre-setting bus_type here makes v4l2_fwnode_endpoint_alloc_parse()
	 * treat it as a hard requirement and reject any endpoint whose DT
	 * bus-type disagrees, so this must stay V4L2_MBUS_UNKNOWN to let it
	 * autodetect C-PHY vs D-PHY from the endpoint itself.
	 */
	struct v4l2_fwnode_endpoint endpoint = { .bus_type = V4L2_MBUS_UNKNOWN };
	struct device_node *ep = NULL;
	unsigned int max_lanes;
	unsigned int port;
	int ret;

	/*
	 * port@0 and port@1 are the two CSI-2 output ports MIPI0/MIPI1
	 * (port@2 is the HDMI input). This chip series can drive both
	 * simultaneously in split or mirror mode, so port_num counts how
	 * many of MIPI0/MIPI1 have an endpoint wired up. This driver only
	 * wires up a single source pad, so lane/bus-type configuration is
	 * parsed from whichever of the two is found first.
	 */
	it6625->port_num = 0;
	for (port = 0; port < 2; port++) {
		struct device_node *port_ep =
			of_graph_get_endpoint_by_regs(dev->of_node, port, -1);

		if (!port_ep)
			continue;

		it6625->port_num++;
		if (!ep)
			ep = port_ep;
		else
			of_node_put(port_ep);
	}

	ret = v4l2_fwnode_endpoint_alloc_parse(of_fwnode_handle(ep), &endpoint);
	of_node_put(ep);
	if (ret)
		return dev_err_probe(dev, ret, "failed to parse endpoint");

	if (endpoint.bus_type != V4L2_MBUS_CSI2_DPHY &&
	    endpoint.bus_type != V4L2_MBUS_CSI2_CPHY) {
		dev_err(dev, "unsupported bus type %d, expected CSI-2 D-PHY or C-PHY",
			endpoint.bus_type);
		ret = -EINVAL;
		goto out_free_endpoint;
	}

	if (endpoint.bus_type == V4L2_MBUS_CSI2_CPHY &&
	    it6625->chip_type != IT6626_CHIP) {
		dev_err(dev, "IT6625 does not support C-PHY, only IT6626 does");
		ret = -EINVAL;
		goto out_free_endpoint;
	}

	max_lanes = (endpoint.bus_type == V4L2_MBUS_CSI2_CPHY) ? 3 : 4;

	if (endpoint.bus.mipi_csi2.num_data_lanes == 0 ||
	    endpoint.bus.mipi_csi2.num_data_lanes > max_lanes) {
		dev_err(dev,
			"invalid number of CSI data lanes: %u (max %u for this bus type)",
			endpoint.bus.mipi_csi2.num_data_lanes, max_lanes);
		ret = -EINVAL;
		goto out_free_endpoint;
	}

	it6625->csi_lanes = endpoint.bus.mipi_csi2.num_data_lanes;
	it6625->bus_type = endpoint.bus_type;

out_free_endpoint:
	v4l2_fwnode_endpoint_free(&endpoint);

	return ret;
}

static int it6625_parse_dt(struct it6625 *it6625)
{
	return it6625_parse_endpoint(it6625);
}

static int it6625_check_device(struct it6625 *it6625)
{
	static const u8 chip_ids[][2] = {
		{ 0x66, 0x25 },
		{ 0x66, 0x26 },
	};
	int chip_id0, chip_id1;

	chip_id0 = it6625_read_byte(it6625, REG_CHIP_ID_0);
	chip_id1 = it6625_read_byte(it6625, REG_CHIP_ID_1);
	if (chip_id0 != chip_ids[it6625->chip_type][0] ||
	    chip_id1 != chip_ids[it6625->chip_type][1]) {
		dev_err(it6625->dev,
			"chip ID mismatch: got 0x%02x%02x, expected 0x%02x%02x",
			chip_id0, chip_id1,
			chip_ids[it6625->chip_type][0],
			chip_ids[it6625->chip_type][1]);
		return -ENODEV;
	}

	return 0;
}

static int it6625_probe(struct i2c_client *client)
{
	struct it6625 *it6625;
	struct v4l2_subdev *sd;
	int err;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -EIO;

	it6625 = devm_kzalloc(&client->dev, sizeof(struct it6625), GFP_KERNEL);
	if (!it6625)
		return -ENOMEM;

	it6625->chip_type = (uintptr_t)i2c_get_match_data(client);

	it6625->reset_gpio = devm_gpiod_get_optional(&client->dev, "reset",
						     GPIOD_OUT_HIGH);
	if (IS_ERR(it6625->reset_gpio))
		return PTR_ERR(it6625->reset_gpio);

	if (it6625->reset_gpio) {
		usleep_range(1000, 2000);
		gpiod_set_value_cansleep(it6625->reset_gpio, 0);
		usleep_range(10000, 11000);
	}

	err = it6625_regmap_i2c_init(client, it6625);
	if (err)
		return err;

	err = it6625_check_device(it6625);
	if (err)
		return err;

	it6625_init_data(it6625);

	err = it6625_parse_dt(it6625);
	if (err)
		return err;

	mutex_init(&it6625->it6625_lock);
	mutex_init(&it6625->edid_lock);
	mutex_init(&it6625->if_read_lock);
	mutex_init(&it6625->if_state_lock);
	init_completion(&it6625->if_latched);
	INIT_DELAYED_WORK(&it6625->hpd_delayed_work, it6625_hpd_delayed_work);
	INIT_WORK(&it6625->polling_work, it6625_polling_work);

	if (client->irq) {
		err = devm_request_threaded_irq(&client->dev, client->irq,
						NULL, it6625_irq_handler,
						IRQF_ONESHOT |
						IRQF_NO_AUTOEN,
						"it6625", it6625);
		if (err)
			goto err_clean_work_queues;
	} else {
		dev_info(it6625->dev, "no IRQ, falling back to polling");
		timer_setup(&it6625->timer, it6625_irq_poll_timer, 0);
	}

	sd = &it6625->sd;
	v4l2_i2c_subdev_init(sd, it6625->i2c_client, &it6625_ops);
	sd->internal_ops = &it6625_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE | V4L2_SUBDEV_FL_HAS_EVENTS;

	err = it6625_v4l2_init_controls(sd);
	if (err) {
		dev_err(it6625->dev, "failed to initialize v4l2 controls: %d", err);
		goto err_clean_work_queues;
	}

	it6625->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	err = media_entity_pads_init(&sd->entity, 1, &it6625->pad);
	if (err < 0) {
		dev_err(it6625->dev, "%s %d err=%d", __func__, __LINE__, err);
		goto err_clean_ctrl_handler;
	}

	sd->state_lock = sd->ctrl_handler->lock;
	err = v4l2_subdev_init_finalize(sd);
	if (err) {
		dev_err(it6625->dev, "%s %d err=%d", __func__, __LINE__, err);
		goto err_clean_hdl;
	}

	err = v4l2_ctrl_handler_setup(sd->ctrl_handler);
	if (err)
		goto err_clean_state;

	it6625->cec_adap = cec_allocate_adapter(&it6625_cec_adap_ops,
						it6625, dev_name(it6625->dev),
						CEC_CAP_DEFAULTS |
						CEC_CAP_MONITOR_ALL |
						CEC_CAP_PHYS_ADDR,
						1);
	if (IS_ERR(it6625->cec_adap)) {
		err = PTR_ERR(it6625->cec_adap);
		dev_err(it6625->dev, "%s %d", __func__, __LINE__);
		goto err_clean_state;
	}

	err = cec_register_adapter(it6625->cec_adap, &client->dev);
	if (err < 0) {
		dev_err(it6625->dev, "%s: failed to register the cec device", __func__);
		cec_delete_adapter(it6625->cec_adap);
		it6625->cec_adap = NULL;
		goto err_clean_state;
	}

	it6625_debugfs_init(it6625, client);

	err = it6625_initial_setup(it6625);
	if (err) {
		dev_err_probe(it6625->dev, err, "failed initial hardware setup");
		goto err_clean_debugfs;
	}

	err = it6625_v4l2_sd_ctrl_update(sd);
	if (err) {
		dev_err_probe(it6625->dev, err, "failed to update controls");
		goto err_clean_debugfs;
	}

	err = v4l2_async_register_subdev(sd);
	if (err < 0) {
		dev_err(it6625->dev, "%s %d err=%d", __func__, __LINE__, err);
		goto err_clean_debugfs;
	}

	if (client->irq)
		enable_irq(client->irq);
	else
		mod_timer(&it6625->timer, jiffies + msecs_to_jiffies(POLL_INTERVAL_MS));

	return 0;

err_clean_debugfs:
	v4l2_debugfs_if_free(it6625->infoframes);
	debugfs_remove_recursive(it6625->debugfs_dir);
	cec_unregister_adapter(it6625->cec_adap);
err_clean_state:
	v4l2_subdev_cleanup(sd);
err_clean_hdl:
	media_entity_cleanup(&sd->entity);
err_clean_ctrl_handler:
	v4l2_ctrl_handler_free(&it6625->hdl);

err_clean_work_queues:
	if (!client->irq)
		timer_shutdown_sync(&it6625->timer);
	cancel_work_sync(&it6625->polling_work);
	cancel_delayed_work_sync(&it6625->hpd_delayed_work);
	mutex_destroy(&it6625->it6625_lock);
	mutex_destroy(&it6625->edid_lock);
	mutex_destroy(&it6625->if_read_lock);
	mutex_destroy(&it6625->if_state_lock);
	return err;
}

static void it6625_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct it6625 *it6625 = sd_to_6625(sd);

	v4l2_debugfs_if_free(it6625->infoframes);

	if (client->irq)
		disable_irq(client->irq);
	else
		timer_shutdown_sync(&it6625->timer);

	cancel_work_sync(&it6625->polling_work);
	cancel_delayed_work_sync(&it6625->hpd_delayed_work);

	v4l2_async_unregister_subdev(sd);
	v4l2_device_unregister_subdev(sd);

	debugfs_remove_recursive(it6625->debugfs_dir);
	cec_unregister_adapter(it6625->cec_adap);

	v4l2_subdev_cleanup(sd);
	media_entity_cleanup(&sd->entity);
	v4l2_ctrl_handler_free(&it6625->hdl);

	mutex_destroy(&it6625->it6625_lock);
	mutex_destroy(&it6625->edid_lock);
	mutex_destroy(&it6625->if_read_lock);
	mutex_destroy(&it6625->if_state_lock);
}

static const struct i2c_device_id it6625_id[] = {
	{ .name = "it6625", .driver_data = IT6625_CHIP },
	{ .name = "it6626", .driver_data = IT6626_CHIP },
	{}
};
MODULE_DEVICE_TABLE(i2c, it6625_id);

static const struct of_device_id it6625_of_match[] = {
	{ .compatible = "ite,it6625", .data = (void *)IT6625_CHIP },
	{ .compatible = "ite,it6626", .data = (void *)IT6626_CHIP },
	{},
};
MODULE_DEVICE_TABLE(of, it6625_of_match);

static struct i2c_driver it6625_driver = {
	.driver = {
		.name = "it6625",
		.of_match_table = it6625_of_match,
	},
	.probe = it6625_probe,
	.remove = it6625_remove,
	.id_table = it6625_id,
};
module_i2c_driver(it6625_driver);

MODULE_DESCRIPTION("iTE it6625/it6626 HDMI to MIPI CSI bridge driver");
MODULE_AUTHOR("Hermes Wu <Hermes.wu@ite.com.tw>");
MODULE_LICENSE("GPL");
