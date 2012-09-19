/*
 * ---------------------------------------------------------------------------
 *  FILE:     inet.c
 *
 *  PURPOSE:
 *      Routines related to IP address changes.
 *      Optional part of the porting exercise. It uses system network
 *      handlers to obtain the UniFi IP address and pass it to the SME
 *      using the unifi_sys_ip_configured_ind().
 *
 * Copyright (C) 2008-2009 Cambridge Silicon Radio Ltd.
 *
 * Refer to LICENSE.txt included with this source code for details on
 * the license terms.
 *
 * ---------------------------------------------------------------------------
 */
#include <linux/inetdevice.h>
#include <linux/notifier.h>

#include "unifi_priv.h"
#include "csr_wifi_hip_conversions.h"

/*
 * The inet notifier is global and not per-netdev.  To avoid having a
 * notifier registered when there are no unifi devices present, it's
 * registered after the first unifi network device is registered, and
 * unregistered when the last unifi network device is unregistered.
 */

static atomic_t inet_notif_refs = ATOMIC_INIT(0);

static int uf_inetaddr_event(struct notifier_block *notif, unsigned long event, void *ifa)
{
    struct net_device *ndev;
    unifi_priv_t *priv;
    struct in_ifaddr *if_addr;
    netInterface_priv_t *InterfacePriv = (netInterface_priv_t *)NULL;

    if (!ifa || !((struct in_ifaddr *)ifa)->ifa_dev) {
        unifi_trace(NULL, UDBG1, "uf_inetaddr_event (%lu) ifa=%p\n", event, ifa);
        return NOTIFY_DONE;
    }

    ndev = ((struct in_ifaddr *)ifa)->ifa_dev->dev;
    InterfacePriv = (netInterface_priv_t*) netdev_priv(ndev);

    /* As the notifier is global, the call may be for a non-UniFi netdev.
     * Therefore check the netdev_priv to make sure it's a known UniFi one.
     */
    if (uf_find_netdev_priv(InterfacePriv) == -1) {
        unifi_trace(NULL, UDBG1, "uf_inetaddr_event (%lu) ndev=%p, other netdev_priv=%p\n",
                    event, ndev, InterfacePriv);
        return NOTIFY_DONE;
    }

    if (!InterfacePriv->privPtr) {
        unifi_error(NULL, "uf_inetaddr_event null priv (%lu) ndev=%p, InterfacePriv=%p\n",
                    event, ndev, InterfacePriv);
        return NOTIFY_DONE;
    }

    priv = InterfacePriv->privPtr;
    if_addr = (struct in_ifaddr *)ifa;

    /* If this event is for a UniFi device, notify the SME that an IP
     * address has been added or removed. */
    if (uf_find_priv(priv) != -1) {
        switch (event) {
            case NETDEV_UP:
                unifi_info(priv, "IP address assigned for %s\n", priv->netdev[InterfacePriv->InterfaceTag]->name);
                priv->sta_ip_address = if_addr->ifa_address;
#ifdef CSR_SUPPORT_WEXT
                sme_mgt_packet_filter_set(priv);
#endif
                break;
            case NETDEV_DOWN:
                unifi_info(priv, "IP address removed for %s\n", priv->netdev[InterfacePriv->InterfaceTag]->name);
                priv->sta_ip_address = 0xFFFFFFFF;
#ifdef CSR_SUPPORT_WEXT
                sme_mgt_packet_filter_set(priv);
#endif
                break;
        }
    }

    return NOTIFY_DONE;
}

static struct notifier_block uf_inetaddr_notifier = {
    .notifier_call = uf_inetaddr_event,
};

void uf_register_inet_notifier(void)
{
    if (atomic_inc_return(&inet_notif_refs) == 1) {
        register_inetaddr_notifier(&uf_inetaddr_notifier);
    }
}

void uf_unregister_inet_notifier(void)
{
    if (atomic_dec_return(&inet_notif_refs) == 0) {
        unregister_inetaddr_notifier(&uf_inetaddr_notifier);
    }
}
