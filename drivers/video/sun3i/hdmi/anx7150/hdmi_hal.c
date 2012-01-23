#include "../hdmi_hal.h"
#include "hdmi_i2cintf.h"
#include "ANX7150_Sys7150.h"

static __u8 video_mode         = ANX7150_V1280x720p_50Hz;
static __u8 sample_rate        = 0;                            //44.1khz
static __u8 mclk_fs            = 0x01;                         //256*fs
static __u8 ANX7150_system_state_prev  = 0;
static __u8 ch_need_cfg = 0;
static __u8	audio_ch = 0;

extern __u8 HPD_FLAG;

__s32 Hdmi_hal_set_display_mode(__u8 hdmi_mode)
{
    if(video_mode != hdmi_mode)
    {
        video_mode  = hdmi_mode;

        if(ANX7150_system_state>=ANX7150_CONFIG_VIDEO)
        {
            __u8 c;

            ANX7150_Set_AVMute(); //wen
            //stop HDCP and reset DDC
            ANX7150_i2c_read_p0_reg(ANX7150_HDCP_CTRL0_REG, &c);
            ANX7150_i2c_write_p0_reg(ANX7150_HDCP_CTRL0_REG, (c & (~ANX7150_HDCP_CTRL0_HW_AUTHEN)));
            ANX7150_RST_DDCChannel();
            ANX7150_Set_System_State(ANX7150_CONFIG_VIDEO);
        }
        //when clock change, clear this reg to avoid error in package config
        ANX7150_i2c_write_p1_reg(ANX7150_INFO_PKTCTRL1_REG, 0x00);     //wen
        ANX7150_i2c_write_p1_reg(ANX7150_INFO_PKTCTRL2_REG, 0x00);

        ANX7150_system_config_done = 0;
        __msg("HDMI_INFO:Hdmi_hal_set_display_mode = %d\n",video_mode);
    }

    return 0;
}

__s32 Hdmi_hal_mode_support(__u8 mode)
{
    if(ANX7150_parse_edid_done == 0)//与电视交互成功
    {
        return -1;
    }

    switch(mode)
    {
    case DISP_TV_MOD_480I:
        if(ANX7150_edid_result.supported_720x480i_60Hz)
        {
            return 0;
        }
        else
        {
            return -1;
        }
    case DISP_TV_MOD_576I:
        if(ANX7150_edid_result.supported_576i_50Hz)
        {
            return 0;
        }
        else
        {
            return -1;
        }
    case DISP_TV_MOD_480P:
        if(ANX7150_edid_result.supported_720x480p_60Hz)
        {
            return 0;
        }
        else
        {
            return -1;
        }
    case DISP_TV_MOD_576P:
        if(ANX7150_edid_result.supported_576p_50Hz)
        {
            return 0;
        }
        else
        {
            return -1;
        }
    case DISP_TV_MOD_720P_50HZ:
        if(ANX7150_edid_result.supported_720p_50Hz)
        {
            return 0;
        }
        else
        {
            return -1;
        }
    case DISP_TV_MOD_720P_60HZ:
        if(ANX7150_edid_result.supported_720p_60Hz)
        {
            return 0;
        }
        else
        {
            return -1;
        }
    case DISP_TV_MOD_1080I_50HZ:
        if(ANX7150_edid_result.supported_1080i_50Hz)
        {
            return 0;
        }
        else
        {
            return -1;
        }
    case DISP_TV_MOD_1080I_60HZ:
        if(ANX7150_edid_result.supported_1080i_60Hz)
        {
            return 0;
        }
        else
        {
            return -1;
        }
    case DISP_TV_MOD_1080P_24HZ:
        if(ANX7150_edid_result.supported_1080p_24Hz)
        {
            return 0;
        }
        else
        {
            return -1;
        }
    case DISP_TV_MOD_1080P_50HZ:
        if(ANX7150_edid_result.supported_1080p_50Hz)
        {
            return 0;
        }
        else
        {
            return -1;
        }
    case DISP_TV_MOD_1080P_60HZ:
        if(ANX7150_edid_result.supported_1080p_60Hz)
        {
            return 0;
        }
        else
        {
            return -1;
        }
    default:
        return -1;
    }
}

__s32 Hdmi_hal_get_HPD_status(void)
{
    uint8 c;

    if(ANX7150_system_state == ANX7150_PLAY_BACK)
	{
	    return HPD_FLAG;
	}
	else
	{
        ANX7150_i2c_read_p0_reg(ANX7150_INTR_STATE_REG, &c);
        if (c & 0x01)
        {
            return 1;//plugin
        }
        else
        {
            return 0;//plugout
        }
    }
}

