/*
 * arch/arm/mach-meson8/iomapping.c
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
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/clockchips.h>
#include <linux/clocksource.h>
#include <asm/memory.h>
#include <asm/mach/map.h>
#include <asm/mach/time.h>
#include <asm/sched_clock.h>
#include <plat/io.h>
#include <mach/io.h>
#include <mach/am_regs.h>


/***********************************************************************
 * IO Mapping
 **********************************************************************/
/*
#define IO_PHY_BASE             0xc0000000  ///value from vlsi team
#define IO_CBUS_PHY_BASE        0xc1100000  ///2M
#define IO_AXI_BUS_PHY_BASE     0xc1300000  ///1M
#define IO_PL310_PHY_BASE       0xc4200000  ///4k
#define IO_A9_PERIPH_PHY_BASE      0xc4300000  ///4k
#define IO_MMC_PHY_BASE     0xc8000000  ///32k

#define IO_AOBUS_PHY_BASE       0xc8100000  ///1M
	#define IO_USB_A_PHY_BASE       0xc9040000  ///256k
	#define IO_USB_B_PHY_BASE       0xc90C0000  ///256k
	#define IO_ETH_PHY_BASE         0xc9410000  ///64k
#define IO_SPIMEM_PHY_BASE      0xcc000000  ///64M
#define IO_A9_APB_PHY_BASE      0xd0000000  ///2M
	#define IO_HDMI_PHY_BASE  0xd0042000
	#define IO_AUDAC_PHY_BASE   0xd0044000  ///112k
	#define IO_DOS_BUS_PHY_BASE     0xd0050000  ///64k
	#define IO_MALI_APB_PHY_BASE    0xd00c0000  ///128k

#define IO_SRAM_PHY_BASE         0xd9000000  ///128k
#define IO_BOOTROM_PHY_BASE     0xd9040000  ///64k
#define IO_SECBUS_PHY_BASE      0xda000000
#define IO_EFUSE_PHY_BASE       0xda000000  ///4k
#define IO_SECURE_PHY_BASE      (IO_SECBUS_PHY_BASE+0x2000)  ///16k

#define IO_REGS_BASE		0xF2000000
#define IO_PL310_BASE		(IO_REGS_BASE + 0x000000) // 4k
#define IO_A9_PERIPH_BASE	(IO_REGS_BASE + 0x001000) // 16k
#define IO_RESERVED_1		(IO_REGS_BASE + 0x005000) // 20K
#define IO_MMC_BASE		(IO_REGS_BASE + 0x008000) // 32K
#define IO_BOOTROM_BASE	(IO_REGS_BASE + 0x010000) // 64K
#define IO_SRAM_BASE		(IO_REGS_BASE + 0x020000) // 128K
#define IO_USB_A_BASE		(IO_REGS_BASE + 0x040000) // 256K
#define IO_USB_B_BASE		(IO_REGS_BASE + 0x080000) // 256K
#define IO_ETH_BASE			(IO_REGS_BASE + 0x0C0000) // 64K
#define IO_SECBUS_BASE		(IO_REGS_BASE + 0x0D0000) // 32k
#define IO_RESERVED_2		(IO_REGS_BASE + 0x0D8000) // 160K
#define IO_CBUS_BASE		(IO_REGS_BASE + 0x100000) // 1M
#define IO_AXI_BUS_BASE		(IO_REGS_BASE + 0x200000) // 2M
#define IO_APB_BUS_BASE	(IO_REGS_BASE + 0x400000) // 2M
#define IO_AOBUS_BASE		(IO_REGS_BASE + 0x600000) // 1M
#define IO_REGS_END			(IO_REGS_BASE + 0xF00000 - 1) // Total 15M 

#define IO_SPI_BASE			(0xFB000000)
#define IO_SPIMEM_BASE		(IO_SPI_BASE + 0x0000000)
#define IO_SPI_END			(IO_SPI_BASE + 0x4000000 - 1) // Total 64M
*/
static __initdata struct map_desc meson_default_io_desc[] = {
    {
        .virtual    = IO_PL310_BASE,
        .pfn        = __phys_to_pfn(IO_PL310_PHY_BASE),
        .length     = SZ_4K,
        .type       = MT_DEVICE,
    } , {
        .virtual    = IO_A9_PERIPH_BASE,
        .pfn        = __phys_to_pfn(IO_A9_PERIPH_PHY_BASE),
        .length     = SZ_16K,
        .type       = MT_DEVICE,
    } , {
        .virtual    = IO_MMC_BUS_BASE,
        .pfn        = __phys_to_pfn(IO_MMC_PHY_BASE),
        .length     = SZ_32K,
        .type       = MT_DEVICE,
    } ,{
           .virtual    = IO_BOOTROM_BASE,
           .pfn        = __phys_to_pfn(IO_BOOTROM_PHY_BASE),
           .length     = SZ_64K,
           .type       = MT_DEVICE,
       }, {
           .virtual    = IO_SRAM_BASE,
           .pfn        = __phys_to_pfn(IO_SRAM_PHY_BASE),
           .length     = SZ_128K,
           .type       = MT_DEVICE,
       } , {
           .virtual    = IO_USB_A_BASE,
        .pfn        = __phys_to_pfn(IO_USB_A_PHY_BASE),
        .length     = SZ_256K,
        .type       = MT_DEVICE,
       } , {
           .virtual    = IO_USB_B_BASE,
        .pfn        = __phys_to_pfn(IO_USB_B_PHY_BASE),
        .length     = SZ_256K,
        .type       = MT_DEVICE,
  } , {
        .virtual    = IO_ETH_BASE,
        .pfn        = __phys_to_pfn(IO_ETH_PHY_BASE),
        .length     = SZ_64K,
        .type       = MT_DEVICE,
   } , {
        .virtual    = IO_SECBUS_BASE,
        .pfn        = __phys_to_pfn(IO_SECBUS_PHY_BASE),
        .length     = SZ_32K,
        .type       = MT_DEVICE,
    } , {
        .virtual    = IO_CBUS_BASE,
        .pfn        = __phys_to_pfn(IO_CBUS_PHY_BASE),
        .length     = SZ_1M,
        .type       = MT_DEVICE,
    } , {
        .virtual    = IO_AXI_BUS_BASE,
        .pfn        = __phys_to_pfn(IO_AXI_BUS_PHY_BASE),
        .length     = SZ_2M,
        .type       = MT_DEVICE,
    } , {
        .virtual    = IO_APB_BUS_BASE,
        .pfn        = __phys_to_pfn(IO_APB_BUS_PHY_BASE),
        .length     = SZ_2M,
        .type       = MT_DEVICE,
    } , {
        .virtual    = IO_AOBUS_BASE,
        .pfn        = __phys_to_pfn(IO_AOBUS_PHY_BASE),
        .length     = SZ_1M,
        .type       = MT_DEVICE,
    },
#ifdef CONFIG_AMLOGIC_SPI_HW_MASTER
    {
            .virtual    = IO_SPIMEM_BASE,
        .pfn        = __phys_to_pfn(IO_SPIMEM_PHY_BASE),
        .length     = SZ_64M,
        .type       = MT_ROM,
     } , 
#endif
#ifdef CONFIG_MESON_SUSPEND
        {
        .virtual    = PAGE_ALIGN(__phys_to_virt(0x04f00000)),
        .pfn        = __phys_to_pfn(0x04f00000),
        .length     = SZ_1M,
        .type       = MT_MEMORY_NONCACHED,
        },
#endif
};
unsigned int meson_uart_phy_addr=0xc8100000;
void __init meson_map_default_io(void)
{
	iotable_init(meson_default_io_desc, ARRAY_SIZE(meson_default_io_desc));
	meson_uart_phy_addr = IO_AOBUS_BASE;
}
