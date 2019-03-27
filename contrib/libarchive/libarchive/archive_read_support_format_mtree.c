/*-
 * Copyright (c) 2003-2007 Tim Kientzle
 * Copyright (c) 2008 Joerg Sonnenberger
 * Copyright (c) 2011-2012 Michihiro NAKAJIMA
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "archive_platform.h"
__FBSDID("$FreeBSD$");

#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif
#include <stddef.h>
/* #include <stdint.h> */ /* See archive_platform.h */
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif

#include "archive.h"
#include "archive_entry.h"
#include "archive_private.h"
#include "archive_rb.h"
#include "archive_read_private.h"
#include "archive_string.h"
#include "archive_pack_dev.h"

#ifndef O_BINARY
#define	O_BINARY 0
#endif
#ifndef O_CLOEXEC
#define O_CLOEXEC	0
#endif

#define	MTREE_HAS_DEVICE	0x0001
#define	MTREE_HAS_FFLAGS	0x0002
#define	MTREE_HAS_GID		0x0004
#define	MTREE_HAS_GNAME		0x0008
#define	MTREE_HAS_MTIME		0x0010
#define	MTREE_HAS_NLINK		0x0020
#define	MTREE_HAS_PERM		0x0040
#define	MTREE_HAS_SIZE		0x0080
#define	MTREE_HAS_TYPE		0x0100
#define	MTREE_HAS_UID		0x0200
#define	MTREE_HAS_UNAME		0x0400

#define	MTREE_HAS_OPTIONAL	0x0800
#define	MTREE_HAS_NOCHANGE	0x1000 /* FreeBSD specific */

#define	MAX_LINE_LEN		(1024 * 1024)

struct mtree_option {
	struct mtree_option *next;
	char *value;
};

struct mtree_entry {
	struct archive_rb_node rbnode;
	struct mtree_entry *next_dup;
	struct mtree_entry *next;
	struct mtree_option *options;
	char *name;
	char full;
	char used;
};

struct mtree {
	struct archive_string	 line;
	size_t			 buffsize;
	char			*buff;
	int64_t			 offset;
	int			 fd;
	int			 archive_format;
	const char		*archive_format_name;
	struct mtree_entry	*entries;
	struct mtree_entry	*this_entry;
	struct archive_rb_tree	 entry_rbtree;
	struct archive_string	 current_dir;
	struct archive_string	 contents_name;

	struct archive_entry_linkresolver *resolver;
	struct archive_rb_tree rbtree;

	int64_t			 cur_size;
	char checkfs;
};

static int	bid_keycmp(const char *, const char *, ssize_t);
static int	cleanup(struct archive_read *);
static int	detect_form(struct archive_read *, int *);
static int	mtree_bid(struct archive_read *, int);
static int	parse_file(struct archive_read *, struct archive_entry *,
		    struct mtree *, struct mtree_entry *, int *);
static void	parse_escapes(char *, struct mtree_entry *);
static int	parse_line(struct archive_read *, struct archive_entry *,
		    struct mtree *, struct mtree_entry *, int *);
static int	parse_keyword(struct archive_read *, struct mtree *,
		    struct archive_entry *, struct mtree_option *, int *);
static int	read_data(struct archive_read *a,
		    const void **buff, size_t *size, int64_t *offset);
static ssize_t	readline(struct archive_read *, struct mtree *, char **, ssize_t);
static int	skip(struct archive_read *a);
static int	read_header(struct archive_read *,
		    struct archive_entry *);
static int64_t	mtree_atol(char **, int base);

/*
 * There's no standard for TIME_T_MAX/TIME_T_MIN.  So we compute them
 * here.  TODO: Move this to configure time, but be careful
 * about cross-compile environments.
 */
static int64_t
get_time_t_max(void)
{
#if defined(TIME_T_MAX)
	return TIME_T_MAX;
#else
	/* ISO C allows time_t to be a floating-point type,
	   but POSIX requires an integer type.  The following
	   should work on any system that follows the POSIX
	   conventions. */
	if (((time_t)0) < ((time_t)-1)) {
		/* Time_t is unsigned */
		return (~(time_t)0);
	} else {
		/* Time_t is signed. */
		/* Assume it's the same as int64_t or int32_t */
		if (sizeof(time_t) == sizeof(int64_t)) {
			return (time_t)INT64_MAX;
		} else {
			return (time_t)INT32_MAX;
		}
	}
#endif
}

static int64_t
get_time_t_min(void)
{
#if defined(TIME_T_MIN)
	return TIME_T_MIN;
#else
	if (((time_t)0) < ((time_t)-1)) {
		/* Time_t is unsigned */
		return (time_t)0;
	} else {
		/* Time_t is signed. */
		if (sizeof(time_t) == sizeof(int64_t)) {
			return (time_t)INT64_MIN;
		} else {
			return (time_t)INT32_MIN;
		}
	}
#endif
}

static int
archive_read_format_mtree_options(struct archive_read *a,
    const char *key, const char *val)
{
	struct mtree *mtree;

	mtree = (struct mtree *)(a->format->data);
	if (strcmp(key, "checkfs")  == 0) {
		/* Allows to read information missing from the mtree from the file system */
		if (val == NULL || val[0] == 0) {
			mtree->checkfs = 0;
		} else {
			mtree->checkfs = 1;
		}
		return (ARCHIVE_OK);
	}

	/* Note: The "warn" return is just to inform the options
	 * supervisor that we didn't handle it.  It will generate
	 * a suitable error if no one used this option. */
	return (ARCHIVE_WARN);
}

static void
free_options(struct mtree_option *head)
{
	struct mtree_option *next;

	for (; head != NULL; head = next) {
		next = head->next;
		free(head->value);
		free(head);
	}
}

static int
mtree_cmp_node(const struct archive_rb_node *n1,
    const struct archive_rb_node *n2)
{
	const struct mtree_entry *e1 = (const struct mtree_entry *)n1;
	const struct mtree_entry *e2 = (const struct mtree_entry *)n2;

	return (strcmp(e1->name, e2->name));
}

static int
mtree_cmp_key(const struct archive_rb_node *n, const void *key)
{
	const struct mtree_entry *e = (const struct mtree_entry *)n;

	return (strcmp(e->name, key));
}

int
archive_read_support_format_mtree(struct archive *_a)
{
	static const struct archive_rb_tree_ops rb_ops = {
		mtree_cmp_node, mtree_cmp_key,
	};
	struct archive_read *a = (struct archive_read *)_a;
	struct mtree *mtree;
	int r;

	archive_check_magic(_a, ARCHIVE_READ_MAGIC,
	    ARCHIVE_STATE_NEW, "archive_read_support_format_mtree");

	mtree = (struct mtree *)calloc(1, sizeof(*mtree));
	if (mtree == NULL) {
		archive_set_error(&a->archive, ENOMEM,
		    "Can't allocate mtree data");
		return (ARCHIVE_FATAL);
	}
	mtree->fd = -1;

	__archive_rb_tree_init(&mtree->rbtree, &rb_ops);

	r = __archive_read_register_format(a, mtree, "mtree",
           mtree_bid, archive_read_format_mtree_options, read_header, read_data, skip, NULL, cleanup, NULL, NULL);

	if (r != ARCHIVE_OK)
		free(mtree);
	return (ARCHIVE_OK);
}

