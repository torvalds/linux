/*
 * Copyright (c) 2012 Damien Miller <djm@mindrot.org>
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

/* $OpenBSD: krl.h,v 1.5 2015/12/30 23:46:14 djm Exp $ */

#ifndef _KRL_H
#define _KRL_H

/* Functions to manage key revocation lists */

#define KRL_MAGIC		"SSHKRL\n\0"
#define KRL_FORMAT_VERSION	1

/* KRL section types */
#define KRL_SECTION_CERTIFICATES	1
#define KRL_SECTION_EXPLICIT_KEY	2
#define KRL_SECTION_FINGERPRINT_SHA1	3
#define KRL_SECTION_SIGNATURE		4

/* KRL_SECTION_CERTIFICATES subsection types */
#define KRL_SECTION_CERT_SERIAL_LIST	0x20
#define KRL_SECTION_CERT_SERIAL_RANGE	0x21
#define KRL_SECTION_CERT_SERIAL_BITMAP	0x22
#define KRL_SECTION_CERT_KEY_ID		0x23

struct sshkey;
struct sshbuf;
struct ssh_krl;

struct ssh_krl *ssh_krl_init(void);
void ssh_krl_free(struct ssh_krl *krl);
void ssh_krl_set_version(struct ssh_krl *krl, u_int64_t version);
int ssh_krl_set_comment(struct ssh_krl *krl, const char *comment);
int ssh_krl_revoke_cert_by_serial(struct ssh_krl *krl,
    const struct sshkey *ca_key, u_int64_t serial);
int ssh_krl_revoke_cert_by_serial_range(struct ssh_krl *krl,
    const struct sshkey *ca_key, u_int64_t lo, u_int64_t hi);
int ssh_krl_revoke_cert_by_key_id(struct ssh_krl *krl,
    const struct sshkey *ca_key, const char *key_id);
int ssh_krl_revoke_key_explicit(struct ssh_krl *krl, const struct sshkey *key);
int ssh_krl_revoke_key_sha1(struct ssh_krl *krl, const struct sshkey *key);
int ssh_krl_revoke_key(struct ssh_krl *krl, const struct sshkey *key);
int ssh_krl_to_blob(struct ssh_krl *krl, struct sshbuf *buf,
    const struct sshkey **sign_keys, u_int nsign_keys);
int ssh_krl_from_blob(struct sshbuf *buf, struct ssh_krl **krlp,
    const struct sshkey **sign_ca_keys, size_t nsign_ca_keys);
int ssh_krl_check_key(struct ssh_krl *krl, const struct sshkey *key);
int ssh_krl_file_contains_key(const char *path, const struct sshkey *key);

#endif /* _KRL_H */

