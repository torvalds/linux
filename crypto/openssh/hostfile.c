/* $OpenBSD: hostfile.c,v 1.73 2018/07/16 03:09:13 djm Exp $ */
/*
 * Author: Tatu Ylonen <ylo@cs.hut.fi>
 * Copyright (c) 1995 Tatu Ylonen <ylo@cs.hut.fi>, Espoo, Finland
 *                    All rights reserved
 * Functions for manipulating the known hosts files.
 *
 * As far as I am concerned, the code I have written for this software
 * can be used freely for any purpose.  Any derived versions of this
 * software must be clearly marked as such, and if the derived work is
 * incompatible with the protocol description in the RFC file, it must be
 * called by a name other than "ssh" or "Secure Shell".
 *
 *
 * Copyright (c) 1999, 2000 Markus Friedl.  All rights reserved.
 * Copyright (c) 1999 Niels Provos.  All rights reserved.
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
#include <sys/stat.h>

#include <netinet/in.h>

#include <errno.h>
#include <resolv.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>

#include "xmalloc.h"
#include "match.h"
#include "sshkey.h"
#include "hostfile.h"
#include "log.h"
#include "misc.h"
#include "ssherr.h"
#include "digest.h"
#include "hmac.h"

struct hostkeys {
	struct hostkey_entry *entries;
	u_int num_entries;
};

/* XXX hmac is too easy to dictionary attack; use bcrypt? */

static int
extract_salt(const char *s, u_int l, u_char *salt, size_t salt_len)
{
	char *p, *b64salt;
	u_int b64len;
	int ret;

	if (l < sizeof(HASH_MAGIC) - 1) {
		debug2("extract_salt: string too short");
		return (-1);
	}
	if (strncmp(s, HASH_MAGIC, sizeof(HASH_MAGIC) - 1) != 0) {
		debug2("extract_salt: invalid magic identifier");
		return (-1);
	}
	s += sizeof(HASH_MAGIC) - 1;
	l -= sizeof(HASH_MAGIC) - 1;
	if ((p = memchr(s, HASH_DELIM, l)) == NULL) {
		debug2("extract_salt: missing salt termination character");
		return (-1);
	}

	b64len = p - s;
	/* Sanity check */
	if (b64len == 0 || b64len > 1024) {
		debug2("extract_salt: bad encoded salt length %u", b64len);
		return (-1);
	}
	b64salt = xmalloc(1 + b64len);
	memcpy(b64salt, s, b64len);
	b64salt[b64len] = '\0';

	ret = __b64_pton(b64salt, salt, salt_len);
	free(b64salt);
	if (ret == -1) {
		debug2("extract_salt: salt decode error");
		return (-1);
	}
	if (ret != (int)ssh_hmac_bytes(SSH_DIGEST_SHA1)) {
		debug2("extract_salt: expected salt len %zd, got %d",
		    ssh_hmac_bytes(SSH_DIGEST_SHA1), ret);
		return (-1);
	}

	return (0);
}

char *
host_hash(const char *host, const char *name_from_hostfile, u_int src_len)
{
	struct ssh_hmac_ctx *ctx;
	u_char salt[256], result[256];
	char uu_salt[512], uu_result[512];
	static char encoded[1024];
	u_int len;

	len = ssh_digest_bytes(SSH_DIGEST_SHA1);

	if (name_from_hostfile == NULL) {
		/* Create new salt */
		arc4random_buf(salt, len);
	} else {
		/* Extract salt from known host entry */
		if (extract_salt(name_from_hostfile, src_len, salt,
		    sizeof(salt)) == -1)
			return (NULL);
	}

	if ((ctx = ssh_hmac_start(SSH_DIGEST_SHA1)) == NULL ||
	    ssh_hmac_init(ctx, salt, len) < 0 ||
	    ssh_hmac_update(ctx, host, strlen(host)) < 0 ||
	    ssh_hmac_final(ctx, result, sizeof(result)))
		fatal("%s: ssh_hmac failed", __func__);
	ssh_hmac_free(ctx);

	if (__b64_ntop(salt, len, uu_salt, sizeof(uu_salt)) == -1 ||
	    __b64_ntop(result, len, uu_result, sizeof(uu_result)) == -1)
		fatal("%s: __b64_ntop failed", __func__);

	snprintf(encoded, sizeof(encoded), "%s%s%c%s", HASH_MAGIC, uu_salt,
	    HASH_DELIM, uu_result);

	return (encoded);
}

