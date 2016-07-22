/**************************************************************************
 *
 * Copyright  2000-2006 Alacritech, Inc.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials provided
 *    with the distribution.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED BY ALACRITECH, INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL ALACRITECH, INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation
 * are those of the authors and should not be interpreted as representing
 * official policies, either expressed or implied, of Alacritech, Inc.
 *
 **************************************************************************/

/*
 * FILENAME: slicoss.c
 *
 * The SLICOSS driver for Alacritech's IS-NIC products.
 *
 * This driver is supposed to support:
 *
 *      Mojave cards (single port PCI Gigabit) both copper and fiber
 *      Oasis cards (single and dual port PCI-x Gigabit) copper and fiber
 *      Kalahari cards (dual and quad port PCI-e Gigabit) copper and fiber
 *
 * The driver was actually tested on Oasis and Kalahari cards.
 *
 *
 * NOTE: This is the standard, non-accelerated version of Alacritech's
 *       IS-NIC driver.
 */

#define KLUDGE_FOR_4GB_BOUNDARY         1
#define DEBUG_MICROCODE                 1
#define DBG                             1
#define SLIC_INTERRUPT_PROCESS_LIMIT	1
#define SLIC_OFFLOAD_IP_CHECKSUM	1
#define STATS_TIMER_INTERVAL		2
#define PING_TIMER_INTERVAL		1
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/timer.h>
#include <linux/pci.h>
#include <linux/spinlock.h>
#include <linux/init.h>
#include <linux/bitops.h>
#include <linux/io.h>
#include <linux/netdevice.h>
#include <linux/crc32.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/delay.h>
#include <linux/seq_file.h>
#include <linux/kthread.h>
#include <linux/module.h>

#include <linux/firmware.h>
#include <linux/types.h>
#include <linux/dma-mapping.h>
#include <linux/mii.h>
#include <linux/if_vlan.h>
#include <asm/unaligned.h>

#include <linux/ethtool.h>
#include <linux/uaccess.h>
#include "slichw.h"
#include "slic.h"

static uint slic_first_init = 1;
static char *slic_banner = "Alacritech SLIC Technology(tm) Server and Storage Accelerator (Non-Accelerated)";

static char *slic_proc_version = "2.0.351  2006/07/14 12:26:00";

static struct base_driver slic_global = { {}, 0, 0, 0, 1, NULL, NULL };
#define DEFAULT_INTAGG_DELAY 100
static unsigned int rcv_count;

#define DRV_NAME          "slicoss"
#define DRV_VERSION       "2.0.1"
#define DRV_AUTHOR        "Alacritech, Inc. Engineering"
#define DRV_DESCRIPTION   "Alacritech SLIC Techonology(tm) "\
		"Non-Accelerated Driver"
#define DRV_COPYRIGHT     "Copyright  2000-2006 Alacritech, Inc. "\
		"All rights reserved."
#define PFX		   DRV_NAME " "

MODULE_AUTHOR(DRV_AUTHOR);
MODULE_DESCRIPTION(DRV_DESCRIPTION);
MODULE_LICENSE("Dual BSD/GPL");

static const struct pci_device_id slic_pci_tbl[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_ALACRITECH, SLIC_1GB_DEVICE_ID) },
	{ PCI_DEVICE(PCI_VENDOR_ID_ALACRITECH, SLIC_2GB_DEVICE_ID) },
	{ 0 }
};

static struct ethtool_ops slic_ethtool_ops;

MODULE_DEVICE_TABLE(pci, slic_pci_tbl);

static void slic_mcast_set_bit(struct adapter *adapter, char *address)
{
	unsigned char crcpoly;

	/* Get the CRC polynomial for the mac address */
	/*
	 * we use bits 1-8 (lsb), bitwise reversed,
	 * msb (= lsb bit 0 before bitrev) is automatically discarded
	 */
	crcpoly = ether_crc(ETH_ALEN, address) >> 23;

	/*
	 * We only have space on the SLIC for 64 entries.  Lop
	 * off the top two bits. (2^6 = 64)
	 */
	crcpoly &= 0x3F;

	/* OR in the new bit into our 64 bit mask. */
	adapter->mcastmask |= (u64)1 << crcpoly;
}

static void slic_mcast_set_mask(struct adapter *adapter)
{
	if (adapter->macopts & (MAC_ALLMCAST | MAC_PROMISC)) {
		/*
		 * Turn on all multicast addresses. We have to do this for
		 * promiscuous mode as well as ALLMCAST mode.  It saves the
		 * Microcode from having to keep state about the MAC
		 * configuration.
		 */
		slic_write32(adapter, SLIC_REG_MCASTLOW, 0xFFFFFFFF);
		slic_write32(adapter, SLIC_REG_MCASTHIGH, 0xFFFFFFFF);
	} else {
		/*
		 * Commit our multicast mast to the SLIC by writing to the
		 * multicast address mask registers
		 */
		slic_write32(adapter, SLIC_REG_MCASTLOW,
			     (u32)(adapter->mcastmask & 0xFFFFFFFF));
		slic_write32(adapter, SLIC_REG_MCASTHIGH,
			     (u32)((adapter->mcastmask >> 32) & 0xFFFFFFFF));
	}
}

static void slic_timer_ping(ulong dev)
{
	struct adapter *adapter;
	struct sliccard *card;

	adapter = netdev_priv((struct net_device *)dev);
	card = adapter->card;

	adapter->pingtimer.expires = jiffies + (PING_TIMER_INTERVAL * HZ);
	add_timer(&adapter->pingtimer);
}

/*
 *  slic_link_config
 *
 *  Write phy control to configure link duplex/speed
 *
 */
static void slic_link_config(struct adapter *adapter,
		      u32 linkspeed, u32 linkduplex)
{
	u32 speed;
	u32 duplex;
	u32 phy_config;
	u32 phy_advreg;
	u32 phy_gctlreg;

	if (adapter->state != ADAPT_UP)
		return;

	if (linkspeed > LINK_1000MB)
		linkspeed = LINK_AUTOSPEED;
	if (linkduplex > LINK_AUTOD)
		linkduplex = LINK_AUTOD;

	if ((linkspeed == LINK_AUTOSPEED) || (linkspeed == LINK_1000MB)) {
		if (adapter->flags & ADAPT_FLAGS_FIBERMEDIA) {
			/*
			 * We've got a fiber gigabit interface, and register
			 *  4 is different in fiber mode than in copper mode
			 */

			/* advertise FD only @1000 Mb */
			phy_advreg = (MIICR_REG_4 | (PAR_ADV1000XFD));
			/* enable PAUSE frames        */
			phy_advreg |= PAR_ASYMPAUSE_FIBER;
			slic_write32(adapter, SLIC_REG_WPHY, phy_advreg);

			if (linkspeed == LINK_AUTOSPEED) {
				/* reset phy, enable auto-neg  */
				phy_config =
				    (MIICR_REG_PCR |
				     (PCR_RESET | PCR_AUTONEG |
				      PCR_AUTONEG_RST));
				slic_write32(adapter, SLIC_REG_WPHY,
					     phy_config);
			} else {	/* forced 1000 Mb FD*/
				/*
				 * power down phy to break link
				 * this may not work)
				 */
				phy_config = (MIICR_REG_PCR | PCR_POWERDOWN);
				slic_write32(adapter, SLIC_REG_WPHY,
					     phy_config);
				slic_flush_write(adapter);
				/*
				 * wait, Marvell says 1 sec,
				 * try to get away with 10 ms
				 */
				mdelay(10);

				/*
				 * disable auto-neg, set speed/duplex,
				 * soft reset phy, powerup
				 */
				phy_config =
				    (MIICR_REG_PCR |
				     (PCR_RESET | PCR_SPEED_1000 |
				      PCR_DUPLEX_FULL));
				slic_write32(adapter, SLIC_REG_WPHY,
					     phy_config);
			}
		} else {	/* copper gigabit */

			/*
			 * Auto-Negotiate or 1000 Mb must be auto negotiated
			 * We've got a copper gigabit interface, and
			 * register 4 is different in copper mode than
			 * in fiber mode
			 */
			if (linkspeed == LINK_AUTOSPEED) {
				/* advertise 10/100 Mb modes   */
				phy_advreg =
				    (MIICR_REG_4 |
				     (PAR_ADV100FD | PAR_ADV100HD | PAR_ADV10FD
				      | PAR_ADV10HD));
			} else {
			/*
			 * linkspeed == LINK_1000MB -
			 * don't advertise 10/100 Mb modes
			 */
				phy_advreg = MIICR_REG_4;
			}
			/* enable PAUSE frames  */
			phy_advreg |= PAR_ASYMPAUSE;
			/* required by the Cicada PHY  */
			phy_advreg |= PAR_802_3;
			slic_write32(adapter, SLIC_REG_WPHY, phy_advreg);
			/* advertise FD only @1000 Mb  */
			phy_gctlreg = (MIICR_REG_9 | (PGC_ADV1000FD));
			slic_write32(adapter, SLIC_REG_WPHY, phy_gctlreg);

			if (adapter->subsysid != SLIC_1GB_CICADA_SUBSYS_ID) {
				/*
				 * if a Marvell PHY
				 * enable auto crossover
				 */
				phy_config =
				    (MIICR_REG_16 | (MRV_REG16_XOVERON));
				slic_write32(adapter, SLIC_REG_WPHY,
					     phy_config);

				/* reset phy, enable auto-neg  */
				phy_config =
				    (MIICR_REG_PCR |
				     (PCR_RESET | PCR_AUTONEG |
				      PCR_AUTONEG_RST));
				slic_write32(adapter, SLIC_REG_WPHY,
					     phy_config);
			} else {	/* it's a Cicada PHY  */
				/* enable and restart auto-neg (don't reset)  */
				phy_config =
				    (MIICR_REG_PCR |
				     (PCR_AUTONEG | PCR_AUTONEG_RST));
				slic_write32(adapter, SLIC_REG_WPHY,
					     phy_config);
			}
		}
	} else {
		/* Forced 10/100  */
		if (linkspeed == LINK_10MB)
			speed = 0;
		else
			speed = PCR_SPEED_100;
		if (linkduplex == LINK_HALFD)
			duplex = 0;
		else
			duplex = PCR_DUPLEX_FULL;

		if (adapter->subsysid != SLIC_1GB_CICADA_SUBSYS_ID) {
			/*
			 * if a Marvell PHY
			 * disable auto crossover
			 */
			phy_config = (MIICR_REG_16 | (MRV_REG16_XOVEROFF));
			slic_write32(adapter, SLIC_REG_WPHY, phy_config);
		}

		/* power down phy to break link (this may not work)  */
		phy_config = (MIICR_REG_PCR | (PCR_POWERDOWN | speed | duplex));
		slic_write32(adapter, SLIC_REG_WPHY, phy_config);
		slic_flush_write(adapter);
		/* wait, Marvell says 1 sec, try to get away with 10 ms */
		mdelay(10);

		if (adapter->subsysid != SLIC_1GB_CICADA_SUBSYS_ID) {
			/*
			 * if a Marvell PHY
			 * disable auto-neg, set speed,
			 * soft reset phy, powerup
			 */
			phy_config =
			    (MIICR_REG_PCR | (PCR_RESET | speed | duplex));
			slic_write32(adapter, SLIC_REG_WPHY, phy_config);
		} else {	/* it's a Cicada PHY  */
			/* disable auto-neg, set speed, powerup  */
			phy_config = (MIICR_REG_PCR | (speed | duplex));
			slic_write32(adapter, SLIC_REG_WPHY, phy_config);
		}
	}
}

static int slic_card_download_gbrcv(struct adapter *adapter)
{
	const struct firmware *fw;
	const char *file = "";
	int ret;
	u32 codeaddr;
	u32 instruction;
	int index = 0;
	u32 rcvucodelen = 0;

	switch (adapter->devid) {
	case SLIC_2GB_DEVICE_ID:
		file = "slicoss/oasisrcvucode.sys";
		break;
	case SLIC_1GB_DEVICE_ID:
		file = "slicoss/gbrcvucode.sys";
		break;
	default:
		return -ENOENT;
	}

	ret = request_firmware(&fw, file, &adapter->pcidev->dev);
	if (ret) {
		dev_err(&adapter->pcidev->dev,
			"Failed to load firmware %s\n", file);
		return ret;
	}

	rcvucodelen = *(u32 *)(fw->data + index);
	index += 4;
	switch (adapter->devid) {
	case SLIC_2GB_DEVICE_ID:
		if (rcvucodelen != OasisRcvUCodeLen) {
			release_firmware(fw);
			return -EINVAL;
		}
		break;
	case SLIC_1GB_DEVICE_ID:
		if (rcvucodelen != GBRcvUCodeLen) {
			release_firmware(fw);
			return -EINVAL;
		}
		break;
	}
	/* start download */
	slic_write32(adapter, SLIC_REG_RCV_WCS, SLIC_RCVWCS_BEGIN);
	/* download the rcv sequencer ucode */
	for (codeaddr = 0; codeaddr < rcvucodelen; codeaddr++) {
		/* write out instruction address */
		slic_write32(adapter, SLIC_REG_RCV_WCS, codeaddr);

		instruction = *(u32 *)(fw->data + index);
		index += 4;
		/* write out the instruction data low addr */
		slic_write32(adapter, SLIC_REG_RCV_WCS, instruction);

		instruction = *(u8 *)(fw->data + index);
		index++;
		/* write out the instruction data high addr */
		slic_write32(adapter, SLIC_REG_RCV_WCS, instruction);
	}

	/* download finished */
	release_firmware(fw);
	slic_write32(adapter, SLIC_REG_RCV_WCS, SLIC_RCVWCS_FINISH);
	slic_flush_write(adapter);

	return 0;
}

