/*
** $Id: //Department/DaVinci/BRANCHES/MT662X_593X_WIFI_DRIVER_V2_3/include/nic/bow.h#1 $
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
** $Log: bow.h $
 *
 * 01 16 2012 chinghwa.yu
 * [WCXRP00000065] Update BoW design and settings
 * Support BOW for 5GHz band.
 *
 * 05 25 2011 terry.wu
 * [WCXRP00000735] [MT6620 Wi-Fi][BoW][FW/Driver] Protect BoW connection establishment
 * Add BoW Cancel Scan Request and Turn On deactive network function.
 *
 * 05 22 2011 terry.wu
 * [WCXRP00000735] [MT6620 Wi-Fi][BoW][FW/Driver] Protect BoW connection establishment
 * Submit missing BoW header files.
 *
 * 03 27 2011 chinghwa.yu
 * [WCXRP00000065] Update BoW design and settings
 * Support multiple physical link.
 *
 * 03 06 2011 chinghwa.yu
 * [WCXRP00000065] Update BoW design and settings
 * Sync BOW Driver to latest person development branch version..
 *
 * 02 10 2011 chinghwa.yu
 * [WCXRP00000065] Update BoW design and settings
 * Fix kernel API change issue.
 * Before ALPS 2.2 (2.2 included), kfifo_alloc() is 
 * struct kfifo *kfifo_alloc(unsigned int size, gfp_t gfp_mask, spinlock_t *lock);
 * After ALPS 2.3, kfifo_alloc() is changed to
 * int kfifo_alloc(struct kfifo *fifo, unsigned int size, gfp_t gfp_mask);
 *
 * 02 10 2011 chinghwa.yu
 * [WCXRP00000065] Update BoW design and settings
 * Update BOW structure.
 *
 * 02 09 2011 cp.wu
 * [WCXRP00000430] [MT6620 Wi-Fi][Firmware][Driver] Create V1.2 branch for MT6620E1 and MT6620E3
 * create V1.2 driver branch based on label MT6620_WIFI_DRIVER_V1_2_110209_1031 
 * with BOW and P2P enabled as default
 *
 * 02 08 2011 chinghwa.yu
 * [WCXRP00000065] Update BoW design and settings
 * Replace kfifo_get and kfifo_put with kfifo_out and kfifo_in.
 * Update BOW get MAC status, remove returning event for AIS network type.
 *
 * 01 11 2011 chinghwa.yu
 * [WCXRP00000065] Update BoW design and settings
 * Add Activity Report definition.
 *
 * 10 18 2010 chinghwa.yu
 * [WCXRP00000110] [MT6620 Wi-Fi] [Driver] Fix BoW Connected event size
 * Fix wrong BoW event size.
 *
 * 07 15 2010 cp.wu
 * 
 * sync. bluetooth-over-Wi-Fi interface to driver interface document v0.2.6.
 *
 * 07 08 2010 cp.wu
 * 
 * [WPD00003833] [MT6620 and MT5931] Driver migration - move to new repository.
 *
 * 06 06 2010 kevin.huang
 * [WPD00003832][MT6620 5931] Create driver base 
 * [MT6620 5931] Create driver base
 *
 * 05 13 2010 cp.wu
 * [WPD00003823][MT6620 Wi-Fi] Add Bluetooth-over-Wi-Fi support 
 * 1) all BT physical handles shares the same RSSI/Link Quality.
 * 2) simplify BT command composing
 *
 * 04 28 2010 cp.wu
 * [WPD00003823][MT6620 Wi-Fi] Add Bluetooth-over-Wi-Fi support 
 * change prefix for data structure used to communicate with 802.11 PAL
 * to avoid ambiguous naming with firmware interface
 *
 * 04 27 2010 cp.wu
 * [WPD00003823][MT6620 Wi-Fi] Add Bluetooth-over-Wi-Fi support 
 * basic implementation for EVENT_BT_OVER_WIFI
 *
 * 04 13 2010 cp.wu
 * [WPD00003823][MT6620 Wi-Fi] Add Bluetooth-over-Wi-Fi support 
 * add framework for BT-over-Wi-Fi support.
 *  *  *  *  *  *  * 1) prPendingCmdInfo is replaced by queue for multiple handler capability
 *  *  *  *  *  *  * 2) command sequence number is now increased atomically 
 *  *  *  *  *  *  * 3) private data could be hold and taken use for other purpose
 *
 * 04 09 2010 cp.wu
 * [WPD00003823][MT6620 Wi-Fi] Add Bluetooth-over-Wi-Fi support 
 * sync. with design document for interface change.
 *
 * 04 02 2010 cp.wu
 * [WPD00003823][MT6620 Wi-Fi] Add Bluetooth-over-Wi-Fi support 
 * Wi-Fi driver no longer needs to implement 802.11 PAL, thus replaced by wrapping command/event definitions
 *
 * 03 16 2010 cp.wu
 * [WPD00003823][MT6620 Wi-Fi] Add Bluetooth-over-Wi-Fi support 
 * correct typo.
 *
 * 03 16 2010 cp.wu
 * [WPD00003823][MT6620 Wi-Fi] Add Bluetooth-over-Wi-Fi support 
 * update for all command/event needed to be supported by 802.11 PAL.
 *
 * 03 16 2010 cp.wu
 * [WPD00003823][MT6620 Wi-Fi] Add Bluetooth-over-Wi-Fi support 
 * build up basic data structure and definitions to support BT-over-WiFi
 *
*/

