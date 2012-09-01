/*
 * ---------------------------------------------------------------------------
 * FILE:     netdev.c
 *
 * PURPOSE:
 *      This file provides the upper edge interface to the linux netdevice
 *      and wireless extensions.
 *      It is part of the porting exercise.
 *
 * Copyright (C) 2005-2010 by Cambridge Silicon Radio Ltd.
 *
 * Refer to LICENSE.txt included with this source code for details on
 * the license terms.
 *
 * ---------------------------------------------------------------------------
 */

/*
 * Porting Notes:
 * This file implements the data plane of the UniFi linux driver.
 *
 * All the Tx packets are passed to the HIP core lib, using the
 * unifi_send_signal() API. For EAPOL packets use the MLME-EAPOL.req
 * signal, for all other use the MLME-UNITDATA.req. The unifi_send_signal()
 * expects the wire-formatted (packed) signal. For convenience, in the OS
 * layer we only use the native (unpacked) signal structures. The HIP core lib
 * provides the write_pack() helper function to convert to the packed signal.
 * The packet is stored in the bulk data of the signal. We do not need to
 * allocate new memory to store the packet, because unifi_net_data_malloc()
 * is implemented to return a skb, which is the format of packet in Linux.
 * The HIP core lib frees the bulk data buffers, so we do not need to do
 * this in the OS layer.
 *
 * All the Rx packets are MLME-UNITDATA.ind signals, passed by the HIP core lib
 * in unifi_receive_event(). We do not need to allocate an skb and copy the
 * received packet because the HIP core lib has stored in memory allocated by
 * unifi_net_data_malloc(). Also, we can perform the 802.11 to Ethernet
 * translation in-place because we allocate the extra memory allocated in
 * unifi_net_data_malloc().
 *
 * If possible, the porting exercise should appropriately implement
 * unifi_net_data_malloc() and unifi_net_data_free() to save copies between
 * network and driver buffers.
 */

#include <linux/types.h>
#include <linux/etherdevice.h>
#include <linux/mutex.h>
#include <linux/semaphore.h>
#include <linux/version.h>
#include <linux/vmalloc.h>
#include "csr_wifi_hip_unifi.h"
#include "csr_wifi_hip_conversions.h"
#include "unifi_priv.h"
#include <net/pkt_sched.h>


/* Wext handler is suported only if CSR_SUPPORT_WEXT is defined */
#ifdef CSR_SUPPORT_WEXT
extern struct iw_handler_def unifi_iw_handler_def;
#endif /* CSR_SUPPORT_WEXT */
static void check_ba_frame_age_timeout( unifi_priv_t *priv,
                                            netInterface_priv_t *interfacePriv,
                                            ba_session_rx_struct *ba_session);
static void process_ba_frame(unifi_priv_t *priv,
                             netInterface_priv_t *interfacePriv,
                             ba_session_rx_struct *ba_session,
                             frame_desc_struct *frame_desc);
static void process_ba_complete(unifi_priv_t *priv, netInterface_priv_t *interfacePriv);
static void process_ma_packet_error_ind(unifi_priv_t *priv, CSR_SIGNAL *signal, bulk_data_param_t *bulkdata);
static void process_amsdu(unifi_priv_t *priv, CSR_SIGNAL *signal, bulk_data_param_t *bulkdata);
static int uf_net_open(struct net_device *dev);
static int uf_net_ioctl(struct net_device *dev, struct ifreq *rq, int cmd);
static int uf_net_stop(struct net_device *dev);
static struct net_device_stats *uf_net_get_stats(struct net_device *dev);
static u16 uf_net_select_queue(struct net_device *dev, struct sk_buff *skb);
static netdev_tx_t uf_net_xmit(struct sk_buff *skb, struct net_device *dev);
static void uf_set_multicast_list(struct net_device *dev);


typedef int (*tx_signal_handler)(unifi_priv_t *priv, struct sk_buff *skb, const struct ethhdr *ehdr, CSR_PRIORITY priority);

#ifdef CONFIG_NET_SCHED
/*
 * Queueing Discipline Interface
 * Only used if kernel is configured with CONFIG_NET_SCHED
 */

/*
 * The driver uses the qdisc interface to buffer and control all
 * outgoing traffic. We create a root qdisc, register our qdisc operations
 * and later we create two subsiduary pfifo queues for the uncontrolled
 * and controlled ports.
 *
 * The network stack delivers all outgoing packets in our enqueue handler.
 * There, we classify the packet and decide whether to store it or drop it
 * (if the controlled port state is set to "discard").
 * If the packet is enqueued, the network stack call our dequeue handler.
 * There, we decide whether we can send the packet, delay it or drop it
 * (the controlled port configuration might have changed meanwhile).
 * If a packet is dequeued, then the network stack calls our hard_start_xmit
 * handler where finally we send the packet.
 *
 * If the hard_start_xmit handler fails to send the packet, we return
 * NETDEV_TX_BUSY and the network stack call our requeue handler where
 * we put the packet back in the same queue in came from.
 *
 */

struct uf_sched_data
{
    /* Traffic Classifier TBD */
    struct tcf_proto *filter_list;
    /* Our two queues */
    struct Qdisc *queues[UNIFI_TRAFFIC_Q_MAX];
};

struct uf_tx_packet_data {
    /* Queue the packet is stored in */
    unifi_TrafficQueue queue;
    /* QoS Priority determined when enqueing packet */
    CSR_PRIORITY priority;
    /* Debug */
    unsigned long host_tag;
};

#endif /* CONFIG_NET_SCHED */

static const struct net_device_ops uf_netdev_ops =
{
    .ndo_open = uf_net_open,
    .ndo_stop = uf_net_stop,
    .ndo_start_xmit = uf_net_xmit,
    .ndo_do_ioctl = uf_net_ioctl,
    .ndo_get_stats = uf_net_get_stats, /* called by /proc/net/dev */
    .ndo_set_rx_mode = uf_set_multicast_list,
    .ndo_select_queue = uf_net_select_queue,
};

static u8 oui_rfc1042[P80211_OUI_LEN] = { 0x00, 0x00, 0x00 };
static u8 oui_8021h[P80211_OUI_LEN]   = { 0x00, 0x00, 0xf8 };


/* Callback for event logging to blocking clients */
static void netdev_mlme_event_handler(ul_client_t  *client,
                                      const u8 *sig_packed, int sig_len,
                                      const bulk_data_param_t *bulkdata,
                                      int dir);

#ifdef CSR_SUPPORT_WEXT
/* Declare netdev_notifier block which will contain the state change
 * handler callback function
 */
static struct notifier_block uf_netdev_notifier;
#endif

/*
 * ---------------------------------------------------------------------------
 *  uf_alloc_netdevice
 *
 *      Allocate memory for the net_device and device private structs
 *      for this interface.
 *      Fill in the fields, but don't register the interface yet.
 *      We need to configure the UniFi first.
 *
 *  Arguments:
 *      sdio_dev        Pointer to SDIO context handle to use for all
 *                      SDIO ops.
 *      bus_id          A small number indicating the SDIO card position on the
 *                      bus. Typically this is the slot number, e.g. 0, 1 etc.
 *                      Valid values are 0 to MAX_UNIFI_DEVS-1.
 *
 *  Returns:
 *      Pointer to device private struct.
 *
 *  Notes:
 *      The net_device and device private structs are allocated together
 *      and should be freed by freeing the net_device pointer.
 * ---------------------------------------------------------------------------
 */
unifi_priv_t *
uf_alloc_netdevice(CsrSdioFunction *sdio_dev, int bus_id)
{
    struct net_device *dev;
    unifi_priv_t *priv;
    netInterface_priv_t *interfacePriv;
#ifdef CSR_SUPPORT_WEXT
    int rc;
#endif
    unsigned char i; /* loop index */

    /*
     * Allocate netdevice struct, assign name template and
     * setup as an ethernet device.
     * The net_device and private structs are zeroed. Ether_setup() then
     * sets up ethernet handlers and values.
     * The RedHat 9 redhat-config-network tool doesn't recognise wlan* devices,
     * so use "eth*" (like other wireless extns drivers).
     */
    dev = alloc_etherdev_mq(sizeof(unifi_priv_t) + sizeof(netInterface_priv_t), UNIFI_TRAFFIC_Q_MAX);

    if (dev == NULL) {
        return NULL;
    }

    /* Set up back pointer from priv to netdev */
    interfacePriv = (netInterface_priv_t *)netdev_priv(dev);
    priv = (unifi_priv_t *)(interfacePriv + 1);
    interfacePriv->privPtr = priv;
    interfacePriv->InterfaceTag = 0;


    /* Initialize all supported netdev interface to be NULL */
    for(i=0; i<CSR_WIFI_NUM_INTERFACES; i++) {
        priv->netdev[i] = NULL;
        priv->interfacePriv[i] = NULL;
    }
    priv->netdev[0] = dev;
    priv->interfacePriv[0] = interfacePriv;

    /* Setup / override net_device fields */
    dev->netdev_ops = &uf_netdev_ops;

#ifdef CSR_SUPPORT_WEXT
    dev->wireless_handlers = &unifi_iw_handler_def;
#if IW_HANDLER_VERSION < 6
    dev->get_wireless_stats = unifi_get_wireless_stats;
#endif /* IW_HANDLER_VERSION */
#endif /* CSR_SUPPORT_WEXT */

    /* This gives us enough headroom to add the 802.11 header */
    dev->needed_headroom = 32;

    /* Use bus_id as instance number */
    priv->instance = bus_id;
    /* Store SDIO pointer to pass in the core */
    priv->sdio = sdio_dev;

    sdio_dev->driverData = (void*)priv;
    /* Consider UniFi to be uninitialised */
    priv->init_progress = UNIFI_INIT_NONE;

    priv->prev_queue = 0;

    /*
     * Initialise the clients structure array.
     * We do not need protection around ul_init_clients() because
     * the character device can not be used until uf_alloc_netdevice()
     * returns and Unifi_instances[bus_id]=priv is set, since unifi_open()
     * will return -ENODEV.
     */
    ul_init_clients(priv);

    /*
     * Register a new ul client to send the multicast list signals.
     * Note: priv->instance must be set before calling this.
     */
    priv->netdev_client = ul_register_client(priv,
            0,
            netdev_mlme_event_handler);
    if (priv->netdev_client == NULL) {
        unifi_error(priv,
                "Failed to register a unifi client for background netdev processing\n");
        free_netdev(priv->netdev[0]);
        return NULL;
    }
    unifi_trace(priv, UDBG2, "Netdev %p client (id:%d s:0x%X) is registered\n",
            dev, priv->netdev_client->client_id, priv->netdev_client->sender_id);

    priv->sta_wmm_capabilities = 0;

#if (defined(CSR_WIFI_SECURITY_WAPI_ENABLE) && defined(CSR_SUPPORT_SME))
    priv->wapi_multicast_filter = 0;
    priv->wapi_unicast_filter = 0;
    priv->wapi_unicast_queued_pkt_filter = 0;
#ifdef CSR_WIFI_SECURITY_WAPI_QOSCTRL_MIC_WORKAROUND
    priv->isWapiConnection = FALSE;
#endif
#endif

    /* Enable all queues by default */
    interfacePriv->queueEnabled[0] = 1;
    interfacePriv->queueEnabled[1] = 1;
    interfacePriv->queueEnabled[2] = 1;
    interfacePriv->queueEnabled[3] = 1;

#ifdef CSR_SUPPORT_SME
    priv->allPeerDozing = 0;
#endif
    /*
     * Initialise the OS private struct.
     */
    /*
     * Instead of deciding in advance to use 11bg or 11a, we could do a more
     * clever scan on both radios.
     */
    if (use_5g) {
        priv->if_index = CSR_INDEX_5G;
        unifi_info(priv, "Using the 802.11a radio\n");
    } else {
        priv->if_index = CSR_INDEX_2G4;
    }

    /* Initialise bh thread structure */
    priv->bh_thread.thread_task = NULL;
    priv->bh_thread.block_thread = 1;
    init_waitqueue_head(&priv->bh_thread.wakeup_q);
    priv->bh_thread.wakeup_flag = 0;
    sprintf(priv->bh_thread.name, "uf_bh_thread");

    /* reset the connected state for the interface */
    interfacePriv->connected = UnifiConnectedUnknown;  /* -1 unknown, 0 no, 1 yes */

#ifdef USE_DRIVER_LOCK
    sema_init(&priv->lock, 1);
#endif /* USE_DRIVER_LOCK */

    spin_lock_init(&priv->send_signal_lock);

    spin_lock_init(&priv->m4_lock);
    sema_init(&priv->ba_mutex, 1);

#if (defined(CSR_WIFI_SECURITY_WAPI_ENABLE) && defined(CSR_WIFI_SECURITY_WAPI_SW_ENCRYPTION))
    spin_lock_init(&priv->wapi_lock);
#endif

#ifdef CSR_SUPPORT_SME
    spin_lock_init(&priv->staRecord_lock);
    spin_lock_init(&priv->tx_q_lock);
#endif

    /* Create the Traffic Analysis workqueue */
    priv->unifi_workqueue = create_singlethread_workqueue("unifi_workq");
    if (priv->unifi_workqueue == NULL) {
        /* Deregister priv->netdev_client */
        ul_deregister_client(priv->netdev_client);
        free_netdev(priv->netdev[0]);
        return NULL;
    }

#ifdef CSR_SUPPORT_SME
    /* Create the Multicast Addresses list work structure */
    INIT_WORK(&priv->multicast_list_task, uf_multicast_list_wq);

    /* Create m4 buffering work structure */
    INIT_WORK(&interfacePriv->send_m4_ready_task, uf_send_m4_ready_wq);

#if (defined(CSR_WIFI_SECURITY_WAPI_ENABLE) && defined(CSR_WIFI_SECURITY_WAPI_SW_ENCRYPTION))
    /* Create work structure to buffer the WAPI data packets to be sent to SME for encryption */
    INIT_WORK(&interfacePriv->send_pkt_to_encrypt, uf_send_pkt_to_encrypt);
#endif
#endif

    priv->ref_count = 1;

    priv->amp_client = NULL;
    priv->coredump_mode = 0;
    priv->ptest_mode = 0;
    priv->wol_suspend = FALSE;
    INIT_LIST_HEAD(&interfacePriv->rx_uncontrolled_list);
    INIT_LIST_HEAD(&interfacePriv->rx_controlled_list);
    sema_init(&priv->rx_q_sem, 1);

#ifdef CSR_SUPPORT_WEXT
    interfacePriv->netdev_callback_registered = FALSE;
    interfacePriv->wait_netdev_change = FALSE;
    /* Register callback for netdevice state changes */
    if ((rc = register_netdevice_notifier(&uf_netdev_notifier)) == 0) {
        interfacePriv->netdev_callback_registered = TRUE;
    }
    else {
        unifi_warning(priv, "Failed to register netdevice notifier : %d %p\n", rc, dev);
    }
#endif /* CSR_SUPPORT_WEXT */

#ifdef CSR_WIFI_SPLIT_PATCH
    /* set it to some invalid value */
    priv->pending_mode_set.common.destination = 0xaaaa;
#endif

    return priv;
} /* uf_alloc_netdevice() */

/*
 *---------------------------------------------------------------------------
 *  uf_alloc_netdevice_for_other_interfaces
 *
 *      Allocate memory for the net_device and device private structs
 *      for this interface.
 *      Fill in the fields, but don't register the interface yet.
 *      We need to configure the UniFi first.
 *
 *  Arguments:
 *      interfaceTag   Interface number.
 *      sdio_dev        Pointer to SDIO context handle to use for all
 *                      SDIO ops.
 *      bus_id          A small number indicating the SDIO card position on the
 *                      bus. Typically this is the slot number, e.g. 0, 1 etc.
 *                      Valid values are 0 to MAX_UNIFI_DEVS-1.
 *
 *  Returns:
 *      Pointer to device private struct.
 *
 *  Notes:
 *      The device private structure contains the interfaceTag and pointer to the unifi_priv
 *      structure created allocated by net_device od interface0.
 *      The net_device and device private structs are allocated together
 *      and should be freed by freeing the net_device pointer.
 * ---------------------------------------------------------------------------
 */
