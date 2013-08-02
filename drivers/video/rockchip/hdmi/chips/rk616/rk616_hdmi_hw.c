#include <linux/delay.h>
#include <asm/io.h>
#include <mach/io.h>
#include "rk616_hdmi.h"
#include "rk616_hdmi_hw.h"
#include <mach/gpio.h>

// static char edid_result = 0;

#ifndef CONFIG_ARCH_RK3026
static int rk616_set_polarity(struct mfd_rk616 * rk616, int vic)
{
        u32 val;
        int ret;
        u32 hdmi_polarity_mask = (3<<14);

        switch(vic)
        {
                
                case HDMI_1920x1080p_60Hz:
                case HDMI_1920x1080p_50Hz:
                case HDMI_1920x1080i_60Hz:
                case HDMI_1920x1080i_50Hz:
                case HDMI_1280x720p_60Hz:
                case HDMI_1280x720p_50Hz:
                        val = 0xc000;
                        ret = rk616->write_dev_bits(rk616, CRU_CFGMISC_CON, hdmi_polarity_mask, &val);
                        break;
			
                case HDMI_720x576p_50Hz_4_3:
                case HDMI_720x576p_50Hz_16_9:
                case HDMI_720x480p_60Hz_4_3:
                case HDMI_720x480p_60Hz_16_9:
                        val = 0x0;
                        ret = rk616->write_dev_bits(rk616, CRU_CFGMISC_CON, hdmi_polarity_mask, &val);
                        break;
                default:
                        val = 0x0;
                        ret = rk616->write_dev_bits(rk616, CRU_CFGMISC_CON, hdmi_polarity_mask, &val);
                        break;
        }
        return ret;
}

static int rk616_hdmi_set_vif(rk_screen * screen,bool connect)
{
        struct rk616_hdmi *rk616_hdmi;
        rk616_hdmi = container_of(hdmi, struct rk616_hdmi, g_hdmi);

        if (connect) 
                rk616_set_polarity(rk616_hdmi->rk616_drv, hdmi->vic); 

	rk616_set_vif(rk616_hdmi->rk616_drv,screen,connect);
	return 0;
}

static int rk616_hdmi_init_pol_set(struct mfd_rk616 * rk616,int pol)
{
	u32 val;
	int ret;
        int int_pol_mask = (1 << 5);

        if (pol) 
                val = 0x0;
        else
                val = 0x20;
        ret = rk616->write_dev_bits(rk616, CRU_CFGMISC_CON, int_pol_mask, &val);

	return 0;
}
#endif

static inline void delay100us(void)
{
	msleep(1);
}


static void rk616_hdmi_av_mute(bool enable)
{
	hdmi_writel(AV_MUTE, v_AUDIO_MUTE(enable) | v_VIDEO_MUTE(enable));
}

static void rk616_hdmi_sys_power_up(void)
{
	hdmi_msk_reg(SYS_CTRL,m_POWER, v_PWR_ON);
}
static void rk616_hdmi_sys_power_down(void)
{
	hdmi_msk_reg(SYS_CTRL,m_POWER, v_PWR_OFF);
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
                if (!(hdmi->set_vif) && (hdmi->vic == HDMI_1920x1080p_60Hz || hdmi->vic == HDMI_1920x1080p_50Hz)) {
                        /* 3026 and 1080p */
        		hdmi_writel(PHY_DRIVER,0xcc);
	        	hdmi_writel(PHY_PRE_EMPHASIS,0x4f);
                } else {
        		hdmi_writel(PHY_DRIVER,0xaa);
	        	hdmi_writel(PHY_PRE_EMPHASIS,0x0f);
                }
		hdmi_writel(PHY_SYS_CTL,0x2d);
		hdmi_writel(PHY_SYS_CTL,0x2c);
		hdmi_writel(PHY_SYS_CTL,0x28);
		hdmi_writel(PHY_SYS_CTL,0x20);
		hdmi_writel(PHY_CHG_PWR,0x0f);
	   	hdmi_writel(0xce, 0x00);
		hdmi_writel(0xce, 0x01);
		rk616_hdmi_av_mute(1);
		rk616_hdmi_sys_power_up();
		break;
	case LOWER_PWR:
		hdmi_dbg(hdmi->dev,"%s change pwr_mode LOWER_PWR pwr_mode = %d, mode = %d\n",__FUNCTION__,hdmi->pwr_mode,mode);
		rk616_hdmi_av_mute(0);
	   	rk616_hdmi_sys_power_down();
		hdmi_writel(PHY_DRIVER,0x00);
		hdmi_writel(PHY_PRE_EMPHASIS,0x00);
		hdmi_writel(PHY_CHG_PWR,0x00);
		hdmi_writel(PHY_SYS_CTL,0x2f);
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
	hdmi_readl(INTERRUPT_STATUS1,&value);
	if(value){
		hdmi_writel(INTERRUPT_STATUS1, value);
	}
#endif
	hdmi_readl(HDMI_STATUS,&value);
	
	hdmi_dbg(hdmi->dev, "[%s] value %02x\n", __FUNCTION__, value);
	value &= m_HOTPLUG;
	if(value == m_HOTPLUG)
		return HDMI_HPD_ACTIVED;
	else if(value)
		return HDMI_HPD_INSERT;
	else
		return HDMI_HPD_REMOVED;
}


