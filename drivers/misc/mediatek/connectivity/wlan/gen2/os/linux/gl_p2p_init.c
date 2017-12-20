/*
* Copyright (C) 2011-2014 MediaTek Inc.
*
* This program is free software: you can redistribute it and/or modify it under the terms of the
* GNU General Public License version 2 as published by the Free Software Foundation.
*
* This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
* without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
* See the GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License along with this program.
* If not, see <http://www.gnu.org/licenses/>.
*/

/*
** Id: @(#) gl_p2p_init.c@@
*/

/*! \file   gl_p2p_init.c
    \brief  init and exit routines of Linux driver interface for Wi-Fi Direct

    This file contains the main routines of Linux driver for MediaTek Inc. 802.11
    Wireless LAN Adapters.
*/

/*******************************************************************************
*                         C O M P I L E R   F L A G S
********************************************************************************
*/

/*******************************************************************************
*                    E X T E R N A L   R E F E R E N C E S
********************************************************************************
*/

#include "precomp.h"

/*******************************************************************************
*                              C O N S T A N T S
********************************************************************************
*/

#define P2P_MODE_INF_NAME "p2p%d"
#if CFG_TC1_FEATURE
#define AP_MODE_INF_NAME "wlan%d"
#else
#define AP_MODE_INF_NAME "ap%d"
#endif
/* #define MAX_INF_NAME_LEN 15 */
/* #define MIN_INF_NAME_LEN 1 */

#define RUNNING_P2P_MODE 0
#define RUNNING_AP_MODE 1

/*******************************************************************************
*                             D A T A   T Y P E S
********************************************************************************
*/

/*******************************************************************************
*                            P U B L I C   D A T A
********************************************************************************
*/

/*******************************************************************************
*                           P R I V A T E   D A T A
********************************************************************************
*/

/*  Get interface name and running mode from module insertion parameter
*       Usage: insmod p2p.ko mode=1
*       default: interface name is p2p%d
*                   running mode is P2P
*/
static PUCHAR ifname = P2P_MODE_INF_NAME;
static UINT_16 mode = RUNNING_P2P_MODE;

/*******************************************************************************
*                                 M A C R O S
********************************************************************************
*/

/*******************************************************************************
*                   F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/

/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/

/*----------------------------------------------------------------------------*/
/*!
* \brief    check interface name parameter is valid or not
*             if invalid, set ifname to P2P_MODE_INF_NAME
*
*
* \retval
*/
/*----------------------------------------------------------------------------*/
VOID p2pCheckInterfaceName(VOID)
{

	if (mode) {
		mode = RUNNING_AP_MODE;
		ifname = AP_MODE_INF_NAME;
	}
#if 0
	UINT_32 ifLen = 0;

	if (ifname) {
		ifLen = strlen(ifname);

		if (ifLen > MAX_INF_NAME_LEN)
			ifname[MAX_INF_NAME_LEN] = '\0';
		else if (ifLen < MIN_INF_NAME_LEN)
			ifname = P2P_MODE_INF_NAME;
	} else {
		ifname = P2P_MODE_INF_NAME;
	}
#endif
}

