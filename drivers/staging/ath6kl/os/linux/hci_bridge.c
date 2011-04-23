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

#ifdef EXPORT_HCI_BRIDGE_INTERFACE
#include <linux/etherdevice.h>
#include <a_config.h>
#include <athdefs.h>
#include "a_types.h"
#include "a_osapi.h"
#include "htc_api.h"
#include "wmi.h"
#include "a_drv.h"
#include "hif.h"
#include "common_drv.h"
#include "a_debug.h"
#define  ATH_DEBUG_HCI_BRIDGE    ATH_DEBUG_MAKE_MODULE_MASK(6)
#define  ATH_DEBUG_HCI_RECV      ATH_DEBUG_MAKE_MODULE_MASK(7)
#define  ATH_DEBUG_HCI_SEND      ATH_DEBUG_MAKE_MODULE_MASK(8)
#define  ATH_DEBUG_HCI_DUMP      ATH_DEBUG_MAKE_MODULE_MASK(9)
#else
#include "ar6000_drv.h"
#endif  /* EXPORT_HCI_BRIDGE_INTERFACE */

#ifdef ATH_AR6K_ENABLE_GMBOX
#ifdef EXPORT_HCI_BRIDGE_INTERFACE
#include "export_hci_transport.h"
#else
#include "hci_transport_api.h"
#endif
#include "epping_test.h"
#include "gmboxif.h"
#include "ar3kconfig.h"
#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>

    /* only build on newer kernels which have BT configured */
#if defined(CONFIG_BT_MODULE) || defined(CONFIG_BT)
#define CONFIG_BLUEZ_HCI_BRIDGE  
#endif

#ifdef EXPORT_HCI_BRIDGE_INTERFACE
unsigned int ar3khcibaud = 0;
unsigned int hciuartscale = 0;
unsigned int hciuartstep = 0;

module_param(ar3khcibaud, int, 0644);
module_param(hciuartscale, int, 0644);
module_param(hciuartstep, int, 0644);
#else
extern unsigned int ar3khcibaud;
extern unsigned int hciuartscale;
extern unsigned int hciuartstep;
#endif /* EXPORT_HCI_BRIDGE_INTERFACE */

struct ar6k_hci_bridge_info {
    void                    *pHCIDev;          /* HCI bridge device */
    struct hci_transport_properties HCIProps;         /* HCI bridge props */
    struct hci_dev          *pBtStackHCIDev;   /* BT Stack HCI dev */
    bool                  HciNormalMode;     /* Actual HCI mode enabled (non-TEST)*/
    bool                  HciRegistered;     /* HCI device registered with stack */
    struct htc_packet_queue        HTCPacketStructHead;
    u8 *pHTCStructAlloc;
    spinlock_t              BridgeLock;
#ifdef EXPORT_HCI_BRIDGE_INTERFACE
    struct hci_transport_misc_handles    HCITransHdl; 
#else
    struct ar6_softc              *ar;
#endif /* EXPORT_HCI_BRIDGE_INTERFACE */
};

#define MAX_ACL_RECV_BUFS           16
#define MAX_EVT_RECV_BUFS           8
#define MAX_HCI_WRITE_QUEUE_DEPTH   32
#define MAX_ACL_RECV_LENGTH         1200
#define MAX_EVT_RECV_LENGTH         257
#define TX_PACKET_RSV_OFFSET        32
#define NUM_HTC_PACKET_STRUCTS     ((MAX_ACL_RECV_BUFS + MAX_EVT_RECV_BUFS + MAX_HCI_WRITE_QUEUE_DEPTH) * 2)

#define HCI_GET_OP_CODE(p)          (((u16)((p)[1])) << 8) | ((u16)((p)[0]))

extern unsigned int setupbtdev;
struct ar3k_config_info      ar3kconfig;

#ifdef EXPORT_HCI_BRIDGE_INTERFACE
struct ar6k_hci_bridge_info *g_pHcidevInfo;
#endif

static int bt_setup_hci(struct ar6k_hci_bridge_info *pHcidevInfo);
static void     bt_cleanup_hci(struct ar6k_hci_bridge_info *pHcidevInfo);
static int bt_register_hci(struct ar6k_hci_bridge_info *pHcidevInfo);
static bool   bt_indicate_recv(struct ar6k_hci_bridge_info      *pHcidevInfo,
                                 HCI_TRANSPORT_PACKET_TYPE Type, 
                                 struct sk_buff            *skb);
static struct sk_buff *bt_alloc_buffer(struct ar6k_hci_bridge_info *pHcidevInfo, int Length);
static void     bt_free_buffer(struct ar6k_hci_bridge_info *pHcidevInfo, struct sk_buff *skb);   
                               
#ifdef EXPORT_HCI_BRIDGE_INTERFACE
int ar6000_setup_hci(void *ar);
void     ar6000_cleanup_hci(void *ar);
int hci_test_send(void *ar, struct sk_buff *skb);
#else
int ar6000_setup_hci(struct ar6_softc *ar);
void     ar6000_cleanup_hci(struct ar6_softc *ar);
/* HCI bridge testing */
int hci_test_send(struct ar6_softc *ar, struct sk_buff *skb);
#endif /* EXPORT_HCI_BRIDGE_INTERFACE */

#define LOCK_BRIDGE(dev)   spin_lock_bh(&(dev)->BridgeLock)
#define UNLOCK_BRIDGE(dev) spin_unlock_bh(&(dev)->BridgeLock)

static inline void FreeBtOsBuf(struct ar6k_hci_bridge_info *pHcidevInfo, void *osbuf)
{    
    if (pHcidevInfo->HciNormalMode) {
        bt_free_buffer(pHcidevInfo, (struct sk_buff *)osbuf);
    } else {
            /* in test mode, these are just ordinary netbuf allocations */
        A_NETBUF_FREE(osbuf);
    }
}

