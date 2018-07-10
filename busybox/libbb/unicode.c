/* vi: set sw=4 ts=4: */
/*
 * Unicode support routines.
 *
 * Copyright (C) 2009 Denys Vlasenko
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
#include "libbb.h"
#include "unicode.h"

/* If it's not #defined as a constant in unicode.h... */
#ifndef unicode_status
uint8_t unicode_status;
#endif

/* This file is compiled only if UNICODE_SUPPORT is on.
 * We check other options and decide whether to use libc support
 * via locale, or use our own logic:
 */

#if ENABLE_UNICODE_USING_LOCALE

/* Unicode support using libc locale support. */

void FAST_FUNC reinit_unicode(const char *LANG)
{
	static const char unicode_0x394[] = { 0xce, 0x94, 0 };
	size_t width;

	/* We pass "" instead of "C" because some libc's have
	 * non-ASCII default locale for setlocale("") call
	 * (this allows users of such libc to have Unicoded
	 * system without having to mess with env).
	 *
	 * We set LC_CTYPE because (a) we may be called with $LC_CTYPE
	 * value in LANG, not with $LC_ALL, (b) internationalized
	 * LC_NUMERIC and LC_TIME are more PITA than benefit
	 * (for one, some utilities have hard time with comma
	 * used as a fractional separator).
	 */
//TODO: avoid repeated calls by caching last string?
	setlocale(LC_CTYPE, LANG ? LANG : "");

	/* In unicode, this is a one character string */
	width = unicode_strlen(unicode_0x394);
	unicode_status = (width == 1 ? UNICODE_ON : UNICODE_OFF);
}

void FAST_FUNC init_unicode(void)
{
	/* Some people set only $LC_CTYPE, not $LC_ALL, because they want
	 * only Unicode to be activated on their system, not the whole
	 * shebang of wrong decimal points, strange date formats and so on.
	 */
	if (unicode_status == UNICODE_UNKNOWN) {
		char *s = getenv("LC_ALL");
		if (!s) s = getenv("LC_CTYPE");
		if (!s) s = getenv("LANG");
		reinit_unicode(s);
	}
}

#else

/* Homegrown Unicode support. It knows only C and Unicode locales. */

# if ENABLE_FEATURE_CHECK_UNICODE_IN_ENV
void FAST_FUNC reinit_unicode(const char *LANG)
{
	unicode_status = UNICODE_OFF;
	if (!LANG || !(strstr(LANG, ".utf") || strstr(LANG, ".UTF")))
		return;
	unicode_status = UNICODE_ON;
}

void FAST_FUNC init_unicode(void)
{
	if (unicode_status == UNICODE_UNKNOWN) {
		char *s = getenv("LC_ALL");
		if (!s) s = getenv("LC_CTYPE");
		if (!s) s = getenv("LANG");
		reinit_unicode(s);
	}
}
# endif

static size_t wcrtomb_internal(char *s, wchar_t wc)
{
	int n, i;
	uint32_t v = wc;

	if (v <= 0x7f) {
		*s = v;
		return 1;
	}

	/* RFC 3629 says that Unicode ends at 10FFFF,
	 * but we cover entire 32 bits */

	/* 4000000-FFFFFFFF -> 111111tt 10tttttt 10zzzzzz 10zzyyyy 10yyyyxx 10xxxxxx */
	/* 200000-3FFFFFF -> 111110tt 10zzzzzz 10zzyyyy 10yyyyxx 10xxxxxx */
	/* 10000-1FFFFF -> 11110zzz 10zzyyyy 10yyyyxx 10xxxxxx */
	/* 800-FFFF -> 1110yyyy 10yyyyxx 10xxxxxx */
	/* 80-7FF -> 110yyyxx 10xxxxxx */

	/* How many bytes do we need? */
	n = 2;
	/* (0x80000000+ would result in n = 7, limiting n to 6) */
	while (v >= 0x800 && n < 6) {
		v >>= 5;
		n++;
	}
	/* Fill bytes n-1..1 */
	i = n;
	while (--i) {
		s[i] = (wc & 0x3f) | 0x80;
		wc >>= 6;
	}
	/* Fill byte 0 */
	s[0] = wc | (uint8_t)(0x3f00 >> n);
	return n;
}
size_t FAST_FUNC wcrtomb(char *s, wchar_t wc, mbstate_t *ps UNUSED_PARAM)
{
	if (unicode_status != UNICODE_ON) {
		*s = wc;
		return 1;
	}

	return wcrtomb_internal(s, wc);
}
size_t FAST_FUNC wcstombs(char *dest, const wchar_t *src, size_t n)
{
	size_t org_n = n;

	if (unicode_status != UNICODE_ON) {
		while (n) {
			wchar_t c = *src++;
			*dest++ = c;
			if (c == 0)
				break;
			n--;
		}
		return org_n - n;
	}

	while (n >= MB_CUR_MAX) {
		wchar_t wc = *src++;
		size_t len = wcrtomb_internal(dest, wc);

		if (wc == L'\0')
			return org_n - n;
		dest += len;
		n -= len;
	}
	while (n) {
		char tbuf[MB_CUR_MAX];
		wchar_t wc = *src++;
		size_t len = wcrtomb_internal(tbuf, wc);

		if (len > n)
			break;
		memcpy(dest, tbuf, len);
		if (wc == L'\0')
			return org_n - n;
		dest += len;
		n -= len;
	}
	return org_n - n;
}

# define ERROR_WCHAR (~(wchar_t)0)

