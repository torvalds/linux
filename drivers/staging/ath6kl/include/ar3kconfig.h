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


typedef struct {
    A_UINT32                 Flags;           /* config flags */
    void                     *pHCIDev;        /* HCI bridge device     */
    HCI_TRANSPORT_PROPERTIES *pHCIProps;      /* HCI bridge props      */
    HIF_DEVICE               *pHIFDevice;     /* HIF layer device      */
    
    A_UINT32                 AR3KBaudRate;    /* AR3K operational baud rate */
    A_UINT16                 AR6KScale;       /* AR6K UART scale value */    
    A_UINT16                 AR6KStep;        /* AR6K UART step value  */
    struct hci_dev           *pBtStackHCIDev; /* BT Stack HCI dev */
    A_UINT32                 PwrMgmtEnabled;  /* TLPM enabled? */  
    A_UINT16                 IdleTimeout;     /* TLPM idle timeout */
    A_UINT16                 WakeupTimeout;   /* TLPM wakeup timeout */
    A_UINT8                  bdaddr[6];       /* Bluetooth device address */
} AR3K_CONFIG_INFO;
                                                                                        
A_STATUS AR3KConfigure(AR3K_CONFIG_INFO *pConfigInfo);

A_STATUS AR3KConfigureExit(void *config);

#ifdef __cplusplus
}
#endif

#endif /*AR3KCONFIG_H_*/
