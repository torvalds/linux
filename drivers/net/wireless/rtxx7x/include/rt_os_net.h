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


#ifndef __RT_OS_NET_H__
#define __RT_OS_NET_H__

#include "chip/chip_id.h"

typedef VOID *(*RTMP_NET_ETH_CONVERT_DEV_SEARCH)(
			IN	VOID			*net_dev_,
			IN	UCHAR			*pData);

typedef int (*RTMP_NET_PACKET_TRANSMIT)(
			IN	VOID			*pPacket);

#ifdef LINUX
#ifdef OS_ABL_FUNC_SUPPORT

/* ========================================================================== */
/* operators used in NETIF module */
/* Note: No need to put any compile option here */
typedef struct _RTMP_DRV_ABL_OPS {

NDIS_STATUS	(*RTMPAllocAdapterBlock)(
	IN  PVOID					handle,
	OUT	VOID					**ppAdapter);

VOID	(*RTMPFreeAdapter)(
	IN	VOID					*pAd);

BOOLEAN (*RtmpRaDevCtrlExit)(
	IN	VOID					*pAd);

INT (*RtmpRaDevCtrlInit)(
	IN	VOID					*pAd,
	IN	RTMP_INF_TYPE			infType);

VOID (*RTMPHandleInterrupt)(
	IN	VOID					*pAd);

INT (*RTMP_COM_IoctlHandle)(
	IN	VOID					*pAd,
	IN	RTMP_IOCTL_INPUT_STRUCT	*wrq,
	IN	INT						cmd,
	IN	USHORT					subcmd,
	IN	VOID					*pData,
	IN	ULONG					Data);

int (*RTMPSendPackets)(
	IN	NDIS_HANDLE				MiniportAdapterContext,
	IN	PPNDIS_PACKET			ppPacketArray,
	IN	UINT					NumberOfPackets,
	IN	UINT32					PktTotalLen,
	IN	RTMP_NET_ETH_CONVERT_DEV_SEARCH	Func);

int (*MBSS_PacketSend)(
	IN	PNDIS_PACKET				pPktSrc,
	IN	PNET_DEV					pDev,
	IN	RTMP_NET_PACKET_TRANSMIT	Func);

int (*WDS_PacketSend)(
	IN	PNDIS_PACKET				pPktSrc,
	IN	PNET_DEV					pDev,
	IN	RTMP_NET_PACKET_TRANSMIT	Func);

int (*APC_PacketSend)(
	IN	PNDIS_PACKET				pPktSrc,
	IN	PNET_DEV					pDev,
	IN	RTMP_NET_PACKET_TRANSMIT	Func);

int (*MESH_PacketSend)(
	IN	PNDIS_PACKET				pPktSrc,
	IN	PNET_DEV					pDev,
	IN	RTMP_NET_PACKET_TRANSMIT	Func);

int (*P2P_PacketSend)(
	IN	PNDIS_PACKET				pPktSrc,
	IN	PNET_DEV					pDev,
	IN	RTMP_NET_PACKET_TRANSMIT	Func);

INT (*RTMP_AP_IoctlHandle)(
	IN	VOID					*pAd,
	IN	RTMP_IOCTL_INPUT_STRUCT	*wrq,
	IN	INT						cmd,
	IN	USHORT					subcmd,
	IN	VOID					*pData,
	IN	ULONG					Data);

INT (*RTMP_STA_IoctlHandle)(
	IN	VOID					*pAd,
	IN	RTMP_IOCTL_INPUT_STRUCT	*wrq,
	IN	INT						cmd,
	IN	USHORT					subcmd,
	IN	VOID					*pData,
	IN	ULONG					Data,
	IN  USHORT                  priv_flags);

VOID (*RTMPDrvOpen)(
	IN	VOID					*pAd);

VOID (*RTMPDrvClose)(
	IN	VOID					*pAd,
	IN	VOID					*net_dev);

VOID (*RTMPInfClose)(
	IN	VOID					*pAd);

int (*rt28xx_init)(
	IN	VOID					*pAd,
	IN	PSTRING					pDefaultMac,
	IN	PSTRING					pHostName);
} RTMP_DRV_ABL_OPS;

extern RTMP_DRV_ABL_OPS *pRtmpDrvOps;

VOID RtmpDrvOpsInit(
	OUT 	VOID				*pDrvOpsOrg,
	INOUT	VOID				*pDrvNetOpsOrg,
	IN		RTMP_PCI_CONFIG		*pPciConfig,
	IN		RTMP_USB_CONFIG		*pUsbConfig);
