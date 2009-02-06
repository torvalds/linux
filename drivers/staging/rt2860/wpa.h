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
	wpa.h

	Abstract:

	Revision History:
	Who			When			What
	--------	----------		----------------------------------------------
	Name		Date			Modification logs
*/

#ifndef	__WPA_H__
#define	__WPA_H__

// EAPOL Key descripter frame format related length
#define LEN_KEY_DESC_NONCE			32
#define LEN_KEY_DESC_IV				16
#define LEN_KEY_DESC_RSC			8
#define LEN_KEY_DESC_ID				8
#define LEN_KEY_DESC_REPLAY			8
#define LEN_KEY_DESC_MIC			16

// The length is the EAPoL-Key frame except key data field.
// Please refer to 802.11i-2004 ,Figure 43u in p.78
#define LEN_EAPOL_KEY_MSG			(sizeof(KEY_DESCRIPTER) - MAX_LEN_OF_RSNIE)

// EAP Code Type.
#define EAP_CODE_REQUEST	1
#define EAP_CODE_RESPONSE	2
#define EAP_CODE_SUCCESS    3
#define EAP_CODE_FAILURE    4

// EAPOL frame Protocol Version
#define	EAPOL_VER					1
#define	EAPOL_VER2					2

// EAPOL-KEY Descriptor Type
#define	WPA1_KEY_DESC				0xfe
#define WPA2_KEY_DESC               0x02

// Key Descriptor Version of Key Information
#define	DESC_TYPE_TKIP				1
#define	DESC_TYPE_AES				2
#define DESC_TYPE_MESH				3

#define LEN_MSG1_2WAY               0x7f
#define MAX_LEN_OF_EAP_HS           256

#define LEN_MASTER_KEY				32

// EAPOL EK, MK
#define LEN_EAP_EK					16
#define LEN_EAP_MICK				16
#define LEN_EAP_KEY					((LEN_EAP_EK)+(LEN_EAP_MICK))
// TKIP key related
#define LEN_PMKID					16
#define LEN_TKIP_EK					16
#define LEN_TKIP_RXMICK				8
#define LEN_TKIP_TXMICK				8
#define LEN_AES_EK					16
#define LEN_AES_KEY					LEN_AES_EK
#define LEN_TKIP_KEY				((LEN_TKIP_EK)+(LEN_TKIP_RXMICK)+(LEN_TKIP_TXMICK))
#define TKIP_AP_TXMICK_OFFSET		((LEN_EAP_KEY)+(LEN_TKIP_EK))
#define TKIP_AP_RXMICK_OFFSET		(TKIP_AP_TXMICK_OFFSET+LEN_TKIP_TXMICK)
#define TKIP_GTK_LENGTH				((LEN_TKIP_EK)+(LEN_TKIP_RXMICK)+(LEN_TKIP_TXMICK))
#define LEN_PTK						((LEN_EAP_KEY)+(LEN_TKIP_KEY))

// RSN IE Length definition
#define MAX_LEN_OF_RSNIE         	90
#define MIN_LEN_OF_RSNIE         	8

//EAP Packet Type
#define	EAPPacket		0
#define	EAPOLStart		1
#define	EAPOLLogoff		2
#define	EAPOLKey		3
#define	EAPOLASFAlert	4
#define	EAPTtypeMax		5

#define	EAPOL_MSG_INVALID	0
#define	EAPOL_PAIR_MSG_1	1
#define	EAPOL_PAIR_MSG_2	2
#define	EAPOL_PAIR_MSG_3	3
#define	EAPOL_PAIR_MSG_4	4
#define	EAPOL_GROUP_MSG_1	5
#define	EAPOL_GROUP_MSG_2	6

#define PAIRWISEKEY					1
#define GROUPKEY					0

// Retry timer counter initial value
#define PEER_MSG1_RETRY_TIMER_CTR           0
#define PEER_MSG3_RETRY_TIMER_CTR           10
#define GROUP_MSG1_RETRY_TIMER_CTR          20


#define EAPOL_START_DISABLE					0
#define EAPOL_START_PSK						1
#define EAPOL_START_1X						2

#define MIX_CIPHER_WPA_TKIP_ON(x)       (((x) & 0x08) != 0)
#define MIX_CIPHER_WPA_AES_ON(x)        (((x) & 0x04) != 0)
#define MIX_CIPHER_WPA2_TKIP_ON(x)      (((x) & 0x02) != 0)
#define MIX_CIPHER_WPA2_AES_ON(x)       (((x) & 0x01) != 0)

