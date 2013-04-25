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


#ifndef	__WPA_H__
#define	__WPA_H__

#ifndef ROUND_UP
#define ROUND_UP(__x, __y) \
	(((ULONG)((__x)+((__y)-1))) & ((ULONG)~((__y)-1)))
#endif

#define	SET_UINT16_TO_ARRARY(_V, _LEN)		\
{											\
	_V[0] = ((UINT16)_LEN) >> 8;			\
	_V[1] = ((UINT16)_LEN & 0xFF);					\
}

#define	INC_UINT16_TO_ARRARY(_V, _LEN)			\
{												\
	UINT16	var_len;							\
												\
	var_len = (_V[0]<<8) | (_V[1]);				\
	var_len += _LEN;							\
												\
	_V[0] = (var_len & 0xFF00) >> 8;			\
	_V[1] = (var_len & 0xFF);					\
}

#define	CONV_ARRARY_TO_UINT16(_V)	((_V[0]<<8) | (_V[1]))

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

#define INC_TX_TSC(_tsc, _cnt)                          \
{                                                       \
    INT i=0;                                            \
	while (++_tsc[i] == 0x0)                            \
    {                                                   \
        i++;                                            \
		if (i == (_cnt))                                \
			break;                                      \
	}                                                   \
}

#define IS_WPA_CAPABILITY(a)       (((a) >= Ndis802_11AuthModeWPA) && ((a) <= Ndis802_11AuthModeWPA1PSKWPA2PSK))

/* 	
	WFA recommend to restrict the encryption type in 11n-HT mode.
 	So, the WEP and TKIP shall not be allowed to use HT rate. 
 */
#define IS_INVALID_HT_SECURITY(_mode)		\
	(((_mode) == Ndis802_11Encryption1Enabled) || \
	 ((_mode) == Ndis802_11Encryption2Enabled))

#define MIX_CIPHER_WPA_TKIP_ON(x)       (((x) & 0x08) != 0)
#define MIX_CIPHER_WPA_AES_ON(x)        (((x) & 0x04) != 0)
#define MIX_CIPHER_WPA2_TKIP_ON(x)      (((x) & 0x02) != 0)
#define MIX_CIPHER_WPA2_AES_ON(x)       (((x) & 0x01) != 0)

/* Some definition are different between Keneral mode and Daemon mode */
#ifdef WPA_DAEMON_MODE
/* The definition for Daemon mode */
#define WPA_GET_BSS_NUM(_pAd)		(_pAd)->mbss_num

#define WPA_GET_PMK(_pAd, _pEntry, _pmk)					\
{															\
	_pmk = _pAd->MBSS[_pEntry->apidx].PMK;					\
}

#define WPA_GET_GTK(_pAd, _pEntry, _gtk)					\
{															\
	_gtk = _pAd->MBSS[_pEntry->apidx].GTK;					\
}

#define WPA_GET_GROUP_CIPHER(_pAd, _pEntry, _cipher)		\
{															\
	_cipher = (_pAd)->MBSS[_pEntry->apidx].GroupEncrypType;	\
}

#define WPA_GET_DEFAULT_KEY_ID(_pAd, _pEntry, _idx)			\
{															\
	_idx = (_pAd)->MBSS[_pEntry->apidx].DefaultKeyId;		\
}

#define WPA_GET_BMCST_TSC(_pAd, _pEntry, _tsc)				\
{															\
	_tsc = 1;												\
}

#define WPA_BSSID(_pAd, _apidx)		(_pAd)->MBSS[_apidx].wlan_addr

#define WPA_OS_MALLOC(_p, _s)		\
{									\
	_p = os_malloc(_s);			\
}

#define WPA_OS_FREE(_p)		\
{								\
	os_free(_p);				\
}

#define WPA_GET_CURRENT_TIME(_time)		\
{										\
	struct timeval tv;					\
	gettimeofday(&tv, NULL);			\
	*(_time) = tv.tv_sec;					\
}

#else
/* The definition for Driver mode */

