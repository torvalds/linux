/* Copyright (c) 1995 Vixie Enterprises
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies, and that
 * the name of Vixie Enterprises not be used in advertising or publicity
 * pertaining to distribution of the document or software without specific,
 * written prior permission.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS" AND VIXIE ENTERPRISES DISCLAIMS ALL
 * WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS.   IN NO EVENT SHALL VIXIE ENTERPRISES
 * BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES
 * OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
 * WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
 * ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
 * SOFTWARE.
 */

#ifndef _I8253_DEFINED
#define _I8253_DEFINED

typedef union {
	unsigned char	i;
	struct {
		unsigned int	bcd	: 1;
#define				i8253_binary	0
#define				i8253_bcd	1
		unsigned int	mode	: 3;
#define				i8253_termcnt	0
#define				i8253_oneshot	1
#define				i8253_rategen	2
#define				i8253_sqrwave	3
#define				i8253_softstb	4
#define				i8253_hardstb	5
		unsigned int	rl	: 2;
#define				i8253_latch	0
#define				i8253_lsb	1
#define				i8253_msb	2
#define				i8253_lmb	3
		unsigned int	cntr	: 2;
#define				i8253_cntr_0	0
#define				i8253_cntr_1	1
#define				i8253_cntr_2	2
	} s;
} i8253_ctrl;

#endif /*_I8253_DEFINED*/