MODULE_FIRMWARE("slicoss/oasisrcvucode.sys");
MODULE_FIRMWARE("slicoss/gbrcvucode.sys");

static int slic_card_download(struct adapter *adapter)
{
	const struct firmware *fw;
	const char *file = "";
	int ret;
	u32 section;
	int thissectionsize;
	int codeaddr;
	u32 instruction;
	u32 baseaddress;
	u32 i;
	u32 numsects = 0;
	u32 sectsize[3];
	u32 sectstart[3];
	int ucode_start, index = 0;

	switch (adapter->devid) {
	case SLIC_2GB_DEVICE_ID:
		file = "slicoss/oasisdownload.sys";
		break;
	case SLIC_1GB_DEVICE_ID:
		file = "slicoss/gbdownload.sys";
		break;
	default:
		return -ENOENT;
	}
	ret = request_firmware(&fw, file, &adapter->pcidev->dev);
	if (ret) {
		dev_err(&adapter->pcidev->dev,
			"Failed to load firmware %s\n", file);
		return ret;
	}
	numsects = *(u32 *)(fw->data + index);
	index += 4;
	for (i = 0; i < numsects; i++) {
		sectsize[i] = *(u32 *)(fw->data + index);
		index += 4;
	}
	for (i = 0; i < numsects; i++) {
		sectstart[i] = *(u32 *)(fw->data + index);
		index += 4;
	}
	ucode_start = index;
	instruction = *(u32 *)(fw->data + index);
	index += 4;
	for (section = 0; section < numsects; section++) {
		baseaddress = sectstart[section];
		thissectionsize = sectsize[section] >> 3;

		for (codeaddr = 0; codeaddr < thissectionsize; codeaddr++) {
			/* Write out instruction address */
			slic_write32(adapter, SLIC_REG_WCS,
				     baseaddress + codeaddr);
			/* Write out instruction to low addr */
			slic_write32(adapter, SLIC_REG_WCS,
				     instruction);
			instruction = *(u32 *)(fw->data + index);
			index += 4;

			/* Write out instruction to high addr */
			slic_write32(adapter, SLIC_REG_WCS,
				     instruction);
			instruction = *(u32 *)(fw->data + index);
			index += 4;
		}
	}
	index = ucode_start;
	for (section = 0; section < numsects; section++) {
		instruction = *(u32 *)(fw->data + index);
		baseaddress = sectstart[section];
		if (baseaddress < 0x8000)
			continue;
		thissectionsize = sectsize[section] >> 3;

		for (codeaddr = 0; codeaddr < thissectionsize; codeaddr++) {
			/* Write out instruction address */
			slic_write32(adapter, SLIC_REG_WCS,
				     SLIC_WCS_COMPARE | (baseaddress +
							 codeaddr));
			/* Write out instruction to low addr */
			slic_write32(adapter, SLIC_REG_WCS, instruction);
			instruction = *(u32 *)(fw->data + index);
			index += 4;
			/* Write out instruction to high addr */
			slic_write32(adapter, SLIC_REG_WCS, instruction);
			instruction = *(u32 *)(fw->data + index);
			index += 4;

		}
	}
	release_firmware(fw);
	/* Everything OK, kick off the card */
	mdelay(10);

	slic_write32(adapter, SLIC_REG_WCS, SLIC_WCS_START);
	slic_flush_write(adapter);
	/*
	 * stall for 20 ms, long enough for ucode to init card
	 * and reach mainloop
	 */
	mdelay(20);

	return 0;
}

MODULE_FIRMWARE("slicoss/oasisdownload.sys");
MODULE_FIRMWARE("slicoss/gbdownload.sys");

static void slic_adapter_set_hwaddr(struct adapter *adapter)
{
	struct sliccard *card = adapter->card;

	if ((adapter->card) && (card->config_set)) {
		memcpy(adapter->macaddr,
		       card->config.MacInfo[adapter->functionnumber].macaddrA,
		       sizeof(struct slic_config_mac));
		if (is_zero_ether_addr(adapter->currmacaddr))
			memcpy(adapter->currmacaddr, adapter->macaddr,
			       ETH_ALEN);
		if (adapter->netdev)
			memcpy(adapter->netdev->dev_addr, adapter->currmacaddr,
			       ETH_ALEN);
	}
}

static void slic_intagg_set(struct adapter *adapter, u32 value)
{
	slic_write32(adapter, SLIC_REG_INTAGG, value);
	adapter->card->loadlevel_current = value;
}

static void slic_soft_reset(struct adapter *adapter)
{
	if (adapter->card->state == CARD_UP) {
		slic_write32(adapter, SLIC_REG_QUIESCE, 0);
		slic_flush_write(adapter);
		mdelay(1);
	}

	slic_write32(adapter, SLIC_REG_RESET, SLIC_RESET_MAGIC);
	slic_flush_write(adapter);

	mdelay(1);
}

static void slic_mac_address_config(struct adapter *adapter)
{
	u32 value;
	u32 value2;

	value = ntohl(*(__be32 *)&adapter->currmacaddr[2]);
	slic_write32(adapter, SLIC_REG_WRADDRAL, value);
	slic_write32(adapter, SLIC_REG_WRADDRBL, value);

	value2 = (u32)((adapter->currmacaddr[0] << 8 |
			     adapter->currmacaddr[1]) & 0xFFFF);

	slic_write32(adapter, SLIC_REG_WRADDRAH, value2);
	slic_write32(adapter, SLIC_REG_WRADDRBH, value2);

	/*
	 * Write our multicast mask out to the card.  This is done
	 * here in addition to the slic_mcast_addr_set routine
	 * because ALL_MCAST may have been enabled or disabled
	 */
	slic_mcast_set_mask(adapter);
}

static void slic_mac_config(struct adapter *adapter)
{
	u32 value;

	/* Setup GMAC gaps */
	if (adapter->linkspeed == LINK_1000MB) {
		value = ((GMCR_GAPBB_1000 << GMCR_GAPBB_SHIFT) |
			 (GMCR_GAPR1_1000 << GMCR_GAPR1_SHIFT) |
			 (GMCR_GAPR2_1000 << GMCR_GAPR2_SHIFT));
	} else {
		value = ((GMCR_GAPBB_100 << GMCR_GAPBB_SHIFT) |
			 (GMCR_GAPR1_100 << GMCR_GAPR1_SHIFT) |
			 (GMCR_GAPR2_100 << GMCR_GAPR2_SHIFT));
	}

	/* enable GMII */
	if (adapter->linkspeed == LINK_1000MB)
		value |= GMCR_GBIT;

	/* enable fullduplex */
	if ((adapter->linkduplex == LINK_FULLD)
	    || (adapter->macopts & MAC_LOOPBACK)) {
		value |= GMCR_FULLD;
	}

	/* write mac config */
	slic_write32(adapter, SLIC_REG_WMCFG, value);

	/* setup mac addresses */
	slic_mac_address_config(adapter);
}

static void slic_config_set(struct adapter *adapter, bool linkchange)
{
	u32 value;
	u32 RcrReset;

	if (linkchange) {
		/* Setup MAC */
		slic_mac_config(adapter);
		RcrReset = GRCR_RESET;
	} else {
		slic_mac_address_config(adapter);
		RcrReset = 0;
	}

	if (adapter->linkduplex == LINK_FULLD) {
		/* setup xmtcfg */
		value = (GXCR_RESET |	/* Always reset     */
			 GXCR_XMTEN |	/* Enable transmit  */
			 GXCR_PAUSEEN);	/* Enable pause     */

		slic_write32(adapter, SLIC_REG_WXCFG, value);

		/* Setup rcvcfg last */
		value = (RcrReset |	/* Reset, if linkchange */
			 GRCR_CTLEN |	/* Enable CTL frames    */
			 GRCR_ADDRAEN |	/* Address A enable     */
			 GRCR_RCVBAD |	/* Rcv bad frames       */
			 (GRCR_HASHSIZE << GRCR_HASHSIZE_SHIFT));
	} else {
		/* setup xmtcfg */
		value = (GXCR_RESET |	/* Always reset     */
			 GXCR_XMTEN);	/* Enable transmit  */

		slic_write32(adapter, SLIC_REG_WXCFG, value);

		/* Setup rcvcfg last */
		value = (RcrReset |	/* Reset, if linkchange */
			 GRCR_ADDRAEN |	/* Address A enable     */
			 GRCR_RCVBAD |	/* Rcv bad frames       */
			 (GRCR_HASHSIZE << GRCR_HASHSIZE_SHIFT));
	}

	if (adapter->state != ADAPT_DOWN) {
		/* Only enable receive if we are restarting or running */
		value |= GRCR_RCVEN;
	}

	if (adapter->macopts & MAC_PROMISC)
		value |= GRCR_RCVALL;

	slic_write32(adapter, SLIC_REG_WRCFG, value);
}

/*
 *  Turn off RCV and XMT, power down PHY
 */
static void slic_config_clear(struct adapter *adapter)
{
	u32 value;
	u32 phy_config;

	/* Setup xmtcfg */
	value = (GXCR_RESET |	/* Always reset */
		 GXCR_PAUSEEN);	/* Enable pause */

	slic_write32(adapter, SLIC_REG_WXCFG, value);

	value = (GRCR_RESET |	/* Always reset      */
		 GRCR_CTLEN |	/* Enable CTL frames */
		 GRCR_ADDRAEN |	/* Address A enable  */
		 (GRCR_HASHSIZE << GRCR_HASHSIZE_SHIFT));

	slic_write32(adapter, SLIC_REG_WRCFG, value);

	/* power down phy */
	phy_config = (MIICR_REG_PCR | (PCR_POWERDOWN));
	slic_write32(adapter, SLIC_REG_WPHY, phy_config);
}

static bool slic_mac_filter(struct adapter *adapter,
			struct ether_header *ether_frame)
{
	struct net_device *netdev = adapter->netdev;
	u32 opts = adapter->macopts;

	if (opts & MAC_PROMISC)
		return true;

	if (is_broadcast_ether_addr(ether_frame->ether_dhost)) {
		if (opts & MAC_BCAST) {
			adapter->rcv_broadcasts++;
			return true;
		}

		return false;
	}

	if (is_multicast_ether_addr(ether_frame->ether_dhost)) {
		if (opts & MAC_ALLMCAST) {
			adapter->rcv_multicasts++;
			netdev->stats.multicast++;
			return true;
		}
		if (opts & MAC_MCAST) {
			struct mcast_address *mcaddr = adapter->mcastaddrs;

			while (mcaddr) {
				if (ether_addr_equal(mcaddr->address,
						     ether_frame->ether_dhost)) {
					adapter->rcv_multicasts++;
					netdev->stats.multicast++;
					return true;
				}
				mcaddr = mcaddr->next;
			}

			return false;
		}

		return false;
	}
	if (opts & MAC_DIRECTED) {
		adapter->rcv_unicasts++;
		return true;
	}
	return false;
}

static int slic_mac_set_address(struct net_device *dev, void *ptr)
{
	struct adapter *adapter = netdev_priv(dev);
	struct sockaddr *addr = ptr;

	if (netif_running(dev))
		return -EBUSY;
	if (!adapter)
		return -EBUSY;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EINVAL;

	memcpy(dev->dev_addr, addr->sa_data, dev->addr_len);
	memcpy(adapter->currmacaddr, addr->sa_data, dev->addr_len);

	slic_config_set(adapter, true);
	return 0;
}

static void slic_timer_load_check(ulong cardaddr)
{
	struct sliccard *card = (struct sliccard *)cardaddr;
	struct adapter *adapter = card->master;
	u32 load = card->events;
	u32 level = 0;

	if ((adapter) && (adapter->state == ADAPT_UP) &&
	    (card->state == CARD_UP) && (slic_global.dynamic_intagg)) {
		if (adapter->devid == SLIC_1GB_DEVICE_ID) {
			if (adapter->linkspeed == LINK_1000MB)
				level = 100;
			else {
				if (load > SLIC_LOAD_5)
					level = SLIC_INTAGG_5;
				else if (load > SLIC_LOAD_4)
					level = SLIC_INTAGG_4;
				else if (load > SLIC_LOAD_3)
					level = SLIC_INTAGG_3;
				else if (load > SLIC_LOAD_2)
					level = SLIC_INTAGG_2;
				else if (load > SLIC_LOAD_1)
					level = SLIC_INTAGG_1;
				else
					level = SLIC_INTAGG_0;
			}
			if (card->loadlevel_current != level) {
				card->loadlevel_current = level;
				slic_write32(adapter, SLIC_REG_INTAGG, level);
			}
		} else {
			if (load > SLIC_LOAD_5)
				level = SLIC_INTAGG_5;
			else if (load > SLIC_LOAD_4)
				level = SLIC_INTAGG_4;
			else if (load > SLIC_LOAD_3)
				level = SLIC_INTAGG_3;
			else if (load > SLIC_LOAD_2)
				level = SLIC_INTAGG_2;
			else if (load > SLIC_LOAD_1)
				level = SLIC_INTAGG_1;
			else
				level = SLIC_INTAGG_0;
			if (card->loadlevel_current != level) {
				card->loadlevel_current = level;
				slic_write32(adapter, SLIC_REG_INTAGG, level);
			}
		}
	}
	card->events = 0;
	card->loadtimer.expires = jiffies + (SLIC_LOADTIMER_PERIOD * HZ);
	add_timer(&card->loadtimer);
}

