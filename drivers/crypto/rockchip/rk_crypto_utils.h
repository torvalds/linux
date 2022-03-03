/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (c) 2022 Rockchip Electronics Co. Ltd. */

#ifndef __RK_CRYPTO_UTILS_H__
#define __RK_CRYPTO_UTILS_H__

void rk_crypto_write_regs(struct rk_crypto_dev *rk_dev, u32 base_addr, const u8 *data, u32 bytes);

void rk_crypto_clear_regs(struct rk_crypto_dev *rk_dev, u32 base_addr, u32 words);

void rk_crypto_read_regs(struct rk_crypto_dev *rk_dev, u32 base_addr, u8 *data, u32 bytes);

#endif

