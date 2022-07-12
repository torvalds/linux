// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) Fuzhou Rockchip Electronics Co.Ltd
 *    Zheng Yang <zhengyang@rock-chips.com>
 *    Yakir Yang <ykk@rock-chips.com>
 */

#include <linux/irq.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/hdmi.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of_device.h>
#include <linux/component.h>
#include<linux/reset.h>

#include <drm/bridge/dw_hdmi.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_edid.h>
#include <drm/drm_of.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_simple_kms_helper.h>
#include <linux/regulator/consumer.h>

#include "vs_drv.h"

#include "inno_hdmi.h"

#define to_inno_hdmi(x)	container_of(x, struct inno_hdmi, x)

struct hdmi_data_info {
	int vic;
	bool sink_is_hdmi;
	bool sink_has_audio;
	unsigned int enc_in_format;
	unsigned int enc_out_format;
	unsigned int colorimetry;
};

struct inno_hdmi_i2c {
	struct i2c_adapter adap;

	u8 ddc_addr;
	u8 segment_addr;

	struct mutex lock;
	struct completion cmp;
};

struct inno_hdmi {
	struct device *dev;
	struct drm_device *drm_dev;

	int irq;
	struct clk *pclk;
	struct clk *sys_clk;
	struct clk *mclk;
	struct clk *bclk;
	struct reset_control *tx_rst;
	void __iomem *regs;

	struct drm_connector	connector;
	struct drm_encoder	encoder;

	struct inno_hdmi_i2c *i2c;
	struct i2c_adapter *ddc;

	unsigned long tmds_rate;

	struct hdmi_data_info	hdmi_data;
	struct drm_display_mode previous_mode;
	struct regulator *hdmi_1p8;
	struct regulator *hdmi_0p9;
	const struct pre_pll_config 	*pre_cfg;
	const struct post_pll_config 	*post_cfg;
};

enum {
	CSC_ITU601_16_235_TO_RGB_0_255_8BIT,
	CSC_ITU601_0_255_TO_RGB_0_255_8BIT,
	CSC_ITU709_16_235_TO_RGB_0_255_8BIT,
	CSC_RGB_0_255_TO_ITU601_16_235_8BIT,
	CSC_RGB_0_255_TO_ITU709_16_235_8BIT,
	CSC_RGB_0_255_TO_RGB_16_235_8BIT,
};

static const struct pre_pll_config pre_pll_cfg_table[] = {
	{ 25200000,  25200000, 1,  100, 2, 3, 3, 12, 3, 3, 4, 0, 0},
	{ 27000000,  27000000, 1,  90, 3, 2, 2, 10, 3, 3, 4, 0, 0},
	{ 27000000,  33750000, 1,  90, 1, 3, 3, 10, 3, 3, 4, 0, 0},
	{ 40000000,  40000000, 1,  80, 2, 2, 2, 12, 2, 2, 2, 0, 0},
	{ 59341000,  59341000, 1,  98, 3, 1, 2,  1, 3, 3, 4, 0, 0xE6AE6B},
	{ 59400000,  59400000, 1,  99, 3, 1, 1,  1, 3, 3, 4, 0, 0},
	{ 59341000,  74176250, 1,  98, 0, 3, 3,  1, 3, 3, 4, 0, 0xE6AE6B},
	{ 59400000,  74250000, 1,  99, 1, 2, 2,  1, 3, 3, 4, 0, 0},
	{ 74176000,  74176000, 1,  98, 1, 2, 2,  1, 2, 3, 4, 0, 0xE6AE6B},
	{ 74250000,  74250000, 1,  99, 1, 2, 2,  1, 2, 3, 4, 0, 0},
	{ 74176000,  92720000, 4, 494, 1, 2, 2,  1, 3, 3, 4, 0, 0x816817},
	{ 74250000,  92812500, 4, 495, 1, 2, 2,  1, 3, 3, 4, 0, 0},
	{148352000, 148352000, 1,  98, 1, 1, 1,  1, 2, 2, 2, 0, 0xE6AE6B},
	{148500000, 148500000, 1,  99, 1, 1, 1,  1, 2, 2, 2, 0, 0},
	{148352000, 185440000, 4, 494, 0, 2, 2,  1, 3, 2, 2, 0, 0x816817},
	{148500000, 185625000, 4, 495, 0, 2, 2,  1, 3, 2, 2, 0, 0},
	{296703000, 296703000, 1,  98, 0, 1, 1,  1, 0, 2, 2, 0, 0xE6AE6B},
	{297000000, 297000000, 1,  99, 0, 1, 1,  1, 0, 2, 2, 0, 0},
	{296703000, 370878750, 4, 494, 1, 2, 0,  1, 3, 1, 1, 0, 0x816817},
	{297000000, 371250000, 4, 495, 1, 2, 0,  1, 3, 1, 1, 0, 0},
	{593407000, 296703500, 1,  98, 0, 1, 1,  1, 0, 2, 1, 0, 0xE6AE6B},
	{594000000, 297000000, 1,  99, 0, 1, 1,  1, 0, 2, 1, 0, 0},
	{593407000, 370879375, 4, 494, 1, 2, 0,  1, 3, 1, 1, 1, 0x816817},
	{594000000, 371250000, 4, 495, 1, 2, 0,  1, 3, 1, 1, 1, 0},
	{593407000, 593407000, 1,  98, 0, 2, 0,  1, 0, 1, 1, 0, 0xE6AE6B},
	{594000000, 594000000, 1,  99, 0, 2, 0,  1, 0, 1, 1, 0, 0},
	{ /* sentinel */ }
};

static const struct post_pll_config post_pll_cfg_table[] = {
	{25200000,	1, 80, 7, 0,1},
	{27000000,	1, 40, 3, 0,1},
	{33750000,	1, 40, 8, 3,1},
	//{33750000,	1, 80, 8, 2},
	{74250000,	1, 20, 1, 3,1},
	//{74250000, 18, 80, 8, 2},
	{148500000, 1, 20, 1, 3,3},
	{297000000, 2, 20, 0, 3,3},
	{594000000, 4, 20, 0, 0,0},//postpll_postdiv_en = 0
	{ /* sentinel */ }
};
/*
static const struct post_pll_config post_pll_cfg_table[] = {
	{33750000,  1, 40, 8, 1},
	{33750000,  1, 80, 8, 2},
	{74250000,  1, 40, 8, 1},
	{74250000, 18, 80, 8, 2},
	{148500000, 2, 40, 4, 3},
	{297000000, 4, 40, 2, 3},
	{594000000, 8, 40, 1, 3},
};
*/
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

