// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (C) 2010, Lars-Peter Clausen <lars@metafoo.de>
 */

#include <asm/reboot.h>

#include "reset.h"

static void jz4740_halt(void)
{
	while (1) {
		__asm__(".set push;\n"
			".set mips3;\n"
			"wait;\n"
			".set pop;\n"
		);
	}
}

void jz4740_reset_init(void)
{
	_machine_halt = jz4740_halt;
}
