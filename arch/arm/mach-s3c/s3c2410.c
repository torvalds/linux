// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2003-2005 Simtec Electronics
//	Ben Dooks <ben@simtec.co.uk>
//
// http://www.simtec.co.uk/products/EB2410ITX/

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/timer.h>
#include <linux/init.h>
#include <linux/gpio.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/syscore_ops.h>
#include <linux/serial_core.h>
#include <linux/serial_s3c.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>
#include <linux/io.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include "map.h"
#include "gpio-samsung.h"
#include <asm/irq.h>
#include <asm/system_misc.h>


#include "regs-clock.h"

#include "cpu.h"
#include "devs.h"
#include "pm.h"

#include "gpio-core.h"
#include "gpio-cfg.h"
#include "gpio-cfg-helpers.h"

#include "s3c24xx.h"

/* Initial IO mappings */

static struct map_desc s3c2410_iodesc[] __initdata __maybe_unused = {
	IODESC_ENT(CLKPWR),
	IODESC_ENT(TIMER),
	IODESC_ENT(WATCHDOG),
};

/* our uart devices */

/* uart registration process */

void __init s3c2410_init_uarts(struct s3c2410_uartcfg *cfg, int no)
{
	s3c24xx_init_uartdevs("s3c2410-uart", s3c2410_uart_resources, cfg, no);
}

/* s3c2410_map_io
 *
 * register the standard cpu IO areas, and any passed in from the
 * machine specific initialisation.
*/

void __init s3c2410_map_io(void)
{
	s3c24xx_gpiocfg_default.set_pull = s3c24xx_gpio_setpull_1up;
	s3c24xx_gpiocfg_default.get_pull = s3c24xx_gpio_getpull_1up;

	iotable_init(s3c2410_iodesc, ARRAY_SIZE(s3c2410_iodesc));
}

struct bus_type s3c2410_subsys = {
	.name = "s3c2410-core",
	.dev_name = "s3c2410-core",
};

/* Note, we would have liked to name this s3c2410-core, but we cannot
 * register two subsystems with the same name.
 */
struct bus_type s3c2410a_subsys = {
	.name = "s3c2410a-core",
	.dev_name = "s3c2410a-core",
};

static struct device s3c2410_dev = {
	.bus		= &s3c2410_subsys,
};

/* need to register the subsystem before we actually register the device, and
 * we also need to ensure that it has been initialised before any of the
 * drivers even try to use it (even if not on an s3c2410 based system)
 * as a driver which may support both 2410 and 2440 may try and use it.
*/

static int __init s3c2410_core_init(void)
{
	return subsys_system_register(&s3c2410_subsys, NULL);
}

core_initcall(s3c2410_core_init);

static int __init s3c2410a_core_init(void)
{
	return subsys_system_register(&s3c2410a_subsys, NULL);
}

core_initcall(s3c2410a_core_init);

int __init s3c2410_init(void)
{
	printk("S3C2410: Initialising architecture\n");

#ifdef CONFIG_PM_SLEEP
	register_syscore_ops(&s3c2410_pm_syscore_ops);
	register_syscore_ops(&s3c24xx_irq_syscore_ops);
#endif

	return device_register(&s3c2410_dev);
}

int __init s3c2410a_init(void)
{
	s3c2410_dev.bus = &s3c2410a_subsys;
	return s3c2410_init();
}
