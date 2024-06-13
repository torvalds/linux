// SPDX-License-Identifier: GPL-2.0-only
/*
 * linux/arch/arm/mach-omap1/board-nand.c
 *
 * Common OMAP1 board NAND code
 *
 * Copyright (C) 2004, 2012 Texas Instruments, Inc.
 * Copyright (C) 2002 MontaVista Software, Inc.
 * Copyright (C) 2001 RidgeRun, Inc.
 * Author: RidgeRun, Inc.
 *         Greg Lonnon (glonnon@ridgerun.com) or info@ridgerun.com
 */
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/rawnand.h>

#include "common.h"

void omap1_nand_cmd_ctl(struct nand_chip *this, int cmd, unsigned int ctrl)
{
	unsigned long mask;

	if (cmd == NAND_CMD_NONE)
		return;

	mask = (ctrl & NAND_CLE) ? 0x02 : 0;
	if (ctrl & NAND_ALE)
		mask |= 0x04;

	writeb(cmd, this->legacy.IO_ADDR_W + mask);
}