static int
cleanup(struct archive_read *a)
{
	struct mtree *mtree;
	struct mtree_entry *p, *q;

	mtree = (struct mtree *)(a->format->data);

	p = mtree->entries;
	while (p != NULL) {
		q = p->next;
		free(p->name);
		free_options(p->options);
		free(p);
		p = q;
	}
	archive_string_free(&mtree->line);
	archive_string_free(&mtree->current_dir);
	archive_string_free(&mtree->contents_name);
	archive_entry_linkresolver_free(mtree->resolver);

	free(mtree->buff);
	free(mtree);
	(a->format->data) = NULL;
	return (ARCHIVE_OK);
}

static ssize_t
get_line_size(const char *b, ssize_t avail, ssize_t *nlsize)
{
	ssize_t len;

	len = 0;
	while (len < avail) {
		switch (*b) {
		case '\0':/* Non-ascii character or control character. */
			if (nlsize != NULL)
				*nlsize = 0;
			return (-1);
		case '\r':
			if (avail-len > 1 && b[1] == '\n') {
				if (nlsize != NULL)
					*nlsize = 2;
				return (len+2);
			}
			/* FALL THROUGH */
		case '\n':
			if (nlsize != NULL)
				*nlsize = 1;
			return (len+1);
		default:
			b++;
			len++;
			break;
		}
	}
	if (nlsize != NULL)
		*nlsize = 0;
	return (avail);
}

/*
 *  <---------------- ravail --------------------->
 *  <-- diff ------> <---  avail ----------------->
 *                   <---- len ----------->
 * | Previous lines | line being parsed  nl extra |
 *                  ^
 *                  b
 *
 */
static ssize_t
next_line(struct archive_read *a,
    const char **b, ssize_t *avail, ssize_t *ravail, ssize_t *nl)
{
	ssize_t len;
	int quit;
	
	quit = 0;
	if (*avail == 0) {
		*nl = 0;
		len = 0;
	} else
		len = get_line_size(*b, *avail, nl);
	/*
	 * Read bytes more while it does not reach the end of line.
	 */
	while (*nl == 0 && len == *avail && !quit) {
		ssize_t diff = *ravail - *avail;
		size_t nbytes_req = (*ravail+1023) & ~1023U;
		ssize_t tested;

		/*
		 * Place an arbitrary limit on the line length.
		 * mtree is almost free-form input and without line length limits,
		 * it can consume a lot of memory.
		 */
		if (len >= MAX_LINE_LEN)
			return (-1);

		/* Increase reading bytes if it is not enough to at least
		 * new two lines. */
		if (nbytes_req < (size_t)*ravail + 160)
			nbytes_req <<= 1;

		*b = __archive_read_ahead(a, nbytes_req, avail);
		if (*b == NULL) {
			if (*ravail >= *avail)
				return (0);
			/* Reading bytes reaches the end of file. */
			*b = __archive_read_ahead(a, *avail, avail);
			quit = 1;
		}
		*ravail = *avail;
		*b += diff;
		*avail -= diff;
		tested = len;/* Skip some bytes we already determinated. */
		len = get_line_size(*b + len, *avail - len, nl);
		if (len >= 0)
			len += tested;
	}
	return (len);
}

/*
 * Compare characters with a mtree keyword.
 * Returns the length of a mtree keyword if matched.
 * Returns 0 if not matched.
 */
static int
bid_keycmp(const char *p, const char *key, ssize_t len)
{
	int match_len = 0;

	while (len > 0 && *p && *key) {
		if (*p == *key) {
			--len;
			++p;
			++key;
			++match_len;
			continue;
		}
		return (0);/* Not match */
	}
	if (*key != '\0')
		return (0);/* Not match */

	/* A following character should be specified characters */
	if (p[0] == '=' || p[0] == ' ' || p[0] == '\t' ||
	    p[0] == '\n' || p[0] == '\r' ||
	   (p[0] == '\\' && (p[1] == '\n' || p[1] == '\r')))
		return (match_len);
	return (0);/* Not match */
}

/*
 * Test whether the characters 'p' has is mtree keyword.
 * Returns the length of a detected keyword.
 * Returns 0 if any keywords were not found.
 */
static int
bid_keyword(const char *p,  ssize_t len)
{
	static const char * const keys_c[] = {
		"content", "contents", "cksum", NULL
	};
	static const char * const keys_df[] = {
		"device", "flags", NULL
	};
	static const char * const keys_g[] = {
		"gid", "gname", NULL
	};
	static const char * const keys_il[] = {
		"ignore", "inode", "link", NULL
	};
	static const char * const keys_m[] = {
		"md5", "md5digest", "mode", NULL
	};
	static const char * const keys_no[] = {
		"nlink", "nochange", "optional", NULL
	};
	static const char * const keys_r[] = {
		"resdevice", "rmd160", "rmd160digest", NULL
	};
	static const char * const keys_s[] = {
		"sha1", "sha1digest",
		"sha256", "sha256digest",
		"sha384", "sha384digest",
		"sha512", "sha512digest",
		"size", NULL
	};
	static const char * const keys_t[] = {
		"tags", "time", "type", NULL
	};
	static const char * const keys_u[] = {
		"uid", "uname",	NULL
	};
	const char * const *keys;
	int i;

	switch (*p) {
	case 'c': keys = keys_c; break;
	case 'd': case 'f': keys = keys_df; break;
	case 'g': keys = keys_g; break;
	case 'i': case 'l': keys = keys_il; break;
	case 'm': keys = keys_m; break;
	case 'n': case 'o': keys = keys_no; break;
	case 'r': keys = keys_r; break;
	case 's': keys = keys_s; break;
	case 't': keys = keys_t; break;
	case 'u': keys = keys_u; break;
	default: return (0);/* Unknown key */
	}

	for (i = 0; keys[i] != NULL; i++) {
		int l = bid_keycmp(p, keys[i], len);
		if (l > 0)
			return (l);
	}
	return (0);/* Unknown key */
}

/*
 * Test whether there is a set of mtree keywords.
 * Returns the number of keyword.
 * Returns -1 if we got incorrect sequence.
 * This function expects a set of "<space characters>keyword=value".
 * When "unset" is specified, expects a set of "<space characters>keyword".
 */
static int
bid_keyword_list(const char *p,  ssize_t len, int unset, int last_is_path)
{
	int l;
	int keycnt = 0;

	while (len > 0 && *p) {
		int blank = 0;

		/* Test whether there are blank characters in the line. */
		while (len >0 && (*p == ' ' || *p == '\t')) {
			++p;
			--len;
			blank = 1;
		}
		if (*p == '\n' || *p == '\r')
			break;
		if (p[0] == '\\' && (p[1] == '\n' || p[1] == '\r'))
			break;
		if (!blank && !last_is_path) /* No blank character. */
			return (-1);
		if (last_is_path && len == 0)
				return (keycnt);

		if (unset) {
			l = bid_keycmp(p, "all", len);
			if (l > 0)
				return (1);
		}
		/* Test whether there is a correct key in the line. */
		l = bid_keyword(p, len);
		if (l == 0)
			return (-1);/* Unknown keyword was found. */
		p += l;
		len -= l;
		keycnt++;

		/* Skip value */
		if (*p == '=') {
			int value = 0;
			++p;
			--len;
			while (len > 0 && *p != ' ' && *p != '\t') {
				++p;
				--len;
				value = 1;
			}
			/* A keyword should have a its value unless
			 * "/unset" operation. */ 
			if (!unset && value == 0)
				return (-1);
		}
	}
	return (keycnt);
}

