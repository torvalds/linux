/*
 * Copyright 2017 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <string.h>
#include <stdio.h>
#include "internal/ctype.h"
#include "openssl/ebcdic.h"

/*
 * Define the character classes for each character in the seven bit ASCII
 * character set.  This is independent of the host's character set, characters
 * are converted to ASCII before being used as an index in to this table.
 * Characters outside of the seven bit ASCII range are detected before indexing.
 */
static const unsigned short ctype_char_map[128] = {
   /* 00 nul */ CTYPE_MASK_cntrl,
   /* 01 soh */ CTYPE_MASK_cntrl,
   /* 02 stx */ CTYPE_MASK_cntrl,
   /* 03 etx */ CTYPE_MASK_cntrl,
   /* 04 eot */ CTYPE_MASK_cntrl,
   /* 05 enq */ CTYPE_MASK_cntrl,
   /* 06 ack */ CTYPE_MASK_cntrl,
   /* 07 \a  */ CTYPE_MASK_cntrl,
   /* 08 \b  */ CTYPE_MASK_cntrl,
   /* 09 \t  */ CTYPE_MASK_blank | CTYPE_MASK_cntrl | CTYPE_MASK_space,
   /* 0A \n  */ CTYPE_MASK_cntrl | CTYPE_MASK_space,
   /* 0B \v  */ CTYPE_MASK_cntrl | CTYPE_MASK_space,
   /* 0C \f  */ CTYPE_MASK_cntrl | CTYPE_MASK_space,
   /* 0D \r  */ CTYPE_MASK_cntrl | CTYPE_MASK_space,
   /* 0E so  */ CTYPE_MASK_cntrl,
   /* 0F si  */ CTYPE_MASK_cntrl,
   /* 10 dle */ CTYPE_MASK_cntrl,
   /* 11 dc1 */ CTYPE_MASK_cntrl,
   /* 12 dc2 */ CTYPE_MASK_cntrl,
   /* 13 dc3 */ CTYPE_MASK_cntrl,
   /* 14 dc4 */ CTYPE_MASK_cntrl,
   /* 15 nak */ CTYPE_MASK_cntrl,
   /* 16 syn */ CTYPE_MASK_cntrl,
   /* 17 etb */ CTYPE_MASK_cntrl,
   /* 18 can */ CTYPE_MASK_cntrl,
   /* 19 em  */ CTYPE_MASK_cntrl,
   /* 1A sub */ CTYPE_MASK_cntrl,
   /* 1B esc */ CTYPE_MASK_cntrl,
   /* 1C fs  */ CTYPE_MASK_cntrl,
   /* 1D gs  */ CTYPE_MASK_cntrl,
   /* 1E rs  */ CTYPE_MASK_cntrl,
   /* 1F us  */ CTYPE_MASK_cntrl,
   /* 20     */ CTYPE_MASK_blank | CTYPE_MASK_print | CTYPE_MASK_space
                | CTYPE_MASK_asn1print,
   /* 21  !  */ CTYPE_MASK_graph | CTYPE_MASK_print | CTYPE_MASK_punct,
   /* 22  "  */ CTYPE_MASK_graph | CTYPE_MASK_print | CTYPE_MASK_punct,
   /* 23  #  */ CTYPE_MASK_graph | CTYPE_MASK_print | CTYPE_MASK_punct,
   /* 24  $  */ CTYPE_MASK_graph | CTYPE_MASK_print | CTYPE_MASK_punct,
   /* 25  %  */ CTYPE_MASK_graph | CTYPE_MASK_print | CTYPE_MASK_punct,
   /* 26  &  */ CTYPE_MASK_graph | CTYPE_MASK_print | CTYPE_MASK_punct,
   /* 27  '  */ CTYPE_MASK_graph | CTYPE_MASK_print | CTYPE_MASK_punct
                | CTYPE_MASK_asn1print,
   /* 28  (  */ CTYPE_MASK_graph | CTYPE_MASK_print | CTYPE_MASK_punct
                | CTYPE_MASK_asn1print,
   /* 29  )  */ CTYPE_MASK_graph | CTYPE_MASK_print | CTYPE_MASK_punct
                | CTYPE_MASK_asn1print,
   /* 2A  *  */ CTYPE_MASK_graph | CTYPE_MASK_print | CTYPE_MASK_punct,
   /* 2B  +  */ CTYPE_MASK_graph | CTYPE_MASK_print | CTYPE_MASK_punct
                | CTYPE_MASK_base64 | CTYPE_MASK_asn1print,
   /* 2C  ,  */ CTYPE_MASK_graph | CTYPE_MASK_print | CTYPE_MASK_punct
                | CTYPE_MASK_asn1print,
   /* 2D  -  */ CTYPE_MASK_graph | CTYPE_MASK_print | CTYPE_MASK_punct
                | CTYPE_MASK_asn1print,
   /* 2E  .  */ CTYPE_MASK_graph | CTYPE_MASK_print | CTYPE_MASK_punct
                | CTYPE_MASK_asn1print,
   /* 2F  /  */ CTYPE_MASK_graph | CTYPE_MASK_print | CTYPE_MASK_punct
                | CTYPE_MASK_base64 | CTYPE_MASK_asn1print,
   /* 30  0  */ CTYPE_MASK_digit | CTYPE_MASK_graph | CTYPE_MASK_print
                | CTYPE_MASK_xdigit | CTYPE_MASK_base64 | CTYPE_MASK_asn1print,
   /* 31  1  */ CTYPE_MASK_digit | CTYPE_MASK_graph | CTYPE_MASK_print
                | CTYPE_MASK_xdigit | CTYPE_MASK_base64 | CTYPE_MASK_asn1print,
   /* 32  2  */ CTYPE_MASK_digit | CTYPE_MASK_graph | CTYPE_MASK_print
                | CTYPE_MASK_xdigit | CTYPE_MASK_base64 | CTYPE_MASK_asn1print,
   /* 33  3  */ CTYPE_MASK_digit | CTYPE_MASK_graph | CTYPE_MASK_print
                | CTYPE_MASK_xdigit | CTYPE_MASK_base64 | CTYPE_MASK_asn1print,
   /* 34  4  */ CTYPE_MASK_digit | CTYPE_MASK_graph | CTYPE_MASK_print
                | CTYPE_MASK_xdigit | CTYPE_MASK_base64 | CTYPE_MASK_asn1print,
   /* 35  5  */ CTYPE_MASK_digit | CTYPE_MASK_graph | CTYPE_MASK_print
                | CTYPE_MASK_xdigit | CTYPE_MASK_base64 | CTYPE_MASK_asn1print,
   /* 36  6  */ CTYPE_MASK_digit | CTYPE_MASK_graph | CTYPE_MASK_print
                | CTYPE_MASK_xdigit | CTYPE_MASK_base64 | CTYPE_MASK_asn1print,
   /* 37  7  */ CTYPE_MASK_digit | CTYPE_MASK_graph | CTYPE_MASK_print
                | CTYPE_MASK_xdigit | CTYPE_MASK_base64 | CTYPE_MASK_asn1print,
   /* 38  8  */ CTYPE_MASK_digit | CTYPE_MASK_graph | CTYPE_MASK_print
                | CTYPE_MASK_xdigit | CTYPE_MASK_base64 | CTYPE_MASK_asn1print,
   /* 39  9  */ CTYPE_MASK_digit | CTYPE_MASK_graph | CTYPE_MASK_print
                | CTYPE_MASK_xdigit | CTYPE_MASK_base64 | CTYPE_MASK_asn1print,
   /* 3A  :  */ CTYPE_MASK_graph | CTYPE_MASK_print | CTYPE_MASK_punct
                | CTYPE_MASK_asn1print,
   /* 3B  ;  */ CTYPE_MASK_graph | CTYPE_MASK_print | CTYPE_MASK_punct,
   /* 3C  <  */ CTYPE_MASK_graph | CTYPE_MASK_print | CTYPE_MASK_punct,
   /* 3D  =  */ CTYPE_MASK_graph | CTYPE_MASK_print | CTYPE_MASK_punct
                | CTYPE_MASK_base64 | CTYPE_MASK_asn1print,
   /* 3E  >  */ CTYPE_MASK_graph | CTYPE_MASK_print | CTYPE_MASK_punct,
   /* 3F  ?  */ CTYPE_MASK_graph | CTYPE_MASK_print | CTYPE_MASK_punct
                | CTYPE_MASK_asn1print,
   /* 40  @  */ CTYPE_MASK_graph | CTYPE_MASK_print | CTYPE_MASK_punct,
   /* 41  A  */ CTYPE_MASK_graph | CTYPE_MASK_print | CTYPE_MASK_upper
                | CTYPE_MASK_xdigit | CTYPE_MASK_base64 | CTYPE_MASK_asn1print,
   /* 42  B  */ CTYPE_MASK_graph | CTYPE_MASK_print | CTYPE_MASK_upper
                | CTYPE_MASK_xdigit | CTYPE_MASK_base64 | CTYPE_MASK_asn1print,
   /* 43  C  */ CTYPE_MASK_graph | CTYPE_MASK_print | CTYPE_MASK_upper
                | CTYPE_MASK_xdigit | CTYPE_MASK_base64 | CTYPE_MASK_asn1print,
   /* 44  D  */ CTYPE_MASK_graph | CTYPE_MASK_print | CTYPE_MASK_upper
                | CTYPE_MASK_xdigit | CTYPE_MASK_base64 | CTYPE_MASK_asn1print,
   /* 45  E  */ CTYPE_MASK_graph | CTYPE_MASK_print | CTYPE_MASK_upper
                | CTYPE_MASK_xdigit | CTYPE_MASK_base64 | CTYPE_MASK_asn1print,
   /* 46  F  */ CTYPE_MASK_graph | CTYPE_MASK_print | CTYPE_MASK_upper
                | CTYPE_MASK_xdigit | CTYPE_MASK_base64 | CTYPE_MASK_asn1print,
   /* 47  G  */ CTYPE_MASK_graph | CTYPE_MASK_print | CTYPE_MASK_upper
                | CTYPE_MASK_base64 | CTYPE_MASK_asn1print,
   /* 48  H  */ CTYPE_MASK_graph | CTYPE_MASK_print | CTYPE_MASK_upper
                | CTYPE_MASK_base64 | CTYPE_MASK_asn1print,
   /* 49  I  */ CTYPE_MASK_graph | CTYPE_MASK_print | CTYPE_MASK_upper
                | CTYPE_MASK_base64 | CTYPE_MASK_asn1print,
   /* 4A  J  */ CTYPE_MASK_graph | CTYPE_MASK_print | CTYPE_MASK_upper
                | CTYPE_MASK_base64 | CTYPE_MASK_asn1print,
   /* 4B  K  */ CTYPE_MASK_graph | CTYPE_MASK_print | CTYPE_MASK_upper
                | CTYPE_MASK_base64 | CTYPE_MASK_asn1print,
   /* 4C  L  */ CTYPE_MASK_graph | CTYPE_MASK_print | CTYPE_MASK_upper
                | CTYPE_MASK_base64 | CTYPE_MASK_asn1print,
   /* 4D  M  */ CTYPE_MASK_graph | CTYPE_MASK_print | CTYPE_MASK_upper
                | CTYPE_MASK_base64 | CTYPE_MASK_asn1print,
   /* 4E  N  */ CTYPE_MASK_graph | CTYPE_MASK_print | CTYPE_MASK_upper
                | CTYPE_MASK_base64 | CTYPE_MASK_asn1print,
   /* 4F  O  */ CTYPE_MASK_graph | CTYPE_MASK_print | CTYPE_MASK_upper
                | CTYPE_MASK_base64 | CTYPE_MASK_asn1print,
   /* 50  P  */ CTYPE_MASK_graph | CTYPE_MASK_print | CTYPE_MASK_upper
                | CTYPE_MASK_base64 | CTYPE_MASK_asn1print,
   /* 51  Q  */ CTYPE_MASK_graph | CTYPE_MASK_print | CTYPE_MASK_upper
                | CTYPE_MASK_base64 | CTYPE_MASK_asn1print,
   /* 52  R  */ CTYPE_MASK_graph | CTYPE_MASK_print | CTYPE_MASK_upper
                | CTYPE_MASK_base64 | CTYPE_MASK_asn1print,
   /* 53  S  */ CTYPE_MASK_graph | CTYPE_MASK_print | CTYPE_MASK_upper
                | CTYPE_MASK_base64 | CTYPE_MASK_asn1print,
   /* 54  T  */ CTYPE_MASK_graph | CTYPE_MASK_print | CTYPE_MASK_upper
                | CTYPE_MASK_base64 | CTYPE_MASK_asn1print,
   /* 55  U  */ CTYPE_MASK_graph | CTYPE_MASK_print | CTYPE_MASK_upper
                | CTYPE_MASK_base64 | CTYPE_MASK_asn1print,
   /* 56  V  */ CTYPE_MASK_graph | CTYPE_MASK_print | CTYPE_MASK_upper
                | CTYPE_MASK_base64 | CTYPE_MASK_asn1print,
   /* 57  W  */ CTYPE_MASK_graph | CTYPE_MASK_print | CTYPE_MASK_upper
                | CTYPE_MASK_base64 | CTYPE_MASK_asn1print,
   /* 58  X  */ CTYPE_MASK_graph | CTYPE_MASK_print | CTYPE_MASK_upper
                | CTYPE_MASK_base64 | CTYPE_MASK_asn1print,
   /* 59  Y  */ CTYPE_MASK_graph | CTYPE_MASK_print | CTYPE_MASK_upper
                | CTYPE_MASK_base64 | CTYPE_MASK_asn1print,
   /* 5A  Z  */ CTYPE_MASK_graph | CTYPE_MASK_print | CTYPE_MASK_upper
                | CTYPE_MASK_base64 | CTYPE_MASK_asn1print,
   /* 5B  [  */ CTYPE_MASK_graph | CTYPE_MASK_print | CTYPE_MASK_punct,
   /* 5C  \  */ CTYPE_MASK_graph | CTYPE_MASK_print | CTYPE_MASK_punct,
   /* 5D  ]  */ CTYPE_MASK_graph | CTYPE_MASK_print | CTYPE_MASK_punct,
   /* 5E  ^  */ CTYPE_MASK_graph | CTYPE_MASK_print | CTYPE_MASK_punct,
   /* 5F  _  */ CTYPE_MASK_graph | CTYPE_MASK_print | CTYPE_MASK_punct,
   /* 60  `  */ CTYPE_MASK_graph | CTYPE_MASK_print | CTYPE_MASK_punct,
   /* 61  a  */ CTYPE_MASK_graph | CTYPE_MASK_lower | CTYPE_MASK_print
                | CTYPE_MASK_xdigit | CTYPE_MASK_base64 | CTYPE_MASK_asn1print,
   /* 62  b  */ CTYPE_MASK_graph | CTYPE_MASK_lower | CTYPE_MASK_print
                | CTYPE_MASK_xdigit | CTYPE_MASK_base64 | CTYPE_MASK_asn1print,
   /* 63  c  */ CTYPE_MASK_graph | CTYPE_MASK_lower | CTYPE_MASK_print
                | CTYPE_MASK_xdigit | CTYPE_MASK_base64 | CTYPE_MASK_asn1print,
   /* 64  d  */ CTYPE_MASK_graph | CTYPE_MASK_lower | CTYPE_MASK_print
                | CTYPE_MASK_xdigit | CTYPE_MASK_base64 | CTYPE_MASK_asn1print,
   /* 65  e  */ CTYPE_MASK_graph | CTYPE_MASK_lower | CTYPE_MASK_print
                | CTYPE_MASK_xdigit | CTYPE_MASK_base64 | CTYPE_MASK_asn1print,
   /* 66  f  */ CTYPE_MASK_graph | CTYPE_MASK_lower | CTYPE_MASK_print
                | CTYPE_MASK_xdigit | CTYPE_MASK_base64 | CTYPE_MASK_asn1print,
   /* 67  g  */ CTYPE_MASK_graph | CTYPE_MASK_lower | CTYPE_MASK_print
                | CTYPE_MASK_base64 | CTYPE_MASK_asn1print,
   /* 68  h  */ CTYPE_MASK_graph | CTYPE_MASK_lower | CTYPE_MASK_print
                | CTYPE_MASK_base64 | CTYPE_MASK_asn1print,
   /* 69  i  */ CTYPE_MASK_graph | CTYPE_MASK_lower | CTYPE_MASK_print
                | CTYPE_MASK_base64 | CTYPE_MASK_asn1print,
   /* 6A  j  */ CTYPE_MASK_graph | CTYPE_MASK_lower | CTYPE_MASK_print
                | CTYPE_MASK_base64 | CTYPE_MASK_asn1print,
   /* 6B  k  */ CTYPE_MASK_graph | CTYPE_MASK_lower | CTYPE_MASK_print
                | CTYPE_MASK_base64 | CTYPE_MASK_asn1print,
   /* 6C  l  */ CTYPE_MASK_graph | CTYPE_MASK_lower | CTYPE_MASK_print
                | CTYPE_MASK_base64 | CTYPE_MASK_asn1print,
   /* 6D  m  */ CTYPE_MASK_graph | CTYPE_MASK_lower | CTYPE_MASK_print
                | CTYPE_MASK_base64 | CTYPE_MASK_asn1print,
   /* 6E  n  */ CTYPE_MASK_graph | CTYPE_MASK_lower | CTYPE_MASK_print
                | CTYPE_MASK_base64 | CTYPE_MASK_asn1print,
   /* 6F  o  */ CTYPE_MASK_graph | CTYPE_MASK_lower | CTYPE_MASK_print
                | CTYPE_MASK_base64 | CTYPE_MASK_asn1print,
   /* 70  p  */ CTYPE_MASK_graph | CTYPE_MASK_lower | CTYPE_MASK_print
                | CTYPE_MASK_base64 | CTYPE_MASK_asn1print,
   /* 71  q  */ CTYPE_MASK_graph | CTYPE_MASK_lower | CTYPE_MASK_print
                | CTYPE_MASK_base64 | CTYPE_MASK_asn1print,
   /* 72  r  */ CTYPE_MASK_graph | CTYPE_MASK_lower | CTYPE_MASK_print
                | CTYPE_MASK_base64 | CTYPE_MASK_asn1print,
   /* 73  s  */ CTYPE_MASK_graph | CTYPE_MASK_lower | CTYPE_MASK_print
                | CTYPE_MASK_base64 | CTYPE_MASK_asn1print,
   /* 74  t  */ CTYPE_MASK_graph | CTYPE_MASK_lower | CTYPE_MASK_print
                | CTYPE_MASK_base64 | CTYPE_MASK_asn1print,
   /* 75  u  */ CTYPE_MASK_graph | CTYPE_MASK_lower | CTYPE_MASK_print
                | CTYPE_MASK_base64 | CTYPE_MASK_asn1print,
   /* 76  v  */ CTYPE_MASK_graph | CTYPE_MASK_lower | CTYPE_MASK_print
                | CTYPE_MASK_base64 | CTYPE_MASK_asn1print,
   /* 77  w  */ CTYPE_MASK_graph | CTYPE_MASK_lower | CTYPE_MASK_print
                | CTYPE_MASK_base64 | CTYPE_MASK_asn1print,
   /* 78  x  */ CTYPE_MASK_graph | CTYPE_MASK_lower | CTYPE_MASK_print
                | CTYPE_MASK_base64 | CTYPE_MASK_asn1print,
   /* 79  y  */ CTYPE_MASK_graph | CTYPE_MASK_lower | CTYPE_MASK_print
                | CTYPE_MASK_base64 | CTYPE_MASK_asn1print,
   /* 7A  z  */ CTYPE_MASK_graph | CTYPE_MASK_lower | CTYPE_MASK_print
                | CTYPE_MASK_base64 | CTYPE_MASK_asn1print,
   /* 7B  {  */ CTYPE_MASK_graph | CTYPE_MASK_print | CTYPE_MASK_punct,
   /* 7C  |  */ CTYPE_MASK_graph | CTYPE_MASK_print | CTYPE_MASK_punct,
   /* 7D  }  */ CTYPE_MASK_graph | CTYPE_MASK_print | CTYPE_MASK_punct,
   /* 7E  ~  */ CTYPE_MASK_graph | CTYPE_MASK_print | CTYPE_MASK_punct,
   /* 7F del */ CTYPE_MASK_cntrl
};

