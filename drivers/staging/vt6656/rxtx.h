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

#include "ttype.h"
#include "device.h"
#include "wcmd.h"

/*---------------------  Export Definitions -------------------------*/

/*---------------------  Export Classes  ----------------------------*/

//
// RTS buffer header
//
typedef struct tagSRTSDataF {
    WORD    wFrameControl;
    WORD    wDurationID;
    BYTE    abyRA[ETH_ALEN];
    BYTE    abyTA[ETH_ALEN];
} SRTSDataF, *PSRTSDataF;

//
// CTS buffer header
//
typedef struct tagSCTSDataF {
    WORD    wFrameControl;
    WORD    wDurationID;
    BYTE    abyRA[ETH_ALEN];
    WORD    wReserved;
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
    WORD            wRTSTxRrvTime_ba;
    WORD            wRTSTxRrvTime_aa;
    WORD            wRTSTxRrvTime_bb;
    WORD            wReserved2;
    WORD            wTxRrvTime_b;
    WORD            wTxRrvTime_a;

    //RTS
    BYTE            byRTSSignalField_b;
    BYTE            byRTSServiceField_b;
    WORD            wRTSTransmitLength_b;
    BYTE            byRTSSignalField_a;
    BYTE            byRTSServiceField_a;
    WORD            wRTSTransmitLength_a;
    WORD            wRTSDuration_ba;
    WORD            wRTSDuration_aa;
    WORD            wRTSDuration_bb;
    WORD            wReserved3;
    SRTSDataF       sRTS;

    //Data
    BYTE            bySignalField_b;
    BYTE            byServiceField_b;
    WORD            wTransmitLength_b;
    BYTE            bySignalField_a;
    BYTE            byServiceField_a;
    WORD            wTransmitLength_a;
    WORD            wDuration_b;
    WORD            wDuration_a;
    WORD            wTimeStampOff_b;
    WORD            wTimeStampOff_a;

} TX_NAF_G_RTS, *PTX_NAF_G_RTS;

typedef struct tagSTX_NAF_G_RTS_MIC
{
    //RsvTime
    WORD            wRTSTxRrvTime_ba;
    WORD            wRTSTxRrvTime_aa;
    WORD            wRTSTxRrvTime_bb;
    WORD            wReserved2;
    WORD            wTxRrvTime_b;
    WORD            wTxRrvTime_a;

    SMICHDR         sMICHDR;

    //RTS
    BYTE            byRTSSignalField_b;
    BYTE            byRTSServiceField_b;
    WORD            wRTSTransmitLength_b;
    BYTE            byRTSSignalField_a;
    BYTE            byRTSServiceField_a;
    WORD            wRTSTransmitLength_a;
    WORD            wRTSDuration_ba;
    WORD            wRTSDuration_aa;
    WORD            wRTSDuration_bb;
    WORD            wReserved3;
    SRTSDataF       sRTS;

    //Data
    BYTE            bySignalField_b;
    BYTE            byServiceField_b;
    WORD            wTransmitLength_b;
    BYTE            bySignalField_a;
    BYTE            byServiceField_a;
    WORD            wTransmitLength_a;
    WORD            wDuration_b;
    WORD            wDuration_a;
    WORD            wTimeStampOff_b;
    WORD            wTimeStampOff_a;

} TX_NAF_G_RTS_MIC, *PTX_NAF_G_RTS_MIC;

typedef struct tagSTX_NAF_G_CTS
{
    //RsvTime
    WORD            wCTSTxRrvTime_ba;
    WORD            wReserved2;
    WORD            wTxRrvTime_b;
    WORD            wTxRrvTime_a;

    //CTS
    BYTE            byCTSSignalField_b;
    BYTE            byCTSServiceField_b;
    WORD            wCTSTransmitLength_b;
    WORD            wCTSDuration_ba;
    WORD            wReserved3;
    SCTSDataF       sCTS;

    //Data
    BYTE            bySignalField_b;
    BYTE            byServiceField_b;
    WORD            wTransmitLength_b;
    BYTE            bySignalField_a;
    BYTE            byServiceField_a;
    WORD            wTransmitLength_a;
    WORD            wDuration_b;
    WORD            wDuration_a;
    WORD            wTimeStampOff_b;
    WORD            wTimeStampOff_a;

} TX_NAF_G_CTS, *PTX_NAF_G_CTS;


