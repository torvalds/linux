/*
 * Agere Systems Inc.
 * 10/100/1000 Base-T Ethernet Driver for the ET1301 and ET131x series MACs
 *
 * Copyright * 2005 Agere Systems Inc.
 * All rights reserved.
 *   http://www.agere.com
 *
 *------------------------------------------------------------------------------
 *
 * et1310_phy.c - Routines for configuring and accessing the PHY
 *
 *------------------------------------------------------------------------------
 *
 * SOFTWARE LICENSE
 *
 * This software is provided subject to the following terms and conditions,
 * which you should read carefully before using the software.  Using this
 * software indicates your acceptance of these terms and conditions.  If you do
 * not agree with these terms and conditions, do not use the software.
 *
 * Copyright * 2005 Agere Systems Inc.
 * All rights reserved.
 *
 * Redistribution and use in source or binary forms, with or without
 * modifications, are permitted provided that the following conditions are met:
 *
 * . Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following Disclaimer as comments in the code as
 *    well as in the documentation and/or other materials provided with the
 *    distribution.
 *
 * . Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following Disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * . Neither the name of Agere Systems Inc. nor the names of the contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * Disclaimer
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, INFRINGEMENT AND THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  ANY
 * USE, MODIFICATION OR DISTRIBUTION OF THIS SOFTWARE IS SOLELY AT THE USERS OWN
 * RISK. IN NO EVENT SHALL AGERE SYSTEMS INC. OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, INCLUDING, BUT NOT LIMITED TO, CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 *
 */

#include "et131x_version.h"
#include "et131x_defs.h"

#include <linux/pci.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>

#include <linux/sched.h>
#include <linux/ptrace.h>
#include <linux/ctype.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/in.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/bitops.h>
#include <asm/system.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/if_arp.h>
#include <linux/ioport.h>
#include <linux/random.h>

#include "et1310_phy.h"

#include "et131x_adapter.h"

#include "et1310_address_map.h"
#include "et1310_tx.h"
#include "et1310_rx.h"

#include "et131x.h"

/**
 * et131x_phy_mii_read - Read from the PHY through the MII Interface on the MAC
 * @adapter: pointer to our private adapter structure
 * @xcvr_addr: the address of the transceiver
 * @xcvr_reg: the register to read
 * @value: pointer to a 16-bit value in which the value will be stored
 *
 * Returns 0 on success, errno on failure (as defined in errno.h)
 */
int et131x_phy_mii_read(struct et131x_adapter *adapter, u8 xcvr_addr,
	      u8 xcvr_reg, u16 *value)
{
	struct mac_regs __iomem *mac = &adapter->regs->mac;
	int status = 0;
	u32 delay;
	u32 mii_addr;
	u32 mii_cmd;
	u32 mii_indicator;

	/* Save a local copy of the registers we are dealing with so we can
	 * set them back
	 */
	mii_addr = readl(&mac->mii_mgmt_addr);
	mii_cmd = readl(&mac->mii_mgmt_cmd);

	/* Stop the current operation */
	writel(0, &mac->mii_mgmt_cmd);

	/* Set up the register we need to read from on the correct PHY */
	writel(MII_ADDR(xcvr_addr, xcvr_reg), &mac->mii_mgmt_addr);

	/* Kick the read cycle off */
	delay = 0;

	writel(0x1, &mac->mii_mgmt_cmd);

	do {
		udelay(50);
		delay++;
		mii_indicator = readl(&mac->mii_mgmt_indicator);
	} while ((mii_indicator & MGMT_WAIT) && delay < 50);

	/* If we hit the max delay, we could not read the register */
	if (delay == 50) {
		dev_warn(&adapter->pdev->dev,
			    "xcvrReg 0x%08x could not be read\n", xcvr_reg);
		dev_warn(&adapter->pdev->dev, "status is  0x%08x\n",
			    mii_indicator);

		status = -EIO;
	}

	/* If we hit here we were able to read the register and we need to
	 * return the value to the caller */
	*value = readl(&mac->mii_mgmt_stat) & 0xFFFF;

	/* Stop the read operation */
	writel(0, &mac->mii_mgmt_cmd);

	/* set the registers we touched back to the state at which we entered
	 * this function
	 */
	writel(mii_addr, &mac->mii_mgmt_addr);
	writel(mii_cmd, &mac->mii_mgmt_cmd);

	return status;
}

