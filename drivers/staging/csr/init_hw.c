/*
 * ---------------------------------------------------------------------------
 * FILE:     init_hw.c
 *
 * PURPOSE:
 *      Use the HIP core lib to initialise the UniFi chip.
 *      It is part of the porting exercise in Linux.
 *
 * Copyright (C) 2009 by Cambridge Silicon Radio Ltd.
 *
 * Refer to LICENSE.txt included with this source code for details on
 * the license terms.
 *
 * ---------------------------------------------------------------------------
 */
#include "csr_wifi_hip_unifi.h"
#include "unifi_priv.h"


#define MAX_INIT_ATTEMPTS        4

extern int led_mask;


/*
 * ---------------------------------------------------------------------------
 *  uf_init_hw
 *
 *      Resets hardware, downloads and initialises f/w.
 *      This function demonstrates how to use the HIP core lib API
 *      to implement the SME unifi_sys_wifi_on_req() part of the SYS API.
 *
 *      In a simple implementation, all this function needs to do is call
 *      unifi_init_card() and then unifi_card_info().
 *      In the Linux implementation, it will retry to initialise UniFi or
 *      try to debug the reasons if unifi_init_card() returns an error.
 *
 *  Arguments:
 *      ospriv          Pointer to OS driver structure for the device.
 *
 *  Returns:
 *      O on success, non-zero otherwise.
 *
 * ---------------------------------------------------------------------------
 */
int
uf_init_hw(unifi_priv_t *priv)
{
    int attempts = 0;
    int priv_instance;
    CsrResult csrResult = CSR_RESULT_FAILURE;

    priv_instance = uf_find_priv(priv);
    if (priv_instance == -1) {
        unifi_warning(priv, "uf_init_hw: Unknown priv instance, will use fw_init[0]\n");
        priv_instance = 0;
    }

    while (1) {
        if (attempts > MAX_INIT_ATTEMPTS) {
            unifi_error(priv, "Failed to initialise UniFi after %d attempts, "
                        "giving up.\n",
                        attempts);
            break;
        }
        attempts++;

        unifi_info(priv, "Initialising UniFi, attempt %d\n", attempts);

        if (fw_init[priv_instance] > 0) {
            unifi_notice(priv, "f/w init prevented by module parameter\n");
            break;
        } else if (fw_init[priv_instance] == 0) {
            fw_init[priv_instance] ++;
        }

        /*
         * Initialise driver core. This will perform a reset of UniFi
         * internals, but not the SDIO CCCR.
         */
        CsrSdioClaim(priv->sdio);
        csrResult = unifi_init_card(priv->card, led_mask);
        CsrSdioRelease(priv->sdio);

        if (csrResult == CSR_WIFI_HIP_RESULT_NO_DEVICE) {
            return CsrHipResultToStatus(csrResult);
        }
        if (csrResult == CSR_WIFI_HIP_RESULT_NOT_FOUND) {
            unifi_error(priv, "Firmware file required, but not found.\n");
            return CsrHipResultToStatus(csrResult);
        }
        if (csrResult != CSR_RESULT_SUCCESS) {
            /* failed. Reset h/w and try again */
            unifi_error(priv, "Failed to initialise UniFi chip.\n");
            continue;
        }

        /* Get the version information from the lib_hip */
        unifi_card_info(priv->card, &priv->card_info);

        return CsrHipResultToStatus(csrResult);
    }

    return CsrHipResultToStatus(csrResult);

} /* uf_init_hw */


