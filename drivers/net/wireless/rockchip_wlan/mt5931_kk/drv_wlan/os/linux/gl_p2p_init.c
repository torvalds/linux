/*
** $Id: @(#) gl_p2p_init.c@@
*/

/*! \file   gl_p2p_init.c
    \brief  init and exit routines of Linux driver interface for Wi-Fi Direct

    This file contains the main routines of Linux driver for MediaTek Inc. 802.11
    Wireless LAN Adapters.
*/

/*******************************************************************************
* Copyright (c) 2011 MediaTek Inc.
*
* All rights reserved. Copying, compilation, modification, distribution
* or any other use whatsoever of this material is strictly prohibited
* except in accordance with a Software License Agreement with
* MediaTek Inc.
********************************************************************************
*/

/*******************************************************************************
* LEGAL DISCLAIMER
*
* BY OPENING THIS FILE, BUYER HEREBY UNEQUIVOCALLY ACKNOWLEDGES AND
* AGREES THAT THE SOFTWARE/FIRMWARE AND ITS DOCUMENTATIONS ("MEDIATEK
* SOFTWARE") RECEIVED FROM MEDIATEK AND/OR ITS REPRESENTATIVES ARE
* PROVIDED TO BUYER ON AN "AS-IS" BASIS ONLY. MEDIATEK EXPRESSLY
* DISCLAIMS ANY AND ALL WARRANTIES, EXPRESS OR IMPLIED, INCLUDING BUT NOT
* LIMITED TO THE IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
* PARTICULAR PURPOSE OR NONINFRINGEMENT. NEITHER DOES MEDIATEK PROVIDE
* ANY WARRANTY WHATSOEVER WITH RESPECT TO THE SOFTWARE OF ANY THIRD PARTY
* WHICH MAY BE USED BY, INCORPORATED IN, OR SUPPLIED WITH THE MEDIATEK
* SOFTWARE, AND BUYER AGREES TO LOOK ONLY TO SUCH THIRD PARTY FOR ANY
* WARRANTY CLAIM RELATING THERETO. MEDIATEK SHALL ALSO NOT BE RESPONSIBLE
* FOR ANY MEDIATEK SOFTWARE RELEASES MADE TO BUYER'S SPECIFICATION OR TO
* CONFORM TO A PARTICULAR STANDARD OR OPEN FORUM.
*
* BUYER'S SOLE AND EXCLUSIVE REMEDY AND MEDIATEK'S ENTIRE AND CUMULATIVE
* LIABILITY WITH RESPECT TO THE MEDIATEK SOFTWARE RELEASED HEREUNDER WILL
* BE, AT MEDIATEK'S OPTION, TO REVISE OR REPLACE THE MEDIATEK SOFTWARE AT
* ISSUE, OR REFUND ANY SOFTWARE LICENSE FEES OR SERVICE CHARGE PAID BY
* BUYER TO MEDIATEK FOR SUCH MEDIATEK SOFTWARE AT ISSUE.
*
* THE TRANSACTION CONTEMPLATED HEREUNDER SHALL BE CONSTRUED IN ACCORDANCE
* WITH THE LAWS OF THE STATE OF CALIFORNIA, USA, EXCLUDING ITS CONFLICT
* OF LAWS PRINCIPLES.  ANY DISPUTES, CONTROVERSIES OR CLAIMS ARISING
* THEREOF AND RELATED THERETO SHALL BE SETTLED BY ARBITRATION IN SAN
* FRANCISCO, CA, UNDER THE RULES OF THE INTERNATIONAL CHAMBER OF COMMERCE
* (ICC).
********************************************************************************
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

#define P2P_MODE_INF_NAME "p2p%d";
#define AP_MODE_INF_NAME "wlan%d";  

//#define MAX_INF_NAME_LEN 15
//#define MIN_INF_NAME_LEN 1

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
#if defined(CONFIG_HAS_EARLYSUSPEND)
extern int glRegisterEarlySuspend(
    struct early_suspend        *prDesc,
    early_suspend_callback      wlanSuspend,
    late_resume_callback        wlanResume);

extern int glUnregisterEarlySuspend(struct early_suspend *prDesc);
#endif

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
VOID
p2pCheckInterfaceName(
    VOID
    )
{

    if(mode) {
        mode = RUNNING_AP_MODE;
        ifname = AP_MODE_INF_NAME;
    }
#if 0
    UINT_32 ifLen = 0;

    if(ifname) {
        ifLen = strlen(ifname);

        if(ifLen > MAX_INF_NAME_LEN) {
            ifname[MAX_INF_NAME_LEN] = '\0';
        }
        else if( ifLen < MIN_INF_NAME_LEN  ) {
            ifname = P2P_MODE_INF_NAME;
        }
    } else {
        ifname = P2P_MODE_INF_NAME;
    }
#endif
}


extern UINT_8 g_aucBufIpAddr[32];

static void wlanP2PEarlySuspend(void)
{
    struct net_device *prDev = NULL;
    P_GLUE_INFO_T prGlueInfo = NULL;
    UINT_8  ip[4] = { 0 };
    UINT_32 u4NumIPv4 = 0;
#ifdef  CONFIG_IPV6
    UINT_8  ip6[16] = { 0 };     // FIX ME: avoid to allocate large memory in stack
    UINT_32 u4NumIPv6 = 0;
#endif
    UINT_32 i;
	P_PARAM_NETWORK_ADDRESS_IP prParamIpAddr;

    printk(KERN_INFO "*********p2pEarlySuspend************\n");

    if(!wlanExportGlueInfo(&prGlueInfo)) {
        printk(KERN_INFO "*********p2pEarlySuspend ignored************\n");
        return;
    }

    ASSERT(prGlueInfo);
    // <1> Sanity check and acquire the net_device
    prDev = prGlueInfo->prP2PInfo->prDevHandler;
    ASSERT(prDev);

    // <3> get the IPv4 address
    if(!prDev || !(prDev->ip_ptr)||\
        !((struct in_device *)(prDev->ip_ptr))->ifa_list||\
        !(&(((struct in_device *)(prDev->ip_ptr))->ifa_list->ifa_local))){
        printk(KERN_INFO "ip is not avaliable.\n");
        return;
    }

    // <4> copy the IPv4 address
    kalMemCopy(ip, &(((struct in_device *)(prDev->ip_ptr))->ifa_list->ifa_local), sizeof(ip));
    printk(KERN_INFO"ip is %d.%d.%d.%d\n",
            ip[0],ip[1],ip[2],ip[3]);

   // todo: traverse between list to find whole sets of IPv4 addresses
    if (!((ip[0] == 0) &&
         (ip[1] == 0) &&
         (ip[2] == 0) &&
         (ip[3] == 0))) {
        u4NumIPv4++;
    }

#ifdef  CONFIG_IPV6
    // <5> get the IPv6 address
    if(!prDev || !(prDev->ip6_ptr)||\
        !((struct in_device *)(prDev->ip6_ptr))->ifa_list||\
        !(&(((struct in_device *)(prDev->ip6_ptr))->ifa_list->ifa_local))){
        printk(KERN_INFO "ipv6 is not avaliable.\n");
        return;
    }
    // <6> copy the IPv6 address
    kalMemCopy(ip6, &(((struct in_device *)(prDev->ip6_ptr))->ifa_list->ifa_local), sizeof(ip6));
    printk(KERN_INFO"ipv6 is %d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d\n",
            ip6[0],ip6[1],ip6[2],ip6[3],
            ip6[4],ip6[5],ip6[6],ip6[7],
            ip6[8],ip6[9],ip6[10],ip6[11],
            ip6[12],ip6[13],ip6[14],ip6[15]
            );
    // todo: traverse between list to find whole sets of IPv6 addresses

    if (!((ip6[0] == 0) &&
         (ip6[1] == 0) &&
         (ip6[2] == 0) &&
         (ip6[3] == 0) &&
         (ip6[4] == 0) &&
         (ip6[5] == 0))) {
    }

#endif
    // <7> set up the ARP filter
    {
        WLAN_STATUS rStatus = WLAN_STATUS_FAILURE;
        UINT_32 u4SetInfoLen = 0;
//        UINT_8 aucBuf[32] = {0};
        UINT_32 u4Len = OFFSET_OF(PARAM_NETWORK_ADDRESS_LIST, arAddress);
        P_PARAM_NETWORK_ADDRESS_LIST prParamNetAddrList = (P_PARAM_NETWORK_ADDRESS_LIST)g_aucBufIpAddr;//aucBuf;
        P_PARAM_NETWORK_ADDRESS prParamNetAddr = prParamNetAddrList->arAddress;

        kalMemZero(g_aucBufIpAddr, sizeof(g_aucBufIpAddr));

        prParamNetAddrList->u4AddressCount = u4NumIPv4 + u4NumIPv6;
        prParamNetAddrList->u2AddressType = PARAM_PROTOCOL_ID_TCP_IP;
        for (i = 0; i < u4NumIPv4; i++) {
            prParamNetAddr->u2AddressLength = sizeof(PARAM_NETWORK_ADDRESS_IP);//4;;
            prParamNetAddr->u2AddressType = PARAM_PROTOCOL_ID_TCP_IP;;
#if 0
            kalMemCopy(prParamNetAddr->aucAddress, ip, sizeof(ip));
            prParamNetAddr = (P_PARAM_NETWORK_ADDRESS)((UINT_32)prParamNetAddr + sizeof(ip));
            u4Len += OFFSET_OF(PARAM_NETWORK_ADDRESS, aucAddress) + sizeof(ip);
#else
            prParamIpAddr = (P_PARAM_NETWORK_ADDRESS_IP)prParamNetAddr->aucAddress;
            kalMemCopy(&prParamIpAddr->in_addr, ip, sizeof(ip));

//            prParamNetAddr = (P_PARAM_NETWORK_ADDRESS)((UINT_32)prParamNetAddr + sizeof(PARAM_NETWORK_ADDRESS));    // TODO: frog. The pointer is not right.

            prParamNetAddr = (P_PARAM_NETWORK_ADDRESS) ((UINT_32) prParamNetAddr +
            (UINT_32) (prParamNetAddr->u2AddressLength + OFFSET_OF(PARAM_NETWORK_ADDRESS, aucAddress)));

            u4Len += OFFSET_OF(PARAM_NETWORK_ADDRESS, aucAddress) + sizeof(PARAM_NETWORK_ADDRESS_IP);
#endif
        }
#ifdef  CONFIG_IPV6
        for (i = 0; i < u4NumIPv6; i++) {
            prParamNetAddr->u2AddressLength = 6;;
            prParamNetAddr->u2AddressType = PARAM_PROTOCOL_ID_TCP_IP;;
            kalMemCopy(prParamNetAddr->aucAddress, ip6, sizeof(ip6));
//            prParamNetAddr = (P_PARAM_NETWORK_ADDRESS)((UINT_32)prParamNetAddr + sizeof(ip6));

            prParamNetAddr = (P_PARAM_NETWORK_ADDRESS) ((UINT_32) prParamNetAddr +
            (UINT_32) (prParamNetAddr->u2AddressLength + OFFSET_OF(PARAM_NETWORK_ADDRESS, aucAddress)));

            u4Len += OFFSET_OF(PARAM_NETWORK_ADDRESS, aucAddress) + sizeof(ip6);
       }
#endif
        ASSERT(u4Len <= sizeof(g_aucBufIpAddr/*aucBuf*/));

        rStatus = kalIoctl(prGlueInfo,
                wlanoidSetP2pSetNetworkAddress,
                (PVOID)prParamNetAddrList,
                u4Len,
                FALSE,
                FALSE,
                TRUE,
                TRUE,
                &u4SetInfoLen);

        if (rStatus != WLAN_STATUS_SUCCESS) {
            printk(KERN_INFO DRV_NAME"set HW pattern filter fail 0x%lx\n", rStatus);
        }
    }
}


