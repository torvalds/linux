/*
 * Agere Systems Inc.
 * 10/100/1000 Base-T Ethernet Driver for the ET1310 and ET131x series MACs
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
#include <linux/phy.h>

#include "et1310_phy.h"

#include "et131x_adapter.h"

#include "et1310_address_map.h"
#include "et1310_tx.h"
#include "et1310_rx.h"

#include "et131x.h"

int et131x_mdio_read(struct mii_bus *bus, int phy_addr, int reg)
{
	struct net_device *netdev = bus->priv;
	struct et131x_adapter *adapter = netdev_priv(netdev);
	u16 value;
	int ret;

	ret = et131x_phy_mii_read(adapter, phy_addr, reg, &value);

	if (ret < 0)
		return ret;
	else
		return value;
}

int et131x_mdio_write(struct mii_bus *bus, int phy_addr, int reg, u16 value)
{
	struct net_device *netdev = bus->priv;
	struct et131x_adapter *adapter = netdev_priv(netdev);

	return et131x_mii_write(adapter, reg, value);
}

int et131x_mdio_reset(struct mii_bus *bus)
{
	struct net_device *netdev = bus->priv;
	struct et131x_adapter *adapter = netdev_priv(netdev);

	et131x_mii_write(adapter, MII_BMCR, 0x8000);

	return 0;
}


int et131x_mii_read(struct et131x_adapter *adapter, u8 reg, u16 *value)
{
	struct phy_device *phydev = adapter->phydev;

	if (!phydev)
		return -EIO;

	return et131x_phy_mii_read(adapter, phydev->addr, reg, value);
}

/**
 * et131x_phy_mii_read - Read from the PHY through the MII Interface on the MAC
 * @adapter: pointer to our private adapter structure
 * @addr: the address of the transceiver
 * @reg: the register to read
 * @value: pointer to a 16-bit value in which the value will be stored
 *
 * Returns 0 on success, errno on failure (as defined in errno.h)
 */
int et131x_phy_mii_read(struct et131x_adapter *adapter, u8 addr,
	      u8 reg, u16 *value)
{
	struct mac_regs __iomem *mac = &adapter->regs->mac;
	int status = 0;
	u32 delay = 0;
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
	writel(MII_ADDR(addr, reg), &mac->mii_mgmt_addr);

	writel(0x1, &mac->mii_mgmt_cmd);

	do {
		udelay(50);
		delay++;
		mii_indicator = readl(&mac->mii_mgmt_indicator);
	} while ((mii_indicator & MGMT_WAIT) && delay < 50);

	/* If we hit the max delay, we could not read the register */
	if (delay == 50) {
		dev_warn(&adapter->pdev->dev,
			    "reg 0x%08x could not be read\n", reg);
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
 * @reg: the register to read
 * @value: 16-bit value to write
 *
 * FIXME: one caller in netdev still
 *
 * Return 0 on success, errno on failure (as defined in errno.h)
 */
int et131x_mii_write(struct et131x_adapter *adapter, u8 reg, u16 value)
{
	struct mac_regs __iomem *mac = &adapter->regs->mac;
	struct phy_device *phydev = adapter->phydev;
	int status = 0;
	u8 addr;
	u32 delay = 0;
	u32 mii_addr;
	u32 mii_cmd;
	u32 mii_indicator;

	if (!phydev)
		return -EIO;

	addr = phydev->addr;

	/* Save a local copy of the registers we are dealing with so we can
	 * set them back
	 */
	mii_addr = readl(&mac->mii_mgmt_addr);
	mii_cmd = readl(&mac->mii_mgmt_cmd);

	/* Stop the current operation */
	writel(0, &mac->mii_mgmt_cmd);

	/* Set up the register we need to write to on the correct PHY */
	writel(MII_ADDR(addr, reg), &mac->mii_mgmt_addr);

	/* Add the value to write to the registers to the mac */
	writel(value, &mac->mii_mgmt_ctrl);

	do {
		udelay(50);
		delay++;
		mii_indicator = readl(&mac->mii_mgmt_indicator);
	} while ((mii_indicator & MGMT_BUSY) && delay < 100);

	/* If we hit the max delay, we could not write the register */
	if (delay == 100) {
		u16 tmp;

		dev_warn(&adapter->pdev->dev,
		    "reg 0x%08x could not be written", reg);
		dev_warn(&adapter->pdev->dev, "status is  0x%08x\n",
			    mii_indicator);
		dev_warn(&adapter->pdev->dev, "command is  0x%08x\n",
			    readl(&mac->mii_mgmt_cmd));

		et131x_mii_read(adapter, reg, &tmp);

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

	et131x_mii_read(adapter, MII_BMCR, &data);
	data &= ~0x0800;	/* Power UP */
	if (down) /* Power DOWN */
		data |= 0x0800;
	et131x_mii_write(adapter, MII_BMCR, data);
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

	et131x_mii_read(adapter, MII_BMSR, &mistatus);
	et131x_mii_read(adapter, MII_STAT1000, &is1000BaseT);
	et131x_mii_read(adapter, PHY_PHY_STATUS, &vmi_phystatus);
	et131x_mii_read(adapter, MII_BMCR, &control);

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

/**
 * et131x_xcvr_init - Init the phy if we are setting it into force mode
 * @adapter: pointer to our private adapter structure
 *
 */
void et131x_xcvr_init(struct et131x_adapter *adapter)
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

	if (bmsr_ints & BMSR_LSTATUS) {
		if (bmsr & BMSR_LSTATUS) {
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
				et131x_mii_write(adapter, 0x12,
						 register18 | 0x4);
				et131x_mii_write(adapter, 0x10,
						 register18 | 0x8402);
				et131x_mii_write(adapter, 0x11,
						 register18 | 511);
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

	if ((bmsr_ints & BMSR_ANEGCOMPLETE) ||
	   (adapter->ai_force_duplex == 3 && (bmsr_ints & BMSR_LSTATUS))) {
		if ((bmsr & BMSR_ANEGCOMPLETE) ||
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
				et131x_mii_write(adapter, 0x12,
						 register18 | 0x4);
				et131x_mii_write(adapter, 0x10,
						 register18 | 0x8402);
				et131x_mii_write(adapter, 0x11,
						 register18 | 511);
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

