/*-
 * Copyright (c) 2003-2010 Tim Kientzle
 * Copyright (c) 2016 Martin Matuska
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

#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#ifdef HAVE_LIMITS_H
#include <limits.h>
#endif
#ifdef HAVE_WCHAR_H
#include <wchar.h>
#endif

#include "archive_acl_private.h"
#include "archive_entry.h"
#include "archive_private.h"

#undef max
#define	max(a, b)	((a)>(b)?(a):(b))

#ifndef HAVE_WMEMCMP
/* Good enough for simple equality testing, but not for sorting. */
#define wmemcmp(a,b,i)  memcmp((a), (b), (i) * sizeof(wchar_t))
#endif

static int	acl_special(struct archive_acl *acl,
		    int type, int permset, int tag);
static struct archive_acl_entry *acl_new_entry(struct archive_acl *acl,
		    int type, int permset, int tag, int id);
static int	archive_acl_add_entry_len_l(struct archive_acl *acl,
		    int type, int permset, int tag, int id, const char *name,
		    size_t len, struct archive_string_conv *sc);
static int	archive_acl_text_want_type(struct archive_acl *acl, int flags);
static ssize_t	archive_acl_text_len(struct archive_acl *acl, int want_type,
		    int flags, int wide, struct archive *a,
		    struct archive_string_conv *sc);
static int	isint_w(const wchar_t *start, const wchar_t *end, int *result);
static int	ismode_w(const wchar_t *start, const wchar_t *end, int *result);
static int	is_nfs4_flags_w(const wchar_t *start, const wchar_t *end,
		    int *result);
static int	is_nfs4_perms_w(const wchar_t *start, const wchar_t *end,
		    int *result);
static void	next_field_w(const wchar_t **wp, const wchar_t **start,
		    const wchar_t **end, wchar_t *sep);
static void	append_entry_w(wchar_t **wp, const wchar_t *prefix, int type,
		    int tag, int flags, const wchar_t *wname, int perm, int id);
static void	append_id_w(wchar_t **wp, int id);
static int	isint(const char *start, const char *end, int *result);
static int	ismode(const char *start, const char *end, int *result);
static int	is_nfs4_flags(const char *start, const char *end,
		    int *result);
static int	is_nfs4_perms(const char *start, const char *end,
		    int *result);
static void	next_field(const char **p, const char **start,
		    const char **end, char *sep);
static void	append_entry(char **p, const char *prefix, int type,
		    int tag, int flags, const char *name, int perm, int id);
static void	append_id(char **p, int id);

static const struct {
	const int perm;
	const char c;
	const wchar_t wc;
} nfsv4_acl_perm_map[] = {
	{ ARCHIVE_ENTRY_ACL_READ_DATA | ARCHIVE_ENTRY_ACL_LIST_DIRECTORY, 'r',
	    L'r' },
	{ ARCHIVE_ENTRY_ACL_WRITE_DATA | ARCHIVE_ENTRY_ACL_ADD_FILE, 'w',
	    L'w' },
	{ ARCHIVE_ENTRY_ACL_EXECUTE, 'x', L'x' },
	{ ARCHIVE_ENTRY_ACL_APPEND_DATA | ARCHIVE_ENTRY_ACL_ADD_SUBDIRECTORY,
	    'p', L'p' },
	{ ARCHIVE_ENTRY_ACL_DELETE, 'd', L'd' },
	{ ARCHIVE_ENTRY_ACL_DELETE_CHILD, 'D', L'D' },
	{ ARCHIVE_ENTRY_ACL_READ_ATTRIBUTES, 'a', L'a' },
	{ ARCHIVE_ENTRY_ACL_WRITE_ATTRIBUTES, 'A', L'A' },
	{ ARCHIVE_ENTRY_ACL_READ_NAMED_ATTRS, 'R', L'R' },
	{ ARCHIVE_ENTRY_ACL_WRITE_NAMED_ATTRS, 'W', L'W' },
	{ ARCHIVE_ENTRY_ACL_READ_ACL, 'c', L'c' },
	{ ARCHIVE_ENTRY_ACL_WRITE_ACL, 'C', L'C' },
	{ ARCHIVE_ENTRY_ACL_WRITE_OWNER, 'o', L'o' },
	{ ARCHIVE_ENTRY_ACL_SYNCHRONIZE, 's', L's' }
};

static const int nfsv4_acl_perm_map_size = (int)(sizeof(nfsv4_acl_perm_map) /
    sizeof(nfsv4_acl_perm_map[0]));

static const struct {
	const int perm;
	const char c;
	const wchar_t wc;
} nfsv4_acl_flag_map[] = {
	{ ARCHIVE_ENTRY_ACL_ENTRY_FILE_INHERIT, 'f', L'f' },
	{ ARCHIVE_ENTRY_ACL_ENTRY_DIRECTORY_INHERIT, 'd', L'd' },
	{ ARCHIVE_ENTRY_ACL_ENTRY_INHERIT_ONLY, 'i', L'i' },
	{ ARCHIVE_ENTRY_ACL_ENTRY_NO_PROPAGATE_INHERIT, 'n', L'n' },
	{ ARCHIVE_ENTRY_ACL_ENTRY_SUCCESSFUL_ACCESS, 'S', L'S' },
	{ ARCHIVE_ENTRY_ACL_ENTRY_FAILED_ACCESS, 'F', L'F' },
	{ ARCHIVE_ENTRY_ACL_ENTRY_INHERITED, 'I', L'I' }
};

static const int nfsv4_acl_flag_map_size = (int)(sizeof(nfsv4_acl_flag_map) /
    sizeof(nfsv4_acl_flag_map[0]));

void
archive_acl_clear(struct archive_acl *acl)
{
	struct archive_acl_entry *ap;

	while (acl->acl_head != NULL) {
		ap = acl->acl_head->next;
		archive_mstring_clean(&acl->acl_head->name);
		free(acl->acl_head);
		acl->acl_head = ap;
	}
	free(acl->acl_text_w);
	acl->acl_text_w = NULL;
	free(acl->acl_text);
	acl->acl_text = NULL;
	acl->acl_p = NULL;
	acl->acl_types = 0;
	acl->acl_state = 0; /* Not counting. */
}

void
archive_acl_copy(struct archive_acl *dest, struct archive_acl *src)
{
	struct archive_acl_entry *ap, *ap2;

	archive_acl_clear(dest);

	dest->mode = src->mode;
	ap = src->acl_head;
	while (ap != NULL) {
		ap2 = acl_new_entry(dest,
		    ap->type, ap->permset, ap->tag, ap->id);
		if (ap2 != NULL)
			archive_mstring_copy(&ap2->name, &ap->name);
		ap = ap->next;
	}
}

int
archive_acl_add_entry(struct archive_acl *acl,
    int type, int permset, int tag, int id, const char *name)
{
	struct archive_acl_entry *ap;

	if (acl_special(acl, type, permset, tag) == 0)
		return ARCHIVE_OK;
	ap = acl_new_entry(acl, type, permset, tag, id);
	if (ap == NULL) {
		/* XXX Error XXX */
		return ARCHIVE_FAILED;
	}
	if (name != NULL  &&  *name != '\0')
		archive_mstring_copy_mbs(&ap->name, name);
	else
		archive_mstring_clean(&ap->name);
	return ARCHIVE_OK;
}

int
archive_acl_add_entry_w_len(struct archive_acl *acl,
    int type, int permset, int tag, int id, const wchar_t *name, size_t len)
{
	struct archive_acl_entry *ap;

	if (acl_special(acl, type, permset, tag) == 0)
		return ARCHIVE_OK;
	ap = acl_new_entry(acl, type, permset, tag, id);
	if (ap == NULL) {
		/* XXX Error XXX */
		return ARCHIVE_FAILED;
	}
	if (name != NULL  &&  *name != L'\0' && len > 0)
		archive_mstring_copy_wcs_len(&ap->name, name, len);
	else
		archive_mstring_clean(&ap->name);
	return ARCHIVE_OK;
}

static int
archive_acl_add_entry_len_l(struct archive_acl *acl,
    int type, int permset, int tag, int id, const char *name, size_t len,
    struct archive_string_conv *sc)
{
	struct archive_acl_entry *ap;
	int r;

	if (acl_special(acl, type, permset, tag) == 0)
		return ARCHIVE_OK;
	ap = acl_new_entry(acl, type, permset, tag, id);
	if (ap == NULL) {
		/* XXX Error XXX */
		return ARCHIVE_FAILED;
	}
	if (name != NULL  &&  *name != '\0' && len > 0) {
		r = archive_mstring_copy_mbs_len_l(&ap->name, name, len, sc);
	} else {
		r = 0;
		archive_mstring_clean(&ap->name);
	}
	if (r == 0)
		return (ARCHIVE_OK);
	else if (errno == ENOMEM)
		return (ARCHIVE_FATAL);
	else
		return (ARCHIVE_WARN);
}

/*
 * If this ACL entry is part of the standard POSIX permissions set,
 * store the permissions in the stat structure and return zero.
 */
