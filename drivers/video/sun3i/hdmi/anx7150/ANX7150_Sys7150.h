#ifndef _ANX7150_Sys_H
#define _ANX7150_Sys_H

#include "../hdmi_hal.h"

typedef unsigned char   uint8;
typedef unsigned long   WORD;


extern uint8 timer_slot,misc_reset_needed;
extern uint8 bist_switch_value_pc,switch_value;
extern uint8 switch_value_sw_backup,switch_value_pc_backup;
extern uint8 ANX7150_system_state;
extern uint8 ANX7150_srm_checked;
extern uint8 ANX7150_HDCP_enable;
extern uint8 ANX7150_INT_Done;
extern uint8 FREQ_MCLK;
extern uint8 HPD_FLAG;




typedef struct
{
    uint8 is_HDMI;
    uint8 ycbcr444_supported;
    uint8 ycbcr422_supported;
    uint8 supported_1080p_60Hz;
    uint8 supported_1080p_50Hz;
    uint8 supported_1080p_24Hz;
    uint8 supported_1080i_60Hz;
    uint8 supported_1080i_50Hz;
    uint8 supported_720p_60Hz;
    uint8 supported_720p_50Hz;
    uint8 supported_576p_50Hz;
    uint8 supported_576i_50Hz;
    uint8 supported_640x480p_60Hz;
    uint8 supported_720x480p_60Hz;
    uint8 supported_720x480i_60Hz;
    uint8 AudioFormat[10];//MAX audio STD block is 10(0x1f / 3)
    uint8 AudioChannel[10];
    uint8 AudioFs[10];
    uint8 AudioLength[10];
    uint8 SpeakerFormat;uint8 edid_errcode;}ANX7150_edid_result_4_system;
    extern ANX7150_edid_result_4_system ANX7150_edid_result;
//#define ITU656
//#ifdef ITU656
struct ANX7150_video_timingtype{ //CEA-861C format
    uint8 ANX7150_640x480p_60Hz[18];//format 1
    uint8 ANX7150_720x480p_60Hz[18];//format 2 & 3
    uint8 ANX7150_1280x720p_60Hz[18];//format 4
    uint8 ANX7150_1920x1080i_60Hz[18];//format 5
    uint8 ANX7150_720x480i_60Hz[18];//format 6 & 7
    //uint8 ANX7150_720x240p_60Hz[18];//format 8 & 9
    //uint8 ANX7150_2880x480i_60Hz[18];//format 10 & 11
    //uint8 ANX7150_2880x240p_60Hz[18];//format 12 & 13
    //uint8 ANX7150_1440x480p_60Hz[18];//format 14 & 15
    //uint8 ANX7150_1920x1080p_60Hz[18];//format 16
    uint8 ANX7150_720x576p_50Hz[18];//format 17 & 18
    uint8 ANX7150_1280x720p_50Hz[18];//format 19
    uint8 ANX7150_1920x1080i_50Hz[18];//format 20*/
    uint8 ANX7150_720x576i_50Hz[18];//format 21 & 22
    /* uint8 ANX7150_720x288p_50Hz[18];//formats 23 & 24
    uint8 ANX7150_2880x576i_50Hz[18];//formats 25 & 26
    uint8 ANX7150_2880x288p_50Hz[18];//formats 27 & 28
    uint8 ANX7150_1440x576p_50Hz[18];//formats 29 & 30
    uint8 ANX7150_1920x1080p_50Hz[18];//format 31
    uint8 ANX7150_1920x1080p_24Hz[18];//format 32
    uint8 ANX7150_1920x1080p_25Hz[18];//format 33
    uint8 ANX7150_1920x1080p_30Hz[18];//format 34*/
};
//#endif
// 8 type of packets are legal, It is possible to sent 6 types in the same time;
// So select 6 types below at most;
// avi_infoframe and audio_infoframe have fixxed address;
// config other selected types of packet to the rest 4 address with no limits.
typedef enum
{
    ANX7150_avi_infoframe,
    ANX7150_audio_infoframe
    /*ANX7150_spd_infoframe,
    ANX7150_mpeg_infoframe,
    ANX7150_acp_packet,
    ANX7150_isrc1_packet,
    ANX7150_isrc2_packet,
    ANX7150_vendor_infoframe,*/
}packet_type;

typedef struct
{
    uint8 type;
    uint8 version;
    uint8 length;
    uint8 pb_uint8[28];
}infoframe_struct;

typedef struct
{
    uint8 packets_need_config;    //which infoframe packet is need updated
    infoframe_struct avi_info;
    infoframe_struct audio_info;
    /*  for the funture use
    infoframe_struct spd_info;
    infoframe_struct mpeg_info;
    infoframe_struct acp_pkt;
    infoframe_struct isrc1_pkt;
    infoframe_struct isrc2_pkt;
    infoframe_struct vendor_info; */

} config_packets;
/*
    uint8 i2s_format;

    uint8(s)	Name	Type	Default 	Description
    7	EXT_VUCP	R/W	        0x0
            Enable indicator of VUCP uint8s extraction from input
            I2S audio stream. 0 = disable; 1 = enable.
    6:5	MCLK_PHS_CTRL	R/W	    0x0
            MCLK phase control for audio SPDIF input, which value
            is depended on the value of MCLK frequency set and not great than it.
    4	Reserved
    3	SHIFT_CTRL 	R/W 	0x0
            WS to SD shift first uint8. 0 = fist uint8 shift (Philips Spec); 1 = no shift.
    2	DIR_CTRL	R/W	    0x0
            SD data Indian (MSB or LSB first) control. 0 = MSB first; 1 = LSB first.
    1	WS_POL	    R/W 	0x0
            Word select left/right polarity select. 0 = left polarity
            when works select is low; 1 = left polarity when word select is high.
    0	JUST_CTRL	R/W 	0x0
            SD Justification control. 1 = data is right justified;
            0 = data is left justified.

*/
/*
    uint8 audio_channel
uint8(s)	Name	Type Default 	Description
5	AUD_SD3_IN	R/W	0x0	Set I2S input channel #3 enable. 0 = disable; 1 = enable.
4	AUD_SD2_IN	R/W	0x0	Set I2S input channel #2 enable. 0 = disable; 1 = enable.
3	AUD_SD1_IN	R/W	0x0	Set I2S input channel #1 enable. 0 = disable; 1 = enable.
2	AUD_SD0_IN	R/W	0x0	Set I2S input channel #0 enable. 0 = disable; 1 = enable.


*/
/*
    uint8 i2s_map0
uint8(s)	Name	Type	Default 	Description
7:6	FIFO3_SEL	R/W	0x3	I2S Channel data stream select for audio FIFO 3. 0 = SD 0; 1 = SD 1; 2 = SD 2; 3 = SD 3;
5:4	FIFO2_SEL	R/W	0x2	I2S Channel data stream select for audio FIFO 2. 0 = SD 0; 1 = SD 1; 2 = SD 2; 3 = SD 3;
3:2	FIFO1_SEL	R/W	0x1	I2S Channel data stream select for audio FIFO 1. 0 = SD 0; 1 = SD 1; 2 = SD 2; 3 = SD 3;
1:0	FIFO0_SEL	R/W	0x0	I2S Channel data stream select for audio FIFO 0. 0 = SD 0; 1 = SD 1; 2 = SD 2; 3 = SD 3;

    uint8 i2s_map1
uint8(s)	Name	Type	Default 	Description
7	SW3	R/W	0x0	Swap left/right channel on I2S channel 3. 1 = swap; 0 = no swap.
6	SW2	R/W	0x0	Swap left/right channel on I2S channel 2. 1 = swap; 0 = no swap.
5	SW1	R/W	0x0	Swap left/right channel on I2S channel 1. 1 = swap; 0 = no swap.
4	SW0	R/W	0x0	Swap left/right channel on I2S channel 0. 1 = swap; 0 = no swap.
3:1	IN_WORD_LEN	R/W	0x5	Input I2S audio word length (corresponding to channel status uint8s [35:33]).  When IN_WORD_MAX = 0, 001 = 16 uint8s; 010 = 18 uint8s; 100 = 19 uint8s; 101 = 20 uint8s; 110 = 17 uint8s; when IN_WORD_MAX = 1, 001 = 20 uint8s; 010 = 22 uint8s; 100 = 23 uint8s; 101 = 24 uint8s; 110 = 21 uint8s.
0	IN_WORD_MAX	R/W	0x1	Input I2S audio word length Max (corresponding to channel status uint8s 32). 0 = maximal word length is 20 uint8s; 1 = maximal word length is 24 uint8s.
*/
/*
    uint8 Channel_status1
uint8(s)	Name	Type	Default 	Description
7:6	MODE	R/W	0x0	00 = PCM Audio
5:3	PCM_MODE	R/W	0x0	000 = 2 audio channels without pre-emphasis;
                        001 = 2 audio channels with 50/15 usec pre-emphasis
2	SW_CPRGT	R/W	0x0	0 = software for which copyright is asserted;
                        1 = software for which no copyright is asserted
1	NON_PCM	R/W	0x0	0 = audio sample word represents linear PCM samples;
                    1 = audio sample word used for other purposes.
0	PROF_APP	R/W	0x0	0 = consumer applications; 1 = professional applications.

    uint8 Channel_status2
uint8(s)	Name	Type	Default 	Description
7:0	CAT_CODE	R/W	0x0	Category code (corresponding to channel status uint8s [15:8])

    uint8 Channel_status3
uint8(s)	Name	Type	Default 	Description
7:4	CH_NUM	R/W	0x0	Channel number (corresponding to channel status uint8s [23:20])
3:0	SOURCE_NUM	R/W	0x0	Source number (corresponding to channel status uint8s [19:16])

    uint8 Channel_status4
uint8(s)	Name	Type	Default 	Description
7:6	CHNL_uint81	R/W	0x0	corresponding to channels status uint8s [31:30]
5:4	CLK_ACCUR	R/W	0x0	Clock accuracy (corresponding to channels status uint8s [29:28]). These two uint8s define the sampling frequency tolerance. The uint8s are set in the transmitter.
3:0	FS_FREQ	R/W	0x0	Sampling clock frequency (corresponding to channel status uint8s [27:24]). 0000 = 44.1 KHz; 0010 = 48 KHz; 0011 = 32 KHz; 1000 = 88.2 KHz; 1010 = 96 KHz; 176.4 KHz; 1110 = 192 KHz; others = reserved.

    uint8 Channel_status5
uint8(s)	Name	Type	Default 	Description
7:4	CHNL_uint82	R/W	0x0	corresponding to channels status uint8s [39:36]
3:1	WORD_LENGTH	R/W	0x5	Audio word length (corresponding to channel status uint8s [35:33]).  When WORD_MAX = 0, 001 = 16 uint8s; 010 = 18 uint8s; 100 = 19 uint8s; 101 = 20 uint8s; 110 = 17 uint8s; when WORD_MAX = 1, 001 = 20 uint8s; 010 = 22 uint8s; 100 = 23 uint8s; 101 = 24 uint8s; 110 = 21 uint8s.
0	WORD_MAX	R/W	0x1	Audio word length Max (corresponding to channel status uint8s 32). 0 = maximal word length is 20 uint8s; 1 = maximal word length is 24 uint8s.

*/
typedef struct
{
    uint8 audio_channel;
    uint8 i2s_format;
    uint8 i2s_swap;
    uint8 Channel_status1;
    uint8 Channel_status2;
    uint8 Channel_status3;
    uint8 Channel_status4;
    uint8 Channel_status5;
} i2s_config_struct;
/*
    uint8 FS_FREQ;

    7:4	FS_FREQ	R	0x0
        Sampling clock frequency (corresponding to channel status uint8s [27:24]).
        0000 = 44.1 KHz; 0010 = 48 KHz; 0011 = 32 KHz; 1000 = 88.2 KHz; 1010 = 96 KHz;
        176.4 KHz; 1110 = 192 KHz; others = reserved.
*/