#ifndef _BOW_H_
#define _BOW_H_

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

#define BOWDEVNAME          "bow0"

#define MAX_BOW_NUMBER_OF_CHANNEL_2G4            14
#define MAX_BOW_NUMBER_OF_CHANNEL_5G              4
#define MAX_BOW_NUMBER_OF_CHANNEL                    18 //(MAX_BOW_NUMBER_OF_CHANNEL_2G4 + MAX_BOW_NUMBER_OF_CHANNEL_5G)

#define MAX_ACTIVITY_REPORT                                    2
#define MAX_ACTIVITY_REPROT_TIME                          660

#define ACTIVITY_REPORT_STATUS_SUCCESS              0
#define ACTIVITY_REPORT_STATUS_FAILURE               1
#define ACTIVITY_REPORT_STATUS_TIME_INVALID     2
#define ACTIVITY_REPORT_STATUS_OTHERS                3

#define ACTIVITY_REPORT_SCHEDULE_UNKNOWN        0           //Does not know the schedule of the interference
#define ACTIVITY_REPORT_SCHEDULE_KNOWN             1

/*******************************************************************************
*                                 M A C R O S
********************************************************************************
*/

/*******************************************************************************
*                         D A T A   T Y P E S
********************************************************************************
*/
typedef struct _BT_OVER_WIFI_COMMAND_HEADER_T {
    UINT_8      ucCommandId;
    UINT_8      ucSeqNumber;
    UINT_16     u2PayloadLength;
} AMPC_COMMAND_HEADER_T, *P_AMPC_COMMAND_HEADER_T;

typedef struct _BT_OVER_WIFI_COMMAND {
    AMPC_COMMAND_HEADER_T   rHeader;
    UINT_8                  aucPayload[0];
} AMPC_COMMAND, *P_AMPC_COMMAND;

typedef struct _BT_OVER_WIFI_EVENT_HEADER_T {
    UINT_8      ucEventId;
    UINT_8      ucSeqNumber;
    UINT_16     u2PayloadLength;
} AMPC_EVENT_HEADER_T, *P_AMPC_EVENT_HEADER_T;

typedef struct _BT_OVER_WIFI_EVENT {
    AMPC_EVENT_HEADER_T rHeader;
    UINT_8                      aucPayload[0];
} AMPC_EVENT, *P_AMPC_EVENT;

typedef struct _CHANNEL_DESC_T {
    UINT_8     ucChannelBand;
    UINT_8     ucChannelNum;
} CHANNEL_DESC, P_CHANNEL_DESC;

// Command Structures 
typedef struct _BOW_SETUP_CONNECTION {
//Fixed to 2.4G
    UINT_8      ucChannelNum;
    UINT_8      ucReserved1;
    UINT_8      aucPeerAddress[6];
    UINT_16     u2BeaconInterval;
    UINT_8      ucTimeoutDiscovery;
    UINT_8      ucTimeoutInactivity;
    UINT_8      ucRole;
    UINT_8      ucPAL_Capabilities;
    INT_8        cMaxTxPower;
    UINT_8      ucReserved2;

//Pending, for future BOW 5G supporting.
/*    UINT_8          aucPeerAddress[6];
    UINT_16         u2BeaconInterval;
    UINT_8          ucTimeoutDiscovery;
    UINT_8          ucTimeoutInactivity;
    UINT_8          ucRole;
    UINT_8          ucPAL_Capabilities;
    INT_8           cMaxTxPower;
    UINT_8          ucChannelListNum;
    CHANNEL_DESC    arChannelList[1];
*/
} BOW_SETUP_CONNECTION, *P_BOW_SETUP_CONNECTION;

typedef struct _BOW_DESTROY_CONNECTION {
    UINT_8      aucPeerAddress[6];
    UINT_8      aucReserved[2];
} BOW_DESTROY_CONNECTION, *P_BOW_DESTROY_CONNECTION;

