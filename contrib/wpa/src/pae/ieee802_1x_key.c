/*
 * IEEE 802.1X-2010 Key Hierarchy
 * Copyright (c) 2013, Qualcomm Atheros, Inc.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 *
 * SAK derivation specified in IEEE Std 802.1X-2010, Clause 6.2
*/

#include "utils/includes.h"

#include "utils/common.h"
#include "crypto/md5.h"
#include "crypto/sha1.h"
#include "crypto/aes_wrap.h"
#include "crypto/crypto.h"
#include "ieee802_1x_key.h"


static void joint_two_mac(const u8 *mac1, const u8 *mac2, u8 *out)
{
	if (os_memcmp(mac1, mac2, ETH_ALEN) < 0) {
		os_memcpy(out, mac1, ETH_ALEN);
		os_memcpy(out + ETH_ALEN, mac2, ETH_ALEN);
	} else {
		os_memcpy(out, mac2, ETH_ALEN);
		os_memcpy(out + ETH_ALEN, mac1, ETH_ALEN);
	}
}


/* IEEE Std 802.1X-2010, 6.2.1 KDF */
static int aes_kdf_128(const u8 *kdk, const char *label, const u8 *context,
		       int ctx_bits, int ret_bits, u8 *ret)
{
	const int h = 128;
	const int r = 8;
	int i, n;
	int lab_len, ctx_len, ret_len, buf_len;
	u8 *buf;

	lab_len = os_strlen(label);
	ctx_len = (ctx_bits + 7) / 8;
	ret_len = ((ret_bits & 0xffff) + 7) / 8;
	buf_len = lab_len + ctx_len + 4;

	os_memset(ret, 0, ret_len);

	n = (ret_bits + h - 1) / h;
	if (n > ((0x1 << r) - 1))
		return -1;

	buf = os_zalloc(buf_len);
	if (buf == NULL)
		return -1;

	os_memcpy(buf + 1, label, lab_len);
	os_memcpy(buf + lab_len + 2, context, ctx_len);
	WPA_PUT_BE16(&buf[buf_len - 2], ret_bits);

	for (i = 0; i < n; i++) {
		buf[0] = (u8) (i + 1);
		if (omac1_aes_128(kdk, buf, buf_len, ret)) {
			os_free(buf);
			return -1;
		}
		ret = ret + h / 8;
	}
	os_free(buf);
	return 0;
}


/********** AES-CMAC-128 **********/
/**
 * ieee802_1x_cak_128bits_aes_cmac
 *
 * IEEE Std 802.1X-2010, 6.2.2
 * CAK = KDF(Key, Label, mac1 | mac2, CAKlength)
 */
int ieee802_1x_cak_128bits_aes_cmac(const u8 *msk, const u8 *mac1,
				    const u8 *mac2, u8 *cak)
{
	u8 context[2 * ETH_ALEN];

	joint_two_mac(mac1, mac2, context);
	return aes_kdf_128(msk, "IEEE8021 EAP CAK",
			   context, sizeof(context) * 8, 128, cak);
}


/**
 * ieee802_1x_ckn_128bits_aes_cmac
 *
 * IEEE Std 802.1X-2010, 6.2.2
 * CKN = KDF(Key, Label, ID | mac1 | mac2, CKNlength)
 */
int ieee802_1x_ckn_128bits_aes_cmac(const u8 *msk, const u8 *mac1,
				    const u8 *mac2, const u8 *sid,
				    size_t sid_bytes, u8 *ckn)
{
	int res;
	u8 *context;
	size_t ctx_len = sid_bytes + ETH_ALEN * 2;

	context = os_zalloc(ctx_len);
	if (!context) {
		wpa_printf(MSG_ERROR, "MKA-%s: out of memory", __func__);
		return -1;
	}
	os_memcpy(context, sid, sid_bytes);
	joint_two_mac(mac1, mac2, context + sid_bytes);

	res = aes_kdf_128(msk, "IEEE8021 EAP CKN", context, ctx_len * 8,
			  128, ckn);
	os_free(context);
	return res;
}


/**
 * ieee802_1x_kek_128bits_aes_cmac
 *
 * IEEE Std 802.1X-2010, 9.3.3
 * KEK = KDF(Key, Label, Keyid, KEKLength)
 */
int ieee802_1x_kek_128bits_aes_cmac(const u8 *cak, const u8 *ckn,
				    size_t ckn_bytes, u8 *kek)
{
	u8 context[16];

	/* First 16 octets of CKN, with null octets appended to pad if needed */
	os_memset(context, 0, sizeof(context));
	os_memcpy(context, ckn, (ckn_bytes < 16) ? ckn_bytes : 16);

	return aes_kdf_128(cak, "IEEE8021 KEK", context, sizeof(context) * 8,
			   128, kek);
}


/**
 * ieee802_1x_ick_128bits_aes_cmac
 *
 * IEEE Std 802.1X-2010, 9.3.3
 * ICK = KDF(Key, Label, Keyid, ICKLength)
 */
int ieee802_1x_ick_128bits_aes_cmac(const u8 *cak, const u8 *ckn,
				    size_t ckn_bytes, u8 *ick)
{
	u8 context[16];

	/* First 16 octets of CKN, with null octets appended to pad if needed */
	os_memset(context, 0, sizeof(context));
	os_memcpy(context, ckn, (ckn_bytes < 16) ? ckn_bytes : 16);

	return aes_kdf_128(cak, "IEEE8021 ICK", context, sizeof(context) * 8,
			   128, ick);
}


/**
 * ieee802_1x_icv_128bits_aes_cmac
 *
 * IEEE Std 802.1X-2010, 9.4.1
 * ICV = AES-CMAC(ICK, M, 128)
 */
int ieee802_1x_icv_128bits_aes_cmac(const u8 *ick, const u8 *msg,
				    size_t msg_bytes, u8 *icv)
{
	if (omac1_aes_128(ick, msg, msg_bytes, icv)) {
		wpa_printf(MSG_ERROR, "MKA: omac1_aes_128 failed");
		return -1;
	}
	return 0;
}


/**
 * ieee802_1x_sak_128bits_aes_cmac
 *
 * IEEE Std 802.1X-2010, 9.8.1
 * SAK = KDF(Key, Label, KS-nonce | MI-value list | KN, SAKLength)
 */
int ieee802_1x_sak_128bits_aes_cmac(const u8 *cak, const u8 *ctx,
				    size_t ctx_bytes, u8 *sak)
{
	return aes_kdf_128(cak, "IEEE8021 SAK", ctx, ctx_bytes * 8, 128, sak);
}