static void FreeHTCStruct(struct ar6k_hci_bridge_info *pHcidevInfo, struct htc_packet *pPacket)
{
    LOCK_BRIDGE(pHcidevInfo);
    HTC_PACKET_ENQUEUE(&pHcidevInfo->HTCPacketStructHead,pPacket);
    UNLOCK_BRIDGE(pHcidevInfo);  
}

static struct htc_packet * AllocHTCStruct(struct ar6k_hci_bridge_info *pHcidevInfo)
{
    struct htc_packet  *pPacket = NULL;
    LOCK_BRIDGE(pHcidevInfo);
    pPacket = HTC_PACKET_DEQUEUE(&pHcidevInfo->HTCPacketStructHead);
    UNLOCK_BRIDGE(pHcidevInfo);  
    return pPacket;
}

#define BLOCK_ROUND_UP_PWR2(x, align)    (((int) (x) + ((align)-1)) & ~((align)-1))

static void RefillRecvBuffers(struct ar6k_hci_bridge_info      *pHcidevInfo, 
                              HCI_TRANSPORT_PACKET_TYPE Type, 
                              int                       NumBuffers)
{
    int                 length, i;
    void                *osBuf = NULL;
    struct htc_packet_queue    queue;
    struct htc_packet          *pPacket;

    INIT_HTC_PACKET_QUEUE(&queue);
    
    if (Type == HCI_ACL_TYPE) {     
        if (pHcidevInfo->HciNormalMode) {  
            length = HCI_MAX_FRAME_SIZE;
        } else {
            length = MAX_ACL_RECV_LENGTH;    
        }
    } else {
        length = MAX_EVT_RECV_LENGTH;
    }
    
        /* add on transport head and tail room */ 
    length += pHcidevInfo->HCIProps.HeadRoom + pHcidevInfo->HCIProps.TailRoom;
        /* round up to the required I/O padding */      
    length = BLOCK_ROUND_UP_PWR2(length,pHcidevInfo->HCIProps.IOBlockPad);
             
    for (i = 0; i < NumBuffers; i++) {   
           
        if (pHcidevInfo->HciNormalMode) {   
            osBuf = bt_alloc_buffer(pHcidevInfo,length);       
        } else {
            osBuf = A_NETBUF_ALLOC(length);  
        }
          
        if (NULL == osBuf) {
            break;    
        }            
         
        pPacket = AllocHTCStruct(pHcidevInfo);
        if (NULL == pPacket) {
            FreeBtOsBuf(pHcidevInfo,osBuf);
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("Failed to alloc HTC struct \n"));
            break;    
        }     
        
        SET_HTC_PACKET_INFO_RX_REFILL(pPacket,osBuf,A_NETBUF_DATA(osBuf),length,Type);
            /* add to queue */
        HTC_PACKET_ENQUEUE(&queue,pPacket);
    }
    
    if (i > 0) {
        HCI_TransportAddReceivePkts(pHcidevInfo->pHCIDev, &queue);    
    }
}

#define HOST_INTEREST_ITEM_ADDRESS(ar, item) \
        (((ar)->arTargetType == TARGET_TYPE_AR6002) ? AR6002_HOST_INTEREST_ITEM_ADDRESS(item) : \
        (((ar)->arTargetType == TARGET_TYPE_AR6003) ? AR6003_HOST_INTEREST_ITEM_ADDRESS(item) : 0))
