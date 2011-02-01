/*
 * Agere Systems Inc.
 * 10/100/1000 Base-T Ethernet Driver for the ET1301 and ET131x series MACs
 *
 * Copyright © 2005 Agere Systems Inc.
 * All rights reserved.
 *   http://www.agere.com
 *
 *------------------------------------------------------------------------------
 *
 * et1310_mac.c - All code and routines pertaining to the MAC
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
 * Copyright © 2005 Agere Systems Inc.
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
#include <linux/pci.h>
#include <asm/system.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/if_arp.h>
#include <linux/ioport.h>
#include <linux/crc32.h>

#include "et1310_phy.h"
#include "et131x_adapter.h"
#include "et131x.h"


#define COUNTER_WRAP_28_BIT 0x10000000
#define COUNTER_WRAP_22_BIT 0x400000
#define COUNTER_WRAP_16_BIT 0x10000
#define COUNTER_WRAP_12_BIT 0x1000

#define COUNTER_MASK_28_BIT (COUNTER_WRAP_28_BIT - 1)
#define COUNTER_MASK_22_BIT (COUNTER_WRAP_22_BIT - 1)
#define COUNTER_MASK_16_BIT (COUNTER_WRAP_16_BIT - 1)
#define COUNTER_MASK_12_BIT (COUNTER_WRAP_12_BIT - 1)

/**
 * ConfigMacRegs1 - Initialize the first part of MAC regs
 * @pAdpater: pointer to our adapter structure
 */
void ConfigMACRegs1(struct et131x_adapter *etdev)
{
	struct _MAC_t __iomem *pMac = &etdev->regs->mac;
	MAC_STATION_ADDR1_t station1;
	MAC_STATION_ADDR2_t station2;
	u32 ipg;

	/* First we need to reset everything.  Write to MAC configuration
	 * register 1 to perform reset.
	 */
	writel(0xC00F0000, &pMac->cfg1);

	/* Next lets configure the MAC Inter-packet gap register */
	ipg = 0x38005860;		/* IPG1 0x38 IPG2 0x58 B2B 0x60 */
	ipg |= 0x50 << 8;		/* ifg enforce 0x50 */
	writel(ipg, &pMac->ipg);

	/* Next lets configure the MAC Half Duplex register */
	/* BEB trunc 0xA, Ex Defer, Rexmit 0xF Coll 0x37 */
	writel(0x00A1F037, &pMac->hfdp);

	/* Next lets configure the MAC Interface Control register */
	writel(0, &pMac->if_ctrl);

	/* Let's move on to setting up the mii management configuration */
	writel(0x07, &pMac->mii_mgmt_cfg);	/* Clock reset 0x7 */

	/* Next lets configure the MAC Station Address register.  These
	 * values are read from the EEPROM during initialization and stored
	 * in the adapter structure.  We write what is stored in the adapter
	 * structure to the MAC Station Address registers high and low.  This
	 * station address is used for generating and checking pause control
	 * packets.
	 */
	station2.bits.Octet1 = etdev->addr[0];
	station2.bits.Octet2 = etdev->addr[1];
	station1.bits.Octet3 = etdev->addr[2];
	station1.bits.Octet4 = etdev->addr[3];
	station1.bits.Octet5 = etdev->addr[4];
	station1.bits.Octet6 = etdev->addr[5];
	writel(station1.value, &pMac->station_addr_1.value);
	writel(station2.value, &pMac->station_addr_2.value);

	/* Max ethernet packet in bytes that will passed by the mac without
	 * being truncated.  Allow the MAC to pass 4 more than our max packet
	 * size.  This is 4 for the Ethernet CRC.
	 *
	 * Packets larger than (RegistryJumboPacket) that do not contain a
	 * VLAN ID will be dropped by the Rx function.
	 */
	writel(etdev->RegistryJumboPacket + 4, &pMac->max_fm_len);

	/* clear out MAC config reset */
	writel(0, &pMac->cfg1);
}

