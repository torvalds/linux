/*
 * Copyright (c) Ian F. Darwin 1986-1995.
 * Software written by Ian F. Darwin and others;
 * maintained 1995-present by Christos Zoulas and others.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice immediately at the beginning of the file, without modification,
 *    this list of conditions, and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
/*
 * Encoding -- determine the character encoding of a text file.
 *
 * Joerg Wunsch <joerg@freebsd.org> wrote the original support for 8-bit
 * international characters.
 */

#include "file.h"

#ifndef	lint
FILE_RCSID("@(#)$File: encoding.c,v 1.14 2017/11/02 20:25:39 christos Exp $")
#endif	/* lint */

#include "magic.h"
#include <string.h>
#include <memory.h>
#include <stdlib.h>


private int looks_ascii(const unsigned char *, size_t, unichar *, size_t *);
private int looks_utf8_with_BOM(const unsigned char *, size_t, unichar *,
    size_t *);
private int looks_utf7(const unsigned char *, size_t, unichar *, size_t *);
private int looks_ucs16(const unsigned char *, size_t, unichar *, size_t *);
private int looks_latin1(const unsigned char *, size_t, unichar *, size_t *);
private int looks_extended(const unsigned char *, size_t, unichar *, size_t *);
private void from_ebcdic(const unsigned char *, size_t, unsigned char *);

#ifdef DEBUG_ENCODING
#define DPRINTF(a) printf a
#else
#define DPRINTF(a)
#endif

/*
 * Try to determine whether text is in some character code we can
 * identify.  Each of these tests, if it succeeds, will leave
 * the text converted into one-unichar-per-character Unicode in
 * ubuf, and the number of characters converted in ulen.
 */
protected int
file_encoding(struct magic_set *ms, const struct buffer *b, unichar **ubuf,
    size_t *ulen, const char **code, const char **code_mime, const char **type)
{
	const unsigned char *buf = b->fbuf;
	size_t nbytes = b->flen;
	size_t mlen;
	int rv = 1, ucs_type;
	unsigned char *nbuf = NULL;
	unichar *udefbuf;
	size_t udeflen;

	if (ubuf == NULL)
		ubuf = &udefbuf;
	if (ulen == NULL)
		ulen = &udeflen;

	*type = "text";
	*ulen = 0;
	*code = "unknown";
	*code_mime = "binary";

	mlen = (nbytes + 1) * sizeof((*ubuf)[0]);
	if ((*ubuf = CAST(unichar *, calloc((size_t)1, mlen))) == NULL) {
		file_oomem(ms, mlen);
		goto done;
	}
	mlen = (nbytes + 1) * sizeof(nbuf[0]);
	if ((nbuf = CAST(unsigned char *, calloc((size_t)1, mlen))) == NULL) {
		file_oomem(ms, mlen);
		goto done;
	}

	if (looks_ascii(buf, nbytes, *ubuf, ulen)) {
		if (looks_utf7(buf, nbytes, *ubuf, ulen) > 0) {
			DPRINTF(("utf-7 %" SIZE_T_FORMAT "u\n", *ulen));
			*code = "UTF-7 Unicode";
			*code_mime = "utf-7";
		} else {
			DPRINTF(("ascii %" SIZE_T_FORMAT "u\n", *ulen));
			*code = "ASCII";
			*code_mime = "us-ascii";
		}
	} else if (looks_utf8_with_BOM(buf, nbytes, *ubuf, ulen) > 0) {
		DPRINTF(("utf8/bom %" SIZE_T_FORMAT "u\n", *ulen));
		*code = "UTF-8 Unicode (with BOM)";
		*code_mime = "utf-8";
	} else if (file_looks_utf8(buf, nbytes, *ubuf, ulen) > 1) {
		DPRINTF(("utf8 %" SIZE_T_FORMAT "u\n", *ulen));
		*code = "UTF-8 Unicode";
		*code_mime = "utf-8";
	} else if ((ucs_type = looks_ucs16(buf, nbytes, *ubuf, ulen)) != 0) {
		if (ucs_type == 1) {
			*code = "Little-endian UTF-16 Unicode";
			*code_mime = "utf-16le";
		} else {
			*code = "Big-endian UTF-16 Unicode";
			*code_mime = "utf-16be";
		}
		DPRINTF(("ucs16 %" SIZE_T_FORMAT "u\n", *ulen));
	} else if (looks_latin1(buf, nbytes, *ubuf, ulen)) {
		DPRINTF(("latin1 %" SIZE_T_FORMAT "u\n", *ulen));
		*code = "ISO-8859";
		*code_mime = "iso-8859-1";
	} else if (looks_extended(buf, nbytes, *ubuf, ulen)) {
		DPRINTF(("extended %" SIZE_T_FORMAT "u\n", *ulen));
		*code = "Non-ISO extended-ASCII";
		*code_mime = "unknown-8bit";
	} else {
		from_ebcdic(buf, nbytes, nbuf);

		if (looks_ascii(nbuf, nbytes, *ubuf, ulen)) {
			DPRINTF(("ebcdic %" SIZE_T_FORMAT "u\n", *ulen));
			*code = "EBCDIC";
			*code_mime = "ebcdic";
		} else if (looks_latin1(nbuf, nbytes, *ubuf, ulen)) {
			DPRINTF(("ebcdic/international %" SIZE_T_FORMAT "u\n",
			    *ulen));
			*code = "International EBCDIC";
			*code_mime = "ebcdic";
		} else { /* Doesn't look like text at all */
			DPRINTF(("binary\n"));
			rv = 0;
			*type = "binary";
		}
	}

 done:
	free(nbuf);
	if (ubuf == &udefbuf)
		free(udefbuf);

	return rv;
}

