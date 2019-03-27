/* $OpenBSD: kexc25519.c,v 1.10 2016/05/02 08:49:03 djm Exp $ */
/*
 * Copyright (c) 2001, 2013 Markus Friedl.  All rights reserved.
 * Copyright (c) 2010 Damien Miller.  All rights reserved.
 * Copyright (c) 2013 Aris Adamantiadis.  All rights reserved.
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

#include <signal.h>
#include <string.h>

#include <openssl/bn.h>
#include <openssl/evp.h>

#include "sshbuf.h"
#include "ssh2.h"
#include "sshkey.h"
#include "cipher.h"
#include "kex.h"
#include "log.h"
#include "digest.h"
#include "ssherr.h"

extern int crypto_scalarmult_curve25519(u_char a[CURVE25519_SIZE],
    const u_char b[CURVE25519_SIZE], const u_char c[CURVE25519_SIZE])
	__attribute__((__bounded__(__minbytes__, 1, CURVE25519_SIZE)))
	__attribute__((__bounded__(__minbytes__, 2, CURVE25519_SIZE)))
	__attribute__((__bounded__(__minbytes__, 3, CURVE25519_SIZE)));

void
kexc25519_keygen(u_char key[CURVE25519_SIZE], u_char pub[CURVE25519_SIZE])
{
	static const u_char basepoint[CURVE25519_SIZE] = {9};

	arc4random_buf(key, CURVE25519_SIZE);
	crypto_scalarmult_curve25519(pub, key, basepoint);
}

int
kexc25519_shared_key(const u_char key[CURVE25519_SIZE],
    const u_char pub[CURVE25519_SIZE], struct sshbuf *out)
{
	u_char shared_key[CURVE25519_SIZE];
	int r;

	/* Check for all-zero public key */
	explicit_bzero(shared_key, CURVE25519_SIZE);
	if (timingsafe_bcmp(pub, shared_key, CURVE25519_SIZE) == 0)
		return SSH_ERR_KEY_INVALID_EC_VALUE;

	crypto_scalarmult_curve25519(shared_key, key, pub);
#ifdef DEBUG_KEXECDH
	dump_digest("shared secret", shared_key, CURVE25519_SIZE);
#endif
	sshbuf_reset(out);
	r = sshbuf_put_bignum2_bytes(out, shared_key, CURVE25519_SIZE);
	explicit_bzero(shared_key, CURVE25519_SIZE);
	return r;
}

int
kex_c25519_hash(
    int hash_alg,
    const char *client_version_string,
    const char *server_version_string,
    const u_char *ckexinit, size_t ckexinitlen,
    const u_char *skexinit, size_t skexinitlen,
    const u_char *serverhostkeyblob, size_t sbloblen,
    const u_char client_dh_pub[CURVE25519_SIZE],
    const u_char server_dh_pub[CURVE25519_SIZE],
    const u_char *shared_secret, size_t secretlen,
    u_char *hash, size_t *hashlen)
{
	struct sshbuf *b;
	int r;

	if (*hashlen < ssh_digest_bytes(hash_alg))
		return SSH_ERR_INVALID_ARGUMENT;
	if ((b = sshbuf_new()) == NULL)
		return SSH_ERR_ALLOC_FAIL;
	if ((r = sshbuf_put_cstring(b, client_version_string)) < 0 ||
	    (r = sshbuf_put_cstring(b, server_version_string)) < 0 ||
	    /* kexinit messages: fake header: len+SSH2_MSG_KEXINIT */
	    (r = sshbuf_put_u32(b, ckexinitlen+1)) < 0 ||
	    (r = sshbuf_put_u8(b, SSH2_MSG_KEXINIT)) < 0 ||
	    (r = sshbuf_put(b, ckexinit, ckexinitlen)) < 0 ||
	    (r = sshbuf_put_u32(b, skexinitlen+1)) < 0 ||
	    (r = sshbuf_put_u8(b, SSH2_MSG_KEXINIT)) < 0 ||
	    (r = sshbuf_put(b, skexinit, skexinitlen)) < 0 ||
	    (r = sshbuf_put_string(b, serverhostkeyblob, sbloblen)) < 0 ||
	    (r = sshbuf_put_string(b, client_dh_pub, CURVE25519_SIZE)) < 0 ||
	    (r = sshbuf_put_string(b, server_dh_pub, CURVE25519_SIZE)) < 0 ||
	    (r = sshbuf_put(b, shared_secret, secretlen)) < 0) {
		sshbuf_free(b);
		return r;
	}
#ifdef DEBUG_KEX
	sshbuf_dump(b, stderr);
#endif
	if (ssh_digest_buffer(hash_alg, b, hash, *hashlen) != 0) {
		sshbuf_free(b);
		return SSH_ERR_LIBCRYPTO_ERROR;
	}
	sshbuf_free(b);
	*hashlen = ssh_digest_bytes(hash_alg);
#ifdef DEBUG_KEX
	dump_digest("hash", hash, *hashlen);
#endif
	return 0;
}
