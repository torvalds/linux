#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/hdmi.h>
#include <linux/slab.h>

#include "anx7150.h"
#include "anx7150_hw.h"
//#ifdef ITU656
struct ANX7150_video_timingtype ANX7150_video_timingtype_table =
{
    //640x480p-60hz
    {0x20/*H_RES_LOW*/, 0x03/*H_RES_HIGH*/,0x80 /*ACT_PIX_LOW*/,0x02 /*ACT_PIX_HIGH*/,
        0x60/*HSYNC_WIDTH_LOW*/,0x00 /*HSYNC_WIDTH_HIGH*/,0x30 /*H_BP_LOW*/,0x00 /*H_BP_HIGH*/,
        0xe0/*ACT_LINE_LOW*/, 0x01/*ACT_LINE_HIGH*/,0x02 /*VSYNC_WIDTH*/, 0x21/*V_BP_LINE*/,
        0x0a/*V_FP_LINE*/,0x10 /*H_FP_LOW*/, 0x00/*H_FP_HIGH*/,
        ANX7150_Progressive, ANX7150_Neg_Hsync_pol, ANX7150_Neg_Vsync_pol},
    //720x480p-60hz
    {0x5a/*H_RES_LOW*/,0x03 /*H_RES_HIGH*/,0xd0/*ACT_PIX_LOW*/, 0x02/*ACT_PIX_HIGH*/,
     0x3e/*HSYNC_WIDTH_LOW*/, 0x00/*HSYNC_WIDTH_HIGH*/, 0x3c/*H_BP_LOW*/, 0x00/*H_BP_HIGH*/,
     0xe0/*ACT_LINE_LOW*/, 0x01/*ACT_LINE_HIGH*/, 0x06/*VSYNC_WIDTH*/, 0x1e/*V_BP_LINE*/,
     0x09/*V_FP_LINE*/, 0x10/*H_FP_LOW*/, 0x00/*H_FP_HIGH*/,
     ANX7150_Progressive, ANX7150_Neg_Hsync_pol, ANX7150_Neg_Vsync_pol},
    //720p-60hz
    {0x72/*H_RES_LOW*/, 0x06/*H_RES_HIGH*/, 0x00/*ACT_PIX_LOW*/, 0x05/*ACT_PIX_HIGH*/,
     0x28/*HSYNC_WIDTH_LOW*/, 0x00/*HSYNC_WIDTH_HIGH*/, 0xdc/*H_BP_LOW*/, 0x00/*H_BP_HIGH*/,
     0xd0/*ACT_LINE_LOW*/, 0x02/*ACT_LINE_HIGH*/, 0x05/*VSYNC_WIDTH*/, 0x14/*V_BP_LINE*/,
     0x05/*V_FP_LINE*/, 0x6e/*H_FP_LOW*/, 0x00/*H_FP_HIGH*/,
     ANX7150_Progressive, ANX7150_Pos_Hsync_pol, ANX7150_Pos_Vsync_pol},
    //1080i-60hz
    {0x98/*H_RES_LOW*/, 0x08/*H_RES_HIGH*/, 0x80/*ACT_PIX_LOW*/, 0x07/*ACT_PIX_HIGH*/,
     0x2c/*HSYNC_WIDTH_LOW*/, 0x00/*HSYNC_WIDTH_HIGH*/, 0x94/*H_BP_LOW*/, 0x00/*H_BP_HIGH*/,
     0x38/*ACT_LINE_LOW*/, 0x04/*ACT_LINE_HIGH*/, 0x05/*VSYNC_WIDTH*/, 0x0f/*V_BP_LINE*/,
     0x02/*V_FP_LINE*/, 0x58/*H_FP_LOW*/, 0x00/*H_FP_HIGH*/,
     ANX7150_Interlace, ANX7150_Pos_Hsync_pol, ANX7150_Pos_Vsync_pol},
    //720x480i-60hz
    {0x5a/*H_RES_LOW*/,0x03 /*H_RES_HIGH*/,0xd0/*ACT_PIX_LOW*/, 0x02/*ACT_PIX_HIGH*/,
     0x3e/*HSYNC_WIDTH_LOW*/, 0x00/*HSYNC_WIDTH_HIGH*/, 0x39/*H_BP_LOW*/, 0x00/*H_BP_HIGH*/,
     0xe0/*ACT_LINE_LOW*/, 0x01/*ACT_LINE_HIGH*/, 0x03/*VSYNC_WIDTH*/, 0x0f/*V_BP_LINE*/,
     0x04/*V_FP_LINE*/, 0x13/*H_FP_LOW*/, 0x00/*H_FP_HIGH*/,
     ANX7150_Interlace, ANX7150_Neg_Hsync_pol, ANX7150_Neg_Vsync_pol},											//update
	//1080p-60hz
		{0x98/*H_RES_LOW*/, 0x08/*H_RES_HIGH*/, 0x80/*ACT_PIX_LOW*/, 0x07/*ACT_PIX_HIGH*/,
		 0x2c/*HSYNC_WIDTH_LOW*/, 0x00/*HSYNC_WIDTH_HIGH*/, 0x94/*H_BP_LOW*/, 0x00/*H_BP_HIGH*/,
		 0x38/*ACT_LINE_LOW*/, 0x04/*ACT_LINE_HIGH*/, 0x05/*VSYNC_WIDTH*/, 0x24/*V_BP_LINE*/,
		 0x04/*V_FP_LINE*/, 0x58/*H_FP_LOW*/, 0x00/*H_FP_HIGH*/,
		 ANX7150_Interlace, ANX7150_Pos_Hsync_pol, ANX7150_Pos_Vsync_pol},
	//576p-50hz
    {0x60/*H_RES_LOW*/,0x03 /*H_RES_HIGH*/,0xd0 /*ACT_PIX_LOW*/, 0x02/*ACT_PIX_HIGH*/,
     0x40/*HSYNC_WIDTH_LOW*/, 0x00/*HSYNC_WIDTH_HIGH*/, 0x44/*H_BP_LOW*/,0x00 /*H_BP_HIGH*/,
     0x40/*ACT_LINE_LOW*/, 0x02/*ACT_LINE_HIGH*/, 0x05/*VSYNC_WIDTH*/, 0x27/*V_BP_LINE*/,
     0x05/*V_FP_LINE*/, 0x0c/*H_FP_LOW*/, 0x00/*H_FP_HIGH*/,
     ANX7150_Progressive, ANX7150_Neg_Hsync_pol, ANX7150_Neg_Vsync_pol},
    //720p-50hz
    {0xbc/*H_RES_LOW*/, 0x07/*H_RES_HIGH*/, 0x00/*ACT_PIX_LOW*/, 0x05/*ACT_PIX_HIGH*/,
     0x28/*HSYNC_WIDTH_LOW*/, 0x00/*HSYNC_WIDTH_HIGH*/, 0xdc/*H_BP_LOW*/, 0x00/*H_BP_HIGH*/,
     0xd0/*ACT_LINE_LOW*/, 0x02/*ACT_LINE_HIGH*/, 0x05/*VSYNC_WIDTH*/, 0x14/*V_BP_LINE*/,
     0x05/*V_FP_LINE*/, 0xb8/*H_FP_LOW*/, 0x01/*H_FP_HIGH*/,
     ANX7150_Progressive, ANX7150_Pos_Hsync_pol, ANX7150_Pos_Vsync_pol},
    //1080i-50hz
    {0x50/*H_RES_LOW*/, 0x0a/*H_RES_HIGH*/, 0x80/*ACT_PIX_LOW*/, 0x07/*ACT_PIX_HIGH*/,
     0x2c/*HSYNC_WIDTH_LOW*/, 0x00/*HSYNC_WIDTH_HIGH*/, 0x94/*H_BP_LOW*/, 0x00/*H_BP_HIGH*/,
     0x38/*ACT_LINE_LOW*/, 0x04/*ACT_LINE_HIGH*/, 0x05/*VSYNC_WIDTH*/, 0x0f/*V_BP_LINE*/,
     0x02/*V_FP_LINE*/, 0x10/*H_FP_LOW*/, 0x02/*H_FP_HIGH*/,
     ANX7150_Interlace, ANX7150_Pos_Hsync_pol, ANX7150_Pos_Vsync_pol},
    //576i-50hz
    {0x60/*H_RES_LOW*/,0x03 /*H_RES_HIGH*/,0xd0 /*ACT_PIX_LOW*/, 0x02/*ACT_PIX_HIGH*/,
     0x3f/*HSYNC_WIDTH_LOW*/, 0x00/*HSYNC_WIDTH_HIGH*/, 0x45/*H_BP_LOW*/,0x00 /*H_BP_HIGH*/,
     0x40/*ACT_LINE_LOW*/,0x02 /*ACT_LINE_HIGH*/, 0x03/*VSYNC_WIDTH*/, 0x13/*V_BP_LINE*/,
     0x02/*V_FP_LINE*/, 0x0c/*H_FP_LOW*/, 0x00/*H_FP_HIGH*/,
     ANX7150_Interlace, ANX7150_Neg_Hsync_pol, ANX7150_Neg_Vsync_pol},
     
	//1080p-50hz
	 {0x50/*H_RES_LOW*/, 0x0a/*H_RES_HIGH*/, 0x80/*ACT_PIX_LOW*/, 0x07/*ACT_PIX_HIGH*/,
	  0x2c/*HSYNC_WIDTH_LOW*/, 0x00/*HSYNC_WIDTH_HIGH*/, 0x94/*H_BP_LOW*/, 0x00/*H_BP_HIGH*/,
	  0x38/*ACT_LINE_LOW*/, 0x04/*ACT_LINE_HIGH*/, 0x05/*VSYNC_WIDTH*/, 0x24/*V_BP_LINE*/,
	  0x04/*V_FP_LINE*/, 0x10/*H_FP_LOW*/, 0x02/*H_FP_HIGH*/,
	  ANX7150_Interlace, ANX7150_Pos_Hsync_pol, ANX7150_Pos_Vsync_pol},
};
//#endif
int anx7150_mass_read_need_delay = 0;

u8 g_video_format = 0x00;
u8 g_audio_format = 0x00;


u8 timer_slot = 0;
u8 *ANX7150_EDID_Buf = NULL;
u8 ANX7150_avi_data[19];//, ANX7150_avi_checksum;
u8 ANX7150_system_state = HDMI_INITIAL;
u8 spdif_error_cnt = 0x00;
u8 misc_reset_needed;
u8 ANX7150_stdaddr,ANX7150_stdreg,ANX7150_ext_block_num;
u8 ANX7150_svd_length,ANX7150_sau_length;
u8 ANX7150_edid_dtd[18];
u32 ANX7150_edid_length;
ANX7150_edid_result_4_system ANX7150_edid_result;

u8 ANX7150_ddc_fifo_full;
u8 ANX7150_ddc_progress;
u8 ANX7150_hdcp_auth_en;
//u8 ANX7150_bksv_ready; //replace by srm_checked xy 01.09
u8 ANX7150_HDCP_enable;
u8 ANX7150_ksv_srm_pass;
u8 ANX7150_hdcp_bcaps;
u8 ANX7150_hdcp_bstatus[2];
u8 ANX7150_srm_checked;
u8 ANX7150_hdcp_auth_pass;
u8 ANX7150_avmute_enable;
u8 ANX7150_send_blue_screen;
u8 ANX7150_hdcp_encryption;
u8 ANX7150_hdcp_init_done;
u8 ANX7150_hdcp_wait_100ms_needed;
u8 ANX7150_auth_fully_pass;
u8 ANX7150_parse_edid_done;//060714 XY
//u8 testen;
//u8 ANX7150_avi_data[19], ANX7150_avi_checksum;
u8 ANX7150_hdcp_auth_fail_counter ;

u8 ANX7150_video_format_config;
u8 ANX7150_emb_sync_mode,ANX7150_de_gen_en,ANX7150_demux_yc_en,ANX7150_ddr_bus_mode;
u8 ANX7150_ddr_edge,ANX7150_ycmux_u8_sel;
u8 ANX7150_system_config_done;
u8 ANX7150_RGBorYCbCr; //modified by zy 060814
u8 ANX7150_in_pix_rpt,ANX7150_tx_pix_rpt;
u8 ANX7150_in_pix_rpt_bkp,ANX7150_tx_pix_rpt_bkp;
u8 ANX7150_video_timing_id;
u8 ANX7150_pix_rpt_set_by_sys;
u8 ANX7150_video_timing_parameter[18];
u8 switch_value_sw_backup,switch_value_pc_backup;
u8 switch_value,bist_switch_value_pc;
u8 ANX7150_new_csc,ANX7150_new_vid_id,ANX7150_new_HW_interface;
u8 ANX7150_INT_Done;

audio_config_struct s_ANX7150_audio_config;
config_packets s_ANX7150_packet_config;

u8 FREQ_MCLK;         //0X72:0X50 u82:0
//000b:Fm = 128*Fs
//001b:Fm = 256*Fs
//010b:Fm = 384*Fs
//011b:Fm = 512*Fs
u8 ANX7150_audio_clock_edge;
int ANX7150_DDC_Mass_Read(struct i2c_client *client, u8 *buf, u16 len);

int anx7150_detect_device(struct anx7150_pdata *anx)
{
    int i, rc = 0; 
    char d1, d2;
    
    for (i=0; i<10; i++) 
    {    
        if((rc = anx7150_i2c_read_p0_reg(anx->client, ANX7150_DEV_IDL_REG, &d1)) < 0) 
            continue;
        if((rc = anx7150_i2c_read_p0_reg(anx->client, ANX7150_DEV_IDH_REG, &d2)) < 0) 
            continue;
        if (d1 == 0x50 && d2 == 0x71)
        {    
            hdmi_dbg(&anx->client->dev, "anx7150 detected!\n");
            return 0;
        }    
    }    
     
    hdmi_dbg(&anx->client->dev, "anx7150 not detected");
    return -1;
}
u8 ANX7150_Get_System_State(void)
{
	return ANX7150_system_state;
}
void ANX7150_Set_System_State(struct i2c_client *client, u8 new_state)
{
    ANX7150_system_state = new_state;
    switch (ANX7150_system_state)
    {
        case HDMI_INITIAL:
            hdmi_dbg(&client->dev, "INITIAL\n");
            break;
        case WAIT_HOTPLUG:
            hdmi_dbg(&client->dev, "WAIT_HOTPLUG\n");
            break;
        case READ_PARSE_EDID:
            hdmi_dbg(&client->dev, "READ_PARSE_EDID\n");
            break;
        case WAIT_RX_SENSE:
            hdmi_dbg(&client->dev, "WAIT_RX_SENSE\n");
            break;
		case WAIT_HDMI_ENABLE:
			hdmi_dbg(&client->dev, "WAIT_HDMI_ENABLE\n");
			break;
		case SYSTEM_CONFIG:
			hdmi_dbg(&client->dev, "SYSTEM_CONFIG\n");
			break;
        case CONFIG_VIDEO:
            dev_info(&client->dev, "CONFIG_VIDEO\n");
            break;
        case CONFIG_AUDIO:
            hdmi_dbg(&client->dev, "CONFIG_AUDIO\n");
            break;
        case CONFIG_PACKETS:
            hdmi_dbg(&client->dev, "CONFIG_PACKETS\n");
            break;
        case HDCP_AUTHENTICATION:
            hdmi_dbg(&client->dev, "HDCP_AUTHENTICATION\n");
            break;
            ////////////////////////////////////////////////
            // System ANX7150_RESET_LINK is kept for RX clock recovery error case, not used in normal case.
        case RESET_LINK:
            hdmi_dbg(&client->dev, "RESET_LINK\n");
            break;
            ////////////////////////////////////////////////
        case PLAY_BACK:
            dev_info(&client->dev, "PLAY_BACK\n");
            break;
		default:
			hdmi_dbg(&client->dev, "unknown state\n");
			break;
    }
}

int anx7150_get_hpd(struct i2c_client *client)
{
	int rc = 0;
	char sys_ctl3, intr_state, sys_state, hpd_state;
	
	if((rc = anx7150_i2c_read_p0_reg(client, ANX7150_SYS_CTRL3_REG, &sys_ctl3)) < 0)
		return rc;
	if(sys_ctl3 & ANX7150_SYS_CTRL3_PWON_ALL)
	{
		if((rc = anx7150_i2c_read_p0_reg(client, ANX7150_SYS_STATE_REG, &sys_state)) < 0)
			return rc;
		hpd_state = (sys_state & ANX7150_SYS_STATE_HP)? 1:0;
	}
	else
	{
		if((rc = anx7150_i2c_read_p0_reg(client, ANX7150_INTR_STATE_REG, &intr_state)) < 0)
			return rc;
		hpd_state = (intr_state)? 1:0;
	}
	return hpd_state;
}
static int anx7150_get_interrupt_status(struct i2c_client *client, struct anx7150_interrupt_s *interrupt_staus)
{
	int rc = 0;
	u8 int_s1;
	u8 int_s2;
	u8 int_s3;
	
	rc = anx7150_i2c_read_p0_reg(client, ANX7150_INTR1_STATUS_REG, &int_s1);//jack wen, for spdif input from SD0.
	rc |= anx7150_i2c_write_p0_reg(client, ANX7150_INTR1_STATUS_REG, &int_s1);//power down all, 090630
	rc |= anx7150_i2c_read_p0_reg(client, ANX7150_INTR2_STATUS_REG, &int_s2);//jack wen, for spdif input from SD0.
	rc |= anx7150_i2c_write_p0_reg(client, ANX7150_INTR2_STATUS_REG, &int_s2);//power down all, 090630
	rc |= anx7150_i2c_read_p0_reg(client, ANX7150_INTR3_STATUS_REG, &int_s3);//jack wen, for spdif input from SD0.
	rc |= anx7150_i2c_write_p0_reg(client, ANX7150_INTR3_STATUS_REG, &int_s3);//power down all, 090630

	interrupt_staus->hotplug_change = (int_s1 & ANX7150_INTR1_STATUS_HP_CHG) ? 1 : 0;
	interrupt_staus->video_format_change = (int_s3 & ANX7150_INTR3_STATUS_VIDF_CHG) ? 1 : 0;
	interrupt_staus->auth_done = (int_s2 & ANX7150_INTR2_STATUS_AUTH_DONE) ? 1 : 0;
	interrupt_staus->auth_state_change = (int_s2 & ANX7150_INTR2_STATUS_AUTH_CHG) ? 1 : 0;
	interrupt_staus->pll_lock_change = (int_s2 & ANX7150_INTR2_STATUS_PLLLOCK_CHG) ? 1 : 0;
	interrupt_staus->rx_sense_change = (int_s3 & ANX7150_INTR3_STATUS_RXSEN_CHG) ? 1 : 0;
	interrupt_staus->HDCP_link_change = (int_s2 & ANX7150_INTR2_STATUS_HDCPLINK_CHK) ? 1 : 0;
	interrupt_staus->audio_clk_change = (int_s3 & ANX7150_INTR3_STATUS_AUDCLK_CHG) ? 1 : 0;
	interrupt_staus->audio_FIFO_overrun = (int_s1 & ANX7150_INTR1_STATUS_AFIFO_OVER) ? 1 : 0;
	interrupt_staus->SPDIF_error = (int_s1 & ANX7150_INTR1_STATUS_SPDIF_ERR) ? 1 : 0;
	interrupt_staus->SPDIF_bi_phase_error = ((int_s3 & ANX7150_INTR3_STATUS_SPDIFBI_ERR) ? 1 : 0) 
										|| ((int_s3 & ANX7150_INTR3_STATUS_SPDIF_UNSTBL) ? 1 : 0);
	return 0;
}
static void ANX7150_Variable_Initial(void)
{
    u8 i;

    ANX7150_hdcp_auth_en = 0;
    ANX7150_ksv_srm_pass =0;
    ANX7150_srm_checked = 0;
    ANX7150_hdcp_auth_pass = 0;
    ANX7150_avmute_enable = 1;
    ANX7150_hdcp_auth_fail_counter =0;
    ANX7150_hdcp_encryption = 0;
    ANX7150_send_blue_screen = 0;
    ANX7150_hdcp_init_done = 0;
    ANX7150_hdcp_wait_100ms_needed = 1;
    ANX7150_auth_fully_pass = 0;
    timer_slot = 0;
    //********************for video config**************
    ANX7150_video_timing_id = 0;
    ANX7150_in_pix_rpt = 0;
    ANX7150_tx_pix_rpt = 0;
    ANX7150_new_csc = 0;
    ANX7150_new_vid_id = 0;
    ANX7150_new_HW_interface = 0;
    //********************end of video config*********

    //********************for edid parse***********
    ANX7150_edid_result.is_HDMI = 1;
    ANX7150_edid_result.ycbcr422_supported = 0;
    ANX7150_edid_result.ycbcr444_supported = 0;
    ANX7150_edid_result.supported_720p_60Hz = 0;
    ANX7150_edid_result.supported_720p_50Hz = 0;
    ANX7150_edid_result.supported_576p_50Hz = 0;
    ANX7150_edid_result.supported_576i_50Hz = 0;
    ANX7150_edid_result.supported_1080i_60Hz = 0;
    ANX7150_edid_result.supported_1080i_50Hz = 0;
    ANX7150_edid_result.supported_640x480p_60Hz = 0;
    ANX7150_edid_result.supported_720x480p_60Hz = 0;
    ANX7150_edid_result.supported_720x480i_60Hz = 0;
    ANX7150_edid_result.edid_errcode = 0;
    ANX7150_edid_result.SpeakerFormat = 0;
    for (i = 0; i < 8; i ++)
    {
        ANX7150_edid_result.AudioChannel[i] = 0;
        ANX7150_edid_result.AudioFormat[i] = 0;
        ANX7150_edid_result.AudioFs[i] = 0;
        ANX7150_edid_result.AudioLength[i] = 0;
    }
    //********************end of edid**************

    s_ANX7150_packet_config.packets_need_config = 0x03;   //new avi infoframe
    s_ANX7150_packet_config.avi_info.type = 0x82;
    s_ANX7150_packet_config.avi_info.version = 0x02;
    s_ANX7150_packet_config.avi_info.length = 0x0d;
    s_ANX7150_packet_config.avi_info.pb_u8[1] = 0x21;//YCbCr422
    s_ANX7150_packet_config.avi_info.pb_u8[2] = 0x08;
    s_ANX7150_packet_config.avi_info.pb_u8[3] = 0x00;
    s_ANX7150_packet_config.avi_info.pb_u8[4] = 0x00;
    s_ANX7150_packet_config.avi_info.pb_u8[5] = 0x00;
    s_ANX7150_packet_config.avi_info.pb_u8[6] = 0x00;
    s_ANX7150_packet_config.avi_info.pb_u8[7] = 0x00;
    s_ANX7150_packet_config.avi_info.pb_u8[8] = 0x00;
    s_ANX7150_packet_config.avi_info.pb_u8[9] = 0x00;
    s_ANX7150_packet_config.avi_info.pb_u8[10] = 0x00;
    s_ANX7150_packet_config.avi_info.pb_u8[11] = 0x00;
    s_ANX7150_packet_config.avi_info.pb_u8[12] = 0x00;
    s_ANX7150_packet_config.avi_info.pb_u8[13] = 0x00;

    // audio infoframe
    s_ANX7150_packet_config.audio_info.type = 0x84;
    s_ANX7150_packet_config.audio_info.version = 0x01;
    s_ANX7150_packet_config.audio_info.length = 0x0a;
    s_ANX7150_packet_config.audio_info.pb_u8[1] = 0x00;  //zy 061123 for ATC
    s_ANX7150_packet_config.audio_info.pb_u8[2] = 0x00;
    s_ANX7150_packet_config.audio_info.pb_u8[3] = 0x00;
    s_ANX7150_packet_config.audio_info.pb_u8[4] = 0x00;
    s_ANX7150_packet_config.audio_info.pb_u8[5] = 0x00;
    s_ANX7150_packet_config.audio_info.pb_u8[6] = 0x00;
    s_ANX7150_packet_config.audio_info.pb_u8[7] = 0x00;
    s_ANX7150_packet_config.audio_info.pb_u8[8] = 0x00;
    s_ANX7150_packet_config.audio_info.pb_u8[9] = 0x00;
    s_ANX7150_packet_config.audio_info.pb_u8[10] = 0x00;

    ANX7150_INT_Done = 0;
}
static void ANX7150_HW_Interface_Variable_Initial(void)
{
    u8 c;

    ANX7150_video_format_config = 0x00;
    ANX7150_RGBorYCbCr = 0x00;
    ANX7150_ddr_edge = ANX7150_IDCK_EDGE_DDR;

    c = 0;
    c = (ANX7150_I2S_CH0_ENABLE << 2) | (ANX7150_I2S_CH1_ENABLE << 3) |
        (ANX7150_I2S_CH2_ENABLE << 4) | (ANX7150_I2S_CH3_ENABLE << 5);
    s_ANX7150_audio_config.audio_type = ANX7150_AUD_HW_INTERFACE;     // input I2S
    s_ANX7150_audio_config.down_sample = 0x00;
    s_ANX7150_audio_config.i2s_config.audio_channel = c;//0x04;
    s_ANX7150_audio_config.i2s_config.Channel_status1 =0x00;
    s_ANX7150_audio_config.i2s_config.Channel_status1 = 0x00;
    s_ANX7150_audio_config.i2s_config.Channel_status2 = 0x00;
    s_ANX7150_audio_config.i2s_config.Channel_status3 = 0x00;
    s_ANX7150_audio_config.i2s_config.Channel_status4 = 0x00;//0x02;//48k
    s_ANX7150_audio_config.i2s_config.Channel_status5 = ANX7150_I2S_WORD_LENGTH;//0x0b;
    s_ANX7150_audio_config.audio_layout = 0x00;

    c = (ANX7150_I2S_SHIFT_CTRL << 3) | (ANX7150_I2S_DIR_CTRL << 2)  |
        (ANX7150_I2S_WS_POL << 1) | ANX7150_I2S_JUST_CTRL;
    s_ANX7150_audio_config.i2s_config.i2s_format = c;//0x00;

    FREQ_MCLK = ANX7150_MCLK_Fs_RELATION;//set the relation of MCLK and WS
    ANX7150_audio_clock_edge = ANX7150_AUD_CLK_EDGE;


}
static int anx7150_hardware_initial(struct i2c_client *client)
{
	int rc = 0;
    char c = 0;
	
    //clear HDCP_HPD_RST
    rc = anx7150_i2c_read_p0_reg(client, ANX7150_SYS_CTRL2_REG, &c);
	c |= (0x01);
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_SYS_CTRL2_REG, &c);

   	mdelay(10);

	c &= (~0x01);
    rc = anx7150_i2c_write_p0_reg(client, ANX7150_SYS_CTRL2_REG, &c);
	
    //Power on I2C
    rc = anx7150_i2c_read_p0_reg(client, ANX7150_SYS_CTRL3_REG, &c);
	c |= (ANX7150_SYS_CTRL3_I2C_PWON);
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_SYS_CTRL3_REG, &c);

	c = 0x00;
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_SYS_CTRL2_REG, &c);
	c= 0x00;
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_SRST_REG, &c);

    //clear HDCP_HPD_RST
	rc = anx7150_i2c_read_p0_reg(client, ANX7150_SYS_CTRL1_REG, &c);
	c &= (0xbf);
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_SYS_CTRL1_REG, &c);

    //Power on Audio capture and Video capture module clock
    rc = anx7150_i2c_read_p0_reg(client, ANX7150_SYS_PD_REG, &c);
	c |= (0x06);
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_SYS_PD_REG, &c);

    //Enable auto set clock range for video PLL
    rc = anx7150_i2c_read_p0_reg(client, ANX7150_CHIP_CTRL_REG, &c);
	c &= (0x00);
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_CHIP_CTRL_REG, &c);

    //Set registers value of Blue Screen when HDCP authentication failed--RGB mode,green field
    c = 0x10;
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_HDCP_BLUESCREEN0_REG, &c);
	c = 0xeb;
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_HDCP_BLUESCREEN1_REG, &c);
	c = 0x10;
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_HDCP_BLUESCREEN2_REG, &c);

    //ANX7150_i2c_read_p0_reg(ANX7150_TMDS_CLKCH_CONFIG_REG, &c);
    //ANX7150_i2c_write_p0_reg(ANX7150_TMDS_CLKCH_CONFIG_REG, (c | 0x80));

	rc = anx7150_i2c_read_p0_reg(client, ANX7150_PLL_CTRL0_REG, &c);
	c = 0x00;
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_PLL_CTRL0_REG, &c);

	rc = anx7150_i2c_read_p0_reg(client, ANX7150_CHIP_DEBUG1_CTRL_REG, &c);
	c |= (0x08);
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_CHIP_DEBUG1_CTRL_REG, &c);

	rc = anx7150_i2c_read_p0_reg(client, ANX7150_PLL_TX_AMP, &c);//jack wen
	c |= (0x01);

	rc = anx7150_i2c_write_p0_reg(client, ANX7150_PLL_TX_AMP, &c); //TMDS swing

	c = 0x00;
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_PLL_CTRL1_REG, &c); //Added for PLL unlock issue in high temperature - Feiw
   //if (ANX7150_AUD_HW_INTERFACE == 0x02) //jack wen, spdif

	rc = anx7150_i2c_read_p0_reg(client, ANX7150_I2S_CTRL_REG, &c);//jack wen, for spdif input from SD0.
	c &= (0xef);
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_I2S_CTRL_REG, &c);

	c = 0xc7;
	rc = anx7150_i2c_write_p0_reg(client, 0xE1, &c);

    //ANX7150_i2c_read_p0_reg(ANX7150_SYS_CTRL1_REG, &c);
    c = 0x00;
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_SYS_CTRL1_REG, &c);//power down HDCP, 090630

	rc = anx7150_i2c_read_p0_reg(client, ANX7150_SYS_CTRL3_REG, &c);//jack wen, for spdif input from SD0.
	c &= (0xfe);
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_SYS_CTRL3_REG, &c);//power down all, 090630

	return rc;
}

