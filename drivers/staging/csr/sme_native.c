/*
 * ***************************************************************************
 *
 *  FILE:     sme_native.c
 *
 * Copyright (C) 2005-2009 by Cambridge Silicon Radio Ltd.
 *
 * Refer to LICENSE.txt included with this source code for details on
 * the license terms.
 *
 * ***************************************************************************
 */

#include <linux/netdevice.h>
#include "unifi_priv.h"
#include "csr_wifi_hip_unifi.h"
#include "csr_wifi_hip_conversions.h"

static const unsigned char wildcard_address[ETH_ALEN] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

int
uf_sme_init(unifi_priv_t *priv)
{
    sema_init(&priv->mlme_blocking_mutex, 1);

#ifdef CSR_SUPPORT_WEXT
    {
        int r = uf_init_wext_interface(priv);
        if (r != 0) {
            return r;
        }
    }
#endif

    return 0;
} /* uf_sme_init() */


void
uf_sme_deinit(unifi_priv_t *priv)
{

    /* Free memory allocated for the scan table */
/*    unifi_clear_scan_table(priv); */

    /* Cancel any pending workqueue tasks */
    flush_workqueue(priv->unifi_workqueue);

#ifdef CSR_SUPPORT_WEXT
    uf_deinit_wext_interface(priv);
#endif

} /* uf_sme_deinit() */


int sme_mgt_wifi_on(unifi_priv_t *priv)
{
    int r, i;
    s32 csrResult;

    if (priv == NULL) {
        return -EINVAL;
    }
    /* Initialize the interface mode to None */
    for (i=0; i<CSR_WIFI_NUM_INTERFACES; i++) {
        priv->interfacePriv[i]->interfaceMode = 0;
    }

    /* Set up interface mode so that get_packet_priority() can
     * select the right QOS priority when WMM is enabled.
     */
    priv->interfacePriv[0]->interfaceMode = CSR_WIFI_ROUTER_CTRL_MODE_STA;

    r = uf_request_firmware_files(priv, UNIFI_FW_STA);
    if (r) {
        unifi_error(priv, "sme_mgt_wifi_on: Failed to get f/w\n");
        return r;
    }

    /*
     * The request to initialise UniFi might come while UniFi is running.
     * We need to block all I/O activity until the reset completes, otherwise
     * an SDIO error might occur resulting an indication to the SME which
     * makes it think that the initialisation has failed.
     */
    priv->bh_thread.block_thread = 1;

    /* Power on UniFi */
    CsrSdioClaim(priv->sdio);
    csrResult = CsrSdioPowerOn(priv->sdio);
    CsrSdioRelease(priv->sdio);
    if(csrResult != CSR_RESULT_SUCCESS && csrResult != CSR_SDIO_RESULT_NOT_RESET) {
        return -EIO;
    }

    if (csrResult == CSR_RESULT_SUCCESS) {
        /* Initialise UniFi hardware */
        r = uf_init_hw(priv);
        if (r) {
            return r;
        }
    }

    /* Re-enable the I/O thread */
    priv->bh_thread.block_thread = 0;

    /* Disable deep sleep signalling during the firmware initialisation, to
     * prevent the wakeup mechanism raising the SDIO clock beyond INIT before
     * the first MLME-RESET.ind. It gets re-enabled at the CONNECTED.ind,
     * immediately after the MLME-RESET.ind
     */
    csrResult = unifi_configure_low_power_mode(priv->card,
                                           UNIFI_LOW_POWER_DISABLED,
                                           UNIFI_PERIODIC_WAKE_HOST_DISABLED);
    if (csrResult != CSR_RESULT_SUCCESS) {
        unifi_warning(priv,
                      "sme_mgt_wifi_on: unifi_configure_low_power_mode() returned an error\n");
    }


    /* Start the I/O thread */
    CsrSdioClaim(priv->sdio);
    r = uf_init_bh(priv);
    if (r) {
        CsrSdioPowerOff(priv->sdio);
        CsrSdioRelease(priv->sdio);
        return r;
    }
    CsrSdioRelease(priv->sdio);

    priv->init_progress = UNIFI_INIT_FW_DOWNLOADED;

    return 0;
}

int
sme_sys_suspend(unifi_priv_t *priv)
{
    const int interfaceNum = 0;     /* FIXME */
    CsrResult csrResult;

    /* Abort any pending requests. */
    uf_abort_mlme(priv);

    /* Allow our mlme request to go through. */
    priv->io_aborted = 0;

    /* Send MLME-RESET.req to UniFi. */
    unifi_reset_state(priv, priv->netdev[interfaceNum]->dev_addr, 0);

    /* Stop the network traffic */
    netif_carrier_off(priv->netdev[interfaceNum]);

    /* Put UniFi to deep sleep */
    CsrSdioClaim(priv->sdio);
    csrResult = unifi_force_low_power_mode(priv->card);
    CsrSdioRelease(priv->sdio);

    return 0;
} /* sme_sys_suspend() */


