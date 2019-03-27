/* $Header: /p/tcsh/cvsroot/tcsh/sh.char.c,v 3.21 2010/02/09 20:32:34 christos Exp $ */
/*
 * sh.char.c: Character classification tables
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
#include "sh.h"

RCSID("$tcsh: sh.char.c,v 3.21 2010/02/09 20:32:34 christos Exp $")

#include "sh.char.h"

/* on default same as original map */
unsigned short _cmap[256] = {
/*	  0 nul		  1 soh		  2 stx		  3 etx	*/
	_CTR,		_CTR,		_CTR,		_CTR,

/*	  4 eot		  5 enq		  6 ack		  7 bel	*/
	_CTR,		_CTR,		_CTR,		_CTR,

/*	  8 bs		  9 ht		 10 nl		 11 vt	*/
	_CTR,		_CTR|_SP|_META,	_CTR|_NL|_META,	_CTR,

/*	 12 np		 13 cr		 14 so		 15 si	*/
	_CTR,		_CTR,		_CTR,		_CTR,

/*	 16 dle		 17 dc1		 18 dc2		 19 dc3	*/
	_CTR,		_CTR,		_CTR,		_CTR,

/*	 20 dc4		 21 nak		 22 syn		 23 etb	*/
	_CTR,		_CTR,		_CTR,		_CTR,

/*	 24 can		 25 em		 26 sub		 27 esc	*/
	_CTR,		_CTR,		_CTR,		_CTR,

/*	 28 fs		 29 gs		 30 rs		 31 us	*/
	_CTR,		_CTR,		_CTR,		_CTR,

/*	 32 sp		 33 !		 34 "		 35 #	*/
	_SP|_META,	_PUN,		_QF|_PUN,	_META|_PUN,

/*	 36 $		 37 %		 38 &		 39 '	*/
	_DOL|_PUN,	_PUN,		_META|_CMD|_PUN,_QF|_PUN,

/*	 40 (		 41 )		 42 *		 43 +	*/
	_META|_CMD|_PUN,_META|_PUN,	_GLOB|_PUN,	_PUN,

/*	 44 ,		 45 -		 46 .		 47 /	*/
	_PUN,		_PUN,		_PUN,		_PUN,

/*	 48 0		 49 1		 50 2		 51 3	*/
	_DIG|_XD,	_DIG|_XD,	_DIG|_XD,	_DIG|_XD,

/*	 52 4		 53 5		 54 6		 55 7	*/
	_DIG|_XD,	_DIG|_XD,	_DIG|_XD,	_DIG|_XD,

/*	 56 8		 57 9		 58 :		 59 ;	*/
	_DIG|_XD,	_DIG|_XD,	_PUN,		_META|_CMD|_PUN,

/*	 60 <		 61 =		 62 >		 63 ?	*/
	_META|_PUN,	_PUN,		_META|_PUN,	_GLOB|_PUN,

/*	 64 @		 65 A		 66 B		 67 C	*/
	_PUN,		_LET|_UP|_XD,	_LET|_UP|_XD,	_LET|_UP|_XD,

/*	 68 D		 69 E		 70 F		 71 G	*/
	_LET|_UP|_XD,	_LET|_UP|_XD,	_LET|_UP|_XD,	_LET|_UP,

/*	 72 H		 73 I		 74 J		 75 K	*/
	_LET|_UP,	_LET|_UP,	_LET|_UP,	_LET|_UP,

/*	 76 L		 77 M		 78 N		 79 O	*/
	_LET|_UP,	_LET|_UP,	_LET|_UP,	_LET|_UP,

/*	 80 P		 81 Q		 82 R		 83 S	*/
	_LET|_UP,	_LET|_UP,	_LET|_UP,	_LET|_UP,

/*	 84 T		 85 U		 86 V		 87 W	*/
	_LET|_UP,	_LET|_UP,	_LET|_UP,	_LET|_UP,

/*	 88 X		 89 Y		 90 Z		 91 [	*/
	_LET|_UP,	_LET|_UP,	_LET|_UP,	_GLOB|_PUN,

/*	 92 \		 93 ]		 94 ^		 95 _	*/
	_ESC|_PUN,	_PUN,		_PUN,		_PUN,

/*	 96 `		 97 a		 98 b		 99 c	*/
  _QB|_GLOB|_META|_PUN,	_LET|_DOW|_XD,	_LET|_DOW|_XD,	_LET|_DOW|_XD,

/*	100 d		101 e		102 f		103 g	*/
	_LET|_DOW|_XD,	_LET|_DOW|_XD,	_LET|_DOW|_XD,	_LET|_DOW,

/*	104 h		105 i		106 j		107 k	*/
	_LET|_DOW,	_LET|_DOW,	_LET|_DOW,	_LET|_DOW,

/*	108 l		109 m		110 n		111 o	*/
	_LET|_DOW,	_LET|_DOW,	_LET|_DOW,	_LET|_DOW,

/*	112 p		113 q		114 r		115 s	*/
	_LET|_DOW,	_LET|_DOW,	_LET|_DOW,	_LET|_DOW,

/*	116 t		117 u		118 v		119 w	*/
	_LET|_DOW,	_LET|_DOW,	_LET|_DOW,	_LET|_DOW,

/*	120 x		121 y		122 z		123 {	*/
	_LET|_DOW,	_LET|_DOW,	_LET|_DOW,	_GLOB|_PUN,

/*	124 |		125 }		126 ~		127 del	*/
	_META|_CMD|_PUN,_PUN,		_PUN,		_CTR,

#ifdef SHORT_STRINGS
/****************************************************************/
/* 128 - 255 The below is supposedly ISO 8859/1			*/
/****************************************************************/
/*	128 (undef)	129 (undef)	130 (undef)	131 (undef)	*/
	_CTR,		_CTR,		_CTR,		_CTR,

/*	132 (undef)	133 (undef)	134 (undef)	135 (undef)	*/
	_CTR,		_CTR,		_CTR,		_CTR,

/*	136 (undef)	137 (undef)	138 (undef)	139 (undef)	*/
	_CTR,		_CTR,		_CTR,		_CTR,

/*	140 (undef)	141 (undef)	142 (undef)	143 (undef)	*/
	_CTR,		_CTR,		_CTR,		_CTR,

/*	144 (undef)	145 (undef)	146 (undef)	147 (undef)	*/
	_CTR,		_CTR,		_CTR,		_CTR,

/*	148 (undef)	149 (undef)	150 (undef)	151 (undef)	*/
	_CTR,		_CTR,		_CTR,		_CTR,

/*	152 (undef)	153 (undef)	154 (undef)	155 (undef)	*/
	_CTR,		_CTR,		_CTR,		_CTR,

/*	156 (undef)	157 (undef)	158 (undef)	159 (undef)	*/
	_CTR,		_CTR,		_CTR,		_CTR,

/*	160 nobreakspace 161 exclamdown	162 cent	163 sterling	*/
	_PUN, /* XXX */	_PUN,		_PUN,		_PUN,

/*	164 currency	165 yen		166 brokenbar	167 section	*/
	_PUN,		_PUN,		_PUN,		_PUN,

/*	168 diaeresis	169 copyright	170 ordfeminine	171 guillemotleft*/
	_PUN,		_PUN,		_PUN,		_PUN,

/*	172 notsign	173 hyphen	174 registered	175 macron	*/
	_PUN,		_PUN,		_PUN,		_PUN,

/*	176 degree	177 plusminus	178 twosuperior	179 threesuperior*/
	_PUN,		_PUN,		_PUN,		_PUN,

/*	180 acute	181 mu 		182 paragraph	183 periodcentered*/
	_PUN,		_PUN, /*XXX*/	_PUN,		_PUN,

/*	184 cedilla	185 onesuperior	186 masculine	187 guillemotright*/
	_PUN,		_PUN,		_PUN,		_PUN,

/*	188 onequarter	189 onehalf	190 threequarters 191 questiondown*/
	_PUN,		_PUN,		_PUN,		_PUN,

/*	192 Agrave	193 Aacute	194 Acircumflex	195 Atilde	*/
	_LET|_UP,	_LET|_UP,	_LET|_UP,	_LET|_UP,

/*	196 Adiaeresis	197 Aring	198 AE		199 Ccedilla	*/
	_LET|_UP,	_LET|_UP,	_LET|_UP,	_LET|_UP,

/*	200 Egrave	201 Eacute	202 Ecircumflex	203 Ediaeresis	*/
	_LET|_UP,	_LET|_UP,	_LET|_UP,	_LET|_UP,

/*	204 Igrave	205 Iacute	206 Icircumflex	207 Idiaeresis	*/
	_LET|_UP,	_LET|_UP,	_LET|_UP,	_LET|_UP,

/*	208 ETH		209 Ntilde	210 Ograve	211 Oacute	*/
	_LET|_UP,	_LET|_UP,	_LET|_UP,	_LET|_UP,

/*	212 Ocircumflex	213 Otilde	214 Odiaeresis	215 multiply	*/
	_LET|_UP,	_LET|_UP,	_LET|_UP,	_PUN,

/*	216 Ooblique	217 Ugrave	218 Uacute	219 Ucircumflex	*/
	_LET|_UP,	_LET|_UP,	_LET|_UP,	_LET|_UP,

/*	220 Udiaeresis	221 Yacute	222 THORN	223 ssharp	*/
	_LET|_UP,	_LET|_UP,	_LET|_UP,	_LET|_DOW,

/*	224 agrave	225 aacute	226 acircumflex	227 atilde	*/
	_LET|_DOW,	_LET|_DOW,	_LET|_DOW,	_LET|_DOW,

/*	228 adiaeresis	229 aring	230 ae		231 ccedilla	*/
	_LET|_DOW,	_LET|_DOW,	_LET|_DOW,	_LET|_DOW,

/*	232 egrave	233 eacute	234 ecircumflex	235 ediaeresis	*/
	_LET|_DOW,	_LET|_DOW,	_LET|_DOW,	_LET|_DOW,

/*	236 igrave	237 iacute	238 icircumflex	239 idiaeresis	*/
	_LET|_DOW,	_LET|_DOW,	_LET|_DOW,	_LET|_DOW,

/*	240 eth		241 ntilde	242 ograve	243 oacute	*/
	_LET|_DOW,	_LET|_DOW,	_LET|_DOW,	_LET|_DOW,

/*	244 ocircumflex	245 otilde	246 odiaeresis	247 division	*/
	_LET|_DOW,	_LET|_DOW,	_LET|_DOW,	_PUN,

/*	248 oslash	249 ugrave	250 uacute	251 ucircumflex	*/
	_LET|_DOW,	_LET|_DOW,	_LET|_DOW,	_LET|_DOW,

/*	252 udiaeresis	253 yacute	254 thorn	255 ydiaeresis	*/
	_LET|_DOW,	_LET|_DOW,	_LET|_DOW,	_LET|_DOW,
#endif /* SHORT_STRINGS */
};