static int
acl_special(struct archive_acl *acl, int type, int permset, int tag)
{
	if (type == ARCHIVE_ENTRY_ACL_TYPE_ACCESS
	    && ((permset & ~007) == 0)) {
		switch (tag) {
		case ARCHIVE_ENTRY_ACL_USER_OBJ:
			acl->mode &= ~0700;
			acl->mode |= (permset & 7) << 6;
			return (0);
		case ARCHIVE_ENTRY_ACL_GROUP_OBJ:
			acl->mode &= ~0070;
			acl->mode |= (permset & 7) << 3;
			return (0);
		case ARCHIVE_ENTRY_ACL_OTHER:
			acl->mode &= ~0007;
			acl->mode |= permset & 7;
			return (0);
		}
	}
	return (1);
}

/*
 * Allocate and populate a new ACL entry with everything but the
 * name.
 */
static struct archive_acl_entry *
acl_new_entry(struct archive_acl *acl,
    int type, int permset, int tag, int id)
{
	struct archive_acl_entry *ap, *aq;

	/* Type argument must be a valid NFS4 or POSIX.1e type.
	 * The type must agree with anything already set and
	 * the permset must be compatible. */
	if (type & ARCHIVE_ENTRY_ACL_TYPE_NFS4) {
		if (acl->acl_types & ~ARCHIVE_ENTRY_ACL_TYPE_NFS4) {
			return (NULL);
		}
		if (permset &
		    ~(ARCHIVE_ENTRY_ACL_PERMS_NFS4
			| ARCHIVE_ENTRY_ACL_INHERITANCE_NFS4)) {
			return (NULL);
		}
	} else	if (type & ARCHIVE_ENTRY_ACL_TYPE_POSIX1E) {
		if (acl->acl_types & ~ARCHIVE_ENTRY_ACL_TYPE_POSIX1E) {
			return (NULL);
		}
		if (permset & ~ARCHIVE_ENTRY_ACL_PERMS_POSIX1E) {
			return (NULL);
		}
	} else {
		return (NULL);
	}

	/* Verify the tag is valid and compatible with NFS4 or POSIX.1e. */
	switch (tag) {
	case ARCHIVE_ENTRY_ACL_USER:
	case ARCHIVE_ENTRY_ACL_USER_OBJ:
	case ARCHIVE_ENTRY_ACL_GROUP:
	case ARCHIVE_ENTRY_ACL_GROUP_OBJ:
		/* Tags valid in both NFS4 and POSIX.1e */
		break;
	case ARCHIVE_ENTRY_ACL_MASK:
	case ARCHIVE_ENTRY_ACL_OTHER:
		/* Tags valid only in POSIX.1e. */
		if (type & ~ARCHIVE_ENTRY_ACL_TYPE_POSIX1E) {
			return (NULL);
		}
		break;
	case ARCHIVE_ENTRY_ACL_EVERYONE:
		/* Tags valid only in NFS4. */
		if (type & ~ARCHIVE_ENTRY_ACL_TYPE_NFS4) {
			return (NULL);
		}
		break;
	default:
		/* No other values are valid. */
		return (NULL);
	}

	free(acl->acl_text_w);
	acl->acl_text_w = NULL;
	free(acl->acl_text);
	acl->acl_text = NULL;

	/*
	 * If there's a matching entry already in the list, overwrite it.
	 * NFSv4 entries may be repeated and are not overwritten.
	 *
	 * TODO: compare names of no id is provided (needs more rework)
	 */
	ap = acl->acl_head;
	aq = NULL;
	while (ap != NULL) {
		if (((type & ARCHIVE_ENTRY_ACL_TYPE_NFS4) == 0) &&
		    ap->type == type && ap->tag == tag && ap->id == id) {
			if (id != -1 || (tag != ARCHIVE_ENTRY_ACL_USER &&
			    tag != ARCHIVE_ENTRY_ACL_GROUP)) {
				ap->permset = permset;
				return (ap);
			}
		}
		aq = ap;
		ap = ap->next;
	}

	/* Add a new entry to the end of the list. */
	ap = (struct archive_acl_entry *)calloc(1, sizeof(*ap));
	if (ap == NULL)
		return (NULL);
	if (aq == NULL)
		acl->acl_head = ap;
	else
		aq->next = ap;
	ap->type = type;
	ap->tag = tag;
	ap->id = id;
	ap->permset = permset;
	acl->acl_types |= type;
	return (ap);
}

/*
 * Return a count of entries matching "want_type".
 */
int
archive_acl_count(struct archive_acl *acl, int want_type)
{
	int count;
	struct archive_acl_entry *ap;

	count = 0;
	ap = acl->acl_head;
	while (ap != NULL) {
		if ((ap->type & want_type) != 0)
			count++;
		ap = ap->next;
	}

	if (count > 0 && ((want_type & ARCHIVE_ENTRY_ACL_TYPE_ACCESS) != 0))
		count += 3;
	return (count);
}

/*
 * Return a bitmask of stored ACL types in an ACL list
 */
int
archive_acl_types(struct archive_acl *acl)
{
	return (acl->acl_types);
}

/*
 * Prepare for reading entries from the ACL data.  Returns a count
 * of entries matching "want_type", or zero if there are no
 * non-extended ACL entries of that type.
 */
int
archive_acl_reset(struct archive_acl *acl, int want_type)
{
	int count, cutoff;

	count = archive_acl_count(acl, want_type);

	/*
	 * If the only entries are the three standard ones,
	 * then don't return any ACL data.  (In this case,
	 * client can just use chmod(2) to set permissions.)
	 */
	if ((want_type & ARCHIVE_ENTRY_ACL_TYPE_ACCESS) != 0)
		cutoff = 3;
	else
		cutoff = 0;

	if (count > cutoff)
		acl->acl_state = ARCHIVE_ENTRY_ACL_USER_OBJ;
	else
		acl->acl_state = 0;
	acl->acl_p = acl->acl_head;
	return (count);
}


/*
 * Return the next ACL entry in the list.  Fake entries for the
 * standard permissions and include them in the returned list.
 */
int
archive_acl_next(struct archive *a, struct archive_acl *acl, int want_type,
    int *type, int *permset, int *tag, int *id, const char **name)
{
	*name = NULL;
	*id = -1;

	/*
	 * The acl_state is either zero (no entries available), -1
	 * (reading from list), or an entry type (retrieve that type
	 * from ae_stat.aest_mode).
	 */
	if (acl->acl_state == 0)
		return (ARCHIVE_WARN);

	/* The first three access entries are special. */
	if ((want_type & ARCHIVE_ENTRY_ACL_TYPE_ACCESS) != 0) {
		switch (acl->acl_state) {
		case ARCHIVE_ENTRY_ACL_USER_OBJ:
			*permset = (acl->mode >> 6) & 7;
			*type = ARCHIVE_ENTRY_ACL_TYPE_ACCESS;
			*tag = ARCHIVE_ENTRY_ACL_USER_OBJ;
			acl->acl_state = ARCHIVE_ENTRY_ACL_GROUP_OBJ;
			return (ARCHIVE_OK);
		case ARCHIVE_ENTRY_ACL_GROUP_OBJ:
			*permset = (acl->mode >> 3) & 7;
			*type = ARCHIVE_ENTRY_ACL_TYPE_ACCESS;
			*tag = ARCHIVE_ENTRY_ACL_GROUP_OBJ;
			acl->acl_state = ARCHIVE_ENTRY_ACL_OTHER;
			return (ARCHIVE_OK);
		case ARCHIVE_ENTRY_ACL_OTHER:
			*permset = acl->mode & 7;
			*type = ARCHIVE_ENTRY_ACL_TYPE_ACCESS;
			*tag = ARCHIVE_ENTRY_ACL_OTHER;
			acl->acl_state = -1;
			acl->acl_p = acl->acl_head;
			return (ARCHIVE_OK);
		default:
			break;
		}
	}

	while (acl->acl_p != NULL && (acl->acl_p->type & want_type) == 0)
		acl->acl_p = acl->acl_p->next;
	if (acl->acl_p == NULL) {
		acl->acl_state = 0;
		*type = 0;
		*permset = 0;
		*tag = 0;
		*id = -1;
		*name = NULL;
		return (ARCHIVE_EOF); /* End of ACL entries. */
	}
	*type = acl->acl_p->type;
	*permset = acl->acl_p->permset;
	*tag = acl->acl_p->tag;
	*id = acl->acl_p->id;
	if (archive_mstring_get_mbs(a, &acl->acl_p->name, name) != 0) {
		if (errno == ENOMEM)
			return (ARCHIVE_FATAL);
		*name = NULL;
	}
	acl->acl_p = acl->acl_p->next;
	return (ARCHIVE_OK);
}

/*
 * Determine what type of ACL do we want
 */
