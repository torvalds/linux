/* $OpenBSD: ssh-pkcs11-client.c,v 1.10 2018/07/09 21:59:10 markus Exp $ */
/*
 * Copyright (c) 2010 Markus Friedl.  All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "includes.h"

#ifdef ENABLE_PKCS11

#include <sys/types.h>
#ifdef HAVE_SYS_TIME_H
# include <sys/time.h>
#endif
#include <sys/socket.h>

#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <openssl/rsa.h>

#include "openbsd-compat/openssl-compat.h"

#include "pathnames.h"
#include "xmalloc.h"
#include "sshbuf.h"
#include "log.h"
#include "misc.h"
#include "sshkey.h"
#include "authfd.h"
#include "atomicio.h"
#include "ssh-pkcs11.h"
#include "ssherr.h"

/* borrows code from sftp-server and ssh-agent */

int fd = -1;
pid_t pid = -1;

static void
send_msg(struct sshbuf *m)
{
	u_char buf[4];
	size_t mlen = sshbuf_len(m);
	int r;

	POKE_U32(buf, mlen);
	if (atomicio(vwrite, fd, buf, 4) != 4 ||
	    atomicio(vwrite, fd, sshbuf_mutable_ptr(m),
	    sshbuf_len(m)) != sshbuf_len(m))
		error("write to helper failed");
	if ((r = sshbuf_consume(m, mlen)) != 0)
		fatal("%s: buffer error: %s", __func__, ssh_err(r));
}

static int
recv_msg(struct sshbuf *m)
{
	u_int l, len;
	u_char c, buf[1024];
	int r;

	if ((len = atomicio(read, fd, buf, 4)) != 4) {
		error("read from helper failed: %u", len);
		return (0); /* XXX */
	}
	len = PEEK_U32(buf);
	if (len > 256 * 1024)
		fatal("response too long: %u", len);
	/* read len bytes into m */
	sshbuf_reset(m);
	while (len > 0) {
		l = len;
		if (l > sizeof(buf))
			l = sizeof(buf);
		if (atomicio(read, fd, buf, l) != l) {
			error("response from helper failed.");
			return (0); /* XXX */
		}
		if ((r = sshbuf_put(m, buf, l)) != 0)
			fatal("%s: buffer error: %s", __func__, ssh_err(r));
		len -= l;
	}
	if ((r = sshbuf_get_u8(m, &c)) != 0)
		fatal("%s: buffer error: %s", __func__, ssh_err(r));
	return c;
}

int
pkcs11_init(int interactive)
{
	return (0);
}

void
pkcs11_terminate(void)
{
	if (fd >= 0)
		close(fd);
}

static int
pkcs11_rsa_private_encrypt(int flen, const u_char *from, u_char *to, RSA *rsa,
    int padding)
{
	struct sshkey key;	/* XXX */
	u_char *blob, *signature = NULL;
	size_t blen, slen = 0;
	int r, ret = -1;
	struct sshbuf *msg;

	if (padding != RSA_PKCS1_PADDING)
		return (-1);
	key.type = KEY_RSA;
	key.rsa = rsa;
	if ((r = sshkey_to_blob(&key, &blob, &blen)) != 0) {
		error("%s: sshkey_to_blob: %s", __func__, ssh_err(r));
		return -1;
	}
	if ((msg = sshbuf_new()) == NULL)
		fatal("%s: sshbuf_new failed", __func__);
	if ((r = sshbuf_put_u8(msg, SSH2_AGENTC_SIGN_REQUEST)) != 0 ||
	    (r = sshbuf_put_string(msg, blob, blen)) != 0 ||
	    (r = sshbuf_put_string(msg, from, flen)) != 0 ||
	    (r = sshbuf_put_u32(msg, 0)) != 0)
		fatal("%s: buffer error: %s", __func__, ssh_err(r));
	free(blob);
	send_msg(msg);
	sshbuf_reset(msg);

	if (recv_msg(msg) == SSH2_AGENT_SIGN_RESPONSE) {
		if ((r = sshbuf_get_string(msg, &signature, &slen)) != 0)
			fatal("%s: buffer error: %s", __func__, ssh_err(r));
		if (slen <= (size_t)RSA_size(rsa)) {
			memcpy(to, signature, slen);
			ret = slen;
		}
		free(signature);
	}
	sshbuf_free(msg);
	return (ret);
}