static int ar6000_hci_transport_ready(HCI_TRANSPORT_HANDLE     HCIHandle,
                                           struct hci_transport_properties *pProps, 
                                           void                     *pContext)
{
    struct ar6k_hci_bridge_info *pHcidevInfo = (struct ar6k_hci_bridge_info *)pContext;
    int              status;
    u32 address, hci_uart_pwr_mgmt_params;
//    struct ar3k_config_info      ar3kconfig;
    
    pHcidevInfo->pHCIDev = HCIHandle;
    
    memcpy(&pHcidevInfo->HCIProps,pProps,sizeof(*pProps));
    
    AR_DEBUG_PRINTF(ATH_DEBUG_HCI_BRIDGE,("HCI ready (hci:0x%lX, headroom:%d, tailroom:%d blockpad:%d) \n", 
            (unsigned long)HCIHandle, 
            pHcidevInfo->HCIProps.HeadRoom, 
            pHcidevInfo->HCIProps.TailRoom,
            pHcidevInfo->HCIProps.IOBlockPad));
    
#ifdef EXPORT_HCI_BRIDGE_INTERFACE
    A_ASSERT((pProps->HeadRoom + pProps->TailRoom) <= (struct net_device *)(pHcidevInfo->HCITransHdl.netDevice)->hard_header_len);
#else
    A_ASSERT((pProps->HeadRoom + pProps->TailRoom) <= pHcidevInfo->ar->arNetDev->hard_header_len);
#endif
                             
        /* provide buffers */
    RefillRecvBuffers(pHcidevInfo, HCI_ACL_TYPE, MAX_ACL_RECV_BUFS);
    RefillRecvBuffers(pHcidevInfo, HCI_EVENT_TYPE, MAX_EVT_RECV_BUFS);
   
    do {
            /* start transport */
        status = HCI_TransportStart(pHcidevInfo->pHCIDev);
         
        if (status) {
            break;    
        }
        
        if (!pHcidevInfo->HciNormalMode) {
                /* in test mode, no need to go any further */
            break;    
        }

        // The delay is required when AR6K is driving the BT reset line
        // where time is needed after the BT chip is out of reset (HCI_TransportStart)
        // and before the first HCI command is issued (AR3KConfigure)
        // FIXME
        // The delay should be configurable and be only applied when AR6K driving the BT
        // reset line. This could be done by some module parameter or based on some HW config
        // info. For now apply 100ms delay blindly
        A_MDELAY(100);
        
        A_MEMZERO(&ar3kconfig,sizeof(ar3kconfig));
        ar3kconfig.pHCIDev = pHcidevInfo->pHCIDev;
        ar3kconfig.pHCIProps = &pHcidevInfo->HCIProps;
#ifdef EXPORT_HCI_BRIDGE_INTERFACE
        ar3kconfig.pHIFDevice = (struct hif_device *)(pHcidevInfo->HCITransHdl.hifDevice);
#else
        ar3kconfig.pHIFDevice = pHcidevInfo->ar->arHifDevice;
#endif
        ar3kconfig.pBtStackHCIDev = pHcidevInfo->pBtStackHCIDev;
        
        if (ar3khcibaud != 0) {
                /* user wants ar3k baud rate change */
            ar3kconfig.Flags |= AR3K_CONFIG_FLAG_SET_AR3K_BAUD;
            ar3kconfig.Flags |= AR3K_CONFIG_FLAG_AR3K_BAUD_CHANGE_DELAY;
            ar3kconfig.AR3KBaudRate = ar3khcibaud;    
        }
        
        if ((hciuartscale != 0) || (hciuartstep != 0)) {   
                /* user wants to tune HCI bridge UART scale/step values */
            ar3kconfig.AR6KScale = (u16)hciuartscale;
            ar3kconfig.AR6KStep = (u16)hciuartstep;
            ar3kconfig.Flags |= AR3K_CONFIG_FLAG_SET_AR6K_SCALE_STEP;
        }
        
        /* Fetch the address of the hi_hci_uart_pwr_mgmt_params instance in the host interest area */
        address = TARG_VTOP(pHcidevInfo->ar->arTargetType, 
                            HOST_INTEREST_ITEM_ADDRESS(pHcidevInfo->ar, hi_hci_uart_pwr_mgmt_params));
        status = ar6000_ReadRegDiag(pHcidevInfo->ar->arHifDevice, &address, &hci_uart_pwr_mgmt_params);
        if (0 == status) {
            ar3kconfig.PwrMgmtEnabled = (hci_uart_pwr_mgmt_params & 0x1);
            ar3kconfig.IdleTimeout = (hci_uart_pwr_mgmt_params & 0xFFFF0000) >> 16;
            ar3kconfig.WakeupTimeout = (hci_uart_pwr_mgmt_params & 0xFF00) >> 8;
        } else {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("HCI Bridge: failed to read hci_uart_pwr_mgmt_params! \n"));
        }
        /* configure the AR3K device */         
		memcpy(ar3kconfig.bdaddr,pHcidevInfo->ar->bdaddr,6);
        status = AR3KConfigure(&ar3kconfig);
        if (status) {
            break; 
        }

        /* Make sure both AR6K and AR3K have power management enabled */
        if (ar3kconfig.PwrMgmtEnabled) {
            status = HCI_TransportEnablePowerMgmt(pHcidevInfo->pHCIDev, true);
            if (status) {
                AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("HCI Bridge: failed to enable TLPM for AR6K! \n"));
            }
        }
        
        status = bt_register_hci(pHcidevInfo);
        
    } while (false);

    return status; 
}

static void ar6000_hci_transport_failure(void *pContext, int Status)
{
    struct ar6k_hci_bridge_info *pHcidevInfo = (struct ar6k_hci_bridge_info *)pContext;
    
    AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("HCI Bridge: transport failure! \n"));
    
    if (pHcidevInfo->HciNormalMode) {
        /* TODO .. */    
    }
}

static void ar6000_hci_transport_removed(void *pContext)
{
    struct ar6k_hci_bridge_info *pHcidevInfo = (struct ar6k_hci_bridge_info *)pContext;
    
    AR_DEBUG_PRINTF(ATH_DEBUG_HCI_BRIDGE, ("HCI Bridge: transport removed. \n"));
    
    A_ASSERT(pHcidevInfo->pHCIDev != NULL);
        
    HCI_TransportDetach(pHcidevInfo->pHCIDev);
    bt_cleanup_hci(pHcidevInfo);
    pHcidevInfo->pHCIDev = NULL;
}

static void ar6000_hci_send_complete(void *pContext, struct htc_packet *pPacket)
{
    struct ar6k_hci_bridge_info *pHcidevInfo = (struct ar6k_hci_bridge_info *)pContext;
    void                 *osbuf = pPacket->pPktContext;
    A_ASSERT(osbuf != NULL);
    A_ASSERT(pHcidevInfo != NULL);
    
    if (pPacket->Status) {
        if ((pPacket->Status != A_ECANCELED) && (pPacket->Status != A_NO_RESOURCE)) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("HCI Bridge: Send Packet Failed: %d \n",pPacket->Status)); 
        }   
    }
            
    FreeHTCStruct(pHcidevInfo,pPacket);    
    FreeBtOsBuf(pHcidevInfo,osbuf);
    
}

