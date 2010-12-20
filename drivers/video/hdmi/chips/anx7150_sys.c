//  ANALOGIX Company
//  ANX7150 Demo Firmware
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/hdmi.h>


#include "anx7150_sys.h"


/******** define uint8 and WORD, by kfx *******/
int anx7150_tmds_enable(struct hdmi *hdmi)
{
	int rc = 0;
	char c;
	
	/* andio stream enable */
	if((rc = anx7150_i2c_read_p0_reg(hdmi->client, ANX7150_HDMI_AUDCTRL1_REG, &c)) < 0)
		return rc;
	c |= (ANX7150_HDMI_AUDCTRL1_IN_EN);

	if((rc = anx7150_i2c_write_p0_reg(hdmi->client, ANX7150_HDMI_AUDCTRL1_REG, &c)) < 0)
		return rc;

	/* video stream enable */
	if((rc = anx7150_i2c_read_p0_reg(hdmi->client, ANX7150_VID_CTRL_REG, &c)) < 0)
		return rc;
	c |= (ANX7150_VID_CTRL_IN_EN);

	if((rc = anx7150_i2c_write_p0_reg(hdmi->client, ANX7150_VID_CTRL_REG, &c)) < 0)
		return rc;

	/* TMDS enable */
	if((rc = anx7150_i2c_read_p0_reg(hdmi->client, ANX7150_TMDS_CLKCH_CONFIG_REG, &c)) < 0)
		return rc;
	c |= (ANX7150_TMDS_CLKCH_MUTE);

	if((rc = anx7150_i2c_write_p0_reg(hdmi->client, ANX7150_TMDS_CLKCH_CONFIG_REG, &c)) < 0)
		return rc;

	return rc;
}

int anx7150_tmds_disable(struct hdmi *hdmi)
{
	int rc = 0;
	char c;
	
	/* andio stream disable */
	if((rc = anx7150_i2c_read_p0_reg(hdmi->client, ANX7150_HDMI_AUDCTRL1_REG, &c)) < 0)
		return rc;
	c &= (~ANX7150_HDMI_AUDCTRL1_IN_EN);

	if((rc = anx7150_i2c_write_p0_reg(hdmi->client, ANX7150_HDMI_AUDCTRL1_REG, &c)) < 0)
		return rc;

	/* video stream disable */
	if((rc = anx7150_i2c_read_p0_reg(hdmi->client, ANX7150_VID_CTRL_REG, &c)) < 0)
		return rc;
	c &= (~ANX7150_VID_CTRL_IN_EN);

	if((rc = anx7150_i2c_write_p0_reg(hdmi->client, ANX7150_VID_CTRL_REG, &c)) < 0)
		return rc;

	/* TMDS disable */
	if((rc = anx7150_i2c_read_p0_reg(hdmi->client, ANX7150_TMDS_CLKCH_CONFIG_REG, &c)) < 0)
		return rc;
	c &= (~ANX7150_TMDS_CLKCH_MUTE);

	if((rc = anx7150_i2c_write_p0_reg(hdmi->client, ANX7150_TMDS_CLKCH_CONFIG_REG, &c)) < 0)
		return rc;

	return rc;
}
static void anx7150_set_video_format(struct hdmi *hdmi)
{
	struct anx7150_pdata *anx = hdmi_get_privdata(hdmi);

	switch(hdmi->resolution)
	{
		case HDMI_720x576p_50Hz:
			anx->video_format = ANX7150_V720x576p_50Hz_4x3;
			break;
		case ANX7150_V1280x720p_50Hz:
			anx->video_format = ANX7150_V1280x720p_50Hz;
			break;
		case HDMI_1280x720p_60Hz:
			anx->video_format = ANX7150_V1280x720p_60Hz;
			break;
		default:
			anx->video_format = ANX7150_V1280x720p_50Hz;
			break;
	}
	anx->system_config_done = 0;
	return;	
}
static void anx7150_set_audio_fs(struct hdmi *hdmi)
{
	struct anx7150_pdata *anx = hdmi_get_privdata(hdmi);

	anx->audio_format = hdmi->audio_fs;
	anx->system_config_done = 0;
	return;	
}