#ifdef CHARSET_EBCDIC
int ossl_toascii(int c)
{
    if (c < -128 || c > 256 || c == EOF)
        return c;
    /*
     * Adjust negatively signed characters.
     * This is not required for ASCII because any character that sign extends
     * is not seven bit and all of the checks are on the seven bit characters.
     * I.e. any check must fail on sign extension.
     */
    if (c < 0)
        c += 256;
    return os_toascii[c];
}

int ossl_fromascii(int c)
{
    if (c < -128 || c > 256 || c == EOF)
        return c;
    if (c < 0)
        c += 256;
    return os_toebcdic[c];
}
#endif

int ossl_ctype_check(int c, unsigned int mask)
{
    const int max = sizeof(ctype_char_map) / sizeof(*ctype_char_map);
    const int a = ossl_toascii(c);

    return a >= 0 && a < max && (ctype_char_map[a] & mask) != 0;
}

#if defined(CHARSET_EBCDIC) && !defined(CHARSET_EBCDIC_TEST)
static const int case_change = 0x40;
#else
static const int case_change = 0x20;
#endif

int ossl_tolower(int c)
{
    return ossl_isupper(c) ? c ^ case_change : c;
}

int ossl_toupper(int c)
{
    return ossl_islower(c) ? c ^ case_change : c;
}
