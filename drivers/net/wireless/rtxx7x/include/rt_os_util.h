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


#ifndef __RT_OS_UTIL_H__
#define __RT_OS_UTIL_H__

/* ============================ rt_linux.c ================================== */
/* General */
VOID RtmpUtilInit(VOID);

/* OS Time */
VOID RTMPusecDelay(
	IN	ULONG					usec);

VOID RtmpOsMsDelay(
	IN	ULONG					msec);

void RTMP_GetCurrentSystemTime(
	IN	LARGE_INTEGER			*time);

void RTMP_GetCurrentSystemTick(
	IN	ULONG					*pNow);

VOID RtmpOsWait(
	IN	UINT32					Time);

UINT32 RtmpOsTimerAfter(
	IN	ULONG					a,
	IN	ULONG					b);

UINT32 RtmpOsTimerBefore(
	IN	ULONG					a,
	IN	ULONG					b);

VOID RtmpOsGetSystemUpTime(
	IN	ULONG					*pTime);

UINT32 RtmpOsTickUnitGet(VOID);

/* OS Memory */
NDIS_STATUS os_alloc_mem(
	IN	VOID					*pReserved,
	OUT	UCHAR					**mem,
	IN	ULONG					size);

NDIS_STATUS os_alloc_mem_suspend(
	IN	VOID					*pReserved,
	OUT	UCHAR					**mem,
	IN	ULONG					size);

NDIS_STATUS os_free_mem(
	IN	VOID					*pReserved,
	IN	PVOID					mem);

NDIS_STATUS AdapterBlockAllocateMemory(
	IN	PVOID					handle,
	OUT	PVOID					*ppAd,
	IN	UINT32					SizeOfpAd);

VOID *RtmpOsVmalloc(
	IN	ULONG					Size);

VOID RtmpOsVfree(
	IN	VOID					*pMem);

ULONG RtmpOsCopyFromUser(
	OUT	VOID					*to,
	IN	const void				*from,
	IN	ULONG					n);

ULONG RtmpOsCopyToUser(
	OUT VOID					*to,
	IN	const void				*from,
	IN	ULONG					n);

BOOLEAN RtmpOsStatsAlloc(
	IN	VOID					**ppStats,
	IN	VOID					**ppIwStats);

/* OS Packet */
PNDIS_PACKET RtmpOSNetPktAlloc(
	IN	VOID					*pReserved,
	IN	int						size);

PNDIS_PACKET RTMP_AllocateFragPacketBuffer(
	IN	VOID					*pReserved,
	IN	ULONG					Length);

NDIS_STATUS RTMPAllocateNdisPacket(
	IN	VOID					*pReserved,
	OUT PNDIS_PACKET			*ppPacket,
	IN	PUCHAR					pHeader,
	IN	UINT					HeaderLen,
	IN	PUCHAR					pData,
	IN	UINT					DataLen);

VOID RTMPFreeNdisPacket(
	IN	VOID					*pReserved,
	IN	PNDIS_PACKET			pPacket);

NDIS_STATUS Sniff2BytesFromNdisBuffer(
	IN  PNDIS_BUFFER			pFirstBuffer,
	IN  UCHAR           		DesiredOffset,
	OUT PUCHAR          		pByte0,
	OUT PUCHAR          		pByte1);

void RTMP_QueryPacketInfo(
	IN  PNDIS_PACKET			pPacket,
	OUT PACKET_INFO  			*pPacketInfo,
	OUT PUCHAR		 			*pSrcBufVA,
	OUT	UINT		 			*pSrcBufLen);

PNDIS_PACKET DuplicatePacket(
	IN	PNET_DEV				pNetDev,
	IN	PNDIS_PACKET			pPacket,
	IN	UCHAR					FromWhichBSSID);

PNDIS_PACKET duplicate_pkt(
	IN	PNET_DEV				pNetDev,
	IN	PUCHAR					pHeader802_3,
    IN  UINT            		HdrLen,
	IN	PUCHAR					pData,
	IN	ULONG					DataSize,
	IN	UCHAR					FromWhichBSSID);

PNDIS_PACKET duplicate_pkt_with_TKIP_MIC(
	IN	VOID					*pReserved,
	IN	PNDIS_PACKET			pOldPkt);

PNDIS_PACKET duplicate_pkt_with_VLAN(
	IN	PNET_DEV				pNetDev,
	IN	USHORT					VLAN_VID,
	IN	USHORT					VLAN_Priority,
	IN	PUCHAR					pHeader802_3,
    IN  UINT            		HdrLen,
	IN	PUCHAR					pData,
	IN	ULONG					DataSize,
	IN	UCHAR					FromWhichBSSID,
	IN	UCHAR					*TPID);