typedef struct
{
    uint8 one_uint8_ctrl;

} super_audio_config_struct;

typedef struct
{
    uint8 audio_type;            // audio type
                                // #define ANX7150_i2s_input 0x01
                                // #define ANX7150_spdif_input 0x02
                                // #define ANX7150_super_audio_input 0x04

    uint8 down_sample;     // 0x72:0x50
                                // 0x00:    00  no down sample
                                // 0x20:    01  2 to 1 down sample
                                // 0x60:    11  4 to 1 down sample
                                // 0x40:    10  reserved
     uint8 audio_layout;//audio layout;
     								//0x00, 2-channel
     								//0x80, 8-channel

    i2s_config_struct i2s_config;
    super_audio_config_struct super_audio_config;

} audio_config_struct;

/*added by gerard.zhu*/
/*DDC type*/
typedef enum {
    DDC_Hdcp,
    DDC_Edid
}ANX7150_DDC_Type;

/*Read DDC status type*/
typedef enum {
    report,
    Judge
}ANX7150_DDC_Status_Check_Type;

/*Define DDC address struction*/
typedef struct {
    uint8 dev_addr;
    uint8 sgmt_addr;
    uint8 offset_addr;
}ANX7150_DDC_Addr;

/*DDC status uint8*/
#define DDC_Error_uint8   0x07
#define DDC_Occup_uint8  0x06
#define DDC_Fifo_Full_uint8  0x05
#define DDC_Fifo_Empt_uint8  0x04
#define DDC_No_Ack_uint8 0x03
#define DDC_Fifo_Rd_uint8    0x02
#define DDC_Fifo_Wr_uint8    0x01
#define DDC_Progress_uint8   0x00

#define YCbCr422 0x20
#define null 0
#define source_ratio 0x08

/*DDC Command*/
#define Abort_Current_Operation 0x00
#define Sequential_uint8_Read 0x01
#define Sequential_uint8_Write 0x02
#define Implicit_Offset_Address_Read 0x3
#define Enhanced_DDC_Sequenital_Read 0x04
#define Clear_DDC_Fifo 0x05
#define I2c_reset 0x06

/*DDC result*/
#define DDC_NO_Err 0x00
#define DDC_Status_Err 0x01
#define DDC_Data_Addr_Err 0x02
#define DDC_Length_Err  0x03

/*checksum result*/
#define Edid_Checksum_No_Err     0x00
#define Edid_Checksum_Err   0x01

/*HDCP device base address*/
#define HDCP_Dev_Addr   0x74

/*HDCP Bksv offset*/
#define HDCP_Bksv_Offset 0x00

/*HDCP Bcaps offset*/
#define HDCP_Bcaps_Offset   0x40

/*HDCP Bstatus offset*/
#define HDCP_Bstatus_offset     0x41

/*HDCP KSV Fifo offset */
#define HDCP_Ksv_Fifo_Offset    0x43

/*HDCP bksv data nums*/
#define Bksv_Data_Nums  5

/*HDCP ksvs data number by defult*/
#define ksvs_data_nums 50

/*DDC Max uint8s*/
#define DDC_Max_Length 1024

/*DDC fifo depth*/
#define DDC_Fifo_Depth  16

/*DDC read delay ms*/
#define DDC_Read_Delay 3

/*DDC Write delay ms*/
#define DDC_Write_Delay 3
/*end*/

extern uint8 ANX7150_parse_edid_done;
extern uint8 ANX7150_system_config_done;
extern uint8 ANX7150_video_format_config,ANX7150_video_timing_id;
extern uint8 ANX7150_new_csc,ANX7150_new_vid_id,ANX7150_new_HW_interface;
extern uint8 ANX7150_ddr_edge;
extern uint8 ANX7150_in_pix_rpt_bkp,ANX7150_tx_pix_rpt_bkp;
extern uint8 ANX7150_in_pix_rpt,ANX7150_tx_pix_rpt;
extern uint8 ANX7150_pix_rpt_set_by_sys;
extern uint8 ANX7150_RGBorYCbCr;
extern audio_config_struct s_ANX7150_audio_config;
extern config_packets s_ANX7150_packet_config;

//********************** BIST Enable***********************************


#define ddr_falling_edge 1
#define ddr_rising_edge 0

#define input_pixel_clk_1x_repeatition 0x00
#define input_pixel_clk_2x_repeatition 0x01
#define input_pixel_clk_4x_repeatition 0x03

//***********************Video Config***********************************
#define ANX7150_RGB_YCrCb444_SepSync 0
#define ANX7150_YCrCb422_SepSync 1
#define ANX7150_YCrCb422_EmbSync 2
#define ANX7150_YCMux422_SepSync_Mode1 3
#define ANX7150_YCMux422_SepSync_Mode2 4
#define ANX7150_YCMux422_EmbSync_Mode1 5
#define ANX7150_YCMux422_EmbSync_Mode2 6
#define ANX7150_RGB_YCrCb444_DDR_SepSync 7
#define ANX7150_RGB_YCrCb444_DDR_EmbSync 8

#define ANX7150_RGB_YCrCb444_SepSync_No_DE 9
#define ANX7150_YCrCb422_SepSync_No_DE 10

#define ANX7150_Progressive 0
#define ANX7150_Interlace 0x08
#define ANX7150_Neg_Hsync_pol 0x20
#define ANX7150_Pos_Hsync_pol 0
#define ANX7150_Neg_Vsync_pol 0x40
#define ANX7150_Pos_Vsync_pol 0

#define ANX7150_V640x480p_60Hz 1
#define ANX7150_V720x480p_60Hz_4x3 2
#define ANX7150_V720x480p_60Hz_16x9 3
#define ANX7150_V1280x720p_60Hz 4
#define ANX7150_V1280x720p_50Hz 19
#define ANX7150_V1920x1080i_60Hz 5
#define ANX7150_V1920x1080p_60Hz 16
#define ANX7150_V1920x1080p_50Hz 31
#define ANX7150_V1920x1080p_24Hz 32
#define ANX7150_V1920x1080p_25Hz 33
#define ANX7150_V1920x1080p_30Hz 34
#define ANX7150_V1920x1080i_50Hz 20
#define ANX7150_V720x480i_60Hz_4x3 6
#define ANX7150_V720x480i_60Hz_16x9 7
#define ANX7150_V720x576i_50Hz_4x3 21
#define ANX7150_V720x576i_50Hz_16x9 22
#define ANX7150_V720x576p_50Hz_4x3 17
#define ANX7150_V720x576p_50Hz_16x9 18

#define ANX7150_RGB 0x00
#define ANX7150_YCbCr422 0x01
#define ANX7150_YCbCr444 0x02
#define ANX7150_CSC_BT709 1
#define ANX7150_CSC_BT601 0

#define ANX7150_EMBEDED_BLUE_SCREEN_ENABLE 1
#define ANX7150_HDCP_FAIL_THRESHOLD 10

#define ANX7150_avi_sel 0x01
#define ANX7150_audio_sel 0x02
#define ANX7150_spd_sel 0x04
#define ANX7150_mpeg_sel 0x08
#define ANX7150_acp_sel 0x10
#define ANX7150_isrc1_sel 0x20
#define ANX7150_isrc2_sel 0x40
#define ANX7150_vendor_sel 0x80

