/** @file pmkcache_rom.c
 *
 *  @brief This file defines function for pmk cache
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
#include "wltypes.h"
#include "pass_phrase.h"
#include "pmkCache_rom.h"

#include "hostsa_ext_def.h"
#include "authenticator.h"

#define BSSID_FLAG  0xff

SINT8 replacementRankMax;

SINT32 ramHook_MAX_PMK_CACHE_ENTRIES;

pmkElement_t *ramHook_pmkCache;
char *ramHook_PSKPassPhrase;

//void (*ramHook_hal_SetCpuMaxSpeed)(void);
//void (*ramHook_hal_RestoreCpuSpeed)(void);

/*!
** \brief      creates a new PMK cache entry with given SSID.
** \param      pSsid pointer to desired SSID.
** \param      ssidLen length of the desired SSID string.
** \return     pointer to newly created PMK cache entry,
**             NULL if PMK cache is full.
*/
pmkElement_t *
pmkCacheNewElement(void *priv)
{
	phostsa_private psapriv = (phostsa_private)priv;
	hostsa_util_fns *util_fns = &psapriv->util_fns;
	UINT8 index;
	pmkElement_t *pPMK = NULL;

#if 0				//!defined(REMOVE_PATCH_HOOKS)
	if (pmkCacheNewElement_hook(&pPMK)) {
		return pPMK;
	}
#endif

	for (index = 0; index < ramHook_MAX_PMK_CACHE_ENTRIES; index++) {
		/* If the cache is full the least recently used entry
		 ** will be replaced.  Decrement all the replacement ranks
		 ** to have a free cache entry.
		 */
		if (ramHook_MAX_PMK_CACHE_ENTRIES == replacementRankMax) {
			(ramHook_pmkCache[index].replacementRank)--;
		}

		/* Either the entry is free or it can be replaced */
		if (NULL == pPMK &&
		    0 == ramHook_pmkCache[index].replacementRank) {
			/* empty entry found */
			pPMK = &ramHook_pmkCache[index];

			/* clear the entry in case this is a replacement */
			memset(util_fns, pPMK, 0x00, sizeof(pmkElement_t));

			if (ramHook_MAX_PMK_CACHE_ENTRIES > replacementRankMax) {
				/* Cache isn't full so increment the max possible rank */
				replacementRankMax++;
			}

			/* Set the rank so it is the last to be replaced */
			ramHook_pmkCache[index].replacementRank =
				replacementRankMax;
		}
	}

	return pPMK;
}

void
pmkCacheUpdateReplacementRank(pmkElement_t *pPMKElement)
{
	UINT8 index;

	/* Update the replacementRank field if the PMK is found */
	if (pPMKElement && pPMKElement->replacementRank != replacementRankMax) {
		/*
		 ** The cache entry with a larger rank value needs to
		 ** to be adjusted.  The cache entry given will have the
		 ** largest rank value
		 */
		for (index = 0; index < ramHook_MAX_PMK_CACHE_ENTRIES; index++) {
			if (ramHook_pmkCache[index].replacementRank
			    > pPMKElement->replacementRank) {
				(ramHook_pmkCache[index].replacementRank)--;
			}
		}

		pPMKElement->replacementRank = replacementRankMax;
	}
}

/*!
** \brief      Finds a PMK entry matching given BSSID
** \param      pBssid pointer to the desired BSSID
** \return     pointer to key data field of the matching PMK cache entry.
**             NULL, if no matching PMK entry is found
*/
pmkElement_t *
pmkCacheFindPMKElement(void *priv, IEEEtypes_MacAddr_t *pBssid)
{
	phostsa_private psapriv = (phostsa_private)priv;
	hostsa_util_fns *util_fns = &psapriv->util_fns;
	UINT8 index = 0;
	pmkElement_t *pPMKElement = NULL;

#if 0				//!defined(REMOVE_PATCH_HOOKS)
	if (pmkCacheFindPMKElement_hook(pBssid, &pPMKElement)) {
		return pPMKElement;
	}
#endif

	for (index = 0; index < ramHook_MAX_PMK_CACHE_ENTRIES; index++) {
		/* See if the entry is valid.
		 ** See if the entry is a PMK
		 ** See if the BSSID matches
		 */
		if (ramHook_pmkCache[index].replacementRank > 0
		    && ramHook_pmkCache[index].length == BSSID_FLAG
		    && (0 == memcmp(util_fns, ramHook_pmkCache[index].key.Bssid,
				    pBssid,
				    sizeof(ramHook_pmkCache[index].key.Bssid))))
		{
			pPMKElement = (ramHook_pmkCache + index);
		}
	}

	/* Update the rank if an entry is found.  Null is an accepted
	 ** input for the function
	 */
	pmkCacheUpdateReplacementRank(pPMKElement);

	return pPMKElement;
}

