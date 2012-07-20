/*
 * ---------------------------------------------------------------------------
 * FILE:     unifi_pdu_processing.c
 *
 * PURPOSE:
 *      This file provides the PDU handling functionality before it gets sent to unfi and after
 *      receiving a PDU from unifi
 *
 * Copyright (C) 2010 by Cambridge Silicon Radio Ltd.
 *
 * Refer to LICENSE.txt included with this source code for details on
 * the license terms.
 *
 * ---------------------------------------------------------------------------
 */


#include <linux/types.h>
#include <linux/etherdevice.h>
#include <linux/vmalloc.h>

#include "csr_wifi_hip_unifi.h"
#include "csr_wifi_hip_conversions.h"
#include "csr_time.h"
#include "unifi_priv.h"
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,13)
#include <net/iw_handler.h>
#endif
#include <net/pkt_sched.h>

#ifdef CSR_SUPPORT_SME
static void _update_buffered_pkt_params_after_alignment(unifi_priv_t *priv, bulk_data_param_t *bulkdata,
                                                        tx_buffered_packets_t* buffered_pkt)
{
    struct sk_buff *skb ;
    u32 align_offset;

    if (priv == NULL || bulkdata == NULL || buffered_pkt == NULL){
        return;
    }

    skb = (struct sk_buff*)bulkdata->d[0].os_net_buf_ptr;
    align_offset = (u32)(long)(bulkdata->d[0].os_data_ptr) & (CSR_WIFI_ALIGN_BYTES-1);
    if(align_offset){
        skb_pull(skb,align_offset);
    }

    buffered_pkt->bulkdata.os_data_ptr = bulkdata->d[0].os_data_ptr;
    buffered_pkt->bulkdata.data_length = bulkdata->d[0].data_length;
    buffered_pkt->bulkdata.os_net_buf_ptr = bulkdata->d[0].os_net_buf_ptr;
    buffered_pkt->bulkdata.net_buf_length = bulkdata->d[0].net_buf_length;
}
#endif

void
unifi_frame_ma_packet_req(unifi_priv_t *priv, CSR_PRIORITY priority,
                          CSR_RATE TransmitRate, CSR_CLIENT_TAG hostTag,
                          u16 interfaceTag, CSR_TRANSMISSION_CONTROL transmissionControl,
                          CSR_PROCESS_ID leSenderProcessId, u8 *peerMacAddress,
                          CSR_SIGNAL *signal)
{

    CSR_MA_PACKET_REQUEST *req = &signal->u.MaPacketRequest;
    netInterface_priv_t *interfacePriv;
    u8 ba_session_idx = 0;
    ba_session_tx_struct *ba_session = NULL;
    u8 *ba_addr = NULL;

    interfacePriv = priv->interfacePriv[interfaceTag];

	unifi_trace(priv, UDBG5,
		"In unifi_frame_ma_packet_req, Frame for Peer: %pMF\n",
		peerMacAddress);
    signal->SignalPrimitiveHeader.SignalId = CSR_MA_PACKET_REQUEST_ID;
    signal->SignalPrimitiveHeader.ReceiverProcessId = 0;
    signal->SignalPrimitiveHeader.SenderProcessId = leSenderProcessId;

    /* Fill the MA-PACKET.req */
    req->Priority = priority;
    unifi_trace(priv, UDBG3, "Tx Frame with Priority: 0x%x\n", req->Priority);

    /* A value of 0 is used for auto selection of rates. But for P2P GO case
     * for action frames the rate is governed by SME. Hence instead of 0,
     * the rate is filled in with the value passed here
     */
    req->TransmitRate = TransmitRate;

    /* packets from netdev then no confirm required but packets from
     * Nme/Sme eapol data frames requires the confirmation
     */
    req->TransmissionControl = transmissionControl;
    req->VirtualInterfaceIdentifier =
           uf_get_vif_identifier(interfacePriv->interfaceMode,interfaceTag);
    memcpy(req->Ra.x, peerMacAddress, ETH_ALEN);

    if (hostTag == 0xffffffff) {
        req->HostTag = interfacePriv->tag++;
        req->HostTag |= 0x40000000;
        unifi_trace(priv, UDBG3, "new host tag assigned = 0x%x\n", req->HostTag);
        interfacePriv->tag &= 0x0fffffff;
    } else {
        req->HostTag = hostTag;
        unifi_trace(priv, UDBG3, "host tag got from SME  = 0x%x\n", req->HostTag);
    }
    /* check if BA session exists for the peer MAC address on same tID */
    if(interfacePriv->interfaceMode == CSR_WIFI_ROUTER_CTRL_MODE_AP ||
       interfacePriv->interfaceMode == CSR_WIFI_ROUTER_CTRL_MODE_P2PGO){
        ba_addr = peerMacAddress;
    }else{
        ba_addr = interfacePriv->bssid.a;
    }
    for (ba_session_idx=0; ba_session_idx < MAX_SUPPORTED_BA_SESSIONS_TX; ba_session_idx++){
        ba_session = interfacePriv->ba_session_tx[ba_session_idx];
        if (ba_session){
           if ((!memcmp(ba_session->macAddress.a, ba_addr, ETH_ALEN)) && (ba_session->tID == priority)){
                req->TransmissionControl |= CSR_ALLOW_BA;
                break;
            }
        }
    }

    unifi_trace(priv, UDBG5, "leaving unifi_frame_ma_packet_req\n");
}

#ifdef CSR_SUPPORT_SME

#define TRANSMISSION_CONTROL_TRIGGER_MASK 0x0001
#define TRANSMISSION_CONTROL_EOSP_MASK 0x0002

static
int frame_and_send_queued_pdu(unifi_priv_t* priv,tx_buffered_packets_t* buffered_pkt,
            CsrWifiRouterCtrlStaInfo_t *staRecord,CsrBool moreData , CsrBool eosp)
{

    CSR_SIGNAL signal;
    bulk_data_param_t bulkdata;
    int result;
    u8 toDs, fromDs, macHeaderLengthInBytes = MAC_HEADER_SIZE;
    u8 *qc;
    u16 *fc = (u16*)(buffered_pkt->bulkdata.os_data_ptr);
    unsigned long lock_flags;
    unifi_trace(priv, UDBG3, "frame_and_send_queued_pdu with moreData: %d , EOSP: %d\n",moreData,eosp);
    unifi_frame_ma_packet_req(priv, buffered_pkt->priority, buffered_pkt->rate, buffered_pkt->hostTag,
               buffered_pkt->interfaceTag, buffered_pkt->transmissionControl,
               buffered_pkt->leSenderProcessId, buffered_pkt->peerMacAddress.a, &signal);
    bulkdata.d[0].os_data_ptr = buffered_pkt->bulkdata.os_data_ptr;
    bulkdata.d[0].data_length = buffered_pkt->bulkdata.data_length;
    bulkdata.d[0].os_net_buf_ptr = buffered_pkt->bulkdata.os_net_buf_ptr;
    bulkdata.d[0].net_buf_length = buffered_pkt->bulkdata.net_buf_length;
    bulkdata.d[1].os_data_ptr = NULL;
    bulkdata.d[1].data_length = 0;
    bulkdata.d[1].os_net_buf_ptr =0;
    bulkdata.d[1].net_buf_length =0;

    if(moreData) {
        *fc |= cpu_to_le16(IEEE802_11_FC_MOREDATA_MASK);
    } else {
        *fc &= cpu_to_le16(~IEEE802_11_FC_MOREDATA_MASK);
    }

    if((staRecord != NULL)&& (staRecord->wmmOrQosEnabled == TRUE))
    {
        unifi_trace(priv, UDBG3, "frame_and_send_queued_pdu WMM Enabled: %d \n",staRecord->wmmOrQosEnabled);

        toDs = (*fc & cpu_to_le16(IEEE802_11_FC_TO_DS_MASK))?1 : 0;
        fromDs = (*fc & cpu_to_le16(IEEE802_11_FC_FROM_DS_MASK))? 1: 0;

        switch(le16_to_cpu(*fc) & IEEE80211_FC_SUBTYPE_MASK)
        {
            case IEEE802_11_FC_TYPE_QOS_DATA & IEEE80211_FC_SUBTYPE_MASK:
            case IEEE802_11_FC_TYPE_QOS_NULL & IEEE80211_FC_SUBTYPE_MASK:
                /* If both are set then the Address4 exists (only for AP) */
                if (fromDs && toDs) {
                    /* 6 is the size of Address4 field */
                    macHeaderLengthInBytes += (QOS_CONTROL_HEADER_SIZE + 6);
                } else {
                    macHeaderLengthInBytes += QOS_CONTROL_HEADER_SIZE;
                }

                /* If order bit set then HT control field is the part of MAC header */
                if (*fc & cpu_to_le16(IEEE80211_FC_ORDER_MASK)) {
                    macHeaderLengthInBytes += HT_CONTROL_HEADER_SIZE;
                    qc = (u8*)(buffered_pkt->bulkdata.os_data_ptr + (macHeaderLengthInBytes-6));
                } else {
                    qc = (u8*)(buffered_pkt->bulkdata.os_data_ptr + (macHeaderLengthInBytes-2));
                }
                *qc = eosp ? *qc | (1 << 4) : *qc & (~(1 << 4));
                break;
            default:
                if (fromDs && toDs)
                    macHeaderLengthInBytes += 6;
        }

    }
    result = ul_send_signal_unpacked(priv, &signal, &bulkdata);
    if(result){
        _update_buffered_pkt_params_after_alignment(priv, &bulkdata,buffered_pkt);
    }

 /* Decrement the packet counts queued in driver */
    if (result != -ENOSPC) {
        /* protect entire counter updation by disabling preemption */
        if (!priv->noOfPktQueuedInDriver) {
            unifi_error(priv, "packets queued in driver 0 still decrementing\n");
        } else {
            spin_lock_irqsave(&priv->tx_q_lock,lock_flags);
            priv->noOfPktQueuedInDriver--;
            spin_unlock_irqrestore(&priv->tx_q_lock,lock_flags);
        }
        /* Sta Record is available for all unicast (except genericMgt Frames) & in other case its NULL */
        if (staRecord) {
            spin_lock_irqsave(&priv->staRecord_lock,lock_flags);
            if (!staRecord->noOfPktQueued) {
                unifi_error(priv, "packets queued in driver per station is 0 still decrementing\n");
            } else {
                staRecord->noOfPktQueued--;
            }
            /* if the STA alive probe frame has failed then reset the saved host tag */
            if (result){
                if (staRecord->nullDataHostTag == buffered_pkt->hostTag){
                    staRecord->nullDataHostTag = INVALID_HOST_TAG;
                }
            }
            spin_unlock_irqrestore(&priv->staRecord_lock,lock_flags);
        }

    }
    return result;
}
#ifdef CSR_SUPPORT_SME
static
void set_eosp_transmit_ctrl(unifi_priv_t *priv, struct list_head *txList)
{
    /* dequeue the tx data packets from the appropriate queue */
    tx_buffered_packets_t *tx_q_item = NULL;
    struct list_head *listHead;
    struct list_head *placeHolder;
    unsigned long lock_flags;


    unifi_trace(priv, UDBG5, "entering set_eosp_transmit_ctrl\n");
    /* check for list empty */
    if (list_empty(txList)) {
        unifi_warning(priv, "In set_eosp_transmit_ctrl, the list is empty\n");
        return;
    }

    /* return the last node , and modify it. */

    spin_lock_irqsave(&priv->tx_q_lock,lock_flags);
    list_for_each_prev_safe(listHead, placeHolder, txList) {
        tx_q_item = list_entry(listHead, tx_buffered_packets_t, q);
        tx_q_item->transmissionControl |= TRANSMISSION_CONTROL_EOSP_MASK;
        tx_q_item->transmissionControl = (tx_q_item->transmissionControl & ~(CSR_NO_CONFIRM_REQUIRED));
        unifi_trace(priv, UDBG1,
                "set_eosp_transmit_ctrl Transmission Control = 0x%x hostTag = 0x%x \n",tx_q_item->transmissionControl,tx_q_item->hostTag);
        unifi_trace(priv,UDBG3,"in set_eosp_transmit_ctrl no.of buffered frames %d\n",priv->noOfPktQueuedInDriver);
        break;
    }
    spin_unlock_irqrestore(&priv->tx_q_lock,lock_flags);
    unifi_trace(priv, UDBG1,"List Empty %d\n",list_empty(txList));
    unifi_trace(priv, UDBG5, "leaving set_eosp_transmit_ctrl\n");
    return;
}

static
void send_vif_availibility_rsp(unifi_priv_t *priv,CSR_VIF_IDENTIFIER vif,CSR_RESULT_CODE resultCode)
{
    CSR_SIGNAL signal;
    CSR_MA_VIF_AVAILABILITY_RESPONSE *rsp;
    bulk_data_param_t *bulkdata = NULL;
    int r;

    unifi_trace(priv, UDBG3, "send_vif_availibility_rsp : invoked with resultCode = %d \n", resultCode);

    memset(&signal,0,sizeof(CSR_SIGNAL));
    rsp = &signal.u.MaVifAvailabilityResponse;
    rsp->VirtualInterfaceIdentifier = vif;
    rsp->ResultCode = resultCode;
    signal.SignalPrimitiveHeader.SignalId = CSR_MA_VIF_AVAILABILITY_RESPONSE_ID;
    signal.SignalPrimitiveHeader.ReceiverProcessId = 0;
    signal.SignalPrimitiveHeader.SenderProcessId = priv->netdev_client->sender_id;

    /* Send the signal to UniFi */
    r = ul_send_signal_unpacked(priv, &signal, bulkdata);
    if(r) {
        unifi_error(priv,"Availibility response sending failed %x status %d\n",vif,r);
    }
    else {
        unifi_trace(priv, UDBG3, "send_vif_availibility_rsp : status = %d \n", r);
    }
}
#endif

static
void verify_and_accomodate_tx_packet(unifi_priv_t *priv)
{
    tx_buffered_packets_t *tx_q_item;
    unsigned long lock_flags;
    struct list_head *listHead, *list;
    struct list_head *placeHolder;
    u8 i, j,eospFramedeleted=0;
    CsrBool thresholdExcedeDueToBroadcast = TRUE;
    /* it will be made it interface Specific in the future when multi interfaces are supported ,
    right now interface 0 is considered */
    netInterface_priv_t *interfacePriv = priv->interfacePriv[0];
    CsrWifiRouterCtrlStaInfo_t *staInfo = NULL;

    unifi_trace(priv, UDBG3, "entering verify_and_accomodate_tx_packet\n");

    for(i = 0; i < UNIFI_MAX_CONNECTIONS; i++) {
        staInfo = interfacePriv->staInfo[i];
            if (staInfo && (staInfo->noOfPktQueued >= CSR_WIFI_DRIVER_MAX_PKT_QUEUING_THRESHOLD_PER_PEER)) {
            /* remove the first(oldest) packet from the all the access catogory, since data
             * packets for station record crossed the threshold limit (64 for AP supporting
             * 8 peers)
             */
            unifi_trace(priv,UDBG3,"number of station pkts queued=  %d for sta id = %d\n", staInfo->noOfPktQueued, staInfo->aid);
            for(j = 0; j < MAX_ACCESS_CATOGORY; j++) {
                list = &staInfo->dataPdu[j];
                spin_lock_irqsave(&priv->tx_q_lock,lock_flags);
                list_for_each_safe(listHead, placeHolder, list) {
                    tx_q_item = list_entry(listHead, tx_buffered_packets_t, q);
                    list_del(listHead);
                    thresholdExcedeDueToBroadcast = FALSE;
                    unifi_net_data_free(priv, &tx_q_item->bulkdata);
                    kfree(tx_q_item);
                    tx_q_item = NULL;
                    if (!priv->noOfPktQueuedInDriver) {
                        unifi_error(priv, "packets queued in driver 0 still decrementing in %s\n", __FUNCTION__);
                    } else {
                        /* protection provided by spinlock */
                        priv->noOfPktQueuedInDriver--;

                    }
                    /* Sta Record is available for all unicast (except genericMgt Frames) & in other case its NULL */
                    if (!staInfo->noOfPktQueued) {
                        unifi_error(priv, "packets queued in driver per station is 0 still decrementing in %s\n", __FUNCTION__);
                    } else {
                        spin_lock(&priv->staRecord_lock);
                        staInfo->noOfPktQueued--;
                        spin_unlock(&priv->staRecord_lock);
                    }
                    break;
                }
                spin_unlock_irqrestore(&priv->tx_q_lock,lock_flags);
            }
        }
    }
    if (thresholdExcedeDueToBroadcast &&  interfacePriv->noOfbroadcastPktQueued > CSR_WIFI_DRIVER_MINIMUM_BROADCAST_PKT_THRESHOLD ) {
        /* Remove the packets from genericMulticastOrBroadCastFrames queue
         * (the max packets in driver is reached due to broadcast/multicast frames)
         */
        spin_lock_irqsave(&priv->tx_q_lock,lock_flags);
        list_for_each_safe(listHead, placeHolder, &interfacePriv->genericMulticastOrBroadCastFrames) {
            tx_q_item = list_entry(listHead, tx_buffered_packets_t, q);
            if(eospFramedeleted){
                tx_q_item->transmissionControl |= TRANSMISSION_CONTROL_EOSP_MASK;
                tx_q_item->transmissionControl = (tx_q_item->transmissionControl & ~(CSR_NO_CONFIRM_REQUIRED));
                unifi_trace(priv, UDBG1,"updating eosp for next packet hostTag:= 0x%x ",tx_q_item->hostTag);
                eospFramedeleted =0;
                break;
            }

            if(tx_q_item->transmissionControl & TRANSMISSION_CONTROL_EOSP_MASK ){
               eospFramedeleted = 1;
            }
            unifi_trace(priv,UDBG1, "freeing of multicast packets ToC = 0x%x hostTag = 0x%x \n",tx_q_item->transmissionControl,tx_q_item->hostTag);
            list_del(listHead);
            unifi_net_data_free(priv, &tx_q_item->bulkdata);
            kfree(tx_q_item);
            priv->noOfPktQueuedInDriver--;
            spin_lock(&priv->staRecord_lock);
            interfacePriv->noOfbroadcastPktQueued--;
            spin_unlock(&priv->staRecord_lock);
            if(!eospFramedeleted){
                break;
            }
        }
        spin_unlock_irqrestore(&priv->tx_q_lock,lock_flags);
    }
    unifi_trace(priv, UDBG3, "leaving verify_and_accomodate_tx_packet\n");
}

static
CsrResult enque_tx_data_pdu(unifi_priv_t *priv, bulk_data_param_t *bulkdata,
                            struct list_head *list, CSR_SIGNAL *signal,
                            CsrBool requeueOnSamePos)
{

    /* queue the tx data packets on to appropriate queue */
    CSR_MA_PACKET_REQUEST *req = &signal->u.MaPacketRequest;
    tx_buffered_packets_t *tx_q_item;
    unsigned long lock_flags;

    unifi_trace(priv, UDBG5, "entering enque_tx_data_pdu\n");
    if(!list) {
       unifi_error(priv,"List is not specified\n");
       return CSR_RESULT_FAILURE;
    }

    /* Removes aged packets & adds the incoming packet */
    if (priv->noOfPktQueuedInDriver >= CSR_WIFI_DRIVER_SUPPORT_FOR_MAX_PKT_QUEUEING) {
        unifi_trace(priv,UDBG3,"number of pkts queued=  %d \n", priv->noOfPktQueuedInDriver);
        verify_and_accomodate_tx_packet(priv);
    }



    tx_q_item = (tx_buffered_packets_t *)kmalloc(sizeof(tx_buffered_packets_t), GFP_ATOMIC);
    if (tx_q_item == NULL) {
        unifi_error(priv,
                "Failed to allocate %d bytes for tx packet record\n",
                sizeof(tx_buffered_packets_t));
        func_exit();
        return CSR_RESULT_FAILURE;
    }

    /* disable the preemption */
    spin_lock_irqsave(&priv->tx_q_lock,lock_flags);
    INIT_LIST_HEAD(&tx_q_item->q);
    /* fill the tx_q structure members */
    tx_q_item->bulkdata.os_data_ptr = bulkdata->d[0].os_data_ptr;
    tx_q_item->bulkdata.data_length = bulkdata->d[0].data_length;
    tx_q_item->bulkdata.os_net_buf_ptr = bulkdata->d[0].os_net_buf_ptr;
    tx_q_item->bulkdata.net_buf_length = bulkdata->d[0].net_buf_length;
    tx_q_item->interfaceTag = req->VirtualInterfaceIdentifier & 0xff;
    tx_q_item->hostTag = req->HostTag;
    tx_q_item->leSenderProcessId = signal->SignalPrimitiveHeader.SenderProcessId;
    tx_q_item->transmissionControl = req->TransmissionControl;
    tx_q_item->priority = req->Priority;
    tx_q_item->rate = req->TransmitRate;
    memcpy(tx_q_item->peerMacAddress.a, req->Ra.x, ETH_ALEN);



    if (requeueOnSamePos) {
        list_add(&tx_q_item->q, list);
    } else {
        list_add_tail(&tx_q_item->q, list);
    }

    /* Count of packet queued in driver */
    priv->noOfPktQueuedInDriver++;
    spin_unlock_irqrestore(&priv->tx_q_lock,lock_flags);
    unifi_trace(priv, UDBG5, "leaving enque_tx_data_pdu\n");
    return CSR_RESULT_SUCCESS;
}

