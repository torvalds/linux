//------------------------------------------------------------------------------
// Copyright (c) 2009-2010 Atheros Corporation.  All rights reserved.
// 
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
//
//------------------------------------------------------------------------------
//==============================================================================
// Author(s): ="Atheros"
//==============================================================================

/* AR3K module configuration APIs for HCI-bridge operation */

#ifndef AR3KCONFIG_H_
#define AR3KCONFIG_H_

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AR3K_CONFIG_FLAG_FORCE_MINBOOT_EXIT         (1 << 0)
#define AR3K_CONFIG_FLAG_SET_AR3K_BAUD              (1 << 1)
#define AR3K_CONFIG_FLAG_AR3K_BAUD_CHANGE_DELAY     (1 << 2)
#define AR3K_CONFIG_FLAG_SET_AR6K_SCALE_STEP        (1 << 3)


struct ar3k_config_info {
    u32 Flags;           /* config flags */
    void                     *pHCIDev;        /* HCI bridge device     */
    struct hci_transport_properties *pHCIProps;      /* HCI bridge props      */
    struct hif_device               *pHIFDevice;     /* HIF layer device      */
    
    u32 AR3KBaudRate;    /* AR3K operational baud rate */
    u16 AR6KScale;       /* AR6K UART scale value */
    u16 AR6KStep;        /* AR6K UART step value  */
    struct hci_dev           *pBtStackHCIDev; /* BT Stack HCI dev */
    u32 PwrMgmtEnabled;  /* TLPM enabled? */
    u16 IdleTimeout;     /* TLPM idle timeout */
    u16 WakeupTimeout;   /* TLPM wakeup timeout */
    u8 bdaddr[6];       /* Bluetooth device address */
};
                                                                                        
int AR3KConfigure(struct ar3k_config_info *pConfigInfo);

int AR3KConfigureExit(void *config);

#ifdef __cplusplus
}
#endif

#endif /*AR3KCONFIG_H_*/
