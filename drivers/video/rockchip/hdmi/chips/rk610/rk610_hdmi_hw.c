#include <linux/delay.h>
#include "rk610_hdmi.h"
#include "rk610_hdmi_hw.h"
#include <asm/atomic.h>

static atomic_t edid_ready;

static int rk610_hdmi_i2c_read_reg(char reg, char *val)
{
	if(i2c_master_reg8_recv(rk610_hdmi->client, reg, val, 1, 100*1000) > 0)
		return  0;
	else {
		printk("[%s] reg %02x error\n", __FUNCTION__, reg);
		return -EINVAL;
	}
}
static int rk610_hdmi_i2c_write_reg(char reg, char val)
{
	return i2c_master_reg8_send(rk610_hdmi->client, reg, &val, 1, 100*1000) > 0? 0: -EINVAL;
}

#define HDMIWrReg	rk610_hdmi_i2c_write_reg

int rk610_hdmi_sys_init(void)
{
	// System power power off
	HDMIWrReg(SYS_CTRL, v_REG_CLK_SOURCE_IIS | v_PWR_OFF | v_INT_POL_HIGH);
	
	//Synchronize analog module.
//	HDMIWrReg(PHY_SYNC, 0x00);
//	HDMIWrReg(PHY_SYNC, 0x01);
	
	// set hdmi phy parameters
	// driver mode
	HDMIWrReg(PHY_DRIVER, v_MAIN_DRIVER(8)| v_PRE_DRIVER(0) | v_TX_ENABLE(0));
//	HDMIWrReg(PHY_PRE_EMPHASIS, 0x04);
	HDMIWrReg(PHY_PRE_EMPHASIS, v_PRE_EMPHASIS(0) | v_TMDS_PWRDOWN(1));	//Driver power down	
	// pll mode
	HDMIWrReg(0xe8, 0x10);
	HDMIWrReg(0xe6, 0x2c);

	HDMIWrReg(PHY_PLL_CTRL, v_PLL_DISABLE(1) | v_PLL_RESET(1) | v_TMDS_RESET(1));
	HDMIWrReg(PHY_PLL_LDO_PWR, v_LDO_PWR_DOWN(1));
	HDMIWrReg(PHY_BANDGAP_PWR, v_BANDGAP_PWR_DOWN);

	// Enable Hotplug interrupt
	HDMIWrReg(INTERRUPT_MASK1, m_INT_HOTPLUG);
	return HDMI_ERROR_SUCESS;
}

void rk610_hdmi_interrupt()
{
	char interrupt = 0;
	
	if(rk610_hdmi_i2c_read_reg(INTERRUPT_STATUS1, &interrupt))
		return;
		
	HDMIWrReg(INTERRUPT_STATUS1, interrupt);
	
	if(interrupt)
		HDMIWrReg(INTERRUPT_STATUS1, interrupt);
	
	if(interrupt & m_INT_HOTPLUG) {
		hdmi_dbg(hdmi->dev, "%s interrupt %02x\n", __FUNCTION__, interrupt);
		if(hdmi->state == HDMI_SLEEP)
			hdmi->state = WAIT_HOTPLUG;
		queue_delayed_work(hdmi->workqueue, &hdmi->delay_work, msecs_to_jiffies(10));	
	}
	else if(interrupt & m_INT_EDID_READY) {
		atomic_set(&edid_ready, 1);
	}
}

int rk610_hdmi_sys_detect_hpd(void)
{
	char hdmi_status = 0;

	#ifdef HDMI_USE_IRQ
	rk610_hdmi_i2c_read_reg(INTERRUPT_STATUS1, &hdmi_status);
	HDMIWrReg(INTERRUPT_STATUS1, hdmi_status);
	#endif
	hdmi_status = 0;
	rk610_hdmi_i2c_read_reg(HDMI_STATUS, &hdmi_status);
//	printk("%s value is %02x\n", __FUNCTION__, hdmi_status);	
	if(hdmi_status)
		return HDMI_HPD_ACTIVED;
	else
		return HDMI_HPD_REMOVED;
}

