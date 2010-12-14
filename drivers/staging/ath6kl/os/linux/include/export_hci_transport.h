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

#include "hci_transport_api.h"
#include "common_drv.h"

extern HCI_TRANSPORT_HANDLE (*_HCI_TransportAttach)(void *HTCHandle, HCI_TRANSPORT_CONFIG_INFO *pInfo);
extern void (*_HCI_TransportDetach)(HCI_TRANSPORT_HANDLE HciTrans);
extern A_STATUS    (*_HCI_TransportAddReceivePkts)(HCI_TRANSPORT_HANDLE HciTrans, HTC_PACKET_QUEUE *pQueue);
extern A_STATUS    (*_HCI_TransportSendPkt)(HCI_TRANSPORT_HANDLE HciTrans, HTC_PACKET *pPacket, A_BOOL Synchronous);
extern void        (*_HCI_TransportStop)(HCI_TRANSPORT_HANDLE HciTrans);
extern A_STATUS    (*_HCI_TransportStart)(HCI_TRANSPORT_HANDLE HciTrans);
extern A_STATUS    (*_HCI_TransportEnableDisableAsyncRecv)(HCI_TRANSPORT_HANDLE HciTrans, A_BOOL Enable);
extern A_STATUS    (*_HCI_TransportRecvHCIEventSync)(HCI_TRANSPORT_HANDLE HciTrans, 
                                          HTC_PACKET           *pPacket,
                                          int                  MaxPollMS);
extern A_STATUS    (*_HCI_TransportSetBaudRate)(HCI_TRANSPORT_HANDLE HciTrans, A_UINT32 Baud);
extern A_STATUS    (*_HCI_TransportEnablePowerMgmt)(HCI_TRANSPORT_HANDLE HciTrans, A_BOOL Enable);


#define HCI_TransportAttach(HTCHandle, pInfo)   \
            _HCI_TransportAttach((HTCHandle), (pInfo))
#define HCI_TransportDetach(HciTrans)    \
            _HCI_TransportDetach(HciTrans)
#define HCI_TransportAddReceivePkts(HciTrans, pQueue)   \
            _HCI_TransportAddReceivePkts((HciTrans), (pQueue))
#define HCI_TransportSendPkt(HciTrans, pPacket, Synchronous)  \
            _HCI_TransportSendPkt((HciTrans), (pPacket), (Synchronous))
#define HCI_TransportStop(HciTrans)  \
            _HCI_TransportStop((HciTrans))
#define HCI_TransportStart(HciTrans)  \
            _HCI_TransportStart((HciTrans))
#define HCI_TransportEnableDisableAsyncRecv(HciTrans, Enable)   \
            _HCI_TransportEnableDisableAsyncRecv((HciTrans), (Enable))
#define HCI_TransportRecvHCIEventSync(HciTrans, pPacket, MaxPollMS)   \
            _HCI_TransportRecvHCIEventSync((HciTrans), (pPacket), (MaxPollMS))
#define HCI_TransportSetBaudRate(HciTrans, Baud)    \
            _HCI_TransportSetBaudRate((HciTrans), (Baud))
#define HCI_TransportEnablePowerMgmt(HciTrans, Enable)    \
            _HCI_TransportEnablePowerMgmt((HciTrans), (Enable))


extern A_STATUS ar6000_register_hci_transport(HCI_TRANSPORT_CALLBACKS *hciTransCallbacks);

extern A_STATUS ar6000_get_hif_dev(HIF_DEVICE *device, void *config);

extern A_STATUS ar6000_set_uart_config(HIF_DEVICE *hifDevice, A_UINT32 scale, A_UINT32 step);

/* get core clock register settings
 * data: 0 - 40/44MHz
 *       1 - 80/88MHz
 *       where (5G band/2.4G band)
 * assume 2.4G band for now
 */
extern A_STATUS ar6000_get_core_clock_config(HIF_DEVICE *hifDevice, A_UINT32 *data);
