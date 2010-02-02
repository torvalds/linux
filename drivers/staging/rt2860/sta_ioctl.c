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
    sta_ioctl.c

    Abstract:
    IOCTL related subroutines

    Revision History:
    Who         When          What
    --------    ----------    ----------------------------------------------
    Rory Chen   01-03-2003    created
	Rory Chen   02-14-2005    modify to support RT61
*/

#include	"rt_config.h"

#ifdef DBG
extern unsigned long RTDebugLevel;
#endif

#define NR_WEP_KEYS 				4
#define WEP_SMALL_KEY_LEN 			(40/8)
#define WEP_LARGE_KEY_LEN 			(104/8)

#define GROUP_KEY_NO                4

extern u8 CipherWpa2Template[];

struct PACKED rt_version_info {
	u8 DriverVersionW;
	u8 DriverVersionX;
	u8 DriverVersionY;
	u8 DriverVersionZ;
	u32 DriverBuildYear;
	u32 DriverBuildMonth;
	u32 DriverBuildDay;
};

static __s32 ralinkrate[] = { 2, 4, 11, 22,	/* CCK */
	12, 18, 24, 36, 48, 72, 96, 108,	/* OFDM */
	13, 26, 39, 52, 78, 104, 117, 130, 26, 52, 78, 104, 156, 208, 234, 260,	/* 20MHz, 800ns GI, MCS: 0 ~ 15 */
	39, 78, 117, 156, 234, 312, 351, 390,	/* 20MHz, 800ns GI, MCS: 16 ~ 23 */
	27, 54, 81, 108, 162, 216, 243, 270, 54, 108, 162, 216, 324, 432, 486, 540,	/* 40MHz, 800ns GI, MCS: 0 ~ 15 */
	81, 162, 243, 324, 486, 648, 729, 810,	/* 40MHz, 800ns GI, MCS: 16 ~ 23 */
	14, 29, 43, 57, 87, 115, 130, 144, 29, 59, 87, 115, 173, 230, 260, 288,	/* 20MHz, 400ns GI, MCS: 0 ~ 15 */
	43, 87, 130, 173, 260, 317, 390, 433,	/* 20MHz, 400ns GI, MCS: 16 ~ 23 */
	30, 60, 90, 120, 180, 240, 270, 300, 60, 120, 180, 240, 360, 480, 540, 600,	/* 40MHz, 400ns GI, MCS: 0 ~ 15 */
	90, 180, 270, 360, 540, 720, 810, 900
};

int Set_SSID_Proc(struct rt_rtmp_adapter *pAdapter, char *arg);

int Set_NetworkType_Proc(struct rt_rtmp_adapter *pAdapter, char *arg);

void RTMPAddKey(struct rt_rtmp_adapter *pAd, struct rt_ndis_802_11_key *pKey)
{
	unsigned long KeyIdx;
	struct rt_mac_table_entry *pEntry;

	DBGPRINT(RT_DEBUG_TRACE, ("RTMPAddKey ------>\n"));

	if (pAd->StaCfg.AuthMode >= Ndis802_11AuthModeWPA) {
		if (pKey->KeyIndex & 0x80000000) {
			if (pAd->StaCfg.AuthMode == Ndis802_11AuthModeWPANone) {
				NdisZeroMemory(pAd->StaCfg.PMK, 32);
				NdisMoveMemory(pAd->StaCfg.PMK,
					       pKey->KeyMaterial,
					       pKey->KeyLength);
				goto end;
			}
			/* Update PTK */
			NdisZeroMemory(&pAd->SharedKey[BSS0][0],
				       sizeof(struct rt_cipher_key));
			pAd->SharedKey[BSS0][0].KeyLen = LEN_TKIP_EK;
			NdisMoveMemory(pAd->SharedKey[BSS0][0].Key,
				       pKey->KeyMaterial, LEN_TKIP_EK);

			if (pAd->StaCfg.PairCipher ==
			    Ndis802_11Encryption2Enabled) {
				NdisMoveMemory(pAd->SharedKey[BSS0][0].RxMic,
					       pKey->KeyMaterial + LEN_TKIP_EK,
					       LEN_TKIP_TXMICK);
				NdisMoveMemory(pAd->SharedKey[BSS0][0].TxMic,
					       pKey->KeyMaterial + LEN_TKIP_EK +
					       LEN_TKIP_TXMICK,
					       LEN_TKIP_RXMICK);
			} else {
				NdisMoveMemory(pAd->SharedKey[BSS0][0].TxMic,
					       pKey->KeyMaterial + LEN_TKIP_EK,
					       LEN_TKIP_TXMICK);
				NdisMoveMemory(pAd->SharedKey[BSS0][0].RxMic,
					       pKey->KeyMaterial + LEN_TKIP_EK +
					       LEN_TKIP_TXMICK,
					       LEN_TKIP_RXMICK);
			}

			/* Decide its ChiperAlg */
			if (pAd->StaCfg.PairCipher ==
			    Ndis802_11Encryption2Enabled)
				pAd->SharedKey[BSS0][0].CipherAlg = CIPHER_TKIP;
			else if (pAd->StaCfg.PairCipher ==
				 Ndis802_11Encryption3Enabled)
				pAd->SharedKey[BSS0][0].CipherAlg = CIPHER_AES;
			else
				pAd->SharedKey[BSS0][0].CipherAlg = CIPHER_NONE;

			/* Update these related information to struct rt_mac_table_entry */
			pEntry = &pAd->MacTab.Content[BSSID_WCID];
			NdisMoveMemory(pEntry->PairwiseKey.Key,
				       pAd->SharedKey[BSS0][0].Key,
				       LEN_TKIP_EK);
			NdisMoveMemory(pEntry->PairwiseKey.RxMic,
				       pAd->SharedKey[BSS0][0].RxMic,
				       LEN_TKIP_RXMICK);
			NdisMoveMemory(pEntry->PairwiseKey.TxMic,
				       pAd->SharedKey[BSS0][0].TxMic,
				       LEN_TKIP_TXMICK);
			pEntry->PairwiseKey.CipherAlg =
			    pAd->SharedKey[BSS0][0].CipherAlg;

			/* Update pairwise key information to ASIC Shared Key Table */
			AsicAddSharedKeyEntry(pAd,
					      BSS0,
					      0,
					      pAd->SharedKey[BSS0][0].CipherAlg,
					      pAd->SharedKey[BSS0][0].Key,
					      pAd->SharedKey[BSS0][0].TxMic,
					      pAd->SharedKey[BSS0][0].RxMic);

			/* Update ASIC WCID attribute table and IVEIV table */
			RTMPAddWcidAttributeEntry(pAd,
						  BSS0,
						  0,
						  pAd->SharedKey[BSS0][0].
						  CipherAlg, pEntry);

			if (pAd->StaCfg.AuthMode >= Ndis802_11AuthModeWPA2) {
				/* set 802.1x port control */
				/*pAd->StaCfg.PortSecured = WPA_802_1X_PORT_SECURED; */
				STA_PORT_SECURED(pAd);

				/* Indicate Connected for GUI */
				pAd->IndicateMediaState =
				    NdisMediaStateConnected;
			}
		} else {
			/* Update GTK */
			pAd->StaCfg.DefaultKeyId = (pKey->KeyIndex & 0xFF);
			NdisZeroMemory(&pAd->
				       SharedKey[BSS0][pAd->StaCfg.
						       DefaultKeyId],
				       sizeof(struct rt_cipher_key));
			pAd->SharedKey[BSS0][pAd->StaCfg.DefaultKeyId].KeyLen =
			    LEN_TKIP_EK;
			NdisMoveMemory(pAd->
				       SharedKey[BSS0][pAd->StaCfg.
						       DefaultKeyId].Key,
				       pKey->KeyMaterial, LEN_TKIP_EK);

			if (pAd->StaCfg.GroupCipher ==
			    Ndis802_11Encryption2Enabled) {
				NdisMoveMemory(pAd->
					       SharedKey[BSS0][pAd->StaCfg.
							       DefaultKeyId].
					       RxMic,
					       pKey->KeyMaterial + LEN_TKIP_EK,
					       LEN_TKIP_TXMICK);
				NdisMoveMemory(pAd->
					       SharedKey[BSS0][pAd->StaCfg.
							       DefaultKeyId].
					       TxMic,
					       pKey->KeyMaterial + LEN_TKIP_EK +
					       LEN_TKIP_TXMICK,
					       LEN_TKIP_RXMICK);
			} else {
				NdisMoveMemory(pAd->
					       SharedKey[BSS0][pAd->StaCfg.
							       DefaultKeyId].
					       TxMic,
					       pKey->KeyMaterial + LEN_TKIP_EK,
					       LEN_TKIP_TXMICK);
				NdisMoveMemory(pAd->
					       SharedKey[BSS0][pAd->StaCfg.
							       DefaultKeyId].
					       RxMic,
					       pKey->KeyMaterial + LEN_TKIP_EK +
					       LEN_TKIP_TXMICK,
					       LEN_TKIP_RXMICK);
			}

			/* Update Shared Key CipherAlg */
			pAd->SharedKey[BSS0][pAd->StaCfg.DefaultKeyId].
			    CipherAlg = CIPHER_NONE;
			if (pAd->StaCfg.GroupCipher ==
			    Ndis802_11Encryption2Enabled)
				pAd->SharedKey[BSS0][pAd->StaCfg.DefaultKeyId].
				    CipherAlg = CIPHER_TKIP;
			else if (pAd->StaCfg.GroupCipher ==
				 Ndis802_11Encryption3Enabled)
				pAd->SharedKey[BSS0][pAd->StaCfg.DefaultKeyId].
				    CipherAlg = CIPHER_AES;

			/* Update group key information to ASIC Shared Key Table */
			AsicAddSharedKeyEntry(pAd,
					      BSS0,
					      pAd->StaCfg.DefaultKeyId,
					      pAd->SharedKey[BSS0][pAd->StaCfg.
								   DefaultKeyId].
					      CipherAlg,
					      pAd->SharedKey[BSS0][pAd->StaCfg.
								   DefaultKeyId].
					      Key,
					      pAd->SharedKey[BSS0][pAd->StaCfg.
								   DefaultKeyId].
					      TxMic,
					      pAd->SharedKey[BSS0][pAd->StaCfg.
								   DefaultKeyId].
					      RxMic);

			/* Update ASIC WCID attribute table and IVEIV table */
			RTMPAddWcidAttributeEntry(pAd,
						  BSS0,
						  pAd->StaCfg.DefaultKeyId,
						  pAd->SharedKey[BSS0][pAd->
								       StaCfg.
								       DefaultKeyId].
						  CipherAlg, NULL);

			/* set 802.1x port control */
			/*pAd->StaCfg.PortSecured = WPA_802_1X_PORT_SECURED; */
			STA_PORT_SECURED(pAd);

			/* Indicate Connected for GUI */
			pAd->IndicateMediaState = NdisMediaStateConnected;
		}
	} else			/* dynamic WEP from wpa_supplicant */
	{
		u8 CipherAlg;
		u8 *Key;

		if (pKey->KeyLength == 32)
			goto end;

		KeyIdx = pKey->KeyIndex & 0x0fffffff;

		if (KeyIdx < 4) {
			/* it is a default shared key, for Pairwise key setting */
			if (pKey->KeyIndex & 0x80000000) {
				pEntry = MacTableLookup(pAd, pKey->BSSID);

				if (pEntry) {
					DBGPRINT(RT_DEBUG_TRACE,
						 ("RTMPAddKey: Set Pair-wise Key\n"));

					/* set key material and key length */
					pEntry->PairwiseKey.KeyLen =
					    (u8)pKey->KeyLength;
					NdisMoveMemory(pEntry->PairwiseKey.Key,
						       &pKey->KeyMaterial,
						       pKey->KeyLength);

					/* set Cipher type */
					if (pKey->KeyLength == 5)
						pEntry->PairwiseKey.CipherAlg =
						    CIPHER_WEP64;
					else
						pEntry->PairwiseKey.CipherAlg =
						    CIPHER_WEP128;

					/* Add Pair-wise key to Asic */
					AsicAddPairwiseKeyEntry(pAd,
								pEntry->Addr,
								(u8)pEntry->
								Aid,
								&pEntry->
								PairwiseKey);

					/* update WCID attribute table and IVEIV table for this entry */
					RTMPAddWcidAttributeEntry(pAd, BSS0, KeyIdx,	/* The value may be not zero */
								  pEntry->
								  PairwiseKey.
								  CipherAlg,
								  pEntry);

				}
			} else {
				/* Default key for tx (shared key) */
				pAd->StaCfg.DefaultKeyId = (u8)KeyIdx;

				/* set key material and key length */
				pAd->SharedKey[BSS0][KeyIdx].KeyLen =
				    (u8)pKey->KeyLength;
				NdisMoveMemory(pAd->SharedKey[BSS0][KeyIdx].Key,
					       &pKey->KeyMaterial,
					       pKey->KeyLength);

				/* Set Ciper type */
				if (pKey->KeyLength == 5)
					pAd->SharedKey[BSS0][KeyIdx].CipherAlg =
					    CIPHER_WEP64;
				else
					pAd->SharedKey[BSS0][KeyIdx].CipherAlg =
					    CIPHER_WEP128;

				CipherAlg =
				    pAd->SharedKey[BSS0][KeyIdx].CipherAlg;
				Key = pAd->SharedKey[BSS0][KeyIdx].Key;

				/* Set Group key material to Asic */
				AsicAddSharedKeyEntry(pAd, BSS0, KeyIdx,
						      CipherAlg, Key, NULL,
						      NULL);

				/* Update WCID attribute table and IVEIV table for this group key table */
				RTMPAddWcidAttributeEntry(pAd, BSS0, KeyIdx,
							  CipherAlg, NULL);

			}
		}
	}
end:
	return;
}

char *rtstrchr(const char *s, int c)
{
	for (; *s != (char)c; ++s)
		if (*s == '\0')
			return NULL;
	return (char *)s;
}

/*
This is required for LinEX2004/kernel2.6.7 to provide iwlist scanning function
*/

int
rt_ioctl_giwname(struct net_device *dev,
		 struct iw_request_info *info, char *name, char *extra)
{
	strncpy(name, "Ralink STA", IFNAMSIZ);
	/* RT2870 2.1.0.0 uses "RT2870 Wireless" */
	/* RT3090 2.1.0.0 uses "RT2860 Wireless" */
	return 0;
}

int rt_ioctl_siwfreq(struct net_device *dev,
		     struct iw_request_info *info,
		     struct iw_freq *freq, char *extra)
{
	struct rt_rtmp_adapter *pAdapter = NULL;
	int chan = -1;

	GET_PAD_FROM_NET_DEV(pAdapter, dev);

	/*check if the interface is down */
	if (!RTMP_TEST_FLAG(pAdapter, fRTMP_ADAPTER_INTERRUPT_IN_USE)) {
		DBGPRINT(RT_DEBUG_TRACE, ("INFO::Network is down!\n"));
		return -ENETDOWN;
	}

	if (freq->e > 1)
		return -EINVAL;

	if ((freq->e == 0) && (freq->m <= 1000))
		chan = freq->m;	/* Setting by channel number */
	else
		MAP_KHZ_TO_CHANNEL_ID((freq->m / 100), chan);	/* Setting by frequency - search the table , like 2.412G, 2.422G, */

	if (ChannelSanity(pAdapter, chan) == TRUE) {
		pAdapter->CommonCfg.Channel = chan;
		DBGPRINT(RT_DEBUG_ERROR,
			 ("==>rt_ioctl_siwfreq::SIOCSIWFREQ[cmd=0x%x] (Channel=%d)\n",
			  SIOCSIWFREQ, pAdapter->CommonCfg.Channel));
	} else
		return -EINVAL;

	return 0;
}

