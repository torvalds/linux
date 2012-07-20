/*
 * ***************************************************************************
 *  FILE:     unifi_sme.c
 *
 *  PURPOSE:    SME related functions.
 *
 *  Copyright (C) 2007-2009 by Cambridge Silicon Radio Ltd.
 *
 * Refer to LICENSE.txt included with this source code for details on
 * the license terms.
 *
 * ***************************************************************************
 */

#include "unifi_priv.h"
#include "csr_wifi_hip_unifi.h"
#include "csr_wifi_hip_conversions.h"




    int
convert_sme_error(CsrResult error)
{
    switch (error) {
        case CSR_RESULT_SUCCESS:
            return 0;
        case CSR_RESULT_FAILURE:
        case CSR_WIFI_RESULT_NOT_FOUND:
        case CSR_WIFI_RESULT_TIMED_OUT:
        case CSR_WIFI_RESULT_CANCELLED:
        case CSR_WIFI_RESULT_UNAVAILABLE:
            return -EIO;
        case CSR_WIFI_RESULT_NO_ROOM:
            return -EBUSY;
        case CSR_WIFI_RESULT_INVALID_PARAMETER:
            return -EINVAL;
        case CSR_WIFI_RESULT_UNSUPPORTED:
            return -EOPNOTSUPP;
        default:
            return -EIO;
    }
}


/*
 * ---------------------------------------------------------------------------
 *  sme_log_event
 *
 *      Callback function to be registered as the SME event callback.
 *      Copies the signal content into a new udi_log_t struct and adds
 *      it to the read queue for the SME client.
 *
 *  Arguments:
 *      arg             This is the value given to unifi_add_udi_hook, in
 *                      this case a pointer to the client instance.
 *      signal          Pointer to the received signal.
 *      signal_len      Size of the signal structure in bytes.
 *      bulkdata        Pointers to any associated bulk data.
 *      dir             Direction of the signal. Zero means from host,
 *                      non-zero means to host.
 *
 *  Returns:
 *      None.
 * ---------------------------------------------------------------------------
 */
    void
sme_log_event(ul_client_t *pcli,
        const u8 *signal, int signal_len,
        const bulk_data_param_t *bulkdata,
        int dir)
{
    unifi_priv_t *priv;
    CSR_SIGNAL unpacked_signal;
    CsrWifiSmeDataBlock mlmeCommand;
    CsrWifiSmeDataBlock dataref1;
    CsrWifiSmeDataBlock dataref2;
    CsrResult result = CSR_RESULT_SUCCESS;
    int r;

    func_enter();
    /* Just a sanity check */
    if ((signal == NULL) || (signal_len <= 0)) {
        func_exit();
        return;
    }

    priv = uf_find_instance(pcli->instance);
    if (!priv) {
        unifi_error(priv, "sme_log_event: invalid priv\n");
        func_exit();
        return;
    }

    if (priv->smepriv == NULL) {
        unifi_error(priv, "sme_log_event: invalid smepriv\n");
        func_exit();
        return;
    }

    unifi_trace(priv, UDBG3,
            "sme_log_event: Process signal 0x%.4X\n",
            CSR_GET_UINT16_FROM_LITTLE_ENDIAN(signal));


    /* If the signal is known, then do any filtering required, otherwise it pass it to the SME. */
    r = read_unpack_signal(signal, &unpacked_signal);
    if (r == CSR_RESULT_SUCCESS) {
        if ((unpacked_signal.SignalPrimitiveHeader.SignalId == CSR_DEBUG_STRING_INDICATION_ID) ||
            (unpacked_signal.SignalPrimitiveHeader.SignalId == CSR_DEBUG_WORD16_INDICATION_ID))
        {
            func_exit();
            return;
        }
        if (unpacked_signal.SignalPrimitiveHeader.SignalId == CSR_MA_PACKET_INDICATION_ID)
        {
            u16 frmCtrl;
            u8 unicastPdu = TRUE;
            u8 *macHdrLocation;
            u8 *raddr = NULL, *taddr = NULL;
            CsrWifiMacAddress peerMacAddress;
            /* Check if we need to send CsrWifiRouterCtrlMicFailureInd*/
            CSR_MA_PACKET_INDICATION *ind = &unpacked_signal.u.MaPacketIndication;

            macHdrLocation = (u8 *) bulkdata->d[0].os_data_ptr;
            /* Fetch the frame control value from  mac header */
            frmCtrl = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(macHdrLocation);

            /* Point to the addresses */
            raddr = macHdrLocation + MAC_HEADER_ADDR1_OFFSET;
            taddr = macHdrLocation + MAC_HEADER_ADDR2_OFFSET;

            memcpy(peerMacAddress.a, taddr, ETH_ALEN);

            if(ind->ReceptionStatus == CSR_MICHAEL_MIC_ERROR)
            {
                if (*raddr & 0x1)
                    unicastPdu = FALSE;

                CsrWifiRouterCtrlMicFailureIndSend (priv->CSR_WIFI_SME_IFACEQUEUE, 0,
                        (ind->VirtualInterfaceIdentifier & 0xff),peerMacAddress,
                        unicastPdu);
                return;
            }
            else
            {
                if(ind->ReceptionStatus == CSR_RX_SUCCESS)
                {
                    u8 pmBit = (frmCtrl & 0x1000)?0x01:0x00;
                    u16 interfaceTag = (ind->VirtualInterfaceIdentifier & 0xff);
                    CsrWifiRouterCtrlStaInfo_t *srcStaInfo =  CsrWifiRouterCtrlGetStationRecordFromPeerMacAddress(priv,taddr,interfaceTag);
                    if((srcStaInfo != NULL) && (uf_check_broadcast_bssid(priv, bulkdata)== FALSE))
                    {
                        uf_process_pm_bit_for_peer(priv,srcStaInfo,pmBit,interfaceTag);

                        /* Update station last activity flag */
                        srcStaInfo->activity_flag = TRUE;
                    }
                }
            }
        }

        if (unpacked_signal.SignalPrimitiveHeader.SignalId == CSR_MA_PACKET_CONFIRM_ID)
        {
            CSR_MA_PACKET_CONFIRM *cfm = &unpacked_signal.u.MaPacketConfirm;
            u16 interfaceTag = (cfm->VirtualInterfaceIdentifier & 0xff);
            netInterface_priv_t *interfacePriv;
            CSR_MA_PACKET_REQUEST *req;
            CsrWifiMacAddress peerMacAddress;

            if (interfaceTag >= CSR_WIFI_NUM_INTERFACES)
            {
                unifi_error(priv, "Bad MA_PACKET_CONFIRM interfaceTag %d\n", interfaceTag);
                func_exit();
                return;
            }

            unifi_trace(priv,UDBG1,"MA-PACKET Confirm (%x, %x)\n", cfm->HostTag, cfm->TransmissionStatus);

            interfacePriv = priv->interfacePriv[interfaceTag];
#ifdef CSR_SUPPORT_SME
            if(interfacePriv->interfaceMode == CSR_WIFI_ROUTER_CTRL_MODE_AP ||
                 interfacePriv->interfaceMode == CSR_WIFI_ROUTER_CTRL_MODE_P2PGO) {

                if(cfm->HostTag == interfacePriv->multicastPduHostTag){
                    uf_process_ma_pkt_cfm_for_ap(priv ,interfaceTag, cfm);
                }
            }
#endif

            req = &interfacePriv->m4_signal.u.MaPacketRequest;

            if(cfm->HostTag & 0x80000000)
            {
                if (cfm->TransmissionStatus != CSR_TX_SUCCESSFUL)
                {
                    result = CSR_RESULT_FAILURE;
                }
#ifdef CSR_SUPPORT_SME
                memcpy(peerMacAddress.a, req->Ra.x, ETH_ALEN);
                /* Check if this is a confirm for EAPOL M4 frame and we need to send transmistted ind*/
                if (interfacePriv->m4_sent && (cfm->HostTag == interfacePriv->m4_hostTag))
                {
                    unifi_trace(priv, UDBG1, "%s: Sending M4 Transmit CFM\n", __FUNCTION__);
                    CsrWifiRouterCtrlM4TransmittedIndSend(priv->CSR_WIFI_SME_IFACEQUEUE, 0,
                            interfaceTag,
                            peerMacAddress,
                            result);
                    interfacePriv->m4_sent = FALSE;
                    interfacePriv->m4_hostTag = 0xffffffff;
                }
#endif
                /* If EAPOL was requested via router APIs then send cfm else ignore*/
                if((cfm->HostTag & 0x80000000) != CSR_WIFI_EAPOL_M4_HOST_TAG) {
                    CsrWifiRouterMaPacketCfmSend((u16)signal[2],
                        cfm->VirtualInterfaceIdentifier,
                        result,
                        (cfm->HostTag & 0x3fffffff), cfm->Rate);
                } else {
                    unifi_trace(priv, UDBG1, "%s: M4 received from netdevice\n", __FUNCTION__);
                }
                func_exit();
                return;
            }
        }
    }

    mlmeCommand.length = signal_len;
    mlmeCommand.data = (u8*)signal;

    dataref1.length = bulkdata->d[0].data_length;
    if (dataref1.length > 0) {
        dataref1.data = (u8 *) bulkdata->d[0].os_data_ptr;
    } else
    {
        dataref1.data = NULL;
    }

    dataref2.length = bulkdata->d[1].data_length;
    if (dataref2.length > 0) {
        dataref2.data = (u8 *) bulkdata->d[1].os_data_ptr;
    } else
    {
        dataref2.data = NULL;
    }

    CsrWifiRouterCtrlHipIndSend(priv->CSR_WIFI_SME_IFACEQUEUE, mlmeCommand.length, mlmeCommand.data,
            dataref1.length, dataref1.data,
            dataref2.length, dataref2.data);

    func_exit();
} /* sme_log_event() */