int
sme_sys_resume(unifi_priv_t *priv)
{
#ifdef CSR_SUPPORT_WEXT
    /* Send disconnect event so clients will re-initialise connection. */
    memset(priv->wext_conf.current_ssid, 0, UNIFI_MAX_SSID_LEN);
    memset((void*)priv->wext_conf.current_bssid, 0, ETH_ALEN);
    priv->wext_conf.capability = 0;
    wext_send_disassoc_event(priv);
#endif
    return 0;
} /* sme_sys_resume() */


/*
 * ---------------------------------------------------------------------------
 *  sme_native_log_event
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
sme_native_log_event(ul_client_t *pcli,
                     const u8 *sig_packed, int sig_len,
                     const bulk_data_param_t *bulkdata,
                     int dir)
{
    unifi_priv_t *priv;
    udi_log_t *logptr;
    u8 *p;
    int i, r;
    int signal_len;
    int total_len;
    udi_msg_t *msgptr;
    CSR_SIGNAL signal;
    ul_client_t *client = pcli;

    if (client == NULL) {
        unifi_error(NULL, "sme_native_log_event: client has exited\n");
        return;
    }

    priv = uf_find_instance(client->instance);
    if (!priv) {
        unifi_error(priv, "invalid priv\n");
        return;
    }

    /* Just a sanity check */
    if ((sig_packed == NULL) || (sig_len <= 0)) {
        return;
    }

    /* Get the unpacked signal */
    r = read_unpack_signal(sig_packed, &signal);
    if (r == 0) {
        signal_len = SigGetSize(&signal);
    } else {
        u16 receiver_id = CSR_GET_UINT16_FROM_LITTLE_ENDIAN((sig_packed) + sizeof(u16)) & 0xFF00;

        /* The control indications are 1 byte, pass them to client. */
        if (sig_len == 1) {
            unifi_trace(priv, UDBG5,
                        "Control indication (0x%x) for native SME.\n",
                        *sig_packed);

            *(u8*)&signal = *sig_packed;
            signal_len = sig_len;
        } else if (receiver_id == 0) {
            /*
             * Also "unknown" signals with a ReceiverId of 0 are passed to the client
             * without unpacking. (This is a code size optimisation to allow signals
             * that the driver not interested in to be dropped from the unpack code).
             */
            unifi_trace(priv, UDBG5,
                        "Signal 0x%.4X with ReceiverId 0 for native SME.\n",
                        CSR_GET_UINT16_FROM_LITTLE_ENDIAN(sig_packed));

            *(u8*)&signal = *sig_packed;
            signal_len = sig_len;
        } else {
            unifi_error(priv,
                        "sme_native_log_event - Received unknown signal 0x%.4X.\n",
                        CSR_GET_UINT16_FROM_LITTLE_ENDIAN(sig_packed));
            return;
        }
    }

    unifi_trace(priv, UDBG3, "sme_native_log_event: signal 0x%.4X for %d\n",
                signal.SignalPrimitiveHeader.SignalId,
                client->client_id);

    total_len = signal_len;
    /* Calculate the buffer we need to store signal plus bulk data */
    for (i = 0; i < UNIFI_MAX_DATA_REFERENCES; i++) {
        total_len += bulkdata->d[i].data_length;
    }

    /* Allocate log structure plus actual signal. */
    logptr = kmalloc(sizeof(udi_log_t) + total_len, GFP_KERNEL);

    if (logptr == NULL) {
        unifi_error(priv,
                    "Failed to allocate %d bytes for a UDI log record\n",
                    sizeof(udi_log_t) + total_len);
        return;
    }

    /* Fill in udi_log struct */
    INIT_LIST_HEAD(&logptr->q);
    msgptr = &logptr->msg;
    msgptr->length = sizeof(udi_msg_t) + total_len;
    msgptr->timestamp = jiffies_to_msecs(jiffies);
    msgptr->direction = dir;
    msgptr->signal_length = signal_len;

    /* Copy signal and bulk data to the log */
    p = (u8 *)(msgptr + 1);
    memcpy(p, &signal, signal_len);
    p += signal_len;

    /* Append any bulk data */
    for (i = 0; i < UNIFI_MAX_DATA_REFERENCES; i++) {
        int len = bulkdata->d[i].data_length;

        /*
         * Len here might not be the same as the length in the bulk data slot.
         * The slot length will always be even, but len could be odd.
         */
        if (len > 0) {
            if (bulkdata->d[i].os_data_ptr) {
                memcpy(p, bulkdata->d[i].os_data_ptr, len);
            } else {
                memset(p, 0, len);
            }
            p += len;
        }
    }

    /* Add to tail of log queue */
    down(&client->udi_sem);
    list_add_tail(&logptr->q, &client->udi_log);
    up(&client->udi_sem);

    /* Wake any waiting user process */
    wake_up_interruptible(&client->udi_wq);

} /* sme_native_log_event() */