/**
 * ConfigMacRegs2 - Initialize the second part of MAC regs
 * @pAdpater: pointer to our adapter structure
 */
void ConfigMACRegs2(struct et131x_adapter *etdev)
{
	int32_t delay = 0;
	struct _MAC_t __iomem *pMac = &etdev->regs->mac;
	u32 cfg1;
	u32 cfg2;
	u32 ifctrl;
	u32 ctl;

	ctl = readl(&etdev->regs->txmac.ctl);
	cfg1 = readl(&pMac->cfg1);
	cfg2 = readl(&pMac->cfg2);
	ifctrl = readl(&pMac->if_ctrl);

	/* Set up the if mode bits */
	cfg2 &= ~0x300;
	if (etdev->linkspeed == TRUEPHY_SPEED_1000MBPS) {
		cfg2 |= 0x200;
		/* Phy mode bit */
		ifctrl &= ~(1 << 24);
	} else {
		cfg2 |= 0x100;
		ifctrl |= (1 << 24);
	}

	/* We need to enable Rx/Tx */
	cfg1 |= CFG1_RX_ENABLE|CFG1_TX_ENABLE|CFG1_TX_FLOW;
	/* Initialize loop back to off */
	cfg1 &= ~(CFG1_LOOPBACK|CFG1_RX_FLOW);
	if (etdev->flowcontrol == FLOW_RXONLY || etdev->flowcontrol == FLOW_BOTH)
		cfg1 |= CFG1_RX_FLOW;
	writel(cfg1, &pMac->cfg1);

	/* Now we need to initialize the MAC Configuration 2 register */
	/* preamble 7, check length, huge frame off, pad crc, crc enable
	   full duplex off */
	cfg2 |= 0x7016;
	cfg2 &= ~0x0021;

	/* Turn on duplex if needed */
	if (etdev->duplex_mode)
		cfg2 |= 0x01;

	ifctrl &= ~(1 << 26);
	if (!etdev->duplex_mode)
		ifctrl |= (1<<26);	/* Enable ghd */

	writel(ifctrl, &pMac->if_ctrl);
	writel(cfg2, &pMac->cfg2);

	do {
		udelay(10);
		delay++;
		cfg1 = readl(&pMac->cfg1);
	} while ((cfg1 & CFG1_WAIT) != CFG1_WAIT && delay < 100);

	if (delay == 100) {
		dev_warn(&etdev->pdev->dev,
		    "Syncd bits did not respond correctly cfg1 word 0x%08x\n",
			cfg1);
	}

	/* Enable TXMAC */
	ctl |= 0x09;	/* TX mac enable, FC disable */
	writel(ctl, &etdev->regs->txmac.ctl);

	/* Ready to start the RXDMA/TXDMA engine */
	if (etdev->Flags & fMP_ADAPTER_LOWER_POWER) {
		et131x_rx_dma_enable(etdev);
		et131x_tx_dma_enable(etdev);
	}
}

