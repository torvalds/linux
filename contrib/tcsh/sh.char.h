/* $Header: /p/tcsh/cvsroot/tcsh/sh.char.h,v 3.37 2014/05/09 19:22:49 christos Exp $ */
/*
 * sh.char.h: Table for spotting special characters quickly
 * 	      Makes for very obscure but efficient coding.
 */
/*-
 * Copyright (c) 1980, 1991 The Regents of the University of California.
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
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#ifndef _h_sh_char
#define _h_sh_char
#if defined(NeXT) && defined(NLS)
# include <appkit/NXCType.h>
#else
# include <ctype.h>
# ifdef WIDE_STRINGS
#  ifdef HAVE_WCTYPE_H
#   include <wctype.h>
#  else
#   include <wchar.h>
#  endif
# endif
#endif

typedef unsigned char tcshuc;
#ifdef _MINIX
# undef _SP
#endif /* _MINIX */
extern unsigned short _cmap[];
#if defined(DSPMBYTE)
# define CHECK_MBYTEVAR	STRdspmbyte
#endif
extern unsigned short _cmap_c[];
extern unsigned short _cmap_mbyte[];
extern short _enable_mbdisp;
extern unsigned short _mbmap[];
extern unsigned short _mbmap_euc[];
extern unsigned short _mbmap_sjis[];
extern unsigned short _mbmap_big5[];
extern unsigned short _mbmap_utf8[];
/* VARIABLE Check str */
/* same compiler require #define even not define DSPMBYTE */
#undef	_MB1
#define _MB1	0x0001
#undef	_MB2
#define _MB2	0x0002

#ifndef NLS
extern tcshuc _cmap_lower[], _cmap_upper[];

#endif

#ifndef __QNXNTO__
#undef	_QF
#define	_QF	0x0001		/* '" (Forward quotes) */
#undef	_QB
#define	_QB	0x0002		/* ` (Backquote) */
#undef	_SP
#define	_SP	0x0004		/* space and tab */
#else
#undef	_XD
#define	_XD	0x0001		/* As in <ctype.h> */
#undef	_UP
#define	_UP	0x0002		/* As in <ctype.h> */
#undef	_SP
#define	_SP	0x0004		/* As in <ctype.h> */
#endif
#undef	_NL
#define	_NL	0x0008		/* \n */
#undef	_META
#define	_META	0x0010		/* lex meta characters, sp #'`";&<>()|\t\n */
#undef	_GLOB
#define	_GLOB	0x0020		/* glob characters, *?{[` */
#undef	_ESC
#define	_ESC	0x0040		/* \ */
#undef	_DOL
#define	_DOL	0x0080		/* $ */
#undef	_DIG
#define	_DIG  	0x0100		/* 0-9 */
#undef	_LET
#define	_LET  	0x0200		/* a-z, A-Z, _, or locale-specific */
#ifndef __QNXNTO__
#undef	_UP
#define	_UP   	0x0400		/* A-Z, or locale-specific */
#else
#undef	_QF
#define	_QF	0x0400		/* '" (Forward quotes) */
#endif
#undef	_DOW
#define	_DOW  	0x0800		/* a-z, or locale-specific */
#ifndef __QNXNTO__
#undef	_XD
#define	_XD 	0x1000		/* 0-9, a-f, A-F */
#else
#undef	_QB
#define	_QB	0x1000		/* 0-9, a-f, A-F */
#endif
#undef	_CMD
#define	_CMD	0x2000		/* lex end of command chars, ;&(|` */
#undef	_CTR
#define _CTR	0x4000		/* control */
#undef	_PUN
#define _PUN	0x8000		/* punctuation */

#ifdef IS_ASCII
# define ASC(ch) (ch)
# define CTL_ESC(ch) (ch)
#else
# ifdef _OSD_POSIX
/* "BS2000 OSD" is a POSIX on a main frame using a EBCDIC char set */
#   include <ascii_ebcdic.h>
# else
/* "OS/390 USS" is a POSIX on a main frame using an IBM1047 char set */
# endif
  extern unsigned short _toascii[256];
  extern unsigned short _toebcdic[256];

/* mainly for comparisons if (ASC(ch)=='\177')... */
#  define ASC(ch)     _toascii[(tcshuc)(ch)]