#endif /* OS_ABL_FUNC_SUPPORT */
#endif /* LINUX */




/* ========================================================================== */
/* operators used in DRIVER module */
typedef void (*RTMP_DRV_USB_COMPLETE_HANDLER)(
			IN	VOID			*pURB);

typedef struct _RTMP_NET_ABL_OPS {

#ifdef RTMP_USB_SUPPORT
/* net complete handlers */
RTMP_DRV_USB_COMPLETE_HANDLER	RtmpNetUsbBulkOutDataPacketComplete;
RTMP_DRV_USB_COMPLETE_HANDLER	RtmpNetUsbBulkOutMLMEPacketComplete;
RTMP_DRV_USB_COMPLETE_HANDLER	RtmpNetUsbBulkOutNullFrameComplete;
RTMP_DRV_USB_COMPLETE_HANDLER	RtmpNetUsbBulkOutRTSFrameComplete;
RTMP_DRV_USB_COMPLETE_HANDLER	RtmpNetUsbBulkOutPsPollComplete;
RTMP_DRV_USB_COMPLETE_HANDLER	RtmpNetUsbBulkRxComplete;

/* drv complete handlers */
RTMP_DRV_USB_COMPLETE_HANDLER	RtmpDrvUsbBulkOutDataPacketComplete;
RTMP_DRV_USB_COMPLETE_HANDLER	RtmpDrvUsbBulkOutMLMEPacketComplete;
RTMP_DRV_USB_COMPLETE_HANDLER	RtmpDrvUsbBulkOutNullFrameComplete;
RTMP_DRV_USB_COMPLETE_HANDLER	RtmpDrvUsbBulkOutRTSFrameComplete;
RTMP_DRV_USB_COMPLETE_HANDLER	RtmpDrvUsbBulkOutPsPollComplete;
RTMP_DRV_USB_COMPLETE_HANDLER	RtmpDrvUsbBulkRxComplete;
#endif /* RTMP_USB_SUPPORT */

} RTMP_NET_ABL_OPS;

extern RTMP_NET_ABL_OPS *pRtmpDrvNetOps;

VOID RtmpNetOpsInit(
	IN VOID				*pNetOpsOrg);

VOID RtmpNetOpsSet(
	IN VOID				*pNetOpsOrg);




/* ========================================================================== */
#if defined(RTMP_MODULE_OS) && defined(OS_ABL_FUNC_SUPPORT)
/* for UTIL/NETIF module in OS ABL mode */

#define RTMPAllocAdapterBlock (((RTMP_DRV_ABL_OPS *)(pRtmpDrvOps))->RTMPAllocAdapterBlock)
#define RTMPFreeAdapter (((RTMP_DRV_ABL_OPS *)(pRtmpDrvOps))->RTMPFreeAdapter)
#define RtmpRaDevCtrlExit (((RTMP_DRV_ABL_OPS *)(pRtmpDrvOps))->RtmpRaDevCtrlExit)
#define RtmpRaDevCtrlInit (((RTMP_DRV_ABL_OPS *)(pRtmpDrvOps))->RtmpRaDevCtrlInit)
#define RTMPHandleInterrupt (((RTMP_DRV_ABL_OPS *)(pRtmpDrvOps))->RTMPHandleInterrupt)
#define RTMP_COM_IoctlHandle (((RTMP_DRV_ABL_OPS *)(pRtmpDrvOps))->RTMP_COM_IoctlHandle)
#define RTMPSendPackets (((RTMP_DRV_ABL_OPS *)(pRtmpDrvOps))->RTMPSendPackets)
#define MBSS_PacketSend (((RTMP_DRV_ABL_OPS *)(pRtmpDrvOps))->MBSS_PacketSend)
#define WDS_PacketSend (((RTMP_DRV_ABL_OPS *)(pRtmpDrvOps))->WDS_PacketSend)
#define APC_PacketSend (((RTMP_DRV_ABL_OPS *)(pRtmpDrvOps))->APC_PacketSend)
#define MESH_PacketSend (((RTMP_DRV_ABL_OPS *)(pRtmpDrvOps))->MESH_PacketSend)
#define P2P_PacketSend (((RTMP_DRV_ABL_OPS *)(pRtmpDrvOps))->P2P_PacketSend)
#define RTMP_AP_IoctlHandle (((RTMP_DRV_ABL_OPS *)(pRtmpDrvOps))->RTMP_AP_IoctlHandle)
#define RTMP_STA_IoctlHandle (((RTMP_DRV_ABL_OPS *)(pRtmpDrvOps))->RTMP_STA_IoctlHandle)
#define RTMPDrvOpen (((RTMP_DRV_ABL_OPS *)(pRtmpDrvOps))->RTMPDrvOpen)
#define RTMPDrvClose (((RTMP_DRV_ABL_OPS *)(pRtmpDrvOps))->RTMPDrvClose)
#define RTMPInfClose (((RTMP_DRV_ABL_OPS *)(pRtmpDrvOps))->RTMPInfClose)
#define rt28xx_init (((RTMP_DRV_ABL_OPS *)(pRtmpDrvOps))->rt28xx_init)

