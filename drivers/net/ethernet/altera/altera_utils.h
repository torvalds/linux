/* SPDX-License-Identifier: GPL-2.0-only */
/* Altera TSE SGDMA and MSGDMA Linux driver
 * Copyright (C) 2014 Altera Corporation. All rights reserved
 */

#ifndef __ALTERA_UTILS_H__
#define __ALTERA_UTILS_H__

#include <linux/compiler.h>
#include <linux/types.h>

void tse_set_bit(void __iomem *ioaddr, size_t offs, u32 bit_mask);
void tse_clear_bit(void __iomem *ioaddr, size_t offs, u32 bit_mask);
int tse_bit_is_set(void __iomem *ioaddr, size_t offs, u32 bit_mask);
int tse_bit_is_clear(void __iomem *ioaddr, size_t offs, u32 bit_mask);

#endif /* __ALTERA_UTILS_H__*/
