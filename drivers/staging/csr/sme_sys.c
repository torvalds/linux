/*
 * ---------------------------------------------------------------------------
 * FILE:     sme_sys.c
 *
 * PURPOSE:
 *      Driver specific implementation of the SME SYS SAP.
 *      It is part of the porting exercise.
 *
 * Copyright (C) 2008-2011 by Cambridge Silicon Radio Ltd.
 *
 * Refer to LICENSE.txt included with this source code for details on
 * the license terms.
 *
 * ---------------------------------------------------------------------------
 */

#include <linux/version.h>
#include "csr_wifi_hip_unifiversion.h"
#include "unifi_priv.h"
#include "csr_wifi_hip_conversions.h"
#ifdef CSR_SUPPORT_WEXT_AP
#include "csr_wifi_sme_sef.h"
#endif

/*
 * This file implements the SME SYS API and contains the following functions:
 * CsrWifiRouterCtrlMediaStatusReqHandler()
 * CsrWifiRouterCtrlHipReqHandler()
 * CsrWifiRouterCtrlPortConfigureReqHandler()
 * CsrWifiRouterCtrlWifiOnReqHandler()
 * CsrWifiRouterCtrlWifiOffReqHandler()
 * CsrWifiRouterCtrlSuspendResHandler()
 * CsrWifiRouterCtrlResumeResHandler()
 * CsrWifiRouterCtrlQosControlReqHandler()
 * CsrWifiRouterCtrlConfigurePowerModeReqHandler()
 * CsrWifiRouterCtrlWifiOnResHandler()
 * CsrWifiRouterCtrlWifiOffRspHandler()
 * CsrWifiRouterCtrlMulticastAddressResHandler()
 * CsrWifiRouterCtrlTrafficConfigReqHandler()
 * CsrWifiRouterCtrlTrafficClassificationReqHandler()
 * CsrWifiRouterCtrlTclasAddReqHandler()
 * CsrWifiRouterCtrlTclasDelReqHandler()
 * CsrWifiRouterCtrlSetModeReqHandler()
 * CsrWifiRouterCtrlWapiMulticastFilterReqHandler()
 * CsrWifiRouterCtrlWapiUnicastFilterReqHandler()
 * CsrWifiRouterCtrlWapiUnicastTxPktReqHandler()
 * CsrWifiRouterCtrlWapiRxPktReqHandler()
 * CsrWifiRouterCtrlWapiFilterReqHandler()
 */

#ifdef CSR_SUPPORT_SME
static void check_inactivity_timer_expire_func(unsigned long data);
void uf_send_disconnected_ind_wq(struct work_struct *work);
#endif

void send_auto_ma_packet_confirm(unifi_priv_t *priv,
                                 netInterface_priv_t *interfacePriv,
                                 struct list_head *buffered_frames_list)
{
    tx_buffered_packets_t *buffered_frame_item = NULL;
    struct list_head *listHead;
    struct list_head *placeHolder;
    int client_id;

    CSR_SIGNAL unpacked_signal;
    u8 sigbuf[UNIFI_PACKED_SIGBUF_SIZE];
    u16 packed_siglen;


    list_for_each_safe(listHead, placeHolder, buffered_frames_list)
    {
        buffered_frame_item = list_entry(listHead, tx_buffered_packets_t, q);

        if(!buffered_frame_item) {
            unifi_error(priv, "Entry should exist, otherwise it is a (BUG)\n");
            continue;
        }

        if ((interfacePriv->interfaceMode != CSR_WIFI_ROUTER_CTRL_MODE_NONE) &&
            (priv->wifi_on_state == wifi_on_done))
        {

            unifi_warning(priv, "Send MA_PACKET_CONFIRM to SenderProcessId = %x for (HostTag = %x TransmissionControl = %x)\n",
                                 (buffered_frame_item->leSenderProcessId),
                                 buffered_frame_item->hostTag,
                                 buffered_frame_item->transmissionControl);

            client_id = buffered_frame_item->leSenderProcessId & 0xFF00;

            if (client_id == priv->sme_cli->sender_id)
            {
                /* construct a MA-PACKET.confirm message for SME */
                memset(&unpacked_signal, 0, sizeof(unpacked_signal));
                unpacked_signal.SignalPrimitiveHeader.SignalId = CSR_MA_PACKET_CONFIRM_ID;
                unpacked_signal.SignalPrimitiveHeader.ReceiverProcessId = buffered_frame_item->leSenderProcessId;
                unpacked_signal.SignalPrimitiveHeader.SenderProcessId = CSR_WIFI_ROUTER_IFACEQUEUE;

                unpacked_signal.u.MaPacketConfirm.VirtualInterfaceIdentifier = uf_get_vif_identifier(interfacePriv->interfaceMode,
                                                                                                     interfacePriv->InterfaceTag);
                unpacked_signal.u.MaPacketConfirm.TransmissionStatus = CSR_RESULT_FAILURE;
                unpacked_signal.u.MaPacketConfirm.RetryCount = 0;
                unpacked_signal.u.MaPacketConfirm.Rate = buffered_frame_item->rate;
                unpacked_signal.u.MaPacketConfirm.HostTag = buffered_frame_item->hostTag;

                write_pack(&unpacked_signal, sigbuf, &packed_siglen);
                unifi_warning(priv, "MA_PACKET_CONFIRM for SME (0x%x, 0x%x, 0x%x, 0x%x)\n",
                                         unpacked_signal.SignalPrimitiveHeader.ReceiverProcessId,
                                         unpacked_signal.SignalPrimitiveHeader.SenderProcessId,
                                         unpacked_signal.u.MaPacketConfirm.VirtualInterfaceIdentifier,
                                         unpacked_signal.u.MaPacketConfirm.HostTag);

                CsrWifiRouterCtrlHipIndSend(priv->CSR_WIFI_SME_IFACEQUEUE,
                                            packed_siglen,
                                            (u8 *)sigbuf,
                                            0, NULL,
                                            0, NULL);
            }
            else if((buffered_frame_item->hostTag & 0x80000000))
            {
                /* construct a MA-PACKET.confirm message for NME */
                unifi_warning(priv, "MA_PACKET_CONFIRM for NME (0x%x, 0x%x, 0x%x, 0x%x)\n",
                                    buffered_frame_item->leSenderProcessId,
                                    buffered_frame_item->interfaceTag,
                                    buffered_frame_item->transmissionControl,
                                    (buffered_frame_item->hostTag & 0x3FFFFFFF));

                CsrWifiRouterMaPacketCfmSend((buffered_frame_item->leSenderProcessId & 0xFF),
                                            buffered_frame_item->interfaceTag,
                                            CSR_RESULT_FAILURE,
                                            (buffered_frame_item->hostTag & 0x3FFFFFFF),
                                            buffered_frame_item->rate);

            }
            else
            {
                unifi_warning(priv, "Buffered packet dropped without sending a confirm\n");
            }

        }

        list_del(listHead);
        kfree(buffered_frame_item);
        buffered_frame_item = NULL;
    }
}

void CsrWifiRouterCtrlMediaStatusReqHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
    unifi_priv_t *priv = (unifi_priv_t*)drvpriv;
    CsrWifiRouterCtrlMediaStatusReq* req = (CsrWifiRouterCtrlMediaStatusReq*)msg;
    netInterface_priv_t *interfacePriv = priv->interfacePriv[req->interfaceTag];
    unsigned long flags;

    if (priv->smepriv == NULL) {
        unifi_error(priv, "CsrWifiRouterCtrlMediaStatusReqHandler: invalid smepriv\n");
        return;
    }
    if (req->interfaceTag >= CSR_WIFI_NUM_INTERFACES) {
        unifi_error(priv, "CsrWifiRouterCtrlMediaStatusReqHandler: invalid interfaceTag\n");
        return;
    }
    unifi_trace(priv, UDBG3, "CsrWifiRouterCtrlMediaStatusReqHandler: Mode = %d req->mediaStatus = %d\n",interfacePriv->interfaceMode,req->mediaStatus);
    if (interfacePriv->interfaceMode != CSR_WIFI_ROUTER_CTRL_MODE_AMP) {
        bulk_data_desc_t bulk_data;

        bulk_data.data_length = 0;

        spin_lock_irqsave(&priv->m4_lock, flags);
        if (interfacePriv->m4_bulk_data.data_length > 0) {
            bulk_data = interfacePriv->m4_bulk_data;
            interfacePriv->m4_bulk_data.net_buf_length = 0;
            interfacePriv->m4_bulk_data.data_length = 0;
            interfacePriv->m4_bulk_data.os_data_ptr = interfacePriv->m4_bulk_data.os_net_buf_ptr = NULL;
        }
        spin_unlock_irqrestore(&priv->m4_lock, flags);

        if (bulk_data.data_length != 0) {
            unifi_trace(priv, UDBG5, "CsrWifiRouterCtrlMediaStatusReqHandler: free M4\n");
            unifi_net_data_free(priv, &bulk_data);
        }

        if ((req->mediaStatus == CSR_WIFI_SME_MEDIA_STATUS_CONNECTED) &&
            (interfacePriv->connected != UnifiConnected)) {

            switch(interfacePriv->interfaceMode){
                case CSR_WIFI_ROUTER_CTRL_MODE_AP:
                case CSR_WIFI_ROUTER_CTRL_MODE_P2PGO:
                    interfacePriv->connected = UnifiConnected;
                    netif_carrier_on(priv->netdev[req->interfaceTag]);
#ifdef CSR_SUPPORT_WEXT
                    wext_send_started_event(priv);
#endif
                    unifi_trace(priv, UDBG1,
                                "CsrWifiRouterCtrlMediaStatusReqHandler: AP/P2PGO setting netif_carrier_on\n");
                    netif_tx_wake_all_queues(priv->netdev[req->interfaceTag]);
                    break;

                default:
#ifdef CSR_SUPPORT_WEXT
                /* In the WEXT builds (sme and native), the userspace is not ready
                 * to process any EAPOL or WAPI packets, until it has been informed
                 * of the NETDEV_CHANGE.
                 */
                if (interfacePriv->netdev_callback_registered && (interfacePriv->interfaceMode != CSR_WIFI_ROUTER_CTRL_MODE_P2PCLI)) {
                    interfacePriv->wait_netdev_change = TRUE;
                    unifi_trace(priv, UDBG1,
                                "CsrWifiRouterCtrlMediaStatusReqHandler: waiting for NETDEV_CHANGE\n");
                    /*
                     * Carrier can go to on, only after wait_netdev_change is set to TRUE.
                     * Otherwise there can be a race in uf_netdev_event().
                     */
                    netif_carrier_on(priv->netdev[req->interfaceTag]);
                    unifi_trace(priv, UDBG1,
                                "CsrWifiRouterCtrlMediaStatusReqHandler: STA/P2PCLI setting netif_carrier_on\n");
                }
                else
#endif
                {
                    /* In the NME build, the userspace does not wait for the NETDEV_CHANGE
                     * so it is ready to process all the EAPOL or WAPI packets.
                     * At this point, we enable all the Tx queues, and we indicate any packets
                     * that are queued (and the respective port is opened).
                     */
                    static const CsrWifiMacAddress broadcast_address = {{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}};
                    interfacePriv->connected = UnifiConnected;
                    unifi_trace(priv, UDBG1,
                                "CsrWifiRouterMediaStatusReqHandler: UnifiConnected && netif_carrier_on\n");
                    netif_carrier_on(priv->netdev[req->interfaceTag]);
                    netif_tx_wake_all_queues(priv->netdev[req->interfaceTag]);
                    uf_process_rx_pending_queue(priv, UF_UNCONTROLLED_PORT_Q, broadcast_address, 1, interfacePriv->InterfaceTag);
                    uf_process_rx_pending_queue(priv, UF_CONTROLLED_PORT_Q, broadcast_address, 1, interfacePriv->InterfaceTag);
                }
                break;
            }
        }

        if (req->mediaStatus == CSR_WIFI_SME_MEDIA_STATUS_DISCONNECTED) {
#ifdef CSR_SUPPORT_WEXT
            unifi_trace(priv, UDBG1,
                        "CsrWifiRouterMediaStatusReqHandler: cancel waiting for NETDEV_CHANGE\n");
            interfacePriv->wait_netdev_change = FALSE;
#endif
            unifi_trace(priv, UDBG1,
                        "CsrWifiRouterMediaStatusReqHandler: setting netif_carrier_off\n");
            netif_carrier_off(priv->netdev[req->interfaceTag]);
#ifdef CSR_SUPPORT_WEXT
            switch(interfacePriv->interfaceMode){
                case CSR_WIFI_ROUTER_CTRL_MODE_AP:
                case CSR_WIFI_ROUTER_CTRL_MODE_P2PGO:
                     wext_send_started_event(priv);
                     break;
                default:
                     break;
            }
#endif
            interfacePriv->connected = UnifiNotConnected;
        }
    } else {
        /* For AMP, just update the L2 connected flag */
        if (req->mediaStatus == CSR_WIFI_SME_MEDIA_STATUS_CONNECTED) {
            unifi_trace(priv, UDBG1, "CsrWifiRouterCtrlMediaStatusReqHandler: AMP connected\n");
            interfacePriv->connected = UnifiConnected;
        } else {
            unifi_trace(priv, UDBG1, "CsrWifiRouterCtrlMediaStatusReqHandler: AMP disconnected\n");
            interfacePriv->connected = UnifiNotConnected;
        }
    }
}


void CsrWifiRouterCtrlHipReqHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
    unifi_priv_t *priv = (unifi_priv_t*)drvpriv;
    CsrWifiRouterCtrlHipReq* hipreq = (CsrWifiRouterCtrlHipReq*)msg;
    bulk_data_param_t bulkdata;
    u8 *signal_ptr;
    int signal_length;
    int r=0;
    void *dest;
    CsrResult csrResult;
    CSR_SIGNAL *signal;
    u16 interfaceTag = 0;
    CSR_MA_PACKET_REQUEST *req;
    netInterface_priv_t *interfacePriv = priv->interfacePriv[interfaceTag];

    if (priv == NULL) {
        return;
    }
    if (priv->smepriv == NULL) {
        unifi_error(priv, "CsrWifiRouterCtrlHipReqHandler: invalid smepriv\n");
        return;
    }
    if (interfaceTag >= CSR_WIFI_NUM_INTERFACES) {
        unifi_error(priv, "CsrWifiRouterCtrlHipReqHandler: invalid interfaceTag\n");
        return;
    }

    /* Initialize bulkdata to avoid os_net_buf is garbage */
    memset(&bulkdata, 0, sizeof(bulk_data_param_t));

    signal = (CSR_SIGNAL *)hipreq->mlmeCommand;

    unifi_trace(priv, UDBG4, "CsrWifiRouterCtrlHipReqHandler: 0x04%X ---->\n",
                *((u16*)hipreq->mlmeCommand));

    /* Construct the signal. */
    signal_ptr = (u8*)hipreq->mlmeCommand;
    signal_length = hipreq->mlmeCommandLength;

    /*
     * The MSB of the sender ID needs to be set to the client ID.
     * The LSB is controlled by the SME.
     */
    signal_ptr[5] = (priv->sme_cli->sender_id >> 8) & 0xff;

    /* Allocate buffers for the bulk data. */
    if (hipreq->dataRef1Length) {
        csrResult = unifi_net_data_malloc(priv, &bulkdata.d[0], hipreq->dataRef1Length);
        if (csrResult == CSR_RESULT_SUCCESS) {
            dest = (void*)bulkdata.d[0].os_data_ptr;
            memcpy(dest, hipreq->dataRef1, hipreq->dataRef1Length);
            bulkdata.d[0].data_length = hipreq->dataRef1Length;
        } else {
            unifi_warning(priv, "signal not sent down, allocation failed in CsrWifiRouterCtrlHipReqHandler\n");
            return;
        }
    } else {
        bulkdata.d[0].os_data_ptr = NULL;
        bulkdata.d[0].data_length = 0;
    }
    if (hipreq->dataRef2Length) {
        csrResult = unifi_net_data_malloc(priv, &bulkdata.d[1], hipreq->dataRef2Length);
        if (csrResult == CSR_RESULT_SUCCESS) {
            dest = (void*)bulkdata.d[1].os_data_ptr;
            memcpy(dest, hipreq->dataRef2, hipreq->dataRef2Length);
            bulkdata.d[1].data_length = hipreq->dataRef2Length;
        } else {
            if (bulkdata.d[0].data_length)
            {
                unifi_net_data_free(priv, &bulkdata.d[0]);
            }
            unifi_warning(priv, "signal not sent down, allocation failed in CsrWifiRouterCtrlHipReqHandler\n");
            return;
        }
    } else {
        bulkdata.d[1].os_data_ptr = NULL;
        bulkdata.d[1].data_length = 0;
    }

    unifi_trace(priv, UDBG3, "SME SEND: Signal 0x%.4X \n",
                *((u16*)signal_ptr));
    if (signal->SignalPrimitiveHeader.SignalId == CSR_MA_PACKET_REQUEST_ID)
    {
        CSR_SIGNAL unpacked_signal;
        read_unpack_signal((u8 *) signal, &unpacked_signal);
        req = &unpacked_signal.u.MaPacketRequest;
        interfaceTag = req->VirtualInterfaceIdentifier & 0xff;
        switch(interfacePriv->interfaceMode)
        {
            case CSR_WIFI_ROUTER_CTRL_MODE_NONE:
                unifi_error(priv, "CsrWifiRouterCtrlHipReqHandler: invalid mode: NONE \n");
                break;
            default:
                unifi_trace(priv, UDBG5, "mode is %x\n", interfacePriv->interfaceMode);
        }
        /* While sending ensure that first 2 bits b31 and b30 are 00. These are used for local routing*/
        r = uf_process_ma_packet_req(priv, req->Ra.x, (req->HostTag & 0x3FFFFFFF), interfaceTag,
                                     req->TransmissionControl, req->TransmitRate,
                                     req->Priority, signal->SignalPrimitiveHeader.SenderProcessId,
                                     &bulkdata);
        if (r)
        {
            if (bulkdata.d[0].data_length)
            {
                unifi_net_data_free(priv, &bulkdata.d[0]);
            }
            if (bulkdata.d[1].data_length)
            {
                unifi_net_data_free(priv, &bulkdata.d[1]);
            }
        }
    } else {
        /* ul_send_signal_raw frees the bulk data if it fails */
        r = ul_send_signal_raw(priv, signal_ptr, signal_length, &bulkdata);
    }

    if (r) {
        unifi_error(priv,
                    "CsrWifiRouterCtrlHipReqHandler: Failed to send signal (0x%.4X - %u)\n",
                    *((u16*)signal_ptr), r);
        CsrWifiRouterCtrlWifiOffIndSend(priv->CSR_WIFI_SME_IFACEQUEUE,0,CSR_WIFI_SME_CONTROL_INDICATION_ERROR);
    }

    unifi_trace(priv, UDBG4, "CsrWifiRouterCtrlHipReqHandler: <----\n");
}