static int
bid_entry(const char *p, ssize_t len, ssize_t nl, int *last_is_path)
{
	int f = 0;
	static const unsigned char safe_char[256] = {
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 00 - 0F */
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 10 - 1F */
		/* !"$%&'()*+,-./  EXCLUSION:( )(#) */
		0, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, /* 20 - 2F */
		/* 0123456789:;<>?  EXCLUSION:(=) */
		1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, /* 30 - 3F */
		/* @ABCDEFGHIJKLMNO */
		1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, /* 40 - 4F */
		/* PQRSTUVWXYZ[\]^_  */
		1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, /* 50 - 5F */
		/* `abcdefghijklmno */
		1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, /* 60 - 6F */
		/* pqrstuvwxyz{|}~ */
		1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, /* 70 - 7F */
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 80 - 8F */
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 90 - 9F */
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* A0 - AF */
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* B0 - BF */
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* C0 - CF */
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* D0 - DF */
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* E0 - EF */
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* F0 - FF */
	};
	ssize_t ll;
	const char *pp = p;
	const char * const pp_end = pp + len;

	*last_is_path = 0;
	/*
	 * Skip the path-name which is quoted.
	 */
	for (;pp < pp_end; ++pp) {
		if (!safe_char[*(const unsigned char *)pp]) {
			if (*pp != ' ' && *pp != '\t' && *pp != '\r'
			    && *pp != '\n')
				f = 0;
			break;
		}
		f = 1;
	}
	ll = pp_end - pp;

	/* If a path-name was not found at the first, try to check
	 * a mtree format(a.k.a form D) ``NetBSD's mtree -D'' creates,
	 * which places the path-name at the last. */
	if (f == 0) {
		const char *pb = p + len - nl;
		int name_len = 0;
		int slash;

		/* The form D accepts only a single line for an entry. */
		if (pb-2 >= p &&
		    pb[-1] == '\\' && (pb[-2] == ' ' || pb[-2] == '\t'))
			return (-1);
		if (pb-1 >= p && pb[-1] == '\\')
			return (-1);

		slash = 0;
		while (p <= --pb && *pb != ' ' && *pb != '\t') {
			if (!safe_char[*(const unsigned char *)pb])
				return (-1);
			name_len++;
			/* The pathname should have a slash in this
			 * format. */
			if (*pb == '/')
				slash = 1;
		}
		if (name_len == 0 || slash == 0)
			return (-1);
		/* If '/' is placed at the first in this field, this is not
		 * a valid filename. */
		if (pb[1] == '/')
			return (-1);
		ll = len - nl - name_len;
		pp = p;
		*last_is_path = 1;
	}

	return (bid_keyword_list(pp, ll, 0, *last_is_path));
}

#define MAX_BID_ENTRY	3

static int
mtree_bid(struct archive_read *a, int best_bid)
{
	const char *signature = "#mtree";
	const char *p;

	(void)best_bid; /* UNUSED */

	/* Now let's look at the actual header and see if it matches. */
	p = __archive_read_ahead(a, strlen(signature), NULL);
	if (p == NULL)
		return (-1);

	if (memcmp(p, signature, strlen(signature)) == 0)
		return (8 * (int)strlen(signature));

	/*
	 * There is not a mtree signature. Let's try to detect mtree format.
	 */
	return (detect_form(a, NULL));
}

static int
detect_form(struct archive_read *a, int *is_form_d)
{
	const char *p;
	ssize_t avail, ravail;
	ssize_t detected_bytes = 0, len, nl;
	int entry_cnt = 0, multiline = 0;
	int form_D = 0;/* The archive is generated by `NetBSD mtree -D'
			* (In this source we call it `form D') . */

	if (is_form_d != NULL)
		*is_form_d = 0;
	p = __archive_read_ahead(a, 1, &avail);
	if (p == NULL)
		return (-1);
	ravail = avail;
	for (;;) {
		len = next_line(a, &p, &avail, &ravail, &nl);
		/* The terminal character of the line should be
		 * a new line character, '\r\n' or '\n'. */
		if (len <= 0 || nl == 0)
			break;
		if (!multiline) {
			/* Leading whitespace is never significant,
			 * ignore it. */
			while (len > 0 && (*p == ' ' || *p == '\t')) {
				++p;
				--avail;
				--len;
			}
			/* Skip comment or empty line. */ 
			if (p[0] == '#' || p[0] == '\n' || p[0] == '\r') {
				p += len;
				avail -= len;
				continue;
			}
		} else {
			/* A continuance line; the terminal
			 * character of previous line was '\' character. */
			if (bid_keyword_list(p, len, 0, 0) <= 0)
				break;
			if (multiline == 1)
				detected_bytes += len;
			if (p[len-nl-1] != '\\') {
				if (multiline == 1 &&
				    ++entry_cnt >= MAX_BID_ENTRY)
					break;
				multiline = 0;
			}
			p += len;
			avail -= len;
			continue;
		}
		if (p[0] != '/') {
			int last_is_path, keywords;

			keywords = bid_entry(p, len, nl, &last_is_path);
			if (keywords >= 0) {
				detected_bytes += len;
				if (form_D == 0) {
					if (last_is_path)
						form_D = 1;
					else if (keywords > 0)
						/* This line is not `form D'. */
						form_D = -1;
				} else if (form_D == 1) {
					if (!last_is_path && keywords > 0)
						/* This this is not `form D'
						 * and We cannot accept mixed
						 * format. */
						break;
				}
				if (!last_is_path && p[len-nl-1] == '\\')
					/* This line continues. */
					multiline = 1;
				else {
					/* We've got plenty of correct lines
					 * to assume that this file is a mtree
					 * format. */
					if (++entry_cnt >= MAX_BID_ENTRY)
						break;
				}
			} else
				break;
		} else if (len > 4 && strncmp(p, "/set", 4) == 0) {
			if (bid_keyword_list(p+4, len-4, 0, 0) <= 0)
				break;
			/* This line continues. */
			if (p[len-nl-1] == '\\')
				multiline = 2;
		} else if (len > 6 && strncmp(p, "/unset", 6) == 0) {
			if (bid_keyword_list(p+6, len-6, 1, 0) <= 0)
				break;
			/* This line continues. */
			if (p[len-nl-1] == '\\')
				multiline = 2;
		} else
			break;

		/* Test next line. */
		p += len;
		avail -= len;
	}
	if (entry_cnt >= MAX_BID_ENTRY || (entry_cnt > 0 && len == 0)) {
		if (is_form_d != NULL) {
			if (form_D == 1)
				*is_form_d = 1;
		}
		return (32);
	}

	return (0);
}

/*
 * The extended mtree format permits multiple lines specifying
 * attributes for each file.  For those entries, only the last line
 * is actually used.  Practically speaking, that means we have
 * to read the entire mtree file into memory up front.
 *
 * The parsing is done in two steps.  First, it is decided if a line
 * changes the global defaults and if it is, processed accordingly.
 * Otherwise, the options of the line are merged with the current
 * global options.
 */
static int
add_option(struct archive_read *a, struct mtree_option **global,
    const char *value, size_t len)
{
	struct mtree_option *opt;

	if ((opt = malloc(sizeof(*opt))) == NULL) {
		archive_set_error(&a->archive, errno, "Can't allocate memory");
		return (ARCHIVE_FATAL);
	}
	if ((opt->value = malloc(len + 1)) == NULL) {
		free(opt);
		archive_set_error(&a->archive, errno, "Can't allocate memory");
		return (ARCHIVE_FATAL);
	}
	memcpy(opt->value, value, len);
	opt->value[len] = '\0';
	opt->next = *global;
	*global = opt;
	return (ARCHIVE_OK);
}

