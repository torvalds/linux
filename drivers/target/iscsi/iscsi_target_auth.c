// SPDX-License-Identifier: GPL-2.0-or-later
/*******************************************************************************
 * This file houses the main functions for the iSCSI CHAP support
 *
 * (c) Copyright 2007-2013 Datera, Inc.
 *
 * Author: Nicholas A. Bellinger <nab@linux-iscsi.org>
 *
 ******************************************************************************/

#include <crypto/md5.h>
#include <crypto/sha1.h>
#include <crypto/sha2.h>
#include <crypto/sha3.h>
#include <crypto/utils.h>
#include <linux/fips.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/hex.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <target/iscsi/iscsi_target_core.h>
#include "iscsi_target_nego.h"
#include "iscsi_target_auth.h"

static const struct {
	const char *digest_name; /* Used only in log messages */
	unsigned int digest_size;
} chap_digest_algs[] = {
	[CHAP_DIGEST_MD5] = {
		.digest_name = "md5",
		.digest_size = MD5_DIGEST_SIZE,
	},
	[CHAP_DIGEST_SHA1] = {
		.digest_name = "sha1",
		.digest_size = SHA1_DIGEST_SIZE,
	},
	[CHAP_DIGEST_SHA256] = {
		.digest_name = "sha256",
		.digest_size = SHA256_DIGEST_SIZE,
	},
	[CHAP_DIGEST_SHA3_256] = {
		.digest_name = "sha3-256",
		.digest_size = SHA3_256_DIGEST_SIZE,
	},
};

static int chap_digest(int digest_type, u8 chap_id, const char *password,
		       const u8 *challenge, size_t challenge_len,
		       u8 *out_digest)
{
	size_t pass_len = strlen(password);
	size_t data_len = 1 + pass_len + challenge_len;
	u8 *data __free(kfree_sensitive) = kmalloc(data_len, GFP_KERNEL);

	if (!data)
		return -ENOMEM;
	data[0] = chap_id;
	memcpy(&data[1], password, pass_len);
	memcpy(&data[1 + pass_len], challenge, challenge_len);

	switch (digest_type) {
	case CHAP_DIGEST_MD5:
		md5(data, data_len, out_digest);
		return 0;
	case CHAP_DIGEST_SHA1:
		sha1(data, data_len, out_digest);
		return 0;
	case CHAP_DIGEST_SHA256:
		sha256(data, data_len, out_digest);
		return 0;
	case CHAP_DIGEST_SHA3_256:
		sha3_256(data, data_len, out_digest);
		return 0;
	default:
		WARN_ON_ONCE(1);
		return -EINVAL;
	}
}

static int chap_gen_challenge(
	struct iscsit_conn *conn,
	int caller,
	char *c_str,
	unsigned int *c_len)
{
	int ret;
	unsigned char *challenge_asciihex;
	struct iscsi_chap *chap = conn->auth_protocol;

	challenge_asciihex = kzalloc(chap->challenge_len * 2 + 1, GFP_KERNEL);
	if (!challenge_asciihex)
		return -ENOMEM;

	memset(chap->challenge, 0, MAX_CHAP_CHALLENGE_LEN);

	ret = get_random_bytes_wait(chap->challenge, chap->challenge_len);
	if (unlikely(ret))
		goto out;

	bin2hex(challenge_asciihex, chap->challenge,
				chap->challenge_len);
	/*
	 * Set CHAP_C, and copy the generated challenge into c_str.
	 */
	*c_len += sprintf(c_str + *c_len, "CHAP_C=0x%s", challenge_asciihex);
	*c_len += 1;

	pr_debug("[%s] Sending CHAP_C=0x%s\n\n", (caller) ? "server" : "client",
			challenge_asciihex);

out:
	kfree(challenge_asciihex);
	return ret;
}

static int chap_check_algorithm(const char *a_str)
{
	char *tmp, *orig, *token;
	long digest_type;
	int r = CHAP_DIGEST_UNKNOWN;

	tmp = kstrdup(a_str, GFP_KERNEL);
	if (!tmp) {
		pr_err("Memory allocation failed for CHAP_A temporary buffer\n");
		return CHAP_DIGEST_UNKNOWN;
	}
	orig = tmp;

	token = strsep(&tmp, "=");
	if (!token)
		goto out;

	if (strcmp(token, "CHAP_A")) {
		pr_err("Unable to locate CHAP_A key\n");
		goto out;
	}
	while (token) {
		token = strsep(&tmp, ",");
		if (!token)
			goto out;

		if (kstrtol(token, 10, &digest_type))
			continue;

		if (fips_enabled && digest_type == CHAP_DIGEST_MD5)
			continue;

		if (digest_type < 0 ||
		    digest_type >= ARRAY_SIZE(chap_digest_algs) ||
		    chap_digest_algs[digest_type].digest_name == NULL)
			continue;
		pr_debug("Selected %s Algorithm\n",
			 chap_digest_algs[digest_type].digest_name);
		r = digest_type;
		goto out;
	}
out:
	kfree(orig);
	return r;
}