static int slic_upr_queue_request(struct adapter *adapter,
			   u32 upr_request,
			   u32 upr_data,
			   u32 upr_data_h,
			   u32 upr_buffer, u32 upr_buffer_h)
{
	struct slic_upr *upr;
	struct slic_upr *uprqueue;

	upr = kmalloc(sizeof(*upr), GFP_ATOMIC);
	if (!upr)
		return -ENOMEM;

	upr->adapter = adapter->port;
	upr->upr_request = upr_request;
	upr->upr_data = upr_data;
	upr->upr_buffer = upr_buffer;
	upr->upr_data_h = upr_data_h;
	upr->upr_buffer_h = upr_buffer_h;
	upr->next = NULL;
	if (adapter->upr_list) {
		uprqueue = adapter->upr_list;

		while (uprqueue->next)
			uprqueue = uprqueue->next;
		uprqueue->next = upr;
	} else {
		adapter->upr_list = upr;
	}
	return 0;
}

static void slic_upr_start(struct adapter *adapter)
{
	struct slic_upr *upr;

	upr = adapter->upr_list;
	if (!upr)
		return;
	if (adapter->upr_busy)
		return;
	adapter->upr_busy = 1;

	switch (upr->upr_request) {
	case SLIC_UPR_STATS:
		if (upr->upr_data_h == 0) {
			slic_write32(adapter, SLIC_REG_RSTAT, upr->upr_data);
		} else {
			slic_write64(adapter, SLIC_REG_RSTAT64, upr->upr_data,
				     upr->upr_data_h);
		}
		break;

	case SLIC_UPR_RLSR:
		slic_write64(adapter, SLIC_REG_LSTAT, upr->upr_data,
			     upr->upr_data_h);
		break;

	case SLIC_UPR_RCONFIG:
		slic_write64(adapter, SLIC_REG_RCONFIG, upr->upr_data,
			     upr->upr_data_h);
		break;
	case SLIC_UPR_PING:
		slic_write32(adapter, SLIC_REG_PING, 1);
		break;
	}
	slic_flush_write(adapter);
}

static int slic_upr_request(struct adapter *adapter,
		     u32 upr_request,
		     u32 upr_data,
		     u32 upr_data_h,
		     u32 upr_buffer, u32 upr_buffer_h)
{
	unsigned long flags;
	int rc;

	spin_lock_irqsave(&adapter->upr_lock, flags);
	rc = slic_upr_queue_request(adapter,
					upr_request,
					upr_data,
					upr_data_h, upr_buffer, upr_buffer_h);
	if (rc)
		goto err_unlock_irq;

	slic_upr_start(adapter);
err_unlock_irq:
	spin_unlock_irqrestore(&adapter->upr_lock, flags);
	return rc;
}

static void slic_link_upr_complete(struct adapter *adapter, u32 isr)
{
	struct slic_shmemory *sm = &adapter->shmem;
	struct slic_shmem_data *sm_data = sm->shmem_data;
	u32 lst = sm_data->lnkstatus;
	uint linkup;
	unsigned char linkspeed;
	unsigned char linkduplex;

	if ((isr & ISR_UPCERR) || (isr & ISR_UPCBSY)) {
		dma_addr_t phaddr = sm->lnkstatus_phaddr;

		slic_upr_queue_request(adapter, SLIC_UPR_RLSR,
				       cpu_to_le32(lower_32_bits(phaddr)),
				       cpu_to_le32(upper_32_bits(phaddr)),
				       0, 0);
		return;
	}
	if (adapter->state != ADAPT_UP)
		return;

	linkup = lst & GIG_LINKUP ? LINK_UP : LINK_DOWN;
	if (lst & GIG_SPEED_1000)
		linkspeed = LINK_1000MB;
	else if (lst & GIG_SPEED_100)
		linkspeed = LINK_100MB;
	else
		linkspeed = LINK_10MB;

	if (lst & GIG_FULLDUPLEX)
		linkduplex = LINK_FULLD;
	else
		linkduplex = LINK_HALFD;

	if ((adapter->linkstate == LINK_DOWN) && (linkup == LINK_DOWN))
		return;

	/* link up event, but nothing has changed */
	if ((adapter->linkstate == LINK_UP) &&
	    (linkup == LINK_UP) &&
	    (adapter->linkspeed == linkspeed) &&
	    (adapter->linkduplex == linkduplex))
		return;

	/* link has changed at this point */

	/* link has gone from up to down */
	if (linkup == LINK_DOWN) {
		adapter->linkstate = LINK_DOWN;
		netif_carrier_off(adapter->netdev);
		return;
	}

	/* link has gone from down to up */
	adapter->linkspeed = linkspeed;
	adapter->linkduplex = linkduplex;

	if (adapter->linkstate != LINK_UP) {
		/* setup the mac */
		slic_config_set(adapter, true);
		adapter->linkstate = LINK_UP;
		netif_carrier_on(adapter->netdev);
	}
}

static void slic_upr_request_complete(struct adapter *adapter, u32 isr)
{
	struct sliccard *card = adapter->card;
	struct slic_upr *upr;
	unsigned long flags;

	spin_lock_irqsave(&adapter->upr_lock, flags);
	upr = adapter->upr_list;
	if (!upr) {
		spin_unlock_irqrestore(&adapter->upr_lock, flags);
		return;
	}
	adapter->upr_list = upr->next;
	upr->next = NULL;
	adapter->upr_busy = 0;
	switch (upr->upr_request) {
	case SLIC_UPR_STATS: {
		struct slic_shmemory *sm = &adapter->shmem;
		struct slic_shmem_data *sm_data = sm->shmem_data;
		struct slic_stats *stats = &sm_data->stats;
		struct slic_stats *old = &adapter->inicstats_prev;
		struct slicnet_stats *stst = &adapter->slic_stats;

		if (isr & ISR_UPCERR) {
			dev_err(&adapter->netdev->dev,
				"SLIC_UPR_STATS command failed isr[%x]\n", isr);
			break;
		}

		UPDATE_STATS_GB(stst->tcp.xmit_tcp_segs, stats->xmit_tcp_segs,
				old->xmit_tcp_segs);

		UPDATE_STATS_GB(stst->tcp.xmit_tcp_bytes, stats->xmit_tcp_bytes,
				old->xmit_tcp_bytes);

		UPDATE_STATS_GB(stst->tcp.rcv_tcp_segs, stats->rcv_tcp_segs,
				old->rcv_tcp_segs);

		UPDATE_STATS_GB(stst->tcp.rcv_tcp_bytes, stats->rcv_tcp_bytes,
				old->rcv_tcp_bytes);

		UPDATE_STATS_GB(stst->iface.xmt_bytes, stats->xmit_bytes,
				old->xmit_bytes);

		UPDATE_STATS_GB(stst->iface.xmt_ucast, stats->xmit_unicasts,
				old->xmit_unicasts);

		UPDATE_STATS_GB(stst->iface.rcv_bytes, stats->rcv_bytes,
				old->rcv_bytes);

		UPDATE_STATS_GB(stst->iface.rcv_ucast, stats->rcv_unicasts,
				old->rcv_unicasts);

		UPDATE_STATS_GB(stst->iface.xmt_errors, stats->xmit_collisions,
				old->xmit_collisions);

		UPDATE_STATS_GB(stst->iface.xmt_errors,
				stats->xmit_excess_collisions,
				old->xmit_excess_collisions);

		UPDATE_STATS_GB(stst->iface.xmt_errors, stats->xmit_other_error,
				old->xmit_other_error);

		UPDATE_STATS_GB(stst->iface.rcv_errors, stats->rcv_other_error,
				old->rcv_other_error);

		UPDATE_STATS_GB(stst->iface.rcv_discards, stats->rcv_drops,
				old->rcv_drops);

		if (stats->rcv_drops > old->rcv_drops)
			adapter->rcv_drops += (stats->rcv_drops -
					       old->rcv_drops);
		memcpy_fromio(old, stats, sizeof(*stats));
		break;
	}
	case SLIC_UPR_RLSR:
		slic_link_upr_complete(adapter, isr);
		break;
	case SLIC_UPR_RCONFIG:
		break;
	case SLIC_UPR_PING:
		card->pingstatus |= (isr & ISR_PINGDSMASK);
		break;
	}
	kfree(upr);
	slic_upr_start(adapter);
	spin_unlock_irqrestore(&adapter->upr_lock, flags);
}

static int slic_config_get(struct adapter *adapter, u32 config, u32 config_h)
{
	return slic_upr_request(adapter, SLIC_UPR_RCONFIG, config, config_h,
				0, 0);
}

/*
 * Compute a checksum of the EEPROM according to RFC 1071.
 */
static u16 slic_eeprom_cksum(void *eeprom, unsigned int len)
{
	u16 *wp = eeprom;
	u32 checksum = 0;

	while (len > 1) {
		checksum += *(wp++);
		len -= 2;
	}

	if (len > 0)
		checksum += *(u8 *)wp;

	while (checksum >> 16)
		checksum = (checksum & 0xFFFF) + ((checksum >> 16) & 0xFFFF);

	return ~checksum;
}

static void slic_rspqueue_free(struct adapter *adapter)
{
	int i;
	struct slic_rspqueue *rspq = &adapter->rspqueue;

	for (i = 0; i < rspq->num_pages; i++) {
		if (rspq->vaddr[i]) {
			pci_free_consistent(adapter->pcidev, PAGE_SIZE,
					    rspq->vaddr[i], rspq->paddr[i]);
		}
		rspq->vaddr[i] = NULL;
		rspq->paddr[i] = 0;
	}
	rspq->offset = 0;
	rspq->pageindex = 0;
	rspq->rspbuf = NULL;
}

static int slic_rspqueue_init(struct adapter *adapter)
{
	int i;
	struct slic_rspqueue *rspq = &adapter->rspqueue;
	u32 paddrh = 0;

	memset(rspq, 0, sizeof(struct slic_rspqueue));

	rspq->num_pages = SLIC_RSPQ_PAGES_GB;

	for (i = 0; i < rspq->num_pages; i++) {
		rspq->vaddr[i] = pci_zalloc_consistent(adapter->pcidev,
						       PAGE_SIZE,
						       &rspq->paddr[i]);
		if (!rspq->vaddr[i]) {
			dev_err(&adapter->pcidev->dev,
				"pci_alloc_consistent failed\n");
			slic_rspqueue_free(adapter);
			return -ENOMEM;
		}

		if (paddrh == 0) {
			slic_write32(adapter, SLIC_REG_RBAR,
				     rspq->paddr[i] | SLIC_RSPQ_BUFSINPAGE);
		} else {
			slic_write64(adapter, SLIC_REG_RBAR64,
				     rspq->paddr[i] | SLIC_RSPQ_BUFSINPAGE,
				     paddrh);
		}
	}
	rspq->offset = 0;
	rspq->pageindex = 0;
	rspq->rspbuf = (struct slic_rspbuf *)rspq->vaddr[0];
	return 0;
}

static struct slic_rspbuf *slic_rspqueue_getnext(struct adapter *adapter)
{
	struct slic_rspqueue *rspq = &adapter->rspqueue;
	struct slic_rspbuf *buf;

	if (!(rspq->rspbuf->status))
		return NULL;

	buf = rspq->rspbuf;
	if (++rspq->offset < SLIC_RSPQ_BUFSINPAGE) {
		rspq->rspbuf++;
	} else {
		slic_write64(adapter, SLIC_REG_RBAR64,
			     rspq->paddr[rspq->pageindex] |
			     SLIC_RSPQ_BUFSINPAGE, 0);
		rspq->pageindex = (rspq->pageindex + 1) % rspq->num_pages;
		rspq->offset = 0;
		rspq->rspbuf = (struct slic_rspbuf *)
						rspq->vaddr[rspq->pageindex];
	}

	return buf;
}

static void slic_cmdqmem_free(struct adapter *adapter)
{
	struct slic_cmdqmem *cmdqmem = &adapter->cmdqmem;
	int i;

	for (i = 0; i < SLIC_CMDQ_MAXPAGES; i++) {
		if (cmdqmem->pages[i]) {
			pci_free_consistent(adapter->pcidev,
					    PAGE_SIZE,
					    (void *)cmdqmem->pages[i],
					    cmdqmem->dma_pages[i]);
		}
	}
	memset(cmdqmem, 0, sizeof(struct slic_cmdqmem));
}

static u32 *slic_cmdqmem_addpage(struct adapter *adapter)
{
	struct slic_cmdqmem *cmdqmem = &adapter->cmdqmem;
	u32 *pageaddr;

	if (cmdqmem->pagecnt >= SLIC_CMDQ_MAXPAGES)
		return NULL;
	pageaddr = pci_alloc_consistent(adapter->pcidev,
					PAGE_SIZE,
					&cmdqmem->dma_pages[cmdqmem->pagecnt]);
	if (!pageaddr)
		return NULL;

	cmdqmem->pages[cmdqmem->pagecnt] = pageaddr;
	cmdqmem->pagecnt++;
	return pageaddr;
}

