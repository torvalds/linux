/*
 *  Copyright (C) 2001-2006 Storlink, Corp.
 *  Copyright (C) 2008-2009 Paulius Zaleckas <paulius.zaleckas@teltonika.lt>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifndef __MACH_SYSTEM_H
#define __MACH_SYSTEM_H

#include <linux/io.h>
#include <mach/hardware.h>
#include <mach/global_reg.h>

#include "common.h"

void gemini_restart(enum reboot_mode mode, const char *cmd)
{
	__raw_writel(RESET_GLOBAL | RESET_CPU1,
		     IO_ADDRESS(GEMINI_GLOBAL_BASE) + GLOBAL_RESET);
}

#endif /* __MACH_SYSTEM_H */