/* Literal escapes ('\010') must be mapped to EBCDIC,
 * for C-Escapes   ('\b'), the compiler already does it.
 */
#  define CTL_ESC(ch) _toebcdic[(tcshuc)(ch)]
#endif /*IS_ASCII*/

#ifdef WIDE_STRINGS
# define cmap(c, bits)	\
    (((c) & QUOTE) || (unsigned int)(c) >= 0x0080 ? 0 : \
	(_cmap[(tcshuc)ASC(c)] & (bits)))
#elif defined(SHORT_STRINGS) && defined(KANJI)
#  define cmap(c, bits)	\
    ((((c) & QUOTE) || ((tcshuc)(ASC(c) & 0x80) && adrof(STRnokanji))) ? \
	0 : (_cmap[(tcshuc)ASC(c)] & (bits)))
#else /* SHORT_STRINGS && KANJI */
# define cmap(c, bits)	\
    (((c) & QUOTE) ? 0 : (_cmap[(tcshuc)ASC(c)] & (bits)))
#endif /* SHORT_STRINGS && KANJI */

#define isglob(c)	cmap((c), _GLOB)
#define isspc(c)	cmap((c), _SP)
#define ismeta(c)	cmap((c), _META)
#define iscmdmeta(c)	cmap((c), _CMD)
#ifdef WIDE_STRINGS
#define letter(c)	(((c) & QUOTE) ? 0 :  \
			 (iswalpha((c)) || (c) == '_'))
#define alnum(c)	(((c) & QUOTE) ? 0 :  \
		         (iswalnum((c)) || (c) == '_'))
#else
#define letter(c)	(((Char)(c) & QUOTE) ? 0 :  \
			 ((isalpha((c)) && !(cmap((c), _PUN))) \
			  || (c) == '_'))
#define alnum(c)	(((Char)(c) & QUOTE) ? 0 :  \
		         ((isalnum((c)) && !(cmap((c), _PUN))) \
			  || (c) == '_'))

#endif

#if defined(DSPMBYTE)
# define IsmbyteU(c)	(Ismbyte1((Char)(c))||(Ismbyte2((Char)(c))&&((c)&0200)))
#endif

