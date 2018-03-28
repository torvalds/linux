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
#include <linux/bitops.h>
#include "michael_mic.h"

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
	A->l = A->k0;			\
	A->r = A->k1;			\
	A->m_bytes = 0;		\
} while (0)

static
void MichaelInitializeFunction(struct michael_mic_t *Mic, uint8_t *key)
{
	// Set the key
	Mic->k0 = getUInt32(key, 0);
	Mic->k1 = getUInt32(key, 4);

	//clear();
	MichaelClear(Mic);
}

#define MichaelBlockFunction(L, R)				\
do {								\
	R ^= rol32(L, 17);					\
	L += R;							\
	R ^= ((L & 0xff00ff00) >> 8) | ((L & 0x00ff00ff) << 8);	\
	L += R;							\
	R ^= rol32(L, 3);					\
	L += R;							\
	R ^= ror32(L, 2);					\
	L += R;							\
} while (0)

static
void MichaelAppend(struct michael_mic_t *Mic, uint8_t *src, int nBytes)
{
	int addlen;

	if (Mic->m_bytes) {
		addlen = 4 - Mic->m_bytes;
		if (addlen > nBytes)
			addlen = nBytes;
		memcpy(&Mic->m[Mic->m_bytes], src, addlen);
		Mic->m_bytes += addlen;
		src += addlen;
		nBytes -= addlen;

		if (Mic->m_bytes < 4)
			return;

		Mic->l ^= getUInt32(Mic->m, 0);
		MichaelBlockFunction(Mic->l, Mic->r);
		Mic->m_bytes = 0;
	}

	while (nBytes >= 4) {
		Mic->l ^= getUInt32(src, 0);
		MichaelBlockFunction(Mic->l, Mic->r);
		src += 4;
		nBytes -= 4;
	}

	if (nBytes > 0) {
		Mic->m_bytes = nBytes;
		memcpy(Mic->m, src, nBytes);
	}
}

static
void MichaelGetMIC(struct michael_mic_t *Mic, uint8_t *dst)
{
	u8 *data = Mic->m;

	switch (Mic->m_bytes) {
	case 0:
		Mic->l ^= 0x5a;
		break;
	case 1:
		Mic->l ^= data[0] | 0x5a00;
		break;
	case 2:
		Mic->l ^= data[0] | (data[1] << 8) | 0x5a0000;
		break;
	case 3:
		Mic->l ^= data[0] | (data[1] << 8) | (data[2] << 16) |
		    0x5a000000;
		break;
	}
	MichaelBlockFunction(Mic->l, Mic->r);
	MichaelBlockFunction(Mic->l, Mic->r);
	// The appendByte function has already computed the result.
	putUInt32(dst, 0, Mic->l);
	putUInt32(dst, 4, Mic->r);

	// Reset to the empty message.
	MichaelClear(Mic);
}

void michael_mic_function(struct michael_mic_t *mic, u8 *key,
			  u8 *data, int len, u8 priority, u8 *result)
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
	MichaelInitializeFunction(mic, key);
	MichaelAppend(mic, (uint8_t *)data, 12);	/* |DA|SA| */
	MichaelAppend(mic, pad_data, 4);	/* |Priority|0|0|0| */
	MichaelAppend(mic, (uint8_t *)(data + 12), len - 12);	/* |Data| */
	MichaelGetMIC(mic, result);
}
