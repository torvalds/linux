// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018 Rockchip Electronics Co. Ltd.
 *
 * Author: Chen Shunqing <csq@rock-chips.com>
 */

#include <linux/irq.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/hdmi.h>
#include <linux/mfd/rk618.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of_device.h>
#include <linux/regmap.h>
#ifdef CONFIG_SWITCH
#include <linux/switch.h>
#endif

#include <drm/drm_of.h>
#include <drm/drmP.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_edid.h>

#include <sound/hdmi-codec.h>

#include "../rockchip_drm_drv.h"

#define RK618_HDMI_BASE			0x0400

#define DDC_SEGMENT_ADDR		0x30

enum PWR_MODE {
	NORMAL,
	LOWER_PWR,
};

#define HDMI_SCL_RATE			(100 * 1000)
#define DDC_BUS_FREQ_L			0x4b
#define DDC_BUS_FREQ_H			0x4c

#define HDMI_SYS_CTRL			0x00
#define m_RST_ANALOG			(1 << 6)
#define v_RST_ANALOG			(0 << 6)
#define v_NOT_RST_ANALOG		(1 << 6)
#define m_RST_DIGITAL			(1 << 5)
#define v_RST_DIGITAL			(0 << 5)
#define v_NOT_RST_DIGITAL		(1 << 5)
#define m_REG_CLK_INV			(1 << 4)
#define v_REG_CLK_NOT_INV		(0 << 4)
#define v_REG_CLK_INV			(1 << 4)
#define m_VCLK_INV			(1 << 3)
#define v_VCLK_NOT_INV			(0 << 3)
#define v_VCLK_INV			(1 << 3)
#define m_REG_CLK_SOURCE		(1 << 2)
#define v_REG_CLK_SOURCE_TMDS		(0 << 2)
#define v_REG_CLK_SOURCE_SYS		(1 << 2)
#define m_POWER				(1 << 1)
#define v_PWR_ON			(0 << 1)
#define v_PWR_OFF			(1 << 1)
#define m_INT_POL			(1 << 0)
#define v_INT_POL_HIGH			1
#define v_INT_POL_LOW			0

#define HDMI_VIDEO_CONTROL1		0x01
#define m_VIDEO_INPUT_FORMAT		(7 << 1)
#define m_DE_SOURCE			(1 << 0)
#define v_VIDEO_INPUT_FORMAT(n)		((n) << 1)
#define v_DE_EXTERNAL			1
#define v_DE_INTERNAL			0
enum {
	VIDEO_INPUT_SDR_RGB444 = 0,
	VIDEO_INPUT_DDR_RGB444 = 5,
	VIDEO_INPUT_DDR_YCBCR422 = 6
};

#define HDMI_VIDEO_CONTROL2		0x02
#define m_VIDEO_OUTPUT_COLOR		(3 << 6)
#define m_VIDEO_INPUT_BITS		(3 << 4)
#define m_VIDEO_INPUT_CSP		(1 << 0)
#define v_VIDEO_OUTPUT_COLOR(n)		(((n) & 0x3) << 6)
#define v_VIDEO_INPUT_BITS(n)		((n) << 4)
#define v_VIDEO_INPUT_CSP(n)		((n) << 0)
enum {
	VIDEO_INPUT_12BITS = 0,
	VIDEO_INPUT_10BITS = 1,
	VIDEO_INPUT_REVERT = 2,
	VIDEO_INPUT_8BITS = 3,
};

#define HDMI_VIDEO_CONTROL		0x03
#define m_VIDEO_AUTO_CSC		(1 << 7)
#define v_VIDEO_AUTO_CSC(n)		((n) << 7)
#define m_VIDEO_C0_C2_SWAP		(1 << 0)
#define v_VIDEO_C0_C2_SWAP(n)		((n) << 0)
enum {
	C0_C2_CHANGE_ENABLE = 0,
	C0_C2_CHANGE_DISABLE = 1,
	AUTO_CSC_DISABLE = 0,
	AUTO_CSC_ENABLE = 1,
};

#define HDMI_VIDEO_CONTROL3		0x04
#define m_COLOR_DEPTH_NOT_INDICATED	(1 << 4)
#define m_SOF				(1 << 3)
#define m_COLOR_RANGE			(1 << 2)
#define m_CSC				(1 << 0)
#define v_COLOR_DEPTH_NOT_INDICATED(n)	((n) << 4)
#define v_SOF_ENABLE			(0 << 3)
#define v_SOF_DISABLE			(1 << 3)
#define v_COLOR_RANGE_FULL		(1 << 2)
#define v_COLOR_RANGE_LIMITED		(0 << 2)
#define v_CSC_ENABLE			1
#define v_CSC_DISABLE			0

#define HDMI_AV_MUTE			0x05
#define m_AVMUTE_CLEAR			(1 << 7)
#define m_AVMUTE_ENABLE			(1 << 6)
#define m_AUDIO_PD			(1 << 2)
#define m_AUDIO_MUTE			(1 << 1)
#define m_VIDEO_BLACK			(1 << 0)
#define v_AVMUTE_CLEAR(n)		((n) << 7)
#define v_AVMUTE_ENABLE(n)		((n) << 6)
#define v_AUDIO_MUTE(n)			((n) << 1)
#define v_AUDIO_PD(n)			((n) << 2)
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
#define m_AUDIO_STATUS_NLPCM		(1 << 7)
#define m_AUDIO_STATUS_USE		(1 << 6)
#define m_AUDIO_STATUS_COPYRIGHT	(1 << 5)
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
#define m_PACKET_GCP_EN			(1 << 7)
#define m_PACKET_MSI_EN			(1 << 6)
#define m_PACKET_SDI_EN			(1 << 5)
#define m_PACKET_VSI_EN			(1 << 4)
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
#define m_HDMI_DVI			(1 << 1)
#define v_HDMI_DVI(n)			((n) << 1)

#define HDMI_INTERRUPT_MASK1		0xc0
#define HDMI_INTERRUPT_STATUS1		0xc1
#define m_INT_HOTPLUG_RK618		BIT(7)
#define	m_INT_ACTIVE_VSYNC		(1 << 5)
#define m_INT_EDID_READY		(1 << 2)

#define HDMI_INTERRUPT_MASK2		0xc2
#define HDMI_INTERRUPT_STATUS2		0xc3
#define m_INT_HDCP_ERR			(1 << 7)
#define m_INT_BKSV_FLAG			(1 << 6)
#define m_INT_HDCP_OK			(1 << 4)

#define HDMI_STATUS			0xc8
#define m_HOTPLUG			(1 << 7)
#define m_MASK_INT_HOTPLUG		(1 << 5)
#define m_INT_HOTPLUG			(1 << 1)
#define v_MASK_INT_HOTPLUG(n)		(((n) & 0x1) << 5)