int anx7150_rst_ddcchannel(struct i2c_client *client)
{
	int rc = 0;
	char c;
    //Reset the DDC channel
    rc = anx7150_i2c_read_p0_reg(client, ANX7150_SYS_CTRL2_REG, &c);

	c |= (ANX7150_SYS_CTRL2_DDC_RST);
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_SYS_CTRL2_REG, &c);

	c &= (~ANX7150_SYS_CTRL2_DDC_RST);
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_SYS_CTRL2_REG, &c);


	c = 0x00;
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_DDC_ACC_CMD_REG, &c);//abort current operation

	c = 0x06;
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_DDC_ACC_CMD_REG, &c);//reset I2C command

	//Clear FIFO
	c = 0x05;
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_DDC_ACC_CMD_REG, &c);//reset I2C command

	return rc;
}

int anx7150_initial(struct i2c_client *client)
{
    ANX7150_Variable_Initial();   //simon
    ANX7150_HW_Interface_Variable_Initial();  //simon
    
    anx7150_hardware_initial(client);   //simon
	return 0;
}
int anx7150_unplug(struct i2c_client *client)
{
	int rc = 0;
	char c;
	dev_info(&client->dev, "anx7150 unplug\n");
	
    //wen HDCP CTS
    ANX7150_Variable_Initial();   //simon
    ANX7150_HW_Interface_Variable_Initial();  //simon
    
    rc = anx7150_hardware_initial(client);   //simon
    if(rc < 0)
		dev_err(&client->dev, "%s>> i2c transfer err\n", __func__);

	c = 0x00;
    rc = anx7150_i2c_write_p0_reg(client, ANX7150_HDCP_CTRL0_REG, &c); //simon
    if(rc < 0)
		dev_err(&client->dev, "%s>> i2c transfer err\n", __func__);
    //wen HDCP CTS
    ANX7150_hdcp_wait_100ms_needed = 1;
    ANX7150_auth_fully_pass = 0;

    // clear ANX7150_parse_edid_done & ANX7150_system_config_done
    ANX7150_parse_edid_done = 0;
//    ANX7150_system_config_done = 0;
    ANX7150_srm_checked = 0;

	return rc;
}
int anx7150_plug(struct i2c_client *client)
{
	int rc = 0;
	char c;

	dev_info(&client->dev, "anx7150 plug\n");

	rc = anx7150_i2c_read_p0_reg(client, ANX7150_SYS_CTRL3_REG, &c);
	c |= (0x01);
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_SYS_CTRL3_REG, &c);//power up all, 090630

    //disable audio & video & hdcp & TMDS and init    begin
    rc = anx7150_i2c_read_p0_reg(client, ANX7150_HDMI_AUDCTRL1_REG, &c);
	c &= (~ANX7150_HDMI_AUDCTRL1_IN_EN);
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_HDMI_AUDCTRL1_REG, &c);

	rc = anx7150_i2c_read_p0_reg(client, ANX7150_VID_CTRL_REG, &c);
	c &= (~ANX7150_VID_CTRL_IN_EN);
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_VID_CTRL_REG, &c);

	rc = anx7150_i2c_read_p0_reg(client, ANX7150_TMDS_CLKCH_CONFIG_REG, &c);
	c &= (~ANX7150_TMDS_CLKCH_MUTE);
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_TMDS_CLKCH_CONFIG_REG, &c);

	rc = anx7150_i2c_read_p0_reg(client, ANX7150_HDCP_CTRL0_REG, &c);
	c &= (~ANX7150_HDCP_CTRL0_HW_AUTHEN);
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_HDCP_CTRL0_REG, &c);

    ANX7150_Variable_Initial();
    //disable video & audio & hdcp & TMDS and init    end

    
    //Power on chip and select DVI mode
    rc = anx7150_i2c_read_p0_reg(client, ANX7150_SYS_CTRL1_REG, &c);
	c |= (0x05);
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_SYS_CTRL1_REG, &c);//  cwz change 0x01 -> 0x05

	rc = anx7150_i2c_read_p0_reg(client, ANX7150_SYS_CTRL1_REG, &c);
	c &= (0xfd);
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_SYS_CTRL1_REG, &c);

    //D("ANX7150 is set to DVI mode\n");
    rc = anx7150_rst_ddcchannel(client);
    //Initial Interrupt
    // disable video/audio CLK,Format change and before config video. 060713 xy

	c = 0x04;
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_INTR1_MASK_REG, &c);

	c = 0x00;
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_INTR2_MASK_REG, &c);

	c = 0x00;
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_INTR3_MASK_REG, &c);

	rc = anx7150_i2c_read_p0_reg(client, ANX7150_INTR1_STATUS_REG, &c);
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_INTR1_STATUS_REG, &c);

	rc = anx7150_i2c_read_p0_reg(client, ANX7150_INTR2_STATUS_REG, &c);
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_INTR2_STATUS_REG, &c);

	rc = anx7150_i2c_read_p0_reg(client, ANX7150_INTR3_STATUS_REG, &c);
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_INTR3_STATUS_REG, &c);

	c = 0x00;
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_INTR_CTRL_REG, &c);

	// clear ANX7150_parse_edid_done & ANX7150_system_config_done
	ANX7150_parse_edid_done = 0;
//	ANX7150_system_config_done = 0;
	ANX7150_srm_checked = 0;

	return rc;
}

int anx7150_set_avmute(struct i2c_client *client)
{
	int rc = 0;
	char c;

	c = 0x01;
	if((rc = anx7150_i2c_write_p1_reg(client, ANX7150_GNRL_CTRL_PKT_REG, &c)) < 0)
		return rc;
	
	if((rc = anx7150_i2c_read_p1_reg(client, ANX7150_INFO_PKTCTRL1_REG, &c)) < 0)
		return rc;
	c |= (0x0c);
	if((rc = anx7150_i2c_write_p1_reg(client, ANX7150_INFO_PKTCTRL1_REG, &c)) < 0)
		return rc;
    ANX7150_avmute_enable = 1;

	return rc;
}
int anx7150_clear_avmute(struct i2c_client *client)
{
	int rc = 0;
    char c;

	c = 0x02;
	if((rc = anx7150_i2c_write_p1_reg(client, ANX7150_GNRL_CTRL_PKT_REG, &c)) < 0)
		return rc;
	
	if((rc = anx7150_i2c_read_p1_reg(client, ANX7150_INFO_PKTCTRL1_REG, &c)) < 0)
		return rc;
	c |= (0x0c);
	if((rc = anx7150_i2c_write_p1_reg(client, ANX7150_INFO_PKTCTRL1_REG, &c)) < 0)
		return rc;
    ANX7150_avmute_enable = 0;
//    D("@@@@@@@@@@@@@@@@@@@@ANX7150_Clear_AVMute\n");
	return rc;

}

static int anx7150_video_format_change(struct i2c_client *client)
{
	int rc;
    char c;
	
    hdmi_dbg(&client->dev, "after video format change int \n");
	
    rc = anx7150_set_avmute(client);//wen
    //stop HDCP and reset DDC
    rc = anx7150_i2c_read_p0_reg(client, ANX7150_HDCP_CTRL0_REG, &c);
	c &= (~ANX7150_HDCP_CTRL0_HW_AUTHEN);
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_HDCP_CTRL0_REG, &c);
	
    rc = anx7150_rst_ddcchannel(client);
	
    //when format change, clear this reg to avoid error in package config
    c = 0x00;
	rc = anx7150_i2c_write_p1_reg(client, ANX7150_INFO_PKTCTRL1_REG, &c);
	c = 0x00;
	rc = anx7150_i2c_write_p1_reg(client, ANX7150_INFO_PKTCTRL2_REG, &c);
    //xy 11.06 when format change, need system config again
	//    ANX7150_system_config_done = 0;
	return rc;
}
static int anx7150_blue_screen_disable(struct i2c_client *client)
{
	int rc = 0;
	char c;

	if((rc = anx7150_i2c_read_p0_reg(client, ANX7150_HDCP_CTRL1_REG, &c)) < 0)
		return rc;
	c &= (0xfb);
	if((rc = anx7150_i2c_write_p0_reg(client, ANX7150_HDCP_CTRL1_REG, &c)) < 0)
		return rc;

    ANX7150_send_blue_screen = 0;
	
	return rc;
}
static int anx7150_blue_screen_enable(struct i2c_client *client)
{
	int rc = 0;
	char c;
	
	if((rc = anx7150_i2c_read_p0_reg(client, ANX7150_HDCP_CTRL1_REG, &c)) < 0)
		return rc;
	c |= (ANX7150_HDCP_CTRL1_BLUE_SCREEN_EN);
	if((rc = anx7150_i2c_write_p0_reg(client, ANX7150_HDCP_CTRL1_REG, &c)) < 0)
		return rc;
    ANX7150_send_blue_screen = 1;

	return rc;
}
static int anx7150_hdcp_encryption_enable(struct i2c_client *client)
{
	int rc = 0;
	u8 c;
	
	if((rc = anx7150_i2c_read_p0_reg(client, ANX7150_HDCP_CTRL0_REG, &c)) < 0)
		return rc;
	c |= (ANX7150_HDCP_CTRL0_ENC_EN);
	if((rc = anx7150_i2c_write_p0_reg(client, ANX7150_HDCP_CTRL0_REG, &c)) < 0)
		return rc;
    ANX7150_hdcp_encryption = 1;

	return rc;
}

static int anx7150_hdcp_encryption_disable(struct i2c_client *client)
{
	int rc = 0;
	u8 c;
	
	if((rc = anx7150_i2c_read_p0_reg(client, ANX7150_HDCP_CTRL0_REG, &c)) < 0)
		return rc;
	c &= (0xfb);
	if((rc = anx7150_i2c_write_p0_reg(client, ANX7150_HDCP_CTRL0_REG, &c)) < 0)
		return rc;

    ANX7150_hdcp_encryption = 0;

	return rc;
}

static int anx7150_auth_done(struct i2c_client *client)
{
	int rc = 0;
    char c;

	hdmi_dbg(&client->dev, "anx7150 auth done\n");
	
	if((rc = anx7150_i2c_read_p0_reg(client, ANX7150_HDCP_STATUS_REG, &c)) < 0)
		return rc;
	
    if (c & ANX7150_HDCP_STATUS_AUTH_PASS)
    {
        hdmi_dbg(&client->dev, "ANX7150_Authentication pass in Auth_Done\n");
        anx7150_blue_screen_disable(client);
        ANX7150_hdcp_auth_pass = 1;
        ANX7150_hdcp_auth_fail_counter = 0;
    }
    else
    {
        hdmi_dbg(&client->dev, "ANX7150_Authentication failed\n");
        ANX7150_hdcp_wait_100ms_needed = 1;
        ANX7150_auth_fully_pass = 0;
        ANX7150_hdcp_auth_pass = 0;
        ANX7150_hdcp_auth_fail_counter ++;
        if (ANX7150_hdcp_auth_fail_counter >= ANX7150_HDCP_FAIL_THRESHOLD)
        {
            ANX7150_hdcp_auth_fail_counter = 0;
            //ANX7150_bksv_ready = 0;
            // TODO: Reset link;
            rc = anx7150_blue_screen_enable(client);
            rc = anx7150_hdcp_encryption_disable(client);
            //disable audio
            rc = anx7150_i2c_read_p0_reg(client, ANX7150_HDMI_AUDCTRL1_REG, &c);
			c &= (~ANX7150_HDMI_AUDCTRL1_IN_EN);
			rc = anx7150_i2c_write_p0_reg(client, ANX7150_HDMI_AUDCTRL1_REG, &c);
        }
    }
	return rc;
}

static int anx7150_clean_hdcp(struct i2c_client *client)
{
	int rc = 0;
	char c;
    //mute TMDS link
    //ANX7150_i2c_read_p0_reg(ANX7150_TMDS_CLKCH_CONFIG_REG, &c);//jack wen
    //ANX7150_i2c_write_p0_reg(ANX7150_TMDS_CLKCH_CONFIG_REG, c & (~ANX7150_TMDS_CLKCH_MUTE));

    //Disable hardware HDCP

	rc = anx7150_i2c_read_p0_reg(client, ANX7150_HDCP_CTRL0_REG, &c);
	c &= (~ANX7150_HDCP_CTRL0_HW_AUTHEN);
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_HDCP_CTRL0_REG, &c);
   
    //Reset HDCP logic
    rc = anx7150_i2c_read_p0_reg(client, ANX7150_SRST_REG, &c);
	c |= (ANX7150_SRST_HDCP_RST);
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_SRST_REG, &c);
	c &= (~ANX7150_SRST_HDCP_RST);
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_SRST_REG, &c);

    //Set ReAuth
     rc = anx7150_i2c_read_p0_reg(client, ANX7150_HDCP_CTRL0_REG, &c);
	c |= (ANX7150_HDCP_CTRL0_RE_AUTH);
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_HDCP_CTRL0_REG, &c);
	c &= (~ANX7150_HDCP_CTRL0_RE_AUTH);
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_HDCP_CTRL0_REG, &c);
    ANX7150_hdcp_auth_en = 0;
    //ANX7150_bksv_ready = 0;
    ANX7150_hdcp_auth_pass = 0;
    ANX7150_hdcp_auth_fail_counter =0 ;
    ANX7150_hdcp_encryption = 0;
    ANX7150_send_blue_screen = 0;
    ANX7150_hdcp_init_done = 0;
    ANX7150_hdcp_wait_100ms_needed = 1;
    ANX7150_auth_fully_pass = 0;
    ANX7150_srm_checked = 0;
    rc = anx7150_rst_ddcchannel(client);

	return rc;
}
static int anx7150_auth_change(struct i2c_client *client)
{
	int rc = 0;
    char c;
	
	int state = ANX7150_Get_System_State();
	
	rc = anx7150_i2c_read_p0_reg(client, ANX7150_HDCP_STATUS_REG, &c);
    if (c & ANX7150_HDCP_STATUS_AUTH_PASS)
    {
        ANX7150_hdcp_auth_pass = 1;
        hdmi_dbg(&client->dev, "ANX7150_Authentication pass in Auth_Change\n");
    }
    else
    {
        rc = anx7150_set_avmute(client); //wen
        hdmi_dbg(&client->dev, "ANX7150_Authentication failed_by_Auth_change\n");
        ANX7150_hdcp_auth_pass = 0;
        ANX7150_hdcp_wait_100ms_needed = 1;
        ANX7150_auth_fully_pass = 0;
        ANX7150_hdcp_init_done=0;   //wen HDCP CTS
        ANX7150_hdcp_auth_en=0;   //wen HDCP CTS
        rc = anx7150_hdcp_encryption_disable(client);
        if (state == PLAY_BACK)
        {
            ANX7150_auth_fully_pass = 0;
            //disable audio
            rc = anx7150_i2c_read_p0_reg(client, ANX7150_HDMI_AUDCTRL1_REG, &c);
			c &= (~ANX7150_HDMI_AUDCTRL1_IN_EN);
			rc = anx7150_i2c_write_p0_reg(client, ANX7150_HDMI_AUDCTRL1_REG, &c);
            rc = anx7150_clean_hdcp(client);							//wen updated for Changhong TV
        }
    }
	return rc;
}
int ANX7150_GET_RECIVER_TYPE(void)
{
	return ANX7150_edid_result.is_HDMI;
}
static int anx7150_audio_clk_change(struct i2c_client *client)
{
	int rc = 0;
	char c;

	hdmi_dbg(&client->dev, "ANX7150: audio clock changed interrupt,disable audio.\n");
    // disable audio

	rc = anx7150_i2c_read_p0_reg(client, ANX7150_HDMI_AUDCTRL1_REG, &c);
	c &= (~ANX7150_HDMI_AUDCTRL1_IN_EN);
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_HDMI_AUDCTRL1_REG, &c);

    //xy 11.06 when format change, need system config again
//    ANX7150_system_config_done = 0;
	return rc;
}

