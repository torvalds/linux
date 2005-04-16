/* 
 * linux/arch/sh/board/adx/setup.c
 *
 * Copyright (C) 2001 A&D Co., Ltd.
 *
 * I/O routine and setup routines for A&D ADX Board
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 */

#include <asm/machvec.h>
#include <linux/module.h>

extern void init_adx_IRQ(void);
extern void *cf_io_base;

const char *get_system_type(void)
{
	return "A&D ADX";
}

unsigned long adx_isa_port2addr(unsigned long offset)
{
	/* CompactFlash (IDE) */
	if (((offset >= 0x1f0) && (offset <= 0x1f7)) || (offset == 0x3f6)) {
		return (unsigned long)cf_io_base + offset;
	}

	/* eth0 */
	if ((offset >= 0x300) && (offset <= 0x30f)) {
		return 0xa5000000 + offset;	/* COMM BOARD (AREA1) */
	}

	return offset + 0xb0000000; /* IOBUS (AREA 4)*/
}

/*
 * The Machine Vector
 */

struct sh_machine_vector mv_adx __initmv = {
	.mv_nr_irqs		= 48,
	.mv_isa_port2addr	= adx_isa_port2addr,
	.mv_init_irq		= init_adx_IRQ,
};
ALIAS_MV(adx)

int __init platform_setup(void)
{
	/* Nothing to see here .. */
	return 0;
}