static void chap_close(struct iscsit_conn *conn)
{
	kfree(conn->auth_protocol);
	conn->auth_protocol = NULL;
}

static struct iscsi_chap *chap_server_open(
	struct iscsit_conn *conn,
	struct iscsi_node_auth *auth,
	const char *a_str,
	char *aic_str,
	unsigned int *aic_len)
{
	int digest_type;
	struct iscsi_chap *chap;

	if (!(auth->naf_flags & NAF_USERID_SET) ||
	    !(auth->naf_flags & NAF_PASSWORD_SET)) {
		pr_err("CHAP user or password not set for"
				" Initiator ACL\n");
		return NULL;
	}

	conn->auth_protocol = kzalloc_obj(struct iscsi_chap);
	if (!conn->auth_protocol)
		return NULL;

	chap = conn->auth_protocol;
	digest_type = chap_check_algorithm(a_str);
	if (digest_type == CHAP_DIGEST_UNKNOWN) {
		pr_err("Unsupported CHAP_A value\n");
		chap_close(conn);
		return NULL;
	}

	chap->digest_name = chap_digest_algs[digest_type].digest_name;
	chap->digest_type = digest_type;
	chap->digest_size = chap_digest_algs[digest_type].digest_size;

	/* Tie the challenge length to the digest size */
	chap->challenge_len = chap->digest_size;

	pr_debug("[server] Got CHAP_A=%d\n", digest_type);
	*aic_len = sprintf(aic_str, "CHAP_A=%d", digest_type);
	*aic_len += 1;
	pr_debug("[server] Sending CHAP_A=%d\n", digest_type);

	/*
	 * Set Identifier.
	 */
	chap->id = conn->tpg->tpg_chap_id++;
	*aic_len += sprintf(aic_str + *aic_len, "CHAP_I=%d", chap->id);
	*aic_len += 1;
	pr_debug("[server] Sending CHAP_I=%d\n", chap->id);
	/*
	 * Generate Challenge.
	 */
	if (chap_gen_challenge(conn, 1, aic_str, aic_len) < 0) {
		chap_close(conn);
		return NULL;
	}

	return chap;
}

static const char base64_lookup_table[] =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static int chap_base64_decode(u8 *dst, const char *src, size_t len)
{
	int i, bits = 0, ac = 0;
	const char *p;
	u8 *cp = dst;

	for (i = 0; i < len; i++) {
		if (src[i] == '=')
			return cp - dst;

		p = strchr(base64_lookup_table, src[i]);
		if (p == NULL || src[i] == 0)
			return -2;

		ac <<= 6;
		ac += (p - base64_lookup_table);
		bits += 6;
		if (bits >= 8) {
			*cp++ = (ac >> (bits - 8)) & 0xff;
			ac &= ~(BIT(16) - BIT(bits - 8));
			bits -= 8;
		}
	}
	if (ac)
		return -1;

	return cp - dst;
}