// audio type
#define ANX7150_i2s_input 0x01
#define ANX7150_spdif_input 0x02
#define ANX7150_super_audio_input 0x04
// freq_mclk
#define ANX7150_mclk_128_Fs 0x00
#define ANX7150_mclk_256_Fs 0x01
#define ANX7150_mclk_384_Fs 0x02
#define ANX7150_mclk_512_Fs 0x03
// thresholds
#define ANX7150_spdif_stable_th 0x03
// fs -> N(ACR)
#define ANX7150_N_32k 0x1000
#define ANX7150_N_44k 0x1880
#define ANX7150_N_88k 0x3100
#define ANX7150_N_176k 0x6200
#define ANX7150_N_48k 0x1800
#define ANX7150_N_96k 0x3000
#define ANX7150_N_192k 0x6000

#define ANX7150_INITIAL 		0x01
#define ANX7150_WAIT_HOTPLUG 		0x02
#define ANX7150_WAIT_RX_SENSE 		0x03
#define ANX7150_READ_PARSE_EDID 	0x04
#define ANX7150_CONFIG_VIDEO 		0x05
#define ANX7150_CONFIG_AUDIO 		0x06
#define ANX7150_CONFIG_PACKETS 		0x07
#define ANX7150_HDCP_AUTHENTICATION 	0x08
#define ANX7150_PLAY_BACK 		0x09
#define ANX7150_RESET_LINK 		0x0a
#define ANX7150_UNKNOWN 		0x0b

#define spdif_error_th 0x0a

#define Hresolution_1920 1920
#define Vresolution_540 540
#define Vresolution_1080 1080
#define Hresolution_1280 1280
#define Vresolution_720 720
#define Hresolution_640 640
#define Vresolution_480 480
#define Hresolution_720 720
#define Vresolution_240 240
#define Vresolution_576 576
#define Vresolution_288 288
#define Hz_50 50
#define Hz_60 60
#define Interlace_EDID 0
#define Progressive_EDID 1
#define ratio_16_9 1.777778
#define ratio_4_3 1.333333

#define ANX7150_EDID_BadHeader 0x01;
#define ANX7150_EDID_861B_not_supported 0x02;
#define ANX7150_EDID_CheckSum_ERR 0x03;
#define ANX7150_EDID_No_ExtBlock 0x04;
#define ANX7150_EDID_ExtBlock_NotFor_861B 0x05;

#define ANX7150_VND_IDL_REG 0x00
#define ANX7150_VND_IDH_REG 0x01
#define ANX7150_DEV_IDL_REG 0x02
#define ANX7150_DEV_IDH_REG 0x03
#define ANX7150_DEV_REV_REG 0x04

#define ANX7150_SRST_REG 0x05
#define ANX7150_TX_RST 0x40
#define ANX7150_SRST_VIDCAP_RST	        0x20	// uint8 position
#define ANX7150_SRST_AFIFO_RST	       	 0x10	// uint8 position
#define ANX7150_SRST_HDCP_RST		        0x08	// uint8 position
#define ANX7150_SRST_VID_FIFO_RST		 0x04	// uint8 position
#define ANX7150_SRST_AUD_RST		 0x02	// uint8 position
#define ANX7150_SRST_SW_RST			 0x01	// uint8 position

#define ANX7150_SYS_STATE_REG 0x06
#define ANX7150_SYS_STATE_AUD_CLK_DET	        0x20	// uint8 position
#define ANX7150_SYS_STATE_AVMUTE	       	 0x10	// uint8 position
#define ANX7150_SYS_STATE_HP		       	 0x08	// uint8 position
#define ANX7150_SYS_STATE_VSYNC		 		 0x04	// uint8 position
#define ANX7150_SYS_STATE_CLK_DET		 	 0x02	// uint8 position
#define ANX7150_SYS_STATE_RSV_DET			 0x01	// uint8 position

#define ANX7150_SYS_CTRL1_REG 0x07
#define ANX7150_SYS_CTRL1_LINKMUTE_EN	        0x80	// uint8 position
#define ANX7150_SYS_CTRL1_HDCPHPD_RST		 0x40	// uint8 position
#define ANX7150_SYS_CTRL1_PDINT_SEL		 0x20	// uint8 position
#define ANX7150_SYS_CTRL1_DDC_FAST	        	 0x10	// uint8 position
#define ANX7150_SYS_CTRL1_DDC_SWCTRL	        0x08	// uint8 position
#define ANX7150_SYS_CTRL1_HDCPMODE		 0x04	// uint8 position
#define ANX7150_SYS_CTRL1_HDMI				 0x02	// uint8 position
#define ANX7150_SYS_CTRL1_PWDN_CTRL	        0x01	// uint8 position

#define ANX7150_SYS_CTRL2_REG 0x08
#define ANX7150_SYS_CTRL2_DDC_RST	      		  0x08	// uint8 position
#define ANX7150_SYS_CTRL2_TMDSBIST_RST	  0x04	// uint8 position
#define ANX7150_SYS_CTRL2_MISC_RST		 	  0x02	// uint8 position
#define ANX7150_SYS_CTRL2_HW_RST	     		  0x01	// uint8 position

#define ANX7150_SYS_CTRL3_REG 0x09
#define ANX7150_SYS_CTRL3_I2C_PWON 0x02
#define ANX7150_SYS_CTRL3_PWON_ALL 0x01

#define ANX7150_SYS_CTRL4_REG 0x0b

#define ANX7150_VID_STATUS_REG 0x10
#define ANX7150_VID_STATUS_VID_STABLE		 0x20	// uint8 position
#define ANX7150_VID_STATUS_EMSYNC_ERR	        0x10	// uint8 position
#define ANX7150_VID_STATUS_FLD_POL	    		 0x08	// uint8 position
#define ANX7150_VID_STATUS_TYPE		 	 0x04	// uint8 position
#define ANX7150_VID_STATUS_VSYNC_POL		 0x02	// uint8 position
#define ANX7150_VID_STATUS_HSYNC_POL	        0x01	// uint8 position

#define ANX7150_VID_MODE_REG 0x11
#define ANX7150_VID_MODE_CHKSHARED_EN	 0x80	// uint8 position
#define ANX7150_VID_MODE_LINKVID_EN		 0x40	// uint8 position
#define ANX7150_VID_MODE_RANGE_Y2R		 0x20	// uint8 position
#define ANX7150_VID_MODE_CSPACE_Y2R	        0x10	// uint8 position
#define ANX7150_VID_MODE_Y2R_SEL	        	 0x08	// uint8 position
#define ANX7150_VID_MODE_UPSAMPLE			 0x04	// uint8 position

#define ANX7150_VID_CTRL_REG  0x12
#define ANX7150_VID_CTRL_IN_EN	    		 0x10	// uint8 position
#define ANX7150_VID_CTRL_YCuint8_SEL        	 0x08	// uint8 position
#define ANX7150_VID_CTRL_uint8CTRL_EN	 		0x04	// uint8 position

#define ANX7150_VID_CAPCTRL0_REG  0x13
#define ANX7150_VID_CAPCTRL0_DEGEN_EN	 	 0x80	// uint8 position
#define ANX7150_VID_CAPCTRL0_EMSYNC_EN	 0x40	// uint8 position
#define ANX7150_VID_CAPCTRL0_DEMUX_EN		 0x20	// uint8 position
#define ANX7150_VID_CAPCTRL0_INV_IDCK	        0x10	// uint8 position
#define ANX7150_VID_CAPCTRL0_DV_BUSMODE	 0x08	// uint8 position
#define ANX7150_VID_CAPCTRL0_DDR_EDGE		 0x04	// uint8 position
#define ANX7150_VID_CAPCTRL0_VIDuint8_SWAP	 0x02	// uint8 position
#define ANX7150_VID_CAPCTRL0_VIDBIST_EN	 0x01	// uint8 position

#define ANX7150_VID_CAPCTRL1_REG 0x14
#define ANX7150_VID_CAPCTRL1_FORMAT_SEL	 	 0x80	// uint8 position
#define ANX7150_VID_CAPCTRL1_VSYNC_POL	   	 0x40	// uint8 position
#define ANX7150_VID_CAPCTRL1_HSYNC_POL		 0x20	// uint8 position
#define ANX7150_VID_CAPCTRL1_INV_FLDPOL	        0x10	// uint8 position
#define ANX7150_VID_CAPCTRL1_VID_TYPE	 		0x08	// uint8 position

#define ANX7150_H_RESL_REG 0x15
#define ANX7150_H_RESH_REG 0x16
#define ANX7150_VID_PIXL_REG 0x17
#define ANX7150_VID_PIXH_REG 0x18
#define ANX7150_H_FRONTPORCHL_REG 0x19
#define ANX7150_H_FRONTPORCHH_REG 0x1A
#define ANX7150_HSYNC_ACT_WIDTHL_REG 0x1B
#define ANX7150_HSYNC_ACT_WIDTHH_REG 0x1C
#define ANX7150_H_BACKPORCHL_REG 0x1D
#define ANX7150_H_BACKPORCHH_REG 0x1E
#define ANX7150_V_RESL_REG 0x1F
#define ANX7150_V_RESH_REG 0x20
#define ANX7150_ACT_LINEL_REG 0x21
#define ANX7150_ACT_LINEH_REG 0x22
#define ANX7150_ACT_LINE2VSYNC_REG 0x23
#define ANX7150_VSYNC_WID_REG 0x24
#define ANX7150_VSYNC_TAIL2VIDLINE_REG 0x25
#define ANX7150_VIDF_HRESL_REG 0x26
#define ANX7150_VIDF_HRESH_REG 0x27
#define ANX7150_VIDF_PIXL_REG 0x28
#define ANX7150_VIDF_PIXH_REG 0x29
#define ANX7150_VIDF_HFORNTPORCHL_REG 0x2A
#define ANX7150_VIDF_HFORNTPORCHH_REG 0x2B
#define ANX7150_VIDF_HSYNCWIDL_REG 0x2C
#define ANX7150_VIDF_HSYNCWIDH_REG 0x2D
#define ANX7150_VIDF_HBACKPORCHL_REG 0x2E
#define ANX7150_VIDF_HBACKPORCHH_REG 0x2F
#define ANX7150_VIDF_VRESL_REG 0x30
#define ANX7150_VIDF_VRESH_REG 0x31
#define ANX7150_VIDF_ACTVIDLINEL_REG 0x32
#define ANX7150_VIDF_ACTVIDLINEH_REG 0x33
#define ANX7150_VIDF_ACTLINE2VSYNC_REG 0x34
#define ANX7150_VIDF_VSYNCWIDLINE_REG 0x35
#define ANX7150_VIDF_VSYNCTAIL2VIDLINE_REG 0x36