static void ar6000_hci_pkt_recv(void *pContext, struct htc_packet *pPacket)
{
    struct ar6k_hci_bridge_info *pHcidevInfo = (struct ar6k_hci_bridge_info *)pContext;
    struct sk_buff       *skb;
    
    A_ASSERT(pHcidevInfo != NULL);
    skb = (struct sk_buff *)pPacket->pPktContext;
    A_ASSERT(skb != NULL);
          
    do {
        
        if (pPacket->Status) {
            break;
        }
  
        AR_DEBUG_PRINTF(ATH_DEBUG_HCI_RECV, 
                        ("HCI Bridge, packet received type : %d len:%d \n",
                        HCI_GET_PACKET_TYPE(pPacket),pPacket->ActualLength));
    
            /* set the actual buffer position in the os buffer, HTC recv buffers posted to HCI are set
             * to fill the front of the buffer */
        A_NETBUF_PUT(skb,pPacket->ActualLength + pHcidevInfo->HCIProps.HeadRoom);
        A_NETBUF_PULL(skb,pHcidevInfo->HCIProps.HeadRoom);
        
        if (AR_DEBUG_LVL_CHECK(ATH_DEBUG_HCI_DUMP)) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ANY,("<<< Recv HCI %s packet len:%d \n",
                        (HCI_GET_PACKET_TYPE(pPacket) == HCI_EVENT_TYPE) ? "EVENT" : "ACL",
                        skb->len));
            AR_DEBUG_PRINTBUF(skb->data, skb->len,"BT HCI RECV Packet Dump");
        }
        
        if (pHcidevInfo->HciNormalMode) {
                /* indicate the packet */         
            if (bt_indicate_recv(pHcidevInfo,HCI_GET_PACKET_TYPE(pPacket),skb)) {
                    /* bt stack accepted the packet */
                skb = NULL;
            }  
            break;
        }
        
            /* for testing, indicate packet to the network stack */ 
#ifdef EXPORT_HCI_BRIDGE_INTERFACE
        skb->dev = (struct net_device *)(pHcidevInfo->HCITransHdl.netDevice);        
        if ((((struct net_device *)pHcidevInfo->HCITransHdl.netDevice)->flags & IFF_UP) == IFF_UP) {
            skb->protocol = eth_type_trans(skb, (struct net_device *)(pHcidevInfo->HCITransHdl.netDevice));
#else
        skb->dev = pHcidevInfo->ar->arNetDev;        
        if ((pHcidevInfo->ar->arNetDev->flags & IFF_UP) == IFF_UP) {
            skb->protocol = eth_type_trans(skb, pHcidevInfo->ar->arNetDev);
#endif
            netif_rx(skb);
            skb = NULL;
        } 
        
    } while (false);
    
    FreeHTCStruct(pHcidevInfo,pPacket);
    
    if (skb != NULL) {
            /* packet was not accepted, free it */
        FreeBtOsBuf(pHcidevInfo,skb);       
    }
    
}

static void  ar6000_hci_pkt_refill(void *pContext, HCI_TRANSPORT_PACKET_TYPE Type, int BuffersAvailable)
{
    struct ar6k_hci_bridge_info *pHcidevInfo = (struct ar6k_hci_bridge_info *)pContext;
    int                  refillCount;

    if (Type == HCI_ACL_TYPE) {
        refillCount =  MAX_ACL_RECV_BUFS - BuffersAvailable;   
    } else {
        refillCount =  MAX_EVT_RECV_BUFS - BuffersAvailable;     
    }
    
    if (refillCount > 0) {
        RefillRecvBuffers(pHcidevInfo,Type,refillCount);
    }
    
}

static HCI_SEND_FULL_ACTION  ar6000_hci_pkt_send_full(void *pContext, struct htc_packet *pPacket)
{
    struct ar6k_hci_bridge_info    *pHcidevInfo = (struct ar6k_hci_bridge_info *)pContext;
    HCI_SEND_FULL_ACTION    action = HCI_SEND_FULL_KEEP;
    
    if (!pHcidevInfo->HciNormalMode) {
            /* for epping testing, check packet tag, some epping packets are
             * special and cannot be dropped */
        if (HTC_GET_TAG_FROM_PKT(pPacket) == AR6K_DATA_PKT_TAG) {
            action = HCI_SEND_FULL_DROP;     
        }
    }
    
    return action;
}

#ifdef EXPORT_HCI_BRIDGE_INTERFACE
int ar6000_setup_hci(void *ar)
#else
int ar6000_setup_hci(struct ar6_softc *ar)
#endif
{
    struct hci_transport_config_info config;
    int                  status = 0;
    int                       i;
    struct htc_packet                *pPacket;
    struct ar6k_hci_bridge_info      *pHcidevInfo;
        
       
    do {
        
        pHcidevInfo = (struct ar6k_hci_bridge_info *)A_MALLOC(sizeof(struct ar6k_hci_bridge_info));
        
        if (NULL == pHcidevInfo) {
            status = A_NO_MEMORY;
            break;    
        }
        
        A_MEMZERO(pHcidevInfo, sizeof(struct ar6k_hci_bridge_info));
#ifdef EXPORT_HCI_BRIDGE_INTERFACE
        g_pHcidevInfo = pHcidevInfo;
        pHcidevInfo->HCITransHdl = *(struct hci_transport_misc_handles *)ar;
#else
        ar->hcidev_info = pHcidevInfo;
        pHcidevInfo->ar = ar;
#endif
        spin_lock_init(&pHcidevInfo->BridgeLock);
        INIT_HTC_PACKET_QUEUE(&pHcidevInfo->HTCPacketStructHead);

        ar->exitCallback = AR3KConfigureExit;
    
        status = bt_setup_hci(pHcidevInfo);
        if (status) {
            break;    
        }
        
        if (pHcidevInfo->HciNormalMode) {      
            AR_DEBUG_PRINTF(ATH_DEBUG_HCI_BRIDGE, ("HCI Bridge: running in normal mode... \n"));    
        } else {
            AR_DEBUG_PRINTF(ATH_DEBUG_HCI_BRIDGE, ("HCI Bridge: running in test mode... \n"));     
        }
        
        pHcidevInfo->pHTCStructAlloc = (u8 *)A_MALLOC((sizeof(struct htc_packet)) * NUM_HTC_PACKET_STRUCTS);
        
        if (NULL == pHcidevInfo->pHTCStructAlloc) {
            status = A_NO_MEMORY;
            break;    
        }
        
        pPacket = (struct htc_packet *)pHcidevInfo->pHTCStructAlloc;
        for (i = 0; i < NUM_HTC_PACKET_STRUCTS; i++,pPacket++) {
            FreeHTCStruct(pHcidevInfo,pPacket);                
        }
        
        A_MEMZERO(&config,sizeof(struct hci_transport_config_info));        
        config.ACLRecvBufferWaterMark = MAX_ACL_RECV_BUFS / 2;
        config.EventRecvBufferWaterMark = MAX_EVT_RECV_BUFS / 2;
        config.MaxSendQueueDepth = MAX_HCI_WRITE_QUEUE_DEPTH;
        config.pContext = pHcidevInfo;    
        config.TransportFailure = ar6000_hci_transport_failure;
        config.TransportReady = ar6000_hci_transport_ready;
        config.TransportRemoved = ar6000_hci_transport_removed;
        config.pHCISendComplete = ar6000_hci_send_complete;
        config.pHCIPktRecv = ar6000_hci_pkt_recv;
        config.pHCIPktRecvRefill = ar6000_hci_pkt_refill;
        config.pHCISendFull = ar6000_hci_pkt_send_full;
       
#ifdef EXPORT_HCI_BRIDGE_INTERFACE
        pHcidevInfo->pHCIDev = HCI_TransportAttach(pHcidevInfo->HCITransHdl.htcHandle, &config);
#else
        pHcidevInfo->pHCIDev = HCI_TransportAttach(ar->arHtcTarget, &config);
#endif

        if (NULL == pHcidevInfo->pHCIDev) {
            status = A_ERROR;      
        }
    
    } while (false);
    
    if (status) {
        if (pHcidevInfo != NULL) {
            if (NULL == pHcidevInfo->pHCIDev) {
                /* GMBOX may not be present in older chips */
                /* just return success */ 
                status = 0;
            }
        }
        ar6000_cleanup_hci(ar);    
    }
    
    return status;
}