#if defined(KANJI) && defined(SHORT_STRINGS) && defined(DSPMBYTE)
/* original table */
unsigned short _cmap_c[256] = {
/*	  0 nul		  1 soh		  2 stx		  3 etx	*/
	_CTR,		_CTR,		_CTR,		_CTR,

/*	  4 eot		  5 enq		  6 ack		  7 bel	*/
	_CTR,		_CTR,		_CTR,		_CTR,

/*	  8 bs		  9 ht		 10 nl		 11 vt	*/
	_CTR,		_CTR|_SP|_META,	_CTR|_NL|_META,	_CTR,

/*	 12 np		 13 cr		 14 so		 15 si	*/
	_CTR,		_CTR,		_CTR,		_CTR,

/*	 16 dle		 17 dc1		 18 dc2		 19 dc3	*/
	_CTR,		_CTR,		_CTR,		_CTR,

/*	 20 dc4		 21 nak		 22 syn		 23 etb	*/
	_CTR,		_CTR,		_CTR,		_CTR,

/*	 24 can		 25 em		 26 sub		 27 esc	*/
	_CTR,		_CTR,		_CTR,		_CTR,

/*	 28 fs		 29 gs		 30 rs		 31 us	*/
	_CTR,		_CTR,		_CTR,		_CTR,

/*	 32 sp		 33 !		 34 "		 35 #	*/
	_SP|_META,	_PUN,		_QF|_PUN,	_META|_PUN,

/*	 36 $		 37 %		 38 &		 39 '	*/
	_DOL|_PUN,	_PUN,		_META|_CMD|_PUN,_QF|_PUN,

/*	 40 (		 41 )		 42 *		 43 +	*/
	_META|_CMD|_PUN,_META|_PUN,	_GLOB|_PUN,	_PUN,

/*	 44 ,		 45 -		 46 .		 47 /	*/
	_PUN,		_PUN,		_PUN,		_PUN,

/*	 48 0		 49 1		 50 2		 51 3	*/
	_DIG|_XD,	_DIG|_XD,	_DIG|_XD,	_DIG|_XD,

/*	 52 4		 53 5		 54 6		 55 7	*/
	_DIG|_XD,	_DIG|_XD,	_DIG|_XD,	_DIG|_XD,

/*	 56 8		 57 9		 58 :		 59 ;	*/
	_DIG|_XD,	_DIG|_XD,	_PUN,		_META|_CMD|_PUN,

/*	 60 <		 61 =		 62 >		 63 ?	*/
	_META|_PUN,	_PUN,		_META|_PUN,	_GLOB|_PUN,

/*	 64 @		 65 A		 66 B		 67 C	*/
	_PUN,		_LET|_UP|_XD,	_LET|_UP|_XD,	_LET|_UP|_XD,

/*	 68 D		 69 E		 70 F		 71 G	*/
	_LET|_UP|_XD,	_LET|_UP|_XD,	_LET|_UP|_XD,	_LET|_UP,

/*	 72 H		 73 I		 74 J		 75 K	*/
	_LET|_UP,	_LET|_UP,	_LET|_UP,	_LET|_UP,

/*	 76 L		 77 M		 78 N		 79 O	*/
	_LET|_UP,	_LET|_UP,	_LET|_UP,	_LET|_UP,

/*	 80 P		 81 Q		 82 R		 83 S	*/
	_LET|_UP,	_LET|_UP,	_LET|_UP,	_LET|_UP,

/*	 84 T		 85 U		 86 V		 87 W	*/
	_LET|_UP,	_LET|_UP,	_LET|_UP,	_LET|_UP,

/*	 88 X		 89 Y		 90 Z		 91 [	*/
	_LET|_UP,	_LET|_UP,	_LET|_UP,	_GLOB|_PUN,

/*	 92 \		 93 ]		 94 ^		 95 _	*/
	_ESC|_PUN,	_PUN,		_PUN,		_PUN,

/*	 96 `		 97 a		 98 b		 99 c	*/
  _QB|_GLOB|_META|_PUN,	_LET|_DOW|_XD,	_LET|_DOW|_XD,	_LET|_DOW|_XD,

/*	100 d		101 e		102 f		103 g	*/
	_LET|_DOW|_XD,	_LET|_DOW|_XD,	_LET|_DOW|_XD,	_LET|_DOW,

/*	104 h		105 i		106 j		107 k	*/
	_LET|_DOW,	_LET|_DOW,	_LET|_DOW,	_LET|_DOW,

/*	108 l		109 m		110 n		111 o	*/
	_LET|_DOW,	_LET|_DOW,	_LET|_DOW,	_LET|_DOW,

/*	112 p		113 q		114 r		115 s	*/
	_LET|_DOW,	_LET|_DOW,	_LET|_DOW,	_LET|_DOW,

/*	116 t		117 u		118 v		119 w	*/
	_LET|_DOW,	_LET|_DOW,	_LET|_DOW,	_LET|_DOW,

/*	120 x		121 y		122 z		123 {	*/
	_LET|_DOW,	_LET|_DOW,	_LET|_DOW,	_GLOB|_PUN,

/*	124 |		125 }		126 ~		127 del	*/
	_META|_CMD|_PUN,_PUN,		_PUN,		_CTR,

/****************************************************************/
/* 128 - 255 The below is supposedly ISO 8859/1			*/
/****************************************************************/
/*	128 (undef)	129 (undef)	130 (undef)	131 (undef)	*/
	_CTR,		_CTR,		_CTR,		_CTR,

/*	132 (undef)	133 (undef)	134 (undef)	135 (undef)	*/
	_CTR,		_CTR,		_CTR,		_CTR,

/*	136 (undef)	137 (undef)	138 (undef)	139 (undef)	*/
	_CTR,		_CTR,		_CTR,		_CTR,

/*	140 (undef)	141 (undef)	142 (undef)	143 (undef)	*/
	_CTR,		_CTR,		_CTR,		_CTR,

/*	144 (undef)	145 (undef)	146 (undef)	147 (undef)	*/
	_CTR,		_CTR,		_CTR,		_CTR,

/*	148 (undef)	149 (undef)	150 (undef)	151 (undef)	*/
	_CTR,		_CTR,		_CTR,		_CTR,

/*	152 (undef)	153 (undef)	154 (undef)	155 (undef)	*/
	_CTR,		_CTR,		_CTR,		_CTR,

/*	156 (undef)	157 (undef)	158 (undef)	159 (undef)	*/
	_CTR,		_CTR,		_CTR,		_CTR,

/*	160 nobreakspace 161 exclamdown	162 cent	163 sterling	*/
	_PUN, /* XXX */	_PUN,		_PUN,		_PUN,

/*	164 currency	165 yen		166 brokenbar	167 section	*/
	_PUN,		_PUN,		_PUN,		_PUN,

/*	168 diaeresis	169 copyright	170 ordfeminine	171 guillemotleft*/
	_PUN,		_PUN,		_PUN,		_PUN,

/*	172 notsign	173 hyphen	174 registered	175 macron	*/
	_PUN,		_PUN,		_PUN,		_PUN,

/*	176 degree	177 plusminus	178 twosuperior	179 threesuperior*/
	_PUN,		_PUN,		_PUN,		_PUN,

/*	180 acute	181 mu 		182 paragraph	183 periodcentered*/
	_PUN,		_PUN, /*XXX*/	_PUN,		_PUN,

/*	184 cedilla	185 onesuperior	186 masculine	187 guillemotright*/
	_PUN,		_PUN,		_PUN,		_PUN,

/*	188 onequarter	189 onehalf	190 threequarters 191 questiondown*/
	_PUN,		_PUN,		_PUN,		_PUN,

/*	192 Agrave	193 Aacute	194 Acircumflex	195 Atilde	*/
	_LET|_UP,	_LET|_UP,	_LET|_UP,	_LET|_UP,

/*	196 Adiaeresis	197 Aring	198 AE		199 Ccedilla	*/
	_LET|_UP,	_LET|_UP,	_LET|_UP,	_LET|_UP,

/*	200 Egrave	201 Eacute	202 Ecircumflex	203 Ediaeresis	*/
	_LET|_UP,	_LET|_UP,	_LET|_UP,	_LET|_UP,

/*	204 Igrave	205 Iacute	206 Icircumflex	207 Idiaeresis	*/
	_LET|_UP,	_LET|_UP,	_LET|_UP,	_LET|_UP,

/*	208 ETH		209 Ntilde	210 Ograve	211 Oacute	*/
	_LET|_UP,	_LET|_UP,	_LET|_UP,	_LET|_UP,

/*	212 Ocircumflex	213 Otilde	214 Odiaeresis	215 multiply	*/
	_LET|_UP,	_LET|_UP,	_LET|_UP,	_PUN,

/*	216 Ooblique	217 Ugrave	218 Uacute	219 Ucircumflex	*/
	_LET|_UP,	_LET|_UP,	_LET|_UP,	_LET|_UP,

/*	220 Udiaeresis	221 Yacute	222 THORN	223 ssharp	*/
	_LET|_UP,	_LET|_UP,	_LET|_UP,	_LET|_DOW,

/*	224 agrave	225 aacute	226 acircumflex	227 atilde	*/
	_LET|_DOW,	_LET|_DOW,	_LET|_DOW,	_LET|_DOW,

/*	228 adiaeresis	229 aring	230 ae		231 ccedilla	*/
	_LET|_DOW,	_LET|_DOW,	_LET|_DOW,	_LET|_DOW,

/*	232 egrave	233 eacute	234 ecircumflex	235 ediaeresis	*/
	_LET|_DOW,	_LET|_DOW,	_LET|_DOW,	_LET|_DOW,

/*	236 igrave	237 iacute	238 icircumflex	239 idiaeresis	*/
	_LET|_DOW,	_LET|_DOW,	_LET|_DOW,	_LET|_DOW,

/*	240 eth		241 ntilde	242 ograve	243 oacute	*/
	_LET|_DOW,	_LET|_DOW,	_LET|_DOW,	_LET|_DOW,

/*	244 ocircumflex	245 otilde	246 odiaeresis	247 division	*/
	_LET|_DOW,	_LET|_DOW,	_LET|_DOW,	_PUN,

/*	248 oslash	249 ugrave	250 uacute	251 ucircumflex	*/
	_LET|_DOW,	_LET|_DOW,	_LET|_DOW,	_LET|_DOW,

/*	252 udiaeresis	253 yacute	254 thorn	255 ydiaeresis	*/
	_LET|_DOW,	_LET|_DOW,	_LET|_DOW,	_LET|_DOW,
};