static void
remove_option(struct mtree_option **global, const char *value, size_t len)
{
	struct mtree_option *iter, *last;

	last = NULL;
	for (iter = *global; iter != NULL; last = iter, iter = iter->next) {
		if (strncmp(iter->value, value, len) == 0 &&
		    (iter->value[len] == '\0' ||
		     iter->value[len] == '='))
			break;
	}
	if (iter == NULL)
		return;
	if (last == NULL)
		*global = iter->next;
	else
		last->next = iter->next;

	free(iter->value);
	free(iter);
}

static int
process_global_set(struct archive_read *a,
    struct mtree_option **global, const char *line)
{
	const char *next, *eq;
	size_t len;
	int r;

	line += 4;
	for (;;) {
		next = line + strspn(line, " \t\r\n");
		if (*next == '\0')
			return (ARCHIVE_OK);
		line = next;
		next = line + strcspn(line, " \t\r\n");
		eq = strchr(line, '=');
		if (eq > next)
			len = next - line;
		else
			len = eq - line;

		remove_option(global, line, len);
		r = add_option(a, global, line, next - line);
		if (r != ARCHIVE_OK)
			return (r);
		line = next;
	}
}

static int
process_global_unset(struct archive_read *a,
    struct mtree_option **global, const char *line)
{
	const char *next;
	size_t len;

	line += 6;
	if (strchr(line, '=') != NULL) {
		archive_set_error(&a->archive, ARCHIVE_ERRNO_MISC,
		    "/unset shall not contain `='");
		return ARCHIVE_FATAL;
	}

	for (;;) {
		next = line + strspn(line, " \t\r\n");
		if (*next == '\0')
			return (ARCHIVE_OK);
		line = next;
		len = strcspn(line, " \t\r\n");

		if (len == 3 && strncmp(line, "all", 3) == 0) {
			free_options(*global);
			*global = NULL;
		} else {
			remove_option(global, line, len);
		}

		line += len;
	}
}

static int
process_add_entry(struct archive_read *a, struct mtree *mtree,
    struct mtree_option **global, const char *line, ssize_t line_len,
    struct mtree_entry **last_entry, int is_form_d)
{
	struct mtree_entry *entry;
	struct mtree_option *iter;
	const char *next, *eq, *name, *end;
	size_t name_len, len;
	int r, i;

	if ((entry = malloc(sizeof(*entry))) == NULL) {
		archive_set_error(&a->archive, errno, "Can't allocate memory");
		return (ARCHIVE_FATAL);
	}
	entry->next = NULL;
	entry->options = NULL;
	entry->name = NULL;
	entry->used = 0;
	entry->full = 0;

	/* Add this entry to list. */
	if (*last_entry == NULL)
		mtree->entries = entry;
	else
		(*last_entry)->next = entry;
	*last_entry = entry;

	if (is_form_d) {
		/* Filename is last item on line. */
		/* Adjust line_len to trim trailing whitespace */
		while (line_len > 0) {
			char last_character = line[line_len - 1];
			if (last_character == '\r'
			    || last_character == '\n'
			    || last_character == '\t'
			    || last_character == ' ') {
				line_len--;
			} else {
				break;
			}
		}
		/* Name starts after the last whitespace separator */
		name = line;
		for (i = 0; i < line_len; i++) {
			if (line[i] == '\r'
			    || line[i] == '\n'
			    || line[i] == '\t'
			    || line[i] == ' ') {
				name = line + i + 1;
			}
		}
		name_len = line + line_len - name;
		end = name;
	} else {
		/* Filename is first item on line */
		name_len = strcspn(line, " \t\r\n");
		name = line;
		line += name_len;
		end = line + line_len;
	}
	/* name/name_len is the name within the line. */
	/* line..end brackets the entire line except the name */

	if ((entry->name = malloc(name_len + 1)) == NULL) {
		archive_set_error(&a->archive, errno, "Can't allocate memory");
		return (ARCHIVE_FATAL);
	}

	memcpy(entry->name, name, name_len);
	entry->name[name_len] = '\0';
	parse_escapes(entry->name, entry);

	entry->next_dup = NULL;
	if (entry->full) {
		if (!__archive_rb_tree_insert_node(&mtree->rbtree, &entry->rbnode)) {
			struct mtree_entry *alt;
			alt = (struct mtree_entry *)__archive_rb_tree_find_node(
			    &mtree->rbtree, entry->name);
			while (alt->next_dup)
				alt = alt->next_dup;
			alt->next_dup = entry;
		}
	}

	for (iter = *global; iter != NULL; iter = iter->next) {
		r = add_option(a, &entry->options, iter->value,
		    strlen(iter->value));
		if (r != ARCHIVE_OK)
			return (r);
	}

	for (;;) {
		next = line + strspn(line, " \t\r\n");
		if (*next == '\0')
			return (ARCHIVE_OK);
		if (next >= end)
			return (ARCHIVE_OK);
		line = next;
		next = line + strcspn(line, " \t\r\n");
		eq = strchr(line, '=');
		if (eq == NULL || eq > next)
			len = next - line;
		else
			len = eq - line;

		remove_option(&entry->options, line, len);
		r = add_option(a, &entry->options, line, next - line);
		if (r != ARCHIVE_OK)
			return (r);
		line = next;
	}
}

static int
read_mtree(struct archive_read *a, struct mtree *mtree)
{
	ssize_t len;
	uintmax_t counter;
	char *p;
	struct mtree_option *global;
	struct mtree_entry *last_entry;
	int r, is_form_d;

	mtree->archive_format = ARCHIVE_FORMAT_MTREE;
	mtree->archive_format_name = "mtree";

	global = NULL;
	last_entry = NULL;

	(void)detect_form(a, &is_form_d);

	for (counter = 1; ; ++counter) {
		len = readline(a, mtree, &p, 65536);
		if (len == 0) {
			mtree->this_entry = mtree->entries;
			free_options(global);
			return (ARCHIVE_OK);
		}
		if (len < 0) {
			free_options(global);
			return ((int)len);
		}
		/* Leading whitespace is never significant, ignore it. */
		while (*p == ' ' || *p == '\t') {
			++p;
			--len;
		}
		/* Skip content lines and blank lines. */
		if (*p == '#')
			continue;
		if (*p == '\r' || *p == '\n' || *p == '\0')
			continue;
		if (*p != '/') {
			r = process_add_entry(a, mtree, &global, p, len,
			    &last_entry, is_form_d);
		} else if (len > 4 && strncmp(p, "/set", 4) == 0) {
			if (p[4] != ' ' && p[4] != '\t')
				break;
			r = process_global_set(a, &global, p);
		} else if (len > 6 && strncmp(p, "/unset", 6) == 0) {
			if (p[6] != ' ' && p[6] != '\t')
				break;
			r = process_global_unset(a, &global, p);
		} else
			break;

		if (r != ARCHIVE_OK) {
			free_options(global);
			return r;
		}
	}

	archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
	    "Can't parse line %ju", counter);
	free_options(global);
	return (ARCHIVE_FATAL);
}

/*
 * Read in the entire mtree file into memory on the first request.
 * Then use the next unused file to satisfy each header request.
 */