/**
 * et131x_mii_write - Write to a PHY register through the MII interface of the MAC
 * @adapter: pointer to our private adapter structure
 * @xcvr_reg: the register to read
 * @value: 16-bit value to write
 *
 * FIXME: one caller in netdev still
 *
 * Return 0 on success, errno on failure (as defined in errno.h)
 */
int et131x_mii_write(struct et131x_adapter *adapter, u8 xcvr_reg, u16 value)
{
	struct mac_regs __iomem *mac = &adapter->regs->mac;
	int status = 0;
	u8 xcvr_addr = adapter->stats.xcvr_addr;
	u32 delay;
	u32 mii_addr;
	u32 mii_cmd;
	u32 mii_indicator;

	/* Save a local copy of the registers we are dealing with so we can
	 * set them back
	 */
	mii_addr = readl(&mac->mii_mgmt_addr);
	mii_cmd = readl(&mac->mii_mgmt_cmd);

	/* Stop the current operation */
	writel(0, &mac->mii_mgmt_cmd);

	/* Set up the register we need to write to on the correct PHY */
	writel(MII_ADDR(xcvr_addr, xcvr_reg), &mac->mii_mgmt_addr);

	/* Add the value to write to the registers to the mac */
	writel(value, &mac->mii_mgmt_ctrl);
	delay = 0;

	do {
		udelay(50);
		delay++;
		mii_indicator = readl(&mac->mii_mgmt_indicator);
	} while ((mii_indicator & MGMT_BUSY) && delay < 100);

	/* If we hit the max delay, we could not write the register */
	if (delay == 100) {
		u16 tmp;

		dev_warn(&adapter->pdev->dev,
		    "xcvrReg 0x%08x could not be written", xcvr_reg);
		dev_warn(&adapter->pdev->dev, "status is  0x%08x\n",
			    mii_indicator);
		dev_warn(&adapter->pdev->dev, "command is  0x%08x\n",
			    readl(&mac->mii_mgmt_cmd));

		et131x_mii_read(adapter, xcvr_reg, &tmp);

		status = -EIO;
	}
	/* Stop the write operation */
	writel(0, &mac->mii_mgmt_cmd);

	/*
	 * set the registers we touched back to the state at which we entered
	 * this function
	 */
	writel(mii_addr, &mac->mii_mgmt_addr);
	writel(mii_cmd, &mac->mii_mgmt_cmd);

	return status;
}

/**
 * et131x_xcvr_find - Find the PHY ID
 * @adapter: pointer to our private adapter structure
 *
 * Returns 0 on success, errno on failure (as defined in errno.h)
 */
int et131x_xcvr_find(struct et131x_adapter *adapter)
{
	u8 xcvr_addr;
	u16 idr1;
	u16 idr2;

	/* We need to get xcvr id and address we just get the first one */
	for (xcvr_addr = 0; xcvr_addr < 32; xcvr_addr++) {
		/* Read the ID from the PHY */
		et131x_phy_mii_read(adapter, xcvr_addr,
			     (u8) offsetof(struct mi_regs, idr1),
			     &idr1);
		et131x_phy_mii_read(adapter, xcvr_addr,
			     (u8) offsetof(struct mi_regs, idr2),
			     &idr2);

		if (idr1 != 0 && idr1 != 0xffff) {
			adapter->stats.xcvr_addr = xcvr_addr;
			return 0;
		}
	}
	return -ENODEV;
}

void et1310_phy_reset(struct et131x_adapter *adapter)
{
	et131x_mii_write(adapter, PHY_CONTROL, 0x8000);
}

/**
 *	et1310_phy_power_down	-	PHY power control
 *	@adapter: device to control
 *	@down: true for off/false for back on
 *
 *	one hundred, ten, one thousand megs
 *	How would you like to have your LAN accessed
 *	Can't you see that this code processed
 *	Phy power, phy power..
 */
void et1310_phy_power_down(struct et131x_adapter *adapter, bool down)
{
	u16 data;

	et131x_mii_read(adapter, PHY_CONTROL, &data);
	data &= ~0x0800;	/* Power UP */
	if (down) /* Power DOWN */
		data |= 0x0800;
	et131x_mii_write(adapter, PHY_CONTROL, data);
}

