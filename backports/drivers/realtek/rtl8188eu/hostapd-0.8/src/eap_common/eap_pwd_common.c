/*
 * EAP server/peer: EAP-pwd shared routines
 * Copyright (c) 2010, Dan Harkins <dharkins@lounge.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the BSD license.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation.
 *
 * See README and COPYING for more details.
 */

#include "includes.h"
#include "common.h"
#include "eap_defs.h"
#include "eap_pwd_common.h"

/* The random function H(x) = HMAC-SHA256(0^32, x) */
void H_Init(HMAC_CTX *ctx)
{
	u8 allzero[SHA256_DIGEST_LENGTH];

	os_memset(allzero, 0, SHA256_DIGEST_LENGTH);
	HMAC_Init(ctx, allzero, SHA256_DIGEST_LENGTH, EVP_sha256());
}


void H_Update(HMAC_CTX *ctx, const u8 *data, int len)
{
	HMAC_Update(ctx, data, len);
}


void H_Final(HMAC_CTX *ctx, u8 *digest)
{
	unsigned int mdlen = SHA256_DIGEST_LENGTH;

	HMAC_Final(ctx, digest, &mdlen);
	HMAC_CTX_cleanup(ctx);
}


/* a counter-based KDF based on NIST SP800-108 */
void eap_pwd_kdf(u8 *key, int keylen, u8 *label, int labellen,
		 u8 *result, int resultbitlen)
{
	HMAC_CTX hctx;
	unsigned char digest[SHA256_DIGEST_LENGTH];
	u16 i, ctr, L;
	int resultbytelen, len = 0;
	unsigned int mdlen = SHA256_DIGEST_LENGTH;
	unsigned char mask = 0xff;

	resultbytelen = (resultbitlen + 7)/8;
	ctr = 0;
	L = htons(resultbitlen);
	while (len < resultbytelen) {
		ctr++; i = htons(ctr);
		HMAC_Init(&hctx, key, keylen, EVP_sha256());
		if (ctr > 1)
			HMAC_Update(&hctx, digest, mdlen);
		HMAC_Update(&hctx, (u8 *) &i, sizeof(u16));
		HMAC_Update(&hctx, label, labellen);
		HMAC_Update(&hctx, (u8 *) &L, sizeof(u16));
		HMAC_Final(&hctx, digest, &mdlen);
		if ((len + (int) mdlen) > resultbytelen)
			os_memcpy(result + len, digest, resultbytelen - len);
		else
			os_memcpy(result + len, digest, mdlen);
		len += mdlen;
		HMAC_CTX_cleanup(&hctx);
	}

	/* since we're expanding to a bit length, mask off the excess */
	if (resultbitlen % 8) {
		mask >>= ((resultbytelen * 8) - resultbitlen);
		result[0] &= mask;
	}
}


/*
 * compute a "random" secret point on an elliptic curve based
 * on the password and identities.
 */
