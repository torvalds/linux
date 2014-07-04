#include <linux/delay.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/of_irq.h>
#include "rk616_hdmi.h"
#include "rk616_hdmi_hw.h"

#if !defined(CONFIG_ARCH_RK3026) && !defined(SOC_CONFIG_RK3036)

static int rk616_set_polarity(struct mfd_rk616 *rk616_drv, int vic)
{
	u32 val;
	int ret;
	u32 hdmi_polarity_mask = (3 << 14);

	switch (vic) {

	case HDMI_1920x1080p_60Hz:
	case HDMI_1920x1080p_50Hz:
	case HDMI_1920x1080i_60Hz:
	case HDMI_1920x1080i_50Hz:
	case HDMI_1280x720p_60Hz:
	case HDMI_1280x720p_50Hz:
		val = 0xc000;
		ret = rk616_drv->write_dev_bits(rk616_drv, CRU_CFGMISC_CON,
						hdmi_polarity_mask, &val);
		break;

	case HDMI_720x576p_50Hz_4_3:
	case HDMI_720x576p_50Hz_16_9:
	case HDMI_720x480p_60Hz_4_3:
	case HDMI_720x480p_60Hz_16_9:
		val = 0x0;
		ret = rk616_drv->write_dev_bits(rk616_drv, CRU_CFGMISC_CON,
						hdmi_polarity_mask, &val);
		break;
	default:
		val = 0x0;
		ret = rk616_drv->write_dev_bits(rk616_drv, CRU_CFGMISC_CON,
						hdmi_polarity_mask, &val);
		break;
	}
	return ret;
}

static int rk616_hdmi_set_vif(struct hdmi *hdmi_drv, struct rk_screen *screen,
			      bool connect)
{
	struct rk_hdmi_device *hdmi_dev = container_of(hdmi_drv,
						       struct rk_hdmi_device,
						       driver);
	struct mfd_rk616 *rk616_drv = hdmi_dev->rk616_drv;

	if (connect)
		rk616_set_polarity(rk616_drv, hdmi_drv->vic);

	rk616_set_vif(rk616_drv, screen, connect);
	return 0;
}

static int rk616_hdmi_init_pol_set(struct mfd_rk616 *rk616_drv, int pol)
{
	u32 val;
	int ret;
	int int_pol_mask = (1 << 5);

	if (pol)
		val = 0x0;
	else
		val = 0x20;
	ret = rk616_drv->write_dev_bits(rk616_drv, CRU_CFGMISC_CON,
					int_pol_mask, &val);

	return 0;
}
#endif

static inline void delay100us(void)
{
	msleep(1);
}

static void rk616_hdmi_av_mute(struct hdmi *hdmi_drv, bool enable)
{
	struct rk_hdmi_device *hdmi_dev = container_of(hdmi_drv,
						       struct rk_hdmi_device,
						       driver);

	hdmi_writel(hdmi_dev, AV_MUTE,
		    v_AUDIO_MUTE(enable) | v_VIDEO_MUTE(enable));
}

static void rk616_hdmi_sys_power(struct hdmi *hdmi_drv, bool enable)
{
	struct rk_hdmi_device *hdmi_dev = container_of(hdmi_drv,
						       struct rk_hdmi_device,
						       driver);

	if (enable)
		hdmi_msk_reg(hdmi_dev, SYS_CTRL, m_POWER, v_PWR_ON);
	else
		hdmi_msk_reg(hdmi_dev, SYS_CTRL, m_POWER, v_PWR_OFF);
}