static void wlanP2PLateResume(void)
{
    struct net_device *prDev = NULL;
    P_GLUE_INFO_T prGlueInfo = NULL;
    UINT_8  ip[4] = { 0 };
#ifdef  CONFIG_IPV6
    UINT_8  ip6[16] = { 0 };     // FIX ME: avoid to allocate large memory in stack
#endif

    printk(KERN_INFO "*********wlanP2PLateResume************\n");
    if(!wlanExportGlueInfo(&prGlueInfo)) {
        printk(KERN_INFO "*********p2pLateResume ignored************\n");
        return;
    }

    ASSERT(prGlueInfo);
    // <1> Sanity check and acquire the net_device
    prDev = prGlueInfo->prP2PInfo->prDevHandler;
    ASSERT(prDev);

   // <3> get the IPv4 address
    if(!prDev || !(prDev->ip_ptr)||\
        !((struct in_device *)(prDev->ip_ptr))->ifa_list||\
        !(&(((struct in_device *)(prDev->ip_ptr))->ifa_list->ifa_local))){
        printk(KERN_INFO "ip is not avaliable.\n");
        return;
    }

    // <4> copy the IPv4 address
    kalMemCopy(ip, &(((struct in_device *)(prDev->ip_ptr))->ifa_list->ifa_local), sizeof(ip));
    printk(KERN_INFO"ip is %d.%d.%d.%d\n",
            ip[0],ip[1],ip[2],ip[3]);

#ifdef  CONFIG_IPV6
    // <5> get the IPv6 address
    if(!prDev || !(prDev->ip6_ptr)||\
        !((struct in_device *)(prDev->ip6_ptr))->ifa_list||\
        !(&(((struct in_device *)(prDev->ip6_ptr))->ifa_list->ifa_local))){
        printk(KERN_INFO "ipv6 is not avaliable.\n");
        return;
    }
    // <6> copy the IPv6 address
    kalMemCopy(ip6, &(((struct in_device *)(prDev->ip6_ptr))->ifa_list->ifa_local), sizeof(ip6));
    printk(KERN_INFO"ipv6 is %d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d\n",
            ip6[0],ip6[1],ip6[2],ip6[3],
            ip6[4],ip6[5],ip6[6],ip6[7],
            ip6[8],ip6[9],ip6[10],ip6[11],
            ip6[12],ip6[13],ip6[14],ip6[15]
            );
#endif
    // <7> clear the ARP filter
    {
        WLAN_STATUS rStatus = WLAN_STATUS_FAILURE;
        UINT_32 u4SetInfoLen = 0;
//        UINT_8 aucBuf[32] = {0};
        UINT_32 u4Len = sizeof(PARAM_NETWORK_ADDRESS_LIST);
        P_PARAM_NETWORK_ADDRESS_LIST prParamNetAddrList = (P_PARAM_NETWORK_ADDRESS_LIST)g_aucBufIpAddr;//aucBuf;

        kalMemZero(g_aucBufIpAddr, sizeof(g_aucBufIpAddr));

        prParamNetAddrList->u4AddressCount = 0;
        prParamNetAddrList->u2AddressType = PARAM_PROTOCOL_ID_TCP_IP;

        ASSERT(u4Len <= sizeof(g_aucBufIpAddr/*aucBuf*/));
        rStatus = kalIoctl(prGlueInfo,
                wlanoidSetP2pSetNetworkAddress,
                (PVOID)prParamNetAddrList,
                u4Len,
                FALSE,
                FALSE,
                TRUE,
                TRUE,
                &u4SetInfoLen);

        if (rStatus != WLAN_STATUS_SUCCESS) {
            printk(KERN_INFO DRV_NAME"set HW pattern filter fail 0x%lx\n", rStatus);
        }
    }
}

