/* $OpenBSD: sshkey.c,v 1.66 2018/07/03 13:20:25 djm Exp $ */
/*
 * Copyright (c) 2000, 2001 Markus Friedl.  All rights reserved.
 * Copyright (c) 2008 Alexander von Gernler.  All rights reserved.
 * Copyright (c) 2010,2011 Damien Miller.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "includes.h"

#include <sys/types.h>
#include <netinet/in.h>

#ifdef WITH_OPENSSL
#include <openssl/evp.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#endif

#include "crypto_api.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <resolv.h>
#ifdef HAVE_UTIL_H
#include <util.h>
#endif /* HAVE_UTIL_H */

#include "ssh2.h"
#include "ssherr.h"
#include "misc.h"
#include "sshbuf.h"
#include "cipher.h"
#include "digest.h"
#define SSHKEY_INTERNAL
#include "sshkey.h"
#include "sshkey-xmss.h"
#include "match.h"

#include "xmss_fast.h"

#include "openbsd-compat/openssl-compat.h"

/* openssh private key file format */
#define MARK_BEGIN		"-----BEGIN OPENSSH PRIVATE KEY-----\n"
#define MARK_END		"-----END OPENSSH PRIVATE KEY-----\n"
#define MARK_BEGIN_LEN		(sizeof(MARK_BEGIN) - 1)
#define MARK_END_LEN		(sizeof(MARK_END) - 1)
#define KDFNAME			"bcrypt"
#define AUTH_MAGIC		"openssh-key-v1"
#define SALT_LEN		16
#define DEFAULT_CIPHERNAME	"aes256-ctr"
#define	DEFAULT_ROUNDS		16

/* Version identification string for SSH v1 identity files. */
#define LEGACY_BEGIN		"SSH PRIVATE KEY FILE FORMAT 1.1\n"

int	sshkey_private_serialize_opt(const struct sshkey *key,
    struct sshbuf *buf, enum sshkey_serialize_rep);
static int sshkey_from_blob_internal(struct sshbuf *buf,
    struct sshkey **keyp, int allow_cert);

/* Supported key types */
struct keytype {
	const char *name;
	const char *shortname;
	const char *sigalg;
	int type;
	int nid;
	int cert;
	int sigonly;
};
static const struct keytype keytypes[] = {
	{ "ssh-ed25519", "ED25519", NULL, KEY_ED25519, 0, 0, 0 },
	{ "ssh-ed25519-cert-v01@openssh.com", "ED25519-CERT", NULL,
	    KEY_ED25519_CERT, 0, 1, 0 },
#ifdef WITH_XMSS
	{ "ssh-xmss@openssh.com", "XMSS", NULL, KEY_XMSS, 0, 0, 0 },
	{ "ssh-xmss-cert-v01@openssh.com", "XMSS-CERT", NULL,
	    KEY_XMSS_CERT, 0, 1, 0 },
#endif /* WITH_XMSS */
#ifdef WITH_OPENSSL
	{ "ssh-rsa", "RSA", NULL, KEY_RSA, 0, 0, 0 },
	{ "rsa-sha2-256", "RSA", NULL, KEY_RSA, 0, 0, 1 },
	{ "rsa-sha2-512", "RSA", NULL, KEY_RSA, 0, 0, 1 },
	{ "ssh-dss", "DSA", NULL, KEY_DSA, 0, 0, 0 },
# ifdef OPENSSL_HAS_ECC
	{ "ecdsa-sha2-nistp256", "ECDSA", NULL,
	    KEY_ECDSA, NID_X9_62_prime256v1, 0, 0 },
	{ "ecdsa-sha2-nistp384", "ECDSA", NULL,
	    KEY_ECDSA, NID_secp384r1, 0, 0 },
#  ifdef OPENSSL_HAS_NISTP521
	{ "ecdsa-sha2-nistp521", "ECDSA", NULL,
	    KEY_ECDSA, NID_secp521r1, 0, 0 },
#  endif /* OPENSSL_HAS_NISTP521 */
# endif /* OPENSSL_HAS_ECC */
	{ "ssh-rsa-cert-v01@openssh.com", "RSA-CERT", NULL,
	    KEY_RSA_CERT, 0, 1, 0 },
	{ "rsa-sha2-256-cert-v01@openssh.com", "RSA-CERT",
	    "ssh-rsa-sha2-256", KEY_RSA_CERT, 0, 1, 1 },
	{ "rsa-sha2-512-cert-v01@openssh.com", "RSA-CERT",
	    "ssh-rsa-sha2-512", KEY_RSA_CERT, 0, 1, 1 },
	{ "ssh-dss-cert-v01@openssh.com", "DSA-CERT", NULL,
	    KEY_DSA_CERT, 0, 1, 0 },
	{ "ssh-rsa-cert-v01@openssh.com", "RSA-CERT", NULL,
	    KEY_RSA_CERT, 0, 1, 0 },
	{ "rsa-sha2-256-cert-v01@openssh.com", "RSA-CERT",
	    "ssh-rsa-sha2-256", KEY_RSA_CERT, 0, 1, 1 },
	{ "rsa-sha2-512-cert-v01@openssh.com", "RSA-CERT",
	    "ssh-rsa-sha2-512", KEY_RSA_CERT, 0, 1, 1 },
	{ "ssh-dss-cert-v01@openssh.com", "DSA-CERT", NULL,
	    KEY_DSA_CERT, 0, 1, 0 },
# ifdef OPENSSL_HAS_ECC
	{ "ecdsa-sha2-nistp256-cert-v01@openssh.com", "ECDSA-CERT", NULL,
	    KEY_ECDSA_CERT, NID_X9_62_prime256v1, 1, 0 },
	{ "ecdsa-sha2-nistp384-cert-v01@openssh.com", "ECDSA-CERT", NULL,
	    KEY_ECDSA_CERT, NID_secp384r1, 1, 0 },
#  ifdef OPENSSL_HAS_NISTP521
	{ "ecdsa-sha2-nistp521-cert-v01@openssh.com", "ECDSA-CERT", NULL,
	   KEY_ECDSA_CERT, NID_secp521r1, 1, 0 },
#  endif /* OPENSSL_HAS_NISTP521 */
# endif /* OPENSSL_HAS_ECC */
#endif /* WITH_OPENSSL */
	{ NULL, NULL, NULL, -1, -1, 0, 0 }
};

const char *
sshkey_type(const struct sshkey *k)
{
	const struct keytype *kt;

	for (kt = keytypes; kt->type != -1; kt++) {
		if (kt->type == k->type)
			return kt->shortname;
	}
	return "unknown";
}

static const char *
sshkey_ssh_name_from_type_nid(int type, int nid)
{
	const struct keytype *kt;

	for (kt = keytypes; kt->type != -1; kt++) {
		if (kt->type == type && (kt->nid == 0 || kt->nid == nid))
			return kt->name;
	}
	return "ssh-unknown";
}

int
sshkey_type_is_cert(int type)
{
	const struct keytype *kt;

	for (kt = keytypes; kt->type != -1; kt++) {
		if (kt->type == type)
			return kt->cert;
	}
	return 0;
}

const char *
sshkey_ssh_name(const struct sshkey *k)
{
	return sshkey_ssh_name_from_type_nid(k->type, k->ecdsa_nid);
}

const char *
sshkey_ssh_name_plain(const struct sshkey *k)
{
	return sshkey_ssh_name_from_type_nid(sshkey_type_plain(k->type),
	    k->ecdsa_nid);
}

int
sshkey_type_from_name(const char *name)
{
	const struct keytype *kt;

	for (kt = keytypes; kt->type != -1; kt++) {
		/* Only allow shortname matches for plain key types */
		if ((kt->name != NULL && strcmp(name, kt->name) == 0) ||
		    (!kt->cert && strcasecmp(kt->shortname, name) == 0))
			return kt->type;
	}
	return KEY_UNSPEC;
}

int
sshkey_ecdsa_nid_from_name(const char *name)
{
	const struct keytype *kt;

	for (kt = keytypes; kt->type != -1; kt++) {
		if (kt->type != KEY_ECDSA && kt->type != KEY_ECDSA_CERT)
			continue;
		if (kt->name != NULL && strcmp(name, kt->name) == 0)
			return kt->nid;
	}
	return -1;
}

char *
sshkey_alg_list(int certs_only, int plain_only, int include_sigonly, char sep)
{
	char *tmp, *ret = NULL;
	size_t nlen, rlen = 0;
	const struct keytype *kt;

	for (kt = keytypes; kt->type != -1; kt++) {
		if (kt->name == NULL)
			continue;
		if (!include_sigonly && kt->sigonly)
			continue;
		if ((certs_only && !kt->cert) || (plain_only && kt->cert))
			continue;
		if (ret != NULL)
			ret[rlen++] = sep;
		nlen = strlen(kt->name);
		if ((tmp = realloc(ret, rlen + nlen + 2)) == NULL) {
			free(ret);
			return NULL;
		}
		ret = tmp;
		memcpy(ret + rlen, kt->name, nlen + 1);
		rlen += nlen;
	}
	return ret;
}

int
sshkey_names_valid2(const char *names, int allow_wildcard)
{
	char *s, *cp, *p;
	const struct keytype *kt;
	int type;

	if (names == NULL || strcmp(names, "") == 0)
		return 0;
	if ((s = cp = strdup(names)) == NULL)
		return 0;
	for ((p = strsep(&cp, ",")); p && *p != '\0';
	    (p = strsep(&cp, ","))) {
		type = sshkey_type_from_name(p);
		if (type == KEY_UNSPEC) {
			if (allow_wildcard) {
				/*
				 * Try matching key types against the string.
				 * If any has a positive or negative match then
				 * the component is accepted.
				 */
				for (kt = keytypes; kt->type != -1; kt++) {
					if (match_pattern_list(kt->name,
					    p, 0) != 0)
						break;
				}
				if (kt->type != -1)
					continue;
			}
			free(s);
			return 0;
		}
	}
	free(s);
	return 1;
}

u_int
sshkey_size(const struct sshkey *k)
{
#ifdef WITH_OPENSSL
	const BIGNUM *rsa_n, *dsa_p;
#endif /* WITH_OPENSSL */

	switch (k->type) {
#ifdef WITH_OPENSSL
	case KEY_RSA:
	case KEY_RSA_CERT:
		if (k->rsa == NULL)
			return 0;
		RSA_get0_key(k->rsa, &rsa_n, NULL, NULL);
		return BN_num_bits(rsa_n);
	case KEY_DSA:
	case KEY_DSA_CERT:
		if (k->dsa == NULL)
			return 0;
		DSA_get0_pqg(k->dsa, &dsa_p, NULL, NULL);
		return BN_num_bits(dsa_p);
	case KEY_ECDSA:
	case KEY_ECDSA_CERT:
		return sshkey_curve_nid_to_bits(k->ecdsa_nid);
#endif /* WITH_OPENSSL */
	case KEY_ED25519:
	case KEY_ED25519_CERT:
	case KEY_XMSS:
	case KEY_XMSS_CERT:
		return 256;	/* XXX */
	}
	return 0;
}

static int
sshkey_type_is_valid_ca(int type)
{
	switch (type) {
	case KEY_RSA:
	case KEY_DSA:
	case KEY_ECDSA:
	case KEY_ED25519:
	case KEY_XMSS:
		return 1;
	default:
		return 0;
	}
}

int
sshkey_is_cert(const struct sshkey *k)
{
	if (k == NULL)
		return 0;
	return sshkey_type_is_cert(k->type);
}

/* Return the cert-less equivalent to a certified key type */
int
sshkey_type_plain(int type)
{
	switch (type) {
	case KEY_RSA_CERT:
		return KEY_RSA;
	case KEY_DSA_CERT:
		return KEY_DSA;
	case KEY_ECDSA_CERT:
		return KEY_ECDSA;
	case KEY_ED25519_CERT:
		return KEY_ED25519;
	case KEY_XMSS_CERT:
		return KEY_XMSS;
	default:
		return type;
	}
}

#ifdef WITH_OPENSSL
/* XXX: these are really begging for a table-driven approach */
int
sshkey_curve_name_to_nid(const char *name)
{
	if (strcmp(name, "nistp256") == 0)
		return NID_X9_62_prime256v1;
	else if (strcmp(name, "nistp384") == 0)
		return NID_secp384r1;
# ifdef OPENSSL_HAS_NISTP521
	else if (strcmp(name, "nistp521") == 0)
		return NID_secp521r1;
# endif /* OPENSSL_HAS_NISTP521 */
	else
		return -1;
}

u_int
sshkey_curve_nid_to_bits(int nid)
{
	switch (nid) {
	case NID_X9_62_prime256v1:
		return 256;
	case NID_secp384r1:
		return 384;
# ifdef OPENSSL_HAS_NISTP521
	case NID_secp521r1:
		return 521;
# endif /* OPENSSL_HAS_NISTP521 */
	default:
		return 0;
	}
}

int
sshkey_ecdsa_bits_to_nid(int bits)
{
	switch (bits) {
	case 256:
		return NID_X9_62_prime256v1;
	case 384:
		return NID_secp384r1;
# ifdef OPENSSL_HAS_NISTP521
	case 521:
		return NID_secp521r1;
# endif /* OPENSSL_HAS_NISTP521 */
	default:
		return -1;
	}
}

const char *
sshkey_curve_nid_to_name(int nid)
{
	switch (nid) {
	case NID_X9_62_prime256v1:
		return "nistp256";
	case NID_secp384r1:
		return "nistp384";
# ifdef OPENSSL_HAS_NISTP521
	case NID_secp521r1:
		return "nistp521";
# endif /* OPENSSL_HAS_NISTP521 */
	default:
		return NULL;
	}
}

int
sshkey_ec_nid_to_hash_alg(int nid)
{
	int kbits = sshkey_curve_nid_to_bits(nid);

	if (kbits <= 0)
		return -1;

	/* RFC5656 section 6.2.1 */
	if (kbits <= 256)
		return SSH_DIGEST_SHA256;
	else if (kbits <= 384)
		return SSH_DIGEST_SHA384;
	else
		return SSH_DIGEST_SHA512;
}
#endif /* WITH_OPENSSL */

static void
cert_free(struct sshkey_cert *cert)
{
	u_int i;

	if (cert == NULL)
		return;
	sshbuf_free(cert->certblob);
	sshbuf_free(cert->critical);
	sshbuf_free(cert->extensions);
	free(cert->key_id);
	for (i = 0; i < cert->nprincipals; i++)
		free(cert->principals[i]);
	free(cert->principals);
	sshkey_free(cert->signature_key);
	freezero(cert, sizeof(*cert));
}

static struct sshkey_cert *
cert_new(void)
{
	struct sshkey_cert *cert;

	if ((cert = calloc(1, sizeof(*cert))) == NULL)
		return NULL;
	if ((cert->certblob = sshbuf_new()) == NULL ||
	    (cert->critical = sshbuf_new()) == NULL ||
	    (cert->extensions = sshbuf_new()) == NULL) {
		cert_free(cert);
		return NULL;
	}
	cert->key_id = NULL;
	cert->principals = NULL;
	cert->signature_key = NULL;
	return cert;
}

struct sshkey *
sshkey_new(int type)
{
	struct sshkey *k;
#ifdef WITH_OPENSSL
	RSA *rsa;
	DSA *dsa;
#endif /* WITH_OPENSSL */

	if ((k = calloc(1, sizeof(*k))) == NULL)
		return NULL;
	k->type = type;
	k->ecdsa = NULL;
	k->ecdsa_nid = -1;
	k->dsa = NULL;
	k->rsa = NULL;
	k->cert = NULL;
	k->ed25519_sk = NULL;
	k->ed25519_pk = NULL;
	k->xmss_sk = NULL;
	k->xmss_pk = NULL;
	switch (k->type) {
#ifdef WITH_OPENSSL
	case KEY_RSA:
	case KEY_RSA_CERT:
		if ((rsa = RSA_new()) == NULL) {
			free(k);
			return NULL;
		}
		k->rsa = rsa;
		break;
	case KEY_DSA:
	case KEY_DSA_CERT:
		if ((dsa = DSA_new()) == NULL) {
			free(k);
			return NULL;
		}
		k->dsa = dsa;
		break;
	case KEY_ECDSA:
	case KEY_ECDSA_CERT:
		/* Cannot do anything until we know the group */
		break;
#endif /* WITH_OPENSSL */
	case KEY_ED25519:
	case KEY_ED25519_CERT:
	case KEY_XMSS:
	case KEY_XMSS_CERT:
		/* no need to prealloc */
		break;
	case KEY_UNSPEC:
		break;
	default:
		free(k);
		return NULL;
	}

	if (sshkey_is_cert(k)) {
		if ((k->cert = cert_new()) == NULL) {
			sshkey_free(k);
			return NULL;
		}
	}

	return k;
}

/* XXX garbage-collect this API */
struct sshkey *
sshkey_new_private(int type)
{
	struct sshkey *k = sshkey_new(type);

	if (k == NULL)
		return NULL;
	return k;
}

void
sshkey_free(struct sshkey *k)
{
	if (k == NULL)
		return;
	switch (k->type) {
#ifdef WITH_OPENSSL
	case KEY_RSA:
	case KEY_RSA_CERT:
		RSA_free(k->rsa);
		k->rsa = NULL;
		break;
	case KEY_DSA:
	case KEY_DSA_CERT:
		DSA_free(k->dsa);
		k->dsa = NULL;
		break;
# ifdef OPENSSL_HAS_ECC
	case KEY_ECDSA:
	case KEY_ECDSA_CERT:
		EC_KEY_free(k->ecdsa);
		k->ecdsa = NULL;
		break;
# endif /* OPENSSL_HAS_ECC */
#endif /* WITH_OPENSSL */
	case KEY_ED25519:
	case KEY_ED25519_CERT:
		freezero(k->ed25519_pk, ED25519_PK_SZ);
		k->ed25519_pk = NULL;
		freezero(k->ed25519_sk, ED25519_SK_SZ);
		k->ed25519_sk = NULL;
		break;
#ifdef WITH_XMSS
	case KEY_XMSS:
	case KEY_XMSS_CERT:
		freezero(k->xmss_pk, sshkey_xmss_pklen(k));
		k->xmss_pk = NULL;
		freezero(k->xmss_sk, sshkey_xmss_sklen(k));
		k->xmss_sk = NULL;
		sshkey_xmss_free_state(k);
		free(k->xmss_name);
		k->xmss_name = NULL;
		free(k->xmss_filename);
		k->xmss_filename = NULL;
		break;
#endif /* WITH_XMSS */
	case KEY_UNSPEC:
		break;
	default:
		break;
	}
	if (sshkey_is_cert(k))
		cert_free(k->cert);
	freezero(k, sizeof(*k));
}

