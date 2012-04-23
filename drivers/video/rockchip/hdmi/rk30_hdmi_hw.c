#include <linux/delay.h>
#include <asm/io.h>
#include <mach/io.h>
#include "rk30_hdmi.h"
#include "rk30_hdmi_hw.h"

static char edid_result = 0;

static inline void delay100us(void)
{
	msleep(1);
}

static void rk30_hdmi_set_pwr_mode(int mode)
{
	if(hdmi->pwr_mode == mode)
		return;
	hdmi_dbg(hdmi->dev, "[%s] mode %d\n", __FUNCTION__, mode);	
	switch(mode)
	{
		case PWR_SAVE_MODE_A:
			HDMIWrReg(SYS_CTRL, 0x10);
			break;
		case PWR_SAVE_MODE_B:
			HDMIWrReg(SYS_CTRL, 0x20);
			break;
		case PWR_SAVE_MODE_D:
			// reset PLL A&B
			HDMIWrReg(SYS_CTRL, 0x4C);
			delay100us();
			// release PLL A reset
			HDMIWrReg(SYS_CTRL, 0x48);
			delay100us();
			// release PLL B reset
			HDMIWrReg(SYS_CTRL, 0x40);
			break;
		case PWR_SAVE_MODE_E:
			HDMIWrReg(SYS_CTRL, 0x80);
			break;
	}
	hdmi->pwr_mode = mode;
	if(mode != PWR_SAVE_MODE_A)
		msleep(10);
	hdmi_dbg(hdmi->dev, "[%s] curmode %02x\n", __FUNCTION__, HDMIRdReg(SYS_CTRL));
}

int rk30_hdmi_detect_hotplug(void)
{
	int value =	HDMIRdReg(HPD_MENS_STA);
	
	hdmi_dbg(hdmi->dev, "[%s] value %02x\n", __FUNCTION__, value);
	value &= m_HOTPLUG_STATUS | m_MSEN_STATUS;
	if(value  == (m_HOTPLUG_STATUS | m_MSEN_STATUS) )
		return HDMI_HPD_ACTIVED;
	else if(value)
		return HDMI_HPD_INSERT;
	else
		return HDMI_HPD_REMOVED;
}

#define HDMI_EDID_DDC_CLK	100000
int rk30_hdmi_read_edid(int block, unsigned char *buff)
{
	int value, ret = -1, ddc_bus_freq = 0;
	char interrupt = 0, trytime = 2;
	
	hdmi_dbg(hdmi->dev, "[%s] block %d\n", __FUNCTION__, block);
	spin_lock(&hdmi->irq_lock);
	edid_result = 0;
	spin_unlock(&hdmi->irq_lock);
	//Before Phy parameter was set, DDC_CLK is equal to PLLA freq which is 24MHz.
	//Set DDC I2C CLK which devided from DDC_CLK to 100KHz.
	ddc_bus_freq = (24000000/HDMI_EDID_DDC_CLK)/4;
	HDMIWrReg(DDC_BUS_FREQ_L, ddc_bus_freq & 0xFF);
	HDMIWrReg(DDC_BUS_FREQ_H, (ddc_bus_freq >> 8) & 0xFF);
	
	// Enable edid interrupt
	HDMIWrReg(INTR_MASK1, m_INT_HOTPLUG | m_INT_MSENS | m_INT_EDID_ERR | m_INT_EDID_READY);
	
	while(trytime--) {
		// Config EDID block and segment addr
		HDMIWrReg(EDID_WORD_ADDR, (block%2) * 0x80);
		HDMIWrReg(EDID_SEGMENT_POINTER, block/2);	
	
		value = 100;
		while(value--)
		{
			spin_lock(&hdmi->irq_lock);
			interrupt = edid_result;
			spin_unlock(&hdmi->irq_lock);
//			hdmi_dbg(hdmi->dev, "[%s] interrupt %02x value %d\n", __FUNCTION__, interrupt, value);
			if(interrupt & (m_INT_EDID_ERR | m_INT_EDID_READY))
				break;
			msleep(10);
		}
		hdmi_dbg(hdmi->dev, "[%s] edid read value %d\n", __FUNCTION__, value);
		if(interrupt & m_INT_EDID_READY)
		{
			for(value = 0; value < HDMI_EDID_BLOCK_SIZE; value++) 
				buff[value] = HDMIRdReg(DDC_READ_FIFO_ADDR);
			ret = 0;
			
			hdmi_dbg(hdmi->dev, "[%s] edid read sucess\n", __FUNCTION__);
#if 0
			for(value = 0; value < 128; value++) {
				printk("%02x ,", buff[value]);
				if( (value + 1) % 8 == 0)
					printk("\n");
			}
#endif
			break;
		}		
		if(interrupt & m_INT_EDID_ERR)
			hdmi_dbg(hdmi->dev, "[%s] edid read error\n", __FUNCTION__);

	}
	// Disable edid interrupt
	HDMIWrReg(INTR_MASK1, m_INT_HOTPLUG | m_INT_MSENS);
	msleep(100);
	return ret;
}

