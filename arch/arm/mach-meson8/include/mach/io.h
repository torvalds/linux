/*
 *
 * arch/arm/mach-meson8/include/mach/io.h
 *
 *  Copyright (C) 2011-2013 AMLOGIC, INC.
 *
 * License terms: GNU General Public License (GPL) version 2
 * Basic register address definitions in physical memory and
 * some block defintions for core devices like the timer.
 */

#ifndef __MACH_MESSON8_IO_H
#define __MACH_MESSON8_IO_H

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
#define IO_A9_PERIPH_PHY_BASE      0xc4300000  ///4k
#define IO_MMC_PHY_BASE     0xc8000000  ///32k

#define IO_AOBUS_PHY_BASE       0xc8100000  ///1M
#define IO_USB_A_PHY_BASE       0xc9040000  ///256k
#define IO_USB_B_PHY_BASE       0xc90C0000  ///256k
#define IO_ETH_PHY_BASE         0xc9410000  ///64k
#define IO_SPIMEM_PHY_BASE      0xcc000000  ///64M
#define IO_APB_BUS_PHY_BASE      0xd0000000  ///2M
	#define IO_HDMI_PHY_BASE  0xd0042000
	#define IO_AUDAC_PHY_BASE   0xd0044000  ///112k
	#define IO_DOS_BUS_PHY_BASE     0xd0050000  ///64k
	#define IO_MALI_APB_PHY_BASE    0xd00c0000  ///128k
#define IO_SRAM_PHY_BASE         0xd9000000  ///128k
#define IO_BOOTROM_PHY_BASE     0xd9040000  ///64k
#define IO_SECBUS_PHY_BASE      0xda000000
#define IO_EFUSE_PHY_BASE       0xda000000  ///4k
#define IO_SECURE_PHY_BASE      (IO_SECBUS_PHY_BASE+0x2000)  ///16k

#ifdef CONFIG_VMSPLIT_3G

#define IO_REGS_BASE		0xFE000000
#define IO_PL310_BASE		(IO_REGS_BASE + 0x000000) // 4k
#define IO_A9_PERIPH_BASE	(IO_REGS_BASE + 0x001000) // 16k
#define IO_RESERVED_1		(IO_REGS_BASE + 0x005000) // 20K
#define IO_MMC_BUS_BASE	(IO_REGS_BASE + 0x008000) // 32K
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

#define IO_SPI_BASE			(IO_REGS_BASE - 0x4000000)
#define IO_SPIMEM_BASE		(IO_SPI_BASE + 0x0000000)
#define IO_SPI_END			(IO_SPI_BASE + 0x4000000 - 1) // Total 64M

/* Quick reference */
#define IO_HDMI_BUS_BASE	(IO_APB_BUS_BASE +  0x40000)
#define IO_AUDAC_BASE   	(IO_APB_BUS_BASE +  0x44000)
#define IO_DOS_BUS_BASE	(IO_APB_BUS_BASE +  0x50000)
#define IO_HDEC_BUS_BASE	(IO_APB_BUS_BASE +  0x54000)
#define IO_NAND_BASE		(IO_APB_BUS_BASE +  0x48600)
#define IO_MALI_APB_BASE	(IO_APB_BUS_BASE +  0xC0000)
#define IO_VPU_BUS_BASE	(IO_APB_BUS_BASE + 0x100000)
#define IO_MIPI_HOST_BASE	(IO_APB_BUS_BASE + 0x140000)
#define IO_MIPI_PHY_BASE	(IO_APB_BUS_BASE + 0x142000)
#define IO_MIPI_DSI_BASE	(IO_APB_BUS_BASE + 0x150000)
#define IO_EDP_TX_BASE		(IO_APB_BUS_BASE + 0x160000)
#define IO_EFUSE_BASE		IO_SECBUS_BASE
#define IO_PERIPH_BASE		IO_A9_PERIPH_BASE

#endif

#ifdef CONFIG_VMSPLIT_2G

#define IO_CBUS_BASE        IO_CBUS_PHY_BASE       ///2M
#define IO_AXI_BUS_BASE     IO_AXI_BUS_PHY_BASE    ///1M
#define IO_PL310_BASE       IO_PL310_PHY_BASE      ///4k
#define IO_PERIPH_BASE      IO_PERIPH_PHY_BASE     ///4k
#define IO_APB_BUS_BASE     IO_APB_BUS_PHY_BASE    ///8k
#define IO_DOS_BUS_BASE     IO_DOS_BUS_PHY_BASE    ///64k
#define IO_AOBUS_BASE       IO_AOBUS_PHY_BASE      ///1M
#define IO_USB_A_BASE       IO_USB_A_PHY_BASE      ///256k
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


#define MESON_PERIPHS1_VIRT_BASE    (IO_AOBUS_BASE+0x4c0)
#define MESON_PERIPHS1_PHYS_BASE    (IO_AOBUS_PHY_BASE+0x4c0)


#define CBUS_REG_OFFSET(reg) ((reg) << 2)
#define CBUS_REG_ADDR(reg)	 (IO_CBUS_BASE + CBUS_REG_OFFSET(reg))

#define VCBUS_REG_ADDR(reg) (IO_APB_BUS_BASE + 0x100000 +CBUS_REG_OFFSET(reg))

#define DOS_REG_ADDR(reg)	 (IO_DOS_BUS_BASE + CBUS_REG_OFFSET(reg))

#define HDEC_REG_ADDR(reg)       (IO_HDEC_BUS_BASE + CBUS_REG_OFFSET(reg))
#define MMC_REG_ADDR(reg)	(IO_MMC_BUS_BASE + (reg))

#define AXI_REG_OFFSET(reg)  ((reg) << 2)
#define AXI_REG_ADDR(reg)	 (IO_AXI_BUS_BASE + AXI_REG_OFFSET(reg))

#define APB_REG_OFFSET(reg)     (reg&0xfffff)
#define APB_REG_ADDR(reg)	    (IO_APB_BUS_BASE + APB_REG_OFFSET(reg))
#define APB_REG_ADDR_VALID(reg) (((unsigned long)(reg) & 3) == 0)

#define AOBUS_REG_OFFSET(reg)   ((reg) )
#define AOBUS_REG_ADDR(reg)	    (IO_AOBUS_BASE + AOBUS_REG_OFFSET(reg))

#define SECBUS_REG_OFFSET(reg)   ((reg) <<2)
#define SECBUS_REG_ADDR(reg)     (IO_SECBUS_BASE+SECBUS_REG_OFFSET(reg))
#define SECBUS2_REG_ADDR(reg)       (IO_SECBUS_BASE+0x4000+SECBUS_REG_OFFSET(reg))
#define SECBUS3_REG_ADDR(reg)       (IO_SECBUS_BASE+0x6000+SECBUS_REG_OFFSET(reg))

void meson_map_default_io(void);

#endif //__MACH_MESSON8_IO_H
