// SPDX-License-Identifier: GPL-2.0
/********************************************************************************
*
*  Copyright (C) 2017 	NEXTCHIP Inc. All rights reserved.
*  Module		: Common header file
*  Description	: This file is common header file
*  Author		:
*  Date         :
*  Version		: Version 2.0
*
********************************************************************************
*  History      :
*
*
********************************************************************************/
#ifndef __COMMON_H__
#define __COMMON_H__

unsigned char nvp6158_I2CReadByte8(unsigned char devaddress, unsigned char address);
void nvp6158_I2CWriteByte8(unsigned char devaddress, unsigned char address, unsigned char data);

#define  gpio_i2c_read   nvp6158_I2CReadByte8
#define  gpio_i2c_write  nvp6158_I2CWriteByte8

//#define STREAM_ON_DEFLAULT

#define I2C_0       (0)
#define I2C_1       (1)
#define I2C_2       (2)
#define I2C_3       (3)

// device address define
#define NVP6158_R0_ID 	0xA1
#define NVP6158C_R0_ID 	0xA0   //6158B AND 6158C USES THE SAME CHIPID,DIFF IN REV_ID
#define NVP6158_REV_ID 	0x00
#define NVP6168_R0_ID 	0xC1
#define NVP6168C_R0_ID 	0xC0
//#define NVP6158C_REV_ID 	0x01
#define CH_PER_CHIP		4

#define NTSC		0x00
#define PAL			0x01

#define AHD_PELCO_16BIT

enum {
    NC_AD_SAMPLE_RATE_8000 = 8000,
    NC_AD_SAMPLE_RATE_16000 = 16000,
    NC_AD_SAMPLE_RATE_32000 = 32000,
    NC_AD_SAMPLE_RATE_44100 = 44100,

    NC_AD_SAMPLE_RATE_MAX,
};

enum {
	NC_AD_AI = 0,
	NC_AD_AOC,

	NC_AD_MAX
};

enum {
	NC_AD_BIT_WIDTH_8 = 0,
	NC_AD_BIT_WIDTH_16,
	NC_AD_BIT_WIDTH_24,

	NC_AD_BIT_WIDTH_MAX
};

//FIXME HI3520 Register
#define VIU_CH_CTRL					0x08
#define VIU_ANC0_START				0x9c
#define VIU_ANC0_SIZE				0xa0
#define VIU_ANC1_START				0xa4
#define VIU_ANC1_SIZE				0xa8
#define VIU_BLANK_DATA_ADDR			0xac

#define IOC_VDEC_GET_EQ_DIST			0x07
#define IOC_VDEC_GET_INPUT_VIDEO_FMT	0x08
#define IOC_VDEC_GET_VIDEO_LOSS     	0x09
#define IOC_VDEC_SET_SYNC		     	0x0A
#define IOC_VDEC_SET_EQUALIZER			0x0B
#define IOC_VDEC_GET_DRIVERVER			0x0C
#define IOC_VDEC_PTZ_ACP_READ			0x0D
#define IOC_VDEC_SET_BRIGHTNESS	    	0x0E
#define IOC_VDEC_SET_CONTRAST   		0x0F
#define IOC_VDEC_SET_HUE    			0x10
#define IOC_VDEC_SET_SATURATION  		0x11
#define IOC_VDEC_SET_SHARPNESS  		0x12
#define IOC_VDEC_SET_CHNMODE    		0x13
#define IOC_VDEC_SET_OUTPORTMODE  		0x14
#define IOC_VDEC_SET_CHDETMODE  		0x15

#define IOC_VDEC_ACP_WRITE              0x2f
#define IOC_VDEC_ACP_WRITE_EXTENTION    0x30
#define IOC_VDEC_PTZ_ACP_READ_EACH_CH	0x31

#define IOC_VDEC_INIT_MOTION			0x40
#define IOC_VDEC_ENABLE_MOTION			0x41
#define IOC_VDEC_DISABLE_MOTION			0x42
#define IOC_VDEC_SET_MOTION_AREA		0x43
#define IOC_VDEC_GET_MOTION_INFO		0x44
#define IOC_VDEC_SET_MOTION_DISPLAY		0x45
#define IOC_VDEC_SET_MOTION_SENS		0x46