/* multi-byte table */
/* BY Masaaki Koyanagi VERY THANKS */
unsigned short _cmap_mbyte[256] = {
/*	  0 nul		  1 soh		  2 stx		  3 etx	*/
	_CTR,		_CTR,		_CTR,		_CTR,

/*	  4 eot		  5 enq		  6 ack		  7 bel	*/
	_CTR,		_CTR,		_CTR,		_CTR,

/*	  8 bs		  9 ht		 10 nl		 11 vt	*/
	_CTR,		_CTR|_SP|_META,	_CTR|_NL|_META,	_CTR,

/*	 12 np		 13 cr		 14 so		 15 si	*/
	_CTR,		_CTR,		_CTR,		_CTR,

/*	 16 dle		 17 dc1		 18 dc2		 19 dc3	*/
	_CTR,		_CTR,		_CTR,		_CTR,

/*	 20 dc4		 21 nak		 22 syn		 23 etb	*/
	_CTR,		_CTR,		_CTR,		_CTR,

/*	 24 can		 25 em		 26 sub		 27 esc	*/
	_CTR,		_CTR,		_CTR,		_CTR,

/*	 28 fs		 29 gs		 30 rs		 31 us	*/
	_CTR,		_CTR,		_CTR,		_CTR,

/*	 32 sp		 33 !		 34 "		 35 #	*/
	_SP|_META,	_PUN,		_QF|_PUN,	_META|_PUN,

/*	 36 $		 37 %		 38 &		 39 '	*/
	_DOL|_PUN,	_PUN,		_META|_CMD|_PUN,_QF|_PUN,

/*	 40 (		 41 )		 42 *		 43 +	*/
	_META|_CMD|_PUN,_META|_PUN,	_GLOB|_PUN,	_PUN,

/*	 44 ,		 45 -		 46 .		 47 /	*/
	_PUN,		_PUN,		_PUN,		_PUN,

/*	 48 0		 49 1		 50 2		 51 3	*/
	_DIG|_XD,	_DIG|_XD,	_DIG|_XD,	_DIG|_XD,

/*	 52 4		 53 5		 54 6		 55 7	*/
	_DIG|_XD,	_DIG|_XD,	_DIG|_XD,	_DIG|_XD,

/*	 56 8		 57 9		 58 :		 59 ;	*/
	_DIG|_XD,	_DIG|_XD,	_PUN,		_META|_CMD|_PUN,

/*	 60 <		 61 =		 62 >		 63 ?	*/
	_META|_PUN,	_PUN,		_META|_PUN,	_GLOB|_PUN,

/*	 64 @		 65 A		 66 B		 67 C	*/
	_PUN,		_LET|_UP|_XD,	_LET|_UP|_XD,	_LET|_UP|_XD,

/*	 68 D		 69 E		 70 F		 71 G	*/
	_LET|_UP|_XD,	_LET|_UP|_XD,	_LET|_UP|_XD,	_LET|_UP,

/*	 72 H		 73 I		 74 J		 75 K	*/
	_LET|_UP,	_LET|_UP,	_LET|_UP,	_LET|_UP,

/*	 76 L		 77 M		 78 N		 79 O	*/
	_LET|_UP,	_LET|_UP,	_LET|_UP,	_LET|_UP,

/*	 80 P		 81 Q		 82 R		 83 S	*/
	_LET|_UP,	_LET|_UP,	_LET|_UP,	_LET|_UP,

/*	 84 T		 85 U		 86 V		 87 W	*/
	_LET|_UP,	_LET|_UP,	_LET|_UP,	_LET|_UP,

/*	 88 X		 89 Y		 90 Z		 91 [	*/
	_LET|_UP,	_LET|_UP,	_LET|_UP,	_GLOB|_PUN,

/*	 92 \		 93 ]		 94 ^		 95 _	*/
	_ESC|_PUN,	_PUN,		_PUN,		_PUN,

/*	 96 `		 97 a		 98 b		 99 c	*/
  _QB|_GLOB|_META|_PUN,	_LET|_DOW|_XD,	_LET|_DOW|_XD,	_LET|_DOW|_XD,

/*	100 d		101 e		102 f		103 g	*/
	_LET|_DOW|_XD,	_LET|_DOW|_XD,	_LET|_DOW|_XD,	_LET|_DOW,

/*	104 h		105 i		106 j		107 k	*/
	_LET|_DOW,	_LET|_DOW,	_LET|_DOW,	_LET|_DOW,

/*	108 l		109 m		110 n		111 o	*/
	_LET|_DOW,	_LET|_DOW,	_LET|_DOW,	_LET|_DOW,

/*	112 p		113 q		114 r		115 s	*/
	_LET|_DOW,	_LET|_DOW,	_LET|_DOW,	_LET|_DOW,

/*	116 t		117 u		118 v		119 w	*/
	_LET|_DOW,	_LET|_DOW,	_LET|_DOW,	_LET|_DOW,

/*	120 x		121 y		122 z		123 {	*/
	_LET|_DOW,	_LET|_DOW,	_LET|_DOW,	_GLOB|_PUN,

/*	124 |		125 }		126 ~		127 del	*/
	_META|_CMD|_PUN,_PUN,		_PUN,		_CTR,

/****************************************************************/
/* 128 - 255 The below is supposedly ISO 8859/1			*/
/****************************************************************/
/*	128 (undef)	129 (undef)	130 (undef)	131 (undef)	*/
	_PUN,		_PUN,		_PUN,		_PUN,

/*	132 (undef)	133 (undef)	134 (undef)	135 (undef)	*/
	_PUN,		_PUN,		_PUN,		_PUN,

/*	136 (undef)	137 (undef)	138 (undef)	139 (undef)	*/
	_PUN,		_PUN,		_PUN,		_PUN,

/*	140 (undef)	141 (undef)	142 (undef)	143 (undef)	*/
	_PUN,		_PUN,		_PUN,		_PUN,

/*	144 (undef)	145 (undef)	146 (undef)	147 (undef)	*/
	_PUN,		_PUN,		_PUN,		_PUN,

/*	148 (undef)	149 (undef)	150 (undef)	151 (undef)	*/
	_PUN,		_PUN,		_PUN,		_PUN,

/*	152 (undef)	153 (undef)	154 (undef)	155 (undef)	*/
	_PUN,		_PUN,		_PUN,		_PUN,

/*	156 (undef)	157 (undef)	158 (undef)	159 (undef)	*/
	_PUN,		_PUN,		_PUN,		_PUN,

/*	160 nobreakspace 161 exclamdown	162 cent	163 sterling	*/
	_PUN, /* XXX */	_PUN,		_PUN,		_PUN,

/*	164 currency	165 yen		166 brokenbar	167 section	*/
	_PUN,		_PUN,		_PUN,		_PUN,

/*	168 diaeresis	169 copyright	170 ordfeminine	171 guillemotleft*/
	_PUN,		_PUN,		_PUN,		_PUN,

/*	172 notsign	173 hyphen	174 registered	175 macron	*/
	_PUN,		_PUN,		_PUN,		_PUN,

/*	176 degree	177 plusminus	178 twosuperior	179 threesuperior*/
	_PUN,		_PUN,		_PUN,		_PUN,

/*	180 acute	181 mu 		182 paragraph	183 periodcentered*/
	_PUN,		_PUN, /*XXX*/	_PUN,		_PUN,

/*	184 cedilla	185 onesuperior	186 masculine	187 guillemotright*/
	_PUN,		_PUN,		_PUN,		_PUN,

/*	188 onequarter	189 onehalf	190 threequarters 191 questiondown*/
	_PUN,		_PUN,		_PUN,		_PUN,

/*	192 Agrave	193 Aacute	194 Acircumflex	195 Atilde	*/
	_LET|_UP,	_LET|_UP,	_LET|_UP,	_LET|_UP,

/*	196 Adiaeresis	197 Aring	198 AE		199 Ccedilla	*/
	_LET|_UP,	_LET|_UP,	_LET|_UP,	_LET|_UP,

/*	200 Egrave	201 Eacute	202 Ecircumflex	203 Ediaeresis	*/
	_LET|_UP,	_LET|_UP,	_LET|_UP,	_LET|_UP,

/*	204 Igrave	205 Iacute	206 Icircumflex	207 Idiaeresis	*/
	_LET|_UP,	_LET|_UP,	_LET|_UP,	_LET|_UP,

/*	208 ETH		209 Ntilde	210 Ograve	211 Oacute	*/
	_LET|_UP,	_LET|_UP,	_LET|_UP,	_LET|_UP,

/*	212 Ocircumflex	213 Otilde	214 Odiaeresis	215 multiply	*/
	_LET|_UP,	_LET|_UP,	_LET|_UP,	_PUN,

/*	216 Ooblique	217 Ugrave	218 Uacute	219 Ucircumflex	*/
	_LET|_UP,	_LET|_UP,	_LET|_UP,	_LET|_UP,

/*	220 Udiaeresis	221 Yacute	222 THORN	223 ssharp	*/
	_LET|_UP,	_LET|_UP,	_LET|_UP,	_LET|_DOW,

/*	224 agrave	225 aacute	226 acircumflex	227 atilde	*/
	_LET|_DOW,	_LET|_DOW,	_LET|_DOW,	_LET|_DOW,

/*	228 adiaeresis	229 aring	230 ae		231 ccedilla	*/
	_LET|_DOW,	_LET|_DOW,	_LET|_DOW,	_LET|_DOW,

/*	232 egrave	233 eacute	234 ecircumflex	235 ediaeresis	*/
	_LET|_DOW,	_LET|_DOW,	_LET|_DOW,	_LET|_DOW,

/*	236 igrave	237 iacute	238 icircumflex	239 idiaeresis	*/
	_LET|_DOW,	_LET|_DOW,	_LET|_DOW,	_LET|_DOW,

/*	240 eth		241 ntilde	242 ograve	243 oacute	*/
	_LET|_DOW,	_LET|_DOW,	_LET|_DOW,	_LET|_DOW,

/*	244 ocircumflex	245 otilde	246 odiaeresis	247 division	*/
	_LET|_DOW,	_LET|_DOW,	_LET|_DOW,	_PUN,

/*	248 oslash	249 ugrave	250 uacute	251 ucircumflex	*/
	_LET|_DOW,	_LET|_DOW,	_LET|_DOW,	_LET|_DOW,

/*	252 udiaeresis	253 yacute	254 thorn	255 ydiaeresis	*/
	_LET|_DOW,	_LET|_DOW,	_LET|_DOW,	_LET|_DOW,
};