#define HDMI_COLORBAR                   0xc9

#define HDMI_PHY_SYNC			0xce
#define HDMI_PHY_SYS_CTL		0xe0
#define m_TMDS_CLK_SOURCE		(1 << 5)
#define v_TMDS_FROM_PLL			(0 << 5)
#define v_TMDS_FROM_GEN			(1 << 5)
#define m_PHASE_CLK			(1 << 4)
#define v_DEFAULT_PHASE			(0 << 4)
#define v_SYNC_PHASE			(1 << 4)
#define m_TMDS_CURRENT_PWR		(1 << 3)
#define v_TURN_ON_CURRENT		(0 << 3)
#define v_CAT_OFF_CURRENT		(1 << 3)
#define m_BANDGAP_PWR			(1 << 2)
#define v_BANDGAP_PWR_UP		(0 << 2)
#define v_BANDGAP_PWR_DOWN		(1 << 2)
#define m_PLL_PWR			(1 << 1)
#define v_PLL_PWR_UP			(0 << 1)
#define v_PLL_PWR_DOWN			(1 << 1)
#define m_TMDS_CHG_PWR			(1 << 0)
#define v_TMDS_CHG_PWR_UP		(0 << 0)
#define v_TMDS_CHG_PWR_DOWN		(1 << 0)

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
#define m_ADJUST_FOR_HISENSE		(1 << 6)
#define m_REJECT_RX_BROADCAST		(1 << 5)
#define m_BUSFREETIME_ENABLE		(1 << 2)
#define m_REJECT_RX			(1 << 1)
#define m_START_TX			(1 << 0)

#define HDMI_CEC_DATA			0xd1
#define HDMI_CEC_TX_OFFSET		0xd2
#define HDMI_CEC_RX_OFFSET		0xd3
#define HDMI_CEC_CLK_H			0xd4
#define HDMI_CEC_CLK_L			0xd5
#define HDMI_CEC_TX_LENGTH		0xd6
#define HDMI_CEC_RX_LENGTH		0xd7
#define HDMI_CEC_TX_INT_MASK		0xd8
#define m_TX_DONE			(1 << 3)
#define m_TX_NOACK			(1 << 2)
#define m_TX_BROADCAST_REJ		(1 << 1)
#define m_TX_BUSNOTFREE			(1 << 0)

#define HDMI_CEC_RX_INT_MASK		0xd9
#define m_RX_LA_ERR			(1 << 4)
#define m_RX_GLITCH			(1 << 3)
#define m_RX_DONE			(1 << 0)

#define HDMI_CEC_TX_INT			0xda
#define HDMI_CEC_RX_INT			0xdb
#define HDMI_CEC_BUSFREETIME_L		0xdc
#define HDMI_CEC_BUSFREETIME_H		0xdd
#define HDMI_CEC_LOGICADDR		0xde

struct audio_info {
	int sample_rate;
	int channels;
	int sample_width;
};

struct hdmi_data_info {
	int vic;
	bool sink_is_hdmi;
	bool sink_has_audio;
	unsigned int enc_in_format;
	unsigned int enc_out_format;
	unsigned int colorimetry;
};

struct rk618_hdmi_i2c {
	struct i2c_adapter adap;

	u8 ddc_addr;
	u8 segment_addr;

	struct mutex lock;
};

struct rk618_hdmi_phy_config {
	unsigned long mpixelclock;
	u8 pre_emphasis;	/* pre-emphasis value */
	u8 vlev_ctr;		/* voltage level control */
};

struct rk618_hdmi {
	struct device *dev;
	int irq;
	struct regmap *regmap;
	struct rk618 *parent;
	struct clk *clock;

	struct drm_bridge base;
	struct drm_connector connector;
	struct drm_bridge *bridge;

	struct rk618_hdmi_i2c *i2c;
	struct i2c_adapter *ddc;

	unsigned int tmds_rate;

	struct platform_device *audio_pdev;
	bool audio_enable;

	struct hdmi_data_info	hdmi_data;
	struct drm_display_mode previous_mode;
#ifdef CONFIG_SWITCH
	struct switch_dev switchdev;
#endif
	struct rockchip_drm_sub_dev sub_dev;
};

enum {
	CSC_ITU601_16_235_TO_RGB_0_255_8BIT,
	CSC_ITU601_0_255_TO_RGB_0_255_8BIT,
	CSC_ITU709_16_235_TO_RGB_0_255_8BIT,
	CSC_RGB_0_255_TO_ITU601_16_235_8BIT,
	CSC_RGB_0_255_TO_ITU709_16_235_8BIT,
	CSC_RGB_0_255_TO_RGB_16_235_8BIT,
};