#ifdef CSR_WIFI_REQUEUE_PACKET_TO_HAL
CsrResult unifi_reque_ma_packet_request (void *ospriv, u32 host_tag,
                                         u16 txStatus, bulk_data_desc_t *bulkDataDesc)
{
    CsrResult status = CSR_RESULT_SUCCESS;
    unifi_priv_t *priv = (unifi_priv_t*)ospriv;
    netInterface_priv_t *interfacePriv;
    struct list_head *list = NULL;
    CsrWifiRouterCtrlStaInfo_t *staRecord = NULL;
    bulk_data_param_t bulkData;
    CSR_SIGNAL signal;
    CSR_PRIORITY priority = 0;
    u16 interfaceTag = 0;
    unifi_TrafficQueue priority_q;
    u16 frameControl = 0, frameType = 0;
    unsigned long lock_flags;

    interfacePriv = priv->interfacePriv[interfaceTag];

    /* If the current mode is not AP or P2PGO then just return failure
     * to clear the hip slot
     */
    if(!((interfacePriv->interfaceMode == CSR_WIFI_ROUTER_CTRL_MODE_AP) ||
        (interfacePriv->interfaceMode == CSR_WIFI_ROUTER_CTRL_MODE_P2PGO))) {
        return CSR_RESULT_FAILURE;
    }

    unifi_trace(priv, UDBG6, "unifi_reque_ma_packet_request: host_tag = 0x%x\n", host_tag);

    staRecord = CsrWifiRouterCtrlGetStationRecordFromPeerMacAddress(priv,
                                                                    (((u8 *) bulkDataDesc->os_data_ptr) + 4),
                                                                    interfaceTag);
    if (NULL == staRecord) {
        unifi_trace(priv, UDBG5, "unifi_reque_ma_packet_request: Invalid STA record \n");
        return CSR_RESULT_FAILURE;
    }

    /* Update TIM if MA-PACKET.cfm fails with status as Tx-retry-limit or No-BSS and then just return failure
     * to clear the hip slot associated with the Packet
     */
    if (CSR_TX_RETRY_LIMIT == txStatus || CSR_TX_NO_BSS == txStatus) {
        if (staRecord->timSet == CSR_WIFI_TIM_RESET || staRecord->timSet == CSR_WIFI_TIM_RESETTING)
        {
            unifi_trace(priv, UDBG2, "unifi_reque_ma_packet_request: CFM failed with Retry Limit or No BSS-->update TIM\n");
            if (!staRecord->timRequestPendingFlag) {
                update_tim(priv, staRecord->aid, 1, interfaceTag, staRecord->assignedHandle);
            }
            else {
                /* Cache the TimSet value so that it will processed immidiatly after
                 * completing the current setTim Request
                 */
                staRecord->updateTimReqQueued = 1;
                unifi_trace(priv, UDBG6, "unifi_reque_ma_packet_request: One more UpdateTim Request(:%d)Queued for AID %x\n",
                                         staRecord->updateTimReqQueued, staRecord->aid);
            }
        }
        return CSR_RESULT_FAILURE;
    }
    else if ((CSR_TX_LIFETIME == txStatus) ||  (CSR_TX_BLOCK_ACK_TIMEOUT == txStatus) ||
             (CSR_TX_FAIL_TRANSMISSION_VIF_INTERRUPTED == txStatus) ||
             (CSR_TX_REJECTED_PEER_STATION_SLEEPING == txStatus)    ||
             (CSR_TX_REJECTED_DTIM_STARTED == txStatus)) {
        /* Extract the Frame control and the frame type */
        frameControl = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(bulkDataDesc->os_data_ptr);
        frameType =  ((frameControl & IEEE80211_FC_TYPE_MASK) >> FRAME_CONTROL_TYPE_FIELD_OFFSET);

        /* Mgmt frames will not be re-queued for Tx
         * so just return failure to clear the hip slot
         */
        if (IEEE802_11_FRAMETYPE_MANAGEMENT == frameType) {
            return CSR_RESULT_FAILURE;
        }
        else if (IEEE802_11_FRAMETYPE_DATA == frameType) {
            /* QOS NULL and DATA NULL frames will not be re-queued for Tx
             * so just return failure to clear the hip slot
             */
            if ((((frameControl & IEEE80211_FC_SUBTYPE_MASK) >> FRAME_CONTROL_SUBTYPE_FIELD_OFFSET) == QOS_DATA_NULL) ||
                (((frameControl & IEEE80211_FC_SUBTYPE_MASK) >> FRAME_CONTROL_SUBTYPE_FIELD_OFFSET)== DATA_NULL )) {
                return CSR_RESULT_FAILURE;
            }
        }

        /* Extract the Packet priority */
        if (TRUE == staRecord->wmmOrQosEnabled) {
            u16 qosControl = 0;
            u8  dataFrameType = 0;

            dataFrameType =((frameControl & IEEE80211_FC_SUBTYPE_MASK) >> 4);

            if (dataFrameType == QOS_DATA) {
                /* QoS control field is offset from frame control by 2 (frame control)
                 * + 2 (duration/ID) + 2 (sequence control) + 3*ETH_ALEN or 4*ETH_ALEN
                 */
                if((frameControl & IEEE802_11_FC_TO_DS_MASK) && (frameControl & IEEE802_11_FC_FROM_DS_MASK)) {
                    qosControl= CSR_GET_UINT16_FROM_LITTLE_ENDIAN(bulkDataDesc->os_data_ptr + 30);
                }
                else {
                    qosControl = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(bulkDataDesc->os_data_ptr + 24);
                }
            }

            priority = (CSR_PRIORITY)(qosControl & IEEE802_11_QC_TID_MASK);

            if (priority < CSR_QOS_UP0 || priority > CSR_QOS_UP7) {
                unifi_trace(priv, UDBG5, "unifi_reque_ma_packet_request: Invalid priority:%x \n", priority);
                return CSR_RESULT_FAILURE;
            }
        }
        else {
            priority = CSR_CONTENTION;
        }

        /* Frame Bulk data to requeue it back to HAL Queues */
        bulkData.d[0].os_data_ptr    = bulkDataDesc->os_data_ptr;
        bulkData.d[0].data_length    = bulkDataDesc->data_length;
        bulkData.d[0].os_net_buf_ptr = bulkDataDesc->os_net_buf_ptr;
        bulkData.d[0].net_buf_length = bulkDataDesc->net_buf_length;

        bulkData.d[1].os_data_ptr    = NULL;
        bulkData.d[1].os_net_buf_ptr = NULL;
        bulkData.d[1].data_length    = bulkData.d[1].net_buf_length = 0;

        /* Initialize signal to zero */
        memset(&signal, 0, sizeof(CSR_SIGNAL));

        /* Frame MA Packet Req */
        unifi_frame_ma_packet_req(priv, priority, 0, host_tag,
                              interfaceTag, CSR_NO_CONFIRM_REQUIRED,
                              priv->netdev_client->sender_id,
                              staRecord->peerMacAddress.a, &signal);

        /* Find the Q-Priority */
        priority_q = unifi_frame_priority_to_queue(priority);
        list = &staRecord->dataPdu[priority_q];

        /* Place the Packet on to HAL Queue */
        status = enque_tx_data_pdu(priv, &bulkData, list, &signal, TRUE);

        /* Update the Per-station queued packet counter */
        if (!status) {
            spin_lock_irqsave(&priv->staRecord_lock, lock_flags);
            staRecord->noOfPktQueued++;
            spin_unlock_irqrestore(&priv->staRecord_lock, lock_flags);
        }
    }
    else {
        /* Packet will not be re-queued for any of the other MA Packet Tx failure
         * reasons so just return failure to clear the hip slot
         */
        return CSR_RESULT_FAILURE;
    }

    return status;
}
#endif

static void is_all_ac_deliver_enabled_and_moredata(CsrWifiRouterCtrlStaInfo_t *staRecord, u8 *allDeliveryEnabled, u8 *dataAvailable)
{
    u8 i;
    *allDeliveryEnabled = TRUE;
    for (i = 0 ;i < MAX_ACCESS_CATOGORY; i++) {
        if (!IS_DELIVERY_ENABLED(staRecord->powersaveMode[i])) {
            /* One is is not Delivery Enabled */
            *allDeliveryEnabled = FALSE;
            break;
        }
    }
    if (*allDeliveryEnabled) {
        *dataAvailable = (!list_empty(&staRecord->dataPdu[0]) || !list_empty(&staRecord->dataPdu[1])
                          ||!list_empty(&staRecord->dataPdu[2]) ||!list_empty(&staRecord->dataPdu[3])
                          ||!list_empty(&staRecord->mgtFrames));
    }
}

/*
 * ---------------------------------------------------------------------------
 *  uf_handle_tim_cfm
 *
 *
 *      This function updates tim status in host depending confirm status from firmware
 *
 *  Arguments:
 *      priv            Pointer to device private context struct
 *      cfm             CSR_MLME_SET_TIM_CONFIRM
 *      receiverProcessId SenderProcessID to fetch handle & timSet status
 *
 * ---------------------------------------------------------------------------
 */
void uf_handle_tim_cfm(unifi_priv_t *priv, CSR_MLME_SET_TIM_CONFIRM *cfm, u16 receiverProcessId)
{
    u8 handle = CSR_WIFI_GET_STATION_HANDLE_FROM_RECEIVER_ID(receiverProcessId);
    u8 timSetStatus = CSR_WIFI_GET_TIMSET_STATE_FROM_RECEIVER_ID(receiverProcessId);
    u16 interfaceTag = (cfm->VirtualInterfaceIdentifier & 0xff);
    netInterface_priv_t *interfacePriv = priv->interfacePriv[interfaceTag];
    CsrWifiRouterCtrlStaInfo_t *staRecord = NULL;
    /* This variable holds what TIM value we wanted to set in firmware */
    u16 timSetValue = 0;
    /* Irrespective of interface the count maintained */
    static u8 retryCount = 0;
    unsigned long lock_flags;
    unifi_trace(priv, UDBG3, "entering %s, handle = %x, timSetStatus = %x\n", __FUNCTION__, handle, timSetStatus);

    if (interfaceTag >= CSR_WIFI_NUM_INTERFACES) {
        unifi_warning(priv, "bad interfaceTag = %x\n", interfaceTag);
        return;
    }

    if ((handle != CSR_WIFI_BROADCAST_OR_MULTICAST_HANDLE) && (handle >= UNIFI_MAX_CONNECTIONS)) {
        unifi_warning(priv, "bad station Handle = %x\n", handle);
        return;
    }

    if (handle != CSR_WIFI_BROADCAST_OR_MULTICAST_HANDLE) {
        spin_lock_irqsave(&priv->staRecord_lock,lock_flags);
        if ((staRecord = ((CsrWifiRouterCtrlStaInfo_t *) (interfacePriv->staInfo[handle]))) == NULL) {
            spin_unlock_irqrestore(&priv->staRecord_lock,lock_flags);
            unifi_warning(priv, "uf_handle_tim_cfm: station record is NULL  handle = %x\n", handle);
            return;
        }
       spin_unlock_irqrestore(&priv->staRecord_lock,lock_flags);
    }
    switch(timSetStatus)
    {
        case CSR_WIFI_TIM_SETTING:
            timSetValue = CSR_WIFI_TIM_SET;
            break;
        case CSR_WIFI_TIM_RESETTING:
            timSetValue = CSR_WIFI_TIM_RESET;
            break;
        default:
            unifi_warning(priv, "timSet state is %x: Debug\n", timSetStatus);
            return;
    }

    /* check TIM confirm for success/failures */
    switch(cfm->ResultCode)
    {
        case CSR_RC_SUCCESS:
            if (handle != CSR_WIFI_BROADCAST_OR_MULTICAST_HANDLE) {
                /* Unicast frame & station record available */
                if (timSetStatus == staRecord->timSet) {
                    staRecord->timSet = timSetValue;
                    /* fh_cmd_q can also be full at some point of time!,
                     * resetting count as queue is cleaned by firmware at this point
                     */
                    retryCount = 0;
                    unifi_trace(priv, UDBG2, "tim (%s) successfully in firmware\n", (timSetValue)?"SET":"RESET");
                } else {
                    unifi_trace(priv, UDBG3, "receiver processID = %x, success: request & confirm states are not matching in TIM cfm: Debug status = %x, staRecord->timSet = %x, handle = %x\n",
                                 receiverProcessId, timSetStatus, staRecord->timSet, handle);
                }

                /* Reset TIM pending flag to send next TIM request */
                staRecord->timRequestPendingFlag = FALSE;

                /* Make sure that one more UpdateTim request is queued, if Queued its value
                 * should be CSR_WIFI_TIM_SET or CSR_WIFI_TIM_RESET
                 */
                if (0xFF != staRecord->updateTimReqQueued)
                {
                    /* Process the UpdateTim Request which is queued while previous UpdateTim was in progress */
                    if (staRecord->timSet != staRecord->updateTimReqQueued)
                    {
                       unifi_trace(priv, UDBG2, "uf_handle_tim_cfm : Processing Queued UpdateTimReq \n");

                       update_tim(priv, staRecord->aid, staRecord->updateTimReqQueued, interfaceTag, handle);

                       staRecord->updateTimReqQueued = 0xFF;
                    }
                }
            } else {

                interfacePriv->bcTimSet = timSetValue;
                /* fh_cmd_q can also be full at some point of time!,
                 * resetting count as queue is cleaned by firmware at this point
                 */
                retryCount = 0;
                unifi_trace(priv, UDBG3, "tim (%s) successfully for broadcast frame in firmware\n", (timSetValue)?"SET":"RESET");

                /* Reset DTIM pending flag to send next DTIM request */
                interfacePriv->bcTimSetReqPendingFlag = FALSE;

                /* Make sure that one more UpdateDTim request is queued, if Queued its value
                 * should be CSR_WIFI_TIM_SET or CSR_WIFI_TIM_RESET
                 */
                if (0xFF != interfacePriv->bcTimSetReqQueued)
                {
                    /* Process the UpdateTim Request which is queued while previous UpdateTim was in progress */
                    if (interfacePriv->bcTimSet != interfacePriv->bcTimSetReqQueued)
                    {
                        unifi_trace(priv, UDBG2, "uf_handle_tim_cfm : Processing Queued UpdateDTimReq \n");

                        update_tim(priv, 0, interfacePriv->bcTimSetReqQueued, interfaceTag, 0xFFFFFFFF);

                        interfacePriv->bcTimSetReqQueued = 0xFF;
                    }
                }

            }
            break;
        case CSR_RC_INVALID_PARAMETERS:
        case CSR_RC_INSUFFICIENT_RESOURCE:
            /* check for max retry limit & send again
             * MAX_RETRY_LIMIT is not maintained for each set of transactions..Its generic
             * If failure crosses this Limit, we have to take a call to FIX
             */
            if (retryCount > UNIFI_MAX_RETRY_LIMIT) {
                CsrBool moreData = FALSE;
                retryCount = 0;
                /* Because of continuos traffic in fh_cmd_q the tim set request is failing (exceeding retry limit)
                 * but if we didn't synchronize our timSet varible state with firmware then it can cause below issues
                 * cond 1. We want to SET tim in firmware if its fails & max retry limit reached
                 *   -> If host set's the timSet to 1, we wont try to send(as max retry reached) update tim but
                 *   firmware is not updated with queue(TIM) status so it wont set TIM in beacon finally host start piling
                 *    up data & wont try to set tim in firmware (This can cause worser performance)
                 * cond 2. We want to reset tim in firmware it fails & reaches max retry limit
                 *   -> If host sets the timSet to Zero, it wont try to set a TIM request unless we wont have any packets
                 *   to be queued, so beacon unnecessarily advertizes the TIM
                 */

                if(staRecord) {
                    if(!staRecord->wmmOrQosEnabled) {
                        moreData = (!list_empty(&staRecord->dataPdu[UNIFI_TRAFFIC_Q_CONTENTION]) ||
                                !list_empty(&staRecord->dataPdu[UNIFI_TRAFFIC_Q_VO]) ||
                                !list_empty(&staRecord->mgtFrames));
                    } else {
                        /* Peer is QSTA */
                        u8 allDeliveryEnabled = 0, dataAvailable = 0;
                        /* Check if all AC's are Delivery Enabled */
                        is_all_ac_deliver_enabled_and_moredata(staRecord, &allDeliveryEnabled, &dataAvailable);
                        /*check for more data in non-delivery enabled queues*/
                        moreData = (uf_is_more_data_for_non_delivery_ac(staRecord) || (allDeliveryEnabled && dataAvailable));

                    }
                    /* To avoid cond 1 & 2, check internal Queues status, if we have more Data then set RESET the timSet(0),
                     *  so we are trying to be in sync with firmware & next packets before queuing atleast try to
                     *  set TIM in firmware otherwise it SET timSet(1)
                     */
                    if (moreData) {
                        staRecord->timSet = CSR_WIFI_TIM_RESET;
                    } else {
                        staRecord->timSet = CSR_WIFI_TIM_SET;
                    }
                } else {
                    /* Its a broadcast frames */
                    moreData = (!list_empty(&interfacePriv->genericMulticastOrBroadCastMgtFrames) ||
                               !list_empty(&interfacePriv->genericMulticastOrBroadCastFrames));
                    if (moreData) {
                        update_tim(priv, 0, CSR_WIFI_TIM_SET, interfaceTag, 0xFFFFFFFF);
                    } else {
                        update_tim(priv, 0, CSR_WIFI_TIM_RESET, interfaceTag, 0xFFFFFFFF);
                    }
                }

                unifi_error(priv, "no of error's for TIM setting crossed the Limit: verify\n");
                return;
            }
            retryCount++;

            if (handle != CSR_WIFI_BROADCAST_OR_MULTICAST_HANDLE) {
                if (timSetStatus == staRecord->timSet) {
                    unifi_warning(priv, "tim request failed, retry for AID = %x\n", staRecord->aid);
                    update_tim(priv, staRecord->aid, timSetValue, interfaceTag, handle);
                } else {
                    unifi_trace(priv, UDBG1, "failure: request & confirm states are not matching in TIM cfm: Debug status = %x, staRecord->timSet = %x\n",
                                  timSetStatus, staRecord->timSet);
                }
            } else {
                unifi_warning(priv, "tim request failed, retry for broadcast frames\n");
                update_tim(priv, 0, timSetValue, interfaceTag, 0xFFFFFFFF);
            }
            break;
        default:
            unifi_warning(priv, "tim update request failed resultcode = %x\n", cfm->ResultCode);
    }

    unifi_trace(priv, UDBG2, "leaving %s\n", __FUNCTION__);
}

/*
 * ---------------------------------------------------------------------------
 *  update_tim
 *
 *
 *      This function updates tim status in firmware for AID[1 to UNIFI_MAX_CONNECTIONS] or
 *       AID[0] for broadcast/multicast packets.
 *
 *      NOTE: The LSB (least significant BYTE) of senderId while sending this MLME premitive
 *       has been modified(utilized) as below
 *
 *       SenderID in signal's SignalPrimitiveHeader is 2 byte the lowe byte bitmap is below
 *
 *       station handle(6 bits)      timSet Status (2 bits)
 *       ---------------------       ----------------------
 *       0  0  0  0  0  0        |       0  0
 *
 * timSet Status can be one of below:
 *
 * CSR_WIFI_TIM_RESET
 * CSR_WIFI_TIM_RESETTING
 * CSR_WIFI_TIM_SET
 * CSR_WIFI_TIM_SETTING
 *
 *  Arguments:
 *      priv            Pointer to device private context struct
 *      aid             can be 1 t0 UNIFI_MAX_CONNECTIONS & 0 means multicast/broadcast
 *      setTim          value SET(1) / RESET(0)
 *      interfaceTag    the interfaceID on which activity going on
 *      handle          from  (0 <= handle < UNIFI_MAX_CONNECTIONS)
 *
 * ---------------------------------------------------------------------------
 */
void update_tim(unifi_priv_t * priv, u16 aid, u8 setTim, u16 interfaceTag, u32 handle)
{
    CSR_SIGNAL signal;
    CsrInt32 r;
    CSR_MLME_SET_TIM_REQUEST *req = &signal.u.MlmeSetTimRequest;
    bulk_data_param_t *bulkdata = NULL;
    netInterface_priv_t *interfacePriv = priv->interfacePriv[interfaceTag];
    u8 senderIdLsb = 0;
    CsrWifiRouterCtrlStaInfo_t *staRecord = NULL;
    u32 oldTimSetStatus = 0, timSetStatus = 0;

    unifi_trace(priv, UDBG5, "entering the update_tim routine\n");


    if (handle == 0xFFFFFFFF) {
        handle &= CSR_WIFI_BROADCAST_OR_MULTICAST_HANDLE;
        if (setTim == interfacePriv->bcTimSet)
        {
            unifi_trace(priv, UDBG3, "update_tim, Drop:Hdl=%x, timval=%d, globalTim=%d\n", handle, setTim, interfacePriv->bcTimSet);
            return;
        }
    } else if ((handle != 0xFFFFFFFF) && (handle >= UNIFI_MAX_CONNECTIONS)) {
        unifi_warning(priv, "bad station Handle = %x\n", handle);
        return;
    }

    if (setTim) {
        timSetStatus =  CSR_WIFI_TIM_SETTING;
    } else {
        timSetStatus =  CSR_WIFI_TIM_RESETTING;
    }

    if (handle != CSR_WIFI_BROADCAST_OR_MULTICAST_HANDLE) {
        if ((staRecord = ((CsrWifiRouterCtrlStaInfo_t *) (interfacePriv->staInfo[handle]))) == NULL) {
            unifi_warning(priv, "station record is NULL in  update_tim: handle = %x :debug\n", handle);
            return;
        }
        /* In case of signal sending failed, revert back to old state */
        oldTimSetStatus = staRecord->timSet;
        staRecord->timSet = timSetStatus;
    }

    /* pack senderID LSB */
    senderIdLsb = CSR_WIFI_PACK_SENDER_ID_LSB_FOR_TIM_REQ(handle,  timSetStatus);

    /* initialize signal to zero */
    memset(&signal, 0, sizeof(CSR_SIGNAL));

    /* Frame the MLME-SET-TIM request */
    signal.SignalPrimitiveHeader.SignalId = CSR_MLME_SET_TIM_REQUEST_ID;
    signal.SignalPrimitiveHeader.ReceiverProcessId = 0;
    CSR_COPY_UINT16_TO_LITTLE_ENDIAN(((priv->netdev_client->sender_id & 0xff00) | senderIdLsb),
                   (u8*)&signal.SignalPrimitiveHeader.SenderProcessId);

    /* set The virtual interfaceIdentifier, aid, tim value */
    req->VirtualInterfaceIdentifier = uf_get_vif_identifier(interfacePriv->interfaceMode,interfaceTag);
    req->AssociationId = aid;
    req->TimValue = setTim;


    unifi_trace(priv, UDBG2, "update_tim:AID %x,senderIdLsb = 0x%x, handle = 0x%x, timSetStatus = %x, sender proceesID = %x \n",
                aid,senderIdLsb, handle, timSetStatus, signal.SignalPrimitiveHeader.SenderProcessId);

    /* Send the signal to UniFi */
    r = ul_send_signal_unpacked(priv, &signal, bulkdata);
    if (r) {
        /* No need to free bulk data, as TIM request doesn't carries any data */
        unifi_error(priv, "Error queueing CSR_MLME_SET_TIM_REQUEST signal\n");
        if (staRecord) {
            staRecord->timSet = oldTimSetStatus ;
        }
        else
        {
            /* MLME_SET_TIM.req sending failed here for AID0, so revert back our bcTimSet status */
            interfacePriv->bcTimSet = !setTim;
        }
    }
    else {
        /* Update tim request pending flag and ensure no more TIM set requests are send
           for the same station until TIM confirm is received */
        if (staRecord) {
            staRecord->timRequestPendingFlag = TRUE;
        }
        else
        {
            /* Update tim request (for AID 0) pending flag and ensure no more DTIM set requests are send
             * for the same station until TIM confirm is received
             */
            interfacePriv->bcTimSetReqPendingFlag = TRUE;
        }
    }
    unifi_trace(priv, UDBG5, "leaving the update_tim routine\n");
}