/*
 * This table reflects a particular philosophy about what constitutes
 * "text," and there is room for disagreement about it.
 *
 * Version 3.31 of the file command considered a file to be ASCII if
 * each of its characters was approved by either the isascii() or
 * isalpha() function.  On most systems, this would mean that any
 * file consisting only of characters in the range 0x00 ... 0x7F
 * would be called ASCII text, but many systems might reasonably
 * consider some characters outside this range to be alphabetic,
 * so the file command would call such characters ASCII.  It might
 * have been more accurate to call this "considered textual on the
 * local system" than "ASCII."
 *
 * It considered a file to be "International language text" if each
 * of its characters was either an ASCII printing character (according
 * to the real ASCII standard, not the above test), a character in
 * the range 0x80 ... 0xFF, or one of the following control characters:
 * backspace, tab, line feed, vertical tab, form feed, carriage return,
 * escape.  No attempt was made to determine the language in which files
 * of this type were written.
 *
 *
 * The table below considers a file to be ASCII if all of its characters
 * are either ASCII printing characters (again, according to the X3.4
 * standard, not isascii()) or any of the following controls: bell,
 * backspace, tab, line feed, form feed, carriage return, esc, nextline.
 *
 * I include bell because some programs (particularly shell scripts)
 * use it literally, even though it is rare in normal text.  I exclude
 * vertical tab because it never seems to be used in real text.  I also
 * include, with hesitation, the X3.64/ECMA-43 control nextline (0x85),
 * because that's what the dd EBCDIC->ASCII table maps the EBCDIC newline
 * character to.  It might be more appropriate to include it in the 8859
 * set instead of the ASCII set, but it's got to be included in *something*
 * we recognize or EBCDIC files aren't going to be considered textual.
 * Some old Unix source files use SO/SI (^N/^O) to shift between Greek
 * and Latin characters, so these should possibly be allowed.  But they
 * make a real mess on VT100-style displays if they're not paired properly,
 * so we are probably better off not calling them text.
 *
 * A file is considered to be ISO-8859 text if its characters are all
 * either ASCII, according to the above definition, or printing characters
 * from the ISO-8859 8-bit extension, characters 0xA0 ... 0xFF.
 *
 * Finally, a file is considered to be international text from some other
 * character code if its characters are all either ISO-8859 (according to
 * the above definition) or characters in the range 0x80 ... 0x9F, which
 * ISO-8859 considers to be control characters but the IBM PC and Macintosh
 * consider to be printing characters.
 */

#define F 0   /* character never appears in text */
#define T 1   /* character appears in plain ASCII text */
#define I 2   /* character appears in ISO-8859 text */
#define X 3   /* character appears in non-ISO extended ASCII (Mac, IBM PC) */