/*
 * ---------------------------------------------------------------------------
 * uf_sme_port_state
 *
 *      Return the state of the controlled port.
 *
 * Arguments:
 *      priv            Pointer to device private context struct
 *      address    Pointer to the destination for tx or sender for rx address
 *      queue           Controlled or uncontrolled queue
 *
 * Returns:
 *      An unifi_ControlledPortAction value.
 * ---------------------------------------------------------------------------
 */
CsrWifiRouterCtrlPortAction
uf_sme_port_state(unifi_priv_t *priv, unsigned char *address, int queue, u16 interfaceTag)
{
    int i;
    unifi_port_config_t *port;
    netInterface_priv_t *interfacePriv;

    if (interfaceTag >= CSR_WIFI_NUM_INTERFACES) {
        unifi_error(priv, "uf_sme_port_state: bad interfaceTag\n");
        return CSR_WIFI_ROUTER_CTRL_PORT_ACTION_8021X_PORT_CLOSED_DISCARD;
    }

    interfacePriv = priv->interfacePriv[interfaceTag];

    if (queue == UF_CONTROLLED_PORT_Q) {
        port = &interfacePriv->controlled_data_port;
    } else {
        port = &interfacePriv->uncontrolled_data_port;
    }

    if (!port->entries_in_use) {
        unifi_trace(priv, UDBG5, "No port configurations, return Discard.\n");
        return CSR_WIFI_ROUTER_CTRL_PORT_ACTION_8021X_PORT_CLOSED_DISCARD;
    }

    /* If the port configuration is common for all destinations, return it. */
    if (port->overide_action == UF_DATA_PORT_OVERIDE) {
        unifi_trace(priv, UDBG5, "Single port configuration (%d).\n",
                port->port_cfg[0].port_action);
        return port->port_cfg[0].port_action;
    }

    unifi_trace(priv, UDBG5, "Multiple (%d) port configurations.\n", port->entries_in_use);

    /* If multiple configurations exist.. */
    for (i = 0; i < UNIFI_MAX_CONNECTIONS; i++) {
        /* .. go through the list and match the destination address. */
        if (port->port_cfg[i].in_use &&
            memcmp(address, port->port_cfg[i].mac_address.a, ETH_ALEN) == 0) {
            /* Return the desired action. */
            return port->port_cfg[i].port_action;
        }
    }

    /* Could not find any information, return Open. */
    unifi_trace(priv, UDBG5, "port configuration not found, return Open.\n");
    return CSR_WIFI_ROUTER_CTRL_PORT_ACTION_8021X_PORT_OPEN;
} /* uf_sme_port_state() */