static
void process_peer_active_transition(unifi_priv_t * priv,
                                    CsrWifiRouterCtrlStaInfo_t *staRecord,
                                    u16 interfaceTag)
{
    int r,i;
    CsrBool spaceAvail[4] = {TRUE,TRUE,TRUE,TRUE};
    tx_buffered_packets_t * buffered_pkt = NULL;
    unsigned long lock_flags;
    netInterface_priv_t *interfacePriv = priv->interfacePriv[interfaceTag];

    unifi_trace(priv, UDBG5, "entering process_peer_active_transition\n");

    if(IS_DTIM_ACTIVE(interfacePriv->dtimActive,interfacePriv->multicastPduHostTag)) {
        /* giving more priority to multicast packets so delaying unicast packets*/
        unifi_trace(priv,UDBG2, "Multicast transmission is going on so resume unicast transmission after DTIM over\n");

        /* As station is active now, even though AP is not able to send frames to it
         * because of DTIM, it needs to reset the TIM here
         */
        if (!staRecord->timRequestPendingFlag){
            if((staRecord->timSet == CSR_WIFI_TIM_SET) || (staRecord->timSet == CSR_WIFI_TIM_SETTING)){
                update_tim(priv, staRecord->aid, 0, interfaceTag, staRecord->assignedHandle);
            }
        }
        else
        {
            /* Cache the TimSet value so that it will processed immidiatly after
             * completing the current setTim Request
             */
            staRecord->updateTimReqQueued = 0;
            unifi_trace(priv, UDBG6, "update_tim : One more UpdateTim Request (Tim value:%d) Queued for AID %x\n", staRecord->updateTimReqQueued,
                        staRecord->aid);
        }
        return;
    }
    while((buffered_pkt=dequeue_tx_data_pdu(priv, &staRecord->mgtFrames))) {
        buffered_pkt->transmissionControl &=
                     ~(TRANSMISSION_CONTROL_TRIGGER_MASK|TRANSMISSION_CONTROL_EOSP_MASK);
        if((r=frame_and_send_queued_pdu(priv,buffered_pkt,staRecord,0,FALSE)) == -ENOSPC) {
            unifi_trace(priv, UDBG2, "p_p_a_t:(ENOSPC) Mgt Frame queueing \n");
            /* Enqueue at the head of the queue */
            spin_lock_irqsave(&priv->tx_q_lock,lock_flags);
            list_add(&buffered_pkt->q, &staRecord->mgtFrames);
            spin_unlock_irqrestore(&priv->tx_q_lock,lock_flags);
            priv->pausedStaHandle[3]=(u8)(staRecord->assignedHandle);
            spaceAvail[3] = FALSE;
            break;
        } else {
            if(r){
                unifi_trace (priv, UDBG1, " HIP validation failure : PDU sending failed \n");
                /* the PDU failed where we can't do any thing so free the storage */
                unifi_net_data_free(priv, &buffered_pkt->bulkdata);
            }
            kfree(buffered_pkt);
        }
    }
    if (!staRecord->timRequestPendingFlag) {
        if (staRecord->txSuspend) {
            if(staRecord->timSet == CSR_WIFI_TIM_SET) {
                update_tim(priv,staRecord->aid,0,interfaceTag, staRecord->assignedHandle);
            }
            return;
        }
    }
    else
    {
        /* Cache the TimSet value so that it will processed immidiatly after
         * completing the current setTim Request
         */
        staRecord->updateTimReqQueued = 0;
        unifi_trace(priv, UDBG6, "update_tim : One more UpdateTim Request (Tim value:%d) Queued for AID %x\n", staRecord->updateTimReqQueued,
                    staRecord->aid);
    }
    for(i=3;i>=0;i--) {
        if(!spaceAvail[i])
            continue;
        unifi_trace(priv, UDBG6, "p_p_a_t:data pkt sending for AC %d \n",i);
        while((buffered_pkt=dequeue_tx_data_pdu(priv, &staRecord->dataPdu[i]))) {
           buffered_pkt->transmissionControl &=
                      ~(TRANSMISSION_CONTROL_TRIGGER_MASK|TRANSMISSION_CONTROL_EOSP_MASK);
           if((r=frame_and_send_queued_pdu(priv,buffered_pkt,staRecord,0,FALSE)) == -ENOSPC) {
               /* Clear the trigger bit transmission control*/
               /* Enqueue at the head of the queue */
               spin_lock_irqsave(&priv->tx_q_lock,lock_flags);
               list_add(&buffered_pkt->q, &staRecord->dataPdu[i]);
               spin_unlock_irqrestore(&priv->tx_q_lock,lock_flags);
               priv->pausedStaHandle[i]=(u8)(staRecord->assignedHandle);
               break;
           } else {
              if(r){
                  unifi_trace (priv, UDBG1, " HIP validation failure : PDU sending failed \n");
                  /* the PDU failed where we can't do any thing so free the storage */
                  unifi_net_data_free(priv, &buffered_pkt->bulkdata);
               }
              kfree(buffered_pkt);
           }
        }
    }
    if (!staRecord->timRequestPendingFlag){
        if((staRecord->timSet  == CSR_WIFI_TIM_SET) || (staRecord->timSet  == CSR_WIFI_TIM_SETTING)) {
            unifi_trace(priv, UDBG3, "p_p_a_t:resetting tim .....\n");
            update_tim(priv,staRecord->aid,0,interfaceTag, staRecord->assignedHandle);
        }
    }
    else
    {
        /* Cache the TimSet value so that it will processed immidiatly after
         * completing the current setTim Request
         */
        staRecord->updateTimReqQueued = 0;
        unifi_trace(priv, UDBG6, "update_tim : One more UpdateTim Request (Tim value:%d) Queued for AID %x\n", staRecord->updateTimReqQueued,
                    staRecord->aid);
    }
    unifi_trace(priv, UDBG5, "leaving process_peer_active_transition\n");
}



void uf_process_ma_pkt_cfm_for_ap(unifi_priv_t *priv,u16 interfaceTag, const CSR_MA_PACKET_CONFIRM *pkt_cfm)
{
    netInterface_priv_t *interfacePriv;
    u8 i;
    CsrWifiRouterCtrlStaInfo_t *staRecord = NULL;
    interfacePriv = priv->interfacePriv[interfaceTag];


    if(pkt_cfm->HostTag == interfacePriv->multicastPduHostTag) {
         unifi_trace(priv,UDBG2,"CFM for marked Multicast Tag = %x\n",interfacePriv->multicastPduHostTag);
         interfacePriv->multicastPduHostTag = 0xffffffff;
         resume_suspended_uapsd(priv,interfaceTag);
         resume_unicast_buffered_frames(priv,interfaceTag);
         if(list_empty(&interfacePriv->genericMulticastOrBroadCastMgtFrames) &&
              list_empty(&interfacePriv->genericMulticastOrBroadCastFrames)) {
            unifi_trace(priv,UDBG1,"Resetting multicastTIM");
            if (!interfacePriv->bcTimSetReqPendingFlag)
            {
                update_tim(priv,0,CSR_WIFI_TIM_RESET,interfaceTag, 0xFFFFFFFF);
            }
            else
            {
                /* Cache the DTimSet value so that it will processed immidiatly after
                 * completing the current setDTim Request
                 */
                 interfacePriv->bcTimSetReqQueued = CSR_WIFI_TIM_RESET;
                 unifi_trace(priv, UDBG2, "uf_process_ma_pkt_cfm_for_ap : One more UpdateDTim Request(%d) Queued \n",
                             interfacePriv->bcTimSetReqQueued);
            }

        }
        return;
    }

    /* Check if it is a Confirm for null data frame used
     * for probing station activity
     */
    for(i =0; i < UNIFI_MAX_CONNECTIONS; i++) {
        staRecord = (CsrWifiRouterCtrlStaInfo_t *) (interfacePriv->staInfo[i]);
        if (staRecord && (staRecord->nullDataHostTag == pkt_cfm->HostTag)) {

            unifi_trace(priv, UDBG1, "CFM for Inactive probe Null frame (tag = %x, status = %d)\n",
                                    pkt_cfm->HostTag,
                                    pkt_cfm->TransmissionStatus
                                    );
            staRecord->nullDataHostTag = INVALID_HOST_TAG;

            if(pkt_cfm->TransmissionStatus == CSR_TX_RETRY_LIMIT){
                CsrTime now;
                CsrTime inactive_time;

                unifi_trace(priv, UDBG1, "Nulldata to probe STA ALIVE Failed with retry limit\n");
                /* Recheck if there is some activity after null data is sent.
                *
                * If still there is no activity then send a disconnected indication
                * to SME to delete the station record.
                */
                if (staRecord->activity_flag){
                    return;
                }
                now = CsrTimeGet(NULL);

                if (staRecord->lastActivity > now)
                {
                    /* simple timer wrap (for 1 wrap) */
                    inactive_time = CsrTimeAdd((CsrTime)CsrTimeSub(CSR_SCHED_TIME_MAX, staRecord->lastActivity),
                                               now);
                }
                else
                {
                    inactive_time = (CsrTime)CsrTimeSub(now, staRecord->lastActivity);
                }

                if (inactive_time >= STA_INACTIVE_TIMEOUT_VAL)
                {
                    struct list_head send_cfm_list;
                    u8 j;

                    /* The SME/NME may be waiting for confirmation for requested frames to this station.
                     * Though this is --VERY UNLIKELY-- in case of station in active mode. But still as a
                     * a defensive check, it loops through buffered frames for this station and if confirmation
                     * is requested, send auto confirmation with failure status. Also flush the frames so
                     * that these are not processed again in PEER_DEL_REQ handler.
                     */
                    INIT_LIST_HEAD(&send_cfm_list);

                    uf_prepare_send_cfm_list_for_queued_pkts(priv,
                                                             &send_cfm_list,
                                                             &(staRecord->mgtFrames));

                    uf_flush_list(priv, &(staRecord->mgtFrames));

                    for(j = 0; j < MAX_ACCESS_CATOGORY; j++){
                        uf_prepare_send_cfm_list_for_queued_pkts(priv,
                                                                 &send_cfm_list,
                                                                 &(staRecord->dataPdu[j]));

                        uf_flush_list(priv,&(staRecord->dataPdu[j]));
                    }

                    send_auto_ma_packet_confirm(priv, staRecord->interfacePriv, &send_cfm_list);



                    unifi_warning(priv, "uf_process_ma_pkt_cfm_for_ap: Router Disconnected IND Peer (%x-%x-%x-%x-%x-%x)\n",
                                             staRecord->peerMacAddress.a[0],
                                             staRecord->peerMacAddress.a[1],
                                             staRecord->peerMacAddress.a[2],
                                             staRecord->peerMacAddress.a[3],
                                             staRecord->peerMacAddress.a[4],
                                             staRecord->peerMacAddress.a[5]);

                    CsrWifiRouterCtrlConnectedIndSend(priv->CSR_WIFI_SME_IFACEQUEUE,
                                                      0,
                                                      staRecord->interfacePriv->InterfaceTag,
                                                      staRecord->peerMacAddress,
                                                      CSR_WIFI_ROUTER_CTRL_PEER_DISCONNECTED);
                }

            }
            else if (pkt_cfm->TransmissionStatus == CSR_TX_SUCCESSFUL)
            {
                 staRecord->activity_flag = TRUE;
            }
        }
    }
}

#endif
u16 uf_get_vif_identifier (CsrWifiRouterCtrlMode mode, u16 tag)
{
    switch(mode)
    {
        case CSR_WIFI_ROUTER_CTRL_MODE_STA:
        case CSR_WIFI_ROUTER_CTRL_MODE_P2PCLI:
            return (0x02<<8|tag);

        case CSR_WIFI_ROUTER_CTRL_MODE_AP:
        case CSR_WIFI_ROUTER_CTRL_MODE_P2PGO:
            return (0x03<<8|tag);

        case CSR_WIFI_ROUTER_CTRL_MODE_IBSS:
            return (0x01<<8|tag);

        case CSR_WIFI_ROUTER_CTRL_MODE_MONITOR:
            return (0x04<<8|tag);
        case CSR_WIFI_ROUTER_CTRL_MODE_AMP:
            return (0x05<<8|tag);
        default:
            return tag;
    }
}

#ifdef CSR_SUPPORT_SME

/*
 * ---------------------------------------------------------------------------
 *  update_macheader
 *
 *
 *      These functions updates mac header for intra BSS packet
 *      routing.
 *      NOTE: This function always has to be called in rx context which
 *      is in bh thread context since GFP_KERNEL is used. In soft IRQ/ Interrupt
 *      context shouldn't be used
 *
 *  Arguments:
 *      priv            Pointer to device private context struct
 *      skb             Socket buffer containing data packet to transmit
 *      newSkb          Socket buffer containing data packet + Mac header if no sufficient headroom in skb
 *      priority        to append QOS control header in Mac header
 *      bulkdata        if newSkb allocated then bulkdata updated to send to unifi
 *      interfaceTag    the interfaceID on which activity going on
 *      macHeaderLengthInBytes no. of bytes of mac header in received frame
 *      qosDestination  used to append Qos control field
 *
 *  Returns:
 *      Zero on success or -1 on error.
 * ---------------------------------------------------------------------------
 */

static int update_macheader(unifi_priv_t *priv, struct sk_buff *skb,
                            struct sk_buff *newSkb, CSR_PRIORITY *priority,
                            bulk_data_param_t *bulkdata, u16 interfaceTag,
                            u8 macHeaderLengthInBytes,
                            u8 qosDestination)
{

    u16 *fc = NULL;
    u8 direction = 0, toDs, fromDs;
    u8 *bufPtr = NULL;
    u8 sa[ETH_ALEN], da[ETH_ALEN];
    netInterface_priv_t *interfacePriv = priv->interfacePriv[interfaceTag];
    int headroom;
    u8 macHeaderBuf[IEEE802_11_DATA_FRAME_MAC_HEADER_SIZE] = {0};

    unifi_trace(priv, UDBG5, "entering the update_macheader function\n");

    /* temporary buffer for the Mac header storage */
    memcpy(macHeaderBuf, skb->data, macHeaderLengthInBytes);

    /* remove the Macheader from the skb */
    skb_pull(skb, macHeaderLengthInBytes);

    /* get the skb headroom for skb_push check */
    headroom = skb_headroom(skb);

    /*  pointer to frame control field */
    fc = (u16*) macHeaderBuf;

    toDs = (*fc & cpu_to_le16(IEEE802_11_FC_TO_DS_MASK))?1 : 0;
    fromDs = (*fc & cpu_to_le16(IEEE802_11_FC_FROM_DS_MASK))? 1: 0;
    unifi_trace(priv, UDBG5, "In update_macheader function, fromDs = %x, toDs = %x\n", fromDs, toDs);
    direction = ((fromDs | (toDs << 1)) & 0x3);

    /* Address1 or 3 from the macheader */
    memcpy(da, macHeaderBuf+4+toDs*12, ETH_ALEN);
    /* Address2, 3 or 4 from the mac header */
    memcpy(sa, macHeaderBuf+10+fromDs*(6+toDs*8), ETH_ALEN);

    unifi_trace(priv, UDBG3, "update_macheader:direction = %x\n", direction);
    /* update the toDs, fromDs & address fields in Mac header */
    switch(direction)
    {
        case 2:
            /* toDs = 1 & fromDs = 0 , toAp when frames received from peer
             * while sending this packet to Destination the Mac header changed
             * as fromDs = 1 & toDs = 0, fromAp
             */
            *fc &= cpu_to_le16(~IEEE802_11_FC_TO_DS_MASK);
            *fc |= cpu_to_le16(IEEE802_11_FC_FROM_DS_MASK);
            /* Address1: MAC address of the actual destination (4 = 2+2) */
            memcpy(macHeaderBuf + 4, da, ETH_ALEN);
            /* Address2: The MAC address of the AP (10 = 2+2+6) */
            memcpy(macHeaderBuf + 10, &interfacePriv->bssid, ETH_ALEN);
            /* Address3: MAC address of the actual source from mac header (16 = 2+2+6+6) */
            memcpy(macHeaderBuf + 16, sa, ETH_ALEN);
            break;
        case 3:
            unifi_trace(priv, UDBG3, "when both the toDs & fromDS set, NOT SUPPORTED\n");
            break;
        default:
            unifi_trace(priv, UDBG3, "problem in decoding packet in update_macheader \n");
            return -1;
    }

    /* frameType is Data always, Validation is done before calling this function */

    /* check for the souce station type */
    switch(le16_to_cpu(*fc) & IEEE80211_FC_SUBTYPE_MASK)
    {
        case IEEE802_11_FC_TYPE_QOS_DATA & IEEE80211_FC_SUBTYPE_MASK:
            /* No need to modify the qos control field */
            if (!qosDestination) {

                /* If source Sta is QOS enabled & if this bit set, then HTC is supported by
                 * peer station & htc field present in macHeader
                 */
                if (*fc & cpu_to_le16(IEEE80211_FC_ORDER_MASK)) {
                    /* HT control field present in Mac header
                     * 6 = sizeof(qosControl) + sizeof(htc)
                     */
                    macHeaderLengthInBytes -= 6;
                } else {
                    macHeaderLengthInBytes -= 2;
                }
                /* Destination STA is non qos so change subtype to DATA */
                *fc &= cpu_to_le16(~IEEE80211_FC_SUBTYPE_MASK);
                *fc |= cpu_to_le16(IEEE802_11_FC_TYPE_DATA);
                /* remove the qos control field & HTC(if present). new macHeaderLengthInBytes is less than old
                 * macHeaderLengthInBytes so no need to verify skb headroom
                 */
                if (headroom < macHeaderLengthInBytes) {
                    unifi_trace(priv, UDBG1, " sufficient headroom not there to push updated mac header \n");
                    return -1;
                }
                bufPtr = (u8 *) skb_push(skb, macHeaderLengthInBytes);

                /*  update bulk data os_data_ptr */
                bulkdata->d[0].os_data_ptr = skb->data;
                bulkdata->d[0].os_net_buf_ptr = (unsigned char*)skb;
                bulkdata->d[0].data_length = skb->len;

            } else {
                /* pointing to QOS control field */
                u8 qc;
                if (*fc & cpu_to_le16(IEEE80211_FC_ORDER_MASK)) {
                    qc = *((u8*)(macHeaderBuf + (macHeaderLengthInBytes - 4 - 2)));
                } else {
                    qc = *((u8*)(macHeaderBuf + (macHeaderLengthInBytes - 2)));
                }

                if ((qc & IEEE802_11_QC_TID_MASK) > 7) {
                    *priority = 7;
                } else {
                    *priority = qc & IEEE802_11_QC_TID_MASK;
                }

                unifi_trace(priv, UDBG1, "Incoming packet priority from QSTA is %x\n", *priority);

                if (headroom < macHeaderLengthInBytes) {
                    unifi_trace(priv, UDBG3, " sufficient headroom not there to push updated mac header \n");
                    return -1;
                }
                bufPtr = (u8 *) skb_push(skb, macHeaderLengthInBytes);
            }
            break;
        default:
            {
                bulk_data_param_t data_ptrs;
                CsrResult csrResult;
                unifi_trace(priv, UDBG5, "normal Data packet, NO QOS \n");

                if (qosDestination) {
                    u8 qc = 0;
                    unifi_trace(priv, UDBG3, "destination is QOS station \n");

                    /* Set Ma-Packet.req UP to UP0 */
                    *priority = CSR_QOS_UP0;

                    /* prepare the qos control field */
                    qc |= CSR_QOS_UP0;
                    /* no Amsdu is in ap buffer so eosp is left 0 */
                    if (da[0] & 0x1) {
                        /* multicast/broadcast frames, no acknowledgement needed */
                        qc |= 1 << 5;
                    }

                    /* update new Mac header Length with 2 = sizeof(qos control) */
                    macHeaderLengthInBytes += 2;

                    /* received DATA frame but destiantion is QOS station so update subtype to QOS*/
                    *fc &= cpu_to_le16(~IEEE80211_FC_SUBTYPE_MASK);
                    *fc |= cpu_to_le16(IEEE802_11_FC_TYPE_QOS_DATA);

                    /* appendQosControlOffset = macHeaderLengthInBytes - 2, since source sta is not QOS */
                    macHeaderBuf[macHeaderLengthInBytes - 2] = qc;
                    /* txopLimit is 0 */
                    macHeaderBuf[macHeaderLengthInBytes - 1] = 0;
                    if (headroom < macHeaderLengthInBytes) {
                        csrResult = unifi_net_data_malloc(priv, &data_ptrs.d[0], skb->len + macHeaderLengthInBytes);

                        if (csrResult != CSR_RESULT_SUCCESS) {
                            unifi_error(priv, " failed to allocate request_data. in update_macheader func\n");
                            return -1;
                        }
                        newSkb = (struct sk_buff *)(data_ptrs.d[0].os_net_buf_ptr);
                        newSkb->len = skb->len + macHeaderLengthInBytes;

                        memcpy((void*)data_ptrs.d[0].os_data_ptr + macHeaderLengthInBytes,
                                skb->data, skb->len);

                        bulkdata->d[0].os_data_ptr = newSkb->data;
                        bulkdata->d[0].os_net_buf_ptr = (unsigned char*)newSkb;
                        bulkdata->d[0].data_length = newSkb->len;

                        bufPtr = (u8*)data_ptrs.d[0].os_data_ptr;

                        /* The old skb will not be used again */
                        kfree_skb(skb);
                    } else {
                        /* skb headroom is sufficient to append Macheader */
                        bufPtr = (u8*)skb_push(skb, macHeaderLengthInBytes);
                        bulkdata->d[0].os_data_ptr = skb->data;
                        bulkdata->d[0].os_net_buf_ptr = (unsigned char*)skb;
                        bulkdata->d[0].data_length = skb->len;
                    }
                } else {
                    unifi_trace(priv, UDBG3, "destination is not a QSTA\n");
                    if (headroom < macHeaderLengthInBytes) {
                        csrResult = unifi_net_data_malloc(priv, &data_ptrs.d[0], skb->len + macHeaderLengthInBytes);

                        if (csrResult != CSR_RESULT_SUCCESS) {
                            unifi_error(priv, " failed to allocate request_data. in update_macheader func\n");
                            return -1;
                        }
                        newSkb = (struct sk_buff *)(data_ptrs.d[0].os_net_buf_ptr);
                        newSkb->len = skb->len + macHeaderLengthInBytes;

                        memcpy((void*)data_ptrs.d[0].os_data_ptr + macHeaderLengthInBytes,
                                skb->data, skb->len);

                        bulkdata->d[0].os_data_ptr = newSkb->data;
                        bulkdata->d[0].os_net_buf_ptr = (unsigned char*)newSkb;
                        bulkdata->d[0].data_length = newSkb->len;

                        bufPtr = (u8*)data_ptrs.d[0].os_data_ptr;

                        /* The old skb will not be used again */
                        kfree_skb(skb);
                    } else {
                        /* skb headroom is sufficient to append Macheader */
                        bufPtr = (u8*)skb_push(skb, macHeaderLengthInBytes);
                        bulkdata->d[0].os_data_ptr = skb->data;
                        bulkdata->d[0].os_net_buf_ptr = (unsigned char*)skb;
                        bulkdata->d[0].data_length = skb->len;
                    }
                }
            }
    }

    /* prepare the complete skb, by pushing the MAC header to the begining of the skb->data */
    unifi_trace(priv, UDBG5, "updated Mac Header: %d \n",macHeaderLengthInBytes);
    memcpy(bufPtr, macHeaderBuf, macHeaderLengthInBytes);

    unifi_trace(priv, UDBG5, "leaving the update_macheader function\n");
    return 0;
}
/*
 * ---------------------------------------------------------------------------
 *  uf_ap_process_data_pdu
 *
 *
 *      Takes care of intra BSS admission control & routing packets within BSS
 *
 *  Arguments:
 *      priv            Pointer to device private context struct
 *      skb             Socket buffer containing data packet to transmit
 *      ehdr            ethernet header to fetch priority of packet
 *      srcStaInfo      source stations record for connection verification
 *      packed_signal
 *      signal_len
 *      signal          MA-PACKET.indication signal
 *      bulkdata        if newSkb allocated then bulkdata updated to send to unifi
 *      macHeaderLengthInBytes no. of bytes of mac header in received frame
 *
 *  Returns:
 *      Zero on success(ap processing complete) or -1 if packet also have to be sent to NETDEV.
 * ---------------------------------------------------------------------------
 */