void ConfigRxMacRegs(struct et131x_adapter *etdev)
{
	struct _RXMAC_t __iomem *pRxMac = &etdev->regs->rxmac;
	RXMAC_WOL_SA_LO_t sa_lo;
	RXMAC_WOL_SA_HI_t sa_hi;
	u32 pf_ctrl = 0;

	/* Disable the MAC while it is being configured (also disable WOL) */
	writel(0x8, &pRxMac->ctrl);

	/* Initialize WOL to disabled. */
	writel(0, &pRxMac->crc0);
	writel(0, &pRxMac->crc12);
	writel(0, &pRxMac->crc34);

	/* We need to set the WOL mask0 - mask4 next.  We initialize it to
	 * its default Values of 0x00000000 because there are not WOL masks
	 * as of this time.
	 */
	writel(0, &pRxMac->mask0_word0);
	writel(0, &pRxMac->mask0_word1);
	writel(0, &pRxMac->mask0_word2);
	writel(0, &pRxMac->mask0_word3);

	writel(0, &pRxMac->mask1_word0);
	writel(0, &pRxMac->mask1_word1);
	writel(0, &pRxMac->mask1_word2);
	writel(0, &pRxMac->mask1_word3);

	writel(0, &pRxMac->mask2_word0);
	writel(0, &pRxMac->mask2_word1);
	writel(0, &pRxMac->mask2_word2);
	writel(0, &pRxMac->mask2_word3);

	writel(0, &pRxMac->mask3_word0);
	writel(0, &pRxMac->mask3_word1);
	writel(0, &pRxMac->mask3_word2);
	writel(0, &pRxMac->mask3_word3);

	writel(0, &pRxMac->mask4_word0);
	writel(0, &pRxMac->mask4_word1);
	writel(0, &pRxMac->mask4_word2);
	writel(0, &pRxMac->mask4_word3);

	/* Lets setup the WOL Source Address */
	sa_lo.bits.sa3 = etdev->addr[2];
	sa_lo.bits.sa4 = etdev->addr[3];
	sa_lo.bits.sa5 = etdev->addr[4];
	sa_lo.bits.sa6 = etdev->addr[5];
	writel(sa_lo.value, &pRxMac->sa_lo.value);

	sa_hi.bits.sa1 = etdev->addr[0];
	sa_hi.bits.sa2 = etdev->addr[1];
	writel(sa_hi.value, &pRxMac->sa_hi.value);

	/* Disable all Packet Filtering */
	writel(0, &pRxMac->pf_ctrl);

	/* Let's initialize the Unicast Packet filtering address */
	if (etdev->PacketFilter & ET131X_PACKET_TYPE_DIRECTED) {
		SetupDeviceForUnicast(etdev);
		pf_ctrl |= 4;	/* Unicast filter */
	} else {
		writel(0, &pRxMac->uni_pf_addr1.value);
		writel(0, &pRxMac->uni_pf_addr2.value);
		writel(0, &pRxMac->uni_pf_addr3.value);
	}

	/* Let's initialize the Multicast hash */
	if (!(etdev->PacketFilter & ET131X_PACKET_TYPE_ALL_MULTICAST)) {
		pf_ctrl |= 2;	/* Multicast filter */
		SetupDeviceForMulticast(etdev);
	}

	/* Runt packet filtering.  Didn't work in version A silicon. */
	pf_ctrl |= (NIC_MIN_PACKET_SIZE + 4) << 16;
	pf_ctrl |= 8;	/* Fragment filter */

	if (etdev->RegistryJumboPacket > 8192)
		/* In order to transmit jumbo packets greater than 8k, the
		 * FIFO between RxMAC and RxDMA needs to be reduced in size
		 * to (16k - Jumbo packet size).  In order to implement this,
		 * we must use "cut through" mode in the RxMAC, which chops
		 * packets down into segments which are (max_size * 16).  In
		 * this case we selected 256 bytes, since this is the size of
		 * the PCI-Express TLP's that the 1310 uses.
		 *
		 * seg_en on, fc_en off, size 0x10
		 */
		writel(0x41, &pRxMac->mcif_ctrl_max_seg);
	else
		writel(0, &pRxMac->mcif_ctrl_max_seg);

	/* Initialize the MCIF water marks */
	writel(0, &pRxMac->mcif_water_mark);

	/*  Initialize the MIF control */
	writel(0, &pRxMac->mif_ctrl);

	/* Initialize the Space Available Register */
	writel(0, &pRxMac->space_avail);

	/* Initialize the the mif_ctrl register
	 * bit 3:  Receive code error. One or more nibbles were signaled as
	 *	   errors  during the reception of the packet.  Clear this
	 *	   bit in Gigabit, set it in 100Mbit.  This was derived
	 *	   experimentally at UNH.
	 * bit 4:  Receive CRC error. The packet's CRC did not match the
	 *	   internally generated CRC.
	 * bit 5:  Receive length check error. Indicates that frame length
	 *	   field value in the packet does not match the actual data
	 *	   byte length and is not a type field.
	 * bit 16: Receive frame truncated.
	 * bit 17: Drop packet enable
	 */
	if (etdev->linkspeed == TRUEPHY_SPEED_100MBPS)
		writel(0x30038, &pRxMac->mif_ctrl);
	else
		writel(0x30030, &pRxMac->mif_ctrl);

	/* Finally we initialize RxMac to be enabled & WOL disabled.  Packet
	 * filter is always enabled since it is where the runt packets are
	 * supposed to be dropped.  For version A silicon, runt packet
	 * dropping doesn't work, so it is disabled in the pf_ctrl register,
	 * but we still leave the packet filter on.
	 */
	writel(pf_ctrl, &pRxMac->pf_ctrl);
	writel(0x9, &pRxMac->ctrl);
}

