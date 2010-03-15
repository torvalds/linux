/*
 *************************************************************************
 * Ralink Tech Inc.
 * 5F., No.36, Taiyuan St., Jhubei City,
 * Hsinchu County 302,
 * Taiwan, R.O.C.
 *
 * (c) Copyright 2002-2007, Ralink Technology, Inc.
 *
 * This program is free software; you can redistribute it and/or modify  *
 * it under the terms of the GNU General Public License as published by  *
 * the Free Software Foundation; either version 2 of the License, or     *
 * (at your option) any later version.                                   *
 *                                                                       *
 * This program is distributed in the hope that it will be useful,       *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 * GNU General Public License for more details.                          *
 *                                                                       *
 * You should have received a copy of the GNU General Public License     *
 * along with this program; if not, write to the                         *
 * Free Software Foundation, Inc.,                                       *
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 *                                                                       *
 *************************************************************************
 */

#ifndef __MD4_H__
#define __MD4_H__

/* MD4 context. */
typedef	struct	_MD4_CTX_	{
	unsigned long	state[4];        /* state (ABCD) */
	unsigned long	count[2];        /* number of bits, modulo 2^64 (lsb first) */
	u8	buffer[64];      /* input buffer */
}	MD4_CTX;

void MD4Init(MD4_CTX *);
void MD4Update(MD4_CTX *, u8 *, UINT);
void MD4Final(u8 [16], MD4_CTX *);

#endif /*__MD4_H__*/
