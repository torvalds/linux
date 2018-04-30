/*
 * TI AM33XX EMIF PM Assembly Offsets
 *
 * Copyright (C) 2016-2017 Texas Instruments Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/ti-emif-sram.h>

int main(void)
{
	ti_emif_asm_offsets();

	return 0;
}
