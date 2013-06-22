#include <linux/delay.h>
#include <asm/io.h>
#include <mach/io.h>
#include "rk616_hdmi.h"
#include "rk616_hdmi_hw.h"
#include <mach/gpio.h>

// static char edid_result = 0;


static int rk616_hdmi_set_vif(rk_screen * screen,bool connect)
{
	rk616_set_vif(g_rk616_hdmi,screen,connect);
	return 0;
}

static int rk616_hdmi_init_pol_set(struct mfd_rk616 * rk616,int pol)
{
	u32 val;
	int ret;
	ret = rk616->read_dev(rk616,CRU_CFGMISC_CON,&val);
	if(pol)
		val &= 0xffffffdf;
	else
		val |= 0x20;
	ret = rk616->write_dev(rk616,CRU_CFGMISC_CON,&val);

	return 0;
}
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
	if(hdmi->pwr_mode == mode)
		return; 
	hdmi_dbg(hdmi->dev,"%s change pwr_mode %d --> %d\n",__FUNCTION__,hdmi->pwr_mode,mode);
    switch(mode){
     case NORMAL:
	     hdmi_dbg(hdmi->dev,"%s change pwr_mode NORMALpwr_mode = %d, mode = %d\n",__FUNCTION__,hdmi->pwr_mode,mode);
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
		break;
	case LOWER_PWR:
		hdmi_dbg(hdmi->dev,"%s change pwr_mode LOWER_PWR pwr_mode = %d, mode = %d\n",__FUNCTION__,hdmi->pwr_mode,mode);
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
#if 0
	HDMIRdReg(INTERRUPT_STATUS1,&value);
	if(value){
		HDMIWrReg(INTERRUPT_STATUS1, value);
	}
#endif
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
#define EDID_BLOCK_SIZE 128

int rk616_hdmi_read_edid(u8 block, u8 * buf)
{
	u32 c = 0;
	int ret = 0,i;
	u8 Segment = 0;
	u8 Offset = 0;
	if(block%2)
		Offset = EDID_BLOCK_SIZE;
	if(block/2)
		Segment = 1;
	printk("EDID DATA (Segment = %d Block = %d Offset = %d):\n", (int) Segment, (int) block, (int) Offset);
	//set edid fifo first addr
	c = 0x00;
	HDMIWrReg(0x4f,0);
	//set edid word address 00/80
	c = Offset;
	HDMIWrReg(0x4e, c);
	//set edid segment pointer
	c = Segment;
	HDMIWrReg(0x4d, c);

	//enable edid interrupt
//	c=0xc6;
//	HDMIWrReg(0xc0, c);
	//wait edid interrupt
	msleep(10);
	//printk("Interrupt generated\n");
	//c=0x00;
	//ret = HDMIRdReg(0xc1, &c);
	//printk("Interrupt reg=%x \n",c);
	//clear EDID interrupt reg
	//c=0x04;
	//HDMIWrReg(0xc1, c);
	for(i=0; i <EDID_BLOCK_SIZE;i++){
		c = 0;	    
		HDMIRdReg( 0x50, &c);
		buf[i] = c;
		if(i%16==0)
			printk("\n>>>%d:",i);
		printk("%02x ",c);
	}
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
	vpara->output_color = VIDEO_OUTPUT_RGB444;
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
	
#if 1
        HDMIWrReg(0xed, 0x0f);
        HDMIWrReg(0xe7, 0x96);
#else
	if(hdmi->tmdsclk >= 148500000) {
		HDMIWrReg(0xed, 0xc);
		HDMIWrReg(0xe7, 0x78);
	}
	else {
		HDMIWrReg(0xed, 0x3);
		HDMIWrReg(0xe7, 0x1e);
	}
#endif
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
	if(HDMI_CODEC_SOURCE_SELECT == INPUT_IIS){
		HDMIWrReg(AUDIO_CTRL1, 0x00); 
		HDMIWrReg(AUDIO_SAMPLE_RATE, rate);
		HDMIWrReg(AUDIO_I2S_MODE, v_I2S_MODE(I2S_STANDARD) | v_I2S_CHANNEL(channel) );	
		HDMIWrReg(AUDIO_I2S_MAP, 0x00); 
		HDMIWrReg(AUDIO_I2S_SWAPS_SPDIF, 0); // no swap	
	}else{
		HDMIWrReg(AUDIO_CTRL1, 0x08);
		HDMIWrReg(AUDIO_I2S_SWAPS_SPDIF, 0); // no swap	
	}
		
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
		}
		rk616_hdmi_sys_power_up();
		rk616_hdmi_sys_power_down();
		rk616_hdmi_sys_power_up();
		HDMIWrReg(0xce, 0x00);
		delay100us();
		HDMIWrReg(0xce, 0x01);
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


void rk616_hdmi_work(void)
{		
	u32 interrupt = 0;
        int value = 0;

        HDMIRdReg(INTERRUPT_STATUS1,&interrupt);
        if(interrupt){
                HDMIWrReg(INTERRUPT_STATUS1, interrupt);
        }

	if(interrupt & m_HOTPLUG){
                if(hdmi->state == HDMI_SLEEP)
			hdmi->state = WAIT_HOTPLUG;
        	if(hdmi->pwr_mode == LOWER_PWR)
	        	rk616_hdmi_set_pwr_mode(NORMAL);

		queue_delayed_work(hdmi->workqueue, &hdmi->delay_work, msecs_to_jiffies(40));	

        }

#if 0	
	if(hdmi->state == HDMI_SLEEP) {
//		hdmi_dbg(hdmi->dev, "hdmi return to sleep mode\n");
		rk616_hdmi_set_pwr_mode(LOWER_PWR);
	}
#endif
#if 0
	if(hdmi->hdcp_irq_cb)
		hdmi->hdcp_irq_cb(interrupt2);
#endif
}

static void rk616_hdmi_reset(void)
{
	u32 val = 0;
	u32 msk = 0;
	
	HDMIMskReg(SYS_CTRL,m_RST_DIGITAL,v_NOT_RST_DIGITAL);
	delay100us();
	HDMIMskReg(SYS_CTRL,m_RST_ANALOG,v_NOT_RST_ANALOG); 		
	delay100us();
	msk = m_REG_CLK_INV | m_REG_CLK_SOURCE | m_POWER | m_INT_POL;
	val = v_REG_CLK_INV | v_REG_CLK_SOURCE_SYS | v_PWR_ON |v_INT_POL_HIGH;
	HDMIMskReg(SYS_CTRL,msk,val);
	HDMIWrReg(INTERRUPT_MASK1,m_INT_HOTPLUG);
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
	hdmi->set_vif = rk616_hdmi_set_vif;
	
	rk616_hdmi_reset();

	rk616_hdmi_init_pol_set(g_rk616_hdmi,0);
	if(hdmi->hdcp_power_on_cb)
		rc = hdmi->hdcp_power_on_cb();

	return rc;
}