short _enable_mbdisp = 0; /* control multi-byte 0...disable 1...enable */

/* multi-byte check table */
/* default = all 0 (clear process in update_vars()/unset()) */
unsigned short _mbmap[256];

unsigned short _mbmap_euc[256] = {
/* first byte	0x8e,0xa0 - 0xf4 */
/* second byte 	0xa0 - 0xfe */
/* 0 - 7f all 0 */
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
/*  80		81		82		83 */
    0,		0,		0,		0,
/*  84		85		86		87 */
    0,		0,		0,		0,
/*  88		89		8a		8b */
    0,		0,		0,		0,
/*  8c		8d		8e		8f */
    0,		0,		_MB1,		0,
/* 90 - 9f all 0 */
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
/*  a0		a1		a2		a3 */
    _MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,
/*  a4		a5		a6		a7 */
    _MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,
/*  a8		a9		aa		ab */
    _MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,
/*  ac		ad		ae		af */
    _MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,
/*  b0		b1		b2		b3 */
    _MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,
/*  b4		b5		b6		b7 */
    _MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,
/*  b8		b9		ba		bb */
    _MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,
/*  bc		bd		be		bf */
    _MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,
/*  c0		c1		c2		c3 */
    _MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,
/*  c4		c5		c6		c7 */
    _MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,
/*  c8		c9		ca		cb */
    _MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,
/*  cc		cd		ce		cf */
    _MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,
/*  d0		d1		d2		d3 */
    _MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,
/*  d4		d5		d6		d7 */
    _MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,
/*  d8		d9		da		db */
    _MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,
/*  dc		dd		de		df */
    _MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,
/*  e0		e1		e2		e3 */
    _MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,
/*  e4		e5		e6		e7 */
    _MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,
/*  e8		e9		ea		eb */
    _MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,
/*  ec		ed		ee		ef */
    _MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,
/*  f0		f1		f2		f3 */
    _MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,
/*  f4		f5		f6		f7 */
    _MB1|_MB2,	_MB2,		_MB2,		_MB2,
/*  f8		f9		fa		fb */
    _MB2,	_MB2,		_MB2,		_MB2,
/*  fc		fd		fe		ff */
    _MB2,	_MB2,		_MB2,		0,
};