void ConfigTxMacRegs(struct et131x_adapter *etdev)
{
	struct txmac_regs *txmac = &etdev->regs->txmac;

	/* We need to update the Control Frame Parameters
	 * cfpt - control frame pause timer set to 64 (0x40)
	 * cfep - control frame extended pause timer set to 0x0
	 */
	if (etdev->flowcontrol == FLOW_NONE)
		writel(0, &txmac->cf_param);
	else
		writel(0x40, &txmac->cf_param);
}

void ConfigMacStatRegs(struct et131x_adapter *etdev)
{
	struct macstat_regs __iomem *macstat =
		&etdev->regs->macstat;

	/* Next we need to initialize all the MAC_STAT registers to zero on
	 * the device.
	 */
	writel(0, &macstat->RFcs);
	writel(0, &macstat->RAln);
	writel(0, &macstat->RFlr);
	writel(0, &macstat->RDrp);
	writel(0, &macstat->RCde);
	writel(0, &macstat->ROvr);
	writel(0, &macstat->RFrg);

	writel(0, &macstat->TScl);
	writel(0, &macstat->TDfr);
	writel(0, &macstat->TMcl);
	writel(0, &macstat->TLcl);
	writel(0, &macstat->TNcl);
	writel(0, &macstat->TOvr);
	writel(0, &macstat->TUnd);

	/* Unmask any counters that we want to track the overflow of.
	 * Initially this will be all counters.  It may become clear later
	 * that we do not need to track all counters.
	 */
	writel(0xFFFFBE32, &macstat->Carry1M);
	writel(0xFFFE7E8B, &macstat->Carry2M);
}

void ConfigFlowControl(struct et131x_adapter *etdev)
{
	if (etdev->duplex_mode == 0) {
		etdev->flowcontrol = FLOW_NONE;
	} else {
		char remote_pause, remote_async_pause;

		ET1310_PhyAccessMiBit(etdev,
				      TRUEPHY_BIT_READ, 5, 10, &remote_pause);
		ET1310_PhyAccessMiBit(etdev,
				      TRUEPHY_BIT_READ, 5, 11,
				      &remote_async_pause);

		if ((remote_pause == TRUEPHY_BIT_SET) &&
		    (remote_async_pause == TRUEPHY_BIT_SET)) {
			etdev->flowcontrol = etdev->wanted_flow;
		} else if ((remote_pause == TRUEPHY_BIT_SET) &&
			   (remote_async_pause == TRUEPHY_BIT_CLEAR)) {
			if (etdev->wanted_flow == FLOW_BOTH)
				etdev->flowcontrol = FLOW_BOTH;
			else
				etdev->flowcontrol = FLOW_NONE;
		} else if ((remote_pause == TRUEPHY_BIT_CLEAR) &&
			   (remote_async_pause == TRUEPHY_BIT_CLEAR)) {
			etdev->flowcontrol = FLOW_NONE;
		} else {/* if (remote_pause == TRUEPHY_CLEAR_BIT &&
			       remote_async_pause == TRUEPHY_SET_BIT) */
			if (etdev->wanted_flow == FLOW_BOTH)
				etdev->flowcontrol = FLOW_RXONLY;
			else
				etdev->flowcontrol = FLOW_NONE;
		}
	}
}

