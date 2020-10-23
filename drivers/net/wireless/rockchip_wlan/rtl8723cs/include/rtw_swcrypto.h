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

#ifndef __RTW_SWCRYPTO_H_
#define __RTW_SWCRYPTO_H_

#define NEW_CRYPTO 1

int _rtw_ccmp_encrypt(u8 *key, u32 key_len, uint hdrlen, u8 *frame, uint plen);
int _rtw_ccmp_decrypt(u8 *key, u32 key_len, uint hdrlen, u8 *frame, uint plen);

int _rtw_gcmp_encrypt(u8 *key, u32 key_len, uint hdrlen, u8 *frame, uint plen);
int _rtw_gcmp_decrypt(u8 *key, u32 key_len, uint hdrlen, u8 *frame, uint plen);

#ifdef CONFIG_RTW_MESH_AEK
int _aes_siv_encrypt(const u8 *key, size_t key_len,
	const u8 *pw, size_t pwlen,
	size_t num_elem, const u8 *addr[], const size_t *len, u8 *out);
int _aes_siv_decrypt(const u8 *key, size_t key_len,
	const u8 *iv_crypt, size_t iv_c_len,
	size_t num_elem, const u8 *addr[], const size_t *len, u8 *out);
#endif

#if defined(CONFIG_IEEE80211W) | defined(CONFIG_TDLS)
u8 _bip_ccmp_protect(const u8 *key, size_t key_len,
	const u8 *data, size_t data_len, u8 *mic);
u8 _bip_gcmp_protect(u8 *whdr_pos, size_t len,
	const u8 *key, size_t key_len,
	const u8 *data, size_t data_len, u8 *mic);
#endif /* CONFIG_IEEE80211W */

#ifdef CONFIG_TDLS
void _tdls_generate_tpk(void *sta, const u8 *own_addr, const u8 *bssid);
#endif /* CONFIG_TDLS */

#endif /* __RTW_SWCRYPTO_H_ */

