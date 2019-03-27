/*-
 * Copyright (c) 2003-2011 Tim Kientzle
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

/*
 * Basic resizable string support, to simplify manipulating arbitrary-sized
 * strings while minimizing heap activity.
 *
 * In particular, the buffer used by a string object is only grown, it
 * never shrinks, so you can clear and reuse the same string object
 * without incurring additional memory allocations.
 */

#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#ifdef HAVE_ICONV_H
#include <iconv.h>
#endif
#ifdef HAVE_LANGINFO_H
#include <langinfo.h>
#endif
#ifdef HAVE_LOCALCHARSET_H
#include <localcharset.h>
#endif
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif
#ifdef HAVE_WCHAR_H
#include <wchar.h>
#endif
#if defined(_WIN32) && !defined(__CYGWIN__)
#include <windows.h>
#include <locale.h>
#endif

#include "archive_endian.h"
#include "archive_private.h"
#include "archive_string.h"
#include "archive_string_composition.h"

#if !defined(HAVE_WMEMCPY) && !defined(wmemcpy)
#define wmemcpy(a,b,i)  (wchar_t *)memcpy((a), (b), (i) * sizeof(wchar_t))
#endif

#if !defined(HAVE_WMEMMOVE) && !defined(wmemmove)
#define wmemmove(a,b,i)  (wchar_t *)memmove((a), (b), (i) * sizeof(wchar_t))
#endif

struct archive_string_conv {
	struct archive_string_conv	*next;
	char				*from_charset;
	char				*to_charset;
	unsigned			 from_cp;
	unsigned			 to_cp;
	/* Set 1 if from_charset and to_charset are the same. */
	int				 same;
	int				 flag;
#define SCONV_TO_CHARSET	1	/* MBS is being converted to specified
					 * charset. */
#define SCONV_FROM_CHARSET	(1<<1)	/* MBS is being converted from
					 * specified charset. */
#define SCONV_BEST_EFFORT 	(1<<2)	/* Copy at least ASCII code. */
#define SCONV_WIN_CP	 	(1<<3)	/* Use Windows API for converting
					 * MBS. */
#define SCONV_UTF8_LIBARCHIVE_2 (1<<4)	/* Incorrect UTF-8 made by libarchive
					 * 2.x in the wrong assumption. */
#define SCONV_NORMALIZATION_C	(1<<6)	/* Need normalization to be Form C.
					 * Before UTF-8 characters are actually
					 * processed. */
#define SCONV_NORMALIZATION_D	(1<<7)	/* Need normalization to be Form D.
					 * Before UTF-8 characters are actually
					 * processed.
					 * Currently this only for MAC OS X. */
#define SCONV_TO_UTF8		(1<<8)	/* "to charset" side is UTF-8. */
#define SCONV_FROM_UTF8		(1<<9)	/* "from charset" side is UTF-8. */
#define SCONV_TO_UTF16BE 	(1<<10)	/* "to charset" side is UTF-16BE. */
#define SCONV_FROM_UTF16BE 	(1<<11)	/* "from charset" side is UTF-16BE. */
#define SCONV_TO_UTF16LE 	(1<<12)	/* "to charset" side is UTF-16LE. */
#define SCONV_FROM_UTF16LE 	(1<<13)	/* "from charset" side is UTF-16LE. */
#define SCONV_TO_UTF16		(SCONV_TO_UTF16BE | SCONV_TO_UTF16LE)
#define SCONV_FROM_UTF16	(SCONV_FROM_UTF16BE | SCONV_FROM_UTF16LE)

#if HAVE_ICONV
	iconv_t				 cd;
	iconv_t				 cd_w;/* Use at archive_mstring on
				 	       * Windows. */
#endif
	/* A temporary buffer for normalization. */
	struct archive_string		 utftmp;
	int (*converter[2])(struct archive_string *, const void *, size_t,
	    struct archive_string_conv *);
	int				 nconverter;
};

#define CP_C_LOCALE	0	/* "C" locale only for this file. */
#define CP_UTF16LE	1200
#define CP_UTF16BE	1201

#define IS_HIGH_SURROGATE_LA(uc) ((uc) >= 0xD800 && (uc) <= 0xDBFF)
#define IS_LOW_SURROGATE_LA(uc)	 ((uc) >= 0xDC00 && (uc) <= 0xDFFF)
#define IS_SURROGATE_PAIR_LA(uc) ((uc) >= 0xD800 && (uc) <= 0xDFFF)
#define UNICODE_MAX		0x10FFFF
#define UNICODE_R_CHAR		0xFFFD	/* Replacement character. */
/* Set U+FFFD(Replacement character) in UTF-8. */
static const char utf8_replacement_char[] = {0xef, 0xbf, 0xbd};

static struct archive_string_conv *find_sconv_object(struct archive *,
	const char *, const char *);
static void add_sconv_object(struct archive *, struct archive_string_conv *);
static struct archive_string_conv *create_sconv_object(const char *,
	const char *, unsigned, int);
static void free_sconv_object(struct archive_string_conv *);
static struct archive_string_conv *get_sconv_object(struct archive *,
	const char *, const char *, int);
static unsigned make_codepage_from_charset(const char *);
static unsigned get_current_codepage(void);
static unsigned get_current_oemcp(void);
static size_t mbsnbytes(const void *, size_t);
static size_t utf16nbytes(const void *, size_t);
#if defined(_WIN32) && !defined(__CYGWIN__)
static int archive_wstring_append_from_mbs_in_codepage(
    struct archive_wstring *, const char *, size_t,
    struct archive_string_conv *);
static int archive_string_append_from_wcs_in_codepage(struct archive_string *,
    const wchar_t *, size_t, struct archive_string_conv *);
static int is_big_endian(void);
static int strncat_in_codepage(struct archive_string *, const void *,
    size_t, struct archive_string_conv *);
static int win_strncat_from_utf16be(struct archive_string *, const void *,
    size_t, struct archive_string_conv *);
static int win_strncat_from_utf16le(struct archive_string *, const void *,
    size_t, struct archive_string_conv *);
static int win_strncat_to_utf16be(struct archive_string *, const void *,
    size_t, struct archive_string_conv *);
static int win_strncat_to_utf16le(struct archive_string *, const void *,
    size_t, struct archive_string_conv *);
#endif
static int best_effort_strncat_from_utf16be(struct archive_string *,
    const void *, size_t, struct archive_string_conv *);
static int best_effort_strncat_from_utf16le(struct archive_string *,
    const void *, size_t, struct archive_string_conv *);
static int best_effort_strncat_to_utf16be(struct archive_string *,
    const void *, size_t, struct archive_string_conv *);
static int best_effort_strncat_to_utf16le(struct archive_string *,
    const void *, size_t, struct archive_string_conv *);
#if defined(HAVE_ICONV)
static int iconv_strncat_in_locale(struct archive_string *, const void *,
    size_t, struct archive_string_conv *);
#endif
static int best_effort_strncat_in_locale(struct archive_string *,
    const void *, size_t, struct archive_string_conv *);
static int _utf8_to_unicode(uint32_t *, const char *, size_t);
static int utf8_to_unicode(uint32_t *, const char *, size_t);
static inline uint32_t combine_surrogate_pair(uint32_t, uint32_t);
static int cesu8_to_unicode(uint32_t *, const char *, size_t);
static size_t unicode_to_utf8(char *, size_t, uint32_t);
static int utf16_to_unicode(uint32_t *, const char *, size_t, int);
static size_t unicode_to_utf16be(char *, size_t, uint32_t);
static size_t unicode_to_utf16le(char *, size_t, uint32_t);
static int strncat_from_utf8_libarchive2(struct archive_string *,
    const void *, size_t, struct archive_string_conv *);
static int strncat_from_utf8_to_utf8(struct archive_string *, const void *,
    size_t, struct archive_string_conv *);
static int archive_string_normalize_C(struct archive_string *, const void *,
    size_t, struct archive_string_conv *);
static int archive_string_normalize_D(struct archive_string *, const void *,
    size_t, struct archive_string_conv *);
static int archive_string_append_unicode(struct archive_string *,
    const void *, size_t, struct archive_string_conv *);

static struct archive_string *
archive_string_append(struct archive_string *as, const char *p, size_t s)
{
	if (archive_string_ensure(as, as->length + s + 1) == NULL)
		return (NULL);
	if (s)
		memmove(as->s + as->length, p, s);
	as->length += s;
	as->s[as->length] = 0;
	return (as);
}

static struct archive_wstring *
archive_wstring_append(struct archive_wstring *as, const wchar_t *p, size_t s)
{
	if (archive_wstring_ensure(as, as->length + s + 1) == NULL)
		return (NULL);
	if (s)
		wmemmove(as->s + as->length, p, s);
	as->length += s;
	as->s[as->length] = 0;
	return (as);
}

struct archive_string *
archive_array_append(struct archive_string *as, const char *p, size_t s)
{
	return archive_string_append(as, p, s);
}

void
archive_string_concat(struct archive_string *dest, struct archive_string *src)
{
	if (archive_string_append(dest, src->s, src->length) == NULL)
		__archive_errx(1, "Out of memory");
}

void
archive_wstring_concat(struct archive_wstring *dest,
    struct archive_wstring *src)
{
	if (archive_wstring_append(dest, src->s, src->length) == NULL)
		__archive_errx(1, "Out of memory");
}

void
archive_string_free(struct archive_string *as)
{
	as->length = 0;
	as->buffer_length = 0;
	free(as->s);
	as->s = NULL;
}

void
archive_wstring_free(struct archive_wstring *as)
{
	as->length = 0;
	as->buffer_length = 0;
	free(as->s);
	as->s = NULL;
}

struct archive_wstring *
archive_wstring_ensure(struct archive_wstring *as, size_t s)
{
	return (struct archive_wstring *)
		archive_string_ensure((struct archive_string *)as,
					s * sizeof(wchar_t));
}

/* Returns NULL on any allocation failure. */
struct archive_string *
archive_string_ensure(struct archive_string *as, size_t s)
{
	char *p;
	size_t new_length;

	/* If buffer is already big enough, don't reallocate. */
	if (as->s && (s <= as->buffer_length))
		return (as);

	/*
	 * Growing the buffer at least exponentially ensures that
	 * append operations are always linear in the number of
	 * characters appended.  Using a smaller growth rate for
	 * larger buffers reduces memory waste somewhat at the cost of
	 * a larger constant factor.
	 */
	if (as->buffer_length < 32)
		/* Start with a minimum 32-character buffer. */
		new_length = 32;
	else if (as->buffer_length < 8192)
		/* Buffers under 8k are doubled for speed. */
		new_length = as->buffer_length + as->buffer_length;
	else {
		/* Buffers 8k and over grow by at least 25% each time. */
		new_length = as->buffer_length + as->buffer_length / 4;
		/* Be safe: If size wraps, fail. */
		if (new_length < as->buffer_length) {
			/* On failure, wipe the string and return NULL. */
			archive_string_free(as);
			errno = ENOMEM;/* Make sure errno has ENOMEM. */
			return (NULL);
		}
	}
	/*
	 * The computation above is a lower limit to how much we'll
	 * grow the buffer.  In any case, we have to grow it enough to
	 * hold the request.
	 */
	if (new_length < s)
		new_length = s;
	/* Now we can reallocate the buffer. */
	p = (char *)realloc(as->s, new_length);
	if (p == NULL) {
		/* On failure, wipe the string and return NULL. */
		archive_string_free(as);
		errno = ENOMEM;/* Make sure errno has ENOMEM. */
		return (NULL);
	}

	as->s = p;
	as->buffer_length = new_length;
	return (as);
}

/*
 * TODO: See if there's a way to avoid scanning
 * the source string twice.  Then test to see
 * if it actually helps (remember that we're almost
 * always called with pretty short arguments, so
 * such an optimization might not help).
 */
struct archive_string *
archive_strncat(struct archive_string *as, const void *_p, size_t n)
{
	size_t s;
	const char *p, *pp;

	p = (const char *)_p;

	/* Like strlen(p), except won't examine positions beyond p[n]. */
	s = 0;
	pp = p;
	while (s < n && *pp) {
		pp++;
		s++;
	}
	if ((as = archive_string_append(as, p, s)) == NULL)
		__archive_errx(1, "Out of memory");
	return (as);
}

struct archive_wstring *
archive_wstrncat(struct archive_wstring *as, const wchar_t *p, size_t n)
{
	size_t s;
	const wchar_t *pp;

	/* Like strlen(p), except won't examine positions beyond p[n]. */
	s = 0;
	pp = p;
	while (s < n && *pp) {
		pp++;
		s++;
	}
	if ((as = archive_wstring_append(as, p, s)) == NULL)
		__archive_errx(1, "Out of memory");
	return (as);
}

struct archive_string *
archive_strcat(struct archive_string *as, const void *p)
{
	/* strcat is just strncat without an effective limit. 
	 * Assert that we'll never get called with a source
	 * string over 16MB.
	 * TODO: Review all uses of strcat in the source
	 * and try to replace them with strncat().
	 */
	return archive_strncat(as, p, 0x1000000);
}

struct archive_wstring *
archive_wstrcat(struct archive_wstring *as, const wchar_t *p)
{
	/* Ditto. */
	return archive_wstrncat(as, p, 0x1000000);
}

struct archive_string *
archive_strappend_char(struct archive_string *as, char c)
{
	if ((as = archive_string_append(as, &c, 1)) == NULL)
		__archive_errx(1, "Out of memory");
	return (as);
}

struct archive_wstring *
archive_wstrappend_wchar(struct archive_wstring *as, wchar_t c)
{
	if ((as = archive_wstring_append(as, &c, 1)) == NULL)
		__archive_errx(1, "Out of memory");
	return (as);
}

/*
 * Get the "current character set" name to use with iconv.
 * On FreeBSD, the empty character set name "" chooses
 * the correct character encoding for the current locale,
 * so this isn't necessary.
 * But iconv on Mac OS 10.6 doesn't seem to handle this correctly;
 * on that system, we have to explicitly call nl_langinfo()
 * to get the right name.  Not sure about other platforms.
 *
 * NOTE: GNU libiconv does not recognize the character-set name
 * which some platform nl_langinfo(CODESET) returns, so we should
 * use locale_charset() instead of nl_langinfo(CODESET) for GNU libiconv.
 */
static const char *
default_iconv_charset(const char *charset) {
	if (charset != NULL && charset[0] != '\0')
		return charset;
#if HAVE_LOCALE_CHARSET && !defined(__APPLE__)
	/* locale_charset() is broken on Mac OS */
	return locale_charset();
#elif HAVE_NL_LANGINFO
	return nl_langinfo(CODESET);
#else
	return "";
#endif
}

#if defined(_WIN32) && !defined(__CYGWIN__)

/*
 * Convert MBS to WCS.
 * Note: returns -1 if conversion fails.
 */
int
archive_wstring_append_from_mbs(struct archive_wstring *dest,
    const char *p, size_t len)
{
	return archive_wstring_append_from_mbs_in_codepage(dest, p, len, NULL);
}

