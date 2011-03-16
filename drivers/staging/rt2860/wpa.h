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
	Name			Date			Modification logs
	Justin P. Mattock	11/07/2010		Fix a typo
*/

#ifndef	__WPA_H__
#define	__WPA_H__

/* EAPOL Key descriptor frame format related length */
#define LEN_KEY_DESC_NONCE			32
#define LEN_KEY_DESC_IV				16
#define LEN_KEY_DESC_RSC			8
#define LEN_KEY_DESC_ID				8
#define LEN_KEY_DESC_REPLAY			8
#define LEN_KEY_DESC_MIC			16

/* The length is the EAPoL-Key frame except key data field. */
/* Please refer to 802.11i-2004 ,Figure 43u in p.78 */
#define LEN_EAPOL_KEY_MSG			(sizeof(struct rt_key_descripter) - MAX_LEN_OF_RSNIE)

/* EAP Code Type. */
#define EAP_CODE_REQUEST	1
#define EAP_CODE_RESPONSE	2
#define EAP_CODE_SUCCESS    3
#define EAP_CODE_FAILURE    4

/* EAPOL frame Protocol Version */
#define	EAPOL_VER					1
#define	EAPOL_VER2					2

/* EAPOL-KEY Descriptor Type */
#define	WPA1_KEY_DESC				0xfe
#define WPA2_KEY_DESC               0x02

/* Key Descriptor Version of Key Information */
#define	DESC_TYPE_TKIP				1
#define	DESC_TYPE_AES				2

#define LEN_MSG1_2WAY               0x7f
#define MAX_LEN_OF_EAP_HS           256

#define LEN_MASTER_KEY				32

/* EAPOL EK, MK */
#define LEN_EAP_EK					16
#define LEN_EAP_MICK				16
#define LEN_EAP_KEY					((LEN_EAP_EK)+(LEN_EAP_MICK))
/* TKIP key related */
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
#define MIN_LEN_OF_GTK				5
#define LEN_PMK						32
#define LEN_PMK_NAME				16
#define LEN_NONCE					32

/* RSN IE Length definition */
#define MAX_LEN_OF_RSNIE		255
#define MIN_LEN_OF_RSNIE         	8

#define KEY_LIFETIME				3600

/*EAP Packet Type */
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

/* Retry timer counter initial value */
#define PEER_MSG1_RETRY_TIMER_CTR           0
#define PEER_MSG3_RETRY_TIMER_CTR           10
#define GROUP_MSG1_RETRY_TIMER_CTR          20

/*#ifdef CONFIG_AP_SUPPORT */
/* WPA mechanism retry timer interval */
#define PEER_MSG1_RETRY_EXEC_INTV           1000	/* 1 sec */
#define PEER_MSG3_RETRY_EXEC_INTV           3000	/* 3 sec */
#define GROUP_KEY_UPDATE_EXEC_INTV          1000	/* 1 sec */
#define PEER_GROUP_KEY_UPDATE_INIV			2000	/* 2 sec */

#define ENQUEUE_EAPOL_START_TIMER			200	/* 200 ms */

/* group rekey interval */
#define TIME_REKEY                          0
#define PKT_REKEY                           1
#define DISABLE_REKEY                       2
#define MAX_REKEY                           2

#define MAX_REKEY_INTER                     0x3ffffff
/*#endif // CONFIG_AP_SUPPORT // */

#define GROUP_SUITE					0
#define PAIRWISE_SUITE				1
#define AKM_SUITE					2
#define PMKID_LIST					3

#define EAPOL_START_DISABLE					0
#define EAPOL_START_PSK						1
#define EAPOL_START_1X						2

#define MIX_CIPHER_WPA_TKIP_ON(x)       (((x) & 0x08) != 0)
#define MIX_CIPHER_WPA_AES_ON(x)        (((x) & 0x04) != 0)
#define MIX_CIPHER_WPA2_TKIP_ON(x)      (((x) & 0x02) != 0)
#define MIX_CIPHER_WPA2_AES_ON(x)       (((x) & 0x01) != 0)

#ifndef ROUND_UP
#define ROUND_UP(__x, __y) \
	(((unsigned long)((__x)+((__y)-1))) & ((unsigned long)~((__y)-1)))
#endif

#define	SET_u16_TO_ARRARY(_V, _LEN)		\
{											\
	_V[0] = (_LEN & 0xFF00) >> 8;			\
	_V[1] = (_LEN & 0xFF);					\
}

#define	INC_u16_TO_ARRARY(_V, _LEN)			\
{												\
	u16	var_len;							\
												\
	var_len = (_V[0]<<8) | (_V[1]);				\
	var_len += _LEN;							\
												\
	_V[0] = (var_len & 0xFF00) >> 8;			\
	_V[1] = (var_len & 0xFF);					\
}

#define	CONV_ARRARY_TO_u16(_V)	((_V[0]<<8) | (_V[1]))

#define	ADD_ONE_To_64BIT_VAR(_V)		\
{										\
	u8	cnt = LEN_KEY_DESC_REPLAY;	\
	do									\
	{									\
		cnt--;							\
		_V[cnt]++;						\
		if (cnt == 0)					\
			break;						\
	}while (_V[cnt] == 0);				\
}

#define IS_WPA_CAPABILITY(a)       (((a) >= Ndis802_11AuthModeWPA) && ((a) <= Ndis802_11AuthModeWPA1PSKWPA2PSK))

