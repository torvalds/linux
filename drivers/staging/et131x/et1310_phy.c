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
#include <linux/slab.h>
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
#include "et1310_pm.h"
#include "et1310_jagcore.h"

#include "et131x_adapter.h"
#include "et131x_netdev.h"
#include "et131x_initpci.h"

#include "et1310_address_map.h"
#include "et1310_tx.h"
#include "et1310_rx.h"
#include "et1310_mac.h"

/* Prototypes for functions with local scope */
static int et131x_xcvr_init(struct et131x_adapter *adapter);

/**
 * PhyMiRead - Read from the PHY through the MII Interface on the MAC
 * @adapter: pointer to our private adapter structure
 * @xcvrAddr: the address of the transciever
 * @xcvrReg: the register to read
 * @value: pointer to a 16-bit value in which the value will be stored
 *
 * Returns 0 on success, errno on failure (as defined in errno.h)
 */
int PhyMiRead(struct et131x_adapter *adapter, uint8_t xcvrAddr,
	      uint8_t xcvrReg, uint16_t *value)
{
	struct _MAC_t __iomem *mac = &adapter->regs->mac;
	int status = 0;
	uint32_t delay;
	MII_MGMT_ADDR_t miiAddr;
	MII_MGMT_CMD_t miiCmd;
	MII_MGMT_INDICATOR_t miiIndicator;

	/* Save a local copy of the registers we are dealing with so we can
	 * set them back
	 */
	miiAddr.value = readl(&mac->mii_mgmt_addr.value);
	miiCmd.value = readl(&mac->mii_mgmt_cmd.value);

	/* Stop the current operation */
	writel(0, &mac->mii_mgmt_cmd.value);

	/* Set up the register we need to read from on the correct PHY */
	{
		MII_MGMT_ADDR_t mii_mgmt_addr = { 0 };

		mii_mgmt_addr.bits.phy_addr = xcvrAddr;
		mii_mgmt_addr.bits.reg_addr = xcvrReg;
		writel(mii_mgmt_addr.value, &mac->mii_mgmt_addr.value);
	}

	/* Kick the read cycle off */
	delay = 0;

	writel(0x1, &mac->mii_mgmt_cmd.value);

	do {
		udelay(50);
		delay++;
		miiIndicator.value = readl(&mac->mii_mgmt_indicator.value);
	} while ((miiIndicator.bits.not_valid || miiIndicator.bits.busy) &&
		 delay < 50);

	/* If we hit the max delay, we could not read the register */
	if (delay >= 50) {
		dev_warn(&adapter->pdev->dev,
			    "xcvrReg 0x%08x could not be read\n", xcvrReg);
		dev_warn(&adapter->pdev->dev, "status is  0x%08x\n",
			    miiIndicator.value);

		status = -EIO;
	}

	/* If we hit here we were able to read the register and we need to
	 * return the value to the caller
	 */
	/* TODO: make this stuff a simple readw()?! */
	{
		MII_MGMT_STAT_t mii_mgmt_stat;

		mii_mgmt_stat.value = readl(&mac->mii_mgmt_stat.value);
		*value = (uint16_t) mii_mgmt_stat.bits.phy_stat;
	}

	/* Stop the read operation */
	writel(0, &mac->mii_mgmt_cmd.value);

	/* set the registers we touched back to the state at which we entered
	 * this function
	 */
	writel(miiAddr.value, &mac->mii_mgmt_addr.value);
	writel(miiCmd.value, &mac->mii_mgmt_cmd.value);

	return status;
}

/**
 * MiWrite - Write to a PHY register through the MII interface of the MAC
 * @adapter: pointer to our private adapter structure
 * @xcvrReg: the register to read
 * @value: 16-bit value to write
 *
 * Return 0 on success, errno on failure (as defined in errno.h)
 */