static void anx7150_set_system_state(struct hdmi *hdmi,  unsigned char ss)
{
	struct anx7150_pdata *anx = hdmi_get_privdata(hdmi);

	anx->system_state = ss;
    switch (ss)
    {
        case ANX7150_INITIAL:
            dev_info(hdmi->dev, "system state: ANX7150_INITIAL\n");
            break;
        case ANX7150_WAIT_HOTPLUG:
            dev_info(hdmi->dev, "system state: ANX7150_WAIT_HOTPLUG\n");
            break;
        case ANX7150_READ_PARSE_EDID:
            dev_info(hdmi->dev, "system state: ANX7150_READ_PARSE_EDID\n");
            break;
        case ANX7150_WAIT_RX_SENSE:
            dev_info(hdmi->dev, "system state: ANX7150_WAIT_RX_SENSE\n");
            break;
        case ANX7150_CONFIG_VIDEO:
            dev_info(hdmi->dev, "system state: ANX7150_CONFIG_VIDEO\n");
            break;
        case ANX7150_CONFIG_AUDIO:
            dev_info(hdmi->dev, "system state: ANX7150_CONFIG_AUDIO\n");
            break;
        case ANX7150_CONFIG_PACKETS:
            dev_info(hdmi->dev, "system state: ANX7150_CONFIG_PACKETS\n");
            break;
        case ANX7150_HDCP_AUTHENTICATION:
            dev_info(hdmi->dev, "system state: ANX7150_HDCP_AUTHENTICATION\n");
            break;
		case ANX7150_RESET_LINK:
            dev_info(hdmi->dev, "system state: ANX7150_RESET_LINK\n");
            break;
        case ANX7150_PLAY_BACK:
            dev_info(hdmi->dev, "system state: ANX7150_PLAY_BACK\n");
            break;
		default:
			dev_info(hdmi->dev, "system state: ANX7150 unknown state\n");
			break;
    }
	return;
}

static void anx7150_variable_initial(struct hdmi *hdmi)
{
    int i;
	struct anx7150_pdata *anx = hdmi_get_privdata(hdmi);
	
    anx7150_set_system_state(hdmi, ANX7150_INITIAL);
	
    anx->anx7150_hdcp_auth_en = 0;
    anx->anx7150_ksv_srm_pass =0;
    anx->anx7150_srm_checked = 0;
    anx->anx7150_hdcp_auth_pass = 0;
    anx->anx7150_avmute_enable = 1;
    anx->anx7150_hdcp_auth_fail_counter =0;
    anx->anx7150_hdcp_encryption = 0;
    anx->anx7150_send_blue_screen = 0;
    anx->anx7150_hdcp_init_done = 0;
    anx->anx7150_hdcp_wait_100ms_needed = 1;
    anx->anx7150_auth_fully_pass = 0;
    anx->timer_slot = 0;
    /***************for video config***************/
    anx->anx7150_video_timing_id = 0;
    anx->anx7150_in_pix_rpt = 0;
    anx->anx7150_tx_pix_rpt = 0;
    anx->anx7150_new_csc = 0;
    anx->anx7150_new_vid_id = 0;
    anx->anx7150_new_hw_interface = 0;
    /*****************end of video config**********/

    /********************for edid parse************/
    anx->edid.is_HDMI = 0;
    anx->edid.ycbcr422_supported = 0;
    anx->edid.ycbcr444_supported = 0;
    anx->edid.supported_720p_60Hz = 0;
    anx->edid.supported_720p_50Hz = 0;
    anx->edid.supported_576p_50Hz = 0;
    anx->edid.supported_576i_50Hz = 0;
    anx->edid.supported_1080i_60Hz = 0;
    anx->edid.supported_1080i_50Hz = 0;
    anx->edid.supported_640x480p_60Hz = 0;
    anx->edid.supported_720x480p_60Hz = 0;
    anx->edid.supported_720x480i_60Hz = 0;
    anx->edid.edid_errcode = 0;
    anx->edid.SpeakerFormat = 0;
    for (i = 0; i < 8; i ++)
    {
        anx->edid.AudioChannel[i] = 0;
        anx->edid.AudioFormat[i] = 0;
        anx->edid.AudioFs[i] = 0;
        anx->edid.AudioLength[i] = 0;
    }
    /********************end of edid****************/

    anx->packets_config.packets_need_config = 0x03;   //new avi infoframe
    anx->packets_config.avi_info.type = 0x82;
    anx->packets_config.avi_info.version = 0x02;
    anx->packets_config.avi_info.length = 0x0d;
    anx->packets_config.avi_info.pb_uint8[1] = 0x21;//YCbCr422
    anx->packets_config.avi_info.pb_uint8[2] = 0x08;
    anx->packets_config.avi_info.pb_uint8[3] = 0x00;
    anx->packets_config.avi_info.pb_uint8[4] = 0x00;
    anx->packets_config.avi_info.pb_uint8[5] = 0x00;
    anx->packets_config.avi_info.pb_uint8[6] = 0x00;
    anx->packets_config.avi_info.pb_uint8[7] = 0x00;
    anx->packets_config.avi_info.pb_uint8[8] = 0x00;
    anx->packets_config.avi_info.pb_uint8[9] = 0x00;
    anx->packets_config.avi_info.pb_uint8[10] = 0x00;
    anx->packets_config.avi_info.pb_uint8[11] = 0x00;
    anx->packets_config.avi_info.pb_uint8[12] = 0x00;
    anx->packets_config.avi_info.pb_uint8[13] = 0x00;

    // audio infoframe
    anx->packets_config.audio_info.type = 0x84;
    anx->packets_config.audio_info.version = 0x01;
    anx->packets_config.audio_info.length = 0x0a;
    anx->packets_config.audio_info.pb_uint8[1] = 0x00;  //zy 061123 for ATC
    anx->packets_config.audio_info.pb_uint8[2] = 0x00;
    anx->packets_config.audio_info.pb_uint8[3] = 0x00;
    anx->packets_config.audio_info.pb_uint8[4] = 0x00;
    anx->packets_config.audio_info.pb_uint8[5] = 0x00;
    anx->packets_config.audio_info.pb_uint8[6] = 0x00;
    anx->packets_config.audio_info.pb_uint8[7] = 0x00;
    anx->packets_config.audio_info.pb_uint8[8] = 0x00;
    anx->packets_config.audio_info.pb_uint8[9] = 0x00;
    anx->packets_config.audio_info.pb_uint8[10] = 0x00;

	anx->anx7150_int_done = 0;
}


