/*
 * ---------------------------------------------------------------------------
 * FILE:     sdio_events.c
 *
 * PURPOSE:
 *      Process the events received by the SDIO glue layer.
 *      Optional part of the porting exercise.
 *
 * Copyright (C) 2009 by Cambridge Silicon Radio Ltd.
 *
 * Refer to LICENSE.txt included with this source code for details on
 * the license terms.
 *
 * ---------------------------------------------------------------------------
 */
#include "unifi_priv.h"


/*
 * Porting Notes:
 * There are two ways to support the suspend/resume system events in a driver.
 * In some operating systems these events are delivered to the OS driver
 * directly from the system. In this case, the OS driver needs to pass these
 * events to the API described in the CSR SDIO Abstration API document.
 * In Linux, and other embedded operating systems, the suspend/resume events
 * come from the SDIO driver. In this case, simply get these events in the
 * SDIO glue layer and notify the OS layer.
 *
 * In either case, typically, the events are processed by the SME.
 * Use the unifi_sys_suspend_ind() and unifi_sys_resume_ind() to pass
 * the events to the SME.
 */

/*
 * ---------------------------------------------------------------------------
 *  unifi_suspend
 *
 *      Handles a suspend request from the SDIO driver.
 *
 *  Arguments:
 *      ospriv          Pointer to OS driver context.
 *
 * ---------------------------------------------------------------------------
 */
void unifi_suspend(void *ospriv)
{
    unifi_priv_t *priv = ospriv;
    int interfaceTag=0;

    /* Stop network traffic. */
    /* need to stop all the netdevices*/
    for( interfaceTag=0;interfaceTag<CSR_WIFI_NUM_INTERFACES;interfaceTag++)
    {
        netInterface_priv_t *interfacePriv = priv->interfacePriv[interfaceTag];
        if (interfacePriv->netdev_registered == 1)
        {
            netif_carrier_off(priv->netdev[interfaceTag]);
            UF_NETIF_TX_STOP_ALL_QUEUES(priv->netdev[interfaceTag]);
        }
    }
    sme_sys_suspend(priv);
} /* unifi_suspend() */


/*
 * ---------------------------------------------------------------------------
 *  unifi_resume
 *
 *      Handles a resume request from the SDIO driver.
 *
 *  Arguments:
 *      ospriv          Pointer to OS driver context.
 *
 * ---------------------------------------------------------------------------
 */
void unifi_resume(void *ospriv)
{
    unifi_priv_t *priv = ospriv;
    int r;

    r = sme_sys_resume(priv);
    if (r) {
        unifi_error(priv, "Failed to resume UniFi\n");
    }

} /* unifi_resume() */