typedef void (*RTMP_CB_8023_PACKET_ANNOUNCE)(
			IN	VOID			*pCtrlBkPtr, 
			IN	PNDIS_PACKET	pPacket,
			IN	UCHAR			OpMode);

BOOLEAN RTMPL2FrameTxAction(
	IN  VOID					*pCtrlBkPtr,
	IN	PNET_DEV				pNetDev,
	IN	RTMP_CB_8023_PACKET_ANNOUNCE _announce_802_3_packet,
	IN	UCHAR					apidx,
	IN	PUCHAR					pData,
	IN	UINT32					data_len,
	IN	UCHAR			OpMode);

PNDIS_PACKET ExpandPacket(
	IN	VOID					*pReserved,
	IN	PNDIS_PACKET			pPacket,
	IN	UINT32					ext_head_len,
	IN	UINT32					ext_tail_len);

PNDIS_PACKET ClonePacket(
	IN	VOID					*pReserved,
	IN	PNDIS_PACKET			pPacket,
	IN	PUCHAR					pData,
	IN	ULONG					DataSize);

void wlan_802_11_to_802_3_packet(
	IN	PNET_DEV				pNetDev,
	IN	UCHAR					OpMode,
	IN	USHORT					VLAN_VID,
	IN	USHORT					VLAN_Priority,
	IN	PNDIS_PACKET			pRxPacket,
	IN	UCHAR					*pData,
	IN	ULONG					DataSize,
	IN	PUCHAR					pHeader802_3,
	IN  UCHAR					FromWhichBSSID,
	IN	UCHAR					*TPID);

void send_monitor_packets(
	IN	PNET_DEV				pNetDev,
	IN	PNDIS_PACKET			pRxPacket,
	IN	PHEADER_802_11			pHeader,
	IN	UCHAR					*pData,
	IN	USHORT					DataSize,
	IN	UCHAR					L2PAD,
	IN	UCHAR					PHYMODE,
	IN	UCHAR					BW,
	IN	UCHAR					ShortGI,
	IN	UCHAR					MCS,
	IN	UCHAR					AMPDU,
	IN	UCHAR					STBC,
	IN	UCHAR					RSSI1,
	IN	UCHAR					BssMonitorFlag11n,
	IN	UCHAR					*pDevName,
	IN	UCHAR					Channel,
	IN	UCHAR					CentralChannel,
	IN	UINT32					MaxRssi);

UCHAR VLAN_8023_Header_Copy(
	IN	USHORT					VLAN_VID,
	IN	USHORT					VLAN_Priority,
	IN	PUCHAR					pHeader802_3,
	IN	UINT            		HdrLen,
	OUT PUCHAR					pData,
	IN	UCHAR					FromWhichBSSID,
	IN	UCHAR					*TPID);

VOID RtmpOsPktBodyCopy(
	IN	PNET_DEV				pNetDev,
	IN	PNDIS_PACKET			pNetPkt,
	IN	ULONG					ThisFrameLen,
	IN	PUCHAR					pData);

INT RtmpOsIsPktCloned(
	IN	PNDIS_PACKET			pNetPkt);

PNDIS_PACKET RtmpOsPktCopy(
	IN	PNDIS_PACKET			pNetPkt);

PNDIS_PACKET RtmpOsPktClone(
	IN	PNDIS_PACKET			pNetPkt);

VOID RtmpOsPktDataPtrAssign(
	IN	PNDIS_PACKET			pNetPkt,
	IN	UCHAR					*pData);

VOID RtmpOsPktLenAssign(
	IN	PNDIS_PACKET			pNetPkt,
	IN	LONG					Len);

VOID RtmpOsPktTailAdjust(
	IN	PNDIS_PACKET			pNetPkt,
	IN	UINT					removedTagLen);

PUCHAR RtmpOsPktTailBufExtend(
	IN	PNDIS_PACKET			pNetPkt,
	IN	UINT					Len);

PUCHAR RtmpOsPktHeadBufExtend(
	IN	PNDIS_PACKET			pNetPkt,
	IN	UINT					Len);

VOID RtmpOsPktReserve(
	IN	PNDIS_PACKET			pNetPkt,
	IN	UINT					Len);

VOID RtmpOsPktProtocolAssign(
	IN	PNDIS_PACKET			pNetPkt);

VOID RtmpOsPktInfPpaSend(
	IN	PNDIS_PACKET			pNetPkt);

VOID RtmpOsPktRcvHandle(
	IN	PNDIS_PACKET			pNetPkt);

