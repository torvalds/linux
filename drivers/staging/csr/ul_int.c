/*
 * ***************************************************************************
 *  FILE:     ul_int.c
 *
 *  PURPOSE:
 *      Manage list of client applications using UniFi.
 *
 * Copyright (C) 2006-2009 by Cambridge Silicon Radio Ltd.
 *
 * Refer to LICENSE.txt included with this source code for details on
 * the license terms.
 *
 * ***************************************************************************
 */
#include "csr_wifi_hip_unifi.h"
#include "csr_wifi_hip_conversions.h"
#include "unifi_priv.h"
#include "unifiio.h"
#include "unifi_os.h"

static void free_bulkdata_buffers(unifi_priv_t *priv, bulk_data_param_t *bulkdata);
static void reset_driver_status(unifi_priv_t *priv);

/*
 * ---------------------------------------------------------------------------
 *  ul_init_clients
 *
 *      Initialise the clients array to empty.
 *
 *  Arguments:
 *      priv            Pointer to device private context struct
 *
 *  Returns:
 *      None.
 *
 *  Notes:
 *      This function needs to be called before priv is stored in
 *      Unifi_instances[].
 * ---------------------------------------------------------------------------
 */
void
ul_init_clients(unifi_priv_t *priv)
{
    int id;
    ul_client_t *ul_clients;

    sema_init(&priv->udi_logging_mutex, 1);
    priv->logging_client = NULL;

    ul_clients = priv->ul_clients;

    for (id = 0; id < MAX_UDI_CLIENTS; id++) {
        memset(&ul_clients[id], 0, sizeof(ul_client_t));

        ul_clients[id].client_id = id;
        ul_clients[id].sender_id = UDI_SENDER_ID_BASE + (id << UDI_SENDER_ID_SHIFT);
        ul_clients[id].instance = -1;
        ul_clients[id].event_hook = NULL;

        INIT_LIST_HEAD(&ul_clients[id].udi_log);
        init_waitqueue_head(&ul_clients[id].udi_wq);
        sema_init(&ul_clients[id].udi_sem, 1);

        ul_clients[id].wake_up_wq_id = 0;
        ul_clients[id].seq_no = 0;
        ul_clients[id].wake_seq_no = 0;
        ul_clients[id].snap_filter.count = 0;
    }
} /* ul_init_clients() */


/*
 * ---------------------------------------------------------------------------
 *  ul_register_client
 *
 *      This function registers a new ul client.
 *
 *  Arguments:
 *      priv            Pointer to device private context struct
 *      configuration   Special configuration for the client.
 *      udi_event_clbk  Callback for receiving event from unifi.
 *
 *  Returns:
 *      0 if a new clients is registered, -1 otherwise.
 * ---------------------------------------------------------------------------
 */
ul_client_t *
ul_register_client(unifi_priv_t *priv, unsigned int configuration,
                   udi_event_t udi_event_clbk)
{
    unsigned char id, ref;
    ul_client_t *ul_clients;

    ul_clients = priv->ul_clients;

    /* check for an unused entry */
    for (id = 0; id < MAX_UDI_CLIENTS; id++) {
        if (ul_clients[id].udi_enabled == 0) {
            ul_clients[id].instance = priv->instance;
            ul_clients[id].udi_enabled = 1;
            ul_clients[id].configuration = configuration;

            /* Allocate memory for the reply signal.. */
            ul_clients[id].reply_signal = kmalloc(sizeof(CSR_SIGNAL), GFP_KERNEL);
            if (ul_clients[id].reply_signal == NULL) {
                unifi_error(priv, "Failed to allocate reply signal for client.\n");
                return NULL;
            }
            /* .. and the bulk data of the reply signal. */
            for (ref = 0; ref < UNIFI_MAX_DATA_REFERENCES; ref ++) {
                ul_clients[id].reply_bulkdata[ref] = kmalloc(sizeof(bulk_data_t), GFP_KERNEL);
                /* If allocation fails, free allocated memory. */
                if (ul_clients[id].reply_bulkdata[ref] == NULL) {
                    for (; ref > 0; ref --) {
                        kfree(ul_clients[id].reply_bulkdata[ref - 1]);
                    }
                    kfree(ul_clients[id].reply_signal);
                    unifi_error(priv, "Failed to allocate bulk data buffers for client.\n");
                    return NULL;
                }
            }

            /* Set the event callback. */
            ul_clients[id].event_hook = udi_event_clbk;

            unifi_trace(priv, UDBG2, "UDI %d (0x%x) registered. configuration = 0x%x\n",
                        id, &ul_clients[id], configuration);
            return &ul_clients[id];
        }
    }
    return NULL;
} /* ul_register_client() */


