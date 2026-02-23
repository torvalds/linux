// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) Rockchip Electronics Co., Ltd.
 *    Zheng Yang <zhengyang@rock-chips.com>
 *    Yakir Yang <ykk@rock-chips.com>
 *    Andy Yan <andyshrk@163.com>
 */

#include <linux/irq.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/hdmi.h>
#include <linux/mfd/syscon.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include <drm/bridge/inno_hdmi.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_edid.h>
#include <drm/drm_of.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_simple_kms_helper.h>

#include <drm/display/drm_hdmi_helper.h>
#include <drm/display/drm_hdmi_state_helper.h>

#define INNO_HDMI_MIN_TMDS_CLOCK  25000000U

#define DDC_SEGMENT_ADDR		0x30

#define HDMI_SCL_RATE			(100 * 1000)

#define DDC_BUS_FREQ_L			0x4b
#define DDC_BUS_FREQ_H			0x4c

#define HDMI_SYS_CTRL			0x00
#define m_RST_ANALOG			BIT(6)
#define v_RST_ANALOG			(0 << 6)
#define v_NOT_RST_ANALOG		BIT(6)
#define m_RST_DIGITAL			BIT(5)
#define v_RST_DIGITAL			(0 << 5)
#define v_NOT_RST_DIGITAL		BIT(5)
#define m_REG_CLK_INV			BIT(4)
#define v_REG_CLK_NOT_INV		(0 << 4)
#define v_REG_CLK_INV			BIT(4)
#define m_VCLK_INV			BIT(3)
#define v_VCLK_NOT_INV			(0 << 3)
#define v_VCLK_INV			BIT(3)
#define m_REG_CLK_SOURCE		BIT(2)
#define v_REG_CLK_SOURCE_TMDS		(0 << 2)
#define v_REG_CLK_SOURCE_SYS		BIT(2)
#define m_POWER				BIT(1)
#define v_PWR_ON			(0 << 1)
#define v_PWR_OFF			BIT(1)
#define m_INT_POL			BIT(0)
#define v_INT_POL_HIGH			1
#define v_INT_POL_LOW			0

#define HDMI_VIDEO_CONTRL1		0x01
#define m_VIDEO_INPUT_FORMAT		(7 << 1)
#define m_DE_SOURCE			BIT(0)
#define v_VIDEO_INPUT_FORMAT(n)		((n) << 1)
#define v_DE_EXTERNAL			1
#define v_DE_INTERNAL			0
enum {
	VIDEO_INPUT_SDR_RGB444 = 0,
	VIDEO_INPUT_DDR_RGB444 = 5,
	VIDEO_INPUT_DDR_YCBCR422 = 6
};

#define HDMI_VIDEO_CONTRL2		0x02
#define m_VIDEO_OUTPUT_COLOR		(3 << 6)
#define m_VIDEO_INPUT_BITS		(3 << 4)
#define m_VIDEO_INPUT_CSP		BIT(0)
#define v_VIDEO_OUTPUT_COLOR(n)		(((n) & 0x3) << 6)
#define v_VIDEO_INPUT_BITS(n)		((n) << 4)
#define v_VIDEO_INPUT_CSP(n)		((n) << 0)
enum {
	VIDEO_INPUT_12BITS = 0,
	VIDEO_INPUT_10BITS = 1,
	VIDEO_INPUT_REVERT = 2,
	VIDEO_INPUT_8BITS = 3,
};

#define HDMI_VIDEO_CONTRL		0x03
#define m_VIDEO_AUTO_CSC		BIT(7)
#define v_VIDEO_AUTO_CSC(n)		((n) << 7)
#define m_VIDEO_C0_C2_SWAP		BIT(0)
#define v_VIDEO_C0_C2_SWAP(n)		((n) << 0)
enum {
	C0_C2_CHANGE_ENABLE = 0,
	C0_C2_CHANGE_DISABLE = 1,
	AUTO_CSC_DISABLE = 0,
	AUTO_CSC_ENABLE = 1,
};

#define HDMI_VIDEO_CONTRL3		0x04
#define m_COLOR_DEPTH_NOT_INDICATED	BIT(4)
#define m_SOF				BIT(3)
#define m_COLOR_RANGE			BIT(2)
#define m_CSC				BIT(0)
#define v_COLOR_DEPTH_NOT_INDICATED(n)	((n) << 4)
#define v_SOF_ENABLE			(0 << 3)
#define v_SOF_DISABLE			BIT(3)
#define v_COLOR_RANGE_FULL		BIT(2)
#define v_COLOR_RANGE_LIMITED		(0 << 2)
#define v_CSC_ENABLE			1
#define v_CSC_DISABLE			0

#define HDMI_AV_MUTE			0x05
#define m_AVMUTE_CLEAR			BIT(7)
#define m_AVMUTE_ENABLE			BIT(6)
#define m_AUDIO_MUTE			BIT(1)
#define m_VIDEO_BLACK			BIT(0)
#define v_AVMUTE_CLEAR(n)		((n) << 7)
#define v_AVMUTE_ENABLE(n)		((n) << 6)
#define v_AUDIO_MUTE(n)			((n) << 1)
#define v_VIDEO_MUTE(n)			((n) << 0)

#define HDMI_VIDEO_TIMING_CTL		0x08
#define v_HSYNC_POLARITY(n)		((n) << 3)
#define v_VSYNC_POLARITY(n)		((n) << 2)
#define v_INETLACE(n)			((n) << 1)
#define v_EXTERANL_VIDEO(n)		((n) << 0)

#define HDMI_VIDEO_EXT_HTOTAL_L		0x09
#define HDMI_VIDEO_EXT_HTOTAL_H		0x0a
#define HDMI_VIDEO_EXT_HBLANK_L		0x0b
#define HDMI_VIDEO_EXT_HBLANK_H		0x0c
#define HDMI_VIDEO_EXT_HDELAY_L		0x0d
#define HDMI_VIDEO_EXT_HDELAY_H		0x0e
#define HDMI_VIDEO_EXT_HDURATION_L	0x0f
#define HDMI_VIDEO_EXT_HDURATION_H	0x10
#define HDMI_VIDEO_EXT_VTOTAL_L		0x11
#define HDMI_VIDEO_EXT_VTOTAL_H		0x12
#define HDMI_VIDEO_EXT_VBLANK		0x13
#define HDMI_VIDEO_EXT_VDELAY		0x14
#define HDMI_VIDEO_EXT_VDURATION	0x15