int rk616_hdmi_read_edid(int block, u8 * buf)
{
        u32 c = 0;
	u8 Segment = 0;
	u8 Offset = 0;

        int ret = -1;
        int i, j;
        int ddc_bus_freq;
        int trytime;
        int checksum = 0;

	if(block%2)
		Offset = HDMI_EDID_BLOCK_SIZE;

	if(block/2)
		Segment = 1;

        ddc_bus_freq = (HDMI_SYS_FREG_CLK>>2)/HDMI_SCL_RATE;
        hdmi_writel(DDC_BUS_FREQ_L, ddc_bus_freq & 0xFF);
        hdmi_writel(DDC_BUS_FREQ_H, (ddc_bus_freq >> 8) & 0xFF);

        hdmi_dbg(hdmi->dev, "EDID DATA (Segment = %d Block = %d Offset = %d):\n", (int) Segment, (int) block, (int) Offset);
        disable_irq(hdmi->irq);

        //enable edid interrupt
        hdmi_writel(INTERRUPT_MASK1, m_INT_HOTPLUG | m_INT_EDID_READY);
       
        for (trytime = 0; trytime < 10; trytime++) {
                c=0x04;
                hdmi_writel(INTERRUPT_STATUS1, c);

                //set edid fifo first addr
                c = 0x00;
                hdmi_writel(EDID_FIFO_OFFSET, c);

                //set edid word address 00/80
                c = Offset;
                hdmi_writel(EDID_WORD_ADDR, c);

                //set edid segment pointer
                c = Segment;
                hdmi_writel(EDID_SEGMENT_POINTER, c);
 
                for (i = 0; i < 10; i++) {
                        //wait edid interrupt
                        msleep(10);
                        c=0x00;
                        hdmi_readl(INTERRUPT_STATUS1, &c);

                        if (c & 0x04) {
                                break;
                        }
                    
                }

                if (c & 0x04) {
                        for(j = 0; j < HDMI_EDID_BLOCK_SIZE; j++){
                                c = 0;	    
                                hdmi_readl(0x50, &c);
                                buf[j] = c;
                                checksum += c;
#ifdef HDMI_DEBUG
                                if(j%16==0)
                                        printk("\n>>>0x%02x: ", j);

                                printk("0x%02x ",c);
#endif
                        }

	                //clear EDID interrupt reg
                        c=0x04;
                        hdmi_writel(INTERRUPT_STATUS1, c);
                        
                        // printk("checksum = 0x%x\n", checksum);
                        if ((checksum &= 0xff) == 0) {
                                ret = 0;
                                hdmi_dbg(hdmi->dev, "[%s] edid read sucess\n", __FUNCTION__);
                                break;
                        }
                }
        }

        hdmi_writel(INTERRUPT_MASK1, m_INT_HOTPLUG);
        enable_irq(hdmi->irq);  

        return ret;
}

static void rk616_hdmi_config_avi(unsigned char vic, unsigned char output_color)
{
	int i;
	char info[SIZE_AVI_INFOFRAME];
	
	memset(info, 0, SIZE_AVI_INFOFRAME);
	hdmi_writel(CONTROL_PACKET_BUF_INDEX, INFOFRAME_AVI);
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
		hdmi_writel(CONTROL_PACKET_ADDR + i, info[i]);
}


