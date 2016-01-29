#include <linux/delay.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/of_irq.h>
#include "rockchip_hdmiv1.h"
#include "rockchip_hdmiv1_hw.h"
#include "rockchip_hdmiv1_hdcp.h"

static inline void delay100us(void)
{
	usleep_range(99, 100);
}

static void rockchip_hdmiv1_av_mute(struct hdmi *hdmi_drv, bool enable)
{
	struct hdmi_dev *hdmi_dev = hdmi_drv->property->priv;

	if (enable) {
		hdmi_msk_reg(hdmi_dev, AV_MUTE,
			     m_AVMUTE_CLEAR | m_AVMUTE_ENABLE,
			     v_AVMUTE_CLEAR(0) | v_AVMUTE_ENABLE(1));
	} else {
		hdmi_msk_reg(hdmi_dev, AV_MUTE,
			     m_AVMUTE_CLEAR | m_AVMUTE_ENABLE,
			     v_AVMUTE_CLEAR(1) | v_AVMUTE_ENABLE(0));
	}
	hdmi_msk_reg(hdmi_dev, PACKET_SEND_AUTO,
		     m_PACKET_GCP_EN, v_PACKET_GCP_EN(1));
}

static void rockchip_hdmiv1_sys_power(struct hdmi *hdmi_drv, bool enable)
{
	struct hdmi_dev *hdmi_dev = hdmi_drv->property->priv;

	if (enable)
		hdmi_msk_reg(hdmi_dev, SYS_CTRL, m_POWER, v_PWR_ON);
	else
		hdmi_msk_reg(hdmi_dev, SYS_CTRL, m_POWER, v_PWR_OFF);
}

static void rockchip_hdmiv1_set_pwr_mode(struct hdmi *hdmi_drv, int mode)
{
	struct hdmi_dev *hdmi_dev = hdmi_drv->property->priv;

	if (hdmi_dev->pwr_mode == mode)
		return;

	dev_info(hdmi_drv->dev, "%s change pwr_mode %d --> %d\n", __func__,
		 hdmi_dev->pwr_mode, mode);

	switch (mode) {
	case NORMAL:
		dev_info(hdmi_drv->dev,
			 "%s change pwr_mode NORMAL\n",
			 __func__);
		rockchip_hdmiv1_sys_power(hdmi_drv, false);
		if (hdmi_dev->soctype == HDMI_SOC_RK3036) {
			hdmi_writel(hdmi_dev, PHY_PRE_EMPHASIS, 0x6f);
			hdmi_writel(hdmi_dev, PHY_DRIVER, 0xbb);
		} else if (hdmi_dev->soctype == HDMI_SOC_RK312X) {
			hdmi_writel(hdmi_dev, PHY_PRE_EMPHASIS, 0x5f);
			hdmi_writel(hdmi_dev, PHY_DRIVER, 0xaa);
		}

		hdmi_writel(hdmi_dev, PHY_SYS_CTL, 0x15);
		hdmi_writel(hdmi_dev, PHY_SYS_CTL, 0x14);
		hdmi_writel(hdmi_dev, PHY_SYS_CTL, 0x10);
		hdmi_writel(hdmi_dev, PHY_CHG_PWR, 0x0f);
		hdmi_writel(hdmi_dev, 0xce, 0x00);
		hdmi_writel(hdmi_dev, 0xce, 0x01);
		rockchip_hdmiv1_sys_power(hdmi_drv, true);
		break;
	case LOWER_PWR:
		dev_info(hdmi_drv->dev,
			 "%s change pwr_mode LOWER_PWR\n",
			 __func__);
		rockchip_hdmiv1_sys_power(hdmi_drv, false);
		hdmi_writel(hdmi_dev, PHY_DRIVER, 0x00);
		hdmi_writel(hdmi_dev, PHY_PRE_EMPHASIS, 0x00);
		hdmi_writel(hdmi_dev, PHY_CHG_PWR, 0x00);
		hdmi_writel(hdmi_dev, PHY_SYS_CTL, 0x17);
		break;
	default:
		dev_info(hdmi_drv->dev, "unkown rk3036 hdmi pwr mode %d\n",
			 mode);
	}

	hdmi_dev->pwr_mode = mode;
}

int rockchip_hdmiv1_detect_hotplug(struct hdmi *hdmi_drv)
{
	int value = 0;
	struct hdmi_dev *hdmi_dev = hdmi_drv->property->priv;

	hdmi_readl(hdmi_dev, HDMI_STATUS, &value);
	value &= m_HOTPLUG;
	if (value == m_HOTPLUG)
		return HDMI_HPD_ACTIVED;
	else if (value)
		return HDMI_HPD_INSERT;
	else
		return HDMI_HPD_REMOVED;
}

int rockchip_hdmiv1_insert(struct hdmi *hdmi_drv)
{
	rockchip_hdmiv1_set_pwr_mode(hdmi_drv, NORMAL);
	return 0;
}