int
uf_ap_process_data_pdu(unifi_priv_t *priv, struct sk_buff *skb,
                       struct ethhdr *ehdr, CsrWifiRouterCtrlStaInfo_t * srcStaInfo,
                       const CSR_SIGNAL *signal,
                       bulk_data_param_t *bulkdata,
                       u8 macHeaderLengthInBytes)
{
    const CSR_MA_PACKET_INDICATION *ind = &(signal->u.MaPacketIndication);
    u16 interfaceTag = (ind->VirtualInterfaceIdentifier & 0x00ff);
    struct sk_buff *newSkb = NULL;
    /* pointer to skb or private skb created using skb_copy() */
    struct sk_buff *skbPtr = skb;
    CsrBool sendToNetdev = FALSE;
    CsrBool qosDestination = FALSE;
    CSR_PRIORITY priority = CSR_CONTENTION;
    CsrWifiRouterCtrlStaInfo_t *dstStaInfo = NULL;
    netInterface_priv_t *interfacePriv;

    unifi_trace(priv, UDBG5, "entering  uf_ap_process_data_pdu %d\n",macHeaderLengthInBytes);
    /* InterfaceTag validation from MA_PACKET.indication */
    if (interfaceTag >= CSR_WIFI_NUM_INTERFACES) {
        unifi_trace(priv, UDBG1, "Interface Tag is Invalid in uf_ap_process_data_pdu\n");
        unifi_net_data_free(priv, &bulkdata->d[0]);
        return 0;
    }
    interfacePriv = priv->interfacePriv[interfaceTag];

    if((interfacePriv->interfaceMode == CSR_WIFI_ROUTER_CTRL_MODE_P2PGO) &&
       (interfacePriv->intraBssEnabled == FALSE)) {
        unifi_trace(priv, UDBG2, "uf_ap_process_data_pdu:P2P GO intrabssEnabled?= %d\n", interfacePriv->intraBssEnabled);

        /*In P2P GO case, if intraBSS distribution Disabled then don't do IntraBSS routing */
        /* If destination in our BSS then drop otherwise give packet to netdev */
        dstStaInfo = CsrWifiRouterCtrlGetStationRecordFromPeerMacAddress(priv, ehdr->h_dest, interfaceTag);
        if (dstStaInfo) {
            unifi_net_data_free(priv, &bulkdata->d[0]);
            return 0;
        }
        /* May be associated P2PCLI trying to send the packets on backbone (Netdev) */
        return -1;
    }

    if(!memcmp(ehdr->h_dest, interfacePriv->bssid.a, ETH_ALEN)) {
        /* This packet will be given to the TCP/IP stack since this packet is for us(AP)
         * No routing needed */
        unifi_trace(priv, UDBG4, "destination address is csr_ap\n");
        return -1;
    }

    /* fetch the destination record from staion record database */
    dstStaInfo = CsrWifiRouterCtrlGetStationRecordFromPeerMacAddress(priv, ehdr->h_dest, interfaceTag);

    /* AP mode processing, & if packet is unicast */
    if(!dstStaInfo) {
        if (!(ehdr->h_dest[0] & 0x1)) {
            /* destination not in station record & its a unicast packet, so pass the packet to network stack */
            unifi_trace(priv, UDBG3, "unicast frame & destination record not exist, send to netdev proto = %x\n", htons(skb->protocol));
            return -1;
        } else {
            /* packet is multicast/broadcast */
            /* copy the skb to skbPtr, send skb to netdev & skbPtr to multicast/broad cast list */
            unifi_trace(priv, UDBG5, "skb_copy, in  uf_ap_process_data_pdu, protocol = %x\n", htons(skb->protocol));
            skbPtr = skb_copy(skb, GFP_KERNEL);
            if(skbPtr == NULL) {
                /* We don't have memory to don't send the frame in BSS*/
                unifi_notice(priv, "broacast/multicast frame can't be sent in BSS No memeory: proto = %x\n", htons(skb->protocol));
                return -1;
            }
            sendToNetdev = TRUE;
        }
    } else {

        /* validate the Peer & Destination Station record */
        if (uf_process_station_records_for_sending_data(priv, interfaceTag, srcStaInfo, dstStaInfo)) {
            unifi_notice(priv, "uf_ap_process_data_pdu: station record validation failed \n");
            interfacePriv->stats.rx_errors++;
            unifi_net_data_free(priv, &bulkdata->d[0]);
            return 0;
        }
    }

    /* BroadCast packet received and it's been sent as non QOS packets.
     * Since WMM spec not mandates broadcast/multicast to be sent as QOS data only,
     * if all Peers are QSTA
     */
    if(sendToNetdev) {
       /* BroadCast packet and it's been sent as non QOS packets */
        qosDestination = FALSE;
    } else if(dstStaInfo && (dstStaInfo->wmmOrQosEnabled == TRUE)) {
          qosDestination = TRUE;
    }

    unifi_trace(priv, UDBG3, "uf_ap_process_data_pdu QoS destination  = %s\n", (qosDestination)? "TRUE": "FALSE");

    /* packet is allowed to send to unifi, update the Mac header */
    if (update_macheader(priv, skbPtr, newSkb, &priority, bulkdata, interfaceTag, macHeaderLengthInBytes, qosDestination)) {
        interfacePriv->stats.rx_errors++;
        unifi_notice(priv, "(Packet Drop) failed to update the Mac header in uf_ap_process_data_pdu\n");
        if (sendToNetdev) {
            /*  Free's the skb_copy(skbPtr) data since packet processing failed */
            bulkdata->d[0].os_data_ptr = skbPtr->data;
            bulkdata->d[0].os_net_buf_ptr = (unsigned char*)skbPtr;
            bulkdata->d[0].data_length = skbPtr->len;
            unifi_net_data_free(priv, &bulkdata->d[0]);
        }
        return -1;
    }

    unifi_trace(priv, UDBG3, "Mac Header updated...calling uf_process_ma_packet_req \n");

    /* Packet is ready to send to unifi ,transmissionControl = 0x0004, confirmation is not needed for data packets */
    if (uf_process_ma_packet_req(priv,  ehdr->h_dest, 0xffffffff, interfaceTag, CSR_NO_CONFIRM_REQUIRED, (CSR_RATE)0,priority, priv->netdev_client->sender_id, bulkdata)) {
        if (sendToNetdev) {
            unifi_trace(priv, UDBG1, "In uf_ap_process_data_pdu, (Packet Drop) uf_process_ma_packet_req failed. freeing skb_copy data (original data sent to Netdev)\n");
            /*  Free's the skb_copy(skbPtr) data since packet processing failed */
            bulkdata->d[0].os_data_ptr = skbPtr->data;
            bulkdata->d[0].os_net_buf_ptr = (unsigned char*)skbPtr;
            bulkdata->d[0].data_length = skbPtr->len;
            unifi_net_data_free(priv, &bulkdata->d[0]);
        } else {
            /* This free's the skb data */
            unifi_trace(priv, UDBG1, "In uf_ap_process_data_pdu, (Packet Drop). Unicast data so freeing original skb \n");
            unifi_net_data_free(priv, &bulkdata->d[0]);
        }
    }
    unifi_trace(priv, UDBG5, "leaving  uf_ap_process_data_pdu\n");

    if (sendToNetdev) {
        /* The packet is multicast/broadcast, so after AP processing packet has to
         * be sent to netdev, if peer port state is open
        */
        unifi_trace(priv, UDBG4, "Packet will be routed to NetDev\n");
        return -1;
    }
    /* Ap handled the packet & its a unicast packet, no need to send to netdev */
    return 0;
}

#endif

CsrResult uf_process_ma_packet_req(unifi_priv_t *priv,
                                   u8 *peerMacAddress,
                                   CSR_CLIENT_TAG hostTag,
                                   u16 interfaceTag,
                                   CSR_TRANSMISSION_CONTROL transmissionControl,
                                   CSR_RATE TransmitRate,
                                   CSR_PRIORITY priority,
                                   CSR_PROCESS_ID leSenderProcessId,
                                   bulk_data_param_t *bulkdata)
{
    CsrResult status = CSR_RESULT_SUCCESS;
    CSR_SIGNAL signal;
    int result;
#ifdef CSR_SUPPORT_SME
   CsrWifiRouterCtrlStaInfo_t *staRecord = NULL;
    const u8 *macHdrLocation =  bulkdata->d[0].os_data_ptr;
    CsrWifiPacketType pktType;
    int frameType = 0;
    CsrBool queuePacketDozing = FALSE;
    u32 priority_q;
    u16 frmCtrl;
    struct list_head * list = NULL; /* List to which buffered PDUs are to be enqueued*/
    CsrBool setBcTim=FALSE;
    netInterface_priv_t *interfacePriv;
    CsrBool requeueOnSamePos = FALSE;
    u32 handle = 0xFFFFFFFF;
    unsigned long lock_flags;

	unifi_trace(priv, UDBG5,
		"entering uf_process_ma_packet_req, peer: %pMF\n",
		peerMacAddress);

    if (interfaceTag >= CSR_WIFI_NUM_INTERFACES) {
        unifi_error(priv, "interfaceTag >= CSR_WIFI_NUM_INTERFACES, interfacetag = %d\n", interfaceTag);
        return CSR_RESULT_FAILURE;
    }
    interfacePriv = priv->interfacePriv[interfaceTag];


    /* fetch the station record for corresponding peer mac address */
    if ((staRecord = CsrWifiRouterCtrlGetStationRecordFromPeerMacAddress(priv, peerMacAddress, interfaceTag))) {
        handle = staRecord->assignedHandle;
    }

    /* Frame ma-packet.req, this is saved/transmitted depend on queue state */
    unifi_frame_ma_packet_req(priv, priority, TransmitRate, hostTag,
                              interfaceTag, transmissionControl, leSenderProcessId,
                              peerMacAddress, &signal);

   /* Since it's common path between STA & AP mode, in case of STA packet
     * need not to be queued but in AP case we have to queue PDU's in
     * different scenarios
     */
    switch(interfacePriv->interfaceMode)
    {
        case CSR_WIFI_ROUTER_CTRL_MODE_AP:
        case CSR_WIFI_ROUTER_CTRL_MODE_P2PGO:
            /* For this mode processing done below */
            break;
        default:
            /* In case of STA/IBSS/P2PCLI/AMP, no checks needed send the packet down & return */
            unifi_trace(priv, UDBG5, "In %s, interface mode is %x \n", __FUNCTION__, interfacePriv->interfaceMode);
            if (interfacePriv->interfaceMode == CSR_WIFI_ROUTER_CTRL_MODE_NONE) {
                unifi_warning(priv, "In %s, interface mode NONE \n", __FUNCTION__);
            }
            if ((result = ul_send_signal_unpacked(priv, &signal, bulkdata))) {
                status = CSR_RESULT_FAILURE;
            }
            return status;
    }

    /* -----Only AP/P2pGO mode handling falls below----- */

    /* convert priority to queue */
    priority_q = unifi_frame_priority_to_queue((CSR_PRIORITY) priority);

    /* check the powersave status of the peer */
    if (staRecord && (staRecord->currentPeerState ==
                     CSR_WIFI_ROUTER_CTRL_PEER_CONNECTED_POWER_SAVE)) {
        /* Peer is dozing & packet have to be delivered, so buffer the packet &
         * update the TIM
         */
        queuePacketDozing = TRUE;
    }

    /* find the type of frame unicast or mulicast/broadcast */
    if (*peerMacAddress & 0x1) {
        /* Multicast/broadCast data are always triggered by vif_availability.ind
         * at the DTIM
         */
        pktType = CSR_WIFI_MULTICAST_PDU;
    } else {
        pktType = CSR_WIFI_UNICAST_PDU;
    }

    /* Fetch the frame control field from mac header & check for frame type */
    frmCtrl = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(macHdrLocation);

    /* Processing done according to Frame/Packet type */
    frameType =  ((frmCtrl & 0x000c) >> FRAME_CONTROL_TYPE_FIELD_OFFSET);
    switch(frameType)
    {
        case IEEE802_11_FRAMETYPE_MANAGEMENT:

            switch(pktType)
            {
                case CSR_WIFI_UNICAST_PDU:
                    unifi_trace(priv, UDBG5, "management unicast PDU in uf_process_ma_packet_req \n");
                    /* push the packet in to the queue with appropriate mgt list */
                    if (!staRecord) {
                        /* push the packet to the unifi if list is empty (if packet lost how to re-enque) */
                        if (list_empty(&interfacePriv->genericMgtFrames)) {
#ifdef CSR_SUPPORT_SME
                            if(!(IS_DTIM_ACTIVE(interfacePriv->dtimActive,interfacePriv->multicastPduHostTag))) {
#endif

                            unifi_trace(priv, UDBG3, "genericMgtFrames list is empty uf_process_ma_packet_req \n");
                            result = ul_send_signal_unpacked(priv, &signal, bulkdata);
                            /*  reque only on ENOSPC */
                            if(result == -ENOSPC) {
                                /* requeue the failed packet to genericMgtFrame with same position */
                                unifi_trace(priv, UDBG1, "(ENOSPC) Sending genericMgtFrames Failed so buffering\n");
                                list = &interfacePriv->genericMgtFrames;
                                requeueOnSamePos = TRUE;
                            }
#ifdef CSR_SUPPORT_SME
                            }else{
                                list = &interfacePriv->genericMgtFrames;
                                unifi_trace(priv, UDBG3, "genericMgtFrames queue empty and dtim started\n hosttag is 0x%x,\n",signal.u.MaPacketRequest.HostTag);
                                update_eosp_to_head_of_broadcast_list_head(priv,interfaceTag);
                           }
#endif
                        } else {
                            /* Queue the packet to genericMgtFrame of unifi_priv_t data structure */
                            list = &interfacePriv->genericMgtFrames;
                            unifi_trace(priv, UDBG2, "genericMgtFrames queue not empty\n");
                        }
                    } else {
                        /* check peer power state */
                        if (queuePacketDozing || !list_empty(&staRecord->mgtFrames) || IS_DTIM_ACTIVE(interfacePriv->dtimActive,interfacePriv->multicastPduHostTag)) {
                            /* peer is in dozing mode, so queue packet in mgt frame list of station record */
                           /*if multicast traffic is going on, buffer the unicast packets*/
                            list = &staRecord->mgtFrames;

                            unifi_trace(priv, UDBG1, "staRecord->MgtFrames list empty? = %s, handle = %d, queuePacketDozing = %d\n",
                                        (list_empty(&staRecord->mgtFrames))? "YES": "NO", staRecord->assignedHandle, queuePacketDozing);
                            if(IS_DTIM_ACTIVE(interfacePriv->dtimActive,interfacePriv->multicastPduHostTag)){
                                update_eosp_to_head_of_broadcast_list_head(priv,interfaceTag);
                            }

                        } else {
                            unifi_trace(priv, UDBG5, "staRecord->mgtFrames list is empty uf_process_ma_packet_req \n");
                            result = ul_send_signal_unpacked(priv, &signal, bulkdata);
                            if(result == -ENOSPC) {
                                /* requeue the failed packet to staRecord->mgtFrames with same position */
                                list = &staRecord->mgtFrames;
                                requeueOnSamePos = TRUE;
                                unifi_trace(priv, UDBG1, "(ENOSPC) Sending MgtFrames Failed handle = %d so buffering\n",staRecord->assignedHandle);
                                priv->pausedStaHandle[0]=(u8)(staRecord->assignedHandle);
                            } else if (result) {
                                status = CSR_RESULT_FAILURE;
                            }
                        }
                    }
                    break;
                case CSR_WIFI_MULTICAST_PDU:
                    unifi_trace(priv, UDBG5, "management multicast/broadcast PDU in uf_process_ma_packet_req 'QUEUE it' \n");
                    /* Queue the packet to genericMulticastOrBroadCastMgtFrames of unifi_priv_t data structure
                     * will be sent when we receive VIF AVAILABILITY from firmware as part of DTIM
                     */

                    list = &interfacePriv->genericMulticastOrBroadCastMgtFrames;
                    if((interfacePriv->interfaceMode != CSR_WIFI_ROUTER_CTRL_MODE_IBSS) &&
                            (list_empty(&interfacePriv->genericMulticastOrBroadCastMgtFrames))) {
                        setBcTim=TRUE;
                    }
                    break;
                default:
                    unifi_error(priv, "condition never meets: packet type unrecognized\n");
            }
            break;
        case IEEE802_11_FRAMETYPE_DATA:
            switch(pktType)
            {
                case CSR_WIFI_UNICAST_PDU:
                    unifi_trace(priv, UDBG5, "data unicast PDU in uf_process_ma_packet_req \n");
                    /* check peer power state, list status & peer port status */
                    if(!staRecord) {
                        unifi_error(priv, "In %s unicast but staRecord = NULL\n", __FUNCTION__);
                        return CSR_RESULT_FAILURE;
                    } else if (queuePacketDozing || isRouterBufferEnabled(priv,priority_q)|| !list_empty(&staRecord->dataPdu[priority_q]) || IS_DTIM_ACTIVE(interfacePriv->dtimActive,interfacePriv->multicastPduHostTag)) {
                        /* peer is in dozing mode, so queue packet in mgt frame list of station record */
                        /* if multicast traffic is going on, buffet the unicast packets */
                        unifi_trace(priv, UDBG2, "Enqueued to staRecord->dataPdu[%d] queuePacketDozing=%d,\
                                Buffering enabled = %d \n", priority_q,queuePacketDozing,isRouterBufferEnabled(priv,priority_q));
                        list = &staRecord->dataPdu[priority_q];
                    } else {
                        unifi_trace(priv, UDBG5, "staRecord->dataPdu[%d] list is empty uf_process_ma_packet_req \n", priority_q);
                        /* Pdu allowed to send to unifi */
                        result = ul_send_signal_unpacked(priv, &signal, bulkdata);
                        if(result == -ENOSPC) {
                            /* requeue the failed packet to staRecord->dataPdu[priority_q] with same position */
                            unifi_trace(priv, UDBG1, "(ENOSPC) Sending Unicast DataPDU to queue %d Failed so buffering\n",priority_q);
                            requeueOnSamePos = TRUE;
                            list = &staRecord->dataPdu[priority_q];
                            priv->pausedStaHandle[priority_q]=(u8)(staRecord->assignedHandle);
                            if(!isRouterBufferEnabled(priv,priority_q)) {
                                unifi_error(priv,"Buffering Not enabled for queue %d \n",priority_q);
                            }
                        } else if (result) {
                            status = CSR_RESULT_FAILURE;
                        }
                    }
                    break;
                case CSR_WIFI_MULTICAST_PDU:
                    unifi_trace(priv, UDBG5, "data multicast/broadcast PDU in uf_process_ma_packet_req \n");
                    /* Queue the packet to genericMulticastOrBroadCastFrames list of unifi_priv_t data structure
                     * will be sent when we receive VIF AVAILABILITY from firmware as part of DTIM
                     */
                    list = &interfacePriv->genericMulticastOrBroadCastFrames;
                    if(list_empty(&interfacePriv->genericMulticastOrBroadCastFrames)) {
                        setBcTim = TRUE;
                    }
                    break;
                default:
                    unifi_error(priv, "condition never meets: packet type un recognized\n");
            }
            break;
        default:
            unifi_error(priv, "unrecognized frame type\n");
    }
    if(list) {
        status = enque_tx_data_pdu(priv, bulkdata,list, &signal,requeueOnSamePos);
        /* Record no. of packet queued for each peer */
        if (staRecord && (pktType == CSR_WIFI_UNICAST_PDU) && (!status)) {
            spin_lock_irqsave(&priv->staRecord_lock,lock_flags);
            staRecord->noOfPktQueued++;
            spin_unlock_irqrestore(&priv->staRecord_lock,lock_flags);
        }
        else if ((pktType == CSR_WIFI_MULTICAST_PDU) && (!status))
        {
            /* If broadcast Tim is set && queuing is successfull, then only update TIM */
            spin_lock_irqsave(&priv->staRecord_lock,lock_flags);
            interfacePriv->noOfbroadcastPktQueued++;
            spin_unlock_irqrestore(&priv->staRecord_lock,lock_flags);
        }
    }
    /* If broadcast Tim is set && queuing is successfull, then only update TIM */
    if(setBcTim && !status) {
        unifi_trace(priv, UDBG3, "tim set due to broadcast pkt\n");
        if (!interfacePriv->bcTimSetReqPendingFlag)
        {
            update_tim(priv,0,CSR_WIFI_TIM_SET,interfaceTag, handle);
        }
        else
        {
            /* Cache the TimSet value so that it will processed immidiatly after
            * completing the current setTim Request
            */
            interfacePriv->bcTimSetReqQueued = CSR_WIFI_TIM_SET;
            unifi_trace(priv, UDBG2, "uf_process_ma_packet_req : One more UpdateDTim Request(:%d) Queued \n",
                        interfacePriv->bcTimSetReqQueued);
        }
    } else if(staRecord && staRecord->currentPeerState ==
                            CSR_WIFI_ROUTER_CTRL_PEER_CONNECTED_POWER_SAVE) {
        if(staRecord->timSet == CSR_WIFI_TIM_RESET || staRecord->timSet == CSR_WIFI_TIM_RESETTING) {
            if(!staRecord->wmmOrQosEnabled) {
                if(!list_empty(&staRecord->mgtFrames) ||
                   !list_empty(&staRecord->dataPdu[3]) ||
                   !list_empty(&staRecord->dataPdu[UNIFI_TRAFFIC_Q_CONTENTION])) {
                    unifi_trace(priv, UDBG3, "tim set due to unicast pkt & peer in powersave\n");
                    if (!staRecord->timRequestPendingFlag){
                        update_tim(priv,staRecord->aid,1,interfaceTag, handle);
                    }
                    else
                    {
                        /* Cache the TimSet value so that it will processed immidiatly after
                         * completing the current setTim Request
                         */
                        staRecord->updateTimReqQueued = 1;
                        unifi_trace(priv, UDBG6, "update_tim : One more UpdateTim Request (Tim value:%d) Queued for AID %x\n", staRecord->updateTimReqQueued,
                                    staRecord->aid);
                    }
                }
            } else {
                /* Check for non delivery enable(i.e trigger enable), all delivery enable & legacy AC for TIM update in firmware */
                u8 allDeliveryEnabled = 0, dataAvailable = 0;
                /* Check if all AC's are Delivery Enabled */
                is_all_ac_deliver_enabled_and_moredata(staRecord, &allDeliveryEnabled, &dataAvailable);
                if (uf_is_more_data_for_non_delivery_ac(staRecord) || (allDeliveryEnabled && dataAvailable)
                    || (!list_empty(&staRecord->mgtFrames))) {
                    if (!staRecord->timRequestPendingFlag) {
                        update_tim(priv,staRecord->aid,1,interfaceTag, handle);
                    }
                    else
                    {
                        /* Cache the TimSet value so that it will processed immidiatly after
                         * completing the current setTim Request
                         */
                        staRecord->updateTimReqQueued = 1;
                        unifi_trace(priv, UDBG6, "update_tim : One more UpdateTim Request (Tim value:%d) Queued for AID %x\n", staRecord->updateTimReqQueued,
                                    staRecord->aid);
                    }
                }
            }
        }
    }

    if((list) && (pktType == CSR_WIFI_UNICAST_PDU && !queuePacketDozing) && !(isRouterBufferEnabled(priv,priority_q)) && !(IS_DTIM_ACTIVE(interfacePriv->dtimActive,interfacePriv->multicastPduHostTag))) {
        unifi_trace(priv, UDBG2, "buffering cleared for queue = %d So resending buffered frames\n",priority_q);
        uf_send_buffered_frames(priv, priority_q);
    }
    unifi_trace(priv, UDBG5, "leaving uf_process_ma_packet_req \n");
    return status;
#else
#ifdef CSR_NATIVE_LINUX
    if (interfaceTag >= CSR_WIFI_NUM_INTERFACES) {
        unifi_error(priv, "interfaceTag >= CSR_WIFI_NUM_INTERFACES, interfacetag = %d\n", interfaceTag);
        return CSR_RESULT_FAILURE;
    }
    /* Frame ma-packet.req, this is saved/transmitted depend on queue state */
    unifi_frame_ma_packet_req(priv, priority, TransmitRate, hostTag, interfaceTag,
            transmissionControl, leSenderProcessId,
            peerMacAddress, &signal);
    result = ul_send_signal_unpacked(priv, &signal, bulkdata);
    if (result) {
        return CSR_RESULT_FAILURE;
    }
#endif
    return status;
#endif
}