#define HDMI_VIDEO_CSC_COEF		0x18

#define HDMI_AUDIO_CTRL1		0x35
enum {
	CTS_SOURCE_INTERNAL = 0,
	CTS_SOURCE_EXTERNAL = 1,
};

#define v_CTS_SOURCE(n)			((n) << 7)

enum {
	DOWNSAMPLE_DISABLE = 0,
	DOWNSAMPLE_1_2 = 1,
	DOWNSAMPLE_1_4 = 2,
};

#define v_DOWN_SAMPLE(n)		((n) << 5)

enum {
	AUDIO_SOURCE_IIS = 0,
	AUDIO_SOURCE_SPDIF = 1,
};

#define v_AUDIO_SOURCE(n)		((n) << 3)

#define v_MCLK_ENABLE(n)		((n) << 2)

enum {
	MCLK_128FS = 0,
	MCLK_256FS = 1,
	MCLK_384FS = 2,
	MCLK_512FS = 3,
};

#define v_MCLK_RATIO(n)			(n)

#define AUDIO_SAMPLE_RATE		0x37

enum {
	AUDIO_32K = 0x3,
	AUDIO_441K = 0x0,
	AUDIO_48K = 0x2,
	AUDIO_882K = 0x8,
	AUDIO_96K = 0xa,
	AUDIO_1764K = 0xc,
	AUDIO_192K = 0xe,
};

#define AUDIO_I2S_MODE			0x38

enum {
	I2S_CHANNEL_1_2 = 1,
	I2S_CHANNEL_3_4 = 3,
	I2S_CHANNEL_5_6 = 7,
	I2S_CHANNEL_7_8 = 0xf
};

#define v_I2S_CHANNEL(n)		((n) << 2)

enum {
	I2S_STANDARD = 0,
	I2S_LEFT_JUSTIFIED = 1,
	I2S_RIGHT_JUSTIFIED = 2,
};

#define v_I2S_MODE(n)			(n)

#define AUDIO_I2S_MAP			0x39
#define AUDIO_I2S_SWAPS_SPDIF		0x3a
#define v_SPIDF_FREQ(n)			(n)

#define N_32K				0x1000
#define N_441K				0x1880
#define N_882K				0x3100
#define N_1764K				0x6200
#define N_48K				0x1800
#define N_96K				0x3000
#define N_192K				0x6000

#define HDMI_AUDIO_CHANNEL_STATUS	0x3e
#define m_AUDIO_STATUS_NLPCM		BIT(7)
#define m_AUDIO_STATUS_USE		BIT(6)
#define m_AUDIO_STATUS_COPYRIGHT	BIT(5)
#define m_AUDIO_STATUS_ADDITION		(3 << 2)
#define m_AUDIO_STATUS_CLK_ACCURACY	(2 << 0)
#define v_AUDIO_STATUS_NLPCM(n)		(((n) & 1) << 7)
#define AUDIO_N_H			0x3f
#define AUDIO_N_M			0x40
#define AUDIO_N_L			0x41

#define HDMI_AUDIO_CTS_H		0x45
#define HDMI_AUDIO_CTS_M		0x46
#define HDMI_AUDIO_CTS_L		0x47

#define HDMI_DDC_CLK_L			0x4b
#define HDMI_DDC_CLK_H			0x4c

#define HDMI_EDID_SEGMENT_POINTER	0x4d
#define HDMI_EDID_WORD_ADDR		0x4e
#define HDMI_EDID_FIFO_OFFSET		0x4f
#define HDMI_EDID_FIFO_ADDR		0x50

#define HDMI_PACKET_SEND_MANUAL		0x9c
#define HDMI_PACKET_SEND_AUTO		0x9d
#define m_PACKET_GCP_EN			BIT(7)
#define m_PACKET_MSI_EN			BIT(6)
#define m_PACKET_SDI_EN			BIT(5)
#define m_PACKET_VSI_EN			BIT(4)
#define v_PACKET_GCP_EN(n)		(((n) & 1) << 7)
#define v_PACKET_MSI_EN(n)		(((n) & 1) << 6)
#define v_PACKET_SDI_EN(n)		(((n) & 1) << 5)
#define v_PACKET_VSI_EN(n)		(((n) & 1) << 4)

#define HDMI_CONTROL_PACKET_BUF_INDEX	0x9f

enum {
	INFOFRAME_VSI = 0x05,
	INFOFRAME_AVI = 0x06,
	INFOFRAME_AAI = 0x08,
};

#define HDMI_CONTROL_PACKET_ADDR	0xa0
#define HDMI_MAXIMUM_INFO_FRAME_SIZE	0x11

enum {
	AVI_COLOR_MODE_RGB = 0,
	AVI_COLOR_MODE_YCBCR422 = 1,
	AVI_COLOR_MODE_YCBCR444 = 2,
	AVI_COLORIMETRY_NO_DATA = 0,

	AVI_COLORIMETRY_SMPTE_170M = 1,
	AVI_COLORIMETRY_ITU709 = 2,
	AVI_COLORIMETRY_EXTENDED = 3,

	AVI_CODED_FRAME_ASPECT_NO_DATA = 0,
	AVI_CODED_FRAME_ASPECT_4_3 = 1,
	AVI_CODED_FRAME_ASPECT_16_9 = 2,

	ACTIVE_ASPECT_RATE_SAME_AS_CODED_FRAME = 0x08,
	ACTIVE_ASPECT_RATE_4_3 = 0x09,
	ACTIVE_ASPECT_RATE_16_9 = 0x0A,
	ACTIVE_ASPECT_RATE_14_9 = 0x0B,
};

#define HDMI_HDCP_CTRL			0x52
#define m_HDMI_DVI			BIT(1)
#define v_HDMI_DVI(n)			((n) << 1)

#define HDMI_INTERRUPT_MASK1		0xc0
#define HDMI_INTERRUPT_STATUS1		0xc1
#define	m_INT_ACTIVE_VSYNC		BIT(5)
#define m_INT_EDID_READY		BIT(2)

