/****************************************************************************
 * Copyright (c) 1998-2008,2012 Free Software Foundation, Inc.              *
 *                                                                          *
 * Permission is hereby granted, free of charge, to any person obtaining a  *
 * copy of this software and associated documentation files (the            *
 * "Software"), to deal in the Software without restriction, including      *
 * without limitation the rights to use, copy, modify, merge, publish,      *
 * distribute, distribute with modifications, sublicense, and/or sell       *
 * copies of the Software, and to permit persons to whom the Software is    *
 * furnished to do so, subject to the following conditions:                 *
 *                                                                          *
 * The above copyright notice and this permission notice shall be included  *
 * in all copies or substantial portions of the Software.                   *
 *                                                                          *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS  *
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF               *
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.   *
 * IN NO EVENT SHALL THE ABOVE COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,   *
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR    *
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR    *
 * THE USE OR OTHER DEALINGS IN THE SOFTWARE.                               *
 *                                                                          *
 * Except as contained in this notice, the name(s) of the above copyright   *
 * holders shall not be used in advertising or otherwise to promote the     *
 * sale, use or other dealings in this Software without prior written       *
 * authorization.                                                           *
 ****************************************************************************/

/****************************************************************************
 *  Author: Zeyd M. Ben-Halim <zmbenhal@netcom.com> 1992,1995               *
 *     and: Eric S. Raymond <esr@snark.thyrsus.com>                         *
 ****************************************************************************/

/*
 * Common macros for lib_getch.c, lib_ungetch.c
 *
 * $Id: fifo_defs.h,v 1.7 2012/08/04 15:59:17 tom Exp $
 */

#ifndef FIFO_DEFS_H
#define FIFO_DEFS_H 1

#define head	sp->_fifohead
#define tail	sp->_fifotail
/* peek points to next uninterpreted character */
#define peek	sp->_fifopeek

#define h_inc() { \
	    (head >= FIFO_SIZE-1) \
		? head = 0 \
		: head++; \
	    if (head == tail) \
		head = -1, tail = 0; \
	}
#define h_dec() { \
	    (head <= 0) \
		? head = FIFO_SIZE-1 \
		: head--; \
	    if (head == tail) \
		tail = -1; \
	}
#define t_inc() { \
	    (tail >= FIFO_SIZE-1) \
		? tail = 0 \
		: tail++; \
	    if (tail == head) \
		tail = -1; \
	    }
#define t_dec() { \
	    (tail <= 0) \
		? tail = FIFO_SIZE-1 \
		: tail--; \
	    if (head == tail) \
		fifo_clear(sp); \
	    }
#define p_inc() { \
	    (peek >= FIFO_SIZE-1) \
		? peek = 0 \
		: peek++; \
	    }

#define cooked_key_in_fifo()	((head >= 0) && (peek != head))
#define raw_key_in_fifo()	((head >= 0) && (peek != tail))

#undef HIDE_EINTR

#endif /* FIFO_DEFS_H */
