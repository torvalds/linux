/*
 * Copyright (C) 2011 Jean-Christophe PLAGNIOL-VILLARD <plagnioj@jcrosoft.com>
 *
 * Under GPLv2 only
 */

#ifndef __ARCH_SYSTEM_REV_H__
#define __ARCH_SYSTEM_REV_H__

/*
 * board revision encoding
 * mach specific
 * the 16-31 bit are reserved for at91 generic information
 *
 * bit 31:
 *	0 => nand 16 bit
 *	1 => nand 8 bit
 */
#define BOARD_HAVE_NAND_8BIT	(1 << 31)
static int inline board_have_nand_8bit(void)
{
	return system_rev & BOARD_HAVE_NAND_8BIT;
}

#endif /* __ARCH_SYSTEM_REV_H__ */