static int
archive_wstring_append_from_mbs_in_codepage(struct archive_wstring *dest,
    const char *s, size_t length, struct archive_string_conv *sc)
{
	int count, ret = 0;
	UINT from_cp;

	if (sc != NULL)
		from_cp = sc->from_cp;
	else
		from_cp = get_current_codepage();

	if (from_cp == CP_C_LOCALE) {
		/*
		 * "C" locale special process.
		 */
		wchar_t *ws;
		const unsigned char *mp;

		if (NULL == archive_wstring_ensure(dest,
		    dest->length + length + 1))
			return (-1);

		ws = dest->s + dest->length;
		mp = (const unsigned char *)s;
		count = 0;
		while (count < (int)length && *mp) {
			*ws++ = (wchar_t)*mp++;
			count++;
		}
	} else if (sc != NULL &&
	    (sc->flag & (SCONV_NORMALIZATION_C | SCONV_NORMALIZATION_D))) {
		/*
		 * Normalize UTF-8 and UTF-16BE and convert it directly
		 * to UTF-16 as wchar_t.
		 */
		struct archive_string u16;
		int saved_flag = sc->flag;/* save current flag. */

		if (is_big_endian())
			sc->flag |= SCONV_TO_UTF16BE;
		else
			sc->flag |= SCONV_TO_UTF16LE;

		if (sc->flag & SCONV_FROM_UTF16) {
			/*
			 *  UTF-16BE/LE NFD ===> UTF-16 NFC
			 *  UTF-16BE/LE NFC ===> UTF-16 NFD
			 */
			count = (int)utf16nbytes(s, length);
		} else {
			/*
			 *  UTF-8 NFD ===> UTF-16 NFC
			 *  UTF-8 NFC ===> UTF-16 NFD
			 */
			count = (int)mbsnbytes(s, length);
		}
		u16.s = (char *)dest->s;
		u16.length = dest->length << 1;;
		u16.buffer_length = dest->buffer_length;
		if (sc->flag & SCONV_NORMALIZATION_C)
			ret = archive_string_normalize_C(&u16, s, count, sc);
		else
			ret = archive_string_normalize_D(&u16, s, count, sc);
		dest->s = (wchar_t *)u16.s;
		dest->length = u16.length >> 1;
		dest->buffer_length = u16.buffer_length;
		sc->flag = saved_flag;/* restore the saved flag. */
		return (ret);
	} else if (sc != NULL && (sc->flag & SCONV_FROM_UTF16)) {
		count = (int)utf16nbytes(s, length);
		count >>= 1; /* to be WCS length */
		/* Allocate memory for WCS. */
		if (NULL == archive_wstring_ensure(dest,
		    dest->length + count + 1))
			return (-1);
		wmemcpy(dest->s + dest->length, (const wchar_t *)s, count);
		if ((sc->flag & SCONV_FROM_UTF16BE) && !is_big_endian()) {
			uint16_t *u16 = (uint16_t *)(dest->s + dest->length);
			int b;
			for (b = 0; b < count; b++) {
				uint16_t val = archive_le16dec(u16+b);
				archive_be16enc(u16+b, val);
			}
		} else if ((sc->flag & SCONV_FROM_UTF16LE) && is_big_endian()) {
			uint16_t *u16 = (uint16_t *)(dest->s + dest->length);
			int b;
			for (b = 0; b < count; b++) {
				uint16_t val = archive_be16dec(u16+b);
				archive_le16enc(u16+b, val);
			}
		}
	} else {
		DWORD mbflag;
		size_t buffsize;

		if (sc == NULL)
			mbflag = 0;
		else if (sc->flag & SCONV_FROM_CHARSET) {
			/* Do not trust the length which comes from
			 * an archive file. */
			length = mbsnbytes(s, length);
			mbflag = 0;
		} else
			mbflag = MB_PRECOMPOSED;

		buffsize = dest->length + length + 1;
		do {
			/* Allocate memory for WCS. */
			if (NULL == archive_wstring_ensure(dest, buffsize))
				return (-1);
			/* Convert MBS to WCS. */
			count = MultiByteToWideChar(from_cp,
			    mbflag, s, (int)length, dest->s + dest->length,
			    (int)(dest->buffer_length >> 1) -1);
			if (count == 0 &&
			    GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
				/* Expand the WCS buffer. */
				buffsize = dest->buffer_length << 1;
				continue;
			}
			if (count == 0 && length != 0)
				ret = -1;
			break;
		} while (1);
	}
	dest->length += count;
	dest->s[dest->length] = L'\0';
	return (ret);
}

#else

/*
 * Convert MBS to WCS.
 * Note: returns -1 if conversion fails.
 */
int
archive_wstring_append_from_mbs(struct archive_wstring *dest,
    const char *p, size_t len)
{
	size_t r;
	int ret_val = 0;
	/*
	 * No single byte will be more than one wide character,
	 * so this length estimate will always be big enough.
	 */
	size_t wcs_length = len;
	size_t mbs_length = len;
	const char *mbs = p;
	wchar_t *wcs;
#if HAVE_MBRTOWC
	mbstate_t shift_state;

	memset(&shift_state, 0, sizeof(shift_state));
#endif
	if (NULL == archive_wstring_ensure(dest, dest->length + wcs_length + 1))
		return (-1);
	wcs = dest->s + dest->length;
	/*
	 * We cannot use mbsrtowcs/mbstowcs here because those may convert
	 * extra MBS when strlen(p) > len and one wide character consists of
	 * multi bytes.
	 */
	while (*mbs && mbs_length > 0) {
		if (wcs_length == 0) {
			dest->length = wcs - dest->s;
			dest->s[dest->length] = L'\0';
			wcs_length = mbs_length;
			if (NULL == archive_wstring_ensure(dest,
			    dest->length + wcs_length + 1))
				return (-1);
			wcs = dest->s + dest->length;
		}
#if HAVE_MBRTOWC
		r = mbrtowc(wcs, mbs, wcs_length, &shift_state);
#else
		r = mbtowc(wcs, mbs, wcs_length);
#endif
		if (r == (size_t)-1 || r == (size_t)-2) {
			ret_val = -1;
			if (errno == EILSEQ) {
				++mbs;
				--mbs_length;
				continue;
			} else
				break;
		}
		if (r == 0 || r > mbs_length)
			break;
		wcs++;
		wcs_length--;
		mbs += r;
		mbs_length -= r;
	}
	dest->length = wcs - dest->s;
	dest->s[dest->length] = L'\0';
	return (ret_val);
}

#endif

#if defined(_WIN32) && !defined(__CYGWIN__)

/*
 * WCS ==> MBS.
 * Note: returns -1 if conversion fails.
 *
 * Win32 builds use WideCharToMultiByte from the Windows API.
 * (Maybe Cygwin should too?  WideCharToMultiByte will know a
 * lot more about local character encodings than the wcrtomb()
 * wrapper is going to know.)
 */
int
archive_string_append_from_wcs(struct archive_string *as,
    const wchar_t *w, size_t len)
{
	return archive_string_append_from_wcs_in_codepage(as, w, len, NULL);
}

static int
archive_string_append_from_wcs_in_codepage(struct archive_string *as,
    const wchar_t *ws, size_t len, struct archive_string_conv *sc)
{
	BOOL defchar_used, *dp;
	int count, ret = 0;
	UINT to_cp;
	int wslen = (int)len;

	if (sc != NULL)
		to_cp = sc->to_cp;
	else
		to_cp = get_current_codepage();

	if (to_cp == CP_C_LOCALE) {
		/*
		 * "C" locale special process.
		 */
		const wchar_t *wp = ws;
		char *p;

		if (NULL == archive_string_ensure(as,
		    as->length + wslen +1))
			return (-1);
		p = as->s + as->length;
		count = 0;
		defchar_used = 0;
		while (count < wslen && *wp) {
			if (*wp > 255) {
				*p++ = '?';
				wp++;
				defchar_used = 1;
			} else
				*p++ = (char)*wp++;
			count++;
		}
	} else if (sc != NULL && (sc->flag & SCONV_TO_UTF16)) {
		uint16_t *u16;

		if (NULL ==
		    archive_string_ensure(as, as->length + len * 2 + 2))
			return (-1);
		u16 = (uint16_t *)(as->s + as->length);
		count = 0;
		defchar_used = 0;
		if (sc->flag & SCONV_TO_UTF16BE) {
			while (count < (int)len && *ws) {
				archive_be16enc(u16+count, *ws);
				ws++;
				count++;
			}
		} else {
			while (count < (int)len && *ws) {
				archive_le16enc(u16+count, *ws);
				ws++;
				count++;
			}
		}
		count <<= 1; /* to be byte size */
	} else {
		/* Make sure the MBS buffer has plenty to set. */
		if (NULL ==
		    archive_string_ensure(as, as->length + len * 2 + 1))
			return (-1);
		do {
			defchar_used = 0;
			if (to_cp == CP_UTF8 || sc == NULL)
				dp = NULL;
			else
				dp = &defchar_used;
			count = WideCharToMultiByte(to_cp, 0, ws, wslen,
			    as->s + as->length, (int)as->buffer_length-1, NULL, dp);
			if (count == 0 &&
			    GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
				/* Expand the MBS buffer and retry. */
				if (NULL == archive_string_ensure(as,
					as->buffer_length + len))
					return (-1);
				continue;
			}
			if (count == 0)
				ret = -1;
			break;
		} while (1);
	}
	as->length += count;
	as->s[as->length] = '\0';
	return (defchar_used?-1:ret);
}

#elif defined(HAVE_WCTOMB) || defined(HAVE_WCRTOMB)

/*
 * Translates a wide character string into current locale character set
 * and appends to the archive_string.  Note: returns -1 if conversion
 * fails.
 */
int
archive_string_append_from_wcs(struct archive_string *as,
    const wchar_t *w, size_t len)
{
	/* We cannot use the standard wcstombs() here because it
	 * cannot tell us how big the output buffer should be.  So
	 * I've built a loop around wcrtomb() or wctomb() that
	 * converts a character at a time and resizes the string as
	 * needed.  We prefer wcrtomb() when it's available because
	 * it's thread-safe. */
	int n, ret_val = 0;
	char *p;
	char *end;
#if HAVE_WCRTOMB
	mbstate_t shift_state;

	memset(&shift_state, 0, sizeof(shift_state));
#else
	/* Clear the shift state before starting. */
	wctomb(NULL, L'\0');
#endif
	/*
	 * Allocate buffer for MBS.
	 * We need this allocation here since it is possible that
	 * as->s is still NULL.
	 */
	if (archive_string_ensure(as, as->length + len + 1) == NULL)
		return (-1);

	p = as->s + as->length;
	end = as->s + as->buffer_length - MB_CUR_MAX -1;
	while (*w != L'\0' && len > 0) {
		if (p >= end) {
			as->length = p - as->s;
			as->s[as->length] = '\0';
			/* Re-allocate buffer for MBS. */
			if (archive_string_ensure(as,
			    as->length + len * 2 + 1) == NULL)
				return (-1);
			p = as->s + as->length;
			end = as->s + as->buffer_length - MB_CUR_MAX -1;
		}
#if HAVE_WCRTOMB
		n = wcrtomb(p, *w++, &shift_state);
#else
		n = wctomb(p, *w++);
#endif
		if (n == -1) {
			if (errno == EILSEQ) {
				/* Skip an illegal wide char. */
				*p++ = '?';
				ret_val = -1;
			} else {
				ret_val = -1;
				break;
			}
		} else
			p += n;
		len--;
	}
	as->length = p - as->s;
	as->s[as->length] = '\0';
	return (ret_val);
}

#else /* HAVE_WCTOMB || HAVE_WCRTOMB */

/*
 * TODO: Test if __STDC_ISO_10646__ is defined.
 * Non-Windows uses ISO C wcrtomb() or wctomb() to perform the conversion
 * one character at a time.  If a non-Windows platform doesn't have
 * either of these, fall back to the built-in UTF8 conversion.
 */
int
archive_string_append_from_wcs(struct archive_string *as,
    const wchar_t *w, size_t len)
{
	(void)as;/* UNUSED */
	(void)w;/* UNUSED */
	(void)len;/* UNUSED */
	errno = ENOSYS;
	return (-1);
}

#endif /* HAVE_WCTOMB || HAVE_WCRTOMB */

/*
 * Find a string conversion object by a pair of 'from' charset name
 * and 'to' charset name from an archive object.
 * Return NULL if not found.
 */
static struct archive_string_conv *
find_sconv_object(struct archive *a, const char *fc, const char *tc)
{
	struct archive_string_conv *sc; 

	if (a == NULL)
		return (NULL);

	for (sc = a->sconv; sc != NULL; sc = sc->next) {
		if (strcmp(sc->from_charset, fc) == 0 &&
		    strcmp(sc->to_charset, tc) == 0)
			break;
	}
	return (sc);
}

/*
 * Register a string object to an archive object.
 */
static void
add_sconv_object(struct archive *a, struct archive_string_conv *sc)
{
	struct archive_string_conv **psc; 

	/* Add a new sconv to sconv list. */
	psc = &(a->sconv);
	while (*psc != NULL)
		psc = &((*psc)->next);
	*psc = sc;
}

static void
add_converter(struct archive_string_conv *sc, int (*converter)
    (struct archive_string *, const void *, size_t,
     struct archive_string_conv *))
{
	if (sc == NULL || sc->nconverter >= 2)
		__archive_errx(1, "Programing error");
	sc->converter[sc->nconverter++] = converter;
}

static void
setup_converter(struct archive_string_conv *sc)
{

	/* Reset. */
	sc->nconverter = 0;

	/*
	 * Perform special sequence for the incorrect UTF-8 filenames
	 * made by libarchive2.x.
	 */
	if (sc->flag & SCONV_UTF8_LIBARCHIVE_2) {
		add_converter(sc, strncat_from_utf8_libarchive2);
		return;
	}

	/*
	 * Convert a string to UTF-16BE/LE.
	 */
	if (sc->flag & SCONV_TO_UTF16) {
		/*
		 * If the current locale is UTF-8, we can translate
		 * a UTF-8 string into a UTF-16BE string.
		 */
		if (sc->flag & SCONV_FROM_UTF8) {
			add_converter(sc, archive_string_append_unicode);
			return;
		}

#if defined(_WIN32) && !defined(__CYGWIN__)
		if (sc->flag & SCONV_WIN_CP) {
			if (sc->flag & SCONV_TO_UTF16BE)
				add_converter(sc, win_strncat_to_utf16be);
			else
				add_converter(sc, win_strncat_to_utf16le);
			return;
		}
#endif

#if defined(HAVE_ICONV)
		if (sc->cd != (iconv_t)-1) {
			add_converter(sc, iconv_strncat_in_locale);
			return;
		}
#endif

		if (sc->flag & SCONV_BEST_EFFORT) {
			if (sc->flag & SCONV_TO_UTF16BE)
				add_converter(sc,
					best_effort_strncat_to_utf16be);
			else
				add_converter(sc,
					best_effort_strncat_to_utf16le);
		} else
			/* Make sure we have no converter. */
			sc->nconverter = 0;
		return;
	}

	/*
	 * Convert a string from UTF-16BE/LE.
	 */
	if (sc->flag & SCONV_FROM_UTF16) {
		/*
		 * At least we should normalize a UTF-16BE string.
		 */
		if (sc->flag & SCONV_NORMALIZATION_D)
			add_converter(sc,archive_string_normalize_D);
		else if (sc->flag & SCONV_NORMALIZATION_C)
			add_converter(sc, archive_string_normalize_C);

		if (sc->flag & SCONV_TO_UTF8) {
			/*
			 * If the current locale is UTF-8, we can translate
			 * a UTF-16BE/LE string into a UTF-8 string directly.
			 */
			if (!(sc->flag &
			    (SCONV_NORMALIZATION_D |SCONV_NORMALIZATION_C)))
				add_converter(sc,
				    archive_string_append_unicode);
			return;
		}

#if defined(_WIN32) && !defined(__CYGWIN__)
		if (sc->flag & SCONV_WIN_CP) {
			if (sc->flag & SCONV_FROM_UTF16BE)
				add_converter(sc, win_strncat_from_utf16be);
			else
				add_converter(sc, win_strncat_from_utf16le);
			return;
		}
#endif

#if defined(HAVE_ICONV)
		if (sc->cd != (iconv_t)-1) {
			add_converter(sc, iconv_strncat_in_locale);
			return;
		}
#endif

		if ((sc->flag & (SCONV_BEST_EFFORT | SCONV_FROM_UTF16BE))
		    == (SCONV_BEST_EFFORT | SCONV_FROM_UTF16BE))
			add_converter(sc, best_effort_strncat_from_utf16be);
		else if ((sc->flag & (SCONV_BEST_EFFORT | SCONV_FROM_UTF16LE))
		    == (SCONV_BEST_EFFORT | SCONV_FROM_UTF16LE))
			add_converter(sc, best_effort_strncat_from_utf16le);
		else
			/* Make sure we have no converter. */
			sc->nconverter = 0;
		return;
	}

	if (sc->flag & SCONV_FROM_UTF8) {
		/*
		 * At least we should normalize a UTF-8 string.
		 */
		if (sc->flag & SCONV_NORMALIZATION_D)
			add_converter(sc,archive_string_normalize_D);
		else if (sc->flag & SCONV_NORMALIZATION_C)
			add_converter(sc, archive_string_normalize_C);

		/*
		 * Copy UTF-8 string with a check of CESU-8.
		 * Apparently, iconv does not check surrogate pairs in UTF-8
		 * when both from-charset and to-charset are UTF-8, and then
		 * we use our UTF-8 copy code.
		 */
		if (sc->flag & SCONV_TO_UTF8) {
			/*
			 * If the current locale is UTF-8, we can translate
			 * a UTF-16BE string into a UTF-8 string directly.
			 */
			if (!(sc->flag &
			    (SCONV_NORMALIZATION_D |SCONV_NORMALIZATION_C)))
				add_converter(sc, strncat_from_utf8_to_utf8);
			return;
		}
	}

#if defined(_WIN32) && !defined(__CYGWIN__)
	/*
	 * On Windows we can use Windows API for a string conversion.
	 */
	if (sc->flag & SCONV_WIN_CP) {
		add_converter(sc, strncat_in_codepage);
		return;
	}
#endif

#if HAVE_ICONV
	if (sc->cd != (iconv_t)-1) {
		add_converter(sc, iconv_strncat_in_locale);
		/*
		 * iconv generally does not support UTF-8-MAC and so
		 * we have to the output of iconv from NFC to NFD if
		 * need.
		 */
		if ((sc->flag & SCONV_FROM_CHARSET) &&
		    (sc->flag & SCONV_TO_UTF8)) {
			if (sc->flag & SCONV_NORMALIZATION_D)
				add_converter(sc, archive_string_normalize_D);
		}
		return;
	}
#endif

	/*
	 * Try conversion in the best effort or no conversion.
	 */
	if ((sc->flag & SCONV_BEST_EFFORT) || sc->same)
		add_converter(sc, best_effort_strncat_in_locale);
	else
		/* Make sure we have no converter. */
		sc->nconverter = 0;
}