#ifdef CSR_WIFI_SEND_GRATUITOUS_ARP
static void
uf_send_gratuitous_arp(unifi_priv_t *priv, u16 interfaceTag)
{
    netInterface_priv_t *interfacePriv = priv->interfacePriv[interfaceTag];
    CSR_PRIORITY priority;
    CSR_SIGNAL signal;
    bulk_data_param_t bulkdata;
    CsrResult csrResult;
    struct sk_buff *skb, *newSkb = NULL;
    s8 protection;
    int r;
    static const u8 arp_req[36] = {0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00,
                                         0x08, 0x06, 0x00, 0x01, 0x08, 0x00, 0x06, 0x04, 0x00, 0x01,
                                         0x00, 0x02, 0x5f, 0x20, 0x2f, 0x02,
                                         0xc0, 0xa8, 0x00, 0x02,
                                         0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                                         0xc0, 0xa8, 0x00, 0x02};

    func_enter();

    csrResult = unifi_net_data_malloc(priv, &bulkdata.d[0], sizeof(arp_req));
    if (csrResult != CSR_RESULT_SUCCESS)
    {
        unifi_error(priv, "Failed to allocate bulk data in CsrWifiSmeRoamCompleteIndHandler()\n");
        return;
    }
    skb = (struct sk_buff *)(bulkdata.d[0].os_net_buf_ptr);
    skb->len = bulkdata.d[0].data_length;

    memcpy(skb->data, arp_req, sizeof(arp_req));
    /* add MAC and IP address */
    memcpy(skb->data + 16, priv->netdev[interfaceTag]->dev_addr, ETH_ALEN);
    skb->data[22] = (priv->sta_ip_address      ) & 0xFF;
    skb->data[23] = (priv->sta_ip_address >>  8) & 0xFF;
    skb->data[24] = (priv->sta_ip_address >> 16) & 0xFF;
    skb->data[25] = (priv->sta_ip_address >> 24) & 0xFF;
    skb->data[32] = (priv->sta_ip_address      ) & 0xFF;
    skb->data[33] = (priv->sta_ip_address >>  8) & 0xFF;
    skb->data[34] = (priv->sta_ip_address >> 16) & 0xFF;
    skb->data[35] = (priv->sta_ip_address >> 24) & 0xFF;

    bulkdata.d[1].os_data_ptr = NULL;
    bulkdata.d[1].os_net_buf_ptr = NULL;
    bulkdata.d[1].net_buf_length = bulkdata.d[1].data_length = 0;

    if ((protection = uf_get_protection_bit_from_interfacemode(priv, interfaceTag, &arp_req[26])) < 0)
    {
        unifi_error(priv, "CsrWifiSmeRoamCompleteIndHandler: Failed to determine protection mode\n");
        unifi_net_data_free(priv, &bulkdata.d[0]);
        return;
    }

    if ((priv->sta_wmm_capabilities & QOS_CAPABILITY_WMM_ENABLED) == 1)
    {
        priority = CSR_QOS_UP0;
    }
    else
    {
        priority = CSR_CONTENTION;
    }

    if (prepare_and_add_macheader(priv, skb, newSkb, priority, &bulkdata,
                                  interfaceTag, &arp_req[26],
                                  priv->netdev[interfaceTag]->dev_addr, protection))
    {
        unifi_error(priv, "CsrWifiSmeRoamCompleteIndHandler: failed to create MAC header\n");
        unifi_net_data_free(priv, &bulkdata.d[0]);
        return;
    }
    bulkdata.d[0].os_data_ptr = skb->data;
    bulkdata.d[0].os_net_buf_ptr = skb;
    bulkdata.d[0].data_length = skb->len;

    unifi_frame_ma_packet_req(priv, priority, 0, 0xffffffff, interfaceTag,
                              CSR_NO_CONFIRM_REQUIRED, priv->netdev_client->sender_id,
                              interfacePriv->bssid.a, &signal);

    r = ul_send_signal_unpacked(priv, &signal, &bulkdata);
    if (r)
    {
        unifi_error(priv, "CsrWifiSmeRoamCompleteIndHandler: failed to send QOS data null packet result: %d\n",r);
        unifi_net_data_free(priv, &bulkdata.d[0]);
        return;
    }

    func_exit();

}
#endif /* CSR_WIFI_SEND_GRATUITOUS_ARP */

/*
 * ---------------------------------------------------------------------------
 * configure_data_port
 *
 *      Store the new controlled port configuration.
 *
 * Arguments:
 *      priv            Pointer to device private context struct
 *      port_cfg        Pointer to the port configuration
 *
 * Returns:
 *      An unifi_ControlledPortAction value.
 * ---------------------------------------------------------------------------
 */
static int
configure_data_port(unifi_priv_t *priv,
        CsrWifiRouterCtrlPortAction port_action,
        const CsrWifiMacAddress *macAddress,
        const int queue,
        u16 interfaceTag)
{
    const u8 broadcast_mac_address[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    unifi_port_config_t *port;
    netInterface_priv_t *interfacePriv;
    int i;
    const char* controlled_string; /* cosmetic "controlled"/"uncontrolled" for trace */

    if (interfaceTag >= CSR_WIFI_NUM_INTERFACES) {
        unifi_error(priv, "configure_data_port: bad interfaceTag\n");
        return -EFAULT;
    }

    interfacePriv = priv->interfacePriv[interfaceTag];

    if (queue == UF_CONTROLLED_PORT_Q) {
        port = &interfacePriv->controlled_data_port;
        controlled_string = "controlled";
    } else {
        port = &interfacePriv->uncontrolled_data_port;
        controlled_string = "uncontrolled";
    }

	unifi_trace(priv, UDBG2,
		"port config request %pM %s with port_action %d.\n",
		macAddress->a, controlled_string, port_action);

    /* If the new configuration has the broadcast MAC address or if we are in infrastructure mode then clear the list first and set port overide mode */
    if ((CSR_WIFI_ROUTER_CTRL_MODE_STA == interfacePriv->interfaceMode ||
        interfacePriv->interfaceMode == CSR_WIFI_ROUTER_CTRL_MODE_P2PCLI) ||
        !memcmp(macAddress->a, broadcast_mac_address, ETH_ALEN)) {

        port->port_cfg[0].port_action = port_action;
        port->port_cfg[0].mac_address = *macAddress;
        port->port_cfg[0].in_use = TRUE;
        port->entries_in_use = 1;
        port->overide_action = UF_DATA_PORT_OVERIDE;

        unifi_trace(priv, UDBG2, "%s port override on\n",
                    (queue == UF_CONTROLLED_PORT_Q) ? "Controlled" : "Uncontrolled");

        /* Discard the remaining entries in the port config table */
        for (i = 1; i < UNIFI_MAX_CONNECTIONS; i++) {
            port->port_cfg[i].in_use = FALSE;
        }

        if (port_action == CSR_WIFI_ROUTER_CTRL_PORT_ACTION_8021X_PORT_OPEN) {
            unifi_trace(priv, UDBG1, "%s port broadcast set to open.\n",
                        (queue == UF_CONTROLLED_PORT_Q) ? "Controlled" : "Uncontrolled");

            /*
             * Ask stack to schedule for transmission any packets queued
             * while controlled port was not open.
             * Use netif_schedule() instead of netif_wake_queue() because
             * transmission should be already enabled at this point. If it
             * is not, probably the interface is down and should remain as is.
             */
            uf_resume_data_plane(priv, queue, *macAddress, interfaceTag);

#ifdef CSR_WIFI_SEND_GRATUITOUS_ARP
            if ((CSR_WIFI_ROUTER_CTRL_MODE_STA == interfacePriv->interfaceMode) &&
                (queue == UF_CONTROLLED_PORT_Q) && (priv->sta_ip_address != 0xFFFFFFFF))
            {
                uf_send_gratuitous_arp(priv, interfaceTag);
            }
#endif
        } else {
            unifi_trace(priv, UDBG1, "%s port broadcast set to %s.\n",
                        (queue == UF_CONTROLLED_PORT_Q) ? "Controlled" : "Uncontrolled",
                        (port_action == CSR_WIFI_ROUTER_CTRL_PORT_ACTION_8021X_PORT_CLOSED_DISCARD) ? "discard": "closed");

            /* If port is closed, discard all the pending Rx packets */
            if (port_action == CSR_WIFI_ROUTER_CTRL_PORT_ACTION_8021X_PORT_CLOSED_DISCARD) {
                uf_free_pending_rx_packets(priv, queue, *macAddress,interfaceTag);
            }
        }
    } else {
        /* store the new configuration, either in the entry with matching mac address (if already present),
         * otherwise in a new entry
         */

        int found_entry_flag;
        int first_free_slot = -1;

        /* If leaving override mode, free the port entry used for override */
        if (port->overide_action == UF_DATA_PORT_OVERIDE) {
            port->port_cfg[0].in_use = FALSE;
            port->entries_in_use = 0;
            port->overide_action = UF_DATA_PORT_NOT_OVERIDE;

            unifi_trace(priv, UDBG2, "%s port override off\n",
                        (queue == UF_CONTROLLED_PORT_Q) ? "Controlled" : "Uncontrolled");
        }

        found_entry_flag = 0;
        for (i = 0; i < UNIFI_MAX_CONNECTIONS; i++) {
            if (port->port_cfg[i].in_use) {
                if (!memcmp(&port->port_cfg[i].mac_address.a, macAddress->a, ETH_ALEN)) {
                    /* We've seen this address before, reconfigure it */
                    port->port_cfg[i].port_action = port_action;
                    found_entry_flag = 1;
                    break;
                }
            } else if (first_free_slot == -1) {
                /* Remember the first free slot on the way past so it can be claimed
                 * if this turns out to be a new MAC address (to save walking the list again).
                 */
                first_free_slot = i;
            }
        }

        /* At this point we found an existing entry and have updated it, or need to
         * add a new entry. If all slots are allocated, give up and return an error.
         */
        if (!found_entry_flag) {
            if (first_free_slot == -1) {
                unifi_error(priv, "no free slot found in port config array (%d used)\n", port->entries_in_use);
                return -EFAULT;
            } else {
                port->entries_in_use++;
            }

            unifi_trace(priv, UDBG3, "port config index assigned in config_data_port = %d\n", first_free_slot);
            port->port_cfg[first_free_slot].in_use = TRUE;
            port->port_cfg[first_free_slot].port_action = port_action;
            port->port_cfg[first_free_slot].mac_address = *macAddress;
        }

        if (port_action == CSR_WIFI_ROUTER_CTRL_PORT_ACTION_8021X_PORT_OPEN) {
            /*
             * Ask stack to schedule for transmission any packets queued
             * while controlled port was not open.
             * Use netif_schedule() instead of netif_wake_queue() because
             * transmission should be already enabled at this point. If it
             * is not, probably the interface is down and should remain as is.
             */
            uf_resume_data_plane(priv, queue, *macAddress, interfaceTag);
        }

        /*
         * If port is closed, discard all the pending Rx packets
         * coming from the peer station.
         */
        if (port_action == CSR_WIFI_ROUTER_CTRL_PORT_ACTION_8021X_PORT_CLOSED_DISCARD) {
            uf_free_pending_rx_packets(priv, queue, *macAddress,interfaceTag);
        }

	unifi_trace(priv, UDBG2,
		"port config %pM with port_action %d.\n",
		macAddress->a, port_action);
    }
    return 0;
} /* configure_data_port() */


void CsrWifiRouterCtrlPortConfigureReqHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
    unifi_priv_t *priv = (unifi_priv_t*)drvpriv;
    CsrWifiRouterCtrlPortConfigureReq* req = (CsrWifiRouterCtrlPortConfigureReq*)msg;
    netInterface_priv_t *interfacePriv = priv->interfacePriv[req->interfaceTag];

    unifi_trace(priv, UDBG3, "entering CsrWifiRouterCtrlPortConfigureReqHandler\n");
    if (priv->smepriv == NULL) {
        unifi_error(priv, "CsrWifiRouterCtrlPortConfigureReqHandler: invalid smepriv\n");
        return;
    }

    /* To update the protection status of the peer/station */
    switch(interfacePriv->interfaceMode)
    {
        case CSR_WIFI_ROUTER_CTRL_MODE_STA:
	    case CSR_WIFI_ROUTER_CTRL_MODE_AMP:
        case CSR_WIFI_ROUTER_CTRL_MODE_IBSS:
        case CSR_WIFI_ROUTER_CTRL_MODE_P2PCLI:
            /* Since for Unifi as a station, the station record not maintained & interfaceID is
             * only needed to update the peer protection status
             */
            interfacePriv->protect = req->setProtection;
            break;
        case CSR_WIFI_ROUTER_CTRL_MODE_AP:
        case CSR_WIFI_ROUTER_CTRL_MODE_P2PGO:
            {
                u8 i;
                CsrWifiRouterCtrlStaInfo_t *staRecord;
                /* Ifscontrolled port is open means, The peer has been added to station record
                 * so that the protection corresponding to the peer is valid in this req
                 */
                if (req->controlledPortAction == CSR_WIFI_ROUTER_CTRL_PORT_ACTION_8021X_PORT_OPEN) {
                    for(i =0; i < UNIFI_MAX_CONNECTIONS; i++) {
                        staRecord = (CsrWifiRouterCtrlStaInfo_t *) (interfacePriv->staInfo[i]);
                        if (staRecord) {
                                /* Find the matching station record & set the protection type */
                                if (!memcmp(req->macAddress.a, staRecord->peerMacAddress.a, ETH_ALEN)) {
                                        staRecord->protection = req->setProtection;
                                        break;
                                }
                        }
                    }
                }
            }
            break;
        default:
            unifi_trace(priv, UDBG1, "CsrWifiRouterCtrlPortConfigureReqHandler(0x%.4X) Uncaught mode %d\n",
                        msg->source, interfacePriv->interfaceMode);
    }

    configure_data_port(priv, req->uncontrolledPortAction, (const CsrWifiMacAddress *)&req->macAddress,
                        UF_UNCONTROLLED_PORT_Q, req->interfaceTag);
    configure_data_port(priv, req->controlledPortAction, (const CsrWifiMacAddress *)&req->macAddress,
                        UF_CONTROLLED_PORT_Q, req->interfaceTag);

    CsrWifiRouterCtrlPortConfigureCfmSend(msg->source,req->clientData,req->interfaceTag,
                                      CSR_RESULT_SUCCESS, req->macAddress);
    unifi_trace(priv, UDBG3, "leaving CsrWifiRouterCtrlPortConfigureReqHandler\n");
}


void CsrWifiRouterCtrlWifiOnReqHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
    unifi_priv_t *priv = (unifi_priv_t*)drvpriv;
    CsrWifiRouterCtrlVersions versions;
    CsrWifiRouterCtrlWifiOnReq* req = (CsrWifiRouterCtrlWifiOnReq*)msg;
    int r,i;
    CsrResult csrResult;

    if (priv == NULL) {
        return;
    }
    if( priv->wol_suspend ) {
        unifi_trace(priv, UDBG1, "CsrWifiRouterCtrlWifiOnReqHandler: Don't reset mode\n");
    } else {
#ifdef ANDROID_BUILD
        /* Take the wakelock while Wi-Fi On is in progress */
        unifi_trace(priv, UDBG1, "CsrWifiRouterCtrlWifiOnReqHandler: take wake lock\n");
        wake_lock(&unifi_sdio_wake_lock);
#endif
        for (i=0; i<CSR_WIFI_NUM_INTERFACES; i++) {
            unifi_trace(priv, UDBG1, "CsrWifiRouterCtrlWifiOnReqHandler: Setting interface %d to NONE\n", i );

            priv->interfacePriv[i]->interfaceMode = 0;
        }
    }
    unifi_trace(priv, UDBG1, "CsrWifiRouterCtrlWifiOnReqHandler(0x%.4X) req->dataLength=%d req->data=0x%x\n", msg->source, req->dataLength, req->data);

    if(req->dataLength==3 && req->data && req->data[0]==0 && req->data[1]==1 && req->data[2]==1)
    {
        priv->cmanrTestMode = TRUE;
        unifi_trace(priv, UDBG1, "CsrWifiRouterCtrlWifiOnReqHandler: cmanrTestMode=%d\n", priv->cmanrTestMode);
    }
    else
    {
        priv->cmanrTestMode = FALSE;
    }

    /*
     * The request to initialise UniFi might come while UniFi is running.
     * We need to block all I/O activity until the reset completes, otherwise
     * an SDIO error might occur resulting an indication to the SME which
     * makes it think that the initialisation has failed.
     */
    priv->bh_thread.block_thread = 1;

    /* Update the wifi_on state */
    priv->wifi_on_state = wifi_on_in_progress;

    /* If UniFi was unpowered, acquire the firmware for download to chip */
    if (!priv->wol_suspend) {
        r = uf_request_firmware_files(priv, UNIFI_FW_STA);
        if (r) {
            unifi_error(priv, "CsrWifiRouterCtrlWifiOnReqHandler: Failed to get f/w\n");
            CsrWifiRouterCtrlWifiOnCfmSend(msg->source, req->clientData, CSR_RESULT_FAILURE);
            return;
        }
    } else {
        unifi_trace(priv, UDBG1, "Don't need firmware\n");
    }

    /* Power on UniFi (which may not necessarily have been off) */
    CsrSdioClaim(priv->sdio);
    csrResult = CsrSdioPowerOn(priv->sdio);
    CsrSdioRelease(priv->sdio);
    if (csrResult != CSR_RESULT_SUCCESS && csrResult != CSR_SDIO_RESULT_NOT_RESET) {
        unifi_error(priv, "CsrWifiRouterCtrlWifiOnReqHandler: Failed to power on UniFi\n");
        CsrWifiRouterCtrlWifiOnCfmSend(msg->source, req->clientData, CSR_RESULT_FAILURE);
        return;
    }

    /* If CsrSdioPowerOn() returns CSR_RESULT_SUCCESS, it means that we need to initialise UniFi */
    if (csrResult == CSR_RESULT_SUCCESS && !priv->wol_suspend) {
        /* Initialise UniFi hardware */
        r = uf_init_hw(priv);
        if (r) {
            unifi_error(priv, "CsrWifiRouterCtrlWifiOnReqHandler: Failed to initialise h/w, error %d\n", r);
            CsrWifiRouterCtrlWifiOnCfmSend(msg->source, req->clientData, CSR_RESULT_FAILURE);
            return;
        }
    } else {
        unifi_trace(priv, UDBG1, "UniFi already initialised\n");
    }

    /* Completed handling of wake up from suspend with UniFi powered */
    priv->wol_suspend = FALSE;

    /* Re-enable the I/O thread */
    priv->bh_thread.block_thread = 0;

    /*
     * Start the I/O thread. The thread might be already running.
     * This fine, just carry on with the request.
     */
    r = uf_init_bh(priv);
    if (r) {
        CsrSdioClaim(priv->sdio);
        CsrSdioPowerOff(priv->sdio);
        CsrSdioRelease(priv->sdio);
        CsrWifiRouterCtrlWifiOnCfmSend(msg->source, req->clientData, CSR_RESULT_FAILURE);
        return;
    }

    /* Get the version information from the core */
    unifi_card_info(priv->card, &priv->card_info);

    /* Set the sme queue id */
    priv->CSR_WIFI_SME_IFACEQUEUE = msg->source;
    CSR_WIFI_SME_IFACEQUEUE = msg->source;


    /* Copy to the unifiio_card_info structure. */
    versions.chipId = priv->card_info.chip_id;
    versions.chipVersion = priv->card_info.chip_version;
    versions.firmwareBuild = priv->card_info.fw_build;
    versions.firmwareHip = priv->card_info.fw_hip_version;
    versions.routerBuild = (char*)CSR_WIFI_VERSION;
    versions.routerHip = (UNIFI_HIP_MAJOR_VERSION << 8) | UNIFI_HIP_MINOR_VERSION;

    CsrWifiRouterCtrlWifiOnIndSend(msg->source, 0, CSR_RESULT_SUCCESS, versions);

    /* Update the wifi_on state */
    priv->wifi_on_state = wifi_on_done;
}


