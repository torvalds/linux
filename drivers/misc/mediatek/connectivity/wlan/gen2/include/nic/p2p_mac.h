/*
** Id: //Department/DaVinci/TRUNK/WiFi_P2P_Driver/include/nic/p2p_mac.h#2
*/

/*! \file   "p2p_mac.h"
    \brief  Brief description.

    Detail description.
*/

#ifndef _P2P_MAC_H
#define _P2P_MAC_H

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

#define ACTION_PUBLIC_WIFI_DIRECT                   9
#define ACTION_GAS_INITIAL_REQUEST                 10
#define ACTION_GAS_INITIAL_RESPONSE               11
#define ACTION_GAS_COMEBACK_REQUEST           12
#define ACTION_GAS_COMEBACK_RESPONSE         13

/* P2P 4.2.8.1 - P2P Public Action Frame Type. */
#define P2P_PUBLIC_ACTION_GO_NEGO_REQ               0
#define P2P_PUBLIC_ACTION_GO_NEGO_RSP               1
#define P2P_PUBLIC_ACTION_GO_NEGO_CFM               2
#define P2P_PUBLIC_ACTION_INVITATION_REQ            3
#define P2P_PUBLIC_ACTION_INVITATION_RSP            4
#define P2P_PUBLIC_ACTION_DEV_DISCOVER_REQ          5
#define P2P_PUBLIC_ACTION_DEV_DISCOVER_RSP          6
#define P2P_PUBLIC_ACTION_PROV_DISCOVERY_REQ        7
#define P2P_PUBLIC_ACTION_PROV_DISCOVERY_RSP        8

/* P2P 4.2.9.1 - P2P Action Frame Type */
#define P2P_ACTION_NOTICE_OF_ABSENCE                0
#define P2P_ACTION_P2P_PRESENCE_REQ                 1
#define P2P_ACTION_P2P_PRESENCE_RSP                 2
#define P2P_ACTION_GO_DISCOVER_REQ                  3

#define P2P_PUBLIC_ACTION_FRAME_LEN                (WLAN_MAC_MGMT_HEADER_LEN+8)
#define P2P_ACTION_FRAME_LEN                       (WLAN_MAC_MGMT_HEADER_LEN+7)

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

/* P2P 4.2.8.2 P2P Public Action Frame Format */
typedef struct _P2P_PUBLIC_ACTION_FRAME_T {
	/* MAC header */
	UINT_16 u2FrameCtrl;	/* Frame Control */
	UINT_16 u2Duration;	/* Duration */
	UINT_8 aucDestAddr[MAC_ADDR_LEN];	/* DA */
	UINT_8 aucSrcAddr[MAC_ADDR_LEN];	/* SA */
	UINT_8 aucBSSID[MAC_ADDR_LEN];	/* BSSID */
	UINT_16 u2SeqCtrl;	/* Sequence Control */
	/* P2P Public Action Frame Body */
	UINT_8 ucCategory;	/* Category, 0x04 */
	UINT_8 ucAction;	/* Action Value, 0x09 */
	UINT_8 aucOui[3];	/* 0x50, 0x6F, 0x9A */
	UINT_8 ucOuiType;	/* 0x09 */
	UINT_8 ucOuiSubtype;	/* GO Nego Req/Rsp/Cfm, P2P Invittion Req/Rsp, Device Discoverability Req/Rsp */
	UINT_8 ucDialogToken;	/* Dialog Token. */
	UINT_8 aucInfoElem[1];	/* P2P IE, WSC IE. */
} __KAL_ATTRIB_PACKED__ P2P_PUBLIC_ACTION_FRAME_T, *P_P2P_PUBLIC_ACTION_FRAME_T;

/* P2P 4.2.9.1 -  General Action Frame Format. */
typedef struct _P2P_ACTION_FRAME_T {
	/* MAC header */
	UINT_16 u2FrameCtrl;	/* Frame Control */
	UINT_16 u2Duration;	/* Duration */
	UINT_8 aucDestAddr[MAC_ADDR_LEN];	/* DA */
	UINT_8 aucSrcAddr[MAC_ADDR_LEN];	/* SA */
	UINT_8 aucBSSID[MAC_ADDR_LEN];	/* BSSID */
	UINT_16 u2SeqCtrl;	/* Sequence Control */
	/* P2P Action Frame Body */
	UINT_8 ucCategory;	/* 0x7F */
	UINT_8 aucOui[3];	/* 0x50, 0x6F, 0x9A */
	UINT_8 ucOuiType;	/* 0x09 */
	UINT_8 ucOuiSubtype;	/*  */
	UINT_8 ucDialogToken;
	UINT_8 aucInfoElem[1];
} __KAL_ATTRIB_PACKED__ P2P_ACTION_FRAME_T, *P_P2P_ACTION_FRAME_T;

/* P2P C.1 GAS Public Action Initial Request Frame Format */
typedef struct _GAS_PUBLIC_ACTION_INITIAL_REQUEST_FRAME_T {
	/* MAC header */
	UINT_16 u2FrameCtrl;	/* Frame Control */
	UINT_16 u2Duration;	/* Duration */
	UINT_8 aucDestAddr[MAC_ADDR_LEN];	/* DA */
	UINT_8 aucSrcAddr[MAC_ADDR_LEN];	/* SA */
	UINT_8 aucBSSID[MAC_ADDR_LEN];	/* BSSID */
	UINT_16 u2SeqCtrl;	/* Sequence Control */
	/* P2P Public Action Frame Body */
	UINT_8 ucCategory;	/* Category, 0x04 */
	UINT_8 ucAction;	/* Action Value, 0x09 */
	UINT_8 ucDialogToken;	/* Dialog Token. */
	UINT_8 aucInfoElem[1];	/* Advertisement IE. */
} __KAL_ATTRIB_PACKED__ GAS_PUBLIC_ACTION_INITIAL_REQUEST_FRAME_T, *P_GAS_PUBLIC_ACTION_INITIAL_REQUEST_FRAME_T;