private char text_chars[256] = {
	/*                  BEL BS HT LF VT FF CR    */
	F, F, F, F, F, F, F, T, T, T, T, T, T, T, F, F,  /* 0x0X */
	/*                              ESC          */
	F, F, F, F, F, F, F, F, F, F, F, T, F, F, F, F,  /* 0x1X */
	T, T, T, T, T, T, T, T, T, T, T, T, T, T, T, T,  /* 0x2X */
	T, T, T, T, T, T, T, T, T, T, T, T, T, T, T, T,  /* 0x3X */
	T, T, T, T, T, T, T, T, T, T, T, T, T, T, T, T,  /* 0x4X */
	T, T, T, T, T, T, T, T, T, T, T, T, T, T, T, T,  /* 0x5X */
	T, T, T, T, T, T, T, T, T, T, T, T, T, T, T, T,  /* 0x6X */
	T, T, T, T, T, T, T, T, T, T, T, T, T, T, T, F,  /* 0x7X */
	/*            NEL                            */
	X, X, X, X, X, T, X, X, X, X, X, X, X, X, X, X,  /* 0x8X */
	X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X,  /* 0x9X */
	I, I, I, I, I, I, I, I, I, I, I, I, I, I, I, I,  /* 0xaX */
	I, I, I, I, I, I, I, I, I, I, I, I, I, I, I, I,  /* 0xbX */
	I, I, I, I, I, I, I, I, I, I, I, I, I, I, I, I,  /* 0xcX */
	I, I, I, I, I, I, I, I, I, I, I, I, I, I, I, I,  /* 0xdX */
	I, I, I, I, I, I, I, I, I, I, I, I, I, I, I, I,  /* 0xeX */
	I, I, I, I, I, I, I, I, I, I, I, I, I, I, I, I   /* 0xfX */
};

private int
looks_ascii(const unsigned char *buf, size_t nbytes, unichar *ubuf,
    size_t *ulen)
{
	size_t i;

	*ulen = 0;

	for (i = 0; i < nbytes; i++) {
		int t = text_chars[buf[i]];

		if (t != T)
			return 0;

		ubuf[(*ulen)++] = buf[i];
	}

	return 1;
}

private int
looks_latin1(const unsigned char *buf, size_t nbytes, unichar *ubuf, size_t *ulen)
{
	size_t i;

	*ulen = 0;

	for (i = 0; i < nbytes; i++) {
		int t = text_chars[buf[i]];

		if (t != T && t != I)
			return 0;

		ubuf[(*ulen)++] = buf[i];
	}

	return 1;
}

private int
looks_extended(const unsigned char *buf, size_t nbytes, unichar *ubuf,
    size_t *ulen)
{
	size_t i;

	*ulen = 0;

	for (i = 0; i < nbytes; i++) {
		int t = text_chars[buf[i]];

		if (t != T && t != I && t != X)
			return 0;

		ubuf[(*ulen)++] = buf[i];
	}

	return 1;
}

/*
 * Decide whether some text looks like UTF-8. Returns:
 *
 *     -1: invalid UTF-8
 *      0: uses odd control characters, so doesn't look like text
 *      1: 7-bit text
 *      2: definitely UTF-8 text (valid high-bit set bytes)
 *
 * If ubuf is non-NULL on entry, text is decoded into ubuf, *ulen;
 * ubuf must be big enough!
 */
protected int
file_looks_utf8(const unsigned char *buf, size_t nbytes, unichar *ubuf, size_t *ulen)
{
	size_t i;
	int n;
	unichar c;
	int gotone = 0, ctrl = 0;

	if (ubuf)
		*ulen = 0;

	for (i = 0; i < nbytes; i++) {
		if ((buf[i] & 0x80) == 0) {	   /* 0xxxxxxx is plain ASCII */
			/*
			 * Even if the whole file is valid UTF-8 sequences,
			 * still reject it if it uses weird control characters.
			 */

			if (text_chars[buf[i]] != T)
				ctrl = 1;

			if (ubuf)
				ubuf[(*ulen)++] = buf[i];
		} else if ((buf[i] & 0x40) == 0) { /* 10xxxxxx never 1st byte */
			return -1;
		} else {			   /* 11xxxxxx begins UTF-8 */
			int following;

			if ((buf[i] & 0x20) == 0) {		/* 110xxxxx */
				c = buf[i] & 0x1f;
				following = 1;
			} else if ((buf[i] & 0x10) == 0) {	/* 1110xxxx */
				c = buf[i] & 0x0f;
				following = 2;
			} else if ((buf[i] & 0x08) == 0) {	/* 11110xxx */
				c = buf[i] & 0x07;
				following = 3;
			} else if ((buf[i] & 0x04) == 0) {	/* 111110xx */
				c = buf[i] & 0x03;
				following = 4;
			} else if ((buf[i] & 0x02) == 0) {	/* 1111110x */
				c = buf[i] & 0x01;
				following = 5;
			} else
				return -1;

			for (n = 0; n < following; n++) {
				i++;
				if (i >= nbytes)
					goto done;

				if ((buf[i] & 0x80) == 0 || (buf[i] & 0x40))
					return -1;

				c = (c << 6) + (buf[i] & 0x3f);
			}

			if (ubuf)
				ubuf[(*ulen)++] = c;
			gotone = 1;
		}
	}
done:
	return ctrl ? 0 : (gotone ? 2 : 1);
}