/*
 * wifi_off:
 *      Common code for CsrWifiRouterCtrlWifiOffReqHandler() and
 *      CsrWifiRouterCtrlWifiOffRspHandler().
 */
static void
wifi_off(unifi_priv_t *priv)
{
    int power_off;
    int priv_instance;
    int i;
    CsrResult csrResult;


    /* Already off? */
    if (priv->wifi_on_state == wifi_on_unspecified) {
        unifi_trace(priv, UDBG1, "wifi_off already\n");
        return;
    }

    unifi_trace(priv, UDBG1, "wifi_off\n");

    /* Destroy the Traffic Analysis Module */
    cancel_work_sync(&priv->ta_ind_work.task);
    cancel_work_sync(&priv->ta_sample_ind_work.task);
#ifdef CSR_SUPPORT_WEXT
    cancel_work_sync(&priv->sme_config_task);
    wext_send_disassoc_event(priv);
#endif

    /* Cancel pending M4 stuff */
    for (i = 0; i < CSR_WIFI_NUM_INTERFACES; i++) {
        if (priv->netdev[i]) {
            netInterface_priv_t *netpriv = (netInterface_priv_t *) netdev_priv(priv->netdev[i]);
            cancel_work_sync(&netpriv->send_m4_ready_task);
        }
    }
    flush_workqueue(priv->unifi_workqueue);

    /* fw_init parameter can prevent power off UniFi, for debugging */
    priv_instance = uf_find_priv(priv);
    if (priv_instance == -1) {
        unifi_warning(priv,
                "CsrWifiRouterCtrlStopReqHandler: Unknown priv instance, will power off card.\n");
        power_off = 1;
    } else {
        power_off = (fw_init[priv_instance] > 0) ? 0 : 1;
    }

    /* Production test mode requires power to the chip, too */
    if (priv->ptest_mode) {
        power_off = 0;
    }

    /* Stop the bh_thread */
    uf_stop_thread(priv, &priv->bh_thread);

    /* Read the f/w panic codes, if any. Protect against second wifi_off() call,
     * which may happen if SME requests a wifi_off and closes the char device */
    if (priv->init_progress != UNIFI_INIT_NONE) {
        CsrSdioClaim(priv->sdio);
        unifi_capture_panic(priv->card);
        CsrSdioRelease(priv->sdio);
    }

    /* Unregister the interrupt handler */
    if (csr_sdio_linux_remove_irq(priv->sdio)) {
        unifi_notice(priv,
                "csr_sdio_linux_remove_irq failed to talk to card.\n");
    }

    if (power_off) {
        unifi_trace(priv, UDBG2,
                    "Force low power and try to power off\n");
        /* Put UniFi to deep sleep, in case we can not power it off */
        CsrSdioClaim(priv->sdio);
        csrResult = unifi_force_low_power_mode(priv->card);
        CsrSdioRelease(priv->sdio);

        CsrSdioPowerOff(priv->sdio);
    }

    /* Consider UniFi to be uninitialised */
    priv->init_progress = UNIFI_INIT_NONE;
    priv->wifi_on_state = wifi_on_unspecified;


} /* wifi_off() */


void CsrWifiRouterCtrlWifiOffReqHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
    unifi_priv_t *priv = (unifi_priv_t*)drvpriv;
    CsrWifiRouterCtrlWifiOffReq* req = (CsrWifiRouterCtrlWifiOffReq*)msg;
    int i = 0;

    if (priv == NULL) {
        return;
    }

    unifi_trace(priv, UDBG1, "CsrWifiRouterCtrlWifiOffReqHandler(0x%.4X)\n", msg->source);

    /* Stop the network traffic on all interfaces before freeing the core. */
    for (i=0; i<CSR_WIFI_NUM_INTERFACES; i++) {
        netInterface_priv_t *interfacePriv = priv->interfacePriv[i];
        if (interfacePriv->netdev_registered == 1) {
            netif_carrier_off(priv->netdev[i]);
            netif_tx_stop_all_queues(priv->netdev[i]);
            interfacePriv->connected = UnifiConnectedUnknown;
        }
        interfacePriv->interfaceMode = 0;

        /* Enable all queues by default */
        interfacePriv->queueEnabled[0] = 1;
        interfacePriv->queueEnabled[1] = 1;
        interfacePriv->queueEnabled[2] = 1;
        interfacePriv->queueEnabled[3] = 1;
    }
    wifi_off(priv);

    CsrWifiRouterCtrlWifiOffCfmSend(msg->source,req->clientData);

    /* If this is called in response to closing the character device, the
     * caller must use uf_sme_cancel_request() to terminate any pending SME
     * blocking request or there will be a delay while the operation times out.
     */
}


void CsrWifiRouterCtrlQosControlReqHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
    unifi_priv_t *priv = (unifi_priv_t*)drvpriv;
    CsrWifiRouterCtrlQosControlReq* req = (CsrWifiRouterCtrlQosControlReq*)msg;
    netInterface_priv_t *interfacePriv;

    if (priv->smepriv == NULL) {
        unifi_error(priv, "CsrWifiRouterCtrlQosControlReqHandler: invalid smepriv\n");
        return;
    }

    unifi_trace(priv, UDBG4, "CsrWifiRouterCtrlQosControlReqHandler:scontrol = %d", req->control);

    if (req->interfaceTag >= CSR_WIFI_NUM_INTERFACES) {
        unifi_error(priv, "CsrWifiRouterCtrlQosControlReqHandler: interfaceID >= CSR_WIFI_NUM_INTERFACES.\n");
        return;
    }
    interfacePriv = priv->interfacePriv[req->interfaceTag];

    if (req->control == CSR_WIFI_ROUTER_CTRL_QOS_CONTROL_WMM_ON) {
        priv->sta_wmm_capabilities |= QOS_CAPABILITY_WMM_ENABLED;
        unifi_trace(priv, UDBG1, "WMM enabled\n");

        unifi_trace(priv, UDBG1, "Queue Config %x\n", req->queueConfig);

        interfacePriv->queueEnabled[UNIFI_TRAFFIC_Q_BK] = (req->queueConfig & CSR_WIFI_ROUTER_CTRL_QUEUE_BK_ENABLE)?1:0;
        interfacePriv->queueEnabled[UNIFI_TRAFFIC_Q_BE] = (req->queueConfig & CSR_WIFI_ROUTER_CTRL_QUEUE_BE_ENABLE)?1:0;
        interfacePriv->queueEnabled[UNIFI_TRAFFIC_Q_VI] = (req->queueConfig & CSR_WIFI_ROUTER_CTRL_QUEUE_VI_ENABLE)?1:0;
        interfacePriv->queueEnabled[UNIFI_TRAFFIC_Q_VO] = (req->queueConfig & CSR_WIFI_ROUTER_CTRL_QUEUE_VO_ENABLE)?1:0;

    } else {
        priv->sta_wmm_capabilities = 0;
        unifi_trace(priv, UDBG1, "WMM disabled\n");
    }
}


void CsrWifiRouterCtrlTclasAddReqHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
    unifi_priv_t *priv = (unifi_priv_t*)drvpriv;
    CsrWifiRouterCtrlTclasAddReq* req = (CsrWifiRouterCtrlTclasAddReq*)msg;

    if (priv == NULL) {
        unifi_error(priv, "CsrWifiRouterCtrlTclasAddReqHandler: invalid smepriv\n");
        return;
    }

    CsrWifiRouterCtrlTclasAddCfmSend(msg->source, req->clientData, req->interfaceTag , CSR_RESULT_SUCCESS);
}

void CsrWifiRouterCtrlTclasDelReqHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
    unifi_priv_t *priv = (unifi_priv_t*)drvpriv;
    CsrWifiRouterCtrlTclasDelReq* req = (CsrWifiRouterCtrlTclasDelReq*)msg;

    if (priv == NULL) {
        unifi_error(priv, "CsrWifiRouterCtrlTclasDelReqHandler: invalid smepriv\n");
        return;
    }

    CsrWifiRouterCtrlTclasDelCfmSend(msg->source, req->clientData, req->interfaceTag, CSR_RESULT_SUCCESS);
}


void CsrWifiRouterCtrlConfigurePowerModeReqHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
    unifi_priv_t *priv = (unifi_priv_t*)drvpriv;
    CsrWifiRouterCtrlConfigurePowerModeReq* req = (CsrWifiRouterCtrlConfigurePowerModeReq*)msg;
    enum unifi_low_power_mode pm;
    CsrResult csrResult;

    if (priv->smepriv == NULL) {
        unifi_error(priv, "CsrWifiRouterCtrlConfigurePowerModeReqHandler: invalid smepriv\n");
        return;
    }

    if (req->mode == CSR_WIFI_ROUTER_CTRL_LOW_POWER_MODE_DISABLED) {
        pm = UNIFI_LOW_POWER_DISABLED;
    } else {
        pm = UNIFI_LOW_POWER_ENABLED;
    }

    unifi_trace(priv, UDBG2,
                "CsrWifiRouterCtrlConfigurePowerModeReqHandler (mode=%d, wake=%d)\n",
                req->mode, req->wakeHost);
    csrResult = unifi_configure_low_power_mode(priv->card, pm,
                                               (req->wakeHost ? UNIFI_PERIODIC_WAKE_HOST_ENABLED : UNIFI_PERIODIC_WAKE_HOST_DISABLED));
}


void CsrWifiRouterCtrlWifiOnResHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
    unifi_priv_t *priv = (unifi_priv_t*)drvpriv;
    CsrWifiRouterCtrlWifiOnRes* res = (CsrWifiRouterCtrlWifiOnRes*)msg;

    if (priv == NULL) {
        unifi_error(NULL, "CsrWifiRouterCtrlWifiOnResHandler: Invalid ospriv.\n");
        return;
    }

    unifi_trace(priv, UDBG1,
                "CsrWifiRouterCtrlWifiOnResHandler: status %d (patch %u)\n", res->status, res->smeVersions.firmwarePatch);

    if (res->smeVersions.firmwarePatch != 0) {
        unifi_info(priv, "Firmware patch %d\n", res->smeVersions.firmwarePatch);
    }

    if (res->numInterfaceAddress > CSR_WIFI_NUM_INTERFACES) {
        unifi_error(priv, "WifiOnResHandler bad numInterfaceAddress %d\n", res->numInterfaceAddress);
        return;
    }

    /* UniFi is now initialised, complete the init. */
    if (res->status == CSR_RESULT_SUCCESS)
    {
        int i; /* used as a loop counter */
        u32 intmode = CSR_WIFI_INTMODE_DEFAULT;
#ifdef CSR_WIFI_SPLIT_PATCH
        u8 switching_ap_fw = FALSE;
#endif
        /* Register the UniFi device with the OS network manager */
        unifi_trace(priv, UDBG3, "Card Init Completed Successfully\n");

        /* Store the MAC address in the netdev */
        for(i=0;i<res->numInterfaceAddress;i++)
        {
            memcpy(priv->netdev[i]->dev_addr, res->stationMacAddress[i].a, ETH_ALEN);
        }

        /* Copy version structure into the private versions field */
        priv->sme_versions = res->smeVersions;

        unifi_trace(priv, UDBG2, "network interfaces count = %d\n",
                    res->numInterfaceAddress);

        /* Register the netdevs for each interface. */
        for(i=0;i<res->numInterfaceAddress;i++)
        {
            netInterface_priv_t *interfacePriv = priv->interfacePriv[i];
            if(!interfacePriv->netdev_registered)
            {
                int r;
                unifi_trace(priv, UDBG3, "registering net device %d\n", i);
                r = uf_register_netdev(priv, i);
                if (r)
                {
                    /* unregister the net_device that are registered in the previous iterations */
                    uf_unregister_netdev(priv);
                    unifi_error(priv, "Failed to register the network device.\n");
                    CsrWifiRouterCtrlWifiOnCfmSend(msg->source, res->clientData, CSR_RESULT_FAILURE);
                    return;
                }
            }
#ifdef CSR_WIFI_SPLIT_PATCH
            else
            {
                /* If a netdev is already registered, we have received this WifiOnRes
                 * in response to switching AP/STA firmware in a ModeSetReq.
                 * Rememeber this in order to send a ModeSetCfm once
                 */
                switching_ap_fw = TRUE;
            }
#endif
        }
        priv->totalInterfaceCount = res->numInterfaceAddress;

        /* If the MIB has selected f/w scheduled interrupt mode, apply it now
         * but let module param override.
         */
        if (run_bh_once != -1) {
            intmode = (u32)run_bh_once;
        } else if (res->scheduledInterrupt) {
            intmode = CSR_WIFI_INTMODE_RUN_BH_ONCE;
        }
        unifi_set_interrupt_mode(priv->card, intmode);

        priv->init_progress = UNIFI_INIT_COMPLETED;

        /* Acknowledge the CsrWifiRouterCtrlWifiOnReq now */
        CsrWifiRouterCtrlWifiOnCfmSend(msg->source, res->clientData, CSR_RESULT_SUCCESS);

#ifdef CSR_WIFI_SPLIT_PATCH
        if (switching_ap_fw && (priv->pending_mode_set.common.destination != 0xaaaa)) {
            unifi_info(priv, "Completed firmware reload with %s patch\n",
                CSR_WIFI_HIP_IS_AP_FW(priv->interfacePriv[0]->interfaceMode) ? "AP" : "STA");

            /* Confirm the ModeSetReq that requested the AP/STA patch switch */
            CsrWifiRouterCtrlModeSetCfmSend(priv->pending_mode_set.common.source,
                                            priv->pending_mode_set.clientData,
                                            priv->pending_mode_set.interfaceTag,
                                            priv->pending_mode_set.mode,
                                            CSR_RESULT_SUCCESS);
            priv->pending_mode_set.common.destination = 0xaaaa;
        }
#endif
        unifi_info(priv, "UniFi ready\n");

#ifdef ANDROID_BUILD
        /* Release the wakelock */
        unifi_trace(priv, UDBG1, "ready: release wake lock\n");
        wake_unlock(&unifi_sdio_wake_lock);
#endif
        /* Firmware initialisation is complete, so let the SDIO bus
         * clock be raised when convienent to the core.
         */
        unifi_request_max_sdio_clock(priv->card);

#ifdef CSR_SUPPORT_WEXT
        /* Notify the Android wpa_supplicant that we are ready */
        wext_send_started_event(priv);

        queue_work(priv->unifi_workqueue, &priv->sme_config_task);
#endif

    } else {
        /* Acknowledge the CsrWifiRouterCtrlWifiOnReq now */
        CsrWifiRouterCtrlWifiOnCfmSend(msg->source, res->clientData, CSR_RESULT_FAILURE);
    }
}


void CsrWifiRouterCtrlWifiOffResHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
}


void CsrWifiRouterCtrlMulticastAddressResHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
}


void CsrWifiRouterMaPacketSubscribeReqHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
    unifi_priv_t *priv = (unifi_priv_t*)drvpriv;
    CsrWifiRouterMaPacketSubscribeReq* req = (CsrWifiRouterMaPacketSubscribeReq*)msg;
    u8 i;
    CsrResult result;

    if (priv == NULL) {
        unifi_error(priv, "CsrWifiRouterMaPacketSubscribeReqHandler: invalid priv\n");
        return;
    }

    /* Look for an unused filter */

    result = CSR_WIFI_RESULT_NO_ROOM;
    for (i = 0; i < MAX_MA_UNIDATA_IND_FILTERS; i++) {

        if (!priv->sme_unidata_ind_filters[i].in_use) {

            priv->sme_unidata_ind_filters[i].in_use = 1;
            priv->sme_unidata_ind_filters[i].appHandle = msg->source;
            priv->sme_unidata_ind_filters[i].encapsulation = req->encapsulation;
            priv->sme_unidata_ind_filters[i].protocol = req->protocol;

            priv->sme_unidata_ind_filters[i].oui[2] = (u8)  (req->oui        & 0xFF);
            priv->sme_unidata_ind_filters[i].oui[1] = (u8) ((req->oui >>  8) & 0xFF);
            priv->sme_unidata_ind_filters[i].oui[0] = (u8) ((req->oui >> 16) & 0xFF);

            result = CSR_RESULT_SUCCESS;
            break;
        }
    }

    unifi_trace(priv, UDBG1,
                "subscribe_req: encap=%d, handle=%d, result=%d\n",
                req->encapsulation, i, result);
    CsrWifiRouterMaPacketSubscribeCfmSend(msg->source,req->interfaceTag, i, result, 0);
}