static const char *mbstowc_internal(wchar_t *res, const char *src)
{
	int bytes;
	unsigned c = (unsigned char) *src++;

	if (c <= 0x7f) {
		*res = c;
		return src;
	}

	/* 80-7FF -> 110yyyxx 10xxxxxx */
	/* 800-FFFF -> 1110yyyy 10yyyyxx 10xxxxxx */
	/* 10000-1FFFFF -> 11110zzz 10zzyyyy 10yyyyxx 10xxxxxx */
	/* 200000-3FFFFFF -> 111110tt 10zzzzzz 10zzyyyy 10yyyyxx 10xxxxxx */
	/* 4000000-FFFFFFFF -> 111111tt 10tttttt 10zzzzzz 10zzyyyy 10yyyyxx 10xxxxxx */
	bytes = 0;
	do {
		c <<= 1;
		bytes++;
	} while ((c & 0x80) && bytes < 6);
	if (bytes == 1) {
		/* A bare "continuation" byte. Say, 80 */
		*res = ERROR_WCHAR;
		return src;
	}
	c = (uint8_t)(c) >> bytes;

	while (--bytes) {
		unsigned ch = (unsigned char) *src;
		if ((ch & 0xc0) != 0x80) {
			/* Missing "continuation" byte. Example: e0 80 */
			*res = ERROR_WCHAR;
			return src;
		}
		c = (c << 6) + (ch & 0x3f);
		src++;
	}

	/* TODO */
	/* Need to check that c isn't produced by overlong encoding */
	/* Example: 11000000 10000000 converts to NUL */
	/* 11110000 10000000 10000100 10000000 converts to 0x100 */
	/* correct encoding: 11000100 10000000 */
	if (c <= 0x7f) { /* crude check */
		*res = ERROR_WCHAR;
		return src;
	}

	*res = c;
	return src;
}
size_t FAST_FUNC mbstowcs(wchar_t *dest, const char *src, size_t n)
{
	size_t org_n = n;

	if (unicode_status != UNICODE_ON) {
		while (n) {
			unsigned char c = *src++;

			if (dest)
				*dest++ = c;
			if (c == 0)
				break;
			n--;
		}
		return org_n - n;
	}

	while (n) {
		wchar_t wc;
		src = mbstowc_internal(&wc, src);
		if (wc == ERROR_WCHAR) /* error */
			return (size_t) -1L;
		if (dest)
			*dest++ = wc;
		if (wc == 0) /* end-of-string */
			break;
		n--;
	}

	return org_n - n;
}

int FAST_FUNC iswspace(wint_t wc)
{
	return (unsigned)wc <= 0x7f && isspace(wc);
}

int FAST_FUNC iswalnum(wint_t wc)
{
	return (unsigned)wc <= 0x7f && isalnum(wc);
}

int FAST_FUNC iswpunct(wint_t wc)
{
	return (unsigned)wc <= 0x7f && ispunct(wc);
}


# if CONFIG_LAST_SUPPORTED_WCHAR >= 0x300
struct interval {
	uint16_t first;
	uint16_t last;
};

/* auxiliary function for binary search in interval table */
static int in_interval_table(unsigned ucs, const struct interval *table, unsigned max)
{
	unsigned min;
	unsigned mid;

	if (ucs < table[0].first || ucs > table[max].last)
		return 0;

	min = 0;
	while (max >= min) {
		mid = (min + max) / 2;
		if (ucs > table[mid].last)
			min = mid + 1;
		else if (ucs < table[mid].first)
			max = mid - 1;
		else
			return 1;
	}
	return 0;
}

static int in_uint16_table(unsigned ucs, const uint16_t *table, unsigned max)
{
	unsigned min;
	unsigned mid;
	unsigned first, last;

	first = table[0] >> 2;
	last = first + (table[0] & 3);
	if (ucs < first || ucs > last)
		return 0;

	min = 0;
	while (max >= min) {
		mid = (min + max) / 2;
		first = table[mid] >> 2;
		last = first + (table[mid] & 3);
		if (ucs > last)
			min = mid + 1;
		else if (ucs < first)
			max = mid - 1;
		else
			return 1;
	}
	return 0;
}
# endif


/*
 * This is an implementation of wcwidth() and wcswidth() (defined in
 * IEEE Std 1002.1-2001) for Unicode.
 *
 * http://www.opengroup.org/onlinepubs/007904975/functions/wcwidth.html
 * http://www.opengroup.org/onlinepubs/007904975/functions/wcswidth.html
 *
 * In fixed-width output devices, Latin characters all occupy a single
 * "cell" position of equal width, whereas ideographic CJK characters
 * occupy two such cells. Interoperability between terminal-line
 * applications and (teletype-style) character terminals using the
 * UTF-8 encoding requires agreement on which character should advance
 * the cursor by how many cell positions. No established formal
 * standards exist at present on which Unicode character shall occupy
 * how many cell positions on character terminals. These routines are
 * a first attempt of defining such behavior based on simple rules
 * applied to data provided by the Unicode Consortium.
 *
 * For some graphical characters, the Unicode standard explicitly
 * defines a character-cell width via the definition of the East Asian
 * FullWidth (F), Wide (W), Half-width (H), and Narrow (Na) classes.
 * In all these cases, there is no ambiguity about which width a
 * terminal shall use. For characters in the East Asian Ambiguous (A)
 * class, the width choice depends purely on a preference of backward
 * compatibility with either historic CJK or Western practice.
 * Choosing single-width for these characters is easy to justify as
 * the appropriate long-term solution, as the CJK practice of
 * displaying these characters as double-width comes from historic
 * implementation simplicity (8-bit encoded characters were displayed
 * single-width and 16-bit ones double-width, even for Greek,
 * Cyrillic, etc.) and not any typographic considerations.
 *
 * Much less clear is the choice of width for the Not East Asian
 * (Neutral) class. Existing practice does not dictate a width for any
 * of these characters. It would nevertheless make sense
 * typographically to allocate two character cells to characters such
 * as for instance EM SPACE or VOLUME INTEGRAL, which cannot be
 * represented adequately with a single-width glyph. The following
 * routines at present merely assign a single-cell width to all
 * neutral characters, in the interest of simplicity. This is not
 * entirely satisfactory and should be reconsidered before
 * establishing a formal standard in this area. At the moment, the
 * decision which Not East Asian (Neutral) characters should be
 * represented by double-width glyphs cannot yet be answered by
 * applying a simple rule from the Unicode database content. Setting
 * up a proper standard for the behavior of UTF-8 character terminals
 * will require a careful analysis not only of each Unicode character,
 * but also of each presentation form, something the author of these
 * routines has avoided to do so far.
 *
 * http://www.unicode.org/unicode/reports/tr11/
 *
 * Markus Kuhn -- 2007-05-26 (Unicode 5.0)
 *
 * Permission to use, copy, modify, and distribute this software
 * for any purpose and without fee is hereby granted. The author
 * disclaims all warranties with regard to this software.
 *
 * Latest version: http://www.cl.cam.ac.uk/~mgk25/ucs/wcwidth.c
 */