#if defined(CONFIG_HAS_EARLYSUSPEND)
static struct early_suspend mt6620_p2p_early_suspend_desc = {
    .level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN,
};

static void p2p_early_suspend(struct early_suspend *h)
{
    printk(KERN_INFO "*********wlanP2P_early_suspend************\n");
    wlanP2PEarlySuspend();
}

static void p2p_late_resume(struct early_suspend *h)
{
    printk(KERN_INFO "*********wlanP2P_late_resume************\n");
    wlanP2PLateResume();
}
#endif

/*----------------------------------------------------------------------------*/
/*!
* \brief
*       run p2p init procedure, include register pointer to wlan
*                                                     glue register p2p
*                                                     set p2p registered flag
* \retval 1     Success
*/
/*----------------------------------------------------------------------------*/
BOOLEAN
p2pLaunch(
    P_GLUE_INFO_T prGlueInfo
    )
{

    printk("p2p Launch\n");

    if(prGlueInfo->prAdapter->fgIsP2PRegistered == TRUE) {
        printk("p2p already registered\n");
        return FALSE;
    }
    else if(glRegisterP2P(prGlueInfo, ifname, (BOOLEAN)mode)) {
        prGlueInfo->prAdapter->fgIsP2PRegistered = TRUE;

        printk("Launch success, fgIsP2PRegistered TRUE.\n");

#if defined(CONFIG_HAS_EARLYSUSPEND)
        /* Here, we register the early suspend and resume callback  */
        glRegisterEarlySuspend(&mt6620_p2p_early_suspend_desc, p2p_early_suspend, p2p_late_resume);
#endif

        return TRUE;
    }
    else {
        printk("Launch Fail\n");
    }

    return FALSE;
}


