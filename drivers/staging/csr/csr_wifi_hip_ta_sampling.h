/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2011
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/

/*
 * ---------------------------------------------------------------------------
 *  FILE:     csr_wifi_hip_ta_sampling.h
 *
 *  PURPOSE:
 *      This file contains Traffic Analysis definitions common to the
 *      sampling and analysis modules.
 *
 * ---------------------------------------------------------------------------
 */
#ifndef __TA_SAMPLING_H__
#define __TA_SAMPLING_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "csr_wifi_hip_unifi.h"

typedef struct ta_l4stats
{
    CsrUint32 rxTcpBytesCount;
    CsrUint32 txTcpBytesCount;
    CsrUint32 rxUdpBytesCount;
    CsrUint32 txUdpBytesCount;
} ta_l4stats_t;

/*
 * Context structure to preserve state between calls.
 */

typedef struct ta_data
{
    /* Current packet filter configuration */
    u16 packet_filter;

    /* Current packet custom filter configuration */
    CsrWifiRouterCtrlTrafficFilter custom_filter;

    /* The timestamp of the last tx packet processed. */
    CsrUint32 tx_last_ts;

    /* The timestamp of the last packet processed. */
    CsrUint32 last_indication_time;

    /* Statistics */
    CsrWifiRouterCtrlTrafficStats stats;

    /* Current traffic classification */
    CsrWifiRouterCtrlTrafficType traffic_type;

    /* Sum of packet rx rates for this interval used to calculate mean */
    CsrUint32    rx_sum_rate;
    ta_l4stats_t ta_l4stats;
} ta_data_t;


void unifi_ta_sampling_init(card_t *card);


#ifdef __cplusplus
}
#endif

#endif /* __TA_SAMPLING_H__ */
