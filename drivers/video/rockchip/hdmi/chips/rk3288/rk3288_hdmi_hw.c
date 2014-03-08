#include <linux/delay.h>
#include <linux/interrupt.h>
#include "rk3288_hdmi_hw.h"

//i2c master reset
void rk3288_hdmi_i2cm_reset(struct rk3288_hdmi_device *hdmi_dev)
{
	hdmi_msk_reg(hdmi_dev, I2CM_SOFTRSTZ, m_I2CM_SOFTRST, v_I2CM_SOFTRST(0));
	msleep(5);
}

int rk3288_hdmi_detect_hotplug(struct hdmi *hdmi_drv)
{
	struct rk3288_hdmi_device *hdmi_dev = container_of(hdmi_drv, struct rk3288_hdmi_device, driver);
	u32 value = hdmi_readl(hdmi_dev,PHY_STAT0);

	hdmi_dbg(hdmi_drv->dev, "[%s] reg%x value %02x\n", __FUNCTION__, PHY_STAT0, value);

	if(value & m_PHY_HPD)
		return HDMI_HPD_ACTIVED;
	else
		return HDMI_HPD_REMOVED;
}

int rk3288_hdmi_read_edid(struct hdmi *hdmi_drv, int block, unsigned char *buff)
{
	int i = 0,index = 0,interrupt = 0,ret = -1,trytime = 2;
	unsigned long flags;
	struct rk3288_hdmi_device *hdmi_dev = container_of(hdmi_drv, struct rk3288_hdmi_device, driver);

	hdmi_dbg(hdmi_drv->dev, "[%s] block %d\n", __FUNCTION__, block);
	spin_lock_irqsave(&hdmi_drv->irq_lock, flags);
	hdmi_dev->edid_status = 0;
	spin_unlock_irqrestore(&hdmi_drv->irq_lock, flags);

	//Set DDC I2C CLK which devided from DDC_CLK to 100KHz.
	hdmi_writel(hdmi_dev, I2CM_SS_SCL_HCNT_0_ADDR, 0x7a);
	hdmi_writel(hdmi_dev, I2CM_SS_SCL_LCNT_0_ADDR, 0x8d);
	hdmi_msk_reg(hdmi_dev, I2CM_DIV, m_I2CM_FAST_STD_MODE, v_I2CM_FAST_STD_MODE(STANDARD_MODE));	//Set Standard Mode

	//Enable edid interrupt
	hdmi_writel(hdmi_dev, IH_MUTE_I2CM_STAT0, v_SCDC_READREQ_MUTE(0) | v_I2CM_DONE_MUTE(0) | v_I2CM_ERR_MUTE(0));

	//hdmi_writel(hdmi_dev, I2CM_SLAVE,);	//TODO Daisen wait to add!!
	while(trytime--) {
		// Config EDID block and segment addr
		hdmi_writel(hdmi_dev, I2CM_SEGADDR, (block%2) * 0x80);
		hdmi_writel(hdmi_dev, I2CM_SEGPTR, block/2);
		//enable Extended sequential read operation
		hdmi_msk_reg(hdmi_dev, I2CM_OPERATION, m_I2CM_RD8_EXT, v_I2CM_RD8_EXT(1));

		i = 100;
		while(i--)
		{
			spin_lock_irqsave(&hdmi_drv->irq_lock, flags);
			interrupt = hdmi_dev->edid_status;
			hdmi_dev->edid_status = 0;
			spin_unlock_irqrestore(&hdmi_drv->irq_lock, flags);
			if(interrupt & (m_SCDC_READREQ | m_I2CM_DONE | m_I2CM_ERROR))
				break;
			msleep(10);
		}

		if(interrupt & (m_SCDC_READREQ | m_I2CM_DONE)) {
			for(i = 0; i < HDMI_EDID_BLOCK_SIZE/8; i++) {
				for(index = 0; index < 8; index++)
					buff[i] = hdmi_readl(hdmi_dev, I2CM_READ_BUFF0 + index);
			}

			ret = 0;
			hdmi_dbg(hdmi_drv->dev, "[%s] edid read sucess\n", __FUNCTION__);
#ifdef HDMI_DEBUG
			for(i = 0; i < 128; i++) {
				printk("%02x ,", buff[i]);
				if( (i + 1) % 16 == 0)
					printk("\n");
			}
#endif
			break;
		}

		if(interrupt & m_I2CM_ERROR) {
			hdmi_err(hdmi_drv->dev, "[%s] edid read error\n", __FUNCTION__);
			rk3288_hdmi_i2cm_reset(hdmi_dev);
		}

		hdmi_dbg(hdmi_drv->dev, "[%s] edid try times %d\n", __FUNCTION__, trytime);
		msleep(100);
	}

	// Disable edid interrupt
	hdmi_writel(hdmi_dev, IH_MUTE_I2CM_STAT0, v_SCDC_READREQ_MUTE(1) | v_I2CM_DONE_MUTE(1) | v_I2CM_ERR_MUTE(1)); //TODO Daisen HDCP enable it??
	return ret;
}

