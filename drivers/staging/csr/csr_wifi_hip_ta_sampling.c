/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2012
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/

/*
 * ---------------------------------------------------------------------------
 *  FILE:     csr_wifi_hip_ta_sampling.c
 *
 *  PURPOSE:
 *      The traffic analysis sampling module.
 *      This gathers data which is sent to the SME and used to analyse
 *      the traffic behaviour.
 *
 * Provides:
 *      unifi_ta_sampling_init - Initialise the internal state
 *      unifi_ta_sample        - Sampling function, call this for every data packet
 *
 * Calls these external functions which must be provided:
 *      unifi_ta_indicate_sampling - Pass sample data to the SME.
 *      unifi_ta_indicate_protocol - Report certain data packet types to the SME.
 * ---------------------------------------------------------------------------
 */

#include "csr_wifi_hip_card_sdio.h"

/* Maximum number of Tx frames we store each CYCLE_1, for detecting period */
#define TA_MAX_INTERVALS_IN_C1          100

/* Number of intervals in CYCLE_1 (one second), for detecting periodic */
/* Must match size of unifi_TrafficStats.intervals - 1 */
#define TA_INTERVALS_NUM               10

/* Step (in msecs) between intervals, for detecting periodic */
/* We are only interested in periods up to 100ms, i.e. between beacons */
/* This is correct for TA_INTERVALS_NUM=10 */
#define TA_INTERVALS_STEP               10


enum ta_frame_identity
{
    TA_FRAME_UNKNOWN,
    TA_FRAME_ETHERNET_UNINTERESTING,
    TA_FRAME_ETHERNET_INTERESTING
};


#define TA_ETHERNET_TYPE_OFFSET     6
#define TA_LLC_HEADER_SIZE          8
#define TA_IP_TYPE_OFFSET           17
#define TA_UDP_SOURCE_PORT_OFFSET   28
#define TA_UDP_DEST_PORT_OFFSET     (TA_UDP_SOURCE_PORT_OFFSET + 2)
#define TA_BOOTP_CLIENT_MAC_ADDR_OFFSET 64
#define TA_DHCP_MESSAGE_TYPE_OFFSET 278
#define TA_DHCP_MESSAGE_TYPE_ACK    0x05
#define TA_PROTO_TYPE_IP            0x0800
#define TA_PROTO_TYPE_EAP           0x888E
#define TA_PROTO_TYPE_WAI           0x8864
#define TA_PROTO_TYPE_ARP           0x0806
#define TA_IP_TYPE_TCP              0x06
#define TA_IP_TYPE_UDP              0x11
#define TA_UDP_PORT_BOOTPC          0x0044
#define TA_UDP_PORT_BOOTPS          0x0043
#define TA_EAPOL_TYPE_OFFSET        9
#define TA_EAPOL_TYPE_START         0x01

#define snap_802_2                  0xAAAA0300
#define oui_rfc1042                 0x00000000
#define oui_8021h                   0x0000f800
static const u8 aironet_snap[5] = { 0x00, 0x40, 0x96, 0x00, 0x00 };


/*
 * ---------------------------------------------------------------------------
 *  ta_detect_protocol
 *
 *      Internal only.
 *      Detects a specific protocol in a frame and indicates a TA event.
 *
 *  Arguments:
 *      ta              The pointer to the TA module.
 *      direction       The direction of the frame (tx or rx).
 *      data            Pointer to the structure that contains the data.
 *
 *  Returns:
 *      None
 * ---------------------------------------------------------------------------
 */