/*
 * Decide whether some text looks like UTF-8 with BOM. If there is no
 * BOM, return -1; otherwise return the result of looks_utf8 on the
 * rest of the text.
 */
private int
looks_utf8_with_BOM(const unsigned char *buf, size_t nbytes, unichar *ubuf,
    size_t *ulen)
{
	if (nbytes > 3 && buf[0] == 0xef && buf[1] == 0xbb && buf[2] == 0xbf)
		return file_looks_utf8(buf + 3, nbytes - 3, ubuf, ulen);
	else
		return -1;
}

private int
looks_utf7(const unsigned char *buf, size_t nbytes, unichar *ubuf, size_t *ulen)
{
	if (nbytes > 4 && buf[0] == '+' && buf[1] == '/' && buf[2] == 'v')
		switch (buf[3]) {
		case '8':
		case '9':
		case '+':
		case '/':
			if (ubuf)
				*ulen = 0;
			return 1;
		default:
			return -1;
		}
	else
		return -1;
}

private int
looks_ucs16(const unsigned char *buf, size_t nbytes, unichar *ubuf,
    size_t *ulen)
{
	int bigend;
	size_t i;

	if (nbytes < 2)
		return 0;

	if (buf[0] == 0xff && buf[1] == 0xfe)
		bigend = 0;
	else if (buf[0] == 0xfe && buf[1] == 0xff)
		bigend = 1;
	else
		return 0;

	*ulen = 0;

	for (i = 2; i + 1 < nbytes; i += 2) {
		/* XXX fix to properly handle chars > 65536 */

		if (bigend)
			ubuf[(*ulen)++] = buf[i + 1] + 256 * buf[i];
		else
			ubuf[(*ulen)++] = buf[i] + 256 * buf[i + 1];

		if (ubuf[*ulen - 1] == 0xfffe)
			return 0;
		if (ubuf[*ulen - 1] < 128 &&
		    text_chars[(size_t)ubuf[*ulen - 1]] != T)
			return 0;
	}

	return 1 + bigend;
}

#undef F
#undef T
#undef I
#undef X

/*
 * This table maps each EBCDIC character to an (8-bit extended) ASCII
 * character, as specified in the rationale for the dd(1) command in
 * draft 11.2 (September, 1991) of the POSIX P1003.2 standard.
 *
 * Unfortunately it does not seem to correspond exactly to any of the
 * five variants of EBCDIC documented in IBM's _Enterprise Systems
 * Architecture/390: Principles of Operation_, SA22-7201-06, Seventh
 * Edition, July, 1999, pp. I-1 - I-4.
 *
 * Fortunately, though, all versions of EBCDIC, including this one, agree
 * on most of the printing characters that also appear in (7-bit) ASCII.
 * Of these, only '|', '!', '~', '^', '[', and ']' are in question at all.
 *
 * Fortunately too, there is general agreement that codes 0x00 through
 * 0x3F represent control characters, 0x41 a nonbreaking space, and the
 * remainder printing characters.
 *
 * This is sufficient to allow us to identify EBCDIC text and to distinguish
 * between old-style and internationalized examples of text.
 */

private unsigned char ebcdic_to_ascii[] = {
  0,   1,   2,   3, 156,   9, 134, 127, 151, 141, 142,  11,  12,  13,  14,  15,
 16,  17,  18,  19, 157, 133,   8, 135,  24,  25, 146, 143,  28,  29,  30,  31,
128, 129, 130, 131, 132,  10,  23,  27, 136, 137, 138, 139, 140,   5,   6,   7,
144, 145,  22, 147, 148, 149, 150,   4, 152, 153, 154, 155,  20,  21, 158,  26,
' ', 160, 161, 162, 163, 164, 165, 166, 167, 168, 213, '.', '<', '(', '+', '|',
'&', 169, 170, 171, 172, 173, 174, 175, 176, 177, '!', '$', '*', ')', ';', '~',
'-', '/', 178, 179, 180, 181, 182, 183, 184, 185, 203, ',', '%', '_', '>', '?',
186, 187, 188, 189, 190, 191, 192, 193, 194, '`', ':', '#', '@', '\'','=', '"',
195, 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 196, 197, 198, 199, 200, 201,
202, 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', '^', 204, 205, 206, 207, 208,
209, 229, 's', 't', 'u', 'v', 'w', 'x', 'y', 'z', 210, 211, 212, '[', 214, 215,
216, 217, 218, 219, 220, 221, 222, 223, 224, 225, 226, 227, 228, ']', 230, 231,
'{', 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 232, 233, 234, 235, 236, 237,
'}', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 238, 239, 240, 241, 242, 243,
'\\',159, 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', 244, 245, 246, 247, 248, 249,
'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 250, 251, 252, 253, 254, 255
};

