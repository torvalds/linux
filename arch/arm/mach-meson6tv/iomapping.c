/*
 * arch/arm/mach-meson6tv/iomapping.c
 *
 * Copyright (C) 2013 Amlogic, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sizes.h>
#include <linux/mm.h>
#include <asm/memory.h>
#include <asm/mach/map.h>

#include <mach/io.h>

/***********************************************************************
 * IO Mapping
 **********************************************************************/
/*
#define IO_CBUS_BASE        0xf1100000  ///2M
#define IO_AXI_BUS_BASE     0xf1300000  ///1M
#define IO_PL310_BASE       0xf2200000  ///4k
#define IO_PERIPH_BASE      0xf2300000  ///4k
#define IO_APB_BUS_BASE     0xf3000000  ///8k
#define IO_DOS_BUS_BASE     0xf3010000  ///64k
#define IO_AOBUS_BASE       0xf3100000  ///1M
#define IO_USB_A_BASE       0xf3240000  ///256k
#define IO_USB_B_BASE       0xf32C0000  ///256k
#define IO_WIFI_BASE        0xf3300000  ///1M
#define IO_SATA_BASE        0xf3400000  ///64k
#define IO_ETH_BASE         0xf3410000  ///64k

#define IO_SPIMEM_BASE      0xf4000000  ///64M
#define IO_A9_APB_BASE      0xf8000000  ///256k
#define IO_DEMOD_APB_BASE   0xf8044000  ///112k
#define IO_MALI_APB_BASE    0xf8060000  ///128k
#define IO_APB2_BUS_BASE    0xf8000000
#define IO_AHB_BASE         0xf9000000  ///128k
#define IO_BOOTROM_BASE     0xf9040000  ///64k
#define IO_SECBUS_BASE      0xfa000000
#define IO_EFUSE_BASE       0xfa000000  ///4k
*/

static __initdata struct map_desc meson6tv_io_desc[] = {
	{
		.virtual	= IO_CBUS_BASE,
		.pfn		= __phys_to_pfn(IO_CBUS_PHY_BASE),
		.length		= SZ_2M,
		.type		= MT_DEVICE,
	}, {
		.virtual	= IO_AXI_BUS_BASE,
		.pfn		= __phys_to_pfn(IO_AXI_BUS_PHY_BASE),
		.length		= SZ_1M,
		.type		= MT_DEVICE,
	}, {
		.virtual	= IO_PL310_BASE,
		.pfn		= __phys_to_pfn(IO_PL310_PHY_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= IO_PERIPH_BASE,
		.pfn		= __phys_to_pfn(IO_PERIPH_PHY_BASE),
		.length		= SZ_1M,
		.type		= MT_DEVICE,
	}, {
		.virtual	= IO_APB_BUS_BASE,
		.pfn		= __phys_to_pfn(IO_APB_BUS_PHY_BASE),
		.length		= SZ_1M,
		.type		= MT_DEVICE,
	}, /*{
		.virtual	= IO_DOS_BUS_BASE,
		.pfn		= __phys_to_pfn(IO_DOS_BUS_PHY_BASE),
		.length		= SZ_64K,
		.type		= MT_DEVICE,
	}, */{
		.virtual	= IO_AOBUS_BASE,
		.pfn		= __phys_to_pfn(IO_AOBUS_PHY_BASE),
		.length		= SZ_1M,
		.type		= MT_DEVICE,
	}, {
		.virtual	= IO_AHB_BUS_BASE,
		.pfn		= __phys_to_pfn(IO_AHB_BUS_PHY_BASE),
		.length		= SZ_8M,
		.type		= MT_DEVICE,
	}, {
		.virtual	= IO_SPIMEM_BASE,
		.pfn		= __phys_to_pfn(IO_SPIMEM_PHY_BASE),
		.length		= SZ_64M,
		.type		= MT_ROM,
	}, {
		.virtual	= IO_APB2_BUS_BASE,
		.pfn		= __phys_to_pfn(IO_APB2_BUS_PHY_BASE),
		.length		= SZ_512K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= IO_AHB_BASE,
		.pfn		= __phys_to_pfn(IO_AHB_PHY_BASE),
		.length		= SZ_128K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= IO_BOOTROM_BASE,
		.pfn		= __phys_to_pfn(IO_BOOTROM_PHY_BASE),
		.length		= SZ_64K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= IO_SECBUS_BASE,
		.pfn		= __phys_to_pfn(IO_SECBUS_PHY_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= IO_SECURE_BASE,
		.pfn		= __phys_to_pfn(IO_SECURE_PHY_BASE),
		.length		= SZ_16K,
		.type		= MT_DEVICE,
	},
#ifdef CONFIG_MESON_SUSPEND
	{
		.virtual	= PAGE_ALIGN(__phys_to_virt(0x9ff00000)),
		.pfn		= __phys_to_pfn(0x9ff00000),
		.length		= SZ_1M,
		.type		= MT_MEMORY_NONCACHED,
	},
#endif

};

void __init meson6tv_map_default_io(void)
{
	iotable_init(meson6tv_io_desc, ARRAY_SIZE(meson6tv_io_desc));
}

