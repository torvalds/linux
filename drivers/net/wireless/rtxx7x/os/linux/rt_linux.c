/*
 *************************************************************************
 * Ralink Tech Inc.
 * 5F., No.36, Taiyuan St., Jhubei City,
 * Hsinchu County 302,
 * Taiwan, R.O.C.
 *
 * (c) Copyright 2002-2010, Ralink Technology, Inc.
 *
 * This program is free software; you can redistribute it and/or modify  *
 * it under the terms of the GNU General Public License as published by  *
 * the Free Software Foundation; either version 2 of the License, or     *
 * (at your option) any later version.                                   *
 *                                                                       *
 * This program is distributed in the hope that it will be useful,       *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 * GNU General Public License for more details.                          *
 *                                                                       *
 * You should have received a copy of the GNU General Public License     *
 * along with this program; if not, write to the                         *
 * Free Software Foundation, Inc.,                                       *
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 *                                                                       *
 *************************************************************************/


#define RTMP_MODULE_OS
#define RTMP_MODULE_OS_UTIL

/*#include "rt_config.h" */
#include "rtmp_comm.h"
#include "rtmp_osabl.h"
#include "rt_os_util.h"

#if defined (CONFIG_RA_HW_NAT) || defined (CONFIG_RA_HW_NAT_MODULE)
#include "../../../../../../net/nat/hw_nat/ra_nat.h"
#include "../../../../../../net/nat/hw_nat/frame_engine.h"
#endif

/* TODO */
#undef RT_CONFIG_IF_OPMODE_ON_AP
#undef RT_CONFIG_IF_OPMODE_ON_STA

#if defined(CONFIG_AP_SUPPORT) && defined(CONFIG_STA_SUPPORT)
#define RT_CONFIG_IF_OPMODE_ON_AP(__OpMode)	if (__OpMode == OPMODE_AP)
#define RT_CONFIG_IF_OPMODE_ON_STA(__OpMode)	if (__OpMode == OPMODE_STA)
#else
#define RT_CONFIG_IF_OPMODE_ON_AP(__OpMode)
#define RT_CONFIG_IF_OPMODE_ON_STA(__OpMode)
#endif

ULONG RTDebugLevel = 1;


#ifdef OS_ABL_FUNC_SUPPORT
ULONG RTPktOffsetData = 0, RTPktOffsetLen = 0, RTPktOffsetCB = 0;
#endif /* OS_ABL_FUNC_SUPPORT */


#ifdef VENDOR_FEATURE4_SUPPORT
ULONG OS_NumOfMemAlloc = 0, OS_NumOfMemFree = 0;
#endif /* VENDOR_FEATURE4_SUPPORT */
#ifdef VENDOR_FEATURE2_SUPPORT
ULONG OS_NumOfPktAlloc = 0, OS_NumOfPktFree = 0;
#endif /* VENDOR_FEATURE2_SUPPORT */

/*
 * the lock will not be used in TX/RX
 * path so throughput should not be impacted
 */
BOOLEAN FlgIsUtilInit = FALSE;
/*NDIS_SPIN_LOCK UtilSemLock;*/
OS_NDIS_SPIN_LOCK UtilSemLock;

/*
========================================================================
Routine Description:
	Initialize something in UTIL module.

Arguments:
	None

Return Value:
	None

Note:
========================================================================
*/
VOID RtmpUtilInit(
	VOID)
{
	if (FlgIsUtilInit == FALSE) {
		OS_NdisAllocateSpinLock(&UtilSemLock);
		FlgIsUtilInit = TRUE;
	}
}

/* timeout -- ms */
static inline VOID __RTMP_SetPeriodicTimer(
	IN OS_NDIS_MINIPORT_TIMER * pTimer,
	IN unsigned long timeout)
{
	timeout = ((timeout * OS_HZ) / 1000);
	pTimer->expires = jiffies + timeout;
	add_timer(pTimer);
}

/* convert NdisMInitializeTimer --> RTMP_OS_Init_Timer */
static inline VOID __RTMP_OS_Init_Timer(
	IN VOID *pReserved,
	IN OS_NDIS_MINIPORT_TIMER * pTimer,
	IN TIMER_FUNCTION function,
	IN PVOID data)
{
	if (!timer_pending(pTimer)) {
		init_timer(pTimer);
		pTimer->data = (unsigned long)data;
		pTimer->function = function;
	}
}

static inline VOID __RTMP_OS_Add_Timer(
	IN OS_NDIS_MINIPORT_TIMER * pTimer,
	IN unsigned long timeout)
{
	if (timer_pending(pTimer))
		return;

	timeout = ((timeout * OS_HZ) / 1000);
	pTimer->expires = jiffies + timeout;
	add_timer(pTimer);
}

static inline VOID __RTMP_OS_Mod_Timer(
	IN OS_NDIS_MINIPORT_TIMER * pTimer,
	IN unsigned long timeout)
{
	timeout = ((timeout * OS_HZ) / 1000);
	mod_timer(pTimer, jiffies + timeout);
}

static inline VOID __RTMP_OS_Del_Timer(
	IN OS_NDIS_MINIPORT_TIMER * pTimer,
	OUT BOOLEAN *pCancelled)
{
	if (timer_pending(pTimer))
		*pCancelled = del_timer_sync(pTimer);
	else
		*pCancelled = TRUE;
}

static inline VOID __RTMP_OS_Release_Timer(
	IN OS_NDIS_MINIPORT_TIMER * pTimer)
{
	/* nothing to do */
}


/* Unify all delay routine by using udelay */
VOID RTMPusecDelay(
	IN ULONG usec)
{
	ULONG i;

	for (i = 0; i < (usec / 50); i++)
		udelay(50);

	if (usec % 50)
		udelay(usec % 50);
}

VOID RtmpOsMsDelay(
	IN ULONG msec)
{
	mdelay(msec);
}

void RTMP_GetCurrentSystemTime(
	LARGE_INTEGER * time)
{
	time->u.LowPart = jiffies;
}

void RTMP_GetCurrentSystemTick(
	ULONG *pNow)
{
	*pNow = jiffies;
}

/* pAd MUST allow to be NULL */

NDIS_STATUS os_alloc_mem(
	IN VOID *pReserved,
	OUT UCHAR **mem,
	IN ULONG size)
{
	*mem = (PUCHAR) kmalloc(size, GFP_ATOMIC);
	if (*mem) {
#ifdef VENDOR_FEATURE4_SUPPORT
		OS_NumOfMemAlloc++;
#endif /* VENDOR_FEATURE4_SUPPORT */

		return (NDIS_STATUS_SUCCESS);
	} else
		return (NDIS_STATUS_FAILURE);
}

NDIS_STATUS os_alloc_mem_suspend(
	IN VOID *pReserved,
	OUT UCHAR **mem,
	IN ULONG size)
{
	*mem = (PUCHAR) kmalloc(size, GFP_KERNEL);
	if (*mem) {
#ifdef VENDOR_FEATURE4_SUPPORT
		OS_NumOfMemAlloc++;
#endif /* VENDOR_FEATURE4_SUPPORT */

		return (NDIS_STATUS_SUCCESS);
	} else
		return (NDIS_STATUS_FAILURE);
}

/* pAd MUST allow to be NULL */
NDIS_STATUS os_free_mem(
	IN VOID *pReserved,
	IN PVOID mem)
{
	ASSERT(mem);
	kfree(mem);

#ifdef VENDOR_FEATURE4_SUPPORT
	OS_NumOfMemFree++;
#endif /* VENDOR_FEATURE4_SUPPORT */

	return (NDIS_STATUS_SUCCESS);
}

#if defined(RTMP_RBUS_SUPPORT) || defined (RTMP_FLASH_SUPPORT)
/* The flag "CONFIG_RALINK_FLASH_API" is used for APSoC Linux SDK */
#ifdef CONFIG_RALINK_FLASH_API

int32_t FlashRead(
	uint32_t *dst,
	uint32_t *src,
	uint32_t count);

int32_t FlashWrite(
	uint16_t *source,
	uint16_t *destination,
	uint32_t numBytes);
#define NVRAM_OFFSET                            0x30000
#if defined (CONFIG_RT2880_FLASH_32M)
#define RF_OFFSET                               0x1FE0000
#else
#ifdef RTMP_FLASH_SUPPORT
#define RF_OFFSET                               0x48000
#else
#define RF_OFFSET                               0x40000
#endif /* RTMP_FLASH_SUPPORT */
#endif

#else /* CONFIG_RALINK_FLASH_API */

#ifdef RA_MTD_RW_BY_NUM
#if defined (CONFIG_RT2880_FLASH_32M)
#define MTD_NUM_FACTORY 5
#else
#define MTD_NUM_FACTORY 2
#endif
extern int ra_mtd_write(
	int num,
	loff_t to,
	size_t len,
	const u_char *buf);

extern int ra_mtd_read(
	int num,
	loff_t from,
	size_t len,
	u_char *buf);
#else
extern int ra_mtd_write_nm(
	char *name,
	loff_t to,
	size_t len,
	const u_char *buf);

extern int ra_mtd_read_nm(
	char *name,
	loff_t from,
	size_t len,
	u_char *buf);
#endif

#endif /* CONFIG_RALINK_FLASH_API */

void RtmpFlashRead(
	UCHAR * p,
	ULONG a,
	ULONG b)
{
#ifdef CONFIG_RALINK_FLASH_API
	FlashRead((uint32_t *) p, (uint32_t *) a, (uint32_t) b);
#else
#ifdef RA_MTD_RW_BY_NUM
	ra_mtd_read(MTD_NUM_FACTORY, 0, (size_t) b, p);
#else
	ra_mtd_read_nm("Factory", a&0xFFFF, (size_t) b, p);
#endif
#endif /* CONFIG_RALINK_FLASH_API */
}

void RtmpFlashWrite(
	UCHAR * p,
	ULONG a,
	ULONG b)
{
#ifdef CONFIG_RALINK_FLASH_API
	FlashWrite((uint16_t *) p, (uint16_t *) a, (uint32_t) b);
#else
#ifdef RA_MTD_RW_BY_NUM
	ra_mtd_write(MTD_NUM_FACTORY, 0, (size_t) b, p);
#else
	ra_mtd_write_nm("Factory", a&0xFFFF, (size_t) b, p);
#endif
#endif /* CONFIG_RALINK_FLASH_API */
}
#endif /* defined(RTMP_RBUS_SUPPORT) || defined (RTMP_FLASH_SUPPORT) */

PNDIS_PACKET RtmpOSNetPktAlloc(
	IN VOID *pReserved,
	IN int size)
{
	struct sk_buff *skb;
	/* Add 2 more bytes for ip header alignment */
	skb = dev_alloc_skb(size + 2);
	if (skb != NULL)
		MEM_DBG_PKT_ALLOC_INC(skb);

	return ((PNDIS_PACKET) skb);
}

PNDIS_PACKET RTMP_AllocateFragPacketBuffer(
	IN VOID *pReserved,
	IN ULONG Length)
{
	struct sk_buff *pkt;

	pkt = dev_alloc_skb(Length);

	if (pkt == NULL) {
		DBGPRINT(RT_DEBUG_ERROR,
			 ("can't allocate frag rx %ld size packet\n", Length));
	}

	if (pkt) {
		MEM_DBG_PKT_ALLOC_INC(pkt);
		RTMP_SET_PACKET_SOURCE(OSPKT_TO_RTPKT(pkt), PKTSRC_NDIS);
	}

	return (PNDIS_PACKET) pkt;
}



/* the allocated NDIS PACKET must be freed via RTMPFreeNdisPacket() */
NDIS_STATUS RTMPAllocateNdisPacket(
	IN VOID *pReserved,
	OUT PNDIS_PACKET *ppPacket,
	IN PUCHAR pHeader,
	IN UINT HeaderLen,
	IN PUCHAR pData,
	IN UINT DataLen)
{
	PNDIS_PACKET pPacket;
	ASSERT(pData);
	ASSERT(DataLen);

	/* 1. Allocate a packet */
	pPacket =
	    (PNDIS_PACKET) dev_alloc_skb(HeaderLen + DataLen +
					 RTMP_PKT_TAIL_PADDING);

	if (pPacket == NULL) {
		*ppPacket = NULL;
#ifdef DEBUG
		printk(KERN_ERR "RTMPAllocateNdisPacket Fail\n\n");
#endif
		return NDIS_STATUS_FAILURE;
	}
	MEM_DBG_PKT_ALLOC_INC(pPacket);

	/* 2. clone the frame content */
	if (HeaderLen > 0)
		NdisMoveMemory(GET_OS_PKT_DATAPTR(pPacket), pHeader, HeaderLen);
	if (DataLen > 0)
		NdisMoveMemory(GET_OS_PKT_DATAPTR(pPacket) + HeaderLen, pData,
			       DataLen);

	/* 3. update length of packet */
	skb_put(GET_OS_PKT_TYPE(pPacket), HeaderLen + DataLen);

	RTMP_SET_PACKET_SOURCE(pPacket, PKTSRC_NDIS);
/*	printk(KERN_ERR "%s : pPacket = %p, len = %d\n", __FUNCTION__,
	pPacket, GET_OS_PKT_LEN(pPacket));*/
	*ppPacket = pPacket;
	return NDIS_STATUS_SUCCESS;
}

/*
  ========================================================================
  Description:
	This routine frees a miniport internally allocated NDIS_PACKET and its
	corresponding NDIS_BUFFER and allocated memory.
  ========================================================================
*/
VOID RTMPFreeNdisPacket(
	IN VOID *pReserved,
	IN PNDIS_PACKET pPacket)
{
	dev_kfree_skb_any(RTPKT_TO_OSPKT(pPacket));
	MEM_DBG_PKT_FREE_INC(pPacket);
}

/* IRQL = DISPATCH_LEVEL */
/* NOTE: we do have an assumption here, that Byte0 and Byte1
 * always reasid at the same scatter gather buffer
 */
NDIS_STATUS Sniff2BytesFromNdisBuffer(
	IN PNDIS_BUFFER pFirstBuffer,
	IN UCHAR DesiredOffset,
	OUT PUCHAR pByte0,
	OUT PUCHAR pByte1)
{
	*pByte0 = *(PUCHAR) (pFirstBuffer + DesiredOffset);
	*pByte1 = *(PUCHAR) (pFirstBuffer + DesiredOffset + 1);

	return NDIS_STATUS_SUCCESS;
}

void RTMP_QueryPacketInfo(
	IN PNDIS_PACKET pPacket,
	OUT PACKET_INFO * pPacketInfo,
	OUT PUCHAR * pSrcBufVA,
	OUT UINT * pSrcBufLen)
{
	pPacketInfo->BufferCount = 1;
	pPacketInfo->pFirstBuffer = (PNDIS_BUFFER) GET_OS_PKT_DATAPTR(pPacket);
	pPacketInfo->PhysicalBufferCount = 1;
	pPacketInfo->TotalPacketLength = GET_OS_PKT_LEN(pPacket);

	*pSrcBufVA = GET_OS_PKT_DATAPTR(pPacket);
	*pSrcBufLen = GET_OS_PKT_LEN(pPacket);
}


PNDIS_PACKET DuplicatePacket(
	IN PNET_DEV pNetDev,
	IN PNDIS_PACKET pPacket,
	IN UCHAR FromWhichBSSID)
{
	struct sk_buff *skb;
	PNDIS_PACKET pRetPacket = NULL;
	USHORT DataSize;
	UCHAR *pData;

	DataSize = (USHORT) GET_OS_PKT_LEN(pPacket);
	pData = (PUCHAR) GET_OS_PKT_DATAPTR(pPacket);

	skb = skb_clone(RTPKT_TO_OSPKT(pPacket), MEM_ALLOC_FLAG);
	if (skb) {
		MEM_DBG_PKT_ALLOC_INC(skb);
		skb->dev = pNetDev;	/*get_netdev_from_bssid(pAd, FromWhichBSSID); */
		pRetPacket = OSPKT_TO_RTPKT(skb);
	}

	return pRetPacket;

}

PNDIS_PACKET duplicate_pkt(
	IN PNET_DEV pNetDev,
	IN PUCHAR pHeader802_3,
	IN UINT HdrLen,
	IN PUCHAR pData,
	IN ULONG DataSize,
	IN UCHAR FromWhichBSSID)
{
	struct sk_buff *skb;
	PNDIS_PACKET pPacket = NULL;

	if ((skb =
	     __dev_alloc_skb(HdrLen + DataSize + 2, MEM_ALLOC_FLAG)) != NULL) {
		MEM_DBG_PKT_ALLOC_INC(skb);

		skb_reserve(skb, 2);
		NdisMoveMemory(skb->tail, pHeader802_3, HdrLen);
		skb_put(skb, HdrLen);
		NdisMoveMemory(skb->tail, pData, DataSize);
		skb_put(skb, DataSize);
		skb->dev = pNetDev;	/*get_netdev_from_bssid(pAd, FromWhichBSSID); */
		pPacket = OSPKT_TO_RTPKT(skb);
	}

	return pPacket;
}

#define TKIP_TX_MIC_SIZE		8
PNDIS_PACKET duplicate_pkt_with_TKIP_MIC(
	IN VOID *pReserved,
	IN PNDIS_PACKET pPacket)
{
	struct sk_buff *skb, *newskb;

	skb = RTPKT_TO_OSPKT(pPacket);
	if (skb_tailroom(skb) < TKIP_TX_MIC_SIZE) {
		/* alloc a new skb and copy the packet */
		newskb =
		    skb_copy_expand(skb, skb_headroom(skb), TKIP_TX_MIC_SIZE,
				    GFP_ATOMIC);

		dev_kfree_skb_any(skb);
		MEM_DBG_PKT_FREE_INC(skb);

		if (newskb == NULL) {
			DBGPRINT(RT_DEBUG_ERROR,
				 ("Extend Tx.MIC for packet failed!, dropping packet!\n"));
			return NULL;
		}
		skb = newskb;
		MEM_DBG_PKT_ALLOC_INC(skb);
	}

	return OSPKT_TO_RTPKT(skb);


}


/*
	========================================================================

	Routine Description:
		Send a L2 frame to upper daemon to trigger state machine

	Arguments:
		pAd - pointer to our pAdapter context

	Return Value:

	Note:

	========================================================================
*/
BOOLEAN RTMPL2FrameTxAction(
	IN VOID * pCtrlBkPtr,
	IN PNET_DEV pNetDev,
	IN RTMP_CB_8023_PACKET_ANNOUNCE _announce_802_3_packet,
	IN UCHAR apidx,
	IN PUCHAR pData,
	IN UINT32 data_len,
	IN	UCHAR			OpMode)
{
	struct sk_buff *skb = dev_alloc_skb(data_len + 2);

	if (!skb) {
		DBGPRINT(RT_DEBUG_ERROR,
			 ("%s : Error! Can't allocate a skb.\n", __FUNCTION__));
		return FALSE;
	}

	MEM_DBG_PKT_ALLOC_INC(skb);
	/*get_netdev_from_bssid(pAd, apidx)); */
	SET_OS_PKT_NETDEV(skb, pNetDev);

	/* 16 byte align the IP header */
	skb_reserve(skb, 2);

	/* Insert the frame content */
	NdisMoveMemory(GET_OS_PKT_DATAPTR(skb), pData, data_len);

	/* End this frame */
	skb_put(GET_OS_PKT_TYPE(skb), data_len);

	DBGPRINT(RT_DEBUG_TRACE, ("%s doen\n", __FUNCTION__));

	_announce_802_3_packet(pCtrlBkPtr, skb, OpMode);

	return TRUE;

}