/**
 *	et1310_phy_auto_neg	-	autonegotiate control
 *	@adapter: device to control
 *	@enabe: autoneg on/off
 *
 *	Set up the autonegotiation state according to whether we will be
 *	negotiating the state or forcing a speed.
 */
static void et1310_phy_auto_neg(struct et131x_adapter *adapter, bool enable)
{
	u16 data;

	et131x_mii_read(adapter, PHY_CONTROL, &data);
	data &= ~0x1000;	/* Autonegotiation OFF */
	if (enable)
		data |= 0x1000;		/* Autonegotiation ON */
	et131x_mii_write(adapter, PHY_CONTROL, data);
}

/**
 *	et1310_phy_duplex_mode	-	duplex control
 *	@adapter: device to control
 *	@duplex: duplex on/off
 *
 *	Set up the duplex state on the PHY
 */
static void et1310_phy_duplex_mode(struct et131x_adapter *adapter, u16 duplex)
{
	u16 data;

	et131x_mii_read(adapter, PHY_CONTROL, &data);
	data &= ~0x100;		/* Set Half Duplex */
	if (duplex == TRUEPHY_DUPLEX_FULL)
		data |= 0x100;	/* Set Full Duplex */
	et131x_mii_write(adapter, PHY_CONTROL, data);
}

/**
 *	et1310_phy_speed_select	-	speed control
 *	@adapter: device to control
 *	@duplex: duplex on/off
 *
 *	Set the speed of our PHY.
 */
static void et1310_phy_speed_select(struct et131x_adapter *adapter, u16 speed)
{
	u16 data;
	static const u16 bits[3] = {0x0000, 0x2000, 0x0040};

	/* Read the PHY control register */
	et131x_mii_read(adapter, PHY_CONTROL, &data);
	/* Clear all Speed settings (Bits 6, 13) */
	data &= ~0x2040;
	/* Write back the new speed */
	et131x_mii_write(adapter, PHY_CONTROL, data | bits[speed]);
}

/**
 *	et1310_phy_link_status	-	read link state
 *	@adapter: device to read
 *	@link_status: reported link state
 *	@autoneg: reported autonegotiation state (complete/incomplete/disabled)
 *	@linkspeed: returnedlink speed in use
 *	@duplex_mode: reported half/full duplex state
 *	@mdi_mdix: not yet working
 *	@masterslave: report whether we are master or slave
 *	@polarity: link polarity
 *
 *	I can read your lan like a magazine
 *	I see if your up
 *	I know your link speed
 *	I see all the setting that you'd rather keep
 */
static void et1310_phy_link_status(struct et131x_adapter *adapter,
			  u8 *link_status,
			  u32 *autoneg,
			  u32 *linkspeed,
			  u32 *duplex_mode,
			  u32 *mdi_mdix,
			  u32 *masterslave, u32 *polarity)
{
	u16 mistatus = 0;
	u16 is1000BaseT = 0;
	u16 vmi_phystatus = 0;
	u16 control = 0;

	et131x_mii_read(adapter, PHY_STATUS, &mistatus);
	et131x_mii_read(adapter, PHY_1000_STATUS, &is1000BaseT);
	et131x_mii_read(adapter, PHY_PHY_STATUS, &vmi_phystatus);
	et131x_mii_read(adapter, PHY_CONTROL, &control);

	*link_status = (vmi_phystatus & 0x0040) ? 1 : 0;
	*autoneg = (control & 0x1000) ? ((vmi_phystatus & 0x0020) ?
					    TRUEPHY_ANEG_COMPLETE :
					    TRUEPHY_ANEG_NOT_COMPLETE) :
		    TRUEPHY_ANEG_DISABLED;
	*linkspeed = (vmi_phystatus & 0x0300) >> 8;
	*duplex_mode = (vmi_phystatus & 0x0080) >> 7;
	/* NOTE: Need to complete this */
	*mdi_mdix = 0;

	*masterslave = (is1000BaseT & 0x4000) ?
			TRUEPHY_CFG_MASTER : TRUEPHY_CFG_SLAVE;
	*polarity = (vmi_phystatus & 0x0400) ?
			TRUEPHY_POLARITY_INVERTED : TRUEPHY_POLARITY_NORMAL;
}