//Video input data uint8 control registers

#define VID_uint8_CTRL0 0x37      //added
#define VID_uint8_CTRL1 0x38
#define VID_uint8_CTRL2 0x39
#define VID_uint8_CTRL3 0x3A
#define VID_uint8_CTRL4 0x3B
#define VID_uint8_CTRL5 0x3C
#define VID_uint8_CTRL6 0x3D
#define VID_uint8_CTRL7 0x3E
#define VID_uint8_CTRL8 0x3F
#define VID_uint8_CTRL9 0x48
#define VID_uint8_CTRL10 0x49
#define VID_uint8_CTRL11 0x4A
#define VID_uint8_CTRL12 0x4B
#define VID_uint8_CTRL13 0x4C
#define VID_uint8_CTRL14 0x4D
#define VID_uint8_CTRL15 0x4E
#define VID_uint8_CTRL16 0x4F
#define VID_uint8_CTRL17 0x89
#define VID_uint8_CTRL18 0x8A
#define VID_uint8_CTRL19 0x8B
#define VID_uint8_CTRL20 0x8C
#define VID_uint8_CTRL21 0x8D
#define VID_uint8_CTRL22 0x8E
#define VID_uint8_CTRL23 0x8F


#define ANX7150_INTR_STATE_REG 0x40

#define ANX7150_INTR_CTRL_REG 0x41

#define ANX7150_INTR_CTRL_SOFT_INTR	 0x04	// uint8 position
#define ANX7150_INTR_CTRL_TYPE			 0x02	// uint8 position
#define ANX7150_INTR_CTRL_POL	 		 0x01	// uint8 position

#define ANX7150_INTR1_STATUS_REG 0x42
#define ANX7150_INTR1_STATUS_CTS_CHG 	 	 0x80	// uint8 position
#define ANX7150_INTR1_STATUS_AFIFO_UNDER	 0x40	// uint8 position
#define ANX7150_INTR1_STATUS_AFIFO_OVER	 0x20	// uint8 position
#define ANX7150_INTR1_STATUS_SPDIF_ERR	        0x10	// uint8 position
#define ANX7150_INTR1_STATUS_SW_INT	 	0x08	// uint8 position
#define ANX7150_INTR1_STATUS_HP_CHG		 0x04	// uint8 position
#define ANX7150_INTR1_STATUS_CTS_OVRWR	 	0x02	// uint8 position
#define ANX7150_INTR1_STATUS_CLK_CHG		 0x01	// uint8 position

#define ANX7150_INTR2_STATUS_REG 0x43
#define ANX7150_INTR2_STATUS_ENCEN_CHG 	 	0x80	// uint8 position
#define ANX7150_INTR2_STATUS_HDCPLINK_CHK	 	0x40	// uint8 position
#define ANX7150_INTR2_STATUS_HDCPENHC_CHK 	0x20	// uint8 position
#define ANX7150_INTR2_STATUS_BKSV_RDY		        0x10	// uint8 position
#define ANX7150_INTR2_STATUS_PLLLOCK_CHG	 	0x08	// uint8 position
#define ANX7150_INTR2_STATUS_SHA_DONE			 0x04	// uint8 position
#define ANX7150_INTR2_STATUS_AUTH_CHG	 		0x02	// uint8 position
#define ANX7150_INTR2_STATUS_AUTH_DONE		 0x01	// uint8 position

#define ANX7150_INTR3_STATUS_REG 0x44
#define ANX7150_INTR3_STATUS_SPDIFBI_ERR 	 	0x80	// uint8 position
#define ANX7150_INTR3_STATUS_VIDF_CHG	 		0x40	// uint8 position
#define ANX7150_INTR3_STATUS_AUDCLK_CHG 		0x20	// uint8 position
#define ANX7150_INTR3_STATUS_DDCACC_ERR	        0x10	// uint8 position
#define ANX7150_INTR3_STATUS_DDC_NOACK	 	0x08	// uint8 position
#define ANX7150_INTR3_STATUS_VSYNC_DET		 0x04	// uint8 position
#define ANX7150_INTR3_STATUS_RXSEN_CHG		0x02	// uint8 position
#define ANX7150_INTR3_STATUS_SPDIF_UNSTBL		 0x01	// uint8 position

#define ANX7150_INTR1_MASK_REG 0x45
#define ANX7150_INTR2_MASK_REG 0x46
#define ANX7150_INTR3_MASK_REG 0x47

#define ANX7150_HDMI_AUDCTRL0_REG 0x50
#define ANX7150_HDMI_AUDCTRL0_LAYOUT 	 	0x80	// uint8 position
#define ANX7150_HDMI_AUDCTRL0_DOWN_SMPL 	0x60	// uint8 position
#define ANX7150_HDMI_AUDCTRL0_CTSGEN_SC 	 	0x10	// uint8 position
#define ANX7150_HDMI_AUDCTRL0_INV_AUDCLK 	 	0x08	// uint8 position

#define ANX7150_HDMI_AUDCTRL1_REG 0x51
#define ANX7150_HDMI_AUDCTRL1_IN_EN 	 		0x80	// uint8 position
#define ANX7150_HDMI_AUDCTRL1_SPDIFIN_EN	 	0x40	// uint8 position
#define ANX7150_HDMI_AUDCTRL1_SD3IN_EN		0x20	// uint8 position
#define ANX7150_HDMI_AUDCTRL1_SD2IN_EN	        0x10	// uint8 position
#define ANX7150_HDMI_AUDCTRL1_SD1IN_EN	 	0x08	// uint8 position
#define ANX7150_HDMI_AUDCTRL1_SD0IN_EN		 0x04	// uint8 position
#define ANX7150_HDMI_AUDCTRL1_SPDIFFS_OVRWR	0x02	// uint8 position
#define ANX7150_HDMI_AUDCTRL1_CLK_SEL		 0x01	// uint8 position

#define ANX7150_I2S_CTRL_REG 0x52
#define ANX7150_I2S_CTRL_VUCP 	 		0x80	// uint8 position
#define SPDIF_IN_SEL 0x10 //0-spdif, 1-multi with sd0
#define ANX7150_I2S_CTRL_SHIFT_CTRL	 	0x08	// uint8 position
#define ANX7150_I2S_CTRL_DIR_CTRL		 0x04	// uint8 position
#define ANX7150_I2S_CTRL_WS_POL		0x02	// uint8 position
#define ANX7150_I2S_CTRL_JUST_CTRL		 0x01	// uint8 position

#define ANX7150_I2SCH_CTRL_REG 0x53
#define ANX7150_I2SCH_FIFO3_SEL	 	0xC0	// uint8 position
#define ANX7150_I2SCH_FIFO2_SEL	 0x30	// uint8 position
#define ANX7150_I2SCH_FIFO1_SEL	 0x0C	// uint8 position
#define ANX7150_I2SCH_FIFO0_SEL	 0x03	// uint8 position

#define ANX7150_I2SCH_SWCTRL_REG 0x54

#define ANX7150_I2SCH_SWCTRL_SW3 	 		0x80	// uint8 position
#define ANX7150_I2SCH_SWCTRL_SW2	 	0x40	// uint8 position
#define ANX7150_I2SCH_SWCTRL_SW1		0x20	// uint8 position
#define ANX7150_I2SCH_SWCTRL_SW0	        0x10	// uint8 position
#define ANX7150_I2SCH_SWCTRL_INWD_LEN		0xE0	// uint8 position
#define ANX7150_I2SCH_SWCTRL_INWD_MAX		 0x01	// uint8 position

#define ANX7150_SPDIFCH_STATUS_REG 0x55
#define ANX7150_SPDIFCH_STATUS_FS_FREG	0xF0	// uint8 position
#define ANX7150_SPDIFCH_STATUS_WD_LEN 0x0E	// uint8 position
#define ANX7150_SPDIFCH_STATUS_WD_MX 0x01	// uint8 position

#define ANX7150_I2SCH_STATUS1_REG 0x56
#define ANX7150_I2SCH_STATUS1_MODE	 0xC0	// uint8 position
#define ANX7150_I2SCH_STATUS1_PCM_MODE	 0x38	// uint8 position
#define ANX7150_I2SCH_STATUS1_SW_CPRGT	 0x04	// uint8 position
#define ANX7150_I2SCH_STATUS1_NON_PCM	0x02	// uint8 position
#define ANX7150_I2SCH_STATUS1_PROF_APP	 0x01	// uint8 position

#define ANX7150_I2SCH_STATUS2_REG 0x57