PNDIS_PACKET ExpandPacket(
	IN VOID *pReserved,
	IN PNDIS_PACKET pPacket,
	IN UINT32 ext_head_len,
	IN UINT32 ext_tail_len)
{
	struct sk_buff *skb, *newskb;

	skb = RTPKT_TO_OSPKT(pPacket);
	if (skb_cloned(skb) || (skb_headroom(skb) < ext_head_len)
	    || (skb_tailroom(skb) < ext_tail_len)) {
		UINT32 head_len =
		    (skb_headroom(skb) <
		     ext_head_len) ? ext_head_len : skb_headroom(skb);
		UINT32 tail_len =
		    (skb_tailroom(skb) <
		     ext_tail_len) ? ext_tail_len : skb_tailroom(skb);

		/* alloc a new skb and copy the packet */
		newskb = skb_copy_expand(skb, head_len, tail_len, GFP_ATOMIC);

		dev_kfree_skb_any(skb);
		MEM_DBG_PKT_FREE_INC(skb);

		if (newskb == NULL) {
			DBGPRINT(RT_DEBUG_ERROR,
				 ("Extend Tx buffer for WPI failed!, dropping packet!\n"));
			return NULL;
		}
		skb = newskb;
		MEM_DBG_PKT_ALLOC_INC(skb);
	}

	return OSPKT_TO_RTPKT(skb);

}

PNDIS_PACKET ClonePacket(
	IN VOID *pReserved,
	IN PNDIS_PACKET pPacket,
	IN PUCHAR pData,
	IN ULONG DataSize)
{
	struct sk_buff *pRxPkt;
	struct sk_buff *pClonedPkt;

	ASSERT(pPacket);
	pRxPkt = RTPKT_TO_OSPKT(pPacket);

	/* clone the packet */
	pClonedPkt = skb_clone(pRxPkt, MEM_ALLOC_FLAG);

	if (pClonedPkt) {
		/* set the correct dataptr and data len */
		MEM_DBG_PKT_ALLOC_INC(pClonedPkt);
		pClonedPkt->dev = pRxPkt->dev;
		pClonedPkt->data = pData;
		pClonedPkt->len = DataSize;
		pClonedPkt->tail = pClonedPkt->data + pClonedPkt->len;
		ASSERT(DataSize < 1530);
	}
	return pClonedPkt;
}

VOID RtmpOsPktInit(
	IN PNDIS_PACKET pNetPkt,
	IN PNET_DEV pNetDev,
	IN UCHAR *pData,
	IN USHORT DataSize)
{
	PNDIS_PACKET pRxPkt;

	pRxPkt = RTPKT_TO_OSPKT(pNetPkt);

	SET_OS_PKT_NETDEV(pRxPkt, pNetDev);
	SET_OS_PKT_DATAPTR(pRxPkt, pData);
	SET_OS_PKT_LEN(pRxPkt, DataSize);
	SET_OS_PKT_DATATAIL(pRxPkt, pData, DataSize);
}


void wlan_802_11_to_802_3_packet(
	IN PNET_DEV pNetDev,
	IN UCHAR OpMode,
	IN USHORT VLAN_VID,
	IN USHORT VLAN_Priority,
	IN PNDIS_PACKET pRxPacket,
	IN UCHAR *pData,
	IN ULONG DataSize,
	IN PUCHAR pHeader802_3,
	IN UCHAR FromWhichBSSID,
	IN UCHAR *TPID)
{
	struct sk_buff *pOSPkt;

/*	ASSERT(pRxBlk->pRxPacket); */
	ASSERT(pHeader802_3);

	pOSPkt = RTPKT_TO_OSPKT(pRxPacket);

	/*get_netdev_from_bssid(pAd, FromWhichBSSID); */
	pOSPkt->dev = pNetDev;
	pOSPkt->data = pData;
	pOSPkt->len = DataSize;
	pOSPkt->tail = pOSPkt->data + pOSPkt->len;

	/* */
	/* copy 802.3 header */
	/* */
	/* */

#ifdef CONFIG_STA_SUPPORT
	RT_CONFIG_IF_OPMODE_ON_STA(OpMode)
	{
	    NdisMoveMemory(skb_push(pOSPkt, LENGTH_802_3), pHeader802_3,
			   LENGTH_802_3);

	}
#endif /* CONFIG_STA_SUPPORT */

}



void hex_dump(
	char *str,
	unsigned char *pSrcBufVA,
	unsigned int SrcBufLen)
{
#ifdef DBG
	unsigned char *pt;
	int x;

	if (RTDebugLevel < RT_DEBUG_TRACE)
		return;

	pt = pSrcBufVA;
	printk("%s: %p, len = %d\n", str, pSrcBufVA, SrcBufLen);
	for (x = 0; x < SrcBufLen; x++) {
		if (x % 16 == 0)
			printk("0x%04x : ", x);
		printk("%02x ", ((unsigned char)pt[x]));
		if (x % 16 == 15)
			printk("\n");
	}
	printk("\n");
#endif /* DBG */
}

#ifdef SYSTEM_LOG_SUPPORT
/*
	========================================================================

	Routine Description:
		Send log message through wireless event

		Support standard iw_event with IWEVCUSTOM. It is used below.

		iwreq_data.data.flags is used to store event_flag that is
		defined by user. iwreq_data.data.length is the length of the
		event log.

		The format of the event log is composed of the entry's MAC
		address and the desired log message (refer to
		pWirelessEventText).

			ex: 11:22:33:44:55:66 has associated successfully

		p.s. The requirement of Wireless Extension is v15 or newer.

	========================================================================
*/
VOID RtmpOsSendWirelessEvent(
	IN VOID *pAd,
	IN USHORT Event_flag,
	IN PUCHAR pAddr,
	IN UCHAR BssIdx,
	IN CHAR Rssi,
	IN RTMP_OS_SEND_WLAN_EVENT pFunc)
{
#if WIRELESS_EXT >= 15
	pFunc(pAd, Event_flag, pAddr, BssIdx, Rssi);

#else
	DBGPRINT(RT_DEBUG_ERROR,
		 ("%s : The Wireless Extension MUST be v15 or newer.\n",
		  __FUNCTION__));
#endif /* WIRELESS_EXT >= 15 */
}
#endif /* SYSTEM_LOG_SUPPORT */


#ifdef CONFIG_STA_SUPPORT
INT32 ralinkrate[] = {
2, 4, 11, 22,		/* CCK */
12, 18, 24, 36, 48, 72, 96, 108,	/* OFDM */
/* 20MHz, 800ns GI, MCS: 0 ~ 15 */
13, 26, 39, 52, 78, 104, 117, 130, 26, 52, 78, 104, 156, 208, 234, 260,
39, 78, 117, 156, 234, 312, 351, 390,	/* 20MHz, 800ns GI, MCS: 16 ~ 23 */
/* 40MHz, 800ns GI, MCS: 0 ~ 15 */
27, 54, 81, 108, 162, 216, 243, 270, 54, 108, 162, 216, 324, 432, 486, 540,
81, 162, 243, 324, 486, 648, 729, 810,	/* 40MHz, 800ns GI, MCS: 16 ~ 23 */
/* 20MHz, 400ns GI, MCS: 0 ~ 15 */
14, 29, 43, 57, 87, 115, 130, 144, 29, 59, 87, 115, 173, 230, 260, 288,
43, 87, 130, 173, 260, 317, 390, 433,	/* 20MHz, 400ns GI, MCS: 16 ~ 23 */
/* 40MHz, 400ns GI, MCS: 0 ~ 15 */
30, 60, 90, 120, 180, 240, 270, 300, 60, 120, 180, 240, 360, 480, 540, 600,
90, 180, 270, 360, 540, 720, 810, 900};	/* 40MHz, 400ns GI, MCS: 16 ~ 23 */

UINT32 RT_RateSize = sizeof (ralinkrate);

void send_monitor_packets(IN PNET_DEV pNetDev,
			  IN PNDIS_PACKET pRxPacket,
			  IN PHEADER_802_11 pHeader,
			  IN UCHAR * pData,
			  IN USHORT DataSize,
			  IN UCHAR L2PAD,
			  IN UCHAR PHYMODE,
			  IN UCHAR BW,
			  IN UCHAR ShortGI,
			  IN UCHAR MCS,
			  IN UCHAR AMPDU,
			  IN UCHAR STBC,
			  IN UCHAR RSSI1,
			  IN UCHAR BssMonitorFlag11n,
			  IN UCHAR * pDevName,
			  IN UCHAR Channel,
			  IN UCHAR CentralChannel,
			  IN UINT32 MaxRssi) {
	struct sk_buff *pOSPkt;
	wlan_ng_prism2_header *ph;
#ifdef MONITOR_FLAG_11N_SNIFFER_SUPPORT
	ETHEREAL_RADIO h,
	*ph_11n33;		/* for new 11n sniffer format */
#endif /* MONITOR_FLAG_11N_SNIFFER_SUPPORT */
	int rate_index = 0;
	USHORT header_len = 0;
	UCHAR temp_header[40] = {
	0};

	MEM_DBG_PKT_FREE_INC(pRxPacket);


	pOSPkt = RTPKT_TO_OSPKT(pRxPacket);	/*pRxBlk->pRxPacket); */
	pOSPkt->dev = pNetDev;	/*get_netdev_from_bssid(pAd, BSS0); */
	if (pHeader->FC.Type == BTYPE_DATA) {
		DataSize -= LENGTH_802_11;
		if ((pHeader->FC.ToDs == 1) && (pHeader->FC.FrDs == 1))
			header_len = LENGTH_802_11_WITH_ADDR4;
		else
			header_len = LENGTH_802_11;

		/* QOS */
		if (pHeader->FC.SubType & 0x08) {
			header_len += 2;
			/* Data skip QOS contorl field */
			DataSize -= 2;
		}

		/* Order bit: A-Ralink or HTC+ */
		if (pHeader->FC.Order) {
			header_len += 4;
			/* Data skip HTC contorl field */
			DataSize -= 4;
		}

		/* Copy Header */
		if (header_len <= 40)
			NdisMoveMemory(temp_header, pData, header_len);

		/* skip HW padding */
		if (L2PAD)
			pData += (header_len + 2);
		else
			pData += header_len;
	}

	/*end if */
	if (DataSize < pOSPkt->len) {
		skb_trim(pOSPkt, DataSize);
	} else {
		skb_put(pOSPkt, (DataSize - pOSPkt->len));
	}			/*end if */

	if ((pData - pOSPkt->data) > 0) {
		skb_put(pOSPkt, (pData - pOSPkt->data));
		skb_pull(pOSPkt, (pData - pOSPkt->data));
	}
	/*end if */
	if (skb_headroom(pOSPkt) <
	    (sizeof (wlan_ng_prism2_header) + header_len)) {
		if (pskb_expand_head
		    (pOSPkt, (sizeof (wlan_ng_prism2_header) + header_len), 0,
		     GFP_ATOMIC)) {
			DBGPRINT(RT_DEBUG_ERROR,
				 ("%s : Reallocate header size of sk_buff fail!\n",
				  __FUNCTION__));
			goto err_free_sk_buff;
		}		/*end if */
	}
	/*end if */
	if (header_len > 0)
		NdisMoveMemory(skb_push(pOSPkt, header_len), temp_header,
			       header_len);

#ifdef MONITOR_FLAG_11N_SNIFFER_SUPPORT
	if (BssMonitorFlag11n == 0)
#endif /* MONITOR_FLAG_11N_SNIFFER_SUPPORT */
	{
		ph = (wlan_ng_prism2_header *) skb_push(pOSPkt,
							sizeof
							(wlan_ng_prism2_header));
		NdisZeroMemory(ph, sizeof (wlan_ng_prism2_header));

		ph->msgcode = DIDmsg_lnxind_wlansniffrm;
		ph->msglen = sizeof (wlan_ng_prism2_header);
		strcpy((PSTRING) ph->devname, (PSTRING) pDevName);

		ph->hosttime.did = DIDmsg_lnxind_wlansniffrm_hosttime;
		ph->hosttime.status = 0;
		ph->hosttime.len = 4;
		ph->hosttime.data = jiffies;

		ph->mactime.did = DIDmsg_lnxind_wlansniffrm_mactime;
		ph->mactime.status = 0;
		ph->mactime.len = 0;
		ph->mactime.data = 0;

		ph->istx.did = DIDmsg_lnxind_wlansniffrm_istx;
		ph->istx.status = 0;
		ph->istx.len = 0;
		ph->istx.data = 0;

		ph->channel.did = DIDmsg_lnxind_wlansniffrm_channel;
		ph->channel.status = 0;
		ph->channel.len = 4;

		ph->channel.data = (u_int32_t) Channel;

		ph->rssi.did = DIDmsg_lnxind_wlansniffrm_rssi;
		ph->rssi.status = 0;
		ph->rssi.len = 4;
		ph->rssi.data = MaxRssi;
		ph->signal.did = DIDmsg_lnxind_wlansniffrm_signal;
		ph->signal.status = 0;
		ph->signal.len = 4;
		ph->signal.data = 0;	/*rssi + noise; */

		ph->noise.did = DIDmsg_lnxind_wlansniffrm_noise;
		ph->noise.status = 0;
		ph->noise.len = 4;
		ph->noise.data = 0;

#ifdef DOT11_N_SUPPORT
		if (PHYMODE >= MODE_HTMIX) {
			rate_index =
			    12 + ((UCHAR) BW * 24) + ((UCHAR) ShortGI * 48) +
			    ((UCHAR) MCS);
		} else
#endif /* DOT11_N_SUPPORT */
		if (PHYMODE == MODE_OFDM)
			rate_index = (UCHAR) (MCS) + 4;
		else
			rate_index = (UCHAR) (MCS);

		if (rate_index < 0)
			rate_index = 0;
		if (rate_index >=
		    (sizeof (ralinkrate) / sizeof (ralinkrate[0])))
			rate_index =
			    (sizeof (ralinkrate) / sizeof (ralinkrate[0])) - 1;

		ph->rate.did = DIDmsg_lnxind_wlansniffrm_rate;
		ph->rate.status = 0;
		ph->rate.len = 4;
		/* real rate = ralinkrate[rate_index] / 2 */
		ph->rate.data = ralinkrate[rate_index];

		ph->frmlen.did = DIDmsg_lnxind_wlansniffrm_frmlen;
		ph->frmlen.status = 0;
		ph->frmlen.len = 4;
		ph->frmlen.data = (u_int32_t) DataSize;
	}
#ifdef MONITOR_FLAG_11N_SNIFFER_SUPPORT
	else {
		ph_11n33 = &h;
		NdisZeroMemory((unsigned char *)ph_11n33,
			       sizeof (ETHEREAL_RADIO));

		/*802.11n fields */
		if (MCS > 15)
			ph_11n33->Flag_80211n |= WIRESHARK_11N_FLAG_3x3;

		if (PHYMODE == MODE_HTGREENFIELD)
			ph_11n33->Flag_80211n |= WIRESHARK_11N_FLAG_GF;

		if (BW == 1) {
			ph_11n33->Flag_80211n |= WIRESHARK_11N_FLAG_BW40;
		} else if (Channel < CentralChannel) {
			ph_11n33->Flag_80211n |= WIRESHARK_11N_FLAG_BW20U;
		} else if (Channel > CentralChannel) {
			ph_11n33->Flag_80211n |= WIRESHARK_11N_FLAG_BW20D;
		} else {
			ph_11n33->Flag_80211n |=
			    (WIRESHARK_11N_FLAG_BW20U |
			     WIRESHARK_11N_FLAG_BW20D);
		}

		if (ShortGI == 1)
			ph_11n33->Flag_80211n |= WIRESHARK_11N_FLAG_SGI;

		/* RXD_STRUC   PRT28XX_RXD_STRUC pRxD = &(pRxBlk->RxD); */
		if (AMPDU)
			ph_11n33->Flag_80211n |= WIRESHARK_11N_FLAG_AMPDU;

		if (STBC)
			ph_11n33->Flag_80211n |= WIRESHARK_11N_FLAG_STBC;

		ph_11n33->signal_level = (UCHAR) RSSI1;

		/* data_rate is the rate index in the wireshark rate table */
		if (PHYMODE >= MODE_HTMIX) {
			if (MCS == 32) {
				if (ShortGI)
					ph_11n33->data_rate = 16;
				else
					ph_11n33->data_rate = 4;
			} else if (MCS > 15)
				ph_11n33->data_rate =
				    (16 * 4 + ((UCHAR) BW * 16) +
				     ((UCHAR) ShortGI * 32) + ((UCHAR) MCS));
			else
				ph_11n33->data_rate =
				    16 + ((UCHAR) BW * 16) +
				    ((UCHAR) ShortGI * 32) + ((UCHAR) MCS);
		} else if (PHYMODE == MODE_OFDM)
			ph_11n33->data_rate = (UCHAR) (MCS) + 4;
		else
			ph_11n33->data_rate = (UCHAR) (MCS);

		/*channel field */
		ph_11n33->channel = (UCHAR) Channel;

		NdisMoveMemory(skb_put(pOSPkt, sizeof (ETHEREAL_RADIO)),
			       (UCHAR *) ph_11n33, sizeof (ETHEREAL_RADIO));
	}
#endif /* MONITOR_FLAG_11N_SNIFFER_SUPPORT */

	pOSPkt->pkt_type = PACKET_OTHERHOST;
	pOSPkt->protocol = eth_type_trans(pOSPkt, pOSPkt->dev);
	pOSPkt->ip_summed = CHECKSUM_NONE;
	netif_rx(pOSPkt);

	return;

      err_free_sk_buff:
	RELEASE_NDIS_PACKET(NULL, pRxPacket, NDIS_STATUS_FAILURE);
	return;

}
#endif /* CONFIG_STA_SUPPORT */


/*******************************************************************************

	File open/close related functions.

 *******************************************************************************/
RTMP_OS_FD RtmpOSFileOpen(char *pPath,
			  int flag,
			  int mode) {
	struct file *filePtr;

	if (flag == RTMP_FILE_RDONLY)
		flag = O_RDONLY;
	else if (flag == RTMP_FILE_WRONLY)
		flag = O_WRONLY;
	else if (flag == RTMP_FILE_CREAT)
		flag = O_CREAT;
	else if (flag == RTMP_FILE_TRUNC)
		flag = O_TRUNC;

	filePtr = filp_open(pPath, flag, 0);
	if (IS_ERR(filePtr)) {
		DBGPRINT(RT_DEBUG_ERROR,
			 ("%s(): Error %ld opening %s\n", __FUNCTION__,
			  -PTR_ERR(filePtr), pPath));
	}

	return (RTMP_OS_FD) filePtr;
}

