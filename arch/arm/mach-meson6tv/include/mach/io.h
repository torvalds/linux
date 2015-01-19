/*
* arch/arm/mach-meson6tv/include/mach/io.h
*
* Copyright (C) 2011-2013 Amlogic, Inc.
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

#ifndef __MACH_MESON6TV_IO_H
#define __MACH_MESON6TV_IO_H

///amlogic old style driver porting
#if (defined CONFIG_MESON_LEGACY_REGISTER_API) && CONFIG_MESON_LEGACY_REGISTER_API
#include "avosstyle_io.h"
#else
#warning "You should replace the register operation with \n" 	\
	"writel/readl/setbits_le32/clrbits_le32/clrsetbits_le32.\n" \
	"the register name must be replace with P_REG_NAME . \n"	\
	"REG_NAME is the old stlye reg name . 	"
#endif

//#define IO_SPACE_LIMIT 0xffffffff

//#define __io(a)     __typesafe_io(a)
#define __mem_pci(a)    (a)


/**
 * U boot style operation
 */


#define clrbits_le32 aml_clr_reg32_mask
#define setbits_le32 aml_set_reg32_mask
#define clrsetbits_le32 aml_clrset_reg32_bits
/**
 * PHY IO MEMORY BASE
 */
#define IO_PHY_BASE             0xc0000000  ///value from vlsi team
#define IO_CBUS_PHY_BASE        0xc1100000  ///2M
#define IO_AXI_BUS_PHY_BASE     0xc1300000  ///1M
#define IO_PL310_PHY_BASE       0xc4200000  ///4k
#define IO_PERIPH_PHY_BASE      0xc4300000  ///4k
#define IO_APB_BUS_PHY_BASE     0xc8000000  ///8k
#define IO_DOS_BUS_PHY_BASE     0xc8010000  ///64k
#define IO_HDEC_BUS_PHY_BASE    0xc8014000  ///64k
#define IO_AOBUS_PHY_BASE       0xc8100000  ///1M
#define IO_AHB_BUS_PHY_BASE		0xc9000000	///8M
	#define IO_USB_A_PHY_BASE       0xc9040000  ///512k
	#define IO_USB_B_PHY_BASE       0xc90C0000  ///256k
	#define IO_WIFI_PHY_BASE        0xc9300000  ///1M
	#define IO_SATA_PHY_BASE        0xc9400000  ///64k
	#define IO_ETH_PHY_BASE         0xc9410000  ///64k
#define IO_SPIMEM_PHY_BASE      0xcc000000  ///64M
#define IO_A9_APB_PHY_BASE      0xd0000000  ///256k
	#define IO_DEMOD_APB_PHY_BASE   0xd0044000  ///112k
	#define IO_MALI_APB_PHY_BASE    0xd0060000  ///128k
#define IO_APB2_BUS_PHY_BASE    0xd0000000
#define IO_AHB_PHY_BASE         0xd9000000  ///128k
#define IO_BOOTROM_PHY_BASE     0xd9040000  ///64k
#define IO_SECBUS_PHY_BASE      0xda000000
#define IO_EFUSE_PHY_BASE       0xda000000  ///4k
#define IO_SECURE_PHY_BASE      (IO_SECBUS_PHY_BASE+0x2000)  ///16k

#ifdef CONFIG_VMSPLIT_3G
#define IO_CBUS_BASE        0xf1100000  ///2M
#define IO_AXI_BUS_BASE     0xf1300000  ///1M
#define IO_PL310_BASE       0xf2200000  ///4k
#define IO_PERIPH_BASE      0xf2300000  ///4k
#define IO_APB_BUS_BASE     0xf3000000  ///8k
    #define IO_HDMI_BUS_BASE     0xf3002000  ///64k
    #define IO_DOS_BUS_BASE     0xf3010000  ///64k
    #define IO_HDEC_BUS_BASE 	0xf3014000  ///64k
#define IO_AOBUS_BASE       0xf3100000  ///1M
#define IO_AHB_BUS_BASE		0xf3200000
	#define IO_USB_A_BASE       0xf3240000  ///256k
	#define IO_USB_B_BASE       0xf32C0000  ///256k
	#define IO_USB_C_BASE       0xf3300000  ///256k
	#define IO_USB_D_BASE       0xf3340000  ///256k
#define IO_ETH_BASE         (IO_AHB_BUS_BASE + IO_ETH_PHY_BASE -IO_AHB_BUS_PHY_BASE)  ///64k
#define IO_SPIMEM_BASE      0xf4000000  ///64M
#define IO_A9_APB_BASE      0xf8000000  ///256k
	#define IO_DEMOD_APB_BASE   0xf8044000  ///112k
	#define IO_MALI_APB_BASE    0xf8060000  ///128k