u8
uf_alloc_netdevice_for_other_interfaces(unifi_priv_t *priv, u16 interfaceTag)
{
    struct net_device *dev;
    netInterface_priv_t *interfacePriv;

    /*
     * Allocate netdevice struct, assign name template and
     * setup as an ethernet device.
     * The net_device and private structs are zeroed. Ether_setup() then
     * sets up ethernet handlers and values.
     * The RedHat 9 redhat-config-network tool doesn't recognise wlan* devices,
     * so use "eth*" (like other wireless extns drivers).
     */
    dev = alloc_etherdev_mq(sizeof(netInterface_priv_t), 1);
    if (dev == NULL) {
        return FALSE;
    }

    if (interfaceTag >= CSR_WIFI_NUM_INTERFACES) {
        unifi_error(priv, "uf_alloc_netdevice_for_other_interfaces bad interfaceTag\n");
        return FALSE;
    }

    /* Set up back pointer from priv to netdev */
    interfacePriv = (netInterface_priv_t *)netdev_priv(dev);
    interfacePriv->privPtr = priv;
    interfacePriv->InterfaceTag = interfaceTag;
    priv->netdev[interfaceTag] = dev;
    priv->interfacePriv[interfacePriv->InterfaceTag] = interfacePriv;

    /* reset the connected state for the interface */
    interfacePriv->connected = UnifiConnectedUnknown;  /* -1 unknown, 0 no, 1 yes */
    INIT_LIST_HEAD(&interfacePriv->rx_uncontrolled_list);
    INIT_LIST_HEAD(&interfacePriv->rx_controlled_list);

    /* Setup / override net_device fields */
    dev->netdev_ops = &uf_netdev_ops;

#ifdef CSR_SUPPORT_WEXT
    dev->wireless_handlers = &unifi_iw_handler_def;
#if IW_HANDLER_VERSION < 6
    dev->get_wireless_stats = unifi_get_wireless_stats;
#endif /* IW_HANDLER_VERSION */
#endif /* CSR_SUPPORT_WEXT */
    return TRUE;
} /* uf_alloc_netdevice() */



/*
 * ---------------------------------------------------------------------------
 *  uf_free_netdevice
 *
 *      Unregister the network device and free the memory allocated for it.
 *      NB This includes the memory for the priv struct.
 *
 *  Arguments:
 *      priv            Device private pointer.
 *
 *  Returns:
 *      None.
 * ---------------------------------------------------------------------------
 */
int
uf_free_netdevice(unifi_priv_t *priv)
{
    int i;
    unsigned long flags;

    func_enter();

    unifi_trace(priv, UDBG1, "uf_free_netdevice\n");

    if (!priv) {
        return -EINVAL;
    }

    /*
     * Free any buffers used for holding firmware
     */
    uf_release_firmware_files(priv);

#if (defined CSR_SUPPORT_SME) && (defined CSR_SUPPORT_WEXT)
    if (priv->connection_config.mlmeAssociateReqInformationElements) {
        kfree(priv->connection_config.mlmeAssociateReqInformationElements);
    }
    priv->connection_config.mlmeAssociateReqInformationElements = NULL;
    priv->connection_config.mlmeAssociateReqInformationElementsLength = 0;

    if (priv->mib_data.length) {
        vfree(priv->mib_data.data);
    }
    priv->mib_data.data = NULL;
    priv->mib_data.length = 0;

#endif /* CSR_SUPPORT_SME && CSR_SUPPORT_WEXT*/

    /* Free any bulkdata buffers allocated for M4 caching */
    spin_lock_irqsave(&priv->m4_lock, flags);
    for (i = 0; i < CSR_WIFI_NUM_INTERFACES; i++) {
        netInterface_priv_t *interfacePriv = priv->interfacePriv[i];
        if (interfacePriv->m4_bulk_data.data_length > 0) {
            unifi_trace(priv, UDBG5, "uf_free_netdevice: free M4 bulkdata %d\n", i);
            unifi_net_data_free(priv, &interfacePriv->m4_bulk_data);
        }
    }
    spin_unlock_irqrestore(&priv->m4_lock, flags);

#if (defined(CSR_WIFI_SECURITY_WAPI_ENABLE) && defined(CSR_WIFI_SECURITY_WAPI_SW_ENCRYPTION))
    /* Free any bulkdata buffers allocated for M4 caching */
    spin_lock_irqsave(&priv->wapi_lock, flags);
    for (i = 0; i < CSR_WIFI_NUM_INTERFACES; i++) {
        netInterface_priv_t *interfacePriv = priv->interfacePriv[i];
        if (interfacePriv->wapi_unicast_bulk_data.data_length > 0) {
            unifi_trace(priv, UDBG5, "uf_free_netdevice: free WAPI PKT bulk data %d\n", i);
            unifi_net_data_free(priv, &interfacePriv->wapi_unicast_bulk_data);
        }
    }
    spin_unlock_irqrestore(&priv->wapi_lock, flags);
#endif

#ifdef CSR_SUPPORT_WEXT
    /* Unregister callback for netdevice state changes */
    unregister_netdevice_notifier(&uf_netdev_notifier);
#endif /* CSR_SUPPORT_WEXT */

#ifdef CSR_SUPPORT_SME
    /* Cancel work items and destroy the workqueue */
    cancel_work_sync(&priv->multicast_list_task);
#endif
/* Destroy the workqueues. */
    flush_workqueue(priv->unifi_workqueue);
    destroy_workqueue(priv->unifi_workqueue);

    /* Free up netdev in reverse order: priv is allocated with netdev[0].
     * So, netdev[0] should be freed after all other netdevs are freed up
     */
    for (i=CSR_WIFI_NUM_INTERFACES-1; i>=0; i--) {
        /*Free the netdev struct and priv, which are all one lump*/
        if (priv->netdev[i]) {
            unifi_error(priv, "uf_free_netdevice: netdev %d %p\n", i, priv->netdev[i]);
            free_netdev(priv->netdev[i]);
        }
    }

    func_exit();
    return 0;
} /* uf_free_netdevice() */


/*
 * ---------------------------------------------------------------------------
 *  uf_net_open
 *
 *      Called when userland does "ifconfig wlan0 up".
 *
 *  Arguments:
 *      dev             Device pointer.
 *
 *  Returns:
 *      None.
 * ---------------------------------------------------------------------------
 */
static int
uf_net_open(struct net_device *dev)
{
    netInterface_priv_t *interfacePriv = (netInterface_priv_t *)netdev_priv(dev);
    unifi_priv_t *priv = interfacePriv->privPtr;

    func_enter();

    /* If we haven't finished UniFi initialisation, we can't start */
    if (priv->init_progress != UNIFI_INIT_COMPLETED) {
        unifi_warning(priv, "%s: unifi not ready, failing net_open\n", __FUNCTION__);
        return -EINVAL;
    }

#if (defined CSR_NATIVE_LINUX) && (defined UNIFI_SNIFF_ARPHRD) && defined(CSR_SUPPORT_WEXT)
    /*
     * To sniff, the user must do "iwconfig mode monitor", which sets
     * priv->wext_conf.mode to IW_MODE_MONITOR.
     * Then he/she must do "ifconfig ethn up", which calls this fn.
     * There is no point in starting the sniff with SNIFFJOIN until
     * this point.
     */
    if (priv->wext_conf.mode == IW_MODE_MONITOR) {
        int err;
        err = uf_start_sniff(priv);
        if (err) {
            return err;
        }
        netif_carrier_on(dev);
    }
#endif

#ifdef CSR_SUPPORT_WEXT
    if (interfacePriv->wait_netdev_change) {
        unifi_trace(priv, UDBG1, "%s: Waiting for NETDEV_CHANGE, assume connected\n",
                    __FUNCTION__);
        interfacePriv->connected = UnifiConnected;
        interfacePriv->wait_netdev_change = FALSE;
    }
#endif

    netif_tx_start_all_queues(dev);

    func_exit();
    return 0;
} /* uf_net_open() */


static int
uf_net_stop(struct net_device *dev)
{
#if defined(CSR_NATIVE_LINUX) && defined(UNIFI_SNIFF_ARPHRD) && defined(CSR_SUPPORT_WEXT)
    netInterface_priv_t *interfacePriv = (netInterface_priv_t*)netdev_priv(dev);
    unifi_priv_t *priv = interfacePriv->privPtr;

    func_enter();

    /* Stop sniffing if in Monitor mode */
    if (priv->wext_conf.mode == IW_MODE_MONITOR) {
        if (priv->card) {
            int err;
            err = unifi_reset_state(priv, dev->dev_addr, 1);
            if (err) {
                return err;
            }
        }
    }
#else
    func_enter();
#endif

    netif_tx_stop_all_queues(dev);

    func_exit();
    return 0;
} /* uf_net_stop() */


/* This is called after the WE handlers */
static int
uf_net_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
    int rc;

    rc = -EOPNOTSUPP;

    return rc;
} /* uf_net_ioctl() */



static struct net_device_stats *
uf_net_get_stats(struct net_device *dev)
{
    netInterface_priv_t *interfacePriv = (netInterface_priv_t *)netdev_priv(dev);

    return &interfacePriv->stats;
} /* uf_net_get_stats() */

static CSR_PRIORITY uf_get_packet_priority(unifi_priv_t *priv, netInterface_priv_t *interfacePriv, struct sk_buff *skb, const int proto)
{
    CSR_PRIORITY priority = CSR_CONTENTION;

    func_enter();
    priority = (CSR_PRIORITY) (skb->priority >> 5);

    if (priority == CSR_QOS_UP0) { /* 0 */

        unifi_trace(priv, UDBG5, "uf_get_packet_priority: proto = 0x%.4X\n", proto);

        switch (proto) {
            case 0x0800:        /* IPv4 */
            case 0x814C:        /* SNMP */
            case 0x880C:        /* GSMP */
                priority = (CSR_PRIORITY) (skb->data[1 + ETH_HLEN] >> 5);
                break;

            case 0x8100:        /* VLAN */
                priority = (CSR_PRIORITY) (skb->data[0 + ETH_HLEN] >> 5);
                break;

            case 0x86DD:        /* IPv6 */
                priority = (CSR_PRIORITY) ((skb->data[0 + ETH_HLEN] & 0x0E) >> 1);
                break;

            default:
                priority = CSR_QOS_UP0;
                break;
        }
    }

    /* Check if we are allowed to transmit on this AC. Because of ACM we may have to downgrade to a lower
     * priority */
    if (interfacePriv->interfaceMode == CSR_WIFI_ROUTER_CTRL_MODE_STA ||
        interfacePriv->interfaceMode == CSR_WIFI_ROUTER_CTRL_MODE_P2PCLI) {
        unifi_TrafficQueue queue;

        /* Keep trying lower priorities until we find a queue
         * Priority to queue mapping is 1,2 - BK, 0,3 - BE, 4,5 - VI, 6,7 - VO */
        queue = unifi_frame_priority_to_queue(priority);

        while (queue > UNIFI_TRAFFIC_Q_BK && !interfacePriv->queueEnabled[queue]) {
            queue--;
            priority = unifi_get_default_downgrade_priority(queue);
        }
    }

    unifi_trace(priv, UDBG5, "Packet priority = %d\n", priority);

    func_exit();
    return priority;
}

/*
 */
/*
 * ---------------------------------------------------------------------------
 *  get_packet_priority
 *
 *  Arguments:
 *      priv             private data area of functional driver
 *      skb              socket buffer
 *      ehdr             ethernet header to fetch protocol
 *      interfacePriv    For accessing station record database
 *
 *
 *  Returns:
 *      CSR_PRIORITY.
 * ---------------------------------------------------------------------------
 */
CSR_PRIORITY
get_packet_priority(unifi_priv_t *priv, struct sk_buff *skb, const struct ethhdr *ehdr, netInterface_priv_t *interfacePriv)
{
    CSR_PRIORITY priority = CSR_CONTENTION;
    const int proto = ntohs(ehdr->h_proto);

    u8 interfaceMode = interfacePriv->interfaceMode;

    func_enter();

    /* Priority Mapping for all the Modes */
    switch(interfaceMode)
    {
        case CSR_WIFI_ROUTER_CTRL_MODE_STA:
        case CSR_WIFI_ROUTER_CTRL_MODE_P2PCLI:
            unifi_trace(priv, UDBG4, "mode is STA \n");
            if ((priv->sta_wmm_capabilities & QOS_CAPABILITY_WMM_ENABLED) == 1) {
                priority = uf_get_packet_priority(priv, interfacePriv, skb, proto);
            } else {
                priority = CSR_CONTENTION;
            }
            break;
#ifdef CSR_SUPPORT_SME
        case CSR_WIFI_ROUTER_CTRL_MODE_AP:
        case CSR_WIFI_ROUTER_CTRL_MODE_P2PGO:
        case CSR_WIFI_ROUTER_CTRL_MODE_IBSS:
            {
                CsrWifiRouterCtrlStaInfo_t * dstStaInfo =
                    CsrWifiRouterCtrlGetStationRecordFromPeerMacAddress(priv,ehdr->h_dest, interfacePriv->InterfaceTag);
                unifi_trace(priv, UDBG4, "mode is AP \n");
                if (!(ehdr->h_dest[0] & 0x01) && dstStaInfo && dstStaInfo->wmmOrQosEnabled) {
                    /* If packet is not Broadcast/multicast */
                    priority = uf_get_packet_priority(priv, interfacePriv, skb, proto);
                } else {
                    /* Since packet destination is not QSTA, set priority to CSR_CONTENTION */
                    unifi_trace(priv, UDBG4, "Destination is not QSTA or BroadCast/Multicast\n");
                    priority = CSR_CONTENTION;
                }
            }
            break;
#endif
        default:
            unifi_trace(priv, UDBG3, " mode unknown in %s func, mode=%x\n", __FUNCTION__, interfaceMode);
    }
    unifi_trace(priv, UDBG5, "priority = %x\n", priority);

    func_exit();
    return priority;
}

/*
 * ---------------------------------------------------------------------------
 *  uf_net_select_queue
 *
 *      Called by the kernel to select which queue to put the packet in
 *
 *  Arguments:
 *      dev             Device pointer
 *      skb             Packet
 *
 *  Returns:
 *      Queue index
 * ---------------------------------------------------------------------------
 */
static u16
uf_net_select_queue(struct net_device *dev, struct sk_buff *skb)
{
    netInterface_priv_t *interfacePriv = (netInterface_priv_t *)netdev_priv(dev);
    unifi_priv_t *priv = (unifi_priv_t *)interfacePriv->privPtr;
    struct ethhdr ehdr;
    unifi_TrafficQueue queue;
    int proto;
    CSR_PRIORITY priority;

    func_enter();

    memcpy(&ehdr, skb->data, ETH_HLEN);
    proto = ntohs(ehdr.h_proto);

    /* 802.1x - apply controlled/uncontrolled port rules */
    if ((proto != ETH_P_PAE)
#ifdef CSR_WIFI_SECURITY_WAPI_ENABLE
            && (proto != ETH_P_WAI)
#endif
       ) {
        /* queues 0 - 3 */
        priority = get_packet_priority(priv, skb, &ehdr, interfacePriv);
        queue = unifi_frame_priority_to_queue(priority);
    } else {
        /* queue 4 */
        queue = UNIFI_TRAFFIC_Q_EAPOL;
    }


    func_exit();
    return (u16)queue;
} /* uf_net_select_queue() */

int
skb_add_llc_snap(struct net_device *dev, struct sk_buff *skb, int proto)
{
    llc_snap_hdr_t *snap;
    netInterface_priv_t *interfacePriv = (netInterface_priv_t *)netdev_priv(dev);
    unifi_priv_t *priv = interfacePriv->privPtr;
    int headroom;

    /* get the headroom available in skb */
    headroom = skb_headroom(skb);
    /* step 1: classify ether frame, DIX or 802.3? */

    if (proto < 0x600) {
        /* codes <= 1500 reserved for 802.3 lengths */
        /* it's 802.3, pass ether payload unchanged,  */
        unifi_trace(priv, UDBG3, "802.3 len: %d\n", skb->len);

        /*   leave off any PAD octets.  */
        skb_trim(skb, proto);
    } else if (proto == ETH_P_8021Q) {

        /* Store the VLAN SNAP (should be 87-65). */
        u16 vlan_snap = *(u16*)skb->data;
        /* check for headroom availability before skb_push 14 = (4 + 10) */
        if (headroom < 14) {
            unifi_trace(priv, UDBG3, "cant append vlan snap: debug\n");
            return -1;
        }
        /* Add AA-AA-03-00-00-00 */
        snap = (llc_snap_hdr_t *)skb_push(skb, 4);
        snap->dsap = snap->ssap = 0xAA;
        snap->ctrl = 0x03;
        memcpy(snap->oui, oui_rfc1042, P80211_OUI_LEN);

        /* Add AA-AA-03-00-00-00 */
        snap = (llc_snap_hdr_t *)skb_push(skb, 10);
        snap->dsap = snap->ssap = 0xAA;
        snap->ctrl = 0x03;
        memcpy(snap->oui, oui_rfc1042, P80211_OUI_LEN);

        /* Add the VLAN specific information */
        snap->protocol = htons(proto);
        *(u16*)(snap + 1) = vlan_snap;

    } else
    {
        /* it's DIXII, time for some conversion */
        unifi_trace(priv, UDBG3, "DIXII len: %d\n", skb->len);

        /* check for headroom availability before skb_push */
        if (headroom < sizeof(llc_snap_hdr_t)) {
            unifi_trace(priv, UDBG3, "cant append snap: debug\n");
            return -1;
        }
        /* tack on SNAP */
        snap = (llc_snap_hdr_t *)skb_push(skb, sizeof(llc_snap_hdr_t));
        snap->dsap = snap->ssap = 0xAA;
        snap->ctrl = 0x03;
        /* Use the appropriate OUI. */
        if ((proto == ETH_P_AARP) || (proto == ETH_P_IPX)) {
            memcpy(snap->oui, oui_8021h, P80211_OUI_LEN);
        } else {
            memcpy(snap->oui, oui_rfc1042, P80211_OUI_LEN);
        }
        snap->protocol = htons(proto);
    }

    return 0;
} /* skb_add_llc_snap() */