int rt_ioctl_giwfreq(struct net_device *dev,
		     struct iw_request_info *info,
		     struct iw_freq *freq, char *extra)
{
	struct rt_rtmp_adapter *pAdapter = NULL;
	u8 ch;
	unsigned long m = 2412000;

	GET_PAD_FROM_NET_DEV(pAdapter, dev);

	ch = pAdapter->CommonCfg.Channel;

	DBGPRINT(RT_DEBUG_TRACE, ("==>rt_ioctl_giwfreq  %d\n", ch));

	MAP_CHANNEL_ID_TO_KHZ(ch, m);
	freq->m = m * 100;
	freq->e = 1;
	return 0;
}

int rt_ioctl_siwmode(struct net_device *dev,
		     struct iw_request_info *info, __u32 * mode, char *extra)
{
	struct rt_rtmp_adapter *pAdapter = NULL;

	GET_PAD_FROM_NET_DEV(pAdapter, dev);

	/*check if the interface is down */
	if (!RTMP_TEST_FLAG(pAdapter, fRTMP_ADAPTER_INTERRUPT_IN_USE)) {
		DBGPRINT(RT_DEBUG_TRACE, ("INFO::Network is down!\n"));
		return -ENETDOWN;
	}

	switch (*mode) {
	case IW_MODE_ADHOC:
		Set_NetworkType_Proc(pAdapter, "Adhoc");
		break;
	case IW_MODE_INFRA:
		Set_NetworkType_Proc(pAdapter, "Infra");
		break;
	case IW_MODE_MONITOR:
		Set_NetworkType_Proc(pAdapter, "Monitor");
		break;
	default:
		DBGPRINT(RT_DEBUG_TRACE,
			 ("===>rt_ioctl_siwmode::SIOCSIWMODE (unknown %d)\n",
			  *mode));
		return -EINVAL;
	}

	/* Reset Ralink supplicant to not use, it will be set to start when UI set PMK key */
	pAdapter->StaCfg.WpaState = SS_NOTUSE;

	return 0;
}

int rt_ioctl_giwmode(struct net_device *dev,
		     struct iw_request_info *info, __u32 * mode, char *extra)
{
	struct rt_rtmp_adapter *pAdapter = NULL;

	GET_PAD_FROM_NET_DEV(pAdapter, dev);

	if (ADHOC_ON(pAdapter))
		*mode = IW_MODE_ADHOC;
	else if (INFRA_ON(pAdapter))
		*mode = IW_MODE_INFRA;
	else if (MONITOR_ON(pAdapter)) {
		*mode = IW_MODE_MONITOR;
	} else
		*mode = IW_MODE_AUTO;

	DBGPRINT(RT_DEBUG_TRACE, ("==>rt_ioctl_giwmode(mode=%d)\n", *mode));
	return 0;
}

int rt_ioctl_siwsens(struct net_device *dev,
		     struct iw_request_info *info, char *name, char *extra)
{
	struct rt_rtmp_adapter *pAdapter = NULL;

	GET_PAD_FROM_NET_DEV(pAdapter, dev);

	/*check if the interface is down */
	if (!RTMP_TEST_FLAG(pAdapter, fRTMP_ADAPTER_INTERRUPT_IN_USE)) {
		DBGPRINT(RT_DEBUG_TRACE, ("INFO::Network is down!\n"));
		return -ENETDOWN;
	}

	return 0;
}

int rt_ioctl_giwsens(struct net_device *dev,
		     struct iw_request_info *info, char *name, char *extra)
{
	return 0;
}

int rt_ioctl_giwrange(struct net_device *dev,
		      struct iw_request_info *info,
		      struct iw_point *data, char *extra)
{
	struct rt_rtmp_adapter *pAdapter = NULL;
	struct iw_range *range = (struct iw_range *)extra;
	u16 val;
	int i;

	GET_PAD_FROM_NET_DEV(pAdapter, dev);

	DBGPRINT(RT_DEBUG_TRACE, ("===>rt_ioctl_giwrange\n"));
	data->length = sizeof(struct iw_range);
	memset(range, 0, sizeof(struct iw_range));

	range->txpower_capa = IW_TXPOW_DBM;

	if (INFRA_ON(pAdapter) || ADHOC_ON(pAdapter)) {
		range->min_pmp = 1 * 1024;
		range->max_pmp = 65535 * 1024;
		range->min_pmt = 1 * 1024;
		range->max_pmt = 1000 * 1024;
		range->pmp_flags = IW_POWER_PERIOD;
		range->pmt_flags = IW_POWER_TIMEOUT;
		range->pm_capa = IW_POWER_PERIOD | IW_POWER_TIMEOUT |
		    IW_POWER_UNICAST_R | IW_POWER_ALL_R;
	}

	range->we_version_compiled = WIRELESS_EXT;
	range->we_version_source = 14;

	range->retry_capa = IW_RETRY_LIMIT;
	range->retry_flags = IW_RETRY_LIMIT;
	range->min_retry = 0;
	range->max_retry = 255;

	range->num_channels = pAdapter->ChannelListNum;

	val = 0;
	for (i = 1; i <= range->num_channels; i++) {
		u32 m = 2412000;
		range->freq[val].i = pAdapter->ChannelList[i - 1].Channel;
		MAP_CHANNEL_ID_TO_KHZ(pAdapter->ChannelList[i - 1].Channel, m);
		range->freq[val].m = m * 100;	/* OS_HZ */

		range->freq[val].e = 1;
		val++;
		if (val == IW_MAX_FREQUENCIES)
			break;
	}
	range->num_frequency = val;

	range->max_qual.qual = 100;	/* what is correct max? This was not
					 * documented exactly. At least
					 * 69 has been observed. */
	range->max_qual.level = 0;	/* dB */
	range->max_qual.noise = 0;	/* dB */

	/* What would be suitable values for "average/typical" qual? */
	range->avg_qual.qual = 20;
	range->avg_qual.level = -60;
	range->avg_qual.noise = -95;
	range->sensitivity = 3;

	range->max_encoding_tokens = NR_WEP_KEYS;
	range->num_encoding_sizes = 2;
	range->encoding_size[0] = 5;
	range->encoding_size[1] = 13;

	range->min_rts = 0;
	range->max_rts = 2347;
	range->min_frag = 256;
	range->max_frag = 2346;

	/* IW_ENC_CAPA_* bit field */
	range->enc_capa = IW_ENC_CAPA_WPA | IW_ENC_CAPA_WPA2 |
	    IW_ENC_CAPA_CIPHER_TKIP | IW_ENC_CAPA_CIPHER_CCMP;

	return 0;
}

int rt_ioctl_siwap(struct net_device *dev,
		   struct iw_request_info *info,
		   struct sockaddr *ap_addr, char *extra)
{
	struct rt_rtmp_adapter *pAdapter = NULL;
	NDIS_802_11_MAC_ADDRESS Bssid;

	GET_PAD_FROM_NET_DEV(pAdapter, dev);

	/*check if the interface is down */
	if (!RTMP_TEST_FLAG(pAdapter, fRTMP_ADAPTER_INTERRUPT_IN_USE)) {
		DBGPRINT(RT_DEBUG_TRACE, ("INFO::Network is down!\n"));
		return -ENETDOWN;
	}

	if (pAdapter->Mlme.CntlMachine.CurrState != CNTL_IDLE) {
		RTMP_MLME_RESET_STATE_MACHINE(pAdapter);
		DBGPRINT(RT_DEBUG_TRACE,
			 ("MLME busy, reset MLME state machine!\n"));
	}
	/* tell CNTL state machine to call NdisMSetInformationComplete() after completing */
	/* this request, because this request is initiated by NDIS. */
	pAdapter->MlmeAux.CurrReqIsFromNdis = FALSE;
	/* Prevent to connect AP again in STAMlmePeriodicExec */
	pAdapter->MlmeAux.AutoReconnectSsidLen = 32;

	memset(Bssid, 0, MAC_ADDR_LEN);
	memcpy(Bssid, ap_addr->sa_data, MAC_ADDR_LEN);
	MlmeEnqueue(pAdapter,
		    MLME_CNTL_STATE_MACHINE,
		    OID_802_11_BSSID,
		    sizeof(NDIS_802_11_MAC_ADDRESS), (void *) & Bssid);

	DBGPRINT(RT_DEBUG_TRACE,
		 ("IOCTL::SIOCSIWAP %02x:%02x:%02x:%02x:%02x:%02x\n", Bssid[0],
		  Bssid[1], Bssid[2], Bssid[3], Bssid[4], Bssid[5]));

	return 0;
}

int rt_ioctl_giwap(struct net_device *dev,
		   struct iw_request_info *info,
		   struct sockaddr *ap_addr, char *extra)
{
	struct rt_rtmp_adapter *pAdapter = NULL;

	GET_PAD_FROM_NET_DEV(pAdapter, dev);

	if (INFRA_ON(pAdapter) || ADHOC_ON(pAdapter)) {
		ap_addr->sa_family = ARPHRD_ETHER;
		memcpy(ap_addr->sa_data, &pAdapter->CommonCfg.Bssid, ETH_ALEN);
	}
	/* Add for RT2870 */
	else if (pAdapter->StaCfg.WpaSupplicantUP != WPA_SUPPLICANT_DISABLE) {
		ap_addr->sa_family = ARPHRD_ETHER;
		memcpy(ap_addr->sa_data, &pAdapter->MlmeAux.Bssid, ETH_ALEN);
	} else {
		DBGPRINT(RT_DEBUG_TRACE, ("IOCTL::SIOCGIWAP(=EMPTY)\n"));
		return -ENOTCONN;
	}

	return 0;
}

/*
 * Units are in db above the noise floor. That means the
 * rssi values reported in the tx/rx descriptors in the
 * driver are the SNR expressed in db.
 *
 * If you assume that the noise floor is -95, which is an
 * excellent assumption 99.5 % of the time, then you can
 * derive the absolute signal level (i.e. -95 + rssi).
 * There are some other slight factors to take into account
 * depending on whether the rssi measurement is from 11b,
 * 11g, or 11a.   These differences are at most 2db and
 * can be documented.
 *
 * NB: various calculations are based on the orinoco/wavelan
 *     drivers for compatibility
 */
static void set_quality(struct rt_rtmp_adapter *pAdapter,
			struct iw_quality *iq, signed char rssi)
{
	__u8 ChannelQuality;

	/* Normalize Rssi */
	if (rssi >= -50)
		ChannelQuality = 100;
	else if (rssi >= -80)	/* between -50 ~ -80dbm */
		ChannelQuality = (__u8) (24 + ((rssi + 80) * 26) / 10);
	else if (rssi >= -90)	/* between -80 ~ -90dbm */
		ChannelQuality = (__u8) ((rssi + 90) * 26) / 10;
	else
		ChannelQuality = 0;

	iq->qual = (__u8) ChannelQuality;

	iq->level = (__u8) (rssi);
	iq->noise = (pAdapter->BbpWriteLatch[66] > pAdapter->BbpTuning.FalseCcaUpperThreshold) ? ((__u8) pAdapter->BbpTuning.FalseCcaUpperThreshold) : ((__u8) pAdapter->BbpWriteLatch[66]);	/* noise level (dBm) */
	iq->noise += 256 - 143;
	iq->updated = pAdapter->iw_stats.qual.updated;
}

int rt_ioctl_iwaplist(struct net_device *dev,
		      struct iw_request_info *info,
		      struct iw_point *data, char *extra)
{
	struct rt_rtmp_adapter *pAdapter = NULL;

	struct sockaddr addr[IW_MAX_AP];
	struct iw_quality qual[IW_MAX_AP];
	int i;

	GET_PAD_FROM_NET_DEV(pAdapter, dev);

	/*check if the interface is down */
	if (!RTMP_TEST_FLAG(pAdapter, fRTMP_ADAPTER_INTERRUPT_IN_USE)) {
		DBGPRINT(RT_DEBUG_TRACE, ("INFO::Network is down!\n"));
		data->length = 0;
		return 0;
		/*return -ENETDOWN; */
	}

	for (i = 0; i < IW_MAX_AP; i++) {
		if (i >= pAdapter->ScanTab.BssNr)
			break;
		addr[i].sa_family = ARPHRD_ETHER;
		memcpy(addr[i].sa_data, &pAdapter->ScanTab.BssEntry[i].Bssid,
		       MAC_ADDR_LEN);
		set_quality(pAdapter, &qual[i],
			    pAdapter->ScanTab.BssEntry[i].Rssi);
	}
	data->length = i;
	memcpy(extra, &addr, i * sizeof(addr[0]));
	data->flags = 1;	/* signal quality present (sort of) */
	memcpy(extra + i * sizeof(addr[0]), &qual, i * sizeof(qual[i]));

	return 0;
}

int rt_ioctl_siwscan(struct net_device *dev,
		     struct iw_request_info *info,
		     struct iw_point *data, char *extra)
{
	struct rt_rtmp_adapter *pAdapter = NULL;

	unsigned long Now;
	int Status = NDIS_STATUS_SUCCESS;

	GET_PAD_FROM_NET_DEV(pAdapter, dev);

	/*check if the interface is down */
	if (!RTMP_TEST_FLAG(pAdapter, fRTMP_ADAPTER_INTERRUPT_IN_USE)) {
		DBGPRINT(RT_DEBUG_TRACE, ("INFO::Network is down!\n"));
		return -ENETDOWN;
	}

	if (MONITOR_ON(pAdapter)) {
		DBGPRINT(RT_DEBUG_TRACE,
			 ("Driver is in Monitor Mode now!\n"));
		return -EINVAL;
	}

	if (pAdapter->StaCfg.WpaSupplicantUP == WPA_SUPPLICANT_ENABLE) {
		pAdapter->StaCfg.WpaSupplicantScanCount++;
	}

	pAdapter->StaCfg.bScanReqIsFromWebUI = TRUE;
	if (RTMP_TEST_FLAG(pAdapter, fRTMP_ADAPTER_BSS_SCAN_IN_PROGRESS))
		return NDIS_STATUS_SUCCESS;
	do {
		Now = jiffies;

		if ((pAdapter->StaCfg.WpaSupplicantUP == WPA_SUPPLICANT_ENABLE)
		    && (pAdapter->StaCfg.WpaSupplicantScanCount > 3)) {
			DBGPRINT(RT_DEBUG_TRACE,
				 ("WpaSupplicantScanCount > 3\n"));
			Status = NDIS_STATUS_SUCCESS;
			break;
		}

		if ((OPSTATUS_TEST_FLAG
		     (pAdapter, fOP_STATUS_MEDIA_STATE_CONNECTED))
		    && ((pAdapter->StaCfg.AuthMode == Ndis802_11AuthModeWPA)
			|| (pAdapter->StaCfg.AuthMode ==
			    Ndis802_11AuthModeWPAPSK))
		    && (pAdapter->StaCfg.PortSecured ==
			WPA_802_1X_PORT_NOT_SECURED)) {
			DBGPRINT(RT_DEBUG_TRACE,
				 ("Link UP, Port Not Secured! ignore this set::OID_802_11_BSSID_LIST_SCAN\n"));
			Status = NDIS_STATUS_SUCCESS;
			break;
		}

		if (pAdapter->Mlme.CntlMachine.CurrState != CNTL_IDLE) {
			RTMP_MLME_RESET_STATE_MACHINE(pAdapter);
			DBGPRINT(RT_DEBUG_TRACE,
				 ("MLME busy, reset MLME state machine!\n"));
		}
		/* tell CNTL state machine to call NdisMSetInformationComplete() after completing */
		/* this request, because this request is initiated by NDIS. */
		pAdapter->MlmeAux.CurrReqIsFromNdis = FALSE;
		/* Reset allowed scan retries */
		pAdapter->StaCfg.ScanCnt = 0;
		pAdapter->StaCfg.LastScanTime = Now;

		MlmeEnqueue(pAdapter,
			    MLME_CNTL_STATE_MACHINE,
			    OID_802_11_BSSID_LIST_SCAN, 0, NULL);

		Status = NDIS_STATUS_SUCCESS;
		RTMP_MLME_HANDLER(pAdapter);
	} while (0);
	return NDIS_STATUS_SUCCESS;
}

