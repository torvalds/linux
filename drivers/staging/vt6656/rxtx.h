/*
 * Copyright (c) 1996, 2003 VIA Networking Technologies, Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * File: rxtx.h
 *
 * Purpose:
 *
 * Author: Jerry Chen
 *
 * Date: Jun. 27, 2002
 *
 */

#ifndef __RXTX_H__
#define __RXTX_H__

#include "device.h"
#include "wcmd.h"

//
// RTS buffer header
//
typedef struct tagSRTSDataF {
    u16    wFrameControl;
    u16    wDurationID;
    u8    abyRA[ETH_ALEN];
    u8    abyTA[ETH_ALEN];
} SRTSDataF, *PSRTSDataF;

//
// CTS buffer header
//
typedef struct tagSCTSDataF {
    u16    wFrameControl;
    u16    wDurationID;
    u8    abyRA[ETH_ALEN];
    u16    wReserved;
} SCTSDataF, *PSCTSDataF;

//
// MICHDR data header
//
typedef struct tagSMICHDR {
	u32 adwHDR0[4];
	u32 adwHDR1[4];
	u32 adwHDR2[4];
} SMICHDR, *PSMICHDR;

typedef struct tagSTX_NAF_G_RTS
{
    //RsvTime
    u16            wRTSTxRrvTime_ba;
    u16            wRTSTxRrvTime_aa;
    u16            wRTSTxRrvTime_bb;
    u16            wReserved2;
    u16            wTxRrvTime_b;
    u16            wTxRrvTime_a;

    //RTS
    u8            byRTSSignalField_b;
    u8            byRTSServiceField_b;
    u16            wRTSTransmitLength_b;
    u8            byRTSSignalField_a;
    u8            byRTSServiceField_a;
    u16            wRTSTransmitLength_a;
    u16            wRTSDuration_ba;
    u16            wRTSDuration_aa;
    u16            wRTSDuration_bb;
    u16            wReserved3;
    SRTSDataF       sRTS;

    //Data
    u8            bySignalField_b;
    u8            byServiceField_b;
    u16            wTransmitLength_b;
    u8            bySignalField_a;
    u8            byServiceField_a;
    u16            wTransmitLength_a;
    u16            wDuration_b;
    u16            wDuration_a;
    u16            wTimeStampOff_b;
    u16            wTimeStampOff_a;

} TX_NAF_G_RTS, *PTX_NAF_G_RTS;

typedef struct tagSTX_NAF_G_RTS_MIC
{
    //RsvTime
    u16            wRTSTxRrvTime_ba;
    u16            wRTSTxRrvTime_aa;
    u16            wRTSTxRrvTime_bb;
    u16            wReserved2;
    u16            wTxRrvTime_b;
    u16            wTxRrvTime_a;

    SMICHDR         sMICHDR;

    //RTS
    u8            byRTSSignalField_b;
    u8            byRTSServiceField_b;
    u16            wRTSTransmitLength_b;
    u8            byRTSSignalField_a;
    u8            byRTSServiceField_a;
    u16            wRTSTransmitLength_a;
    u16            wRTSDuration_ba;
    u16            wRTSDuration_aa;
    u16            wRTSDuration_bb;
    u16            wReserved3;
    SRTSDataF       sRTS;

    //Data
    u8            bySignalField_b;
    u8            byServiceField_b;
    u16            wTransmitLength_b;
    u8            bySignalField_a;
    u8            byServiceField_a;
    u16            wTransmitLength_a;
    u16            wDuration_b;
    u16            wDuration_a;
    u16            wTimeStampOff_b;
    u16            wTimeStampOff_a;

} TX_NAF_G_RTS_MIC, *PTX_NAF_G_RTS_MIC;

