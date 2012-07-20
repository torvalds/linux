/*
 * ***************************************************************************
 *  FILE:     unifi_event.c
 *
 *  PURPOSE:
 *      Process the signals received by UniFi.
 *      It is part of the porting exercise.
 *
 * Copyright (C) 2009 by Cambridge Silicon Radio Ltd.
 *
 * Refer to LICENSE.txt included with this source code for details on
 * the license terms.
 *
 * ***************************************************************************
 */


/*
 * Porting notes:
 * The implementation of unifi_receive_event() in Linux is fairly complicated.
 * The linux driver support multiple userspace applications and several
 * build configurations, so the received signals are processed by different
 * processes and multiple times.
 * In a simple implementation, this function needs to deliver:
 * - The MLME-UNITDATA.ind signals to the Rx data plane and to the Traffic
 *   Analysis using unifi_ta_sample().
 * - The MLME-UNITDATA-STATUS.ind signals to the Tx data plane.
 * - All the other signals to the SME using unifi_sys_hip_ind().
 */

#include "csr_wifi_hip_unifi.h"
#include "csr_wifi_hip_conversions.h"
#include "unifi_priv.h"


/*
 * ---------------------------------------------------------------------------
 *  send_to_client
 *
 *      Helper for unifi_receive_event.
 *
 *      This function forwards a signal to one client.
 *
 *  Arguments:
 *      priv        Pointer to driver's private data.
 *      client      Pointer to the client structure.
 *      receiver_id The reciever id of the signal.
 *      sigdata     Pointer to the packed signal buffer.
 *      siglen      Length of the packed signal.
 *      bulkdata    Pointer to the signal's bulk data.
 *
 *  Returns:
 *      None.
 *
 * ---------------------------------------------------------------------------
 */
static void send_to_client(unifi_priv_t *priv, ul_client_t *client,
        int receiver_id,
        unsigned char *sigdata, int siglen,
        const bulk_data_param_t *bulkdata)
{
    if (client && client->event_hook) {
        /*unifi_trace(priv, UDBG3,
                "Receive: client %d, (s:0x%X, r:0x%X) - Signal 0x%.4X \n",
                client->client_id, client->sender_id, receiver_id,
                CSR_GET_UINT16_FROM_LITTLE_ENDIAN(sigdata));*/

        client->event_hook(client, sigdata, siglen, bulkdata, UDI_TO_HOST);
    }
}

/*
 * ---------------------------------------------------------------------------
 *  process_pkt_data_ind
 *
 *      Dispatcher for received signals.
 *
 *      This function receives the 'to host' signals and forwards
 *      them to the unifi linux clients.
 *
 *  Arguments:
 *      priv         Context
 *      sigdata      Pointer to the packed signal buffer(Its in form of MA-PACKET.ind).
 *      bulkdata     Pointer to signal's bulkdata
 *      freeBulkData Pointer to a flag which gets set if the bulkdata needs to
 *                   be freed after calling the logging handlers. If it is not
 *                   set the bulkdata must be freed by the MLME handler or
 *                   passed to the network stack.
 *  Returns:
 *      TRUE if the packet should be routed to the SME etc.
 *      FALSE if the packet is for the driver or network stack
 * ---------------------------------------------------------------------------
 */