static void inno_hdmi_power_up(struct inno_hdmi *hdmi)
{
	u8 val;

	val = readl_relaxed(hdmi->regs + (0x1b0) * 0x04);
	val |= 0x4;
	writel_relaxed(val, hdmi->regs + (0x1b0) * 0x04);
	writel_relaxed(0xf, hdmi->regs + (0x1cc) * 0x04);

	/*turn on pre-PLL*/
	val = readl_relaxed(hdmi->regs + (0x1a0) * 0x04);
	val &= ~(0x1);
	writel_relaxed(val, hdmi->regs + (0x1a0) * 0x04);
	/*turn on post-PLL*/
	val = readl_relaxed(hdmi->regs + (0x1aa) * 0x04);
	val &= ~(0x1);
	writel_relaxed(val, hdmi->regs + (0x1aa) * 0x04);

	while (!(readl_relaxed(hdmi->regs + (0x1a9) * 0x04) & 0x1))
	;
	while (!(readl_relaxed(hdmi->regs + (0x1af) * 0x04) & 0x1))
	;

	/*turn on LDO*/
	writel_relaxed(0x7, hdmi->regs + (0x1b4) * 0x04);
	/*turn on serializer*/
	writel_relaxed(0x70, hdmi->regs + (0x1be) * 0x04);
}

static void inno_hdmi_tx_phy_power_down(struct inno_hdmi *hdmi)
{
	//hdmi_write(0x63, word_align(0x00));
	writel_relaxed(0x63, hdmi->regs + (0x00) * 0x04);
}

static void inno_hdmi_config_pll(struct inno_hdmi *hdmi)
{
	u8 reg_1ad_value = hdmi->post_cfg->post_div_en ?
		 hdmi->post_cfg->postdiv : 0x0d;
	const reg_value_t cfg_pll_data[] = {
		{0x1a0, 0x01},
		{0x1aa, 0x0f},
		{0x1a1, hdmi->pre_cfg->prediv},
		{0x1a2, 0xf0 | hdmi->pre_cfg->fbdiv>>8},
		{0x1a3, hdmi->pre_cfg->fbdiv},
		{0x1a4, ((hdmi->pre_cfg->tmds_div_a << 4) | (hdmi->pre_cfg->tmds_div_b << 2) | (hdmi->pre_cfg->tmds_div_c))},
		{0x1a5, (hdmi->pre_cfg->pclk_div_b << 5) | hdmi->pre_cfg->pclk_div_a},
		{0x1a6, (hdmi->pre_cfg->pclk_div_c << 5) | hdmi->pre_cfg->pclk_div_d},
		{0x1ab, hdmi->post_cfg->prediv},
		{0x1ac, hdmi->post_cfg->fbdiv & 0xff},
		{0x1ad, reg_1ad_value},
		{0x1aa, 0x0e},
		{0x1a0, 0x00},
	};

	int i;
	for (i = 0; i < sizeof(cfg_pll_data) / sizeof(reg_value_t); i++)
	{
		/*
		if(0x1ad == cfg_pll_data[i].reg)
		{
			if(0 == hdmi->post_cfg->post_div_en)
			{
				writel_relaxed(0x0d, hdmi->regs + (cfg_pll_data[i].reg) * 0x04);
				continue;
			}
		}*/
		dev_info(hdmi->dev, "%s %d reg[%02x],val[%02x]\n",__func__, __LINE__,cfg_pll_data[i].reg,cfg_pll_data[i].value);
		writel_relaxed(cfg_pll_data[i].value, hdmi->regs + (cfg_pll_data[i].reg) * 0x04);
	}
	return;
}

static void inno_hdmi_config_1920x1080p60(struct inno_hdmi *hdmi)
{

	const reg_value_t cfg_pll_data[] = {
		/* config pll: 1080p, 60hz*/
		{0x1a0, 0x01},
		{0x1aa, 0x0f},
		{0x1a1, 0x01},
		{0x1a2, 0xf0},
		{0x1a3, 0x63},
		{0x1a4, 0x15},
		{0x1a5, 0x41},
		{0x1a6, 0x42},
		{0x1ab, 0x01},
		{0x1ac, 0x0a},
		{0x1ad, 0x00},
		{0x1aa, 0x0e},
		{0x1a0, 0x00},
	};

	int i;
	for (i = 0; i < sizeof(cfg_pll_data) / sizeof(reg_value_t); i++)
		writel_relaxed(cfg_pll_data[i].value, hdmi->regs + (cfg_pll_data[i].reg) * 0x04);

	return;
}

static void inno_hdmi_tx_ctrl(struct inno_hdmi *hdmi)
{
	writel_relaxed(0x06, hdmi->regs + (0x9f) * 0x04);
	writel_relaxed(0x82, hdmi->regs + (0xa0) * 0x04);
	writel_relaxed(0xd, hdmi->regs + (0xa2) * 0x04);
	writel_relaxed(0x0, hdmi->regs + (0xa3) * 0x04);
	writel_relaxed(0x0, hdmi->regs + (0xa4) * 0x04);
	writel_relaxed(0x8, hdmi->regs + (0xa5) * 0x04);
	writel_relaxed(0x70, hdmi->regs + (0xa6) * 0x04);
	writel_relaxed(0x10, hdmi->regs + (0xa7) * 0x04);
	writel_relaxed(0x10, hdmi->regs + (0xc9) * 0x04);
}

static void inno_hdmi_tx_phy_param_config(struct inno_hdmi *hdmi)
{
	inno_hdmi_config_1920x1080p60(hdmi);
	inno_hdmi_tx_ctrl(hdmi);

    return;
}

static void inno_hdmi_tx_phy_power_on(struct inno_hdmi *hdmi)
{
	const reg_value_t pwon_data[] = {
		{0x00, 0x61},
	};
	int i;
	for (i = 0; i < sizeof(pwon_data)/sizeof(reg_value_t); i++) {
		writel_relaxed(pwon_data[i].value, hdmi->regs + (pwon_data[i].reg) * 0x04);
	}
	return;
}

void inno_hdmi_tmds_driver_on(struct inno_hdmi *hdmi)
{
	writel_relaxed(0x8f, hdmi->regs + (0x1b2) * 0x04);
}


