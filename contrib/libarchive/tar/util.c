/*-
 * Copyright (c) 2003-2007 Tim Kientzle
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

#include "bsdtar_platform.h"
__FBSDID("$FreeBSD$");

#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>  /* Linux doesn't define mode_t, etc. in sys/stat.h. */
#endif
#include <ctype.h>
#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#ifdef HAVE_IO_H
#include <io.h>
#endif
#ifdef HAVE_STDARG_H
#include <stdarg.h>
#endif
#ifdef HAVE_STDINT_H
#include <stdint.h>
#endif
#include <stdio.h>
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif
#ifdef HAVE_WCTYPE_H
#include <wctype.h>
#else
/* If we don't have wctype, we need to hack up some version of iswprint(). */
#define	iswprint isprint
#endif

#include "bsdtar.h"
#include "err.h"
#include "passphrase.h"

static size_t	bsdtar_expand_char(char *, size_t, char);
static const char *strip_components(const char *path, int elements);

#if defined(_WIN32) && !defined(__CYGWIN__)
#define	read _read
#endif

/* TODO:  Hack up a version of mbtowc for platforms with no wide
 * character support at all.  I think the following might suffice,
 * but it needs careful testing.
 * #if !HAVE_MBTOWC
 * #define	mbtowc(wcp, p, n) ((*wcp = *p), 1)
 * #endif
 */

/*
 * Print a string, taking care with any non-printable characters.
 *
 * Note that we use a stack-allocated buffer to receive the formatted
 * string if we can.  This is partly performance (avoiding a call to
 * malloc()), partly out of expedience (we have to call vsnprintf()
 * before malloc() anyway to find out how big a buffer we need; we may
 * as well point that first call at a small local buffer in case it
 * works), but mostly for safety (so we can use this to print messages
 * about out-of-memory conditions).
 */

void
safe_fprintf(FILE *f, const char *fmt, ...)
{
	char fmtbuff_stack[256]; /* Place to format the printf() string. */
	char outbuff[256]; /* Buffer for outgoing characters. */
	char *fmtbuff_heap; /* If fmtbuff_stack is too small, we use malloc */
	char *fmtbuff;  /* Pointer to fmtbuff_stack or fmtbuff_heap. */
	int fmtbuff_length;
	int length, n;
	va_list ap;
	const char *p;
	unsigned i;
	wchar_t wc;
	char try_wc;

	/* Use a stack-allocated buffer if we can, for speed and safety. */
	fmtbuff_heap = NULL;
	fmtbuff_length = sizeof(fmtbuff_stack);
	fmtbuff = fmtbuff_stack;

	/* Try formatting into the stack buffer. */
	va_start(ap, fmt);
	length = vsnprintf(fmtbuff, fmtbuff_length, fmt, ap);
	va_end(ap);

	/* If the result was too large, allocate a buffer on the heap. */
	while (length < 0 || length >= fmtbuff_length) {
		if (length >= fmtbuff_length)
			fmtbuff_length = length+1;
		else if (fmtbuff_length < 8192)
			fmtbuff_length *= 2;
		else if (fmtbuff_length < 1000000)
			fmtbuff_length += fmtbuff_length / 4;
		else {
			length = fmtbuff_length;
			fmtbuff_heap[length-1] = '\0';
			break;
		}
		free(fmtbuff_heap);
		fmtbuff_heap = malloc(fmtbuff_length);

		/* Reformat the result into the heap buffer if we can. */
		if (fmtbuff_heap != NULL) {
			fmtbuff = fmtbuff_heap;
			va_start(ap, fmt);
			length = vsnprintf(fmtbuff, fmtbuff_length, fmt, ap);
			va_end(ap);
		} else {
			/* Leave fmtbuff pointing to the truncated
			 * string in fmtbuff_stack. */
			fmtbuff = fmtbuff_stack;
			length = sizeof(fmtbuff_stack) - 1;
			break;
		}
	}

	/* Note: mbrtowc() has a cleaner API, but mbtowc() seems a bit
	 * more portable, so we use that here instead. */
	if (mbtowc(NULL, NULL, 1) == -1) { /* Reset the shift state. */
		/* mbtowc() should never fail in practice, but
		 * handle the theoretical error anyway. */
		free(fmtbuff_heap);
		return;
	}

	/* Write data, expanding unprintable characters. */
	p = fmtbuff;
	i = 0;
	try_wc = 1;
	while (*p != '\0') {

		/* Convert to wide char, test if the wide
		 * char is printable in the current locale. */
		if (try_wc && (n = mbtowc(&wc, p, length)) != -1) {
			length -= n;
			if (iswprint(wc) && wc != L'\\') {
				/* Printable, copy the bytes through. */
				while (n-- > 0)
					outbuff[i++] = *p++;
			} else {
				/* Not printable, format the bytes. */
				while (n-- > 0)
					i += (unsigned)bsdtar_expand_char(
					    outbuff, i, *p++);
			}
		} else {
			/* After any conversion failure, don't bother
			 * trying to convert the rest. */
			i += (unsigned)bsdtar_expand_char(outbuff, i, *p++);
			try_wc = 0;
		}

		/* If our output buffer is full, dump it and keep going. */
		if (i > (sizeof(outbuff) - 128)) {
			outbuff[i] = '\0';
			fprintf(f, "%s", outbuff);
			i = 0;
		}
	}
	outbuff[i] = '\0';
	fprintf(f, "%s", outbuff);

	/* If we allocated a heap-based formatting buffer, free it now. */
	free(fmtbuff_heap);
}