/*
 * ---------------------------------------------------------------------------
 *  ul_deregister_client
 *
 *      This function deregisters a blocking UDI client.
 *
 *  Arguments:
 *      client      Pointer to the client we deregister.
 *
 *  Returns:
 *      0 if a new clients is deregistered.
 * ---------------------------------------------------------------------------
 */
int
ul_deregister_client(ul_client_t *ul_client)
{
    struct list_head *pos, *n;
    udi_log_t *logptr;
    unifi_priv_t *priv = uf_find_instance(ul_client->instance);
    int ref;

    ul_client->instance = -1;
    ul_client->event_hook = NULL;
    ul_client->udi_enabled = 0;
    unifi_trace(priv, UDBG5, "UDI (0x%x) deregistered.\n", ul_client);

    /* Free memory allocated for the reply signal and its bulk data. */
    kfree(ul_client->reply_signal);
    for (ref = 0; ref < UNIFI_MAX_DATA_REFERENCES; ref ++) {
        kfree(ul_client->reply_bulkdata[ref]);
    }

    if (ul_client->snap_filter.count) {
        ul_client->snap_filter.count = 0;
        kfree(ul_client->snap_filter.protocols);
    }

    /* Free anything pending on the udi_log list */
    down(&ul_client->udi_sem);
    list_for_each_safe(pos, n, &ul_client->udi_log)
    {
        logptr = list_entry(pos, udi_log_t, q);
        list_del(pos);
        kfree(logptr);
    }
    up(&ul_client->udi_sem);

    return 0;
} /* ul_deregister_client() */



/*
 * ---------------------------------------------------------------------------
 *  logging_handler
 *
 *      This function is registered with the driver core.
 *      It is called every time a UniFi HIP Signal is sent. It iterates over
 *      the list of processes interested in receiving log events and
 *      delivers the events to them.
 *
 *  Arguments:
 *      ospriv      Pointer to driver's private data.
 *      sigdata     Pointer to the packed signal buffer.
 *      signal_len  Length of the packed signal.
 *      bulkdata    Pointer to the signal's bulk data.
 *      dir         Direction of the signal
 *                  0 = from-host
 *                  1 = to-host
 *
 *  Returns:
 *      None.
 * ---------------------------------------------------------------------------
 */
void
logging_handler(void *ospriv,
                u8 *sigdata, u32 signal_len,
                const bulk_data_param_t *bulkdata,
                enum udi_log_direction direction)
{
    unifi_priv_t *priv = (unifi_priv_t*)ospriv;
    ul_client_t *client;
    int dir;

    dir = (direction == UDI_LOG_FROM_HOST) ? UDI_FROM_HOST : UDI_TO_HOST;

    down(&priv->udi_logging_mutex);
    client = priv->logging_client;
    if (client != NULL) {
        client->event_hook(client, sigdata, signal_len,
                           bulkdata, dir);
    }
    up(&priv->udi_logging_mutex);

} /* logging_handler() */



/*
 * ---------------------------------------------------------------------------
 *  ul_log_config_ind
 *
 *      This function uses the client's register callback
 *      to indicate configuration information e.g core errors.
 *
 *  Arguments:
 *      priv        Pointer to driver's private data.
 *      conf_param  Pointer to the configuration data.
 *      len         Length of the configuration data.
 *
 *  Returns:
 *      None.
 * ---------------------------------------------------------------------------
 */
void
ul_log_config_ind(unifi_priv_t *priv, u8 *conf_param, int len)
{
#ifdef CSR_SUPPORT_SME
    if (priv->smepriv == NULL)
    {
        return;
    }
    if ((CONFIG_IND_ERROR == (*conf_param)) && (priv->wifi_on_state == wifi_on_in_progress)) {
        unifi_notice(priv, "ul_log_config_ind: wifi on in progress, suppress error\n");
    } else {
        /* wifi_off_ind (error or exit) */
        CsrWifiRouterCtrlWifiOffIndSend(priv->CSR_WIFI_SME_IFACEQUEUE, 0, (CsrWifiRouterCtrlControlIndication)(*conf_param));
    }
#ifdef CSR_WIFI_HIP_DEBUG_OFFLINE
    unifi_debug_buf_dump();
#endif
#else
    bulk_data_param_t bulkdata;

    /*
     * If someone killed unifi_managed before the driver was unloaded
     * the g_drvpriv pointer is going to be NULL. In this case it is
     * safe to assume that there is no client to get the indication.
     */
    if (!priv) {
        unifi_notice(NULL, "uf_sme_event_ind: NULL priv\n");
        return;
    }

    /* Create a null bulkdata structure. */
    bulkdata.d[0].data_length = 0;
    bulkdata.d[1].data_length = 0;

    sme_native_log_event(priv->sme_cli, conf_param, sizeof(u8),
                         &bulkdata, UDI_CONFIG_IND);

#endif /* CSR_SUPPORT_SME */

} /* ul_log_config_ind */