#else /* RTMP_MODULE_OS && OS_ABL_FUNC_SUPPORT */

NDIS_STATUS RTMPAllocAdapterBlock(
	IN	PVOID			handle,
	OUT VOID   			**ppAdapter);

VOID RTMPFreeAdapter(
	IN  VOID   			*pAd);

BOOLEAN RtmpRaDevCtrlExit(
	IN	VOID			*pAd);

INT RtmpRaDevCtrlInit(
	IN	VOID			*pAd,
	IN	RTMP_INF_TYPE	infType);

VOID RTMPHandleInterrupt(
	IN	VOID			*pAd);

INT RTMP_COM_IoctlHandle(
	IN	VOID					*pAd,
	IN	RTMP_IOCTL_INPUT_STRUCT	*wrq,
	IN	INT						cmd,
	IN	USHORT					subcmd,
	IN	VOID					*pData,
	IN	ULONG					Data);

int	RTMPSendPackets(
	IN	NDIS_HANDLE		MiniportAdapterContext,
	IN	PPNDIS_PACKET	ppPacketArray,
	IN	UINT			NumberOfPackets,
	IN	UINT32			PktTotalLen,
	IN	RTMP_NET_ETH_CONVERT_DEV_SEARCH	Func);

int MBSS_PacketSend(
	IN	PNDIS_PACKET				pPktSrc,
	IN	PNET_DEV					pDev,
	IN	RTMP_NET_PACKET_TRANSMIT	Func);

int WDS_PacketSend(
	IN	PNDIS_PACKET				pPktSrc,
	IN	PNET_DEV					pDev,
	IN	RTMP_NET_PACKET_TRANSMIT	Func);

int APC_PacketSend(
	IN	PNDIS_PACKET				pPktSrc,
	IN	PNET_DEV					pDev,
	IN	RTMP_NET_PACKET_TRANSMIT	Func);

int MESH_PacketSend(
	IN	PNDIS_PACKET				pPktSrc,
	IN	PNET_DEV					pDev,
	IN	RTMP_NET_PACKET_TRANSMIT	Func);

int P2P_PacketSend(
	IN	PNDIS_PACKET				pPktSrc,
	IN	PNET_DEV					pDev,
	IN	RTMP_NET_PACKET_TRANSMIT	Func);


#ifdef CONFIG_STA_SUPPORT
INT RTMP_STA_IoctlHandle(
	IN	VOID					*pAd,
	IN	RTMP_IOCTL_INPUT_STRUCT	*wrq,
	IN	INT						cmd,
	IN	USHORT					subcmd,
	IN	VOID					*pData,
	IN	ULONG					Data,
	IN  USHORT                  priv_flags );
#endif /* CONFIG_STA_SUPPORT */

VOID RTMPDrvOpen(
	IN VOID						*pAd);

VOID RTMPDrvClose(
	IN VOID						*pAd,
	IN VOID						*net_dev);

VOID RTMPInfClose(
	IN VOID						*pAd);

int rt28xx_init(
	IN VOID						*pAd,
	IN PSTRING					pDefaultMac,
	IN PSTRING					pHostName);

PNET_DEV RtmpPhyNetDevMainCreate(
	IN VOID						*pAd);
#endif /* RTMP_MODULE_OS */




/* ========================================================================== */
int rt28xx_close(VOID *dev);
int rt28xx_open(VOID *dev);

__inline INT VIRTUAL_IF_UP(VOID *pAd)
{
	RT_CMD_INF_UP_DOWN InfConf = { rt28xx_open, rt28xx_close };
	if (RTMP_COM_IoctlHandle(pAd, NULL, CMD_RTPRIV_IOCTL_VIRTUAL_INF_UP,
						0, &InfConf, 0) != NDIS_STATUS_SUCCESS)
		return -1;
	return 0;
}

__inline VOID VIRTUAL_IF_DOWN(VOID *pAd)
{
	RT_CMD_INF_UP_DOWN InfConf = { rt28xx_open, rt28xx_close };
	RTMP_COM_IoctlHandle(pAd, NULL, CMD_RTPRIV_IOCTL_VIRTUAL_INF_DOWN,
						0, &InfConf, 0);
	return;
}

