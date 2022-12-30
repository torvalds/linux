/**
 ****************************************************************************************
 *
 * @file ecrnx_calibration_data.h
 *
 * @brief Calibration Data function declarations
 *
 * Copyright (C) ESWIN 2015-2020
 *
 ****************************************************************************************
 */

#ifndef _ECRNX_CALIBRATION_H_
#define _ECRNX_CALIBRATION_H_
#include "lmac_types.h"
/**
 * INCLUDE FILES
 ****************************************************************************************
 */

/**
 * DEFINES
 ****************************************************************************************
 */
#define CAL_MAC_ADDR_LEN 6
#define CAL_FORMAT_CLASS 3
#define CAL_MCS_CNT      10
#define CAL_CHAN_CNT     3
#define IS_LOW_CHAN(ch)  ((ch) >= 1 && (ch) <= 4)
#define IS_MID_CHAN(ch)  ((ch) >= 5 && (ch) <= 8)
#define IS_HIG_CHAN(ch)  ((ch) >= 9 && (ch) <= 14)
#define GAIN_DELTA_CFG_BUF_SIZE (CAL_FORMAT_MAX * CAL_MCS_CNT)


typedef enum {
    CHAN_LEVEL_LOW,
    CHAN_LEVEL_MID,
    CHAN_LEVEL_HIGH,
    CHAN_LEVEL_MAX,
} chan_level_e;

typedef enum {
    CAL_FORMAT_11B,
    CAL_FORMAT_11G,
    CAL_FORMAT_11N,
    CAL_FORMAT_11N_40M,
    CAL_FORMAT_11AX,
    CAL_FORMAT_MAX
} wifi_cal_format_e;

typedef struct {
    uint8_t gain[CHAN_LEVEL_MAX][CAL_FORMAT_MAX][CAL_MCS_CNT];
}tx_gain_cal_t;

typedef struct {
    uint8_t fine;
    uint8_t corase;
    uint8_t swl;
}cfo_cal_t;

typedef struct {
    tx_gain_cal_t tx_gain;
    cfo_cal_t     cfo_cal;
    uint8_t       mac_addr[CAL_MAC_ADDR_LEN];
    uint32_t      lol;
}wifi_cal_data_t;

/**
 * TYPE DEFINITIONS
 ****************************************************************************************
 */

extern s8 gain_delta[];
extern wifi_cal_data_t cal_result;
/**
 * FUNCTION DECLARATIONS
 ****************************************************************************************
 */

#endif /* _ECRNX_CALIBRATION_H_ */