VOID RtmpOsPktNatMagicTag(
	IN	PNDIS_PACKET			pNetPkt);

VOID RtmpOsPktNatNone(
	IN	PNDIS_PACKET			pNetPkt);

VOID RtmpOsPktInit(
	IN	PNDIS_PACKET			pNetPkt,
	IN	PNET_DEV				pNetDev,
	IN	UCHAR					*pData,
	IN	USHORT					DataSize);

PNDIS_PACKET RtmpOsPktIappMakeUp(
	IN	PNET_DEV				pNetDev,
	IN	UINT8					*pMac);

BOOLEAN RtmpOsPktOffsetInit(VOID);

UINT16 RtmpOsNtohs(
	IN	UINT16					Value);

UINT16 RtmpOsHtons(
	IN	UINT16					Value);

UINT32 RtmpOsNtohl(
	IN	UINT32					Value);

UINT32 RtmpOsHtonl(
	IN	UINT32					Value);

/* OS File */
RTMP_OS_FD RtmpOSFileOpen(char *pPath,  int flag, int mode);
int RtmpOSFileClose(RTMP_OS_FD osfd);
void RtmpOSFileSeek(RTMP_OS_FD osfd, int offset);
int RtmpOSFileRead(RTMP_OS_FD osfd, char *pDataPtr, int readLen);
int RtmpOSFileWrite(RTMP_OS_FD osfd, char *pDataPtr, int writeLen);

INT32 RtmpOsFileIsErr(
	IN	VOID					*pFile);

void RtmpOSFSInfoChange(
	IN	RTMP_OS_FS_INFO			*pOSFSInfoOrg,
	IN	BOOLEAN					bSet);

/* OS Network Interface */
int RtmpOSNetDevAddrSet(
	IN UCHAR					OpMode,
	IN PNET_DEV 				pNetDev,
	IN PUCHAR					pMacAddr,
	IN PUCHAR					dev_name);

void RtmpOSNetDevClose(
	IN PNET_DEV					pNetDev);

void RtmpOSNetDevFree(
	IN	PNET_DEV				pNetDev);

INT RtmpOSNetDevAlloc(
	IN	PNET_DEV				*new_dev_p,
	IN	UINT32					privDataSize);

INT RtmpOSNetDevOpsAlloc(
	IN	PVOID					*pNetDevOps);


PNET_DEV RtmpOSNetDevGetByName(
	IN	PNET_DEV				pNetDev,
	IN	PSTRING					pDevName);

void RtmpOSNetDeviceRefPut(
	IN	PNET_DEV				pNetDev);

INT RtmpOSNetDevDestory(
	IN	VOID					*pReserved,
	IN	PNET_DEV				pNetDev);

void RtmpOSNetDevDetach(
	IN	PNET_DEV				pNetDev);

int RtmpOSNetDevAttach(
	IN	UCHAR					OpMode,
	IN	PNET_DEV				pNetDev, 
	IN	RTMP_OS_NETDEV_OP_HOOK	*pDevOpHook);

PNET_DEV RtmpOSNetDevCreate(
	IN	INT32					MC_RowID,
	IN	UINT32					*pIoctlIF,
	IN	INT 					devType,
	IN	INT						devNum,
	IN	INT						privMemSize,
	IN	PSTRING					pNamePrefix);

BOOLEAN RtmpOSNetDevIsUp(
	IN	VOID					*pDev);

unsigned char *RtmpOsNetDevGetPhyAddr(
	IN	VOID					*pDev);

VOID RtmpOsNetQueueStart(
	IN	PNET_DEV				pDev);

VOID RtmpOsNetQueueStop(
	IN	PNET_DEV				pDev);

VOID RtmpOsNetQueueWake(
	IN	PNET_DEV				pDev);

VOID RtmpOsSetPktNetDev(
	IN	VOID					*pPkt,
	IN	VOID					*pDev);

PNET_DEV RtmpOsPktNetDevGet(
	IN	VOID					*pPkt);

char *RtmpOsGetNetDevName(
	IN	VOID					*pDev);

VOID RtmpOsSetNetDevPriv(
	IN	VOID					*pDev,
	IN	VOID					*pPriv);

VOID *RtmpOsGetNetDevPriv(
	IN	VOID					*pDev);

VOID RtmpOsSetNetDevType(
	IN	VOID					*pDev,
	IN	USHORT					Type);

VOID RtmpOsSetNetDevTypeMonitor(
	IN	VOID					*pDev);

/* OS Semaphore */
VOID RtmpOsCmdUp(
	IN	RTMP_OS_TASK			*pCmdQTask);

