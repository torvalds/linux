/*	$OpenBSD: char.c,v 1.6 2015/10/26 16:27:04 naddy Exp $	*/
/*	$NetBSD: char.c,v 1.6 1995/03/21 09:02:26 cgd Exp $	*/

/*-
 * Copyright (c) 1980, 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
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

#include "char.h"

unsigned short _cmap[256] = {
/*	nul		soh		stx		etx	*/
	_CTR,		_CTR,		_CTR,		_CTR,

/*	eot		enq		ack		bel	*/
	_CTR,		_CTR,		_CTR,		_CTR,

/*	bs		ht		nl		vt	*/
	_CTR,		_CTR|_SP|_META,	_CTR|_NL|_META,	_CTR,

/*	np		cr		so		si	*/
	_CTR,		_CTR,		_CTR,		_CTR,

/*	dle		dc1		dc2		dc3	*/
	_CTR,		_CTR,		_CTR,		_CTR,

/*	dc4		nak		syn		etb	*/
	_CTR,		_CTR,		_CTR,		_CTR,

/*	can		em		sub		esc	*/
	_CTR,		_CTR,		_CTR,		_CTR,

/*	fs		gs		rs		us	*/
	_CTR,		_CTR,		_CTR,		_CTR,

/*	sp		!		"		#	*/
	_SP|_META,	0,		_QF,		_META,

/*	$		%		&		'	*/
	_DOL,		0,		_META|_CMD,	_QF,

/*	(		)		*		+	*/
	_META|_CMD,	_META,		_GLOB,		0,

/*	,		-		.		/	*/
	0,		0,		0,		0,

/*	0		1		2		3	*/
	_DIG|_XD,	_DIG|_XD,	_DIG|_XD,	_DIG|_XD,

/*	4		5		6		7	*/
	_DIG|_XD,	_DIG|_XD,	_DIG|_XD,	_DIG|_XD,

/*	8		9		:		;	*/
	_DIG|_XD,	_DIG|_XD,	0,		_META|_CMD,

/*	<		=		>		?	*/
	_META,		0,		_META,		_GLOB,

/*	@		A		B		C	*/
	0,		_LET|_UP|_XD,	_LET|_UP|_XD,	_LET|_UP|_XD,

/*	D		E		F		G	*/
	_LET|_UP|_XD,	_LET|_UP|_XD,	_LET|_UP|_XD,	_LET|_UP,

/*	H		I		J		K	*/
	_LET|_UP,	_LET|_UP,	_LET|_UP,	_LET|_UP,

/*	L		M		N		O	*/
	_LET|_UP,	_LET|_UP,	_LET|_UP,	_LET|_UP,

/*	P		Q		R		S	*/
	_LET|_UP,	_LET|_UP,	_LET|_UP,	_LET|_UP,

/*	T		U		V		W	*/
	_LET|_UP,	_LET|_UP,	_LET|_UP,	_LET|_UP,

/*	X		Y		Z		[	*/
	_LET|_UP,	_LET|_UP,	_LET|_UP,	_GLOB,

/*	\		]		^		_	*/
	_ESC,		0,		0,		0,

/*	`		a		b		c	*/
  _QB|_GLOB|_META,	_LET|_LOW|_XD,	_LET|_LOW|_XD,	_LET|_LOW|_XD,

/*	d		e		f		g	*/
	_LET|_LOW|_XD,	_LET|_LOW|_XD,	_LET|_LOW|_XD,	_LET|_LOW,

/*	h		i		j		k	*/
	_LET|_LOW,	_LET|_LOW,	_LET|_LOW,	_LET|_LOW,

/*	l		m		n		o	*/
	_LET|_LOW,	_LET|_LOW,	_LET|_LOW,	_LET|_LOW,

/*	p		q		r		s	*/
	_LET|_LOW,	_LET|_LOW,	_LET|_LOW,	_LET|_LOW,

/*	t		u		v		w	*/
	_LET|_LOW,	_LET|_LOW,	_LET|_LOW,	_LET|_LOW,

/*	x		y		z		{	*/
	_LET|_LOW,	_LET|_LOW,	_LET|_LOW,	_GLOB,

/*	|		}		~		del	*/
	_META|_CMD,	0,		0,		_CTR,

/****************************************************************/
/* 128 - 255							*/
/****************************************************************/
/*	(undef)		...					*/
	0,
};
