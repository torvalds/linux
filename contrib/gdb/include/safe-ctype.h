/* <ctype.h> replacement macros.

   Copyright (C) 2000, 2001 Free Software Foundation, Inc.
   Contributed by Zack Weinberg <zackw@stanford.edu>.

This file is part of the libiberty library.
Libiberty is free software; you can redistribute it and/or
modify it under the terms of the GNU Library General Public
License as published by the Free Software Foundation; either
version 2 of the License, or (at your option) any later version.

Libiberty is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Library General Public License for more details.

You should have received a copy of the GNU Library General Public
License along with libiberty; see the file COPYING.LIB.  If
not, write to the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
Boston, MA 02111-1307, USA.  */

/* This is a compatible replacement of the standard C library's <ctype.h>
   with the following properties:

   - Implements all isxxx() macros required by C99.
   - Also implements some character classes useful when
     parsing C-like languages.
   - Does not change behavior depending on the current locale.
   - Behaves properly for all values in the range of a signed or
     unsigned char.

   To avoid conflicts, this header defines the isxxx functions in upper
   case, e.g. ISALPHA not isalpha.  */

#ifndef SAFE_CTYPE_H
#define SAFE_CTYPE_H

#ifdef isalpha
 #error "safe-ctype.h and ctype.h may not be used simultaneously"
#endif

/* Determine host character set.  */
#define HOST_CHARSET_UNKNOWN 0
#define HOST_CHARSET_ASCII   1
#define HOST_CHARSET_EBCDIC  2

#if  '\n' == 0x0A && ' ' == 0x20 && '0' == 0x30 \
   && 'A' == 0x41 && 'a' == 0x61 && '!' == 0x21
#  define HOST_CHARSET HOST_CHARSET_ASCII
#else
# if '\n' == 0x15 && ' ' == 0x40 && '0' == 0xF0 \
   && 'A' == 0xC1 && 'a' == 0x81 && '!' == 0x5A
#  define HOST_CHARSET HOST_CHARSET_EBCDIC
# else
#  define HOST_CHARSET HOST_CHARSET_UNKNOWN
# endif
#endif

/* Categories.  */

enum {
  /* In C99 */
  _sch_isblank  = 0x0001,	/* space \t */
  _sch_iscntrl  = 0x0002,	/* nonprinting characters */
  _sch_isdigit  = 0x0004,	/* 0-9 */
  _sch_islower  = 0x0008,	/* a-z */
  _sch_isprint  = 0x0010,	/* any printing character including ' ' */
  _sch_ispunct  = 0x0020,	/* all punctuation */
  _sch_isspace  = 0x0040,	/* space \t \n \r \f \v */
  _sch_isupper  = 0x0080,	/* A-Z */
  _sch_isxdigit = 0x0100,	/* 0-9A-Fa-f */

  /* Extra categories useful to cpplib.  */
  _sch_isidst	= 0x0200,	/* A-Za-z_ */
  _sch_isvsp    = 0x0400,	/* \n \r */
  _sch_isnvsp   = 0x0800,	/* space \t \f \v \0 */

  /* Combinations of the above.  */
  _sch_isalpha  = _sch_isupper|_sch_islower,	/* A-Za-z */
  _sch_isalnum  = _sch_isalpha|_sch_isdigit,	/* A-Za-z0-9 */
  _sch_isidnum  = _sch_isidst|_sch_isdigit,	/* A-Za-z0-9_ */
  _sch_isgraph  = _sch_isalnum|_sch_ispunct,	/* isprint and not space */
  _sch_iscppsp  = _sch_isvsp|_sch_isnvsp,	/* isspace + \0 */
  _sch_isbasic  = _sch_isprint|_sch_iscppsp     /* basic charset of ISO C
						   (plus ` and @)  */
};

/* Character classification.  */
extern const unsigned short _sch_istable[256];

#define _sch_test(c, bit) (_sch_istable[(c) & 0xff] & (unsigned short)(bit))

#define ISALPHA(c)  _sch_test(c, _sch_isalpha)
#define ISALNUM(c)  _sch_test(c, _sch_isalnum)
#define ISBLANK(c)  _sch_test(c, _sch_isblank)
#define ISCNTRL(c)  _sch_test(c, _sch_iscntrl)
#define ISDIGIT(c)  _sch_test(c, _sch_isdigit)
#define ISGRAPH(c)  _sch_test(c, _sch_isgraph)
#define ISLOWER(c)  _sch_test(c, _sch_islower)
#define ISPRINT(c)  _sch_test(c, _sch_isprint)
#define ISPUNCT(c)  _sch_test(c, _sch_ispunct)
#define ISSPACE(c)  _sch_test(c, _sch_isspace)
#define ISUPPER(c)  _sch_test(c, _sch_isupper)
#define ISXDIGIT(c) _sch_test(c, _sch_isxdigit)

#define ISIDNUM(c)	_sch_test(c, _sch_isidnum)
#define ISIDST(c)	_sch_test(c, _sch_isidst)
#define IS_ISOBASIC(c)	_sch_test(c, _sch_isbasic)
#define IS_VSPACE(c)	_sch_test(c, _sch_isvsp)
#define IS_NVSPACE(c)	_sch_test(c, _sch_isnvsp)
#define IS_SPACE_OR_NUL(c)	_sch_test(c, _sch_iscppsp)

/* Character transformation.  */
extern const unsigned char  _sch_toupper[256];
extern const unsigned char  _sch_tolower[256];
#define TOUPPER(c) _sch_toupper[(c) & 0xff]
#define TOLOWER(c) _sch_tolower[(c) & 0xff]

#endif /* SAFE_CTYPE_H */
