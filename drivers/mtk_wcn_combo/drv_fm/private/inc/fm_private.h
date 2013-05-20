#ifndef __FM_PRIVATE_H__
#define __FM_PRIVATE_H__

#include "fm_typedef.h"

typedef enum fm_priv_state {
    UNINITED,
    INITED
} fm_priv_state_t;

typedef enum fm_adpll_state {
    FM_ADPLL_ON,
    FM_ADPLL_OFF
} fm_adpll_state_t;

typedef enum fm_hl_dese {
    FM_HL_DESE_LOW,
    FM_HL_DESE_HIGH
} fm_hl_dese_t;

typedef enum fm_adpll_clk {
    FM_ADPLL_16M,
    FM_ADPLL_15M
} fm_adpll_clk_t;

typedef enum fm_mcu_desense {
    FM_MCU_DESE_ENABLE,
    FM_MCU_DESE_DISABLE
} fm_mcu_desense_t;

typedef enum fm_gps_desense {
    FM_GPS_DESE_ENABLE,
    FM_GPS_DESE_DISABLE
} fm_gps_desense_t;

//6620
typedef struct MT6620fm_priv_cb {
	//Basic functions.
	int (*hl_side)(uint16_t freq, int *hl);
	int (*adpll_freq_avoid)(uint16_t freq, int *freqavoid);
	int (*mcu_freq_avoid)(uint16_t freq, int *freqavoid);
    int (*tx_pwr_ctrl)(uint16_t freq, int *ctr);
    int (*rtc_drift_ctrl)(uint16_t freq, int *ctr);
    int (*tx_desense_wifi)(uint16_t freq, int *ctr);
    int (*is_dese_chan)(fm_u16 freq);             // check if this is a de-sense channel
}MT6620fm_priv_cb_t;

typedef struct MT6620fm_priv{
    int state;
    void *data;
    MT6620fm_priv_cb_t priv_tbl;
}MT6620fm_priv_t;

//6628
typedef struct fm_priv_cb {
    //De-sense functions.
    fm_s32(*is_dese_chan)(fm_u16 freq);             // check if this is a de-sense channel
    fm_s32(*hl_dese)(fm_u16 freq, void *arg);       // return value: 0, low side; 1, high side; else error no
    fm_s32(*fa_dese)(fm_u16 freq, void *arg);       // return value: 0, fa off; 1, fa on; else error no
    fm_s32(*mcu_dese)(fm_u16 freq, void *arg);      // return value: 0, mcu dese disable; 1, enable; else error no
    fm_s32(*gps_dese)(fm_u16 freq, void *arg);      // return value: 0,mcu dese disable; 1, enable; else error no
    fm_u16(*chan_para_get)(fm_u16 freq);            //get channel parameter, HL side/ FA / ATJ
} fm_priv_cb_t;

typedef struct fm_priv {
    fm_s32 state;
    fm_priv_cb_t priv_tbl;
    void *data;
} fm_priv_t;

typedef struct fm_pub_cb {
    //Basic functions.
    fm_s32(*read)(fm_u8 addr, fm_u16 *val);
    fm_s32(*write)(fm_u8 addr, fm_u16 val);
    fm_s32(*setbits)(fm_u8 addr, fm_u16 bits, fm_u16 mask);
    fm_s32(*rampdown)(void);
    fm_s32(*msdelay)(fm_u32 val);
    fm_s32(*usdelay)(fm_u32 val);
    fm_s32(*log)(const fm_s8 *arg1, ...);
} fm_pub_cb_t;

typedef struct fm_pub {
    fm_s32 state;
    void *data;
    struct fm_pub_cb pub_tbl;
} fm_pub_t;


#if 0//(!defined(MT6620_FM)&&!defined(MT6628_FM))
extern fm_s32 fm_priv_register(struct fm_priv *pri, struct fm_pub *pub);
extern fm_s32 fm_priv_unregister(struct fm_priv *pri, struct fm_pub *pub);
#endif
#endif //__FM_PRIVATE_H__