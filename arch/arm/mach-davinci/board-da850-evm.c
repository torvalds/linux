/*
 * TI DA850/OMAP-L138 EVM board
 *
 * Copyright (C) 2009 Texas Instruments Incorporated - http://www.ti.com/
 *
 * Derived from: arch/arm/mach-davinci/board-da830-evm.c
 * Original Copyrights follow:
 *
 * 2007, 2009 (c) MontaVista Software, Inc. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/console.h>
#include <linux/i2c.h>
#include <linux/i2c/at24.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>

#include <mach/common.h>
#include <mach/irqs.h>
#include <mach/cp_intc.h>
#include <mach/da8xx.h>

static struct davinci_i2c_platform_data da850_evm_i2c_0_pdata = {
	.bus_freq	= 100,	/* kHz */
	.bus_delay	= 0,	/* usec */
};

static struct davinci_uart_config da850_evm_uart_config __initdata = {
	.enabled_uarts = 0x7,
};

static __init void da850_evm_init(void)
{
	int ret;

	ret = da8xx_register_edma();
	if (ret)
		pr_warning("da850_evm_init: edma registration failed: %d\n",
				ret);

	ret = da8xx_pinmux_setup(da850_i2c0_pins);
	if (ret)
		pr_warning("da850_evm_init: i2c0 mux setup failed: %d\n",
				ret);

	ret = da8xx_register_i2c(0, &da850_evm_i2c_0_pdata);
	if (ret)
		pr_warning("da850_evm_init: i2c0 registration failed: %d\n",
				ret);

	ret = da8xx_register_watchdog();
	if (ret)
		pr_warning("da830_evm_init: watchdog registration failed: %d\n",
				ret);

	davinci_serial_init(&da850_evm_uart_config);

	/*
	 * shut down uart 0 and 1; they are not used on the board and
	 * accessing them causes endless "too much work in irq53" messages
	 * with arago fs
	 */
	__raw_writel(0, IO_ADDRESS(DA8XX_UART1_BASE) + 0x30);
	__raw_writel(0, IO_ADDRESS(DA8XX_UART0_BASE) + 0x30);
}

#ifdef CONFIG_SERIAL_8250_CONSOLE
static int __init da850_evm_console_init(void)
{
	return add_preferred_console("ttyS", 2, "115200");
}
console_initcall(da850_evm_console_init);
#endif

static __init void da850_evm_irq_init(void)
{
	struct davinci_soc_info *soc_info = &davinci_soc_info;

	cp_intc_init((void __iomem *)DA8XX_CP_INTC_VIRT, DA850_N_CP_INTC_IRQ,
			soc_info->intc_irq_prios);
}

static void __init da850_evm_map_io(void)
{
	da850_init();
}

MACHINE_START(DAVINCI_DA850_EVM, "DaVinci DA850/OMAP-L138 EVM")
	.phys_io	= IO_PHYS,
	.io_pg_offst	= (__IO_ADDRESS(IO_PHYS) >> 18) & 0xfffc,
	.boot_params	= (DA8XX_DDR_BASE + 0x100),
	.map_io		= da850_evm_map_io,
	.init_irq	= da850_evm_irq_init,
	.timer		= &davinci_timer,
	.init_machine	= da850_evm_init,
MACHINE_END