#define IO_APB2_BUS_BASE    0xf8000000
#define IO_AHB_BASE         0xf9000000  ///128k
#define IO_BOOTROM_BASE     0xf9040000  ///64k
#define IO_SECBUS_BASE      0xfa000000
#define IO_EFUSE_BASE       0xfa000000  ///4k
#define IO_SECURE_BASE      0xfa002000  ///16k
#endif

#ifdef CONFIG_VMSPLIT_2G
#define IO_CBUS_BASE        IO_CBUS_PHY_BASE       ///2M
#define IO_AXI_BUS_BASE     IO_AXI_BUS_PHY_BASE    ///1M
#define IO_PL310_BASE       IO_PL310_PHY_BASE      ///4k
#define IO_PERIPH_BASE      IO_PERIPH_PHY_BASE     ///4k
#define IO_APB_BUS_BASE     IO_APB_BUS_PHY_BASE    ///8k
#define IO_DOS_BUS_BASE     IO_DOS_BUS_PHY_BASE    ///64k
#define IO_AOBUS_BASE       IO_AOBUS_PHY_BASE      ///1M
#define IO_USB_A_BASE       IO_USB_A_PHY_BASE      ///512k
#define IO_USB_B_BASE       IO_USB_B_PHY_BASE      ///256k
#define IO_WIFI_BASE        IO_WIFI_PHY_BASE       ///1M
#define IO_SATA_BASE        IO_SATA_PHY_BASE       ///64k
#define IO_ETH_BASE         IO_ETH_PHY_BASE        ///64k
#define IO_SPIMEM_BASE      IO_SPIMEM_PHY_BASE     ///64M
#define IO_A9_APB_BASE      IO_A9_APB_PHY_BASE     ///256k
#define IO_DEMOD_APB_BASE   IO_DEMOD_APB_PHY_BASE  ///112k
#define IO_MALI_APB_BASE    IO_MALI_APB_PHY_BASE   ///128k
#define IO_APB2_BUS_BASE    IO_APB2_BUS_PHY_BASE
#define IO_AHB_BASE         IO_AHB_PHY_BASE        ///128k
#define IO_AHB_BUS_BASE         IO_AHB_BUS_PHY_BASE        ///128k

#define IO_BOOTROM_BASE     IO_BOOTROM_PHY_BASE    ///64k
#define IO_SECBUS_BASE      IO_SECBUS_PHY_BASE
#define IO_EFUSE_BASE       IO_EFUSE_PHY_BASE      ///4k
#define IO_SECURE_BASE      IO_SECURE_PHY_BASE     ///16k
#endif

#ifdef CONFIG_VMSPLIT_1G
#error Unsupported Memory Split Type
#endif


#define MESON_PERIPHS1_VIRT_BASE	(IO_AOBUS_BASE+0x4c0)
#define MESON_PERIPHS1_PHYS_BASE	(IO_AOBUS_PHY_BASE+0x4c0)


#define CBUS_REG_OFFSET(reg)		((reg) << 2)
#define CBUS_REG_ADDR(reg)		(IO_CBUS_BASE + CBUS_REG_OFFSET(reg))

#define CBUS_REG_OFFSET(reg)		((reg) << 2)
#define DOS_REG_ADDR(reg)		(IO_DOS_BUS_BASE + CBUS_REG_OFFSET(reg))

#define CBUS_REG_OFFSET(reg)		((reg) << 2)
#define HDEC_REG_ADDR(reg)		(IO_HDEC_BUS_BASE + CBUS_REG_OFFSET(reg))
#define AXI_REG_OFFSET(reg)		((reg) << 2)
#define AXI_REG_ADDR(reg)		(IO_AXI_BUS_BASE + AXI_REG_OFFSET(reg))

#define AHB_REG_OFFSET(reg)		((reg) << 2)
#define AHB_REG_ADDR(reg)		(IO_AHB_BUS_BASE + AHB_REG_OFFSET(reg))

#define APB_REG_OFFSET(reg)		(reg&0xfffff)
#define APB_REG_ADDR(reg)		(IO_APB_BUS_BASE + APB_REG_OFFSET(reg))
#define APB_REG_ADDR_VALID(reg)		(((unsigned long)(reg) & 3) == 0)

#define AOBUS_REG_OFFSET(reg)		((reg) )
#define AOBUS_REG_ADDR(reg)		(IO_AOBUS_BASE + AOBUS_REG_OFFSET(reg))

#define SECBUS_REG_OFFSET(reg)		((reg) <<2)
#define SECBUS_REG_ADDR(reg)		(IO_SECBUS_BASE+SECBUS_REG_OFFSET(reg))
#define SECBUS2_REG_ADDR(reg)		(IO_SECURE_BASE+0x2000+SECBUS_REG_OFFSET(reg))

void meson6tv_map_default_io(void);

#endif //__MACH_MESON6TV_IO_H