#ifdef EXPORT_HCI_BRIDGE_INTERFACE
void  ar6000_cleanup_hci(void *ar)
#else
void  ar6000_cleanup_hci(struct ar6_softc *ar)
#endif
{
#ifdef EXPORT_HCI_BRIDGE_INTERFACE
    struct ar6k_hci_bridge_info *pHcidevInfo = g_pHcidevInfo;
#else
    struct ar6k_hci_bridge_info *pHcidevInfo = (struct ar6k_hci_bridge_info *)ar->hcidev_info;
#endif
    
    if (pHcidevInfo != NULL) {
        bt_cleanup_hci(pHcidevInfo);   
        
        if (pHcidevInfo->pHCIDev != NULL) {
            HCI_TransportStop(pHcidevInfo->pHCIDev);
            HCI_TransportDetach(pHcidevInfo->pHCIDev);
            pHcidevInfo->pHCIDev = NULL;
        } 
        
        if (pHcidevInfo->pHTCStructAlloc != NULL) {
            A_FREE(pHcidevInfo->pHTCStructAlloc);
            pHcidevInfo->pHTCStructAlloc = NULL;    
        }
        
        A_FREE(pHcidevInfo);
#ifndef EXPORT_HCI_BRIDGE_INTERFACE
        ar->hcidev_info = NULL;
#endif
    }
    
    
}

#ifdef EXPORT_HCI_BRIDGE_INTERFACE
int hci_test_send(void *ar, struct sk_buff *skb)
#else
int hci_test_send(struct ar6_softc *ar, struct sk_buff *skb)
#endif
{
    int              status = 0;
    int              length;
    EPPING_HEADER    *pHeader;
    struct htc_packet       *pPacket;   
    HTC_TX_TAG       htc_tag = AR6K_DATA_PKT_TAG;
#ifdef EXPORT_HCI_BRIDGE_INTERFACE
    struct ar6k_hci_bridge_info *pHcidevInfo = g_pHcidevInfo;
#else
    struct ar6k_hci_bridge_info *pHcidevInfo = (struct ar6k_hci_bridge_info *)ar->hcidev_info;
#endif
            
    do {
        
        if (NULL == pHcidevInfo) {
            status = A_ERROR;
            break;    
        }
        
        if (NULL == pHcidevInfo->pHCIDev) {
            status = A_ERROR;
            break;    
        }
        
        if (pHcidevInfo->HciNormalMode) {
                /* this interface cannot run when normal WMI is running */
            status = A_ERROR;
            break;    
        }
        
        pHeader = (EPPING_HEADER *)A_NETBUF_DATA(skb);
        
        if (!IS_EPPING_PACKET(pHeader)) {
            status = A_EINVAL;
            break;
        }
                       
        if (IS_EPING_PACKET_NO_DROP(pHeader)) {
            htc_tag = AR6K_CONTROL_PKT_TAG;   
        }
        
        length = sizeof(EPPING_HEADER) + pHeader->DataLength;
                
        pPacket = AllocHTCStruct(pHcidevInfo);
        if (NULL == pPacket) {        
            status = A_NO_MEMORY;
            break;
        } 
     
        SET_HTC_PACKET_INFO_TX(pPacket,
                               skb,
                               A_NETBUF_DATA(skb),
                               length,
                               HCI_ACL_TYPE,  /* send every thing out as ACL */
                               htc_tag);
             
        HCI_TransportSendPkt(pHcidevInfo->pHCIDev,pPacket,false);
        pPacket = NULL;
            
    } while (false);
            
    return status;
}

