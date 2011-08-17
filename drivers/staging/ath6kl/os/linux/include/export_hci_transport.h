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

extern HCI_TRANSPORT_HANDLE (*_HCI_TransportAttach)(void *HTCHandle, struct hci_transport_config_info *pInfo);
extern void (*_HCI_TransportDetach)(HCI_TRANSPORT_HANDLE HciTrans);
extern int    (*_HCI_TransportAddReceivePkts)(HCI_TRANSPORT_HANDLE HciTrans, struct htc_packet_queue *pQueue);
extern int    (*_HCI_TransportSendPkt)(HCI_TRANSPORT_HANDLE HciTrans, struct htc_packet *pPacket, bool Synchronous);
extern void        (*_HCI_TransportStop)(HCI_TRANSPORT_HANDLE HciTrans);
extern int    (*_HCI_TransportStart)(HCI_TRANSPORT_HANDLE HciTrans);
extern int    (*_HCI_TransportEnableDisableAsyncRecv)(HCI_TRANSPORT_HANDLE HciTrans, bool Enable);
extern int    (*_HCI_TransportRecvHCIEventSync)(HCI_TRANSPORT_HANDLE HciTrans,
                                          struct htc_packet           *pPacket,
                                          int                  MaxPollMS);
extern int    (*_HCI_TransportSetBaudRate)(HCI_TRANSPORT_HANDLE HciTrans, u32 Baud);
extern int    (*_HCI_TransportEnablePowerMgmt)(HCI_TRANSPORT_HANDLE HciTrans, bool Enable);


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


extern int ar6000_register_hci_transport(struct hci_transport_callbacks *hciTransCallbacks);

extern int ar6000_get_hif_dev(struct hif_device *device, void *config);

extern int ar6000_set_uart_config(struct hif_device *hifDevice, u32 scale, u32 step);

/* get core clock register settings
 * data: 0 - 40/44MHz
 *       1 - 80/88MHz
 *       where (5G band/2.4G band)
 * assume 2.4G band for now
 */
extern int ar6000_get_core_clock_config(struct hif_device *hifDevice, u32 *data);
