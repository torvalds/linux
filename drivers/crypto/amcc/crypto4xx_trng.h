/**
 * AMCC SoC PPC4xx Crypto Driver
 *
 * Copyright (c) 2008 Applied Micro Circuits Corporation.
 * All rights reserved. James Hsiao <jhsiao@amcc.com>
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
 * This file defines the security context
 * associate format.
 */

#ifndef __CRYPTO4XX_TRNG_H__
#define __CRYPTO4XX_TRNG_H__

#ifdef CONFIG_HW_RANDOM_PPC4XX
void ppc4xx_trng_probe(struct crypto4xx_core_device *core_dev);
void ppc4xx_trng_remove(struct crypto4xx_core_device *core_dev);
#else
static inline void ppc4xx_trng_probe(
	struct crypto4xx_device *dev __maybe_unused) { }
static inline void ppc4xx_trng_remove(
	struct crypto4xx_device *dev __maybe_unused) { }
#endif

#endif