static void et1310_phy_and_or_reg(struct et131x_adapter *adapter,
				  u16 regnum, u16 and_mask, u16 or_mask)
{
	u16 reg;

	et131x_mii_read(adapter, regnum, &reg);
	reg &= and_mask;
	reg |= or_mask;
	et131x_mii_write(adapter, regnum, reg);
}

/* Still used from _mac for BIT_READ */
void et1310_phy_access_mii_bit(struct et131x_adapter *adapter, u16 action,
			       u16 regnum, u16 bitnum, u8 *value)
{
	u16 reg;
	u16 mask = 0x0001 << bitnum;

	/* Read the requested register */
	et131x_mii_read(adapter, regnum, &reg);

	switch (action) {
	case TRUEPHY_BIT_READ:
		*value = (reg & mask) >> bitnum;
		break;

	case TRUEPHY_BIT_SET:
		et131x_mii_write(adapter, regnum, reg | mask);
		break;

	case TRUEPHY_BIT_CLEAR:
		et131x_mii_write(adapter, regnum, reg & ~mask);
		break;

	default:
		break;
	}
}

void et1310_phy_advertise_1000BaseT(struct et131x_adapter *adapter,
				  u16 duplex)
{
	u16 data;

	/* Read the PHY 1000 Base-T Control Register */
	et131x_mii_read(adapter, PHY_1000_CONTROL, &data);

	/* Clear Bits 8,9 */
	data &= ~0x0300;

	switch (duplex) {
	case TRUEPHY_ADV_DUPLEX_NONE:
		/* Duplex already cleared, do nothing */
		break;

	case TRUEPHY_ADV_DUPLEX_FULL:
		/* Set Bit 9 */
		data |= 0x0200;
		break;

	case TRUEPHY_ADV_DUPLEX_HALF:
		/* Set Bit 8 */
		data |= 0x0100;
		break;

	case TRUEPHY_ADV_DUPLEX_BOTH:
	default:
		data |= 0x0300;
		break;
	}

	/* Write back advertisement */
	et131x_mii_write(adapter, PHY_1000_CONTROL, data);
}

static void et1310_phy_advertise_100BaseT(struct et131x_adapter *adapter,
					  u16 duplex)
{
	u16 data;

	/* Read the Autonegotiation Register (10/100) */
	et131x_mii_read(adapter, PHY_AUTO_ADVERTISEMENT, &data);

	/* Clear bits 7,8 */
	data &= ~0x0180;

	switch (duplex) {
	case TRUEPHY_ADV_DUPLEX_NONE:
		/* Duplex already cleared, do nothing */
		break;

	case TRUEPHY_ADV_DUPLEX_FULL:
		/* Set Bit 8 */
		data |= 0x0100;
		break;

	case TRUEPHY_ADV_DUPLEX_HALF:
		/* Set Bit 7 */
		data |= 0x0080;
		break;

	case TRUEPHY_ADV_DUPLEX_BOTH:
	default:
		/* Set Bits 7,8 */
		data |= 0x0180;
		break;
	}

	/* Write back advertisement */
	et131x_mii_write(adapter, PHY_AUTO_ADVERTISEMENT, data);
}

static void et1310_phy_advertise_10BaseT(struct et131x_adapter *adapter,
				u16 duplex)
{
	u16 data;

	/* Read the Autonegotiation Register (10/100) */
	et131x_mii_read(adapter, PHY_AUTO_ADVERTISEMENT, &data);

	/* Clear bits 5,6 */
	data &= ~0x0060;

	switch (duplex) {
	case TRUEPHY_ADV_DUPLEX_NONE:
		/* Duplex already cleared, do nothing */
		break;

	case TRUEPHY_ADV_DUPLEX_FULL:
		/* Set Bit 6 */
		data |= 0x0040;
		break;

	case TRUEPHY_ADV_DUPLEX_HALF:
		/* Set Bit 5 */
		data |= 0x0020;
		break;

	case TRUEPHY_ADV_DUPLEX_BOTH:
	default:
		/* Set Bits 5,6 */
		data |= 0x0060;
		break;
	}

	/* Write back advertisement */
	et131x_mii_write(adapter, PHY_AUTO_ADVERTISEMENT, data);
}