int rt_ioctl_giwscan(struct net_device *dev,
		     struct iw_request_info *info,
		     struct iw_point *data, char *extra)
{
	struct rt_rtmp_adapter *pAdapter = NULL;
	int i = 0;
	char *current_ev = extra, *previous_ev = extra;
	char *end_buf;
	char *current_val;
	char custom[MAX_CUSTOM_LEN] = { 0 };
	struct iw_event iwe;

	GET_PAD_FROM_NET_DEV(pAdapter, dev);

	if (RTMP_TEST_FLAG(pAdapter, fRTMP_ADAPTER_BSS_SCAN_IN_PROGRESS)) {
		/*
		 * Still scanning, indicate the caller should try again.
		 */
		return -EAGAIN;
	}

	if (pAdapter->StaCfg.WpaSupplicantUP == WPA_SUPPLICANT_ENABLE) {
		pAdapter->StaCfg.WpaSupplicantScanCount = 0;
	}

	if (pAdapter->ScanTab.BssNr == 0) {
		data->length = 0;
		return 0;
	}

	if (data->length > 0)
		end_buf = extra + data->length;
	else
		end_buf = extra + IW_SCAN_MAX_DATA;

	for (i = 0; i < pAdapter->ScanTab.BssNr; i++) {
		if (current_ev >= end_buf) {
			return -E2BIG;
		}
		/*MAC address */
		/*================================ */
		memset(&iwe, 0, sizeof(iwe));
		iwe.cmd = SIOCGIWAP;
		iwe.u.ap_addr.sa_family = ARPHRD_ETHER;
		memcpy(iwe.u.ap_addr.sa_data,
		       &pAdapter->ScanTab.BssEntry[i].Bssid, ETH_ALEN);

		previous_ev = current_ev;
		current_ev =
		    iwe_stream_add_event(info, current_ev, end_buf, &iwe,
					 IW_EV_ADDR_LEN);
		if (current_ev == previous_ev)
			return -E2BIG;

		/*
		   Protocol:
		   it will show scanned AP's WirelessMode .
		   it might be
		   802.11a
		   802.11a/n
		   802.11g/n
		   802.11b/g/n
		   802.11g
		   802.11b/g
		 */
		memset(&iwe, 0, sizeof(iwe));
		iwe.cmd = SIOCGIWNAME;

		{
			struct rt_bss_entry *pBssEntry = &pAdapter->ScanTab.BssEntry[i];
			BOOLEAN isGonly = FALSE;
			int rateCnt = 0;

			if (pBssEntry->Channel > 14) {
				if (pBssEntry->HtCapabilityLen != 0)
					strcpy(iwe.u.name, "802.11a/n");
				else
					strcpy(iwe.u.name, "802.11a");
			} else {
				/*
				   if one of non B mode rate is set supported rate . it mean G only.
				 */
				for (rateCnt = 0;
				     rateCnt < pBssEntry->SupRateLen;
				     rateCnt++) {
					/*
					   6Mbps(140) 9Mbps(146) and >=12Mbps(152) are supported rate , it mean G only.
					 */
					if (pBssEntry->SupRate[rateCnt] == 140
					    || pBssEntry->SupRate[rateCnt] ==
					    146
					    || pBssEntry->SupRate[rateCnt] >=
					    152)
						isGonly = TRUE;
				}

				for (rateCnt = 0;
				     rateCnt < pBssEntry->ExtRateLen;
				     rateCnt++) {
					if (pBssEntry->ExtRate[rateCnt] == 140
					    || pBssEntry->ExtRate[rateCnt] ==
					    146
					    || pBssEntry->ExtRate[rateCnt] >=
					    152)
						isGonly = TRUE;
				}

				if (pBssEntry->HtCapabilityLen != 0) {
					if (isGonly == TRUE)
						strcpy(iwe.u.name, "802.11g/n");
					else
						strcpy(iwe.u.name,
						       "802.11b/g/n");
				} else {
					if (isGonly == TRUE)
						strcpy(iwe.u.name, "802.11g");
					else {
						if (pBssEntry->SupRateLen == 4
						    && pBssEntry->ExtRateLen ==
						    0)
							strcpy(iwe.u.name,
							       "802.11b");
						else
							strcpy(iwe.u.name,
							       "802.11b/g");
					}
				}
			}
		}

		previous_ev = current_ev;
		current_ev =
		    iwe_stream_add_event(info, current_ev, end_buf, &iwe,
					 IW_EV_ADDR_LEN);
		if (current_ev == previous_ev)
			return -E2BIG;

		/*ESSID */
		/*================================ */
		memset(&iwe, 0, sizeof(iwe));
		iwe.cmd = SIOCGIWESSID;
		iwe.u.data.length = pAdapter->ScanTab.BssEntry[i].SsidLen;
		iwe.u.data.flags = 1;

		previous_ev = current_ev;
		current_ev =
		    iwe_stream_add_point(info, current_ev, end_buf, &iwe,
					 (char *)pAdapter->ScanTab.
					 BssEntry[i].Ssid);
		if (current_ev == previous_ev)
			return -E2BIG;

		/*Network Type */
		/*================================ */
		memset(&iwe, 0, sizeof(iwe));
		iwe.cmd = SIOCGIWMODE;
		if (pAdapter->ScanTab.BssEntry[i].BssType == Ndis802_11IBSS) {
			iwe.u.mode = IW_MODE_ADHOC;
		} else if (pAdapter->ScanTab.BssEntry[i].BssType ==
			   Ndis802_11Infrastructure) {
			iwe.u.mode = IW_MODE_INFRA;
		} else {
			iwe.u.mode = IW_MODE_AUTO;
		}
		iwe.len = IW_EV_UINT_LEN;

		previous_ev = current_ev;
		current_ev =
		    iwe_stream_add_event(info, current_ev, end_buf, &iwe,
					 IW_EV_UINT_LEN);
		if (current_ev == previous_ev)
			return -E2BIG;

		/*Channel and Frequency */
		/*================================ */
		memset(&iwe, 0, sizeof(iwe));
		iwe.cmd = SIOCGIWFREQ;
		if (INFRA_ON(pAdapter) || ADHOC_ON(pAdapter))
			iwe.u.freq.m = pAdapter->ScanTab.BssEntry[i].Channel;
		else
			iwe.u.freq.m = pAdapter->ScanTab.BssEntry[i].Channel;
		iwe.u.freq.e = 0;
		iwe.u.freq.i = 0;

		previous_ev = current_ev;
		current_ev =
		    iwe_stream_add_event(info, current_ev, end_buf, &iwe,
					 IW_EV_FREQ_LEN);
		if (current_ev == previous_ev)
			return -E2BIG;

		/*Add quality statistics */
		/*================================ */
		memset(&iwe, 0, sizeof(iwe));
		iwe.cmd = IWEVQUAL;
		iwe.u.qual.level = 0;
		iwe.u.qual.noise = 0;
		set_quality(pAdapter, &iwe.u.qual,
			    pAdapter->ScanTab.BssEntry[i].Rssi);
		current_ev =
		    iwe_stream_add_event(info, current_ev, end_buf, &iwe,
					 IW_EV_QUAL_LEN);
		if (current_ev == previous_ev)
			return -E2BIG;

		/*Encyption key */
		/*================================ */
		memset(&iwe, 0, sizeof(iwe));
		iwe.cmd = SIOCGIWENCODE;
		if (CAP_IS_PRIVACY_ON
		    (pAdapter->ScanTab.BssEntry[i].CapabilityInfo))
			iwe.u.data.flags = IW_ENCODE_ENABLED | IW_ENCODE_NOKEY;
		else
			iwe.u.data.flags = IW_ENCODE_DISABLED;

		previous_ev = current_ev;
		current_ev =
		    iwe_stream_add_point(info, current_ev, end_buf, &iwe,
					 (char *)pAdapter->
					 SharedKey[BSS0][(iwe.u.data.
							  flags &
							  IW_ENCODE_INDEX) -
							 1].Key);
		if (current_ev == previous_ev)
			return -E2BIG;

		/*Bit Rate */
		/*================================ */
		if (pAdapter->ScanTab.BssEntry[i].SupRateLen) {
			u8 tmpRate =
			    pAdapter->ScanTab.BssEntry[i].SupRate[pAdapter->
								  ScanTab.
								  BssEntry[i].
								  SupRateLen -
								  1];
			memset(&iwe, 0, sizeof(iwe));
			iwe.cmd = SIOCGIWRATE;
			current_val = current_ev + IW_EV_LCP_LEN;
			if (tmpRate == 0x82)
				iwe.u.bitrate.value = 1 * 1000000;
			else if (tmpRate == 0x84)
				iwe.u.bitrate.value = 2 * 1000000;
			else if (tmpRate == 0x8B)
				iwe.u.bitrate.value = 5.5 * 1000000;
			else if (tmpRate == 0x96)
				iwe.u.bitrate.value = 11 * 1000000;
			else
				iwe.u.bitrate.value = (tmpRate / 2) * 1000000;

			if (tmpRate == 0x6c
			    && pAdapter->ScanTab.BssEntry[i].HtCapabilityLen >
			    0) {
				int rate_count =
				    sizeof(ralinkrate) / sizeof(__s32);
				struct rt_ht_cap_info capInfo =
				    pAdapter->ScanTab.BssEntry[i].HtCapability.
				    HtCapInfo;
				int shortGI =
				    capInfo.ChannelWidth ? capInfo.
				    ShortGIfor40 : capInfo.ShortGIfor20;
				int maxMCS =
				    pAdapter->ScanTab.BssEntry[i].HtCapability.
				    MCSSet[1] ? 15 : 7;
				int rate_index =
				    12 + ((u8)capInfo.ChannelWidth * 24) +
				    ((u8)shortGI * 48) + ((u8)maxMCS);
				if (rate_index < 0)
					rate_index = 0;
				if (rate_index > rate_count)
					rate_index = rate_count;
				iwe.u.bitrate.value =
				    ralinkrate[rate_index] * 500000;
			}

			iwe.u.bitrate.disabled = 0;
			current_val = iwe_stream_add_value(info, current_ev,
							   current_val, end_buf,
							   &iwe,
							   IW_EV_PARAM_LEN);

			if ((current_val - current_ev) > IW_EV_LCP_LEN)
				current_ev = current_val;
			else
				return -E2BIG;
		}
		/*WPA IE */
		if (pAdapter->ScanTab.BssEntry[i].WpaIE.IELen > 0) {
			memset(&iwe, 0, sizeof(iwe));
			memset(&custom[0], 0, MAX_CUSTOM_LEN);
			memcpy(custom,
			       &(pAdapter->ScanTab.BssEntry[i].WpaIE.IE[0]),
			       pAdapter->ScanTab.BssEntry[i].WpaIE.IELen);
			iwe.cmd = IWEVGENIE;
			iwe.u.data.length =
			    pAdapter->ScanTab.BssEntry[i].WpaIE.IELen;
			current_ev =
			    iwe_stream_add_point(info, current_ev, end_buf,
						 &iwe, custom);
			if (current_ev == previous_ev)
				return -E2BIG;
		}
		/*WPA2 IE */
		if (pAdapter->ScanTab.BssEntry[i].RsnIE.IELen > 0) {
			memset(&iwe, 0, sizeof(iwe));
			memset(&custom[0], 0, MAX_CUSTOM_LEN);
			memcpy(custom,
			       &(pAdapter->ScanTab.BssEntry[i].RsnIE.IE[0]),
			       pAdapter->ScanTab.BssEntry[i].RsnIE.IELen);
			iwe.cmd = IWEVGENIE;
			iwe.u.data.length =
			    pAdapter->ScanTab.BssEntry[i].RsnIE.IELen;
			current_ev =
			    iwe_stream_add_point(info, current_ev, end_buf,
						 &iwe, custom);
			if (current_ev == previous_ev)
				return -E2BIG;
		}
	}

	data->length = current_ev - extra;
	pAdapter->StaCfg.bScanReqIsFromWebUI = FALSE;
	DBGPRINT(RT_DEBUG_ERROR,
		 ("===>rt_ioctl_giwscan. %d(%d) BSS returned, data->length = %d\n",
		  i, pAdapter->ScanTab.BssNr, data->length));
	return 0;
}

int rt_ioctl_siwessid(struct net_device *dev,
		      struct iw_request_info *info,
		      struct iw_point *data, char *essid)
{
	struct rt_rtmp_adapter *pAdapter = NULL;

	GET_PAD_FROM_NET_DEV(pAdapter, dev);

	/*check if the interface is down */
	if (!RTMP_TEST_FLAG(pAdapter, fRTMP_ADAPTER_INTERRUPT_IN_USE)) {
		DBGPRINT(RT_DEBUG_TRACE, ("INFO::Network is down!\n"));
		return -ENETDOWN;
	}

	if (data->flags) {
		char *pSsidString = NULL;

		/* Includes null character. */
		if (data->length > (IW_ESSID_MAX_SIZE + 1))
			return -E2BIG;

		pSsidString = kmalloc(MAX_LEN_OF_SSID + 1, MEM_ALLOC_FLAG);
		if (pSsidString) {
			NdisZeroMemory(pSsidString, MAX_LEN_OF_SSID + 1);
			NdisMoveMemory(pSsidString, essid, data->length);
			if (Set_SSID_Proc(pAdapter, pSsidString) == FALSE)
				return -EINVAL;
		} else
			return -ENOMEM;
	} else {
		/* ANY ssid */
		if (Set_SSID_Proc(pAdapter, "") == FALSE)
			return -EINVAL;
	}
	return 0;
}

int rt_ioctl_giwessid(struct net_device *dev,
		      struct iw_request_info *info,
		      struct iw_point *data, char *essid)
{
	struct rt_rtmp_adapter *pAdapter = NULL;

	GET_PAD_FROM_NET_DEV(pAdapter, dev);

	data->flags = 1;
	if (MONITOR_ON(pAdapter)) {
		data->length = 0;
		return 0;
	}

	if (OPSTATUS_TEST_FLAG(pAdapter, fOP_STATUS_MEDIA_STATE_CONNECTED)) {
		DBGPRINT(RT_DEBUG_TRACE, ("MediaState is connected\n"));
		data->length = pAdapter->CommonCfg.SsidLen;
		memcpy(essid, pAdapter->CommonCfg.Ssid,
		       pAdapter->CommonCfg.SsidLen);
	}
#ifdef RTMP_MAC_USB
	/* Add for RT2870 */
	else if (pAdapter->StaCfg.WpaSupplicantUP != WPA_SUPPLICANT_DISABLE) {
		data->length = pAdapter->CommonCfg.SsidLen;
		memcpy(essid, pAdapter->CommonCfg.Ssid,
		       pAdapter->CommonCfg.SsidLen);
	}
#endif /* RTMP_MAC_USB // */
	else {			/*the ANY ssid was specified */
		data->length = 0;
		DBGPRINT(RT_DEBUG_TRACE,
			 ("MediaState is not connected, ess\n"));
	}

	return 0;

}

int rt_ioctl_siwnickn(struct net_device *dev,
		      struct iw_request_info *info,
		      struct iw_point *data, char *nickname)
{
	struct rt_rtmp_adapter *pAdapter = NULL;

	GET_PAD_FROM_NET_DEV(pAdapter, dev);