#ifdef CSR_SUPPORT_SME
static int
_identify_sme_ma_pkt_ind(unifi_priv_t *priv,
                         const s8 *oui, u16 protocol,
                         const CSR_SIGNAL *signal,
                         bulk_data_param_t *bulkdata,
                         const unsigned char *daddr,
                         const unsigned char *saddr)
{
    CSR_MA_PACKET_INDICATION *pkt_ind = (CSR_MA_PACKET_INDICATION*)&signal->u.MaPacketIndication;
    int r;
    u8 i;

    unifi_trace(priv, UDBG5,
            "_identify_sme_ma_pkt_ind -->\n");
    for (i = 0; i < MAX_MA_UNIDATA_IND_FILTERS; i++) {
        if (priv->sme_unidata_ind_filters[i].in_use) {
            if (!memcmp(oui, priv->sme_unidata_ind_filters[i].oui, 3) &&
                    (protocol == priv->sme_unidata_ind_filters[i].protocol)) {

                /* Send to client */
                if (priv->sme_cli) {
                    /*
                     * Pass the packet to the SME, using unifi_sys_ma_unitdata_ind().
                     * The frame needs to be converted according to the encapsulation.
                     */
                    unifi_trace(priv, UDBG1,
                            "_identify_sme_ma_pkt_ind: handle=%d, encap=%d, proto=%x\n",
                            i, priv->sme_unidata_ind_filters[i].encapsulation,
                            priv->sme_unidata_ind_filters[i].protocol);
                    if (priv->sme_unidata_ind_filters[i].encapsulation == CSR_WIFI_ROUTER_ENCAPSULATION_ETHERNET) {
                        struct sk_buff *skb;
                        /* The translation is performed on skb... */
                        skb = (struct sk_buff*)bulkdata->d[0].os_net_buf_ptr;
                        skb->len = bulkdata->d[0].data_length;

                        unifi_trace(priv, UDBG1,
                                "_identify_sme_ma_pkt_ind: skb_80211_to_ether -->\n");
                        r = skb_80211_to_ether(priv, skb, daddr, saddr,
                                signal, bulkdata);
                        unifi_trace(priv, UDBG1,
                                "_identify_sme_ma_pkt_ind: skb_80211_to_ether <--\n");
                        if (r) {
                            return -EINVAL;
                        }

                        /* ... but we indicate buffer and length */
                        bulkdata->d[0].os_data_ptr = skb->data;
                        bulkdata->d[0].data_length = skb->len;
                    } else {
                        /* Add the MAC addresses before the SNAP */
                        bulkdata->d[0].os_data_ptr -= 2*ETH_ALEN;
                        bulkdata->d[0].data_length += 2*ETH_ALEN;
                        memcpy((void*)bulkdata->d[0].os_data_ptr, daddr, ETH_ALEN);
                        memcpy((void*)bulkdata->d[0].os_data_ptr + ETH_ALEN, saddr, ETH_ALEN);
                    }

                    unifi_trace(priv, UDBG1,
                            "_identify_sme_ma_pkt_ind: unifi_sys_ma_pkt_ind -->\n");
                    CsrWifiRouterMaPacketIndSend(priv->sme_unidata_ind_filters[i].appHandle,
                            (pkt_ind->VirtualInterfaceIdentifier & 0xff),
                            i,
                            pkt_ind->ReceptionStatus,
                            bulkdata->d[0].data_length,
                            (u8*)bulkdata->d[0].os_data_ptr,
                            NULL,
                            pkt_ind->Rssi,
                            pkt_ind->Snr,
                            pkt_ind->ReceivedRate);


                    unifi_trace(priv, UDBG1,
                            "_identify_sme_ma_pkt_ind: unifi_sys_ma_pkt_ind <--\n");
                }

                return 1;
            }
        }
    }

    return -1;
}
#endif /* CSR_SUPPORT_SME */

/*
 * ---------------------------------------------------------------------------
 *  skb_80211_to_ether
 *
 *      Make sure the received frame is in Ethernet (802.3) form.
 *      De-encapsulates SNAP if necessary, adds a ethernet header.
 *      The source buffer should not contain an 802.11 MAC header
 *
 *  Arguments:
 *      payload         Pointer to packet data received from UniFi.
 *      payload_length  Number of bytes of data received from UniFi.
 *      daddr           Destination MAC address.
 *      saddr           Source MAC address.
 *
 *  Returns:
 *      0 on success, -1 if the packet is bad and should be dropped,
 *      1 if the packet was forwarded to the SME or AMP client.
 * ---------------------------------------------------------------------------
 */
int
skb_80211_to_ether(unifi_priv_t *priv, struct sk_buff *skb,
                   const unsigned char *daddr, const unsigned char *saddr,
                   const CSR_SIGNAL *signal,
                   bulk_data_param_t *bulkdata)
{
    unsigned char *payload;
    int payload_length;
    struct ethhdr *eth;
    llc_snap_hdr_t *snap;
    int headroom;
#define UF_VLAN_LLC_HEADER_SIZE     18
    static const u8 vlan_inner_snap[] = { 0xAA, 0xAA, 0x03, 0x00, 0x00, 0x00 };
#if defined(CSR_NATIVE_SOFTMAC) && defined(CSR_SUPPORT_SME)
    const CSR_MA_PACKET_INDICATION *pkt_ind = &signal->u.MaPacketIndication;
#endif

    if(skb== NULL || daddr == NULL || saddr == NULL){
        unifi_error(priv,"skb_80211_to_ether: PBC fail\n");
        return 1;
    }

    payload = skb->data;
    payload_length = skb->len;

    snap = (llc_snap_hdr_t *)payload;
    eth  = (struct ethhdr *)payload;

    /* get the skb headroom size */
    headroom = skb_headroom(skb);

    /*
     * Test for the various encodings
     */
    if ((payload_length >= sizeof(llc_snap_hdr_t)) &&
            (snap->dsap == 0xAA) &&
            (snap->ssap == 0xAA) &&
            (snap->ctrl == 0x03) &&
            (snap->oui[0] == 0) &&
            (snap->oui[1] == 0) &&
            ((snap->oui[2] == 0) || (snap->oui[2] == 0xF8)))
    {
        /* AppleTalk AARP (2) or IPX SNAP */
        if ((snap->oui[2] == 0) &&
                ((ntohs(snap->protocol) == ETH_P_AARP) || (ntohs(snap->protocol) == ETH_P_IPX)))
        {
            u16 len;

            unifi_trace(priv, UDBG3, "%s len: %d\n",
                    (ntohs(snap->protocol) == ETH_P_AARP) ? "ETH_P_AARP" : "ETH_P_IPX",
                    payload_length);

            /* check for headroom availability before skb_push */
            if (headroom < (2 * ETH_ALEN + 2)) {
                unifi_warning(priv, "headroom not available to skb_push ether header\n");
                return -1;
            }

            /* Add 802.3 header and leave full payload */
            len = htons(skb->len);
            memcpy(skb_push(skb, 2), &len, 2);
            memcpy(skb_push(skb, ETH_ALEN), saddr, ETH_ALEN);
            memcpy(skb_push(skb, ETH_ALEN), daddr, ETH_ALEN);

            return 0;
        }
        /* VLAN-tagged IP */
        if ((snap->oui[2] == 0) && (ntohs(snap->protocol) == ETH_P_8021Q))
        {
            /*
             * The translation doesn't change the packet length, so is done in-place.
             *
             * Example header (from Std 802.11-2007 Annex M):
             * AA-AA-03-00-00-00-81-00-87-65-AA-AA-03-00-00-00-08-06
             * -------SNAP-------p1-p1-ll-ll-------SNAP--------p2-p2
             * dd-dd-dd-dd-dd-dd-aa-aa-aa-aa-aa-aa-p1-p1-ll-ll-p2-p2
             * dd-dd-dd-dd-dd-dd-aa-aa-aa-aa-aa-aa-81-00-87-65-08-06
             */
            u16 vlan_snap;

            if (payload_length < UF_VLAN_LLC_HEADER_SIZE) {
                unifi_warning(priv, "VLAN SNAP header too short: %d bytes\n", payload_length);
                return -1;
            }

            if (memcmp(payload + 10, vlan_inner_snap, 6)) {
                unifi_warning(priv, "VLAN malformatted SNAP header.\n");
                return -1;
            }

            unifi_trace(priv, UDBG3, "VLAN SNAP: %02x-%02x\n", payload[8], payload[9]);
            unifi_trace(priv, UDBG3, "VLAN len: %d\n", payload_length);

            /* Create the 802.3 header */

            vlan_snap = *((u16*)(payload + 8));

            /* Create LLC header without byte-swapping */
            eth->h_proto = snap->protocol;

            memcpy(eth->h_dest, daddr, ETH_ALEN);
            memcpy(eth->h_source, saddr, ETH_ALEN);
            *(u16*)(eth + 1) = vlan_snap;
            return 0;
        }

        /* it's a SNAP + RFC1042 frame */
        unifi_trace(priv, UDBG3, "SNAP+RFC1042 len: %d\n", payload_length);

        /* chop SNAP+llc header from skb. */
        skb_pull(skb, sizeof(llc_snap_hdr_t));

        /* Since skb_pull called above to chop snap+llc, no need to check for headroom
         * availability before skb_push
         */
        /* create 802.3 header at beginning of skb. */
        eth = (struct ethhdr *)skb_push(skb, ETH_HLEN);
        memcpy(eth->h_dest, daddr, ETH_ALEN);
        memcpy(eth->h_source, saddr, ETH_ALEN);
        /* Copy protocol field without byte-swapping */
        eth->h_proto = snap->protocol;
    } else {
        u16 len;

        /* check for headroom availability before skb_push */
        if (headroom < (2 * ETH_ALEN + 2)) {
            unifi_warning(priv, "headroom not available to skb_push ether header\n");
            return -1;
        }
        /* Add 802.3 header and leave full payload */
        len = htons(skb->len);
        memcpy(skb_push(skb, 2), &len, 2);
        memcpy(skb_push(skb, ETH_ALEN), saddr, ETH_ALEN);
        memcpy(skb_push(skb, ETH_ALEN), daddr, ETH_ALEN);

        return 1;
    }

    return 0;
} /* skb_80211_to_ether() */


static CsrWifiRouterCtrlPortAction verify_port(unifi_priv_t *priv, unsigned char *address, int queue, u16 interfaceTag)
{
#ifdef CSR_NATIVE_LINUX
#ifdef CSR_SUPPORT_WEXT
    if (queue == UF_CONTROLLED_PORT_Q) {
        return priv->wext_conf.block_controlled_port;
    } else {
        return CSR_WIFI_ROUTER_CTRL_PORT_ACTION_8021X_PORT_OPEN;
    }
#else
    return CSR_WIFI_ROUTER_CTRL_PORT_ACTION_8021X_PORT_OPEN; /* default to open for softmac dev */
#endif
#else
    return uf_sme_port_state(priv, address, queue, interfaceTag);
#endif
}

/*
 * ---------------------------------------------------------------------------
 *  prepare_and_add_macheader
 *
 *
 *      These functions adds mac header for packet from netdev
 *      to UniFi for transmission.
 *      EAP protocol packets are also appended with Mac header &
 *      sent using send_ma_pkt_request().
 *
 *  Arguments:
 *      priv            Pointer to device private context struct
 *      skb             Socket buffer containing data packet to transmit
 *      newSkb          Socket buffer containing data packet + Mac header if no sufficient headroom in skb
 *      serviceClass    to append QOS control header in Mac header
 *      bulkdata        if newSkb allocated then bulkdata updated to send to unifi
 *      interfaceTag    the interfaceID on which activity going on
 *      daddr           destination address
 *      saddr           source address
 *      protection      protection bit set in framce control of mac header
 *
 *  Returns:
 *      Zero on success or error code.
 * ---------------------------------------------------------------------------
 */