/*
 * Return canonicalized charset-name but this supports just UTF-8, UTF-16BE
 * and CP932 which are referenced in create_sconv_object().
 */
static const char *
canonical_charset_name(const char *charset)
{
	char cs[16];
	char *p;
	const char *s;

	if (charset == NULL || charset[0] == '\0'
	    || strlen(charset) > 15)
		return (charset);

	/* Copy name to uppercase. */
	p = cs;
	s = charset;
	while (*s) {
		char c = *s++;
		if (c >= 'a' && c <= 'z')
			c -= 'a' - 'A';
		*p++ = c;
	}
	*p++ = '\0';

	if (strcmp(cs, "UTF-8") == 0 ||
	    strcmp(cs, "UTF8") == 0)
		return ("UTF-8");
	if (strcmp(cs, "UTF-16BE") == 0 ||
	    strcmp(cs, "UTF16BE") == 0)
		return ("UTF-16BE");
	if (strcmp(cs, "UTF-16LE") == 0 ||
	    strcmp(cs, "UTF16LE") == 0)
		return ("UTF-16LE");
	if (strcmp(cs, "CP932") == 0)
		return ("CP932");
	return (charset);
}

/*
 * Create a string conversion object.
 */
static struct archive_string_conv *
create_sconv_object(const char *fc, const char *tc,
    unsigned current_codepage, int flag)
{
	struct archive_string_conv *sc; 

	sc = calloc(1, sizeof(*sc));
	if (sc == NULL)
		return (NULL);
	sc->next = NULL;
	sc->from_charset = strdup(fc);
	if (sc->from_charset == NULL) {
		free(sc);
		return (NULL);
	}
	sc->to_charset = strdup(tc);
	if (sc->to_charset == NULL) {
		free(sc->from_charset);
		free(sc);
		return (NULL);
	}
	archive_string_init(&sc->utftmp);

	if (flag & SCONV_TO_CHARSET) {
		/*
		 * Convert characters from the current locale charset to
		 * a specified charset.
		 */
		sc->from_cp = current_codepage;
		sc->to_cp = make_codepage_from_charset(tc);
#if defined(_WIN32) && !defined(__CYGWIN__)
		if (IsValidCodePage(sc->to_cp))
			flag |= SCONV_WIN_CP;
#endif
	} else if (flag & SCONV_FROM_CHARSET) {
		/*
		 * Convert characters from a specified charset to
		 * the current locale charset.
		 */
		sc->to_cp = current_codepage;
		sc->from_cp = make_codepage_from_charset(fc);
#if defined(_WIN32) && !defined(__CYGWIN__)
		if (IsValidCodePage(sc->from_cp))
			flag |= SCONV_WIN_CP;
#endif
	}

	/*
	 * Check if "from charset" and "to charset" are the same.
	 */
	if (strcmp(fc, tc) == 0 ||
	    (sc->from_cp != (unsigned)-1 && sc->from_cp == sc->to_cp))
		sc->same = 1;
	else
		sc->same = 0;

	/*
	 * Mark if "from charset" or "to charset" are UTF-8 or UTF-16BE/LE.
	 */
	if (strcmp(tc, "UTF-8") == 0)
		flag |= SCONV_TO_UTF8;
	else if (strcmp(tc, "UTF-16BE") == 0)
		flag |= SCONV_TO_UTF16BE;
	else if (strcmp(tc, "UTF-16LE") == 0)
		flag |= SCONV_TO_UTF16LE;
	if (strcmp(fc, "UTF-8") == 0)
		flag |= SCONV_FROM_UTF8;
	else if (strcmp(fc, "UTF-16BE") == 0)
		flag |= SCONV_FROM_UTF16BE;
	else if (strcmp(fc, "UTF-16LE") == 0)
		flag |= SCONV_FROM_UTF16LE;
#if defined(_WIN32) && !defined(__CYGWIN__)
	if (sc->to_cp == CP_UTF8)
		flag |= SCONV_TO_UTF8;
	else if (sc->to_cp == CP_UTF16BE)
		flag |= SCONV_TO_UTF16BE | SCONV_WIN_CP;
	else if (sc->to_cp == CP_UTF16LE)
		flag |= SCONV_TO_UTF16LE | SCONV_WIN_CP;
	if (sc->from_cp == CP_UTF8)
		flag |= SCONV_FROM_UTF8;
	else if (sc->from_cp == CP_UTF16BE)
		flag |= SCONV_FROM_UTF16BE | SCONV_WIN_CP;
	else if (sc->from_cp == CP_UTF16LE)
		flag |= SCONV_FROM_UTF16LE | SCONV_WIN_CP;
#endif

	/*
	 * Set a flag for Unicode NFD. Usually iconv cannot correctly
	 * handle it. So we have to translate NFD characters to NFC ones
	 * ourselves before iconv handles. Another reason is to prevent
	 * that the same sight of two filenames, one is NFC and other
	 * is NFD, would be in its directory.
	 * On Mac OS X, although its filesystem layer automatically
	 * convert filenames to NFD, it would be useful for filename
	 * comparing to find out the same filenames that we normalize
	 * that to be NFD ourselves.
	 */
	if ((flag & SCONV_FROM_CHARSET) &&
	    (flag & (SCONV_FROM_UTF16 | SCONV_FROM_UTF8))) {
#if defined(__APPLE__)
		if (flag & SCONV_TO_UTF8)
			flag |= SCONV_NORMALIZATION_D;
		else
#endif
			flag |= SCONV_NORMALIZATION_C;
	}
#if defined(__APPLE__)
	/*
	 * In case writing an archive file, make sure that a filename
	 * going to be passed to iconv is a Unicode NFC string since
	 * a filename in HFS Plus filesystem is a Unicode NFD one and
	 * iconv cannot handle it with "UTF-8" charset. It is simpler
	 * than a use of "UTF-8-MAC" charset.
	 */
	if ((flag & SCONV_TO_CHARSET) &&
	    (flag & (SCONV_FROM_UTF16 | SCONV_FROM_UTF8)) &&
	    !(flag & (SCONV_TO_UTF16 | SCONV_TO_UTF8)))
		flag |= SCONV_NORMALIZATION_C;
	/*
	 * In case reading an archive file. make sure that a filename
	 * will be passed to users is a Unicode NFD string in order to
	 * correctly compare the filename with other one which comes
	 * from HFS Plus filesystem.
	 */
	if ((flag & SCONV_FROM_CHARSET) &&
	   !(flag & (SCONV_FROM_UTF16 | SCONV_FROM_UTF8)) &&
	    (flag & SCONV_TO_UTF8))
		flag |= SCONV_NORMALIZATION_D;
#endif

#if defined(HAVE_ICONV)
	sc->cd_w = (iconv_t)-1;
	/*
	 * Create an iconv object.
	 */
	if (((flag & (SCONV_TO_UTF8 | SCONV_TO_UTF16)) &&
	    (flag & (SCONV_FROM_UTF8 | SCONV_FROM_UTF16))) ||
	    (flag & SCONV_WIN_CP)) {
		/* This case we won't use iconv. */
		sc->cd = (iconv_t)-1;
	} else {
		sc->cd = iconv_open(tc, fc);
		if (sc->cd == (iconv_t)-1 && (sc->flag & SCONV_BEST_EFFORT)) {
			/*
			 * Unfortunately, all of iconv implements do support
			 * "CP932" character-set, so we should use "SJIS"
			 * instead if iconv_open failed.
			 */
			if (strcmp(tc, "CP932") == 0)
				sc->cd = iconv_open("SJIS", fc);
			else if (strcmp(fc, "CP932") == 0)
				sc->cd = iconv_open(tc, "SJIS");
		}
#if defined(_WIN32) && !defined(__CYGWIN__)
		/*
		 * archive_mstring on Windows directly convert multi-bytes
		 * into archive_wstring in order not to depend on locale
		 * so that you can do a I18N programming. This will be
		 * used only in archive_mstring_copy_mbs_len_l so far.
		 */
		if (flag & SCONV_FROM_CHARSET) {
			sc->cd_w = iconv_open("UTF-8", fc);
			if (sc->cd_w == (iconv_t)-1 &&
			    (sc->flag & SCONV_BEST_EFFORT)) {
				if (strcmp(fc, "CP932") == 0)
					sc->cd_w = iconv_open("UTF-8", "SJIS");
			}
		}
#endif /* _WIN32 && !__CYGWIN__ */
	}
#endif	/* HAVE_ICONV */

	sc->flag = flag;

	/*
	 * Set up converters.
	 */
	setup_converter(sc);

	return (sc);
}

/*
 * Free a string conversion object.
 */
static void
free_sconv_object(struct archive_string_conv *sc)
{
	free(sc->from_charset);
	free(sc->to_charset);
	archive_string_free(&sc->utftmp);
#if HAVE_ICONV
	if (sc->cd != (iconv_t)-1)
		iconv_close(sc->cd);
	if (sc->cd_w != (iconv_t)-1)
		iconv_close(sc->cd_w);
#endif
	free(sc);
}

#if defined(_WIN32) && !defined(__CYGWIN__)
static unsigned
my_atoi(const char *p)
{
	unsigned cp;

	cp = 0;
	while (*p) {
		if (*p >= '0' && *p <= '9')
			cp = cp * 10 + (*p - '0');
		else
			return (-1);
		p++;
	}
	return (cp);
}

/*
 * Translate Charset name (as used by iconv) into CodePage (as used by Windows)
 * Return -1 if failed.
 *
 * Note: This translation code may be insufficient.
 */
static struct charset {
	const char *name;
	unsigned cp;
} charsets[] = {
	/* MUST BE SORTED! */
	{"ASCII", 1252},
	{"ASMO-708", 708},
	{"BIG5", 950},
	{"CHINESE", 936},
	{"CP367", 1252},
	{"CP819", 1252},
	{"CP1025", 21025},
	{"DOS-720", 720},
	{"DOS-862", 862},
	{"EUC-CN", 51936},
	{"EUC-JP", 51932},
	{"EUC-KR", 949},
	{"EUCCN", 51936},
	{"EUCJP", 51932},
	{"EUCKR", 949},
	{"GB18030", 54936},
	{"GB2312", 936},
	{"HEBREW", 1255},
	{"HZ-GB-2312", 52936},
	{"IBM273", 20273},
	{"IBM277", 20277},
	{"IBM278", 20278},
	{"IBM280", 20280},
	{"IBM284", 20284},
	{"IBM285", 20285},
	{"IBM290", 20290},
	{"IBM297", 20297},
	{"IBM367", 1252},
	{"IBM420", 20420},
	{"IBM423", 20423},
	{"IBM424", 20424},
	{"IBM819", 1252},
	{"IBM871", 20871},
	{"IBM880", 20880},
	{"IBM905", 20905},
	{"IBM924", 20924},
	{"ISO-8859-1", 28591},
	{"ISO-8859-13", 28603},
	{"ISO-8859-15", 28605},
	{"ISO-8859-2", 28592},
	{"ISO-8859-3", 28593},
	{"ISO-8859-4", 28594},
	{"ISO-8859-5", 28595},
	{"ISO-8859-6", 28596},
	{"ISO-8859-7", 28597},
	{"ISO-8859-8", 28598},
	{"ISO-8859-9", 28599},
	{"ISO8859-1", 28591},
	{"ISO8859-13", 28603},
	{"ISO8859-15", 28605},
	{"ISO8859-2", 28592},
	{"ISO8859-3", 28593},
	{"ISO8859-4", 28594},
	{"ISO8859-5", 28595},
	{"ISO8859-6", 28596},
	{"ISO8859-7", 28597},
	{"ISO8859-8", 28598},
	{"ISO8859-9", 28599},
	{"JOHAB", 1361},
	{"KOI8-R", 20866},
	{"KOI8-U", 21866},
	{"KS_C_5601-1987", 949},
	{"LATIN1", 1252},
	{"LATIN2", 28592},
	{"MACINTOSH", 10000},
	{"SHIFT-JIS", 932},
	{"SHIFT_JIS", 932},
	{"SJIS", 932},
	{"US", 1252},
	{"US-ASCII", 1252},
	{"UTF-16", 1200},
	{"UTF-16BE", 1201},
	{"UTF-16LE", 1200},
	{"UTF-8", CP_UTF8},
	{"X-EUROPA", 29001},
	{"X-MAC-ARABIC", 10004},
	{"X-MAC-CE", 10029},
	{"X-MAC-CHINESEIMP", 10008},
	{"X-MAC-CHINESETRAD", 10002},
	{"X-MAC-CROATIAN", 10082},
	{"X-MAC-CYRILLIC", 10007},
	{"X-MAC-GREEK", 10006},
	{"X-MAC-HEBREW", 10005},
	{"X-MAC-ICELANDIC", 10079},
	{"X-MAC-JAPANESE", 10001},
	{"X-MAC-KOREAN", 10003},
	{"X-MAC-ROMANIAN", 10010},
	{"X-MAC-THAI", 10021},
	{"X-MAC-TURKISH", 10081},
	{"X-MAC-UKRAINIAN", 10017},
};
static unsigned
make_codepage_from_charset(const char *charset)
{
	char cs[16];
	char *p;
	unsigned cp;
	int a, b;

	if (charset == NULL || strlen(charset) > 15)
		return -1;

	/* Copy name to uppercase. */
	p = cs;
	while (*charset) {
		char c = *charset++;
		if (c >= 'a' && c <= 'z')
			c -= 'a' - 'A';
		*p++ = c;
	}
	*p++ = '\0';
	cp = -1;

	/* Look it up in the table first, so that we can easily
	 * override CP367, which we map to 1252 instead of 367. */
	a = 0;
	b = sizeof(charsets)/sizeof(charsets[0]);
	while (b > a) {
		int c = (b + a) / 2;
		int r = strcmp(charsets[c].name, cs);
		if (r < 0)
			a = c + 1;
		else if (r > 0)
			b = c;
		else
			return charsets[c].cp;
	}

	/* If it's not in the table, try to parse it. */
	switch (*cs) {
	case 'C':
		if (cs[1] == 'P' && cs[2] >= '0' && cs[2] <= '9') {
			cp = my_atoi(cs + 2);
		} else if (strcmp(cs, "CP_ACP") == 0)
			cp = get_current_codepage();
		else if (strcmp(cs, "CP_OEMCP") == 0)
			cp = get_current_oemcp();
		break;
	case 'I':
		if (cs[1] == 'B' && cs[2] == 'M' &&
		    cs[3] >= '0' && cs[3] <= '9') {
			cp = my_atoi(cs + 3);
		}
		break;
	case 'W':
		if (strncmp(cs, "WINDOWS-", 8) == 0) {
			cp = my_atoi(cs + 8);
			if (cp != 874 && (cp < 1250 || cp > 1258))
				cp = -1;/* This may invalid code. */
		}
		break;
	}
	return (cp);
}

/*
 * Return ANSI Code Page of current locale set by setlocale().
 */
