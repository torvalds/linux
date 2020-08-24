/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2017 Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 *****************************************************************************/
#include <drv_types.h>
#include <hal_data.h>
#include <aes.h>
#include <aes_siv.h>
#include <aes_wrap.h>
#include <sha256.h>
#include <wlancrypto_wrap.h>

/**
 * rtw_ccmp_encrypt - 
 * @key: the temporal key 
 * @hdrlen: mac header length
 * @frame: the frame including the mac header, pn and payload
 * @plen: payload length, i.e., length of the plain text, without PN and MIC
 */
int _rtw_ccmp_encrypt(u8 *key, u32 key_len, uint hdrlen, u8 *frame, uint plen)
{
	u8 *enc = NULL;
	size_t enc_len = 0;

	if (key_len == 16) { /* 128 bits */
		enc = ccmp_encrypt(key,
			frame,
			hdrlen + plen,
			hdrlen,
			(hdrlen == 26) ? (frame + hdrlen - 2) : NULL,
			NULL, 0, &enc_len);
	} else if (key_len == 32) { /* 256 bits */
		enc = ccmp_256_encrypt(key,
			frame,
			hdrlen + plen,
			hdrlen,
			(hdrlen == 26) ? (frame + hdrlen - 2) : NULL,
			NULL, 0, &enc_len);
	}

	if (enc == NULL) {
		RTW_INFO("Failed to encrypt CCMP(%u) frame", key_len);
		return _FAIL;
	}

	/* Copy @enc back to @frame and free @enc */
	_rtw_memcpy(frame, enc, enc_len);
	rtw_mfree(enc, enc_len + AES_BLOCK_SIZE);

	return _SUCCESS;
}


/**
 * rtw_ccmp_decrypt -
 * @key: the temporal key
 * @hdrlen: length of the mac header
 * @frame: the raw frame (@hdrlen + PN + enc_data + MIC)
 * @plen: length of the frame (@hdrlen + PN + enc_data + MIC)
 */
int _rtw_ccmp_decrypt(u8 *key, u32 key_len, uint hdrlen, u8 *frame,
	uint plen)
{
	u8 *plain = NULL;
	size_t plain_len = 0;
	const struct ieee80211_hdr *hdr;

	hdr = (const struct ieee80211_hdr *)frame;

	if (key_len == 16) { /* 128 bits */
		plain = ccmp_decrypt(key,
			hdr,
			frame + hdrlen, /* PN + enc_data + MIC */
			plen - hdrlen, /* PN + enc_data + MIC */
			&plain_len);
	} else if (key_len == 32) { /* 256 bits */
		plain = ccmp_256_decrypt(key,
			hdr,
			frame + hdrlen, /* PN + enc_data + MIC */
			plen - hdrlen, /* PN + enc_data + MIC */
			&plain_len);
	}

	if (plain == NULL) {
		RTW_INFO("Failed to decrypt CCMP(%u) frame", key_len);
		return _FAIL;
	}

	/* Copy @plain back to @frame and free @plain */
	_rtw_memcpy(frame + hdrlen + 8, plain, plain_len);
	rtw_mfree(plain, plen - hdrlen + AES_BLOCK_SIZE);

	RTW_DBG_DUMP("ccmp_decrypt(): decrypted frame\n",
		frame, hdrlen + 8 + plen);

	return _SUCCESS;
}


#ifdef CONFIG_RTW_MESH_AEK
/* wrapper to ase_siv_encrypt and aes_siv_decrypt */
int _aes_siv_encrypt(const u8 *key, size_t key_len,
	const u8 *pw, size_t pwlen,
	size_t num_elem, const u8 *addr[], const size_t *len, u8 *out)
{
	return aes_siv_encrypt(key, key_len, pw, pwlen, num_elem, addr, len, out);
}
int _aes_siv_decrypt(const u8 *key, size_t key_len,
	const u8 *iv_crypt, size_t iv_c_len,
	size_t num_elem, const u8 *addr[], const size_t *len, u8 *out)
{
	return aes_siv_decrypt(key, key_len, iv_crypt, iv_c_len, num_elem, addr, len, out);
}
#endif


/**
 * _rtw_gcmp_encrypt - 
 * @key: the temporal key 
 * @hdrlen: mac header length
 * @frame: the frame including the mac header, pn and payload
 * @plen: payload length, i.e., length of the plain text, without PN and MIC
 */
int _rtw_gcmp_encrypt(u8 *key, u32 key_len, uint hdrlen, u8 *frame, uint plen)
{
	u8 *enc = NULL;
	size_t enc_len = 0;

	enc = gcmp_encrypt(key, key_len,
		frame,
		hdrlen + plen,
		hdrlen,
		(hdrlen == 26) ? (frame + hdrlen - 2) : NULL,
		NULL, 0, &enc_len);
	if (enc == NULL) {
		RTW_INFO("Failed to encrypt GCMP frame");
		return _FAIL;
	}

	/* Copy @enc back to @frame and free @enc */
	_rtw_memcpy(frame, enc, enc_len);
	rtw_mfree(enc, enc_len + AES_BLOCK_SIZE);

	return _SUCCESS;
}