#define HDMI_INTERRUPT_MASK2		0xc2
#define HDMI_INTERRUPT_STATUS2		0xc3
#define m_INT_HDCP_ERR			BIT(7)
#define m_INT_BKSV_FLAG			BIT(6)
#define m_INT_HDCP_OK			BIT(4)

#define HDMI_STATUS			0xc8
#define m_HOTPLUG			BIT(7)
#define m_MASK_INT_HOTPLUG		BIT(5)
#define m_INT_HOTPLUG			BIT(1)
#define v_MASK_INT_HOTPLUG(n)		(((n) & 0x1) << 5)

#define HDMI_COLORBAR                   0xc9

#define HDMI_PHY_SYNC			0xce
#define HDMI_PHY_SYS_CTL		0xe0
#define m_TMDS_CLK_SOURCE		BIT(5)
#define v_TMDS_FROM_PLL			(0 << 5)
#define v_TMDS_FROM_GEN			BIT(5)
#define m_PHASE_CLK			BIT(4)
#define v_DEFAULT_PHASE			(0 << 4)
#define v_SYNC_PHASE			BIT(4)
#define m_TMDS_CURRENT_PWR		BIT(3)
#define v_TURN_ON_CURRENT		(0 << 3)
#define v_CAT_OFF_CURRENT		BIT(3)
#define m_BANDGAP_PWR			BIT(2)
#define v_BANDGAP_PWR_UP		(0 << 2)
#define v_BANDGAP_PWR_DOWN		BIT(2)
#define m_PLL_PWR			BIT(1)
#define v_PLL_PWR_UP			(0 << 1)
#define v_PLL_PWR_DOWN			BIT(1)
#define m_TMDS_CHG_PWR			BIT(0)
#define v_TMDS_CHG_PWR_UP		(0 << 0)
#define v_TMDS_CHG_PWR_DOWN		BIT(0)

#define HDMI_PHY_CHG_PWR		0xe1
#define v_CLK_CHG_PWR(n)		(((n) & 1) << 3)
#define v_DATA_CHG_PWR(n)		(((n) & 7) << 0)

#define HDMI_PHY_DRIVER			0xe2
#define v_CLK_MAIN_DRIVER(n)		((n) << 4)
#define v_DATA_MAIN_DRIVER(n)		((n) << 0)

#define HDMI_PHY_PRE_EMPHASIS		0xe3
#define v_PRE_EMPHASIS(n)		(((n) & 7) << 4)
#define v_CLK_PRE_DRIVER(n)		(((n) & 3) << 2)
#define v_DATA_PRE_DRIVER(n)		(((n) & 3) << 0)

#define HDMI_PHY_FEEDBACK_DIV_RATIO_LOW		0xe7
#define v_FEEDBACK_DIV_LOW(n)			((n) & 0xff)
#define HDMI_PHY_FEEDBACK_DIV_RATIO_HIGH	0xe8
#define v_FEEDBACK_DIV_HIGH(n)			((n) & 1)

#define HDMI_PHY_PRE_DIV_RATIO		0xed
#define v_PRE_DIV_RATIO(n)		((n) & 0x1f)

#define HDMI_CEC_CTRL			0xd0
#define m_ADJUST_FOR_HISENSE		BIT(6)
#define m_REJECT_RX_BROADCAST		BIT(5)
#define m_BUSFREETIME_ENABLE		BIT(2)
#define m_REJECT_RX			BIT(1)
#define m_START_TX			BIT(0)

#define HDMI_CEC_DATA			0xd1
#define HDMI_CEC_TX_OFFSET		0xd2
#define HDMI_CEC_RX_OFFSET		0xd3
#define HDMI_CEC_CLK_H			0xd4
#define HDMI_CEC_CLK_L			0xd5
#define HDMI_CEC_TX_LENGTH		0xd6
#define HDMI_CEC_RX_LENGTH		0xd7
#define HDMI_CEC_TX_INT_MASK		0xd8
#define m_TX_DONE			BIT(3)
#define m_TX_NOACK			BIT(2)
#define m_TX_BROADCAST_REJ		BIT(1)
#define m_TX_BUSNOTFREE			BIT(0)

#define HDMI_CEC_RX_INT_MASK		0xd9
#define m_RX_LA_ERR			BIT(4)
#define m_RX_GLITCH			BIT(3)
#define m_RX_DONE			BIT(0)

#define HDMI_CEC_TX_INT			0xda
#define HDMI_CEC_RX_INT			0xdb
#define HDMI_CEC_BUSFREETIME_L		0xdc
#define HDMI_CEC_BUSFREETIME_H		0xdd
#define HDMI_CEC_LOGICADDR		0xde

struct inno_hdmi_i2c {
	struct i2c_adapter adap;

	u8 ddc_addr;
	u8 segment_addr;

	struct mutex lock;
	struct completion cmp;
};

struct inno_hdmi {
	struct device *dev;
	struct drm_bridge bridge;
	struct clk *pclk;
	struct clk *refclk;
	void __iomem *regs;
	struct regmap *grf;

	struct inno_hdmi_i2c *i2c;
	struct i2c_adapter *ddc;
	const struct inno_hdmi_plat_data *plat_data;
};

enum {
	CSC_RGB_0_255_TO_ITU601_16_235_8BIT,
	CSC_RGB_0_255_TO_ITU709_16_235_8BIT,
	CSC_RGB_0_255_TO_RGB_16_235_8BIT,
};