__s32 Hdmi_hal_audio_enable(__u8 mode, __u8 channel)
{
    if(channel == 0)//disable,do it immediately
    {
        Set_IIS_CH(0);
    }
    else
    {
        channel = 1;
	    s_ANX7150_audio_config.i2s_config.audio_channel = (channel<<2);
	    ANX7150_system_config_done = 0;
	    if(ANX7150_system_state >= ANX7150_CONFIG_AUDIO)
	    {
	        Set_IIS_CH(channel);

	        ANX7150_Set_System_State(ANX7150_CONFIG_AUDIO);
	    }
	    else
	    {
	        ch_need_cfg = 1;
	    }
    }

    audio_ch = channel;
    __msg("HDMI_INFO:Hdmi_hal_audio_enable, ch = %d\n",channel);

    return 0;
}

__s32 Hdmi_hal_set_audio_para(hdmi_audio_t * audio_para)
{
    __u8   c;
    __u8   new_fs           = 0x01;
    __u8   new_sample_rate  = 0x00;

    if(!audio_para)
    {
        return -1;
    }

    switch(audio_para->fs_between)
    {
    case 128:
        new_fs = 0x00;
        break;
    case 256:
        new_fs = 0x01;
        break;
    case 384:
        new_fs = 0x02;
        break;
    case 512:
        new_fs = 0x03;
        break;
    }

    switch(audio_para->sample_rate)
    {
    case 32000:
        new_sample_rate = 0x03;
        break;
    case 44100:
        new_sample_rate = 0x00;
        break;
    case 88200:
        new_sample_rate = 0x08;
        break;
    case 176400:
        new_sample_rate = 0x0c;
        break;
    case 48000:
        new_sample_rate = 0x02;
        break;
    case 96000:
        new_sample_rate = 0x0a;
        break;
    case 196000:
        new_sample_rate = 0x0e;
        break;
    case 768000:
        new_sample_rate = 0x09;
        break;
    }

    if(new_fs != mclk_fs)
    {
        mclk_fs    = new_fs;
        FREQ_MCLK = mclk_fs;
        ANX7150_system_config_done = 0;
        if(ANX7150_system_state >= ANX7150_CONFIG_AUDIO)
        {
            ANX7150_i2c_read_p0_reg(0x51,&c);
            c &= ~0x80;
            ANX7150_i2c_write_p0_reg(0x51,c);

            ANX7150_Set_System_State(ANX7150_CONFIG_AUDIO);
        }
        __msg("HDMI_INFO:hdmi audio mclk changed = %d*fs\n",audio_para->fs_between);
    }

    if(new_sample_rate != sample_rate)
    {
        sample_rate    = new_sample_rate;
        ANX7150_system_config_done = 0;
        if(ANX7150_system_state >= ANX7150_CONFIG_AUDIO)
        {
            ANX7150_i2c_read_p0_reg(0x51,&c);
            c &= ~0x80;
            ANX7150_i2c_write_p0_reg(0x51,c);

            ANX7150_Set_System_State(ANX7150_CONFIG_AUDIO);
        }
        __msg("HDMI_INFO:hdmi audio sample_rate changed = %dHz\n",audio_para->sample_rate);
    }

    return 0;
}

__s32 Hdmi_hal_get_connection_status(void)
{
    return ANX7150_system_state;
}

__s32 Hdmi_hal_standby_exit(void)
{
    HDMI_System_Init();                 //initial register and in power down mode

    return 0;
}

__s32 Hdmi_hal_main_task(void)
{
    if((ANX7150_parse_edid_done == 1) && (ANX7150_system_config_done == 0))
    {
        //system should config all the parameters here
        ANX7150_API_System_Config(video_mode,input_pixel_clk_1x_repeatition,sample_rate);
        ANX7150_system_config_done = 1;
    }

    ANX7150_Task();//真正进行配置和管理,包括plugin plugout的检测

    if(ANX7150_system_state == ANX7150_PLAY_BACK && ch_need_cfg==1)
    {
        Set_IIS_CH(audio_ch);
        ch_need_cfg = 0;
    }

    if(ANX7150_system_state_prev != ANX7150_system_state)
    {
        __msg("HDMI SYSTEM STATE = %d\n",ANX7150_system_state);
        ANX7150_system_state_prev = ANX7150_system_state;
    }

    return 0;
}

__s32 Hdmi_hal_init(void)
{
    audio_ch = 0;
    ch_need_cfg = 0;

    Set_IIS_CH(0);
    HDMI_System_Init();                 //initial register and in power down mode

    return 0;
}

__s32 Hdmi_hal_exit(void)
{
    HDMI_System_Init();                 //initial register and in power down mode

    return 0;
}