unsigned short _mbmap_sjis[256] = {
/* first byte	0x81-0x9f,0xe0 - 0xfc */
/* second byte 	0x40-0x7e,0x80 - 0xfc */
/* 0 - 3f all 0 */
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
/*  40		41		42		43 */
    _MB2,	_MB2,		_MB2,		_MB2,
/*  44		45		46		47 */
    _MB2,	_MB2,		_MB2,		_MB2,
/*  48		49		4a		4b */
    _MB2,	_MB2,		_MB2,		_MB2,
/*  4c		4d		4e		4f */
    _MB2,	_MB2,		_MB2,		_MB2,
/*  50		51		52		53 */
    _MB2,	_MB2,		_MB2,		_MB2,
/*  54		55		56		57 */
    _MB2,	_MB2,		_MB2,		_MB2,
/*  58		59		5a		5b */
    _MB2,	_MB2,		_MB2,		_MB2,
/*  5c		5d		5e		5f */
    _MB2,	_MB2,		_MB2,		_MB2,
/*  60		61		62		63 */
    _MB2,	_MB2,		_MB2,		_MB2,
/*  64		65		66		67 */
    _MB2,	_MB2,		_MB2,		_MB2,
/*  68		69		6a		6b */
    _MB2,	_MB2,		_MB2,		_MB2,
/*  6c		6d		6e		6f */
    _MB2,	_MB2,		_MB2,		_MB2,
/*  70		71		72		73 */
    _MB2,	_MB2,		_MB2,		_MB2,
/*  74		75		76		77 */
    _MB2,	_MB2,		_MB2,		_MB2,
/*  78		79		7a		7b */
    _MB2,	_MB2,		_MB2,		_MB2,
/*  7c		7d		7e		7f */
    _MB2,	_MB2,		_MB2,		0,
/*  80		81		82		83 */
    _MB2,	_MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,
/*  84		85		86		87 */
    _MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,
/*  88		89		8a		8b */
    _MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,
/*  8c		8d		8e		8f */
    _MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,
/*  90		91		92		93 */
    _MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,
/*  94		95		96		97 */
    _MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,
/*  98		99		9a		9b */
    _MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,
/*  9c		9d		9e		9f */
    _MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,
/*  a0		a1		a2		a3 */
    _MB2,	_MB2,		_MB2,		_MB2,
/*  a4		a5		a6		a7 */
    _MB2,	_MB2,		_MB2,		_MB2,
/*  a8		a9		aa		ab */
    _MB2,	_MB2,		_MB2,		_MB2,
/*  ac		ad		ae		af */
    _MB2,	_MB2,		_MB2,		_MB2,
/*  b0		b1		b2		b3 */
    _MB2,	_MB2,		_MB2,		_MB2,
/*  b4		b5		b6		b7 */
    _MB2,	_MB2,		_MB2,		_MB2,
/*  b8		b9		ba		bb */
    _MB2,	_MB2,		_MB2,		_MB2,
/*  bc		bd		be		bf */
    _MB2,	_MB2,		_MB2,		_MB2,
/*  c0		c1		c2		c3 */
    _MB2,	_MB2,		_MB2,		_MB2,
/*  c4		c5		c6		c7 */
    _MB2,	_MB2,		_MB2,		_MB2,
/*  c8		c9		ca		cb */
    _MB2,	_MB2,		_MB2,		_MB2,
/*  cc		cd		ce		cf */
    _MB2,	_MB2,		_MB2,		_MB2,
/*  d0		d1		d2		d3 */
    _MB2,	_MB2,		_MB2,		_MB2,
/*  d4		d5		d6		d7 */
    _MB2,	_MB2,		_MB2,		_MB2,
/*  d8		d9		da		db */
    _MB2,	_MB2,		_MB2,		_MB2,
/*  dc		dd		de		df */
    _MB2,	_MB2,		_MB2,		_MB2,
/*  e0		e1		e2		e3 */
    _MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,
/*  e4		e5		e6		e7 */
    _MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,
/*  e8		e9		ea		eb */
    _MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,
/*  ec		ed		ee		ef */
    _MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,
/*  f0		f1		f2		f3 */
    _MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,
/*  f4		f5		f6		f7 */
    _MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,
/*  f8		f9		fa		fb */
    _MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,
/*  fc		fd		fe		ff */
    _MB1|_MB2,	0,		0,		0,
};

unsigned short _mbmap_big5[256] = {
/* This is latest big5 charmap, so called "Big5+" */
/* first byte   0x81 - 0xfe */
/* second byte  0x40 - 0x7e, 0x80 - 0xfe */
/* 0x00 - 0x3f = 0 */
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
/* 0x40 - 0x7e = 2 */
    _MB2,	_MB2,		_MB2,		_MB2,
    _MB2,	_MB2,		_MB2,		_MB2,
    _MB2,	_MB2,		_MB2,		_MB2,
    _MB2,	_MB2,		_MB2,		_MB2,
    _MB2,	_MB2,		_MB2,		_MB2,
    _MB2,	_MB2,		_MB2,		_MB2,
    _MB2,	_MB2,		_MB2,		_MB2,
    _MB2,	_MB2,		_MB2,		_MB2,
    _MB2,	_MB2,		_MB2,		_MB2,
    _MB2,	_MB2,		_MB2,		_MB2,
    _MB2,	_MB2,		_MB2,		_MB2,
    _MB2,	_MB2,		_MB2,		_MB2,
    _MB2,	_MB2,		_MB2,		_MB2,
    _MB2,	_MB2,		_MB2,		_MB2,
    _MB2,	_MB2,		_MB2,		_MB2,
						/* 0x7f = 0 */
    _MB2,	_MB2,		_MB2,		0,
/* 0x80 = 2, 0x81 - 0xfe = 3 */
    _MB2,      _MB1|_MB2,      _MB1|_MB2,      _MB1|_MB2,
    _MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,
    _MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,
    _MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,
    _MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,
    _MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,
    _MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,
    _MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,    
    _MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,
    _MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,
    _MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,
    _MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,
    _MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,
    _MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,
    _MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,
    _MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,    
    _MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,
    _MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,
    _MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,
    _MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,
    _MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,
    _MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,
    _MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,
    _MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,
    _MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,
    _MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,
    _MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,
    _MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,
    _MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,
    _MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,
    _MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,
						/* 0xff = 0 */
    _MB1|_MB2,	_MB1|_MB2,	_MB1|_MB2,	0,
};