static int
read_header(struct archive_read *a, struct archive_entry *entry)
{
	struct mtree *mtree;
	char *p;
	int r, use_next;

	mtree = (struct mtree *)(a->format->data);

	if (mtree->fd >= 0) {
		close(mtree->fd);
		mtree->fd = -1;
	}

	if (mtree->entries == NULL) {
		mtree->resolver = archive_entry_linkresolver_new();
		if (mtree->resolver == NULL)
			return ARCHIVE_FATAL;
		archive_entry_linkresolver_set_strategy(mtree->resolver,
		    ARCHIVE_FORMAT_MTREE);
		r = read_mtree(a, mtree);
		if (r != ARCHIVE_OK)
			return (r);
	}

	a->archive.archive_format = mtree->archive_format;
	a->archive.archive_format_name = mtree->archive_format_name;

	for (;;) {
		if (mtree->this_entry == NULL)
			return (ARCHIVE_EOF);
		if (strcmp(mtree->this_entry->name, "..") == 0) {
			mtree->this_entry->used = 1;
			if (archive_strlen(&mtree->current_dir) > 0) {
				/* Roll back current path. */
				p = mtree->current_dir.s
				    + mtree->current_dir.length - 1;
				while (p >= mtree->current_dir.s && *p != '/')
					--p;
				if (p >= mtree->current_dir.s)
					--p;
				mtree->current_dir.length
				    = p - mtree->current_dir.s + 1;
			}
		}
		if (!mtree->this_entry->used) {
			use_next = 0;
			r = parse_file(a, entry, mtree, mtree->this_entry,
				&use_next);
			if (use_next == 0)
				return (r);
		}
		mtree->this_entry = mtree->this_entry->next;
	}
}

/*
 * A single file can have multiple lines contribute specifications.
 * Parse as many lines as necessary, then pull additional information
 * from a backing file on disk as necessary.
 */
static int
parse_file(struct archive_read *a, struct archive_entry *entry,
    struct mtree *mtree, struct mtree_entry *mentry, int *use_next)
{
	const char *path;
	struct stat st_storage, *st;
	struct mtree_entry *mp;
	struct archive_entry *sparse_entry;
	int r = ARCHIVE_OK, r1, parsed_kws;

	mentry->used = 1;

	/* Initialize reasonable defaults. */
	archive_entry_set_filetype(entry, AE_IFREG);
	archive_entry_set_size(entry, 0);
	archive_string_empty(&mtree->contents_name);

	/* Parse options from this line. */
	parsed_kws = 0;
	r = parse_line(a, entry, mtree, mentry, &parsed_kws);

	if (mentry->full) {
		archive_entry_copy_pathname(entry, mentry->name);
		/*
		 * "Full" entries are allowed to have multiple lines
		 * and those lines aren't required to be adjacent.  We
		 * don't support multiple lines for "relative" entries
		 * nor do we make any attempt to merge data from
		 * separate "relative" and "full" entries.  (Merging
		 * "relative" and "full" entries would require dealing
		 * with pathname canonicalization, which is a very
		 * tricky subject.)
		 */
		mp = (struct mtree_entry *)__archive_rb_tree_find_node(
		    &mtree->rbtree, mentry->name);
		for (; mp; mp = mp->next_dup) {
			if (mp->full && !mp->used) {
				/* Later lines override earlier ones. */
				mp->used = 1;
				r1 = parse_line(a, entry, mtree, mp, &parsed_kws);
				if (r1 < r)
					r = r1;
			}
		}
	} else {
		/*
		 * Relative entries require us to construct
		 * the full path and possibly update the
		 * current directory.
		 */
		size_t n = archive_strlen(&mtree->current_dir);
		if (n > 0)
			archive_strcat(&mtree->current_dir, "/");
		archive_strcat(&mtree->current_dir, mentry->name);
		archive_entry_copy_pathname(entry, mtree->current_dir.s);
		if (archive_entry_filetype(entry) != AE_IFDIR)
			mtree->current_dir.length = n;
	}

	if (mtree->checkfs) {
		/*
		 * Try to open and stat the file to get the real size
		 * and other file info.  It would be nice to avoid
		 * this here so that getting a listing of an mtree
		 * wouldn't require opening every referenced contents
		 * file.  But then we wouldn't know the actual
		 * contents size, so I don't see a really viable way
		 * around this.  (Also, we may want to someday pull
		 * other unspecified info from the contents file on
		 * disk.)
		 */
		mtree->fd = -1;
		if (archive_strlen(&mtree->contents_name) > 0)
			path = mtree->contents_name.s;
		else
			path = archive_entry_pathname(entry);

		if (archive_entry_filetype(entry) == AE_IFREG ||
				archive_entry_filetype(entry) == AE_IFDIR) {
			mtree->fd = open(path, O_RDONLY | O_BINARY | O_CLOEXEC);
			__archive_ensure_cloexec_flag(mtree->fd);
			if (mtree->fd == -1 &&
				(errno != ENOENT ||
				 archive_strlen(&mtree->contents_name) > 0)) {
				archive_set_error(&a->archive, errno,
						"Can't open %s", path);
				r = ARCHIVE_WARN;
			}
		}

		st = &st_storage;
		if (mtree->fd >= 0) {
			if (fstat(mtree->fd, st) == -1) {
				archive_set_error(&a->archive, errno,
						"Could not fstat %s", path);
				r = ARCHIVE_WARN;
				/* If we can't stat it, don't keep it open. */
				close(mtree->fd);
				mtree->fd = -1;
				st = NULL;
			}
		} else if (lstat(path, st) == -1) {
			st = NULL;
		}

		/*
		 * Check for a mismatch between the type in the specification
		 * and the type of the contents object on disk.
		 */
		if (st != NULL) {
			if (((st->st_mode & S_IFMT) == S_IFREG &&
			      archive_entry_filetype(entry) == AE_IFREG)
#ifdef S_IFLNK
			  ||((st->st_mode & S_IFMT) == S_IFLNK &&
			      archive_entry_filetype(entry) == AE_IFLNK)
#endif
#ifdef S_IFSOCK
			  ||((st->st_mode & S_IFSOCK) == S_IFSOCK &&
			      archive_entry_filetype(entry) == AE_IFSOCK)
#endif
#ifdef S_IFCHR
			  ||((st->st_mode & S_IFMT) == S_IFCHR &&
			      archive_entry_filetype(entry) == AE_IFCHR)
#endif
#ifdef S_IFBLK
			  ||((st->st_mode & S_IFMT) == S_IFBLK &&
			      archive_entry_filetype(entry) == AE_IFBLK)
#endif
			  ||((st->st_mode & S_IFMT) == S_IFDIR &&
			      archive_entry_filetype(entry) == AE_IFDIR)
#ifdef S_IFIFO
			  ||((st->st_mode & S_IFMT) == S_IFIFO &&
			      archive_entry_filetype(entry) == AE_IFIFO)
#endif
			) {
				/* Types match. */
			} else {
				/* Types don't match; bail out gracefully. */
				if (mtree->fd >= 0)
					close(mtree->fd);
				mtree->fd = -1;
				if (parsed_kws & MTREE_HAS_OPTIONAL) {
					/* It's not an error for an optional
					 * entry to not match disk. */
					*use_next = 1;
				} else if (r == ARCHIVE_OK) {
					archive_set_error(&a->archive,
					    ARCHIVE_ERRNO_MISC,
					    "mtree specification has different"
					    " type for %s",
					    archive_entry_pathname(entry));
					r = ARCHIVE_WARN;
				}
				return (r);
			}
		}

		/*
		 * If there is a contents file on disk, pick some of the
		 * metadata from that file.  For most of these, we only
		 * set it from the contents if it wasn't already parsed
		 * from the specification.
		 */
		if (st != NULL) {
			if (((parsed_kws & MTREE_HAS_DEVICE) == 0 ||
				(parsed_kws & MTREE_HAS_NOCHANGE) != 0) &&
				(archive_entry_filetype(entry) == AE_IFCHR ||
				 archive_entry_filetype(entry) == AE_IFBLK))
				archive_entry_set_rdev(entry, st->st_rdev);
			if ((parsed_kws & (MTREE_HAS_GID | MTREE_HAS_GNAME))
				== 0 ||
			    (parsed_kws & MTREE_HAS_NOCHANGE) != 0)
				archive_entry_set_gid(entry, st->st_gid);
			if ((parsed_kws & (MTREE_HAS_UID | MTREE_HAS_UNAME))
				== 0 ||
			    (parsed_kws & MTREE_HAS_NOCHANGE) != 0)
				archive_entry_set_uid(entry, st->st_uid);
			if ((parsed_kws & MTREE_HAS_MTIME) == 0 ||
			    (parsed_kws & MTREE_HAS_NOCHANGE) != 0) {
#if HAVE_STRUCT_STAT_ST_MTIMESPEC_TV_NSEC
				archive_entry_set_mtime(entry, st->st_mtime,
						st->st_mtimespec.tv_nsec);
#elif HAVE_STRUCT_STAT_ST_MTIM_TV_NSEC
				archive_entry_set_mtime(entry, st->st_mtime,
						st->st_mtim.tv_nsec);
#elif HAVE_STRUCT_STAT_ST_MTIME_N
				archive_entry_set_mtime(entry, st->st_mtime,
						st->st_mtime_n);
#elif HAVE_STRUCT_STAT_ST_UMTIME
				archive_entry_set_mtime(entry, st->st_mtime,
						st->st_umtime*1000);
#elif HAVE_STRUCT_STAT_ST_MTIME_USEC
				archive_entry_set_mtime(entry, st->st_mtime,
						st->st_mtime_usec*1000);
#else
				archive_entry_set_mtime(entry, st->st_mtime, 0);
#endif
			}
			if ((parsed_kws & MTREE_HAS_NLINK) == 0 ||
			    (parsed_kws & MTREE_HAS_NOCHANGE) != 0)
				archive_entry_set_nlink(entry, st->st_nlink);
			if ((parsed_kws & MTREE_HAS_PERM) == 0 ||
			    (parsed_kws & MTREE_HAS_NOCHANGE) != 0)
				archive_entry_set_perm(entry, st->st_mode);
			if ((parsed_kws & MTREE_HAS_SIZE) == 0 ||
			    (parsed_kws & MTREE_HAS_NOCHANGE) != 0)
				archive_entry_set_size(entry, st->st_size);
			archive_entry_set_ino(entry, st->st_ino);
			archive_entry_set_dev(entry, st->st_dev);

			archive_entry_linkify(mtree->resolver, &entry,
				&sparse_entry);
		} else if (parsed_kws & MTREE_HAS_OPTIONAL) {
			/*
			 * Couldn't open the entry, stat it or the on-disk type
			 * didn't match.  If this entry is optional, just
			 * ignore it and read the next header entry.
			 */
			*use_next = 1;
			return ARCHIVE_OK;
		}
	}