void ar6000_set_default_ar3kconfig(struct ar6_softc *ar, void *ar3kconfig)
{
    struct ar6k_hci_bridge_info *pHcidevInfo = (struct ar6k_hci_bridge_info *)ar->hcidev_info;
    struct ar3k_config_info *config = (struct ar3k_config_info *)ar3kconfig;

    config->pHCIDev = pHcidevInfo->pHCIDev;
    config->pHCIProps = &pHcidevInfo->HCIProps;
    config->pHIFDevice = ar->arHifDevice;
    config->pBtStackHCIDev = pHcidevInfo->pBtStackHCIDev;
    config->Flags |= AR3K_CONFIG_FLAG_SET_AR3K_BAUD;
    config->AR3KBaudRate = 115200;    
}

#ifdef CONFIG_BLUEZ_HCI_BRIDGE   
/*** BT Stack Entrypoints *******/

/*
 * bt_open - open a handle to the device
*/
static int bt_open(struct hci_dev *hdev)
{
 
    AR_DEBUG_PRINTF(ATH_DEBUG_TRC, ("HCI Bridge: bt_open - enter - x\n"));
    set_bit(HCI_RUNNING, &hdev->flags);
    set_bit(HCI_UP, &hdev->flags);
    set_bit(HCI_INIT, &hdev->flags);         
    return 0;
}

/*
 * bt_close - close handle to the device
*/
static int bt_close(struct hci_dev *hdev)
{
    AR_DEBUG_PRINTF(ATH_DEBUG_TRC, ("HCI Bridge: bt_close - enter\n"));
    clear_bit(HCI_RUNNING, &hdev->flags);
    return 0;
}

/*
 * bt_send_frame - send data frames
*/
static int bt_send_frame(struct sk_buff *skb)
{
    struct hci_dev             *hdev = (struct hci_dev *)skb->dev;
    HCI_TRANSPORT_PACKET_TYPE  type;
    struct ar6k_hci_bridge_info       *pHcidevInfo;
    struct htc_packet                 *pPacket;
    int                   status = 0;
    struct sk_buff             *txSkb = NULL;
    
    if (!hdev) {
        AR_DEBUG_PRINTF(ATH_DEBUG_WARN, ("HCI Bridge: bt_send_frame - no device\n"));
        return -ENODEV;
    }
      
    if (!test_bit(HCI_RUNNING, &hdev->flags)) {
        AR_DEBUG_PRINTF(ATH_DEBUG_TRC, ("HCI Bridge: bt_send_frame - not open\n"));
        return -EBUSY;
    }
  
    pHcidevInfo = (struct ar6k_hci_bridge_info *)hdev->driver_data;   
    A_ASSERT(pHcidevInfo != NULL);
      
    AR_DEBUG_PRINTF(ATH_DEBUG_HCI_SEND, ("+bt_send_frame type: %d \n",bt_cb(skb)->pkt_type));
    type = HCI_COMMAND_TYPE;
    
    switch (bt_cb(skb)->pkt_type) {
        case HCI_COMMAND_PKT:
            type = HCI_COMMAND_TYPE;
            hdev->stat.cmd_tx++;
            break;
 
        case HCI_ACLDATA_PKT:
            type = HCI_ACL_TYPE;
            hdev->stat.acl_tx++;
            break;

        case HCI_SCODATA_PKT:
            /* we don't support SCO over the bridge */
            kfree_skb(skb);
            return 0;
        default:
            A_ASSERT(false);
            kfree_skb(skb);
            return 0;
    } 

    if (AR_DEBUG_LVL_CHECK(ATH_DEBUG_HCI_DUMP)) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ANY,(">>> Send HCI %s packet len: %d\n",
                        (type == HCI_COMMAND_TYPE) ? "COMMAND" : "ACL",
                        skb->len));
        if (type == HCI_COMMAND_TYPE) {
            u16 opcode = HCI_GET_OP_CODE(skb->data);
            AR_DEBUG_PRINTF(ATH_DEBUG_ANY,("    HCI Command: OGF:0x%X OCF:0x%X \r\n", 
                  opcode >> 10, opcode & 0x3FF));
        }
        AR_DEBUG_PRINTBUF(skb->data,skb->len,"BT HCI SEND Packet Dump");
    }
    
    do {
        
        txSkb = bt_skb_alloc(TX_PACKET_RSV_OFFSET + pHcidevInfo->HCIProps.HeadRoom + 
                             pHcidevInfo->HCIProps.TailRoom + skb->len, 
                             GFP_ATOMIC);

        if (txSkb == NULL) {
            status = A_NO_MEMORY;
            break;    
        }
        
        bt_cb(txSkb)->pkt_type = bt_cb(skb)->pkt_type;
        txSkb->dev = (void *)pHcidevInfo->pBtStackHCIDev;
        skb_reserve(txSkb, TX_PACKET_RSV_OFFSET + pHcidevInfo->HCIProps.HeadRoom);
        memcpy(txSkb->data, skb->data, skb->len);
        skb_put(txSkb,skb->len);
        
        pPacket = AllocHTCStruct(pHcidevInfo);        
        if (NULL == pPacket) {
            status = A_NO_MEMORY;
            break;    
        }       
              
        /* HCI packet length here doesn't include the 1-byte transport header which
         * will be handled by the HCI transport layer. Enough headroom has already
         * been reserved above for the transport header
         */
        SET_HTC_PACKET_INFO_TX(pPacket,
                               txSkb,
                               txSkb->data,
                               txSkb->len,
                               type, 
                               AR6K_CONTROL_PKT_TAG); /* HCI packets cannot be dropped */
        
        AR_DEBUG_PRINTF(ATH_DEBUG_HCI_SEND, ("HCI Bridge: bt_send_frame skb:0x%lX \n",(unsigned long)txSkb));
        AR_DEBUG_PRINTF(ATH_DEBUG_HCI_SEND, ("HCI Bridge: type:%d, Total Length:%d Bytes \n",
                                      type, txSkb->len));
                                      
        status = HCI_TransportSendPkt(pHcidevInfo->pHCIDev,pPacket,false);
        pPacket = NULL;
        txSkb = NULL;
        
    } while (false);
   
    if (txSkb != NULL) {
        kfree_skb(txSkb);    
    }
    
    kfree_skb(skb);        
       
    AR_DEBUG_PRINTF(ATH_DEBUG_HCI_SEND, ("-bt_send_frame  \n"));
    return 0;
}