/*!
** \brief      If a matching SSID entry is present in the PMK Cache, returns a
**             pointer to its key field.
** \param      pSsid pointer to string containing desired SSID.
** \param      ssidLen length of the SSID string *pSsid.
** \exception  Does not handle the case when multiple matching SSID entries are
**             found. Returns the first match.
** \return     pointer to pmkElement with matching SSID entry from PMK cache,
**             NULL if no matching entry found.
*/
pmkElement_t *
pmkCacheFindPSKElement(void *priv, UINT8 *pSsid, UINT8 ssidLen)
{
	phostsa_private psapriv = (phostsa_private)priv;
	hostsa_util_fns *util_fns = &psapriv->util_fns;
	UINT8 index = 0;
	pmkElement_t *pPMKElement = NULL;

#if 0				//!defined(REMOVE_PATCH_HOOKS)
	if (pmkCacheFindPSKElement_hook(pSsid, ssidLen, &pPMKElement)) {
		return pPMKElement;
	}
#endif

	for (index = 0; index < ramHook_MAX_PMK_CACHE_ENTRIES; index++) {
		/* See if the entry is valid.
		 ** See if the entry is a PSK
		 ** See if the SSID matches
		 */
		if (ramHook_pmkCache[index].replacementRank
		    && ramHook_pmkCache[index].length == ssidLen
		    && (0 == memcmp(util_fns, ramHook_pmkCache[index].key.Ssid,
				    pSsid, ssidLen))) {
			pPMKElement = (ramHook_pmkCache + index);
		}
	}

	/* Update the rank if an entry is found.  Null is an accepted
	 ** input for the function
	 */
	pmkCacheUpdateReplacementRank(pPMKElement);

	return pPMKElement;
}

UINT8 *
pmkCacheFindPMK(void *priv, IEEEtypes_MacAddr_t *pBssid)
{
	UINT8 *pPMK = NULL;
	pmkElement_t *pPMKElement = pmkCacheFindPMKElement(priv, pBssid);

	/* extract the PMK from the cache entry */
	if (pPMKElement) {
		pPMK = pPMKElement->PMK;
	}

	return pPMK;
}

void
pmkCacheAddPMK(void *priv, IEEEtypes_MacAddr_t *pBssid, UINT8 *pPMK)
{
	phostsa_private psapriv = (phostsa_private)priv;
	hostsa_util_fns *util_fns = &psapriv->util_fns;
	pmkElement_t *pPMKElement;

#if 0				//!defined(REMOVE_PATCH_HOOKS)
	if (pmkCacheAddPMK_hook(pBssid, pPMK)) {
		return;
	}
#endif

	pPMKElement = pmkCacheFindPMKElement(priv, pBssid);

	if (!pPMKElement) {
		/* Entry not found. Create a new entry and fill it in */
		pPMKElement = pmkCacheNewElement(priv);

		/* Update the key union with the BSSID */
		memcpy(util_fns, pPMKElement->key.Bssid,
		       pBssid, sizeof(pPMKElement->key.Bssid));

		/* Set the length to a value that is invalid for
		 ** an SSID.  The invalid value will flag the entry as a PMK
		 */
		pPMKElement->length = BSSID_FLAG;
	}

	if (pPMK) {
		memcpy(util_fns, pPMKElement->PMK, pPMK, MAX_PMK_SIZE);
	}
}