static int anx7150_afifo_overrun(struct i2c_client *client)
{
	int rc = 0;
	char c;
	hdmi_dbg(&client->dev, "ANX7150: AFIFO overrun interrupt,disable audio.\n");
    // disable audio

	rc = anx7150_i2c_read_p0_reg(client, ANX7150_HDMI_AUDCTRL1_REG, &c);
	c &= (~ANX7150_HDMI_AUDCTRL1_IN_EN);
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_HDMI_AUDCTRL1_REG, &c);

	return rc;
}
static int anx7150_spdif_error(struct i2c_client *client, int cur_state, int SPDIF_bi_phase_err, int SPDIF_error)
{
	int rc = 0;
	char c;
	int state = cur_state;

	if(SPDIF_bi_phase_err || SPDIF_error)
	{
		rc = anx7150_i2c_read_p0_reg(client, ANX7150_HDMI_AUDCTRL1_REG, &c);
		if( c & ANX7150_HDMI_AUDCTRL1_SPDIFIN_EN)	
		{
	
		    if ((state == CONFIG_AUDIO 
				|| state == CONFIG_PACKETS 
				|| state == HDCP_AUTHENTICATION 
				|| state == PLAY_BACK ))
		    {
				if(SPDIF_bi_phase_err){
			        hdmi_dbg(&client->dev, "SPDIF BI Phase or Unstable error.\n");
			        spdif_error_cnt += 0x03;
				}

				if(SPDIF_error){
					hdmi_dbg(&client->dev, "SPDIF Parity error.\n");
					spdif_error_cnt += 0x01;
				}

		    }

		    // adjust spdif phase
		    if (spdif_error_cnt >= spdif_error_th)
		    {
		        char freq_mclk,c1,c2;
		        spdif_error_cnt = 0x00;
		        hdmi_dbg(&client->dev, "adjust mclk phase!\n");
				
				rc = anx7150_i2c_read_p0_reg(client, ANX7150_HDMI_AUDCTRL0_REG, &c2);
				rc = anx7150_i2c_read_p0_reg(client, ANX7150_I2S_CTRL_REG, &c1);

		        freq_mclk = c2 & 0x07;
		        switch (freq_mclk)
		        {
		            case ANX7150_mclk_128_Fs:   //invert 0x50[3]
		                hdmi_dbg(&client->dev, "adjust mclk phase when 128*Fs!\n");
		                if ( c2 & 0x08 )    c2 &= 0xf7;
		                else   c2 |= 0x08;

						rc = anx7150_i2c_write_p0_reg(client, ANX7150_HDMI_AUDCTRL0_REG, &c2);
		                break;

		            case ANX7150_mclk_256_Fs:
		            case ANX7150_mclk_384_Fs:
		                hdmi_dbg(&client->dev, "adjust mclk phase when 256*Fs or 384*Fs!\n");
		                if ( c1 & 0x60 )   c1 &= 0x9f;
		                else     c1 |= 0x20;
						rc = anx7150_i2c_write_p0_reg(client, ANX7150_I2S_CTRL_REG, &c1);
		                break;

		            case ANX7150_mclk_512_Fs:
		                hdmi_dbg(&client->dev, "adjust mclk phase when 512*Fs!\n");
		                if ( c1 & 0x60 )   c1 &= 0x9f;
		                else    c1 |= 0x40;
		                rc = anx7150_i2c_write_p0_reg(client, ANX7150_I2S_CTRL_REG, &c1);
		                break;
		            default:
		                break;

		        }
		    }
		}
	}
	else{
		if(spdif_error_cnt > 0 && state == PLAY_BACK) spdif_error_cnt --;
		if(spdif_error_cnt > 0 && state  < CONFIG_AUDIO) spdif_error_cnt = 0x00;

	}

	return rc;
}
static int anx7150_plllock(struct i2c_client *client)
{
	int rc = 0;
	char c;
	
	rc = anx7150_i2c_read_p0_reg(client, ANX7150_CHIP_STATUS_REG, &c);
    if((c&0x01) == 0)
	{
        rc = anx7150_set_avmute(client);//wen
        hdmi_dbg(&client->dev, "ANX7150: PLL unlock interrupt,disable audio.\n");
        // disable audio & video
        rc = anx7150_i2c_read_p0_reg(client, ANX7150_HDMI_AUDCTRL1_REG, &c);
        c &= (~ANX7150_HDMI_AUDCTRL1_IN_EN);
		rc = anx7150_i2c_write_p0_reg(client, ANX7150_HDMI_AUDCTRL1_REG, &c);

		rc = anx7150_i2c_read_p0_reg(client, ANX7150_VID_CTRL_REG, &c);
        c &= (~ANX7150_VID_CTRL_IN_EN);
		rc = anx7150_i2c_write_p0_reg(client, ANX7150_VID_CTRL_REG, &c);

	    //when pll change, clear this reg to avoid error in package config
	    c = 0x00;
		rc = anx7150_i2c_write_p1_reg(client, ANX7150_INFO_PKTCTRL1_REG, &c);//wen
		c = 0x00;
		rc = anx7150_i2c_write_p1_reg(client, ANX7150_INFO_PKTCTRL2_REG, &c);

//	    ANX7150_system_config_done = 0;//jack wen
	}
	return rc;
}
static int anx7150_rx_sense_change(struct i2c_client *client, int cur_state)
{
	int rc = 0;
	char c;
	int state = cur_state;

	rc = anx7150_i2c_read_p0_reg(client, ANX7150_SYS_STATE_REG, &c);
    hdmi_dbg(&client->dev, "ANX7150_Rx_Sense_Interrupt, ANX7150_SYS_STATE_REG = %.2x\n", (unsigned int)c); //wen

    if ( c & ANX7150_SYS_STATE_RSV_DET)
    {
        //xy 11.06 Power on chip
        rc = anx7150_i2c_read_p0_reg(client, ANX7150_SYS_CTRL1_REG, &c);
		c |= (0x01);
		rc = anx7150_i2c_write_p0_reg(client, ANX7150_SYS_CTRL1_REG, &c);

        s_ANX7150_packet_config.packets_need_config = 0x03;   //new avi infoframe	wen
    }
    else
    {
        // Rx is not active
        if (state > WAIT_HOTPLUG)
        {
            //stop HDCP and reset DDC when lost Rx sense
            rc = anx7150_i2c_read_p0_reg(client, ANX7150_HDCP_CTRL0_REG, &c);
			c &= (~ANX7150_HDCP_CTRL0_REG);
			rc = anx7150_i2c_write_p0_reg(client, ANX7150_HDCP_CTRL0_REG, &c);
			
            rc = anx7150_rst_ddcchannel(client);

			rc = anx7150_i2c_read_p0_reg(client, ANX7150_SYS_CTRL1_REG, &c);
			c &= (0xfd);
			rc = anx7150_i2c_write_p0_reg(client, ANX7150_SYS_CTRL1_REG, &c);
			
            // mute TMDS link
            rc = anx7150_i2c_read_p0_reg(client, ANX7150_TMDS_CLKCH_CONFIG_REG, &c);
			c &= (~ANX7150_TMDS_CLKCH_MUTE);
			rc = anx7150_i2c_write_p0_reg(client, ANX7150_TMDS_CLKCH_CONFIG_REG, &c);
        }
        //Power down chip
        rc = anx7150_i2c_read_p0_reg(client, ANX7150_SYS_CTRL1_REG, &c);
		c &= (0xfe);
		rc = anx7150_i2c_write_p0_reg(client, ANX7150_SYS_CTRL1_REG, &c);
    }
    //xy 11.06 when format change, need system config again
//    ANX7150_system_config_done = 0;//wen HDCP CTS

	return rc;
}
int ANX7150_Interrupt_Process(struct anx7150_pdata *anx, int cur_state)
{
	struct anx7150_interrupt_s interrupt_staus;

	int state;
	int hot_plug;
	int rc;

	state = cur_state;

	hot_plug = anx7150_get_hpd(anx->client);

	rc = anx7150_get_interrupt_status(anx->client, &interrupt_staus);
	if(rc < 0){
		goto out;
	}	

	if(anx->dev.HPD_status != hot_plug){
		anx->dev.HPD_change_cnt++;
	}
	else{
		anx->dev.HPD_change_cnt = 0;
	}

	if(anx->dev.HPD_change_cnt > 1){
		hdmi_dbg(&anx->client->dev, "hotplug_change\n");

		if(hot_plug == HDMI_RECIVER_UNPLUG){
			anx7150_unplug(anx->client);
			state = HDMI_INITIAL;
			anx->dev.reciver_status = HDMI_RECIVER_INACTIVE;
		}

		anx->dev.HPD_change_cnt = 0;
		anx->dev.HPD_status = hot_plug;
	}
	return state;
	if(state != HDMI_INITIAL && state != WAIT_HOTPLUG){
		if(interrupt_staus.video_format_change){
			if(state > SYSTEM_CONFIG){
				rc = anx7150_video_format_change(anx->client);
				state = CONFIG_VIDEO;
			}
		}

		if(interrupt_staus.auth_done){
			rc = anx7150_auth_done(anx->client);
			state = CONFIG_AUDIO;
		}

		if(interrupt_staus.auth_state_change){
			rc = anx7150_auth_change(anx->client);
			if(state == PLAY_BACK){
				state = HDCP_AUTHENTICATION;
			}
		}

		if(ANX7150_GET_RECIVER_TYPE() == 1){
			/*
			if(interrupt_staus.audio_clk_change){
				if(state > CONFIG_VIDEO){
					rc = anx7150_audio_clk_change(anx->client);
					state = SYSTEM_CONFIG;
				}
			}
			
			if(interrupt_staus.audio_FIFO_overrun){
				if(state > CONFIG_VIDEO){
					rc = anx7150_afifo_overrun(anx->client);
					state = CONFIG_AUDIO;
				}
			}

*/
			rc = anx7150_spdif_error(anx->client, state, interrupt_staus.SPDIF_bi_phase_error, interrupt_staus.SPDIF_error);
		}

		if(interrupt_staus.pll_lock_change){
			if(state > SYSTEM_CONFIG){
				rc = anx7150_plllock(anx->client);
				state = SYSTEM_CONFIG;
			}
		}

		if(interrupt_staus.rx_sense_change){
			anx7150_rx_sense_change(anx->client, state);
			if(state > WAIT_RX_SENSE) 
				state = WAIT_RX_SENSE;
		}
	}

out:
	return state;
}

int ANX7150_API_Initial(struct i2c_client *client)
{
	int rc = 0;
	hdmi_dbg(&client->dev, "%s\n", __func__);

    ANX7150_Variable_Initial();
    ANX7150_HW_Interface_Variable_Initial();
    rc = anx7150_hardware_initial(client);

	return rc;
}

void ANX7150_Shutdown(struct i2c_client *client)
{
	hdmi_dbg(&client->dev, "%s\n", __func__);
	ANX7150_API_Initial(client);
	ANX7150_Set_System_State(client, HDMI_INITIAL);
}

static int anx7150_initddc_read(struct i2c_client *client, 
								u8 devaddr, u8 segmentpointer,
                          		u8 offset, u8  access_num_Low,u8 access_num_high)
{
	int rc = 0;
	char c;

    //Write slave device address
    c = devaddr;
    rc = anx7150_i2c_write_p0_reg(client, ANX7150_DDC_SLV_ADDR_REG, &c);
    // Write segment address
    c = segmentpointer;
    rc = anx7150_i2c_write_p0_reg(client, ANX7150_DDC_SLV_SEGADDR_REG, &c);
    //Write offset
    c = offset;
    rc = anx7150_i2c_write_p0_reg(client, ANX7150_DDC_SLV_OFFADDR_REG, &c);
    //Write number for access
    c = access_num_Low;
    rc = anx7150_i2c_write_p0_reg(client, ANX7150_DDC_ACCNUM0_REG, &c);
	c = access_num_high;
    rc = anx7150_i2c_write_p0_reg(client, ANX7150_DDC_ACCNUM1_REG, &c);
    //Clear FIFO
    c = 0x05;
    rc = anx7150_i2c_write_p0_reg(client, ANX7150_DDC_ACC_CMD_REG, &c);
    //EDDC sequential Read
    c = 0x04;
    rc = anx7150_i2c_write_p0_reg(client, ANX7150_DDC_ACC_CMD_REG, &c);

	return rc;
}
static int ANX7150_GetEDIDLength(struct i2c_client *client)
{
    u8 edid_data_length;
	int rc = 0;

    anx7150_rst_ddcchannel(client);

    rc = anx7150_initddc_read(client, 0xa0, 0x00, 0x7e, 0x01, 0x00);

	mdelay(10);
	rc = anx7150_i2c_read_p0_reg(client, ANX7150_DDC_FIFO_ACC_REG, &edid_data_length);

    ANX7150_edid_length = edid_data_length * 128 + 128;

	return rc;

}
/*** DDC fetch and block validation ***/

static const u8 edid_header[] = {
	0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00
};


/*
 * Sanity check the EDID block (base or extension).  Return 0 if the block
 * doesn't check out, or 1 if it's valid.
 */
 
int anx7150_edid_block_valid(struct i2c_client *client, u8 *raw_edid)
{
	int i;
	u8 csum = 0;
	struct edid *edid = (struct edid *)raw_edid;

	if (raw_edid[0] == 0x00) {
		int score = 0;

		for (i = 0; i < sizeof(edid_header); i++)
			if (raw_edid[i] == edid_header[i])
				score++;

		if (score == 8) ;
		else if (score >= 6) {
			hdmi_dbg(&client->dev, "Fixing EDID header, your hardware may be failing\n");
			memcpy(raw_edid, edid_header, sizeof(edid_header));
		} else {
			goto bad;
		}
	}

#if 0
	for (i = 0; i < EDID_LENGTH; i++)
		csum += raw_edid[i];
	if (csum) {
		hdmi_dbg(&client->dev, "EDID checksum is invalid, remainder is %d\n", csum);

		/* allow CEA to slide through, switches mangle this */
		if (raw_edid[0] != 0x02)
			goto bad;
	}
#endif

	/* per-block-type checks */
	switch (raw_edid[0]) {
	case 0: /* base */
		if (edid->version != 1) {
			dev_err(&client->dev, "EDID has major version %d, instead of 1\n", edid->version);
			goto bad;
		}

		if (edid->revision > 4)
			dev_err(&client->dev,"EDID minor > 4, assuming backward compatibility\n");
		break;

	default:
		break;
	}

	return 1;

bad:
	if (raw_edid) {
		dev_err(&client->dev, "Raw EDID:\n");
		print_hex_dump_bytes(KERN_ERR, DUMP_PREFIX_NONE, raw_edid, EDID_LENGTH);
		printk("\n");
	}
	return 0;
}
int ANX7150_DDC_EDID(struct i2c_client *client, u8 *buf, u8 block, u16 len)
{
	u8 offset;
	u8 segment;
	u8 len_low;
	u8 len_high;
	
	offset   = EDID_LENGTH * (block & 0x01);
	segment  = block >> 1;
	len_low  = len & 0xFF;
	len_high = (len >> 8) & 0xFF;

	anx7150_initddc_read(client, 0xa0, segment, offset, len_low, len_high);
	if(ANX7150_DDC_Mass_Read(client, buf, len) == len)
		return 0;
	else
		return -1;
}
u8 *ANX7150_Read_EDID(struct i2c_client *client)
{
	u8 *block = NULL;
	u8 *raw_edid = NULL;
	u8 extend_block_num;
	int i = 0;
	int j = 0;

	anx7150_rst_ddcchannel(client);

	if ((block = (u8 *)kmalloc(EDID_LENGTH, GFP_KERNEL)) == NULL)
		return NULL;

	/* base block fetch */
	hdmi_dbg(&client->dev, "Read base block\n");
	for (i = 0; i < 4; i++) {
		if(ANX7150_DDC_EDID(client, block, 0, EDID_LENGTH))
			goto out;
		if(anx7150_edid_block_valid(client, block))
			break;
		else
			dev_err(&client->dev, "Read base block err, retry...\n");
		
		mdelay(10);
	}

	if(i == 4){
		dev_err(&client->dev, "Read base block failed\n");
		goto out;
	}

	/* if there's no extensions, we're done */
	extend_block_num = block[0x7e];
	if(extend_block_num == 0)
		goto out;
	
	dev_err(&client->dev, "extend_block_num = %d\n", extend_block_num);

	raw_edid = krealloc(block, (extend_block_num + 1) * EDID_LENGTH, GFP_KERNEL);
	if(!raw_edid)
		goto out;

	block = raw_edid;

	hdmi_dbg(&client->dev, "Read extend block\n");
	for(j=1; j<=extend_block_num; j++){
		for(i=0; i<4; i++){
			if(ANX7150_DDC_EDID(client, raw_edid + j * EDID_LENGTH, j, EDID_LENGTH))
				goto out;
			if(anx7150_edid_block_valid(client, raw_edid + j * EDID_LENGTH))
				break;
			else
				dev_err(&client->dev, "Read extend block %d err, retry...\n", j);

			mdelay(10);
		}

		if(i == 4){
			dev_err(&client->dev, "Read extend block %d failed\n", j);
			goto out;
		}
	}

	dev_err(&client->dev, "\n\nRaw EDID(extend_block_num = %d, total_len = %d):\n\n", extend_block_num, EDID_LENGTH*(extend_block_num+1));
	print_hex_dump_bytes(KERN_ERR, DUMP_PREFIX_NONE, raw_edid, EDID_LENGTH*(extend_block_num+1));
	printk("\n\n");

	return raw_edid;

out:
	kfree(block);
	return NULL;
}
int ANX7150_DDC_Mass_Read(struct i2c_client *client, u8 *buf, u16 len)
{
	int rc = 0;
    u32 i, j;
    char c, c1,ddc_empty_cnt;

    i = len;
    while (i > 0)
    {
        //check DDC FIFO statue
        rc = anx7150_i2c_read_p0_reg(client, ANX7150_DDC_CHSTATUS_REG, &c);
        if (c & ANX7150_DDC_CHSTATUS_DDC_OCCUPY)
        {
            hdmi_dbg(&client->dev, "ANX7150 DDC channel is accessed by an external device, break!.\n");
            break;
        }
        if (c & ANX7150_DDC_CHSTATUS_FIFO_FULL)
            ANX7150_ddc_fifo_full = 1;
        else
            ANX7150_ddc_fifo_full = 0;
        if (c & ANX7150_DDC_CHSTATUS_INPRO)
            ANX7150_ddc_progress = 1;
        else
            ANX7150_ddc_progress = 0;
        if (ANX7150_ddc_fifo_full)
        {
            hdmi_dbg(&client->dev, "DDC FIFO is full during edid reading");
			rc = anx7150_i2c_read_p0_reg(client, ANX7150_DDC_FIFOCNT_REG, &c);
            hdmi_dbg(&client->dev, "FIFO counter is %.2x\n", (u32) c);
            for (j=0; j<c; j++)
            {
            	rc = anx7150_i2c_read_p0_reg(client, ANX7150_DDC_FIFO_ACC_REG, &c1);
							buf[len - i + j] = c1;

              ANX7150_ddc_fifo_full = 0;
				if(anx7150_mass_read_need_delay)
					mdelay(1);
            }
            i = i - c;
            //D("\n");
        }
        else if (!ANX7150_ddc_progress)
        {
            //D("ANX7150 DDC FIFO access finished.\n");
            rc = anx7150_i2c_read_p0_reg(client, ANX7150_DDC_FIFOCNT_REG, &c);
            //D("FIFO counter is %.2x\n", (u32) c);
            if (!c)
            {
                i =0;
                break;
            }
            for (j=0; j<c; j++)
            {
            	rc = anx7150_i2c_read_p0_reg(client, ANX7150_DDC_FIFO_ACC_REG, &c1);
                buf[len - i + j] = c1;
            }
            i = i - c;
            //D("\ni=%d\n", i);
        }
        else
        {
            ddc_empty_cnt = 0x00;
            for (c1=0; c1<0x0a; c1++)
            {
            	rc = anx7150_i2c_read_p0_reg(client, ANX7150_DDC_CHSTATUS_REG, &c);
                //D("DDC FIFO access is progressing.\n");
                //D("DDC Channel status is 0x%.2x\n",(u32)c);
                if (c & ANX7150_DDC_CHSTATUS_FIFO_EMPT)
                    ddc_empty_cnt++;
                mdelay(1);
                //D("ddc_empty_cnt =  0x%.2x\n",(u32)ddc_empty_cnt);
            }
            if (ddc_empty_cnt >= 0x0a)
                break;
        }
    }
	return (len - i);
}

static u8 ANX7150_Read_EDID_u8(u8 segmentpointer,u8 offset)
{
    /*u8 c;
    anx7150_initddc_read(0xa0, segmentpointer, offset, 0x01, 0x00);
     ANX7150_i2c_read_p0_reg(ANX7150_DDC_FIFOCNT_REG, &c);
     while(c==0)
    	ANX7150_i2c_read_p0_reg(ANX7150_DDC_FIFO_ACC_REG, &c);
    return c;*/

    return ANX7150_EDID_Buf[offset];
}
static u8 ANX7150_Parse_EDIDHeader(void)
{
    u8 i,temp;
    temp = 0;
    // the EDID header should begin with 0x00,0xff,0xff,0xff,0xff,0xff,0xff,0x00
    if ((ANX7150_Read_EDID_u8(0, 0) == 0x00) && (ANX7150_Read_EDID_u8(0, 7) == 0x00))
    {
        for (i = 1; i < 7; i++)
        {
            if (ANX7150_Read_EDID_u8(0, i) != 0xff)
            {
                temp = 0x01;
                break;
            }
        }
    }
    else
    {
        temp = 0x01;
    }
    if (temp == 0x01)
    {
        return 0;
    }
    else
    {
        return 1;
    }
}
static u8 ANX7150_Parse_EDIDVersion(void)
{

    if (!((ANX7150_Read_EDID_u8(0, 0x12) == 1) && (ANX7150_Read_EDID_u8(0, 0x13) >= 3) ))
    {
        return 0;
    }
    else
    {
        return 1;
    }
}
static void ANX7150_Parse_DTD(void)
{
    u32 temp;
    unsigned long temp1,temp2;
    u32 Hresolution,Vresolution,Hblanking,Vblanking;
    u32 PixelCLK,Vtotal,H_image_size,V_image_size;
    u8 Hz;
    //float Ratio;

    temp = ANX7150_edid_dtd[1];
    temp = temp << 8;
    PixelCLK = temp + ANX7150_edid_dtd[0];
    //	D("Pixel clock is 10000 * %u\n",  temp);

    temp = ANX7150_edid_dtd[4];
    temp = (temp << 4) & 0x0f00;
    Hresolution = temp + ANX7150_edid_dtd[2];
    //D("Horizontal Active is  %u\n",  Hresolution);

    temp = ANX7150_edid_dtd[4];
    temp = (temp << 8) & 0x0f00;
    Hblanking = temp + ANX7150_edid_dtd[3];
    //D("Horizontal Blanking is  %u\n",  temp);

    temp = ANX7150_edid_dtd[7];
    temp = (temp << 4) & 0x0f00;
    Vresolution = temp + ANX7150_edid_dtd[5];
    //D("Vertical Active is  %u\n",  Vresolution);

    temp = ANX7150_edid_dtd[7];
    temp = (temp << 8) & 0x0f00;
    Vblanking = temp + ANX7150_edid_dtd[6];
    //D("Vertical Blanking is  %u\n",  temp);

    temp = ANX7150_edid_dtd[11];
    temp = (temp << 2) & 0x0300;
    temp = temp + ANX7150_edid_dtd[8];
    //D("Horizontal Sync Offset is  %u\n",  temp);

    temp = ANX7150_edid_dtd[11];
    temp = (temp << 4) & 0x0300;
    temp = temp + ANX7150_edid_dtd[9];
    //D("Horizontal Sync Pulse is  %u\n",  temp);

    temp = ANX7150_edid_dtd[11];
    temp = (temp << 2) & 0x0030;
    temp = temp + (ANX7150_edid_dtd[10] >> 4);
    //D("Vertical Sync Offset is  %u\n",  temp);

    temp = ANX7150_edid_dtd[11];
    temp = (temp << 4) & 0x0030;
    temp = temp + (ANX7150_edid_dtd[8] & 0x0f);
    //D("Vertical Sync Pulse is  %u\n",  temp);

    temp = ANX7150_edid_dtd[14];
    temp = (temp << 4) & 0x0f00;
    H_image_size = temp + ANX7150_edid_dtd[12];
    //D("Horizontal Image size is  %u\n",  temp);

    temp = ANX7150_edid_dtd[14];
    temp = (temp << 8) & 0x0f00;
    V_image_size = temp + ANX7150_edid_dtd[13];
    //D("Vertical Image size is  %u\n",  temp);

    //D("Horizontal Border is  %bu\n",  ANX7150_edid_dtd[15]);

    //D("Vertical Border is  %bu\n",  ANX7150_edid_dtd[16]);

    temp1 = Hresolution + Hblanking;
    Vtotal = Vresolution + Vblanking;
    temp1 = temp1 * Vtotal;
    temp2 = PixelCLK;
    temp2 = temp2 * 10000;
    if (temp1 == 0)																												//update
        Hz=0;
    else
        Hz = temp2 / temp1;
    //Hz = temp2 / temp1;
    if ((Hz == 59) || (Hz == 60))
    {
        Hz = 60;
        //D("_______________Vertical Active is  %u\n",  Vresolution);
        if (Vresolution == 540)
            ANX7150_edid_result.supported_1080i_60Hz = 1;
        if (Vresolution == 1080)
            ANX7150_edid_result.supported_1080p_60Hz = 1;
        if (Vresolution == 720)
            ANX7150_edid_result.supported_720p_60Hz = 1;
        if ((Hresolution == 640) && (Vresolution == 480))
            ANX7150_edid_result.supported_640x480p_60Hz = 1;
        if ((Hresolution == 720) && (Vresolution == 480))
            ANX7150_edid_result.supported_720x480p_60Hz = 1;
        if ((Hresolution == 720) && (Vresolution == 240))
            ANX7150_edid_result.supported_720x480i_60Hz = 1;
    }
    if (Hz == 50)
    {
        //D("+++++++++++++++Vertical Active is  %u\n",  Vresolution);
        if (Vresolution == 540)
            ANX7150_edid_result.supported_1080i_50Hz = 1;
        if (Vresolution == 1080)
            ANX7150_edid_result.supported_1080p_50Hz = 1;
        if (Vresolution == 720)
            ANX7150_edid_result.supported_720p_50Hz = 1;
        if (Vresolution == 576)
            ANX7150_edid_result.supported_576p_50Hz = 1;
        if (Vresolution == 288)
            ANX7150_edid_result.supported_576i_50Hz = 1;
    }
    //D("Fresh rate :% bu Hz\n", Hz);
    //Ratio = H_image_size;
    //Ratio = Ratio / V_image_size;
    //D("Picture ratio : %f \n", Ratio);
}
static void ANX7150_Parse_DTDinBlockONE(void)
{
    u8 i;
    for (i = 0; i < 18; i++)
    {
        ANX7150_edid_dtd[i] = ANX7150_Read_EDID_u8(0, (i + 0x36));
    }
    //D("Parse the first DTD in Block one:\n");
    ANX7150_Parse_DTD();

    if ((ANX7150_Read_EDID_u8(0, 0x48) == 0)
            && (ANX7150_Read_EDID_u8(0, 0x49) == 0)
            && (ANX7150_Read_EDID_u8(0, 0x4a) == 0))
    {
        ;//D("the second DTD in Block one is not used to descript video timing.\n");
    }
    else
    {
        for (i = 0; i < 18; i++)
        {
            ANX7150_edid_dtd[i] = ANX7150_Read_EDID_u8(0, (i + 0x48));
        }
        ANX7150_Parse_DTD();
    }

    if ((ANX7150_Read_EDID_u8(0,0x5a) == 0)
            && (ANX7150_Read_EDID_u8(0,0x5b) == 0)
            && (ANX7150_Read_EDID_u8(0,0x5c) == 0))
    {
        ;//D("the third DTD in Block one is not used to descript video timing.\n");
    }
    else
    {
        for (i = 0; i < 18; i++)
        {
            ANX7150_edid_dtd[i] = ANX7150_Read_EDID_u8(0, (i + 0x5a));
        }
        ANX7150_Parse_DTD();
    }

    if ((ANX7150_Read_EDID_u8(0,0x6c) == 0)
            && (ANX7150_Read_EDID_u8(0,0x6d) == 0)
            && (ANX7150_Read_EDID_u8(0,0x6e) == 0))
    {
        ;//D("the fourth DTD in Block one is not used to descript video timing.\n");
    }
    else
    {
        for (i = 0; i < 18; i++)
        {
            ANX7150_edid_dtd[i] = ANX7150_Read_EDID_u8(0,(i + 0x6c));
        }
        ANX7150_Parse_DTD();
    }
}
static void ANX7150_Parse_NativeFormat(void)
{
    u8 temp;
    temp = ANX7150_Read_EDID_u8(0,0x83) & 0xf0;
    /*if(temp & 0x80)
     	;//D("DTV supports underscan.\n");
     if(temp & 0x40)
     	;//D("DTV supports BasicAudio.\n");*/
    if (temp & 0x20)
    {
        //D("DTV supports YCbCr 4:4:4.\n");
        ANX7150_edid_result.ycbcr444_supported= 1;
    }
    if (temp & 0x10)
    {
        //D("DTV supports YCbCr 4:2:2.\n");
        ANX7150_edid_result.ycbcr422_supported= 1;
    }
}
static void ANX7150_Parse_DTDinExtBlock(void)
{
    u8 i,DTDbeginAddr;
    DTDbeginAddr = ANX7150_Read_EDID_u8(ANX7150_ext_block_num/2, 0x82)
                   + 0x80;
    while (DTDbeginAddr < (0x6c + 0x80))
    {
        if ((ANX7150_Read_EDID_u8(ANX7150_ext_block_num/2,DTDbeginAddr) == 0)
                && (ANX7150_Read_EDID_u8(ANX7150_ext_block_num/2,(DTDbeginAddr + 1)) == 0)
                && (ANX7150_Read_EDID_u8(ANX7150_ext_block_num/2,(DTDbeginAddr + 2)) == 0))
        {
            ;//D("this DTD in Extension Block is not used to descript video timing.\n");
        }
        else
        {
            for (i = 0; i < 18; i++)
            {
                ANX7150_edid_dtd[i] = ANX7150_Read_EDID_u8(ANX7150_ext_block_num/2,(i + DTDbeginAddr));
            }
            //D("Parse the DTD in Extension Block :\n");
            ANX7150_Parse_DTD();
        }
        DTDbeginAddr = DTDbeginAddr + 18;
    }
}
static void ANX7150_Parse_AudioSTD(void)
{
    u8 i,AudioFormat,STDReg_tmp,STDAddr_tmp;
    STDReg_tmp = ANX7150_stdreg & 0x1f;
    STDAddr_tmp = ANX7150_stdaddr + 1;
    i = 0;
    while (i < STDReg_tmp)
    {
        AudioFormat = (ANX7150_Read_EDID_u8(ANX7150_ext_block_num/2,STDAddr_tmp ) & 0xF8) >> 3;
        ANX7150_edid_result.AudioChannel[i/3] = (ANX7150_Read_EDID_u8(ANX7150_ext_block_num/2,STDAddr_tmp) & 0x07) + 1;
        ANX7150_edid_result.AudioFormat[i/3] = AudioFormat;
        ANX7150_edid_result.AudioFs[i/3] = ANX7150_Read_EDID_u8(ANX7150_ext_block_num/2,(STDAddr_tmp + 1)) & 0x7f;

        if (AudioFormat == 1)
            ANX7150_edid_result.AudioLength[i/3] = ANX7150_Read_EDID_u8(ANX7150_ext_block_num/2,(STDAddr_tmp + 2)) & 0x07;
        else
            ANX7150_edid_result.AudioLength[i/3] = ANX7150_Read_EDID_u8(ANX7150_ext_block_num/2,(STDAddr_tmp + 2)) << 3;

        i = i + 3;
        STDAddr_tmp = STDAddr_tmp + 3;
    }
}
static void ANX7150_Parse_VideoSTD(void)
{
    u8 i,STDReg_tmp,STDAddr_tmp;
    u8 SVD_ID[34];
    STDReg_tmp = ANX7150_stdreg & 0x1f;
    STDAddr_tmp = ANX7150_stdaddr + 1;
    i = 0;
    while (i < STDReg_tmp)
    {
        SVD_ID[i] = ANX7150_Read_EDID_u8(ANX7150_ext_block_num/2,STDAddr_tmp) & 0x7F;
        //D("ANX7150_edid_result.SVD_ID[%.2x]=0x%.2x\n",(u32)i,(u32)ANX7150_edid_result.SVD_ID[i]);
        //if(ANX7150_Read_EDID_u8(ANX7150_ext_block_num/2,STDAddr_tmp) & 0x80)
        //    D(" Native mode");
        if (SVD_ID[i] == 1)
            ANX7150_edid_result.supported_640x480p_60Hz = 1;
        else if (SVD_ID[i] == 4)
            ANX7150_edid_result.supported_720p_60Hz = 1;
        else if (SVD_ID[i] == 19)
            ANX7150_edid_result.supported_720p_50Hz = 1;
        else if (SVD_ID[i] == 16)
            ANX7150_edid_result.supported_1080p_60Hz = 1;
        else if (SVD_ID[i] == 31)
            ANX7150_edid_result.supported_1080p_50Hz = 1;
        else if (SVD_ID[i] == 5)
            ANX7150_edid_result.supported_1080i_60Hz = 1;
        else if (SVD_ID[i] == 20)
            ANX7150_edid_result.supported_1080i_50Hz = 1;
        else if ((SVD_ID[i] == 2) ||(SVD_ID[i] == 3))
            ANX7150_edid_result.supported_720x480p_60Hz = 1;
        else if ((SVD_ID[i] == 6) ||(SVD_ID[i] == 7))
            ANX7150_edid_result.supported_720x480i_60Hz = 1;
        else if ((SVD_ID[i] == 17) ||(SVD_ID[i] == 18))
            ANX7150_edid_result.supported_576p_50Hz = 1;
        else if ((SVD_ID[i] == 21) ||(SVD_ID[i] == 22))
            ANX7150_edid_result.supported_576i_50Hz = 1;

        i = i + 1;
        STDAddr_tmp = STDAddr_tmp + 1;
    }
}
static void ANX7150_Parse_SpeakerSTD(void)
{
    ANX7150_edid_result.SpeakerFormat = ANX7150_Read_EDID_u8(ANX7150_ext_block_num/2,(ANX7150_stdaddr + 1)) ;
}
static void ANX7150_Parse_VendorSTD(void)
{
    //u8 c;
    if ((ANX7150_Read_EDID_u8(ANX7150_ext_block_num/2,(ANX7150_stdaddr + 1)) == 0x03)
            && (ANX7150_Read_EDID_u8(ANX7150_ext_block_num/2,(ANX7150_stdaddr + 2)) == 0x0c)
            && (ANX7150_Read_EDID_u8(ANX7150_ext_block_num/2,(ANX7150_stdaddr + 3)) == 0x00))
    {
        ANX7150_edid_result.is_HDMI = 1;
        //ANX7150_i2c_read_p0_reg(ANX7150_SYS_CTRL1_REG, &c);
        //ANX7150_i2c_write_p0_reg(ANX7150_SYS_CTRL1_REG, c |ANX7150_SYS_CTRL1_HDMI);
    }
    else
    {
        ANX7150_edid_result.is_HDMI = 0;
        //ANX7150_i2c_read_p0_reg(ANX7150_SYS_CTRL1_REG, &c);
        //ANX7150_i2c_write_p0_reg(ANX7150_SYS_CTRL1_REG, c & (~ANX7150_SYS_CTRL1_HDMI));
    }
}

