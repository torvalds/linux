/** @file pmkcache.h
 *
 *  @brief This file contains define for pmk cache
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
#ifndef PMK_CACHE_H__
#define PMK_CACHE_H__

#include "wltypes.h"
#include "IEEE_types.h"
#include "pmkCache_rom.h"

/*!
** \brief      If a matching SSID entry is present in the PMK Cache, returns a
**             pointer to the PSK.  If no entry is found in the cache, a
**             new PSK entry will be generated if a PassPhrase is set.
** \param      pSsid pointer to string containing desired SSID.
** \param      ssidLen length of the SSID string *pSsid.
** \exception  Does not handle the case when multiple matching SSID entries are
**             found. Returns the first match.
** \return     pointer to PSK with matching SSID entry from PMK cache,
**             NULL if no matching entry found.
*/
extern UINT8 *pmkCacheFindPSK(void *priv, UINT8 *pSsid, UINT8 ssidLen);

/*!
** \brief      Flushes all entries in PMK cache
*/
extern void pmkCacheFlush(void *priv);

extern void pmkCacheGetPassphrase(void *priv, char *pPassphrase);

extern void pmkCacheSetPassphrase(void *priv, char *pPassphrase);
extern void pmkCacheInit(void *priv);
extern void pmkCacheRomInit(void);

extern void supplicantDisable(void *priv);

#endif
