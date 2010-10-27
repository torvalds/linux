/*
 *  Copyright (C) 2009-2010, Lars-Peter Clausen <lars@metafoo.de>
 *  JZ4740 setup code
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under  the terms of the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <linux/init.h>
#include <linux/kernel.h>

#include "reset.h"

void __init plat_mem_setup(void)
{
	jz4740_reset_init();
}

const char *get_system_type(void)
{
	return "JZ4740";
}