static void ANX7150_Parse_STD(void)
{
    u8 DTDbeginAddr;
    ANX7150_stdaddr = 0x84;
    DTDbeginAddr = ANX7150_Read_EDID_u8(ANX7150_ext_block_num/2,0x82) + 0x80;
    // D("Video DTDbeginAddr Register :%.2x\n", (u32) DTDbeginAddr);
    while (ANX7150_stdaddr < DTDbeginAddr)
    {
        ANX7150_stdreg = ANX7150_Read_EDID_u8(ANX7150_ext_block_num/2,ANX7150_stdaddr);
        switch (ANX7150_stdreg & 0xe0)
        {
            case 0x20:
                ANX7150_Parse_AudioSTD();
                ANX7150_sau_length = ANX7150_stdreg & 0x1f;
                break;
            case 0x40:
                ANX7150_Parse_VideoSTD();
                ANX7150_svd_length = ANX7150_stdreg & 0x1f;
                break;
            case 0x80:
                ANX7150_Parse_SpeakerSTD();
                break;
            case 0x60:
                ANX7150_Parse_VendorSTD();
                break;
            default:
                break;
        }
        ANX7150_stdaddr = ANX7150_stdaddr + (ANX7150_stdreg & 0x1f) + 0x01;
    }
}
static u8 ANX7150_EDID_Checksum(u8 block_number)
{
    u8 i, real_checksum;
    u8 edid_block_checksum;

    edid_block_checksum = 0;
    for (i = 0; i < 127; i ++)
    {
        if ((block_number / 2) * 2 == block_number)
            edid_block_checksum = edid_block_checksum + ANX7150_Read_EDID_u8(block_number/2, i);
        else
            edid_block_checksum = edid_block_checksum + ANX7150_Read_EDID_u8(block_number/2, i + 0x80);
    }
    edid_block_checksum = (~edid_block_checksum) + 1;
    // D("edid_block_checksum = 0x%.2x\n",(u32)edid_block_checksum);
    if ((block_number / 2) * 2 == block_number)
        real_checksum = ANX7150_Read_EDID_u8(block_number/2, 0x7f);
    else
        real_checksum = ANX7150_Read_EDID_u8(block_number/2, 0xff);
    if (real_checksum == edid_block_checksum)
        return 1;
    else
        return 0;
}
static u8 ANX7150_Parse_ExtBlock(void)
{
    u8 i,c;

    for (i = 0; i < ANX7150_Read_EDID_u8(0, 0x7e); i++)   //read in blocks
    {
        c = ANX7150_Read_EDID_u8(i/2, 0x80);
        if ( c == 0x02)
        {
            ANX7150_ext_block_num = i + 1;
            ANX7150_Parse_DTDinExtBlock();
            ANX7150_Parse_STD();
            if (!(ANX7150_EDID_Checksum(ANX7150_ext_block_num)))
            {
                ANX7150_edid_result.edid_errcode = ANX7150_EDID_CheckSum_ERR;
                return ANX7150_edid_result.edid_errcode;
            }
        }
        else
        {
            ANX7150_edid_result.edid_errcode = ANX7150_EDID_ExtBlock_NotFor_861B;
            return ANX7150_edid_result.edid_errcode;
        }
    }

	return 0;
}
int ANX7150_Parse_EDID(struct i2c_client *client, struct anx7150_dev_s *dev)
{
	int rc = 0, i;
	char c;

	if(dev->rk29_output_status == RK29_OUTPUT_STATUS_LCD)
		anx7150_mass_read_need_delay = 1;
	else
		anx7150_mass_read_need_delay = 0;

	/* Clear HDCP Authentication indicator */
	rc = anx7150_i2c_read_p0_reg(client, ANX7150_HDCP_CTRL0_REG, &c);
	c &= (~ANX7150_HDCP_CTRL0_HW_AUTHEN);
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_HDCP_CTRL0_REG, &c);
	ANX7150_hdcp_auth_en = 0;


    ANX7150_EDID_Buf = ANX7150_Read_EDID(client);
    
		if(!ANX7150_EDID_Buf){
			ANX7150_edid_result.edid_errcode = ANX7150_EDID_BadHeader;
			dev_err(&client->dev, "READ EDID ERROR\n");
			goto err;
		}

/*
    if(ANX7150_EDID_Checksum(0) == 0)
    {
        D("EDID Block one check sum error, Stop parsing\n");
        ANX7150_edid_result.edid_errcode = ANX7150_EDID_CheckSum_ERR;
        return ANX7150_edid_result.edid_errcode;
    }
*/

    //ANX7150_Parse_BasicDis();
    ANX7150_Parse_DTDinBlockONE();

        if(ANX7150_EDID_Buf[0x7e] == 0)
        {
            hdmi_dbg(&client->dev, "No EDID extension blocks.\n");
            ANX7150_edid_result.edid_errcode = ANX7150_EDID_No_ExtBlock;
            return ANX7150_edid_result.edid_errcode;
        }
        
    ANX7150_Parse_NativeFormat();
    ANX7150_Parse_ExtBlock();

    if (ANX7150_edid_result.edid_errcode == ANX7150_EDID_ExtBlock_NotFor_861B){
		dev_err(&client->dev,"EDID ExtBlock not support for 861B, Stop parsing\n");
        goto err;
    }

    if (ANX7150_edid_result.edid_errcode == ANX7150_EDID_CheckSum_ERR){
		dev_err(&client->dev,"EDID Block check sum error, Stop parsing\n");
        goto err;
    }

    hdmi_dbg(&client->dev,"EDID parsing finished!\n");

    {
        hdmi_dbg(&client->dev,"ANX7150_edid_result.edid_errcode = 0x%.2x\n",(u32)ANX7150_edid_result.edid_errcode);
        hdmi_dbg(&client->dev,"ANX7150_edid_result.is_HDMI = 0x%.2x\n",(u32)ANX7150_edid_result.is_HDMI);
        hdmi_dbg(&client->dev,"ANX7150_edid_result.ycbcr422_supported = 0x%.2x\n",(u32)ANX7150_edid_result.ycbcr422_supported);
        hdmi_dbg(&client->dev,"ANX7150_edid_result.ycbcr444_supported = 0x%.2x\n",(u32)ANX7150_edid_result.ycbcr444_supported);
        hdmi_dbg(&client->dev,"ANX7150_edid_result.supported_1080i_60Hz = 0x%.2x\n",(u32)ANX7150_edid_result.supported_1080i_60Hz);
        hdmi_dbg(&client->dev,"ANX7150_edid_result.supported_1080i_50Hz = 0x%.2x\n",(u32)ANX7150_edid_result.supported_1080i_50Hz);
		hdmi_dbg(&client->dev,"ANX7150_edid_result.supported_1080p_60Hz = 0x%.2x\n",(u32)ANX7150_edid_result.supported_1080p_60Hz);
		hdmi_dbg(&client->dev,"ANX7150_edid_result.supported_1080p_50Hz = 0x%.2x\n",(u32)ANX7150_edid_result.supported_1080p_50Hz);
		hdmi_dbg(&client->dev,"ANX7150_edid_result.supported_720p_60Hz = 0x%.2x\n",(u32)ANX7150_edid_result.supported_720p_60Hz);
        hdmi_dbg(&client->dev,"ANX7150_edid_result.supported_720p_50Hz = 0x%.2x\n",(u32)ANX7150_edid_result.supported_720p_50Hz);
        hdmi_dbg(&client->dev,"ANX7150_edid_result.supported_640x480p_60Hz = 0x%.2x\n",(u32)ANX7150_edid_result.supported_640x480p_60Hz);
        hdmi_dbg(&client->dev,"ANX7150_edid_result.supported_720x480p_60Hz = 0x%.2x\n",(u32)ANX7150_edid_result.supported_720x480p_60Hz);
        hdmi_dbg(&client->dev,"ANX7150_edid_result.supported_720x480i_60Hz = 0x%.2x\n",(u32)ANX7150_edid_result.supported_720x480i_60Hz);
        hdmi_dbg(&client->dev,"ANX7150_edid_result.supported_576p_50Hz = 0x%.2x\n",(u32)ANX7150_edid_result.supported_576p_50Hz);
        hdmi_dbg(&client->dev,"ANX7150_edid_result.supported_576i_50Hz = 0x%.2x\n",(u32)ANX7150_edid_result.supported_576i_50Hz);
        if (!ANX7150_edid_result.edid_errcode)
        {
            for (i = 0; i < ANX7150_sau_length/3; i++)
            {
                hdmi_dbg(&client->dev,"ANX7150_edid_result.AudioChannel = 0x%.2x\n",(u32)ANX7150_edid_result.AudioChannel[i]);
                hdmi_dbg(&client->dev,"ANX7150_edid_result.AudioFormat = 0x%.2x\n",(u32)ANX7150_edid_result.AudioFormat[i]);
                hdmi_dbg(&client->dev,"ANX7150_edid_result.AudioFs = 0x%.2x\n",(u32)ANX7150_edid_result.AudioFs[i]);
                hdmi_dbg(&client->dev,"ANX7150_edid_result.AudioLength = 0x%.2x\n",(u32)ANX7150_edid_result.AudioLength[i]);
            }
            hdmi_dbg(&client->dev,"ANX7150_edid_result.SpeakerFormat = 0x%.2x\n",(u32)ANX7150_edid_result.SpeakerFormat);
        }
    }
	
	ANX7150_parse_edid_done = 1;
	kfree(ANX7150_EDID_Buf);
	ANX7150_EDID_Buf = NULL;
	return 0;
	
err:
		if(ANX7150_EDID_Buf){
		kfree(ANX7150_EDID_Buf);
		ANX7150_EDID_Buf = NULL;
	}
	return ANX7150_edid_result.edid_errcode;
}
int ANX7150_GET_SENSE_STATE(struct i2c_client *client)
{
	int rc = 0;
	char c;

	hdmi_dbg(&client->dev, "enter\n");
	rc = anx7150_i2c_read_p0_reg(client, ANX7150_SYS_STATE_REG, &c);

	return (c & ANX7150_SYS_STATE_RSV_DET) ? 1 : 0;
}
int ANX7150_Get_Optimal_resolution(int resolution_set)
{
	int resolution_real;
	int find_resolution = 0;

	switch(resolution_set){
	case HDMI_1280x720p_50Hz:
		if(ANX7150_edid_result.supported_720p_50Hz){
			resolution_real = HDMI_1280x720p_50Hz;
			find_resolution = 1;
		}
		break;
	case HDMI_1280x720p_60Hz:
		if(ANX7150_edid_result.supported_720p_60Hz){
			resolution_real = HDMI_1280x720p_60Hz;
			find_resolution = 1;
		}
		break;
	case HDMI_720x576p_50Hz_4x3:
		if(ANX7150_edid_result.supported_576p_50Hz){
			resolution_real = HDMI_720x576p_50Hz_4x3;
			find_resolution = 1;
		}
		break;
	case HDMI_720x576p_50Hz_16x9:
		if(ANX7150_edid_result.supported_576p_50Hz){
			resolution_real = HDMI_720x576p_50Hz_16x9;
			find_resolution = 1;
		}
		break;
	case HDMI_720x480p_60Hz_4x3:
		if(ANX7150_edid_result.supported_720x480p_60Hz){
			resolution_real = HDMI_720x480p_60Hz_4x3;
			find_resolution = 1;
		}
		break;
	case HDMI_720x480p_60Hz_16x9:
		if(ANX7150_edid_result.supported_720x480p_60Hz){
			resolution_real = HDMI_720x480p_60Hz_16x9;
			find_resolution = 1;
		}
		break;
	case HDMI_1920x1080p_50Hz:
		if(ANX7150_edid_result.supported_1080p_50Hz){
			resolution_real = HDMI_1920x1080p_50Hz;
			find_resolution = 1;
		}
		break;
	case HDMI_1920x1080p_60Hz:
		if(ANX7150_edid_result.supported_1080p_60Hz){
			resolution_real = HDMI_1920x1080p_60Hz;
			find_resolution = 1;
		}
		break;
	default:
		break;
	}

	if(find_resolution == 0){

		if(ANX7150_edid_result.supported_720p_50Hz)
			resolution_real = HDMI_1280x720p_50Hz;
		else if(ANX7150_edid_result.supported_720p_60Hz)
			resolution_real = HDMI_1280x720p_60Hz;
		else if(ANX7150_edid_result.supported_576p_50Hz)
			resolution_real = HDMI_720x576p_50Hz_4x3;
		else if(ANX7150_edid_result.supported_720x480p_60Hz)
			resolution_real = HDMI_720x480p_60Hz_4x3;
		else if(ANX7150_edid_result.supported_1080p_50Hz)
			resolution_real = HDMI_1920x1080p_50Hz;
		else if(ANX7150_edid_result.supported_1080p_60Hz)
			resolution_real = HDMI_1920x1080p_60Hz;
		else
			resolution_real = HDMI_1280x720p_50Hz;
	}

	return resolution_real;
}
void ANX7150_API_HDCP_ONorOFF(u8 HDCP_ONorOFF)
{	
    ANX7150_HDCP_enable = HDCP_ONorOFF;// 1: on;  0:off
}
static void ANX7150_API_Video_Config(u8 video_id,u8 input_pixel_rpt_time)
{
    ANX7150_video_timing_id = video_id;
    ANX7150_in_pix_rpt = input_pixel_rpt_time;
}
static void ANX7150_API_Packets_Config(u8 pkt_sel)
{
    s_ANX7150_packet_config.packets_need_config = pkt_sel;
}
static void ANX7150_API_AVI_Config(u8 pb1,u8 pb2,u8 pb3,u8 pb4,u8 pb5,
                            u8 pb6,u8 pb7,u8 pb8,u8 pb9,u8 pb10,u8 pb11,u8 pb12,u8 pb13)
{
    s_ANX7150_packet_config.avi_info.pb_u8[1] = pb1;
    s_ANX7150_packet_config.avi_info.pb_u8[2] = pb2;
    s_ANX7150_packet_config.avi_info.pb_u8[3] = pb3;
    s_ANX7150_packet_config.avi_info.pb_u8[4] = pb4;
    s_ANX7150_packet_config.avi_info.pb_u8[5] = pb5;
    s_ANX7150_packet_config.avi_info.pb_u8[6] = pb6;
    s_ANX7150_packet_config.avi_info.pb_u8[7] = pb7;
    s_ANX7150_packet_config.avi_info.pb_u8[8] = pb8;
    s_ANX7150_packet_config.avi_info.pb_u8[9] = pb9;
    s_ANX7150_packet_config.avi_info.pb_u8[10] = pb10;
    s_ANX7150_packet_config.avi_info.pb_u8[11] = pb11;
    s_ANX7150_packet_config.avi_info.pb_u8[12] = pb12;
    s_ANX7150_packet_config.avi_info.pb_u8[13] = pb13;
}
static void ANX7150_API_AUD_INFO_Config(u8 pb1,u8 pb2,u8 pb3,u8 pb4,u8 pb5,
                                 u8 pb6,u8 pb7,u8 pb8,u8 pb9,u8 pb10)
{
    s_ANX7150_packet_config.audio_info.pb_u8[1] = pb1;
    s_ANX7150_packet_config.audio_info.pb_u8[2] = pb2;
    s_ANX7150_packet_config.audio_info.pb_u8[3] = pb3;
    s_ANX7150_packet_config.audio_info.pb_u8[4] = pb4;
    s_ANX7150_packet_config.audio_info.pb_u8[5] = pb5;
    s_ANX7150_packet_config.audio_info.pb_u8[6] = pb6;
    s_ANX7150_packet_config.audio_info.pb_u8[7] = pb7;
    s_ANX7150_packet_config.audio_info.pb_u8[8] = pb8;
    s_ANX7150_packet_config.audio_info.pb_u8[9] = pb9;
    s_ANX7150_packet_config.audio_info.pb_u8[10] = pb10;
}
static void ANX7150_API_AUD_CHStatus_Config(u8 MODE,u8 PCM_MODE,u8 SW_CPRGT,u8 NON_PCM,
                                     u8 PROF_APP,u8 CAT_CODE,u8 CH_NUM,u8 SOURCE_NUM,u8 CLK_ACCUR,u8 Fs)
{
    //MODE: 0x00 = PCM Audio
    //PCM_MODE: 0x00 = 2 audio channels without pre-emphasis;
    //0x01 = 2 audio channels with 50/15 usec pre-emphasis;
    //SW_CPRGT: 0x00 = copyright is asserted;
    // 0x01 = copyright is not asserted;
    //NON_PCM: 0x00 = Represents linear PCM
    //0x01 = For other purposes
    //PROF_APP: 0x00 = consumer applications;
    // 0x01 = professional applications;

    //CAT_CODE: Category code
    //CH_NUM: 0x00 = Do not take into account
    // 0x01 = left channel for stereo channel format
    // 0x02 = right channel for stereo channel format
    //SOURCE_NUM: source number
    // 0x00 = Do not take into account
    // 0x01 = 1; 0x02 = 2; 0x03 = 3
    //CLK_ACCUR: 0x00 = level II
    // 0x01 = level I
    // 0x02 = level III
    // else reserved;

    s_ANX7150_audio_config.i2s_config.Channel_status1 = (MODE << 7) | (PCM_MODE << 5) |
            (SW_CPRGT << 2) | (NON_PCM << 1) | PROF_APP;
    s_ANX7150_audio_config.i2s_config.Channel_status2 = CAT_CODE;
    s_ANX7150_audio_config.i2s_config.Channel_status3 = (CH_NUM << 7) | SOURCE_NUM;
    s_ANX7150_audio_config.i2s_config.Channel_status4 = (CLK_ACCUR << 5) | Fs;
}
void ANX7150_API_System_Config(void)
{
    ANX7150_API_Video_Config(g_video_format,input_pixel_clk_1x_repeatition);
    ANX7150_API_Packets_Config(ANX7150_avi_sel | ANX7150_audio_sel);
    if (s_ANX7150_packet_config.packets_need_config & ANX7150_avi_sel)
        ANX7150_API_AVI_Config(	0x00,source_ratio,null,null,null,null,null,null,null,null,null,null,null);
    if (s_ANX7150_packet_config.packets_need_config & ANX7150_audio_sel)
        ANX7150_API_AUD_INFO_Config(null,null,null,null,null,null,null,null,null,null);
    ANX7150_API_AUD_CHStatus_Config(null,null,null,null,null,null,null,null,null,g_audio_format);

//	ANX7150_system_config_done = 1;
}

