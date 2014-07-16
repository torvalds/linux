/* linux/arch/arm/mach-s5pv210/setup-ide.c
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * S5PV210 setup information for IDE
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/kernel.h>

#include <plat/gpio-cfg.h>
#include <mach/gpio-samsung.h>

static void s5pv210_ide_cfg_gpios(unsigned int base, unsigned int nr)
{
	s3c_gpio_cfgrange_nopull(base, nr, S3C_GPIO_SFN(4));

	for (; nr > 0; nr--, base++)
		s5p_gpio_set_drvstr(base, S5P_GPIO_DRVSTR_LV4);
}

void s5pv210_ide_setup_gpio(void)
{
	/* CF_Add[0 - 2], CF_IORDY, CF_INTRQ, CF_DMARQ, CF_DMARST, CF_DMACK */
	s5pv210_ide_cfg_gpios(S5PV210_GPJ0(0), 8);

	/* CF_Data[0 - 7] */
	s5pv210_ide_cfg_gpios(S5PV210_GPJ2(0), 8);

	/* CF_Data[8 - 15] */
	s5pv210_ide_cfg_gpios(S5PV210_GPJ3(0), 8);

	/* CF_CS0, CF_CS1, CF_IORD, CF_IOWR */
	s5pv210_ide_cfg_gpios(S5PV210_GPJ4(0), 4);
}