unsigned short _mbmap_utf8[256] = {
/* utf8 uses up to 6 bytes */
/* first byte        0xc0 - 0xfd */
/* any further bytes 0x80 - 0xbf */
/* 0 - 7f all 0 */
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
/*  80 - bf : all _MB2 */
    _MB2, _MB2, _MB2, _MB2, _MB2, _MB2, _MB2, _MB2,
    _MB2, _MB2, _MB2, _MB2, _MB2, _MB2, _MB2, _MB2,
    _MB2, _MB2, _MB2, _MB2, _MB2, _MB2, _MB2, _MB2,
    _MB2, _MB2, _MB2, _MB2, _MB2, _MB2, _MB2, _MB2,
    _MB2, _MB2, _MB2, _MB2, _MB2, _MB2, _MB2, _MB2,
    _MB2, _MB2, _MB2, _MB2, _MB2, _MB2, _MB2, _MB2,
    _MB2, _MB2, _MB2, _MB2, _MB2, _MB2, _MB2, _MB2,
    _MB2, _MB2, _MB2, _MB2, _MB2, _MB2, _MB2, _MB2,
/* c0 - fc : all _MB1 */
    _MB1, _MB1, _MB1, _MB1, _MB1, _MB1, _MB1, _MB1,
    _MB1, _MB1, _MB1, _MB1, _MB1, _MB1, _MB1, _MB1,
    _MB1, _MB1, _MB1, _MB1, _MB1, _MB1, _MB1, _MB1,
    _MB1, _MB1, _MB1, _MB1, _MB1, _MB1, _MB1, _MB1,
    _MB1, _MB1, _MB1, _MB1, _MB1, _MB1, _MB1, _MB1,
    _MB1, _MB1, _MB1, _MB1, _MB1, _MB1, _MB1, _MB1,
    _MB1, _MB1, _MB1, _MB1, _MB1, _MB1, _MB1, _MB1,
/*  f8    f9    fa    fb    fc    fd    fe    ff*/
    _MB1, _MB1, _MB1, _MB1, _MB1, _MB1, 0,    0
};
#endif /* defined(KANJI) && defined(SHORT_STRINGS) && defined(DSPMBYTE) */

#ifndef NLS
/* _cmap_lower, _cmap_upper for ISO 8859/1 */

unsigned char _cmap_lower[256] = {
	0000,	0001,	0002,	0003,	0004,	0005,	0006,	0007,
	0010,	0011,	0012,	0013,	0014,	0015,	0016,	0017,
	0020,	0021,	0022,	0023,	0024,	0025,	0026,	0027,
	0030,	0031,	0032,	0033,	0034,	0035,	0036,	0037,
	0040,	0041,	0042,	0043,	0044,	0045,	0046,	0047,
	0050,	0051,	0052,	0053,	0054,	0055,	0056,	0057,
	0060,	0061,	0062,	0063,	0064,	0065,	0066,	0067,
	0070,	0071,	0072,	0073,	0074,	0075,	0076,	0077,
	0100,	0141,	0142,	0143,	0144,	0145,	0146,	0147,
	0150,	0151,	0152,	0153,	0154,	0155,	0156,	0157,
	0160,	0161,	0162,	0163,	0164,	0165,	0166,	0167,
	0170,	0171,	0172,	0133,	0134,	0135,	0136,	0137,
	0140,	0141,	0142,	0143,	0144,	0145,	0146,	0147,
	0150,	0151,	0152,	0153,	0154,	0155,	0156,	0157,
	0160,	0161,	0162,	0163,	0164,	0165,	0166,	0167,
	0170,	0171,	0172,	0173,	0174,	0175,	0176,	0177,
	0200,	0201,	0202,	0203,	0204,	0205,	0206,	0207,
	0210,	0211,	0212,	0213,	0214,	0215,	0216,	0217,
	0220,	0221,	0222,	0223,	0224,	0225,	0226,	0227,
	0230,	0231,	0232,	0233,	0234,	0235,	0236,	0237,
	0240,	0241,	0242,	0243,	0244,	0245,	0246,	0247,
	0250,	0251,	0252,	0253,	0254,	0255,	0256,	0257,
	0260,	0261,	0262,	0263,	0264,	0265,	0266,	0267,
	0270,	0271,	0272,	0273,	0274,	0275,	0276,	0277,
	0340,	0341,	0342,	0343,	0344,	0345,	0346,	0347,
	0350,	0351,	0352,	0353,	0354,	0355,	0356,	0357,
	0360,	0361,	0362,	0363,	0364,	0365,	0366,	0327,
	0370,	0371,	0372,	0373,	0374,	0375,	0376,	0337,
	0340,	0341,	0342,	0343,	0344,	0345,	0346,	0347,
	0350,	0351,	0352,	0353,	0354,	0355,	0356,	0357,
	0360,	0361,	0362,	0363,	0364,	0365,	0366,	0367,
	0370,	0371,	0372,	0373,	0374,	0375,	0376,	0377,
};

unsigned char _cmap_upper[256] = {
	0000,	0001,	0002,	0003,	0004,	0005,	0006,	0007,
	0010,	0011,	0012,	0013,	0014,	0015,	0016,	0017,
	0020,	0021,	0022,	0023,	0024,	0025,	0026,	0027,
	0030,	0031,	0032,	0033,	0034,	0035,	0036,	0037,
	0040,	0041,	0042,	0043,	0044,	0045,	0046,	0047,
	0050,	0051,	0052,	0053,	0054,	0055,	0056,	0057,
	0060,	0061,	0062,	0063,	0064,	0065,	0066,	0067,
	0070,	0071,	0072,	0073,	0074,	0075,	0076,	0077,
	0100,	0101,	0102,	0103,	0104,	0105,	0106,	0107,
	0110,	0111,	0112,	0113,	0114,	0115,	0116,	0117,
	0120,	0121,	0122,	0123,	0124,	0125,	0126,	0127,
	0130,	0131,	0132,	0133,	0134,	0135,	0136,	0137,
	0140,	0101,	0102,	0103,	0104,	0105,	0106,	0107,
	0110,	0111,	0112,	0113,	0114,	0115,	0116,	0117,
	0120,	0121,	0122,	0123,	0124,	0125,	0126,	0127,
	0130,	0131,	0132,	0173,	0174,	0175,	0176,	0177,
	0200,	0201,	0202,	0203,	0204,	0205,	0206,	0207,
	0210,	0211,	0212,	0213,	0214,	0215,	0216,	0217,
	0220,	0221,	0222,	0223,	0224,	0225,	0226,	0227,
	0230,	0231,	0232,	0233,	0234,	0235,	0236,	0237,
	0240,	0241,	0242,	0243,	0244,	0245,	0246,	0247,
	0250,	0251,	0252,	0253,	0254,	0255,	0256,	0257,
	0260,	0261,	0262,	0263,	0264,	0265,	0266,	0267,
	0270,	0271,	0272,	0273,	0274,	0275,	0276,	0277,
	0300,	0301,	0302,	0303,	0304,	0305,	0306,	0307,
	0310,	0311,	0312,	0313,	0314,	0315,	0316,	0317,
	0320,	0321,	0322,	0323,	0324,	0325,	0326,	0327,
	0330,	0331,	0332,	0333,	0334,	0335,	0336,	0337,
	0300,	0301,	0302,	0303,	0304,	0305,	0306,	0307,
	0310,	0311,	0312,	0313,	0314,	0315,	0316,	0317,
	0320,	0321,	0322,	0323,	0324,	0325,	0326,	0367,
	0330,	0331,	0332,	0333,	0334,	0335,	0336,	0377,
};
#endif /* NLS */