void
pmkCacheAddPSK(void *priv, UINT8 *pSsid, UINT8 ssidLen, UINT8 *pPSK,
	       UINT8 *pPassphrase)
{
	phostsa_private psapriv = (phostsa_private)priv;
	hostsa_util_fns *util_fns = &psapriv->util_fns;
	pmkElement_t *pPMKElement;

#if 0				//!defined(REMOVE_PATCH_HOOKS)
	if (pmkCacheAddPSK_hook(pSsid, ssidLen, pPSK)) {
		return;
	}
#endif

	pPMKElement = pmkCacheFindPSKElement(priv, pSsid, ssidLen);

	if (NULL == pPMKElement) {
		/* Entry not found. Create a new entry and fill it in */
		pPMKElement = pmkCacheNewElement(priv);

		/* Update the key portion with the SSID */
		memcpy(util_fns, pPMKElement->key.Ssid, pSsid, ssidLen);

		pPMKElement->length = ssidLen;
	}

	if (pPSK) {
		memcpy(util_fns, pPMKElement->PMK, pPSK, MAX_PMK_SIZE);
	}

	if (pPassphrase)
		memcpy(util_fns, pPMKElement->passphrase, pPassphrase,
		       PSK_PASS_PHRASE_LEN_MAX);

}

void
pmkCacheDeletePMK(void *priv, t_u8 *pBssid)
{
	phostsa_private psapriv = (phostsa_private)priv;
	hostsa_util_fns *util_fns = &psapriv->util_fns;
	pmkElement_t *pPMKElement =
		pmkCacheFindPMKElement(priv, (IEEEtypes_MacAddr_t *)pBssid);

	if (pPMKElement) {
		/* Invalidate the enrty by setting the memory for the
		 ** cache entry to zero.
		 ** This will ensure that the replacementRank is zero
		 */
		memset(util_fns, pPMKElement, 0x00, sizeof(pmkElement_t));
		replacementRankMax--;
	}
}

void
pmkCacheDeletePSK(void *priv, UINT8 *pSsid, UINT8 ssidLen)
{
	phostsa_private psapriv = (phostsa_private)priv;
	hostsa_util_fns *util_fns = &psapriv->util_fns;
	pmkElement_t *pPMKElement =
		pmkCacheFindPSKElement(priv, pSsid, ssidLen);

	if (pPMKElement) {
		/* Invalidate the enrty by setting the memory for the
		 ** cache entry to zero.
		 ** This will ensure that the replacementRank is zero
		 */
		memset(util_fns, pPMKElement, 0x00, sizeof(pmkElement_t));
		replacementRankMax--;
	}
}

UINT8
pmkCacheGetHexNibble(UINT8 nibble)
{
	if (nibble >= 'a') {
		return (nibble - 'a' + 10);
	}

	if (nibble >= 'A') {
		return (nibble - 'A' + 10);
	}

	return (nibble - '0');
}

void
pmkCacheGeneratePSK(void *priv, UINT8 *pSsid,
		    UINT8 ssidLen, char *pPassphrase, UINT8 *pPSK)
{
	int i;

#if 0				//!defined(REMOVE_PATCH_HOOKS)
	if (pmkCacheGeneratePSK_hook(pSsid, ssidLen, pPassphrase, pPSK)) {
		return;
	}
#endif

	if (pPSK && pPassphrase) {
		for (i = 0; i < PSK_PASS_PHRASE_LEN_MAX; i++) {
			if (pPassphrase[i] == 0) {
				break;
			}
		}

		if (i > 7 && i < PSK_PASS_PHRASE_LEN_MAX) {
			/* bump the CPU speed for the PSK generation */
			//ramHook_hal_SetCpuMaxSpeed();
			Mrvl_PasswordHash((void *)priv, pPassphrase,
					  (UINT8 *)pSsid, ssidLen, pPSK);
			//ramHook_hal_RestoreCpuSpeed();
		} else if (i == PSK_PASS_PHRASE_LEN_MAX) {
			/* Convert ASCII to binary */
			for (i = 0; i < PSK_PASS_PHRASE_LEN_MAX; i += 2) {
				pPSK[i / 2] =
					((pmkCacheGetHexNibble(pPassphrase[i])
					  << 4)
					 |
					 pmkCacheGetHexNibble(pPassphrase
							      [i + 1]));

			}
		}
	}
}