#define SYSCLK	11289600
#define DDC_CLK 100000
int rk610_hdmi_sys_read_edid(int block, unsigned char *buff)
{
	char value;
	int count, rc = HDMI_ERROR_EDID;
	char hdmi_status = 0;
	
	// Config DDC bus clock: ddc_clk = reg_clk/4*(reg 0x4c 0x4b)
	// when reg00 select reg_clk equal to sys_clk which is equal
	// to i2s clk, it gernerally is 11.2896MHz.
	
	count = SYSCLK/(DDC_CLK*4);
	HDMIWrReg(DDC_CLK_L, count & 0xFF);
	HDMIWrReg(DDC_CLK_H, (count >> 8) & 0xFF);
	
	// Enable EDID Interrupt
//	edid_ready = 0;
	atomic_set(&edid_ready, 0);
	value = 0;
	rk610_hdmi_i2c_read_reg(INTERRUPT_MASK1, &value);
	value |= m_INT_EDID_READY;
	HDMIWrReg(INTERRUPT_MASK1, value);
	
	// Reset FIFO offset
	HDMIWrReg(EDID_FIFO_OFFSET, 0);
	// Set EDID read addr.
	HDMIWrReg(EDID_WORD_ADDR, (block%2) * 0x80);
	HDMIWrReg(EDID_SEGMENT_POINTER, block/2);

	count = 0;
	while(count++ < 10)
	{	
#ifdef HDMI_USE_IRQ
	    value = atomic_read(&edid_ready);
#else 
	    msleep(10);
	    rk610_hdmi_i2c_read_reg(INTERRUPT_STATUS1, &hdmi_status);
	    value = (hdmi_status & m_INT_EDID_READY);
#endif
	    if(value)
	    {
		for(value = 0; value < 128; value++)
		    rk610_hdmi_i2c_read_reg(EDID_FIFO_ADDR, buff + value);
		rc = HDMI_ERROR_SUCESS;
		break;
	    }
	    msleep(100);
	}
	// Disable EDID interrupt.
	value = 0;
	rk610_hdmi_i2c_read_reg(INTERRUPT_MASK1, &value);
	value &= ~m_INT_EDID_READY;
	HDMIWrReg(INTERRUPT_MASK1, value);
	return rc;
}

static void rk610_hdmi_config_avi(unsigned char vic, unsigned char output_color)
{
	int i;
	char info[SIZE_AVI_INFOFRAME];
	
	memset(info, 0, SIZE_AVI_INFOFRAME);
	HDMIWrReg(CONTROL_PACKET_BUF_INDEX, INFOFRAME_AVI);
	info[0] = 0x82;
	info[1] = 0x02;
	info[2] = 0x0D;	
	info[3] = info[0] + info[1] + info[2];
	info[4] = (AVI_COLOR_MODE_RGB << 5);
	info[5] = (AVI_COLORIMETRY_NO_DATA << 6) | (AVI_CODED_FRAME_ASPECT_NO_DATA << 4) | ACTIVE_ASPECT_RATE_SAME_AS_CODED_FRAME;
	info[6] = 0;
	info[7] = vic;
	info[8] = 0;

	// Calculate AVI InfoFrame ChecKsum
	for (i = 4; i < SIZE_AVI_INFOFRAME; i++)
	{
    	info[3] += info[i];
	}
	info[3] = 0x100 - info[3];
	
	for(i = 0; i < SIZE_AVI_INFOFRAME; i++)
		HDMIWrReg(CONTROL_PACKET_ADDR + i, info[i]);
}