#define ANX7150_I2SCH_STATUS3_REG 0x58
#define ANX7150_I2SCH_STATUS3_CH_NUM	0xF0	// uint8 position
#define ANX7150_I2SCH_STATUS3_SRC_NUM	0x0F	// uint8 position



#define ANX7150_I2SCH_STATUS4_REG 0x59

#define ANX7150_I2SCH_STATUS5_REG 0x5A

#define ANX7150_I2SCH_STATUS5_WORD_MAX 0x01	// uint8 position

#define ANX7150_HDMI_AUDSTATUS_REG 0x5B

#define ANX7150_HDMI_AUDSTATUS_SPDIF_DET 0x01	// uint8 position

#define ANX7150_HDMI_AUDBIST_CTRL_REG 0x5C

#define ANX7150_HDMI_AUDBIST_EN3	 	0x08	// uint8 position
#define ANX7150_HDMI_AUDBIST_EN2		 0x04	// uint8 position
#define ANX7150_HDMI_AUDBIST_EN1		0x02	// uint8 position
#define ANX7150_HDMI_AUDBIST_EN0		 0x01	// uint8 position

#define ANX7150_AUD_INCLK_CNT_REG 0x5D
#define ANX7150_AUD_DEBUG_STATUS_REG 0x5E

#define ANX7150_ONEuint8_AUD_CTRL_REG 0x60

#define ANX7150_ONEuint8_AUD_CTRL_SEN7 	 	0x80	// uint8 position
#define ANX7150_ONEuint8_AUD_CTRL_SEN6	 	0x40	// uint8 position
#define ANX7150_ONEuint8_AUD_CTRL_SEN5		0x20	// uint8 position
#define ANX7150_ONEuint8_AUD_CTRL_SEN4	    0x10	// uint8 position
#define ANX7150_ONEuint8_AUD_CTRL_SEN3	 	0x08	// uint8 position
#define ANX7150_ONEuint8_AUD_CTRL_SEN2		0x04	// uint8 position
#define ANX7150_ONEuint8_AUD_CTRL_SEN1		0x02	// uint8 position
#define ANX7150_ONEuint8_AUD_CTRL_SEN0		0x01	// uint8 position

#define ANX7150_ONEuint8_AUD0_CTRL_REG 0x61
#define ANX7150_ONEuint8_AUD1_CTRL_REG 0x62
#define ANX7150_ONEuint8_AUD2_CTRL_REG 0x63
#define ANX7150_ONEuint8_AUD3_CTRL_REG 0x64

#define ANX7150_ONEuint8_AUDCLK_CTRL_REG 0x65

#define ANX7150_ONEuint8_AUDCLK_DET 	0x08	// uint8 position

#define ANX7150_SPDIF_ERR_THRSHLD_REG 0x66
#define ANX7150_SPDIF_ERR_CNT_REG 0x67

#define ANX7150_HDMI_LINK_CTRL_REG 0x70

#define ANX7150_HDMI_LINK_DATA_MUTEEN1 	 	0x80	// uint8 position
#define ANX7150_HDMI_LINK_DATA_MUTEEN0	 	0x40	// uint8 position
#define ANX7150_HDMI_LINK_CLK_MUTEEN2		0x20	// uint8 position
#define ANX7150_HDMI_LINK_CLK_MUTEEN1	    0x10	// uint8 position
#define ANX7150_HDMI_LINK_CLK_MUTEEN0	 	0x08	// uint8 position
#define ANX7150_HDMI_LINK_DEC_DE			0x04	// uint8 position
#define ANX7150_HDMI_LINK_PRMB_INC			0x02	// uint8 position
#define ANX7150_HDMI_LINK_AUTO_PROG			0x01	// uint8 position

#define ANX7150_VID_CAPCTRL2_REG  0x71

#define ANX7150_VID_CAPCTRL2_CHK_UPDATEEN    0x10	// uint8 position

#define ANX7150_LINK_MUTEEE_REG 0x72

#define ANX7150_LINK_MUTEEE_AVMUTE_EN2		0x20	// uint8 position
#define ANX7150_LINK_MUTEEE_AVMUTE_EN1	    0x10	// uint8 position
#define ANX7150_LINK_MUTEEE_AVMUTE_EN0	 	0x08	// uint8 position
#define ANX7150_LINK_MUTEEE_AUDMUTE_EN2		0x04	// uint8 position
#define ANX7150_LINK_MUTEEE_AUDMUTE_EN1		0x02	// uint8 position
#define ANX7150_LINK_MUTEEE_AUDMUTE_EN0		0x01	// uint8 position

#define ANX7150_SERDES_TEST0_REG 0x73
#define ANX7150_SERDES_TEST1_REG 0x74
#define ANX7150_SERDES_TEST2_REG 0x75

#define ANX7150_PLL_TX_AMP 0x76


#define ANX7150_DDC_SLV_ADDR_REG 0x80
#define ANX7150_DDC_SLV_SEGADDR_REG 0x81
#define ANX7150_DDC_SLV_OFFADDR_REG 0x82
#define ANX7150_DDC_ACC_CMD_REG 0x83
#define ANX7150_DDC_ACCNUM0_REG 0x84
#define ANX7150_DDC_ACCNUM1_REG 0x85

#define ANX7150_DDC_CHSTATUS_REG 0x86

#define ANX7150_DDC_CHSTATUS_DDCERR	 	0x80	// uint8 position
#define ANX7150_DDC_CHSTATUS_DDC_OCCUPY	 	0x40	// uint8 position
#define ANX7150_DDC_CHSTATUS_FIFO_FULL		0x20	// uint8 position
#define ANX7150_DDC_CHSTATUS_FIFO_EMPT	    0x10	// uint8 position
#define ANX7150_DDC_CHSTATUS_NOACK	 	0x08	// uint8 position
#define ANX7150_DDC_CHSTATUS_FIFO_RD			0x04	// uint8 position
#define ANX7150_DDC_CHSTATUS_FIFO_WR			0x02	// uint8 position
#define ANX7150_DDC_CHSTATUS_INPRO			0x01	// uint8 position

#define ANX7150_DDC_FIFO_ACC_REG 0x87
#define ANX7150_DDC_FIFOCNT_REG 0x88

#define ANX7150_SYS_PD_REG 0x90
#define ANX7150_SYS_PD_PLL 	 	0x80	// uint8 position
#define ANX7150_SYS_PD_TMDS	 	0x40	// uint8 position
#define ANX7150_SYS_PD_TMDS_CLK		0x20	// uint8 position
#define ANX7150_SYS_PD_MISC	    0x10	// uint8 position
#define ANX7150_SYS_PD_LINK	 	0x08	// uint8 position
#define ANX7150_SYS_PD_IDCK			0x04	// uint8 position
#define ANX7150_SYS_PD_AUD			0x02	// uint8 position
#define ANX7150_SYS_PD_MACRO_ALL	0x01	// uint8 position

#define ANX7150_LINKFSM_DEBUG0_REG 0x91
#define ANX7150_LINKFSM_DEBUG1_REG 0x92

#define ANX7150_PLL_CTRL0_REG 0x93
#define ANX7150_PLL_CTRL0_CPREG_BLEED			0x02	// uint8 position
#define ANX7150_PLL_CTRL0_TEST_EN	0x01	// uint8 position

#define ANX7150_PLL_CTRL1_REG 0x94
#define ANX7150_PLL_CTRL1_TESTEN 	 	0x80	// uint8 position

#define ANX7150_OSC_CTRL_REG 0x95
#define ANX7150_OSC_CTRL_TESTEN	 	0x80	// uint8 position
#define ANX7150_OSC_CTRL_SEL_BG	 	0x40	// uint8 position

#define ANX7150_TMDS_CH0_CONFIG_REG 0x96
#define ANX7150_TMDS_CH0_TESTEN		0x20	// uint8 position
#define ANX7150_TMDS_CH0_AMP		0x1C	// uint8 position
#define ANX7150_TMDS_CHO_EMP		0x03	// uint8 position

#define ANX7150_TMDS_CH1_CONFIG_REG 0x97
#define ANX7150_TMDS_CH1_TESTEN		0x20	// uint8 position
#define ANX7150_TMDS_CH1_AMP		0x1C	// uint8 position
#define ANX7150_TMDS_CH1_EMP		0x03	// uint8 position

#define ANX7150_TMDS_CH2_CONFIG_REG 0x98
#define ANX7150_TMDS_CH2_TESTEN		0x20	// uint8 position
#define ANX7150_TMDS_CH2_AMP		0x1C	// uint8 position
#define ANX7150_TMDS_CH2_EMP		0x03	// uint8 position

#define ANX7150_TMDS_CLKCH_CONFIG_REG 0x99
#define ANX7150_TMDS_CLKCH_MUTE	 	0x80	// uint8 position
#define ANX7150_TMDS_CLKCH_TESTEN	0x08	// uint8 position
#define ANX7150_TMDS_CLKCH_AMP		0x07	// uint8 position

#define ANX7150_CHIP_CTRL_REG 0x9A
#define ANX7150_CHIP_CTRL_PRBS_GENEN 	 	0x80	// uint8 position
#define ANX7150_CHIP_CTRL_LINK_DBGSEL	 	0x70	// uint8 position
#define ANX7150_CHIP_CTRL_VIDCHK_EN		 	0x08	// uint8 position
#define ANX7150_CHIP_CTRL_MISC_TIMER		0x04	// uint8 position
#define ANX7150_CHIP_CTRL_PLL_RNG		0x02	// uint8 position
#define ANX7150_CHIP_CTRL_PLL_MAN		0x01	// uint8 position