/*
 * Parses an RSA (number of bits, e, n) or DSA key from a string.  Moves the
 * pointer over the key.  Skips any whitespace at the beginning and at end.
 */

int
hostfile_read_key(char **cpp, u_int *bitsp, struct sshkey *ret)
{
	char *cp;
	int r;

	/* Skip leading whitespace. */
	for (cp = *cpp; *cp == ' ' || *cp == '\t'; cp++)
		;

	if ((r = sshkey_read(ret, &cp)) != 0)
		return 0;

	/* Skip trailing whitespace. */
	for (; *cp == ' ' || *cp == '\t'; cp++)
		;

	/* Return results. */
	*cpp = cp;
	if (bitsp != NULL)
		*bitsp = sshkey_size(ret);
	return 1;
}

static HostkeyMarker
check_markers(char **cpp)
{
	char marker[32], *sp, *cp = *cpp;
	int ret = MRK_NONE;

	while (*cp == '@') {
		/* Only one marker is allowed */
		if (ret != MRK_NONE)
			return MRK_ERROR;
		/* Markers are terminated by whitespace */
		if ((sp = strchr(cp, ' ')) == NULL &&
		    (sp = strchr(cp, '\t')) == NULL)
			return MRK_ERROR;
		/* Extract marker for comparison */
		if (sp <= cp + 1 || sp >= cp + sizeof(marker))
			return MRK_ERROR;
		memcpy(marker, cp, sp - cp);
		marker[sp - cp] = '\0';
		if (strcmp(marker, CA_MARKER) == 0)
			ret = MRK_CA;
		else if (strcmp(marker, REVOKE_MARKER) == 0)
			ret = MRK_REVOKE;
		else
			return MRK_ERROR;

		/* Skip past marker and any whitespace that follows it */
		cp = sp;
		for (; *cp == ' ' || *cp == '\t'; cp++)
			;
	}
	*cpp = cp;
	return ret;
}

struct hostkeys *
init_hostkeys(void)
{
	struct hostkeys *ret = xcalloc(1, sizeof(*ret));

	ret->entries = NULL;
	return ret;
}

struct load_callback_ctx {
	const char *host;
	u_long num_loaded;
	struct hostkeys *hostkeys;
};

static int
record_hostkey(struct hostkey_foreach_line *l, void *_ctx)
{
	struct load_callback_ctx *ctx = (struct load_callback_ctx *)_ctx;
	struct hostkeys *hostkeys = ctx->hostkeys;
	struct hostkey_entry *tmp;

	if (l->status == HKF_STATUS_INVALID) {
		/* XXX make this verbose() in the future */
		debug("%s:%ld: parse error in hostkeys file",
		    l->path, l->linenum);
		return 0;
	}

	debug3("%s: found %skey type %s in file %s:%lu", __func__,
	    l->marker == MRK_NONE ? "" :
	    (l->marker == MRK_CA ? "ca " : "revoked "),
	    sshkey_type(l->key), l->path, l->linenum);
	if ((tmp = recallocarray(hostkeys->entries, hostkeys->num_entries,
	    hostkeys->num_entries + 1, sizeof(*hostkeys->entries))) == NULL)
		return SSH_ERR_ALLOC_FAIL;
	hostkeys->entries = tmp;
	hostkeys->entries[hostkeys->num_entries].host = xstrdup(ctx->host);
	hostkeys->entries[hostkeys->num_entries].file = xstrdup(l->path);
	hostkeys->entries[hostkeys->num_entries].line = l->linenum;
	hostkeys->entries[hostkeys->num_entries].key = l->key;
	l->key = NULL; /* steal it */
	hostkeys->entries[hostkeys->num_entries].marker = l->marker;
	hostkeys->num_entries++;
	ctx->num_loaded++;

	return 0;
}