/*
 * Render an arbitrary sequence of bytes into printable ASCII characters.
 */
static size_t
bsdtar_expand_char(char *buff, size_t offset, char c)
{
	size_t i = offset;

	if (isprint((unsigned char)c) && c != '\\')
		buff[i++] = c;
	else {
		buff[i++] = '\\';
		switch (c) {
		case '\a': buff[i++] = 'a'; break;
		case '\b': buff[i++] = 'b'; break;
		case '\f': buff[i++] = 'f'; break;
		case '\n': buff[i++] = 'n'; break;
#if '\r' != '\n'
		/* On some platforms, \n and \r are the same. */
		case '\r': buff[i++] = 'r'; break;
#endif
		case '\t': buff[i++] = 't'; break;
		case '\v': buff[i++] = 'v'; break;
		case '\\': buff[i++] = '\\'; break;
		default:
			sprintf(buff + i, "%03o", 0xFF & (int)c);
			i += 3;
		}
	}

	return (i - offset);
}

int
yes(const char *fmt, ...)
{
	char buff[32];
	char *p;
	ssize_t l;

	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fprintf(stderr, " (y/N)? ");
	fflush(stderr);

	l = read(2, buff, sizeof(buff) - 1);
	if (l < 0) {
	  fprintf(stderr, "Keyboard read failed\n");
	  exit(1);
	}
	if (l == 0)
		return (0);
	buff[l] = 0;

	for (p = buff; *p != '\0'; p++) {
		if (isspace((unsigned char)*p))
			continue;
		switch(*p) {
		case 'y': case 'Y':
			return (1);
		case 'n': case 'N':
			return (0);
		default:
			return (0);
		}
	}

	return (0);
}

/*-
 * The logic here for -C <dir> attempts to avoid
 * chdir() as long as possible.  For example:
 * "-C /foo -C /bar file"          needs chdir("/bar") but not chdir("/foo")
 * "-C /foo -C bar file"           needs chdir("/foo/bar")
 * "-C /foo -C bar /file1"         does not need chdir()
 * "-C /foo -C bar /file1 file2"   needs chdir("/foo/bar") before file2
 *
 * The only correct way to handle this is to record a "pending" chdir
 * request and combine multiple requests intelligently until we
 * need to process a non-absolute file.  set_chdir() adds the new dir
 * to the pending list; do_chdir() actually executes any pending chdir.
 *
 * This way, programs that build tar command lines don't have to worry
 * about -C with non-existent directories; such requests will only
 * fail if the directory must be accessed.
 *
 */