static int rk616_hdmi_config_video(struct hdmi_video_para *vpara)
{
	int value;
	struct fb_videomode *mode;
	struct rk616_hdmi *rk616_hdmi;

	hdmi_dbg(hdmi->dev, "[%s]\n", __FUNCTION__);
        rk616_hdmi = container_of(hdmi, struct rk616_hdmi, g_hdmi);

	if(vpara == NULL) {
		hdmi_err(hdmi->dev, "[%s] input parameter error\n", __FUNCTION__);
		return -1;
	}

        if(hdmi->pwr_mode == LOWER_PWR) {
                rk616_hdmi_set_pwr_mode(NORMAL);
        }


	vpara->output_color = VIDEO_OUTPUT_RGB444;
	if(hdmi->hdcp_power_off_cb)
		hdmi->hdcp_power_off_cb();
		// Diable video and audio output
	hdmi_writel(AV_MUTE, v_AUDIO_MUTE(1) | v_VIDEO_MUTE(1));
	
	// Input video mode is SDR RGB24bit, Data enable signal from external
	hdmi_writel(VIDEO_CONTRL1, v_VIDEO_INPUT_FORMAT(VIDEO_INPUT_SDR_RGB444) | v_DE_EXTERNAL);
	hdmi_writel(VIDEO_CONTRL2, v_VIDEO_INPUT_BITS(VIDEO_INPUT_8BITS) | (vpara->output_color & 0xFF));

	// Set HDMI Mode
	hdmi_writel(HDCP_CTRL, v_HDMI_DVI(vpara->output_mode));

	// Enable or disalbe color space convert
	if(vpara->input_color != vpara->output_color) {
		value = v_SOF_DISABLE | v_CSC_ENABLE;
	}
	else
		value = v_SOF_DISABLE;
	hdmi_writel(VIDEO_CONTRL3, value);

	// Set ext video
#if 1
	hdmi_writel(VIDEO_TIMING_CTL, 0);
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
	hdmi_writel(VIDEO_TIMING_CTL, value);
	
	value = mode->left_margin + mode->xres + mode->right_margin + mode->hsync_len;
	hdmi_writel(VIDEO_EXT_HTOTAL_L, value & 0xFF);
	hdmi_writel(VIDEO_EXT_HTOTAL_H, (value >> 8) & 0xFF);
	
	value = mode->left_margin + mode->right_margin + mode->hsync_len;
	hdmi_writel(VIDEO_EXT_HBLANK_L, value & 0xFF);
	hdmi_writel(VIDEO_EXT_HBLANK_H, (value >> 8) & 0xFF);
	
	value = mode->left_margin + mode->hsync_len;
	hdmi_writel(VIDEO_EXT_HDELAY_L, value & 0xFF);
	hdmi_writel(VIDEO_EXT_HDELAY_H, (value >> 8) & 0xFF);
	
	value = mode->hsync_len;
	hdmi_writel(VIDEO_EXT_HDURATION_L, value & 0xFF);
	hdmi_writel(VIDEO_EXT_HDURATION_H, (value >> 8) & 0xFF);
	
	value = mode->upper_margin + mode->yres + mode->lower_margin + mode->vsync_len;
	hdmi_writel(VIDEO_EXT_VTOTAL_L, value & 0xFF);
	hdmi_writel(VIDEO_EXT_VTOTAL_H, (value >> 8) & 0xFF);
	
	value = mode->upper_margin + mode->vsync_len + mode->lower_margin;
	hdmi_writel(VIDEO_EXT_VBLANK, value & 0xFF);

	if(vpara->vic == HDMI_720x480p_60Hz_4_3 || vpara->vic == HDMI_720x480p_60Hz_16_9)
		value = 42;
	else
		value = mode->upper_margin + mode->vsync_len;

	hdmi_writel(VIDEO_EXT_VDELAY, value & 0xFF);
	
	value = mode->vsync_len;
	hdmi_writel(VIDEO_EXT_VDURATION, value & 0xFF);
#endif

        if(vpara->output_mode == OUTPUT_HDMI) {
		rk616_hdmi_config_avi(vpara->vic, vpara->output_color);
		hdmi_dbg(hdmi->dev, "[%s] sucess output HDMI.\n", __FUNCTION__);
	}
	else {
		hdmi_dbg(hdmi->dev, "[%s] sucess output DVI.\n", __FUNCTION__);	
	}
	
        // if(rk616_hdmi->rk616_drv) {     
        if (hdmi->set_vif) {
                hdmi_writel(0xed, 0x0f);
                hdmi_writel(0xe7, 0x96);
        } else {        // rk3028a
                hdmi_writel(0xed, 0x1e);
                hdmi_writel(0xe7, 0x2c);
                hdmi_writel(0xe8, 0x01);
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
	
	hdmi_writel(CONTROL_PACKET_BUF_INDEX, INFOFRAME_AAI);
	for(i = 0; i < SIZE_AUDIO_INFOFRAME; i++)
		hdmi_writel(CONTROL_PACKET_ADDR + i, info[i]);
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
		hdmi_writel(AUDIO_CTRL1, 0x00); 
		hdmi_writel(AUDIO_SAMPLE_RATE, rate);
		hdmi_writel(AUDIO_I2S_MODE, v_I2S_MODE(I2S_STANDARD) | v_I2S_CHANNEL(channel) );	
		hdmi_writel(AUDIO_I2S_MAP, 0x00); 
		hdmi_writel(AUDIO_I2S_SWAPS_SPDIF, 0); // no swap	
	}else{
		hdmi_writel(AUDIO_CTRL1, 0x08);
		hdmi_writel(AUDIO_I2S_SWAPS_SPDIF, 0); // no swap	
	}
	
        //Set N value
        hdmi_writel(AUDIO_N_H, (N >> 16) & 0x0F);
        hdmi_writel(AUDIO_N_M, (N >> 8) & 0xFF); 
        hdmi_writel(AUDIO_N_L, N & 0xFF);    
        rk616_hdmi_config_aai();
      
        return 0;
}

