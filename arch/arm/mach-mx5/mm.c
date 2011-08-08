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

#include <asm/mach/map.h>

#include <mach/hardware.h>
#include <mach/common.h>
#include <mach/devices-common.h>
#include <mach/iomux-v3.h>

/*
 * Define the MX51 memory map.
 */
static struct map_desc mx51_io_desc[] __initdata = {
	imx_map_entry(MX51, IRAM, MT_DEVICE),
	imx_map_entry(MX51, DEBUG, MT_DEVICE),
	imx_map_entry(MX51, AIPS1, MT_DEVICE),
	imx_map_entry(MX51, SPBA0, MT_DEVICE),
	imx_map_entry(MX51, AIPS2, MT_DEVICE),
};

/*
 * Define the MX53 memory map.
 */
static struct map_desc mx53_io_desc[] __initdata = {
	imx_map_entry(MX53, AIPS1, MT_DEVICE),
	imx_map_entry(MX53, SPBA0, MT_DEVICE),
	imx_map_entry(MX53, AIPS2, MT_DEVICE),
};

/*
 * This function initializes the memory map. It is called during the
 * system startup to create static physical to virtual memory mappings
 * for the IO modules.
 */
void __init mx51_map_io(void)
{
	iotable_init(mx51_io_desc, ARRAY_SIZE(mx51_io_desc));
}

void __init imx51_init_early(void)
{
	mxc_set_cpu_type(MXC_CPU_MX51);
	mxc_iomux_v3_init(MX51_IO_ADDRESS(MX51_IOMUXC_BASE_ADDR));
	mxc_arch_reset_init(MX51_IO_ADDRESS(MX51_WDOG1_BASE_ADDR));
}

void __init mx53_map_io(void)
{
	iotable_init(mx53_io_desc, ARRAY_SIZE(mx53_io_desc));
}

void __init imx53_init_early(void)
{
	mxc_set_cpu_type(MXC_CPU_MX53);
	mxc_iomux_v3_init(MX53_IO_ADDRESS(MX53_IOMUXC_BASE_ADDR));
	mxc_arch_reset_init(MX53_IO_ADDRESS(MX53_WDOG1_BASE_ADDR));
}

void __init mx51_init_irq(void)
{
	unsigned long tzic_addr;
	void __iomem *tzic_virt;

	if (mx51_revision() < IMX_CHIP_REVISION_2_0)
		tzic_addr = MX51_TZIC_BASE_ADDR_TO1;
	else
		tzic_addr = MX51_TZIC_BASE_ADDR;

	tzic_virt = ioremap(tzic_addr, SZ_16K);
	if (!tzic_virt)
		panic("unable to map TZIC interrupt controller\n");

	tzic_init_irq(tzic_virt);
}

void __init mx53_init_irq(void)
{
	unsigned long tzic_addr;
	void __iomem *tzic_virt;

	tzic_addr = MX53_TZIC_BASE_ADDR;

	tzic_virt = ioremap(tzic_addr, SZ_16K);
	if (!tzic_virt)
		panic("unable to map TZIC interrupt controller\n");

	tzic_init_irq(tzic_virt);
}

static struct sdma_script_start_addrs imx51_sdma_script __initdata = {
	.ap_2_ap_addr = 642,
	.uart_2_mcu_addr = 817,
	.mcu_2_app_addr = 747,
	.mcu_2_shp_addr = 961,
	.ata_2_mcu_addr = 1473,
	.mcu_2_ata_addr = 1392,
	.app_2_per_addr = 1033,
	.app_2_mcu_addr = 683,
	.shp_2_per_addr = 1251,
	.shp_2_mcu_addr = 892,
};

static struct sdma_platform_data imx51_sdma_pdata __initdata = {
	.fw_name = "sdma-imx51.bin",
	.script_addrs = &imx51_sdma_script,
};

static struct sdma_script_start_addrs imx53_sdma_script __initdata = {
	.ap_2_ap_addr = 642,
	.app_2_mcu_addr = 683,
	.mcu_2_app_addr = 747,
	.uart_2_mcu_addr = 817,
	.shp_2_mcu_addr = 891,
	.mcu_2_shp_addr = 960,
	.uartsh_2_mcu_addr = 1032,
	.spdif_2_mcu_addr = 1100,
	.mcu_2_spdif_addr = 1134,
	.firi_2_mcu_addr = 1193,
	.mcu_2_firi_addr = 1290,
};

static struct sdma_platform_data imx53_sdma_pdata __initdata = {
	.fw_name = "sdma-imx53.bin",
	.script_addrs = &imx53_sdma_script,
};

void __init imx51_soc_init(void)
{
	/* i.mx51 has the i.mx31 type gpio */
	mxc_register_gpio("imx31-gpio", 0, MX51_GPIO1_BASE_ADDR, SZ_16K, MX51_MXC_INT_GPIO1_LOW, MX51_MXC_INT_GPIO1_HIGH);
	mxc_register_gpio("imx31-gpio", 1, MX51_GPIO2_BASE_ADDR, SZ_16K, MX51_MXC_INT_GPIO2_LOW, MX51_MXC_INT_GPIO2_HIGH);
	mxc_register_gpio("imx31-gpio", 2, MX51_GPIO3_BASE_ADDR, SZ_16K, MX51_MXC_INT_GPIO3_LOW, MX51_MXC_INT_GPIO3_HIGH);
	mxc_register_gpio("imx31-gpio", 3, MX51_GPIO4_BASE_ADDR, SZ_16K, MX51_MXC_INT_GPIO4_LOW, MX51_MXC_INT_GPIO4_HIGH);

	/* i.mx51 has the i.mx35 type sdma */
	imx_add_imx_sdma("imx35-sdma", MX51_SDMA_BASE_ADDR, MX51_INT_SDMA, &imx51_sdma_pdata);
}

void __init imx53_soc_init(void)
{
	/* i.mx53 has the i.mx31 type gpio */
	mxc_register_gpio("imx31-gpio", 0, MX53_GPIO1_BASE_ADDR, SZ_16K, MX53_INT_GPIO1_LOW, MX53_INT_GPIO1_HIGH);
	mxc_register_gpio("imx31-gpio", 1, MX53_GPIO2_BASE_ADDR, SZ_16K, MX53_INT_GPIO2_LOW, MX53_INT_GPIO2_HIGH);
	mxc_register_gpio("imx31-gpio", 2, MX53_GPIO3_BASE_ADDR, SZ_16K, MX53_INT_GPIO3_LOW, MX53_INT_GPIO3_HIGH);
	mxc_register_gpio("imx31-gpio", 3, MX53_GPIO4_BASE_ADDR, SZ_16K, MX53_INT_GPIO4_LOW, MX53_INT_GPIO4_HIGH);
	mxc_register_gpio("imx31-gpio", 4, MX53_GPIO5_BASE_ADDR, SZ_16K, MX53_INT_GPIO5_LOW, MX53_INT_GPIO5_HIGH);
	mxc_register_gpio("imx31-gpio", 5, MX53_GPIO6_BASE_ADDR, SZ_16K, MX53_INT_GPIO6_LOW, MX53_INT_GPIO6_HIGH);
	mxc_register_gpio("imx31-gpio", 6, MX53_GPIO7_BASE_ADDR, SZ_16K, MX53_INT_GPIO7_LOW, MX53_INT_GPIO7_HIGH);

	/* i.mx53 has the i.mx35 type sdma */
	imx_add_imx_sdma("imx35-sdma", MX53_SDMA_BASE_ADDR, MX53_INT_SDMA, &imx53_sdma_pdata);
}