typedef struct tagSTX_NAF_G_CTS_MIC
{
    //RsvTime
    WORD            wCTSTxRrvTime_ba;
    WORD            wReserved2;
    WORD            wTxRrvTime_b;
    WORD            wTxRrvTime_a;


    SMICHDR         sMICHDR;

    //CTS
    BYTE            byCTSSignalField_b;
    BYTE            byCTSServiceField_b;
    WORD            wCTSTransmitLength_b;
    WORD            wCTSDuration_ba;
    WORD            wReserved3;
    SCTSDataF       sCTS;

    //Data
    BYTE            bySignalField_b;
    BYTE            byServiceField_b;
    WORD            wTransmitLength_b;
    BYTE            bySignalField_a;
    BYTE            byServiceField_a;
    WORD            wTransmitLength_a;
    WORD            wDuration_b;
    WORD            wDuration_a;
    WORD            wTimeStampOff_b;
    WORD            wTimeStampOff_a;

} TX_NAF_G_CTS_MIC, *PTX_NAF_G_CTS_MIC;


typedef struct tagSTX_NAF_G_BEACON
{
    WORD            wFIFOCtl;
    WORD            wTimeStamp;

    //CTS
    BYTE            byCTSSignalField_b;
    BYTE            byCTSServiceField_b;
    WORD            wCTSTransmitLength_b;
    WORD            wCTSDuration_ba;
    WORD            wReserved1;
    SCTSDataF       sCTS;

    //Data
    BYTE            bySignalField_a;
    BYTE            byServiceField_a;
    WORD            wTransmitLength_a;
    WORD            wDuration_a;
    WORD            wTimeStampOff_a;


} TX_NAF_G_BEACON, *PTX_NAF_G_BEACON;


typedef struct tagSTX_NAF_AB_RTS
{
    //RsvTime
    WORD            wRTSTxRrvTime_ab;
    WORD            wTxRrvTime_ab;

    //RTS
    BYTE            byRTSSignalField_ab;
    BYTE            byRTSServiceField_ab;
    WORD            wRTSTransmitLength_ab;
    WORD            wRTSDuration_ab;
    WORD            wReserved2;
    SRTSDataF       sRTS;

    //Data
    BYTE            bySignalField_ab;
    BYTE            byServiceField_ab;
    WORD            wTransmitLength_ab;
    WORD            wDuration_ab;
    WORD            wTimeStampOff_ab;


} TX_NAF_AB_RTS, *PTX_NAF_AB_RTS;


typedef struct tagSTX_NAF_AB_RTS_MIC
{
    //RsvTime
    WORD            wRTSTxRrvTime_ab;
    WORD            wTxRrvTime_ab;

    SMICHDR         sMICHDR;

    //RTS
    BYTE            byRTSSignalField_ab;
    BYTE            byRTSServiceField_ab;
    WORD            wRTSTransmitLength_ab;
    WORD            wRTSDuration_ab;
    WORD            wReserved2;
    SRTSDataF       sRTS;

    //Data
    BYTE            bySignalField_ab;
    BYTE            byServiceField_ab;
    WORD            wTransmitLength_ab;
    WORD            wDuration_ab;
    WORD            wTimeStampOff_ab;


} TX_NAF_AB_RTS_MIC, *PTX_NAF_AB_RTS_MIC;