static unsigned
get_current_codepage(void)
{
	char *locale, *p;
	unsigned cp;

	locale = setlocale(LC_CTYPE, NULL);
	if (locale == NULL)
		return (GetACP());
	if (locale[0] == 'C' && locale[1] == '\0')
		return (CP_C_LOCALE);
	p = strrchr(locale, '.');
	if (p == NULL)
		return (GetACP());
	if (strcmp(p+1, "utf8") == 0)
		return CP_UTF8;
	cp = my_atoi(p+1);
	if ((int)cp <= 0)
		return (GetACP());
	return (cp);
}

/*
 * Translation table between Locale Name and ACP/OEMCP.
 */
static struct {
	unsigned acp;
	unsigned ocp;
	const char *locale;
} acp_ocp_map[] = {
	{  950,  950, "Chinese_Taiwan" },
	{  936,  936, "Chinese_People's Republic of China" },
	{  950,  950, "Chinese_Taiwan" },
	{ 1250,  852, "Czech_Czech Republic" },
	{ 1252,  850, "Danish_Denmark" },
	{ 1252,  850, "Dutch_Netherlands" },
	{ 1252,  850, "Dutch_Belgium" },
	{ 1252,  437, "English_United States" },
	{ 1252,  850, "English_Australia" },
	{ 1252,  850, "English_Canada" },
	{ 1252,  850, "English_New Zealand" },
	{ 1252,  850, "English_United Kingdom" },
	{ 1252,  437, "English_United States" },
	{ 1252,  850, "Finnish_Finland" },
	{ 1252,  850, "French_France" },
	{ 1252,  850, "French_Belgium" },
	{ 1252,  850, "French_Canada" },
	{ 1252,  850, "French_Switzerland" },
	{ 1252,  850, "German_Germany" },
	{ 1252,  850, "German_Austria" },
	{ 1252,  850, "German_Switzerland" },
	{ 1253,  737, "Greek_Greece" },
	{ 1250,  852, "Hungarian_Hungary" },
	{ 1252,  850, "Icelandic_Iceland" },
	{ 1252,  850, "Italian_Italy" },
	{ 1252,  850, "Italian_Switzerland" },
	{  932,  932, "Japanese_Japan" },
	{  949,  949, "Korean_Korea" },
	{ 1252,  850, "Norwegian (BokmOl)_Norway" },
	{ 1252,  850, "Norwegian (BokmOl)_Norway" },
	{ 1252,  850, "Norwegian-Nynorsk_Norway" },
	{ 1250,  852, "Polish_Poland" },
	{ 1252,  850, "Portuguese_Portugal" },
	{ 1252,  850, "Portuguese_Brazil" },
	{ 1251,  866, "Russian_Russia" },
	{ 1250,  852, "Slovak_Slovakia" },
	{ 1252,  850, "Spanish_Spain" },
	{ 1252,  850, "Spanish_Mexico" },
	{ 1252,  850, "Spanish_Spain" },
	{ 1252,  850, "Swedish_Sweden" },
	{ 1254,  857, "Turkish_Turkey" },
	{ 0, 0, NULL}
};

/*
 * Return OEM Code Page of current locale set by setlocale().
 */
static unsigned
get_current_oemcp(void)
{
	int i;
	char *locale, *p;
	size_t len;

	locale = setlocale(LC_CTYPE, NULL);
	if (locale == NULL)
		return (GetOEMCP());
	if (locale[0] == 'C' && locale[1] == '\0')
		return (CP_C_LOCALE);

	p = strrchr(locale, '.');
	if (p == NULL)
		return (GetOEMCP());
	len = p - locale;
	for (i = 0; acp_ocp_map[i].acp; i++) {
		if (strncmp(acp_ocp_map[i].locale, locale, len) == 0)
			return (acp_ocp_map[i].ocp);
	}
	return (GetOEMCP());
}
#else

/*
 * POSIX platform does not use CodePage.
 */

static unsigned
get_current_codepage(void)
{
	return (-1);/* Unknown */
}
static unsigned
make_codepage_from_charset(const char *charset)
{
	(void)charset; /* UNUSED */
	return (-1);/* Unknown */
}
static unsigned
get_current_oemcp(void)
{
	return (-1);/* Unknown */
}

#endif /* defined(_WIN32) && !defined(__CYGWIN__) */

/*
 * Return a string conversion object.
 */
static struct archive_string_conv *
get_sconv_object(struct archive *a, const char *fc, const char *tc, int flag)
{
	struct archive_string_conv *sc;
	unsigned current_codepage;

	/* Check if we have made the sconv object. */
	sc = find_sconv_object(a, fc, tc);
	if (sc != NULL)
		return (sc);

	if (a == NULL)
		current_codepage = get_current_codepage();
	else
		current_codepage = a->current_codepage;

	sc = create_sconv_object(canonical_charset_name(fc),
	    canonical_charset_name(tc), current_codepage, flag);
	if (sc == NULL) {
		if (a != NULL)
			archive_set_error(a, ENOMEM,
			    "Could not allocate memory for "
			    "a string conversion object");
		return (NULL);
	}

	/*
	 * If there is no converter for current string conversion object,
	 * we cannot handle this conversion.
	 */
	if (sc->nconverter == 0) {
		if (a != NULL) {
#if HAVE_ICONV
			archive_set_error(a, ARCHIVE_ERRNO_MISC,
			    "iconv_open failed : Cannot handle ``%s''",
			    (flag & SCONV_TO_CHARSET)?tc:fc);
#else
			archive_set_error(a, ARCHIVE_ERRNO_MISC,
			    "A character-set conversion not fully supported "
			    "on this platform");
#endif
		}
		/* Failed; free a sconv object. */
		free_sconv_object(sc);
		return (NULL);
	}

	/*
	 * Success!
	 */
	if (a != NULL)
		add_sconv_object(a, sc);
	return (sc);
}

static const char *
get_current_charset(struct archive *a)
{
	const char *cur_charset;

	if (a == NULL)
		cur_charset = default_iconv_charset("");
	else {
		cur_charset = default_iconv_charset(a->current_code);
		if (a->current_code == NULL) {
			a->current_code = strdup(cur_charset);
			a->current_codepage = get_current_codepage();
			a->current_oemcp = get_current_oemcp();
		}
	}
	return (cur_charset);
}

/*
 * Make and Return a string conversion object.
 * Return NULL if the platform does not support the specified conversion
 * and best_effort is 0.
 * If best_effort is set, A string conversion object must be returned
 * unless memory allocation for the object fails, but the conversion
 * might fail when non-ASCII code is found.
 */
struct archive_string_conv *
archive_string_conversion_to_charset(struct archive *a, const char *charset,
    int best_effort)
{
	int flag = SCONV_TO_CHARSET;

	if (best_effort)
		flag |= SCONV_BEST_EFFORT;
	return (get_sconv_object(a, get_current_charset(a), charset, flag));
}

struct archive_string_conv *
archive_string_conversion_from_charset(struct archive *a, const char *charset,
    int best_effort)
{
	int flag = SCONV_FROM_CHARSET;

	if (best_effort)
		flag |= SCONV_BEST_EFFORT;
	return (get_sconv_object(a, charset, get_current_charset(a), flag));
}

/*
 * archive_string_default_conversion_*_archive() are provided for Windows
 * platform because other archiver application use CP_OEMCP for
 * MultiByteToWideChar() and WideCharToMultiByte() for the filenames
 * in tar or zip files. But mbstowcs/wcstombs(CRT) usually use CP_ACP
 * unless you use setlocale(LC_ALL, ".OCP")(specify CP_OEMCP).
 * So we should make a string conversion between CP_ACP and CP_OEMCP
 * for compatibility.
 */
#if defined(_WIN32) && !defined(__CYGWIN__)
struct archive_string_conv *
archive_string_default_conversion_for_read(struct archive *a)
{
	const char *cur_charset = get_current_charset(a);
	char oemcp[16];

	/* NOTE: a check of cur_charset is unneeded but we need
	 * that get_current_charset() has been surely called at
	 * this time whatever C compiler optimized. */
	if (cur_charset != NULL &&
	    (a->current_codepage == CP_C_LOCALE ||
	     a->current_codepage == a->current_oemcp))
		return (NULL);/* no conversion. */

	_snprintf(oemcp, sizeof(oemcp)-1, "CP%d", a->current_oemcp);
	/* Make sure a null termination must be set. */
	oemcp[sizeof(oemcp)-1] = '\0';
	return (get_sconv_object(a, oemcp, cur_charset,
	    SCONV_FROM_CHARSET));
}

struct archive_string_conv *
archive_string_default_conversion_for_write(struct archive *a)
{
	const char *cur_charset = get_current_charset(a);
	char oemcp[16];

	/* NOTE: a check of cur_charset is unneeded but we need
	 * that get_current_charset() has been surely called at
	 * this time whatever C compiler optimized. */
	if (cur_charset != NULL &&
	    (a->current_codepage == CP_C_LOCALE ||
	     a->current_codepage == a->current_oemcp))
		return (NULL);/* no conversion. */

	_snprintf(oemcp, sizeof(oemcp)-1, "CP%d", a->current_oemcp);
	/* Make sure a null termination must be set. */
	oemcp[sizeof(oemcp)-1] = '\0';
	return (get_sconv_object(a, cur_charset, oemcp,
	    SCONV_TO_CHARSET));
}
#else
struct archive_string_conv *
archive_string_default_conversion_for_read(struct archive *a)
{
	(void)a; /* UNUSED */
	return (NULL);
}

struct archive_string_conv *
archive_string_default_conversion_for_write(struct archive *a)
{
	(void)a; /* UNUSED */
	return (NULL);
}
#endif

/*
 * Dispose of all character conversion objects in the archive object.
 */
void
archive_string_conversion_free(struct archive *a)
{
	struct archive_string_conv *sc; 
	struct archive_string_conv *sc_next; 

	for (sc = a->sconv; sc != NULL; sc = sc_next) {
		sc_next = sc->next;
		free_sconv_object(sc);
	}
	a->sconv = NULL;
	free(a->current_code);
	a->current_code = NULL;
}

/*
 * Return a conversion charset name.
 */
const char *
archive_string_conversion_charset_name(struct archive_string_conv *sc)
{
	if (sc->flag & SCONV_TO_CHARSET)
		return (sc->to_charset);
	else
		return (sc->from_charset);
}

/*
 * Change the behavior of a string conversion.
 */
void
archive_string_conversion_set_opt(struct archive_string_conv *sc, int opt)
{
	switch (opt) {
	/*
	 * A filename in UTF-8 was made with libarchive 2.x in a wrong
	 * assumption that wchar_t was Unicode.
	 * This option enables simulating the assumption in order to read
	 * that filename correctly.
	 */
	case SCONV_SET_OPT_UTF8_LIBARCHIVE2X:
#if (defined(_WIN32) && !defined(__CYGWIN__)) \
	 || defined(__STDC_ISO_10646__) || defined(__APPLE__)
		/*
		 * Nothing to do for it since wchar_t on these platforms
		 * is really Unicode.
		 */
		(void)sc; /* UNUSED */
#else
		if ((sc->flag & SCONV_UTF8_LIBARCHIVE_2) == 0) {
			sc->flag |= SCONV_UTF8_LIBARCHIVE_2;
			/* Set up string converters. */
			setup_converter(sc);
		}
#endif
		break;
	case SCONV_SET_OPT_NORMALIZATION_C:
		if ((sc->flag & SCONV_NORMALIZATION_C) == 0) {
			sc->flag |= SCONV_NORMALIZATION_C;
			sc->flag &= ~SCONV_NORMALIZATION_D;
			/* Set up string converters. */
			setup_converter(sc);
		}
		break;
	case SCONV_SET_OPT_NORMALIZATION_D:
#if defined(HAVE_ICONV)
		/*
		 * If iconv will take the string, do not change the
		 * setting of the normalization.
		 */
		if (!(sc->flag & SCONV_WIN_CP) &&
		     (sc->flag & (SCONV_FROM_UTF16 | SCONV_FROM_UTF8)) &&
		    !(sc->flag & (SCONV_TO_UTF16 | SCONV_TO_UTF8)))
			break;
#endif
		if ((sc->flag & SCONV_NORMALIZATION_D) == 0) {
			sc->flag |= SCONV_NORMALIZATION_D;
			sc->flag &= ~SCONV_NORMALIZATION_C;
			/* Set up string converters. */
			setup_converter(sc);
		}
		break;
	default:
		break;
	}
}

/*
 *
 * Copy one archive_string to another in locale conversion.
 *
 *	archive_strncat_l();
 *	archive_strncpy_l();
 *
 */

static size_t
mbsnbytes(const void *_p, size_t n)
{
	size_t s;
	const char *p, *pp;

	if (_p == NULL)
		return (0);
	p = (const char *)_p;

	/* Like strlen(p), except won't examine positions beyond p[n]. */
	s = 0;
	pp = p;
	while (s < n && *pp) {
		pp++;
		s++;
	}
	return (s);
}

static size_t
utf16nbytes(const void *_p, size_t n)
{
	size_t s;
	const char *p, *pp;

	if (_p == NULL)
		return (0);
	p = (const char *)_p;

	/* Like strlen(p), except won't examine positions beyond p[n]. */
	s = 0;
	pp = p;
	n >>= 1;
	while (s < n && (pp[0] || pp[1])) {
		pp += 2;
		s++;
	}
	return (s<<1);
}

int
archive_strncpy_l(struct archive_string *as, const void *_p, size_t n,
    struct archive_string_conv *sc)
{
	as->length = 0;
	return (archive_strncat_l(as, _p, n, sc));
}

int
archive_strncat_l(struct archive_string *as, const void *_p, size_t n,
    struct archive_string_conv *sc)
{
	const void *s;
	size_t length = 0;
	int i, r = 0, r2;

	if (_p != NULL && n > 0) {
		if (sc != NULL && (sc->flag & SCONV_FROM_UTF16))
			length = utf16nbytes(_p, n);
		else
			length = mbsnbytes(_p, n);
	}

	/* We must allocate memory even if there is no data for conversion
	 * or copy. This simulates archive_string_append behavior. */
	if (length == 0) {
		int tn = 1;
		if (sc != NULL && (sc->flag & SCONV_TO_UTF16))
			tn = 2;
		if (archive_string_ensure(as, as->length + tn) == NULL)
			return (-1);
		as->s[as->length] = 0;
		if (tn == 2)
			as->s[as->length+1] = 0;
		return (0);
	}

	/*
	 * If sc is NULL, we just make a copy.
	 */
	if (sc == NULL) {
		if (archive_string_append(as, _p, length) == NULL)
			return (-1);/* No memory */
		return (0);
	}

	s = _p;
	i = 0;
	if (sc->nconverter > 1) {
		sc->utftmp.length = 0;
		r2 = sc->converter[0](&(sc->utftmp), s, length, sc);
		if (r2 != 0 && errno == ENOMEM)
			return (r2);
		if (r > r2)
			r = r2;
		s = sc->utftmp.s;
		length = sc->utftmp.length;
		++i;
	}
	r2 = sc->converter[i](as, s, length, sc);
	if (r > r2)
		r = r2;
	return (r);
}

#if HAVE_ICONV

/*
 * Return -1 if conversion fails.
 */
