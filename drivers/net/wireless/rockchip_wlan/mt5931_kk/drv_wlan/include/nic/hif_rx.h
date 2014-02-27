/*
** $Id: //Department/DaVinci/BRANCHES/MT662X_593X_WIFI_DRIVER_V2_3/include/nic/hif_rx.h#1 $
*/

/*! \file   "hif_rx.h"
    \brief  Provide HIF RX Header Information between F/W and Driver

    N/A
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
** $Log: hif_rx.h $
 *
 * 09 01 2010 kevin.huang
 * NULL
 * Use LINK LIST operation to process SCAN result
 *
 * 07 16 2010 yarco.yang
 * 
 * 1. Support BSS Absence/Presence Event
 * 2. Support STA change PS mode Event
 * 3. Support BMC forwarding for AP mode.
 *
 * 07 08 2010 cp.wu
 * 
 * [WPD00003833] [MT6620 and MT5931] Driver migration - move to new repository.
 *
 * 06 14 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration 
 * follow-ups for HIF_RX_HEADER_T update:
 * 1) add TCL 
 * 2) add RCPI
 * 3) add ChannelNumber
 *
 * 06 11 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration 
 * 1) migrate assoc.c.
 * 2) add ucTxSeqNum for tracking frames which needs TX-DONE awareness
 * 3) add configuration options for CNM_MEM and RSN modules
 * 4) add data path for management frames
 * 5) eliminate rPacketInfo of MSDU_INFO_T
 *
 * 06 09 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration 
 * add necessary changes to driver data paths.
 *
 * 06 06 2010 kevin.huang
 * [WPD00003832][MT6620 5931] Create driver base 
 * [MT6620 5931] Create driver base
**  \main\maintrunk.MT6620WiFiDriver_Prj\10 2009-12-10 16:44:00 GMT mtk02752
**  code clean
**  \main\maintrunk.MT6620WiFiDriver_Prj\9 2009-12-09 13:59:20 GMT MTK02468
**  Added HIF_RX_HDR parsing macros
**  \main\maintrunk.MT6620WiFiDriver_Prj\8 2009-11-24 19:54:54 GMT mtk02752
**  adopt HIF_RX_HEADER_T in new data path
**  \main\maintrunk.MT6620WiFiDriver_Prj\7 2009-10-29 19:51:19 GMT mtk01084
**  modify FW/ driver interface
**  \main\maintrunk.MT6620WiFiDriver_Prj\6 2009-04-28 10:33:58 GMT mtk01461
**  Add define of HW_APPENED_LEN
**  \main\maintrunk.MT6620WiFiDriver_Prj\5 2009-04-01 10:51:02 GMT mtk01461
**  Rename ENUM_HIF_RX_PKT_TYPE_T
**  \main\maintrunk.MT6620WiFiDriver_Prj\4 2009-03-19 12:05:03 GMT mtk01426
**  Remove __KAL_ATTRIB_PACKED__ and add hifDataTypeCheck()
**  \main\maintrunk.MT6620WiFiDriver_Prj\3 2009-03-17 20:18:52 GMT mtk01426
**  Add comment to HIF_RX_HEADER_T
**  \main\maintrunk.MT6620WiFiDriver_Prj\2 2009-03-10 20:16:23 GMT mtk01426
**  Init for develop
**
*/

#ifndef _HIF_RX_H
#define _HIF_RX_H

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
/*! HIF_RX_HEADER_T */
// DW 0, Byte 1
#define HIF_RX_HDR_PACKET_TYPE_MASK      BITS(0,1)

// DW 1, Byte 0
#define HIF_RX_HDR_HEADER_LEN            BITS(2,7)
#define HIF_RX_HDR_HEADER_LEN_OFFSET     2
#define HIF_RX_HDR_HEADER_OFFSET_MASK    BITS(0,1)

// DW 1, Byte 1
#define HIF_RX_HDR_80211_HEADER_FORMAT   BIT(0)
#define HIF_RX_HDR_DO_REORDER            BIT(1)
#define HIF_RX_HDR_PAL                   BIT(2)
#define HIF_RX_HDR_TCL                   BIT(3)
#define HIF_RX_HDR_NETWORK_IDX_MASK      BITS(4,7)
#define HIF_RX_HDR_NETWORK_IDX_OFFSET    4

// DW 1, Byte 2, 3
#define HIF_RX_HDR_SEQ_NO_MASK           BITS(0,11)
#define HIF_RX_HDR_TID_MASK              BITS(12,14)
#define HIF_RX_HDR_TID_OFFSET            12
#define HIF_RX_HDR_BAR_FRAME             BIT(15)



#define HIF_RX_HDR_FLAG_AMP_WDS             BIT(0)
#define HIF_RX_HDR_FLAG_802_11_FORMAT       BIT(1)
#define HIF_RX_HDR_FLAG_BAR_FRAME           BIT(2)
#define HIF_RX_HDR_FLAG_DO_REORDERING       BIT(3)
#define HIF_RX_HDR_FLAG_CTRL_WARPPER_FRAME  BIT(4)

#define HIF_RX_HW_APPENDED_LEN              4