BOOLEAN RtmpOsSemaInitLocked(
	IN	RTMP_OS_SEM				*pSemOrg,
	IN	LIST_HEADER				*pSemList);

BOOLEAN RtmpOsSemaInit(
	IN	RTMP_OS_SEM				*pSemOrg,
	IN	LIST_HEADER				*pSemList);

BOOLEAN RtmpOsSemaDestory(
	IN	RTMP_OS_SEM				*pSemOrg);

INT32 RtmpOsSemaWaitInterruptible(
	IN	RTMP_OS_SEM				*pSemOrg);

VOID RtmpOsSemaWakeUp(
	IN	RTMP_OS_SEM				*pSemOrg);

VOID RtmpOsMlmeUp(
	IN	RTMP_OS_TASK			*pMlmeQTask);

/* OS Task */
BOOLEAN RtmpOsTaskletSche(
	IN	RTMP_NET_TASK_STRUCT	*pTasklet);

BOOLEAN RtmpOsTaskletInit(
	IN	RTMP_NET_TASK_STRUCT	*pTasklet,
	IN	VOID					(*pFunc)(unsigned long data),
	IN	ULONG					Data,
	IN	LIST_HEADER				*pTaskletList);

BOOLEAN RtmpOsTaskletKill(
	IN	RTMP_NET_TASK_STRUCT	*pTasklet);

VOID RtmpOsTaskletDataAssign(
	IN	RTMP_NET_TASK_STRUCT	*pTasklet,
	IN	ULONG					Data);

VOID RtmpOsTaskWakeUp(
	IN	RTMP_OS_TASK			*pTaskOrg);

INT32 RtmpOsTaskIsKilled(
	IN	RTMP_OS_TASK			*pTaskOrg);

BOOLEAN RtmpOsCheckTaskLegality(
	IN	RTMP_OS_TASK			*pTaskOrg);

BOOLEAN RtmpOSTaskAlloc(
	IN	RTMP_OS_TASK			*pTask,
	IN	LIST_HEADER				*pTaskList);

VOID RtmpOSTaskFree(
	IN	RTMP_OS_TASK			*pTask);

NDIS_STATUS RtmpOSTaskKill(
	IN	RTMP_OS_TASK			*pTaskOrg);

INT RtmpOSTaskNotifyToExit(
	IN	RTMP_OS_TASK			*pTaskOrg);

VOID RtmpOSTaskCustomize(
	IN	RTMP_OS_TASK			*pTaskOrg);

NDIS_STATUS RtmpOSTaskAttach(
	IN	RTMP_OS_TASK			*pTaskOrg,
	IN	RTMP_OS_TASK_CALLBACK	fn,
	IN	ULONG					arg);

NDIS_STATUS RtmpOSTaskInit(
	IN	RTMP_OS_TASK			*pTaskOrg,
	IN	PSTRING					pTaskName,
	IN	VOID					*pPriv,
	IN	LIST_HEADER				*pTaskList,
	IN	LIST_HEADER				*pSemList);

BOOLEAN RtmpOSTaskWait(
	IN	VOID					*pReserved,
	IN	RTMP_OS_TASK			*pTaskOrg,
	IN	INT32					*pStatus);

VOID *RtmpOsTaskDataGet(
	IN	RTMP_OS_TASK			*pTaskOrg);

INT32 RtmpThreadPidKill(
	IN	RTMP_OS_PID				PID);

/* OS Timer */
VOID RTMP_SetPeriodicTimer(
	IN	NDIS_MINIPORT_TIMER		*pTimerOrg, 
	IN	unsigned long			timeout);

VOID RTMP_OS_Init_Timer(
	IN	VOID 					*pReserved,
	IN	NDIS_MINIPORT_TIMER		*pTimerOrg, 
	IN	TIMER_FUNCTION			function,
	IN	PVOID					data,
	IN	LIST_HEADER				*pTimerList);

VOID RTMP_OS_Add_Timer(
	IN	NDIS_MINIPORT_TIMER		*pTimerOrg,
	IN	unsigned long			timeout);

VOID RTMP_OS_Mod_Timer(
	IN	NDIS_MINIPORT_TIMER		*pTimerOrg,
	IN	unsigned long			timeout);

VOID RTMP_OS_Del_Timer(
	IN	NDIS_MINIPORT_TIMER		*pTimerOrg,
	OUT	BOOLEAN					*pCancelled);

VOID RTMP_OS_Release_Timer(
	IN	NDIS_MINIPORT_TIMER		*pTimerOrg);