static int
iconv_strncat_in_locale(struct archive_string *as, const void *_p,
    size_t length, struct archive_string_conv *sc)
{
	ICONV_CONST char *itp;
	size_t remaining;
	iconv_t cd;
	char *outp;
	size_t avail, bs;
	int return_value = 0; /* success */
	int to_size, from_size;

	if (sc->flag & SCONV_TO_UTF16)
		to_size = 2;
	else
		to_size = 1;
	if (sc->flag & SCONV_FROM_UTF16)
		from_size = 2;
	else
		from_size = 1;

	if (archive_string_ensure(as, as->length + length*2+to_size) == NULL)
		return (-1);

	cd = sc->cd;
	itp = (char *)(uintptr_t)_p;
	remaining = length;
	outp = as->s + as->length;
	avail = as->buffer_length - as->length - to_size;
	while (remaining >= (size_t)from_size) {
		size_t result = iconv(cd, &itp, &remaining, &outp, &avail);

		if (result != (size_t)-1)
			break; /* Conversion completed. */

		if (errno == EILSEQ || errno == EINVAL) {
			/*
		 	 * If an output charset is UTF-8 or UTF-16BE/LE,
			 * unknown character should be U+FFFD
			 * (replacement character).
			 */
			if (sc->flag & (SCONV_TO_UTF8 | SCONV_TO_UTF16)) {
				size_t rbytes;
				if (sc->flag & SCONV_TO_UTF8)
					rbytes = sizeof(utf8_replacement_char);
				else
					rbytes = 2;

				if (avail < rbytes) {
					as->length = outp - as->s;
					bs = as->buffer_length +
					    (remaining * to_size) + rbytes;
					if (NULL ==
					    archive_string_ensure(as, bs))
						return (-1);
					outp = as->s + as->length;
					avail = as->buffer_length
					    - as->length - to_size;
				}
				if (sc->flag & SCONV_TO_UTF8)
					memcpy(outp, utf8_replacement_char, sizeof(utf8_replacement_char));
				else if (sc->flag & SCONV_TO_UTF16BE)
					archive_be16enc(outp, UNICODE_R_CHAR);
				else
					archive_le16enc(outp, UNICODE_R_CHAR);
				outp += rbytes;
				avail -= rbytes;
			} else {
				/* Skip the illegal input bytes. */
				*outp++ = '?';
				avail--;
			}
			itp += from_size;
			remaining -= from_size;
			return_value = -1; /* failure */
		} else {
			/* E2BIG no output buffer,
			 * Increase an output buffer.  */
			as->length = outp - as->s;
			bs = as->buffer_length + remaining * 2;
			if (NULL == archive_string_ensure(as, bs))
				return (-1);
			outp = as->s + as->length;
			avail = as->buffer_length - as->length - to_size;
		}
	}
	as->length = outp - as->s;
	as->s[as->length] = 0;
	if (to_size == 2)
		as->s[as->length+1] = 0;
	return (return_value);
}

#endif /* HAVE_ICONV */


#if defined(_WIN32) && !defined(__CYGWIN__)

/*
 * Translate a string from a some CodePage to an another CodePage by
 * Windows APIs, and copy the result. Return -1 if conversion fails.
 */
static int
strncat_in_codepage(struct archive_string *as,
    const void *_p, size_t length, struct archive_string_conv *sc)
{
	const char *s = (const char *)_p;
	struct archive_wstring aws;
	size_t l;
	int r, saved_flag;

	archive_string_init(&aws);
	saved_flag = sc->flag;
	sc->flag &= ~(SCONV_NORMALIZATION_D | SCONV_NORMALIZATION_C);
	r = archive_wstring_append_from_mbs_in_codepage(&aws, s, length, sc);
	sc->flag = saved_flag;
	if (r != 0) {
		archive_wstring_free(&aws);
		if (errno != ENOMEM)
			archive_string_append(as, s, length);
		return (-1);
	}

	l = as->length;
	r = archive_string_append_from_wcs_in_codepage(
	    as, aws.s, aws.length, sc);
	if (r != 0 && errno != ENOMEM && l == as->length)
		archive_string_append(as, s, length);
	archive_wstring_free(&aws);
	return (r);
}

/*
 * Test whether MBS ==> WCS is okay.
 */
static int
invalid_mbs(const void *_p, size_t n, struct archive_string_conv *sc)
{
	const char *p = (const char *)_p;
	unsigned codepage;
	DWORD mbflag = MB_ERR_INVALID_CHARS;

	if (sc->flag & SCONV_FROM_CHARSET)
		codepage = sc->to_cp;
	else
		codepage = sc->from_cp;

	if (codepage == CP_C_LOCALE)
		return (0);
	if (codepage != CP_UTF8)
		mbflag |= MB_PRECOMPOSED;

	if (MultiByteToWideChar(codepage, mbflag, p, (int)n, NULL, 0) == 0)
		return (-1); /* Invalid */
	return (0); /* Okay */
}

#else

/*
 * Test whether MBS ==> WCS is okay.
 */
static int
invalid_mbs(const void *_p, size_t n, struct archive_string_conv *sc)
{
	const char *p = (const char *)_p;
	size_t r;

#if HAVE_MBRTOWC
	mbstate_t shift_state;

	memset(&shift_state, 0, sizeof(shift_state));
#else
	/* Clear the shift state before starting. */
	mbtowc(NULL, NULL, 0);
#endif
	while (n) {
		wchar_t wc;

#if HAVE_MBRTOWC
		r = mbrtowc(&wc, p, n, &shift_state);
#else
		r = mbtowc(&wc, p, n);
#endif
		if (r == (size_t)-1 || r == (size_t)-2)
			return (-1);/* Invalid. */
		if (r == 0)
			break;
		p += r;
		n -= r;
	}
	(void)sc; /* UNUSED */
	return (0); /* All Okey. */
}

#endif /* defined(_WIN32) && !defined(__CYGWIN__) */

/*
 * Basically returns -1 because we cannot make a conversion of charset
 * without iconv but in some cases this would return 0.
 * Returns 0 if all copied characters are ASCII.
 * Returns 0 if both from-locale and to-locale are the same and those
 * can be WCS with no error.
 */
static int
best_effort_strncat_in_locale(struct archive_string *as, const void *_p,
    size_t length, struct archive_string_conv *sc)
{
	size_t remaining;
	const uint8_t *itp;
	int return_value = 0; /* success */

	/*
	 * If both from-locale and to-locale is the same, this makes a copy.
	 * And then this checks all copied MBS can be WCS if so returns 0.
	 */
	if (sc->same) {
		if (archive_string_append(as, _p, length) == NULL)
			return (-1);/* No memory */
		return (invalid_mbs(_p, length, sc));
	}

	/*
	 * If a character is ASCII, this just copies it. If not, this
	 * assigns '?' character instead but in UTF-8 locale this assigns
	 * byte sequence 0xEF 0xBD 0xBD, which are code point U+FFFD,
	 * a Replacement Character in Unicode.
	 */

	remaining = length;
	itp = (const uint8_t *)_p;
	while (*itp && remaining > 0) {
		if (*itp > 127) {
			// Non-ASCII: Substitute with suitable replacement
			if (sc->flag & SCONV_TO_UTF8) {
				if (archive_string_append(as, utf8_replacement_char, sizeof(utf8_replacement_char)) == NULL) {
					__archive_errx(1, "Out of memory");
				}
			} else {
				archive_strappend_char(as, '?');
			}
			return_value = -1;
		} else {
			archive_strappend_char(as, *itp);
		}
		++itp;
	}
	return (return_value);
}


/*
 * Unicode conversion functions.
 *   - UTF-8 <===> UTF-8 in removing surrogate pairs.
 *   - UTF-8 NFD ===> UTF-8 NFC in removing surrogate pairs.
 *   - UTF-8 made by libarchive 2.x ===> UTF-8.
 *   - UTF-16BE <===> UTF-8.
 *
 */

/*
 * Utility to convert a single UTF-8 sequence.
 *
 * Usually return used bytes, return used byte in negative value when
 * a unicode character is replaced with U+FFFD.
 * See also http://unicode.org/review/pr-121.html Public Review Issue #121
 * Recommended Practice for Replacement Characters.
 */
static int
_utf8_to_unicode(uint32_t *pwc, const char *s, size_t n)
{
	static const char utf8_count[256] = {
		 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,/* 00 - 0F */
		 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,/* 10 - 1F */
		 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,/* 20 - 2F */
		 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,/* 30 - 3F */
		 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,/* 40 - 4F */
		 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,/* 50 - 5F */
		 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,/* 60 - 6F */
		 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,/* 70 - 7F */
		 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,/* 80 - 8F */
		 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,/* 90 - 9F */
		 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,/* A0 - AF */
		 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,/* B0 - BF */
		 0, 0, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,/* C0 - CF */
		 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,/* D0 - DF */
		 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,/* E0 - EF */
		 4, 4, 4, 4, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 /* F0 - FF */
	};
	int ch, i;
	int cnt;
	uint32_t wc;

	/* Sanity check. */
	if (n == 0)
		return (0);
	/*
	 * Decode 1-4 bytes depending on the value of the first byte.
	 */
	ch = (unsigned char)*s;
	if (ch == 0)
		return (0); /* Standard:  return 0 for end-of-string. */
	cnt = utf8_count[ch];

	/* Invalid sequence or there are not plenty bytes. */
	if ((int)n < cnt) {
		cnt = (int)n;
		for (i = 1; i < cnt; i++) {
			if ((s[i] & 0xc0) != 0x80) {
				cnt = i;
				break;
			}
		}
		goto invalid_sequence;
	}

	/* Make a Unicode code point from a single UTF-8 sequence. */
	switch (cnt) {
	case 1:	/* 1 byte sequence. */
		*pwc = ch & 0x7f;
		return (cnt);
	case 2:	/* 2 bytes sequence. */
		if ((s[1] & 0xc0) != 0x80) {
			cnt = 1;
			goto invalid_sequence;
		}
		*pwc = ((ch & 0x1f) << 6) | (s[1] & 0x3f);
		return (cnt);
	case 3:	/* 3 bytes sequence. */
		if ((s[1] & 0xc0) != 0x80) {
			cnt = 1;
			goto invalid_sequence;
		}
		if ((s[2] & 0xc0) != 0x80) {
			cnt = 2;
			goto invalid_sequence;
		}
		wc = ((ch & 0x0f) << 12)
		    | ((s[1] & 0x3f) << 6)
		    | (s[2] & 0x3f);
		if (wc < 0x800)
			goto invalid_sequence;/* Overlong sequence. */
		break;
	case 4:	/* 4 bytes sequence. */
		if ((s[1] & 0xc0) != 0x80) {
			cnt = 1;
			goto invalid_sequence;
		}
		if ((s[2] & 0xc0) != 0x80) {
			cnt = 2;
			goto invalid_sequence;
		}
		if ((s[3] & 0xc0) != 0x80) {
			cnt = 3;
			goto invalid_sequence;
		}
		wc = ((ch & 0x07) << 18)
		    | ((s[1] & 0x3f) << 12)
		    | ((s[2] & 0x3f) << 6)
		    | (s[3] & 0x3f);
		if (wc < 0x10000)
			goto invalid_sequence;/* Overlong sequence. */
		break;
	default: /* Others are all invalid sequence. */
		if (ch == 0xc0 || ch == 0xc1)
			cnt = 2;
		else if (ch >= 0xf5 && ch <= 0xf7)
			cnt = 4;
		else if (ch >= 0xf8 && ch <= 0xfb)
			cnt = 5;
		else if (ch == 0xfc || ch == 0xfd)
			cnt = 6;
		else
			cnt = 1;
		if ((int)n < cnt)
			cnt = (int)n;
		for (i = 1; i < cnt; i++) {
			if ((s[i] & 0xc0) != 0x80) {
				cnt = i;
				break;
			}
		}
		goto invalid_sequence;
	}

	/* The code point larger than 0x10FFFF is not legal
	 * Unicode values. */
	if (wc > UNICODE_MAX)
		goto invalid_sequence;
	/* Correctly gets a Unicode, returns used bytes. */
	*pwc = wc;
	return (cnt);
invalid_sequence:
	*pwc = UNICODE_R_CHAR;/* set the Replacement Character instead. */
	return (cnt * -1);
}

static int
utf8_to_unicode(uint32_t *pwc, const char *s, size_t n)
{
	int cnt;

	cnt = _utf8_to_unicode(pwc, s, n);
	/* Any of Surrogate pair is not legal Unicode values. */
	if (cnt == 3 && IS_SURROGATE_PAIR_LA(*pwc))
		return (-3);
	return (cnt);
}

static inline uint32_t
combine_surrogate_pair(uint32_t uc, uint32_t uc2)
{
	uc -= 0xD800;
	uc *= 0x400;
	uc += uc2 - 0xDC00;
	uc += 0x10000;
	return (uc);
}

/*
 * Convert a single UTF-8/CESU-8 sequence to a Unicode code point in
 * removing surrogate pairs.
 *
 * CESU-8: The Compatibility Encoding Scheme for UTF-16.
 *
 * Usually return used bytes, return used byte in negative value when
 * a unicode character is replaced with U+FFFD.
 */
static int
cesu8_to_unicode(uint32_t *pwc, const char *s, size_t n)
{
	uint32_t wc = 0;
	int cnt;

	cnt = _utf8_to_unicode(&wc, s, n);
	if (cnt == 3 && IS_HIGH_SURROGATE_LA(wc)) {
		uint32_t wc2 = 0;
		if (n - 3 < 3) {
			/* Invalid byte sequence. */
			goto invalid_sequence;
		}
		cnt = _utf8_to_unicode(&wc2, s+3, n-3);
		if (cnt != 3 || !IS_LOW_SURROGATE_LA(wc2)) {
			/* Invalid byte sequence. */
			goto invalid_sequence;
		}
		wc = combine_surrogate_pair(wc, wc2);
		cnt = 6;
	} else if (cnt == 3 && IS_LOW_SURROGATE_LA(wc)) {
		/* Invalid byte sequence. */
		goto invalid_sequence;
	}
	*pwc = wc;
	return (cnt);
invalid_sequence:
	*pwc = UNICODE_R_CHAR;/* set the Replacement Character instead. */
	if (cnt > 0)
		cnt *= -1;
	return (cnt);
}

/*
 * Convert a Unicode code point to a single UTF-8 sequence.
 *
 * NOTE:This function does not check if the Unicode is legal or not.
 * Please you definitely check it before calling this.
 */
static size_t
unicode_to_utf8(char *p, size_t remaining, uint32_t uc)
{
	char *_p = p;

	/* Invalid Unicode char maps to Replacement character */
	if (uc > UNICODE_MAX)
		uc = UNICODE_R_CHAR;
	/* Translate code point to UTF8 */
	if (uc <= 0x7f) {
		if (remaining == 0)
			return (0);
		*p++ = (char)uc;
	} else if (uc <= 0x7ff) {
		if (remaining < 2)
			return (0);
		*p++ = 0xc0 | ((uc >> 6) & 0x1f);
		*p++ = 0x80 | (uc & 0x3f);
	} else if (uc <= 0xffff) {
		if (remaining < 3)
			return (0);
		*p++ = 0xe0 | ((uc >> 12) & 0x0f);
		*p++ = 0x80 | ((uc >> 6) & 0x3f);
		*p++ = 0x80 | (uc & 0x3f);
	} else {
		if (remaining < 4)
			return (0);
		*p++ = 0xf0 | ((uc >> 18) & 0x07);
		*p++ = 0x80 | ((uc >> 12) & 0x3f);
		*p++ = 0x80 | ((uc >> 6) & 0x3f);
		*p++ = 0x80 | (uc & 0x3f);
	}
	return (p - _p);
}

static int
utf16be_to_unicode(uint32_t *pwc, const char *s, size_t n)
{
	return (utf16_to_unicode(pwc, s, n, 1));
}

static int
utf16le_to_unicode(uint32_t *pwc, const char *s, size_t n)
{
	return (utf16_to_unicode(pwc, s, n, 0));
}

static int
utf16_to_unicode(uint32_t *pwc, const char *s, size_t n, int be)
{
	const char *utf16 = s;
	unsigned uc;

	if (n == 0)
		return (0);
	if (n == 1) {
		/* set the Replacement Character instead. */
		*pwc = UNICODE_R_CHAR;
		return (-1);
	}

	if (be)
		uc = archive_be16dec(utf16);
	else
		uc = archive_le16dec(utf16);
	utf16 += 2;
		
	/* If this is a surrogate pair, assemble the full code point.*/
	if (IS_HIGH_SURROGATE_LA(uc)) {
		unsigned uc2;

		if (n >= 4) {
			if (be)
				uc2 = archive_be16dec(utf16);
			else
				uc2 = archive_le16dec(utf16);
		} else
			uc2 = 0;
		if (IS_LOW_SURROGATE_LA(uc2)) {
			uc = combine_surrogate_pair(uc, uc2);
			utf16 += 2;
		} else {
	 		/* Undescribed code point should be U+FFFD
		 	* (replacement character). */
			*pwc = UNICODE_R_CHAR;
			return (-2);
		}
	}

	/*
	 * Surrogate pair values(0xd800 through 0xdfff) are only
	 * used by UTF-16, so, after above calculation, the code
	 * must not be surrogate values, and Unicode has no codes
	 * larger than 0x10ffff. Thus, those are not legal Unicode
	 * values.
	 */
	if (IS_SURROGATE_PAIR_LA(uc) || uc > UNICODE_MAX) {
	 	/* Undescribed code point should be U+FFFD
	 	* (replacement character). */
		*pwc = UNICODE_R_CHAR;
		return (((int)(utf16 - s)) * -1);
	}
	*pwc = uc;
	return ((int)(utf16 - s));
}