VOID
p2pSetMode (
    IN BOOLEAN fgIsAPMOde
    ) 
{
    if (fgIsAPMOde) {
        mode = RUNNING_AP_MODE;
        ifname = AP_MODE_INF_NAME;
    }
    else {
        mode = RUNNING_P2P_MODE;
        ifname = P2P_MODE_INF_NAME;
    }

    return;
} /* p2pSetMode */


/*----------------------------------------------------------------------------*/
/*!
* \brief
*       run p2p exit procedure, include unregister pointer to wlan
*                                                     glue unregister p2p
*                                                     set p2p registered flag

* \retval 1     Success
*/
/*----------------------------------------------------------------------------*/
BOOLEAN
p2pRemove(
    P_GLUE_INFO_T prGlueInfo
    )
{
    if(prGlueInfo->prAdapter->fgIsP2PRegistered == FALSE) {
        printk("p2p is not Registered.\n");
        return FALSE;
    }
    else {

#if defined(CONFIG_HAS_EARLYSUSPEND)
        glUnregisterEarlySuspend(&mt6620_p2p_early_suspend_desc);
#endif
        /*Check p2p fsm is stop or not. If not then stop now*/
        if(IS_P2P_ACTIVE(prGlueInfo->prAdapter)) {
            p2pStopImmediate(prGlueInfo);
        }
        prGlueInfo->prAdapter->fgIsP2PRegistered = FALSE;
        glUnregisterP2P(prGlueInfo);
        /*p2p is removed successfully*/
        return TRUE;
    }
    return FALSE;
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

    /*check interface name validation*/
    p2pCheckInterfaceName();

    printk( KERN_INFO DRV_NAME "InitP2P, Ifname: %s, Mode: %s\n", ifname, mode ? "AP":"P2P");

    /*register p2p init & exit function to wlan sub module handler*/
    wlanSubModRegisterInitExit(p2pLaunch, p2pRemove, P2P_MODULE);

    /*if wlan is not start yet, do nothing
        * p2pLaunch will be called by txthread while wlan start
        */
    /*if wlan is not started yet, return FALSE*/
    if(wlanExportGlueInfo(&prGlueInfo)) {
        wlanSubModInit(prGlueInfo);
        return ( prGlueInfo->prAdapter->fgIsP2PRegistered? 0: -EIO);
    }

    return 0;
} /* end of initP2P() */


/*----------------------------------------------------------------------------*/
/*!
* \brief Driver exit point when the driver as a Linux Module is removed. Called
*        at module unload time, by the user level modutils application: rmmod.
*        This is our last chance to clean up after ourselves.
*
* \return (none)
*/
/*----------------------------------------------------------------------------*/
//1 Module Leave Point
static VOID __exit exitP2P(void)
{
    P_GLUE_INFO_T prGlueInfo;

    printk( KERN_INFO DRV_NAME "ExitP2P\n");

    /*if wlan is not started yet, return FALSE*/
    if(wlanExportGlueInfo(&prGlueInfo)) {
        wlanSubModExit(prGlueInfo);
    }
    /*UNregister p2p init & exit function to wlan sub module handler*/
    wlanSubModRegisterInitExit(NULL, NULL, P2P_MODULE);
} /* end of exitP2P() */
#endif