int MiWrite(struct et131x_adapter *adapter, uint8_t xcvrReg, uint16_t value)
{
	struct _MAC_t __iomem *mac = &adapter->regs->mac;
	int status = 0;
	uint8_t xcvrAddr = adapter->Stats.xcvr_addr;
	uint32_t delay;
	MII_MGMT_ADDR_t miiAddr;
	MII_MGMT_CMD_t miiCmd;
	MII_MGMT_INDICATOR_t miiIndicator;

	/* Save a local copy of the registers we are dealing with so we can
	 * set them back
	 */
	miiAddr.value = readl(&mac->mii_mgmt_addr.value);
	miiCmd.value = readl(&mac->mii_mgmt_cmd.value);

	/* Stop the current operation */
	writel(0, &mac->mii_mgmt_cmd.value);

	/* Set up the register we need to write to on the correct PHY */
	{
		MII_MGMT_ADDR_t mii_mgmt_addr;

		mii_mgmt_addr.bits.phy_addr = xcvrAddr;
		mii_mgmt_addr.bits.reg_addr = xcvrReg;
		writel(mii_mgmt_addr.value, &mac->mii_mgmt_addr.value);
	}

	/* Add the value to write to the registers to the mac */
	writel(value, &mac->mii_mgmt_ctrl.value);
	delay = 0;

	do {
		udelay(50);
		delay++;
		miiIndicator.value = readl(&mac->mii_mgmt_indicator.value);
	} while (miiIndicator.bits.busy && delay < 100);

	/* If we hit the max delay, we could not write the register */
	if (delay == 100) {
		uint16_t TempValue;

		dev_warn(&adapter->pdev->dev,
		    "xcvrReg 0x%08x could not be written", xcvrReg);
		dev_warn(&adapter->pdev->dev, "status is  0x%08x\n",
			    miiIndicator.value);
		dev_warn(&adapter->pdev->dev, "command is  0x%08x\n",
			    readl(&mac->mii_mgmt_cmd.value));

		MiRead(adapter, xcvrReg, &TempValue);

		status = -EIO;
	}

	/* Stop the write operation */
	writel(0, &mac->mii_mgmt_cmd.value);

	/* set the registers we touched back to the state at which we entered
	 * this function
	 */
	writel(miiAddr.value, &mac->mii_mgmt_addr.value);
	writel(miiCmd.value, &mac->mii_mgmt_cmd.value);

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
	int status = -ENODEV;
	uint8_t xcvr_addr;
	MI_IDR1_t idr1;
	MI_IDR2_t idr2;
	uint32_t xcvr_id;

	/* We need to get xcvr id and address we just get the first one */
	for (xcvr_addr = 0; xcvr_addr < 32; xcvr_addr++) {
		/* Read the ID from the PHY */
		PhyMiRead(adapter, xcvr_addr,
			  (uint8_t) offsetof(MI_REGS_t, idr1),
			  &idr1.value);
		PhyMiRead(adapter, xcvr_addr,
			  (uint8_t) offsetof(MI_REGS_t, idr2),
			  &idr2.value);

		xcvr_id = (uint32_t) ((idr1.value << 16) | idr2.value);

		if ((idr1.value != 0) && (idr1.value != 0xffff)) {
			adapter->Stats.xcvr_id = xcvr_id;
			adapter->Stats.xcvr_addr = xcvr_addr;

			status = 0;
			break;
		}
	}
	return status;
}

/**
 * et131x_setphy_normal - Set PHY for normal operation.
 * @adapter: pointer to our private adapter structure
 *
 * Used by Power Management to force the PHY into 10 Base T half-duplex mode,
 * when going to D3 in WOL mode. Also used during initialization to set the
 * PHY for normal operation.
 */
int et131x_setphy_normal(struct et131x_adapter *adapter)
{
	int status;

	/* Make sure the PHY is powered up */
	ET1310_PhyPowerDown(adapter, 0);
	status = et131x_xcvr_init(adapter);
	return status;
}

/**
 * et131x_xcvr_init - Init the phy if we are setting it into force mode
 * @adapter: pointer to our private adapter structure
 *
 * Returns 0 on success, errno on failure (as defined in errno.h)
 */