int rockchip_hdmiv1_read_edid(struct hdmi *hdmi_drv, int block, u8 *buf)
{
	u32 c = 0;
	u8 segment = 0;
	u8 offset = 0;
	int ret = -1;
	int i, j;
	int ddc_bus_freq;
	int trytime;
	int checksum = 0;
	struct hdmi_dev *hdmi_dev = hdmi_drv->property->priv;

	if (block % 2)
		offset = HDMI_EDID_BLOCK_SIZE;

	if (block / 2)
		segment = 1;
	ddc_bus_freq = (hdmi_dev->hclk_rate >> 2) / HDMI_SCL_RATE;
	hdmi_writel(hdmi_dev, DDC_BUS_FREQ_L, ddc_bus_freq & 0xFF);
	hdmi_writel(hdmi_dev, DDC_BUS_FREQ_H, (ddc_bus_freq >> 8) & 0xFF);

	dev_info(hdmi_drv->dev,
		 "EDID DATA (Segment = %d Block = %d Offset = %d):\n",
		 (int)segment, (int)block, (int)offset);
	disable_irq(hdmi_dev->irq);

	/* Enable edid interrupt */
	hdmi_writel(hdmi_dev, INTERRUPT_MASK1, m_INT_EDID_READY);

	for (trytime = 0; trytime < 10; trytime++) {
		checksum = 0;
		hdmi_writel(hdmi_dev, INTERRUPT_STATUS1, 0x04);

		/* Set edid fifo first addr */
		hdmi_writel(hdmi_dev, EDID_FIFO_OFFSET, 0x00);

		/* Set edid word address 0x00/0x80 */
		hdmi_writel(hdmi_dev, EDID_WORD_ADDR, offset);

		/* Set edid segment pointer */
		hdmi_writel(hdmi_dev, EDID_SEGMENT_POINTER, segment);

		for (i = 0; i < 200; i++) {
			/* Wait edid interrupt */
			usleep_range(900, 1000);
			c = 0x00;
			hdmi_readl(hdmi_dev, INTERRUPT_STATUS1, &c);

			if (c & m_INT_EDID_READY)
				break;
		}

		if (c & m_INT_EDID_READY) {
			for (j = 0; j < HDMI_EDID_BLOCK_SIZE; j++) {
				c = 0;
				hdmi_readl(hdmi_dev, 0x50, &c);
				buf[j] = c;
				checksum += c;
			}

			if ((checksum & 0xff) == 0) {
				ret = 0;
				dev_info(hdmi_drv->dev,
					 "[%s] edid read sucess\n", __func__);
				break;
			}
		}
	}
	/*close edid irq*/
	hdmi_writel(hdmi_dev, INTERRUPT_MASK1, 0);
	/* clear EDID interrupt reg */
	hdmi_writel(hdmi_dev, INTERRUPT_STATUS1,
		    m_INT_EDID_READY);

	enable_irq(hdmi_dev->irq);

	return ret;
}

static const char coeff_csc[][24] = {
	/* YUV2RGB:601 SD mode(Y[16:235],UV[16:240],RGB[0:255]):
	 *  R = 1.164*Y +1.596*V - 204
	 *  G = 1.164*Y - 0.391*U - 0.813*V + 154
	 *  B = 1.164*Y + 2.018*U - 258
	 */
	{
	0x04, 0xa7, 0x00, 0x00, 0x06, 0x62, 0x02, 0xcc,
	0x04, 0xa7, 0x11, 0x90, 0x13, 0x40, 0x00, 0x9a,
	0x04, 0xa7, 0x08, 0x12, 0x00, 0x00, 0x03, 0x02},

	/* YUV2RGB:601 SD mode(YUV[0:255],RGB[0:255]):
	 *  R = Y + 1.402*V - 248
	 *  G = Y - 0.344*U - 0.714*V + 135
	 *  B = Y + 1.772*U - 227
	 */
	{
	0x04, 0x00, 0x00, 0x00, 0x05, 0x9b, 0x02, 0xf8,
	0x04, 0x00, 0x11, 0x60, 0x12, 0xdb, 0x00, 0x87,
	0x04, 0x00, 0x07, 0x16, 0x00, 0x00, 0x02, 0xe3},
	/* YUV2RGB:709 HD mode(Y[16:235],UV[16:240],RGB[0:255]):
	 *  R = 1.164*Y +1.793*V - 248
	 *  G = 1.164*Y - 0.213*U - 0.534*V + 77
	 *  B = 1.164*Y + 2.115*U - 289
	 */
	{
	0x04, 0xa7, 0x00, 0x00, 0x07, 0x2c, 0x02, 0xf8,
	0x04, 0xa7, 0x10, 0xda, 0x12, 0x22, 0x00, 0x4d,
	0x04, 0xa7, 0x08, 0x74, 0x00, 0x00, 0x03, 0x21},
	/* RGB2YUV:601 SD mode:
	 *  Cb = -0.291G  - 0.148R + 0.439B + 128
	 *  Y   = 0.504G   + 0.257R + 0.098B + 16
	 *  Cr  = -0.368G + 0.439R - 0.071B + 128
	 */
	{
	0x11, 0x5f, 0x01, 0x82, 0x10, 0x23, 0x00, 0x80,
	0x02, 0x1c, 0x00, 0xa1, 0x00, 0x36, 0x00, 0x1e,
	0x11, 0x29, 0x10, 0x59, 0x01, 0x82, 0x00, 0x80
	},

	/* RGB2YUV:709 HD mode:
	 *  Cb = - 0.338G - 0.101R +  0.439B + 128
	 *  Y  =    0.614G + 0.183R +  0.062B + 16
	 *  Cr = - 0.399G + 0.439R  -  0.040B + 128
	 */
	{
	0x11, 0x98, 0x01, 0xc1, 0x10, 0x28, 0x00, 0x80,
	0x02, 0x74, 0x00, 0xbb, 0x00, 0x3f, 0x00, 0x10,
	0x11, 0x5a, 0x10, 0x67, 0x01, 0xc1, 0x00, 0x80
	},
	/* RGB[0:255]2RGB[16:235]:
	 * R' = R x (235-16)/255 + 16;
	 * G' = G x (235-16)/255 + 16;
	 * B' = B x (235-16)/255 + 16;
	 */
	{
	0x00, 0x00, 0x03, 0x6F, 0x00, 0x00, 0x00, 0x10,
	0x03, 0x6F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10,
	0x00, 0x00, 0x00, 0x00, 0x03, 0x6F, 0x00, 0x10},
};

static int rockchip_hdmiv1_video_csc(struct hdmi *hdmi_drv,
				     struct hdmi_video *vpara)
{
	int value, i, csc_mode, c0_c2_change, auto_csc, csc_enable;
	const char *coeff = NULL;
	struct hdmi_dev *hdmi_dev = hdmi_drv->property->priv;