void p2pHandleSystemSuspend(void)
{
	struct net_device *prDev = NULL;
	P_GLUE_INFO_T prGlueInfo = NULL;
	UINT_8 ip[4] = { 0 };
	UINT_32 u4NumIPv4 = 0;
#ifdef CONFIG_IPV6
	UINT_8 ip6[16] = { 0 };	/* FIX ME: avoid to allocate large memory in stack */
	UINT_32 u4NumIPv6 = 0;
#endif
	UINT_32 i;
	P_PARAM_NETWORK_ADDRESS_IP prParamIpAddr;


	if (!wlanExportGlueInfo(&prGlueInfo)) {
		DBGLOG(P2P, INFO, "No glue info\n");
		return;
	}

	ASSERT(prGlueInfo);
	/* <1> Sanity check and acquire the net_device */
	prDev = prGlueInfo->prP2PInfo->prDevHandler;
	ASSERT(prDev);

	/* <3> get the IPv4 address */
	if (!prDev || !(prDev->ip_ptr) ||
	    !((struct in_device *)(prDev->ip_ptr))->ifa_list ||
	    !(&(((struct in_device *)(prDev->ip_ptr))->ifa_list->ifa_local))) {
		DBGLOG(P2P, INFO, "ip is not available.\n");
		return;
	}
	/* <4> copy the IPv4 address */
	kalMemCopy(ip, &(((struct in_device *)(prDev->ip_ptr))->ifa_list->ifa_local), sizeof(ip));

	/* todo: traverse between list to find whole sets of IPv4 addresses */
	if (!((ip[0] == 0) && (ip[1] == 0) && (ip[2] == 0) && (ip[3] == 0)))
		u4NumIPv4++;
#ifdef CONFIG_IPV6
	/* <5> get the IPv6 address */
	if (!prDev || !(prDev->ip6_ptr) ||
	    !((struct in_device *)(prDev->ip6_ptr))->ifa_list ||
	    !(&(((struct in_device *)(prDev->ip6_ptr))->ifa_list->ifa_local))) {
		DBGLOG(P2P, INFO, "ipv6 is not available.\n");
		return;
	}
	/* <6> copy the IPv6 address */
	kalMemCopy(ip6, &(((struct in_device *)(prDev->ip6_ptr))->ifa_list->ifa_local), sizeof(ip6));
	DBGLOG(P2P, INFO, "ipv6 is %d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d\n",
	       ip6[0], ip6[1], ip6[2], ip6[3],
	       ip6[4], ip6[5], ip6[6], ip6[7], ip6[8], ip6[9], ip6[10], ip6[11], ip6[12], ip6[13], ip6[14], ip6[15]);
	/* todo: traverse between list to find whole sets of IPv6 addresses */

	if (!((ip6[0] == 0) && (ip6[1] == 0) && (ip6[2] == 0) && (ip6[3] == 0) && (ip6[4] == 0) && (ip6[5] == 0)))
		; /* Do nothing */
#endif
	/* <7> set up the ARP filter */
	{
		WLAN_STATUS rStatus = WLAN_STATUS_FAILURE;
		UINT_32 u4SetInfoLen = 0;
/* UINT_8 aucBuf[32] = {0}; */
		UINT_32 u4Len = OFFSET_OF(PARAM_NETWORK_ADDRESS_LIST, arAddress);
		P_PARAM_NETWORK_ADDRESS_LIST prParamNetAddrList = (P_PARAM_NETWORK_ADDRESS_LIST) g_aucBufIpAddr;
		/* aucBuf; */
		P_PARAM_NETWORK_ADDRESS prParamNetAddr = prParamNetAddrList->arAddress;

		kalMemZero(g_aucBufIpAddr, sizeof(g_aucBufIpAddr));

		prParamNetAddrList->u4AddressCount = u4NumIPv4;
#ifdef CONFIG_IPV6
		prParamNetAddrList->u4AddressCount += u4NumIPv6;
#endif

		prParamNetAddrList->u2AddressType = PARAM_PROTOCOL_ID_TCP_IP;
		for (i = 0; i < u4NumIPv4; i++) {
			prParamNetAddr->u2AddressLength = sizeof(PARAM_NETWORK_ADDRESS_IP);	/* 4;; */
			prParamNetAddr->u2AddressType = PARAM_PROTOCOL_ID_TCP_IP;
#if 0
			kalMemCopy(prParamNetAddr->aucAddress, ip, sizeof(ip));
			prParamNetAddr = (P_PARAM_NETWORK_ADDRESS) ((UINT_32) prParamNetAddr + sizeof(ip));
			u4Len += OFFSET_OF(PARAM_NETWORK_ADDRESS, aucAddress) + sizeof(ip);
#else
			prParamIpAddr = (P_PARAM_NETWORK_ADDRESS_IP) prParamNetAddr->aucAddress;
			kalMemCopy(&prParamIpAddr->in_addr, ip, sizeof(ip));

/* prParamNetAddr = (P_PARAM_NETWORK_ADDRESS)((UINT_32)prParamNetAddr + sizeof(PARAM_NETWORK_ADDRESS));
// TODO: frog. The pointer is not right. */

			prParamNetAddr = (P_PARAM_NETWORK_ADDRESS) ((ULONG) prParamNetAddr +
								    (ULONG) (prParamNetAddr->u2AddressLength +
									     OFFSET_OF(PARAM_NETWORK_ADDRESS,
										       aucAddress)));

			u4Len += OFFSET_OF(PARAM_NETWORK_ADDRESS, aucAddress) + sizeof(PARAM_NETWORK_ADDRESS_IP);
#endif
		}
#ifdef CONFIG_IPV6
		for (i = 0; i < u4NumIPv6; i++) {
			prParamNetAddr->u2AddressLength = 6;
			prParamNetAddr->u2AddressType = PARAM_PROTOCOL_ID_TCP_IP;
			kalMemCopy(prParamNetAddr->aucAddress, ip6, sizeof(ip6));
/* prParamNetAddr = (P_PARAM_NETWORK_ADDRESS)((UINT_32)prParamNetAddr + sizeof(ip6)); */

			prParamNetAddr = (P_PARAM_NETWORK_ADDRESS) ((ULONG) prParamNetAddr +
								    (ULONG) (prParamNetAddr->u2AddressLength +
									     OFFSET_OF(PARAM_NETWORK_ADDRESS,
										       aucAddress)));

			u4Len += OFFSET_OF(PARAM_NETWORK_ADDRESS, aucAddress) + sizeof(ip6);
		}
#endif
		ASSERT(u4Len <= sizeof(g_aucBufIpAddr /*aucBuf */));

		rStatus = kalIoctl(prGlueInfo,
				   wlanoidSetP2pSetNetworkAddress,
				   (PVOID) prParamNetAddrList, u4Len, FALSE, FALSE, TRUE, TRUE, &u4SetInfoLen);

		DBGLOG(INIT, INFO, "IP: %d.%d.%d.%d, rStatus: %u\n", ip[0], ip[1], ip[2], ip[3], rStatus);
	}
}