static int anx7150_blue_screen_format_config(struct i2c_client *client)
{
 	int rc = 0 ;
	char c;
	
    // TODO:Add ITU 601 format.(Now only ITU 709 format added)
    switch (ANX7150_RGBorYCbCr)
    {
        case ANX7150_RGB: //select RGB mode
        	c = 0x10;
        	rc = anx7150_i2c_write_p0_reg(client, ANX7150_HDCP_BLUESCREEN0_REG, &c);
			c = 0xeb;
        	rc = anx7150_i2c_write_p0_reg(client, ANX7150_HDCP_BLUESCREEN1_REG, &c);
			c = 0x10;
        	rc = anx7150_i2c_write_p0_reg(client, ANX7150_HDCP_BLUESCREEN2_REG, &c);
            break;
        case ANX7150_YCbCr422: //select YCbCr4:2:2 mode
        	c = 0x00;
        	rc = anx7150_i2c_write_p0_reg(client, ANX7150_HDCP_BLUESCREEN0_REG, &c);
			c = 0xad;
        	rc = anx7150_i2c_write_p0_reg(client, ANX7150_HDCP_BLUESCREEN1_REG, &c);
			c = 0x2a;
        	rc = anx7150_i2c_write_p0_reg(client, ANX7150_HDCP_BLUESCREEN2_REG, &c);
            break;
        case ANX7150_YCbCr444: //select YCbCr4:4:4 mode
        	c = 0x1a;
        	rc = anx7150_i2c_write_p0_reg(client, ANX7150_HDCP_BLUESCREEN0_REG, &c);
			c = 0xad;
        	rc = anx7150_i2c_write_p0_reg(client, ANX7150_HDCP_BLUESCREEN1_REG, &c);
			c = 0x2a;
        	rc = anx7150_i2c_write_p0_reg(client, ANX7150_HDCP_BLUESCREEN2_REG, &c);
            break;
        default:
            break;
    }
	return rc;
}
static void ANX7150_Get_Video_Timing(void)
{
    u8 i;
	
//#ifdef ITU656
    for (i = 0; i < 18; i++)
    {
        switch (ANX7150_video_timing_id)
        {
            case ANX7150_V640x480p_60Hz:
                //D("640x480p_60Hz!\n");
                ANX7150_video_timing_parameter[i] = ANX7150_video_timingtype_table.ANX7150_640x480p_60Hz[i];
                break;
            case ANX7150_V720x480p_60Hz_4x3:
            case ANX7150_V720x480p_60Hz_16x9:
                //D("720x480p_60Hz!\n");
                ANX7150_video_timing_parameter[i] = ANX7150_video_timingtype_table.ANX7150_720x480p_60Hz[i];
                break;
            case ANX7150_V1280x720p_60Hz:
                //D("1280x720p_60Hz!\n");
                ANX7150_video_timing_parameter[i] = ANX7150_video_timingtype_table.ANX7150_1280x720p_60Hz[i];
                break;
            case ANX7150_V1920x1080i_60Hz:
                //D("1920x1080i_60Hz!\n");
                ANX7150_video_timing_parameter[i] = ANX7150_video_timingtype_table.ANX7150_1920x1080i_60Hz[i];
                break;
            case ANX7150_V720x480i_60Hz_4x3:
            case ANX7150_V720x480i_60Hz_16x9:
                //D("720x480i_60Hz!\n");
                ANX7150_video_timing_parameter[i] = ANX7150_video_timingtype_table.ANX7150_720x480i_60Hz[i];
                break;
            case ANX7150_V720x576p_50Hz_4x3:
            case ANX7150_V720x576p_50Hz_16x9:
                //D("720x576p_50Hz!\n");
                ANX7150_video_timing_parameter[i] = ANX7150_video_timingtype_table.ANX7150_720x576p_50Hz[i];
                break;
            case ANX7150_V1280x720p_50Hz:
                //D("1280x720p_50Hz!\n");
                ANX7150_video_timing_parameter[i] = ANX7150_video_timingtype_table.ANX7150_1280x720p_50Hz[i];
                break;
            case ANX7150_V1920x1080i_50Hz:
                //D("1920x1080i_50Hz!\n");
                ANX7150_video_timing_parameter[i] = ANX7150_video_timingtype_table.ANX7150_1920x1080i_50Hz[i];
                break;
            case ANX7150_V720x576i_50Hz_4x3:
            case ANX7150_V720x576i_50Hz_16x9:
                //D("720x576i_50Hz!\n");
                ANX7150_video_timing_parameter[i] = ANX7150_video_timingtype_table.ANX7150_720x576i_50Hz[i];
                break;

            default:
                break;
        }
        //D("Video_Timing_Parameter[%.2x]=%.2x\n", (u32)i, (u32) ANX7150_video_timing_parameter[i]);
    }
    /*#else
        for(i = 0; i < 18; i++)
        {
            switch(ANX7150_video_timing_id)
            {
                case ANX7150_V640x480p_60Hz:
                    //D("640x480p_60Hz!\n");
                    ANX7150_video_timing_parameter[i] = Load_from_EEPROM(0, i);
                    DRVDelayMs(3);
                    break;
                case ANX7150_V720x480p_60Hz_4x3:
                case ANX7150_V720x480p_60Hz_16x9:
                    //D("720x480p_60Hz!\n");
                    ANX7150_video_timing_parameter[i] = Load_from_EEPROM(0, 18 + i);
                    DRVDelayMs(3);
                    break;
                case ANX7150_V1280x720p_60Hz:
                    //D("1280x720p_60Hz!\n");
                    ANX7150_video_timing_parameter[i] = Load_from_EEPROM(0, 36 + i);
                    DRVDelayMs(3);
                    break;
                case ANX7150_V1920x1080i_60Hz:
                    //D("1920x1080i_60Hz!\n");
                    ANX7150_video_timing_parameter[i] = Load_from_EEPROM(0, 54 + i);
                    DRVDelayMs(3);
                    break;
                case ANX7150_V720x480i_60Hz_4x3:
                case ANX7150_V720x480i_60Hz_16x9:
                    //D("720x480i_60Hz!\n");
                    ANX7150_video_timing_parameter[i] = Load_from_EEPROM(0, 72 + i);
                    DRVDelayMs(3);
                    break;
                case ANX7150_V720x576p_50Hz_4x3:
                case ANX7150_V720x576p_50Hz_16x9:
                    //D("720x576p_50Hz!\n");
                    ANX7150_video_timing_parameter[i] = Load_from_EEPROM(0, 90 + i);
                    DRVDelayMs(3);
                    break;
                case ANX7150_V1280x720p_50Hz:
                    //D("1280x720p_50Hz!\n");
                    ANX7150_video_timing_parameter[i] = Load_from_EEPROM(0, 108 + i);
                    DRVDelayMs(3);
                    break;
                case ANX7150_V1920x1080i_50Hz:
                    //D("1920x1080i_50Hz!\n");
                    ANX7150_video_timing_parameter[i] = Load_from_EEPROM(0, 126 + i);
                    DRVDelayMs(3);
                    break;
                case ANX7150_V720x576i_50Hz_4x3:
                case ANX7150_V720x576i_50Hz_16x9:
                    //D("720x576i_50Hz!\n");
                    ANX7150_video_timing_parameter[i] = Load_from_EEPROM(0, 144 + i);
                    DRVDelayMs(3);
                    break;

                default:
                    break;
            }
            //D("Video_Timing_Parameter[%.2x]=%.2x\n", (u32)i, (u32) ANX7150_video_timing_parameter[i]);
        }
    #endif*/
}
static void ANX7150_Parse_Video_Format(void)
{
    switch (ANX7150_video_format_config)
    {
        case ANX7150_RGB_YCrCb444_SepSync:
            ANX7150_emb_sync_mode = 0;
            ANX7150_demux_yc_en = 0;
            ANX7150_ddr_bus_mode = 0;
            ANX7150_de_gen_en = 0;
            //D("RGB_YCrCb444_SepSync mode!\n");
            break;
        case ANX7150_YCrCb422_SepSync:
            ANX7150_emb_sync_mode = 0;
            ANX7150_demux_yc_en = 0;
            ANX7150_ddr_bus_mode = 0;
            ANX7150_de_gen_en = 0;
            //D("YCrCb422_SepSync mode!\n");
            break;
        case ANX7150_YCrCb422_EmbSync:
            //D("YCrCb422_EmbSync mode!\n");
            ANX7150_demux_yc_en = 0;
            ANX7150_ddr_bus_mode = 0;
            ANX7150_de_gen_en = 0;
            ANX7150_emb_sync_mode = 1;
            ANX7150_Get_Video_Timing();
            break;
        case ANX7150_YCMux422_SepSync_Mode1:
            //D("YCMux422_SepSync_Mode1 mode!\n");
            ANX7150_emb_sync_mode = 0;
            ANX7150_ddr_bus_mode = 0;
            ANX7150_de_gen_en = 0;
            ANX7150_ycmux_u8_sel = 0;
            ANX7150_demux_yc_en = 1;
            break;
        case ANX7150_YCMux422_SepSync_Mode2:
            //D("YCMux422_SepSync_Mode2 mode!\n");
            ANX7150_emb_sync_mode = 0;
            ANX7150_ddr_bus_mode = 0;
            ANX7150_de_gen_en = 0;
            ANX7150_ycmux_u8_sel = 1;
            ANX7150_demux_yc_en = 1;
            break;
        case ANX7150_YCMux422_EmbSync_Mode1:
            //D("YCMux422_EmbSync_Mode1 mode!\n");
            ANX7150_ddr_bus_mode = 0;
            ANX7150_de_gen_en = 0;
            ANX7150_emb_sync_mode = 1;
            ANX7150_ycmux_u8_sel = 0;
            ANX7150_demux_yc_en = 1;
            ANX7150_Get_Video_Timing();
            break;
        case ANX7150_YCMux422_EmbSync_Mode2:
            //D("YCMux422_EmbSync_Mode2 mode!\n");
            ANX7150_ddr_bus_mode = 0;
            ANX7150_de_gen_en = 0;
            ANX7150_emb_sync_mode = 1;
            ANX7150_ycmux_u8_sel = 1;
            ANX7150_demux_yc_en = 1;
            ANX7150_Get_Video_Timing();
            break;
        case ANX7150_RGB_YCrCb444_DDR_SepSync:
            //D("RGB_YCrCb444_DDR_SepSync mode!\n");
            ANX7150_emb_sync_mode = 0;
            ANX7150_demux_yc_en = 0;
            ANX7150_de_gen_en = 0;
            ANX7150_ddr_bus_mode = 1;
            break;
        case ANX7150_RGB_YCrCb444_DDR_EmbSync:
            //D("RGB_YCrCb444_DDR_EmbSync mode!\n");
            ANX7150_demux_yc_en = 0;
            ANX7150_de_gen_en = 0;
            ANX7150_emb_sync_mode = 1;
            ANX7150_ddr_bus_mode = 1;
            ANX7150_Get_Video_Timing();
            break;
        case ANX7150_RGB_YCrCb444_SepSync_No_DE:
            //D("RGB_YCrCb444_SepSync_No_DE mode!\n");
            ANX7150_emb_sync_mode = 0;
            ANX7150_demux_yc_en = 0;
            ANX7150_ddr_bus_mode = 0;
            ANX7150_de_gen_en = 1;
            ANX7150_Get_Video_Timing();
            break;
        case ANX7150_YCrCb422_SepSync_No_DE:
            //D("YCrCb422_SepSync_No_DE mode!\n");
            ANX7150_emb_sync_mode = 0;
            ANX7150_demux_yc_en = 0;
            ANX7150_ddr_bus_mode = 0;
            ANX7150_de_gen_en = 1;
            ANX7150_Get_Video_Timing();
            break;
        default:
            break;
    }
}
static int anx7150_de_generator(struct i2c_client *client)
{
	int rc = 0;
	char c;
    u8 video_type,hsync_pol,vsync_pol,v_fp,v_bp,vsync_width;
    u8 hsync_width_low,hsync_width_high,v_active_low,v_active_high;
    u8 h_active_low,h_active_high,h_res_low,h_res_high,h_bp_low,h_bp_high;
    u32 hsync_width,h_active,h_res,h_bp;

    video_type = ANX7150_video_timing_parameter[15];
    hsync_pol = ANX7150_video_timing_parameter[16];
    vsync_pol = ANX7150_video_timing_parameter[17];
    v_fp = ANX7150_video_timing_parameter[12];
    v_bp = ANX7150_video_timing_parameter[11];
    vsync_width = ANX7150_video_timing_parameter[10];
    hsync_width = ANX7150_video_timing_parameter[5];
    hsync_width = (hsync_width << 8) + ANX7150_video_timing_parameter[4];
    v_active_high = ANX7150_video_timing_parameter[9];
    v_active_low = ANX7150_video_timing_parameter[8];
    h_active = ANX7150_video_timing_parameter[3];
    h_active = (h_active << 8) + ANX7150_video_timing_parameter[2];
    h_res = ANX7150_video_timing_parameter[1];
    h_res = (h_res << 8) + ANX7150_video_timing_parameter[0];
    h_bp = ANX7150_video_timing_parameter[7];
    h_bp = (h_bp << 8) + ANX7150_video_timing_parameter[6];
    if (ANX7150_demux_yc_en)
    {
        hsync_width = 2* hsync_width;
        h_active = 2 * h_active;
        h_res = 2 * h_res;
        h_bp = 2 * h_bp;
    }
    hsync_width_low = hsync_width & 0xff;
    hsync_width_high = (hsync_width >> 8) & 0xff;
    h_active_low = h_active & 0xff;
    h_active_high = (h_active >> 8) & 0xff;
    h_res_low = h_res & 0xff;
    h_res_high = (h_res >> 8) & 0xff;
    h_bp_low = h_bp & 0xff;
    h_bp_high = (h_bp >> 8) & 0xff;

	rc = anx7150_i2c_read_p0_reg(client, ANX7150_VID_CAPCTRL1_REG, &c);
	c = (c & 0xf7) | video_type;
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_VID_CAPCTRL1_REG, &c);
	rc = anx7150_i2c_read_p0_reg(client, ANX7150_VID_CAPCTRL1_REG, &c);
	c = (c & 0xdf) | hsync_pol;
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_VID_CAPCTRL1_REG, &c);
	rc = anx7150_i2c_read_p0_reg(client, ANX7150_VID_CAPCTRL1_REG, &c);
	c = (c & 0xbf) | vsync_pol;
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_VID_CAPCTRL1_REG, &c);
	c = v_active_low;
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_ACT_LINEL_REG, &c);
	c = v_active_high;
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_ACT_LINEH_REG, &c);
	c = vsync_width;
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_VSYNC_WID_REG, &c);
	c = v_bp;
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_VSYNC_TAIL2VIDLINE_REG, &c);
	c = h_active_low;
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_VID_PIXL_REG, &c);
	c = h_active_high;
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_VID_PIXH_REG, &c);
	c = h_res_low;
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_H_RESL_REG, &c);
	c = h_res_high;
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_H_RESH_REG, &c);
  	c = hsync_width_low;
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_HSYNC_ACT_WIDTHL_REG, &c);
    c = hsync_width_high;
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_HSYNC_ACT_WIDTHH_REG, &c);
	c = h_bp_low;
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_H_BACKPORCHL_REG, &c);
	c = h_bp_high;
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_H_BACKPORCHH_REG, &c);
	rc = anx7150_i2c_read_p0_reg(client, ANX7150_VID_CAPCTRL0_REG, &c);
	c |= ANX7150_VID_CAPCTRL0_DEGEN_EN;
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_VID_CAPCTRL0_REG, &c);

	return rc;
}
static int anx7150_embed_sync_decode(struct i2c_client *client)
{
	int rc = 0;
	char c;
	 u8 video_type,hsync_pol,vsync_pol,v_fp,vsync_width;
	 u8 h_fp_low,h_fp_high,hsync_width_low,hsync_width_high;
	 u32 h_fp,hsync_width;
	
	 video_type = ANX7150_video_timing_parameter[15];
	 hsync_pol = ANX7150_video_timing_parameter[16];
	 vsync_pol = ANX7150_video_timing_parameter[17];
	 v_fp = ANX7150_video_timing_parameter[12];
	 vsync_width = ANX7150_video_timing_parameter[10];
	 h_fp = ANX7150_video_timing_parameter[14];
	 h_fp = (h_fp << 8) + ANX7150_video_timing_parameter[13];
	 hsync_width = ANX7150_video_timing_parameter[5];
	 hsync_width = (hsync_width << 8) + ANX7150_video_timing_parameter[4];
	 if (ANX7150_demux_yc_en)
	 {
		 h_fp = 2 * h_fp;
		 hsync_width = 2* hsync_width;
	 }
	 h_fp_low = h_fp & 0xff;
	 h_fp_high = (h_fp >> 8) & 0xff;
	 hsync_width_low = hsync_width & 0xff;
	 hsync_width_high = (hsync_width >> 8) & 0xff;

	rc = anx7150_i2c_read_p0_reg(client, ANX7150_VID_CAPCTRL1_REG, &c);
	c = (c & 0xf7) | video_type;
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_VID_CAPCTRL1_REG, &c);
	rc = anx7150_i2c_read_p0_reg(client, ANX7150_VID_CAPCTRL1_REG, &c);
	c = (c & 0xdf) | hsync_pol;
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_VID_CAPCTRL1_REG, &c);
	rc = anx7150_i2c_read_p0_reg(client, ANX7150_VID_CAPCTRL1_REG, &c);
	c = (c & 0xbf) | vsync_pol;
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_VID_CAPCTRL1_REG, &c);
	rc = anx7150_i2c_read_p0_reg(client, ANX7150_VID_CAPCTRL0_REG, &c);
	c = c | ANX7150_VID_CAPCTRL0_EMSYNC_EN;
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_VID_CAPCTRL0_REG, &c);

	c = v_fp;
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_ACT_LINE2VSYNC_REG, &c);
	c = vsync_width;
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_VSYNC_WID_REG, &c);
	c = h_fp_low;
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_H_FRONTPORCHL_REG, &c);
	c = h_fp_high;
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_H_FRONTPORCHH_REG, &c);
	c = hsync_width_low;
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_HSYNC_ACT_WIDTHL_REG, &c);
	c = hsync_width_high;
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_HSYNC_ACT_WIDTHH_REG, &c);
	return rc;
}
int ANX7150_Blue_Screen(struct anx7150_pdata *anx)
{
	return anx7150_blue_screen_format_config(anx->client);
}
//******************************Video Config***************************************
int ANX7150_Config_Video(struct i2c_client *client)
{
	int rc = 0;
	int retry = 0;
    char c,TX_is_HDMI;
    char cspace_y2r, y2r_sel, up_sample,range_y2r;

    cspace_y2r = 0;
    y2r_sel = 0;
    up_sample = 0;
    range_y2r = 0;

    //ANX7150_RGBorYCbCr = 0x00;						//RGB
    //ANX7150_RGBorYCbCr = ANX7150_INPUT_COLORSPACE;						//update
	c = 0x00;
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_VID_MODE_REG, &c);
	rc = anx7150_i2c_read_p0_reg(client, ANX7150_VID_CTRL_REG, &c);
	c &= (~ANX7150_VID_CTRL_u8CTRL_EN);
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_VID_CTRL_REG, &c);
/*
    if (!ANX7150_system_config_done)
    {
        D("System has not finished config!\n");
        return;
    }
*/
	rc = anx7150_i2c_read_p0_reg(client, ANX7150_SYS_STATE_REG, &c);
    if (!(c & 0x02))
    {
        hdmi_dbg(&client->dev, "No clock detected !\n");
        //ANX7150_i2c_write_p0_reg(ANX7150_SYS_CTRL2_REG, 0x02);
        return -1;
    }

    rc = anx7150_clean_hdcp(client);

    //color space issue
    switch (ANX7150_video_timing_id)
    {
        case ANX7150_V1280x720p_50Hz:
        case ANX7150_V1280x720p_60Hz:
        case ANX7150_V1920x1080i_60Hz:
        case ANX7150_V1920x1080i_50Hz:
        case ANX7150_V1920x1080p_60Hz:
        case ANX7150_V1920x1080p_50Hz:
            y2r_sel = ANX7150_CSC_BT709;
            break;
        default:
            y2r_sel = ANX7150_CSC_BT601;
            break;
    }
    //rang[0~255]/[16~235] select
    if (ANX7150_video_timing_id == ANX7150_V640x480p_60Hz)
        range_y2r = 1;//rang[0~255]
    else
        range_y2r = 0;//rang[16~235]
    if ((ANX7150_RGBorYCbCr == ANX7150_YCbCr422) && (!ANX7150_edid_result.ycbcr422_supported))
    {
        up_sample = 1;
        if (ANX7150_edid_result.ycbcr444_supported)
            cspace_y2r = 0;
        else
            cspace_y2r = 1;
    }
    if ((ANX7150_RGBorYCbCr == ANX7150_YCbCr444) && (!ANX7150_edid_result.ycbcr444_supported))
    {
        cspace_y2r = 1;
    }
    //Config the embeded blue screen format according to output video format.
    rc = anx7150_blue_screen_format_config(client);

    ANX7150_Parse_Video_Format();

    if (ANX7150_de_gen_en)
    {
        hdmi_dbg(&client->dev, "ANX7150_de_gen_en!\n");
        rc = anx7150_de_generator(client);
    }
    else
    {
    	rc = anx7150_i2c_read_p0_reg(client, ANX7150_VID_CAPCTRL0_REG, &c);
		c &= (~ANX7150_VID_CAPCTRL0_DEGEN_EN);
		rc = anx7150_i2c_write_p0_reg(client, ANX7150_VID_CAPCTRL0_REG, &c);
    }
    if (ANX7150_emb_sync_mode)
    {
        hdmi_dbg(&client->dev, "ANX7150_Embed_Sync_Decode!\n");
        rc = anx7150_embed_sync_decode(client);
		
        if (ANX7150_ddr_bus_mode) //jack wen; for DDR embeded sync
        {
        	rc = anx7150_i2c_read_p0_reg(client, ANX7150_SYS_CTRL4_REG, &c);
			c |= (0x04);
			rc = anx7150_i2c_write_p0_reg(client, ANX7150_SYS_CTRL4_REG, &c);
        }
        else
        {
        	rc = anx7150_i2c_read_p0_reg(client, ANX7150_SYS_CTRL4_REG, &c);
			c &= (0xfb);
			rc = anx7150_i2c_write_p0_reg(client, ANX7150_SYS_CTRL4_REG, &c);
        }
    }
    else
    {
    	rc = anx7150_i2c_read_p0_reg(client, ANX7150_VID_CAPCTRL0_REG, &c);
		c &= (~ANX7150_VID_CAPCTRL0_EMSYNC_EN);
		rc = anx7150_i2c_write_p0_reg(client, ANX7150_VID_CAPCTRL0_REG, &c);
    }
    if (ANX7150_demux_yc_en)
    {
        hdmi_dbg(&client->dev, "ANX7150_demux_yc_en!\n");
		rc = anx7150_i2c_read_p0_reg(client, ANX7150_VID_CAPCTRL0_REG, &c);
		c |= (ANX7150_VID_CAPCTRL0_DEMUX_EN);
		rc = anx7150_i2c_write_p0_reg(client, ANX7150_VID_CAPCTRL0_REG, &c);
		
        if (ANX7150_ycmux_u8_sel)
        {
        	rc = anx7150_i2c_read_p0_reg(client, ANX7150_VID_CTRL_REG, &c);
			c |= (ANX7150_VID_CTRL_YCu8_SEL);
			rc = anx7150_i2c_write_p0_reg(client, ANX7150_VID_CTRL_REG, &c);
            //jack wen, u8 mapping for yc mux, D3-8,1-0 -->D1-4
            hdmi_dbg(&client->dev, "ANX7150_demux_yc_en!####D1-4\n");

			rc = anx7150_i2c_read_p0_reg(client, ANX7150_VID_CTRL_REG, &c);
			c |= (ANX7150_VID_CTRL_u8CTRL_EN);
			rc = anx7150_i2c_write_p0_reg(client, ANX7150_VID_CTRL_REG, &c);

			c = 0x0d;
			rc = anx7150_i2c_write_p0_reg(client, VID_u8_CTRL11, &c);
			c = 0x0c;
			rc = anx7150_i2c_write_p0_reg(client, VID_u8_CTRL10, &c);
			c = 0x0b;
			rc = anx7150_i2c_write_p0_reg(client, VID_u8_CTRL9, &c);
			c = 0x0a;
			rc = anx7150_i2c_write_p0_reg(client, VID_u8_CTRL8, &c);
			c = 0x09;
			rc = anx7150_i2c_write_p0_reg(client, VID_u8_CTRL7, &c);
			c = 0x08;
			rc = anx7150_i2c_write_p0_reg(client, VID_u8_CTRL6, &c);
			c = 0x01;
			rc = anx7150_i2c_write_p0_reg(client, VID_u8_CTRL5, &c);
			c = 0x00;
			rc = anx7150_i2c_write_p0_reg(client, VID_u8_CTRL4, &c);
            //
        }
        else
        {
        	rc = anx7150_i2c_read_p0_reg(client, ANX7150_VID_CTRL_REG, &c);
			c &= (~ANX7150_VID_CTRL_YCu8_SEL);
			rc = anx7150_i2c_write_p0_reg(client, ANX7150_VID_CTRL_REG, &c);
            //jack wen, u8 mapping for yc mux, D3-8,1-0 -->D5-8,
          	hdmi_dbg(&client->dev, "ANX7150_demux_yc_en!####D5-8\n");
			rc = anx7150_i2c_read_p0_reg(client, ANX7150_VID_CTRL_REG, &c);
			c |= (ANX7150_VID_CTRL_u8CTRL_EN);
			rc = anx7150_i2c_write_p0_reg(client, ANX7150_VID_CTRL_REG, &c);
			
            c = 0x0d;
			rc = anx7150_i2c_write_p0_reg(client, VID_u8_CTRL15, &c);
			c = 0x0c;
			rc = anx7150_i2c_write_p0_reg(client, VID_u8_CTRL14, &c);
			c = 0x0b;
			rc = anx7150_i2c_write_p0_reg(client, VID_u8_CTRL3, &c);
			c = 0x0a;
			rc = anx7150_i2c_write_p0_reg(client, VID_u8_CTRL12, &c);
			c = 0x09;
			rc = anx7150_i2c_write_p0_reg(client, VID_u8_CTRL11, &c);
			c = 0x08;
			rc = anx7150_i2c_write_p0_reg(client, VID_u8_CTRL10, &c);
			c = 0x01;
			rc = anx7150_i2c_write_p0_reg(client, VID_u8_CTRL9, &c);
			c = 0x00;
			rc = anx7150_i2c_write_p0_reg(client, VID_u8_CTRL8, &c);
            //
        }
    }
    else
    {
    	rc = anx7150_i2c_read_p0_reg(client, ANX7150_VID_CAPCTRL0_REG, &c);
		c &= (~ANX7150_VID_CAPCTRL0_DEMUX_EN);
		rc = anx7150_i2c_write_p0_reg(client, ANX7150_VID_CAPCTRL0_REG, &c);
        //jack wen

        //

    }
    if (ANX7150_ddr_bus_mode)
    {
        //D("ANX7150_ddr_bus_mode!\n");
        rc = anx7150_i2c_read_p0_reg(client, ANX7150_VID_CAPCTRL0_REG, &c);
		c |= (ANX7150_VID_CAPCTRL0_DV_BUSMODE);
		rc = anx7150_i2c_write_p0_reg(client, ANX7150_VID_CAPCTRL0_REG, &c);
		 //jack wen
		rc = anx7150_i2c_read_p0_reg(client, ANX7150_SYS_CTRL4_REG, &c);
		c = (c & 0xfc) | 0x02;
		rc = anx7150_i2c_write_p0_reg(client, ANX7150_SYS_CTRL4_REG, &c);
		rc = anx7150_i2c_read_p0_reg(client, ANX7150_VID_CTRL_REG, &c);
		c |= (ANX7150_VID_CTRL_YCu8_SEL);
		rc = anx7150_i2c_write_p0_reg(client, ANX7150_VID_CTRL_REG, &c);
       
		//jack wen

        if (ANX7150_ddr_edge)
        {
        	rc = anx7150_i2c_read_p0_reg(client, ANX7150_VID_CAPCTRL0_REG, &c);
			c |= (ANX7150_VID_CAPCTRL0_DDR_EDGE);
			rc = anx7150_i2c_write_p0_reg(client, ANX7150_VID_CAPCTRL0_REG, &c);
          }
        else
        {
        	rc = anx7150_i2c_read_p0_reg(client, ANX7150_VID_CAPCTRL0_REG, &c);
			c &= (~ANX7150_VID_CAPCTRL0_DDR_EDGE);
			rc = anx7150_i2c_write_p0_reg(client, ANX7150_VID_CAPCTRL0_REG, &c);
         }

        //jack wen for DDR+seperate maping
        if (ANX7150_video_format_config == 0x07)//jack wen, DDR yc422, 601,
        {
            hdmi_dbg(&client->dev, "ANX7150_DDR_601_Maping!\n");
			
			rc = anx7150_i2c_read_p0_reg(client, ANX7150_VID_CTRL_REG, &c);
			c |= (ANX7150_VID_CTRL_u8CTRL_EN);
			rc = anx7150_i2c_write_p0_reg(client, ANX7150_VID_CTRL_REG, &c);

			c = 0x0b;
			rc = anx7150_i2c_write_p0_reg(client, VID_u8_CTRL23, &c);
			c = 0x0a;
			rc = anx7150_i2c_write_p0_reg(client, VID_u8_CTRL22, &c);
			c = 0x09;
			rc = anx7150_i2c_write_p0_reg(client, VID_u8_CTRL21, &c);
			c = 0x08;
			rc = anx7150_i2c_write_p0_reg(client, VID_u8_CTRL20, &c);
			c = 0x07;
			rc = anx7150_i2c_write_p0_reg(client, VID_u8_CTRL19, &c);
			c = 0x06;
			rc = anx7150_i2c_write_p0_reg(client, VID_u8_CTRL18, &c);
			c = 0x05;
			rc = anx7150_i2c_write_p0_reg(client, VID_u8_CTRL17, &c);
			c = 0x04;
			rc = anx7150_i2c_write_p0_reg(client, VID_u8_CTRL16, &c);

			c = 0x17;
			rc = anx7150_i2c_write_p0_reg(client, VID_u8_CTRL15, &c);
			c = 0x16;
			rc = anx7150_i2c_write_p0_reg(client, VID_u8_CTRL14, &c);
			c = 0x15;
			rc = anx7150_i2c_write_p0_reg(client, VID_u8_CTRL13, &c);
			c = 0x14;
			rc = anx7150_i2c_write_p0_reg(client, VID_u8_CTRL12, &c);
			c = 0x13;
			rc = anx7150_i2c_write_p0_reg(client, VID_u8_CTRL11, &c);
			c = 0x12;
			rc = anx7150_i2c_write_p0_reg(client, VID_u8_CTRL10, &c);
			c = 0x11;
			rc = anx7150_i2c_write_p0_reg(client, VID_u8_CTRL9, &c);
			c = 0x10;
			rc = anx7150_i2c_write_p0_reg(client, VID_u8_CTRL8, &c);

            c = 0x03;
			rc = anx7150_i2c_write_p0_reg(client, VID_u8_CTRL7, &c);
			c = 0x02;
			rc = anx7150_i2c_write_p0_reg(client, VID_u8_CTRL6, &c);
			c = 0x01;
			rc = anx7150_i2c_write_p0_reg(client, VID_u8_CTRL5, &c);
			c = 0x00;
			rc = anx7150_i2c_write_p0_reg(client, VID_u8_CTRL4, &c);
			c = 0x0f;
			rc = anx7150_i2c_write_p0_reg(client, VID_u8_CTRL3, &c);
			c = 0x0e;
			rc = anx7150_i2c_write_p0_reg(client, VID_u8_CTRL2, &c);
			c = 0x0d;
			rc = anx7150_i2c_write_p0_reg(client, VID_u8_CTRL1, &c);
			c = 0x0c;
			rc = anx7150_i2c_write_p0_reg(client, VID_u8_CTRL0, &c);

        }
        else if (ANX7150_video_format_config == 0x08)//jack wen, DDR yc422, 656,
        {
            hdmi_dbg(&client->dev, "ANX7150_DDR_656_Maping!\n");

			rc = anx7150_i2c_read_p0_reg(client, ANX7150_VID_CTRL_REG, &c);
			c &= (~ANX7150_VID_CTRL_u8CTRL_EN);
			rc = anx7150_i2c_write_p0_reg(client, ANX7150_VID_CTRL_REG, &c);
        }
    }
    else
    {
    	rc = anx7150_i2c_read_p0_reg(client, ANX7150_VID_CAPCTRL0_REG, &c);
		c &= (~ANX7150_VID_CAPCTRL0_DV_BUSMODE);
		rc = anx7150_i2c_write_p0_reg(client, ANX7150_VID_CAPCTRL0_REG, &c);
		rc = anx7150_i2c_read_p0_reg(client, ANX7150_VID_CAPCTRL0_REG, &c);
		c &= (~ANX7150_VID_CAPCTRL0_DDR_EDGE);
		rc = anx7150_i2c_write_p0_reg(client, ANX7150_VID_CAPCTRL0_REG, &c);

		rc = anx7150_i2c_read_p0_reg(client, ANX7150_SYS_CTRL4_REG, &c);
		c &= (0xfc);
		rc = anx7150_i2c_write_p0_reg(client, ANX7150_SYS_CTRL4_REG, &c);
    }

    if (cspace_y2r)
    {
        hdmi_dbg(&client->dev, "Color space Y2R enabled********\n");
		rc = anx7150_i2c_read_p0_reg(client, ANX7150_VID_MODE_REG, &c);
		c |= (ANX7150_VID_MODE_CSPACE_Y2R);
		rc = anx7150_i2c_write_p0_reg(client, ANX7150_VID_MODE_REG, &c);
        if (y2r_sel)
        {
            hdmi_dbg(&client->dev, "Y2R_SEL!\n");
			rc = anx7150_i2c_read_p0_reg(client, ANX7150_VID_MODE_REG, &c);
			c |= (ANX7150_VID_MODE_Y2R_SEL);
			rc = anx7150_i2c_write_p0_reg(client, ANX7150_VID_MODE_REG, &c);
          }
        else
        {
        	rc = anx7150_i2c_read_p0_reg(client, ANX7150_VID_MODE_REG, &c);
			c &= (~ANX7150_VID_MODE_Y2R_SEL);
			rc = anx7150_i2c_write_p0_reg(client, ANX7150_VID_MODE_REG, &c);	
         }
    }
    else
    {
    	rc = anx7150_i2c_read_p0_reg(client, ANX7150_VID_MODE_REG, &c);
		c &= (~ANX7150_VID_MODE_CSPACE_Y2R);
		rc = anx7150_i2c_write_p0_reg(client, ANX7150_VID_MODE_REG, &c);
    }

    if (up_sample)
    {
        hdmi_dbg(&client->dev, "UP_SAMPLE!\n");
		rc = anx7150_i2c_read_p0_reg(client, ANX7150_VID_MODE_REG, &c);
		c |= (ANX7150_VID_MODE_UPSAMPLE);
		rc = anx7150_i2c_write_p0_reg(client, ANX7150_VID_MODE_REG, &c);
    }
    else
    {
    	rc = anx7150_i2c_read_p0_reg(client, ANX7150_VID_MODE_REG, &c);
		c &= (~ANX7150_VID_MODE_UPSAMPLE);
		rc = anx7150_i2c_write_p0_reg(client, ANX7150_VID_MODE_REG, &c);
    }

    if (range_y2r)
    {
    	rc = anx7150_i2c_read_p0_reg(client, ANX7150_VID_MODE_REG, &c);
		c |= (ANX7150_VID_MODE_RANGE_Y2R);
		rc = anx7150_i2c_write_p0_reg(client, ANX7150_VID_MODE_REG, &c);
    }
    else
    {
    	rc = anx7150_i2c_read_p0_reg(client, ANX7150_VID_MODE_REG, &c);
		c &= (~ANX7150_VID_MODE_RANGE_Y2R);
		rc = anx7150_i2c_write_p0_reg(client, ANX7150_VID_MODE_REG, &c);
    }

    if (!ANX7150_pix_rpt_set_by_sys)
    {
        if ((ANX7150_video_timing_id == ANX7150_V720x480i_60Hz_16x9)
                || (ANX7150_video_timing_id == ANX7150_V720x576i_50Hz_16x9)
                || (ANX7150_video_timing_id == ANX7150_V720x480i_60Hz_4x3)
                || (ANX7150_video_timing_id == ANX7150_V720x576i_50Hz_4x3))
            ANX7150_tx_pix_rpt = 1;
        else
            ANX7150_tx_pix_rpt = 0;
    }
    //set input pixel repeat times
    rc = anx7150_i2c_read_p0_reg(client, ANX7150_VID_MODE_REG, &c);
	c = ((c & 0xfc) |ANX7150_in_pix_rpt);
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_VID_MODE_REG, &c);
    //set link pixel repeat times
    rc = anx7150_i2c_read_p0_reg(client, ANX7150_VID_CTRL_REG, &c);
	c = ((c & 0xfc) |ANX7150_tx_pix_rpt);
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_VID_CTRL_REG, &c);

    if ((ANX7150_in_pix_rpt != ANX7150_in_pix_rpt_bkp)
            ||(ANX7150_tx_pix_rpt != ANX7150_tx_pix_rpt_bkp) )
    {
    	c = 0x02;
		rc = anx7150_i2c_write_p0_reg(client, ANX7150_SYS_CTRL2_REG, &c);
		c = 0x00;
		rc = anx7150_i2c_write_p0_reg(client, ANX7150_SYS_CTRL2_REG, &c);
        hdmi_dbg(&client->dev, "MISC_Reset!\n");
        ANX7150_in_pix_rpt_bkp = ANX7150_in_pix_rpt;
        ANX7150_tx_pix_rpt_bkp = ANX7150_tx_pix_rpt;
    }
    //enable video input
    rc = anx7150_i2c_read_p0_reg(client, ANX7150_VID_CTRL_REG, &c);
	c |= (ANX7150_VID_CTRL_IN_EN);
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_VID_CTRL_REG, &c);
    //D("Video configure OK!\n");

	retry = 0;
	do{
	    mdelay(60);
		rc = anx7150_i2c_read_p0_reg(client, ANX7150_VID_STATUS_REG, &c);
	    if (c & ANX7150_VID_STATUS_VID_STABLE){
	        hdmi_dbg(&client->dev, "Video stable, continue!\n");
	        break;
	    }
		else{
			hdmi_dbg(&client->dev,"Video not stable!, retry = %d\n", retry);
		}
	}while(retry++ < 5);

    if (cspace_y2r)
        ANX7150_RGBorYCbCr = ANX7150_RGB;
    //Enable video CLK,Format change after config video.
    // ANX7150_i2c_read_p0_reg(ANX7150_INTR1_MASK_REG, &c);
    // ANX7150_i2c_write_p0_reg(ANX7150_INTR1_MASK_REG, c |0x01);//3
    rc = anx7150_i2c_read_p0_reg(client, ANX7150_INTR2_MASK_REG, &c);
	c |= (0x48);
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_INTR2_MASK_REG, &c);
	rc = anx7150_i2c_read_p0_reg(client, ANX7150_INTR3_MASK_REG, &c);
	c |= (0x40);
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_INTR3_MASK_REG, &c);
	
    if (ANX7150_edid_result.is_HDMI)
    {
    	rc = anx7150_i2c_read_p0_reg(client, ANX7150_SYS_CTRL1_REG, &c);
		c |= (0x02);
		rc = anx7150_i2c_write_p0_reg(client, ANX7150_SYS_CTRL1_REG, &c);
        hdmi_dbg(&client->dev,"ANX7150 is set to HDMI mode\n");
    }
	rc = anx7150_i2c_read_p0_reg(client, ANX7150_SYS_CTRL1_REG, &c);
    TX_is_HDMI = c & 0x02;

    if (TX_is_HDMI == 0x02)
    {
        anx7150_set_avmute(client);//wen
    }

    //reset TMDS link to align 4 channels  xy 061120
    hdmi_dbg(&client->dev,"reset TMDS link to align 4 channels\n");

	rc = anx7150_i2c_read_p0_reg(client, ANX7150_SRST_REG, &c);
	c |= (ANX7150_TX_RST);
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_SRST_REG, &c);
	c &= (~ANX7150_TX_RST);
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_SRST_REG, &c);
	
    //Enable TMDS clock output // just enable u87, and let the other u8s along to avoid overwriting.
    hdmi_dbg(&client->dev,"Enable TMDS clock output\n");
	rc = anx7150_i2c_read_p0_reg(client, ANX7150_TMDS_CLKCH_CONFIG_REG, &c);
	c |= (ANX7150_TMDS_CLKCH_MUTE);
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_TMDS_CLKCH_CONFIG_REG, &c);
	if(ANX7150_HDCP_enable)
    	mdelay(100);  //400ms only for HDCP CTS

    //ANX7150_i2c_read_p0_reg(ANX7150_VID_MODE_REG, &c);  //zy 061110
    return 0;
}
static u8 anx7150_config_i2s(struct i2c_client *client)
{
	int rc;
	char c = 0x00;
    u8 exe_result = 0x00;
    char c1 = 0x00;

    hdmi_dbg(&client->dev,"ANX7150: config i2s audio.\n");

    //select SCK as source
    rc = anx7150_i2c_read_p0_reg(client, ANX7150_HDMI_AUDCTRL1_REG, &c);
    c &=  ~ANX7150_HDMI_AUDCTRL1_CLK_SEL;
    hdmi_dbg(&client->dev,"select SCK as source, c = 0x%.2x\n",(u32)c);
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_HDMI_AUDCTRL1_REG, &c);


    //config i2s channel
    rc = anx7150_i2c_read_p0_reg(client, ANX7150_HDMI_AUDCTRL1_REG, &c);
    c1 = s_ANX7150_audio_config.i2s_config.audio_channel;    // need u8[5:2]
    c1 &= 0x3c;
    c &= ~0x3c;
    c |= c1;
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_HDMI_AUDCTRL1_REG, &c);
    hdmi_dbg(&client->dev,"config i2s channel, c = 0x%.2x\n",(u32)c);
	
    //config i2s format
    //ANX7150_i2c_read_p0_reg(ANX7150_I2S_CTRL_REG, &c);
    c = s_ANX7150_audio_config.i2s_config.i2s_format;
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_I2S_CTRL_REG, &c);
    hdmi_dbg(&client->dev,"config i2s format, c = 0x%.2x\n",(u32)c);

    //map i2s fifo

    //TODO: config I2S channel map register according to system


    //ANX7150_i2c_write_p0_reg(ANX7150_I2SCH_CTRL_REG, c);

    //swap right/left channel
    /*ANX7150_i2c_read_p0_reg(ANX7150_I2SCH_SWCTRL_REG, &c);
    c1 = 0x00;
    c1 &= 0xf0;
    c &= ~0xf0;
    c |= c1;
    ANX7150_i2c_write_p0_reg(ANX7150_I2SCH_SWCTRL_REG, c);
    D("map i2s ffio, c = 0x%.2x\n",(u32)c);*/

    //down sample
    rc = anx7150_i2c_read_p0_reg(client, ANX7150_HDMI_AUDCTRL0_REG, &c);
    c1 = s_ANX7150_audio_config.down_sample;
    c1 &= 0x60;
    c &= ~0x60;
    c |= c1;
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_HDMI_AUDCTRL0_REG, &c);
    hdmi_dbg(&client->dev,"down sample, c = 0x%.2x\n",(u32)c);

    //config i2s channel status(5 regs)
    c = s_ANX7150_audio_config.i2s_config.Channel_status1;
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_I2SCH_STATUS1_REG, &c);
    c = s_ANX7150_audio_config.i2s_config.Channel_status2;
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_I2SCH_STATUS2_REG, &c);
    c = s_ANX7150_audio_config.i2s_config.Channel_status3;
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_I2SCH_STATUS3_REG, &c);
    c = s_ANX7150_audio_config.i2s_config.Channel_status4;
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_I2SCH_STATUS4_REG, &c);
    hdmi_dbg(&client->dev,"@@@@@@@@config i2s channel status4, c = 0x%.2x\n",(unsigned int)c);//jack wen

    c = s_ANX7150_audio_config.i2s_config.Channel_status5;
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_I2SCH_STATUS5_REG, &c);
    hdmi_dbg(&client->dev,"config i2s channel status, c = 0x%.2x\n",(u32)c);

    exe_result = ANX7150_i2s_input;
    //D("return = 0x%.2x\n",(u32)exe_result);

    // open corresponding interrupt
    //ANX7150_i2c_read_p0_reg(ANX7150_INTR1_MASK_REG, &c);
    //ANX7150_i2c_write_p0_reg(ANX7150_INTR1_MASK_REG, (c | 0x22) );
    //ANX7150_i2c_read_p0_reg(ANX7150_INTR3_MASK_REG, &c);
    //ANX7150_i2c_write_p0_reg(ANX7150_INTR3_MASK_REG, (c | 0x20) );


    return exe_result;
}