static void slic_cmdq_free(struct adapter *adapter)
{
	struct slic_hostcmd *cmd;

	cmd = adapter->cmdq_all.head;
	while (cmd) {
		if (cmd->busy) {
			struct sk_buff *tempskb;

			tempskb = cmd->skb;
			if (tempskb) {
				cmd->skb = NULL;
				dev_kfree_skb_irq(tempskb);
			}
		}
		cmd = cmd->next_all;
	}
	memset(&adapter->cmdq_all, 0, sizeof(struct slic_cmdqueue));
	memset(&adapter->cmdq_free, 0, sizeof(struct slic_cmdqueue));
	memset(&adapter->cmdq_done, 0, sizeof(struct slic_cmdqueue));
	slic_cmdqmem_free(adapter);
}

static void slic_cmdq_addcmdpage(struct adapter *adapter, u32 *page)
{
	struct slic_hostcmd *cmd;
	struct slic_hostcmd *prev;
	struct slic_hostcmd *tail;
	struct slic_cmdqueue *cmdq;
	int cmdcnt;
	void *cmdaddr;
	ulong phys_addr;
	u32 phys_addrl;
	u32 phys_addrh;
	struct slic_handle *pslic_handle;
	unsigned long flags;

	cmdaddr = page;
	cmd = cmdaddr;
	cmdcnt = 0;

	phys_addr = virt_to_bus((void *)page);
	phys_addrl = SLIC_GET_ADDR_LOW(phys_addr);
	phys_addrh = SLIC_GET_ADDR_HIGH(phys_addr);

	prev = NULL;
	tail = cmd;
	while ((cmdcnt < SLIC_CMDQ_CMDSINPAGE) &&
	       (adapter->slic_handle_ix < 256)) {
		/* Allocate and initialize a SLIC_HANDLE for this command */
		spin_lock_irqsave(&adapter->handle_lock, flags);
		pslic_handle  =  adapter->pfree_slic_handles;
		adapter->pfree_slic_handles = pslic_handle->next;
		spin_unlock_irqrestore(&adapter->handle_lock, flags);
		pslic_handle->type = SLIC_HANDLE_CMD;
		pslic_handle->address = (void *)cmd;
		pslic_handle->offset = (ushort)adapter->slic_handle_ix++;
		pslic_handle->other_handle = NULL;
		pslic_handle->next = NULL;

		cmd->pslic_handle = pslic_handle;
		cmd->cmd64.hosthandle = pslic_handle->token.handle_token;
		cmd->busy = false;
		cmd->paddrl = phys_addrl;
		cmd->paddrh = phys_addrh;
		cmd->next_all = prev;
		cmd->next = prev;
		prev = cmd;
		phys_addrl += SLIC_HOSTCMD_SIZE;
		cmdaddr += SLIC_HOSTCMD_SIZE;

		cmd = cmdaddr;
		cmdcnt++;
	}

	cmdq = &adapter->cmdq_all;
	cmdq->count += cmdcnt;	/*  SLIC_CMDQ_CMDSINPAGE;   mooktodo */
	tail->next_all = cmdq->head;
	cmdq->head = prev;
	cmdq = &adapter->cmdq_free;
	spin_lock_irqsave(&cmdq->lock, flags);
	cmdq->count += cmdcnt;	/*  SLIC_CMDQ_CMDSINPAGE;   mooktodo */
	tail->next = cmdq->head;
	cmdq->head = prev;
	spin_unlock_irqrestore(&cmdq->lock, flags);
}

static int slic_cmdq_init(struct adapter *adapter)
{
	int i;
	u32 *pageaddr;

	memset(&adapter->cmdq_all, 0, sizeof(struct slic_cmdqueue));
	memset(&adapter->cmdq_free, 0, sizeof(struct slic_cmdqueue));
	memset(&adapter->cmdq_done, 0, sizeof(struct slic_cmdqueue));
	spin_lock_init(&adapter->cmdq_all.lock);
	spin_lock_init(&adapter->cmdq_free.lock);
	spin_lock_init(&adapter->cmdq_done.lock);
	memset(&adapter->cmdqmem, 0, sizeof(struct slic_cmdqmem));
	adapter->slic_handle_ix = 1;
	for (i = 0; i < SLIC_CMDQ_INITPAGES; i++) {
		pageaddr = slic_cmdqmem_addpage(adapter);
		if (!pageaddr) {
			slic_cmdq_free(adapter);
			return -ENOMEM;
		}
		slic_cmdq_addcmdpage(adapter, pageaddr);
	}
	adapter->slic_handle_ix = 1;

	return 0;
}

static void slic_cmdq_reset(struct adapter *adapter)
{
	struct slic_hostcmd *hcmd;
	struct sk_buff *skb;
	u32 outstanding;
	unsigned long flags;

	spin_lock_irqsave(&adapter->cmdq_free.lock, flags);
	spin_lock(&adapter->cmdq_done.lock);
	outstanding = adapter->cmdq_all.count - adapter->cmdq_done.count;
	outstanding -= adapter->cmdq_free.count;
	hcmd = adapter->cmdq_all.head;
	while (hcmd) {
		if (hcmd->busy) {
			skb = hcmd->skb;
			hcmd->busy = 0;
			hcmd->skb = NULL;
			dev_kfree_skb_irq(skb);
		}
		hcmd = hcmd->next_all;
	}
	adapter->cmdq_free.count = 0;
	adapter->cmdq_free.head = NULL;
	adapter->cmdq_free.tail = NULL;
	adapter->cmdq_done.count = 0;
	adapter->cmdq_done.head = NULL;
	adapter->cmdq_done.tail = NULL;
	adapter->cmdq_free.head = adapter->cmdq_all.head;
	hcmd = adapter->cmdq_all.head;
	while (hcmd) {
		adapter->cmdq_free.count++;
		hcmd->next = hcmd->next_all;
		hcmd = hcmd->next_all;
	}
	if (adapter->cmdq_free.count != adapter->cmdq_all.count) {
		dev_err(&adapter->netdev->dev,
			"free_count %d != all count %d\n",
			adapter->cmdq_free.count, adapter->cmdq_all.count);
	}
	spin_unlock(&adapter->cmdq_done.lock);
	spin_unlock_irqrestore(&adapter->cmdq_free.lock, flags);
}

static void slic_cmdq_getdone(struct adapter *adapter)
{
	struct slic_cmdqueue *done_cmdq = &adapter->cmdq_done;
	struct slic_cmdqueue *free_cmdq = &adapter->cmdq_free;
	unsigned long flags;

	spin_lock_irqsave(&done_cmdq->lock, flags);

	free_cmdq->head = done_cmdq->head;
	free_cmdq->count = done_cmdq->count;
	done_cmdq->head = NULL;
	done_cmdq->tail = NULL;
	done_cmdq->count = 0;
	spin_unlock_irqrestore(&done_cmdq->lock, flags);
}

static struct slic_hostcmd *slic_cmdq_getfree(struct adapter *adapter)
{
	struct slic_cmdqueue *cmdq = &adapter->cmdq_free;
	struct slic_hostcmd *cmd = NULL;
	unsigned long flags;

lock_and_retry:
	spin_lock_irqsave(&cmdq->lock, flags);
retry:
	cmd = cmdq->head;
	if (cmd) {
		cmdq->head = cmd->next;
		cmdq->count--;
		spin_unlock_irqrestore(&cmdq->lock, flags);
	} else {
		slic_cmdq_getdone(adapter);
		cmd = cmdq->head;
		if (cmd) {
			goto retry;
		} else {
			u32 *pageaddr;

			spin_unlock_irqrestore(&cmdq->lock, flags);
			pageaddr = slic_cmdqmem_addpage(adapter);
			if (pageaddr) {
				slic_cmdq_addcmdpage(adapter, pageaddr);
				goto lock_and_retry;
			}
		}
	}
	return cmd;
}

static void slic_cmdq_putdone_irq(struct adapter *adapter,
				struct slic_hostcmd *cmd)
{
	struct slic_cmdqueue *cmdq = &adapter->cmdq_done;

	spin_lock(&cmdq->lock);
	cmd->busy = 0;
	cmd->next = cmdq->head;
	cmdq->head = cmd;
	cmdq->count++;
	if ((adapter->xmitq_full) && (cmdq->count > 10))
		netif_wake_queue(adapter->netdev);
	spin_unlock(&cmdq->lock);
}

static int slic_rcvqueue_fill(struct adapter *adapter)
{
	void *paddr;
	u32 paddrl;
	u32 paddrh;
	struct slic_rcvqueue *rcvq = &adapter->rcvqueue;
	int i = 0;
	struct device *dev = &adapter->netdev->dev;

	while (i < SLIC_RCVQ_FILLENTRIES) {
		struct slic_rcvbuf *rcvbuf;
		struct sk_buff *skb;
#ifdef KLUDGE_FOR_4GB_BOUNDARY
retry_rcvqfill:
#endif
		skb = alloc_skb(SLIC_RCVQ_RCVBUFSIZE, GFP_ATOMIC);
		if (skb) {
			paddr = (void *)(unsigned long)
				pci_map_single(adapter->pcidev,
					       skb->data,
					       SLIC_RCVQ_RCVBUFSIZE,
					       PCI_DMA_FROMDEVICE);
			paddrl = SLIC_GET_ADDR_LOW(paddr);
			paddrh = SLIC_GET_ADDR_HIGH(paddr);

			skb->len = SLIC_RCVBUF_HEADSIZE;
			rcvbuf = (struct slic_rcvbuf *)skb->head;
			rcvbuf->status = 0;
			skb->next = NULL;
#ifdef KLUDGE_FOR_4GB_BOUNDARY
			if (paddrl == 0) {
				dev_err(dev, "%s: LOW 32bits PHYSICAL ADDRESS == 0\n",
					__func__);
				dev_err(dev, "skb[%p] PROBLEM\n", skb);
				dev_err(dev, "         skbdata[%p]\n",
						skb->data);
				dev_err(dev, "         skblen[%x]\n", skb->len);
				dev_err(dev, "         paddr[%p]\n", paddr);
				dev_err(dev, "         paddrl[%x]\n", paddrl);
				dev_err(dev, "         paddrh[%x]\n", paddrh);
				dev_err(dev, "         rcvq->head[%p]\n",
						rcvq->head);
				dev_err(dev, "         rcvq->tail[%p]\n",
						rcvq->tail);
				dev_err(dev, "         rcvq->count[%x]\n",
						rcvq->count);
				dev_err(dev, "SKIP THIS SKB!!!!!!!!\n");
				goto retry_rcvqfill;
			}
#else
			if (paddrl == 0) {
				dev_err(dev, "%s: LOW 32bits PHYSICAL ADDRESS == 0\n",
					__func__);
				dev_err(dev, "skb[%p] PROBLEM\n", skb);
				dev_err(dev, "         skbdata[%p]\n",
						skb->data);
				dev_err(dev, "         skblen[%x]\n", skb->len);
				dev_err(dev, "         paddr[%p]\n", paddr);
				dev_err(dev, "         paddrl[%x]\n", paddrl);
				dev_err(dev, "         paddrh[%x]\n", paddrh);
				dev_err(dev, "         rcvq->head[%p]\n",
						rcvq->head);
				dev_err(dev, "         rcvq->tail[%p]\n",
						rcvq->tail);
				dev_err(dev, "         rcvq->count[%x]\n",
						rcvq->count);
				dev_err(dev, "GIVE TO CARD ANYWAY\n");
			}
#endif
			if (paddrh == 0) {
				slic_write32(adapter, SLIC_REG_HBAR,
					     (u32)paddrl);
			} else {
				slic_write64(adapter, SLIC_REG_HBAR64, paddrl,
					     paddrh);
			}
			if (rcvq->head)
				rcvq->tail->next = skb;
			else
				rcvq->head = skb;
			rcvq->tail = skb;
			rcvq->count++;
			i++;
		} else {
			dev_err(&adapter->netdev->dev,
				"slic_rcvqueue_fill could only get [%d] skbuffs\n",
				i);
			break;
		}
	}
	return i;
}

static void slic_rcvqueue_free(struct adapter *adapter)
{
	struct slic_rcvqueue *rcvq = &adapter->rcvqueue;
	struct sk_buff *skb;

	while (rcvq->head) {
		skb = rcvq->head;
		rcvq->head = rcvq->head->next;
		dev_kfree_skb(skb);
	}
	rcvq->tail = NULL;
	rcvq->head = NULL;
	rcvq->count = 0;
}

static int slic_rcvqueue_init(struct adapter *adapter)
{
	int i, count;
	struct slic_rcvqueue *rcvq = &adapter->rcvqueue;

	rcvq->tail = NULL;
	rcvq->head = NULL;
	rcvq->size = SLIC_RCVQ_ENTRIES;
	rcvq->errors = 0;
	rcvq->count = 0;
	i = SLIC_RCVQ_ENTRIES / SLIC_RCVQ_FILLENTRIES;
	count = 0;
	while (i) {
		count += slic_rcvqueue_fill(adapter);
		i--;
	}
	if (rcvq->count < SLIC_RCVQ_MINENTRIES) {
		slic_rcvqueue_free(adapter);
		return -ENOMEM;
	}
	return 0;
}