#define IOC_AUDIO_SET_CHNNUM            0x80
#define IOC_AUDIO_SET_SAMPLE_RATE       0x81
#define IOC_AUDIO_SET_BITWIDTH          0x82
#define IOC_VDEC_SET_I2C				0x83

#define IOC_VDEC_ACP_POSSIBLE_FIRMUP	0xA0	// by Andy(2016-06-26)
#define IOC_VDEC_ACP_CHECK_ISPSTATUS    0xA1	// by Andy(2016-07-12)
#define IOC_VDEC_ACP_START_FIRMUP	    0xA2	// by Andy(2016-07-12)
#define IOC_VDEC_ACP_FIRMUP				0xA3	// by Andy(2016-06-26)
#define IOC_VDEC_ACP_FIRMUP_END			0xA4	// by Andy(2016-06-26)

#define IOC_VDEC_GET_ADC_CLK			0xB1
#define IOC_VDEC_SET_ADC_CLK			0xB2

/*----------------------- Coaxial protocol  ---------------------*/
// Coax UP Stream - 8bit
#define IOC_VDEC_COAX_TX_INIT			  0xA0
#define IOC_VDEC_COAX_TX_CMD_SEND	  0xA1

// Coax UP Stream - 16bit only ACP 720P Support
#define IOC_VDEC_COAX_TX_16BIT_INIT		  0xB4
#define IOC_VDEC_COAX_TX_16BIT_CMD_SEND	  0xB5
#define IOC_VDEC_COAX_TX_CVI_NEW_CMD_SEND 0xB6

// Coax Down Stream
#define IOC_VDEC_COAX_RX_INIT      0xA2
#define IOC_VDEC_COAX_RX_DATA_READ 0xA3
#define IOC_VDEC_COAX_RX_BUF_CLEAR 0xA4
#define IOC_VDEC_COAX_RX_DEINIT    0xA5

// Coax Test
#define IOC_VDEC_COAX_TEST_TX_INIT_DATA_READ  0xA6
#define IOC_VDEC_COAX_TEST_DATA_SET           0xA7
#define IOC_VDEC_COAX_TEST_DATA_READ          0xA8

// Coax FW Update
#define IOC_VDEC_COAX_FW_ACP_HEADER_GET     0xA9
#define IOC_VDEC_COAX_FW_READY_CMD_SET  0xAA
#define IOC_VDEC_COAX_FW_READY_ACK_GET  0xAB
#define IOC_VDEC_COAX_FW_START_CMD_SET  0xAC
#define IOC_VDEC_COAX_FW_START_ACK_GET  0xAD
#define IOC_VDEC_COAX_FW_SEND_DATA_SET  0xAE
#define IOC_VDEC_COAX_FW_SEND_ACK_GET   0xAF
#define IOC_VDEC_COAX_FW_END_CMD_SET    0xB0
#define IOC_VDEC_COAX_FW_END_ACK_GET    0xB1

// Bank Dump Test
#define IOC_VDEC_COAX_BANK_DUMP_GET    0xB2

// ACP Option
#define IOC_VDEC_COAX_RT_NRT_MODE_CHANGE_SET 0xB3

/*----------------------- MOTION -----------------*/
#define IOC_VDEC_MOTION_SET			0x70
#define IOC_VDEC_MOTION_PIXEL_SET     0x71
#define IOC_VDEC_MOTION_PIXEL_GET     0x72
#define IOC_VDEC_MOTION_TSEN_SET      0x73
#define IOC_VDEC_MOTION_PSEN_SET      0x74
#define IOC_VDEC_MOTION_ALL_PIXEL_SET 0x75
#define IOC_VDEC_MOTION_DETECTION_GET 0x76

typedef struct _nvp6158_video_mode
{
    unsigned int chip;
    unsigned int mode;
	unsigned char vformat[16];
	unsigned char chmode[16];
}nvp6158_video_mode;

