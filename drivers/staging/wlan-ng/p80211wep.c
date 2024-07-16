// SPDX-License-Identifier: (GPL-2.0 OR MPL-1.1)
/*
 *
 * WEP encode/decode for P80211.
 *
 * Copyright (C) 2002 AbsoluteValue Systems, Inc.  All Rights Reserved.
 * --------------------------------------------------------------------
 *
 * linux-wlan
 *
 *   The contents of this file are subject to the Mozilla Public
 *   License Version 1.1 (the "License"); you may not use this file
 *   except in compliance with the License. You may obtain a copy of
 *   the License at http://www.mozilla.org/MPL/
 *
 *   Software distributed under the License is distributed on an "AS
 *   IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or
 *   implied. See the License for the specific language governing
 *   rights and limitations under the License.
 *
 *   Alternatively, the contents of this file may be used under the
 *   terms of the GNU Public License version 2 (the "GPL"), in which
 *   case the provisions of the GPL are applicable instead of the
 *   above.  If you wish to allow the use of your version of this file
 *   only under the terms of the GPL and not to allow others to use
 *   your version of this file under the MPL, indicate your decision
 *   by deleting the provisions above and replace them with the notice
 *   and other provisions required by the GPL.  If you do not delete
 *   the provisions above, a recipient may use your version of this
 *   file under either the MPL or the GPL.
 *
 * --------------------------------------------------------------------
 *
 * Inquiries regarding the linux-wlan Open Source project can be
 * made directly to:
 *
 * AbsoluteValue Systems Inc.
 * info@linux-wlan.com
 * http://www.linux-wlan.com
 *
 * --------------------------------------------------------------------
 *
 * Portions of the development of this software were funded by
 * Intersil Corporation as part of PRISM(R) chipset product development.
 *
 * --------------------------------------------------------------------
 */

/*================================================================*/
/* System Includes */

#include <linux/crc32.h>
#include <linux/netdevice.h>
#include <linux/wireless.h>
#include <linux/random.h>
#include <linux/kernel.h>
#include "p80211hdr.h"
#include "p80211types.h"
#include "p80211msg.h"
#include "p80211conv.h"
#include "p80211netdev.h"

#define WEP_KEY(x)       (((x) & 0xC0) >> 6)

/* keylen in bytes! */

int wep_change_key(struct wlandevice *wlandev, int keynum, u8 *key, int keylen)
{
	if (keylen < 0)
		return -1;
	if (keylen >= MAX_KEYLEN)
		return -1;
	if (!key)
		return -1;
	if (keynum < 0)
		return -1;
	if (keynum >= NUM_WEPKEYS)
		return -1;

	wlandev->wep_keylens[keynum] = keylen;
	memcpy(wlandev->wep_keys[keynum], key, keylen);

	return 0;
}

/*
 * 4-byte IV at start of buffer, 4-byte ICV at end of buffer.
 * if successful, buf start is payload begin, length -= 8;
 */
int wep_decrypt(struct wlandevice *wlandev, u8 *buf, u32 len, int key_override,
		u8 *iv, u8 *icv)
{
	u32 i, j, k, crc, keylen;
	u8 s[256], key[64], c_crc[4];
	u8 keyidx;

	/* Needs to be at least 8 bytes of payload */
	if (len <= 0)
		return -1;

	/* initialize the first bytes of the key from the IV */
	key[0] = iv[0];
	key[1] = iv[1];
	key[2] = iv[2];
	keyidx = WEP_KEY(iv[3]);

	if (key_override >= 0)
		keyidx = key_override;

	if (keyidx >= NUM_WEPKEYS)
		return -2;

	keylen = wlandev->wep_keylens[keyidx];

	if (keylen == 0)
		return -3;

	/* copy the rest of the key over from the designated key */
	memcpy(key + 3, wlandev->wep_keys[keyidx], keylen);

	keylen += 3;		/* add in IV bytes */

	/* set up the RC4 state */
	for (i = 0; i < 256; i++)
		s[i] = i;
	j = 0;
	for (i = 0; i < 256; i++) {
		j = (j + s[i] + key[i % keylen]) & 0xff;
		swap(i, j);
	}

	/* Apply the RC4 to the data, update the CRC32 */
	i = 0;
	j = 0;
	for (k = 0; k < len; k++) {
		i = (i + 1) & 0xff;
		j = (j + s[i]) & 0xff;
		swap(i, j);
		buf[k] ^= s[(s[i] + s[j]) & 0xff];
	}
	crc = ~crc32_le(~0, buf, len);

	/* now let's check the crc */
	c_crc[0] = crc;
	c_crc[1] = crc >> 8;
	c_crc[2] = crc >> 16;
	c_crc[3] = crc >> 24;

	for (k = 0; k < 4; k++) {
		i = (i + 1) & 0xff;
		j = (j + s[i]) & 0xff;
		swap(i, j);
		if ((c_crc[k] ^ s[(s[i] + s[j]) & 0xff]) != icv[k])
			return -(4 | (k << 4));	/* ICV mismatch */
	}

	return 0;
}

/* encrypts in-place. */
int wep_encrypt(struct wlandevice *wlandev, u8 *buf,
		u8 *dst, u32 len, int keynum, u8 *iv, u8 *icv)
{
	u32 i, j, k, crc, keylen;
	u8 s[256], key[64];

	/* no point in WEPping an empty frame */
	if (len <= 0)
		return -1;

	/* we need to have a real key.. */
	if (keynum >= NUM_WEPKEYS)
		return -2;
	keylen = wlandev->wep_keylens[keynum];
	if (keylen <= 0)
		return -3;

	/* use a random IV.  And skip known weak ones. */
	get_random_bytes(iv, 3);
	while ((iv[1] == 0xff) && (iv[0] >= 3) && (iv[0] < keylen))
		get_random_bytes(iv, 3);

	iv[3] = (keynum & 0x03) << 6;

	key[0] = iv[0];
	key[1] = iv[1];
	key[2] = iv[2];

	/* copy the rest of the key over from the designated key */
	memcpy(key + 3, wlandev->wep_keys[keynum], keylen);

	keylen += 3;		/* add in IV bytes */

	/* set up the RC4 state */
	for (i = 0; i < 256; i++)
		s[i] = i;
	j = 0;
	for (i = 0; i < 256; i++) {
		j = (j + s[i] + key[i % keylen]) & 0xff;
		swap(i, j);
	}

	/* Update CRC32 then apply RC4 to the data */
	i = 0;
	j = 0;
	for (k = 0; k < len; k++) {
		i = (i + 1) & 0xff;
		j = (j + s[i]) & 0xff;
		swap(i, j);
		dst[k] = buf[k] ^ s[(s[i] + s[j]) & 0xff];
	}
	crc = ~crc32_le(~0, buf, len);

	/* now let's encrypt the crc */
	icv[0] = crc;
	icv[1] = crc >> 8;
	icv[2] = crc >> 16;
	icv[3] = crc >> 24;

	for (k = 0; k < 4; k++) {
		i = (i + 1) & 0xff;
		j = (j + s[i]) & 0xff;
		swap(i, j);
		icv[k] ^= s[(s[i] + s[j]) & 0xff];
	}

	return 0;
}
