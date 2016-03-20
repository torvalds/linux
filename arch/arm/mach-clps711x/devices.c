/*
 *  CLPS711X common devices definitions
 *
 *  Author: Alexander Shiyan <shc_work@mail.ru>, 2013-2014
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/io.h>
#include <linux/of_fdt.h>
#include <linux/platform_device.h>
#include <linux/random.h>
#include <linux/sizes.h>
#include <linux/slab.h>
#include <linux/sys_soc.h>

#include <asm/system_info.h>

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

static void __init clps711x_soc_init(void)
{
	struct soc_device_attribute *soc_dev_attr;
	struct soc_device *soc_dev;
	void __iomem *base;
	u32 id[5];

	base = ioremap(CLPS711X_PHYS_BASE, SZ_32K);
	if (!base)
		return;

	id[0] = readl(base + UNIQID);
	id[1] = readl(base + RANDID0);
	id[2] = readl(base + RANDID1);
	id[3] = readl(base + RANDID2);
	id[4] = readl(base + RANDID3);
	system_rev = SYSFLG1_VERID(readl(base + SYSFLG1));

	add_device_randomness(id, sizeof(id));

	system_serial_low = id[0];

	soc_dev_attr = kzalloc(sizeof(*soc_dev_attr), GFP_KERNEL);
	if (!soc_dev_attr)
		goto out_unmap;

	soc_dev_attr->machine = of_flat_dt_get_machine_name();
	soc_dev_attr->family = "Cirrus Logic CLPS711X";
	soc_dev_attr->revision = kasprintf(GFP_KERNEL, "%u", system_rev);
	soc_dev_attr->soc_id = kasprintf(GFP_KERNEL, "%08x", id[0]);

	soc_dev = soc_device_register(soc_dev_attr);
	if (IS_ERR(soc_dev)) {
		kfree(soc_dev_attr->revision);
		kfree(soc_dev_attr->soc_id);
		kfree(soc_dev_attr);
	}

out_unmap:
	iounmap(base);
}

void __init clps711x_devices_init(void)
{
	clps711x_add_cpuidle();
	clps711x_add_gpio();
	clps711x_add_syscon();
	clps711x_add_uart();
	clps711x_soc_init();
}
