/*
** $Id:
//Department/DaVinci/TRUNK/MT6620_5931_WiFi_Driver/os/linux/include/gl_p2p_os.h#28 $
*/

/*! \file   gl_p2p_os.h
    \brief  List the external reference to OS for p2p GLUE Layer.

    In this file we define the data structure - GLUE_INFO_T to store those objects
    we acquired from OS - e.g. TIMER, SPINLOCK, NET DEVICE ... . And all the
    external reference (header file, extern func() ..) to OS for GLUE Layer should
    also list down here.
*/





#ifndef _GL_P2P_OS_H
#define _GL_P2P_OS_H

/*******************************************************************************
*                         C O M P I L E R   F L A G S
********************************************************************************
*/

/*******************************************************************************
*                    E X T E R N A L   R E F E R E N C E S
********************************************************************************
*/

/*******************************************************************************
*                              C O N S T A N T S
********************************************************************************
*/

/*******************************************************************************
*                                 M A C R O S
********************************************************************************
*/

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

/*******************************************************************************
*                  F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/


struct _GL_P2P_INFO_T {

    /* Device handle */
    struct net_device           *prDevHandler;

    #if CFG_ENABLE_WIFI_DIRECT_CFG_80211
    /* cfg80211 */
    struct wireless_dev         wdev;

    struct cfg80211_scan_request    *prScanRequest;

    UINT_64 u8Cookie;

    /* Generation for station list update. */
    INT_32 i4Generation;

    UINT_32 u4OsMgmtFrameFilter;

    #endif

    /* Device statistics */
    struct net_device_stats     rNetDevStats;

    /* glue layer variables */
    /*move to glueinfo->adapter*/
   // BOOLEAN                     fgIsRegistered;
    UINT_32                     u4FreqInKHz;    /* frequency */
    UINT_8                      ucRole;         /* 0: P2P Device, 1: Group Client, 2: Group Owner */
    UINT_8                      ucIntent;       /* range: 0-15 */
    UINT_8                      ucScanMode;     /* 0: Search & Listen, 1: Scan without probe response */

    ENUM_PARAM_MEDIA_STATE_T    eState;
    UINT_32                     u4PacketFilter;
    PARAM_MAC_ADDRESS           aucMCAddrList[MAX_NUM_GROUP_ADDR];

    /* connection-requested peer information */
    UINT_8                      aucConnReqDevName[32];
    INT_32                      u4ConnReqNameLength;
    PARAM_MAC_ADDRESS           rConnReqPeerAddr;
    PARAM_MAC_ADDRESS           rConnReqGroupAddr;   /* For invitation group. */
    UINT_8                      ucConnReqDevType;
    INT_32                      i4ConnReqConfigMethod;
    INT_32                      i4ConnReqActiveConfigMethod;

    UINT_32                     u4CipherPairwise;
    UINT_8                      ucWSCRunning;

    UINT_8                      aucWSCIE[3][400]; /* 0 for beacon, 1 for probe req, 2 for probe response */
    UINT_16                     u2WSCIELen[3];

	UINT_8                      ucOperatingChnl;
	UINT_8                      ucInvitationType;

	UINT_32                     u4InvStatus;

    /* For SET_STRUCT/GET_STRUCT */
    UINT_8                      aucOidBuf[4096];

#if 1 /* CFG_SUPPORT_ANTI_PIRACY */
    UINT_8                      aucSecCheck[256];
    UINT_8                      aucSecCheckRsp[256];
#endif

    /* Hotspot Client Management */
    PARAM_MAC_ADDRESS           aucblackMACList[8];
    UINT_8                      ucMaxClients;

};


BOOLEAN
p2pRegisterToWlan(
    P_GLUE_INFO_T prGlueInfo
    );

BOOLEAN
p2pUnregisterToWlan(
    P_GLUE_INFO_T prGlueInfo
    );

BOOLEAN
p2pLaunch(
    P_GLUE_INFO_T prGlueInfo
    );

BOOLEAN
p2pRemove(
    P_GLUE_INFO_T prGlueInfo
    );

VOID
p2pSetMode(
    IN BOOLEAN fgIsAPMOde
    );


BOOLEAN
glRegisterP2P(
    P_GLUE_INFO_T prGlueInfo,
    const char *prDevName,
    BOOLEAN fgIsApMode
    );

BOOLEAN
glUnregisterP2P(
    P_GLUE_INFO_T prGlueInfo
    );

BOOLEAN
p2pNetRegister(
    P_GLUE_INFO_T prGlueInfo,
    BOOLEAN fgIsRtnlLockAcquired
    );

BOOLEAN
p2pNetUnregister(
    P_GLUE_INFO_T prGlueInfo,
    BOOLEAN fgIsRtnlLockAcquired
    );

BOOLEAN
p2pStopImmediate(
    P_GLUE_INFO_T prGlueInfo
    );

BOOLEAN
p2PFreeInfo(
    P_GLUE_INFO_T prGlueInfo
    );


#endif