int rk610_hdmi_sys_config_video(struct hdmi_video_para *vpara)
{
	char value;
	struct fb_videomode *mode;
	
	hdmi_dbg(hdmi->dev, "[%s]\n", __FUNCTION__);
	if(vpara == NULL) {
		hdmi_err(hdmi->dev, "[%s] input parameter error\n", __FUNCTION__);
		return -1;
	}
	if(hdmi->hdcp_power_off_cb)
		hdmi->hdcp_power_off_cb();
	// Diable video and audio output
	HDMIWrReg(AV_MUTE, v_AUDIO_MUTE(1) | v_VIDEO_MUTE(1));
	
	// Input video mode is SDR RGB24bit, Data enable signal from external
	HDMIWrReg(VIDEO_CONTRL1, v_VIDEO_INPUT_FORMAT(VIDEO_INPUT_SDR_RGB444) | v_DE_EXTERNAL);
	HDMIWrReg(VIDEO_CONTRL2, v_VIDEO_INPUT_BITS(VIDEO_INPUT_8BITS) | (vpara->output_color & 0xFF));

	// Set HDMI Mode
	HDMIWrReg(HDCP_CTRL, v_HDMI_DVI(vpara->output_mode));

	// Enable or disalbe color space convert
	if(vpara->input_color != vpara->output_color) {
		value = v_SOF_DISABLE | v_CSC_ENABLE;
	}
	else
		value = v_SOF_DISABLE;
	HDMIWrReg(VIDEO_CONTRL3, value);
	
	#if 1
	HDMIWrReg(VIDEO_TIMING_CTL, 0);
	mode = (struct fb_videomode *)hdmi_vic_to_videomode(vpara->vic);
	if(mode == NULL)
	{
		hdmi_err(hdmi->dev, "[%s] not found vic %d\n", __FUNCTION__, vpara->vic);
		return -ENOENT;
	}
	hdmi->tmdsclk = mode->pixclock;
#else
	value = v_EXTERANL_VIDEO(1) | v_INETLACE(mode->vmode);
	if(mode->sync & FB_SYNC_HOR_HIGH_ACT)
		value |= v_HSYNC_POLARITY(1);
	if(mode->sync & FB_SYNC_VERT_HIGH_ACT)
		value |= v_VSYNC_POLARITY(1);
	HDMIWrReg(VIDEO_TIMING_CTL, value);
	
	value = mode->left_margin + mode->xres + mode->right_margin + mode->hsync_len;
	HDMIWrReg(VIDEO_EXT_HTOTAL_L, value & 0xFF);
	HDMIWrReg(VIDEO_EXT_HTOTAL_H, (value >> 8) & 0xFF);
	
	value = mode->left_margin + mode->right_margin + mode->hsync_len;
	HDMIWrReg(VIDEO_EXT_HBLANK_L, value & 0xFF);
	HDMIWrReg(VIDEO_EXT_HBLANK_H, (value >> 8) & 0xFF);
	
	value = mode->left_margin + mode->hsync_len;
	HDMIWrReg(VIDEO_EXT_HDELAY_L, value & 0xFF);
	HDMIWrReg(VIDEO_EXT_HDELAY_H, (value >> 8) & 0xFF);
	
	value = mode->hsync_len;
	HDMIWrReg(VIDEO_EXT_HDURATION_L, value & 0xFF);
	HDMIWrReg(VIDEO_EXT_HDURATION_H, (value >> 8) & 0xFF);
	
	value = mode->upper_margin + mode->yres + mode->lower_margin + mode->vsync_len;
	HDMIWrReg(VIDEO_EXT_VTOTAL_L, value & 0xFF);
	HDMIWrReg(VIDEO_EXT_VTOTAL_H, (value >> 8) & 0xFF);
	
	value = mode->upper_margin + mode->vsync_len + mode->lower_margin;
	HDMIWrReg(VIDEO_EXT_VBLANK, value & 0xFF);

	if(vpara->vic == HDMI_720x480p_60Hz_4_3 || vpara->vic == HDMI_720x480p_60Hz_16_9)
		value = 42;
	else
		value = mode->upper_margin + mode->vsync_len;

	HDMIWrReg(VIDEO_EXT_VDELAY, value & 0xFF);
	
	value = mode->vsync_len;
	HDMIWrReg(VIDEO_EXT_VDURATION, value & 0xFF);
	#endif
	
	if(vpara->output_mode == OUTPUT_HDMI) {
		rk610_hdmi_config_avi(vpara->vic, vpara->output_color);
		hdmi_dbg(hdmi->dev, "[%s] sucess output HDMI.\n", __FUNCTION__);
	}
	else {
		hdmi_dbg(hdmi->dev, "[%s] sucess output DVI.\n", __FUNCTION__);	
	}

	// Power on TMDS
	HDMIWrReg(PHY_PRE_EMPHASIS, v_PRE_EMPHASIS(0) | v_TMDS_PWRDOWN(0)); // TMDS power on
	
	// Enable TMDS
	value = 0;
	rk610_hdmi_i2c_read_reg(PHY_DRIVER, &value);
	value |= v_TX_ENABLE(1);
	HDMIWrReg(PHY_DRIVER, value);
	
	return 0;
}