/*
 * bt_ioctl - ioctl processing
*/
static int bt_ioctl(struct hci_dev *hdev, unsigned int cmd, unsigned long arg)
{
    AR_DEBUG_PRINTF(ATH_DEBUG_TRC, ("HCI Bridge: bt_ioctl - enter\n"));
    return -ENOIOCTLCMD;
}

/*
 * bt_flush - flush outstandingbpackets
*/
static int bt_flush(struct hci_dev *hdev)
{
    struct ar6k_hci_bridge_info    *pHcidevInfo; 
    
    AR_DEBUG_PRINTF(ATH_DEBUG_TRC, ("HCI Bridge: bt_flush - enter\n"));
    
    pHcidevInfo = (struct ar6k_hci_bridge_info *)hdev->driver_data;   
    
    /* TODO??? */   
    
    return 0;
}


/*
 * bt_destruct - 
*/
static void bt_destruct(struct hci_dev *hdev)
{
    AR_DEBUG_PRINTF(ATH_DEBUG_TRC, ("HCI Bridge: bt_destruct - enter\n"));
    /* nothing to do here */
}

static int bt_setup_hci(struct ar6k_hci_bridge_info *pHcidevInfo)
{
    int                    status = 0;
    struct hci_dev              *pHciDev = NULL;
    struct hif_device_os_device_info   osDevInfo;
    
    if (!setupbtdev) {
        return 0;
    } 
        
    do {
            
        A_MEMZERO(&osDevInfo,sizeof(osDevInfo));
            /* get the underlying OS device */
#ifdef EXPORT_HCI_BRIDGE_INTERFACE
        status = ar6000_get_hif_dev((struct hif_device *)(pHcidevInfo->HCITransHdl.hifDevice), 
                                    &osDevInfo);
#else
        status = HIFConfigureDevice(pHcidevInfo->ar->arHifDevice, 
                                    HIF_DEVICE_GET_OS_DEVICE,
                                    &osDevInfo, 
                                    sizeof(osDevInfo));
#endif
                                    
        if (status) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("Failed to OS device info from HIF\n"));
            break;
        }
        
            /* allocate a BT HCI struct for this device */
        pHciDev = hci_alloc_dev();
        if (NULL == pHciDev) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("HCI Bridge - failed to allocate bt struct \n"));
            status = A_NO_MEMORY;
            break;
        }    
            /* save the device, we'll register this later */
        pHcidevInfo->pBtStackHCIDev = pHciDev;       
        SET_HCIDEV_DEV(pHciDev,osDevInfo.pOSDevice);          
        SET_HCI_BUS_TYPE(pHciDev, HCI_VIRTUAL, HCI_BREDR);
        pHciDev->driver_data = pHcidevInfo;
        pHciDev->open     = bt_open;
        pHciDev->close    = bt_close;
        pHciDev->send     = bt_send_frame;
        pHciDev->ioctl    = bt_ioctl;
        pHciDev->flush    = bt_flush;
        pHciDev->destruct = bt_destruct;
        pHciDev->owner = THIS_MODULE; 
            /* driver is running in normal BT mode */
        pHcidevInfo->HciNormalMode = true;
        
    } while (false);
    
    if (status) {
        bt_cleanup_hci(pHcidevInfo);    
    }
    
    return status;
}

static void bt_cleanup_hci(struct ar6k_hci_bridge_info *pHcidevInfo)
{   
    int   err;      
        
    if (pHcidevInfo->HciRegistered) {
        pHcidevInfo->HciRegistered = false;
        clear_bit(HCI_RUNNING, &pHcidevInfo->pBtStackHCIDev->flags);
        clear_bit(HCI_UP, &pHcidevInfo->pBtStackHCIDev->flags);
        clear_bit(HCI_INIT, &pHcidevInfo->pBtStackHCIDev->flags);   
        A_ASSERT(pHcidevInfo->pBtStackHCIDev != NULL);
            /* unregister */
        if ((err = hci_unregister_dev(pHcidevInfo->pBtStackHCIDev)) < 0) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("HCI Bridge: failed to unregister with bluetooth %d\n",err));
        }          
    }   
    
    kfree(pHcidevInfo->pBtStackHCIDev);
    pHcidevInfo->pBtStackHCIDev = NULL;  
}

static int bt_register_hci(struct ar6k_hci_bridge_info *pHcidevInfo)
{
    int       err;
    int  status = 0;
    
    do {          
        AR_DEBUG_PRINTF(ATH_DEBUG_HCI_BRIDGE, ("HCI Bridge: registering HCI... \n"));
        A_ASSERT(pHcidevInfo->pBtStackHCIDev != NULL);
             /* mark that we are registered */
        pHcidevInfo->HciRegistered = true;
        if ((err = hci_register_dev(pHcidevInfo->pBtStackHCIDev)) < 0) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("HCI Bridge: failed to register with bluetooth %d\n",err));
            pHcidevInfo->HciRegistered = false;
            status = A_ERROR;
            break;
        }
    
        AR_DEBUG_PRINTF(ATH_DEBUG_HCI_BRIDGE, ("HCI Bridge: HCI registered \n"));
        
    } while (false);
    
    return status;
}