static inline void rk30_hdmi_config_phy_reg(int reg, int value)
{
	HDMIWrReg(reg, value);
	HDMIWrReg(SYS_CTRL, 0x2C);
	delay100us();
	HDMIWrReg(SYS_CTRL, 0x20);
	msleep(1);
}

static void rk30_hdmi_config_phy(unsigned char vic)
{
	HDMIWrReg(DEEP_COLOR_MODE, 0x22);	// tmds frequency same as input dlck
	rk30_hdmi_set_pwr_mode(PWR_SAVE_MODE_B);
	switch(vic)
	{
		case HDMI_1920x1080p_60Hz:
		case HDMI_1920x1080p_50Hz:
		#if 0	
			rk30_hdmi_config_phy_reg(0x158, 0x0E);
			rk30_hdmi_config_phy_reg(0x15c, 0x00);
			rk30_hdmi_config_phy_reg(0x160, 0x60);
			rk30_hdmi_config_phy_reg(0x164, 0x00);
			rk30_hdmi_config_phy_reg(0x168, 0xDA);
			rk30_hdmi_config_phy_reg(0x16c, 0xA2);
			rk30_hdmi_config_phy_reg(0x170, 0x0e);
			rk30_hdmi_config_phy_reg(0x174, 0x22);
			rk30_hdmi_config_phy_reg(0x178, 0x00);
		#else	
			rk30_hdmi_config_phy_reg(0x158, 0x0E);
			rk30_hdmi_config_phy_reg(0x15c, 0x00);
			rk30_hdmi_config_phy_reg(0x160, 0x77);
			rk30_hdmi_config_phy_reg(0x164, 0x00);
			rk30_hdmi_config_phy_reg(0x168, 0xDA);
			rk30_hdmi_config_phy_reg(0x16c, 0xA1);
			rk30_hdmi_config_phy_reg(0x170, 0x06);
			rk30_hdmi_config_phy_reg(0x174, 0x22);
			rk30_hdmi_config_phy_reg(0x178, 0x00);
		#endif
			break;
			
		case HDMI_1920x1080i_60Hz:
		case HDMI_1920x1080i_50Hz:
		case HDMI_1280x720p_60Hz:
		case HDMI_1280x720p_50Hz:
			rk30_hdmi_config_phy_reg(0x158, 0x06);
			rk30_hdmi_config_phy_reg(0x15c, 0x00);
			rk30_hdmi_config_phy_reg(0x160, 0x60);
			rk30_hdmi_config_phy_reg(0x164, 0x00);
			rk30_hdmi_config_phy_reg(0x168, 0xCA);
			rk30_hdmi_config_phy_reg(0x16c, 0xA3);
			rk30_hdmi_config_phy_reg(0x170, 0x0e);
			rk30_hdmi_config_phy_reg(0x174, 0x20);
			rk30_hdmi_config_phy_reg(0x178, 0x00);
			break;
			
		case HDMI_720x576p_50Hz_4_3:
		case HDMI_720x576p_50Hz_16_9:
		case HDMI_720x480p_60Hz_4_3:
		case HDMI_720x480p_60Hz_16_9:
			rk30_hdmi_config_phy_reg(0x158, 0x02);
			rk30_hdmi_config_phy_reg(0x15c, 0x00);
			rk30_hdmi_config_phy_reg(0x160, 0x60);
			rk30_hdmi_config_phy_reg(0x164, 0x00);
			rk30_hdmi_config_phy_reg(0x168, 0xC2);
			rk30_hdmi_config_phy_reg(0x16c, 0xA2);
			rk30_hdmi_config_phy_reg(0x170, 0x0e);
			rk30_hdmi_config_phy_reg(0x174, 0x20);
			rk30_hdmi_config_phy_reg(0x178, 0x00);
			break;
		default:
			hdmi_dbg(hdmi->dev, "not support such vic %d\n", vic);
			break;
	}
}

