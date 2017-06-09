/*
 *   Driver for KeyStream wireless LAN
 *
 *   Copyright (C) 2005-2008 KeyStream Corp.
 *   Copyright (C) 2009 Renesas Technology Corp.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License version 2 as
 *   published by the Free Software Foundation.
 */

#include <linux/types.h>
#include <linux/string.h>
#include "michael_mic.h"

// Rotation functions on 32 bit values
#define ROL32(A, n)	(((A) << (n)) | (((A) >> (32 - (n))) & ((1UL << (n)) - 1)))
#define ROR32(A, n)	ROL32((A), 32 - (n))
// Convert from Byte[] to UInt32 in a portable way
#define getUInt32(A, B)	((uint32_t)(A[B + 0] << 0) \
		+ (A[B + 1] << 8) + (A[B + 2] << 16) + (A[B + 3] << 24))

// Convert from UInt32 to Byte[] in a portable way
#define putUInt32(A, B, C)					\
do {								\
	A[B + 0] = (uint8_t)(C & 0xff);				\
	A[B + 1] = (uint8_t)((C >> 8) & 0xff);			\
	A[B + 2] = (uint8_t)((C >> 16) & 0xff);			\
	A[B + 3] = (uint8_t)((C >> 24) & 0xff);			\
} while (0)

// Reset the state to the empty message.
#define MichaelClear(A)			\
do {					\
	A->L = A->K0;			\
	A->R = A->K1;			\
	A->nBytesInM = 0;		\
} while (0)

static
void MichaelInitializeFunction(struct michael_mic_t *Mic, uint8_t *key)
{
	// Set the key
	Mic->K0 = getUInt32(key, 0);
	Mic->K1 = getUInt32(key, 4);

	//clear();
	MichaelClear(Mic);
}

#define MichaelBlockFunction(L, R)				\
do {								\
	R ^= ROL32(L, 17);					\
	L += R;							\
	R ^= ((L & 0xff00ff00) >> 8) | ((L & 0x00ff00ff) << 8);	\
	L += R;							\
	R ^= ROL32(L, 3);					\
	L += R;							\
	R ^= ROR32(L, 2);					\
	L += R;							\
} while (0)

static
void MichaelAppend(struct michael_mic_t *Mic, uint8_t *src, int nBytes)
{
	int addlen;

	if (Mic->nBytesInM) {
		addlen = 4 - Mic->nBytesInM;
		if (addlen > nBytes)
			addlen = nBytes;
		memcpy(&Mic->M[Mic->nBytesInM], src, addlen);
		Mic->nBytesInM += addlen;
		src += addlen;
		nBytes -= addlen;

		if (Mic->nBytesInM < 4)
			return;

		Mic->L ^= getUInt32(Mic->M, 0);
		MichaelBlockFunction(Mic->L, Mic->R);
		Mic->nBytesInM = 0;
	}

	while (nBytes >= 4) {
		Mic->L ^= getUInt32(src, 0);
		MichaelBlockFunction(Mic->L, Mic->R);
		src += 4;
		nBytes -= 4;
	}

	if (nBytes > 0) {
		Mic->nBytesInM = nBytes;
		memcpy(Mic->M, src, nBytes);
	}
}

static
void MichaelGetMIC(struct michael_mic_t *Mic, uint8_t *dst)
{
	u8 *data = Mic->M;

	switch (Mic->nBytesInM) {
	case 0:
		Mic->L ^= 0x5a;
		break;
	case 1:
		Mic->L ^= data[0] | 0x5a00;
		break;
	case 2:
		Mic->L ^= data[0] | (data[1] << 8) | 0x5a0000;
		break;
	case 3:
		Mic->L ^= data[0] | (data[1] << 8) | (data[2] << 16) |
		    0x5a000000;
		break;
	}
	MichaelBlockFunction(Mic->L, Mic->R);
	MichaelBlockFunction(Mic->L, Mic->R);
	// The appendByte function has already computed the result.
	putUInt32(dst, 0, Mic->L);
	putUInt32(dst, 4, Mic->R);

	// Reset to the empty message.
	MichaelClear(Mic);
}

void MichaelMICFunction(struct michael_mic_t *Mic, u8 *Key,
			u8 *Data, int Len, u8 priority,
			u8 *Result)
{
	u8 pad_data[4] = { priority, 0, 0, 0 };
	// Compute the MIC value
	/*
	 * IEEE802.11i  page 47
	 * Figure 43g TKIP MIC processing format
	 * +--+--+--------+--+----+--+--+--+--+--+--+--+--+
	 * |6 |6 |1       |3 |M   |1 |1 |1 |1 |1 |1 |1 |1 | Octet
	 * +--+--+--------+--+----+--+--+--+--+--+--+--+--+
	 * |DA|SA|Priority|0 |Data|M0|M1|M2|M3|M4|M5|M6|M7|
	 * +--+--+--------+--+----+--+--+--+--+--+--+--+--+
	 */
	MichaelInitializeFunction(Mic, Key);
	MichaelAppend(Mic, (uint8_t *)Data, 12);	/* |DA|SA| */
	MichaelAppend(Mic, pad_data, 4);	/* |Priority|0|0|0| */
	MichaelAppend(Mic, (uint8_t *)(Data + 12), Len - 12);	/* |Data| */
	MichaelGetMIC(Mic, Result);
}
