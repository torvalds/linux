/*
 * Copyright 2008-2010 Freescale Semiconductor, Inc. All Rights Reserved.
 *
 * The code contained herein is licensed under the GNU General Public
 * License.  You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 *
 * Create static mapping between physical to virtual memory.
 */

#include <linux/mm.h>
#include <linux/init.h>
#include <linux/pinctrl/machine.h>

#include <asm/mach/map.h>

#include <mach/mx23.h>
#include <mach/mx28.h>
#include <mach/common.h>
#include <mach/iomux.h>

/*
 * Define the MX23 memory map.
 */
static struct map_desc mx23_io_desc[] __initdata = {
	mxs_map_entry(MX23, OCRAM, MT_DEVICE),
	mxs_map_entry(MX23, IO, MT_DEVICE),
};

/*
 * Define the MX28 memory map.
 */
static struct map_desc mx28_io_desc[] __initdata = {
	mxs_map_entry(MX28, OCRAM, MT_DEVICE),
	mxs_map_entry(MX28, IO, MT_DEVICE),
};

/*
 * This function initializes the memory map. It is called during the
 * system startup to create static physical to virtual memory mappings
 * for the IO modules.
 */
void __init mx23_map_io(void)
{
	iotable_init(mx23_io_desc, ARRAY_SIZE(mx23_io_desc));
}

void __init mx23_init_irq(void)
{
	icoll_init_irq();
}

void __init mx28_map_io(void)
{
	iotable_init(mx28_io_desc, ARRAY_SIZE(mx28_io_desc));
}

void __init mx28_init_irq(void)
{
	icoll_init_irq();
}

void __init mx23_soc_init(void)
{
	pinctrl_provide_dummies();

	mxs_add_dma("imx23-dma-apbh", MX23_APBH_DMA_BASE_ADDR);
	mxs_add_dma("imx23-dma-apbx", MX23_APBX_DMA_BASE_ADDR);

	mxs_add_gpio("imx23-gpio", 0, MX23_PINCTRL_BASE_ADDR, MX23_INT_GPIO0);
	mxs_add_gpio("imx23-gpio", 1, MX23_PINCTRL_BASE_ADDR, MX23_INT_GPIO1);
	mxs_add_gpio("imx23-gpio", 2, MX23_PINCTRL_BASE_ADDR, MX23_INT_GPIO2);
}

void __init mx28_soc_init(void)
{
	pinctrl_provide_dummies();

	mxs_add_dma("imx28-dma-apbh", MX23_APBH_DMA_BASE_ADDR);
	mxs_add_dma("imx28-dma-apbx", MX23_APBX_DMA_BASE_ADDR);

	mxs_add_gpio("imx28-gpio", 0, MX28_PINCTRL_BASE_ADDR, MX28_INT_GPIO0);
	mxs_add_gpio("imx28-gpio", 1, MX28_PINCTRL_BASE_ADDR, MX28_INT_GPIO1);
	mxs_add_gpio("imx28-gpio", 2, MX28_PINCTRL_BASE_ADDR, MX28_INT_GPIO2);
	mxs_add_gpio("imx28-gpio", 3, MX28_PINCTRL_BASE_ADDR, MX28_INT_GPIO3);
	mxs_add_gpio("imx28-gpio", 4, MX28_PINCTRL_BASE_ADDR, MX28_INT_GPIO4);
}
