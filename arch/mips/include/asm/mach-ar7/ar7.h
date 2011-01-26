/*
 * Copyright (C) 2006,2007 Felix Fietkau <nbd@openwrt.org>
 * Copyright (C) 2006,2007 Eugene Konev <ejka@openwrt.org>
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

#ifndef __AR7_H__
#define __AR7_H__

#include <linux/delay.h>
#include <linux/io.h>
#include <linux/errno.h>

#include <asm/addrspace.h>

#define AR7_SDRAM_BASE	0x14000000

#define AR7_REGS_BASE	0x08610000

#define AR7_REGS_MAC0	(AR7_REGS_BASE + 0x0000)
#define AR7_REGS_GPIO	(AR7_REGS_BASE + 0x0900)
/* 0x08610A00 - 0x08610BFF (512 bytes, 128 bytes / clock) */
#define AR7_REGS_POWER	(AR7_REGS_BASE + 0x0a00)
#define AR7_REGS_CLOCKS (AR7_REGS_POWER + 0x80)
#define UR8_REGS_CLOCKS (AR7_REGS_POWER + 0x20)
#define AR7_REGS_UART0	(AR7_REGS_BASE + 0x0e00)
#define AR7_REGS_USB	(AR7_REGS_BASE + 0x1200)
#define AR7_REGS_RESET	(AR7_REGS_BASE + 0x1600)
#define AR7_REGS_PINSEL (AR7_REGS_BASE + 0x160C)
#define AR7_REGS_VLYNQ0	(AR7_REGS_BASE + 0x1800)
#define AR7_REGS_DCL	(AR7_REGS_BASE + 0x1a00)
#define AR7_REGS_VLYNQ1	(AR7_REGS_BASE + 0x1c00)
#define AR7_REGS_MDIO	(AR7_REGS_BASE + 0x1e00)
#define AR7_REGS_IRQ	(AR7_REGS_BASE + 0x2400)
#define AR7_REGS_MAC1	(AR7_REGS_BASE + 0x2800)

#define AR7_REGS_WDT	(AR7_REGS_BASE + 0x1f00)
#define UR8_REGS_WDT	(AR7_REGS_BASE + 0x0b00)
#define UR8_REGS_UART1	(AR7_REGS_BASE + 0x0f00)

/* Titan registers */
#define TITAN_REGS_ESWITCH_BASE	(0x08640000)
#define TITAN_REGS_MAC0		(TITAN_REGS_ESWITCH_BASE)
#define TITAN_REGS_MAC1		(TITAN_REGS_ESWITCH_BASE + 0x0800)
#define TITAN_REGS_MDIO		(TITAN_REGS_ESWITCH_BASE + 0x02000)
#define TITAN_REGS_VLYNQ0	(AR7_REGS_BASE + 0x1c00)
#define TITAN_REGS_VLYNQ1	(AR7_REGS_BASE + 0x1300)

#define AR7_RESET_PERIPHERAL	0x0
#define AR7_RESET_SOFTWARE	0x4
#define AR7_RESET_STATUS	0x8

#define AR7_RESET_BIT_CPMAC_LO	17
#define AR7_RESET_BIT_CPMAC_HI	21
#define AR7_RESET_BIT_MDIO	22
#define AR7_RESET_BIT_EPHY	26

#define TITAN_RESET_BIT_EPHY1	28

/* GPIO control registers */
#define AR7_GPIO_INPUT	0x0
#define AR7_GPIO_OUTPUT	0x4
#define AR7_GPIO_DIR	0x8
#define AR7_GPIO_ENABLE	0xc
#define TITAN_GPIO_INPUT_0	0x0
#define TITAN_GPIO_INPUT_1	0x4
#define TITAN_GPIO_OUTPUT_0	0x8
#define TITAN_GPIO_OUTPUT_1	0xc
#define TITAN_GPIO_DIR_0	0x10
#define TITAN_GPIO_DIR_1	0x14
#define TITAN_GPIO_ENBL_0	0x18
#define TITAN_GPIO_ENBL_1	0x1c

