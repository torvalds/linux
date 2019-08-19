// SPDX-License-Identifier: GPL-2.0-only
/*
 * restart.c
 *
 * Copyright (C) 2001 MontaVista Software, Inc.
 */
#include <asm/hardware/iop3xx.h>
#include <asm/system_misc.h>
#include <mach/hardware.h>

void iop3xx_restart(enum reboot_mode mode, const char *cmd)
{
	*IOP3XX_PCSR = 0x30;

	/* Jump into ROM at address 0 */
	soft_restart(0);
}
