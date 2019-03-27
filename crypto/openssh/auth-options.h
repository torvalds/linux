/* $OpenBSD: auth-options.h,v 1.27 2018/06/06 18:23:32 djm Exp $ */

/*
 * Copyright (c) 2018 Damien Miller <djm@mindrot.org>
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

#ifndef AUTH_OPTIONS_H
#define AUTH_OPTIONS_H

struct passwd;
struct sshkey;

/*
 * sshauthopt represents key options parsed from authorized_keys or
 * from certificate extensions/options.
 */
struct sshauthopt {
	/* Feature flags */
	int permit_port_forwarding_flag;
	int permit_agent_forwarding_flag;
	int permit_x11_forwarding_flag;
	int permit_pty_flag;
	int permit_user_rc;

	/* "restrict" keyword was invoked */
	int restricted;

	/* key/principal expiry date */
	uint64_t valid_before;

	/* Certificate-related options */
	int cert_authority;
	char *cert_principals;

	int force_tun_device;
	char *force_command;

	/* Custom environment */
	size_t nenv;
	char **env;

	/* Permitted port forwardings */
	size_t npermitopen;
	char **permitopen;

	/* Permitted listens (remote forwarding) */
	size_t npermitlisten;
	char **permitlisten;

	/*
	 * Permitted host/addresses (comma-separated)
	 * Caller must check source address matches both lists (if present).
	 */
	char *required_from_host_cert;
	char *required_from_host_keys;
};

struct sshauthopt *sshauthopt_new(void);
struct sshauthopt *sshauthopt_new_with_keys_defaults(void);
void sshauthopt_free(struct sshauthopt *opts);
struct sshauthopt *sshauthopt_copy(const struct sshauthopt *orig);
int sshauthopt_serialise(const struct sshauthopt *opts, struct sshbuf *m, int);
int sshauthopt_deserialise(struct sshbuf *m, struct sshauthopt **opts);

/*
 * Parse authorized_keys options. Returns an options structure on success
 * or NULL on failure. Will set errstr on failure.
 */
struct sshauthopt *sshauthopt_parse(const char *s, const char **errstr);

/*
 * Parse certification options to a struct sshauthopt.
 * Returns options on success or NULL on failure.
 */
struct sshauthopt *sshauthopt_from_cert(struct sshkey *k);

/*
 * Merge key options.
 */
struct sshauthopt *sshauthopt_merge(const struct sshauthopt *primary,
    const struct sshauthopt *additional, const char **errstrp);

#endif