	/*check if the interface is down */
	if (!RTMP_TEST_FLAG(pAdapter, fRTMP_ADAPTER_INTERRUPT_IN_USE)) {
		DBGPRINT(RT_DEBUG_TRACE, ("INFO::Network is down!\n"));
		return -ENETDOWN;
	}

	if (data->length > IW_ESSID_MAX_SIZE)
		return -EINVAL;

	memset(pAdapter->nickname, 0, IW_ESSID_MAX_SIZE + 1);
	memcpy(pAdapter->nickname, nickname, data->length);

	return 0;
}

int rt_ioctl_giwnickn(struct net_device *dev,
		      struct iw_request_info *info,
		      struct iw_point *data, char *nickname)
{
	struct rt_rtmp_adapter *pAdapter = NULL;

	GET_PAD_FROM_NET_DEV(pAdapter, dev);

	if (data->length > strlen((char *)pAdapter->nickname) + 1)
		data->length = strlen((char *)pAdapter->nickname) + 1;
	if (data->length > 0) {
		memcpy(nickname, pAdapter->nickname, data->length - 1);
		nickname[data->length - 1] = '\0';
	}
	return 0;
}

int rt_ioctl_siwrts(struct net_device *dev,
		    struct iw_request_info *info,
		    struct iw_param *rts, char *extra)
{
	struct rt_rtmp_adapter *pAdapter = NULL;
	u16 val;

	GET_PAD_FROM_NET_DEV(pAdapter, dev);

	/*check if the interface is down */
	if (!RTMP_TEST_FLAG(pAdapter, fRTMP_ADAPTER_INTERRUPT_IN_USE)) {
		DBGPRINT(RT_DEBUG_TRACE, ("INFO::Network is down!\n"));
		return -ENETDOWN;
	}

	if (rts->disabled)
		val = MAX_RTS_THRESHOLD;
	else if (rts->value < 0 || rts->value > MAX_RTS_THRESHOLD)
		return -EINVAL;
	else if (rts->value == 0)
		val = MAX_RTS_THRESHOLD;
	else
		val = rts->value;

	if (val != pAdapter->CommonCfg.RtsThreshold)
		pAdapter->CommonCfg.RtsThreshold = val;

	return 0;
}

int rt_ioctl_giwrts(struct net_device *dev,
		    struct iw_request_info *info,
		    struct iw_param *rts, char *extra)
{
	struct rt_rtmp_adapter *pAdapter = NULL;

	GET_PAD_FROM_NET_DEV(pAdapter, dev);

	/*check if the interface is down */
	if (!RTMP_TEST_FLAG(pAdapter, fRTMP_ADAPTER_INTERRUPT_IN_USE)) {
		DBGPRINT(RT_DEBUG_TRACE, ("INFO::Network is down!\n"));
		return -ENETDOWN;
	}

	rts->value = pAdapter->CommonCfg.RtsThreshold;
	rts->disabled = (rts->value == MAX_RTS_THRESHOLD);
	rts->fixed = 1;

	return 0;
}

int rt_ioctl_siwfrag(struct net_device *dev,
		     struct iw_request_info *info,
		     struct iw_param *frag, char *extra)
{
	struct rt_rtmp_adapter *pAdapter = NULL;
	u16 val;

	GET_PAD_FROM_NET_DEV(pAdapter, dev);

	/*check if the interface is down */
	if (!RTMP_TEST_FLAG(pAdapter, fRTMP_ADAPTER_INTERRUPT_IN_USE)) {
		DBGPRINT(RT_DEBUG_TRACE, ("INFO::Network is down!\n"));
		return -ENETDOWN;
	}

	if (frag->disabled)
		val = MAX_FRAG_THRESHOLD;
	else if (frag->value >= MIN_FRAG_THRESHOLD
		 && frag->value <= MAX_FRAG_THRESHOLD)
		val = __cpu_to_le16(frag->value & ~0x1);	/* even numbers only */
	else if (frag->value == 0)
		val = MAX_FRAG_THRESHOLD;
	else
		return -EINVAL;

	pAdapter->CommonCfg.FragmentThreshold = val;
	return 0;
}

int rt_ioctl_giwfrag(struct net_device *dev,
		     struct iw_request_info *info,
		     struct iw_param *frag, char *extra)
{
	struct rt_rtmp_adapter *pAdapter = NULL;

	GET_PAD_FROM_NET_DEV(pAdapter, dev);

	/*check if the interface is down */
	if (!RTMP_TEST_FLAG(pAdapter, fRTMP_ADAPTER_INTERRUPT_IN_USE)) {
		DBGPRINT(RT_DEBUG_TRACE, ("INFO::Network is down!\n"));
		return -ENETDOWN;
	}

	frag->value = pAdapter->CommonCfg.FragmentThreshold;
	frag->disabled = (frag->value == MAX_FRAG_THRESHOLD);
	frag->fixed = 1;

	return 0;
}

#define MAX_WEP_KEY_SIZE 13
#define MIN_WEP_KEY_SIZE 5
int rt_ioctl_siwencode(struct net_device *dev,
		       struct iw_request_info *info,
		       struct iw_point *erq, char *extra)
{
	struct rt_rtmp_adapter *pAdapter = NULL;

	GET_PAD_FROM_NET_DEV(pAdapter, dev);

	/*check if the interface is down */
	if (!RTMP_TEST_FLAG(pAdapter, fRTMP_ADAPTER_INTERRUPT_IN_USE)) {
		DBGPRINT(RT_DEBUG_TRACE, ("INFO::Network is down!\n"));
		return -ENETDOWN;
	}

	if ((erq->length == 0) && (erq->flags & IW_ENCODE_DISABLED)) {
		pAdapter->StaCfg.PairCipher = Ndis802_11WEPDisabled;
		pAdapter->StaCfg.GroupCipher = Ndis802_11WEPDisabled;
		pAdapter->StaCfg.WepStatus = Ndis802_11WEPDisabled;
		pAdapter->StaCfg.OrigWepStatus = pAdapter->StaCfg.WepStatus;
		pAdapter->StaCfg.AuthMode = Ndis802_11AuthModeOpen;
		goto done;
	} else if (erq->flags & IW_ENCODE_RESTRICTED
		   || erq->flags & IW_ENCODE_OPEN) {
		/*pAdapter->StaCfg.PortSecured = WPA_802_1X_PORT_SECURED; */
		STA_PORT_SECURED(pAdapter);
		pAdapter->StaCfg.PairCipher = Ndis802_11WEPEnabled;
		pAdapter->StaCfg.GroupCipher = Ndis802_11WEPEnabled;
		pAdapter->StaCfg.WepStatus = Ndis802_11WEPEnabled;
		pAdapter->StaCfg.OrigWepStatus = pAdapter->StaCfg.WepStatus;
		if (erq->flags & IW_ENCODE_RESTRICTED)
			pAdapter->StaCfg.AuthMode = Ndis802_11AuthModeShared;
		else
			pAdapter->StaCfg.AuthMode = Ndis802_11AuthModeOpen;
	}

	if (erq->length > 0) {
		int keyIdx = (erq->flags & IW_ENCODE_INDEX) - 1;
		/* Check the size of the key */
		if (erq->length > MAX_WEP_KEY_SIZE) {
			return -EINVAL;
		}
		/* Check key index */
		if ((keyIdx < 0) || (keyIdx >= NR_WEP_KEYS)) {
			DBGPRINT(RT_DEBUG_TRACE,
				 ("==>rt_ioctl_siwencode::Wrong keyIdx=%d! Using default key instead (%d)\n",
				  keyIdx, pAdapter->StaCfg.DefaultKeyId));

			/*Using default key */
			keyIdx = pAdapter->StaCfg.DefaultKeyId;
		} else
			pAdapter->StaCfg.DefaultKeyId = keyIdx;

		NdisZeroMemory(pAdapter->SharedKey[BSS0][keyIdx].Key, 16);

		if (erq->length == MAX_WEP_KEY_SIZE) {
			pAdapter->SharedKey[BSS0][keyIdx].KeyLen =
			    MAX_WEP_KEY_SIZE;
			pAdapter->SharedKey[BSS0][keyIdx].CipherAlg =
			    CIPHER_WEP128;
		} else if (erq->length == MIN_WEP_KEY_SIZE) {
			pAdapter->SharedKey[BSS0][keyIdx].KeyLen =
			    MIN_WEP_KEY_SIZE;
			pAdapter->SharedKey[BSS0][keyIdx].CipherAlg =
			    CIPHER_WEP64;
		} else
			/* Disable the key */
			pAdapter->SharedKey[BSS0][keyIdx].KeyLen = 0;

		/* Check if the key is not marked as invalid */
		if (!(erq->flags & IW_ENCODE_NOKEY)) {
			/* Copy the key in the driver */
			NdisMoveMemory(pAdapter->SharedKey[BSS0][keyIdx].Key,
				       extra, erq->length);
		}
	} else {
		/* Do we want to just set the transmit key index ? */
		int index = (erq->flags & IW_ENCODE_INDEX) - 1;
		if ((index >= 0) && (index < 4)) {
			pAdapter->StaCfg.DefaultKeyId = index;
		} else
			/* Don't complain if only change the mode */
		if (!(erq->flags & IW_ENCODE_MODE))
			return -EINVAL;
	}

done:
	DBGPRINT(RT_DEBUG_TRACE,
		 ("==>rt_ioctl_siwencode::erq->flags=%x\n", erq->flags));
	DBGPRINT(RT_DEBUG_TRACE,
		 ("==>rt_ioctl_siwencode::AuthMode=%x\n",
		  pAdapter->StaCfg.AuthMode));
	DBGPRINT(RT_DEBUG_TRACE,
		 ("==>rt_ioctl_siwencode::DefaultKeyId=%x, KeyLen = %d\n",
		  pAdapter->StaCfg.DefaultKeyId,
		  pAdapter->SharedKey[BSS0][pAdapter->StaCfg.DefaultKeyId].
		  KeyLen));
	DBGPRINT(RT_DEBUG_TRACE,
		 ("==>rt_ioctl_siwencode::WepStatus=%x\n",
		  pAdapter->StaCfg.WepStatus));
	return 0;
}

int
rt_ioctl_giwencode(struct net_device *dev,
		   struct iw_request_info *info,
		   struct iw_point *erq, char *key)
{
	int kid;
	struct rt_rtmp_adapter *pAdapter = NULL;

	GET_PAD_FROM_NET_DEV(pAdapter, dev);

	/*check if the interface is down */
	if (!RTMP_TEST_FLAG(pAdapter, fRTMP_ADAPTER_INTERRUPT_IN_USE)) {
		DBGPRINT(RT_DEBUG_TRACE, ("INFO::Network is down!\n"));
		return -ENETDOWN;
	}

	kid = erq->flags & IW_ENCODE_INDEX;
	DBGPRINT(RT_DEBUG_TRACE,
		 ("===>rt_ioctl_giwencode %d\n", erq->flags & IW_ENCODE_INDEX));

	if (pAdapter->StaCfg.WepStatus == Ndis802_11WEPDisabled) {
		erq->length = 0;
		erq->flags = IW_ENCODE_DISABLED;
	} else if ((kid > 0) && (kid <= 4)) {
		/* copy wep key */
		erq->flags = kid;	/* NB: base 1 */
		if (erq->length > pAdapter->SharedKey[BSS0][kid - 1].KeyLen)
			erq->length = pAdapter->SharedKey[BSS0][kid - 1].KeyLen;
		memcpy(key, pAdapter->SharedKey[BSS0][kid - 1].Key,
		       erq->length);
		/*if ((kid == pAdapter->PortCfg.DefaultKeyId)) */
		/*erq->flags |= IW_ENCODE_ENABLED; */	/* XXX */
		if (pAdapter->StaCfg.AuthMode == Ndis802_11AuthModeShared)
			erq->flags |= IW_ENCODE_RESTRICTED;	/* XXX */
		else
			erq->flags |= IW_ENCODE_OPEN;	/* XXX */

	} else if (kid == 0) {
		if (pAdapter->StaCfg.AuthMode == Ndis802_11AuthModeShared)
			erq->flags |= IW_ENCODE_RESTRICTED;	/* XXX */
		else
			erq->flags |= IW_ENCODE_OPEN;	/* XXX */
		erq->length =
		    pAdapter->SharedKey[BSS0][pAdapter->StaCfg.DefaultKeyId].
		    KeyLen;
		memcpy(key,
		       pAdapter->SharedKey[BSS0][pAdapter->StaCfg.DefaultKeyId].
		       Key, erq->length);
		/* copy default key ID */
		if (pAdapter->StaCfg.AuthMode == Ndis802_11AuthModeShared)
			erq->flags |= IW_ENCODE_RESTRICTED;	/* XXX */
		else
			erq->flags |= IW_ENCODE_OPEN;	/* XXX */
		erq->flags = pAdapter->StaCfg.DefaultKeyId + 1;	/* NB: base 1 */
		erq->flags |= IW_ENCODE_ENABLED;	/* XXX */
	}

	return 0;

}

void getBaInfo(struct rt_rtmp_adapter *pAd, char *pOutBuf)
{
	int i, j;
	struct rt_ba_ori_entry *pOriBAEntry;
	struct rt_ba_rec_entry *pRecBAEntry;

	for (i = 0; i < MAX_LEN_OF_MAC_TABLE; i++) {
		struct rt_mac_table_entry *pEntry = &pAd->MacTab.Content[i];
		if (((pEntry->ValidAsCLI || pEntry->ValidAsApCli)
		     && (pEntry->Sst == SST_ASSOC))
		    || (pEntry->ValidAsWDS) || (pEntry->ValidAsMesh)) {
			sprintf(pOutBuf + strlen(pOutBuf),
				"\n%02X:%02X:%02X:%02X:%02X:%02X (Aid = %d) (AP) -\n",
				pEntry->Addr[0], pEntry->Addr[1],
				pEntry->Addr[2], pEntry->Addr[3],
				pEntry->Addr[4], pEntry->Addr[5], pEntry->Aid);

			sprintf(pOutBuf, "%s[Recipient]\n", pOutBuf);
			for (j = 0; j < NUM_OF_TID; j++) {
				if (pEntry->BARecWcidArray[j] != 0) {
					pRecBAEntry =
					    &pAd->BATable.BARecEntry[pEntry->
								     BARecWcidArray
								     [j]];
					sprintf(pOutBuf + strlen(pOutBuf),
						"TID=%d, BAWinSize=%d, LastIndSeq=%d, ReorderingPkts=%d\n",
						j, pRecBAEntry->BAWinSize,
						pRecBAEntry->LastIndSeq,
						pRecBAEntry->list.qlen);
				}
			}
			sprintf(pOutBuf, "%s\n", pOutBuf);

			sprintf(pOutBuf, "%s[Originator]\n", pOutBuf);
			for (j = 0; j < NUM_OF_TID; j++) {
				if (pEntry->BAOriWcidArray[j] != 0) {
					pOriBAEntry =
					    &pAd->BATable.BAOriEntry[pEntry->
								     BAOriWcidArray
								     [j]];
					sprintf(pOutBuf + strlen(pOutBuf),
						"TID=%d, BAWinSize=%d, StartSeq=%d, CurTxSeq=%d\n",
						j, pOriBAEntry->BAWinSize,
						pOriBAEntry->Sequence,
						pEntry->TxSeq[j]);
				}
			}
			sprintf(pOutBuf, "%s\n\n", pOutBuf);
		}
		if (strlen(pOutBuf) > (IW_PRIV_SIZE_MASK - 30))
			break;
	}

	return;
}

int rt_ioctl_siwmlme(struct net_device *dev,
		     struct iw_request_info *info,
		     union iwreq_data *wrqu, char *extra)
{
	struct rt_rtmp_adapter *pAd = NULL;
	struct iw_mlme *pMlme = (struct iw_mlme *)wrqu->data.pointer;
	struct rt_mlme_queue_elem MsgElem;
	struct rt_mlme_disassoc_req DisAssocReq;
	struct rt_mlme_deauth_req DeAuthReq;