	/* Enable or disalbe color space convert */
	dev_info(hdmi_drv->dev, "[%s] input_color=%d,output_color=%d\n",
		 __func__, vpara->color_input, vpara->color_output);
	if (vpara->color_input == vpara->color_output) {
		if ((vpara->color_input >= HDMI_COLOR_YCBCR444) ||
		    (vpara->color_input == HDMI_COLOR_RGB_0_255)) {
			value = v_SOF_DISABLE | v_COLOR_DEPTH_NOT_INDICATED(1);
			hdmi_writel(hdmi_dev, VIDEO_CONTRL3, value);
			hdmi_msk_reg(hdmi_dev, VIDEO_CONTRL,
				     m_VIDEO_AUTO_CSC | m_VIDEO_C0_C2_SWAP,
				     v_VIDEO_AUTO_CSC(AUTO_CSC_DISABLE) |
				     v_VIDEO_C0_C2_SWAP(C0_C2_CHANGE_DISABLE));
			return 0;
		} else if (vpara->color_input == HDMI_COLOR_RGB_16_235) {
			csc_mode = CSC_RGB_0_255_TO_RGB_16_235_8BIT;
			auto_csc = AUTO_CSC_DISABLE;
			c0_c2_change = C0_C2_CHANGE_DISABLE;
			csc_enable = v_CSC_ENABLE;
		}
	}

	switch (vpara->vic) {
	case HDMI_720X480I_60HZ_4_3:
	case HDMI_720X576I_50HZ_4_3:
	case HDMI_720X480P_60HZ_4_3:
	case HDMI_720X576P_50HZ_4_3:
	case HDMI_720X480I_60HZ_16_9:
	case HDMI_720X576I_50HZ_16_9:
	case HDMI_720X480P_60HZ_16_9:
	case HDMI_720X576P_50HZ_16_9:
		if (((vpara->color_input == HDMI_COLOR_RGB_0_255) ||
		     (vpara->color_input == HDMI_COLOR_RGB_16_235)) &&
		    vpara->color_output >= HDMI_COLOR_YCBCR444) {
			csc_mode = CSC_RGB_0_255_TO_ITU601_16_235_8BIT;
			auto_csc = AUTO_CSC_DISABLE;
			c0_c2_change = C0_C2_CHANGE_DISABLE;
			csc_enable = v_CSC_ENABLE;
		} else if (vpara->color_input >= HDMI_COLOR_YCBCR444 &&
			   ((vpara->color_output == HDMI_COLOR_RGB_0_255) ||
			   (vpara->color_output == HDMI_COLOR_RGB_16_235))) {
#ifdef AUTO_DEFINE_CSC
			csc_mode = CSC_ITU601_16_235_TO_RGB_0_255_8BIT;
			auto_csc = AUTO_CSC_ENABLE;
			c0_c2_change = C0_C2_CHANGE_DISABLE;
			csc_enable = v_CSC_DISABLE;
#else
			csc_mode = CSC_ITU601_16_235_TO_RGB_0_255_8BIT;
			auto_csc = AUTO_CSC_DISABLE;
			c0_c2_change = C0_C2_CHANGE_ENABLE;
			csc_enable = v_CSC_ENABLE;
#endif
		}
		break;
	default:
		if (((vpara->color_input == HDMI_COLOR_RGB_0_255) ||
		     (vpara->color_input == HDMI_COLOR_RGB_16_235)) &&
		    vpara->color_output >= HDMI_COLOR_YCBCR444) {
			csc_mode = CSC_RGB_0_255_TO_ITU709_16_235_8BIT;
			auto_csc = AUTO_CSC_DISABLE;
			c0_c2_change = C0_C2_CHANGE_DISABLE;
			csc_enable = v_CSC_ENABLE;
		} else if (vpara->color_input >= HDMI_COLOR_YCBCR444 &&
			   ((vpara->color_output == HDMI_COLOR_RGB_0_255) ||
			   (vpara->color_output == HDMI_COLOR_RGB_16_235))) {
#ifdef AUTO_DEFINE_CSC
			csc_mode = CSC_ITU709_16_235_TO_RGB_0_255_8BIT;
			auto_csc = AUTO_CSC_ENABLE;
			c0_c2_change = C0_C2_CHANGE_DISABLE;
			csc_enable = v_CSC_DISABLE;
#else
			/*CSC_ITU709_16_235_TO_RGB_0_255_8BIT;*/
			csc_mode = CSC_ITU601_16_235_TO_RGB_0_255_8BIT;
			auto_csc = AUTO_CSC_DISABLE;
			c0_c2_change = C0_C2_CHANGE_ENABLE;
			csc_enable = v_CSC_ENABLE;
#endif
		}
		break;
	}

	coeff = coeff_csc[csc_mode];
	for (i = 0; i < 24; i++)
		hdmi_writel(hdmi_dev, VIDEO_CSC_COEF+i, coeff[i]);

