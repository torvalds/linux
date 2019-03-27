/* 	$OpenBSD: common.c,v 1.3 2018/09/13 09:03:20 djm Exp $ */
/*
 * Helpers for key API tests
 *
 * Placed in the public domain
 */

#include "includes.h"

#include <sys/types.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#ifdef HAVE_STDINT_H
#include <stdint.h>
#endif
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <openssl/bn.h>
#include <openssl/rsa.h>
#include <openssl/dsa.h>
#include <openssl/objects.h>
#ifdef OPENSSL_HAS_NISTP256
# include <openssl/ec.h>
#endif

#include "../test_helper/test_helper.h"

#include "ssherr.h"
#include "authfile.h"
#include "sshkey.h"
#include "sshbuf.h"

#include "common.h"

struct sshbuf *
load_file(const char *name)
{
	int fd;
	struct sshbuf *ret;

	ASSERT_PTR_NE(ret = sshbuf_new(), NULL);
	ASSERT_INT_NE(fd = open(test_data_file(name), O_RDONLY), -1);
	ASSERT_INT_EQ(sshkey_load_file(fd, ret), 0);
	close(fd);
	return ret;
}

struct sshbuf *
load_text_file(const char *name)
{
	struct sshbuf *ret = load_file(name);
	const u_char *p;

	/* Trim whitespace at EOL */
	for (p = sshbuf_ptr(ret); sshbuf_len(ret) > 0;) {
		if (p[sshbuf_len(ret) - 1] == '\r' ||
		    p[sshbuf_len(ret) - 1] == '\t' ||
		    p[sshbuf_len(ret) - 1] == ' ' ||
		    p[sshbuf_len(ret) - 1] == '\n')
			ASSERT_INT_EQ(sshbuf_consume_end(ret, 1), 0);
		else
			break;
	}
	/* \0 terminate */
	ASSERT_INT_EQ(sshbuf_put_u8(ret, 0), 0);
	return ret;
}

BIGNUM *
load_bignum(const char *name)
{
	BIGNUM *ret = NULL;
	struct sshbuf *buf;

	buf = load_text_file(name);
	ASSERT_INT_NE(BN_hex2bn(&ret, (const char *)sshbuf_ptr(buf)), 0);
	sshbuf_free(buf);
	return ret;
}

const BIGNUM *
rsa_n(struct sshkey *k)
{
	const BIGNUM *n = NULL;

	ASSERT_PTR_NE(k, NULL);
	ASSERT_PTR_NE(k->rsa, NULL);
	RSA_get0_key(k->rsa, &n, NULL, NULL);
	return n;
}

const BIGNUM *
rsa_e(struct sshkey *k)
{
	const BIGNUM *e = NULL;

	ASSERT_PTR_NE(k, NULL);
	ASSERT_PTR_NE(k->rsa, NULL);
	RSA_get0_key(k->rsa, NULL, &e, NULL);
	return e;
}

const BIGNUM *
rsa_p(struct sshkey *k)
{
	const BIGNUM *p = NULL;

	ASSERT_PTR_NE(k, NULL);
	ASSERT_PTR_NE(k->rsa, NULL);
	RSA_get0_factors(k->rsa, &p, NULL);
	return p;
}

const BIGNUM *
rsa_q(struct sshkey *k)
{
	const BIGNUM *q = NULL;

	ASSERT_PTR_NE(k, NULL);
	ASSERT_PTR_NE(k->rsa, NULL);
	RSA_get0_factors(k->rsa, NULL, &q);
	return q;
}

const BIGNUM *
dsa_g(struct sshkey *k)
{
	const BIGNUM *g = NULL;

	ASSERT_PTR_NE(k, NULL);
	ASSERT_PTR_NE(k->dsa, NULL);
	DSA_get0_pqg(k->dsa, NULL, NULL, &g);
	return g;
}

const BIGNUM *
dsa_pub_key(struct sshkey *k)
{
	const BIGNUM *pub_key = NULL;

	ASSERT_PTR_NE(k, NULL);
	ASSERT_PTR_NE(k->dsa, NULL);
	DSA_get0_key(k->dsa, &pub_key, NULL);
	return pub_key;
}

const BIGNUM *
dsa_priv_key(struct sshkey *k)
{
	const BIGNUM *priv_key = NULL;

	ASSERT_PTR_NE(k, NULL);
	ASSERT_PTR_NE(k->dsa, NULL);
	DSA_get0_key(k->dsa, NULL, &priv_key);
	return priv_key;
}