#ifdef CSR_SUPPORT_SME
s8 uf_get_protection_bit_from_interfacemode(unifi_priv_t *priv, u16 interfaceTag, const u8 *daddr)
{
    s8 protection = 0;
    netInterface_priv_t *interfacePriv = priv->interfacePriv[interfaceTag];

    switch(interfacePriv->interfaceMode)
    {
        case CSR_WIFI_ROUTER_CTRL_MODE_STA:
        case CSR_WIFI_ROUTER_CTRL_MODE_P2PCLI:
        case CSR_WIFI_ROUTER_CTRL_MODE_AMP:
        case CSR_WIFI_ROUTER_CTRL_MODE_IBSS:
            protection = interfacePriv->protect;
            break;
        case CSR_WIFI_ROUTER_CTRL_MODE_AP:
        case CSR_WIFI_ROUTER_CTRL_MODE_P2PGO:
            {
                CsrWifiRouterCtrlStaInfo_t *dstStaInfo = NULL;
                if (daddr[0] & 0x1) {
                    unifi_trace(priv, UDBG3, "broadcast/multicast packet in send_ma_pkt_request\n");
                    /* In this mode, the protect member of priv structure has an information of how
                     * AP/P2PGO has started, & the member updated in set mode request for AP/P2PGO
                     */
                    protection = interfacePriv->protect;
                } else {
                    /* fetch the destination record from staion record database */
                    dstStaInfo = CsrWifiRouterCtrlGetStationRecordFromPeerMacAddress(priv, daddr, interfaceTag);
                    if (!dstStaInfo) {
                        unifi_trace(priv, UDBG3, "peer not found in station record in send_ma_pkt_request\n");
                        return -1;
                    }
                    protection = dstStaInfo->protection;
                }
            }
            break;
        default:
            unifi_trace(priv, UDBG2, "mode unknown in send_ma_pkt_request\n");
    }
    return protection;
}
#endif
#ifdef CSR_SUPPORT_SME
u8 send_multicast_frames(unifi_priv_t *priv, u16 interfaceTag)
{
    int r;
    tx_buffered_packets_t * buffered_pkt = NULL;
    CsrBool moreData = FALSE;
    u8 pduSent =0;
    unsigned long lock_flags;
    netInterface_priv_t *interfacePriv = priv->interfacePriv[interfaceTag];
    u32 hostTag = 0xffffffff;

    func_enter();
    if(!isRouterBufferEnabled(priv,UNIFI_TRAFFIC_Q_VO)) {
        while((interfacePriv->dtimActive)&& (buffered_pkt=dequeue_tx_data_pdu(priv,&interfacePriv->genericMulticastOrBroadCastMgtFrames))) {
            buffered_pkt->transmissionControl |= (TRANSMISSION_CONTROL_TRIGGER_MASK);
            moreData = (buffered_pkt->transmissionControl & TRANSMISSION_CONTROL_EOSP_MASK)?FALSE:TRUE;


            unifi_trace(priv,UDBG2,"DTIM Occurred for interface:sending Mgt packet %d\n",interfaceTag);

            if((r=frame_and_send_queued_pdu(priv,buffered_pkt,NULL,moreData,FALSE)) == -ENOSPC) {
               unifi_trace(priv,UDBG1,"frame_and_send_queued_pdu failed with ENOSPC for host tag = %x\n", buffered_pkt->hostTag);
               /* Enqueue at the head of the queue */
               spin_lock_irqsave(&priv->tx_q_lock,lock_flags);
               list_add(&buffered_pkt->q, &interfacePriv->genericMulticastOrBroadCastMgtFrames);
               spin_unlock_irqrestore(&priv->tx_q_lock,lock_flags);
               break;
            } else {
                unifi_trace(priv,UDBG1,"send_multicast_frames: Send genericMulticastOrBroadCastMgtFrames (%x, %x)\n",
                                        buffered_pkt->hostTag,
                                        r);
                if(r) {
                   unifi_net_data_free(priv, &buffered_pkt->bulkdata);
                }
                if(!moreData) {

                    interfacePriv->dtimActive = FALSE;
                    if(!r) {
                        hostTag = buffered_pkt->hostTag;
                        pduSent++;
                    } else {
                        send_vif_availibility_rsp(priv,uf_get_vif_identifier(interfacePriv->interfaceMode,interfaceTag),CSR_RC_UNSPECIFIED_FAILURE);
                    }
                }
                /* Buffered frame sent successfully */
                spin_lock_irqsave(&priv->staRecord_lock,lock_flags);
                interfacePriv->noOfbroadcastPktQueued--;
                spin_unlock_irqrestore(&priv->staRecord_lock,lock_flags);
                kfree(buffered_pkt);
           }

        }
    }
    if(!isRouterBufferEnabled(priv,UNIFI_TRAFFIC_Q_CONTENTION)) {
        while((interfacePriv->dtimActive)&& (buffered_pkt=dequeue_tx_data_pdu(priv,&interfacePriv->genericMulticastOrBroadCastFrames))) {
            buffered_pkt->transmissionControl |= TRANSMISSION_CONTROL_TRIGGER_MASK;
            moreData = (buffered_pkt->transmissionControl & TRANSMISSION_CONTROL_EOSP_MASK)?FALSE:TRUE;


            if((r=frame_and_send_queued_pdu(priv,buffered_pkt,NULL,moreData,FALSE)) == -ENOSPC) {
                /* Clear the trigger bit transmission control*/
                buffered_pkt->transmissionControl &= ~(TRANSMISSION_CONTROL_TRIGGER_MASK);
                /* Enqueue at the head of the queue */
                spin_lock_irqsave(&priv->tx_q_lock,lock_flags);
                list_add(&buffered_pkt->q, &interfacePriv->genericMulticastOrBroadCastFrames);
                spin_unlock_irqrestore(&priv->tx_q_lock,lock_flags);
                break;
            } else {
                if(r) {
                    unifi_trace(priv,UDBG1,"send_multicast_frames: Send genericMulticastOrBroadCastFrame failed (%x, %x)\n",
                                            buffered_pkt->hostTag,
                                            r);
                    unifi_net_data_free(priv, &buffered_pkt->bulkdata);
                }
                if(!moreData) {
                    interfacePriv->dtimActive = FALSE;
                    if(!r) {
                        pduSent ++;
                        hostTag = buffered_pkt->hostTag;
                    } else {
                        send_vif_availibility_rsp(priv,uf_get_vif_identifier(interfacePriv->interfaceMode,interfaceTag),CSR_RC_UNSPECIFIED_FAILURE);
                    }
                }
                /* Buffered frame sent successfully */
                spin_lock_irqsave(&priv->staRecord_lock,lock_flags);
                interfacePriv->noOfbroadcastPktQueued--;
                spin_unlock_irqrestore(&priv->staRecord_lock,lock_flags);
                kfree(buffered_pkt);
            }
        }
    }
    if((interfacePriv->dtimActive == FALSE)) {
        /* Record the host Tag*/
        unifi_trace(priv,UDBG2,"send_multicast_frames: Recorded hostTag of EOSP packet: = 0x%x\n",hostTag);
        interfacePriv->multicastPduHostTag = hostTag;
    }
    return pduSent;
}
#endif
void uf_process_ma_vif_availibility_ind(unifi_priv_t *priv,u8 *sigdata,
                                        u32 siglen)
{
#ifdef CSR_SUPPORT_SME
    CSR_SIGNAL signal;
    CSR_MA_VIF_AVAILABILITY_INDICATION *ind;
    int r;
    u16 interfaceTag;
    u8 pduSent =0;
    CSR_RESULT_CODE resultCode = CSR_RC_SUCCESS;
    netInterface_priv_t *interfacePriv;

    func_enter();
    unifi_trace(priv, UDBG3,
            "uf_process_ma_vif_availibility_ind: Process signal 0x%.4X\n",
            *((u16*)sigdata));

    r = read_unpack_signal(sigdata, &signal);
    if (r) {
        unifi_error(priv,
                    "uf_process_ma_vif_availibility_ind: Received unknown signal 0x%.4X.\n",
                    CSR_GET_UINT16_FROM_LITTLE_ENDIAN(sigdata));
        func_exit();
        return;
    }
    ind = &signal.u.MaVifAvailabilityIndication;
    interfaceTag=ind->VirtualInterfaceIdentifier & 0xff;

    if (interfaceTag >= CSR_WIFI_NUM_INTERFACES) {
        unifi_error(priv, "in vif_availability_ind interfaceTag is wrong\n");
        return;
    }

    interfacePriv = priv->interfacePriv[interfaceTag];

    if(ind->Multicast) {
        if(list_empty(&interfacePriv->genericMulticastOrBroadCastFrames) &&
            list_empty(&interfacePriv->genericMulticastOrBroadCastMgtFrames)) {
            /* This condition can occur because of a potential race where the
               TIM is not yet reset as host is waiting for confirm but it is sent
               by firmware and DTIM occurs*/
            unifi_notice(priv,"ma_vif_availibility_ind recevied for multicast but queues are empty%d\n",interfaceTag);
            send_vif_availibility_rsp(priv,ind->VirtualInterfaceIdentifier,CSR_RC_NO_BUFFERED_BROADCAST_MULTICAST_FRAMES);
            interfacePriv->dtimActive = FALSE;
            if(interfacePriv->multicastPduHostTag == 0xffffffff) {
                unifi_notice(priv,"ma_vif_availibility_ind recevied for multicast but queues are empty%d\n",interfaceTag);
                /* This may be an extra request in very rare race conditions but it is fine as it would atleast remove the potential lock up */
                if (!interfacePriv->bcTimSetReqPendingFlag)
                {
                    update_tim(priv,0,CSR_WIFI_TIM_RESET,interfaceTag, 0xFFFFFFFF);
                }
                else
                {
                    /* Cache the TimSet value so that it will processed immidiatly after
                     * completing the current setTim Request
                     */
                    interfacePriv->bcTimSetReqQueued = CSR_WIFI_TIM_RESET;
                    unifi_trace(priv, UDBG2, "uf_process_ma_vif_availibility_ind : One more UpdateDTim Request(%d) Queued \n",
                                interfacePriv->bcTimSetReqQueued);
                }
            }
            return;
        }
        if(interfacePriv->dtimActive) {
            unifi_trace(priv,UDBG2,"DTIM Occurred for already active DTIM interface %d\n",interfaceTag);
            return;
        } else {
            unifi_trace(priv,UDBG2,"DTIM Occurred for interface %d\n",interfaceTag);
            if(list_empty(&interfacePriv->genericMulticastOrBroadCastFrames)) {
                set_eosp_transmit_ctrl(priv,&interfacePriv->genericMulticastOrBroadCastMgtFrames);
            } else {
                set_eosp_transmit_ctrl(priv,&interfacePriv->genericMulticastOrBroadCastFrames);
            }
        }
        interfacePriv->dtimActive = TRUE;
        pduSent = send_multicast_frames(priv,interfaceTag);
    }
    else {
        unifi_error(priv,"Interface switching is not supported %d\n",interfaceTag);
        resultCode = CSR_RC_NOT_SUPPORTED;
        send_vif_availibility_rsp(priv,ind->VirtualInterfaceIdentifier,CSR_RC_NOT_SUPPORTED);
    }
#endif
}
#ifdef CSR_SUPPORT_SME

#define  GET_ACTIVE_INTERFACE_TAG(priv) 0

static CsrBool uf_is_more_data_for_delivery_ac(unifi_priv_t *priv, CsrWifiRouterCtrlStaInfo_t *staRecord)
{
    s8 i;

    for(i=UNIFI_TRAFFIC_Q_VO; i >= UNIFI_TRAFFIC_Q_BK; i--)
    {
        if(((staRecord->powersaveMode[i]==CSR_WIFI_AC_DELIVERY_ONLY_ENABLE)
             ||(staRecord->powersaveMode[i]==CSR_WIFI_AC_TRIGGER_AND_DELIVERY_ENABLED))
             &&(!list_empty(&staRecord->dataPdu[i]))) {
            unifi_trace(priv,UDBG2,"uf_is_more_data_for_delivery_ac: Data Available AC = %d\n", i);
            return TRUE;
        }
    }

    unifi_trace(priv,UDBG2,"uf_is_more_data_for_delivery_ac: Data NOT Available \n");
    return FALSE;
}

static CsrBool uf_is_more_data_for_usp_delivery(unifi_priv_t *priv, CsrWifiRouterCtrlStaInfo_t *staRecord, unifi_TrafficQueue queue)
{
    s8 i;

    for(i = queue; i >= UNIFI_TRAFFIC_Q_BK; i--)
    {
        if(((staRecord->powersaveMode[i]==CSR_WIFI_AC_DELIVERY_ONLY_ENABLE)
             ||(staRecord->powersaveMode[i]==CSR_WIFI_AC_TRIGGER_AND_DELIVERY_ENABLED))
             &&(!list_empty(&staRecord->dataPdu[i]))) {
            unifi_trace(priv,UDBG2,"uf_is_more_data_for_usp_delivery: Data Available AC = %d\n", i);
            return TRUE;
        }
    }

    unifi_trace(priv,UDBG2,"uf_is_more_data_for_usp_delivery: Data NOT Available \n");
    return FALSE;
}

/*
 * ---------------------------------------------------------------------------
 *  uf_send_buffered_data_from_delivery_ac
 *
 *      This function takes care of
 *      -> Parsing the delivery enabled queue & sending frame down to HIP
 *      -> Setting EOSP=1 when USP to be terminated
 *      -> Depending on MAX SP length services the USP
 *
 * NOTE:This function always called from uf_handle_uspframes_delivery(), Dont
 *      call this function from any other location in code
 *
 *  Arguments:
 *      priv        Pointer to device private context struct
 *      vif         interface specific HIP vif instance
 *      staInfo     peer for which UAPSD to be scheduled
 *      queue       AC from which Data to be sent in USP
 *      txList      access category for processing list
 * ---------------------------------------------------------------------------
 */
void uf_send_buffered_data_from_delivery_ac(unifi_priv_t *priv,
                                            CsrWifiRouterCtrlStaInfo_t * staInfo,
                                            u8 queue,
                                            struct list_head *txList)
{

    u16 interfaceTag = GET_ACTIVE_INTERFACE_TAG(priv);
    tx_buffered_packets_t * buffered_pkt = NULL;
    unsigned long lock_flags;
    CsrBool eosp=FALSE;
    s8 r =0;
    CsrBool moreData = FALSE;
    netInterface_priv_t *interfacePriv = priv->interfacePriv[interfaceTag];

    unifi_trace(priv, UDBG2, "++uf_send_buffered_data_from_delivery_ac, active=%x\n", staInfo->uapsdActive);

    if (queue > UNIFI_TRAFFIC_Q_VO)
    {
        return;
    }
    while((buffered_pkt=dequeue_tx_data_pdu(priv, txList))) {
        if((IS_DTIM_ACTIVE(interfacePriv->dtimActive,interfacePriv->multicastPduHostTag))) {
            unifi_trace(priv, UDBG2, "uf_send_buffered_data_from_delivery_ac: DTIM Active, suspend UAPSD, staId: 0x%x\n",
                        staInfo->aid);

            /* Once resume called, the U-APSD delivery operation will resume */
            spin_lock_irqsave(&priv->staRecord_lock,lock_flags);
            staInfo->uspSuspend = TRUE;
            spin_unlock_irqrestore(&priv->staRecord_lock,lock_flags);
            /* re-queueing the packet as DTIM started */
            spin_lock_irqsave(&priv->tx_q_lock,lock_flags);
            list_add(&buffered_pkt->q,txList);
            spin_unlock_irqrestore(&priv->tx_q_lock,lock_flags);
            break;
        }

        buffered_pkt->transmissionControl &=
                 ~(TRANSMISSION_CONTROL_TRIGGER_MASK | TRANSMISSION_CONTROL_EOSP_MASK);


        if((staInfo->wmmOrQosEnabled == TRUE)&&(staInfo->uapsdActive == TRUE)) {

             buffered_pkt->transmissionControl = TRANSMISSION_CONTROL_TRIGGER_MASK;

             /* Check All delivery enables Ac for more data, because caller of this
              * function not aware about last packet
              * (First check in moreData fetching helps in draining out Mgt frames Q)
              */
              moreData = (!list_empty(txList) || uf_is_more_data_for_usp_delivery(priv, staInfo, queue));

              if(staInfo->noOfSpFramesSent == (staInfo->maxSpLength - 1)) {
                  moreData = FALSE;
              }

              if(moreData == FALSE) {
                  eosp = TRUE;
                  buffered_pkt->transmissionControl =
                      (TRANSMISSION_CONTROL_TRIGGER_MASK | TRANSMISSION_CONTROL_EOSP_MASK);
              }
        } else {
            /* Non QoS and non U-APSD */
            unifi_warning(priv, "uf_send_buffered_data_from_delivery_ac: non U-APSD !!! \n");
        }

        unifi_trace(priv,UDBG2,"uf_send_buffered_data_from_delivery_ac : MoreData:%d, EOSP:%d\n",moreData,eosp);

        if((r=frame_and_send_queued_pdu(priv,buffered_pkt,staInfo,moreData,eosp)) == -ENOSPC) {

            unifi_trace(priv, UDBG2, "uf_send_buffered_data_from_delivery_ac: UASPD suspended, ENOSPC in hipQ=%x\n", queue);

            /* Once resume called, the U-APSD delivery operation will resume */
            spin_lock_irqsave(&priv->staRecord_lock,lock_flags);
            staInfo->uspSuspend = TRUE;
            spin_unlock_irqrestore(&priv->staRecord_lock,lock_flags);

            spin_lock_irqsave(&priv->tx_q_lock,lock_flags);
            list_add(&buffered_pkt->q,txList);
            spin_unlock_irqrestore(&priv->tx_q_lock,lock_flags);
            priv->pausedStaHandle[queue]=(u8)(staInfo->assignedHandle);
            break;
        } else {
            if(r){
                /* the PDU failed where we can't do any thing so free the storage */
                unifi_net_data_free(priv, &buffered_pkt->bulkdata);
            }
            kfree(buffered_pkt);
            spin_lock_irqsave(&priv->staRecord_lock,lock_flags);
            staInfo->noOfSpFramesSent++;
            if((!moreData) || (staInfo->noOfSpFramesSent == staInfo->maxSpLength)) {
                unifi_trace(priv, UDBG2, "uf_send_buffered_data_from_delivery_ac: Terminating USP\n");
                staInfo->uapsdActive = FALSE;
                staInfo->uspSuspend = FALSE;
                staInfo->noOfSpFramesSent = 0;
                spin_unlock_irqrestore(&priv->staRecord_lock,lock_flags);
                break;
            }
            spin_unlock_irqrestore(&priv->staRecord_lock,lock_flags);
        }
    }
    unifi_trace(priv, UDBG2, "--uf_send_buffered_data_from_delivery_ac, active=%x\n", staInfo->uapsdActive);
}

void uf_send_buffered_data_from_ac(unifi_priv_t *priv,
                                   CsrWifiRouterCtrlStaInfo_t * staInfo,
                                   u8 queue,
                                   struct list_head *txList)
{
    tx_buffered_packets_t * buffered_pkt = NULL;
    unsigned long lock_flags;
    CsrBool eosp=FALSE;
    CsrBool moreData = FALSE;
    s8 r =0;

    func_enter();

    unifi_trace(priv,UDBG2,"uf_send_buffered_data_from_ac :\n");

