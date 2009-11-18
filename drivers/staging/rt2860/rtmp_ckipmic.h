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

typedef	struct	_MIC_CONTEXT	{
	/* --- MMH context                            */
	UCHAR		CK[16];				/* the key                                    */
	UCHAR		coefficient[16];	/* current aes counter mode coefficients      */
	ULONGLONG	accum;				/* accumulated mic, reduced to u32 in final() */
	UINT		position;			/* current position (byte offset) in message  */
	UCHAR		part[4];			/* for conversion of message to u32 for mmh   */
}	MIC_CONTEXT, *PMIC_CONTEXT;

VOID xor_128(
    IN  PUCHAR              a,
    IN  PUCHAR              b,
    OUT PUCHAR              out);

UCHAR RTMPCkipSbox(
    IN  UCHAR               a);

VOID xor_32(
    IN  PUCHAR              a,
    IN  PUCHAR              b,
    OUT PUCHAR              out);

VOID next_key(
    IN  PUCHAR              key,
    IN  INT                 round);

VOID byte_sub(
    IN  PUCHAR              in,
    OUT PUCHAR              out);

VOID shift_row(
    IN  PUCHAR              in,
    OUT PUCHAR              out);

VOID mix_column(
    IN  PUCHAR              in,
    OUT PUCHAR              out);

#endif //__RTMP_CKIPMIC_H__