/* P2P C.2 GAS Public Action Initial Response Frame Format */
typedef struct _GAS_PUBLIC_ACTION_INITIAL_RESPONSE_FRAME_T {
	/* MAC header */
	UINT_16 u2FrameCtrl;	/* Frame Control */
	UINT_16 u2Duration;	/* Duration */
	UINT_8 aucDestAddr[MAC_ADDR_LEN];	/* DA */
	UINT_8 aucSrcAddr[MAC_ADDR_LEN];	/* SA */
	UINT_8 aucBSSID[MAC_ADDR_LEN];	/* BSSID */
	UINT_16 u2SeqCtrl;	/* Sequence Control */
	/* P2P Public Action Frame Body */
	UINT_8 ucCategory;	/* Category, 0x04 */
	UINT_8 ucAction;	/* Action Value, 0x09 */
	UINT_8 ucDialogToken;	/* Dialog Token. */
	UINT_16 u2StatusCode;	/* Initial Response. */
	UINT_16 u2ComebackDelay;	/* Initial Response. *//* In unit of TU. */
	UINT_8 aucInfoElem[1];	/* Advertisement IE. */
} __KAL_ATTRIB_PACKED__ GAS_PUBLIC_ACTION_INITIAL_RESPONSE_FRAME_T, *P_GAS_PUBLIC_ACTION_INITIAL_RESPONSE_FRAME_T;

/* P2P C.3-1 GAS Public Action Comeback Request Frame Format */
typedef struct _GAS_PUBLIC_ACTION_COMEBACK_REQUEST_FRAME_T {
	/* MAC header */
	UINT_16 u2FrameCtrl;	/* Frame Control */
	UINT_16 u2Duration;	/* Duration */
	UINT_8 aucDestAddr[MAC_ADDR_LEN];	/* DA */
	UINT_8 aucSrcAddr[MAC_ADDR_LEN];	/* SA */
	UINT_8 aucBSSID[MAC_ADDR_LEN];	/* BSSID */
	UINT_16 u2SeqCtrl;	/* Sequence Control */
	/* P2P Public Action Frame Body */
	UINT_8 ucCategory;	/* Category, 0x04 */
	UINT_8 ucAction;	/* Action Value, 0x09 */
	UINT_8 ucDialogToken;	/* Dialog Token. */
} __KAL_ATTRIB_PACKED__ GAS_PUBLIC_ACTION_COMEBACK_REQUEST_FRAME_T, *P_GAS_PUBLIC_ACTION_COMEBACK_REQUEST_FRAME_T;

/* P2P C.3-2 GAS Public Action Comeback Response Frame Format */
typedef struct _GAS_PUBLIC_ACTION_COMEBACK_RESPONSE_FRAME_T {
	/* MAC header */
	UINT_16 u2FrameCtrl;	/* Frame Control */
	UINT_16 u2Duration;	/* Duration */
	UINT_8 aucDestAddr[MAC_ADDR_LEN];	/* DA */
	UINT_8 aucSrcAddr[MAC_ADDR_LEN];	/* SA */
	UINT_8 aucBSSID[MAC_ADDR_LEN];	/* BSSID */
	UINT_16 u2SeqCtrl;	/* Sequence Control */
	/* P2P Public Action Frame Body */
	UINT_8 ucCategory;	/* Category, 0x04 */
	UINT_8 ucAction;	/* Action Value, 0x09 */
	UINT_8 ucDialogToken;	/* Dialog Token. */
	UINT_16 u2StatusCode;	/* Comeback Response. */
	UINT_8 ucFragmentID;	/*Comeback Response. */
	UINT_16 u2ComebackDelay;	/* Comeback Response. */
	UINT_8 aucInfoElem[1];	/* Advertisement IE. */
} __KAL_ATTRIB_PACKED__ GAS_PUBLIC_ACTION_COMEBACK_RESPONSE_FRAME_T, *P_GAS_PUBLIC_ACTION_COMEBACK_RESPONSE_FRAME_T;

typedef struct _P2P_SD_VENDER_SPECIFIC_CONTENT_T {
	/* Service Discovery Vendor-specific Content. */
	UINT_8 ucOuiSubtype;	/* 0x09 */
	UINT_16 u2ServiceUpdateIndicator;
	UINT_8 aucServiceTLV[1];
} __KAL_ATTRIB_PACKED__ P2P_SD_VENDER_SPECIFIC_CONTENT_T, *P_P2P_SD_VENDER_SPECIFIC_CONTENT_T;

typedef struct _P2P_SERVICE_REQUEST_TLV_T {
	UINT_16 u2Length;
	UINT_8 ucServiceProtocolType;
	UINT_8 ucServiceTransID;
	UINT_8 aucQueryData[1];
} __KAL_ATTRIB_PACKED__ P2P_SERVICE_REQUEST_TLV_T, *P_P2P_SERVICE_REQUEST_TLV_T;

typedef struct _P2P_SERVICE_RESPONSE_TLV_T {
	UINT_16 u2Length;
	UINT_8 ucServiceProtocolType;
	UINT_8 ucServiceTransID;
	UINT_8 ucStatusCode;
	UINT_8 aucResponseData[1];
} __KAL_ATTRIB_PACKED__ P2P_SERVICE_RESPONSE_TLV_T, *P_P2P_SERVICE_RESPONSE_TLV_T;

#endif