	GET_PAD_FROM_NET_DEV(pAd, dev);

	DBGPRINT(RT_DEBUG_TRACE, ("====> %s\n", __func__));

	if (pMlme == NULL)
		return -EINVAL;

	switch (pMlme->cmd) {
#ifdef IW_MLME_DEAUTH
	case IW_MLME_DEAUTH:
		DBGPRINT(RT_DEBUG_TRACE,
			 ("====> %s - IW_MLME_DEAUTH\n", __func__));
		COPY_MAC_ADDR(DeAuthReq.Addr, pAd->CommonCfg.Bssid);
		DeAuthReq.Reason = pMlme->reason_code;
		MsgElem.MsgLen = sizeof(struct rt_mlme_deauth_req);
		NdisMoveMemory(MsgElem.Msg, &DeAuthReq,
			       sizeof(struct rt_mlme_deauth_req));
		MlmeDeauthReqAction(pAd, &MsgElem);
		if (INFRA_ON(pAd)) {
			LinkDown(pAd, FALSE);
			pAd->Mlme.AssocMachine.CurrState = ASSOC_IDLE;
		}
		break;
#endif /* IW_MLME_DEAUTH // */
#ifdef IW_MLME_DISASSOC
	case IW_MLME_DISASSOC:
		DBGPRINT(RT_DEBUG_TRACE,
			 ("====> %s - IW_MLME_DISASSOC\n", __func__));
		COPY_MAC_ADDR(DisAssocReq.Addr, pAd->CommonCfg.Bssid);
		DisAssocReq.Reason = pMlme->reason_code;

		MsgElem.Machine = ASSOC_STATE_MACHINE;
		MsgElem.MsgType = MT2_MLME_DISASSOC_REQ;
		MsgElem.MsgLen = sizeof(struct rt_mlme_disassoc_req);
		NdisMoveMemory(MsgElem.Msg, &DisAssocReq,
			       sizeof(struct rt_mlme_disassoc_req));

		pAd->Mlme.CntlMachine.CurrState = CNTL_WAIT_OID_DISASSOC;
		MlmeDisassocReqAction(pAd, &MsgElem);
		break;
#endif /* IW_MLME_DISASSOC // */
	default:
		DBGPRINT(RT_DEBUG_TRACE,
			 ("====> %s - Unknow Command\n", __func__));
		break;
	}

	return 0;
}

int rt_ioctl_siwauth(struct net_device *dev,
		     struct iw_request_info *info,
		     union iwreq_data *wrqu, char *extra)
{
	struct rt_rtmp_adapter *pAdapter = NULL;
	struct iw_param *param = &wrqu->param;

	GET_PAD_FROM_NET_DEV(pAdapter, dev);