#if defined(CONFIG_AP_SUPPORT) && defined(CONFIG_STA_SUPPORT)
#define WPA_GET_BSS_NUM(_pAd)		(((_pAd)->OpMode == OPMODE_AP) ? (_pAd)->ApCfg.BssidNum : 1)
#define WPA_GET_GROUP_CIPHER(_pAd, _pEntry, _cipher)					\
	{																	\
	_cipher = Ndis802_11WEPDisabled;								\
		if ((_pAd)->OpMode == OPMODE_AP)								\
		{																\
		if (IS_ENTRY_APCLI(_pEntry) && 								\
			((_pEntry)->MatchAPCLITabIdx < MAX_APCLI_NUM))			\
			_cipher = (_pAd)->ApCfg.ApCliTab[(_pEntry)->MatchAPCLITabIdx].GroupCipher;	\
			else if ((_pEntry)->apidx < (_pAd)->ApCfg.BssidNum)			\
				_cipher = (_pAd)->ApCfg.MBSSID[_pEntry->apidx].GroupKeyWepStatus;\
		}																\
		else															\
			_cipher = (_pAd)->StaCfg.GroupCipher;						\
	}

#define WPA_BSSID(_pAd, _apidx) 	(((_pAd)->OpMode == OPMODE_AP) ?\
									(_pAd)->ApCfg.MBSSID[_apidx].Bssid :\
									(_pAd)->CommonCfg.Bssid)
#elif defined(CONFIG_AP_SUPPORT)
#define WPA_GET_BSS_NUM(_pAd)		(_pAd)->ApCfg.BssidNum
#define WPA_GET_GROUP_CIPHER(_pAd, _pEntry, _cipher)				\
	{																\
	_cipher = Ndis802_11WEPDisabled;							\
	if (IS_ENTRY_APCLI(_pEntry) && 								\
		((_pEntry)->MatchAPCLITabIdx < MAX_APCLI_NUM))			\
		_cipher = (_pAd)->ApCfg.ApCliTab[(_pEntry)->MatchAPCLITabIdx].GroupCipher;	\
		else if ((_pEntry)->apidx < (_pAd)->ApCfg.BssidNum)			\
			_cipher = (_pAd)->ApCfg.MBSSID[_pEntry->apidx].GroupKeyWepStatus;\
	}

#define WPA_BSSID(_pAd, _apidx) 	(_pAd)->ApCfg.MBSSID[_apidx].Bssid

#elif defined(CONFIG_STA_SUPPORT)
#define WPA_GET_BSS_NUM(_pAd)		1
#define WPA_GET_GROUP_CIPHER(_pAd, _pEntry, _cipher)				\
	{																\
		_cipher = (_pAd)->StaCfg.GroupCipher;						\
	}
#define WPA_BSSID(_pAd, _apidx) 	(_pAd)->CommonCfg.Bssid
#endif /* defined(CONFIG_STA_SUPPORT) */

#define WPA_OS_MALLOC(_p, _s)		\
{									\
	os_alloc_mem(NULL, (PUCHAR *)&_p, _s);		\
}

#define WPA_OS_FREE(_p)		\
{							\
	os_free_mem(NULL, _p);	\
}

#define WPA_GET_CURRENT_TIME(_time)		NdisGetSystemUpTime(_time);

#endif /* End of Driver Mode */


/*========================================
	The prototype is defined in cmm_wpa.c
  ========================================*/
void inc_iv_byte(
	UCHAR *iv,
	UINT len,
	UINT cnt);

BOOLEAN WpaMsgTypeSubst(
	IN UCHAR EAPType,
	OUT INT *MsgType);

VOID PRF(
	IN UCHAR *key,
	IN INT key_len,
	IN UCHAR *prefix,
	IN INT prefix_len,
	IN UCHAR *data,
	IN INT data_len,
	OUT UCHAR *output,
	IN INT len);

int RtmpPasswordHash(
	char *password,
	unsigned char *ssid,
	int ssidlength,
	unsigned char *output);

	VOID KDF(
	IN PUINT8 key,
	IN INT key_len,
	IN PUINT8 label,
	IN INT label_len,
	IN PUINT8 data,
	IN INT data_len,
	OUT PUINT8 output,
	IN USHORT len);

PUINT8 WPA_ExtractSuiteFromRSNIE(
	IN PUINT8 rsnie,
	IN UINT rsnie_len,
	IN UINT8 type,
	OUT UINT8 *count);

