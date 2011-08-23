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
 * et1310_config_mac_regs1 - Initialize the first part of MAC regs
 * @adapter: pointer to our adapter structure
 */
void et1310_config_mac_regs1(struct et131x_adapter *adapter)
{
	struct mac_regs __iomem *macregs = &adapter->regs->mac;
	u32 station1;
	u32 station2;
	u32 ipg;

	/* First we need to reset everything.  Write to MAC configuration
	 * register 1 to perform reset.
	 */
	writel(0xC00F0000, &macregs->cfg1);

	/* Next lets configure the MAC Inter-packet gap register */
	ipg = 0x38005860;		/* IPG1 0x38 IPG2 0x58 B2B 0x60 */
	ipg |= 0x50 << 8;		/* ifg enforce 0x50 */
	writel(ipg, &macregs->ipg);

	/* Next lets configure the MAC Half Duplex register */
	/* BEB trunc 0xA, Ex Defer, Rexmit 0xF Coll 0x37 */
	writel(0x00A1F037, &macregs->hfdp);

	/* Next lets configure the MAC Interface Control register */
	writel(0, &macregs->if_ctrl);

	/* Let's move on to setting up the mii management configuration */
	writel(0x07, &macregs->mii_mgmt_cfg);	/* Clock reset 0x7 */

	/* Next lets configure the MAC Station Address register.  These
	 * values are read from the EEPROM during initialization and stored
	 * in the adapter structure.  We write what is stored in the adapter
	 * structure to the MAC Station Address registers high and low.  This
	 * station address is used for generating and checking pause control
	 * packets.
	 */
	station2 = (adapter->addr[1] << ET_MAC_STATION_ADDR2_OC2_SHIFT) |
		   (adapter->addr[0] << ET_MAC_STATION_ADDR2_OC1_SHIFT);
	station1 = (adapter->addr[5] << ET_MAC_STATION_ADDR1_OC6_SHIFT) |
		   (adapter->addr[4] << ET_MAC_STATION_ADDR1_OC5_SHIFT) |
		   (adapter->addr[3] << ET_MAC_STATION_ADDR1_OC4_SHIFT) |
		    adapter->addr[2];
	writel(station1, &macregs->station_addr_1);
	writel(station2, &macregs->station_addr_2);

	/* Max ethernet packet in bytes that will passed by the mac without
	 * being truncated.  Allow the MAC to pass 4 more than our max packet
	 * size.  This is 4 for the Ethernet CRC.
	 *
	 * Packets larger than (registry_jumbo_packet) that do not contain a
	 * VLAN ID will be dropped by the Rx function.
	 */
	writel(adapter->registry_jumbo_packet + 4, &macregs->max_fm_len);

	/* clear out MAC config reset */
	writel(0, &macregs->cfg1);
}

/**
 * et1310_config_mac_regs2 - Initialize the second part of MAC regs
 * @adapter: pointer to our adapter structure
 */
void et1310_config_mac_regs2(struct et131x_adapter *adapter)
{
	int32_t delay = 0;
	struct mac_regs __iomem *mac = &adapter->regs->mac;
	u32 cfg1;
	u32 cfg2;
	u32 ifctrl;
	u32 ctl;

	ctl = readl(&adapter->regs->txmac.ctl);
	cfg1 = readl(&mac->cfg1);
	cfg2 = readl(&mac->cfg2);
	ifctrl = readl(&mac->if_ctrl);

	/* Set up the if mode bits */
	cfg2 &= ~0x300;
	if (adapter->linkspeed == TRUEPHY_SPEED_1000MBPS) {
		cfg2 |= 0x200;
		/* Phy mode bit */
		ifctrl &= ~(1 << 24);
	} else {
		cfg2 |= 0x100;
		ifctrl |= (1 << 24);
	}

	/* We need to enable Rx/Tx */
	cfg1 |= CFG1_RX_ENABLE | CFG1_TX_ENABLE | CFG1_TX_FLOW;
	/* Initialize loop back to off */
	cfg1 &= ~(CFG1_LOOPBACK | CFG1_RX_FLOW);
	if (adapter->flowcontrol == FLOW_RXONLY || adapter->flowcontrol == FLOW_BOTH)
		cfg1 |= CFG1_RX_FLOW;
	writel(cfg1, &mac->cfg1);

	/* Now we need to initialize the MAC Configuration 2 register */
	/* preamble 7, check length, huge frame off, pad crc, crc enable
	   full duplex off */
	cfg2 |= 0x7016;
	cfg2 &= ~0x0021;

	/* Turn on duplex if needed */
	if (adapter->duplex_mode)
		cfg2 |= 0x01;

	ifctrl &= ~(1 << 26);
	if (!adapter->duplex_mode)
		ifctrl |= (1<<26);	/* Enable ghd */

	writel(ifctrl, &mac->if_ctrl);
	writel(cfg2, &mac->cfg2);

	do {
		udelay(10);
		delay++;
		cfg1 = readl(&mac->cfg1);
	} while ((cfg1 & CFG1_WAIT) != CFG1_WAIT && delay < 100);

	if (delay == 100) {
		dev_warn(&adapter->pdev->dev,
		    "Syncd bits did not respond correctly cfg1 word 0x%08x\n",
			cfg1);
	}

	/* Enable txmac */
	ctl |= 0x09;	/* TX mac enable, FC disable */
	writel(ctl, &adapter->regs->txmac.ctl);

	/* Ready to start the RXDMA/TXDMA engine */
	if (adapter->flags & fMP_ADAPTER_LOWER_POWER) {
		et131x_rx_dma_enable(adapter);
		et131x_tx_dma_enable(adapter);
	}
}