/* redirect the private key encrypt operation to the ssh-pkcs11-helper */
static int
wrap_key(RSA *rsa)
{
	static RSA_METHOD *helper_rsa;

	if ((helper_rsa = RSA_meth_dup(RSA_get_default_method())) == NULL)
		fatal("%s: RSA_meth_dup failed", __func__);
	if (!RSA_meth_set1_name(helper_rsa, "ssh-pkcs11-helper") ||
	    !RSA_meth_set_priv_enc(helper_rsa, pkcs11_rsa_private_encrypt))
		fatal("%s: failed to prepare method", __func__);
	RSA_set_method(rsa, helper_rsa);
	return (0);
}

static int
pkcs11_start_helper(void)
{
	int pair[2];

	if (socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == -1) {
		error("socketpair: %s", strerror(errno));
		return (-1);
	}
	if ((pid = fork()) == -1) {
		error("fork: %s", strerror(errno));
		return (-1);
	} else if (pid == 0) {
		if ((dup2(pair[1], STDIN_FILENO) == -1) ||
		    (dup2(pair[1], STDOUT_FILENO) == -1)) {
			fprintf(stderr, "dup2: %s\n", strerror(errno));
			_exit(1);
		}
		close(pair[0]);
		close(pair[1]);
		execlp(_PATH_SSH_PKCS11_HELPER, _PATH_SSH_PKCS11_HELPER,
		    (char *)NULL);
		fprintf(stderr, "exec: %s: %s\n", _PATH_SSH_PKCS11_HELPER,
		    strerror(errno));
		_exit(1);
	}
	close(pair[1]);
	fd = pair[0];
	return (0);
}

int
pkcs11_add_provider(char *name, char *pin, struct sshkey ***keysp)
{
	struct sshkey *k;
	int r;
	u_char *blob;
	size_t blen;
	u_int nkeys, i;
	struct sshbuf *msg;

	if (fd < 0 && pkcs11_start_helper() < 0)
		return (-1);

	if ((msg = sshbuf_new()) == NULL)
		fatal("%s: sshbuf_new failed", __func__);
	if ((r = sshbuf_put_u8(msg, SSH_AGENTC_ADD_SMARTCARD_KEY)) != 0 ||
	    (r = sshbuf_put_cstring(msg, name)) != 0 ||
	    (r = sshbuf_put_cstring(msg, pin)) != 0)
		fatal("%s: buffer error: %s", __func__, ssh_err(r));
	send_msg(msg);
	sshbuf_reset(msg);

	if (recv_msg(msg) == SSH2_AGENT_IDENTITIES_ANSWER) {
		if ((r = sshbuf_get_u32(msg, &nkeys)) != 0)
			fatal("%s: buffer error: %s", __func__, ssh_err(r));
		*keysp = xcalloc(nkeys, sizeof(struct sshkey *));
		for (i = 0; i < nkeys; i++) {
			/* XXX clean up properly instead of fatal() */
			if ((r = sshbuf_get_string(msg, &blob, &blen)) != 0 ||
			    (r = sshbuf_skip_string(msg)) != 0)
				fatal("%s: buffer error: %s",
				    __func__, ssh_err(r));
			if ((r = sshkey_from_blob(blob, blen, &k)) != 0)
				fatal("%s: bad key: %s", __func__, ssh_err(r));
			wrap_key(k->rsa);
			(*keysp)[i] = k;
			free(blob);
		}
	} else {
		nkeys = -1;
	}
	sshbuf_free(msg);
	return (nkeys);
}

int
pkcs11_del_provider(char *name)
{
	int r, ret = -1;
	struct sshbuf *msg;

	if ((msg = sshbuf_new()) == NULL)
		fatal("%s: sshbuf_new failed", __func__);
	if ((r = sshbuf_put_u8(msg, SSH_AGENTC_REMOVE_SMARTCARD_KEY)) != 0 ||
	    (r = sshbuf_put_cstring(msg, name)) != 0 ||
	    (r = sshbuf_put_cstring(msg, "")) != 0)
		fatal("%s: buffer error: %s", __func__, ssh_err(r));
	send_msg(msg);
	sshbuf_reset(msg);

	if (recv_msg(msg) == SSH_AGENT_SUCCESS)
		ret = 0;
	sshbuf_free(msg);
	return (ret);
}

#endif /* ENABLE_PKCS11 */
