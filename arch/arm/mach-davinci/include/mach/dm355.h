/*
 * Chip specific defines for DM355 SoC
 *
 * Author: Kevin Hilman, Deep Root Systems, LLC
 *
 * 2007 (c) Deep Root Systems, LLC. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */
#ifndef __ASM_ARCH_DM355_H
#define __ASM_ARCH_DM355_H

#include <mach/hardware.h>

struct spi_board_info;

void __init dm355_init(void);
void dm355_init_spi0(unsigned chipselect_mask,
		struct spi_board_info *info, unsigned len);

#endif /* __ASM_ARCH_DM355_H */