	value = v_SOF_DISABLE | csc_enable | v_COLOR_DEPTH_NOT_INDICATED(1);
	hdmi_writel(hdmi_dev, VIDEO_CONTRL3, value);
	hdmi_msk_reg(hdmi_dev, VIDEO_CONTRL,
		     m_VIDEO_AUTO_CSC |
		     m_VIDEO_C0_C2_SWAP,
		     v_VIDEO_AUTO_CSC(auto_csc) |
		     v_VIDEO_C0_C2_SWAP(c0_c2_change));

#if 0
	if (vpara->input_color != vpara->output_color) {
		if (vpara->input_color == VIDEO_INPUT_COLOR_RGB) {/*rgb2yuv*/
			coeff = coeff_csc[3];
			for (i = 0; i < 24; i++)
				hdmi_writel(hdmi_dev,
					    VIDEO_CSC_COEF+i, coeff[i]);

			value = v_SOF_DISABLE | v_CSC_ENABLE;
			hdmi_writel(hdmi_dev, VIDEO_CONTRL3, value);
			hdmi_msk_reg(hdmi_dev, VIDEO_CONTRL,
				     m_VIDEO_AUTO_CSC | m_VIDEO_C0_C2_EXCHANGE,
				     v_VIDEO_AUTO_CSC(0) |
				     v_VIDEO_C0_C2_EXCHANGE(1));
		} else {/*yuv2rgb*/
#ifdef AUTO_DEFINE_CSC
			value = v_SOF_DISABLE | v_CSC_DISABLE;
			hdmi_writel(hdmi_dev, VIDEO_CONTRL3, value);
			hdmi_msk_reg(hdmi_dev, VIDEO_CONTRL,
				     m_VIDEO_AUTO_CSC | m_VIDEO_C0_C2_EXCHANGE,
				     v_VIDEO_AUTO_CSC(1) |
				     v_VIDEO_C0_C2_EXCHANGE(1));
#else
			if (hdmi_drv->lcdc->cur_screen->mode.xres <= 576) {
				/*x <= 576,REC-601*/
				coeff = coeff_csc[0];
				pr_info("xres<=576,xres=%d\n",
					hdmi_drv->lcdc->cur_screen->mode.xres);
			} else/*x > 576,REC-709*/{
				coeff = coeff_csc[2];
				pr_info("xres>576,xres=%d\n",
					hdmi_drv->lcdc->cur_screen->mode.xres);
			}
			for (i = 0; i < 24; i++)
				hdmi_writel(hdmi_dev,
					    VIDEO_CSC_COEF+i, coeff[i]);

			value = v_SOF_DISABLE | v_CSC_ENABLE;
			hdmi_writel(hdmi_dev, VIDEO_CONTRL3, value);
			hdmi_msk_reg(hdmi_dev, VIDEO_CONTRL,
				     m_VIDEO_AUTO_CSC |
				     m_VIDEO_C0_C2_EXCHANGE,
				     v_VIDEO_AUTO_CSC(0) |
				     v_VIDEO_C0_C2_EXCHANGE(0));
#endif
		}
	} else {
		if (vpara->input_color == VIDEO_INPUT_COLOR_RGB) {
			/*rgb[0:255]->rbg[16:235]*/
			coeff = coeff_csc[5];
			for (i = 0; i < 24; i++)
				hdmi_writel(hdmi_dev,
					    VIDEO_CSC_COEF+i, coeff[i]);

			value = v_SOF_DISABLE | v_CSC_ENABLE;
			hdmi_writel(hdmi_dev, VIDEO_CONTRL3, value);
			hdmi_msk_reg(hdmi_dev, VIDEO_CONTRL,
				     m_VIDEO_AUTO_CSC | m_VIDEO_C0_C2_EXCHANGE,
				     v_VIDEO_AUTO_CSC(0) |
				     v_VIDEO_C0_C2_EXCHANGE(1));
		} else {
			value = v_SOF_DISABLE;
			hdmi_writel(hdmi_dev, VIDEO_CONTRL3, value);
			hdmi_msk_reg(hdmi_dev, VIDEO_CONTRL,
				     m_VIDEO_AUTO_CSC |
				     m_VIDEO_C0_C2_EXCHANGE,
				     v_VIDEO_AUTO_CSC(0) |
				     v_VIDEO_C0_C2_EXCHANGE(1));
		}
	}
#endif
	return 0;
}

static int rockchip_hdmiv1_config_vsi(struct hdmi *hdmi,
				      unsigned char vic_3d,
				      unsigned char format)
{
	struct hdmi_dev *hdmi_dev = hdmi->property->priv;
	char info[SIZE_VSI_INFOFRAME];
	int i;

	DBG("[%s] vic_3d %d format %d.\n", __func__, vic_3d, format);
	memset(info, 0, SIZE_VSI_INFOFRAME);
	hdmi_msk_reg(hdmi_dev, PACKET_SEND_AUTO,
		     m_PACKET_VSI_EN, v_PACKET_VSI_EN(0));
	hdmi_writel(hdmi_dev, CONTROL_PACKET_BUF_INDEX, INFOFRAME_VSI);
	/* Header Bytes */
	info[0] = 0x81;
	info[1] = 0x01;
	/* PB1 - PB3 contain the 24bit IEEE Registration Identifier */
	info[4] = 0x03;
	info[5] = 0x0c;
	info[6] = 0x00;
	/* PB4 - HDMI_Video_Format into bits 7:5 */
	info[7] = format << 5;
	/* PB5 - Depending on the video format, this byte will contain either
	 * the HDMI_VIC code in buts 7:0, OR the 3D_Structure in bits 7:4.
	 */
	switch (format) {
	case HDMI_VIDEO_FORMAT_4KX2K:
		/* This is a 2x4K mode, set the HDMI_VIC in buts 7:0.  Values
		 * are from HDMI 1.4 Spec, 8.2.3.1 (Table 8-13).
		 */
		info[2] = 0x06 - 1;
		info[8] = vic_3d;
		info[9] = 0;
		break;
	case HDMI_VIDEO_FORMAT_3D:
		/* This is a 3D mode, set the 3D_Structure in buts 7:4
		 * Bits 3:0 are reseved so set to 0.  Values are from HDMI 1.4
		 * Spec, Appendix H (Table H-2).
		 */
		info[8] = vic_3d << 4;
		/* Add the Extended data field when the 3D format is
		 * Side-by-Side(Half). See Spec Table H-3 for details.
		 */
		if ((info[8] >> 4) == HDMI_3D_SIDE_BY_SIDE_HALF) {
			info[2] = 0x06;
			info[9] = 0x00;
		} else {
			info[2] = 0x06 - 1;
		}
		break;
	default:
		info[2] = 0x06 - 2;
		info[8] = 0;
		info[9] = 0;
		break;
	}
	info[3] = info[0] + info[1] + info[2];
	/* Calculate InfoFrame ChecKsum */
	for (i = 4; i < SIZE_VSI_INFOFRAME; i++)
		info[3] += info[i];
	info[3] = 0x100 - info[3];

	for (i = 0; i < SIZE_VSI_INFOFRAME; i++)
		hdmi_writel(hdmi_dev, CONTROL_PACKET_ADDR + i, info[i]);
	hdmi_msk_reg(hdmi_dev, PACKET_SEND_AUTO,
		     m_PACKET_VSI_EN, v_PACKET_VSI_EN(1));
	return 0;
}