/*
 * ---------------------------------------------------------------------------
 * uf_sme_port_config_handle
 *
 *      Return the port config handle of the controlled/uncontrolled port.
 *
 * Arguments:
 *      priv            Pointer to device private context struct
 *      address    Pointer to the destination for tx or sender for rx address
 *      queue           Controlled or uncontrolled queue
 *
 * Returns:
 *      An  unifi_port_cfg_t* .
 * ---------------------------------------------------------------------------
 */
unifi_port_cfg_t*
uf_sme_port_config_handle(unifi_priv_t *priv, unsigned char *address, int queue, u16 interfaceTag)
{
    int i;
    unifi_port_config_t *port;
    netInterface_priv_t *interfacePriv = priv->interfacePriv[interfaceTag];

    if (interfaceTag >= CSR_WIFI_NUM_INTERFACES) {
        unifi_error(priv, "uf_sme_port_config_handle: bad interfaceTag\n");
        return NULL;
    }

    if (queue == UF_CONTROLLED_PORT_Q) {
        port = &interfacePriv->controlled_data_port;
    } else {
        port = &interfacePriv->uncontrolled_data_port;
    }

    if (!port->entries_in_use) {
        unifi_trace(priv, UDBG5, "No port configurations, return Discard.\n");
        return NULL;
    }

    /* If the port configuration is common for all destinations, return it. */
    if (port->overide_action == UF_DATA_PORT_OVERIDE) {
        unifi_trace(priv, UDBG5, "Single port configuration (%d).\n",
                port->port_cfg[0].port_action);
        if (address) {
            unifi_trace(priv, UDBG5, "addr[0] = %x, addr[1] = %x, addr[2] = %x, addr[3] = %x\n", address[0], address[1], address[2], address[3]);
        }
        return &port->port_cfg[0];
    }

    unifi_trace(priv, UDBG5, "Multiple port configurations.\n");

    /* If multiple configurations exist.. */
    for (i = 0; i < UNIFI_MAX_CONNECTIONS; i++) {
        /* .. go through the list and match the destination address. */
        if (port->port_cfg[i].in_use &&
            memcmp(address, port->port_cfg[i].mac_address.a, ETH_ALEN) == 0) {
            /* Return the desired action. */
            return &port->port_cfg[i];
        }
    }

    /* Could not find any information, return Open. */
    unifi_trace(priv, UDBG5, "port configuration not found, returning NULL (debug).\n");
    return NULL;
} /* uf_sme_port_config_handle */

void
uf_multicast_list_wq(struct work_struct *work)
{
    unifi_priv_t *priv = container_of(work, unifi_priv_t,
            multicast_list_task);
    int i;
    u16 interfaceTag = 0;
    CsrWifiMacAddress* multicast_address_list = NULL;
    int mc_count;
    u8 *mc_list;
    netInterface_priv_t *interfacePriv = priv->interfacePriv[interfaceTag];

    if (interfaceTag >= CSR_WIFI_NUM_INTERFACES) {
        unifi_error(priv, "uf_multicast_list_wq: bad interfaceTag\n");
        return;
    }

    unifi_trace(priv, UDBG5,
            "uf_multicast_list_wq: list count = %d\n",
            interfacePriv->mc_list_count);

    /* Flush the current list */
    CsrWifiRouterCtrlMulticastAddressIndSend(priv->CSR_WIFI_SME_IFACEQUEUE,0, interfaceTag, CSR_WIFI_SME_LIST_ACTION_FLUSH, 0, NULL);

    mc_count = interfacePriv->mc_list_count;
    mc_list = interfacePriv->mc_list;
    /*
     * Allocate a new list, need to free it later
     * in unifi_mgt_multicast_address_cfm().
     */
    multicast_address_list = CsrPmemAlloc(mc_count * sizeof(CsrWifiMacAddress));

    if (multicast_address_list == NULL) {
        return;
    }

    for (i = 0; i < mc_count; i++) {
        memcpy(multicast_address_list[i].a, mc_list, ETH_ALEN);
        mc_list += ETH_ALEN;
    }

    if (priv->smepriv == NULL) {
        kfree(multicast_address_list);
        return;
    }

    CsrWifiRouterCtrlMulticastAddressIndSend(priv->CSR_WIFI_SME_IFACEQUEUE,0,
            interfaceTag,
            CSR_WIFI_SME_LIST_ACTION_ADD,
            mc_count, multicast_address_list);

    /* The SME will take a copy of the addreses*/
    kfree(multicast_address_list);
}


int unifi_cfg_power(unifi_priv_t *priv, unsigned char *arg)
{
    unifi_cfg_power_t cfg_power;
    int rc;
    int wol;

    if (get_user(cfg_power, (unifi_cfg_power_t*)(((unifi_cfg_command_t*)arg) + 1))) {
        unifi_error(priv, "UNIFI_CFG: Failed to get the argument\n");
        return -EFAULT;
    }

    switch (cfg_power) {
        case UNIFI_CFG_POWER_OFF:
            priv->wol_suspend = (enable_wol == UNIFI_WOL_OFF) ? FALSE : TRUE;
            rc = sme_sys_suspend(priv);
            if (rc) {
                return rc;
            }
            break;
        case UNIFI_CFG_POWER_ON:
            wol = priv->wol_suspend;
            rc = sme_sys_resume(priv);
            if (rc) {
                return rc;
            }
            if (wol) {
                /* Kick the BH to ensure pending transfers are handled when
                 * a suspend happened with card powered.
                 */
                unifi_send_signal(priv->card, NULL, 0, NULL);
            }
            break;
        default:
            unifi_error(priv, "WIFI POWER: Unknown value.\n");
            return -EINVAL;
    }

    return 0;
}