#ifdef RTMP_MODULE_OS


#ifdef CONFIG_STA_SUPPORT
INT rt28xx_sta_ioctl(
	IN	PNET_DEV		net_dev,
	IN	OUT	struct ifreq	*rq,
	IN	INT			cmd);
#endif /* CONFIG_STA_SUPPORT */

PNET_DEV RtmpPhyNetDevInit(
	IN VOID						*pAd,
	IN RTMP_OS_NETDEV_OP_HOOK	*pNetHook);

BOOLEAN RtmpPhyNetDevExit(
	IN VOID						*pAd,
	IN PNET_DEV					net_dev);

#endif /* RTMP_MODULE_OS && OS_ABL_FUNC_SUPPORT */


VOID RT28xx_MBSS_Init(
	IN VOID *pAd,
	IN PNET_DEV main_dev_p);
VOID RT28xx_MBSS_Remove(
	IN VOID *pAd);
INT MBSS_VirtualIF_Open(
	IN	PNET_DEV			dev_p);
INT MBSS_VirtualIF_Close(
	IN	PNET_DEV			dev_p);
INT MBSS_VirtualIF_PacketSend(
	IN PNDIS_PACKET			skb_p,
	IN PNET_DEV				dev_p);
INT MBSS_VirtualIF_Ioctl(
	IN PNET_DEV				dev_p,
	IN OUT VOID 			*rq_p,
	IN INT cmd);

VOID RT28xx_WDS_Init(
	IN VOID					*pAd,
	IN PNET_DEV				net_dev);
INT WdsVirtualIFSendPackets(
	IN PNDIS_PACKET			pSkb,
	IN PNET_DEV				dev);
INT WdsVirtualIF_open(
	IN	PNET_DEV			dev);
INT WdsVirtualIF_close(
	IN PNET_DEV				dev);
INT WdsVirtualIF_ioctl(
	IN PNET_DEV				net_dev,
	IN OUT VOID				*rq,
	IN INT					cmd);
VOID RT28xx_WDS_Remove(
	IN VOID					*pAd);

VOID RT28xx_ApCli_Init(
	IN VOID 				*pAd,
	IN PNET_DEV				main_dev_p);
INT ApCli_VirtualIF_Open(
	IN PNET_DEV				dev_p);
INT ApCli_VirtualIF_Close(
	IN	PNET_DEV			dev_p);
INT ApCli_VirtualIF_PacketSend(
	IN PNDIS_PACKET 		pPktSrc,
	IN PNET_DEV				pDev);
INT ApCli_VirtualIF_Ioctl(
	IN PNET_DEV				dev_p,
	IN OUT VOID 			*rq_p,
	IN INT 					cmd);
VOID RT28xx_ApCli_Remove(
	IN VOID 				*pAd);

VOID RTMP_Mesh_Init(
	IN VOID					*pAd,
	IN PNET_DEV				main_dev_p,
	IN PSTRING				pHostName);
VOID RTMP_Mesh_Remove(
	IN VOID 				*pAd);
INT Mesh_VirtualIF_Open(
	IN PNET_DEV				pDev);
INT Mesh_VirtualIF_Close(
	IN	PNET_DEV			pDev);
INT Mesh_VirtualIF_PacketSend(
	IN PNDIS_PACKET 		pPktSrc,
	IN PNET_DEV				pDev);
INT Mesh_VirtualIF_Ioctl(
	IN PNET_DEV				dev_p,
	IN OUT VOID				*rq_p,
	IN INT 					cmd);

VOID RTMP_P2P_Init(
		 IN VOID			 *pAd,
		 IN PNET_DEV main_dev_p);

 INT P2P_VirtualIF_Open(
	 IN  PNET_DEV	 dev_p);

 INT P2P_VirtualIF_Close(
	 IN  PNET_DEV	 dev_p);

 INT P2P_VirtualIF_PacketSend(
	 IN PNDIS_PACKET	 skb_p,
	 IN PNET_DEV		 dev_p);

 INT P2P_VirtualIF_Ioctl(
	 IN PNET_DEV			 dev_p,
	 IN OUT VOID	 *rq_p,
	 IN INT cmd);

VOID RTMP_P2P_Remove(
	IN VOID				*pAd);


/* communication with RALINK DRIVER module in NET module */
/* general */
#define RTMP_DRIVER_NET_DEV_GET(__pAd, __pNetDev)							\
	RTMP_COM_IoctlHandle(__pAd, NULL, CMD_RTPRIV_IOCTL_NETDEV_GET, 0, __pNetDev, 0)

