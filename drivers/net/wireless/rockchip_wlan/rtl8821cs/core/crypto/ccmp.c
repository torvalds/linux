/* SPDX-License-Identifier: GPL-2.0 */
/*
 * CTR with CBC-MAC Protocol (CCMP)
 * Copyright (c) 2010-2012, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "rtw_crypto_wrap.h"

#include "aes.h"
#include "aes_wrap.h"
#include "wlancrypto_wrap.h"



static void ccmp_aad_nonce(const struct ieee80211_hdr *hdr, const u8 *data,
			   u8 *aad, size_t *aad_len, u8 *nonce)
{
	u16 fc, stype, seq;
	int qos = 0, addr4 = 0;
	u8 *pos;

	nonce[0] = 0;

	fc = le_to_host16(hdr->frame_control);
	stype = WLAN_FC_GET_STYPE(fc);
	if ((fc & (WLAN_FC_TODS | WLAN_FC_FROMDS)) ==
	    (WLAN_FC_TODS | WLAN_FC_FROMDS))
		addr4 = 1;

	if (WLAN_FC_GET_TYPE(fc) == WLAN_FC_TYPE_DATA) {
		fc &= ~0x0070; /* Mask subtype bits */
		if (stype & WLAN_FC_STYPE_QOS_DATA) {
			const u8 *qc;
			qos = 1;
			fc &= ~WLAN_FC_ORDER;
			qc = (const u8 *)hdr + 24;
			if (addr4)
				qc += ETH_ALEN;
			nonce[0] = qc[0] & 0x0f;
		}
	} else if (WLAN_FC_GET_TYPE(fc) == WLAN_FC_TYPE_MGMT)
		nonce[0] |= 0x10; /* Management */

	fc &= ~(WLAN_FC_RETRY | WLAN_FC_PWRMGT | WLAN_FC_MOREDATA);
	fc |= WLAN_FC_ISWEP;
	WPA_PUT_LE16(aad, fc);
	pos = aad + 2;
	os_memcpy(pos, GetAddr1Ptr((u8 *)hdr), 3 * ETH_ALEN);
	pos += 3 * ETH_ALEN;
	seq = le_to_host16(hdr->seq_ctrl);
	seq &= ~0xfff0; /* Mask Seq#; do not modify Frag# */
	WPA_PUT_LE16(pos, seq);
	pos += 2;

	os_memcpy(pos, (u8 *)hdr + 24, addr4 * ETH_ALEN + qos * 2);
	pos += addr4 * ETH_ALEN;
	if (qos) {
		pos[0] &= ~0x70;
		if (1 /* FIX: either device has SPP A-MSDU Capab = 0 */)
			pos[0] &= ~0x80;
		pos++;
		*pos++ = 0x00;
	}

	*aad_len = pos - aad;

	os_memcpy(nonce + 1, hdr->addr2, ETH_ALEN);
	nonce[7] = data[7]; /* PN5 */
	nonce[8] = data[6]; /* PN4 */
	nonce[9] = data[5]; /* PN3 */
	nonce[10] = data[4]; /* PN2 */
	nonce[11] = data[1]; /* PN1 */
	nonce[12] = data[0]; /* PN0 */
}