void CsrWifiRouterMaPacketUnsubscribeReqHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
    unifi_priv_t *priv = (unifi_priv_t*)drvpriv;
    CsrWifiRouterMaPacketUnsubscribeReq* req = (CsrWifiRouterMaPacketUnsubscribeReq*)msg;
    CsrResult result;

    if (priv == NULL) {
        unifi_error(priv, "CsrWifiRouterMaPacketUnsubscribeReqHandler: invalid priv\n");
        return;
    }

    result = CSR_WIFI_RESULT_NOT_FOUND;

    if (req->subscriptionHandle < MAX_MA_UNIDATA_IND_FILTERS) {
        if (priv->sme_unidata_ind_filters[req->subscriptionHandle].in_use) {
            priv->sme_unidata_ind_filters[req->subscriptionHandle].in_use = 0;
            result = CSR_RESULT_SUCCESS;
        } else {
            result = CSR_WIFI_RESULT_NOT_FOUND;
        }
    }

    unifi_trace(priv, UDBG1,
                "unsubscribe_req: handle=%d, result=%d\n",
                req->subscriptionHandle, result);
    CsrWifiRouterMaPacketUnsubscribeCfmSend(msg->source,req->interfaceTag, result);
}


void CsrWifiRouterCtrlCapabilitiesReqHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
    unifi_priv_t *priv = (unifi_priv_t*)drvpriv;
    CsrWifiRouterCtrlCapabilitiesReq* req = (CsrWifiRouterCtrlCapabilitiesReq*)msg;

    if (priv == NULL) {
        unifi_error(priv, "CsrWifiRouterCtrlCapabilitiesReqHandler: invalid priv\n");
        return;
    }

    CsrWifiRouterCtrlCapabilitiesCfmSend(msg->source,req->clientData,
            UNIFI_SOFT_COMMAND_Q_LENGTH - 1,
            UNIFI_SOFT_TRAFFIC_Q_LENGTH - 1);
}


void CsrWifiRouterCtrlSuspendResHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
    unifi_priv_t *priv = (unifi_priv_t*)drvpriv;
    CsrWifiRouterCtrlSuspendRes* res = (CsrWifiRouterCtrlSuspendRes*)msg;

    if (priv == NULL) {
        unifi_error(priv, "CsrWifiRouterCtrlSuspendResHandler: invalid priv\n");
        return;
    }

    sme_complete_request(priv, res->status);
}


void CsrWifiRouterCtrlResumeResHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
    unifi_priv_t *priv = (unifi_priv_t*)drvpriv;
    CsrWifiRouterCtrlResumeRes* res = (CsrWifiRouterCtrlResumeRes*)msg;

    if (priv == NULL) {
        unifi_error(priv, "CsrWifiRouterCtrlResumeResHandler: invalid priv\n");
        return;
    }

    sme_complete_request(priv, res->status);
}


void CsrWifiRouterCtrlTrafficConfigReqHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
    unifi_priv_t *priv = (unifi_priv_t*)drvpriv;
    CsrWifiRouterCtrlTrafficConfigReq* req = (CsrWifiRouterCtrlTrafficConfigReq*)msg;
    CsrResult csrResult;

    if (priv == NULL) {
        unifi_error(priv, "CsrWifiRouterCtrlTrafficConfigReqHandler: invalid smepriv\n");
        return;
    }
    if (req->trafficConfigType == CSR_WIFI_ROUTER_CTRL_TRAFFIC_CONFIG_TYPE_FILTER)
    {
        req->config.packetFilter |= CSR_WIFI_ROUTER_CTRL_TRAFFIC_PACKET_TYPE_CUSTOM;
    }
    csrResult = unifi_ta_configure(priv->card, req->trafficConfigType, (const CsrWifiRouterCtrlTrafficConfig *)&req->config);
}

void CsrWifiRouterCtrlTrafficClassificationReqHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
    unifi_priv_t *priv = (unifi_priv_t*)drvpriv;
    CsrWifiRouterCtrlTrafficClassificationReq* req = (CsrWifiRouterCtrlTrafficClassificationReq*)msg;

    if (priv == NULL) {
        unifi_error(priv, "CsrWifiRouterCtrlTrafficClassificationReqHandler: invalid smepriv\n");
        return;
    }

    unifi_ta_classification(priv->card, req->trafficType, req->period);
}

static int
_sys_packet_req(unifi_priv_t *priv, const CSR_SIGNAL *signal,
        u8 subscriptionHandle,
        u16 frameLength, u8 *frame,
        int proto)
{
    int r;
    const sme_ma_unidata_ind_filter_t *subs;
    bulk_data_param_t bulkdata;
    CSR_MA_PACKET_REQUEST req = signal->u.MaPacketRequest;
    struct sk_buff *skb, *newSkb = NULL;
    CsrWifiMacAddress peerMacAddress;
    CsrResult csrResult;
    u16 interfaceTag = req.VirtualInterfaceIdentifier & 0xff;
    u8 eapolStore = FALSE;
    s8 protection = 0;
    netInterface_priv_t *interfacePriv;
    unsigned long flags;

    if (interfaceTag >= CSR_WIFI_NUM_INTERFACES) {
        unifi_error(priv, "_sys_packet_req: interfaceID >= CSR_WIFI_NUM_INTERFACES.\n");
        return -EINVAL;
    }
    interfacePriv = priv->interfacePriv[interfaceTag];
    if (!priv->sme_unidata_ind_filters[subscriptionHandle].in_use) {
        unifi_error(priv, "_sys_packet_req: unknown subscription.\n");
        return -EINVAL;
    }

    subs = &priv->sme_unidata_ind_filters[subscriptionHandle];
    unifi_trace(priv, UDBG1,
                "_sys_packet_req: handle=%d, subs=%p, encap=%d\n",
                subscriptionHandle, subs, subs->encapsulation);

    csrResult = unifi_net_data_malloc(priv, &bulkdata.d[0], frameLength);
    if (csrResult != CSR_RESULT_SUCCESS) {
        unifi_error(priv, "_sys_packet_req: failed to allocate bulkdata.\n");
        return (int)CsrHipResultToStatus(csrResult);
    }

    /* get the peer Mac address */
    memcpy(&peerMacAddress, frame, ETH_ALEN);

    /* Determine if we need to add encapsulation header */
    if (subs->encapsulation == CSR_WIFI_ROUTER_ENCAPSULATION_ETHERNET) {
        memcpy((void*)bulkdata.d[0].os_data_ptr, frame, frameLength);

        /* The translation is performed on the skb */
        skb = (struct sk_buff*)bulkdata.d[0].os_net_buf_ptr;

        unifi_trace(priv, UDBG1,
                    "_sys_packet_req: skb_add_llc_snap -->\n");
        r = skb_add_llc_snap(priv->netdev[interfaceTag], skb, proto);
        unifi_trace(priv, UDBG1,
                    "_sys_packet_req: skb_add_llc_snap <--\n");
        if (r) {
            unifi_error(priv,
                        "_sys_packet_req: failed to translate eth frame.\n");
            unifi_net_data_free(priv,&bulkdata.d[0]);
            return r;
        }

        bulkdata.d[0].data_length = skb->len;
    } else {
        /* Crop the MAC addresses from the packet */
        memcpy((void*)bulkdata.d[0].os_data_ptr, frame + 2*ETH_ALEN, frameLength - 2*ETH_ALEN);
        bulkdata.d[0].data_length = frameLength - 2*ETH_ALEN;
        skb = (struct sk_buff*)bulkdata.d[0].os_net_buf_ptr;
        skb->len = bulkdata.d[0].data_length;

    }

    bulkdata.d[1].os_data_ptr = NULL;
    bulkdata.d[1].os_net_buf_ptr = NULL;
    bulkdata.d[1].data_length = 0;

    /* check for m4 detection */
    if (0 == uf_verify_m4(priv, bulkdata.d[0].os_data_ptr, bulkdata.d[0].data_length)) {
        eapolStore = TRUE;
    }

#ifdef CSR_WIFI_SECURITY_WAPI_ENABLE
    if (proto == ETH_P_WAI)
     {
        protection = 0; /*WAI packets always sent unencrypted*/
     }
   else
     {
#endif

#ifdef CSR_SUPPORT_SME
    if ((protection = uf_get_protection_bit_from_interfacemode(priv, interfaceTag, peerMacAddress.a)) < 0) {
        unifi_error(priv, "unicast address, but destination not in station record database\n");
        unifi_net_data_free(priv,&bulkdata.d[0]);
        return -1;
    }
#else
    protection = 0;
#endif

#ifdef CSR_WIFI_SECURITY_WAPI_ENABLE
    }
#endif

    /* add Mac header */
    if (prepare_and_add_macheader(priv, skb, newSkb, req.Priority, &bulkdata, interfaceTag, frame, frame + ETH_ALEN, protection)) {
        unifi_error(priv, "failed to create MAC header\n");
        unifi_net_data_free(priv,&bulkdata.d[0]);
        return -1;
    }

    if (eapolStore) {
        spin_lock_irqsave(&priv->m4_lock, flags);
        /* Store the EAPOL M4 packet for later */
        interfacePriv->m4_signal = *signal;
        interfacePriv->m4_bulk_data.net_buf_length = bulkdata.d[0].net_buf_length;
        interfacePriv->m4_bulk_data.data_length = bulkdata.d[0].data_length;
        interfacePriv->m4_bulk_data.os_data_ptr = bulkdata.d[0].os_data_ptr;
        interfacePriv->m4_bulk_data.os_net_buf_ptr = bulkdata.d[0].os_net_buf_ptr;
        spin_unlock_irqrestore(&priv->m4_lock, flags);
        /* Send a signal to SME */
        unifi_trace(priv, UDBG1, "_sys_packet_req: Sending CsrWifiRouterCtrlM4ReadyToSendInd\n");
        CsrWifiRouterCtrlM4ReadyToSendIndSend(priv->CSR_WIFI_SME_IFACEQUEUE, 0, interfaceTag, peerMacAddress);
        return 0;
    }

    /* Send the signal to UniFi */
      /* Set the B31 to 1 for local routing*/
    r= uf_process_ma_packet_req(priv,  peerMacAddress.a, (req.HostTag | 0x80000000), interfaceTag, 0,
                                (CSR_RATE)0, req.Priority, signal->SignalPrimitiveHeader.SenderProcessId, &bulkdata);
    if (r) {
        unifi_error(priv,
                    "_sys_packet_req: failed to send signal.\n");
        unifi_net_data_free(priv,&bulkdata.d[0]);
        return r;
    }
    /* The final CsrWifiRouterMaPacketCfmSend() will called when the actual MA-PACKET.cfm is received from the chip */

    return 0;
}

void CsrWifiRouterMaPacketReqHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
    int r;
    unifi_priv_t *priv = (unifi_priv_t*)drvpriv;
    CsrWifiRouterMaPacketReq* mareq = (CsrWifiRouterMaPacketReq*)msg;
    llc_snap_hdr_t *snap;
    u16 snap_protocol;
    CSR_SIGNAL signal;
    CSR_MA_PACKET_REQUEST *req = &signal.u.MaPacketRequest;
    CsrWifiRouterCtrlPortAction controlPortaction;
    u8 *daddr, *saddr;
    u16 interfaceTag = mareq->interfaceTag & 0x00ff;
    int queue;
    netInterface_priv_t *interfacePriv = priv->interfacePriv[interfaceTag];

    if (!mareq->frame || !priv || !priv->smepriv)
    {
        unifi_error(priv, "CsrWifiRouterMaPacketReqHandler: invalid frame/priv/priv->smepriv\n");
        return;
    }

    if (interfaceTag >= CSR_WIFI_NUM_INTERFACES) {
        unifi_error(priv, "CsrWifiRouterMaPacketReqHandler: interfaceID >= CSR_WIFI_NUM_INTERFACES.\n");
        return;
    }
    /* get a pointer to dest & source Mac address */
    daddr = mareq->frame;
    saddr = (mareq->frame + ETH_ALEN);
    /* point to the proper position of frame, since frame has MAC header */
    snap = (llc_snap_hdr_t *) (mareq->frame + 2 * ETH_ALEN);
    snap_protocol = ntohs(snap->protocol);
    if((snap_protocol == ETH_P_PAE)
#ifdef CSR_WIFI_SECURITY_WAPI_ENABLE
       || (snap_protocol == ETH_P_WAI)
#endif
    )
    {
        queue = UF_UNCONTROLLED_PORT_Q;
    }
    else
    {
        queue = UF_CONTROLLED_PORT_Q;
    }

    /* Controlled port restrictions apply to the packets */
    controlPortaction = uf_sme_port_state(priv, daddr, queue, interfaceTag);
    if (controlPortaction != CSR_WIFI_ROUTER_CTRL_PORT_ACTION_8021X_PORT_OPEN)
    {
        unifi_warning(priv, "CsrWifiRouterMaPacketReqHandler: (%s)controlled port is closed.\n", (queue == UF_CONTROLLED_PORT_Q)?"":"un");
        if(mareq->cfmRequested)
        {
            CsrWifiRouterMaPacketCfmSend(msg->source,
                                     interfaceTag,
                                     CSR_RESULT_FAILURE,
                                     mareq->hostTag, 0);
        }
        return;
    }

    signal.SignalPrimitiveHeader.SignalId = CSR_MA_PACKET_REQUEST_ID;
    /* Store the appHandle in the LSB of the SenderId. */
    CSR_COPY_UINT16_TO_LITTLE_ENDIAN(((priv->sme_cli->sender_id & 0xff00) | (unsigned int)msg->source),
                                     (u8*)&signal.SignalPrimitiveHeader.SenderProcessId);
    signal.SignalPrimitiveHeader.ReceiverProcessId = 0;

    /* Fill in the MA-PACKET.req signal */
    memcpy(req->Ra.x, daddr, ETH_ALEN);
    req->Priority = mareq->priority;
    req->TransmitRate = 0; /* Let firmware select the rate*/
    req->VirtualInterfaceIdentifier = uf_get_vif_identifier(interfacePriv->interfaceMode,interfaceTag);
    req->HostTag = mareq->hostTag;

    if(mareq->cfmRequested)
        req->TransmissionControl = 0;
    else
        req->TransmissionControl = CSR_NO_CONFIRM_REQUIRED;

    r = _sys_packet_req(priv, &signal, mareq->subscriptionHandle,
            mareq->frameLength, mareq->frame, snap_protocol);

    if (r && mareq->cfmRequested)
    {
        CsrWifiRouterMaPacketCfmSend(msg->source,interfaceTag,
                                     CSR_RESULT_FAILURE,
                                     mareq->hostTag, 0);
    }
    return;
}

void CsrWifiRouterMaPacketCancelReqHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
}

void CsrWifiRouterCtrlM4TransmitReqHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
    unifi_priv_t *priv = (unifi_priv_t*)drvpriv;
    CsrWifiRouterCtrlM4TransmitReq* req = (CsrWifiRouterCtrlM4TransmitReq*)msg;
    int r;
    bulk_data_param_t bulkdata;
    netInterface_priv_t *interfacePriv;
    CSR_SIGNAL m4_signal;
    unsigned long flags;

    if (priv == NULL) {
        unifi_error(priv, "CsrWifiRouterCtrlM4TransmitReqHandler: invalid smepriv\n");
        return;
    }
    if (req->interfaceTag >= CSR_WIFI_NUM_INTERFACES) {
        unifi_error(priv, "M4TransmitReqHandler: interfaceTag >= CSR_WIFI_NUM_INTERFACES\n");
        return;
    }

    interfacePriv = priv->interfacePriv[req->interfaceTag];
    spin_lock_irqsave(&priv->m4_lock, flags);
    if (interfacePriv->m4_bulk_data.data_length == 0) {
        spin_unlock_irqrestore(&priv->m4_lock, flags);
        unifi_error(priv, "CsrWifiRouterCtrlM4TransmitReqHandler: invalid buffer\n");
        return;
    }

    memcpy(&bulkdata.d[0], &interfacePriv->m4_bulk_data, sizeof(bulk_data_desc_t));

    interfacePriv->m4_bulk_data.net_buf_length = 0;
    interfacePriv->m4_bulk_data.data_length = 0;
    interfacePriv->m4_bulk_data.os_data_ptr = interfacePriv->m4_bulk_data.os_net_buf_ptr = NULL;
    m4_signal = interfacePriv->m4_signal;
    spin_unlock_irqrestore(&priv->m4_lock, flags);

    bulkdata.d[1].os_data_ptr = NULL;
    bulkdata.d[1].data_length = 0;

    interfacePriv->m4_sent = TRUE;
    m4_signal.u.MaPacketRequest.HostTag |= 0x80000000;
    /* Store the hostTag for later varification */
    interfacePriv->m4_hostTag = m4_signal.u.MaPacketRequest.HostTag;
    r = ul_send_signal_unpacked(priv, &m4_signal, &bulkdata);
    unifi_trace(priv, UDBG1,
                "CsrWifiRouterCtrlM4TransmitReqHandler: sent\n");
    if (r) {
        unifi_error(priv,
                    "CsrWifiRouterCtrlM4TransmitReqHandler: failed to send signal.\n");
        unifi_net_data_free(priv, &bulkdata.d[0]);
    }
}