#define RTMP_DRIVER_NET_DEV_SET(__pAd, __pNetDev)							\
	RTMP_COM_IoctlHandle(__pAd, NULL, CMD_RTPRIV_IOCTL_NETDEV_SET, 0, __pNetDev, 0)

#define RTMP_DRIVER_OP_MODE_GET(__pAd, __pOpMode)							\
	RTMP_COM_IoctlHandle(__pAd, NULL, CMD_RTPRIV_IOCTL_OPMODE_GET, 0, __pOpMode, 0)

#define RTMP_DRIVER_IW_STATS_GET(__pAd, __pIwStats)							\
	RTMP_COM_IoctlHandle(__pAd, NULL, CMD_RTPRIV_IOCTL_INF_IW_STATUS_GET, 0, __pIwStats, 0)

#define RTMP_DRIVER_INF_STATS_GET(__pAd, __pInfStats)						\
	RTMP_COM_IoctlHandle(__pAd, NULL, CMD_RTPRIV_IOCTL_INF_STATS_GET, 0, __pInfStats, 0)

#define RTMP_DRIVER_INF_TYPE_GET(__pAd, __pInfType)							\
	RTMP_COM_IoctlHandle(__pAd, NULL, CMD_RTPRIV_IOCTL_INF_TYPE_GET, 0, __pInfType, 0)

#define RTMP_DRIVER_TASK_LIST_GET(__pAd, __pList)							\
	RTMP_COM_IoctlHandle(__pAd, NULL, CMD_RTPRIV_IOCTL_TASK_LIST_GET, 0, __pList, 0)

#define RTMP_DRIVER_NIC_NOT_EXIST_SET(__pAd)								\
	RTMP_COM_IoctlHandle(__pAd, NULL, CMD_RTPRIV_IOCTL_NIC_NOT_EXIST, 0, NULL, 0)

#ifdef CONFIG_APSTA_MIXED_SUPPORT
#define RTMP_DRIVER_MAX_IN_BITS_SET(__pAd, __MaxInBit)							\
	RTMP_COM_IoctlHandle(__pAd, NULL, CMD_RTPRIV_IOCTL_MAX_IN_BIT, 0, NULL, __MaxInBit)
#endif /* CONFIG_APSTA_MIXED_SUPPORT */
#ifdef CONFIG_STA_SUPPORT

#define RTMP_DRIVER_ADAPTER_END_DISSASSOCIATE(__pAd)								\
	RTMP_COM_IoctlHandle(__pAd, NULL, CMD_RTPRIV_IOCTL_ADAPTER_SEND_DISSASSOCIATE, 0, NULL, 0)

#ifdef CONFIG_PM
#ifdef USB_SUPPORT_SELECTIVE_SUSPEND

#define RTMP_DRIVER_USB_DEV_GET(__pAd, __pUsbDev)                                                       \
        RTMP_COM_IoctlHandle(__pAd, NULL, CMD_RTPRIV_IOCTL_USB_DEV_GET, 0, __pUsbDev, 0)

#define RTMP_DRIVER_USB_INTF_GET(__pAd, __pUsbIntf)                                                     \
        RTMP_COM_IoctlHandle(__pAd, NULL, CMD_RTPRIV_IOCTL_USB_INTF_GET, 0, __pUsbIntf, 0)

#define RTMP_DRIVER_ADAPTER_SUSPEND_SET(__pAd)								\
	RTMP_COM_IoctlHandle(__pAd, NULL, CMD_RTPRIV_IOCTL_ADAPTER_SUSPEND_SET, 0, NULL, 0)

#define RTMP_DRIVER_ADAPTER_SUSPEND_CLEAR(__pAd)								\
	RTMP_COM_IoctlHandle(__pAd, NULL, CMD_RTPRIV_IOCTL_ADAPTER_SUSPEND_CLEAR, 0, NULL, 0)

#define RTMP_DRIVER_ADAPTER_SUSPEND_TEST(__pAd, __flag)							\
	RTMP_COM_IoctlHandle(__pAd, NULL, CMD_RTPRIV_IOCTL_ADAPTER_SUSPEND_TEST, 0,  __flag, 0)

#define RTMP_DRIVER_ADAPTER_CPU_SUSPEND_SET(__pAd)								\
	RTMP_COM_IoctlHandle(__pAd, NULL, CMD_RTPRIV_IOCTL_ADAPTER_CPU_SUSPEND_SET, 0, NULL, 0)