/**
 * UpdateMacStatHostCounters - Update the local copy of the statistics
 * @etdev: pointer to the adapter structure
 */
void UpdateMacStatHostCounters(struct et131x_adapter *etdev)
{
	struct _ce_stats_t *stats = &etdev->Stats;
	struct macstat_regs __iomem *macstat =
		&etdev->regs->macstat;

	stats->collisions += readl(&macstat->TNcl);
	stats->first_collision += readl(&macstat->TScl);
	stats->tx_deferred += readl(&macstat->TDfr);
	stats->excessive_collisions += readl(&macstat->TMcl);
	stats->late_collisions += readl(&macstat->TLcl);
	stats->tx_uflo += readl(&macstat->TUnd);
	stats->max_pkt_error += readl(&macstat->TOvr);

	stats->alignment_err += readl(&macstat->RAln);
	stats->crc_err += readl(&macstat->RCde);
	stats->norcvbuf += readl(&macstat->RDrp);
	stats->rx_ov_flow += readl(&macstat->ROvr);
	stats->code_violations += readl(&macstat->RFcs);
	stats->length_err += readl(&macstat->RFlr);

	stats->other_errors += readl(&macstat->RFrg);
}

/**
 * HandleMacStatInterrupt
 * @etdev: pointer to the adapter structure
 *
 * One of the MACSTAT counters has wrapped.  Update the local copy of
 * the statistics held in the adapter structure, checking the "wrap"
 * bit for each counter.
 */
void HandleMacStatInterrupt(struct et131x_adapter *etdev)
{
	u32 Carry1;
	u32 Carry2;

	/* Read the interrupt bits from the register(s).  These are Clear On
	 * Write.
	 */
	Carry1 = readl(&etdev->regs->macstat.Carry1);
	Carry2 = readl(&etdev->regs->macstat.Carry2);

	writel(Carry1, &etdev->regs->macstat.Carry1);
	writel(Carry2, &etdev->regs->macstat.Carry2);

	/* We need to do update the host copy of all the MAC_STAT counters.
	 * For each counter, check it's overflow bit.  If the overflow bit is
	 * set, then increment the host version of the count by one complete
	 * revolution of the counter.  This routine is called when the counter
	 * block indicates that one of the counters has wrapped.
	 */
	if (Carry1 & (1 << 14))
		etdev->Stats.code_violations += COUNTER_WRAP_16_BIT;
	if (Carry1 & (1 << 8))
		etdev->Stats.alignment_err += COUNTER_WRAP_12_BIT;
	if (Carry1 & (1 << 7))
		etdev->Stats.length_err += COUNTER_WRAP_16_BIT;
	if (Carry1 & (1 << 2))
		etdev->Stats.other_errors += COUNTER_WRAP_16_BIT;
	if (Carry1 & (1 << 6))
		etdev->Stats.crc_err += COUNTER_WRAP_16_BIT;
	if (Carry1 & (1 << 3))
		etdev->Stats.rx_ov_flow += COUNTER_WRAP_16_BIT;
	if (Carry1 & (1 << 0))
		etdev->Stats.norcvbuf += COUNTER_WRAP_16_BIT;
	if (Carry2 & (1 << 16))
		etdev->Stats.max_pkt_error += COUNTER_WRAP_12_BIT;
	if (Carry2 & (1 << 15))
		etdev->Stats.tx_uflo += COUNTER_WRAP_12_BIT;
	if (Carry2 & (1 << 6))
		etdev->Stats.first_collision += COUNTER_WRAP_12_BIT;
	if (Carry2 & (1 << 8))
		etdev->Stats.tx_deferred += COUNTER_WRAP_12_BIT;
	if (Carry2 & (1 << 5))
		etdev->Stats.excessive_collisions += COUNTER_WRAP_12_BIT;
	if (Carry2 & (1 << 4))
		etdev->Stats.late_collisions += COUNTER_WRAP_12_BIT;
	if (Carry2 & (1 << 2))
		etdev->Stats.collisions += COUNTER_WRAP_12_BIT;
}