/**
 * et131x_xcvr_init - Init the phy if we are setting it into force mode
 * @adapter: pointer to our private adapter structure
 *
 */
static void et131x_xcvr_init(struct et131x_adapter *adapter)
{
	u16 imr;
	u16 isr;
	u16 lcr2;

	/* Zero out the adapter structure variable representing BMSR */
	adapter->bmsr = 0;

	et131x_mii_read(adapter, (u8) offsetof(struct mi_regs, isr), &isr);
	et131x_mii_read(adapter, (u8) offsetof(struct mi_regs, imr), &imr);

	/* Set the link status interrupt only.  Bad behavior when link status
	 * and auto neg are set, we run into a nested interrupt problem
	 */
	imr |= 0x0105;

	et131x_mii_write(adapter, (u8) offsetof(struct mi_regs, imr), imr);

	/* Set the LED behavior such that LED 1 indicates speed (off =
	 * 10Mbits, blink = 100Mbits, on = 1000Mbits) and LED 2 indicates
	 * link and activity (on for link, blink off for activity).
	 *
	 * NOTE: Some customizations have been added here for specific
	 * vendors; The LED behavior is now determined by vendor data in the
	 * EEPROM. However, the above description is the default.
	 */
	if ((adapter->eeprom_data[1] & 0x4) == 0) {
		et131x_mii_read(adapter, (u8) offsetof(struct mi_regs, lcr2),
		       &lcr2);

		lcr2 &= 0x00FF;
		lcr2 |= 0xA000;	/* led link */

		if ((adapter->eeprom_data[1] & 0x8) == 0)
			lcr2 |= 0x0300;
		else
			lcr2 |= 0x0400;

		et131x_mii_write(adapter, (u8) offsetof(struct mi_regs, lcr2),
			lcr2);
	}

	/* Determine if we need to go into a force mode and set it */
	if (adapter->ai_force_speed == 0 && adapter->ai_force_duplex == 0) {
		if (adapter->wanted_flow == FLOW_TXONLY ||
		    adapter->wanted_flow == FLOW_BOTH)
			et1310_phy_access_mii_bit(adapter,
					      TRUEPHY_BIT_SET, 4, 11, NULL);
		else
			et1310_phy_access_mii_bit(adapter,
					      TRUEPHY_BIT_CLEAR, 4, 11, NULL);

		if (adapter->wanted_flow == FLOW_BOTH)
			et1310_phy_access_mii_bit(adapter,
					      TRUEPHY_BIT_SET, 4, 10, NULL);
		else
			et1310_phy_access_mii_bit(adapter,
					      TRUEPHY_BIT_CLEAR, 4, 10, NULL);

		/* Set the phy to autonegotiation */
		et1310_phy_auto_neg(adapter, true);

		/* NOTE - Do we need this? */
		et1310_phy_access_mii_bit(adapter, TRUEPHY_BIT_SET, 0, 9, NULL);
		return;
	}

	et1310_phy_auto_neg(adapter, false);

	/* Set to the correct force mode. */
	if (adapter->ai_force_duplex != 1) {
		if (adapter->wanted_flow == FLOW_TXONLY ||
		    adapter->wanted_flow == FLOW_BOTH)
			et1310_phy_access_mii_bit(adapter,
				      TRUEPHY_BIT_SET, 4, 11, NULL);
		else
			et1310_phy_access_mii_bit(adapter,
					      TRUEPHY_BIT_CLEAR, 4, 11, NULL);

		if (adapter->wanted_flow == FLOW_BOTH)
			et1310_phy_access_mii_bit(adapter,
					      TRUEPHY_BIT_SET, 4, 10, NULL);
		else
			et1310_phy_access_mii_bit(adapter,
					      TRUEPHY_BIT_CLEAR, 4, 10, NULL);
	} else {
		et1310_phy_access_mii_bit(adapter, TRUEPHY_BIT_CLEAR,
					  4, 10, NULL);
		et1310_phy_access_mii_bit(adapter, TRUEPHY_BIT_CLEAR,
					  4, 11, NULL);
	}
	et1310_phy_power_down(adapter, 1);
	switch (adapter->ai_force_speed) {
	case 10:
		/* First we need to turn off all other advertisement */
		et1310_phy_advertise_1000BaseT(adapter, TRUEPHY_ADV_DUPLEX_NONE);
		et1310_phy_advertise_100BaseT(adapter, TRUEPHY_ADV_DUPLEX_NONE);
		if (adapter->ai_force_duplex == 1) {
			/* Set our advertise values accordingly */
			et1310_phy_advertise_10BaseT(adapter,
						TRUEPHY_ADV_DUPLEX_HALF);
		} else if (adapter->ai_force_duplex == 2) {
			/* Set our advertise values accordingly */
			et1310_phy_advertise_10BaseT(adapter,
						TRUEPHY_ADV_DUPLEX_FULL);
		} else {
			/* Disable autoneg */
			et1310_phy_auto_neg(adapter, false);
			/* Disable rest of the advertisements */
			et1310_phy_advertise_10BaseT(adapter,
					TRUEPHY_ADV_DUPLEX_NONE);
			/* Force 10 Mbps */
			et1310_phy_speed_select(adapter, TRUEPHY_SPEED_10MBPS);
			/* Force Full duplex */
			et1310_phy_duplex_mode(adapter, TRUEPHY_DUPLEX_FULL);
		}
		break;
	case 100:
		/* first we need to turn off all other advertisement */
		et1310_phy_advertise_1000BaseT(adapter, TRUEPHY_ADV_DUPLEX_NONE);
		et1310_phy_advertise_10BaseT(adapter, TRUEPHY_ADV_DUPLEX_NONE);
		if (adapter->ai_force_duplex == 1) {
			/* Set our advertise values accordingly */
			et1310_phy_advertise_100BaseT(adapter,
						TRUEPHY_ADV_DUPLEX_HALF);
			/* Set speed */
			et1310_phy_speed_select(adapter, TRUEPHY_SPEED_100MBPS);
		} else if (adapter->ai_force_duplex == 2) {
			/* Set our advertise values accordingly */
			et1310_phy_advertise_100BaseT(adapter,
						TRUEPHY_ADV_DUPLEX_FULL);
		} else {
			/* Disable autoneg */
			et1310_phy_auto_neg(adapter, false);
			/* Disable other advertisement */
			et1310_phy_advertise_100BaseT(adapter,
						TRUEPHY_ADV_DUPLEX_NONE);
			/* Force 100 Mbps */
			et1310_phy_speed_select(adapter, TRUEPHY_SPEED_100MBPS);
			/* Force Full duplex */
			et1310_phy_duplex_mode(adapter, TRUEPHY_DUPLEX_FULL);
		}
		break;
	case 1000:
		/* first we need to turn off all other advertisement */
		et1310_phy_advertise_100BaseT(adapter, TRUEPHY_ADV_DUPLEX_NONE);
		et1310_phy_advertise_10BaseT(adapter, TRUEPHY_ADV_DUPLEX_NONE);
		/* set our advertise values accordingly */
		et1310_phy_advertise_1000BaseT(adapter, TRUEPHY_ADV_DUPLEX_FULL);
		break;
	}
	et1310_phy_power_down(adapter, 0);
}