int prepare_and_add_macheader(unifi_priv_t *priv, struct sk_buff *skb, struct sk_buff *newSkb,
                              CSR_PRIORITY priority,
                              bulk_data_param_t *bulkdata,
                              u16 interfaceTag,
                              const u8 *daddr,
                              const u8 *saddr,
                              u8 protection)
{
    u16 fc = 0;
    u8 qc = 0;
    u8 macHeaderLengthInBytes = MAC_HEADER_SIZE, *bufPtr = NULL;
    bulk_data_param_t data_ptrs;
    CsrResult csrResult;
    int headroom =0;
    u8 direction = 0;
    netInterface_priv_t *interfacePriv = priv->interfacePriv[interfaceTag];
    u8 *addressOne;
    u8 bQosNull = false;

    if (skb == NULL) {
        unifi_error(priv,"prepare_and_add_macheader: Invalid SKB reference\n");
        return -1;
    }

    /* add a MAC header refer: 7.1.3.1 Frame Control field in P802.11REVmb.book */
    if (priority != CSR_CONTENTION) {
        /* EAPOL packets don't go as QOS_DATA */
        if (priority == CSR_MANAGEMENT) {
            fc |= cpu_to_le16(IEEE802_11_FC_TYPE_DATA);
        } else {
            /* Qos Control Field */
            macHeaderLengthInBytes += QOS_CONTROL_HEADER_SIZE;

            if (skb->len) {

                fc |= cpu_to_le16(IEEE802_11_FC_TYPE_QOS_DATA);
            } else {
                fc |= cpu_to_le16(IEEE802_11_FC_TYPE_QOS_NULL);
                bQosNull = true;
            }
        }
    } else {
        if(skb->len == 0) {
            fc |= cpu_to_le16(IEEE802_11_FC_TYPE_NULL);
        } else {
            fc |= cpu_to_le16(IEEE802_11_FC_TYPE_DATA);
        }
    }

    switch (interfacePriv->interfaceMode)
    {
        case  CSR_WIFI_ROUTER_CTRL_MODE_STA:
        case CSR_WIFI_ROUTER_CTRL_MODE_P2PCLI:
            direction = 2;
            fc |= cpu_to_le16(IEEE802_11_FC_TO_DS_MASK);
            break;
        case  CSR_WIFI_ROUTER_CTRL_MODE_IBSS:
            direction = 0;
            break;
        case  CSR_WIFI_ROUTER_CTRL_MODE_AP:
        case CSR_WIFI_ROUTER_CTRL_MODE_P2PGO:
            direction = 1;
            fc |= cpu_to_le16(IEEE802_11_FC_FROM_DS_MASK);
            break;
        case CSR_WIFI_ROUTER_CTRL_MODE_AMP:
            if (priority == CSR_MANAGEMENT ) {

                direction = 2;
                fc |= cpu_to_le16(IEEE802_11_FC_TO_DS_MASK);
            } else {
                /* Data frames have to use WDS 4 address frames */
                direction = 3;
                fc |= cpu_to_le16(IEEE802_11_FC_TO_DS_MASK | IEEE802_11_FC_FROM_DS_MASK);
                macHeaderLengthInBytes += 6;
            }
            break;
        default:
            unifi_warning(priv, "prepare_and_add_macheader: Unknown mode %d\n",
                          interfacePriv->interfaceMode);
    }


    /* If Sta is QOS & HTC is supported then need to set 'order' bit */
    /* We don't support HT Control for now */

    if(protection) {
        fc |= cpu_to_le16(IEEE802_11_FC_PROTECTED_MASK);
    }

    /* check the skb headroom before pushing mac header */
    headroom = skb_headroom(skb);

    if (headroom < macHeaderLengthInBytes) {
        unifi_trace(priv, UDBG5,
                    "prepare_and_add_macheader: Allocate headroom extra %d bytes\n",
                    macHeaderLengthInBytes);

        csrResult = unifi_net_data_malloc(priv, &data_ptrs.d[0], skb->len + macHeaderLengthInBytes);

        if (csrResult != CSR_RESULT_SUCCESS) {
            unifi_error(priv, " failed to allocate request_data. in %s func\n", __FUNCTION__);
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

        /* headroom has sufficient size, so will get proper pointer */
        bufPtr = (u8*)skb_push(skb, macHeaderLengthInBytes);
        bulkdata->d[0].os_data_ptr = skb->data;
        bulkdata->d[0].os_net_buf_ptr = (unsigned char*)skb;
        bulkdata->d[0].data_length = skb->len;
    }

    /* Frame the actual MAC header */

    memset(bufPtr, 0, macHeaderLengthInBytes);

    /* copy frameControl field */
    memcpy(bufPtr, &fc, sizeof(fc));
    bufPtr += sizeof(fc);
    macHeaderLengthInBytes -= sizeof(fc);

    /* Duration/ID field which is 2 bytes */
    bufPtr += 2;
    macHeaderLengthInBytes -= 2;

    switch(direction)
    {
        case 0:
            /* Its an Ad-Hoc no need to route it through AP */
            /* Address1: MAC address of the destination from eth header */
            memcpy(bufPtr, daddr, ETH_ALEN);
            bufPtr += ETH_ALEN;
            macHeaderLengthInBytes -= ETH_ALEN;

            /* Address2: MAC address of the source */
            memcpy(bufPtr, saddr, ETH_ALEN);
            bufPtr += ETH_ALEN;
            macHeaderLengthInBytes -= ETH_ALEN;

            /* Address3: the BSSID (locally generated in AdHoc (creators Bssid)) */
            memcpy(bufPtr, &interfacePriv->bssid, ETH_ALEN);
            bufPtr += ETH_ALEN;
            macHeaderLengthInBytes -= ETH_ALEN;
            break;
        case 1:
           /* Address1: MAC address of the actual destination */
            memcpy(bufPtr, daddr, ETH_ALEN);
            bufPtr += ETH_ALEN;
            macHeaderLengthInBytes -= ETH_ALEN;
            /* Address2: The MAC address of the AP */
            memcpy(bufPtr, &interfacePriv->bssid, ETH_ALEN);
            bufPtr += ETH_ALEN;
            macHeaderLengthInBytes -= ETH_ALEN;

            /* Address3: MAC address of the source from eth header */
            memcpy(bufPtr, saddr, ETH_ALEN);
            bufPtr += ETH_ALEN;
            macHeaderLengthInBytes -= ETH_ALEN;
            break;
        case  2:
            /* Address1: To AP is the MAC address of the AP to which its associated */
            memcpy(bufPtr, &interfacePriv->bssid, ETH_ALEN);
            bufPtr += ETH_ALEN;
            macHeaderLengthInBytes -= ETH_ALEN;

            /* Address2: MAC address of the source from eth header */
            memcpy(bufPtr, saddr, ETH_ALEN);
            bufPtr += ETH_ALEN;
            macHeaderLengthInBytes -= ETH_ALEN;

            /* Address3: MAC address of the actual destination on the distribution system */
            memcpy(bufPtr, daddr, ETH_ALEN);
            bufPtr += ETH_ALEN;
            macHeaderLengthInBytes -= ETH_ALEN;
            break;
        case 3:
            memcpy(bufPtr, &interfacePriv->bssid, ETH_ALEN);
            bufPtr += ETH_ALEN;
            macHeaderLengthInBytes -= ETH_ALEN;

            /* Address2: MAC address of the source from eth header */
            memcpy(bufPtr, saddr, ETH_ALEN);
            bufPtr += ETH_ALEN;
            macHeaderLengthInBytes -= ETH_ALEN;

            /* Address3: MAC address of the actual destination on the distribution system */
            memcpy(bufPtr, daddr, ETH_ALEN);
            bufPtr += ETH_ALEN;
            macHeaderLengthInBytes -= ETH_ALEN;
            break;
        default:
            unifi_error(priv,"Unknown direction =%d : Not handled now\n",direction);
            return -1;
    }
    /* 2 bytes of frame control field, appended by firmware */
    bufPtr += 2;
    macHeaderLengthInBytes -= 2;

    if (3 == direction) {
        /* Address4: MAC address of the source */
        memcpy(bufPtr, saddr, ETH_ALEN);
        bufPtr += ETH_ALEN;
        macHeaderLengthInBytes -= ETH_ALEN;
    }

    /* IF Qos Data or Qos Null Data then set QosControl field */
    if ((priority != CSR_CONTENTION) && (macHeaderLengthInBytes >= QOS_CONTROL_HEADER_SIZE)) {

        if (priority > 7) {
            unifi_trace(priv, UDBG1, "data packets priority is more than 7, priority = %x\n", priority);
            qc |= 7;
        } else {
            qc |= priority;
        }
        /*assigning address1
        * Address1 offset taken fromm bufPtr(currently bufPtr pointing to Qos contorl) variable in reverse direction
        * Address4 don't exit
        */

        addressOne = bufPtr- ADDRESS_ONE_OFFSET;

        if (addressOne[0] & 0x1) {
            /* multicast/broadcast frames, no acknowledgement needed */
            qc |= 1 << 5;
        }
        /* non-AP mode only for now */
        if(interfacePriv->interfaceMode == CSR_WIFI_ROUTER_CTRL_MODE_STA ||
           interfacePriv->interfaceMode == CSR_WIFI_ROUTER_CTRL_MODE_IBSS ||
           interfacePriv->interfaceMode == CSR_WIFI_ROUTER_CTRL_MODE_P2PCLI) {
           /* In case of STA and IBSS case eosp and txop limit is 0. */
        } else {
            if(bQosNull) {
                qc |= 1 << 4;
            }
        }

        /* append Qos control field to mac header */
        bufPtr[0] = qc;
        /* txop limit is 0 */
        bufPtr[1] = 0;
        macHeaderLengthInBytes -= QOS_CONTROL_HEADER_SIZE;
    }
    if (macHeaderLengthInBytes) {
        unifi_warning(priv, " Mac header not appended properly\n");
        return -1;
    }
    return 0;
}

/*
 * ---------------------------------------------------------------------------
 *  send_ma_pkt_request
 *
 *      These functions send a data packet to UniFi for transmission.
 *      EAP protocol packets are also sent as send_ma_pkt_request().
 *
 *  Arguments:
 *      priv            Pointer to device private context struct
 *      skb             Socket buffer containing data packet to transmit
 *      ehdr            Pointer to Ethernet header within skb.
 *
 *  Returns:
 *      Zero on success or error code.
 * ---------------------------------------------------------------------------
 */

static int
send_ma_pkt_request(unifi_priv_t *priv, struct sk_buff *skb, const struct ethhdr *ehdr, CSR_PRIORITY priority)
{
    int r;
    u16 i;
    u8 eapolStore = FALSE;
    struct sk_buff *newSkb = NULL;
    bulk_data_param_t bulkdata;
    const int proto = ntohs(ehdr->h_proto);
    u16 interfaceTag;
    CsrWifiMacAddress peerAddress;
    CSR_TRANSMISSION_CONTROL transmissionControl = CSR_NO_CONFIRM_REQUIRED;
    s8 protection;
    netInterface_priv_t *interfacePriv = NULL;
    CSR_RATE TransmitRate = (CSR_RATE)0;

    unifi_trace(priv, UDBG5, "entering send_ma_pkt_request\n");

    /* Get the interface Tag by means of source Mac address */
    for (i = 0; i < CSR_WIFI_NUM_INTERFACES; i++) {
        if (!memcmp(priv->netdev[i]->dev_addr, ehdr->h_source, ETH_ALEN)) {
            interfaceTag = i;
            interfacePriv = priv->interfacePriv[interfaceTag];
            break;
        }
    }

    if (interfacePriv == NULL) {
        /* No match found - error */
        interfaceTag = 0;
        interfacePriv = priv->interfacePriv[interfaceTag];
        unifi_warning(priv, "Mac address not matching ... debugging needed\n");
        interfacePriv->stats.tx_dropped++;
        kfree_skb(skb);
        return -1;
    }

    /* Add a SNAP header if necessary */
    if (skb_add_llc_snap(priv->netdev[interfaceTag], skb, proto) != 0) {
        /* convert failed */
        unifi_error(priv, "skb_add_llc_snap failed.\n");
        kfree_skb(skb);
        return -1;
    }

    bulkdata.d[0].os_data_ptr = skb->data;
    bulkdata.d[0].os_net_buf_ptr = (unsigned char*)skb;
    bulkdata.d[0].net_buf_length = bulkdata.d[0].data_length = skb->len;
    bulkdata.d[1].os_data_ptr = NULL;
    bulkdata.d[1].os_net_buf_ptr = NULL;
    bulkdata.d[1].net_buf_length = bulkdata.d[1].data_length = 0;

#ifdef CSR_SUPPORT_SME
    /* Notify the TA module for the Tx frame  for non AP/P2PGO mode*/
    if ((interfacePriv->interfaceMode != CSR_WIFI_ROUTER_CTRL_MODE_AP) &&
        (interfacePriv->interfaceMode != CSR_WIFI_ROUTER_CTRL_MODE_P2PGO)) {
        unifi_ta_sample(priv->card, CSR_WIFI_ROUTER_CTRL_PROTOCOL_DIRECTION_TX,
                        &bulkdata.d[0], ehdr->h_source,
                        priv->netdev[interfaceTag]->dev_addr,
                        jiffies_to_msecs(jiffies),
                        0);     /* rate is unknown on tx */
    }
#endif /* CSR_SUPPORT_SME */

    if ((proto == ETH_P_PAE)
#ifdef CSR_WIFI_SECURITY_WAPI_ENABLE
            || (proto == ETH_P_WAI)
#endif
       )
    {
        /* check for m4 detection */
        if (0 == uf_verify_m4(priv, bulkdata.d[0].os_data_ptr, bulkdata.d[0].data_length)) {
            eapolStore = TRUE;
        }
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
    if ((protection = uf_get_protection_bit_from_interfacemode(priv, interfaceTag, ehdr->h_dest)) < 0) {
        unifi_warning(priv, "unicast address, but destination not in station record database\n");
        unifi_net_data_free(priv, &bulkdata.d[0]);
        return -1;
    }
#else
    protection = 0;
#endif
#ifdef CSR_WIFI_SECURITY_WAPI_ENABLE
   }
#endif

    /* append Mac header for Eapol as well as data packet */
    if (prepare_and_add_macheader(priv, skb, newSkb, priority, &bulkdata, interfaceTag, ehdr->h_dest, ehdr->h_source, protection)) {
        unifi_error(priv, "failed to create MAC header\n");
        unifi_net_data_free(priv, &bulkdata.d[0]);
        return -1;
    }

    /* RA adrress must contain the immediate destination MAC address that is similiar to
     * the Address 1 field of 802.11 Mac header here 4 is: (sizeof(framecontrol) + sizeof (durationID))
     * which is address 1 field
     */
    memcpy(peerAddress.a, ((u8 *) bulkdata.d[0].os_data_ptr) + 4, ETH_ALEN);

    unifi_trace(priv, UDBG5, "RA[0]=%x, RA[1]=%x, RA[2]=%x, RA[3]=%x, RA[4]=%x, RA[5]=%x\n",
                peerAddress.a[0],peerAddress.a[1], peerAddress.a[2], peerAddress.a[3],
                peerAddress.a[4],peerAddress.a[5]);


    if ((proto == ETH_P_PAE)
#ifdef CSR_WIFI_SECURITY_WAPI_ENABLE
            || (proto == ETH_P_WAI)
#endif
       )
    {
        CSR_SIGNAL signal;
        CSR_MA_PACKET_REQUEST *req = &signal.u.MaPacketRequest;

        /* initialize signal to zero */
        memset(&signal, 0, sizeof(CSR_SIGNAL));

        /* Frame MA_PACKET request */
        signal.SignalPrimitiveHeader.SignalId = CSR_MA_PACKET_REQUEST_ID;
        signal.SignalPrimitiveHeader.ReceiverProcessId = 0;
        signal.SignalPrimitiveHeader.SenderProcessId = priv->netdev_client->sender_id;

        transmissionControl = req->TransmissionControl = 0;
#ifdef CSR_SUPPORT_SME
        if (eapolStore)
        {
            netInterface_priv_t *netpriv = (netInterface_priv_t *)netdev_priv(priv->netdev[interfaceTag]);

            /* Fill the MA-PACKET.req */

            req->Priority = priority;
            unifi_trace(priv, UDBG3, "Tx Frame with Priority: %x\n", req->Priority);

            /* rate selected by firmware */
            req->TransmitRate = 0;
            req->HostTag = CSR_WIFI_EAPOL_M4_HOST_TAG;
            /* RA address matching with address 1 of Mac header */
            memcpy(req->Ra.x, ((u8 *) bulkdata.d[0].os_data_ptr) + 4, ETH_ALEN);

            spin_lock(&priv->m4_lock);
            /* Store the M4-PACKET.req for later */
            interfacePriv->m4_signal = signal;
            interfacePriv->m4_bulk_data.net_buf_length = bulkdata.d[0].net_buf_length;
            interfacePriv->m4_bulk_data.data_length = bulkdata.d[0].data_length;
            interfacePriv->m4_bulk_data.os_data_ptr = bulkdata.d[0].os_data_ptr;
            interfacePriv->m4_bulk_data.os_net_buf_ptr = bulkdata.d[0].os_net_buf_ptr;
            spin_unlock(&priv->m4_lock);

            /* Signal the workqueue to call CsrWifiRouterCtrlM4ReadyToSendIndSend().
             * It cannot be called directly from the tx path because it
             * does a non-atomic kmalloc via the framework's CsrPmemAlloc().
             */
            queue_work(priv->unifi_workqueue, &netpriv->send_m4_ready_task);

            return 0;
        }
#endif
    }/*EAPOL or WAI packet*/

#if (defined(CSR_WIFI_SECURITY_WAPI_ENABLE) && defined(CSR_WIFI_SECURITY_WAPI_SW_ENCRYPTION))
    if ((CSR_WIFI_ROUTER_CTRL_MODE_STA == interfacePriv->interfaceMode) && \
        (priv->wapi_unicast_filter) && \
        (proto != ETH_P_PAE) && \
        (proto != ETH_P_WAI) && \
        (skb->len > 0))
    {
        CSR_SIGNAL signal;
        CSR_MA_PACKET_REQUEST *req = &signal.u.MaPacketRequest;
        netInterface_priv_t *netpriv = (netInterface_priv_t *)netdev_priv(priv->netdev[interfaceTag]);

        unifi_trace(priv, UDBG4, "send_ma_pkt_request() - WAPI unicast data packet when USKID = 1 \n");

        /* initialize signal to zero */
        memset(&signal, 0, sizeof(CSR_SIGNAL));
        /* Frame MA_PACKET request */
        signal.SignalPrimitiveHeader.SignalId = CSR_MA_PACKET_REQUEST_ID;
        signal.SignalPrimitiveHeader.ReceiverProcessId = 0;
        signal.SignalPrimitiveHeader.SenderProcessId = priv->netdev_client->sender_id;

        /* Fill the MA-PACKET.req */
        req->TransmissionControl = 0;
        req->Priority = priority;
        unifi_trace(priv, UDBG3, "Tx Frame with Priority: %x\n", req->Priority);
        req->TransmitRate = (CSR_RATE) 0; /* rate selected by firmware */
        req->HostTag = 0xffffffff;        /* Ask for a new HostTag */
        /* RA address matching with address 1 of Mac header */
        memcpy(req->Ra.x, ((u8 *) bulkdata.d[0].os_data_ptr) + 4, ETH_ALEN);

        /* Store the M4-PACKET.req for later */
        spin_lock(&priv->wapi_lock);
        interfacePriv->wapi_unicast_ma_pkt_sig = signal;
        interfacePriv->wapi_unicast_bulk_data.net_buf_length = bulkdata.d[0].net_buf_length;
        interfacePriv->wapi_unicast_bulk_data.data_length = bulkdata.d[0].data_length;
        interfacePriv->wapi_unicast_bulk_data.os_data_ptr = bulkdata.d[0].os_data_ptr;
        interfacePriv->wapi_unicast_bulk_data.os_net_buf_ptr = bulkdata.d[0].os_net_buf_ptr;
        spin_unlock(&priv->wapi_lock);

        /* Signal the workqueue to call CsrWifiRouterCtrlWapiUnicastTxEncryptIndSend().
         * It cannot be called directly from the tx path because it
         * does a non-atomic kmalloc via the framework's CsrPmemAlloc().
         */
        queue_work(priv->unifi_workqueue, &netpriv->send_pkt_to_encrypt);

        return 0;
    }
#endif

    if(priv->cmanrTestMode)
    {
        TransmitRate = priv->cmanrTestModeTransmitRate;
        unifi_trace(priv, UDBG2, "send_ma_pkt_request: cmanrTestModeTransmitRate = %d TransmitRate=%d\n",
                    priv->cmanrTestModeTransmitRate,
                    TransmitRate
                   );
    }

    /* Send UniFi msg */
    /* Here hostTag is been sent as 0xffffffff, its been appended properly while framing MA-Packet request in pdu_processing.c file */
    r = uf_process_ma_packet_req(priv,
                                 peerAddress.a,
                                 0xffffffff,  /* Ask for a new HostTag */
                                 interfaceTag,
                                 transmissionControl,
                                 TransmitRate,
                                 priority,
                                 priv->netdev_client->sender_id,
                                 &bulkdata);

    if (r) {
        unifi_trace(priv, UDBG1, "(HIP validation failure) r = %x\n", r);
        unifi_net_data_free(priv, &bulkdata.d[0]);
        return -1;
    }

    unifi_trace(priv, UDBG3, "leaving send_ma_pkt_request, UNITDATA result code = %d\n", r);

    return r;
} /* send_ma_pkt_request() */

/*
 * ---------------------------------------------------------------------------
 *  uf_net_xmit
 *
 *      This function is called by the higher level stack to transmit an
 *      ethernet packet.
 *
 *  Arguments:
 *      skb     Ethernet packet to send.
 *      dev     Pointer to the linux net device.
 *
 *  Returns:
 *      0   on success (packet was consumed, not necessarily transmitted)
 *      1   if packet was requeued
 *     -1   on error
 *
 *
 *  Notes:
 *      The controlled port is handled in the qdisc dequeue handler.
 * ---------------------------------------------------------------------------
 */
static netdev_tx_t
uf_net_xmit(struct sk_buff *skb, struct net_device *dev)
{
    netInterface_priv_t *interfacePriv = (netInterface_priv_t *)netdev_priv(dev);
    unifi_priv_t *priv = interfacePriv->privPtr;
    struct ethhdr ehdr;
    int proto, port;
    int result;
    static tx_signal_handler tx_handler;
    CSR_PRIORITY priority;
    CsrWifiRouterCtrlPortAction port_action;

    func_enter();

    unifi_trace(priv, UDBG5, "unifi_net_xmit: skb = %x\n", skb);

    memcpy(&ehdr, skb->data, ETH_HLEN);
    proto = ntohs(ehdr.h_proto);
    priority = get_packet_priority(priv, skb, &ehdr, interfacePriv);

    /* All frames are sent as MA-PACKET.req (EAPOL also) */
    tx_handler = send_ma_pkt_request;

    /* 802.1x - apply controlled/uncontrolled port rules */
    if ((proto != ETH_P_PAE)
#ifdef CSR_WIFI_SECURITY_WAPI_ENABLE
            && (proto != ETH_P_WAI)
#endif
       ) {
        port = UF_CONTROLLED_PORT_Q;
    } else {
        /* queue 4 */
        port = UF_UNCONTROLLED_PORT_Q;
    }

    /* Uncontrolled port rules apply */
    port_action = verify_port(priv
        , (((CSR_WIFI_ROUTER_CTRL_MODE_STA == interfacePriv->interfaceMode)||(CSR_WIFI_ROUTER_CTRL_MODE_P2PCLI== interfacePriv->interfaceMode))? interfacePriv->bssid.a: ehdr.h_dest)
        , port
        , interfacePriv->InterfaceTag);

    if (port_action == CSR_WIFI_ROUTER_CTRL_PORT_ACTION_8021X_PORT_OPEN) {
        unifi_trace(priv, UDBG5,
                    "uf_net_xmit: %s controlled port open\n",
                    port ? "" : "un");
        /* Remove the ethernet header */
        skb_pull(skb, ETH_HLEN);
        result = tx_handler(priv, skb, &ehdr, priority);
    } else {

        /* Discard the packet if necessary */
        unifi_trace(priv, UDBG2,
                "uf_net_xmit: %s controlled port %s\n",
                port ? "" : "un", port_action==CSR_WIFI_ROUTER_CTRL_PORT_ACTION_8021X_PORT_CLOSED_BLOCK ? "blocked" : "closed");
        interfacePriv->stats.tx_dropped++;
        kfree_skb(skb);

        func_exit();
        return NETDEV_TX_OK;
    }

    if (result == NETDEV_TX_OK) {
#if (defined(CSR_WIFI_SECURITY_WAPI_ENABLE) && defined(CSR_WIFI_SECURITY_WAPI_SW_ENCRYPTION))
    	/* Don't update the tx stats when the pkt is to be sent for sw encryption*/
    	if (!((CSR_WIFI_ROUTER_CTRL_MODE_STA == interfacePriv->interfaceMode) &&
              (priv->wapi_unicast_filter == 1)))
        {
            dev->trans_start = jiffies;
            /* Should really count tx stats in the UNITDATA.status signal but
             * that doesn't have the length.
             */
            interfacePriv->stats.tx_packets++;
            /* count only the packet payload */
            interfacePriv->stats.tx_bytes += skb->len;

        }
#else
    	dev->trans_start = jiffies;

        /*
         * Should really count tx stats in the UNITDATA.status signal but
         * that doesn't have the length.
         */
        interfacePriv->stats.tx_packets++;
        /* count only the packet payload */
        interfacePriv->stats.tx_bytes += skb->len;
#endif
    } else if (result < 0) {

        /* Failed to send: fh queue was full, and the skb was discarded.
         * Return OK to indicate that the buffer was consumed, to stop the
         * kernel re-transmitting the freed buffer.
         */
        interfacePriv->stats.tx_dropped++;
        unifi_trace(priv, UDBG1, "unifi_net_xmit: (Packet Drop), dropped count = %x\n", interfacePriv->stats.tx_dropped);
        result = NETDEV_TX_OK;
    }

    /* The skb will have been freed by send_XXX_request() */

    func_exit();
    return result;
} /* uf_net_xmit() */

/*
 * ---------------------------------------------------------------------------
 *  unifi_pause_xmit
 *  unifi_restart_xmit
 *
 *      These functions are called from the UniFi core to control the flow
 *      of packets from the upper layers.
 *      unifi_pause_xmit() is called when the internal queue is full and
 *      should take action to stop unifi_ma_unitdata() being called.
 *      When the queue has drained, unifi_restart_xmit() will be called to
 *      re-enable the flow of packets for transmission.
 *
 *  Arguments:
 *      ospriv          OS private context pointer.
 *
 *  Returns:
 *      unifi_pause_xmit() is called from interrupt context.
 * ---------------------------------------------------------------------------
 */
void
unifi_pause_xmit(void *ospriv, unifi_TrafficQueue queue)
{
    unifi_priv_t *priv = ospriv;
    int i; /* used as a loop counter */

    func_enter();
    unifi_trace(priv, UDBG2, "Stopping queue %d\n", queue);

    for(i=0;i<CSR_WIFI_NUM_INTERFACES;i++)
    {
        if (netif_running(priv->netdev[i]))
        {
            netif_stop_subqueue(priv->netdev[i], (u16)queue);
        }
    }

#ifdef CSR_SUPPORT_SME
    if(queue<=3) {
        routerStartBuffering(priv,queue);
        unifi_trace(priv,UDBG2,"Start buffering %d\n", queue);
     } else {
        routerStartBuffering(priv,0);
        unifi_error(priv, "Start buffering %d defaulting to 0\n", queue);
     }
#endif
    func_exit();

} /* unifi_pause_xmit() */

void
unifi_restart_xmit(void *ospriv, unifi_TrafficQueue queue)
{
    unifi_priv_t *priv = ospriv;
    int i=0; /* used as a loop counter */

    func_enter();
    unifi_trace(priv, UDBG2, "Waking queue %d\n", queue);

    for(i=0;i<CSR_WIFI_NUM_INTERFACES;i++)
    {
        if (netif_running(priv->netdev[i]))
        {
            netif_wake_subqueue(priv->netdev[i], (u16)queue);
        }
    }

#ifdef CSR_SUPPORT_SME
    if(queue <=3) {
        routerStopBuffering(priv,queue);
        uf_send_buffered_frames(priv,queue);
    } else {
        routerStopBuffering(priv,0);
        uf_send_buffered_frames(priv,0);
    }
#endif
    func_exit();
} /* unifi_restart_xmit() */


static void
indicate_rx_skb(unifi_priv_t *priv, u16 ifTag, u8* dst_a, u8* src_a, struct sk_buff *skb, CSR_SIGNAL *signal,
                bulk_data_param_t *bulkdata)
{
    int r, sr = 0;
    struct net_device *dev;

#ifdef CSR_SUPPORT_SME
    llc_snap_hdr_t *snap;

    snap = (llc_snap_hdr_t *)skb->data;

    sr = _identify_sme_ma_pkt_ind(priv,
                                  snap->oui, ntohs(snap->protocol),
                                  signal,
                                  bulkdata,
                                  dst_a, src_a );
#endif

    /*
     * Decapsulate any SNAP header and
     * prepend an ethernet header so that the skb manipulation and ARP
     * stuff works.
     */
    r = skb_80211_to_ether(priv, skb, dst_a, src_a,
                           signal, bulkdata);
    if (r == -1) {
        /* Drop the packet and return */
        priv->interfacePriv[ifTag]->stats.rx_errors++;
        priv->interfacePriv[ifTag]->stats.rx_frame_errors++;
        unifi_net_data_free(priv, &bulkdata->d[0]);
        unifi_notice(priv, "indicate_rx_skb: Discard unknown frame.\n");
        func_exit();
        return;
    }

    /* Handle the case where packet is sent up through the subscription
     * API but should not be given to the network stack (AMP PAL case)
     * LLC header is different from WiFi and the packet has been subscribed for
     */
    if (r == 1 && sr == 1) {
        unifi_net_data_free(priv, &bulkdata->d[0]);
        unifi_trace(priv, UDBG5, "indicate_rx_skb: Data given to subscription"
                "API, not being given to kernel\n");
        func_exit();
        return;
    }

    dev = priv->netdev[ifTag];
    /* Now we look like a regular ethernet frame */
    /* Fill in SKB meta data */
    skb->dev = dev;
    skb->protocol = eth_type_trans(skb, dev);
    skb->ip_summed = CHECKSUM_UNNECESSARY;

    /* Test for an overlength frame */
    if (skb->len > (dev->mtu + ETH_HLEN)) {
        /* A bogus length ethfrm has been encap'd. */
        /* Is someone trying an oflow attack? */
        unifi_error(priv, "%s: oversize frame (%d > %d)\n",
                    dev->name,
                    skb->len, dev->mtu + ETH_HLEN);

        /* Drop the packet and return */
        priv->interfacePriv[ifTag]->stats.rx_errors++;
        priv->interfacePriv[ifTag]->stats.rx_length_errors++;
        unifi_net_data_free(priv, &bulkdata->d[0]);
        func_exit();
        return;
    }


    if(priv->cmanrTestMode)
    {
        const CSR_MA_PACKET_INDICATION *pkt_ind = &signal->u.MaPacketIndication;
        priv->cmanrTestModeTransmitRate = pkt_ind->ReceivedRate;
        unifi_trace(priv, UDBG2, "indicate_rx_skb: cmanrTestModeTransmitRate=%d\n", priv->cmanrTestModeTransmitRate);
    }

    /* Pass SKB up the stack */
#ifdef CSR_WIFI_USE_NETIF_RX
        netif_rx(skb);
#else
        netif_rx_ni(skb);
#endif

    if (dev != NULL) {
        dev->last_rx = jiffies;
    }

    /* Bump rx stats */
    priv->interfacePriv[ifTag]->stats.rx_packets++;
    priv->interfacePriv[ifTag]->stats.rx_bytes += bulkdata->d[0].data_length;

    func_exit();
    return;
}

void
uf_process_rx_pending_queue(unifi_priv_t *priv, int queue,
                            CsrWifiMacAddress source_address,
                            int indicate, u16 interfaceTag)
{
    rx_buffered_packets_t *rx_q_item;
    struct list_head *rx_list;
    struct list_head *n;
    struct list_head *l_h;
    static const CsrWifiMacAddress broadcast_address = {{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}};
    netInterface_priv_t *interfacePriv = priv->interfacePriv[interfaceTag];

    if (interfaceTag >= CSR_WIFI_NUM_INTERFACES) {
        unifi_error(priv, "uf_process_rx_pending_queue bad interfaceTag\n");
        return;
    }

    if (queue == UF_CONTROLLED_PORT_Q) {
        rx_list = &interfacePriv->rx_controlled_list;
    } else {
        rx_list = &interfacePriv->rx_uncontrolled_list;
    }

    down(&priv->rx_q_sem);
    list_for_each_safe(l_h, n, rx_list) {
        rx_q_item = list_entry(l_h, rx_buffered_packets_t, q);

        /* Validate against the source address */
        if (memcmp(broadcast_address.a, source_address.a, ETH_ALEN) &&
                memcmp(rx_q_item->sa.a, source_address.a, ETH_ALEN)) {

            unifi_trace(priv, UDBG2,
                        "uf_process_rx_pending_queue: Skipping sa=%02X%02X%02X%02X%02X%02X skb=%p, bulkdata=%p\n",
                        rx_q_item->sa.a[0], rx_q_item->sa.a[1],
                        rx_q_item->sa.a[2], rx_q_item->sa.a[3],
                        rx_q_item->sa.a[4], rx_q_item->sa.a[5],
                        rx_q_item->skb, &rx_q_item->bulkdata.d[0]);
            continue;
        }

        list_del(l_h);


        unifi_trace(priv, UDBG2,
                    "uf_process_rx_pending_queue: Was Blocked skb=%p, bulkdata=%p\n",
                    rx_q_item->skb, &rx_q_item->bulkdata);

        if (indicate) {
            indicate_rx_skb(priv, interfaceTag, rx_q_item->da.a, rx_q_item->sa.a, rx_q_item->skb, &rx_q_item->signal, &rx_q_item->bulkdata);
        } else {
            interfacePriv->stats.rx_dropped++;
            unifi_net_data_free(priv, &rx_q_item->bulkdata.d[0]);
        }

        /* It is our resposibility to free the Rx structure object. */
        kfree(rx_q_item);
    }
    up(&priv->rx_q_sem);
}

/*
 * ---------------------------------------------------------------------------
 *  uf_resume_data_plane
 *
 *      Is called when the (un)controlled port is set to open,
 *      to notify the network stack to schedule for transmission
 *      any packets queued in the qdisk while port was closed and
 *      indicated to the stack any packets buffered in the Rx queues.
 *
 *  Arguments:
 *      priv        Pointer to device private struct
 *
 *  Returns:
 * ---------------------------------------------------------------------------
 */
void
uf_resume_data_plane(unifi_priv_t *priv, int queue,
                     CsrWifiMacAddress peer_address,
                     u16 interfaceTag)
{
#ifdef CSR_SUPPORT_WEXT
    netInterface_priv_t *interfacePriv = priv->interfacePriv[interfaceTag];
#endif

    if (interfaceTag >= CSR_WIFI_NUM_INTERFACES) {
        unifi_error(priv, "uf_resume_data_plane bad interfaceTag\n");
        return;
    }

    unifi_trace(priv, UDBG2, "Resuming netif\n");

    /*
     * If we are waiting for the net device to enter the up state, don't
     * process the rx queue yet as it will be done by the callback when
     * the device is ready.
     */
#ifdef CSR_SUPPORT_WEXT
    if (!interfacePriv->wait_netdev_change)
#endif
    {
#ifdef CONFIG_NET_SCHED
        if (netif_running(priv->netdev[interfaceTag])) {
            netif_tx_schedule_all(priv->netdev[interfaceTag]);
        }
#endif
        uf_process_rx_pending_queue(priv, queue, peer_address, 1,interfaceTag);
    }
} /* uf_resume_data_plane() */


void uf_free_pending_rx_packets(unifi_priv_t *priv, int queue, CsrWifiMacAddress peer_address,u16 interfaceTag)
{
    uf_process_rx_pending_queue(priv, queue, peer_address, 0,interfaceTag);

} /* uf_free_pending_rx_packets() */


/*
 * ---------------------------------------------------------------------------
 *  unifi_rx
 *
 *      Reformat a UniFi data received packet into a p80211 packet and
 *      pass it up the protocol stack.
 *
 *  Arguments:
 *      None.
 *
 *  Returns:
 *      None.
 * ---------------------------------------------------------------------------
 */
static void
unifi_rx(unifi_priv_t *priv, CSR_SIGNAL *signal, bulk_data_param_t *bulkdata)
{
    u16 interfaceTag;
    bulk_data_desc_t *pData;
    const CSR_MA_PACKET_INDICATION *pkt_ind = &signal->u.MaPacketIndication;
    struct sk_buff *skb;
    CsrWifiRouterCtrlPortAction port_action;
    u8 dataFrameType;
    int proto;
    int queue;

    u8 da[ETH_ALEN], sa[ETH_ALEN];
    u8 toDs, fromDs, frameType, macHeaderLengthInBytes = MAC_HEADER_SIZE;
    u16 frameControl;
    netInterface_priv_t *interfacePriv;
    struct ethhdr ehdr;

    func_enter();

    interfaceTag = (pkt_ind->VirtualInterfaceIdentifier & 0xff);
    interfacePriv = priv->interfacePriv[interfaceTag];

    /* Sanity check that the VIF refers to a sensible interface */
    if (interfaceTag >= CSR_WIFI_NUM_INTERFACES)
    {
        unifi_error(priv, "%s: MA-PACKET indication with bad interfaceTag %d\n", __FUNCTION__, interfaceTag);
        unifi_net_data_free(priv,&bulkdata->d[0]);
        func_exit();
        return;
    }

    /* Sanity check that the VIF refers to an allocated netdev */
    if (!interfacePriv->netdev_registered)
    {
        unifi_error(priv, "%s: MA-PACKET indication with unallocated interfaceTag %d\n", __FUNCTION__, interfaceTag);
        unifi_net_data_free(priv, &bulkdata->d[0]);
        func_exit();
        return;
    }

    if (bulkdata->d[0].data_length == 0) {
        unifi_warning(priv, "%s: MA-PACKET indication with zero bulk data\n", __FUNCTION__);
        unifi_net_data_free(priv,&bulkdata->d[0]);
        func_exit();
        return;
    }


    skb = (struct sk_buff*)bulkdata->d[0].os_net_buf_ptr;
    skb->len = bulkdata->d[0].data_length;

    /* Point to the addresses */
    toDs = (skb->data[1] & 0x01) ? 1 : 0;
    fromDs = (skb->data[1] & 0x02) ? 1 : 0;

    memcpy(da,(skb->data+4+toDs*12),ETH_ALEN);/* Address1 or 3 */
    memcpy(sa,(skb->data+10+fromDs*(6+toDs*8)),ETH_ALEN); /* Address2, 3 or 4 */


    pData = &bulkdata->d[0];
    frameControl = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(pData->os_data_ptr);
    frameType = ((frameControl & 0x000C) >> 2);

    dataFrameType =((frameControl & 0x00f0) >> 4);
    unifi_trace(priv, UDBG6,
                "%s: Receive Data Frame Type %d \n", __FUNCTION__,dataFrameType);

    switch(dataFrameType)
    {
        case QOS_DATA:
        case QOS_DATA_NULL:
            /* If both are set then the Address4 exists (only for AP) */
            if (fromDs && toDs)
            {
                /* 6 is the size of Address4 field */
                macHeaderLengthInBytes += (QOS_CONTROL_HEADER_SIZE + 6);
            }
            else
            {
                macHeaderLengthInBytes += QOS_CONTROL_HEADER_SIZE;
            }

            /* If order bit set then HT control field is the part of MAC header */
            if (frameControl & FRAME_CONTROL_ORDER_BIT)
                macHeaderLengthInBytes += HT_CONTROL_HEADER_SIZE;
            break;
        default:
            if (fromDs && toDs)
                macHeaderLengthInBytes += 6;
    }

    /* Prepare the ethernet header from snap header of skb data */
    switch(dataFrameType)
    {
        case DATA_NULL:
        case QOS_DATA_NULL:
            /* This is for only queue info fetching, EAPOL wont come as
             * null data so the proto is initialized as zero
             */
            proto = 0x0;
            break;
        default:
            {
                llc_snap_hdr_t *snap;
                /* Fetch a snap header to find protocol (for IPV4/IPV6 packets
                 * the snap header fetching offset is same)
                 */
                snap = (llc_snap_hdr_t *) (skb->data + macHeaderLengthInBytes);

                /* prepare the ethernet header from the snap header & addresses */
                ehdr.h_proto = snap->protocol;
                memcpy(ehdr.h_dest, da, ETH_ALEN);
                memcpy(ehdr.h_source, sa, ETH_ALEN);
            }
            proto = ntohs(ehdr.h_proto);
    }
    unifi_trace(priv, UDBG3, "in unifi_rx protocol from snap header = 0x%x\n", proto);

    if ((proto != ETH_P_PAE)
#ifdef CSR_WIFI_SECURITY_WAPI_ENABLE
            && (proto != ETH_P_WAI)
#endif
       ) {
        queue = UF_CONTROLLED_PORT_Q;
    } else {
        queue = UF_UNCONTROLLED_PORT_Q;
    }

    port_action = verify_port(priv, (unsigned char*)sa, queue, interfaceTag);
    unifi_trace(priv, UDBG3, "in unifi_rx port action is = 0x%x & queue = %x\n", port_action, queue);

#ifdef CSR_SUPPORT_SME
    /* Notify the TA module for the Rx frame for non P2PGO and AP cases*/
    if((interfacePriv->interfaceMode != CSR_WIFI_ROUTER_CTRL_MODE_AP) &&
            (interfacePriv->interfaceMode != CSR_WIFI_ROUTER_CTRL_MODE_P2PGO))
    {
        /* Remove MAC header of length(macHeaderLengthInBytes) before sampling */
        skb_pull(skb, macHeaderLengthInBytes);
        pData->os_data_ptr = skb->data;
        pData->data_length -= macHeaderLengthInBytes;

        if (pData->data_length) {
            unifi_ta_sample(priv->card, CSR_WIFI_ROUTER_CTRL_PROTOCOL_DIRECTION_RX,
                            &bulkdata->d[0],
                            sa, priv->netdev[interfaceTag]->dev_addr,
                            jiffies_to_msecs(jiffies),
                            pkt_ind->ReceivedRate);
        }
    } else {

        /* AP/P2PGO specific handling here */
        CsrWifiRouterCtrlStaInfo_t * srcStaInfo =
            CsrWifiRouterCtrlGetStationRecordFromPeerMacAddress(priv,sa,interfaceTag);

        /* Defensive check only; Source address is already checked in
        process_ma_packet_ind and we should have a valid source address here */

         if(srcStaInfo == NULL) {
            CsrWifiMacAddress peerMacAddress;
            /* Unknown data PDU */
            memcpy(peerMacAddress.a,sa,ETH_ALEN);
            unifi_trace(priv, UDBG1, "%s: Unexpected frame from peer = %x:%x:%x:%x:%x:%x\n", __FUNCTION__,
            sa[0], sa[1],sa[2], sa[3], sa[4],sa[5]);
            CsrWifiRouterCtrlUnexpectedFrameIndSend(priv->CSR_WIFI_SME_IFACEQUEUE,0,interfaceTag,peerMacAddress);
            unifi_net_data_free(priv, &bulkdata->d[0]);
            func_exit();
            return;
        }

       /* For AP GO mode, don't store the PDUs */
        if (port_action != CSR_WIFI_ROUTER_CTRL_PORT_ACTION_8021X_PORT_OPEN) {
            /* Drop the packet and return */
            CsrWifiMacAddress peerMacAddress;
            memcpy(peerMacAddress.a,sa,ETH_ALEN);
            unifi_trace(priv, UDBG3, "%s: Port is not open: unexpected frame from peer = %x:%x:%x:%x:%x:%x\n",
                        __FUNCTION__, sa[0], sa[1],sa[2], sa[3], sa[4],sa[5]);

            CsrWifiRouterCtrlUnexpectedFrameIndSend(priv->CSR_WIFI_SME_IFACEQUEUE,0,interfaceTag,peerMacAddress);
            interfacePriv->stats.rx_dropped++;
            unifi_net_data_free(priv, &bulkdata->d[0]);
            unifi_notice(priv, "%s: Dropping packet, proto=0x%04x, %s port\n", __FUNCTION__,
                         proto, queue ? "Controlled" : "Un-controlled");
            func_exit();
            return;
        }

         /* Qos NULL/Data NULL  are freed here and not processed further */
        if((dataFrameType == QOS_DATA_NULL) || (dataFrameType == DATA_NULL)){
            unifi_trace(priv, UDBG5, "%s: Null Frame Received and Freed\n", __FUNCTION__);
            unifi_net_data_free(priv, &bulkdata->d[0]);
            func_exit();
            return;
        }

        /* Now we have done with MAC header so proceed with the real data part*/
        /* This function takes care of appropriate routing for AP/P2PGO case*/
        /* the function hadnles following things
           2. Routing the PDU to appropriate location
           3. Error case handling
           */
        if(!(uf_ap_process_data_pdu(priv, skb, &ehdr, srcStaInfo,
             signal,
             bulkdata,
             macHeaderLengthInBytes)))
        {
            func_exit();
            return;
        }
        unifi_trace(priv, UDBG5, "unifi_rx: no specific AP handling process as normal frame, MAC Header len %d\n",macHeaderLengthInBytes);
        /* Remove the MAC header for subsequent conversion */
        skb_pull(skb, macHeaderLengthInBytes);
        pData->os_data_ptr = skb->data;
        pData->data_length -= macHeaderLengthInBytes;
        pData->os_net_buf_ptr = (unsigned char*)skb;
        pData->net_buf_length = skb->len;
    }
#endif /* CSR_SUPPORT_SME */


    /* Now that the MAC header is removed, null-data frames have zero length
     * and can be dropped
     */
    if (pData->data_length == 0) {
        if (((frameControl & 0x00f0) >> 4) != QOS_DATA_NULL &&
            ((frameControl & 0x00f0) >> 4) != DATA_NULL) {
            unifi_trace(priv, UDBG1, "Zero length frame, but not null-data %04x\n", frameControl);
        }
        unifi_net_data_free(priv, &bulkdata->d[0]);
        func_exit();
        return;
    }

    if (port_action == CSR_WIFI_ROUTER_CTRL_PORT_ACTION_8021X_PORT_CLOSED_DISCARD) {
        /* Drop the packet and return */
        interfacePriv->stats.rx_dropped++;
        unifi_net_data_free(priv, &bulkdata->d[0]);
        unifi_notice(priv, "%s: Dropping packet, proto=0x%04x, %s port\n",
                     __FUNCTION__, proto, queue ? "controlled" : "uncontrolled");
        func_exit();
        return;
    } else if ( (port_action == CSR_WIFI_ROUTER_CTRL_PORT_ACTION_8021X_PORT_CLOSED_BLOCK) ||
                   (interfacePriv->connected != UnifiConnected) ) {

        /* Buffer the packet into the Rx queues */
        rx_buffered_packets_t *rx_q_item;
        struct list_head *rx_list;

        rx_q_item = (rx_buffered_packets_t *)kmalloc(sizeof(rx_buffered_packets_t),
                GFP_KERNEL);
        if (rx_q_item == NULL) {
            unifi_error(priv, "%s: Failed to allocate %d bytes for rx packet record\n",
                        __FUNCTION__, sizeof(rx_buffered_packets_t));
            interfacePriv->stats.rx_dropped++;
            unifi_net_data_free(priv, &bulkdata->d[0]);
            func_exit();
            return;
        }

        INIT_LIST_HEAD(&rx_q_item->q);
        rx_q_item->bulkdata = *bulkdata;
        rx_q_item->skb = skb;
        rx_q_item->signal = *signal;
        memcpy(rx_q_item->sa.a, sa, ETH_ALEN);
        memcpy(rx_q_item->da.a, da, ETH_ALEN);
        unifi_trace(priv, UDBG2, "%s: Blocked skb=%p, bulkdata=%p\n",
                    __FUNCTION__, rx_q_item->skb, &rx_q_item->bulkdata);

        if (queue == UF_CONTROLLED_PORT_Q) {
            rx_list = &interfacePriv->rx_controlled_list;
        } else {
            rx_list = &interfacePriv->rx_uncontrolled_list;
        }

        /* Add to tail of packets queue */
        down(&priv->rx_q_sem);
        list_add_tail(&rx_q_item->q, rx_list);
        up(&priv->rx_q_sem);

        func_exit();
        return;

    }

    indicate_rx_skb(priv, interfaceTag, da, sa, skb, signal, bulkdata);

    func_exit();

} /* unifi_rx() */

static void process_ma_packet_cfm(unifi_priv_t *priv, CSR_SIGNAL *signal, bulk_data_param_t *bulkdata)
{
    u16 interfaceTag;
    const CSR_MA_PACKET_CONFIRM *pkt_cfm = &signal->u.MaPacketConfirm;
    netInterface_priv_t *interfacePriv;

    func_enter();
    interfaceTag = (pkt_cfm->VirtualInterfaceIdentifier & 0xff);
    interfacePriv = priv->interfacePriv[interfaceTag];

    /* Sanity check that the VIF refers to a sensible interface */
    if (interfaceTag >= CSR_WIFI_NUM_INTERFACES)
    {
        unifi_error(priv, "%s: MA-PACKET confirm with bad interfaceTag %d\n", __FUNCTION__, interfaceTag);
        func_exit();
        return;
    }
#ifdef CSR_SUPPORT_SME
    if(interfacePriv->interfaceMode == CSR_WIFI_ROUTER_CTRL_MODE_AP ||
       interfacePriv->interfaceMode == CSR_WIFI_ROUTER_CTRL_MODE_P2PGO) {

        uf_process_ma_pkt_cfm_for_ap(priv,interfaceTag,pkt_cfm);
    } else if (interfacePriv->m4_sent && (pkt_cfm->HostTag == interfacePriv->m4_hostTag)) {
        /* Check if this is a confirm for EAPOL M4 frame and we need to send transmistted ind*/
        CsrResult result = pkt_cfm->TransmissionStatus == CSR_TX_SUCCESSFUL?CSR_RESULT_SUCCESS:CSR_RESULT_FAILURE;
        CsrWifiMacAddress peerMacAddress;
        memcpy(peerMacAddress.a, interfacePriv->m4_signal.u.MaPacketRequest.Ra.x, ETH_ALEN);

        unifi_trace(priv, UDBG1, "%s: Sending M4 Transmit CFM\n", __FUNCTION__);
        CsrWifiRouterCtrlM4TransmittedIndSend(priv->CSR_WIFI_SME_IFACEQUEUE, 0,
                                              interfaceTag,
                                              peerMacAddress,
                                              result);
        interfacePriv->m4_sent = FALSE;
        interfacePriv->m4_hostTag = 0xffffffff;
    }
#endif
    func_exit();
    return;
}


/*
 * ---------------------------------------------------------------------------
 *  unifi_rx
 *
 *      Reformat a UniFi data received packet into a p80211 packet and
 *      pass it up the protocol stack.
 *
 *  Arguments:
 *      None.
 *
 *  Returns:
 *      None.
 * ---------------------------------------------------------------------------
 */
static void process_ma_packet_ind(unifi_priv_t *priv, CSR_SIGNAL *signal, bulk_data_param_t *bulkdata)
{
    u16 interfaceTag;
    bulk_data_desc_t *pData;
    CSR_MA_PACKET_INDICATION *pkt_ind = (CSR_MA_PACKET_INDICATION*)&signal->u.MaPacketIndication;
    struct sk_buff *skb;
    u16 frameControl;
    netInterface_priv_t *interfacePriv;
    u8 da[ETH_ALEN], sa[ETH_ALEN];
    u8 *bssid = NULL, *ba_addr = NULL;
    u8 toDs, fromDs, frameType;
    u8 i =0;

#ifdef CSR_SUPPORT_SME
    u8 dataFrameType = 0;
    u8 powerSaveChanged = FALSE;
    u8 pmBit = 0;
    CsrWifiRouterCtrlStaInfo_t *srcStaInfo = NULL;
    u16 qosControl;

#endif

    func_enter();

    interfaceTag = (pkt_ind->VirtualInterfaceIdentifier & 0xff);
    interfacePriv = priv->interfacePriv[interfaceTag];


    /* Sanity check that the VIF refers to a sensible interface */
    if (interfaceTag >= CSR_WIFI_NUM_INTERFACES)
    {
        unifi_error(priv, "%s: MA-PACKET indication with bad interfaceTag %d\n", __FUNCTION__, interfaceTag);
        unifi_net_data_free(priv,&bulkdata->d[0]);
        func_exit();
        return;
    }

    /* Sanity check that the VIF refers to an allocated netdev */
    if (!interfacePriv->netdev_registered)
    {
        unifi_error(priv, "%s: MA-PACKET indication with unallocated interfaceTag %d\n", __FUNCTION__, interfaceTag);
        unifi_net_data_free(priv, &bulkdata->d[0]);
        func_exit();
        return;
    }

    if (bulkdata->d[0].data_length == 0) {
        unifi_warning(priv, "%s: MA-PACKET indication with zero bulk data\n", __FUNCTION__);
        unifi_net_data_free(priv,&bulkdata->d[0]);
        func_exit();
        return;
    }
    /* For monitor mode we need to pass this indication to the registered application
    handle this seperately*/
    /* MIC failure is already taken care of so no need to send the PDUs which are not successfully received in non-monitor mode*/
    if(pkt_ind->ReceptionStatus != CSR_RX_SUCCESS)
    {
        unifi_warning(priv, "%s: MA-PACKET indication with status = %d\n",__FUNCTION__, pkt_ind->ReceptionStatus);
        unifi_net_data_free(priv,&bulkdata->d[0]);
        func_exit();
        return;
    }


    skb = (struct sk_buff*)bulkdata->d[0].os_net_buf_ptr;
    skb->len = bulkdata->d[0].data_length;

    /* Point to the addresses */
    toDs = (skb->data[1] & 0x01) ? 1 : 0;
    fromDs = (skb->data[1] & 0x02) ? 1 : 0;

    memcpy(da,(skb->data+4+toDs*12),ETH_ALEN);/* Address1 or 3 */
    memcpy(sa,(skb->data+10+fromDs*(6+toDs*8)),ETH_ALEN); /* Address2, 3 or 4 */

    /* Find the BSSID, which will be used to match the BA session */
    if (toDs && fromDs)
    {
        unifi_trace(priv, UDBG6, "4 address frame - don't try to find BSSID\n");
        bssid = NULL;
    }
    else
    {
        bssid = (u8 *) (skb->data + 4 + 12 - (fromDs * 6) - (toDs * 12));
    }

    pData = &bulkdata->d[0];
    frameControl = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(pData->os_data_ptr);
    frameType = ((frameControl & 0x000C) >> 2);

    unifi_trace(priv, UDBG3, "Rx Frame Type: %d sn: %d\n",frameType,
         (le16_to_cpu(*((u16*)(bulkdata->d[0].os_data_ptr + IEEE802_11_SEQUENCE_CONTROL_OFFSET))) >> 4) & 0xfff);
    if(frameType == IEEE802_11_FRAMETYPE_CONTROL){
#ifdef CSR_SUPPORT_SME
        unifi_trace(priv, UDBG6, "%s: Received Control Frame\n", __FUNCTION__);

        if((frameControl & 0x00f0) == 0x00A0){
            /* This is a PS-POLL request */
            u8 pmBit = (frameControl & 0x1000)?0x01:0x00;
            unifi_trace(priv, UDBG6, "%s: Received PS-POLL Frame\n", __FUNCTION__);

            uf_process_ps_poll(priv,sa,da,pmBit,interfaceTag);
        }
        else {
            unifi_warning(priv, "%s: Non PS-POLL control frame is received\n", __FUNCTION__);
        }
#endif
        unifi_net_data_free(priv,&bulkdata->d[0]);
        func_exit();
        return;
    }
    if(frameType != IEEE802_11_FRAMETYPE_DATA) {
        unifi_warning(priv, "%s: Non control Non Data frame is received\n",__FUNCTION__);
        unifi_net_data_free(priv,&bulkdata->d[0]);
        func_exit();
        return;
    }

#ifdef CSR_SUPPORT_SME
    if((interfacePriv->interfaceMode == CSR_WIFI_ROUTER_CTRL_MODE_AP) ||
       (interfacePriv->interfaceMode == CSR_WIFI_ROUTER_CTRL_MODE_P2PGO)){

        srcStaInfo = CsrWifiRouterCtrlGetStationRecordFromPeerMacAddress(priv,sa,interfaceTag);

        if(srcStaInfo == NULL) {
            CsrWifiMacAddress peerMacAddress;
            /* Unknown data PDU */
            memcpy(peerMacAddress.a,sa,ETH_ALEN);
            unifi_trace(priv, UDBG1, "%s: Unexpected frame from peer = %x:%x:%x:%x:%x:%x\n", __FUNCTION__,
            sa[0], sa[1],sa[2], sa[3], sa[4],sa[5]);
            CsrWifiRouterCtrlUnexpectedFrameIndSend(priv->CSR_WIFI_SME_IFACEQUEUE,0,interfaceTag,peerMacAddress);
            unifi_net_data_free(priv, &bulkdata->d[0]);
            func_exit();
            return;
        }

        /*
        verify power management bit here so as to ensure host and unifi are always
        in sync with power management status of peer.

        If we do it later, it may so happen we have stored the frame in BA re-ordering
        buffer and hence host and unifi are out of sync for power management status
        */

        pmBit = (frameControl & 0x1000)?0x01:0x00;
        powerSaveChanged = uf_process_pm_bit_for_peer(priv,srcStaInfo,pmBit,interfaceTag);

        /* Update station last activity time */
        srcStaInfo->activity_flag = TRUE;

        /* For Qos Frame if PM bit is toggled to indicate the change in power save state then it shall not be
        considered as Trigger Frame. Enter only if WMM STA and peer is in Power save */

        dataFrameType = ((frameControl & 0x00f0) >> 4);

        if((powerSaveChanged == FALSE)&&(srcStaInfo->wmmOrQosEnabled == TRUE)&&
        (srcStaInfo->currentPeerState == CSR_WIFI_ROUTER_CTRL_PEER_CONNECTED_POWER_SAVE)){

            if((dataFrameType == QOS_DATA) || (dataFrameType == QOS_DATA_NULL)){

                /*
                 * QoS control field is offset from frame control by 2 (frame control)
                 * + 2 (duration/ID) + 2 (sequence control) + 3*ETH_ALEN or 4*ETH_ALEN
                 */
                if((frameControl & IEEE802_11_FC_TO_DS_MASK) && (frameControl & IEEE802_11_FC_FROM_DS_MASK)){
                    qosControl= CSR_GET_UINT16_FROM_LITTLE_ENDIAN(pData->os_data_ptr + 30);
                }
                else{
                    qosControl = CSR_GET_UINT16_FROM_LITTLE_ENDIAN(pData->os_data_ptr + 24);
                }
                unifi_trace(priv, UDBG5, "%s: Check if U-APSD operations are triggered for qosControl: 0x%x\n",__FUNCTION__,qosControl);
                uf_process_wmm_deliver_ac_uapsd(priv,srcStaInfo,qosControl,interfaceTag);
            }
        }
    }

#endif

    if( ((frameControl & 0x00f0) >> 4) == QOS_DATA) {
        u8 *qos_control_ptr = (u8*)bulkdata->d[0].os_data_ptr + (((frameControl & IEEE802_11_FC_TO_DS_MASK) && (frameControl & IEEE802_11_FC_FROM_DS_MASK))?30: 24);
        int tID = *qos_control_ptr & IEEE802_11_QC_TID_MASK; /* using ls octet of qos control */
        ba_session_rx_struct *ba_session;
        u8 ba_session_idx = 0;
        /* Get the BA originator address */
        if(interfacePriv->interfaceMode == CSR_WIFI_ROUTER_CTRL_MODE_AP ||
           interfacePriv->interfaceMode == CSR_WIFI_ROUTER_CTRL_MODE_P2PGO){
            ba_addr = sa;
        }else{
            ba_addr = bssid;
        }

        down(&priv->ba_mutex);
        for (ba_session_idx=0; ba_session_idx < MAX_SUPPORTED_BA_SESSIONS_RX; ba_session_idx++){
            ba_session = interfacePriv->ba_session_rx[ba_session_idx];
            if (ba_session){
                unifi_trace(priv, UDBG6, "found ba_session=0x%x ba_session_idx=%d", ba_session, ba_session_idx);
                if ((!memcmp(ba_session->macAddress.a, ba_addr, ETH_ALEN)) && (ba_session->tID == tID)){
                        frame_desc_struct frame_desc;
                        frame_desc.bulkdata = *bulkdata;
                        frame_desc.signal = *signal;
                        frame_desc.sn = (le16_to_cpu(*((u16*)(bulkdata->d[0].os_data_ptr + IEEE802_11_SEQUENCE_CONTROL_OFFSET))) >> 4) & 0xfff;
                        frame_desc.active = TRUE;
                        unifi_trace(priv, UDBG6, "%s: calling process_ba_frame (session=%d)\n", __FUNCTION__, ba_session_idx);
                        process_ba_frame(priv, interfacePriv, ba_session, &frame_desc);
                        up(&priv->ba_mutex);
                        process_ba_complete(priv, interfacePriv);
                        break;
                }
            }
        }
        if (ba_session_idx == MAX_SUPPORTED_BA_SESSIONS_RX){
            up(&priv->ba_mutex);
            unifi_trace(priv, UDBG6, "%s: calling process_amsdu()", __FUNCTION__);
            process_amsdu(priv, signal, bulkdata);
        }
    } else {
        unifi_trace(priv, UDBG6, "calling unifi_rx()");
        unifi_rx(priv, signal, bulkdata);
    }

    /* check if the frames in reorder buffer has aged, the check
     * is done after receive processing so that if the missing frame
     * has arrived in this receive process, then it is handled cleanly.
     *
     * And also this code here takes care that timeout check is made for all
     * the receive indications
     */
    down(&priv->ba_mutex);
    for (i=0; i < MAX_SUPPORTED_BA_SESSIONS_RX; i++){
        ba_session_rx_struct *ba_session;
        ba_session = interfacePriv->ba_session_rx[i];
            if (ba_session){
                check_ba_frame_age_timeout(priv, interfacePriv, ba_session);
            }
    }
    up(&priv->ba_mutex);
    process_ba_complete(priv, interfacePriv);

    func_exit();
}
/*
 * ---------------------------------------------------------------------------
 *  uf_set_multicast_list
 *
 *      This function is called by the higher level stack to set
 *      a list of multicast rx addresses.
 *
 *  Arguments:
 *      dev             Network Device pointer.
 *
 *  Returns:
 *      None.
 *
 *  Notes:
 * ---------------------------------------------------------------------------
 */

static void
uf_set_multicast_list(struct net_device *dev)
{
    netInterface_priv_t *interfacePriv = (netInterface_priv_t *)netdev_priv(dev);
    unifi_priv_t *priv = interfacePriv->privPtr;

#ifdef CSR_NATIVE_LINUX
    unifi_trace(priv, UDBG3, "uf_set_multicast_list unsupported\n");
    return;
#else

    u8 *mc_list = interfacePriv->mc_list;
    struct netdev_hw_addr *mc_addr;
    int mc_addr_count;

    if (priv->init_progress != UNIFI_INIT_COMPLETED) {
        return;
    }

    mc_addr_count = netdev_mc_count(dev);

    unifi_trace(priv, UDBG3,
            "uf_set_multicast_list (count=%d)\n", mc_addr_count);


    /* Not enough space? */
    if (mc_addr_count > UNIFI_MAX_MULTICAST_ADDRESSES) {
        return;
    }

    /* Store the list to be processed by the work item. */
    interfacePriv->mc_list_count = mc_addr_count;
    netdev_hw_addr_list_for_each(mc_addr, &dev->mc) {
        memcpy(mc_list, mc_addr->addr, ETH_ALEN);
        mc_list += ETH_ALEN;
    }

    /* Send a message to the workqueue */
    queue_work(priv->unifi_workqueue, &priv->multicast_list_task);
#endif

} /* uf_set_multicast_list() */

/*
 * ---------------------------------------------------------------------------
 *  netdev_mlme_event_handler
 *
 *      Callback function to be used as the udi_event_callback when registering
 *      as a netdev client.
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
static void
netdev_mlme_event_handler(ul_client_t *pcli, const u8 *sig_packed, int sig_len,
                          const bulk_data_param_t *bulkdata_o, int dir)
{
    CSR_SIGNAL signal;
    unifi_priv_t *priv = uf_find_instance(pcli->instance);
    int id, r;
    bulk_data_param_t bulkdata;

    func_enter();

    /* Just a sanity check */
    if (sig_packed == NULL) {
        return;
    }

    /*
     * This copy is to silence a compiler warning about discarding the
     * const qualifier.
     */
    bulkdata = *bulkdata_o;

    /* Get the unpacked signal */
    r = read_unpack_signal(sig_packed, &signal);
    if (r) {
        /*
         * The CSR_MLME_CONNECTED_INDICATION_ID has a receiverID=0 so will
         * fall through this case. It is safe to ignore this signal.
         */
        unifi_trace(priv, UDBG1,
                    "Netdev - Received unknown signal 0x%.4X.\n",
                    CSR_GET_UINT16_FROM_LITTLE_ENDIAN(sig_packed));
        return;
    }

    id = signal.SignalPrimitiveHeader.SignalId;
    unifi_trace(priv, UDBG3, "Netdev - Process signal 0x%.4X\n", id);

    /*
     * Take the appropriate action for the signal.
     */
    switch (id) {
        case CSR_MA_PACKET_ERROR_INDICATION_ID:
            process_ma_packet_error_ind(priv, &signal, &bulkdata);
            break;
        case CSR_MA_PACKET_INDICATION_ID:
            process_ma_packet_ind(priv, &signal, &bulkdata);
            break;
        case  CSR_MA_PACKET_CONFIRM_ID:
            process_ma_packet_cfm(priv, &signal, &bulkdata);
            break;
#ifdef CSR_SUPPORT_SME
        case CSR_MLME_SET_TIM_CONFIRM_ID:
            /* Handle TIM confirms from FW & set the station record's TIM state appropriately,
             * In case of failures, tries with max_retransmit limit
             */
            uf_handle_tim_cfm(priv, &signal.u.MlmeSetTimConfirm, signal.SignalPrimitiveHeader.ReceiverProcessId);
            break;
#endif
        case CSR_DEBUG_STRING_INDICATION_ID:
            debug_string_indication(priv, bulkdata.d[0].os_data_ptr, bulkdata.d[0].data_length);
            break;

        case CSR_DEBUG_WORD16_INDICATION_ID:
            debug_word16_indication(priv, &signal);
            break;

        case CSR_DEBUG_GENERIC_CONFIRM_ID:
        case CSR_DEBUG_GENERIC_INDICATION_ID:
            debug_generic_indication(priv, &signal);
            break;
        default:
            break;
    }

    func_exit();
} /* netdev_mlme_event_handler() */


