/*
 * arch/ppc/syslib/ibm440sp_common.c
 *
 * PPC440SP system library
 *
 * Matt Porter <mporter@kernel.crashing.org>
 * Copyright 2002-2005 MontaVista Software Inc.
 *
 * Eugene Surovegin <eugene.surovegin@zultys.com> or <ebs@ebshome.net>
 * Copyright (c) 2003, 2004 Zultys Technologies
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */
#include <linux/config.h>
#include <linux/types.h>
#include <linux/serial.h>

#include <asm/param.h>
#include <asm/ibm44x.h>
#include <asm/mmu.h>
#include <asm/machdep.h>
#include <asm/time.h>
#include <asm/ppc4xx_pic.h>

/*
 * Read the 440SP memory controller to get size of system memory.
 */
unsigned long __init ibm440sp_find_end_of_memory(void)
{
	u32 i;
	u32 mem_size = 0;

	/* Read two bank sizes and sum */
	for (i=0; i<2; i++)
		switch (mfdcr(DCRN_MQ0_BS0BAS + i) & MQ0_CONFIG_SIZE_MASK) {
			case MQ0_CONFIG_SIZE_8M:
				mem_size += PPC44x_MEM_SIZE_8M;
				break;
			case MQ0_CONFIG_SIZE_16M:
				mem_size += PPC44x_MEM_SIZE_16M;
				break;
			case MQ0_CONFIG_SIZE_32M:
				mem_size += PPC44x_MEM_SIZE_32M;
				break;
			case MQ0_CONFIG_SIZE_64M:
				mem_size += PPC44x_MEM_SIZE_64M;
				break;
			case MQ0_CONFIG_SIZE_128M:
				mem_size += PPC44x_MEM_SIZE_128M;
				break;
			case MQ0_CONFIG_SIZE_256M:
				mem_size += PPC44x_MEM_SIZE_256M;
				break;
			case MQ0_CONFIG_SIZE_512M:
				mem_size += PPC44x_MEM_SIZE_512M;
				break;
			case MQ0_CONFIG_SIZE_1G:
				mem_size += PPC44x_MEM_SIZE_1G;
				break;
			case MQ0_CONFIG_SIZE_2G:
				mem_size += PPC44x_MEM_SIZE_2G;
				break;
			default:
				break;
		}
	return mem_size;
}