typedef struct tagSTX_NAF_AB_CTS
{
    //RsvTime
    WORD            wReserved2;
    WORD            wTxRrvTime_ab;

    //Data
    BYTE            bySignalField_ab;
    BYTE            byServiceField_ab;
    WORD            wTransmitLength_ab;
    WORD            wDuration_ab;
    WORD            wTimeStampOff_ab;

} TX_NAF_AB_CTS, *PTX_NAF_AB_CTS;

typedef struct tagSTX_NAF_AB_CTS_MIC
{
    //RsvTime
    WORD            wReserved2;
    WORD            wTxRrvTime_ab;

    SMICHDR         sMICHDR;

    //Data
    BYTE            bySignalField_ab;
    BYTE            byServiceField_ab;
    WORD            wTransmitLength_ab;
    WORD            wDuration_ab;
    WORD            wTimeStampOff_ab;

} TX_NAF_AB_CTS_MIC, *PTX_NAF_AB_CTS_MIC;


typedef struct tagSTX_NAF_AB_BEACON
{
    WORD            wFIFOCtl;
    WORD            wTimeStamp;

   //Data
    BYTE            bySignalField_ab;
    BYTE            byServiceField_ab;
    WORD            wTransmitLength_ab;
    WORD            wDuration_ab;
    WORD            wTimeStampOff_ab;

} TX_NAF_AB_BEACON, *PTX_NAF_AB_BEACON;

typedef struct tagSTX_AF_G_RTS
{
    //RsvTime
    WORD            wRTSTxRrvTime_ba;
    WORD            wRTSTxRrvTime_aa;
    WORD            wRTSTxRrvTime_bb;
    WORD            wReserved2;
    WORD            wTxRrvTime_b;
    WORD            wTxRrvTime_a;

    //RTS
    BYTE            byRTSSignalField_b;
    BYTE            byRTSServiceField_b;
    WORD            wRTSTransmitLength_b;
    BYTE            byRTSSignalField_a;
    BYTE            byRTSServiceField_a;
    WORD            wRTSTransmitLength_a;
    WORD            wRTSDuration_ba;
    WORD            wRTSDuration_aa;
    WORD            wRTSDuration_bb;
    WORD            wReserved3;
    WORD            wRTSDuration_ba_f0;
    WORD            wRTSDuration_aa_f0;
    WORD            wRTSDuration_ba_f1;
    WORD            wRTSDuration_aa_f1;
    SRTSDataF       sRTS;

    //Data
    BYTE            bySignalField_b;
    BYTE            byServiceField_b;
    WORD            wTransmitLength_b;
    BYTE            bySignalField_a;
    BYTE            byServiceField_a;
    WORD            wTransmitLength_a;
    WORD            wDuration_b;
    WORD            wDuration_a;
    WORD            wDuration_a_f0;
    WORD            wDuration_a_f1;
    WORD            wTimeStampOff_b;
    WORD            wTimeStampOff_a;

} TX_AF_G_RTS, *PTX_AF_G_RTS;


typedef struct tagSTX_AF_G_RTS_MIC
{
    //RsvTime
    WORD            wRTSTxRrvTime_ba;
    WORD            wRTSTxRrvTime_aa;
    WORD            wRTSTxRrvTime_bb;
    WORD            wReserved2;
    WORD            wTxRrvTime_b;
    WORD            wTxRrvTime_a;

    SMICHDR         sMICHDR;

    //RTS
    BYTE            byRTSSignalField_b;
    BYTE            byRTSServiceField_b;
    WORD            wRTSTransmitLength_b;
    BYTE            byRTSSignalField_a;
    BYTE            byRTSServiceField_a;
    WORD            wRTSTransmitLength_a;
    WORD            wRTSDuration_ba;
    WORD            wRTSDuration_aa;
    WORD            wRTSDuration_bb;
    WORD            wReserved3;
    WORD            wRTSDuration_ba_f0;
    WORD            wRTSDuration_aa_f0;
    WORD            wRTSDuration_ba_f1;
    WORD            wRTSDuration_aa_f1;
    SRTSDataF       sRTS;

    //Data
    BYTE            bySignalField_b;
    BYTE            byServiceField_b;
    WORD            wTransmitLength_b;
    BYTE            bySignalField_a;
    BYTE            byServiceField_a;
    WORD            wTransmitLength_a;
    WORD            wDuration_b;
    WORD            wDuration_a;
    WORD            wDuration_a_f0;
    WORD            wDuration_a_f1;
    WORD            wTimeStampOff_b;
    WORD            wTimeStampOff_a;

} TX_AF_G_RTS_MIC, *PTX_AF_G_RTS_MIC;