void p2pHandleSystemResume(void)
{
	struct net_device *prDev = NULL;
	P_GLUE_INFO_T prGlueInfo = NULL;
	UINT_8 ip[4] = { 0 };
#ifdef CONFIG_IPV6
	UINT_8 ip6[16] = { 0 };	/* FIX ME: avoid to allocate large memory in stack */
#endif

	if (!wlanExportGlueInfo(&prGlueInfo)) {
		DBGLOG(P2P, WARN, "no glue info\n");
		return;
	}

	ASSERT(prGlueInfo);
	/* <1> Sanity check and acquire the net_device */
	prDev = prGlueInfo->prP2PInfo->prDevHandler;
	ASSERT(prDev);

	/* <3> get the IPv4 address */
	if (!prDev || !(prDev->ip_ptr) ||
	    !((struct in_device *)(prDev->ip_ptr))->ifa_list ||
	    !(&(((struct in_device *)(prDev->ip_ptr))->ifa_list->ifa_local))) {
		DBGLOG(P2P, INFO, "ip is not available.\n");
		return;
	}
	/* <4> copy the IPv4 address */
	kalMemCopy(ip, &(((struct in_device *)(prDev->ip_ptr))->ifa_list->ifa_local), sizeof(ip));

#ifdef CONFIG_IPV6
	/* <5> get the IPv6 address */
	if (!prDev || !(prDev->ip6_ptr) ||
	    !((struct in_device *)(prDev->ip6_ptr))->ifa_list ||
	    !(&(((struct in_device *)(prDev->ip6_ptr))->ifa_list->ifa_local))) {
		DBGLOG(P2P, INFO, "ipv6 is not available.\n");
		return;
	}
	/* <6> copy the IPv6 address */
	kalMemCopy(ip6, &(((struct in_device *)(prDev->ip6_ptr))->ifa_list->ifa_local), sizeof(ip6));
	DBGLOG(P2P, INFO, "ipv6 is %d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d\n",
	       ip6[0], ip6[1], ip6[2], ip6[3],
	       ip6[4], ip6[5], ip6[6], ip6[7], ip6[8], ip6[9], ip6[10], ip6[11], ip6[12], ip6[13], ip6[14], ip6[15]);
#endif
	/* <7> clear the ARP filter */
	{
		WLAN_STATUS rStatus = WLAN_STATUS_FAILURE;
		UINT_32 u4SetInfoLen = 0;
/* UINT_8 aucBuf[32] = {0}; */
		UINT_32 u4Len = sizeof(PARAM_NETWORK_ADDRESS_LIST);
		P_PARAM_NETWORK_ADDRESS_LIST prParamNetAddrList = (P_PARAM_NETWORK_ADDRESS_LIST) g_aucBufIpAddr;
		/* aucBuf; */

		kalMemZero(g_aucBufIpAddr, sizeof(g_aucBufIpAddr));

		prParamNetAddrList->u4AddressCount = 0;
		prParamNetAddrList->u2AddressType = PARAM_PROTOCOL_ID_TCP_IP;

		ASSERT(u4Len <= sizeof(g_aucBufIpAddr /*aucBuf */));
		rStatus = kalIoctl(prGlueInfo,
				   wlanoidSetP2pSetNetworkAddress,
				   (PVOID) prParamNetAddrList, u4Len, FALSE, FALSE, TRUE, TRUE, &u4SetInfoLen);

		DBGLOG(INIT, INFO, "IP: %d.%d.%d.%d, rStatus: %u\n", ip[0], ip[1], ip[2], ip[3], rStatus);
	}
}