static enum ta_frame_identity ta_detect_protocol(card_t *card, CsrWifiRouterCtrlProtocolDirection direction,
                                                 const bulk_data_desc_t *data,
                                                 const u8 *saddr,
                                                 const u8 *sta_macaddr)
{
    ta_data_t *tad = &card->ta_sampling;
    CsrUint16 proto;
    CsrUint16 source_port, dest_port;
    CsrWifiMacAddress srcAddress;
    CsrUint32 snap_hdr, oui_hdr;

    if (data->data_length < TA_LLC_HEADER_SIZE)
    {
        return TA_FRAME_UNKNOWN;
    }

    snap_hdr = (((CsrUint32)data->os_data_ptr[0]) << 24) |
               (((CsrUint32)data->os_data_ptr[1]) << 16) |
               (((CsrUint32)data->os_data_ptr[2]) << 8);
    if (snap_hdr != snap_802_2)
    {
        return TA_FRAME_UNKNOWN;
    }

    if (tad->packet_filter & CSR_WIFI_ROUTER_CTRL_TRAFFIC_PACKET_TYPE_CUSTOM)
    {
        /*
         * Here we would use the custom filter to detect interesting frames.
         */
    }

    oui_hdr = (((CsrUint32)data->os_data_ptr[3]) << 24) |
              (((CsrUint32)data->os_data_ptr[4]) << 16) |
              (((CsrUint32)data->os_data_ptr[5]) << 8);
    if ((oui_hdr == oui_rfc1042) || (oui_hdr == oui_8021h))
    {
        proto = (data->os_data_ptr[TA_ETHERNET_TYPE_OFFSET] * 256) +
                data->os_data_ptr[TA_ETHERNET_TYPE_OFFSET + 1];

        /* The only interesting IP frames are the DHCP */
        if (proto == TA_PROTO_TYPE_IP)
        {
            if (data->data_length > TA_IP_TYPE_OFFSET)
            {
                if (tad->packet_filter & CSR_WIFI_ROUTER_CTRL_TRAFFIC_PACKET_TYPE_CUSTOM)
                {
                    ta_l4stats_t *ta_l4stats = &tad->ta_l4stats;
                    u8 l4proto = data->os_data_ptr[TA_IP_TYPE_OFFSET];

                    if (l4proto == TA_IP_TYPE_TCP)
                    {
                        if (direction == CSR_WIFI_ROUTER_CTRL_PROTOCOL_DIRECTION_TX)
                        {
                            ta_l4stats->txTcpBytesCount += data->data_length;
                        }
                        else
                        {
                            ta_l4stats->rxTcpBytesCount += data->data_length;
                        }
                    }
                    else if (l4proto == TA_IP_TYPE_UDP)
                    {
                        if (direction == CSR_WIFI_ROUTER_CTRL_PROTOCOL_DIRECTION_TX)
                        {
                            ta_l4stats->txUdpBytesCount += data->data_length;
                        }
                        else
                        {
                            ta_l4stats->rxUdpBytesCount += data->data_length;
                        }
                    }
                }

                /* detect DHCP frames */
                if (tad->packet_filter & CSR_WIFI_ROUTER_CTRL_TRAFFIC_PACKET_TYPE_DHCP)
                {
                    /* DHCP frames are UDP frames with BOOTP ports */
                    if (data->os_data_ptr[TA_IP_TYPE_OFFSET] == TA_IP_TYPE_UDP)
                    {
                        if (data->data_length > TA_UDP_DEST_PORT_OFFSET)
                        {
                            source_port = (data->os_data_ptr[TA_UDP_SOURCE_PORT_OFFSET] * 256) +
                                          data->os_data_ptr[TA_UDP_SOURCE_PORT_OFFSET + 1];
                            dest_port = (data->os_data_ptr[TA_UDP_DEST_PORT_OFFSET] * 256) +
                                        data->os_data_ptr[TA_UDP_DEST_PORT_OFFSET + 1];

                            if (((source_port == TA_UDP_PORT_BOOTPC) && (dest_port == TA_UDP_PORT_BOOTPS)) ||
                                ((source_port == TA_UDP_PORT_BOOTPS) && (dest_port == TA_UDP_PORT_BOOTPC)))
                            {
                                /* The DHCP should have at least a message type (request, ack, nack, etc) */
                                if (data->data_length > TA_DHCP_MESSAGE_TYPE_OFFSET + 6)
                                {
                                    UNIFI_MAC_ADDRESS_COPY(srcAddress.a, saddr);

                                    if (direction == CSR_WIFI_ROUTER_CTRL_PROTOCOL_DIRECTION_TX)
                                    {
                                        unifi_ta_indicate_protocol(card->ospriv,
                                                                   CSR_WIFI_ROUTER_CTRL_TRAFFIC_PACKET_TYPE_DHCP,
                                                                   direction,
                                                                   &srcAddress);
                                        return TA_FRAME_ETHERNET_UNINTERESTING;
                                    }

                                    /* DHCPACK is a special indication */
                                    if (UNIFI_MAC_ADDRESS_CMP(data->os_data_ptr + TA_BOOTP_CLIENT_MAC_ADDR_OFFSET, sta_macaddr) == TRUE)
                                    {
                                        if (data->os_data_ptr[TA_DHCP_MESSAGE_TYPE_OFFSET] == TA_DHCP_MESSAGE_TYPE_ACK)
                                        {
                                            unifi_ta_indicate_protocol(card->ospriv,
                                                                       CSR_WIFI_ROUTER_CTRL_TRAFFIC_PACKET_TYPE_DHCP_ACK,
                                                                       direction,
                                                                       &srcAddress);
                                        }
                                        else
                                        {
                                            unifi_ta_indicate_protocol(card->ospriv,
                                                                       CSR_WIFI_ROUTER_CTRL_TRAFFIC_PACKET_TYPE_DHCP,
                                                                       direction,
                                                                       &srcAddress);
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            return TA_FRAME_ETHERNET_INTERESTING;
        }

        /* detect protocol type EAPOL or WAI (treated as equivalent here) */
        if (tad->packet_filter & CSR_WIFI_ROUTER_CTRL_TRAFFIC_PACKET_TYPE_EAPOL)
        {
            if (TA_PROTO_TYPE_EAP == proto || TA_PROTO_TYPE_WAI == proto)
            {
                if ((TA_PROTO_TYPE_WAI == proto) || (direction != CSR_WIFI_ROUTER_CTRL_PROTOCOL_DIRECTION_TX) ||
                    (data->os_data_ptr[TA_EAPOL_TYPE_OFFSET] == TA_EAPOL_TYPE_START))
                {
                    UNIFI_MAC_ADDRESS_COPY(srcAddress.a, saddr);
                    unifi_ta_indicate_protocol(card->ospriv,
                                               CSR_WIFI_ROUTER_CTRL_TRAFFIC_PACKET_TYPE_EAPOL,
                                               direction, &srcAddress);
                }
                return TA_FRAME_ETHERNET_UNINTERESTING;
            }
        }

        /* detect protocol type 0x0806 (ARP) */
        if (tad->packet_filter & CSR_WIFI_ROUTER_CTRL_TRAFFIC_PACKET_TYPE_ARP)
        {
            if (proto == TA_PROTO_TYPE_ARP)
            {
                UNIFI_MAC_ADDRESS_COPY(srcAddress.a, saddr);
                unifi_ta_indicate_protocol(card->ospriv,
                                           CSR_WIFI_ROUTER_CTRL_TRAFFIC_PACKET_TYPE_ARP,
                                           direction, &srcAddress);
                return TA_FRAME_ETHERNET_UNINTERESTING;
            }
        }

        return TA_FRAME_ETHERNET_INTERESTING;
    }
    else if (tad->packet_filter & CSR_WIFI_ROUTER_CTRL_TRAFFIC_PACKET_TYPE_AIRONET)
    {
        /* detect Aironet frames */
        if (!CsrMemCmp(data->os_data_ptr + 3, aironet_snap, 5))
        {
            UNIFI_MAC_ADDRESS_COPY(srcAddress.a, saddr);
            unifi_ta_indicate_protocol(card->ospriv, CSR_WIFI_ROUTER_CTRL_TRAFFIC_PACKET_TYPE_AIRONET,
                                       direction, &srcAddress);
        }
    }

    return TA_FRAME_ETHERNET_UNINTERESTING;
} /* ta_detect_protocol() */


static void tas_reset_data(ta_data_t *tad)
{
    CsrInt16 i;

    for (i = 0; i < (TA_INTERVALS_NUM + 1); i++)
    {
        tad->stats.intervals[i] = 0;
    }

    tad->stats.rxFramesNum = 0;
    tad->stats.txFramesNum = 0;
    tad->stats.rxBytesCount = 0;
    tad->stats.txBytesCount = 0;
    tad->stats.rxMeanRate = 0;

    tad->rx_sum_rate = 0;

    tad->ta_l4stats.rxTcpBytesCount = 0;
    tad->ta_l4stats.txTcpBytesCount = 0;
    tad->ta_l4stats.rxUdpBytesCount = 0;
    tad->ta_l4stats.txUdpBytesCount = 0;
} /* tas_reset_data() */


/*
 * ---------------------------------------------------------------------------
 *  API.
 *  unifi_ta_sampling_init
 *
 *      (Re)Initialise the Traffic Analysis sampling module.
 *      Resets the counters and timestamps.
 *
 *  Arguments:
 *      tad             Pointer to a ta_data_t structure containing the
 *                      context for this device instance.
 *      drv_priv        An opaque pointer that the TA sampling module will
 *                      pass in call-outs.
 *
 *  Returns:
 *      None.
 * ---------------------------------------------------------------------------
 */
void unifi_ta_sampling_init(card_t *card)
{
    (void)unifi_ta_configure(card, CSR_WIFI_ROUTER_CTRL_TRAFFIC_CONFIG_TYPE_RESET, NULL);

    card->ta_sampling.packet_filter = CSR_WIFI_ROUTER_CTRL_TRAFFIC_PACKET_TYPE_NONE;
    card->ta_sampling.traffic_type = CSR_WIFI_ROUTER_CTRL_TRAFFIC_TYPE_OCCASIONAL;
} /* unifi_ta_sampling_init() */


/*
 * ---------------------------------------------------------------------------
 *  API.
 *  unifi_ta_sample
 *
 *      Sample a data frame for the TA module.
 *      This function stores all the useful information it can extract from
 *      the frame and detects any specific protocols.
 *
 *  Arguments:
 *      tad             The pointer to the TA sampling context struct.
 *      direction       The direction of the frame (rx, tx)
 *      data            Pointer to the frame data
 *      saddr           Source MAC address of frame.
 *      timestamp       Time (in msecs) that the frame was received.
 *      rate            Reported data rate for the rx frame (0 for tx frames)
 *
 *  Returns:
 *      None
 * ---------------------------------------------------------------------------
 */
void unifi_ta_sample(card_t                            *card,
                     CsrWifiRouterCtrlProtocolDirection direction,
                     const bulk_data_desc_t            *data,
                     const u8                    *saddr,
                     const u8                    *sta_macaddr,
                     CsrUint32                          timestamp,
                     CsrUint16                          rate)
{
    ta_data_t *tad = &card->ta_sampling;
    enum ta_frame_identity identity;
    CsrUint32 time_delta;



    /* Step1: Check for specific frames */
    if (tad->packet_filter != CSR_WIFI_ROUTER_CTRL_TRAFFIC_PACKET_TYPE_NONE)
    {
        identity = ta_detect_protocol(card, direction, data, saddr, sta_macaddr);
    }
    else
    {
        identity = TA_FRAME_ETHERNET_INTERESTING;
    }


    /* Step2: Update the information in the current record */
    if (direction == CSR_WIFI_ROUTER_CTRL_PROTOCOL_DIRECTION_RX)
    {
        /* Update the Rx packet count and the throughput count */
        tad->stats.rxFramesNum++;
        tad->stats.rxBytesCount += data->data_length;

        /* Accumulate packet Rx rates for later averaging */
        tad->rx_sum_rate += rate;
    }
    else
    {
        if (identity == TA_FRAME_ETHERNET_INTERESTING)
        {
            /*
             * Store the period between the last and the current frame.
             * There is not point storing more than TA_MAX_INTERVALS_IN_C1 periods,
             * the traffic will be bursty or continuous.
             */
            if (tad->stats.txFramesNum < TA_MAX_INTERVALS_IN_C1)
            {
                CsrUint32 interval;
                CsrUint32 index_in_intervals;

                interval = timestamp - tad->tx_last_ts;
                tad->tx_last_ts = timestamp;
                index_in_intervals = (interval + TA_INTERVALS_STEP / 2 - 1) / TA_INTERVALS_STEP;

                /* If the interval is interesting, update the t1_intervals count */
                if (index_in_intervals <= TA_INTERVALS_NUM)
                {
                    unifi_trace(card->ospriv, UDBG5,
                                "unifi_ta_sample: TX interval=%d index=%d\n",
                                interval, index_in_intervals);
                    tad->stats.intervals[index_in_intervals]++;
                }
            }
        }

        /* Update the Tx packet count... */
        tad->stats.txFramesNum++;
        /* ... and the number of bytes for throughput. */
        tad->stats.txBytesCount += data->data_length;
    }

    /*
     * If more than one second has elapsed since the last report, send
     * another one.
     */
    /* Unsigned subtraction handles wrap-around from 0xFFFFFFFF to 0 */
    time_delta = timestamp - tad->last_indication_time;
    if (time_delta >= 1000)
    {
        /*
         * rxFramesNum can be flashed in tas_reset_data() by another thread.
         * Use a temp to avoid division by zero.
         */
        CsrUint32 temp_rxFramesNum;
        temp_rxFramesNum = tad->stats.rxFramesNum;

        /* Calculate this interval's mean frame Rx rate from the sum */
        if (temp_rxFramesNum)
        {
            tad->stats.rxMeanRate = tad->rx_sum_rate / temp_rxFramesNum;
        }
        unifi_trace(card->ospriv, UDBG5,
                    "unifi_ta_sample: RX fr=%lu, r=%u, sum=%lu, av=%lu\n",
                    tad->stats.rxFramesNum, rate,
                    tad->rx_sum_rate, tad->stats.rxMeanRate);

        /*
         * Send the information collected in the stats struct
         * to the SME and reset the counters.
         */
        if (tad->packet_filter & CSR_WIFI_ROUTER_CTRL_TRAFFIC_PACKET_TYPE_CUSTOM)
        {
            CsrUint32 rxTcpThroughput = tad->ta_l4stats.rxTcpBytesCount / time_delta;
            CsrUint32 txTcpThroughput = tad->ta_l4stats.txTcpBytesCount / time_delta;
            CsrUint32 rxUdpThroughput = tad->ta_l4stats.rxUdpBytesCount / time_delta;
            CsrUint32 txUdpThroughput = tad->ta_l4stats.txUdpBytesCount / time_delta;

            unifi_ta_indicate_l4stats(card->ospriv,
                                      rxTcpThroughput,
                                      txTcpThroughput,
                                      rxUdpThroughput,
                                      txUdpThroughput
                                      );
        }
        unifi_ta_indicate_sampling(card->ospriv, &tad->stats);
        tas_reset_data(tad);
        tad->last_indication_time = timestamp;
    }
} /* unifi_ta_sample() */


/*
 * ---------------------------------------------------------------------------
 *  External API.
 *  unifi_ta_configure
 *
 *      Configures the TA module parameters.
 *
 *  Arguments:
 *      ta              The pointer to the TA module.
 *      config_type     The type of the configuration request
 *      config          Pointer to the configuration parameters.
 *
 *  Returns:
 *      CSR_RESULT_SUCCESS on success, CSR error code otherwise
 * ---------------------------------------------------------------------------
 */
CsrResult unifi_ta_configure(card_t                               *card,
                             CsrWifiRouterCtrlTrafficConfigType    config_type,
                             const CsrWifiRouterCtrlTrafficConfig *config)
{
    ta_data_t *tad = &card->ta_sampling;

    /* Reinitialise our data when we are reset */
    if (config_type == CSR_WIFI_ROUTER_CTRL_TRAFFIC_CONFIG_TYPE_RESET)
    {
        /* Reset the stats to zero */
        tas_reset_data(tad);

        /* Reset the timer variables */
        tad->tx_last_ts = 0;
        tad->last_indication_time = 0;

        return CSR_RESULT_SUCCESS;
    }

    if (config_type == CSR_WIFI_ROUTER_CTRL_TRAFFIC_CONFIG_TYPE_FILTER)
    {
        tad->packet_filter = config->packetFilter;

        if (tad->packet_filter & CSR_WIFI_ROUTER_CTRL_TRAFFIC_PACKET_TYPE_CUSTOM)
        {
            tad->custom_filter = config->customFilter;
        }

        return CSR_RESULT_SUCCESS;
    }

    return CSR_RESULT_SUCCESS;
} /* unifi_ta_configure() */


/*
 * ---------------------------------------------------------------------------
 *  External API.
 *  unifi_ta_classification
 *
 *      Configures the current TA classification.
 *
 *  Arguments:
 *      ta              The pointer to the TA module.
 *      traffic_type    The classification type
 *      period          The traffic period if the type is periodic
 *
 *  Returns:
 *      None
 * ---------------------------------------------------------------------------
 */
void unifi_ta_classification(card_t                      *card,
                             CsrWifiRouterCtrlTrafficType traffic_type,
                             CsrUint16                    period)
{
    unifi_trace(card->ospriv, UDBG3,
                "Changed current ta classification to: %d\n", traffic_type);

    card->ta_sampling.traffic_type = traffic_type;
}