static void rk30_hdmi_config_avi(unsigned char vic, unsigned char output_color)
{
	int i;
	char info[SIZE_AVI_INFOFRAME];
	
	HDMIWrReg(CONTROL_PACKET_BUF_INDEX, INFOFRAME_AVI);
	HDMIWrReg(CONTROL_PACKET_HB0, 0x82);
	HDMIWrReg(CONTROL_PACKET_HB1, 0x2);
	HDMIWrReg(CONTROL_PACKET_HB2, 0x0D);
	memset(info, 0, SIZE_AVI_INFOFRAME);
	
	info[1] = (AVI_COLOR_MODE_RGB << 5);
	info[2] = (AVI_COLORIMETRY_NO_DATA << 6) | (AVI_CODED_FRAME_ASPECT_NO_DATA << 4) | ACTIVE_ASPECT_RATE_SAME_AS_CODED_FRAME;
	info[3] = 0;
	info[4] = vic;
	info[5] = 0;

	// Calculate AVI InfoFrame ChecKsum
	info[0] = 0x82 + 0x02 +0x0D;
	for (i = 1; i < SIZE_AVI_INFOFRAME; i++)
	{
    	info[0] += info[i];
	}
	info[0] = 0x100 - info[0];
	
	for(i = 0; i < SIZE_AVI_INFOFRAME; i++)
		HDMIWrReg(CONTROL_PACKET_PB_ADDR + i*4, info[i]);
}