#define RTMP_DRIVER_ADAPTER_CPU_SUSPEND_CLEAR(__pAd)								\
	RTMP_COM_IoctlHandle(__pAd, NULL, CMD_RTPRIV_IOCTL_ADAPTER_CPU_SUSPEND_CLEAR, 0, NULL, 0)

#define RTMP_DRIVER_ADAPTER_CPU_SUSPEND_TEST(__pAd, __flag)								\
	RTMP_COM_IoctlHandle(__pAd, NULL, CMD_RTPRIV_IOCTL_ADAPTER_CPU_SUSPEND_TEST, 0,  __flag, 0)

#define RTMP_DRIVER_ADAPTER_IDLE_RADIO_OFF_TEST(__pAd, __flag)								\
	RTMP_COM_IoctlHandle(__pAd, NULL, CMD_RTPRIV_IOCTL_ADAPTER_IDLE_RADIO_OFF_TEST, 0,  __flag, 0)

#define RTMP_DRIVER_ADAPTER_RT28XX_USB_ASICRADIO_OFF(__pAd)								\
	RTMP_COM_IoctlHandle(__pAd, NULL, CMD_RTPRIV_IOCTL_ADAPTER_RT28XX_USB_ASICRADIO_OFF, 0, NULL, 0)

#define RTMP_DRIVER_ADAPTER_RT28XX_USB_ASICRADIO_ON(__pAd)								\
	RTMP_COM_IoctlHandle(__pAd, NULL, CMD_RTPRIV_IOCTL_ADAPTER_RT28XX_USB_ASICRADIO_ON, 0, NULL, 0)

#endif /* USB_SUPPORT_SELECTIVE_SUSPEND */
#endif /* CONFIG_PM */

#define RTMP_DRIVER_AP_SSID_GET(__pAd, pData)								\
	RTMP_COM_IoctlHandle(__pAd, NULL, CMD_RTPRIV_IOCTL_AP_BSSID_GET, 0, pData, 0)
#endif /* CONFIG_STA_SUPPORT */

#define RTMP_DRIVER_VIRTUAL_INF_NUM_GET(__pAd, __pIfNum)					\
	RTMP_COM_IoctlHandle(__pAd, NULL, CMD_RTPRIV_IOCTL_VIRTUAL_INF_GET, 0, __pIfNum, 0)

#define RTMP_DRIVER_CHANNEL_GET(__pAd, __Channel)							\
	RTMP_COM_IoctlHandle(__pAd, NULL, CMD_RTPRIV_IOCTL_SIOCGIWFREQ, 0, __Channel, 0)

#define RTMP_DRIVER_IOCTL_SANITY_CHECK(__pAd)								\
	RTMP_COM_IoctlHandle(__pAd, NULL, CMD_RTPRIV_IOCTL_SANITY_CHECK, 0, NULL, 0)

#define RTMP_DRIVER_BITRATE_GET(__pAd, __pBitRate)							\
	RTMP_COM_IoctlHandle(__pAd, NULL, CMD_RTPRIV_IOCTL_AP_SIOCGIWRATEQ, 0, __pBitRate, 0)

#define RTMP_DRIVER_MAIN_INF_CREATE(__pAd, __ppNetDev)						\
	RTMP_COM_IoctlHandle(__pAd, NULL, CMD_RTPRIV_IOCTL_INF_MAIN_CREATE, 0, __ppNetDev, 0)

#define RTMP_DRIVER_MAIN_INF_GET(__pAd, __pInfId)							\
	RTMP_COM_IoctlHandle(__pAd, NULL, CMD_RTPRIV_IOCTL_INF_MAIN_ID_GET, 0, __pInfId, 0)

#define RTMP_DRIVER_MAIN_INF_CHECK(__pAd, __InfId)							\
	RTMP_COM_IoctlHandle(__pAd, NULL, CMD_RTPRIV_IOCTL_INF_MAIN_CHECK, 0, NULL, __InfId)

#define RTMP_DRIVER_P2P_INF_CHECK(__pAd, __InfId)							\
	RTMP_COM_IoctlHandle(__pAd, NULL, CMD_RTPRIV_IOCTL_INF_P2P_CHECK, 0, NULL, __InfId)

/* cfg80211 */
#define RTMP_DRIVER_CFG80211_START(__pAd)									\
	RTMP_COM_IoctlHandle(__pAd, NULL, CMD_RTPRIV_IOCTL_CFG80211_CFG_START, 0, NULL, 0)


#ifdef RT_CFG80211_SUPPORT
#define RTMP_DRIVER_80211_CB_GET(__pAd, __ppCB)							\
	RTMP_COM_IoctlHandle(__pAd, NULL, CMD_RTPRIV_IOCTL_80211_CB_GET, 0, __ppCB, 0)