static void rk616_hdmi_set_pwr_mode(struct hdmi *hdmi_drv, int mode)
{
	struct rk_hdmi_device *hdmi_dev = container_of(hdmi_drv,
						       struct rk_hdmi_device,
						       driver);

	if (hdmi_drv->pwr_mode == mode)
		return;

	hdmi_dbg(hdmi_drv->dev, "%s change pwr_mode %d --> %d\n", __func__,
		 hdmi_drv->pwr_mode, mode);

	switch (mode) {
	case NORMAL:
		hdmi_dbg(hdmi_drv->dev,
			 "%s change pwr_mode NORMAL pwr_mode = %d, mode = %d\n",
			 __func__, hdmi->pwr_mode, mode);
		rk616_hdmi_sys_power(hdmi_drv, false);
		if (!(hdmi_drv->set_vif)
		    && (hdmi_drv->vic == HDMI_1920x1080p_60Hz
			|| hdmi_drv->vic == HDMI_1920x1080p_50Hz)) {
			/* 3026 and 1080p */
			hdmi_writel(hdmi_dev, PHY_DRIVER, 0xcc);
			hdmi_writel(hdmi_dev, PHY_PRE_EMPHASIS, 0x4f);
		} else {
			hdmi_writel(hdmi_dev, PHY_DRIVER, 0x99);
			hdmi_writel(hdmi_dev, PHY_PRE_EMPHASIS, 0x0f);
		}
#ifdef SOC_CONFIG_RK3036
		hdmi_writel(hdmi_dev, PHY_SYS_CTL,0x15);
		hdmi_writel(hdmi_dev, PHY_SYS_CTL,0x14);
		hdmi_writel(hdmi_dev, PHY_SYS_CTL,0x10);
#else
		hdmi_writel(hdmi_dev, PHY_SYS_CTL,0x2d);
		hdmi_writel(hdmi_dev, PHY_SYS_CTL,0x2c);
		hdmi_writel(hdmi_dev, PHY_SYS_CTL,0x28);
		hdmi_writel(hdmi_dev, PHY_SYS_CTL,0x20);
#endif
		hdmi_writel(hdmi_dev, PHY_CHG_PWR, 0x0f);
		hdmi_writel(hdmi_dev, 0xce, 0x00);
		hdmi_writel(hdmi_dev, 0xce, 0x01);
		rk616_hdmi_av_mute(hdmi_drv, 1);
		rk616_hdmi_sys_power(hdmi_drv, true);
		break;
	case LOWER_PWR:
		hdmi_dbg(hdmi_drv->dev,
			 "%s change pwr_mode LOWER_PWR pwr_mode = %d, mode = %d\n",
			 __func__, hdmi->pwr_mode, mode);
		rk616_hdmi_av_mute(hdmi_drv, 0);
		rk616_hdmi_sys_power(hdmi_drv, false);
		hdmi_writel(hdmi_dev, PHY_DRIVER, 0x00);
		hdmi_writel(hdmi_dev, PHY_PRE_EMPHASIS, 0x00);
		hdmi_writel(hdmi_dev, PHY_CHG_PWR, 0x00);
#ifdef SOC_CONFIG_RK3036
		hdmi_writel(hdmi_dev, PHY_SYS_CTL,0x17);
#else
		hdmi_writel(hdmi_dev, PHY_SYS_CTL,0x2f);
#endif
		break;
	default:
		hdmi_dbg(hdmi_drv->dev, "unkown rk616 hdmi pwr mode %d\n",
			 mode);
	}

	hdmi_drv->pwr_mode = mode;
}

int rk616_hdmi_detect_hotplug(struct hdmi *hdmi_drv)
{
	int value = 0;
	struct rk_hdmi_device *hdmi_dev = container_of(hdmi_drv,
						       struct rk_hdmi_device,
						       driver);

	hdmi_dbg(hdmi_drv->dev, "[%s] value %02x\n", __func__, value);
	hdmi_readl(hdmi_dev, HDMI_STATUS, &value);
	value &= m_HOTPLUG;
	if (value == m_HOTPLUG)
		return HDMI_HPD_ACTIVED;
	else if (value)
		return HDMI_HPD_INSERT;
	else
		return HDMI_HPD_REMOVED;
}

