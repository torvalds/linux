/*
 * Texas Instruments TNETV107X SoC Specific Defines
 *
 * Copyright (C) 2010 Texas Instruments
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
#ifndef __ASM_ARCH_DAVINCI_TNETV107X_H
#define __ASM_ARCH_DAVINCI_TNETV107X_H

#include <asm/sizes.h>

#define TNETV107X_DDR_BASE	0x80000000

/*
 * Fixed mapping for early init starts here. If low-level debug is enabled,
 * this area also gets mapped via io_pg_offset and io_phys by the boot code.
 * To fit in with the io_pg_offset calculation, the io base address selected
 * here _must_ be a multiple of 2^20.
 */
#define TNETV107X_IO_BASE	0x08000000
#define TNETV107X_IO_VIRT	(IO_VIRT + SZ_1M)

#define TNETV107X_N_GPIO	65

#ifndef __ASSEMBLY__

#include <linux/serial_8250.h>
#include <linux/input/matrix_keypad.h>
#include <linux/mfd/ti_ssp.h>

#include <linux/platform_data/mmc-davinci.h>
#include <linux/platform_data/mtd-davinci.h>
#include <mach/serial.h>

struct tnetv107x_device_info {
	struct davinci_uart_config	*serial_config;
	struct davinci_mmc_config	*mmc_config[2];  /* 2 controllers */
	struct davinci_nand_pdata	*nand_config[4]; /* 4 chipsels */
	struct matrix_keypad_platform_data *keypad_config;
	struct ti_ssp_data		*ssp_config;
};

extern struct platform_device tnetv107x_wdt_device;
extern struct platform_device tnetv107x_serial_device;

extern void __init tnetv107x_init(void);
extern void __init tnetv107x_devices_init(struct tnetv107x_device_info *);
extern void __init tnetv107x_irq_init(void);
void tnetv107x_restart(char mode, const char *cmd);

#endif

#endif /* __ASM_ARCH_DAVINCI_TNETV107X_H */