/*
 * ---------------------------------------------------------------------------
 *  uf_net_get_name
 *
 *      Retrieve the name (e.g. eth1) associated with this network device
 *
 *  Arguments:
 *      dev             Pointer to the network device.
 *      name            Buffer to write name
 *      len             Size of buffer in bytes
 *
 *  Returns:
 *      None
 *
 *  Notes:
 * ---------------------------------------------------------------------------
 */
void uf_net_get_name(struct net_device *dev, char *name, int len)
{
    *name = '\0';
    if (dev) {
        strlcpy(name, dev->name, (len > IFNAMSIZ) ? IFNAMSIZ : len);
    }

} /* uf_net_get_name */

#ifdef CSR_SUPPORT_WEXT

/*
 * ---------------------------------------------------------------------------
 *  uf_netdev_event
 *
 *     Callback function to handle netdev state changes
 *
 *  Arguments:
 *      notif           Pointer to a notifier_block.
 *      event           Event prompting notification
 *      ptr             net_device pointer
 *
 *  Returns:
 *      None
 *
 *  Notes:
 *   The event handler is global, and may occur on non-UniFi netdevs.
 * ---------------------------------------------------------------------------
 */
static int
uf_netdev_event(struct notifier_block *notif, unsigned long event, void* ptr) {
    struct net_device *netdev = ptr;
    netInterface_priv_t *interfacePriv = (netInterface_priv_t *)netdev_priv(netdev);
    unifi_priv_t *priv = NULL;
    static const CsrWifiMacAddress broadcast_address = {{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}};

    /* Check that the event is for a UniFi netdev. If it's not, the netdev_priv
     * structure is not safe to use.
     */
    if (uf_find_netdev_priv(interfacePriv) == -1) {
        unifi_trace(NULL, UDBG1, "uf_netdev_event: ignore e=%d, ptr=%p, priv=%p %s\n",
                    event, ptr, interfacePriv, netdev->name);
        return 0;
    }

    switch(event) {
    case NETDEV_CHANGE:
        priv = interfacePriv->privPtr;
        unifi_trace(priv, UDBG1, "NETDEV_CHANGE: %p %s %s waiting for it\n",
                    ptr,
                    netdev->name,
                    interfacePriv->wait_netdev_change ? "" : "not");

        if (interfacePriv->wait_netdev_change) {
            netif_tx_wake_all_queues(priv->netdev[interfacePriv->InterfaceTag]);
            interfacePriv->connected = UnifiConnected;
            interfacePriv->wait_netdev_change = FALSE;
            /* Note: passing the broadcast address here will allow anyone to attempt to join our adhoc network */
            uf_process_rx_pending_queue(priv, UF_UNCONTROLLED_PORT_Q, broadcast_address, 1,interfacePriv->InterfaceTag);
            uf_process_rx_pending_queue(priv, UF_CONTROLLED_PORT_Q, broadcast_address, 1,interfacePriv->InterfaceTag);
        }
        break;

    default:
        break;
    }
    return 0;
}