static int
cert_compare(struct sshkey_cert *a, struct sshkey_cert *b)
{
	if (a == NULL && b == NULL)
		return 1;
	if (a == NULL || b == NULL)
		return 0;
	if (sshbuf_len(a->certblob) != sshbuf_len(b->certblob))
		return 0;
	if (timingsafe_bcmp(sshbuf_ptr(a->certblob), sshbuf_ptr(b->certblob),
	    sshbuf_len(a->certblob)) != 0)
		return 0;
	return 1;
}

/*
 * Compare public portions of key only, allowing comparisons between
 * certificates and plain keys too.
 */
int
sshkey_equal_public(const struct sshkey *a, const struct sshkey *b)
{
#if defined(WITH_OPENSSL)
	const BIGNUM *rsa_e_a, *rsa_n_a;
	const BIGNUM *rsa_e_b, *rsa_n_b;
	const BIGNUM *dsa_p_a, *dsa_q_a, *dsa_g_a, *dsa_pub_key_a;
	const BIGNUM *dsa_p_b, *dsa_q_b, *dsa_g_b, *dsa_pub_key_b;
# if defined(OPENSSL_HAS_ECC)
	BN_CTX *bnctx;
# endif /* OPENSSL_HAS_ECC */
#endif /* WITH_OPENSSL */

	if (a == NULL || b == NULL ||
	    sshkey_type_plain(a->type) != sshkey_type_plain(b->type))
		return 0;

	switch (a->type) {
#ifdef WITH_OPENSSL
	case KEY_RSA_CERT:
	case KEY_RSA:
		if (a->rsa == NULL || b->rsa == NULL)
			return 0;
		RSA_get0_key(a->rsa, &rsa_n_a, &rsa_e_a, NULL);
		RSA_get0_key(b->rsa, &rsa_n_b, &rsa_e_b, NULL);
		return BN_cmp(rsa_e_a, rsa_e_b) == 0 &&
		    BN_cmp(rsa_n_a, rsa_n_b) == 0;
	case KEY_DSA_CERT:
	case KEY_DSA:
		if (a->dsa == NULL || b->dsa == NULL)
			return 0;
		DSA_get0_pqg(a->dsa, &dsa_p_a, &dsa_q_a, &dsa_g_a);
		DSA_get0_pqg(b->dsa, &dsa_p_b, &dsa_q_b, &dsa_g_b);
		DSA_get0_key(a->dsa, &dsa_pub_key_a, NULL);
		DSA_get0_key(b->dsa, &dsa_pub_key_b, NULL);
		return BN_cmp(dsa_p_a, dsa_p_b) == 0 &&
		    BN_cmp(dsa_q_a, dsa_q_b) == 0 &&
		    BN_cmp(dsa_g_a, dsa_g_b) == 0 &&
		    BN_cmp(dsa_pub_key_a, dsa_pub_key_b) == 0;
# ifdef OPENSSL_HAS_ECC
	case KEY_ECDSA_CERT:
	case KEY_ECDSA:
		if (a->ecdsa == NULL || b->ecdsa == NULL ||
		    EC_KEY_get0_public_key(a->ecdsa) == NULL ||
		    EC_KEY_get0_public_key(b->ecdsa) == NULL)
			return 0;
		if ((bnctx = BN_CTX_new()) == NULL)
			return 0;
		if (EC_GROUP_cmp(EC_KEY_get0_group(a->ecdsa),
		    EC_KEY_get0_group(b->ecdsa), bnctx) != 0 ||
		    EC_POINT_cmp(EC_KEY_get0_group(a->ecdsa),
		    EC_KEY_get0_public_key(a->ecdsa),
		    EC_KEY_get0_public_key(b->ecdsa), bnctx) != 0) {
			BN_CTX_free(bnctx);
			return 0;
		}
		BN_CTX_free(bnctx);
		return 1;
# endif /* OPENSSL_HAS_ECC */
#endif /* WITH_OPENSSL */
	case KEY_ED25519:
	case KEY_ED25519_CERT:
		return a->ed25519_pk != NULL && b->ed25519_pk != NULL &&
		    memcmp(a->ed25519_pk, b->ed25519_pk, ED25519_PK_SZ) == 0;
#ifdef WITH_XMSS
	case KEY_XMSS:
	case KEY_XMSS_CERT:
		return a->xmss_pk != NULL && b->xmss_pk != NULL &&
		    sshkey_xmss_pklen(a) == sshkey_xmss_pklen(b) &&
		    memcmp(a->xmss_pk, b->xmss_pk, sshkey_xmss_pklen(a)) == 0;
#endif /* WITH_XMSS */
	default:
		return 0;
	}
	/* NOTREACHED */
}

int
sshkey_equal(const struct sshkey *a, const struct sshkey *b)
{
	if (a == NULL || b == NULL || a->type != b->type)
		return 0;
	if (sshkey_is_cert(a)) {
		if (!cert_compare(a->cert, b->cert))
			return 0;
	}
	return sshkey_equal_public(a, b);
}

static int
to_blob_buf(const struct sshkey *key, struct sshbuf *b, int force_plain,
  enum sshkey_serialize_rep opts)
{
	int type, ret = SSH_ERR_INTERNAL_ERROR;
	const char *typename;
#ifdef WITH_OPENSSL
	const BIGNUM *rsa_n, *rsa_e, *dsa_p, *dsa_q, *dsa_g, *dsa_pub_key;
#endif /* WITH_OPENSSL */

	if (key == NULL)
		return SSH_ERR_INVALID_ARGUMENT;

	if (sshkey_is_cert(key)) {
		if (key->cert == NULL)
			return SSH_ERR_EXPECTED_CERT;
		if (sshbuf_len(key->cert->certblob) == 0)
			return SSH_ERR_KEY_LACKS_CERTBLOB;
	}
	type = force_plain ? sshkey_type_plain(key->type) : key->type;
	typename = sshkey_ssh_name_from_type_nid(type, key->ecdsa_nid);

	switch (type) {
#ifdef WITH_OPENSSL
	case KEY_DSA_CERT:
	case KEY_ECDSA_CERT:
	case KEY_RSA_CERT:
#endif /* WITH_OPENSSL */
	case KEY_ED25519_CERT:
#ifdef WITH_XMSS
	case KEY_XMSS_CERT:
#endif /* WITH_XMSS */
		/* Use the existing blob */
		/* XXX modified flag? */
		if ((ret = sshbuf_putb(b, key->cert->certblob)) != 0)
			return ret;
		break;
#ifdef WITH_OPENSSL
	case KEY_DSA:
		if (key->dsa == NULL)
			return SSH_ERR_INVALID_ARGUMENT;
		DSA_get0_pqg(key->dsa, &dsa_p, &dsa_q, &dsa_g);
		DSA_get0_key(key->dsa, &dsa_pub_key, NULL);
		if ((ret = sshbuf_put_cstring(b, typename)) != 0 ||
		    (ret = sshbuf_put_bignum2(b, dsa_p)) != 0 ||
		    (ret = sshbuf_put_bignum2(b, dsa_q)) != 0 ||
		    (ret = sshbuf_put_bignum2(b, dsa_g)) != 0 ||
		    (ret = sshbuf_put_bignum2(b, dsa_pub_key)) != 0)
			return ret;
		break;
# ifdef OPENSSL_HAS_ECC
	case KEY_ECDSA:
		if (key->ecdsa == NULL)
			return SSH_ERR_INVALID_ARGUMENT;
		if ((ret = sshbuf_put_cstring(b, typename)) != 0 ||
		    (ret = sshbuf_put_cstring(b,
		    sshkey_curve_nid_to_name(key->ecdsa_nid))) != 0 ||
		    (ret = sshbuf_put_eckey(b, key->ecdsa)) != 0)
			return ret;
		break;
# endif
	case KEY_RSA:
		if (key->rsa == NULL)
			return SSH_ERR_INVALID_ARGUMENT;
		RSA_get0_key(key->rsa, &rsa_n, &rsa_e, NULL);
		if ((ret = sshbuf_put_cstring(b, typename)) != 0 ||
		    (ret = sshbuf_put_bignum2(b, rsa_e)) != 0 ||
		    (ret = sshbuf_put_bignum2(b, rsa_n)) != 0)
			return ret;
		break;
#endif /* WITH_OPENSSL */
	case KEY_ED25519:
		if (key->ed25519_pk == NULL)
			return SSH_ERR_INVALID_ARGUMENT;
		if ((ret = sshbuf_put_cstring(b, typename)) != 0 ||
		    (ret = sshbuf_put_string(b,
		    key->ed25519_pk, ED25519_PK_SZ)) != 0)
			return ret;
		break;
#ifdef WITH_XMSS
	case KEY_XMSS:
		if (key->xmss_name == NULL || key->xmss_pk == NULL ||
		    sshkey_xmss_pklen(key) == 0)
			return SSH_ERR_INVALID_ARGUMENT;
		if ((ret = sshbuf_put_cstring(b, typename)) != 0 ||
		    (ret = sshbuf_put_cstring(b, key->xmss_name)) != 0 ||
		    (ret = sshbuf_put_string(b,
		    key->xmss_pk, sshkey_xmss_pklen(key))) != 0 ||
		    (ret = sshkey_xmss_serialize_pk_info(key, b, opts)) != 0)
			return ret;
		break;
#endif /* WITH_XMSS */
	default:
		return SSH_ERR_KEY_TYPE_UNKNOWN;
	}
	return 0;
}

int
sshkey_putb(const struct sshkey *key, struct sshbuf *b)
{
	return to_blob_buf(key, b, 0, SSHKEY_SERIALIZE_DEFAULT);
}

int
sshkey_puts_opts(const struct sshkey *key, struct sshbuf *b,
    enum sshkey_serialize_rep opts)
{
	struct sshbuf *tmp;
	int r;

	if ((tmp = sshbuf_new()) == NULL)
		return SSH_ERR_ALLOC_FAIL;
	r = to_blob_buf(key, tmp, 0, opts);
	if (r == 0)
		r = sshbuf_put_stringb(b, tmp);
	sshbuf_free(tmp);
	return r;
}

int
sshkey_puts(const struct sshkey *key, struct sshbuf *b)
{
	return sshkey_puts_opts(key, b, SSHKEY_SERIALIZE_DEFAULT);
}

int
sshkey_putb_plain(const struct sshkey *key, struct sshbuf *b)
{
	return to_blob_buf(key, b, 1, SSHKEY_SERIALIZE_DEFAULT);
}

static int
to_blob(const struct sshkey *key, u_char **blobp, size_t *lenp, int force_plain,
    enum sshkey_serialize_rep opts)
{
	int ret = SSH_ERR_INTERNAL_ERROR;
	size_t len;
	struct sshbuf *b = NULL;

	if (lenp != NULL)
		*lenp = 0;
	if (blobp != NULL)
		*blobp = NULL;
	if ((b = sshbuf_new()) == NULL)
		return SSH_ERR_ALLOC_FAIL;
	if ((ret = to_blob_buf(key, b, force_plain, opts)) != 0)
		goto out;
	len = sshbuf_len(b);
	if (lenp != NULL)
		*lenp = len;
	if (blobp != NULL) {
		if ((*blobp = malloc(len)) == NULL) {
			ret = SSH_ERR_ALLOC_FAIL;
			goto out;
		}
		memcpy(*blobp, sshbuf_ptr(b), len);
	}
	ret = 0;
 out:
	sshbuf_free(b);
	return ret;
}

int
sshkey_to_blob(const struct sshkey *key, u_char **blobp, size_t *lenp)
{
	return to_blob(key, blobp, lenp, 0, SSHKEY_SERIALIZE_DEFAULT);
}

int
sshkey_plain_to_blob(const struct sshkey *key, u_char **blobp, size_t *lenp)
{
	return to_blob(key, blobp, lenp, 1, SSHKEY_SERIALIZE_DEFAULT);
}

int
sshkey_fingerprint_raw(const struct sshkey *k, int dgst_alg,
    u_char **retp, size_t *lenp)
{
	u_char *blob = NULL, *ret = NULL;
	size_t blob_len = 0;
	int r = SSH_ERR_INTERNAL_ERROR;

	if (retp != NULL)
		*retp = NULL;
	if (lenp != NULL)
		*lenp = 0;
	if (ssh_digest_bytes(dgst_alg) == 0) {
		r = SSH_ERR_INVALID_ARGUMENT;
		goto out;
	}
	if ((r = to_blob(k, &blob, &blob_len, 1, SSHKEY_SERIALIZE_DEFAULT))
	    != 0)
		goto out;
	if ((ret = calloc(1, SSH_DIGEST_MAX_LENGTH)) == NULL) {
		r = SSH_ERR_ALLOC_FAIL;
		goto out;
	}
	if ((r = ssh_digest_memory(dgst_alg, blob, blob_len,
	    ret, SSH_DIGEST_MAX_LENGTH)) != 0)
		goto out;
	/* success */
	if (retp != NULL) {
		*retp = ret;
		ret = NULL;
	}
	if (lenp != NULL)
		*lenp = ssh_digest_bytes(dgst_alg);
	r = 0;
 out:
	free(ret);
	if (blob != NULL) {
		explicit_bzero(blob, blob_len);
		free(blob);
	}
	return r;
}

static char *
fingerprint_b64(const char *alg, u_char *dgst_raw, size_t dgst_raw_len)
{
	char *ret;
	size_t plen = strlen(alg) + 1;
	size_t rlen = ((dgst_raw_len + 2) / 3) * 4 + plen + 1;
	int r;

	if (dgst_raw_len > 65536 || (ret = calloc(1, rlen)) == NULL)
		return NULL;
	strlcpy(ret, alg, rlen);
	strlcat(ret, ":", rlen);
	if (dgst_raw_len == 0)
		return ret;
	if ((r = b64_ntop(dgst_raw, dgst_raw_len,
	    ret + plen, rlen - plen)) == -1) {
		freezero(ret, rlen);
		return NULL;
	}
	/* Trim padding characters from end */
	ret[strcspn(ret, "=")] = '\0';
	return ret;
}

static char *
fingerprint_hex(const char *alg, u_char *dgst_raw, size_t dgst_raw_len)
{
	char *retval, hex[5];
	size_t i, rlen = dgst_raw_len * 3 + strlen(alg) + 2;

	if (dgst_raw_len > 65536 || (retval = calloc(1, rlen)) == NULL)
		return NULL;
	strlcpy(retval, alg, rlen);
	strlcat(retval, ":", rlen);
	for (i = 0; i < dgst_raw_len; i++) {
		snprintf(hex, sizeof(hex), "%s%02x",
		    i > 0 ? ":" : "", dgst_raw[i]);
		strlcat(retval, hex, rlen);
	}
	return retval;
}

static char *
fingerprint_bubblebabble(u_char *dgst_raw, size_t dgst_raw_len)
{
	char vowels[] = { 'a', 'e', 'i', 'o', 'u', 'y' };
	char consonants[] = { 'b', 'c', 'd', 'f', 'g', 'h', 'k', 'l', 'm',
	    'n', 'p', 'r', 's', 't', 'v', 'z', 'x' };
	u_int i, j = 0, rounds, seed = 1;
	char *retval;

	rounds = (dgst_raw_len / 2) + 1;
	if ((retval = calloc(rounds, 6)) == NULL)
		return NULL;
	retval[j++] = 'x';
	for (i = 0; i < rounds; i++) {
		u_int idx0, idx1, idx2, idx3, idx4;
		if ((i + 1 < rounds) || (dgst_raw_len % 2 != 0)) {
			idx0 = (((((u_int)(dgst_raw[2 * i])) >> 6) & 3) +
			    seed) % 6;
			idx1 = (((u_int)(dgst_raw[2 * i])) >> 2) & 15;
			idx2 = ((((u_int)(dgst_raw[2 * i])) & 3) +
			    (seed / 6)) % 6;
			retval[j++] = vowels[idx0];
			retval[j++] = consonants[idx1];
			retval[j++] = vowels[idx2];
			if ((i + 1) < rounds) {
				idx3 = (((u_int)(dgst_raw[(2 * i) + 1])) >> 4) & 15;
				idx4 = (((u_int)(dgst_raw[(2 * i) + 1]))) & 15;
				retval[j++] = consonants[idx3];
				retval[j++] = '-';
				retval[j++] = consonants[idx4];
				seed = ((seed * 5) +
				    ((((u_int)(dgst_raw[2 * i])) * 7) +
				    ((u_int)(dgst_raw[(2 * i) + 1])))) % 36;
			}
		} else {
			idx0 = seed % 6;
			idx1 = 16;
			idx2 = seed / 6;
			retval[j++] = vowels[idx0];
			retval[j++] = consonants[idx1];
			retval[j++] = vowels[idx2];
		}
	}
	retval[j++] = 'x';
	retval[j++] = '\0';
	return retval;
}

/*
 * Draw an ASCII-Art representing the fingerprint so human brain can
 * profit from its built-in pattern recognition ability.
 * This technique is called "random art" and can be found in some
 * scientific publications like this original paper:
 *
 * "Hash Visualization: a New Technique to improve Real-World Security",
 * Perrig A. and Song D., 1999, International Workshop on Cryptographic
 * Techniques and E-Commerce (CrypTEC '99)
 * sparrow.ece.cmu.edu/~adrian/projects/validation/validation.pdf
 *
 * The subject came up in a talk by Dan Kaminsky, too.
 *
 * If you see the picture is different, the key is different.
 * If the picture looks the same, you still know nothing.
 *
 * The algorithm used here is a worm crawling over a discrete plane,
 * leaving a trace (augmenting the field) everywhere it goes.
 * Movement is taken from dgst_raw 2bit-wise.  Bumping into walls
 * makes the respective movement vector be ignored for this turn.
 * Graphs are not unambiguous, because circles in graphs can be
 * walked in either direction.
 */

/*
 * Field sizes for the random art.  Have to be odd, so the starting point
 * can be in the exact middle of the picture, and FLDBASE should be >=8 .
 * Else pictures would be too dense, and drawing the frame would
 * fail, too, because the key type would not fit in anymore.
 */