    while(!isRouterBufferEnabled(priv,queue) &&
                    ((buffered_pkt=dequeue_tx_data_pdu(priv, txList))!=NULL)){

        buffered_pkt->transmissionControl &=
                 ~(TRANSMISSION_CONTROL_TRIGGER_MASK|TRANSMISSION_CONTROL_EOSP_MASK);

        unifi_trace(priv,UDBG3,"uf_send_buffered_data_from_ac : MoreData:%d, EOSP:%d\n",moreData,eosp);

        if((r=frame_and_send_queued_pdu(priv,buffered_pkt,staInfo,moreData,eosp)) == -ENOSPC) {
           /* Enqueue at the head of the queue */
           spin_lock_irqsave(&priv->tx_q_lock,lock_flags);
           list_add(&buffered_pkt->q,txList);
           spin_unlock_irqrestore(&priv->tx_q_lock,lock_flags);
           if(staInfo != NULL){
              priv->pausedStaHandle[queue]=(u8)(staInfo->assignedHandle);
           }
           unifi_trace(priv,UDBG3," uf_send_buffered_data_from_ac: PDU sending failed .. no space for queue %d \n",queue);
           } else {
            if(r){
                /* the PDU failed where we can't do any thing so free the storage */
                unifi_net_data_free(priv, &buffered_pkt->bulkdata);
            }
            kfree(buffered_pkt);
      }
  }

  func_exit();

}

void uf_send_buffered_frames(unifi_priv_t *priv,unifi_TrafficQueue q)
{
    u16 interfaceTag = GET_ACTIVE_INTERFACE_TAG(priv);
    u32 startIndex=0,endIndex=0;
    CsrWifiRouterCtrlStaInfo_t * staInfo = NULL;
    u8 queue;
    CsrBool moreData = FALSE;

    netInterface_priv_t *interfacePriv = priv->interfacePriv[interfaceTag];

    if(!((interfacePriv->interfaceMode == CSR_WIFI_ROUTER_CTRL_MODE_AP) ||
        (interfacePriv->interfaceMode == CSR_WIFI_ROUTER_CTRL_MODE_P2PGO)))
        return;
    func_enter();

    queue = (q<=3)?q:0;

    if(interfacePriv->dtimActive) {
        /* this function updates dtimActive*/
        send_multicast_frames(priv,interfaceTag);
        if(!interfacePriv->dtimActive) {
            moreData = (!list_empty(&interfacePriv->genericMulticastOrBroadCastMgtFrames) ||
             !list_empty(&interfacePriv->genericMulticastOrBroadCastFrames));
            if(!moreData) {
                if (!interfacePriv->bcTimSetReqPendingFlag)
                {
                    update_tim(priv,0,CSR_WIFI_TIM_RESET,interfaceTag, 0XFFFFFFFF);
                }
                else
                {
                    /* Cache the TimSet value so that it will processed immidiatly after
                     * completing the current setTim Request
                     */
                    interfacePriv->bcTimSetReqQueued = CSR_WIFI_TIM_RESET;
                    unifi_trace(priv, UDBG2, "uf_send_buffered_frames : One more UpdateDTim Request(%d) Queued \n",
                                interfacePriv->bcTimSetReqQueued);
                }
            }
        } else {
            moreData = (!list_empty(&interfacePriv->genericMulticastOrBroadCastMgtFrames) ||
                        !list_empty(&interfacePriv->genericMulticastOrBroadCastFrames));
           if(!moreData) {
               /* This should never happen but if it happens, we need a way out */
               unifi_error(priv,"ERROR: No More Data but DTIM is active sending Response\n");
               send_vif_availibility_rsp(priv,uf_get_vif_identifier(interfacePriv->interfaceMode,interfaceTag),CSR_RC_NO_BUFFERED_BROADCAST_MULTICAST_FRAMES);
               interfacePriv->dtimActive = FALSE;
           }
        }
        func_exit();
        return;
    }
    if(priv->pausedStaHandle[queue] > 7) {
        priv->pausedStaHandle[queue] = 0;
    }

    if(queue == UNIFI_TRAFFIC_Q_VO) {


        unifi_trace(priv,UDBG2,"uf_send_buffered_frames : trying mgt from queue=%d\n",queue);
        for(startIndex= 0; startIndex < UNIFI_MAX_CONNECTIONS;startIndex++) {
            staInfo =  CsrWifiRouterCtrlGetStationRecordFromHandle(priv,startIndex,interfaceTag);
            if(!staInfo ) {
                continue;
            } else if((staInfo->currentPeerState == CSR_WIFI_ROUTER_CTRL_PEER_CONNECTED_POWER_SAVE)
                       &&(staInfo->uapsdActive == FALSE) ) {
                continue;
            }

            if((staInfo != NULL)&&(staInfo->currentPeerState == CSR_WIFI_ROUTER_CTRL_PEER_CONNECTED_ACTIVE)
                               &&(staInfo->uapsdActive == FALSE)){
                            /*Non-UAPSD case push the management frames out*/
               if(!list_empty(&staInfo->mgtFrames)){
                    uf_send_buffered_data_from_ac(priv,staInfo, UNIFI_TRAFFIC_Q_VO, &staInfo->mgtFrames);
                }
            }

            if(isRouterBufferEnabled(priv,queue)) {
                unifi_notice(priv,"uf_send_buffered_frames : No space Left for queue = %d\n",queue);
                break;
            }
        }
        /*push generic management frames out*/
        if(!list_empty(&interfacePriv->genericMgtFrames)) {
            unifi_trace(priv,UDBG2,"uf_send_buffered_frames : trying generic mgt from queue=%d\n",queue);
            uf_send_buffered_data_from_ac(priv,staInfo, UNIFI_TRAFFIC_Q_VO, &interfacePriv->genericMgtFrames);
        }
    }


    unifi_trace(priv,UDBG2,"uf_send_buffered_frames : Resume called for Queue=%d\n",queue);
    unifi_trace(priv,UDBG2,"uf_send_buffered_frames : start=%d end=%d\n",startIndex,endIndex);

    startIndex = priv->pausedStaHandle[queue];
    endIndex = (startIndex + UNIFI_MAX_CONNECTIONS -1) % UNIFI_MAX_CONNECTIONS;

    while(startIndex != endIndex) {
        staInfo =  CsrWifiRouterCtrlGetStationRecordFromHandle(priv,startIndex,interfaceTag);
        if(!staInfo) {
            startIndex ++;
            if(startIndex >= UNIFI_MAX_CONNECTIONS) {
                startIndex = 0;
            }
            continue;
        } else if((staInfo->currentPeerState == CSR_WIFI_ROUTER_CTRL_PEER_CONNECTED_POWER_SAVE)
                   &&(staInfo->uapsdActive == FALSE)) {
            startIndex ++;
            if(startIndex >= UNIFI_MAX_CONNECTIONS) {
                startIndex = 0;
            }
            continue;
        }
        /* Peer is active or U-APSD is active so send PDUs to the peer */
        unifi_trace(priv,UDBG2,"uf_send_buffered_frames : trying data from queue=%d\n",queue);


        if((staInfo != NULL)&&(staInfo->currentPeerState == CSR_WIFI_ROUTER_CTRL_PEER_CONNECTED_ACTIVE)
                           &&(staInfo->uapsdActive == FALSE)) {
           if(!list_empty(&staInfo->dataPdu[queue])) {

               /*Non-UAPSD case push the AC frames out*/
               uf_send_buffered_data_from_ac(priv, staInfo, queue, (&staInfo->dataPdu[queue]));
           }
        }
        startIndex ++;
        if(startIndex >= UNIFI_MAX_CONNECTIONS) {
           startIndex = 0;
        }
    }
    if(isRouterBufferEnabled(priv,queue)) {
        priv->pausedStaHandle[queue] = endIndex;
    } else {
        priv->pausedStaHandle[queue] = 0;
    }

    /* U-APSD might have stopped because of ENOSPC in lib_hip (pause activity).
     * So restart it if U-APSD was active with any of the station
     */
    unifi_trace(priv, UDBG4, "csrWifiHipSendBufferedFrames: UAPSD Resume Q=%x\n", queue);
    resume_suspended_uapsd(priv, interfaceTag);
    func_exit();
}


CsrBool uf_is_more_data_for_non_delivery_ac(CsrWifiRouterCtrlStaInfo_t *staRecord)
{
    u8 i;

    for(i=0;i<=3;i++)
    {
        if(((staRecord->powersaveMode[i]==CSR_WIFI_AC_TRIGGER_ONLY_ENABLED)
                ||(staRecord->powersaveMode[i]==CSR_WIFI_AC_LEGACY_POWER_SAVE))
                &&(!list_empty(&staRecord->dataPdu[i]))){

         return TRUE;
        }
    }

    if(((staRecord->powersaveMode[UNIFI_TRAFFIC_Q_VO]==CSR_WIFI_AC_TRIGGER_ONLY_ENABLED)
            ||(staRecord->powersaveMode[UNIFI_TRAFFIC_Q_VO]==CSR_WIFI_AC_LEGACY_POWER_SAVE))
            &&(!list_empty(&staRecord->mgtFrames))){

     return TRUE;
    }



    return FALSE;
}


int uf_process_station_records_for_sending_data(unifi_priv_t *priv,u16 interfaceTag,
                                                 CsrWifiRouterCtrlStaInfo_t *srcStaInfo,
                                                 CsrWifiRouterCtrlStaInfo_t *dstStaInfo)
{
    netInterface_priv_t *interfacePriv = priv->interfacePriv[interfaceTag];

    unifi_trace(priv, UDBG5, "entering uf_process_station_records_for_sending_data\n");

    if (srcStaInfo->currentPeerState == CSR_WIFI_ROUTER_CTRL_PEER_DISCONNECTED) {
        unifi_error(priv, "Peer State not connected AID = %x, handle = %x, control port state = %x\n",
                    srcStaInfo->aid, srcStaInfo->assignedHandle, srcStaInfo->peerControlledPort->port_action);
        return -1;
    }
    switch (interfacePriv->interfaceMode)
    {
        case CSR_WIFI_ROUTER_CTRL_MODE_P2PGO:
        case CSR_WIFI_ROUTER_CTRL_MODE_AP:
            unifi_trace(priv, UDBG5, "mode is AP/P2PGO\n");
            break;
        default:
            unifi_warning(priv, "mode is nor AP neither P2PGO, packet cant be xmit\n");
            return -1;
    }

    switch(dstStaInfo->peerControlledPort->port_action)
    {
        case CSR_WIFI_ROUTER_CTRL_PORT_ACTION_8021X_PORT_CLOSED_DISCARD:
        case CSR_WIFI_ROUTER_CTRL_PORT_ACTION_8021X_PORT_CLOSED_BLOCK:
            unifi_trace(priv, UDBG5, "destination port is closed/blocked, discarding the packet\n");
            return -1;
        default:
            unifi_trace(priv, UDBG5, "destination port state is open\n");
    }

    /* port state is open, destination station record is valid, Power save state is
     * validated in uf_process_ma_packet_req function
     */
    unifi_trace(priv, UDBG5, "leaving uf_process_station_records_for_sending_data\n");
    return 0;
}


/*
 * ---------------------------------------------------------------------------
 *  uf_handle_uspframes_delivery
 *
 *      This function takes care of handling USP session for peer, when
 *      -> trigger frame from peer
 *      -> suspended USP to be processed (resumed)
 *
 *      NOTE: uf_send_buffered_data_from_delivery_ac() always called from this function, Dont
 *      make a direct call to uf_send_buffered_data_from_delivery_ac() from any other part of
 *      code
 *
 *  Arguments:
 *      priv            Pointer to device private context struct
 *      staInfo         peer for which UAPSD to be scheduled
 *      interfaceTag    virtual interface tag
 * ---------------------------------------------------------------------------
 */
static void uf_handle_uspframes_delivery(unifi_priv_t * priv, CsrWifiRouterCtrlStaInfo_t *staInfo, u16 interfaceTag)
{

    s8 i;
    u8 allDeliveryEnabled = 0, dataAvailable = 0;
    netInterface_priv_t *interfacePriv = priv->interfacePriv[interfaceTag];
    unsigned long lock_flags;

    unifi_trace(priv, UDBG2, " ++ uf_handle_uspframes_delivery, uapsd active=%x, suspended?=%x\n",
                staInfo->uapsdActive, staInfo->uspSuspend);

    /* Check for Buffered frames according to priority order & deliver it
     *  1. AC_VO delivery enable & Mgt frames available
     *  2. Process remaining Ac's from order AC_VO to AC_BK
     */

    /* USP initiated by WMMPS enabled peer  & SET the status flag to TRUE */
    if (!staInfo->uspSuspend && staInfo->uapsdActive)
    {
        unifi_notice(priv, "uf_handle_uspframes_delivery: U-APSD already active! STA=%x:%x:%x:%x:%x:%x\n",
                staInfo->peerMacAddress.a[0], staInfo->peerMacAddress.a[1],
                staInfo->peerMacAddress.a[2], staInfo->peerMacAddress.a[3],
                staInfo->peerMacAddress.a[4], staInfo->peerMacAddress.a[5]);
        return;
    }

    spin_lock_irqsave(&priv->staRecord_lock,lock_flags);
    staInfo->uapsdActive = TRUE;
    staInfo->uspSuspend = FALSE;
    spin_unlock_irqrestore(&priv->staRecord_lock,lock_flags);

    if(((staInfo->powersaveMode[UNIFI_TRAFFIC_Q_VO]==CSR_WIFI_AC_TRIGGER_AND_DELIVERY_ENABLED)||
        (staInfo->powersaveMode[UNIFI_TRAFFIC_Q_VO]==CSR_WIFI_AC_DELIVERY_ONLY_ENABLE))
        && (!list_empty(&staInfo->mgtFrames))) {

         /* Management queue has data &&  UNIFI_TRAFFIC_Q_VO is delivery enable */
        unifi_trace(priv, UDBG4, "uf_handle_uspframes_delivery: Sending buffered management frames\n");
        uf_send_buffered_data_from_delivery_ac(priv, staInfo, UNIFI_TRAFFIC_Q_VO, &staInfo->mgtFrames);
    }

    if (!uf_is_more_data_for_delivery_ac(priv, staInfo)) {
        /* All delivery enable AC's are empty, so QNULL to be sent to terminate the USP
         * NOTE: If we have sent Mgt frame also, we must send QNULL followed to terminate USP
         */
        if (!staInfo->uspSuspend) {
            spin_lock_irqsave(&priv->staRecord_lock,lock_flags);
            staInfo->uapsdActive = FALSE;
            spin_unlock_irqrestore(&priv->staRecord_lock,lock_flags);

            unifi_trace(priv, UDBG2, "uf_handle_uspframes_delivery: sending QNull for trigger\n");
            uf_send_qos_null(priv, interfaceTag, staInfo->peerMacAddress.a, (CSR_PRIORITY) staInfo->triggerFramePriority, staInfo);
            staInfo->triggerFramePriority = CSR_QOS_UP0;
        } else {
            unifi_trace(priv, UDBG2, "uf_handle_uspframes_delivery: MgtQ xfer suspended\n");
        }
    } else {
        for(i = UNIFI_TRAFFIC_Q_VO; i >= UNIFI_TRAFFIC_Q_BK; i--) {
            if(((staInfo->powersaveMode[i]==CSR_WIFI_AC_DELIVERY_ONLY_ENABLE)
                ||(staInfo->powersaveMode[i]==CSR_WIFI_AC_TRIGGER_AND_DELIVERY_ENABLED))
                && (!list_empty(&staInfo->dataPdu[i]))) {
                /* Deliver Data according to AC priority (from VO to BK) as part of USP */
                unifi_trace(priv, UDBG4, "uf_handle_uspframes_delivery: Buffered data frames from Queue (%d) for USP\n", i);
                uf_send_buffered_data_from_delivery_ac(priv, staInfo, i, &staInfo->dataPdu[i]);
            }

            if ((!staInfo->uapsdActive) ||
                    (staInfo->uspSuspend && IS_DTIM_ACTIVE(interfacePriv->dtimActive,interfacePriv->multicastPduHostTag))) {
                /* If DTIM active found on one AC, No need to parse the remaining AC's
                 * as USP suspended. Break out of loop
                 */
                unifi_trace(priv, UDBG2, "uf_handle_uspframes_delivery: suspend=%x,  DTIM=%x, USP terminated=%s\n",
                           staInfo->uspSuspend, IS_DTIM_ACTIVE(interfacePriv->dtimActive,interfacePriv->multicastPduHostTag),
                           staInfo->uapsdActive?"NO":"YES");
                break;
            }
        }
    }

    /* Depending on the USP status, update the TIM accordingly for delivery enabled AC only
     * (since we are not manipulating any Non-delivery list(AC))
     */
    is_all_ac_deliver_enabled_and_moredata(staInfo, &allDeliveryEnabled, &dataAvailable);
    if ((allDeliveryEnabled && !dataAvailable)) {
        if ((staInfo->timSet != CSR_WIFI_TIM_RESET) || (staInfo->timSet != CSR_WIFI_TIM_RESETTING)) {
            staInfo->updateTimReqQueued = (u8) CSR_WIFI_TIM_RESET;
            unifi_trace(priv, UDBG4, " --uf_handle_uspframes_delivery, UAPSD timset\n");
            if (!staInfo->timRequestPendingFlag) {
                update_tim(priv, staInfo->aid, 0, interfaceTag, staInfo->assignedHandle);
            }
        }
    }
    unifi_trace(priv, UDBG2, " --uf_handle_uspframes_delivery, uapsd active=%x, suspend?=%x\n",
                staInfo->uapsdActive, staInfo->uspSuspend);
}

void uf_process_wmm_deliver_ac_uapsd(unifi_priv_t * priv,
                                     CsrWifiRouterCtrlStaInfo_t * srcStaInfo,
                                     u16 qosControl,
                                     u16 interfaceTag)
{
    CSR_PRIORITY priority;
    unifi_TrafficQueue priority_q;
    unsigned long lock_flags;

    unifi_trace(priv, UDBG2, "++uf_process_wmm_deliver_ac_uapsd: uapsdactive?=%x\n", srcStaInfo->uapsdActive);
    /* If recceived Frames trigger Frame and Devlivery enabled AC has data
     * then transmit from High priorty delivery enabled AC
     */
    priority = (CSR_PRIORITY)(qosControl & IEEE802_11_QC_TID_MASK);
    priority_q = unifi_frame_priority_to_queue((CSR_PRIORITY) priority);

    if((srcStaInfo->powersaveMode[priority_q]==CSR_WIFI_AC_TRIGGER_ONLY_ENABLED)
        ||(srcStaInfo->powersaveMode[priority_q]==CSR_WIFI_AC_TRIGGER_AND_DELIVERY_ENABLED)) {
        spin_lock_irqsave(&priv->staRecord_lock,lock_flags);
        srcStaInfo->triggerFramePriority = priority;
        spin_unlock_irqrestore(&priv->staRecord_lock,lock_flags);
        unifi_trace(priv, UDBG2, "uf_process_wmm_deliver_ac_uapsd: trigger frame, Begin U-APSD, triggerQ=%x\n", priority_q);
        uf_handle_uspframes_delivery(priv, srcStaInfo, interfaceTag);
    }
    unifi_trace(priv, UDBG2, "--uf_process_wmm_deliver_ac_uapsd: uapsdactive?=%x\n", srcStaInfo->uapsdActive);
}


void uf_send_qos_null(unifi_priv_t * priv,u16 interfaceTag, const u8 *da,CSR_PRIORITY priority,CsrWifiRouterCtrlStaInfo_t * srcStaInfo)
{
    bulk_data_param_t bulkdata;
    CsrResult csrResult;
    struct sk_buff *skb, *newSkb = NULL;
    CsrWifiMacAddress peerAddress;
    netInterface_priv_t *interfacePriv = priv->interfacePriv[interfaceTag];
    CSR_TRANSMISSION_CONTROL transmissionControl = (TRANSMISSION_CONTROL_EOSP_MASK | TRANSMISSION_CONTROL_TRIGGER_MASK);
    int r;
    CSR_SIGNAL signal;
    u32 priority_q;
    CSR_RATE transmitRate = 0;


    func_enter();
    /* Send a Null Frame to Peer,
     * 32= size of mac header  */
    csrResult = unifi_net_data_malloc(priv, &bulkdata.d[0], MAC_HEADER_SIZE + QOS_CONTROL_HEADER_SIZE);

    if (csrResult != CSR_RESULT_SUCCESS) {
        unifi_error(priv, " failed to allocate request_data. in uf_send_qos_null func\n");
        return ;
    }
    skb = (struct sk_buff *)(bulkdata.d[0].os_net_buf_ptr);
    skb->len = 0;
    bulkdata.d[0].os_data_ptr = skb->data;
    bulkdata.d[0].os_net_buf_ptr = (unsigned char*)skb;
    bulkdata.d[0].net_buf_length = bulkdata.d[0].data_length = skb->len;
    bulkdata.d[1].os_data_ptr = NULL;
    bulkdata.d[1].os_net_buf_ptr = NULL;
    bulkdata.d[1].net_buf_length = bulkdata.d[1].data_length = 0;

    /* For null frames protection bit should not be set in MAC header, so passing value 0 below for protection field */

    if (prepare_and_add_macheader(priv, skb, newSkb, priority, &bulkdata, interfaceTag, da, interfacePriv->bssid.a, 0)) {
        unifi_error(priv, "failed to create MAC header\n");
        unifi_net_data_free(priv, &bulkdata.d[0]);
        return;
    }
    memcpy(peerAddress.a, ((u8 *) bulkdata.d[0].os_data_ptr) + 4, ETH_ALEN);
    /* convert priority to queue */
    priority_q = unifi_frame_priority_to_queue((CSR_PRIORITY) priority);

    /* Frame ma-packet.req, this is saved/transmitted depend on queue state
     * send the null frame at data rate of 1 Mb/s for AP or 6 Mb/s for P2PGO
     */
    switch (interfacePriv->interfaceMode)
    {
        case CSR_WIFI_ROUTER_CTRL_MODE_AP:
            transmitRate = 2;
            break;
        case CSR_WIFI_ROUTER_CTRL_MODE_P2PGO:
            transmitRate = 12;
            break;
        default:
            transmitRate = 0;
    }
    unifi_frame_ma_packet_req(priv, priority, transmitRate, 0xffffffff, interfaceTag,
                              transmissionControl, priv->netdev_client->sender_id,
                              peerAddress.a, &signal);

    r = ul_send_signal_unpacked(priv, &signal, &bulkdata);
    if(r) {
        unifi_error(priv, "failed to send QOS data null packet result: %d\n",r);
        unifi_net_data_free(priv, &bulkdata.d[0]);
    }

    func_exit();
    return;

}
void uf_send_nulldata(unifi_priv_t * priv,u16 interfaceTag, const u8 *da,CSR_PRIORITY priority,CsrWifiRouterCtrlStaInfo_t * srcStaInfo)
{
    bulk_data_param_t bulkdata;
    CsrResult csrResult;
    struct sk_buff *skb, *newSkb = NULL;
    CsrWifiMacAddress peerAddress;
    netInterface_priv_t *interfacePriv = priv->interfacePriv[interfaceTag];
    CSR_TRANSMISSION_CONTROL transmissionControl = 0;
    int r;
    CSR_SIGNAL signal;
    u32 priority_q;
    CSR_RATE transmitRate = 0;
    CSR_MA_PACKET_REQUEST *req = &signal.u.MaPacketRequest;
    unsigned long lock_flags;

    func_enter();
    /* Send a Null Frame to Peer, size = 24 for MAC header */
    csrResult = unifi_net_data_malloc(priv, &bulkdata.d[0], MAC_HEADER_SIZE);

    if (csrResult != CSR_RESULT_SUCCESS) {
        unifi_error(priv, "uf_send_nulldata: Failed to allocate memory for NULL frame\n");
        return ;
    }
    skb = (struct sk_buff *)(bulkdata.d[0].os_net_buf_ptr);
    skb->len = 0;
    bulkdata.d[0].os_data_ptr = skb->data;
    bulkdata.d[0].os_net_buf_ptr = (unsigned char*)skb;
    bulkdata.d[0].net_buf_length = bulkdata.d[0].data_length = skb->len;
    bulkdata.d[1].os_data_ptr = NULL;
    bulkdata.d[1].os_net_buf_ptr = NULL;
    bulkdata.d[1].net_buf_length = bulkdata.d[1].data_length = 0;

    /* For null frames protection bit should not be set in MAC header, so passing value 0 below for protection field */
    if (prepare_and_add_macheader(priv, skb, newSkb, priority, &bulkdata, interfaceTag, da, interfacePriv->bssid.a, 0)) {
        unifi_error(priv, "uf_send_nulldata: Failed to create MAC header\n");
        unifi_net_data_free(priv, &bulkdata.d[0]);
        return;
    }
    memcpy(peerAddress.a, ((u8 *) bulkdata.d[0].os_data_ptr) + 4, ETH_ALEN);
    /* convert priority to queue */
    priority_q = unifi_frame_priority_to_queue((CSR_PRIORITY) priority);
    transmissionControl &= ~(CSR_NO_CONFIRM_REQUIRED);

    /* Frame ma-packet.req, this is saved/transmitted depend on queue state
     * send the null frame at data rate of 1 Mb/s for AP or 6 Mb/s for P2PGO
     */
    switch (interfacePriv->interfaceMode)
    {
        case CSR_WIFI_ROUTER_CTRL_MODE_AP:
            transmitRate = 2;
            break;
        case CSR_WIFI_ROUTER_CTRL_MODE_P2PGO:
            transmitRate = 12;
            break;
        default:
            transmitRate = 0;
    }
    unifi_frame_ma_packet_req(priv, priority, transmitRate, INVALID_HOST_TAG, interfaceTag,
                              transmissionControl, priv->netdev_client->sender_id,
                              peerAddress.a, &signal);

    /* Save host tag to check the status on reception of MA packet confirm */
    srcStaInfo->nullDataHostTag = req->HostTag;
    unifi_trace(priv, UDBG1, "uf_send_nulldata: STA AID = %d hostTag = %x\n", srcStaInfo->aid, req->HostTag);

    r = ul_send_signal_unpacked(priv, &signal, &bulkdata);

    if(r == -ENOSPC) {
        unifi_trace(priv, UDBG1, "uf_send_nulldata: ENOSPC Requeue the Null frame\n");
        enque_tx_data_pdu(priv, &bulkdata, &srcStaInfo->dataPdu[priority_q], &signal, 1);
        spin_lock_irqsave(&priv->staRecord_lock,lock_flags);
        srcStaInfo->noOfPktQueued++;
        spin_unlock_irqrestore(&priv->staRecord_lock,lock_flags);


    }
    if(r && r != -ENOSPC){
        unifi_error(priv, "uf_send_nulldata: Failed to send Null frame Error = %d\n",r);
        unifi_net_data_free(priv, &bulkdata.d[0]);
        srcStaInfo->nullDataHostTag = INVALID_HOST_TAG;
    }

    func_exit();
    return;
}