int RtmpOSFileClose(RTMP_OS_FD osfd) {
	filp_close(osfd, NULL);
	return 0;
}

void RtmpOSFileSeek(RTMP_OS_FD osfd,
		    int offset) {
	osfd->f_pos = offset;
}

int RtmpOSFileRead(RTMP_OS_FD osfd,
		     char *pDataPtr, int readLen) {
	/* The object must have a read method */
	if (osfd->f_op && osfd->f_op->read) {
		return osfd->f_op->read(osfd, pDataPtr, readLen, &osfd->f_pos);
	} else {
		DBGPRINT(RT_DEBUG_ERROR, ("no file read method\n"));
		return -1;
	}
}

int RtmpOSFileWrite(RTMP_OS_FD osfd,
		    char *pDataPtr, int writeLen) {
	return osfd->f_op->write(osfd,
				 pDataPtr,
				 (
	size_t) writeLen,
				 &osfd->f_pos);
}

static inline void __RtmpOSFSInfoChange(OS_FS_INFO * pOSFSInfo,
					BOOLEAN bSet) {
	if (bSet) {
		/* Save uid and gid used for filesystem access. */
		/* Set user and group to 0 (root) */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,29)
		pOSFSInfo->fsuid = current->fsuid;
		pOSFSInfo->fsgid = current->fsgid;
		current->fsuid = current->fsgid = 0;
#else
		pOSFSInfo->fsuid = current_fsuid();
		pOSFSInfo->fsgid = current_fsgid();
#endif
		pOSFSInfo->fs = get_fs();
		set_fs(KERNEL_DS);
	} else {
		set_fs(pOSFSInfo->fs);
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,29)
		current->fsuid = pOSFSInfo->fsuid;
		current->fsgid = pOSFSInfo->fsgid;
#endif
	}
}

/*******************************************************************************

	Task create/management/kill related functions.

 *******************************************************************************/
static inline NDIS_STATUS __RtmpOSTaskKill(IN OS_TASK *pTask) {
/*	RTMP_ADAPTER *pAd; */
	int ret = NDIS_STATUS_FAILURE;

/*	pAd = (RTMP_ADAPTER *)pTask->priv; */

#ifdef KTHREAD_SUPPORT
	if (pTask->kthread_task) {
		kthread_stop(pTask->kthread_task);
		ret = NDIS_STATUS_SUCCESS;
	}
#else
	CHECK_PID_LEGALITY(pTask->taskPID) {
		DBGPRINT(RT_DEBUG_TRACE,
			 ("Terminate the task(%s) with pid(%d)!\n",
			  pTask->taskName, GET_PID_NUMBER(pTask->taskPID)));
		mb();
		pTask->task_killed = 1;
		mb();
		ret = KILL_THREAD_PID(pTask->taskPID, SIGTERM, 1);
		if (ret) {
			printk(KERN_WARNING
			       "kill task(%s) with pid(%d) failed(retVal=%d)!\n",
			       pTask->taskName, GET_PID_NUMBER(pTask->taskPID),
			       ret);
		} else {
			wait_for_completion(&pTask->taskComplete);
			pTask->taskPID = THREAD_PID_INIT_VALUE;
			pTask->task_killed = 0;
			RTMP_SEM_EVENT_DESTORY(&pTask->taskSema);
			ret = NDIS_STATUS_SUCCESS;
		}
	}
#endif

	return ret;

}

static inline INT __RtmpOSTaskNotifyToExit(IN OS_TASK *pTask) {

#ifndef KTHREAD_SUPPORT
	pTask->taskPID = THREAD_PID_INIT_VALUE;
	complete_and_exit(&pTask->taskComplete, 0);
#endif

	return 0;
}

static inline void __RtmpOSTaskCustomize(IN OS_TASK *pTask) {

#ifndef KTHREAD_SUPPORT

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0)
	daemonize((PSTRING) & pTask->taskName[0] /*"%s",pAd->net_dev->name */ );

	allow_signal(SIGTERM);
	allow_signal(SIGKILL);
	current->flags |= PF_NOFREEZE;
#else
	unsigned long flags;

	daemonize();
	reparent_to_init();
	strcpy(current->comm, &pTask->taskName[0]);

	siginitsetinv(&current->blocked, sigmask(SIGTERM) | sigmask(SIGKILL));

	/* Allow interception of SIGKILL only
	 * Don't allow other signals to interrupt the transmission */
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,4,22)
	spin_lock_irqsave(&current->sigmask_lock, flags);
	flush_signals(current);
	recalc_sigpending(current);
	spin_unlock_irqrestore(&current->sigmask_lock, flags);
#endif
#endif

	RTMP_GET_OS_PID(pTask->taskPID, current->pid);

	/* signal that we've started the thread */
	complete(&pTask->taskComplete);

#endif
}

static inline NDIS_STATUS __RtmpOSTaskAttach(IN OS_TASK *pTask,
					     IN RTMP_OS_TASK_CALLBACK fn,
					     IN ULONG arg) {
	NDIS_STATUS status = NDIS_STATUS_SUCCESS;
#ifndef KTHREAD_SUPPORT
	pid_t pid_number = -1;
#endif /* KTHREAD_SUPPORT */

#ifdef KTHREAD_SUPPORT
	pTask->task_killed = 0;
	pTask->kthread_task = NULL;
	pTask->kthread_task =
	    kthread_run((cast_fn) fn, (void *)arg, pTask->taskName);
	if (IS_ERR(pTask->kthread_task))
		status = NDIS_STATUS_FAILURE;
#else
	pid_number =
	    kernel_thread((cast_fn) fn, (void *)arg, RTMP_OS_MGMT_TASK_FLAGS);
	if (pid_number < 0) {
		DBGPRINT(RT_DEBUG_ERROR,
			 ("Attach task(%s) failed!\n", pTask->taskName));
		status = NDIS_STATUS_FAILURE;
	} else {
		/* Wait for the thread to start */
		wait_for_completion(&pTask->taskComplete);
		status = NDIS_STATUS_SUCCESS;
	}
#endif
	return status;
}

static inline NDIS_STATUS __RtmpOSTaskInit(IN OS_TASK *pTask,
					   IN PSTRING pTaskName,
					   IN VOID *pPriv,
					   IN LIST_HEADER *pSemList) {
	int len;

	ASSERT(pTask);

#ifndef KTHREAD_SUPPORT
	NdisZeroMemory((PUCHAR) (pTask), sizeof (OS_TASK));
#endif

	len = strlen(pTaskName);
	len =
	    len >
	    (RTMP_OS_TASK_NAME_LEN - 1) ? (RTMP_OS_TASK_NAME_LEN - 1) : len;
	NdisMoveMemory(&pTask->taskName[0], pTaskName, len);
	pTask->priv = pPriv;

#ifndef KTHREAD_SUPPORT
	RTMP_SEM_EVENT_INIT_LOCKED(&(pTask->taskSema), pSemList);
	pTask->taskPID = THREAD_PID_INIT_VALUE;
	init_completion(&pTask->taskComplete);
#endif

#ifdef KTHREAD_SUPPORT
	init_waitqueue_head(&(pTask->kthread_q));
#endif /* KTHREAD_SUPPORT */

	return NDIS_STATUS_SUCCESS;
}

BOOLEAN __RtmpOSTaskWait(IN VOID *pReserved,
			 IN OS_TASK *pTask,
			 IN INT32 *pStatus) {
#ifdef KTHREAD_SUPPORT
	RTMP_WAIT_EVENT_INTERRUPTIBLE((*pStatus), pTask);

	if ((pTask->task_killed == 1) || ((*pStatus) != 0))
		return FALSE;
#else

	RTMP_SEM_EVENT_WAIT(&(pTask->taskSema), (*pStatus));

	/* unlock the device pointers */
	if ((*pStatus) != 0) {
/*		RTMP_SET_FLAG_(*pFlags, fRTMP_ADAPTER_HALT_IN_PROGRESS); */
		return FALSE;
	}
#endif /* KTHREAD_SUPPORT */

	return TRUE;
}


#if LINUX_VERSION_CODE <= 0x20402	/* Red Hat 7.1 */
struct net_device *alloc_netdev(
	int sizeof_priv,
	const char *mask,
	void (*setup) (struct net_device *))
{
	struct net_device *dev;
	INT alloc_size;

	/* ensure 32-byte alignment of the private area */
	alloc_size = sizeof (*dev) + sizeof_priv + 31;

	dev = (struct net_device *)kmalloc(alloc_size, GFP_KERNEL);
	if (dev == NULL) {
		DBGPRINT(RT_DEBUG_ERROR,
			 ("alloc_netdev: Unable to allocate device memory.\n"));
		return NULL;
	}

	memset(dev, 0, alloc_size);

	if (sizeof_priv)
		dev->priv = (void *)(((long)(dev + 1) + 31) & ~31);

	setup(dev);
	strcpy(dev->name, mask);

	return dev;
}
#endif /* LINUX_VERSION_CODE */

static UINT32 RtmpOSWirelessEventTranslate(IN UINT32 eventType) {
	switch (eventType) {
	case RT_WLAN_EVENT_CUSTOM:
		eventType = IWEVCUSTOM;
		break;

	case RT_WLAN_EVENT_CGIWAP:
		eventType = SIOCGIWAP;
		break;

#if WIRELESS_EXT > 17
	case RT_WLAN_EVENT_ASSOC_REQ_IE:
		eventType = IWEVASSOCREQIE;
		break;
#endif /* WIRELESS_EXT */

#if WIRELESS_EXT >= 14
	case RT_WLAN_EVENT_SCAN:
		eventType = SIOCGIWSCAN;
		break;
#endif /* WIRELESS_EXT */

	case RT_WLAN_EVENT_EXPIRED:
		eventType = IWEVEXPIRED;
		break;

	default:
		printk("Unknown event: %d\n", eventType);
		break;
	}

	return eventType;
}

int RtmpOSWrielessEventSend(IN PNET_DEV pNetDev,
			    IN UINT32 eventType,
			    IN INT flags,
			    IN PUCHAR pSrcMac,
			    IN PUCHAR pData,
			    IN UINT32 dataLen) {
	union iwreq_data wrqu;

	/* translate event type */
	eventType = RtmpOSWirelessEventTranslate(eventType);

	memset(&wrqu, 0, sizeof (wrqu));

	if (flags > -1)
		wrqu.data.flags = flags;

	if (pSrcMac)
		memcpy(wrqu.ap_addr.sa_data, pSrcMac, MAC_ADDR_LEN);

	if ((pData != NULL) && (dataLen > 0))
		wrqu.data.length = dataLen;

	wireless_send_event(pNetDev, eventType, &wrqu, (char *)pData);
	return 0;
}

int RtmpOSWrielessEventSendExt(IN PNET_DEV pNetDev,
			       IN UINT32 eventType,
			       IN INT flags,
			       IN PUCHAR pSrcMac,
			       IN PUCHAR pData,
			       IN UINT32 dataLen,
			       IN UINT32 family) {
	union iwreq_data wrqu;

	/* translate event type */
	eventType = RtmpOSWirelessEventTranslate(eventType);

	/* translate event type */
	memset(&wrqu, 0, sizeof (wrqu));

	if (flags > -1)
		wrqu.data.flags = flags;

	if (pSrcMac)
		memcpy(wrqu.ap_addr.sa_data, pSrcMac, MAC_ADDR_LEN);

	if ((pData != NULL) && (dataLen > 0))
		wrqu.data.length = dataLen;

	wrqu.addr.sa_family = family;

	wireless_send_event(pNetDev, eventType, &wrqu, (char *)pData);
	return 0;
}

int RtmpOSNetDevAddrSet(IN UCHAR OpMode,
			IN PNET_DEV pNetDev,
			IN PUCHAR pMacAddr,
			IN PUCHAR dev_name) {
	struct net_device *net_dev;
/*	RTMP_ADAPTER *pAd; */

	net_dev = pNetDev;
/*	GET_PAD_FROM_NET_DEV(pAd, net_dev); */

#ifdef CONFIG_STA_SUPPORT
	/* work-around for the SuSE due to it has it's own interface name management system. */
	RT_CONFIG_IF_OPMODE_ON_STA(OpMode) {
/*		NdisZeroMemory(pAd->StaCfg.dev_name, 16); */
/*		NdisMoveMemory(pAd->StaCfg.dev_name, net_dev->name, strlen(net_dev->name)); */
		NdisZeroMemory(dev_name, 16);
		NdisMoveMemory(dev_name, net_dev->name, strlen(net_dev->name));
	}
#endif /* CONFIG_STA_SUPPORT */

	NdisMoveMemory(net_dev->dev_addr, pMacAddr, 6);

	return 0;
}

/*
  *	Assign the network dev name for created Ralink WiFi interface.
  */
static int RtmpOSNetDevRequestName(IN INT32 MC_RowID,
				   IN UINT32 *pIoctlIF,
				   IN PNET_DEV dev,
				   IN PSTRING pPrefixStr,
				   IN INT devIdx) {
	PNET_DEV existNetDev;
	STRING suffixName[IFNAMSIZ];
	STRING desiredName[IFNAMSIZ];
	int ifNameIdx,
	 prefixLen,
	 slotNameLen;
	int Status;

	prefixLen = strlen(pPrefixStr);
	ASSERT((prefixLen < IFNAMSIZ));

	for (ifNameIdx = devIdx; ifNameIdx < 32; ifNameIdx++) {
		memset(suffixName, 0, IFNAMSIZ);
		memset(desiredName, 0, IFNAMSIZ);
		strncpy(&desiredName[0], pPrefixStr, prefixLen);

#ifdef MULTIPLE_CARD_SUPPORT
		if (MC_RowID >= 0)
			sprintf(suffixName, "%02d_%d", MC_RowID, ifNameIdx);
		else
#endif /* MULTIPLE_CARD_SUPPORT */
			sprintf(suffixName, "%d", ifNameIdx);

		slotNameLen = strlen(suffixName);
		ASSERT(((slotNameLen + prefixLen) < IFNAMSIZ));
		strcat(desiredName, suffixName);

		existNetDev = RtmpOSNetDevGetByName(dev, &desiredName[0]);
		if (existNetDev == NULL)
			break;
		else
			RtmpOSNetDeviceRefPut(existNetDev);
	}

	if (ifNameIdx < 32) {
#ifdef HOSTAPD_SUPPORT
		*pIoctlIF = ifNameIdx;
#endif /*HOSTAPD_SUPPORT */
		strcpy(&dev->name[0], &desiredName[0]);
		Status = NDIS_STATUS_SUCCESS;
	} else {
		DBGPRINT(RT_DEBUG_ERROR,
			 ("Cannot request DevName with preifx(%s) and in range(0~32) as suffix from OS!\n",
			  pPrefixStr));
		Status = NDIS_STATUS_FAILURE;
	}

	return Status;
}

void RtmpOSNetDevClose(IN PNET_DEV pNetDev) {
	dev_close(pNetDev);
}

void RtmpOSNetDevFree(PNET_DEV pNetDev) {
	ASSERT(pNetDev);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0)
	free_netdev(pNetDev);
#else
	kfree(pNetDev);
#endif
}

INT RtmpOSNetDevAlloc(IN PNET_DEV *new_dev_p,
		      IN UINT32 privDataSize) {
	/* assign it as null first. */
	*new_dev_p = NULL;

	DBGPRINT(RT_DEBUG_TRACE,
		 ("Allocate a net device with private data size=%d!\n",
		  privDataSize));
#if LINUX_VERSION_CODE <= 0x20402	/* Red Hat 7.1 */
	*new_dev_p = alloc_netdev(privDataSize, "eth%d", ether_setup);
#else
	*new_dev_p = alloc_etherdev(privDataSize);
#endif /* LINUX_VERSION_CODE */

	if (*new_dev_p)
		return NDIS_STATUS_SUCCESS;
	else
		return NDIS_STATUS_FAILURE;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,31)
INT RtmpOSNetDevOpsAlloc(IN PVOID *pNetDevOps) {
	*pNetDevOps = (PVOID) vmalloc(sizeof (struct net_device_ops));
	if (*pNetDevOps) {
		NdisZeroMemory(*pNetDevOps, sizeof (struct net_device_ops));
		return NDIS_STATUS_SUCCESS;
	} else {
		return NDIS_STATUS_FAILURE;
	}
}
#endif
PNET_DEV RtmpOSNetDevGetByName(PNET_DEV pNetDev,
			       PSTRING pDevName) {
	PNET_DEV pTargetNetDev = NULL;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0)

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,24)
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,26)
	pTargetNetDev = dev_get_by_name(dev_net(pNetDev), pDevName);
#else
	ASSERT(pNetDev);
	pTargetNetDev = dev_get_by_name(pNetDev->nd_net, pDevName);
#endif
#else
	pTargetNetDev = dev_get_by_name(pDevName);
#endif /* KERNEL_VERSION(2,6,24) */

#else
	int devNameLen;

	devNameLen = strlen(pDevName);
	ASSERT((devNameLen <= IFNAMSIZ));

	for (pTargetNetDev = dev_base; pTargetNetDev != NULL;
	     pTargetNetDev = pTargetNetDev->next) {
		if (strncmp(pTargetNetDev->name, pDevName, devNameLen) == 0)
			break;
	}
#endif /* KERNEL_VERSION(2,5,0) */

	return pTargetNetDev;
}

void RtmpOSNetDeviceRefPut(PNET_DEV pNetDev) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0)
	/* 
	   every time dev_get_by_name is called, and it has returned a valid struct 
	   net_device*, dev_put should be called afterwards, because otherwise the 
	   machine hangs when the device is unregistered (since dev->refcnt > 1).
	 */
	if (pNetDev)
		dev_put(pNetDev);
#endif /* LINUX_VERSION_CODE */
}

INT RtmpOSNetDevDestory(IN VOID *pReserved,
			IN PNET_DEV pNetDev) {

	/* TODO: Need to fix this */
	printk("WARNING: This function(%s) not implement yet!!!\n",
	       __FUNCTION__);
	return 0;
}

void RtmpOSNetDevDetach(PNET_DEV pNetDev) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,31)
	struct net_device_ops *pNetDevOps = (struct net_device_ops *)pNetDev->netdev_ops;
#endif

	unregister_netdev(pNetDev);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,31)
	vfree(pNetDevOps);
#endif
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,18)
static void RALINK_ET_DrvInfoGet(IN struct net_device *pDev,
				 IN struct ethtool_drvinfo *pInfo) {
	strcpy(pInfo->driver, "RALINK WLAN");


	sprintf(pInfo->bus_info,
		"CSR 0x%lx",
		pDev->base_addr);
} static struct ethtool_ops RALINK_Ethtool_Ops = {
.get_drvinfo = RALINK_ET_DrvInfoGet,};
#endif /* LINUX_VERSION_CODE */

int RtmpOSNetDevAttach(IN UCHAR OpMode,
		       IN PNET_DEV pNetDev,
		       IN RTMP_OS_NETDEV_OP_HOOK *pDevOpHook) {
	int ret,
	 rtnl_locked = FALSE;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,31)
	struct net_device_ops *pNetDevOps = (struct net_device_ops *)pNetDev->netdev_ops;