#define	FLDBASE		8
#define	FLDSIZE_Y	(FLDBASE + 1)
#define	FLDSIZE_X	(FLDBASE * 2 + 1)
static char *
fingerprint_randomart(const char *alg, u_char *dgst_raw, size_t dgst_raw_len,
    const struct sshkey *k)
{
	/*
	 * Chars to be used after each other every time the worm
	 * intersects with itself.  Matter of taste.
	 */
	char	*augmentation_string = " .o+=*BOX@%&#/^SE";
	char	*retval, *p, title[FLDSIZE_X], hash[FLDSIZE_X];
	u_char	 field[FLDSIZE_X][FLDSIZE_Y];
	size_t	 i, tlen, hlen;
	u_int	 b;
	int	 x, y, r;
	size_t	 len = strlen(augmentation_string) - 1;

	if ((retval = calloc((FLDSIZE_X + 3), (FLDSIZE_Y + 2))) == NULL)
		return NULL;

	/* initialize field */
	memset(field, 0, FLDSIZE_X * FLDSIZE_Y * sizeof(char));
	x = FLDSIZE_X / 2;
	y = FLDSIZE_Y / 2;

	/* process raw key */
	for (i = 0; i < dgst_raw_len; i++) {
		int input;
		/* each byte conveys four 2-bit move commands */
		input = dgst_raw[i];
		for (b = 0; b < 4; b++) {
			/* evaluate 2 bit, rest is shifted later */
			x += (input & 0x1) ? 1 : -1;
			y += (input & 0x2) ? 1 : -1;

			/* assure we are still in bounds */
			x = MAXIMUM(x, 0);
			y = MAXIMUM(y, 0);
			x = MINIMUM(x, FLDSIZE_X - 1);
			y = MINIMUM(y, FLDSIZE_Y - 1);

			/* augment the field */
			if (field[x][y] < len - 2)
				field[x][y]++;
			input = input >> 2;
		}
	}

	/* mark starting point and end point*/
	field[FLDSIZE_X / 2][FLDSIZE_Y / 2] = len - 1;
	field[x][y] = len;

	/* assemble title */
	r = snprintf(title, sizeof(title), "[%s %u]",
		sshkey_type(k), sshkey_size(k));
	/* If [type size] won't fit, then try [type]; fits "[ED25519-CERT]" */
	if (r < 0 || r > (int)sizeof(title))
		r = snprintf(title, sizeof(title), "[%s]", sshkey_type(k));
	tlen = (r <= 0) ? 0 : strlen(title);

	/* assemble hash ID. */
	r = snprintf(hash, sizeof(hash), "[%s]", alg);
	hlen = (r <= 0) ? 0 : strlen(hash);

	/* output upper border */
	p = retval;
	*p++ = '+';
	for (i = 0; i < (FLDSIZE_X - tlen) / 2; i++)
		*p++ = '-';
	memcpy(p, title, tlen);
	p += tlen;
	for (i += tlen; i < FLDSIZE_X; i++)
		*p++ = '-';
	*p++ = '+';
	*p++ = '\n';

	/* output content */
	for (y = 0; y < FLDSIZE_Y; y++) {
		*p++ = '|';
		for (x = 0; x < FLDSIZE_X; x++)
			*p++ = augmentation_string[MINIMUM(field[x][y], len)];
		*p++ = '|';
		*p++ = '\n';
	}

	/* output lower border */
	*p++ = '+';
	for (i = 0; i < (FLDSIZE_X - hlen) / 2; i++)
		*p++ = '-';
	memcpy(p, hash, hlen);
	p += hlen;
	for (i += hlen; i < FLDSIZE_X; i++)
		*p++ = '-';
	*p++ = '+';

	return retval;
}

char *
sshkey_fingerprint(const struct sshkey *k, int dgst_alg,
    enum sshkey_fp_rep dgst_rep)
{
	char *retval = NULL;
	u_char *dgst_raw;
	size_t dgst_raw_len;

	if (sshkey_fingerprint_raw(k, dgst_alg, &dgst_raw, &dgst_raw_len) != 0)
		return NULL;
	switch (dgst_rep) {
	case SSH_FP_DEFAULT:
		if (dgst_alg == SSH_DIGEST_MD5) {
			retval = fingerprint_hex(ssh_digest_alg_name(dgst_alg),
			    dgst_raw, dgst_raw_len);
		} else {
			retval = fingerprint_b64(ssh_digest_alg_name(dgst_alg),
			    dgst_raw, dgst_raw_len);
		}
		break;
	case SSH_FP_HEX:
		retval = fingerprint_hex(ssh_digest_alg_name(dgst_alg),
		    dgst_raw, dgst_raw_len);
		break;
	case SSH_FP_BASE64:
		retval = fingerprint_b64(ssh_digest_alg_name(dgst_alg),
		    dgst_raw, dgst_raw_len);
		break;
	case SSH_FP_BUBBLEBABBLE:
		retval = fingerprint_bubblebabble(dgst_raw, dgst_raw_len);
		break;
	case SSH_FP_RANDOMART:
		retval = fingerprint_randomart(ssh_digest_alg_name(dgst_alg),
		    dgst_raw, dgst_raw_len, k);
		break;
	default:
		explicit_bzero(dgst_raw, dgst_raw_len);
		free(dgst_raw);
		return NULL;
	}
	explicit_bzero(dgst_raw, dgst_raw_len);
	free(dgst_raw);
	return retval;
}

static int
peek_type_nid(const char *s, size_t l, int *nid)
{
	const struct keytype *kt;

	for (kt = keytypes; kt->type != -1; kt++) {
		if (kt->name == NULL || strlen(kt->name) != l)
			continue;
		if (memcmp(s, kt->name, l) == 0) {
			*nid = -1;
			if (kt->type == KEY_ECDSA || kt->type == KEY_ECDSA_CERT)
				*nid = kt->nid;
			return kt->type;
		}
	}
	return KEY_UNSPEC;
}

/* XXX this can now be made const char * */
int
sshkey_read(struct sshkey *ret, char **cpp)
{
	struct sshkey *k;
	char *cp, *blobcopy;
	size_t space;
	int r, type, curve_nid = -1;
	struct sshbuf *blob;

	if (ret == NULL)
		return SSH_ERR_INVALID_ARGUMENT;

	switch (ret->type) {
	case KEY_UNSPEC:
	case KEY_RSA:
	case KEY_DSA:
	case KEY_ECDSA:
	case KEY_ED25519:
	case KEY_DSA_CERT:
	case KEY_ECDSA_CERT:
	case KEY_RSA_CERT:
	case KEY_ED25519_CERT:
#ifdef WITH_XMSS
	case KEY_XMSS:
	case KEY_XMSS_CERT:
#endif /* WITH_XMSS */
		break; /* ok */
	default:
		return SSH_ERR_INVALID_ARGUMENT;
	}

	/* Decode type */
	cp = *cpp;
	space = strcspn(cp, " \t");
	if (space == strlen(cp))
		return SSH_ERR_INVALID_FORMAT;
	if ((type = peek_type_nid(cp, space, &curve_nid)) == KEY_UNSPEC)
		return SSH_ERR_INVALID_FORMAT;

	/* skip whitespace */
	for (cp += space; *cp == ' ' || *cp == '\t'; cp++)
		;
	if (*cp == '\0')
		return SSH_ERR_INVALID_FORMAT;
	if (ret->type != KEY_UNSPEC && ret->type != type)
		return SSH_ERR_KEY_TYPE_MISMATCH;
	if ((blob = sshbuf_new()) == NULL)
		return SSH_ERR_ALLOC_FAIL;

	/* find end of keyblob and decode */
	space = strcspn(cp, " \t");
	if ((blobcopy = strndup(cp, space)) == NULL) {
		sshbuf_free(blob);
		return SSH_ERR_ALLOC_FAIL;
	}
	if ((r = sshbuf_b64tod(blob, blobcopy)) != 0) {
		free(blobcopy);
		sshbuf_free(blob);
		return r;
	}
	free(blobcopy);
	if ((r = sshkey_fromb(blob, &k)) != 0) {
		sshbuf_free(blob);
		return r;
	}
	sshbuf_free(blob);

	/* skip whitespace and leave cp at start of comment */
	for (cp += space; *cp == ' ' || *cp == '\t'; cp++)
		;

	/* ensure type of blob matches type at start of line */
	if (k->type != type) {
		sshkey_free(k);
		return SSH_ERR_KEY_TYPE_MISMATCH;
	}
	if (sshkey_type_plain(type) == KEY_ECDSA && curve_nid != k->ecdsa_nid) {
		sshkey_free(k);
		return SSH_ERR_EC_CURVE_MISMATCH;
	}

	/* Fill in ret from parsed key */
	ret->type = type;
	if (sshkey_is_cert(ret)) {
		if (!sshkey_is_cert(k)) {
			sshkey_free(k);
			return SSH_ERR_EXPECTED_CERT;
		}
		if (ret->cert != NULL)
			cert_free(ret->cert);
		ret->cert = k->cert;
		k->cert = NULL;
	}
	switch (sshkey_type_plain(ret->type)) {
#ifdef WITH_OPENSSL
	case KEY_RSA:
		RSA_free(ret->rsa);
		ret->rsa = k->rsa;
		k->rsa = NULL;
#ifdef DEBUG_PK
		RSA_print_fp(stderr, ret->rsa, 8);
#endif
		break;
	case KEY_DSA:
		DSA_free(ret->dsa);
		ret->dsa = k->dsa;
		k->dsa = NULL;
#ifdef DEBUG_PK
		DSA_print_fp(stderr, ret->dsa, 8);
#endif
		break;
# ifdef OPENSSL_HAS_ECC
	case KEY_ECDSA:
		EC_KEY_free(ret->ecdsa);
		ret->ecdsa = k->ecdsa;
		ret->ecdsa_nid = k->ecdsa_nid;
		k->ecdsa = NULL;
		k->ecdsa_nid = -1;
#ifdef DEBUG_PK
		sshkey_dump_ec_key(ret->ecdsa);
#endif
		break;
# endif /* OPENSSL_HAS_ECC */
#endif /* WITH_OPENSSL */
	case KEY_ED25519:
		freezero(ret->ed25519_pk, ED25519_PK_SZ);
		ret->ed25519_pk = k->ed25519_pk;
		k->ed25519_pk = NULL;
#ifdef DEBUG_PK
		/* XXX */
#endif
		break;
#ifdef WITH_XMSS
	case KEY_XMSS:
		free(ret->xmss_pk);
		ret->xmss_pk = k->xmss_pk;
		k->xmss_pk = NULL;
		free(ret->xmss_state);
		ret->xmss_state = k->xmss_state;
		k->xmss_state = NULL;
		free(ret->xmss_name);
		ret->xmss_name = k->xmss_name;
		k->xmss_name = NULL;
		free(ret->xmss_filename);
		ret->xmss_filename = k->xmss_filename;
		k->xmss_filename = NULL;
#ifdef DEBUG_PK
		/* XXX */
#endif
		break;
#endif /* WITH_XMSS */
	default:
		sshkey_free(k);
		return SSH_ERR_INTERNAL_ERROR;
	}
	sshkey_free(k);

	/* success */
	*cpp = cp;
	return 0;
}


int
sshkey_to_base64(const struct sshkey *key, char **b64p)
{
	int r = SSH_ERR_INTERNAL_ERROR;
	struct sshbuf *b = NULL;
	char *uu = NULL;

	if (b64p != NULL)
		*b64p = NULL;
	if ((b = sshbuf_new()) == NULL)
		return SSH_ERR_ALLOC_FAIL;
	if ((r = sshkey_putb(key, b)) != 0)
		goto out;
	if ((uu = sshbuf_dtob64(b)) == NULL) {
		r = SSH_ERR_ALLOC_FAIL;
		goto out;
	}
	/* Success */
	if (b64p != NULL) {
		*b64p = uu;
		uu = NULL;
	}
	r = 0;
 out:
	sshbuf_free(b);
	free(uu);
	return r;
}

int
sshkey_format_text(const struct sshkey *key, struct sshbuf *b)
{
	int r = SSH_ERR_INTERNAL_ERROR;
	char *uu = NULL;

	if ((r = sshkey_to_base64(key, &uu)) != 0)
		goto out;
	if ((r = sshbuf_putf(b, "%s %s",
	    sshkey_ssh_name(key), uu)) != 0)
		goto out;
	r = 0;
 out:
	free(uu);
	return r;
}

int
sshkey_write(const struct sshkey *key, FILE *f)
{
	struct sshbuf *b = NULL;
	int r = SSH_ERR_INTERNAL_ERROR;

	if ((b = sshbuf_new()) == NULL)
		return SSH_ERR_ALLOC_FAIL;
	if ((r = sshkey_format_text(key, b)) != 0)
		goto out;
	if (fwrite(sshbuf_ptr(b), sshbuf_len(b), 1, f) != 1) {
		if (feof(f))
			errno = EPIPE;
		r = SSH_ERR_SYSTEM_ERROR;
		goto out;
	}
	/* Success */
	r = 0;
 out:
	sshbuf_free(b);
	return r;
}

const char *
sshkey_cert_type(const struct sshkey *k)
{
	switch (k->cert->type) {
	case SSH2_CERT_TYPE_USER:
		return "user";
	case SSH2_CERT_TYPE_HOST:
		return "host";
	default:
		return "unknown";
	}
}

#ifdef WITH_OPENSSL
static int
rsa_generate_private_key(u_int bits, RSA **rsap)
{
	RSA *private = NULL;
	BIGNUM *f4 = NULL;
	int ret = SSH_ERR_INTERNAL_ERROR;

	if (rsap == NULL)
		return SSH_ERR_INVALID_ARGUMENT;
	if (bits < SSH_RSA_MINIMUM_MODULUS_SIZE ||
	    bits > SSHBUF_MAX_BIGNUM * 8)
		return SSH_ERR_KEY_LENGTH;
	*rsap = NULL;
	if ((private = RSA_new()) == NULL || (f4 = BN_new()) == NULL) {
		ret = SSH_ERR_ALLOC_FAIL;
		goto out;
	}
	if (!BN_set_word(f4, RSA_F4) ||
	    !RSA_generate_key_ex(private, bits, f4, NULL)) {
		ret = SSH_ERR_LIBCRYPTO_ERROR;
		goto out;
	}
	*rsap = private;
	private = NULL;
	ret = 0;
 out:
	RSA_free(private);
	BN_free(f4);
	return ret;
}

static int
dsa_generate_private_key(u_int bits, DSA **dsap)
{
	DSA *private;
	int ret = SSH_ERR_INTERNAL_ERROR;

	if (dsap == NULL)
		return SSH_ERR_INVALID_ARGUMENT;
	if (bits != 1024)
		return SSH_ERR_KEY_LENGTH;
	if ((private = DSA_new()) == NULL) {
		ret = SSH_ERR_ALLOC_FAIL;
		goto out;
	}
	*dsap = NULL;
	if (!DSA_generate_parameters_ex(private, bits, NULL, 0, NULL,
	    NULL, NULL) || !DSA_generate_key(private)) {
		ret = SSH_ERR_LIBCRYPTO_ERROR;
		goto out;
	}
	*dsap = private;
	private = NULL;
	ret = 0;
 out:
	DSA_free(private);
	return ret;
}

# ifdef OPENSSL_HAS_ECC
int
sshkey_ecdsa_key_to_nid(EC_KEY *k)
{
	EC_GROUP *eg;
	int nids[] = {
		NID_X9_62_prime256v1,
		NID_secp384r1,
#  ifdef OPENSSL_HAS_NISTP521
		NID_secp521r1,
#  endif /* OPENSSL_HAS_NISTP521 */
		-1
	};
	int nid;
	u_int i;
	BN_CTX *bnctx;
	const EC_GROUP *g = EC_KEY_get0_group(k);

	/*
	 * The group may be stored in a ASN.1 encoded private key in one of two
	 * ways: as a "named group", which is reconstituted by ASN.1 object ID
	 * or explicit group parameters encoded into the key blob. Only the
	 * "named group" case sets the group NID for us, but we can figure
	 * it out for the other case by comparing against all the groups that
	 * are supported.
	 */
	if ((nid = EC_GROUP_get_curve_name(g)) > 0)
		return nid;
	if ((bnctx = BN_CTX_new()) == NULL)
		return -1;
	for (i = 0; nids[i] != -1; i++) {
		if ((eg = EC_GROUP_new_by_curve_name(nids[i])) == NULL) {
			BN_CTX_free(bnctx);
			return -1;
		}
		if (EC_GROUP_cmp(g, eg, bnctx) == 0)
			break;
		EC_GROUP_free(eg);
	}
	BN_CTX_free(bnctx);
	if (nids[i] != -1) {
		/* Use the group with the NID attached */
		EC_GROUP_set_asn1_flag(eg, OPENSSL_EC_NAMED_CURVE);
		if (EC_KEY_set_group(k, eg) != 1) {
			EC_GROUP_free(eg);
			return -1;
		}
	}
	return nids[i];
}

static int
ecdsa_generate_private_key(u_int bits, int *nid, EC_KEY **ecdsap)
{
	EC_KEY *private;
	int ret = SSH_ERR_INTERNAL_ERROR;

	if (nid == NULL || ecdsap == NULL)
		return SSH_ERR_INVALID_ARGUMENT;
	if ((*nid = sshkey_ecdsa_bits_to_nid(bits)) == -1)
		return SSH_ERR_KEY_LENGTH;
	*ecdsap = NULL;
	if ((private = EC_KEY_new_by_curve_name(*nid)) == NULL) {
		ret = SSH_ERR_ALLOC_FAIL;
		goto out;
	}
	if (EC_KEY_generate_key(private) != 1) {
		ret = SSH_ERR_LIBCRYPTO_ERROR;
		goto out;
	}
	EC_KEY_set_asn1_flag(private, OPENSSL_EC_NAMED_CURVE);
	*ecdsap = private;
	private = NULL;
	ret = 0;
 out:
	EC_KEY_free(private);
	return ret;
}
# endif /* OPENSSL_HAS_ECC */
#endif /* WITH_OPENSSL */

int
sshkey_generate(int type, u_int bits, struct sshkey **keyp)
{
	struct sshkey *k;
	int ret = SSH_ERR_INTERNAL_ERROR;

	if (keyp == NULL)
		return SSH_ERR_INVALID_ARGUMENT;
	*keyp = NULL;
	if ((k = sshkey_new(KEY_UNSPEC)) == NULL)
		return SSH_ERR_ALLOC_FAIL;
	switch (type) {
	case KEY_ED25519:
		if ((k->ed25519_pk = malloc(ED25519_PK_SZ)) == NULL ||
		    (k->ed25519_sk = malloc(ED25519_SK_SZ)) == NULL) {
			ret = SSH_ERR_ALLOC_FAIL;
			break;
		}
		crypto_sign_ed25519_keypair(k->ed25519_pk, k->ed25519_sk);
		ret = 0;
		break;
#ifdef WITH_XMSS
	case KEY_XMSS:
		ret = sshkey_xmss_generate_private_key(k, bits);
		break;
#endif /* WITH_XMSS */
#ifdef WITH_OPENSSL
	case KEY_DSA:
		ret = dsa_generate_private_key(bits, &k->dsa);
		break;
# ifdef OPENSSL_HAS_ECC
	case KEY_ECDSA:
		ret = ecdsa_generate_private_key(bits, &k->ecdsa_nid,
		    &k->ecdsa);
		break;
# endif /* OPENSSL_HAS_ECC */
	case KEY_RSA:
		ret = rsa_generate_private_key(bits, &k->rsa);
		break;
#endif /* WITH_OPENSSL */
	default:
		ret = SSH_ERR_INVALID_ARGUMENT;
	}
	if (ret == 0) {
		k->type = type;
		*keyp = k;
	} else
		sshkey_free(k);
	return ret;
}