typedef struct _nvp6158_chn_mode
{
    unsigned char ch;
	unsigned char vformat;
	unsigned char chmode;
}nvp6158_chn_mode;

typedef struct _nvp6158_opt_mode
{
	unsigned char chipsel;
    unsigned char portsel;
	unsigned char portmode;
	unsigned char chid;
}nvp6158_opt_mode;

typedef struct _nvp6158_input_videofmt
{
    unsigned int inputvideofmt[16];
	unsigned int getvideofmt[16];
	unsigned int geteqstage[16];
	unsigned int getacpdata[16][8];
}nvp6158_input_videofmt;

typedef struct _nvp6158_input_videofmt_ch
{
	unsigned char ch;
	nvp6158_input_videofmt vfmt;
}nvp6158_input_videofmt_ch;

typedef struct _nvp6124_i2c_mode
{
	unsigned char flag;       // 0: read, 1 : write
	unsigned char slaveaddr;
	unsigned char bank;
	unsigned char address;
	unsigned char data;
}nvp6124_i2c_mode;

typedef struct _nvp6158_video_adjust
{
 	unsigned char ch;
	unsigned char value;
}nvp6158_video_adjust;

typedef struct _nvp6158_motion_area
{
    unsigned char ch;
    int m_info[12];
}nvp6158_motion_area;

typedef struct _nvp6158_audio_playback
{
    unsigned char chip;
    unsigned char ch;
}nvp6158_audio_playback;

typedef struct _nvp6158_audio_da_mute
{
    unsigned char chip;
}nvp6158_audio_da_mute;

typedef struct _nvp6158_audio_da_volume
{
    unsigned char chip;
    unsigned char volume;
}nvp6158_audio_da_volume;

typedef struct _nvp6158_audio_format
{
	unsigned char format;   /* 0:i2s; 1:dsp */
    unsigned char mode;   /* 0:slave 1:master*/
	unsigned char dspformat; /*0:dsp;1:ssp*/
    unsigned char clkdir;  /*0:inverted;1:non-inverted*/
	unsigned char chn_num; /*2,4,8,16*/
	unsigned char bitrate; /*0:256fs 1:384fs invalid for nvp6114 2:320fs*/
	unsigned char precision;/*0:16bit;1:8bit*/
	unsigned char samplerate;/*0:8kHZ;1:16kHZ; 2:32kHZ*/
} nvp6158_audio_format;

// by Andy(2016-06-26)
/*typedef struct __file_information
{
	unsigned int	channel;
  	unsigned char 	filename[64];
  	unsigned char 	filePullname[64+32];
  	unsigned int	filesize;
  	unsigned int	filechecksum;			// (sum of file&0x0000FFFFF)
  	unsigned int	currentpacketnum;		// current packet sequnce number(0,1,2........)
  	unsigned int	filepacketnum;			// file packet number = (total size/128bytes), if remain exist, file packet number++
  	unsigned char 	onepacketbuf[128+32];

  	unsigned int	currentFileOffset;		// Current file offset
  	unsigned int	readsize;				// currnet read size

  	unsigned int	ispossiblefirmup[16]; 	// is it possible to update firmware?
  	int 			result;

  	int				appstatus[16];			// Application status

} FIRMWARE_UP_FILE_INFO, *PFIRMWARE_UP_FILE_INFO;
*/
enum __CABLE_TYPE_INFORMATION__
{
	CABLE_TYPE_COAX=0,
	CABLE_TYPE_UTP,

	CABLE_TYPE_MAX
};

enum __DETECTION_TYPE_INFORMATION__
{
	DETECTION_TYPE_AUTO=0,
	DETECTION_TYPE_AHD,
	DETECTION_TYPE_CHD,
	DETECTION_TYPE_THD,
	DETECTION_TYPE_CVBS,

	DETECTION_TYPE_MAX
};

#define NVP6158_IOC_MAGIC            'n'