void et1310_config_rxmac_regs(struct et131x_adapter *adapter)
{
	struct rxmac_regs __iomem *rxmac = &adapter->regs->rxmac;
	u32 sa_lo;
	u32 sa_hi = 0;
	u32 pf_ctrl = 0;

	/* Disable the MAC while it is being configured (also disable WOL) */
	writel(0x8, &rxmac->ctrl);

	/* Initialize WOL to disabled. */
	writel(0, &rxmac->crc0);
	writel(0, &rxmac->crc12);
	writel(0, &rxmac->crc34);

	/* We need to set the WOL mask0 - mask4 next.  We initialize it to
	 * its default Values of 0x00000000 because there are not WOL masks
	 * as of this time.
	 */
	writel(0, &rxmac->mask0_word0);
	writel(0, &rxmac->mask0_word1);
	writel(0, &rxmac->mask0_word2);
	writel(0, &rxmac->mask0_word3);

	writel(0, &rxmac->mask1_word0);
	writel(0, &rxmac->mask1_word1);
	writel(0, &rxmac->mask1_word2);
	writel(0, &rxmac->mask1_word3);

	writel(0, &rxmac->mask2_word0);
	writel(0, &rxmac->mask2_word1);
	writel(0, &rxmac->mask2_word2);
	writel(0, &rxmac->mask2_word3);

	writel(0, &rxmac->mask3_word0);
	writel(0, &rxmac->mask3_word1);
	writel(0, &rxmac->mask3_word2);
	writel(0, &rxmac->mask3_word3);

	writel(0, &rxmac->mask4_word0);
	writel(0, &rxmac->mask4_word1);
	writel(0, &rxmac->mask4_word2);
	writel(0, &rxmac->mask4_word3);

	/* Lets setup the WOL Source Address */
	sa_lo = (adapter->addr[2] << ET_WOL_LO_SA3_SHIFT) |
		(adapter->addr[3] << ET_WOL_LO_SA4_SHIFT) |
		(adapter->addr[4] << ET_WOL_LO_SA5_SHIFT) |
		 adapter->addr[5];
	writel(sa_lo, &rxmac->sa_lo);

	sa_hi = (u32) (adapter->addr[0] << ET_WOL_HI_SA1_SHIFT) |
	               adapter->addr[1];
	writel(sa_hi, &rxmac->sa_hi);

	/* Disable all Packet Filtering */
	writel(0, &rxmac->pf_ctrl);

	/* Let's initialize the Unicast Packet filtering address */
	if (adapter->packet_filter & ET131X_PACKET_TYPE_DIRECTED) {
		et1310_setup_device_for_unicast(adapter);
		pf_ctrl |= 4;	/* Unicast filter */
	} else {
		writel(0, &rxmac->uni_pf_addr1);
		writel(0, &rxmac->uni_pf_addr2);
		writel(0, &rxmac->uni_pf_addr3);
	}

	/* Let's initialize the Multicast hash */
	if (!(adapter->packet_filter & ET131X_PACKET_TYPE_ALL_MULTICAST)) {
		pf_ctrl |= 2;	/* Multicast filter */
		et1310_setup_device_for_multicast(adapter);
	}

	/* Runt packet filtering.  Didn't work in version A silicon. */
	pf_ctrl |= (NIC_MIN_PACKET_SIZE + 4) << 16;
	pf_ctrl |= 8;	/* Fragment filter */

	if (adapter->registry_jumbo_packet > 8192)
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
		writel(0x41, &rxmac->mcif_ctrl_max_seg);
	else
		writel(0, &rxmac->mcif_ctrl_max_seg);

	/* Initialize the MCIF water marks */
	writel(0, &rxmac->mcif_water_mark);

	/*  Initialize the MIF control */
	writel(0, &rxmac->mif_ctrl);

	/* Initialize the Space Available Register */
	writel(0, &rxmac->space_avail);

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
	if (adapter->linkspeed == TRUEPHY_SPEED_100MBPS)
		writel(0x30038, &rxmac->mif_ctrl);
	else
		writel(0x30030, &rxmac->mif_ctrl);

	/* Finally we initialize RxMac to be enabled & WOL disabled.  Packet
	 * filter is always enabled since it is where the runt packets are
	 * supposed to be dropped.  For version A silicon, runt packet
	 * dropping doesn't work, so it is disabled in the pf_ctrl register,
	 * but we still leave the packet filter on.
	 */
	writel(pf_ctrl, &rxmac->pf_ctrl);
	writel(0x9, &rxmac->ctrl);
}

