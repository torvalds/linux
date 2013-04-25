/*
 *************************************************************************
 * Ralink Tech Inc.
 * 5F., No.36, Taiyuan St., Jhubei City,
 * Hsinchu County 302,
 * Taiwan, R.O.C.
 *
 * (c) Copyright 2002-2010, Ralink Technology, Inc.
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
 *************************************************************************/


#ifndef WPA_CMM_H
#define WPA_CMM_H

#include "rtmp_type.h"
#include "dot11i_wpa.h"

#define CACHE_NOT_FOUND         -1

#define TX_EAPOL_BUFFER			1500

/* Retry timer counter initial value */
#define PEER_MSG1_RETRY_TIMER_CTR           0
#define PEER_MSG3_RETRY_TIMER_CTR           10
#define GROUP_MSG1_RETRY_TIMER_CTR          20

/* WPA mechanism retry timer interval */
#define PEER_MSG1_RETRY_EXEC_INTV	1000	/* 1 sec */
#define PEER_MSG3_RETRY_EXEC_INTV	3000	/* 3 sec */
#define GROUP_KEY_UPDATE_EXEC_INTV	1000	/* 1 sec */
#define PEER_GROUP_KEY_UPDATE_INIV	2000	/* 2 sec */

#define	EAPOL_MSG_INVALID	0
#define	EAPOL_PAIR_MSG_1	1
#define	EAPOL_PAIR_MSG_2	2
#define	EAPOL_PAIR_MSG_3	3
#define	EAPOL_PAIR_MSG_4	4
#define	EAPOL_GROUP_MSG_1	5
#define	EAPOL_GROUP_MSG_2	6

#define ENQUEUE_EAPOL_START_TIMER	200	/* 200 ms */

/* group rekey interval */
#define TIME_REKEY                          0
#define PKT_REKEY                           1
#define DISABLE_REKEY                       2
#define MAX_REKEY                           2

#define MAX_REKEY_INTER                     0x3ffffff

#define EAPOL_START_DISABLE					0
#define EAPOL_START_PSK						1
#define EAPOL_START_1X						2

/* */
/* Common WPA state machine: states, events, total function # */
/* */
#define WPA_PTK                      0
#define MAX_WPA_PTK_STATE            1

#define WPA_MACHINE_BASE             0
#define MT2_EAPPacket                0
#define MT2_EAPOLStart               1
#define MT2_EAPOLLogoff              2
#define MT2_EAPOLKey                 3
#define MT2_EAPOLASFAlert            4
#define MAX_WPA_MSG                  5

#define WPA_FUNC_SIZE                (MAX_WPA_PTK_STATE * MAX_WPA_MSG)

typedef enum _WpaRole {
	WPA_NONE,		/* 0 */
	WPA_Authenticator,	/* 1 */
	WPA_Supplicant,		/* 2 */
	WPA_BOTH,		/* 3: Authenticator and Supplicant */
} WPA_ROLE;

/*for-wpa value domain of pMacEntry->WpaState  802.1i D3   p.114 */
typedef enum _ApWpaState {
	AS_NOTUSE,		/* 0 */
	AS_DISCONNECT,		/* 1 */
	AS_DISCONNECTED,	/* 2 */
	AS_INITIALIZE,		/* 3 */
	AS_AUTHENTICATION,	/* 4 */
	AS_AUTHENTICATION2,	/* 5 */
	AS_INITPMK,		/* 6 */
	AS_INITPSK,		/* 7 */
	AS_PTKSTART,		/* 8 */
	AS_PTKINIT_NEGOTIATING,	/* 9 */
	AS_PTKINITDONE,		/* 10 */
	AS_UPDATEKEYS,		/* 11 */
	AS_INTEGRITY_FAILURE,	/* 12 */
	AS_KEYUPDATE,		/* 13 */
} AP_WPA_STATE;

/* For supplicant state machine states. 802.11i Draft 4.1, p. 97 */
/* We simplified it */
typedef enum _WpaState {
	SS_NOTUSE,		/* 0 */
	SS_START,		/* 1 */
	SS_WAIT_MSG_3,		/* 2 */
	SS_WAIT_GROUP,		/* 3 */
	SS_FINISH,		/* 4 */
	SS_KEYUPDATE,		/* 5 */
} WPA_STATE;

/* for-wpa value domain of pMacEntry->WpaState  802.1i D3   p.114 */
typedef enum _GTKState {
	REKEY_NEGOTIATING,
	REKEY_ESTABLISHED,
	KEYERROR,
} GTK_STATE;

/*  for-wpa  value domain of pMacEntry->WpaState  802.1i D3   p.114 */
typedef enum _WpaGTKState {
	SETKEYS,
	SETKEYS_DONE,
} WPA_GTK_STATE;

/* WPA internal command type */
#define WPA_SM_4WAY_HS_START 	1
#define WPA_SM_DISCONNECT		0xff

