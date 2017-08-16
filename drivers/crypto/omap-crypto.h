/*
 * OMAP Crypto driver common support routines.
 *
 * Copyright (c) 2017 Texas Instruments Incorporated
 *   Tero Kristo <t-kristo@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#ifndef __CRYPTO_OMAP_CRYPTO_H
#define __CRYPTO_OMAP_CRYPTO_H

enum {
	OMAP_CRYPTO_NOT_ALIGNED = 1,
	OMAP_CRYPTO_BAD_DATA_LENGTH,
};

#define OMAP_CRYPTO_DATA_COPIED		BIT(0)
#define OMAP_CRYPTO_SG_COPIED		BIT(1)

#define OMAP_CRYPTO_COPY_MASK		0x3

#define OMAP_CRYPTO_COPY_DATA		BIT(0)
#define OMAP_CRYPTO_FORCE_COPY		BIT(1)
#define OMAP_CRYPTO_ZERO_BUF		BIT(2)
#define OMAP_CRYPTO_FORCE_SINGLE_ENTRY	BIT(3)

int omap_crypto_align_sg(struct scatterlist **sg, int total, int bs,
			 struct scatterlist *new_sg, u16 flags,
			 u8 flags_shift, unsigned long *dd_flags);
void omap_crypto_cleanup(struct scatterlist *sg, struct scatterlist *orig,
			 int offset, int len, u8 flags_shift,
			 unsigned long flags);

#endif