typedef struct tagSTX_AF_G_CTS
{
    //RsvTime
    WORD            wCTSTxRrvTime_ba;
    WORD            wReserved2;
    WORD            wTxRrvTime_b;
    WORD            wTxRrvTime_a;

    //CTS
    BYTE            byCTSSignalField_b;
    BYTE            byCTSServiceField_b;
    WORD            wCTSTransmitLength_b;
    WORD            wCTSDuration_ba;
    WORD            wReserved3;
    WORD            wCTSDuration_ba_f0;
    WORD            wCTSDuration_ba_f1;
    SCTSDataF       sCTS;

    //Data
    BYTE            bySignalField_b;
    BYTE            byServiceField_b;
    WORD            wTransmitLength_b;
    BYTE            bySignalField_a;
    BYTE            byServiceField_a;
    WORD            wTransmitLength_a;
    WORD            wDuration_b;
    WORD            wDuration_a;
    WORD            wDuration_a_f0;
    WORD            wDuration_a_f1;
    WORD            wTimeStampOff_b;
    WORD            wTimeStampOff_a;

} TX_AF_G_CTS, *PTX_AF_G_CTS;


typedef struct tagSTX_AF_G_CTS_MIC
{
    //RsvTime
    WORD            wCTSTxRrvTime_ba;
    WORD            wReserved2;
    WORD            wTxRrvTime_b;
    WORD            wTxRrvTime_a;


    SMICHDR         sMICHDR;

    //CTS
    BYTE            byCTSSignalField_b;
    BYTE            byCTSServiceField_b;
    WORD            wCTSTransmitLength_b;
    WORD            wCTSDuration_ba;
    WORD            wReserved3;
    WORD            wCTSDuration_ba_f0;
    WORD            wCTSDuration_ba_f1;
    SCTSDataF       sCTS;

    //Data
    BYTE            bySignalField_b;
    BYTE            byServiceField_b;
    WORD            wTransmitLength_b;
    BYTE            bySignalField_a;
    BYTE            byServiceField_a;
    WORD            wTransmitLength_a;
    WORD            wDuration_b;
    WORD            wDuration_a;
    WORD            wDuration_a_f0;
    WORD            wDuration_a_f1;
    WORD            wTimeStampOff_b;
    WORD            wTimeStampOff_a;

} TX_AF_G_CTS_MIC, *PTX_AF_G_CTS_MIC;



typedef struct tagSTX_AF_A_RTS
{
    //RsvTime
    WORD            wRTSTxRrvTime_a;
    WORD            wTxRrvTime_a;

    //RTS
    BYTE            byRTSSignalField_a;
    BYTE            byRTSServiceField_a;
    WORD            wRTSTransmitLength_a;
    WORD            wRTSDuration_a;
    WORD            wReserved2;
    WORD            wRTSDuration_a_f0;
    WORD            wRTSDuration_a_f1;
    SRTSDataF       sRTS;

    //Data
    BYTE            bySignalField_a;
    BYTE            byServiceField_a;
    WORD            wTransmitLength_a;
    WORD            wDuration_a;
    WORD            wTimeStampOff_a;
    WORD            wDuration_a_f0;
    WORD            wDuration_a_f1;

} TX_AF_A_RTS, *PTX_AF_A_RTS;