int
sshkey_cert_copy(const struct sshkey *from_key, struct sshkey *to_key)
{
	u_int i;
	const struct sshkey_cert *from;
	struct sshkey_cert *to;
	int ret = SSH_ERR_INTERNAL_ERROR;

	if (to_key->cert != NULL) {
		cert_free(to_key->cert);
		to_key->cert = NULL;
	}

	if ((from = from_key->cert) == NULL)
		return SSH_ERR_INVALID_ARGUMENT;

	if ((to = to_key->cert = cert_new()) == NULL)
		return SSH_ERR_ALLOC_FAIL;

	if ((ret = sshbuf_putb(to->certblob, from->certblob)) != 0 ||
	    (ret = sshbuf_putb(to->critical, from->critical)) != 0 ||
	    (ret = sshbuf_putb(to->extensions, from->extensions)) != 0)
		return ret;

	to->serial = from->serial;
	to->type = from->type;
	if (from->key_id == NULL)
		to->key_id = NULL;
	else if ((to->key_id = strdup(from->key_id)) == NULL)
		return SSH_ERR_ALLOC_FAIL;
	to->valid_after = from->valid_after;
	to->valid_before = from->valid_before;
	if (from->signature_key == NULL)
		to->signature_key = NULL;
	else if ((ret = sshkey_from_private(from->signature_key,
	    &to->signature_key)) != 0)
		return ret;

	if (from->nprincipals > SSHKEY_CERT_MAX_PRINCIPALS)
		return SSH_ERR_INVALID_ARGUMENT;
	if (from->nprincipals > 0) {
		if ((to->principals = calloc(from->nprincipals,
		    sizeof(*to->principals))) == NULL)
			return SSH_ERR_ALLOC_FAIL;
		for (i = 0; i < from->nprincipals; i++) {
			to->principals[i] = strdup(from->principals[i]);
			if (to->principals[i] == NULL) {
				to->nprincipals = i;
				return SSH_ERR_ALLOC_FAIL;
			}
		}
	}
	to->nprincipals = from->nprincipals;
	return 0;
}

int
sshkey_from_private(const struct sshkey *k, struct sshkey **pkp)
{
	struct sshkey *n = NULL;
	int r = SSH_ERR_INTERNAL_ERROR;
#ifdef WITH_OPENSSL
	const BIGNUM *rsa_n, *rsa_e;
	BIGNUM *rsa_n_dup = NULL, *rsa_e_dup = NULL;
	const BIGNUM *dsa_p, *dsa_q, *dsa_g, *dsa_pub_key;
	BIGNUM *dsa_p_dup = NULL, *dsa_q_dup = NULL, *dsa_g_dup = NULL;
	BIGNUM *dsa_pub_key_dup = NULL;
#endif /* WITH_OPENSSL */

	*pkp = NULL;
	switch (k->type) {
#ifdef WITH_OPENSSL
	case KEY_DSA:
	case KEY_DSA_CERT:
		if ((n = sshkey_new(k->type)) == NULL) {
			r = SSH_ERR_ALLOC_FAIL;
			goto out;
		}

		DSA_get0_pqg(k->dsa, &dsa_p, &dsa_q, &dsa_g);
		DSA_get0_key(k->dsa, &dsa_pub_key, NULL);
		if ((dsa_p_dup = BN_dup(dsa_p)) == NULL ||
		    (dsa_q_dup = BN_dup(dsa_q)) == NULL ||
		    (dsa_g_dup = BN_dup(dsa_g)) == NULL ||
		    (dsa_pub_key_dup = BN_dup(dsa_pub_key)) == NULL) {
			r = SSH_ERR_ALLOC_FAIL;
			goto out;
		}
		if (!DSA_set0_pqg(n->dsa, dsa_p_dup, dsa_q_dup, dsa_g_dup)) {
			r = SSH_ERR_LIBCRYPTO_ERROR;
			goto out;
		}
		dsa_p_dup = dsa_q_dup = dsa_g_dup = NULL; /* transferred */
		if (!DSA_set0_key(n->dsa, dsa_pub_key_dup, NULL)) {
			r = SSH_ERR_LIBCRYPTO_ERROR;
			goto out;
		}
		dsa_pub_key_dup = NULL; /* transferred */

		break;
# ifdef OPENSSL_HAS_ECC
	case KEY_ECDSA:
	case KEY_ECDSA_CERT:
		if ((n = sshkey_new(k->type)) == NULL) {
			r = SSH_ERR_ALLOC_FAIL;
			goto out;
		}
		n->ecdsa_nid = k->ecdsa_nid;
		n->ecdsa = EC_KEY_new_by_curve_name(k->ecdsa_nid);
		if (n->ecdsa == NULL) {
			r = SSH_ERR_ALLOC_FAIL;
			goto out;
		}
		if (EC_KEY_set_public_key(n->ecdsa,
		    EC_KEY_get0_public_key(k->ecdsa)) != 1) {
			r = SSH_ERR_LIBCRYPTO_ERROR;
			goto out;
		}
		break;
# endif /* OPENSSL_HAS_ECC */
	case KEY_RSA:
	case KEY_RSA_CERT:
		if ((n = sshkey_new(k->type)) == NULL) {
			r = SSH_ERR_ALLOC_FAIL;
			goto out;
		}
		RSA_get0_key(k->rsa, &rsa_n, &rsa_e, NULL);
		if ((rsa_n_dup = BN_dup(rsa_n)) == NULL ||
		    (rsa_e_dup = BN_dup(rsa_e)) == NULL) {
			r = SSH_ERR_ALLOC_FAIL;
			goto out;
		}
		if (!RSA_set0_key(n->rsa, rsa_n_dup, rsa_e_dup, NULL)) {
			r = SSH_ERR_LIBCRYPTO_ERROR;
			goto out;
		}
		rsa_n_dup = rsa_e_dup = NULL; /* transferred */
		break;
#endif /* WITH_OPENSSL */
	case KEY_ED25519:
	case KEY_ED25519_CERT:
		if ((n = sshkey_new(k->type)) == NULL) {
			r = SSH_ERR_ALLOC_FAIL;
			goto out;
		}
		if (k->ed25519_pk != NULL) {
			if ((n->ed25519_pk = malloc(ED25519_PK_SZ)) == NULL) {
				r = SSH_ERR_ALLOC_FAIL;
				goto out;
			}
			memcpy(n->ed25519_pk, k->ed25519_pk, ED25519_PK_SZ);
		}
		break;
#ifdef WITH_XMSS
	case KEY_XMSS:
	case KEY_XMSS_CERT:
		if ((n = sshkey_new(k->type)) == NULL) {
			r = SSH_ERR_ALLOC_FAIL;
			goto out;
		}
		if ((r = sshkey_xmss_init(n, k->xmss_name)) != 0)
			goto out;
		if (k->xmss_pk != NULL) {
			size_t pklen = sshkey_xmss_pklen(k);
			if (pklen == 0 || sshkey_xmss_pklen(n) != pklen) {
				r = SSH_ERR_INTERNAL_ERROR;
				goto out;
			}
			if ((n->xmss_pk = malloc(pklen)) == NULL) {
				r = SSH_ERR_ALLOC_FAIL;
				goto out;
			}
			memcpy(n->xmss_pk, k->xmss_pk, pklen);
		}
		break;
#endif /* WITH_XMSS */
	default:
		r = SSH_ERR_KEY_TYPE_UNKNOWN;
		goto out;
	}
	if (sshkey_is_cert(k) && (r = sshkey_cert_copy(k, n)) != 0)
		goto out;
	/* success */
	*pkp = n;
	n = NULL;
	r = 0;
 out:
	sshkey_free(n);
#ifdef WITH_OPENSSL
	BN_clear_free(rsa_n_dup);
	BN_clear_free(rsa_e_dup);
	BN_clear_free(dsa_p_dup);
	BN_clear_free(dsa_q_dup);
	BN_clear_free(dsa_g_dup);
	BN_clear_free(dsa_pub_key_dup);
#endif

	return r;
}

static int
cert_parse(struct sshbuf *b, struct sshkey *key, struct sshbuf *certbuf)
{
	struct sshbuf *principals = NULL, *crit = NULL;
	struct sshbuf *exts = NULL, *ca = NULL;
	u_char *sig = NULL;
	size_t signed_len = 0, slen = 0, kidlen = 0;
	int ret = SSH_ERR_INTERNAL_ERROR;

	/* Copy the entire key blob for verification and later serialisation */
	if ((ret = sshbuf_putb(key->cert->certblob, certbuf)) != 0)
		return ret;

	/* Parse body of certificate up to signature */
	if ((ret = sshbuf_get_u64(b, &key->cert->serial)) != 0 ||
	    (ret = sshbuf_get_u32(b, &key->cert->type)) != 0 ||
	    (ret = sshbuf_get_cstring(b, &key->cert->key_id, &kidlen)) != 0 ||
	    (ret = sshbuf_froms(b, &principals)) != 0 ||
	    (ret = sshbuf_get_u64(b, &key->cert->valid_after)) != 0 ||
	    (ret = sshbuf_get_u64(b, &key->cert->valid_before)) != 0 ||
	    (ret = sshbuf_froms(b, &crit)) != 0 ||
	    (ret = sshbuf_froms(b, &exts)) != 0 ||
	    (ret = sshbuf_get_string_direct(b, NULL, NULL)) != 0 ||
	    (ret = sshbuf_froms(b, &ca)) != 0) {
		/* XXX debug print error for ret */
		ret = SSH_ERR_INVALID_FORMAT;
		goto out;
	}

	/* Signature is left in the buffer so we can calculate this length */
	signed_len = sshbuf_len(key->cert->certblob) - sshbuf_len(b);

	if ((ret = sshbuf_get_string(b, &sig, &slen)) != 0) {
		ret = SSH_ERR_INVALID_FORMAT;
		goto out;
	}

	if (key->cert->type != SSH2_CERT_TYPE_USER &&
	    key->cert->type != SSH2_CERT_TYPE_HOST) {
		ret = SSH_ERR_KEY_CERT_UNKNOWN_TYPE;
		goto out;
	}

	/* Parse principals section */
	while (sshbuf_len(principals) > 0) {
		char *principal = NULL;
		char **oprincipals = NULL;

		if (key->cert->nprincipals >= SSHKEY_CERT_MAX_PRINCIPALS) {
			ret = SSH_ERR_INVALID_FORMAT;
			goto out;
		}
		if ((ret = sshbuf_get_cstring(principals, &principal,
		    NULL)) != 0) {
			ret = SSH_ERR_INVALID_FORMAT;
			goto out;
		}
		oprincipals = key->cert->principals;
		key->cert->principals = recallocarray(key->cert->principals,
		    key->cert->nprincipals, key->cert->nprincipals + 1,
		    sizeof(*key->cert->principals));
		if (key->cert->principals == NULL) {
			free(principal);
			key->cert->principals = oprincipals;
			ret = SSH_ERR_ALLOC_FAIL;
			goto out;
		}
		key->cert->principals[key->cert->nprincipals++] = principal;
	}

	/*
	 * Stash a copies of the critical options and extensions sections
	 * for later use.
	 */
	if ((ret = sshbuf_putb(key->cert->critical, crit)) != 0 ||
	    (exts != NULL &&
	    (ret = sshbuf_putb(key->cert->extensions, exts)) != 0))
		goto out;

	/*
	 * Validate critical options and extensions sections format.
	 */
	while (sshbuf_len(crit) != 0) {
		if ((ret = sshbuf_get_string_direct(crit, NULL, NULL)) != 0 ||
		    (ret = sshbuf_get_string_direct(crit, NULL, NULL)) != 0) {
			sshbuf_reset(key->cert->critical);
			ret = SSH_ERR_INVALID_FORMAT;
			goto out;
		}
	}
	while (exts != NULL && sshbuf_len(exts) != 0) {
		if ((ret = sshbuf_get_string_direct(exts, NULL, NULL)) != 0 ||
		    (ret = sshbuf_get_string_direct(exts, NULL, NULL)) != 0) {
			sshbuf_reset(key->cert->extensions);
			ret = SSH_ERR_INVALID_FORMAT;
			goto out;
		}
	}

	/* Parse CA key and check signature */
	if (sshkey_from_blob_internal(ca, &key->cert->signature_key, 0) != 0) {
		ret = SSH_ERR_KEY_CERT_INVALID_SIGN_KEY;
		goto out;
	}
	if (!sshkey_type_is_valid_ca(key->cert->signature_key->type)) {
		ret = SSH_ERR_KEY_CERT_INVALID_SIGN_KEY;
		goto out;
	}
	if ((ret = sshkey_verify(key->cert->signature_key, sig, slen,
	    sshbuf_ptr(key->cert->certblob), signed_len, NULL, 0)) != 0)
		goto out;

	/* Success */
	ret = 0;
 out:
	sshbuf_free(ca);
	sshbuf_free(crit);
	sshbuf_free(exts);
	sshbuf_free(principals);
	free(sig);
	return ret;
}

#ifdef WITH_OPENSSL
static int
check_rsa_length(const RSA *rsa)
{
	const BIGNUM *rsa_n;

	RSA_get0_key(rsa, &rsa_n, NULL, NULL);
	if (BN_num_bits(rsa_n) < SSH_RSA_MINIMUM_MODULUS_SIZE)
		return SSH_ERR_KEY_LENGTH;
	return 0;
}
#endif

static int
sshkey_from_blob_internal(struct sshbuf *b, struct sshkey **keyp,
    int allow_cert)
{
	int type, ret = SSH_ERR_INTERNAL_ERROR;
	char *ktype = NULL, *curve = NULL, *xmss_name = NULL;
	struct sshkey *key = NULL;
	size_t len;
	u_char *pk = NULL;
	struct sshbuf *copy;
#if defined(WITH_OPENSSL)
	BIGNUM *rsa_n = NULL, *rsa_e = NULL;
	BIGNUM *dsa_p = NULL, *dsa_q = NULL, *dsa_g = NULL, *dsa_pub_key = NULL;
# if defined(OPENSSL_HAS_ECC)
	EC_POINT *q = NULL;
# endif /* OPENSSL_HAS_ECC */
#endif /* WITH_OPENSSL */

#ifdef DEBUG_PK /* XXX */
	sshbuf_dump(b, stderr);
#endif
	if (keyp != NULL)
		*keyp = NULL;
	if ((copy = sshbuf_fromb(b)) == NULL) {
		ret = SSH_ERR_ALLOC_FAIL;
		goto out;
	}
	if (sshbuf_get_cstring(b, &ktype, NULL) != 0) {
		ret = SSH_ERR_INVALID_FORMAT;
		goto out;
	}

	type = sshkey_type_from_name(ktype);
	if (!allow_cert && sshkey_type_is_cert(type)) {
		ret = SSH_ERR_KEY_CERT_INVALID_SIGN_KEY;
		goto out;
	}
	switch (type) {
#ifdef WITH_OPENSSL
	case KEY_RSA_CERT:
		/* Skip nonce */
		if (sshbuf_get_string_direct(b, NULL, NULL) != 0) {
			ret = SSH_ERR_INVALID_FORMAT;
			goto out;
		}
		/* FALLTHROUGH */
	case KEY_RSA:
		if ((key = sshkey_new(type)) == NULL) {
			ret = SSH_ERR_ALLOC_FAIL;
			goto out;
		}
		if ((rsa_e = BN_new()) == NULL ||
		    (rsa_n = BN_new()) == NULL) {
			ret = SSH_ERR_ALLOC_FAIL;
			goto out;
		}
		if (sshbuf_get_bignum2(b, rsa_e) != 0 ||
		    sshbuf_get_bignum2(b, rsa_n) != 0) {
			ret = SSH_ERR_INVALID_FORMAT;
			goto out;
		}
		if (!RSA_set0_key(key->rsa, rsa_n, rsa_e, NULL)) {
			ret = SSH_ERR_LIBCRYPTO_ERROR;
			goto out;
		}
		rsa_n = rsa_e = NULL; /* transferred */
		if ((ret = check_rsa_length(key->rsa)) != 0)
			goto out;
#ifdef DEBUG_PK
		RSA_print_fp(stderr, key->rsa, 8);
#endif
		break;
	case KEY_DSA_CERT:
		/* Skip nonce */
		if (sshbuf_get_string_direct(b, NULL, NULL) != 0) {
			ret = SSH_ERR_INVALID_FORMAT;
			goto out;
		}
		/* FALLTHROUGH */
	case KEY_DSA:
		if ((key = sshkey_new(type)) == NULL) {
			ret = SSH_ERR_ALLOC_FAIL;
			goto out;
		}
		if ((dsa_p = BN_new()) == NULL ||
		    (dsa_q = BN_new()) == NULL ||
		    (dsa_g = BN_new()) == NULL ||
		    (dsa_pub_key = BN_new()) == NULL) {
			ret = SSH_ERR_ALLOC_FAIL;
			goto out;
		}
		if (sshbuf_get_bignum2(b, dsa_p) != 0 ||
		    sshbuf_get_bignum2(b, dsa_q) != 0 ||
		    sshbuf_get_bignum2(b, dsa_g) != 0 ||
		    sshbuf_get_bignum2(b, dsa_pub_key) != 0) {
			ret = SSH_ERR_INVALID_FORMAT;
			goto out;
		}
		if (!DSA_set0_pqg(key->dsa, dsa_p, dsa_q, dsa_g)) {
			ret = SSH_ERR_LIBCRYPTO_ERROR;
			goto out;
		}
		dsa_p = dsa_q = dsa_g = NULL; /* transferred */
		if (!DSA_set0_key(key->dsa, dsa_pub_key, NULL)) {
			ret = SSH_ERR_LIBCRYPTO_ERROR;
			goto out;
		}
		dsa_pub_key = NULL; /* transferred */
#ifdef DEBUG_PK
		DSA_print_fp(stderr, key->dsa, 8);
#endif
		break;
	case KEY_ECDSA_CERT:
		/* Skip nonce */
		if (sshbuf_get_string_direct(b, NULL, NULL) != 0) {
			ret = SSH_ERR_INVALID_FORMAT;
			goto out;
		}
		/* FALLTHROUGH */
# ifdef OPENSSL_HAS_ECC
	case KEY_ECDSA:
		if ((key = sshkey_new(type)) == NULL) {
			ret = SSH_ERR_ALLOC_FAIL;
			goto out;
		}
		key->ecdsa_nid = sshkey_ecdsa_nid_from_name(ktype);
		if (sshbuf_get_cstring(b, &curve, NULL) != 0) {
			ret = SSH_ERR_INVALID_FORMAT;
			goto out;
		}
		if (key->ecdsa_nid != sshkey_curve_name_to_nid(curve)) {
			ret = SSH_ERR_EC_CURVE_MISMATCH;
			goto out;
		}
		EC_KEY_free(key->ecdsa);
		if ((key->ecdsa = EC_KEY_new_by_curve_name(key->ecdsa_nid))
		    == NULL) {
			ret = SSH_ERR_EC_CURVE_INVALID;
			goto out;
		}
		if ((q = EC_POINT_new(EC_KEY_get0_group(key->ecdsa))) == NULL) {
			ret = SSH_ERR_ALLOC_FAIL;
			goto out;
		}
		if (sshbuf_get_ec(b, q, EC_KEY_get0_group(key->ecdsa)) != 0) {
			ret = SSH_ERR_INVALID_FORMAT;
			goto out;
		}
		if (sshkey_ec_validate_public(EC_KEY_get0_group(key->ecdsa),
		    q) != 0) {
			ret = SSH_ERR_KEY_INVALID_EC_VALUE;
			goto out;
		}
		if (EC_KEY_set_public_key(key->ecdsa, q) != 1) {
			/* XXX assume it is a allocation error */
			ret = SSH_ERR_ALLOC_FAIL;
			goto out;
		}
#ifdef DEBUG_PK
		sshkey_dump_ec_point(EC_KEY_get0_group(key->ecdsa), q);
#endif
		break;
# endif /* OPENSSL_HAS_ECC */
#endif /* WITH_OPENSSL */
	case KEY_ED25519_CERT:
		/* Skip nonce */
		if (sshbuf_get_string_direct(b, NULL, NULL) != 0) {
			ret = SSH_ERR_INVALID_FORMAT;
			goto out;
		}
		/* FALLTHROUGH */
	case KEY_ED25519:
		if ((ret = sshbuf_get_string(b, &pk, &len)) != 0)
			goto out;
		if (len != ED25519_PK_SZ) {
			ret = SSH_ERR_INVALID_FORMAT;
			goto out;
		}
		if ((key = sshkey_new(type)) == NULL) {
			ret = SSH_ERR_ALLOC_FAIL;
			goto out;
		}
		key->ed25519_pk = pk;
		pk = NULL;
		break;
#ifdef WITH_XMSS
	case KEY_XMSS_CERT:
		/* Skip nonce */
		if (sshbuf_get_string_direct(b, NULL, NULL) != 0) {
			ret = SSH_ERR_INVALID_FORMAT;
			goto out;
		}
		/* FALLTHROUGH */
	case KEY_XMSS:
		if ((ret = sshbuf_get_cstring(b, &xmss_name, NULL)) != 0)
			goto out;
		if ((key = sshkey_new(type)) == NULL) {
			ret = SSH_ERR_ALLOC_FAIL;
			goto out;
		}
		if ((ret = sshkey_xmss_init(key, xmss_name)) != 0)
			goto out;
		if ((ret = sshbuf_get_string(b, &pk, &len)) != 0)
			goto out;
		if (len == 0 || len != sshkey_xmss_pklen(key)) {
			ret = SSH_ERR_INVALID_FORMAT;
			goto out;
		}
		key->xmss_pk = pk;
		pk = NULL;
		if (type != KEY_XMSS_CERT &&
		    (ret = sshkey_xmss_deserialize_pk_info(key, b)) != 0)
			goto out;
		break;
#endif /* WITH_XMSS */
	case KEY_UNSPEC:
	default:
		ret = SSH_ERR_KEY_TYPE_UNKNOWN;
		goto out;
	}

	/* Parse certificate potion */
	if (sshkey_is_cert(key) && (ret = cert_parse(b, key, copy)) != 0)
		goto out;

	if (key != NULL && sshbuf_len(b) != 0) {
		ret = SSH_ERR_INVALID_FORMAT;
		goto out;
	}
	ret = 0;
	if (keyp != NULL) {
		*keyp = key;
		key = NULL;
	}
 out:
	sshbuf_free(copy);
	sshkey_free(key);
	free(xmss_name);
	free(ktype);
	free(curve);
	free(pk);
#if defined(WITH_OPENSSL)
	BN_clear_free(rsa_n);
	BN_clear_free(rsa_e);
	BN_clear_free(dsa_p);
	BN_clear_free(dsa_q);
	BN_clear_free(dsa_g);
	BN_clear_free(dsa_pub_key);
# if defined(OPENSSL_HAS_ECC)
	EC_POINT_free(q);
# endif /* OPENSSL_HAS_ECC */
#endif /* WITH_OPENSSL */
	return ret;
}