static const char coeff_csc[][24] = {
	/*
	 * RGB2YUV:601 SD mode:
	 *   Cb = -0.291G - 0.148R + 0.439B + 128
	 *   Y  = 0.504G  + 0.257R + 0.098B + 16
	 *   Cr = -0.368G + 0.439R - 0.071B + 128
	 */
	{
		0x11, 0x5f, 0x01, 0x82, 0x10, 0x23, 0x00, 0x80,
		0x02, 0x1c, 0x00, 0xa1, 0x00, 0x36, 0x00, 0x1e,
		0x11, 0x29, 0x10, 0x59, 0x01, 0x82, 0x00, 0x80
	},
	/*
	 * RGB2YUV:709 HD mode:
	 *   Cb = - 0.338G - 0.101R + 0.439B + 128
	 *   Y  = 0.614G   + 0.183R + 0.062B + 16
	 *   Cr = - 0.399G + 0.439R - 0.040B + 128
	 */
	{
		0x11, 0x98, 0x01, 0xc1, 0x10, 0x28, 0x00, 0x80,
		0x02, 0x74, 0x00, 0xbb, 0x00, 0x3f, 0x00, 0x10,
		0x11, 0x5a, 0x10, 0x67, 0x01, 0xc1, 0x00, 0x80
	},
	/*
	 * RGB[0:255]2RGB[16:235]:
	 *   R' = R x (235-16)/255 + 16;
	 *   G' = G x (235-16)/255 + 16;
	 *   B' = B x (235-16)/255 + 16;
	 */
	{
		0x00, 0x00, 0x03, 0x6F, 0x00, 0x00, 0x00, 0x10,
		0x03, 0x6F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10,
		0x00, 0x00, 0x00, 0x00, 0x03, 0x6F, 0x00, 0x10
	},
};

static struct inno_hdmi *bridge_to_inno_hdmi(struct drm_bridge *bridge)
{
	return container_of(bridge, struct inno_hdmi, bridge);
}

static int inno_hdmi_find_phy_config(struct inno_hdmi *hdmi,
				     unsigned long pixelclk)
{
	const struct inno_hdmi_phy_config *phy_configs = hdmi->plat_data->phy_configs;
	int i;

	for (i = 0; phy_configs[i].pixelclock != ~0UL; i++) {
		if (pixelclk <= phy_configs[i].pixelclock)
			return i;
	}

	DRM_DEV_DEBUG(hdmi->dev, "No phy configuration for pixelclock %lu\n",
		      pixelclk);

	return -EINVAL;
}

static inline u8 hdmi_readb(struct inno_hdmi *hdmi, u16 offset)
{
	return readl_relaxed(hdmi->regs + (offset) * 0x04);
}

static inline void hdmi_writeb(struct inno_hdmi *hdmi, u16 offset, u32 val)
{
	writel_relaxed(val, hdmi->regs + (offset) * 0x04);
}

static inline void hdmi_modb(struct inno_hdmi *hdmi, u16 offset,
			     u32 msk, u32 val)
{
	u8 temp = hdmi_readb(hdmi, offset) & ~msk;

	temp |= val & msk;
	hdmi_writeb(hdmi, offset, temp);
}

static void inno_hdmi_i2c_init(struct inno_hdmi *hdmi, unsigned long long rate)
{
	unsigned long long ddc_bus_freq = rate >> 2;

	do_div(ddc_bus_freq, HDMI_SCL_RATE);

	hdmi_writeb(hdmi, DDC_BUS_FREQ_L, ddc_bus_freq & 0xFF);
	hdmi_writeb(hdmi, DDC_BUS_FREQ_H, (ddc_bus_freq >> 8) & 0xFF);

	/* Clear the EDID interrupt flag and mute the interrupt */
	hdmi_writeb(hdmi, HDMI_INTERRUPT_MASK1, 0);
	hdmi_writeb(hdmi, HDMI_INTERRUPT_STATUS1, m_INT_EDID_READY);
}

static void inno_hdmi_sys_power(struct inno_hdmi *hdmi, bool enable)
{
	if (enable)
		hdmi_modb(hdmi, HDMI_SYS_CTRL, m_POWER, v_PWR_ON);
	else
		hdmi_modb(hdmi, HDMI_SYS_CTRL, m_POWER, v_PWR_OFF);
}

static void inno_hdmi_standby(struct inno_hdmi *hdmi)
{
	inno_hdmi_sys_power(hdmi, false);

	hdmi_writeb(hdmi, HDMI_PHY_DRIVER, 0x00);
	hdmi_writeb(hdmi, HDMI_PHY_PRE_EMPHASIS, 0x00);
	hdmi_writeb(hdmi, HDMI_PHY_CHG_PWR, 0x00);
	hdmi_writeb(hdmi, HDMI_PHY_SYS_CTL, 0x15);
};

static void inno_hdmi_power_up(struct inno_hdmi *hdmi,
			       unsigned long mpixelclock)
{
	struct inno_hdmi_phy_config *phy_config;
	int ret = inno_hdmi_find_phy_config(hdmi, mpixelclock);

	if (ret < 0) {
		phy_config = hdmi->plat_data->default_phy_config;
		DRM_DEV_ERROR(hdmi->dev,
			      "Using default phy configuration for TMDS rate %lu",
			      mpixelclock);
	} else {
		phy_config = &hdmi->plat_data->phy_configs[ret];
	}

	inno_hdmi_sys_power(hdmi, false);

	hdmi_writeb(hdmi, HDMI_PHY_PRE_EMPHASIS, phy_config->pre_emphasis);
	hdmi_writeb(hdmi, HDMI_PHY_DRIVER, phy_config->voltage_level_control);
	hdmi_writeb(hdmi, HDMI_PHY_SYS_CTL, 0x15);
	hdmi_writeb(hdmi, HDMI_PHY_SYS_CTL, 0x14);
	hdmi_writeb(hdmi, HDMI_PHY_SYS_CTL, 0x10);
	hdmi_writeb(hdmi, HDMI_PHY_CHG_PWR, 0x0f);
	hdmi_writeb(hdmi, HDMI_PHY_SYNC, 0x00);
	hdmi_writeb(hdmi, HDMI_PHY_SYNC, 0x01);

	inno_hdmi_sys_power(hdmi, true);
};