#endif

	DBGPRINT(RT_DEBUG_TRACE, ("RtmpOSNetDevAttach()--->\n"));

	/* If we need hook some callback function to the net device structrue, now do it. */
	if (pDevOpHook) {
/*		PRTMP_ADAPTER pAd = NULL; */

/*		GET_PAD_FROM_NET_DEV(pAd, pNetDev); */

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,31)
		pNetDevOps->ndo_open = pDevOpHook->open;
		pNetDevOps->ndo_stop = pDevOpHook->stop;
		pNetDevOps->ndo_start_xmit =
		    (HARD_START_XMIT_FUNC) (pDevOpHook->xmit);
		pNetDevOps->ndo_do_ioctl = pDevOpHook->ioctl;
#else
		pNetDev->open = pDevOpHook->open;
		pNetDev->stop = pDevOpHook->stop;
		pNetDev->hard_start_xmit =
		    (HARD_START_XMIT_FUNC) (pDevOpHook->xmit);
		pNetDev->do_ioctl = pDevOpHook->ioctl;
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,18)
		pNetDev->ethtool_ops = &RALINK_Ethtool_Ops;
#endif

		/* if you don't implement get_stats, just leave the callback function as NULL, a dummy 
		   function will make kernel panic.
		 */
		if (pDevOpHook->get_stats)
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,31)
			pNetDevOps->ndo_get_stats = pDevOpHook->get_stats;
#else
			pNetDev->get_stats = pDevOpHook->get_stats;
#endif

		/* OS specific flags, here we used to indicate if we are virtual interface */
		pNetDev->priv_flags = pDevOpHook->priv_flags;

#if (WIRELESS_EXT < 21) && (WIRELESS_EXT >= 12)
/*		pNetDev->get_wireless_stats = rt28xx_get_wireless_stats; */
		pNetDev->get_wireless_stats = pDevOpHook->get_wstats;
#endif

#ifdef CONFIG_STA_SUPPORT
#if WIRELESS_EXT >= 12
		if (OpMode == OPMODE_STA) {
/*			pNetDev->wireless_handlers = &rt28xx_iw_handler_def; */
			pNetDev->wireless_handlers = pDevOpHook->iw_handler;
		}
#endif /*WIRELESS_EXT >= 12 */
#endif /* CONFIG_STA_SUPPORT */

#ifdef CONFIG_APSTA_MIXED_SUPPORT
#if WIRELESS_EXT >= 12
		if (OpMode == OPMODE_AP) {
/*			pNetDev->wireless_handlers = &rt28xx_ap_iw_handler_def; */
			pNetDev->wireless_handlers = pDevOpHook->iw_handler;
		}
#endif /*WIRELESS_EXT >= 12 */
#endif /* CONFIG_APSTA_MIXED_SUPPORT */

		/* copy the net device mac address to the net_device structure. */
		NdisMoveMemory(pNetDev->dev_addr, &pDevOpHook->devAddr[0],
			       MAC_ADDR_LEN);

		rtnl_locked = pDevOpHook->needProtcted;

	}
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,24)
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,31)
	pNetDevOps->ndo_validate_addr = NULL;
	/*pNetDev->netdev_ops = ops; */
#else
	pNetDev->validate_addr = NULL;
#endif
#endif

	if (rtnl_locked)
		ret = register_netdevice(pNetDev);
	else
		ret = register_netdev(pNetDev);

	netif_stop_queue(pNetDev);

	DBGPRINT(RT_DEBUG_TRACE, ("<---RtmpOSNetDevAttach(), ret=%d\n", ret));
	if (ret == 0)
		return NDIS_STATUS_SUCCESS;
	else
		return NDIS_STATUS_FAILURE;
}

PNET_DEV RtmpOSNetDevCreate(IN INT32 MC_RowID,
			    IN UINT32 *pIoctlIF,
			    IN INT devType,
			    IN INT devNum,
			    IN INT privMemSize,
			    IN PSTRING pNamePrefix) {
	struct net_device *pNetDev = NULL;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,31)
	struct net_device_ops *pNetDevOps = NULL;
#endif
	int status;

	/* allocate a new network device */
	status = RtmpOSNetDevAlloc(&pNetDev, 0 /*privMemSize */ );
	if (status != NDIS_STATUS_SUCCESS) {
		/* allocation fail, exit */
		DBGPRINT(RT_DEBUG_ERROR,
			 ("Allocate network device fail (%s)...\n",
			  pNamePrefix));
		return NULL;
	}
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,31)
	status = RtmpOSNetDevOpsAlloc((PVOID) & pNetDevOps);
	if (status != NDIS_STATUS_SUCCESS) {
		/* error! no any available ra name can be used! */
		DBGPRINT(RT_DEBUG_TRACE, ("Allocate net device ops fail!\n"));
		RtmpOSNetDevFree(pNetDev);

		return NULL;
	} else {
		DBGPRINT(RT_DEBUG_TRACE,
			 ("Allocate net device ops success!\n"));
		pNetDev->netdev_ops = pNetDevOps;
	}
#endif
	/* find a available interface name, max 32 interfaces */
	status =
	    RtmpOSNetDevRequestName(MC_RowID, pIoctlIF, pNetDev, pNamePrefix,
				    devNum);
	if (status != NDIS_STATUS_SUCCESS) {
		/* error! no any available ra name can be used! */
		DBGPRINT(RT_DEBUG_ERROR,
			 ("Assign interface name (%s with suffix 0~32) failed...\n",
			  pNamePrefix));
		RtmpOSNetDevFree(pNetDev);

		return NULL;
	} else {
		DBGPRINT(RT_DEBUG_TRACE,
			 ("The name of the new %s interface is %s...\n",
			  pNamePrefix, pNetDev->name));
	}

	return pNetDev;
}



/*
========================================================================
Routine Description:
    Allocate memory for adapter control block.

Arguments:
    pAd					Pointer to our adapter

Return Value:
	NDIS_STATUS_SUCCESS
	NDIS_STATUS_FAILURE
	NDIS_STATUS_RESOURCES

Note:
========================================================================
*/
NDIS_STATUS AdapterBlockAllocateMemory(IN PVOID handle,
				       OUT PVOID *ppAd,
				       IN UINT32 SizeOfpAd) {
/*	RTMP_ADAPTER *pAd; */
#ifdef WORKQUEUE_BH
/*	POS_COOKIE cookie; */
#endif /* WORKQUEUE_BH */


#ifdef OS_ABL_FUNC_SUPPORT
	/* get offset for sk_buff */
	{
		struct sk_buff *pPkt = NULL;

		pPkt = kmalloc(sizeof (struct sk_buff), GFP_ATOMIC);
		if (pPkt == NULL) {
			*ppAd = NULL;
			return (NDIS_STATUS_FAILURE);
		}

		RTPktOffsetData = (ULONG) (&(pPkt->data)) - (ULONG) pPkt;
		RTPktOffsetLen = (ULONG) (&(pPkt->len)) - (ULONG) pPkt;
		RTPktOffsetCB = (ULONG) (pPkt->cb) - (ULONG) pPkt;
		kfree(pPkt);

		DBGPRINT(RT_DEBUG_TRACE,
			 ("packet> data offset = %lu\n", RTPktOffsetData));
		DBGPRINT(RT_DEBUG_TRACE,
			 ("packet> len offset = %lu\n", RTPktOffsetLen));
		DBGPRINT(RT_DEBUG_TRACE,
			 ("packet> cb offset = %lu\n", RTPktOffsetCB));
	}
#endif /* OS_ABL_FUNC_SUPPORT */

/*	*ppAd = (PVOID)vmalloc(sizeof(RTMP_ADAPTER)); //pci_alloc_consistent(pci_dev, sizeof(RTMP_ADAPTER), phy_addr); */
	*ppAd = (PVOID) vmalloc(SizeOfpAd);	/*pci_alloc_consistent(pci_dev, sizeof(RTMP_ADAPTER), phy_addr); */
/*	pAd = (RTMP_ADAPTER *)(*ppAd); */
	if (*ppAd) {
		NdisZeroMemory(*ppAd, SizeOfpAd);

		return (NDIS_STATUS_SUCCESS);
	} else {
		return (NDIS_STATUS_FAILURE);
	}
}

/* ========================================================================== */

UINT RtmpOsWirelessExtVerGet(VOID) {
	return WIRELESS_EXT;
}

VOID RtmpDrvAllMacPrint(IN VOID *pReserved,
			IN UINT32 *pBufMac,
			IN UINT32 AddrStart,
			IN UINT32 AddrEnd,
			IN UINT32 AddrStep) {
	struct file *file_w;
	PSTRING fileName = "MacDump.txt";
	mm_segment_t orig_fs;
	STRING *msg;//[1024];
	ULONG macAddr = 0;
	UINT32 macValue = 0;

	os_alloc_mem(NULL, (UCHAR **)&msg, 1024);

	if (msg)
	{
		orig_fs = get_fs();
		set_fs(KERNEL_DS);

		/* open file */
		file_w = filp_open(fileName, O_WRONLY | O_CREAT, 0);
		if (IS_ERR(file_w)) {
			DBGPRINT(RT_DEBUG_TRACE,
				 ("-->2) %s: Error %ld opening %s\n", __FUNCTION__,
				  -PTR_ERR(file_w), fileName));
		} else {
			if (file_w->f_op && file_w->f_op->write) {
				file_w->f_pos = 0;
				macAddr = AddrStart;

				while (macAddr <= AddrEnd) {
	/*				RTMP_IO_READ32(pAd, macAddr, &macValue); // sample */
					macValue = *pBufMac++;
					sprintf(msg, "%08lx = %08X\n", macAddr,
						macValue);

					/* write data to file */
					file_w->f_op->write(file_w, msg, strlen(msg),
							    &file_w->f_pos);

					printk("%s", msg);
					macAddr += AddrStep;
				}
				sprintf(msg, "\nDump all MAC values to %s\n", fileName);
			}
			filp_close(file_w, NULL);
		}
		set_fs(orig_fs);
		os_free_mem(NULL, msg);
	}
}

VOID RtmpDrvAllE2PPrint(IN VOID *pReserved,
			IN USHORT *pMacContent,
			IN UINT32 AddrEnd,
			IN UINT32 AddrStep) {
	struct file *file_w;
	PSTRING fileName = "EEPROMDump.txt";
	mm_segment_t orig_fs;
	STRING *msg;//[1024];
	USHORT eepAddr = 0;
	USHORT eepValue;

	os_alloc_mem(NULL, (UCHAR **)&msg, 1024);

	if (msg)
	{
		orig_fs = get_fs();
		set_fs(KERNEL_DS);
		
		/* open file */
		file_w = filp_open(fileName, O_WRONLY | O_CREAT, 0);
		if (IS_ERR(file_w)) {
			DBGPRINT(RT_DEBUG_TRACE,
				 ("-->2) %s: Error %ld opening %s\n", __FUNCTION__,
				  -PTR_ERR(file_w), fileName));
		} else {
			if (file_w->f_op && file_w->f_op->write) {
				file_w->f_pos = 0;
				eepAddr = 0x00;

	/*			while (eepAddr <= 0xFE) */
				while (eepAddr <= AddrEnd) {
	/*				RT28xx_EEPROM_READ16(pAd, eepAddr, eepValue); // sample */
					eepValue = *pMacContent;
					sprintf(msg, "%08x = %04x\n", eepAddr,
						eepValue);

					/* write data to file */
					file_w->f_op->write(file_w, msg, strlen(msg),
							    &file_w->f_pos);

					printk("%s", msg);
					eepAddr += AddrStep;
					pMacContent += AddrStep;
				}
				sprintf(msg, "\nDump all EEPROM values to %s\n",
					fileName);
			}
			filp_close(file_w, NULL);
		}
		set_fs(orig_fs);
		os_free_mem(NULL, msg);
	}
}

/*
========================================================================
Routine Description:
	Check if the network interface is up.

Arguments:
	*pDev			- Network Interface

Return Value:
	None

Note:
========================================================================
*/
BOOLEAN RtmpOSNetDevIsUp(IN VOID *pDev) {
	struct net_device *pNetDev = (struct net_device *)pDev;

	if ((pNetDev == NULL) || !(pNetDev->flags & IFF_UP))
		return FALSE;

	return TRUE;
}

/*
========================================================================
Routine Description:
	Wake up the command thread.

Arguments:
	pAd				- WLAN control block pointer

Return Value:
	None

Note:
========================================================================
*/
VOID RtmpOsCmdUp(IN RTMP_OS_TASK *pCmdQTask) {
#ifdef KTHREAD_SUPPORT
	do {
		OS_TASK *pTask = RTMP_OS_TASK_GET(pCmdQTask);
		{
			pTask->kthread_running = TRUE;
			wake_up(&pTask->kthread_q);
		}
	} while (0);
#else
	do {
		OS_TASK *pTask = RTMP_OS_TASK_GET(pCmdQTask);
		CHECK_PID_LEGALITY(pTask->taskPID) {
			RTMP_SEM_EVENT_UP(&(pTask->taskSema));
		}
	} while (0);
#endif /* KTHREAD_SUPPORT */
}

/*
========================================================================
Routine Description:
	Wake up USB Mlme thread.

Arguments:
	pAd				- WLAN control block pointer

Return Value:
	None

Note:
========================================================================
*/
VOID RtmpOsMlmeUp(IN RTMP_OS_TASK *pMlmeQTask) {
#ifdef RTMP_USB_SUPPORT
/*	OS_RTUSBMlmeUp(pMlmeQTask); */

#ifdef KTHREAD_SUPPORT
	do {
		OS_TASK *pTask = RTMP_OS_TASK_GET(pMlmeQTask);
		if ((pTask != NULL) && (pTask->kthread_task)) {
			pTask->kthread_running = TRUE;
			wake_up(&pTask->kthread_q);
		}
	} while (0);
#else
	do {
		OS_TASK *pTask = RTMP_OS_TASK_GET(pMlmeQTask);
		if (pTask != NULL) {
			CHECK_PID_LEGALITY(pTask->taskPID) {
				RTMP_SEM_EVENT_UP(&(pTask->taskSema));
			}
		}
	} while (0);
#endif /* KTHREAD_SUPPORT */
#endif /* RTMP_USB_SUPPORT */
}

/*
========================================================================
Routine Description:
	Check if the file is error.

Arguments:
	pFile			- the file

Return Value:
	OK or any error

Note:
	rt_linux.h, not rt_drv.h
========================================================================
*/
INT32 RtmpOsFileIsErr(IN VOID *pFile) {
	return IS_FILE_OPEN_ERR(pFile);
}

int RtmpOSIRQRelease(IN PNET_DEV pNetDev,
		     IN UINT32 infType,
		     IN PPCI_DEV pci_dev,
		     IN BOOLEAN *pHaveMsi) {
	struct net_device *net_dev = pNetDev;
/*	PRTMP_ADAPTER pAd = NULL; */

/*	GET_PAD_FROM_NET_DEV(pAd, net_dev); */

/*	ASSERT(pAd); */
	net_dev = net_dev;	/* avoid compile warning */



	return 0;
}

/*
========================================================================
Routine Description:
	Enable or disable wireless event sent.

Arguments:
	pReserved		- Reserved
	FlgIsWEntSup	- TRUE or FALSE

Return Value:
	None

Note:
========================================================================
*/
VOID RtmpOsWlanEventSet(IN VOID *pReserved,
			IN BOOLEAN *pCfgWEnt,
			IN BOOLEAN FlgIsWEntSup) {
#if WIRELESS_EXT >= 15
/*	pAd->CommonCfg.bWirelessEvent = FlgIsWEntSup; */
	*pCfgWEnt = FlgIsWEntSup;
#else
	*pCfgWEnt = 0;		/* disable */
#endif
}

/*
========================================================================
Routine Description:
	vmalloc

Arguments:
	Size			- memory size

Return Value:
	the memory

Note:
========================================================================
*/
VOID *RtmpOsVmalloc(IN ULONG Size) {
	return vmalloc(Size);
}

/*
========================================================================
Routine Description:
	vfree

Arguments:
	pMem			- the memory

Return Value:
	None

Note:
========================================================================
*/
VOID RtmpOsVfree(IN VOID *pMem) {
	if (pMem != NULL)
		vfree(pMem);
}

/*
========================================================================
Routine Description:
	Get network interface name.

Arguments:
	pDev			- the device

Return Value:
	the name

Note:
========================================================================
*/
char *RtmpOsGetNetDevName(IN VOID *pDev) {
	return ((PNET_DEV) pDev)->name;
}

/*
========================================================================
Routine Description:
	Assign protocol to the packet.

Arguments:
	pPkt			- the packet

Return Value:
	None

Note:
========================================================================
*/
VOID RtmpOsPktProtocolAssign(IN PNDIS_PACKET pNetPkt) {
	struct sk_buff *pRxPkt = RTPKT_TO_OSPKT(pNetPkt);
	pRxPkt->protocol = eth_type_trans(pRxPkt, pRxPkt->dev);
}

BOOLEAN RtmpOsStatsAlloc(IN VOID **ppStats,
			 IN VOID **ppIwStats) {
	os_alloc_mem(NULL, (UCHAR **) ppStats,
		     sizeof (struct net_device_stats));
	if ((*ppStats) == NULL)
		return FALSE;
	NdisZeroMemory((UCHAR *) *ppStats, sizeof (struct net_device_stats));

#if WIRELESS_EXT >= 12
	os_alloc_mem(NULL, (UCHAR **) ppIwStats, sizeof (struct iw_statistics));
	if ((*ppIwStats) == NULL) {
		os_free_mem(NULL, *ppStats);
		return FALSE;
	}
	NdisZeroMemory((UCHAR *)* ppIwStats, sizeof (struct iw_statistics));
#endif

	return TRUE;
}

/*
========================================================================
Routine Description:
	Pass the received packet to OS.

Arguments:
	pPkt			- the packet

Return Value:
	None

Note:
========================================================================
*/
VOID RtmpOsPktRcvHandle(IN PNDIS_PACKET pNetPkt) {
	struct sk_buff *pRxPkt = RTPKT_TO_OSPKT(pNetPkt);
	netif_rx(pRxPkt);
}

VOID RtmpOsTaskPidInit(IN RTMP_OS_PID *pPid) {
	*pPid = THREAD_PID_INIT_VALUE;
}

/*
========================================================================
Routine Description:
	Get the network interface for the packet.

Arguments:
	pPkt			- the packet

Return Value:
	None

Note:
========================================================================
*/
PNET_DEV RtmpOsPktNetDevGet(IN VOID *pPkt) {
	return GET_OS_PKT_NETDEV(pPkt);
}

#ifdef IAPP_SUPPORT
/* Layer 2 Update frame to switch/bridge */
/* For any Layer2 devices, e.g., bridges, switches and other APs, the frame
   can update their forwarding tables with the correct port to reach the new
   location of the STA */