static struct notifier_block uf_netdev_notifier = {
    .notifier_call = uf_netdev_event,
};
#endif /* CSR_SUPPORT_WEXT */


static void
        process_amsdu(unifi_priv_t *priv, CSR_SIGNAL *signal, bulk_data_param_t *bulkdata)
{
    u32 offset;
    u32 length = bulkdata->d[0].data_length;
    u32 subframe_length, subframe_body_length, dot11_hdr_size;
    u8 *ptr;
    bulk_data_param_t subframe_bulkdata;
    u8 *dot11_hdr_ptr = (u8*)bulkdata->d[0].os_data_ptr;
    CsrResult csrResult;
    u16 frameControl;
    u8 *qos_control_ptr;

    frameControl = le16_to_cpu(*((u16*)dot11_hdr_ptr));
    qos_control_ptr = dot11_hdr_ptr + (((frameControl & IEEE802_11_FC_TO_DS_MASK) && (frameControl & IEEE802_11_FC_FROM_DS_MASK))?30: 24);
    if(!(*qos_control_ptr & IEEE802_11_QC_A_MSDU_PRESENT)) {
        unifi_trace(priv, UDBG6, "%s: calling unifi_rx()", __FUNCTION__);
        unifi_rx(priv, signal, bulkdata);
        return;
    }
    *qos_control_ptr &= ~(IEEE802_11_QC_A_MSDU_PRESENT);

    ptr = qos_control_ptr + 2;
    offset = dot11_hdr_size = ptr - dot11_hdr_ptr;

    while(length > (offset + sizeof(struct ethhdr) + sizeof(llc_snap_hdr_t))) {
        subframe_body_length = ntohs(((struct ethhdr*)ptr)->h_proto);
        if(subframe_body_length > IEEE802_11_MAX_DATA_LEN) {
            unifi_error(priv, "%s: bad subframe_body_length = %d\n", __FUNCTION__, subframe_body_length);
            break;
        }
        subframe_length = sizeof(struct ethhdr) + subframe_body_length;
        memset(&subframe_bulkdata, 0, sizeof(bulk_data_param_t));

        csrResult = unifi_net_data_malloc(priv, &subframe_bulkdata.d[0], dot11_hdr_size + subframe_body_length);

        if (csrResult != CSR_RESULT_SUCCESS) {
            unifi_error(priv, "%s: unifi_net_data_malloc failed\n", __FUNCTION__);
            break;
        }

        memcpy((u8*)subframe_bulkdata.d[0].os_data_ptr, dot11_hdr_ptr, dot11_hdr_size);


        /* When to DS=0 and from DS=0, address 3 will already have BSSID so no need to re-program */
        if ((frameControl & IEEE802_11_FC_TO_DS_MASK) && !(frameControl & IEEE802_11_FC_FROM_DS_MASK)){
                memcpy((u8*)subframe_bulkdata.d[0].os_data_ptr + IEEE802_11_ADDR3_OFFSET, ((struct ethhdr*)ptr)->h_dest, ETH_ALEN);
        }
        else if (!(frameControl & IEEE802_11_FC_TO_DS_MASK) && (frameControl & IEEE802_11_FC_FROM_DS_MASK)){
                memcpy((u8*)subframe_bulkdata.d[0].os_data_ptr + IEEE802_11_ADDR3_OFFSET,
                         ((struct ethhdr*)ptr)->h_source,
                           ETH_ALEN);
        }

        memcpy((u8*)subframe_bulkdata.d[0].os_data_ptr + dot11_hdr_size,
                ptr + sizeof(struct ethhdr),
                             subframe_body_length);
        unifi_trace(priv, UDBG6, "%s: calling unifi_rx. length = %d subframe_length = %d\n", __FUNCTION__, length, subframe_length);
        unifi_rx(priv, signal, &subframe_bulkdata);

        subframe_length = (subframe_length + 3)&(~0x3);
        ptr += subframe_length;
        offset += subframe_length;
    }
    unifi_net_data_free(priv, &bulkdata->d[0]);
}