static void inno_hdmi_init_hw(struct inno_hdmi *hdmi)
{
	u32 val;
	u32 msk;

	hdmi_modb(hdmi, HDMI_SYS_CTRL, m_RST_DIGITAL, v_NOT_RST_DIGITAL);
	usleep_range(100, 150);

	hdmi_modb(hdmi, HDMI_SYS_CTRL, m_RST_ANALOG, v_NOT_RST_ANALOG);
	usleep_range(100, 150);

	msk = m_REG_CLK_INV | m_REG_CLK_SOURCE | m_POWER | m_INT_POL;
	val = v_REG_CLK_INV | v_REG_CLK_SOURCE_SYS | v_PWR_ON | v_INT_POL_HIGH;
	hdmi_modb(hdmi, HDMI_SYS_CTRL, msk, val);

	inno_hdmi_standby(hdmi);

	/*
	 * When the controller isn't configured to an accurate
	 * video timing and there is no reference clock available,
	 * then the TMDS clock source would be switched to PCLK_HDMI,
	 * so we need to init the TMDS rate to PCLK rate, and
	 * reconfigure the DDC clock.
	 */
	if (hdmi->refclk)
		inno_hdmi_i2c_init(hdmi, clk_get_rate(hdmi->refclk));
	else
		inno_hdmi_i2c_init(hdmi, clk_get_rate(hdmi->pclk));

	/* Unmute hotplug interrupt */
	hdmi_modb(hdmi, HDMI_STATUS, m_MASK_INT_HOTPLUG, v_MASK_INT_HOTPLUG(1));
}

static int inno_hdmi_bridge_clear_avi_infoframe(struct drm_bridge *bridge)
{
	struct inno_hdmi *hdmi = bridge_to_inno_hdmi(bridge);

	hdmi_writeb(hdmi, HDMI_CONTROL_PACKET_BUF_INDEX, INFOFRAME_AVI);

	return 0;
}

static int inno_hdmi_bridge_write_avi_infoframe(struct drm_bridge *bridge,
						const u8 *buffer, size_t len)
{
	struct inno_hdmi *hdmi = bridge_to_inno_hdmi(bridge);
	ssize_t i;

	inno_hdmi_bridge_clear_avi_infoframe(bridge);

	for (i = 0; i < len; i++)
		hdmi_writeb(hdmi, HDMI_CONTROL_PACKET_ADDR + i, buffer[i]);

	return 0;
}

static int inno_hdmi_bridge_clear_hdmi_infoframe(struct drm_bridge *bridge)
{
	drm_warn_once(bridge->encoder->dev, "HDMI VSI not implemented\n");

	return 0;
}

static int inno_hdmi_bridge_write_hdmi_infoframe(struct drm_bridge *bridge,
						 const u8 *buffer, size_t len)
{
	drm_warn_once(bridge->encoder->dev, "HDMI VSI not implemented\n");

	return 0;
}

static int inno_hdmi_config_video_csc(struct inno_hdmi *hdmi,
				      struct drm_connector *connector,
				      struct drm_display_mode *mode)
{
	struct drm_connector_state *conn_state = connector->state;
	int c0_c2_change = 0;
	int csc_enable = 0;
	int csc_mode = 0;
	int auto_csc = 0;
	int value;
	int i;
	int colorimetry;
	u8 vic = drm_match_cea_mode(mode);

	if (vic == 6 || vic == 7 || vic == 21 || vic == 22 ||
	    vic == 2 || vic == 3 || vic == 17 || vic == 18)
		colorimetry = HDMI_COLORIMETRY_ITU_601;
	else
		colorimetry = HDMI_COLORIMETRY_ITU_709;


	/* Input video mode is SDR RGB24bit, data enable signal from external */
	hdmi_writeb(hdmi, HDMI_VIDEO_CONTRL1, v_DE_EXTERNAL |
		    v_VIDEO_INPUT_FORMAT(VIDEO_INPUT_SDR_RGB444));

	/* Input color hardcode to RGB, and output color hardcode to RGB888 */
	value = v_VIDEO_INPUT_BITS(VIDEO_INPUT_8BITS) |
		v_VIDEO_OUTPUT_COLOR(0) |
		v_VIDEO_INPUT_CSP(0);
	hdmi_writeb(hdmi, HDMI_VIDEO_CONTRL2, value);

	if (conn_state->hdmi.output_format == HDMI_COLORSPACE_RGB) {
		if (conn_state->hdmi.is_limited_range) {
			csc_mode = CSC_RGB_0_255_TO_RGB_16_235_8BIT;
			auto_csc = AUTO_CSC_DISABLE;
			c0_c2_change = C0_C2_CHANGE_DISABLE;
			csc_enable = v_CSC_ENABLE;

		} else {
			value = v_SOF_DISABLE | v_COLOR_DEPTH_NOT_INDICATED(1);
			hdmi_writeb(hdmi, HDMI_VIDEO_CONTRL3, value);

			hdmi_modb(hdmi, HDMI_VIDEO_CONTRL,
				  m_VIDEO_AUTO_CSC | m_VIDEO_C0_C2_SWAP,
				  v_VIDEO_AUTO_CSC(AUTO_CSC_DISABLE) |
				  v_VIDEO_C0_C2_SWAP(C0_C2_CHANGE_DISABLE));
			return 0;
		}
	} else {
		if (colorimetry == HDMI_COLORIMETRY_ITU_601) {
			if (conn_state->hdmi.output_format == HDMI_COLORSPACE_YUV444) {
				csc_mode = CSC_RGB_0_255_TO_ITU601_16_235_8BIT;
				auto_csc = AUTO_CSC_DISABLE;
				c0_c2_change = C0_C2_CHANGE_DISABLE;
				csc_enable = v_CSC_ENABLE;
			}
		} else {
			if (conn_state->hdmi.output_format == HDMI_COLORSPACE_YUV444) {
				csc_mode = CSC_RGB_0_255_TO_ITU709_16_235_8BIT;
				auto_csc = AUTO_CSC_DISABLE;
				c0_c2_change = C0_C2_CHANGE_DISABLE;
				csc_enable = v_CSC_ENABLE;
			}
		}
	}

	for (i = 0; i < 24; i++)
		hdmi_writeb(hdmi, HDMI_VIDEO_CSC_COEF + i, coeff_csc[csc_mode][i]);

	value = v_SOF_DISABLE | csc_enable | v_COLOR_DEPTH_NOT_INDICATED(1);
	hdmi_writeb(hdmi, HDMI_VIDEO_CONTRL3, value);
	hdmi_modb(hdmi, HDMI_VIDEO_CONTRL, m_VIDEO_AUTO_CSC |
		  m_VIDEO_C0_C2_SWAP, v_VIDEO_AUTO_CSC(auto_csc) |
		  v_VIDEO_C0_C2_SWAP(c0_c2_change));