static int
archive_acl_text_want_type(struct archive_acl *acl, int flags)
{
	int want_type;

	/* Check if ACL is NFSv4 */
	if ((acl->acl_types & ARCHIVE_ENTRY_ACL_TYPE_NFS4) != 0) {
		/* NFSv4 should never mix with POSIX.1e */
		if ((acl->acl_types & ARCHIVE_ENTRY_ACL_TYPE_POSIX1E) != 0)
			return (0);
		else
			return (ARCHIVE_ENTRY_ACL_TYPE_NFS4);
	}

	/* Now deal with POSIX.1e ACLs */

	want_type = 0;
	if ((flags & ARCHIVE_ENTRY_ACL_TYPE_ACCESS) != 0)
		want_type |= ARCHIVE_ENTRY_ACL_TYPE_ACCESS;
	if ((flags & ARCHIVE_ENTRY_ACL_TYPE_DEFAULT) != 0)
		want_type |= ARCHIVE_ENTRY_ACL_TYPE_DEFAULT;

	/* By default we want both access and default ACLs */
	if (want_type == 0)
		return (ARCHIVE_ENTRY_ACL_TYPE_POSIX1E);

	return (want_type);
}

/*
 * Calculate ACL text string length
 */
static ssize_t
archive_acl_text_len(struct archive_acl *acl, int want_type, int flags,
    int wide, struct archive *a, struct archive_string_conv *sc) {
	struct archive_acl_entry *ap;
	const char *name;
	const wchar_t *wname;
	int count, idlen, tmp, r;
	ssize_t length;
	size_t len;

	count = 0;
	length = 0;
	for (ap = acl->acl_head; ap != NULL; ap = ap->next) {
		if ((ap->type & want_type) == 0)
			continue;
		/*
		 * Filemode-mapping ACL entries are stored exclusively in
		 * ap->mode so they should not be in the list
		 */
		if ((ap->type == ARCHIVE_ENTRY_ACL_TYPE_ACCESS)
		    && (ap->tag == ARCHIVE_ENTRY_ACL_USER_OBJ
		    || ap->tag == ARCHIVE_ENTRY_ACL_GROUP_OBJ
		    || ap->tag == ARCHIVE_ENTRY_ACL_OTHER))
			continue;
		count++;
		if ((want_type & ARCHIVE_ENTRY_ACL_TYPE_DEFAULT) != 0
		    && (ap->type & ARCHIVE_ENTRY_ACL_TYPE_DEFAULT) != 0)
			length += 8; /* "default:" */
		switch (ap->tag) {
		case ARCHIVE_ENTRY_ACL_USER_OBJ:
			if (want_type == ARCHIVE_ENTRY_ACL_TYPE_NFS4) {
				length += 6; /* "owner@" */
				break;
			}
			/* FALLTHROUGH */
		case ARCHIVE_ENTRY_ACL_USER:
		case ARCHIVE_ENTRY_ACL_MASK:
			length += 4; /* "user", "mask" */
			break;
		case ARCHIVE_ENTRY_ACL_GROUP_OBJ:
			if (want_type == ARCHIVE_ENTRY_ACL_TYPE_NFS4) {
				length += 6; /* "group@" */
				break;
			}
			/* FALLTHROUGH */
		case ARCHIVE_ENTRY_ACL_GROUP:
		case ARCHIVE_ENTRY_ACL_OTHER:
			length += 5; /* "group", "other" */
			break;
		case ARCHIVE_ENTRY_ACL_EVERYONE:
			length += 9; /* "everyone@" */
			break;
		}
		length += 1; /* colon after tag */
		if (ap->tag == ARCHIVE_ENTRY_ACL_USER ||
		    ap->tag == ARCHIVE_ENTRY_ACL_GROUP) {
			if (wide) {
				r = archive_mstring_get_wcs(a, &ap->name,
				    &wname);
				if (r == 0 && wname != NULL)
					length += wcslen(wname);
				else if (r < 0 && errno == ENOMEM)
					return (0);
				else
					length += sizeof(uid_t) * 3 + 1;
			} else {
				r = archive_mstring_get_mbs_l(&ap->name, &name,
				    &len, sc);
				if (r != 0)
					return (0);
				if (len > 0 && name != NULL)
					length += len;
				else
					length += sizeof(uid_t) * 3 + 1;
			}
			length += 1; /* colon after user or group name */
		} else if (want_type != ARCHIVE_ENTRY_ACL_TYPE_NFS4)
			length += 1; /* 2nd colon empty user,group or other */

		if (((flags & ARCHIVE_ENTRY_ACL_STYLE_SOLARIS) != 0)
		    && ((want_type & ARCHIVE_ENTRY_ACL_TYPE_POSIX1E) != 0)
		    && (ap->tag == ARCHIVE_ENTRY_ACL_OTHER
		    || ap->tag == ARCHIVE_ENTRY_ACL_MASK)) {
			/* Solaris has no colon after other: and mask: */
			length = length - 1;
		}

		if (want_type == ARCHIVE_ENTRY_ACL_TYPE_NFS4) {
			/* rwxpdDaARWcCos:fdinSFI:deny */
			length += 27;
			if ((ap->type & ARCHIVE_ENTRY_ACL_TYPE_DENY) == 0)
				length += 1; /* allow, alarm, audit */
		} else
			length += 3; /* rwx */

		if ((ap->tag == ARCHIVE_ENTRY_ACL_USER ||
		    ap->tag == ARCHIVE_ENTRY_ACL_GROUP) &&
		    (flags & ARCHIVE_ENTRY_ACL_STYLE_EXTRA_ID) != 0) {
			length += 1; /* colon */
			/* ID digit count */
			idlen = 1;
			tmp = ap->id;
			while (tmp > 9) {
				tmp = tmp / 10;
				idlen++;
			}
			length += idlen;
		}
		length ++; /* entry separator */
	}

	/* Add filemode-mapping access entries to the length */
	if ((want_type & ARCHIVE_ENTRY_ACL_TYPE_ACCESS) != 0) {
		if ((flags & ARCHIVE_ENTRY_ACL_STYLE_SOLARIS) != 0) {
			/* "user::rwx\ngroup::rwx\nother:rwx\n" */
			length += 31;
		} else {
			/* "user::rwx\ngroup::rwx\nother::rwx\n" */
			length += 32;
		}
	} else if (count == 0)
		return (0);

	/* The terminating character is included in count */
	return (length);
}

/*
 * Generate a wide text version of the ACL. The flags parameter controls
 * the type and style of the generated ACL.
 */
wchar_t *
archive_acl_to_text_w(struct archive_acl *acl, ssize_t *text_len, int flags,
    struct archive *a)
{
	int count;
	ssize_t length;
	size_t len;
	const wchar_t *wname;
	const wchar_t *prefix;
	wchar_t separator;
	struct archive_acl_entry *ap;
	int id, r, want_type;
	wchar_t *wp, *ws;

	want_type = archive_acl_text_want_type(acl, flags);

	/* Both NFSv4 and POSIX.1 types found */
	if (want_type == 0)
		return (NULL);

	if (want_type == ARCHIVE_ENTRY_ACL_TYPE_POSIX1E)
		flags |= ARCHIVE_ENTRY_ACL_STYLE_MARK_DEFAULT;

	length = archive_acl_text_len(acl, want_type, flags, 1, a, NULL);

	if (length == 0)
		return (NULL);

	if (flags & ARCHIVE_ENTRY_ACL_STYLE_SEPARATOR_COMMA)
		separator = L',';
	else
		separator = L'\n';

	/* Now, allocate the string and actually populate it. */
	wp = ws = (wchar_t *)malloc(length * sizeof(wchar_t));
	if (wp == NULL) {
		if (errno == ENOMEM)
			__archive_errx(1, "No memory");
		return (NULL);
	}
	count = 0;

	if ((want_type & ARCHIVE_ENTRY_ACL_TYPE_ACCESS) != 0) {
		append_entry_w(&wp, NULL, ARCHIVE_ENTRY_ACL_TYPE_ACCESS,
		    ARCHIVE_ENTRY_ACL_USER_OBJ, flags, NULL,
		    acl->mode & 0700, -1);
		*wp++ = separator;
		append_entry_w(&wp, NULL, ARCHIVE_ENTRY_ACL_TYPE_ACCESS,
		    ARCHIVE_ENTRY_ACL_GROUP_OBJ, flags, NULL,
		    acl->mode & 0070, -1);
		*wp++ = separator;
		append_entry_w(&wp, NULL, ARCHIVE_ENTRY_ACL_TYPE_ACCESS,
		    ARCHIVE_ENTRY_ACL_OTHER, flags, NULL,
		    acl->mode & 0007, -1);
		count += 3;
	}

