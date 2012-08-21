/* Copyright Statement:
 *
 * This software/firmware and related documentation ("MediaTek Software") are
 * protected under relevant copyright laws. The information contained herein
 * is confidential and proprietary to MediaTek Inc. and/or its licensors.
 * Without the prior written permission of MediaTek inc. and/or its licensors,
 * any reproduction, modification, use or disclosure of MediaTek Software,
 * and information contained herein, in whole or in part, shall be strictly prohibited.
 */
/* MediaTek Inc. (C) 2010. All rights reserved.
 *
 * BY OPENING THIS FILE, RECEIVER HEREBY UNEQUIVOCALLY ACKNOWLEDGES AND AGREES
 * THAT THE SOFTWARE/FIRMWARE AND ITS DOCUMENTATIONS ("MEDIATEK SOFTWARE")
 * RECEIVED FROM MEDIATEK AND/OR ITS REPRESENTATIVES ARE PROVIDED TO RECEIVER ON
 * AN "AS-IS" BASIS ONLY. MEDIATEK EXPRESSLY DISCLAIMS ANY AND ALL WARRANTIES,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE OR NONINFRINGEMENT.
 * NEITHER DOES MEDIATEK PROVIDE ANY WARRANTY WHATSOEVER WITH RESPECT TO THE
 * SOFTWARE OF ANY THIRD PARTY WHICH MAY BE USED BY, INCORPORATED IN, OR
 * SUPPLIED WITH THE MEDIATEK SOFTWARE, AND RECEIVER AGREES TO LOOK ONLY TO SUCH
 * THIRD PARTY FOR ANY WARRANTY CLAIM RELATING THERETO. RECEIVER EXPRESSLY ACKNOWLEDGES
 * THAT IT IS RECEIVER'S SOLE RESPONSIBILITY TO OBTAIN FROM ANY THIRD PARTY ALL PROPER LICENSES
 * CONTAINED IN MEDIATEK SOFTWARE. MEDIATEK SHALL ALSO NOT BE RESPONSIBLE FOR ANY MEDIATEK
 * SOFTWARE RELEASES MADE TO RECEIVER'S SPECIFICATION OR TO CONFORM TO A PARTICULAR
 * STANDARD OR OPEN FORUM. RECEIVER'S SOLE AND EXCLUSIVE REMEDY AND MEDIATEK'S ENTIRE AND
 * CUMULATIVE LIABILITY WITH RESPECT TO THE MEDIATEK SOFTWARE RELEASED HEREUNDER WILL BE,
 * AT MEDIATEK'S OPTION, TO REVISE OR REPLACE THE MEDIATEK SOFTWARE AT ISSUE,
 * OR REFUND ANY SOFTWARE LICENSE FEES OR SERVICE CHARGE PAID BY RECEIVER TO
 * MEDIATEK FOR SUCH MEDIATEK SOFTWARE AT ISSUE.
 *
 * The following software/firmware and/or related documentation ("MediaTek Software")
 * have been modified by MediaTek Inc. All revisions are subject to any receiver's
 * applicable license agreements with MediaTek Inc.
 */