/* Assigned Unicode character ranges:
 * Plane Range
 * 0       0000–FFFF   Basic Multilingual Plane
 * 1      10000–1FFFF  Supplementary Multilingual Plane
 * 2      20000–2FFFF  Supplementary Ideographic Plane
 * 3      30000-3FFFF  Tertiary Ideographic Plane (no chars assigned yet)
 * 4-13   40000–DFFFF  currently unassigned
 * 14     E0000–EFFFF  Supplementary Special-purpose Plane
 * 15     F0000–FFFFF  Supplementary Private Use Area-A
 * 16    100000–10FFFF Supplementary Private Use Area-B
 *
 * "Supplementary Special-purpose Plane currently contains non-graphical
 * characters in two blocks of 128 and 240 characters. The first block
 * is for language tag characters for use when language cannot be indicated
 * through other protocols (such as the xml:lang  attribute in XML).
 * The other block contains glyph variation selectors to indicate
 * an alternate glyph for a character that cannot be determined by context."
 *
 * In simpler terms: it is a tool to fix the "Han unification" mess
 * created by Unicode committee, to select Chinese/Japanese/Korean/Taiwan
 * version of a character. (They forgot that the whole purpose of the Unicode
 * was to be able to write all chars in one charset without such tricks).
 * Until East Asian users say it is actually necessary to support these
 * code points in console applications like busybox
 * (i.e. do these chars ever appear in filenames, hostnames, text files
 * and such?), we are treating these code points as invalid.
 *
 * Tertiary Ideographic Plane is also ignored for now,
 * until Unicode committee assigns something there.
 */
/* The following two functions define the column width of an ISO 10646
 * character as follows:
 *
 *    - The null character (U+0000) has a column width of 0.
 *
 *    - Other C0/C1 control characters and DEL will lead to a return
 *      value of -1.
 *
 *    - Non-spacing and enclosing combining characters (general
 *      category code Mn or Me in the Unicode database) have a
 *      column width of 0.
 *
 *    - SOFT HYPHEN (U+00AD) has a column width of 1.
 *
 *    - Other format characters (general category code Cf in the Unicode
 *      database) and ZERO WIDTH SPACE (U+200B) have a column width of 0.
 *
 *    - Hangul Jamo medial vowels and final consonants (U+1160-U+11FF)
 *      have a column width of 0.
 *
 *    - Spacing characters in the East Asian Wide (W) or East Asian
 *      Full-width (F) category as defined in Unicode Technical
 *      Report #11 have a column width of 2.
 *
 *    - All remaining characters (including all printable
 *      ISO 8859-1 and WGL4 characters, Unicode control characters,
 *      etc.) have a column width of 1.
 *
 * This implementation assumes that wchar_t characters are encoded
 * in ISO 10646.
 */