int unifi_cfg_power_save(unifi_priv_t *priv, unsigned char *arg)
{
    unifi_cfg_powersave_t cfg_power_save;
    CsrWifiSmePowerConfig powerConfig;
    int rc;

    if (get_user(cfg_power_save, (unifi_cfg_powersave_t*)(((unifi_cfg_command_t*)arg) + 1))) {
        unifi_error(priv, "UNIFI_CFG: Failed to get the argument\n");
        return -EFAULT;
    }

    /* Get the coex info from the SME */
    rc = sme_mgt_power_config_get(priv, &powerConfig);
    if (rc) {
        unifi_error(priv, "UNIFI_CFG: Get unifi_PowerConfigValue failed.\n");
        return rc;
    }

    switch (cfg_power_save) {
        case UNIFI_CFG_POWERSAVE_NONE:
            powerConfig.powerSaveLevel = CSR_WIFI_SME_POWER_SAVE_LEVEL_LOW;
            break;
        case UNIFI_CFG_POWERSAVE_FAST:
            powerConfig.powerSaveLevel = CSR_WIFI_SME_POWER_SAVE_LEVEL_MED;
            break;
        case UNIFI_CFG_POWERSAVE_FULL:
            powerConfig.powerSaveLevel = CSR_WIFI_SME_POWER_SAVE_LEVEL_HIGH;
            break;
        case UNIFI_CFG_POWERSAVE_AUTO:
            powerConfig.powerSaveLevel = CSR_WIFI_SME_POWER_SAVE_LEVEL_AUTO;
            break;
        default:
            unifi_error(priv, "POWERSAVE: Unknown value.\n");
            return -EINVAL;
    }

    rc = sme_mgt_power_config_set(priv, &powerConfig);

    if (rc) {
        unifi_error(priv, "UNIFI_CFG: Set unifi_PowerConfigValue failed.\n");
    }

    return rc;
}


int unifi_cfg_power_supply(unifi_priv_t *priv, unsigned char *arg)
{
    unifi_cfg_powersupply_t cfg_power_supply;
    CsrWifiSmeHostConfig hostConfig;
    int rc;

    if (get_user(cfg_power_supply, (unifi_cfg_powersupply_t*)(((unifi_cfg_command_t*)arg) + 1))) {
        unifi_error(priv, "UNIFI_CFG: Failed to get the argument\n");
        return -EFAULT;
    }

    /* Get the coex info from the SME */
    rc = sme_mgt_host_config_get(priv, &hostConfig);
    if (rc) {
        unifi_error(priv, "UNIFI_CFG: Get unifi_HostConfigValue failed.\n");
        return rc;
    }

    switch (cfg_power_supply) {
        case UNIFI_CFG_POWERSUPPLY_MAINS:
            hostConfig.powerMode = CSR_WIFI_SME_HOST_POWER_MODE_ACTIVE;
            break;
        case UNIFI_CFG_POWERSUPPLY_BATTERIES:
            hostConfig.powerMode = CSR_WIFI_SME_HOST_POWER_MODE_POWER_SAVE;
            break;
        default:
            unifi_error(priv, "POWERSUPPLY: Unknown value.\n");
            return -EINVAL;
    }

    rc = sme_mgt_host_config_set(priv, &hostConfig);
    if (rc) {
        unifi_error(priv, "UNIFI_CFG: Set unifi_HostConfigValue failed.\n");
    }

    return rc;
}


int unifi_cfg_packet_filters(unifi_priv_t *priv, unsigned char *arg)
{
    unsigned char *tclas_buffer;
    unsigned int tclas_buffer_length;
    tclas_t *dhcp_tclas;
    int rc;

    /* Free any TCLASs previously allocated */
    if (priv->packet_filters.tclas_ies_length) {
        kfree(priv->filter_tclas_ies);
        priv->filter_tclas_ies = NULL;
    }

    tclas_buffer = ((unsigned char*)arg) + sizeof(unifi_cfg_command_t) + sizeof(unsigned int);
    if (copy_from_user(&priv->packet_filters, (void*)tclas_buffer,
                sizeof(uf_cfg_bcast_packet_filter_t))) {
        unifi_error(priv, "UNIFI_CFG: Failed to get the filter struct\n");
        return -EFAULT;
    }

    tclas_buffer_length = priv->packet_filters.tclas_ies_length;

    /* Allocate TCLASs if necessary */
    if (priv->packet_filters.dhcp_filter) {
        priv->packet_filters.tclas_ies_length += sizeof(tclas_t);
    }
    if (priv->packet_filters.tclas_ies_length > 0) {
        priv->filter_tclas_ies = CsrPmemAlloc(priv->packet_filters.tclas_ies_length);
        if (priv->filter_tclas_ies == NULL) {
            return -ENOMEM;
        }
        if (tclas_buffer_length) {
            tclas_buffer += sizeof(uf_cfg_bcast_packet_filter_t) - sizeof(unsigned char*);
            if (copy_from_user(priv->filter_tclas_ies,
                        tclas_buffer,
                        tclas_buffer_length)) {
                unifi_error(priv, "UNIFI_CFG: Failed to get the TCLAS buffer\n");
                return -EFAULT;
            }
        }
    }

    if(priv->packet_filters.dhcp_filter)
    {
        /* Append the DHCP tclas IE */
        dhcp_tclas = (tclas_t*)(priv->filter_tclas_ies + tclas_buffer_length);
        memset(dhcp_tclas, 0, sizeof(tclas_t));
        dhcp_tclas->element_id = 14;
        dhcp_tclas->length = sizeof(tcpip_clsfr_t) + 1;
        dhcp_tclas->user_priority = 0;
        dhcp_tclas->tcp_ip_cls_fr.cls_fr_type = 1;
        dhcp_tclas->tcp_ip_cls_fr.version = 4;
        ((u8*)(&dhcp_tclas->tcp_ip_cls_fr.source_port))[0] = 0x00;
        ((u8*)(&dhcp_tclas->tcp_ip_cls_fr.source_port))[1] = 0x44;
        ((u8*)(&dhcp_tclas->tcp_ip_cls_fr.dest_port))[0] = 0x00;
        ((u8*)(&dhcp_tclas->tcp_ip_cls_fr.dest_port))[1] = 0x43;
        dhcp_tclas->tcp_ip_cls_fr.protocol = 0x11;
        dhcp_tclas->tcp_ip_cls_fr.cls_fr_mask = 0x58; //bits: 3,4,6
    }

    rc = sme_mgt_packet_filter_set(priv);

    return rc;
}