#ifdef notdef
/*
 * The following EBCDIC-to-ASCII table may relate more closely to reality,
 * or at least to modern reality.  It comes from
 *
 *   http://ftp.s390.ibm.com/products/oe/bpxqp9.html
 *
 * and maps the characters of EBCDIC code page 1047 (the code used for
 * Unix-derived software on IBM's 390 systems) to the corresponding
 * characters from ISO 8859-1.
 *
 * If this table is used instead of the above one, some of the special
 * cases for the NEL character can be taken out of the code.
 */

private unsigned char ebcdic_1047_to_8859[] = {
0x00,0x01,0x02,0x03,0x9C,0x09,0x86,0x7F,0x97,0x8D,0x8E,0x0B,0x0C,0x0D,0x0E,0x0F,
0x10,0x11,0x12,0x13,0x9D,0x0A,0x08,0x87,0x18,0x19,0x92,0x8F,0x1C,0x1D,0x1E,0x1F,
0x80,0x81,0x82,0x83,0x84,0x85,0x17,0x1B,0x88,0x89,0x8A,0x8B,0x8C,0x05,0x06,0x07,
0x90,0x91,0x16,0x93,0x94,0x95,0x96,0x04,0x98,0x99,0x9A,0x9B,0x14,0x15,0x9E,0x1A,
0x20,0xA0,0xE2,0xE4,0xE0,0xE1,0xE3,0xE5,0xE7,0xF1,0xA2,0x2E,0x3C,0x28,0x2B,0x7C,
0x26,0xE9,0xEA,0xEB,0xE8,0xED,0xEE,0xEF,0xEC,0xDF,0x21,0x24,0x2A,0x29,0x3B,0x5E,
0x2D,0x2F,0xC2,0xC4,0xC0,0xC1,0xC3,0xC5,0xC7,0xD1,0xA6,0x2C,0x25,0x5F,0x3E,0x3F,
0xF8,0xC9,0xCA,0xCB,0xC8,0xCD,0xCE,0xCF,0xCC,0x60,0x3A,0x23,0x40,0x27,0x3D,0x22,
0xD8,0x61,0x62,0x63,0x64,0x65,0x66,0x67,0x68,0x69,0xAB,0xBB,0xF0,0xFD,0xFE,0xB1,
0xB0,0x6A,0x6B,0x6C,0x6D,0x6E,0x6F,0x70,0x71,0x72,0xAA,0xBA,0xE6,0xB8,0xC6,0xA4,
0xB5,0x7E,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7A,0xA1,0xBF,0xD0,0x5B,0xDE,0xAE,
0xAC,0xA3,0xA5,0xB7,0xA9,0xA7,0xB6,0xBC,0xBD,0xBE,0xDD,0xA8,0xAF,0x5D,0xB4,0xD7,
0x7B,0x41,0x42,0x43,0x44,0x45,0x46,0x47,0x48,0x49,0xAD,0xF4,0xF6,0xF2,0xF3,0xF5,
0x7D,0x4A,0x4B,0x4C,0x4D,0x4E,0x4F,0x50,0x51,0x52,0xB9,0xFB,0xFC,0xF9,0xFA,0xFF,
0x5C,0xF7,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5A,0xB2,0xD4,0xD6,0xD2,0xD3,0xD5,
0x30,0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38,0x39,0xB3,0xDB,0xDC,0xD9,0xDA,0x9F
};
#endif

/*
 * Copy buf[0 ... nbytes-1] into out[], translating EBCDIC to ASCII.
 */
private void
from_ebcdic(const unsigned char *buf, size_t nbytes, unsigned char *out)
{
	size_t i;

	for (i = 0; i < nbytes; i++) {
		out[i] = ebcdic_to_ascii[buf[i]];
	}
}