static size_t
unicode_to_utf16be(char *p, size_t remaining, uint32_t uc)
{
	char *utf16 = p;

	if (uc > 0xffff) {
		/* We have a code point that won't fit into a
		 * wchar_t; convert it to a surrogate pair. */
		if (remaining < 4)
			return (0);
		uc -= 0x10000;
		archive_be16enc(utf16, ((uc >> 10) & 0x3ff) + 0xD800);
		archive_be16enc(utf16+2, (uc & 0x3ff) + 0xDC00);
		return (4);
	} else {
		if (remaining < 2)
			return (0);
		archive_be16enc(utf16, uc);
		return (2);
	}
}

static size_t
unicode_to_utf16le(char *p, size_t remaining, uint32_t uc)
{
	char *utf16 = p;

	if (uc > 0xffff) {
		/* We have a code point that won't fit into a
		 * wchar_t; convert it to a surrogate pair. */
		if (remaining < 4)
			return (0);
		uc -= 0x10000;
		archive_le16enc(utf16, ((uc >> 10) & 0x3ff) + 0xD800);
		archive_le16enc(utf16+2, (uc & 0x3ff) + 0xDC00);
		return (4);
	} else {
		if (remaining < 2)
			return (0);
		archive_le16enc(utf16, uc);
		return (2);
	}
}

/*
 * Copy UTF-8 string in checking surrogate pair.
 * If any surrogate pair are found, it would be canonicalized.
 */
static int
strncat_from_utf8_to_utf8(struct archive_string *as, const void *_p,
    size_t len, struct archive_string_conv *sc)
{
	const char *s;
	char *p, *endp;
	int n, ret = 0;

	(void)sc; /* UNUSED */

	if (archive_string_ensure(as, as->length + len + 1) == NULL)
		return (-1);

	s = (const char *)_p;
	p = as->s + as->length;
	endp = as->s + as->buffer_length -1;
	do {
		uint32_t uc;
		const char *ss = s;
		size_t w;

		/*
		 * Forward byte sequence until a conversion of that is needed.
		 */
		while ((n = utf8_to_unicode(&uc, s, len)) > 0) {
			s += n;
			len -= n;
		}
		if (ss < s) {
			if (p + (s - ss) > endp) {
				as->length = p - as->s;
				if (archive_string_ensure(as,
				    as->buffer_length + len + 1) == NULL)
					return (-1);
				p = as->s + as->length;
				endp = as->s + as->buffer_length -1;
			}

			memcpy(p, ss, s - ss);
			p += s - ss;
		}

		/*
		 * If n is negative, current byte sequence needs a replacement.
		 */
		if (n < 0) {
			if (n == -3 && IS_SURROGATE_PAIR_LA(uc)) {
				/* Current byte sequence may be CESU-8. */
				n = cesu8_to_unicode(&uc, s, len);
			}
			if (n < 0) {
				ret = -1;
				n *= -1;/* Use a replaced unicode character. */
			}

			/* Rebuild UTF-8 byte sequence. */
			while ((w = unicode_to_utf8(p, endp - p, uc)) == 0) {
				as->length = p - as->s;
				if (archive_string_ensure(as,
				    as->buffer_length + len + 1) == NULL)
					return (-1);
				p = as->s + as->length;
				endp = as->s + as->buffer_length -1;
			}
			p += w;
			s += n;
			len -= n;
		}
	} while (n > 0);
	as->length = p - as->s;
	as->s[as->length] = '\0';
	return (ret);
}

static int
archive_string_append_unicode(struct archive_string *as, const void *_p,
    size_t len, struct archive_string_conv *sc)
{
	const char *s;
	char *p, *endp;
	uint32_t uc;
	size_t w;
	int n, ret = 0, ts, tm;
	int (*parse)(uint32_t *, const char *, size_t);
	size_t (*unparse)(char *, size_t, uint32_t);

	if (sc->flag & SCONV_TO_UTF16BE) {
		unparse = unicode_to_utf16be;
		ts = 2;
	} else if (sc->flag & SCONV_TO_UTF16LE) {
		unparse = unicode_to_utf16le;
		ts = 2;
	} else if (sc->flag & SCONV_TO_UTF8) {
		unparse = unicode_to_utf8;
		ts = 1;
	} else {
		/*
		 * This case is going to be converted to another
		 * character-set through iconv.
		 */
		if (sc->flag & SCONV_FROM_UTF16BE) {
			unparse = unicode_to_utf16be;
			ts = 2;
		} else if (sc->flag & SCONV_FROM_UTF16LE) {
			unparse = unicode_to_utf16le;
			ts = 2;
		} else {
			unparse = unicode_to_utf8;
			ts = 1;
		}
	}

	if (sc->flag & SCONV_FROM_UTF16BE) {
		parse = utf16be_to_unicode;
		tm = 1;
	} else if (sc->flag & SCONV_FROM_UTF16LE) {
		parse = utf16le_to_unicode;
		tm = 1;
	} else {
		parse = cesu8_to_unicode;
		tm = ts;
	}

	if (archive_string_ensure(as, as->length + len * tm + ts) == NULL)
		return (-1);

	s = (const char *)_p;
	p = as->s + as->length;
	endp = as->s + as->buffer_length - ts;
	while ((n = parse(&uc, s, len)) != 0) {
		if (n < 0) {
			/* Use a replaced unicode character. */
			n *= -1;
			ret = -1;
		}
		s += n;
		len -= n;
		while ((w = unparse(p, endp - p, uc)) == 0) {
			/* There is not enough output buffer so
			 * we have to expand it. */
			as->length = p - as->s;
			if (archive_string_ensure(as,
			    as->buffer_length + len * tm + ts) == NULL)
				return (-1);
			p = as->s + as->length;
			endp = as->s + as->buffer_length - ts;
		}
		p += w;
	}
	as->length = p - as->s;
	as->s[as->length] = '\0';
	if (ts == 2)
		as->s[as->length+1] = '\0';
	return (ret);
}

/*
 * Following Constants for Hangul compositions this information comes from
 * Unicode Standard Annex #15  http://unicode.org/reports/tr15/
 */
#define HC_SBASE	0xAC00
#define HC_LBASE	0x1100
#define HC_VBASE	0x1161
#define HC_TBASE	0x11A7
#define HC_LCOUNT	19
#define HC_VCOUNT	21
#define HC_TCOUNT	28
#define HC_NCOUNT	(HC_VCOUNT * HC_TCOUNT)
#define HC_SCOUNT	(HC_LCOUNT * HC_NCOUNT)

static uint32_t
get_nfc(uint32_t uc, uint32_t uc2)
{
	int t, b;

	t = 0;
	b = sizeof(u_composition_table)/sizeof(u_composition_table[0]) -1;
	while (b >= t) {
		int m = (t + b) / 2;
		if (u_composition_table[m].cp1 < uc)
			t = m + 1;
		else if (u_composition_table[m].cp1 > uc)
			b = m - 1;
		else if (u_composition_table[m].cp2 < uc2)
			t = m + 1;
		else if (u_composition_table[m].cp2 > uc2)
			b = m - 1;
		else
			return (u_composition_table[m].nfc);
	}
	return (0);
}

#define FDC_MAX 10	/* The maximum number of Following Decomposable
			 * Characters. */

/*
 * Update first code point.
 */
#define UPDATE_UC(new_uc)	do {		\
	uc = new_uc;				\
	ucptr = NULL;				\
} while (0)

/*
 * Replace first code point with second code point.
 */
#define REPLACE_UC_WITH_UC2() do {		\
	uc = uc2;				\
	ucptr = uc2ptr;				\
	n = n2;					\
} while (0)

#define EXPAND_BUFFER() do {			\
	as->length = p - as->s;			\
	if (archive_string_ensure(as,		\
	    as->buffer_length + len * tm + ts) == NULL)\
		return (-1);			\
	p = as->s + as->length;			\
	endp = as->s + as->buffer_length - ts;	\
} while (0)

#define UNPARSE(p, endp, uc)	do {		\
	while ((w = unparse(p, (endp) - (p), uc)) == 0) {\
		EXPAND_BUFFER();		\
	}					\
	p += w;					\
} while (0)

/*
 * Write first code point.
 * If the code point has not be changed from its original code,
 * this just copies it from its original buffer pointer.
 * If not, this converts it to UTF-8 byte sequence and copies it.
 */
#define WRITE_UC()	do {			\
	if (ucptr) {				\
		if (p + n > endp)		\
			EXPAND_BUFFER();	\
		switch (n) {			\
		case 4:				\
			*p++ = *ucptr++;	\
			/* FALL THROUGH */	\
		case 3:				\
			*p++ = *ucptr++;	\
			/* FALL THROUGH */	\
		case 2:				\
			*p++ = *ucptr++;	\
			/* FALL THROUGH */	\
		case 1:				\
			*p++ = *ucptr;		\
			break;			\
		}				\
		ucptr = NULL;			\
	} else {				\
		UNPARSE(p, endp, uc);		\
	}					\
} while (0)

/*
 * Collect following decomposable code points.
 */
#define COLLECT_CPS(start)	do {		\
	int _i;					\
	for (_i = start; _i < FDC_MAX ; _i++) {	\
		nx = parse(&ucx[_i], s, len);	\
		if (nx <= 0)			\
			break;			\
		cx = CCC(ucx[_i]);		\
		if (cl >= cx && cl != 228 && cx != 228)\
			break;			\
		s += nx;			\
		len -= nx;			\
		cl = cx;			\
		ccx[_i] = cx;			\
	}					\
	if (_i >= FDC_MAX) {			\
		ret = -1;			\
		ucx_size = FDC_MAX;		\
	} else					\
		ucx_size = _i;			\
} while (0)

/*
 * Normalize UTF-8/UTF-16BE characters to Form C and copy the result.
 *
 * TODO: Convert composition exclusions, which are never converted
 * from NFC,NFD,NFKC and NFKD, to Form C.
 */
static int
archive_string_normalize_C(struct archive_string *as, const void *_p,
    size_t len, struct archive_string_conv *sc)
{
	const char *s = (const char *)_p;
	char *p, *endp;
	uint32_t uc, uc2;
	size_t w;
	int always_replace, n, n2, ret = 0, spair, ts, tm;
	int (*parse)(uint32_t *, const char *, size_t);
	size_t (*unparse)(char *, size_t, uint32_t);

	always_replace = 1;
	ts = 1;/* text size. */
	if (sc->flag & SCONV_TO_UTF16BE) {
		unparse = unicode_to_utf16be;
		ts = 2;
		if (sc->flag & SCONV_FROM_UTF16BE)
			always_replace = 0;
	} else if (sc->flag & SCONV_TO_UTF16LE) {
		unparse = unicode_to_utf16le;
		ts = 2;
		if (sc->flag & SCONV_FROM_UTF16LE)
			always_replace = 0;
	} else if (sc->flag & SCONV_TO_UTF8) {
		unparse = unicode_to_utf8;
		if (sc->flag & SCONV_FROM_UTF8)
			always_replace = 0;
	} else {
		/*
		 * This case is going to be converted to another
		 * character-set through iconv.
		 */
		always_replace = 0;
		if (sc->flag & SCONV_FROM_UTF16BE) {
			unparse = unicode_to_utf16be;
			ts = 2;
		} else if (sc->flag & SCONV_FROM_UTF16LE) {
			unparse = unicode_to_utf16le;
			ts = 2;
		} else {
			unparse = unicode_to_utf8;
		}
	}

	if (sc->flag & SCONV_FROM_UTF16BE) {
		parse = utf16be_to_unicode;
		tm = 1;
		spair = 4;/* surrogate pair size in UTF-16. */
	} else if (sc->flag & SCONV_FROM_UTF16LE) {
		parse = utf16le_to_unicode;
		tm = 1;
		spair = 4;/* surrogate pair size in UTF-16. */
	} else {
		parse = cesu8_to_unicode;
		tm = ts;
		spair = 6;/* surrogate pair size in UTF-8. */
	}

	if (archive_string_ensure(as, as->length + len * tm + ts) == NULL)
		return (-1);

	p = as->s + as->length;
	endp = as->s + as->buffer_length - ts;
	while ((n = parse(&uc, s, len)) != 0) {
		const char *ucptr, *uc2ptr;

		if (n < 0) {
			/* Use a replaced unicode character. */
			UNPARSE(p, endp, uc);
			s += n*-1;
			len -= n*-1;
			ret = -1;
			continue;
		} else if (n == spair || always_replace)
			/* uc is converted from a surrogate pair.
			 * this should be treated as a changed code. */
			ucptr = NULL;
		else
			ucptr = s;
		s += n;
		len -= n;

		/* Read second code point. */
		while ((n2 = parse(&uc2, s, len)) > 0) {
			uint32_t ucx[FDC_MAX];
			int ccx[FDC_MAX];
			int cl, cx, i, nx, ucx_size;
			int LIndex,SIndex;
			uint32_t nfc;

			if (n2 == spair || always_replace)
				/* uc2 is converted from a surrogate pair.
			 	 * this should be treated as a changed code. */
				uc2ptr = NULL;
			else
				uc2ptr = s;
			s += n2;
			len -= n2;

			/*
			 * If current second code point is out of decomposable
			 * code points, finding compositions is unneeded.
			 */
			if (!IS_DECOMPOSABLE_BLOCK(uc2)) {
				WRITE_UC();
				REPLACE_UC_WITH_UC2();
				continue;
			}

			/*
			 * Try to combine current code points.
			 */
			/*
			 * We have to combine Hangul characters according to
			 * http://uniicode.org/reports/tr15/#Hangul
			 */
			if (0 <= (LIndex = uc - HC_LBASE) &&
			    LIndex < HC_LCOUNT) {
				/*
				 * Hangul Composition.
				 * 1. Two current code points are L and V.
				 */
				int VIndex = uc2 - HC_VBASE;
				if (0 <= VIndex && VIndex < HC_VCOUNT) {
					/* Make syllable of form LV. */
					UPDATE_UC(HC_SBASE +
					    (LIndex * HC_VCOUNT + VIndex) *
					     HC_TCOUNT);
				} else {
					WRITE_UC();
					REPLACE_UC_WITH_UC2();
				}
				continue;
			} else if (0 <= (SIndex = uc - HC_SBASE) &&
			    SIndex < HC_SCOUNT && (SIndex % HC_TCOUNT) == 0) {
				/*
				 * Hangul Composition.
				 * 2. Two current code points are LV and T.
				 */
				int TIndex = uc2 - HC_TBASE;
				if (0 < TIndex && TIndex < HC_TCOUNT) {
					/* Make syllable of form LVT. */
					UPDATE_UC(uc + TIndex);
				} else {
					WRITE_UC();
					REPLACE_UC_WITH_UC2();
				}
				continue;
			} else if ((nfc = get_nfc(uc, uc2)) != 0) {
				/* A composition to current code points
				 * is found. */
				UPDATE_UC(nfc);
				continue;
			} else if ((cl = CCC(uc2)) == 0) {
				/* Clearly 'uc2' the second code point is not
				 * a decomposable code. */
				WRITE_UC();
				REPLACE_UC_WITH_UC2();
				continue;
			}

			/*
			 * Collect following decomposable code points.
			 */
			cx = 0;
			ucx[0] = uc2;
			ccx[0] = cl;
			COLLECT_CPS(1);

			/*
			 * Find a composed code in the collected code points.
			 */
			i = 1;
			while (i < ucx_size) {
				int j;

				if ((nfc = get_nfc(uc, ucx[i])) == 0) {
					i++;
					continue;
				}

				/*
				 * nfc is composed of uc and ucx[i].
				 */
				UPDATE_UC(nfc);

				/*
				 * Remove ucx[i] by shifting
				 * following code points.
				 */
				for (j = i; j+1 < ucx_size; j++) {
					ucx[j] = ucx[j+1];
					ccx[j] = ccx[j+1];
				}
				ucx_size --;

				/*
				 * Collect following code points blocked
				 * by ucx[i] the removed code point.
				 */
				if (ucx_size > 0 && i == ucx_size &&
				    nx > 0 && cx == cl) {
					cl =  ccx[ucx_size-1];
					COLLECT_CPS(ucx_size);
				}
				/*
				 * Restart finding a composed code with
				 * the updated uc from the top of the
				 * collected code points.
				 */
				i = 0;
			}

			/*
			 * Apparently the current code points are not
			 * decomposed characters or already composed.
			 */
			WRITE_UC();
			for (i = 0; i < ucx_size; i++)
				UNPARSE(p, endp, ucx[i]);

			/*
			 * Flush out remaining canonical combining characters.
			 */
			if (nx > 0 && cx == cl && len > 0) {
				while ((nx = parse(&ucx[0], s, len))
				    > 0) {
					cx = CCC(ucx[0]);
					if (cl > cx)
						break;
					s += nx;
					len -= nx;
					cl = cx;
					UNPARSE(p, endp, ucx[0]);
				}
			}
			break;
		}
		if (n2 < 0) {
			WRITE_UC();
			/* Use a replaced unicode character. */
			UNPARSE(p, endp, uc2);
			s += n2*-1;
			len -= n2*-1;
			ret = -1;
			continue;
		} else if (n2 == 0) {
			WRITE_UC();
			break;
		}
	}
	as->length = p - as->s;
	as->s[as->length] = '\0';
	if (ts == 2)
		as->s[as->length+1] = '\0';
	return (ret);
}