/*----------------------------------------------------------------------------*/
/*!
* \brief
*       run p2p init procedure, include register pointer to wlan
*                                                     glue register p2p
*                                                     set p2p registered flag
* \retval 1     Success
*/
/*----------------------------------------------------------------------------*/
BOOLEAN p2pLaunch(P_GLUE_INFO_T prGlueInfo)
{

	DBGLOG(P2P, TRACE, "p2pLaunch\n");

	if (prGlueInfo->prAdapter->fgIsP2PRegistered == TRUE) {
		DBGLOG(P2P, INFO, "p2p already registered\n");
		return FALSE;
	} else if (glRegisterP2P(prGlueInfo, ifname, (BOOLEAN) mode)) {
		prGlueInfo->prAdapter->fgIsP2PRegistered = TRUE;

		DBGLOG(P2P, TRACE, "Launch success, fgIsP2PRegistered TRUE.\n");
		return TRUE;
	}
	DBGLOG(P2P, ERROR, "Launch Fail\n");

	return FALSE;
}

VOID p2pSetMode(IN BOOLEAN fgIsAPMOde)
{
	if (fgIsAPMOde) {
		mode = RUNNING_AP_MODE;
		ifname = AP_MODE_INF_NAME;
	} else {
		mode = RUNNING_P2P_MODE;
		ifname = P2P_MODE_INF_NAME;
	}

}				/* p2pSetMode */

/*----------------------------------------------------------------------------*/
/*!
* \brief
*       run p2p exit procedure, include unregister pointer to wlan
*                                                     glue unregister p2p
*                                                     set p2p registered flag

* \retval 1     Success
*/
/*----------------------------------------------------------------------------*/
BOOLEAN p2pRemove(P_GLUE_INFO_T prGlueInfo)
{
	if (prGlueInfo->prAdapter->fgIsP2PRegistered == FALSE) {
		DBGLOG(P2P, INFO, "p2p is not Registered.\n");
		return FALSE;
	}
	/*Check p2p fsm is stop or not. If not then stop now */
	if (IS_P2P_ACTIVE(prGlueInfo->prAdapter))
		p2pStopImmediate(prGlueInfo);
	prGlueInfo->prAdapter->fgIsP2PRegistered = FALSE;
	glUnregisterP2P(prGlueInfo);
	/*p2p is removed successfully */
	return TRUE;

}

#if 0
/*----------------------------------------------------------------------------*/
/*!
* \brief Driver entry point when the driver is configured as a Linux Module, and
*        is called once at module load time, by the user-level modutils
*        application: insmod or modprobe.
*
* \retval 0     Success
*/
/*----------------------------------------------------------------------------*/
static int initP2P(void)
{
	P_GLUE_INFO_T prGlueInfo;

	/*check interface name validation */
	p2pCheckInterfaceName();

	DBGLOG(P2P, INFO, "InitP2P, Ifname: %s, Mode: %s\n", ifname, mode ? "AP" : "P2P");

	/*register p2p init & exit function to wlan sub module handler */
	wlanSubModRegisterInitExit(p2pLaunch, p2pRemove, P2P_MODULE);

	/*if wlan is not start yet, do nothing
	 * p2pLaunch will be called by txthread while wlan start
	 */
	/*if wlan is not started yet, return FALSE */
	if (wlanExportGlueInfo(&prGlueInfo)) {
		wlanSubModInit(prGlueInfo);
		return prGlueInfo->prAdapter->fgIsP2PRegistered ? 0 : -EIO;
	}

	return 0;
}				/* end of initP2P() */

/*----------------------------------------------------------------------------*/
/*!
* \brief Driver exit point when the driver as a Linux Module is removed. Called
*        at module unload time, by the user level modutils application: rmmod.
*        This is our last chance to clean up after ourselves.
*
* \return (none)
*/
/*----------------------------------------------------------------------------*/
/* 1 Module Leave Point */
static VOID __exit exitP2P(void)
{
	P_GLUE_INFO_T prGlueInfo;

	DBGLOG(P2P, INFO, KERN_INFO DRV_NAME "ExitP2P\n");

	/*if wlan is not started yet, return FALSE */
	if (wlanExportGlueInfo(&prGlueInfo))
		wlanSubModExit(prGlueInfo);
	/*UNregister p2p init & exit function to wlan sub module handler */
	wlanSubModRegisterInitExit(NULL, NULL, P2P_MODULE);
}				/* end of exitP2P() */
#endif