	/*check if the interface is down */
	if (!RTMP_TEST_FLAG(pAdapter, fRTMP_ADAPTER_INTERRUPT_IN_USE)) {
		DBGPRINT(RT_DEBUG_TRACE, ("INFO::Network is down!\n"));
		return -ENETDOWN;
	}
	switch (param->flags & IW_AUTH_INDEX) {
	case IW_AUTH_WPA_VERSION:
		if (param->value == IW_AUTH_WPA_VERSION_WPA) {
			pAdapter->StaCfg.AuthMode = Ndis802_11AuthModeWPAPSK;
			if (pAdapter->StaCfg.BssType == BSS_ADHOC)
				pAdapter->StaCfg.AuthMode =
				    Ndis802_11AuthModeWPANone;
		} else if (param->value == IW_AUTH_WPA_VERSION_WPA2)
			pAdapter->StaCfg.AuthMode = Ndis802_11AuthModeWPA2PSK;

		DBGPRINT(RT_DEBUG_TRACE,
			 ("%s::IW_AUTH_WPA_VERSION - param->value = %d!\n",
			  __func__, param->value));
		break;
	case IW_AUTH_CIPHER_PAIRWISE:
		if (param->value == IW_AUTH_CIPHER_NONE) {
			pAdapter->StaCfg.WepStatus = Ndis802_11WEPDisabled;
			pAdapter->StaCfg.OrigWepStatus =
			    pAdapter->StaCfg.WepStatus;
			pAdapter->StaCfg.PairCipher = Ndis802_11WEPDisabled;
		} else if (param->value == IW_AUTH_CIPHER_WEP40 ||
			   param->value == IW_AUTH_CIPHER_WEP104) {
			pAdapter->StaCfg.WepStatus = Ndis802_11WEPEnabled;
			pAdapter->StaCfg.OrigWepStatus =
			    pAdapter->StaCfg.WepStatus;
			pAdapter->StaCfg.PairCipher = Ndis802_11WEPEnabled;
			pAdapter->StaCfg.IEEE8021X = FALSE;
		} else if (param->value == IW_AUTH_CIPHER_TKIP) {
			pAdapter->StaCfg.WepStatus =
			    Ndis802_11Encryption2Enabled;
			pAdapter->StaCfg.OrigWepStatus =
			    pAdapter->StaCfg.WepStatus;
			pAdapter->StaCfg.PairCipher =
			    Ndis802_11Encryption2Enabled;
		} else if (param->value == IW_AUTH_CIPHER_CCMP) {
			pAdapter->StaCfg.WepStatus =
			    Ndis802_11Encryption3Enabled;
			pAdapter->StaCfg.OrigWepStatus =
			    pAdapter->StaCfg.WepStatus;
			pAdapter->StaCfg.PairCipher =
			    Ndis802_11Encryption3Enabled;
		}
		DBGPRINT(RT_DEBUG_TRACE,
			 ("%s::IW_AUTH_CIPHER_PAIRWISE - param->value = %d!\n",
			  __func__, param->value));
		break;
	case IW_AUTH_CIPHER_GROUP:
		if (param->value == IW_AUTH_CIPHER_NONE) {
			pAdapter->StaCfg.GroupCipher = Ndis802_11WEPDisabled;
		} else if (param->value == IW_AUTH_CIPHER_WEP40 ||
			   param->value == IW_AUTH_CIPHER_WEP104) {
			pAdapter->StaCfg.GroupCipher = Ndis802_11WEPEnabled;
		} else if (param->value == IW_AUTH_CIPHER_TKIP) {
			pAdapter->StaCfg.GroupCipher =
			    Ndis802_11Encryption2Enabled;
		} else if (param->value == IW_AUTH_CIPHER_CCMP) {
			pAdapter->StaCfg.GroupCipher =
			    Ndis802_11Encryption3Enabled;
		}
		DBGPRINT(RT_DEBUG_TRACE,
			 ("%s::IW_AUTH_CIPHER_GROUP - param->value = %d!\n",
			  __func__, param->value));
		break;
	case IW_AUTH_KEY_MGMT:
		if (param->value == IW_AUTH_KEY_MGMT_802_1X) {
			if (pAdapter->StaCfg.AuthMode ==
			    Ndis802_11AuthModeWPAPSK) {
				pAdapter->StaCfg.AuthMode =
				    Ndis802_11AuthModeWPA;
				pAdapter->StaCfg.IEEE8021X = FALSE;
			} else if (pAdapter->StaCfg.AuthMode ==
				   Ndis802_11AuthModeWPA2PSK) {
				pAdapter->StaCfg.AuthMode =
				    Ndis802_11AuthModeWPA2;
				pAdapter->StaCfg.IEEE8021X = FALSE;
			} else
				/* WEP 1x */
				pAdapter->StaCfg.IEEE8021X = TRUE;
		} else if (param->value == 0) {
			/*pAdapter->StaCfg.PortSecured = WPA_802_1X_PORT_SECURED; */
			STA_PORT_SECURED(pAdapter);
		}
		DBGPRINT(RT_DEBUG_TRACE,
			 ("%s::IW_AUTH_KEY_MGMT - param->value = %d!\n",
			  __func__, param->value));
		break;
	case IW_AUTH_RX_UNENCRYPTED_EAPOL:
		break;
	case IW_AUTH_PRIVACY_INVOKED:
		/*if (param->value == 0)
		   {
		   pAdapter->StaCfg.AuthMode = Ndis802_11AuthModeOpen;
		   pAdapter->StaCfg.WepStatus = Ndis802_11WEPDisabled;
		   pAdapter->StaCfg.OrigWepStatus = pAdapter->StaCfg.WepStatus;
		   pAdapter->StaCfg.PairCipher = Ndis802_11WEPDisabled;
		   pAdapter->StaCfg.GroupCipher = Ndis802_11WEPDisabled;
		   } */
		DBGPRINT(RT_DEBUG_TRACE,
			 ("%s::IW_AUTH_PRIVACY_INVOKED - param->value = %d!\n",
			  __func__, param->value));
		break;
	case IW_AUTH_DROP_UNENCRYPTED:
		if (param->value != 0)
			pAdapter->StaCfg.PortSecured =
			    WPA_802_1X_PORT_NOT_SECURED;
		else {
			/*pAdapter->StaCfg.PortSecured = WPA_802_1X_PORT_SECURED; */
			STA_PORT_SECURED(pAdapter);
		}
		DBGPRINT(RT_DEBUG_TRACE,
			 ("%s::IW_AUTH_WPA_VERSION - param->value = %d!\n",
			  __func__, param->value));
		break;
	case IW_AUTH_80211_AUTH_ALG:
		if (param->value & IW_AUTH_ALG_SHARED_KEY) {
			pAdapter->StaCfg.AuthMode = Ndis802_11AuthModeShared;
		} else if (param->value & IW_AUTH_ALG_OPEN_SYSTEM) {
			pAdapter->StaCfg.AuthMode = Ndis802_11AuthModeOpen;
		} else
			return -EINVAL;
		DBGPRINT(RT_DEBUG_TRACE,
			 ("%s::IW_AUTH_80211_AUTH_ALG - param->value = %d!\n",
			  __func__, param->value));
		break;
	case IW_AUTH_WPA_ENABLED:
		DBGPRINT(RT_DEBUG_TRACE,
			 ("%s::IW_AUTH_WPA_ENABLED - Driver supports WPA!(param->value = %d)\n",
			  __func__, param->value));
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

int rt_ioctl_giwauth(struct net_device *dev,
		     struct iw_request_info *info,
		     union iwreq_data *wrqu, char *extra)
{
	struct rt_rtmp_adapter *pAdapter = NULL;
	struct iw_param *param = &wrqu->param;

	GET_PAD_FROM_NET_DEV(pAdapter, dev);

	/*check if the interface is down */
	if (!RTMP_TEST_FLAG(pAdapter, fRTMP_ADAPTER_INTERRUPT_IN_USE)) {
		DBGPRINT(RT_DEBUG_TRACE, ("INFO::Network is down!\n"));
		return -ENETDOWN;
	}

	switch (param->flags & IW_AUTH_INDEX) {
	case IW_AUTH_DROP_UNENCRYPTED:
		param->value =
		    (pAdapter->StaCfg.WepStatus ==
		     Ndis802_11WEPDisabled) ? 0 : 1;
		break;

	case IW_AUTH_80211_AUTH_ALG:
		param->value =
		    (pAdapter->StaCfg.AuthMode ==
		     Ndis802_11AuthModeShared) ? IW_AUTH_ALG_SHARED_KEY :
		    IW_AUTH_ALG_OPEN_SYSTEM;
		break;

	case IW_AUTH_WPA_ENABLED:
		param->value =
		    (pAdapter->StaCfg.AuthMode >=
		     Ndis802_11AuthModeWPA) ? 1 : 0;
		break;

	default:
		return -EOPNOTSUPP;
	}
	DBGPRINT(RT_DEBUG_TRACE,
		 ("rt_ioctl_giwauth::param->value = %d!\n", param->value));
	return 0;
}

void fnSetCipherKey(struct rt_rtmp_adapter *pAdapter,
		    int keyIdx,
		    u8 CipherAlg,
		    IN BOOLEAN bGTK, IN struct iw_encode_ext *ext)
{
	NdisZeroMemory(&pAdapter->SharedKey[BSS0][keyIdx], sizeof(struct rt_cipher_key));
	pAdapter->SharedKey[BSS0][keyIdx].KeyLen = LEN_TKIP_EK;
	NdisMoveMemory(pAdapter->SharedKey[BSS0][keyIdx].Key, ext->key,
		       LEN_TKIP_EK);
	NdisMoveMemory(pAdapter->SharedKey[BSS0][keyIdx].TxMic,
		       ext->key + LEN_TKIP_EK, LEN_TKIP_TXMICK);
	NdisMoveMemory(pAdapter->SharedKey[BSS0][keyIdx].RxMic,
		       ext->key + LEN_TKIP_EK + LEN_TKIP_TXMICK,
		       LEN_TKIP_RXMICK);
	pAdapter->SharedKey[BSS0][keyIdx].CipherAlg = CipherAlg;

	/* Update group key information to ASIC Shared Key Table */
	AsicAddSharedKeyEntry(pAdapter,
			      BSS0,
			      keyIdx,
			      pAdapter->SharedKey[BSS0][keyIdx].CipherAlg,
			      pAdapter->SharedKey[BSS0][keyIdx].Key,
			      pAdapter->SharedKey[BSS0][keyIdx].TxMic,
			      pAdapter->SharedKey[BSS0][keyIdx].RxMic);

	if (bGTK)
		/* Update ASIC WCID attribute table and IVEIV table */
		RTMPAddWcidAttributeEntry(pAdapter,
					  BSS0,
					  keyIdx,
					  pAdapter->SharedKey[BSS0][keyIdx].
					  CipherAlg, NULL);
	else
		/* Update ASIC WCID attribute table and IVEIV table */
		RTMPAddWcidAttributeEntry(pAdapter,
					  BSS0,
					  keyIdx,
					  pAdapter->SharedKey[BSS0][keyIdx].
					  CipherAlg,
					  &pAdapter->MacTab.
					  Content[BSSID_WCID]);
}

int rt_ioctl_siwencodeext(struct net_device *dev,
			  struct iw_request_info *info,
			  union iwreq_data *wrqu, char *extra)
{
	struct rt_rtmp_adapter *pAdapter = NULL;
	struct iw_point *encoding = &wrqu->encoding;
	struct iw_encode_ext *ext = (struct iw_encode_ext *)extra;
	int keyIdx, alg = ext->alg;

	GET_PAD_FROM_NET_DEV(pAdapter, dev);

	/*check if the interface is down */
	if (!RTMP_TEST_FLAG(pAdapter, fRTMP_ADAPTER_INTERRUPT_IN_USE)) {
		DBGPRINT(RT_DEBUG_TRACE, ("INFO::Network is down!\n"));
		return -ENETDOWN;
	}

	if (encoding->flags & IW_ENCODE_DISABLED) {
		keyIdx = (encoding->flags & IW_ENCODE_INDEX) - 1;
		/* set BSSID wcid entry of the Pair-wise Key table as no-security mode */
		AsicRemovePairwiseKeyEntry(pAdapter, BSS0, BSSID_WCID);
		pAdapter->SharedKey[BSS0][keyIdx].KeyLen = 0;
		pAdapter->SharedKey[BSS0][keyIdx].CipherAlg = CIPHER_NONE;
		AsicRemoveSharedKeyEntry(pAdapter, 0, (u8)keyIdx);
		NdisZeroMemory(&pAdapter->SharedKey[BSS0][keyIdx],
			       sizeof(struct rt_cipher_key));
		DBGPRINT(RT_DEBUG_TRACE,
			 ("%s::Remove all keys!(encoding->flags = %x)\n",
			  __func__, encoding->flags));
	} else {
		/* Get Key Index and convet to our own defined key index */
		keyIdx = (encoding->flags & IW_ENCODE_INDEX) - 1;
		if ((keyIdx < 0) || (keyIdx >= NR_WEP_KEYS))
			return -EINVAL;

		if (ext->ext_flags & IW_ENCODE_EXT_SET_TX_KEY) {
			pAdapter->StaCfg.DefaultKeyId = keyIdx;
			DBGPRINT(RT_DEBUG_TRACE,
				 ("%s::DefaultKeyId = %d\n", __func__,
				  pAdapter->StaCfg.DefaultKeyId));
		}

		switch (alg) {
		case IW_ENCODE_ALG_NONE:
			DBGPRINT(RT_DEBUG_TRACE,
				 ("%s::IW_ENCODE_ALG_NONE\n", __func__));
			break;
		case IW_ENCODE_ALG_WEP:
			DBGPRINT(RT_DEBUG_TRACE,
				 ("%s::IW_ENCODE_ALG_WEP - ext->key_len = %d, keyIdx = %d\n",
				  __func__, ext->key_len, keyIdx));
			if (ext->key_len == MAX_WEP_KEY_SIZE) {
				pAdapter->SharedKey[BSS0][keyIdx].KeyLen =
				    MAX_WEP_KEY_SIZE;
				pAdapter->SharedKey[BSS0][keyIdx].CipherAlg =
				    CIPHER_WEP128;
			} else if (ext->key_len == MIN_WEP_KEY_SIZE) {
				pAdapter->SharedKey[BSS0][keyIdx].KeyLen =
				    MIN_WEP_KEY_SIZE;
				pAdapter->SharedKey[BSS0][keyIdx].CipherAlg =
				    CIPHER_WEP64;
			} else
				return -EINVAL;

			NdisZeroMemory(pAdapter->SharedKey[BSS0][keyIdx].Key,
				       16);
			NdisMoveMemory(pAdapter->SharedKey[BSS0][keyIdx].Key,
				       ext->key, ext->key_len);
			if (pAdapter->StaCfg.GroupCipher ==
			    Ndis802_11GroupWEP40Enabled
			    || pAdapter->StaCfg.GroupCipher ==
			    Ndis802_11GroupWEP104Enabled) {
				/* Set Group key material to Asic */
				AsicAddSharedKeyEntry(pAdapter, BSS0, keyIdx,
						      pAdapter->
						      SharedKey[BSS0][keyIdx].
						      CipherAlg,
						      pAdapter->
						      SharedKey[BSS0][keyIdx].
						      Key, NULL, NULL);

				/* Update WCID attribute table and IVEIV table for this group key table */
				RTMPAddWcidAttributeEntry(pAdapter, BSS0,
							  keyIdx,
							  pAdapter->
							  SharedKey[BSS0]
							  [keyIdx].CipherAlg,
							  NULL);

				STA_PORT_SECURED(pAdapter);

				/* Indicate Connected for GUI */
				pAdapter->IndicateMediaState =
				    NdisMediaStateConnected;
			}
			break;
		case IW_ENCODE_ALG_TKIP:
			DBGPRINT(RT_DEBUG_TRACE,
				 ("%s::IW_ENCODE_ALG_TKIP - keyIdx = %d, ext->key_len = %d\n",
				  __func__, keyIdx, ext->key_len));
			if (ext->key_len == 32) {
				if (ext->ext_flags & IW_ENCODE_EXT_SET_TX_KEY) {
					fnSetCipherKey(pAdapter, keyIdx,
						       CIPHER_TKIP, FALSE, ext);
					if (pAdapter->StaCfg.AuthMode >=
					    Ndis802_11AuthModeWPA2) {
						/*pAdapter->StaCfg.PortSecured = WPA_802_1X_PORT_SECURED; */
						STA_PORT_SECURED(pAdapter);
						pAdapter->IndicateMediaState =
						    NdisMediaStateConnected;
					}
				} else if (ext->
					   ext_flags & IW_ENCODE_EXT_GROUP_KEY)
				{
					fnSetCipherKey(pAdapter, keyIdx,
						       CIPHER_TKIP, TRUE, ext);

					/* set 802.1x port control */
					/*pAdapter->StaCfg.PortSecured = WPA_802_1X_PORT_SECURED; */
					STA_PORT_SECURED(pAdapter);
					pAdapter->IndicateMediaState =
					    NdisMediaStateConnected;
				}
			} else
				return -EINVAL;
			break;
		case IW_ENCODE_ALG_CCMP:
			if (ext->ext_flags & IW_ENCODE_EXT_SET_TX_KEY) {
				fnSetCipherKey(pAdapter, keyIdx, CIPHER_AES,
					       FALSE, ext);
				if (pAdapter->StaCfg.AuthMode >=
				    Ndis802_11AuthModeWPA2)
					/*pAdapter->StaCfg.PortSecured = WPA_802_1X_PORT_SECURED; */
					STA_PORT_SECURED(pAdapter);
				pAdapter->IndicateMediaState =
				    NdisMediaStateConnected;
			} else if (ext->ext_flags & IW_ENCODE_EXT_GROUP_KEY) {
				fnSetCipherKey(pAdapter, keyIdx, CIPHER_AES,
					       TRUE, ext);

				/* set 802.1x port control */
				/*pAdapter->StaCfg.PortSecured = WPA_802_1X_PORT_SECURED; */
				STA_PORT_SECURED(pAdapter);
				pAdapter->IndicateMediaState =
				    NdisMediaStateConnected;
			}
			break;
		default:
			return -EINVAL;
		}
	}

	return 0;
}

int
rt_ioctl_giwencodeext(struct net_device *dev,
		      struct iw_request_info *info,
		      union iwreq_data *wrqu, char *extra)
{
	struct rt_rtmp_adapter *pAd = NULL;
	char *pKey = NULL;
	struct iw_point *encoding = &wrqu->encoding;
	struct iw_encode_ext *ext = (struct iw_encode_ext *)extra;
	int idx, max_key_len;

	GET_PAD_FROM_NET_DEV(pAd, dev);

	DBGPRINT(RT_DEBUG_TRACE, ("===> rt_ioctl_giwencodeext\n"));

	max_key_len = encoding->length - sizeof(*ext);
	if (max_key_len < 0)
		return -EINVAL;

	idx = encoding->flags & IW_ENCODE_INDEX;
	if (idx) {
		if (idx < 1 || idx > 4)
			return -EINVAL;
		idx--;

		if ((pAd->StaCfg.WepStatus == Ndis802_11Encryption2Enabled) ||
		    (pAd->StaCfg.WepStatus == Ndis802_11Encryption3Enabled)) {
			if (idx != pAd->StaCfg.DefaultKeyId) {
				ext->key_len = 0;
				return 0;
			}
		}
	} else
		idx = pAd->StaCfg.DefaultKeyId;

	encoding->flags = idx + 1;
	memset(ext, 0, sizeof(*ext));

	ext->key_len = 0;
	switch (pAd->StaCfg.WepStatus) {
	case Ndis802_11WEPDisabled:
		ext->alg = IW_ENCODE_ALG_NONE;
		encoding->flags |= IW_ENCODE_DISABLED;
		break;
	case Ndis802_11WEPEnabled:
		ext->alg = IW_ENCODE_ALG_WEP;
		if (pAd->SharedKey[BSS0][idx].KeyLen > max_key_len)
			return -E2BIG;
		else {
			ext->key_len = pAd->SharedKey[BSS0][idx].KeyLen;
			pKey = (char *)& (pAd->SharedKey[BSS0][idx].Key[0]);
		}
		break;
	case Ndis802_11Encryption2Enabled:
	case Ndis802_11Encryption3Enabled:
		if (pAd->StaCfg.WepStatus == Ndis802_11Encryption2Enabled)
			ext->alg = IW_ENCODE_ALG_TKIP;
		else
			ext->alg = IW_ENCODE_ALG_CCMP;

		if (max_key_len < 32)
			return -E2BIG;
		else {
			ext->key_len = 32;
			pKey = (char *)& pAd->StaCfg.PMK[0];
		}
		break;
	default:
		return -EINVAL;
	}

	if (ext->key_len && pKey) {
		encoding->flags |= IW_ENCODE_ENABLED;
		memcpy(ext->key, pKey, ext->key_len);
	}

	return 0;
}

int rt_ioctl_siwgenie(struct net_device *dev,
		      struct iw_request_info *info,
		      union iwreq_data *wrqu, char *extra)
{
	struct rt_rtmp_adapter *pAd = NULL;

	GET_PAD_FROM_NET_DEV(pAd, dev);

	DBGPRINT(RT_DEBUG_TRACE, ("===> rt_ioctl_siwgenie\n"));
	pAd->StaCfg.bRSN_IE_FromWpaSupplicant = FALSE;
	if (wrqu->data.length > MAX_LEN_OF_RSNIE ||
	    (wrqu->data.length && extra == NULL))
		return -EINVAL;

	if (wrqu->data.length) {
		pAd->StaCfg.RSNIE_Len = wrqu->data.length;
		NdisMoveMemory(&pAd->StaCfg.RSN_IE[0], extra,
			       pAd->StaCfg.RSNIE_Len);
		pAd->StaCfg.bRSN_IE_FromWpaSupplicant = TRUE;
	} else {
		pAd->StaCfg.RSNIE_Len = 0;
		NdisZeroMemory(&pAd->StaCfg.RSN_IE[0], MAX_LEN_OF_RSNIE);
	}

	return 0;
}

int rt_ioctl_giwgenie(struct net_device *dev,
		      struct iw_request_info *info,
		      union iwreq_data *wrqu, char *extra)
{
	struct rt_rtmp_adapter *pAd = NULL;

	GET_PAD_FROM_NET_DEV(pAd, dev);

	if ((pAd->StaCfg.RSNIE_Len == 0) ||
	    (pAd->StaCfg.AuthMode < Ndis802_11AuthModeWPA)) {
		wrqu->data.length = 0;
		return 0;
	}

	if (pAd->StaCfg.WpaSupplicantUP == WPA_SUPPLICANT_ENABLE) {
		if (wrqu->data.length < pAd->StaCfg.RSNIE_Len)
			return -E2BIG;

		wrqu->data.length = pAd->StaCfg.RSNIE_Len;
		memcpy(extra, &pAd->StaCfg.RSN_IE[0], pAd->StaCfg.RSNIE_Len);
	} else {
		u8 RSNIe = IE_WPA;

		if (wrqu->data.length < (pAd->StaCfg.RSNIE_Len + 2))	/* ID, Len */
			return -E2BIG;
		wrqu->data.length = pAd->StaCfg.RSNIE_Len + 2;

		if ((pAd->StaCfg.AuthMode == Ndis802_11AuthModeWPA2PSK) ||
		    (pAd->StaCfg.AuthMode == Ndis802_11AuthModeWPA2))
			RSNIe = IE_RSN;

		extra[0] = (char)RSNIe;
		extra[1] = pAd->StaCfg.RSNIE_Len;
		memcpy(extra + 2, &pAd->StaCfg.RSN_IE[0],
		       pAd->StaCfg.RSNIE_Len);
	}

	return 0;
}

int rt_ioctl_siwpmksa(struct net_device *dev,
		      struct iw_request_info *info,
		      union iwreq_data *wrqu, char *extra)
{
	struct rt_rtmp_adapter *pAd = NULL;
	struct iw_pmksa *pPmksa = (struct iw_pmksa *)wrqu->data.pointer;
	int CachedIdx = 0, idx = 0;

	GET_PAD_FROM_NET_DEV(pAd, dev);

	if (pPmksa == NULL)
		return -EINVAL;

	DBGPRINT(RT_DEBUG_TRACE, ("===> rt_ioctl_siwpmksa\n"));
	switch (pPmksa->cmd) {
	case IW_PMKSA_FLUSH:
		NdisZeroMemory(pAd->StaCfg.SavedPMK,
			       sizeof(struct rt_bssid_info) * PMKID_NO);
		DBGPRINT(RT_DEBUG_TRACE,
			 ("rt_ioctl_siwpmksa - IW_PMKSA_FLUSH\n"));
		break;
	case IW_PMKSA_REMOVE:
		for (CachedIdx = 0; CachedIdx < pAd->StaCfg.SavedPMKNum;
		     CachedIdx++) {
			/* compare the BSSID */
			if (NdisEqualMemory
			    (pPmksa->bssid.sa_data,
			     pAd->StaCfg.SavedPMK[CachedIdx].BSSID,
			     MAC_ADDR_LEN)) {
				NdisZeroMemory(pAd->StaCfg.SavedPMK[CachedIdx].
					       BSSID, MAC_ADDR_LEN);
				NdisZeroMemory(pAd->StaCfg.SavedPMK[CachedIdx].
					       PMKID, 16);
				for (idx = CachedIdx;
				     idx < (pAd->StaCfg.SavedPMKNum - 1);
				     idx++) {
					NdisMoveMemory(&pAd->StaCfg.
						       SavedPMK[idx].BSSID[0],
						       &pAd->StaCfg.
						       SavedPMK[idx +
								1].BSSID[0],
						       MAC_ADDR_LEN);
					NdisMoveMemory(&pAd->StaCfg.
						       SavedPMK[idx].PMKID[0],
						       &pAd->StaCfg.
						       SavedPMK[idx +
								1].PMKID[0],
						       16);
				}
				pAd->StaCfg.SavedPMKNum--;
				break;
			}
		}

		DBGPRINT(RT_DEBUG_TRACE,
			 ("rt_ioctl_siwpmksa - IW_PMKSA_REMOVE\n"));
		break;
	case IW_PMKSA_ADD:
		for (CachedIdx = 0; CachedIdx < pAd->StaCfg.SavedPMKNum;
		     CachedIdx++) {
			/* compare the BSSID */
			if (NdisEqualMemory
			    (pPmksa->bssid.sa_data,
			     pAd->StaCfg.SavedPMK[CachedIdx].BSSID,
			     MAC_ADDR_LEN))
				break;
		}

		/* Found, replace it */
		if (CachedIdx < PMKID_NO) {
			DBGPRINT(RT_DEBUG_OFF,
				 ("Update PMKID, idx = %d\n", CachedIdx));
			NdisMoveMemory(&pAd->StaCfg.SavedPMK[CachedIdx].
				       BSSID[0], pPmksa->bssid.sa_data,
				       MAC_ADDR_LEN);
			NdisMoveMemory(&pAd->StaCfg.SavedPMK[CachedIdx].
				       PMKID[0], pPmksa->pmkid, 16);
			pAd->StaCfg.SavedPMKNum++;
		}
		/* Not found, replace the last one */
		else {
			/* Randomly replace one */
			CachedIdx = (pPmksa->bssid.sa_data[5] % PMKID_NO);
			DBGPRINT(RT_DEBUG_OFF,
				 ("Update PMKID, idx = %d\n", CachedIdx));
			NdisMoveMemory(&pAd->StaCfg.SavedPMK[CachedIdx].
				       BSSID[0], pPmksa->bssid.sa_data,
				       MAC_ADDR_LEN);
			NdisMoveMemory(&pAd->StaCfg.SavedPMK[CachedIdx].
				       PMKID[0], pPmksa->pmkid, 16);
		}

		DBGPRINT(RT_DEBUG_TRACE,
			 ("rt_ioctl_siwpmksa - IW_PMKSA_ADD\n"));
		break;
	default:
		DBGPRINT(RT_DEBUG_TRACE,
			 ("rt_ioctl_siwpmksa - Unknown Command!\n"));
		break;
	}

	return 0;
}

int rt_ioctl_siwrate(struct net_device *dev,
		     struct iw_request_info *info,
		     union iwreq_data *wrqu, char *extra)
{
	struct rt_rtmp_adapter *pAd = NULL;
	u32 rate = wrqu->bitrate.value, fixed = wrqu->bitrate.fixed;

	GET_PAD_FROM_NET_DEV(pAd, dev);

	/*check if the interface is down */
	if (!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_INTERRUPT_IN_USE)) {
		DBGPRINT(RT_DEBUG_TRACE,
			 ("rt_ioctl_siwrate::Network is down!\n"));
		return -ENETDOWN;
	}

	DBGPRINT(RT_DEBUG_TRACE,
		 ("rt_ioctl_siwrate::(rate = %d, fixed = %d)\n", rate, fixed));
	/* rate = -1 => auto rate
	   rate = X, fixed = 1 => (fixed rate X)
	 */
	if (rate == -1) {
		/*Auto Rate */
		pAd->StaCfg.DesiredTransmitSetting.field.MCS = MCS_AUTO;
		pAd->StaCfg.bAutoTxRateSwitch = TRUE;
		if ((pAd->CommonCfg.PhyMode <= PHY_11G) ||
		    (pAd->MacTab.Content[BSSID_WCID].HTPhyMode.field.MODE <=
		     MODE_OFDM))
			RTMPSetDesiredRates(pAd, -1);

		SetCommonHT(pAd);
	} else {
		if (fixed) {
			pAd->StaCfg.bAutoTxRateSwitch = FALSE;
			if ((pAd->CommonCfg.PhyMode <= PHY_11G) ||
			    (pAd->MacTab.Content[BSSID_WCID].HTPhyMode.field.
			     MODE <= MODE_OFDM))
				RTMPSetDesiredRates(pAd, rate);
			else {
				pAd->StaCfg.DesiredTransmitSetting.field.MCS =
				    MCS_AUTO;
				SetCommonHT(pAd);
			}
			DBGPRINT(RT_DEBUG_TRACE,
				 ("rt_ioctl_siwrate::(HtMcs=%d)\n",
				  pAd->StaCfg.DesiredTransmitSetting.field.
				  MCS));
		} else {
			/* TODO: rate = X, fixed = 0 => (rates <= X) */
			return -EOPNOTSUPP;
		}
	}

	return 0;
}

int rt_ioctl_giwrate(struct net_device *dev,
		     struct iw_request_info *info,
		     union iwreq_data *wrqu, char *extra)
{
	struct rt_rtmp_adapter *pAd = NULL;
	int rate_index = 0, rate_count = 0;
	HTTRANSMIT_SETTING ht_setting;
/* Remove to global variable
    __s32 ralinkrate[] =
	{2,  4,   11,  22, // CCK
	12, 18,   24,  36, 48, 72, 96, 108, // OFDM
	13, 26,   39,  52,  78, 104, 117, 130, 26,  52,  78, 104, 156, 208, 234, 260, // 20MHz, 800ns GI, MCS: 0 ~ 15
	39, 78,  117, 156, 234, 312, 351, 390,										  // 20MHz, 800ns GI, MCS: 16 ~ 23
	27, 54,   81, 108, 162, 216, 243, 270, 54, 108, 162, 216, 324, 432, 486, 540, // 40MHz, 800ns GI, MCS: 0 ~ 15
	81, 162, 243, 324, 486, 648, 729, 810,										  // 40MHz, 800ns GI, MCS: 16 ~ 23
	14, 29,   43,  57,  87, 115, 130, 144, 29, 59,   87, 115, 173, 230, 260, 288, // 20MHz, 400ns GI, MCS: 0 ~ 15
	43, 87,  130, 173, 260, 317, 390, 433,										  // 20MHz, 400ns GI, MCS: 16 ~ 23
	30, 60,   90, 120, 180, 240, 270, 300, 60, 120, 180, 240, 360, 480, 540, 600, // 40MHz, 400ns GI, MCS: 0 ~ 15
	90, 180, 270, 360, 540, 720, 810, 900};										  // 40MHz, 400ns GI, MCS: 16 ~ 23
*/
	GET_PAD_FROM_NET_DEV(pAd, dev);