#define RTMP_DRIVER_80211_CB_SET(__pAd, __pCB)							\
	RTMP_COM_IoctlHandle(__pAd, NULL, CMD_RTPRIV_IOCTL_80211_CB_SET, 0, __pCB, 0)
#define RTMP_DRIVER_80211_CHAN_SET(__pAd, __pChan)						\
	RTMP_COM_IoctlHandle(__pAd, NULL, CMD_RTPRIV_IOCTL_80211_CHAN_SET, 0, __pChan, 0)
#define RTMP_DRIVER_80211_VIF_SET(__pAd, __Filter, __IfType)			\
	RTMP_COM_IoctlHandle(__pAd, NULL, CMD_RTPRIV_IOCTL_80211_VIF_CHG, 0, &__Filter, __IfType)
#define RTMP_DRIVER_80211_SCAN(__pAd)									\
	RTMP_COM_IoctlHandle(__pAd, NULL, CMD_RTPRIV_IOCTL_80211_SCAN, 0, NULL, 0)
#define RTMP_DRIVER_80211_IBSS_JOIN(__pAd, __pInfo)						\
	RTMP_COM_IoctlHandle(__pAd, NULL, CMD_RTPRIV_IOCTL_80211_IBSS_JOIN, 0, __pInfo, 0)
#define RTMP_DRIVER_80211_STA_LEAVE(__pAd)								\
	RTMP_COM_IoctlHandle(__pAd, NULL, CMD_RTPRIV_IOCTL_80211_STA_LEAVE, 0, NULL, 0)
#define RTMP_DRIVER_80211_STA_GET(__pAd, __pStaInfo)					\
	RTMP_COM_IoctlHandle(__pAd, NULL, CMD_RTPRIV_IOCTL_80211_STA_GET, 0, __pStaInfo, 0)
#define RTMP_DRIVER_80211_KEY_ADD(__pAd, __pKeyInfo)					\
	RTMP_COM_IoctlHandle(__pAd, NULL, CMD_RTPRIV_IOCTL_80211_KEY_ADD, 0, __pKeyInfo, 0)
#define RTMP_DRIVER_80211_KEY_DEFAULT_SET(__pAd, __KeyId)				\
	RTMP_COM_IoctlHandle(__pAd, NULL, CMD_RTPRIV_IOCTL_80211_KEY_DEFAULT_SET, 0, NULL, __KeyId)
#define RTMP_DRIVER_80211_CONNECT(__pAd, __pConnInfo)					\
	RTMP_COM_IoctlHandle(__pAd, NULL, CMD_RTPRIV_IOCTL_80211_CONNECT_TO, 0, __pConnInfo, 0)
#define RTMP_DRIVER_80211_RFKILL(__pAd, __pActive)						\
	RTMP_COM_IoctlHandle(__pAd, NULL, CMD_RTPRIV_IOCTL_80211_RFKILL, 0, __pActive, 0)
#define RTMP_DRIVER_80211_REG_NOTIFY(__pAd, __pNotify)					\
	RTMP_COM_IoctlHandle(__pAd, NULL, CMD_RTPRIV_IOCTL_80211_REG_NOTIFY_TO, 0, __pNotify, 0)
#define RTMP_DRIVER_80211_UNREGISTER(__pAd, __pNetDev)					\
	RTMP_COM_IoctlHandle(__pAd, NULL, CMD_RTPRIV_IOCTL_80211_UNREGISTER, 0, __pNetDev, 0)
#define RTMP_DRIVER_80211_BANDINFO_GET(__pAd, __pBandInfo)				\
	RTMP_COM_IoctlHandle(__pAd, NULL, CMD_RTPRIV_IOCTL_80211_BANDINFO_GET, 0, __pBandInfo, 0)
#endif /* RT_CFG80211_SUPPORT */

/* mesh */
#define RTMP_DRIVER_MESH_REMOVE(__pAd)										\
	RTMP_COM_IoctlHandle(__pAd, NULL, CMD_RTPRIV_IOCTL_MESH_REMOVE, 0, NULL, 0)

/* inf ppa */
#define RTMP_DRIVER_INF_PPA_INIT(__pAd)										\
	RTMP_COM_IoctlHandle(__pAd, NULL, CMD_RTPRIV_IOCTL_INF_PPA_INIT, 0, NULL, 0)

#define RTMP_DRIVER_INF_PPA_EXIT(__pAd)										\
	RTMP_COM_IoctlHandle(__pAd, NULL, CMD_RTPRIV_IOCTL_INF_PPA_EXIT, 0, NULL, 0)

