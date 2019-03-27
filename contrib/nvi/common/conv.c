/*-
 * Copyright (c) 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 1993, 1994, 1995, 1996
 *	Keith Bostic.  All rights reserved.
 * Copyright (c) 2011, 2012
 *	Zhihao Yuan.  All rights reserved.
 *
 * See the LICENSE file for redistribution information.
 */

#include "config.h"

#ifndef lint
static const char sccsid[] = "$Id: conv.c,v 2.40 2014/02/27 16:25:29 zy Exp $";
#endif /* not lint */

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/time.h>

#include <bitstring.h>
#include <errno.h>
#include <limits.h>
#include <langinfo.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "common.h"

/*
 * codeset --
 *	Get the locale encoding.
 *
 * PUBLIC: char * codeset(void);
 */
char *
codeset(void)
{
	static char *cs;

	if (cs == NULL)
		cs = nl_langinfo(CODESET);

	return cs;
}

#ifdef USE_WIDECHAR
static int 
raw2int(SCR *sp, const char * str, ssize_t len, CONVWIN *cw, size_t *tolen,
    CHAR_T **dst)
{
	int i;
	CHAR_T **tostr = &cw->bp1.wc;
	size_t  *blen = &cw->blen1;

	BINC_RETW(NULL, *tostr, *blen, len);

	*tolen = len;
	for (i = 0; i < len; ++i)
		(*tostr)[i] = (u_char) str[i];

	*dst = cw->bp1.wc;

	return 0;
}

#define CONV_BUFFER_SIZE    512
/* fill the buffer with codeset encoding of string pointed to by str
 * left has the number of bytes left in str and is adjusted
 * len contains the number of bytes put in the buffer
 */
#ifdef USE_ICONV
#define CONVERT(str, left, src, len)					\
	do {								\
		size_t outleft;						\
		char *bp = buffer;					\
		outleft = CONV_BUFFER_SIZE;				\
		errno = 0;						\
		if (iconv(id, (iconv_src_t)&str, &left, &bp, &outleft)	\
		    == -1 && errno != E2BIG)				\
			goto err;					\
		if ((len = CONV_BUFFER_SIZE - outleft) == 0) {		\
			error = -left;					\
			goto err;					\
		}							\
		src = buffer;						\
	} while (0)

#define IC_RESET()							\
	do {								\
		if (id != (iconv_t)-1)					\
			iconv(id, NULL, NULL, NULL, NULL);		\
	} while(0)
#else
#define CONVERT(str, left, src, len)
#define IC_RESET()
#endif

static int 
default_char2int(SCR *sp, const char * str, ssize_t len, CONVWIN *cw, 
    size_t *tolen, CHAR_T **dst, iconv_t id)
{
	size_t i = 0, j;
	CHAR_T **tostr = &cw->bp1.wc;
	size_t *blen = &cw->blen1;
	mbstate_t mbs;
	size_t n;
	ssize_t nlen = len;
	char *src = (char *)str;
#ifdef USE_ICONV
	char buffer[CONV_BUFFER_SIZE];
#endif
	size_t left = len;
	int error = 1;

	BZERO(&mbs, 1);
	BINC_RETW(NULL, *tostr, *blen, nlen);

#ifdef USE_ICONV
	if (id != (iconv_t)-1)
		CONVERT(str, left, src, len);
#endif

	for (i = 0, j = 0; j < len; ) {
		n = mbrtowc((*tostr)+i, src+j, len-j, &mbs);
		/* NULL character converted */
		if (n == -2)
			error = -(len-j);
		if (n == -1 || n == -2)
			goto err;
		if (n == 0)
			n = 1;
		j += n;
		if (++i >= *blen) {
			nlen += 256;
			BINC_RETW(NULL, *tostr, *blen, nlen);
		}
		if (id != (iconv_t)-1 && j == len && left) {
			CONVERT(str, left, src, len);
			j = 0;
		}
	}

	error = 0;
err:
	*tolen = i;
	*dst = cw->bp1.wc;
	IC_RESET();

	return error;
}

static int 
fe_char2int(SCR *sp, const char * str, ssize_t len, CONVWIN *cw, size_t *tolen,
    CHAR_T **dst)
{
	return default_char2int(sp, str, len, cw, tolen, dst,
	    sp->conv.id[IC_FE_CHAR2INT]);
}

static int 
ie_char2int(SCR *sp, const char * str, ssize_t len, CONVWIN *cw, size_t *tolen,
    CHAR_T **dst)
{
	return default_char2int(sp, str, len, cw, tolen, dst,
	    sp->conv.id[IC_IE_CHAR2INT]);
}

