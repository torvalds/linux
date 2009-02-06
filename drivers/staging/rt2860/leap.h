/*
 *************************************************************************
 * Ralink Tech Inc.
 * 5F., No.36, Taiyuan St., Jhubei City,
 * Hsinchu County 302,
 * Taiwan, R.O.C.
 *
 * (c) Copyright 2002-2007, Ralink Technology, Inc.
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
 *************************************************************************

	Module Name:
	leap.h

	Abstract:

	Revision History:
	Who			When			What
	--------	----------		----------------------------------------------
	Name		Date			Modification logs
*/
#ifndef __LEAP_H__
#define __LEAP_H__

// Messages for Associate state machine
#define LEAP_MACHINE_BASE                   30

#define LEAP_MSG_REQUEST_IDENTITY           31
#define LEAP_MSG_REQUEST_LEAP               32
#define LEAP_MSG_SUCCESS                    33
#define LEAP_MSG_FAILED                     34
#define LEAP_MSG_RESPONSE_LEAP              35
#define LEAP_MSG_EAPOLKEY                   36
#define LEAP_MSG_UNKNOWN                    37
#define LEAP_MSG                            38
//! assoc state-machine states
#define LEAP_IDLE                           0
#define LEAP_WAIT_IDENTITY_REQUEST          1
#define LEAP_WAIT_CHANLLENGE_REQUEST        2
#define LEAP_WAIT_SUCCESS                   3
#define LEAP_WAIT_CHANLLENGE_RESPONSE       4
#define LEAP_WAIT_EAPOLKEY                  5

#define LEAP_REASON_INVALID_AUTH                    0x01
#define LEAP_REASON_AUTH_TIMEOUT                    0x02
#define LEAP_REASON_CHALLENGE_FROM_AP_FAILED        0x03
#define LEAP_REASON_CHALLENGE_TO_AP_FAILED          0x04

#define CISCO_AuthModeLEAP                          0x80
#define CISCO_AuthModeLEAPNone                      0x00
#define LEAP_AUTH_TIMEOUT                           30000
#define LEAP_CHALLENGE_RESPONSE_LENGTH              24
#define LEAP_CHALLENGE_REQUEST_LENGTH               8

typedef struct _LEAP_EAPOL_HEADER_ {
    UCHAR       Version;
    UCHAR       Type;
    UCHAR       Length[2];
} LEAP_EAPOL_HEADER, *PLEAP_EAPOL_HEADER;

typedef struct _LEAP_EAPOL_PACKET_ {
    UCHAR       Code;
    UCHAR       Identifier;
    UCHAR       Length[2];
    UCHAR       Type;
} LEAP_EAPOL_PACKET, *PLEAP_EAPOL_PACKET;

typedef struct _LEAP_EAP_CONTENTS_ {
    UCHAR       Version;
    UCHAR       Reserved;
    UCHAR       Length;
} LEAP_EAP_CONTENTS, *PLEAP_EAP_CONTENTS;

/*** EAPOL key ***/
typedef struct _EAPOL_KEY_HEADER_ {
    UCHAR       Type;
    UCHAR       Length[2];
    UCHAR       Counter[8];
    UCHAR       IV[16];
    UCHAR       Index;
    UCHAR       Signature[16];
} EAPOL_KEY_HEADER, *PEAPOL_KEY_HEADER;

BOOLEAN LeapMsgTypeSubst(
    IN  UCHAR   EAPType,
    OUT ULONG   *MsgType);

VOID LeapMachinePerformAction(
    IN PRTMP_ADAPTER    pAd,
    IN STATE_MACHINE    *S,
    IN MLME_QUEUE_ELEM  *Elem);

VOID LeapMacHeaderInit(
    IN  PRTMP_ADAPTER       pAd,
    IN  OUT PHEADER_802_11  pHdr80211,
    IN  UCHAR               wep,
    IN  PUCHAR              pAddr3);

VOID LeapStartAction(
    IN PRTMP_ADAPTER    pAd,
    IN MLME_QUEUE_ELEM  *Elem);

VOID LeapIdentityAction(
    IN PRTMP_ADAPTER    pAd,
    IN MLME_QUEUE_ELEM  *Elem);

VOID LeapPeerChallengeAction(
    IN PRTMP_ADAPTER    pAd,
    IN MLME_QUEUE_ELEM  *Elem);

VOID HashPwd(
    IN  PUCHAR  pwd,
    IN  INT     pwdlen,
    OUT PUCHAR  hash);

VOID PeerChallengeResponse(
    IN  PUCHAR  szChallenge,
    IN  PUCHAR  smbPasswd,
    OUT PUCHAR  szResponse);

VOID ParityKey(
    OUT PUCHAR  szOut,
    IN  PUCHAR  szIn);

VOID DesKey(
    OUT ULONG   k[16][2],
    IN  PUCHAR  key,
    IN  INT     decrypt);

VOID Des(
    IN  ULONG   ks[16][2],
    OUT UCHAR   block[8]);

VOID DesEncrypt(
    IN  PUCHAR  szClear,
    IN  PUCHAR  szKey,
    OUT PUCHAR  szOut);

VOID LeapNetworkChallengeAction(
    IN PRTMP_ADAPTER    pAd,
    IN MLME_QUEUE_ELEM  *Elem);

VOID LeapNetworkChallengeResponse(
    IN PRTMP_ADAPTER    pAd,
    IN MLME_QUEUE_ELEM  *Elem);

VOID HashpwdHash(
    IN  PUCHAR  hash,
    IN  PUCHAR  hashhash);

VOID ProcessSessionKey(
    OUT PUCHAR  SessionKey,
    IN  PUCHAR  hash2,
    IN  PUCHAR  ChallengeToRadius,
    IN  PUCHAR  ChallengeResponseFromRadius,
    IN  PUCHAR  ChallengeFromRadius,
    IN  PUCHAR  ChallengeResponseToRadius);

VOID LeapEapolKeyAction(
    IN PRTMP_ADAPTER    pAd,
    IN MLME_QUEUE_ELEM  *Elem);

VOID RogueApTableInit(
    IN ROGUEAP_TABLE    *Tab);

ULONG RogueApTableSearch(
    IN ROGUEAP_TABLE    *Tab,
    IN PUCHAR           pAddr);

VOID RogueApEntrySet(
    IN  PRTMP_ADAPTER   pAd,
    OUT ROGUEAP_ENTRY   *pRogueAp,
    IN PUCHAR           pAddr,
    IN UCHAR            FaileCode);

ULONG RogueApTableSetEntry(
    IN  PRTMP_ADAPTER   pAd,
    OUT ROGUEAP_TABLE  *Tab,
    IN PUCHAR           pAddr,
    IN UCHAR            FaileCode);

VOID RogueApTableDeleteEntry(
    IN OUT ROGUEAP_TABLE *Tab,
    IN PUCHAR          pAddr);

VOID LeapAuthTimeout(
    IN PVOID SystemSpecific1,
    IN PVOID FunctionContext,
    IN PVOID SystemSpecific2,
    IN PVOID SystemSpecific3);

VOID LeapSendRogueAPReport(
    IN  PRTMP_ADAPTER   pAd);

BOOLEAN CCKMAssocRspSanity(
    IN PRTMP_ADAPTER    pAd,
    IN VOID             *Msg,
    IN ULONG            MsgLen);

#endif  // __LEAP_H__
