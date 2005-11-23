/*
 * Helper functions for the PM3386s on the Radisys ENP2611
 * Copyright (C) 2004, 2005 Lennert Buytenhek <buytenh@wantstofly.org>
 * Dedicated to Marija Kulikova.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <asm/io.h>
#include "pm3386.h"

/*
 * Read from register 'reg' of PM3386 device 'pm'.
 */
static u16 pm3386_reg_read(int pm, int reg)
{
	void *_reg;
	u16 value;

	_reg = (void *)ENP2611_PM3386_0_VIRT_BASE;
	if (pm == 1)
		_reg = (void *)ENP2611_PM3386_1_VIRT_BASE;

	value = *((volatile u16 *)(_reg + (reg << 1)));

//	printk(KERN_INFO "pm3386_reg_read(%d, %.3x) = %.8x\n", pm, reg, value);

	return value;
}

/*
 * Write to register 'reg' of PM3386 device 'pm', and perform
 * a readback from the identification register.
 */
static void pm3386_reg_write(int pm, int reg, u16 value)
{
	void *_reg;
	u16 dummy;

//	printk(KERN_INFO "pm3386_reg_write(%d, %.3x, %.8x)\n", pm, reg, value);

	_reg = (void *)ENP2611_PM3386_0_VIRT_BASE;
	if (pm == 1)
		_reg = (void *)ENP2611_PM3386_1_VIRT_BASE;

	*((volatile u16 *)(_reg + (reg << 1))) = value;

	dummy = *((volatile u16 *)_reg);
	__asm__ __volatile__("mov %0, %0" : "+r" (dummy));
}

/*
 * Read from port 'port' register 'reg', where the registers
 * for the different ports are 'spacing' registers apart.
 */
static u16 pm3386_port_reg_read(int port, int _reg, int spacing)
{
	int reg;

	reg = _reg;
	if (port & 1)
		reg += spacing;

	return pm3386_reg_read(port >> 1, reg);
}

/*
 * Write to port 'port' register 'reg', where the registers
 * for the different ports are 'spacing' registers apart.
 */
static void pm3386_port_reg_write(int port, int _reg, int spacing, u16 value)
{
	int reg;

	reg = _reg;
	if (port & 1)
		reg += spacing;

	pm3386_reg_write(port >> 1, reg, value);
}


void pm3386_reset(void)
{
	u8 mac[3][6];

	/* Save programmed MAC addresses.  */
	pm3386_get_mac(0, mac[0]);
	pm3386_get_mac(1, mac[1]);
	pm3386_get_mac(2, mac[2]);

	/* Assert analog and digital reset.  */
	pm3386_reg_write(0, 0x002, 0x0060);
	pm3386_reg_write(1, 0x002, 0x0060);
	mdelay(1);

	/* Deassert analog reset.  */
	pm3386_reg_write(0, 0x002, 0x0062);
	pm3386_reg_write(1, 0x002, 0x0062);
	mdelay(10);

	/* Deassert digital reset.  */
	pm3386_reg_write(0, 0x002, 0x0063);
	pm3386_reg_write(1, 0x002, 0x0063);
	mdelay(10);

	/* Restore programmed MAC addresses.  */
	pm3386_set_mac(0, mac[0]);
	pm3386_set_mac(1, mac[1]);
	pm3386_set_mac(2, mac[2]);

	/* Disable carrier on all ports.  */
	pm3386_set_carrier(0, 0);
	pm3386_set_carrier(1, 0);
	pm3386_set_carrier(2, 0);
}

static u16 swaph(u16 x)
{
	return ((x << 8) | (x >> 8)) & 0xffff;
}