static void inno_hdmi_i2c_init(struct inno_hdmi *hdmi)
{
	int ddc_bus_freq;

	ddc_bus_freq = (hdmi->tmds_rate >> 2) / HDMI_SCL_RATE;

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

static void inno_hdmi_set_pwr_mode(struct inno_hdmi *hdmi, int mode)
{
	switch (mode) {
	case NORMAL:
		inno_hdmi_sys_power(hdmi, true);
		break;

	case LOWER_PWR:
		inno_hdmi_sys_power(hdmi, false);
		break;

	default:
		DRM_DEV_ERROR(hdmi->dev, "Unknown power mode %d\n", mode);
	}
}

static void inno_hdmi_init(struct inno_hdmi *hdmi)
{
	writel_relaxed(0x3, hdmi->regs + (0x100) * 0x04);
	writel_relaxed(0xc, hdmi->regs + (0x8) * 0x04);

	inno_hdmi_power_up(hdmi);
	inno_hdmi_tx_phy_power_down(hdmi);
	inno_hdmi_tx_phy_param_config(hdmi);
	//inno_hdmi_wait_pll_clk_locked();

	inno_hdmi_tx_phy_power_on(hdmi);
	inno_hdmi_tmds_driver_on(hdmi);

	writel_relaxed(0x0, hdmi->regs + (0xce) * 0x04);
	writel_relaxed(0x1, hdmi->regs + (0xce) * 0x04);

}

static void inno_hdmi_reset(struct inno_hdmi *hdmi)
{
	u32 val;
	u32 msk;

	hdmi_modb(hdmi, HDMI_SYS_CTRL, m_RST_DIGITAL, v_NOT_RST_DIGITAL);
	udelay(100);

	hdmi_modb(hdmi, HDMI_SYS_CTRL, m_RST_ANALOG, v_NOT_RST_ANALOG);
	udelay(100);

	msk = m_REG_CLK_INV | m_REG_CLK_SOURCE | m_POWER | m_INT_POL;
	val = v_REG_CLK_INV | v_REG_CLK_SOURCE_SYS | v_PWR_ON | v_INT_POL_HIGH;
	hdmi_modb(hdmi, HDMI_SYS_CTRL, msk, val);

	inno_hdmi_set_pwr_mode(hdmi, NORMAL);
}

static int inno_hdmi_upload_frame(struct inno_hdmi *hdmi, int setup_rc,
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

static int inno_hdmi_config_video_vsi(struct inno_hdmi *hdmi,
				      struct drm_display_mode *mode)
{
	union hdmi_infoframe frame;
	int rc;

	rc = drm_hdmi_vendor_infoframe_from_display_mode(&frame.vendor.hdmi,
							 &hdmi->connector,
							 mode);

	return inno_hdmi_upload_frame(hdmi, rc, &frame, INFOFRAME_VSI,
		m_PACKET_VSI_EN, v_PACKET_VSI_EN(0), v_PACKET_VSI_EN(1));
}

static int inno_hdmi_config_video_avi(struct inno_hdmi *hdmi,
				      struct drm_display_mode *mode)
{
	union hdmi_infoframe frame;
	int rc;

	rc = drm_hdmi_avi_infoframe_from_display_mode(&frame.avi,
						      &hdmi->connector,
						      mode);

	if (hdmi->hdmi_data.enc_out_format == HDMI_COLORSPACE_YUV444)
		frame.avi.colorspace = HDMI_COLORSPACE_YUV444;
	else if (hdmi->hdmi_data.enc_out_format == HDMI_COLORSPACE_YUV422)
		frame.avi.colorspace = HDMI_COLORSPACE_YUV422;
	else
		frame.avi.colorspace = HDMI_COLORSPACE_RGB;

	return inno_hdmi_upload_frame(hdmi, rc, &frame, INFOFRAME_AVI, 0, 0, 0);
}

static int inno_hdmi_config_video_csc(struct inno_hdmi *hdmi)
{
	struct hdmi_data_info *data = &hdmi->hdmi_data;
	int c0_c2_change = 0;
	int csc_enable = 0;
	int csc_mode = 0;
	int auto_csc = 0;
	int value;
	int i;

	/* Input video mode is SDR RGB24bit, data enable signal from external */
	hdmi_writeb(hdmi, HDMI_VIDEO_CONTRL1, v_DE_EXTERNAL |
		    v_VIDEO_INPUT_FORMAT(VIDEO_INPUT_SDR_RGB444));

	/* Input color hardcode to RGB, and output color hardcode to RGB888 */
	value = v_VIDEO_INPUT_BITS(VIDEO_INPUT_8BITS) |
		v_VIDEO_OUTPUT_COLOR(0) |
		v_VIDEO_INPUT_CSP(0);
	hdmi_writeb(hdmi, HDMI_VIDEO_CONTRL2, value);

	if (data->enc_in_format == data->enc_out_format) {
		if ((data->enc_in_format == HDMI_COLORSPACE_RGB) ||
		    (data->enc_in_format >= HDMI_COLORSPACE_YUV444)) {
			value = v_SOF_DISABLE | v_COLOR_DEPTH_NOT_INDICATED(1);
			hdmi_writeb(hdmi, HDMI_VIDEO_CONTRL3, value);

			hdmi_modb(hdmi, HDMI_VIDEO_CONTRL,
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
	hdmi_writeb(hdmi, HDMI_VIDEO_CONTRL3, value);
	hdmi_modb(hdmi, HDMI_VIDEO_CONTRL, m_VIDEO_AUTO_CSC |
		  m_VIDEO_C0_C2_SWAP, v_VIDEO_AUTO_CSC(auto_csc) |
		  v_VIDEO_C0_C2_SWAP(c0_c2_change));

	return 0;
}

static int inno_hdmi_config_video_timing(struct inno_hdmi *hdmi,
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

	value = mode->hsync_start - mode->hdisplay;
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

	value = mode->vsync_start - mode->vdisplay;
	hdmi_writeb(hdmi, HDMI_VIDEO_EXT_VDELAY, value & 0xFF);

	value = mode->vsync_end - mode->vsync_start;
	hdmi_writeb(hdmi, HDMI_VIDEO_EXT_VDURATION, value & 0xFF);

	return 0;
}

 static const
 struct pre_pll_config *inno_hdmi_phy_get_pre_pll_cfg(struct inno_hdmi *hdmi,
							  unsigned long rate)
 {
	 const struct pre_pll_config *cfg = pre_pll_cfg_table;
	 //unsigned long tmdsclock = 148500000;
	rate = (rate / 1000) * 1000;

	 for (; cfg->pixclock != 0; cfg++)
		 if (cfg->pixclock == rate)
			 break;

	 if (cfg->pixclock == 0)
		 return ERR_PTR(-EINVAL);

	 return cfg;
 }

#define PRE_PLL_POWER_DOWN			BIT(0)

 /* phy tuning values for an undocumented set of registers */
 static const struct phy_config inno_phy_cfg[] = {
	 {	 165000000, {
			 0x07, 0x0a, 0x0a, 0x0a, 0x00, 0x00, 0x08, 0x08, 0x08,
			 0x00, 0xac, 0xcc, 0xcc, 0xcc,
		 },
	 }, {
		 340000000, {
			 0x0b, 0x0d, 0x0d, 0x0d, 0x07, 0x15, 0x08, 0x08, 0x08,
			 0x3f, 0xac, 0xcc, 0xcd, 0xdd,
		 },
	 }, {
		 594000000, {
			 0x10, 0x1a, 0x1a, 0x1a, 0x07, 0x15, 0x08, 0x08, 0x08,
			 0x00, 0xac, 0xcc, 0xcc, 0xcc,
		 },
	 }, { /* sentinel */ },
 };

 static int inno_hdmi_phy_clk_set_rate(struct inno_hdmi *hdmi,unsigned long rate)
 {
 	unsigned long tmdsclock;
	hdmi->post_cfg = post_pll_cfg_table;

	tmdsclock = hdmi->tmds_rate;
	dev_info(hdmi->dev, "%s rate %lu tmdsclk %lu\n",__func__, rate, tmdsclock);

	hdmi->pre_cfg = inno_hdmi_phy_get_pre_pll_cfg(hdmi, tmdsclock);
	if (IS_ERR(hdmi->pre_cfg))
	return PTR_ERR(hdmi->pre_cfg);

	for (; hdmi->post_cfg->tmdsclock != 0; hdmi->post_cfg++)
		if (tmdsclock <= hdmi->post_cfg->tmdsclock && hdmi->post_cfg->version)
			break;

	dev_info(hdmi->dev, "%s hdmi->pre_cfg->pixclock = %lu\n",__func__, hdmi->pre_cfg->pixclock);
	dev_info(hdmi->dev, "%s hdmi->pre_cfg->tmdsclock = %lu\n",__func__, hdmi->pre_cfg->tmdsclock);
	dev_info(hdmi->dev, "%s hdmi->pre_cfg->prediv = %d\n",__func__, hdmi->pre_cfg->prediv);
	dev_info(hdmi->dev, "%s hdmi->pre_cfg->fbdiv = %d\n",__func__, hdmi->pre_cfg->fbdiv);
	dev_info(hdmi->dev, "%s hdmi->pre_cfg->tmds_div_a = %d\n",__func__, hdmi->pre_cfg->tmds_div_a);
	dev_info(hdmi->dev, "%s hdmi->pre_cfg->tmds_div_b = %d\n",__func__, hdmi->pre_cfg->tmds_div_b);
	dev_info(hdmi->dev, "%s hdmi->pre_cfg->tmds_div_c = %d\n",__func__, hdmi->pre_cfg->tmds_div_c);
	dev_info(hdmi->dev, "%s hdmi->pre_cfg->pclk_div_a = %d\n",__func__, hdmi->pre_cfg->pclk_div_a);
	dev_info(hdmi->dev, "%s hdmi->pre_cfg->pclk_div_b = %d\n",__func__, hdmi->pre_cfg->pclk_div_b);
	dev_info(hdmi->dev, "%s hdmi->pre_cfg->pclk_div_c = %d\n",__func__, hdmi->pre_cfg->pclk_div_c);
	dev_info(hdmi->dev, "%s hdmi->pre_cfg->pclk_div_d = %d\n",__func__, hdmi->pre_cfg->pclk_div_d);
	dev_info(hdmi->dev, "%s hdmi->pre_cfg->vco_div_5_en = %d\n",__func__, hdmi->pre_cfg->vco_div_5_en);
	dev_info(hdmi->dev, "%s hdmi->pre_cfg->fracdiv = %d\n",__func__, hdmi->pre_cfg->fracdiv);


	dev_info(hdmi->dev, "*******************************************************\n");

	dev_info(hdmi->dev, "%s hdmi->post_cfg->tmdsclock = %lu\n",__func__, hdmi->post_cfg->tmdsclock);
	dev_info(hdmi->dev, "%s hdmi->post_cfg->prediv = %d\n",__func__, hdmi->post_cfg->prediv);
	dev_info(hdmi->dev, "%s hdmi->post_cfg->fbdiv = %d\n",__func__, hdmi->post_cfg->fbdiv);
	dev_info(hdmi->dev, "%s hdmi->post_cfg->postdiv = %d\n",__func__, hdmi->post_cfg->postdiv);
	dev_info(hdmi->dev, "%s hdmi->post_cfg->post_div_en = %d\n",__func__, hdmi->post_cfg->post_div_en);
	dev_info(hdmi->dev, "%s hdmi->post_cfg->version = %d\n",__func__, hdmi->post_cfg->version);

	inno_hdmi_config_pll(hdmi);
	//1920x1080p60
	//inno_hdmi_tx_ctrl(hdmi);

#if 0 //pre pll + post pll configire

	/*pre-pll power down*/
	hdmi_modb(hdmi, 0x1a0, INNO_PRE_PLL_POWER_DOWN, INNO_PRE_PLL_POWER_DOWN);

	/* Configure pre-pll */
	hdmi_modb(hdmi, 0x1a0, INNO_PCLK_VCO_DIV_5_MASK, INNO_PCLK_VCO_DIV_5(hdmi->pre_cfg->vco_div_5_en));
	hdmi_writeb(hdmi, 0x1a1, INNO_PRE_PLL_PRE_DIV(hdmi->pre_cfg->prediv));

	u32 val;
	val = INNO_SPREAD_SPECTRUM_MOD_DISABLE;
	if (!hdmi->pre_cfg->fracdiv)
		val |= INNO_PRE_PLL_FRAC_DIV_DISABLE;
	hdmi_writeb(hdmi, 0x1a2, INNO_PRE_PLL_FB_DIV_11_8(hdmi->pre_cfg->fbdiv | val));

	hdmi_writeb(hdmi, 0x1a3, INNO_PRE_PLL_FB_DIV_7_0(hdmi->pre_cfg->fbdiv));

	hdmi_writeb(hdmi, 0x1a5, INNO_PRE_PLL_PCLK_DIV_A(hdmi->pre_cfg->pclk_div_a) |
			INNO_PRE_PLL_PCLK_DIV_B(hdmi->pre_cfg->pclk_div_b));

	hdmi_writeb(hdmi, 0x1a6, INNO_PRE_PLL_PCLK_DIV_C(hdmi->pre_cfg->pclk_div_c) |
			INNO_PRE_PLL_PCLK_DIV_D(hdmi->pre_cfg->pclk_div_d));

	hdmi_writeb(hdmi, 0x1a4, INNO_PRE_PLL_TMDSCLK_DIV_C(hdmi->pre_cfg->tmds_div_c) |
			INNO_PRE_PLL_TMDSCLK_DIV_A(hdmi->pre_cfg->tmds_div_a) |
			INNO_PRE_PLL_TMDSCLK_DIV_B(hdmi->pre_cfg->tmds_div_b));

	hdmi_writeb(hdmi, 0x1d3, INNO_PRE_PLL_FRAC_DIV_7_0(hdmi->pre_cfg->fracdiv));
	hdmi_writeb(hdmi, 0x1d2, INNO_PRE_PLL_FRAC_DIV_15_8(hdmi->pre_cfg->fracdiv));
	hdmi_writeb(hdmi, 0x1d1, INNO_PRE_PLL_FRAC_DIV_23_16(hdmi->pre_cfg->fracdiv));

	/*pre-pll power down*/
	hdmi_modb(hdmi, 0x1a0, INNO_PRE_PLL_POWER_DOWN, 0);

	const struct phy_config *phy_cfg = inno_phy_cfg;

	for (; phy_cfg->tmdsclock != 0; phy_cfg++)
		if (tmdsclock <= phy_cfg->tmdsclock)
			break;

	hdmi_modb(hdmi, 0x1aa, INNO_POST_PLL_POWER_DOWN, INNO_POST_PLL_POWER_DOWN);

	hdmi_writeb(hdmi, 0x1ac, INNO_POST_PLL_FB_DIV_7_0(hdmi->post_cfg->fbdiv));

	if (hdmi->post_cfg->postdiv == 1) {
		hdmi_modb(hdmi, 0x1aa, INNO_POST_PLL_REFCLK_SEL_TMDS, INNO_POST_PLL_REFCLK_SEL_TMDS);
		hdmi_modb(hdmi, 0x1aa, BIT(4), INNO_POST_PLL_FB_DIV_8(hdmi->post_cfg->fbdiv));
		hdmi_modb(hdmi, 0x1ab, INNO_POST_PLL_Pre_DIV_MASK, INNO_POST_PLL_PRE_DIV(hdmi->post_cfg->prediv));
	} else {
		v = (hdmi->post_cfg->postdiv / 2) - 1;
		v &= INNO_POST_PLL_POST_DIV_MASK;
		hdmi_modb(hdmi, 0x1ad, INNO_POST_PLL_POST_DIV_MASK, v);
		hdmi_modb(hdmi, 0x1aa, BIT(4), INNO_POST_PLL_FB_DIV_8(hdmi->post_cfg->fbdiv));
		hdmi_modb(hdmi, 0x1ab, INNO_POST_PLL_Pre_DIV_MASK, INNO_POST_PLL_PRE_DIV(hdmi->post_cfg->prediv));
		hdmi_modb(hdmi, 0x1aa, INNO_POST_PLL_REFCLK_SEL_TMDS, INNO_POST_PLL_REFCLK_SEL_TMDS);
		hdmi_modb(hdmi, 0x1aa, INNO_POST_PLL_POST_DIV_ENABLE, INNO_POST_PLL_POST_DIV_ENABLE);
	}

	for (v = 0; v < 14; v++){
		hdmi_writeb(hdmi, 0x1b5 + v, phy_cfg->regs[v]);
	}

	if (phy_cfg->tmdsclock > 340000000) {
		/* Set termination resistor to 100ohm */
		v = clk_get_rate(hdmi->sys_clk) / 100000;
	
		hdmi_writeb(hdmi, 0x1c5, INNO_TERM_RESISTOR_CALIB_SPEED_14_8(v)
			   | INNO_BYPASS_TERM_RESISTOR_CALIB);
	
		hdmi_writeb(hdmi, 0x1c6, INNO_TERM_RESISTOR_CALIB_SPEED_7_0(v));
		hdmi_writeb(hdmi, 0x1c7, INNO_TERM_RESISTOR_100);		
		hdmi_modb(hdmi, 0x1c5, INNO_BYPASS_TERM_RESISTOR_CALIB, 0);
	} else {
		hdmi_writeb(hdmi, 0x1c5, INNO_BYPASS_TERM_RESISTOR_CALIB);

		/* clk termination resistor is 50ohm (parallel resistors) */
		if (phy_cfg->tmdsclock > 165000000){
			hdmi_modb(hdmi, 0x1c8,
					 INNO_ESD_DETECT_MASK,
					 INNO_TERM_RESISTOR_200);
		}
		/* data termination resistor for D2, D1 and D0 is 150ohm */
		for (v = 0; v < 3; v++){
			hdmi_modb(hdmi, 0x1c9 + v,
					 INNO_ESD_DETECT_MASK,
					 INNO_TERM_RESISTOR_200);
		}
	}

	hdmi_modb(hdmi, 0x1aa, INNO_POST_PLL_POWER_DOWN, 0);


#endif
	 return 0;
 }

static int inno_hdmi_setup(struct inno_hdmi *hdmi,
			   struct drm_display_mode *mode)
{
	u8 val;
	int value;
	hdmi->hdmi_data.vic = drm_match_cea_mode(mode);

	val = readl_relaxed(hdmi->regs + (0x1b0) * 0x04);
	val |= 0x4;
	writel_relaxed(val, hdmi->regs + (0x1b0) * 0x04);
	writel_relaxed(0xf, hdmi->regs + (0x1cc) * 0x04);

	/*turn on pre-PLL*/
	val = readl_relaxed(hdmi->regs + (0x1a0) * 0x04);
	val &= ~(0x1);
	writel_relaxed(val, hdmi->regs + (0x1a0) * 0x04);
	/*turn on post-PLL*/
	val = readl_relaxed(hdmi->regs + (0x1aa) * 0x04);
	val &= ~(0x1);
	writel_relaxed(val, hdmi->regs + (0x1aa) * 0x04);

	hdmi->tmds_rate = mode->clock * 1000;
	inno_hdmi_phy_clk_set_rate(hdmi,hdmi->tmds_rate);

	while (!(readl_relaxed(hdmi->regs + (0x1a9) * 0x04) & 0x1))
	;
	while (!(readl_relaxed(hdmi->regs + (0x1af) * 0x04) & 0x1))
	;

	/*turn on LDO*/
	writel_relaxed(0x7, hdmi->regs + (0x1b4) * 0x04);
	/*turn on serializer*/
	writel_relaxed(0x70, hdmi->regs + (0x1be) * 0x04);
	inno_hdmi_tx_phy_power_down(hdmi);
	/* Set HDMI Mode */
	hdmi_writeb(hdmi, 0x100,0x3);
	//hdmi_writeb(hdmi, 0x8,0x00);

	/* Set detail external video timing polarity and interlace mode */
	value = v_EXTERANL_VIDEO(0);
	value |= mode->flags & DRM_MODE_FLAG_PHSYNC ?
		 v_HSYNC_POLARITY(1) : v_HSYNC_POLARITY(0);
	value |= mode->flags & DRM_MODE_FLAG_PVSYNC ?
		 v_VSYNC_POLARITY(1) : v_VSYNC_POLARITY(0);
	value |= mode->flags & DRM_MODE_FLAG_INTERLACE ?
		 v_INETLACE(1) : v_INETLACE(0);
	hdmi_writeb(hdmi, HDMI_VIDEO_TIMING_CTL, value);

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

	/* Mute audio output */
	hdmi_modb(hdmi, HDMI_AV_MUTE, m_AUDIO_MUTE ,
		  v_AUDIO_MUTE(1));


    if(0 == hdmi->hdmi_data.vic)
	{
		inno_hdmi_config_video_timing(hdmi, mode);

		inno_hdmi_config_video_csc(hdmi);
    }

	if (hdmi->hdmi_data.sink_is_hdmi) {
		inno_hdmi_config_video_avi(hdmi, mode);
		inno_hdmi_config_video_vsi(hdmi, mode);
	}

	inno_hdmi_tx_phy_power_on(hdmi);
	inno_hdmi_tmds_driver_on(hdmi);

	writel_relaxed(0x0, hdmi->regs + (0xce) * 0x04);
	writel_relaxed(0x1, hdmi->regs + (0xce) * 0x04);

	return 0;
}

static void inno_hdmi_encoder_mode_set(struct drm_encoder *encoder,
				       struct drm_display_mode *mode,
				       struct drm_display_mode *adj_mode)
{
	struct inno_hdmi *hdmi = to_inno_hdmi(encoder);

	inno_hdmi_setup(hdmi, adj_mode);

	/* Store the display mode for plugin/DPMS poweron events */
	memcpy(&hdmi->previous_mode, adj_mode, sizeof(hdmi->previous_mode));
}

static void inno_hdmi_encoder_enable(struct drm_encoder *encoder)
{
	struct inno_hdmi *hdmi = to_inno_hdmi(encoder);
	//inno_hdmi_init(hdmi);

	inno_hdmi_set_pwr_mode(hdmi, NORMAL);
}

static void inno_hdmi_encoder_disable(struct drm_encoder *encoder)
{
	struct inno_hdmi *hdmi = to_inno_hdmi(encoder);

	hdmi_modb(hdmi, 0x1b2, 0xf, 0);
	hdmi_modb(hdmi, 0x1be, 0xf, 0);
	hdmi_modb(hdmi, 0x1b4, 0xf, 0);
	hdmi_modb(hdmi, 0x1a0, 1, 1);
	hdmi_modb(hdmi, 0x1aa, 1, 1);
	hdmi_modb(hdmi, 0x1cc, 0x0f, 0);
	hdmi_modb(hdmi, 0x1b0, 1<<2, 0);

	inno_hdmi_set_pwr_mode(hdmi, LOWER_PWR);
}

static bool inno_hdmi_encoder_mode_fixup(struct drm_encoder *encoder,
					 const struct drm_display_mode *mode,
					 struct drm_display_mode *adj_mode)
{
	return true;
}

static int
inno_hdmi_encoder_atomic_check(struct drm_encoder *encoder,
			       struct drm_crtc_state *crtc_state,
			       struct drm_connector_state *conn_state)
{
	//struct rockchip_crtc_state *s = to_rockchip_crtc_state(crtc_state);

	//s->output_mode = ROCKCHIP_OUT_MODE_P888;
	//s->output_type = DRM_MODE_CONNECTOR_HDMIA;

	return 0;
}

static const struct drm_encoder_helper_funcs inno_hdmi_encoder_helper_funcs = {
	.enable     = inno_hdmi_encoder_enable,
	.disable    = inno_hdmi_encoder_disable,
	.mode_fixup = inno_hdmi_encoder_mode_fixup,
	.mode_set   = inno_hdmi_encoder_mode_set,
	.atomic_check = inno_hdmi_encoder_atomic_check,
};

static enum drm_connector_status
inno_hdmi_connector_detect(struct drm_connector *connector, bool force)
{
	struct inno_hdmi *hdmi = to_inno_hdmi(connector);

	return (hdmi_readb(hdmi, HDMI_STATUS) & m_HOTPLUG) ?
		connector_status_connected : connector_status_disconnected;
}

static int inno_hdmi_connector_get_modes(struct drm_connector *connector)
{
	struct inno_hdmi *hdmi = to_inno_hdmi(connector);
	struct edid *edid;
	int ret = 0;

	if (!hdmi->ddc)
		return 0;

	edid = drm_get_edid(connector, hdmi->ddc);
	if (edid) {
		hdmi->hdmi_data.sink_is_hdmi = drm_detect_hdmi_monitor(edid);
		hdmi->hdmi_data.sink_has_audio = drm_detect_monitor_audio(edid);
		drm_connector_update_edid_property(connector, edid);
		ret = drm_add_edid_modes(connector, edid);
		kfree(edid);
	}

	return ret;
}

static const struct dw_hdmi_mpll_config starfive_mpll_cfg[] = {
	{
		25200000, {
			{ 0x00b3, 0x0000},
			{ 0x2153, 0x0000},
			{ 0x40f3, 0x0000}
		},
	}, {
		27000000, {
			{ 0x00b3, 0x0000},
			{ 0x2153, 0x0000},
			{ 0x40f3, 0x0000}
		},
	}, {
		36000000, {
			{ 0x00b3, 0x0000},
			{ 0x2153, 0x0000},
			{ 0x40f3, 0x0000}
		},
	}, {
		40000000, {
			{ 0x00b3, 0x0000},
			{ 0x2153, 0x0000},
			{ 0x40f3, 0x0000}
		},
	}, {
		54000000, {
			{ 0x0072, 0x0001},
			{ 0x2142, 0x0001},
			{ 0x40a2, 0x0001},
		},
	}, {
		65000000, {
			{ 0x0072, 0x0001},
			{ 0x2142, 0x0001},
			{ 0x40a2, 0x0001},
		},
	}, {
		66000000, {
			{ 0x013e, 0x0003},
			{ 0x217e, 0x0002},
			{ 0x4061, 0x0002}
		},
	}, {
		74250000, {
			{ 0x0072, 0x0001},
			{ 0x2145, 0x0002},
			{ 0x4061, 0x0002}
		},
	}, {
		83500000, {
			{ 0x0072, 0x0001},
		},
	}, {
		108000000, {
			{ 0x0051, 0x0002},
			{ 0x2145, 0x0002},
			{ 0x4061, 0x0002}
		},
	}, {
		106500000, {
			{ 0x0051, 0x0002},
			{ 0x2145, 0x0002},
			{ 0x4061, 0x0002}
		},
	}, {
		146250000, {
			{ 0x0051, 0x0002},
			{ 0x2145, 0x0002},
			{ 0x4061, 0x0002}
		},
	}, {
		148500000, {
			{ 0x0051, 0x0003},
			{ 0x214c, 0x0003},
			{ 0x4064, 0x0003}
		},
	}, {
		~0UL, {
			{ 0x00a0, 0x000a },
			{ 0x2001, 0x000f },
			{ 0x4002, 0x000f },
		},
	}
};

static enum drm_mode_status
inno_hdmi_connector_mode_valid(struct drm_connector *connector,
			       struct drm_display_mode *mode)
{
#if 0
	const struct dw_hdmi_mpll_config *mpll_cfg = starfive_mpll_cfg;
	int pclk = mode->clock * 1000;
	bool valid = false;
	int i;

	for (i = 0; mpll_cfg[i].mpixelclock != (~0UL); i++) {
		if (pclk == mpll_cfg[i].mpixelclock) {
			valid = true;
			break;
		}
	}

	return (valid) ? MODE_OK : MODE_BAD;
#endif
	u32 vic = drm_match_cea_mode(mode);

	if (vic >= 1)
		return MODE_OK;
	else
		return MODE_BAD;

	//return MODE_OK;
}

static int
inno_hdmi_probe_single_connector_modes(struct drm_connector *connector,
				       uint32_t maxX, uint32_t maxY)
{
	return drm_helper_probe_single_connector_modes(connector, 1920, 1080);
}

static void inno_hdmi_connector_destroy(struct drm_connector *connector)
{
	drm_connector_unregister(connector);
	drm_connector_cleanup(connector);
}

static const struct drm_connector_funcs inno_hdmi_connector_funcs = {
	.fill_modes = inno_hdmi_probe_single_connector_modes,
	.detect = inno_hdmi_connector_detect,
	.destroy = inno_hdmi_connector_destroy,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static struct drm_connector_helper_funcs inno_hdmi_connector_helper_funcs = {
	.get_modes = inno_hdmi_connector_get_modes,
	.mode_valid = inno_hdmi_connector_mode_valid,
};

static int inno_hdmi_register(struct drm_device *drm, struct inno_hdmi *hdmi)
{
	struct drm_encoder *encoder = &hdmi->encoder;
	struct device *dev = hdmi->dev;

	encoder->possible_crtcs = drm_of_find_possible_crtcs(drm, dev->of_node);

	/*
	 * If we failed to find the CRTC(s) which this encoder is
	 * supposed to be connected to, it's because the CRTC has
	 * not been registered yet.  Defer probing, and hope that
	 * the required CRTC is added later.
	 */
	if (encoder->possible_crtcs == 0)
		return -EPROBE_DEFER;

	drm_encoder_helper_add(encoder, &inno_hdmi_encoder_helper_funcs);
	drm_simple_encoder_init(drm, encoder, DRM_MODE_ENCODER_TMDS);

	hdmi->connector.polled = DRM_CONNECTOR_POLL_HPD;

	drm_connector_helper_add(&hdmi->connector,
				 &inno_hdmi_connector_helper_funcs);
	drm_connector_init_with_ddc(drm, &hdmi->connector,
				    &inno_hdmi_connector_funcs,
				    DRM_MODE_CONNECTOR_HDMIA,
				    hdmi->ddc);

	drm_connector_attach_encoder(&hdmi->connector, encoder);

	return 0;
}

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

	drm_helper_hpd_irq_event(hdmi->connector.dev);

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
	if ((msgs->len != 1) ||
	    ((msgs->addr != DDC_ADDR) && (msgs->addr != DDC_SEGMENT_ADDR)))
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
	adap->class = I2C_CLASS_DDC;
	adap->owner = THIS_MODULE;
	adap->dev.parent = hdmi->dev;
	adap->algo = &inno_hdmi_algorithm;
	strlcpy(adap->name, "Inno HDMI", sizeof(adap->name));
	i2c_set_adapdata(adap, hdmi);

	ret = i2c_add_adapter(adap);
	if (ret) {
		dev_warn(hdmi->dev, "cannot add %s I2C adapter\n", adap->name);
		devm_kfree(hdmi->dev, i2c);
		return ERR_PTR(ret);
	}

	hdmi->i2c = i2c;

	DRM_DEV_INFO(hdmi->dev, "registered %s I2C bus driver success\n", adap->name);

	return adap;
}




#if 0
static void inno_hdmi_get_edid(struct inno_hdmi *hdmi, unsigned int tmds_clk, unsigned char *data)
{
	unsigned int i, ddc_div_msb, ddc_div_lsb;
	unsigned int ddc_scl = 100000; //scl:100k

	//hdmi_write(0xff, 0x0c2);
	//hdmi_write(0xff, 0x0c4);

	ddc_div_lsb = (tmds_clk/(4*ddc_scl))>>2;
	ddc_div_msb = (tmds_clk/(4*ddc_scl)) & 0x00ff;
	hdmi_writeb(hdmi, 0x04b,ddc_div_lsb);
	hdmi_writeb(hdmi, 0x04c,ddc_div_msb);
	/*enable EDID ready interrupt*/
	hdmi_writeb(hdmi,0x0c0,0x04 );

	/*read e-edid segment 0x00 256 bytes steps*/
	/*set EDID FIFO initial address*/
	hdmi_writeb(hdmi,0x04f, 0x00);
	/*set EDID first word address, read first 128 byte*/
	hdmi_writeb(hdmi,0x04e, 0x00);
	/*set EDID segment 0x00 address*/
	hdmi_writeb(hdmi, 0x04d,0x00);

	while(!(hdmi_readb(hdmi,0x0c1) & 0x4));
	hdmi_writeb(hdmi,0x0c1, 0x04); // clear ready interrupt: write 1 to bit2

	/*read first 128 bytes*/
	for(i = 0; i < 128; i++)
	{
		data[i] = hdmi_readb(hdmi,0x050);
	}

	/*set EDID FIFO initial address again*/
	hdmi_writeb(hdmi,0x04f, 0x00);
	/*set EDID first word address, read last 128 byte*/
	hdmi_writeb(hdmi,0x04e, 0x80);
	/*set EDID segment 0x00 address*/
	hdmi_writeb(hdmi,0x04d, 0x00);

	while(!(hdmi_readb(hdmi,0x0c1) & 0x4));
	hdmi_writeb(hdmi,0x0c1, 0x04); // clear ready interrupt: write 1 to bit2

	/*read last 128 bytes*/
	for(i = 128; i < 256; i++)
	{
		data[i] = hdmi_readb(hdmi,0x050);
	}

}
#endif

static int inno_hdmi_get_clk_rst(struct device *dev, struct inno_hdmi *hdmi)
{
	int ret;
	hdmi->sys_clk = devm_clk_get(dev, "sysclk");
	if (IS_ERR(hdmi->sys_clk)) {
		DRM_DEV_ERROR(dev, "Unable to get HDMI sysclk clk\n");
		return PTR_ERR(hdmi->sys_clk);
	}
	hdmi->mclk = devm_clk_get(dev, "mclk");
	if (IS_ERR(hdmi->mclk)) {
		DRM_DEV_ERROR(dev, "Unable to get HDMI mclk clk\n");
		return PTR_ERR(hdmi->mclk);
	}
	hdmi->bclk = devm_clk_get(dev, "bclk");
	if (IS_ERR(hdmi->bclk)) {
		DRM_DEV_ERROR(dev, "Unable to get HDMI bclk clk\n");
		return PTR_ERR(hdmi->bclk);
	}
	hdmi->tx_rst = reset_control_get_exclusive(dev, "hdmi_tx");
	if (IS_ERR(hdmi->tx_rst)) {
		DRM_DEV_ERROR(dev, "Unable to get HDMI tx rst\n");
		return PTR_ERR(hdmi->tx_rst);
	}
	return ret;
}

static int inno_hdmi_en_clk_deas_rst(struct device *dev, struct inno_hdmi *hdmi)
{
	int ret;

	ret = clk_prepare_enable(hdmi->sys_clk);
	if (ret) {
		DRM_DEV_ERROR(dev,
			      "Cannot enable HDMI sys clock: %d\n", ret);
		return ret;
	}
	ret = clk_prepare_enable(hdmi->mclk);
	if (ret) {
		DRM_DEV_ERROR(dev,
			      "Cannot enable HDMI mclk clock: %d\n", ret);
		return ret;
	}
	ret = clk_prepare_enable(hdmi->bclk);
	if (ret) {
		DRM_DEV_ERROR(dev,
			      "Cannot enable HDMI bclk clock: %d\n", ret);
		return ret;
	}

	ret = reset_control_deassert(hdmi->tx_rst);
	if (ret < 0) {
		dev_err(dev, "failed to deassert tx_rst\n");
		return ret;
    }
	return ret;
}


static int inno_hdmi_bind(struct device *dev, struct device *master,
				 void *data)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct drm_device *drm = data;
	struct inno_hdmi *hdmi;
	struct resource *iores;
	int irq;
	int ret;

	hdmi = devm_kzalloc(dev, sizeof(*hdmi), GFP_KERNEL);
	if (!hdmi)
		return -ENOMEM;

	hdmi->dev = dev;
	hdmi->drm_dev = drm;

	iores = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	hdmi->regs = devm_ioremap_resource(dev, iores);
	if (IS_ERR(hdmi->regs))
		return PTR_ERR(hdmi->regs);

	hdmi->hdmi_1p8 = devm_regulator_get(dev, "hdmi_1p8");
	if (IS_ERR(hdmi->hdmi_1p8))
		return PTR_ERR(hdmi->hdmi_1p8);

	hdmi->hdmi_0p9 = devm_regulator_get(dev, "hdmi_0p9");
	if (IS_ERR(hdmi->hdmi_0p9))
		return PTR_ERR(hdmi->hdmi_0p9);

	//pmic turn on
	ret = regulator_enable(hdmi->hdmi_1p8);
	if (ret) {
		dev_err(dev, "Cannot enable hdmi_1p8 regulator\n");
		goto err_reg_1p8;
	}
	udelay(100);
	ret = regulator_enable(hdmi->hdmi_0p9);
	if (ret) {
		dev_err(dev, "Cannot enable hdmi_0p9 regulator\n");
		goto err_reg_0p9;
	}
	udelay(100);

	ret = inno_hdmi_get_clk_rst(dev, hdmi);
	ret = inno_hdmi_en_clk_deas_rst(dev, hdmi);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		ret = irq;
		goto err_disable_clk;
	}
#ifdef CONFIG_DRM_I2C_NXP_TDA998X
	inno_hdmi_init(hdmi);
#endif
	inno_hdmi_reset(hdmi);

	hdmi->ddc = inno_hdmi_i2c_adapter(hdmi);
	if (IS_ERR(hdmi->ddc)) {
		ret = PTR_ERR(hdmi->ddc);
		hdmi->ddc = NULL;
		goto err_disable_clk;
	}

	hdmi->tmds_rate = 51200000;

	inno_hdmi_i2c_init(hdmi);

	ret = inno_hdmi_register(drm, hdmi);
	if (ret)
		goto err_put_adapter;

	dev_set_drvdata(dev, hdmi);

	/* Unmute hotplug interrupt */
	hdmi_modb(hdmi, HDMI_STATUS, m_MASK_INT_HOTPLUG, v_MASK_INT_HOTPLUG(1));

	ret = devm_request_threaded_irq(dev, irq, inno_hdmi_hardirq,
					inno_hdmi_irq, IRQF_SHARED,
					dev_name(dev), hdmi);
	if (ret < 0)
		goto err_cleanup_hdmi;

	return 0;
err_cleanup_hdmi:
	hdmi->connector.funcs->destroy(&hdmi->connector);
	hdmi->encoder.funcs->destroy(&hdmi->encoder);
err_put_adapter:
	i2c_put_adapter(hdmi->ddc);
err_disable_clk:
	//clk_disable_unprepare(hdmi->pclk);
err_reg_0p9:
	regulator_disable(hdmi->hdmi_1p8);
err_reg_1p8:
	return ret;
}

static void inno_hdmi_unbind(struct device *dev, struct device *master,
			     void *data)
{
	struct inno_hdmi *hdmi = dev_get_drvdata(dev);
	int ret;

	hdmi->connector.funcs->destroy(&hdmi->connector);
	hdmi->encoder.funcs->destroy(&hdmi->encoder);

	i2c_put_adapter(hdmi->ddc);

	ret = reset_control_assert(hdmi->tx_rst);
	if (ret < 0)
		dev_err(dev, "failed to assert tx_rst\n");

	clk_disable_unprepare(hdmi->sys_clk);
	clk_disable_unprepare(hdmi->mclk);
	clk_disable_unprepare(hdmi->bclk);

	regulator_disable(hdmi->hdmi_1p8);
	udelay(100);
	regulator_disable(hdmi->hdmi_0p9);
}

static const struct component_ops inno_hdmi_ops = {
	.bind	= inno_hdmi_bind,
	.unbind	= inno_hdmi_unbind,
};

static int inno_hdmi_probe(struct platform_device *pdev)
{
	return component_add(&pdev->dev, &inno_hdmi_ops);
}

static int inno_hdmi_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &inno_hdmi_ops);

	return 0;
}

static const struct of_device_id inno_hdmi_dt_ids[] = {
	{ .compatible = "rockchip,rk3036-inno-hdmi",
	},
	{},
};
MODULE_DEVICE_TABLE(of, inno_hdmi_dt_ids);

struct platform_driver inno_hdmi_driver = {
	.probe  = inno_hdmi_probe,
	.remove = inno_hdmi_remove,
	.driver = {
		.name = "innohdmi-rockchip",
		.of_match_table = inno_hdmi_dt_ids,
	},
};