#define ROUND_UP(__x, __y) \
	(((ULONG)((__x)+((__y)-1))) & ((ULONG)~((__y)-1)))

#define	ADD_ONE_To_64BIT_VAR(_V)		\
{										\
	UCHAR	cnt = LEN_KEY_DESC_REPLAY;	\
	do									\
	{									\
		cnt--;							\
		_V[cnt]++;						\
		if (cnt == 0)					\
			break;						\
	}while (_V[cnt] == 0);				\
}

#define IS_WPA_CAPABILITY(a)       (((a) >= Ndis802_11AuthModeWPA) && ((a) <= Ndis802_11AuthModeWPA1PSKWPA2PSK))

// EAPOL Key Information definition within Key descriptor format
typedef	struct PACKED _KEY_INFO
{
#ifdef RT_BIG_ENDIAN
	UCHAR	KeyAck:1;
    UCHAR	Install:1;
    UCHAR	KeyIndex:2;
    UCHAR	KeyType:1;
    UCHAR	KeyDescVer:3;
    UCHAR	Rsvd:3;
    UCHAR	EKD_DL:1;		// EKD for AP; DL for STA
    UCHAR	Request:1;
    UCHAR	Error:1;
    UCHAR	Secure:1;
    UCHAR	KeyMic:1;
#else
	UCHAR	KeyMic:1;
	UCHAR	Secure:1;
	UCHAR	Error:1;
	UCHAR	Request:1;
	UCHAR	EKD_DL:1;       // EKD for AP; DL for STA
	UCHAR	Rsvd:3;
	UCHAR	KeyDescVer:3;
	UCHAR	KeyType:1;
	UCHAR	KeyIndex:2;
	UCHAR	Install:1;
	UCHAR	KeyAck:1;
#endif
}	KEY_INFO, *PKEY_INFO;

// EAPOL Key descriptor format
typedef	struct PACKED _KEY_DESCRIPTER
{
	UCHAR		Type;
	KEY_INFO	KeyInfo;
	UCHAR		KeyLength[2];
	UCHAR		ReplayCounter[LEN_KEY_DESC_REPLAY];
	UCHAR		KeyNonce[LEN_KEY_DESC_NONCE];
	UCHAR		KeyIv[LEN_KEY_DESC_IV];
	UCHAR		KeyRsc[LEN_KEY_DESC_RSC];
	UCHAR		KeyId[LEN_KEY_DESC_ID];
	UCHAR		KeyMic[LEN_KEY_DESC_MIC];
	UCHAR		KeyDataLen[2];
	UCHAR		KeyData[MAX_LEN_OF_RSNIE];
}	KEY_DESCRIPTER, *PKEY_DESCRIPTER;

typedef	struct PACKED _EAPOL_PACKET
{
	UCHAR	 			ProVer;
	UCHAR	 			ProType;
	UCHAR	 			Body_Len[2];
	KEY_DESCRIPTER		KeyDesc;
}	EAPOL_PACKET, *PEAPOL_PACKET;

//802.11i D10 page 83
typedef struct PACKED _GTK_ENCAP
{
#ifndef RT_BIG_ENDIAN
    UCHAR               Kid:2;
    UCHAR               tx:1;
    UCHAR               rsv:5;
    UCHAR               rsv1;
#else
    UCHAR               rsv:5;
    UCHAR               tx:1;
    UCHAR               Kid:2;
    UCHAR               rsv1;
#endif
    UCHAR               GTK[TKIP_GTK_LENGTH];
}   GTK_ENCAP, *PGTK_ENCAP;

typedef struct PACKED _KDE_ENCAP
{
    UCHAR               Type;
    UCHAR               Len;
    UCHAR               OUI[3];
    UCHAR               DataType;
    GTK_ENCAP      GTKEncap;
}   KDE_ENCAP, *PKDE_ENCAP;

// For WPA1
typedef struct PACKED _RSNIE {
    UCHAR   oui[4];
    USHORT  version;
    UCHAR   mcast[4];
    USHORT  ucount;
    struct PACKED {
        UCHAR oui[4];
    }ucast[1];
} RSNIE, *PRSNIE;

// For WPA2
typedef struct PACKED _RSNIE2 {
    USHORT  version;
    UCHAR   mcast[4];
    USHORT  ucount;
    struct PACKED {
        UCHAR oui[4];
    }ucast[1];
} RSNIE2, *PRSNIE2;