void et1310_config_txmac_regs(struct et131x_adapter *adapter)
{
	struct txmac_regs *txmac = &adapter->regs->txmac;

	/* We need to update the Control Frame Parameters
	 * cfpt - control frame pause timer set to 64 (0x40)
	 * cfep - control frame extended pause timer set to 0x0
	 */
	if (adapter->flowcontrol == FLOW_NONE)
		writel(0, &txmac->cf_param);
	else
		writel(0x40, &txmac->cf_param);
}

void et1310_config_macstat_regs(struct et131x_adapter *adapter)
{
	struct macstat_regs __iomem *macstat =
		&adapter->regs->macstat;

	/* Next we need to initialize all the macstat registers to zero on
	 * the device.
	 */
	writel(0, &macstat->txrx_0_64_byte_frames);
	writel(0, &macstat->txrx_65_127_byte_frames);
	writel(0, &macstat->txrx_128_255_byte_frames);
	writel(0, &macstat->txrx_256_511_byte_frames);
	writel(0, &macstat->txrx_512_1023_byte_frames);
	writel(0, &macstat->txrx_1024_1518_byte_frames);
	writel(0, &macstat->txrx_1519_1522_gvln_frames);

	writel(0, &macstat->rx_bytes);
	writel(0, &macstat->rx_packets);
	writel(0, &macstat->rx_fcs_errs);
	writel(0, &macstat->rx_multicast_packets);
	writel(0, &macstat->rx_broadcast_packets);
	writel(0, &macstat->rx_control_frames);
	writel(0, &macstat->rx_pause_frames);
	writel(0, &macstat->rx_unknown_opcodes);
	writel(0, &macstat->rx_align_errs);
	writel(0, &macstat->rx_frame_len_errs);
	writel(0, &macstat->rx_code_errs);
	writel(0, &macstat->rx_carrier_sense_errs);
	writel(0, &macstat->rx_undersize_packets);
	writel(0, &macstat->rx_oversize_packets);
	writel(0, &macstat->rx_fragment_packets);
	writel(0, &macstat->rx_jabbers);
	writel(0, &macstat->rx_drops);

	writel(0, &macstat->tx_bytes);
	writel(0, &macstat->tx_packets);
	writel(0, &macstat->tx_multicast_packets);
	writel(0, &macstat->tx_broadcast_packets);
	writel(0, &macstat->tx_pause_frames);
	writel(0, &macstat->tx_deferred);
	writel(0, &macstat->tx_excessive_deferred);
	writel(0, &macstat->tx_single_collisions);
	writel(0, &macstat->tx_multiple_collisions);
	writel(0, &macstat->tx_late_collisions);
	writel(0, &macstat->tx_excessive_collisions);
	writel(0, &macstat->tx_total_collisions);
	writel(0, &macstat->tx_pause_honored_frames);
	writel(0, &macstat->tx_drops);
	writel(0, &macstat->tx_jabbers);
	writel(0, &macstat->tx_fcs_errs);
	writel(0, &macstat->tx_control_frames);
	writel(0, &macstat->tx_oversize_frames);
	writel(0, &macstat->tx_undersize_frames);
	writel(0, &macstat->tx_fragments);
	writel(0, &macstat->carry_reg1);
	writel(0, &macstat->carry_reg2);

	/* Unmask any counters that we want to track the overflow of.
	 * Initially this will be all counters.  It may become clear later
	 * that we do not need to track all counters.
	 */
	writel(0xFFFFBE32, &macstat->carry_reg1_mask);
	writel(0xFFFE7E8B, &macstat->carry_reg2_mask);
}

