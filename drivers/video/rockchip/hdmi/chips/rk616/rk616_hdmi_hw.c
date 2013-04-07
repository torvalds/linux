#include <linux/delay.h>
#include <asm/io.h>
#include <mach/io.h>
#include "rk616_hdmi.h"
#include "rk616_hdmi_hw.h"

static char edid_result = 0;
static bool analog_sync = 0;

static inline void delay100us(void)
{
	msleep(1);
}


static void rk616_hdmi_av_mute(bool enable)
{
	HDMIWrReg(AV_MUTE, v_AUDIO_MUTE(enable) | v_VIDEO_MUTE(enable));
}

static void rk616_hdmi_sys_power_up(void)
{
	HDMIMskReg(SYS_CTRL,m_POWER, v_PWR_ON);
}
static void rk616_hdmi_sys_power_down(void)
{
	HDMIMskReg(SYS_CTRL,m_POWER, v_PWR_OFF);
}


static void rk616_hdmi_set_pwr_mode(int mode)
{
	hdmi_dbg(hdmi->dev,"%s \n",__FUNCTION__);
	if(hdmi->pwr_mode == mode)
		return; 
    switch(mode){
     case NORMAL:
	   	rk616_hdmi_sys_power_down();
		HDMIWrReg(PHY_DRIVER,0xaa);
		HDMIWrReg(PHY_PRE_EMPHASIS,0x0f);
		HDMIWrReg(PHY_SYS_CTL,0x2d);
		HDMIWrReg(PHY_SYS_CTL,0x2c);
		HDMIWrReg(PHY_SYS_CTL,0x28);
		HDMIWrReg(PHY_SYS_CTL,0x20);
		HDMIWrReg(PHY_CHG_PWR,0x0f);
	   	HDMIWrReg(0xce, 0x00);
		HDMIWrReg(0xce, 0x01);
		rk616_hdmi_av_mute(1);
		rk616_hdmi_sys_power_up();
		analog_sync = 1;
		break;
	case LOWER_PWR:
		rk616_hdmi_av_mute(0);
	   	rk616_hdmi_sys_power_down();
		HDMIWrReg(PHY_DRIVER,0x00);
		HDMIWrReg(PHY_PRE_EMPHASIS,0x00);
		HDMIWrReg(PHY_CHG_PWR,0x00);
		HDMIWrReg(PHY_SYS_CTL,0x2f);
		break;
	default:
	    hdmi_dbg(hdmi->dev,"unkown rk616 hdmi pwr mode %d\n",mode);
    }
	hdmi->pwr_mode = mode;
}


int rk616_hdmi_detect_hotplug(void)
{
	int value = 0;
	HDMIRdReg(HDMI_STATUS,&value);
	
	hdmi_dbg(hdmi->dev, "[%s] value %02x\n", __FUNCTION__, value);
	value &= m_HOTPLUG;
	if(value == m_HOTPLUG)
		return HDMI_HPD_ACTIVED;
	else if(value)
		return HDMI_HPD_INSERT;
	else
		return HDMI_HPD_REMOVED;
}

#define HDMI_SYS_FREG_CLK        11289600
#define HDMI_SCL_RATE            (100*1000)
#define HDMI_DDC_CONFIG          (HDMI_SYS_FREG_CLK>>2)/HDMI_SCL_RATE
#define DDC_BUS_FREQ_L 			0x4b
#define DDC_BUS_FREQ_H 			0x4c

int rk616_hdmi_read_edid(int block, unsigned char *buff)
{
	int value, ret = -1, ddc_bus_freq = 0;
	char interrupt = 0, trytime = 2;
	unsigned long flags;
	
	hdmi_dbg(hdmi->dev, "[%s] block %d\n", __FUNCTION__, block);
	spin_lock_irqsave(&hdmi->irq_lock, flags);
	edid_result = 0;
	spin_unlock_irqrestore(&hdmi->irq_lock, flags);
	//Before Phy parameter was set, DDC_CLK is equal to PLLA freq which is 30MHz.
	//Set DDC I2C CLK which devided from DDC_CLK to 100KHz.

	ddc_bus_freq = HDMI_DDC_CONFIG; 
	HDMIWrReg(DDC_BUS_FREQ_L, ddc_bus_freq & 0xFF);
	HDMIWrReg(DDC_BUS_FREQ_H, (ddc_bus_freq >> 8) & 0xFF);
	
	// Enable edid interrupt
	HDMIWrReg(INTERRUPT_MASK1, m_INT_HOTPLUG | m_INT_EDID_READY);
	
	while(trytime--) {
		// Config EDID block and segment addr
		HDMIWrReg(EDID_WORD_ADDR, (block%2) * 0x80);
		HDMIWrReg(EDID_SEGMENT_POINTER, block/2);	
	
		value = 10;
		while(value--)
		{
			spin_lock_irqsave(&hdmi->irq_lock, flags);
			interrupt = edid_result;
			edid_result = 0;
			spin_unlock_irqrestore(&hdmi->irq_lock, flags);
			if(interrupt & (m_INT_EDID_READY))
				break;
			msleep(10);
		}
		hdmi_dbg(hdmi->dev, "[%s] edid read value %d\n", __FUNCTION__, value);
		if(interrupt & m_INT_EDID_READY)
		{
			for(value = 0; value < HDMI_EDID_BLOCK_SIZE; value++) 
				 HDMIRdReg(EDID_FIFO_ADDR,(buff+value));
			ret = 0;
			
			hdmi_dbg(hdmi->dev, "[%s] edid read sucess\n", __FUNCTION__);
#ifdef HDMI_DEBUG
			for(value = 0; value < 128; value++) {
				printk("%02x ,", buff[value]);
				if( (value + 1) % 16 == 0)
					printk("\n");
			}
#endif
			break;
		}else
			hdmi_err(hdmi->dev, "[%s] edid read error\n", __FUNCTION__);
			
		hdmi_dbg(hdmi->dev, "[%s] edid try times %d\n", __FUNCTION__, trytime);
		msleep(100);
	}
	// Disable edid interrupt
	HDMIWrReg(INTERRUPT_MASK1, m_INT_HOTPLUG);
//	msleep(100);
	return ret;
}