	rate_count = sizeof(ralinkrate) / sizeof(__s32);
	/*check if the interface is down */
	if (!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_INTERRUPT_IN_USE)) {
		DBGPRINT(RT_DEBUG_TRACE, ("INFO::Network is down!\n"));
		return -ENETDOWN;
	}

	if ((pAd->StaCfg.bAutoTxRateSwitch == FALSE) &&
	    (INFRA_ON(pAd)) &&
	    ((pAd->CommonCfg.PhyMode <= PHY_11G)
	     || (pAd->MacTab.Content[BSSID_WCID].HTPhyMode.field.MODE <=
		 MODE_OFDM)))
		ht_setting.word = pAd->StaCfg.HTPhyMode.word;
	else
		ht_setting.word =
		    pAd->MacTab.Content[BSSID_WCID].HTPhyMode.word;

	if (ht_setting.field.MODE >= MODE_HTMIX) {
/*      rate_index = 12 + ((u8)ht_setting.field.BW *16) + ((u8)ht_setting.field.ShortGI *32) + ((u8)ht_setting.field.MCS); */
		rate_index =
		    12 + ((u8)ht_setting.field.BW * 24) +
		    ((u8)ht_setting.field.ShortGI * 48) +
		    ((u8)ht_setting.field.MCS);
	} else if (ht_setting.field.MODE == MODE_OFDM)
		rate_index = (u8)(ht_setting.field.MCS) + 4;
	else if (ht_setting.field.MODE == MODE_CCK)
		rate_index = (u8)(ht_setting.field.MCS);

	if (rate_index < 0)
		rate_index = 0;

	if (rate_index > rate_count)
		rate_index = rate_count;

	wrqu->bitrate.value = ralinkrate[rate_index] * 500000;
	wrqu->bitrate.disabled = 0;

	return 0;
}

static const iw_handler rt_handler[] = {
	(iw_handler) NULL,	/* SIOCSIWCOMMIT */
	(iw_handler) rt_ioctl_giwname,	/* SIOCGIWNAME   */
	(iw_handler) NULL,	/* SIOCSIWNWID   */
	(iw_handler) NULL,	/* SIOCGIWNWID   */
	(iw_handler) rt_ioctl_siwfreq,	/* SIOCSIWFREQ   */
	(iw_handler) rt_ioctl_giwfreq,	/* SIOCGIWFREQ   */
	(iw_handler) rt_ioctl_siwmode,	/* SIOCSIWMODE   */
	(iw_handler) rt_ioctl_giwmode,	/* SIOCGIWMODE   */
	(iw_handler) NULL,	/* SIOCSIWSENS   */
	(iw_handler) NULL,	/* SIOCGIWSENS   */
	(iw_handler) NULL /* not used */ ,	/* SIOCSIWRANGE  */
	(iw_handler) rt_ioctl_giwrange,	/* SIOCGIWRANGE  */
	(iw_handler) NULL /* not used */ ,	/* SIOCSIWPRIV   */
	(iw_handler) NULL /* kernel code */ ,	/* SIOCGIWPRIV   */
	(iw_handler) NULL /* not used */ ,	/* SIOCSIWSTATS  */
	(iw_handler) rt28xx_get_wireless_stats /* kernel code */ ,	/* SIOCGIWSTATS  */
	(iw_handler) NULL,	/* SIOCSIWSPY    */
	(iw_handler) NULL,	/* SIOCGIWSPY    */
	(iw_handler) NULL,	/* SIOCSIWTHRSPY */
	(iw_handler) NULL,	/* SIOCGIWTHRSPY */
	(iw_handler) rt_ioctl_siwap,	/* SIOCSIWAP     */
	(iw_handler) rt_ioctl_giwap,	/* SIOCGIWAP     */
	(iw_handler) rt_ioctl_siwmlme,	/* SIOCSIWMLME   */
	(iw_handler) rt_ioctl_iwaplist,	/* SIOCGIWAPLIST */
	(iw_handler) rt_ioctl_siwscan,	/* SIOCSIWSCAN   */
	(iw_handler) rt_ioctl_giwscan,	/* SIOCGIWSCAN   */
	(iw_handler) rt_ioctl_siwessid,	/* SIOCSIWESSID  */
	(iw_handler) rt_ioctl_giwessid,	/* SIOCGIWESSID  */
	(iw_handler) rt_ioctl_siwnickn,	/* SIOCSIWNICKN  */
	(iw_handler) rt_ioctl_giwnickn,	/* SIOCGIWNICKN  */
	(iw_handler) NULL,	/* -- hole --    */
	(iw_handler) NULL,	/* -- hole --    */
	(iw_handler) rt_ioctl_siwrate,	/* SIOCSIWRATE   */
	(iw_handler) rt_ioctl_giwrate,	/* SIOCGIWRATE   */
	(iw_handler) rt_ioctl_siwrts,	/* SIOCSIWRTS    */
	(iw_handler) rt_ioctl_giwrts,	/* SIOCGIWRTS    */
	(iw_handler) rt_ioctl_siwfrag,	/* SIOCSIWFRAG   */
	(iw_handler) rt_ioctl_giwfrag,	/* SIOCGIWFRAG   */
	(iw_handler) NULL,	/* SIOCSIWTXPOW  */
	(iw_handler) NULL,	/* SIOCGIWTXPOW  */
	(iw_handler) NULL,	/* SIOCSIWRETRY  */
	(iw_handler) NULL,	/* SIOCGIWRETRY  */
	(iw_handler) rt_ioctl_siwencode,	/* SIOCSIWENCODE */
	(iw_handler) rt_ioctl_giwencode,	/* SIOCGIWENCODE */
	(iw_handler) NULL,	/* SIOCSIWPOWER  */
	(iw_handler) NULL,	/* SIOCGIWPOWER  */
	(iw_handler) NULL,	/* -- hole -- */
	(iw_handler) NULL,	/* -- hole -- */
	(iw_handler) rt_ioctl_siwgenie,	/* SIOCSIWGENIE  */
	(iw_handler) rt_ioctl_giwgenie,	/* SIOCGIWGENIE  */
	(iw_handler) rt_ioctl_siwauth,	/* SIOCSIWAUTH   */
	(iw_handler) rt_ioctl_giwauth,	/* SIOCGIWAUTH   */
	(iw_handler) rt_ioctl_siwencodeext,	/* SIOCSIWENCODEEXT */
	(iw_handler) rt_ioctl_giwencodeext,	/* SIOCGIWENCODEEXT */
	(iw_handler) rt_ioctl_siwpmksa,	/* SIOCSIWPMKSA  */
};

const struct iw_handler_def rt28xx_iw_handler_def = {
	.standard = (iw_handler *) rt_handler,
	.num_standard = sizeof(rt_handler) / sizeof(iw_handler),
#if IW_HANDLER_VERSION >= 7
	.get_wireless_stats = rt28xx_get_wireless_stats,
#endif
};

int rt28xx_sta_ioctl(IN struct net_device *net_dev,
		     IN OUT struct ifreq *rq, int cmd)
{
	struct os_cookie *pObj;
	struct rt_rtmp_adapter *pAd = NULL;
	struct iwreq *wrq = (struct iwreq *)rq;
	BOOLEAN StateMachineTouched = FALSE;
	int Status = NDIS_STATUS_SUCCESS;

	GET_PAD_FROM_NET_DEV(pAd, net_dev);

	pObj = (struct os_cookie *)pAd->OS_Cookie;

	/*check if the interface is down */
	if (!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_INTERRUPT_IN_USE)) {
		{
			DBGPRINT(RT_DEBUG_TRACE, ("INFO::Network is down!\n"));
			return -ENETDOWN;
		}
	}

	{			/* determine this ioctl command is comming from which interface. */
		pObj->ioctl_if_type = INT_MAIN;
		pObj->ioctl_if = MAIN_MBSSID;
	}

	switch (cmd) {
	case SIOCGIFHWADDR:
		DBGPRINT(RT_DEBUG_TRACE, ("IOCTL::SIOCGIFHWADDR\n"));
		memcpy(wrq->u.name, pAd->CurrentAddress, ETH_ALEN);
		break;
	case SIOCGIWNAME:
		{
			char *name = &wrq->u.name[0];
			rt_ioctl_giwname(net_dev, NULL, name, NULL);
			break;
		}
	case SIOCGIWESSID:	/*Get ESSID */
		{
			struct iw_point *essid = &wrq->u.essid;
			rt_ioctl_giwessid(net_dev, NULL, essid, essid->pointer);
			break;
		}
	case SIOCSIWESSID:	/*Set ESSID */
		{
			struct iw_point *essid = &wrq->u.essid;
			rt_ioctl_siwessid(net_dev, NULL, essid, essid->pointer);
			break;
		}
	case SIOCSIWNWID:	/* set network id (the cell) */
	case SIOCGIWNWID:	/* get network id */
		Status = -EOPNOTSUPP;
		break;
	case SIOCSIWFREQ:	/*set channel/frequency (Hz) */
		{
			struct iw_freq *freq = &wrq->u.freq;
			rt_ioctl_siwfreq(net_dev, NULL, freq, NULL);
			break;
		}
	case SIOCGIWFREQ:	/* get channel/frequency (Hz) */
		{
			struct iw_freq *freq = &wrq->u.freq;
			rt_ioctl_giwfreq(net_dev, NULL, freq, NULL);
			break;
		}
	case SIOCSIWNICKN:	/*set node name/nickname */
		{
			/*struct iw_point *data=&wrq->u.data; */
			/*rt_ioctl_siwnickn(net_dev, NULL, data, NULL); */
			break;
		}
	case SIOCGIWNICKN:	/*get node name/nickname */
		{
			struct iw_point *erq = NULL;
			erq = &wrq->u.data;
			erq->length = strlen((char *)pAd->nickname);
			Status =
			    copy_to_user(erq->pointer, pAd->nickname,
					 erq->length);
			break;
		}
	case SIOCGIWRATE:	/*get default bit rate (bps) */
		rt_ioctl_giwrate(net_dev, NULL, &wrq->u, NULL);
		break;
	case SIOCSIWRATE:	/*set default bit rate (bps) */
		rt_ioctl_siwrate(net_dev, NULL, &wrq->u, NULL);
		break;
	case SIOCGIWRTS:	/* get RTS/CTS threshold (bytes) */
		{
			struct iw_param *rts = &wrq->u.rts;
			rt_ioctl_giwrts(net_dev, NULL, rts, NULL);
			break;
		}
	case SIOCSIWRTS:	/*set RTS/CTS threshold (bytes) */
		{
			struct iw_param *rts = &wrq->u.rts;
			rt_ioctl_siwrts(net_dev, NULL, rts, NULL);
			break;
		}
	case SIOCGIWFRAG:	/*get fragmentation thr (bytes) */
		{
			struct iw_param *frag = &wrq->u.frag;
			rt_ioctl_giwfrag(net_dev, NULL, frag, NULL);
			break;
		}
	case SIOCSIWFRAG:	/*set fragmentation thr (bytes) */
		{
			struct iw_param *frag = &wrq->u.frag;
			rt_ioctl_siwfrag(net_dev, NULL, frag, NULL);
			break;
		}
	case SIOCGIWENCODE:	/*get encoding token & mode */
		{
			struct iw_point *erq = &wrq->u.encoding;
			if (erq)
				rt_ioctl_giwencode(net_dev, NULL, erq,
						   erq->pointer);
			break;
		}
	case SIOCSIWENCODE:	/*set encoding token & mode */
		{
			struct iw_point *erq = &wrq->u.encoding;
			if (erq)
				rt_ioctl_siwencode(net_dev, NULL, erq,
						   erq->pointer);
			break;
		}
	case SIOCGIWAP:	/*get access point MAC addresses */
		{
			struct sockaddr *ap_addr = &wrq->u.ap_addr;
			rt_ioctl_giwap(net_dev, NULL, ap_addr,
				       ap_addr->sa_data);
			break;
		}
	case SIOCSIWAP:	/*set access point MAC addresses */
		{
			struct sockaddr *ap_addr = &wrq->u.ap_addr;
			rt_ioctl_siwap(net_dev, NULL, ap_addr,
				       ap_addr->sa_data);
			break;
		}
	case SIOCGIWMODE:	/*get operation mode */
		{
			__u32 *mode = &wrq->u.mode;
			rt_ioctl_giwmode(net_dev, NULL, mode, NULL);
			break;
		}
	case SIOCSIWMODE:	/*set operation mode */
		{
			__u32 *mode = &wrq->u.mode;
			rt_ioctl_siwmode(net_dev, NULL, mode, NULL);
			break;
		}
	case SIOCGIWSENS:	/*get sensitivity (dBm) */
	case SIOCSIWSENS:	/*set sensitivity (dBm) */
	case SIOCGIWPOWER:	/*get Power Management settings */
	case SIOCSIWPOWER:	/*set Power Management settings */
	case SIOCGIWTXPOW:	/*get transmit power (dBm) */
	case SIOCSIWTXPOW:	/*set transmit power (dBm) */
	case SIOCGIWRANGE:	/*Get range of parameters */
	case SIOCGIWRETRY:	/*get retry limits and lifetime */
	case SIOCSIWRETRY:	/*set retry limits and lifetime */
	case RT_PRIV_IOCTL:
	case RT_PRIV_IOCTL_EXT:
	case RTPRIV_IOCTL_SET:
	case RTPRIV_IOCTL_GSITESURVEY:
	case SIOCGIWPRIV:
		Status = -EOPNOTSUPP;
		break;
	case SIOCETHTOOL:
		break;
	default:
		DBGPRINT(RT_DEBUG_ERROR,
			 ("IOCTL::unknown IOCTL's cmd = 0x%08x\n", cmd));
		Status = -EOPNOTSUPP;
		break;
	}

	if (StateMachineTouched)	/* Upper layer sent a MLME-related operations */
		RTMP_MLME_HANDLER(pAd);

	return Status;
}