typedef struct tagSTX_NAF_G_CTS
{
    //RsvTime
    u16            wCTSTxRrvTime_ba;
    u16            wReserved2;
    u16            wTxRrvTime_b;
    u16            wTxRrvTime_a;

    //CTS
    u8            byCTSSignalField_b;
    u8            byCTSServiceField_b;
    u16            wCTSTransmitLength_b;
    u16            wCTSDuration_ba;
    u16            wReserved3;
    SCTSDataF       sCTS;

    //Data
    u8            bySignalField_b;
    u8            byServiceField_b;
    u16            wTransmitLength_b;
    u8            bySignalField_a;
    u8            byServiceField_a;
    u16            wTransmitLength_a;
    u16            wDuration_b;
    u16            wDuration_a;
    u16            wTimeStampOff_b;
    u16            wTimeStampOff_a;

} TX_NAF_G_CTS, *PTX_NAF_G_CTS;

typedef struct tagSTX_NAF_G_CTS_MIC
{
    //RsvTime
    u16            wCTSTxRrvTime_ba;
    u16            wReserved2;
    u16            wTxRrvTime_b;
    u16            wTxRrvTime_a;

    SMICHDR         sMICHDR;

    //CTS
    u8            byCTSSignalField_b;
    u8            byCTSServiceField_b;
    u16            wCTSTransmitLength_b;
    u16            wCTSDuration_ba;
    u16            wReserved3;
    SCTSDataF       sCTS;

    //Data
    u8            bySignalField_b;
    u8            byServiceField_b;
    u16            wTransmitLength_b;
    u8            bySignalField_a;
    u8            byServiceField_a;
    u16            wTransmitLength_a;
    u16            wDuration_b;
    u16            wDuration_a;
    u16            wTimeStampOff_b;
    u16            wTimeStampOff_a;

} TX_NAF_G_CTS_MIC, *PTX_NAF_G_CTS_MIC;

typedef struct tagSTX_NAF_G_BEACON
{
    u16            wFIFOCtl;
    u16            wTimeStamp;

    //CTS
    u8            byCTSSignalField_b;
    u8            byCTSServiceField_b;
    u16            wCTSTransmitLength_b;
    u16            wCTSDuration_ba;
    u16            wReserved1;
    SCTSDataF       sCTS;

    //Data
    u8            bySignalField_a;
    u8            byServiceField_a;
    u16            wTransmitLength_a;
    u16            wDuration_a;
    u16            wTimeStampOff_a;

} TX_NAF_G_BEACON, *PTX_NAF_G_BEACON;

typedef struct tagSTX_NAF_AB_RTS
{
    //RsvTime
    u16            wRTSTxRrvTime_ab;
    u16            wTxRrvTime_ab;

    //RTS
    u8            byRTSSignalField_ab;
    u8            byRTSServiceField_ab;
    u16            wRTSTransmitLength_ab;
    u16            wRTSDuration_ab;
    u16            wReserved2;
    SRTSDataF       sRTS;

    //Data
    u8            bySignalField_ab;
    u8            byServiceField_ab;
    u16            wTransmitLength_ab;
    u16            wDuration_ab;
    u16            wTimeStampOff_ab;

} TX_NAF_AB_RTS, *PTX_NAF_AB_RTS;

typedef struct tagSTX_NAF_AB_RTS_MIC
{
    //RsvTime
    u16            wRTSTxRrvTime_ab;
    u16            wTxRrvTime_ab;

    SMICHDR         sMICHDR;

    //RTS
    u8            byRTSSignalField_ab;
    u8            byRTSServiceField_ab;
    u16            wRTSTransmitLength_ab;
    u16            wRTSDuration_ab;
    u16            wReserved2;
    SRTSDataF       sRTS;

    //Data
    u8            bySignalField_ab;
    u8            byServiceField_ab;
    u16            wTransmitLength_ab;
    u16            wDuration_ab;
    u16            wTimeStampOff_ab;

} TX_NAF_AB_RTS_MIC, *PTX_NAF_AB_RTS_MIC;

typedef struct tagSTX_NAF_AB_CTS
{
    //RsvTime
    u16            wReserved2;
    u16            wTxRrvTime_ab;

    //Data
    u8            bySignalField_ab;
    u8            byServiceField_ab;
    u16            wTransmitLength_ab;
    u16            wDuration_ab;
    u16            wTimeStampOff_ab;

} TX_NAF_AB_CTS, *PTX_NAF_AB_CTS;