void SetupDeviceForMulticast(struct et131x_adapter *etdev)
{
	struct _RXMAC_t __iomem *rxmac = &etdev->regs->rxmac;
	uint32_t nIndex;
	uint32_t result;
	uint32_t hash1 = 0;
	uint32_t hash2 = 0;
	uint32_t hash3 = 0;
	uint32_t hash4 = 0;
	u32 pm_csr;

	/* If ET131X_PACKET_TYPE_MULTICAST is specified, then we provision
	 * the multi-cast LIST.  If it is NOT specified, (and "ALL" is not
	 * specified) then we should pass NO multi-cast addresses to the
	 * driver.
	 */
	if (etdev->PacketFilter & ET131X_PACKET_TYPE_MULTICAST) {
		/* Loop through our multicast array and set up the device */
		for (nIndex = 0; nIndex < etdev->MCAddressCount; nIndex++) {
			result = ether_crc(6, etdev->MCList[nIndex]);

			result = (result & 0x3F800000) >> 23;

			if (result < 32) {
				hash1 |= (1 << result);
			} else if ((31 < result) && (result < 64)) {
				result -= 32;
				hash2 |= (1 << result);
			} else if ((63 < result) && (result < 96)) {
				result -= 64;
				hash3 |= (1 << result);
			} else {
				result -= 96;
				hash4 |= (1 << result);
			}
		}
	}

	/* Write out the new hash to the device */
	pm_csr = readl(&etdev->regs->global.pm_csr);
	if ((pm_csr & ET_PM_PHY_SW_COMA) == 0) {
		writel(hash1, &rxmac->multi_hash1);
		writel(hash2, &rxmac->multi_hash2);
		writel(hash3, &rxmac->multi_hash3);
		writel(hash4, &rxmac->multi_hash4);
	}
}

void SetupDeviceForUnicast(struct et131x_adapter *etdev)
{
	struct _RXMAC_t __iomem *rxmac = &etdev->regs->rxmac;
	RXMAC_UNI_PF_ADDR1_t uni_pf1;
	RXMAC_UNI_PF_ADDR2_t uni_pf2;
	RXMAC_UNI_PF_ADDR3_t uni_pf3;
	u32 pm_csr;

	/* Set up unicast packet filter reg 3 to be the first two octets of
	 * the MAC address for both address
	 *
	 * Set up unicast packet filter reg 2 to be the octets 2 - 5 of the
	 * MAC address for second address
	 *
	 * Set up unicast packet filter reg 3 to be the octets 2 - 5 of the
	 * MAC address for first address
	 */
	uni_pf3.bits.addr1_1 = etdev->addr[0];
	uni_pf3.bits.addr1_2 = etdev->addr[1];
	uni_pf3.bits.addr2_1 = etdev->addr[0];
	uni_pf3.bits.addr2_2 = etdev->addr[1];

	uni_pf2.bits.addr2_3 = etdev->addr[2];
	uni_pf2.bits.addr2_4 = etdev->addr[3];
	uni_pf2.bits.addr2_5 = etdev->addr[4];
	uni_pf2.bits.addr2_6 = etdev->addr[5];

	uni_pf1.bits.addr1_3 = etdev->addr[2];
	uni_pf1.bits.addr1_4 = etdev->addr[3];
	uni_pf1.bits.addr1_5 = etdev->addr[4];
	uni_pf1.bits.addr1_6 = etdev->addr[5];

	pm_csr = readl(&etdev->regs->global.pm_csr);
	if ((pm_csr & ET_PM_PHY_SW_COMA) == 0) {
		writel(uni_pf1.value, &rxmac->uni_pf_addr1.value);
		writel(uni_pf2.value, &rxmac->uni_pf_addr2.value);
		writel(uni_pf3.value, &rxmac->uni_pf_addr3.value);
	}
}