static void rk3288_hdmi_config_phy(unsigned char vic)
{
	//TODO Daisen wait to add code
}

static void rk3288_hdmi_config_avi(struct hdmi *hdmi_drv, unsigned char vic, unsigned char output_color)
{
	int clolorimetry, aspect_ratio, y1y0;
	struct rk3288_hdmi_device *hdmi_dev = container_of(hdmi_drv, struct rk3288_hdmi_device, driver);

	//Set AVI infoFrame Data byte1
	if(output_color == VIDEO_OUTPUT_YCBCR444)
		y1y0 = AVI_COLOR_MODE_YCBCR444;
	else if(output_color == VIDEO_OUTPUT_YCBCR422)
		y1y0 = AVI_COLOR_MODE_YCBCR422;
	else
		y1y0 = AVI_COLOR_MODE_RGB;

	hdmi_msk_reg(hdmi_dev, FC_AVICONF0, m_FC_ACTIV_FORMAT | m_FC_RGC_YCC, v_FC_RGC_YCC(y1y0) | v_FC_ACTIV_FORMAT(1));

	//Set AVI infoFrame Data byte2
	switch(vic)
	{
		case HDMI_720x480i_60Hz_4_3:
		case HDMI_720x576i_50Hz_4_3:
		case HDMI_720x480p_60Hz_4_3:
		case HDMI_720x576p_50Hz_4_3:
			aspect_ratio = AVI_CODED_FRAME_ASPECT_4_3;
			clolorimetry = AVI_COLORIMETRY_SMPTE_170M;
			break;
		case HDMI_720x480i_60Hz_16_9:
		case HDMI_720x576i_50Hz_16_9:
		case HDMI_720x480p_60Hz_16_9:
		case HDMI_720x576p_50Hz_16_9:
			aspect_ratio = AVI_CODED_FRAME_ASPECT_16_9;
			clolorimetry = AVI_COLORIMETRY_SMPTE_170M;
			break;
		default:
			aspect_ratio = AVI_CODED_FRAME_ASPECT_16_9;
			clolorimetry = AVI_COLORIMETRY_ITU709;
	}

	if(output_color == VIDEO_OUTPUT_RGB444)
		clolorimetry = AVI_COLORIMETRY_NO_DATA;

	hdmi_writel(hdmi_dev, FC_AVICONF1, v_FC_COLORIMETRY(clolorimetry) | v_FC_PIC_ASPEC_RATIO(aspect_ratio) | v_FC_ACT_ASPEC_RATIO(ACTIVE_ASPECT_RATE_SAME_AS_CODED_FRAME));

	//Set AVI infoFrame Data byte3
	hdmi_writel(hdmi_dev, FC_AVICONF2, 0x00);

	//Set AVI infoFrame Data byte4
	hdmi_writel(hdmi_dev, FC_AVIVID, (vic & 0xff));

	//Set AVI infoFrame Data byte5
	hdmi_msk_reg(hdmi_dev, FC_AVICONF3, m_FC_YQ | m_FC_CN, v_FC_YQ(YQ_LIMITED_RANGE) | v_FC_CN(CN_GRAPHICS));

	//TODO Daisen add Calculate AVI InfoFrame ChecKsum
}