typedef struct GNU_PACKED _RT_IAPP_L2_UPDATE_FRAME {

	UCHAR DA[ETH_ALEN];	/* broadcast MAC address */
	UCHAR SA[ETH_ALEN];	/* the MAC address of the STA that has just associated
				   or reassociated */
	USHORT Len;		/* 8 octets */
	UCHAR DSAP;		/* null */
	UCHAR SSAP;		/* null */
	UCHAR Control;		/* reference to IEEE Std 802.2 */
	UCHAR XIDInfo[3];	/* reference to IEEE Std 802.2 */
} RT_IAPP_L2_UPDATE_FRAME, *PRT_IAPP_L2_UPDATE_FRAME;

PNDIS_PACKET RtmpOsPktIappMakeUp(IN PNET_DEV pNetDev,
				 IN UINT8 *pMac) {
	RT_IAPP_L2_UPDATE_FRAME frame_body;
	INT size = sizeof (RT_IAPP_L2_UPDATE_FRAME);
	PNDIS_PACKET pNetBuf;

	if (pNetDev == NULL)
		return NULL;

	pNetBuf = RtmpOSNetPktAlloc(NULL, size);
	if (!pNetBuf) {
		DBGPRINT(RT_DEBUG_ERROR, ("Error! Can't allocate a skb.\n"));
		return NULL;
	}

	/* init the update frame body */
	NdisZeroMemory(&frame_body, size);

	memset(frame_body.DA, 0xFF, ETH_ALEN);
	memcpy(frame_body.SA, pMac, ETH_ALEN);

	frame_body.Len = OS_HTONS(ETH_ALEN);
	frame_body.DSAP = 0;
	frame_body.SSAP = 0x01;
	frame_body.Control = 0xAF;

	frame_body.XIDInfo[0] = 0x81;
	frame_body.XIDInfo[1] = 1;
	frame_body.XIDInfo[2] = 1 << 1;

	SET_OS_PKT_NETDEV(pNetBuf, pNetDev);
	skb_reserve(pNetBuf, 2);
	memcpy(skb_put(pNetBuf, size), &frame_body, size);
	return pNetBuf;
}
#endif /* IAPP_SUPPORT */

VOID RtmpOsPktNatMagicTag(IN PNDIS_PACKET pNetPkt) {
}

VOID RtmpOsPktNatNone(IN PNDIS_PACKET pNetPkt) {
}


#ifdef RT_CFG80211_SUPPORT
/* all available channels */
UCHAR Cfg80211_Chan[] = {
	1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,

	/* 802.11 UNI / HyperLan 2 */
	36, 38, 40, 44, 46, 48, 52, 54, 56, 60, 62, 64,

	/* 802.11 HyperLan 2 */
	100, 104, 108, 112, 116, 118, 120, 124, 126, 128, 132, 134, 136,

	/* 802.11 UNII */
	140, 149, 151, 153, 157, 159, 161, 165, 167, 169, 171, 173,

	/* Japan */
	184, 188, 192, 196, 208, 212, 216,
};

/*
	Array of bitrates the hardware can operate with
	in this band. Must be sorted to give a valid "supported
	rates" IE, i.e. CCK rates first, then OFDM.

	For HT, assign MCS in another structure, ieee80211_sta_ht_cap.
*/
const struct ieee80211_rate Cfg80211_SupRate[12] = {
	{
		.flags = IEEE80211_RATE_SHORT_PREAMBLE,
		.bitrate = 10,
		.hw_value = 0,
		.hw_value_short = 0,
	},
	{
		.flags = IEEE80211_RATE_SHORT_PREAMBLE,
		.bitrate = 20,
		.hw_value = 1,
		.hw_value_short = 1,
	},
	{
		.flags = IEEE80211_RATE_SHORT_PREAMBLE,
		.bitrate = 55,
		.hw_value = 2,
		.hw_value_short = 2,
	},
	{
		.flags = IEEE80211_RATE_SHORT_PREAMBLE,
		.bitrate = 110,
		.hw_value = 3,
		.hw_value_short = 3,
	},
	{
		.flags = 0,
		.bitrate = 60,
		.hw_value = 4,
		.hw_value_short = 4,
	},
	{
		.flags = 0,
		.bitrate = 90,
		.hw_value = 5,
		.hw_value_short = 5,
	},
	{
		.flags = 0,
		.bitrate = 120,
		.hw_value = 6,
		.hw_value_short = 6,
	},
	{
		.flags = 0,
		.bitrate = 180,
		.hw_value = 7,
		.hw_value_short = 7,
	},
	{
		.flags = 0,
		.bitrate = 240,
		.hw_value = 8,
		.hw_value_short = 8,
	},
	{
		.flags = 0,
		.bitrate = 360,
		.hw_value = 9,
		.hw_value_short = 9,
	},
	{
		.flags = 0,
		.bitrate = 480,
		.hw_value = 10,
		.hw_value_short = 10,
	},
	{
		.flags = 0,
		.bitrate = 540,
		.hw_value = 11,
		.hw_value_short = 11,
	},
};

static const UINT32 CipherSuites[] = {
	WLAN_CIPHER_SUITE_WEP40,
	WLAN_CIPHER_SUITE_WEP104,
	WLAN_CIPHER_SUITE_TKIP,
	WLAN_CIPHER_SUITE_CCMP,
};

/*
========================================================================
Routine Description:
	UnRegister MAC80211 Module.

Arguments:
	pCB				- CFG80211 control block pointer
	pNetDev			- Network device

Return Value:
	NONE

Note:
========================================================================
*/
VOID CFG80211OS_UnRegister(
	IN VOID						*pCB,
	IN VOID						*pNetDevOrg)
{
	CFG80211_CB *pCfg80211_CB = (CFG80211_CB *)pCB;
	struct net_device *pNetDev = (struct net_device *)pNetDevOrg;



	/* unregister */
	if (pCfg80211_CB->pCfg80211_Wdev != NULL)
	{
		DBGPRINT(RT_DEBUG_ERROR, ("80211> unregister/free wireless device\n"));

		/*
			Must unregister, or you will suffer problem when you change
			regulatory domain by using iw.
		*/
		
#ifdef RFKILL_HW_SUPPORT
		wiphy_rfkill_stop_polling(pCfg80211_CB->pCfg80211_Wdev->wiphy);
#endif /* RFKILL_HW_SUPPORT */
		wiphy_unregister(pCfg80211_CB->pCfg80211_Wdev->wiphy);
		wiphy_free(pCfg80211_CB->pCfg80211_Wdev->wiphy);
		os_free_mem(NULL, pCfg80211_CB->pCfg80211_Wdev);

		if (pCfg80211_CB->pCfg80211_Channels != NULL)
			os_free_mem(NULL, pCfg80211_CB->pCfg80211_Channels);
		/* End of if */

		if (pCfg80211_CB->pCfg80211_Rates != NULL)
			os_free_mem(NULL, pCfg80211_CB->pCfg80211_Rates);
		/* End of if */

		pCfg80211_CB->pCfg80211_Wdev = NULL;
		pCfg80211_CB->pCfg80211_Channels = NULL;
		pCfg80211_CB->pCfg80211_Rates = NULL;

		/* must reset to NULL; or kernel will panic in unregister_netdev */
		pNetDev->ieee80211_ptr = NULL;
		SET_NETDEV_DEV(pNetDev, NULL);
	} /* End of if */

	os_free_mem(NULL, pCfg80211_CB);
} /* End of CFG80211_UnRegister */


/*
========================================================================
Routine Description:
	Initialize wireless channel in 2.4GHZ and 5GHZ.

Arguments:
	pAd				- WLAN control block pointer
	pWiphy			- WLAN PHY interface
	pChannels		- Current channel info
	pRates			- Current rate info

Return Value:
	TRUE			- init successfully
	FALSE			- init fail

Note:
	TX Power related:

	1. Suppose we can send power to 15dBm in the board.
	2. A value 0x0 ~ 0x1F for a channel. We will adjust it based on 15dBm/
		54Mbps. So if value == 0x07, the TX power of 54Mbps is 15dBm and
		the value is 0x07 in the EEPROM.
	3. Based on TX power value of 54Mbps/channel, adjust another value
		0x0 ~ 0xF for other data rate. (-6dBm ~ +6dBm)

	Other related factors:
	1. TX power percentage from UI/users;
	2. Maximum TX power limitation in the regulatory domain.
========================================================================
*/
BOOLEAN CFG80211_SupBandInit(
	IN VOID						*pCB,
	IN CFG80211_BAND 			*pBandInfo,
	IN VOID						*pWiphyOrg,
	IN VOID						*pChannelsOrg,
	IN VOID						*pRatesOrg)
{
	CFG80211_CB *pCfg80211_CB = (CFG80211_CB *)pCB;
	struct wiphy *pWiphy = (struct wiphy *)pWiphyOrg;
	struct ieee80211_channel *pChannels = (struct ieee80211_channel *)pChannelsOrg;
	struct ieee80211_rate *pRates = (struct ieee80211_rate *)pRatesOrg;
	struct ieee80211_supported_band *pBand;
	UINT32 NumOfChan, NumOfRate;
	UINT32 IdLoop;
	UINT32 CurTxPower;


	/* sanity check */
	if (pBandInfo->RFICType == 0)
		pBandInfo->RFICType = RFIC_24GHZ | RFIC_5GHZ;
	/* End of if */

	CFG80211DBG(RT_DEBUG_ERROR, ("80211> RFICType = %d\n",
				pBandInfo->RFICType));

	/* init */
	if (pBandInfo->RFICType & RFIC_5GHZ)
		NumOfChan = CFG80211_NUM_OF_CHAN_2GHZ + CFG80211_NUM_OF_CHAN_5GHZ;
	else
		NumOfChan = CFG80211_NUM_OF_CHAN_2GHZ;
	/* End of if */

	if (pBandInfo->FlgIsBMode == TRUE)
		NumOfRate = 4;
	else
		NumOfRate = 4 + 8;
	/* End of if */

	if (pChannels == NULL)
	{
		pChannels = kzalloc(sizeof(*pChannels) * NumOfChan, GFP_KERNEL);
		if (!pChannels)
		{
			DBGPRINT(RT_DEBUG_ERROR, ("80211> ieee80211_channel allocation fail!\n"));
			return FALSE;
		} /* End of if */
	} /* End of if */

	CFG80211DBG(RT_DEBUG_ERROR, ("80211> Number of channel = %d\n",
				CFG80211_NUM_OF_CHAN_5GHZ));

	if (pRates == NULL)
	{
		pRates = kzalloc(sizeof(*pRates) * NumOfRate, GFP_KERNEL);
		if (!pRates)
		{
			os_free_mem(NULL, pChannels);
			DBGPRINT(RT_DEBUG_ERROR, ("80211> ieee80211_rate allocation fail!\n"));
			return FALSE;
		} /* End of if */
	} /* End of if */

	CFG80211DBG(RT_DEBUG_ERROR, ("80211> Number of rate = %d\n", NumOfRate));

	/* get TX power */
#ifdef SINGLE_SKU
	CurTxPower = pBandInfo->DefineMaxTxPwr; /* dBm */
#else
	CurTxPower = 0; /* unknown */
#endif /* SINGLE_SKU */

	CFG80211DBG(RT_DEBUG_ERROR, ("80211> CurTxPower = %d dBm\n", CurTxPower));

	/* init channel */
	for(IdLoop=0; IdLoop<NumOfChan; IdLoop++)
	{
		pChannels[IdLoop].center_freq = \
					ieee80211_channel_to_frequency(Cfg80211_Chan[IdLoop]);
		pChannels[IdLoop].hw_value = IdLoop;

		if (IdLoop < CFG80211_NUM_OF_CHAN_2GHZ)
			pChannels[IdLoop].max_power = CurTxPower;
		else
			pChannels[IdLoop].max_power = CurTxPower;
		/* End of if */

		pChannels[IdLoop].max_antenna_gain = 0xff;
	} /* End of if */

	/* init rate */
	for(IdLoop=0; IdLoop<NumOfRate; IdLoop++)
		memcpy(&pRates[IdLoop], &Cfg80211_SupRate[IdLoop], sizeof(*pRates));
	/* End of for */

	pBand = &pCfg80211_CB->Cfg80211_bands[IEEE80211_BAND_2GHZ];
	if (pBandInfo->RFICType & RFIC_24GHZ)
	{
		pBand->n_channels = CFG80211_NUM_OF_CHAN_2GHZ;
		pBand->n_bitrates = NumOfRate;
		pBand->channels = pChannels;
		pBand->bitrates = pRates;

#ifdef DOT11_N_SUPPORT
		/* for HT, assign pBand->ht_cap */
		pBand->ht_cap.ht_supported = true;
		pBand->ht_cap.cap = IEEE80211_HT_CAP_SUP_WIDTH_20_40 |
					       IEEE80211_HT_CAP_SM_PS |
					       IEEE80211_HT_CAP_SGI_40 |
					       IEEE80211_HT_CAP_DSSSCCK40;
		pBand->ht_cap.ampdu_factor = 3; /* 2 ^ 16 */
		pBand->ht_cap.ampdu_density = pBandInfo->MpduDensity;

		memset(&pBand->ht_cap.mcs, 0, sizeof(pBand->ht_cap.mcs));
		CFG80211DBG(RT_DEBUG_ERROR,
					("80211> TxStream = %d\n", pBandInfo->TxStream));

		switch(pBandInfo->TxStream)
		{
			case 1:
			default:
				pBand->ht_cap.mcs.rx_mask[0] = 0xff;
				break;

			case 2:
				pBand->ht_cap.mcs.rx_mask[0] = 0xff;
				pBand->ht_cap.mcs.rx_mask[1] = 0xff;
				break;

			case 3:
				pBand->ht_cap.mcs.rx_mask[0] = 0xff;
				pBand->ht_cap.mcs.rx_mask[1] = 0xff;
				pBand->ht_cap.mcs.rx_mask[2] = 0xff;
				break;
		} /* End of switch */

		pBand->ht_cap.mcs.tx_params = IEEE80211_HT_MCS_TX_DEFINED;
#endif /* DOT11_N_SUPPORT */

		pWiphy->bands[IEEE80211_BAND_2GHZ] = pBand;
	}
	else
	{
		pWiphy->bands[IEEE80211_BAND_2GHZ] = NULL;
		pBand->channels = NULL;
		pBand->bitrates = NULL;
	} /* End of if */

	pBand = &pCfg80211_CB->Cfg80211_bands[IEEE80211_BAND_5GHZ];
	if (pBandInfo->RFICType & RFIC_5GHZ)
	{
		pBand->n_channels = CFG80211_NUM_OF_CHAN_5GHZ;
		pBand->n_bitrates = NumOfRate - 4;
		pBand->channels = &pChannels[CFG80211_NUM_OF_CHAN_2GHZ];
		pBand->bitrates = &pRates[4];

		/* for HT, assign pBand->ht_cap */
#ifdef DOT11_N_SUPPORT
		/* for HT, assign pBand->ht_cap */
		pBand->ht_cap.ht_supported = true;
		pBand->ht_cap.cap = IEEE80211_HT_CAP_SUP_WIDTH_20_40 |
					       IEEE80211_HT_CAP_SM_PS |
					       IEEE80211_HT_CAP_SGI_40 |
					       IEEE80211_HT_CAP_DSSSCCK40;
		pBand->ht_cap.ampdu_factor = 3; /* 2 ^ 16 */
		pBand->ht_cap.ampdu_density = pBandInfo->MpduDensity;

		memset(&pBand->ht_cap.mcs, 0, sizeof(pBand->ht_cap.mcs));
		switch(pBandInfo->RxStream)
		{
			case 1:
			default:
				pBand->ht_cap.mcs.rx_mask[0] = 0xff;
				break;

			case 2:
				pBand->ht_cap.mcs.rx_mask[0] = 0xff;
				pBand->ht_cap.mcs.rx_mask[1] = 0xff;
				break;

			case 3:
				pBand->ht_cap.mcs.rx_mask[0] = 0xff;
				pBand->ht_cap.mcs.rx_mask[1] = 0xff;
				pBand->ht_cap.mcs.rx_mask[2] = 0xff;
				break;
		} /* End of switch */

		pBand->ht_cap.mcs.tx_params = IEEE80211_HT_MCS_TX_DEFINED;
#endif /* DOT11_N_SUPPORT */

		pWiphy->bands[IEEE80211_BAND_5GHZ] = pBand;
	}
	else
	{
		pWiphy->bands[IEEE80211_BAND_5GHZ] = NULL;
		pBand->channels = NULL;
		pBand->bitrates = NULL;
	} /* End of if */

	pCfg80211_CB->pCfg80211_Channels = pChannels;
	pCfg80211_CB->pCfg80211_Rates = pRates;

	return TRUE;
} /* End of CFG80211_SupBandInit */


/*
========================================================================
Routine Description:
	Re-Initialize wireless channel/PHY in 2.4GHZ and 5GHZ.

Arguments:
	pCB				- CFG80211 control block pointer
	pBandInfo		- Band information

Return Value:
	TRUE			- re-init successfully
	FALSE			- re-init fail

Note:
	CFG80211_SupBandInit() is called in xx_probe().
	But we do not have complete chip information in xx_probe() so we
	need to re-init bands in xx_open().
========================================================================
*/
BOOLEAN CFG80211OS_SupBandReInit(
	IN VOID							*pCB,
	IN CFG80211_BAND 				*pBandInfo)
{
	CFG80211_CB *pCfg80211_CB = (CFG80211_CB *)pCB;
	struct wiphy *pWiphy;


	/* sanity check */
	if ((pCfg80211_CB == NULL) || (pCfg80211_CB->pCfg80211_Wdev == NULL))
		return FALSE;
	/* End of if */

	pWiphy = pCfg80211_CB->pCfg80211_Wdev->wiphy;

	if (pWiphy != NULL)
	{
		CFG80211DBG(RT_DEBUG_ERROR, ("80211> re-init bands...\n"));

		/* re-init bands */
		CFG80211_SupBandInit(pCfg80211_CB, pBandInfo, pWiphy,
							pCfg80211_CB->pCfg80211_Channels,
							pCfg80211_CB->pCfg80211_Rates);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,32))
		/* re-init PHY */
		pWiphy->rts_threshold = pBandInfo->RtsThreshold;
		pWiphy->frag_threshold = pBandInfo->FragmentThreshold;
		pWiphy->retry_short = pBandInfo->RetryMaxCnt & 0xff;
		pWiphy->retry_long = (pBandInfo->RetryMaxCnt & 0xff00)>>8;
#endif /* LINUX_VERSION_CODE */

		return TRUE;
	} /* End of if */

	return FALSE;
} /* End of CFG80211OS_SupBandReInit */