/* reset the station records when the mode is set as CSR_WIFI_ROUTER_CTRL_MODE_NONE */
static void CsrWifiRouterCtrlResetStationRecordList(unifi_priv_t *priv, u16 interfaceTag)
{
    u8 i,j;
    CsrWifiRouterCtrlStaInfo_t *staInfo=NULL;
    netInterface_priv_t *interfacePriv = priv->interfacePriv[interfaceTag];
    unsigned long lock_flags;

    /* create a list for sending confirms of un-delivered packets */
    struct list_head send_cfm_list;

    if (interfaceTag >= CSR_WIFI_NUM_INTERFACES) {
        unifi_error(priv, "CsrWifiRouterCtrlResetStationRecordList: bad interfaceTag\n");
        return;
    }

    INIT_LIST_HEAD(&send_cfm_list);

    /* Reset the station record to NULL if mode is NONE */
    for(i = 0; i < UNIFI_MAX_CONNECTIONS; i++) {
        if ((staInfo=interfacePriv->staInfo[i]) != NULL) {
            uf_prepare_send_cfm_list_for_queued_pkts(priv,
                                                 &send_cfm_list,
                                                 &(staInfo->mgtFrames));
            uf_flush_list(priv,&(staInfo->mgtFrames));
            for(j=0;j<MAX_ACCESS_CATOGORY;j++){
                uf_prepare_send_cfm_list_for_queued_pkts(priv,
                                                     &send_cfm_list,
                                                     &(staInfo->dataPdu[j]));
                uf_flush_list(priv,&(staInfo->dataPdu[j]));
            }

            spin_lock_irqsave(&priv->staRecord_lock,lock_flags);
            /* Removing station record information from port config array */
            memset(staInfo->peerControlledPort, 0, sizeof(unifi_port_cfg_t));
            staInfo->peerControlledPort->port_action = CSR_WIFI_ROUTER_CTRL_PORT_ACTION_8021X_PORT_CLOSED_DISCARD;
            staInfo->peerControlledPort->in_use = FALSE;
            interfacePriv->controlled_data_port.entries_in_use--;

            memset(staInfo->peerUnControlledPort, 0, sizeof(unifi_port_cfg_t));
            staInfo->peerUnControlledPort->port_action = CSR_WIFI_ROUTER_CTRL_PORT_ACTION_8021X_PORT_CLOSED_DISCARD;
            staInfo->peerUnControlledPort->in_use = FALSE;
            interfacePriv->uncontrolled_data_port.entries_in_use--;

            kfree(interfacePriv->staInfo[i]);
            interfacePriv->staInfo[i] = NULL;
            spin_unlock_irqrestore(&priv->staRecord_lock,lock_flags);
        }
    }
    /* after the critical region process the list of frames that requested cfm
     * and send cfm to requestor one by one
     */
    send_auto_ma_packet_confirm(priv, interfacePriv, &send_cfm_list);

#ifdef CSR_SUPPORT_SME
    /* Interface Independent, no of packet queued, incase of mode is None or AP set to 0 */
    switch(interfacePriv->interfaceMode)
    {
        case CSR_WIFI_ROUTER_CTRL_MODE_AP:
        case CSR_WIFI_ROUTER_CTRL_MODE_P2PGO:
        case CSR_WIFI_ROUTER_CTRL_MODE_NONE:
            if (priv->noOfPktQueuedInDriver) {
                unifi_warning(priv, "After reset the noOfPktQueuedInDriver = %x\n", priv->noOfPktQueuedInDriver);
                spin_lock_irqsave(&priv->tx_q_lock,lock_flags);
                priv->noOfPktQueuedInDriver = 0;
                spin_unlock_irqrestore(&priv->tx_q_lock,lock_flags);
            }
            break;
        case CSR_WIFI_ROUTER_CTRL_MODE_IBSS:
            break;
        default:
            unifi_error(priv, "interfacemode is not correct in CsrWifiRouterCtrlResetStationRecordList: debug\n");
    }
#endif

    if (((interfacePriv->controlled_data_port.entries_in_use != 0) || (interfacePriv->uncontrolled_data_port.entries_in_use != 0))
            && (interfacePriv->interfaceMode == CSR_WIFI_ROUTER_CTRL_MODE_NONE)) {
        /* Print in case if the value of entries goes to -ve/+ve (apart from 0)
         * we expect the entries should be zero here if mode is set as NONE
         */
        unifi_trace(priv, UDBG3, "In %s controlled port entries = %d, uncontrolled port entries = %d\n",
                   __FUNCTION__, interfacePriv->controlled_data_port.entries_in_use,
                   interfacePriv->uncontrolled_data_port.entries_in_use);
    }
}

void CsrWifiRouterCtrlInterfaceReset(unifi_priv_t *priv, u16 interfaceTag)
{
    netInterface_priv_t *interfacePriv;

    /* create a list for sending confirms of un-delivered packets */
    struct list_head send_cfm_list;

    if (interfaceTag >= CSR_WIFI_NUM_INTERFACES) {
        unifi_error(priv, "CsrWifiRouterCtrlInterfaceReset: bad interfaceTag\n");
        return;
    }

    interfacePriv = priv->interfacePriv[interfaceTag];

    INIT_LIST_HEAD(&send_cfm_list);

    /* Enable all queues by default */
    interfacePriv->queueEnabled[0] = 1;
    interfacePriv->queueEnabled[1] = 1;
    interfacePriv->queueEnabled[2] = 1;
    interfacePriv->queueEnabled[3] = 1;

    uf_prepare_send_cfm_list_for_queued_pkts(priv,
                                             &send_cfm_list,
                                             &(interfacePriv->genericMgtFrames));
    uf_flush_list(priv,&(interfacePriv->genericMgtFrames));

    uf_prepare_send_cfm_list_for_queued_pkts(priv,
                                             &send_cfm_list,
                                             &(interfacePriv->genericMulticastOrBroadCastMgtFrames));
    uf_flush_list(priv,&(interfacePriv->genericMulticastOrBroadCastMgtFrames));

    uf_prepare_send_cfm_list_for_queued_pkts(priv,
                                             &send_cfm_list,
                                             &(interfacePriv->genericMulticastOrBroadCastFrames));

    uf_flush_list(priv,&(interfacePriv->genericMulticastOrBroadCastFrames));

    /*  process the list of frames that requested cfm
    and send cfm to requestor one by one */
    send_auto_ma_packet_confirm(priv, interfacePriv, &send_cfm_list);

    /* Reset the station record to NULL if mode is tried to set as NONE */
    switch(interfacePriv->interfaceMode)
    {
        case CSR_WIFI_ROUTER_CTRL_MODE_STA:
        case CSR_WIFI_ROUTER_CTRL_MODE_P2PCLI:
        case CSR_WIFI_ROUTER_CTRL_MODE_MONITOR:
        case CSR_WIFI_ROUTER_CTRL_MODE_AMP:
            /* station records not available in these modes */
            break;
        default:
            CsrWifiRouterCtrlResetStationRecordList(priv,interfaceTag);
    }

    interfacePriv->num_stations_joined = 0;
    interfacePriv->sta_activity_check_enabled = FALSE;
}


void CsrWifiRouterCtrlModeSetReqHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
    unifi_priv_t *priv = (unifi_priv_t*)drvpriv;
    CsrWifiRouterCtrlModeSetReq* req = (CsrWifiRouterCtrlModeSetReq*)msg;

    if (priv == NULL)
    {
        unifi_error(priv, "CsrWifiRouterCtrlModeSetReqHandler: invalid smepriv\n");
        return;
    }

    if (req->interfaceTag < CSR_WIFI_NUM_INTERFACES)
    {
        netInterface_priv_t *interfacePriv = priv->interfacePriv[req->interfaceTag];
#ifdef CSR_WIFI_SPLIT_PATCH
        u8 old_mode = interfacePriv->interfaceMode;
#endif
        unifi_trace(priv, UDBG1, "CsrWifiRouterCtrlModeSetReqHandler: interfacePriv->interfaceMode = %d\n",
                interfacePriv->interfaceMode);

        interfacePriv->interfaceMode = req->mode;

#ifdef CSR_WIFI_SPLIT_PATCH
        /* Detect a change in mode that requires a switch to/from the AP firmware patch.
         * This should only happen when transitioning in/out of AP modes.
         */
        if (CSR_WIFI_HIP_IS_AP_FW(req->mode) != CSR_WIFI_HIP_IS_AP_FW(old_mode))
        {
            CsrWifiRouterCtrlVersions versions;
            int r;

#ifdef ANDROID_BUILD
            /* Take the wakelock while switching patch */
            unifi_trace(priv, UDBG1, "patch switch: take wake lock\n");
            wake_lock(&unifi_sdio_wake_lock);
#endif
            unifi_info(priv, "Resetting UniFi with %s patch\n", CSR_WIFI_HIP_IS_AP_FW(req->mode) ? "AP" : "STA");

            r = uf_request_firmware_files(priv, UNIFI_FW_STA);
            if (r) {
                unifi_error(priv, "CsrWifiRouterCtrlModeSetReqHandler: Failed to get f/w\n");
                CsrWifiRouterCtrlModeSetCfmSend(msg->source, req->clientData, req->interfaceTag,
                                                req->mode, CSR_RESULT_FAILURE);
                return;
            }

            /* Block the I/O thread */
            priv->bh_thread.block_thread = 1;

            /* Reset and download the new patch */
            r = uf_init_hw(priv);
            if (r) {
                unifi_error(priv, "CsrWifiRouterCtrlWifiOnReqHandler: Failed to initialise h/w, error %d\n", r);
                CsrWifiRouterCtrlModeSetCfmSend(msg->source, req->clientData, req->interfaceTag,
                                                req->mode, CSR_RESULT_FAILURE);
                return;
            }

            /* Re-enable the I/O thread */
            priv->bh_thread.block_thread = 0;

            /* Get the version information from the core */
            unifi_card_info(priv->card, &priv->card_info);

            /* Copy to the unifiio_card_info structure. */
            versions.chipId = priv->card_info.chip_id;
            versions.chipVersion = priv->card_info.chip_version;
            versions.firmwareBuild = priv->card_info.fw_build;
            versions.firmwareHip = priv->card_info.fw_hip_version;
            versions.routerBuild = (char*)CSR_WIFI_VERSION;
            versions.routerHip = (UNIFI_HIP_MAJOR_VERSION << 8) | UNIFI_HIP_MINOR_VERSION;

            /* Now that new firmware is running, send a WifiOnInd to the NME. This will
             * cause it to retransfer the MIB.
             */
            CsrWifiRouterCtrlWifiOnIndSend(msg->source, 0, CSR_RESULT_SUCCESS, versions);

            /* Store the request so we know where to send the ModeSetCfm */
            priv->pending_mode_set = *req;
        }
        else
#endif
        {
            /* No patch switch, confirm straightaway */
            CsrWifiRouterCtrlModeSetCfmSend(msg->source, req->clientData, req->interfaceTag,
                                            req->mode, CSR_RESULT_SUCCESS);
        }

        interfacePriv->bssid = req->bssid;
        /* For modes other than AP/P2PGO, set below member FALSE */
        interfacePriv->intraBssEnabled = FALSE;
        /* Initialise the variable bcTimSet with a value
         * other then CSR_WIFI_TIM_SET or CSR_WIFI_TIM_RESET value
         */
        interfacePriv->bcTimSet = 0xFF;
        interfacePriv->bcTimSetReqPendingFlag = FALSE;
        /* Initialise the variable bcTimSetReqQueued with a value
         * other then CSR_WIFI_TIM_SET or CSR_WIFI_TIM_RESET value
         */
        interfacePriv->bcTimSetReqQueued =0xFF;
        CsrWifiRouterCtrlInterfaceReset(priv,req->interfaceTag);

        if(req->mode == CSR_WIFI_ROUTER_CTRL_MODE_AP ||
           req->mode == CSR_WIFI_ROUTER_CTRL_MODE_P2PGO) {
            interfacePriv->protect = req->protection;
            interfacePriv->dtimActive=FALSE;
            interfacePriv->multicastPduHostTag = 0xffffffff;
            /* For AP/P2PGO mode SME sending intraBssDistEnabled
             * i.e. for AP: intraBssDistEnabled = TRUE, for P2PGO
             * intraBssDistEnabled = TRUE/FALSE on requirement
             */
            interfacePriv->intraBssEnabled = req->intraBssDistEnabled;
            unifi_trace(priv, UDBG3, "CsrWifiRouterCtrlModeSetReqHandler: IntraBssDisEnabled = %d\n",
                        req->intraBssDistEnabled);
        } else if (req->mode == CSR_WIFI_ROUTER_CTRL_MODE_NONE) {
              netif_carrier_off(priv->netdev[req->interfaceTag]);
              interfacePriv->connected = UnifiConnectedUnknown;
        }
    }
    else {
        unifi_error(priv, "CsrWifiRouterCtrlModeSetReqHandler: invalid interfaceTag :%d\n",req->interfaceTag);
    }
}

void CsrWifiRouterMaPacketResHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
}

/* delete the station record from the station record data base */
static int peer_delete_record(unifi_priv_t *priv, CsrWifiRouterCtrlPeerDelReq *req)
{
    u8 j;
    CsrWifiRouterCtrlStaInfo_t *staInfo = NULL;
    unifi_port_config_t *controlledPort;
    unifi_port_config_t *unControlledPort;
    netInterface_priv_t *interfacePriv;

    u8 ba_session_idx = 0;
    ba_session_rx_struct *ba_session_rx = NULL;
    ba_session_tx_struct *ba_session_tx = NULL;

    /* create a list for sending confirms of un-delivered packets */
    struct list_head send_cfm_list;

    unsigned long lock_flags;

    if ((req->peerRecordHandle >= UNIFI_MAX_CONNECTIONS) || (req->interfaceTag >= CSR_WIFI_NUM_INTERFACES)) {
        unifi_error(priv, "handle/interfaceTag is not proper, handle = %d, interfaceTag = %d\n", req->peerRecordHandle, req->interfaceTag);
        return CSR_RESULT_FAILURE;
    }

    INIT_LIST_HEAD(&send_cfm_list);

    interfacePriv = priv->interfacePriv[req->interfaceTag];
    /* remove the station record & make it NULL */
    if ((staInfo=interfacePriv->staInfo[req->peerRecordHandle])!=NULL) {

        uf_prepare_send_cfm_list_for_queued_pkts(priv,
                                                 &send_cfm_list,
                                                 &(staInfo->mgtFrames));

        uf_flush_list(priv,&(staInfo->mgtFrames));
        for(j=0;j<MAX_ACCESS_CATOGORY;j++){
            uf_prepare_send_cfm_list_for_queued_pkts(priv,
                                                     &send_cfm_list,
                                                     &(staInfo->dataPdu[j]));
            uf_flush_list(priv,&(staInfo->dataPdu[j]));
        }

        spin_lock_irqsave(&priv->staRecord_lock,lock_flags);
        /* clear the port configure array info, for the corresponding peer entry */
        controlledPort = &interfacePriv->controlled_data_port;
        unControlledPort = &interfacePriv->uncontrolled_data_port;

        unifi_trace(priv, UDBG1, "peer_delete_record: Peer found handle = %d, port in use: cont(%d), unCont(%d)\n",
                    req->peerRecordHandle, controlledPort->entries_in_use, unControlledPort->entries_in_use);

        memset(staInfo->peerControlledPort, 0, sizeof(unifi_port_cfg_t));
        staInfo->peerControlledPort->port_action = CSR_WIFI_ROUTER_CTRL_PORT_ACTION_8021X_PORT_CLOSED_DISCARD;
        staInfo->peerControlledPort->in_use = FALSE;
        if (controlledPort->entries_in_use) {
            controlledPort->entries_in_use--;
        } else {
            unifi_warning(priv, "number of controlled port entries is zero, trying to decrement: debug\n");
        }

        memset(staInfo->peerUnControlledPort, 0, sizeof(unifi_port_cfg_t));
        staInfo->peerUnControlledPort->port_action = CSR_WIFI_ROUTER_CTRL_PORT_ACTION_8021X_PORT_CLOSED_DISCARD;
        staInfo->peerUnControlledPort->in_use = FALSE;
        if (unControlledPort->entries_in_use) {
            unControlledPort->entries_in_use--;
        } else {
            unifi_warning(priv, "number of uncontrolled port entries is zero, trying to decrement: debug\n");
        }

        spin_unlock_irqrestore(&priv->staRecord_lock,lock_flags);
        /* update the TIM with zero */
        if (interfacePriv->interfaceMode != CSR_WIFI_ROUTER_CTRL_MODE_IBSS &&
                staInfo->timSet == CSR_WIFI_TIM_SET) {
            unifi_trace(priv, UDBG3, "peer is deleted so TIM updated to 0, in firmware\n");
            update_tim(priv,staInfo->aid,0,req->interfaceTag, req->peerRecordHandle);
        }


        /* Stop BA session if it is active, for this peer address all BA sessions
        (per tID per role) are closed */

        down(&priv->ba_mutex);
        for(ba_session_idx=0; ba_session_idx < MAX_SUPPORTED_BA_SESSIONS_RX; ba_session_idx++){
            ba_session_rx = priv->interfacePriv[req->interfaceTag]->ba_session_rx[ba_session_idx];
            if(ba_session_rx) {
                if(!memcmp(ba_session_rx->macAddress.a, staInfo->peerMacAddress.a, ETH_ALEN)){
                    blockack_session_stop(priv,
                                        req->interfaceTag,
                                        CSR_WIFI_ROUTER_CTRL_BLOCK_ACK_RECIPIENT,
                                        ba_session_rx->tID,
                                        ba_session_rx->macAddress);
                }
            }
        }

        for(ba_session_idx=0; ba_session_idx < MAX_SUPPORTED_BA_SESSIONS_TX; ba_session_idx++){
            ba_session_tx = priv->interfacePriv[req->interfaceTag]->ba_session_tx[ba_session_idx];
            if(ba_session_tx) {
                if(!memcmp(ba_session_tx->macAddress.a, staInfo->peerMacAddress.a, ETH_ALEN)){
                    blockack_session_stop(priv,
                                        req->interfaceTag,
                                        CSR_WIFI_ROUTER_CTRL_BLOCK_ACK_ORIGINATOR,
                                        ba_session_tx->tID,
                                        ba_session_tx->macAddress);
                }
            }
        }

        up(&priv->ba_mutex);

#ifdef CSR_SUPPORT_SME
        unifi_trace(priv, UDBG1, "Canceling work queue for STA with AID: %d\n", staInfo->aid);
        cancel_work_sync(&staInfo->send_disconnected_ind_task);
#endif

        spin_lock_irqsave(&priv->staRecord_lock,lock_flags);
#ifdef CSR_SUPPORT_SME
        interfacePriv->num_stations_joined--;

        staInfo->nullDataHostTag = INVALID_HOST_TAG;

        if ((interfacePriv->sta_activity_check_enabled) &&
            (interfacePriv->num_stations_joined < STA_INACTIVE_DETECTION_TRIGGER_THRESHOLD))
        {
            unifi_trace(priv, UDBG1, "STOPPING the Inactivity Timer (num of stations = %d)\n", interfacePriv->num_stations_joined);
            interfacePriv->sta_activity_check_enabled = FALSE;
            del_timer_sync(&interfacePriv->sta_activity_check_timer);
        }
#endif

        /* Free the station record for corresponding peer */
        kfree(interfacePriv->staInfo[req->peerRecordHandle]);
        interfacePriv->staInfo[req->peerRecordHandle] = NULL;
        spin_unlock_irqrestore(&priv->staRecord_lock,lock_flags);

        /* after the critical region process the list of frames that requested cfm
        and send cfm to requestor one by one */
        send_auto_ma_packet_confirm(priv, interfacePriv, &send_cfm_list);


    }
    else
    {
        unifi_trace(priv, UDBG3, " peer not found: Delete request Peer handle[%d]\n", req->peerRecordHandle);
    }

    return CSR_RESULT_SUCCESS;
}

