/*
 * arch/arm/mach-imx/mm-imx27.c
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
#include <linux/pinctrl/machine.h>
#include <mach/hardware.h>
#include <mach/common.h>
#include <mach/devices-common.h>
#include <asm/pgtable.h>
#include <asm/mach/map.h>
#include <mach/iomux-v1.h>

/* MX27 memory map definition */
static struct map_desc imx27_io_desc[] __initdata = {
	/*
	 * this fixed mapping covers:
	 * - AIPI1
	 * - AIPI2
	 * - AITC
	 * - ROM Patch
	 * - and some reserved space
	 */
	imx_map_entry(MX27, AIPI, MT_DEVICE),
	/*
	 * this fixed mapping covers:
	 * - CSI
	 * - ATA
	 */
	imx_map_entry(MX27, SAHB1, MT_DEVICE),
	/*
	 * this fixed mapping covers:
	 * - EMI
	 */
	imx_map_entry(MX27, X_MEMC, MT_DEVICE),
};

/*
 * Initialize the memory map. It is called during the
 * system startup to create static physical to virtual
 * memory map for the IO modules.
 */
void __init mx27_map_io(void)
{
	iotable_init(imx27_io_desc, ARRAY_SIZE(imx27_io_desc));
}

void __init imx27_init_early(void)
{
	mxc_set_cpu_type(MXC_CPU_MX27);
	mxc_arch_reset_init(MX27_IO_ADDRESS(MX27_WDOG_BASE_ADDR));
	imx_iomuxv1_init(MX27_IO_ADDRESS(MX27_GPIO_BASE_ADDR),
			MX27_NUM_GPIO_PORT);
}

void __init mx27_init_irq(void)
{
	mxc_init_irq(MX27_IO_ADDRESS(MX27_AVIC_BASE_ADDR));
}

static const struct resource imx27_audmux_res[] __initconst = {
	DEFINE_RES_MEM(MX27_AUDMUX_BASE_ADDR, SZ_4K),
};

void __init imx27_soc_init(void)
{
	/* i.mx27 has the i.mx21 type gpio */
	mxc_register_gpio("imx21-gpio", 0, MX27_GPIO1_BASE_ADDR, SZ_256, MX27_INT_GPIO, 0);
	mxc_register_gpio("imx21-gpio", 1, MX27_GPIO2_BASE_ADDR, SZ_256, MX27_INT_GPIO, 0);
	mxc_register_gpio("imx21-gpio", 2, MX27_GPIO3_BASE_ADDR, SZ_256, MX27_INT_GPIO, 0);
	mxc_register_gpio("imx21-gpio", 3, MX27_GPIO4_BASE_ADDR, SZ_256, MX27_INT_GPIO, 0);
	mxc_register_gpio("imx21-gpio", 4, MX27_GPIO5_BASE_ADDR, SZ_256, MX27_INT_GPIO, 0);
	mxc_register_gpio("imx21-gpio", 5, MX27_GPIO6_BASE_ADDR, SZ_256, MX27_INT_GPIO, 0);

	pinctrl_provide_dummies();
	imx_add_imx_dma();
	/* imx27 has the imx21 type audmux */
	platform_device_register_simple("imx21-audmux", 0, imx27_audmux_res,
					ARRAY_SIZE(imx27_audmux_res));
}