int rk30_hdmi_config_video(int vic, int output_color, int output_mode)
{
	int value;
	struct fb_videomode *mode;
	
	hdmi_dbg(hdmi->dev, "[%s]\n", __FUNCTION__);
	
	if(hdmi->pwr_mode == PWR_SAVE_MODE_E)
		rk30_hdmi_set_pwr_mode(PWR_SAVE_MODE_D);
	if(hdmi->pwr_mode == PWR_SAVE_MODE_D || hdmi->pwr_mode == PWR_SAVE_MODE_A)
		rk30_hdmi_set_pwr_mode(PWR_SAVE_MODE_B);
		
	// Input video mode is RGB24bit, Data enable signal from external
	HDMIMskReg(value, AV_CTRL1, m_INPUT_VIDEO_MODE | m_DE_SIGNAL_SELECT, \
		v_INPUT_VIDEO_MODE(VIDEO_INPUT_RGB_YCBCR_444) | EXTERNAL_DE)	
	HDMIMskReg(value, VIDEO_CTRL1, m_VIDEO_OUTPUT_MODE | m_VIDEO_INPUT_DEPTH | m_VIDEO_INPUT_COLOR_MODE, \
		v_VIDEO_OUTPUT_MODE(output_color) | v_VIDEO_INPUT_DEPTH(VIDEO_INPUT_DEPTH_8BIT) | VIDEO_INPUT_COLOR_RGB)
	
	// Set HDMI Mode
	HDMIWrReg(HDCP_CTRL, v_HDMI_DVI(output_mode));

	// Set ext video
	mode = (struct fb_videomode *)hdmi_vic_to_videomode(vic);
	if(mode == NULL)
	{
		hdmi_dbg(hdmi->dev, "[%s] not found vic %d\n", __FUNCTION__, vic);
		return -ENOENT;
	}
	value = v_EXT_VIDEO_ENABLE(1) | v_INTERLACE(mode->vmode);
	if(mode->sync & FB_SYNC_HOR_HIGH_ACT)
		value |= v_HSYNC_POLARITY(1);
	if(mode->sync | FB_SYNC_VERT_HIGH_ACT)
		value |= v_VSYNC_POLARITY(1);
	HDMIWrReg(EXT_VIDEO_PARA, value);
	value = mode->left_margin + mode->xres + mode->right_margin + mode->hsync_len;
	HDMIWrReg(EXT_VIDEO_PARA_HTOTAL_L, value & 0xFF);
	HDMIWrReg(EXT_VIDEO_PARA_HTOTAL_H, (value >> 8) & 0xFF);
	
	value = mode->left_margin + mode->right_margin + mode->hsync_len;
	HDMIWrReg(EXT_VIDEO_PARA_HBLANK_L, value & 0xFF);
	HDMIWrReg(EXT_VIDEO_PARA_HBLANK_H, (value >> 8) & 0xFF);
	
	value = mode->left_margin + mode->hsync_len;
	HDMIWrReg(EXT_VIDEO_PARA_HDELAY_L, value & 0xFF);
	HDMIWrReg(EXT_VIDEO_PARA_HDELAY_H, (value >> 8) & 0xFF);
	
	value = mode->hsync_len;
	HDMIWrReg(EXT_VIDEO_PARA_HSYNCWIDTH_L, value & 0xFF);
	HDMIWrReg(EXT_VIDEO_PARA_HSYNCWIDTH_H, (value >> 8) & 0xFF);
	
	value = mode->upper_margin + mode->yres + mode->lower_margin + mode->vsync_len;
	HDMIWrReg(EXT_VIDEO_PARA_VTOTAL_L, value & 0xFF);
	HDMIWrReg(EXT_VIDEO_PARA_VTOTAL_H, (value >> 8) & 0xFF);
	
	value = mode->upper_margin + mode->vsync_len + mode->lower_margin;
	HDMIWrReg(EXT_VIDEO_PARA_VBLANK_L, value & 0xFF);
	
	value = mode->upper_margin + mode->vsync_len;
	HDMIWrReg(EXT_VIDEO_PARA_VDELAY, value & 0xFF);
	
	value = mode->vsync_len;
	HDMIWrReg(EXT_VIDEO_PARA_VSYNCWIDTH, value & 0xFF);
	
	if(output_mode == OUTPUT_HDMI) {
		rk30_hdmi_config_avi(vic, output_mode);
		hdmi_dbg(hdmi->dev, "[%s] sucess output HDMI.\n", __FUNCTION__);
	}
	else {
		hdmi_dbg(hdmi->dev, "[%s] sucess output DVI.\n", __FUNCTION__);	
	}
	rk30_hdmi_config_phy(vic);
	rk30_hdmi_control_output(0);
	return 0;
}

static void rk30_hdmi_config_aai(void)
{
	int i;
	char info[SIZE_AUDIO_INFOFRAME];
	
	memset(info, 0, SIZE_AUDIO_INFOFRAME);
	
	info[0] = 0x84;
	info[1] = 0x01;
	info[2] = 0x0A;
	
	info[3] = info[0] + info[1] + info[2];	
	for (i = 4; i < SIZE_AUDIO_INFOFRAME; i++)
    	info[3] += info[i];
    	
	info[3] = 0x100 - info[3];
	
	HDMIWrReg(CONTROL_PACKET_BUF_INDEX, INFOFRAME_AAI);
	for(i = 0; i < SIZE_AUDIO_INFOFRAME; i++)
		HDMIWrReg(CONTROL_PACKET_HB0 + i*4, info[i]);
}