static int et131x_xcvr_init(struct et131x_adapter *adapter)
{
	int status = 0;
	MI_IMR_t imr;
	MI_ISR_t isr;
	MI_LCR2_t lcr2;

	/* Zero out the adapter structure variable representing BMSR */
	adapter->Bmsr.value = 0;

	MiRead(adapter, (uint8_t) offsetof(MI_REGS_t, isr), &isr.value);

	MiRead(adapter, (uint8_t) offsetof(MI_REGS_t, imr), &imr.value);

	/* Set the link status interrupt only.  Bad behavior when link status
	 * and auto neg are set, we run into a nested interrupt problem
	 */
	imr.bits.int_en = 0x1;
	imr.bits.link_status = 0x1;
	imr.bits.autoneg_status = 0x1;

	MiWrite(adapter, (uint8_t) offsetof(MI_REGS_t, imr), imr.value);

	/* Set the LED behavior such that LED 1 indicates speed (off =
	 * 10Mbits, blink = 100Mbits, on = 1000Mbits) and LED 2 indicates
	 * link and activity (on for link, blink off for activity).
	 *
	 * NOTE: Some customizations have been added here for specific
	 * vendors; The LED behavior is now determined by vendor data in the
	 * EEPROM. However, the above description is the default.
	 */
	if ((adapter->eepromData[1] & 0x4) == 0) {
		MiRead(adapter, (uint8_t) offsetof(MI_REGS_t, lcr2),
		       &lcr2.value);
		if ((adapter->eepromData[1] & 0x8) == 0)
			lcr2.bits.led_tx_rx = 0x3;
		else
			lcr2.bits.led_tx_rx = 0x4;
		lcr2.bits.led_link = 0xa;
		MiWrite(adapter, (uint8_t) offsetof(MI_REGS_t, lcr2),
			lcr2.value);
	}

	/* Determine if we need to go into a force mode and set it */
	if (adapter->AiForceSpeed == 0 && adapter->AiForceDpx == 0) {
		if ((adapter->RegistryFlowControl == TxOnly) ||
		    (adapter->RegistryFlowControl == Both)) {
			ET1310_PhyAccessMiBit(adapter,
					      TRUEPHY_BIT_SET, 4, 11, NULL);
		} else {
			ET1310_PhyAccessMiBit(adapter,
					      TRUEPHY_BIT_CLEAR, 4, 11, NULL);
		}

		if (adapter->RegistryFlowControl == Both) {
			ET1310_PhyAccessMiBit(adapter,
					      TRUEPHY_BIT_SET, 4, 10, NULL);
		} else {
			ET1310_PhyAccessMiBit(adapter,
					      TRUEPHY_BIT_CLEAR, 4, 10, NULL);
		}

		/* Set the phy to autonegotiation */
		ET1310_PhyAutoNeg(adapter, true);

		/* NOTE - Do we need this? */
		ET1310_PhyAccessMiBit(adapter, TRUEPHY_BIT_SET, 0, 9, NULL);
		return status;
	} else {
		ET1310_PhyAutoNeg(adapter, false);

		/* Set to the correct force mode. */
		if (adapter->AiForceDpx != 1) {
			if ((adapter->RegistryFlowControl == TxOnly) ||
			    (adapter->RegistryFlowControl == Both)) {
				ET1310_PhyAccessMiBit(adapter,
						      TRUEPHY_BIT_SET, 4, 11,
						      NULL);
			} else {
				ET1310_PhyAccessMiBit(adapter,
						      TRUEPHY_BIT_CLEAR, 4, 11,
						      NULL);
			}

			if (adapter->RegistryFlowControl == Both) {
				ET1310_PhyAccessMiBit(adapter,
						      TRUEPHY_BIT_SET, 4, 10,
						      NULL);
			} else {
				ET1310_PhyAccessMiBit(adapter,
						      TRUEPHY_BIT_CLEAR, 4, 10,
						      NULL);
			}
		} else {
			ET1310_PhyAccessMiBit(adapter,
					      TRUEPHY_BIT_CLEAR, 4, 10, NULL);
			ET1310_PhyAccessMiBit(adapter,
					      TRUEPHY_BIT_CLEAR, 4, 11, NULL);
		}

		switch (adapter->AiForceSpeed) {
		case 10:
			if (adapter->AiForceDpx == 1)
				TPAL_SetPhy10HalfDuplex(adapter);
			else if (adapter->AiForceDpx == 2)
				TPAL_SetPhy10FullDuplex(adapter);
			else
				TPAL_SetPhy10Force(adapter);
			break;
		case 100:
			if (adapter->AiForceDpx == 1)
				TPAL_SetPhy100HalfDuplex(adapter);
			else if (adapter->AiForceDpx == 2)
				TPAL_SetPhy100FullDuplex(adapter);
			else
				TPAL_SetPhy100Force(adapter);
			break;
		case 1000:
			TPAL_SetPhy1000FullDuplex(adapter);
			break;
		}

		return status;
	}
}

