/*
 * Copyright (C) 2011 Freescale Semiconductor, Inc. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
#include <asm/sizes.h>
#include <mach/mx23.h>
#include <mach/mx28.h>
#include <mach/devices-common.h>
#include <linux/dma-mapping.h>

#ifdef CONFIG_SOC_IMX23
const struct mxs_gpmi_nand_data mx23_gpmi_nand_data __initconst = {
	.devid = "imx23-gpmi-nand",
	.res = {
		/* GPMI */
		DEFINE_RES_MEM_NAMED(MX23_GPMI_BASE_ADDR, SZ_8K,
					GPMI_NAND_GPMI_REGS_ADDR_RES_NAME),
		DEFINE_RES_IRQ_NAMED(MX23_INT_GPMI_ATTENTION,
					GPMI_NAND_GPMI_INTERRUPT_RES_NAME),
		/* BCH */
		DEFINE_RES_MEM_NAMED(MX23_BCH_BASE_ADDR, SZ_8K,
					GPMI_NAND_BCH_REGS_ADDR_RES_NAME),
		DEFINE_RES_IRQ_NAMED(MX23_INT_BCH,
					GPMI_NAND_BCH_INTERRUPT_RES_NAME),
		/* DMA */
		DEFINE_RES_NAMED(MX23_DMA_GPMI0,
					MX23_DMA_GPMI3 - MX23_DMA_GPMI0 + 1,
					GPMI_NAND_DMA_CHANNELS_RES_NAME,
					IORESOURCE_DMA),
		DEFINE_RES_IRQ_NAMED(MX23_INT_GPMI_DMA,
					GPMI_NAND_DMA_INTERRUPT_RES_NAME),
	},
};
#endif

#ifdef CONFIG_SOC_IMX28
const struct mxs_gpmi_nand_data mx28_gpmi_nand_data __initconst = {
	.devid = "imx28-gpmi-nand",
	.res = {
		/* GPMI */
		DEFINE_RES_MEM_NAMED(MX28_GPMI_BASE_ADDR, SZ_8K,
					GPMI_NAND_GPMI_REGS_ADDR_RES_NAME),
		DEFINE_RES_IRQ_NAMED(MX28_INT_GPMI,
					GPMI_NAND_GPMI_INTERRUPT_RES_NAME),
		/* BCH */
		DEFINE_RES_MEM_NAMED(MX28_BCH_BASE_ADDR, SZ_8K,
					GPMI_NAND_BCH_REGS_ADDR_RES_NAME),
		DEFINE_RES_IRQ_NAMED(MX28_INT_BCH,
					GPMI_NAND_BCH_INTERRUPT_RES_NAME),
		/* DMA */
		DEFINE_RES_NAMED(MX28_DMA_GPMI0,
					MX28_DMA_GPMI7 - MX28_DMA_GPMI0 + 1,
					GPMI_NAND_DMA_CHANNELS_RES_NAME,
					IORESOURCE_DMA),
		DEFINE_RES_IRQ_NAMED(MX28_INT_GPMI_DMA,
					GPMI_NAND_DMA_INTERRUPT_RES_NAME),
	},
};
#endif

struct platform_device *__init
mxs_add_gpmi_nand(const struct gpmi_nand_platform_data *pdata,
		const struct mxs_gpmi_nand_data *data)
{
	return mxs_add_platform_device_dmamask(data->devid, -1,
				data->res, GPMI_NAND_RES_SIZE,
				pdata, sizeof(*pdata), DMA_BIT_MASK(32));
}