int rk30_hdmi_config_audio(struct hdmi_audio *audio)
{
	int value, rate, N;
	char word_length, channel;
	
	if(audio->channel < 3)
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
			rate = AUDIO_32K;
			N = N_32K;
			break;
		case HDMI_AUDIO_FS_44100:
			rate = AUDIO_441K;
			N = N_441K;
			break;
		case HDMI_AUDIO_FS_48000:
			rate = AUDIO_48K;
			N = N_48K;
			break;
		case HDMI_AUDIO_FS_88200:
			rate = AUDIO_882K;
			N = N_882K;
			break;
		case HDMI_AUDIO_FS_96000:
			rate = AUDIO_96K;
			N = N_96K;
			break;
		case HDMI_AUDIO_FS_176400:
			rate = AUDIO_1764K;
			N = N_1764K;
			break;
		case HDMI_AUDIO_FS_192000:
			rate = AUDIO_192K;
			N = N_192K;
			break;
		default:
			dev_err(hdmi->dev, "[%s] not support such sample rate %d\n", __FUNCTION__, audio->rate);
			return -ENOENT;
	}
	switch(audio->word_length)
	{
		case HDMI_AUDIO_WORD_LENGTH_16bit:
			word_length = 0x02;
			break;
		case HDMI_AUDIO_WORD_LENGTH_20bit:
			word_length = 0x0a;
			break;
		case HDMI_AUDIO_WORD_LENGTH_24bit:
			word_length = 0x0b;
			break;
		default:
			dev_err(hdmi->dev, "[%s] not support such word length %d\n", __FUNCTION__, audio->word_length);
			return -ENOENT;
	}
	//set_audio_if I2S
	HDMIWrReg(AUDIO_CTRL1, 0x00); //internal CTS, disable down sample, i2s input, disable MCLK
	HDMIWrReg(AUDIO_CTRL2, 0x40); 
	HDMIWrReg(I2S_AUDIO_CTRL, v_I2S_MODE(I2S_MODE_STANDARD) | v_I2S_CHANNEL(channel) );	
	HDMIWrReg(I2S_INPUT_SWAP, 0x00); //no swap
	HDMIMskReg(value, AV_CTRL1, m_AUDIO_SAMPLE_RATE, v_AUDIO_SAMPLE_RATE(rate))	
	HDMIWrReg(SRC_NUM_AUDIO_LEN, word_length);
		
    //Set N value 6144, fs=48kHz
    HDMIWrReg(N_1, N & 0xFF);
    HDMIWrReg(N_2, (N >> 8) & 0xFF);
    HDMIWrReg(LR_SWAP_N3, (N >> 16) & 0x0F); 
    
    rk30_hdmi_config_aai();
    return 0;
}

static void rk30_hdmi_audio_reset(void)
{
	int value;
	
	HDMIMskReg(value, VIDEO_SETTING2, m_AUDIO_RESET, AUDIO_CAPTURE_RESET)
	msleep(1);
	HDMIMskReg(value, VIDEO_SETTING2, m_AUDIO_RESET, 0)
}

void rk30_hdmi_control_output(int enable)
{
	hdmi_dbg(hdmi->dev, "[%s] %d\n", __FUNCTION__, enable);
	if(enable == 0) {
		HDMIWrReg(VIDEO_SETTING2, 0x03);
	}
	else {
		//  Switch to power save mode_d
		rk30_hdmi_set_pwr_mode(PWR_SAVE_MODE_D);
		//  Switch to power save mode_e
		rk30_hdmi_set_pwr_mode(PWR_SAVE_MODE_E);
		HDMIWrReg(VIDEO_SETTING2, 0x00);
		rk30_hdmi_audio_reset();
	}
}