static char coeff_csc[][24] = {		//TODO Daisen wait to modify
		//   G		R	    B		Bias
		//   A1    |	A2     |    A3     |	A4    |
		//   B1    |    B2     |    B3     |    B4    |
		//   B1    |    B2     |    B3     |    B4    |
	{	//CSC_RGB_0_255_TO_ITU601_16_235
		0x02, 0x59, 0x01, 0x32, 0x00, 0x75, 0x00, 0x10, 	//Y
		0x11, 0xb6, 0x02, 0x0b, 0x10, 0x55, 0x00, 0x80, 	//Cr
		0x11, 0x5b, 0x10, 0xb0, 0x02, 0x0b, 0x00, 0x80, 	//Cb
	},
	{	//CSC_RGB_0_255_TO_ITU709_16_235
		0x02, 0xdc, 0x00, 0xda, 0x00, 0x4a, 0x00, 0x10, 	//Y
		0x11, 0xdb, 0x02, 0x0b, 0x10, 0x30, 0x00, 0x80,		//Cr
		0x11, 0x93, 0x10, 0x78, 0x02, 0x0b, 0x00, 0x80, 	//Cb
	},
		//Y		Cr	    Cb		Bias
	{	//CSC_ITU601_16_235_TO_RGB_16_235
		0x04, 0x00, 0x05, 0x7c, 0x00, 0x00, 0x02, 0xaf, 	//R
		0x04, 0x00, 0x12, 0xcb, 0x11, 0x58, 0x00, 0x84, 	//G
		0x04, 0x00, 0x00, 0x00, 0x06, 0xee, 0x02, 0xde,		//B
	},
	{	//CSC_ITU709_16_235_TO_RGB_16_235
		0x04, 0x00, 0x06, 0x29, 0x00, 0x00, 0x02, 0xc5, 	//R
		0x04, 0x00, 0x11, 0xd6, 0x10, 0xbb, 0x00, 0x52, 	//G
		0x04, 0x00, 0x00, 0x00, 0x07, 0x44, 0x02, 0xe8, 	//B
	},
	{	//CSC_ITU601_16_235_TO_RGB_0_255
		0x04, 0xa8, 0x05, 0x7c, 0x00, 0x00, 0x02, 0xc2, 	//R
		0x04, 0xa8, 0x12, 0xcb, 0x11, 0x58, 0x00, 0x72, 	//G
		0x04, 0xa8, 0x00, 0x00, 0x06, 0xee, 0x02, 0xf0, 	//B
	},
	{	//CSC_ITU709_16_235_TO_RGB_0_255
		0x04, 0xa8, 0x06, 0x29, 0x00, 0x00, 0x02, 0xd8, 	//R
		0x04, 0xa8, 0x11, 0xd6, 0x10, 0xbb, 0x00, 0x40, 	//G
		0x04, 0xa8, 0x00, 0x00, 0x07, 0x44, 0x02, 0xfb, 	//B
	},

};