static void rockchip_hdmiv1_config_avi(struct hdmi *hdmi_drv,
				       unsigned char vic,
				       unsigned char output_color)
{
	int i;
	int avi_color_mode;
	char info[SIZE_AVI_INFOFRAME];
	struct hdmi_dev *hdmi_dev = hdmi_drv->property->priv;

	memset(info, 0, SIZE_AVI_INFOFRAME);
	hdmi_writel(hdmi_dev, CONTROL_PACKET_BUF_INDEX, INFOFRAME_AVI);
	info[0] = 0x82;
	info[1] = 0x02;
	info[2] = 0x0D;
	info[3] = info[0] + info[1] + info[2];

	if ((output_color == HDMI_COLOR_RGB_0_255) ||
	    (output_color == HDMI_COLOR_RGB_16_235))
		avi_color_mode = AVI_COLOR_MODE_RGB;
	else if (output_color == HDMI_COLOR_YCBCR444)
		avi_color_mode = AVI_COLOR_MODE_YCBCR444;
	else if (output_color == HDMI_COLOR_YCBCR422)
		avi_color_mode = AVI_COLOR_MODE_YCBCR422;

	info[4] = (avi_color_mode << 5);
	info[5] =
	    (AVI_COLORIMETRY_NO_DATA << 6) |
	    (AVI_CODED_FRAME_ASPECT_NO_DATA << 4) |
	    ACTIVE_ASPECT_RATE_SAME_AS_CODED_FRAME;
	info[6] = 0;
	info[7] = vic;
	if ((vic == HDMI_720X480I_60HZ_4_3) ||
	    (vic == HDMI_720X576I_50HZ_4_3) ||
	    (vic == HDMI_720X480I_60HZ_16_9) ||
	    (vic == HDMI_720X480I_60HZ_16_9))
		info[8] = 1;
	else
		info[8] = 0;

	/* Calculate AVI InfoFrame ChecKsum */
	for (i = 4; i < SIZE_AVI_INFOFRAME; i++)
		info[3] += info[i];

	info[3] = 0x100 - info[3];

	for (i = 0; i < SIZE_AVI_INFOFRAME; i++)
		hdmi_writel(hdmi_dev, CONTROL_PACKET_ADDR + i, info[i]);
}

static int rockchip_hdmiv1_config_video(struct hdmi *hdmi_drv,
					struct hdmi_video *vpara)
{
	struct fb_videomode *mode;
	struct hdmi_dev *hdmi_dev = hdmi_drv->property->priv;
	int value;

	dev_dbg(hdmi_drv->dev, "[%s]\n", __func__);

	if (vpara == NULL) {
		dev_err(hdmi_drv->dev, "[%s] input parameter error\n",
			__func__);
		return -1;
	}

	if (hdmi_dev->soctype == HDMI_SOC_RK3036) {
		/*rk3036 vop only can output rgb fmt*/
		vpara->color_input = HDMI_COLOR_RGB_0_255;
	}

	mode = (struct fb_videomode *)hdmi_vic_to_videomode(vpara->vic);
	if (mode == NULL) {
		dev_err(hdmi_drv->dev, "[%s] not found vic %d\n", __func__,
			vpara->vic);
		return -ENOENT;
	}
	hdmi_dev->tmdsclk = mode->pixclock;
	if (hdmi_drv->uboot)
		return 0;
	/* Disable video and audio output */
	hdmi_msk_reg(hdmi_dev, AV_MUTE,
		     m_AUDIO_MUTE | m_AUDIO_PD | m_VIDEO_BLACK,
		     v_AUDIO_MUTE(1) | v_AUDIO_PD(1) | v_VIDEO_MUTE(1));

	/* Input video mode is SDR RGB24bit,
	 * Data enable signal from external
	 */
	hdmi_writel(hdmi_dev, VIDEO_CONTRL1,
		    v_VIDEO_INPUT_FORMAT(VIDEO_INPUT_SDR_RGB444) |
		    v_DE_EXTERNAL);

	value = v_VIDEO_INPUT_BITS(VIDEO_INPUT_8BITS);
	if (vpara->color_output <= HDMI_COLOR_RGB_16_235)
		value |= v_VIDEO_OUTPUT_COLOR(0);
	else
		value |= v_VIDEO_OUTPUT_COLOR((vpara->color_output - 2) & 0x3);
	if (vpara->color_input <= HDMI_COLOR_RGB_16_235)
		value |= v_VIDEO_INPUT_CSP(0);
	else
		value |= v_VIDEO_INPUT_CSP((vpara->color_input - 2) & 0x1);

	hdmi_writel(hdmi_dev, VIDEO_CONTRL2, value);
	/* Set HDMI Mode */
	hdmi_writel(hdmi_dev, HDCP_CTRL, v_HDMI_DVI(vpara->sink_hdmi));

	/* Enable or disalbe color space convert */
	rockchip_hdmiv1_video_csc(hdmi_drv, vpara);

