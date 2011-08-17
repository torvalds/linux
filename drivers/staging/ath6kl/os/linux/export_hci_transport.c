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
// HCI bridge implementation
//
// Author(s): ="Atheros"
//==============================================================================
#include <a_config.h>
#include <athdefs.h>
#include "a_osapi.h"
#include "htc_api.h"
#include "a_drv.h"
#include "hif.h"
#include "common_drv.h"
#include "a_debug.h"
#include "hci_transport_api.h"

#include "AR6002/hw4.0/hw/apb_athr_wlan_map.h"
#include "AR6002/hw4.0/hw/uart_reg.h"
#include "AR6002/hw4.0/hw/rtc_wlan_reg.h"

HCI_TRANSPORT_HANDLE (*_HCI_TransportAttach)(void *HTCHandle, struct hci_transport_config_info *pInfo);
void (*_HCI_TransportDetach)(HCI_TRANSPORT_HANDLE HciTrans);
int    (*_HCI_TransportAddReceivePkts)(HCI_TRANSPORT_HANDLE HciTrans, struct htc_packet_queue *pQueue);
int    (*_HCI_TransportSendPkt)(HCI_TRANSPORT_HANDLE HciTrans, struct htc_packet *pPacket, bool Synchronous);
void        (*_HCI_TransportStop)(HCI_TRANSPORT_HANDLE HciTrans);
int    (*_HCI_TransportStart)(HCI_TRANSPORT_HANDLE HciTrans);
int    (*_HCI_TransportEnableDisableAsyncRecv)(HCI_TRANSPORT_HANDLE HciTrans, bool Enable);
int    (*_HCI_TransportRecvHCIEventSync)(HCI_TRANSPORT_HANDLE HciTrans,
                                          struct htc_packet           *pPacket,
                                          int                  MaxPollMS);
int    (*_HCI_TransportSetBaudRate)(HCI_TRANSPORT_HANDLE HciTrans, u32 Baud);
int    (*_HCI_TransportEnablePowerMgmt)(HCI_TRANSPORT_HANDLE HciTrans, bool Enable);

extern struct hci_transport_callbacks ar6kHciTransCallbacks;

int ar6000_register_hci_transport(struct hci_transport_callbacks *hciTransCallbacks)
{
    ar6kHciTransCallbacks = *hciTransCallbacks;

    _HCI_TransportAttach = HCI_TransportAttach;
    _HCI_TransportDetach = HCI_TransportDetach;
    _HCI_TransportAddReceivePkts = HCI_TransportAddReceivePkts;
    _HCI_TransportSendPkt = HCI_TransportSendPkt;
    _HCI_TransportStop = HCI_TransportStop;
    _HCI_TransportStart = HCI_TransportStart;
    _HCI_TransportEnableDisableAsyncRecv = HCI_TransportEnableDisableAsyncRecv;
    _HCI_TransportRecvHCIEventSync = HCI_TransportRecvHCIEventSync;
    _HCI_TransportSetBaudRate = HCI_TransportSetBaudRate;
    _HCI_TransportEnablePowerMgmt = HCI_TransportEnablePowerMgmt;

    return 0;
}

int
ar6000_get_hif_dev(struct hif_device *device, void *config)
{
    int status;

    status = HIFConfigureDevice(device,
                                HIF_DEVICE_GET_OS_DEVICE,
                                (struct hif_device_os_device_info *)config, 
                                sizeof(struct hif_device_os_device_info));
    return status;
}

int ar6000_set_uart_config(struct hif_device *hifDevice,
                                u32 scale,
                                u32 step)
{
    u32 regAddress;
    u32 regVal;
    int status;

    regAddress = WLAN_UART_BASE_ADDRESS | UART_CLKDIV_ADDRESS;
    regVal = ((u32)scale << 16) | step;
    /* change the HCI UART scale/step values through the diagnostic window */
    status = ar6000_WriteRegDiag(hifDevice, &regAddress, &regVal);                     

    return status;
}

int ar6000_get_core_clock_config(struct hif_device *hifDevice, u32 *data)
{
    u32 regAddress;
    int status;

    regAddress = WLAN_RTC_BASE_ADDRESS | WLAN_CPU_CLOCK_ADDRESS;
    /* read CPU clock settings*/
    status = ar6000_ReadRegDiag(hifDevice, &regAddress, data);

    return status;
}

EXPORT_SYMBOL(ar6000_register_hci_transport);
EXPORT_SYMBOL(ar6000_get_hif_dev);
EXPORT_SYMBOL(ar6000_set_uart_config);
EXPORT_SYMBOL(ar6000_get_core_clock_config);
EXPORT_SYMBOL(_HCI_TransportAttach);
EXPORT_SYMBOL(_HCI_TransportDetach);
EXPORT_SYMBOL(_HCI_TransportAddReceivePkts);
EXPORT_SYMBOL(_HCI_TransportSendPkt);
EXPORT_SYMBOL(_HCI_TransportStop);
EXPORT_SYMBOL(_HCI_TransportStart);
EXPORT_SYMBOL(_HCI_TransportEnableDisableAsyncRecv);
EXPORT_SYMBOL(_HCI_TransportRecvHCIEventSync);
EXPORT_SYMBOL(_HCI_TransportSetBaudRate);
EXPORT_SYMBOL(_HCI_TransportEnablePowerMgmt);
