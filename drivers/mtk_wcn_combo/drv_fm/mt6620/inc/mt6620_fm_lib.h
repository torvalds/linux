#ifndef __MT6620_FM_LIB_H__
#define __MT6620_FM_LIB_H__

#include "fm_typedef.h"

#define MT6620_VOL_MAX   0x2B	// 43 volume(0-15)
enum {
    DSPPATCH = 0xFFF9,
    USDELAY = 0xFFFA,
    MSDELAY = 0xFFFB,
    HW_VER = 0xFFFD,
    POLL_N = 0xFFFE, //poling check if bit(n) is '0'
    POLL_P = 0xFFFF, //polling check if bit(n) is '1'
};

enum {
    FM_PUS_DSPPATCH = DSPPATCH,
    FM_PUS_USDELAY = USDELAY,
    FM_PUS_MSDELAY = MSDELAY,
    FM_PUS_HW_VER = HW_VER,
    FM_PUS_POLL_N = POLL_N, //poling check if bit(n) is '0'
    FM_PUS_POLL_P = POLL_P, //polling check if bit(n) is '1'
    FM_PUS_MAX
};

enum {
    DSP_PATH = 0x02,
    DSP_COEFF = 0x03,
    DSP_HW_COEFF = 0x04
};

enum IMG_TYPE {
    IMG_WRONG = 0,
    IMG_ROM,
    IMG_PATCH,
    IMG_COEFFICIENT,
    IMG_HW_COEFFICIENT
};

enum {
    mt6620_E1 = 0,
    mt6620_E2
};

/*enum {
    FM_LONG_ANA = 0,
    FM_SHORT_ANA
};*/
enum {
    MT6620_I2S_ON = 0,
    MT6620_I2S_OFF
};

enum {
    MT6620_I2S_MASTER = 0,
    MT6620_I2S_SLAVE
};

enum {
    MT6620_I2S_32K = 0,
    MT6620_I2S_44K,
    MT6620_I2S_48K
};

struct mt6620_fm_i2s_info {
    fm_s32 status;
    fm_s32 mode;
    fm_s32 rate;
};
struct mt6620_fm_softmute_tune_cqi_t 
{  
	fm_u16 ch;				//current frequency
	fm_u16 rssi;              // RSSI of current channel (raw data)
	fm_u16 pamd;              // PAMD of current channel (raw data)
	fm_u16 mr;              // MR of current channel (raw data)
	fm_u16 atdc;              // ATDC of current channel (raw data)
	fm_u16 prx;              // PRX of current channel (raw data)
	fm_u16 smg;              // soft mute gain of current channel (raw data)
};

#define BITn(n) (uint16_t)(1<<(n))
#define MASK(n) (uint16_t)(~(1<<(n)))
//#define HiSideTableSize 1
#define FM_TX_PWR_CTRL_FREQ_THR 890
#define FM_TX_PWR_CTRL_TMP_THR_UP 45
#define FM_TX_PWR_CTRL_TMP_THR_DOWN 0

#define FM_TX_TRACKING_TIME_MAX 10000 //TX VCO tracking time, default 100ms

//#define MT6620_FPGA
//#define FM_MAIN_PGSEL   (0x9F)
/*
#define FM_MAIN_BASE            (0x0)
#define FM_MAIN_BITMAP0         (FM_MAIN_BASE + 0x80)
#define FM_MAIN_BITMAP1         (FM_MAIN_BASE + 0x81)
#define FM_MAIN_BITMAP2         (FM_MAIN_BASE + 0x82)
#define FM_MAIN_BITMAP3         (FM_MAIN_BASE + 0x83)
#define FM_MAIN_BITMAP4         (FM_MAIN_BASE + 0x84)
#define FM_MAIN_BITMAP5         (FM_MAIN_BASE + 0x85)
#define FM_MAIN_BITMAP6         (FM_MAIN_BASE + 0x86)
#define FM_MAIN_BITMAP7         (FM_MAIN_BASE + 0x87)
#define FM_MAIN_BITMAP8         (FM_MAIN_BASE + 0x88)
#define FM_MAIN_BITMAP9         (FM_MAIN_BASE + 0x89)
#define FM_MAIN_BITMAPA         (FM_MAIN_BASE + 0x8a)
#define FM_MAIN_BITMAPB         (FM_MAIN_BASE + 0x8b)
#define FM_MAIN_BITMAPC         (FM_MAIN_BASE + 0x8c)
#define FM_MAIN_BITMAPD         (FM_MAIN_BASE + 0x8d)
#define FM_MAIN_BITMAPE         (FM_MAIN_BASE + 0x8e)
#define FM_MAIN_BITMAPF         (FM_MAIN_BASE + 0x8f)
*/
enum group_idx {
	mono = 0,
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
	Sblend_OFF = 0,
	Sblend_ON,
	ITEM_TOTAL_NUMS
};
	
#endif
