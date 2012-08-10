/*
** $Id: //Department/DaVinci/BRANCHES/WIFI_P2P_DRIVER_V2_2/include/nic/p2p.h#1 $
*/

/*******************************************************************************
* Copyright (c) 2007 MediaTek Inc.
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

/*
** $Log: p2p.h $
 *
 * 03 19 2011 terry.wu
 * [WCXRP00000577] [MT6620 Wi-Fi][Driver][FW] Create V2.0 branch for firmware and driver
 * create V2.0 p2p driver release based on label "MT6620_WIFI_P2P_DRIVER_V2_0_2100_0319_2011" from main trunk.
 *
 * 10 20 2010 wh.su
 * [WCXRP00000124] [MT6620 Wi-Fi] [Driver] Support the dissolve P2P Group
 * Add the code to support disconnect p2p group
 *
 * 09 21 2010 kevin.huang
 * [WCXRP00000054] [MT6620 Wi-Fi][Driver] Restructure driver for second Interface
 * Isolate P2P related function for Hardware Software Bundle
 *
 * 08 03 2010 cp.wu
 * NULL
 * [Wi-Fi Direct] add framework for driver hooks
 *
 * 07 08 2010 cp.wu
 *
 * [WPD00003833] [MT6620 and MT5931] Driver migration - move to new repository.
 *
 * 06 23 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * p2p interface revised to be sync. with HAL
 *
 * 06 06 2010 kevin.huang
 * [WPD00003832][MT6620 5931] Create driver base
 * [MT6620 5931] Create driver base
 *
 * 05 18 2010 cp.wu
 * [WPD00003831][MT6620 Wi-Fi] Add framework for Wi-Fi Direct support
 * add parameter to control:
 * 1) auto group owner
 * 2) P2P-PS parameter (CTWindow, NoA descriptors)
 *
 * 05 18 2010 cp.wu
 * [WPD00003831][MT6620 Wi-Fi] Add framework for Wi-Fi Direct support
 * correct WPS Device Password ID definition.
 *
 * 05 17 2010 cp.wu
 * [WPD00003831][MT6620 Wi-Fi] Add framework for Wi-Fi Direct support
 * implement get scan result.
 *
 * 05 17 2010 cp.wu
 * [WPD00003831][MT6620 Wi-Fi] Add framework for Wi-Fi Direct support
 * add basic handling framework for wireless extension ioctls.
 *
 * 05 14 2010 cp.wu
 * [WPD00003831][MT6620 Wi-Fi] Add framework for Wi-Fi Direct support
 * add ioctl framework for Wi-Fi Direct by reusing wireless extension ioctls as well
 *
 * 05 11 2010 cp.wu
 * [WPD00003831][MT6620 Wi-Fi] Add framework for Wi-Fi Direct support
 * p2p ioctls revised.
 *
 * 05 10 2010 cp.wu
 * [WPD00003831][MT6620 Wi-Fi] Add framework for Wi-Fi Direct support
 * implement basic wi-fi direct framework
 *
 * 05 07 2010 cp.wu
 * [WPD00003831][MT6620 Wi-Fi] Add framework for Wi-Fi Direct support
 * add basic framework for implementating P2P driver hook.
 *
 *
*/

#ifndef _P2P_H
#define _P2P_H

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

// refer to 'Config Methods' in WPS
#define WPS_CONFIG_USBA                 0x0001
#define WPS_CONFIG_ETHERNET             0x0002
#define WPS_CONFIG_LABEL                0x0004
#define WPS_CONFIG_DISPLAY              0x0008
#define WPS_CONFIG_EXT_NFC              0x0010
#define WPS_CONFIG_INT_NFC              0x0020
#define WPS_CONFIG_NFC                  0x0040
#define WPS_CONFIG_PBC                  0x0080
#define WPS_CONFIG_KEYPAD               0x0100

// refer to 'Device Password ID' in WPS
#define WPS_DEV_PASSWORD_ID_PIN         0x0000
#define WPS_DEV_PASSWORD_ID_USER        0x0001
#define WPS_DEV_PASSWORD_ID_MACHINE     0x0002
#define WPS_DEV_PASSWORD_ID_REKEY       0x0003
#define WPS_DEV_PASSWORD_ID_PUSHBUTTON  0x0004
#define WPS_DEV_PASSWORD_ID_REGISTRAR   0x0005


#define P2P_DEVICE_TYPE_NUM         2
#define P2P_DEVICE_NAME_LENGTH      32
#define P2P_NETWORK_NUM             8
#define P2P_MEMBER_NUM              8

#define P2P_WILDCARD_SSID           "DIRECT-"

/*******************************************************************************
*                                 M A C R O S
********************************************************************************
*/

/*******************************************************************************
*                             D A T A   T Y P E S
********************************************************************************
*/

struct _P2P_INFO_T {
    UINT_32 u4DeviceNum;
    EVENT_P2P_DEV_DISCOVER_RESULT_T arP2pDiscoverResult[CFG_MAX_NUM_BSS_LIST];
    PUINT_8 pucCurrIePtr;
    UINT_8 aucCommIePool[CFG_MAX_COMMON_IE_BUF_LEN];   /* A common pool for IE of all scan results. */
};

typedef enum {
    ENUM_P2P_PEER_GROUP,
    ENUM_P2P_PEER_DEVICE,
    ENUM_P2P_PEER_NUM
} ENUM_P2P_PEER_TYPE, *P_ENUM_P2P_PEER_TYPE;

typedef struct _P2P_DEVICE_INFO {
    UINT_8              aucDevAddr[PARAM_MAC_ADDR_LEN];
    UINT_8              aucIfAddr[PARAM_MAC_ADDR_LEN];
    UINT_8              ucDevCapabilityBitmap;
    INT_32              i4ConfigMethod;
    UINT_8              aucPrimaryDeviceType[8];
    UINT_8              aucSecondaryDeviceType[8];
    UINT_8              aucDeviceName[P2P_DEVICE_NAME_LENGTH];
} P2P_DEVICE_INFO, *P_P2P_DEVICE_INFO;

typedef struct _P2P_GROUP_INFO {
    PARAM_SSID_T        rGroupID;
    P2P_DEVICE_INFO     rGroupOwnerInfo;
    UINT_8              ucMemberNum;
    P2P_DEVICE_INFO     arMemberInfo[P2P_MEMBER_NUM];
} P2P_GROUP_INFO, *P_P2P_GROUP_INFO;

typedef struct _P2P_NETWORK_INFO {
    ENUM_P2P_PEER_TYPE      eNodeType;

    union {
        P2P_GROUP_INFO      rGroupInfo;
        P2P_DEVICE_INFO     rDeviceInfo;
    } node;

} P2P_NETWORK_INFO, *P_P2P_NETWORK_INFO;

typedef struct _P2P_NETWORK_LIST {
    UINT_8                  ucNetworkNum;
    P2P_NETWORK_INFO        rP2PNetworkInfo[P2P_NETWORK_NUM];
} P2P_NETWORK_LIST, *P_P2P_NETWORK_LIST;

typedef struct _P2P_DISCONNECT_INFO {
    UINT_8                  ucRole;
    UINT_8                  ucRsv[3];
} P2P_DISCONNECT_INFO, *P_P2P_DISCONNECT_INFO;

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

#endif /*_P2P_H */