#define SN_TO_INDEX(__ba_session, __sn) (((__sn - __ba_session->start_sn) & 0xFFF) % __ba_session->wind_size)


#define ADVANCE_EXPECTED_SN(__ba_session) \
{ \
    __ba_session->expected_sn++; \
    __ba_session->expected_sn &= 0xFFF; \
}

#define FREE_BUFFER_SLOT(__ba_session, __index) \
{ \
    __ba_session->occupied_slots--; \
    __ba_session->buffer[__index].active = FALSE; \
    ADVANCE_EXPECTED_SN(__ba_session); \
}

static void add_frame_to_ba_complete(unifi_priv_t *priv,
                          netInterface_priv_t *interfacePriv,
                          frame_desc_struct *frame_desc)
{
    interfacePriv->ba_complete[interfacePriv->ba_complete_index] = *frame_desc;
    interfacePriv->ba_complete_index++;
}


static void update_expected_sn(unifi_priv_t *priv,
                          netInterface_priv_t *interfacePriv,
                          ba_session_rx_struct *ba_session,
                          u16 sn)
{
    int i, j;
    u16 gap;

    gap = (sn - ba_session->expected_sn) & 0xFFF;
    unifi_trace(priv, UDBG6, "%s: proccess the frames up to new_expected_sn = %d gap = %d\n", __FUNCTION__, sn, gap);
    for(j = 0; j < gap && j < ba_session->wind_size; j++) {
        i = SN_TO_INDEX(ba_session, ba_session->expected_sn);
        unifi_trace(priv, UDBG6, "%s: proccess the slot index = %d\n", __FUNCTION__, i);
        if(ba_session->buffer[i].active) {
            add_frame_to_ba_complete(priv, interfacePriv, &ba_session->buffer[i]);
            unifi_trace(priv, UDBG6, "%s: proccess the frame at index = %d expected_sn = %d\n", __FUNCTION__, i, ba_session->expected_sn);
            FREE_BUFFER_SLOT(ba_session, i);
        } else {
            unifi_trace(priv, UDBG6, "%s: empty slot at index = %d\n", __FUNCTION__, i);
            ADVANCE_EXPECTED_SN(ba_session);
        }
    }
    ba_session->expected_sn = sn;
}