int FAST_FUNC wcwidth(unsigned ucs)
{
# if CONFIG_LAST_SUPPORTED_WCHAR >= 0x300
	/* sorted list of non-overlapping intervals of non-spacing characters */
	/* generated by "uniset +cat=Me +cat=Mn +cat=Cf -00AD +1160-11FF +200B c" */
#  define BIG_(a,b) { a, b },
#  define PAIR(a,b)
#  define ARRAY /* PAIR if < 0x4000 and no more than 4 chars big */ \
		BIG_(0x0300, 0x036F) \
		PAIR(0x0483, 0x0486) \
		PAIR(0x0488, 0x0489) \
		BIG_(0x0591, 0x05BD) \
		PAIR(0x05BF, 0x05BF) \
		PAIR(0x05C1, 0x05C2) \
		PAIR(0x05C4, 0x05C5) \
		PAIR(0x05C7, 0x05C7) \
		PAIR(0x0600, 0x0603) \
		BIG_(0x0610, 0x0615) \
		BIG_(0x064B, 0x065E) \
		PAIR(0x0670, 0x0670) \
		BIG_(0x06D6, 0x06E4) \
		PAIR(0x06E7, 0x06E8) \
		PAIR(0x06EA, 0x06ED) \
		PAIR(0x070F, 0x070F) \
		PAIR(0x0711, 0x0711) \
		BIG_(0x0730, 0x074A) \
		BIG_(0x07A6, 0x07B0) \
		BIG_(0x07EB, 0x07F3) \
		PAIR(0x0901, 0x0902) \
		PAIR(0x093C, 0x093C) \
		BIG_(0x0941, 0x0948) \
		PAIR(0x094D, 0x094D) \
		PAIR(0x0951, 0x0954) \
		PAIR(0x0962, 0x0963) \
		PAIR(0x0981, 0x0981) \
		PAIR(0x09BC, 0x09BC) \
		PAIR(0x09C1, 0x09C4) \
		PAIR(0x09CD, 0x09CD) \
		PAIR(0x09E2, 0x09E3) \
		PAIR(0x0A01, 0x0A02) \
		PAIR(0x0A3C, 0x0A3C) \
		PAIR(0x0A41, 0x0A42) \
		PAIR(0x0A47, 0x0A48) \
		PAIR(0x0A4B, 0x0A4D) \
		PAIR(0x0A70, 0x0A71) \
		PAIR(0x0A81, 0x0A82) \
		PAIR(0x0ABC, 0x0ABC) \
		BIG_(0x0AC1, 0x0AC5) \
		PAIR(0x0AC7, 0x0AC8) \
		PAIR(0x0ACD, 0x0ACD) \
		PAIR(0x0AE2, 0x0AE3) \
		PAIR(0x0B01, 0x0B01) \
		PAIR(0x0B3C, 0x0B3C) \
		PAIR(0x0B3F, 0x0B3F) \
		PAIR(0x0B41, 0x0B43) \
		PAIR(0x0B4D, 0x0B4D) \
		PAIR(0x0B56, 0x0B56) \
		PAIR(0x0B82, 0x0B82) \
		PAIR(0x0BC0, 0x0BC0) \
		PAIR(0x0BCD, 0x0BCD) \
		PAIR(0x0C3E, 0x0C40) \
		PAIR(0x0C46, 0x0C48) \
		PAIR(0x0C4A, 0x0C4D) \
		PAIR(0x0C55, 0x0C56) \
		PAIR(0x0CBC, 0x0CBC) \
		PAIR(0x0CBF, 0x0CBF) \
		PAIR(0x0CC6, 0x0CC6) \
		PAIR(0x0CCC, 0x0CCD) \
		PAIR(0x0CE2, 0x0CE3) \
		PAIR(0x0D41, 0x0D43) \
		PAIR(0x0D4D, 0x0D4D) \
		PAIR(0x0DCA, 0x0DCA) \
		PAIR(0x0DD2, 0x0DD4) \
		PAIR(0x0DD6, 0x0DD6) \
		PAIR(0x0E31, 0x0E31) \
		BIG_(0x0E34, 0x0E3A) \
		BIG_(0x0E47, 0x0E4E) \
		PAIR(0x0EB1, 0x0EB1) \
		BIG_(0x0EB4, 0x0EB9) \
		PAIR(0x0EBB, 0x0EBC) \
		BIG_(0x0EC8, 0x0ECD) \
		PAIR(0x0F18, 0x0F19) \
		PAIR(0x0F35, 0x0F35) \
		PAIR(0x0F37, 0x0F37) \
		PAIR(0x0F39, 0x0F39) \
		BIG_(0x0F71, 0x0F7E) \
		BIG_(0x0F80, 0x0F84) \
		PAIR(0x0F86, 0x0F87) \
		PAIR(0x0FC6, 0x0FC6) \
		BIG_(0x0F90, 0x0F97) \
		BIG_(0x0F99, 0x0FBC) \
		PAIR(0x102D, 0x1030) \
		PAIR(0x1032, 0x1032) \
		PAIR(0x1036, 0x1037) \
		PAIR(0x1039, 0x1039) \
		PAIR(0x1058, 0x1059) \
		BIG_(0x1160, 0x11FF) \
		PAIR(0x135F, 0x135F) \
		PAIR(0x1712, 0x1714) \
		PAIR(0x1732, 0x1734) \
		PAIR(0x1752, 0x1753) \
		PAIR(0x1772, 0x1773) \
		PAIR(0x17B4, 0x17B5) \
		BIG_(0x17B7, 0x17BD) \
		PAIR(0x17C6, 0x17C6) \
		BIG_(0x17C9, 0x17D3) \
		PAIR(0x17DD, 0x17DD) \
		PAIR(0x180B, 0x180D) \
		PAIR(0x18A9, 0x18A9) \
		PAIR(0x1920, 0x1922) \
		PAIR(0x1927, 0x1928) \
		PAIR(0x1932, 0x1932) \
		PAIR(0x1939, 0x193B) \
		PAIR(0x1A17, 0x1A18) \
		PAIR(0x1B00, 0x1B03) \
		PAIR(0x1B34, 0x1B34) \
		BIG_(0x1B36, 0x1B3A) \
		PAIR(0x1B3C, 0x1B3C) \
		PAIR(0x1B42, 0x1B42) \
		BIG_(0x1B6B, 0x1B73) \
		BIG_(0x1DC0, 0x1DCA) \
		PAIR(0x1DFE, 0x1DFF) \
		BIG_(0x200B, 0x200F) \
		BIG_(0x202A, 0x202E) \
		PAIR(0x2060, 0x2063) \
		BIG_(0x206A, 0x206F) \
		BIG_(0x20D0, 0x20EF) \
		BIG_(0x302A, 0x302F) \
		PAIR(0x3099, 0x309A) \
		/* Too big to be packed in PAIRs: */ \
		BIG_(0xA806, 0xA806) \
		BIG_(0xA80B, 0xA80B) \
		BIG_(0xA825, 0xA826) \
		BIG_(0xFB1E, 0xFB1E) \
		BIG_(0xFE00, 0xFE0F) \
		BIG_(0xFE20, 0xFE23) \
		BIG_(0xFEFF, 0xFEFF) \
		BIG_(0xFFF9, 0xFFFB)
	static const struct interval combining[] = { ARRAY };
#  undef BIG_
#  undef PAIR
#  define BIG_(a,b)
#  define PAIR(a,b) (a << 2) | (b-a),
	static const uint16_t combining1[] = { ARRAY };
#  undef BIG_
#  undef PAIR
#  define BIG_(a,b) char big_##a[b < 0x4000 && b-a <= 3 ? -1 : 1];
#  define PAIR(a,b) char pair##a[b >= 0x4000 || b-a > 3 ? -1 : 1];
	struct CHECK { ARRAY };
#  undef BIG_
#  undef PAIR
#  undef ARRAY
# endif

	if (ucs == 0)
		return 0;

	/* Test for 8-bit control characters (00-1f, 80-9f, 7f) */
	if ((ucs & ~0x80) < 0x20 || ucs == 0x7f)
		return -1;
	/* Quick abort if it is an obviously invalid char */
	if (ucs > CONFIG_LAST_SUPPORTED_WCHAR)
		return -1;

	/* Optimization: no combining chars below 0x300 */
	if (CONFIG_LAST_SUPPORTED_WCHAR < 0x300 || ucs < 0x300)
		return 1;

# if CONFIG_LAST_SUPPORTED_WCHAR >= 0x300
	/* Binary search in table of non-spacing characters */
	if (in_interval_table(ucs, combining, ARRAY_SIZE(combining) - 1))
		return 0;
	if (in_uint16_table(ucs, combining1, ARRAY_SIZE(combining1) - 1))
		return 0;

	/* Optimization: all chars below 0x1100 are not double-width */
	if (CONFIG_LAST_SUPPORTED_WCHAR < 0x1100 || ucs < 0x1100)
		return 1;

#  if CONFIG_LAST_SUPPORTED_WCHAR >= 0x1100
	/* Invalid code points: */
	/* High (d800..dbff) and low (dc00..dfff) surrogates (valid only in UTF16) */
	/* Private Use Area (e000..f8ff) */
	/* Noncharacters fdd0..fdef */
	if ((CONFIG_LAST_SUPPORTED_WCHAR >= 0xd800 && ucs >= 0xd800 && ucs <= 0xf8ff)
	 || (CONFIG_LAST_SUPPORTED_WCHAR >= 0xfdd0 && ucs >= 0xfdd0 && ucs <= 0xfdef)
	) {
		return -1;
	}
	/* 0xfffe and 0xffff in every plane are invalid */
	if (CONFIG_LAST_SUPPORTED_WCHAR >= 0xfffe && ((ucs & 0xfffe) == 0xfffe)) {
		return -1;
	}

#   if CONFIG_LAST_SUPPORTED_WCHAR >= 0x10000
	if (ucs >= 0x10000) {
		/* Combining chars in Supplementary Multilingual Plane 0x1xxxx */
		static const struct interval combining0x10000[] = {
			{ 0x0A01, 0x0A03 }, { 0x0A05, 0x0A06 }, { 0x0A0C, 0x0A0F },
			{ 0x0A38, 0x0A3A }, { 0x0A3F, 0x0A3F }, { 0xD167, 0xD169 },
			{ 0xD173, 0xD182 }, { 0xD185, 0xD18B }, { 0xD1AA, 0xD1AD },
			{ 0xD242, 0xD244 }
		};
		/* Binary search in table of non-spacing characters in Supplementary Multilingual Plane */
		if (in_interval_table(ucs ^ 0x10000, combining0x10000, ARRAY_SIZE(combining0x10000) - 1))
			return 0;
		/* Check a few non-spacing chars in Supplementary Special-purpose Plane 0xExxxx */
		if (CONFIG_LAST_SUPPORTED_WCHAR >= 0xE0001
		 && (  ucs == 0xE0001
		    || (ucs >= 0xE0020 && ucs <= 0xE007F)
		    || (ucs >= 0xE0100 && ucs <= 0xE01EF)
		    )
		) {
			return 0;
		}
	}
#   endif

	/* If we arrive here, ucs is not a combining or C0/C1 control character.
	 * Check whether it's 1 char or 2-shar wide.
	 */
	return 1 +
		(  (/*ucs >= 0x1100 &&*/ ucs <= 0x115f) /* Hangul Jamo init. consonants */
		|| ucs == 0x2329 /* left-pointing angle bracket; also CJK punct. char */
		|| ucs == 0x232a /* right-pointing angle bracket; also CJK punct. char */
		|| (ucs >= 0x2e80 && ucs <= 0xa4cf && ucs != 0x303f) /* CJK ... Yi */
#   if CONFIG_LAST_SUPPORTED_WCHAR >= 0xac00
		|| (ucs >= 0xac00 && ucs <= 0xd7a3) /* Hangul Syllables */
		|| (ucs >= 0xf900 && ucs <= 0xfaff) /* CJK Compatibility Ideographs */
		|| (ucs >= 0xfe10 && ucs <= 0xfe19) /* Vertical forms */
		|| (ucs >= 0xfe30 && ucs <= 0xfe6f) /* CJK Compatibility Forms */
		|| (ucs >= 0xff00 && ucs <= 0xff60) /* Fullwidth Forms */
		|| (ucs >= 0xffe0 && ucs <= 0xffe6)
		|| ((ucs >> 17) == (2 >> 1)) /* 20000..3ffff: Supplementary and Tertiary Ideographic Planes */
#   endif
		);
#  endif /* >= 0x1100 */
# endif /* >= 0x300 */
}