static u8 anx7150_config_spdif(struct i2c_client *client)
{
	int rc = 0;
    u8 exe_result = 0x00;
    char c = 0x00;
    char c1 = 0x00;
 //   u8 c2 = 0x00;
 //   u8 freq_mclk = 0x00;

    hdmi_dbg(&client->dev, "ANX7150: config SPDIF audio.\n");


    //Select MCLK
    rc = anx7150_i2c_read_p0_reg(client, ANX7150_HDMI_AUDCTRL1_REG, &c);
	c |= (ANX7150_HDMI_AUDCTRL1_CLK_SEL);
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_HDMI_AUDCTRL1_REG, &c);

    //D("ANX7150: enable SPDIF audio.\n");
    //Enable SPDIF
    rc = anx7150_i2c_read_p0_reg(client, ANX7150_HDMI_AUDCTRL1_REG, &c);
	c |= (ANX7150_HDMI_AUDCTRL1_SPDIFIN_EN);
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_HDMI_AUDCTRL1_REG, &c);

    //adjust MCLK phase in interrupt routine

    // adjust FS_FREQ   //FS_FREQ
    c1 = s_ANX7150_audio_config.i2s_config.Channel_status4 & 0x0f;
	rc = anx7150_i2c_read_p0_reg(client, ANX7150_SPDIFCH_STATUS_REG, &c);
    c &= ANX7150_SPDIFCH_STATUS_FS_FREG;
    c = c >> 4;

    if ( c != c1)
    {
        //D("adjust FS_FREQ by system!\n");
        rc = anx7150_i2c_read_p0_reg(client, ANX7150_I2SCH_STATUS4_REG, &c);
        c &= 0xf0;
        c |= c1;
		rc = anx7150_i2c_write_p0_reg(client, ANX7150_I2SCH_STATUS4_REG, &c);

        //enable using FS_FREQ from 0x59
        rc = anx7150_i2c_read_p0_reg(client, ANX7150_HDMI_AUDCTRL1_REG, &c);
		c |= (0x02);
		rc = anx7150_i2c_write_p0_reg(client, ANX7150_HDMI_AUDCTRL1_REG, &c);
    }

    // down sample
    rc = anx7150_i2c_read_p0_reg(client, ANX7150_HDMI_AUDCTRL0_REG, &c);
    c1 = s_ANX7150_audio_config.down_sample;
    c1 &= 0x60;
    c &= ~0x60;
    c |= c1;
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_HDMI_AUDCTRL0_REG, &c);

    if (s_ANX7150_audio_config.down_sample)     //zy 060816
    {
        // adjust FS_FREQ by system because down sample
        //D("adjust FS_FREQ by system because down sample!\n");

        c1 = s_ANX7150_audio_config.i2s_config.Channel_status4 & 0x0f;
		rc = anx7150_i2c_read_p0_reg(client, ANX7150_I2SCH_STATUS4_REG, &c);
 
        c &= 0xf0;
        c |= c1;
		rc = anx7150_i2c_write_p0_reg(client, ANX7150_I2SCH_STATUS4_REG, &c);
    }


    // spdif is stable
    hdmi_dbg(&client->dev, "config SPDIF audio done");
    exe_result = ANX7150_spdif_input;

    // open corresponding interrupt
    rc = anx7150_i2c_read_p0_reg(client, ANX7150_INTR1_MASK_REG, &c);
	c |= (0x32);
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_INTR1_MASK_REG, &c);
    //ANX7150_i2c_read_p0_reg(ANX7150_INTR3_MASK_REG, &c);
    //ANX7150_i2c_write_p0_reg(ANX7150_INTR3_MASK_REG, (c | 0xa1) );
    return exe_result;
}

static u8 anx7150_config_super_audio(struct i2c_client *client)
{
	int rc = 0;
    u8 exe_result = 0x00;
    u8 c = 0x00;


    //D("ANX7150: config one u8 audio.\n");

    // select sck as source
    rc = anx7150_i2c_read_p0_reg(client, ANX7150_HDMI_AUDCTRL1_REG, &c);
	c &= (~ANX7150_HDMI_AUDCTRL1_CLK_SEL);
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_HDMI_AUDCTRL1_REG, &c);

    // Enable stream  0x60
    c = s_ANX7150_audio_config.super_audio_config.one_u8_ctrl;
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_ONEu8_AUD_CTRL_REG, &c);


    // Map stream 0x61
    // TODO: config super audio  map register according to system

    exe_result = ANX7150_super_audio_input;
    return exe_result;

}

