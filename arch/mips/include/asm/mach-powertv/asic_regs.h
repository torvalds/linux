/*
 * Copyright (C) 2009  Cisco Systems, Inc.
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef __ASM_MACH_POWERTV_ASIC_H_
#define __ASM_MACH_POWERTV_ASIC_H_
#include <linux/io.h>

/* ASIC types */
enum asic_type {
	ASIC_UNKNOWN,
	ASIC_ZEUS,
	ASIC_CALLIOPE,
	ASIC_CRONUS,
	ASIC_CRONUSLITE,
	ASICS
};

/* hardcoded values read from Chip Version registers */
#define CRONUS_10	0x0B4C1C20
#define CRONUS_11	0x0B4C1C21
#define CRONUSLITE_10	0x0B4C1C40

#define NAND_FLASH_BASE	0x03000000
#define ZEUS_IO_BASE	0x09000000
#define CALLIOPE_IO_BASE	0x08000000
#define CRONUS_IO_BASE	0x09000000
#define ASIC_IO_SIZE	0x01000000

/* Definitions for backward compatibility */
#define UART1_INTSTAT	uart1_intstat
#define UART1_INTEN	uart1_inten
#define UART1_CONFIG1	uart1_config1
#define UART1_CONFIG2	uart1_config2
#define UART1_DIVISORHI	uart1_divisorhi
#define UART1_DIVISORLO	uart1_divisorlo
#define UART1_DATA	uart1_data
#define UART1_STATUS	uart1_status

/* ASIC register enumeration */
struct register_map {
	u32 eic_slow0_strt_add;
	u32 eic_cfg_bits;
	u32 eic_ready_status;

	u32 chipver3;
	u32 chipver2;
	u32 chipver1;
	u32 chipver0;

	u32 uart1_intstat;
	u32 uart1_inten;
	u32 uart1_config1;
	u32 uart1_config2;
	u32 uart1_divisorhi;
	u32 uart1_divisorlo;
	u32 uart1_data;
	u32 uart1_status;

	u32 int_stat_3;
	u32 int_stat_2;
	u32 int_stat_1;
	u32 int_stat_0;
	u32 int_config;
	u32 int_int_scan;
	u32 ien_int_3;
	u32 ien_int_2;
	u32 ien_int_1;
	u32 ien_int_0;
	u32 int_level_3_3;
	u32 int_level_3_2;
	u32 int_level_3_1;
	u32 int_level_3_0;
	u32 int_level_2_3;
	u32 int_level_2_2;
	u32 int_level_2_1;
	u32 int_level_2_0;
	u32 int_level_1_3;
	u32 int_level_1_2;
	u32 int_level_1_1;
	u32 int_level_1_0;
	u32 int_level_0_3;
	u32 int_level_0_2;
	u32 int_level_0_1;
	u32 int_level_0_0;
	u32 int_docsis_en;

	u32 mips_pll_setup;
	u32 usb_fs;
	u32 test_bus;
	u32 crt_spare;
	u32 usb2_ohci_int_mask;
	u32 usb2_strap;
	u32 ehci_hcapbase;
	u32 ohci_hc_revision;
	u32 bcm1_bs_lmi_steer;
	u32 usb2_control;
	u32 usb2_stbus_obc;
	u32 usb2_stbus_mess_size;
	u32 usb2_stbus_chunk_size;

	u32 pcie_regs;
	u32 tim_ch;
	u32 tim_cl;
	u32 gpio_dout;
	u32 gpio_din;
	u32 gpio_dir;
	u32 watchdog;
	u32 front_panel;

	u32 register_maps;
};

extern enum asic_type asic;
extern const struct register_map *register_map;
extern unsigned long asic_phy_base;	/* Physical address of ASIC */
extern unsigned long asic_base;		/* Virtual address of ASIC */

/*
 * Macros to interface to registers through their ioremapped address
 * asic_reg_offset	Returns the offset of a given register from the start
 *			of the ASIC address space
 * asic_reg_phys_addr	Returns the physical address of the given register
 * asic_reg_addr	Returns the iomapped virtual address of the given
 *			register.
 */
#define asic_reg_offset(x)	(register_map->x)
#define asic_reg_phys_addr(x)	(asic_phy_base + asic_reg_offset(x))
#define asic_reg_addr(x) \
	((unsigned int *) (asic_base + asic_reg_offset(x)))

/*
 * The asic_reg macro is gone. It should be replaced by either asic_read or
 * asic_write, as appropriate.
 */

#define asic_read(x)		readl(asic_reg_addr(x))
#define asic_write(v, x)	writel(v, asic_reg_addr(x))

extern void asic_irq_init(void);
#endif
