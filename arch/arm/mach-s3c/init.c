// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2008 Simtec Electronics
//	Ben Dooks <ben@simtec.co.uk>
//	http://armlinux.simtec.co.uk/
//
// S3C series CPU initialisation

/*
 * NOTE: Code in this file is not used on S3C64xx when booting with
 * Device Tree support.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/serial_core.h>
#include <linux/serial_s3c.h>
#include <linux/platform_device.h>
#include <linux/of.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include "cpu.h"
#include "devs.h"

static struct cpu_table *cpu;

static struct cpu_table * __init s3c_lookup_cpu(unsigned long idcode,
						struct cpu_table *tab,
						unsigned int count)
{
	for (; count != 0; count--, tab++) {
		if ((idcode & tab->idmask) == (tab->idcode & tab->idmask))
			return tab;
	}

	return NULL;
}

void __init s3c_init_cpu(unsigned long idcode,
			 struct cpu_table *cputab, unsigned int cputab_size)
{
	cpu = s3c_lookup_cpu(idcode, cputab, cputab_size);

	if (cpu == NULL) {
		printk(KERN_ERR "Unknown CPU type 0x%08lx\n", idcode);
		panic("Unknown S3C24XX CPU");
	}

	printk("CPU %s (id 0x%08lx)\n", cpu->name, idcode);

	if (cpu->init == NULL) {
		printk(KERN_ERR "CPU %s support not enabled\n", cpu->name);
		panic("Unsupported Samsung CPU");
	}

	if (cpu->map_io)
		cpu->map_io();

	pr_err("The platform is deprecated and scheduled for removal. Please reach to the maintainers of the platform and linux-samsung-soc@vger.kernel.org if you still use it.  Without such feedback, the platform will be removed after 2022.\n");
}

/* s3c24xx_init_clocks
 *
 * Initialise the clock subsystem and associated information from the
 * given master crystal value.
 *
 * xtal  = 0 -> use default PLL crystal value (normally 12MHz)
 *      != 0 -> PLL crystal value in Hz
*/

void __init s3c24xx_init_clocks(int xtal)
{
	if (xtal == 0)
		xtal = 12*1000*1000;

	if (cpu == NULL)
		panic("s3c24xx_init_clocks: no cpu setup?\n");

	if (cpu->init_clocks == NULL)
		panic("s3c24xx_init_clocks: cpu has no clock init\n");
	else
		(cpu->init_clocks)(xtal);
}

/* uart management */
#if IS_ENABLED(CONFIG_SAMSUNG_ATAGS)
static int nr_uarts __initdata = 0;

#ifdef CONFIG_SERIAL_SAMSUNG_UARTS
static struct s3c2410_uartcfg uart_cfgs[CONFIG_SERIAL_SAMSUNG_UARTS];
#endif

/* s3c24xx_init_uartdevs
 *
 * copy the specified platform data and configuration into our central
 * set of devices, before the data is thrown away after the init process.
 *
 * This also fills in the array passed to the serial driver for the
 * early initialisation of the console.
*/

void __init s3c24xx_init_uartdevs(char *name,
				  struct s3c24xx_uart_resources *res,
				  struct s3c2410_uartcfg *cfg, int no)
{
#ifdef CONFIG_SERIAL_SAMSUNG_UARTS
	struct platform_device *platdev;
	struct s3c2410_uartcfg *cfgptr = uart_cfgs;
	struct s3c24xx_uart_resources *resp;
	int uart;

	memcpy(cfgptr, cfg, sizeof(struct s3c2410_uartcfg) * no);

	for (uart = 0; uart < no; uart++, cfg++, cfgptr++) {
		platdev = s3c24xx_uart_src[cfgptr->hwport];

		resp = res + cfgptr->hwport;

		s3c24xx_uart_devs[uart] = platdev;

		platdev->name = name;
		platdev->resource = resp->resources;
		platdev->num_resources = resp->nr_resources;

		platdev->dev.platform_data = cfgptr;
	}

	nr_uarts = no;
#endif
}

void __init s3c24xx_init_uarts(struct s3c2410_uartcfg *cfg, int no)
{
	if (cpu == NULL)
		return;

	if (cpu->init_uarts == NULL && IS_ENABLED(CONFIG_SAMSUNG_ATAGS)) {
		printk(KERN_ERR "s3c24xx_init_uarts: cpu has no uart init\n");
	} else
		(cpu->init_uarts)(cfg, no);
}
#endif

static int __init s3c_arch_init(void)
{
	int ret;

	/* init is only needed for ATAGS based platforms */
	if (!IS_ENABLED(CONFIG_ATAGS) ||
	    (!soc_is_s3c24xx() && !soc_is_s3c64xx()))
		return 0;

	// do the correct init for cpu

	if (cpu == NULL) {
		/* Not needed when booting with device tree. */
		if (of_have_populated_dt())
			return 0;
		panic("s3c_arch_init: NULL cpu\n");
	}

	ret = (cpu->init)();
	if (ret != 0)
		return ret;
#if IS_ENABLED(CONFIG_SAMSUNG_ATAGS)
	ret = platform_add_devices(s3c24xx_uart_devs, nr_uarts);
#endif
	return ret;
}

arch_initcall(s3c_arch_init);
