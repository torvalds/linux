/*-
 * Copyright (c) 1992, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 1992, 1993, 1994, 1995, 1996
 *	Keith Bostic.  All rights reserved.
 * Copyright (c) 2011, 2012
 *	Zhihao Yuan.  All rights reserved.
 *
 * See the LICENSE file for redistribution information.
 *
 *	$Id: conv.h,v 2.32 2013/03/11 01:20:53 zy Exp $
 */

#ifdef USE_ICONV
#include <iconv.h>
#ifdef ICONV_TRADITIONAL
typedef char **		iconv_src_t;
#else
typedef char const **	iconv_src_t;
#endif
#else
typedef int	iconv_t;
#endif

/*
 * XXX
 * We can not use MB_CUR_MAX here, since UTF-8 may report it as 6, but
 * a sequence longer than 4 is deprecated by RFC 3629.
 */
#define KEY_NEEDSWIDE(sp, ch)						\
	(INTISWIDE(ch) && KEY_LEN(sp, ch) <= 4)
#define KEY_COL(sp, ch)							\
	(KEY_NEEDSWIDE(sp, ch) ? CHAR_WIDTH(sp, ch) : KEY_LEN(sp, ch))

enum { IC_FE_CHAR2INT, IC_FE_INT2CHAR, IC_IE_CHAR2INT, IC_IE_TO_UTF16 };

struct _conv_win {
	union {
		char 	*c;
		CHAR_T	*wc;
	}	bp1;
	size_t	blen1;
};

typedef int (*char2wchar_t) 
    (SCR *, const char *, ssize_t, struct _conv_win *, size_t *, CHAR_T **);
typedef int (*wchar2char_t) 
    (SCR *, const CHAR_T *, ssize_t, struct _conv_win *, size_t *, char **);

struct _conv {
	char2wchar_t	sys2int;
	wchar2char_t	int2sys;
	char2wchar_t	file2int;
	wchar2char_t	int2file;
	char2wchar_t	input2int;
	iconv_t		id[IC_IE_TO_UTF16 + 1];
};