int unifi_cfg_wmm_qos_info(unifi_priv_t *priv, unsigned char *arg)
{
    u8 wmm_qos_info;
    int rc = 0;

    if (get_user(wmm_qos_info, (u8*)(((unifi_cfg_command_t*)arg) + 1))) {
        unifi_error(priv, "UNIFI_CFG: Failed to get the argument\n");
        return -EFAULT;
    }

    /* Store the value in the connection info */
    priv->connection_config.wmmQosInfo = wmm_qos_info;

    return rc;
}


int unifi_cfg_wmm_addts(unifi_priv_t *priv, unsigned char *arg)
{
    u32 addts_tid;
    u8 addts_ie_length;
    u8 *addts_ie;
    u8 *addts_params;
    CsrWifiSmeDataBlock tspec;
    CsrWifiSmeDataBlock tclas;
    int rc;

    addts_params = (u8*)(((unifi_cfg_command_t*)arg) + 1);
    if (get_user(addts_tid, (u32*)addts_params)) {
        unifi_error(priv, "unifi_cfg_wmm_addts: Failed to get the argument\n");
        return -EFAULT;
    }

    addts_params += sizeof(u32);
    if (get_user(addts_ie_length, (u8*)addts_params)) {
        unifi_error(priv, "unifi_cfg_wmm_addts: Failed to get the argument\n");
        return -EFAULT;
    }

    unifi_trace(priv, UDBG4, "addts: tid = 0x%x ie_length = %d\n",
            addts_tid, addts_ie_length);

    addts_ie = CsrPmemAlloc(addts_ie_length);
    if (addts_ie == NULL) {
        unifi_error(priv,
                "unifi_cfg_wmm_addts: Failed to malloc %d bytes for addts_ie buffer\n",
                addts_ie_length);
        return -ENOMEM;
    }

    addts_params += sizeof(u8);
    rc = copy_from_user(addts_ie, addts_params, addts_ie_length);
    if (rc) {
        unifi_error(priv, "unifi_cfg_wmm_addts: Failed to get the addts buffer\n");
        kfree(addts_ie);
        return -EFAULT;
    }

    tspec.data = addts_ie;
    tspec.length = addts_ie_length;
    tclas.data = NULL;
    tclas.length = 0;

    rc = sme_mgt_tspec(priv, CSR_WIFI_SME_LIST_ACTION_ADD, addts_tid,
            &tspec, &tclas);

    kfree(addts_ie);
    return rc;
}


int unifi_cfg_wmm_delts(unifi_priv_t *priv, unsigned char *arg)
{
    u32 delts_tid;
    u8 *delts_params;
    CsrWifiSmeDataBlock tspec;
    CsrWifiSmeDataBlock tclas;
    int rc;

    delts_params = (u8*)(((unifi_cfg_command_t*)arg) + 1);
    if (get_user(delts_tid, (u32*)delts_params)) {
        unifi_error(priv, "unifi_cfg_wmm_delts: Failed to get the argument\n");
        return -EFAULT;
    }

    unifi_trace(priv, UDBG4, "delts: tid = 0x%x\n", delts_tid);

    tspec.data = tclas.data = NULL;
    tspec.length = tclas.length = 0;

    rc = sme_mgt_tspec(priv, CSR_WIFI_SME_LIST_ACTION_REMOVE, delts_tid,
            &tspec, &tclas);

    return rc;
}

int unifi_cfg_strict_draft_n(unifi_priv_t *priv, unsigned char *arg)
{
    u8 strict_draft_n;
    u8 *strict_draft_n_params;
    int rc;

    CsrWifiSmeStaConfig  staConfig;
    CsrWifiSmeDeviceConfig  deviceConfig;

    strict_draft_n_params = (u8*)(((unifi_cfg_command_t*)arg) + 1);
    if (get_user(strict_draft_n, (u8*)strict_draft_n_params)) {
        unifi_error(priv, "unifi_cfg_strict_draft_n: Failed to get the argument\n");
        return -EFAULT;
    }

    unifi_trace(priv, UDBG4, "strict_draft_n: = %s\n", ((strict_draft_n) ? "yes":"no"));

    rc = sme_mgt_sme_config_get(priv, &staConfig, &deviceConfig);

    if (rc) {
        unifi_warning(priv, "unifi_cfg_strict_draft_n: Get unifi_SMEConfigValue failed.\n");
        return -EFAULT;
    }

    deviceConfig.enableStrictDraftN = strict_draft_n;

    rc = sme_mgt_sme_config_set(priv, &staConfig, &deviceConfig);
    if (rc) {
        unifi_warning(priv, "unifi_cfg_strict_draft_n: Set unifi_SMEConfigValue failed.\n");
        rc = -EFAULT;
    }

    return rc;
}


int unifi_cfg_enable_okc(unifi_priv_t *priv, unsigned char *arg)
{
    u8 enable_okc;
    u8 *enable_okc_params;
    int rc;

    CsrWifiSmeStaConfig staConfig;
    CsrWifiSmeDeviceConfig deviceConfig;

    enable_okc_params = (u8*)(((unifi_cfg_command_t*)arg) + 1);
    if (get_user(enable_okc, (u8*)enable_okc_params)) {
        unifi_error(priv, "unifi_cfg_enable_okc: Failed to get the argument\n");
        return -EFAULT;
    }

    unifi_trace(priv, UDBG4, "enable_okc: = %s\n", ((enable_okc) ? "yes":"no"));

    rc = sme_mgt_sme_config_get(priv, &staConfig, &deviceConfig);
    if (rc) {
        unifi_warning(priv, "unifi_cfg_enable_okc: Get unifi_SMEConfigValue failed.\n");
        return -EFAULT;
    }

    staConfig.enableOpportunisticKeyCaching = enable_okc;

    rc = sme_mgt_sme_config_set(priv, &staConfig, &deviceConfig);
    if (rc) {
        unifi_warning(priv, "unifi_cfg_enable_okc: Set unifi_SMEConfigValue failed.\n");
        rc = -EFAULT;
    }

    return rc;
}


