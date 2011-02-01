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

/* Prototypes for functions with local scope */
static void et131x_xcvr_init(struct et131x_adapter *etdev);

/**
 * PhyMiRead - Read from the PHY through the MII Interface on the MAC
 * @etdev: pointer to our private adapter structure
 * @xcvrAddr: the address of the transciever
 * @xcvrReg: the register to read
 * @value: pointer to a 16-bit value in which the value will be stored
 *
 * Returns 0 on success, errno on failure (as defined in errno.h)
 */
int PhyMiRead(struct et131x_adapter *etdev, u8 xcvrAddr,
	      u8 xcvrReg, u16 *value)
{
	struct _MAC_t __iomem *mac = &etdev->regs->mac;
	int status = 0;
	u32 delay;
	u32 miiAddr;
	u32 miiCmd;
	u32 miiIndicator;

	/* Save a local copy of the registers we are dealing with so we can
	 * set them back
	 */
	miiAddr = readl(&mac->mii_mgmt_addr);
	miiCmd = readl(&mac->mii_mgmt_cmd);

	/* Stop the current operation */
	writel(0, &mac->mii_mgmt_cmd);

	/* Set up the register we need to read from on the correct PHY */
	writel(MII_ADDR(xcvrAddr, xcvrReg), &mac->mii_mgmt_addr);

	/* Kick the read cycle off */
	delay = 0;

	writel(0x1, &mac->mii_mgmt_cmd);

	do {
		udelay(50);
		delay++;
		miiIndicator = readl(&mac->mii_mgmt_indicator);
	} while ((miiIndicator & MGMT_WAIT) && delay < 50);

	/* If we hit the max delay, we could not read the register */
	if (delay == 50) {
		dev_warn(&etdev->pdev->dev,
			    "xcvrReg 0x%08x could not be read\n", xcvrReg);
		dev_warn(&etdev->pdev->dev, "status is  0x%08x\n",
			    miiIndicator);

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
	writel(miiAddr, &mac->mii_mgmt_addr);
	writel(miiCmd, &mac->mii_mgmt_cmd);

	return status;
}

/**
 * MiWrite - Write to a PHY register through the MII interface of the MAC
 * @etdev: pointer to our private adapter structure
 * @xcvrReg: the register to read
 * @value: 16-bit value to write
 *
 * FIXME: one caller in netdev still
 *
 * Return 0 on success, errno on failure (as defined in errno.h)
 */
int MiWrite(struct et131x_adapter *etdev, u8 xcvrReg, u16 value)
{
	struct _MAC_t __iomem *mac = &etdev->regs->mac;
	int status = 0;
	u8 xcvrAddr = etdev->Stats.xcvr_addr;
	u32 delay;
	u32 miiAddr;
	u32 miiCmd;
	u32 miiIndicator;

	/* Save a local copy of the registers we are dealing with so we can
	 * set them back
	 */
	miiAddr = readl(&mac->mii_mgmt_addr);
	miiCmd = readl(&mac->mii_mgmt_cmd);

	/* Stop the current operation */
	writel(0, &mac->mii_mgmt_cmd);

	/* Set up the register we need to write to on the correct PHY */
	writel(MII_ADDR(xcvrAddr, xcvrReg), &mac->mii_mgmt_addr);

	/* Add the value to write to the registers to the mac */
	writel(value, &mac->mii_mgmt_ctrl);
	delay = 0;

	do {
		udelay(50);
		delay++;
		miiIndicator = readl(&mac->mii_mgmt_indicator);
	} while ((miiIndicator & MGMT_BUSY) && delay < 100);

	/* If we hit the max delay, we could not write the register */
	if (delay == 100) {
		u16 TempValue;

		dev_warn(&etdev->pdev->dev,
		    "xcvrReg 0x%08x could not be written", xcvrReg);
		dev_warn(&etdev->pdev->dev, "status is  0x%08x\n",
			    miiIndicator);
		dev_warn(&etdev->pdev->dev, "command is  0x%08x\n",
			    readl(&mac->mii_mgmt_cmd));

		MiRead(etdev, xcvrReg, &TempValue);

		status = -EIO;
	}
	/* Stop the write operation */
	writel(0, &mac->mii_mgmt_cmd);

	/* set the registers we touched back to the state at which we entered
	 * this function
	 */
	writel(miiAddr, &mac->mii_mgmt_addr);
	writel(miiCmd, &mac->mii_mgmt_cmd);

	return status;
}

/**
 * et131x_xcvr_find - Find the PHY ID
 * @etdev: pointer to our private adapter structure
 *
 * Returns 0 on success, errno on failure (as defined in errno.h)
 */
int et131x_xcvr_find(struct et131x_adapter *etdev)
{
	u8 xcvr_addr;
	u16 idr1;
	u16 idr2;
	u32 xcvr_id;

	/* We need to get xcvr id and address we just get the first one */
	for (xcvr_addr = 0; xcvr_addr < 32; xcvr_addr++) {
		/* Read the ID from the PHY */
		PhyMiRead(etdev, xcvr_addr,
			  (u8) offsetof(struct mi_regs, idr1),
			  &idr1);
		PhyMiRead(etdev, xcvr_addr,
			  (u8) offsetof(struct mi_regs, idr2),
			  &idr2);

		xcvr_id = (u32) ((idr1 << 16) | idr2);

		if (idr1 != 0 && idr1 != 0xffff) {
			etdev->Stats.xcvr_id = xcvr_id;
			etdev->Stats.xcvr_addr = xcvr_addr;
			return 0;
		}
	}
	return -ENODEV;
}

void ET1310_PhyReset(struct et131x_adapter *etdev)
{
	MiWrite(etdev, PHY_CONTROL, 0x8000);
}

/**
 *	ET1310_PhyPowerDown	-	PHY power control
 *	@etdev: device to control
 *	@down: true for off/false for back on
 *
 *	one hundred, ten, one thousand megs
 *	How would you like to have your LAN accessed
 *	Can't you see that this code processed
 *	Phy power, phy power..
 */

void ET1310_PhyPowerDown(struct et131x_adapter *etdev, bool down)
{
	u16 data;

	MiRead(etdev, PHY_CONTROL, &data);
	data &= ~0x0800;	/* Power UP */
	if (down) /* Power DOWN */
		data |= 0x0800;
	MiWrite(etdev, PHY_CONTROL, data);
}

/**
 *	ET130_PhyAutoNEg	-	autonegotiate control
 *	@etdev: device to control
 *	@enabe: autoneg on/off
 *
 *	Set up the autonegotiation state according to whether we will be
 *	negotiating the state or forcing a speed.
 */

static void ET1310_PhyAutoNeg(struct et131x_adapter *etdev, bool enable)
{
	u16 data;

	MiRead(etdev, PHY_CONTROL, &data);
	data &= ~0x1000;	/* Autonegotiation OFF */
	if (enable)
		data |= 0x1000;		/* Autonegotiation ON */
	MiWrite(etdev, PHY_CONTROL, data);
}

/**
 *	ET130_PhyDuplexMode	-	duplex control
 *	@etdev: device to control
 *	@duplex: duplex on/off
 *
 *	Set up the duplex state on the PHY
 */

static void ET1310_PhyDuplexMode(struct et131x_adapter *etdev, u16 duplex)
{
	u16 data;

	MiRead(etdev, PHY_CONTROL, &data);
	data &= ~0x100;		/* Set Half Duplex */
	if (duplex == TRUEPHY_DUPLEX_FULL)
		data |= 0x100;	/* Set Full Duplex */
	MiWrite(etdev, PHY_CONTROL, data);
}

/**
 *	ET130_PhySpeedSelect	-	speed control
 *	@etdev: device to control
 *	@duplex: duplex on/off
 *
 *	Set the speed of our PHY.
 */

static void ET1310_PhySpeedSelect(struct et131x_adapter *etdev, u16 speed)
{
	u16 data;
	static const u16 bits[3] = {0x0000, 0x2000, 0x0040};

	/* Read the PHY control register */
	MiRead(etdev, PHY_CONTROL, &data);
	/* Clear all Speed settings (Bits 6, 13) */
	data &= ~0x2040;
	/* Write back the new speed */
	MiWrite(etdev, PHY_CONTROL, data | bits[speed]);
}

/**
 *	ET1310_PhyLinkStatus	-	read link state
 *	@etdev: device to read
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

static void ET1310_PhyLinkStatus(struct et131x_adapter *etdev,
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

	MiRead(etdev, PHY_STATUS, &mistatus);
	MiRead(etdev, PHY_1000_STATUS, &is1000BaseT);
	MiRead(etdev, PHY_PHY_STATUS, &vmi_phystatus);
	MiRead(etdev, PHY_CONTROL, &control);

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

static void ET1310_PhyAndOrReg(struct et131x_adapter *etdev,
			u16 regnum, u16 andMask, u16 orMask)
{
	u16 reg;

	MiRead(etdev, regnum, &reg);
	reg &= andMask;
	reg |= orMask;
	MiWrite(etdev, regnum, reg);
}

/* Still used from _mac  for BIT_READ */
void ET1310_PhyAccessMiBit(struct et131x_adapter *etdev, u16 action,
			   u16 regnum, u16 bitnum, u8 *value)
{
	u16 reg;
	u16 mask = 0x0001 << bitnum;

	/* Read the requested register */
	MiRead(etdev, regnum, &reg);

	switch (action) {
	case TRUEPHY_BIT_READ:
		*value = (reg & mask) >> bitnum;
		break;

	case TRUEPHY_BIT_SET:
		MiWrite(etdev, regnum, reg | mask);
		break;

	case TRUEPHY_BIT_CLEAR:
		MiWrite(etdev, regnum, reg & ~mask);
		break;

	default:
		break;
	}
}

void ET1310_PhyAdvertise1000BaseT(struct et131x_adapter *etdev,
				  u16 duplex)
{
	u16 data;

	/* Read the PHY 1000 Base-T Control Register */
	MiRead(etdev, PHY_1000_CONTROL, &data);

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
	MiWrite(etdev, PHY_1000_CONTROL, data);
}

static void ET1310_PhyAdvertise100BaseT(struct et131x_adapter *etdev,
				 u16 duplex)
{
	u16 data;

	/* Read the Autonegotiation Register (10/100) */
	MiRead(etdev, PHY_AUTO_ADVERTISEMENT, &data);

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
	MiWrite(etdev, PHY_AUTO_ADVERTISEMENT, data);
}

static void ET1310_PhyAdvertise10BaseT(struct et131x_adapter *etdev,
				u16 duplex)
{
	u16 data;

	/* Read the Autonegotiation Register (10/100) */
	MiRead(etdev, PHY_AUTO_ADVERTISEMENT, &data);

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
	MiWrite(etdev, PHY_AUTO_ADVERTISEMENT, data);
}

/**
 * et131x_setphy_normal - Set PHY for normal operation.
 * @etdev: pointer to our private adapter structure
 *
 * Used by Power Management to force the PHY into 10 Base T half-duplex mode,
 * when going to D3 in WOL mode. Also used during initialization to set the
 * PHY for normal operation.
 */
void et131x_setphy_normal(struct et131x_adapter *etdev)
{
	/* Make sure the PHY is powered up */
	ET1310_PhyPowerDown(etdev, 0);
	et131x_xcvr_init(etdev);
}


/**
 * et131x_xcvr_init - Init the phy if we are setting it into force mode
 * @etdev: pointer to our private adapter structure
 *
 */
static void et131x_xcvr_init(struct et131x_adapter *etdev)
{
	u16 imr;
	u16 isr;
	u16 lcr2;

	/* Zero out the adapter structure variable representing BMSR */
	etdev->Bmsr.value = 0;

	MiRead(etdev, (u8) offsetof(struct mi_regs, isr), &isr);
	MiRead(etdev, (u8) offsetof(struct mi_regs, imr), &imr);

	/* Set the link status interrupt only.  Bad behavior when link status
	 * and auto neg are set, we run into a nested interrupt problem
	 */
        imr |= 0x0105;

	MiWrite(etdev, (u8) offsetof(struct mi_regs, imr), imr);

	/* Set the LED behavior such that LED 1 indicates speed (off =
	 * 10Mbits, blink = 100Mbits, on = 1000Mbits) and LED 2 indicates
	 * link and activity (on for link, blink off for activity).
	 *
	 * NOTE: Some customizations have been added here for specific
	 * vendors; The LED behavior is now determined by vendor data in the
	 * EEPROM. However, the above description is the default.
	 */
	if ((etdev->eeprom_data[1] & 0x4) == 0) {
		MiRead(etdev, (u8) offsetof(struct mi_regs, lcr2),
		       &lcr2);

		lcr2 &= 0x00FF;
		lcr2 |= 0xA000;	/* led link */

		if ((etdev->eeprom_data[1] & 0x8) == 0)
			lcr2 |= 0x0300;
		else
			lcr2 |= 0x0400;

		MiWrite(etdev, (u8) offsetof(struct mi_regs, lcr2),
			lcr2);
	}

	/* Determine if we need to go into a force mode and set it */
	if (etdev->AiForceSpeed == 0 && etdev->AiForceDpx == 0) {
		if (etdev->wanted_flow == FLOW_TXONLY ||
		    etdev->wanted_flow == FLOW_BOTH)
			ET1310_PhyAccessMiBit(etdev,
					      TRUEPHY_BIT_SET, 4, 11, NULL);
		else
			ET1310_PhyAccessMiBit(etdev,
					      TRUEPHY_BIT_CLEAR, 4, 11, NULL);

		if (etdev->wanted_flow == FLOW_BOTH)
			ET1310_PhyAccessMiBit(etdev,
					      TRUEPHY_BIT_SET, 4, 10, NULL);
		else
			ET1310_PhyAccessMiBit(etdev,
					      TRUEPHY_BIT_CLEAR, 4, 10, NULL);

		/* Set the phy to autonegotiation */
		ET1310_PhyAutoNeg(etdev, true);

		/* NOTE - Do we need this? */
		ET1310_PhyAccessMiBit(etdev, TRUEPHY_BIT_SET, 0, 9, NULL);
		return;
	}

	ET1310_PhyAutoNeg(etdev, false);

	/* Set to the correct force mode. */
	if (etdev->AiForceDpx != 1) {
		if (etdev->wanted_flow == FLOW_TXONLY ||
		    etdev->wanted_flow == FLOW_BOTH)
			ET1310_PhyAccessMiBit(etdev,
				      TRUEPHY_BIT_SET, 4, 11, NULL);
		else
			ET1310_PhyAccessMiBit(etdev,
					      TRUEPHY_BIT_CLEAR, 4, 11, NULL);

		if (etdev->wanted_flow == FLOW_BOTH)
			ET1310_PhyAccessMiBit(etdev,
					      TRUEPHY_BIT_SET, 4, 10, NULL);
		else
			ET1310_PhyAccessMiBit(etdev,
					      TRUEPHY_BIT_CLEAR, 4, 10, NULL);
	} else {
		ET1310_PhyAccessMiBit(etdev, TRUEPHY_BIT_CLEAR, 4, 10, NULL);
		ET1310_PhyAccessMiBit(etdev, TRUEPHY_BIT_CLEAR, 4, 11, NULL);
	}
	ET1310_PhyPowerDown(etdev, 1);
	switch (etdev->AiForceSpeed) {
	case 10:
		/* First we need to turn off all other advertisement */
		ET1310_PhyAdvertise1000BaseT(etdev, TRUEPHY_ADV_DUPLEX_NONE);
		ET1310_PhyAdvertise100BaseT(etdev, TRUEPHY_ADV_DUPLEX_NONE);
		if (etdev->AiForceDpx == 1) {
			/* Set our advertise values accordingly */
			ET1310_PhyAdvertise10BaseT(etdev,
						TRUEPHY_ADV_DUPLEX_HALF);
		} else if (etdev->AiForceDpx == 2) {
			/* Set our advertise values accordingly */
			ET1310_PhyAdvertise10BaseT(etdev,
						TRUEPHY_ADV_DUPLEX_FULL);
		} else {
			/* Disable autoneg */
			ET1310_PhyAutoNeg(etdev, false);
			/* Disable rest of the advertisements */
			ET1310_PhyAdvertise10BaseT(etdev,
					TRUEPHY_ADV_DUPLEX_NONE);
			/* Force 10 Mbps */
			ET1310_PhySpeedSelect(etdev, TRUEPHY_SPEED_10MBPS);
			/* Force Full duplex */
			ET1310_PhyDuplexMode(etdev, TRUEPHY_DUPLEX_FULL);
		}
		break;
	case 100:
		/* first we need to turn off all other advertisement */
		ET1310_PhyAdvertise1000BaseT(etdev, TRUEPHY_ADV_DUPLEX_NONE);
		ET1310_PhyAdvertise10BaseT(etdev, TRUEPHY_ADV_DUPLEX_NONE);
		if (etdev->AiForceDpx == 1) {
			/* Set our advertise values accordingly */
			ET1310_PhyAdvertise100BaseT(etdev,
						TRUEPHY_ADV_DUPLEX_HALF);
			/* Set speed */
			ET1310_PhySpeedSelect(etdev, TRUEPHY_SPEED_100MBPS);
		} else if (etdev->AiForceDpx == 2) {
			/* Set our advertise values accordingly */
			ET1310_PhyAdvertise100BaseT(etdev,
						TRUEPHY_ADV_DUPLEX_FULL);
		} else {
			/* Disable autoneg */
			ET1310_PhyAutoNeg(etdev, false);
			/* Disable other advertisement */
			ET1310_PhyAdvertise100BaseT(etdev,
						TRUEPHY_ADV_DUPLEX_NONE);
			/* Force 100 Mbps */
			ET1310_PhySpeedSelect(etdev, TRUEPHY_SPEED_100MBPS);
			/* Force Full duplex */
			ET1310_PhyDuplexMode(etdev, TRUEPHY_DUPLEX_FULL);
		}
		break;
	case 1000:
		/* first we need to turn off all other advertisement */
		ET1310_PhyAdvertise100BaseT(etdev, TRUEPHY_ADV_DUPLEX_NONE);
		ET1310_PhyAdvertise10BaseT(etdev, TRUEPHY_ADV_DUPLEX_NONE);
		/* set our advertise values accordingly */
		ET1310_PhyAdvertise1000BaseT(etdev, TRUEPHY_ADV_DUPLEX_FULL);
		break;
	}
	ET1310_PhyPowerDown(etdev, 0);
}

void et131x_Mii_check(struct et131x_adapter *etdev,
		      MI_BMSR_t bmsr, MI_BMSR_t bmsr_ints)
{
	u8 link_status;
	u32 autoneg_status;
	u32 speed;
	u32 duplex;
	u32 mdi_mdix;
	u32 masterslave;
	u32 polarity;
	unsigned long flags;

	if (bmsr_ints.bits.link_status) {
		if (bmsr.bits.link_status) {
			etdev->boot_coma = 20;

			/* Update our state variables and indicate the
			 * connected state
			 */
			spin_lock_irqsave(&etdev->Lock, flags);

			etdev->MediaState = NETIF_STATUS_MEDIA_CONNECT;
			etdev->Flags &= ~fMP_ADAPTER_LINK_DETECTION;

			spin_unlock_irqrestore(&etdev->Lock, flags);

			netif_carrier_on(etdev->netdev);
		} else {
			dev_warn(&etdev->pdev->dev,
			    "Link down - cable problem ?\n");

			if (etdev->linkspeed == TRUEPHY_SPEED_10MBPS) {
				/* NOTE - Is there a way to query this without
				 * TruePHY?
				 * && TRU_QueryCoreType(etdev->hTruePhy, 0) ==
				 * EMI_TRUEPHY_A13O) {
				 */
				u16 Register18;

				MiRead(etdev, 0x12, &Register18);
				MiWrite(etdev, 0x12, Register18 | 0x4);
				MiWrite(etdev, 0x10, Register18 | 0x8402);
				MiWrite(etdev, 0x11, Register18 | 511);
				MiWrite(etdev, 0x12, Register18);
			}

			/* For the first N seconds of life, we are in "link
			 * detection" When we are in this state, we should
			 * only report "connected". When the LinkDetection
			 * Timer expires, we can report disconnected (handled
			 * in the LinkDetectionDPC).
			 */
			if (!(etdev->Flags & fMP_ADAPTER_LINK_DETECTION) ||
			 (etdev->MediaState == NETIF_STATUS_MEDIA_DISCONNECT)) {
				spin_lock_irqsave(&etdev->Lock, flags);
				etdev->MediaState =
				    NETIF_STATUS_MEDIA_DISCONNECT;
				spin_unlock_irqrestore(&etdev->Lock,
						       flags);

				netif_carrier_off(etdev->netdev);
			}

			etdev->linkspeed = 0;
			etdev->duplex_mode = 0;

			/* Free the packets being actively sent & stopped */
			et131x_free_busy_send_packets(etdev);

			/* Re-initialize the send structures */
			et131x_init_send(etdev);

			/* Reset the RFD list and re-start RU */
			et131x_reset_recv(etdev);

			/*
			 * Bring the device back to the state it was during
			 * init prior to autonegotiation being complete. This
			 * way, when we get the auto-neg complete interrupt,
			 * we can complete init by calling ConfigMacREGS2.
			 */
			et131x_soft_reset(etdev);

			/* Setup ET1310 as per the documentation */
			et131x_adapter_setup(etdev);

			/* Setup the PHY into coma mode until the cable is
			 * plugged back in
			 */
			if (etdev->RegistryPhyComa == 1)
				EnablePhyComa(etdev);
		}
	}

	if (bmsr_ints.bits.auto_neg_complete ||
	    (etdev->AiForceDpx == 3 && bmsr_ints.bits.link_status)) {
		if (bmsr.bits.auto_neg_complete || etdev->AiForceDpx == 3) {
			ET1310_PhyLinkStatus(etdev,
					     &link_status, &autoneg_status,
					     &speed, &duplex, &mdi_mdix,
					     &masterslave, &polarity);

			etdev->linkspeed = speed;
			etdev->duplex_mode = duplex;

			etdev->boot_coma = 20;

			if (etdev->linkspeed == TRUEPHY_SPEED_10MBPS) {
				/*
				 * NOTE - Is there a way to query this without
				 * TruePHY?
				 * && TRU_QueryCoreType(etdev->hTruePhy, 0)==
				 * EMI_TRUEPHY_A13O) {
				 */
				u16 Register18;

				MiRead(etdev, 0x12, &Register18);
				MiWrite(etdev, 0x12, Register18 | 0x4);
				MiWrite(etdev, 0x10, Register18 | 0x8402);
				MiWrite(etdev, 0x11, Register18 | 511);
				MiWrite(etdev, 0x12, Register18);
			}

			ConfigFlowControl(etdev);

			if (etdev->linkspeed == TRUEPHY_SPEED_1000MBPS &&
					etdev->RegistryJumboPacket > 2048)
				ET1310_PhyAndOrReg(etdev, 0x16, 0xcfff,
								   0x2000);

			SetRxDmaTimer(etdev);
			ConfigMACRegs2(etdev);
		}
	}
}

/*
 * The routines which follow provide low-level access to the PHY, and are used
 * primarily by the routines above (although there are a few places elsewhere
 * in the driver where this level of access is required).
 */

static const u16 ConfigPhy[25][2] = {
	/* Reg      Value      Register */
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
void ET1310_PhyInit(struct et131x_adapter *etdev)
{
	u16 data, index;

	if (etdev == NULL)
		return;

	/* get the identity (again ?) */
	MiRead(etdev, PHY_ID_1, &data);
	MiRead(etdev, PHY_ID_2, &data);

	/* what does this do/achieve ? */
	MiRead(etdev, PHY_MPHY_CONTROL_REG, &data); /* should read 0002 */
	MiWrite(etdev, PHY_MPHY_CONTROL_REG,	0x0006);

	/* read modem register 0402, should I do something with the return
	   data ? */
	MiWrite(etdev, PHY_INDEX_REG, 0x0402);
	MiRead(etdev, PHY_DATA_REG, &data);

	/* what does this do/achieve ? */
	MiWrite(etdev, PHY_MPHY_CONTROL_REG, 0x0002);

	/* get the identity (again ?) */
	MiRead(etdev, PHY_ID_1, &data);
	MiRead(etdev, PHY_ID_2, &data);

	/* what does this achieve ? */
	MiRead(etdev, PHY_MPHY_CONTROL_REG, &data); /* should read 0002 */
	MiWrite(etdev, PHY_MPHY_CONTROL_REG, 0x0006);

	/* read modem register 0402, should I do something with
	   the return data? */
	MiWrite(etdev, PHY_INDEX_REG, 0x0402);
	MiRead(etdev, PHY_DATA_REG, &data);

	MiWrite(etdev, PHY_MPHY_CONTROL_REG, 0x0002);

	/* what does this achieve (should return 0x1040) */
	MiRead(etdev, PHY_CONTROL, &data);
	MiRead(etdev, PHY_MPHY_CONTROL_REG, &data); /* should read 0002 */
	MiWrite(etdev, PHY_CONTROL, 0x1840);

	MiWrite(etdev, PHY_MPHY_CONTROL_REG, 0x0007);

	/* here the writing of the array starts.... */
	index = 0;
	while (ConfigPhy[index][0] != 0x0000) {
		/* write value */
		MiWrite(etdev, PHY_INDEX_REG, ConfigPhy[index][0]);
		MiWrite(etdev, PHY_DATA_REG, ConfigPhy[index][1]);

		/* read it back */
		MiWrite(etdev, PHY_INDEX_REG, ConfigPhy[index][0]);
		MiRead(etdev, PHY_DATA_REG, &data);

		/* do a check on the value read back ? */
		index++;
	}
	/* here the writing of the array ends... */

	MiRead(etdev, PHY_CONTROL, &data);		/* 0x1840 */
	MiRead(etdev, PHY_MPHY_CONTROL_REG, &data);/* should read 0007 */
	MiWrite(etdev, PHY_CONTROL, 0x1040);
	MiWrite(etdev, PHY_MPHY_CONTROL_REG, 0x0002);
}