static int 
cs_char2int(SCR *sp, const char * str, ssize_t len, CONVWIN *cw, size_t *tolen,
    CHAR_T **dst)
{
	return default_char2int(sp, str, len, cw, tolen, dst, (iconv_t)-1);
}

static int 
int2raw(SCR *sp, const CHAR_T * str, ssize_t len, CONVWIN *cw, size_t *tolen,
    char **dst)
{
	int i;
	char **tostr = &cw->bp1.c;
	size_t  *blen = &cw->blen1;

	BINC_RETC(NULL, *tostr, *blen, len);

	*tolen = len;
	for (i = 0; i < len; ++i)
		(*tostr)[i] = str[i];

	*dst = cw->bp1.c;

	return 0;
}

static int 
default_int2char(SCR *sp, const CHAR_T * str, ssize_t len, CONVWIN *cw, 
    size_t *tolen, char **pdst, iconv_t id)
{
	size_t i, j, offset = 0;
	char **tostr = &cw->bp1.c;
	size_t *blen = &cw->blen1;
	mbstate_t mbs;
	size_t n;
	ssize_t  nlen = len + MB_CUR_MAX;
	char *dst;
	size_t buflen;
#ifdef USE_ICONV
	char buffer[CONV_BUFFER_SIZE];
#endif
	int error = 1;

/* convert first len bytes of buffer and append it to cw->bp
 * len is adjusted => 0
 * offset contains the offset in cw->bp and is adjusted
 * cw->bp is grown as required
 */
#ifdef USE_ICONV
#define CONVERT2(_buffer, lenp, cw, offset)				\
	do {								\
		char *bp = _buffer;					\
		int ret;						\
		do {							\
			size_t outleft = cw->blen1 - offset;		\
			char *obp = cw->bp1.c + offset;			\
			if (cw->blen1 < offset + MB_CUR_MAX) {		\
				nlen += 256;				\
				BINC_RETC(NULL, cw->bp1.c, cw->blen1,	\
				    nlen);				\
			}						\
			errno = 0;					\
			ret = iconv(id, (iconv_src_t)&bp, lenp, &obp,	\
			    &outleft);					\
			if (ret == -1 && errno != E2BIG)		\
				goto err;				\
			offset = cw->blen1 - outleft;			\
		} while (ret != 0); 					\
	} while (0)
#else
#define CONVERT2(_buffer, lenp, cw, offset)
#endif


	BZERO(&mbs, 1);
	BINC_RETC(NULL, *tostr, *blen, nlen);
	dst = *tostr; buflen = *blen;

#ifdef USE_ICONV
	if (id != (iconv_t)-1) {
		dst = buffer; buflen = CONV_BUFFER_SIZE;
	}
#endif

	for (i = 0, j = 0; i < len; ++i) {
		n = wcrtomb(dst+j, str[i], &mbs);
		if (n == -1)
			goto err;
		j += n;
		if (buflen < j + MB_CUR_MAX) {
			if (id != (iconv_t)-1) {
				CONVERT2(buffer, &j, cw, offset);
			} else {
				nlen += 256;
				BINC_RETC(NULL, *tostr, *blen, nlen);
				dst = *tostr; buflen = *blen;
			}
		}
	}

	n = wcrtomb(dst+j, L'\0', &mbs);
	j += n - 1;				/* don't count NUL at the end */
	*tolen = j;

	if (id != (iconv_t)-1) {
		CONVERT2(buffer, &j, cw, offset);
		/* back to the initial state */
		CONVERT2(NULL, NULL, cw, offset);
		*tolen = offset;
	}

	error = 0;
err:
	if (error)
		*tolen = j;
	*pdst = cw->bp1.c;
	IC_RESET();

	return error;
}

static int 
fe_int2char(SCR *sp, const CHAR_T * str, ssize_t len, CONVWIN *cw, 
    size_t *tolen, char **dst)
{
	return default_int2char(sp, str, len, cw, tolen, dst,
		sp->conv.id[IC_FE_INT2CHAR]);
}

static int 
cs_int2char(SCR *sp, const CHAR_T * str, ssize_t len, CONVWIN *cw, 
    size_t *tolen, char **dst)
{
	return default_int2char(sp, str, len, cw, tolen, dst, (iconv_t)-1);
}

#endif

/*
 * conv_init --
 *	Initialize the iconv environment.
 *
 * PUBLIC: void conv_init(SCR *, SCR *);
 */
