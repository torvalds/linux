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

	Module Name:
	rtmp_ckipmic.h

	Abstract:

	Revision History:
	Who			When			What
	--------	----------		----------------------------------------------
	Name		Date			Modification logs
*/
#ifndef	__RTMP_CKIPMIC_H__
#define	__RTMP_CKIPMIC_H__

struct rt_mic_context {
	/* --- MMH context                            */
	u8 CK[16];		/* the key                                    */
	u8 coefficient[16];	/* current aes counter mode coefficients      */
	unsigned long long accum;	/* accumulated mic, reduced to u32 in final() */
	u32 position;		/* current position (byte offset) in message  */
	u8 part[4];		/* for conversion of message to u32 for mmh   */
};

void xor_128(u8 *a, u8 *b, u8 *out);

u8 RTMPCkipSbox(u8 a);

void xor_32(u8 *a, u8 *b, u8 *out);

void next_key(u8 *key, int round);

void byte_sub(u8 *in, u8 *out);

void shift_row(u8 *in, u8 *out);

void mix_column(u8 *in, u8 *out);

#endif /*__RTMP_CKIPMIC_H__ */
