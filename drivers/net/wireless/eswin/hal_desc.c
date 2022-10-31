/**
 *****************************************************************************
 *
 * @file hal_desc.c
 *
 * Copyright (C) ESWIN 2015-2020
 *
 *****************************************************************************
 */

#include <linux/string.h>
#include "hal_desc.h"

const struct ecrnx_legrate legrates_lut[] = {
    [0]  = { .idx = 0,  .rate = 10 },
    [1]  = { .idx = 1,  .rate = 20 },
    [2]  = { .idx = 2,  .rate = 55 },
    [3]  = { .idx = 3,  .rate = 110 },
    [4]  = { .idx = -1, .rate = 0 },
    [5]  = { .idx = -1, .rate = 0 },
    [6]  = { .idx = -1, .rate = 0 },
    [7]  = { .idx = -1, .rate = 0 },
    [8]  = { .idx = 10, .rate = 480 },
    [9]  = { .idx = 8,  .rate = 240 },
    [10] = { .idx = 6,  .rate = 120 },
    [11] = { .idx = 4,  .rate = 60 },
    [12] = { .idx = 11, .rate = 540 },
    [13] = { .idx = 9,  .rate = 360 },
    [14] = { .idx = 7,  .rate = 180 },
    [15] = { .idx = 5,  .rate = 90 },
};

/**
 * ecrnx_machw_type - Return type (NX or HE) MAC HW is used
 *
 */
int ecrnx_machw_type(uint32_t machw_version_2)
{
    uint32_t machw_um_ver_maj = (machw_version_2 >> 4) & 0x7;

    if (machw_um_ver_maj >= 4)
        return ECRNX_MACHW_HE;
    else
        return ECRNX_MACHW_NX;
}

/**
 * ecrnx_rx_vector_convert - Convert in place a RX vector from NX hardware into
 * a RX vector formatted by HE hardware.
 *
 * @machw_type: Type of MACHW in use.
 * @rx_vect1: Rx vector 1 descriptor of the received frame.
 * @rx_vect2: Rx vector 2 descriptor of the received frame.
 */
void ecrnx_rx_vector_convert(int machw_type,
                            struct rx_vector_1 *rx_vect1,
                            struct rx_vector_2 *rx_vect2)
{
    struct rx_vector_1_nx rx_vect1_nx;
    struct rx_vector_2_nx rx_vect2_nx;

    // Check if we need to do the conversion. Only if old modem is used
    if (machw_type == ECRNX_MACHW_HE) {
        rx_vect1->rssi1 = rx_vect1->rssi_leg;
        return;
    }

    // Copy the received vector locally
    memcpy(&rx_vect1_nx, rx_vect1, sizeof(struct rx_vector_1_nx));

    // Reset it
    memset(rx_vect1, 0, sizeof(struct rx_vector_1));

    // Perform the conversion
    rx_vect1->format_mod = rx_vect1_nx.format_mod;
    rx_vect1->ch_bw = rx_vect1_nx.ch_bw;
    rx_vect1->pre_type = rx_vect1_nx.pre_type;
    rx_vect1->antenna_set = rx_vect1_nx.antenna_set;
    rx_vect1->leg_length = rx_vect1_nx.leg_length;
    rx_vect1->leg_rate = rx_vect1_nx.leg_rate;
    rx_vect1->rssi1 = rx_vect1_nx.rssi1;