BOOLEAN RTMP_OS_Alloc_Rsc(
	IN	LIST_HEADER				*pRscList,
	IN	VOID 					*pRsc,
	IN	UINT32					RscLen);

VOID RTMP_OS_Free_Rscs(
	IN	LIST_HEADER				*pRscList);

/* OS Lock */
BOOLEAN RtmpOsAllocateLock(
	IN	NDIS_SPIN_LOCK			*pLock,
	IN	LIST_HEADER				*pLockList);

VOID RtmpOsFreeSpinLock(
	IN	NDIS_SPIN_LOCK			*pLockOrg);

VOID RtmpOsSpinLockBh(
	IN	NDIS_SPIN_LOCK			*pLockOrg);

VOID RtmpOsSpinUnLockBh(
	IN	NDIS_SPIN_LOCK			*pLockOrg);

VOID RtmpOsIntLock(
	IN	NDIS_SPIN_LOCK			*pLockOrg,
	IN	ULONG					*pIrqFlags);

VOID RtmpOsIntUnLock(
	IN	NDIS_SPIN_LOCK			*pLockOrg,
	IN	ULONG					IrqFlags);

/* OS PID */
VOID RtmpOsGetPid(
	IN	ULONG					*pDst,
	IN	ULONG					PID);

VOID RtmpOsTaskPidInit(
	IN	RTMP_OS_PID				*pPid);

/* OS I/O */
VOID RTMP_PCI_Writel(
	IN	ULONG					Value,
	IN	VOID					*pAddr);

VOID RTMP_PCI_Writew(
	IN	ULONG					Value,
	IN	VOID					*pAddr);

VOID RTMP_PCI_Writeb(
	IN	ULONG					Value,
	IN	VOID					*pAddr);

ULONG RTMP_PCI_Readl(
	IN	VOID					*pAddr);

ULONG RTMP_PCI_Readw(
	IN	VOID					*pAddr);

ULONG RTMP_PCI_Readb(
	IN	VOID					*pAddr);

int RtmpOsPciConfigReadWord(
	IN	VOID					*pDev,
	IN	UINT32					Offset,
	OUT UINT16					*pValue);

int RtmpOsPciConfigWriteWord(
	IN	VOID					*pDev,
	IN	UINT32					Offset,
	IN	UINT16					Value);

int RtmpOsPciConfigReadDWord(
	IN	VOID					*pDev,
	IN	UINT32					Offset,
	OUT UINT32					*pValue);

int RtmpOsPciConfigWriteDWord(
	IN	VOID					*pDev,
	IN	UINT32					Offset,
	IN	UINT32					Value);

int RtmpOsPciFindCapability(
	IN	VOID					*pDev,
	IN	int						Cap);

VOID *RTMPFindHostPCIDev(
    IN	VOID					*pPciDevSrc);

int RtmpOsPciMsiEnable(
	IN	VOID					*pDev);

VOID RtmpOsPciMsiDisable(
	IN	VOID					*pDev);

/* OS Wireless */
ULONG RtmpOsMaxScanDataGet(VOID);

/* OS Interrutp */
INT32 RtmpOsIsInInterrupt(VOID);

/* OS USB */
VOID *RtmpOsUsbUrbDataGet(
	IN	VOID					*pUrb);

NTSTATUS RtmpOsUsbUrbStatusGet(
	IN	VOID					*pUrb);

ULONG RtmpOsUsbUrbLenGet(
	IN	VOID					*pUrb);

/* OS Atomic */
BOOLEAN RtmpOsAtomicInit(
	IN	RTMP_OS_ATOMIC		*pAtomic,
	IN	LIST_HEADER			*pAtomicList);

LONG RtmpOsAtomicRead(
	IN	RTMP_OS_ATOMIC		*pAtomic);

VOID RtmpOsAtomicDec(
	IN	RTMP_OS_ATOMIC		*pAtomic);

VOID RtmpOsAtomicInterlockedExchange(
	IN	RTMP_OS_ATOMIC		*pAtomicSrc,
	IN	LONG				Value);

/* OS Utility */
void hex_dump(char *str, unsigned char *pSrcBufVA, unsigned int SrcBufLen);

typedef VOID (*RTMP_OS_SEND_WLAN_EVENT)(
	IN	VOID					*pAdSrc,
	IN	USHORT					Event_flag,
	IN	PUCHAR 					pAddr,
	IN  UCHAR					BssIdx,
	IN	CHAR					Rssi);