VOID WpaShowAllsuite(
	IN PUINT8 rsnie,
	IN UINT rsnie_len);

VOID RTMPInsertRSNIE(
	IN PUCHAR pFrameBuf,
	OUT PULONG pFrameLen,
	IN PUINT8 rsnie_ptr,
	IN UINT8 rsnie_len,
	IN PUINT8 pmkid_ptr,
	IN UINT8 pmkid_len);

/* 
 =====================================	
 	function prototype in cmm_wpa.c
 =====================================	
*/
VOID RTMPToWirelessSta(
	IN PRTMP_ADAPTER pAd,
	IN PMAC_TABLE_ENTRY pEntry,
	IN PUCHAR pHeader802_3,
	IN UINT HdrLen,
	IN PUCHAR pData,
	IN UINT DataLen,
	IN BOOLEAN bClearFrame);

VOID WpaDerivePTK(
	IN PRTMP_ADAPTER pAd,
	IN UCHAR *PMK,
	IN UCHAR *ANonce,
	IN UCHAR *AA,
	IN UCHAR *SNonce,
	IN UCHAR *SA,
	OUT UCHAR *output,
	IN UINT len);

VOID WpaDeriveGTK(
	IN UCHAR *PMK,
	IN UCHAR *GNonce,
	IN UCHAR *AA,
	OUT UCHAR *output,
	IN UINT len);

VOID GenRandom(
	IN PRTMP_ADAPTER pAd,
	IN UCHAR *macAddr,
	OUT UCHAR *random);

BOOLEAN RTMPCheckWPAframe(
	IN PRTMP_ADAPTER pAd,
	IN PMAC_TABLE_ENTRY pEntry,
	IN PUCHAR pData,
	IN ULONG DataByteCount,
	IN UCHAR FromWhichBSSID);

BOOLEAN RTMPParseEapolKeyData(
	IN PRTMP_ADAPTER pAd,
	IN PUCHAR pKeyData,
	IN UCHAR KeyDataLen,
	IN UCHAR GroupKeyIndex,
	IN UCHAR MsgType,
	IN BOOLEAN bWPA2,
	IN MAC_TABLE_ENTRY *pEntry);

VOID WPA_ConstructKdeHdr(
	IN UINT8 data_type,
	IN UINT8 data_len,
	OUT PUCHAR pBuf);

VOID ConstructEapolMsg(
	IN PMAC_TABLE_ENTRY pEntry,
	IN UCHAR GroupKeyWepStatus,
	IN UCHAR MsgType,
	IN UCHAR DefaultKeyIdx,
	IN UCHAR *KeyNonce,
	IN UCHAR *TxRSC,
	IN UCHAR *GTK,
	IN UCHAR *RSNIE,
	IN UCHAR RSNIE_Len,
	OUT PEAPOL_PACKET pMsg);

PCIPHER_KEY RTMPSwCipherKeySelection(
	IN PRTMP_ADAPTER pAd,
	IN PUCHAR pIV,
	IN RX_BLK *pRxBlk,
	IN PMAC_TABLE_ENTRY pEntry);

NDIS_STATUS RTMPSoftDecryptionAction(
	IN PRTMP_ADAPTER pAd,
	IN PUCHAR pHdr,
	IN UCHAR UserPriority,
	IN PCIPHER_KEY pKey,
	INOUT PUCHAR pData,
	INOUT UINT16 *DataByteCnt);

VOID RTMPSoftConstructIVHdr(
	IN UCHAR CipherAlg,
	IN UCHAR key_id,
	IN PUCHAR pTxIv,
	OUT PUCHAR pHdrIv,
	OUT UINT8 *hdr_iv_len);

VOID RTMPSoftEncryptionAction(
	IN PRTMP_ADAPTER pAd,
	IN UCHAR CipherAlg,
	IN PUCHAR pHdr,
	IN PUCHAR pSrcBufData,
	IN UINT32 SrcBufLen,
	IN UCHAR KeyIdx,
	IN PCIPHER_KEY pKey,
	OUT UINT8 *ext_len);