#define NVP6158_SET_AUDIO_PLAYBACK   		_IOW(NVP6158_IOC_MAGIC, 0x21, nvp6158_audio_playback) 
#define NVP6158_SET_AUDIO_DA_MUTE    		_IOW(NVP6158_IOC_MAGIC, 0x22, nvp6158_audio_da_mute)
#define NVP6158_SET_AUDIO_DA_VOLUME  		_IOW(NVP6158_IOC_MAGIC, 0x23, nvp6158_audio_da_volume)
/*set record format*/
#define NVP6158_SET_AUDIO_R_FORMAT     		_IOW(NVP6158_IOC_MAGIC, 0x24, nvp6158_audio_format)
/*set playback format*/
#define NVP6158_SET_AUDIO_PB_FORMAT     	_IOW(NVP6158_IOC_MAGIC, 0x25, nvp6158_audio_format)

#define _SET_BIT(data,bit) ((data)|=(1<<(bit)))
#define _CLE_BIT(data,bit) ((data)&=(~(1<<(bit))))
//////////////////////////////////////
typedef enum _NC_VIDEO_ONOFF
{
	VIDEO_LOSS_ON = 0,
	VIDEO_LOSS_OFF = 1,

} NC_VIDEO_ONOFF;

typedef struct _decoder_dev_ch_info_s
{
	unsigned char ch;
	unsigned char devnum;
	unsigned char fmt_def;
}decoder_dev_ch_info_s;

typedef enum NC_VIVO_CH_FORMATDEF
{
	NC_VIVO_CH_FORMATDEF_UNKNOWN = 0,
	NC_VIVO_CH_FORMATDEF_AUTO,

	AHD20_SD_H960_NT,   //960h*480i
	AHD20_SD_H960_PAL,  //960h*576i
	AHD20_SD_SH720_NT,  //720h*480i
	AHD20_SD_SH720_PAL, //720h*576i
	AHD20_SD_H1280_NT,
	AHD20_SD_H1280_PAL,
	AHD20_SD_H1440_NT,
	AHD20_SD_H1440_PAL,
	AHD20_SD_H960_EX_NT,  //1920h*480i
	AHD20_SD_H960_EX_PAL, //1920h*576i
	AHD20_SD_H960_2EX_NT,
	AHD20_SD_H960_2EX_PAL,
	AHD20_SD_H960_2EX_Btype_NT,	 //3840h*480i
	AHD20_SD_H960_2EX_Btype_PAL, //3840h*576i

	AHD30_4M_30P,
	AHD30_4M_25P,
	AHD30_4M_15P,
	AHD30_3M_30P,
	AHD30_3M_25P,
	AHD30_3M_18P,	//2048 x 1536
	AHD30_5M_12_5P,	//2592 x 1944
	AHD30_5M_20P,	//2592 x 1944

	AHD30_5_3M_20P,
	AHD30_6M_18P,
	AHD30_6M_20P,
	AHD30_8M_X_30P,
	AHD30_8M_X_25P,
	AHD30_8M_7_5P,
	AHD30_8M_12_5P,
	AHD30_8M_15P,

	TVI_FHD_30P,
	TVI_FHD_25P,
	TVI_HD_60P,
	TVI_HD_50P,
	TVI_HD_30P,
	TVI_HD_25P,
	TVI_HD_30P_EX,
	TVI_HD_25P_EX,
	TVI_HD_B_30P,
	TVI_HD_B_25P,
	TVI_HD_B_30P_EX,
	TVI_HD_B_25P_EX,
	TVI_3M_18P,	//1920 x 1536
	TVI_5M_12_5P,
	TVI_5M_20P,
	TVI_4M_30P,
	TVI_4M_25P,
	TVI_4M_15P,
	TVI_8M_15P, 
	TVI_8M_12_5P,

	CVI_FHD_30P,
	CVI_FHD_25P,
	CVI_HD_60P,
	CVI_HD_50P,
	CVI_HD_30P,
	CVI_HD_25P,
	CVI_HD_30P_EX,
	CVI_HD_25P_EX,
	CVI_4M_30P,
	CVI_4M_25P,
	CVI_5M_20P,
	CVI_8M_15P,
	CVI_8M_12_5P,

	AHD20_1080P_60P,
	AHD20_1080P_50P,
	AHD20_1080P_15P,
	AHD20_1080P_12_5P,

	TVI_FHD_60P,
	TVI_FHD_50P,

	AHD20_960P_30P,
	AHD20_960P_25P,
	AHD20_960P_60P,
	AHD20_960P_50P,

	AHD20_1080P_15P_EX, // Hidden For test
	AHD20_1080P_12_5P_EX, // Hidden For test

	AHD20_720P_15P_2EX_Btype, // Hidden For test
	AHD20_720P_12_5P_2EX_Btype, // Hidden For test

	AHD20_720P_15P_EX_Btype, // Hidden For test
	AHD20_720P_12_5P_EX_Btype, // Hidden For test

	AHD20_1080P_30P,
	AHD20_1080P_25P,

	AHD20_720P_60P,
	AHD20_720P_50P,
	AHD20_720P_30P,
	AHD20_720P_25P,
	AHD20_720P_30P_EX,
	AHD20_720P_25P_EX,
	AHD20_720P_30P_EX_Btype,
	AHD20_720P_25P_EX_Btype,

	NC_VIVO_CH_FORMATDEF_MAX,

} NC_VIVO_CH_FORMATDEF;

