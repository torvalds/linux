/**
 ******************************************************************************
 *
 * @file rwnx_tdls.h
 *
 * @brief TDLS function declarations
 *
 * Copyright (C) RivieraWaves 2012-2019
 *
 ******************************************************************************
 */

#ifndef RWNX_TDLS_H_
#define RWNX_TDLS_H_

#include "rwnx_defs.h"

struct ieee_types_header {
    u8 element_id;
    u8 len;
} __packed;

struct ieee_types_bss_co_2040 {
    struct ieee_types_header ieee_hdr;
    u8 bss_2040co;
} __packed;

struct ieee_types_extcap {
    struct ieee_types_header ieee_hdr;
    u8 ext_capab[8];
} __packed;

struct ieee_types_vht_cap {
    struct ieee_types_header ieee_hdr;
    struct ieee80211_vht_cap vhtcap;
} __packed;

struct ieee_types_vht_oper {
    struct ieee_types_header ieee_hdr;
    struct ieee80211_vht_operation vhtoper;
} __packed;

struct ieee_types_aid {
    struct ieee_types_header ieee_hdr;
    u16 aid;
} __packed;

int rwnx_tdls_send_mgmt_packet_data(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif,
                         const u8 *peer, u8 action_code, u8 dialog_token,
                         u16 status_code, u32 peer_capability, bool initiator,
                         const u8 *extra_ies, size_t extra_ies_len, u8 oper_class,
                         struct cfg80211_chan_def *chandef);

#endif /* RWNX_TDLS_H_ */
