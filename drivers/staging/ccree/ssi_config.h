/*
 * Copyright (C) 2012-2017 ARM Limited or its affiliates.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

/* \file ssi_config.h
 * Definitions for ARM CryptoCell Linux Crypto Driver
 */

#ifndef __SSI_CONFIG_H__
#define __SSI_CONFIG_H__

#include <linux/version.h>

//#define FLUSH_CACHE_ALL
//#define COMPLETION_DELAY
//#define DX_DUMP_DESCS
// #define DX_DUMP_BYTES
// #define CC_DEBUG
#define ENABLE_CC_SYSFS		/* Enable sysfs interface for debugging REE driver */
//#define DX_IRQ_DELAY 100000
#define DMA_BIT_MASK_LEN	48	/* was 32 bit, but for juno's sake it was enlarged to 48 bit */

#endif /*__DX_CONFIG_H__*/