	for (ap = acl->acl_head; ap != NULL; ap = ap->next) {
		if ((ap->type & want_type) == 0)
			continue;
		/*
		 * Filemode-mapping ACL entries are stored exclusively in
		 * ap->mode so they should not be in the list
		 */
		if ((ap->type == ARCHIVE_ENTRY_ACL_TYPE_ACCESS)
		    && (ap->tag == ARCHIVE_ENTRY_ACL_USER_OBJ
		    || ap->tag == ARCHIVE_ENTRY_ACL_GROUP_OBJ
		    || ap->tag == ARCHIVE_ENTRY_ACL_OTHER))
			continue;
		if (ap->type == ARCHIVE_ENTRY_ACL_TYPE_DEFAULT &&
		    (flags & ARCHIVE_ENTRY_ACL_STYLE_MARK_DEFAULT) != 0)
			prefix = L"default:";
		else
			prefix = NULL;
		r = archive_mstring_get_wcs(a, &ap->name, &wname);
		if (r == 0) {
			if (count > 0)
				*wp++ = separator;
			if (flags & ARCHIVE_ENTRY_ACL_STYLE_EXTRA_ID)
				id = ap->id;
			else
				id = -1;
			append_entry_w(&wp, prefix, ap->type, ap->tag, flags,
			    wname, ap->permset, id);
			count++;
		} else if (r < 0 && errno == ENOMEM) {
			free(ws);
			return (NULL);
		}
	}

	/* Add terminating character */
	*wp++ = L'\0';

	len = wcslen(ws);

	if ((ssize_t)len > (length - 1))
		__archive_errx(1, "Buffer overrun");

	if (text_len != NULL)
		*text_len = len;

	return (ws);
}

static void
append_id_w(wchar_t **wp, int id)
{
	if (id < 0)
		id = 0;
	if (id > 9)
		append_id_w(wp, id / 10);
	*(*wp)++ = L"0123456789"[id % 10];
}

static void
append_entry_w(wchar_t **wp, const wchar_t *prefix, int type,
    int tag, int flags, const wchar_t *wname, int perm, int id)
{
	int i;

	if (prefix != NULL) {
		wcscpy(*wp, prefix);
		*wp += wcslen(*wp);
	}
	switch (tag) {
	case ARCHIVE_ENTRY_ACL_USER_OBJ:
		wname = NULL;
		id = -1;
		if ((type & ARCHIVE_ENTRY_ACL_TYPE_NFS4) != 0) {
			wcscpy(*wp, L"owner@");
			break;
		}
		/* FALLTHROUGH */
	case ARCHIVE_ENTRY_ACL_USER:
		wcscpy(*wp, L"user");
		break;
	case ARCHIVE_ENTRY_ACL_GROUP_OBJ:
		wname = NULL;
		id = -1;
		if ((type & ARCHIVE_ENTRY_ACL_TYPE_NFS4) != 0) {
			wcscpy(*wp, L"group@");
			break;
		}
		/* FALLTHROUGH */
	case ARCHIVE_ENTRY_ACL_GROUP:
		wcscpy(*wp, L"group");
		break;
	case ARCHIVE_ENTRY_ACL_MASK:
		wcscpy(*wp, L"mask");
		wname = NULL;
		id = -1;
		break;
	case ARCHIVE_ENTRY_ACL_OTHER:
		wcscpy(*wp, L"other");
		wname = NULL;
		id = -1;
		break;
	case ARCHIVE_ENTRY_ACL_EVERYONE:
		wcscpy(*wp, L"everyone@");
		wname = NULL;
		id = -1;
		break;
	}
	*wp += wcslen(*wp);
	*(*wp)++ = L':';
	if (((type & ARCHIVE_ENTRY_ACL_TYPE_POSIX1E) != 0) ||
	    tag == ARCHIVE_ENTRY_ACL_USER ||
	    tag == ARCHIVE_ENTRY_ACL_GROUP) {
		if (wname != NULL) {
			wcscpy(*wp, wname);
			*wp += wcslen(*wp);
		} else if (tag == ARCHIVE_ENTRY_ACL_USER
		    || tag == ARCHIVE_ENTRY_ACL_GROUP) {
			append_id_w(wp, id);
			if ((type & ARCHIVE_ENTRY_ACL_TYPE_NFS4) == 0)
				id = -1;
		}
		/* Solaris style has no second colon after other and mask */
		if (((flags & ARCHIVE_ENTRY_ACL_STYLE_SOLARIS) == 0)
		    || (tag != ARCHIVE_ENTRY_ACL_OTHER
		    && tag != ARCHIVE_ENTRY_ACL_MASK))
			*(*wp)++ = L':';
	}
	if ((type & ARCHIVE_ENTRY_ACL_TYPE_POSIX1E) != 0) {
		/* POSIX.1e ACL perms */
		*(*wp)++ = (perm & 0444) ? L'r' : L'-';
		*(*wp)++ = (perm & 0222) ? L'w' : L'-';
		*(*wp)++ = (perm & 0111) ? L'x' : L'-';
	} else {
		/* NFSv4 ACL perms */
		for (i = 0; i < nfsv4_acl_perm_map_size; i++) {
			if (perm & nfsv4_acl_perm_map[i].perm)
				*(*wp)++ = nfsv4_acl_perm_map[i].wc;
			else if ((flags & ARCHIVE_ENTRY_ACL_STYLE_COMPACT) == 0)
				*(*wp)++ = L'-';
		}
		*(*wp)++ = L':';
		for (i = 0; i < nfsv4_acl_flag_map_size; i++) {
			if (perm & nfsv4_acl_flag_map[i].perm)
				*(*wp)++ = nfsv4_acl_flag_map[i].wc;
			else if ((flags & ARCHIVE_ENTRY_ACL_STYLE_COMPACT) == 0)
				*(*wp)++ = L'-';
		}
		*(*wp)++ = L':';
		switch (type) {
		case ARCHIVE_ENTRY_ACL_TYPE_ALLOW:
			wcscpy(*wp, L"allow");
			break;
		case ARCHIVE_ENTRY_ACL_TYPE_DENY:
			wcscpy(*wp, L"deny");
			break;
		case ARCHIVE_ENTRY_ACL_TYPE_AUDIT:
			wcscpy(*wp, L"audit");
			break;
		case ARCHIVE_ENTRY_ACL_TYPE_ALARM:
			wcscpy(*wp, L"alarm");
			break;
		default:
			break;
		}
		*wp += wcslen(*wp);
	}
	if (id != -1) {
		*(*wp)++ = L':';
		append_id_w(wp, id);
	}
}

/*
 * Generate a text version of the ACL. The flags parameter controls
 * the type and style of the generated ACL.
 */
char *
archive_acl_to_text_l(struct archive_acl *acl, ssize_t *text_len, int flags,
    struct archive_string_conv *sc)
{
	int count;
	ssize_t length;
	size_t len;
	const char *name;
	const char *prefix;
	char separator;
	struct archive_acl_entry *ap;
	int id, r, want_type;
	char *p, *s;

	want_type = archive_acl_text_want_type(acl, flags);

	/* Both NFSv4 and POSIX.1 types found */
	if (want_type == 0)
		return (NULL);

	if (want_type == ARCHIVE_ENTRY_ACL_TYPE_POSIX1E)
		flags |= ARCHIVE_ENTRY_ACL_STYLE_MARK_DEFAULT;

	length = archive_acl_text_len(acl, want_type, flags, 0, NULL, sc);

	if (length == 0)
		return (NULL);

	if (flags & ARCHIVE_ENTRY_ACL_STYLE_SEPARATOR_COMMA)
		separator = ',';
	else
		separator = '\n';

	/* Now, allocate the string and actually populate it. */
	p = s = (char *)malloc(length * sizeof(char));
	if (p == NULL) {
		if (errno == ENOMEM)
			__archive_errx(1, "No memory");
		return (NULL);
	}
	count = 0;

	if ((want_type & ARCHIVE_ENTRY_ACL_TYPE_ACCESS) != 0) {
		append_entry(&p, NULL, ARCHIVE_ENTRY_ACL_TYPE_ACCESS,
		    ARCHIVE_ENTRY_ACL_USER_OBJ, flags, NULL,
		    acl->mode & 0700, -1);
		*p++ = separator;
		append_entry(&p, NULL, ARCHIVE_ENTRY_ACL_TYPE_ACCESS,
		    ARCHIVE_ENTRY_ACL_GROUP_OBJ, flags, NULL,
		    acl->mode & 0070, -1);
		*p++ = separator;
		append_entry(&p, NULL, ARCHIVE_ENTRY_ACL_TYPE_ACCESS,
		    ARCHIVE_ENTRY_ACL_OTHER, flags, NULL,
		    acl->mode & 0007, -1);
		count += 3;
	}

	for (ap = acl->acl_head; ap != NULL; ap = ap->next) {
		if ((ap->type & want_type) == 0)
			continue;
		/*
		 * Filemode-mapping ACL entries are stored exclusively in
		 * ap->mode so they should not be in the list
		 */
		if ((ap->type == ARCHIVE_ENTRY_ACL_TYPE_ACCESS)
		    && (ap->tag == ARCHIVE_ENTRY_ACL_USER_OBJ
		    || ap->tag == ARCHIVE_ENTRY_ACL_GROUP_OBJ
		    || ap->tag == ARCHIVE_ENTRY_ACL_OTHER))
			continue;
		if (ap->type == ARCHIVE_ENTRY_ACL_TYPE_DEFAULT &&
		    (flags & ARCHIVE_ENTRY_ACL_STYLE_MARK_DEFAULT) != 0)
			prefix = "default:";
		else
			prefix = NULL;
		r = archive_mstring_get_mbs_l(
		    &ap->name, &name, &len, sc);
		if (r != 0) {
			free(s);
			return (NULL);
		}
		if (count > 0)
			*p++ = separator;
		if (name == NULL ||
		    (flags & ARCHIVE_ENTRY_ACL_STYLE_EXTRA_ID)) {
			id = ap->id;
		} else {
			id = -1;
		}
		append_entry(&p, prefix, ap->type, ap->tag, flags, name,
		    ap->permset, id);
		count++;
	}