	return 0;
}

static int inno_hdmi_config_video_timing(struct inno_hdmi *hdmi,
					 struct drm_display_mode *mode)
{
	const struct inno_hdmi_plat_ops *plat_ops = hdmi->plat_data->ops;
	u32 value;

	if (plat_ops && plat_ops->enable)
		plat_ops->enable(hdmi->dev, mode);

	/* Set detail external video timing polarity and interlace mode */
	value = v_EXTERANL_VIDEO(1);
	value |= mode->flags & DRM_MODE_FLAG_PHSYNC ?
		 v_HSYNC_POLARITY(1) : v_HSYNC_POLARITY(0);
	value |= mode->flags & DRM_MODE_FLAG_PVSYNC ?
		 v_VSYNC_POLARITY(1) : v_VSYNC_POLARITY(0);
	value |= mode->flags & DRM_MODE_FLAG_INTERLACE ?
		 v_INETLACE(1) : v_INETLACE(0);
	hdmi_writeb(hdmi, HDMI_VIDEO_TIMING_CTL, value);

	/* Set detail external video timing */
	value = mode->htotal;
	hdmi_writeb(hdmi, HDMI_VIDEO_EXT_HTOTAL_L, value & 0xFF);
	hdmi_writeb(hdmi, HDMI_VIDEO_EXT_HTOTAL_H, (value >> 8) & 0xFF);

	value = mode->htotal - mode->hdisplay;
	hdmi_writeb(hdmi, HDMI_VIDEO_EXT_HBLANK_L, value & 0xFF);
	hdmi_writeb(hdmi, HDMI_VIDEO_EXT_HBLANK_H, (value >> 8) & 0xFF);

	value = mode->htotal - mode->hsync_start;
	hdmi_writeb(hdmi, HDMI_VIDEO_EXT_HDELAY_L, value & 0xFF);
	hdmi_writeb(hdmi, HDMI_VIDEO_EXT_HDELAY_H, (value >> 8) & 0xFF);

	value = mode->hsync_end - mode->hsync_start;
	hdmi_writeb(hdmi, HDMI_VIDEO_EXT_HDURATION_L, value & 0xFF);
	hdmi_writeb(hdmi, HDMI_VIDEO_EXT_HDURATION_H, (value >> 8) & 0xFF);

	value = mode->vtotal;
	hdmi_writeb(hdmi, HDMI_VIDEO_EXT_VTOTAL_L, value & 0xFF);
	hdmi_writeb(hdmi, HDMI_VIDEO_EXT_VTOTAL_H, (value >> 8) & 0xFF);

	value = mode->vtotal - mode->vdisplay;
	hdmi_writeb(hdmi, HDMI_VIDEO_EXT_VBLANK, value & 0xFF);

	value = mode->vtotal - mode->vsync_start;
	hdmi_writeb(hdmi, HDMI_VIDEO_EXT_VDELAY, value & 0xFF);

	value = mode->vsync_end - mode->vsync_start;
	hdmi_writeb(hdmi, HDMI_VIDEO_EXT_VDURATION, value & 0xFF);

	hdmi_writeb(hdmi, HDMI_PHY_PRE_DIV_RATIO, 0x1e);
	hdmi_writeb(hdmi, HDMI_PHY_FEEDBACK_DIV_RATIO_LOW, 0x2c);
	hdmi_writeb(hdmi, HDMI_PHY_FEEDBACK_DIV_RATIO_HIGH, 0x01);

	return 0;
}

static int inno_hdmi_setup(struct inno_hdmi *hdmi, struct drm_atomic_state *state)
{
	struct drm_bridge *bridge = &hdmi->bridge;
	struct drm_connector *connector;
	struct drm_display_info *info;
	struct drm_connector_state *new_conn_state;
	struct drm_crtc_state *new_crtc_state;

	connector = drm_atomic_get_new_connector_for_encoder(state, bridge->encoder);

	new_conn_state = drm_atomic_get_new_connector_state(state, connector);
	if (WARN_ON(!new_conn_state))
		return -EINVAL;

	new_crtc_state = drm_atomic_get_new_crtc_state(state, new_conn_state->crtc);
	if (WARN_ON(!new_crtc_state))
		return -EINVAL;

	info = &connector->display_info;

	/* Mute video and audio output */
	hdmi_modb(hdmi, HDMI_AV_MUTE, m_AUDIO_MUTE | m_VIDEO_BLACK,
		  v_AUDIO_MUTE(1) | v_VIDEO_MUTE(1));

	/* Set HDMI Mode */
	hdmi_writeb(hdmi, HDMI_HDCP_CTRL, v_HDMI_DVI(info->is_hdmi));

	inno_hdmi_config_video_timing(hdmi, &new_crtc_state->adjusted_mode);

	inno_hdmi_config_video_csc(hdmi, connector, &new_crtc_state->adjusted_mode);

	drm_atomic_helper_connector_hdmi_update_infoframes(connector, state);

	/*
	 * When IP controller have configured to an accurate video
	 * timing, then the TMDS clock source would be switched to
	 * DCLK_LCDC, so we need to init the TMDS rate to mode pixel
	 * clock rate, and reconfigure the DDC clock.
	 */
	inno_hdmi_i2c_init(hdmi, new_conn_state->hdmi.tmds_char_rate);

	/* Unmute video and audio output */
	hdmi_modb(hdmi, HDMI_AV_MUTE, m_AUDIO_MUTE | m_VIDEO_BLACK,
		  v_AUDIO_MUTE(0) | v_VIDEO_MUTE(0));

	inno_hdmi_power_up(hdmi, new_conn_state->hdmi.tmds_char_rate);

	return 0;
}