void
load_hostkeys(struct hostkeys *hostkeys, const char *host, const char *path)
{
	int r;
	struct load_callback_ctx ctx;

	ctx.host = host;
	ctx.num_loaded = 0;
	ctx.hostkeys = hostkeys;

	if ((r = hostkeys_foreach(path, record_hostkey, &ctx, host, NULL,
	    HKF_WANT_MATCH|HKF_WANT_PARSE_KEY)) != 0) {
		if (r != SSH_ERR_SYSTEM_ERROR && errno != ENOENT)
			debug("%s: hostkeys_foreach failed for %s: %s",
			    __func__, path, ssh_err(r));
	}
	if (ctx.num_loaded != 0)
		debug3("%s: loaded %lu keys from %s", __func__,
		    ctx.num_loaded, host);
}

void
free_hostkeys(struct hostkeys *hostkeys)
{
	u_int i;

	for (i = 0; i < hostkeys->num_entries; i++) {
		free(hostkeys->entries[i].host);
		free(hostkeys->entries[i].file);
		sshkey_free(hostkeys->entries[i].key);
		explicit_bzero(hostkeys->entries + i, sizeof(*hostkeys->entries));
	}
	free(hostkeys->entries);
	explicit_bzero(hostkeys, sizeof(*hostkeys));
	free(hostkeys);
}

static int
check_key_not_revoked(struct hostkeys *hostkeys, struct sshkey *k)
{
	int is_cert = sshkey_is_cert(k);
	u_int i;

	for (i = 0; i < hostkeys->num_entries; i++) {
		if (hostkeys->entries[i].marker != MRK_REVOKE)
			continue;
		if (sshkey_equal_public(k, hostkeys->entries[i].key))
			return -1;
		if (is_cert &&
		    sshkey_equal_public(k->cert->signature_key,
		    hostkeys->entries[i].key))
			return -1;
	}
	return 0;
}

/*
 * Match keys against a specified key, or look one up by key type.
 *
 * If looking for a keytype (key == NULL) and one is found then return
 * HOST_FOUND, otherwise HOST_NEW.
 *
 * If looking for a key (key != NULL):
 *  1. If the key is a cert and a matching CA is found, return HOST_OK
 *  2. If the key is not a cert and a matching key is found, return HOST_OK
 *  3. If no key matches but a key with a different type is found, then
 *     return HOST_CHANGED
 *  4. If no matching keys are found, then return HOST_NEW.
 *
 * Finally, check any found key is not revoked.
 */
static HostStatus
check_hostkeys_by_key_or_type(struct hostkeys *hostkeys,
    struct sshkey *k, int keytype, const struct hostkey_entry **found)
{
	u_int i;
	HostStatus end_return = HOST_NEW;
	int want_cert = sshkey_is_cert(k);
	HostkeyMarker want_marker = want_cert ? MRK_CA : MRK_NONE;

	if (found != NULL)
		*found = NULL;

	for (i = 0; i < hostkeys->num_entries; i++) {
		if (hostkeys->entries[i].marker != want_marker)
			continue;
		if (k == NULL) {
			if (hostkeys->entries[i].key->type != keytype)
				continue;
			end_return = HOST_FOUND;
			if (found != NULL)
				*found = hostkeys->entries + i;
			k = hostkeys->entries[i].key;
			break;
		}
		if (want_cert) {
			if (sshkey_equal_public(k->cert->signature_key,
			    hostkeys->entries[i].key)) {
				/* A matching CA exists */
				end_return = HOST_OK;
				if (found != NULL)
					*found = hostkeys->entries + i;
				break;
			}
		} else {
			if (sshkey_equal(k, hostkeys->entries[i].key)) {
				end_return = HOST_OK;
				if (found != NULL)
					*found = hostkeys->entries + i;
				break;
			}
			/* A non-maching key exists */
			end_return = HOST_CHANGED;
			if (found != NULL)
				*found = hostkeys->entries + i;
		}
	}
	if (check_key_not_revoked(hostkeys, k) != 0) {
		end_return = HOST_REVOKED;
		if (found != NULL)
			*found = NULL;
	}
	return end_return;
}

