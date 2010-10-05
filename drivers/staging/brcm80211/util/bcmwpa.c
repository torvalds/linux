/*
 * Copyright (c) 2010 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <typedefs.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linuxver.h>
#include <bcmutils.h>
#include <bcmwpa.h>

/* Is this body of this tlvs entry a WFA entry? If
 * not update the tlvs buffer pointer/length.
 */
bool bcm_is_wfa_ie(u8 *ie, u8 **tlvs, uint *tlvs_len, u8 type)
{
	/* If the contents match the WFA_OUI and type */
	if ((ie[TLV_LEN_OFF] > (WFA_OUI_LEN + 1)) &&
	    !bcmp(&ie[TLV_BODY_OFF], WFA_OUI, WFA_OUI_LEN) &&
	    type == ie[TLV_BODY_OFF + WFA_OUI_LEN]) {
		return TRUE;
	}

	/* point to the next ie */
	ie += ie[TLV_LEN_OFF] + TLV_HDR_LEN;
	/* calculate the length of the rest of the buffer */
	*tlvs_len -= (int)(ie - *tlvs);
	/* update the pointer to the start of the buffer */
	*tlvs = ie;

	return FALSE;
}

wpa_ie_fixed_t *BCMROMFN(bcm_find_wpaie) (u8 * parse, uint len)
{
	bcm_tlv_t *ie;

	while ((ie = bcm_parse_tlvs(parse, len, DOT11_MNG_VS_ID))) {
		if (bcm_is_wpa_ie((u8 *) ie, &parse, &len)) {
			return (wpa_ie_fixed_t *) ie;
		}
	}
	return NULL;
}