void CsrWifiRouterCtrlPeerDelReqHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
    CsrWifiRouterCtrlPeerDelReq* req = (CsrWifiRouterCtrlPeerDelReq*)msg;
    CsrResult status = CSR_RESULT_SUCCESS;
    unifi_priv_t *priv = (unifi_priv_t*)drvpriv;
    netInterface_priv_t *interfacePriv = priv->interfacePriv[req->interfaceTag];

    unifi_trace(priv, UDBG2, "entering CsrWifiRouterCtrlPeerDelReqHandler \n");
    if (priv == NULL)
    {
        unifi_error(priv, "CsrWifiRouterCtrlPeerDelReqHandler: invalid smepriv\n");
        return;
    }

    if (req->interfaceTag >= CSR_WIFI_NUM_INTERFACES)
    {
        unifi_error(priv, "CsrWifiRouterCtrlPeerDelReqHandler: bad interfaceTag\n");
        return;
    }

    switch(interfacePriv->interfaceMode)
    {
        case CSR_WIFI_ROUTER_CTRL_MODE_AP:
        case CSR_WIFI_ROUTER_CTRL_MODE_IBSS:
        case CSR_WIFI_ROUTER_CTRL_MODE_P2PGO:
            /* remove the station from station record data base */
            status = peer_delete_record(priv, req);
            break;
        case CSR_WIFI_ROUTER_CTRL_MODE_STA:
        case CSR_WIFI_ROUTER_CTRL_MODE_P2PCLI:
        default:
            /* No station record to maintain in these modes */
            break;
    }

    CsrWifiRouterCtrlPeerDelCfmSend(msg->source,req->clientData,req->interfaceTag,status);
    unifi_trace(priv, UDBG2, "leaving CsrWifiRouterCtrlPeerDelReqHandler \n");
}

/* Add the new station to the station record data base */
static int peer_add_new_record(unifi_priv_t *priv,CsrWifiRouterCtrlPeerAddReq *req,u32 *handle)
{
    u8 i, powerModeTemp = 0;
    u8 freeSlotFound = FALSE;
    CsrWifiRouterCtrlStaInfo_t *newRecord = NULL;
    netInterface_priv_t *interfacePriv = priv->interfacePriv[req->interfaceTag];
    CsrTime currentTime, currentTimeHi;
    unsigned long lock_flags;

    if (req->interfaceTag >= CSR_WIFI_NUM_INTERFACES) {
        unifi_error(priv, "peer_add_new_record: bad interfaceTag\n");
        return CSR_RESULT_FAILURE;
    }

    currentTime = CsrTimeGet(&currentTimeHi);

    for(i = 0; i < UNIFI_MAX_CONNECTIONS; i++) {
        if(interfacePriv->staInfo[i] == NULL) {
            /* Slot is empty, so can be used for station record */
            freeSlotFound = TRUE;
            *handle = i;

            /* Allocate for the new station record , to avoid race condition would happen between ADD_PEER &
             * DEL_PEER the allocation made atomic memory rather than kernel memory
             */
            newRecord = (CsrWifiRouterCtrlStaInfo_t *) kmalloc(sizeof(CsrWifiRouterCtrlStaInfo_t), GFP_ATOMIC);
            if (!newRecord) {
                unifi_error(priv, "failed to allocate the %d bytes of mem for station record\n",
                            sizeof(CsrWifiRouterCtrlStaInfo_t));
                return CSR_RESULT_FAILURE;
            }

            unifi_trace(priv, UDBG1, "peer_add_new_record: handle = %d AID = %d addr = %x:%x:%x:%x:%x:%x LI=%u\n",
                        *handle, req->associationId, req->peerMacAddress.a[0], req->peerMacAddress.a[1], req->peerMacAddress.a[2],
                        req->peerMacAddress.a[3], req->peerMacAddress.a[4], req->peerMacAddress.a[5],
                        req->staInfo.listenIntervalInTus);

            /* disable the preemption until station record updated */
            spin_lock_irqsave(&priv->staRecord_lock,lock_flags);

            interfacePriv->staInfo[i] = newRecord;
            /* Initialize the record*/
            memset(newRecord,0,sizeof(CsrWifiRouterCtrlStaInfo_t));
            /* update the station record */
            memcpy(newRecord->peerMacAddress.a, req->peerMacAddress.a, ETH_ALEN);
            newRecord->wmmOrQosEnabled = req->staInfo.wmmOrQosEnabled;

            /* maxSpLength is bit map in qosInfo field, so converting accordingly */
            newRecord->maxSpLength = req->staInfo.maxSpLength * 2;

            /*Max SP 0 mean any number of packets. since we buffer only 512
            packets we are hard coding this to zero for the moment */

            if(newRecord->maxSpLength == 0)
                newRecord->maxSpLength=512;

            newRecord->assignedHandle = i;

             /* copy power save mode of all access catagory (Trigger/Delivery/both enabled/disabled) */
            powerModeTemp = (u8) ((req->staInfo.powersaveMode >> 4) & 0xff);

            if(!(req->staInfo.powersaveMode & 0x0001))
                newRecord->powersaveMode[UNIFI_TRAFFIC_Q_BK]= CSR_WIFI_AC_LEGACY_POWER_SAVE;
            else
               newRecord->powersaveMode[UNIFI_TRAFFIC_Q_BK]= powerModeTemp & 0x03;

            if(!(req->staInfo.powersaveMode & 0x0002))
                newRecord->powersaveMode[UNIFI_TRAFFIC_Q_BE]= CSR_WIFI_AC_LEGACY_POWER_SAVE;
            else
               newRecord->powersaveMode[UNIFI_TRAFFIC_Q_BE]= ((powerModeTemp & 0x0C)>> 2);

            if(!(req->staInfo.powersaveMode & 0x0004))
                newRecord->powersaveMode[UNIFI_TRAFFIC_Q_VI]= CSR_WIFI_AC_LEGACY_POWER_SAVE;
            else
               newRecord->powersaveMode[UNIFI_TRAFFIC_Q_VI]= ((powerModeTemp & 0x30)>> 4);

            if(!(req->staInfo.powersaveMode & 0x0008))
                newRecord->powersaveMode[UNIFI_TRAFFIC_Q_VO]= CSR_WIFI_AC_LEGACY_POWER_SAVE;
            else
               newRecord->powersaveMode[UNIFI_TRAFFIC_Q_VO]= ((powerModeTemp & 0xC0)>> 6);

            {
                u8 k;
                for(k=0; k< MAX_ACCESS_CATOGORY ;k++)
                    unifi_trace(priv, UDBG2, "peer_add_new_record: WMM : %d ,AC %d, powersaveMode %x \n",
                            req->staInfo.wmmOrQosEnabled,k,newRecord->powersaveMode[k]);
            }

            unifi_trace(priv, UDBG3, "newRecord->wmmOrQosEnabled : %d , MAX SP : %d\n",
                    newRecord->wmmOrQosEnabled,newRecord->maxSpLength);

            /* Initialize the mgtFrames & data Pdu list */
            {
                u8 j;
                INIT_LIST_HEAD(&newRecord->mgtFrames);
                for(j = 0; j < MAX_ACCESS_CATOGORY; j++) {
                    INIT_LIST_HEAD(&newRecord->dataPdu[j]);
                }
            }

            newRecord->lastActivity = currentTime;
            newRecord->activity_flag = TRUE;

            /* enable the preemption as station record updated */
            spin_unlock_irqrestore(&priv->staRecord_lock,lock_flags);

            /* First time port actions are set for the peer with below information */
            configure_data_port(priv, CSR_WIFI_ROUTER_CTRL_PORT_ACTION_8021X_PORT_OPEN, &newRecord->peerMacAddress,
                                UF_UNCONTROLLED_PORT_Q, req->interfaceTag);

            if (interfacePriv->interfaceMode == CSR_WIFI_ROUTER_CTRL_MODE_IBSS) {
                configure_data_port(priv, CSR_WIFI_ROUTER_CTRL_PORT_ACTION_8021X_PORT_OPEN, &newRecord->peerMacAddress,
                                    UF_CONTROLLED_PORT_Q, req->interfaceTag);
            } else {
                configure_data_port(priv, CSR_WIFI_ROUTER_CTRL_PORT_ACTION_8021X_PORT_CLOSED_DISCARD, &newRecord->peerMacAddress,
                                    UF_CONTROLLED_PORT_Q, req->interfaceTag);
            }


            spin_lock_irqsave(&priv->staRecord_lock,lock_flags);
            /* Port status must be already set before calling the Add Peer request */
            newRecord->peerControlledPort = uf_sme_port_config_handle(priv, newRecord->peerMacAddress.a,
                                                                      UF_CONTROLLED_PORT_Q, req->interfaceTag);
            newRecord->peerUnControlledPort = uf_sme_port_config_handle(priv, newRecord->peerMacAddress.a,
                                                                        UF_UNCONTROLLED_PORT_Q, req->interfaceTag);

            if (!newRecord->peerControlledPort || !newRecord->peerUnControlledPort) {
                /* enable the preemption as station record failed to update */
                unifi_warning(priv, "Un/ControlledPort record not found in port configuration array index = %d\n", i);
                kfree(interfacePriv->staInfo[i]);
                interfacePriv->staInfo[i] = NULL;
                spin_unlock_irqrestore(&priv->staRecord_lock,lock_flags);
                return CSR_RESULT_FAILURE;
            }

            newRecord->currentPeerState = CSR_WIFI_ROUTER_CTRL_PEER_CONNECTED_ACTIVE;

            /* changes done during block ack handling */
            newRecord->txSuspend = FALSE;

            /*U-APSD related data structure*/
            newRecord->timRequestPendingFlag = FALSE;

            /* Initialise the variable updateTimReqQueued with a value
             * other then CSR_WIFI_TIM_SET or CSR_WIFI_TIM_RESET value
             */
            newRecord->updateTimReqQueued = 0xFF;
            newRecord->timSet = CSR_WIFI_TIM_RESET;
            newRecord->uapsdActive = FALSE;
            newRecord->noOfSpFramesSent =0;
            newRecord->triggerFramePriority = CSR_QOS_UP0;

            /* The protection bit is updated once the port opens for corresponding peer in
             * routerPortConfigure request */

            /* update the association ID */
            newRecord->aid = req->associationId;

#ifdef CSR_SUPPORT_SME
            interfacePriv->num_stations_joined++;
            newRecord->interfacePriv = interfacePriv;
            newRecord->listenIntervalInTus = req->staInfo.listenIntervalInTus;
            newRecord->nullDataHostTag = INVALID_HOST_TAG;

            INIT_WORK(&newRecord->send_disconnected_ind_task, uf_send_disconnected_ind_wq);

            if(!(interfacePriv->sta_activity_check_enabled) &&
               (interfacePriv->num_stations_joined >= STA_INACTIVE_DETECTION_TRIGGER_THRESHOLD)){
                unifi_trace(priv, UDBG1,
                            "peer_add_new_record: STARTING the Inactivity Timer (num of stations = %d)",
                            interfacePriv->num_stations_joined);

                interfacePriv->sta_activity_check_enabled = TRUE;
                interfacePriv->sta_activity_check_timer.function = check_inactivity_timer_expire_func;
                interfacePriv->sta_activity_check_timer.data = (unsigned long)interfacePriv;

                init_timer(&interfacePriv->sta_activity_check_timer);
                mod_timer(&interfacePriv->sta_activity_check_timer,
                          (jiffies + usecs_to_jiffies(STA_INACTIVE_DETECTION_TIMER_INTERVAL * 1000 * 1000)));

            }
#endif
            spin_unlock_irqrestore(&priv->staRecord_lock,lock_flags);
            break;
        }
    }

    if(!freeSlotFound) {
        unifi_error(priv, "Limited connectivity, Free slot not found for station record addition\n");
        return CSR_RESULT_FAILURE;
    }
    return CSR_RESULT_SUCCESS;
}

#ifdef CSR_SUPPORT_SME
static void check_inactivity_timer_expire_func(unsigned long data)
{
    struct unifi_priv *priv;
    CsrWifiRouterCtrlStaInfo_t *sta_record = NULL;
    u8 i = 0;
    CsrTime now;
    CsrTime inactive_time;
    netInterface_priv_t *interfacePriv = (netInterface_priv_t *) data;

    if (!interfacePriv)
    {
        return;
    }

    priv = interfacePriv->privPtr;

    if (interfacePriv->InterfaceTag >= CSR_WIFI_NUM_INTERFACES)
    {
        unifi_error(priv, "check_inactivity_timer_expire_func: Invalid interfaceTag\n");
        return;
    }

    /* RUN Algorithm to check inactivity for each connected station */
    now = CsrTimeGet(NULL);

    for(i = 0; i < UNIFI_MAX_CONNECTIONS; i++) {
        if(interfacePriv->staInfo[i] != NULL) {
            sta_record = interfacePriv->staInfo[i];

            if (sta_record->activity_flag == TRUE){
                sta_record->activity_flag = FALSE;
                sta_record->lastActivity = now;
                continue;
            }

            if (sta_record->lastActivity > now)
            {
                /* simple timer wrap (for 1 wrap) */
                inactive_time = CsrTimeAdd((CsrTime)CsrTimeSub(CSR_SCHED_TIME_MAX, sta_record->lastActivity), now);
            }
            else
            {
                inactive_time = (CsrTime)CsrTimeSub(now, sta_record->lastActivity);
            }

            if (inactive_time >= STA_INACTIVE_TIMEOUT_VAL)
            {
                unifi_trace(priv, UDBG1, "STA is Inactive - AID = %d inactive_time = %d\n",
                                        sta_record->aid,
                                        inactive_time);

                /* station is in-active, if it is in active mode send a null frame
                 * and the station should acknowledge the null frame, if acknowledgement
                 * is not received throw out the station.
                 * If the station is in Power Save, update TIM for the station so
                 * that it wakes up and register some activity through PS-Poll or
                 * trigger frame.
                 */
                 if (sta_record->currentPeerState == CSR_WIFI_ROUTER_CTRL_PEER_CONNECTED_ACTIVE)
                 {
                    unifi_trace(priv, UDBG1, "STA power save state - Active, send a NULL frame to check if it is ALIVE\n");
                    uf_send_nulldata ( priv,
                                       sta_record->interfacePriv->InterfaceTag,
                                       sta_record->peerMacAddress.a,
                                       CSR_CONTENTION,
                                       sta_record);
                 }
                 else if (sta_record->currentPeerState == CSR_WIFI_ROUTER_CTRL_PEER_CONNECTED_POWER_SAVE)
                 {
                    if((sta_record->timSet == CSR_WIFI_TIM_SET) ||
                       (sta_record->timSet == CSR_WIFI_TIM_SETTING))
                    {
                        unifi_trace(priv, UDBG1, "STA power save state - PS, TIM is already SET\n");

                        /* If TIM is set and we do not have any activity for
                         * more than 3 listen intervals then send a disconnected
                         * indication to SME, to delete the station from station
                         * record list.
                         * The inactivity is already more than STA_INACTIVE_TIMEOUT_VAL
                         * and this check ensures if the listen interval is a larger
                         * value than STA_INACTIVE_TIMEOUT_VAL.
                         */
                         if (inactive_time > (3 * (sta_record->listenIntervalInTus * 1024)))
                         {
                            unifi_trace(priv, UDBG1, "STA is inactive for more than 3 listen intervals\n");
                            queue_work( priv->unifi_workqueue,
                                        &sta_record->send_disconnected_ind_task);
                         }

                    }
                    else
                    {
                        unifi_trace(priv, UDBG1, "STA power save state - PS, update TIM to see if it is ALIVE\n");
                        update_tim(priv,
                                   sta_record->aid,
                                   CSR_WIFI_TIM_SET,
                                   interfacePriv->InterfaceTag,
                                   sta_record->assignedHandle);
                    }
                 }
            }
        }
    }

    /* re-run the timer interrupt */
    mod_timer(&interfacePriv->sta_activity_check_timer,
              (jiffies + usecs_to_jiffies(STA_INACTIVE_DETECTION_TIMER_INTERVAL * 1000 * 1000)));

}


void uf_send_disconnected_ind_wq(struct work_struct *work)
{

    CsrWifiRouterCtrlStaInfo_t *staInfo = container_of(work, CsrWifiRouterCtrlStaInfo_t, send_disconnected_ind_task);
    unifi_priv_t *priv;
    u16 interfaceTag;
    struct list_head send_cfm_list;
    u8 j;

    func_enter();

    if(!staInfo) {
        return;
    }

    if(!staInfo->interfacePriv) {
        return;
    }

    priv = staInfo->interfacePriv->privPtr;
    interfaceTag =  staInfo->interfacePriv->InterfaceTag;

    if (interfaceTag >= CSR_WIFI_NUM_INTERFACES) {
        unifi_error(priv, "uf_send_disconnected_ind_wq: invalid interfaceTag\n");
        return;
    }

    /* The SME/NME may be waiting for confirmation for requested frames to this station.
     * So loop through buffered frames for this station and if confirmation is
     * requested, send auto confirmation with failure status. Also flush the frames so
     * that these are not processed again in PEER_DEL_REQ handler.
     */
    INIT_LIST_HEAD(&send_cfm_list);

    uf_prepare_send_cfm_list_for_queued_pkts(priv,
                                             &send_cfm_list,
                                             &(staInfo->mgtFrames));

    uf_flush_list(priv, &(staInfo->mgtFrames));

    for(j = 0; j < MAX_ACCESS_CATOGORY; j++){
        uf_prepare_send_cfm_list_for_queued_pkts(priv,
                                                 &send_cfm_list,
                                                 &(staInfo->dataPdu[j]));

        uf_flush_list(priv,&(staInfo->dataPdu[j]));
    }

    send_auto_ma_packet_confirm(priv, staInfo->interfacePriv, &send_cfm_list);

    unifi_warning(priv, "uf_send_disconnected_ind_wq: Router Disconnected IND Peer (%x-%x-%x-%x-%x-%x)\n",
                staInfo->peerMacAddress.a[0],
                staInfo->peerMacAddress.a[1],
                staInfo->peerMacAddress.a[2],
                staInfo->peerMacAddress.a[3],
                staInfo->peerMacAddress.a[4],
                staInfo->peerMacAddress.a[5]);

    CsrWifiRouterCtrlConnectedIndSend(priv->CSR_WIFI_SME_IFACEQUEUE,
                                      0,
                                      staInfo->interfacePriv->InterfaceTag,
                                      staInfo->peerMacAddress,
                                      CSR_WIFI_ROUTER_CTRL_PEER_DISCONNECTED);


    return;
}