int
sshkey_from_blob(const u_char *blob, size_t blen, struct sshkey **keyp)
{
	struct sshbuf *b;
	int r;

	if ((b = sshbuf_from(blob, blen)) == NULL)
		return SSH_ERR_ALLOC_FAIL;
	r = sshkey_from_blob_internal(b, keyp, 1);
	sshbuf_free(b);
	return r;
}

int
sshkey_fromb(struct sshbuf *b, struct sshkey **keyp)
{
	return sshkey_from_blob_internal(b, keyp, 1);
}

int
sshkey_froms(struct sshbuf *buf, struct sshkey **keyp)
{
	struct sshbuf *b;
	int r;

	if ((r = sshbuf_froms(buf, &b)) != 0)
		return r;
	r = sshkey_from_blob_internal(b, keyp, 1);
	sshbuf_free(b);
	return r;
}

static int
get_sigtype(const u_char *sig, size_t siglen, char **sigtypep)
{
	int r;
	struct sshbuf *b = NULL;
	char *sigtype = NULL;

	if (sigtypep != NULL)
		*sigtypep = NULL;
	if ((b = sshbuf_from(sig, siglen)) == NULL)
		return SSH_ERR_ALLOC_FAIL;
	if ((r = sshbuf_get_cstring(b, &sigtype, NULL)) != 0)
		goto out;
	/* success */
	if (sigtypep != NULL) {
		*sigtypep = sigtype;
		sigtype = NULL;
	}
	r = 0;
 out:
	free(sigtype);
	sshbuf_free(b);
	return r;
}

/*
 * Returns the expected signature algorithm for a given public key algorithm.
 */
const char *
sshkey_sigalg_by_name(const char *name)
{
	const struct keytype *kt;

	for (kt = keytypes; kt->type != -1; kt++) {
		if (strcmp(kt->name, name) != 0)
			continue;
		if (kt->sigalg != NULL)
			return kt->sigalg;
		if (!kt->cert)
			return kt->name;
		return sshkey_ssh_name_from_type_nid(
		    sshkey_type_plain(kt->type), kt->nid);
	}
	return NULL;
}

/*
 * Verifies that the signature algorithm appearing inside the signature blob
 * matches that which was requested.
 */
int
sshkey_check_sigtype(const u_char *sig, size_t siglen,
    const char *requested_alg)
{
	const char *expected_alg;
	char *sigtype = NULL;
	int r;

	if (requested_alg == NULL)
		return 0;
	if ((expected_alg = sshkey_sigalg_by_name(requested_alg)) == NULL)
		return SSH_ERR_INVALID_ARGUMENT;
	if ((r = get_sigtype(sig, siglen, &sigtype)) != 0)
		return r;
	r = strcmp(expected_alg, sigtype) == 0;
	free(sigtype);
	return r ? 0 : SSH_ERR_SIGN_ALG_UNSUPPORTED;
}

int
sshkey_sign(const struct sshkey *key,
    u_char **sigp, size_t *lenp,
    const u_char *data, size_t datalen, const char *alg, u_int compat)
{
	if (sigp != NULL)
		*sigp = NULL;
	if (lenp != NULL)
		*lenp = 0;
	if (datalen > SSH_KEY_MAX_SIGN_DATA_SIZE)
		return SSH_ERR_INVALID_ARGUMENT;
	switch (key->type) {
#ifdef WITH_OPENSSL
	case KEY_DSA_CERT:
	case KEY_DSA:
		return ssh_dss_sign(key, sigp, lenp, data, datalen, compat);
# ifdef OPENSSL_HAS_ECC
	case KEY_ECDSA_CERT:
	case KEY_ECDSA:
		return ssh_ecdsa_sign(key, sigp, lenp, data, datalen, compat);
# endif /* OPENSSL_HAS_ECC */
	case KEY_RSA_CERT:
	case KEY_RSA:
		return ssh_rsa_sign(key, sigp, lenp, data, datalen, alg);
#endif /* WITH_OPENSSL */
	case KEY_ED25519:
	case KEY_ED25519_CERT:
		return ssh_ed25519_sign(key, sigp, lenp, data, datalen, compat);
#ifdef WITH_XMSS
	case KEY_XMSS:
	case KEY_XMSS_CERT:
		return ssh_xmss_sign(key, sigp, lenp, data, datalen, compat);
#endif /* WITH_XMSS */
	default:
		return SSH_ERR_KEY_TYPE_UNKNOWN;
	}
}

/*
 * ssh_key_verify returns 0 for a correct signature  and < 0 on error.
 * If "alg" specified, then the signature must use that algorithm.
 */
int
sshkey_verify(const struct sshkey *key,
    const u_char *sig, size_t siglen,
    const u_char *data, size_t dlen, const char *alg, u_int compat)
{
	if (siglen == 0 || dlen > SSH_KEY_MAX_SIGN_DATA_SIZE)
		return SSH_ERR_INVALID_ARGUMENT;
	switch (key->type) {
#ifdef WITH_OPENSSL
	case KEY_DSA_CERT:
	case KEY_DSA:
		return ssh_dss_verify(key, sig, siglen, data, dlen, compat);
# ifdef OPENSSL_HAS_ECC
	case KEY_ECDSA_CERT:
	case KEY_ECDSA:
		return ssh_ecdsa_verify(key, sig, siglen, data, dlen, compat);
# endif /* OPENSSL_HAS_ECC */
	case KEY_RSA_CERT:
	case KEY_RSA:
		return ssh_rsa_verify(key, sig, siglen, data, dlen, alg);
#endif /* WITH_OPENSSL */
	case KEY_ED25519:
	case KEY_ED25519_CERT:
		return ssh_ed25519_verify(key, sig, siglen, data, dlen, compat);
#ifdef WITH_XMSS
	case KEY_XMSS:
	case KEY_XMSS_CERT:
		return ssh_xmss_verify(key, sig, siglen, data, dlen, compat);
#endif /* WITH_XMSS */
	default:
		return SSH_ERR_KEY_TYPE_UNKNOWN;
	}
}

/* Convert a plain key to their _CERT equivalent */
int
sshkey_to_certified(struct sshkey *k)
{
	int newtype;

	switch (k->type) {
#ifdef WITH_OPENSSL
	case KEY_RSA:
		newtype = KEY_RSA_CERT;
		break;
	case KEY_DSA:
		newtype = KEY_DSA_CERT;
		break;
	case KEY_ECDSA:
		newtype = KEY_ECDSA_CERT;
		break;
#endif /* WITH_OPENSSL */
	case KEY_ED25519:
		newtype = KEY_ED25519_CERT;
		break;
#ifdef WITH_XMSS
	case KEY_XMSS:
		newtype = KEY_XMSS_CERT;
		break;
#endif /* WITH_XMSS */
	default:
		return SSH_ERR_INVALID_ARGUMENT;
	}
	if ((k->cert = cert_new()) == NULL)
		return SSH_ERR_ALLOC_FAIL;
	k->type = newtype;
	return 0;
}

/* Convert a certificate to its raw key equivalent */
int
sshkey_drop_cert(struct sshkey *k)
{
	if (!sshkey_type_is_cert(k->type))
		return SSH_ERR_KEY_TYPE_UNKNOWN;
	cert_free(k->cert);
	k->cert = NULL;
	k->type = sshkey_type_plain(k->type);
	return 0;
}

/* Sign a certified key, (re-)generating the signed certblob. */
int
sshkey_certify_custom(struct sshkey *k, struct sshkey *ca, const char *alg,
    sshkey_certify_signer *signer, void *signer_ctx)
{
	struct sshbuf *principals = NULL;
	u_char *ca_blob = NULL, *sig_blob = NULL, nonce[32];
	size_t i, ca_len, sig_len;
	int ret = SSH_ERR_INTERNAL_ERROR;
	struct sshbuf *cert;
#ifdef WITH_OPENSSL
	const BIGNUM *rsa_n, *rsa_e, *dsa_p, *dsa_q, *dsa_g, *dsa_pub_key;
#endif /* WITH_OPENSSL */

	if (k == NULL || k->cert == NULL ||
	    k->cert->certblob == NULL || ca == NULL)
		return SSH_ERR_INVALID_ARGUMENT;
	if (!sshkey_is_cert(k))
		return SSH_ERR_KEY_TYPE_UNKNOWN;
	if (!sshkey_type_is_valid_ca(ca->type))
		return SSH_ERR_KEY_CERT_INVALID_SIGN_KEY;

	if ((ret = sshkey_to_blob(ca, &ca_blob, &ca_len)) != 0)
		return SSH_ERR_KEY_CERT_INVALID_SIGN_KEY;

	cert = k->cert->certblob; /* for readability */
	sshbuf_reset(cert);
	if ((ret = sshbuf_put_cstring(cert, sshkey_ssh_name(k))) != 0)
		goto out;

	/* -v01 certs put nonce first */
	arc4random_buf(&nonce, sizeof(nonce));
	if ((ret = sshbuf_put_string(cert, nonce, sizeof(nonce))) != 0)
		goto out;

	/* XXX this substantially duplicates to_blob(); refactor */
	switch (k->type) {
#ifdef WITH_OPENSSL
	case KEY_DSA_CERT:
		DSA_get0_pqg(k->dsa, &dsa_p, &dsa_q, &dsa_g);
		DSA_get0_key(k->dsa, &dsa_pub_key, NULL);
		if ((ret = sshbuf_put_bignum2(cert, dsa_p)) != 0 ||
		    (ret = sshbuf_put_bignum2(cert, dsa_q)) != 0 ||
		    (ret = sshbuf_put_bignum2(cert, dsa_g)) != 0 ||
		    (ret = sshbuf_put_bignum2(cert, dsa_pub_key)) != 0)
			goto out;
		break;
# ifdef OPENSSL_HAS_ECC
	case KEY_ECDSA_CERT:
		if ((ret = sshbuf_put_cstring(cert,
		    sshkey_curve_nid_to_name(k->ecdsa_nid))) != 0 ||
		    (ret = sshbuf_put_ec(cert,
		    EC_KEY_get0_public_key(k->ecdsa),
		    EC_KEY_get0_group(k->ecdsa))) != 0)
			goto out;
		break;
# endif /* OPENSSL_HAS_ECC */
	case KEY_RSA_CERT:
		RSA_get0_key(k->rsa, &rsa_n, &rsa_e, NULL);
		if ((ret = sshbuf_put_bignum2(cert, rsa_e)) != 0 ||
		    (ret = sshbuf_put_bignum2(cert, rsa_n)) != 0)
			goto out;
		break;
#endif /* WITH_OPENSSL */
	case KEY_ED25519_CERT:
		if ((ret = sshbuf_put_string(cert,
		    k->ed25519_pk, ED25519_PK_SZ)) != 0)
			goto out;
		break;
#ifdef WITH_XMSS
	case KEY_XMSS_CERT:
		if (k->xmss_name == NULL) {
			ret = SSH_ERR_INVALID_ARGUMENT;
			goto out;
		}
		if ((ret = sshbuf_put_cstring(cert, k->xmss_name)) ||
		    (ret = sshbuf_put_string(cert,
		    k->xmss_pk, sshkey_xmss_pklen(k))) != 0)
			goto out;
		break;
#endif /* WITH_XMSS */
	default:
		ret = SSH_ERR_INVALID_ARGUMENT;
		goto out;
	}

	if ((ret = sshbuf_put_u64(cert, k->cert->serial)) != 0 ||
	    (ret = sshbuf_put_u32(cert, k->cert->type)) != 0 ||
	    (ret = sshbuf_put_cstring(cert, k->cert->key_id)) != 0)
		goto out;

	if ((principals = sshbuf_new()) == NULL) {
		ret = SSH_ERR_ALLOC_FAIL;
		goto out;
	}
	for (i = 0; i < k->cert->nprincipals; i++) {
		if ((ret = sshbuf_put_cstring(principals,
		    k->cert->principals[i])) != 0)
			goto out;
	}
	if ((ret = sshbuf_put_stringb(cert, principals)) != 0 ||
	    (ret = sshbuf_put_u64(cert, k->cert->valid_after)) != 0 ||
	    (ret = sshbuf_put_u64(cert, k->cert->valid_before)) != 0 ||
	    (ret = sshbuf_put_stringb(cert, k->cert->critical)) != 0 ||
	    (ret = sshbuf_put_stringb(cert, k->cert->extensions)) != 0 ||
	    (ret = sshbuf_put_string(cert, NULL, 0)) != 0 || /* Reserved */
	    (ret = sshbuf_put_string(cert, ca_blob, ca_len)) != 0)
		goto out;

	/* Sign the whole mess */
	if ((ret = signer(ca, &sig_blob, &sig_len, sshbuf_ptr(cert),
	    sshbuf_len(cert), alg, 0, signer_ctx)) != 0)
		goto out;

	/* Append signature and we are done */
	if ((ret = sshbuf_put_string(cert, sig_blob, sig_len)) != 0)
		goto out;
	ret = 0;
 out:
	if (ret != 0)
		sshbuf_reset(cert);
	free(sig_blob);
	free(ca_blob);
	sshbuf_free(principals);
	return ret;
}

static int
default_key_sign(const struct sshkey *key, u_char **sigp, size_t *lenp,
    const u_char *data, size_t datalen,
    const char *alg, u_int compat, void *ctx)
{
	if (ctx != NULL)
		return SSH_ERR_INVALID_ARGUMENT;
	return sshkey_sign(key, sigp, lenp, data, datalen, alg, compat);
}

int
sshkey_certify(struct sshkey *k, struct sshkey *ca, const char *alg)
{
	return sshkey_certify_custom(k, ca, alg, default_key_sign, NULL);
}

int
sshkey_cert_check_authority(const struct sshkey *k,
    int want_host, int require_principal,
    const char *name, const char **reason)
{
	u_int i, principal_matches;
	time_t now = time(NULL);

	if (reason != NULL)
		*reason = NULL;

	if (want_host) {
		if (k->cert->type != SSH2_CERT_TYPE_HOST) {
			*reason = "Certificate invalid: not a host certificate";
			return SSH_ERR_KEY_CERT_INVALID;
		}
	} else {
		if (k->cert->type != SSH2_CERT_TYPE_USER) {
			*reason = "Certificate invalid: not a user certificate";
			return SSH_ERR_KEY_CERT_INVALID;
		}
	}
	if (now < 0) {
		/* yikes - system clock before epoch! */
		*reason = "Certificate invalid: not yet valid";
		return SSH_ERR_KEY_CERT_INVALID;
	}
	if ((u_int64_t)now < k->cert->valid_after) {
		*reason = "Certificate invalid: not yet valid";
		return SSH_ERR_KEY_CERT_INVALID;
	}
	if ((u_int64_t)now >= k->cert->valid_before) {
		*reason = "Certificate invalid: expired";
		return SSH_ERR_KEY_CERT_INVALID;
	}
	if (k->cert->nprincipals == 0) {
		if (require_principal) {
			*reason = "Certificate lacks principal list";
			return SSH_ERR_KEY_CERT_INVALID;
		}
	} else if (name != NULL) {
		principal_matches = 0;
		for (i = 0; i < k->cert->nprincipals; i++) {
			if (strcmp(name, k->cert->principals[i]) == 0) {
				principal_matches = 1;
				break;
			}
		}
		if (!principal_matches) {
			*reason = "Certificate invalid: name is not a listed "
			    "principal";
			return SSH_ERR_KEY_CERT_INVALID;
		}
	}
	return 0;
}