static void complete_ready_sequence(unifi_priv_t *priv,
                               netInterface_priv_t *interfacePriv,
                               ba_session_rx_struct *ba_session)
{
    int i;

    i = SN_TO_INDEX(ba_session, ba_session->expected_sn);
    while (ba_session->buffer[i].active) {
        add_frame_to_ba_complete(priv, interfacePriv, &ba_session->buffer[i]);
        unifi_trace(priv, UDBG6, "%s: completed stored frame(expected_sn=%d) at i = %d\n", __FUNCTION__, ba_session->expected_sn, i);
        FREE_BUFFER_SLOT(ba_session, i);
        i = SN_TO_INDEX(ba_session, ba_session->expected_sn);
    }
}


void scroll_ba_window(unifi_priv_t *priv,
                                netInterface_priv_t *interfacePriv,
                                ba_session_rx_struct *ba_session,
                                u16 sn)
{
    if(((sn - ba_session->expected_sn) & 0xFFF) <= 2048) {
        update_expected_sn(priv, interfacePriv, ba_session, sn);
        complete_ready_sequence(priv, interfacePriv, ba_session);
    }
}


static int consume_frame_or_get_buffer_index(unifi_priv_t *priv,
                                            netInterface_priv_t *interfacePriv,
                                            ba_session_rx_struct *ba_session,
                                            u16 sn,
                                            frame_desc_struct *frame_desc) {
    int i;
    u16 sn_temp;

    if(((sn - ba_session->expected_sn) & 0xFFF) <= 2048) {

        /* once we are in BA window, set the flag for BA trigger */
        if(!ba_session->trigger_ba_after_ssn){
            ba_session->trigger_ba_after_ssn = TRUE;
        }

        sn_temp = ba_session->expected_sn + ba_session->wind_size;
        unifi_trace(priv, UDBG6, "%s: new frame: sn=%d\n", __FUNCTION__, sn);
        if(!(((sn - sn_temp) & 0xFFF) > 2048)) {
            u16 new_expected_sn;
            unifi_trace(priv, UDBG6, "%s: frame is out of window\n", __FUNCTION__);
            sn_temp = (sn - ba_session->wind_size) & 0xFFF;
            new_expected_sn = (sn_temp + 1) & 0xFFF;
            update_expected_sn(priv, interfacePriv, ba_session, new_expected_sn);
        }
        i = -1;
        if (sn == ba_session->expected_sn) {
            unifi_trace(priv, UDBG6, "%s: sn = ba_session->expected_sn = %d\n", __FUNCTION__, sn);
            ADVANCE_EXPECTED_SN(ba_session);
            add_frame_to_ba_complete(priv, interfacePriv, frame_desc);
        } else {
            i = SN_TO_INDEX(ba_session, sn);
            unifi_trace(priv, UDBG6, "%s: sn(%d) != ba_session->expected_sn(%d), i = %d\n", __FUNCTION__, sn, ba_session->expected_sn, i);
            if (ba_session->buffer[i].active) {
                unifi_trace(priv, UDBG6, "%s: free frame at i = %d\n", __FUNCTION__, i);
                i = -1;
                unifi_net_data_free(priv, &frame_desc->bulkdata.d[0]);
            }
        }
    } else {
        i = -1;
        if(!ba_session->trigger_ba_after_ssn){
            unifi_trace(priv, UDBG6, "%s: frame before ssn, pass it up: sn=%d\n", __FUNCTION__, sn);
            add_frame_to_ba_complete(priv, interfacePriv, frame_desc);
        }else{
            unifi_trace(priv, UDBG6, "%s: old frame, drop: sn=%d, expected_sn=%d\n", __FUNCTION__, sn, ba_session->expected_sn);
            unifi_net_data_free(priv, &frame_desc->bulkdata.d[0]);
        }
    }
    return i;
}



static void process_ba_frame(unifi_priv_t *priv,
                                             netInterface_priv_t *interfacePriv,
                                             ba_session_rx_struct *ba_session,
                                             frame_desc_struct *frame_desc)
{
    int i;
    u16 sn = frame_desc->sn;

    if (ba_session->timeout) {
        mod_timer(&ba_session->timer, (jiffies + usecs_to_jiffies((ba_session->timeout) * 1024)));
    }
    unifi_trace(priv, UDBG6, "%s: got frame(sn=%d)\n", __FUNCTION__, sn);

    i = consume_frame_or_get_buffer_index(priv, interfacePriv, ba_session, sn, frame_desc);
    if(i >= 0) {
        unifi_trace(priv, UDBG6, "%s: store frame(sn=%d) at i = %d\n", __FUNCTION__, sn, i);
        ba_session->buffer[i] = *frame_desc;
        ba_session->buffer[i].recv_time = CsrTimeGet(NULL);
        ba_session->occupied_slots++;
    } else {
        unifi_trace(priv, UDBG6, "%s: frame consumed - sn = %d\n", __FUNCTION__, sn);
    }
    complete_ready_sequence(priv, interfacePriv, ba_session);
}


static void process_ba_complete(unifi_priv_t *priv, netInterface_priv_t *interfacePriv)
{
    frame_desc_struct *frame_desc;
    u8 i;

    for(i = 0; i < interfacePriv->ba_complete_index; i++) {
        frame_desc = &interfacePriv->ba_complete[i];
        unifi_trace(priv, UDBG6, "%s: calling process_amsdu()\n", __FUNCTION__);
        process_amsdu(priv, &frame_desc->signal, &frame_desc->bulkdata);
    }
    interfacePriv->ba_complete_index = 0;

}


/* Check if the frames in BA reoder buffer has aged and
 * if so release the frames to upper processes and move
 * the window
 */
static void check_ba_frame_age_timeout( unifi_priv_t *priv,
                                        netInterface_priv_t *interfacePriv,
                                        ba_session_rx_struct *ba_session)
{
    CsrTime now;
    CsrTime age;
    u8 i, j;
    u16 sn_temp;

    /* gap is started at 1 because we have buffered frames and
     * hence a minimum gap of 1 exists
     */
    u8 gap=1;

    now = CsrTimeGet(NULL);

    if (ba_session->occupied_slots)
    {
        /* expected sequence has not arrived so start searching from next
         * sequence number until a frame is available and determine the gap.
         * Check if the frame available has timedout, if so advance the
         * expected sequence number and release the frames
         */
        sn_temp = (ba_session->expected_sn + 1) & 0xFFF;

        for(j = 0; j < ba_session->wind_size; j++)
        {
            i = SN_TO_INDEX(ba_session, sn_temp);

            if(ba_session->buffer[i].active)
            {
                unifi_trace(priv, UDBG6, "check age at slot index = %d sn = %d recv_time = %u now = %u\n",
                                        i,
                                        ba_session->buffer[i].sn,
                                        ba_session->buffer[i].recv_time,
                                        now);

                if (ba_session->buffer[i].recv_time > now)
                {
                    /* timer wrap */
                    age = CsrTimeAdd((CsrTime)CsrTimeSub(CSR_SCHED_TIME_MAX, ba_session->buffer[i].recv_time), now);
                }
                else
                {
                    age = (CsrTime)CsrTimeSub(now, ba_session->buffer[i].recv_time);
                }

                if (age >= CSR_WIFI_BA_MPDU_FRAME_AGE_TIMEOUT)
                {
                    unifi_trace(priv, UDBG2, "release the frame at index = %d gap = %d expected_sn = %d sn = %d\n",
                                            i,
                                            gap,
                                            ba_session->expected_sn,
                                            ba_session->buffer[i].sn);

                    /* if it has timedout don't wait for missing frames, move the window */
                    while (gap--)
                    {
                        ADVANCE_EXPECTED_SN(ba_session);
                    }
                    add_frame_to_ba_complete(priv, interfacePriv, &ba_session->buffer[i]);
                    FREE_BUFFER_SLOT(ba_session, i);
                    complete_ready_sequence(priv, interfacePriv, ba_session);
                }
                break;

            }
            else
            {
                /* advance temp sequence number and frame gap */
                sn_temp = (sn_temp + 1) & 0xFFF;
                gap++;
            }
        }
    }
}


static void process_ma_packet_error_ind(unifi_priv_t *priv, CSR_SIGNAL *signal, bulk_data_param_t *bulkdata)
{
    u16 interfaceTag;
    const CSR_MA_PACKET_ERROR_INDICATION *pkt_err_ind = &signal->u.MaPacketErrorIndication;
    netInterface_priv_t *interfacePriv;
    ba_session_rx_struct *ba_session;
    u8 ba_session_idx = 0;
    CSR_PRIORITY        UserPriority;
    CSR_SEQUENCE_NUMBER sn;

    func_enter();

    interfaceTag = (pkt_err_ind->VirtualInterfaceIdentifier & 0xff);


    /* Sanity check that the VIF refers to a sensible interface */
    if (interfaceTag >= CSR_WIFI_NUM_INTERFACES)
    {
        unifi_error(priv, "%s: MaPacketErrorIndication indication with bad interfaceTag %d\n", __FUNCTION__, interfaceTag);
        func_exit();
        return;
    }

    interfacePriv = priv->interfacePriv[interfaceTag];
    UserPriority = pkt_err_ind->UserPriority;
    if(UserPriority > 15) {
        unifi_error(priv, "%s: MaPacketErrorIndication indication with bad UserPriority=%d\n", __FUNCTION__, UserPriority);
        func_exit();
    }
    sn = pkt_err_ind->SequenceNumber;

    down(&priv->ba_mutex);
    /* To find the right ba_session loop through the BA sessions, compare MAC address and tID */
    for (ba_session_idx=0; ba_session_idx < MAX_SUPPORTED_BA_SESSIONS_RX; ba_session_idx++){
        ba_session = interfacePriv->ba_session_rx[ba_session_idx];
        if (ba_session){
            if ((!memcmp(ba_session->macAddress.a, pkt_err_ind->PeerQstaAddress.x, ETH_ALEN)) && (ba_session->tID == UserPriority)){
                if (ba_session->timeout) {
                    mod_timer(&ba_session->timer, (jiffies + usecs_to_jiffies((ba_session->timeout) * 1024)));
                }
                scroll_ba_window(priv, interfacePriv, ba_session, sn);
                break;
            }
        }
    }

    up(&priv->ba_mutex);
    process_ba_complete(priv, interfacePriv);
    func_exit();
}


