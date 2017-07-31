/*
 * Code specific to PKUnity SoC and UniCore ISA
 *
 *	Maintained by GUAN Xue-tao <gxt@mprc.pku.edu.cn>
 *	Copyright (C) 2001-2011 Guan Xuetao
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef _I8042_UNICORE32_H
#define _I8042_UNICORE32_H

#include <mach/hardware.h>

/*
 * Names.
 */
#define I8042_KBD_PHYS_DESC "isa0060/serio0"
#define I8042_AUX_PHYS_DESC "isa0060/serio1"
#define I8042_MUX_PHYS_DESC "isa0060/serio%d"

/*
 * IRQs.
 */
#define I8042_KBD_IRQ           IRQ_PS2_KBD
#define I8042_AUX_IRQ           IRQ_PS2_AUX

/*
 * Register numbers.
 */
#define I8042_COMMAND_REG	PS2_COMMAND
#define I8042_STATUS_REG	PS2_STATUS
#define I8042_DATA_REG		PS2_DATA

#define I8042_REGION_START	(resource_size_t)(PS2_DATA)
#define I8042_REGION_SIZE	(resource_size_t)(16)

static inline int i8042_read_data(void)
{
	return readb(I8042_DATA_REG);
}

static inline int i8042_read_status(void)
{
	return readb(I8042_STATUS_REG);
}

static inline void i8042_write_data(int val)
{
	writeb(val, I8042_DATA_REG);
}

static inline void i8042_write_command(int val)
{
	writeb(val, I8042_COMMAND_REG);
}

static inline int i8042_platform_init(void)
{
	if (!request_mem_region(I8042_REGION_START, I8042_REGION_SIZE, "i8042"))
		return -EBUSY;

	i8042_reset = I8042_RESET_ALWAYS;
	return 0;
}

static inline void i8042_platform_exit(void)
{
	release_mem_region(I8042_REGION_START, I8042_REGION_SIZE);
}

#endif /* _I8042_UNICORE32_H */