#ifdef NLS
# ifdef WIDE_STRINGS
#  define Isspace(c)	(((c) & QUOTE) ? 0 : iswspace(c))
#  define Isdigit(c)	(((c) & QUOTE) ? 0 : iswdigit(c))
#  define Isalpha(c)	(((c) & QUOTE) ? 0 : iswalpha(c))
#  define Islower(c)	(((c) & QUOTE) ? 0 : iswlower(c))
#  define Isupper(c)	(((c) & QUOTE) ? 0 : iswupper(c))
#  define Tolower(c) 	(((c) & QUOTE) ? 0 : (wchar_t)towlower(c))
#  define Toupper(c) 	(((c) & QUOTE) ? 0 : (wchar_t)towupper(c))
#  define Isxdigit(c)	(((c) & QUOTE) ? 0 : iswxdigit(c))
#  define Isalnum(c)	(((c) & QUOTE) ? 0 : iswalnum(c))
#  define Iscntrl(c) 	(((c) & QUOTE) ? 0 : iswcntrl(c))
#  define Isprint(c) 	(((c) & QUOTE) ? 0 : iswprint(c))
#  define Ispunct(c) 	(((c) & QUOTE) ? 0 : iswpunct(c))
# elif defined (NeXT)
#  define Isspace(c)	(((Char)(c) & QUOTE) ? 0 : NXIsSpace((unsigned) (c)))
#  define Isdigit(c)	(((Char)(c) & QUOTE) ? 0 : NXIsDigit((unsigned) (c)))
#  define Isalpha(c)	(((Char)(c) & QUOTE) ? 0 : NXIsAlpha((unsigned) (c)))
#  define Islower(c)	(((Char)(c) & QUOTE) ? 0 : NXIsLower((unsigned) (c)))
#  define Isupper(c)	(((Char)(c) & QUOTE) ? 0 : NXIsUpper((unsigned) (c)))
#  define Tolower(c) 	(((Char)(c) & QUOTE) ? 0 : NXToLower((unsigned) (c)))
#  define Toupper(c) 	(((Char)(c) & QUOTE) ? 0 : NXToUpper((unsigned) (c)))
#  define Isxdigit(c)	(((Char)(c) & QUOTE) ? 0 : NXIsXDigit((unsigned) (c)))
#if defined(DSPMBYTE)
#  define IscntrlM(c) 	(((Char)(c) & QUOTE) ? 0 : NXIsCntrl((unsigned) (c)))
#  define Iscntrl(c)	( (IscntrlM(c)) && !(_enable_mbdisp&&(IsmbyteU((c)))) )
#  define IsprintM(c) 	(((Char)(c) & QUOTE) ? 0 : NXIsPrint((unsigned) (c)))
#  define Isprint(c)	( (IsprintM(c)) || (_enable_mbdisp&&(IsmbyteU((c)))) )
#else
#  define Isalnum(c)	(((Char)(c) & QUOTE) ? 0 : NXIsAlNum((unsigned) (c)))
#  define Iscntrl(c) 	(((Char)(c) & QUOTE) ? 0 : NXIsCntrl((unsigned) (c)))
#  define Isprint(c) 	(((Char)(c) & QUOTE) ? 0 : NXIsPrint((unsigned) (c)))
#endif /* !defined(DSPMBYTE) */
#  define Ispunct(c) 	(((Char)(c) & QUOTE) ? 0 : NXIsPunct((unsigned) (c)))
# else /* !NeXT */
#  ifndef WINNT_NATIVE
#   define Isspace(c)	(((Char)(c) & QUOTE) ? 0 : isspace((tcshuc) (c)))
#   define Isdigit(c)	(((Char)(c) & QUOTE) ? 0 : isdigit((tcshuc) (c)))
#   define Isalpha(c)	(((Char)(c) & QUOTE) ? 0 : isalpha((tcshuc) (c)))
#   define Islower(c)	(((Char)(c) & QUOTE) ? 0 : islower((tcshuc) (c)))
#   define Isupper(c)	(((Char)(c) & QUOTE) ? 0 : isupper((tcshuc) (c)))
#   define Tolower(c) 	(((Char)(c) & QUOTE) ? 0 : tolower((tcshuc) (c)))
#   define Toupper(c) 	(((Char)(c) & QUOTE) ? 0 : toupper((tcshuc) (c)))
#   define Isxdigit(c)	(((Char)(c) & QUOTE) ? 0 : isxdigit((tcshuc) (c)))
#   define Isalnum(c)	(((Char)(c) & QUOTE) ? 0 : isalnum((tcshuc) (c)))
#if defined(DSPMBYTE)
#   define IscntrlM(c) 	(((Char)(c) & QUOTE) ? 0 : iscntrl((tcshuc) (c)))
#   define Iscntrl(c)	( (IscntrlM(c)) && !(_enable_mbdisp&&(IsmbyteU((c)))) )
#else
#   define Iscntrl(c) 	(((Char)(c) & QUOTE) ? 0 : iscntrl((tcshuc) (c)))
#endif /* !defined(DSPMBYTE) */
#   if SOLARIS2 == 24
    /* 
     * From <casper@fwi.uva.nl> Casper Dik:
     * In Solaris 2.4, isprint('\t') returns true after setlocal(LC_ALL,"").
     * This breaks commandline editing when you include tabs.
     * (This is in the en_US locale).
     */
#if defined(DSPMBYTE)
#    define IsprintM(c) 	(((Char)(c) & QUOTE) ? 0 : \
				(isprint((tcshuc) (c)) && (c) != '\t'))
#else
#    define Isprint(c) 	(((Char)(c) & QUOTE) ? 0 : \
				(isprint((tcshuc) (c)) && (c) != '\t'))