/**
 * et131x_setphy_normal - Set PHY for normal operation.
 * @adapter: pointer to our private adapter structure
 *
 * Used by Power Management to force the PHY into 10 Base T half-duplex mode,
 * when going to D3 in WOL mode. Also used during initialization to set the
 * PHY for normal operation.
 */
void et131x_setphy_normal(struct et131x_adapter *adapter)
{
	/* Make sure the PHY is powered up */
	et1310_phy_power_down(adapter, 0);
	et131x_xcvr_init(adapter);
}

void et131x_mii_check(struct et131x_adapter *adapter,
		      u16 bmsr, u16 bmsr_ints)
{
	u8 link_status;
	u32 autoneg_status;
	u32 speed;
	u32 duplex;
	u32 mdi_mdix;
	u32 masterslave;
	u32 polarity;

	if (bmsr_ints & MI_BMSR_LINK_STATUS) {
		if (bmsr & MI_BMSR_LINK_STATUS) {
			adapter->boot_coma = 20;
			netif_carrier_on(adapter->netdev);
		} else {
			dev_warn(&adapter->pdev->dev,
			    "Link down - cable problem ?\n");

			if (adapter->linkspeed == TRUEPHY_SPEED_10MBPS) {
				/* NOTE - Is there a way to query this without
				 * TruePHY?
				 * && TRU_QueryCoreType(adapter->hTruePhy, 0) ==
				 * EMI_TRUEPHY_A13O) {
				 */
				u16 register18;

				et131x_mii_read(adapter, 0x12, &register18);
				et131x_mii_write(adapter, 0x12, register18 | 0x4);
				et131x_mii_write(adapter, 0x10,
						 register18 | 0x8402);
				et131x_mii_write(adapter, 0x11, register18 | 511);
				et131x_mii_write(adapter, 0x12, register18);
			}

			netif_carrier_off(adapter->netdev);

			adapter->linkspeed = 0;
			adapter->duplex_mode = 0;

			/* Free the packets being actively sent & stopped */
			et131x_free_busy_send_packets(adapter);

			/* Re-initialize the send structures */
			et131x_init_send(adapter);

			/* Reset the RFD list and re-start RU */
			et131x_reset_recv(adapter);

			/*
			 * Bring the device back to the state it was during
			 * init prior to autonegotiation being complete. This
			 * way, when we get the auto-neg complete interrupt,
			 * we can complete init by calling config_mac_regs2.
			 */
			et131x_soft_reset(adapter);

			/* Setup ET1310 as per the documentation */
			et131x_adapter_setup(adapter);

			/* Setup the PHY into coma mode until the cable is
			 * plugged back in
			 */
			if (adapter->registry_phy_coma == 1)
				et1310_enable_phy_coma(adapter);
		}
	}

	if ((bmsr_ints & MI_BMSR_AUTO_NEG_COMPLETE) ||
	   (adapter->ai_force_duplex == 3 && (bmsr_ints & MI_BMSR_LINK_STATUS))) {
		if ((bmsr & MI_BMSR_AUTO_NEG_COMPLETE) ||
		    adapter->ai_force_duplex == 3) {
			et1310_phy_link_status(adapter,
					     &link_status, &autoneg_status,
					     &speed, &duplex, &mdi_mdix,
					     &masterslave, &polarity);

			adapter->linkspeed = speed;
			adapter->duplex_mode = duplex;

			adapter->boot_coma = 20;

			if (adapter->linkspeed == TRUEPHY_SPEED_10MBPS) {
				/*
				 * NOTE - Is there a way to query this without
				 * TruePHY?
				 * && TRU_QueryCoreType(adapter->hTruePhy, 0)==
				 * EMI_TRUEPHY_A13O) {
				 */
				u16 register18;

				et131x_mii_read(adapter, 0x12, &register18);
				et131x_mii_write(adapter, 0x12, register18 | 0x4);
				et131x_mii_write(adapter, 0x10,
						 register18 | 0x8402);
				et131x_mii_write(adapter, 0x11, register18 | 511);
				et131x_mii_write(adapter, 0x12, register18);
			}

			et1310_config_flow_control(adapter);

			if (adapter->linkspeed == TRUEPHY_SPEED_1000MBPS &&
					adapter->registry_jumbo_packet > 2048)
				et1310_phy_and_or_reg(adapter, 0x16, 0xcfff,
								   0x2000);

			et131x_set_rx_dma_timer(adapter);
			et1310_config_mac_regs2(adapter);
		}
	}
}