void
conv_init(SCR *orig, SCR *sp)
{
	int i;

	if (orig == NULL)
		setlocale(LC_ALL, "");
	if (orig != NULL)
		BCOPY(&orig->conv, &sp->conv, 1);
#ifdef USE_WIDECHAR
	else {
		char *ctype = setlocale(LC_CTYPE, NULL);

		/*
		 * XXX
		 * This hack fixes the libncursesw issue on FreeBSD.
		 */
		if (!strcmp(ctype, "ko_KR.CP949"))
			setlocale(LC_CTYPE, "ko_KR.eucKR");
		else if (!strcmp(ctype, "zh_CN.GB2312"))
			setlocale(LC_CTYPE, "zh_CN.eucCN");
		else if (!strcmp(ctype, "zh_CN.GBK"))
			setlocale(LC_CTYPE, "zh_CN.GB18030");

		/*
		 * Switch to 8bit mode if locale is C;
		 * LC_CTYPE should be reseted to C if unmatched.
		 */
		if (!strcmp(ctype, "C") || !strcmp(ctype, "POSIX")) {
			sp->conv.sys2int = sp->conv.file2int = raw2int;
			sp->conv.int2sys = sp->conv.int2file = int2raw;
			sp->conv.input2int = raw2int;
		} else {
			sp->conv.sys2int = cs_char2int;
			sp->conv.int2sys = cs_int2char;
			sp->conv.file2int = fe_char2int;
			sp->conv.int2file = fe_int2char;
			sp->conv.input2int = ie_char2int;
		}
#ifdef USE_ICONV
		o_set(sp, O_INPUTENCODING, OS_STRDUP, codeset(), 0);
#endif
	}
#endif

	/* iconv descriptors must be distinct to screens. */
	for (i = 0; i <= IC_IE_TO_UTF16; ++i)
		sp->conv.id[i] = (iconv_t)-1;
#ifdef USE_ICONV
	conv_enc(sp, O_INPUTENCODING, 0);
#endif
}

/*
 * conv_enc --
 *	Convert file/input encoding.
 *
 * PUBLIC: int conv_enc(SCR *, int, char *);
 */
int
conv_enc(SCR *sp, int option, char *enc)
{
#if defined(USE_WIDECHAR) && defined(USE_ICONV)
	iconv_t *c2w, *w2c;
	iconv_t id_c2w, id_w2c;

	switch (option) {
	case O_FILEENCODING:
		c2w = sp->conv.id + IC_FE_CHAR2INT;
		w2c = sp->conv.id + IC_FE_INT2CHAR;
		if (!enc)
			enc = O_STR(sp, O_FILEENCODING);

		if (strcasecmp(codeset(), enc)) {
			if ((id_c2w = iconv_open(codeset(), enc)) ==
			    (iconv_t)-1)
				goto err;
			if ((id_w2c = iconv_open(enc, codeset())) ==
			    (iconv_t)-1)
				goto err;
		} else {
			id_c2w = (iconv_t)-1;
			id_w2c = (iconv_t)-1;
		}

		break;

	case O_INPUTENCODING:
		c2w = sp->conv.id + IC_IE_CHAR2INT;
		w2c = sp->conv.id + IC_IE_TO_UTF16;
		if (!enc)
			enc = O_STR(sp, O_INPUTENCODING);

		if (strcasecmp(codeset(), enc)) {
			if ((id_c2w = iconv_open(codeset(), enc)) ==
			    (iconv_t)-1)
				goto err;
		} else
			id_c2w = (iconv_t)-1;

		/* UTF-16 can not be locale and can not be inputed. */
		if ((id_w2c = iconv_open("utf-16be", enc)) == (iconv_t)-1)
			goto err;

		break;

	default:
		abort();
	}

	if (*c2w != (iconv_t)-1)
		iconv_close(*c2w);
	if (*w2c != (iconv_t)-1)
		iconv_close(*w2c);

	*c2w = id_c2w;
	*w2c = id_w2c;

	F_CLR(sp, SC_CONV_ERROR);
	F_SET(sp, SC_SCR_REFORMAT);

	return 0;
err:
#endif
	switch (option) {
	case O_FILEENCODING:
		msgq(sp, M_ERR, "321|File encoding conversion not supported");
		break;
	case O_INPUTENCODING:
		msgq(sp, M_ERR, "322|Input encoding conversion not supported");
		break;
	}
	return 1;
}

/*
 * conv_end --
 *	Close the iconv descriptors, release the buffer.
 *
 * PUBLIC: void conv_end(SCR *);
 */
void
conv_end(SCR *sp)
{
#if defined(USE_WIDECHAR) && defined(USE_ICONV)
	int i;
	for (i = 0; i <= IC_IE_TO_UTF16; ++i)
		if (sp->conv.id[i] != (iconv_t)-1)
			iconv_close(sp->conv.id[i]);
	if (sp->cw.bp1.c != NULL)
		free(sp->cw.bp1.c);
#endif
}