int unifi_cfg_get_info(unifi_priv_t *priv, unsigned char *arg)
{
    unifi_cfg_get_t get_cmd;
    char inst_name[IFNAMSIZ];
    int rc;

    if (get_user(get_cmd, (unifi_cfg_get_t*)(((unifi_cfg_command_t*)arg) + 1))) {
        unifi_error(priv, "UNIFI_CFG: Failed to get the argument\n");
        return -EFAULT;
    }

    switch (get_cmd) {
        case UNIFI_CFG_GET_COEX:
            {
                CsrWifiSmeCoexInfo coexInfo;
                /* Get the coex info from the SME */
                rc = sme_mgt_coex_info_get(priv, &coexInfo);
                if (rc) {
                    unifi_error(priv, "UNIFI_CFG: Get unifi_CoexInfoValue failed.\n");
                    return rc;
                }

                /* Copy the info to the out buffer */
                if (copy_to_user((void*)arg,
                            &coexInfo,
                            sizeof(CsrWifiSmeCoexInfo))) {
                    unifi_error(priv, "UNIFI_CFG: Failed to copy the coex info\n");
                    return -EFAULT;
                }
                break;
            }
        case UNIFI_CFG_GET_POWER_MODE:
            {
                CsrWifiSmePowerConfig powerConfig;
                rc = sme_mgt_power_config_get(priv, &powerConfig);
                if (rc) {
                    unifi_error(priv, "UNIFI_CFG: Get unifi_PowerConfigValue failed.\n");
                    return rc;
                }

                /* Copy the info to the out buffer */
                if (copy_to_user((void*)arg,
                            &powerConfig.powerSaveLevel,
                            sizeof(CsrWifiSmePowerSaveLevel))) {
                    unifi_error(priv, "UNIFI_CFG: Failed to copy the power save info\n");
                    return -EFAULT;
                }
                break;
            }
        case UNIFI_CFG_GET_POWER_SUPPLY:
            {
                CsrWifiSmeHostConfig hostConfig;
                rc = sme_mgt_host_config_get(priv, &hostConfig);
                if (rc) {
                    unifi_error(priv, "UNIFI_CFG: Get unifi_HostConfigValue failed.\n");
                    return rc;
                }

                /* Copy the info to the out buffer */
                if (copy_to_user((void*)arg,
                            &hostConfig.powerMode,
                            sizeof(CsrWifiSmeHostPowerMode))) {
                    unifi_error(priv, "UNIFI_CFG: Failed to copy the host power mode\n");
                    return -EFAULT;
                }
                break;
            }
        case UNIFI_CFG_GET_VERSIONS:
            break;
        case UNIFI_CFG_GET_INSTANCE:
            {
                u16 InterfaceId=0;
                uf_net_get_name(priv->netdev[InterfaceId], &inst_name[0], sizeof(inst_name));

                /* Copy the info to the out buffer */
                if (copy_to_user((void*)arg,
                            &inst_name[0],
                            sizeof(inst_name))) {
                    unifi_error(priv, "UNIFI_CFG: Failed to copy the instance name\n");
                    return -EFAULT;
                }
            }
            break;

        case UNIFI_CFG_GET_AP_CONFIG:
            {
#ifdef CSR_SUPPORT_WEXT_AP
                uf_cfg_ap_config_t cfg_ap_config;
                cfg_ap_config.channel = priv->ap_config.channel;
                cfg_ap_config.beaconInterval = priv->ap_mac_config.beaconInterval;
                cfg_ap_config.wmmEnabled = priv->ap_mac_config.wmmEnabled;
                cfg_ap_config.dtimPeriod = priv->ap_mac_config.dtimPeriod;
                cfg_ap_config.phySupportedBitmap = priv->ap_mac_config.phySupportedBitmap;
                if (copy_to_user((void*)arg,
                            &cfg_ap_config,
                            sizeof(uf_cfg_ap_config_t))) {
                    unifi_error(priv, "UNIFI_CFG: Failed to copy the AP configuration\n");
                    return -EFAULT;
                }
#else
                   return -EPERM;
#endif
            }
            break;


        default:
            unifi_error(priv, "unifi_cfg_get_info: Unknown value.\n");
            return -EINVAL;
    }

    return 0;
}
#ifdef CSR_SUPPORT_WEXT_AP
int
 uf_configure_supported_rates(u8 * supportedRates, u8 phySupportedBitmap)
{
    int i=0;
    u8 b=FALSE, g = FALSE, n = FALSE;
    b = phySupportedBitmap & CSR_WIFI_SME_AP_PHY_SUPPORT_B;
    n = phySupportedBitmap & CSR_WIFI_SME_AP_PHY_SUPPORT_N;
    g = phySupportedBitmap & CSR_WIFI_SME_AP_PHY_SUPPORT_G;
    if(b || g) {
        supportedRates[i++]=0x82;
        supportedRates[i++]=0x84;
        supportedRates[i++]=0x8b;
        supportedRates[i++]=0x96;
    } else if(n) {
        /* For some strange reasons WiFi stack needs both b and g rates*/
        supportedRates[i++]=0x02;
        supportedRates[i++]=0x04;
        supportedRates[i++]=0x0b;
        supportedRates[i++]=0x16;
        supportedRates[i++]=0x0c;
        supportedRates[i++]=0x12;
        supportedRates[i++]=0x18;
	supportedRates[i++]=0x24;
        supportedRates[i++]=0x30;
        supportedRates[i++]=0x48;
        supportedRates[i++]=0x60;
        supportedRates[i++]=0x6c;
    }
    if(g) {
        if(!b) {
            supportedRates[i++]=0x8c;
            supportedRates[i++]=0x98;
            supportedRates[i++]=0xb0;
        } else {
            supportedRates[i++]=0x0c;
            supportedRates[i++]=0x18;
            supportedRates[i++]=0x30;
        }
        supportedRates[i++]=0x48;
        supportedRates[i++]=0x12;
        supportedRates[i++]=0x24;
        supportedRates[i++]=0x60;
        supportedRates[i++]=0x6c;
    }
    return i;
}
int unifi_cfg_set_ap_config(unifi_priv_t * priv,unsigned char* arg)
{
    uf_cfg_ap_config_t cfg_ap_config;
    char *buffer;

    buffer = ((unsigned char*)arg) + sizeof(unifi_cfg_command_t) + sizeof(unsigned int);
    if (copy_from_user(&cfg_ap_config, (void*)buffer,
                sizeof(uf_cfg_ap_config_t))) {
        unifi_error(priv, "UNIFI_CFG: Failed to get the ap config struct\n");
        return -EFAULT;
    }
    priv->ap_config.channel = cfg_ap_config.channel;
    priv->ap_mac_config.dtimPeriod = cfg_ap_config.dtimPeriod;
    priv->ap_mac_config.beaconInterval = cfg_ap_config.beaconInterval;
    priv->group_sec_config.apGroupkeyTimeout = cfg_ap_config.groupkeyTimeout;
    priv->group_sec_config.apStrictGtkRekey = cfg_ap_config.strictGtkRekeyEnabled;
    priv->group_sec_config.apGmkTimeout = cfg_ap_config.gmkTimeout;
    priv->group_sec_config.apResponseTimeout = cfg_ap_config.responseTimeout;
    priv->group_sec_config.apRetransLimit = cfg_ap_config.retransLimit;

    priv->ap_mac_config.shortSlotTimeEnabled = cfg_ap_config.shortSlotTimeEnabled;
    priv->ap_mac_config.ctsProtectionType=cfg_ap_config.ctsProtectionType;

    priv->ap_mac_config.wmmEnabled = cfg_ap_config.wmmEnabled;

    priv->ap_mac_config.apHtParams.rxStbc=cfg_ap_config.rxStbc;
    priv->ap_mac_config.apHtParams.rifsModeAllowed=cfg_ap_config.rifsModeAllowed;

    priv->ap_mac_config.phySupportedBitmap = cfg_ap_config.phySupportedBitmap;
    priv->ap_mac_config.maxListenInterval=cfg_ap_config.maxListenInterval;

    priv->ap_mac_config.supportedRatesCount=     uf_configure_supported_rates(priv->ap_mac_config.supportedRates,priv->ap_mac_config.phySupportedBitmap);

    return 0;
}