/*
 * ---------------------------------------------------------------------------
 *  free_bulkdata_buffers
 *
 *      Free the bulkdata buffers e.g. after a failed unifi_send_signal().
 *
 *  Arguments:
 *      priv        Pointer to device private struct
 *      bulkdata    Pointer to bulkdata parameter table
 *
 *  Returns:
 *      None.
 * ---------------------------------------------------------------------------
 */
static void
free_bulkdata_buffers(unifi_priv_t *priv, bulk_data_param_t *bulkdata)
{
    int i;

    if (bulkdata) {
        for (i = 0; i < UNIFI_MAX_DATA_REFERENCES; ++i) {
            if (bulkdata->d[i].data_length != 0) {
                unifi_net_data_free(priv, (bulk_data_desc_t *)(&bulkdata->d[i]));
                /* data_length is now 0 */
            }
        }
    }

} /* free_bulkdata_buffers */

static int
_align_bulk_data_buffers(unifi_priv_t *priv, u8 *signal,
                         bulk_data_param_t *bulkdata)
{
    unsigned int i;

    if ((bulkdata == NULL) || (CSR_WIFI_ALIGN_BYTES == 0)) {
        return 0;
    }

    for (i = 0; i < UNIFI_MAX_DATA_REFERENCES; i++)
    {
        struct sk_buff *skb;
        /*
        * The following complex casting is in place in order to eliminate 64-bit compilation warning
        * "cast to/from pointer from/to integer of different size"
        */
        u32 align_offset = (u32)(long)(bulkdata->d[i].os_data_ptr) & (CSR_WIFI_ALIGN_BYTES-1);
        if (align_offset)
        {
            skb = (struct sk_buff*)bulkdata->d[i].os_net_buf_ptr;
            if (skb == NULL) {
                unifi_warning(priv,
                              "_align_bulk_data_buffers: Align offset found (%d) but skb is NULL!\n",
                              align_offset);
                return -EINVAL;
            }
            if (bulkdata->d[i].data_length == 0) {
                unifi_warning(priv,
                              "_align_bulk_data_buffers: Align offset found (%d) but length is zero\n",
                              align_offset);
                return CSR_RESULT_SUCCESS;
            }
            unifi_trace(priv, UDBG5,
                        "Align f-h buffer (0x%p) by %d bytes (skb->data: 0x%p)\n",
                        bulkdata->d[i].os_data_ptr, align_offset, skb->data);


            /* Check if there is enough headroom... */
            if (unlikely(skb_headroom(skb) < align_offset))
            {
                struct sk_buff *tmp = skb;

                unifi_trace(priv, UDBG5, "Headroom not enough - realloc it\n");
                skb = skb_realloc_headroom(skb, align_offset);
                if (skb == NULL) {
                    unifi_error(priv,
                                "_align_bulk_data_buffers: skb_realloc_headroom failed - signal is dropped\n");
                    return -EFAULT;
                }
                /* Free the old bulk data only if allocation succeeds */
                kfree_skb(tmp);
                /* Bulkdata needs to point to the new skb */
                bulkdata->d[i].os_net_buf_ptr = (const unsigned char*)skb;
                bulkdata->d[i].os_data_ptr = (const void*)skb->data;
            }
            /* ... before pushing the data to the right alignment offset */
            skb_push(skb, align_offset);

        }
        /* The direction bit is zero for the from-host */
        signal[SIZEOF_SIGNAL_HEADER + (i * SIZEOF_DATAREF) + 1] = align_offset;

    }
    return 0;
} /* _align_bulk_data_buffers() */


/*
 * ---------------------------------------------------------------------------
 *  ul_send_signal_unpacked
 *
 *      This function sends a host formatted signal to unifi.
 *
 *  Arguments:
 *      priv        Pointer to driver's private data.
 *      sigptr      Pointer to the signal.
 *      bulkdata    Pointer to the signal's bulk data.
 *
 *  Returns:
 *      O on success, error code otherwise.
 *
 *  Notes:
 *  The signals have to be sent in the format described in the host interface
 *  specification, i.e wire formatted. Certain clients use the host formatted
 *  structures. The write_pack() transforms the host formatted signal
 *  into the wired formatted signal. The code is in the core, since the signals
 *  are defined therefore binded to the host interface specification.
 * ---------------------------------------------------------------------------
 */