void et131x_Mii_check(struct et131x_adapter *etdev,
		      MI_BMSR_t bmsr, MI_BMSR_t bmsr_ints)
{
	uint8_t link_status;
	uint32_t autoneg_status;
	uint32_t speed;
	uint32_t duplex;
	uint32_t mdi_mdix;
	uint32_t masterslave;
	uint32_t polarity;
	unsigned long flags;

	if (bmsr_ints.bits.link_status) {
		if (bmsr.bits.link_status) {
			etdev->PoMgmt.TransPhyComaModeOnBoot = 20;

			/* Update our state variables and indicate the
			 * connected state
			 */
			spin_lock_irqsave(&etdev->Lock, flags);

			etdev->MediaState = NETIF_STATUS_MEDIA_CONNECT;
			etdev->Flags &= ~fMP_ADAPTER_LINK_DETECTION;

			spin_unlock_irqrestore(&etdev->Lock, flags);

			/* Don't indicate state if we're in loopback mode */
			if (etdev->RegistryPhyLoopbk == false)
				netif_carrier_on(etdev->netdev);
		} else {
			dev_warn(&etdev->pdev->dev,
			    "Link down - cable problem ?\n");

			if (etdev->linkspeed == TRUEPHY_SPEED_10MBPS) {
				/* NOTE - Is there a way to query this without
				 * TruePHY?
				 * && TRU_QueryCoreType(etdev->hTruePhy, 0) == EMI_TRUEPHY_A13O) {
				 */
				uint16_t Register18;

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

				/* Only indicate state if we're in loopback
				 * mode
				 */
				if (etdev->RegistryPhyLoopbk == false)
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

			etdev->PoMgmt.TransPhyComaModeOnBoot = 20;

			if (etdev->linkspeed == TRUEPHY_SPEED_10MBPS) {
				/*
				 * NOTE - Is there a way to query this without
				 * TruePHY?
				 * && TRU_QueryCoreType(etdev->hTruePhy, 0)== EMI_TRUEPHY_A13O) {
				 */
				uint16_t Register18;

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

/**
 * TPAL_SetPhy10HalfDuplex - Force the phy into 10 Base T Half Duplex mode.
 * @etdev: pointer to the adapter structure
 *
 * Also sets the MAC so it is syncd up properly
 */
void TPAL_SetPhy10HalfDuplex(struct et131x_adapter *etdev)
{
	/* Power down PHY */
	ET1310_PhyPowerDown(etdev, 1);

	/* First we need to turn off all other advertisement */
	ET1310_PhyAdvertise1000BaseT(etdev, TRUEPHY_ADV_DUPLEX_NONE);

	ET1310_PhyAdvertise100BaseT(etdev, TRUEPHY_ADV_DUPLEX_NONE);

	/* Set our advertise values accordingly */
	ET1310_PhyAdvertise10BaseT(etdev, TRUEPHY_ADV_DUPLEX_HALF);

	/* Power up PHY */
	ET1310_PhyPowerDown(etdev, 0);
}

/**
 * TPAL_SetPhy10FullDuplex - Force the phy into 10 Base T Full Duplex mode.
 * @etdev: pointer to the adapter structure
 *
 * Also sets the MAC so it is syncd up properly
 */
void TPAL_SetPhy10FullDuplex(struct et131x_adapter *etdev)
{
	/* Power down PHY */
	ET1310_PhyPowerDown(etdev, 1);

	/* First we need to turn off all other advertisement */
	ET1310_PhyAdvertise1000BaseT(etdev, TRUEPHY_ADV_DUPLEX_NONE);

	ET1310_PhyAdvertise100BaseT(etdev, TRUEPHY_ADV_DUPLEX_NONE);

	/* Set our advertise values accordingly */
	ET1310_PhyAdvertise10BaseT(etdev, TRUEPHY_ADV_DUPLEX_FULL);

	/* Power up PHY */
	ET1310_PhyPowerDown(etdev, 0);
}

/**
 * TPAL_SetPhy10Force - Force Base-T FD mode WITHOUT using autonegotiation
 * @etdev: pointer to the adapter structure
 */
void TPAL_SetPhy10Force(struct et131x_adapter *etdev)
{
	/* Power down PHY */
	ET1310_PhyPowerDown(etdev, 1);

	/* Disable autoneg */
	ET1310_PhyAutoNeg(etdev, false);

	/* Disable all advertisement */
	ET1310_PhyAdvertise1000BaseT(etdev, TRUEPHY_ADV_DUPLEX_NONE);
	ET1310_PhyAdvertise10BaseT(etdev, TRUEPHY_ADV_DUPLEX_NONE);
	ET1310_PhyAdvertise100BaseT(etdev, TRUEPHY_ADV_DUPLEX_NONE);

	/* Force 10 Mbps */
	ET1310_PhySpeedSelect(etdev, TRUEPHY_SPEED_10MBPS);

	/* Force Full duplex */
	ET1310_PhyDuplexMode(etdev, TRUEPHY_DUPLEX_FULL);

	/* Power up PHY */
	ET1310_PhyPowerDown(etdev, 0);
}

/**
 * TPAL_SetPhy100HalfDuplex - Force 100 Base T Half Duplex mode.
 * @etdev: pointer to the adapter structure
 *
 * Also sets the MAC so it is syncd up properly.
 */
void TPAL_SetPhy100HalfDuplex(struct et131x_adapter *etdev)
{
	/* Power down PHY */
	ET1310_PhyPowerDown(etdev, 1);

	/* first we need to turn off all other advertisement */
	ET1310_PhyAdvertise1000BaseT(etdev, TRUEPHY_ADV_DUPLEX_NONE);

	ET1310_PhyAdvertise10BaseT(etdev, TRUEPHY_ADV_DUPLEX_NONE);

	/* Set our advertise values accordingly */
	ET1310_PhyAdvertise100BaseT(etdev, TRUEPHY_ADV_DUPLEX_HALF);

	/* Set speed */
	ET1310_PhySpeedSelect(etdev, TRUEPHY_SPEED_100MBPS);

	/* Power up PHY */
	ET1310_PhyPowerDown(etdev, 0);
}

/**
 * TPAL_SetPhy100FullDuplex - Force 100 Base T Full Duplex mode.
 * @etdev: pointer to the adapter structure
 *
 * Also sets the MAC so it is syncd up properly
 */
void TPAL_SetPhy100FullDuplex(struct et131x_adapter *etdev)
{
	/* Power down PHY */
	ET1310_PhyPowerDown(etdev, 1);

	/* First we need to turn off all other advertisement */
	ET1310_PhyAdvertise1000BaseT(etdev, TRUEPHY_ADV_DUPLEX_NONE);

	ET1310_PhyAdvertise10BaseT(etdev, TRUEPHY_ADV_DUPLEX_NONE);

	/* Set our advertise values accordingly */
	ET1310_PhyAdvertise100BaseT(etdev, TRUEPHY_ADV_DUPLEX_FULL);

	/* Power up PHY */
	ET1310_PhyPowerDown(etdev, 0);
}

/**
 * TPAL_SetPhy100Force - Force 100 BaseT FD mode WITHOUT using autonegotiation
 * @etdev: pointer to the adapter structure
 */
void TPAL_SetPhy100Force(struct et131x_adapter *etdev)
{
	/* Power down PHY */
	ET1310_PhyPowerDown(etdev, 1);

	/* Disable autoneg */
	ET1310_PhyAutoNeg(etdev, false);

	/* Disable all advertisement */
	ET1310_PhyAdvertise1000BaseT(etdev, TRUEPHY_ADV_DUPLEX_NONE);
	ET1310_PhyAdvertise10BaseT(etdev, TRUEPHY_ADV_DUPLEX_NONE);
	ET1310_PhyAdvertise100BaseT(etdev, TRUEPHY_ADV_DUPLEX_NONE);

	/* Force 100 Mbps */
	ET1310_PhySpeedSelect(etdev, TRUEPHY_SPEED_100MBPS);

	/* Force Full duplex */
	ET1310_PhyDuplexMode(etdev, TRUEPHY_DUPLEX_FULL);

	/* Power up PHY */
	ET1310_PhyPowerDown(etdev, 0);
}

/**
 * TPAL_SetPhy1000FullDuplex - Force 1000 Base T Full Duplex mode
 * @etdev: pointer to the adapter structure
 *
 * Also sets the MAC so it is syncd up properly.
 */
void TPAL_SetPhy1000FullDuplex(struct et131x_adapter *etdev)
{
	/* Power down PHY */
	ET1310_PhyPowerDown(etdev, 1);

	/* first we need to turn off all other advertisement */
	ET1310_PhyAdvertise100BaseT(etdev, TRUEPHY_ADV_DUPLEX_NONE);

	ET1310_PhyAdvertise10BaseT(etdev, TRUEPHY_ADV_DUPLEX_NONE);

	/* set our advertise values accordingly */
	ET1310_PhyAdvertise1000BaseT(etdev, TRUEPHY_ADV_DUPLEX_FULL);

	/* power up PHY */
	ET1310_PhyPowerDown(etdev, 0);
}

/**
 * TPAL_SetPhyAutoNeg - Set phy to autonegotiation mode.
 * @etdev: pointer to the adapter structure
 */
void TPAL_SetPhyAutoNeg(struct et131x_adapter *etdev)
{
	/* Power down PHY */
	ET1310_PhyPowerDown(etdev, 1);

	/* Turn on advertisement of all capabilities */
	ET1310_PhyAdvertise10BaseT(etdev, TRUEPHY_ADV_DUPLEX_BOTH);

	ET1310_PhyAdvertise100BaseT(etdev, TRUEPHY_ADV_DUPLEX_BOTH);

	if (etdev->pdev->device != ET131X_PCI_DEVICE_ID_FAST)
		ET1310_PhyAdvertise1000BaseT(etdev, TRUEPHY_ADV_DUPLEX_FULL);
	else
		ET1310_PhyAdvertise1000BaseT(etdev, TRUEPHY_ADV_DUPLEX_NONE);

	/* Make sure auto-neg is ON (it is disabled in FORCE modes) */
	ET1310_PhyAutoNeg(etdev, true);

	/* Power up PHY */
	ET1310_PhyPowerDown(etdev, 0);
}


/*
 * The routines which follow provide low-level access to the PHY, and are used
 * primarily by the routines above (although there are a few places elsewhere
 * in the driver where this level of access is required).
 */

static const uint16_t ConfigPhy[25][2] = {
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
	uint16_t data, index;

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

void ET1310_PhyReset(struct et131x_adapter *etdev)
{
	MiWrite(etdev, PHY_CONTROL, 0x8000);
}

void ET1310_PhyPowerDown(struct et131x_adapter *etdev, bool down)
{
	uint16_t data;

	MiRead(etdev, PHY_CONTROL, &data);

	if (down == false) {
		/* Power UP */
		data &= ~0x0800;
		MiWrite(etdev, PHY_CONTROL, data);
	} else {
		/* Power DOWN */
		data |= 0x0800;
		MiWrite(etdev, PHY_CONTROL, data);
	}
}

void ET1310_PhyAutoNeg(struct et131x_adapter *etdev, bool enable)
{
	uint16_t data;

	MiRead(etdev, PHY_CONTROL, &data);

	if (enable == true) {
		/* Autonegotiation ON */
		data |= 0x1000;
		MiWrite(etdev, PHY_CONTROL, data);
	} else {
		/* Autonegotiation OFF */
		data &= ~0x1000;
		MiWrite(etdev, PHY_CONTROL, data);
	}
}

void ET1310_PhyDuplexMode(struct et131x_adapter *etdev, uint16_t duplex)
{
	uint16_t data;

	MiRead(etdev, PHY_CONTROL, &data);

	if (duplex == TRUEPHY_DUPLEX_FULL) {
		/* Set Full Duplex */
		data |= 0x100;
		MiWrite(etdev, PHY_CONTROL, data);
	} else {
		/* Set Half Duplex */
		data &= ~0x100;
		MiWrite(etdev, PHY_CONTROL, data);
	}
}

void ET1310_PhySpeedSelect(struct et131x_adapter *etdev, uint16_t speed)
{
	uint16_t data;

	/* Read the PHY control register */
	MiRead(etdev, PHY_CONTROL, &data);

	/* Clear all Speed settings (Bits 6, 13) */
	data &= ~0x2040;

	/* Reset the speed bits based on user selection */
	switch (speed) {
	case TRUEPHY_SPEED_10MBPS:
		/* Bits already cleared above, do nothing */
		break;

	case TRUEPHY_SPEED_100MBPS:
		/* 100M == Set bit 13 */
		data |= 0x2000;
		break;

	case TRUEPHY_SPEED_1000MBPS:
	default:
		data |= 0x0040;
		break;
	}

	/* Write back the new speed */
	MiWrite(etdev, PHY_CONTROL, data);
}

void ET1310_PhyAdvertise1000BaseT(struct et131x_adapter *etdev,
				  uint16_t duplex)
{
	uint16_t data;

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

void ET1310_PhyAdvertise100BaseT(struct et131x_adapter *etdev,
				 uint16_t duplex)
{
	uint16_t data;

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

void ET1310_PhyAdvertise10BaseT(struct et131x_adapter *etdev,
				uint16_t duplex)
{
	uint16_t data;

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

void ET1310_PhyLinkStatus(struct et131x_adapter *etdev,
			  uint8_t *link_status,
			  uint32_t *autoneg,
			  uint32_t *linkspeed,
			  uint32_t *duplex_mode,
			  uint32_t *mdi_mdix,
			  uint32_t *masterslave, uint32_t *polarity)
{
	uint16_t mistatus = 0;
	uint16_t is1000BaseT = 0;
	uint16_t vmi_phystatus = 0;
	uint16_t control = 0;

	MiRead(etdev, PHY_STATUS, &mistatus);
	MiRead(etdev, PHY_1000_STATUS, &is1000BaseT);
	MiRead(etdev, PHY_PHY_STATUS, &vmi_phystatus);
	MiRead(etdev, PHY_CONTROL, &control);

	if (link_status) {
		*link_status =
		    (unsigned char)((vmi_phystatus & 0x0040) ? 1 : 0);
	}

	if (autoneg) {
		*autoneg =
		    (control & 0x1000) ? ((vmi_phystatus & 0x0020) ?
					    TRUEPHY_ANEG_COMPLETE :
					    TRUEPHY_ANEG_NOT_COMPLETE) :
		    TRUEPHY_ANEG_DISABLED;
	}

	if (linkspeed)
		*linkspeed = (vmi_phystatus & 0x0300) >> 8;

	if (duplex_mode)
		*duplex_mode = (vmi_phystatus & 0x0080) >> 7;

	if (mdi_mdix)
		/* NOTE: Need to complete this */
		*mdi_mdix = 0;

	if (masterslave) {
		*masterslave =
		    (is1000BaseT & 0x4000) ? TRUEPHY_CFG_MASTER :
		    TRUEPHY_CFG_SLAVE;
	}

	if (polarity) {
		*polarity =
		    (vmi_phystatus & 0x0400) ? TRUEPHY_POLARITY_INVERTED :
		    TRUEPHY_POLARITY_NORMAL;
	}
}

void ET1310_PhyAndOrReg(struct et131x_adapter *etdev,
			uint16_t regnum, uint16_t andMask, uint16_t orMask)
{
	uint16_t reg;

	/* Read the requested register */
	MiRead(etdev, regnum, &reg);

	/* Apply the AND mask */
	reg &= andMask;

	/* Apply the OR mask */
	reg |= orMask;

	/* Write the value back to the register */
	MiWrite(etdev, regnum, reg);
}

void ET1310_PhyAccessMiBit(struct et131x_adapter *etdev, uint16_t action,
			   uint16_t regnum, uint16_t bitnum, uint8_t *value)
{
	uint16_t reg;
	uint16_t mask = 0;

	/* Create a mask to isolate the requested bit */
	mask = 0x0001 << bitnum;

	/* Read the requested register */
	MiRead(etdev, regnum, &reg);

	switch (action) {
	case TRUEPHY_BIT_READ:
		if (value != NULL)
			*value = (reg & mask) >> bitnum;
		break;

	case TRUEPHY_BIT_SET:
		reg |= mask;
		MiWrite(etdev, regnum, reg);
		break;

	case TRUEPHY_BIT_CLEAR:
		reg &= ~mask;
		MiWrite(etdev, regnum, reg);
		break;

	default:
		break;
	}
}