static bool bt_indicate_recv(struct ar6k_hci_bridge_info      *pHcidevInfo,
                               HCI_TRANSPORT_PACKET_TYPE Type, 
                               struct                    sk_buff *skb)
{
    u8 btType;
    int                   len;
    bool                success = false;
    BT_HCI_EVENT_HEADER   *pEvent;
    
    do {
             
        if (!test_bit(HCI_RUNNING, &pHcidevInfo->pBtStackHCIDev->flags)) {
            AR_DEBUG_PRINTF(ATH_DEBUG_WARN, ("HCI Bridge: bt_indicate_recv - not running\n"));
            break;
        }
    
        switch (Type) {
            case HCI_ACL_TYPE:
                btType = HCI_ACLDATA_PKT;
                break;
            case HCI_EVENT_TYPE:  
                btType = HCI_EVENT_PKT;  
                break;
            default:
                btType = 0;
                A_ASSERT(false);
                break;
        } 
        
        if (0 == btType) {
            break;    
        }
        
            /* set the final type */
        bt_cb(skb)->pkt_type = btType;
            /* set dev */
        skb->dev = (void *)pHcidevInfo->pBtStackHCIDev;
        len = skb->len;
        
        if (AR_DEBUG_LVL_CHECK(ATH_DEBUG_HCI_RECV)) {
            if (bt_cb(skb)->pkt_type == HCI_EVENT_PKT) {                
                pEvent = (BT_HCI_EVENT_HEADER *)skb->data; 
                AR_DEBUG_PRINTF(ATH_DEBUG_HCI_RECV, ("BT HCI EventCode: %d, len:%d \n", 
                        pEvent->EventCode, pEvent->ParamLength));
            } 
        }
        
            /* pass receive packet up the stack */    
        if (hci_recv_frame(skb) != 0) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("HCI Bridge: hci_recv_frame failed \n"));
            break;
        } else {
            AR_DEBUG_PRINTF(ATH_DEBUG_HCI_RECV, 
                    ("HCI Bridge: Indicated RCV of type:%d, Length:%d \n",btType,len));
        }
            
        success = true;
    
    } while (false);
    
    return success;
}

static struct sk_buff* bt_alloc_buffer(struct ar6k_hci_bridge_info *pHcidevInfo, int Length) 
{ 
    struct sk_buff *skb;         
        /* in normal HCI mode we need to alloc from the bt core APIs */
    skb = bt_skb_alloc(Length, GFP_ATOMIC);
    if (NULL == skb) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("Failed to alloc bt sk_buff \n"));
    }
    return skb;
}

static void bt_free_buffer(struct ar6k_hci_bridge_info *pHcidevInfo, struct sk_buff *skb)
{
    kfree_skb(skb);    
}

#else // { CONFIG_BLUEZ_HCI_BRIDGE

    /* stubs when we only want to test the HCI bridging Interface without the HT stack */
static int bt_setup_hci(struct ar6k_hci_bridge_info *pHcidevInfo)
{
    return 0;
}
static void bt_cleanup_hci(struct ar6k_hci_bridge_info *pHcidevInfo)
{   
     
}
static int bt_register_hci(struct ar6k_hci_bridge_info *pHcidevInfo)
{
    A_ASSERT(false);
    return A_ERROR;    
}

static bool bt_indicate_recv(struct ar6k_hci_bridge_info      *pHcidevInfo,
                               HCI_TRANSPORT_PACKET_TYPE Type, 
                               struct                    sk_buff *skb)
{
    A_ASSERT(false);
    return false;
}

static struct sk_buff* bt_alloc_buffer(struct ar6k_hci_bridge_info *pHcidevInfo, int Length) 
{
    A_ASSERT(false);
    return NULL;
}
static void bt_free_buffer(struct ar6k_hci_bridge_info *pHcidevInfo, struct sk_buff *skb)
{
    A_ASSERT(false);
}

#endif // } CONFIG_BLUEZ_HCI_BRIDGE

#else  // { ATH_AR6K_ENABLE_GMBOX

    /* stubs when GMBOX support is not needed */
    
#ifdef EXPORT_HCI_BRIDGE_INTERFACE
int ar6000_setup_hci(void *ar)
#else
int ar6000_setup_hci(struct ar6_softc *ar)
#endif
{
    return 0;
}

#ifdef EXPORT_HCI_BRIDGE_INTERFACE
void ar6000_cleanup_hci(void *ar)
#else
void ar6000_cleanup_hci(struct ar6_softc *ar)
#endif
{
    return;    
}

#ifndef EXPORT_HCI_BRIDGE_INTERFACE
void ar6000_set_default_ar3kconfig(struct ar6_softc *ar, void *ar3kconfig)
{
    return;
}
#endif

#ifdef EXPORT_HCI_BRIDGE_INTERFACE
int hci_test_send(void *ar, struct sk_buff *skb)
#else
int hci_test_send(struct ar6_softc *ar, struct sk_buff *skb)
#endif
{
    return -EOPNOTSUPP;
}

#endif // } ATH_AR6K_ENABLE_GMBOX


#ifdef EXPORT_HCI_BRIDGE_INTERFACE
static int __init
hcibridge_init_module(void)
{
    int status;
    struct hci_transport_callbacks hciTransCallbacks;

    hciTransCallbacks.setupTransport = ar6000_setup_hci;
    hciTransCallbacks.cleanupTransport = ar6000_cleanup_hci;

    status = ar6000_register_hci_transport(&hciTransCallbacks);
    if (status)
        return -ENODEV;

    return 0;
}

static void __exit
hcibridge_cleanup_module(void)
{
}

module_init(hcibridge_init_module);
module_exit(hcibridge_cleanup_module);
MODULE_LICENSE("Dual BSD/GPL");
#endif
