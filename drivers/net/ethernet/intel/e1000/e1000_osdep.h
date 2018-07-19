/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 1999 - 2006 Intel Corporation. */

/* glue for the OS independent part of e1000
 * includes register access macros
 */

#ifndef _E1000_OSDEP_H_
#define _E1000_OSDEP_H_

#include <asm/io.h>

#define CONFIG_RAM_BASE         0x60000
#define GBE_CONFIG_OFFSET       0x0

#define GBE_CONFIG_RAM_BASE \
	((unsigned int)(CONFIG_RAM_BASE + GBE_CONFIG_OFFSET))

#define GBE_CONFIG_BASE_VIRT \
	((void __iomem *)phys_to_virt(GBE_CONFIG_RAM_BASE))

#define GBE_CONFIG_FLASH_WRITE(base, offset, count, data) \
	(iowrite16_rep(base + offset, data, count))

#define GBE_CONFIG_FLASH_READ(base, offset, count, data) \
	(ioread16_rep(base + (offset << 1), data, count))

#define er32(reg)							\
	(readl(hw->hw_addr + ((hw->mac_type >= e1000_82543)		\
			       ? E1000_##reg : E1000_82542_##reg)))

#define ew32(reg, value)						\
	(writel((value), (hw->hw_addr + ((hw->mac_type >= e1000_82543)	\
					 ? E1000_##reg : E1000_82542_##reg))))

#define E1000_WRITE_REG_ARRAY(a, reg, offset, value) ( \
    writel((value), ((a)->hw_addr + \
        (((a)->mac_type >= e1000_82543) ? E1000_##reg : E1000_82542_##reg) + \
        ((offset) << 2))))

#define E1000_READ_REG_ARRAY(a, reg, offset) ( \
    readl((a)->hw_addr + \
        (((a)->mac_type >= e1000_82543) ? E1000_##reg : E1000_82542_##reg) + \
        ((offset) << 2)))

#define E1000_READ_REG_ARRAY_DWORD E1000_READ_REG_ARRAY
#define E1000_WRITE_REG_ARRAY_DWORD E1000_WRITE_REG_ARRAY

#define E1000_WRITE_REG_ARRAY_WORD(a, reg, offset, value) ( \
    writew((value), ((a)->hw_addr + \
        (((a)->mac_type >= e1000_82543) ? E1000_##reg : E1000_82542_##reg) + \
        ((offset) << 1))))

#define E1000_READ_REG_ARRAY_WORD(a, reg, offset) ( \
    readw((a)->hw_addr + \
        (((a)->mac_type >= e1000_82543) ? E1000_##reg : E1000_82542_##reg) + \
        ((offset) << 1)))

#define E1000_WRITE_REG_ARRAY_BYTE(a, reg, offset, value) ( \
    writeb((value), ((a)->hw_addr + \
        (((a)->mac_type >= e1000_82543) ? E1000_##reg : E1000_82542_##reg) + \
        (offset))))

#define E1000_READ_REG_ARRAY_BYTE(a, reg, offset) ( \
    readb((a)->hw_addr + \
        (((a)->mac_type >= e1000_82543) ? E1000_##reg : E1000_82542_##reg) + \
        (offset)))

#define E1000_WRITE_FLUSH() er32(STATUS)

#define E1000_WRITE_ICH_FLASH_REG(a, reg, value) ( \
    writel((value), ((a)->flash_address + reg)))

#define E1000_READ_ICH_FLASH_REG(a, reg) ( \
    readl((a)->flash_address + reg))

#define E1000_WRITE_ICH_FLASH_REG16(a, reg, value) ( \
    writew((value), ((a)->flash_address + reg)))

#define E1000_READ_ICH_FLASH_REG16(a, reg) ( \
    readw((a)->flash_address + reg))

#endif /* _E1000_OSDEP_H_ */
