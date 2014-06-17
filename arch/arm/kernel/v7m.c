/*
 * Copyright (C) 2013 Uwe Kleine-Koenig for Pengutronix
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation.
 */
#include <linux/io.h>
#include <linux/reboot.h>
#include <asm/barrier.h>
#include <asm/v7m.h>

void armv7m_restart(enum reboot_mode mode, const char *cmd)
{
	dsb();
	__raw_writel(V7M_SCB_AIRCR_VECTKEY | V7M_SCB_AIRCR_SYSRESETREQ,
			BASEADDR_V7M_SCB + V7M_SCB_AIRCR);
	dsb();
}
