#ifndef __MT6626_FM_LIB_H__
#define __MT6626_FM_LIB_H__

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
    mt6626_E1 = 0,
    mt6626_E2
};

enum {
    FM_LONG_ANA = 0,
    FM_SHORT_ANA
};

struct ctrl_word_operation {
    fm_u16 addr;
    fm_u16 and;
    fm_u16 or;
};

enum {
    MT66x6_BT = 0,
    MT66x6_FM,
    MT66x6_MAX
};

enum {
    MT6626_I2S_ON = 0,
    MT6626_I2S_OFF
};

enum {
    MT6626_I2S_MASTER = 0,
    MT6626_I2S_SLAVE
};

enum {
    MT6626_I2S_32K = 0,
    MT6626_I2S_44K,
    MT6626_I2S_48K
};

#endif