#endif
void CsrWifiRouterCtrlPeerAddReqHandler(void* drvpriv,CsrWifiFsmEvent* msg)
{
    CsrWifiRouterCtrlPeerAddReq* req = (CsrWifiRouterCtrlPeerAddReq*)msg;
    CsrResult status = CSR_RESULT_SUCCESS;
    unifi_priv_t *priv = (unifi_priv_t*)drvpriv;
    u32 handle = 0;
    netInterface_priv_t *interfacePriv = priv->interfacePriv[req->interfaceTag];

    unifi_trace(priv, UDBG2, "entering CsrWifiRouterCtrlPeerAddReqHandler \n");
    if (priv == NULL)
    {
        unifi_error(priv, "CsrWifiRouterCtrlPeerAddReqHandler: invalid smepriv\n");
        return;
    }

    if (req->interfaceTag >= CSR_WIFI_NUM_INTERFACES)
    {
        unifi_error(priv, "CsrWifiRouterCtrlPeerAddReqHandler: bad interfaceTag\n");
        return;
    }

    switch(interfacePriv->interfaceMode)
    {
        case CSR_WIFI_ROUTER_CTRL_MODE_AP:
        case CSR_WIFI_ROUTER_CTRL_MODE_IBSS:
        case CSR_WIFI_ROUTER_CTRL_MODE_P2PGO:
            /* Add station record */
            status = peer_add_new_record(priv,req,&handle);
            break;
        case CSR_WIFI_ROUTER_CTRL_MODE_STA:
        case CSR_WIFI_ROUTER_CTRL_MODE_P2PCLI:
        default:
            /* No station record to maintain in these modes */
            break;
    }

    CsrWifiRouterCtrlPeerAddCfmSend(msg->source,req->clientData,req->interfaceTag,req->peerMacAddress,handle,status);
    unifi_trace(priv, UDBG2, "leaving CsrWifiRouterCtrlPeerAddReqHandler \n");
}

void CsrWifiRouterCtrlPeerUpdateReqHandler(void* drvpriv,CsrWifiFsmEvent* msg)
{
    CsrWifiRouterCtrlPeerUpdateReq* req = (CsrWifiRouterCtrlPeerUpdateReq*)msg;
    CsrResult status = CSR_RESULT_SUCCESS;
    unifi_priv_t *priv = (unifi_priv_t*)drvpriv;

    unifi_trace(priv, UDBG2, "entering CsrWifiRouterCtrlPeerUpdateReqHandler \n");
    if (priv == NULL)
    {
        unifi_error(priv, "CsrWifiRouterCtrlPeerUpdateReqHandler: invalid smepriv\n");
        return;
    }

    CsrWifiRouterCtrlPeerUpdateCfmSend(msg->source,req->clientData,req->interfaceTag,status);
    unifi_trace(priv, UDBG2, "leaving CsrWifiRouterCtrlPeerUpdateReqHandler \n");
}


 void CsrWifiRouterCtrlRawSdioDeinitialiseReqHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
    /* This will never be called as it is intercepted in the Userspace */
}

void CsrWifiRouterCtrlRawSdioInitialiseReqHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
    /* This will never be called as it is intercepted in the Userspace */
}

void
uf_send_ba_err_wq(struct work_struct *work)
{
    ba_session_rx_struct *ba_session = container_of(work, ba_session_rx_struct, send_ba_err_task);
    unifi_priv_t *priv;

    if(!ba_session) {
        return;
    }

    if(!ba_session->interfacePriv) {
        return;
    }

    priv = ba_session->interfacePriv->privPtr;

    if (ba_session->interfacePriv->InterfaceTag >= CSR_WIFI_NUM_INTERFACES) {
        unifi_error(priv, "%s: invalid interfaceTag\n", __FUNCTION__);
        return;
    }

    unifi_warning(priv, "%s: Calling CsrWifiRouterCtrlBlockAckErrorIndSend(%d, %d, %d, %d, %x:%x:%x:%x:%x:%x, %d)\n",
                    __FUNCTION__,
                    priv->CSR_WIFI_SME_IFACEQUEUE,
                    0,
                    ba_session->interfacePriv->InterfaceTag,
                    ba_session->tID,
                    ba_session->macAddress.a[0],
                    ba_session->macAddress.a[1],
                    ba_session->macAddress.a[2],
                    ba_session->macAddress.a[3],
                    ba_session->macAddress.a[4],
                    ba_session->macAddress.a[5],
                    CSR_RESULT_SUCCESS
                 );
    CsrWifiRouterCtrlBlockAckErrorIndSend(priv->CSR_WIFI_SME_IFACEQUEUE,
                    0,
                    ba_session->interfacePriv->InterfaceTag,
                    ba_session->tID,
                    ba_session->macAddress,
                    CSR_RESULT_SUCCESS);
}


static void ba_session_terminate_timer_func(unsigned long data)
{
    ba_session_rx_struct *ba_session = (ba_session_rx_struct*)data;
    struct unifi_priv *priv;

    if(!ba_session) {
        return;
    }

    if(!ba_session->interfacePriv) {
        return;
    }

    priv = ba_session->interfacePriv->privPtr;

    if (ba_session->interfacePriv->InterfaceTag >= CSR_WIFI_NUM_INTERFACES) {
        unifi_error(priv, "%s: invalid interfaceTag\n", __FUNCTION__);
        return;
    }

    queue_work(priv->unifi_workqueue, &ba_session->send_ba_err_task);
}


u8 blockack_session_stop(unifi_priv_t *priv,
                                     u16 interfaceTag,
                                     CsrWifiRouterCtrlBlockAckRole role,
                                     u16 tID,
                                     CsrWifiMacAddress macAddress)
{
    netInterface_priv_t *interfacePriv;
    ba_session_rx_struct *ba_session_rx = NULL;
    ba_session_tx_struct *ba_session_tx = NULL;
    u8 ba_session_idx = 0;
    int i;

    if (interfaceTag >= CSR_WIFI_NUM_INTERFACES) {
        unifi_error(priv, "%s: bad interfaceTag = %d\n", __FUNCTION__, interfaceTag);
        return FALSE;
    }

    interfacePriv = priv->interfacePriv[interfaceTag];

    if(!interfacePriv) {
        unifi_error(priv, "%s: bad interfacePriv\n", __FUNCTION__);
        return FALSE;
    }

    if(tID > 15) {
        unifi_error(priv, "%s: bad tID = %d\n", __FUNCTION__, tID);
        return FALSE;
    }

    if((role != CSR_WIFI_ROUTER_CTRL_BLOCK_ACK_ORIGINATOR) &&
        (role != CSR_WIFI_ROUTER_CTRL_BLOCK_ACK_RECIPIENT)) {
        unifi_error(priv, "%s: bad role = %d\n", __FUNCTION__, role);
        return FALSE;
        }

	unifi_warning(priv,
		"%s: stopping ba_session for peer = %pM role = %d tID = %d\n",
		__func__, macAddress.a, role, tID);

    /* find out the appropriate ba session (/station /tid /role) for which stop is requested */
    if (role == CSR_WIFI_ROUTER_CTRL_BLOCK_ACK_RECIPIENT){
        for (ba_session_idx =0; ba_session_idx < MAX_SUPPORTED_BA_SESSIONS_RX; ba_session_idx++){

            ba_session_rx = interfacePriv->ba_session_rx[ba_session_idx];

            if(ba_session_rx){
                if ((!memcmp(ba_session_rx->macAddress.a, macAddress.a, ETH_ALEN)) && (ba_session_rx->tID == tID)){
                    break;
                }
            }
        }

        if (!ba_session_rx || (ba_session_idx == MAX_SUPPORTED_BA_SESSIONS_RX)) {
            unifi_error(priv, "%s: bad ba_session for Rx [tID=%d]\n", __FUNCTION__, tID);
            return FALSE;
        }


        if(ba_session_rx->timeout) {
            del_timer_sync(&ba_session_rx->timer);
        }
        cancel_work_sync(&ba_session_rx->send_ba_err_task);
        for (i = 0; i < ba_session_rx->wind_size; i++) {
            if(ba_session_rx->buffer[i].active) {
                frame_desc_struct *frame_desc = &ba_session_rx->buffer[i];
                unifi_net_data_free(priv, &frame_desc->bulkdata.d[0]);
            }
        }
        kfree(ba_session_rx->buffer);

        interfacePriv->ba_session_rx[ba_session_idx] = NULL;
        kfree(ba_session_rx);
    }else if (role == CSR_WIFI_ROUTER_CTRL_BLOCK_ACK_ORIGINATOR){
        for (ba_session_idx =0; ba_session_idx < MAX_SUPPORTED_BA_SESSIONS_TX; ba_session_idx++){
        ba_session_tx = interfacePriv->ba_session_tx[ba_session_idx];
            if(ba_session_tx){
                if ((!memcmp(ba_session_tx->macAddress.a, macAddress.a, ETH_ALEN)) && (ba_session_tx->tID == tID)){
                    break;
                }
            }
        }

        if (!ba_session_tx || (ba_session_idx == MAX_SUPPORTED_BA_SESSIONS_TX)) {
            unifi_error(priv, "%s: bad ba_session for Tx [tID=%d]\n", __FUNCTION__, tID);
            return FALSE;
        }
        interfacePriv->ba_session_tx[ba_session_idx] = NULL;
        kfree(ba_session_tx);

    }

    return TRUE;
}


void CsrWifiRouterCtrlBlockAckDisableReqHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
    CsrWifiRouterCtrlBlockAckDisableReq* req = (CsrWifiRouterCtrlBlockAckDisableReq*)msg;
    u8 r;
    unifi_priv_t *priv = (unifi_priv_t*)drvpriv;

    unifi_trace(priv, UDBG6, "%s: in ok\n", __FUNCTION__);

    down(&priv->ba_mutex);
    r = blockack_session_stop(priv,
                              req->interfaceTag,
                              req->role,
                              req->trafficStreamID,
                              req->macAddress);
    up(&priv->ba_mutex);

    CsrWifiRouterCtrlBlockAckDisableCfmSend(msg->source,
                                            req->clientData,
                                            req->interfaceTag,
                                            r?CSR_RESULT_SUCCESS:CSR_RESULT_FAILURE);

    unifi_trace(priv, UDBG6, "%s: out ok\n", __FUNCTION__);
}


u8 blockack_session_start(unifi_priv_t *priv,
                               u16 interfaceTag,
                               u16 tID,
                               u16 timeout,
                               CsrWifiRouterCtrlBlockAckRole role,
                               u16 wind_size,
                               u16 start_sn,
                               CsrWifiMacAddress macAddress
                              )
{
    netInterface_priv_t *interfacePriv;
    ba_session_rx_struct *ba_session_rx = NULL;
    ba_session_tx_struct *ba_session_tx = NULL;
    u8 ba_session_idx = 0;


    if (interfaceTag >= CSR_WIFI_NUM_INTERFACES) {
        unifi_error(priv, "%s: bad interfaceTag = %d\n", __FUNCTION__, interfaceTag);
        return FALSE;
    }

    interfacePriv = priv->interfacePriv[interfaceTag];

    if(!interfacePriv) {
        unifi_error(priv, "%s: bad interfacePriv\n", __FUNCTION__);
        return FALSE;
    }

    if(tID > 15)
    {
        unifi_error(priv, "%s: bad tID=%d\n", __FUNCTION__, tID);
        return FALSE;
    }

    if(wind_size > MAX_BA_WIND_SIZE) {
        unifi_error(priv, "%s: bad wind_size = %d\n", __FUNCTION__, wind_size);
        return FALSE;
    }

    if(role != CSR_WIFI_ROUTER_CTRL_BLOCK_ACK_ORIGINATOR &&
       role != CSR_WIFI_ROUTER_CTRL_BLOCK_ACK_RECIPIENT) {
        unifi_error(priv, "%s: bad role = %d\n", __FUNCTION__, role);
        return FALSE;
    }

	unifi_warning(priv,
		"%s: ba session with peer= (%pM)\n", __func__,
		macAddress.a);

    unifi_warning(priv, "%s: ba session for tID=%d timeout=%d role=%d wind_size=%d start_sn=%d\n", __FUNCTION__,
                  tID,
                  timeout,
                  role,
                  wind_size,
                  start_sn);

    /* Check if BA session exists for per station, per TID, per role or not.
    if BA session exists update parameters and if it does not exist
    create a new BA session */
    if (role == CSR_WIFI_ROUTER_CTRL_BLOCK_ACK_ORIGINATOR){
        for (ba_session_idx =0; ba_session_idx < MAX_SUPPORTED_BA_SESSIONS_TX; ba_session_idx++){
            ba_session_tx = interfacePriv->ba_session_tx[ba_session_idx];
            if (ba_session_tx) {
                if ((!memcmp(ba_session_tx->macAddress.a, macAddress.a, ETH_ALEN)) && (ba_session_tx->tID == tID)){
                    unifi_warning(priv, "%s: ba_session for Tx already exists\n", __FUNCTION__);
                    return TRUE;
                }
            }
        }

        /* we have to create new ba_session_tx struct */
         ba_session_tx = NULL;

        /* loop through until an empty BA session slot is there and save the session there */
        for (ba_session_idx=0; ba_session_idx < MAX_SUPPORTED_BA_SESSIONS_TX ; ba_session_idx++){
            if (!(interfacePriv->ba_session_tx[ba_session_idx])){
                break;
            }
        }
        if (ba_session_idx == MAX_SUPPORTED_BA_SESSIONS_TX){
            unifi_error(priv, "%s: All ba_session used for Tx, NO free session available\n", __FUNCTION__);
            return FALSE;
        }

        /* create and populate the new BA session structure */
        ba_session_tx = kmalloc(sizeof(ba_session_tx_struct), GFP_KERNEL);
        if (!ba_session_tx) {
            unifi_error(priv, "%s: kmalloc failed for ba_session_tx\n", __FUNCTION__);
            return FALSE;
        }
        memset(ba_session_tx, 0, sizeof(ba_session_tx_struct));

        ba_session_tx->interfacePriv = interfacePriv;
        ba_session_tx->tID = tID;
        ba_session_tx->macAddress = macAddress;

        interfacePriv->ba_session_tx[ba_session_idx] = ba_session_tx;

    } else if (role == CSR_WIFI_ROUTER_CTRL_BLOCK_ACK_RECIPIENT){

        for (ba_session_idx =0; ba_session_idx < MAX_SUPPORTED_BA_SESSIONS_RX; ba_session_idx++){
            ba_session_rx = interfacePriv->ba_session_rx[ba_session_idx];
            if (ba_session_rx) {
                if ((!memcmp(ba_session_rx->macAddress.a, macAddress.a, ETH_ALEN)) && (ba_session_rx->tID == tID)){
                    unifi_warning(priv, "%s: ba_session for Rx[tID = %d] already exists\n", __FUNCTION__, tID);

                    if(ba_session_rx->wind_size == wind_size &&
                        ba_session_rx->timeout == timeout &&
                        ba_session_rx->expected_sn == start_sn) {
                        return TRUE;
                    }

                    if(ba_session_rx->timeout) {
                        del_timer_sync(&ba_session_rx->timer);
                        ba_session_rx->timeout = 0;
                    }

                    if(ba_session_rx->wind_size != wind_size) {
                        blockack_session_stop(priv, interfaceTag, role, tID, macAddress);
                    } else {
                        if (timeout) {
                            ba_session_rx->timeout = timeout;
                            ba_session_rx->timer.function = ba_session_terminate_timer_func;
                            ba_session_rx->timer.data = (unsigned long)ba_session_rx;
                            init_timer(&ba_session_rx->timer);
                            mod_timer(&ba_session_rx->timer, (jiffies + usecs_to_jiffies((ba_session_rx->timeout) * 1024)));
                        }
                        /*
                         * The starting sequence number shall remain same if the BA
                         * enable request is issued to update BA parameters only. If
                         * it is not same, then we scroll our window to the new starting
                         * sequence number. This could happen if the DELBA frame from
                         * originator is lost and then we receive ADDBA frame with new SSN.
                        */
                        if(ba_session_rx->start_sn != start_sn) {
                            scroll_ba_window(priv, interfacePriv, ba_session_rx, start_sn);
                        }
                        return TRUE;
                    }
                }
            }
        }

        /* we could have a valid BA session pointer here or un-initialized
        ba session pointer. but in any case we have to create a new session.
        so re-initialize the ba_session pointer */
        ba_session_rx = NULL;

        /* loop through until an empty BA session slot is there and save the session there */
        for (ba_session_idx=0; ba_session_idx < MAX_SUPPORTED_BA_SESSIONS_RX ; ba_session_idx++){
            if (!(interfacePriv->ba_session_rx[ba_session_idx])){
                break;
            }
        }
        if (ba_session_idx == MAX_SUPPORTED_BA_SESSIONS_RX){
            unifi_error(priv, "%s: All ba_session used for Rx, NO free session available\n", __FUNCTION__);
            return FALSE;
        }

        /* It is observed that with some devices there is a race between
         * EAPOL exchanges and BA session establishment. This results in
         * some EAPOL authentication packets getting stuck in BA reorder
         * buffer and hence the conection cannot be established. To avoid
         * this we check here if the EAPOL authentication is complete and
         * if so then only allow the BA session to establish.
         *
         * It is verified that the peers normally re-establish
         * the BA session after the initial rejection.
         */
        if (CSR_WIFI_ROUTER_CTRL_PORT_ACTION_8021X_PORT_OPEN != uf_sme_port_state(priv, macAddress.a, UF_CONTROLLED_PORT_Q, interfacePriv->InterfaceTag))
        {
            unifi_warning(priv, "blockack_session_start: Controlled port not opened, Reject BA request\n");
            return FALSE;
        }

        ba_session_rx = kmalloc(sizeof(ba_session_rx_struct), GFP_KERNEL);
        if (!ba_session_rx) {
            unifi_error(priv, "%s: kmalloc failed for ba_session_rx\n", __FUNCTION__);
            return FALSE;
        }
        memset(ba_session_rx, 0, sizeof(ba_session_rx_struct));

        ba_session_rx->wind_size = wind_size;
        ba_session_rx->start_sn = ba_session_rx->expected_sn = start_sn;
        ba_session_rx->trigger_ba_after_ssn = FALSE;

        ba_session_rx->buffer = kmalloc(ba_session_rx->wind_size*sizeof(frame_desc_struct), GFP_KERNEL);
        if (!ba_session_rx->buffer) {
            kfree(ba_session_rx);
            unifi_error(priv, "%s: kmalloc failed for buffer\n", __FUNCTION__);
            return FALSE;
        }

        memset(ba_session_rx->buffer, 0, ba_session_rx->wind_size*sizeof(frame_desc_struct));

        INIT_WORK(&ba_session_rx->send_ba_err_task, uf_send_ba_err_wq);
        if (timeout) {
            ba_session_rx->timeout = timeout;
            ba_session_rx->timer.function = ba_session_terminate_timer_func;
            ba_session_rx->timer.data = (unsigned long)ba_session_rx;
            init_timer(&ba_session_rx->timer);
            mod_timer(&ba_session_rx->timer, (jiffies + usecs_to_jiffies((ba_session_rx->timeout) * 1024)));
        }

        ba_session_rx->interfacePriv = interfacePriv;
        ba_session_rx->tID = tID;
        ba_session_rx->macAddress = macAddress;

        interfacePriv->ba_session_rx[ba_session_idx] = ba_session_rx;
    }
    return TRUE;
}