static void anx7150_hw_interface_variable_initial(struct hdmi *hdmi)
{
    unsigned char c;
	struct anx7150_pdata *anx = hdmi_get_privdata(hdmi);
	
    anx->anx7150_video_format_config = 0x00;
	anx->anx7150_rgborycbcr = 0x00;
    anx->anx7150_ddr_edge = ANX7150_IDCK_EDGE_DDR;

    c = 0;
    c = (ANX7150_I2S_CH0_ENABLE << 2) | (ANX7150_I2S_CH1_ENABLE << 3) |
        (ANX7150_I2S_CH2_ENABLE << 4) | (ANX7150_I2S_CH3_ENABLE << 5);
	
    anx->audio_config.audio_type = ANX7150_AUD_HW_INTERFACE;     // input I2S
    anx->audio_config.down_sample = 0x00;
    anx->audio_config.i2s_config.audio_channel = c;//0x04;
    anx->audio_config.i2s_config.Channel_status1 =0x00;
    anx->audio_config.i2s_config.Channel_status1 = 0x00;
    anx->audio_config.i2s_config.Channel_status2 = 0x00;
    anx->audio_config.i2s_config.Channel_status3 = 0x00;
    anx->audio_config.i2s_config.Channel_status4 = 0x00;//0x02;//48k
    anx->audio_config.i2s_config.Channel_status5 = ANX7150_I2S_WORD_LENGTH;//0x0b;
    anx->audio_config.audio_layout = 0x00;

    c = (ANX7150_I2S_SHIFT_CTRL << 3) | (ANX7150_I2S_DIR_CTRL << 2)  |
        (ANX7150_I2S_WS_POL << 1) | ANX7150_I2S_JUST_CTRL;
    anx->audio_config.i2s_config.i2s_format = c;//0x00;

    anx->freq_mclk= ANX7150_MCLK_Fs_RELATION;//set the relation of MCLK and WS
    anx->anx7150_audio_clock_edge = ANX7150_AUD_CLK_EDGE;
	return;
}
static int anx7150_hardware_initial(struct hdmi *hdmi)
{
	int rc = 0;
    char c = 0;
	
    //clear HDCP_HPD_RST
    rc = anx7150_i2c_read_p0_reg(hdmi->client, ANX7150_SYS_CTRL2_REG, &c);
	c |= (0x01);
	rc = anx7150_i2c_write_p0_reg(hdmi->client, ANX7150_SYS_CTRL2_REG, &c);

   	mdelay(10);

	c &= (~0x01);
    rc = anx7150_i2c_write_p0_reg(hdmi->client, ANX7150_SYS_CTRL2_REG, &c);
	
    //Power on I2C
    rc = anx7150_i2c_read_p0_reg(hdmi->client, ANX7150_SYS_CTRL3_REG, &c);
	c |= (ANX7150_SYS_CTRL3_I2C_PWON);
	rc = anx7150_i2c_write_p0_reg(hdmi->client, ANX7150_SYS_CTRL3_REG, &c);

	c = 0x00;
	rc = anx7150_i2c_write_p0_reg(hdmi->client, ANX7150_SYS_CTRL2_REG, &c);
	c= 0x00;
	rc = anx7150_i2c_write_p0_reg(hdmi->client, ANX7150_SRST_REG, &c);

    //clear HDCP_HPD_RST
	rc = anx7150_i2c_read_p0_reg(hdmi->client, ANX7150_SYS_CTRL1_REG, &c);
	c &= (0xbf);
	rc = anx7150_i2c_write_p0_reg(hdmi->client, ANX7150_SYS_CTRL1_REG, &c);

    //Power on Audio capture and Video capture module clock
    rc = anx7150_i2c_read_p0_reg(hdmi->client, ANX7150_SYS_PD_REG, &c);
	c |= (0x06);
	rc = anx7150_i2c_write_p0_reg(hdmi->client, ANX7150_SYS_PD_REG, &c);

    //Enable auto set clock range for video PLL
    rc = anx7150_i2c_read_p0_reg(hdmi->client, ANX7150_CHIP_CTRL_REG, &c);
	c &= (0x00);
	rc = anx7150_i2c_write_p0_reg(hdmi->client, ANX7150_CHIP_CTRL_REG, &c);

    //Set registers value of Blue Screen when HDCP authentication failed--RGB mode,green field
    c = 0x10;
	rc = anx7150_i2c_write_p0_reg(hdmi->client, ANX7150_HDCP_BLUESCREEN0_REG, &c);
	c = 0xeb;
	rc = anx7150_i2c_write_p0_reg(hdmi->client, ANX7150_HDCP_BLUESCREEN1_REG, &c);
	c = 0x10;
	rc = anx7150_i2c_write_p0_reg(hdmi->client, ANX7150_HDCP_BLUESCREEN2_REG, &c);

    //ANX7150_i2c_read_p0_reg(ANX7150_TMDS_CLKCH_CONFIG_REG, &c);
    //ANX7150_i2c_write_p0_reg(ANX7150_TMDS_CLKCH_CONFIG_REG, (c | 0x80));

	rc = anx7150_i2c_read_p0_reg(hdmi->client, ANX7150_PLL_CTRL0_REG, &c);
	c = 0x00;
	rc = anx7150_i2c_write_p0_reg(hdmi->client, ANX7150_PLL_CTRL0_REG, &c);

	rc = anx7150_i2c_read_p0_reg(hdmi->client, ANX7150_CHIP_DEBUG1_CTRL_REG, &c);
	c |= (0x08);
	rc = anx7150_i2c_write_p0_reg(hdmi->client, ANX7150_CHIP_DEBUG1_CTRL_REG, &c);

	rc = anx7150_i2c_read_p0_reg(hdmi->client, ANX7150_PLL_TX_AMP, &c);//jack wen
	c |= (0x01);

	rc = anx7150_i2c_write_p0_reg(hdmi->client, ANX7150_PLL_TX_AMP, &c); //TMDS swing

	c = 0x00;
	rc = anx7150_i2c_write_p0_reg(hdmi->client, ANX7150_PLL_CTRL1_REG, &c); //Added for PLL unlock issue in high temperature - Feiw
   //if (ANX7150_AUD_HW_INTERFACE == 0x02) //jack wen, spdif

	rc = anx7150_i2c_read_p0_reg(hdmi->client, ANX7150_I2S_CTRL_REG, &c);//jack wen, for spdif input from SD0.
	c &= (0xef);
	rc = anx7150_i2c_write_p0_reg(hdmi->client, ANX7150_I2S_CTRL_REG, &c);

	c = 0xc7;
	rc = anx7150_i2c_write_p0_reg(hdmi->client, 0xE1, &c);

    //ANX7150_i2c_read_p0_reg(ANX7150_SYS_CTRL1_REG, &c);
    c = 0x00;
	rc = anx7150_i2c_write_p0_reg(hdmi->client, ANX7150_SYS_CTRL1_REG, &c);//power down HDCP, 090630

	rc = anx7150_i2c_read_p0_reg(hdmi->client, ANX7150_SYS_CTRL3_REG, &c);//jack wen, for spdif input from SD0.
	c &= (0xef);
	rc = anx7150_i2c_write_p0_reg(hdmi->client, ANX7150_SYS_CTRL3_REG, &c);//power down all, 090630

    //anx7150_set_system_state(hdmi, ANX7150_WAIT_HOTPLUG);
	return rc;
}