typedef struct _BOW_SET_PTK {
    UINT_8      aucPeerAddress[6];
    UINT_8      aucReserved[2];
    UINT_8      aucTemporalKey[16];
} BOW_SET_PTK, *P_BOW_SET_PTK;

typedef struct _BOW_READ_RSSI {
    UINT_8      aucPeerAddress[6];
    UINT_8      aucReserved[2];
} BOW_READ_RSSI, *P_BOW_READ_RSSI;

typedef struct _BOW_READ_LINK_QUALITY {
    UINT_8      aucPeerAddress[6];
    UINT_8      aucReserved[2];
} BOW_READ_LINK_QUALITY, *P_BOW_READ_LINK_QUALITY;

typedef struct _BOW_SHORT_RANGE_MODE {
    UINT_8      aucPeerAddress[6];
    INT_8       cTxPower;
    UINT_8      ucReserved;
} BOW_SHORT_RANGE_MODE, *P_BOW_SHORT_RANGE_MODE;

// Event Structures
typedef struct _BOW_COMMAND_STATUS {
    UINT_8      ucStatus;
    UINT_8      ucReserved[3];
} BOW_COMMAND_STATUS, *P_BOW_COMMAND_STATUS;

typedef struct _BOW_MAC_STATUS {
    UINT_8                 aucMacAddr[6];
    UINT_8                 ucAvailability;
    UINT_8                 ucNumOfChannel;
    CHANNEL_DESC    arChannelList[MAX_BOW_NUMBER_OF_CHANNEL];
} BOW_MAC_STATUS, *P_BOW_MAC_STATUS;

typedef struct _BOW_LINK_CONNECTED {
    CHANNEL_DESC    rChannel;
    UINT_8          aucReserved;
    UINT_8          aucPeerAddress[6];
} BOW_LINK_CONNECTED, *P_BOW_LINK_CONNECTED;

typedef struct _BOW_LINK_DISCONNECTED {
    UINT_8  ucReason;
    UINT_8  aucReserved;
    UINT_8  aucPeerAddress[6];
} BOW_LINK_DISCONNECTED, *P_BOW_LINK_DISCONNECTED;

typedef struct _BOW_RSSI {
    INT_8   cRssi;
    UINT_8  aucReserved[3];
} BOW_RSSI, *P_BOW_RSSI;

typedef struct _BOW_LINK_QUALITY {
    UINT_8  ucLinkQuality;
    UINT_8  aucReserved[3];
} BOW_LINK_QUALITY, *P_BOW_LINK_QUALITY;

typedef enum _ENUM_BOW_CMD_ID_T {
    BOW_CMD_ID_GET_MAC_STATUS = 1,
    BOW_CMD_ID_SETUP_CONNECTION,
    BOW_CMD_ID_DESTROY_CONNECTION,
    BOW_CMD_ID_SET_PTK,
    BOW_CMD_ID_READ_RSSI,
    BOW_CMD_ID_READ_LINK_QUALITY,
    BOW_CMD_ID_SHORT_RANGE_MODE,
    BOW_CMD_ID_GET_CHANNEL_LIST,
} ENUM_BOW_CMD_ID_T, *P_ENUM_BOW_CMD_ID_T;

typedef enum _ENUM_BOW_EVENT_ID_T {
    BOW_EVENT_ID_COMMAND_STATUS = 1,
    BOW_EVENT_ID_MAC_STATUS,
    BOW_EVENT_ID_LINK_CONNECTED,
    BOW_EVENT_ID_LINK_DISCONNECTED,
    BOW_EVENT_ID_RSSI,
    BOW_EVENT_ID_LINK_QUALITY,
    BOW_EVENT_ID_CHANNEL_LIST,
    BOW_EVENT_ID_CHANNEL_SELECTED,
} ENUM_BOW_EVENT_ID_T, *P_ENUM_BOW_EVENT_ID_T;

typedef enum _ENUM_BOW_DEVICE_STATE {
    BOW_DEVICE_STATE_DISCONNECTED = 0,
    BOW_DEVICE_STATE_DISCONNECTING,
    BOW_DEVICE_STATE_ACQUIRING_CHANNEL,
    BOW_DEVICE_STATE_STARTING,
    BOW_DEVICE_STATE_SCANNING,
    BOW_DEVICE_STATE_CONNECTING,
    BOW_DEVICE_STATE_CONNECTED,
    BOW_DEVICE_STATE_NUM
} ENUM_BOW_DEVICE_STATE, *P_ENUM_BOW_DEVICE_STATE;

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

#endif /*_BOW_H */