void
set_chdir(struct bsdtar *bsdtar, const char *newdir)
{
#if defined(_WIN32) && !defined(__CYGWIN__)
	if (newdir[0] == '/' || newdir[0] == '\\' ||
	    /* Detect this type, for example, "C:\" or "C:/" */
	    (((newdir[0] >= 'a' && newdir[0] <= 'z') ||
	      (newdir[0] >= 'A' && newdir[0] <= 'Z')) &&
	    newdir[1] == ':' && (newdir[2] == '/' || newdir[2] == '\\'))) {
#else
	if (newdir[0] == '/') {
#endif
		/* The -C /foo -C /bar case; dump first one. */
		free(bsdtar->pending_chdir);
		bsdtar->pending_chdir = NULL;
	}
	if (bsdtar->pending_chdir == NULL)
		/* Easy case: no previously-saved dir. */
		bsdtar->pending_chdir = strdup(newdir);
	else {
		/* The -C /foo -C bar case; concatenate */
		char *old_pending = bsdtar->pending_chdir;
		size_t old_len = strlen(old_pending);
		bsdtar->pending_chdir = malloc(old_len + strlen(newdir) + 2);
		if (old_pending[old_len - 1] == '/')
			old_pending[old_len - 1] = '\0';
		if (bsdtar->pending_chdir != NULL)
			sprintf(bsdtar->pending_chdir, "%s/%s",
			    old_pending, newdir);
		free(old_pending);
	}
	if (bsdtar->pending_chdir == NULL)
		lafe_errc(1, errno, "No memory");
}

void
do_chdir(struct bsdtar *bsdtar)
{
	if (bsdtar->pending_chdir == NULL)
		return;

	if (chdir(bsdtar->pending_chdir) != 0) {
		lafe_errc(1, 0, "could not chdir to '%s'\n",
		    bsdtar->pending_chdir);
	}
	free(bsdtar->pending_chdir);
	bsdtar->pending_chdir = NULL;
}

static const char *
strip_components(const char *p, int elements)
{
	/* Skip as many elements as necessary. */
	while (elements > 0) {
		switch (*p++) {
		case '/':
#if defined(_WIN32) && !defined(__CYGWIN__)
		case '\\': /* Support \ path sep on Windows ONLY. */
#endif
			elements--;
			break;
		case '\0':
			/* Path is too short, skip it. */
			return (NULL);
		}
	}

	/* Skip any / characters.  This handles short paths that have
	 * additional / termination.  This also handles the case where
	 * the logic above stops in the middle of a duplicate //
	 * sequence (which would otherwise get converted to an
	 * absolute path). */
	for (;;) {
		switch (*p) {
		case '/':
#if defined(_WIN32) && !defined(__CYGWIN__)
		case '\\': /* Support \ path sep on Windows ONLY. */
#endif
			++p;
			break;
		case '\0':
			return (NULL);
		default:
			return (p);
		}
	}
}

static void
warn_strip_leading_char(struct bsdtar *bsdtar, const char *c)
{
	if (!bsdtar->warned_lead_slash) {
		lafe_warnc(0,
			   "Removing leading '%c' from member names",
			   c[0]);
		bsdtar->warned_lead_slash = 1;
	}
}

static void
warn_strip_drive_letter(struct bsdtar *bsdtar)
{
	if (!bsdtar->warned_lead_slash) {
		lafe_warnc(0,
			   "Removing leading drive letter from "
			   "member names");
		bsdtar->warned_lead_slash = 1;
	}
}

/*
 * Convert absolute path to non-absolute path by skipping leading
 * absolute path prefixes.
 */
static const char*
strip_absolute_path(struct bsdtar *bsdtar, const char *p)
{
	const char *rp;

	/* Remove leading "//./" or "//?/" or "//?/UNC/"
	 * (absolute path prefixes used by Windows API) */
	if ((p[0] == '/' || p[0] == '\\') &&
	    (p[1] == '/' || p[1] == '\\') &&
	    (p[2] == '.' || p[2] == '?') &&
	    (p[3] == '/' || p[3] == '\\'))
	{
		if (p[2] == '?' &&
		    (p[4] == 'U' || p[4] == 'u') &&
		    (p[5] == 'N' || p[5] == 'n') &&
		    (p[6] == 'C' || p[6] == 'c') &&
		    (p[7] == '/' || p[7] == '\\'))
			p += 8;
		else
			p += 4;
		warn_strip_drive_letter(bsdtar);
	}

	/* Remove multiple leading slashes and Windows drive letters. */
	do {
		rp = p;
		if (((p[0] >= 'a' && p[0] <= 'z') ||
		     (p[0] >= 'A' && p[0] <= 'Z')) &&
		    p[1] == ':') {
			p += 2;
			warn_strip_drive_letter(bsdtar);
		}

		/* Remove leading "/../", "/./", "//", etc. */
		while (p[0] == '/' || p[0] == '\\') {
			if (p[1] == '.' &&
			    p[2] == '.' &&
			    (p[3] == '/' || p[3] == '\\')) {
				p += 3; /* Remove "/..", leave "/" for next pass. */
			} else if (p[1] == '.' &&
				   (p[2] == '/' || p[2] == '\\')) {
				p += 2; /* Remove "/.", leave "/" for next pass. */
			} else
				p += 1; /* Remove "/". */
			warn_strip_leading_char(bsdtar, rp);
		}
	} while (rp != p);

	return (p);
}

/*
 * Handle --strip-components and any future path-rewriting options.
 * Returns non-zero if the pathname should not be extracted.
 *
 * Note: The rewrites are applied uniformly to pathnames and hardlink
 * names but not to symlink bodies.  This is deliberate: Symlink
 * bodies are not necessarily filenames.  Even when they are, they
 * need to be interpreted relative to the directory containing them,
 * so simple rewrites like this are rarely appropriate.
 *
 * TODO: Support pax-style regex path rewrites.
 */
int
edit_pathname(struct bsdtar *bsdtar, struct archive_entry *entry)
{
	const char *name = archive_entry_pathname(entry);
	const char *original_name = name;
	const char *hardlinkname = archive_entry_hardlink(entry);
	const char *original_hardlinkname = hardlinkname;
#if defined(HAVE_REGEX_H) || defined(HAVE_PCREPOSIX_H)
	char *subst_name;
	int r;

	/* Apply user-specified substitution to pathname. */
	r = apply_substitution(bsdtar, name, &subst_name, 0, 0);
	if (r == -1) {
		lafe_warnc(0, "Invalid substitution, skipping entry");
		return 1;
	}
	if (r == 1) {
		archive_entry_copy_pathname(entry, subst_name);
		if (*subst_name == '\0') {
			free(subst_name);
			return -1;
		} else
			free(subst_name);
		name = archive_entry_pathname(entry);
		original_name = name;
	}

	/* Apply user-specified substitution to hardlink target. */
	if (hardlinkname != NULL) {
		r = apply_substitution(bsdtar, hardlinkname, &subst_name, 0, 1);
		if (r == -1) {
			lafe_warnc(0, "Invalid substitution, skipping entry");
			return 1;
		}
		if (r == 1) {
			archive_entry_copy_hardlink(entry, subst_name);
			free(subst_name);
		}
		hardlinkname = archive_entry_hardlink(entry);
		original_hardlinkname = hardlinkname;
	}

	/* Apply user-specified substitution to symlink body. */
	if (archive_entry_symlink(entry) != NULL) {
		r = apply_substitution(bsdtar, archive_entry_symlink(entry), &subst_name, 1, 0);
		if (r == -1) {
			lafe_warnc(0, "Invalid substitution, skipping entry");
			return 1;
		}
		if (r == 1) {
			archive_entry_copy_symlink(entry, subst_name);
			free(subst_name);
		}
	}
#endif

	/* Strip leading dir names as per --strip-components option. */
	if (bsdtar->strip_components > 0) {
		name = strip_components(name, bsdtar->strip_components);
		if (name == NULL)
			return (1);

		if (hardlinkname != NULL) {
			hardlinkname = strip_components(hardlinkname,
			    bsdtar->strip_components);
			if (hardlinkname == NULL)
				return (1);
		}
	}

	if ((bsdtar->flags & OPTFLAG_ABSOLUTE_PATHS) == 0) {
		/* By default, don't write or restore absolute pathnames. */
		name = strip_absolute_path(bsdtar, name);
		if (*name == '\0')
			name = ".";

		if (hardlinkname != NULL) {
			hardlinkname = strip_absolute_path(bsdtar, hardlinkname);
			if (*hardlinkname == '\0')
				return (1);
		}
	} else {
		/* Strip redundant leading '/' characters. */
		while (name[0] == '/' && name[1] == '/')
			name++;
	}

	/* Replace name in archive_entry. */
	if (name != original_name) {
		archive_entry_copy_pathname(entry, name);
	}
	if (hardlinkname != original_hardlinkname) {
		archive_entry_copy_hardlink(entry, hardlinkname);
	}
	return (0);
}

/*
 * It would be nice to just use printf() for formatting large numbers,
 * but the compatibility problems are quite a headache.  Hence the
 * following simple utility function.
 */
const char *
tar_i64toa(int64_t n0)
{
	static char buff[24];
	uint64_t n = n0 < 0 ? -n0 : n0;
	char *p = buff + sizeof(buff);

	*--p = '\0';
	do {
		*--p = '0' + (int)(n % 10);
	} while (n /= 10);
	if (n0 < 0)
		*--p = '-';
	return p;
}

/*
 * Like strcmp(), but try to be a little more aware of the fact that
 * we're comparing two paths.  Right now, it just handles leading
 * "./" and trailing '/' specially, so that "a/b/" == "./a/b"
 *
 * TODO: Make this better, so that "./a//b/./c/" == "a/b/c"
 * TODO: After this works, push it down into libarchive.
 * TODO: Publish the path normalization routines in libarchive so
 * that bsdtar can normalize paths and use fast strcmp() instead
 * of this.
 *
 * Note: This is currently only used within write.c, so should
 * not handle \ path separators.
 */

int
pathcmp(const char *a, const char *b)
{
	/* Skip leading './' */
	if (a[0] == '.' && a[1] == '/' && a[2] != '\0')
		a += 2;
	if (b[0] == '.' && b[1] == '/' && b[2] != '\0')
		b += 2;
	/* Find the first difference, or return (0) if none. */
	while (*a == *b) {
		if (*a == '\0')
			return (0);
		a++;
		b++;
	}
	/*
	 * If one ends in '/' and the other one doesn't,
	 * they're the same.
	 */
	if (a[0] == '/' && a[1] == '\0' && b[0] == '\0')
		return (0);
	if (a[0] == '\0' && b[0] == '/' && b[1] == '\0')
		return (0);
	/* They're really different, return the correct sign. */
	return (*(const unsigned char *)a - *(const unsigned char *)b);
}

#define PPBUFF_SIZE 1024
const char *
passphrase_callback(struct archive *a, void *_client_data)
{
	struct bsdtar *bsdtar = (struct bsdtar *)_client_data;
	(void)a; /* UNUSED */

	if (bsdtar->ppbuff == NULL) {
		bsdtar->ppbuff = malloc(PPBUFF_SIZE);
		if (bsdtar->ppbuff == NULL)
			lafe_errc(1, errno, "Out of memory");
	}
	return lafe_readpassphrase("Enter passphrase:",
		bsdtar->ppbuff, PPBUFF_SIZE);
}

void
passphrase_free(char *ppbuff)
{
	if (ppbuff != NULL) {
		memset(ppbuff, 0, PPBUFF_SIZE);
		free(ppbuff);
	}
}

/*
 * Display information about the current file.
 *
 * The format here roughly duplicates the output of 'ls -l'.
 * This is based on SUSv2, where 'tar tv' is documented as
 * listing additional information in an "unspecified format,"
 * and 'pax -l' is documented as using the same format as 'ls -l'.
 */
void
list_item_verbose(struct bsdtar *bsdtar, FILE *out, struct archive_entry *entry)
{
	char			 tmp[100];
	size_t			 w;
	const char		*p;
	const char		*fmt;
	time_t			 tim;
	static time_t		 now;

	/*
	 * We avoid collecting the entire list in memory at once by
	 * listing things as we see them.  However, that also means we can't
	 * just pre-compute the field widths.  Instead, we start with guesses
	 * and just widen them as necessary.  These numbers are completely
	 * arbitrary.
	 */
	if (!bsdtar->u_width) {
		bsdtar->u_width = 6;
		bsdtar->gs_width = 13;
	}
	if (!now)
		time(&now);
	fprintf(out, "%s %d ",
	    archive_entry_strmode(entry),
	    archive_entry_nlink(entry));

	/* Use uname if it's present, else uid. */
	p = archive_entry_uname(entry);
	if ((p == NULL) || (*p == '\0')) {
		sprintf(tmp, "%lu ",
		    (unsigned long)archive_entry_uid(entry));
		p = tmp;
	}
	w = strlen(p);
	if (w > bsdtar->u_width)
		bsdtar->u_width = w;
	fprintf(out, "%-*s ", (int)bsdtar->u_width, p);

	/* Use gname if it's present, else gid. */
	p = archive_entry_gname(entry);
	if (p != NULL && p[0] != '\0') {
		fprintf(out, "%s", p);
		w = strlen(p);
	} else {
		sprintf(tmp, "%lu",
		    (unsigned long)archive_entry_gid(entry));
		w = strlen(tmp);
		fprintf(out, "%s", tmp);
	}

	/*
	 * Print device number or file size, right-aligned so as to make
	 * total width of group and devnum/filesize fields be gs_width.
	 * If gs_width is too small, grow it.
	 */
	if (archive_entry_filetype(entry) == AE_IFCHR
	    || archive_entry_filetype(entry) == AE_IFBLK) {
		sprintf(tmp, "%lu,%lu",
		    (unsigned long)archive_entry_rdevmajor(entry),
		    (unsigned long)archive_entry_rdevminor(entry));
	} else {
		strcpy(tmp, tar_i64toa(archive_entry_size(entry)));
	}
	if (w + strlen(tmp) >= bsdtar->gs_width)
		bsdtar->gs_width = w+strlen(tmp)+1;
	fprintf(out, "%*s", (int)(bsdtar->gs_width - w), tmp);

	/* Format the time using 'ls -l' conventions. */
	tim = archive_entry_mtime(entry);
#define	HALF_YEAR (time_t)365 * 86400 / 2
#if defined(_WIN32) && !defined(__CYGWIN__)
#define	DAY_FMT  "%d"  /* Windows' strftime function does not support %e format. */
#else
#define	DAY_FMT  "%e"  /* Day number without leading zeros */
#endif
	if (tim < now - HALF_YEAR || tim > now + HALF_YEAR)
		fmt = bsdtar->day_first ? DAY_FMT " %b  %Y" : "%b " DAY_FMT "  %Y";
	else
		fmt = bsdtar->day_first ? DAY_FMT " %b %H:%M" : "%b " DAY_FMT " %H:%M";
	strftime(tmp, sizeof(tmp), fmt, localtime(&tim));
	fprintf(out, " %s ", tmp);
	safe_fprintf(out, "%s", archive_entry_pathname(entry));

	/* Extra information for links. */
	if (archive_entry_hardlink(entry)) /* Hard link */
		safe_fprintf(out, " link to %s",
		    archive_entry_hardlink(entry));
	else if (archive_entry_symlink(entry)) /* Symbolic link */
		safe_fprintf(out, " -> %s", archive_entry_symlink(entry));
}