/*
========================================================================
Routine Description:
	Hint to the wireless core a regulatory domain from driver.

Arguments:
	pAd				- WLAN control block pointer
	pCountryIe		- pointer to the country IE
	CountryIeLen	- length of the country IE

Return Value:
	NONE

Note:
	Must call the function in kernel thread.
========================================================================
*/
VOID CFG80211OS_RegHint(
	IN VOID						*pCB,
	IN UCHAR					*pCountryIe,
	IN ULONG					CountryIeLen)
{
	CFG80211_CB *pCfg80211_CB = (CFG80211_CB *)pCB;


	CFG80211DBG(RT_DEBUG_ERROR,
			("crda> regulatory domain hint: %c%c\n",
			pCountryIe[0], pCountryIe[1]));

	if ((pCfg80211_CB->pCfg80211_Wdev == NULL) || (pCountryIe == NULL))
	{
		CFG80211DBG(RT_DEBUG_ERROR, ("crda> regulatory domain hint not support!\n"));
		return;
	} /* End of if */

	/* hints a country IE as a regulatory domain "without" channel/power info. */
/*	regulatory_hint(pCfg80211_CB->pMac80211_Hw->wiphy, pCountryIe); */
	regulatory_hint(pCfg80211_CB->pCfg80211_Wdev->wiphy, (const char *)pCountryIe);
} /* End of CFG80211OS_RegHint */


/*
========================================================================
Routine Description:
	Hint to the wireless core a regulatory domain from country element.

Arguments:
	pAdCB			- WLAN control block pointer
	pCountryIe		- pointer to the country IE
	CountryIeLen	- length of the country IE

Return Value:
	NONE

Note:
	Must call the function in kernel thread.
========================================================================
*/
VOID CFG80211OS_RegHint11D(
	IN VOID						*pCB,
	IN UCHAR					*pCountryIe,
	IN ULONG					CountryIeLen)
{
	/* no regulatory_hint_11d() in 2.6.32 */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,32))
	CFG80211_CB *pCfg80211_CB = (CFG80211_CB *)pCB;


	if ((pCfg80211_CB->pCfg80211_Wdev == NULL) || (pCountryIe == NULL))
	{
		CFG80211DBG(RT_DEBUG_ERROR, ("crda> regulatory domain hint not support!\n"));
		return;
	} /* End of if */

	CFG80211DBG(RT_DEBUG_ERROR,
				("crda> regulatory domain hint: %c%c\n",
				pCountryIe[0], pCountryIe[1]));

	/*
		hints a country IE as a regulatory domain "with" channel/power info.
		but if you use regulatory_hint(), it only hint "regulatory domain".
	*/
/*	regulatory_hint_11d(pCfg80211_CB->pMac80211_Hw->wiphy, pCountryIe, CountryIeLen); */
	regulatory_hint_11d(pCfg80211_CB->pCfg80211_Wdev->wiphy, pCountryIe, CountryIeLen);
#endif /* LINUX_VERSION_CODE */
} /* End of CFG80211_RegHint11D */


BOOLEAN CFG80211OS_BandInfoGet(
	IN VOID						*pCB,
	IN VOID						*pWiphyOrg,
	OUT VOID					**ppBand24,
	OUT VOID					**ppBand5)
{
	CFG80211_CB *pCfg80211_CB = (CFG80211_CB *)pCB;
	struct wiphy *pWiphy = (struct wiphy *)pWiphyOrg;


	/* sanity check */
	if (pWiphy == NULL)
	{
		if ((pCfg80211_CB != NULL) && (pCfg80211_CB->pCfg80211_Wdev != NULL))
			pWiphy = pCfg80211_CB->pCfg80211_Wdev->wiphy;
		/* End of if */
	} /* End of if */

	if (pWiphy == NULL)
		return FALSE;

	*ppBand24 = pWiphy->bands[IEEE80211_BAND_2GHZ];
	*ppBand5 = pWiphy->bands[IEEE80211_BAND_5GHZ];
	return TRUE;
}


UINT32 CFG80211OS_ChanNumGet(
	IN VOID						*pCB,
	IN VOID						*pWiphyOrg,
	IN UINT32					IdBand)
{
	CFG80211_CB *pCfg80211_CB = (CFG80211_CB *)pCB;
	struct wiphy *pWiphy = (struct wiphy *)pWiphyOrg;


	/* sanity check */
	if (pWiphy == NULL)
	{
		if ((pCfg80211_CB != NULL) && (pCfg80211_CB->pCfg80211_Wdev != NULL))
			pWiphy = pCfg80211_CB->pCfg80211_Wdev->wiphy;
		/* End of if */
	} /* End of if */

	if (pWiphy == NULL)
		return 0;

	if (pWiphy->bands[IdBand] != NULL)
		return pWiphy->bands[IdBand]->n_channels;

	return 0;
}


BOOLEAN CFG80211OS_ChanInfoGet(
	IN VOID						*pCB,
	IN VOID						*pWiphyOrg,
	IN UINT32					IdBand,
	IN UINT32					IdChan,
	OUT UINT32					*pChanId,
	OUT UINT32					*pPower,
	OUT BOOLEAN					*pFlgIsRadar)
{
	CFG80211_CB *pCfg80211_CB = (CFG80211_CB *)pCB;
	struct wiphy *pWiphy = (struct wiphy *)pWiphyOrg;
	struct ieee80211_supported_band *pSband;
	struct ieee80211_channel *pChan;


	/* sanity check */
	if (pWiphy == NULL)
	{
		if ((pCfg80211_CB != NULL) && (pCfg80211_CB->pCfg80211_Wdev != NULL))
			pWiphy = pCfg80211_CB->pCfg80211_Wdev->wiphy;
		/* End of if */
	} /* End of if */

	if (pWiphy == NULL)
		return FALSE;

	pSband = pWiphy->bands[IdBand];
	pChan = &pSband->channels[IdChan];

	*pChanId = ieee80211_frequency_to_channel(pChan->center_freq);

	if (pChan->flags & IEEE80211_CHAN_DISABLED)
	{
		CFG80211DBG(RT_DEBUG_ERROR,
					("Chan %03d (frq %d):\tnot allowed!\n",
					(*pChanId), pChan->center_freq));
		return FALSE;
	}

	*pPower = pChan->max_power;

	if (pChan->flags & IEEE80211_CHAN_RADAR)
		*pFlgIsRadar = TRUE;
	else
		*pFlgIsRadar = FALSE;

	return TRUE;
}


/*
========================================================================
Routine Description:
	Inform us that a scan is got.

Arguments:
	pAdCB				- WLAN control block pointer

Return Value:
	NONE

Note:
	Call RT_CFG80211_SCANNING_INFORM, not CFG80211_Scaning
========================================================================
*/
VOID CFG80211OS_Scaning(
	IN VOID						*pCB,
	IN VOID						**pChanOrg,
	IN UINT32					ChanId,
	IN UCHAR					*pFrame,
	IN UINT32					FrameLen,
	IN INT32					RSSI,
	IN BOOLEAN					FlgIsNMode,
	IN UINT8					BW)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,30))
#ifdef CONFIG_STA_SUPPORT
	CFG80211_CB *pCfg80211_CB = (CFG80211_CB *)pCB;
	struct ieee80211_channel *pChan;


	if ((*pChanOrg) == NULL)
	{
		os_alloc_mem(NULL, (UCHAR **)pChanOrg, sizeof(struct ieee80211_channel));
		if ((*pChanOrg) == NULL)
		{
			DBGPRINT(RT_DEBUG_ERROR, ("80211> Allocate chan fail!\n"));
			return;
		}
	}

	pChan = (struct ieee80211_channel *)(*pChanOrg);
	memset(pChan, 0, sizeof(*pChan));

	if (ChanId > 14)
		pChan->band = IEEE80211_BAND_5GHZ;
	else
		pChan->band = IEEE80211_BAND_2GHZ;
	/* End of if */

	pChan->center_freq = ieee80211_channel_to_frequency(ChanId);

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,32))
	if (FlgIsNMode == TRUE)
	{
		if (BW == 0)
			pChan->max_bandwidth = 20; /* 20MHz */
		else
			pChan->max_bandwidth = 40; /* 40MHz */
		/* End of if */
	}
	else
		pChan->max_bandwidth = 5; /* 5MHz for non-HT device */
	/* End of if */
#endif /* LINUX_VERSION_CODE */

	/* no use currently in 2.6.30 */
/*	if (ieee80211_is_beacon(((struct ieee80211_mgmt *)pFrame)->frame_control)) */
/*		pChan->beacon_found = 1; */
	/* End of if */

	/* inform 80211 a scan is got */
	/* we can use cfg80211_inform_bss in 2.6.31, it is easy more than the one */
	/* in cfg80211_inform_bss_frame(), it will memcpy pFrame but pChan */
	cfg80211_inform_bss_frame(pCfg80211_CB->pCfg80211_Wdev->wiphy,
								pChan,
								(struct ieee80211_mgmt *)pFrame,
								FrameLen,
								RSSI,
								GFP_ATOMIC);

	CFG80211DBG(RT_DEBUG_ERROR, ("80211> cfg80211_inform_bss_frame\n"));
#endif /* CONFIG_STA_SUPPORT */
#endif /* LINUX_VERSION_CODE */
}


/*
========================================================================
Routine Description:
	Inform us that scan ends.

Arguments:
	pAdCB			- WLAN control block pointer
	FlgIsAborted	- 1: scan is aborted

Return Value:
	NONE

Note:
========================================================================
*/
VOID CFG80211OS_ScanEnd(
	IN VOID						*pCB,
	IN BOOLEAN					FlgIsAborted)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,30))
#ifdef CONFIG_STA_SUPPORT
	CFG80211_CB *pCfg80211_CB = (CFG80211_CB *)pCB;


	CFG80211DBG(RT_DEBUG_ERROR, ("80211> cfg80211_scan_done\n"));
	cfg80211_scan_done(pCfg80211_CB->pCfg80211_ScanReq, FlgIsAborted);
#endif /* CONFIG_STA_SUPPORT */
#endif /* LINUX_VERSION_CODE */
}


/*
========================================================================
Routine Description:
	Inform CFG80211 about association status.

Arguments:
	pAdCB			- WLAN control block pointer
	pBSSID			- the BSSID of the AP
	pReqIe			- the element list in the association request frame
	ReqIeLen		- the request element length
	pRspIe			- the element list in the association response frame
	RspIeLen		- the response element length
	FlgIsSuccess	- 1: success; otherwise: fail

Return Value:
	None

Note:
========================================================================
*/
void CFG80211OS_ConnectResultInform(
	IN VOID						*pCB,
	IN UCHAR					*pBSSID,
	IN UCHAR					*pReqIe,
	IN UINT32					ReqIeLen,
	IN UCHAR					*pRspIe,
	IN UINT32					RspIeLen,
	IN UCHAR					FlgIsSuccess)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,32))
	CFG80211_CB *pCfg80211_CB = (CFG80211_CB *)pCB;


	if ((pCfg80211_CB->pCfg80211_Wdev->netdev == NULL) || (pBSSID == NULL))
		return;
	/* End of if */

	if (FlgIsSuccess)
	{
		cfg80211_connect_result(pCfg80211_CB->pCfg80211_Wdev->netdev,
								pBSSID,
								pReqIe,
								ReqIeLen,
								pRspIe,
								RspIeLen,
								WLAN_STATUS_SUCCESS,
								GFP_KERNEL);
	}
	else
	{
		cfg80211_connect_result(pCfg80211_CB->pCfg80211_Wdev->netdev,
								pBSSID,
								NULL, 0, NULL, 0,
								WLAN_STATUS_UNSPECIFIED_FAILURE,
								GFP_KERNEL);
	} /* End of if */
#endif /* LINUX_VERSION_CODE */
} /* End of CFG80211_ConnectResultInform */
#endif /* RT_CFG80211_SUPPORT */




#ifdef OS_ABL_FUNC_SUPPORT
/*
========================================================================
Routine Description:
	Change/Recover file UID/GID.

Arguments:
	pOSFSInfoOrg	- the file
	bSet			- Change (TRUE) or Recover (FALSE)

Return Value:
	None

Note:
	rt_linux.h, not rt_drv.h
========================================================================
*/
void RtmpOSFSInfoChange(IN RTMP_OS_FS_INFO *pOSFSInfoOrg,
			IN BOOLEAN bSet) {
	OS_FS_INFO *pOSFSInfo;

	if (bSet == TRUE) {
		os_alloc_mem(NULL, (UCHAR **) & (pOSFSInfoOrg->pContent),
			     sizeof (OS_FS_INFO));
		if (pOSFSInfoOrg->pContent == NULL) {
			DBGPRINT(RT_DEBUG_ERROR,
				 ("%s: alloc file info fail!\n", __FUNCTION__));
			return;
		} else
			memset(pOSFSInfoOrg->pContent, 0, sizeof (OS_FS_INFO));
	}

	pOSFSInfo = (OS_FS_INFO *) (pOSFSInfoOrg->pContent);
	if (pOSFSInfo == NULL) {
		DBGPRINT(RT_DEBUG_ERROR,
			 ("%s: pOSFSInfo == NULL!\n", __FUNCTION__));
		return;
	}

	__RtmpOSFSInfoChange(pOSFSInfo, bSet);

	if (bSet == FALSE) {
		if (pOSFSInfoOrg->pContent != NULL) {
			os_free_mem(NULL, pOSFSInfoOrg->pContent);
			pOSFSInfoOrg->pContent = NULL;
		}
	}
}

/*
========================================================================
Routine Description:
	Activate a tasklet.

Arguments:
	pTasklet		- the tasklet

Return Value:
	TRUE or FALSE

Note:
========================================================================
*/
BOOLEAN RtmpOsTaskletSche(IN RTMP_NET_TASK_STRUCT *pTasklet) {
	if (pTasklet->pContent == NULL)
		return FALSE;

#ifdef WORKQUEUE_BH
	schedule_work((struct work_struct *)(pTasklet->pContent));
#else
	tasklet_hi_schedule((OS_NET_TASK_STRUCT *) (pTasklet->pContent));
#endif /* WORKQUEUE_BH */

	return TRUE;
}

/*
========================================================================
Routine Description:
	Initialize a tasklet.

Arguments:
	pTasklet		- the tasklet

Return Value:
	TRUE or FALSE

Note:
========================================================================
*/
BOOLEAN RtmpOsTaskletInit(IN RTMP_NET_TASK_STRUCT *pTasklet,
			  IN VOID (*pFunc) (unsigned long data),
			  IN ULONG Data,
			  IN LIST_HEADER * pTaskletList) {
#ifdef WORKQUEUE_BH
	if (RTMP_OS_Alloc_Rsc(pTaskletList, pTasklet,
			      sizeof (struct work_struct)) == FALSE) {
		return FALSE;	/* allocate fail */
	}

	INIT_WORK((struct work_struct *)(pTasklet->pContent), pFunc);
#else

	if (RTMP_OS_Alloc_Rsc(pTaskletList, pTasklet,
			      sizeof (OS_NET_TASK_STRUCT)) == FALSE) {
		return FALSE;	/* allocate fail */
	}

	tasklet_init((OS_NET_TASK_STRUCT *) (pTasklet->pContent), pFunc, Data);
#endif /* WORKQUEUE_BH */

	return TRUE;
}

/*
========================================================================
Routine Description:
	Delete a tasklet.

Arguments:
	pTasklet		- the tasklet

Return Value:
	TRUE or FALSE

Note:
========================================================================
*/
BOOLEAN RtmpOsTaskletKill(IN RTMP_NET_TASK_STRUCT *pTasklet) {
	if (pTasklet->pContent != NULL) {
#ifndef WORKQUEUE_BH
		tasklet_kill((OS_NET_TASK_STRUCT *) (pTasklet->pContent));
#endif /* WORKQUEUE_BH */

		/* we will free all tasks memory in RTMP_OS_FREE_TASKLET() */
/*		os_free_mem(NULL, pTasklet->pContent); */
/*		pTasklet->pContent = NULL; */
	}

	return TRUE;
}

VOID RtmpOsTaskletDataAssign(IN RTMP_NET_TASK_STRUCT *pTasklet,
			     IN ULONG Data) {
#ifndef WORKQUEUE_BH
	if (pTasklet->pContent != NULL)
		((OS_NET_TASK_STRUCT *) (pTasklet->pContent))->data =
		    (ULONG) Data;
#endif /* WORKQUEUE_BH */
}

INT32 RtmpOsTaskIsKilled(IN RTMP_OS_TASK *pTaskOrg) {
	OS_TASK *pTask;

	pTask = (OS_TASK *) (pTaskOrg->pContent);
	if (pTask == NULL)
		return 1;
	return pTask->task_killed;
}

VOID RtmpOsTaskWakeUp(IN RTMP_OS_TASK *pTaskOrg) {
	OS_TASK *pTask;

	pTask = (OS_TASK *) (pTaskOrg->pContent);
	if (pTask == NULL)
		return;

#ifdef KTHREAD_SUPPORT
	WAKE_UP(pTask);
#else
	RTMP_SEM_EVENT_UP(&pTask->taskSema);
#endif
}

/*
========================================================================
Routine Description:
	Check if the task is legal.

Arguments:
	pPkt			- the packet
	pDev			- the device

Return Value:
	None

Note:
========================================================================
*/
BOOLEAN RtmpOsCheckTaskLegality(IN RTMP_OS_TASK *pTaskOrg) {
	OS_TASK *pTask;

	pTask = (OS_TASK *) (pTaskOrg->pContent);
	if (pTask == NULL)
		return FALSE;

#ifdef KTHREAD_SUPPORT
	if (pTask->kthread_task == NULL)
#else
	CHECK_PID_LEGALITY(pTask->taskPID);
	else
#endif
	return FALSE;

	return TRUE;
}

/* timeout -- ms */
VOID RTMP_SetPeriodicTimer(IN NDIS_MINIPORT_TIMER *pTimerOrg,
			   IN unsigned long timeout) {
	OS_NDIS_MINIPORT_TIMER *pTimer;

	pTimer = (OS_NDIS_MINIPORT_TIMER *) (pTimerOrg->pContent);
	if (pTimer != NULL) {
		__RTMP_SetPeriodicTimer(pTimer, timeout);
	}
}

/* convert NdisMInitializeTimer --> RTMP_OS_Init_Timer */
VOID RTMP_OS_Init_Timer(IN VOID *pReserved,
			IN NDIS_MINIPORT_TIMER *pTimerOrg,
			IN TIMER_FUNCTION function,
			IN PVOID data,
			IN LIST_HEADER *pTimerList) {
	OS_NDIS_MINIPORT_TIMER *pTimer;

	if (RTMP_OS_Alloc_Rsc(pTimerList, pTimerOrg,
			      sizeof (OS_NDIS_MINIPORT_TIMER)) == FALSE) {
		return;		/* allocate fail */
	}

	pTimer = (OS_NDIS_MINIPORT_TIMER *) (pTimerOrg->pContent);

	if (pTimer != NULL) {
		__RTMP_OS_Init_Timer(pReserved, pTimer, function, data);
	}
}

VOID RTMP_OS_Add_Timer(IN NDIS_MINIPORT_TIMER *pTimerOrg,
		       IN unsigned long timeout) {
	OS_NDIS_MINIPORT_TIMER *pTimer;

	 pTimer = (OS_NDIS_MINIPORT_TIMER *) (pTimerOrg->pContent);

	if (pTimer != NULL) {
		if (timer_pending(pTimer))
			return;

		__RTMP_OS_Add_Timer(pTimer,
				    timeout);
	}
} 