int rk616_hdmi_read_edid(struct hdmi *hdmi_drv, int block, u8 *buf)
{
	u32 c = 0;
	u8 segment = 0;
	u8 offset = 0;
	int ret = -1;
	int i, j;
	int ddc_bus_freq;
	int trytime;
	int checksum = 0;
	struct rk_hdmi_device *hdmi_dev = container_of(hdmi_drv,
						       struct rk_hdmi_device,
						       driver);

	if (block % 2)
		offset = HDMI_EDID_BLOCK_SIZE;

	if (block / 2)
		segment = 1;

	ddc_bus_freq = (HDMI_SYS_FREG_CLK >> 2) / HDMI_SCL_RATE;
	hdmi_writel(hdmi_dev, DDC_BUS_FREQ_L, ddc_bus_freq & 0xFF);
	hdmi_writel(hdmi_dev, DDC_BUS_FREQ_H, (ddc_bus_freq >> 8) & 0xFF);

	hdmi_dbg(hdmi_drv->dev,
		 "EDID DATA (Segment = %d Block = %d Offset = %d):\n",
		 (int)segment, (int)block, (int)offset);
	disable_irq(hdmi_drv->irq);

	/* Enable edid interrupt */
	hdmi_writel(hdmi_dev, INTERRUPT_MASK1,
		    m_INT_HOTPLUG | m_INT_EDID_READY);

	for (trytime = 0; trytime < 10; trytime++) {
		hdmi_writel(hdmi_dev, INTERRUPT_STATUS1, 0x04);

		/* Set edid fifo first addr */
		hdmi_writel(hdmi_dev, EDID_FIFO_OFFSET, 0x00);

		/* Set edid word address 0x00/0x80 */
		hdmi_writel(hdmi_dev, EDID_WORD_ADDR, offset);

		/* Set edid segment pointer */
		hdmi_writel(hdmi_dev, EDID_SEGMENT_POINTER, segment);

		for (i = 0; i < 10; i++) {
			/* Wait edid interrupt */
			msleep(10);
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
#ifdef HDMI_DEBUG
				if (j % 16 == 0)
					dev_info(hdmi_drv->dev, "\n>>>0x%02x: ",
						 j);

				dev_info(hdmi_drv->dev, "0x%02x ", c);
#endif
			}

			/* clear EDID interrupt reg */
			hdmi_writel(hdmi_dev, INTERRUPT_STATUS1,
				    m_INT_EDID_READY);

			if ((checksum & 0xff) == 0) {
				ret = 0;
				hdmi_dbg(hdmi_drv->dev,
					 "[%s] edid read sucess\n", __func__);
				break;
			}
		}
	}
	//close edid irq
#ifdef SOC_CONFIG_RK3036
	hdmi_writel(hdmi_dev, INTERRUPT_MASK1, 0);
#else
	hdmi_writel(hdmi_dev, INTERRUPT_MASK1, m_INT_HOTPLUG);
#endif
	enable_irq(hdmi_drv->irq);

	return ret;
}

static void rk616_hdmi_config_avi(struct hdmi *hdmi_drv,
				  unsigned char vic, unsigned char output_color)
{
	int i;
	char info[SIZE_AVI_INFOFRAME];
	struct rk_hdmi_device *hdmi_dev = container_of(hdmi_drv,
						       struct rk_hdmi_device,
						       driver);

	memset(info, 0, SIZE_AVI_INFOFRAME);
	hdmi_writel(hdmi_dev, CONTROL_PACKET_BUF_INDEX, INFOFRAME_AVI);
	info[0] = 0x82;
	info[1] = 0x02;
	info[2] = 0x0D;
	info[3] = info[0] + info[1] + info[2];
	info[4] = (AVI_COLOR_MODE_RGB << 5);
	info[5] =
	    (AVI_COLORIMETRY_NO_DATA << 6) | (AVI_CODED_FRAME_ASPECT_NO_DATA <<
					      4) |
	    ACTIVE_ASPECT_RATE_SAME_AS_CODED_FRAME;
	info[6] = 0;
	info[7] = vic;
	info[8] = 0;

	/* Calculate AVI InfoFrame ChecKsum */
	for (i = 4; i < SIZE_AVI_INFOFRAME; i++)
		info[3] += info[i];

	info[3] = 0x100 - info[3];

	for (i = 0; i < SIZE_AVI_INFOFRAME; i++)
		hdmi_writel(hdmi_dev, CONTROL_PACKET_ADDR + i, info[i]);
}

static int rk616_hdmi_config_video(struct hdmi *hdmi_drv,
				   struct hdmi_video_para *vpara)
{
	int value;
	struct fb_videomode *mode;
	struct rk_hdmi_device *hdmi_dev = container_of(hdmi_drv,
						       struct rk_hdmi_device,
						       driver);

	hdmi_dbg(hdmi_drv->dev, "[%s]\n", __func__);

	if (vpara == NULL) {
		hdmi_err(hdmi_drv->dev, "[%s] input parameter error\n",
			 __func__);
		return -1;
	}