static void ccmp_aad_nonce_pv1(const u8 *hdr, const u8 *a1, const u8 *a2,
			       const u8 *a3, const u8 *pn,
			       u8 *aad, size_t *aad_len, u8 *nonce)
{
	u16 fc, type;
	u8 *pos;

	nonce[0] = BIT(5); /* PV1 */
	/* TODO: Priority for QMF; 0 is used for Data frames */

	fc = WPA_GET_LE16(hdr);
	type = (fc & (BIT(2) | BIT(3) | BIT(4))) >> 2;

	if (type == 1)
		nonce[0] |= 0x10; /* Management */

	fc &= ~(BIT(10) | BIT(11) | BIT(13) | BIT(14) | BIT(15));
	fc |= BIT(12);
	WPA_PUT_LE16(aad, fc);
	pos = aad + 2;
	if (type == 0 || type == 3) {
		const u8 *sc;

		os_memcpy(pos, a1, ETH_ALEN);
		pos += ETH_ALEN;
		os_memcpy(pos, a2, ETH_ALEN);
		pos += ETH_ALEN;

		if (type == 0) {
			/* Either A1 or A2 contains SID */
			sc = hdr + 2 + 2 + ETH_ALEN;
		} else {
			/* Both A1 and A2 contain full addresses */
			sc = hdr + 2 + 2 * ETH_ALEN;
		}
		/* SC with Sequence Number subfield (bits 4-15 of the Sequence
		 * Control field) masked to 0. */
		*pos++ = *sc & 0x0f;
		*pos++ = 0;

		if (a3) {
			os_memcpy(pos, a3, ETH_ALEN);
			pos += ETH_ALEN;
		}
	}

	*aad_len = pos - aad;

	os_memcpy(nonce + 1, a2, ETH_ALEN);
	nonce[7] = pn[5]; /* PN5 */
	nonce[8] = pn[4]; /* PN4 */
	nonce[9] = pn[3]; /* PN3 */
	nonce[10] = pn[2]; /* PN2 */
	nonce[11] = pn[1]; /* PN1 */
	nonce[12] = pn[0]; /* PN0 */
}


u8 * ccmp_decrypt(const u8 *tk, const struct ieee80211_hdr *hdr,
		  const u8 *data, size_t data_len, size_t *decrypted_len)
{
	u8 aad[30], nonce[13];
	size_t aad_len;
	size_t mlen;
	u8 *plain;

	if (data_len < 8 + 8)
		return NULL;

	plain = os_malloc(data_len + AES_BLOCK_SIZE);
	if (plain == NULL)
		return NULL;

	mlen = data_len - 8 - 8;

	os_memset(aad, 0, sizeof(aad));
	ccmp_aad_nonce(hdr, data, aad, &aad_len, nonce);
	wpa_hexdump(_MSG_EXCESSIVE_, "CCMP AAD", aad, aad_len);
	wpa_hexdump(_MSG_EXCESSIVE_, "CCMP nonce", nonce, 13);

	if (aes_ccm_ad(tk, 16, nonce, 8, data + 8, mlen, aad, aad_len,
		       data + 8 + mlen, plain) < 0) {
		u16 seq_ctrl = le_to_host16(hdr->seq_ctrl);
		wpa_printf(_MSG_INFO_, "Invalid CCMP MIC in frame: A1=" MACSTR
			   " A2=" MACSTR " A3=" MACSTR " seq=%u frag=%u",
			   MAC2STR(hdr->addr1), MAC2STR(hdr->addr2),
			   MAC2STR(hdr->addr3),
			   WLAN_GET_SEQ_SEQ(seq_ctrl),
			   WLAN_GET_SEQ_FRAG(seq_ctrl));
		rtw_mfree(plain, data_len + AES_BLOCK_SIZE);
		return NULL;
	}
	wpa_hexdump(_MSG_EXCESSIVE_, "CCMP decrypted", plain, mlen);

	*decrypted_len = mlen;
	return plain;
}


void ccmp_get_pn(u8 *pn, const u8 *data)
{
	pn[0] = data[7]; /* PN5 */
	pn[1] = data[6]; /* PN4 */
	pn[2] = data[5]; /* PN3 */
	pn[3] = data[4]; /* PN2 */
	pn[4] = data[1]; /* PN1 */
	pn[5] = data[0]; /* PN0 */
}


