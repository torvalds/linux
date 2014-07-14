/*
 * Copyright (c) 2011-2012 Espressif System.
 *
 *   MAC header
 */

#ifndef _ESP_WMAC_H_
#define _ESP_WMAC_H_

struct esp_mac_rx_ctrl {
        signed rssi:8;
        unsigned rate:4;
        unsigned is_group:1;
        unsigned:1;
        unsigned sig_mode:2;
        unsigned legacy_length:12;
        unsigned damatch0:1;
        unsigned damatch1:1;
        unsigned bssidmatch0:1;
        unsigned bssidmatch1:1;
        unsigned MCS:7;
        unsigned CWB:1;
        unsigned HT_length:16;
        unsigned Smoothing:1;
        unsigned Not_Sounding:1;
        unsigned:1;
        unsigned Aggregation:1;
        unsigned STBC:2;
        unsigned FEC_CODING:1;
        unsigned SGI:1;
        unsigned rxend_state:8;
        unsigned ampdu_cnt:8;
        unsigned channel:4;
        unsigned:12;
};

struct esp_rx_ampdu_len {
        unsigned substate:8;
        unsigned sublen:12;
        unsigned :12;
};

struct esp_tx_ampdu_entry {
        u32 sub_len:12,
            dili_num:7,
            :1,
            null_byte:2,
            data:1,
            enc:1,
            seq:8;
};

//rxend_state flags
#define RX_PYH_ERR_MIN 0x42
#define RX_AGC_ERR_MIN 0x42
#define RX_AGC_ERR_MAX 0x47
#define RX_OFDM_ERR_MIN 0x50
#define RX_OFDM_ERR_MAX 0x58
#define RX_CCK_ERR_MIN 0x59
#define RX_CCK_ERR_MAX 0x5F
#define RX_ABORT 0x80
#define RX_SF_ERR 0x40
#define RX_FCS_ERR 0x41
#define RX_AHBOV_ERR 0xC0
#define RX_BUFOV_ERR 0xC1
#define RX_BUFINV_ERR 0xC2
#define RX_AMPDUSF_ERR 0xC3
#define RX_AMPDUBUFOV_ERR 0xC4
#define RX_MACBBFIFOOV_ERR 0xC5
#define RX_RPBM_ERR 0xC6
#define RX_BTFORCE_ERR 0xC7
#define RX_SECOV_ERR 0xE1
#define RX_SECPROT_ERR0 0xE2
#define RX_SECPROT_ERR1 0xE3
#define RX_SECKEY_ERR 0xE4
#define RX_SECCRLEN_ERR 0xE5
#define RX_SECFIFO_TIMEOUT 0xE6
#define RX_WEPICV_ERR 0xF0
#define RX_TKIPICV_ERR 0xF4
#define RX_TKIPMIC_ERR 0xF5
#define RX_CCMPMIC_ERR 0xF8
#define RX_WAPIMIC_ERR 0xFC

s8 esp_wmac_rate2idx(u8 rate);
bool esp_wmac_rxsec_error(u8 error);

#endif /* _ESP_WMAC_H_ */