	/* Output RGB as default */
	vpara->output_color = VIDEO_OUTPUT_RGB444;
	if (hdmi_drv->pwr_mode == LOWER_PWR)
		rk616_hdmi_set_pwr_mode(hdmi_drv, NORMAL);

	/* Disable video and audio output */
	hdmi_writel(hdmi_dev, AV_MUTE, v_AUDIO_MUTE(1) | v_VIDEO_MUTE(1));

	/* Input video mode is SDR RGB24bit, Data enable signal from external */
	hdmi_writel(hdmi_dev, VIDEO_CONTRL1,
		    v_VIDEO_INPUT_FORMAT(VIDEO_INPUT_SDR_RGB444) |
		    v_DE_EXTERNAL);
	hdmi_writel(hdmi_dev, VIDEO_CONTRL2,
		    v_VIDEO_INPUT_BITS(VIDEO_INPUT_8BITS) |
		    v_VIDEO_OUTPUT_FORMAT(vpara->output_color & 0xFF));

	/* Set HDMI Mode */
	hdmi_writel(hdmi_dev, HDCP_CTRL, v_HDMI_DVI(vpara->output_mode));

	/* Enable or disalbe color space convert */
	if (vpara->input_color != vpara->output_color)
		value = v_SOF_DISABLE | v_CSC_ENABLE;
	else
		value = v_SOF_DISABLE;
	hdmi_writel(hdmi_dev, VIDEO_CONTRL3, value);

	/* Set ext video timing */
#if 1
	hdmi_writel(hdmi_dev, VIDEO_TIMING_CTL, 0);
	mode = (struct fb_videomode *)hdmi_vic_to_videomode(vpara->vic);
	if (mode == NULL) {
		hdmi_err(hdmi_drv->dev, "[%s] not found vic %d\n", __func__,
			 vpara->vic);
		return -ENOENT;
	}
	hdmi_drv->tmdsclk = mode->pixclock;
#else
	value = v_EXTERANL_VIDEO(1) | v_INETLACE(mode->vmode);
	if (mode->sync & FB_SYNC_HOR_HIGH_ACT)
		value |= v_HSYNC_POLARITY(1);
	if (mode->sync & FB_SYNC_VERT_HIGH_ACT)
		value |= v_VSYNC_POLARITY(1);
	hdmi_writel(hdmi_dev, VIDEO_TIMING_CTL, value);

	value = mode->left_margin + mode->xres + mode->right_margin +
	    mode->hsync_len;
	hdmi_writel(hdmi_dev, VIDEO_EXT_HTOTAL_L, value & 0xFF);
	hdmi_writel(hdmi_dev, VIDEO_EXT_HTOTAL_H, (value >> 8) & 0xFF);

	value = mode->left_margin + mode->right_margin + mode->hsync_len;
	hdmi_writel(hdmi_dev, VIDEO_EXT_HBLANK_L, value & 0xFF);
	hdmi_writel(hdmi_dev, VIDEO_EXT_HBLANK_H, (value >> 8) & 0xFF);

	value = mode->left_margin + mode->hsync_len;
	hdmi_writel(hdmi_dev, VIDEO_EXT_HDELAY_L, value & 0xFF);
	hdmi_writel(hdmi_dev, VIDEO_EXT_HDELAY_H, (value >> 8) & 0xFF);

	value = mode->hsync_len;
	hdmi_writel(hdmi_dev, VIDEO_EXT_HDURATION_L, value & 0xFF);
	hdmi_writel(hdmi_dev, VIDEO_EXT_HDURATION_H, (value >> 8) & 0xFF);

	value = mode->upper_margin + mode->yres + mode->lower_margin +
	    mode->vsync_len;
	hdmi_writel(hdmi_dev, VIDEO_EXT_VTOTAL_L, value & 0xFF);
	hdmi_writel(hdmi_dev, VIDEO_EXT_VTOTAL_H, (value >> 8) & 0xFF);

	value = mode->upper_margin + mode->vsync_len + mode->lower_margin;
	hdmi_writel(hdmi_dev, VIDEO_EXT_VBLANK, value & 0xFF);

	if (vpara->vic == HDMI_720x480p_60Hz_4_3
	    || vpara->vic == HDMI_720x480p_60Hz_16_9)
		value = 42;
	else
		value = mode->upper_margin + mode->vsync_len;

