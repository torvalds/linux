/******************************************************************************
 *
 * Copyright(c) 2007 - 2010 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * Modifications for inclusion into the Linux staging tree are
 * Copyright(c) 2010 Larry Finger. All rights reserved.
 *
 * Contact information:
 * WLAN FAE <wlanfae@realtek.com>
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 ******************************************************************************/
#ifndef __RTL871X_SECURITY_H_
#define __RTL871X_SECURITY_H_

#include "osdep_service.h"
#include "drv_types.h"

#define _NO_PRIVACY_	0x0
#define _WEP40_		0x1
#define _TKIP_		0x2
#define _TKIP_WTMIC_	0x3
#define _AES_		0x4
#define _WEP104_	0x5

#define _WPA_IE_ID_	0xdd
#define _WPA2_IE_ID_	0x30

#ifndef Ndis802_11AuthModeWPA2
#define Ndis802_11AuthModeWPA2 (Ndis802_11AuthModeWPANone + 1)
#endif

#ifndef Ndis802_11AuthModeWPA2PSK
#define Ndis802_11AuthModeWPA2PSK (Ndis802_11AuthModeWPANone + 2)
#endif

union pn48 {
	u64 val;
#if defined(__BIG_ENDIAN)
	struct {
		u8 TSC7;
		u8 TSC6;
		u8 TSC5;
		u8 TSC4;
		u8 TSC3;
		u8 TSC2;
		u8 TSC1;
		u8 TSC0;
	} _byte_;
#else
	struct {
		u8 TSC0;
		u8 TSC1;
		u8 TSC2;
		u8 TSC3;
		u8 TSC4;
		u8 TSC5;
		u8 TSC6;
		u8 TSC7;
	} _byte_;
#endif
};

union Keytype {
	u8 skey[16];
	u32 lkey[4];
};

struct RT_PMKID_LIST {
	u8 bUsed;
	u8 Bssid[6];
	u8 PMKID[16];
	u8 SsidBuf[33];
	u8 *ssid_octet;
	u16 ssid_length;
};

struct security_priv {
	u32 AuthAlgrthm;		/* 802.11 auth, could be open, shared,
					 * 8021x and authswitch */
	u32 PrivacyAlgrthm;		/* This specify the privacy for shared
					 * auth. algorithm. */
	u32 PrivacyKeyIndex;		/* this is only valid for legendary
					 * wep, 0~3 for key id. */
	union Keytype DefKey[4];	/* this is only valid for def. key */
	u32 DefKeylen[4];
	u32 XGrpPrivacy;		/* This specify the privacy algthm.
					 * used for Grp key */
	u32 XGrpKeyid;			/* key id used for Grp Key */
	union Keytype	XGrpKey[2];	/* 802.1x Group Key, for
					 * inx0 and inx1 */
	union Keytype	XGrptxmickey[2];
	union Keytype	XGrprxmickey[2];
	union pn48 Grptxpn;		/* PN48 used for Grp Key xmit. */
	union pn48 Grprxpn;		/* PN48 used for Grp Key recv. */
	u8 wps_hw_pbc_pressed;/*for hw pbc pressed*/
	u8 wps_phase;/*for wps*/
	u8 wps_ie[MAX_WPA_IE_LEN<<2];
	int wps_ie_len;
	u8	binstallGrpkey;
	u8	busetkipkey;
	struct timer_list tkip_timer;
	u8	bcheck_grpkey;
	u8	bgrpkey_handshake;
	s32	sw_encrypt;	/* from registry_priv */
	s32	sw_decrypt;	/* from registry_priv */
	s32	hw_decrypted;	/* if the rx packets is hw_decrypted==false,
				 * it means the hw has not been ready. */
	u32 ndisauthtype;	/* keeps the auth_type & enc_status from upper
				 * layer ioctl(wpa_supplicant or wzc) */
	u32 ndisencryptstatus;
	struct wlan_bssid_ex sec_bss;  /* for joinbss (h2c buffer) usage */
	struct NDIS_802_11_WEP ndiswep;
	u8 assoc_info[600];
	u8 szofcapability[256]; /* for wpa2 usage */
	u8 oidassociation[512]; /* for wpa/wpa2 usage */
	u8 authenticator_ie[256];  /* store ap security information element */
	u8 supplicant_ie[256];  /* store sta security information element */
	/* for tkip countermeasure */
	u32 last_mic_err_time;
	u8	btkip_countermeasure;
	u8	btkip_wait_report;
	u32 btkip_countermeasure_time;
	/*-------------------------------------------------------------------
	 * For WPA2 Pre-Authentication.
	 *------------------------------------------------------------------ */
	struct RT_PMKID_LIST		PMKIDList[NUM_PMKID_CACHE];
	u8				PMKIDIndex;
};