	/* Add terminating character */
	*p++ = '\0';

	len = strlen(s);

	if ((ssize_t)len > (length - 1))
		__archive_errx(1, "Buffer overrun");

	if (text_len != NULL)
		*text_len = len;

	return (s);
}

static void
append_id(char **p, int id)
{
	if (id < 0)
		id = 0;
	if (id > 9)
		append_id(p, id / 10);
	*(*p)++ = "0123456789"[id % 10];
}

static void
append_entry(char **p, const char *prefix, int type,
    int tag, int flags, const char *name, int perm, int id)
{
	int i;

	if (prefix != NULL) {
		strcpy(*p, prefix);
		*p += strlen(*p);
	}
	switch (tag) {
	case ARCHIVE_ENTRY_ACL_USER_OBJ:
		name = NULL;
		id = -1;
		if ((type & ARCHIVE_ENTRY_ACL_TYPE_NFS4) != 0) {
			strcpy(*p, "owner@");
			break;
		}
		/* FALLTHROUGH */
	case ARCHIVE_ENTRY_ACL_USER:
		strcpy(*p, "user");
		break;
	case ARCHIVE_ENTRY_ACL_GROUP_OBJ:
		name = NULL;
		id = -1;
		if ((type & ARCHIVE_ENTRY_ACL_TYPE_NFS4) != 0) {
			strcpy(*p, "group@");
			break;
		}
		/* FALLTHROUGH */
	case ARCHIVE_ENTRY_ACL_GROUP:
		strcpy(*p, "group");
		break;
	case ARCHIVE_ENTRY_ACL_MASK:
		strcpy(*p, "mask");
		name = NULL;
		id = -1;
		break;
	case ARCHIVE_ENTRY_ACL_OTHER:
		strcpy(*p, "other");
		name = NULL;
		id = -1;
		break;
	case ARCHIVE_ENTRY_ACL_EVERYONE:
		strcpy(*p, "everyone@");
		name = NULL;
		id = -1;
		break;
	}
	*p += strlen(*p);
	*(*p)++ = ':';
	if (((type & ARCHIVE_ENTRY_ACL_TYPE_POSIX1E) != 0) ||
	    tag == ARCHIVE_ENTRY_ACL_USER ||
	    tag == ARCHIVE_ENTRY_ACL_GROUP) {
		if (name != NULL) {
			strcpy(*p, name);
			*p += strlen(*p);
		} else if (tag == ARCHIVE_ENTRY_ACL_USER
		    || tag == ARCHIVE_ENTRY_ACL_GROUP) {
			append_id(p, id);
			if ((type & ARCHIVE_ENTRY_ACL_TYPE_NFS4) == 0)
				id = -1;
		}
		/* Solaris style has no second colon after other and mask */
		if (((flags & ARCHIVE_ENTRY_ACL_STYLE_SOLARIS) == 0)
		    || (tag != ARCHIVE_ENTRY_ACL_OTHER
		    && tag != ARCHIVE_ENTRY_ACL_MASK))
			*(*p)++ = ':';
	}
	if ((type & ARCHIVE_ENTRY_ACL_TYPE_POSIX1E) != 0) {
		/* POSIX.1e ACL perms */
		*(*p)++ = (perm & 0444) ? 'r' : '-';
		*(*p)++ = (perm & 0222) ? 'w' : '-';
		*(*p)++ = (perm & 0111) ? 'x' : '-';
	} else {
		/* NFSv4 ACL perms */
		for (i = 0; i < nfsv4_acl_perm_map_size; i++) {
			if (perm & nfsv4_acl_perm_map[i].perm)
				*(*p)++ = nfsv4_acl_perm_map[i].c;
			else if ((flags & ARCHIVE_ENTRY_ACL_STYLE_COMPACT) == 0)
				*(*p)++ = '-';
		}
		*(*p)++ = ':';
		for (i = 0; i < nfsv4_acl_flag_map_size; i++) {
			if (perm & nfsv4_acl_flag_map[i].perm)
				*(*p)++ = nfsv4_acl_flag_map[i].c;
			else if ((flags & ARCHIVE_ENTRY_ACL_STYLE_COMPACT) == 0)
				*(*p)++ = '-';
		}
		*(*p)++ = ':';
		switch (type) {
		case ARCHIVE_ENTRY_ACL_TYPE_ALLOW:
			strcpy(*p, "allow");
			break;
		case ARCHIVE_ENTRY_ACL_TYPE_DENY:
			strcpy(*p, "deny");
			break;
		case ARCHIVE_ENTRY_ACL_TYPE_AUDIT:
			strcpy(*p, "audit");
			break;
		case ARCHIVE_ENTRY_ACL_TYPE_ALARM:
			strcpy(*p, "alarm");
			break;
		}
		*p += strlen(*p);
	}
	if (id != -1) {
		*(*p)++ = ':';
		append_id(p, id);
	}
}

/*
 * Parse a wide ACL text string.
 *
 * The want_type argument may be one of the following:
 * ARCHIVE_ENTRY_ACL_TYPE_ACCESS - text is a POSIX.1e ACL of type ACCESS
 * ARCHIVE_ENTRY_ACL_TYPE_DEFAULT - text is a POSIX.1e ACL of type DEFAULT
 * ARCHIVE_ENTRY_ACL_TYPE_NFS4 - text is as a NFSv4 ACL
 *
 * POSIX.1e ACL entries prefixed with "default:" are treated as
 * ARCHIVE_ENTRY_ACL_TYPE_DEFAULT unless type is ARCHIVE_ENTRY_ACL_TYPE_NFS4
 */