/* alps/ALPS_SW/TRUNK/MAIN/alps/kernel/arch/arm/mach-mt6516/include/mach/fm.h
 *
 * (C) Copyright 2009 
 * MediaTek <www.MediaTek.com>
 * William Chung <William.Chung@MediaTek.com>
 *
 * MT6516 AR10x0 FM Radio Driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef __FM_H__
#define __FM_H__

//#define FMDEBUG

#include <linux/ioctl.h>
#include <linux/time.h>

//scan sort ways
enum{
    FM_SCAN_SORT_NON = 0,
    FM_SCAN_SORT_UP,
    FM_SCAN_SORT_DOWN,
    FM_SCAN_SORT_MAX
};

//*****************************************************************************************
//***********************************FM config for customer ***********************************
//*****************************************************************************************
//RX
#define FMR_RSSI_TH_LONG    0x0301      //FM radio long antenna RSSI threshold(11.375dBuV)
#define FMR_RSSI_TH_SHORT   0x02E0      //FM radio short antenna RSSI threshold(-1dBuV)
#define FMR_CQI_TH          0x00E9      //FM radio Channel quality indicator threshold(0x0000~0x00FF)
#define FMR_SEEK_SPACE      1           //FM radio seek space,1:100KHZ; 2:200KHZ
#define FMR_SCAN_CH_SIZE    40          //FM radio scan max channel size
#define FMR_BAND            1           //FM radio band, 1:87.5MHz~108.0MHz; 2:76.0MHz~90.0MHz; 3:76.0MHz~108.0MHz; 4:special
#define FMR_BAND_FREQ_L     875         //FM radio special band low freq(Default 87.5MHz)
#define FMR_BAND_FREQ_H     1080        //FM radio special band high freq(Default 108.0MHz)
#define FM_SCAN_SORT_SELECT FM_SCAN_SORT_DOWN

//TX
#define FMTX_PWR_LEVEL_MAX  120         //FM transmitter power level, rang: 85db~120db, default 120db

//*****************************************************************************************
//***********************************FM config for engineer ***********************************
//*****************************************************************************************
//RX
#define FMR_MR_TH           0x01BD      //FM radio MR threshold
#define ADDR_SCAN_TH        0xE0        //scan thrshold register
#define ADDR_CQI_TH         0xE1        //scan CQI register

//TX
#define FMTX_SCAN_HOLE_LOW  923         //92.3MHz~95.4MHz should not show to user
#define FMTX_SCAN_HOLE_HIGH 954         //92.3MHz~95.4MHz should not show to user
//*****************************************************************************************

#define FM_NAME             "fm"
#define FM_DEVICE_NAME      "/dev/fm"

// errno
#define FM_SUCCESS      0
#define FM_FAILED       1
#define FM_EPARM        2
#define FM_BADSTATUS    3
#define FM_TUNE_FAILED  4
#define FM_SEEK_FAILED  5
#define FM_BUSY         6
#define FM_SCAN_FAILED  7

// band

#define FM_BAND_UNKNOWN 0
#define FM_BAND_UE      1 // US/Europe band  87.5MHz ~ 108MHz (DEFAULT)
#define FM_BAND_JAPAN   2 // Japan band      76MHz   ~ 90MHz
#define FM_BAND_JAPANW  3 // Japan wideband  76MHZ   ~ 108MHz
#define FM_BAND_SPECIAL 4 // special   band  between 76MHZ   and  108MHz
#define FM_BAND_DEFAULT FM_BAND_UE
#define FM_FREQ_MIN  FMR_BAND_FREQ_L
#define FM_FREQ_MAX  FMR_BAND_FREQ_H
#define FM_RAIDO_BAND FM_BAND_UE
// space
#define FM_SPACE_UNKNOWN    0
#define FM_SPACE_100K       1
#define FM_SPACE_200K       2
#define FM_SPACE_DEFAULT    FMR_SEEK_SPACE
#define FM_SEEK_SPACE FMR_SEEK_SPACE

//max scan chl num
#define FM_MAX_CHL_SIZE FMR_SCAN_CH_SIZE
// auto HiLo
#define FM_AUTO_HILO_OFF    0
#define FM_AUTO_HILO_ON     1

// seek direction
#define FM_SEEK_UP          0
#define FM_SEEK_DOWN        1

#define FM_CHIP_AR1000 0x1000
#define FM_CHIP_MT5192 0x91
#define FM_CHIP_MT5193 0x92
#define FM_CHIP_MT6616 0x6616
#define FM_CHIP_MT6620 0x6620
#define FM_CHIP_MT6626 0x6626
#define FM_CHIP_MT6628 0x6628
#define FM_CHIP_UNSUPPORTED 0xffff

// seek threshold
#define FM_SEEKTH_LEVEL_DEFAULT 4

#define FM_IOC_MAGIC        0xf5 // FIXME: any conflict?

struct fm_tune_parm {
    uint8_t err;
    uint8_t band;
    uint8_t space;
    uint8_t hilo;
    uint16_t freq; // IN/OUT parameter
};

struct fm_seek_parm {
    uint8_t err;
    uint8_t band;
    uint8_t space;
    uint8_t hilo;
    uint8_t seekdir;
    uint8_t seekth;
    uint16_t freq; // IN/OUT parameter
};

struct fm_scan_parm {
    uint8_t  err;
    uint8_t  band;
    uint8_t  space;
    uint8_t  hilo;
    uint16_t freq; // OUT parameter
    uint16_t ScanTBL[16]; //need no less than the chip
    uint16_t ScanTBLSize; //IN/OUT parameter
};

struct fm_ch_rssi{
    uint16_t freq;
    int rssi;
};

struct fm_rssi_req{
    uint16_t num;
    uint16_t read_cnt;
    struct fm_ch_rssi cr[16*16];
};

struct fm_hw_info{
    int chip_id; //chip ID, eg. 6620
    int eco_ver; //chip ECO version, eg. E3
    int rom_ver; //FM DSP rom code version, eg. V2
    int patch_ver; //FM DSP patch version, eg. 1.11
    int reserve;
};

//For RDS feature
typedef struct
{
   uint8_t TP;
   uint8_t TA;
   uint8_t Music;
   uint8_t Stereo;
   uint8_t Artificial_Head;
   uint8_t Compressed;
   uint8_t Dynamic_PTY;
   uint8_t Text_AB;
   uint32_t flag_status;
}RDSFlag_Struct;

typedef struct
{
   uint16_t Month;
   uint16_t Day;
   uint16_t Year;
   uint16_t Hour;
   uint16_t Minute;
   uint8_t Local_Time_offset_signbit;
   uint8_t Local_Time_offset_half_hour;
}CT_Struct;

typedef struct
{
   int16_t AF_Num;
   int16_t AF[2][25];  //100KHz
   uint8_t Addr_Cnt;
   uint8_t isMethod_A;
   uint8_t isAFNum_Get;
}AF_Info;

typedef struct
{
   uint8_t PS[3][8];
   uint8_t Addr_Cnt;
}PS_Info;

typedef struct
{
   uint8_t TextData[4][64];
   uint8_t GetLength;
   uint8_t isRTDisplay;
   uint8_t TextLength;
   uint8_t isTypeA;
   uint8_t BufCnt;
   uint16_t Addr_Cnt;
}RT_Info;

struct rds_raw_data
{
    int dirty; //indicate if the data changed or not
    int len; //the data len form chip
    uint8_t data[146];
};

struct rds_group_cnt
{
    unsigned long total;
    unsigned long groupA[16]; //RDS groupA counter
    unsigned long groupB[16]; //RDS groupB counter
};

enum rds_group_cnt_opcode
{
    RDS_GROUP_CNT_READ = 0,
    RDS_GROUP_CNT_WRITE,
    RDS_GROUP_CNT_RESET,
    RDS_GROUP_CNT_MAX
};

struct rds_group_cnt_req
{
    int err;
    enum rds_group_cnt_opcode op;
    struct rds_group_cnt gc;
};

typedef struct
{
   CT_Struct CT;
   RDSFlag_Struct RDSFlag;
   uint16_t PI;
   uint8_t Switch_TP;
   uint8_t PTY;
   AF_Info AF_Data;
   AF_Info AFON_Data;
   uint8_t Radio_Page_Code;
   uint16_t Program_Item_Number_Code;
   uint8_t Extend_Country_Code;
   uint16_t Language_Code;
   PS_Info PS_Data;
   uint8_t PS_ON[8];   
   RT_Info RT_Data;
   //uint16_t Block_Backup[32][4];
   //uint16_t Group_Cnt[32];
   //uint8_t EINT_Flag;
   //uint8_t RDS_Flag;
   uint16_t event_status; //will use RDSFlag_Struct RDSFlag->flag_status to check which event, is that ok? 
} RDSData_Struct;


//Need care the following definition.
//valid Rds Flag for notify
typedef enum {
   RDS_FLAG_IS_TP              = 0x0001, // Program is a traffic program
   RDS_FLAG_IS_TA              = 0x0002, // Program currently broadcasts a traffic ann.
   RDS_FLAG_IS_MUSIC           = 0x0004, // Program currently broadcasts music
   RDS_FLAG_IS_STEREO          = 0x0008, // Program is transmitted in stereo
   RDS_FLAG_IS_ARTIFICIAL_HEAD = 0x0010, // Program is an artificial head recording
   RDS_FLAG_IS_COMPRESSED      = 0x0020, // Program content is compressed
   RDS_FLAG_IS_DYNAMIC_PTY     = 0x0040, // Program type can change 
   RDS_FLAG_TEXT_AB            = 0x0080  // If this flag changes state, a new radio text 					 string begins
} RdsFlag;

typedef enum {
   RDS_EVENT_FLAGS          = 0x0001, // One of the RDS flags has changed state
   RDS_EVENT_PI_CODE        = 0x0002, // The program identification code has changed
   RDS_EVENT_PTY_CODE       = 0x0004, // The program type code has changed
   RDS_EVENT_PROGRAMNAME    = 0x0008, // The program name has changed
   RDS_EVENT_UTCDATETIME    = 0x0010, // A new UTC date/time is available
   RDS_EVENT_LOCDATETIME    = 0x0020, // A new local date/time is available
   RDS_EVENT_LAST_RADIOTEXT = 0x0040, // A radio text string was completed
   RDS_EVENT_AF             = 0x0080, // Current Channel RF signal strength too weak, need do AF switch  
   RDS_EVENT_AF_LIST        = 0x0100, // An alternative frequency list is ready
   RDS_EVENT_AFON_LIST      = 0x0200, // An alternative frequency list is ready
   RDS_EVENT_TAON           = 0x0400,  // Other Network traffic announcement start
   RDS_EVENT_TAON_OFF       = 0x0800, // Other Network traffic announcement finished.
   RDS_EVENT_RDS            = 0x2000, // RDS Interrupt had arrived durint timer period  
   RDS_EVENT_NO_RDS         = 0x4000, // RDS Interrupt not arrived durint timer period  
   RDS_EVENT_RDS_TIMER      = 0x8000 // Timer for RDS Bler Check. ---- BLER  block error rate
} RdsEvent;

struct fm_rds_tx_parm {
    uint8_t err;
    uint16_t pi;
    uint16_t ps[12]; // 4 ps
    uint16_t other_rds[87];  // 0~29 other groups
    uint8_t other_rds_cnt; // # of other group
};

typedef struct fm_rds_tx_req{
    unsigned char pty;         // 0~31 integer
    unsigned char rds_rbds;    // 0:RDS, 1:RBDS
    unsigned char dyn_pty;     // 0:static, 1:dynamic
    unsigned short pi_code;    // 2-byte hex
    unsigned char ps_buf[8];     // hex buf of PS
    unsigned char ps_len;      // length of PS, must be 0 / 8"
    unsigned char af;          // 0~204, 0:not used, 1~204:(87.5+0.1*af)MHz
    unsigned char ah;          // Artificial head, 0:no, 1:yes
    unsigned char stereo;      // 0:mono, 1:stereo
    unsigned char compress;    // Audio compress, 0:no, 1:yes
    unsigned char tp;          // traffic program, 0:no, 1:yes
    unsigned char ta;          // traffic announcement, 0:no, 1:yes
    unsigned char speech;      // 0:music, 1:speech
}fm_rds_tx_req;

#define TX_SCAN_MAX 10
#define TX_SCAN_MIN 1
struct fm_tx_scan_parm {
    uint8_t  err;
    uint8_t  band;	//87.6~108MHz
    uint8_t  space;
    uint8_t  hilo;
    uint16_t freq; 	// start freq, if less than band min freq, then will use band min freq
    uint8_t	 scandir;
    uint16_t ScanTBL[TX_SCAN_MAX]; 	//need no less than the chip
    uint16_t ScanTBLSize; //IN: desired size, OUT: scan result size 
};

struct fm_gps_rtc_info{
    int             err;            //error number, 0: success, other: err code
    int             retryCnt;       //GPS mnl can decide retry times
    int             ageThd;         //GPS 3D fix time diff threshold
    int             driftThd;       //GPS RTC drift threshold
    struct timeval  tvThd;          //time value diff threshold
    int             age;            //GPS 3D fix time diff
    int             drift;          //GPS RTC drift
    union{
        unsigned long stamp;        //time stamp in jiffies
        struct timeval  tv;         //time stamp value in RTC
    };
    int             flag;           //rw flag
};

typedef enum
{
	FM_I2S_ON = 0,
	FM_I2S_OFF
}fm_i2s_state;

typedef enum
{
	FM_I2S_MASTER = 0,
	FM_I2S_SLAVE
}fm_i2s_mode;

typedef enum
{
	FM_I2S_32K = 0,
	FM_I2S_44K,
	FM_I2S_48K
}fm_i2s_sample;

struct fm_i2s_setting{
    int onoff;
    int mode;
    int sample;
};

typedef enum{
    FM_RX = 0,
    FM_TX = 1
}FM_PWR_T;

#define FM_IOCTL_POWERUP       _IOWR(FM_IOC_MAGIC, 0, struct fm_tune_parm*)
#define FM_IOCTL_POWERDOWN     _IOWR(FM_IOC_MAGIC, 1, int32_t*)
#define FM_IOCTL_TUNE          _IOWR(FM_IOC_MAGIC, 2, struct fm_tune_parm*)
#define FM_IOCTL_SEEK          _IOWR(FM_IOC_MAGIC, 3, struct fm_seek_parm*)
#define FM_IOCTL_SETVOL        _IOWR(FM_IOC_MAGIC, 4, uint32_t*)
#define FM_IOCTL_GETVOL        _IOWR(FM_IOC_MAGIC, 5, uint32_t*)
#define FM_IOCTL_MUTE          _IOWR(FM_IOC_MAGIC, 6, uint32_t*)
#define FM_IOCTL_GETRSSI       _IOWR(FM_IOC_MAGIC, 7, uint32_t*)
#define FM_IOCTL_SCAN          _IOWR(FM_IOC_MAGIC, 8, struct fm_scan_parm*)
#define FM_IOCTL_STOP_SCAN     _IO(FM_IOC_MAGIC,   9)
#define FM_IOCTL_POWERUP_TX    _IOWR(FM_IOC_MAGIC, 20, struct fm_tune_parm*)
#define FM_IOCTL_TUNE_TX       _IOWR(FM_IOC_MAGIC, 21, struct fm_tune_parm*)
#define FM_IOCTL_RDS_TX        _IOWR(FM_IOC_MAGIC, 22, struct fm_rds_tx_parm*)

//IOCTL and struct for test
#define FM_IOCTL_GETCHIPID     _IOWR(FM_IOC_MAGIC, 10, uint16_t*)
#define FM_IOCTL_EM_TEST       _IOWR(FM_IOC_MAGIC, 11, struct fm_em_parm*)
#define FM_IOCTL_RW_REG        _IOWR(FM_IOC_MAGIC, 12, struct fm_ctl_parm*)
#define FM_IOCTL_GETMONOSTERO  _IOWR(FM_IOC_MAGIC, 13, uint16_t*)
#define FM_IOCTL_GETCURPAMD    _IOWR(FM_IOC_MAGIC, 14, uint16_t*)
#define FM_IOCTL_GETGOODBCNT   _IOWR(FM_IOC_MAGIC, 15, uint16_t*)
#define FM_IOCTL_GETBADBNT     _IOWR(FM_IOC_MAGIC, 16, uint16_t*)
#define FM_IOCTL_GETBLERRATIO  _IOWR(FM_IOC_MAGIC, 17, uint16_t*)


//IOCTL for RDS 
#define FM_IOCTL_RDS_ONOFF     _IOWR(FM_IOC_MAGIC, 18, uint16_t*)
#define FM_IOCTL_RDS_SUPPORT   _IOWR(FM_IOC_MAGIC, 19, int32_t*)

#define FM_IOCTL_RDS_SIM_DATA  _IOWR(FM_IOC_MAGIC, 23, uint32_t*)
#define FM_IOCTL_IS_FM_POWERED_UP  _IOWR(FM_IOC_MAGIC, 24, uint32_t*)

//IOCTL for FM Tx
#define FM_IOCTL_TX_SUPPORT    _IOWR(FM_IOC_MAGIC, 25, int32_t*)
#define FM_IOCTL_RDSTX_SUPPORT _IOWR(FM_IOC_MAGIC, 26, int32_t*)
#define FM_IOCTL_RDSTX_ENABLE  _IOWR(FM_IOC_MAGIC, 27, int32_t*)
#define FM_IOCTL_TX_SCAN       _IOWR(FM_IOC_MAGIC, 28, struct fm_tx_scan_parm*)

//IOCTL for FM over BT
#define FM_IOCTL_OVER_BT_ENABLE  _IOWR(FM_IOC_MAGIC, 29, int32_t*)

//IOCTL for FM ANTENNA SWITCH
#define FM_IOCTL_ANA_SWITCH     _IOWR(FM_IOC_MAGIC, 30, int32_t*)
#define FM_IOCTL_GETCAPARRAY  	_IOWR(FM_IOC_MAGIC, 31, int32_t*)

//IOCTL for FM compensation by GPS RTC
#define FM_IOCTL_GPS_RTC_DRIFT  _IOWR(FM_IOC_MAGIC, 32, struct fm_gps_rtc_info*)

//IOCTL for FM I2S Setting
#define FM_IOCTL_I2S_SETTING  _IOWR(FM_IOC_MAGIC, 33, struct fm_i2s_setting*)

#define FM_IOCTL_RDS_GROUPCNT   _IOWR(FM_IOC_MAGIC, 34, struct rds_group_cnt_req*)
#define FM_IOCTL_RDS_GET_LOG    _IOWR(FM_IOC_MAGIC, 35, struct rds_raw_data*)

#define FM_IOCTL_SCAN_GETRSSI   _IOWR(FM_IOC_MAGIC, 36, struct fm_rssi_req*)
#define FM_IOCTL_SETMONOSTERO   _IOWR(FM_IOC_MAGIC, 37, int32_t)
#define FM_IOCTL_GET_HW_INFO    _IOWR(FM_IOC_MAGIC, 38, struct fm_hw_info*)

#define FM_IOCTL_DUMP_REG   _IO(FM_IOC_MAGIC, 0xFF)

enum group_idx {
    mono=0,
    stereo,
    RSSI_threshold,
    HCC_Enable,
    PAMD_threshold,
    Softmute_Enable,
    De_emphasis,
    HL_Side,
    Demod_BW,
    Dynamic_Limiter,
    Softmute_Rate,
    AFC_Enable,
    Softmute_Level,
    Analog_Volume,
    GROUP_TOTAL_NUMS
};
	
enum item_idx {
    Sblend_OFF=0,
    Sblend_ON,  
    ITEM_TOTAL_NUMS
};

struct fm_ctl_parm {
    uint8_t err;
    uint8_t addr;
    uint16_t val;
    uint16_t rw_flag;//0:write, 1:read
};

struct fm_em_parm {
	uint16_t group_idx;
	uint16_t item_idx;
	uint32_t item_value;	
};
#endif // __FM_H__