VOID RTMP_OS_Mod_Timer(IN NDIS_MINIPORT_TIMER *pTimerOrg,
			 IN unsigned long timeout) {
	OS_NDIS_MINIPORT_TIMER *pTimer;

	pTimer = (OS_NDIS_MINIPORT_TIMER *) (pTimerOrg->pContent);
	if (pTimer != NULL) {
		__RTMP_OS_Mod_Timer(pTimer, timeout);
	}
}

VOID RTMP_OS_Del_Timer(IN NDIS_MINIPORT_TIMER *pTimerOrg,
		       OUT BOOLEAN *pCancelled) {
	OS_NDIS_MINIPORT_TIMER *pTimer;

	pTimer = (OS_NDIS_MINIPORT_TIMER *) (pTimerOrg->pContent);
	if (pTimer != NULL) {
		__RTMP_OS_Del_Timer(pTimer, pCancelled);
	}
}

VOID RTMP_OS_Release_Timer(IN NDIS_MINIPORT_TIMER *pTimerOrg) {
	OS_NDIS_MINIPORT_TIMER *pTimer;

	pTimer = (OS_NDIS_MINIPORT_TIMER *) (pTimerOrg->pContent);
	if (pTimer != NULL) {
		__RTMP_OS_Release_Timer(pTimer);

		os_free_mem(NULL, pTimer);
		pTimerOrg->pContent = NULL;
	}
}

/*
========================================================================
Routine Description:
	Allocate a OS resource.

Arguments:
	pAd				- WLAN control block pointer
	pRsc			- the resource
	RscLen			- resource length

Return Value:
	TRUE or FALSE

Note:
========================================================================
*/
BOOLEAN RTMP_OS_Alloc_Rsc(IN LIST_HEADER *pRscList,
			  IN VOID *pRscSrc,
			  IN UINT32 RscLen) {
	OS_RSTRUC *pRsc = (OS_RSTRUC *) pRscSrc;

	if (pRsc->pContent == NULL) {
		/* new entry */
		os_alloc_mem(NULL, (UCHAR **) & (pRsc->pContent), RscLen);
		if (pRsc->pContent == NULL) {
			DBGPRINT(RT_DEBUG_ERROR,
				 ("%s: alloc timer fail!\n", __FUNCTION__));
			return FALSE;
		} else {
			LIST_RESOURCE_OBJ_ENTRY *pObj;

			/* allocate resource record entry */
			os_alloc_mem(NULL, (UCHAR **) & (pObj),
				     sizeof (LIST_RESOURCE_OBJ_ENTRY));
			if (pObj == NULL) {
				DBGPRINT(RT_DEBUG_ERROR,
					 ("%s: alloc timer obj fail!\n",
					  __FUNCTION__));
				os_free_mem(NULL, pRsc->pContent);
				pRsc->pContent = NULL;
				return FALSE;
			} else {
				memset(pRsc->pContent, 0, RscLen);
				pObj->pRscObj = (VOID *) pRsc;

				OS_SEM_LOCK(&UtilSemLock);
				insertTailList(pRscList, (LIST_ENTRY *) pObj);
				OS_SEM_UNLOCK(&UtilSemLock);
			}
		}
	}

	return TRUE;
}

/*
========================================================================
Routine Description:
	Free all timers.

Arguments:
	pAd				- WLAN control block pointer

Return Value:
	None

Note:
========================================================================
*/
VOID RTMP_OS_Free_Rscs(IN LIST_HEADER *pRscList) {
	LIST_RESOURCE_OBJ_ENTRY *pObj;
	OS_RSTRUC *pRsc;

	OS_SEM_LOCK(&UtilSemLock);
	while (TRUE) {
		pObj = (LIST_RESOURCE_OBJ_ENTRY *) removeHeadList(pRscList);
		if (pObj == NULL)
			break;
		pRsc = (OS_RSTRUC *) (pObj->pRscObj);

		if (pRsc->pContent != NULL) {
			/* free the timer memory */
			os_free_mem(NULL, pRsc->pContent);
			pRsc->pContent = NULL;
		} else {
			/*
			   The case is possible because some timers are released during
			   the driver life time, EX: we will release some timers in
			   MacTableDeleteEntry().
			   But we do not recommend the behavior, i.e. not to release
			   timers in the driver life time; Or we can not cancel the
			   timer for timer preemption problem.
			 */
		}

		os_free_mem(NULL, pObj);	/* free the timer record entry */
	}
	OS_SEM_UNLOCK(&UtilSemLock);
}

/*
========================================================================
Routine Description:
	Allocate a kernel task.

Arguments:
	pTask			- the task

Return Value:
	None

Note:
========================================================================
*/
BOOLEAN RtmpOSTaskAlloc(IN RTMP_OS_TASK *pTask,
			IN LIST_HEADER *pTaskList) {
	if (RTMP_OS_Alloc_Rsc(pTaskList, pTask, sizeof (OS_TASK)) == FALSE) {
		DBGPRINT(RT_DEBUG_ERROR,
			 ("%s: alloc task fail!\n", __FUNCTION__));
		return FALSE;	/* allocate fail */
	}

	return TRUE;
}

/*
========================================================================
Routine Description:
	Free a kernel task.

Arguments:
	pTask			- the task

Return Value:
	None

Note:
========================================================================
*/
VOID RtmpOSTaskFree(IN RTMP_OS_TASK *pTask) {
	/* we will free all tasks memory in RTMP_OS_FREE_TASK() */
}

/*
========================================================================
Routine Description:
	Kill a kernel task.

Arguments:
	pTaskOrg		- the task

Return Value:
	None

Note:
========================================================================
*/
NDIS_STATUS RtmpOSTaskKill(IN RTMP_OS_TASK *pTaskOrg) {
	OS_TASK *pTask;
	NDIS_STATUS Status;

	pTask = (OS_TASK *) (pTaskOrg->pContent);
	if (pTask != NULL) {
		Status = __RtmpOSTaskKill(pTask);
		RtmpOSTaskFree(pTaskOrg);
		return Status;
	}

	return NDIS_STATUS_FAILURE;
}

/*
========================================================================
Routine Description:
	Notify kernel the task exit.

Arguments:
	pTaskOrg		- the task

Return Value:
	None

Note:
========================================================================
*/
INT RtmpOSTaskNotifyToExit(IN RTMP_OS_TASK *pTaskOrg) {
	OS_TASK *pTask;

	pTask = (OS_TASK *) (pTaskOrg->pContent);
	if (pTask == NULL)
		return 0;
	return __RtmpOSTaskNotifyToExit(pTask);
}

/*
========================================================================
Routine Description:
	Customize the task.

Arguments:
	pTaskOrg		- the task

Return Value:
	None

Note:
========================================================================
*/
VOID RtmpOSTaskCustomize(IN RTMP_OS_TASK *pTaskOrg) {
	OS_TASK *pTask;

	pTask = (OS_TASK *) (pTaskOrg->pContent);
	if (pTask == NULL)
		return;
	__RtmpOSTaskCustomize(pTask);
}

/*
========================================================================
Routine Description:
	Activate a kernel task.

Arguments:
	pTaskOrg		- the task
	fn				- task handler
	arg				- task input argument

Return Value:
	None

Note:
========================================================================
*/
NDIS_STATUS RtmpOSTaskAttach(IN RTMP_OS_TASK *pTaskOrg,
			     IN RTMP_OS_TASK_CALLBACK fn,
			     IN ULONG arg) {
	OS_TASK *pTask;

	pTask = (OS_TASK *) (pTaskOrg->pContent);
	if (pTask == NULL)
		return NDIS_STATUS_FAILURE;
	return __RtmpOSTaskAttach(pTask, fn, arg);
}

/*
========================================================================
Routine Description:
	Initialize a kernel task.

Arguments:
	pTaskOrg		- the task
	pTaskName		- task name
	pPriv			- task input argument

Return Value:
	None

Note:
========================================================================
*/
NDIS_STATUS RtmpOSTaskInit(IN RTMP_OS_TASK *pTaskOrg,
			   IN PSTRING pTaskName,
			   IN VOID *pPriv,
			   IN LIST_HEADER *pTaskList,
			   IN LIST_HEADER *pSemList) {
	OS_TASK *pTask;

	if (RtmpOSTaskAlloc(pTaskOrg, pTaskList) == FALSE)
		return NDIS_STATUS_FAILURE;

	pTask = (OS_TASK *) (pTaskOrg->pContent);
	if (pTask == NULL)
		return NDIS_STATUS_FAILURE;

	return __RtmpOSTaskInit(pTask, pTaskName, pPriv, pSemList);
}

/*
========================================================================
Routine Description:
	Wait for a event in the task.

Arguments:
	pAd				- WLAN control block pointer
	pTaskOrg		- the task

Return Value:
	TRUE
	FALSE

Note:
========================================================================
*/
BOOLEAN RtmpOSTaskWait(IN VOID *pReserved,
		       IN RTMP_OS_TASK *pTaskOrg,
		       IN INT32 *pStatus) {
	OS_TASK *pTask;

	pTask = (OS_TASK *) (pTaskOrg->pContent);
	if (pTask == NULL)
		return FALSE;

	return __RtmpOSTaskWait(pReserved, pTask, pStatus);
}

/*
========================================================================
Routine Description:
	Get private data for the task.

Arguments:
	pTaskOrg		- the task

Return Value:
	None

Note:
========================================================================
*/
VOID *RtmpOsTaskDataGet(IN RTMP_OS_TASK *pTaskOrg) {
	if (pTaskOrg->pContent == NULL)
		return NULL;

	return (((OS_TASK *) (pTaskOrg->pContent))->priv);
}

/*
========================================================================
Routine Description:
	Allocate a lock.

Arguments:
	pLock			- the lock

Return Value:
	None

Note:
========================================================================
*/
BOOLEAN RtmpOsAllocateLock(IN NDIS_SPIN_LOCK *pLock,
			   IN LIST_HEADER *pLockList) {
	if (RTMP_OS_Alloc_Rsc(pLockList, pLock,
			      sizeof (OS_NDIS_SPIN_LOCK)) == FALSE) {
		DBGPRINT(RT_DEBUG_ERROR,
			 ("%s: alloc lock fail!\n", __FUNCTION__));
		return FALSE;	/* allocate fail */
	}

	OS_NdisAllocateSpinLock(pLock->pContent);
	return TRUE;
}

/*
========================================================================
Routine Description:
	Free a lock.

Arguments:
	pLockOrg		- the lock

Return Value:
	None

Note:
========================================================================
*/
VOID RtmpOsFreeSpinLock(IN NDIS_SPIN_LOCK *pLockOrg) {
	OS_NdisFreeSpinLock(pLock);

	/* we will free all locks memory in RTMP_OS_FREE_LOCK() */
}

/*
========================================================================
Routine Description:
	Spin lock bh.

Arguments:
	pLockOrg		- the lock

Return Value:
	None

Note:
========================================================================
*/
VOID RtmpOsSpinLockBh(IN NDIS_SPIN_LOCK *pLockOrg) {
	OS_NDIS_SPIN_LOCK *pLock;

	pLock = (OS_NDIS_SPIN_LOCK *) (pLockOrg->pContent);
	if (pLock != NULL) {
		OS_SEM_LOCK(pLock);
	} else
		printk("lock> warning! the lock was freed!\n");
}

/*
========================================================================
Routine Description:
	Spin unlock bh.

Arguments:
	pLockOrg		- the lock

Return Value:
	None

Note:
========================================================================
*/
VOID RtmpOsSpinUnLockBh(IN NDIS_SPIN_LOCK *pLockOrg) {
	OS_NDIS_SPIN_LOCK *pLock;

	pLock = (OS_NDIS_SPIN_LOCK *) (pLockOrg->pContent);
	if (pLock != NULL) {
		OS_SEM_UNLOCK(pLock);
	} else
		printk("lock> warning! the lock was freed!\n");
}

/*
========================================================================
Routine Description:
	Interrupt lock.

Arguments:
	pLockOrg		- the lock
	pIrqFlags		- the lock flags

Return Value:
	None

Note:
========================================================================
*/
VOID RtmpOsIntLock(IN NDIS_SPIN_LOCK *pLockOrg,
		   IN ULONG *pIrqFlags) {
	OS_NDIS_SPIN_LOCK *pLock;

	pLock = (OS_NDIS_SPIN_LOCK *) (pLockOrg->pContent);
	if (pLock != NULL) {
		OS_INT_LOCK(pLock, *pIrqFlags);
	} else
		printk("lock> warning! the lock was freed!\n");
}

/*
========================================================================
Routine Description:
	Interrupt unlock.

Arguments:
	pLockOrg		- the lock
	IrqFlags		- the lock flags

Return Value:
	None

Note:
========================================================================
*/
VOID RtmpOsIntUnLock(IN NDIS_SPIN_LOCK *pLockOrg,
		     IN ULONG IrqFlags) {
	OS_NDIS_SPIN_LOCK *pLock;

	pLock = (OS_NDIS_SPIN_LOCK *) (pLockOrg->pContent);
	if (pLock != NULL) {
		OS_INT_UNLOCK(pLock, IrqFlags);
	} else
		printk("lock> warning! the lock was freed!\n");
}

/*
========================================================================
Routine Description:
	Get MAC address for the network interface.

Arguments:
	pDev			- the device

Return Value:
	None

Note:
========================================================================
*/
unsigned char *RtmpOsNetDevGetPhyAddr(IN VOID *pDev) {
	return RTMP_OS_NETDEV_GET_PHYADDR((PNET_DEV) pDev);
}

/*
========================================================================
Routine Description:
	Start network interface TX queue.

Arguments:
	pDev			- the device

Return Value:
	None

Note:
========================================================================
*/
VOID RtmpOsNetQueueStart(IN PNET_DEV pDev) {
	RTMP_OS_NETDEV_START_QUEUE(pDev);
}

/*
========================================================================
Routine Description:
	Stop network interface TX queue.

Arguments:
	pDev			- the device

Return Value:
	None

Note:
========================================================================
*/
VOID RtmpOsNetQueueStop(IN PNET_DEV pDev) {
	RTMP_OS_NETDEV_STOP_QUEUE(pDev);
}

/*
========================================================================
Routine Description:
	Wake up network interface TX queue.

Arguments:
	pDev			- the device

Return Value:
	None

Note:
========================================================================
*/
VOID RtmpOsNetQueueWake(IN PNET_DEV pDev) {
	RTMP_OS_NETDEV_WAKE_QUEUE(pDev);
}

/*
========================================================================
Routine Description:
	Assign network interface to the packet.

Arguments:
	pPkt			- the packet
	pDev			- the device

Return Value:
	None

Note:
========================================================================
*/
VOID RtmpOsSetPktNetDev(IN VOID *pPkt,
			IN VOID *pDev) {
	SET_OS_PKT_NETDEV(pPkt, (PNET_DEV) pDev);
}

/*
========================================================================
Routine Description:
	Assign private data pointer to the network interface.

Arguments:
	pDev			- the device
	pPriv			- the pointer

Return Value:
	None

Note:
========================================================================
*/
VOID RtmpOsSetNetDevPriv(IN VOID *pDev,
			 IN VOID *pPriv) {
	RTMP_OS_NETDEV_SET_PRIV((PNET_DEV) pDev, pPriv);
}

/*
========================================================================
Routine Description:
	Get private data pointer from the network interface.

Arguments:
	pDev			- the device
	pPriv			- the pointer

Return Value:
	None

Note:
========================================================================
*/
VOID *RtmpOsGetNetDevPriv(IN VOID *pDev) {
	return (VOID *) RTMP_OS_NETDEV_GET_PRIV((PNET_DEV) pDev);
}

/*
========================================================================
Routine Description:
	Assign network interface type.

Arguments:
	pDev			- the device
	Type			- the type

Return Value:
	None

Note:
========================================================================
*/
VOID RtmpOsSetNetDevType(IN VOID *pDev,
			 IN USHORT Type) {
	RTMP_OS_NETDEV_SET_TYPE((PNET_DEV) pDev, Type);
}

/*
========================================================================
Routine Description:
	Assign network interface type for monitor mode.

Arguments:
	pDev			- the device
	Type			- the type

Return Value:
	None

Note:
========================================================================
*/
VOID RtmpOsSetNetDevTypeMonitor(IN VOID *pDev) {
	RTMP_OS_NETDEV_SET_TYPE((PNET_DEV) pDev, ARPHRD_IEEE80211_PRISM);
}

/*
========================================================================
Routine Description:
	Get PID.

Arguments:
	pPkt			- the packet
	pDev			- the device

Return Value:
	None

Note:
========================================================================
*/
VOID RtmpOsGetPid(IN ULONG *pDst,
		  IN ULONG PID) {
	RT_GET_OS_PID(*pDst, PID);
}

/*
========================================================================
Routine Description:
	Wait for a moment.

Arguments:
	Time			- micro second

Return Value:
	None

Note:
========================================================================
*/
VOID RtmpOsWait(IN UINT32 Time) {
	OS_WAIT(Time);
}

/*
========================================================================
Routine Description:
	Check if b is smaller than a.

Arguments:
	Time			- micro second

Return Value:
	None

Note:
========================================================================
*/
UINT32 RtmpOsTimerAfter(IN ULONG a,
			IN ULONG b) {
	return RTMP_TIME_AFTER(a, b);
}

/*
========================================================================
Routine Description:
	Check if b is not smaller than a.

Arguments:
	Time			- micro second

Return Value:
	None

Note:
========================================================================
*/
UINT32 RtmpOsTimerBefore(IN ULONG a,
			 IN ULONG b) {
	return RTMP_TIME_BEFORE(a, b);
}

/*
========================================================================
Routine Description:
	Get current system time.

Arguments:
	pTime			- system time (tick)

Return Value:
	None

Note:
========================================================================
*/
VOID RtmpOsGetSystemUpTime(IN ULONG *pTime) {
	NdisGetSystemUpTime(pTime);
}

/*
========================================================================
Routine Description:
	Get OS tick unit.

Arguments:
	pOps			- Utility table

Return Value:
	None

Note:
========================================================================
*/
UINT32 RtmpOsTickUnitGet(VOID) {
	return HZ;
}

/*
========================================================================
Routine Description:
	ntohs

Arguments:
	Value			- the value

Return Value:
	the value

Note:
========================================================================
*/
UINT16 RtmpOsNtohs(IN UINT16 Value) {
	return OS_NTOHS(Value);
}

/*
========================================================================
Routine Description:
	htons

Arguments:
	Value			- the value

Return Value:
	the value

Note:
========================================================================
*/
UINT16 RtmpOsHtons(IN UINT16 Value) {
	return OS_HTONS(Value);
}

/*
========================================================================
Routine Description:
	ntohl

Arguments:
	Value			- the value

Return Value:
	the value

Note:
========================================================================
*/
UINT32 RtmpOsNtohl(IN UINT32 Value) {
	return OS_NTOHL(Value);
}

/*
========================================================================
Routine Description:
	htonl

Arguments:
	Value			- the value

Return Value:
	the value

Note:
========================================================================
*/
UINT32 RtmpOsHtonl(IN UINT32 Value) {
	return OS_HTONL(Value);
}