/*
 * ---------------------------------------------------------------------------
 *  unifi_ta_indicate_protocol
 *
 *      Report that a packet of a particular type has been seen
 *
 *  Arguments:
 *      drv_priv        The device context pointer passed to ta_init.
 *      protocol        The protocol type enum value.
 *      direction       Whether the packet was a tx or rx.
 *      src_addr        The source MAC address from the data packet.
 *
 *  Returns:
 *      None.
 *
 *  Notes:
 *      We defer the actual sending to a background workqueue,
 *      see uf_ta_ind_wq().
 * ---------------------------------------------------------------------------
 */
void
unifi_ta_indicate_protocol(void *ospriv,
                           CsrWifiRouterCtrlTrafficPacketType packet_type,
                           CsrWifiRouterCtrlProtocolDirection direction,
                           const CsrWifiMacAddress *src_addr)
{

} /* unifi_ta_indicate_protocol */

/*
 * ---------------------------------------------------------------------------
 * unifi_ta_indicate_sampling
 *
 *      Send the TA sampling information to the SME.
 *
 *  Arguments:
 *      drv_priv        The device context pointer passed to ta_init.
 *      stats           The TA sampling data to send.
 *
 *  Returns:
 *      None.
 * ---------------------------------------------------------------------------
 */
void
unifi_ta_indicate_sampling(void *ospriv, CsrWifiRouterCtrlTrafficStats *stats)
{

} /* unifi_ta_indicate_sampling() */


void
unifi_ta_indicate_l4stats(void *ospriv,
                            u32 rxTcpThroughput,
                            u32 txTcpThroughput,
                            u32 rxUdpThroughput,
                            u32 txUdpThroughput)
{

} /* unifi_ta_indicate_l4stats() */

/*
 * ---------------------------------------------------------------------------
 * uf_native_process_udi_signal
 *
 *      Process interesting signals from the UDI interface.
 *
 *  Arguments:
 *      pcli            A pointer to the client instance.
 *      signal          Pointer to the received signal.
 *      signal_len      Size of the signal structure in bytes.
 *      bulkdata        Pointers to any associated bulk data.
 *      dir             Direction of the signal. Zero means from host,
 *                      non-zero means to host.
 *
 *
 *  Returns:
 *      None.
 * ---------------------------------------------------------------------------
 */
void
uf_native_process_udi_signal(ul_client_t *pcli,
                             const u8 *packed_signal, int packed_signal_len,
                             const bulk_data_param_t *bulkdata, int dir)
{

} /* uf_native_process_udi_signal() */


/*
 * ---------------------------------------------------------------------------
 *  sme_native_mlme_event_handler
 *
 *      Callback function to be used as the udi_event_callback when registering
 *      as a client.
 *      This function implements a blocking request-reply interface for WEXT.
 *      To use it, a client specifies this function as the udi_event_callback
 *      to ul_register_client(). The signal dispatcher in
 *      unifi_receive_event() will call this function to deliver a signal.
 *
 *  Arguments:
 *      pcli            Pointer to the client instance.
 *      signal          Pointer to the received signal.
 *      signal_len      Size of the signal structure in bytes.
 *      bulkdata        Pointer to structure containing any associated bulk data.
 *      dir             Direction of the signal. Zero means from host,
 *                      non-zero means to host.
 *
 *  Returns:
 *      None.
 * ---------------------------------------------------------------------------
 */