	hdmi_writel(hdmi_dev, VIDEO_EXT_VDELAY, value & 0xFF);

	value = mode->vsync_len;
	hdmi_writel(hdmi_dev, VIDEO_EXT_VDURATION, value & 0xFF);
#endif

	if (vpara->output_mode == OUTPUT_HDMI) {
		rk616_hdmi_config_avi(hdmi_drv, vpara->vic,
				      vpara->output_color);
		hdmi_dbg(hdmi->dev, "[%s] sucess output HDMI.\n", __func__);
	} else {
		hdmi_dbg(hdmi->dev, "[%s] sucess output DVI.\n", __func__);
	}

	if (hdmi_drv->set_vif) {
		hdmi_writel(hdmi_dev, PHY_PRE_DIV_RATIO, 0x0f);
		hdmi_writel(hdmi_dev, PHY_FEEDBACK_DIV_RATIO_LOW, 0x96);
	} else {		/* rk3028a */
		hdmi_writel(hdmi_dev, PHY_PRE_DIV_RATIO, 0x1e);
		hdmi_writel(hdmi_dev, PHY_FEEDBACK_DIV_RATIO_LOW, 0x2c);
		hdmi_writel(hdmi_dev, PHY_FEEDBACK_DIV_RATIO_HIGH, 0x01);
	}
	return 0;
}