u8 * ccmp_encrypt(const u8 *tk, u8 *frame, size_t len, size_t hdrlen, u8 *qos,
		  u8 *pn, int keyid, size_t *encrypted_len)
{
	u8 aad[30], nonce[13];
	size_t aad_len, plen;
	u8 *crypt, *pos, *pdata;
	struct ieee80211_hdr *hdr;

	if (len < hdrlen || hdrlen < 24)
		return NULL;
	plen = len - hdrlen;

	crypt = os_malloc(hdrlen + 8 + plen + 8 + AES_BLOCK_SIZE);
	if (crypt == NULL)
		return NULL;

	if (pn == NULL) {
		os_memcpy(crypt, frame, hdrlen + 8);
		hdr = (struct ieee80211_hdr *) crypt;
		hdr->frame_control |= host_to_le16(WLAN_FC_ISWEP);
		pos = crypt + hdrlen + 8;
		pdata = frame + hdrlen + 8;
	} else {
		os_memcpy(crypt, frame, hdrlen);
		hdr = (struct ieee80211_hdr *) crypt;
		hdr->frame_control |= host_to_le16(WLAN_FC_ISWEP);
		pos = crypt + hdrlen;
		*pos++ = pn[5]; /* PN0 */
		*pos++ = pn[4]; /* PN1 */
		*pos++ = 0x00; /* Rsvd */
		*pos++ = 0x20 | (keyid << 6);
		*pos++ = pn[3]; /* PN2 */
		*pos++ = pn[2]; /* PN3 */
		*pos++ = pn[1]; /* PN4 */
		*pos++ = pn[0]; /* PN5 */
		pdata = frame + hdrlen;
	}

	os_memset(aad, 0, sizeof(aad));
	ccmp_aad_nonce(hdr, crypt + hdrlen, aad, &aad_len, nonce);
	wpa_hexdump(_MSG_EXCESSIVE_, "CCMP AAD", aad, aad_len);
	wpa_hexdump(_MSG_EXCESSIVE_, "CCMP nonce", nonce, 13);

	if (aes_ccm_ae(tk, 16, nonce, 8, pdata, plen, aad, aad_len,
		       pos, pos + plen) < 0) {
		rtw_mfree(crypt, hdrlen + 8 + plen + 8 + AES_BLOCK_SIZE);
		return NULL;
	}

	wpa_hexdump(_MSG_EXCESSIVE_, "CCMP encrypted", crypt + hdrlen + 8, plen);

	*encrypted_len = hdrlen + 8 + plen + 8;

	return crypt;
}


u8 * ccmp_encrypt_pv1(const u8 *tk, const u8 *a1, const u8 *a2, const u8 *a3,
		      const u8 *frame, size_t len,
		      size_t hdrlen, const u8 *pn, int keyid,
		      size_t *encrypted_len)
{
	u8 aad[24], nonce[13];
	size_t aad_len, plen;
	u8 *crypt, *pos;
	struct ieee80211_hdr *hdr;

	if (len < hdrlen || hdrlen < 12)
		return NULL;
	plen = len - hdrlen;

	crypt = os_malloc(hdrlen + plen + 8 + AES_BLOCK_SIZE);
	if (crypt == NULL)
		return NULL;

	os_memcpy(crypt, frame, hdrlen);
	hdr = (struct ieee80211_hdr *) crypt;
	hdr->frame_control |= host_to_le16(BIT(12)); /* Protected Frame */
	pos = crypt + hdrlen;

	os_memset(aad, 0, sizeof(aad));
	ccmp_aad_nonce_pv1(crypt, a1, a2, a3, pn, aad, &aad_len, nonce);
	wpa_hexdump(_MSG_EXCESSIVE_, "CCMP AAD", aad, aad_len);
	wpa_hexdump(_MSG_EXCESSIVE_, "CCMP nonce", nonce, sizeof(nonce));

	if (aes_ccm_ae(tk, 16, nonce, 8, frame + hdrlen, plen, aad, aad_len,
		       pos, pos + plen) < 0) {
		rtw_mfree(crypt, hdrlen + plen + 8 + AES_BLOCK_SIZE);
		return NULL;
	}

	wpa_hexdump(_MSG_EXCESSIVE_, "CCMP encrypted", crypt + hdrlen, plen);

	*encrypted_len = hdrlen + plen + 8;

	return crypt;
}