int compute_password_element(EAP_PWD_group *grp, u16 num,
			     u8 *password, int password_len,
			     u8 *id_server, int id_server_len,
			     u8 *id_peer, int id_peer_len, u8 *token)
{
	BIGNUM *x_candidate = NULL, *rnd = NULL, *cofactor = NULL;
	HMAC_CTX ctx;
	unsigned char pwe_digest[SHA256_DIGEST_LENGTH], *prfbuf = NULL, ctr;
	int nid, is_odd, primebitlen, primebytelen, ret = 0;

	switch (num) { /* from IANA registry for IKE D-H groups */
        case 19:
		nid = NID_X9_62_prime256v1;
		break;
        case 20:
		nid = NID_secp384r1;
		break;
        case 21:
		nid = NID_secp521r1;
		break;
        case 25:
		nid = NID_X9_62_prime192v1;
		break;
        case 26:
		nid = NID_secp224r1;
		break;
        default:
		wpa_printf(MSG_INFO, "EAP-pwd: unsupported group %d", num);
		return -1;
	}

	grp->pwe = NULL;
	grp->order = NULL;
	grp->prime = NULL;

	if ((grp->group = EC_GROUP_new_by_curve_name(nid)) == NULL) {
		wpa_printf(MSG_INFO, "EAP-pwd: unable to create EC_GROUP");
		goto fail;
	}

	if (((rnd = BN_new()) == NULL) ||
	    ((cofactor = BN_new()) == NULL) ||
	    ((grp->pwe = EC_POINT_new(grp->group)) == NULL) ||
	    ((grp->order = BN_new()) == NULL) ||
	    ((grp->prime = BN_new()) == NULL) ||
	    ((x_candidate = BN_new()) == NULL)) {
		wpa_printf(MSG_INFO, "EAP-pwd: unable to create bignums");
		goto fail;
	}

	if (!EC_GROUP_get_curve_GFp(grp->group, grp->prime, NULL, NULL, NULL))
	{
		wpa_printf(MSG_INFO, "EAP-pwd: unable to get prime for GFp "
			   "curve");
		goto fail;
	}
	if (!EC_GROUP_get_order(grp->group, grp->order, NULL)) {
		wpa_printf(MSG_INFO, "EAP-pwd: unable to get order for curve");
		goto fail;
	}
	if (!EC_GROUP_get_cofactor(grp->group, cofactor, NULL)) {
		wpa_printf(MSG_INFO, "EAP-pwd: unable to get cofactor for "
			   "curve");
		goto fail;
	}
	primebitlen = BN_num_bits(grp->prime);
	primebytelen = BN_num_bytes(grp->prime);
	if ((prfbuf = os_malloc(primebytelen)) == NULL) {
		wpa_printf(MSG_INFO, "EAP-pwd: unable to malloc space for prf "
			   "buffer");
		goto fail;
	}
	os_memset(prfbuf, 0, primebytelen);
	ctr = 0;
	while (1) {
		if (ctr > 10) {
			wpa_printf(MSG_INFO, "EAP-pwd: unable to find random "
				   "point on curve for group %d, something's "
				   "fishy", num);
			goto fail;
		}
		ctr++;

		/*
		 * compute counter-mode password value and stretch to prime
		 *    pwd-seed = H(token | peer-id | server-id | password |
		 *		   counter)
		 */
		H_Init(&ctx);
		H_Update(&ctx, token, sizeof(u32));
		H_Update(&ctx, id_peer, id_peer_len);
		H_Update(&ctx, id_server, id_server_len);
		H_Update(&ctx, password, password_len);
		H_Update(&ctx, &ctr, sizeof(ctr));
		H_Final(&ctx, pwe_digest);

		BN_bin2bn(pwe_digest, SHA256_DIGEST_LENGTH, rnd);

		eap_pwd_kdf(pwe_digest, SHA256_DIGEST_LENGTH,
			    (unsigned char *) "EAP-pwd Hunting And Pecking",
			    os_strlen("EAP-pwd Hunting And Pecking"),
			    prfbuf, primebitlen);

		BN_bin2bn(prfbuf, primebytelen, x_candidate);
		if (BN_ucmp(x_candidate, grp->prime) >= 0)
			continue;

		wpa_hexdump(MSG_DEBUG, "EAP-pwd: x_candidate",
			    prfbuf, primebytelen);

		/*
		 * need to unambiguously identify the solution, if there is
		 * one...
		 */
		if (BN_is_odd(rnd))
			is_odd = 1;
		else
			is_odd = 0;

		/*
		 * solve the quadratic equation, if it's not solvable then we
		 * don't have a point
		 */
		if (!EC_POINT_set_compressed_coordinates_GFp(grp->group,
							     grp->pwe,
							     x_candidate,
							     is_odd, NULL))
			continue;
		/*
		 * If there's a solution to the equation then the point must be
		 * on the curve so why check again explicitly? OpenSSL code
		 * says this is required by X9.62. We're not X9.62 but it can't
		 * hurt just to be sure.
		 */
		if (!EC_POINT_is_on_curve(grp->group, grp->pwe, NULL)) {
			wpa_printf(MSG_INFO, "EAP-pwd: point is not on curve");
			continue;
		}

		if (BN_cmp(cofactor, BN_value_one())) {
			/* make sure the point is not in a small sub-group */
			if (!EC_POINT_mul(grp->group, grp->pwe, NULL, grp->pwe,
					  cofactor, NULL)) {
				wpa_printf(MSG_INFO, "EAP-pwd: cannot "
					   "multiply generator by order");
				continue;
			}
			if (EC_POINT_is_at_infinity(grp->group, grp->pwe)) {
				wpa_printf(MSG_INFO, "EAP-pwd: point is at "
					   "infinity");
				continue;
			}
		}
		/* if we got here then we have a new generator. */
		break;
	}
	wpa_printf(MSG_DEBUG, "EAP-pwd: found a PWE in %d tries", ctr);
	grp->group_num = num;
	if (0) {
 fail:
		EC_GROUP_free(grp->group);
		EC_POINT_free(grp->pwe);
		BN_free(grp->order);
		BN_free(grp->prime);
		os_free(grp);
		grp = NULL;
		ret = 1;
	}
	/* cleanliness and order.... */
	BN_free(cofactor);
	BN_free(x_candidate);
	BN_free(rnd);
	os_free(prfbuf);

	return ret;
}