VOID RtmpOsSendWirelessEvent(
	IN	VOID			*pAd,
	IN	USHORT			Event_flag,
	IN	PUCHAR 			pAddr,
	IN	UCHAR			BssIdx,
	IN	CHAR			Rssi,
	IN	RTMP_OS_SEND_WLAN_EVENT		pFunc);


int RtmpOSWrielessEventSend(
	IN	PNET_DEV				pNetDev,
	IN	UINT32					eventType,
	IN	INT						flags,
	IN	PUCHAR					pSrcMac,
	IN	PUCHAR					pData,
	IN	UINT32					dataLen);

int RtmpOSWrielessEventSendExt(
	IN	PNET_DEV				pNetDev,
	IN	UINT32					eventType,
	IN	INT						flags,
	IN	PUCHAR					pSrcMac,
	IN	PUCHAR					pData,
	IN	UINT32					dataLen,
	IN	UINT32					family);

UINT RtmpOsWirelessExtVerGet(VOID);

VOID RtmpDrvAllMacPrint(
	IN VOID						*pReserved,
	IN UINT32					*pBufMac,
	IN UINT32					AddrStart,
	IN UINT32					AddrEnd,
	IN UINT32					AddrStep);

VOID RtmpDrvAllE2PPrint(
	IN	VOID					*pReserved,
	IN	USHORT					*pMacContent,
	IN	UINT32					AddrEnd,
	IN	UINT32					AddrStep);

int RtmpOSIRQRelease(
	IN	PNET_DEV				pNetDev,
	IN	UINT32					infType,
	IN	PPCI_DEV				pci_dev,
	IN	BOOLEAN					*pHaveMsi);

VOID RtmpOsWlanEventSet(
	IN	VOID					*pReserved,
	IN	BOOLEAN					*pCfgWEnt,
	IN	BOOLEAN					FlgIsWEntSup);

UINT16 RtmpOsGetUnaligned(
	IN	UINT16					*pWord);

UINT32 RtmpOsGetUnaligned32(
	IN	UINT32					*pWord);

ULONG RtmpOsGetUnalignedlong(
	IN	ULONG					*pWord);

long RtmpOsSimpleStrtol(
	IN	const char				*cp,
	IN	char 					**endp,
	IN	unsigned int			base);

VOID RtmpOsOpsInit(
	IN	RTMP_OS_ABL_OPS			*pOps);

/* ============================ rt_os_util.c ================================ */
VOID RtmpDrvMaxRateGet(
	IN	VOID					*pReserved,
	IN	UINT8					MODE,
	IN	UINT8					ShortGI,
	IN	UINT8					BW,
	IN	UINT8					MCS,
	OUT	UINT32					*pRate);

char * rtstrchr(const char * s, int c);

PSTRING   WscGetAuthTypeStr(
    IN  USHORT					authFlag);

PSTRING   WscGetEncryTypeStr(
    IN  USHORT					encryFlag);

USHORT WscGetAuthTypeFromStr(
    IN  PSTRING          		arg);

USHORT WscGetEncrypTypeFromStr(
    IN  PSTRING          		arg);

VOID RtmpMeshDown(
	IN	VOID					*pDrvCtrlBK,
	IN	BOOLEAN					WaitFlag,
	IN	BOOLEAN					(*RtmpMeshLinkCheck)(IN VOID *pAd));

USHORT RtmpOsNetPrivGet(
	IN	PNET_DEV				pDev);

BOOLEAN RtmpOsCmdDisplayLenCheck(
	IN	UINT32					LenSrc,
	IN	UINT32					Offset);

VOID    WpaSendMicFailureToWpaSupplicant(
	IN	PNET_DEV				pNetDev,
    IN  BOOLEAN					bUnicast);

int wext_notify_event_assoc(
	IN	PNET_DEV				pNetDev,
	IN	UCHAR					*ReqVarIEs,
	IN	UINT32					ReqVarIELen);

VOID    SendAssocIEsToWpaSupplicant( 
	IN	PNET_DEV				pNetDev,
	IN	UCHAR					*ReqVarIEs,
	IN	UINT32					ReqVarIELen);

/* ============================ rt_rbus_pci_util.c ========================== */
void RTMP_AllocateTxDescMemory(
	IN	PPCI_DEV				pPciDev,
	IN	UINT					Index,
	IN	ULONG					Length,
	IN	BOOLEAN					Cached,
	OUT	PVOID					*VirtualAddress,
	OUT	PNDIS_PHYSICAL_ADDRESS	PhysicalAddress);

void RTMP_AllocateMgmtDescMemory(
	IN	PPCI_DEV				pPciDev,
	IN	ULONG					Length,
	IN	BOOLEAN					Cached,
	OUT	PVOID					*VirtualAddress,
	OUT	PNDIS_PHYSICAL_ADDRESS	PhysicalAddress);

