/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright 2025 Google LLC */

#ifndef _RISCV_CRC_CLMUL_H
#define _RISCV_CRC_CLMUL_H

#include <linux/types.h>
#include "crc-clmul-consts.h"

u32 crc32_msb_clmul(u32 crc, const void *p, size_t len,
		    const struct crc_clmul_consts *consts);
u32 crc32_lsb_clmul(u32 crc, const void *p, size_t len,
		    const struct crc_clmul_consts *consts);

#endif /* _RISCV_CRC_CLMUL_H */