#define CSCSCALE	2
static void rk3288_hdmi_config_csc(struct hdmi *hdmi_drv, struct hdmi_video_para *vpara)
{
	int i, mode;
	char *coeff = NULL;
	struct rk3288_hdmi_device *hdmi_dev = container_of(hdmi_drv, struct rk3288_hdmi_device, driver);

	if( ((vpara->input_color == VIDEO_INPUT_COLOR_RGB) && (vpara->output_color == VIDEO_OUTPUT_RGB444)) ||
		((vpara->input_color == VIDEO_INPUT_COLOR_YCBCR) && (vpara->output_color != VIDEO_OUTPUT_RGB444) ))
	{
		return;
	}

	switch(vpara->vic)
	{
		case HDMI_720x480i_60Hz_4_3:
		case HDMI_720x576i_50Hz_4_3:
		case HDMI_720x480p_60Hz_4_3:
		case HDMI_720x576p_50Hz_4_3:
		case HDMI_720x480i_60Hz_16_9:
		case HDMI_720x576i_50Hz_16_9:
		case HDMI_720x480p_60Hz_16_9:
		case HDMI_720x576p_50Hz_16_9:
			if(vpara->input_color == VIDEO_INPUT_COLOR_RGB)
				mode = CSC_RGB_0_255_TO_ITU601_16_235;
			else if(vpara->output_mode == OUTPUT_HDMI)
				mode = CSC_ITU601_16_235_TO_RGB_16_235;
			else
				mode = CSC_ITU601_16_235_TO_RGB_0_255;
			break;
		default:
			if(vpara->input_color == VIDEO_INPUT_COLOR_RGB)
				mode = CSC_RGB_0_255_TO_ITU709_16_235;
			else if(vpara->output_mode == OUTPUT_HDMI)
				mode = CSC_ITU709_16_235_TO_RGB_16_235;
			else
				mode = CSC_ITU709_16_235_TO_RGB_0_255;
			break;
	}

	hdmi_msk_reg(hdmi_dev, CSC_SCALE, m_CSC_SCALE, v_CSC_SCALE(CSCSCALE));
	coeff = coeff_csc[mode];

	for(i = 0; i < 24; i++)
		hdmi_writel(hdmi_dev, CSC_COEF_A1_MSB + i, coeff[i]);

	//enable CSC TODO Daisen wait to add
}