static int
get_nfd(uint32_t *cp1, uint32_t *cp2, uint32_t uc)
{
	int t, b;

	/*
	 * These are not converted to NFD on Mac OS.
	 */
	if ((uc >= 0x2000 && uc <= 0x2FFF) ||
	    (uc >= 0xF900 && uc <= 0xFAFF) ||
	    (uc >= 0x2F800 && uc <= 0x2FAFF))
		return (0);
	/*
	 * Those code points are not converted to NFD on Mac OS.
	 * I do not know the reason because it is undocumented.
	 *   NFC        NFD
	 *   1109A  ==> 11099 110BA
	 *   1109C  ==> 1109B 110BA
	 *   110AB  ==> 110A5 110BA
	 */
	if (uc == 0x1109A || uc == 0x1109C || uc == 0x110AB)
		return (0);

	t = 0;
	b = sizeof(u_decomposition_table)/sizeof(u_decomposition_table[0]) -1;
	while (b >= t) {
		int m = (t + b) / 2;
		if (u_decomposition_table[m].nfc < uc)
			t = m + 1;
		else if (u_decomposition_table[m].nfc > uc)
			b = m - 1;
		else {
			*cp1 = u_decomposition_table[m].cp1;
			*cp2 = u_decomposition_table[m].cp2;
			return (1);
		}
	}
	return (0);
}

#define REPLACE_UC_WITH(cp) do {		\
	uc = cp;				\
	ucptr = NULL;				\
} while (0)

/*
 * Normalize UTF-8 characters to Form D and copy the result.
 */
static int
archive_string_normalize_D(struct archive_string *as, const void *_p,
    size_t len, struct archive_string_conv *sc)
{
	const char *s = (const char *)_p;
	char *p, *endp;
	uint32_t uc, uc2;
	size_t w;
	int always_replace, n, n2, ret = 0, spair, ts, tm;
	int (*parse)(uint32_t *, const char *, size_t);
	size_t (*unparse)(char *, size_t, uint32_t);

	always_replace = 1;
	ts = 1;/* text size. */
	if (sc->flag & SCONV_TO_UTF16BE) {
		unparse = unicode_to_utf16be;
		ts = 2;
		if (sc->flag & SCONV_FROM_UTF16BE)
			always_replace = 0;
	} else if (sc->flag & SCONV_TO_UTF16LE) {
		unparse = unicode_to_utf16le;
		ts = 2;
		if (sc->flag & SCONV_FROM_UTF16LE)
			always_replace = 0;
	} else if (sc->flag & SCONV_TO_UTF8) {
		unparse = unicode_to_utf8;
		if (sc->flag & SCONV_FROM_UTF8)
			always_replace = 0;
	} else {
		/*
		 * This case is going to be converted to another
		 * character-set through iconv.
		 */
		always_replace = 0;
		if (sc->flag & SCONV_FROM_UTF16BE) {
			unparse = unicode_to_utf16be;
			ts = 2;
		} else if (sc->flag & SCONV_FROM_UTF16LE) {
			unparse = unicode_to_utf16le;
			ts = 2;
		} else {
			unparse = unicode_to_utf8;
		}
	}

	if (sc->flag & SCONV_FROM_UTF16BE) {
		parse = utf16be_to_unicode;
		tm = 1;
		spair = 4;/* surrogate pair size in UTF-16. */
	} else if (sc->flag & SCONV_FROM_UTF16LE) {
		parse = utf16le_to_unicode;
		tm = 1;
		spair = 4;/* surrogate pair size in UTF-16. */
	} else {
		parse = cesu8_to_unicode;
		tm = ts;
		spair = 6;/* surrogate pair size in UTF-8. */
	}

	if (archive_string_ensure(as, as->length + len * tm + ts) == NULL)
		return (-1);

	p = as->s + as->length;
	endp = as->s + as->buffer_length - ts;
	while ((n = parse(&uc, s, len)) != 0) {
		const char *ucptr;
		uint32_t cp1, cp2;
		int SIndex;
		struct {
			uint32_t uc;
			int ccc;
		} fdc[FDC_MAX];
		int fdi, fdj;
		int ccc;

check_first_code:
		if (n < 0) {
			/* Use a replaced unicode character. */
			UNPARSE(p, endp, uc);
			s += n*-1;
			len -= n*-1;
			ret = -1;
			continue;
		} else if (n == spair || always_replace)
			/* uc is converted from a surrogate pair.
			 * this should be treated as a changed code. */
			ucptr = NULL;
		else
			ucptr = s;
		s += n;
		len -= n;

		/* Hangul Decomposition. */
		if ((SIndex = uc - HC_SBASE) >= 0 && SIndex < HC_SCOUNT) {
			int L = HC_LBASE + SIndex / HC_NCOUNT;
			int V = HC_VBASE + (SIndex % HC_NCOUNT) / HC_TCOUNT;
			int T = HC_TBASE + SIndex % HC_TCOUNT;

			REPLACE_UC_WITH(L);
			WRITE_UC();
			REPLACE_UC_WITH(V);
			WRITE_UC();
			if (T != HC_TBASE) {
				REPLACE_UC_WITH(T);
				WRITE_UC();
			}
			continue;
		}
		if (IS_DECOMPOSABLE_BLOCK(uc) && CCC(uc) != 0) {
			WRITE_UC();
			continue;
		}

		fdi = 0;
		while (get_nfd(&cp1, &cp2, uc) && fdi < FDC_MAX) {
			int k;

			for (k = fdi; k > 0; k--)
				fdc[k] = fdc[k-1];
			fdc[0].ccc = CCC(cp2);
			fdc[0].uc = cp2;
			fdi++;
			REPLACE_UC_WITH(cp1);
		}

		/* Read following code points. */
		while ((n2 = parse(&uc2, s, len)) > 0 &&
		    (ccc = CCC(uc2)) != 0 && fdi < FDC_MAX) {
			int j, k;

			s += n2;
			len -= n2;
			for (j = 0; j < fdi; j++) {
				if (fdc[j].ccc > ccc)
					break;
			}
			if (j < fdi) {
				for (k = fdi; k > j; k--)
					fdc[k] = fdc[k-1];
				fdc[j].ccc = ccc;
				fdc[j].uc = uc2;
			} else {
				fdc[fdi].ccc = ccc;
				fdc[fdi].uc = uc2;
			}
			fdi++;
		}

		WRITE_UC();
		for (fdj = 0; fdj < fdi; fdj++) {
			REPLACE_UC_WITH(fdc[fdj].uc);
			WRITE_UC();
		}

		if (n2 == 0)
			break;
		REPLACE_UC_WITH(uc2);
		n = n2;
		goto check_first_code;
	}
	as->length = p - as->s;
	as->s[as->length] = '\0';
	if (ts == 2)
		as->s[as->length+1] = '\0';
	return (ret);
}

/*
 * libarchive 2.x made incorrect UTF-8 strings in the wrong assumption
 * that WCS is Unicode. It is true for several platforms but some are false.
 * And then people who did not use UTF-8 locale on the non Unicode WCS
 * platform and made a tar file with libarchive(mostly bsdtar) 2.x. Those
 * now cannot get right filename from libarchive 3.x and later since we
 * fixed the wrong assumption and it is incompatible to older its versions.
 * So we provide special option, "compat-2x.x", for resolving it.
 * That option enable the string conversion of libarchive 2.x.
 *
 * Translates the wrong UTF-8 string made by libarchive 2.x into current
 * locale character set and appends to the archive_string.
 * Note: returns -1 if conversion fails.
 */
static int
strncat_from_utf8_libarchive2(struct archive_string *as,
    const void *_p, size_t len, struct archive_string_conv *sc)
{
	const char *s;
	int n;
	char *p;
	char *end;
	uint32_t unicode;
#if HAVE_WCRTOMB
	mbstate_t shift_state;

	memset(&shift_state, 0, sizeof(shift_state));
#else
	/* Clear the shift state before starting. */
	wctomb(NULL, L'\0');
#endif
	(void)sc; /* UNUSED */
	/*
	 * Allocate buffer for MBS.
	 * We need this allocation here since it is possible that
	 * as->s is still NULL.
	 */
	if (archive_string_ensure(as, as->length + len + 1) == NULL)
		return (-1);

	s = (const char *)_p;
	p = as->s + as->length;
	end = as->s + as->buffer_length - MB_CUR_MAX -1;
	while ((n = _utf8_to_unicode(&unicode, s, len)) != 0) {
		wchar_t wc;

		if (p >= end) {
			as->length = p - as->s;
			/* Re-allocate buffer for MBS. */
			if (archive_string_ensure(as,
			    as->length + len * 2 + 1) == NULL)
				return (-1);
			p = as->s + as->length;
			end = as->s + as->buffer_length - MB_CUR_MAX -1;
		}

		/*
		 * As libarchive 2.x, translates the UTF-8 characters into
		 * wide-characters in the assumption that WCS is Unicode.
		 */
		if (n < 0) {
			n *= -1;
			wc = L'?';
		} else
			wc = (wchar_t)unicode;

		s += n;
		len -= n;
		/*
		 * Translates the wide-character into the current locale MBS.
		 */
#if HAVE_WCRTOMB
		n = (int)wcrtomb(p, wc, &shift_state);
#else
		n = (int)wctomb(p, wc);
#endif
		if (n == -1)
			return (-1);
		p += n;
	}
	as->length = p - as->s;
	as->s[as->length] = '\0';
	return (0);
}


/*
 * Conversion functions between current locale dependent MBS and UTF-16BE.
 *   strncat_from_utf16be() : UTF-16BE --> MBS
 *   strncat_to_utf16be()   : MBS --> UTF16BE
 */

#if defined(_WIN32) && !defined(__CYGWIN__)

/*
 * Convert a UTF-16BE/LE string to current locale and copy the result.
 * Return -1 if conversion fails.
 */
static int
win_strncat_from_utf16(struct archive_string *as, const void *_p, size_t bytes,
    struct archive_string_conv *sc, int be)
{
	struct archive_string tmp;
	const char *u16;
	int ll;
	BOOL defchar;
	char *mbs;
	size_t mbs_size, b;
	int ret = 0;

	bytes &= ~1;
	if (archive_string_ensure(as, as->length + bytes +1) == NULL)
		return (-1);

	mbs = as->s + as->length;
	mbs_size = as->buffer_length - as->length -1;

	if (sc->to_cp == CP_C_LOCALE) {
		/*
		 * "C" locale special process.
		 */
		u16 = _p;
		ll = 0;
		for (b = 0; b < bytes; b += 2) {
			uint16_t val;
			if (be)
				val = archive_be16dec(u16+b);
			else
				val = archive_le16dec(u16+b);
			if (val > 255) {
				*mbs++ = '?';
				ret = -1;
			} else
				*mbs++ = (char)(val&0xff);
			ll++;
		}
		as->length += ll;
		as->s[as->length] = '\0';
		return (ret);
	}

	archive_string_init(&tmp);
	if (be) {
		if (is_big_endian()) {
			u16 = _p;
		} else {
			if (archive_string_ensure(&tmp, bytes+2) == NULL)
				return (-1);
			memcpy(tmp.s, _p, bytes);
			for (b = 0; b < bytes; b += 2) {
				uint16_t val = archive_be16dec(tmp.s+b);
				archive_le16enc(tmp.s+b, val);
			}
			u16 = tmp.s;
		}
	} else {
		if (!is_big_endian()) {
			u16 = _p;
		} else {
			if (archive_string_ensure(&tmp, bytes+2) == NULL)
				return (-1);
			memcpy(tmp.s, _p, bytes);
			for (b = 0; b < bytes; b += 2) {
				uint16_t val = archive_le16dec(tmp.s+b);
				archive_be16enc(tmp.s+b, val);
			}
			u16 = tmp.s;
		}
	}

	do {
		defchar = 0;
		ll = WideCharToMultiByte(sc->to_cp, 0,
		    (LPCWSTR)u16, (int)bytes>>1, mbs, (int)mbs_size,
			NULL, &defchar);
		/* Exit loop if we succeeded */
		if (ll != 0 ||
		    GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
			break;
		}
		/* Else expand buffer and loop to try again. */
		ll = WideCharToMultiByte(sc->to_cp, 0,
		    (LPCWSTR)u16, (int)bytes, NULL, 0, NULL, NULL);
		if (archive_string_ensure(as, ll +1) == NULL)
			return (-1);
		mbs = as->s + as->length;
		mbs_size = as->buffer_length - as->length -1;
	} while (1);
	archive_string_free(&tmp);
	as->length += ll;
	as->s[as->length] = '\0';
	if (ll == 0 || defchar)
		ret = -1;
	return (ret);
}

static int
win_strncat_from_utf16be(struct archive_string *as, const void *_p,
    size_t bytes, struct archive_string_conv *sc)
{
	return (win_strncat_from_utf16(as, _p, bytes, sc, 1));
}

static int
win_strncat_from_utf16le(struct archive_string *as, const void *_p,
    size_t bytes, struct archive_string_conv *sc)
{
	return (win_strncat_from_utf16(as, _p, bytes, sc, 0));
}

static int
is_big_endian(void)
{
	uint16_t d = 1;

	return (archive_be16dec(&d) == 1);
}

/*
 * Convert a current locale string to UTF-16BE/LE and copy the result.
 * Return -1 if conversion fails.
 */
static int
win_strncat_to_utf16(struct archive_string *as16, const void *_p,
    size_t length, struct archive_string_conv *sc, int bigendian)
{
	const char *s = (const char *)_p;
	char *u16;
	size_t count, avail;

	if (archive_string_ensure(as16,
	    as16->length + (length + 1) * 2) == NULL)
		return (-1);

	u16 = as16->s + as16->length;
	avail = as16->buffer_length - 2;
	if (sc->from_cp == CP_C_LOCALE) {
		/*
		 * "C" locale special process.
		 */
		count = 0;
		while (count < length && *s) {
			if (bigendian)
				archive_be16enc(u16, *s);
			else
				archive_le16enc(u16, *s);
			u16 += 2;
			s++;
			count++;
		}
		as16->length += count << 1;
		as16->s[as16->length] = 0;
		as16->s[as16->length+1] = 0;
		return (0);
	}
	do {
		count = MultiByteToWideChar(sc->from_cp,
		    MB_PRECOMPOSED, s, (int)length, (LPWSTR)u16, (int)avail>>1);
		/* Exit loop if we succeeded */
		if (count != 0 ||
		    GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
			break;
		}
		/* Expand buffer and try again */
		count = MultiByteToWideChar(sc->from_cp,
		    MB_PRECOMPOSED, s, (int)length, NULL, 0);
		if (archive_string_ensure(as16, (count +1) * 2)
		    == NULL)
			return (-1);
		u16 = as16->s + as16->length;
		avail = as16->buffer_length - 2;
	} while (1);
	as16->length += count * 2;
	as16->s[as16->length] = 0;
	as16->s[as16->length+1] = 0;
	if (count == 0)
		return (-1);

	if (is_big_endian()) {
		if (!bigendian) {
			while (count > 0) {
				uint16_t v = archive_be16dec(u16);
				archive_le16enc(u16, v);
				u16 += 2;
				count--;
			}
		}
	} else {
		if (bigendian) {
			while (count > 0) {
				uint16_t v = archive_le16dec(u16);
				archive_be16enc(u16, v);
				u16 += 2;
				count--;
			}
		}
	}
	return (0);
}

static int
win_strncat_to_utf16be(struct archive_string *as16, const void *_p,
    size_t length, struct archive_string_conv *sc)
{
	return (win_strncat_to_utf16(as16, _p, length, sc, 1));
}