#endif
#ifdef CSR_SUPPORT_WEXT

    void
uf_sme_config_wq(struct work_struct *work)
{
    CsrWifiSmeStaConfig  staConfig;
    CsrWifiSmeDeviceConfig  deviceConfig;
    unifi_priv_t *priv = container_of(work, unifi_priv_t, sme_config_task);

    /* Register to receive indications from the SME */
    CsrWifiSmeEventMaskSetReqSend(0,
            CSR_WIFI_SME_INDICATIONS_WIFIOFF | CSR_WIFI_SME_INDICATIONS_CONNECTIONQUALITY |
            CSR_WIFI_SME_INDICATIONS_MEDIASTATUS | CSR_WIFI_SME_INDICATIONS_MICFAILURE);

    if (sme_mgt_sme_config_get(priv, &staConfig, &deviceConfig)) {
        unifi_warning(priv, "uf_sme_config_wq: Get unifi_SMEConfigValue failed.\n");
        return;
    }

    if (priv->if_index == CSR_INDEX_5G) {
        staConfig.ifIndex = CSR_WIFI_SME_RADIO_IF_GHZ_5_0;
    } else {
        staConfig.ifIndex = CSR_WIFI_SME_RADIO_IF_GHZ_2_4;
    }

    deviceConfig.trustLevel = (CsrWifiSme80211dTrustLevel)tl_80211d;
    if (sme_mgt_sme_config_set(priv, &staConfig, &deviceConfig)) {
        unifi_warning(priv,
                "SME config for 802.11d Trust Level and Radio Band failed.\n");
        return;
    }

} /* uf_sme_config_wq() */

#endif /* CSR_SUPPORT_WEXT */


/*
 * ---------------------------------------------------------------------------
 *  uf_ta_ind_wq
 *
 *      Deferred work queue function to send Traffic Analysis protocols
 *      indications to the SME.
 *      These are done in a deferred work queue for two reasons:
 *       - the CsrWifiRouterCtrl...Send() functions are not safe for atomic context
 *       - we want to load the main driver data path as lightly as possible
 *
 *      The TA classifications already come from a workqueue.
 *
 *  Arguments:
 *      work    Pointer to work queue item.
 *
 *  Returns:
 *      None.
 * ---------------------------------------------------------------------------
 */
    void
uf_ta_ind_wq(struct work_struct *work)
{
    struct ta_ind *ind = container_of(work, struct ta_ind, task);
    unifi_priv_t *priv = container_of(ind, unifi_priv_t, ta_ind_work);
    u16 interfaceTag = 0;


    CsrWifiRouterCtrlTrafficProtocolIndSend(priv->CSR_WIFI_SME_IFACEQUEUE,0,
            interfaceTag,
            ind->packet_type,
            ind->direction,
            ind->src_addr);
    ind->in_use = 0;

} /* uf_ta_ind_wq() */


/*
 * ---------------------------------------------------------------------------
 *  uf_ta_sample_ind_wq
 *
 *      Deferred work queue function to send Traffic Analysis sample
 *      indications to the SME.
 *      These are done in a deferred work queue for two reasons:
 *       - the CsrWifiRouterCtrl...Send() functions are not safe for atomic context
 *       - we want to load the main driver data path as lightly as possible
 *
 *      The TA classifications already come from a workqueue.
 *
 *  Arguments:
 *      work    Pointer to work queue item.
 *
 *  Returns:
 *      None.
 * ---------------------------------------------------------------------------
 */
    void
