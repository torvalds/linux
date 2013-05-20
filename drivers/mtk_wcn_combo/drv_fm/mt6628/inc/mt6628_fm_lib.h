#ifndef __MT6628_FM_LIB_H__
#define __MT6628_FM_LIB_H__

#include "fm_typedef.h"

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
    mt6628_E1 = 0,
    mt6628_E2
};

enum {
    FM_LONG_ANA = 0,
    FM_SHORT_ANA
};

enum {
    MT6628_I2S_ON = 0,
    MT6628_I2S_OFF
};

enum {
    MT6628_I2S_MASTER = 0,
    MT6628_I2S_SLAVE
};

enum {
    MT6628_I2S_32K = 0,
    MT6628_I2S_44K,
    MT6628_I2S_48K
};
/*
struct mt6628_fm_i2s_info {
    fm_s32 status;
    fm_s32 mode;
    fm_s32 rate;
};
*/
struct mt6628_fm_cqi {
    fm_u16 ch;
    fm_u16 rssi;
    fm_u16 reserve;
};

struct adapt_fm_cqi {
    fm_s32 ch;
    fm_s32 rssi;
    fm_s32 reserve;
};

struct mt6628_full_cqi {
    fm_u16 ch;
    fm_u16 rssi;
    fm_u16 pamd;
    fm_u16 pr;
    fm_u16 fpamd;
    fm_u16 mr;
    fm_u16 atdc;
    fm_u16 prx;
    fm_u16 atdev;
    fm_u16 smg; // soft-mute gain
    fm_u16 drssi; // delta rssi
};


#endif