size_t
sshkey_format_cert_validity(const struct sshkey_cert *cert, char *s, size_t l)
{
	char from[32], to[32], ret[64];
	time_t tt;
	struct tm *tm;

	*from = *to = '\0';
	if (cert->valid_after == 0 &&
	    cert->valid_before == 0xffffffffffffffffULL)
		return strlcpy(s, "forever", l);

	if (cert->valid_after != 0) {
		/* XXX revisit INT_MAX in 2038 :) */
		tt = cert->valid_after > INT_MAX ?
		    INT_MAX : cert->valid_after;
		tm = localtime(&tt);
		strftime(from, sizeof(from), "%Y-%m-%dT%H:%M:%S", tm);
	}
	if (cert->valid_before != 0xffffffffffffffffULL) {
		/* XXX revisit INT_MAX in 2038 :) */
		tt = cert->valid_before > INT_MAX ?
		    INT_MAX : cert->valid_before;
		tm = localtime(&tt);
		strftime(to, sizeof(to), "%Y-%m-%dT%H:%M:%S", tm);
	}

	if (cert->valid_after == 0)
		snprintf(ret, sizeof(ret), "before %s", to);
	else if (cert->valid_before == 0xffffffffffffffffULL)
		snprintf(ret, sizeof(ret), "after %s", from);
	else
		snprintf(ret, sizeof(ret), "from %s to %s", from, to);

	return strlcpy(s, ret, l);
}

int
sshkey_private_serialize_opt(const struct sshkey *key, struct sshbuf *b,
    enum sshkey_serialize_rep opts)
{
	int r = SSH_ERR_INTERNAL_ERROR;
#ifdef WITH_OPENSSL
	const BIGNUM *rsa_n, *rsa_e, *rsa_d, *rsa_iqmp, *rsa_p, *rsa_q;
	const BIGNUM *dsa_p, *dsa_q, *dsa_g, *dsa_pub_key, *dsa_priv_key;
#endif /* WITH_OPENSSL */

	if ((r = sshbuf_put_cstring(b, sshkey_ssh_name(key))) != 0)
		goto out;
	switch (key->type) {
#ifdef WITH_OPENSSL
	case KEY_RSA:
		RSA_get0_key(key->rsa, &rsa_n, &rsa_e, &rsa_d);
		RSA_get0_factors(key->rsa, &rsa_p, &rsa_q);
		RSA_get0_crt_params(key->rsa, NULL, NULL, &rsa_iqmp);
		if ((r = sshbuf_put_bignum2(b, rsa_n)) != 0 ||
		    (r = sshbuf_put_bignum2(b, rsa_e)) != 0 ||
		    (r = sshbuf_put_bignum2(b, rsa_d)) != 0 ||
		    (r = sshbuf_put_bignum2(b, rsa_iqmp)) != 0 ||
		    (r = sshbuf_put_bignum2(b, rsa_p)) != 0 ||
		    (r = sshbuf_put_bignum2(b, rsa_q)) != 0)
			goto out;
		break;
	case KEY_RSA_CERT:
		if (key->cert == NULL || sshbuf_len(key->cert->certblob) == 0) {
			r = SSH_ERR_INVALID_ARGUMENT;
			goto out;
		}
		RSA_get0_key(key->rsa, NULL, NULL, &rsa_d);
		RSA_get0_factors(key->rsa, &rsa_p, &rsa_q);
		RSA_get0_crt_params(key->rsa, NULL, NULL, &rsa_iqmp);
		if ((r = sshbuf_put_stringb(b, key->cert->certblob)) != 0 ||
		    (r = sshbuf_put_bignum2(b, rsa_d)) != 0 ||
		    (r = sshbuf_put_bignum2(b, rsa_iqmp)) != 0 ||
		    (r = sshbuf_put_bignum2(b, rsa_p)) != 0 ||
		    (r = sshbuf_put_bignum2(b, rsa_q)) != 0)
			goto out;
		break;
	case KEY_DSA:
		DSA_get0_pqg(key->dsa, &dsa_p, &dsa_q, &dsa_g);
		DSA_get0_key(key->dsa, &dsa_pub_key, &dsa_priv_key);
		if ((r = sshbuf_put_bignum2(b, dsa_p)) != 0 ||
		    (r = sshbuf_put_bignum2(b, dsa_q)) != 0 ||
		    (r = sshbuf_put_bignum2(b, dsa_g)) != 0 ||
		    (r = sshbuf_put_bignum2(b, dsa_pub_key)) != 0 ||
		    (r = sshbuf_put_bignum2(b, dsa_priv_key)) != 0)
			goto out;
		break;
	case KEY_DSA_CERT:
		if (key->cert == NULL || sshbuf_len(key->cert->certblob) == 0) {
			r = SSH_ERR_INVALID_ARGUMENT;
			goto out;
		}
		DSA_get0_key(key->dsa, NULL, &dsa_priv_key);
		if ((r = sshbuf_put_stringb(b, key->cert->certblob)) != 0 ||
		    (r = sshbuf_put_bignum2(b, dsa_priv_key)) != 0)
			goto out;
		break;
# ifdef OPENSSL_HAS_ECC
	case KEY_ECDSA:
		if ((r = sshbuf_put_cstring(b,
		    sshkey_curve_nid_to_name(key->ecdsa_nid))) != 0 ||
		    (r = sshbuf_put_eckey(b, key->ecdsa)) != 0 ||
		    (r = sshbuf_put_bignum2(b,
		    EC_KEY_get0_private_key(key->ecdsa))) != 0)
			goto out;
		break;
	case KEY_ECDSA_CERT:
		if (key->cert == NULL || sshbuf_len(key->cert->certblob) == 0) {
			r = SSH_ERR_INVALID_ARGUMENT;
			goto out;
		}
		if ((r = sshbuf_put_stringb(b, key->cert->certblob)) != 0 ||
		    (r = sshbuf_put_bignum2(b,
		    EC_KEY_get0_private_key(key->ecdsa))) != 0)
			goto out;
		break;
# endif /* OPENSSL_HAS_ECC */
#endif /* WITH_OPENSSL */
	case KEY_ED25519:
		if ((r = sshbuf_put_string(b, key->ed25519_pk,
		    ED25519_PK_SZ)) != 0 ||
		    (r = sshbuf_put_string(b, key->ed25519_sk,
		    ED25519_SK_SZ)) != 0)
			goto out;
		break;
	case KEY_ED25519_CERT:
		if (key->cert == NULL || sshbuf_len(key->cert->certblob) == 0) {
			r = SSH_ERR_INVALID_ARGUMENT;
			goto out;
		}
		if ((r = sshbuf_put_stringb(b, key->cert->certblob)) != 0 ||
		    (r = sshbuf_put_string(b, key->ed25519_pk,
		    ED25519_PK_SZ)) != 0 ||
		    (r = sshbuf_put_string(b, key->ed25519_sk,
		    ED25519_SK_SZ)) != 0)
			goto out;
		break;
#ifdef WITH_XMSS
	case KEY_XMSS:
		if (key->xmss_name == NULL) {
			r = SSH_ERR_INVALID_ARGUMENT;
			goto out;
		}
		if ((r = sshbuf_put_cstring(b, key->xmss_name)) != 0 ||
		    (r = sshbuf_put_string(b, key->xmss_pk,
		    sshkey_xmss_pklen(key))) != 0 ||
		    (r = sshbuf_put_string(b, key->xmss_sk,
		    sshkey_xmss_sklen(key))) != 0 ||
		    (r = sshkey_xmss_serialize_state_opt(key, b, opts)) != 0)
			goto out;
		break;
	case KEY_XMSS_CERT:
		if (key->cert == NULL || sshbuf_len(key->cert->certblob) == 0 ||
		    key->xmss_name == NULL) {
			r = SSH_ERR_INVALID_ARGUMENT;
			goto out;
		}
		if ((r = sshbuf_put_stringb(b, key->cert->certblob)) != 0 ||
		    (r = sshbuf_put_cstring(b, key->xmss_name)) != 0 ||
		    (r = sshbuf_put_string(b, key->xmss_pk,
		    sshkey_xmss_pklen(key))) != 0 ||
		    (r = sshbuf_put_string(b, key->xmss_sk,
		    sshkey_xmss_sklen(key))) != 0 ||
		    (r = sshkey_xmss_serialize_state_opt(key, b, opts)) != 0)
			goto out;
		break;
#endif /* WITH_XMSS */
	default:
		r = SSH_ERR_INVALID_ARGUMENT;
		goto out;
	}
	/* success */
	r = 0;
 out:
	return r;
}

int
sshkey_private_serialize(const struct sshkey *key, struct sshbuf *b)
{
	return sshkey_private_serialize_opt(key, b,
	    SSHKEY_SERIALIZE_DEFAULT);
}

int
sshkey_private_deserialize(struct sshbuf *buf, struct sshkey **kp)
{
	char *tname = NULL, *curve = NULL, *xmss_name = NULL;
	struct sshkey *k = NULL;
	size_t pklen = 0, sklen = 0;
	int type, r = SSH_ERR_INTERNAL_ERROR;
	u_char *ed25519_pk = NULL, *ed25519_sk = NULL;
	u_char *xmss_pk = NULL, *xmss_sk = NULL;
#ifdef WITH_OPENSSL
	BIGNUM *exponent = NULL;
	BIGNUM *rsa_n = NULL, *rsa_e = NULL, *rsa_d = NULL;
	BIGNUM *rsa_iqmp = NULL, *rsa_p = NULL, *rsa_q = NULL;
	BIGNUM *dsa_p = NULL, *dsa_q = NULL, *dsa_g = NULL;
	BIGNUM *dsa_pub_key = NULL, *dsa_priv_key = NULL;
#endif /* WITH_OPENSSL */

	if (kp != NULL)
		*kp = NULL;
	if ((r = sshbuf_get_cstring(buf, &tname, NULL)) != 0)
		goto out;
	type = sshkey_type_from_name(tname);
	switch (type) {
#ifdef WITH_OPENSSL
	case KEY_DSA:
		if ((k = sshkey_new_private(type)) == NULL) {
			r = SSH_ERR_ALLOC_FAIL;
			goto out;
		}
		if ((dsa_p = BN_new()) == NULL ||
		    (dsa_q = BN_new()) == NULL ||
		    (dsa_g = BN_new()) == NULL ||
		    (dsa_pub_key = BN_new()) == NULL ||
		    (dsa_priv_key = BN_new()) == NULL) {
			r = SSH_ERR_ALLOC_FAIL;
			goto out;
		}
		if ((r = sshbuf_get_bignum2(buf, dsa_p)) != 0 ||
		    (r = sshbuf_get_bignum2(buf, dsa_q)) != 0 ||
		    (r = sshbuf_get_bignum2(buf, dsa_g)) != 0 ||
		    (r = sshbuf_get_bignum2(buf, dsa_pub_key)) != 0 ||
		    (r = sshbuf_get_bignum2(buf, dsa_priv_key)) != 0)
			goto out;
		if (!DSA_set0_pqg(k->dsa, dsa_p, dsa_q, dsa_g)) {
			r = SSH_ERR_LIBCRYPTO_ERROR;
			goto out;
		}
		dsa_p = dsa_q = dsa_g = NULL; /* transferred */
		if (!DSA_set0_key(k->dsa, dsa_pub_key, dsa_priv_key)) {
			r = SSH_ERR_LIBCRYPTO_ERROR;
			goto out;
		}
		dsa_pub_key = dsa_priv_key = NULL; /* transferred */
		break;
	case KEY_DSA_CERT:
		if ((dsa_priv_key = BN_new()) == NULL) {
			r = SSH_ERR_ALLOC_FAIL;
			goto out;
		}
		if ((r = sshkey_froms(buf, &k)) != 0 ||
		    (r = sshbuf_get_bignum2(buf, dsa_priv_key)) != 0)
			goto out;
		if (!DSA_set0_key(k->dsa, NULL, dsa_priv_key)) {
			r = SSH_ERR_LIBCRYPTO_ERROR;
			goto out;
		}
		dsa_priv_key = NULL; /* transferred */
		break;
# ifdef OPENSSL_HAS_ECC
	case KEY_ECDSA:
		if ((k = sshkey_new_private(type)) == NULL) {
			r = SSH_ERR_ALLOC_FAIL;
			goto out;
		}
		if ((k->ecdsa_nid = sshkey_ecdsa_nid_from_name(tname)) == -1) {
			r = SSH_ERR_INVALID_ARGUMENT;
			goto out;
		}
		if ((r = sshbuf_get_cstring(buf, &curve, NULL)) != 0)
			goto out;
		if (k->ecdsa_nid != sshkey_curve_name_to_nid(curve)) {
			r = SSH_ERR_EC_CURVE_MISMATCH;
			goto out;
		}
		k->ecdsa = EC_KEY_new_by_curve_name(k->ecdsa_nid);
		if (k->ecdsa  == NULL || (exponent = BN_new()) == NULL) {
			r = SSH_ERR_LIBCRYPTO_ERROR;
			goto out;
		}
		if ((r = sshbuf_get_eckey(buf, k->ecdsa)) != 0 ||
		    (r = sshbuf_get_bignum2(buf, exponent)))
			goto out;
		if (EC_KEY_set_private_key(k->ecdsa, exponent) != 1) {
			r = SSH_ERR_LIBCRYPTO_ERROR;
			goto out;
		}
		if ((r = sshkey_ec_validate_public(EC_KEY_get0_group(k->ecdsa),
		    EC_KEY_get0_public_key(k->ecdsa))) != 0 ||
		    (r = sshkey_ec_validate_private(k->ecdsa)) != 0)
			goto out;
		break;
	case KEY_ECDSA_CERT:
		if ((exponent = BN_new()) == NULL) {
			r = SSH_ERR_LIBCRYPTO_ERROR;
			goto out;
		}
		if ((r = sshkey_froms(buf, &k)) != 0 ||
		    (r = sshbuf_get_bignum2(buf, exponent)) != 0)
			goto out;
		if (EC_KEY_set_private_key(k->ecdsa, exponent) != 1) {
			r = SSH_ERR_LIBCRYPTO_ERROR;
			goto out;
		}
		if ((r = sshkey_ec_validate_public(EC_KEY_get0_group(k->ecdsa),
		    EC_KEY_get0_public_key(k->ecdsa))) != 0 ||
		    (r = sshkey_ec_validate_private(k->ecdsa)) != 0)
			goto out;
		break;
# endif /* OPENSSL_HAS_ECC */
	case KEY_RSA:
		if ((k = sshkey_new_private(type)) == NULL) {
			r = SSH_ERR_ALLOC_FAIL;
			goto out;
		}
		if ((rsa_n = BN_new()) == NULL ||
		    (rsa_e = BN_new()) == NULL ||
		    (rsa_d = BN_new()) == NULL ||
		    (rsa_iqmp = BN_new()) == NULL ||
		    (rsa_p = BN_new()) == NULL ||
		    (rsa_q = BN_new()) == NULL) {
			r = SSH_ERR_ALLOC_FAIL;
			goto out;
		}
		if ((r = sshbuf_get_bignum2(buf, rsa_n)) != 0 ||
		    (r = sshbuf_get_bignum2(buf, rsa_e)) != 0 ||
		    (r = sshbuf_get_bignum2(buf, rsa_d)) != 0 ||
		    (r = sshbuf_get_bignum2(buf, rsa_iqmp)) != 0 ||
		    (r = sshbuf_get_bignum2(buf, rsa_p)) != 0 ||
		    (r = sshbuf_get_bignum2(buf, rsa_q)) != 0)
			goto out;
		if (!RSA_set0_key(k->rsa, rsa_n, rsa_e, rsa_d)) {
			r = SSH_ERR_LIBCRYPTO_ERROR;
			goto out;
		}
		rsa_n = rsa_e = rsa_d = NULL; /* transferred */
		if (!RSA_set0_factors(k->rsa, rsa_p, rsa_q)) {
			r = SSH_ERR_LIBCRYPTO_ERROR;
			goto out;
		}
		rsa_p = rsa_q = NULL; /* transferred */
		if ((r = check_rsa_length(k->rsa)) != 0)
			goto out;
		if ((r = ssh_rsa_complete_crt_parameters(k, rsa_iqmp)) != 0)
			goto out;
		break;
	case KEY_RSA_CERT:
		if ((rsa_d = BN_new()) == NULL ||
		    (rsa_iqmp = BN_new()) == NULL ||
		    (rsa_p = BN_new()) == NULL ||
		    (rsa_q = BN_new()) == NULL) {
			r = SSH_ERR_ALLOC_FAIL;
			goto out;
		}
		if ((r = sshkey_froms(buf, &k)) != 0 ||
		    (r = sshbuf_get_bignum2(buf, rsa_d)) != 0 ||
		    (r = sshbuf_get_bignum2(buf, rsa_iqmp)) != 0 ||
		    (r = sshbuf_get_bignum2(buf, rsa_p)) != 0 ||
		    (r = sshbuf_get_bignum2(buf, rsa_q)) != 0)
			goto out;
		if (!RSA_set0_key(k->rsa, NULL, NULL, rsa_d)) {
			r = SSH_ERR_LIBCRYPTO_ERROR;
			goto out;
		}
		rsa_d = NULL; /* transferred */
		if (!RSA_set0_factors(k->rsa, rsa_p, rsa_q)) {
			r = SSH_ERR_LIBCRYPTO_ERROR;
			goto out;
		}
		rsa_p = rsa_q = NULL; /* transferred */
		if ((r = check_rsa_length(k->rsa)) != 0)
			goto out;
		if ((r = ssh_rsa_complete_crt_parameters(k, rsa_iqmp)) != 0)
			goto out;
		break;
#endif /* WITH_OPENSSL */
	case KEY_ED25519:
		if ((k = sshkey_new_private(type)) == NULL) {
			r = SSH_ERR_ALLOC_FAIL;
			goto out;
		}
		if ((r = sshbuf_get_string(buf, &ed25519_pk, &pklen)) != 0 ||
		    (r = sshbuf_get_string(buf, &ed25519_sk, &sklen)) != 0)
			goto out;
		if (pklen != ED25519_PK_SZ || sklen != ED25519_SK_SZ) {
			r = SSH_ERR_INVALID_FORMAT;
			goto out;
		}
		k->ed25519_pk = ed25519_pk;
		k->ed25519_sk = ed25519_sk;
		ed25519_pk = ed25519_sk = NULL;
		break;
	case KEY_ED25519_CERT:
		if ((r = sshkey_froms(buf, &k)) != 0 ||
		    (r = sshbuf_get_string(buf, &ed25519_pk, &pklen)) != 0 ||
		    (r = sshbuf_get_string(buf, &ed25519_sk, &sklen)) != 0)
			goto out;
		if (pklen != ED25519_PK_SZ || sklen != ED25519_SK_SZ) {
			r = SSH_ERR_INVALID_FORMAT;
			goto out;
		}
		k->ed25519_pk = ed25519_pk;
		k->ed25519_sk = ed25519_sk;
		ed25519_pk = ed25519_sk = NULL;
		break;
#ifdef WITH_XMSS
	case KEY_XMSS:
		if ((k = sshkey_new_private(type)) == NULL) {
			r = SSH_ERR_ALLOC_FAIL;
			goto out;
		}
		if ((r = sshbuf_get_cstring(buf, &xmss_name, NULL)) != 0 ||
		    (r = sshkey_xmss_init(k, xmss_name)) != 0 ||
		    (r = sshbuf_get_string(buf, &xmss_pk, &pklen)) != 0 ||
		    (r = sshbuf_get_string(buf, &xmss_sk, &sklen)) != 0)
			goto out;
		if (pklen != sshkey_xmss_pklen(k) ||
		    sklen != sshkey_xmss_sklen(k)) {
			r = SSH_ERR_INVALID_FORMAT;
			goto out;
		}
		k->xmss_pk = xmss_pk;
		k->xmss_sk = xmss_sk;
		xmss_pk = xmss_sk = NULL;
		/* optional internal state */
		if ((r = sshkey_xmss_deserialize_state_opt(k, buf)) != 0)
			goto out;
		break;
	case KEY_XMSS_CERT:
		if ((r = sshkey_froms(buf, &k)) != 0 ||
		    (r = sshbuf_get_cstring(buf, &xmss_name, NULL)) != 0 ||
		    (r = sshbuf_get_string(buf, &xmss_pk, &pklen)) != 0 ||
		    (r = sshbuf_get_string(buf, &xmss_sk, &sklen)) != 0)
			goto out;
		if (strcmp(xmss_name, k->xmss_name)) {
			r = SSH_ERR_INVALID_FORMAT;
			goto out;
		}
		if (pklen != sshkey_xmss_pklen(k) ||
		    sklen != sshkey_xmss_sklen(k)) {
			r = SSH_ERR_INVALID_FORMAT;
			goto out;
		}
		k->xmss_pk = xmss_pk;
		k->xmss_sk = xmss_sk;
		xmss_pk = xmss_sk = NULL;
		/* optional internal state */
		if ((r = sshkey_xmss_deserialize_state_opt(k, buf)) != 0)
			goto out;
		break;
#endif /* WITH_XMSS */
	default:
		r = SSH_ERR_KEY_TYPE_UNKNOWN;
		goto out;
	}
#ifdef WITH_OPENSSL
	/* enable blinding */
	switch (k->type) {
	case KEY_RSA:
	case KEY_RSA_CERT:
		if (RSA_blinding_on(k->rsa, NULL) != 1) {
			r = SSH_ERR_LIBCRYPTO_ERROR;
			goto out;
		}
		break;
	}
#endif /* WITH_OPENSSL */
	/* success */
	r = 0;
	if (kp != NULL) {
		*kp = k;
		k = NULL;
	}
 out:
	free(tname);
	free(curve);
#ifdef WITH_OPENSSL
	BN_clear_free(exponent);
	BN_clear_free(dsa_p);
	BN_clear_free(dsa_q);
	BN_clear_free(dsa_g);
	BN_clear_free(dsa_pub_key);
	BN_clear_free(dsa_priv_key);
	BN_clear_free(rsa_n);
	BN_clear_free(rsa_e);
	BN_clear_free(rsa_d);
	BN_clear_free(rsa_p);
	BN_clear_free(rsa_q);
	BN_clear_free(rsa_iqmp);
#endif /* WITH_OPENSSL */
	sshkey_free(k);
	freezero(ed25519_pk, pklen);
	freezero(ed25519_sk, sklen);
	free(xmss_name);
	freezero(xmss_pk, pklen);
	freezero(xmss_sk, sklen);
	return r;
}