uf_ta_sample_ind_wq(struct work_struct *work)
{
    struct ta_sample_ind *ind = container_of(work, struct ta_sample_ind, task);
    unifi_priv_t *priv = container_of(ind, unifi_priv_t, ta_sample_ind_work);
    u16 interfaceTag = 0;

     unifi_trace(priv, UDBG5, "rxtcp %d txtcp %d rxudp %d txudp %d prio %d\n",
        priv->rxTcpThroughput,
        priv->txTcpThroughput,
        priv->rxUdpThroughput,
        priv->txUdpThroughput,
        priv->bh_thread.prio);

    if(priv->rxTcpThroughput > 1000)
    {
        if (bh_priority == -1 && priv->bh_thread.prio != 1)
        {
            struct sched_param param;
            priv->bh_thread.prio = 1;
            unifi_trace(priv, UDBG1, "%s new thread (RT) priority = %d\n",
                        priv->bh_thread.name, priv->bh_thread.prio);
            param.sched_priority = priv->bh_thread.prio;
            sched_setscheduler(priv->bh_thread.thread_task, SCHED_FIFO, &param);
        }
    } else
    {
        if (bh_priority == -1 && priv->bh_thread.prio != DEFAULT_PRIO)
        {
            struct sched_param param;
            param.sched_priority = 0;
            sched_setscheduler(priv->bh_thread.thread_task, SCHED_NORMAL, &param);
            priv->bh_thread.prio = DEFAULT_PRIO;
            unifi_trace(priv, UDBG1, "%s new thread priority = %d\n",
                        priv->bh_thread.name, priv->bh_thread.prio);
            set_user_nice(priv->bh_thread.thread_task, PRIO_TO_NICE(priv->bh_thread.prio));
        }
    }

    CsrWifiRouterCtrlTrafficSampleIndSend(priv->CSR_WIFI_SME_IFACEQUEUE,0, interfaceTag, ind->stats);

    ind->in_use = 0;

} /* uf_ta_sample_ind_wq() */


/*
 * ---------------------------------------------------------------------------
 *  uf_send_m4_ready_wq
 *
 *      Deferred work queue function to send M4 ReadyToSend inds to the SME.
 *      These are done in a deferred work queue for two reasons:
 *       - the CsrWifiRouterCtrl...Send() functions are not safe for atomic context
 *       - we want to load the main driver data path as lightly as possible
 *
 *  Arguments:
 *      work    Pointer to work queue item.
 *
 *  Returns:
 *      None.
 * ---------------------------------------------------------------------------
 */
void
uf_send_m4_ready_wq(struct work_struct *work)
{
    netInterface_priv_t *InterfacePriv = container_of(work, netInterface_priv_t, send_m4_ready_task);
    u16 iface = InterfacePriv->InterfaceTag;
    unifi_priv_t *priv = InterfacePriv->privPtr;
    CSR_MA_PACKET_REQUEST *req = &InterfacePriv->m4_signal.u.MaPacketRequest;
    CsrWifiMacAddress peer;
    unsigned long flags;

    func_enter();

    /* The peer address was stored in the signal */
    spin_lock_irqsave(&priv->m4_lock, flags);
    memcpy(peer.a, req->Ra.x, sizeof(peer.a));
    spin_unlock_irqrestore(&priv->m4_lock, flags);

    /* Send a signal to SME */
    CsrWifiRouterCtrlM4ReadyToSendIndSend(priv->CSR_WIFI_SME_IFACEQUEUE, 0, iface, peer);

	unifi_trace(priv, UDBG1, "M4ReadyToSendInd sent for peer %pMF\n",
		peer.a);

    func_exit();

} /* uf_send_m4_ready_wq() */

#if (defined(CSR_WIFI_SECURITY_WAPI_ENABLE) && defined(CSR_WIFI_SECURITY_WAPI_SW_ENCRYPTION))
/*
 * ---------------------------------------------------------------------------
 *  uf_send_pkt_to_encrypt
 *
 *      Deferred work queue function to send the WAPI data pkts to SME when unicast KeyId = 1
 *      These are done in a deferred work queue for two reasons:
 *       - the CsrWifiRouterCtrl...Send() functions are not safe for atomic context
 *       - we want to load the main driver data path as lightly as possible
 *
 *  Arguments:
 *      work    Pointer to work queue item.
 *
 *  Returns:
 *      None.
 * ---------------------------------------------------------------------------
 */
void uf_send_pkt_to_encrypt(struct work_struct *work)
{
    netInterface_priv_t *interfacePriv = container_of(work, netInterface_priv_t, send_pkt_to_encrypt);
    u16 interfaceTag = interfacePriv->InterfaceTag;
    unifi_priv_t *priv = interfacePriv->privPtr;

    u32 pktBulkDataLength;
    u8 *pktBulkData;
    unsigned long flags;

    if (interfacePriv->interfaceMode == CSR_WIFI_ROUTER_CTRL_MODE_STA) {

        func_enter();

        pktBulkDataLength = interfacePriv->wapi_unicast_bulk_data.data_length;

        if (pktBulkDataLength > 0) {
		    pktBulkData = (u8 *)CsrPmemAlloc(pktBulkDataLength);
		    memset(pktBulkData, 0, pktBulkDataLength);
	    } else {
		    unifi_error(priv, "uf_send_pkt_to_encrypt() : invalid buffer\n");
		    return;
	    }

        spin_lock_irqsave(&priv->wapi_lock, flags);
        /* Copy over the MA PKT REQ bulk data */
        memcpy(pktBulkData, (u8*)interfacePriv->wapi_unicast_bulk_data.os_data_ptr, pktBulkDataLength);
        /* Free any bulk data buffers allocated for the WAPI Data pkt */
        unifi_net_data_free(priv, &interfacePriv->wapi_unicast_bulk_data);
        interfacePriv->wapi_unicast_bulk_data.net_buf_length = 0;
        interfacePriv->wapi_unicast_bulk_data.data_length = 0;
        interfacePriv->wapi_unicast_bulk_data.os_data_ptr = interfacePriv->wapi_unicast_bulk_data.os_net_buf_ptr = NULL;
        spin_unlock_irqrestore(&priv->wapi_lock, flags);

        CsrWifiRouterCtrlWapiUnicastTxEncryptIndSend(priv->CSR_WIFI_SME_IFACEQUEUE, 0, interfaceTag, pktBulkDataLength, pktBulkData);
        unifi_trace(priv, UDBG1, "WapiUnicastTxEncryptInd sent to SME\n");

        kfree(pktBulkData); /* Would have been copied over by the SME Handler */

        func_exit();
    } else {
	    unifi_warning(priv, "uf_send_pkt_to_encrypt() is NOT applicable for interface mode - %d\n",interfacePriv->interfaceMode);
    }
}/* uf_send_pkt_to_encrypt() */
#endif