# if ENABLE_UNICODE_BIDI_SUPPORT
int FAST_FUNC unicode_bidi_isrtl(wint_t wc)
{
	/* ranges taken from
	 * http://www.unicode.org/Public/5.2.0/ucd/extracted/DerivedBidiClass.txt
	 * Bidi_Class=Left_To_Right | Bidi_Class=Arabic_Letter
	 */
#  define BIG_(a,b) { a, b },
#  define PAIR(a,b)
#  define ARRAY \
		PAIR(0x0590, 0x0590) \
		PAIR(0x05BE, 0x05BE) \
		PAIR(0x05C0, 0x05C0) \
		PAIR(0x05C3, 0x05C3) \
		PAIR(0x05C6, 0x05C6) \
		BIG_(0x05C8, 0x05FF) \
		PAIR(0x0604, 0x0605) \
		PAIR(0x0608, 0x0608) \
		PAIR(0x060B, 0x060B) \
		PAIR(0x060D, 0x060D) \
		BIG_(0x061B, 0x064A) \
		PAIR(0x065F, 0x065F) \
		PAIR(0x066D, 0x066F) \
		BIG_(0x0671, 0x06D5) \
		PAIR(0x06E5, 0x06E6) \
		PAIR(0x06EE, 0x06EF) \
		BIG_(0x06FA, 0x070E) \
		PAIR(0x0710, 0x0710) \
		BIG_(0x0712, 0x072F) \
		BIG_(0x074B, 0x07A5) \
		BIG_(0x07B1, 0x07EA) \
		PAIR(0x07F4, 0x07F5) \
		BIG_(0x07FA, 0x0815) \
		PAIR(0x081A, 0x081A) \
		PAIR(0x0824, 0x0824) \
		PAIR(0x0828, 0x0828) \
		BIG_(0x082E, 0x08FF) \
		PAIR(0x200F, 0x200F) \
		PAIR(0x202B, 0x202B) \
		PAIR(0x202E, 0x202E) \
		BIG_(0xFB1D, 0xFB1D) \
		BIG_(0xFB1F, 0xFB28) \
		BIG_(0xFB2A, 0xFD3D) \
		BIG_(0xFD40, 0xFDCF) \
		BIG_(0xFDC8, 0xFDCF) \
		BIG_(0xFDF0, 0xFDFC) \
		BIG_(0xFDFE, 0xFDFF) \
		BIG_(0xFE70, 0xFEFE)
		/* Probably not necessary
		{0x10800, 0x1091E},
		{0x10920, 0x10A00},
		{0x10A04, 0x10A04},
		{0x10A07, 0x10A0B},
		{0x10A10, 0x10A37},
		{0x10A3B, 0x10A3E},
		{0x10A40, 0x10A7F},
		{0x10B36, 0x10B38},
		{0x10B40, 0x10E5F},
		{0x10E7F, 0x10FFF},
		{0x1E800, 0x1EFFF}
		*/
	static const struct interval rtl_b[] = { ARRAY };
#  undef BIG_
#  undef PAIR
#  define BIG_(a,b)
#  define PAIR(a,b) (a << 2) | (b-a),
	static const uint16_t rtl_p[] = { ARRAY };
#  undef BIG_
#  undef PAIR
#  define BIG_(a,b) char big_##a[b < 0x4000 && b-a <= 3 ? -1 : 1];
#  define PAIR(a,b) char pair##a[b >= 0x4000 || b-a > 3 ? -1 : 1];
	struct CHECK { ARRAY };
#  undef BIG_
#  undef PAIR
#  undef ARRAY

	if (in_interval_table(wc, rtl_b, ARRAY_SIZE(rtl_b) - 1))
		return 1;
	if (in_uint16_table(wc, rtl_p, ARRAY_SIZE(rtl_p) - 1))
		return 1;
	return 0;
}