int
ul_send_signal_unpacked(unifi_priv_t *priv, CSR_SIGNAL *sigptr,
                        bulk_data_param_t *bulkdata)
{
    u8 sigbuf[UNIFI_PACKED_SIGBUF_SIZE];
    u16 packed_siglen;
    CsrResult csrResult;
    unsigned long lock_flags;
    int r;


    csrResult = write_pack(sigptr, sigbuf, &packed_siglen);
    if (csrResult != CSR_RESULT_SUCCESS) {
        unifi_error(priv, "Malformed HIP signal in ul_send_signal_unpacked()\n");
        return CsrHipResultToStatus(csrResult);
    }
    r = _align_bulk_data_buffers(priv, sigbuf, (bulk_data_param_t*)bulkdata);
    if (r) {
        return r;
    }

    spin_lock_irqsave(&priv->send_signal_lock, lock_flags);
    csrResult = unifi_send_signal(priv->card, sigbuf, packed_siglen, bulkdata);
    if (csrResult != CSR_RESULT_SUCCESS) {
  /*      free_bulkdata_buffers(priv, (bulk_data_param_t *)bulkdata); */
        spin_unlock_irqrestore(&priv->send_signal_lock, lock_flags);
        return CsrHipResultToStatus(csrResult);
    }
    spin_unlock_irqrestore(&priv->send_signal_lock, lock_flags);

    return 0;
} /* ul_send_signal_unpacked() */


/*
 * ---------------------------------------------------------------------------
 *  reset_driver_status
 *
 *      This function is called from ul_send_signal_raw() when it detects
 *      that the SME has sent a MLME-RESET request.
 *
 *  Arguments:
 *      priv        Pointer to device private struct
 *
 *  Returns:
 *      None.
 * ---------------------------------------------------------------------------
 */
static void
reset_driver_status(unifi_priv_t *priv)
{
    priv->sta_wmm_capabilities = 0;
#ifdef CSR_NATIVE_LINUX
#ifdef CSR_SUPPORT_WEXT
    priv->wext_conf.flag_associated = 0;
    priv->wext_conf.block_controlled_port = CSR_WIFI_ROUTER_PORT_ACTION_8021X_PORT_OPEN;
    priv->wext_conf.bss_wmm_capabilities = 0;
    priv->wext_conf.disable_join_on_ssid_set = 0;
#endif
#endif
} /* reset_driver_status() */


/*
 * ---------------------------------------------------------------------------
 *  ul_send_signal_raw
 *
 *      This function sends a wire formatted data signal to unifi.
 *
 *  Arguments:
 *      priv        Pointer to driver's private data.
 *      sigptr      Pointer to the signal.
 *      siglen      Length of the signal.
 *      bulkdata    Pointer to the signal's bulk data.
 *
 *  Returns:
 *      O on success, error code otherwise.
 * ---------------------------------------------------------------------------
 */
int
ul_send_signal_raw(unifi_priv_t *priv, unsigned char *sigptr, int siglen,
                   bulk_data_param_t *bulkdata)
{
    CsrResult csrResult;
    unsigned long lock_flags;
    int r;

    /*
     * Make sure that the signal is updated with the bulk data
     * alignment for DMA.
     */
    r = _align_bulk_data_buffers(priv, (u8*)sigptr, bulkdata);
    if (r) {
        return r;
    }

    spin_lock_irqsave(&priv->send_signal_lock, lock_flags);
    csrResult = unifi_send_signal(priv->card, sigptr, siglen, bulkdata);
    if (csrResult != CSR_RESULT_SUCCESS) {
        free_bulkdata_buffers(priv, bulkdata);
        spin_unlock_irqrestore(&priv->send_signal_lock, lock_flags);
        return CsrHipResultToStatus(csrResult);
    }
    spin_unlock_irqrestore(&priv->send_signal_lock, lock_flags);

    /*
     * Since this is use by unicli, if we get an MLME reset request
     * we need to initialize a few status parameters
     * that the driver uses to make decisions.
     */
    if (GET_SIGNAL_ID(sigptr) == CSR_MLME_RESET_REQUEST_ID) {
        reset_driver_status(priv);
    }

    return 0;
} /* ul_send_signal_raw() */