	/* Set ext video timing */
	if ((mode->vmode || mode->pixclock <= 27000000) &&
	    vpara->format_3d != HDMI_3D_FRAME_PACKING) {
		hdmi_writel(hdmi_dev, VIDEO_TIMING_CTL, 0);
	} else {
		if (vpara->format_3d == HDMI_3D_FRAME_PACKING)
			value = v_EXTERANL_VIDEO(1) |
				v_INETLACE(0);
		else
			value = v_EXTERANL_VIDEO(1) |
				v_INETLACE(mode->vmode);
		if (mode->sync & FB_SYNC_HOR_HIGH_ACT)
			value |= v_HSYNC_POLARITY(1);
		if (mode->sync & FB_SYNC_VERT_HIGH_ACT)
			value |= v_VSYNC_POLARITY(1);
		hdmi_writel(hdmi_dev, VIDEO_TIMING_CTL, value);

		value = mode->left_margin +
			mode->xres + mode->right_margin +
			mode->hsync_len;
		hdmi_writel(hdmi_dev, VIDEO_EXT_HTOTAL_L, value & 0xFF);
		hdmi_writel(hdmi_dev, VIDEO_EXT_HTOTAL_H, (value >> 8) & 0xFF);

		value = mode->left_margin +
			mode->right_margin +
			mode->hsync_len;
		hdmi_writel(hdmi_dev, VIDEO_EXT_HBLANK_L, value & 0xFF);
		hdmi_writel(hdmi_dev, VIDEO_EXT_HBLANK_H, (value >> 8) & 0xFF);

		value = mode->left_margin + mode->hsync_len;
		hdmi_writel(hdmi_dev, VIDEO_EXT_HDELAY_L, value & 0xFF);
		hdmi_writel(hdmi_dev, VIDEO_EXT_HDELAY_H, (value >> 8) & 0xFF);

		value = mode->hsync_len;
		hdmi_writel(hdmi_dev, VIDEO_EXT_HDURATION_L,
			    value & 0xFF);
		hdmi_writel(hdmi_dev, VIDEO_EXT_HDURATION_H,
			    (value >> 8) & 0xFF);

		if (vpara->format_3d == HDMI_3D_FRAME_PACKING) {
			if (mode->vmode == 0)
				value = mode->upper_margin +
					mode->lower_margin +
					mode->vsync_len +
					2 * mode->yres;
			else
				value = 4 * (mode->upper_margin +
					     mode->lower_margin +
					     mode->vsync_len) +
					2 * mode->yres + 2;
		} else {
			value = mode->upper_margin +
				mode->lower_margin +
				mode->vsync_len +
				mode->yres;
		}
		hdmi_writel(hdmi_dev, VIDEO_EXT_VTOTAL_L, value & 0xFF);
		hdmi_writel(hdmi_dev, VIDEO_EXT_VTOTAL_H, (value >> 8) & 0xFF);

		value = mode->upper_margin +
			mode->vsync_len +
			mode->lower_margin;
		hdmi_writel(hdmi_dev, VIDEO_EXT_VBLANK, value & 0xFF);

		if (vpara->vic == HDMI_720X480P_60HZ_4_3 ||
		    vpara->vic == HDMI_720X480P_60HZ_16_9)
			value = 42;
		else
			value = mode->upper_margin + mode->vsync_len;

		hdmi_writel(hdmi_dev, VIDEO_EXT_VDELAY, value & 0xFF);

		value = mode->vsync_len;
		hdmi_writel(hdmi_dev, VIDEO_EXT_VDURATION, value & 0xFF);
	}
	if (vpara->sink_hdmi == OUTPUT_HDMI) {
		rockchip_hdmiv1_config_avi(hdmi_drv, vpara->vic,
					   vpara->color_output);
		if (vpara->format_3d != HDMI_3D_NONE) {
			rockchip_hdmiv1_config_vsi(hdmi_drv,
						   vpara->format_3d,
						   HDMI_VIDEO_FORMAT_3D);
		} else if ((vpara->vic > 92 && vpara->vic < 96) ||
			 (vpara->vic == 98)) {
			vpara->vic = (vpara->vic == 98) ?
				     4 : (96 - vpara->vic);
			rockchip_hdmiv1_config_vsi(hdmi_drv,
						   vpara->vic,
						   HDMI_VIDEO_FORMAT_4KX2K);
		} else {
			rockchip_hdmiv1_config_vsi(hdmi_drv,
						   vpara->vic,
						   HDMI_VIDEO_FORMAT_NORMAL);
		}
		dev_info(hdmi_drv->dev,
			 "[%s] sucess output HDMI.\n", __func__);
	} else {
		dev_info(hdmi_drv->dev,
			 "[%s] sucess output DVI.\n", __func__);
	}

	/* rk3028a */
	hdmi_writel(hdmi_dev, PHY_PRE_DIV_RATIO, 0x1e);
	hdmi_writel(hdmi_dev, PHY_FEEDBACK_DIV_RATIO_LOW, 0x2c);
	hdmi_writel(hdmi_dev, PHY_FEEDBACK_DIV_RATIO_HIGH, 0x01);

	return 0;
}

static void rockchip_hdmiv1_config_aai(struct hdmi *hdmi_drv)
{
	int i;
	char info[SIZE_AUDIO_INFOFRAME];
	struct hdmi_dev *hdmi_dev = hdmi_drv->property->priv;

	memset(info, 0, SIZE_AUDIO_INFOFRAME);

	info[0] = 0x84;
	info[1] = 0x01;
	info[2] = 0x0A;

	info[3] = info[0] + info[1] + info[2];
	for (i = 4; i < SIZE_AUDIO_INFOFRAME; i++)
		info[3] += info[i];

	info[3] = 0x100 - info[3];

	hdmi_writel(hdmi_dev, CONTROL_PACKET_BUF_INDEX, INFOFRAME_AAI);
	for (i = 0; i < SIZE_AUDIO_INFOFRAME; i++)
		hdmi_writel(hdmi_dev, CONTROL_PACKET_ADDR + i, info[i]);
}