u8 ANX7150_Config_Audio(struct i2c_client *client)
{
	int rc;
	char c = 0x00;
    u8 exe_result = 0x00;
    u8 audio_layout = 0x00;
    u8 fs = 0x00;
    u32 ACR_N = 0x0000;

    //set audio clock edge

	rc = anx7150_i2c_read_p0_reg(client, ANX7150_HDMI_AUDCTRL0_REG, &c);
	c = ((c & 0xf7) | ANX7150_audio_clock_edge);
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_HDMI_AUDCTRL0_REG, &c);
	
    //cts get select from SCK
    rc = anx7150_i2c_read_p0_reg(client, ANX7150_HDMI_AUDCTRL0_REG, &c);
	c = (c & 0xef);
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_HDMI_AUDCTRL0_REG, &c);
    hdmi_dbg(&client->dev, "audio_type = 0x%.2x\n",(u32)s_ANX7150_audio_config.audio_type);
    if (s_ANX7150_audio_config.audio_type & ANX7150_i2s_input)
    {
    	hdmi_dbg(&client->dev, "Config I2s.\n");
        exe_result |= anx7150_config_i2s(client);
    }
    else
    {
        //disable I2S audio input
        hdmi_dbg(&client->dev, "Disable I2S audio input.\n");
		rc = anx7150_i2c_read_p0_reg(client, ANX7150_HDMI_AUDCTRL1_REG, &c);
        c &= 0xc3;
		rc = anx7150_i2c_write_p0_reg(client, ANX7150_HDMI_AUDCTRL1_REG, &c);
    }

    if (s_ANX7150_audio_config.audio_type & ANX7150_spdif_input)
    {
        exe_result |= anx7150_config_spdif(client);
    }
    else
    {
        //disable SPDIF audio input
        hdmi_dbg(&client->dev, "Disable SPDIF audio input.\n");
		rc = anx7150_i2c_read_p0_reg(client, ANX7150_HDMI_AUDCTRL1_REG, &c);
        c &= ~ANX7150_HDMI_AUDCTRL1_SPDIFIN_EN;
		rc = anx7150_i2c_write_p0_reg(client, ANX7150_HDMI_AUDCTRL1_REG, &c);
    }

    if (s_ANX7150_audio_config.audio_type & ANX7150_super_audio_input)
    {
        exe_result |= anx7150_config_super_audio(client);
    }
    else
    {
        //disable super audio output
        hdmi_dbg(&client->dev, "ANX7150: disable super audio output.\n");
		c = 0x00;
		rc = anx7150_i2c_write_p0_reg(client, ANX7150_ONEu8_AUD_CTRL_REG, &c);
    }

    if ((s_ANX7150_audio_config.audio_type & 0x07) == 0x00)
    {
        hdmi_dbg(&client->dev, "ANX7150 input no audio type.\n");
    }

    //audio layout
    if (s_ANX7150_audio_config.audio_type & ANX7150_i2s_input)
    {
        //ANX7150_i2c_read_p0_reg(ANX7150_HDMI_AUDCTRL1_REG, &c);
        audio_layout = s_ANX7150_audio_config.audio_layout;

        //HDMI_RX_ReadI2C_RX0(0x15, &c);
#if 0
        if ((c & 0x08) ==0x08 )   //u8[5:3]
        {
            audio_layout = 0x80;
        }
        else
        {
            audio_layout = 0x00;
        }
#endif
    }
    if (s_ANX7150_audio_config.audio_type & ANX7150_super_audio_input)
    {
    	rc = anx7150_i2c_read_p0_reg(client, ANX7150_ONEu8_AUD_CTRL_REG, &c);
        if ( c & 0xfc)      //u8[5:3]
        {
            audio_layout = 0x80;
        }
    }
	rc = anx7150_i2c_read_p0_reg(client, ANX7150_HDMI_AUDCTRL0_REG, &c);
    c &= ~0x80;
    c |= audio_layout;
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_HDMI_AUDCTRL0_REG, &c);

    if (  (s_ANX7150_audio_config.audio_type & 0x07) == exe_result )
    {
        //Initial N value
		rc = anx7150_i2c_read_p0_reg(client, ANX7150_I2SCH_STATUS4_REG, &c);
        fs = c & 0x0f;
        // set default value to N
        ACR_N = ANX7150_N_48k;
        switch (fs)
        {
            case(0x00)://44.1k
                ACR_N = ANX7150_N_44k;
                break;
            case(0x02)://48k
                ACR_N = ANX7150_N_48k;
                break;
            case(0x03)://32k
                ACR_N = ANX7150_N_32k;
                break;
            case(0x08)://88k
                ACR_N = ANX7150_N_88k;
                break;
            case(0x0a)://96k
                ACR_N = ANX7150_N_96k;
                break;
            case(0x0c)://176k
                ACR_N = ANX7150_N_176k;
                break;
            case(0x0e)://192k
                ACR_N = ANX7150_N_192k;
                break;
            default:
                dev_err(&client->dev, "note wrong fs.\n");
                break;
        }
        // write N(ACR) to corresponding regs
        c = ACR_N;
		rc = anx7150_i2c_write_p1_reg(client, ANX7150_ACR_N1_SW_REG, &c);
        c = ACR_N>>8;
		rc = anx7150_i2c_write_p1_reg(client, ANX7150_ACR_N2_SW_REG, &c);
		c = 0x00;
		rc = anx7150_i2c_write_p1_reg(client, ANX7150_ACR_N3_SW_REG, &c);
	
        // set the relation of MCLK and Fs  xy 070117
        rc = anx7150_i2c_read_p0_reg(client, ANX7150_HDMI_AUDCTRL0_REG, &c);
		c = (c & 0xf8) | FREQ_MCLK;
		rc = anx7150_i2c_write_p0_reg(client, ANX7150_HDMI_AUDCTRL0_REG, &c);
       hdmi_dbg(&client->dev, "Audio MCLK input mode is: %.2x\n",(u32)FREQ_MCLK);

        //Enable control of ACR
        rc = anx7150_i2c_read_p1_reg(client, ANX7150_INFO_PKTCTRL1_REG, &c);
		c |= (ANX7150_INFO_PKTCTRL1_ACR_EN);
		rc = anx7150_i2c_write_p1_reg(client, ANX7150_INFO_PKTCTRL1_REG, &c);
        //audio enable:
        rc = anx7150_i2c_read_p0_reg(client, ANX7150_HDMI_AUDCTRL1_REG, &c);
		c |= (ANX7150_HDMI_AUDCTRL1_IN_EN);
		rc = anx7150_i2c_write_p0_reg(client, ANX7150_HDMI_AUDCTRL1_REG, &c);
    }

    return exe_result;

}
static u8 ANX7150_Checksum(infoframe_struct *p)
{
    u8 checksum = 0x00;
    u8 i;

    checksum = p->type + p->length + p->version;
    for (i=1; i <= p->length; i++)
    {
        checksum += p->pb_u8[i];
    }
    checksum = ~checksum;
    checksum += 0x01;

    return checksum;
}
static u8 anx7150_load_infoframe(struct i2c_client *client, packet_type member,
                             infoframe_struct *p)
{
	int rc = 0;
    u8 exe_result = 0x00;
    u8 address[8] = {0x00,0x20,0x40,0x60,0x80,0x80,0xa0,0xa0};
    u8 i;
    char c;

    p->pb_u8[0] = ANX7150_Checksum(p);

    // write infoframe to according regs
    c = p->type;
    rc = anx7150_i2c_write_p1_reg(client, address[member], &c);
	c = p->version;
    rc = anx7150_i2c_write_p1_reg(client, address[member]+1, &c);
	c = p->length;
    rc = anx7150_i2c_write_p1_reg(client, address[member]+2, &c);

    for (i=0; i <= p->length; i++)
    {
    	c = p->pb_u8[i];
    	rc = anx7150_i2c_write_p1_reg(client, address[member]+3+i, &c);
		rc = anx7150_i2c_read_p1_reg(client, address[member]+3+i, &c);
    }
    return exe_result;
}

//*************** Config Packet ****************************
u8 ANX7150_Config_Packet(struct i2c_client *client)
{
	int rc = 0;
    u8 exe_result = 0x00;     // There is no use in current solution
    u8 info_packet_sel;
    char c;

    info_packet_sel = s_ANX7150_packet_config.packets_need_config;
    hdmi_dbg(&client->dev, "info_packet_sel = 0x%.2x\n",(u32) info_packet_sel);
    // New packet?
    if ( info_packet_sel != 0x00)
    {
        // avi infoframe
        if ( info_packet_sel & ANX7150_avi_sel )
        {
            c = s_ANX7150_packet_config.avi_info.pb_u8[1];  //color space
            c &= 0x9f;
            c |= (ANX7150_RGBorYCbCr << 5);
            s_ANX7150_packet_config.avi_info.pb_u8[1] = c | 0x10;
		    switch(ANX7150_video_timing_id)	
			{
			case ANX7150_V720x480p_60Hz_4x3:
			case ANX7150_V720x480p_60Hz_16x9:
			case ANX7150_V720x576p_50Hz_4x3:
			case ANX7150_V720x576p_50Hz_16x9:
				s_ANX7150_packet_config.avi_info.pb_u8[2] = 0x58;
				break;
			case ANX7150_V1280x720p_50Hz:
			case ANX7150_V1280x720p_60Hz:
			case ANX7150_V1920x1080p_50Hz:
			case ANX7150_V1920x1080p_60Hz:
				s_ANX7150_packet_config.avi_info.pb_u8[2] = 0xa8;
				break;
			default:
				s_ANX7150_packet_config.avi_info.pb_u8[2] = 0xa8;
				break;
	     	}

            c = s_ANX7150_packet_config.avi_info.pb_u8[4];// vid ID
            c = c & 0x80;
            s_ANX7150_packet_config.avi_info.pb_u8[4] = c | ANX7150_video_timing_id;
            c = s_ANX7150_packet_config.avi_info.pb_u8[5]; //repeat times
            c = c & 0xf0;
            c |= (ANX7150_tx_pix_rpt & 0x0f);
            s_ANX7150_packet_config.avi_info.pb_u8[5] = c;
            hdmi_dbg(&client->dev, "config avi infoframe packet.\n");
            // Disable repeater
            rc = anx7150_i2c_read_p1_reg(client, ANX7150_INFO_PKTCTRL1_REG, &c);
            c &= ~ANX7150_INFO_PKTCTRL1_AVI_RPT;
			rc = anx7150_i2c_write_p1_reg(client, ANX7150_INFO_PKTCTRL1_REG, &c);

            // Enable?wait:go
            rc = anx7150_i2c_read_p1_reg(client, ANX7150_INFO_PKTCTRL1_REG, &c);
            if (c & ANX7150_INFO_PKTCTRL1_AVI_EN)
            {
                //D("wait disable, config avi infoframe packet.\n");
                return exe_result; //jack wen
            }

            // load packet data to regs
            rc = anx7150_load_infoframe(client, ANX7150_avi_infoframe,
                                    &(s_ANX7150_packet_config.avi_info));
            // Enable and repeater
            rc = anx7150_i2c_read_p1_reg(client, ANX7150_INFO_PKTCTRL1_REG, &c);
            c |= 0x30;
			rc = anx7150_i2c_write_p1_reg(client, ANX7150_INFO_PKTCTRL1_REG, &c);

            // complete avi packet
            hdmi_dbg(&client->dev, "config avi infoframe packet done.\n");
            s_ANX7150_packet_config.packets_need_config &= ~ANX7150_avi_sel;

        }

        // audio infoframe
        if ( info_packet_sel & ANX7150_audio_sel )
        {
            hdmi_dbg(&client->dev, "config audio infoframe packet.\n");

            // Disable repeater
            rc = anx7150_i2c_read_p1_reg(client, ANX7150_INFO_PKTCTRL2_REG, &c);
            c &= ~ANX7150_INFO_PKTCTRL2_AIF_RPT;
			rc = anx7150_i2c_write_p1_reg(client, ANX7150_INFO_PKTCTRL2_REG, &c);

            // Enable?wait:go
            rc = anx7150_i2c_read_p1_reg(client, ANX7150_INFO_PKTCTRL2_REG, &c);
            if (c & ANX7150_INFO_PKTCTRL2_AIF_EN)
            {
                //D("wait disable, config audio infoframe packet.\n");
                //return exe_result;//jack wen
            }
            // config packet

            // load packet data to regs
            
            anx7150_load_infoframe( client, ANX7150_audio_infoframe,
                                    &(s_ANX7150_packet_config.audio_info));
            // Enable and repeater
            rc = anx7150_i2c_read_p1_reg(client, ANX7150_INFO_PKTCTRL2_REG, &c);
            c |= 0x03;
			rc = anx7150_i2c_write_p1_reg(client, ANX7150_INFO_PKTCTRL2_REG, &c);

            // complete avi packet

            hdmi_dbg(&client->dev, "config audio infoframe packet done.\n");
            s_ANX7150_packet_config.packets_need_config &= ~ANX7150_audio_sel;

        }

        // config other 4 packets
        /*

                if( info_packet_sel & 0xfc )
                {
                    D("other packets.\n");

                    //find the current type need config
                    if(info_packet_sel & ANX7150_spd_sel)    type_sel = ANX7150_spd_sel;
                    else if(info_packet_sel & ANX7150_mpeg_sel)    type_sel = ANX7150_mpeg_sel;
                    else if(info_packet_sel & ANX7150_acp_sel)    type_sel = ANX7150_acp_sel;
                    else if(info_packet_sel & ANX7150_isrc1_sel)    type_sel = ANX7150_isrc1_sel;
                    else if(info_packet_sel & ANX7150_isrc2_sel)    type_sel = ANX7150_isrc2_sel;
                    else  type_sel = ANX7150_vendor_sel;


                    // Disable repeater
                    ANX7150_i2c_read_p1_reg(ANX7150_INFO_PKTCTRL2_REG, &c);
                    c &= ~ANX7150_INFO_PKTCTRL2_AIF_RPT;
                    ANX7150_i2c_write_p1_reg(ANX7150_INFO_PKTCTRL2_REG, c);

                    switch(type_sel)
                    {
                        case ANX7150_spd_sel:
                            ANX7150_i2c_read_p1_reg(ANX7150_INFO_PKTCTRL1_REG, &c);
                            c &= ~ANX7150_INFO_PKTCTRL1_SPD_RPT;
                            ANX7150_i2c_write_p1_reg(ANX7150_INFO_PKTCTRL1_REG, c);

                            ANX7150_i2c_read_p1_reg(ANX7150_INFO_PKTCTRL1_REG, &c);
                            if(c & ANX7150_INFO_PKTCTRL1_SPD_EN)
                            {
                                D("wait disable, config spd infoframe packet.\n");
                                return exe_result;
                            }
                            break;

                        case ANX7150_mpeg_sel:
                            ANX7150_i2c_read_p1_reg(ANX7150_INFO_PKTCTRL2_REG, &c);
                            c &= ~ANX7150_INFO_PKTCTRL2_MPEG_RPT;
                            ANX7150_i2c_write_p1_reg(ANX7150_INFO_PKTCTRL2_REG, c);

                            ANX7150_i2c_read_p1_reg(ANX7150_INFO_PKTCTRL2_REG, &c);
                            if(c & ANX7150_INFO_PKTCTRL2_MPEG_EN)
                            {
                                D("wait disable, config mpeg infoframe packet.\n");
                                return exe_result;
                            }
                            break;

                        case ANX7150_acp_sel:
                            ANX7150_i2c_read_p1_reg(ANX7150_INFO_PKTCTRL2_REG, &c);
                            c &= ~ANX7150_INFO_PKTCTRL2_UD0_RPT;
                            ANX7150_i2c_write_p1_reg(ANX7150_INFO_PKTCTRL2_REG, c);

                            ANX7150_i2c_read_p1_reg(ANX7150_INFO_PKTCTRL2_REG, &c);
                            if(c & ANX7150_INFO_PKTCTRL2_UD0_EN)
                            {
                                D("wait disable, config mpeg infoframe packet.\n");
                                return exe_result;
                            }
                            break;

                        case ANX7150_isrc1_sel:
                            ANX7150_i2c_read_p1_reg(ANX7150_INFO_PKTCTRL2_REG, &c);
                            c &= ~ANX7150_INFO_PKTCTRL2_UD0_RPT;
                            ANX7150_i2c_write_p1_reg(ANX7150_INFO_PKTCTRL2_REG, c);
                            ANX7150_i2c_read_p1_reg(ANX7150_INFO_PKTCTRL2_REG, &c);
                            if(c & ANX7150_INFO_PKTCTRL2_UD0_EN)
                            {
                                D("wait disable, config isrc1 packet.\n");
                                return exe_result;
                            }
                            break;

                        case ANX7150_isrc2_sel:
                            ANX7150_i2c_read_p1_reg(ANX7150_INFO_PKTCTRL2_REG, &c);
                            c &= ~ANX7150_INFO_PKTCTRL2_UD_RPT;
                            ANX7150_i2c_write_p1_reg(ANX7150_INFO_PKTCTRL2_REG, c);
                            ANX7150_i2c_read_p1_reg(ANX7150_INFO_PKTCTRL2_REG, &c);
                            if(c & ANX7150_INFO_PKTCTRL2_UD_EN)
                            {
                                D("wait disable, config isrc2 packet.\n");
                                return exe_result;
                            }
                            break;

                        case ANX7150_vendor_sel:
                            ANX7150_i2c_read_p1_reg(ANX7150_INFO_PKTCTRL2_REG, &c);
                            c &= ~ANX7150_INFO_PKTCTRL2_UD_RPT;
                            ANX7150_i2c_write_p1_reg(ANX7150_INFO_PKTCTRL2_REG, c);
                            ANX7150_i2c_read_p1_reg(ANX7150_INFO_PKTCTRL2_REG, &c);
                            if(c & ANX7150_INFO_PKTCTRL2_UD_EN)
                            {
                                D("wait disable, config vendor packet.\n");
                                return exe_result;
                            }
                            break;

                        default : break;
                    }


                    // config packet
                    // TODO: config packet in top level

                    // load packet data to regs
                    switch(type_sel)
                    {
                        case ANX7150_spd_sel:
                            ANX7150_Load_Infoframe( ANX7150_spd_infoframe,
                                                    &(s_ANX7150_packet_config.spd_info));
                            D("config spd done.\n");
                            ANX7150_i2c_read_p1_reg(ANX7150_INFO_PKTCTRL1_REG, &c);
                            c |= 0xc0;
                            ANX7150_i2c_write_p1_reg(ANX7150_INFO_PKTCTRL1_REG, c);
                            break;

                        case ANX7150_mpeg_sel:
                            ANX7150_Load_Infoframe( ANX7150_mpeg_infoframe,
                                                    &(s_ANX7150_packet_config.mpeg_info));
                            D("config mpeg done.\n");
                            ANX7150_i2c_read_p1_reg(ANX7150_INFO_PKTCTRL2_REG, &c);
                            c |= 0x0c;
                            ANX7150_i2c_write_p1_reg(ANX7150_INFO_PKTCTRL2_REG, c);
                            break;

                        case ANX7150_acp_sel:
                            ANX7150_Load_Packet( ANX7150_acp_packet,
                                                    &(s_ANX7150_packet_config.acp_pkt));
                            D("config acp done.\n");
                            ANX7150_i2c_read_p1_reg(ANX7150_INFO_PKTCTRL2_REG, &c);
                            c |= 0x30;
                            ANX7150_i2c_write_p1_reg(ANX7150_INFO_PKTCTRL2_REG, c);
                            break;

                        case ANX7150_isrc1_sel:
                            ANX7150_Load_Packet( ANX7150_isrc1_packet,
                                                    &(s_ANX7150_packet_config.acp_pkt));
                            D("config isrc1 done.\n");
                            ANX7150_i2c_read_p1_reg(ANX7150_INFO_PKTCTRL2_REG, &c);
                            c |= 0x30;
                            ANX7150_i2c_write_p1_reg(ANX7150_INFO_PKTCTRL2_REG, c);
                            break;

                        case ANX7150_isrc2_sel:
                            ANX7150_Load_Packet( ANX7150_isrc2_packet,
                                                    &(s_ANX7150_packet_config.acp_pkt));
                            D("config isrc2 done.\n");
                            ANX7150_i2c_read_p1_reg(ANX7150_INFO_PKTCTRL2_REG, &c);
                            c |= 0xc0;
                            ANX7150_i2c_write_p1_reg(ANX7150_INFO_PKTCTRL2_REG, c);
                            break;

                        case ANX7150_vendor_sel:
                            ANX7150_Load_Infoframe( ANX7150_vendor_infoframe,
                                                    &(s_ANX7150_packet_config.vendor_info));
                            D("config vendor done.\n");
                            ANX7150_i2c_read_p1_reg(ANX7150_INFO_PKTCTRL2_REG, &c);
                            c |= 0xc0;
                            ANX7150_i2c_write_p1_reg(ANX7150_INFO_PKTCTRL2_REG, c);
                            break;

                        default : break;
                    }

                    // Enable and repeater
                    ANX7150_i2c_read_p1_reg(ANX7150_INFO_PKTCTRL2_REG, &c);
                    c |= 0x03;
                    ANX7150_i2c_write_p1_reg(ANX7150_INFO_PKTCTRL2_REG, c);

                    // complete config packet
                    D("config other packets done.\n");
                    s_ANX7150_packet_config.packets_need_config &= ~type_sel;

                }
                */
    }


    if ( s_ANX7150_packet_config.packets_need_config  == 0x00)
    {
        hdmi_dbg(&client->dev, "config packets done\n");
        //ANX7150_Set_System_State(ANX7150_HDCP_AUTHENTICATION);
    }


    return exe_result;
}
//******************** HDCP process ********************************
static int anx7150_hardware_hdcp_auth_init(struct i2c_client *client)
{
	int rc = 0;
    u8 c;

//    ANX7150_i2c_read_p0_reg(ANX7150_SYS_CTRL1_REG, &c); //72:07.2 hdcp on
//    ANX7150_i2c_write_p0_reg(ANX7150_SYS_CTRL1_REG, (c | ANX7150_SYS_CTRL1_HDCPMODE));
	// disable hw hdcp
//    ANX7150_i2c_read_p0_reg(ANX7150_HDCP_CTRL0_REG, &c);
//    ANX7150_i2c_write_p0_reg(ANX7150_HDCP_CTRL0_REG, (c & (~ANX7150_HDCP_CTRL0_HW_AUTHEN)));

    //ANX7150_i2c_write_p0_reg(ANX7150_HDCP_CTRL0_REG, 0x03); //h/w auth off, jh simplay/hdcp
     c = 0x00;
	rc = anx7150_i2c_write_p0_reg(client, ANX7150_HDCP_CTRL0_REG, &c); //bit 0/1 off, as from start, we don't know if Bksv/srm/KSVList valid or not. SY.

    // DDC reset
    rc = anx7150_rst_ddcchannel(client);

    anx7150_initddc_read(client, 0x74, 0x00, 0x40, 0x01, 0x00);
    mdelay(5);
	rc = anx7150_i2c_read_p0_reg(client, ANX7150_DDC_FIFO_ACC_REG, &ANX7150_hdcp_bcaps);
    hdmi_dbg(&client->dev, "ANX7150_Hardware_HDCP_Auth_Init(): ANX7150_hdcp_bcaps = 0x%.2x\n",    (u32)ANX7150_hdcp_bcaps);

    if (ANX7150_hdcp_bcaps & 0x02)
    {   //enable 1.1 feature
    	hdmi_dbg(&client->dev, "ANX7150_Hardware_HDCP_Auth_Init(): bcaps supports 1.1\n");
		rc = anx7150_i2c_read_p0_reg(client, ANX7150_HDCP_CTRL1_REG, &c);
		c |= ANX7150_HDCP_CTRL1_HDCP11_EN;
		rc = anx7150_i2c_write_p0_reg(client, ANX7150_HDCP_CTRL1_REG, &c);
     }
    else
    {   //disable 1.1 feature and enable HDCP two special point check
    	hdmi_dbg(&client->dev, "bcaps don't support 1.1\n");
		rc = anx7150_i2c_read_p0_reg(client, ANX7150_HDCP_CTRL1_REG, &c);
		c = ((c & (~ANX7150_HDCP_CTRL1_HDCP11_EN)) | ANX7150_LINK_CHK_12_EN);
		rc = anx7150_i2c_write_p0_reg(client, ANX7150_HDCP_CTRL1_REG, &c);
    }
    //handle repeater bit. SY.
    if (ANX7150_hdcp_bcaps & 0x40)
    {
	         //repeater
		hdmi_dbg(&client->dev, "ANX7150_Hardware_HDCP_Auth_Init(): bcaps shows Sink is a repeater\n");
		rc = anx7150_i2c_read_p0_reg(client, ANX7150_HDCP_CTRL0_REG, &c);
		c |= ANX7150_HDCP_CTRL0_RX_REP;
		rc = anx7150_i2c_write_p0_reg(client, ANX7150_HDCP_CTRL0_REG, &c);
	}
    else
    {
			 //receiver
		hdmi_dbg(&client->dev, "ANX7150_Hardware_HDCP_Auth_Init(): bcaps shows Sink is a receiver\n");
		rc = anx7150_i2c_read_p0_reg(client, ANX7150_HDCP_CTRL0_REG, &c);
		c &= ~ANX7150_HDCP_CTRL0_RX_REP;
		rc = anx7150_i2c_write_p0_reg(client, ANX7150_HDCP_CTRL0_REG, &c);
	}
    anx7150_rst_ddcchannel(client);
    ANX7150_hdcp_auth_en = 0;

	return rc;
}
static u8 anx7150_bksv_srm(struct i2c_client *client)
{
	int rc = 0;
#if 1
    u8 bksv[5],i,bksv_one,c1;
    anx7150_initddc_read(client, 0x74, 0x00, 0x00, 0x05, 0x00);
    mdelay(15);
    for (i = 0; i < 5; i ++)
    {
    	rc = anx7150_i2c_read_p0_reg(client, ANX7150_DDC_FIFO_ACC_REG, &bksv[i]);
    }

    bksv_one = 0;
    for (i = 0; i < 8; i++)
    {
        c1 = 0x01 << i;
        if (bksv[0] & c1)
            bksv_one ++;
        if (bksv[1] & c1)
            bksv_one ++;
        if (bksv[2] & c1)
            bksv_one ++;
        if (bksv[3] & c1)
            bksv_one ++;
        if (bksv[4] & c1)
            bksv_one ++;
    }
    //wen HDCP CTS
    if (bksv_one != 20)
    {
        hdmi_dbg(&client->dev, "BKSV check fail\n");
        return 0;
    }
    else
    {
        hdmi_dbg(&client->dev, "BKSV check OK\n");
        return 1;
    }
#endif

#if 0					//wen HDCP CTS
    /*address by gerard.zhu*/
    u8 i,j,bksv_ones_count,bksv_data[Bksv_Data_Nums] = {0};
    ANX7150_DDC_Addr bksv_ddc_addr;
    u32 bksv_length;
    ANX7150_DDC_Type ddc_type;

    i = 0;
    j = 0;
    bksv_ones_count = 0;
    bksv_ddc_addr.dev_addr = HDCP_Dev_Addr;
    bksv_ddc_addr.sgmt_addr = 0;
    bksv_ddc_addr.offset_addr = HDCP_Bksv_Offset;
    bksv_length = Bksv_Data_Nums;
    ddc_type = DDC_Hdcp;

    if (!ANX7150_DDC_Read(bksv_ddc_addr, bksv_data, bksv_length, ddc_type))
    {
        /*Judge validity for Bksv*/
        while (i < Bksv_Data_Nums)
        {
            while (j < 8)
            {
                if (((bksv_data[i] >> j) & 0x01) == 1)
                {
                    bksv_ones_count++;
                }
                j++;
            }
            i++;
            j = 0;
        }
        if (bksv_ones_count != 20)
        {
            rk29printk ("!!!!BKSV 1s ¡Ù20\n");					//update  rk29printk ("!!!!BKSV 1s ¡Ù20\n");
            return 0;
        }
    }
    /*end*/

    D("bksv is ready.\n");
    // TODO: Compare the bskv[] value to the revocation list to decide if this value is a illegal BKSV. This is system depended.
    //If illegal, return 0; legal, return 1. Now just return 1
    return 1;
#endif
}

