/* 	$OpenBSD: test_sshkey.c,v 1.17 2018/09/13 09:03:20 djm Exp $ */
/*
 * Regress test for sshkey.h key management API
 *
 * Placed in the public domain
 */

#include "includes.h"

#include <sys/types.h>
#include <sys/param.h>
#include <stdio.h>
#ifdef HAVE_STDINT_H
#include <stdint.h>
#endif
#include <stdlib.h>
#include <string.h>

#include <openssl/bn.h>
#include <openssl/rsa.h>
#include <openssl/dsa.h>
#if defined(OPENSSL_HAS_ECC) && defined(OPENSSL_HAS_NISTP256)
# include <openssl/ec.h>
#endif

#include "../test_helper/test_helper.h"

#include "ssherr.h"
#include "sshbuf.h"
#define SSHBUF_INTERNAL 1	/* access internals for testing */
#include "sshkey.h"

#include "authfile.h"
#include "common.h"
#include "ssh2.h"

void sshkey_tests(void);

static void
put_opt(struct sshbuf *b, const char *name, const char *value)
{
	struct sshbuf *sect;

	sect = sshbuf_new();
	ASSERT_PTR_NE(sect, NULL);
	ASSERT_INT_EQ(sshbuf_put_cstring(b, name), 0);
	if (value != NULL)
		ASSERT_INT_EQ(sshbuf_put_cstring(sect, value), 0);
	ASSERT_INT_EQ(sshbuf_put_stringb(b, sect), 0);
	sshbuf_free(sect);
}

static void
build_cert(struct sshbuf *b, const struct sshkey *k, const char *type,
    const struct sshkey *sign_key, const struct sshkey *ca_key,
    const char *sig_alg)
{
	struct sshbuf *ca_buf, *pk, *principals, *critopts, *exts;
	u_char *sigblob;
	size_t siglen;

	ca_buf = sshbuf_new();
	ASSERT_PTR_NE(ca_buf, NULL);
	ASSERT_INT_EQ(sshkey_putb(ca_key, ca_buf), 0);

	/*
	 * Get the public key serialisation by rendering the key and skipping
	 * the type string. This is a bit of a hack :/
	 */
	pk = sshbuf_new();
	ASSERT_PTR_NE(pk, NULL);
	ASSERT_INT_EQ(sshkey_putb_plain(k, pk), 0);
	ASSERT_INT_EQ(sshbuf_skip_string(pk), 0);

	principals = sshbuf_new();
	ASSERT_PTR_NE(principals, NULL);
	ASSERT_INT_EQ(sshbuf_put_cstring(principals, "gsamsa"), 0);
	ASSERT_INT_EQ(sshbuf_put_cstring(principals, "gregor"), 0);

	critopts = sshbuf_new();
	ASSERT_PTR_NE(critopts, NULL);
	put_opt(critopts, "force-command", "/usr/local/bin/nethack");
	put_opt(critopts, "source-address", "192.168.0.0/24,127.0.0.1,::1");

	exts = sshbuf_new();
	ASSERT_PTR_NE(exts, NULL);
	put_opt(critopts, "permit-X11-forwarding", NULL);

	ASSERT_INT_EQ(sshbuf_put_cstring(b, type), 0);
	ASSERT_INT_EQ(sshbuf_put_cstring(b, "noncenoncenonce!"), 0); /* nonce */
	ASSERT_INT_EQ(sshbuf_putb(b, pk), 0); /* public key serialisation */
	ASSERT_INT_EQ(sshbuf_put_u64(b, 1234), 0); /* serial */
	ASSERT_INT_EQ(sshbuf_put_u32(b, SSH2_CERT_TYPE_USER), 0); /* type */
	ASSERT_INT_EQ(sshbuf_put_cstring(b, "gregor"), 0); /* key ID */
	ASSERT_INT_EQ(sshbuf_put_stringb(b, principals), 0); /* principals */
	ASSERT_INT_EQ(sshbuf_put_u64(b, 0), 0); /* start */
	ASSERT_INT_EQ(sshbuf_put_u64(b, 0xffffffffffffffffULL), 0); /* end */
	ASSERT_INT_EQ(sshbuf_put_stringb(b, critopts), 0); /* options */
	ASSERT_INT_EQ(sshbuf_put_stringb(b, exts), 0); /* extensions */
	ASSERT_INT_EQ(sshbuf_put_string(b, NULL, 0), 0); /* reserved */
	ASSERT_INT_EQ(sshbuf_put_stringb(b, ca_buf), 0); /* signature key */
	ASSERT_INT_EQ(sshkey_sign(sign_key, &sigblob, &siglen,
	    sshbuf_ptr(b), sshbuf_len(b), sig_alg, 0), 0);
	ASSERT_INT_EQ(sshbuf_put_string(b, sigblob, siglen), 0); /* signature */

	free(sigblob);
	sshbuf_free(ca_buf);
	sshbuf_free(exts);
	sshbuf_free(critopts);
	sshbuf_free(principals);
	sshbuf_free(pk);
}

