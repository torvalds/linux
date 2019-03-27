/*-
 * Copyright (c) 1992, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 1992, 1993, 1994, 1995, 1996
 *	Keith Bostic.  All rights reserved.
 *
 * See the LICENSE file for redistribution information.
 *
 *	$Id: multibyte.h,v 1.32 2012/10/07 01:35:58 zy Exp $
 */

#ifndef MULTIBYTE_H
#define MULTIBYTE_H

/*
 * Fundamental character types.
 *
 * CHAR_T	An integral type that can hold any character.
 * ARG_CHAR_T	The type of a CHAR_T when passed as an argument using
 *		traditional promotion rules.  It should also be able
 *		to be compared against any CHAR_T for equality without
 *		problems.
 * UCHAR_T	The shortest unified character type (8-bit clean).
 * RCHAR_T	The character type used by the internal regex engine.
 *
 * If no integral type can hold a character, don't even try the port.
 */
typedef	int		ARG_CHAR_T;

#ifdef USE_WIDECHAR
#include <wchar.h>
#include <wctype.h>

typedef	wchar_t		CHAR_T;
typedef	wint_t		UCHAR_T;
typedef wchar_t 	RCHAR_T;
#define REOF		WEOF

#define STRLEN		wcslen
#define STRTOL		wcstol
#define STRTOUL		wcstoul
#define SPRINTF		swprintf
#define STRCMP		wcscmp
#define STRPBRK		wcspbrk
#define ISBLANK		iswblank
#define ISCNTRL		iswcntrl
#define ISDIGIT		iswdigit
#define ISXDIGIT	iswxdigit
#define ISGRAPH		iswgraph
#define ISLOWER		iswlower
#define ISPRINT		iswprint
#define ISPUNCT		iswpunct
#define ISSPACE		iswspace
#define ISUPPER		iswupper
#define TOLOWER		towlower
#define TOUPPER		towupper
#define STRSET		wmemset
#define STRCHR		wcschr
#define STRRCHR		wcsrchr
#define GETC		getwc

#define L(ch)		L ## ch
#define WS		"%ls"
#define WVS		"%*ls"
#define WC		"%lc"

#else
typedef	u_char		CHAR_T;
typedef	u_char		UCHAR_T;
typedef	char		RCHAR_T;
#define REOF		EOF

#define STRLEN		strlen
#define STRTOL(a,b,c)	(strtol(a,(char**)b,c))
#define STRTOUL(a,b,c)	(strtoul(a,(char**)b,c))
#define SPRINTF		snprintf
#define STRCMP		strcmp
#define STRPBRK		strpbrk
#define ISBLANK		isblank
#define ISCNTRL		iscntrl
#define ISDIGIT		isdigit
#define ISXDIGIT	isxdigit
#define ISGRAPH		isgraph
#define ISLOWER		islower
#define ISPRINT		isprint
#define ISPUNCT		ispunct
#define ISSPACE		isspace
#define ISUPPER		isupper
#define TOLOWER		tolower
#define TOUPPER		toupper
#define STRSET		memset
#define STRCHR		strchr
#define STRRCHR		strrchr
#define GETC		getc

#define L(ch)		ch
#define WS		"%s"
#define WVS		"%*s"
#define WC		"%c"

#endif

#if defined(USE_WIDECHAR) && defined(DEBUG)
#define MEMCPY			wmemcpy
#define MEMMOVE			wmemmove
#define MEMCMP			wmemcmp
#else
#define MEMCPY(p, t, len)	memcpy(p, t, (len) * sizeof(CHAR_T))
#define MEMMOVE(p, t, len)	memmove(p, t, (len) * sizeof(CHAR_T))
#define MEMCMP(p, t, len)	memcmp(p, t, (len) * sizeof(CHAR_T))
#endif

#define SIZE(w)			(sizeof(w) / sizeof(*w))

#endif
