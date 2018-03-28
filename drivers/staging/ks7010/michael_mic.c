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
#include <asm/unaligned.h>
#include "michael_mic.h"


// Reset the state to the empty message.
static inline void michael_clear(struct michael_mic_t *mic)
{
	mic->l = mic->k0;
	mic->r = mic->k1;
	mic->m_bytes = 0;
}

static void michael_init(struct michael_mic_t *mic, u8 *key)
{
	// Set the key
	mic->k0 = get_unaligned_le32(key);
	mic->k1 = get_unaligned_le32(key + 4);

	//clear();
	michael_clear(mic);
}

static inline void michael_block(struct michael_mic_t *mic)
{
	mic->r ^= rol32(mic->l, 17);
	mic->l += mic->r;
	mic->r ^= ((mic->l & 0xff00ff00) >> 8) |
		  ((mic->l & 0x00ff00ff) << 8);
	mic->l += mic->r;
	mic->r ^= rol32(mic->l, 3);					\
	mic->l += mic->r;
	mic->r ^= ror32(mic->l, 2);					\
	mic->l += mic->r;
}

static void michael_append(struct michael_mic_t *mic, uint8_t *src, int bytes)
{
	int addlen;

	if (mic->m_bytes) {
		addlen = 4 - mic->m_bytes;
		if (addlen > bytes)
			addlen = bytes;
		memcpy(&mic->m[mic->m_bytes], src, addlen);
		mic->m_bytes += addlen;
		src += addlen;
		bytes -= addlen;

		if (mic->m_bytes < 4)
			return;

		mic->l ^= get_unaligned_le32(mic->m);
		michael_block(mic);
		mic->m_bytes = 0;
	}

	while (bytes >= 4) {
		mic->l ^= get_unaligned_le32(src);
		michael_block(mic);
		src += 4;
		bytes -= 4;
	}

	if (bytes > 0) {
		mic->m_bytes = bytes;
		memcpy(mic->m, src, bytes);
	}
}

static void michael_get_mic(struct michael_mic_t *mic, uint8_t *dst)
{
	u8 *data = mic->m;

	switch (mic->m_bytes) {
	case 0:
		mic->l ^= 0x5a;
		break;
	case 1:
		mic->l ^= data[0] | 0x5a00;
		break;
	case 2:
		mic->l ^= data[0] | (data[1] << 8) | 0x5a0000;
		break;
	case 3:
		mic->l ^= data[0] | (data[1] << 8) | (data[2] << 16) |
		    0x5a000000;
		break;
	}
	michael_block(mic);
	michael_block(mic);
	// The appendByte function has already computed the result.
	put_unaligned_le32(mic->l, dst);
	put_unaligned_le32(mic->r, dst + 4);

	// Reset to the empty message.
	michael_clear(mic);
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
	michael_init(mic, key);
	michael_append(mic, (uint8_t *)data, 12);	/* |DA|SA| */
	michael_append(mic, pad_data, 4);	/* |Priority|0|0|0| */
	michael_append(mic, (uint8_t *)(data + 12), len - 12);	/* |Data| */
	michael_get_mic(mic, result);
}