static u8 anx7150_is_ksvlist_vld(struct i2c_client *client)
{
	int rc = 0;
//wen HDCP CTS
#if 1
    hdmi_dbg(&client->dev, "ANX7150_IS_KSVList_VLD() is called.\n");
    anx7150_initddc_read(client, 0x74, 0x00, 0x41, 0x02, 0x00); //Bstatus, two u8s
    mdelay(5);
	rc = anx7150_i2c_read_p0_reg(client, ANX7150_DDC_FIFO_ACC_REG, &ANX7150_hdcp_bstatus[0]);
	rc = anx7150_i2c_read_p0_reg(client, ANX7150_DDC_FIFO_ACC_REG, &ANX7150_hdcp_bstatus[1]);

    if ((ANX7150_hdcp_bstatus[0] & 0x80) | (ANX7150_hdcp_bstatus[1] & 0x08))
    {
        hdmi_dbg(&client->dev, "Max dev/cascade exceeded: ANX7150_hdcp_bstatus[0]: 0x%x,ANX7150_hdcp_bstatus[1]:0x%x\n", (u32)ANX7150_hdcp_bstatus[0],(u32)ANX7150_hdcp_bstatus[1]);
        return 0;//HDCP topology error. More than 127 RX are attached or more than seven levels of repeater are cascaded.
    }
    return 1;
#endif
//wen HDCP CTS

}

static void anx7150_show_video_parameter(struct i2c_client *client)
{
	int rc = 0;
    // int h_res,h_act,v_res,v_act,h_fp,hsync_width,h_bp;
    char c, c1;

    hdmi_dbg(&client->dev, "\n\n**********************************ANX7150 Info**********************************\n");

    hdmi_dbg(&client->dev, "   ANX7150 mode = Normal mode\n");
    if ((ANX7150_demux_yc_en == 1) && (ANX7150_emb_sync_mode == 0))
        hdmi_dbg(&client->dev, "   Input video format = YC_MUX\n");
    if ((ANX7150_demux_yc_en == 0) && (ANX7150_emb_sync_mode == 1))
        hdmi_dbg(&client->dev, "   Input video format = 656\n");
    if ((ANX7150_demux_yc_en == 1) && (ANX7150_emb_sync_mode == 1))
        hdmi_dbg(&client->dev, "   Input video format = YC_MUX + 656\n");
    if ((ANX7150_demux_yc_en == 0) && (ANX7150_emb_sync_mode == 0))
        hdmi_dbg(&client->dev, "   Input video format = Seperate Sync\n");
    if (ANX7150_de_gen_en)
        hdmi_dbg(&client->dev, "   DE generator = Enable\n");
    else
        hdmi_dbg(&client->dev, "   DE generator = Disable\n");
    if ((ANX7150_ddr_bus_mode == 1)&& (ANX7150_emb_sync_mode == 0))
        hdmi_dbg(&client->dev, "   Input video format = DDR mode\n");
    else if ((ANX7150_ddr_bus_mode == 1)&& (ANX7150_emb_sync_mode == 1))
        hdmi_dbg(&client->dev, "   Input video format = DDR mode + 656\n");
	rc = anx7150_i2c_read_p0_reg(client, ANX7150_SYS_CTRL1_REG, &c1);
    c1 = (c1 & 0x02);
    if (c1)
    {
        hdmi_dbg(&client->dev, "   Output video mode = HDMI\n");
		rc = anx7150_i2c_read_p0_reg(client, 0x04, &c);
        c = (c & 0x60) >> 5;
        switch (c)
        {
            case ANX7150_RGB:
                hdmi_dbg(&client->dev, "   Output video color format = RGB\n");
                break;
            case ANX7150_YCbCr422:
                hdmi_dbg(&client->dev, "   Output video color format = YCbCr422\n");
                break;
            case ANX7150_YCbCr444:
                hdmi_dbg(&client->dev, "   Output video color format = YCbCr444\n");
                break;
            default:
                break;
        }
    }
    else
    {
        hdmi_dbg(&client->dev, "   Output video mode = DVI\n");
        hdmi_dbg(&client->dev, "   Output video color format = RGB\n");
    }

    /*for(i = 0x10; i < 0x25; i ++)
    {
        ANX7150_i2c_read_p0_reg(i, &c );
        D("0x%.2x = 0x%.2x\n",(unsigned int)i,(unsigned int)c);
    }*/
    /*   ANX7150_i2c_read_p0_reg(ANX7150_VID_STATUS_REG, &c);
       if((c & ANX7150_VID_STATUS_TYPE) == 0x04)
           D("Video Type = Interlace");
       else
           D("Video Type = Progressive");
       ANX7150_i2c_read_p0_reg(ANX7150_VIDF_HRESH_REG, &c);
       h_res = c;
       h_res = h_res << 8;
       ANX7150_i2c_read_p0_reg(ANX7150_VIDF_HRESL_REG, &c);
       h_res = h_res + c;
       D("H_resolution = %u\n",h_res);
       ANX7150_i2c_read_p0_reg(ANX7150_VIDF_PIXH_REG, &c);
       h_act = c;
       h_act = h_act << 8;
       ANX7150_i2c_read_p0_reg(ANX7150_VIDF_PIXL_REG, &c);
       h_act = h_act + c;
       D("H_active = %u\n",h_act);

       ANX7150_i2c_read_p0_reg(ANX7150_VIDF_VRESH_REG, &c);
       v_res = c;
       v_res = v_res << 8;
       ANX7150_i2c_read_p0_reg(ANX7150_VIDF_VRESL_REG, &c);
       v_res = v_res + c;
       D("V_resolution = %u\n",v_res);
       ANX7150_i2c_read_p0_reg(ANX7150_VIDF_ACTVIDLINEH_REG, &c);
       v_act = c;
       v_act = v_act << 8;
       ANX7150_i2c_read_p0_reg(ANX7150_VIDF_ACTVIDLINEL_REG, &c);
       v_act = v_act + c;
       D("V_active = %u\n",v_act);

       ANX7150_i2c_read_p0_reg(ANX7150_VIDF_HFORNTPORCHH_REG, &c);
       h_fp = c;
       h_fp = h_fp << 8;
       ANX7150_i2c_read_p0_reg(ANX7150_VIDF_HFORNTPORCHL_REG, &c);
       h_fp = h_fp + c;
       D("H_FP = %u\n",h_fp);

       ANX7150_i2c_read_p0_reg(ANX7150_VIDF_HBACKPORCHH_REG, &c);
       h_bp = c;
       h_bp = h_bp << 8;
       ANX7150_i2c_read_p0_reg(ANX7150_VIDF_HBACKPORCHL_REG, &c);
       h_bp = h_bp + c;
       D("H_BP = %u\n",h_bp);

       ANX7150_i2c_read_p0_reg(ANX7150_VIDF_HSYNCWIDH_REG, &c);
       hsync_width = c;
       hsync_width = hsync_width << 8;
       ANX7150_i2c_read_p0_reg(ANX7150_VIDF_HSYNCWIDL_REG, &c);
       hsync_width = hsync_width + c;
       D("Hsync_width = %u\n",hsync_width);

       ANX7150_i2c_read_p0_reg(ANX7150_VIDF_ACTLINE2VSYNC_REG, &c);
       D("Vsync_FP = %bu\n",c);

       ANX7150_i2c_read_p0_reg(ANX7150_VIDF_VSYNCTAIL2VIDLINE_REG, &c);
       D("Vsync_BP = %bu\n",c);

       ANX7150_i2c_read_p0_reg(ANX7150_VIDF_VSYNCWIDLINE_REG, &c);
       D("Vsync_width = %bu\n",c);*/
    {
        hdmi_dbg(&client->dev, "   Normal mode output video format: \n");
        switch (ANX7150_video_timing_id)
        {
            case ANX7150_V720x480p_60Hz_4x3:
            case ANX7150_V720x480p_60Hz_16x9:
                hdmi_dbg(&client->dev, "720x480p@60\n");
                if (ANX7150_edid_result.supported_720x480p_60Hz)
                    hdmi_dbg(&client->dev, "and sink supports this format.\n");
                else
                    hdmi_dbg(&client->dev, "but sink does not support this format.\n");
                break;
            case ANX7150_V1280x720p_60Hz:
                hdmi_dbg(&client->dev, "1280x720p@60\n");
                if (ANX7150_edid_result.supported_720p_60Hz)
                    hdmi_dbg(&client->dev, "and sink supports this format.\n");
                else
                    hdmi_dbg(&client->dev, "but sink does not support this format.\n");
                break;
            case ANX7150_V1920x1080i_60Hz:
                hdmi_dbg(&client->dev, "1920x1080i@60\n");
                if (ANX7150_edid_result.supported_1080i_60Hz)
                    hdmi_dbg(&client->dev, "and sink supports this format.\n");
                else
                    hdmi_dbg(&client->dev, "but sink does not support this format.\n");
                break;
            case ANX7150_V1920x1080p_60Hz:
                hdmi_dbg(&client->dev, "1920x1080p@60\n");
                if (ANX7150_edid_result.supported_1080p_60Hz)
                    hdmi_dbg(&client->dev, "and sink supports this format.\n");
                else
                    hdmi_dbg(&client->dev, "but sink does not support this format.\n");
                break;
            case ANX7150_V1920x1080p_50Hz:
                hdmi_dbg(&client->dev, "1920x1080p@50\n");
                if (ANX7150_edid_result.supported_1080p_50Hz)
                    hdmi_dbg(&client->dev, "and sink supports this format.\n");
                else
                    hdmi_dbg(&client->dev, "but sink does not support this format.\n");
                break;
            case ANX7150_V1280x720p_50Hz:
                hdmi_dbg(&client->dev, "1280x720p@50\n");
                if (ANX7150_edid_result.supported_720p_50Hz)
                    hdmi_dbg(&client->dev, "and sink supports this format.\n");
                else
                    hdmi_dbg(&client->dev, "but sink does not support this format.\n");
                break;
            case ANX7150_V1920x1080i_50Hz:
                hdmi_dbg(&client->dev, "1920x1080i@50\n");
                if (ANX7150_edid_result.supported_1080i_50Hz)
                    hdmi_dbg(&client->dev, "and sink supports this format.\n");
                else
                    hdmi_dbg(&client->dev, "but sink does not support this format.\n");
                break;
            case ANX7150_V720x576p_50Hz_4x3:
            case ANX7150_V720x576p_50Hz_16x9:
                hdmi_dbg(&client->dev, "720x576p@50\n");
                if (ANX7150_edid_result.supported_576p_50Hz)
                    hdmi_dbg(&client->dev, "and sink supports this format.\n");
                else
                    hdmi_dbg(&client->dev, "but sink does not support this format.\n");
                break;
            case ANX7150_V720x576i_50Hz_4x3:
            case ANX7150_V720x576i_50Hz_16x9:
                hdmi_dbg(&client->dev, "720x576i@50\n");
                if (ANX7150_edid_result.supported_576i_50Hz)
                    hdmi_dbg(&client->dev, "and sink supports this format.\n");
                else
                    hdmi_dbg(&client->dev, "but sink does not support this format.\n");
                break;
            case ANX7150_V720x480i_60Hz_4x3:
            case ANX7150_V720x480i_60Hz_16x9:
                hdmi_dbg(&client->dev, "720x480i@60\n");
                if (ANX7150_edid_result.supported_720x480i_60Hz)
                    hdmi_dbg(&client->dev, "and sink supports this format.\n");
                else
                    hdmi_dbg(&client->dev, "but sink does not support this format.\n");
                break;
            default:
                hdmi_dbg(&client->dev, "unknown(video ID is: %.2x).\n",(u32)ANX7150_video_timing_id);
                break;
        }
    }
    if (c1)//HDMI output
    {
    	rc = anx7150_i2c_read_p0_reg(client, ANX7150_HDMI_AUDCTRL0_REG, &c);
        c = c & 0x03;
        hdmi_dbg(&client->dev, "   MCLK Frequence = ");

        switch (c)
        {
            case 0x00:
                hdmi_dbg(&client->dev, "128 * Fs.\n");
                break;
            case 0x01:
                hdmi_dbg(&client->dev, "256 * Fs.\n");
                break;
            case 0x02:
                hdmi_dbg(&client->dev, "384 * Fs.\n");
                break;
            case 0x03:
                hdmi_dbg(&client->dev, "512 * Fs.\n");
                break;
            default :
                hdmi_dbg(&client->dev, "Wrong MCLK output.\n");
                break;
        }

        if ( ANX7150_AUD_HW_INTERFACE == 0x01)
        {
            hdmi_dbg(&client->dev, "   Input Audio Interface = I2S.\n");
			rc = anx7150_i2c_read_p0_reg(client, ANX7150_I2SCH_STATUS4_REG, &c);
        }
        else if (ANX7150_AUD_HW_INTERFACE == 0x02)
        {
            hdmi_dbg(&client->dev, "   Input Audio Interface = SPDIF.\n");
			rc = anx7150_i2c_read_p0_reg(client, ANX7150_SPDIFCH_STATUS_REG, &c);
            c=c>>4;
        }
		rc = anx7150_i2c_read_p0_reg(client, ANX7150_I2SCH_STATUS4_REG, &c);
        hdmi_dbg(&client->dev, "   Audio Fs = ");
        c &= 0x0f;
        switch (c)
        {
            case 0x00:
                hdmi_dbg(&client->dev, "   Audio Fs = 44.1 KHz.\n");
                break;
            case 0x02:
				hdmi_dbg(&client->dev, "   Audio Fs = 48 KHz.\n");
                break;
            case 0x03:
				hdmi_dbg(&client->dev, "   Audio Fs = 32 KHz.\n");
                break;
            case 0x08:
				hdmi_dbg(&client->dev, "   Audio Fs = 88.2 KHz.\n");
                break;
            case 0x0a:
				hdmi_dbg(&client->dev, "   Audio Fs = 96 KHz.\n\n");
                break;
            case 0x0c:
				hdmi_dbg(&client->dev, "   Audio Fs = 176.4 KHz.\n");
                break;
            case 0x0e:
				hdmi_dbg(&client->dev, "   Audio Fs = 192 KHz.\n");
                hdmi_dbg(&client->dev, "192 KHz.\n");
                break;
            default :
				hdmi_dbg(&client->dev, "   Audio Fs = Wrong Fs output.\n");
                hdmi_dbg(&client->dev, "Wrong Fs output.\n");
                break;
        }

        if	(ANX7150_HDCP_enable == 1)
            hdmi_dbg(&client->dev, "   ANX7150_HDCP_Enable.\n");
        else
            hdmi_dbg(&client->dev, "   ANX7150_HDCP_Disable.\n");

    }
    hdmi_dbg(&client->dev, "\n********************************************************************************\n\n");
}
void ANX7150_HDCP_Process(struct i2c_client *client, int enable)
{
	int rc = 0;
    char c,i;
	//u8 c1;
    u8 Bksv_valid=0;//wen HDCP CTS

    if (ANX7150_HDCP_enable)
    { //HDCP_EN =1 means to do HDCP authentication,SWITCH4 = 0 means not to do HDCP authentication.

        //ANX7150_i2c_read_p0_reg(ANX7150_SYS_CTRL1_REG, &c);
        //ANX7150_i2c_write_p0_reg(ANX7150_SYS_CTRL1_REG, c | 0x04);//power on HDCP, 090630

        //ANX7150_i2c_read_p0_reg(ANX7150_INTR2_MASK_REG, &c);
        //ANX7150_i2c_write_p0_reg(ANX7150_INTR2_MASK_REG, c |0x03);
        mdelay(10);//let unencrypted video play a while, required by HDCP CTS. SY//wen HDCP CTS
        anx7150_set_avmute(client);//before auth, set_avmute//wen
        mdelay(10);//wen HDCP CTS

        if ( !ANX7150_hdcp_init_done )
        {
        	rc = anx7150_i2c_read_p0_reg(client, ANX7150_SYS_CTRL1_REG, &c);
            c |= ANX7150_SYS_CTRL1_HDCPMODE;
			rc = anx7150_i2c_write_p0_reg(client, ANX7150_SYS_CTRL1_REG, &c);
            if (ANX7150_edid_result.is_HDMI)
                rc = anx7150_hardware_hdcp_auth_init(client);
            else
            {   //DVI, disable 1.1 feature and enable HDCP two special point check
            	rc = anx7150_i2c_read_p0_reg(client, ANX7150_HDCP_CTRL1_REG, &c);
            	c = ((c & (~ANX7150_HDCP_CTRL1_HDCP11_EN)) | ANX7150_LINK_CHK_12_EN);
				rc = anx7150_i2c_write_p0_reg(client, ANX7150_HDCP_CTRL1_REG, &c);
            }

            //wen HDCP CTS
            if (!anx7150_bksv_srm(client))
            {
                anx7150_blue_screen_enable(client);
                anx7150_clear_avmute(client);
                Bksv_valid=0;
                return;
            }
            else //SY.
            {
                Bksv_valid=1;
				rc = anx7150_i2c_read_p0_reg(client, ANX7150_HDCP_CTRL0_REG, &c);
            	c |= 0x03;
				rc = anx7150_i2c_write_p0_reg(client, ANX7150_HDCP_CTRL0_REG, &c);
            }

            ANX7150_hdcp_init_done = 1;
//wen HDCP CTS
        }


//wen HDCP CTS
        if ((Bksv_valid) && (!ANX7150_hdcp_auth_en))
        {
            hdmi_dbg(&client->dev, "enable hw hdcp\n");
            anx7150_rst_ddcchannel(client);
			rc = anx7150_i2c_read_p0_reg(client, ANX7150_HDCP_CTRL0_REG, &c);
            c |= ANX7150_HDCP_CTRL0_HW_AUTHEN;
			rc = anx7150_i2c_write_p0_reg(client, ANX7150_HDCP_CTRL0_REG, &c);
             ANX7150_hdcp_auth_en = 1;
        }

        if ((Bksv_valid) && (ANX7150_hdcp_wait_100ms_needed))
        {
            ANX7150_hdcp_wait_100ms_needed = 0;
            //disable audio

			rc = anx7150_i2c_read_p0_reg(client, ANX7150_HDMI_AUDCTRL1_REG, &c);
            c &= ~ANX7150_HDMI_AUDCTRL1_IN_EN;
			rc = anx7150_i2c_write_p0_reg(client, ANX7150_HDMI_AUDCTRL1_REG, &c);
			
            hdmi_dbg(&client->dev, "++++++++ANX7150_hdcp_wait_100ms_needed+++++++++\n");
            mdelay(150);    //  100 -> 150
            return;
        }
//wen HDCP CTS

        if (ANX7150_hdcp_auth_pass) 			//wen HDCP CTS
        {
            //Clear the SRM_Check_Pass u8, then when reauthentication occurs, firmware can catch it.
			rc = anx7150_i2c_read_p0_reg(client, ANX7150_HDCP_CTRL0_REG, &c);
            c &= 0xfc;
			rc = anx7150_i2c_write_p0_reg(client, ANX7150_HDCP_CTRL0_REG, &c);

            //Enable HDCP Hardware encryption
            if (!ANX7150_hdcp_encryption)
            {
                anx7150_hdcp_encryption_enable(client);
            }
            if (ANX7150_send_blue_screen)
            {
                anx7150_blue_screen_disable(client);
            }
            if (ANX7150_avmute_enable)
            {
                anx7150_clear_avmute(client);
            }

            i = 0;
			rc = anx7150_i2c_read_p0_reg(client, ANX7150_HDCP_STATUS_REG, &c);
			while((c&0x04)==0x00)//wait for encryption.
			{
                mdelay(2);
				rc = anx7150_i2c_read_p0_reg(client, ANX7150_HDCP_STATUS_REG, &c);
                i++;
                if (i > 10)
                    break;
			}

            //enable audio SY.
            rc = anx7150_i2c_read_p0_reg(client, ANX7150_HDMI_AUDCTRL1_REG, &c);
            c |= ANX7150_HDMI_AUDCTRL1_IN_EN;
			rc = anx7150_i2c_write_p0_reg(client, ANX7150_HDMI_AUDCTRL1_REG, &c);
            hdmi_dbg(&client->dev, "@@@@@  HDCP Auth PASSED!   @@@@@\n");

            if (ANX7150_hdcp_bcaps & 0x40) //repeater
            {
                hdmi_dbg(&client->dev, "Find a repeater!\n");
                //actually it is KSVList check. we can't do SRM check due to the lack of SRM file. SY.
                if (!ANX7150_srm_checked)
                {
                    if (!anx7150_is_ksvlist_vld(client))
                    {
                        hdmi_dbg(&client->dev, "ksvlist not good. disable encryption");
                        anx7150_hdcp_encryption_disable(client);
                        anx7150_blue_screen_enable(client);
                        anx7150_clear_avmute(client);
                        ANX7150_ksv_srm_pass = 0;
                        anx7150_clean_hdcp(client);//SY.
                        //remove below will pass 1b-05/1b-06
                        //ANX7150_Set_System_State(ANX7150_WAIT_HOTPLUG);//SY.
                        return;
                    }
                    ANX7150_srm_checked=1;
                    ANX7150_ksv_srm_pass = 1;
                }
            }
            else
            {
                hdmi_dbg(&client->dev, "Find a receiver.\n");
            }
        }
        else 							//wen HDCP CTS
        {
            hdmi_dbg(&client->dev, "#####   HDCP Auth FAILED!   #####\n");
            //also need to disable HW AUTHEN
            rc = anx7150_i2c_read_p0_reg(client, ANX7150_HDCP_CTRL0_REG, &c);
            c &= ~ANX7150_HDCP_CTRL0_HW_AUTHEN;
			rc = anx7150_i2c_write_p0_reg(client, ANX7150_HDCP_CTRL0_REG, &c);
			ANX7150_hdcp_auth_en = 0;
			//ANX7150_hdcp_init_done = 0;
			//ANX7150_hdcp_wait_100ms_needed = 1; //wen, update 080703

            if (ANX7150_hdcp_encryption)
            {
                anx7150_hdcp_encryption_disable(client);
            }
            if (!ANX7150_send_blue_screen)
            {
                anx7150_blue_screen_enable(client);
            }
            if (ANX7150_avmute_enable)
            {
                anx7150_clear_avmute(client);
            }
            //disable audio
            rc = anx7150_i2c_read_p0_reg(client, ANX7150_HDMI_AUDCTRL1_REG, &c);
            c &= ~ANX7150_HDMI_AUDCTRL1_IN_EN;
			rc = anx7150_i2c_write_p0_reg(client, ANX7150_HDMI_AUDCTRL1_REG, &c);
			
            return;
        }

    }
    else				//wen HDCP CTS
    {
        hdmi_dbg(&client->dev, "hdcp pin is off.\n");
        if (ANX7150_send_blue_screen)
        {
            anx7150_blue_screen_disable(client);
        }
        if (ANX7150_avmute_enable)
        {
            anx7150_clear_avmute(client);
        }
        //enable audio SY.
        rc = anx7150_i2c_read_p0_reg(client, ANX7150_HDMI_AUDCTRL1_REG, &c);
        c |= ANX7150_HDMI_AUDCTRL1_IN_EN;
		rc = anx7150_i2c_write_p0_reg(client, ANX7150_HDMI_AUDCTRL1_REG, &c);
    }

//wen HDCP CTS
	rc = anx7150_i2c_read_p0_reg(client, ANX7150_SYS_CTRL1_REG, &c); //72:07.1 hdmi or dvi mode
    c = c & 0x02;
    if (c == 0x02)
    {
        hdmi_dbg(&client->dev, "end of ANX7150_HDCP_Process(): in HDMI mode.\n");
    }
    else
    {
        hdmi_dbg(&client->dev, "!end of ANX7150_HDCP_Process(): in DVI mode.\n");
        //To-Do: Config to DVI mode.
    }

    anx7150_show_video_parameter(client);
	if(!enable)
				anx7150_set_avmute(client);
}

void  HDMI_Set_Video_Format(u8 video_format) //CPU set the lowpower mode
{	
    switch (video_format)
    {
        case HDMI_1280x720p_50Hz:
            g_video_format = ANX7150_V1280x720p_50Hz;
            break;
		case HDMI_1280x720p_60Hz:
			g_video_format = ANX7150_V1280x720p_60Hz;
			break;
		case HDMI_720x576p_50Hz_4x3:
			g_video_format = ANX7150_V720x576p_50Hz_4x3;
			break;
		case HDMI_720x576p_50Hz_16x9:
			g_video_format = ANX7150_V720x576p_50Hz_16x9;
			break;
		case HDMI_720x480p_60Hz_4x3:
			g_video_format = ANX7150_V720x480p_60Hz_4x3;
			break;
		case HDMI_720x480p_60Hz_16x9:
			g_video_format = ANX7150_V720x480p_60Hz_16x9;
			break;
		case HDMI_1920x1080p_50Hz:
			g_video_format = ANX7150_V1920x1080p_50Hz;
			break;
		case HDMI_1920x1080p_60Hz:
			g_video_format = ANX7150_V1920x1080p_60Hz;
			break;
        default:
            g_video_format = ANX7150_V1280x720p_50Hz;
            break;
    }
//    ANX7150_system_config_done = 0;
}
void  HDMI_Set_Audio_Fs( u8 audio_fs) //ANX7150 call this to check lowpower
{
    g_audio_format = audio_fs;
//    ANX7150_system_config_done = 0;
}
int ANX7150_PLAYBACK_Process(void)
{
//	D("enter\n");

    if ((s_ANX7150_packet_config.packets_need_config != 0x00) && (ANX7150_edid_result.is_HDMI == 1))
    {
        return 1;
    }

	return 0;
}


