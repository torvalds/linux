/** @file pmkcache.c
 *
 *  @brief This file defines pmk cache functions
 *
 * Copyright (C) 2014-2017, Marvell International Ltd.
 *
 * This software file (the "File") is distributed by Marvell International
 * Ltd. under the terms of the GNU General Public License Version 2, June 1991
 * (the "License").  You may use, redistribute and/or modify this File in
 * accordance with the terms and conditions of the License, a copy of which
 * is available by writing to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA or on the
 * worldwide web at http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt.
 *
 * THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
 * ARE EXPRESSLY DISCLAIMED.  The License provides additional details about
 * this warranty disclaimer.
 */

/******************************************************
Change log:
    03/07/2014: Initial version
******************************************************/
#include "wl_macros.h"
#include "pass_phrase.h"
#include "pmkCache.h"
#include "hostsa_ext_def.h"
#include "authenticator.h"
#include "tlv.h"
#include "keyMgmtApStaCommon.h"

#define MAX_PMK_CACHE_ENTRIES 10

pmkElement_t pmkCache[MAX_PMK_CACHE_ENTRIES];

char PSKPassPhrase[PSK_PASS_PHRASE_LEN_MAX];
/*
**  Replacement rank is used to determine which cache entry to replace
**  once the cache is full.  The rank order is determined by usage. The
**  least recently used cache element is the first to be replaced.
**
**  replacementRankMax is an indication of the number of cache entries used.
**  It is used to  determine the rank of the current cache entry used.
**
**  Rank order goes from 1 to MAX_PMK_CACHE_ENTRIES. If the cache is full,
**  the element with rank 1 is first to be replaced.
**
**  Replacement rank of zero indicates that the entry is invalid.
*/

UINT8 *
pmkCacheFindPSK(void *priv, UINT8 *pSsid, UINT8 ssidLen)
{
	UINT8 *pPMK = NULL;
	pmkElement_t *pPMKElement;

	if (!pPMK) {
		/* extract the PSK from the cache entry */
		pPMKElement =
			pmkCacheFindPSKElement((void *)priv, pSsid, ssidLen);
		if (pPMKElement) {
			pPMK = pPMKElement->PMK;
		} else if ('\0' != PSKPassPhrase[0]) {
			/* Generate a new PSK entry with the
			 ** provided passphrase.
			 */
			pmkCacheAddPSK((void *)priv, pSsid, ssidLen, NULL,
				       PSKPassPhrase);
			pPMKElement =
				pmkCacheFindPSKElement((void *)priv, pSsid,
						       ssidLen);
			pmkCacheGeneratePSK((void *)priv, pSsid, ssidLen,
					    PSKPassPhrase, pPMKElement->PMK);
			pPMK = pPMKElement->PMK;
		}
	}

	return pPMK;
}

UINT8 *
pmkCacheFindPassphrase(void *priv, UINT8 *pSsid, UINT8 ssidLen)
{
	UINT8 *pPassphrase = NULL;
	pmkElement_t *pPMKElement;

	if (!pPassphrase) {
		/* extract the PSK from the cache entry */
		pPMKElement =
			pmkCacheFindPSKElement((void *)priv, pSsid, ssidLen);
		if (pPMKElement) {
			pPassphrase = pPMKElement->passphrase;
		}

	}

	return pPassphrase;
}

void
pmkCacheSetPassphrase(void *priv, char *pPassphrase)
{
	phostsa_private psapriv = (phostsa_private)priv;
	hostsa_util_fns *util_fns = &psapriv->util_fns;

	if (pPassphrase != NULL) {
		memcpy(util_fns, PSKPassPhrase,
		       pPassphrase, sizeof(PSKPassPhrase));
	}
}

void
pmkCacheGetPassphrase(void *priv, char *pPassphrase)
{
	phostsa_private psapriv = (phostsa_private)priv;
	hostsa_util_fns *util_fns = &psapriv->util_fns;

	if (pPassphrase != NULL) {
		memcpy(util_fns, pPassphrase,
		       PSKPassPhrase, sizeof(PSKPassPhrase));
	}
}

void
pmkCacheInit(void *priv)
{
	phostsa_private psapriv = (phostsa_private)priv;
	hostsa_util_fns *util_fns = &psapriv->util_fns;

	memset(util_fns, pmkCache, 0x00, sizeof(pmkCache));
	memset(util_fns, PSKPassPhrase, 0x00, sizeof(PSKPassPhrase));
	replacementRankMax = 0;
}

void
pmkCacheFlush(void *priv)
{
	phostsa_private psapriv = (phostsa_private)priv;
	hostsa_util_fns *util_fns = &psapriv->util_fns;

	memset(util_fns, pmkCache, 0x00, sizeof(pmkCache));
	replacementRankMax = 0;
}

//#pragma arm section code = ".init"
void
pmkCacheRomInit(void)
{
	ramHook_MAX_PMK_CACHE_ENTRIES = MAX_PMK_CACHE_ENTRIES;
	ramHook_pmkCache = &pmkCache[0];
	ramHook_PSKPassPhrase = &PSKPassPhrase[0];
//    ramHook_hal_SetCpuMaxSpeed    = hal_SetCpuOpToSecuritySpeed;
//    ramHook_hal_RestoreCpuSpeed   = cm_SetPerformanceParams;
}

//#pragma arm section code

#ifdef DRV_EMBEDDED_SUPPLICANT