void et1310_config_flow_control(struct et131x_adapter *adapter)
{
	if (adapter->duplex_mode == 0) {
		adapter->flowcontrol = FLOW_NONE;
	} else {
		char remote_pause, remote_async_pause;

		et1310_phy_access_mii_bit(adapter,
				TRUEPHY_BIT_READ, 5, 10, &remote_pause);
		et1310_phy_access_mii_bit(adapter,
				TRUEPHY_BIT_READ, 5, 11,
				&remote_async_pause);

		if ((remote_pause == TRUEPHY_BIT_SET) &&
		    (remote_async_pause == TRUEPHY_BIT_SET)) {
			adapter->flowcontrol = adapter->wanted_flow;
		} else if ((remote_pause == TRUEPHY_BIT_SET) &&
			   (remote_async_pause == TRUEPHY_BIT_CLEAR)) {
			if (adapter->wanted_flow == FLOW_BOTH)
				adapter->flowcontrol = FLOW_BOTH;
			else
				adapter->flowcontrol = FLOW_NONE;
		} else if ((remote_pause == TRUEPHY_BIT_CLEAR) &&
			   (remote_async_pause == TRUEPHY_BIT_CLEAR)) {
			adapter->flowcontrol = FLOW_NONE;
		} else {/* if (remote_pause == TRUEPHY_CLEAR_BIT &&
			       remote_async_pause == TRUEPHY_SET_BIT) */
			if (adapter->wanted_flow == FLOW_BOTH)
				adapter->flowcontrol = FLOW_RXONLY;
			else
				adapter->flowcontrol = FLOW_NONE;
		}
	}
}

/**
 * et1310_update_macstat_host_counters - Update the local copy of the statistics
 * @adapter: pointer to the adapter structure
 */
void et1310_update_macstat_host_counters(struct et131x_adapter *adapter)
{
	struct ce_stats *stats = &adapter->stats;
	struct macstat_regs __iomem *macstat =
		&adapter->regs->macstat;

	stats->tx_collisions	       += readl(&macstat->tx_total_collisions);
	stats->tx_first_collisions     += readl(&macstat->tx_single_collisions);
	stats->tx_deferred	       += readl(&macstat->tx_deferred);
	stats->tx_excessive_collisions +=
				readl(&macstat->tx_multiple_collisions);
	stats->tx_late_collisions      += readl(&macstat->tx_late_collisions);
	stats->tx_underflows	       += readl(&macstat->tx_undersize_frames);
	stats->tx_max_pkt_errs	       += readl(&macstat->tx_oversize_frames);

	stats->rx_align_errs        += readl(&macstat->rx_align_errs);
	stats->rx_crc_errs          += readl(&macstat->rx_code_errs);
	stats->rcvd_pkts_dropped    += readl(&macstat->rx_drops);
	stats->rx_overflows         += readl(&macstat->rx_oversize_packets);
	stats->rx_code_violations   += readl(&macstat->rx_fcs_errs);
	stats->rx_length_errs       += readl(&macstat->rx_frame_len_errs);
	stats->rx_other_errs        += readl(&macstat->rx_fragment_packets);
}

/**
 * et1310_handle_macstat_interrupt
 * @adapter: pointer to the adapter structure
 *
 * One of the MACSTAT counters has wrapped.  Update the local copy of
 * the statistics held in the adapter structure, checking the "wrap"
 * bit for each counter.
 */
