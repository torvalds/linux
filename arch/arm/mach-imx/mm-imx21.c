/*
 * arch/arm/mach-imx/mm-imx21.c
 *
 * Copyright (C) 2008 Juergen Beisert (kernel@pengutronix.de)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */

#include <linux/mm.h>
#include <linux/init.h>
#include <mach/hardware.h>
#include <mach/common.h>
#include <asm/pgtable.h>
#include <asm/mach/map.h>
#include <mach/gpio.h>
#include <mach/irqs.h>
#include <mach/iomux-v1.h>

/* MX21 memory map definition */
static struct map_desc imx21_io_desc[] __initdata = {
	/*
	 * this fixed mapping covers:
	 * - AIPI1
	 * - AIPI2
	 * - AITC
	 * - ROM Patch
	 * - and some reserved space
	 */
	imx_map_entry(MX21, AIPI, MT_DEVICE),
	/*
	 * this fixed mapping covers:
	 * - CSI
	 * - ATA
	 */
	imx_map_entry(MX21, SAHB1, MT_DEVICE),
	/*
	 * this fixed mapping covers:
	 * - EMI
	 */
	imx_map_entry(MX21, X_MEMC, MT_DEVICE),
};

/*
 * Initialize the memory map. It is called during the
 * system startup to create static physical to virtual
 * memory map for the IO modules.
 */
void __init mx21_map_io(void)
{
	iotable_init(imx21_io_desc, ARRAY_SIZE(imx21_io_desc));
}

void __init imx21_init_early(void)
{
	mxc_set_cpu_type(MXC_CPU_MX21);
	mxc_arch_reset_init(MX21_IO_ADDRESS(MX21_WDOG_BASE_ADDR));
	imx_iomuxv1_init(MX21_IO_ADDRESS(MX21_GPIO_BASE_ADDR),
			MX21_NUM_GPIO_PORT);
}

static struct mxc_gpio_port imx21_gpio_ports[] = {
	DEFINE_IMX_GPIO_PORT_IRQ(MX21, 0, 1, MX21_INT_GPIO),
	DEFINE_IMX_GPIO_PORT(MX21, 1, 2),
	DEFINE_IMX_GPIO_PORT(MX21, 2, 3),
	DEFINE_IMX_GPIO_PORT(MX21, 3, 4),
	DEFINE_IMX_GPIO_PORT(MX21, 4, 5),
	DEFINE_IMX_GPIO_PORT(MX21, 5, 6),
};

void __init mx21_init_irq(void)
{
	mxc_init_irq(MX21_IO_ADDRESS(MX21_AVIC_BASE_ADDR));
	mxc_gpio_init(imx21_gpio_ports,	ARRAY_SIZE(imx21_gpio_ports));
}