/*
    ==========================================================================
    Description:
        Set SSID
    Return:
        TRUE if all parameters are OK, FALSE otherwise
    ==========================================================================
*/
int Set_SSID_Proc(struct rt_rtmp_adapter *pAdapter, char *arg)
{
	struct rt_ndis_802_11_ssid Ssid, *pSsid = NULL;
	BOOLEAN StateMachineTouched = FALSE;
	int success = TRUE;

	if (strlen(arg) <= MAX_LEN_OF_SSID) {
		NdisZeroMemory(&Ssid, sizeof(struct rt_ndis_802_11_ssid));
		if (strlen(arg) != 0) {
			NdisMoveMemory(Ssid.Ssid, arg, strlen(arg));
			Ssid.SsidLength = strlen(arg);
		} else		/*ANY ssid */
		{
			Ssid.SsidLength = 0;
			memcpy(Ssid.Ssid, "", 0);
			pAdapter->StaCfg.BssType = BSS_INFRA;
			pAdapter->StaCfg.AuthMode = Ndis802_11AuthModeOpen;
			pAdapter->StaCfg.WepStatus =
			    Ndis802_11EncryptionDisabled;
		}
		pSsid = &Ssid;

		if (pAdapter->Mlme.CntlMachine.CurrState != CNTL_IDLE) {
			RTMP_MLME_RESET_STATE_MACHINE(pAdapter);
			DBGPRINT(RT_DEBUG_TRACE,
				 ("MLME busy, reset MLME state machine!\n"));
		}

		if ((pAdapter->StaCfg.WpaPassPhraseLen >= 8) &&
		    (pAdapter->StaCfg.WpaPassPhraseLen <= 64)) {
			char passphrase_str[65] = { 0 };
			u8 keyMaterial[40];

			RTMPMoveMemory(passphrase_str,
				       pAdapter->StaCfg.WpaPassPhrase,
				       pAdapter->StaCfg.WpaPassPhraseLen);
			RTMPZeroMemory(pAdapter->StaCfg.PMK, 32);
			if (pAdapter->StaCfg.WpaPassPhraseLen == 64) {
				AtoH((char *)pAdapter->StaCfg.WpaPassPhrase,
				     pAdapter->StaCfg.PMK, 32);
			} else {
				PasswordHash((char *)pAdapter->StaCfg.
					     WpaPassPhrase, Ssid.Ssid,
					     Ssid.SsidLength, keyMaterial);
				NdisMoveMemory(pAdapter->StaCfg.PMK,
					       keyMaterial, 32);
			}
		}

		pAdapter->MlmeAux.CurrReqIsFromNdis = TRUE;
		pAdapter->StaCfg.bScanReqIsFromWebUI = FALSE;
		pAdapter->bConfigChanged = TRUE;

		MlmeEnqueue(pAdapter,
			    MLME_CNTL_STATE_MACHINE,
			    OID_802_11_SSID,
			    sizeof(struct rt_ndis_802_11_ssid), (void *) pSsid);

		StateMachineTouched = TRUE;
		DBGPRINT(RT_DEBUG_TRACE,
			 ("Set_SSID_Proc::(Len=%d,Ssid=%s)\n", Ssid.SsidLength,
			  Ssid.Ssid));
	} else
		success = FALSE;

	if (StateMachineTouched)	/* Upper layer sent a MLME-related operations */
		RTMP_MLME_HANDLER(pAdapter);

	return success;
}

/*
    ==========================================================================
    Description:
        Set Network Type(Infrastructure/Adhoc mode)
    Return:
        TRUE if all parameters are OK, FALSE otherwise
    ==========================================================================
*/
int Set_NetworkType_Proc(struct rt_rtmp_adapter *pAdapter, char *arg)
{
	u32 Value = 0;

	if (strcmp(arg, "Adhoc") == 0) {
		if (pAdapter->StaCfg.BssType != BSS_ADHOC) {
			/* Config has changed */
			pAdapter->bConfigChanged = TRUE;
			if (MONITOR_ON(pAdapter)) {
				RTMP_IO_WRITE32(pAdapter, RX_FILTR_CFG,
						STANORMAL);
				RTMP_IO_READ32(pAdapter, MAC_SYS_CTRL, &Value);
				Value &= (~0x80);
				RTMP_IO_WRITE32(pAdapter, MAC_SYS_CTRL, Value);
				OPSTATUS_CLEAR_FLAG(pAdapter,
						    fOP_STATUS_MEDIA_STATE_CONNECTED);
				pAdapter->StaCfg.bAutoReconnect = TRUE;
				LinkDown(pAdapter, FALSE);
			}
			if (INFRA_ON(pAdapter)) {
				/*BOOLEAN Cancelled; */
				/* Set the AutoReconnectSsid to prevent it reconnect to old SSID */
				/* Since calling this indicate user don't want to connect to that SSID anymore. */
				pAdapter->MlmeAux.AutoReconnectSsidLen = 32;
				NdisZeroMemory(pAdapter->MlmeAux.
					       AutoReconnectSsid,
					       pAdapter->MlmeAux.
					       AutoReconnectSsidLen);

				LinkDown(pAdapter, FALSE);

				DBGPRINT(RT_DEBUG_TRACE,
					 ("NDIS_STATUS_MEDIA_DISCONNECT Event BB!\n"));
			}
		}
		pAdapter->StaCfg.BssType = BSS_ADHOC;
		pAdapter->net_dev->type = pAdapter->StaCfg.OriDevType;
		DBGPRINT(RT_DEBUG_TRACE,
			 ("===>Set_NetworkType_Proc::(AD-HOC)\n"));
	} else if (strcmp(arg, "Infra") == 0) {
		if (pAdapter->StaCfg.BssType != BSS_INFRA) {
			/* Config has changed */
			pAdapter->bConfigChanged = TRUE;
			if (MONITOR_ON(pAdapter)) {
				RTMP_IO_WRITE32(pAdapter, RX_FILTR_CFG,
						STANORMAL);
				RTMP_IO_READ32(pAdapter, MAC_SYS_CTRL, &Value);
				Value &= (~0x80);
				RTMP_IO_WRITE32(pAdapter, MAC_SYS_CTRL, Value);
				OPSTATUS_CLEAR_FLAG(pAdapter,
						    fOP_STATUS_MEDIA_STATE_CONNECTED);
				pAdapter->StaCfg.bAutoReconnect = TRUE;
				LinkDown(pAdapter, FALSE);
			}
			if (ADHOC_ON(pAdapter)) {
				/* Set the AutoReconnectSsid to prevent it reconnect to old SSID */
				/* Since calling this indicate user don't want to connect to that SSID anymore. */
				pAdapter->MlmeAux.AutoReconnectSsidLen = 32;
				NdisZeroMemory(pAdapter->MlmeAux.
					       AutoReconnectSsid,
					       pAdapter->MlmeAux.
					       AutoReconnectSsidLen);

				LinkDown(pAdapter, FALSE);
			}
		}
		pAdapter->StaCfg.BssType = BSS_INFRA;
		pAdapter->net_dev->type = pAdapter->StaCfg.OriDevType;
		DBGPRINT(RT_DEBUG_TRACE,
			 ("===>Set_NetworkType_Proc::(INFRA)\n"));
	} else if (strcmp(arg, "Monitor") == 0) {
		u8 bbpValue = 0;
		BCN_TIME_CFG_STRUC csr;
		OPSTATUS_CLEAR_FLAG(pAdapter, fOP_STATUS_INFRA_ON);
		OPSTATUS_CLEAR_FLAG(pAdapter, fOP_STATUS_ADHOC_ON);
		OPSTATUS_SET_FLAG(pAdapter, fOP_STATUS_MEDIA_STATE_CONNECTED);
		/* disable all periodic state machine */
		pAdapter->StaCfg.bAutoReconnect = FALSE;
		/* reset all mlme state machine */
		RTMP_MLME_RESET_STATE_MACHINE(pAdapter);
		DBGPRINT(RT_DEBUG_TRACE,
			 ("fOP_STATUS_MEDIA_STATE_CONNECTED \n"));
		if (pAdapter->CommonCfg.CentralChannel == 0) {
			if (pAdapter->CommonCfg.PhyMode == PHY_11AN_MIXED)
				pAdapter->CommonCfg.CentralChannel = 36;
			else
				pAdapter->CommonCfg.CentralChannel = 6;
		} else
			N_ChannelCheck(pAdapter);

		if (pAdapter->CommonCfg.PhyMode >= PHY_11ABGN_MIXED &&
		    pAdapter->CommonCfg.RegTransmitSetting.field.BW == BW_40 &&
		    pAdapter->CommonCfg.RegTransmitSetting.field.EXTCHA ==
		    EXTCHA_ABOVE) {
			/* 40MHz ,control channel at lower */
			RTMP_BBP_IO_READ8_BY_REG_ID(pAdapter, BBP_R4,
						    &bbpValue);
			bbpValue &= (~0x18);
			bbpValue |= 0x10;
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAdapter, BBP_R4,
						     bbpValue);
			pAdapter->CommonCfg.BBPCurrentBW = BW_40;
			/*  RX : control channel at lower */
			RTMP_BBP_IO_READ8_BY_REG_ID(pAdapter, BBP_R3,
						    &bbpValue);
			bbpValue &= (~0x20);
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAdapter, BBP_R3,
						     bbpValue);

			RTMP_IO_READ32(pAdapter, TX_BAND_CFG, &Value);
			Value &= 0xfffffffe;
			RTMP_IO_WRITE32(pAdapter, TX_BAND_CFG, Value);
			pAdapter->CommonCfg.CentralChannel =
			    pAdapter->CommonCfg.Channel + 2;
			AsicSwitchChannel(pAdapter,
					  pAdapter->CommonCfg.CentralChannel,
					  FALSE);
			AsicLockChannel(pAdapter,
					pAdapter->CommonCfg.CentralChannel);
			DBGPRINT(RT_DEBUG_TRACE,
				 ("BW_40 ,control_channel(%d), CentralChannel(%d) \n",
				  pAdapter->CommonCfg.Channel,
				  pAdapter->CommonCfg.CentralChannel));
		} else if (pAdapter->CommonCfg.PhyMode >= PHY_11ABGN_MIXED
			   && pAdapter->CommonCfg.RegTransmitSetting.field.BW ==
			   BW_40
			   && pAdapter->CommonCfg.RegTransmitSetting.field.
			   EXTCHA == EXTCHA_BELOW) {
			/* 40MHz ,control channel at upper */
			RTMP_BBP_IO_READ8_BY_REG_ID(pAdapter, BBP_R4,
						    &bbpValue);
			bbpValue &= (~0x18);
			bbpValue |= 0x10;
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAdapter, BBP_R4,
						     bbpValue);
			pAdapter->CommonCfg.BBPCurrentBW = BW_40;
			RTMP_IO_READ32(pAdapter, TX_BAND_CFG, &Value);
			Value |= 0x1;
			RTMP_IO_WRITE32(pAdapter, TX_BAND_CFG, Value);

			RTMP_BBP_IO_READ8_BY_REG_ID(pAdapter, BBP_R3,
						    &bbpValue);
			bbpValue |= (0x20);
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAdapter, BBP_R3,
						     bbpValue);
			pAdapter->CommonCfg.CentralChannel =
			    pAdapter->CommonCfg.Channel - 2;
			AsicSwitchChannel(pAdapter,
					  pAdapter->CommonCfg.CentralChannel,
					  FALSE);
			AsicLockChannel(pAdapter,
					pAdapter->CommonCfg.CentralChannel);
			DBGPRINT(RT_DEBUG_TRACE,
				 ("BW_40 ,control_channel(%d), CentralChannel(%d) \n",
				  pAdapter->CommonCfg.Channel,
				  pAdapter->CommonCfg.CentralChannel));
		} else {
			/* 20MHz */
			RTMP_BBP_IO_READ8_BY_REG_ID(pAdapter, BBP_R4,
						    &bbpValue);
			bbpValue &= (~0x18);
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAdapter, BBP_R4,
						     bbpValue);
			pAdapter->CommonCfg.BBPCurrentBW = BW_20;
			AsicSwitchChannel(pAdapter, pAdapter->CommonCfg.Channel,
					  FALSE);
			AsicLockChannel(pAdapter, pAdapter->CommonCfg.Channel);
			DBGPRINT(RT_DEBUG_TRACE,
				 ("BW_20, Channel(%d)\n",
				  pAdapter->CommonCfg.Channel));
		}
		/* Enable Rx with promiscuous reception */
		RTMP_IO_WRITE32(pAdapter, RX_FILTR_CFG, 0x3);
		/* ASIC supporsts sniffer function with replacing RSSI with timestamp. */
		/*RTMP_IO_READ32(pAdapter, MAC_SYS_CTRL, &Value); */
		/*Value |= (0x80); */
		/*RTMP_IO_WRITE32(pAdapter, MAC_SYS_CTRL, Value); */
		/* disable sync */
		RTMP_IO_READ32(pAdapter, BCN_TIME_CFG, &csr.word);
		csr.field.bBeaconGen = 0;
		csr.field.bTBTTEnable = 0;
		csr.field.TsfSyncMode = 0;
		RTMP_IO_WRITE32(pAdapter, BCN_TIME_CFG, csr.word);

		pAdapter->StaCfg.BssType = BSS_MONITOR;
		pAdapter->net_dev->type = ARPHRD_IEEE80211_PRISM;	/*ARPHRD_IEEE80211; // IEEE80211 */
		DBGPRINT(RT_DEBUG_TRACE,
			 ("===>Set_NetworkType_Proc::(MONITOR)\n"));
	}
	/* Reset Ralink supplicant to not use, it will be set to start when UI set PMK key */
	pAdapter->StaCfg.WpaState = SS_NOTUSE;

	DBGPRINT(RT_DEBUG_TRACE,
		 ("Set_NetworkType_Proc::(NetworkType=%d)\n",
		  pAdapter->StaCfg.BssType));

	return TRUE;
}