int rk30_hdmi_removed(void)
{
	if(hdmi->pwr_mode == PWR_SAVE_MODE_E)
	{
		HDMIWrReg(VIDEO_SETTING2, 0x00);
		rk30_hdmi_audio_reset();
		rk30_hdmi_set_pwr_mode(PWR_SAVE_MODE_D);
	}
	if(hdmi->pwr_mode == PWR_SAVE_MODE_D)
		rk30_hdmi_set_pwr_mode(PWR_SAVE_MODE_B);
	if(hdmi->pwr_mode == PWR_SAVE_MODE_B && hdmi->state == HDMI_SLEEP)
	{
		HDMIWrReg(INTR_MASK1, m_INT_HOTPLUG | m_INT_MSENS);
		HDMIWrReg(INTR_MASK2, 0);
		HDMIWrReg(INTR_MASK3, 0);
		HDMIWrReg(INTR_MASK4, 0);
		rk30_hdmi_set_pwr_mode(PWR_SAVE_MODE_A);
	}	
	return HDMI_ERROR_SUCESS;
}


irqreturn_t hdmi_irq(int irq, void *priv)
{		
	char interrupt1 = 0, interrupt2 = 0, interrupt3 = 0, interrupt4 = 0;
	
	spin_lock(&hdmi->irq_lock);
	if(hdmi->pwr_mode == PWR_SAVE_MODE_A)
	{
		hdmi_dbg(hdmi->dev, "hdmi irq wake up\n");
		HDMIWrReg(SYS_CTRL, 0x20);
		hdmi->pwr_mode = PWR_SAVE_MODE_B;
		
		// HDMI was inserted when system is sleeping, irq was triggered only once
		// when wake up. So we need to check hotplug status.
		if(HDMIRdReg(HPD_MENS_STA) & (m_HOTPLUG_STATUS | m_MSEN_STATUS)) {			
			queue_delayed_work(hdmi->workqueue, &hdmi->delay_work, msecs_to_jiffies(10));
		}
	}
	else
	{
		interrupt1 = HDMIRdReg(INTR_STATUS1);
		interrupt2 = HDMIRdReg(INTR_STATUS2);
		interrupt3 = HDMIRdReg(INTR_STATUS3);
		interrupt4 = HDMIRdReg(INTR_STATUS4);
		HDMIWrReg(INTR_STATUS1, interrupt1);
		HDMIWrReg(INTR_STATUS2, interrupt2);
		HDMIWrReg(INTR_STATUS3, interrupt3);
		HDMIWrReg(INTR_STATUS4, interrupt4);
#if 0
		hdmi_dbg(hdmi->dev, "[%s] interrupt1 %02x interrupt2 %02x interrupt3 %02x interrupt4 %02x\n",\
			 __FUNCTION__, interrupt1, interrupt2, interrupt3, interrupt4);
#endif
		if(interrupt1 & (m_INT_HOTPLUG | m_INT_MSENS))
		{
			if(hdmi->state == HDMI_SLEEP)
				hdmi->state = WAIT_HOTPLUG;
			interrupt1 &= ~(m_INT_HOTPLUG | m_INT_MSENS);
			queue_delayed_work(hdmi->workqueue, &hdmi->delay_work, msecs_to_jiffies(10));	
		}
		else if(interrupt1 & (m_INT_EDID_READY | m_INT_EDID_ERR))
			edid_result = interrupt1;
		else if(hdmi->state == HDMI_SLEEP) {
			hdmi_dbg(hdmi->dev, "hdmi return to sleep mode\n");
			HDMIWrReg(SYS_CTRL, 0x10);
			hdmi->pwr_mode = PWR_SAVE_MODE_A;
		}
	}
	spin_unlock(&hdmi->irq_lock);
	return IRQ_HANDLED;
}