typedef struct tagSTX_NAF_AB_CTS_MIC
{
    //RsvTime
    u16            wReserved2;
    u16            wTxRrvTime_ab;

    SMICHDR         sMICHDR;

    //Data
    u8            bySignalField_ab;
    u8            byServiceField_ab;
    u16            wTransmitLength_ab;
    u16            wDuration_ab;
    u16            wTimeStampOff_ab;

} TX_NAF_AB_CTS_MIC, *PTX_NAF_AB_CTS_MIC;

typedef struct tagSTX_NAF_AB_BEACON
{
    u16            wFIFOCtl;
    u16            wTimeStamp;

   //Data
    u8            bySignalField_ab;
    u8            byServiceField_ab;
    u16            wTransmitLength_ab;
    u16            wDuration_ab;
    u16            wTimeStampOff_ab;

} TX_NAF_AB_BEACON, *PTX_NAF_AB_BEACON;

typedef struct tagSTX_AF_G_RTS
{
    //RsvTime
    u16            wRTSTxRrvTime_ba;
    u16            wRTSTxRrvTime_aa;
    u16            wRTSTxRrvTime_bb;
    u16            wReserved2;
    u16            wTxRrvTime_b;
    u16            wTxRrvTime_a;

    //RTS
    u8            byRTSSignalField_b;
    u8            byRTSServiceField_b;
    u16            wRTSTransmitLength_b;
    u8            byRTSSignalField_a;
    u8            byRTSServiceField_a;
    u16            wRTSTransmitLength_a;
    u16            wRTSDuration_ba;
    u16            wRTSDuration_aa;
    u16            wRTSDuration_bb;
    u16            wReserved3;
    u16            wRTSDuration_ba_f0;
    u16            wRTSDuration_aa_f0;
    u16            wRTSDuration_ba_f1;
    u16            wRTSDuration_aa_f1;
    SRTSDataF       sRTS;

    //Data
    u8            bySignalField_b;
    u8            byServiceField_b;
    u16            wTransmitLength_b;
    u8            bySignalField_a;
    u8            byServiceField_a;
    u16            wTransmitLength_a;
    u16            wDuration_b;
    u16            wDuration_a;
    u16            wDuration_a_f0;
    u16            wDuration_a_f1;
    u16            wTimeStampOff_b;
    u16            wTimeStampOff_a;

} TX_AF_G_RTS, *PTX_AF_G_RTS;

typedef struct tagSTX_AF_G_RTS_MIC
{
    //RsvTime
    u16            wRTSTxRrvTime_ba;
    u16            wRTSTxRrvTime_aa;
    u16            wRTSTxRrvTime_bb;
    u16            wReserved2;
    u16            wTxRrvTime_b;
    u16            wTxRrvTime_a;

    SMICHDR         sMICHDR;

    //RTS
    u8            byRTSSignalField_b;
    u8            byRTSServiceField_b;
    u16            wRTSTransmitLength_b;
    u8            byRTSSignalField_a;
    u8            byRTSServiceField_a;
    u16            wRTSTransmitLength_a;
    u16            wRTSDuration_ba;
    u16            wRTSDuration_aa;
    u16            wRTSDuration_bb;
    u16            wReserved3;
    u16            wRTSDuration_ba_f0;
    u16            wRTSDuration_aa_f0;
    u16            wRTSDuration_ba_f1;
    u16            wRTSDuration_aa_f1;
    SRTSDataF       sRTS;

    //Data
    u8            bySignalField_b;
    u8            byServiceField_b;
    u16            wTransmitLength_b;
    u8            bySignalField_a;
    u8            byServiceField_a;
    u16            wTransmitLength_a;
    u16            wDuration_b;
    u16            wDuration_a;
    u16            wDuration_a_f0;
    u16            wDuration_a_f1;
    u16            wTimeStampOff_b;
    u16            wTimeStampOff_a;

} TX_AF_G_RTS_MIC, *PTX_AF_G_RTS_MIC;