#endif /* !defined(DSPMBYTE) */
#   else
#if defined(DSPMBYTE)
#    define IsprintM(c) (((Char)(c) & QUOTE) ? 0 : isprint((tcshuc) (c)))
#else
#    define Isprint(c) 	(((Char)(c) & QUOTE) ? 0 : isprint((tcshuc) (c)))
#endif /* !defined(DSPMBYTE) */
#   endif /* SOLARIS2 == 24 */
#if defined(DSPMBYTE)
#   define Isprint(c)	( (IsprintM(c)) || (_enable_mbdisp&&(IsmbyteU((c)))) )
#endif /* !defined(DSPMBYTE) */
#    define Ispunct(c) 	(((Char)(c) & QUOTE) ? 0 : ispunct((tcshuc) (c)))
#  else /* WINNT_NATIVE */
#   define Isspace(c) (((Char)(c) & QUOTE) ? 0 : isspace( oem_it((tcshuc)(c))))
#   define Isdigit(c) (((Char)(c) & QUOTE) ? 0 : isdigit( oem_it((tcshuc)(c))))
#   define Isalpha(c) (((Char)(c) & QUOTE) ? 0 : isalpha( oem_it((tcshuc)(c))))
#   define Islower(c) (((Char)(c) & QUOTE) ? 0 : islower( oem_it((tcshuc)(c))))
#   define Isupper(c) (((Char)(c) & QUOTE) ? 0 : isupper( oem_it((tcshuc)(c))))
#   define Tolower(c) (((Char)(c) & QUOTE) ? 0 : tolower( oem_it((tcshuc)(c))))
#   define Toupper(c) (((Char)(c) & QUOTE) ? 0 : toupper( oem_it((tcshuc)(c))))
#   define Isxdigit(c)(((Char)(c) & QUOTE) ? 0 : isxdigit(oem_it((tcshuc)(c))))
#   define Isalnum(c) (((Char)(c) & QUOTE) ? 0 : isalnum( oem_it((tcshuc)(c))))
#   define Ispunct(c) (((Char)(c) & QUOTE) ? 0 : ispunct( oem_it((tcshuc)(c))))
#if defined(DSPMBYTE)
#   define IscntrlM(c) (((Char)(c) & QUOTE) ? 0 : iscntrl( oem_it((tcshuc)(c))))
#   define Iscntrl(c)	( (IscntrlM(c)) && !(_enable_mbdisp&&(IsmbyteU((c)))) )
#   define IsprintM(c) (((Char)(c) & QUOTE) ? 0 : isprint( oem_it((tcshuc)(c))))
#   define Isprint(c)	( (IsprintM(c)) || (_enable_mbdisp&&(IsmbyteU((c)))) )
#else
#   define Iscntrl(c) (((Char)(c) & QUOTE) ? 0 : iscntrl( oem_it((tcshuc)(c))))
#   define Isprint(c) (((Char)(c) & QUOTE) ? 0 : isprint( oem_it((tcshuc)(c))))
#endif /* !defined(DSPMBYTE) */
#  endif /* WINNT_NATIVE */
# endif /* !NeXT */
#else /* !NLS */
# define Isspace(c)	cmap((c), _SP|_NL)
# define Isdigit(c)	cmap((c), _DIG)
# define Isalpha(c)	(cmap((c),_LET) && !(((c) & META) && AsciiOnly))
# define Islower(c)	(cmap((c),_DOW) && !(((c) & META) && AsciiOnly))
# define Isupper(c)	(cmap((c), _UP) && !(((c) & META) && AsciiOnly))
# define Tolower(c)	(_cmap_lower[ASC(c)])
# define Toupper(c)	(_cmap_upper[ASC(c)])
# define Isxdigit(c)	cmap((c), _XD)
# define Isalnum(c)	(cmap((c), _DIG|_LET) && !(((Char)(c) & META) && AsciiOnly))
#if defined(DSPMBYTE)
# define IscntrlM(c)	(cmap((c),_CTR) && !(((c) & META) && AsciiOnly))
# define Iscntrl(c)	( (IscntrlM(c)) && !(_enable_mbdisp&&(IsmbyteU((c)))) )
# define IsprintM(c)	(!cmap((c),_CTR) && !(((c) & META) && AsciiOnly))
# define Isprint(c)	( (IsprintM(c)) || (_enable_mbdisp&&(IsmbyteU((c)))) )
#else
# define Iscntrl(c)	(cmap((c),_CTR) && !(((c) & META) && AsciiOnly))
# define Isprint(c)	(!cmap((c),_CTR) && !(((c) & META) && AsciiOnly))
#endif /* !defined(DSPMBYTE) */
# define Ispunct(c)	(cmap((c),_PUN) && !(((c) & META) && AsciiOnly))

#endif /* !NLS */

#if defined(DSPMBYTE)
# define Ismbyte1(c)	((_mbmap[(c) & 0377] & _MB1) ? 1 : 0)
# define Ismbyte2(c)	((_mbmap[(c) & 0377] & _MB2) ? 1 : 0)
#endif

#endif /* _h_sh_char */