static enum drm_mode_status inno_hdmi_bridge_mode_valid(struct drm_bridge *bridge,
							const struct drm_display_info *info,
							const struct drm_display_mode *mode)
{
	struct inno_hdmi *hdmi = bridge_to_inno_hdmi(bridge);
	unsigned long mpixelclk, max_tolerance;
	long rounded_refclk;

	/* No support for double-clock modes */
	if (mode->flags & DRM_MODE_FLAG_DBLCLK)
		return MODE_BAD;

	mpixelclk = mode->clock * 1000;

	if (mpixelclk < INNO_HDMI_MIN_TMDS_CLOCK)
		return MODE_CLOCK_LOW;

	if (inno_hdmi_find_phy_config(hdmi, mpixelclk) < 0)
		return MODE_CLOCK_HIGH;

	if (hdmi->refclk) {
		rounded_refclk = clk_round_rate(hdmi->refclk, mpixelclk);
		if (rounded_refclk < 0)
			return MODE_BAD;

		/* Vesa DMT standard mentions +/- 0.5% max tolerance */
		max_tolerance = mpixelclk / 200;
		if (abs_diff((unsigned long)rounded_refclk, mpixelclk) > max_tolerance)
			return MODE_NOCLOCK;
	}

	return MODE_OK;
}

static enum drm_connector_status
inno_hdmi_bridge_detect(struct drm_bridge *bridge, struct drm_connector *connector)
{
	struct inno_hdmi *hdmi = bridge_to_inno_hdmi(bridge);

	return (hdmi_readb(hdmi, HDMI_STATUS) & m_HOTPLUG) ?
		connector_status_connected : connector_status_disconnected;
}

static const struct drm_edid *
inno_hdmi_bridge_edid_read(struct drm_bridge *bridge, struct drm_connector *connector)
{
	struct inno_hdmi *hdmi = bridge_to_inno_hdmi(bridge);
	const struct drm_edid *drm_edid;

	drm_edid = drm_edid_read_ddc(connector, bridge->ddc);
	if (!drm_edid)
		dev_dbg(hdmi->dev, "failed to get edid\n");

	return drm_edid;
}

static void inno_hdmi_bridge_atomic_enable(struct drm_bridge *bridge,
					   struct drm_atomic_state *state)
{
	struct inno_hdmi *hdmi = bridge_to_inno_hdmi(bridge);

	inno_hdmi_setup(hdmi, state);
}

static void inno_hdmi_bridge_atomic_disable(struct drm_bridge *bridge,
					    struct drm_atomic_state *state)
{
	struct inno_hdmi *hdmi = bridge_to_inno_hdmi(bridge);

	inno_hdmi_standby(hdmi);
}

static const struct drm_bridge_funcs inno_hdmi_bridge_funcs = {
	.atomic_duplicate_state = drm_atomic_helper_bridge_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_bridge_destroy_state,
	.atomic_reset = drm_atomic_helper_bridge_reset,
	.atomic_enable = inno_hdmi_bridge_atomic_enable,
	.atomic_disable = inno_hdmi_bridge_atomic_disable,
	.detect = inno_hdmi_bridge_detect,
	.edid_read = inno_hdmi_bridge_edid_read,
	.hdmi_clear_avi_infoframe = inno_hdmi_bridge_clear_avi_infoframe,
	.hdmi_write_avi_infoframe = inno_hdmi_bridge_write_avi_infoframe,
	.hdmi_clear_hdmi_infoframe = inno_hdmi_bridge_clear_hdmi_infoframe,
	.hdmi_write_hdmi_infoframe = inno_hdmi_bridge_write_hdmi_infoframe,
	.mode_valid = inno_hdmi_bridge_mode_valid,
};

static irqreturn_t inno_hdmi_i2c_irq(struct inno_hdmi *hdmi)
{
	struct inno_hdmi_i2c *i2c = hdmi->i2c;
	u8 stat;

	stat = hdmi_readb(hdmi, HDMI_INTERRUPT_STATUS1);
	if (!(stat & m_INT_EDID_READY))
		return IRQ_NONE;

	/* Clear HDMI EDID interrupt flag */
	hdmi_writeb(hdmi, HDMI_INTERRUPT_STATUS1, m_INT_EDID_READY);

	complete(&i2c->cmp);

	return IRQ_HANDLED;
}

static irqreturn_t inno_hdmi_hardirq(int irq, void *dev_id)
{
	struct inno_hdmi *hdmi = dev_id;
	irqreturn_t ret = IRQ_NONE;
	u8 interrupt;

	if (hdmi->i2c)
		ret = inno_hdmi_i2c_irq(hdmi);

	interrupt = hdmi_readb(hdmi, HDMI_STATUS);
	if (interrupt & m_INT_HOTPLUG) {
		hdmi_modb(hdmi, HDMI_STATUS, m_INT_HOTPLUG, m_INT_HOTPLUG);
		ret = IRQ_WAKE_THREAD;
	}

	return ret;
}

static irqreturn_t inno_hdmi_irq(int irq, void *dev_id)
{
	struct inno_hdmi *hdmi = dev_id;

	drm_helper_hpd_irq_event(hdmi->bridge.dev);

	return IRQ_HANDLED;
}

static int inno_hdmi_i2c_read(struct inno_hdmi *hdmi, struct i2c_msg *msgs)
{
	int length = msgs->len;
	u8 *buf = msgs->buf;
	int ret;

	ret = wait_for_completion_timeout(&hdmi->i2c->cmp, HZ / 10);
	if (!ret)
		return -EAGAIN;

	while (length--)
		*buf++ = hdmi_readb(hdmi, HDMI_EDID_FIFO_ADDR);

	return 0;
}

static int inno_hdmi_i2c_write(struct inno_hdmi *hdmi, struct i2c_msg *msgs)
{
	/*
	 * The DDC module only support read EDID message, so
	 * we assume that each word write to this i2c adapter
	 * should be the offset of EDID word address.
	 */
	if (msgs->len != 1 || (msgs->addr != DDC_ADDR && msgs->addr != DDC_SEGMENT_ADDR))
		return -EINVAL;

	reinit_completion(&hdmi->i2c->cmp);

	if (msgs->addr == DDC_SEGMENT_ADDR)
		hdmi->i2c->segment_addr = msgs->buf[0];
	if (msgs->addr == DDC_ADDR)
		hdmi->i2c->ddc_addr = msgs->buf[0];

	/* Set edid fifo first addr */
	hdmi_writeb(hdmi, HDMI_EDID_FIFO_OFFSET, 0x00);

	/* Set edid word address 0x00/0x80 */
	hdmi_writeb(hdmi, HDMI_EDID_WORD_ADDR, hdmi->i2c->ddc_addr);

	/* Set edid segment pointer */
	hdmi_writeb(hdmi, HDMI_EDID_SEGMENT_POINTER, hdmi->i2c->segment_addr);

	return 0;
}