static void rk610_hdmi_config_aai(void)
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
		HDMIWrReg(CONTROL_PACKET_ADDR + i, info[i]);
}

int rk610_hdmi_sys_config_audio(struct hdmi_audio *audio)
{
	int rate, N, channel, mclk_fs;
	
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
			dev_err(hdmi->dev, "[%s] not support such sample rate %d\n", __FUNCTION__, audio->rate);
			return -ENOENT;
	}

	//set_audio source I2S
	HDMIWrReg(AUDIO_CTRL1, 0x00); //internal CTS, disable down sample, i2s input, disable MCLK
	HDMIWrReg(AUDIO_SAMPLE_RATE, rate);
	HDMIWrReg(AUDIO_I2S_MODE, v_I2S_MODE(I2S_STANDARD) | v_I2S_CHANNEL(channel) );	
	HDMIWrReg(AUDIO_I2S_MAP, 0x00); 
	HDMIWrReg(AUDIO_I2S_SWAPS_SPDIF, 0); // no swap	
		
    //Set N value
    HDMIWrReg(AUDIO_N_H, (N >> 16) & 0x0F);
    HDMIWrReg(AUDIO_N_M, (N >> 8) & 0xFF); 
	HDMIWrReg(AUDIO_N_L, N & 0xFF);    
    rk610_hdmi_config_aai();
    
    return 0;
}

void rk610_hdmi_sys_enalbe_output(int enable)
{
	char mutestatus = 0;
	
	if(enable) {
		rk610_hdmi_i2c_read_reg(AV_MUTE, &mutestatus);
		if(mutestatus && (m_AUDIO_MUTE | m_VIDEO_BLACK)) {
			HDMIWrReg(AV_MUTE, v_AUDIO_MUTE(0) | v_VIDEO_MUTE(0));
			HDMIWrReg(SYS_CTRL, v_REG_CLK_SOURCE_IIS | v_PWR_ON | v_INT_POL_HIGH);
			HDMIWrReg(SYS_CTRL, v_REG_CLK_SOURCE_IIS | v_PWR_OFF | v_INT_POL_HIGH);
			HDMIWrReg(SYS_CTRL, v_REG_CLK_SOURCE_IIS | v_PWR_ON | v_INT_POL_HIGH);
			if(hdmi->hdcp_cb)
				hdmi->hdcp_cb();
		}
	}
	else {
		HDMIWrReg(AV_MUTE, v_AUDIO_MUTE(1) | v_VIDEO_MUTE(1));		
	}
}

int rk610_hdmi_sys_insert(void)
{
	hdmi_dbg(hdmi->dev, "%s \n", __FUNCTION__);
	//Bring up analog module.
	HDMIWrReg(PHY_BANDGAP_PWR, v_BANDGAP_PWR_UP);	//BG power on 
	HDMIWrReg(PHY_PLL_LDO_PWR, 0x00);		//PLL power on
	msleep(1);
	HDMIWrReg(PHY_PLL_CTRL, v_PLL_DISABLE(0));	//Analog reset
	return 0;
}

int rk610_hdmi_sys_remove(void)
{
	hdmi_dbg(hdmi->dev, "%s \n", __FUNCTION__);
	if(hdmi->hdcp_power_off_cb)
		hdmi->hdcp_power_off_cb();
	HDMIWrReg(PHY_DRIVER, v_MAIN_DRIVER(8)| v_PRE_DRIVER(0) | v_TX_ENABLE(0));
	HDMIWrReg(PHY_PRE_EMPHASIS, v_PRE_EMPHASIS(0) | v_TMDS_PWRDOWN(1));	//Driver power down	
	HDMIWrReg(PHY_PLL_CTRL, v_PLL_DISABLE(1) | v_PLL_RESET(1) | v_TMDS_RESET(1));
	HDMIWrReg(PHY_PLL_LDO_PWR, v_LDO_PWR_DOWN(1));
	HDMIWrReg(PHY_BANDGAP_PWR, v_BANDGAP_PWR_DOWN);
	return 0;
}
