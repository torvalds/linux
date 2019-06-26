/* SPDX-License-Identifier: GPL-2.0-or-later */
/**
 * AMCC SoC PPC4xx Crypto Driver
 *
 * Copyright (c) 2008 Applied Micro Circuits Corporation.
 * All rights reserved. James Hsiao <jhsiao@amcc.com>
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
	struct crypto4xx_core_device *dev __maybe_unused) { }
static inline void ppc4xx_trng_remove(
	struct crypto4xx_core_device *dev __maybe_unused) { }
#endif

#endif