VOID RTMPMakeRSNIE(
	IN PRTMP_ADAPTER pAd,
	IN UINT AuthMode,
	IN UINT WepStatus,
	IN UCHAR apidx);

VOID WPAInstallPairwiseKey(
	PRTMP_ADAPTER pAd,
	UINT8 BssIdx,
	PMAC_TABLE_ENTRY pEntry,
	BOOLEAN bAE);

VOID WPAInstallSharedKey(
	PRTMP_ADAPTER pAd,
	UINT8 GroupCipher,
	UINT8 BssIdx,
	UINT8 KeyIdx,
	UINT8 Wcid,
	BOOLEAN bAE,
	PUINT8 pGtk,
	UINT8 GtkLen);

VOID RTMPSetWcidSecurityInfo(
	PRTMP_ADAPTER pAd,
	UINT8 BssIdx,
	UINT8 KeyIdx,
	UINT8 CipherAlg,
	UINT8 Wcid,
	UINT8 KeyTabFlag);

VOID CalculateMIC(
	IN UCHAR KeyDescVer,
	IN UCHAR *PTK,
	OUT PEAPOL_PACKET pMsg);

PSTRING GetEapolMsgType(
	CHAR msg);

#ifdef CONFIG_STA_SUPPORT
#endif /* CONFIG_STA_SUPPORT */

/* 
 =====================================	
 	function prototype in cmm_wep.c
 =====================================	
*/
UINT RTMP_CALC_FCS32(
	IN UINT Fcs,
	IN PUCHAR Cp,
	IN INT Len);

VOID RTMPConstructWEPIVHdr(
	IN UINT8 key_idx,
	IN UCHAR *pn,
	OUT UCHAR *iv_hdr);

BOOLEAN RTMPSoftEncryptWEP(
	IN PRTMP_ADAPTER pAd,
	IN PUCHAR pIvHdr,
	IN PCIPHER_KEY pKey,
	INOUT PUCHAR pData,
	IN ULONG DataByteCnt);

BOOLEAN RTMPSoftDecryptWEP(
	IN PRTMP_ADAPTER pAd,
	IN PCIPHER_KEY pKey,
	INOUT PUCHAR pData,
	INOUT UINT16 *DataByteCnt);

/* 
 =====================================	
 	function prototype in cmm_tkip.c
 =====================================	
*/
BOOLEAN RTMPSoftDecryptTKIP(
	IN PRTMP_ADAPTER pAd,
	IN PUCHAR pHdr,
	IN UCHAR UserPriority,
	IN PCIPHER_KEY pKey,
	INOUT PUCHAR pData,
	IN UINT16 *DataByteCnt);

VOID TKIP_GTK_KEY_WRAP(
	IN UCHAR *key,
	IN UCHAR *iv,
	IN UCHAR *input_text,
	IN UINT32 input_len,
	OUT UCHAR *output_text);

VOID TKIP_GTK_KEY_UNWRAP(
	IN UCHAR *key,
	IN UCHAR *iv,
	IN UCHAR *input_text,
	IN UINT32 input_len,
	OUT UCHAR *output_text);

/* 
 =====================================	
 	function prototype in cmm_aes.c
 =====================================	
*/
BOOLEAN RTMPSoftDecryptAES(
	IN PRTMP_ADAPTER pAd,
	IN PUCHAR pData,
	IN ULONG DataByteCnt,
	IN PCIPHER_KEY pWpaKey);

VOID RTMPConstructCCMPHdr(
	IN UINT8 key_idx,
	IN UCHAR *pn,
	OUT UCHAR *ccmp_hdr);

BOOLEAN RTMPSoftEncryptCCMP(
	IN PRTMP_ADAPTER pAd,
	IN PUCHAR pHdr,
	IN PUCHAR pIV,
	IN PUCHAR pKey,
	INOUT PUCHAR pData,
	IN UINT32 DataLen);

BOOLEAN RTMPSoftDecryptCCMP(
	IN PRTMP_ADAPTER pAd,
	IN PUCHAR pHdr,
	IN PCIPHER_KEY pKey,
	INOUT PUCHAR pData,
	INOUT UINT16 *DataLen);

VOID CCMP_test_vector(
	IN PRTMP_ADAPTER pAd,
	IN INT input);

#endif