#define ANX7150_CHIP_STATUS_REG 0x9B
#define ANX7150_CHIP_STATUS_GPIO	 	0x80	// uint8 position
#define ANX7150_CHIP_STATUS_SDA	 		0x40	// uint8 position
#define ANX7150_CHIP_STATUS_SCL			0x20	// uint8 position
#define ANX7150_CHIP_STATUS_PLL_HSPO	0x04	// uint8 position
#define ANX7150_CHIP_STATUS_PLL_LOCK	0x02	// uint8 position
#define ANX7150_CHIP_STATUS_MISC_LOCK	0x01	// uint8 position

#define ANX7150_DBG_PINGPIO_CTRL_REG  0x9C
#define ANX7150_DBG_PINGPIO_VDLOW_SHAREDEN		0x04	// uint8 position
#define ANX7150_DBG_PINGPIO_GPIO_ADDREN			0x02	// uint8 position
#define ANX7150_DBG_PINGPIO_GPIO_OUT			0x01	// uint8 position

#define ANX7150_CHIP_DEBUG0_CTRL_REG  0x9D
#define ANX7150_CHIP_DEBUG0_PRBS_ERR 0xE0		// uint8 position
#define ANX7150_CHIP_DEBUG0_CAPST  	 0x1F		// uint8 position

#define ANX7150_CHIP_DEBUG1_CTRL_REG  0x9E
#define ANX7150_CHIP_DEBUG1_SDA_SW 	 	0x80	// uint8 position
#define ANX7150_CHIP_DEBUG1_SCL_SW	 	0x40	// uint8 position
#define ANX7150_CHIP_DEBUG1_SERDES_TESTEN		0x20	// uint8 position
#define ANX7150_CHIP_DEBUG1_CLK_BYPASS	    0x10	// uint8 position
#define ANX7150_CHIP_DEBUG1_FORCE_PLLLOCK	 	0x08	// uint8 position
#define ANX7150_CHIP_DEBUG1_PLLLOCK_BYPASS			0x04	// uint8 position
#define ANX7150_CHIP_DEBUG1_FORCE_HP			0x02	// uint8 position
#define ANX7150_CHIP_DEBUG1_HP_DEGLITCH			0x01	// uint8 position

#define ANX7150_CHIP_DEBUG2_CTRL_REG  0x9F
#define ANX7150_CHIP_DEBUG2_EXEMB_SYNCEN		0x04	// uint8 position
#define ANX7150_CHIP_DEBUG2_VIDBIST			0x02	// uint8 position

#define ANX7150_VID_INCLK_REG  0x5F

#define ANX7150_HDCP_STATUS_REG  0xA0
#define ANX7150_HDCP_STATUS_ADV_CIPHER 	 	0x80	// uint8 position
#define ANX7150_HDCP_STATUS_R0_READY	    0x10	// uint8 position
#define ANX7150_HDCP_STATUS_AKSV_ACT	 	0x08	// uint8 position
#define ANX7150_HDCP_STATUS_ENCRYPT			0x04	// uint8 position
#define ANX7150_HDCP_STATUS_AUTH_PASS			0x02	// uint8 position
#define ANX7150_HDCP_STATUS_KEY_DONE			0x01	// uint8 position

#define ANX7150_HDCP_CTRL0_REG  0xA1
#define ANX7150_HDCP_CTRL0_STORE_AN 	 	0x80	// uint8 position
#define ANX7150_HDCP_CTRL0_RX_REP	 	0x40	// uint8 position
#define ANX7150_HDCP_CTRL0_RE_AUTH		0x20	// uint8 position
#define ANX7150_HDCP_CTRL0_SW_AUTHOK	    0x10	// uint8 position
#define ANX7150_HDCP_CTRL0_HW_AUTHEN	 	0x08	// uint8 position
#define ANX7150_HDCP_CTRL0_ENC_EN			0x04	// uint8 position
#define ANX7150_HDCP_CTRL0_BKSV_SRM			0x02	// uint8 position
#define ANX7150_HDCP_CTRL0_KSV_VLD			0x01	// uint8 position

#define ANX7150_HDCP_CTRL1_REG  0xA2
#define ANX7150_LINK_CHK_12_EN  0x40
#define ANX7150_HDCP_CTRL1_DDC_NOSTOP		0x20	// uint8 position
#define ANX7150_HDCP_CTRL1_DDC_NOACK	    0x10	// uint8 position
#define ANX7150_HDCP_CTRL1_EDDC_NOACK	 	0x08	// uint8 position
#define ANX7150_HDCP_CTRL1_BLUE_SCREEN_EN			0x04	// uint8 position
#define ANX7150_HDCP_CTRL1_RCV11_EN			0x02	// uint8 position
#define ANX7150_HDCP_CTRL1_HDCP11_EN			0x01	// uint8 position

#define ANX7150_HDCP_Link_Check_FRAME_NUM_REG  0xA3
#define ANX7150_HDCP_AKSV1_REG  0xA5
#define ANX7150_HDCP_AKSV2_REG  0xA6
#define ANX7150_HDCP_AKSV3_REG  0xA7
#define ANX7150_HDCP_AKSV4_REG  0xA8
#define ANX7150_HDCP_AKSV5_REG  0xA9

#define ANX7150_HDCP_AN1_REG  0xAA
#define ANX7150_HDCP_AN2_REG  0xAB
#define ANX7150_HDCP_AN3_REG  0xAC
#define ANX7150_HDCP_AN4_REG  0xAD
#define ANX7150_HDCP_AN5_REG  0xAE
#define ANX7150_HDCP_AN6_REG  0xAF
#define ANX7150_HDCP_AN7_REG  0xB0
#define ANX7150_HDCP_AN8_REG  0xB1

#define ANX7150_HDCP_BKSV1_REG  0xB2
#define ANX7150_HDCP_BKSV2_REG  0xB3
#define ANX7150_HDCP_BKSV3_REG  0xB4
#define ANX7150_HDCP_BKSV4_REG  0xB5
#define ANX7150_HDCP_BKSV5_REG  0xB6

#define ANX7150_HDCP_RI1_REG  0xB7
#define ANX7150_HDCP_RI2_REG  0xB8

#define ANX7150_HDCP_PJ_REG  0xB9
#define ANX7150_HDCP_RX_CAPS_REG  0xBA
#define ANX7150_HDCP_BSTATUS0_REG  0xBB
#define ANX7150_HDCP_BSTATUS1_REG  0xBC

#define ANX7150_HDCP_AMO0_REG  0xD0
#define ANX7150_HDCP_AMO1_REG  0xD1
#define ANX7150_HDCP_AMO2_REG  0xD2
#define ANX7150_HDCP_AMO3_REG  0xD3
#define ANX7150_HDCP_AMO4_REG  0xD4
#define ANX7150_HDCP_AMO5_REG  0xD5
#define ANX7150_HDCP_AMO6_REG  0xD6
#define ANX7150_HDCP_AMO7_REG  0xD7

#define ANX7150_HDCP_DBG_CTRL_REG  0xBD

#define ANX7150_HDCP_DBG_ENC_INC 	0x08	// uint8 position
#define ANX7150_HDCP_DBG_DDC_SPEED	0x06	// uint8 position
#define ANX7150_HDCP_DBG_SKIP_RPT	0x01	// uint8 position

#define ANX7150_HDCP_KEY_STATUS_REG  0xBE
#define ANX7150_HDCP_KEY_BIST_EN	0x04	// uint8 position
#define ANX7150_HDCP_KEY_BIST_ERR	0x02	// uint8 position
#define ANX7150_HDCP_KEY_CMD_DONE	0x01	// uint8 position

#define ANX7150_KEY_CMD_REGISTER 0xBF   //added

#define ANX7150_HDCP_AUTHDBG_STATUS_REG  0xC7
#define ANX7150_HDCP_ENCRYPTDBG_STATUS_REG  0xC8
#define ANX7150_HDCP_FRAME_NUM_REG  0xC9

#define ANX7150_DDC_MSTR_INTER_REG  0xCA
#define ANX7150_DDC_MSTR_LINK_REG  0xCB

#define ANX7150_HDCP_BLUESCREEN0_REG  0xCC
#define ANX7150_HDCP_BLUESCREEN1_REG  0xCD
#define ANX7150_HDCP_BLUESCREEN2_REG  0xCE
//	DEV_ADDR = 0x7A or 0x7E
#define ANX7150_INFO_PKTCTRL1_REG  0xC0
#define ANX7150_INFO_PKTCTRL1_SPD_RPT 	 	0x80	// uint8 position
#define ANX7150_INFO_PKTCTRL1_SPD_EN	 	0x40	// uint8 position
#define ANX7150_INFO_PKTCTRL1_AVI_RPT		0x20	// uint8 position
#define ANX7150_INFO_PKTCTRL1_AVI_EN	    0x10	// uint8 position
#define ANX7150_INFO_PKTCTRL1_GCP_RPT	 	0x08	// uint8 position
#define ANX7150_INFO_PKTCTRL1_GCP_EN		0x04	// uint8 position
#define ANX7150_INFO_PKTCTRL1_ACR_NEW		0x02	// uint8 position
#define ANX7150_INFO_PKTCTRL1_ACR_EN		0x01	// uint8 position