static int inno_hdmi_i2c_xfer(struct i2c_adapter *adap,
			      struct i2c_msg *msgs, int num)
{
	struct inno_hdmi *hdmi = i2c_get_adapdata(adap);
	struct inno_hdmi_i2c *i2c = hdmi->i2c;
	int i, ret = 0;

	mutex_lock(&i2c->lock);

	/* Clear the EDID interrupt flag and unmute the interrupt */
	hdmi_writeb(hdmi, HDMI_INTERRUPT_MASK1, m_INT_EDID_READY);
	hdmi_writeb(hdmi, HDMI_INTERRUPT_STATUS1, m_INT_EDID_READY);

	for (i = 0; i < num; i++) {
		DRM_DEV_DEBUG(hdmi->dev,
			      "xfer: num: %d/%d, len: %d, flags: %#x\n",
			      i + 1, num, msgs[i].len, msgs[i].flags);

		if (msgs[i].flags & I2C_M_RD)
			ret = inno_hdmi_i2c_read(hdmi, &msgs[i]);
		else
			ret = inno_hdmi_i2c_write(hdmi, &msgs[i]);

		if (ret < 0)
			break;
	}

	if (!ret)
		ret = num;

	/* Mute HDMI EDID interrupt */
	hdmi_writeb(hdmi, HDMI_INTERRUPT_MASK1, 0);

	mutex_unlock(&i2c->lock);

	return ret;
}

static u32 inno_hdmi_i2c_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static const struct i2c_algorithm inno_hdmi_algorithm = {
	.master_xfer	= inno_hdmi_i2c_xfer,
	.functionality	= inno_hdmi_i2c_func,
};

static struct i2c_adapter *inno_hdmi_i2c_adapter(struct inno_hdmi *hdmi)
{
	struct i2c_adapter *adap;
	struct inno_hdmi_i2c *i2c;
	int ret;

	i2c = devm_kzalloc(hdmi->dev, sizeof(*i2c), GFP_KERNEL);
	if (!i2c)
		return ERR_PTR(-ENOMEM);

	mutex_init(&i2c->lock);
	init_completion(&i2c->cmp);

	adap = &i2c->adap;
	adap->owner = THIS_MODULE;
	adap->dev.parent = hdmi->dev;
	adap->dev.of_node = hdmi->dev->of_node;
	adap->algo = &inno_hdmi_algorithm;
	strscpy(adap->name, "Inno HDMI", sizeof(adap->name));
	i2c_set_adapdata(adap, hdmi);

	ret = devm_i2c_add_adapter(hdmi->dev, adap);
	if (ret) {
		dev_warn(hdmi->dev, "cannot add %s I2C adapter\n", adap->name);
		return ERR_PTR(ret);
	}

	hdmi->i2c = i2c;

	DRM_DEV_INFO(hdmi->dev, "registered %s I2C bus driver\n", adap->name);

	return adap;
}

struct inno_hdmi *inno_hdmi_bind(struct device *dev,
				 struct drm_encoder *encoder,
				 const struct inno_hdmi_plat_data *plat_data)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct inno_hdmi *hdmi;
	int irq;
	int ret;

	if (!plat_data->phy_configs || !plat_data->default_phy_config) {
		dev_err(dev, "Missing platform PHY ops\n");
		return ERR_PTR(-ENODEV);
	}

	hdmi = devm_drm_bridge_alloc(dev, struct inno_hdmi, bridge, &inno_hdmi_bridge_funcs);
	if (IS_ERR(hdmi))
		return ERR_CAST(hdmi);

	hdmi->dev = dev;
	hdmi->plat_data = plat_data;

	hdmi->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(hdmi->regs))
		return ERR_CAST(hdmi->regs);

	hdmi->pclk = devm_clk_get_enabled(hdmi->dev, "pclk");
	if (IS_ERR(hdmi->pclk)) {
		dev_err_probe(dev, PTR_ERR(hdmi->pclk), "Unable to get HDMI pclk\n");
		return ERR_CAST(hdmi->pclk);
	}

	hdmi->refclk = devm_clk_get_optional_enabled(hdmi->dev, "ref");
	if (IS_ERR(hdmi->refclk)) {
		dev_err_probe(dev, PTR_ERR(hdmi->refclk), "Unable to get HDMI refclk\n");
		return ERR_CAST(hdmi->refclk);
	}

	inno_hdmi_init_hw(hdmi);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return ERR_PTR(irq);

	ret = devm_request_threaded_irq(dev, irq, inno_hdmi_hardirq,
					inno_hdmi_irq, IRQF_SHARED,
					dev_name(dev), hdmi);
	if (ret)
		return ERR_PTR(ret);

	hdmi->bridge.driver_private = hdmi;
	hdmi->bridge.ops = DRM_BRIDGE_OP_DETECT |
			   DRM_BRIDGE_OP_EDID |
			   DRM_BRIDGE_OP_HDMI |
			   DRM_BRIDGE_OP_HPD;
	hdmi->bridge.of_node = pdev->dev.of_node;
	hdmi->bridge.type = DRM_MODE_CONNECTOR_HDMIA;
	hdmi->bridge.vendor = "Inno";
	hdmi->bridge.product = "Inno HDMI";

	hdmi->bridge.ddc = inno_hdmi_i2c_adapter(hdmi);
	if (IS_ERR(hdmi->bridge.ddc))
		return ERR_CAST(hdmi->bridge.ddc);

	ret = devm_drm_bridge_add(dev, &hdmi->bridge);
	if (ret)
		return ERR_PTR(ret);

	ret = drm_bridge_attach(encoder, &hdmi->bridge, NULL, DRM_BRIDGE_ATTACH_NO_CONNECTOR);
	if (ret)
		return ERR_PTR(ret);

	return hdmi;
}
EXPORT_SYMBOL_GPL(inno_hdmi_bind);
MODULE_AUTHOR("Andy Yan <andyshrk@163.com>");
MODULE_DESCRIPTION("INNOSILICON HDMI transmitter library");
MODULE_LICENSE("GPL");