/* WPA element IDs */
typedef enum _WPA_VARIABLE_ELEMENT_ID {
	WPA_ELEM_CMD = 1,
	WPA_ELEM_PEER_RSNIE,
	WPA_ELEM_LOCAL_RSNIE,
	WPA_ELEM_PMK,
	WPA_ELEM_RESV
} WPA_VARIABLE_ELEMENT_ID;

#define GROUP_SUITE					0
#define PAIRWISE_SUITE				1
#define AKM_SUITE					2
#define RSN_CAP_INFO				3
#define PMKID_LIST					4
#define G_MGMT_SUITE				5

/* */
/*	The definition of the cipher combination */
/* */
/* 	 bit3	bit2  bit1   bit0 */
/*	+------------+------------+ */
/* 	|	  WPA	 |	   WPA2   | */
/*	+------+-----+------+-----+ */
/*	| TKIP | AES | TKIP | AES | */
/*	|	0  |  1  |   1  |  0  | -> 0x06 */
/*	|	0  |  1  |   1  |  1  | -> 0x07 */
/*	|	1  |  0  |   0  |  1  | -> 0x09 */
/*	|	1  |  0  |   1  |  1  | -> 0x0B */
/*	|	1  |  1  |   0  |  1  | -> 0x0D */
/*	|	1  |  1  |   1  |  0  | -> 0x0E */
/*	|	1  |  1  |   1  |  1  |	-> 0x0F */
/*	+------+-----+------+-----+ */
/* */
typedef enum _WpaMixPairCipher {
	MIX_CIPHER_NOTUSE = 0x00,
	WPA_NONE_WPA2_TKIPAES = 0x03,	/* WPA2-TKIPAES */
	WPA_AES_WPA2_TKIP = 0x06,
	WPA_AES_WPA2_TKIPAES = 0x07,
	WPA_TKIP_WPA2_AES = 0x09,
	WPA_TKIP_WPA2_TKIPAES = 0x0B,
	WPA_TKIPAES_WPA2_NONE = 0x0C,	/* WPA-TKIPAES */
	WPA_TKIPAES_WPA2_AES = 0x0D,
	WPA_TKIPAES_WPA2_TKIP = 0x0E,
	WPA_TKIPAES_WPA2_TKIPAES = 0x0F,
} WPA_MIX_PAIR_CIPHER;

/* The internal command list for ralink dot1x daemon using */
typedef enum _Dot1xInternalCmd {
	DOT1X_DISCONNECT_ENTRY,
	DOT1X_RELOAD_CONFIG,
} DOT1X_INTERNAL_CMD;

/* 802.1x authentication format */
typedef struct _IEEE8021X_FRAME {
	UCHAR Version;		/* 1.0 */
	UCHAR Type;		/* 0 = EAP Packet */
	USHORT Length;
} IEEE8021X_FRAME, *PIEEE8021X_FRAME;

typedef struct GNU_PACKED _RSN_IE_HEADER_STRUCT {
	UCHAR Eid;
	UCHAR Length;
	USHORT Version;		/* Little endian format */
} RSN_IE_HEADER_STRUCT, *PRSN_IE_HEADER_STRUCT;

/* Cipher suite selector types */
typedef struct GNU_PACKED _CIPHER_SUITE_STRUCT {
	UCHAR Oui[3];
	UCHAR Type;
} CIPHER_SUITE_STRUCT, *PCIPHER_SUITE_STRUCT;

/* Authentication and Key Management suite selector */
typedef struct GNU_PACKED _AKM_SUITE_STRUCT {
	UCHAR Oui[3];
	UCHAR Type;
} AKM_SUITE_STRUCT, *PAKM_SUITE_STRUCT;

/* RSN capability */
typedef struct GNU_PACKED _RSN_CAPABILITY {
	USHORT Rsv:10;
	USHORT GTKSAReplayCnt:2;
	USHORT PTKSAReplayCnt:2;
	USHORT NoPairwise:1;
	USHORT PreAuth:1;
} RSN_CAPABILITY, *PRSN_CAPABILITY;

typedef struct _CIPHER_KEY {
	UCHAR Key[16];		/* 128 bits max */
	UCHAR TxMic[8];
	UCHAR RxMic[8];
	UCHAR TxTsc[16];	/* TSC value. Change it from 48bit to 128bit */
	UCHAR RxTsc[16];	/* TSC value. Change it from 48bit to 128bit */
	UCHAR CipherAlg;	/* 0:none, 1:WEP64, 2:WEP128, 3:TKIP, 4:AES, 5:CKIP64, 6:CKIP128 */
	UCHAR KeyLen;		/* Key length for each key, 0: entry is invalid */
#ifdef CONFIG_STA_SUPPORT
	UCHAR BssId[6];
#endif				/* CONFIG_STA_SUPPORT */
	UCHAR Type;		/* Indicate Pairwise/Group when reporting MIC error */
} CIPHER_KEY, *PCIPHER_KEY;

#endif /* WPA_CMM_H */
