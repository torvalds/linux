/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2011
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/

/*
 * ---------------------------------------------------------------------------
 *  FILE:     csr_wifi_hip_unifi_udi.h
 *
 *  PURPOSE:
 *      Declarations and definitions for the UniFi Debug Interface.
 *
 * ---------------------------------------------------------------------------
 */
#ifndef __CSR_WIFI_HIP_UNIFI_UDI_H__
#define __CSR_WIFI_HIP_UNIFI_UDI_H__

#include "csr_wifi_hip_unifi.h"
#include "csr_wifi_hip_signals.h"


/*
 * Support for tracing the wire protocol.
 */
enum udi_log_direction
{
    UDI_LOG_FROM_HOST   = 0x0000,
    UDI_LOG_TO_HOST     = 0x0001
};

typedef void (*udi_func_t)(void *ospriv, u8 *sigdata,
                           u32 signal_len,
                           const bulk_data_param_t *bulkdata,
                           enum udi_log_direction dir);

CsrResult unifi_set_udi_hook(card_t *card, udi_func_t udi_fn);
CsrResult unifi_remove_udi_hook(card_t *card, udi_func_t udi_fn);


/*
 * Function to print current status info to a string.
 * This is used in the linux /proc interface and might be useful
 * in other systems.
 */
s32 unifi_print_status(card_t *card, char *str, s32 *remain);

#define UNIFI_SNPRINTF_RET(buf_p, remain, written)                  \
    do {                                                            \
        if (written >= remain) {                                    \
            if (remain >= 2) {                                      \
                buf_p[remain - 2] = '\n';                           \
                buf_p[remain - 1] = 0;                              \
            }                                                       \
            buf_p += remain;                                        \
            remain = 0;                                             \
        } else if (written > 0) {                                   \
            buf_p += written;                                       \
            remain -= written;                                      \
        }                                                           \
    } while (0)

#endif /* __CSR_WIFI_HIP_UNIFI_UDI_H__ */