static const char coeff_csc[][24] = {
	/*
	 * YUV2RGB:601 SD mode(Y[16:235], UV[16:240], RGB[0:255]):
	 *   R = 1.164*Y + 1.596*V - 204
	 *   G = 1.164*Y - 0.391*U - 0.813*V + 154
	 *   B = 1.164*Y + 2.018*U - 258
	 */
	{
		0x04, 0xa7, 0x00, 0x00, 0x06, 0x62, 0x02, 0xcc,
		0x04, 0xa7, 0x11, 0x90, 0x13, 0x40, 0x00, 0x9a,
		0x04, 0xa7, 0x08, 0x12, 0x00, 0x00, 0x03, 0x02
	},
	/*
	 * YUV2RGB:601 SD mode(YUV[0:255],RGB[0:255]):
	 *   R = Y + 1.402*V - 248
	 *   G = Y - 0.344*U - 0.714*V + 135
	 *   B = Y + 1.772*U - 227
	 */
	{
		0x04, 0x00, 0x00, 0x00, 0x05, 0x9b, 0x02, 0xf8,
		0x04, 0x00, 0x11, 0x60, 0x12, 0xdb, 0x00, 0x87,
		0x04, 0x00, 0x07, 0x16, 0x00, 0x00, 0x02, 0xe3
	},
	/*
	 * YUV2RGB:709 HD mode(Y[16:235],UV[16:240],RGB[0:255]):
	 *   R = 1.164*Y + 1.793*V - 248
	 *   G = 1.164*Y - 0.213*U - 0.534*V + 77
	 *   B = 1.164*Y + 2.115*U - 289
	 */
	{
		0x04, 0xa7, 0x00, 0x00, 0x07, 0x2c, 0x02, 0xf8,
		0x04, 0xa7, 0x10, 0xda, 0x12, 0x22, 0x00, 0x4d,
		0x04, 0xa7, 0x08, 0x74, 0x00, 0x00, 0x03, 0x21
	},

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

static inline struct rk618_hdmi *bridge_to_hdmi(struct drm_bridge *b)
{
	return container_of(b, struct rk618_hdmi, base);
}

static inline struct rk618_hdmi *connector_to_hdmi(struct drm_connector *c)
{
	return container_of(c, struct rk618_hdmi, connector);
}

static inline u8 hdmi_readb(struct rk618_hdmi *hdmi, u16 offset)
{
	u32 val;

	regmap_read(hdmi->regmap, (RK618_HDMI_BASE + ((offset) << 2)), &val);

	return val;
}

static inline void hdmi_writeb(struct rk618_hdmi *hdmi, u16 offset, u32 val)
{
	regmap_write(hdmi->regmap, (RK618_HDMI_BASE + ((offset) << 2)), val);
}

static void rk618_hdmi_set_polarity(struct rk618_hdmi *hdmi, int vic)
{
	u32 val, mask = HDMI_HSYNC_POL_INV | HDMI_VSYNC_POL_INV;

	if (vic == 76 || vic == 75 || vic == 5 || vic == 20 ||
	    vic == 39 || vic == 16 || vic == 4)
		val = HDMI_HSYNC_POL_INV | HDMI_VSYNC_POL_INV;
	else
		val = 0;

	regmap_update_bits(hdmi->parent->regmap, RK618_MISC_CON, mask, val);
}

static void rk618_hdmi_pol_init(struct rk618_hdmi *hdmi, int pol)
{
	u32 val;

	if (pol)
		val = 0x0;
	else
		val = 0x20;
	regmap_update_bits(hdmi->parent->regmap, RK618_MISC_CON,
			   INT_ACTIVE_LOW, val);

	regmap_update_bits(hdmi->parent->regmap,
			   RK618_MISC_CON, HDMI_CLK_SEL_MASK,
			   HDMI_CLK_SEL_VIDEO_INF0_CLK);
}

static inline void hdmi_modb(struct rk618_hdmi *hdmi, u16 offset,
			     u32 msk, u32 val)
{
	u8 temp = hdmi_readb(hdmi, offset) & ~msk;

	temp |= val & msk;
	hdmi_writeb(hdmi, offset, temp);
}

static void rk618_hdmi_i2c_init(struct rk618_hdmi *hdmi)
{
	int ddc_bus_freq;

	ddc_bus_freq = (hdmi->tmds_rate >> 2) / HDMI_SCL_RATE;

	hdmi_writeb(hdmi, DDC_BUS_FREQ_L, ddc_bus_freq & 0xFF);
	hdmi_writeb(hdmi, DDC_BUS_FREQ_H, (ddc_bus_freq >> 8) & 0xFF);

	/* Clear the EDID interrupt flag and mute the interrupt */
	hdmi_writeb(hdmi, HDMI_INTERRUPT_MASK1, 0);
	hdmi_writeb(hdmi, HDMI_INTERRUPT_STATUS1, m_INT_EDID_READY);
	hdmi_modb(hdmi, HDMI_INTERRUPT_MASK1, m_INT_HOTPLUG_RK618,
		  m_INT_HOTPLUG_RK618);
}

static void rk618_hdmi_sys_power(struct rk618_hdmi *hdmi, bool enable)
{
	if (enable)
		hdmi_modb(hdmi, HDMI_SYS_CTRL, m_POWER, v_PWR_ON);
	else
		hdmi_modb(hdmi, HDMI_SYS_CTRL, m_POWER, v_PWR_OFF);
}

static struct rk618_hdmi_phy_config rk618_hdmi_phy_config[] = {
	/* pixelclk pre-emp vlev */
	{ 74250000,  0x0f, 0xaa },
	{ 165000000, 0x0f, 0xaa },
	{ ~0UL,	     0x00, 0x00 }
};

static void rk618_hdmi_set_pwr_mode(struct rk618_hdmi *hdmi, int mode)
{
	const struct rk618_hdmi_phy_config *phy_config =
						rk618_hdmi_phy_config;

	switch (mode) {
	case NORMAL:
		rk618_hdmi_sys_power(hdmi, false);
		for (; phy_config->mpixelclock != ~0UL; phy_config++)
			if (hdmi->tmds_rate <= phy_config->mpixelclock)
				break;
		if (!phy_config->mpixelclock)
			return;
		hdmi_writeb(hdmi, HDMI_PHY_PRE_EMPHASIS,
			    phy_config->pre_emphasis);
		hdmi_writeb(hdmi, HDMI_PHY_DRIVER, phy_config->vlev_ctr);

		hdmi_writeb(hdmi, HDMI_PHY_SYS_CTL, 0x2d);
		hdmi_writeb(hdmi, HDMI_PHY_SYS_CTL, 0x2c);
		hdmi_writeb(hdmi, HDMI_PHY_SYS_CTL, 0x28);
		hdmi_writeb(hdmi, HDMI_PHY_SYS_CTL, 0x20);
		hdmi_writeb(hdmi, HDMI_PHY_CHG_PWR, 0x0f);
		hdmi_writeb(hdmi, HDMI_PHY_SYNC, 0x00);
		hdmi_writeb(hdmi, HDMI_PHY_SYNC, 0x01);

		rk618_hdmi_sys_power(hdmi, true);
		break;

	case LOWER_PWR:
		rk618_hdmi_sys_power(hdmi, false);
		hdmi_writeb(hdmi, HDMI_PHY_DRIVER, 0x00);
		hdmi_writeb(hdmi, HDMI_PHY_PRE_EMPHASIS, 0x00);
		hdmi_writeb(hdmi, HDMI_PHY_CHG_PWR, 0x00);
		hdmi_writeb(hdmi, HDMI_PHY_SYS_CTL, 0x2f);
		break;

	default:
		dev_err(hdmi->dev, "Unknown power mode %d\n", mode);
	}
}

static void rk618_hdmi_reset(struct rk618_hdmi *hdmi)
{
	u32 val;
	u32 msk;

	hdmi_modb(hdmi, HDMI_SYS_CTRL, m_RST_DIGITAL, v_NOT_RST_DIGITAL);
	usleep_range(100, 110);

	hdmi_modb(hdmi, HDMI_SYS_CTRL, m_RST_ANALOG, v_NOT_RST_ANALOG);
	usleep_range(100, 110);

	msk = m_REG_CLK_INV | m_REG_CLK_SOURCE | m_POWER | m_INT_POL;
	val = v_REG_CLK_INV | v_REG_CLK_SOURCE_SYS | v_PWR_ON | v_INT_POL_HIGH;
	hdmi_modb(hdmi, HDMI_SYS_CTRL, msk, val);

	rk618_hdmi_set_pwr_mode(hdmi, NORMAL);
}

static int rk618_hdmi_upload_frame(struct rk618_hdmi *hdmi, int setup_rc,
				   union hdmi_infoframe *frame, u32 frame_index,
				   u32 mask, u32 disable, u32 enable)
{
	if (mask)
		hdmi_modb(hdmi, HDMI_PACKET_SEND_AUTO, mask, disable);

	hdmi_writeb(hdmi, HDMI_CONTROL_PACKET_BUF_INDEX, frame_index);

	if (setup_rc >= 0) {
		u8 packed_frame[HDMI_MAXIMUM_INFO_FRAME_SIZE];
		ssize_t rc, i;

		rc = hdmi_infoframe_pack(frame, packed_frame,
					 sizeof(packed_frame));
		if (rc < 0)
			return rc;

		for (i = 0; i < rc; i++)
			hdmi_writeb(hdmi, HDMI_CONTROL_PACKET_ADDR + i,
				    packed_frame[i]);

		if (mask)
			hdmi_modb(hdmi, HDMI_PACKET_SEND_AUTO, mask, enable);
	}

	return setup_rc;
}

static int rk618_hdmi_config_video_vsi(struct rk618_hdmi *hdmi,
				       struct drm_display_mode *mode)
{
	union hdmi_infoframe frame;
	int rc;

	rc = drm_hdmi_vendor_infoframe_from_display_mode(&frame.vendor.hdmi,
							 &hdmi->connector,
							 mode);

	return rk618_hdmi_upload_frame(hdmi, rc, &frame,
				       INFOFRAME_VSI, m_PACKET_VSI_EN,
				       v_PACKET_VSI_EN(0), v_PACKET_VSI_EN(1));
}

static int rk618_hdmi_config_video_avi(struct rk618_hdmi *hdmi,
				       struct drm_display_mode *mode)
{
	union hdmi_infoframe frame;
	int rc;

	rc = drm_hdmi_avi_infoframe_from_display_mode(&frame.avi, mode, false);

	if (hdmi->hdmi_data.enc_out_format == HDMI_COLORSPACE_YUV444)
		frame.avi.colorspace = HDMI_COLORSPACE_YUV444;
	else if (hdmi->hdmi_data.enc_out_format == HDMI_COLORSPACE_YUV422)
		frame.avi.colorspace = HDMI_COLORSPACE_YUV422;
	else
		frame.avi.colorspace = HDMI_COLORSPACE_RGB;

	if (frame.avi.colorspace != HDMI_COLORSPACE_RGB)
		frame.avi.colorimetry = hdmi->hdmi_data.colorimetry;

	frame.avi.scan_mode = HDMI_SCAN_MODE_NONE;

	return rk618_hdmi_upload_frame(hdmi, rc, &frame,
				       INFOFRAME_AVI, 0, 0, 0);
}

static int rk618_hdmi_config_audio_aai(struct rk618_hdmi *hdmi,
				       struct audio_info *audio)
{
	struct hdmi_audio_infoframe *faudio;
	union hdmi_infoframe frame;
	int rc;

	rc = hdmi_audio_infoframe_init(&frame.audio);
	faudio = (struct hdmi_audio_infoframe *)&frame;

	faudio->channels = audio->channels;

	return rk618_hdmi_upload_frame(hdmi, rc, &frame,
				       INFOFRAME_AAI, 0, 0, 0);
}

static int rk618_hdmi_config_video_csc(struct rk618_hdmi *hdmi)
{
	struct hdmi_data_info *data = &hdmi->hdmi_data;
	int c0_c2_change = 0;
	int csc_enable = 0;
	int csc_mode = 0;
	int auto_csc = 0;
	int value;
	int i;

	/* Input video mode is SDR RGB24bit, data enable signal from external */
	hdmi_writeb(hdmi, HDMI_VIDEO_CONTROL1, v_DE_EXTERNAL |
		    v_VIDEO_INPUT_FORMAT(VIDEO_INPUT_SDR_RGB444));

	/* Input color hardcode to RGB, and output color hardcode to RGB888 */
	value = v_VIDEO_INPUT_BITS(VIDEO_INPUT_8BITS) |
		v_VIDEO_OUTPUT_COLOR(0) |
		v_VIDEO_INPUT_CSP(0);
	hdmi_writeb(hdmi, HDMI_VIDEO_CONTROL2, value);

	if (data->enc_in_format == data->enc_out_format) {
		if ((data->enc_in_format == HDMI_COLORSPACE_RGB) ||
		    (data->enc_in_format >= HDMI_COLORSPACE_YUV444)) {
			value = v_SOF_DISABLE | v_COLOR_DEPTH_NOT_INDICATED(1);
			hdmi_writeb(hdmi, HDMI_VIDEO_CONTROL3, value);

			hdmi_modb(hdmi, HDMI_VIDEO_CONTROL,
				  m_VIDEO_AUTO_CSC | m_VIDEO_C0_C2_SWAP,
				  v_VIDEO_AUTO_CSC(AUTO_CSC_DISABLE) |
				  v_VIDEO_C0_C2_SWAP(C0_C2_CHANGE_DISABLE));
			return 0;
		}
	}

	if (data->colorimetry == HDMI_COLORIMETRY_ITU_601) {
		if ((data->enc_in_format == HDMI_COLORSPACE_RGB) &&
		    (data->enc_out_format == HDMI_COLORSPACE_YUV444)) {
			csc_mode = CSC_RGB_0_255_TO_ITU601_16_235_8BIT;
			auto_csc = AUTO_CSC_DISABLE;
			c0_c2_change = C0_C2_CHANGE_DISABLE;
			csc_enable = v_CSC_ENABLE;
		} else if ((data->enc_in_format == HDMI_COLORSPACE_YUV444) &&
			   (data->enc_out_format == HDMI_COLORSPACE_RGB)) {
			csc_mode = CSC_ITU601_16_235_TO_RGB_0_255_8BIT;
			auto_csc = AUTO_CSC_ENABLE;
			c0_c2_change = C0_C2_CHANGE_DISABLE;
			csc_enable = v_CSC_DISABLE;
		}
	} else {
		if ((data->enc_in_format == HDMI_COLORSPACE_RGB) &&
		    (data->enc_out_format == HDMI_COLORSPACE_YUV444)) {
			csc_mode = CSC_RGB_0_255_TO_ITU709_16_235_8BIT;
			auto_csc = AUTO_CSC_DISABLE;
			c0_c2_change = C0_C2_CHANGE_DISABLE;
			csc_enable = v_CSC_ENABLE;
		} else if ((data->enc_in_format == HDMI_COLORSPACE_YUV444) &&
			   (data->enc_out_format == HDMI_COLORSPACE_RGB)) {
			csc_mode = CSC_ITU709_16_235_TO_RGB_0_255_8BIT;
			auto_csc = AUTO_CSC_ENABLE;
			c0_c2_change = C0_C2_CHANGE_DISABLE;
			csc_enable = v_CSC_DISABLE;
		}
	}

	for (i = 0; i < 24; i++)
		hdmi_writeb(hdmi, HDMI_VIDEO_CSC_COEF + i,
			    coeff_csc[csc_mode][i]);

	value = v_SOF_DISABLE | csc_enable | v_COLOR_DEPTH_NOT_INDICATED(1);
	hdmi_writeb(hdmi, HDMI_VIDEO_CONTROL3, value);
	hdmi_modb(hdmi, HDMI_VIDEO_CONTROL, m_VIDEO_AUTO_CSC |
		  m_VIDEO_C0_C2_SWAP, v_VIDEO_AUTO_CSC(auto_csc) |
		  v_VIDEO_C0_C2_SWAP(c0_c2_change));

	return 0;
}

static int rk618_hdmi_config_video_timing(struct rk618_hdmi *hdmi,
					  struct drm_display_mode *mode)
{
	int value;

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

static int rk618_hdmi_setup(struct rk618_hdmi *hdmi,
			    struct drm_display_mode *mode)
{
	hdmi->hdmi_data.vic = drm_match_cea_mode(mode);

	hdmi->hdmi_data.enc_in_format = HDMI_COLORSPACE_RGB;
	hdmi->hdmi_data.enc_out_format = HDMI_COLORSPACE_RGB;

	if ((hdmi->hdmi_data.vic == 6) || (hdmi->hdmi_data.vic == 7) ||
	    (hdmi->hdmi_data.vic == 21) || (hdmi->hdmi_data.vic == 22) ||
	    (hdmi->hdmi_data.vic == 2) || (hdmi->hdmi_data.vic == 3) ||
	    (hdmi->hdmi_data.vic == 17) || (hdmi->hdmi_data.vic == 18))
		hdmi->hdmi_data.colorimetry = HDMI_COLORIMETRY_ITU_601;
	else
		hdmi->hdmi_data.colorimetry = HDMI_COLORIMETRY_ITU_709;

	/* Mute video and audio output */
	hdmi_modb(hdmi, HDMI_AV_MUTE, m_AUDIO_MUTE | m_VIDEO_BLACK,
		  v_AUDIO_MUTE(1) | v_VIDEO_MUTE(1));

	/* Set HDMI Mode */
	hdmi_writeb(hdmi, HDMI_HDCP_CTRL,
		    v_HDMI_DVI(hdmi->hdmi_data.sink_is_hdmi));

	rk618_hdmi_config_video_timing(hdmi, mode);

	rk618_hdmi_config_video_csc(hdmi);

	if (hdmi->hdmi_data.sink_is_hdmi) {
		rk618_hdmi_config_video_avi(hdmi, mode);
		rk618_hdmi_config_video_vsi(hdmi, mode);
	}

	/*
	 * When IP controller have configured to an accurate video
	 * timing, then the TMDS clock source would be switched to
	 * DCLK_LCDC, so we need to init the TMDS rate to mode pixel
	 * clock rate, and reconfigure the DDC clock.
	 */
	hdmi->tmds_rate = mode->clock * 1000;
	rk618_hdmi_i2c_init(hdmi);

	/* Unmute video and audio output */
	hdmi_modb(hdmi, HDMI_AV_MUTE, m_VIDEO_BLACK, v_VIDEO_MUTE(0));
	if (hdmi->audio_enable)
		hdmi_modb(hdmi, HDMI_AV_MUTE, m_AUDIO_MUTE, v_AUDIO_MUTE(0));

	return 0;
}

static bool rk618_hdmi_hpd_detect(struct rk618_hdmi *hdmi)
{
	return !!(hdmi_readb(hdmi, HDMI_STATUS) & m_HOTPLUG);
}

static enum drm_connector_status
rk618_hdmi_connector_detect(struct drm_connector *connector, bool force)
{
	struct rk618_hdmi *hdmi = connector_to_hdmi(connector);
	bool status;

	status = rk618_hdmi_hpd_detect(hdmi);
#ifdef CONFIG_SWITCH
	switch_set_state(&hdmi->switchdev, status);
#endif

	return status ? connector_status_connected :
			connector_status_disconnected;
}

static int rk618_hdmi_connector_get_modes(struct drm_connector *connector)
{
	struct rk618_hdmi *hdmi = connector_to_hdmi(connector);
	struct drm_display_info *info = &connector->display_info;
	struct edid *edid = NULL;
	int ret = 0;

	if (!hdmi->ddc)
		return 0;

	if (rk618_hdmi_hpd_detect(hdmi))
		edid = drm_get_edid(connector, hdmi->ddc);

	if (edid) {
		hdmi->hdmi_data.sink_is_hdmi = drm_detect_hdmi_monitor(edid);
		hdmi->hdmi_data.sink_has_audio = drm_detect_monitor_audio(edid);
		drm_connector_update_edid_property(connector, edid);
		ret = drm_add_edid_modes(connector, edid);
		kfree(edid);
	} else {
		hdmi->hdmi_data.sink_is_hdmi = true;
		hdmi->hdmi_data.sink_has_audio = true;
		ret = rockchip_drm_add_modes_noedid(connector);
		info->edid_hdmi_dc_modes = 0;
		info->hdmi.y420_dc_modes = 0;
		info->color_formats = 0;

		dev_info(hdmi->dev, "failed to get edid\n");
	}

	return ret;
}

static enum drm_mode_status
rk618_hdmi_connector_mode_valid(struct drm_connector *connector,
				struct drm_display_mode *mode)
{
	if ((mode->hdisplay == 1920 && mode->vdisplay == 1080) ||
	    (mode->hdisplay == 1280 && mode->vdisplay == 720))
		return MODE_OK;
	else
		return MODE_BAD;
}

static struct drm_encoder *
rk618_hdmi_connector_best_encoder(struct drm_connector *connector)
{
	struct rk618_hdmi *hdmi = connector_to_hdmi(connector);

	return hdmi->base.encoder;
}

static int
rk618_hdmi_probe_single_connector_modes(struct drm_connector *connector,
					uint32_t maxX, uint32_t maxY)
{
	return drm_helper_probe_single_connector_modes(connector, 1920, 1080);
}

static const struct drm_connector_funcs rk618_hdmi_connector_funcs = {
	.fill_modes = rk618_hdmi_probe_single_connector_modes,
	.detect = rk618_hdmi_connector_detect,
	.destroy = drm_connector_cleanup,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static const struct drm_connector_helper_funcs
rk618_hdmi_connector_helper_funcs = {
	.get_modes = rk618_hdmi_connector_get_modes,
	.mode_valid = rk618_hdmi_connector_mode_valid,
	.best_encoder = rk618_hdmi_connector_best_encoder,
};

static void rk618_hdmi_bridge_mode_set(struct drm_bridge *bridge,
				       struct drm_display_mode *mode,
				       struct drm_display_mode *adj_mode)
{
	struct rk618_hdmi *hdmi = bridge_to_hdmi(bridge);

	/* Store the display mode for plugin/DPMS poweron events */
	memcpy(&hdmi->previous_mode, adj_mode, sizeof(hdmi->previous_mode));
}

static void rk618_hdmi_bridge_enable(struct drm_bridge *bridge)
{
	struct rk618_hdmi *hdmi = bridge_to_hdmi(bridge);

	clk_prepare_enable(hdmi->clock);

	if (!rk618_hdmi_hpd_detect(hdmi)) {
		rk618_hdmi_set_pwr_mode(hdmi, LOWER_PWR);
		return;
	}

	rk618_hdmi_setup(hdmi, &hdmi->previous_mode);
	rk618_hdmi_set_polarity(hdmi, hdmi->hdmi_data.vic);
	rk618_hdmi_set_pwr_mode(hdmi, NORMAL);
}

static void rk618_hdmi_bridge_disable(struct drm_bridge *bridge)
{
	struct rk618_hdmi *hdmi = bridge_to_hdmi(bridge);

	rk618_hdmi_set_pwr_mode(hdmi, LOWER_PWR);

	clk_disable_unprepare(hdmi->clock);
}

static int rk618_hdmi_bridge_attach(struct drm_bridge *bridge)
{
	struct rk618_hdmi *hdmi = bridge_to_hdmi(bridge);
	struct device *dev = hdmi->dev;
	struct drm_connector *connector = &hdmi->connector;
	struct drm_device *drm = bridge->dev;
	struct device_node *endpoint;
	int ret;

	connector->polled = DRM_CONNECTOR_POLL_HPD;

	ret = drm_connector_init(drm, connector, &rk618_hdmi_connector_funcs,
				 DRM_MODE_CONNECTOR_HDMIA);
	if (ret) {
		dev_err(hdmi->dev, "Failed to initialize connector with drm\n");
		return ret;
	}

	drm_connector_helper_add(connector,
				 &rk618_hdmi_connector_helper_funcs);
	drm_connector_attach_encoder(connector, bridge->encoder);

	hdmi->sub_dev.connector = &hdmi->connector;
	hdmi->sub_dev.of_node = hdmi->dev->of_node;
	rockchip_drm_register_sub_dev(&hdmi->sub_dev);

	endpoint = of_graph_get_endpoint_by_regs(dev->of_node, 1, -1);
	if (endpoint && of_device_is_available(endpoint)) {
		struct device_node *remote;

		remote = of_graph_get_remote_port_parent(endpoint);
		of_node_put(endpoint);
		if (!remote || !of_device_is_available(remote))
			return -ENODEV;

		hdmi->bridge = of_drm_find_bridge(remote);
		of_node_put(remote);
		if (!hdmi->bridge)
			return -EPROBE_DEFER;

		ret = drm_bridge_attach(bridge->encoder, hdmi->bridge, bridge);
		if (ret) {
			dev_err(dev, "failed to attach bridge\n");
			return ret;
		}
	}

	return 0;
}

static void rk618_hdmi_bridge_detach(struct drm_bridge *bridge)
{
	struct rk618_hdmi *hdmi = bridge_to_hdmi(bridge);

	rockchip_drm_unregister_sub_dev(&hdmi->sub_dev);
}

static const struct drm_bridge_funcs rk618_hdmi_bridge_funcs = {
	.attach = rk618_hdmi_bridge_attach,
	.detach = rk618_hdmi_bridge_detach,
	.mode_set = rk618_hdmi_bridge_mode_set,
	.enable = rk618_hdmi_bridge_enable,
	.disable = rk618_hdmi_bridge_disable,
};

static int
rk618_hdmi_audio_config_set(struct rk618_hdmi *hdmi, struct audio_info *audio)
{
	int rate, N, channel;

	if (audio->channels < 3)
		channel = I2S_CHANNEL_1_2;
	else if (audio->channels < 5)
		channel = I2S_CHANNEL_3_4;
	else if (audio->channels < 7)
		channel = I2S_CHANNEL_5_6;
	else
		channel = I2S_CHANNEL_7_8;

	switch (audio->sample_rate) {
	case 32000:
		rate = AUDIO_32K;
		N = N_32K;
		break;
	case 44100:
		rate = AUDIO_441K;
		N = N_441K;
		break;
	case 48000:
		rate = AUDIO_48K;
		N = N_48K;
		break;
	case 88200:
		rate = AUDIO_882K;
		N = N_882K;
		break;
	case 96000:
		rate = AUDIO_96K;
		N = N_96K;
		break;
	case 176400:
		rate = AUDIO_1764K;
		N = N_1764K;
		break;
	case 192000:
		rate = AUDIO_192K;
		N = N_192K;
		break;
	default:
		dev_err(hdmi->dev, "[%s] not support such sample rate %d\n",
			__func__, audio->sample_rate);
		return -ENOENT;
	}

	/* set_audio source I2S */
	hdmi_writeb(hdmi, HDMI_AUDIO_CTRL1, 0x01);
	hdmi_writeb(hdmi, AUDIO_SAMPLE_RATE, rate);
	hdmi_writeb(hdmi, AUDIO_I2S_MODE, v_I2S_MODE(I2S_STANDARD) |
		    v_I2S_CHANNEL(channel));

	hdmi_writeb(hdmi, AUDIO_I2S_MAP, 0x00);
	hdmi_writeb(hdmi, AUDIO_I2S_SWAPS_SPDIF, 0);

	/* Set N value */
	hdmi_writeb(hdmi, AUDIO_N_H, (N >> 16) & 0x0F);
	hdmi_writeb(hdmi, AUDIO_N_M, (N >> 8) & 0xFF);
	hdmi_writeb(hdmi, AUDIO_N_L, N & 0xFF);

	/*Set hdmi nlpcm mode to support hdmi bitstream*/
	hdmi_writeb(hdmi, HDMI_AUDIO_CHANNEL_STATUS, v_AUDIO_STATUS_NLPCM(0));

	return rk618_hdmi_config_audio_aai(hdmi, audio);
}

static int rk618_hdmi_audio_hw_params(struct device *dev, void *d,
				      struct hdmi_codec_daifmt *daifmt,
				      struct hdmi_codec_params *params)
{
	struct rk618_hdmi *hdmi = dev_get_drvdata(dev);
	struct audio_info audio = {
		.sample_width = params->sample_width,
		.sample_rate = params->sample_rate,
		.channels = params->channels,
	};

	if (!hdmi->hdmi_data.sink_has_audio) {
		dev_err(hdmi->dev, "Sink do not support audio!\n");
		return -ENODEV;
	}

	if (!hdmi->base.encoder->crtc)
		return -ENODEV;

	switch (daifmt->fmt) {
	case HDMI_I2S:
		break;
	default:
		dev_err(dev, "%s: Invalid format %d\n", __func__, daifmt->fmt);
		return -EINVAL;
	}

	return rk618_hdmi_audio_config_set(hdmi, &audio);
}

static void rk618_hdmi_audio_shutdown(struct device *dev, void *d)
{
	/* do nothing */
}

static int rk618_hdmi_audio_digital_mute(struct device *dev, void *d, bool mute)
{
	struct rk618_hdmi *hdmi = dev_get_drvdata(dev);

	if (!hdmi->hdmi_data.sink_has_audio) {
		dev_err(hdmi->dev, "Sink do not support audio!\n");
		return -ENODEV;
	}

	hdmi->audio_enable = !mute;

	if (mute)
		hdmi_modb(hdmi, HDMI_AV_MUTE, m_AUDIO_MUTE | m_AUDIO_PD,
			  v_AUDIO_MUTE(1) | v_AUDIO_PD(1));
	else
		hdmi_modb(hdmi, HDMI_AV_MUTE, m_AUDIO_MUTE | m_AUDIO_PD,
			  v_AUDIO_MUTE(0) | v_AUDIO_PD(0));

	return 0;
}

static int rk618_hdmi_audio_get_eld(struct device *dev, void *d,
				    uint8_t *buf, size_t len)
{
	struct rk618_hdmi *hdmi = dev_get_drvdata(dev);
	struct drm_mode_config *config = &hdmi->base.dev->mode_config;
	struct drm_connector *connector;
	int ret = -ENODEV;

	mutex_lock(&config->mutex);
	list_for_each_entry(connector, &config->connector_list, head) {
		if (hdmi->base.encoder == connector->encoder) {
			memcpy(buf, connector->eld,
			       min(sizeof(connector->eld), len));
			ret = 0;
		}
	}
	mutex_unlock(&config->mutex);

	return ret;
}

static const struct hdmi_codec_ops audio_codec_ops = {
	.hw_params = rk618_hdmi_audio_hw_params,
	.audio_shutdown = rk618_hdmi_audio_shutdown,
	.digital_mute = rk618_hdmi_audio_digital_mute,
	.get_eld = rk618_hdmi_audio_get_eld,
};

static int rk618_hdmi_audio_codec_init(struct rk618_hdmi *hdmi,
				       struct device *dev)
{
	struct hdmi_codec_pdata codec_data = {
		.i2s = 1,
		.ops = &audio_codec_ops,
		.max_i2s_channels = 8,
	};

	hdmi->audio_enable = false;
	hdmi->audio_pdev = platform_device_register_data(dev,
				HDMI_CODEC_DRV_NAME, PLATFORM_DEVID_NONE,
				&codec_data, sizeof(codec_data));

	return PTR_ERR_OR_ZERO(hdmi->audio_pdev);
}

static irqreturn_t rk618_hdmi_irq(int irq, void *dev_id)
{
	struct rk618_hdmi *hdmi = dev_id;

	hdmi_modb(hdmi, HDMI_INTERRUPT_STATUS1, m_INT_HOTPLUG_RK618,
		  m_INT_HOTPLUG_RK618);

	if (hdmi->connector.dev)
		drm_helper_hpd_irq_event(hdmi->connector.dev);

	return IRQ_HANDLED;
}

static int rk618_hdmi_i2c_read(struct rk618_hdmi *hdmi, struct i2c_msg *msgs)
{
	int length = msgs->len;
	u8 *buf = msgs->buf;
	int i;
	u32 c;

	for (i = 0; i < 10; i++) {
		msleep(20);
		c = hdmi_readb(hdmi, HDMI_INTERRUPT_STATUS1);

		if (c & m_INT_EDID_READY)
			break;
	}
	if ((c & m_INT_EDID_READY) == 0)
		return -EAGAIN;

	while (length--)
		*buf++ = hdmi_readb(hdmi, HDMI_EDID_FIFO_ADDR);

	return 0;
}

static int rk618_hdmi_i2c_write(struct rk618_hdmi *hdmi, struct i2c_msg *msgs)
{
	/*
	 * The DDC module only support read EDID message, so
	 * we assume that each word write to this i2c adapter
	 * should be the offset of EDID word address.
	 */
	if ((msgs->len != 1) ||
	    ((msgs->addr != DDC_ADDR) && (msgs->addr != DDC_SEGMENT_ADDR)))
		return -EINVAL;

	if (msgs->addr == DDC_ADDR)
		hdmi->i2c->ddc_addr = msgs->buf[0];
	if (msgs->addr == DDC_SEGMENT_ADDR) {
		hdmi->i2c->segment_addr = msgs->buf[0];
		return 0;
	}

	/* Set edid fifo first addr */
	hdmi_writeb(hdmi, HDMI_EDID_FIFO_OFFSET, 0x00);

	/* Set edid word address 0x00/0x80 */
	hdmi_writeb(hdmi, HDMI_EDID_WORD_ADDR, hdmi->i2c->ddc_addr);

	/* Set edid segment pointer */
	hdmi_writeb(hdmi, HDMI_EDID_SEGMENT_POINTER, hdmi->i2c->segment_addr);

	return 0;
}

static int rk618_hdmi_i2c_xfer(struct i2c_adapter *adap,
			       struct i2c_msg *msgs, int num)
{
	struct rk618_hdmi *hdmi = i2c_get_adapdata(adap);
	struct rk618_hdmi_i2c *i2c = hdmi->i2c;
	int i, ret = 0;

	mutex_lock(&i2c->lock);

	hdmi->i2c->ddc_addr = 0;
	hdmi->i2c->segment_addr = 0;

	/* Clear the EDID interrupt flag and unmute the interrupt */
	hdmi_writeb(hdmi, HDMI_INTERRUPT_MASK1, m_INT_EDID_READY);
	hdmi_writeb(hdmi, HDMI_INTERRUPT_STATUS1, m_INT_EDID_READY);

	for (i = 0; i < num; i++) {
		dev_dbg(hdmi->dev, "xfer: num: %d/%d, len: %d, flags: %#x\n",
			i + 1, num, msgs[i].len, msgs[i].flags);

		if (msgs[i].flags & I2C_M_RD)
			ret = rk618_hdmi_i2c_read(hdmi, &msgs[i]);
		else
			ret = rk618_hdmi_i2c_write(hdmi, &msgs[i]);

		if (ret < 0)
			break;
	}

	if (!ret)
		ret = num;

	/* Mute HDMI EDID interrupt */
	hdmi_writeb(hdmi, HDMI_INTERRUPT_MASK1, 0);
	hdmi_modb(hdmi, HDMI_INTERRUPT_MASK1, m_INT_HOTPLUG_RK618,
		  m_INT_HOTPLUG_RK618);

	mutex_unlock(&i2c->lock);

	return ret;
}

static u32 rk618_hdmi_i2c_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static const struct i2c_algorithm rk618_hdmi_algorithm = {
	.master_xfer	= rk618_hdmi_i2c_xfer,
	.functionality	= rk618_hdmi_i2c_func,
};

static struct i2c_adapter *rk618_hdmi_i2c_adapter(struct rk618_hdmi *hdmi)
{
	struct i2c_adapter *adap;
	struct rk618_hdmi_i2c *i2c;
	int ret;

	i2c = devm_kzalloc(hdmi->dev, sizeof(*i2c), GFP_KERNEL);
	if (!i2c)
		return ERR_PTR(-ENOMEM);

	mutex_init(&i2c->lock);

	adap = &i2c->adap;
	adap->class = I2C_CLASS_DDC;
	adap->owner = THIS_MODULE;
	adap->dev.parent = hdmi->dev;
	adap->dev.of_node = hdmi->dev->of_node;
	adap->algo = &rk618_hdmi_algorithm;
	strlcpy(adap->name, "RK618 HDMI", sizeof(adap->name));
	i2c_set_adapdata(adap, hdmi);

	ret = i2c_add_adapter(adap);
	if (ret) {
		dev_warn(hdmi->dev, "cannot add %s I2C adapter\n", adap->name);
		devm_kfree(hdmi->dev, i2c);
		return ERR_PTR(ret);
	}

	hdmi->i2c = i2c;

	dev_info(hdmi->dev, "registered %s I2C bus driver\n", adap->name);

	return adap;
}

static const struct regmap_range rk618_hdmi_volatile_reg_ranges[] = {
	regmap_reg_range(0x0400, 0x07b4),
};

static const struct regmap_access_table rk618_hdmi_volatile_regs = {
	.yes_ranges = rk618_hdmi_volatile_reg_ranges,
	.n_yes_ranges = ARRAY_SIZE(rk618_hdmi_volatile_reg_ranges),
};

static bool rk618_is_read_enable_reg(struct device *dev, unsigned int reg)
{
	if (reg >= RK618_HDMI_BASE &&
	    reg <= (HDMI_CEC_LOGICADDR * RK618_HDMI_BASE))
		return true;

	return false;
}

static const struct regmap_config rk618_hdmi_regmap_config = {
	.name = "hdmi",
	.reg_bits = 16,
	.val_bits = 32,
	.reg_stride = 4,
	.max_register = 0x07b4,
	.cache_type = REGCACHE_RBTREE,
	.reg_format_endian = REGMAP_ENDIAN_NATIVE,
	.val_format_endian = REGMAP_ENDIAN_NATIVE,
	.readable_reg = rk618_is_read_enable_reg,
	.volatile_table = &rk618_hdmi_volatile_regs,
};

static int rk618_hdmi_probe(struct platform_device *pdev)
{
	struct rk618 *rk618 = dev_get_drvdata(pdev->dev.parent);
	struct device *dev = &pdev->dev;
	struct rk618_hdmi *hdmi;
	int irq;
	int ret;

	if (!of_device_is_available(dev->of_node))
		return -ENODEV;

	hdmi = devm_kzalloc(dev, sizeof(*hdmi), GFP_KERNEL);
	if (!hdmi)
		return -ENOMEM;

	hdmi->dev = dev;
	hdmi->parent = rk618;
	platform_set_drvdata(pdev, hdmi);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	hdmi->regmap = devm_regmap_init_i2c(rk618->client,
					    &rk618_hdmi_regmap_config);
	if (IS_ERR(hdmi->regmap)) {
		ret = PTR_ERR(hdmi->regmap);
		dev_err(dev, "failed to allocate register map: %d\n", ret);
		return PTR_ERR(hdmi->regmap);
	}

	hdmi->clock = devm_clk_get(dev, "hdmi");
	if (IS_ERR(hdmi->clock)) {
		dev_err(dev, "Unable to get HDMI clock\n");
		return PTR_ERR(hdmi->clock);
	}

	rk618_hdmi_pol_init(hdmi, 0);
	rk618_hdmi_reset(hdmi);

	hdmi->ddc = rk618_hdmi_i2c_adapter(hdmi);
	if (IS_ERR(hdmi->ddc)) {
		ret = PTR_ERR(hdmi->ddc);
		hdmi->ddc = NULL;
		return ret;
	}

	/*
	 * When IP controller haven't configured to an accurate video
	 * timing, then the TMDS clock source would be switched to
	 * PCLK_HDMI, so we need to init the TMDS rate to PCLK rate,
	 * and reconfigure the DDC clock.
	 */
	hdmi->tmds_rate = clk_get_rate(rk618->clkin);

	rk618_hdmi_i2c_init(hdmi);
	rk618_hdmi_audio_codec_init(hdmi, dev);

	/* Unmute hotplug interrupt */
	hdmi_modb(hdmi, HDMI_STATUS, m_MASK_INT_HOTPLUG, v_MASK_INT_HOTPLUG(1));
	hdmi_modb(hdmi, HDMI_INTERRUPT_STATUS1, m_INT_HOTPLUG_RK618,
		  m_INT_HOTPLUG_RK618);

	ret = devm_request_threaded_irq(dev, irq, NULL,
					rk618_hdmi_irq,
					IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
					dev_name(dev), hdmi);
	if (ret) {
		dev_err(dev, "failed to request hdmi irq: %d\n", ret);
		return ret;
	}

	hdmi->base.funcs = &rk618_hdmi_bridge_funcs;
	hdmi->base.of_node = dev->of_node;
	drm_bridge_add(&hdmi->base);

#ifdef CONFIG_SWITCH
	hdmi->switchdev.name = "hdmi";
	switch_dev_register(&hdmi->switchdev);
#endif

	return 0;
}

static int rk618_hdmi_remove(struct platform_device *pdev)
{
	struct rk618_hdmi *hdmi = platform_get_drvdata(pdev);

	drm_bridge_remove(&hdmi->base);
	i2c_put_adapter(hdmi->ddc);
#ifdef CONFIG_SWITCH
	switch_dev_unregister(&hdmi->switchdev);
#endif

	return 0;
}

static const struct of_device_id rk618_hdmi_dt_ids[] = {
	{ .compatible = "rockchip,rk618-hdmi", },
	{},
};
MODULE_DEVICE_TABLE(of, rk618_hdmi_dt_ids);

static struct platform_driver rk618_hdmi_driver = {
	.probe  = rk618_hdmi_probe,
	.remove = rk618_hdmi_remove,
	.driver = {
		.name = "rk618-hdmi",
		.of_match_table = rk618_hdmi_dt_ids,
	},
};

module_platform_driver(rk618_hdmi_driver);

MODULE_AUTHOR("Chen Shunqing <csq@rock-chips.com>");
MODULE_AUTHOR("Zheng Yang <zhengyang@rock-chips.com>");
MODULE_AUTHOR("Yakir Yang <ykk@rock-chips.com>");
MODULE_DESCRIPTION("Rockchip RK618 HDMI driver");
MODULE_LICENSE("GPL v2");
