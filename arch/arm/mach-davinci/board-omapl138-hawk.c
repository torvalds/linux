/*
 * Hawkboard.org based on TI's OMAP-L138 Platform
 *
 * Initial code: Syed Mohammed Khasim
 *
 * Copyright (C) 2009 Texas Instruments Incorporated - http://www.ti.com
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of
 * any kind, whether express or implied.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/console.h>
#include <linux/gpio.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>

#include <mach/cp_intc.h>
#include <mach/da8xx.h>

static struct davinci_uart_config omapl138_hawk_uart_config __initdata = {
	.enabled_uarts = 0x7,
};

static __init void omapl138_hawk_init(void)
{
	int ret;

	davinci_serial_init(&omapl138_hawk_uart_config);

	ret = da8xx_register_watchdog();
	if (ret)
		pr_warning("omapl138_hawk_init: "
			"watchdog registration failed: %d\n",
			ret);
}

#ifdef CONFIG_SERIAL_8250_CONSOLE
static int __init omapl138_hawk_console_init(void)
{
	if (!machine_is_omapl138_hawkboard())
		return 0;

	return add_preferred_console("ttyS", 2, "115200");
}
console_initcall(omapl138_hawk_console_init);
#endif

static void __init omapl138_hawk_map_io(void)
{
	da850_init();
}

MACHINE_START(OMAPL138_HAWKBOARD, "AM18x/OMAP-L138 Hawkboard")
	.boot_params	= (DA8XX_DDR_BASE + 0x100),
	.map_io		= omapl138_hawk_map_io,
	.init_irq	= cp_intc_init,
	.timer		= &davinci_timer,
	.init_machine	= omapl138_hawk_init,
MACHINE_END