static int anx7150_initial(struct hdmi *hdmi)
{
	int rc = 0;
	anx7150_variable_initial(hdmi);
	anx7150_hw_interface_variable_initial(hdmi);

	rc = anx7150_hardware_initial(hdmi);
	return rc;
}
static void anx7150_set_hdcp_state(struct hdmi* hdmi)
{
	struct anx7150_pdata *anx = hdmi_get_privdata(hdmi);

	anx->hdcp_on = hdmi->hdcp_on;
	return;
}
int anx7150_system_init(struct hdmi *hdmi)
{
	int rc = 0;

	if((rc = anx7150_detect_device(hdmi)) < 0)
	{
		dev_err(hdmi->dev, "anx7150 api detectdevice err!\n");
		return rc;
	}
	anx7150_set_audio_fs(hdmi);
	
	if((rc = anx7150_initial(hdmi)) < 0)
	{
		dev_err(hdmi->dev, "anx7150 initial err!\n");
		return rc;
	}
	anx7150_set_hdcp_state(hdmi);

	return 0;
}


















static u8 anx7150_detect_device(struct hdmi *hdmi)
{
	int i, rc = 0;
	char d1, d2;
	
	for (i=0; i<10; i++)
	{
		if((rc = anx7150_i2c_read_p0_reg(hdmi->client, ANX7150_DEV_IDL_REG, &d1)) < 0)
			continue;
		if((rc = anx7150_i2c_read_p0_reg(hdmi->client, ANX7150_DEV_IDH_REG, &d2)) < 0)
			continue;
		if (d1 == 0x50 && d2 == 0x71)
		{
			dev_info(hdmi->dev, "anx7150 detected!\n");
			return 1;
		}
	}
		
	dev_info(hdmi->dev, "anx7150 not detected");
	return 0;
}
static u8 anx7150_get_system_state(struct anx7150_pdata *anx)
{
	return anx->anx7150_system_state;
}
static u8 anx7150_get_hpd(struct i2c_client *client)
{
	char c;
	
	if((rc = anx7150_i2c_read_p0_reg(hdmi->client, ANX7150_SYS_CTRL3_REG, &c)) < 0)
		return rc;
	if(c & ANX7150_SYS_CTRL3_PWON_ALL)
	{
		if((rc = anx7150_i2c_read_p0_reg(hdmi->client, ANX7150_SYS_STATE_REG, &c)) < 0)
			return rc;
		return (c & ANX7150_SYS_STATE_HP)? 1:0;
	}
	else
	{
		if((rc = anx7150_i2c_read_p0_reg(hdmi->client, ANX7150_INTR_STATE_REG, &c)) < 0)
			return rc;
		return (c)? 1:0;
	}
}
static u8 anx7150_interrupt_process(struct hdmi *hdmi, int state)
{
	int hot_plug;
	int rc;

	hot_plug = anx7150_get_hpd(hdmi->client);
}

int anx7150_display_on_hw(struct hdmi *hdmi)
{
	u8 state;
	struct anx7150_pdata *anx = hdmi_get_privdata(hdmi);

	anx->anx7150_detect = anx7150_detect_device(hdmi);
	if(anx->anx7150_detect < 0)
	{
		return -EIO;
	}

	state = anx7150_get_system_state(anx);
	if(hdmi->display_on == 0 && hdmi->auto_switch == 0)
	{
		if(state > WAIT_HDMI_ENABLE)
			state = INITIAL;
	}
	if(hdmi->param_conf == 1)
	{
		if(state > WAIT_HDMI_ENABLE)
			state = WAIT_HDMI_ENABLE;
		hdmi->param_conf = 0;
	}

	state = anx7150_interrupt_process();
	
}