#if defined(WITH_OPENSSL) && defined(OPENSSL_HAS_ECC)
int
sshkey_ec_validate_public(const EC_GROUP *group, const EC_POINT *public)
{
	BN_CTX *bnctx;
	EC_POINT *nq = NULL;
	BIGNUM *order, *x, *y, *tmp;
	int ret = SSH_ERR_KEY_INVALID_EC_VALUE;

	/*
	 * NB. This assumes OpenSSL has already verified that the public
	 * point lies on the curve. This is done by EC_POINT_oct2point()
	 * implicitly calling EC_POINT_is_on_curve(). If this code is ever
	 * reachable with public points not unmarshalled using
	 * EC_POINT_oct2point then the caller will need to explicitly check.
	 */

	if ((bnctx = BN_CTX_new()) == NULL)
		return SSH_ERR_ALLOC_FAIL;
	BN_CTX_start(bnctx);

	/*
	 * We shouldn't ever hit this case because bignum_get_ecpoint()
	 * refuses to load GF2m points.
	 */
	if (EC_METHOD_get_field_type(EC_GROUP_method_of(group)) !=
	    NID_X9_62_prime_field)
		goto out;

	/* Q != infinity */
	if (EC_POINT_is_at_infinity(group, public))
		goto out;

	if ((x = BN_CTX_get(bnctx)) == NULL ||
	    (y = BN_CTX_get(bnctx)) == NULL ||
	    (order = BN_CTX_get(bnctx)) == NULL ||
	    (tmp = BN_CTX_get(bnctx)) == NULL) {
		ret = SSH_ERR_ALLOC_FAIL;
		goto out;
	}

	/* log2(x) > log2(order)/2, log2(y) > log2(order)/2 */
	if (EC_GROUP_get_order(group, order, bnctx) != 1 ||
	    EC_POINT_get_affine_coordinates_GFp(group, public,
	    x, y, bnctx) != 1) {
		ret = SSH_ERR_LIBCRYPTO_ERROR;
		goto out;
	}
	if (BN_num_bits(x) <= BN_num_bits(order) / 2 ||
	    BN_num_bits(y) <= BN_num_bits(order) / 2)
		goto out;

	/* nQ == infinity (n == order of subgroup) */
	if ((nq = EC_POINT_new(group)) == NULL) {
		ret = SSH_ERR_ALLOC_FAIL;
		goto out;
	}
	if (EC_POINT_mul(group, nq, NULL, public, order, bnctx) != 1) {
		ret = SSH_ERR_LIBCRYPTO_ERROR;
		goto out;
	}
	if (EC_POINT_is_at_infinity(group, nq) != 1)
		goto out;

	/* x < order - 1, y < order - 1 */
	if (!BN_sub(tmp, order, BN_value_one())) {
		ret = SSH_ERR_LIBCRYPTO_ERROR;
		goto out;
	}
	if (BN_cmp(x, tmp) >= 0 || BN_cmp(y, tmp) >= 0)
		goto out;
	ret = 0;
 out:
	BN_CTX_free(bnctx);
	EC_POINT_free(nq);
	return ret;
}

int
sshkey_ec_validate_private(const EC_KEY *key)
{
	BN_CTX *bnctx;
	BIGNUM *order, *tmp;
	int ret = SSH_ERR_KEY_INVALID_EC_VALUE;

	if ((bnctx = BN_CTX_new()) == NULL)
		return SSH_ERR_ALLOC_FAIL;
	BN_CTX_start(bnctx);

	if ((order = BN_CTX_get(bnctx)) == NULL ||
	    (tmp = BN_CTX_get(bnctx)) == NULL) {
		ret = SSH_ERR_ALLOC_FAIL;
		goto out;
	}

	/* log2(private) > log2(order)/2 */
	if (EC_GROUP_get_order(EC_KEY_get0_group(key), order, bnctx) != 1) {
		ret = SSH_ERR_LIBCRYPTO_ERROR;
		goto out;
	}
	if (BN_num_bits(EC_KEY_get0_private_key(key)) <=
	    BN_num_bits(order) / 2)
		goto out;

	/* private < order - 1 */
	if (!BN_sub(tmp, order, BN_value_one())) {
		ret = SSH_ERR_LIBCRYPTO_ERROR;
		goto out;
	}
	if (BN_cmp(EC_KEY_get0_private_key(key), tmp) >= 0)
		goto out;
	ret = 0;
 out:
	BN_CTX_free(bnctx);
	return ret;
}

void
sshkey_dump_ec_point(const EC_GROUP *group, const EC_POINT *point)
{
	BIGNUM *x, *y;
	BN_CTX *bnctx;

	if (point == NULL) {
		fputs("point=(NULL)\n", stderr);
		return;
	}
	if ((bnctx = BN_CTX_new()) == NULL) {
		fprintf(stderr, "%s: BN_CTX_new failed\n", __func__);
		return;
	}
	BN_CTX_start(bnctx);
	if ((x = BN_CTX_get(bnctx)) == NULL ||
	    (y = BN_CTX_get(bnctx)) == NULL) {
		fprintf(stderr, "%s: BN_CTX_get failed\n", __func__);
		return;
	}
	if (EC_METHOD_get_field_type(EC_GROUP_method_of(group)) !=
	    NID_X9_62_prime_field) {
		fprintf(stderr, "%s: group is not a prime field\n", __func__);
		return;
	}
	if (EC_POINT_get_affine_coordinates_GFp(group, point, x, y,
	    bnctx) != 1) {
		fprintf(stderr, "%s: EC_POINT_get_affine_coordinates_GFp\n",
		    __func__);
		return;
	}
	fputs("x=", stderr);
	BN_print_fp(stderr, x);
	fputs("\ny=", stderr);
	BN_print_fp(stderr, y);
	fputs("\n", stderr);
	BN_CTX_free(bnctx);
}

void
sshkey_dump_ec_key(const EC_KEY *key)
{
	const BIGNUM *exponent;

	sshkey_dump_ec_point(EC_KEY_get0_group(key),
	    EC_KEY_get0_public_key(key));
	fputs("exponent=", stderr);
	if ((exponent = EC_KEY_get0_private_key(key)) == NULL)
		fputs("(NULL)", stderr);
	else
		BN_print_fp(stderr, EC_KEY_get0_private_key(key));
	fputs("\n", stderr);
}
#endif /* WITH_OPENSSL && OPENSSL_HAS_ECC */

static int
sshkey_private_to_blob2(const struct sshkey *prv, struct sshbuf *blob,
    const char *passphrase, const char *comment, const char *ciphername,
    int rounds)
{
	u_char *cp, *key = NULL, *pubkeyblob = NULL;
	u_char salt[SALT_LEN];
	char *b64 = NULL;
	size_t i, pubkeylen, keylen, ivlen, blocksize, authlen;
	u_int check;
	int r = SSH_ERR_INTERNAL_ERROR;
	struct sshcipher_ctx *ciphercontext = NULL;
	const struct sshcipher *cipher;
	const char *kdfname = KDFNAME;
	struct sshbuf *encoded = NULL, *encrypted = NULL, *kdf = NULL;

	if (rounds <= 0)
		rounds = DEFAULT_ROUNDS;
	if (passphrase == NULL || !strlen(passphrase)) {
		ciphername = "none";
		kdfname = "none";
	} else if (ciphername == NULL)
		ciphername = DEFAULT_CIPHERNAME;
	if ((cipher = cipher_by_name(ciphername)) == NULL) {
		r = SSH_ERR_INVALID_ARGUMENT;
		goto out;
	}

	if ((kdf = sshbuf_new()) == NULL ||
	    (encoded = sshbuf_new()) == NULL ||
	    (encrypted = sshbuf_new()) == NULL) {
		r = SSH_ERR_ALLOC_FAIL;
		goto out;
	}
	blocksize = cipher_blocksize(cipher);
	keylen = cipher_keylen(cipher);
	ivlen = cipher_ivlen(cipher);
	authlen = cipher_authlen(cipher);
	if ((key = calloc(1, keylen + ivlen)) == NULL) {
		r = SSH_ERR_ALLOC_FAIL;
		goto out;
	}
	if (strcmp(kdfname, "bcrypt") == 0) {
		arc4random_buf(salt, SALT_LEN);
		if (bcrypt_pbkdf(passphrase, strlen(passphrase),
		    salt, SALT_LEN, key, keylen + ivlen, rounds) < 0) {
			r = SSH_ERR_INVALID_ARGUMENT;
			goto out;
		}
		if ((r = sshbuf_put_string(kdf, salt, SALT_LEN)) != 0 ||
		    (r = sshbuf_put_u32(kdf, rounds)) != 0)
			goto out;
	} else if (strcmp(kdfname, "none") != 0) {
		/* Unsupported KDF type */
		r = SSH_ERR_KEY_UNKNOWN_CIPHER;
		goto out;
	}
	if ((r = cipher_init(&ciphercontext, cipher, key, keylen,
	    key + keylen, ivlen, 1)) != 0)
		goto out;

	if ((r = sshbuf_put(encoded, AUTH_MAGIC, sizeof(AUTH_MAGIC))) != 0 ||
	    (r = sshbuf_put_cstring(encoded, ciphername)) != 0 ||
	    (r = sshbuf_put_cstring(encoded, kdfname)) != 0 ||
	    (r = sshbuf_put_stringb(encoded, kdf)) != 0 ||
	    (r = sshbuf_put_u32(encoded, 1)) != 0 ||	/* number of keys */
	    (r = sshkey_to_blob(prv, &pubkeyblob, &pubkeylen)) != 0 ||
	    (r = sshbuf_put_string(encoded, pubkeyblob, pubkeylen)) != 0)
		goto out;

	/* set up the buffer that will be encrypted */

	/* Random check bytes */
	check = arc4random();
	if ((r = sshbuf_put_u32(encrypted, check)) != 0 ||
	    (r = sshbuf_put_u32(encrypted, check)) != 0)
		goto out;

	/* append private key and comment*/
	if ((r = sshkey_private_serialize_opt(prv, encrypted,
	     SSHKEY_SERIALIZE_FULL)) != 0 ||
	    (r = sshbuf_put_cstring(encrypted, comment)) != 0)
		goto out;

	/* padding */
	i = 0;
	while (sshbuf_len(encrypted) % blocksize) {
		if ((r = sshbuf_put_u8(encrypted, ++i & 0xff)) != 0)
			goto out;
	}

	/* length in destination buffer */
	if ((r = sshbuf_put_u32(encoded, sshbuf_len(encrypted))) != 0)
		goto out;

	/* encrypt */
	if ((r = sshbuf_reserve(encoded,
	    sshbuf_len(encrypted) + authlen, &cp)) != 0)
		goto out;
	if ((r = cipher_crypt(ciphercontext, 0, cp,
	    sshbuf_ptr(encrypted), sshbuf_len(encrypted), 0, authlen)) != 0)
		goto out;

	/* uuencode */
	if ((b64 = sshbuf_dtob64(encoded)) == NULL) {
		r = SSH_ERR_ALLOC_FAIL;
		goto out;
	}

	sshbuf_reset(blob);
	if ((r = sshbuf_put(blob, MARK_BEGIN, MARK_BEGIN_LEN)) != 0)
		goto out;
	for (i = 0; i < strlen(b64); i++) {
		if ((r = sshbuf_put_u8(blob, b64[i])) != 0)
			goto out;
		/* insert line breaks */
		if (i % 70 == 69 && (r = sshbuf_put_u8(blob, '\n')) != 0)
			goto out;
	}
	if (i % 70 != 69 && (r = sshbuf_put_u8(blob, '\n')) != 0)
		goto out;
	if ((r = sshbuf_put(blob, MARK_END, MARK_END_LEN)) != 0)
		goto out;

	/* success */
	r = 0;

 out:
	sshbuf_free(kdf);
	sshbuf_free(encoded);
	sshbuf_free(encrypted);
	cipher_free(ciphercontext);
	explicit_bzero(salt, sizeof(salt));
	if (key != NULL) {
		explicit_bzero(key, keylen + ivlen);
		free(key);
	}
	if (pubkeyblob != NULL) {
		explicit_bzero(pubkeyblob, pubkeylen);
		free(pubkeyblob);
	}
	if (b64 != NULL) {
		explicit_bzero(b64, strlen(b64));
		free(b64);
	}
	return r;
}

static int
sshkey_parse_private2(struct sshbuf *blob, int type, const char *passphrase,
    struct sshkey **keyp, char **commentp)
{
	char *comment = NULL, *ciphername = NULL, *kdfname = NULL;
	const struct sshcipher *cipher = NULL;
	const u_char *cp;
	int r = SSH_ERR_INTERNAL_ERROR;
	size_t encoded_len;
	size_t i, keylen = 0, ivlen = 0, authlen = 0, slen = 0;
	struct sshbuf *encoded = NULL, *decoded = NULL;
	struct sshbuf *kdf = NULL, *decrypted = NULL;
	struct sshcipher_ctx *ciphercontext = NULL;
	struct sshkey *k = NULL;
	u_char *key = NULL, *salt = NULL, *dp, pad, last;
	u_int blocksize, rounds, nkeys, encrypted_len, check1, check2;

	if (keyp != NULL)
		*keyp = NULL;
	if (commentp != NULL)
		*commentp = NULL;

	if ((encoded = sshbuf_new()) == NULL ||
	    (decoded = sshbuf_new()) == NULL ||
	    (decrypted = sshbuf_new()) == NULL) {
		r = SSH_ERR_ALLOC_FAIL;
		goto out;
	}

	/* check preamble */
	cp = sshbuf_ptr(blob);
	encoded_len = sshbuf_len(blob);
	if (encoded_len < (MARK_BEGIN_LEN + MARK_END_LEN) ||
	    memcmp(cp, MARK_BEGIN, MARK_BEGIN_LEN) != 0) {
		r = SSH_ERR_INVALID_FORMAT;
		goto out;
	}
	cp += MARK_BEGIN_LEN;
	encoded_len -= MARK_BEGIN_LEN;

	/* Look for end marker, removing whitespace as we go */
	while (encoded_len > 0) {
		if (*cp != '\n' && *cp != '\r') {
			if ((r = sshbuf_put_u8(encoded, *cp)) != 0)
				goto out;
		}
		last = *cp;
		encoded_len--;
		cp++;
		if (last == '\n') {
			if (encoded_len >= MARK_END_LEN &&
			    memcmp(cp, MARK_END, MARK_END_LEN) == 0) {
				/* \0 terminate */
				if ((r = sshbuf_put_u8(encoded, 0)) != 0)
					goto out;
				break;
			}
		}
	}
	if (encoded_len == 0) {
		r = SSH_ERR_INVALID_FORMAT;
		goto out;
	}

	/* decode base64 */
	if ((r = sshbuf_b64tod(decoded, (char *)sshbuf_ptr(encoded))) != 0)
		goto out;