static struct sk_buff *slic_rcvqueue_getnext(struct adapter *adapter)
{
	struct slic_rcvqueue *rcvq = &adapter->rcvqueue;
	struct sk_buff *skb;
	struct slic_rcvbuf *rcvbuf;
	int count;

	if (rcvq->count) {
		skb = rcvq->head;
		rcvbuf = (struct slic_rcvbuf *)skb->head;

		if (rcvbuf->status & IRHDDR_SVALID) {
			rcvq->head = rcvq->head->next;
			skb->next = NULL;
			rcvq->count--;
		} else {
			skb = NULL;
		}
	} else {
		dev_err(&adapter->netdev->dev,
			"RcvQ Empty!! rcvq[%p] count[%x]\n", rcvq, rcvq->count);
		skb = NULL;
	}
	while (rcvq->count < SLIC_RCVQ_FILLTHRESH) {
		count = slic_rcvqueue_fill(adapter);
		if (!count)
			break;
	}
	if (skb)
		rcvq->errors = 0;
	return skb;
}

static u32 slic_rcvqueue_reinsert(struct adapter *adapter, struct sk_buff *skb)
{
	struct slic_rcvqueue *rcvq = &adapter->rcvqueue;
	void *paddr;
	u32 paddrl;
	u32 paddrh;
	struct slic_rcvbuf *rcvbuf = (struct slic_rcvbuf *)skb->head;
	struct device *dev;

	paddr = (void *)(unsigned long)
		pci_map_single(adapter->pcidev, skb->head,
			       SLIC_RCVQ_RCVBUFSIZE, PCI_DMA_FROMDEVICE);
	rcvbuf->status = 0;
	skb->next = NULL;

	paddrl = SLIC_GET_ADDR_LOW(paddr);
	paddrh = SLIC_GET_ADDR_HIGH(paddr);

	if (paddrl == 0) {
		dev = &adapter->netdev->dev;
		dev_err(dev, "%s: LOW 32bits PHYSICAL ADDRESS == 0\n",
			__func__);
		dev_err(dev, "skb[%p] PROBLEM\n", skb);
		dev_err(dev, "         skbdata[%p]\n", skb->data);
		dev_err(dev, "         skblen[%x]\n", skb->len);
		dev_err(dev, "         paddr[%p]\n", paddr);
		dev_err(dev, "         paddrl[%x]\n", paddrl);
		dev_err(dev, "         paddrh[%x]\n", paddrh);
		dev_err(dev, "         rcvq->head[%p]\n", rcvq->head);
		dev_err(dev, "         rcvq->tail[%p]\n", rcvq->tail);
		dev_err(dev, "         rcvq->count[%x]\n", rcvq->count);
	}
	if (paddrh == 0) {
		slic_write32(adapter, SLIC_REG_HBAR, (u32)paddrl);
	} else {
		slic_write64(adapter, SLIC_REG_HBAR64, paddrl, paddrh);
	}
	if (rcvq->head)
		rcvq->tail->next = skb;
	else
		rcvq->head = skb;
	rcvq->tail = skb;
	rcvq->count++;
	return rcvq->count;
}

/*
 * slic_link_event_handler -
 *
 * Initiate a link configuration sequence.  The link configuration begins
 * by issuing a READ_LINK_STATUS command to the Utility Processor on the
 * SLIC.  Since the command finishes asynchronously, the slic_upr_comlete
 * routine will follow it up witha UP configuration write command, which
 * will also complete asynchronously.
 *
 */
static int slic_link_event_handler(struct adapter *adapter)
{
	int status;
	struct slic_shmemory *sm = &adapter->shmem;
	dma_addr_t phaddr = sm->lnkstatus_phaddr;


	if (adapter->state != ADAPT_UP) {
		/* Adapter is not operational.  Ignore.  */
		return -ENODEV;
	}
	/* no 4GB wrap guaranteed */
	status = slic_upr_request(adapter, SLIC_UPR_RLSR,
				  cpu_to_le32(lower_32_bits(phaddr)),
				  cpu_to_le32(upper_32_bits(phaddr)), 0, 0);
	return status;
}

static void slic_init_cleanup(struct adapter *adapter)
{
	if (adapter->intrregistered) {
		adapter->intrregistered = 0;
		free_irq(adapter->netdev->irq, adapter->netdev);
	}

	if (adapter->shmem.shmem_data) {
		struct slic_shmemory *sm = &adapter->shmem;
		struct slic_shmem_data *sm_data = sm->shmem_data;

		pci_free_consistent(adapter->pcidev, sizeof(*sm_data), sm_data,
				    sm->isr_phaddr);
	}

	if (adapter->pingtimerset) {
		adapter->pingtimerset = 0;
		del_timer(&adapter->pingtimer);
	}

	slic_rspqueue_free(adapter);
	slic_cmdq_free(adapter);
	slic_rcvqueue_free(adapter);
}

/*
 *  Allocate a mcast_address structure to hold the multicast address.
 *  Link it in.
 */
static int slic_mcast_add_list(struct adapter *adapter, char *address)
{
	struct mcast_address *mcaddr, *mlist;

	/* Check to see if it already exists */
	mlist = adapter->mcastaddrs;
	while (mlist) {
		if (ether_addr_equal(mlist->address, address))
			return 0;
		mlist = mlist->next;
	}

	/* Doesn't already exist.  Allocate a structure to hold it */
	mcaddr = kmalloc(sizeof(*mcaddr), GFP_ATOMIC);
	if (!mcaddr)
		return 1;

	ether_addr_copy(mcaddr->address, address);

	mcaddr->next = adapter->mcastaddrs;
	adapter->mcastaddrs = mcaddr;

	return 0;
}

static void slic_mcast_set_list(struct net_device *dev)
{
	struct adapter *adapter = netdev_priv(dev);
	int status = 0;
	char *addresses;
	struct netdev_hw_addr *ha;

	netdev_for_each_mc_addr(ha, dev) {
		addresses = (char *)&ha->addr;
		status = slic_mcast_add_list(adapter, addresses);
		if (status != 0)
			break;
		slic_mcast_set_bit(adapter, addresses);
	}

	if (adapter->devflags_prev != dev->flags) {
		adapter->macopts = MAC_DIRECTED;
		if (dev->flags) {
			if (dev->flags & IFF_BROADCAST)
				adapter->macopts |= MAC_BCAST;
			if (dev->flags & IFF_PROMISC)
				adapter->macopts |= MAC_PROMISC;
			if (dev->flags & IFF_ALLMULTI)
				adapter->macopts |= MAC_ALLMCAST;
			if (dev->flags & IFF_MULTICAST)
				adapter->macopts |= MAC_MCAST;
		}
		adapter->devflags_prev = dev->flags;
		slic_config_set(adapter, true);
	} else {
		if (status == 0)
			slic_mcast_set_mask(adapter);
	}
}

#define  XMIT_FAIL_LINK_STATE               1
#define  XMIT_FAIL_ZERO_LENGTH              2
#define  XMIT_FAIL_HOSTCMD_FAIL             3

static void slic_xmit_build_request(struct adapter *adapter,
			     struct slic_hostcmd *hcmd, struct sk_buff *skb)
{
	struct slic_host64_cmd *ihcmd;
	ulong phys_addr;

	ihcmd = &hcmd->cmd64;

	ihcmd->flags = adapter->port << IHFLG_IFSHFT;
	ihcmd->command = IHCMD_XMT_REQ;
	ihcmd->u.slic_buffers.totlen = skb->len;
	phys_addr = pci_map_single(adapter->pcidev, skb->data, skb->len,
			PCI_DMA_TODEVICE);
	if (pci_dma_mapping_error(adapter->pcidev, phys_addr)) {
		kfree_skb(skb);
		dev_err(&adapter->pcidev->dev, "DMA mapping error\n");
		return;
	}
	ihcmd->u.slic_buffers.bufs[0].paddrl = SLIC_GET_ADDR_LOW(phys_addr);
	ihcmd->u.slic_buffers.bufs[0].paddrh = SLIC_GET_ADDR_HIGH(phys_addr);
	ihcmd->u.slic_buffers.bufs[0].length = skb->len;
#if BITS_PER_LONG == 64
	hcmd->cmdsize = (u32)((((u64)&ihcmd->u.slic_buffers.bufs[1] -
				     (u64)hcmd) + 31) >> 5);
#else
	hcmd->cmdsize = (((u32)&ihcmd->u.slic_buffers.bufs[1] -
				       (u32)hcmd) + 31) >> 5;
#endif
}

static void slic_xmit_fail(struct adapter *adapter,
		    struct sk_buff *skb,
		    void *cmd, u32 skbtype, u32 status)
{
	if (adapter->xmitq_full)
		netif_stop_queue(adapter->netdev);
	if ((!cmd) && (status <= XMIT_FAIL_HOSTCMD_FAIL)) {
		switch (status) {
		case XMIT_FAIL_LINK_STATE:
			dev_err(&adapter->netdev->dev,
				"reject xmit skb[%p: %x] linkstate[%s] adapter[%s:%d] card[%s:%d]\n",
				skb, skb->pkt_type,
				SLIC_LINKSTATE(adapter->linkstate),
				SLIC_ADAPTER_STATE(adapter->state),
				adapter->state,
				SLIC_CARD_STATE(adapter->card->state),
				adapter->card->state);
			break;
		case XMIT_FAIL_ZERO_LENGTH:
			dev_err(&adapter->netdev->dev,
				"xmit_start skb->len == 0 skb[%p] type[%x]\n",
				skb, skb->pkt_type);
			break;
		case XMIT_FAIL_HOSTCMD_FAIL:
			dev_err(&adapter->netdev->dev,
				"xmit_start skb[%p] type[%x] No host commands available\n",
				skb, skb->pkt_type);
			break;
		}
	}
	dev_kfree_skb(skb);
	adapter->netdev->stats.tx_dropped++;
}

static void slic_rcv_handle_error(struct adapter *adapter,
					struct slic_rcvbuf *rcvbuf)
{
	struct slic_hddr_wds *hdr = (struct slic_hddr_wds *)rcvbuf->data;
	struct net_device *netdev = adapter->netdev;

	if (adapter->devid != SLIC_1GB_DEVICE_ID) {
		if (hdr->frame_status14 & VRHSTAT_802OE)
			adapter->if_events.oflow802++;
		if (hdr->frame_status14 & VRHSTAT_TPOFLO)
			adapter->if_events.Tprtoflow++;
		if (hdr->frame_status_b14 & VRHSTATB_802UE)
			adapter->if_events.uflow802++;
		if (hdr->frame_status_b14 & VRHSTATB_RCVE) {
			adapter->if_events.rcvearly++;
			netdev->stats.rx_fifo_errors++;
		}
		if (hdr->frame_status_b14 & VRHSTATB_BUFF) {
			adapter->if_events.Bufov++;
			netdev->stats.rx_over_errors++;
		}
		if (hdr->frame_status_b14 & VRHSTATB_CARRE) {
			adapter->if_events.Carre++;
			netdev->stats.tx_carrier_errors++;
		}
		if (hdr->frame_status_b14 & VRHSTATB_LONGE)
			adapter->if_events.Longe++;
		if (hdr->frame_status_b14 & VRHSTATB_PREA)
			adapter->if_events.Invp++;
		if (hdr->frame_status_b14 & VRHSTATB_CRC) {
			adapter->if_events.Crc++;
			netdev->stats.rx_crc_errors++;
		}
		if (hdr->frame_status_b14 & VRHSTATB_DRBL)
			adapter->if_events.Drbl++;
		if (hdr->frame_status_b14 & VRHSTATB_CODE)
			adapter->if_events.Code++;
		if (hdr->frame_status_b14 & VRHSTATB_TPCSUM)
			adapter->if_events.TpCsum++;
		if (hdr->frame_status_b14 & VRHSTATB_TPHLEN)
			adapter->if_events.TpHlen++;
		if (hdr->frame_status_b14 & VRHSTATB_IPCSUM)
			adapter->if_events.IpCsum++;
		if (hdr->frame_status_b14 & VRHSTATB_IPLERR)
			adapter->if_events.IpLen++;
		if (hdr->frame_status_b14 & VRHSTATB_IPHERR)
			adapter->if_events.IpHlen++;
	} else {
		if (hdr->frame_statusGB & VGBSTAT_XPERR) {
			u32 xerr = hdr->frame_statusGB >> VGBSTAT_XERRSHFT;

			if (xerr == VGBSTAT_XCSERR)
				adapter->if_events.TpCsum++;
			if (xerr == VGBSTAT_XUFLOW)
				adapter->if_events.Tprtoflow++;
			if (xerr == VGBSTAT_XHLEN)
				adapter->if_events.TpHlen++;
		}
		if (hdr->frame_statusGB & VGBSTAT_NETERR) {
			u32 nerr =
			    (hdr->
			     frame_statusGB >> VGBSTAT_NERRSHFT) &
			    VGBSTAT_NERRMSK;
			if (nerr == VGBSTAT_NCSERR)
				adapter->if_events.IpCsum++;
			if (nerr == VGBSTAT_NUFLOW)
				adapter->if_events.IpLen++;
			if (nerr == VGBSTAT_NHLEN)
				adapter->if_events.IpHlen++;
		}
		if (hdr->frame_statusGB & VGBSTAT_LNKERR) {
			u32 lerr = hdr->frame_statusGB & VGBSTAT_LERRMSK;

			if (lerr == VGBSTAT_LDEARLY)
				adapter->if_events.rcvearly++;
			if (lerr == VGBSTAT_LBOFLO)
				adapter->if_events.Bufov++;
			if (lerr == VGBSTAT_LCODERR)
				adapter->if_events.Code++;
			if (lerr == VGBSTAT_LDBLNBL)
				adapter->if_events.Drbl++;
			if (lerr == VGBSTAT_LCRCERR)
				adapter->if_events.Crc++;
			if (lerr == VGBSTAT_LOFLO)
				adapter->if_events.oflow802++;
			if (lerr == VGBSTAT_LUFLO)
				adapter->if_events.uflow802++;
		}
	}
}