void et1310_handle_macstat_interrupt(struct et131x_adapter *adapter)
{
	u32 carry_reg1;
	u32 carry_reg2;

	/* Read the interrupt bits from the register(s).  These are Clear On
	 * Write.
	 */
	carry_reg1 = readl(&adapter->regs->macstat.carry_reg1);
	carry_reg2 = readl(&adapter->regs->macstat.carry_reg2);

	writel(carry_reg1, &adapter->regs->macstat.carry_reg1);
	writel(carry_reg2, &adapter->regs->macstat.carry_reg2);

	/* We need to do update the host copy of all the MAC_STAT counters.
	 * For each counter, check it's overflow bit.  If the overflow bit is
	 * set, then increment the host version of the count by one complete
	 * revolution of the counter.  This routine is called when the counter
	 * block indicates that one of the counters has wrapped.
	 */
	if (carry_reg1 & (1 << 14))
		adapter->stats.rx_code_violations	+= COUNTER_WRAP_16_BIT;
	if (carry_reg1 & (1 << 8))
		adapter->stats.rx_align_errs	+= COUNTER_WRAP_12_BIT;
	if (carry_reg1 & (1 << 7))
		adapter->stats.rx_length_errs	+= COUNTER_WRAP_16_BIT;
	if (carry_reg1 & (1 << 2))
		adapter->stats.rx_other_errs	+= COUNTER_WRAP_16_BIT;
	if (carry_reg1 & (1 << 6))
		adapter->stats.rx_crc_errs	+= COUNTER_WRAP_16_BIT;
	if (carry_reg1 & (1 << 3))
		adapter->stats.rx_overflows	+= COUNTER_WRAP_16_BIT;
	if (carry_reg1 & (1 << 0))
		adapter->stats.rcvd_pkts_dropped	+= COUNTER_WRAP_16_BIT;
	if (carry_reg2 & (1 << 16))
		adapter->stats.tx_max_pkt_errs	+= COUNTER_WRAP_12_BIT;
	if (carry_reg2 & (1 << 15))
		adapter->stats.tx_underflows	+= COUNTER_WRAP_12_BIT;
	if (carry_reg2 & (1 << 6))
		adapter->stats.tx_first_collisions += COUNTER_WRAP_12_BIT;
	if (carry_reg2 & (1 << 8))
		adapter->stats.tx_deferred	+= COUNTER_WRAP_12_BIT;
	if (carry_reg2 & (1 << 5))
		adapter->stats.tx_excessive_collisions += COUNTER_WRAP_12_BIT;
	if (carry_reg2 & (1 << 4))
		adapter->stats.tx_late_collisions	+= COUNTER_WRAP_12_BIT;
	if (carry_reg2 & (1 << 2))
		adapter->stats.tx_collisions	+= COUNTER_WRAP_12_BIT;
}

void et1310_setup_device_for_multicast(struct et131x_adapter *adapter)
{
	struct rxmac_regs __iomem *rxmac = &adapter->regs->rxmac;
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
	if (adapter->packet_filter & ET131X_PACKET_TYPE_MULTICAST) {
		/* Loop through our multicast array and set up the device */
		for (nIndex = 0; nIndex < adapter->multicast_addr_count;
		     nIndex++) {
			result = ether_crc(6, adapter->multicast_list[nIndex]);

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
	pm_csr = readl(&adapter->regs->global.pm_csr);
	if ((pm_csr & ET_PM_PHY_SW_COMA) == 0) {
		writel(hash1, &rxmac->multi_hash1);
		writel(hash2, &rxmac->multi_hash2);
		writel(hash3, &rxmac->multi_hash3);
		writel(hash4, &rxmac->multi_hash4);
	}
}

void et1310_setup_device_for_unicast(struct et131x_adapter *adapter)
{
	struct rxmac_regs __iomem *rxmac = &adapter->regs->rxmac;
	u32 uni_pf1;
	u32 uni_pf2;
	u32 uni_pf3;
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
	uni_pf3 = (adapter->addr[0] << ET_UNI_PF_ADDR2_1_SHIFT) |
		  (adapter->addr[1] << ET_UNI_PF_ADDR2_2_SHIFT) |
		  (adapter->addr[0] << ET_UNI_PF_ADDR1_1_SHIFT) |
		   adapter->addr[1];

	uni_pf2 = (adapter->addr[2] << ET_UNI_PF_ADDR2_3_SHIFT) |
		  (adapter->addr[3] << ET_UNI_PF_ADDR2_4_SHIFT) |
		  (adapter->addr[4] << ET_UNI_PF_ADDR2_5_SHIFT) |
		   adapter->addr[5];

	uni_pf1 = (adapter->addr[2] << ET_UNI_PF_ADDR1_3_SHIFT) |
		  (adapter->addr[3] << ET_UNI_PF_ADDR1_4_SHIFT) |
		  (adapter->addr[4] << ET_UNI_PF_ADDR1_5_SHIFT) |
		   adapter->addr[5];

	pm_csr = readl(&adapter->regs->global.pm_csr);
	if ((pm_csr & ET_PM_PHY_SW_COMA) == 0) {
		writel(uni_pf1, &rxmac->uni_pf_addr1);
		writel(uni_pf2, &rxmac->uni_pf_addr2);
		writel(uni_pf3, &rxmac->uni_pf_addr3);
	}
}