#define ANX7150_INFO_PKTCTRL2_REG  0xC1
#define ANX7150_INFO_PKTCTRL2_UD1_RPT 	 	0x80	// uint8 position
#define ANX7150_INFO_PKTCTRL2_UD1_EN	 	0x40	// uint8 position
#define ANX7150_INFO_PKTCTRL2_UD0_RPT		0x20	// uint8 position
#define ANX7150_INFO_PKTCTRL2_UD0_EN	    0x10	// uint8 position
#define ANX7150_INFO_PKTCTRL2_MPEG_RPT	 	0x08	// uint8 position
#define ANX7150_INFO_PKTCTRL2_MPEG_EN		0x04	// uint8 position
#define ANX7150_INFO_PKTCTRL2_AIF_RPT		0x02	// uint8 position
#define ANX7150_INFO_PKTCTRL2_AIF_EN		0x01	// uint8 position

#define ANX7150_ACR_N1_SW_REG  0xC2
#define ANX7150_ACR_N2_SW_REG  0xC3
#define ANX7150_ACR_N3_SW_REG  0xC4

#define ANX7150_ACR_CTS1_SW_REG  0xC5
#define ANX7150_ACR_CTS2_SW_REG  0xC6
#define ANX7150_ACR_CTS3_SW_REG  0xC7

#define ANX7150_ACR_CTS1_HW_REG  0xC8
#define ANX7150_ACR_CTS2_HW_REG  0xC9
#define ANX7150_ACR_CTS3_HW_REG  0xCA

#define ANX7150_ACR_CTS_CTRL_REG  0xCB

#define ANX7150_GNRL_CTRL_PKT_REG  0xCC
#define ANX7150_GNRL_CTRL_CLR_AVMUTE		0x02	// uint8 position
#define ANX7150_GNRL_CTRL_SET_AVMUTE		0x01	// uint8 position

#define ANX7150_AUD_PKT_FLATCTRL_REG  0xCD
#define ANX7150_AUD_PKT_AUTOFLAT_EN 		0x80	// uint8 position
#define ANX7150_AUD_PKT_FLAT 	 			0x07	// uint8 position


//select video hardware interface
#define ANX7150_VID_HW_INTERFACE 0x00//0x00:RGB and YcbCr 4:4:4 Formats with Separate Syncs (24-bpp mode)
                                                                 //0x01:YCbCr 4:2:2 Formats with Separate Syncs(16-bbp)
                                                                 //0x02:YCbCr 4:2:2 Formats with Embedded Syncs(No HS/VS/DE)
                                                                 //0x03:YC Mux 4:2:2 Formats with Separate Sync Mode1(uint815:8 and uint8 3:0 are used)
                                                                 //0x04:YC Mux 4:2:2 Formats with Separate Sync Mode2(uint811:0 are used)
                                                                 //0x05:YC Mux 4:2:2 Formats with Embedded Sync Mode1(uint815:8 and uint8 3:0 are used)
                                                                 //0x06:YC Mux 4:2:2 Formats with Embedded Sync Mode2(uint811:0 are used)
                                                                 //0x07:RGB and YcbCr 4:4:4 DDR Formats with Separate Syncs
                                                                 //0x08:RGB and YcbCr 4:4:4 DDR Formats with Embedded Syncs
                                                                 //0x09:RGB and YcbCr 4:4:4 Formats with Separate Syncs but no DE
                                                                 //0x0a:YCbCr 4:2:2 Formats with Separate Syncs but no DE
//select input color space
#define ANX7150_INPUT_COLORSPACE 0x00//0x00: input color space is RGB
                                                                //0x01: input color space is YCbCr422
                                                                //0x02: input color space is YCbCr444
//select input pixel clock edge for DDR mode
#define ANX7150_IDCK_EDGE_DDR 0x00  //0x00:use rising edge to latch even numbered pixel data//jack wen
                                                                //0x01:use falling edge to latch even numbered pixel data

//select audio hardware interface
#define ANX7150_AUD_HW_INTERFACE 0x01//0x01:audio input comes from I2S
                                                                  //0x02:audio input comes from SPDIF
                                                                  //0x04:audio input comes from one uint8 audio
//select MCLK and Fs relationship if audio HW interface is I2S
#define ANX7150_MCLK_Fs_RELATION 0x01//0x00:MCLK = 128 * Fs
                                                                //0x01:MCLK = 256 * Fs
                                                                //0x02:MCLK = 384 * Fs
                                                                //0x03:MCLK = 512 * Fs			//wen updated error

#define ANX7150_AUD_CLK_EDGE 0x00  //0x00:use MCLK and SCK rising edge to latch audio data
                                                                //0x08, revised by wen. //0x80:use MCLK and SCK falling edge to latch audio data
//select I2S channel numbers if audio HW interface is I2S
#define ANX7150_I2S_CH0_ENABLE 0x00 //0x01:enable channel 0 input; 0x00: disable
#define ANX7150_I2S_CH1_ENABLE 0x00 //0x01:enable channel 0 input; 0x00: disable
#define ANX7150_I2S_CH2_ENABLE 0x00 //0x01:enable channel 0 input; 0x00: disable
#define ANX7150_I2S_CH3_ENABLE 0x00 //0x01:enable channel 0 input; 0x00: disable
//select I2S word length if audio HW interface is I2S
#define ANX7150_I2S_WORD_LENGTH 0x02
                                        //0x02 = 16uint8s; 0x04 = 18 uint8s; 0x08 = 19 uint8s; 0x0a = 20 uint8s(maximal word length is 20uint8s); 0x0c = 17 uint8s;
                                        // 0x03 = 20uint8s(maximal word length is 24uint8s); 0x05 = 22 uint8s; 0x09 = 23 uint8s; 0x0b = 24 uint8s; 0x0d = 21 uint8s;

//select I2S format if audio HW interface is I2S
#define ANX7150_I2S_SHIFT_CTRL 0x00//0x00: fist uint8 shift(philips spec)
                                                                //0x01:no shift
#define ANX7150_I2S_DIR_CTRL 0x00//0x00:SD data MSB first
                                                            //0x01:LSB first
#define ANX7150_I2S_WS_POL 0x00//0x00:left polarity when word select is low
                                                        //0x01:left polarity when word select is high
#define ANX7150_I2S_JUST_CTRL 0x00//0x00:data is left justified
                                                             //0x01:data is right justified

#define EDID_Parse_Enable 1
//InfoFrame and Control Packet Registers
// 0x7A or 0X7E
/*
#define AVI_HB0  0x00
#define AVI_HB1  0x01
#define AVI_HB2  0x02
#define AVI_PB0   0x03
#define AVI_PB1   0x04
#define AVI_PB2   0x05
#define AVI_PB3   0x06
#define AVI_PB4   0x07
#define AVI_PB5   0x08
#define AVI_PB6   0x09
#define AVI_PB7   0x0A
#define AVI_PB8   0x0B
#define AVI_PB9   0x0C
#define AVI_PB10   0x0D
#define AVI_PB11   0x0E
#define AVI_PB12   0x0F
#define AVI_PB13   0x10
#define AVI_PB14   0x11
#define AVI_PB15   0x12

#define AUD_HBO  0x20
#define AUD_HB1  0x21
#define AUD_HB2  0x22
#define AUD_PB0  0x23
#define AUD_PB1  0x24
#define AUD_PB2  0x25
#define AUD_PB3  0x26
#define AUD_PB4  0x27
#define AUD_PB5  0x28
#define AUD_PB6  0x29
#define AUD_PB7  0x2A
#define AUD_PB8  0x2B
#define AUD_PB9  0x2C
#define AUD_PB10  0x2D

#define SPD_HBO  0x40
#define SPD_HB1  0x41
#define SPD_HB2  0x42
#define SPD_PB0  0x43
#define SPD_PB1  0x44
#define SPD_PB2  0x45
#define SPD_PB3  0x46
#define SPD_PB4  0x47
#define SPD_PB5  0x48
#define SPD_PB6  0x49
#define SPD_PB7  0x4A
#define SPD_PB8  0x4B
#define SPD_PB9  0x4C
#define SPD_PB10  0x4D
#define SPD_PB11  0x4E
#define SPD_PB12  0x4F
#define SPD_PB13  0x50
#define SPD_PB14  0x51
#define SPD_PB15  0x52
#define SPD_PB16  0x53
#define SPD_PB17  0x54
#define SPD_PB18  0x55
#define SPD_PB19  0x56
#define SPD_PB20  0x57
#define SPD_PB21  0x58
#define SPD_PB22  0x59
#define SPD_PB23  0x5A
#define SPD_PB24  0x5B
#define SPD_PB25  0x5C
#define SPD_PB26  0x5D
#define SPD_PB27  0x5E

#define MPEG_HBO  0x60
#define MPEG_HB1  0x61
#define MPEG_HB2  0x62
#define MPEG_PB0  0x63
#define MPEG_PB1  0x64
#define MPEG_PB2  0x65
#define MPEG_PB3  0x66
#define MPEG_PB4  0x67
#define MPEG_PB5  0x68
#define MPEG_PB6  0x69
#define MPEG_PB7  0x6A
#define MPEG_PB8  0x6B
#define MPEG_PB9  0x6C
#define MPEG_PB10  0x6D
#define MPEG_PB11  0x6E
#define MPEG_PB12  0x6F
#define MPEG_PB13  0x70
#define MPEG_PB14  0x71
#define MPEG_PB15  0x72
#define MPEG_PB16  0x73
#define MPEG_PB17  0x74
#define MPEG_PB18  0x75
#define MPEG_PB19  0x76
#define MPEG_PB20  0x77
#define MPEG_PB21  0x78
#define MPEG_PB22  0x79
#define MPEG_PB23  0x7A
#define MPEG_PB24  0x7B
#define MPEG_PB25  0x7C
#define MPEG_PB26  0x7D
#define MPEG_PB27  0x7E

#define USRDF0_HBO  0x80
#define USRDF0_HB1  0x81
#define USRDF0_HB2  0x82
#define USRDF0_PB0  0x83
#define USRDF0_PB1  0x84
#define USRDF0_PB2  0x85
#define USRDF0_PB3  0x86
#define USRDF0_PB4  0x87
#define USRDF0_PB5  0x88
#define USRDF0_PB6  0x89
#define USRDF0_PB7  0x8A
#define USRDF0_PB8  0x8B
#define USRDF0_PB9  0x8C
#define USRDF0_PB10  0x8D
#define USRDF0_PB11  0x8E
#define USRDF0_PB12  0x8F
#define USRDF0_PB13  0x90
#define USRDF0_PB14  0x91
#define USRDF0_PB15  0x92
#define USRDF0_PB16  0x93
#define USRDF0_PB17  0x94
#define USRDF0_PB18  0x95
#define USRDF0_PB19  0x96
#define USRDF0_PB20  0x97
#define USRDF0_PB21  0x98
#define USRDF0_PB22  0x99
#define USRDF0_PB23  0x9A
#define USRDF0_PB24  0x9B
#define USRDF0_PB25  0x9C
#define USRDF0_PB26  0x9D
#define USRDF0_PB27  0x9E

#define USRDF1_HBO  0xA0
#define USRDF1_HB1  0xA1
#define USRDF1_HB2  0xA2
#define USRDF1_PB0  0xA3
#define USRDF1_PB1  0xA4
#define USRDF1_PB2  0xA5
#define USRDF1_PB3  0xA6
#define USRDF1_PB4  0xA7
#define USRDF1_PB5  0xA8
#define USRDF1_PB6  0xA9
#define USRDF1_PB7  0xAA
#define USRDF1_PB8  0xAB
#define USRDF1_PB9  0xAC
#define USRDF1_PB10  0xAD
#define USRDF1_PB11  0xAE
#define USRDF1_PB12  0xAF
#define USRDF1_PB13  0xB0
#define USRDF1_PB14  0xB1
#define USRDF1_PB15  0xB2
#define USRDF1_PB16  0xB3
#define USRDF1_PB17  0xB4
#define USRDF1_PB18  0xB5
#define USRDF1_PB19  0xB6
#define USRDF1_PB20  0xB7
#define USRDF1_PB21  0xB8
#define USRDF1_PB22  0xB9
#define USRDF1_PB23  0xBA
#define USRDF1_PB24  0xBB
#define USRDF1_PB25  0xBC
#define USRDF1_PB26  0xBD
#define USRDF1_PB27  0xBE
*/