/*
 * The routines which follow provide low-level access to the PHY, and are used
 * primarily by the routines above (although there are a few places elsewhere
 * in the driver where this level of access is required).
 */
static const u16 config_phy[25][2] = {
	/* Reg	 Value		Register */
	/* Addr                         */
	{0x880B, 0x0926},	/* AfeIfCreg4B1000Msbs */
	{0x880C, 0x0926},	/* AfeIfCreg4B100Msbs */
	{0x880D, 0x0926},	/* AfeIfCreg4B10Msbs */

	{0x880E, 0xB4D3},	/* AfeIfCreg4B1000Lsbs */
	{0x880F, 0xB4D3},	/* AfeIfCreg4B100Lsbs */
	{0x8810, 0xB4D3},	/* AfeIfCreg4B10Lsbs */

	{0x8805, 0xB03E},	/* AfeIfCreg3B1000Msbs */
	{0x8806, 0xB03E},	/* AfeIfCreg3B100Msbs */
	{0x8807, 0xFF00},	/* AfeIfCreg3B10Msbs */

	{0x8808, 0xE090},	/* AfeIfCreg3B1000Lsbs */
	{0x8809, 0xE110},	/* AfeIfCreg3B100Lsbs */
	{0x880A, 0x0000},	/* AfeIfCreg3B10Lsbs */

	{0x300D, 1},		/* DisableNorm */

	{0x280C, 0x0180},	/* LinkHoldEnd */

	{0x1C21, 0x0002},	/* AlphaM */

	{0x3821, 6},		/* FfeLkgTx0 */
	{0x381D, 1},		/* FfeLkg1g4 */
	{0x381E, 1},		/* FfeLkg1g5 */
	{0x381F, 1},		/* FfeLkg1g6 */
	{0x3820, 1},		/* FfeLkg1g7 */

	{0x8402, 0x01F0},	/* Btinact */
	{0x800E, 20},		/* LftrainTime */
	{0x800F, 24},		/* DvguardTime */
	{0x8010, 46},		/* IdlguardTime */

	{0, 0}
};