static void rk616_hdmi_config_aai(struct hdmi *hdmi_drv)
{
	int i;
	char info[SIZE_AUDIO_INFOFRAME];
	struct rk_hdmi_device *hdmi_dev = container_of(hdmi_drv,
						       struct rk_hdmi_device,
						       driver);

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

static int rk616_hdmi_config_audio(struct hdmi *hdmi_drv,
				   struct hdmi_audio *audio)
{
	int rate, N, channel, mclk_fs;
	struct rk_hdmi_device *hdmi_dev = container_of(hdmi_drv,
						       struct rk_hdmi_device,
						       driver);

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
		dev_err(hdmi_drv->dev, "[%s] not support such sample rate %d\n",
			__func__, audio->rate);
		return -ENOENT;
	}

	/* set_audio source I2S */
	if (HDMI_CODEC_SOURCE_SELECT == INPUT_IIS) {
		hdmi_writel(hdmi_dev, AUDIO_CTRL1, 0x00);
		hdmi_writel(hdmi_dev, AUDIO_SAMPLE_RATE, rate);
		hdmi_writel(hdmi_dev, AUDIO_I2S_MODE,
			    v_I2S_MODE(I2S_STANDARD) | v_I2S_CHANNEL(channel));
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
	rk616_hdmi_config_aai(hdmi_drv);

	return 0;
}

void rk616_hdmi_control_output(struct hdmi *hdmi_drv, int enable)
{
	int mutestatus = 0;
	struct rk_hdmi_device *hdmi_dev = container_of(hdmi_drv,
						       struct rk_hdmi_device,
						       driver);

	if (enable) {
		if (hdmi_drv->pwr_mode == LOWER_PWR)
			rk616_hdmi_set_pwr_mode(hdmi_drv, NORMAL);
		hdmi_readl(hdmi_dev, AV_MUTE, &mutestatus);
		if (mutestatus && (m_AUDIO_MUTE | m_VIDEO_BLACK)) {
			hdmi_writel(hdmi_dev, AV_MUTE,
				    v_AUDIO_MUTE(0) | v_VIDEO_MUTE(0));
		}
		rk616_hdmi_sys_power(hdmi_drv, true);
		rk616_hdmi_sys_power(hdmi_drv, false);
		rk616_hdmi_sys_power(hdmi_drv, true);
		hdmi_writel(hdmi_dev, 0xce, 0x00);
		delay100us();
		hdmi_writel(hdmi_dev, 0xce, 0x01);
	} else {
		hdmi_writel(hdmi_dev, AV_MUTE,
			    v_AUDIO_MUTE(1) | v_VIDEO_MUTE(1));
	}
}

int rk616_hdmi_removed(struct hdmi *hdmi_drv)
{

	dev_info(hdmi_drv->dev, "Removed.\n");
	if (hdmi_drv->hdcp_power_off_cb)
		hdmi_drv->hdcp_power_off_cb();
	rk616_hdmi_set_pwr_mode(hdmi_drv, LOWER_PWR);

	return HDMI_ERROR_SUCESS;
}

void rk616_hdmi_work(struct hdmi *hdmi_drv)
{
	u32 interrupt = 0;
	struct rk_hdmi_device *hdmi_dev = container_of(hdmi_drv,
						       struct rk_hdmi_device,
						       driver);
#ifdef SOC_CONFIG_RK3036
	hdmi_readl(hdmi_dev, HDMI_STATUS,&interrupt);
	if(interrupt){
		hdmi_writel(hdmi_dev, HDMI_STATUS, interrupt);
	}
#else
	hdmi_readl(hdmi_dev, INTERRUPT_STATUS1,&interrupt);
	if(interrupt){
		hdmi_writel(hdmi_dev, INTERRUPT_STATUS1, interrupt);
	}
#endif
	if (interrupt & m_HOTPLUG) {
		if (hdmi_drv->state == HDMI_SLEEP)
			hdmi_drv->state = WAIT_HOTPLUG;

		queue_delayed_work(hdmi_drv->workqueue, &hdmi_drv->delay_work,
				   msecs_to_jiffies(40));

	}

	if (hdmi_drv->hdcp_irq_cb)
		hdmi_drv->hdcp_irq_cb(0);
}

static void rk616_hdmi_reset(struct hdmi *hdmi_drv)
{
	u32 val = 0;
	u32 msk = 0;
	struct rk_hdmi_device *hdmi_dev = container_of(hdmi_drv,
						       struct rk_hdmi_device,
						       driver);

	hdmi_msk_reg(hdmi_dev, SYS_CTRL, m_RST_DIGITAL, v_NOT_RST_DIGITAL);
	delay100us();
	hdmi_msk_reg(hdmi_dev, SYS_CTRL, m_RST_ANALOG, v_NOT_RST_ANALOG);
	delay100us();
	msk = m_REG_CLK_INV | m_REG_CLK_SOURCE | m_POWER | m_INT_POL;
	val = v_REG_CLK_INV | v_REG_CLK_SOURCE_SYS | v_PWR_ON | v_INT_POL_HIGH;
	hdmi_msk_reg(hdmi_dev, SYS_CTRL, msk, val);
#ifdef SOC_CONFIG_RK3036
	hdmi_readl(hdmi_dev, HDMI_STATUS,&val);//enable hpg
	val |= m_MASK_INT_HOTPLUG;
	hdmi_writel(hdmi_dev, HDMI_STATUS,val);
#else
	hdmi_writel(hdmi_dev, INTERRUPT_MASK1, m_INT_HOTPLUG);	
#endif
	rk616_hdmi_set_pwr_mode(hdmi_drv, LOWER_PWR);
}

int rk616_hdmi_initial(struct hdmi *hdmi_drv)
{
	int rc = HDMI_ERROR_SUCESS;
#if !defined(CONFIG_ARCH_RK3026) && !defined(SOC_CONFIG_RK3036)
	struct rk_hdmi_device *hdmi_dev = container_of(hdmi_drv,
						       struct rk_hdmi_device,
						       driver);
	struct mfd_rk616 *rk616_drv = hdmi_dev->rk616_drv;
#endif

	hdmi_drv->pwr_mode = NORMAL;
	hdmi_drv->remove = rk616_hdmi_removed;
	hdmi_drv->control_output = rk616_hdmi_control_output;
	hdmi_drv->config_video = rk616_hdmi_config_video;
	hdmi_drv->config_audio = rk616_hdmi_config_audio;
	hdmi_drv->detect_hotplug = rk616_hdmi_detect_hotplug;
	hdmi_drv->read_edid = rk616_hdmi_read_edid;

#if defined(CONFIG_ARCH_RK3026)
	rk3028_hdmi_reset_pclk();
	rk616_hdmi_reset(hdmi_drv);
#elif defined(SOC_CONFIG_RK3036)
	rk616_hdmi_reset(hdmi_drv);
#else
	hdmi_drv->set_vif = rk616_hdmi_set_vif;
	rk616_hdmi_reset(hdmi_drv);
	rk616_hdmi_init_pol_set(rk616_drv, 0);
#endif

	if (hdmi_drv->hdcp_power_on_cb)
		rc = hdmi_drv->hdcp_power_on_cb();

	return rc;
}