typedef struct tagSTX_AF_G_CTS
{
    //RsvTime
    u16            wCTSTxRrvTime_ba;
    u16            wReserved2;
    u16            wTxRrvTime_b;
    u16            wTxRrvTime_a;

    //CTS
    u8            byCTSSignalField_b;
    u8            byCTSServiceField_b;
    u16            wCTSTransmitLength_b;
    u16            wCTSDuration_ba;
    u16            wReserved3;
    u16            wCTSDuration_ba_f0;
    u16            wCTSDuration_ba_f1;
    SCTSDataF       sCTS;

    //Data
    u8            bySignalField_b;
    u8            byServiceField_b;
    u16            wTransmitLength_b;
    u8            bySignalField_a;
    u8            byServiceField_a;
    u16            wTransmitLength_a;
    u16            wDuration_b;
    u16            wDuration_a;
    u16            wDuration_a_f0;
    u16            wDuration_a_f1;
    u16            wTimeStampOff_b;
    u16            wTimeStampOff_a;

} TX_AF_G_CTS, *PTX_AF_G_CTS;

typedef struct tagSTX_AF_G_CTS_MIC
{
    //RsvTime
    u16            wCTSTxRrvTime_ba;
    u16            wReserved2;
    u16            wTxRrvTime_b;
    u16            wTxRrvTime_a;

    SMICHDR         sMICHDR;

    //CTS
    u8            byCTSSignalField_b;
    u8            byCTSServiceField_b;
    u16            wCTSTransmitLength_b;
    u16            wCTSDuration_ba;
    u16            wReserved3;
    u16            wCTSDuration_ba_f0;
    u16            wCTSDuration_ba_f1;
    SCTSDataF       sCTS;

    //Data
    u8            bySignalField_b;
    u8            byServiceField_b;
    u16            wTransmitLength_b;
    u8            bySignalField_a;
    u8            byServiceField_a;
    u16            wTransmitLength_a;
    u16            wDuration_b;
    u16            wDuration_a;
    u16            wDuration_a_f0;
    u16            wDuration_a_f1;
    u16            wTimeStampOff_b;
    u16            wTimeStampOff_a;

} TX_AF_G_CTS_MIC, *PTX_AF_G_CTS_MIC;

typedef struct tagSTX_AF_A_RTS
{
    //RsvTime
    u16            wRTSTxRrvTime_a;
    u16            wTxRrvTime_a;

    //RTS
    u8            byRTSSignalField_a;
    u8            byRTSServiceField_a;
    u16            wRTSTransmitLength_a;
    u16            wRTSDuration_a;
    u16            wReserved2;
    u16            wRTSDuration_a_f0;
    u16            wRTSDuration_a_f1;
    SRTSDataF       sRTS;

    //Data
    u8            bySignalField_a;
    u8            byServiceField_a;
    u16            wTransmitLength_a;
    u16            wDuration_a;
    u16            wTimeStampOff_a;
    u16            wDuration_a_f0;
    u16            wDuration_a_f1;

} TX_AF_A_RTS, *PTX_AF_A_RTS;

typedef struct tagSTX_AF_A_RTS_MIC
{
    //RsvTime
    u16            wRTSTxRrvTime_a;
    u16            wTxRrvTime_a;

    SMICHDR         sMICHDR;

    //RTS
    u8            byRTSSignalField_a;
    u8            byRTSServiceField_a;
    u16            wRTSTransmitLength_a;
    u16            wRTSDuration_a;
    u16            wReserved2;
    u16            wRTSDuration_a_f0;
    u16            wRTSDuration_a_f1;
    SRTSDataF       sRTS;

    //Data
    u8            bySignalField_a;
    u8            byServiceField_a;
    u16            wTransmitLength_a;
    u16            wDuration_a;
    u16            wTimeStampOff_a;
    u16            wDuration_a_f0;
    u16            wDuration_a_f1;

} TX_AF_A_RTS_MIC, *PTX_AF_A_RTS_MIC;