void
sme_native_mlme_event_handler(ul_client_t *pcli,
                              const u8 *sig_packed, int sig_len,
                              const bulk_data_param_t *bulkdata,
                              int dir)
{
    CSR_SIGNAL signal;
    int signal_len;
    unifi_priv_t *priv = uf_find_instance(pcli->instance);
    int id, r;

    /* Just a sanity check */
    if ((sig_packed == NULL) || (sig_len <= 0)) {
        return;
    }

    /* Get the unpacked signal */
    r = read_unpack_signal(sig_packed, &signal);
    if (r == 0) {
        signal_len = SigGetSize(&signal);
    } else {
        unifi_error(priv,
                    "sme_native_mlme_event_handler - Received unknown signal 0x%.4X.\n",
                    CSR_GET_UINT16_FROM_LITTLE_ENDIAN(sig_packed));
        return;
    }

    id = signal.SignalPrimitiveHeader.SignalId;
    unifi_trace(priv, UDBG4, "wext - Process signal 0x%.4X\n", id);

    /*
     * Take the appropriate action for the signal.
     */
    switch (id) {
        /*
         * Confirm replies from UniFi.
         * These all have zero or one CSR_DATAREF member. (FIXME: check this is still true for softmac)
         */
        case CSR_MA_PACKET_CONFIRM_ID:
        case CSR_MLME_RESET_CONFIRM_ID:
        case CSR_MLME_GET_CONFIRM_ID:
        case CSR_MLME_SET_CONFIRM_ID:
        case CSR_MLME_GET_NEXT_CONFIRM_ID:
        case CSR_MLME_POWERMGT_CONFIRM_ID:
        case CSR_MLME_SCAN_CONFIRM_ID:
        case CSR_MLME_HL_SYNC_CONFIRM_ID:
        case CSR_MLME_MEASURE_CONFIRM_ID:
        case CSR_MLME_SETKEYS_CONFIRM_ID:
        case CSR_MLME_DELETEKEYS_CONFIRM_ID:
        case CSR_MLME_HL_SYNC_CANCEL_CONFIRM_ID:
        case CSR_MLME_ADD_PERIODIC_CONFIRM_ID:
        case CSR_MLME_DEL_PERIODIC_CONFIRM_ID:
        case CSR_MLME_ADD_AUTONOMOUS_SCAN_CONFIRM_ID:
        case CSR_MLME_DEL_AUTONOMOUS_SCAN_CONFIRM_ID:
        case CSR_MLME_SET_PACKET_FILTER_CONFIRM_ID:
        case CSR_MLME_STOP_MEASURE_CONFIRM_ID:
        case CSR_MLME_PAUSE_AUTONOMOUS_SCAN_CONFIRM_ID:
        case CSR_MLME_ADD_TRIGGERED_GET_CONFIRM_ID:
        case CSR_MLME_DEL_TRIGGERED_GET_CONFIRM_ID:
        case CSR_MLME_ADD_BLACKOUT_CONFIRM_ID:
        case CSR_MLME_DEL_BLACKOUT_CONFIRM_ID:
        case CSR_MLME_ADD_RX_TRIGGER_CONFIRM_ID:
        case CSR_MLME_DEL_RX_TRIGGER_CONFIRM_ID:
        case CSR_MLME_CONNECT_STATUS_CONFIRM_ID:
        case CSR_MLME_MODIFY_BSS_PARAMETER_CONFIRM_ID:
        case CSR_MLME_ADD_TEMPLATE_CONFIRM_ID:
        case CSR_MLME_CONFIG_QUEUE_CONFIRM_ID:
        case CSR_MLME_ADD_TSPEC_CONFIRM_ID:
        case CSR_MLME_DEL_TSPEC_CONFIRM_ID:
        case CSR_MLME_START_AGGREGATION_CONFIRM_ID:
        case CSR_MLME_STOP_AGGREGATION_CONFIRM_ID:
        case CSR_MLME_SM_START_CONFIRM_ID:
        case CSR_MLME_LEAVE_CONFIRM_ID:
        case CSR_MLME_SET_TIM_CONFIRM_ID:
        case CSR_MLME_GET_KEY_SEQUENCE_CONFIRM_ID:
        case CSR_MLME_SET_CHANNEL_CONFIRM_ID:
        case CSR_MLME_ADD_MULTICAST_ADDRESS_CONFIRM_ID:
        case CSR_DEBUG_GENERIC_CONFIRM_ID:
            unifi_mlme_copy_reply_and_wakeup_client(pcli, &signal, signal_len, bulkdata);
            break;

        case CSR_MLME_CONNECTED_INDICATION_ID:
            /* We currently ignore the connected-ind for softmac f/w development */
            unifi_info(priv, "CSR_MLME_CONNECTED_INDICATION_ID ignored\n");
            break;

        default:
            break;
    }

} /* sme_native_mlme_event_handler() */



/*
 * -------------------------------------------------------------------------
 *  unifi_reset_state
 *
 *      Ensure that a MAC address has been set.
 *      Send the MLME-RESET signal.
 *      This must be called at least once before starting to do any
 *      network activities (e.g. scan, join etc).
 *
 * Arguments:
 *      priv            Pointer to device private context struct
 *      macaddr         Pointer to chip MAC address.
 *                      If this is FF:FF:FF:FF:FF:FF it will be replaced
 *                      with the MAC address from the chip.
 *      set_default_mib 1 if the f/w must reset the MIB to the default values
 *                      0 otherwise
 *
 * Returns:
 *      0 on success, an error code otherwise.
 * -------------------------------------------------------------------------
 */
int
unifi_reset_state(unifi_priv_t *priv, unsigned char *macaddr,
                  unsigned char set_default_mib)
{
    int r = 0;

#ifdef CSR_SUPPORT_WEXT
    /* The reset clears any 802.11 association. */
    priv->wext_conf.flag_associated = 0;
#endif

    return r;
} /* unifi_reset_state() */