u8 * ccmp_256_decrypt(const u8 *tk, const struct ieee80211_hdr *hdr,
		      const u8 *data, size_t data_len, size_t *decrypted_len)
{
	u8 aad[30], nonce[13];
	size_t aad_len;
	size_t mlen;
	u8 *plain;

	if (data_len < 8 + 16)
		return NULL;

	plain = os_malloc(data_len + AES_BLOCK_SIZE);
	if (plain == NULL)
		return NULL;

	mlen = data_len - 8 - 16;

	os_memset(aad, 0, sizeof(aad));
	ccmp_aad_nonce(hdr, data, aad, &aad_len, nonce);
	wpa_hexdump(_MSG_EXCESSIVE_, "CCMP-256 AAD", aad, aad_len);
	wpa_hexdump(_MSG_EXCESSIVE_, "CCMP-256 nonce", nonce, 13);

	if (aes_ccm_ad(tk, 32, nonce, 16, data + 8, mlen, aad, aad_len,
		       data + 8 + mlen, plain) < 0) {
		u16 seq_ctrl = le_to_host16(hdr->seq_ctrl);
		wpa_printf(_MSG_INFO_, "Invalid CCMP-256 MIC in frame: A1=" MACSTR
			   " A2=" MACSTR " A3=" MACSTR " seq=%u frag=%u",
			   MAC2STR(hdr->addr1), MAC2STR(hdr->addr2),
			   MAC2STR(hdr->addr3),
			   WLAN_GET_SEQ_SEQ(seq_ctrl),
			   WLAN_GET_SEQ_FRAG(seq_ctrl));
		rtw_mfree(plain, data_len + AES_BLOCK_SIZE);
		return NULL;
	}
	wpa_hexdump(_MSG_EXCESSIVE_, "CCMP-256 decrypted", plain, mlen);

	*decrypted_len = mlen;
	return plain;
}


u8 * ccmp_256_encrypt(const u8 *tk, u8 *frame, size_t len, size_t hdrlen,
		      u8 *qos, u8 *pn, int keyid, size_t *encrypted_len)
{
	u8 aad[30], nonce[13];
	size_t aad_len, plen;
	u8 *crypt, *pos, *pdata;
	struct ieee80211_hdr *hdr;

	if (len < hdrlen || hdrlen < 24)
		return NULL;
	plen = len - hdrlen;

	crypt = os_malloc(hdrlen + 8 + plen + 16 + AES_BLOCK_SIZE);
	if (crypt == NULL)
		return NULL;

	if (pn == NULL) {
		os_memcpy(crypt, frame, hdrlen + 8);
		hdr = (struct ieee80211_hdr *) crypt;
		hdr->frame_control |= host_to_le16(WLAN_FC_ISWEP);
		pos = crypt + hdrlen + 8;
		pdata = frame + hdrlen + 8;
	} else {
		os_memcpy(crypt, frame, hdrlen);
		hdr = (struct ieee80211_hdr *) crypt;
		hdr->frame_control |= host_to_le16(WLAN_FC_ISWEP);
		pos = crypt + hdrlen;
		*pos++ = pn[5]; /* PN0 */
		*pos++ = pn[4]; /* PN1 */
		*pos++ = 0x00; /* Rsvd */
		*pos++ = 0x20 | (keyid << 6);
		*pos++ = pn[3]; /* PN2 */
		*pos++ = pn[2]; /* PN3 */
		*pos++ = pn[1]; /* PN4 */
		*pos++ = pn[0]; /* PN5 */
		pdata = frame + hdrlen;
	}

	os_memset(aad, 0, sizeof(aad));
	ccmp_aad_nonce(hdr, crypt + hdrlen, aad, &aad_len, nonce);
	wpa_hexdump(_MSG_EXCESSIVE_, "CCMP-256 AAD", aad, aad_len);
	wpa_hexdump(_MSG_EXCESSIVE_, "CCMP-256 nonce", nonce, 13);

	if (aes_ccm_ae(tk, 32, nonce, 16, pdata, plen, aad, aad_len,
		       pos, pos + plen) < 0) {
		rtw_mfree(crypt, hdrlen + 8 + plen + 16 + AES_BLOCK_SIZE);
		return NULL;
	}

	wpa_hexdump(_MSG_EXCESSIVE_, "CCMP-256 encrypted", crypt + hdrlen + 8,
		    plen);

	*encrypted_len = hdrlen + 8 + plen + 16;

	return crypt;
}