static int rockchip_hdmiv1_config_audio(struct hdmi *hdmi_drv,
					struct hdmi_audio *audio)
{
	int rate, N, channel, mclk_fs;
	struct hdmi_dev *hdmi_dev = hdmi_drv->property->priv;

	if (audio->channel < 3)
		channel = I2S_CHANNEL_1_2;
	else if (audio->channel < 5)
		channel = I2S_CHANNEL_3_4;
	else if (audio->channel < 7)
		channel = I2S_CHANNEL_5_6;
	else
		channel = I2S_CHANNEL_7_8;

	switch (audio->rate) {
	case HDMI_AUDIO_FS_32000:
		rate = AUDIO_32K;
		N = N_32K;
		mclk_fs = MCLK_384FS;
		break;
	case HDMI_AUDIO_FS_44100:
		rate = AUDIO_441K;
		N = N_441K;
		mclk_fs = MCLK_256FS;
		break;
	case HDMI_AUDIO_FS_48000:
		rate = AUDIO_48K;
		N = N_48K;
		mclk_fs = MCLK_256FS;
		break;
	case HDMI_AUDIO_FS_88200:
		rate = AUDIO_882K;
		N = N_882K;
		mclk_fs = MCLK_128FS;
		break;
	case HDMI_AUDIO_FS_96000:
		rate = AUDIO_96K;
		N = N_96K;
		mclk_fs = MCLK_128FS;
		break;
	case HDMI_AUDIO_FS_176400:
		rate = AUDIO_1764K;
		N = N_1764K;
		mclk_fs = MCLK_128FS;
		break;
	case HDMI_AUDIO_FS_192000:
		rate = AUDIO_192K;
		N = N_192K;
		mclk_fs = MCLK_128FS;
		break;
	default:
		dev_err(hdmi_drv->dev,
			"[%s] not support such sample rate %d\n",
			__func__, audio->rate);
		return -ENOENT;
	}

	/* set_audio source I2S */
	if (hdmi_dev->audiosrc == HDMI_AUDIO_SRC_IIS) {
		hdmi_writel(hdmi_dev, AUDIO_CTRL1, 0x01);
		hdmi_writel(hdmi_dev, AUDIO_SAMPLE_RATE, rate);
		hdmi_writel(hdmi_dev, AUDIO_I2S_MODE,
			    v_I2S_MODE(I2S_STANDARD) |
			    v_I2S_CHANNEL(channel));
		hdmi_writel(hdmi_dev, AUDIO_I2S_MAP, 0x00);
		/* no swap */
		hdmi_writel(hdmi_dev, AUDIO_I2S_SWAPS_SPDIF, 0);
	} else {
		hdmi_writel(hdmi_dev, AUDIO_CTRL1, 0x08);
		/* no swap */
		hdmi_writel(hdmi_dev, AUDIO_I2S_SWAPS_SPDIF, 0);
	}

	/* Set N value */
	hdmi_writel(hdmi_dev, AUDIO_N_H, (N >> 16) & 0x0F);
	hdmi_writel(hdmi_dev, AUDIO_N_M, (N >> 8) & 0xFF);
	hdmi_writel(hdmi_dev, AUDIO_N_L, N & 0xFF);

	/*Set hdmi nlpcm mode to support hdmi bitstream*/
	if (audio->type == HDMI_AUDIO_NLPCM)
		hdmi_writel(hdmi_dev, AUDIO_CHANNEL_STATUS,
			    v_AUDIO_STATUS_NLPCM(1));
	else
		hdmi_writel(hdmi_dev, AUDIO_CHANNEL_STATUS,
			    v_AUDIO_STATUS_NLPCM(0));

	rockchip_hdmiv1_config_aai(hdmi_drv);

	return 0;
}

int rockchip_hdmiv1_control_output(struct hdmi *hdmi_drv, int enable)
{
	int mutestatus = 0;
	struct hdmi_dev *hdmi_dev = hdmi_drv->property->priv;

	if (hdmi_drv->uboot) {
		hdmi_drv->uboot = 0;
		return 0;
	}

	if (enable == HDMI_AV_UNMUTE) {
		if (hdmi_dev->pwr_mode == LOWER_PWR)
			rockchip_hdmiv1_set_pwr_mode(hdmi_drv, NORMAL);
		hdmi_readl(hdmi_dev, AV_MUTE, &mutestatus);
		if (mutestatus & m_VIDEO_BLACK) {
			rockchip_hdmiv1_sys_power(hdmi_drv, true);
			rockchip_hdmiv1_sys_power(hdmi_drv, false);
			delay100us();
			rockchip_hdmiv1_sys_power(hdmi_drv, true);
			hdmi_writel(hdmi_dev, 0xce, 0x00);
			delay100us();
			hdmi_writel(hdmi_dev, 0xce, 0x01);
		}

		if (mutestatus && (m_AUDIO_MUTE | m_VIDEO_BLACK)) {
			hdmi_msk_reg(hdmi_dev, AV_MUTE,
				     m_AUDIO_MUTE |
				     m_AUDIO_PD |
				     m_VIDEO_BLACK,
				     v_AUDIO_MUTE(0) |
				     v_AUDIO_PD(0) |
				     v_VIDEO_MUTE(0));
		}
		rockchip_hdmiv1_av_mute(hdmi_drv, 0);
	} else {
		mutestatus = 0;
		if (enable & HDMI_VIDEO_MUTE)
			mutestatus |= v_VIDEO_MUTE(1);
		if (enable & HDMI_AUDIO_MUTE)
			mutestatus |= (v_AUDIO_MUTE(1) | v_AUDIO_PD(1));
		hdmi_msk_reg(hdmi_dev, AV_MUTE,
			     m_AUDIO_MUTE | m_AUDIO_PD | m_VIDEO_BLACK,
			     mutestatus);

		if (enable == (HDMI_VIDEO_MUTE | HDMI_AUDIO_MUTE)) {
			rockchip_hdmiv1_av_mute(hdmi_drv, 1);
			msleep(100);
			rockchip_hdmiv1_set_pwr_mode(hdmi_drv, LOWER_PWR);
		}
	}
	return 0;
}