#define TCP_OFFLOAD_FRAME_PUSHFLAG  0x10000000
#define M_FAST_PATH                 0x0040

static void slic_rcv_handler(struct adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	struct sk_buff *skb;
	struct slic_rcvbuf *rcvbuf;
	u32 frames = 0;

	while ((skb = slic_rcvqueue_getnext(adapter))) {
		u32 rx_bytes;

		rcvbuf = (struct slic_rcvbuf *)skb->head;
		adapter->card->events++;
		if (rcvbuf->status & IRHDDR_ERR) {
			adapter->rx_errors++;
			slic_rcv_handle_error(adapter, rcvbuf);
			slic_rcvqueue_reinsert(adapter, skb);
			continue;
		}

		if (!slic_mac_filter(adapter, (struct ether_header *)
					rcvbuf->data)) {
			slic_rcvqueue_reinsert(adapter, skb);
			continue;
		}
		skb_pull(skb, SLIC_RCVBUF_HEADSIZE);
		rx_bytes = (rcvbuf->length & IRHDDR_FLEN_MSK);
		skb_put(skb, rx_bytes);
		netdev->stats.rx_packets++;
		netdev->stats.rx_bytes += rx_bytes;
#if SLIC_OFFLOAD_IP_CHECKSUM
		skb->ip_summed = CHECKSUM_UNNECESSARY;
#endif

		skb->dev = adapter->netdev;
		skb->protocol = eth_type_trans(skb, skb->dev);
		netif_rx(skb);

		++frames;
#if SLIC_INTERRUPT_PROCESS_LIMIT
		if (frames >= SLIC_RCVQ_MAX_PROCESS_ISR) {
			adapter->rcv_interrupt_yields++;
			break;
		}
#endif
	}
	adapter->max_isr_rcvs = max(adapter->max_isr_rcvs, frames);
}

static void slic_xmit_complete(struct adapter *adapter)
{
	struct slic_hostcmd *hcmd;
	struct slic_rspbuf *rspbuf;
	u32 frames = 0;
	struct slic_handle_word slic_handle_word;

	do {
		rspbuf = slic_rspqueue_getnext(adapter);
		if (!rspbuf)
			break;
		adapter->xmit_completes++;
		adapter->card->events++;
		/*
		 * Get the complete host command buffer
		 */
		slic_handle_word.handle_token = rspbuf->hosthandle;
		hcmd =
			adapter->slic_handles[slic_handle_word.handle_index].
									address;
/*      hcmd = (struct slic_hostcmd *) rspbuf->hosthandle; */
		if (hcmd->type == SLIC_CMD_DUMB) {
			if (hcmd->skb)
				dev_kfree_skb_irq(hcmd->skb);
			slic_cmdq_putdone_irq(adapter, hcmd);
		}
		rspbuf->status = 0;
		rspbuf->hosthandle = 0;
		frames++;
	} while (1);
	adapter->max_isr_xmits = max(adapter->max_isr_xmits, frames);
}

static void slic_interrupt_card_up(u32 isr, struct adapter *adapter,
			struct net_device *dev)
{
	if (isr & ~ISR_IO) {
		if (isr & ISR_ERR) {
			adapter->error_interrupts++;
			if (isr & ISR_RMISS) {
				int count;
				int pre_count;
				int errors;

				struct slic_rcvqueue *rcvq =
					&adapter->rcvqueue;

				adapter->error_rmiss_interrupts++;

				if (!rcvq->errors)
					rcv_count = rcvq->count;
				pre_count = rcvq->count;
				errors = rcvq->errors;

				while (rcvq->count < SLIC_RCVQ_FILLTHRESH) {
					count = slic_rcvqueue_fill(adapter);
					if (!count)
						break;
				}
			} else if (isr & ISR_XDROP) {
				dev_err(&dev->dev,
						"isr & ISR_ERR [%x] ISR_XDROP\n",
						isr);
			} else {
				dev_err(&dev->dev,
						"isr & ISR_ERR [%x]\n",
						isr);
			}
		}

		if (isr & ISR_LEVENT) {
			adapter->linkevent_interrupts++;
			if (slic_link_event_handler(adapter))
				adapter->linkevent_interrupts--;
		}

		if ((isr & ISR_UPC) || (isr & ISR_UPCERR) ||
		    (isr & ISR_UPCBSY)) {
			adapter->upr_interrupts++;
			slic_upr_request_complete(adapter, isr);
		}
	}

	if (isr & ISR_RCV) {
		adapter->rcv_interrupts++;
		slic_rcv_handler(adapter);
	}

	if (isr & ISR_CMD) {
		adapter->xmit_interrupts++;
		slic_xmit_complete(adapter);
	}
}

static irqreturn_t slic_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
	struct adapter *adapter = netdev_priv(dev);
	struct slic_shmemory *sm = &adapter->shmem;
	struct slic_shmem_data *sm_data = sm->shmem_data;
	u32 isr;

	if (sm_data->isr) {
		slic_write32(adapter, SLIC_REG_ICR, ICR_INT_MASK);
		slic_flush_write(adapter);

		isr = sm_data->isr;
		sm_data->isr = 0;
		adapter->num_isrs++;
		switch (adapter->card->state) {
		case CARD_UP:
			slic_interrupt_card_up(isr, adapter, dev);
			break;

		case CARD_DOWN:
			if ((isr & ISR_UPC) ||
			    (isr & ISR_UPCERR) || (isr & ISR_UPCBSY)) {
				adapter->upr_interrupts++;
				slic_upr_request_complete(adapter, isr);
			}
			break;
		}

		adapter->all_reg_writes += 2;
		adapter->isr_reg_writes++;
		slic_write32(adapter, SLIC_REG_ISR, 0);
	} else {
		adapter->false_interrupts++;
	}
	return IRQ_HANDLED;
}

#define NORMAL_ETHFRAME     0

static netdev_tx_t slic_xmit_start(struct sk_buff *skb, struct net_device *dev)
{
	struct sliccard *card;
	struct adapter *adapter = netdev_priv(dev);
	struct slic_hostcmd *hcmd = NULL;
	u32 status = 0;
	void *offloadcmd = NULL;

	card = adapter->card;
	if ((adapter->linkstate != LINK_UP) ||
	    (adapter->state != ADAPT_UP) || (card->state != CARD_UP)) {
		status = XMIT_FAIL_LINK_STATE;
		goto xmit_fail;

	} else if (skb->len == 0) {
		status = XMIT_FAIL_ZERO_LENGTH;
		goto xmit_fail;
	}

	hcmd = slic_cmdq_getfree(adapter);
	if (!hcmd) {
		adapter->xmitq_full = 1;
		status = XMIT_FAIL_HOSTCMD_FAIL;
		goto xmit_fail;
	}
	hcmd->skb = skb;
	hcmd->busy = 1;
	hcmd->type = SLIC_CMD_DUMB;
	slic_xmit_build_request(adapter, hcmd, skb);
	dev->stats.tx_packets++;
	dev->stats.tx_bytes += skb->len;

#ifdef DEBUG_DUMP
	if (adapter->kill_card) {
		struct slic_host64_cmd ihcmd;

		ihcmd = &hcmd->cmd64;

		ihcmd->flags |= 0x40;
		adapter->kill_card = 0;	/* only do this once */
	}
#endif
	if (hcmd->paddrh == 0) {
		slic_write32(adapter, SLIC_REG_CBAR, (hcmd->paddrl |
						      hcmd->cmdsize));
	} else {
		slic_write64(adapter, SLIC_REG_CBAR64,
			     hcmd->paddrl | hcmd->cmdsize, hcmd->paddrh);
	}
xmit_done:
	return NETDEV_TX_OK;
xmit_fail:
	slic_xmit_fail(adapter, skb, offloadcmd, NORMAL_ETHFRAME, status);
	goto xmit_done;
}

static void slic_adapter_freeresources(struct adapter *adapter)
{
	slic_init_cleanup(adapter);
	adapter->error_interrupts = 0;
	adapter->rcv_interrupts = 0;
	adapter->xmit_interrupts = 0;
	adapter->linkevent_interrupts = 0;
	adapter->upr_interrupts = 0;
	adapter->num_isrs = 0;
	adapter->xmit_completes = 0;
	adapter->rcv_broadcasts = 0;
	adapter->rcv_multicasts = 0;
	adapter->rcv_unicasts = 0;
}

static int slic_adapter_allocresources(struct adapter *adapter,
				       unsigned long *flags)
{
	if (!adapter->intrregistered) {
		int retval;

		spin_unlock_irqrestore(&slic_global.driver_lock, *flags);

		retval = request_irq(adapter->netdev->irq,
				     &slic_interrupt,
				     IRQF_SHARED,
				     adapter->netdev->name, adapter->netdev);

		spin_lock_irqsave(&slic_global.driver_lock, *flags);

		if (retval) {
			dev_err(&adapter->netdev->dev,
				"request_irq (%s) FAILED [%x]\n",
				adapter->netdev->name, retval);
			return retval;
		}
		adapter->intrregistered = 1;
	}
	return 0;
}

/*
 *  slic_if_init
 *
 *  Perform initialization of our slic interface.
 *
 */
static int slic_if_init(struct adapter *adapter, unsigned long *flags)
{
	struct sliccard *card = adapter->card;
	struct net_device *dev = adapter->netdev;
	struct slic_shmemory *sm = &adapter->shmem;
	struct slic_shmem_data *sm_data = sm->shmem_data;
	int rc;

	/* adapter should be down at this point */
	if (adapter->state != ADAPT_DOWN) {
		dev_err(&dev->dev, "%s: adapter->state != ADAPT_DOWN\n",
			__func__);
		rc = -EIO;
		goto err;
	}

	adapter->devflags_prev = dev->flags;
	adapter->macopts = MAC_DIRECTED;
	if (dev->flags) {
		if (dev->flags & IFF_BROADCAST)
			adapter->macopts |= MAC_BCAST;
		if (dev->flags & IFF_PROMISC)
			adapter->macopts |= MAC_PROMISC;
		if (dev->flags & IFF_ALLMULTI)
			adapter->macopts |= MAC_ALLMCAST;
		if (dev->flags & IFF_MULTICAST)
			adapter->macopts |= MAC_MCAST;
	}
	rc = slic_adapter_allocresources(adapter, flags);
	if (rc) {
		dev_err(&dev->dev, "slic_adapter_allocresources FAILED %x\n",
			rc);
		slic_adapter_freeresources(adapter);
		goto err;
	}

	if (!adapter->queues_initialized) {
		rc = slic_rspqueue_init(adapter);
		if (rc)
			goto err;
		rc = slic_cmdq_init(adapter);
		if (rc)
			goto err;
		rc = slic_rcvqueue_init(adapter);
		if (rc)
			goto err;
		adapter->queues_initialized = 1;
	}

	slic_write32(adapter, SLIC_REG_ICR, ICR_INT_OFF);
	slic_flush_write(adapter);
	mdelay(1);

	if (!adapter->isp_initialized) {
		unsigned long flags;

		spin_lock_irqsave(&adapter->bit64reglock, flags);
		slic_write32(adapter, SLIC_REG_ADDR_UPPER,
			     cpu_to_le32(upper_32_bits(sm->isr_phaddr)));
		slic_write32(adapter, SLIC_REG_ISP,
			     cpu_to_le32(lower_32_bits(sm->isr_phaddr)));
		spin_unlock_irqrestore(&adapter->bit64reglock, flags);

		adapter->isp_initialized = 1;
	}

	adapter->state = ADAPT_UP;
	if (!card->loadtimerset) {
		setup_timer(&card->loadtimer, &slic_timer_load_check,
			    (ulong)card);
		card->loadtimer.expires =
		    jiffies + (SLIC_LOADTIMER_PERIOD * HZ);
		add_timer(&card->loadtimer);

		card->loadtimerset = 1;
	}

	if (!adapter->pingtimerset) {
		setup_timer(&adapter->pingtimer, &slic_timer_ping, (ulong)dev);
		adapter->pingtimer.expires =
		    jiffies + (PING_TIMER_INTERVAL * HZ);
		add_timer(&adapter->pingtimer);
		adapter->pingtimerset = 1;
		adapter->card->pingstatus = ISR_PINGMASK;
	}

	/*
	 *    clear any pending events, then enable interrupts
	 */
	sm_data->isr = 0;
	slic_write32(adapter, SLIC_REG_ISR, 0);
	slic_write32(adapter, SLIC_REG_ICR, ICR_INT_ON);

	slic_link_config(adapter, LINK_AUTOSPEED, LINK_AUTOD);
	slic_flush_write(adapter);

	rc = slic_link_event_handler(adapter);
	if (rc) {
		/* disable interrupts then clear pending events */
		slic_write32(adapter, SLIC_REG_ICR, ICR_INT_OFF);
		slic_write32(adapter, SLIC_REG_ISR, 0);
		slic_flush_write(adapter);

		if (adapter->pingtimerset) {
			del_timer(&adapter->pingtimer);
			adapter->pingtimerset = 0;
		}
		if (card->loadtimerset) {
			del_timer(&card->loadtimer);
			card->loadtimerset = 0;
		}
		adapter->state = ADAPT_DOWN;
		slic_adapter_freeresources(adapter);
	}

err:
	return rc;
}