void pm3386_init_port(int port)
{
	int pm = port >> 1;

	/*
	 * Work around ENP2611 bootloader programming MAC address
	 * in reverse.
	 */
	if (pm3386_port_reg_read(port, 0x30a, 0x100) == 0x0000 &&
	    (pm3386_port_reg_read(port, 0x309, 0x100) & 0xff00) == 0x5000) {
		u16 temp[3];

		temp[0] = pm3386_port_reg_read(port, 0x308, 0x100);
		temp[1] = pm3386_port_reg_read(port, 0x309, 0x100);
		temp[2] = pm3386_port_reg_read(port, 0x30a, 0x100);
		pm3386_port_reg_write(port, 0x308, 0x100, swaph(temp[2]));
		pm3386_port_reg_write(port, 0x309, 0x100, swaph(temp[1]));
		pm3386_port_reg_write(port, 0x30a, 0x100, swaph(temp[0]));
	}

	/*
	 * Initialise narrowbanding mode.  See application note 2010486
	 * for more information.  (@@@ We also need to issue a reset
	 * when ROOL or DOOL are detected.)
	 */
	pm3386_port_reg_write(port, 0x708, 0x10, 0xd055);
	udelay(500);
	pm3386_port_reg_write(port, 0x708, 0x10, 0x5055);

	/*
	 * SPI-3 ingress block.  Set 64 bytes SPI-3 burst size
	 * towards SPI-3 bridge.
	 */
	pm3386_port_reg_write(port, 0x122, 0x20, 0x0002);

	/*
	 * Enable ingress protocol checking, and soft reset the
	 * SPI-3 ingress block.
	 */
	pm3386_reg_write(pm, 0x103, 0x0003);
	while (!(pm3386_reg_read(pm, 0x103) & 0x80))
		;

	/*
	 * SPI-3 egress block.  Gather 12288 bytes of the current
	 * packet in the TX fifo before initiating transmit on the
	 * SERDES interface.  (Prevents TX underflows.)
	 */
	pm3386_port_reg_write(port, 0x221, 0x20, 0x0007);

	/*
	 * Enforce odd parity from the SPI-3 bridge, and soft reset
	 * the SPI-3 egress block.
	 */
	pm3386_reg_write(pm, 0x203, 0x000d & ~(4 << (port & 1)));
	while ((pm3386_reg_read(pm, 0x203) & 0x000c) != 0x000c)
		;

	/*
	 * EGMAC block.  Set this channels to reject long preambles,
	 * not send or transmit PAUSE frames, enable preamble checking,
	 * disable frame length checking, enable FCS appending, enable
	 * TX frame padding.
	 */
	pm3386_port_reg_write(port, 0x302, 0x100, 0x0113);

	/*
	 * Soft reset the EGMAC block.
	 */
	pm3386_port_reg_write(port, 0x301, 0x100, 0x8000);
	pm3386_port_reg_write(port, 0x301, 0x100, 0x0000);

	/*
	 * Auto-sense autonegotiation status.
	 */
	pm3386_port_reg_write(port, 0x306, 0x100, 0x0100);

	/*
	 * Allow reception of jumbo frames.
	 */
	pm3386_port_reg_write(port, 0x310, 0x100, 9018);

	/*
	 * Allow transmission of jumbo frames.
	 */
	pm3386_port_reg_write(port, 0x336, 0x100, 9018);

	/* @@@ Should set 0x337/0x437 (RX forwarding threshold.)  */

	/*
	 * Set autonegotiation parameters to 'no PAUSE, full duplex.'
	 */
	pm3386_port_reg_write(port, 0x31c, 0x100, 0x0020);

	/*
	 * Enable and restart autonegotiation.
	 */
	pm3386_port_reg_write(port, 0x318, 0x100, 0x0003);
	pm3386_port_reg_write(port, 0x318, 0x100, 0x0002);
}

void pm3386_get_mac(int port, u8 *mac)
{
	u16 temp;

	temp = pm3386_port_reg_read(port, 0x308, 0x100);
	mac[0] = temp & 0xff;
	mac[1] = (temp >> 8) & 0xff;

	temp = pm3386_port_reg_read(port, 0x309, 0x100);
	mac[2] = temp & 0xff;
	mac[3] = (temp >> 8) & 0xff;

	temp = pm3386_port_reg_read(port, 0x30a, 0x100);
	mac[4] = temp & 0xff;
	mac[5] = (temp >> 8) & 0xff;
}

void pm3386_set_mac(int port, u8 *mac)
{
	pm3386_port_reg_write(port, 0x308, 0x100, (mac[1] << 8) | mac[0]);
	pm3386_port_reg_write(port, 0x309, 0x100, (mac[3] << 8) | mac[2]);
	pm3386_port_reg_write(port, 0x30a, 0x100, (mac[5] << 8) | mac[4]);
}

static u32 pm3386_get_stat(int port, u16 base)
{
	u32 value;

	value = pm3386_port_reg_read(port, base, 0x100);
	value |= pm3386_port_reg_read(port, base + 1, 0x100) << 16;

	return value;
}

void pm3386_get_stats(int port, struct net_device_stats *stats)
{
	/*
	 * Snapshot statistics counters.
	 */
	pm3386_port_reg_write(port, 0x500, 0x100, 0x0001);
	while (pm3386_port_reg_read(port, 0x500, 0x100) & 0x0001)
		;

	memset(stats, 0, sizeof(*stats));

	stats->rx_packets = pm3386_get_stat(port, 0x510);
	stats->tx_packets = pm3386_get_stat(port, 0x590);
	stats->rx_bytes = pm3386_get_stat(port, 0x514);
	stats->tx_bytes = pm3386_get_stat(port, 0x594);
	/* @@@ Add other stats.  */
}

void pm3386_set_carrier(int port, int state)
{
	pm3386_port_reg_write(port, 0x703, 0x10, state ? 0x1001 : 0x0000);
}

int pm3386_is_link_up(int port)
{
	u16 temp;

	temp = pm3386_port_reg_read(port, 0x31a, 0x100);
	temp = pm3386_port_reg_read(port, 0x31a, 0x100);

	return !!(temp & 0x0002);
}

void pm3386_enable_rx(int port)
{
	u16 temp;

	temp = pm3386_port_reg_read(port, 0x303, 0x100);
	temp |= 0x1000;
	pm3386_port_reg_write(port, 0x303, 0x100, temp);
}

void pm3386_disable_rx(int port)
{
	u16 temp;

	temp = pm3386_port_reg_read(port, 0x303, 0x100);
	temp &= 0xefff;
	pm3386_port_reg_write(port, 0x303, 0x100, temp);
}

void pm3386_enable_tx(int port)
{
	u16 temp;

	temp = pm3386_port_reg_read(port, 0x303, 0x100);
	temp |= 0x4000;
	pm3386_port_reg_write(port, 0x303, 0x100, temp);
}

void pm3386_disable_tx(int port)
{
	u16 temp;

	temp = pm3386_port_reg_read(port, 0x303, 0x100);
	temp &= 0xbfff;
	pm3386_port_reg_write(port, 0x303, 0x100, temp);
}

MODULE_LICENSE("GPL");