int rockchip_hdmiv1_removed(struct hdmi *hdmi_drv)
{
	dev_info(hdmi_drv->dev, "Removed.\n");
	if (hdmi_drv->ops->hdcp_power_off_cb)
		hdmi_drv->ops->hdcp_power_off_cb(hdmi_drv);

	rockchip_hdmiv1_control_output(hdmi_drv, -1);
	rockchip_hdmiv1_set_pwr_mode(hdmi_drv, LOWER_PWR);

	return HDMI_ERROR_SUCESS;
}

static int rockchip_hdmiv1_enable(struct hdmi *hdmi_drv)
{
	struct hdmi_dev *hdmi_dev = hdmi_drv->property->priv;

	if (!hdmi_dev->enable) {
		hdmi_dev->enable = 1;
		hdmi_msk_reg(hdmi_dev, HDMI_STATUS,
			     m_MASK_INT_HOTPLUG, v_MASK_INT_HOTPLUG(1));
	}
	hdmi_submit_work(hdmi_drv, HDMI_HPD_CHANGE, 10, 0);
	return 0;
}

static int rockchip_hdmiv1_disable(struct hdmi *hdmi_drv)
{
	struct hdmi_dev *hdmi_dev = hdmi_drv->property->priv;

	if (hdmi_dev->enable) {
		hdmi_dev->enable = 0;
		hdmi_msk_reg(hdmi_dev, HDMI_STATUS,
			     m_MASK_INT_HOTPLUG, v_MASK_INT_HOTPLUG(0));
	}
	return 0;
}

void rockchip_hdmiv1_irq(struct hdmi *hdmi_drv)
{
	u32 interrupt = 0;
	struct hdmi_dev *hdmi_dev = hdmi_drv->property->priv;

	hdmi_readl(hdmi_dev, INTERRUPT_STATUS1, &interrupt);
	if (interrupt) {
		hdmi_writel(hdmi_dev, INTERRUPT_STATUS1, interrupt);
		dev_info(hdmi_drv->dev, "Clear edid irq.\n");
	}

	hdmi_readl(hdmi_dev, HDMI_STATUS, &interrupt);
	if (interrupt)
		hdmi_writel(hdmi_dev, HDMI_STATUS, interrupt);
	if (interrupt & m_INT_HOTPLUG)
		hdmi_submit_work(hdmi_drv, HDMI_HPD_CHANGE, 20, 0);

	if (hdmi_drv->ops->hdcp_irq_cb)
		hdmi_drv->ops->hdcp_irq_cb(0);
	if (hdmi_drv->property->feature & SUPPORT_CEC)
		rockchip_hdmiv1_cec_isr(hdmi_dev);
}

static void rockchip_hdmiv1_reset(struct hdmi *hdmi_drv)
{
	u32 val = 0;
	u32 msk = 0;
	struct hdmi_dev *hdmi_dev = hdmi_drv->property->priv;

	hdmi_msk_reg(hdmi_dev, SYS_CTRL, m_RST_DIGITAL, v_NOT_RST_DIGITAL);
	delay100us();
	hdmi_msk_reg(hdmi_dev, SYS_CTRL, m_RST_ANALOG, v_NOT_RST_ANALOG);
	delay100us();
	msk = m_REG_CLK_INV | m_REG_CLK_SOURCE | m_POWER | m_INT_POL;
	val = v_REG_CLK_INV | v_REG_CLK_SOURCE_SYS | v_PWR_ON | v_INT_POL_HIGH;
	hdmi_msk_reg(hdmi_dev, SYS_CTRL, msk, val);

	rockchip_hdmiv1_set_pwr_mode(hdmi_drv, LOWER_PWR);
}

void rockchip_hdmiv1_dev_init_ops(struct hdmi_ops *ops)
{
	if (ops) {
		ops->disable = rockchip_hdmiv1_disable;
		ops->enable = rockchip_hdmiv1_enable;
		ops->remove = rockchip_hdmiv1_removed;
		ops->setmute = rockchip_hdmiv1_control_output;
		ops->setvideo = rockchip_hdmiv1_config_video;
		ops->setaudio = rockchip_hdmiv1_config_audio;
		ops->getstatus = rockchip_hdmiv1_detect_hotplug;
		ops->getedid = rockchip_hdmiv1_read_edid;
		ops->insert	= rockchip_hdmiv1_insert;
		ops->setvsi = rockchip_hdmiv1_config_vsi;
	}
}

int rockchip_hdmiv1_initial(struct hdmi *hdmi_drv)
{
	int rc = HDMI_ERROR_SUCESS;
	struct hdmi_dev *hdmi_dev = hdmi_drv->property->priv;

	hdmi_dev->pwr_mode = NORMAL;

	if (!hdmi_drv->uboot) {
		rockchip_hdmiv1_reset_pclk();
		rockchip_hdmiv1_reset(hdmi_drv);
		hdmi_msk_reg(hdmi_dev, HDMI_STATUS,
			     m_MASK_INT_HOTPLUG, v_MASK_INT_HOTPLUG(0));
	} else if (hdmi_drv->ops->getstatus(hdmi_drv) == HDMI_HPD_REMOVED) {
		rockchip_hdmiv1_removed(hdmi_drv);
		hdmi_drv->lcdc->uboot_logo = 0;
		hdmi_drv->uboot = 0;
	}
	if (hdmi_drv->property->feature & SUPPORT_CEC)
		rockchip_hdmiv1_cec_init(hdmi_drv);
	if (hdmi_drv->property->feature & SUPPORT_HDCP)
		rockchip_hdmiv1_hdcp_init(hdmi_drv);
	return rc;
}