int
archive_acl_from_text_w(struct archive_acl *acl, const wchar_t *text,
    int want_type)
{
	struct {
		const wchar_t *start;
		const wchar_t *end;
	} field[6], name;

	const wchar_t *s, *st;

	int numfields, fields, n, r, sol, ret;
	int type, types, tag, permset, id;
	size_t len;
	wchar_t sep;

	ret = ARCHIVE_OK;
	types = 0;

	switch (want_type) {
	case ARCHIVE_ENTRY_ACL_TYPE_POSIX1E:
		want_type = ARCHIVE_ENTRY_ACL_TYPE_ACCESS;
		__LA_FALLTHROUGH;
	case ARCHIVE_ENTRY_ACL_TYPE_ACCESS:
	case ARCHIVE_ENTRY_ACL_TYPE_DEFAULT:
		numfields = 5;
		break;
	case ARCHIVE_ENTRY_ACL_TYPE_NFS4:
		numfields = 6;
		break;
	default:
		return (ARCHIVE_FATAL);
	}

	while (text != NULL && *text != L'\0') {
		/*
		 * Parse the fields out of the next entry,
		 * advance 'text' to start of next entry.
		 */
		fields = 0;
		do {
			const wchar_t *start, *end;
			next_field_w(&text, &start, &end, &sep);
			if (fields < numfields) {
				field[fields].start = start;
				field[fields].end = end;
			}
			++fields;
		} while (sep == L':');

		/* Set remaining fields to blank. */
		for (n = fields; n < numfields; ++n)
			field[n].start = field[n].end = NULL;

		if (field[0].start != NULL && *(field[0].start) == L'#') {
			/* Comment, skip entry */
			continue;
		}

		n = 0;
		sol = 0;
		id = -1;
		permset = 0;
		name.start = name.end = NULL;

		if (want_type != ARCHIVE_ENTRY_ACL_TYPE_NFS4) {
			/* POSIX.1e ACLs */
			/*
			 * Default keyword "default:user::rwx"
			 * if found, we have one more field
			 *
			 * We also support old Solaris extension:
			 * "defaultuser::rwx" is the default ACL corresponding
			 * to "user::rwx", etc. valid only for first field
			 */
			s = field[0].start;
			len = field[0].end - field[0].start;
			if (*s == L'd' && (len == 1 || (len >= 7
			    && wmemcmp((s + 1), L"efault", 6) == 0))) {
				type = ARCHIVE_ENTRY_ACL_TYPE_DEFAULT;
				if (len > 7)
					field[0].start += 7;
				else
					n = 1;
			} else
				type = want_type;

			/* Check for a numeric ID in field n+1 or n+3. */
			isint_w(field[n + 1].start, field[n + 1].end, &id);
			/* Field n+3 is optional. */
			if (id == -1 && fields > n+3)
				isint_w(field[n + 3].start, field[n + 3].end,
				    &id);

			tag = 0;
			s = field[n].start;
			st = field[n].start + 1;
			len = field[n].end - field[n].start;

			switch (*s) {
			case L'u':
				if (len == 1 || (len == 4
				    && wmemcmp(st, L"ser", 3) == 0))
					tag = ARCHIVE_ENTRY_ACL_USER_OBJ;
				break;
			case L'g':
				if (len == 1 || (len == 5
				    && wmemcmp(st, L"roup", 4) == 0))
					tag = ARCHIVE_ENTRY_ACL_GROUP_OBJ;
				break;
			case L'o':
				if (len == 1 || (len == 5
				    && wmemcmp(st, L"ther", 4) == 0))
					tag = ARCHIVE_ENTRY_ACL_OTHER;
				break;
			case L'm':
				if (len == 1 || (len == 4
				    && wmemcmp(st, L"ask", 3) == 0))
					tag = ARCHIVE_ENTRY_ACL_MASK;
				break;
			default:
					break;
			}

			switch (tag) {
			case ARCHIVE_ENTRY_ACL_OTHER:
			case ARCHIVE_ENTRY_ACL_MASK:
				if (fields == (n + 2)
				    && field[n + 1].start < field[n + 1].end
				    && ismode_w(field[n + 1].start,
				    field[n + 1].end, &permset)) {
					/* This is Solaris-style "other:rwx" */
					sol = 1;
				} else if (fields == (n + 3) &&
				    field[n + 1].start < field[n + 1].end) {
					/* Invalid mask or other field */
					ret = ARCHIVE_WARN;
					continue;
				}
				break;
			case ARCHIVE_ENTRY_ACL_USER_OBJ:
			case ARCHIVE_ENTRY_ACL_GROUP_OBJ:
				if (id != -1 ||
				    field[n + 1].start < field[n + 1].end) {
					name = field[n + 1];
					if (tag == ARCHIVE_ENTRY_ACL_USER_OBJ)
						tag = ARCHIVE_ENTRY_ACL_USER;
					else
						tag = ARCHIVE_ENTRY_ACL_GROUP;
				}
				break;
			default:
				/* Invalid tag, skip entry */
				ret = ARCHIVE_WARN;
				continue;
			}

			/*
			 * Without "default:" we expect mode in field 2
			 * Exception: Solaris other and mask fields
			 */
			if (permset == 0 && !ismode_w(field[n + 2 - sol].start,
			    field[n + 2 - sol].end, &permset)) {
				/* Invalid mode, skip entry */
				ret = ARCHIVE_WARN;
				continue;
			}
		} else {
			/* NFS4 ACLs */
			s = field[0].start;
			len = field[0].end - field[0].start;
			tag = 0;

			switch (len) {
			case 4:
				if (wmemcmp(s, L"user", 4) == 0)
					tag = ARCHIVE_ENTRY_ACL_USER;
				break;
			case 5:
				if (wmemcmp(s, L"group", 5) == 0)
					tag = ARCHIVE_ENTRY_ACL_GROUP;
				break;
			case 6:
				if (wmemcmp(s, L"owner@", 6) == 0)
					tag = ARCHIVE_ENTRY_ACL_USER_OBJ;
				else if (wmemcmp(s, L"group@", len) == 0)
					tag = ARCHIVE_ENTRY_ACL_GROUP_OBJ;
				break;
			case 9:
				if (wmemcmp(s, L"everyone@", 9) == 0)
					tag = ARCHIVE_ENTRY_ACL_EVERYONE;
			default:
				break;
			}

			if (tag == 0) {
				/* Invalid tag, skip entry */
				ret = ARCHIVE_WARN;
				continue;
			} else if (tag == ARCHIVE_ENTRY_ACL_USER ||
			    tag == ARCHIVE_ENTRY_ACL_GROUP) {
				n = 1;
				name = field[1];
				isint_w(name.start, name.end, &id);
			} else
				n = 0;

			if (!is_nfs4_perms_w(field[1 + n].start,
			    field[1 + n].end, &permset)) {
				/* Invalid NFSv4 perms, skip entry */
				ret = ARCHIVE_WARN;
				continue;
			}
			if (!is_nfs4_flags_w(field[2 + n].start,
			    field[2 + n].end, &permset)) {
				/* Invalid NFSv4 flags, skip entry */
				ret = ARCHIVE_WARN;
				continue;
			}
			s = field[3 + n].start;
			len = field[3 + n].end - field[3 + n].start;
			type = 0;
			if (len == 4) {
				if (wmemcmp(s, L"deny", 4) == 0)
					type = ARCHIVE_ENTRY_ACL_TYPE_DENY;
			} else if (len == 5) {
				if (wmemcmp(s, L"allow", 5) == 0)
					type = ARCHIVE_ENTRY_ACL_TYPE_ALLOW;
				else if (wmemcmp(s, L"audit", 5) == 0)
					type = ARCHIVE_ENTRY_ACL_TYPE_AUDIT;
				else if (wmemcmp(s, L"alarm", 5) == 0)
					type = ARCHIVE_ENTRY_ACL_TYPE_ALARM;
			}
			if (type == 0) {
				/* Invalid entry type, skip entry */
				ret = ARCHIVE_WARN;
				continue;
			}
			isint_w(field[4 + n].start, field[4 + n].end, &id);
		}

		/* Add entry to the internal list. */
		r = archive_acl_add_entry_w_len(acl, type, permset,
		    tag, id, name.start, name.end - name.start);
		if (r < ARCHIVE_WARN)
			return (r);
		if (r != ARCHIVE_OK)
			ret = ARCHIVE_WARN;
		types |= type;
	}

	/* Reset ACL */
	archive_acl_reset(acl, types);

	return (ret);
}

/*
 * Parse a string to a positive decimal integer.  Returns true if
 * the string is non-empty and consists only of decimal digits,
 * false otherwise.
 */
static int
isint_w(const wchar_t *start, const wchar_t *end, int *result)
{
	int n = 0;
	if (start >= end)
		return (0);
	while (start < end) {
		if (*start < '0' || *start > '9')
			return (0);
		if (n > (INT_MAX / 10) ||
		    (n == INT_MAX / 10 && (*start - '0') > INT_MAX % 10)) {
			n = INT_MAX;
		} else {
			n *= 10;
			n += *start - '0';
		}
		start++;
	}
	*result = n;
	return (1);
}

/*
 * Parse a string as a mode field.  Returns true if
 * the string is non-empty and consists only of mode characters,
 * false otherwise.
 */
static int
ismode_w(const wchar_t *start, const wchar_t *end, int *permset)
{
	const wchar_t *p;

	if (start >= end)
		return (0);
	p = start;
	*permset = 0;
	while (p < end) {
		switch (*p++) {
		case L'r': case L'R':
			*permset |= ARCHIVE_ENTRY_ACL_READ;
			break;
		case L'w': case L'W':
			*permset |= ARCHIVE_ENTRY_ACL_WRITE;
			break;
		case L'x': case L'X':
			*permset |= ARCHIVE_ENTRY_ACL_EXECUTE;
			break;
		case L'-':
			break;
		default:
			return (0);
		}
	}
	return (1);
}

/*
 * Parse a string as a NFS4 ACL permission field.
 * Returns true if the string is non-empty and consists only of NFS4 ACL
 * permission characters, false otherwise
 */
static int
is_nfs4_perms_w(const wchar_t *start, const wchar_t *end, int *permset)
{
	const wchar_t *p = start;

	while (p < end) {
		switch (*p++) {
		case L'r':
			*permset |= ARCHIVE_ENTRY_ACL_READ_DATA;
			break;
		case L'w':
			*permset |= ARCHIVE_ENTRY_ACL_WRITE_DATA;
			break;
		case L'x':
			*permset |= ARCHIVE_ENTRY_ACL_EXECUTE;
			break;
		case L'p':
			*permset |= ARCHIVE_ENTRY_ACL_APPEND_DATA;
			break;
		case L'D':
			*permset |= ARCHIVE_ENTRY_ACL_DELETE_CHILD;
			break;
		case L'd':
			*permset |= ARCHIVE_ENTRY_ACL_DELETE;
			break;
		case L'a':
			*permset |= ARCHIVE_ENTRY_ACL_READ_ATTRIBUTES;
			break;
		case L'A':
			*permset |= ARCHIVE_ENTRY_ACL_WRITE_ATTRIBUTES;
			break;
		case L'R':
			*permset |= ARCHIVE_ENTRY_ACL_READ_NAMED_ATTRS;
			break;
		case L'W':
			*permset |= ARCHIVE_ENTRY_ACL_WRITE_NAMED_ATTRS;
			break;
		case L'c':
			*permset |= ARCHIVE_ENTRY_ACL_READ_ACL;
			break;
		case L'C':
			*permset |= ARCHIVE_ENTRY_ACL_WRITE_ACL;
			break;
		case L'o':
			*permset |= ARCHIVE_ENTRY_ACL_WRITE_OWNER;
			break;
		case L's':
			*permset |= ARCHIVE_ENTRY_ACL_SYNCHRONIZE;
			break;
		case L'-':
			break;
		default:
			return(0);
		}
	}
	return (1);
}