/* condensed version of the phy initialization routine */
void et1310_phy_init(struct et131x_adapter *adapter)
{
	u16 data, index;

	/* get the identity (again ?) */
	et131x_mii_read(adapter, PHY_ID_1, &data);
	et131x_mii_read(adapter, PHY_ID_2, &data);

	/* what does this do/achieve ? */
	/* should read 0002 */
	et131x_mii_read(adapter, PHY_MPHY_CONTROL_REG, &data);
	et131x_mii_write(adapter, PHY_MPHY_CONTROL_REG,	0x0006);

	/* read modem register 0402, should I do something with the return
	   data ? */
	et131x_mii_write(adapter, PHY_INDEX_REG, 0x0402);
	et131x_mii_read(adapter, PHY_DATA_REG, &data);

	/* what does this do/achieve ? */
	et131x_mii_write(adapter, PHY_MPHY_CONTROL_REG, 0x0002);

	/* get the identity (again ?) */
	et131x_mii_read(adapter, PHY_ID_1, &data);
	et131x_mii_read(adapter, PHY_ID_2, &data);

	/* what does this achieve ? */
	/* should read 0002 */
	et131x_mii_read(adapter, PHY_MPHY_CONTROL_REG, &data);
	et131x_mii_write(adapter, PHY_MPHY_CONTROL_REG, 0x0006);

	/* read modem register 0402, should I do something with
	   the return data? */
	et131x_mii_write(adapter, PHY_INDEX_REG, 0x0402);
	et131x_mii_read(adapter, PHY_DATA_REG, &data);

	et131x_mii_write(adapter, PHY_MPHY_CONTROL_REG, 0x0002);

	/* what does this achieve (should return 0x1040) */
	et131x_mii_read(adapter, PHY_CONTROL, &data);
	/* should read 0002 */
	et131x_mii_read(adapter, PHY_MPHY_CONTROL_REG, &data);
	et131x_mii_write(adapter, PHY_CONTROL, 0x1840);

	et131x_mii_write(adapter, PHY_MPHY_CONTROL_REG, 0x0007);

	/* here the writing of the array starts.... */
	index = 0;
	while (config_phy[index][0] != 0x0000) {
		/* write value */
		et131x_mii_write(adapter, PHY_INDEX_REG, config_phy[index][0]);
		et131x_mii_write(adapter, PHY_DATA_REG, config_phy[index][1]);

		/* read it back */
		et131x_mii_write(adapter, PHY_INDEX_REG, config_phy[index][0]);
		et131x_mii_read(adapter, PHY_DATA_REG, &data);

		/* do a check on the value read back ? */
		index++;
	}
	/* here the writing of the array ends... */

	et131x_mii_read(adapter, PHY_CONTROL, &data);		/* 0x1840 */
	/* should read 0007 */
	et131x_mii_read(adapter, PHY_MPHY_CONTROL_REG, &data);
	et131x_mii_write(adapter, PHY_CONTROL, 0x1040);
	et131x_mii_write(adapter, PHY_MPHY_CONTROL_REG, 0x0002);
}