#ifdef _OSD_POSIX
/*
	   Initial Port for tcsh-6.07.04 by <Martin.Kraemer@Mch.SNI.De>

"BS2000 OSD" is a POSIX on a main frame.
It is made by Siemens Nixdorf AG, Germany.
Within the POSIX subsystem, the same character set was chosen as in
"native BS2000", namely EBCDIC.  Yes, tcsh now runs on EBCDIC platforms, too.

EBCDIC Table. (Yes, in EBCDIC, the letters 'a'..'z' are not contiguous!)
This table is bijective, i.e. there are no ambigous or duplicate characters
00    00 01 02 03 85 09 86 7f  87 8d 8e 0b 0c 0d 0e 0f  * ................ *
10    10 11 12 13 8f 0a 08 97  18 19 9c 9d 1c 1d 1e 1f  * ................ *
20    80 81 82 83 84 92 17 1b  88 89 8a 8b 8c 05 06 07  * ................ *
30    90 91 16 93 94 95 96 04  98 99 9a 9b 14 15 9e 1a  * ................ *
40    20 a0 e2 e4 e0 e1 e3 e5  e7 f1 60 2e 3c 28 2b 7c  *  .........`.<(+| *
50    26 e9 ea eb e8 ed ee ef  ec df 21 24 2a 29 3b 9f  * &.........!$*);. *
60    2d 2f c2 c4 c0 c1 c3 c5  c7 d1 5e 2c 25 5f 3e 3f  * -/........^,%_>? *
70    f8 c9 ca cb c8 cd ce cf  cc a8 3a 23 40 27 3d 22  * ..........:#@'=" *
80    d8 61 62 63 64 65 66 67  68 69 ab bb f0 fd fe b1  * .abcdefghi...... *
90    b0 6a 6b 6c 6d 6e 6f 70  71 72 aa ba e6 b8 c6 a4  * .jklmnopqr...... *
a0    b5 af 73 74 75 76 77 78  79 7a a1 bf d0 dd de ae  * ..stuvwxyz...... *
b0    a2 a3 a5 b7 a9 a7 b6 bc  bd be ac 5b 5c 5d b4 d7  * ...........[\].. *
c0    f9 41 42 43 44 45 46 47  48 49 ad f4 f6 f2 f3 f5  * .ABCDEFGHI...... *
d0    a6 4a 4b 4c 4d 4e 4f 50  51 52 b9 fb fc db fa ff  * .JKLMNOPQR...... *
e0    d9 f7 53 54 55 56 57 58  59 5a b2 d4 d6 d2 d3 d5  * ..STUVWXYZ...... *
f0    30 31 32 33 34 35 36 37  38 39 b3 7b dc 7d da 7e  * 0123456789.{.}.~ *
*/
unsigned short _toascii[256] = {
/*00*/ 0x00, 0x01, 0x02, 0x03, 0x85, 0x09, 0x86, 0x7f,
       0x87, 0x8d, 0x8e, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, /* ................ */
/*10*/ 0x10, 0x11, 0x12, 0x13, 0x8f, 0x0a, 0x08, 0x97,
       0x18, 0x19, 0x9c, 0x9d, 0x1c, 0x1d, 0x1e, 0x1f, /* ................ */
/*20*/ 0x80, 0x81, 0x82, 0x83, 0x84, 0x92, 0x17, 0x1b,
       0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x05, 0x06, 0x07, /* ................ */
/*30*/ 0x90, 0x91, 0x16, 0x93, 0x94, 0x95, 0x96, 0x04,
       0x98, 0x99, 0x9a, 0x9b, 0x14, 0x15, 0x9e, 0x1a, /* ................ */
/*40*/ 0x20, 0xa0, 0xe2, 0xe4, 0xe0, 0xe1, 0xe3, 0xe5,
       0xe7, 0xf1, 0x60, 0x2e, 0x3c, 0x28, 0x2b, 0x7c, /* .........`.<(+|  */
/*50*/ 0x26, 0xe9, 0xea, 0xeb, 0xe8, 0xed, 0xee, 0xef,
       0xec, 0xdf, 0x21, 0x24, 0x2a, 0x29, 0x3b, 0x9f, /* &.........!$*);. */
/*60*/ 0x2d, 0x2f, 0xc2, 0xc4, 0xc0, 0xc1, 0xc3, 0xc5,
       0xc7, 0xd1, 0x5e, 0x2c, 0x25, 0x5f, 0x3e, 0x3f, /* -/........^,%_>? */
/*70*/ 0xf8, 0xc9, 0xca, 0xcb, 0xc8, 0xcd, 0xce, 0xcf,
       0xcc, 0xa8, 0x3a, 0x23, 0x40, 0x27, 0x3d, 0x22, /* ..........:#@'=" */
/*80*/ 0xd8, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67,
       0x68, 0x69, 0xab, 0xbb, 0xf0, 0xfd, 0xfe, 0xb1, /* .abcdefghi...... */
/*90*/ 0xb0, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f, 0x70,
       0x71, 0x72, 0xaa, 0xba, 0xe6, 0xb8, 0xc6, 0xa4, /* .jklmnopqr...... */
/*a0*/ 0xb5, 0xaf, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78,
       0x79, 0x7a, 0xa1, 0xbf, 0xd0, 0xdd, 0xde, 0xae, /* ..stuvwxyz...... */
/*b0*/ 0xa2, 0xa3, 0xa5, 0xb7, 0xa9, 0xa7, 0xb6, 0xbc,
       0xbd, 0xbe, 0xac, 0x5b, 0x5c, 0x5d, 0xb4, 0xd7, /* ...........[\].. */
/*c0*/ 0xf9, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47,
       0x48, 0x49, 0xad, 0xf4, 0xf6, 0xf2, 0xf3, 0xf5, /* .ABCDEFGHI...... */
/*d0*/ 0xa6, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f, 0x50,
       0x51, 0x52, 0xb9, 0xfb, 0xfc, 0xdb, 0xfa, 0xff, /* .JKLMNOPQR...... */
/*e0*/ 0xd9, 0xf7, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,
       0x59, 0x5a, 0xb2, 0xd4, 0xd6, 0xd2, 0xd3, 0xd5, /* ..STUVWXYZ...... */
/*f0*/ 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
       0x38, 0x39, 0xb3, 0x7b, 0xdc, 0x7d, 0xda, 0x7e  /* 0123456789.{.}.~ */
};
/* The same, for ascii-in-ebcdic
00    00 01 02 03 37 2d 2e 2f  16 05 15 0b 0c 0d 0e 0f  * ................ *
10    10 11 12 13 3c 3d 32 26  18 19 3f 27 1c 1d 1e 1f  * ................ *
20    40 5a 7f 7b 5b 6c 50 7d  4d 5d 5c 4e 6b 60 4b 61  *  !"#$%&'()*+,-./ *
30    f0 f1 f2 f3 f4 f5 f6 f7  f8 f9 7a 5e 4c 7e 6e 6f  * 0123456789:;<=>? *
40    7c c1 c2 c3 c4 c5 c6 c7  c8 c9 d1 d2 d3 d4 d5 d6  * @ABCDEFGHIJKLMNO *
50    d7 d8 d9 e2 e3 e4 e5 e6  e7 e8 e9 bb bc bd 6a 6d  * PQRSTUVWXYZ[\]^_ *
60    4a 81 82 83 84 85 86 87  88 89 91 92 93 94 95 96  * `abcdefghijklmno *
70    97 98 99 a2 a3 a4 a5 a6  a7 a8 a9 fb 4f fd ff 07  * pqrstuvwxyz{|}~. *
80    20 21 22 23 24 04 06 08  28 29 2a 2b 2c 09 0a 14  * ................ *
90    30 31 25 33 34 35 36 17  38 39 3a 3b 1a 1b 3e 5f  * ................ *
a0    41 aa b0 b1 9f b2 d0 b5  79 b4 9a 8a ba ca af a1  * ................ *
b0    90 8f ea fa be a0 b6 b3  9d da 9b 8b b7 b8 b9 ab  * ................ *
c0    64 65 62 66 63 67 9e 68  74 71 72 73 78 75 76 77  * ................ *
d0    ac 69 ed ee eb ef ec bf  80 e0 fe dd fc ad ae 59  * ................ *
e0    44 45 42 46 43 47 9c 48  54 51 52 53 58 55 56 57  * ................ *
f0    8c 49 cd ce cb cf cc e1  70 c0 de db dc 8d 8e df  * ................ *
*/
unsigned short _toebcdic[256] = {
/*00*/  0x00, 0x01, 0x02, 0x03, 0x37, 0x2d, 0x2e, 0x2f,
	0x16, 0x05, 0x15, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,  /* ................ */
/*10*/  0x10, 0x11, 0x12, 0x13, 0x3c, 0x3d, 0x32, 0x26,
	0x18, 0x19, 0x3f, 0x27, 0x1c, 0x1d, 0x1e, 0x1f,  /* ................ */
/*20*/  0x40, 0x5a, 0x7f, 0x7b, 0x5b, 0x6c, 0x50, 0x7d,
	0x4d, 0x5d, 0x5c, 0x4e, 0x6b, 0x60, 0x4b, 0x61,  /*  !"#$%&'()*+,-./ */
/*30*/  0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7,
	0xf8, 0xf9, 0x7a, 0x5e, 0x4c, 0x7e, 0x6e, 0x6f,  /* 0123456789:;<=>? */
/*40*/  0x7c, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7,
	0xc8, 0xc9, 0xd1, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6,  /* @ABCDEFGHIJKLMNO */
/*50*/  0xd7, 0xd8, 0xd9, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6,
	0xe7, 0xe8, 0xe9, 0xbb, 0xbc, 0xbd, 0x6a, 0x6d,  /* PQRSTUVWXYZ[\]^_ */
/*60*/  0x4a, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
	0x88, 0x89, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96,  /* `abcdefghijklmno */
/*70*/  0x97, 0x98, 0x99, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6,
	0xa7, 0xa8, 0xa9, 0xfb, 0x4f, 0xfd, 0xff, 0x07,  /* pqrstuvwxyz{|}~. */
/*80*/  0x20, 0x21, 0x22, 0x23, 0x24, 0x04, 0x06, 0x08,
	0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x09, 0x0a, 0x14,  /* ................ */
/*90*/  0x30, 0x31, 0x25, 0x33, 0x34, 0x35, 0x36, 0x17,
	0x38, 0x39, 0x3a, 0x3b, 0x1a, 0x1b, 0x3e, 0x5f,  /* ................ */
/*a0*/  0x41, 0xaa, 0xb0, 0xb1, 0x9f, 0xb2, 0xd0, 0xb5,
	0x79, 0xb4, 0x9a, 0x8a, 0xba, 0xca, 0xaf, 0xa1,  /* ................ */
/*b0*/  0x90, 0x8f, 0xea, 0xfa, 0xbe, 0xa0, 0xb6, 0xb3,
	0x9d, 0xda, 0x9b, 0x8b, 0xb7, 0xb8, 0xb9, 0xab,  /* ................ */
/*c0*/  0x64, 0x65, 0x62, 0x66, 0x63, 0x67, 0x9e, 0x68,
	0x74, 0x71, 0x72, 0x73, 0x78, 0x75, 0x76, 0x77,  /* ................ */
/*d0*/  0xac, 0x69, 0xed, 0xee, 0xeb, 0xef, 0xec, 0xbf,
	0x80, 0xe0, 0xfe, 0xdd, 0xfc, 0xad, 0xae, 0x59,  /* ................ */
/*e0*/  0x44, 0x45, 0x42, 0x46, 0x43, 0x47, 0x9c, 0x48,
	0x54, 0x51, 0x52, 0x53, 0x58, 0x55, 0x56, 0x57,  /* ................ */
/*f0*/  0x8c, 0x49, 0xcd, 0xce, 0xcb, 0xcf, 0xcc, 0xe1,
	0x70, 0xc0, 0xde, 0xdb, 0xdc, 0x8d, 0x8e, 0xdf   /* ................ */
};