/* EAPOL Key Information definition within Key descriptor format */
struct PACKED rt_key_info {
	u8 KeyMic:1;
	u8 Secure:1;
	u8 Error:1;
	u8 Request:1;
	u8 EKD_DL:1;		/* EKD for AP; DL for STA */
	u8 Rsvd:3;
	u8 KeyDescVer:3;
	u8 KeyType:1;
	u8 KeyIndex:2;
	u8 Install:1;
	u8 KeyAck:1;
};

/* EAPOL Key descriptor format */
struct PACKED rt_key_descripter {
	u8 Type;
	struct rt_key_info KeyInfo;
	u8 KeyLength[2];
	u8 ReplayCounter[LEN_KEY_DESC_REPLAY];
	u8 KeyNonce[LEN_KEY_DESC_NONCE];
	u8 KeyIv[LEN_KEY_DESC_IV];
	u8 KeyRsc[LEN_KEY_DESC_RSC];
	u8 KeyId[LEN_KEY_DESC_ID];
	u8 KeyMic[LEN_KEY_DESC_MIC];
	u8 KeyDataLen[2];
	u8 KeyData[MAX_LEN_OF_RSNIE];
};

struct PACKED rt_eapol_packet {
	u8 ProVer;
	u8 ProType;
	u8 Body_Len[2];
	struct rt_key_descripter KeyDesc;
};

/*802.11i D10 page 83 */
struct PACKED rt_gtk_encap {
	u8 Kid:2;
	u8 tx:1;
	u8 rsv:5;
	u8 rsv1;
	u8 GTK[TKIP_GTK_LENGTH];
};

struct PACKED rt_kde_encap {
	u8 Type;
	u8 Len;
	u8 OUI[3];
	u8 DataType;
	struct rt_gtk_encap GTKEncap;
};

/* For WPA1 */
struct PACKED rt_rsnie {
	u8 oui[4];
	u16 version;
	u8 mcast[4];
	u16 ucount;
	struct PACKED {
		u8 oui[4];
	} ucast[1];
};

/* For WPA2 */
struct PACKED rt_rsnie2 {
	u16 version;
	u8 mcast[4];
	u16 ucount;
	struct PACKED {
		u8 oui[4];
	} ucast[1];
};

/* AKM Suite */
struct PACKED rt_rsnie_auth {
	u16 acount;
	struct PACKED {
		u8 oui[4];
	} auth[1];
};

typedef union PACKED _RSN_CAPABILITIES {
	struct PACKED {
		u16 PreAuth:1;
		u16 No_Pairwise:1;
		u16 PTKSA_R_Counter:2;
		u16 GTKSA_R_Counter:2;
		u16 Rsvd:10;
	} field;
	u16 word;
} RSN_CAPABILITIES, *PRSN_CAPABILITIES;

struct PACKED rt_eap_hdr {
	u8 ProVer;
	u8 ProType;
	u8 Body_Len[2];
	u8 code;
	u8 identifier;
	u8 length[2];	/* including code and identifier, followed by length-2 octets of data */
};

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

/* */
/*      The definition of the cipher combination */
/* */
/*       bit3   bit2  bit1   bit0 */
/*      +------------+------------+ */
/*      |         WPA    |         WPA2   | */
/*      +------+-----+------+-----+ */
/*      | TKIP | AES | TKIP | AES | */
/*      |       0  |  1  |   1  |  0  | -> 0x06 */
/*      |       0  |  1  |   1  |  1  | -> 0x07 */
/*      |       1  |  0  |   0  |  1  | -> 0x09 */
/*      |       1  |  0  |   1  |  1  | -> 0x0B */
/*      |       1  |  1  |   0  |  1  | -> 0x0D */
/*      |       1  |  1  |   1  |  0  | -> 0x0E */
/*      |       1  |  1  |   1  |  1  | -> 0x0F */
/*      +------+-----+------+-----+ */
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

struct PACKED rt_rsn_ie_header {
	u8 Eid;
	u8 Length;
	u16 Version;		/* Little endian format */
};

/* Cipher suite selector types */
struct PACKED rt_cipher_suite_struct {
	u8 Oui[3];
	u8 Type;
};

/* Authentication and Key Management suite selector */
struct PACKED rt_akm_suite {
	u8 Oui[3];
	u8 Type;
};

/* RSN capability */
struct PACKED rt_rsn_capability {
	u16 Rsv:10;
	u16 GTKSAReplayCnt:2;
	u16 PTKSAReplayCnt:2;
	u16 NoPairwise:1;
	u16 PreAuth:1;
};

/*========================================
	The prototype is defined in cmm_wpa.c
  ========================================*/
BOOLEAN WpaMsgTypeSubst(u8 EAPType, int *MsgType);

void PRF(u8 *key, int key_len, u8 *prefix, int prefix_len,
	 u8 *data, int data_len, u8 *output, int len);

int PasswordHash(char *password,
		 unsigned char *ssid, int ssidlength, unsigned char *output);

u8 *GetSuiteFromRSNIE(u8 *rsnie, u32 rsnie_len, u8 type, u8 *count);

void WpaShowAllsuite(u8 *rsnie, u32 rsnie_len);

void RTMPInsertRSNIE(u8 *pFrameBuf,
		     unsigned long *pFrameLen,
		     u8 *rsnie_ptr,
		     u8 rsnie_len,
		     u8 *pmkid_ptr, u8 pmkid_len);

#endif