void RTMP_AllocateRxDescMemory(
	IN	PPCI_DEV				pPciDev,
	IN	ULONG					Length,
	IN	BOOLEAN					Cached,
	OUT	PVOID					*VirtualAddress,
	OUT	PNDIS_PHYSICAL_ADDRESS	PhysicalAddress);

void RTMP_FreeDescMemory(
	IN	PPCI_DEV				pPciDev,
	IN	ULONG					Length,
	IN	PVOID					VirtualAddress,
	IN	NDIS_PHYSICAL_ADDRESS	PhysicalAddress);

void RTMP_AllocateFirstTxBuffer(
	IN	PPCI_DEV				pPciDev,
	IN	UINT					Index,
	IN	ULONG					Length,
	IN	BOOLEAN					Cached,
	OUT	PVOID					*VirtualAddress,
	OUT	PNDIS_PHYSICAL_ADDRESS	PhysicalAddress);

void RTMP_FreeFirstTxBuffer(
	IN	PPCI_DEV				pPciDev,
	IN	ULONG					Length,
	IN	BOOLEAN					Cached,
	IN	PVOID					VirtualAddress,
	IN	NDIS_PHYSICAL_ADDRESS	PhysicalAddress);

PNDIS_PACKET RTMP_AllocateRxPacketBuffer(
	IN	VOID					*pReserved,
	IN	VOID					*pPciDev,
	IN	ULONG					Length,
	IN	BOOLEAN					Cached,
	OUT	PVOID					*VirtualAddress,
	OUT	PNDIS_PHYSICAL_ADDRESS	PhysicalAddress);

#ifdef CONFIG_STA_SUPPORT
#ifdef CONFIG_PM
#ifdef USB_SUPPORT_SELECTIVE_SUSPEND

int RTMP_Usb_AutoPM_Put_Interface(
	IN	VOID			*pUsb_Dev,
	IN	VOID			*intf);

int  RTMP_Usb_AutoPM_Get_Interface(
	IN	VOID			*pUsb_Dev,
	IN	VOID			*intf);

#endif /* USB_SUPPORT_SELECTIVE_SUSPEND */
#endif /* CONFIG_PM */
#endif /* CONFIG_STA_SUPPORT */



ra_dma_addr_t linux_pci_map_single(void *pPciDev, void *ptr, size_t size, int sd_idx, int direction);

void linux_pci_unmap_single(void *pPciDev, ra_dma_addr_t dma_addr, size_t size, int direction);

/* ============================ rt_usb_util.c =============================== */
#ifdef RTMP_MAC_USB
void dump_urb(VOID *purb);

int rausb_register(VOID * new_driver);

void rausb_deregister(VOID * driver);

/*struct urb *rausb_alloc_urb(int iso_packets); */

void rausb_free_urb(VOID *urb);

void rausb_put_dev(VOID *dev);

struct usb_device *rausb_get_dev(VOID *dev);

int rausb_submit_urb(VOID *urb);

void *rausb_buffer_alloc(VOID *dev,
							size_t size,
							ra_dma_addr_t *dma);

void rausb_buffer_free(VOID *dev,
							size_t size,
							void *addr,
							ra_dma_addr_t dma);

int rausb_control_msg(VOID *dev,
						unsigned int pipe,
						__u8 request,
						__u8 requesttype,
						__u16 value,
						__u16 index,
						void *data,
						__u16 size,
						int timeout);

unsigned int rausb_sndctrlpipe(VOID *dev, ULONG address);

unsigned int rausb_rcvctrlpipe(VOID *dev, ULONG address);

void rausb_kill_urb(VOID *urb);

VOID RtmpOsUsbEmptyUrbCheck(
	IN	VOID				**ppWait,
	IN	NDIS_SPIN_LOCK		*pBulkInLock,
	IN	UCHAR				PendingRx);

typedef VOID (*USB_COMPLETE_HANDLER)(VOID *);

VOID	RtmpOsUsbInitHTTxDesc(
	IN	VOID			*pUrbSrc,
	IN	VOID			*pUsb_Dev,
	IN	UINT			BulkOutEpAddr,
	IN	PUCHAR			pSrc,
	IN	ULONG			BulkOutSize,
	IN	USB_COMPLETE_HANDLER	Func,
	IN	VOID			*pTxContext,
	IN	ra_dma_addr_t		TransferDma);