    switch (rx_vect1->format_mod) {
        case FORMATMOD_NON_HT:
        case FORMATMOD_NON_HT_DUP_OFDM:
            rx_vect1->leg.dyn_bw_in_non_ht = rx_vect1_nx.dyn_bw;
            rx_vect1->leg.chn_bw_in_non_ht = rx_vect1_nx.ch_bw;
            rx_vect1->leg.lsig_valid = rx_vect1_nx.lsig_valid;
            break;
        case FORMATMOD_HT_MF:
        case FORMATMOD_HT_GF:
            rx_vect1->ht.sounding = rx_vect1_nx.sounding;
            rx_vect1->ht.smoothing = rx_vect1_nx.smoothing;
            rx_vect1->ht.short_gi = rx_vect1_nx.short_gi;
            rx_vect1->ht.aggregation = rx_vect1_nx.aggregation;
            rx_vect1->ht.stbc = rx_vect1_nx.stbc;
            rx_vect1->ht.num_extn_ss = rx_vect1_nx.num_extn_ss;
            rx_vect1->ht.lsig_valid = rx_vect1_nx.lsig_valid;
            rx_vect1->ht.mcs = rx_vect1_nx.mcs;
            rx_vect1->ht.fec = rx_vect1_nx.fec_coding;
            rx_vect1->ht.length = rx_vect1_nx.ht_length;
            break;
        case FORMATMOD_VHT:
            rx_vect1->vht.sounding = rx_vect1_nx.sounding;
            rx_vect1->vht.beamformed = !rx_vect1_nx.smoothing;
            rx_vect1->vht.short_gi = rx_vect1_nx.short_gi;
            rx_vect1->vht.stbc = rx_vect1_nx.stbc;
            rx_vect1->vht.doze_not_allowed = rx_vect1_nx.doze_not_allowed;
            rx_vect1->vht.first_user = rx_vect1_nx.first_user;
            rx_vect1->vht.partial_aid = rx_vect1_nx.partial_aid;
            rx_vect1->vht.group_id = rx_vect1_nx.group_id;
            rx_vect1->vht.mcs = rx_vect1_nx.mcs;
            rx_vect1->vht.nss = rx_vect1_nx.stbc ? rx_vect1_nx.n_sts/2 : rx_vect1_nx.n_sts;
            rx_vect1->vht.fec = rx_vect1_nx.fec_coding;
            rx_vect1->vht.length = (rx_vect1_nx._ht_length << 16) | rx_vect1_nx.ht_length;
            break;
    }

    if (!rx_vect2)
        return;

    // Copy the received vector 2 locally
    memcpy(&rx_vect2_nx, rx_vect2, sizeof(struct rx_vector_2_nx));

    // Reset it
    memset(rx_vect2, 0, sizeof(struct rx_vector_2));

    rx_vect2->rcpi1 = rx_vect2_nx.rcpi;
    rx_vect2->rcpi2 = rx_vect2_nx.rcpi;
    rx_vect2->rcpi3 = rx_vect2_nx.rcpi;
    rx_vect2->rcpi4 = rx_vect2_nx.rcpi;

    rx_vect2->evm1 = rx_vect2_nx.evm1;
    rx_vect2->evm2 = rx_vect2_nx.evm2;
    rx_vect2->evm3 = rx_vect2_nx.evm3;
    rx_vect2->evm4 = rx_vect2_nx.evm4;
}


/**
 * ecrnx_rx_status_convert - Convert in place a legacy MPDU status from NX hardware
 * into a MPDU status formatted by HE hardware.
 *
 * @machw_type: Type of MACHW in use.
 * @status: Rx MPDU status of the received frame.
 */
void ecrnx_rx_status_convert(int machw_type, struct mpdu_status *status)
{
    struct mpdu_status_nx *status_nx;

    if (machw_type == ECRNX_MACHW_HE)
        return;

    status_nx = (struct mpdu_status_nx *)status;
    status->undef_err = status_nx->undef_err;

    switch (status_nx->decr_status) {
        case ECRNX_RX_HD_NX_DECR_UNENC:
            status->decr_type = ECRNX_RX_HD_DECR_UNENC;
            status->decr_err = 0;
            break;
        case ECRNX_RX_HD_NX_DECR_ICVFAIL:
            status->decr_type = ECRNX_RX_HD_DECR_WEP;
            status->decr_err = 1;
            break;
        case ECRNX_RX_HD_NX_DECR_CCMPFAIL:
        case ECRNX_RX_HD_NX_DECR_AMSDUDISCARD:
            status->decr_type = ECRNX_RX_HD_DECR_CCMP128;
            status->decr_err = 1;
            break;
        case ECRNX_RX_HD_NX_DECR_NULLKEY:
            status->decr_type = ECRNX_RX_HD_DECR_NULLKEY;
            status->decr_err = 1;
            break;
        case ECRNX_RX_HD_NX_DECR_WEPSUCCESS:
            status->decr_type = ECRNX_RX_HD_DECR_WEP;
            status->decr_err = 0;
            break;
        case ECRNX_RX_HD_NX_DECR_TKIPSUCCESS:
            status->decr_type = ECRNX_RX_HD_DECR_TKIP;
            status->decr_err = 0;
            break;
        case ECRNX_RX_HD_NX_DECR_CCMPSUCCESS:
            status->decr_type = ECRNX_RX_HD_DECR_CCMP128;
            status->decr_err = 0;
            break;
    }
}