void ANX7150_Timer_Process (void);

void ANX7150_Config_Video(void);
void ANX7150_Parse_Video_Format(void);
void ANX7150_Get_Video_Timing(void);
void ANX7150_DE_Generator(void);
void ANX7150_Embed_Sync_Decode(void);
void ANX7150_Show_Video_Parameter(void);
void ANX7150_Clean_HDCP(void);

uint8 ANX7150_Config_Packet(void);
uint8 ANX7150_Load_Infoframe(packet_type member,
                            infoframe_struct *p);

uint8 ANX7150_Checksum(infoframe_struct *p);
uint8 ANX7150_Config_Audio(void);
uint8 ANX7150_Config_I2s(void);
uint8 ANX7150_Config_Spdif(void);
uint8 ANX7150_Config_Super_Audio(void);
void ANX7150_HDCP_Process(void);
void ANX7150_PLAYBACK_Process(void);
void ANX7150_Timer_Slot1(void);
void ANX7150_Timer_Slot2(void);
void ANX7150_Timer_Slot3(void);
void ANX7150_Timer_Slot4(void);
void ANX7150_Hotplug_Change_Interrupt(void);
void ANX7150_Variable_Initial(void);
void ANX7150_HW_Interface_Variable_Initial(void);
//void ANX7150_Config_Bist_Video(WORD bist_select_number);
void ANX7150_Video_Format_Change_Interrupt(void);
void ANX7150_Video_Clock_Change_Interrupt(void);
void ANX7150_Audio_CLK_Change_Interrupt(void);
void ANX7150_Set_AVMute(void);
void ANX7150_Clear_AVMute(void);
uint8 ANX7150_BKSV_SRM(void);
void ANX7150_Auth_Done_Interrupt(void);
void ANX7150_Auth_Change_Interrupt(void);
void ANX7150_Blue_Screen_Format_Config(void);
void ANX7150_Blue_Screen_Enable(void);
void ANX7150_Blue_Screen_Disable(void);
void ANX7150_HDCP_Encryption_Enable(void);
void ANX7150_HDCP_Encryption_Disable(void);
void ANX7150_AFIFO_Overrun_Interrupt(void);
void ANX7150_PllLock_Interrupt(void);
void ANX7150_Rx_Sense_Interrupt(void);
void ANX7150_HDCPLINK_CHK_Interrupt(void);

void ANX7150_SPDIF_Error_Interrupt(uint8 int1, uint8 int3);
void ANX7150_RST_DDCChannel(void);
void ANX7150_Hardware_HDCP_Auth_Init(void);
void ANX7150_Set_System_State(uint8 ss);
void ANX7150_Hardware_Initial(void);
void ANX7150_API_Initial(void);
void ANX7150_Interrupt_Process(void);
//void ANX7150_Interrupt_Information(uint8 c, uint8 n);

uint8 ANX7150_Parse_EDID(void);
void ANX7150_Read_EDID(void);
void ANX7150_DDC_Mass_Read(WORD length, uint8 segment);
uint8 ANX7150_Parse_EDIDHeader(void);
uint8 ANX7150_Parse_EDIDVersion(void);
void ANX7150_Parse_DTD(void);
//void ANX7150_Parse_BasicDis(void);
void ANX7150_Parse_VendorSTD(void);
void ANX7150_Parse_SpeakerSTD(void);
void ANX7150_Parse_VideoSTD(void);
void ANX7150_Parse_AudioSTD(void);
void ANX7150_Parse_STD(void);
void ANX7150_Parse_NativeFormat(void);
void ANX7150_Parse_DTDinBlockONE(void);
void ANX7150_Parse_DTDinExtBlock(void);
uint8 ANX7150_Parse_ExtBlock(void);
void ANX7150_GetEDIDLength(void);
uint8 ANX7150_EDID_Checksum(uint8 block_number) ;
void ANX7150_InitDDC_Read(uint8 devaddr, uint8 segmentpointer,uint8 offset, uint8  access_num_Low,uint8 access_num_high);
uint8 ANX7150_Read_EDID_uint8(uint8 segmentpointer,uint8 offset);
uint8 ANX7150_Chip_Located(void);

/*added by gerard.zhu*/
uint8 ANX7150_DDC_Parameter_Validity(uint8 * Addr, WORD length);
void ANX7150_DDC_Set_Address(ANX7150_DDC_Addr ddc_address, ANX7150_DDC_Type ddc_type);
void ANX7150_DDC_Set_Number(WORD length);
void ANX7150_DDC_Command(uint8 DDC_command);
uint8 ANX7150_DDC_Check_Status(ANX7150_DDC_Status_Check_Type DDC_status_need_type, uint8 status_uint8);
uint8 ANX7150_DDC_Count_Compare(uint8 length);
uint8 ANX7150_DDC_Read(ANX7150_DDC_Addr ddc_address, const uint8 *DDC_data, WORD length, ANX7150_DDC_Type DDC_type);
//uint8 ANX7150_DDC_Write(ANX7150_DDC_Addr ddc_address, const uint8 *DDC_data, WORD length, ANX7150_DDC_Type DDC_type);//wen
/*end*/

//void Write_data_to_EE();



uint8 ANX7150_IS_KSVList_VLD(void);
//void ANX7150_IS_KSVFIFO_Ready(void);//wen
//uint8 ANX7150_Check_KSV_SRM(void);//wen
void ANX7150_Task(void);

void  HDMI_System_Init(void);
void HDMI_System_Task(void);



//user interface define ends


void ANX7150_API_System_Config(uint8 video_id,uint8 repeation_mode,uint8 sample_rate);
void ANX7150_API_Video_Config(uint8 video_id,uint8 input_pixel_rpt_time);
void ANX7150_API_AUD_CHStatus_Config(uint8 MODE,uint8 PCM_MODE,uint8 SW_CPRGT,uint8 NON_PCM,
    uint8 PROF_APP,uint8 CAT_CODE,uint8 CH_NUM,uint8 SOURCE_NUM,uint8 CLK_ACCUR,uint8 Fs);
uint8 ANX7150_API_DetectDevice(void);
void ANX7150_API_HoldSystemConfig(uint8 bHold_ANX7150);
void ANX7150_API_HDCP_ONorOFF(uint8 HDCP_ONorOFF);
void ANX7150_API_Packets_Config(uint8 pkt_sel);
void ANX7150_API_AVI_Config(uint8 pb1,uint8 pb2,uint8 pb3,uint8 pb4,uint8 pb5,
    uint8 pb6,uint8 pb7,uint8 pb8,uint8 pb9,uint8 pb10,uint8 pb11,uint8 pb12,uint8 pb13);
void ANX7150_API_AUD_INFO_Config(uint8 pb1,uint8 pb2,uint8 pb3,uint8 pb4,uint8 pb5,
    uint8 pb6,uint8 pb7,uint8 pb8,uint8 pb9,uint8 pb10);
//void ANX7150_User_Change_Input ();		//wen
void ANX7150_API_Set_AVMute(void);
void ANX7150_API_Clean_HDCP(void);
void ANX7150_API_Audio_Config(uint8 aud_fs);
//void Audio_MCLK_FS_CFG(uint8 FREQ_MCLK);


void Set_IIS_CH(uint8 ch);
#endif