static void rk616_hdmi_config_avi(unsigned char vic, unsigned char output_color)
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

static int rk616_hdmi_config_video(struct hdmi_video_para *vpara)
{
	int value;
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

	// Set ext video
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
		rk616_hdmi_config_avi(vpara->vic, vpara->output_color);
		hdmi_dbg(hdmi->dev, "[%s] sucess output HDMI.\n", __FUNCTION__);
	}
	else {
		hdmi_dbg(hdmi->dev, "[%s] sucess output DVI.\n", __FUNCTION__);	
	}
	
	if(hdmi->tmdsclk >= 148500000) {
		HDMIWrReg(0xe3, 0x4f);
	}
	else {
		HDMIWrReg(0xe3, 0x0f);
	}
	return 0;
}

static void rk616_hdmi_config_aai(void)
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

static int rk616_hdmi_config_audio(struct hdmi_audio *audio)
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
    rk616_hdmi_config_aai();
    
    return 0;
}

void rk616_hdmi_control_output(int enable)
{
	char mutestatus = 0;
	
	if(enable) {
		if(hdmi->pwr_mode == LOWER_PWR)
			rk616_hdmi_set_pwr_mode(NORMAL);
		 HDMIRdReg(AV_MUTE,&mutestatus);
		if(mutestatus && (m_AUDIO_MUTE | m_VIDEO_BLACK)) {
			HDMIWrReg(AV_MUTE, v_AUDIO_MUTE(0) | v_VIDEO_MUTE(0));
    		rk616_hdmi_sys_power_up();
    		rk616_hdmi_sys_power_down();
     		rk616_hdmi_sys_power_up();
			if(analog_sync){
				HDMIWrReg(0xce, 0x00);
				delay100us();
				HDMIWrReg(0xce, 0x01);
				analog_sync = 0;
			}
		}
	}
	else {
		HDMIWrReg(AV_MUTE, v_AUDIO_MUTE(1) | v_VIDEO_MUTE(1));
	}
}

int rk616_hdmi_removed(void)
{

	dev_printk(KERN_INFO , hdmi->dev , "Removed.\n");
	rk616_hdmi_set_pwr_mode(LOWER_PWR);

	return HDMI_ERROR_SUCESS;
}


irqreturn_t hdmi_irq(int irq, void *priv)
{		
	char interrupt1 = 0;
	unsigned long flags;
	spin_lock_irqsave(&hdmi->irq_lock,flags);
	HDMIRdReg(INTERRUPT_STATUS1,&interrupt1);
	HDMIWrReg(INTERRUPT_STATUS1, interrupt1);
#if 1
		hdmi_dbg(hdmi->dev, "[%s] interrupt1 %02x  \n",\
			 __FUNCTION__, interrupt1);
#endif
	if(interrupt1 & m_INT_HOTPLUG ){
		if(hdmi->state == HDMI_SLEEP)
			hdmi->state = WAIT_HOTPLUG;
		if(hdmi->pwr_mode == LOWER_PWR)
			rk616_hdmi_set_pwr_mode(NORMAL);
		queue_delayed_work(hdmi->workqueue, &hdmi->delay_work, msecs_to_jiffies(10));	
	}else if(interrupt1 & m_INT_EDID_READY) {
		edid_result = interrupt1;
	}else if(hdmi->state == HDMI_SLEEP) {
		hdmi_dbg(hdmi->dev, "hdmi return to sleep mode\n");
		rk616_hdmi_set_pwr_mode(LOWER_PWR);
	}
#if 0
	if(hdmi->hdcp_irq_cb)
		hdmi->hdcp_irq_cb(interrupt2);
#endif
	spin_unlock_irqrestore(&hdmi->irq_lock,flags);
	return IRQ_HANDLED;
}

static void rk616_hdmi_reset(void)
{
	char val = 0;
	val = v_REG_CLK_INV| v_VCLK_INV |v_REG_CLK_SOURCE_SYS|v_PWR_ON |v_INT_POL_LOW;
	HDMIWrReg(SYS_CTRL,val);
	delay100us();
	HDMIMskReg(SYS_CTRL,m_RST_ANALOG,v_NOT_RST_ANALOG); 		
	delay100us();
	HDMIMskReg(SYS_CTRL,m_RST_DIGITAL,v_NOT_RST_DIGITAL);
	rk616_hdmi_set_pwr_mode(LOWER_PWR);
}

int rk616_hdmi_initial(void)
{
	int rc = HDMI_ERROR_SUCESS;

	hdmi->pwr_mode = NORMAL;
	hdmi->remove = rk616_hdmi_removed ;
	hdmi->control_output = rk616_hdmi_control_output;
	hdmi->config_video = rk616_hdmi_config_video;
	hdmi->config_audio = rk616_hdmi_config_audio;
	hdmi->detect_hotplug = rk616_hdmi_detect_hotplug;
	hdmi->read_edid = rk616_hdmi_read_edid;
	
	rk616_hdmi_reset();
	
	if(hdmi->hdcp_power_on_cb)
		rc = hdmi->hdcp_power_on_cb();

	return rc;
}