t_u16
SupplicantSetPassphrase(void *priv, void *pPassphraseBuf)
{
	phostsa_private psapriv = (phostsa_private)priv;
	hostsa_util_fns *util_fns = NULL;
	mlan_ds_passphrase *psk = (mlan_ds_passphrase *)pPassphraseBuf;
	IEEEtypes_MacAddr_t *pBssid = NULL;
	UINT8 *pPMK = NULL;
	UINT8 Passphrase[PSK_PASS_PHRASE_LEN_MAX], *pPassphrase = NULL;
	UINT8 *pSsid = NULL;
	UINT8 ssidLen = 0;
	UINT16 retVal = 0;
	t_u8 zero_mac[] = { 0, 0, 0, 0, 0, 0 };

	if (!psapriv)
		return retVal;
	util_fns = &psapriv->util_fns;

	if (memcmp(util_fns, (t_u8 *)&psk->bssid, zero_mac, sizeof(zero_mac)))
		pBssid = (IEEEtypes_MacAddr_t *)&psk->bssid;

	ssidLen = psk->ssid.ssid_len;
	if (ssidLen > 0)
		pSsid = psk->ssid.ssid;

	if (psk->psk_type == MLAN_PSK_PASSPHRASE) {
		pPassphrase = psk->psk.passphrase.passphrase;
		memset(util_fns, Passphrase, 0x00, sizeof(Passphrase));
		memcpy(util_fns, Passphrase, pPassphrase,
		       MIN(MLAN_MAX_PASSPHRASE_LENGTH,
			   psk->psk.passphrase.passphrase_len));
	}

	if (psk->psk_type == MLAN_PSK_PMK)
		pPMK = psk->psk.pmk.pmk;

	/* Always enable the supplicant on a set */
	// supplicantEnable(priv);

	if (pBssid && pPMK) {
		pmkCacheAddPMK(priv, pBssid, pPMK);
	} else if (pSsid) {
		if (pPMK) {
			pmkCacheAddPSK(priv, pSsid, ssidLen, pPMK, NULL);
		} else if (pPassphrase) {
			pmkCacheAddPSK(priv, pSsid, ssidLen, NULL, Passphrase);
			pPMK = pmkCacheFindPSK(priv, pSsid, ssidLen);
			pmkCacheGeneratePSK(priv, pSsid, ssidLen,
					    (char *)Passphrase, pPMK);
		} else {
			/* Just an SSID so we can't set anything in the cache */
			retVal = 1;
		}
	} else if (pPassphrase) {
		memcpy(util_fns, PSKPassPhrase, Passphrase, sizeof(Passphrase));
	} else {
		/* Not enough data to set anything in the cache */
		retVal = 1;
	}

	return retVal;
}

BOOLEAN
SupplicantClearPMK_internal(void *priv, void *pPassphraseBuf)
{
	phostsa_private psapriv = (phostsa_private)priv;
	hostsa_util_fns *util_fns = &psapriv->util_fns;
	mlan_ds_passphrase *psk = (mlan_ds_passphrase *)pPassphraseBuf;
	IEEEtypes_MacAddr_t *pBssid = NULL;
	UINT8 *pPassphrase = NULL;
	UINT8 *pSsid = NULL;
	UINT8 ssidLen = 0;
	t_u8 zero_mac[] = { 0, 0, 0, 0, 0, 0 };

	if (memcmp(util_fns, (t_u8 *)&psk->bssid, zero_mac, sizeof(zero_mac)))
		pBssid = (IEEEtypes_MacAddr_t *)&psk->bssid;

	ssidLen = psk->ssid.ssid_len;
	if (ssidLen > 0)
		pSsid = psk->ssid.ssid;

	if (psk->psk_type == MLAN_PSK_PASSPHRASE)
		pPassphrase = psk->psk.passphrase.passphrase;

	if (pBssid) {
		pmkCacheDeletePMK(priv, (UINT8 *)pBssid);
	} else if (pSsid) {
		pmkCacheDeletePSK(priv, pSsid, ssidLen);
	} else if (pPassphrase) {
		/* Clear the global passphrase by setting it to blank */
		memset(util_fns, ramHook_PSKPassPhrase, 0x00,
		       PSK_PASS_PHRASE_LEN_MAX);
	} else {
		return FALSE;
	}

	return TRUE;
}

void
SupplicantClearPMK(void *priv, void *pPassphrase)
{
	if (!priv)
		return;

	if (!SupplicantClearPMK_internal(priv, pPassphrase)) {
		/* Always disable the supplicant on a flush */
		supplicantDisable(priv);
		pmkCacheFlush(priv);
	}
}

void
SupplicantQueryPassphrase(void *priv, void *pPassphraseBuf)
{
	phostsa_private psapriv = (phostsa_private)priv;
	hostsa_util_fns *util_fns = NULL;
	mlan_ds_passphrase *psk = (mlan_ds_passphrase *)pPassphraseBuf;
	UINT8 *pPassphrase = NULL;
	UINT8 *pSsid = NULL;
	UINT8 ssidLen = 0;

	if (!psapriv)
		return;

	util_fns = &psapriv->util_fns;
	ssidLen = psk->ssid.ssid_len;
	pSsid = psk->ssid.ssid;

	if (ssidLen) {
		pPassphrase = pmkCacheFindPassphrase(priv, pSsid, ssidLen);
		if (pPassphrase) {
			psk->psk_type = MLAN_PSK_PASSPHRASE;
			memcpy(util_fns, psk->psk.passphrase.passphrase,
			       pPassphrase, PSK_PASS_PHRASE_LEN_MAX);
			psk->psk.passphrase.passphrase_len =
				wlan_strlen(pPassphrase);
		}
	}

}
#endif