	/* check magic */
	if (sshbuf_len(decoded) < sizeof(AUTH_MAGIC) ||
	    memcmp(sshbuf_ptr(decoded), AUTH_MAGIC, sizeof(AUTH_MAGIC))) {
		r = SSH_ERR_INVALID_FORMAT;
		goto out;
	}
	/* parse public portion of key */
	if ((r = sshbuf_consume(decoded, sizeof(AUTH_MAGIC))) != 0 ||
	    (r = sshbuf_get_cstring(decoded, &ciphername, NULL)) != 0 ||
	    (r = sshbuf_get_cstring(decoded, &kdfname, NULL)) != 0 ||
	    (r = sshbuf_froms(decoded, &kdf)) != 0 ||
	    (r = sshbuf_get_u32(decoded, &nkeys)) != 0 ||
	    (r = sshbuf_skip_string(decoded)) != 0 || /* pubkey */
	    (r = sshbuf_get_u32(decoded, &encrypted_len)) != 0)
		goto out;

	if ((cipher = cipher_by_name(ciphername)) == NULL) {
		r = SSH_ERR_KEY_UNKNOWN_CIPHER;
		goto out;
	}
	if ((passphrase == NULL || strlen(passphrase) == 0) &&
	    strcmp(ciphername, "none") != 0) {
		/* passphrase required */
		r = SSH_ERR_KEY_WRONG_PASSPHRASE;
		goto out;
	}
	if (strcmp(kdfname, "none") != 0 && strcmp(kdfname, "bcrypt") != 0) {
		r = SSH_ERR_KEY_UNKNOWN_CIPHER;
		goto out;
	}
	if (!strcmp(kdfname, "none") && strcmp(ciphername, "none") != 0) {
		r = SSH_ERR_INVALID_FORMAT;
		goto out;
	}
	if (nkeys != 1) {
		/* XXX only one key supported */
		r = SSH_ERR_INVALID_FORMAT;
		goto out;
	}

	/* check size of encrypted key blob */
	blocksize = cipher_blocksize(cipher);
	if (encrypted_len < blocksize || (encrypted_len % blocksize) != 0) {
		r = SSH_ERR_INVALID_FORMAT;
		goto out;
	}

	/* setup key */
	keylen = cipher_keylen(cipher);
	ivlen = cipher_ivlen(cipher);
	authlen = cipher_authlen(cipher);
	if ((key = calloc(1, keylen + ivlen)) == NULL) {
		r = SSH_ERR_ALLOC_FAIL;
		goto out;
	}
	if (strcmp(kdfname, "bcrypt") == 0) {
		if ((r = sshbuf_get_string(kdf, &salt, &slen)) != 0 ||
		    (r = sshbuf_get_u32(kdf, &rounds)) != 0)
			goto out;
		if (bcrypt_pbkdf(passphrase, strlen(passphrase), salt, slen,
		    key, keylen + ivlen, rounds) < 0) {
			r = SSH_ERR_INVALID_FORMAT;
			goto out;
		}
	}

	/* check that an appropriate amount of auth data is present */
	if (sshbuf_len(decoded) < encrypted_len + authlen) {
		r = SSH_ERR_INVALID_FORMAT;
		goto out;
	}

	/* decrypt private portion of key */
	if ((r = sshbuf_reserve(decrypted, encrypted_len, &dp)) != 0 ||
	    (r = cipher_init(&ciphercontext, cipher, key, keylen,
	    key + keylen, ivlen, 0)) != 0)
		goto out;
	if ((r = cipher_crypt(ciphercontext, 0, dp, sshbuf_ptr(decoded),
	    encrypted_len, 0, authlen)) != 0) {
		/* an integrity error here indicates an incorrect passphrase */
		if (r == SSH_ERR_MAC_INVALID)
			r = SSH_ERR_KEY_WRONG_PASSPHRASE;
		goto out;
	}
	if ((r = sshbuf_consume(decoded, encrypted_len + authlen)) != 0)
		goto out;
	/* there should be no trailing data */
	if (sshbuf_len(decoded) != 0) {
		r = SSH_ERR_INVALID_FORMAT;
		goto out;
	}

	/* check check bytes */
	if ((r = sshbuf_get_u32(decrypted, &check1)) != 0 ||
	    (r = sshbuf_get_u32(decrypted, &check2)) != 0)
		goto out;
	if (check1 != check2) {
		r = SSH_ERR_KEY_WRONG_PASSPHRASE;
		goto out;
	}

	/* Load the private key and comment */
	if ((r = sshkey_private_deserialize(decrypted, &k)) != 0 ||
	    (r = sshbuf_get_cstring(decrypted, &comment, NULL)) != 0)
		goto out;

	/* Check deterministic padding */
	i = 0;
	while (sshbuf_len(decrypted)) {
		if ((r = sshbuf_get_u8(decrypted, &pad)) != 0)
			goto out;
		if (pad != (++i & 0xff)) {
			r = SSH_ERR_INVALID_FORMAT;
			goto out;
		}
	}

	/* XXX decode pubkey and check against private */

	/* success */
	r = 0;
	if (keyp != NULL) {
		*keyp = k;
		k = NULL;
	}
	if (commentp != NULL) {
		*commentp = comment;
		comment = NULL;
	}
 out:
	pad = 0;
	cipher_free(ciphercontext);
	free(ciphername);
	free(kdfname);
	free(comment);
	if (salt != NULL) {
		explicit_bzero(salt, slen);
		free(salt);
	}
	if (key != NULL) {
		explicit_bzero(key, keylen + ivlen);
		free(key);
	}
	sshbuf_free(encoded);
	sshbuf_free(decoded);
	sshbuf_free(kdf);
	sshbuf_free(decrypted);
	sshkey_free(k);
	return r;
}


#ifdef WITH_OPENSSL
/* convert SSH v2 key in OpenSSL PEM format */
static int
sshkey_private_pem_to_blob(struct sshkey *key, struct sshbuf *blob,
    const char *_passphrase, const char *comment)
{
	int success, r;
	int blen, len = strlen(_passphrase);
	u_char *passphrase = (len > 0) ? (u_char *)_passphrase : NULL;
	const EVP_CIPHER *cipher = (len > 0) ? EVP_aes_128_cbc() : NULL;
	char *bptr;
	BIO *bio = NULL;

	if (len > 0 && len <= 4)
		return SSH_ERR_PASSPHRASE_TOO_SHORT;
	if ((bio = BIO_new(BIO_s_mem())) == NULL)
		return SSH_ERR_ALLOC_FAIL;

	switch (key->type) {
	case KEY_DSA:
		success = PEM_write_bio_DSAPrivateKey(bio, key->dsa,
		    cipher, passphrase, len, NULL, NULL);
		break;
#ifdef OPENSSL_HAS_ECC
	case KEY_ECDSA:
		success = PEM_write_bio_ECPrivateKey(bio, key->ecdsa,
		    cipher, passphrase, len, NULL, NULL);
		break;
#endif
	case KEY_RSA:
		success = PEM_write_bio_RSAPrivateKey(bio, key->rsa,
		    cipher, passphrase, len, NULL, NULL);
		break;
	default:
		success = 0;
		break;
	}
	if (success == 0) {
		r = SSH_ERR_LIBCRYPTO_ERROR;
		goto out;
	}
	if ((blen = BIO_get_mem_data(bio, &bptr)) <= 0) {
		r = SSH_ERR_INTERNAL_ERROR;
		goto out;
	}
	if ((r = sshbuf_put(blob, bptr, blen)) != 0)
		goto out;
	r = 0;
 out:
	BIO_free(bio);
	return r;
}
#endif /* WITH_OPENSSL */

/* Serialise "key" to buffer "blob" */
int
sshkey_private_to_fileblob(struct sshkey *key, struct sshbuf *blob,
    const char *passphrase, const char *comment,
    int force_new_format, const char *new_format_cipher, int new_format_rounds)
{
	switch (key->type) {
#ifdef WITH_OPENSSL
	case KEY_DSA:
	case KEY_ECDSA:
	case KEY_RSA:
		if (force_new_format) {
			return sshkey_private_to_blob2(key, blob, passphrase,
			    comment, new_format_cipher, new_format_rounds);
		}
		return sshkey_private_pem_to_blob(key, blob,
		    passphrase, comment);
#endif /* WITH_OPENSSL */
	case KEY_ED25519:
#ifdef WITH_XMSS
	case KEY_XMSS:
#endif /* WITH_XMSS */
		return sshkey_private_to_blob2(key, blob, passphrase,
		    comment, new_format_cipher, new_format_rounds);
	default:
		return SSH_ERR_KEY_TYPE_UNKNOWN;
	}
}


#ifdef WITH_OPENSSL
static int
translate_libcrypto_error(unsigned long pem_err)
{
	int pem_reason = ERR_GET_REASON(pem_err);

	switch (ERR_GET_LIB(pem_err)) {
	case ERR_LIB_PEM:
		switch (pem_reason) {
		case PEM_R_BAD_PASSWORD_READ:
		case PEM_R_PROBLEMS_GETTING_PASSWORD:
		case PEM_R_BAD_DECRYPT:
			return SSH_ERR_KEY_WRONG_PASSPHRASE;
		default:
			return SSH_ERR_INVALID_FORMAT;
		}
	case ERR_LIB_EVP:
		switch (pem_reason) {
		case EVP_R_BAD_DECRYPT:
			return SSH_ERR_KEY_WRONG_PASSPHRASE;
#ifdef EVP_R_BN_DECODE_ERROR
		case EVP_R_BN_DECODE_ERROR:
#endif
		case EVP_R_DECODE_ERROR:
#ifdef EVP_R_PRIVATE_KEY_DECODE_ERROR
		case EVP_R_PRIVATE_KEY_DECODE_ERROR:
#endif
			return SSH_ERR_INVALID_FORMAT;
		default:
			return SSH_ERR_LIBCRYPTO_ERROR;
		}
	case ERR_LIB_ASN1:
		return SSH_ERR_INVALID_FORMAT;
	}
	return SSH_ERR_LIBCRYPTO_ERROR;
}

static void
clear_libcrypto_errors(void)
{
	while (ERR_get_error() != 0)
		;
}

/*
 * Translate OpenSSL error codes to determine whether
 * passphrase is required/incorrect.
 */
static int
convert_libcrypto_error(void)
{
	/*
	 * Some password errors are reported at the beginning
	 * of the error queue.
	 */
	if (translate_libcrypto_error(ERR_peek_error()) ==
	    SSH_ERR_KEY_WRONG_PASSPHRASE)
		return SSH_ERR_KEY_WRONG_PASSPHRASE;
	return translate_libcrypto_error(ERR_peek_last_error());
}

static int
sshkey_parse_private_pem_fileblob(struct sshbuf *blob, int type,
    const char *passphrase, struct sshkey **keyp)
{
	EVP_PKEY *pk = NULL;
	struct sshkey *prv = NULL;
	BIO *bio = NULL;
	int r;

	if (keyp != NULL)
		*keyp = NULL;

	if ((bio = BIO_new(BIO_s_mem())) == NULL || sshbuf_len(blob) > INT_MAX)
		return SSH_ERR_ALLOC_FAIL;
	if (BIO_write(bio, sshbuf_ptr(blob), sshbuf_len(blob)) !=
	    (int)sshbuf_len(blob)) {
		r = SSH_ERR_ALLOC_FAIL;
		goto out;
	}

	clear_libcrypto_errors();
	if ((pk = PEM_read_bio_PrivateKey(bio, NULL, NULL,
	    (char *)passphrase)) == NULL) {
		r = convert_libcrypto_error();
		goto out;
	}
	if (EVP_PKEY_base_id(pk) == EVP_PKEY_RSA &&
	    (type == KEY_UNSPEC || type == KEY_RSA)) {
		if ((prv = sshkey_new(KEY_UNSPEC)) == NULL) {
			r = SSH_ERR_ALLOC_FAIL;
			goto out;
		}
		prv->rsa = EVP_PKEY_get1_RSA(pk);
		prv->type = KEY_RSA;
#ifdef DEBUG_PK
		RSA_print_fp(stderr, prv->rsa, 8);
#endif
		if (RSA_blinding_on(prv->rsa, NULL) != 1) {
			r = SSH_ERR_LIBCRYPTO_ERROR;
			goto out;
		}
		if ((r = check_rsa_length(prv->rsa)) != 0)
			goto out;
	} else if (EVP_PKEY_base_id(pk) == EVP_PKEY_DSA &&
	    (type == KEY_UNSPEC || type == KEY_DSA)) {
		if ((prv = sshkey_new(KEY_UNSPEC)) == NULL) {
			r = SSH_ERR_ALLOC_FAIL;
			goto out;
		}
		prv->dsa = EVP_PKEY_get1_DSA(pk);
		prv->type = KEY_DSA;
#ifdef DEBUG_PK
		DSA_print_fp(stderr, prv->dsa, 8);
#endif
#ifdef OPENSSL_HAS_ECC
	} else if (EVP_PKEY_base_id(pk) == EVP_PKEY_EC &&
	    (type == KEY_UNSPEC || type == KEY_ECDSA)) {
		if ((prv = sshkey_new(KEY_UNSPEC)) == NULL) {
			r = SSH_ERR_ALLOC_FAIL;
			goto out;
		}
		prv->ecdsa = EVP_PKEY_get1_EC_KEY(pk);
		prv->type = KEY_ECDSA;
		prv->ecdsa_nid = sshkey_ecdsa_key_to_nid(prv->ecdsa);
		if (prv->ecdsa_nid == -1 ||
		    sshkey_curve_nid_to_name(prv->ecdsa_nid) == NULL ||
		    sshkey_ec_validate_public(EC_KEY_get0_group(prv->ecdsa),
		    EC_KEY_get0_public_key(prv->ecdsa)) != 0 ||
		    sshkey_ec_validate_private(prv->ecdsa) != 0) {
			r = SSH_ERR_INVALID_FORMAT;
			goto out;
		}
# ifdef DEBUG_PK
		if (prv != NULL && prv->ecdsa != NULL)
			sshkey_dump_ec_key(prv->ecdsa);
# endif
#endif /* OPENSSL_HAS_ECC */
	} else {
		r = SSH_ERR_INVALID_FORMAT;
		goto out;
	}
	r = 0;
	if (keyp != NULL) {
		*keyp = prv;
		prv = NULL;
	}
 out:
	BIO_free(bio);
	EVP_PKEY_free(pk);
	sshkey_free(prv);
	return r;
}
#endif /* WITH_OPENSSL */

int
sshkey_parse_private_fileblob_type(struct sshbuf *blob, int type,
    const char *passphrase, struct sshkey **keyp, char **commentp)
{
	int r = SSH_ERR_INTERNAL_ERROR;

	if (keyp != NULL)
		*keyp = NULL;
	if (commentp != NULL)
		*commentp = NULL;

	switch (type) {
#ifdef WITH_OPENSSL
	case KEY_DSA:
	case KEY_ECDSA:
	case KEY_RSA:
		return sshkey_parse_private_pem_fileblob(blob, type,
		    passphrase, keyp);
#endif /* WITH_OPENSSL */
	case KEY_ED25519:
#ifdef WITH_XMSS
	case KEY_XMSS:
#endif /* WITH_XMSS */
		return sshkey_parse_private2(blob, type, passphrase,
		    keyp, commentp);
	case KEY_UNSPEC:
		r = sshkey_parse_private2(blob, type, passphrase, keyp,
		    commentp);
		/* Do not fallback to PEM parser if only passphrase is wrong. */
		if (r == 0 || r == SSH_ERR_KEY_WRONG_PASSPHRASE)
			return r;
#ifdef WITH_OPENSSL
		return sshkey_parse_private_pem_fileblob(blob, type,
		    passphrase, keyp);
#else
		return SSH_ERR_INVALID_FORMAT;
#endif /* WITH_OPENSSL */
	default:
		return SSH_ERR_KEY_TYPE_UNKNOWN;
	}
}

int
sshkey_parse_private_fileblob(struct sshbuf *buffer, const char *passphrase,
    struct sshkey **keyp, char **commentp)
{
	if (keyp != NULL)
		*keyp = NULL;
	if (commentp != NULL)
		*commentp = NULL;

	return sshkey_parse_private_fileblob_type(buffer, KEY_UNSPEC,
	    passphrase, keyp, commentp);
}

#ifdef WITH_XMSS
/*
 * serialize the key with the current state and forward the state
 * maxsign times.
 */
int
sshkey_private_serialize_maxsign(const struct sshkey *k, struct sshbuf *b,
    u_int32_t maxsign, sshkey_printfn *pr)
{
	int r, rupdate;

	if (maxsign == 0 ||
	    sshkey_type_plain(k->type) != KEY_XMSS)
		return sshkey_private_serialize_opt(k, b,
		    SSHKEY_SERIALIZE_DEFAULT);
	if ((r = sshkey_xmss_get_state(k, pr)) != 0 ||
	    (r = sshkey_private_serialize_opt(k, b,
	    SSHKEY_SERIALIZE_STATE)) != 0 ||
	    (r = sshkey_xmss_forward_state(k, maxsign)) != 0)
		goto out;
	r = 0;
out:
	if ((rupdate = sshkey_xmss_update_state(k, pr)) != 0) {
		if (r == 0)
			r = rupdate;
	}
	return r;
}

u_int32_t
sshkey_signatures_left(const struct sshkey *k)
{
	if (sshkey_type_plain(k->type) == KEY_XMSS)
		return sshkey_xmss_signatures_left(k);
	return 0;
}

int
sshkey_enable_maxsign(struct sshkey *k, u_int32_t maxsign)
{
	if (sshkey_type_plain(k->type) != KEY_XMSS)
		return SSH_ERR_INVALID_ARGUMENT;
	return sshkey_xmss_enable_maxsign(k, maxsign);
}

int
sshkey_set_filename(struct sshkey *k, const char *filename)
{
	if (k == NULL)
		return SSH_ERR_INVALID_ARGUMENT;
	if (sshkey_type_plain(k->type) != KEY_XMSS)
		return 0;
	if (filename == NULL)
		return SSH_ERR_INVALID_ARGUMENT;
	if ((k->xmss_filename = strdup(filename)) == NULL)
		return SSH_ERR_ALLOC_FAIL;
	return 0;
}
#else
int
sshkey_private_serialize_maxsign(const struct sshkey *k, struct sshbuf *b,
    u_int32_t maxsign, sshkey_printfn *pr)
{
	return sshkey_private_serialize_opt(k, b, SSHKEY_SERIALIZE_DEFAULT);
}

u_int32_t
sshkey_signatures_left(const struct sshkey *k)
{
	return 0;
}

int
sshkey_enable_maxsign(struct sshkey *k, u_int32_t maxsign)
{
	return SSH_ERR_INVALID_ARGUMENT;
}

int
sshkey_set_filename(struct sshkey *k, const char *filename)
{
	if (k == NULL)
		return SSH_ERR_INVALID_ARGUMENT;
	return 0;
}
#endif /* WITH_XMSS */