void rk616_hdmi_control_output(int enable)
{
	int mutestatus = 0;
	
	if(enable) {
		if(hdmi->pwr_mode == LOWER_PWR)
			rk616_hdmi_set_pwr_mode(NORMAL);
		hdmi_readl(AV_MUTE,&mutestatus);
		if(mutestatus && (m_AUDIO_MUTE | m_VIDEO_BLACK)) {
			hdmi_writel(AV_MUTE, v_AUDIO_MUTE(0) | v_VIDEO_MUTE(0));
		}
		rk616_hdmi_sys_power_up();
		rk616_hdmi_sys_power_down();
		rk616_hdmi_sys_power_up();
		hdmi_writel(0xce, 0x00);
		delay100us();
		hdmi_writel(0xce, 0x01);
	}
	else {
		hdmi_writel(AV_MUTE, v_AUDIO_MUTE(1) | v_VIDEO_MUTE(1));
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
        // int value = 0;

        
        hdmi_readl(INTERRUPT_STATUS1,&interrupt);
        if(interrupt){
                hdmi_writel(INTERRUPT_STATUS1, interrupt);
        }

	if(interrupt & m_HOTPLUG){
                if(hdmi->state == HDMI_SLEEP)
			hdmi->state = WAIT_HOTPLUG;
        	//if(hdmi->pwr_mode == LOWER_PWR)
	        //	rk616_hdmi_set_pwr_mode(NORMAL);

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
	
	hdmi_msk_reg(SYS_CTRL,m_RST_DIGITAL,v_NOT_RST_DIGITAL);
	delay100us();
	hdmi_msk_reg(SYS_CTRL,m_RST_ANALOG,v_NOT_RST_ANALOG);		
	delay100us();
	msk = m_REG_CLK_INV | m_REG_CLK_SOURCE | m_POWER | m_INT_POL;
	val = v_REG_CLK_INV | v_REG_CLK_SOURCE_SYS | v_PWR_ON |v_INT_POL_HIGH;
	hdmi_msk_reg(SYS_CTRL,msk,val);
	hdmi_writel(INTERRUPT_MASK1,m_INT_HOTPLUG);
	rk616_hdmi_set_pwr_mode(LOWER_PWR);
}

int rk616_hdmi_initial(void)
{
	int rc = HDMI_ERROR_SUCESS;
        struct rk616_hdmi *rk616_hdmi;
        rk616_hdmi = container_of(hdmi, struct rk616_hdmi, g_hdmi);

	hdmi->pwr_mode = NORMAL;
	hdmi->remove = rk616_hdmi_removed ;
	hdmi->control_output = rk616_hdmi_control_output;
	hdmi->config_video = rk616_hdmi_config_video;
	hdmi->config_audio = rk616_hdmi_config_audio;
	hdmi->detect_hotplug = rk616_hdmi_detect_hotplug;
	hdmi->read_edid = rk616_hdmi_read_edid;

#ifdef CONFIG_ARCH_RK3026
        rk3028_hdmi_reset_pclk();
        rk616_hdmi_reset();
#else
        hdmi->set_vif = rk616_hdmi_set_vif;
        rk616_hdmi_reset();
	rk616_hdmi_init_pol_set(rk616_hdmi->rk616_drv, 0);
#endif
        
	if(hdmi->hdcp_power_on_cb)
		rc = hdmi->hdcp_power_on_cb();

	return rc;
}