#  if ENABLE_UNICODE_NEUTRAL_TABLE
int FAST_FUNC unicode_bidi_is_neutral_wchar(wint_t wc)
{
	/* ranges taken from
	 * http://www.unicode.org/Public/5.2.0/ucd/extracted/DerivedBidiClass.txt
	 * Bidi_Classes: Paragraph_Separator, Segment_Separator,
	 * White_Space, Other_Neutral, European_Number, European_Separator,
	 * European_Terminator, Arabic_Number, Common_Separator
	 */
#  define BIG_(a,b) { a, b },
#  define PAIR(a,b)
#  define ARRAY \
		BIG_(0x0009, 0x000D) \
		BIG_(0x001C, 0x0040) \
		BIG_(0x005B, 0x0060) \
		PAIR(0x007B, 0x007E) \
		PAIR(0x0085, 0x0085) \
		BIG_(0x00A0, 0x00A9) \
		PAIR(0x00AB, 0x00AC) \
		BIG_(0x00AE, 0x00B4) \
		PAIR(0x00B6, 0x00B9) \
		BIG_(0x00BB, 0x00BF) \
		PAIR(0x00D7, 0x00D7) \
		PAIR(0x00F7, 0x00F7) \
		PAIR(0x02B9, 0x02BA) \
		BIG_(0x02C2, 0x02CF) \
		BIG_(0x02D2, 0x02DF) \
		BIG_(0x02E5, 0x02FF) \
		PAIR(0x0374, 0x0375) \
		PAIR(0x037E, 0x037E) \
		PAIR(0x0384, 0x0385) \
		PAIR(0x0387, 0x0387) \
		PAIR(0x03F6, 0x03F6) \
		PAIR(0x058A, 0x058A) \
		PAIR(0x0600, 0x0603) \
		PAIR(0x0606, 0x0607) \
		PAIR(0x0609, 0x060A) \
		PAIR(0x060C, 0x060C) \
		PAIR(0x060E, 0x060F) \
		BIG_(0x0660, 0x066C) \
		PAIR(0x06DD, 0x06DD) \
		PAIR(0x06E9, 0x06E9) \
		BIG_(0x06F0, 0x06F9) \
		PAIR(0x07F6, 0x07F9) \
		PAIR(0x09F2, 0x09F3) \
		PAIR(0x09FB, 0x09FB) \
		PAIR(0x0AF1, 0x0AF1) \
		BIG_(0x0BF3, 0x0BFA) \
		BIG_(0x0C78, 0x0C7E) \
		PAIR(0x0CF1, 0x0CF2) \
		PAIR(0x0E3F, 0x0E3F) \
		PAIR(0x0F3A, 0x0F3D) \
		BIG_(0x1390, 0x1400) \
		PAIR(0x1680, 0x1680) \
		PAIR(0x169B, 0x169C) \
		PAIR(0x17DB, 0x17DB) \
		BIG_(0x17F0, 0x17F9) \
		BIG_(0x1800, 0x180A) \
		PAIR(0x180E, 0x180E) \
		PAIR(0x1940, 0x1940) \
		PAIR(0x1944, 0x1945) \
		BIG_(0x19DE, 0x19FF) \
		PAIR(0x1FBD, 0x1FBD) \
		PAIR(0x1FBF, 0x1FC1) \
		PAIR(0x1FCD, 0x1FCF) \
		PAIR(0x1FDD, 0x1FDF) \
		PAIR(0x1FED, 0x1FEF) \
		PAIR(0x1FFD, 0x1FFE) \
		BIG_(0x2000, 0x200A) \
		BIG_(0x2010, 0x2029) \
		BIG_(0x202F, 0x205F) \
		PAIR(0x2070, 0x2070) \
		BIG_(0x2074, 0x207E) \
		BIG_(0x2080, 0x208E) \
		BIG_(0x20A0, 0x20B8) \
		PAIR(0x2100, 0x2101) \
		PAIR(0x2103, 0x2106) \
		PAIR(0x2108, 0x2109) \
		PAIR(0x2114, 0x2114) \
		PAIR(0x2116, 0x2118) \
		BIG_(0x211E, 0x2123) \
		PAIR(0x2125, 0x2125) \
		PAIR(0x2127, 0x2127) \
		PAIR(0x2129, 0x2129) \
		PAIR(0x212E, 0x212E) \
		PAIR(0x213A, 0x213B) \
		BIG_(0x2140, 0x2144) \
		PAIR(0x214A, 0x214D) \
		BIG_(0x2150, 0x215F) \
		PAIR(0x2189, 0x2189) \
		BIG_(0x2190, 0x2335) \
		BIG_(0x237B, 0x2394) \
		BIG_(0x2396, 0x23E8) \
		BIG_(0x2400, 0x2426) \
		BIG_(0x2440, 0x244A) \
		BIG_(0x2460, 0x249B) \
		BIG_(0x24EA, 0x26AB) \
		BIG_(0x26AD, 0x26CD) \
		BIG_(0x26CF, 0x26E1) \
		PAIR(0x26E3, 0x26E3) \
		BIG_(0x26E8, 0x26FF) \
		PAIR(0x2701, 0x2704) \
		PAIR(0x2706, 0x2709) \
		BIG_(0x270C, 0x2727) \
		BIG_(0x2729, 0x274B) \
		PAIR(0x274D, 0x274D) \
		PAIR(0x274F, 0x2752) \
		BIG_(0x2756, 0x275E) \
		BIG_(0x2761, 0x2794) \
		BIG_(0x2798, 0x27AF) \
		BIG_(0x27B1, 0x27BE) \
		BIG_(0x27C0, 0x27CA) \
		PAIR(0x27CC, 0x27CC) \
		BIG_(0x27D0, 0x27FF) \
		BIG_(0x2900, 0x2B4C) \
		BIG_(0x2B50, 0x2B59) \
		BIG_(0x2CE5, 0x2CEA) \
		BIG_(0x2CF9, 0x2CFF) \
		BIG_(0x2E00, 0x2E99) \
		BIG_(0x2E9B, 0x2EF3) \
		BIG_(0x2F00, 0x2FD5) \
		BIG_(0x2FF0, 0x2FFB) \
		BIG_(0x3000, 0x3004) \
		BIG_(0x3008, 0x3020) \
		PAIR(0x3030, 0x3030) \
		PAIR(0x3036, 0x3037) \
		PAIR(0x303D, 0x303D) \
		PAIR(0x303E, 0x303F) \
		PAIR(0x309B, 0x309C) \
		PAIR(0x30A0, 0x30A0) \
		PAIR(0x30FB, 0x30FB) \
		BIG_(0x31C0, 0x31E3) \
		PAIR(0x321D, 0x321E) \
		BIG_(0x3250, 0x325F) \
		PAIR(0x327C, 0x327E) \
		BIG_(0x32B1, 0x32BF) \
		PAIR(0x32CC, 0x32CF) \
		PAIR(0x3377, 0x337A) \
		PAIR(0x33DE, 0x33DF) \
		PAIR(0x33FF, 0x33FF) \
		BIG_(0x4DC0, 0x4DFF) \
		BIG_(0xA490, 0xA4C6) \
		BIG_(0xA60D, 0xA60F) \
		BIG_(0xA673, 0xA673) \
		BIG_(0xA67E, 0xA67F) \
		BIG_(0xA700, 0xA721) \
		BIG_(0xA788, 0xA788) \
		BIG_(0xA828, 0xA82B) \
		BIG_(0xA838, 0xA839) \
		BIG_(0xA874, 0xA877) \
		BIG_(0xFB29, 0xFB29) \
		BIG_(0xFD3E, 0xFD3F) \
		BIG_(0xFDFD, 0xFDFD) \
		BIG_(0xFE10, 0xFE19) \
		BIG_(0xFE30, 0xFE52) \
		BIG_(0xFE54, 0xFE66) \
		BIG_(0xFE68, 0xFE6B) \
		BIG_(0xFF01, 0xFF20) \
		BIG_(0xFF3B, 0xFF40) \
		BIG_(0xFF5B, 0xFF65) \
		BIG_(0xFFE0, 0xFFE6) \
		BIG_(0xFFE8, 0xFFEE) \
		BIG_(0xFFF9, 0xFFFD)
		/*
		{0x10101, 0x10101},
		{0x10140, 0x1019B},
		{0x1091F, 0x1091F},
		{0x10B39, 0x10B3F},
		{0x10E60, 0x10E7E},
		{0x1D200, 0x1D241},
		{0x1D245, 0x1D245},
		{0x1D300, 0x1D356},
		{0x1D6DB, 0x1D6DB},
		{0x1D715, 0x1D715},
		{0x1D74F, 0x1D74F},
		{0x1D789, 0x1D789},
		{0x1D7C3, 0x1D7C3},
		{0x1D7CE, 0x1D7FF},
		{0x1F000, 0x1F02B},
		{0x1F030, 0x1F093},
		{0x1F100, 0x1F10A}
		*/
	static const struct interval neutral_b[] = { ARRAY };
#  undef BIG_
#  undef PAIR
#  define BIG_(a,b)
#  define PAIR(a,b) (a << 2) | (b-a),
	static const uint16_t neutral_p[] = { ARRAY };
#  undef BIG_
#  undef PAIR
#  define BIG_(a,b) char big_##a[b < 0x4000 && b-a <= 3 ? -1 : 1];
#  define PAIR(a,b) char pair##a[b >= 0x4000 || b-a > 3 ? -1 : 1];
	struct CHECK { ARRAY };
#  undef BIG_
#  undef PAIR
#  undef ARRAY

	if (in_interval_table(wc, neutral_b, ARRAY_SIZE(neutral_b) - 1))
		return 1;
	if (in_uint16_table(wc, neutral_p, ARRAY_SIZE(neutral_p) - 1))
		return 1;
	return 0;
}
#  endif

