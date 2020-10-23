/*
 * GCM with GMAC Protocol (GCMP)
 * Copyright (c) 2012, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "rtw_crypto_wrap.h"

#include "aes.h"
#include "aes_wrap.h"
#include "wlancrypto_wrap.h"


static void gcmp_aad_nonce(const struct ieee80211_hdr *hdr, const u8 *data,
			   u8 *aad, size_t *aad_len, u8 *nonce)
{
	u16 fc, stype, seq;
	int qos = 0, addr4 = 0;
	u8 *pos;

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
		}
	}

	fc &= ~(WLAN_FC_RETRY | WLAN_FC_PWRMGT | WLAN_FC_MOREDATA);
	WPA_PUT_LE16(aad, fc);
	pos = aad + 2;
	os_memcpy(pos, GetAddr1Ptr((u8 *)hdr), 3 * ETH_ALEN);
	pos += 3 * ETH_ALEN;
	seq = le_to_host16(hdr->seq_ctrl);
	seq &= ~0xfff0; /* Mask Seq#; do not modify Frag# */
	WPA_PUT_LE16(pos, seq);
	pos += 2;

	wpa_printf(_MSG_INFO_, "pos - aad = %u, qos(%d)\n", (pos - aad), qos);

	os_memcpy(pos, (u8 *)hdr + 24, addr4 * ETH_ALEN + qos * 2);
	pos += addr4 * ETH_ALEN;
	if (qos) {
		pos[0] &= ~0x70;
		if (1 /* FIX: either device has SPP A-MSDU Capab = 0 */)
			pos[0] &= ~0x80;
		pos++;
		*pos++ = 0x00;
	}

	wpa_printf(_MSG_INFO_, "pos - aad = %u\n", (pos - aad));
	*aad_len = pos - aad;

	os_memcpy(nonce, hdr->addr2, ETH_ALEN);
	nonce[6] = data[7]; /* PN5 */
	nonce[7] = data[6]; /* PN4 */
	nonce[8] = data[5]; /* PN3 */
	nonce[9] = data[4]; /* PN2 */
	nonce[10] = data[1]; /* PN1 */
	nonce[11] = data[0]; /* PN0 */
}

/**
 * gcmp_decrypt -
 * @tk: the temporal key
 * @tk_len: length of @tk
 * @hdr: the mac header
 * @data: payload after mac header (PN + enc_data + MIC)
 * @data_len: length of @data (PN + enc_data + MIC)
 * @decrypted_len: length of the data decrypted
 */
u8 * gcmp_decrypt(const u8 *tk, size_t tk_len, const struct ieee80211_hdr *hdr,
		  const u8 *data, size_t data_len, size_t *decrypted_len)
{
	u8 aad[30], nonce[12], *plain;
	size_t aad_len, mlen;
	const u8 *m;

	if (data_len < 8 + 16)
		return NULL;

	plain = os_malloc(data_len + AES_BLOCK_SIZE);
	if (plain == NULL)
		return NULL;

	m = data + 8;
	mlen = data_len - 8 - 16;

	os_memset(aad, 0, sizeof(aad));
	gcmp_aad_nonce(hdr, data, aad, &aad_len, nonce);
	wpa_hexdump(_MSG_EXCESSIVE_, "GCMP AAD", aad, aad_len);
	wpa_hexdump(_MSG_EXCESSIVE_, "GCMP nonce", nonce, sizeof(nonce));

	if (aes_gcm_ad(tk, tk_len, nonce, sizeof(nonce), m, mlen, aad, aad_len,
		       m + mlen, plain) < 0) {
		u16 seq_ctrl = le_to_host16(hdr->seq_ctrl);
		wpa_printf(_MSG_INFO_, "Invalid GCMP frame: A1=" MACSTR
			   " A2=" MACSTR " A3=" MACSTR " seq=%u frag=%u",
			   MAC2STR(hdr->addr1), MAC2STR(hdr->addr2),
			   MAC2STR(hdr->addr3),
			   WLAN_GET_SEQ_SEQ(seq_ctrl),
			   WLAN_GET_SEQ_FRAG(seq_ctrl));
		rtw_mfree(plain, data_len + AES_BLOCK_SIZE);
		return NULL;
	}

	*decrypted_len = mlen;
	return plain;
}

/**
 * gcmp_encrypt - 
 * @tk: the temporal key
 * @tk_len: length of @tk
 * @frame: the point to mac header, the frame including mac header and payload, 
 *         if @pn is NULL, then the frame including pn
 * @len: length of @frame 
 *         length = mac header + payload
 * @hdrlen: length of the mac header
 * @qos: pointer to the QOS field of the frame
 * @pn: packet number
 * @keyid: key id
 * @encrypted_len: length of the encrypted frame 
 *                 including mac header, pn, payload and MIC
 */
u8 * gcmp_encrypt(const u8 *tk, size_t tk_len, const u8 *frame, size_t len,
		  size_t hdrlen, const u8 *qos,
		  const u8 *pn, int keyid, size_t *encrypted_len)
{
	u8 aad[30], nonce[12], *crypt, *pos;
	const u8 *pdata;
	size_t aad_len, plen;
	struct ieee80211_hdr *hdr;

	if (len < hdrlen || hdrlen < 24)
		return NULL;
	plen = len - hdrlen;

	crypt = os_malloc(hdrlen + 8 + plen + 16 + AES_BLOCK_SIZE);
	if (crypt == NULL)
		return NULL;

	if (pn == NULL) {
		os_memcpy(crypt, frame, hdrlen + 8);
		hdr = (struct ieee80211_hdr *)crypt;
		pos = crypt + hdrlen + 8;
		pdata = frame + hdrlen + 8;
	} else {
		os_memcpy(crypt, frame, hdrlen);
		hdr = (struct ieee80211_hdr *)crypt;
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
	gcmp_aad_nonce(hdr, crypt + hdrlen, aad, &aad_len, nonce);
	wpa_hexdump(_MSG_EXCESSIVE_, "GCMP AAD", aad, aad_len);
	wpa_hexdump(_MSG_EXCESSIVE_, "GCMP nonce", nonce, sizeof(nonce));

	if (aes_gcm_ae(tk, tk_len, nonce, sizeof(nonce), pdata, plen,
			aad, aad_len, pos, pos + plen) < 0) {
		rtw_mfree(crypt, hdrlen + 8 + plen + 16 + AES_BLOCK_SIZE);
		return NULL;
	}

	wpa_hexdump(_MSG_EXCESSIVE_, "GCMP MIC", pos + plen, 16);
	wpa_hexdump(_MSG_EXCESSIVE_, "GCMP encrypted", pos, plen);

	*encrypted_len = hdrlen + 8 + plen + 16;

	return crypt;
}