	mtree->cur_size = archive_entry_size(entry);
	mtree->offset = 0;

	return r;
}

/*
 * Each line contains a sequence of keywords.
 */
static int
parse_line(struct archive_read *a, struct archive_entry *entry,
    struct mtree *mtree, struct mtree_entry *mp, int *parsed_kws)
{
	struct mtree_option *iter;
	int r = ARCHIVE_OK, r1;

	for (iter = mp->options; iter != NULL; iter = iter->next) {
		r1 = parse_keyword(a, mtree, entry, iter, parsed_kws);
		if (r1 < r)
			r = r1;
	}
	if (r == ARCHIVE_OK && (*parsed_kws & MTREE_HAS_TYPE) == 0) {
		archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
		    "Missing type keyword in mtree specification");
		return (ARCHIVE_WARN);
	}
	return (r);
}

/*
 * Device entries have one of the following forms:
 *  - raw dev_t
 *  - format,major,minor[,subdevice]
 * When parsing succeeded, `pdev' will contain the appropriate dev_t value.
 */

/* strsep() is not in C90, but strcspn() is. */
/* Taken from http://unixpapa.com/incnote/string.html */
static char *
la_strsep(char **sp, const char *sep)
{
	char *p, *s;
	if (sp == NULL || *sp == NULL || **sp == '\0')
		return(NULL);
	s = *sp;
	p = s + strcspn(s, sep);
	if (*p != '\0')
		*p++ = '\0';
	*sp = p;
	return(s);
}

static int
parse_device(dev_t *pdev, struct archive *a, char *val)
{
#define MAX_PACK_ARGS 3
	unsigned long numbers[MAX_PACK_ARGS];
	char *p, *dev;
	int argc;
	pack_t *pack;
	dev_t result;
	const char *error = NULL;

	memset(pdev, 0, sizeof(*pdev));
	if ((dev = strchr(val, ',')) != NULL) {
		/*
		 * Device's major/minor are given in a specified format.
		 * Decode and pack it accordingly.
		 */
		*dev++ = '\0';
		if ((pack = pack_find(val)) == NULL) {
			archive_set_error(a, ARCHIVE_ERRNO_FILE_FORMAT,
			    "Unknown format `%s'", val);
			return ARCHIVE_WARN;
		}
		argc = 0;
		while ((p = la_strsep(&dev, ",")) != NULL) {
			if (*p == '\0') {
				archive_set_error(a, ARCHIVE_ERRNO_FILE_FORMAT,
				    "Missing number");
				return ARCHIVE_WARN;
			}
			if (argc >= MAX_PACK_ARGS) {
				archive_set_error(a, ARCHIVE_ERRNO_FILE_FORMAT,
				    "Too many arguments");
				return ARCHIVE_WARN;
			}
			numbers[argc++] = (unsigned long)mtree_atol(&p, 0);
		}
		if (argc < 2) {
			archive_set_error(a, ARCHIVE_ERRNO_FILE_FORMAT,
			    "Not enough arguments");
			return ARCHIVE_WARN;
		}
		result = (*pack)(argc, numbers, &error);
		if (error != NULL) {
			archive_set_error(a, ARCHIVE_ERRNO_FILE_FORMAT,
			    "%s", error);
			return ARCHIVE_WARN;
		}
	} else {
		/* file system raw value. */
		result = (dev_t)mtree_atol(&val, 0);
	}
	*pdev = result;
	return ARCHIVE_OK;
#undef MAX_PACK_ARGS
}

/*
 * Parse a single keyword and its value.
 */
static int
parse_keyword(struct archive_read *a, struct mtree *mtree,
    struct archive_entry *entry, struct mtree_option *opt, int *parsed_kws)
{
	char *val, *key;

	key = opt->value;

	if (*key == '\0')
		return (ARCHIVE_OK);

	if (strcmp(key, "nochange") == 0) {
		*parsed_kws |= MTREE_HAS_NOCHANGE;
		return (ARCHIVE_OK);
	}
	if (strcmp(key, "optional") == 0) {
		*parsed_kws |= MTREE_HAS_OPTIONAL;
		return (ARCHIVE_OK);
	}
	if (strcmp(key, "ignore") == 0) {
		/*
		 * The mtree processing is not recursive, so
		 * recursion will only happen for explicitly listed
		 * entries.
		 */
		return (ARCHIVE_OK);
	}

