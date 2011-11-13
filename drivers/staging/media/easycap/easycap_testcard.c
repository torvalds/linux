/******************************************************************************
*                                                                             *
*  easycap_testcard.c                                                         *
*                                                                             *
******************************************************************************/
/*
 *
 *  Copyright (C) 2010 R.M. Thomas  <rmthomas@sciolus.org>
 *
 *
 *  This is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  The software is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this software; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
*/
/*****************************************************************************/

#include "easycap.h"

/*****************************************************************************/
#define TESTCARD_BYTESPERLINE (2 * 720)
void
easycap_testcard(struct easycap *peasycap, int field)
{
	int total;
	int y, u, v, r, g, b;
	unsigned char uyvy[4];
	int i1, line, k, m, n, more, much, barwidth, barheight;
	unsigned char bfbar[TESTCARD_BYTESPERLINE / 8], *p1, *p2;
	struct data_buffer *pfield_buffer;

	if (!peasycap) {
		SAY("ERROR: peasycap is NULL\n");
		return;
	}
	JOM(8, "%i=field\n", field);
	switch (peasycap->width) {
	case 720:
	case 360: {
		barwidth = (2 * 720) / 8;
		break;
	}
	case 704:
	case 352: {
		barwidth = (2 * 704) / 8;
		break;
	}
	case 640:
	case 320: {
		barwidth = (2 * 640) / 8;
		break;
	}
	default: {
		SAM("ERROR:  cannot set barwidth\n");
		return;
	}
	}
	if (TESTCARD_BYTESPERLINE < barwidth) {
		SAM("ERROR: barwidth is too large\n");
		return;
	}
	switch (peasycap->height) {
	case 576:
	case 288: {
		barheight = 576;
		break;
	}
	case 480:
	case 240: {
		barheight = 480;
		break;
	}
	default: {
		SAM("ERROR: cannot set barheight\n");
		return;
	}
	}
	total = 0;
	k = field;
	m = 0;
	n = 0;

	for (line = 0;  line < (barheight / 2);  line++) {
		for (i1 = 0;  i1 < 8;  i1++) {
			r = (i1 * 256)/8;
			g = (i1 * 256)/8;
			b = (i1 * 256)/8;

			y =  299*r/1000 + 587*g/1000 + 114*b/1000 ;
			u = -147*r/1000 - 289*g/1000 + 436*b/1000 ;
			u = u + 128;
			v =  615*r/1000 - 515*g/1000 - 100*b/1000 ;
			v = v + 128;

			uyvy[0] =  0xFF & u ;
			uyvy[1] =  0xFF & y ;
			uyvy[2] =  0xFF & v ;
			uyvy[3] =  0xFF & y ;

			p1 = &bfbar[0];
			while (p1 < &bfbar[barwidth]) {
				*p1++ = uyvy[0] ;
				*p1++ = uyvy[1] ;
				*p1++ = uyvy[2] ;
				*p1++ = uyvy[3] ;
				total += 4;
			}

			p1 = &bfbar[0];
			more = barwidth;

			while (more) {
				if ((FIELD_BUFFER_SIZE/PAGE_SIZE) <= m) {
					SAM("ERROR:  bad m reached\n");
					return;
				}
				if (PAGE_SIZE < n) {
					SAM("ERROR:  bad n reached\n");
					return;
				}

				if (0 > more) {
					SAM("ERROR:  internal fault\n");
					return;
				}

				much = PAGE_SIZE - n;
				if (much > more)
					much = more;
				pfield_buffer = &peasycap->field_buffer[k][m];
				p2 = pfield_buffer->pgo + n;
				memcpy(p2, p1, much);

				p1 += much;
				n += much;
				more -= much;
				if (PAGE_SIZE == n) {
					m++;
					n = 0;
				}
			}
		}
	}
	return;
}
