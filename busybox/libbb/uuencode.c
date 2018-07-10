/* vi: set sw=4 ts=4: */
/*
 * Copyright 2003, Glenn McGrath
 * Copyright 2006, Rob Landley <rob@landley.net>
 * Copyright 2010, Denys Vlasenko
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
#include "libbb.h"

/* Conversion table.  for base 64 */
const char bb_uuenc_tbl_base64[65 + 1] ALIGN1 = {
	'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H',
	'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
	'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X',
	'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f',
	'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',
	'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
	'w', 'x', 'y', 'z', '0', '1', '2', '3',
	'4', '5', '6', '7', '8', '9', '+', '/',
	'=' /* termination character */,
	'\0' /* needed for uudecode.c only */
};

const char bb_uuenc_tbl_std[65] ALIGN1 = {
	'`', '!', '"', '#', '$', '%', '&', '\'',
	'(', ')', '*', '+', ',', '-', '.', '/',
	'0', '1', '2', '3', '4', '5', '6', '7',
	'8', '9', ':', ';', '<', '=', '>', '?',
	'@', 'A', 'B', 'C', 'D', 'E', 'F', 'G',
	'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O',
	'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W',
	'X', 'Y', 'Z', '[', '\\', ']', '^', '_',
	'`' /* termination character */
};

/*
 * Encode bytes at S of length LENGTH to uuencode or base64 format and place it
 * to STORE.  STORE will be 0-terminated, and must point to a writable
 * buffer of at least 1+BASE64_LENGTH(length) bytes.
 * where BASE64_LENGTH(len) = (4 * ((LENGTH + 2) / 3))
 */
void FAST_FUNC bb_uuencode(char *p, const void *src, int length, const char *tbl)
{
	const unsigned char *s = src;

	/* Transform the 3x8 bits to 4x6 bits */
	while (length > 0) {
		unsigned s1, s2;

		/* Are s[1], s[2] valid or should be assumed 0? */
		s1 = s2 = 0;
		length -= 3; /* can be >=0, -1, -2 */
		if (length >= -1) {
			s1 = s[1];
			if (length >= 0)
				s2 = s[2];
		}
		*p++ = tbl[s[0] >> 2];
		*p++ = tbl[((s[0] & 3) << 4) + (s1 >> 4)];
		*p++ = tbl[((s1 & 0xf) << 2) + (s2 >> 6)];
		*p++ = tbl[s2 & 0x3f];
		s += 3;
	}
	/* Zero-terminate */
	*p = '\0';
	/* If length is -2 or -1, pad last char or two */
	while (length) {
		*--p = tbl[64];
		length++;
	}
}

/*
 * Decode base64 encoded string. Stops on '\0'.
 *
 * Returns: pointer to the undecoded part of source.
 * If points to '\0', then the source was fully decoded.
 * (*pp_dst): advanced past the last written byte.
 */
const char* FAST_FUNC decode_base64(char **pp_dst, const char *src)
{
	char *dst = *pp_dst;
	const char *src_tail;

	while (1) {
		unsigned char six_bit[4];
		int count = 0;

		/* Fetch up to four 6-bit values */
		src_tail = src;
		while (count < 4) {
			char *table_ptr;
			int ch;

			/* Get next _valid_ character.
			 * bb_uuenc_tbl_base64[] contains this string:
			 *  0         1         2         3         4         5         6
			 *  01234567890123456789012345678901234567890123456789012345678901234
			 * "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/="
			 */
			do {
				ch = *src;
				if (ch == '\0') {
					if (count == 0) {
						/* Example:
						 * If we decode "QUJD <NUL>", we want
						 * to return ptr to NUL, not to ' ',
						 * because we did fully decode
						 * the string (to "ABC").
						 */
						src_tail = src;
					}
					goto ret;
				}
				src++;
				table_ptr = strchr(bb_uuenc_tbl_base64, ch);
//TODO: add BASE64_FLAG_foo to die on bad char?
			} while (!table_ptr);

			/* Convert encoded character to decimal */
			ch = table_ptr - bb_uuenc_tbl_base64;

			/* ch is 64 if char was '=', otherwise 0..63 */
			if (ch == 64)
				break;
			six_bit[count] = ch;
			count++;
		}

		/* Transform 6-bit values to 8-bit ones.
		 * count can be < 4 when we decode the tail:
		 * "eQ==" -> "y", not "y NUL NUL".
		 * Note that (count > 1) is always true,
		 * "x===" encoding is not valid:
		 * even a single zero byte encodes as "AA==".
		 * However, with current logic we come here with count == 1
		 * when we decode "==" tail.
		 */
		if (count > 1)
			*dst++ = six_bit[0] << 2 | six_bit[1] >> 4;
		if (count > 2)
			*dst++ = six_bit[1] << 4 | six_bit[2] >> 2;
		if (count > 3)
			*dst++ = six_bit[2] << 6 | six_bit[3];
		/* Note that if we decode "AA==" and ate first '=',
		 * we just decoded one char (count == 2) and now we'll
		 * do the loop once more to decode second '='.
		 */
	} /* while (1) */
 ret:
	*pp_dst = dst;
	return src_tail;
}

/*
 * Decode base64 encoded stream.
 * Can stop on EOF, specified char, or on uuencode-style "====" line:
 * flags argument controls it.
 */
void FAST_FUNC read_base64(FILE *src_stream, FILE *dst_stream, int flags)
{
/* Note that EOF _can_ be passed as exit_char too */
#define exit_char    ((int)(signed char)flags)
#define uu_style_end (flags & BASE64_FLAG_UU_STOP)

	/* uuencoded files have 61 byte lines. Use 64 byte buffer
	 * to process line at a time.
	 */
	enum { BUFFER_SIZE = 64 };

	char in_buf[BUFFER_SIZE + 2];
	char out_buf[BUFFER_SIZE / 4 * 3 + 2];
	char *out_tail;
	const char *in_tail;
	int term_seen = 0;
	int in_count = 0;

	while (1) {
		while (in_count < BUFFER_SIZE) {
			int ch = fgetc(src_stream);
			if (ch == exit_char) {
				if (in_count == 0)
					return;
				term_seen = 1;
				break;
			}
			if (ch == EOF) {
				term_seen = 1;
				break;
			}
			/* Prevent "====" line to be split: stop if we see '\n'.
			 * We can also skip other whitespace and skirt the problem
			 * of files with NULs by stopping on any control char or space:
			 */
			if (ch <= ' ')
				break;
			in_buf[in_count++] = ch;
		}
		in_buf[in_count] = '\0';

		/* Did we encounter "====" line? */
		if (uu_style_end && strcmp(in_buf, "====") == 0)
			return;

		out_tail = out_buf;
		in_tail = decode_base64(&out_tail, in_buf);

		fwrite(out_buf, (out_tail - out_buf), 1, dst_stream);

		if (term_seen) {
			/* Did we consume ALL characters? */
			if (*in_tail == '\0')
				return;
			/* No */
			bb_error_msg_and_die("truncated base64 input");
		}

		/* It was partial decode */
		in_count = strlen(in_tail);
		memmove(in_buf, in_tail, in_count);
	}
}