// For DW 2, Byte 3 - ucHwChannelNum
#define HW_CHNL_NUM_MAX_2G4                 14
#define HW_CHNL_NUM_MAX_4G_5G               (255 - HW_CHNL_NUM_MAX_2G4)

/*******************************************************************************
*                         D A T A   T Y P E S
********************************************************************************
*/

typedef struct _HIF_RX_HEADER_T {
    UINT_16    u2PacketLen;
    UINT_16    u2PacketType;
    UINT_8     ucHerderLenOffset;
    UINT_8     uc80211_Reorder_PAL_TCL;
    UINT_16    u2SeqNoTid;
    UINT_8     ucStaRecIdx;
    UINT_8     ucRcpi;
    UINT_8     ucHwChannelNum;
    UINT_8     ucReserved;
}  HIF_RX_HEADER_T, *P_HIF_RX_HEADER_T;

typedef enum _ENUM_HIF_RX_PKT_TYPE_T {
    HIF_RX_PKT_TYPE_DATA = 0,
    HIF_RX_PKT_TYPE_EVENT,
    HIF_RX_PKT_TYPE_TX_LOOPBACK,
    HIF_RX_PKT_TYPE_MANAGEMENT,
    HIF_RX_PKT_TYPE_NUM
} ENUM_HIF_RX_PKT_TYPE_T, *P_ENUM_HIF_RX_PKT_TYPE_T;

/*******************************************************************************
*                            P U B L I C   D A T A
********************************************************************************
*/

/*******************************************************************************
*                           P R I V A T E   D A T A
********************************************************************************
*/

/*******************************************************************************
*                                 M A C R O S
********************************************************************************
*/
#define HIF_RX_HDR_SIZE        sizeof(HIF_RX_HEADER_T)

#define HIF_RX_HDR_GET_80211_FLAG(_prHifRxHdr) \
    (((((_prHifRxHdr)->uc80211_Reorder_PAL_TCL) & HIF_RX_HDR_80211_HEADER_FORMAT) ? TRUE : FALSE))
#define HIF_RX_HDR_GET_REORDER_FLAG(_prHifRxHdr) \
    (((((_prHifRxHdr)->uc80211_Reorder_PAL_TCL) & HIF_RX_HDR_DO_REORDER) ? TRUE : FALSE))
#define HIF_RX_HDR_GET_PAL_FLAG(_prHifRxHdr) \
    (((((_prHifRxHdr)->uc80211_Reorder_PAL_TCL) & HIF_RX_HDR_PAL) ? TRUE : FALSE))
#define HIF_RX_HDR_GET_TCL_FLAG(_prHifRxHdr) \
    (((((_prHifRxHdr)->uc80211_Reorder_PAL_TCL) & HIF_RX_HDR_TCL) ? TRUE : FALSE))
#define HIF_RX_HDR_GET_NETWORK_IDX(_prHifRxHdr) \
    ((((_prHifRxHdr)->uc80211_Reorder_PAL_TCL) & HIF_RX_HDR_NETWORK_IDX_MASK)\
    >> HIF_RX_HDR_NETWORK_IDX_OFFSET)


#define HIF_RX_HDR_GET_TID(_prHifRxHdr) \
    ((((_prHifRxHdr)->u2SeqNoTid) & HIF_RX_HDR_TID_MASK)\
    >> HIF_RX_HDR_TID_OFFSET)
#define HIF_RX_HDR_GET_SN(_prHifRxHdr) \
    (((_prHifRxHdr)->u2SeqNoTid) & HIF_RX_HDR_SEQ_NO_MASK)
#define HIF_RX_HDR_GET_BAR_FLAG(_prHifRxHdr) \
    (((((_prHifRxHdr)->u2SeqNoTid) & HIF_RX_HDR_BAR_FRAME)? TRUE: FALSE))


#define HIF_RX_HDR_GET_CHNL_NUM(_prHifRxHdr) \
    ( ( ((_prHifRxHdr)->ucHwChannelNum) > HW_CHNL_NUM_MAX_4G_5G ) ? \
      ( ((_prHifRxHdr)->ucHwChannelNum) - HW_CHNL_NUM_MAX_4G_5G ) : \
      ((_prHifRxHdr)->ucHwChannelNum) )

/* To do: support more bands other than 2.4G and 5G */
#define HIF_RX_HDR_GET_RF_BAND(_prHifRxHdr) \
    ( ( ((_prHifRxHdr)->ucHwChannelNum) <= HW_CHNL_NUM_MAX_2G4 ) ? \
      BAND_2G4 : BAND_5G)

/*******************************************************************************
*                  F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/
__KAL_INLINE__ VOID
hifDataTypeCheck (
    VOID
    );

/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/

/* Kevin: we don't have to call following function to inspect the data structure.
 * It will check automatically while at compile time.
 * We'll need this for porting driver to different RTOS.
 */
__KAL_INLINE__ VOID
hifDataTypeCheck (
    VOID
    )
{
    DATA_STRUC_INSPECTING_ASSERT(sizeof(HIF_RX_HEADER_T) == 12);

    return;
}

#endif
