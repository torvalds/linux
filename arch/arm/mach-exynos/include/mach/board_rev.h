/* linux/arch/arm/mach-exynos/include/mach/board_rev.h
 *
 * Copyright (c) 2010-2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * EXYNOS - board revision support header
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ASM_ARCH_BOARD_REV_H
#define __ASM_ARCH_BOARD_REV_H __FILE__

enum {
	SAMSUNG_BOARD_REV_0_0 = 0x0000,
	SAMSUNG_BOARD_REV_0_1 = 0x0001
};

extern int samsung_board_rev;

#define samsung_board_rev_is_0_0() (samsung_board_rev == SAMSUNG_BOARD_REV_0_0)
#define samsung_board_rev_is_0_1() (samsung_board_rev == SAMSUNG_BOARD_REV_0_1)

#endif /* __ASM_ARCH_BOARD_REV_H */