static void
signature_test(struct sshkey *k, struct sshkey *bad, const char *sig_alg,
    const u_char *d, size_t l)
{
	size_t len;
	u_char *sig;

	ASSERT_INT_EQ(sshkey_sign(k, &sig, &len, d, l, sig_alg, 0), 0);
	ASSERT_SIZE_T_GT(len, 8);
	ASSERT_PTR_NE(sig, NULL);
	ASSERT_INT_EQ(sshkey_verify(k, sig, len, d, l, NULL, 0), 0);
	ASSERT_INT_NE(sshkey_verify(bad, sig, len, d, l, NULL, 0), 0);
	/* Fuzz test is more comprehensive, this is just a smoke test */
	sig[len - 5] ^= 0x10;
	ASSERT_INT_NE(sshkey_verify(k, sig, len, d, l, NULL, 0), 0);
	free(sig);
}

static void
banana(u_char *s, size_t l)
{
	size_t o;
	const u_char the_banana[] = { 'b', 'a', 'n', 'a', 'n', 'a' };

	for (o = 0; o < l; o += sizeof(the_banana)) {
		if (l - o < sizeof(the_banana)) {
			memcpy(s + o, "nanananana", l - o);
			break;
		}
		memcpy(s + o, banana, sizeof(the_banana));
	}
}

static void
signature_tests(struct sshkey *k, struct sshkey *bad, const char *sig_alg)
{
	u_char i, buf[2049];
	size_t lens[] = {
		1, 2, 7, 8, 9, 15, 16, 17, 31, 32, 33, 127, 128, 129,
		255, 256, 257, 1023, 1024, 1025, 2047, 2048, 2049
	};

	for (i = 0; i < (sizeof(lens)/sizeof(lens[0])); i++) {
		test_subtest_info("%s key, banana length %zu",
		    sshkey_type(k), lens[i]);
		banana(buf, lens[i]);
		signature_test(k, bad, sig_alg, buf, lens[i]);
	}
}

static struct sshkey *
get_private(const char *n)
{
	struct sshbuf *b;
	struct sshkey *ret;

	b = load_file(n);
	ASSERT_INT_EQ(sshkey_parse_private_fileblob(b, "", &ret, NULL), 0);
	sshbuf_free(b);
	return ret;
}