HostStatus
check_key_in_hostkeys(struct hostkeys *hostkeys, struct sshkey *key,
    const struct hostkey_entry **found)
{
	if (key == NULL)
		fatal("no key to look up");
	return check_hostkeys_by_key_or_type(hostkeys, key, 0, found);
}

int
lookup_key_in_hostkeys_by_type(struct hostkeys *hostkeys, int keytype,
    const struct hostkey_entry **found)
{
	return (check_hostkeys_by_key_or_type(hostkeys, NULL, keytype,
	    found) == HOST_FOUND);
}

static int
write_host_entry(FILE *f, const char *host, const char *ip,
    const struct sshkey *key, int store_hash)
{
	int r, success = 0;
	char *hashed_host = NULL, *lhost;

	lhost = xstrdup(host);
	lowercase(lhost);

	if (store_hash) {
		if ((hashed_host = host_hash(lhost, NULL, 0)) == NULL) {
			error("%s: host_hash failed", __func__);
			free(lhost);
			return 0;
		}
		fprintf(f, "%s ", hashed_host);
	} else if (ip != NULL)
		fprintf(f, "%s,%s ", lhost, ip);
	else {
		fprintf(f, "%s ", lhost);
	}
	free(lhost);
	if ((r = sshkey_write(key, f)) == 0)
		success = 1;
	else
		error("%s: sshkey_write failed: %s", __func__, ssh_err(r));
	fputc('\n', f);
	return success;
}

/*
 * Appends an entry to the host file.  Returns false if the entry could not
 * be appended.
 */
int
add_host_to_hostfile(const char *filename, const char *host,
    const struct sshkey *key, int store_hash)
{
	FILE *f;
	int success;

	if (key == NULL)
		return 1;	/* XXX ? */
	f = fopen(filename, "a");
	if (!f)
		return 0;
	success = write_host_entry(f, host, NULL, key, store_hash);
	fclose(f);
	return success;
}

struct host_delete_ctx {
	FILE *out;
	int quiet;
	const char *host;
	int *skip_keys; /* XXX split for host/ip? might want to ensure both */
	struct sshkey * const *keys;
	size_t nkeys;
	int modified;
};

static int
host_delete(struct hostkey_foreach_line *l, void *_ctx)
{
	struct host_delete_ctx *ctx = (struct host_delete_ctx *)_ctx;
	int loglevel = ctx->quiet ? SYSLOG_LEVEL_DEBUG1 : SYSLOG_LEVEL_VERBOSE;
	size_t i;

	if (l->status == HKF_STATUS_MATCHED) {
		if (l->marker != MRK_NONE) {
			/* Don't remove CA and revocation lines */
			fprintf(ctx->out, "%s\n", l->line);
			return 0;
		}

		/*
		 * If this line contains one of the keys that we will be
		 * adding later, then don't change it and mark the key for
		 * skipping.
		 */
		for (i = 0; i < ctx->nkeys; i++) {
			if (sshkey_equal(ctx->keys[i], l->key)) {
				ctx->skip_keys[i] = 1;
				fprintf(ctx->out, "%s\n", l->line);
				debug3("%s: %s key already at %s:%ld", __func__,
				    sshkey_type(l->key), l->path, l->linenum);
				return 0;
			}
		}

		/*
		 * Hostname matches and has no CA/revoke marker, delete it
		 * by *not* writing the line to ctx->out.
		 */
		do_log2(loglevel, "%s%s%s:%ld: Removed %s key for host %s",
		    ctx->quiet ? __func__ : "", ctx->quiet ? ": " : "",
		    l->path, l->linenum, sshkey_type(l->key), ctx->host);
		ctx->modified = 1;
		return 0;
	}
	/* Retain non-matching hosts and invalid lines when deleting */
	if (l->status == HKF_STATUS_INVALID) {
		do_log2(loglevel, "%s%s%s:%ld: invalid known_hosts entry",
		    ctx->quiet ? __func__ : "", ctx->quiet ? ": " : "",
		    l->path, l->linenum);
	}
	fprintf(ctx->out, "%s\n", l->line);
	return 0;
}