static int slic_entry_open(struct net_device *dev)
{
	struct adapter *adapter = netdev_priv(dev);
	struct sliccard *card = adapter->card;
	unsigned long flags;
	int status;

	netif_carrier_off(dev);

	spin_lock_irqsave(&slic_global.driver_lock, flags);
	if (!adapter->activated) {
		card->adapters_activated++;
		slic_global.num_slic_ports_active++;
		adapter->activated = 1;
	}
	status = slic_if_init(adapter, &flags);

	if (status != 0) {
		if (adapter->activated) {
			card->adapters_activated--;
			slic_global.num_slic_ports_active--;
			adapter->activated = 0;
		}
		goto spin_unlock;
	}
	if (!card->master)
		card->master = adapter;

spin_unlock:
	spin_unlock_irqrestore(&slic_global.driver_lock, flags);

	netif_start_queue(adapter->netdev);

	return status;
}

static void slic_card_cleanup(struct sliccard *card)
{
	if (card->loadtimerset) {
		card->loadtimerset = 0;
		del_timer_sync(&card->loadtimer);
	}

	kfree(card);
}

static void slic_entry_remove(struct pci_dev *pcidev)
{
	struct net_device *dev = pci_get_drvdata(pcidev);
	struct adapter *adapter = netdev_priv(dev);
	struct sliccard *card;
	struct mcast_address *mcaddr, *mlist;

	unregister_netdev(dev);

	slic_adapter_freeresources(adapter);
	iounmap(adapter->regs);

	/* free multicast addresses */
	mlist = adapter->mcastaddrs;
	while (mlist) {
		mcaddr = mlist;
		mlist = mlist->next;
		kfree(mcaddr);
	}
	card = adapter->card;
	card->adapters_allocated--;
	adapter->allocated = 0;
	if (!card->adapters_allocated) {
		struct sliccard *curr_card = slic_global.slic_card;

		if (curr_card == card) {
			slic_global.slic_card = card->next;
		} else {
			while (curr_card->next != card)
				curr_card = curr_card->next;
			curr_card->next = card->next;
		}
		slic_global.num_slic_cards--;
		slic_card_cleanup(card);
	}
	free_netdev(dev);
	pci_release_regions(pcidev);
	pci_disable_device(pcidev);
}

static int slic_entry_halt(struct net_device *dev)
{
	struct adapter *adapter = netdev_priv(dev);
	struct sliccard *card = adapter->card;
	unsigned long flags;

	spin_lock_irqsave(&slic_global.driver_lock, flags);
	netif_stop_queue(adapter->netdev);
	adapter->state = ADAPT_DOWN;
	adapter->linkstate = LINK_DOWN;
	adapter->upr_list = NULL;
	adapter->upr_busy = 0;
	adapter->devflags_prev = 0;
	slic_write32(adapter, SLIC_REG_ICR, ICR_INT_OFF);
	adapter->all_reg_writes++;
	adapter->icr_reg_writes++;
	slic_config_clear(adapter);
	if (adapter->activated) {
		card->adapters_activated--;
		slic_global.num_slic_ports_active--;
		adapter->activated = 0;
	}
#ifdef AUTOMATIC_RESET
	slic_write32(adapter, SLIC_REG_RESET_IFACE, 0);
#endif
	slic_flush_write(adapter);

	/*
	 *  Reset the adapter's cmd queues
	 */
	slic_cmdq_reset(adapter);

#ifdef AUTOMATIC_RESET
	if (!card->adapters_activated)
		slic_card_init(card, adapter);
#endif

	spin_unlock_irqrestore(&slic_global.driver_lock, flags);

	netif_carrier_off(dev);

	return 0;
}

static struct net_device_stats *slic_get_stats(struct net_device *dev)
{
	struct adapter *adapter = netdev_priv(dev);

	dev->stats.collisions = adapter->slic_stats.iface.xmit_collisions;
	dev->stats.rx_errors = adapter->slic_stats.iface.rcv_errors;
	dev->stats.tx_errors = adapter->slic_stats.iface.xmt_errors;
	dev->stats.rx_missed_errors = adapter->slic_stats.iface.rcv_discards;
	dev->stats.tx_heartbeat_errors = 0;
	dev->stats.tx_aborted_errors = 0;
	dev->stats.tx_window_errors = 0;
	dev->stats.tx_fifo_errors = 0;
	dev->stats.rx_frame_errors = 0;
	dev->stats.rx_length_errors = 0;

	return &dev->stats;
}

