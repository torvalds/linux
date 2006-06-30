/*
 * Generic library functions for the MSF (Media and Switch Fabric) unit
 * found on the Intel IXP2400 network processor.
 *
 * Copyright (C) 2004, 2005 Lennert Buytenhek <buytenh@wantstofly.org>
 * Dedicated to Marija Kulikova.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2.1 of the
 * License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <asm/hardware.h>
#include <asm/arch/ixp2000-regs.h>
#include <asm/delay.h>
#include <asm/io.h>
#include "ixp2400-msf.h"

/*
 * This is the Intel recommended PLL init procedure as described on
 * page 340 of the IXP2400/IXP2800 Programmer's Reference Manual.
 */
static void ixp2400_pll_init(struct ixp2400_msf_parameters *mp)
{
	int rx_dual_clock;
	int tx_dual_clock;
	u32 value;

	/*
	 * If the RX mode is not 1x32, we have to enable both RX PLLs
	 * (#0 and #1.)  The same thing for the TX direction.
	 */
	rx_dual_clock = !!(mp->rx_mode & IXP2400_RX_MODE_WIDTH_MASK);
	tx_dual_clock = !!(mp->tx_mode & IXP2400_TX_MODE_WIDTH_MASK);

	/*
	 * Read initial value.
	 */
	value = ixp2000_reg_read(IXP2000_MSF_CLK_CNTRL);

	/*
	 * Put PLLs in powerdown and bypass mode.
	 */
	value |= 0x0000f0f0;
	ixp2000_reg_write(IXP2000_MSF_CLK_CNTRL, value);

	/*
	 * Set single or dual clock mode bits.
	 */
	value &= ~0x03000000;
	value |= (rx_dual_clock << 24) | (tx_dual_clock << 25);

	/*
	 * Set multipliers.
	 */
	value &= ~0x00ff0000;
	value |= mp->rxclk01_multiplier << 16;
	value |= mp->rxclk23_multiplier << 18;
	value |= mp->txclk01_multiplier << 20;
	value |= mp->txclk23_multiplier << 22;

	/*
	 * And write value.
	 */
	ixp2000_reg_write(IXP2000_MSF_CLK_CNTRL, value);

	/*
	 * Disable PLL bypass mode.
	 */
	value &= ~(0x00005000 | rx_dual_clock << 13 | tx_dual_clock << 15);
	ixp2000_reg_write(IXP2000_MSF_CLK_CNTRL, value);

	/*
	 * Turn on PLLs.
	 */
	value &= ~(0x00000050 | rx_dual_clock << 5 | tx_dual_clock << 7);
	ixp2000_reg_write(IXP2000_MSF_CLK_CNTRL, value);

	/*
	 * Wait for PLLs to lock.  There are lock status bits, but IXP2400
	 * erratum #65 says that these lock bits should not be relied upon
	 * as they might not accurately reflect the true state of the PLLs.
	 */
	udelay(100);
}

/*
 * Needed according to p480 of Programmer's Reference Manual.
 */
static void ixp2400_msf_free_rbuf_entries(struct ixp2400_msf_parameters *mp)
{
	int size_bits;
	int i;

	/*
	 * Work around IXP2400 erratum #69 (silent RBUF-to-DRAM transfer
	 * corruption) in the Intel-recommended way: do not add the RBUF
	 * elements susceptible to corruption to the freelist.
	 */
	size_bits = mp->rx_mode & IXP2400_RX_MODE_RBUF_SIZE_MASK;
	if (size_bits == IXP2400_RX_MODE_RBUF_SIZE_64) {
		for (i = 1; i < 128; i++) {
			if (i == 9 || i == 18 || i == 27)
				continue;
			ixp2000_reg_write(IXP2000_MSF_RBUF_ELEMENT_DONE, i);
		}
	} else if (size_bits == IXP2400_RX_MODE_RBUF_SIZE_128) {
		for (i = 1; i < 64; i++) {
			if (i == 4 || i == 9 || i == 13)
				continue;
			ixp2000_reg_write(IXP2000_MSF_RBUF_ELEMENT_DONE, i);
		}
	} else if (size_bits == IXP2400_RX_MODE_RBUF_SIZE_256) {
		for (i = 1; i < 32; i++) {
			if (i == 2 || i == 4 || i == 6)
				continue;
			ixp2000_reg_write(IXP2000_MSF_RBUF_ELEMENT_DONE, i);
		}
	}
}

static u32 ixp2400_msf_valid_channels(u32 reg)
{
	u32 channels;

	channels = 0;
	switch (reg & IXP2400_RX_MODE_WIDTH_MASK) {
	case IXP2400_RX_MODE_1x32:
		channels = 0x1;
		if (reg & IXP2400_RX_MODE_MPHY &&
		    !(reg & IXP2400_RX_MODE_MPHY_32))
			channels = 0xf;
		break;

	case IXP2400_RX_MODE_2x16:
		channels = 0x5;
		break;

	case IXP2400_RX_MODE_4x8:
		channels = 0xf;
		break;

	case IXP2400_RX_MODE_1x16_2x8:
		channels = 0xd;
		break;
	}

	return channels;
}

static void ixp2400_msf_enable_rx(struct ixp2400_msf_parameters *mp)
{
	u32 value;

	value = ixp2000_reg_read(IXP2000_MSF_RX_CONTROL) & 0x0fffffff;
	value |= ixp2400_msf_valid_channels(mp->rx_mode) << 28;
	ixp2000_reg_write(IXP2000_MSF_RX_CONTROL, value);
}

static void ixp2400_msf_enable_tx(struct ixp2400_msf_parameters *mp)
{
	u32 value;

	value = ixp2000_reg_read(IXP2000_MSF_TX_CONTROL) & 0x0fffffff;
	value |= ixp2400_msf_valid_channels(mp->tx_mode) << 28;
	ixp2000_reg_write(IXP2000_MSF_TX_CONTROL, value);
}


void ixp2400_msf_init(struct ixp2400_msf_parameters *mp)
{
	u32 value;
	int i;

	/*
	 * Init the RX/TX PLLs based on the passed parameter block.
	 */
	ixp2400_pll_init(mp);

	/*
	 * Reset MSF.  Bit 7 in IXP_RESET_0 resets the MSF.
	 */
	value = ixp2000_reg_read(IXP2000_RESET0);
	ixp2000_reg_write(IXP2000_RESET0, value | 0x80);
	ixp2000_reg_write(IXP2000_RESET0, value & ~0x80);

	/*
	 * Initialise the RX section.
	 */
	ixp2000_reg_write(IXP2000_MSF_RX_MPHY_POLL_LIMIT, mp->rx_poll_ports - 1);
	ixp2000_reg_write(IXP2000_MSF_RX_CONTROL, mp->rx_mode);
	for (i = 0; i < 4; i++) {
		ixp2000_reg_write(IXP2000_MSF_RX_UP_CONTROL_0 + i,
						mp->rx_channel_mode[i]);
	}
	ixp2400_msf_free_rbuf_entries(mp);
	ixp2400_msf_enable_rx(mp);

	/*
	 * Initialise the TX section.
	 */
	ixp2000_reg_write(IXP2000_MSF_TX_MPHY_POLL_LIMIT, mp->tx_poll_ports - 1);
	ixp2000_reg_write(IXP2000_MSF_TX_CONTROL, mp->tx_mode);
	for (i = 0; i < 4; i++) {
		ixp2000_reg_write(IXP2000_MSF_TX_UP_CONTROL_0 + i,
						mp->tx_channel_mode[i]);
	}
	ixp2400_msf_enable_tx(mp);
}
