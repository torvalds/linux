/*
 * ---------------------------------------------------------------------------
 * FILE:     data_tx.c
 *
 * PURPOSE:
 *      This file provides functions to send data requests to the UniFi.
 *
 * Copyright (C) 2007-2009 by Cambridge Silicon Radio Ltd.
 *
 * Refer to LICENSE.txt included with this source code for details on
 * the license terms.
 *
 * ---------------------------------------------------------------------------
 */
#include "csr_wifi_hip_unifi.h"
#include "unifi_priv.h"

int
uf_verify_m4(unifi_priv_t *priv, const unsigned char *packet, unsigned int length)
{
	const unsigned char *p = packet;
	u16 keyinfo;


	if (length < (4 + 5 + 8 + 32 + 16 + 8 + 8 + 16 + 1 + 8))
		return 1;

	p += 8;
	keyinfo = p[5] << 8 | p[6]; /* big-endian */
	if (
	  (p[0] == 1 || p[0] == 2) /* protocol version 802.1X-2001 (WPA) or -2004 (WPA2) */ &&
	  p[1] == 3 /* EAPOL-Key */ &&
	  /* don't bother checking p[2] p[3] (hh ll, packet body length) */
	  (p[4] == 254 || p[4] == 2) /* descriptor type P802.1i-D3.0 (WPA) or 802.11i-2004 (WPA2) */ &&
	  ((keyinfo & 0x0007) == 1 || (keyinfo & 0x0007) == 2) /* key descriptor version */ &&
	 (keyinfo & ~0x0207U) == 0x0108 && /* key info for 4/4 or 4/2 -- ignore key desc version and sec bit (since varies in WPA 4/4) */
	  (p[4 + 5 + 8 + 32 + 16 + 8 + 8 + 16 + 0] == 0 && /* key data length (2 octets) 0 for 4/4 only */
	   p[4 + 5 + 8 + 32 + 16 + 8 + 8 + 16 + 1] == 0)
	) {
		unifi_trace(priv, UDBG1, "uf_verify_m4: M4 detected\n");
		return 0;
	} else {
		return 1;
	}
}

/*
 * ---------------------------------------------------------------------------
 *
 *      Data transport signals.
 *
 * ---------------------------------------------------------------------------
 */