int rk3288_hdmi_config_video(struct hdmi *hdmi_drv, struct hdmi_video_para *vpara)
{
	int input_color = 0;
	int h_act,v_act,h_syncdelay,v_syncdelay,h_sync,v_sync,h_blank,v_blank;
	struct fb_videomode *mode = NULL;
	int vsync_pol = hdmi_drv->lcdc->cur_screen->pin_vsync;
	int hsync_pol = hdmi_drv->lcdc->cur_screen->pin_hsync;
	int de_pol = hdmi_drv->lcdc->cur_screen->pin_den;
	struct rk3288_hdmi_device *hdmi_dev = container_of(hdmi_drv, struct rk3288_hdmi_device, driver);

	switch(vpara->input_color)
	{
		case VIDEO_OUTPUT_RGB444:
			input_color = VIDEO_RGB444_8BIT;
			break;
		case VIDEO_OUTPUT_YCBCR444:
			input_color = VIDEO_YCBCR444_8BIT;
			break;
		case VIDEO_OUTPUT_YCBCR422:
			input_color = VIDEO_YCBCR422_8BIT;
			break;
		default:
			input_color = VIDEO_RGB444_8BIT;
			break;
	}

	//Set Data enable signal from external and set video sample input mapping
	hdmi_msk_reg(hdmi_dev, TX_INVID0, m_INTERNAL_DE_GEN | m_VIDEO_MAPPING, v_INTERNAL_DE_GEN(0) | v_VIDEO_MAPPING(input_color));

	if(hdmi_drv->tmdsclk > 340000000) {	//used for HDMI 2.0 TX
		hdmi_msk_reg(hdmi_dev, FC_INVIDCONF, m_FC_HDCP_KEEPOUT, v_FC_HDCP_KEEPOUT(1));
		hdmi_msk_reg(hdmi_dev, FC_SCRAMBLER_CTRL, m_FC_SCRAMBLE_EN, v_FC_SCRAMBLE_EN(1));
	}

	//Color space convert
	rk3288_hdmi_config_csc(hdmi_drv, vpara);

	//Set video timing
	mode = (struct fb_videomode *)hdmi_vic_to_videomode(vpara->vic);
	if(mode == NULL)
	{
		hdmi_err(hdmi_drv->dev, "[%s] not found vic %d\n", __FUNCTION__, vpara->vic);
		return -ENOENT;
	}

	hdmi_msk_reg(hdmi_dev, FC_INVIDCONF, m_FC_VSYNC_POL | m_FC_HSYNC_POL | m_FC_DE_POL | m_FC_INTERLACE_MODE,
		v_FC_VSYNC_POL(vsync_pol) | v_FC_HSYNC_POL(hsync_pol) | v_FC_DE_POL(de_pol) | v_FC_INTERLACE_MODE(mode->vmode));	//TODO Daisen wait to set m_FC_VBLANK value!!

	h_act = mode->xres;
	hdmi_msk_reg(hdmi_dev, FC_INHACTIV1,m_FC_H_ACTIVE, v_FC_H_ACTIVE(h_act >> 8));
	hdmi_writel(hdmi_dev, FC_INHACTIV0, (h_act & 0xff));

	v_act = mode->yres;
	hdmi_msk_reg(hdmi_dev, FC_INVACTIV1, m_FC_V_ACTIVE, v_FC_V_ACTIVE(v_act >> 8));
	hdmi_writel(hdmi_dev, FC_INVACTIV0, (v_act & 0xff));

	h_blank = mode->hsync_len + mode->left_margin + mode->right_margin;
	hdmi_msk_reg(hdmi_dev, FC_INHBLANK1, m_FC_H_BLANK, v_FC_H_BLANK(h_blank >> 8));
	hdmi_writel(hdmi_dev, FC_INHBLANK0, (h_blank & 0xff));

	v_blank = mode->vsync_len + mode->upper_margin + mode->lower_margin;
	hdmi_writel(hdmi_dev, FC_INVBLANK, (v_blank & 0xff));

	h_syncdelay = mode->left_margin + mode->hsync_len;	//TODO Daisen wait to modify
	hdmi_msk_reg(hdmi_dev, FC_HSYNCINDELAY1, m_FC_H_SYNCFP, v_FC_H_SYNCFP(h_syncdelay >> 8));
	hdmi_writel(hdmi_dev, FC_HSYNCINDELAY0, (h_syncdelay & 0xff));

	v_syncdelay = mode->upper_margin + mode->vsync_len;	//TODO Daisen wait to modify
	hdmi_writel(hdmi_dev, FC_VSYNCINDELAY, (v_syncdelay & 0xff));

	h_sync = mode->hsync_len;
	hdmi_msk_reg(hdmi_dev, FC_HSYNCINWIDTH1, m_FC_HSYNC, v_FC_HSYNC(h_sync >> 8));
	hdmi_writel(hdmi_dev, FC_HSYNCINWIDTH0, (h_sync & 0xff));

	v_sync = mode->vsync_len;
	hdmi_writel(hdmi_dev, FC_VSYNCINWIDTH, (v_sync & 0xff));

	//Set HDMI/DVI mode
	hdmi_msk_reg(hdmi_dev, FC_INVIDCONF, m_FC_HDMI_DVI, v_FC_HDMI_DVI(vpara->output_mode));
	if(vpara->output_mode == OUTPUT_HDMI) {
		rk3288_hdmi_config_avi(hdmi_drv, vpara->vic, vpara->output_color);
		hdmi_dbg(hdmi_drv->dev, "[%s] sucess output HDMI.\n", __FUNCTION__);
	}
	else {
		hdmi_dbg(hdmi_drv->dev, "[%s] sucess output DVI.\n", __FUNCTION__);
	}

	rk3288_hdmi_config_phy(vpara->vic);
	rk3288_hdmi_control_output(hdmi_drv, 0);
	return 0;
}

static void rk3288_hdmi_config_aai(struct hdmi *hdmi_drv, struct hdmi_audio *audio)
{
	//struct rk3288_hdmi_device *hdmi_dev = container_of(hdmi_drv, struct rk3288_hdmi_device, driver);

	//hdmi_writel(hdmi_dev, FC_AUDICONF0, m_FC_CHN_CNT | m_FC_CODING_TYEP, v_FC_CHN_CNT(audio->channel) | v_FC_CODING_TYEP());

	//TODO Daisen wait to add
}