void CsrWifiRouterCtrlBlockAckEnableReqHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
    CsrWifiRouterCtrlBlockAckEnableReq* req = (CsrWifiRouterCtrlBlockAckEnableReq*)msg;
    u8 r;
    unifi_priv_t *priv = (unifi_priv_t*)drvpriv;

    unifi_trace(priv, UDBG6, ">>%s\n", __FUNCTION__);
    down(&priv->ba_mutex);
    r = blockack_session_start(priv,
                               req->interfaceTag,
                               req->trafficStreamID,
                               req->timeout,
                               req->role,
                               req->bufferSize,
                               req->ssn,
                               req->macAddress
                              );
    up(&priv->ba_mutex);

    CsrWifiRouterCtrlBlockAckEnableCfmSend(msg->source,
                                           req->clientData,
                                           req->interfaceTag,
                                           r?CSR_RESULT_SUCCESS:CSR_RESULT_FAILURE);
    unifi_trace(priv, UDBG6, "<<%s: r=%d\n", __FUNCTION__, r);

}

void CsrWifiRouterCtrlWapiMulticastFilterReqHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
#ifdef CSR_WIFI_SECURITY_WAPI_ENABLE

    unifi_priv_t *priv = (unifi_priv_t*)drvpriv;
    CsrWifiRouterCtrlWapiMulticastFilterReq* req = (CsrWifiRouterCtrlWapiMulticastFilterReq*)msg;
    netInterface_priv_t *interfacePriv = priv->interfacePriv[req->interfaceTag];

    if (CSR_WIFI_ROUTER_CTRL_MODE_STA == interfacePriv->interfaceMode) {

        unifi_trace(priv, UDBG6, ">>%s\n", __FUNCTION__);

        unifi_trace(priv, UDBG1, "CsrWifiRouterCtrlWapiMulticastFilterReq: req->status = %d\n", req->status);

        /* status 1 - Filter on
        * status 0 - Filter off */
        priv->wapi_multicast_filter = req->status;

        unifi_trace(priv, UDBG6, "<<%s\n", __FUNCTION__);
    } else {

    	unifi_warning(priv, "%s is NOT applicable for interface mode - %d\n", __FUNCTION__,interfacePriv->interfaceMode);

    }
#elif defined(UNIFI_DEBUG)
    /*WAPI Disabled*/
    unifi_priv_t *priv = (unifi_priv_t*)drvpriv;
    unifi_error(priv,"CsrWifiRouterCtrlWapiMulticastFilterReqHandler: called when WAPI isn't enabled\n");
#endif
}

void CsrWifiRouterCtrlWapiUnicastFilterReqHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
#ifdef CSR_WIFI_SECURITY_WAPI_ENABLE

    unifi_priv_t *priv = (unifi_priv_t*)drvpriv;
    CsrWifiRouterCtrlWapiUnicastFilterReq* req = (CsrWifiRouterCtrlWapiUnicastFilterReq*)msg;
    netInterface_priv_t *interfacePriv = priv->interfacePriv[req->interfaceTag];

    if (CSR_WIFI_ROUTER_CTRL_MODE_STA == interfacePriv->interfaceMode) {

        unifi_trace(priv, UDBG6, ">>%s\n", __FUNCTION__);

        unifi_trace(priv, UDBG1, "CsrWifiRouterCtrlWapiUnicastFilterReq: req->status= %d\n", req->status);

        if ((priv->wapi_unicast_filter == 1) && (req->status == 0)) {
            /* When we have successfully re-associated and obtained a new unicast key with keyid = 0 */
            priv->wapi_unicast_queued_pkt_filter = 1;
        }

        /* status 1 - Filter ON
         * status 0 - Filter OFF */
        priv->wapi_unicast_filter = req->status;

        unifi_trace(priv, UDBG6, "<<%s\n", __FUNCTION__);
    } else {

    	 unifi_warning(priv, "%s is NOT applicable for interface mode - %d\n", __FUNCTION__,interfacePriv->interfaceMode);

    }
#elif defined(UNIFI_DEBUG)
    /*WAPI Disabled*/
    unifi_priv_t *priv = (unifi_priv_t*)drvpriv;
    unifi_error(priv,"CsrWifiRouterCtrlWapiUnicastFilterReqHandler: called when WAPI isn't enabled\n");
#endif
}

void CsrWifiRouterCtrlWapiRxPktReqHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
#ifdef CSR_WIFI_SECURITY_WAPI_ENABLE

    unifi_priv_t *priv = (unifi_priv_t*)drvpriv;
    CsrWifiRouterCtrlWapiRxPktReq* req =  (CsrWifiRouterCtrlWapiRxPktReq*)msg;
    int client_id, receiver_id;
    bulk_data_param_t bulkdata;
    CsrResult res;
    ul_client_t *client;
    CSR_SIGNAL signal;
    CSR_MA_PACKET_INDICATION *pkt_ind;
    netInterface_priv_t *interfacePriv = priv->interfacePriv[req->interfaceTag];

    if (CSR_WIFI_ROUTER_CTRL_MODE_STA == interfacePriv->interfaceMode) {

    	unifi_trace(priv, UDBG6, ">>%s\n", __FUNCTION__);

        if (priv == NULL) {
            unifi_error(priv, "CsrWifiRouterCtrlWapiRxPktReq : invalid priv\n",__FUNCTION__);
            return;
        }

        if (priv->smepriv == NULL) {
             unifi_error(priv, "CsrWifiRouterCtrlWapiRxPktReq : invalid sme priv\n",__FUNCTION__);
             return;
        }

        if (req->dataLength == 0 || req->data == NULL) {
             unifi_error(priv, "CsrWifiRouterCtrlWapiRxPktReq: invalid request\n",__FUNCTION__);
             return;
        }

        res = unifi_net_data_malloc(priv, &bulkdata.d[0], req->dataLength);
        if (res != CSR_RESULT_SUCCESS) {
             unifi_error(priv, "CsrWifiRouterCtrlWapiRxPktReq: Could not allocate net data\n",__FUNCTION__);
             return;
        }

        /* This function is expected to be called only when the MIC has been verified by SME to be correct
         * So reset the reception status to rx_success */
        res = read_unpack_signal(req->signal, &signal);
        if (res) {
	          unifi_error(priv,"CsrWifiRouterCtrlWapiRxPktReqHandler: Received unknown or corrupted signal.\n");
	          return;
        }
        pkt_ind = (CSR_MA_PACKET_INDICATION*) (&((&signal)->u).MaPacketIndication);
        if (pkt_ind->ReceptionStatus != CSR_MICHAEL_MIC_ERROR) {
	          unifi_error(priv,"CsrWifiRouterCtrlWapiRxPktReqHandler: Unknown signal with reception status = %d\n",pkt_ind->ReceptionStatus);
	          return;
        } else {
	          unifi_trace(priv, UDBG4,"CsrWifiRouterCtrlWapiRxPktReqHandler: MIC verified , RX_SUCCESS \n",__FUNCTION__);
	          pkt_ind->ReceptionStatus = CSR_RX_SUCCESS;
	          write_pack(&signal, req->signal, &(req->signalLength));
        }

        memcpy((void*)bulkdata.d[0].os_data_ptr, req->data, req->dataLength);

        receiver_id = CSR_GET_UINT16_FROM_LITTLE_ENDIAN((req->signal) + sizeof(s16)) & 0xFFF0;
        client_id = (receiver_id & 0x0F00) >> UDI_SENDER_ID_SHIFT;

        client = &priv->ul_clients[client_id];

        if (client && client->event_hook) {
              unifi_trace(priv, UDBG3,
                          "CsrWifiRouterCtrlWapiRxPktReq: "
                          "Sending signal to client %d, (s:0x%X, r:0x%X) - Signal 0x%X \n",
                          client->client_id, client->sender_id, receiver_id,
                          CSR_GET_UINT16_FROM_LITTLE_ENDIAN(req->signal));

              client->event_hook(client, req->signal, req->signalLength, &bulkdata, UDI_TO_HOST);
        } else {
              unifi_trace(priv, UDBG4, "No client to give the packet to\n");
              unifi_net_data_free(priv, &bulkdata.d[0]);
        }

        unifi_trace(priv, UDBG6, "<<%s\n", __FUNCTION__);
    } else {
    	unifi_warning(priv, "%s is NOT applicable for interface mode - %d\n", __FUNCTION__,interfacePriv->interfaceMode);
    }
#elif defined(UNIFI_DEBUG)
    /*WAPI Disabled*/
    unifi_priv_t *priv = (unifi_priv_t*)drvpriv;
    unifi_error(priv,"CsrWifiRouterCtrlWapiRxPktReqHandler: called when WAPI isn't enabled\n");
#endif
}

void CsrWifiRouterCtrlWapiUnicastTxPktReqHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
#if (defined(CSR_WIFI_SECURITY_WAPI_ENABLE) && defined(CSR_WIFI_SECURITY_WAPI_SW_ENCRYPTION))

	unifi_priv_t *priv = (unifi_priv_t*) drvpriv;
    CsrWifiRouterCtrlWapiUnicastTxPktReq *req 	= (CsrWifiRouterCtrlWapiUnicastTxPktReq*) msg;
    netInterface_priv_t *interfacePriv = priv->interfacePriv[req->interfaceTag];
    bulk_data_param_t bulkdata;
    u8 macHeaderLengthInBytes = MAC_HEADER_SIZE;
    /*KeyID, Reserved, PN, MIC*/
    u8 appendedCryptoFields = 1 + 1 + 16 + 16;
    CsrResult result;
    /* Retrieve the MA PACKET REQ fields from the Signal retained from send_ma_pkt_request() */
    CSR_MA_PACKET_REQUEST *storedSignalMAPktReq = &interfacePriv->wapi_unicast_ma_pkt_sig.u.MaPacketRequest;

    if (CSR_WIFI_ROUTER_CTRL_MODE_STA == interfacePriv->interfaceMode) {

        unifi_trace(priv, UDBG6, ">>%s\n", __FUNCTION__);

        if (priv == NULL) {
            unifi_error(priv, "CsrWifiRouterCtrlWapiUnicastTxPktReqHandler : invalid priv\n",__FUNCTION__);
            return;
        }
        if (priv->smepriv == NULL) {
            unifi_error(priv, "CsrWifiRouterCtrlWapiUnicastTxPktReqHandler : invalid sme priv\n",__FUNCTION__);
            return;
        }
        if (req->data == NULL) {
            unifi_error(priv, "CsrWifiRouterCtrlWapiUnicastTxPktReqHandler: invalid request\n",__FUNCTION__);
            return;
        } else {
            /* If it is QoS data (type = data subtype = QoS), frame header contains QoS control field */
            if ((req->data[0] & 0x88) == 0x88) {
      	        macHeaderLengthInBytes  = macHeaderLengthInBytes + QOS_CONTROL_HEADER_SIZE;
            }
        }
        if ( !(req->dataLength>(macHeaderLengthInBytes+appendedCryptoFields)) ) {
            unifi_error(priv, "CsrWifiRouterCtrlWapiUnicastTxPktReqHandler: invalid dataLength\n",__FUNCTION__);
            return;
        }

	    /* Encrypted DATA Packet contained in (req->data)
         * -------------------------------------------------------------------
         * |MAC Header|  KeyId   | Reserved |    PN    | xxDataxx | xxMICxxx |
         * -------------------------------------------------------------------
         *                                             (<-----Encrypted----->)
         * -------------------------------------------------------------------
         * |24/26(QoS)|    1     |    1     |    16    |    x     |    16    |
         * -------------------------------------------------------------------
         */
        result = unifi_net_data_malloc(priv, &bulkdata.d[0], req->dataLength);
        if (result != CSR_RESULT_SUCCESS) {
             unifi_error(priv, "CsrWifiRouterCtrlWapiUnicastTxPktReqHandler: Could not allocate net data\n",__FUNCTION__);
             return;
        }
        memcpy((void*)bulkdata.d[0].os_data_ptr, req->data, req->dataLength);
        bulkdata.d[0].data_length = req->dataLength;
        bulkdata.d[1].os_data_ptr = NULL;
        bulkdata.d[1].data_length = 0;

        /* Send UniFi msg */
        /* Here hostTag is been sent as 0xffffffff, its been appended properly while framing MA-Packet request in pdu_processing.c file */
        result = uf_process_ma_packet_req(priv,
    	                                  storedSignalMAPktReq->Ra.x,
                                          storedSignalMAPktReq->HostTag,/* Ask for a new HostTag */
                                          req->interfaceTag,
                                          storedSignalMAPktReq->TransmissionControl,
                                          storedSignalMAPktReq->TransmitRate,
                                          storedSignalMAPktReq->Priority, /* Retained value */
                                          interfacePriv->wapi_unicast_ma_pkt_sig.SignalPrimitiveHeader.SenderProcessId, /*FIXME AP: VALIDATE ???*/
                                          &bulkdata);

        if (result == NETDEV_TX_OK) {
             (priv->netdev[req->interfaceTag])->trans_start = jiffies;
             /* Should really count tx stats in the UNITDATA.status signal but
              * that doesn't have the length.
              */
             interfacePriv->stats.tx_packets++;

             /* count only the packet payload */
             interfacePriv->stats.tx_bytes += req->dataLength - macHeaderLengthInBytes - appendedCryptoFields;
             unifi_trace(priv, UDBG1, "CsrWifiRouterCtrlWapiUnicastTxPktReqHandler: (Packet Sent), sent count = %x\n", interfacePriv->stats.tx_packets);
        } else {
             /* Failed to send: fh queue was full, and the skb was discarded*/
             unifi_trace(priv, UDBG1, "(HIP validation failure) Result = %d\n", result);
             unifi_net_data_free(priv, &bulkdata.d[0]);

             interfacePriv->stats.tx_dropped++;
             unifi_trace(priv, UDBG1, "CsrWifiRouterCtrlWapiUnicastTxPktReqHandler: (Packet Drop), dropped count = %x\n", interfacePriv->stats.tx_dropped);
        }

        unifi_trace(priv, UDBG6, "<<%s\n", __FUNCTION__);

    } else {

    	unifi_warning(priv, "%s is NOT applicable for interface mode - %d\n", __FUNCTION__,interfacePriv->interfaceMode);

    }
#elif defined(UNIFI_DEBUG)
    /*WAPI Disabled*/
    unifi_priv_t *priv = (unifi_priv_t*)drvpriv;
    unifi_error(priv,"CsrWifiRouterCtrlWapiUnicastTxPktReqHandler: called when WAPI SW ENCRYPTION isn't enabled\n");
#endif
}

void CsrWifiRouterCtrlWapiFilterReqHandler(void* drvpriv, CsrWifiFsmEvent* msg)
{
#ifdef CSR_WIFI_SECURITY_WAPI_ENABLE

#ifdef CSR_WIFI_SECURITY_WAPI_QOSCTRL_MIC_WORKAROUND
    unifi_priv_t *priv = (unifi_priv_t*)drvpriv;
    CsrWifiRouterCtrlWapiFilterReq* req = (CsrWifiRouterCtrlWapiFilterReq*)msg;
    netInterface_priv_t *interfacePriv = priv->interfacePriv[req->interfaceTag];

    if (CSR_WIFI_ROUTER_CTRL_MODE_STA == interfacePriv->interfaceMode) {

        unifi_trace(priv, UDBG6, ">>%s\n", __FUNCTION__);

        unifi_trace(priv, UDBG1, "CsrWifiRouterCtrlWapiFilterReq: req->isWapiConnected [0/1] = %d \n",req->isWapiConnected);

        priv->isWapiConnection = req->isWapiConnected;

        unifi_trace(priv, UDBG6, "<<%s\n", __FUNCTION__);
    } else {

    	unifi_warning(priv, "%s is NOT applicable for interface mode - %d\n", __FUNCTION__,interfacePriv->interfaceMode);

    }
#endif

#elif defined(UNIFI_DEBUG)
    /*WAPI Disabled*/
    unifi_priv_t *priv = (unifi_priv_t*)drvpriv;
    unifi_error(priv,"CsrWifiRouterCtrlWapiFilterReq: called when WAPI isn't enabled\n");
#endif
}
