/*
 *  CLPS711X common devices definitions
 *
 *  Author: Alexander Shiyan <shc_work@mail.ru>, 2013
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/platform_device.h>
#include <linux/sizes.h>

#include <mach/hardware.h>

static const struct resource clps711x_cpuidle_res __initconst =
	DEFINE_RES_MEM(CLPS711X_PHYS_BASE + HALT, SZ_128);

static void __init clps711x_add_cpuidle(void)
{
	platform_device_register_simple("clps711x-cpuidle", PLATFORM_DEVID_NONE,
					&clps711x_cpuidle_res, 1);
}

static const phys_addr_t clps711x_gpios[][2] __initconst = {
	{ PADR, PADDR },
	{ PBDR, PBDDR },
	{ PCDR, PCDDR },
	{ PDDR, PDDDR },
	{ PEDR, PEDDR },
};

static void __init clps711x_add_gpio(void)
{
	unsigned i;
	struct resource gpio_res[2];

	memset(gpio_res, 0, sizeof(gpio_res));

	gpio_res[0].flags = IORESOURCE_MEM;
	gpio_res[1].flags = IORESOURCE_MEM;

	for (i = 0; i < ARRAY_SIZE(clps711x_gpios); i++) {
		gpio_res[0].start = CLPS711X_PHYS_BASE + clps711x_gpios[i][0];
		gpio_res[0].end = gpio_res[0].start;
		gpio_res[1].start = CLPS711X_PHYS_BASE + clps711x_gpios[i][1];
		gpio_res[1].end = gpio_res[1].start;

		platform_device_register_simple("clps711x-gpio", i,
						gpio_res, ARRAY_SIZE(gpio_res));
	}
}

const struct resource clps711x_syscon_res[] __initconst = {
	/* SYSCON1, SYSFLG1 */
	DEFINE_RES_MEM(CLPS711X_PHYS_BASE + SYSCON1, SZ_128),
	/* SYSCON2, SYSFLG2 */
	DEFINE_RES_MEM(CLPS711X_PHYS_BASE + SYSCON2, SZ_128),
	/* SYSCON3 */
	DEFINE_RES_MEM(CLPS711X_PHYS_BASE + SYSCON3, SZ_64),
};

static void __init clps711x_add_syscon(void)
{
	unsigned i;

	for (i = 0; i < ARRAY_SIZE(clps711x_syscon_res); i++)
		platform_device_register_simple("syscon", i + 1,
						&clps711x_syscon_res[i], 1);
}

static const struct resource clps711x_uart1_res[] __initconst = {
	DEFINE_RES_MEM(CLPS711X_PHYS_BASE + UARTDR1, SZ_128),
	DEFINE_RES_IRQ(IRQ_UTXINT1),
	DEFINE_RES_IRQ(IRQ_URXINT1),
};

static const struct resource clps711x_uart2_res[] __initconst = {
	DEFINE_RES_MEM(CLPS711X_PHYS_BASE + UARTDR2, SZ_128),
	DEFINE_RES_IRQ(IRQ_UTXINT2),
	DEFINE_RES_IRQ(IRQ_URXINT2),
};

static void __init clps711x_add_uart(void)
{
	platform_device_register_simple("clps711x-uart", 0, clps711x_uart1_res,
					ARRAY_SIZE(clps711x_uart1_res));
	platform_device_register_simple("clps711x-uart", 1, clps711x_uart2_res,
					ARRAY_SIZE(clps711x_uart2_res));
};

void __init clps711x_devices_init(void)
{
	clps711x_add_cpuidle();
	clps711x_add_gpio();
	clps711x_add_syscon();
	clps711x_add_uart();
}