int rk3288_hdmi_config_audio(struct hdmi *hdmi_drv, struct hdmi_audio *audio)
{
	int word_length = 0,channel = 0,N = 0;
	struct rk3288_hdmi_device *hdmi_dev = container_of(hdmi_drv, struct rk3288_hdmi_device, driver);

	if(audio->channel < 3)	//TODO Daisen
		channel = I2S_CHANNEL_1_2;
	else if(audio->channel < 5)
		channel = I2S_CHANNEL_3_4;
	else if(audio->channel < 7)
		channel = I2S_CHANNEL_5_6;
	else
		channel = I2S_CHANNEL_7_8;

	switch(audio->rate)
	{
		case HDMI_AUDIO_FS_32000:
			if(hdmi_drv->tmdsclk >= 594000000)
				N = N_32K_HIGHCLK;
			else if(hdmi_drv->tmdsclk == 297000000)
				N = N_32K_MIDCLK;
			else
				N = N_32K_LOWCLK;
			break;
		case HDMI_AUDIO_FS_44100:
			if(hdmi_drv->tmdsclk >= 594000000)
				N = N_441K_HIGHCLK;
			else if(hdmi_drv->tmdsclk == 297000000)
				N = N_441K_MIDCLK;
			else
				N = N_441K_LOWCLK;
			break;
		case HDMI_AUDIO_FS_48000:
			if(hdmi_drv->tmdsclk >= 594000000)
				N = N_48K_HIGHCLK;
			else if(hdmi_drv->tmdsclk == 297000000)
				N = N_48K_MIDCLK;
			else
				N = N_48K_LOWCLK;
			break;
		case HDMI_AUDIO_FS_88200:
			if(hdmi_drv->tmdsclk >= 594000000)
				N = N_882K_HIGHCLK;
			else if(hdmi_drv->tmdsclk == 297000000)
				N = N_882K_MIDCLK;
			else
				N = N_882K_LOWCLK;
			break;
		case HDMI_AUDIO_FS_96000:
			if(hdmi_drv->tmdsclk >= 594000000)
				N = N_96K_HIGHCLK;
			else if(hdmi_drv->tmdsclk == 297000000)
				N = N_96K_MIDCLK;
			else
				N = N_96K_LOWCLK;
			break;
		case HDMI_AUDIO_FS_176400:
			if(hdmi_drv->tmdsclk >= 594000000)
				N = N_1764K_HIGHCLK;
			else if(hdmi_drv->tmdsclk == 297000000)
				N = N_1764K_MIDCLK;
			else
				N = N_1764K_LOWCLK;
			break;
		case HDMI_AUDIO_FS_192000:
			if(hdmi_drv->tmdsclk >= 594000000)
				N = N_192K_HIGHCLK;
			else if(hdmi_drv->tmdsclk == 297000000)
				N = N_192K_MIDCLK;
			else
				N = N_192K_LOWCLK;
			break;
		default:
			hdmi_err(hdmi_drv->dev, "[%s] not support such sample rate %d\n", __FUNCTION__, audio->rate);
			return -ENOENT;
	}

	switch(audio->word_length)
	{
		case HDMI_AUDIO_WORD_LENGTH_16bit:
			word_length = I2S_16BIT_SAMPLE;
			break;
		case HDMI_AUDIO_WORD_LENGTH_20bit:
			word_length = I2S_20BIT_SAMPLE;
			break;
		case HDMI_AUDIO_WORD_LENGTH_24bit:
			word_length = I2S_24BIT_SAMPLE;
			break;
		default:
			word_length = I2S_16BIT_SAMPLE;
	}

	if(hdmi_drv->audio.type == INPUT_SPDIF) {
		hdmi_msk_reg(hdmi_dev, AUD_CONF0, m_I2S_SEL, v_I2S_SEL(AUDIO_SPDIF_GPA));
		hdmi_msk_reg(hdmi_dev, AUD_SPDIF1, m_SET_NLPCM | m_SPDIF_WIDTH, v_SET_NLPCM(PCM_LINEAR) | v_SPDIF_WIDTH(word_length));
	}
	else {
		hdmi_msk_reg(hdmi_dev, AUD_CONF0, m_I2S_SEL | m_I2S_IN_EN, v_I2S_SEL(AUDIO_I2S) | v_I2S_IN_EN(channel));
		hdmi_writel(hdmi_dev, AUD_CONF1, v_I2S_MODE(I2S_STANDARD_MODE) | v_I2S_WIDTH(word_length));
	}

	hdmi_msk_reg(hdmi_dev, AUD_INPUTCLKFS, m_LFS_FACTOR, v_LFS_FACTOR(FS_128));

	//Set N value
	hdmi_msk_reg(hdmi_dev, AUD_N3, m_AUD_N3, v_AUD_N3(N >> 16));
	hdmi_writel(hdmi_dev, AUD_N2, (N >> 8) & 0xff);
	hdmi_writel(hdmi_dev, AUD_N1, N & 0xff);
	//Set Automatic CTS generation
	hdmi_msk_reg(hdmi_dev, AUD_CTS3, m_CTS_MANUAL, v_CTS_MANUAL(0));

	rk3288_hdmi_config_aai(hdmi_drv, audio);

	return 0;
}