static CsrBool check_routing_pkt_data_ind(unifi_priv_t *priv,
        u8 *sigdata,
        const bulk_data_param_t* bulkdata,
        CsrBool *freeBulkData,
        netInterface_priv_t *interfacePriv)
{
    CsrUint16  frmCtrl, receptionStatus, frmCtrlSubType;
    u8 *macHdrLocation;
    u8 interfaceTag;
    CsrBool isDataFrame;
    CsrBool isProtocolVerInvalid = FALSE;
    CsrBool isDataFrameSubTypeNoData = FALSE;

#ifdef CSR_WIFI_SECURITY_WAPI_ENABLE
    static const u8 wapiProtocolIdSNAPHeader[] = {0x88,0xb4};
    static const u8 wapiProtocolIdSNAPHeaderOffset = 6;
    u8 *destAddr;
    u8 *srcAddr;
    CsrBool isWapiUnicastPkt = FALSE;

#ifdef CSR_WIFI_SECURITY_WAPI_QOSCTRL_MIC_WORKAROUND
    CsrUint16 qosControl;
#endif

    u8 llcSnapHeaderOffset = 0;

    destAddr = (u8 *) bulkdata->d[0].os_data_ptr + MAC_HEADER_ADDR1_OFFSET;
    srcAddr  = (u8 *) bulkdata->d[0].os_data_ptr + MAC_HEADER_ADDR2_OFFSET;

    /*Individual/Group bit - Bit 0 of first byte*/
    isWapiUnicastPkt = (!(destAddr[0] & 0x01)) ? TRUE : FALSE;
#endif

#define CSR_WIFI_MA_PKT_IND_RECEPTION_STATUS_OFFSET    sizeof(CSR_SIGNAL_PRIMITIVE_HEADER) + 22

    *freeBulkData = FALSE;

    /* Fetch the MAC header location from  MA_PKT_IND packet */
    macHdrLocation = (u8 *) bulkdata->d[0].os_data_ptr;
    /* Fetch the Frame Control value from  MAC header */
    frmCtrl = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(macHdrLocation);

    /* Pull out interface tag from virtual interface identifier */
    interfaceTag = (CSR_GET_UINT16_FROM_LITTLE_ENDIAN(sigdata + 14)) & 0xff;

    /* check for MIC failure before processing the signal */
    receptionStatus = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(sigdata + CSR_WIFI_MA_PKT_IND_RECEPTION_STATUS_OFFSET);

    /* To discard any spurious MIC failures that could be reported by the firmware */
    isDataFrame = ((frmCtrl & IEEE80211_FC_TYPE_MASK) == (IEEE802_11_FC_TYPE_DATA & IEEE80211_FC_TYPE_MASK)) ? TRUE : FALSE;
    /* 0x00 is the only valid protocol version*/
    isProtocolVerInvalid = (frmCtrl & IEEE80211_FC_PROTO_VERSION_MASK) ? TRUE : FALSE;
    frmCtrlSubType = (frmCtrl & IEEE80211_FC_SUBTYPE_MASK) >> FRAME_CONTROL_SUBTYPE_FIELD_OFFSET;
    /*Exclude the no data & reserved sub-types from MIC failure processing*/
    isDataFrameSubTypeNoData = (((frmCtrlSubType>0x03)&&(frmCtrlSubType<0x08)) || (frmCtrlSubType>0x0B)) ? TRUE : FALSE;
    if ((receptionStatus == CSR_MICHAEL_MIC_ERROR) &&
        ((!isDataFrame) || isProtocolVerInvalid || (isDataFrame && isDataFrameSubTypeNoData))) {
        /* Currently MIC errors are discarded for frames other than data frames. This might need changing when we start
         * supporting 802.11w (Protected Management frames)
         */
        *freeBulkData = TRUE;
        unifi_trace(priv, UDBG4, "Discarding this frame and ignoring the MIC failure as this is a garbage/non-data/no data frame\n");
        return FALSE;
     }

#ifdef CSR_WIFI_SECURITY_WAPI_ENABLE

    if (receptionStatus == CSR_MICHAEL_MIC_ERROR) {

        if (interfacePriv->interfaceMode == CSR_WIFI_ROUTER_CTRL_MODE_STA) {

#ifdef CSR_WIFI_SECURITY_WAPI_QOSCTRL_MIC_WORKAROUND
            if ((isDataFrame) &&
                ((IEEE802_11_FC_TYPE_QOS_DATA & IEEE80211_FC_SUBTYPE_MASK) == (frmCtrl & IEEE80211_FC_SUBTYPE_MASK)) &&
                (priv->isWapiConnection))
            {
            	qosControl = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(macHdrLocation + (((frmCtrl & IEEE802_11_FC_TO_DS_MASK) && (frmCtrl & IEEE802_11_FC_FROM_DS_MASK)) ? 30 : 24) );

            	unifi_trace(priv, UDBG4, "check_routing_pkt_data_ind() :: Value of the QoS control field - 0x%04x \n", qosControl);

                if (qosControl & IEEE802_11_QC_NON_TID_BITS_MASK)
                {
                	unifi_trace(priv, UDBG4, "Ignore the MIC failure and pass the MPDU to the stack when any of bits [4-15] is set in the QoS control field\n");

            		/*Exclude the MIC [16] and the PN [16] that are appended by the firmware*/
            		((bulk_data_param_t*)bulkdata)->d[0].data_length = bulkdata->d[0].data_length - 32;

            		/*Clear the reception status of the signal (CSR_RX_SUCCESS)*/
            		*(sigdata + CSR_WIFI_MA_PKT_IND_RECEPTION_STATUS_OFFSET)     = 0x00;
            		*(sigdata + CSR_WIFI_MA_PKT_IND_RECEPTION_STATUS_OFFSET+1)   = 0x00;

            		*freeBulkData = FALSE;

            		return FALSE;
                }
            }
#endif
            /* If this MIC ERROR reported by the firmware is either for
             *    [1] a WAPI Multicast MPDU and the Multicast filter has NOT been set (It is set only when group key index (MSKID) = 1 in Group Rekeying)   OR
             *    [2] a WAPI Unicast MPDU and either the CONTROL PORT is open or the WAPI Unicast filter or filter(s) is NOT set
             * then report a MIC FAILURE indication to the SME.
             */
#ifndef CSR_WIFI_SECURITY_WAPI_SW_ENCRYPTION
    	if ((priv->wapi_multicast_filter == 0) || isWapiUnicastPkt) {
#else
        /*When SW encryption is enabled and USKID=1 (wapi_unicast_filter = 1), we are expected
		 *to receive MIC failure INDs for unicast MPDUs*/
    	if ( ((priv->wapi_multicast_filter == 0) && !isWapiUnicastPkt) ||
             ((priv->wapi_unicast_filter   == 0) &&  isWapiUnicastPkt) ) {
#endif
                /*Discard the frame*/
                *freeBulkData = TRUE;
                unifi_trace(priv, UDBG4, "Discarding the contents of the frame with MIC failure \n");

                if (isWapiUnicastPkt &&
                    ((uf_sme_port_state(priv,srcAddr,UF_CONTROLLED_PORT_Q,interfaceTag) != CSR_WIFI_ROUTER_CTRL_PORT_ACTION_8021X_PORT_OPEN)||
#ifndef CSR_WIFI_SECURITY_WAPI_SW_ENCRYPTION
                    (priv->wapi_unicast_filter) ||
#endif
                    (priv->wapi_unicast_queued_pkt_filter))) {

                    /* Workaround to handle MIC failures reported by the firmware for encrypted packets from the AP
                     * while we are in the process of re-association induced by unsupported WAPI Unicast key index
                     *             - Discard the packets with MIC failures "until" we have
                     *               a. negotiated a key,
                     *               b. opened the CONTROL PORT and
                     *               c. the AP has started using the new key
                     */
                    unifi_trace(priv, UDBG4, "Ignoring the MIC failure as either a. CONTROL PORT isn't OPEN or b. Unicast filter is set or c. WAPI AP using old key for buffered pkts\n");

                    /*Ignore this MIC failure*/
                    return FALSE;

                }/*WAPI re-key specific workaround*/

                unifi_trace(priv, UDBG6, "check_routing_pkt_data_ind - MIC FAILURE : interfaceTag %x Src Addr %x:%x:%x:%x:%x:%x\n",
                            interfaceTag, srcAddr[0], srcAddr[1], srcAddr[2], srcAddr[3], srcAddr[4], srcAddr[5]);
                unifi_trace(priv, UDBG6, "check_routing_pkt_data_ind - MIC FAILURE : Dest Addr %x:%x:%x:%x:%x:%x\n",
                            destAddr[0], destAddr[1], destAddr[2], destAddr[3], destAddr[4], destAddr[5]);
                unifi_trace(priv, UDBG6, "check_routing_pkt_data_ind - MIC FAILURE : Control Port State - 0x%.4X \n",
                            uf_sme_port_state(priv,srcAddr,UF_CONTROLLED_PORT_Q,interfaceTag));

                unifi_error(priv, "MIC failure in %s\n", __FUNCTION__);

                /*Report the MIC failure to the SME*/
                return TRUE;
            }
        }/* STA mode */
        else {
            /* Its AP Mode . Just Return */
            *freeBulkData = TRUE;
            unifi_error(priv, "MIC failure in %s\n", __FUNCTION__);
            return TRUE;
         } /* AP mode */
    }/* MIC error */
#else
    if (receptionStatus == CSR_MICHAEL_MIC_ERROR) {
        *freeBulkData = TRUE;
        unifi_error(priv, "MIC failure in %s\n", __FUNCTION__);
        return TRUE;
    }
#endif /*CSR_WIFI_SECURITY_WAPI_ENABLE*/

    unifi_trace(priv, UDBG4, "frmCtrl = 0x%04x %s\n",
                frmCtrl,
                (((frmCtrl & 0x000c)>>FRAME_CONTROL_TYPE_FIELD_OFFSET) == IEEE802_11_FRAMETYPE_MANAGEMENT) ?
                    "Mgt" : "Ctrl/Data");

#ifdef CSR_WIFI_SECURITY_WAPI_ENABLE
    /* To ignore MIC failures reported due to the WAPI AP using the old key for queued packets before
     * starting to use the new key negotiated as part of unicast re-keying
     */
    if ((interfacePriv->interfaceMode == CSR_WIFI_ROUTER_CTRL_MODE_STA)&&
        isWapiUnicastPkt &&
        (receptionStatus == CSR_RX_SUCCESS) &&
        (priv->wapi_unicast_queued_pkt_filter==1)) {

        unifi_trace(priv, UDBG6, "check_routing_pkt_data_ind(): WAPI unicast pkt received when the (wapi_unicast_queued_pkt_filter) is set\n");

        if (isDataFrame) {
            switch(frmCtrl & IEEE80211_FC_SUBTYPE_MASK) {
                case IEEE802_11_FC_TYPE_QOS_DATA & IEEE80211_FC_SUBTYPE_MASK:
                    llcSnapHeaderOffset = MAC_HEADER_SIZE + 2;
                    break;
                case IEEE802_11_FC_TYPE_QOS_NULL & IEEE80211_FC_SUBTYPE_MASK:
                case IEEE802_11_FC_TYPE_NULL & IEEE80211_FC_SUBTYPE_MASK:
                    break;
                default:
                    llcSnapHeaderOffset = MAC_HEADER_SIZE;
            }
        }

        if (llcSnapHeaderOffset > 0) {
        	/* QoS data or Data */
            unifi_trace(priv, UDBG6, "check_routing_pkt_data_ind(): SNAP header found & its offset %d\n",llcSnapHeaderOffset);
            if (memcmp((u8 *)(bulkdata->d[0].os_data_ptr+llcSnapHeaderOffset+wapiProtocolIdSNAPHeaderOffset),
                       wapiProtocolIdSNAPHeader,sizeof(wapiProtocolIdSNAPHeader))) {

            	unifi_trace(priv, UDBG6, "check_routing_pkt_data_ind(): This is a data & NOT a WAI protocol packet\n");
                /* On the first unicast data pkt that is decrypted successfully after re-keying, reset the filter */
                priv->wapi_unicast_queued_pkt_filter = 0;
                unifi_trace(priv, UDBG4, "check_routing_pkt_data_ind(): WAPI AP has started using the new unicast key, no more MIC failures expected (reset filter)\n");
            }
            else {
                unifi_trace(priv, UDBG6, "check_routing_pkt_data_ind(): WAPI - This is a WAI protocol packet\n");
            }
        }
	}
#endif


    switch ((frmCtrl & 0x000c)>>FRAME_CONTROL_TYPE_FIELD_OFFSET) {
        case IEEE802_11_FRAMETYPE_MANAGEMENT:
            *freeBulkData = TRUE;       /* Free (after SME handler copies it) */

            /* In P2P device mode, filter the legacy AP beacons here */
            if((interfacePriv->interfaceMode == CSR_WIFI_ROUTER_CTRL_MODE_P2P)&&\
               ((CSR_WIFI_80211_GET_FRAME_SUBTYPE(macHdrLocation)) == CSR_WIFI_80211_FRAME_SUBTYPE_BEACON)){

                u8 *pSsid, *pSsidLen;
                static u8 P2PWildCardSsid[CSR_WIFI_P2P_WILDCARD_SSID_LENGTH] = {'D', 'I', 'R', 'E', 'C', 'T', '-'};

                pSsidLen = macHdrLocation + MAC_HEADER_SIZE + CSR_WIFI_BEACON_FIXED_LENGTH;
                pSsid = pSsidLen + 2;

                if(*(pSsidLen + 1) >= CSR_WIFI_P2P_WILDCARD_SSID_LENGTH){
                    if(memcmp(pSsid, P2PWildCardSsid, CSR_WIFI_P2P_WILDCARD_SSID_LENGTH) == 0){
                        unifi_trace(priv, UDBG6, "Received a P2P Beacon, pass it to SME\n");
                        return TRUE;
                    }
                }
                unifi_trace(priv, UDBG6, "Received a Legacy AP beacon in P2P mode, drop it\n");
                return FALSE;
            }
            return TRUE;                /* Route to SME */
        case IEEE802_11_FRAMETYPE_DATA:
        case IEEE802_11_FRAMETYPE_CONTROL:
            *freeBulkData = FALSE;      /* Network stack or MLME handler frees */
            return FALSE;
        default:
            unifi_error(priv, "Unhandled frame type %04x\n", frmCtrl);
            *freeBulkData = TRUE;       /* Not interested, but must free it */
            return FALSE;
    }
}

/*
 * ---------------------------------------------------------------------------
 *  unifi_process_receive_event
 *
 *      Dispatcher for received signals.
 *
 *      This function receives the 'to host' signals and forwards
 *      them to the unifi linux clients.
 *
 *  Arguments:
 *      ospriv      Pointer to driver's private data.
 *      sigdata     Pointer to the packed signal buffer.
 *      siglen      Length of the packed signal.
 *      bulkdata    Pointer to the signal's bulk data.
 *
 *  Returns:
 *      None.
 *
 *  Notes:
 *  The signals are received in the format described in the host interface
 *  specification, i.e wire formatted. Certain clients use the same format
 *  to interpret them and other clients use the host formatted structures.
 *  Each client has to call read_unpack_signal() to transform the wire
 *  formatted signal into the host formatted signal, if necessary.
 *  The code is in the core, since the signals are defined therefore
 *  binded to the host interface specification.
 * ---------------------------------------------------------------------------
 */
static void
unifi_process_receive_event(void *ospriv,
                            u8 *sigdata, CsrUint32 siglen,
                            const bulk_data_param_t *bulkdata)
{
    unifi_priv_t *priv = (unifi_priv_t*)ospriv;
    int i, receiver_id;
    int client_id;
    CsrInt16 signal_id;
    CsrBool pktIndToSme = FALSE, freeBulkData = FALSE;

    func_enter();

    unifi_trace(priv, UDBG5, "unifi_process_receive_event: "
                "%04x %04x %04x %04x %04x %04x %04x %04x (%d)\n",
                CSR_GET_UINT16_FROM_LITTLE_ENDIAN((sigdata) + sizeof(CsrInt16)*0) & 0xFFFF,
                CSR_GET_UINT16_FROM_LITTLE_ENDIAN((sigdata) + sizeof(CsrInt16)*1) & 0xFFFF,
                CSR_GET_UINT16_FROM_LITTLE_ENDIAN((sigdata) + sizeof(CsrInt16)*2) & 0xFFFF,
                CSR_GET_UINT16_FROM_LITTLE_ENDIAN((sigdata) + sizeof(CsrInt16)*3) & 0xFFFF,
                CSR_GET_UINT16_FROM_LITTLE_ENDIAN((sigdata) + sizeof(CsrInt16)*4) & 0xFFFF,
                CSR_GET_UINT16_FROM_LITTLE_ENDIAN((sigdata) + sizeof(CsrInt16)*5) & 0xFFFF,
                CSR_GET_UINT16_FROM_LITTLE_ENDIAN((sigdata) + sizeof(CsrInt16)*6) & 0xFFFF,
                CSR_GET_UINT16_FROM_LITTLE_ENDIAN((sigdata) + sizeof(CsrInt16)*7) & 0xFFFF,
                siglen);

    receiver_id = CSR_GET_UINT16_FROM_LITTLE_ENDIAN((sigdata) + sizeof(CsrInt16)) & 0xFF00;
    client_id = (receiver_id & 0x0F00) >> UDI_SENDER_ID_SHIFT;
    signal_id = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(sigdata);



    /* check for the type of frame received (checks for 802.11 management frames) */
    if (signal_id == CSR_MA_PACKET_INDICATION_ID)
    {
#define CSR_MA_PACKET_INDICATION_INTERFACETAG_OFFSET    14
        u8 interfaceTag;
        netInterface_priv_t *interfacePriv;

        /* Pull out interface tag from virtual interface identifier */
        interfaceTag = (CSR_GET_UINT16_FROM_LITTLE_ENDIAN(sigdata + CSR_MA_PACKET_INDICATION_INTERFACETAG_OFFSET)) & 0xff;
        interfacePriv = priv->interfacePriv[interfaceTag];

        /* Update activity for this station in case of IBSS */
#ifdef CSR_SUPPORT_SME
        if (interfacePriv->interfaceMode == CSR_WIFI_ROUTER_CTRL_MODE_IBSS)
        {
            u8 *saddr;
            /* Fetch the source address from  mac header */
            saddr = (u8 *) bulkdata->d[0].os_data_ptr + MAC_HEADER_ADDR2_OFFSET;
            unifi_trace(priv, UDBG5,
                                    "Updating sta activity in IBSS interfaceTag %x Src Addr %x:%x:%x:%x:%x:%x\n",
                                    interfaceTag, saddr[0], saddr[1], saddr[2], saddr[3], saddr[4], saddr[5]);

            uf_update_sta_activity(priv, interfaceTag, saddr);
        }
#endif

        pktIndToSme = check_routing_pkt_data_ind(priv, sigdata, bulkdata, &freeBulkData, interfacePriv);

        unifi_trace(priv, UDBG6, "RX: packet entry point to driver from HIP,pkt to SME ?(%s) \n", (pktIndToSme)? "YES":"NO");

    }

    if (pktIndToSme)
    {
        /* Management MA_PACKET_IND for SME */
        if(sigdata != NULL && bulkdata != NULL){
            send_to_client(priv, priv->sme_cli, receiver_id, sigdata, siglen, bulkdata);
        }
        else{
            unifi_error(priv, "unifi_receive_event2: sigdata or Bulkdata is NULL \n");
        }
#ifdef CSR_NATIVE_LINUX
        send_to_client(priv, priv->wext_client,
                receiver_id,
                sigdata, siglen, bulkdata);
#endif
    }
    else
    {
        /* Signals with ReceiverId==0 are also reported to SME / WEXT,
         * unless they are data/control MA_PACKET_INDs or VIF_AVAILABILITY_INDs
         */
        if (!receiver_id) {
               if(signal_id == CSR_MA_VIF_AVAILABILITY_INDICATION_ID) {
                      uf_process_ma_vif_availibility_ind(priv, sigdata, siglen);
               }
               else if (signal_id != CSR_MA_PACKET_INDICATION_ID) {
                      send_to_client(priv, priv->sme_cli, receiver_id, sigdata, siglen, bulkdata);
#ifdef CSR_NATIVE_LINUX
                      send_to_client(priv, priv->wext_client,
                                     receiver_id,
                                     sigdata, siglen, bulkdata);
#endif
               }
               else
               {

#if (defined(CSR_SUPPORT_SME) && defined(CSR_WIFI_SECURITY_WAPI_ENABLE))
                   #define CSR_MA_PACKET_INDICATION_RECEPTION_STATUS_OFFSET    sizeof(CSR_SIGNAL_PRIMITIVE_HEADER) + 22
                   netInterface_priv_t *interfacePriv;
                   u8 interfaceTag;
                   CsrUint16 receptionStatus = CSR_RX_SUCCESS;

                   /* Pull out interface tag from virtual interface identifier */
                   interfaceTag = (CSR_GET_UINT16_FROM_LITTLE_ENDIAN(sigdata + CSR_MA_PACKET_INDICATION_INTERFACETAG_OFFSET)) & 0xff;
                   interfacePriv = priv->interfacePriv[interfaceTag];

                   /* check for MIC failure */
                   receptionStatus = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(sigdata + CSR_MA_PACKET_INDICATION_RECEPTION_STATUS_OFFSET);

                   /* Send a WAPI MPDU to SME for re-check MIC if the respective filter has been set*/
                   if ((!freeBulkData) &&
                       (interfacePriv->interfaceMode == CSR_WIFI_ROUTER_CTRL_MODE_STA) &&
                       (receptionStatus == CSR_MICHAEL_MIC_ERROR) &&
                       ((priv->wapi_multicast_filter == 1)
#ifdef CSR_WIFI_SECURITY_WAPI_SW_ENCRYPTION
                         || (priv->wapi_unicast_filter == 1)
#endif
                       ))
                   {
                       CSR_SIGNAL signal;
                       u8 *destAddr;
                       CsrResult res;
                       CsrUint16 interfaceTag = 0;
                       CsrBool isMcastPkt = TRUE;

                       unifi_trace(priv, UDBG6, "Received a WAPI data packet when the Unicast/Multicast filter is set\n");
                       res = read_unpack_signal(sigdata, &signal);
                       if (res) {
                           unifi_error(priv, "Received unknown or corrupted signal (0x%x).\n",
                                       CSR_GET_UINT16_FROM_LITTLE_ENDIAN(sigdata));
                           return;
                       }

                       /* Check if the type of MPDU and the respective filter status*/
                       destAddr = (u8 *) bulkdata->d[0].os_data_ptr + MAC_HEADER_ADDR1_OFFSET;
                       isMcastPkt = (destAddr[0] & 0x01) ? TRUE : FALSE;
                       unifi_trace(priv, UDBG6,
                                   "1.MPDU type: (%s), 2.Multicast filter: (%s), 3. Unicast filter: (%s)\n",
                                   ((isMcastPkt) ? "Multiast":"Unicast"),
                                   ((priv->wapi_multicast_filter) ? "Enabled":"Disabled"),
                                   ((priv->wapi_unicast_filter)  ? "Enabled":"Disabled"));

                       if (((isMcastPkt) && (priv->wapi_multicast_filter == 1))
#ifdef CSR_WIFI_SECURITY_WAPI_SW_ENCRYPTION
                           || ((!isMcastPkt) && (priv->wapi_unicast_filter == 1))
#endif
                          )
                        {
                            unifi_trace(priv, UDBG4, "Sending the WAPI MPDU for MIC check\n");
                            CsrWifiRouterCtrlWapiRxMicCheckIndSend(priv->CSR_WIFI_SME_IFACEQUEUE, 0, interfaceTag, siglen, sigdata, bulkdata->d[0].data_length, (u8*)bulkdata->d[0].os_data_ptr);

                            for (i = 0; i < UNIFI_MAX_DATA_REFERENCES; i++) {
                                if (bulkdata->d[i].data_length != 0) {
                                    unifi_net_data_free(priv, (void *)&bulkdata->d[i]);
                                }
                           }
                           func_exit();
                           return;
                       }
                   } /* CSR_MA_PACKET_INDICATION_ID */
#endif /*CSR_SUPPORT_SME && CSR_WIFI_SECURITY_WAPI_ENABLE*/
               }
        }

        /* calls the registered clients handler callback func.
         * netdev_mlme_event_handler is one of the registered handler used to route
         * data packet to network stack or AMP/EAPOL related data to SME
         *
         * The freeBulkData check ensures that, it has received a management frame and
         * the frame needs to be freed here. So not to be passed to netdev handler
         */
        if(!freeBulkData){
            if ((client_id < MAX_UDI_CLIENTS) &&
                    (&priv->ul_clients[client_id] != priv->logging_client)) {
            	unifi_trace(priv, UDBG6, "Call the registered clients handler callback func\n");
                send_to_client(priv, &priv->ul_clients[client_id],
                        receiver_id,
                        sigdata, siglen, bulkdata);
            }
        }
    }

    /*
     * Free bulk data buffers here unless it is a CSR_MA_PACKET_INDICATION
     */
    switch (signal_id)
    {
#ifdef UNIFI_SNIFF_ARPHRD
        case CSR_MA_SNIFFDATA_INDICATION_ID:
#endif
            break;

        case CSR_MA_PACKET_INDICATION_ID:
            if (!freeBulkData)
            {
                break;
            }
            /* FALLS THROUGH... */
        default:
            for (i = 0; i < UNIFI_MAX_DATA_REFERENCES; i++) {
                if (bulkdata->d[i].data_length != 0) {
                    unifi_net_data_free(priv, (void *)&bulkdata->d[i]);
                }
            }
    }

    func_exit();
} /* unifi_process_receive_event() */


#ifdef CSR_WIFI_RX_PATH_SPLIT
static CsrBool signal_buffer_is_full(unifi_priv_t* priv)
{
    return (((priv->rxSignalBuffer.writePointer + 1)% priv->rxSignalBuffer.size) == (priv->rxSignalBuffer.readPointer));
}

void unifi_rx_queue_flush(void *ospriv)
{
    unifi_priv_t *priv = (unifi_priv_t*)ospriv;

    func_enter();
    unifi_trace(priv, UDBG4, "rx_wq_handler: RdPtr = %d WritePtr =  %d\n",
                priv->rxSignalBuffer.readPointer,priv->rxSignalBuffer.writePointer);
    if(priv != NULL) {
        u8 readPointer = priv->rxSignalBuffer.readPointer;
        while (readPointer != priv->rxSignalBuffer.writePointer)
        {
             rx_buff_struct_t *buf = &priv->rxSignalBuffer.rx_buff[readPointer];
             unifi_trace(priv, UDBG6, "rx_wq_handler: RdPtr = %d WritePtr =  %d\n",
                         readPointer,priv->rxSignalBuffer.writePointer);
             unifi_process_receive_event(priv, buf->bufptr, buf->sig_len, &buf->data_ptrs);
             readPointer ++;
             if(readPointer >= priv->rxSignalBuffer.size) {
                    readPointer = 0;
             }
        }
        priv->rxSignalBuffer.readPointer = readPointer;
    }
    func_exit();
}

void rx_wq_handler(struct work_struct *work)
{
    unifi_priv_t *priv = container_of(work, unifi_priv_t, rx_work_struct);
    unifi_rx_queue_flush(priv);
}
#endif



/*
 * ---------------------------------------------------------------------------
 *  unifi_receive_event
 *
 *      Dispatcher for received signals.
 *
 *      This function receives the 'to host' signals and forwards
 *      them to the unifi linux clients.
 *
 *  Arguments:
 *      ospriv      Pointer to driver's private data.
 *      sigdata     Pointer to the packed signal buffer.
 *      siglen      Length of the packed signal.
 *      bulkdata    Pointer to the signal's bulk data.
 *
 *  Returns:
 *      None.
 *
 *  Notes:
 *  The signals are received in the format described in the host interface
 *  specification, i.e wire formatted. Certain clients use the same format
 *  to interpret them and other clients use the host formatted structures.
 *  Each client has to call read_unpack_signal() to transform the wire
 *  formatted signal into the host formatted signal, if necessary.
 *  The code is in the core, since the signals are defined therefore
 *  binded to the host interface specification.
 * ---------------------------------------------------------------------------
 */
void
unifi_receive_event(void *ospriv,
                    u8 *sigdata, CsrUint32 siglen,
                    const bulk_data_param_t *bulkdata)
{
#ifdef CSR_WIFI_RX_PATH_SPLIT
    unifi_priv_t *priv = (unifi_priv_t*)ospriv;
    u8 writePointer;
    int i;
    rx_buff_struct_t * rx_buff;
    func_enter();

    unifi_trace(priv, UDBG5, "unifi_receive_event: "
            "%04x %04x %04x %04x %04x %04x %04x %04x (%d)\n",
            CSR_GET_UINT16_FROM_LITTLE_ENDIAN((sigdata) + sizeof(CsrInt16)*0) & 0xFFFF,
            CSR_GET_UINT16_FROM_LITTLE_ENDIAN((sigdata) + sizeof(CsrInt16)*1) & 0xFFFF,
            CSR_GET_UINT16_FROM_LITTLE_ENDIAN((sigdata) + sizeof(CsrInt16)*2) & 0xFFFF,
            CSR_GET_UINT16_FROM_LITTLE_ENDIAN((sigdata) + sizeof(CsrInt16)*3) & 0xFFFF,
            CSR_GET_UINT16_FROM_LITTLE_ENDIAN((sigdata) + sizeof(CsrInt16)*4) & 0xFFFF,
            CSR_GET_UINT16_FROM_LITTLE_ENDIAN((sigdata) + sizeof(CsrInt16)*5) & 0xFFFF,
            CSR_GET_UINT16_FROM_LITTLE_ENDIAN((sigdata) + sizeof(CsrInt16)*6) & 0xFFFF,
            CSR_GET_UINT16_FROM_LITTLE_ENDIAN((sigdata) + sizeof(CsrInt16)*7) & 0xFFFF, siglen);
    if(signal_buffer_is_full(priv)) {
        unifi_error(priv,"TO HOST signal queue FULL dropping the PDU\n");
        for (i = 0; i < UNIFI_MAX_DATA_REFERENCES; i++) {
            if (bulkdata->d[i].data_length != 0) {
                unifi_net_data_free(priv, (void *)&bulkdata->d[i]);
            }
        }
        return;
    }
    writePointer = priv->rxSignalBuffer.writePointer;
    rx_buff = &priv->rxSignalBuffer.rx_buff[writePointer];
    memcpy(rx_buff->bufptr,sigdata,siglen);
    rx_buff->sig_len = siglen;
    rx_buff->data_ptrs = *bulkdata;
    writePointer++;
    if(writePointer >= priv->rxSignalBuffer.size) {
        writePointer =0;
    }
    unifi_trace(priv, UDBG4, "unifi_receive_event:writePtr = %d\n",priv->rxSignalBuffer.writePointer);
    priv->rxSignalBuffer.writePointer = writePointer;

#ifndef CSR_WIFI_RX_PATH_SPLIT_DONT_USE_WQ
    queue_work(priv->rx_workqueue, &priv->rx_work_struct);
#endif

#else
    unifi_process_receive_event(ospriv, sigdata, siglen, bulkdata);
#endif
    func_exit();
} /* unifi_receive_event() */