void
sshkey_tests(void)
{
	struct sshkey *k1, *k2, *k3, *k4, *kr, *kd, *kf;
#ifdef OPENSSL_HAS_ECC
	struct sshkey *ke;
#endif
	struct sshbuf *b;

	TEST_START("new invalid");
	k1 = sshkey_new(-42);
	ASSERT_PTR_EQ(k1, NULL);
	TEST_DONE();

	TEST_START("new/free KEY_UNSPEC");
	k1 = sshkey_new(KEY_UNSPEC);
	ASSERT_PTR_NE(k1, NULL);
	sshkey_free(k1);
	TEST_DONE();

	TEST_START("new/free KEY_RSA");
	k1 = sshkey_new(KEY_RSA);
	ASSERT_PTR_NE(k1, NULL);
	ASSERT_PTR_NE(k1->rsa, NULL);
	sshkey_free(k1);
	TEST_DONE();

	TEST_START("new/free KEY_DSA");
	k1 = sshkey_new(KEY_DSA);
	ASSERT_PTR_NE(k1, NULL);
	ASSERT_PTR_NE(k1->dsa, NULL);
	sshkey_free(k1);
	TEST_DONE();

#ifdef OPENSSL_HAS_ECC
	TEST_START("new/free KEY_ECDSA");
	k1 = sshkey_new(KEY_ECDSA);
	ASSERT_PTR_NE(k1, NULL);
	ASSERT_PTR_EQ(k1->ecdsa, NULL);  /* Can't allocate without NID */
	sshkey_free(k1);
	TEST_DONE();
#endif

	TEST_START("new/free KEY_ED25519");
	k1 = sshkey_new(KEY_ED25519);
	ASSERT_PTR_NE(k1, NULL);
	/* These should be blank until key loaded or generated */
	ASSERT_PTR_EQ(k1->ed25519_sk, NULL);
	ASSERT_PTR_EQ(k1->ed25519_pk, NULL);
	sshkey_free(k1);
	TEST_DONE();

	TEST_START("generate KEY_RSA too small modulus");
	ASSERT_INT_EQ(sshkey_generate(KEY_RSA, 128, &k1),
	    SSH_ERR_KEY_LENGTH);
	ASSERT_PTR_EQ(k1, NULL);
	TEST_DONE();

	TEST_START("generate KEY_RSA too large modulus");
	ASSERT_INT_EQ(sshkey_generate(KEY_RSA, 1 << 20, &k1),
	    SSH_ERR_KEY_LENGTH);
	ASSERT_PTR_EQ(k1, NULL);
	TEST_DONE();

	TEST_START("generate KEY_DSA wrong bits");
	ASSERT_INT_EQ(sshkey_generate(KEY_DSA, 2048, &k1),
	    SSH_ERR_KEY_LENGTH);
	ASSERT_PTR_EQ(k1, NULL);
	sshkey_free(k1);
	TEST_DONE();

#ifdef OPENSSL_HAS_ECC
	TEST_START("generate KEY_ECDSA wrong bits");
	ASSERT_INT_EQ(sshkey_generate(KEY_ECDSA, 42, &k1),
	    SSH_ERR_KEY_LENGTH);
	ASSERT_PTR_EQ(k1, NULL);
	sshkey_free(k1);
	TEST_DONE();
#endif

	TEST_START("generate KEY_RSA");
	ASSERT_INT_EQ(sshkey_generate(KEY_RSA, 767, &kr),
	    SSH_ERR_KEY_LENGTH);
	ASSERT_INT_EQ(sshkey_generate(KEY_RSA, 1024, &kr), 0);
	ASSERT_PTR_NE(kr, NULL);
	ASSERT_PTR_NE(kr->rsa, NULL);
	ASSERT_PTR_NE(rsa_n(kr), NULL);
	ASSERT_PTR_NE(rsa_e(kr), NULL);
	ASSERT_PTR_NE(rsa_p(kr), NULL);
	ASSERT_INT_EQ(BN_num_bits(rsa_n(kr)), 1024);
	TEST_DONE();

	TEST_START("generate KEY_DSA");
	ASSERT_INT_EQ(sshkey_generate(KEY_DSA, 1024, &kd), 0);
	ASSERT_PTR_NE(kd, NULL);
	ASSERT_PTR_NE(kd->dsa, NULL);
	ASSERT_PTR_NE(dsa_g(kd), NULL);
	ASSERT_PTR_NE(dsa_priv_key(kd), NULL);
	TEST_DONE();

#ifdef OPENSSL_HAS_ECC
	TEST_START("generate KEY_ECDSA");
	ASSERT_INT_EQ(sshkey_generate(KEY_ECDSA, 256, &ke), 0);
	ASSERT_PTR_NE(ke, NULL);
	ASSERT_PTR_NE(ke->ecdsa, NULL);
	ASSERT_PTR_NE(EC_KEY_get0_public_key(ke->ecdsa), NULL);
	ASSERT_PTR_NE(EC_KEY_get0_private_key(ke->ecdsa), NULL);
	TEST_DONE();
#endif

	TEST_START("generate KEY_ED25519");
	ASSERT_INT_EQ(sshkey_generate(KEY_ED25519, 256, &kf), 0);
	ASSERT_PTR_NE(kf, NULL);
	ASSERT_INT_EQ(kf->type, KEY_ED25519);
	ASSERT_PTR_NE(kf->ed25519_pk, NULL);
	ASSERT_PTR_NE(kf->ed25519_sk, NULL);
	TEST_DONE();

	TEST_START("demote KEY_RSA");
	ASSERT_INT_EQ(sshkey_demote(kr, &k1), 0);
	ASSERT_PTR_NE(k1, NULL);
	ASSERT_PTR_NE(kr, k1);
	ASSERT_INT_EQ(k1->type, KEY_RSA);
	ASSERT_PTR_NE(k1->rsa, NULL);
	ASSERT_PTR_NE(rsa_n(k1), NULL);
	ASSERT_PTR_NE(rsa_e(k1), NULL);
	ASSERT_PTR_EQ(rsa_p(k1), NULL);
	TEST_DONE();

	TEST_START("equal KEY_RSA/demoted KEY_RSA");
	ASSERT_INT_EQ(sshkey_equal(kr, k1), 1);
	sshkey_free(k1);
	TEST_DONE();

	TEST_START("demote KEY_DSA");
	ASSERT_INT_EQ(sshkey_demote(kd, &k1), 0);
	ASSERT_PTR_NE(k1, NULL);
	ASSERT_PTR_NE(kd, k1);
	ASSERT_INT_EQ(k1->type, KEY_DSA);
	ASSERT_PTR_NE(k1->dsa, NULL);
	ASSERT_PTR_NE(dsa_g(k1), NULL);
	ASSERT_PTR_EQ(dsa_priv_key(k1), NULL);
	TEST_DONE();

	TEST_START("equal KEY_DSA/demoted KEY_DSA");
	ASSERT_INT_EQ(sshkey_equal(kd, k1), 1);
	sshkey_free(k1);
	TEST_DONE();

#ifdef OPENSSL_HAS_ECC
	TEST_START("demote KEY_ECDSA");
	ASSERT_INT_EQ(sshkey_demote(ke, &k1), 0);
	ASSERT_PTR_NE(k1, NULL);
	ASSERT_PTR_NE(ke, k1);
	ASSERT_INT_EQ(k1->type, KEY_ECDSA);
	ASSERT_PTR_NE(k1->ecdsa, NULL);
	ASSERT_INT_EQ(k1->ecdsa_nid, ke->ecdsa_nid);
	ASSERT_PTR_NE(EC_KEY_get0_public_key(ke->ecdsa), NULL);
	ASSERT_PTR_EQ(EC_KEY_get0_private_key(k1->ecdsa), NULL);
	TEST_DONE();

	TEST_START("equal KEY_ECDSA/demoted KEY_ECDSA");
	ASSERT_INT_EQ(sshkey_equal(ke, k1), 1);
	sshkey_free(k1);
	TEST_DONE();
#endif

	TEST_START("demote KEY_ED25519");
	ASSERT_INT_EQ(sshkey_demote(kf, &k1), 0);
	ASSERT_PTR_NE(k1, NULL);
	ASSERT_PTR_NE(kf, k1);
	ASSERT_INT_EQ(k1->type, KEY_ED25519);
	ASSERT_PTR_NE(k1->ed25519_pk, NULL);
	ASSERT_PTR_EQ(k1->ed25519_sk, NULL);
	TEST_DONE();

	TEST_START("equal KEY_ED25519/demoted KEY_ED25519");
	ASSERT_INT_EQ(sshkey_equal(kf, k1), 1);
	sshkey_free(k1);
	TEST_DONE();

	TEST_START("equal mismatched key types");
	ASSERT_INT_EQ(sshkey_equal(kd, kr), 0);
#ifdef OPENSSL_HAS_ECC
	ASSERT_INT_EQ(sshkey_equal(kd, ke), 0);
	ASSERT_INT_EQ(sshkey_equal(kr, ke), 0);
	ASSERT_INT_EQ(sshkey_equal(ke, kf), 0);
#endif
	ASSERT_INT_EQ(sshkey_equal(kd, kf), 0);
	TEST_DONE();

	TEST_START("equal different keys");
	ASSERT_INT_EQ(sshkey_generate(KEY_RSA, 1024, &k1), 0);
	ASSERT_INT_EQ(sshkey_equal(kr, k1), 0);
	sshkey_free(k1);
	ASSERT_INT_EQ(sshkey_generate(KEY_DSA, 1024, &k1), 0);
	ASSERT_INT_EQ(sshkey_equal(kd, k1), 0);
	sshkey_free(k1);
#ifdef OPENSSL_HAS_ECC
	ASSERT_INT_EQ(sshkey_generate(KEY_ECDSA, 256, &k1), 0);
	ASSERT_INT_EQ(sshkey_equal(ke, k1), 0);
	sshkey_free(k1);
#endif
	ASSERT_INT_EQ(sshkey_generate(KEY_ED25519, 256, &k1), 0);
	ASSERT_INT_EQ(sshkey_equal(kf, k1), 0);
	sshkey_free(k1);
	TEST_DONE();

	sshkey_free(kr);
	sshkey_free(kd);
#ifdef OPENSSL_HAS_ECC
	sshkey_free(ke);
#endif
	sshkey_free(kf);

	TEST_START("certify key");
	ASSERT_INT_EQ(sshkey_load_public(test_data_file("ed25519_1.pub"),
	    &k1, NULL), 0);
	k2 = get_private("ed25519_2");
	ASSERT_INT_EQ(sshkey_to_certified(k1), 0);
	ASSERT_PTR_NE(k1->cert, NULL);
	k1->cert->type = SSH2_CERT_TYPE_USER;
	k1->cert->serial = 1234;
	k1->cert->key_id = strdup("estragon");
	ASSERT_PTR_NE(k1->cert->key_id, NULL);
	k1->cert->principals = calloc(4, sizeof(*k1->cert->principals));
	ASSERT_PTR_NE(k1->cert->principals, NULL);
	k1->cert->principals[0] = strdup("estragon");
	k1->cert->principals[1] = strdup("vladimir");
	k1->cert->principals[2] = strdup("pozzo");
	k1->cert->principals[3] = strdup("lucky");
	ASSERT_PTR_NE(k1->cert->principals[0], NULL);
	ASSERT_PTR_NE(k1->cert->principals[1], NULL);
	ASSERT_PTR_NE(k1->cert->principals[2], NULL);
	ASSERT_PTR_NE(k1->cert->principals[3], NULL);
	k1->cert->nprincipals = 4;
	k1->cert->valid_after = 0;
	k1->cert->valid_before = (u_int64_t)-1;
	sshbuf_free(k1->cert->critical);
	k1->cert->critical = sshbuf_new();
	ASSERT_PTR_NE(k1->cert->critical, NULL);
	sshbuf_free(k1->cert->extensions);
	k1->cert->extensions = sshbuf_new();
	ASSERT_PTR_NE(k1->cert->extensions, NULL);
	put_opt(k1->cert->critical, "force-command", "/usr/bin/true");
	put_opt(k1->cert->critical, "source-address", "127.0.0.1");
	put_opt(k1->cert->extensions, "permit-X11-forwarding", NULL);
	put_opt(k1->cert->extensions, "permit-agent-forwarding", NULL);
	ASSERT_INT_EQ(sshkey_from_private(k2, &k1->cert->signature_key), 0);
	ASSERT_INT_EQ(sshkey_certify(k1, k2, NULL), 0);
	b = sshbuf_new();
	ASSERT_PTR_NE(b, NULL);
	ASSERT_INT_EQ(sshkey_putb(k1, b), 0);
	ASSERT_INT_EQ(sshkey_from_blob(sshbuf_ptr(b), sshbuf_len(b), &k3), 0);

	sshkey_free(k1);
	sshkey_free(k2);
	sshkey_free(k3);
	sshbuf_reset(b);
	TEST_DONE();

	TEST_START("sign and verify RSA");
	k1 = get_private("rsa_1");
	ASSERT_INT_EQ(sshkey_load_public(test_data_file("rsa_2.pub"), &k2,
	    NULL), 0);
	signature_tests(k1, k2, "ssh-rsa");
	sshkey_free(k1);
	sshkey_free(k2);
	TEST_DONE();

	TEST_START("sign and verify RSA-SHA256");
	k1 = get_private("rsa_1");
	ASSERT_INT_EQ(sshkey_load_public(test_data_file("rsa_2.pub"), &k2,
	    NULL), 0);
	signature_tests(k1, k2, "rsa-sha2-256");
	sshkey_free(k1);
	sshkey_free(k2);
	TEST_DONE();

	TEST_START("sign and verify RSA-SHA512");
	k1 = get_private("rsa_1");
	ASSERT_INT_EQ(sshkey_load_public(test_data_file("rsa_2.pub"), &k2,
	    NULL), 0);
	signature_tests(k1, k2, "rsa-sha2-512");
	sshkey_free(k1);
	sshkey_free(k2);
	TEST_DONE();

	TEST_START("sign and verify DSA");
	k1 = get_private("dsa_1");
	ASSERT_INT_EQ(sshkey_load_public(test_data_file("dsa_2.pub"), &k2,
	    NULL), 0);
	signature_tests(k1, k2, NULL);
	sshkey_free(k1);
	sshkey_free(k2);
	TEST_DONE();

#ifdef OPENSSL_HAS_ECC
	TEST_START("sign and verify ECDSA");
	k1 = get_private("ecdsa_1");
	ASSERT_INT_EQ(sshkey_load_public(test_data_file("ecdsa_2.pub"), &k2,
	    NULL), 0);
	signature_tests(k1, k2, NULL);
	sshkey_free(k1);
	sshkey_free(k2);
	TEST_DONE();
#endif

	TEST_START("sign and verify ED25519");
	k1 = get_private("ed25519_1");
	ASSERT_INT_EQ(sshkey_load_public(test_data_file("ed25519_2.pub"), &k2,
	    NULL), 0);
	signature_tests(k1, k2, NULL);
	sshkey_free(k1);
	sshkey_free(k2);
	TEST_DONE();

	TEST_START("nested certificate");
	ASSERT_INT_EQ(sshkey_load_cert(test_data_file("rsa_1"), &k1), 0);
	ASSERT_INT_EQ(sshkey_load_public(test_data_file("rsa_1.pub"), &k2,
	    NULL), 0);
	k3 = get_private("rsa_1");
	build_cert(b, k2, "ssh-rsa-cert-v01@openssh.com", k3, k1, NULL);
	ASSERT_INT_EQ(sshkey_from_blob(sshbuf_ptr(b), sshbuf_len(b), &k4),
	    SSH_ERR_KEY_CERT_INVALID_SIGN_KEY);
	ASSERT_PTR_EQ(k4, NULL);
	sshkey_free(k1);
	sshkey_free(k2);
	sshkey_free(k3);
	sshbuf_free(b);
	TEST_DONE();

}