// AKM Suite
typedef struct PACKED _RSNIE_AUTH {
    USHORT acount;
    struct PACKED {
        UCHAR oui[4];
    }auth[1];
} RSNIE_AUTH,*PRSNIE_AUTH;

typedef	union PACKED _RSN_CAPABILITIES	{
	struct	PACKED {
#ifdef RT_BIG_ENDIAN
        USHORT		Rsvd:10;
        USHORT		GTKSA_R_Counter:2;
        USHORT		PTKSA_R_Counter:2;
        USHORT		No_Pairwise:1;
		USHORT		PreAuth:1;
#else
        USHORT		PreAuth:1;
		USHORT		No_Pairwise:1;
		USHORT		PTKSA_R_Counter:2;
		USHORT		GTKSA_R_Counter:2;
		USHORT		Rsvd:10;
#endif
	}	field;
	USHORT			word;
}	RSN_CAPABILITIES, *PRSN_CAPABILITIES;

typedef struct PACKED _EAP_HDR {
    UCHAR   ProVer;
    UCHAR   ProType;
    UCHAR   Body_Len[2];
    UCHAR   code;
    UCHAR   identifier;
    UCHAR   length[2]; // including code and identifier, followed by length-2 octets of data
} EAP_HDR, *PEAP_HDR;

// For supplicant state machine states. 802.11i Draft 4.1, p. 97
// We simplified it
typedef	enum	_WpaState
{
	SS_NOTUSE,				// 0
	SS_START,				// 1
	SS_WAIT_MSG_3,			// 2
	SS_WAIT_GROUP,			// 3
	SS_FINISH,  			// 4
	SS_KEYUPDATE,			// 5
}	WPA_STATE;

//
//	The definition of the cipher combination
//
// 	 bit3	bit2  bit1   bit0
//	+------------+------------+
// 	|	  WPA	 |	   WPA2   |
//	+------+-----+------+-----+
//	| TKIP | AES | TKIP | AES |
//	|	0  |  1  |   1  |  0  | -> 0x06
//	|	0  |  1  |   1  |  1  | -> 0x07
//	|	1  |  0  |   0  |  1  | -> 0x09
//	|	1  |  0  |   1  |  1  | -> 0x0B
//	|	1  |  1  |   0  |  1  | -> 0x0D
//	|	1  |  1  |   1  |  0  | -> 0x0E
//	|	1  |  1  |   1  |  1  |	-> 0x0F
//	+------+-----+------+-----+
//
typedef	enum	_WpaMixPairCipher
{
	MIX_CIPHER_NOTUSE 			= 0x00,
	WPA_NONE_WPA2_TKIPAES		= 0x03,		// WPA2-TKIPAES
	WPA_AES_WPA2_TKIP 			= 0x06,
	WPA_AES_WPA2_TKIPAES		= 0x07,
	WPA_TKIP_WPA2_AES			= 0x09,
	WPA_TKIP_WPA2_TKIPAES		= 0x0B,
	WPA_TKIPAES_WPA2_NONE		= 0x0C,		// WPA-TKIPAES
	WPA_TKIPAES_WPA2_AES		= 0x0D,
	WPA_TKIPAES_WPA2_TKIP		= 0x0E,
	WPA_TKIPAES_WPA2_TKIPAES	= 0x0F,
}	WPA_MIX_PAIR_CIPHER;

typedef struct PACKED _RSN_IE_HEADER_STRUCT	{
	UCHAR		Eid;
	UCHAR		Length;
	USHORT		Version;	// Little endian format
}	RSN_IE_HEADER_STRUCT, *PRSN_IE_HEADER_STRUCT;

// Cipher suite selector types
typedef struct PACKED _CIPHER_SUITE_STRUCT	{
	UCHAR		Oui[3];
	UCHAR		Type;
}	CIPHER_SUITE_STRUCT, *PCIPHER_SUITE_STRUCT;

// Authentication and Key Management suite selector
typedef struct PACKED _AKM_SUITE_STRUCT	{
	UCHAR		Oui[3];
	UCHAR		Type;
}	AKM_SUITE_STRUCT, *PAKM_SUITE_STRUCT;

// RSN capability
typedef struct	PACKED _RSN_CAPABILITY	{
	USHORT		Rsv:10;
	USHORT		GTKSAReplayCnt:2;
	USHORT		PTKSAReplayCnt:2;
	USHORT		NoPairwise:1;
	USHORT		PreAuth:1;
}	RSN_CAPABILITY, *PRSN_CAPABILITY;

#endif