CsrBool uf_check_broadcast_bssid(unifi_priv_t *priv, const bulk_data_param_t *bulkdata)
{
    u8 *bssid = NULL;
    static const CsrWifiMacAddress broadcast_address = {{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}};
    u8 toDs, fromDs;

    toDs = (((bulkdata->d[0].os_data_ptr)[1]) & 0x01) ? 1 : 0;
    fromDs =(((bulkdata->d[0].os_data_ptr)[1]) & 0x02) ? 1 : 0;

     if (toDs && fromDs)
    {
        unifi_trace(priv, UDBG6, "Address 4 present, Don't try to find BSSID\n");
        bssid = NULL;
    }
    else if((toDs == 0) && (fromDs ==0))
    {
        /* BSSID is Address 3 */
        bssid = (u8 *) (bulkdata->d[0].os_data_ptr + 4 + (2 * ETH_ALEN));
    }
    else if(toDs)
    {
        /* BSSID is Address 1 */
        bssid = (u8 *) (bulkdata->d[0].os_data_ptr + 4);
    }
    else if(fromDs)
    {
        /* BSSID is Address 2 */
        bssid = (u8 *) (bulkdata->d[0].os_data_ptr + 4 + ETH_ALEN);
    }

    if (memcmp(broadcast_address.a, bssid, ETH_ALEN)== 0)
    {
        return TRUE;
    }
    else
    {
        return FALSE;
    }
}


CsrBool uf_process_pm_bit_for_peer(unifi_priv_t * priv, CsrWifiRouterCtrlStaInfo_t * srcStaInfo,
                                u8 pmBit,u16 interfaceTag)
{
    CsrBool moreData = FALSE;
    CsrBool powerSaveChanged = FALSE;
    unsigned long lock_flags;

    unifi_trace(priv, UDBG3, "entering uf_process_pm_bit_for_peer\n");
    if (pmBit) {
        priv->allPeerDozing |= (0x01 << (srcStaInfo->assignedHandle));
    } else {
        priv->allPeerDozing &= ~(0x01 << (srcStaInfo->assignedHandle));
    }
    if(pmBit) {
        if(srcStaInfo->currentPeerState == CSR_WIFI_ROUTER_CTRL_PEER_CONNECTED_ACTIVE) {

            /* disable the preemption */
            spin_lock_irqsave(&priv->staRecord_lock,lock_flags);
            srcStaInfo->currentPeerState =CSR_WIFI_ROUTER_CTRL_PEER_CONNECTED_POWER_SAVE;
            powerSaveChanged = TRUE;
            /* enable the preemption */
            spin_unlock_irqrestore(&priv->staRecord_lock,lock_flags);
        } else {
            return powerSaveChanged;
        }
    } else {
        if(srcStaInfo->currentPeerState == CSR_WIFI_ROUTER_CTRL_PEER_CONNECTED_POWER_SAVE) {
            /* disable the preemption */
            spin_lock_irqsave(&priv->staRecord_lock,lock_flags);
            srcStaInfo->currentPeerState = CSR_WIFI_ROUTER_CTRL_PEER_CONNECTED_ACTIVE;
            powerSaveChanged = TRUE;
            /* enable the preemption */
            spin_unlock_irqrestore(&priv->staRecord_lock,lock_flags);
        }else {
            return powerSaveChanged;
        }
    }


    if(srcStaInfo->currentPeerState == CSR_WIFI_ROUTER_CTRL_PEER_CONNECTED_ACTIVE) {
        unifi_trace(priv,UDBG3, "Peer with AID = %d is active now\n",srcStaInfo->aid);
        process_peer_active_transition(priv,srcStaInfo,interfaceTag);
    } else {
        unifi_trace(priv,UDBG3, "Peer with AID = %d is in PS Now\n",srcStaInfo->aid);
        /* Set TIM if needed */
        if(!srcStaInfo->wmmOrQosEnabled) {
            moreData = (!list_empty(&srcStaInfo->mgtFrames) ||
                        !list_empty(&srcStaInfo->dataPdu[UNIFI_TRAFFIC_Q_VO])||
                        !list_empty(&srcStaInfo->dataPdu[UNIFI_TRAFFIC_Q_CONTENTION]));
            if(moreData && (srcStaInfo->timSet == CSR_WIFI_TIM_RESET)) {
                unifi_trace(priv, UDBG3, "This condition should not occur\n");
                if (!srcStaInfo->timRequestPendingFlag){
                    update_tim(priv,srcStaInfo->aid,1,interfaceTag, srcStaInfo->assignedHandle);
                }
                else
                {
                    /* Cache the TimSet value so that it will processed immidiatly after
                     * completing the current setTim Request
                     */
                    srcStaInfo->updateTimReqQueued = 1;
                    unifi_trace(priv, UDBG6, "update_tim : One more UpdateTim Request (Tim value:%d) Queued for AID %x\n", srcStaInfo->updateTimReqQueued,
                                srcStaInfo->aid);
                }

            }
        } else {
            u8 allDeliveryEnabled = 0, dataAvailable = 0;
            unifi_trace(priv, UDBG5, "Qos in AP Mode\n");
            /* Check if all AC's are Delivery Enabled */
            is_all_ac_deliver_enabled_and_moredata(srcStaInfo, &allDeliveryEnabled, &dataAvailable);
            /*check for more data in non-delivery enabled queues*/
            moreData = (uf_is_more_data_for_non_delivery_ac(srcStaInfo) || (allDeliveryEnabled && dataAvailable));

            if(moreData && (srcStaInfo->timSet == CSR_WIFI_TIM_RESET)) {
                if (!srcStaInfo->timRequestPendingFlag){
                    update_tim(priv,srcStaInfo->aid,1,interfaceTag, srcStaInfo->assignedHandle);
                }
                else
                {
                    /* Cache the TimSet value so that it will processed immidiatly after
                     * completing the current setTim Request
                     */
                    srcStaInfo->updateTimReqQueued = 1;
                    unifi_trace(priv, UDBG6, "update_tim : One more UpdateTim Request (Tim value:%d) Queued for AID %x\n", srcStaInfo->updateTimReqQueued,
                                srcStaInfo->aid);
                }
            }
        }
    }
    unifi_trace(priv, UDBG3, "leaving uf_process_pm_bit_for_peer\n");
    return powerSaveChanged;
}



void uf_process_ps_poll(unifi_priv_t *priv,u8* sa,u8* da,u8 pmBit,u16 interfaceTag)
{
    CsrWifiRouterCtrlStaInfo_t *staRecord =
    CsrWifiRouterCtrlGetStationRecordFromPeerMacAddress(priv, sa, interfaceTag);
    tx_buffered_packets_t * buffered_pkt = NULL;
    CsrWifiMacAddress peerMacAddress;
    unsigned long lock_flags;
    s8 r =0;
    CsrBool moreData = FALSE;
    netInterface_priv_t *interfacePriv = priv->interfacePriv[interfaceTag];

    unifi_trace(priv, UDBG3, "entering uf_process_ps_poll\n");
    if(!staRecord) {
        memcpy(peerMacAddress.a,sa,ETH_ALEN);
        unifi_trace(priv, UDBG3, "In uf_process_ps_poll, sta record not found:unexpected frame addr = %x:%x:%x:%x:%x:%x\n",
                sa[0], sa[1],sa[2], sa[3], sa[4],sa[5]);
        CsrWifiRouterCtrlUnexpectedFrameIndSend(priv->CSR_WIFI_SME_IFACEQUEUE,0,interfaceTag,peerMacAddress);
        return;
    }

    uf_process_pm_bit_for_peer(priv,staRecord,pmBit,interfaceTag);

    /* Update station last activity time */
    staRecord->activity_flag = TRUE;

    /* This should not change the PM bit as PS-POLL has PM bit always set */
    if(!pmBit) {
        unifi_notice (priv," PM bit reset in PS-POLL\n");
        return;
    }

    if(IS_DTIM_ACTIVE(interfacePriv->dtimActive,interfacePriv->multicastPduHostTag)) {
        /* giving more priority to multicast packets so dropping ps-poll*/
        unifi_notice (priv," multicast transmission is going on so don't take action on PS-POLL\n");
        return;
    }

    if(!staRecord->wmmOrQosEnabled) {
        if((buffered_pkt=dequeue_tx_data_pdu(priv, &staRecord->mgtFrames))) {
            buffered_pkt->transmissionControl |= TRANSMISSION_CONTROL_TRIGGER_MASK;
            moreData = (!list_empty(&staRecord->dataPdu[UNIFI_TRAFFIC_Q_CONTENTION]) ||
                        !list_empty(&staRecord->dataPdu[UNIFI_TRAFFIC_Q_VO]) ||
                        !list_empty(&staRecord->mgtFrames));

            buffered_pkt->transmissionControl |= (TRANSMISSION_CONTROL_TRIGGER_MASK | TRANSMISSION_CONTROL_EOSP_MASK);
            if((r=frame_and_send_queued_pdu(priv,buffered_pkt,staRecord,moreData,FALSE)) == -ENOSPC) {
                /* Clear the trigger bit transmission control*/
                buffered_pkt->transmissionControl &= ~(TRANSMISSION_CONTROL_TRIGGER_MASK | TRANSMISSION_CONTROL_EOSP_MASK);
                /* Enqueue at the head of the queue */
                spin_lock_irqsave(&priv->tx_q_lock,lock_flags);
                list_add(&buffered_pkt->q, &staRecord->mgtFrames);
                spin_unlock_irqrestore(&priv->tx_q_lock,lock_flags);
                unifi_trace(priv, UDBG1, "(ENOSPC) PS-POLL received : PDU sending failed \n");
                priv->pausedStaHandle[3]=(u8)(staRecord->assignedHandle);
            } else {
                if(r){
                    unifi_trace (priv, UDBG1, " HIP validation failure : PDU sending failed \n");
                    /* the PDU failed where we can't do any thing so free the storage */
                    unifi_net_data_free(priv, &buffered_pkt->bulkdata);
                }
                kfree(buffered_pkt);
            }
        } else if((buffered_pkt=dequeue_tx_data_pdu(priv, &staRecord->dataPdu[UNIFI_TRAFFIC_Q_VO]))) {
            buffered_pkt->transmissionControl |= TRANSMISSION_CONTROL_TRIGGER_MASK;
            moreData = (!list_empty(&staRecord->dataPdu[UNIFI_TRAFFIC_Q_CONTENTION]) ||
                        !list_empty(&staRecord->dataPdu[UNIFI_TRAFFIC_Q_VO]));

            buffered_pkt->transmissionControl |= (TRANSMISSION_CONTROL_TRIGGER_MASK | TRANSMISSION_CONTROL_EOSP_MASK);
            if((r=frame_and_send_queued_pdu(priv,buffered_pkt,staRecord,moreData,FALSE)) == -ENOSPC) {
                /* Clear the trigger bit transmission control*/
                buffered_pkt->transmissionControl &= ~(TRANSMISSION_CONTROL_TRIGGER_MASK | TRANSMISSION_CONTROL_EOSP_MASK);
                /* Enqueue at the head of the queue */
                spin_lock_irqsave(&priv->tx_q_lock,lock_flags);
                list_add(&buffered_pkt->q, &staRecord->dataPdu[UNIFI_TRAFFIC_Q_VO]);
                spin_unlock_irqrestore(&priv->tx_q_lock,lock_flags);
                priv->pausedStaHandle[3]=(u8)(staRecord->assignedHandle);
                unifi_trace(priv, UDBG1, "(ENOSPC) PS-POLL received : PDU sending failed \n");
            } else {
                if(r){
                    unifi_trace (priv, UDBG1, " HIP validation failure : PDU sending failed \n");
                    /* the PDU failed where we can't do any thing so free the storage */
                    unifi_net_data_free(priv, &buffered_pkt->bulkdata);
                }
                kfree(buffered_pkt);
            }
        } else  if((buffered_pkt=dequeue_tx_data_pdu(priv, &staRecord->dataPdu[UNIFI_TRAFFIC_Q_CONTENTION]))) {
            buffered_pkt->transmissionControl |= TRANSMISSION_CONTROL_TRIGGER_MASK;
            moreData = !list_empty(&staRecord->dataPdu[UNIFI_TRAFFIC_Q_CONTENTION]);

            buffered_pkt->transmissionControl |= (TRANSMISSION_CONTROL_TRIGGER_MASK | TRANSMISSION_CONTROL_EOSP_MASK);
            if((r=frame_and_send_queued_pdu(priv,buffered_pkt,staRecord,moreData,FALSE)) == -ENOSPC) {
                /* Clear the trigger bit transmission control*/
                buffered_pkt->transmissionControl &= ~(TRANSMISSION_CONTROL_TRIGGER_MASK | TRANSMISSION_CONTROL_EOSP_MASK);
                /* Enqueue at the head of the queue */
                spin_lock_irqsave(&priv->tx_q_lock,lock_flags);
                list_add(&buffered_pkt->q, &staRecord->dataPdu[UNIFI_TRAFFIC_Q_CONTENTION]);
                spin_unlock_irqrestore(&priv->tx_q_lock,lock_flags);
                priv->pausedStaHandle[0]=(u8)(staRecord->assignedHandle);
                unifi_trace(priv, UDBG1, "(ENOSPC) PS-POLL received : PDU sending failed \n");
            } else {
                if(r){
                    unifi_trace (priv, UDBG1, " HIP validation failure : PDU sending failed \n");
                    /* the PDU failed where we can't do any thing so free the storage */
                    unifi_net_data_free(priv, &buffered_pkt->bulkdata);
                }
                kfree(buffered_pkt);
            }
        } else {
         /* Actually since we have sent an ACK, there
         * there is no need to send a NULL frame*/
        }
        moreData = (!list_empty(&staRecord->dataPdu[UNIFI_TRAFFIC_Q_VO]) ||
           !list_empty(&staRecord->dataPdu[UNIFI_TRAFFIC_Q_CONTENTION]) ||
            !list_empty(&staRecord->mgtFrames));
        if(!moreData && (staRecord->timSet == CSR_WIFI_TIM_SET)) {
            unifi_trace(priv, UDBG3, "more data = NULL, set tim to 0 in uf_process_ps_poll\n");
            if (!staRecord->timRequestPendingFlag){
                update_tim(priv,staRecord->aid,0,interfaceTag, staRecord->assignedHandle);
            }
            else
            {
                /* Cache the TimSet value so that it will processed immidiatly after
                 * completing the current setTim Request
                 */
                staRecord->updateTimReqQueued = 0;
                unifi_trace(priv, UDBG6, "update_tim : One more UpdateTim Request (Tim value:%d) Queued for AID %x\n", staRecord->updateTimReqQueued,
                            staRecord->aid);
            }
        }
    } else {

        u8 allDeliveryEnabled = 0, dataAvailable = 0;
        unifi_trace(priv, UDBG3,"Qos Support station.Processing PS-Poll\n");

        /*Send Data From Management Frames*/
        /* Priority orders for delivering the buffered packets are
         * 1. Deliver the Management frames if there
         * 2. Other access catagory frames which are non deliver enable including UNIFI_TRAFFIC_Q_VO
         * priority is from VO->BK
         */

        /* Check if all AC's are Delivery Enabled */
        is_all_ac_deliver_enabled_and_moredata(staRecord, &allDeliveryEnabled, &dataAvailable);

        if (allDeliveryEnabled) {
            unifi_trace(priv, UDBG3, "uf_process_ps_poll: All ACs are delivery enable so Sending QOS Null in response of Ps-poll\n");
            uf_send_qos_null(priv,interfaceTag,sa,CSR_QOS_UP0,staRecord);
            return;
        }

        if (!list_empty(&staRecord->mgtFrames)) {
             if ((buffered_pkt=dequeue_tx_data_pdu(priv, &staRecord->mgtFrames))) {
                    /* We dont have packets in non delivery enabled UNIFI_TRAFFIC_Q_VO, So we are looking in management
                     * queue of the station record
                     */
                    moreData = uf_is_more_data_for_non_delivery_ac(staRecord);
                    buffered_pkt->transmissionControl |= (TRANSMISSION_CONTROL_TRIGGER_MASK | TRANSMISSION_CONTROL_EOSP_MASK);

                    /* Last parameter is EOSP & its false always for PS-POLL processing */
                    if((r=frame_and_send_queued_pdu(priv,buffered_pkt,staRecord,moreData,FALSE)) == -ENOSPC) {
                        /* Clear the trigger bit transmission control*/
                        buffered_pkt->transmissionControl &= ~(TRANSMISSION_CONTROL_TRIGGER_MASK | TRANSMISSION_CONTROL_EOSP_MASK);
                        /* Enqueue at the head of the queue */
                        spin_lock_irqsave(&priv->tx_q_lock,lock_flags);
                        list_add(&buffered_pkt->q, &staRecord->mgtFrames);
                        spin_unlock_irqrestore(&priv->tx_q_lock,lock_flags);
                        priv->pausedStaHandle[0]=(u8)(staRecord->assignedHandle);
                        unifi_trace(priv, UDBG1, "(ENOSPC) PS-POLL received : PDU sending failed \n");
                    } else {
                        if(r){
                            unifi_trace (priv, UDBG1, " HIP validation failure : PDU sending failed \n");
                            /* the PDU failed where we can't do any thing so free the storage */
                            unifi_net_data_free(priv, &buffered_pkt->bulkdata);
                        }
                        kfree(buffered_pkt);
                    }
                } else {
                    unifi_error(priv, "uf_process_ps_poll: Mgt frame list empty!! \n");
                }

        } else {
            s8 i;
            /* We dont have buffered packet in mangement frame queue (1 failed), So proceed with condition 2
             * UNIFI_TRAFFIC_Q_VO -> VI -> BE -> BK
             */
            for(i= 3; i>=0; i--) {
                if (!IS_DELIVERY_ENABLED(staRecord->powersaveMode[i])) {
                    /* Send One packet, if queue is NULL then continue */
                    if((buffered_pkt=dequeue_tx_data_pdu(priv, &staRecord->dataPdu[i]))) {
                        moreData = uf_is_more_data_for_non_delivery_ac(staRecord);

                        buffered_pkt->transmissionControl |= (TRANSMISSION_CONTROL_TRIGGER_MASK | TRANSMISSION_CONTROL_EOSP_MASK);

                        /* Last parameter is EOSP & its false always for PS-POLL processing */
                        if((r=frame_and_send_queued_pdu(priv,buffered_pkt,staRecord,moreData,FALSE)) == -ENOSPC) {
                            /* Clear the trigger bit transmission control*/
                            buffered_pkt->transmissionControl &= ~(TRANSMISSION_CONTROL_TRIGGER_MASK | TRANSMISSION_CONTROL_EOSP_MASK);
                            /* Enqueue at the head of the queue */
                            spin_lock_irqsave(&priv->tx_q_lock,lock_flags);
                            list_add(&buffered_pkt->q, &staRecord->dataPdu[i]);
                            spin_unlock_irqrestore(&priv->tx_q_lock,lock_flags);
                            priv->pausedStaHandle[0]=(u8)(staRecord->assignedHandle);
                            unifi_trace(priv, UDBG1, "(ENOSPC) PS-POLL received : PDU sending failed \n");
                        } else {
                            if(r) {
                                unifi_trace (priv, UDBG1, " HIP validation failure : PDU sending failed \n");
                                /* the PDU failed where we can't do any thing so free the storage */
                                unifi_net_data_free(priv, &buffered_pkt->bulkdata);
                            }
                            kfree(buffered_pkt);
                        }
                        break;
                    }
                }
            }
        }
        /* Check if all AC's are Delivery Enabled */
        is_all_ac_deliver_enabled_and_moredata(staRecord, &allDeliveryEnabled, &dataAvailable);
        /*check for more data in non-delivery enabled queues*/
        moreData = (uf_is_more_data_for_non_delivery_ac(staRecord) || (allDeliveryEnabled && dataAvailable));
        if(!moreData && (staRecord->timSet == CSR_WIFI_TIM_SET)) {
            unifi_trace(priv, UDBG3, "more data = NULL, set tim to 0 in uf_process_ps_poll\n");
            if (!staRecord->timRequestPendingFlag){
                update_tim(priv,staRecord->aid,0,interfaceTag, staRecord->assignedHandle);
            }
            else
            {
                /* Cache the TimSet value so that it will processed immidiatly after
                 * completing the current setTim Request
                 */
                staRecord->updateTimReqQueued = 0;
                unifi_trace(priv, UDBG6, "update_tim : One more UpdateTim Request (Tim value:%d) Queued for AID %x\n", staRecord->updateTimReqQueued,
                            staRecord->aid);
            }

        }
    }

        unifi_trace(priv, UDBG3, "leaving uf_process_ps_poll\n");
}