int
hostfile_replace_entries(const char *filename, const char *host, const char *ip,
    struct sshkey **keys, size_t nkeys, int store_hash, int quiet, int hash_alg)
{
	int r, fd, oerrno = 0;
	int loglevel = quiet ? SYSLOG_LEVEL_DEBUG1 : SYSLOG_LEVEL_VERBOSE;
	struct host_delete_ctx ctx;
	char *fp, *temp = NULL, *back = NULL;
	mode_t omask;
	size_t i;

	omask = umask(077);

	memset(&ctx, 0, sizeof(ctx));
	ctx.host = host;
	ctx.quiet = quiet;
	if ((ctx.skip_keys = calloc(nkeys, sizeof(*ctx.skip_keys))) == NULL)
		return SSH_ERR_ALLOC_FAIL;
	ctx.keys = keys;
	ctx.nkeys = nkeys;
	ctx.modified = 0;

	/*
	 * Prepare temporary file for in-place deletion.
	 */
	if ((r = asprintf(&temp, "%s.XXXXXXXXXXX", filename)) < 0 ||
	    (r = asprintf(&back, "%s.old", filename)) < 0) {
		r = SSH_ERR_ALLOC_FAIL;
		goto fail;
	}

	if ((fd = mkstemp(temp)) == -1) {
		oerrno = errno;
		error("%s: mkstemp: %s", __func__, strerror(oerrno));
		r = SSH_ERR_SYSTEM_ERROR;
		goto fail;
	}
	if ((ctx.out = fdopen(fd, "w")) == NULL) {
		oerrno = errno;
		close(fd);
		error("%s: fdopen: %s", __func__, strerror(oerrno));
		r = SSH_ERR_SYSTEM_ERROR;
		goto fail;
	}

	/* Remove all entries for the specified host from the file */
	if ((r = hostkeys_foreach(filename, host_delete, &ctx, host, ip,
	    HKF_WANT_PARSE_KEY)) != 0) {
		error("%s: hostkeys_foreach failed: %s", __func__, ssh_err(r));
		goto fail;
	}

	/* Add the requested keys */
	for (i = 0; i < nkeys; i++) {
		if (ctx.skip_keys[i])
			continue;
		if ((fp = sshkey_fingerprint(keys[i], hash_alg,
		    SSH_FP_DEFAULT)) == NULL) {
			r = SSH_ERR_ALLOC_FAIL;
			goto fail;
		}
		do_log2(loglevel, "%s%sAdding new key for %s to %s: %s %s",
		    quiet ? __func__ : "", quiet ? ": " : "", host, filename,
		    sshkey_ssh_name(keys[i]), fp);
		free(fp);
		if (!write_host_entry(ctx.out, host, ip, keys[i], store_hash)) {
			r = SSH_ERR_INTERNAL_ERROR;
			goto fail;
		}
		ctx.modified = 1;
	}
	fclose(ctx.out);
	ctx.out = NULL;

