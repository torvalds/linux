/*
 * Copyright (c) 1996, 2003 VIA Networking Technologies, Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 *
 * File: michael.cpp
 *
 * Purpose: The implementation of LIST data structure.
 *
 * Author: Kyle Hsu
 *
 * Date: Sep 4, 2002
 *
 * Functions:
 *      s_dwGetUINT32 - Convert from u8[] to DWORD in a portable way
 *      s_vPutUINT32 - Convert from DWORD to u8[] in a portable way
 *      s_vClear - Reset the state to the empty message.
 *      s_vSetKey - Set the key.
 *      MIC_vInit - Set the key.
 *      s_vAppendByte - Append the byte to our word-sized buffer.
 *      MIC_vAppend - call s_vAppendByte.
 *      MIC_vGetMIC - Append the minimum padding and call s_vAppendByte.
 *
 * Revision History:
 *
 */

#include "tmacro.h"
#include "michael.h"

/*---------------------  Static Definitions -------------------------*/

/*---------------------  Static Variables  --------------------------*/

/*---------------------  Static Functions  --------------------------*/
/*
 * static DWORD s_dwGetUINT32(u8 * p);         Get DWORD from
 *							4 bytes LSByte first
 * static void s_vPutUINT32(u8* p, DWORD val); Put DWORD into
 *							4 bytes LSByte first
 */
static void s_vClear(void);		/* Clear the internal message,
					 * resets the object to the
					 * state just after construction. */
static void s_vSetKey(DWORD dwK0, DWORD dwK1);
static void s_vAppendByte(u8 b);	/* Add a single byte to the internal
					 * message */

/*---------------------  Export Variables  --------------------------*/
static DWORD  L, R;		/* Current state */
static DWORD  K0, K1;		/* Key */
static DWORD  M;		/* Message accumulator (single word) */
static unsigned int   nBytesInM;	/* # bytes in M */

/*---------------------  Export Functions  --------------------------*/

/*
static DWORD s_dwGetUINT32 (u8 * p)
// Convert from u8[] to DWORD in a portable way
{
	DWORD res = 0;
	unsigned int i;
	for (i = 0; i < 4; i++)
		res |= (*p++) << (8*i);
	return res;
}

static void s_vPutUINT32(u8 *p, DWORD val)
// Convert from DWORD to u8[] in a portable way
{
	unsigned int i;
	for (i = 0; i < 4; i++) {
		*p++ = (u8) (val & 0xff);
		val >>= 8;
	}
}
*/

static void s_vClear(void)
{
	/* Reset the state to the empty message. */
	L = K0;
	R = K1;
	nBytesInM = 0;
	M = 0;
}

static void s_vSetKey(DWORD dwK0, DWORD dwK1)
{
	/* Set the key */
	K0 = dwK0;
	K1 = dwK1;
	/* and reset the message */
	s_vClear();
}

static void s_vAppendByte(u8 b)
{
	/* Append the byte to our word-sized buffer */
	M |= b << (8*nBytesInM);
	nBytesInM++;
	/* Process the word if it is full. */
	if (nBytesInM >= 4) {
		L ^= M;
		R ^= ROL32(L, 17);
		L += R;
		R ^= ((L & 0xff00ff00) >> 8) | ((L & 0x00ff00ff) << 8);
		L += R;
		R ^= ROL32(L, 3);
		L += R;
		R ^= ROR32(L, 2);
		L += R;
		/* Clear the buffer */
		M = 0;
		nBytesInM = 0;
	}
}

void MIC_vInit(DWORD dwK0, DWORD dwK1)
{
	/* Set the key */
	s_vSetKey(dwK0, dwK1);
}


void MIC_vUnInit(void)
{
	/* Wipe the key material */
	K0 = 0;
	K1 = 0;

	/* And the other fields as well. */
	/* Note that this sets (L,R) to (K0,K1) which is just fine. */
	s_vClear();
}

void MIC_vAppend(u8 * src, unsigned int nBytes)
{
    /* This is simple */
	while (nBytes > 0) {
		s_vAppendByte(*src++);
		nBytes--;
	}
}

void MIC_vGetMIC(PDWORD pdwL, PDWORD pdwR)
{
	/* Append the minimum padding */
	s_vAppendByte(0x5a);
	s_vAppendByte(0);
	s_vAppendByte(0);
	s_vAppendByte(0);
	s_vAppendByte(0);
	/* and then zeroes until the length is a multiple of 4 */
	while (nBytesInM != 0)
		s_vAppendByte(0);
	/* The s_vAppendByte function has already computed the result. */
	*pdwL = L;
	*pdwR = R;
	/* Reset to the empty message. */
	s_vClear();
}
