// SPDX-License-Identifier: GPL-2.0
/*
 * Rockchip crypto uitls
 *
 * Copyright (c) 2022, Rockchip Electronics Co., Ltd
 *
 * Author: Lin Jinhan <troy.lin@rock-chips.com>
 *
 */

#include "rk_crypto_core.h"
#include "rk_crypto_utils.h"

static inline void word2byte_be(u32 word, u8 *ch)
{
	ch[0] = (word >> 24) & 0xff;
	ch[1] = (word >> 16) & 0xff;
	ch[2] = (word >> 8) & 0xff;
	ch[3] = (word >> 0) & 0xff;
}

static inline u32 byte2word_be(const u8 *ch)
{
	return (*ch << 24) + (*(ch + 1) << 16) +
	       (*(ch + 2) << 8) + *(ch + 3);
}

void rk_crypto_write_regs(struct rk_crypto_dev *rk_dev, u32 base_addr, const u8 *data, u32 bytes)
{
	u32 i;
	u8 tmp_buf[4];

	for (i = 0; i < bytes / 4; i++, base_addr += 4)
		CRYPTO_WRITE(rk_dev, base_addr, byte2word_be(data + i * 4));

	if (bytes % 4) {
		memset(tmp_buf, 0x00, sizeof(tmp_buf));
		memcpy((u8 *)tmp_buf, data + (bytes / 4) * 4, bytes % 4);
		CRYPTO_WRITE(rk_dev, base_addr, byte2word_be(tmp_buf));
	}
}

void rk_crypto_clear_regs(struct rk_crypto_dev *rk_dev, u32 base_addr, u32 words)
{
	u32 i;

	for (i = 0; i < words; i++, base_addr += 4)
		CRYPTO_WRITE(rk_dev, base_addr, 0);
}

void rk_crypto_read_regs(struct rk_crypto_dev *rk_dev, u32 base_addr, u8 *data, u32 bytes)
{
	u32 i;

	for (i = 0; i < bytes / 4; i++, base_addr += 4)
		word2byte_be(CRYPTO_READ(rk_dev, base_addr), data + i * 4);

	if (bytes % 4) {
		uint8_t tmp_buf[4];

		word2byte_be(CRYPTO_READ(rk_dev, base_addr), tmp_buf);
		memcpy(data + i * 4, tmp_buf, bytes % 4);
	}
}