#define AR7_CHIP_7100	0x18
#define AR7_CHIP_7200	0x2b
#define AR7_CHIP_7300	0x05
#define AR7_CHIP_TITAN	0x07
#define TITAN_CHIP_1050	0x0f
#define TITAN_CHIP_1055	0x0e
#define TITAN_CHIP_1056	0x0d
#define TITAN_CHIP_1060	0x07

/* Interrupts */
#define AR7_IRQ_UART0	15
#define AR7_IRQ_UART1	16

/* Clocks */
#define AR7_AFE_CLOCK	35328000
#define AR7_REF_CLOCK	25000000
#define AR7_XTAL_CLOCK	24000000

/* DCL */
#define AR7_WDT_HW_ENA	0x10

struct plat_cpmac_data {
	int reset_bit;
	int power_bit;
	u32 phy_mask;
	char dev_addr[6];
};

struct plat_dsl_data {
	int reset_bit_dsl;
	int reset_bit_sar;
};

extern int ar7_cpu_clock, ar7_bus_clock, ar7_dsp_clock;

static inline int ar7_is_titan(void)
{
	return (readl((void *)KSEG1ADDR(AR7_REGS_GPIO + 0x24)) & 0xffff) ==
		AR7_CHIP_TITAN;
}

static inline u16 ar7_chip_id(void)
{
	return ar7_is_titan() ? AR7_CHIP_TITAN : (readl((void *)
		KSEG1ADDR(AR7_REGS_GPIO + 0x14)) & 0xffff);
}

static inline u16 titan_chip_id(void)
{
	unsigned int val = readl((void *)KSEG1ADDR(AR7_REGS_GPIO +
						TITAN_GPIO_INPUT_1));
	return ((val >> 12) & 0x0f);
}

static inline u8 ar7_chip_rev(void)
{
	return (readl((void *)KSEG1ADDR(AR7_REGS_GPIO + (ar7_is_titan() ? 0x24 :
		0x14))) >> 16) & 0xff;
}

struct clk {
	unsigned int	rate;
};

static inline int ar7_has_high_cpmac(void)
{
	u16 chip_id = ar7_chip_id();
	switch (chip_id) {
	case AR7_CHIP_7100:
	case AR7_CHIP_7200:
		return 0;
	case AR7_CHIP_7300:
		return 1;
	default:
		return -ENXIO;
	}
}
#define ar7_has_high_vlynq ar7_has_high_cpmac
#define ar7_has_second_uart ar7_has_high_cpmac

static inline void ar7_device_enable(u32 bit)
{
	void *reset_reg =
		(void *)KSEG1ADDR(AR7_REGS_RESET + AR7_RESET_PERIPHERAL);
	writel(readl(reset_reg) | (1 << bit), reset_reg);
	msleep(20);
}

static inline void ar7_device_disable(u32 bit)
{
	void *reset_reg =
		(void *)KSEG1ADDR(AR7_REGS_RESET + AR7_RESET_PERIPHERAL);
	writel(readl(reset_reg) & ~(1 << bit), reset_reg);
	msleep(20);
}

static inline void ar7_device_reset(u32 bit)
{
	ar7_device_disable(bit);
	ar7_device_enable(bit);
}

static inline void ar7_device_on(u32 bit)
{
	void *power_reg = (void *)KSEG1ADDR(AR7_REGS_POWER);
	writel(readl(power_reg) | (1 << bit), power_reg);
	msleep(20);
}

static inline void ar7_device_off(u32 bit)
{
	void *power_reg = (void *)KSEG1ADDR(AR7_REGS_POWER);
	writel(readl(power_reg) & ~(1 << bit), power_reg);
	msleep(20);
}

int __init ar7_gpio_init(void);
void __init ar7_init_clocks(void);

#endif /* __AR7_H__ */