/*
 * Parse a string as a NFS4 ACL flags field.
 * Returns true if the string is non-empty and consists only of NFS4 ACL
 * flag characters, false otherwise
 */
static int
is_nfs4_flags_w(const wchar_t *start, const wchar_t *end, int *permset)
{
	const wchar_t *p = start;

	while (p < end) {
		switch(*p++) {
		case L'f':
			*permset |= ARCHIVE_ENTRY_ACL_ENTRY_FILE_INHERIT;
			break;
		case L'd':
			*permset |= ARCHIVE_ENTRY_ACL_ENTRY_DIRECTORY_INHERIT;
			break;
		case L'i':
			*permset |= ARCHIVE_ENTRY_ACL_ENTRY_INHERIT_ONLY;
			break;
		case L'n':
			*permset |=
			    ARCHIVE_ENTRY_ACL_ENTRY_NO_PROPAGATE_INHERIT;
			break;
		case L'S':
			*permset |= ARCHIVE_ENTRY_ACL_ENTRY_SUCCESSFUL_ACCESS;
			break;
		case L'F':
			*permset |= ARCHIVE_ENTRY_ACL_ENTRY_FAILED_ACCESS;
			break;
		case L'I':
			*permset |= ARCHIVE_ENTRY_ACL_ENTRY_INHERITED;
			break;
		case L'-':
			break;
		default:
			return (0);
		}
	}
	return (1);
}

/*
 * Match "[:whitespace:]*(.*)[:whitespace:]*[:,\n]".  *wp is updated
 * to point to just after the separator.  *start points to the first
 * character of the matched text and *end just after the last
 * character of the matched identifier.  In particular *end - *start
 * is the length of the field body, not including leading or trailing
 * whitespace.
 */
static void
next_field_w(const wchar_t **wp, const wchar_t **start,
    const wchar_t **end, wchar_t *sep)
{
	/* Skip leading whitespace to find start of field. */
	while (**wp == L' ' || **wp == L'\t' || **wp == L'\n') {
		(*wp)++;
	}
	*start = *wp;

	/* Scan for the separator. */
	while (**wp != L'\0' && **wp != L',' && **wp != L':' &&
	    **wp != L'\n' && **wp != L'#') {
		(*wp)++;
	}
	*sep = **wp;

	/* Locate end of field, trim trailing whitespace if necessary */
	if (*wp == *start) {
		*end = *wp;
	} else {
		*end = *wp - 1;
		while (**end == L' ' || **end == L'\t' || **end == L'\n') {
			(*end)--;
		}
		(*end)++;
	}

	/* Handle in-field comments */
	if (*sep == L'#') {
		while (**wp != L'\0' && **wp != L',' && **wp != L'\n') {
			(*wp)++;
		}
		*sep = **wp;
	}

	/* Adjust scanner location. */
	if (**wp != L'\0')
		(*wp)++;
}

/*
 * Parse an ACL text string.
 *
 * The want_type argument may be one of the following:
 * ARCHIVE_ENTRY_ACL_TYPE_ACCESS - text is a POSIX.1e ACL of type ACCESS
 * ARCHIVE_ENTRY_ACL_TYPE_DEFAULT - text is a POSIX.1e ACL of type DEFAULT
 * ARCHIVE_ENTRY_ACL_TYPE_NFS4 - text is as a NFSv4 ACL
 *
 * POSIX.1e ACL entries prefixed with "default:" are treated as
 * ARCHIVE_ENTRY_ACL_TYPE_DEFAULT unless type is ARCHIVE_ENTRY_ACL_TYPE_NFS4
 */
int
archive_acl_from_text_l(struct archive_acl *acl, const char *text,
    int want_type, struct archive_string_conv *sc)
{
	struct {
		const char *start;
		const char *end;
	} field[6], name;

	const char *s, *st;
	int numfields, fields, n, r, sol, ret;
	int type, types, tag, permset, id;
	size_t len;
	char sep;

	switch (want_type) {
	case ARCHIVE_ENTRY_ACL_TYPE_POSIX1E:
		want_type = ARCHIVE_ENTRY_ACL_TYPE_ACCESS;
		__LA_FALLTHROUGH;
	case ARCHIVE_ENTRY_ACL_TYPE_ACCESS:
	case ARCHIVE_ENTRY_ACL_TYPE_DEFAULT:
		numfields = 5;
		break;
	case ARCHIVE_ENTRY_ACL_TYPE_NFS4:
		numfields = 6;
		break;
	default:
		return (ARCHIVE_FATAL);
	}

	ret = ARCHIVE_OK;
	types = 0;

	while (text != NULL &&  *text != '\0') {
		/*
		 * Parse the fields out of the next entry,
		 * advance 'text' to start of next entry.
		 */
		fields = 0;
		do {
			const char *start, *end;
			next_field(&text, &start, &end, &sep);
			if (fields < numfields) {
				field[fields].start = start;
				field[fields].end = end;
			}
			++fields;
		} while (sep == ':');

		/* Set remaining fields to blank. */
		for (n = fields; n < numfields; ++n)
			field[n].start = field[n].end = NULL;

		if (field[0].start != NULL && *(field[0].start) == '#') {
			/* Comment, skip entry */
			continue;
		}

		n = 0;
		sol = 0;
		id = -1;
		permset = 0;
		name.start = name.end = NULL;

		if (want_type != ARCHIVE_ENTRY_ACL_TYPE_NFS4) {
			/* POSIX.1e ACLs */
			/*
			 * Default keyword "default:user::rwx"
			 * if found, we have one more field
			 *
			 * We also support old Solaris extension:
			 * "defaultuser::rwx" is the default ACL corresponding
			 * to "user::rwx", etc. valid only for first field
			 */
			s = field[0].start;
			len = field[0].end - field[0].start;
			if (*s == 'd' && (len == 1 || (len >= 7
			    && memcmp((s + 1), "efault", 6) == 0))) {
				type = ARCHIVE_ENTRY_ACL_TYPE_DEFAULT;
				if (len > 7)
					field[0].start += 7;
				else
					n = 1;
			} else
				type = want_type;

			/* Check for a numeric ID in field n+1 or n+3. */
			isint(field[n + 1].start, field[n + 1].end, &id);
			/* Field n+3 is optional. */
			if (id == -1 && fields > (n + 3))
				isint(field[n + 3].start, field[n + 3].end,
				    &id);

			tag = 0;
			s = field[n].start;
			st = field[n].start + 1;
			len = field[n].end - field[n].start;

			if (len == 0) {
				ret = ARCHIVE_WARN;
				continue;
			}

			switch (*s) {
			case 'u':
				if (len == 1 || (len == 4
				    && memcmp(st, "ser", 3) == 0))
					tag = ARCHIVE_ENTRY_ACL_USER_OBJ;
				break;
			case 'g':
				if (len == 1 || (len == 5
				    && memcmp(st, "roup", 4) == 0))
					tag = ARCHIVE_ENTRY_ACL_GROUP_OBJ;
				break;
			case 'o':
				if (len == 1 || (len == 5
				    && memcmp(st, "ther", 4) == 0))
					tag = ARCHIVE_ENTRY_ACL_OTHER;
				break;
			case 'm':
				if (len == 1 || (len == 4
				    && memcmp(st, "ask", 3) == 0))
					tag = ARCHIVE_ENTRY_ACL_MASK;
				break;
			default:
					break;
			}

			switch (tag) {
			case ARCHIVE_ENTRY_ACL_OTHER:
			case ARCHIVE_ENTRY_ACL_MASK:
				if (fields == (n + 2)
				    && field[n + 1].start < field[n + 1].end
				    && ismode(field[n + 1].start,
				    field[n + 1].end, &permset)) {
					/* This is Solaris-style "other:rwx" */
					sol = 1;
				} else if (fields == (n + 3) &&
				    field[n + 1].start < field[n + 1].end) {
					/* Invalid mask or other field */
					ret = ARCHIVE_WARN;
					continue;
				}
				break;
			case ARCHIVE_ENTRY_ACL_USER_OBJ:
			case ARCHIVE_ENTRY_ACL_GROUP_OBJ:
				if (id != -1 ||
				    field[n + 1].start < field[n + 1].end) {
					name = field[n + 1];
					if (tag == ARCHIVE_ENTRY_ACL_USER_OBJ)
						tag = ARCHIVE_ENTRY_ACL_USER;
					else
						tag = ARCHIVE_ENTRY_ACL_GROUP;
				}
				break;
			default:
				/* Invalid tag, skip entry */
				ret = ARCHIVE_WARN;
				continue;
			}

			/*
			 * Without "default:" we expect mode in field 3
			 * Exception: Solaris other and mask fields
			 */
			if (permset == 0 && !ismode(field[n + 2 - sol].start,
			    field[n + 2 - sol].end, &permset)) {
				/* Invalid mode, skip entry */
				ret = ARCHIVE_WARN;
				continue;
			}
		} else {
			/* NFS4 ACLs */
			s = field[0].start;
			len = field[0].end - field[0].start;
			tag = 0;

			switch (len) {
			case 4:
				if (memcmp(s, "user", 4) == 0)
					tag = ARCHIVE_ENTRY_ACL_USER;
				break;
			case 5:
				if (memcmp(s, "group", 5) == 0)
					tag = ARCHIVE_ENTRY_ACL_GROUP;
				break;
			case 6:
				if (memcmp(s, "owner@", 6) == 0)
					tag = ARCHIVE_ENTRY_ACL_USER_OBJ;
				else if (memcmp(s, "group@", 6) == 0)
					tag = ARCHIVE_ENTRY_ACL_GROUP_OBJ;
				break;
			case 9:
				if (memcmp(s, "everyone@", 9) == 0)
					tag = ARCHIVE_ENTRY_ACL_EVERYONE;
				break;
			default:
				break;
			}

			if (tag == 0) {
				/* Invalid tag, skip entry */
				ret = ARCHIVE_WARN;
				continue;
			} else if (tag == ARCHIVE_ENTRY_ACL_USER ||
			    tag == ARCHIVE_ENTRY_ACL_GROUP) {
				n = 1;
				name = field[1];
				isint(name.start, name.end, &id);
			} else
				n = 0;

			if (!is_nfs4_perms(field[1 + n].start,
			    field[1 + n].end, &permset)) {
				/* Invalid NFSv4 perms, skip entry */
				ret = ARCHIVE_WARN;
				continue;
			}
			if (!is_nfs4_flags(field[2 + n].start,
			    field[2 + n].end, &permset)) {
				/* Invalid NFSv4 flags, skip entry */
				ret = ARCHIVE_WARN;
				continue;
			}
			s = field[3 + n].start;
			len = field[3 + n].end - field[3 + n].start;
			type = 0;
			if (len == 4) {
				if (memcmp(s, "deny", 4) == 0)
					type = ARCHIVE_ENTRY_ACL_TYPE_DENY;
			} else if (len == 5) {
				if (memcmp(s, "allow", 5) == 0)
					type = ARCHIVE_ENTRY_ACL_TYPE_ALLOW;
				else if (memcmp(s, "audit", 5) == 0)
					type = ARCHIVE_ENTRY_ACL_TYPE_AUDIT;
				else if (memcmp(s, "alarm", 5) == 0)
					type = ARCHIVE_ENTRY_ACL_TYPE_ALARM;
			}
			if (type == 0) {
				/* Invalid entry type, skip entry */
				ret = ARCHIVE_WARN;
				continue;
			}
			isint(field[4 + n].start, field[4 + n].end,
			    &id);
		}

		/* Add entry to the internal list. */
		r = archive_acl_add_entry_len_l(acl, type, permset,
		    tag, id, name.start, name.end - name.start, sc);
		if (r < ARCHIVE_WARN)
			return (r);
		if (r != ARCHIVE_OK)
			ret = ARCHIVE_WARN;
		types |= type;
	}

	/* Reset ACL */
	archive_acl_reset(acl, types);

	return (ret);
}