	val = strchr(key, '=');
	if (val == NULL) {
		archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
		    "Malformed attribute \"%s\" (%d)", key, key[0]);
		return (ARCHIVE_WARN);
	}

	*val = '\0';
	++val;

	switch (key[0]) {
	case 'c':
		if (strcmp(key, "content") == 0
		    || strcmp(key, "contents") == 0) {
			parse_escapes(val, NULL);
			archive_strcpy(&mtree->contents_name, val);
			break;
		}
		if (strcmp(key, "cksum") == 0)
			break;
		__LA_FALLTHROUGH;
	case 'd':
		if (strcmp(key, "device") == 0) {
			/* stat(2) st_rdev field, e.g. the major/minor IDs
			 * of a char/block special file */
			int r;
			dev_t dev;

			*parsed_kws |= MTREE_HAS_DEVICE;
			r = parse_device(&dev, &a->archive, val);
			if (r == ARCHIVE_OK)
				archive_entry_set_rdev(entry, dev);
			return r;
		}
		__LA_FALLTHROUGH;
	case 'f':
		if (strcmp(key, "flags") == 0) {
			*parsed_kws |= MTREE_HAS_FFLAGS;
			archive_entry_copy_fflags_text(entry, val);
			break;
		}
		__LA_FALLTHROUGH;
	case 'g':
		if (strcmp(key, "gid") == 0) {
			*parsed_kws |= MTREE_HAS_GID;
			archive_entry_set_gid(entry, mtree_atol(&val, 10));
			break;
		}
		if (strcmp(key, "gname") == 0) {
			*parsed_kws |= MTREE_HAS_GNAME;
			archive_entry_copy_gname(entry, val);
			break;
		}
		__LA_FALLTHROUGH;
	case 'i':
		if (strcmp(key, "inode") == 0) {
			archive_entry_set_ino(entry, mtree_atol(&val, 10));
			break;
		}
		__LA_FALLTHROUGH;
	case 'l':
		if (strcmp(key, "link") == 0) {
			archive_entry_copy_symlink(entry, val);
			break;
		}
		__LA_FALLTHROUGH;
	case 'm':
		if (strcmp(key, "md5") == 0 || strcmp(key, "md5digest") == 0)
			break;
		if (strcmp(key, "mode") == 0) {
			if (val[0] >= '0' && val[0] <= '7') {
				*parsed_kws |= MTREE_HAS_PERM;
				archive_entry_set_perm(entry,
				    (mode_t)mtree_atol(&val, 8));
			} else {
				archive_set_error(&a->archive,
				    ARCHIVE_ERRNO_FILE_FORMAT,
				    "Symbolic or non-octal mode \"%s\" unsupported", val);
				return ARCHIVE_WARN;
			}
			break;
		}
		__LA_FALLTHROUGH;
	case 'n':
		if (strcmp(key, "nlink") == 0) {
			*parsed_kws |= MTREE_HAS_NLINK;
			archive_entry_set_nlink(entry,
				(unsigned int)mtree_atol(&val, 10));
			break;
		}
		__LA_FALLTHROUGH;
	case 'r':
		if (strcmp(key, "resdevice") == 0) {
			/* stat(2) st_dev field, e.g. the device ID where the
			 * inode resides */
			int r;
			dev_t dev;

			r = parse_device(&dev, &a->archive, val);
			if (r == ARCHIVE_OK)
				archive_entry_set_dev(entry, dev);
			return r;
		}
		if (strcmp(key, "rmd160") == 0 ||
		    strcmp(key, "rmd160digest") == 0)
			break;
		__LA_FALLTHROUGH;
	case 's':
		if (strcmp(key, "sha1") == 0 || strcmp(key, "sha1digest") == 0)
			break;
		if (strcmp(key, "sha256") == 0 ||
		    strcmp(key, "sha256digest") == 0)
			break;
		if (strcmp(key, "sha384") == 0 ||
		    strcmp(key, "sha384digest") == 0)
			break;
		if (strcmp(key, "sha512") == 0 ||
		    strcmp(key, "sha512digest") == 0)
			break;
		if (strcmp(key, "size") == 0) {
			archive_entry_set_size(entry, mtree_atol(&val, 10));
			break;
		}
		__LA_FALLTHROUGH;
	case 't':
		if (strcmp(key, "tags") == 0) {
			/*
			 * Comma delimited list of tags.
			 * Ignore the tags for now, but the interface
			 * should be extended to allow inclusion/exclusion.
			 */
			break;
		}
		if (strcmp(key, "time") == 0) {
			int64_t m;
			int64_t my_time_t_max = get_time_t_max();
			int64_t my_time_t_min = get_time_t_min();
			long ns = 0;

			*parsed_kws |= MTREE_HAS_MTIME;
			m = mtree_atol(&val, 10);
			/* Replicate an old mtree bug:
			 * 123456789.1 represents 123456789
			 * seconds and 1 nanosecond. */
			if (*val == '.') {
				++val;
				ns = (long)mtree_atol(&val, 10);
				if (ns < 0)
					ns = 0;
				else if (ns > 999999999)
					ns = 999999999;
			}
			if (m > my_time_t_max)
				m = my_time_t_max;
			else if (m < my_time_t_min)
				m = my_time_t_min;
			archive_entry_set_mtime(entry, (time_t)m, ns);
			break;
		}
		if (strcmp(key, "type") == 0) {
			switch (val[0]) {
			case 'b':
				if (strcmp(val, "block") == 0) {
					archive_entry_set_filetype(entry, AE_IFBLK);
					break;
				}
				__LA_FALLTHROUGH;
			case 'c':
				if (strcmp(val, "char") == 0) {
					archive_entry_set_filetype(entry,
						AE_IFCHR);
					break;
				}
				__LA_FALLTHROUGH;
			case 'd':
				if (strcmp(val, "dir") == 0) {
					archive_entry_set_filetype(entry,
						AE_IFDIR);
					break;
				}
				__LA_FALLTHROUGH;
			case 'f':
				if (strcmp(val, "fifo") == 0) {
					archive_entry_set_filetype(entry,
						AE_IFIFO);
					break;
				}
				if (strcmp(val, "file") == 0) {
					archive_entry_set_filetype(entry,
						AE_IFREG);
					break;
				}
				__LA_FALLTHROUGH;
			case 'l':
				if (strcmp(val, "link") == 0) {
					archive_entry_set_filetype(entry,
						AE_IFLNK);
					break;
				}
				__LA_FALLTHROUGH;
			default:
				archive_set_error(&a->archive,
				    ARCHIVE_ERRNO_FILE_FORMAT,
				    "Unrecognized file type \"%s\"; "
				    "assuming \"file\"", val);
				archive_entry_set_filetype(entry, AE_IFREG);
				return (ARCHIVE_WARN);
			}
			*parsed_kws |= MTREE_HAS_TYPE;
			break;
		}
		__LA_FALLTHROUGH;
	case 'u':
		if (strcmp(key, "uid") == 0) {
			*parsed_kws |= MTREE_HAS_UID;
			archive_entry_set_uid(entry, mtree_atol(&val, 10));
			break;
		}
		if (strcmp(key, "uname") == 0) {
			*parsed_kws |= MTREE_HAS_UNAME;
			archive_entry_copy_uname(entry, val);
			break;
		}
		__LA_FALLTHROUGH;
	default:
		archive_set_error(&a->archive, ARCHIVE_ERRNO_FILE_FORMAT,
		    "Unrecognized key %s=%s", key, val);
		return (ARCHIVE_WARN);
	}
	return (ARCHIVE_OK);
}