	if (ctx.modified) {
		/* Backup the original file and replace it with the temporary */
		if (unlink(back) == -1 && errno != ENOENT) {
			oerrno = errno;
			error("%s: unlink %.100s: %s", __func__,
			    back, strerror(errno));
			r = SSH_ERR_SYSTEM_ERROR;
			goto fail;
		}
		if (link(filename, back) == -1) {
			oerrno = errno;
			error("%s: link %.100s to %.100s: %s", __func__,
			    filename, back, strerror(errno));
			r = SSH_ERR_SYSTEM_ERROR;
			goto fail;
		}
		if (rename(temp, filename) == -1) {
			oerrno = errno;
			error("%s: rename \"%s\" to \"%s\": %s", __func__,
			    temp, filename, strerror(errno));
			r = SSH_ERR_SYSTEM_ERROR;
			goto fail;
		}
	} else {
		/* No changes made; just delete the temporary file */
		if (unlink(temp) != 0)
			error("%s: unlink \"%s\": %s", __func__,
			    temp, strerror(errno));
	}

	/* success */
	r = 0;
 fail:
	if (temp != NULL && r != 0)
		unlink(temp);
	free(temp);
	free(back);
	if (ctx.out != NULL)
		fclose(ctx.out);
	free(ctx.skip_keys);
	umask(omask);
	if (r == SSH_ERR_SYSTEM_ERROR)
		errno = oerrno;
	return r;
}

static int
match_maybe_hashed(const char *host, const char *names, int *was_hashed)
{
	int hashed = *names == HASH_DELIM;
	const char *hashed_host;
	size_t nlen = strlen(names);

	if (was_hashed != NULL)
		*was_hashed = hashed;
	if (hashed) {
		if ((hashed_host = host_hash(host, names, nlen)) == NULL)
			return -1;
		return nlen == strlen(hashed_host) &&
		    strncmp(hashed_host, names, nlen) == 0;
	}
	return match_hostname(host, names) == 1;
}