/**
 * _rtw_gcmp_decrypt -
 * @key: the temporal key
 * @hdrlen: length of the mac header
 * @frame: the raw frame (@hdrlen + PN + enc_data + MIC)
 * @plen: length of the frame (@hdrlen + PN + enc_data + MIC)
 */
int _rtw_gcmp_decrypt(u8 *key, u32 key_len, uint hdrlen, u8 *frame, uint plen)
{
	u8 *plain = NULL;
	size_t plain_len = 0;
	const struct ieee80211_hdr *hdr;

	hdr = (const struct ieee80211_hdr *)frame;

	plain = gcmp_decrypt(key, key_len,
		hdr,
		frame + hdrlen, /* PN + enc_data + MIC */
		plen - hdrlen, /* PN + enc_data + MIC */
		&plain_len);

	if (plain == NULL) {
		RTW_INFO("Failed to decrypt GCMP(%u) frame", key_len);
		return _FAIL;
	}

	/* Copy @plain back to @frame and free @plain */
	_rtw_memcpy(frame + hdrlen + 8, plain, plain_len);
	rtw_mfree(plain, plen - hdrlen + AES_BLOCK_SIZE);

	RTW_DBG_DUMP("gcmp_decipher(): decrypted frame\n",
		frame, hdrlen + 8 + plen);

	return _SUCCESS;
}


#if  defined(CONFIG_IEEE80211W) | defined(CONFIG_TDLS)
u8 _bip_ccmp_protect(const u8 *key, size_t key_len,
	const u8 *data, size_t data_len, u8 *mic)
{
	u8 res = _SUCCESS;

	if (key_len == 16) {
		if (omac1_aes_128(key, data, data_len, mic)) {
			res = _FAIL;
			RTW_ERR("%s : omac1_aes_128 fail!", __func__);
		}
	} else if (key_len == 32) {
		if (omac1_aes_256(key, data, data_len, mic)) {
			res = _FAIL;
			RTW_ERR("%s : omac1_aes_256 fail!", __func__);
		}
	} else {
		RTW_ERR("%s : key_len not match!", __func__);
		res = _FAIL;
	}

	return  res;
}


u8 _bip_gcmp_protect(u8 *whdr_pos, size_t len,
	const u8 *key, size_t key_len,
	const u8 *data, size_t data_len, u8 *mic)
{
	u8 res = _SUCCESS;
	u32 mic_len = 16;
	u8 nonce[12], *npos;
	const u8 *gcmp_ipn;

	gcmp_ipn = whdr_pos + len - mic_len - 6;

	/* Nonce: A2 | IPN */
	_rtw_memcpy(nonce, get_addr2_ptr(whdr_pos), ETH_ALEN);
	npos = nonce + ETH_ALEN;
	*npos++ = gcmp_ipn[5];
	*npos++ = gcmp_ipn[4];
	*npos++ = gcmp_ipn[3];
	*npos++ = gcmp_ipn[2];
	*npos++ = gcmp_ipn[1];
	*npos++ = gcmp_ipn[0];

	if (aes_gmac(key, key_len, nonce, sizeof(nonce),
			data, data_len, mic)) {
		res = _FAIL;
		RTW_ERR("%s : aes_gmac fail!", __func__);
	}

	return res;
}
#endif /* CONFIG_IEEE80211W */


#ifdef CONFIG_TDLS
void _tdls_generate_tpk(void *sta, const u8 *own_addr, const u8 *bssid)
{
	struct sta_info *psta = (struct sta_info *)sta;
	u8 *SNonce = psta->SNonce;
	u8 *ANonce = psta->ANonce;

	u8 key_input[SHA256_MAC_LEN];
	const u8 *nonce[2];
	size_t len[2];
	u8 data[3 * ETH_ALEN];

	/* IEEE Std 802.11z-2010 8.5.9.1:
	 * TPK-Key-Input = SHA-256(min(SNonce, ANonce) || max(SNonce, ANonce))
	 */
	len[0] = 32;
	len[1] = 32;
	if (_rtw_memcmp2(SNonce, ANonce, 32) < 0) {
		nonce[0] = SNonce;
		nonce[1] = ANonce;
	} else {
		nonce[0] = ANonce;
		nonce[1] = SNonce;
	}

	sha256_vector(2, nonce, len, key_input);

	/*
	 * TPK = KDF-Hash-Length(TPK-Key-Input, "TDLS PMK",
	 *	min(MAC_I, MAC_R) || max(MAC_I, MAC_R) || BSSID)
	 */

	if (_rtw_memcmp2(own_addr, psta->cmn.mac_addr, ETH_ALEN) < 0) {
		_rtw_memcpy(data, own_addr, ETH_ALEN);
		_rtw_memcpy(data + ETH_ALEN, psta->cmn.mac_addr, ETH_ALEN);
	} else {
		_rtw_memcpy(data, psta->cmn.mac_addr, ETH_ALEN);
		_rtw_memcpy(data + ETH_ALEN, own_addr, ETH_ALEN);
	}

	_rtw_memcpy(data + 2 * ETH_ALEN, bssid, ETH_ALEN);

	sha256_prf(key_input, SHA256_MAC_LEN, "TDLS PMK", data, sizeof(data), (u8 *)&psta->tpk, sizeof(psta->tpk));
}
#endif /* CONFIG_TDLS */