#define GET_ENCRY_ALGO(psecuritypriv, psta, encry_algo, bmcst) \
do { \
	switch (psecuritypriv->AuthAlgrthm) { \
	case 0: \
	case 1: \
	case 3: \
		encry_algo = (u8)psecuritypriv->PrivacyAlgrthm; \
		break; \
	case 2: \
		if (bmcst) \
			encry_algo = (u8)psecuritypriv->XGrpPrivacy; \
		else \
			encry_algo = (u8)psta->XPrivacy; \
		break; \
	} \
} while (0)
#define SET_ICE_IV_LEN(iv_len, icv_len, encrypt)\
do {\
	switch (encrypt) { \
	case _WEP40_: \
	case _WEP104_: \
		iv_len = 4; \
		icv_len = 4; \
		break; \
	case _TKIP_: \
		iv_len = 8; \
		icv_len = 4; \
		break; \
	case _AES_: \
		iv_len = 8; \
		icv_len = 8; \
		break; \
	default: \
		iv_len = 0; \
		icv_len = 0; \
		break; \
	} \
} while (0)
#define GET_TKIP_PN(iv, txpn) \
do {\
	txpn._byte_.TSC0 = iv[2];\
	txpn._byte_.TSC1 = iv[0];\
	txpn._byte_.TSC2 = iv[4];\
	txpn._byte_.TSC3 = iv[5];\
	txpn._byte_.TSC4 = iv[6];\
	txpn._byte_.TSC5 = iv[7];\
} while (0)

#define ROL32(A, n) (((A) << (n)) | (((A)>>(32-(n)))  & ((1UL << (n)) - 1)))
#define ROR32(A, n) ROL32((A), 32 - (n))

struct mic_data {
	u32  K0, K1;         /* Key */
	u32  L, R;           /* Current state */
	u32  M;              /* Message accumulator (single word) */
	u32  nBytesInM;      /* # bytes in M */
};

void seccalctkipmic(
	u8  *key,
	u8  *header,
	u8  *data,
	u32  data_len,
	u8  *Miccode,
	u8   priority);

void r8712_secmicsetkey(struct mic_data *pmicdata, u8 *key);
void r8712_secmicappend(struct mic_data *pmicdata, u8 *src, u32 nBytes);
void r8712_secgetmic(struct mic_data *pmicdata, u8 *dst);
u32 r8712_aes_encrypt(struct _adapter *padapter, u8 *pxmitframe);
u32 r8712_tkip_encrypt(struct _adapter *padapter, u8 *pxmitframe);
void r8712_wep_encrypt(struct _adapter *padapter, u8  *pxmitframe);
u32 r8712_aes_decrypt(struct _adapter *padapter, u8  *precvframe);
u32 r8712_tkip_decrypt(struct _adapter *padapter, u8  *precvframe);
void r8712_wep_decrypt(struct _adapter *padapter, u8  *precvframe);
void r8712_use_tkipkey_handler(void *FunctionContext);

#endif	/*__RTL871X_SECURITY_H_ */