int
hostkeys_foreach(const char *path, hostkeys_foreach_fn *callback, void *ctx,
    const char *host, const char *ip, u_int options)
{
	FILE *f;
	char *line = NULL, ktype[128];
	u_long linenum = 0;
	char *cp, *cp2;
	u_int kbits;
	int hashed;
	int s, r = 0;
	struct hostkey_foreach_line lineinfo;
	size_t linesize = 0, l;

	memset(&lineinfo, 0, sizeof(lineinfo));
	if (host == NULL && (options & HKF_WANT_MATCH) != 0)
		return SSH_ERR_INVALID_ARGUMENT;
	if ((f = fopen(path, "r")) == NULL)
		return SSH_ERR_SYSTEM_ERROR;

	debug3("%s: reading file \"%s\"", __func__, path);
	while (getline(&line, &linesize, f) != -1) {
		linenum++;
		line[strcspn(line, "\n")] = '\0';

		free(lineinfo.line);
		sshkey_free(lineinfo.key);
		memset(&lineinfo, 0, sizeof(lineinfo));
		lineinfo.path = path;
		lineinfo.linenum = linenum;
		lineinfo.line = xstrdup(line);
		lineinfo.marker = MRK_NONE;
		lineinfo.status = HKF_STATUS_OK;
		lineinfo.keytype = KEY_UNSPEC;

		/* Skip any leading whitespace, comments and empty lines. */
		for (cp = line; *cp == ' ' || *cp == '\t'; cp++)
			;
		if (!*cp || *cp == '#' || *cp == '\n') {
			if ((options & HKF_WANT_MATCH) == 0) {
				lineinfo.status = HKF_STATUS_COMMENT;
				if ((r = callback(&lineinfo, ctx)) != 0)
					break;
			}
			continue;
		}

		if ((lineinfo.marker = check_markers(&cp)) == MRK_ERROR) {
			verbose("%s: invalid marker at %s:%lu",
			    __func__, path, linenum);
			if ((options & HKF_WANT_MATCH) == 0)
				goto bad;
			continue;
		}

		/* Find the end of the host name portion. */
		for (cp2 = cp; *cp2 && *cp2 != ' ' && *cp2 != '\t'; cp2++)
			;
		lineinfo.hosts = cp;
		*cp2++ = '\0';

		/* Check if the host name matches. */
		if (host != NULL) {
			if ((s = match_maybe_hashed(host, lineinfo.hosts,
			    &hashed)) == -1) {
				debug2("%s: %s:%ld: bad host hash \"%.32s\"",
				    __func__, path, linenum, lineinfo.hosts);
				goto bad;
			}
			if (s == 1) {
				lineinfo.status = HKF_STATUS_MATCHED;
				lineinfo.match |= HKF_MATCH_HOST |
				    (hashed ? HKF_MATCH_HOST_HASHED : 0);
			}
			/* Try matching IP address if supplied */
			if (ip != NULL) {
				if ((s = match_maybe_hashed(ip, lineinfo.hosts,
				    &hashed)) == -1) {
					debug2("%s: %s:%ld: bad ip hash "
					    "\"%.32s\"", __func__, path,
					    linenum, lineinfo.hosts);
					goto bad;
				}
				if (s == 1) {
					lineinfo.status = HKF_STATUS_MATCHED;
					lineinfo.match |= HKF_MATCH_IP |
					    (hashed ? HKF_MATCH_IP_HASHED : 0);
				}
			}
			/*
			 * Skip this line if host matching requested and
			 * neither host nor address matched.
			 */
			if ((options & HKF_WANT_MATCH) != 0 &&
			    lineinfo.status != HKF_STATUS_MATCHED)
				continue;
		}

		/* Got a match.  Skip host name and any following whitespace */
		for (; *cp2 == ' ' || *cp2 == '\t'; cp2++)
			;
		if (*cp2 == '\0' || *cp2 == '#') {
			debug2("%s:%ld: truncated before key type",
			    path, linenum);
			goto bad;
		}
		lineinfo.rawkey = cp = cp2;

		if ((options & HKF_WANT_PARSE_KEY) != 0) {
			/*
			 * Extract the key from the line.  This will skip
			 * any leading whitespace.  Ignore badly formatted
			 * lines.
			 */
			if ((lineinfo.key = sshkey_new(KEY_UNSPEC)) == NULL) {
				error("%s: sshkey_new failed", __func__);
				r = SSH_ERR_ALLOC_FAIL;
				break;
			}
			if (!hostfile_read_key(&cp, &kbits, lineinfo.key)) {
				goto bad;
			}
			lineinfo.keytype = lineinfo.key->type;
			lineinfo.comment = cp;
		} else {
			/* Extract and parse key type */
			l = strcspn(lineinfo.rawkey, " \t");
			if (l <= 1 || l >= sizeof(ktype) ||
			    lineinfo.rawkey[l] == '\0')
				goto bad;
			memcpy(ktype, lineinfo.rawkey, l);
			ktype[l] = '\0';
			lineinfo.keytype = sshkey_type_from_name(ktype);

			/*
			 * Assume legacy RSA1 if the first component is a short
			 * decimal number.
			 */
			if (lineinfo.keytype == KEY_UNSPEC && l < 8 &&
			    strspn(ktype, "0123456789") == l)
				goto bad;

			/*
			 * Check that something other than whitespace follows
			 * the key type. This won't catch all corruption, but
			 * it does catch trivial truncation.
			 */
			cp2 += l; /* Skip past key type */
			for (; *cp2 == ' ' || *cp2 == '\t'; cp2++)
				;
			if (*cp2 == '\0' || *cp2 == '#') {
				debug2("%s:%ld: truncated after key type",
				    path, linenum);
				lineinfo.keytype = KEY_UNSPEC;
			}
			if (lineinfo.keytype == KEY_UNSPEC) {
 bad:
				sshkey_free(lineinfo.key);
				lineinfo.key = NULL;
				lineinfo.status = HKF_STATUS_INVALID;
				if ((r = callback(&lineinfo, ctx)) != 0)
					break;
				continue;
			}
		}
		if ((r = callback(&lineinfo, ctx)) != 0)
			break;
	}
	sshkey_free(lineinfo.key);
	free(lineinfo.line);
	free(line);
	fclose(f);
	return r;
}