typedef enum NC_FORMAT_FPS
{
	FMT_FPS_UNKNOWN = 0,
	FMT_NT = 1,
	FMT_PAL,
	FMT_12_5P,
	FMT_7_5P,
	FMT_30P,
	FMT_25P,
	FMT_50P,
	FMT_60P,
	FMT_15P,
	FMT_18P,
	FMT_18_75P,
	FMT_20P,

	FMT_FPS_MAX,

} NC_FORMAT_FPS;

typedef enum NC_FORMAT_STANDARD
{
	FMT_STD_UNKNOWN = 0,
	FMT_SD,
	FMT_AHD20,
	FMT_AHD30,
	FMT_TVI,
	FMT_CVI,

	FMT_AUTO,		// FIXME

	FMT_STD_MAX,

} NC_FORMAT_STANDARD;

typedef enum NC_FORMAT_RESOLUTION
{
	FMT_RESOL_UNKNOWN = 0,
	FMT_SH720,
	FMT_H960,
	FMT_H1280,
	FMT_H1440,
	FMT_H960_EX,
	FMT_H960_2EX,
	FMT_H960_Btype_2EX,
	FMT_720P,
	FMT_720P_EX,
	FMT_720P_Btype,
	FMT_720P_Btype_EX,
	FMT_1080P,
	FMT_1080P_EX,
	FMT_3M,
	FMT_4M,
	FMT_5M,
	FMT_5_3M,
	FMT_6M,
	FMT_8M_X,
	FMT_8M,

	FMT_H960_Btype_2EX_SP,
	FMT_720P_Btype_EX_SP,

	FMT_RESOL_MAX,

} NC_FORMAT_RESOLUTION;

typedef enum _dvp_mode
{
	BT601 = 0,
	BT656_1MUX,
	BT656_2MUX,
	BT656_4MUX,
	BT656I_TEST_MODES,
	BT1120_1MUX,
	BT1120_2MUX,
	BT1120_4MUX,
	NVP6158_DVP_MODES_END
} NVP6158_DVP_MODE;

typedef struct VDEC_DEV_INFORM_S{

	unsigned char nvp6158_chip_id[4];
	unsigned char chip_rev[4];
	unsigned char chip_addr[4];

	unsigned char Total_Port_Num;
	unsigned char Total_Chip_Cnt;

}VDEC_DEV_INFORM_S;

typedef struct _NVP6158_INFORMATION_S
{
	unsigned char			ch;
	NC_VIVO_CH_FORMATDEF 	curvideofmt[ 16 ];
	NC_VIVO_CH_FORMATDEF 	prevideofmt[ 16 ];
	unsigned char 		 	curvideoloss[ 16 ];
	unsigned char			vfc[16];
	unsigned char			debounce[16][5];
	unsigned char			debounceidx[16];
	VDEC_DEV_INFORM_S 		chipinform;

} NVP6158_INFORMATION_S;

#endif