void add_to_send_cfm_list(unifi_priv_t * priv,
                          tx_buffered_packets_t *tx_q_item,
                          struct list_head *frames_need_cfm_list)
{
    tx_buffered_packets_t *send_cfm_list_item = NULL;

    send_cfm_list_item = (tx_buffered_packets_t *) kmalloc(sizeof(tx_buffered_packets_t), GFP_ATOMIC);

    if(send_cfm_list_item == NULL){
        unifi_warning(priv, "%s: Failed to allocate memory for new list item \n");
        return;
    }

    INIT_LIST_HEAD(&send_cfm_list_item->q);

    send_cfm_list_item->hostTag = tx_q_item->hostTag;
    send_cfm_list_item->interfaceTag = tx_q_item->interfaceTag;
    send_cfm_list_item->transmissionControl = tx_q_item->transmissionControl;
    send_cfm_list_item->leSenderProcessId = tx_q_item->leSenderProcessId;
    send_cfm_list_item->rate = tx_q_item->rate;
    memcpy(send_cfm_list_item->peerMacAddress.a, tx_q_item->peerMacAddress.a, ETH_ALEN);
    send_cfm_list_item->priority = tx_q_item->priority;

    list_add_tail(&send_cfm_list_item->q, frames_need_cfm_list);
}

void uf_prepare_send_cfm_list_for_queued_pkts(unifi_priv_t * priv,
                                                 struct list_head *frames_need_cfm_list,
                                                 struct list_head * list)
{
    tx_buffered_packets_t *tx_q_item = NULL;
    struct list_head *listHead;
    struct list_head *placeHolder;
    unsigned long lock_flags;

    func_enter();

    spin_lock_irqsave(&priv->tx_q_lock,lock_flags);

    /* Search through the list and if confirmation required for any frames,
    add it to the send_cfm list */
    list_for_each_safe(listHead, placeHolder, list) {
        tx_q_item = list_entry(listHead, tx_buffered_packets_t, q);

        if(!tx_q_item) {
            unifi_error(priv, "Entry should exist, otherwise it is a (BUG)\n");
            continue;
        }

        /* check if confirmation is requested and if the sender ID
        is not netdevice client then save the entry in the list for need cfms */
        if (!(tx_q_item->transmissionControl & CSR_NO_CONFIRM_REQUIRED) &&
            (tx_q_item->leSenderProcessId != priv->netdev_client->sender_id)){
             unifi_trace(priv, UDBG1, "%s: SenderProcessID=%x host tag=%x transmission control=%x\n",
                __FUNCTION__,
                tx_q_item->leSenderProcessId,
                tx_q_item->hostTag,
                tx_q_item->transmissionControl);

             add_to_send_cfm_list(priv, tx_q_item, frames_need_cfm_list);
        }
    }

    spin_unlock_irqrestore(&priv->tx_q_lock,lock_flags);

    func_exit();
}



void uf_flush_list(unifi_priv_t * priv, struct list_head * list)
{
    tx_buffered_packets_t *tx_q_item;
    struct list_head *listHead;
    struct list_head *placeHolder;
    unsigned long lock_flags;

    unifi_trace(priv, UDBG5, "entering the uf_flush_list \n");

    spin_lock_irqsave(&priv->tx_q_lock,lock_flags);
    /* go through list, delete & free memory */
    list_for_each_safe(listHead, placeHolder, list) {
        tx_q_item = list_entry(listHead, tx_buffered_packets_t, q);

        if(!tx_q_item) {
            unifi_error(priv, "entry should exists, otherwise crashes (bug)\n");
        }
        unifi_trace(priv, UDBG5,
                "proccess_tx:  in uf_flush_list peerMacAddress=%02X%02X%02X%02X%02X%02X senderProcessId=%x\n",
                tx_q_item->peerMacAddress.a[0], tx_q_item->peerMacAddress.a[1],
                tx_q_item->peerMacAddress.a[2], tx_q_item->peerMacAddress.a[3],
                tx_q_item->peerMacAddress.a[4], tx_q_item->peerMacAddress.a[5],
                tx_q_item->leSenderProcessId);

        list_del(listHead);
        /* free the allocated memory */
        unifi_net_data_free(priv, &tx_q_item->bulkdata);
        kfree(tx_q_item);
        tx_q_item = NULL;
        if (!priv->noOfPktQueuedInDriver) {
            unifi_error(priv, "packets queued in driver 0 still decrementing in %s\n", __FUNCTION__);
        } else {
            priv->noOfPktQueuedInDriver--;
        }
    }
    spin_unlock_irqrestore(&priv->tx_q_lock,lock_flags);
}

tx_buffered_packets_t *dequeue_tx_data_pdu(unifi_priv_t *priv, struct list_head *txList)
{
    /* dequeue the tx data packets from the appropriate queue */
    tx_buffered_packets_t *tx_q_item = NULL;
    struct list_head *listHead;
    struct list_head *placeHolder;
    unsigned long lock_flags;

    unifi_trace(priv, UDBG5, "entering dequeue_tx_data_pdu\n");
    /* check for list empty */
    if (list_empty(txList)) {
        unifi_trace(priv, UDBG5, "In dequeue_tx_data_pdu, the list is empty\n");
        return NULL;
    }

    /* Verification, if packet count is negetive */
    if (priv->noOfPktQueuedInDriver == 0xFFFF) {
        unifi_warning(priv, "no packet available in queue: debug");
        return NULL;
    }

    /* return first node after header, & delete from the list  && atleast one item exist */
    spin_lock_irqsave(&priv->tx_q_lock,lock_flags);
    list_for_each_safe(listHead, placeHolder, txList) {
        tx_q_item = list_entry(listHead, tx_buffered_packets_t, q);
        list_del(listHead);
        break;
    }
    spin_unlock_irqrestore(&priv->tx_q_lock,lock_flags);

    if (tx_q_item) {
        unifi_trace(priv, UDBG5,
                "proccess_tx:  In dequeue_tx_data_pdu peerMacAddress=%02X%02X%02X%02X%02X%02X senderProcessId=%x\n",
                tx_q_item->peerMacAddress.a[0], tx_q_item->peerMacAddress.a[1],
                tx_q_item->peerMacAddress.a[2], tx_q_item->peerMacAddress.a[3],
                tx_q_item->peerMacAddress.a[4], tx_q_item->peerMacAddress.a[5],
                tx_q_item->leSenderProcessId);
    }

    unifi_trace(priv, UDBG5, "leaving dequeue_tx_data_pdu\n");
    return tx_q_item;
}
/* generic function to get the station record handler */
CsrWifiRouterCtrlStaInfo_t *CsrWifiRouterCtrlGetStationRecordFromPeerMacAddress(unifi_priv_t *priv,
        const u8 *peerMacAddress,
        u16 interfaceTag)
{
    u8 i;
    netInterface_priv_t *interfacePriv;
    unsigned long lock_flags;

    if (interfaceTag >= CSR_WIFI_NUM_INTERFACES) {
        unifi_error(priv, "interfaceTag is not proper, interfaceTag = %d\n", interfaceTag);
        return NULL;
    }

    interfacePriv = priv->interfacePriv[interfaceTag];

    /* disable the preemption untill station record is fetched */
    spin_lock_irqsave(&priv->staRecord_lock,lock_flags);

    for (i = 0; i < UNIFI_MAX_CONNECTIONS; i++) {
        if (interfacePriv->staInfo[i]!= NULL) {
            if (!memcmp(((CsrWifiRouterCtrlStaInfo_t *) (interfacePriv->staInfo[i]))->peerMacAddress.a, peerMacAddress, ETH_ALEN)) {
                /* enable the preemption as station record is fetched */
                spin_unlock_irqrestore(&priv->staRecord_lock,lock_flags);
                unifi_trace(priv, UDBG5, "peer entry found in station record\n");
                return ((CsrWifiRouterCtrlStaInfo_t *) (interfacePriv->staInfo[i]));
            }
        }
    }
    /* enable the preemption as station record is fetched */
    spin_unlock_irqrestore(&priv->staRecord_lock,lock_flags);
    unifi_trace(priv, UDBG5, "peer entry not found in station record\n");
    return NULL;
}
/* generic function to get the station record handler from the handle */
CsrWifiRouterCtrlStaInfo_t * CsrWifiRouterCtrlGetStationRecordFromHandle(unifi_priv_t *priv,
                                                                 u32 handle,
                                                                 u16 interfaceTag)
{
    netInterface_priv_t *interfacePriv;

    if ((handle >= UNIFI_MAX_CONNECTIONS) || (interfaceTag >= CSR_WIFI_NUM_INTERFACES)) {
        unifi_error(priv, "handle/interfaceTag is not proper, handle = %d, interfaceTag = %d\n", handle, interfaceTag);
        return NULL;
    }
    interfacePriv = priv->interfacePriv[interfaceTag];
    return ((CsrWifiRouterCtrlStaInfo_t *) (interfacePriv->staInfo[handle]));
}

/* Function to do inactivity */
void uf_check_inactivity(unifi_priv_t *priv, u16 interfaceTag, CsrTime currentTime)
{
    u32 i;
    CsrWifiRouterCtrlStaInfo_t *staInfo;
    CsrTime elapsedTime;    /* Time in microseconds */
    netInterface_priv_t *interfacePriv = priv->interfacePriv[interfaceTag];
    CsrWifiMacAddress peerMacAddress;
    unsigned long lock_flags;

    if (interfacePriv == NULL) {
        unifi_trace(priv, UDBG3, "uf_check_inactivity: Interface priv is NULL \n");
        return;
    }

    spin_lock_irqsave(&priv->staRecord_lock,lock_flags);
    /* Go through the list of stations to check for inactivity */
    for(i = 0; i < UNIFI_MAX_CONNECTIONS; i++) {
        staInfo =  CsrWifiRouterCtrlGetStationRecordFromHandle(priv, i, interfaceTag);
        if(!staInfo ) {
            continue;
        }

        unifi_trace(priv, UDBG3, "Running Inactivity handler Time %xus station's last activity %xus\n",
                currentTime, staInfo->lastActivity);


        elapsedTime = (currentTime >= staInfo->lastActivity)?
                (currentTime - staInfo->lastActivity):
                (~((u32)0) - staInfo->lastActivity + currentTime);
        spin_unlock_irqrestore(&priv->staRecord_lock,lock_flags);

        if (elapsedTime > MAX_INACTIVITY_INTERVAL) {
            memcpy((u8*)&peerMacAddress, (u8*)&staInfo->peerMacAddress, sizeof(CsrWifiMacAddress));

            /* Indicate inactivity for the station */
            unifi_trace(priv, UDBG3, "Station %x:%x:%x:%x:%x:%x inactive since %xus\n sending Inactive Ind\n",
                        peerMacAddress.a[0], peerMacAddress.a[1],
                        peerMacAddress.a[2], peerMacAddress.a[3],
                        peerMacAddress.a[4], peerMacAddress.a[5],
                        elapsedTime);

            CsrWifiRouterCtrlStaInactiveIndSend(priv->CSR_WIFI_SME_IFACEQUEUE, 0, interfaceTag, peerMacAddress);
        }
    }

    interfacePriv->last_inactivity_check = currentTime;
}

/* Function to update activity of a station */
void uf_update_sta_activity(unifi_priv_t *priv, u16 interfaceTag, const u8 *peerMacAddress)
{
    CsrTime elapsedTime, currentTime;    /* Time in microseconds */
    CsrTime timeHi;         /* Not used - Time in microseconds */
    CsrWifiRouterCtrlStaInfo_t *staInfo;
    netInterface_priv_t *interfacePriv = priv->interfacePriv[interfaceTag];
    unsigned long lock_flags;

    if (interfacePriv == NULL) {
        unifi_trace(priv, UDBG3, "uf_check_inactivity: Interface priv is NULL \n");
        return;
    }

    currentTime = CsrTimeGet(&timeHi);


    staInfo = CsrWifiRouterCtrlGetStationRecordFromPeerMacAddress(priv, peerMacAddress, interfaceTag);

    if (staInfo == NULL) {
        unifi_trace(priv, UDBG4, "Sta does not exist yet");
        return;
    }

    spin_lock_irqsave(&priv->staRecord_lock,lock_flags);
    /* Update activity */
    staInfo->lastActivity = currentTime;

    /* See if inactivity handler needs to be run
     * Here it is theoretically possible that the counter may have wrapped around. But
     * since we just want to know when to run the inactivity handler it does not really matter.
     * Especially since this is data path it makes sense in keeping it simple and avoiding
     * 64 bit handling */
    elapsedTime = (currentTime >= interfacePriv->last_inactivity_check)?
                    (currentTime - interfacePriv->last_inactivity_check):
                    (~((u32)0) - interfacePriv->last_inactivity_check + currentTime);

    spin_unlock_irqrestore(&priv->staRecord_lock,lock_flags);

    /* Check if it is time to run the inactivity handler */
    if (elapsedTime > INACTIVITY_CHECK_INTERVAL) {
        uf_check_inactivity(priv, interfaceTag, currentTime);
    }
}
void resume_unicast_buffered_frames(unifi_priv_t *priv, u16 interfaceTag)
{

   netInterface_priv_t *interfacePriv = priv->interfacePriv[interfaceTag];
   u8 i;
   int j;
   tx_buffered_packets_t * buffered_pkt = NULL;
   CsrBool hipslotFree[4] = {TRUE,TRUE,TRUE,TRUE};
   int r;
   unsigned long lock_flags;

   func_enter();
   while(!isRouterBufferEnabled(priv,3) &&
                            ((buffered_pkt=dequeue_tx_data_pdu(priv,&interfacePriv->genericMgtFrames))!=NULL)) {
        buffered_pkt->transmissionControl &=
                     ~(TRANSMISSION_CONTROL_TRIGGER_MASK|TRANSMISSION_CONTROL_EOSP_MASK);
        if((r=frame_and_send_queued_pdu(priv,buffered_pkt,NULL,0,FALSE)) == -ENOSPC) {
            /* Enqueue at the head of the queue */
            spin_lock_irqsave(&priv->tx_q_lock,lock_flags);
            list_add(&buffered_pkt->q, &interfacePriv->genericMgtFrames);
            spin_unlock_irqrestore(&priv->tx_q_lock,lock_flags);
            hipslotFree[3]=FALSE;
            break;
        }else {
            if(r){
                unifi_trace (priv, UDBG1, " HIP validation failure : PDU sending failed \n");
                /* the PDU failed where we can't do any thing so free the storage */
                unifi_net_data_free(priv, &buffered_pkt->bulkdata);
            }
            kfree(buffered_pkt);
        }
   }
   for(i = 0; i < UNIFI_MAX_CONNECTIONS; i++) {
        CsrWifiRouterCtrlStaInfo_t *staInfo = interfacePriv->staInfo[i];
        if(!hipslotFree[0] && !hipslotFree[1] && !hipslotFree[2] && !hipslotFree[3]) {
            unifi_trace(priv, UDBG3, "(ENOSPC) in resume_unicast_buffered_frames:: hip slots are full \n");
            break;
        }
        if (staInfo && (staInfo->currentPeerState == CSR_WIFI_ROUTER_CTRL_PEER_CONNECTED_ACTIVE)) {
          while((( TRUE == hipslotFree[3] ) && (buffered_pkt=dequeue_tx_data_pdu(priv, &staInfo->mgtFrames)))) {
              buffered_pkt->transmissionControl &=
                           ~(TRANSMISSION_CONTROL_TRIGGER_MASK|TRANSMISSION_CONTROL_EOSP_MASK);
              if((r=frame_and_send_queued_pdu(priv,buffered_pkt,staInfo,0,FALSE)) == -ENOSPC) {
                  unifi_trace(priv, UDBG3, "(ENOSPC) in resume_unicast_buffered_frames:: hip slots are full for voice queue\n");
                  /* Enqueue at the head of the queue */
                  spin_lock_irqsave(&priv->tx_q_lock,lock_flags);
                  list_add(&buffered_pkt->q, &staInfo->mgtFrames);
                  spin_unlock_irqrestore(&priv->tx_q_lock,lock_flags);
                  priv->pausedStaHandle[3]=(u8)(staInfo->assignedHandle);
                  hipslotFree[3] = FALSE;
                  break;
              } else {
                  if(r){
                      unifi_trace (priv, UDBG1, " HIP validation failure : PDU sending failed \n");
                      /* the PDU failed where we can't do any thing so free the storage */
                      unifi_net_data_free(priv, &buffered_pkt->bulkdata);
                  }
                  kfree(buffered_pkt);
              }
          }

          for(j=3;j>=0;j--) {
              if(!hipslotFree[j])
                  continue;

              while((buffered_pkt=dequeue_tx_data_pdu(priv, &staInfo->dataPdu[j]))) {
                 buffered_pkt->transmissionControl &=
                            ~(TRANSMISSION_CONTROL_TRIGGER_MASK|TRANSMISSION_CONTROL_EOSP_MASK);
                 if((r=frame_and_send_queued_pdu(priv,buffered_pkt,staInfo,0,FALSE)) == -ENOSPC) {
                     /* Enqueue at the head of the queue */
                     spin_lock_irqsave(&priv->tx_q_lock,lock_flags);
                     list_add(&buffered_pkt->q, &staInfo->dataPdu[j]);
                     spin_unlock_irqrestore(&priv->tx_q_lock,lock_flags);
                     priv->pausedStaHandle[j]=(u8)(staInfo->assignedHandle);
                     hipslotFree[j]=FALSE;
                     break;
                 } else {
                    if(r){
                        unifi_trace (priv, UDBG1, " HIP validation failure : PDU sending failed \n");
                        /* the PDU failed where we can't do any thing so free the storage */
                        unifi_net_data_free(priv, &buffered_pkt->bulkdata);
                     }
                    kfree(buffered_pkt);
                 }
              }
          }
       }
    }
    func_exit();
}
void update_eosp_to_head_of_broadcast_list_head(unifi_priv_t *priv,u16 interfaceTag)
{

    netInterface_priv_t *interfacePriv = priv->interfacePriv[interfaceTag];
    unsigned long lock_flags;
    struct list_head *listHead;
    struct list_head *placeHolder;
    tx_buffered_packets_t *tx_q_item;

    func_enter();
    if (interfacePriv->noOfbroadcastPktQueued) {

        /* Update the EOSP to the HEAD of b/c list
         * beacuse we have received any mgmt packet so it should not hold for long time
         * peer may time out.
         */
        spin_lock_irqsave(&priv->tx_q_lock,lock_flags);
        list_for_each_safe(listHead, placeHolder, &interfacePriv->genericMulticastOrBroadCastFrames) {
            tx_q_item = list_entry(listHead, tx_buffered_packets_t, q);
            tx_q_item->transmissionControl |= TRANSMISSION_CONTROL_EOSP_MASK;
            tx_q_item->transmissionControl = (tx_q_item->transmissionControl & ~(CSR_NO_CONFIRM_REQUIRED));
            unifi_trace(priv, UDBG1,"updating eosp for list Head hostTag:= 0x%x ",tx_q_item->hostTag);
            break;
        }
        spin_unlock_irqrestore(&priv->tx_q_lock,lock_flags);
    }
    func_exit();
}

/*
 * ---------------------------------------------------------------------------
 *  resume_suspended_uapsd
 *
 *      This function takes care processing packets of Unscheduled Service Period,
 *      which been suspended earlier due to DTIM/HIP ENOSPC scenarios
 *
 *  Arguments:
 *      priv            Pointer to device private context struct
 *      interfaceTag    For which resume should happen
 * ---------------------------------------------------------------------------
 */
void resume_suspended_uapsd(unifi_priv_t* priv,u16 interfaceTag)
{

   u8 startIndex;
   CsrWifiRouterCtrlStaInfo_t * staInfo = NULL;
    unsigned long lock_flags;

    unifi_trace(priv, UDBG2, "++resume_suspended_uapsd: \n");
    for(startIndex= 0; startIndex < UNIFI_MAX_CONNECTIONS;startIndex++) {
        staInfo =  CsrWifiRouterCtrlGetStationRecordFromHandle(priv,startIndex,interfaceTag);

        if(!staInfo || !staInfo->wmmOrQosEnabled) {
            continue;
        } else if((staInfo->currentPeerState == CSR_WIFI_ROUTER_CTRL_PEER_CONNECTED_POWER_SAVE)
                   &&staInfo->uapsdActive && staInfo->uspSuspend) {
            /* U-APSD Still active & previously suspended either ENOSPC of FH queues OR
             * due to DTIM activity
             */
            uf_handle_uspframes_delivery(priv, staInfo, interfaceTag);
        } else {
            unifi_trace(priv, UDBG2, "resume_suspended_uapsd: PS state=%x, uapsdActive?=%x, suspend?=%x\n",
                        staInfo->currentPeerState, staInfo->uapsdActive, staInfo->uspSuspend);
            if (staInfo->currentPeerState == CSR_WIFI_ROUTER_CTRL_PEER_CONNECTED_ACTIVE)
            {
                spin_lock_irqsave(&priv->staRecord_lock,lock_flags);
                staInfo->uapsdActive = FALSE;
                staInfo->uspSuspend = FALSE;
                spin_unlock_irqrestore(&priv->staRecord_lock,lock_flags);
            }
        }
    }
    unifi_trace(priv, UDBG2, "--resume_suspended_uapsd:\n");
}

#endif
