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

    /* For powered suspend, tell the resume's wifi_on() not to reinit UniFi */
    priv->wol_suspend = (enable_wol == UNIFI_WOL_OFF) ? FALSE : TRUE;

    unifi_trace(priv, UDBG1, "unifi_suspend: wol_suspend %d, enable_wol %d",
                priv->wol_suspend, enable_wol );

    /* Stop network traffic. */
    /* need to stop all the netdevices*/
    for( interfaceTag=0;interfaceTag<CSR_WIFI_NUM_INTERFACES;interfaceTag++)
    {
        netInterface_priv_t *interfacePriv = priv->interfacePriv[interfaceTag];
        if (interfacePriv->netdev_registered == 1)
        {
            if( priv->wol_suspend ) {
                unifi_trace(priv, UDBG1, "unifi_suspend: Don't netif_carrier_off");
            } else {
                unifi_trace(priv, UDBG1, "unifi_suspend: netif_carrier_off");
                netif_carrier_off(priv->netdev[interfaceTag]);
            }
            netif_tx_stop_all_queues(priv->netdev[interfaceTag]);
        }
    }

    unifi_trace(priv, UDBG1, "unifi_suspend: suspend SME");

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
    int interfaceTag=0;
    int r;
    int wol = priv->wol_suspend;

    unifi_trace(priv, UDBG1, "unifi_resume: resume SME, enable_wol=%d", enable_wol);

    /* The resume causes wifi-on which will re-enable the BH and reinstall the ISR */
    r = sme_sys_resume(priv);
    if (r) {
        unifi_error(priv, "Failed to resume UniFi\n");
    }

    /* Resume the network interfaces. For the cold resume case, this will
     * happen upon reconnection.
     */
    if (wol) {
        unifi_trace(priv, UDBG1, "unifi_resume: try to enable carrier");

        /* need to start all the netdevices*/
        for( interfaceTag=0;interfaceTag<CSR_WIFI_NUM_INTERFACES;interfaceTag++) {
            netInterface_priv_t *interfacePriv = priv->interfacePriv[interfaceTag];

            unifi_trace(priv, UDBG1, "unifi_resume: interfaceTag %d netdev_registered %d mode %d\n",
                   interfaceTag, interfacePriv->netdev_registered, interfacePriv->interfaceMode);

            if (interfacePriv->netdev_registered == 1)
            {
                netif_carrier_on(priv->netdev[interfaceTag]);
                netif_tx_start_all_queues(priv->netdev[interfaceTag]);
            }
        }

        /* Kick the BH thread (with reason=host) to poll for data that may have
         * arrived during a powered suspend. This caters for the case where the SME
         * doesn't interact with the chip (e.g install autonomous scans) during resume.
         */
        unifi_send_signal(priv->card, NULL, 0, NULL);
    }

} /* unifi_resume() */