VOID	RtmpOsUsbInitRxDesc(
	IN	VOID			*pUrbSrc,
	IN	VOID			*pUsb_Dev,
	IN	UINT			BulkInEpAddr,
	IN	UCHAR			*pTransferBuffer,
	IN	UINT32			BufSize,
	IN	USB_COMPLETE_HANDLER	Func,
	IN	VOID			*pRxContext,
	IN	ra_dma_addr_t		TransferDma);

VOID *RtmpOsUsbContextGet(
	IN	VOID			*pUrb);

NTSTATUS RtmpOsUsbStatusGet(
	IN	VOID			*pUrb);

VOID RtmpOsUsbDmaMapping(
	IN	VOID			*pUrb);
#endif /* RTMP_MAC_USB */


UINT32 RtmpOsGetUsbDevVendorID(
	IN VOID *pUsbDev);

UINT32 RtmpOsGetUsbDevProductID(
	IN VOID *pUsbDev);

/* CFG80211 */
#ifdef RT_CFG80211_SUPPORT
typedef struct __CFG80211_BAND {

	UINT8 RFICType;
	UINT8 MpduDensity;
	UINT8 TxStream;
	UINT8 RxStream;
	UINT32 MaxTxPwr;
	UINT32 MaxBssTable;

	UINT16 RtsThreshold;
	UINT16 FragmentThreshold;
	UINT32 RetryMaxCnt; /* bit0~7: short; bit8 ~ 15: long */
	BOOLEAN FlgIsBMode;
} CFG80211_BAND;

VOID CFG80211OS_UnRegister(
	IN VOID						*pCB,
	IN VOID						*pNetDev);

BOOLEAN CFG80211_SupBandInit(
	IN VOID						*pCB,
	IN CFG80211_BAND 			*pBandInfo,
	IN VOID						*pWiphyOrg,
	IN VOID						*pChannelsOrg,
	IN VOID						*pRatesOrg);

BOOLEAN CFG80211OS_SupBandReInit(
	IN VOID						*pCB,
	IN CFG80211_BAND 			*pBandInfo);

VOID CFG80211OS_RegHint(
	IN VOID						*pCB,
	IN UCHAR					*pCountryIe,
	IN ULONG					CountryIeLen);

VOID CFG80211OS_RegHint11D(
	IN VOID						*pCB,
	IN UCHAR					*pCountryIe,
	IN ULONG					CountryIeLen);

BOOLEAN CFG80211OS_BandInfoGet(
	IN VOID						*pCB,
	IN VOID						*pWiphyOrg,
	OUT VOID					**ppBand24,
	OUT VOID					**ppBand5);

UINT32 CFG80211OS_ChanNumGet(
	IN VOID						*pCB,
	IN VOID						*pWiphyOrg,
	IN UINT32					IdBand);

BOOLEAN CFG80211OS_ChanInfoGet(
	IN VOID						*pCB,
	IN VOID						*pWiphyOrg,
	IN UINT32					IdBand,
	IN UINT32					IdChan,
	OUT UINT32					*pChanId,
	OUT UINT32					*pPower,
	OUT BOOLEAN					*pFlgIsRadar);

VOID CFG80211OS_Scaning(
	IN VOID						*pCB,
	IN VOID						**pChanOrg,
	IN UINT32					ChanId,
	IN UCHAR					*pFrame,
	IN UINT32					FrameLen,
	IN INT32					RSSI,
	IN BOOLEAN					FlgIsNMode,
	IN UINT8					BW);

VOID CFG80211OS_ScanEnd(
	IN VOID						*pCB,
	IN BOOLEAN					FlgIsAborted);

void CFG80211OS_ConnectResultInform(
	IN VOID						*pCB,
	IN UCHAR					*pBSSID,
	IN UCHAR					*pReqIe,
	IN UINT32					ReqIeLen,
	IN UCHAR					*pRspIe,
	IN UINT32					RspIeLen,
	IN UCHAR					FlgIsSuccess);
#endif /* RT_CFG80211_SUPPORT */

/* ========================================================================== */
extern UCHAR SNAP_802_1H[6];
extern UCHAR SNAP_BRIDGE_TUNNEL[6];
extern UCHAR EAPOL[2];
extern UCHAR TPID[];
extern UCHAR IPX[2];
extern UCHAR APPLE_TALK[2];
extern UCHAR NUM_BIT8[8];
extern ULONG RTPktOffsetData, RTPktOffsetLen, RTPktOffsetCB;

extern ULONG OS_NumOfMemAlloc, OS_NumOfMemFree;

extern INT32 ralinkrate[];
extern UINT32 RT_RateSize;

#endif /* __RT_OS_UTIL_H__ */