#endif /*_OSD_POSIX*/

#ifdef __MVS__
/* 
   The IBM 1047 coded char set to/from ISO 8859-1 mapping differs from 
   the POSIX-BC mapping in several places
 */
unsigned short _toascii[256] = {
      0x00, 0x01, 0x02, 0x03, 0x9c, 0x09, 0x86, 0x7f, 
      0x97, 0x8d, 0x8e, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 
      0x10, 0x11, 0x12, 0x13, 0x9d, 0x0a, 0x08, 0x87, 
      0x18, 0x19, 0x92, 0x8f, 0x1c, 0x1d, 0x1e, 0x1f, 
      0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x17, 0x1b, 
      0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x05, 0x06, 0x07, 
      0x90, 0x91, 0x16, 0x93, 0x94, 0x95, 0x96, 0x04, 
      0x98, 0x99, 0x9a, 0x9b, 0x14, 0x15, 0x9e, 0x1a, 
      0x20, 0xa0, 0xe2, 0xe4, 0xe0, 0xe1, 0xe3, 0xe5, 
      0xe7, 0xf1, 0xa2, 0x2e, 0x3c, 0x28, 0x2b, 0x7c, 
      0x26, 0xe9, 0xea, 0xeb, 0xe8, 0xed, 0xee, 0xef, 
      0xec, 0xdf, 0x21, 0x24, 0x2a, 0x29, 0x3b, 0x5e, 
      0x2d, 0x2f, 0xc2, 0xc4, 0xc0, 0xc1, 0xc3, 0xc5, 
      0xc7, 0xd1, 0xa6, 0x2c, 0x25, 0x5f, 0x3e, 0x3f, 
      0xf8, 0xc9, 0xca, 0xcb, 0xc8, 0xcd, 0xce, 0xcf, 
      0xcc, 0x60, 0x3a, 0x23, 0x40, 0x27, 0x3d, 0x22, 
      0xd8, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 
      0x68, 0x69, 0xab, 0xbb, 0xf0, 0xfd, 0xfe, 0xb1, 
      0xb0, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f, 0x70, 
      0x71, 0x72, 0xaa, 0xba, 0xe6, 0xb8, 0xc6, 0xa4, 
      0xb5, 0x7e, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 
      0x79, 0x7a, 0xa1, 0xbf, 0xd0, 0x5b, 0xde, 0xae, 
      0xac, 0xa3, 0xa5, 0xb7, 0xa9, 0xa7, 0xb6, 0xbc, 
      0xbd, 0xbe, 0xdd, 0xa8, 0xaf, 0x5d, 0xb4, 0xd7, 
      0x7b, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 
      0x48, 0x49, 0xad, 0xf4, 0xf6, 0xf2, 0xf3, 0xf5, 
      0x7d, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f, 0x50, 
      0x51, 0x52, 0xb9, 0xfb, 0xfc, 0xf9, 0xfa, 0xff, 
      0x5c, 0xf7, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 
      0x59, 0x5a, 0xb2, 0xd4, 0xd6, 0xd2, 0xd3, 0xd5, 
      0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 
      0x38, 0x39, 0xb3, 0xdb, 0xdc, 0xd9, 0xda, 0x9f, 
};
unsigned short _toebcdic[256] = {
      0x00, 0x01, 0x02, 0x03, 0x37, 0x2d, 0x2e, 0x2f, 
      0x16, 0x05, 0x15, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 
      0x10, 0x11, 0x12, 0x13, 0x3c, 0x3d, 0x32, 0x26, 
      0x18, 0x19, 0x3f, 0x27, 0x1c, 0x1d, 0x1e, 0x1f, 
      0x40, 0x5a, 0x7f, 0x7b, 0x5b, 0x6c, 0x50, 0x7d, 
      0x4d, 0x5d, 0x5c, 0x4e, 0x6b, 0x60, 0x4b, 0x61, 
      0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 
      0xf8, 0xf9, 0x7a, 0x5e, 0x4c, 0x7e, 0x6e, 0x6f, 
      0x7c, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 
      0xc8, 0xc9, 0xd1, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 
      0xd7, 0xd8, 0xd9, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 
      0xe7, 0xe8, 0xe9, 0xad, 0xe0, 0xbd, 0x5f, 0x6d, 
      0x79, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 
      0x88, 0x89, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 
      0x97, 0x98, 0x99, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 
      0xa7, 0xa8, 0xa9, 0xc0, 0x4f, 0xd0, 0xa1, 0x07, 
      0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x06, 0x17, 
      0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x09, 0x0a, 0x1b, 
      0x30, 0x31, 0x1a, 0x33, 0x34, 0x35, 0x36, 0x08, 
      0x38, 0x39, 0x3a, 0x3b, 0x04, 0x14, 0x3e, 0xff, 
      0x41, 0xaa, 0x4a, 0xb1, 0x9f, 0xb2, 0x6a, 0xb5, 
      0xbb, 0xb4, 0x9a, 0x8a, 0xb0, 0xca, 0xaf, 0xbc, 
      0x90, 0x8f, 0xea, 0xfa, 0xbe, 0xa0, 0xb6, 0xb3, 
      0x9d, 0xda, 0x9b, 0x8b, 0xb7, 0xb8, 0xb9, 0xab, 
      0x64, 0x65, 0x62, 0x66, 0x63, 0x67, 0x9e, 0x68, 
      0x74, 0x71, 0x72, 0x73, 0x78, 0x75, 0x76, 0x77, 
      0xac, 0x69, 0xed, 0xee, 0xeb, 0xef, 0xec, 0xbf, 
      0x80, 0xfd, 0xfe, 0xfb, 0xfc, 0xba, 0xae, 0x59, 
      0x44, 0x45, 0x42, 0x46, 0x43, 0x47, 0x9c, 0x48, 
      0x54, 0x51, 0x52, 0x53, 0x58, 0x55, 0x56, 0x57, 
      0x8c, 0x49, 0xcd, 0xce, 0xcb, 0xcf, 0xcc, 0xe1, 
      0x70, 0xdd, 0xde, 0xdb, 0xdc, 0x8d, 0x8e, 0xdf 
};
#endif /*__MVS__*/
