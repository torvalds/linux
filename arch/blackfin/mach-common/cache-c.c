/*
 * Blackfin cache control code (simpler control-style functions)
 *
 * Copyright 2004-2008 Analog Devices Inc.
 *
 * Enter bugs at http://blackfin.uclinux.org/
 *
 * Licensed under the GPL-2 or later.
 */

#include <asm/blackfin.h>

/* Invalidate the Entire Data cache by
 * clearing DMC[1:0] bits
 */
void blackfin_invalidate_entire_dcache(void)
{
	u32 dmem = bfin_read_DMEM_CONTROL();
	bfin_write_DMEM_CONTROL(dmem & ~0xc);
	SSYNC();
	bfin_write_DMEM_CONTROL(dmem);
	SSYNC();
}

/* Invalidate the Entire Instruction cache by
 * clearing IMC bit
 */
void blackfin_invalidate_entire_icache(void)
{
	u32 imem = bfin_read_IMEM_CONTROL();
	bfin_write_IMEM_CONTROL(imem & ~0x4);
	SSYNC();
	bfin_write_IMEM_CONTROL(imem);
	SSYNC();
}