void rk3288_hdmi_control_output(struct hdmi *hdmi_drv, int enable)
{

}

int rk3288_hdmi_removed(struct hdmi *hdmi_drv)
{
	return 0;
}

int rk3288_hdmi_initial(struct hdmi *hdmi_drv)
{
        int rc = HDMI_ERROR_SUCESS;

        hdmi_drv->remove = rk3288_hdmi_removed;
        hdmi_drv->control_output = rk3288_hdmi_control_output;
        hdmi_drv->config_video = rk3288_hdmi_config_video;
        hdmi_drv->config_audio = rk3288_hdmi_config_audio;
        hdmi_drv->detect_hotplug = rk3288_hdmi_detect_hotplug;
        hdmi_drv->read_edid = rk3288_hdmi_read_edid;

        return rc;
}

irqreturn_t hdmi_irq(int irq, void *priv)
{
	struct hdmi *hdmi_drv = (struct hdmi *)priv;
	struct rk3288_hdmi_device *hdmi_dev = container_of(hdmi_drv, struct rk3288_hdmi_device, driver);
	int phy_int,i2cm_int,cec_int,hdcp_int;

	phy_int = hdmi_readl(hdmi_dev, IH_PHY_STAT0);
	i2cm_int = hdmi_readl(hdmi_dev, IH_I2CM_STAT0);
	cec_int = hdmi_readl(hdmi_dev, IH_CEC_STAT0);
	hdcp_int = hdmi_readl(hdmi_dev, A_APIINTSTAT);

	//clear interrupt
	hdmi_writel(hdmi_dev, IH_PHY_STAT0, phy_int);
	hdmi_writel(hdmi_dev, IH_I2CM_STAT0, i2cm_int);
	hdmi_writel(hdmi_dev, IH_CEC_STAT0, cec_int);
	hdmi_writel(hdmi_dev, A_APIINTCLR, hdcp_int);

	//HPD
	if(phy_int & m_HPD) {
		if(hdmi_drv->state == HDMI_SLEEP)
			hdmi_drv->state = WAIT_HOTPLUG;
		queue_delayed_work(hdmi_drv->workqueue, &hdmi_drv->delay_work, msecs_to_jiffies(5));
	}

	//EDID Ready
	if(i2cm_int & (m_SCDC_READREQ | m_I2CM_DONE | m_I2CM_ERROR)) {
		spin_lock(&hdmi_drv->irq_lock);
		hdmi_dev->edid_status = i2cm_int;
		spin_unlock(&hdmi_drv->irq_lock);
	}

	//CEC
	if(cec_int) {	//TODO Daisen wait to modify
	}

	//HDCP
	if(hdmi_drv->hdcp_irq_cb)
		hdmi_drv->hdcp_irq_cb(hdcp_int);

	return IRQ_HANDLED;
}