int compute_keys(EAP_PWD_group *grp, BN_CTX *bnctx, BIGNUM *k,
		 BIGNUM *peer_scalar, BIGNUM *server_scalar,
		 u8 *commit_peer, u8 *commit_server,
		 u32 *ciphersuite, u8 *msk, u8 *emsk)
{
	HMAC_CTX ctx;
	u8 mk[SHA256_DIGEST_LENGTH], *cruft;
	u8 session_id[SHA256_DIGEST_LENGTH + 1];
	u8 msk_emsk[EAP_MSK_LEN + EAP_EMSK_LEN];

	if ((cruft = os_malloc(BN_num_bytes(grp->prime))) == NULL)
		return -1;

	/*
	 * first compute the session-id = TypeCode | H(ciphersuite | scal_p |
	 *	scal_s)
	 */
	session_id[0] = EAP_TYPE_PWD;
	H_Init(&ctx);
	H_Update(&ctx, (u8 *)ciphersuite, sizeof(u32));
	BN_bn2bin(peer_scalar, cruft);
	H_Update(&ctx, cruft, BN_num_bytes(grp->order));
	BN_bn2bin(server_scalar, cruft);
	H_Update(&ctx, cruft, BN_num_bytes(grp->order));
	H_Final(&ctx, &session_id[1]);

	/* then compute MK = H(k | commit-peer | commit-server) */
	H_Init(&ctx);
	os_memset(cruft, 0, BN_num_bytes(grp->prime));
	BN_bn2bin(k, cruft);
	H_Update(&ctx, cruft, BN_num_bytes(grp->prime));
	H_Update(&ctx, commit_peer, SHA256_DIGEST_LENGTH);
	H_Update(&ctx, commit_server, SHA256_DIGEST_LENGTH);
	H_Final(&ctx, mk);

	/* stretch the mk with the session-id to get MSK | EMSK */
	eap_pwd_kdf(mk, SHA256_DIGEST_LENGTH,
		    session_id, SHA256_DIGEST_LENGTH+1,
		    msk_emsk, (EAP_MSK_LEN + EAP_EMSK_LEN) * 8);

	os_memcpy(msk, msk_emsk, EAP_MSK_LEN);
	os_memcpy(emsk, msk_emsk + EAP_MSK_LEN, EAP_EMSK_LEN);

	os_free(cruft);

	return 1;
}