/*
 * Parse a string to a positive decimal integer.  Returns true if
 * the string is non-empty and consists only of decimal digits,
 * false otherwise.
 */
static int
isint(const char *start, const char *end, int *result)
{
	int n = 0;
	if (start >= end)
		return (0);
	while (start < end) {
		if (*start < '0' || *start > '9')
			return (0);
		if (n > (INT_MAX / 10) ||
		    (n == INT_MAX / 10 && (*start - '0') > INT_MAX % 10)) {
			n = INT_MAX;
		} else {
			n *= 10;
			n += *start - '0';
		}
		start++;
	}
	*result = n;
	return (1);
}

/*
 * Parse a string as a mode field.  Returns true if
 * the string is non-empty and consists only of mode characters,
 * false otherwise.
 */
static int
ismode(const char *start, const char *end, int *permset)
{
	const char *p;

	if (start >= end)
		return (0);
	p = start;
	*permset = 0;
	while (p < end) {
		switch (*p++) {
		case 'r': case 'R':
			*permset |= ARCHIVE_ENTRY_ACL_READ;
			break;
		case 'w': case 'W':
			*permset |= ARCHIVE_ENTRY_ACL_WRITE;
			break;
		case 'x': case 'X':
			*permset |= ARCHIVE_ENTRY_ACL_EXECUTE;
			break;
		case '-':
			break;
		default:
			return (0);
		}
	}
	return (1);
}

/*
 * Parse a string as a NFS4 ACL permission field.
 * Returns true if the string is non-empty and consists only of NFS4 ACL
 * permission characters, false otherwise
 */
static int
is_nfs4_perms(const char *start, const char *end, int *permset)
{
	const char *p = start;

	while (p < end) {
		switch (*p++) {
		case 'r':
			*permset |= ARCHIVE_ENTRY_ACL_READ_DATA;
			break;
		case 'w':
			*permset |= ARCHIVE_ENTRY_ACL_WRITE_DATA;
			break;
		case 'x':
			*permset |= ARCHIVE_ENTRY_ACL_EXECUTE;
			break;
		case 'p':
			*permset |= ARCHIVE_ENTRY_ACL_APPEND_DATA;
			break;
		case 'D':
			*permset |= ARCHIVE_ENTRY_ACL_DELETE_CHILD;
			break;
		case 'd':
			*permset |= ARCHIVE_ENTRY_ACL_DELETE;
			break;
		case 'a':
			*permset |= ARCHIVE_ENTRY_ACL_READ_ATTRIBUTES;
			break;
		case 'A':
			*permset |= ARCHIVE_ENTRY_ACL_WRITE_ATTRIBUTES;
			break;
		case 'R':
			*permset |= ARCHIVE_ENTRY_ACL_READ_NAMED_ATTRS;
			break;
		case 'W':
			*permset |= ARCHIVE_ENTRY_ACL_WRITE_NAMED_ATTRS;
			break;
		case 'c':
			*permset |= ARCHIVE_ENTRY_ACL_READ_ACL;
			break;
		case 'C':
			*permset |= ARCHIVE_ENTRY_ACL_WRITE_ACL;
			break;
		case 'o':
			*permset |= ARCHIVE_ENTRY_ACL_WRITE_OWNER;
			break;
		case 's':
			*permset |= ARCHIVE_ENTRY_ACL_SYNCHRONIZE;
			break;
		case '-':
			break;
		default:
			return(0);
		}
	}
	return (1);
}

/*
 * Parse a string as a NFS4 ACL flags field.
 * Returns true if the string is non-empty and consists only of NFS4 ACL
 * flag characters, false otherwise
 */
static int
is_nfs4_flags(const char *start, const char *end, int *permset)
{
	const char *p = start;

	while (p < end) {
		switch(*p++) {
		case 'f':
			*permset |= ARCHIVE_ENTRY_ACL_ENTRY_FILE_INHERIT;
			break;
		case 'd':
			*permset |= ARCHIVE_ENTRY_ACL_ENTRY_DIRECTORY_INHERIT;
			break;
		case 'i':
			*permset |= ARCHIVE_ENTRY_ACL_ENTRY_INHERIT_ONLY;
			break;
		case 'n':
			*permset |=
			    ARCHIVE_ENTRY_ACL_ENTRY_NO_PROPAGATE_INHERIT;
			break;
		case 'S':
			*permset |= ARCHIVE_ENTRY_ACL_ENTRY_SUCCESSFUL_ACCESS;
			break;
		case 'F':
			*permset |= ARCHIVE_ENTRY_ACL_ENTRY_FAILED_ACCESS;
			break;
		case 'I':
			*permset |= ARCHIVE_ENTRY_ACL_ENTRY_INHERITED;
			break;
		case '-':
			break;
		default:
			return (0);
		}
	}
	return (1);
}

/*
 * Match "[:whitespace:]*(.*)[:whitespace:]*[:,\n]".  *wp is updated
 * to point to just after the separator.  *start points to the first
 * character of the matched text and *end just after the last
 * character of the matched identifier.  In particular *end - *start
 * is the length of the field body, not including leading or trailing
 * whitespace.
 */
static void
next_field(const char **p, const char **start,
    const char **end, char *sep)
{
	/* Skip leading whitespace to find start of field. */
	while (**p == ' ' || **p == '\t' || **p == '\n') {
		(*p)++;
	}
	*start = *p;

	/* Scan for the separator. */
	while (**p != '\0' && **p != ',' && **p != ':' && **p != '\n' &&
	    **p != '#') {
		(*p)++;
	}
	*sep = **p;

	/* Locate end of field, trim trailing whitespace if necessary */
	if (*p == *start) {
		*end = *p;
	} else {
		*end = *p - 1;
		while (**end == ' ' || **end == '\t' || **end == '\n') {
			(*end)--;
		}
		(*end)++;
	}

	/* Handle in-field comments */
	if (*sep == '#') {
		while (**p != '\0' && **p != ',' && **p != '\n') {
			(*p)++;
		}
		*sep = **p;
	}

	/* Adjust scanner location. */
	if (**p != '\0')
		(*p)++;
}