# endif /* UNICODE_BIDI_SUPPORT */

#endif /* Homegrown Unicode support */


/* The rest is mostly same for libc and for "homegrown" support */

size_t FAST_FUNC unicode_strlen(const char *string)
{
	size_t width = mbstowcs(NULL, string, INT_MAX);
	if (width == (size_t)-1L)
		return strlen(string);
	return width;
}

size_t FAST_FUNC unicode_strwidth(const char *string)
{
	uni_stat_t uni_stat;
	printable_string(&uni_stat, string);
	return uni_stat.unicode_width;
}

static char* FAST_FUNC unicode_conv_to_printable2(uni_stat_t *stats, const char *src, unsigned width, int flags)
{
	char *dst;
	unsigned dst_len;
	unsigned uni_count;
	unsigned uni_width;

	if (unicode_status != UNICODE_ON) {
		char *d;
		if (flags & UNI_FLAG_PAD) {
			d = dst = xmalloc(width + 1);
			while ((int)--width >= 0) {
				unsigned char c = *src;
				if (c == '\0') {
					do
						*d++ = ' ';
					while ((int)--width >= 0);
					break;
				}
				*d++ = (c >= ' ' && c < 0x7f) ? c : '?';
				src++;
			}
			*d = '\0';
		} else {
			d = dst = xstrndup(src, width);
			while (*d) {
				unsigned char c = *d;
				if (c < ' ' || c >= 0x7f)
					*d = '?';
				d++;
			}
		}
		if (stats) {
			stats->byte_count = (d - dst);
			stats->unicode_count = (d - dst);
			stats->unicode_width = (d - dst);
		}
		return dst;
	}

	dst = NULL;
	uni_count = uni_width = 0;
	dst_len = 0;
	while (1) {
		int w;
		wchar_t wc;

#if ENABLE_UNICODE_USING_LOCALE
		{
			mbstate_t mbst = { 0 };
			ssize_t rc = mbsrtowcs(&wc, &src, 1, &mbst);
			/* If invalid sequence is seen: -1 is returned,
			 * src points to the invalid sequence, errno = EILSEQ.
			 * Else number of wchars (excluding terminating L'\0')
			 * written to dest is returned.
			 * If len (here: 1) non-L'\0' wchars stored at dest,
			 * src points to the next char to be converted.
			 * If string is completely converted: src = NULL.
			 */
			if (rc == 0) /* end-of-string */
				break;
			if (rc < 0) { /* error */
				src++;
				goto subst;
			}
			if (!iswprint(wc))
				goto subst;
		}
#else
		src = mbstowc_internal(&wc, src);
		/* src is advanced to next mb char
		 * wc == ERROR_WCHAR: invalid sequence is seen
		 * else: wc is set
		 */
		if (wc == ERROR_WCHAR) /* error */
			goto subst;
		if (wc == 0) /* end-of-string */
			break;
#endif
		if (CONFIG_LAST_SUPPORTED_WCHAR && wc > CONFIG_LAST_SUPPORTED_WCHAR)
			goto subst;
		w = wcwidth(wc);
		if ((ENABLE_UNICODE_COMBINING_WCHARS && w < 0) /* non-printable wchar */
		 || (!ENABLE_UNICODE_COMBINING_WCHARS && w <= 0)
		 || (!ENABLE_UNICODE_WIDE_WCHARS && w > 1)
		) {
 subst:
			wc = CONFIG_SUBST_WCHAR;
			w = 1;
		}
		width -= w;
		/* Note: if width == 0, we still may add more chars,
		 * they may be zero-width or combining ones */
		if ((int)width < 0) {
			/* can't add this wc, string would become longer than width */
			width += w;
			break;
		}

		uni_count++;
		uni_width += w;
		dst = xrealloc(dst, dst_len + MB_CUR_MAX);
#if ENABLE_UNICODE_USING_LOCALE
		{
			mbstate_t mbst = { 0 };
			dst_len += wcrtomb(&dst[dst_len], wc, &mbst);
		}
#else
		dst_len += wcrtomb_internal(&dst[dst_len], wc);
#endif
	}

	/* Pad to remaining width */
	if (flags & UNI_FLAG_PAD) {
		dst = xrealloc(dst, dst_len + width + 1);
		uni_count += width;
		uni_width += width;
		while ((int)--width >= 0) {
			dst[dst_len++] = ' ';
		}
	}
	dst[dst_len] = '\0';
	if (stats) {
		stats->byte_count = dst_len;
		stats->unicode_count = uni_count;
		stats->unicode_width = uni_width;
	}

	return dst;
}
char* FAST_FUNC unicode_conv_to_printable(uni_stat_t *stats, const char *src)
{
	return unicode_conv_to_printable2(stats, src, INT_MAX, 0);
}
char* FAST_FUNC unicode_conv_to_printable_fixedwidth(/*uni_stat_t *stats,*/ const char *src, unsigned width)
{
	return unicode_conv_to_printable2(/*stats:*/ NULL, src, width, UNI_FLAG_PAD);
}

#ifdef UNUSED
char* FAST_FUNC unicode_conv_to_printable_maxwidth(uni_stat_t *stats, const char *src, unsigned maxwidth)
{
	return unicode_conv_to_printable2(stats, src, maxwidth, 0);
}

unsigned FAST_FUNC unicode_padding_to_width(unsigned width, const char *src)
{
	if (unicode_status != UNICODE_ON) {
		return width - strnlen(src, width);
	}

	while (1) {
		int w;
		wchar_t wc;

#if ENABLE_UNICODE_USING_LOCALE
		{
			mbstate_t mbst = { 0 };
			ssize_t rc = mbsrtowcs(&wc, &src, 1, &mbst);
			if (rc <= 0) /* error, or end-of-string */
				return width;
		}
#else
		src = mbstowc_internal(&wc, src);
		if (wc == ERROR_WCHAR || wc == 0) /* error, or end-of-string */
			return width;
#endif
		w = wcwidth(wc);
		if (w < 0) /* non-printable wchar */
			return width;
		width -= w;
		if ((int)width <= 0) /* string is longer than width */
			return 0;
	}
}
#endif