static int chap_server_compute_hash(
	struct iscsit_conn *conn,
	struct iscsi_node_auth *auth,
	char *nr_in_ptr,
	char *nr_out_ptr,
	unsigned int *nr_out_len)
{
	unsigned long id;
	unsigned char type;
	unsigned char identifier[10], *initiatorchg = NULL;
	unsigned char *initiatorchg_binhex = NULL;
	unsigned char *digest = NULL;
	unsigned char *response = NULL;
	unsigned char *client_digest = NULL;
	unsigned char *server_digest = NULL;
	unsigned char chap_n[MAX_CHAP_N_SIZE], chap_r[MAX_RESPONSE_LENGTH];
	size_t compare_len;
	struct iscsi_chap *chap = conn->auth_protocol;
	int auth_ret = -1, ret, initiatorchg_len;

	digest = kzalloc(chap->digest_size, GFP_KERNEL);
	if (!digest) {
		pr_err("Unable to allocate the digest buffer\n");
		goto out;
	}

	response = kzalloc(chap->digest_size * 2 + 2, GFP_KERNEL);
	if (!response) {
		pr_err("Unable to allocate the response buffer\n");
		goto out;
	}

	client_digest = kzalloc(chap->digest_size, GFP_KERNEL);
	if (!client_digest) {
		pr_err("Unable to allocate the client_digest buffer\n");
		goto out;
	}

	server_digest = kzalloc(chap->digest_size, GFP_KERNEL);
	if (!server_digest) {
		pr_err("Unable to allocate the server_digest buffer\n");
		goto out;
	}

	memset(identifier, 0, 10);
	memset(chap_n, 0, MAX_CHAP_N_SIZE);
	memset(chap_r, 0, MAX_RESPONSE_LENGTH);

	initiatorchg = kzalloc(CHAP_CHALLENGE_STR_LEN, GFP_KERNEL);
	if (!initiatorchg) {
		pr_err("Unable to allocate challenge buffer\n");
		goto out;
	}

	initiatorchg_binhex = kzalloc(CHAP_CHALLENGE_STR_LEN, GFP_KERNEL);
	if (!initiatorchg_binhex) {
		pr_err("Unable to allocate initiatorchg_binhex buffer\n");
		goto out;
	}
	/*
	 * Extract CHAP_N.
	 */
	if (extract_param(nr_in_ptr, "CHAP_N", MAX_CHAP_N_SIZE, chap_n,
				&type) < 0) {
		pr_err("Could not find CHAP_N.\n");
		goto out;
	}
	if (type == HEX) {
		pr_err("Could not find CHAP_N.\n");
		goto out;
	}

	/* Include the terminating NULL in the compare */
	compare_len = strlen(auth->userid) + 1;
	if (strncmp(chap_n, auth->userid, compare_len) != 0) {
		pr_err("CHAP_N values do not match!\n");
		goto out;
	}
	pr_debug("[server] Got CHAP_N=%s\n", chap_n);
	/*
	 * Extract CHAP_R.
	 */
	if (extract_param(nr_in_ptr, "CHAP_R", MAX_RESPONSE_LENGTH, chap_r,
				&type) < 0) {
		pr_err("Could not find CHAP_R.\n");
		goto out;
	}

	switch (type) {
	case HEX:
		if (strlen(chap_r) != chap->digest_size * 2) {
			pr_err("Malformed CHAP_R\n");
			goto out;
		}
		if (hex2bin(client_digest, chap_r, chap->digest_size) < 0) {
			pr_err("Malformed CHAP_R: invalid HEX\n");
			goto out;
		}
		break;
	case BASE64: {
		size_t r_len = strlen(chap_r);

		while (r_len > 0 && chap_r[r_len - 1] == '=')
			r_len--;
		if (r_len > DIV_ROUND_UP(chap->digest_size * 4, 3)) {
			pr_err("Malformed CHAP_R: base64 payload too long\n");
			goto out;
		}
		if (chap_base64_decode(client_digest, chap_r, strlen(chap_r)) !=
		    chap->digest_size) {
			pr_err("Malformed CHAP_R: invalid BASE64\n");
			goto out;
		}
		break;
	}
	default:
		pr_err("Could not find CHAP_R\n");
		goto out;
	}

	pr_debug("[server] Got CHAP_R=%s\n", chap_r);

	ret = chap_digest(chap->digest_type, chap->id, auth->password,
			  chap->challenge, chap->challenge_len, server_digest);
	if (ret < 0) {
		pr_err("chap_digest() failed\n");
		goto out;
	}

	bin2hex(response, server_digest, chap->digest_size);
	pr_debug("[server] %s Server Digest: %s\n",
		chap->digest_name, response);

	if (crypto_memneq(server_digest, client_digest, chap->digest_size)) {
		pr_debug("[server] %s Digests do not match!\n\n",
			chap->digest_name);
		goto out;
	} else
		pr_debug("[server] %s Digests match, CHAP connection"
				" successful.\n\n", chap->digest_name);
	/*
	 * One way authentication has succeeded, return now if mutual
	 * authentication is not enabled.
	 */
	if (!auth->authenticate_target) {
		auth_ret = 0;
		goto out;
	}
	/*
	 * Get CHAP_I.
	 */
	ret = extract_param(nr_in_ptr, "CHAP_I", 10, identifier, &type);
	if (ret == -ENOENT) {
		pr_debug("Could not find CHAP_I. Initiator uses One way authentication.\n");
		auth_ret = 0;
		goto out;
	}
	if (ret < 0) {
		pr_err("Could not find CHAP_I.\n");
		goto out;
	}

	if (type == HEX)
		ret = kstrtoul(identifier, 16, &id);
	else if (type == DECIMAL)
		ret = kstrtoul(identifier, 10, &id);
	else
		ret = -EINVAL;

	if (ret < 0) {
		pr_err("kstrtoul() failed for CHAP identifier: %d\n", ret);
		goto out;
	}
	if (id > 255) {
		pr_err("chap identifier: %lu greater than 255\n", id);
		goto out;
	}
	/*
	 * RFC 1994 says Identifier is no more than octet (8 bits).
	 */
	pr_debug("[server] Got CHAP_I=%lu\n", id);
	/*
	 * Get CHAP_C.
	 */
	if (extract_param(nr_in_ptr, "CHAP_C", CHAP_CHALLENGE_STR_LEN,
			initiatorchg, &type) < 0) {
		pr_err("Could not find CHAP_C.\n");
		goto out;
	}

	switch (type) {
	case HEX:
		initiatorchg_len = DIV_ROUND_UP(strlen(initiatorchg), 2);
		if (!initiatorchg_len) {
			pr_err("Unable to convert incoming challenge\n");
			goto out;
		}
		if (initiatorchg_len > 1024) {
			pr_err("CHAP_C exceeds maximum binary size of 1024 bytes\n");
			goto out;
		}

		if (hex2bin(initiatorchg_binhex, initiatorchg,
			    initiatorchg_len) < 0) {
			pr_err("Malformed CHAP_C: invalid HEX\n");
			goto out;
		}
		break;
	case BASE64:
		/*
		 * No overflow check needed: initiatorchg_binhex is
		 * CHAP_CHALLENGE_STR_LEN bytes and extract_param() caps
		 * initiatorchg at CHAP_CHALLENGE_STR_LEN characters, so
		 * the decoded output is at most DIV_ROUND_UP(
		 * (CHAP_CHALLENGE_STR_LEN - 1) * 3, 4) bytes, which is
		 * less than CHAP_CHALLENGE_STR_LEN.
		 */
		initiatorchg_len = chap_base64_decode(initiatorchg_binhex,
						      initiatorchg,
						      strlen(initiatorchg));
		if (initiatorchg_len < 0) {
			pr_err("Malformed CHAP_C: invalid BASE64\n");
			goto out;
		}
		if (!initiatorchg_len) {
			pr_err("Unable to convert incoming challenge\n");
			goto out;
		}
		if (initiatorchg_len > 1024) {
			pr_err("CHAP_C exceeds maximum binary size of 1024 bytes\n");
			goto out;
		}
		break;
	default:
		pr_err("Could not find CHAP_C.\n");
		goto out;
	}

	pr_debug("[server] Got CHAP_C=%s\n", initiatorchg);
	/*
	 * During mutual authentication, the CHAP_C generated by the
	 * initiator must not match the original CHAP_C generated by
	 * the target.
	 */
	if (initiatorchg_len == chap->challenge_len &&
				!memcmp(initiatorchg_binhex, chap->challenge,
				initiatorchg_len)) {
		pr_err("initiator CHAP_C matches target CHAP_C, failing"
		       " login attempt\n");
		goto out;
	}
	/*
	 * Generate CHAP_N and CHAP_R for mutual authentication.
	 */
	ret = chap_digest(chap->digest_type, id, auth->password_mutual,
			  initiatorchg_binhex, initiatorchg_len, digest);
	if (ret < 0) {
		pr_err("chap_digest() failed\n");
		goto out;
	}
	*nr_out_len = sprintf(nr_out_ptr, "CHAP_N=%s", auth->userid_mutual);
	*nr_out_len += 1;
	pr_debug("[server] Sending CHAP_N=%s\n", auth->userid_mutual);
	/*
	 * Convert response from binary hex to ascii hext.
	 */
	bin2hex(response, digest, chap->digest_size);
	*nr_out_len += sprintf(nr_out_ptr + *nr_out_len, "CHAP_R=0x%s",
			response);
	*nr_out_len += 1;
	pr_debug("[server] Sending CHAP_R=0x%s\n", response);
	auth_ret = 0;
out:
	kfree(initiatorchg);
	kfree(initiatorchg_binhex);
	kfree(digest);
	kfree(response);
	kfree(server_digest);
	kfree(client_digest);
	return auth_ret;
}

u32 chap_main_loop(
	struct iscsit_conn *conn,
	struct iscsi_node_auth *auth,
	char *in_text,
	char *out_text,
	int *in_len,
	int *out_len)
{
	struct iscsi_chap *chap = conn->auth_protocol;

	if (!chap) {
		chap = chap_server_open(conn, auth, in_text, out_text, out_len);
		if (!chap)
			return 2;
		chap->chap_state = CHAP_STAGE_SERVER_AIC;
		return 0;
	} else if (chap->chap_state == CHAP_STAGE_SERVER_AIC) {
		convert_null_to_semi(in_text, *in_len);
		if (chap_server_compute_hash(conn, auth, in_text, out_text,
				out_len) < 0) {
			chap_close(conn);
			return 2;
		}
		if (auth->authenticate_target)
			chap->chap_state = CHAP_STAGE_SERVER_NR;
		else
			*out_len = 0;
		chap_close(conn);
		return 1;
	}

	return 2;
}