static int
win_strncat_to_utf16le(struct archive_string *as16, const void *_p,
    size_t length, struct archive_string_conv *sc)
{
	return (win_strncat_to_utf16(as16, _p, length, sc, 0));
}

#endif /* _WIN32 && !__CYGWIN__ */

/*
 * Do the best effort for conversions.
 * We cannot handle UTF-16BE character-set without such iconv,
 * but there is a chance if a string consists just ASCII code or
 * a current locale is UTF-8.
 */

/*
 * Convert a UTF-16BE string to current locale and copy the result.
 * Return -1 if conversion fails.
 */
static int
best_effort_strncat_from_utf16(struct archive_string *as, const void *_p,
    size_t bytes, struct archive_string_conv *sc, int be)
{
	const char *utf16 = (const char *)_p;
	char *mbs;
	uint32_t uc;
	int n, ret;

	(void)sc; /* UNUSED */
	/*
	 * Other case, we should do the best effort.
	 * If all character are ASCII(<0x7f), we can convert it.
	 * if not , we set a alternative character and return -1.
	 */
	ret = 0;
	if (archive_string_ensure(as, as->length + bytes +1) == NULL)
		return (-1);
	mbs = as->s + as->length;

	while ((n = utf16_to_unicode(&uc, utf16, bytes, be)) != 0) {
		if (n < 0) {
			n *= -1;
			ret =  -1;
		}
		bytes -= n;
		utf16 += n;

		if (uc > 127) {
			/* We cannot handle it. */
			*mbs++ = '?';
			ret =  -1;
		} else
			*mbs++ = (char)uc;
	}
	as->length = mbs - as->s;
	as->s[as->length] = '\0';
	return (ret);
}

static int
best_effort_strncat_from_utf16be(struct archive_string *as, const void *_p,
    size_t bytes, struct archive_string_conv *sc)
{
	return (best_effort_strncat_from_utf16(as, _p, bytes, sc, 1));
}

static int
best_effort_strncat_from_utf16le(struct archive_string *as, const void *_p,
    size_t bytes, struct archive_string_conv *sc)
{
	return (best_effort_strncat_from_utf16(as, _p, bytes, sc, 0));
}

/*
 * Convert a current locale string to UTF-16BE/LE and copy the result.
 * Return -1 if conversion fails.
 */
static int
best_effort_strncat_to_utf16(struct archive_string *as16, const void *_p,
    size_t length, struct archive_string_conv *sc, int bigendian)
{
	const char *s = (const char *)_p;
	char *utf16;
	size_t remaining;
	int ret;

	(void)sc; /* UNUSED */
	/*
	 * Other case, we should do the best effort.
	 * If all character are ASCII(<0x7f), we can convert it.
	 * if not , we set a alternative character and return -1.
	 */
	ret = 0;
	remaining = length;

	if (archive_string_ensure(as16,
	    as16->length + (length + 1) * 2) == NULL)
		return (-1);

	utf16 = as16->s + as16->length;
	while (remaining--) {
		unsigned c = *s++;
		if (c > 127) {
			/* We cannot handle it. */
			c = UNICODE_R_CHAR;
			ret = -1;
		}
		if (bigendian)
			archive_be16enc(utf16, c);
		else
			archive_le16enc(utf16, c);
		utf16 += 2;
	}
	as16->length = utf16 - as16->s;
	as16->s[as16->length] = 0;
	as16->s[as16->length+1] = 0;
	return (ret);
}

static int
best_effort_strncat_to_utf16be(struct archive_string *as16, const void *_p,
    size_t length, struct archive_string_conv *sc)
{
	return (best_effort_strncat_to_utf16(as16, _p, length, sc, 1));
}

static int
best_effort_strncat_to_utf16le(struct archive_string *as16, const void *_p,
    size_t length, struct archive_string_conv *sc)
{
	return (best_effort_strncat_to_utf16(as16, _p, length, sc, 0));
}


/*
 * Multistring operations.
 */

void
archive_mstring_clean(struct archive_mstring *aes)
{
	archive_wstring_free(&(aes->aes_wcs));
	archive_string_free(&(aes->aes_mbs));
	archive_string_free(&(aes->aes_utf8));
	archive_string_free(&(aes->aes_mbs_in_locale));
	aes->aes_set = 0;
}

void
archive_mstring_copy(struct archive_mstring *dest, struct archive_mstring *src)
{
	dest->aes_set = src->aes_set;
	archive_string_copy(&(dest->aes_mbs), &(src->aes_mbs));
	archive_string_copy(&(dest->aes_utf8), &(src->aes_utf8));
	archive_wstring_copy(&(dest->aes_wcs), &(src->aes_wcs));
}

int
archive_mstring_get_utf8(struct archive *a, struct archive_mstring *aes,
  const char **p)
{
	struct archive_string_conv *sc;
	int r;

	/* If we already have a UTF8 form, return that immediately. */
	if (aes->aes_set & AES_SET_UTF8) {
		*p = aes->aes_utf8.s;
		return (0);
	}

	*p = NULL;
	if (aes->aes_set & AES_SET_MBS) {
		sc = archive_string_conversion_to_charset(a, "UTF-8", 1);
		if (sc == NULL)
			return (-1);/* Couldn't allocate memory for sc. */
		r = archive_strncpy_l(&(aes->aes_utf8), aes->aes_mbs.s,
		    aes->aes_mbs.length, sc);
		if (a == NULL)
			free_sconv_object(sc);
		if (r == 0) {
			aes->aes_set |= AES_SET_UTF8;
			*p = aes->aes_utf8.s;
			return (0);/* success. */
		} else
			return (-1);/* failure. */
	}
	return (0);/* success. */
}

int
archive_mstring_get_mbs(struct archive *a, struct archive_mstring *aes,
    const char **p)
{
	int r, ret = 0;

	(void)a; /* UNUSED */
	/* If we already have an MBS form, return that immediately. */
	if (aes->aes_set & AES_SET_MBS) {
		*p = aes->aes_mbs.s;
		return (ret);
	}

	*p = NULL;
	/* If there's a WCS form, try converting with the native locale. */
	if (aes->aes_set & AES_SET_WCS) {
		archive_string_empty(&(aes->aes_mbs));
		r = archive_string_append_from_wcs(&(aes->aes_mbs),
		    aes->aes_wcs.s, aes->aes_wcs.length);
		*p = aes->aes_mbs.s;
		if (r == 0) {
			aes->aes_set |= AES_SET_MBS;
			return (ret);
		} else
			ret = -1;
	}

	/*
	 * Only a UTF-8 form cannot avail because its conversion already
	 * failed at archive_mstring_update_utf8().
	 */
	return (ret);
}

int
archive_mstring_get_wcs(struct archive *a, struct archive_mstring *aes,
    const wchar_t **wp)
{
	int r, ret = 0;

	(void)a;/* UNUSED */
	/* Return WCS form if we already have it. */
	if (aes->aes_set & AES_SET_WCS) {
		*wp = aes->aes_wcs.s;
		return (ret);
	}

	*wp = NULL;
	/* Try converting MBS to WCS using native locale. */
	if (aes->aes_set & AES_SET_MBS) {
		archive_wstring_empty(&(aes->aes_wcs));
		r = archive_wstring_append_from_mbs(&(aes->aes_wcs),
		    aes->aes_mbs.s, aes->aes_mbs.length);
		if (r == 0) {
			aes->aes_set |= AES_SET_WCS;
			*wp = aes->aes_wcs.s;
		} else
			ret = -1;/* failure. */
	}
	return (ret);
}

int
archive_mstring_get_mbs_l(struct archive_mstring *aes,
    const char **p, size_t *length, struct archive_string_conv *sc)
{
	int r, ret = 0;

#if defined(_WIN32) && !defined(__CYGWIN__)
	/*
	 * Internationalization programming on Windows must use Wide
	 * characters because Windows platform cannot make locale UTF-8.
	 */
	if (sc != NULL && (aes->aes_set & AES_SET_WCS) != 0) {
		archive_string_empty(&(aes->aes_mbs_in_locale));
		r = archive_string_append_from_wcs_in_codepage(
		    &(aes->aes_mbs_in_locale), aes->aes_wcs.s,
		    aes->aes_wcs.length, sc);
		if (r == 0) {
			*p = aes->aes_mbs_in_locale.s;
			if (length != NULL)
				*length = aes->aes_mbs_in_locale.length;
			return (0);
		} else if (errno == ENOMEM)
			return (-1);
		else
			ret = -1;
	}
#endif

	/* If there is not an MBS form but is a WCS form, try converting
	 * with the native locale to be used for translating it to specified
	 * character-set. */
	if ((aes->aes_set & AES_SET_MBS) == 0 &&
	    (aes->aes_set & AES_SET_WCS) != 0) {
		archive_string_empty(&(aes->aes_mbs));
		r = archive_string_append_from_wcs(&(aes->aes_mbs),
		    aes->aes_wcs.s, aes->aes_wcs.length);
		if (r == 0)
			aes->aes_set |= AES_SET_MBS;
		else if (errno == ENOMEM)
			return (-1);
		else
			ret = -1;
	}
	/* If we already have an MBS form, use it to be translated to
	 * specified character-set. */
	if (aes->aes_set & AES_SET_MBS) {
		if (sc == NULL) {
			/* Conversion is unneeded. */
			*p = aes->aes_mbs.s;
			if (length != NULL)
				*length = aes->aes_mbs.length;
			return (0);
		}
		ret = archive_strncpy_l(&(aes->aes_mbs_in_locale),
		    aes->aes_mbs.s, aes->aes_mbs.length, sc);
		*p = aes->aes_mbs_in_locale.s;
		if (length != NULL)
			*length = aes->aes_mbs_in_locale.length;
	} else {
		*p = NULL;
		if (length != NULL)
			*length = 0;
	}
	return (ret);
}

int
archive_mstring_copy_mbs(struct archive_mstring *aes, const char *mbs)
{
	if (mbs == NULL) {
		aes->aes_set = 0;
		return (0);
	}
	return (archive_mstring_copy_mbs_len(aes, mbs, strlen(mbs)));
}

int
archive_mstring_copy_mbs_len(struct archive_mstring *aes, const char *mbs,
    size_t len)
{
	if (mbs == NULL) {
		aes->aes_set = 0;
		return (0);
	}
	aes->aes_set = AES_SET_MBS; /* Only MBS form is set now. */
	archive_strncpy(&(aes->aes_mbs), mbs, len);
	archive_string_empty(&(aes->aes_utf8));
	archive_wstring_empty(&(aes->aes_wcs));
	return (0);
}

int
archive_mstring_copy_wcs(struct archive_mstring *aes, const wchar_t *wcs)
{
	return archive_mstring_copy_wcs_len(aes, wcs,
				wcs == NULL ? 0 : wcslen(wcs));
}

int
archive_mstring_copy_utf8(struct archive_mstring *aes, const char *utf8)
{
  if (utf8 == NULL) {
    aes->aes_set = 0;
    return (0);
  }
  aes->aes_set = AES_SET_UTF8;
  archive_string_empty(&(aes->aes_mbs));
  archive_string_empty(&(aes->aes_wcs));
  archive_strncpy(&(aes->aes_utf8), utf8, strlen(utf8));
  return (int)strlen(utf8);
}

int
archive_mstring_copy_wcs_len(struct archive_mstring *aes, const wchar_t *wcs,
    size_t len)
{
	if (wcs == NULL) {
		aes->aes_set = 0;
		return (0);
	}
	aes->aes_set = AES_SET_WCS; /* Only WCS form set. */
	archive_string_empty(&(aes->aes_mbs));
	archive_string_empty(&(aes->aes_utf8));
	archive_wstrncpy(&(aes->aes_wcs), wcs, len);
	return (0);
}

int
archive_mstring_copy_mbs_len_l(struct archive_mstring *aes,
    const char *mbs, size_t len, struct archive_string_conv *sc)
{
	int r;

	if (mbs == NULL) {
		aes->aes_set = 0;
		return (0);
	}
	archive_string_empty(&(aes->aes_mbs));
	archive_wstring_empty(&(aes->aes_wcs));
	archive_string_empty(&(aes->aes_utf8));
#if defined(_WIN32) && !defined(__CYGWIN__)
	/*
	 * Internationalization programming on Windows must use Wide
	 * characters because Windows platform cannot make locale UTF-8.
	 */
	if (sc == NULL) {
		if (archive_string_append(&(aes->aes_mbs),
			mbs, mbsnbytes(mbs, len)) == NULL) {
			aes->aes_set = 0;
			r = -1;
		} else {
			aes->aes_set = AES_SET_MBS;
			r = 0;
		}
#if defined(HAVE_ICONV)
	} else if (sc != NULL && sc->cd_w != (iconv_t)-1) {
		/*
		 * This case happens only when MultiByteToWideChar() cannot
		 * handle sc->from_cp, and we have to iconv in order to
		 * translate character-set to wchar_t,UTF-16.
		 */
		iconv_t cd = sc->cd;
		unsigned from_cp;
		int flag;

		/*
		 * Translate multi-bytes from some character-set to UTF-8.
		 */ 
		sc->cd = sc->cd_w;
		r = archive_strncpy_l(&(aes->aes_utf8), mbs, len, sc);
		sc->cd = cd;
		if (r != 0) {
			aes->aes_set = 0;
			return (r);
		}
		aes->aes_set = AES_SET_UTF8;

		/*
		 * Append the UTF-8 string into wstring.
		 */ 
		flag = sc->flag;
		sc->flag &= ~(SCONV_NORMALIZATION_C
				| SCONV_TO_UTF16| SCONV_FROM_UTF16);
		from_cp = sc->from_cp;
		sc->from_cp = CP_UTF8;
		r = archive_wstring_append_from_mbs_in_codepage(&(aes->aes_wcs),
			aes->aes_utf8.s, aes->aes_utf8.length, sc);
		sc->flag = flag;
		sc->from_cp = from_cp;
		if (r == 0)
			aes->aes_set |= AES_SET_WCS;
#endif
	} else {
		r = archive_wstring_append_from_mbs_in_codepage(
		    &(aes->aes_wcs), mbs, len, sc);
		if (r == 0)
			aes->aes_set = AES_SET_WCS;
		else
			aes->aes_set = 0;
	}
#else
	r = archive_strncpy_l(&(aes->aes_mbs), mbs, len, sc);
	if (r == 0)
		aes->aes_set = AES_SET_MBS; /* Only MBS form is set now. */
	else
		aes->aes_set = 0;
#endif
	return (r);
}

/*
 * The 'update' form tries to proactively update all forms of
 * this string (WCS and MBS) and returns an error if any of
 * them fail.  This is used by the 'pax' handler, for instance,
 * to detect and report character-conversion failures early while
 * still allowing clients to get potentially useful values from
 * the more tolerant lazy conversions.  (get_mbs and get_wcs will
 * strive to give the user something useful, so you can get hopefully
 * usable values even if some of the character conversions are failing.)
 */
int
archive_mstring_update_utf8(struct archive *a, struct archive_mstring *aes,
    const char *utf8)
{
	struct archive_string_conv *sc;
	int r;

	if (utf8 == NULL) {
		aes->aes_set = 0;
		return (0); /* Succeeded in clearing everything. */
	}

	/* Save the UTF8 string. */
	archive_strcpy(&(aes->aes_utf8), utf8);

	/* Empty the mbs and wcs strings. */
	archive_string_empty(&(aes->aes_mbs));
	archive_wstring_empty(&(aes->aes_wcs));

	aes->aes_set = AES_SET_UTF8;	/* Only UTF8 is set now. */

	/* Try converting UTF-8 to MBS, return false on failure. */
	sc = archive_string_conversion_from_charset(a, "UTF-8", 1);
	if (sc == NULL)
		return (-1);/* Couldn't allocate memory for sc. */
	r = archive_strcpy_l(&(aes->aes_mbs), utf8, sc);
	if (a == NULL)
		free_sconv_object(sc);
	if (r != 0)
		return (-1);
	aes->aes_set = AES_SET_UTF8 | AES_SET_MBS; /* Both UTF8 and MBS set. */

	/* Try converting MBS to WCS, return false on failure. */
	if (archive_wstring_append_from_mbs(&(aes->aes_wcs), aes->aes_mbs.s,
	    aes->aes_mbs.length))
		return (-1);
	aes->aes_set = AES_SET_UTF8 | AES_SET_WCS | AES_SET_MBS;

	/* All conversions succeeded. */
	return (0);
}