/*
========================================================================
Routine Description:
	get_unaligned for 16-bit value.

Arguments:
	pWord			- the value

Return Value:
	the value

Note:
========================================================================
*/
UINT16 RtmpOsGetUnaligned(IN UINT16 *pWord) {
	return get_unaligned(pWord);
}

/*
========================================================================
Routine Description:
	get_unaligned for 32-bit value.

Arguments:
	pWord			- the value

Return Value:
	the value

Note:
========================================================================
*/
UINT32 RtmpOsGetUnaligned32(IN UINT32 *pWord) {
	return get_unaligned(pWord);
}

/*
========================================================================
Routine Description:
	get_unaligned for long-bit value.

Arguments:
	pWord			- the value

Return Value:
	the value

Note:
========================================================================
*/
ULONG RtmpOsGetUnalignedlong(IN ULONG *pWord) {
	return get_unaligned(pWord);
}

/*
========================================================================
Routine Description:
	Get maximum scan data length.

Arguments:
	None

Return Value:
	length

Note:
	Used in site servey.
========================================================================
*/
ULONG RtmpOsMaxScanDataGet(VOID) {
	return IW_SCAN_MAX_DATA;
}

/*
========================================================================
Routine Description:
	copy_from_user

Arguments:
	to				-
	from			-
	n				- size

Return Value:
	copy size

Note:
========================================================================
*/
ULONG RtmpOsCopyFromUser(OUT VOID *to,
			 IN const void *from,
			 IN ULONG n) {
	return (copy_from_user(to, from, n));
}

/*
========================================================================
Routine Description:
	copy_to_user

Arguments:
	to				-
	from			-
	n				- size

Return Value:
	copy size

Note:
========================================================================
*/
ULONG RtmpOsCopyToUser(OUT VOID *to,
		       IN const void *from,
		       IN ULONG n) {
	return (copy_to_user(to, from, n));
}

/*
========================================================================
Routine Description:
	Initialize a semaphore.

Arguments:
	pSem			- the semaphore

Return Value:
	TRUE			- Successfully
	FALSE			- Fail

Note:
========================================================================
*/
BOOLEAN RtmpOsSemaInitLocked(IN RTMP_OS_SEM *pSem,
			     IN LIST_HEADER *pSemList) {
	if (RTMP_OS_Alloc_Rsc(pSemList, pSem, sizeof (OS_SEM)) == FALSE) {
		DBGPRINT(RT_DEBUG_ERROR,
			 ("%s: alloc semaphore fail!\n", __FUNCTION__));
		return FALSE;	/* allocate fail */
	}

	OS_SEM_EVENT_INIT_LOCKED((OS_SEM *) (pSem->pContent));
	return TRUE;
}

/*
========================================================================
Routine Description:
	Initialize a semaphore.

Arguments:
	pSemOrg			- the semaphore

Return Value:
	TRUE			- Successfully
	FALSE			- Fail

Note:
========================================================================
*/
BOOLEAN RtmpOsSemaInit(IN RTMP_OS_SEM *pSem,
		       IN LIST_HEADER *pSemList) {
	if (RTMP_OS_Alloc_Rsc(pSemList, pSem, sizeof (OS_SEM)) == FALSE) {
		DBGPRINT(RT_DEBUG_ERROR,
			 ("%s: alloc semaphore fail!\n", __FUNCTION__));
		return FALSE;	/* allocate fail */
	}

	OS_SEM_EVENT_INIT((OS_SEM *) (pSem->pContent));
	return TRUE;
}

/*
========================================================================
Routine Description:
	Destroy a semaphore.

Arguments:
	pSemOrg			- the semaphore

Return Value:
	TRUE			- Successfully
	FALSE			- Fail

Note:
========================================================================
*/
BOOLEAN RtmpOsSemaDestory(IN RTMP_OS_SEM *pSemOrg) {
	OS_SEM *pSem;

	pSem = (OS_SEM *) (pSemOrg->pContent);
	if (pSem != NULL) {
		OS_SEM_EVENT_DESTORY(pSem);

		/* we will free all tasks memory in RTMP_OS_FREE_SEM() */
/*		os_free_mem(NULL, pSem); */
/*		pSemOrg->pContent = NULL; */
	} else
		printk("sem> warning! double-free sem!\n");
	return TRUE;
}

/*
========================================================================
Routine Description:
	Wait a semaphore.

Arguments:
	pSemOrg			- the semaphore

Return Value:
	0				- Successfully
	Otherwise		- Fail

Note:
========================================================================
*/
INT32 RtmpOsSemaWaitInterruptible(IN RTMP_OS_SEM *pSemOrg) {
	OS_SEM *pSem;
	INT32 Status = -1;

	pSem = (OS_SEM *) (pSemOrg->pContent);
	if (pSem != NULL)
		OS_SEM_EVENT_WAIT(pSem, Status);
	return Status;
}

/*
========================================================================
Routine Description:
	Wake up a semaphore.

Arguments:
	pSemOrg			- the semaphore

Return Value:
	None

Note:
========================================================================
*/
VOID RtmpOsSemaWakeUp(IN RTMP_OS_SEM *pSemOrg) {
	OS_SEM *pSem;

	pSem = (OS_SEM *) (pSemOrg->pContent);
	if (pSem != NULL)
		OS_SEM_EVENT_UP(pSem);
}

/*
========================================================================
Routine Description:
	Check if we are in a interrupt.

Arguments:
	None

Return Value:
	0				- No
	Otherwise		- Yes

Note:
========================================================================
*/
INT32 RtmpOsIsInInterrupt(VOID) {
	return (in_interrupt());
}

/*
========================================================================
Routine Description:
	Copy the data buffer to the packet frame body.

Arguments:
	pAd				- WLAN control block pointer
	pNetPkt			- the packet
	ThisFrameLen	- copy length
	pData			- the data buffer

Return Value:
	None

Note:
========================================================================
*/
VOID RtmpOsPktBodyCopy(IN PNET_DEV pNetDev,
		       IN PNDIS_PACKET pNetPkt,
		       IN ULONG ThisFrameLen,
		       IN PUCHAR pData) {
	memcpy(skb_put(pNetPkt, ThisFrameLen), pData, ThisFrameLen);
	SET_OS_PKT_NETDEV(pNetPkt, pNetDev);
	RTMP_SET_PACKET_SOURCE(OSPKT_TO_RTPKT(pNetPkt), PKTSRC_NDIS);
}

/*
========================================================================
Routine Description:
	Check if the packet is cloned.

Arguments:
	pNetPkt			- the packet

Return Value:
	TRUE			- Yes
	Otherwise		- No

Note:
========================================================================
*/
INT RtmpOsIsPktCloned(IN PNDIS_PACKET pNetPkt) {
	return OS_PKT_CLONED(pNetPkt);
}

/*
========================================================================
Routine Description:
	Duplicate a packet.

Arguments:
	pNetPkt			- the packet

Return Value:
	the new packet

Note:
========================================================================
*/
PNDIS_PACKET RtmpOsPktCopy(IN PNDIS_PACKET pNetPkt) {
	return skb_copy(RTPKT_TO_OSPKT(pNetPkt), GFP_ATOMIC);
}

/*
========================================================================
Routine Description:
	Clone a packet.

Arguments:
	pNetPkt			- the packet

Return Value:
	the cloned packet

Note:
========================================================================
*/
PNDIS_PACKET RtmpOsPktClone(IN PNDIS_PACKET pNetPkt) {
	return skb_clone(RTPKT_TO_OSPKT(pNetPkt), MEM_ALLOC_FLAG);
}

/*
========================================================================
Routine Description:
	Assign the data pointer for the packet.

Arguments:
	pNetPkt			- the packet
	*pData			- the data buffer

Return Value:
	None

Note:
========================================================================
*/
VOID RtmpOsPktDataPtrAssign(IN PNDIS_PACKET pNetPkt,
			    IN UCHAR *pData) {
	SET_OS_PKT_DATAPTR(pNetPkt, pData);
}

/*
========================================================================
Routine Description:
	Assign the data length for the packet.

Arguments:
	pNetPkt			- the packet
	Len				- the data length

Return Value:
	None

Note:
========================================================================
*/
VOID RtmpOsPktLenAssign(IN PNDIS_PACKET pNetPkt,
			IN LONG Len) {
	SET_OS_PKT_LEN(pNetPkt, Len);
}

/*
========================================================================
Routine Description:
	Adjust the tail pointer for the packet.

Arguments:
	pNetPkt			- the packet
	removedTagLen	- the size for adjustment

Return Value:
	None

Note:
========================================================================
*/
VOID RtmpOsPktTailAdjust(IN PNDIS_PACKET pNetPkt,
			 IN UINT removedTagLen) {
	OS_PKT_TAIL_ADJUST(pNetPkt, removedTagLen);
}

/*
========================================================================
Routine Description:
	Adjust the data pointer for the packet.

Arguments:
	pNetPkt			- the packet
	Len				- the size for adjustment

Return Value:
	the new data pointer for the packet

Note:
========================================================================
*/
PUCHAR RtmpOsPktTailBufExtend(IN PNDIS_PACKET pNetPkt,
			      IN UINT Len) {
	return OS_PKT_TAIL_BUF_EXTEND(pNetPkt, Len);
}

/*
========================================================================
Routine Description:
	adjust headroom for the packet.

Arguments:
	pNetPkt			- the packet
	Len				- the size for adjustment

Return Value:
	the new data pointer for the packet

Note:
========================================================================
*/
VOID RtmpOsPktReserve(IN PNDIS_PACKET pNetPkt,
		      IN UINT Len) {
	OS_PKT_RESERVE(pNetPkt, Len);
}

/*
========================================================================
Routine Description:
	Adjust the data pointer for the packet.

Arguments:
	pNetPkt			- the packet
	Len				- the size for adjustment

Return Value:
	the new data pointer for the packet

Note:
========================================================================
*/
PUCHAR RtmpOsPktHeadBufExtend(IN PNDIS_PACKET pNetPkt,
			      IN UINT Len) {
	return OS_PKT_HEAD_BUF_EXTEND(pNetPkt, Len);
}

/*
========================================================================
Routine Description:
	

Arguments:
	pPkt			- the packet

Return Value:
	None

Note:
========================================================================
*/
VOID RtmpOsPktInfPpaSend(IN PNDIS_PACKET pNetPkt) {
#ifdef INF_PPA_SUPPORT
	struct sk_buff *pRxPkt = RTPKT_TO_OSPKT(pNetPkt);
	int ret = 0;
	unsigned int ppa_flags = 0;	/* reserved for now */

	pRxPkt->protocol = eth_type_trans(pRxPkt, pRxPkt->dev);

	memset(pRxPkt->head, 0, pRxPkt->data - pRxPkt->head - 14);
	DBGPRINT(RT_DEBUG_TRACE,
		 ("ppa_hook_directpath_send_fn rx :ret:%d headroom:%d dev:%s pktlen:%d<===\n",
		  ret, skb_headroom(pRxPkt)
		  , pRxPkt->dev->name, pRxPkt->len));
	hex_dump("rx packet", pRxPkt->data, 32);
	ret =
	    ppa_hook_directpath_send_fn(pAd->g_if_id, pRxPkt, pRxPkt->len,
					ppa_flags);
#endif /* INF_PPA_SUPPORT */
}

INT32 RtmpThreadPidKill(IN RTMP_OS_PID PID) {
	return KILL_THREAD_PID(PID, SIGTERM, 1);
}

long RtmpOsSimpleStrtol(IN const char *cp, IN char **endp, IN unsigned int base) {
	return simple_strtol(cp,
			     endp,
			     base);
	return simple_strtol(cp, endp, base);
}

BOOLEAN RtmpOsPktOffsetInit(VOID) {
	struct sk_buff *pPkt = NULL;

	if ((RTPktOffsetData == 0) && (RTPktOffsetLen == 0)
	    && (RTPktOffsetCB == 0)) {
		pPkt = kmalloc(sizeof (struct sk_buff), GFP_ATOMIC);
		if (pPkt == NULL)
			return FALSE;

		RTPktOffsetData = (ULONG) (&(pPkt->data)) - (ULONG) pPkt;
		RTPktOffsetLen = (ULONG) (&(pPkt->len)) - (ULONG) pPkt;
		RTPktOffsetCB = (ULONG) (pPkt->cb) - (ULONG) pPkt;
		kfree(pPkt);

		DBGPRINT(RT_DEBUG_TRACE,
			 ("packet> data offset = %lu\n", RTPktOffsetData));
		DBGPRINT(RT_DEBUG_TRACE,
			 ("packet> len offset = %lu\n", RTPktOffsetLen));
		DBGPRINT(RT_DEBUG_TRACE,
			 ("packet> cb offset = %lu\n", RTPktOffsetCB));
	}

	return TRUE;
}

/*
========================================================================
Routine Description:
	Initialize the OS atomic_t.

Arguments:
	pAtomic			- the atomic

Return Value:
	TRUE			- allocation successfully
	FALSE			- allocation fail

Note:
========================================================================
*/
BOOLEAN RtmpOsAtomicInit(IN RTMP_OS_ATOMIC *pAtomic,
			 IN LIST_HEADER *pAtomicList) {
	if (RTMP_OS_Alloc_Rsc(pAtomicList, pAtomic, sizeof (atomic_t)) == FALSE) {
		DBGPRINT(RT_DEBUG_ERROR,
			 ("%s: alloc atomic fail!\n", __FUNCTION__));
		return FALSE;	/* allocate fail */
	}

	return TRUE;
}

/*
========================================================================
Routine Description:
	Atomic read a variable.

Arguments:
	pAtomic			- the atomic

Return Value:
	content

Note:
========================================================================
*/
LONG RtmpOsAtomicRead(IN RTMP_OS_ATOMIC *pAtomicSrc) {
	atomic_t *pAtomic;

	if (pAtomicSrc->pContent == NULL)
		return 0;

	pAtomic = (atomic_t *) (pAtomicSrc->pContent);
	return atomic_read(pAtomic);
}

/*
========================================================================
Routine Description:
	Atomic dec a variable.

Arguments:
	pAtomic			- the atomic

Return Value:
	content

Note:
========================================================================
*/
VOID RtmpOsAtomicDec(IN RTMP_OS_ATOMIC *pAtomicSrc) {
	atomic_t *pAtomic;

	if (pAtomicSrc->pContent == NULL)
		return;

	pAtomic = (atomic_t *) (pAtomicSrc->pContent);
	atomic_dec(pAtomic);
}

/*
========================================================================
Routine Description:
	Sets a 32-bit variable to the specified value as an atomic operation.

Arguments:
	pAtomic			- the atomic
	Value			- the value to be exchanged

Return Value:
	the initial value of the pAtomicSrc parameter

Note:
========================================================================
*/
VOID RtmpOsAtomicInterlockedExchange(IN RTMP_OS_ATOMIC *pAtomicSrc,
				     IN LONG Value) {
	atomic_t *pAtomic;

	if (pAtomicSrc->pContent == NULL)
		return;

	pAtomic = (atomic_t *) (pAtomicSrc->pContent);
	InterlockedExchange(pAtomic, Value);
}

/*
========================================================================
Routine Description:
	Initialize the OS utilities.

Arguments:
	pOps			- Utility table

Return Value:
	None

Note:
========================================================================
*/
VOID RtmpOsOpsInit(IN RTMP_OS_ABL_OPS *pOps) {
	pOps->ra_printk = (RTMP_PRINTK)printk;
	pOps->ra_snprintf = (RTMP_SNPRINTF)snprintf;
}


#else /* OS_ABL_FUNC_SUPPORT */

void RtmpOSFSInfoChange(IN RTMP_OS_FS_INFO *pOSFSInfoOrg,
			IN BOOLEAN bSet) {
	__RtmpOSFSInfoChange(pOSFSInfoOrg, bSet);
}

/* timeout -- ms */
VOID RTMP_SetPeriodicTimer(IN NDIS_MINIPORT_TIMER *pTimerOrg,
			   IN unsigned long timeout) {
	__RTMP_SetPeriodicTimer(pTimerOrg,
				timeout);
}

/* convert NdisMInitializeTimer --> RTMP_OS_Init_Timer */
VOID RTMP_OS_Init_Timer(
					  IN VOID *pReserved,
					  IN NDIS_MINIPORT_TIMER *pTimerOrg,
					  IN TIMER_FUNCTION function,
					  IN PVOID data,
					  IN LIST_HEADER *pTimerList) {
	__RTMP_OS_Init_Timer(pReserved, pTimerOrg, function, data);
}

VOID RTMP_OS_Add_Timer(IN NDIS_MINIPORT_TIMER *pTimerOrg,
		       IN unsigned long timeout) {
	__RTMP_OS_Add_Timer(pTimerOrg,
			    timeout);
}

VOID RTMP_OS_Mod_Timer(IN NDIS_MINIPORT_TIMER *pTimerOrg,
			 IN unsigned long timeout) {
	__RTMP_OS_Mod_Timer(pTimerOrg,
			    timeout);
}

VOID RTMP_OS_Del_Timer(IN NDIS_MINIPORT_TIMER *pTimerOrg,
			 OUT BOOLEAN *pCancelled) {
	__RTMP_OS_Del_Timer(pTimerOrg, pCancelled);
}

VOID RTMP_OS_Release_Timer(IN NDIS_MINIPORT_TIMER *pTimerOrg) {
	__RTMP_OS_Release_Timer(pTimerOrg);
}

NDIS_STATUS RtmpOSTaskKill(IN RTMP_OS_TASK * pTask) {
	return __RtmpOSTaskKill(pTask);
}

INT RtmpOSTaskNotifyToExit(IN RTMP_OS_TASK * pTask) {
	return __RtmpOSTaskNotifyToExit(pTask);
}

void RtmpOSTaskCustomize(IN RTMP_OS_TASK * pTask) {
	__RtmpOSTaskCustomize(pTask);
}

NDIS_STATUS RtmpOSTaskAttach(IN RTMP_OS_TASK * pTask,
			     IN RTMP_OS_TASK_CALLBACK fn,
			     IN ULONG arg) {
	return __RtmpOSTaskAttach(pTask, fn, arg);
}

NDIS_STATUS RtmpOSTaskInit(IN RTMP_OS_TASK * pTask,
			   IN PSTRING pTaskName,
			   IN VOID *pPriv,
			   IN LIST_HEADER * pTaskList,
			   IN LIST_HEADER * pSemList) {
	return __RtmpOSTaskInit(pTask, pTaskName, pPriv, pSemList);
}

BOOLEAN RtmpOSTaskWait(IN VOID *pReserved,
		       IN RTMP_OS_TASK * pTask,
		       IN INT32 *pStatus) {
	return __RtmpOSTaskWait(pReserved, pTask, pStatus);
}

VOID RtmpOsTaskWakeUp(IN RTMP_OS_TASK * pTask) {
#ifdef KTHREAD_SUPPORT
	WAKE_UP(pTask);
#else
	RTMP_SEM_EVENT_UP(&pTask->taskSema);
#endif
}

#endif /* OS_ABL_FUNC_SUPPORT */

/* End of rt_linux.c */