/* pci */
#define RTMP_DRIVER_IRQ_INIT(__pAd)											\
	RTMP_COM_IoctlHandle(__pAd, NULL, CMD_RTPRIV_IOCTL_IRQ_INIT, 0, NULL, 0)

#define RTMP_DRIVER_IRQ_RELEASE(__pAd)										\
	RTMP_COM_IoctlHandle(__pAd, NULL, CMD_RTPRIV_IOCTL_IRQ_RELEASE, 0, NULL, 0)

#define RTMP_DRIVER_PCI_MSI_ENABLE(__pAd, __pPciDev)						\
	RTMP_COM_IoctlHandle(__pAd, NULL, CMD_RTPRIV_IOCTL_MSI_ENABLE, 0, __pPciDev, 0)

#define RTMP_DRIVER_PCI_SUSPEND(__pAd)										\
	RTMP_COM_IoctlHandle(__pAd, NULL, CMD_RTPRIV_IOCTL_PCI_SUSPEND, 0, NULL, 0)

#define RTMP_DRIVER_PCI_RESUME(__pAd)										\
	RTMP_COM_IoctlHandle(__pAd, NULL, CMD_RTPRIV_IOCTL_PCI_RESUME, 0, NULL, 0)

#define RTMP_DRIVER_PCI_CSR_SET(__pAd, __Address)							\
	RTMP_COM_IoctlHandle(__pAd, NULL, CMD_RTPRIV_IOCTL_PCI_CSR_SET, 0, NULL, __Address)

#define RTMP_DRIVER_PCIE_INIT(__pAd, __pPciDev)								\
{																			\
	RT_CMD_PCIE_INIT __Config, *__pConfig = &__Config;						\
	__pConfig->pPciDev = __pPciDev;											\
	__pConfig->ConfigDeviceID = PCI_DEVICE_ID;								\
	__pConfig->ConfigSubsystemVendorID = PCI_SUBSYSTEM_VENDOR_ID;			\
	__pConfig->ConfigSubsystemID = PCI_SUBSYSTEM_ID;						\
	RTMP_COM_IoctlHandle(__pAd, NULL, CMD_RTPRIV_IOCTL_PCIE_INIT, 0, __pConfig, 0);\
}

/* usb */
#define RTMP_DRIVER_USB_MORE_FLAG_SET(__pAd, __pConfig)						\
	RTMP_COM_IoctlHandle(__pAd, NULL, CMD_RTPRIV_IOCTL_USB_MORE_FLAG_SET, 0, __pConfig, 0)

#define RTMP_DRIVER_USB_CONFIG_INIT(__pAd, __pConfig)						\
	RTMP_COM_IoctlHandle(__pAd, NULL, CMD_RTPRIV_IOCTL_USB_CONFIG_INIT, 0, __pConfig, 0)

#define RTMP_DRIVER_USB_SUSPEND(__pAd, __bIsRunning)						\
	RTMP_COM_IoctlHandle(__pAd, NULL, CMD_RTPRIV_IOCTL_USB_SUSPEND, 0, NULL, __bIsRunning)

#define RTMP_DRIVER_USB_RESUME(__pAd)										\
	RTMP_COM_IoctlHandle(__pAd, NULL, CMD_RTPRIV_IOCTL_USB_RESUME, 0, NULL, 0)

/* ap */
#define RTMP_DRIVER_AP_BITRATE_GET(__pAd, __pConfig)							\
	RTMP_AP_IoctlHandle(__pAd, NULL, CMD_RTPRIV_IOCTL_AP_SIOCGIWRATEQ, 0, __pConfig, 0)

#define RTMP_DRIVER_AP_MAIN_OPEN(__pAd)										\
	RTMP_AP_IoctlHandle(__pAd, NULL, CMD_RTPRIV_IOCTL_MAIN_OPEN, 0, NULL, 0)

/* sta */
#define RTMP_DRIVER_STA_DEV_TYPE_SET(__pAd, __Type)							\
	RTMP_STA_IoctlHandle(__pAd, NULL, CMD_RTPRIV_IOCTL_ORI_DEV_TYPE_SET, 0, NULL, __Type, __Type)

#define RTMP_DRIVER_MAC_ADDR_GET(__pAd, __pMacAddr)							\
	RTMP_COM_IoctlHandle(__pAd, NULL, CMD_RTPRIV_IOCTL_MAC_ADDR_GET, 0, __pMacAddr, 0)

#endif /* __RT_OS_NET_H__ */

/* End of rt_os_net.h */
