/*
 * Copyright 2017 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

/*
 * This version of ctype.h provides a standardised and platform
 * independent implementation that supports seven bit ASCII characters.
 * The specific intent is to not pass extended ASCII characters (> 127)
 * even if the host operating system would.
 *
 * There is EBCDIC support included for machines which use this.  However,
 * there are a number of concerns about how well EBCDIC is supported
 * throughout the rest of the source code.  Refer to issue #4154 for
 * details.
 */
#ifndef INTERNAL_CTYPE_H
# define INTERNAL_CTYPE_H

# define CTYPE_MASK_lower       0x1
# define CTYPE_MASK_upper       0x2
# define CTYPE_MASK_digit       0x4
# define CTYPE_MASK_space       0x8
# define CTYPE_MASK_xdigit      0x10
# define CTYPE_MASK_blank       0x20
# define CTYPE_MASK_cntrl       0x40
# define CTYPE_MASK_graph       0x80
# define CTYPE_MASK_print       0x100
# define CTYPE_MASK_punct       0x200
# define CTYPE_MASK_base64      0x400
# define CTYPE_MASK_asn1print   0x800

# define CTYPE_MASK_alpha   (CTYPE_MASK_lower | CTYPE_MASK_upper)
# define CTYPE_MASK_alnum   (CTYPE_MASK_alpha | CTYPE_MASK_digit)

/*
 * The ascii mask assumes that any other classification implies that
 * the character is ASCII and that there are no ASCII characters
 * that aren't in any of the classifications.
 *
 * This assumption holds at the moment, but it might not in the future.
 */
# define CTYPE_MASK_ascii   (~0)

# ifdef CHARSET_EBCDIC
int ossl_toascii(int c);
int ossl_fromascii(int c);
# else
#  define ossl_toascii(c)       (c)
#  define ossl_fromascii(c)     (c)
# endif
int ossl_ctype_check(int c, unsigned int mask);
int ossl_tolower(int c);
int ossl_toupper(int c);

# define ossl_isalnum(c)        (ossl_ctype_check((c), CTYPE_MASK_alnum))
# define ossl_isalpha(c)        (ossl_ctype_check((c), CTYPE_MASK_alpha))
# ifdef CHARSET_EBCDIC
# define ossl_isascii(c)        (ossl_ctype_check((c), CTYPE_MASK_ascii))
# else
# define ossl_isascii(c)        (((c) & ~127) == 0)
# endif
# define ossl_isblank(c)        (ossl_ctype_check((c), CTYPE_MASK_blank))
# define ossl_iscntrl(c)        (ossl_ctype_check((c), CTYPE_MASK_cntrl))
# define ossl_isdigit(c)        (ossl_ctype_check((c), CTYPE_MASK_digit))
# define ossl_isgraph(c)        (ossl_ctype_check((c), CTYPE_MASK_graph))
# define ossl_islower(c)        (ossl_ctype_check((c), CTYPE_MASK_lower))
# define ossl_isprint(c)        (ossl_ctype_check((c), CTYPE_MASK_print))
# define ossl_ispunct(c)        (ossl_ctype_check((c), CTYPE_MASK_punct))
# define ossl_isspace(c)        (ossl_ctype_check((c), CTYPE_MASK_space))
# define ossl_isupper(c)        (ossl_ctype_check((c), CTYPE_MASK_upper))
# define ossl_isxdigit(c)       (ossl_ctype_check((c), CTYPE_MASK_xdigit))
# define ossl_isbase64(c)       (ossl_ctype_check((c), CTYPE_MASK_base64))
# define ossl_isasn1print(c)    (ossl_ctype_check((c), CTYPE_MASK_asn1print))

#endif