static int
read_data(struct archive_read *a, const void **buff, size_t *size,
    int64_t *offset)
{
	size_t bytes_to_read;
	ssize_t bytes_read;
	struct mtree *mtree;

	mtree = (struct mtree *)(a->format->data);
	if (mtree->fd < 0) {
		*buff = NULL;
		*offset = 0;
		*size = 0;
		return (ARCHIVE_EOF);
	}
	if (mtree->buff == NULL) {
		mtree->buffsize = 64 * 1024;
		mtree->buff = malloc(mtree->buffsize);
		if (mtree->buff == NULL) {
			archive_set_error(&a->archive, ENOMEM,
			    "Can't allocate memory");
			return (ARCHIVE_FATAL);
		}
	}

	*buff = mtree->buff;
	*offset = mtree->offset;
	if ((int64_t)mtree->buffsize > mtree->cur_size - mtree->offset)
		bytes_to_read = (size_t)(mtree->cur_size - mtree->offset);
	else
		bytes_to_read = mtree->buffsize;
	bytes_read = read(mtree->fd, mtree->buff, bytes_to_read);
	if (bytes_read < 0) {
		archive_set_error(&a->archive, errno, "Can't read");
		return (ARCHIVE_WARN);
	}
	if (bytes_read == 0) {
		*size = 0;
		return (ARCHIVE_EOF);
	}
	mtree->offset += bytes_read;
	*size = bytes_read;
	return (ARCHIVE_OK);
}

/* Skip does nothing except possibly close the contents file. */
static int
skip(struct archive_read *a)
{
	struct mtree *mtree;

	mtree = (struct mtree *)(a->format->data);
	if (mtree->fd >= 0) {
		close(mtree->fd);
		mtree->fd = -1;
	}
	return (ARCHIVE_OK);
}

/*
 * Since parsing backslash sequences always makes strings shorter,
 * we can always do this conversion in-place.
 */
static void
parse_escapes(char *src, struct mtree_entry *mentry)
{
	char *dest = src;
	char c;

	if (mentry != NULL && strcmp(src, ".") == 0)
		mentry->full = 1;

	while (*src != '\0') {
		c = *src++;
		if (c == '/' && mentry != NULL)
			mentry->full = 1;
		if (c == '\\') {
			switch (src[0]) {
			case '0':
				if (src[1] < '0' || src[1] > '7') {
					c = 0;
					++src;
					break;
				}
				/* FALLTHROUGH */
			case '1':
			case '2':
			case '3':
				if (src[1] >= '0' && src[1] <= '7' &&
				    src[2] >= '0' && src[2] <= '7') {
					c = (src[0] - '0') << 6;
					c |= (src[1] - '0') << 3;
					c |= (src[2] - '0');
					src += 3;
				}
				break;
			case 'a':
				c = '\a';
				++src;
				break;
			case 'b':
				c = '\b';
				++src;
				break;
			case 'f':
				c = '\f';
				++src;
				break;
			case 'n':
				c = '\n';
				++src;
				break;
			case 'r':
				c = '\r';
				++src;
				break;
			case 's':
				c = ' ';
				++src;
				break;
			case 't':
				c = '\t';
				++src;
				break;
			case 'v':
				c = '\v';
				++src;
				break;
			case '\\':
				c = '\\';
				++src;
				break;
			}
		}
		*dest++ = c;
	}
	*dest = '\0';
}

/* Parse a hex digit. */
static int
parsedigit(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	else if (c >= 'a' && c <= 'f')
		return c - 'a';
	else if (c >= 'A' && c <= 'F')
		return c - 'A';
	else
		return -1;
}

/*
 * Note that this implementation does not (and should not!) obey
 * locale settings; you cannot simply substitute strtol here, since
 * it does obey locale.
 */
static int64_t
mtree_atol(char **p, int base)
{
	int64_t l, limit;
	int digit, last_digit_limit;

	if (base == 0) {
		if (**p != '0')
			base = 10;
		else if ((*p)[1] == 'x' || (*p)[1] == 'X') {
			*p += 2;
			base = 16;
		} else {
			base = 8;
		}
	}

	if (**p == '-') {
		limit = INT64_MIN / base;
		last_digit_limit = INT64_MIN % base;
		++(*p);

		l = 0;
		digit = parsedigit(**p);
		while (digit >= 0 && digit < base) {
			if (l < limit || (l == limit && digit > last_digit_limit))
				return INT64_MIN;
			l = (l * base) - digit;
			digit = parsedigit(*++(*p));
		}
		return l;
	} else {
		limit = INT64_MAX / base;
		last_digit_limit = INT64_MAX % base;

		l = 0;
		digit = parsedigit(**p);
		while (digit >= 0 && digit < base) {
			if (l > limit || (l == limit && digit > last_digit_limit))
				return INT64_MAX;
			l = (l * base) + digit;
			digit = parsedigit(*++(*p));
		}
		return l;
	}
}

/*
 * Returns length of line (including trailing newline)
 * or negative on error.  'start' argument is updated to
 * point to first character of line.
 */
static ssize_t
readline(struct archive_read *a, struct mtree *mtree, char **start,
    ssize_t limit)
{
	ssize_t bytes_read;
	ssize_t total_size = 0;
	ssize_t find_off = 0;
	const void *t;
	void *nl;
	char *u;

	/* Accumulate line in a line buffer. */
	for (;;) {
		/* Read some more. */
		t = __archive_read_ahead(a, 1, &bytes_read);
		if (t == NULL)
			return (0);
		if (bytes_read < 0)
			return (ARCHIVE_FATAL);
		nl = memchr(t, '\n', bytes_read);
		/* If we found '\n', trim the read to end exactly there. */
		if (nl != NULL) {
			bytes_read = ((const char *)nl) - ((const char *)t) + 1;
		}
		if (total_size + bytes_read + 1 > limit) {
			archive_set_error(&a->archive,
			    ARCHIVE_ERRNO_FILE_FORMAT,
			    "Line too long");
			return (ARCHIVE_FATAL);
		}
		if (archive_string_ensure(&mtree->line,
			total_size + bytes_read + 1) == NULL) {
			archive_set_error(&a->archive, ENOMEM,
			    "Can't allocate working buffer");
			return (ARCHIVE_FATAL);
		}
		/* Append new bytes to string. */
		memcpy(mtree->line.s + total_size, t, bytes_read);
		__archive_read_consume(a, bytes_read);
		total_size += bytes_read;
		mtree->line.s[total_size] = '\0';

		for (u = mtree->line.s + find_off; *u; ++u) {
			if (u[0] == '\n') {
				/* Ends with unescaped newline. */
				*start = mtree->line.s;
				return total_size;
			} else if (u[0] == '#') {
				/* Ends with comment sequence #...\n */
				if (nl == NULL) {
					/* But we've not found the \n yet */
					break;
				}
			} else if (u[0] == '\\') {
				if (u[1] == '\n') {
					/* Trim escaped newline. */
					total_size -= 2;
					mtree->line.s[total_size] = '\0';
					break;
				} else if (u[1] != '\0') {
					/* Skip the two-char escape sequence */
					++u;
				}
			}
		}
		find_off = u - mtree->line.s;
	}
}