static int slic_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct adapter *adapter = netdev_priv(dev);
	struct ethtool_cmd edata;
	struct ethtool_cmd ecmd;
	u32 data[7];
	u32 intagg;

	switch (cmd) {
	case SIOCSLICSETINTAGG:
		if (copy_from_user(data, rq->ifr_data, 28))
			return -EFAULT;
		intagg = data[0];
		dev_err(&dev->dev, "set interrupt aggregation to %d\n",
			intagg);
		slic_intagg_set(adapter, intagg);
		return 0;

	case SIOCETHTOOL:
		if (copy_from_user(&ecmd, rq->ifr_data, sizeof(ecmd)))
			return -EFAULT;

		if (ecmd.cmd == ETHTOOL_GSET) {
			memset(&edata, 0, sizeof(edata));
			edata.supported = (SUPPORTED_10baseT_Half |
					   SUPPORTED_10baseT_Full |
					   SUPPORTED_100baseT_Half |
					   SUPPORTED_100baseT_Full |
					   SUPPORTED_Autoneg | SUPPORTED_MII);
			edata.port = PORT_MII;
			edata.transceiver = XCVR_INTERNAL;
			edata.phy_address = 0;
			if (adapter->linkspeed == LINK_100MB)
				edata.speed = SPEED_100;
			else if (adapter->linkspeed == LINK_10MB)
				edata.speed = SPEED_10;
			else
				edata.speed = 0;

			if (adapter->linkduplex == LINK_FULLD)
				edata.duplex = DUPLEX_FULL;
			else
				edata.duplex = DUPLEX_HALF;

			edata.autoneg = AUTONEG_ENABLE;
			edata.maxtxpkt = 1;
			edata.maxrxpkt = 1;
			if (copy_to_user(rq->ifr_data, &edata, sizeof(edata)))
				return -EFAULT;

		} else if (ecmd.cmd == ETHTOOL_SSET) {
			if (!capable(CAP_NET_ADMIN))
				return -EPERM;

			if (adapter->linkspeed == LINK_100MB)
				edata.speed = SPEED_100;
			else if (adapter->linkspeed == LINK_10MB)
				edata.speed = SPEED_10;
			else
				edata.speed = 0;

			if (adapter->linkduplex == LINK_FULLD)
				edata.duplex = DUPLEX_FULL;
			else
				edata.duplex = DUPLEX_HALF;

			edata.autoneg = AUTONEG_ENABLE;
			edata.maxtxpkt = 1;
			edata.maxrxpkt = 1;
			if ((ecmd.speed != edata.speed) ||
			    (ecmd.duplex != edata.duplex)) {
				u32 speed;
				u32 duplex;

				if (ecmd.speed == SPEED_10)
					speed = 0;
				else
					speed = PCR_SPEED_100;
				if (ecmd.duplex == DUPLEX_FULL)
					duplex = PCR_DUPLEX_FULL;
				else
					duplex = 0;
				slic_link_config(adapter, speed, duplex);
				if (slic_link_event_handler(adapter))
					return -EFAULT;
			}
		}
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static void slic_config_pci(struct pci_dev *pcidev)
{
	u16 pci_command;
	u16 new_command;

	pci_read_config_word(pcidev, PCI_COMMAND, &pci_command);

	new_command = pci_command | PCI_COMMAND_MASTER
	    | PCI_COMMAND_MEMORY
	    | PCI_COMMAND_INVALIDATE
	    | PCI_COMMAND_PARITY | PCI_COMMAND_SERR | PCI_COMMAND_FAST_BACK;
	if (pci_command != new_command)
		pci_write_config_word(pcidev, PCI_COMMAND, new_command);
}

static int slic_card_init(struct sliccard *card, struct adapter *adapter)
{
	struct slic_shmemory *sm = &adapter->shmem;
	struct slic_shmem_data *sm_data = sm->shmem_data;
	struct slic_eeprom *peeprom;
	struct oslic_eeprom *pOeeprom;
	dma_addr_t phys_config;
	u32 phys_configh;
	u32 phys_configl;
	u32 i = 0;
	int status;
	uint macaddrs = card->card_size;
	ushort eecodesize;
	ushort dramsize;
	ushort ee_chksum;
	ushort calc_chksum;
	struct slic_config_mac *pmac;
	unsigned char fruformat;
	unsigned char oemfruformat;
	struct atk_fru *patkfru;
	union oemfru *poemfru;
	unsigned long flags;

	/* Reset everything except PCI configuration space */
	slic_soft_reset(adapter);

	/* Download the microcode */
	status = slic_card_download(adapter);
	if (status)
		return status;

	if (!card->config_set) {
		peeprom = pci_alloc_consistent(adapter->pcidev,
					       sizeof(struct slic_eeprom),
					       &phys_config);

		phys_configl = SLIC_GET_ADDR_LOW(phys_config);
		phys_configh = SLIC_GET_ADDR_HIGH(phys_config);

		if (!peeprom) {
			dev_err(&adapter->pcidev->dev,
				"Failed to allocate DMA memory for EEPROM.\n");
			return -ENOMEM;
		}

		memset(peeprom, 0, sizeof(struct slic_eeprom));

		slic_write32(adapter, SLIC_REG_ICR, ICR_INT_OFF);
		slic_flush_write(adapter);
		mdelay(1);

		spin_lock_irqsave(&adapter->bit64reglock, flags);
		slic_write32(adapter, SLIC_REG_ADDR_UPPER,
			     cpu_to_le32(upper_32_bits(sm->isr_phaddr)));
		slic_write32(adapter, SLIC_REG_ISP,
			     cpu_to_le32(lower_32_bits(sm->isr_phaddr)));
		spin_unlock_irqrestore(&adapter->bit64reglock, flags);

		status = slic_config_get(adapter, phys_configl, phys_configh);
		if (status) {
			dev_err(&adapter->pcidev->dev,
				"Failed to fetch config data from device.\n");
			goto card_init_err;
		}

		for (;;) {
			if (sm_data->isr) {
				if (sm_data->isr & ISR_UPC) {
					sm_data->isr = 0;
					slic_write64(adapter, SLIC_REG_ISP, 0,
						     0);
					slic_write32(adapter, SLIC_REG_ISR, 0);
					slic_flush_write(adapter);

					slic_upr_request_complete(adapter, 0);
					break;
				}

				sm_data->isr = 0;
				slic_write32(adapter, SLIC_REG_ISR, 0);
				slic_flush_write(adapter);
			} else {
				mdelay(1);
				i++;
				if (i > 5000) {
					dev_err(&adapter->pcidev->dev,
						"Fetch of config data timed out.\n");
					slic_write64(adapter, SLIC_REG_ISP,
						     0, 0);
					slic_flush_write(adapter);

					status = -EINVAL;
					goto card_init_err;
				}
			}
		}

		switch (adapter->devid) {
		/* Oasis card */
		case SLIC_2GB_DEVICE_ID:
			/* extract EEPROM data and pointers to EEPROM data */
			pOeeprom = (struct oslic_eeprom *)peeprom;
			eecodesize = pOeeprom->EecodeSize;
			dramsize = pOeeprom->DramSize;
			pmac = pOeeprom->MacInfo;
			fruformat = pOeeprom->FruFormat;
			patkfru = &pOeeprom->AtkFru;
			oemfruformat = pOeeprom->OemFruFormat;
			poemfru = &pOeeprom->OemFru;
			macaddrs = 2;
			/*
			 * Minor kludge for Oasis card
			 * get 2 MAC addresses from the
			 * EEPROM to ensure that function 1
			 * gets the Port 1 MAC address
			 */
			break;
		default:
			/* extract EEPROM data and pointers to EEPROM data */
			eecodesize = peeprom->EecodeSize;
			dramsize = peeprom->DramSize;
			pmac = peeprom->u2.mac.MacInfo;
			fruformat = peeprom->FruFormat;
			patkfru = &peeprom->AtkFru;
			oemfruformat = peeprom->OemFruFormat;
			poemfru = &peeprom->OemFru;
			break;
		}

		card->config.EepromValid = false;

		/*  see if the EEPROM is valid by checking it's checksum */
		if ((eecodesize <= MAX_EECODE_SIZE) &&
		    (eecodesize >= MIN_EECODE_SIZE)) {

			ee_chksum =
			    *(u16 *)((char *)peeprom + (eecodesize - 2));
			/*
			 *  calculate the EEPROM checksum
			 */
			calc_chksum = slic_eeprom_cksum(peeprom,
							eecodesize - 2);
			/*
			 *  if the ucdoe chksum flag bit worked,
			 *  we wouldn't need this
			 */
			if (ee_chksum == calc_chksum)
				card->config.EepromValid = true;
		}
		/*  copy in the DRAM size */
		card->config.DramSize = dramsize;

		/*  copy in the MAC address(es) */
		for (i = 0; i < macaddrs; i++) {
			memcpy(&card->config.MacInfo[i],
			       &pmac[i], sizeof(struct slic_config_mac));
		}

		/*  copy the Alacritech FRU information */
		card->config.FruFormat = fruformat;
		memcpy(&card->config.AtkFru, patkfru,
						sizeof(struct atk_fru));

		pci_free_consistent(adapter->pcidev,
				    sizeof(struct slic_eeprom),
				    peeprom, phys_config);

		if (!card->config.EepromValid) {
			slic_write64(adapter, SLIC_REG_ISP, 0, 0);
			slic_flush_write(adapter);
			dev_err(&adapter->pcidev->dev, "EEPROM invalid.\n");
			return -EINVAL;
		}

		card->config_set = 1;
	}

	status = slic_card_download_gbrcv(adapter);
	if (status)
		return status;

	if (slic_global.dynamic_intagg)
		slic_intagg_set(adapter, 0);
	else
		slic_intagg_set(adapter, adapter->intagg_delay);

	/*
	 *  Initialize ping status to "ok"
	 */
	card->pingstatus = ISR_PINGMASK;

	/*
	 * Lastly, mark our card state as up and return success
	 */
	card->state = CARD_UP;
	card->reset_in_progress = 0;

	return 0;

card_init_err:
	pci_free_consistent(adapter->pcidev, sizeof(struct slic_eeprom),
			    peeprom, phys_config);
	return status;
}

static int slic_get_coalesce(struct net_device *dev,
			     struct ethtool_coalesce *coalesce)
{
	struct adapter *adapter = netdev_priv(dev);

	adapter->intagg_delay = coalesce->rx_coalesce_usecs;
	adapter->dynamic_intagg = coalesce->use_adaptive_rx_coalesce;
	return 0;
}

static int slic_set_coalesce(struct net_device *dev,
			     struct ethtool_coalesce *coalesce)
{
	struct adapter *adapter = netdev_priv(dev);

	coalesce->rx_coalesce_usecs = adapter->intagg_delay;
	coalesce->use_adaptive_rx_coalesce = adapter->dynamic_intagg;
	return 0;
}

static void slic_init_driver(void)
{
	if (slic_first_init) {
		slic_first_init = 0;
		spin_lock_init(&slic_global.driver_lock);
	}
}

static int slic_init_adapter(struct net_device *netdev,
			     struct pci_dev *pcidev,
			     const struct pci_device_id *pci_tbl_entry,
			     void __iomem *memaddr, int chip_idx)
{
	ushort index;
	struct slic_handle *pslic_handle;
	struct adapter *adapter = netdev_priv(netdev);
	struct slic_shmemory *sm = &adapter->shmem;
	struct slic_shmem_data *sm_data;
	dma_addr_t phaddr;

/*	adapter->pcidev = pcidev;*/
	adapter->vendid = pci_tbl_entry->vendor;
	adapter->devid = pci_tbl_entry->device;
	adapter->subsysid = pci_tbl_entry->subdevice;
	adapter->busnumber = pcidev->bus->number;
	adapter->slotnumber = ((pcidev->devfn >> 3) & 0x1F);
	adapter->functionnumber = (pcidev->devfn & 0x7);
	adapter->regs = memaddr;
	adapter->irq = pcidev->irq;
	adapter->chipid = chip_idx;
	adapter->port = 0;
	adapter->cardindex = adapter->port;
	spin_lock_init(&adapter->upr_lock);
	spin_lock_init(&adapter->bit64reglock);
	spin_lock_init(&adapter->adapter_lock);
	spin_lock_init(&adapter->reset_lock);
	spin_lock_init(&adapter->handle_lock);

	adapter->card_size = 1;
	/*
	 * Initialize slic_handle array
	 */
	/*
	 * Start with 1.  0 is an invalid host handle.
	 */
	for (index = 1, pslic_handle = &adapter->slic_handles[1];
	     index < SLIC_CMDQ_MAXCMDS; index++, pslic_handle++) {

		pslic_handle->token.handle_index = index;
		pslic_handle->type = SLIC_HANDLE_FREE;
		pslic_handle->next = adapter->pfree_slic_handles;
		adapter->pfree_slic_handles = pslic_handle;
	}
	sm_data = pci_zalloc_consistent(adapter->pcidev, sizeof(*sm_data),
					&phaddr);
	if (!sm_data)
		return -ENOMEM;

	sm->shmem_data = sm_data;
	sm->isr_phaddr = phaddr;
	sm->lnkstatus_phaddr = phaddr + offsetof(struct slic_shmem_data,
						 lnkstatus);
	sm->stats_phaddr = phaddr + offsetof(struct slic_shmem_data, stats);

	return 0;
}

static const struct net_device_ops slic_netdev_ops = {
	.ndo_open		= slic_entry_open,
	.ndo_stop		= slic_entry_halt,
	.ndo_start_xmit		= slic_xmit_start,
	.ndo_do_ioctl		= slic_ioctl,
	.ndo_set_mac_address	= slic_mac_set_address,
	.ndo_get_stats		= slic_get_stats,
	.ndo_set_rx_mode	= slic_mcast_set_list,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_change_mtu		= eth_change_mtu,
};

static u32 slic_card_locate(struct adapter *adapter)
{
	struct sliccard *card = slic_global.slic_card;
	struct physcard *physcard = slic_global.phys_card;
	ushort card_hostid;
	uint i;

	card_hostid = slic_read32(adapter, SLIC_REG_HOSTID);

	/* Initialize a new card structure if need be */
	if (card_hostid == SLIC_HOSTID_DEFAULT) {
		card = kzalloc(sizeof(*card), GFP_KERNEL);
		if (!card)
			return -ENOMEM;

		card->next = slic_global.slic_card;
		slic_global.slic_card = card;
		card->busnumber = adapter->busnumber;
		card->slotnumber = adapter->slotnumber;

		/* Find an available cardnum */
		for (i = 0; i < SLIC_MAX_CARDS; i++) {
			if (slic_global.cardnuminuse[i] == 0) {
				slic_global.cardnuminuse[i] = 1;
				card->cardnum = i;
				break;
			}
		}
		slic_global.num_slic_cards++;
	} else {
		/* Card exists, find the card this adapter belongs to */
		while (card) {
			if (card->cardnum == card_hostid)
				break;
			card = card->next;
		}
	}

	if (!card)
		return -ENXIO;
	/* Put the adapter in the card's adapter list */
	if (!card->adapter[adapter->port]) {
		card->adapter[adapter->port] = adapter;
		adapter->card = card;
	}

	card->card_size = 1;	/* one port per *logical* card */

	while (physcard) {
		for (i = 0; i < SLIC_MAX_PORTS; i++) {
			if (physcard->adapter[i])
				break;
		}
		if (i == SLIC_MAX_PORTS)
			break;

		if (physcard->adapter[i]->slotnumber == adapter->slotnumber)
			break;
		physcard = physcard->next;
	}
	if (!physcard) {
		/* no structure allocated for this physical card yet */
		physcard = kzalloc(sizeof(*physcard), GFP_ATOMIC);
		if (!physcard) {
			if (card_hostid == SLIC_HOSTID_DEFAULT)
				kfree(card);
			return -ENOMEM;
		}

		physcard->next = slic_global.phys_card;
		slic_global.phys_card = physcard;
		physcard->adapters_allocd = 1;
	} else {
		physcard->adapters_allocd++;
	}
	/* Note - this is ZERO relative */
	adapter->physport = physcard->adapters_allocd - 1;

	physcard->adapter[adapter->physport] = adapter;
	adapter->physcard = physcard;

	return 0;
}

static int slic_entry_probe(struct pci_dev *pcidev,
			       const struct pci_device_id *pci_tbl_entry)
{
	static int cards_found;
	static int did_version;
	int err = -ENODEV;
	struct net_device *netdev;
	struct adapter *adapter;
	void __iomem *memmapped_ioaddr = NULL;
	ulong mmio_start = 0;
	ulong mmio_len = 0;
	struct sliccard *card = NULL;
	int pci_using_dac = 0;

	err = pci_enable_device(pcidev);

	if (err)
		return err;

	if (did_version++ == 0) {
		dev_info(&pcidev->dev, "%s\n", slic_banner);
		dev_info(&pcidev->dev, "%s\n", slic_proc_version);
	}

	if (!pci_set_dma_mask(pcidev, DMA_BIT_MASK(64))) {
		pci_using_dac = 1;
		err = pci_set_consistent_dma_mask(pcidev, DMA_BIT_MASK(64));
		if (err) {
			dev_err(&pcidev->dev, "unable to obtain 64-bit DMA for consistent allocations\n");
			goto err_out_disable_pci;
		}
	} else {
		err = pci_set_dma_mask(pcidev, DMA_BIT_MASK(32));
		if (err) {
			dev_err(&pcidev->dev, "no usable DMA configuration\n");
			goto err_out_disable_pci;
		}
		pci_using_dac = 0;
		pci_set_consistent_dma_mask(pcidev, DMA_BIT_MASK(32));
	}

	err = pci_request_regions(pcidev, DRV_NAME);
	if (err) {
		dev_err(&pcidev->dev, "can't obtain PCI resources\n");
		goto err_out_disable_pci;
	}

	pci_set_master(pcidev);

	netdev = alloc_etherdev(sizeof(struct adapter));
	if (!netdev) {
		err = -ENOMEM;
		goto err_out_exit_slic_probe;
	}

	netdev->ethtool_ops = &slic_ethtool_ops;
	SET_NETDEV_DEV(netdev, &pcidev->dev);

	pci_set_drvdata(pcidev, netdev);
	adapter = netdev_priv(netdev);
	adapter->netdev = netdev;
	adapter->pcidev = pcidev;
	slic_global.dynamic_intagg = adapter->dynamic_intagg;
	if (pci_using_dac)
		netdev->features |= NETIF_F_HIGHDMA;

	mmio_start = pci_resource_start(pcidev, 0);
	mmio_len = pci_resource_len(pcidev, 0);

	memmapped_ioaddr = ioremap_nocache(mmio_start, mmio_len);
	if (!memmapped_ioaddr) {
		dev_err(&pcidev->dev, "cannot remap MMIO region %lx @ %lx\n",
			mmio_len, mmio_start);
		err = -ENOMEM;
		goto err_out_free_netdev;
	}

	slic_config_pci(pcidev);

	slic_init_driver();

	err = slic_init_adapter(netdev, pcidev, pci_tbl_entry, memmapped_ioaddr,
				cards_found);
	if (err) {
		dev_err(&pcidev->dev, "failed to init adapter: %i\n", err);
		goto err_out_unmap;
	}

	err = slic_card_locate(adapter);
	if (err) {
		dev_err(&pcidev->dev, "cannot locate card\n");
		goto err_clean_init;
	}

	card = adapter->card;

	if (!adapter->allocated) {
		card->adapters_allocated++;
		adapter->allocated = 1;
	}

	err = slic_card_init(card, adapter);
	if (err)
		goto err_clean_init;

	slic_adapter_set_hwaddr(adapter);

	netdev->base_addr = (unsigned long)memmapped_ioaddr;
	netdev->irq = adapter->irq;
	netdev->netdev_ops = &slic_netdev_ops;

	netif_carrier_off(netdev);

	strcpy(netdev->name, "eth%d");
	err = register_netdev(netdev);
	if (err) {
		dev_err(&pcidev->dev, "Cannot register net device, aborting.\n");
		goto err_clean_init;
	}

	cards_found++;

	return 0;

err_clean_init:
	slic_init_cleanup(adapter);
err_out_unmap:
	iounmap(memmapped_ioaddr);
err_out_free_netdev:
	free_netdev(netdev);
err_out_exit_slic_probe:
	pci_release_regions(pcidev);
err_out_disable_pci:
	pci_disable_device(pcidev);
	return err;
}

static struct pci_driver slic_driver = {
	.name = DRV_NAME,
	.id_table = slic_pci_tbl,
	.probe = slic_entry_probe,
	.remove = slic_entry_remove,
};

static int __init slic_module_init(void)
{
	slic_init_driver();

	return pci_register_driver(&slic_driver);
}

static void __exit slic_module_cleanup(void)
{
	pci_unregister_driver(&slic_driver);
}

static struct ethtool_ops slic_ethtool_ops = {
	.get_coalesce = slic_get_coalesce,
	.set_coalesce = slic_set_coalesce
};

module_init(slic_module_init);
module_exit(slic_module_cleanup);