typedef struct tagSTX_AF_A_CTS
{
    //RsvTime
    u16            wReserved2;
    u16            wTxRrvTime_a;

    //Data
    u8            bySignalField_a;
    u8            byServiceField_a;
    u16            wTransmitLength_a;
    u16            wDuration_a;
    u16            wTimeStampOff_a;
    u16            wDuration_a_f0;
    u16            wDuration_a_f1;

} TX_AF_A_CTS, *PTX_AF_A_CTS;

typedef struct tagSTX_AF_A_CTS_MIC
{
    //RsvTime
    u16            wReserved2;
    u16            wTxRrvTime_a;

    SMICHDR         sMICHDR;

    //Data
    u8            bySignalField_a;
    u8            byServiceField_a;
    u16            wTransmitLength_a;
    u16            wDuration_a;
    u16            wTimeStampOff_a;
    u16            wDuration_a_f0;
    u16            wDuration_a_f1;

} TX_AF_A_CTS_MIC, *PTX_AF_A_CTS_MIC;

//
// union with all of the TX Buffer Type
//
typedef union tagUTX_BUFFER_CONTAINER
{
    TX_NAF_G_RTS                    RTS_G;
    TX_NAF_G_RTS_MIC                RTS_G_MIC;
    TX_NAF_G_CTS                    CTS_G;
    TX_NAF_G_CTS_MIC                CTS_G_MIC;
    //TX_NAF_G_BEACON                 Beacon_G;
    TX_NAF_AB_RTS                   RTS_AB;
    TX_NAF_AB_RTS_MIC               RTS_AB_MIC;
    TX_NAF_AB_CTS                   CTS_AB;
    TX_NAF_AB_CTS_MIC               CTS_AB_MIC;
    //TX_NAF_AB_BEACON                Beacon_AB;
    TX_AF_G_RTS                     RTS_G_AutoFB;
    TX_AF_G_RTS_MIC                 RTS_G_AutoFB_MIC;
    TX_AF_G_CTS                     CTS_G_AutoFB;
    TX_AF_G_CTS_MIC                 CTS_G_AutoFB_MIC;
    TX_AF_A_RTS                     RTS_A_AutoFB;
    TX_AF_A_RTS_MIC                 RTS_A_AutoFB_MIC;
    TX_AF_A_CTS                     CTS_A_AutoFB;
    TX_AF_A_CTS_MIC                 CTS_A_AutoFB_MIC;

} TX_BUFFER_CONTAINER, *PTX_BUFFER_CONTAINER;

//
// Remote NDIS message format
//
typedef struct tagSTX_BUFFER
{
    u8                            byType;
    u8                            byPKTNO;
    u16                            wTxByteCount;

	u32 adwTxKey[4];
    u16                            wFIFOCtl;
    u16                            wTimeStamp;
    u16                            wFragCtl;
    u16                            wReserved;

    // Actual message
    TX_BUFFER_CONTAINER             BufferHeader;

} TX_BUFFER, *PTX_BUFFER;

//
// Remote NDIS message format
//
typedef struct tagSBEACON_BUFFER
{
    u8                            byType;
    u8                            byPKTNO;
    u16                            wTxByteCount;

    u16                            wFIFOCtl;
    u16                            wTimeStamp;

    // Actual message
    TX_BUFFER_CONTAINER             BufferHeader;

} BEACON_BUFFER, *PBEACON_BUFFER;

void vDMA0_tx_80211(struct vnt_private *, struct sk_buff *skb);
int nsDMA_tx_packet(struct vnt_private *, u32 uDMAIdx, struct sk_buff *skb);
CMD_STATUS csMgmt_xmit(struct vnt_private *, struct vnt_tx_mgmt *);
CMD_STATUS csBeacon_xmit(struct vnt_private *, struct vnt_tx_mgmt *);
int bRelayPacketSend(struct vnt_private *, u8 *pbySkbData, u32 uDataLen,
	u32 uNodeIndex);

#endif /* __RXTX_H__ */
