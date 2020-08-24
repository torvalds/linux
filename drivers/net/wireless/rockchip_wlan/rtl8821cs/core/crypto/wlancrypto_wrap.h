/* SPDX-License-Identifier: GPL-2.0 */
/*
 * wlantest - IEEE 802.11 protocol monitoring and testing tool
 * Copyright (c) 2010-2013, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef WLANCRYPTO_WRAP_H
#define WLANCRYPTO_WRAP_H

int sha256_vector(size_t num_elem, const u8 *addr[], const size_t *len,
	u8 *mac);

u8* ccmp_decrypt(const u8 *tk, const struct ieee80211_hdr *hdr,
	const u8 *data, size_t data_len, size_t *decrypted_len);
u8* ccmp_encrypt(const u8 *tk, u8 *frame, size_t len, size_t hdrlen, u8 *qos,
	u8 *pn, int keyid, size_t *encrypted_len);
u8* ccmp_encrypt_pv1(const u8 *tk, const u8 *a1, const u8 *a2, const u8 *a3,
	const u8 *frame, size_t len,
	size_t hdrlen, const u8 *pn, int keyid,
	size_t *encrypted_len);
u8* ccmp_256_decrypt(const u8 *tk, const struct ieee80211_hdr *hdr,
	const u8 *data, size_t data_len, size_t *decrypted_len);
u8* ccmp_256_encrypt(const u8 *tk, u8 *frame, size_t len, size_t hdrlen,
	u8 *qos, u8 *pn, int keyid, size_t *encrypted_len);

u8* gcmp_decrypt(const u8 *tk, size_t tk_len, const struct ieee80211_hdr *hdr,
	const u8 *data, size_t data_len, size_t *decrypted_len);
u8* gcmp_encrypt(const u8 *tk, size_t tk_len, const u8 *frame, size_t len,
	size_t hdrlen, const u8 *qos,
	const u8 *pn, int keyid, size_t *encrypted_len);

#endif /* WLANCRYPTO_WRAP_H */