typedef struct tagSTX_AF_A_RTS_MIC
{
    //RsvTime
    WORD            wRTSTxRrvTime_a;
    WORD            wTxRrvTime_a;

    SMICHDR         sMICHDR;

    //RTS
    BYTE            byRTSSignalField_a;
    BYTE            byRTSServiceField_a;
    WORD            wRTSTransmitLength_a;
    WORD            wRTSDuration_a;
    WORD            wReserved2;
    WORD            wRTSDuration_a_f0;
    WORD            wRTSDuration_a_f1;
    SRTSDataF       sRTS;

    //Data
    BYTE            bySignalField_a;
    BYTE            byServiceField_a;
    WORD            wTransmitLength_a;
    WORD            wDuration_a;
    WORD            wTimeStampOff_a;
    WORD            wDuration_a_f0;
    WORD            wDuration_a_f1;

} TX_AF_A_RTS_MIC, *PTX_AF_A_RTS_MIC;



typedef struct tagSTX_AF_A_CTS
{
    //RsvTime
    WORD            wReserved2;
    WORD            wTxRrvTime_a;

    //Data
    BYTE            bySignalField_a;
    BYTE            byServiceField_a;
    WORD            wTransmitLength_a;
    WORD            wDuration_a;
    WORD            wTimeStampOff_a;
    WORD            wDuration_a_f0;
    WORD            wDuration_a_f1;

} TX_AF_A_CTS, *PTX_AF_A_CTS;


typedef struct tagSTX_AF_A_CTS_MIC
{
    //RsvTime
    WORD            wReserved2;
    WORD            wTxRrvTime_a;

    SMICHDR         sMICHDR;

    //Data
    BYTE            bySignalField_a;
    BYTE            byServiceField_a;
    WORD            wTransmitLength_a;
    WORD            wDuration_a;
    WORD            wTimeStampOff_a;
    WORD            wDuration_a_f0;
    WORD            wDuration_a_f1;

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
    BYTE                            byType;
    BYTE                            byPKTNO;
    WORD                            wTxByteCount;

	u32 adwTxKey[4];
    WORD                            wFIFOCtl;
    WORD                            wTimeStamp;
    WORD                            wFragCtl;
    WORD                            wReserved;


    // Actual message
    TX_BUFFER_CONTAINER             BufferHeader;

} TX_BUFFER, *PTX_BUFFER;


//
// Remote NDIS message format
//
typedef struct tagSBEACON_BUFFER
{
    BYTE                            byType;
    BYTE                            byPKTNO;
    WORD                            wTxByteCount;

    WORD                            wFIFOCtl;
    WORD                            wTimeStamp;

    // Actual message
    TX_BUFFER_CONTAINER             BufferHeader;

} BEACON_BUFFER, *PBEACON_BUFFER;


/*---------------------  Export Variables  --------------------------*/

/*---------------------  Export Functions  --------------------------*/

BOOL
bPacketToWirelessUsb(
      PSDevice         pDevice,
      BYTE             byPktType,
      PBYTE            usbPacketBuf,
      BOOL             bNeedEncrypt,
      unsigned int             cbPayloadSize,
      unsigned int             uDMAIdx,
      PSEthernetHeader psEthHeader,
      PBYTE            pPacket,
      PSKeyItem        pTransmitKey,
      unsigned int             uNodeIndex,
      WORD             wCurrentRate,
     unsigned int             *pcbHeaderLen,
     unsigned int             *pcbTotalLen
    );

void vDMA0_tx_80211(PSDevice  pDevice, struct sk_buff *skb);
int nsDMA_tx_packet(PSDevice pDevice,
		    unsigned int uDMAIdx,
		    struct sk_buff *skb);
CMD_STATUS csMgmt_xmit(PSDevice pDevice, PSTxMgmtPacket pPacket);
CMD_STATUS csBeacon_xmit(PSDevice pDevice, PSTxMgmtPacket pPacket);
BOOL bRelayPacketSend(PSDevice pDevice, PBYTE pbySkbData,
		      unsigned int uDataLen, unsigned int uNodeIndex);

#endif /* __RXTX_H__ */
