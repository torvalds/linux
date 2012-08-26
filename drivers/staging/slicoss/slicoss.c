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
#define SLIC_OFFLOAD_IP_CHECKSUM		1
#define STATS_TIMER_INTERVAL			2
#define PING_TIMER_INTERVAL			    1

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
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/delay.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/moduleparam.h>

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
static char *slic_banner = "Alacritech SLIC Technology(tm) Server "\
		"and Storage Accelerator (Non-Accelerated)";

static char *slic_proc_version = "2.0.351  2006/07/14 12:26:00";
static char *slic_product_name = "SLIC Technology(tm) Server "\
		"and Storage Accelerator (Non-Accelerated)";
static char *slic_vendor = "Alacritech, Inc.";

static int slic_debug = 1;
static int debug = -1;
static struct net_device *head_netdevice;

static struct base_driver slic_global = { {}, 0, 0, 0, 1, NULL, NULL };
static int intagg_delay = 100;
static u32 dynamic_intagg;
static unsigned int rcv_count;
static struct dentry *slic_debugfs;

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

module_param(dynamic_intagg, int, 0);
MODULE_PARM_DESC(dynamic_intagg, "Dynamic Interrupt Aggregation Setting");
module_param(intagg_delay, int, 0);
MODULE_PARM_DESC(intagg_delay, "uSec Interrupt Aggregation Delay");

static DEFINE_PCI_DEVICE_TABLE(slic_pci_tbl) = {
	{ PCI_DEVICE(PCI_VENDOR_ID_ALACRITECH, SLIC_1GB_DEVICE_ID) },
	{ PCI_DEVICE(PCI_VENDOR_ID_ALACRITECH, SLIC_2GB_DEVICE_ID) },
	{ 0 }
};

MODULE_DEVICE_TABLE(pci, slic_pci_tbl);

#ifdef ASSERT
#undef ASSERT
#endif

static void slic_assert_fail(void)
{
	u32 cpuid;
	u32 curr_pid;
	cpuid = smp_processor_id();
	curr_pid = current->pid;

	printk(KERN_ERR "%s CPU # %d ---- PID # %d\n",
	       __func__, cpuid, curr_pid);
}

#ifndef ASSERT
#define ASSERT(a) do {							\
	if (!(a)) {							\
		printk(KERN_ERR "slicoss ASSERT() Failure: function %s"	\
			"line %d\n", __func__, __LINE__);		\
		slic_assert_fail();					\
	}								\
} while (0)
#endif


#define SLIC_GET_SLIC_HANDLE(_adapter, _pslic_handle)                   \
{                                                                       \
    spin_lock_irqsave(&_adapter->handle_lock.lock,                      \
			_adapter->handle_lock.flags);                   \
    _pslic_handle  =  _adapter->pfree_slic_handles;                     \
    if (_pslic_handle) {                                                \
	ASSERT(_pslic_handle->type == SLIC_HANDLE_FREE);                \
	_adapter->pfree_slic_handles = _pslic_handle->next;             \
    }                                                                   \
    spin_unlock_irqrestore(&_adapter->handle_lock.lock,                 \
			_adapter->handle_lock.flags);                   \
}

#define SLIC_FREE_SLIC_HANDLE(_adapter, _pslic_handle)                  \
{                                                                       \
    _pslic_handle->type = SLIC_HANDLE_FREE;                             \
    spin_lock_irqsave(&_adapter->handle_lock.lock,                      \
			_adapter->handle_lock.flags);                   \
    _pslic_handle->next = _adapter->pfree_slic_handles;                 \
    _adapter->pfree_slic_handles = _pslic_handle;                       \
    spin_unlock_irqrestore(&_adapter->handle_lock.lock,                 \
			_adapter->handle_lock.flags);                   \
}

static inline void slic_reg32_write(void __iomem *reg, u32 value, bool flush)
{
	writel(value, reg);
	if (flush)
		mb();
}

static inline void slic_reg64_write(struct adapter *adapter, void __iomem *reg,
				    u32 value, void __iomem *regh, u32 paddrh,
				    bool flush)
{
	spin_lock_irqsave(&adapter->bit64reglock.lock,
				adapter->bit64reglock.flags);
	if (paddrh != adapter->curaddrupper) {
		adapter->curaddrupper = paddrh;
		writel(paddrh, regh);
	}
	writel(value, reg);
	if (flush)
		mb();
	spin_unlock_irqrestore(&adapter->bit64reglock.lock,
				adapter->bit64reglock.flags);
}

/*
 * Functions to obtain the CRC corresponding to the destination mac address.
 * This is a standard ethernet CRC in that it is a 32-bit, reflected CRC using
 * the polynomial:
 *   x^32 + x^26 + x^23 + x^22 + x^16 + x^12 + x^11 + x^10 + x^8 + x^7 + x^5 +
 *   x^4 + x^2 + x^1.
 *
 * After the CRC for the 6 bytes is generated (but before the value is
 * complemented),
 * we must then transpose the value and return bits 30-23.
 *
 */
static u32 slic_crc_table[256];	/* Table of CRCs for all possible byte values */
static u32 slic_crc_init;	/* Is table initialized */

/*
 *  Contruct the CRC32 table
 */
static void slic_mcast_init_crc32(void)
{
	u32 c;		/*  CRC shit reg                 */
	u32 e = 0;		/*  Poly X-or pattern            */
	int i;			/*  counter                      */
	int k;			/*  byte being shifted into crc  */

	static int p[] = { 0, 1, 2, 4, 5, 7, 8, 10, 11, 12, 16, 22, 23, 26 };

	for (i = 0; i < ARRAY_SIZE(p); i++)
		e |= 1L << (31 - p[i]);

	for (i = 1; i < 256; i++) {
		c = i;
		for (k = 8; k; k--)
			c = c & 1 ? (c >> 1) ^ e : c >> 1;
		slic_crc_table[i] = c;
	}
}

/*
 *  Return the MAC hast as described above.
 */
static unsigned char slic_mcast_get_mac_hash(char *macaddr)
{
	u32 crc;
	char *p;
	int i;
	unsigned char machash = 0;

	if (!slic_crc_init) {
		slic_mcast_init_crc32();
		slic_crc_init = 1;
	}

	crc = 0xFFFFFFFF;	/* Preload shift register, per crc-32 spec */
	for (i = 0, p = macaddr; i < 6; ++p, ++i)
		crc = (crc >> 8) ^ slic_crc_table[(crc ^ *p) & 0xFF];

	/* Return bits 1-8, transposed */
	for (i = 1; i < 9; i++)
		machash |= (((crc >> i) & 1) << (8 - i));

	return machash;
}

static void slic_mcast_set_bit(struct adapter *adapter, char *address)
{
	unsigned char crcpoly;

	/* Get the CRC polynomial for the mac address */
	crcpoly = slic_mcast_get_mac_hash(address);

	/* We only have space on the SLIC for 64 entries.  Lop
	 * off the top two bits. (2^6 = 64)
	 */
	crcpoly &= 0x3F;

	/* OR in the new bit into our 64 bit mask. */
	adapter->mcastmask |= (u64) 1 << crcpoly;
}

static void slic_mcast_set_mask(struct adapter *adapter)
{
	__iomem struct slic_regs *slic_regs = adapter->slic_regs;

	if (adapter->macopts & (MAC_ALLMCAST | MAC_PROMISC)) {
		/* Turn on all multicast addresses. We have to do this for
		 * promiscuous mode as well as ALLMCAST mode.  It saves the
		 * Microcode from having to keep state about the MAC
		 * configuration.
		 */
		slic_reg32_write(&slic_regs->slic_mcastlow, 0xFFFFFFFF, FLUSH);
		slic_reg32_write(&slic_regs->slic_mcasthigh, 0xFFFFFFFF,
				 FLUSH);
	} else {
		/* Commit our multicast mast to the SLIC by writing to the
		 * multicast address mask registers
		 */
		slic_reg32_write(&slic_regs->slic_mcastlow,
			(u32)(adapter->mcastmask & 0xFFFFFFFF), FLUSH);
		slic_reg32_write(&slic_regs->slic_mcasthigh,
			(u32)((adapter->mcastmask >> 32) & 0xFFFFFFFF), FLUSH);
	}
}

static void slic_timer_ping(ulong dev)
{
	struct adapter *adapter;
	struct sliccard *card;

	ASSERT(dev);
	adapter = netdev_priv((struct net_device *)dev);
	ASSERT(adapter);
	card = adapter->card;
	ASSERT(card);

	adapter->pingtimer.expires = jiffies + (PING_TIMER_INTERVAL * HZ);
	add_timer(&adapter->pingtimer);
}

static void slic_unmap_mmio_space(struct adapter *adapter)
{
	if (adapter->slic_regs)
		iounmap(adapter->slic_regs);
	adapter->slic_regs = NULL;
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
	u32 __iomem *wphy;
	u32 speed;
	u32 duplex;
	u32 phy_config;
	u32 phy_advreg;
	u32 phy_gctlreg;

	if (adapter->state != ADAPT_UP)
		return;

	ASSERT((adapter->devid == SLIC_1GB_DEVICE_ID)
	       || (adapter->devid == SLIC_2GB_DEVICE_ID));

	if (linkspeed > LINK_1000MB)
		linkspeed = LINK_AUTOSPEED;
	if (linkduplex > LINK_AUTOD)
		linkduplex = LINK_AUTOD;

	wphy = &adapter->slic_regs->slic_wphy;

	if ((linkspeed == LINK_AUTOSPEED) || (linkspeed == LINK_1000MB)) {
		if (adapter->flags & ADAPT_FLAGS_FIBERMEDIA) {
			/*  We've got a fiber gigabit interface, and register
			 *  4 is different in fiber mode than in copper mode
			 */

			/* advertise FD only @1000 Mb */
			phy_advreg = (MIICR_REG_4 | (PAR_ADV1000XFD));
			/* enable PAUSE frames        */
			phy_advreg |= PAR_ASYMPAUSE_FIBER;
			slic_reg32_write(wphy, phy_advreg, FLUSH);

			if (linkspeed == LINK_AUTOSPEED) {
				/* reset phy, enable auto-neg  */
				phy_config =
				    (MIICR_REG_PCR |
				     (PCR_RESET | PCR_AUTONEG |
				      PCR_AUTONEG_RST));
				slic_reg32_write(wphy, phy_config, FLUSH);
			} else {	/* forced 1000 Mb FD*/
				/* power down phy to break link
				   this may not work) */
				phy_config = (MIICR_REG_PCR | PCR_POWERDOWN);
				slic_reg32_write(wphy, phy_config, FLUSH);
				/* wait, Marvell says 1 sec,
				   try to get away with 10 ms  */
				mdelay(10);

				/* disable auto-neg, set speed/duplex,
				   soft reset phy, powerup */
				phy_config =
				    (MIICR_REG_PCR |
				     (PCR_RESET | PCR_SPEED_1000 |
				      PCR_DUPLEX_FULL));
				slic_reg32_write(wphy, phy_config, FLUSH);
			}
		} else {	/* copper gigabit */

			/* Auto-Negotiate or 1000 Mb must be auto negotiated
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
			/* linkspeed == LINK_1000MB -
			   don't advertise 10/100 Mb modes  */
				phy_advreg = MIICR_REG_4;
			}
			/* enable PAUSE frames  */
			phy_advreg |= PAR_ASYMPAUSE;
			/* required by the Cicada PHY  */
			phy_advreg |= PAR_802_3;
			slic_reg32_write(wphy, phy_advreg, FLUSH);
			/* advertise FD only @1000 Mb  */
			phy_gctlreg = (MIICR_REG_9 | (PGC_ADV1000FD));
			slic_reg32_write(wphy, phy_gctlreg, FLUSH);

			if (adapter->subsysid != SLIC_1GB_CICADA_SUBSYS_ID) {
				/* if a Marvell PHY
				   enable auto crossover */
				phy_config =
				    (MIICR_REG_16 | (MRV_REG16_XOVERON));
				slic_reg32_write(wphy, phy_config, FLUSH);

				/* reset phy, enable auto-neg  */
				phy_config =
				    (MIICR_REG_PCR |
				     (PCR_RESET | PCR_AUTONEG |
				      PCR_AUTONEG_RST));
				slic_reg32_write(wphy, phy_config, FLUSH);
			} else {	/* it's a Cicada PHY  */
				/* enable and restart auto-neg (don't reset)  */
				phy_config =
				    (MIICR_REG_PCR |
				     (PCR_AUTONEG | PCR_AUTONEG_RST));
				slic_reg32_write(wphy, phy_config, FLUSH);
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
			/* if a Marvell PHY
			   disable auto crossover  */
			phy_config = (MIICR_REG_16 | (MRV_REG16_XOVEROFF));
			slic_reg32_write(wphy, phy_config, FLUSH);
		}

		/* power down phy to break link (this may not work)  */
		phy_config = (MIICR_REG_PCR | (PCR_POWERDOWN | speed | duplex));
		slic_reg32_write(wphy, phy_config, FLUSH);

		/* wait, Marvell says 1 sec, try to get away with 10 ms */
		mdelay(10);

		if (adapter->subsysid != SLIC_1GB_CICADA_SUBSYS_ID) {
			/* if a Marvell PHY
			   disable auto-neg, set speed,
			   soft reset phy, powerup */
			phy_config =
			    (MIICR_REG_PCR | (PCR_RESET | speed | duplex));
			slic_reg32_write(wphy, phy_config, FLUSH);
		} else {	/* it's a Cicada PHY  */
			/* disable auto-neg, set speed, powerup  */
			phy_config = (MIICR_REG_PCR | (speed | duplex));
			slic_reg32_write(wphy, phy_config, FLUSH);
		}
	}
}

static int slic_card_download_gbrcv(struct adapter *adapter)
{
	const struct firmware *fw;
	const char *file = "";
	int ret;
	__iomem struct slic_regs *slic_regs = adapter->slic_regs;
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
			"SLICOSS: Failed to load firmware %s\n", file);
		return ret;
	}

	rcvucodelen = *(u32 *)(fw->data + index);
	index += 4;
	switch (adapter->devid) {
	case SLIC_2GB_DEVICE_ID:
		if (rcvucodelen != OasisRcvUCodeLen)
			return -EINVAL;
		break;
	case SLIC_1GB_DEVICE_ID:
		if (rcvucodelen != GBRcvUCodeLen)
			return -EINVAL;
		break;
	default:
		ASSERT(0);
		break;
	}
	/* start download */
	slic_reg32_write(&slic_regs->slic_rcv_wcs, SLIC_RCVWCS_BEGIN, FLUSH);
	/* download the rcv sequencer ucode */
	for (codeaddr = 0; codeaddr < rcvucodelen; codeaddr++) {
		/* write out instruction address */
		slic_reg32_write(&slic_regs->slic_rcv_wcs, codeaddr, FLUSH);

		instruction = *(u32 *)(fw->data + index);
		index += 4;
		/* write out the instruction data low addr */
		slic_reg32_write(&slic_regs->slic_rcv_wcs, instruction, FLUSH);

		instruction = *(u8 *)(fw->data + index);
		index++;
		/* write out the instruction data high addr */
		slic_reg32_write(&slic_regs->slic_rcv_wcs, (u8)instruction,
				 FLUSH);
	}

	/* download finished */
	release_firmware(fw);
	slic_reg32_write(&slic_regs->slic_rcv_wcs, SLIC_RCVWCS_FINISH, FLUSH);
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
	__iomem struct slic_regs *slic_regs = adapter->slic_regs;
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
		ASSERT(0);
		break;
	}
	ret = request_firmware(&fw, file, &adapter->pcidev->dev);
	if (ret) {
		dev_err(&adapter->pcidev->dev,
			"SLICOSS: Failed to load firmware %s\n", file);
		return ret;
	}
	numsects = *(u32 *)(fw->data + index);
	index += 4;
	ASSERT(numsects <= 3);
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
			slic_reg32_write(&slic_regs->slic_wcs,
					 baseaddress + codeaddr, FLUSH);
			/* Write out instruction to low addr */
			slic_reg32_write(&slic_regs->slic_wcs, instruction, FLUSH);
			instruction = *(u32 *)(fw->data + index);
			index += 4;

			/* Write out instruction to high addr */
			slic_reg32_write(&slic_regs->slic_wcs, instruction, FLUSH);
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
			slic_reg32_write(&slic_regs->slic_wcs,
				SLIC_WCS_COMPARE | (baseaddress + codeaddr),
				FLUSH);
			/* Write out instruction to low addr */
			slic_reg32_write(&slic_regs->slic_wcs, instruction,
					 FLUSH);
			instruction = *(u32 *)(fw->data + index);
			index += 4;
			/* Write out instruction to high addr */
			slic_reg32_write(&slic_regs->slic_wcs, instruction,
					 FLUSH);
			instruction = *(u32 *)(fw->data + index);
			index += 4;

			/* Check SRAM location zero. If it is non-zero. Abort.*/
/*			failure = readl((u32 __iomem *)&slic_regs->slic_reset);
			if (failure) {
				release_firmware(fw);
				return -EIO;
			}*/
		}
	}
	release_firmware(fw);
	/* Everything OK, kick off the card */
	mdelay(10);
	slic_reg32_write(&slic_regs->slic_wcs, SLIC_WCS_START, FLUSH);

	/* stall for 20 ms, long enough for ucode to init card
	   and reach mainloop */
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
		if (!(adapter->currmacaddr[0] || adapter->currmacaddr[1] ||
		      adapter->currmacaddr[2] || adapter->currmacaddr[3] ||
		      adapter->currmacaddr[4] || adapter->currmacaddr[5])) {
			memcpy(adapter->currmacaddr, adapter->macaddr, 6);
		}
		if (adapter->netdev) {
			memcpy(adapter->netdev->dev_addr, adapter->currmacaddr,
			       6);
		}
	}
}

static void slic_intagg_set(struct adapter *adapter, u32 value)
{
	slic_reg32_write(&adapter->slic_regs->slic_intagg, value, FLUSH);
	adapter->card->loadlevel_current = value;
}

static void slic_soft_reset(struct adapter *adapter)
{
	if (adapter->card->state == CARD_UP) {
		slic_reg32_write(&adapter->slic_regs->slic_quiesce, 0, FLUSH);
		mdelay(1);
	}

	slic_reg32_write(&adapter->slic_regs->slic_reset, SLIC_RESET_MAGIC,
			 FLUSH);
	mdelay(1);
}

static void slic_mac_address_config(struct adapter *adapter)
{
	u32 value;
	u32 value2;
	__iomem struct slic_regs *slic_regs = adapter->slic_regs;

	value = *(u32 *) &adapter->currmacaddr[2];
	value = ntohl(value);
	slic_reg32_write(&slic_regs->slic_wraddral, value, FLUSH);
	slic_reg32_write(&slic_regs->slic_wraddrbl, value, FLUSH);

	value2 = (u32) ((adapter->currmacaddr[0] << 8 |
			     adapter->currmacaddr[1]) & 0xFFFF);

	slic_reg32_write(&slic_regs->slic_wraddrah, value2, FLUSH);
	slic_reg32_write(&slic_regs->slic_wraddrbh, value2, FLUSH);

	/* Write our multicast mask out to the card.  This is done */
	/* here in addition to the slic_mcast_addr_set routine     */
	/* because ALL_MCAST may have been enabled or disabled     */
	slic_mcast_set_mask(adapter);
}

static void slic_mac_config(struct adapter *adapter)
{
	u32 value;
	__iomem struct slic_regs *slic_regs = adapter->slic_regs;

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
	slic_reg32_write(&slic_regs->slic_wmcfg, value, FLUSH);

	/* setup mac addresses */
	slic_mac_address_config(adapter);
}

static void slic_config_set(struct adapter *adapter, bool linkchange)
{
	u32 value;
	u32 RcrReset;
	__iomem struct slic_regs *slic_regs = adapter->slic_regs;

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

		slic_reg32_write(&slic_regs->slic_wxcfg, value, FLUSH);

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

		slic_reg32_write(&slic_regs->slic_wxcfg, value, FLUSH);

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

	slic_reg32_write(&slic_regs->slic_wrcfg, value, FLUSH);
}

/*
 *  Turn off RCV and XMT, power down PHY
 */
static void slic_config_clear(struct adapter *adapter)
{
	u32 value;
	u32 phy_config;
	__iomem struct slic_regs *slic_regs = adapter->slic_regs;

	/* Setup xmtcfg */
	value = (GXCR_RESET |	/* Always reset */
		 GXCR_PAUSEEN);	/* Enable pause */

	slic_reg32_write(&slic_regs->slic_wxcfg, value, FLUSH);

	value = (GRCR_RESET |	/* Always reset      */
		 GRCR_CTLEN |	/* Enable CTL frames */
		 GRCR_ADDRAEN |	/* Address A enable  */
		 (GRCR_HASHSIZE << GRCR_HASHSIZE_SHIFT));

	slic_reg32_write(&slic_regs->slic_wrcfg, value, FLUSH);

	/* power down phy */
	phy_config = (MIICR_REG_PCR | (PCR_POWERDOWN));
	slic_reg32_write(&slic_regs->slic_wphy, phy_config, FLUSH);
}

static bool slic_mac_filter(struct adapter *adapter,
			struct ether_header *ether_frame)
{
	struct net_device *netdev = adapter->netdev;
	u32 opts = adapter->macopts;
	u32 *dhost4 = (u32 *)&ether_frame->ether_dhost[0];
	u16 *dhost2 = (u16 *)&ether_frame->ether_dhost[4];

	if (opts & MAC_PROMISC)
		return true;

	if ((*dhost4 == 0xFFFFFFFF) && (*dhost2 == 0xFFFF)) {
		if (opts & MAC_BCAST) {
			adapter->rcv_broadcasts++;
			return true;
		} else {
			return false;
		}
	}

	if (ether_frame->ether_dhost[0] & 0x01) {
		if (opts & MAC_ALLMCAST) {
			adapter->rcv_multicasts++;
			netdev->stats.multicast++;
			return true;
		}
		if (opts & MAC_MCAST) {
			struct mcast_address *mcaddr = adapter->mcastaddrs;

			while (mcaddr) {
				if (!compare_ether_addr(mcaddr->address,
							ether_frame->ether_dhost)) {
					adapter->rcv_multicasts++;
					netdev->stats.multicast++;
					return true;
				}
				mcaddr = mcaddr->next;
			}
			return false;
		} else {
			return false;
		}
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
	u32 __iomem *intagg;
	u32 load = card->events;
	u32 level = 0;

	intagg = &adapter->slic_regs->slic_intagg;

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
				slic_reg32_write(intagg, level, FLUSH);
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
				slic_reg32_write(intagg, level, FLUSH);
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

	upr = kmalloc(sizeof(struct slic_upr), GFP_ATOMIC);
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
	__iomem struct slic_regs *slic_regs = adapter->slic_regs;
/*
    char * ptr1;
    char * ptr2;
    uint cmdoffset;
*/
	upr = adapter->upr_list;
	if (!upr)
		return;
	if (adapter->upr_busy)
		return;
	adapter->upr_busy = 1;

	switch (upr->upr_request) {
	case SLIC_UPR_STATS:
		if (upr->upr_data_h == 0) {
			slic_reg32_write(&slic_regs->slic_stats, upr->upr_data,
					 FLUSH);
		} else {
			slic_reg64_write(adapter, &slic_regs->slic_stats64,
					 upr->upr_data,
					 &slic_regs->slic_addr_upper,
					 upr->upr_data_h, FLUSH);
		}
		break;

	case SLIC_UPR_RLSR:
		slic_reg64_write(adapter, &slic_regs->slic_rlsr, upr->upr_data,
				 &slic_regs->slic_addr_upper, upr->upr_data_h,
				 FLUSH);
		break;

	case SLIC_UPR_RCONFIG:
		slic_reg64_write(adapter, &slic_regs->slic_rconfig,
				 upr->upr_data, &slic_regs->slic_addr_upper,
				 upr->upr_data_h, FLUSH);
		break;
	case SLIC_UPR_PING:
		slic_reg32_write(&slic_regs->slic_ping, 1, FLUSH);
		break;
	default:
		ASSERT(0);
	}
}

static int slic_upr_request(struct adapter *adapter,
		     u32 upr_request,
		     u32 upr_data,
		     u32 upr_data_h,
		     u32 upr_buffer, u32 upr_buffer_h)
{
	int rc;

	spin_lock_irqsave(&adapter->upr_lock.lock, adapter->upr_lock.flags);
	rc = slic_upr_queue_request(adapter,
					upr_request,
					upr_data,
					upr_data_h, upr_buffer, upr_buffer_h);
	if (rc)
		goto err_unlock_irq;

	slic_upr_start(adapter);
err_unlock_irq:
	spin_unlock_irqrestore(&adapter->upr_lock.lock,
				adapter->upr_lock.flags);
	return rc;
}

static void slic_link_upr_complete(struct adapter *adapter, u32 isr)
{
	u32 linkstatus = adapter->pshmem->linkstatus;
	uint linkup;
	unsigned char linkspeed;
	unsigned char linkduplex;

	if ((isr & ISR_UPCERR) || (isr & ISR_UPCBSY)) {
		struct slic_shmem *pshmem;

		pshmem = (struct slic_shmem *)adapter->phys_shmem;
#if BITS_PER_LONG == 64
		slic_upr_queue_request(adapter,
				       SLIC_UPR_RLSR,
				       SLIC_GET_ADDR_LOW(&pshmem->linkstatus),
				       SLIC_GET_ADDR_HIGH(&pshmem->linkstatus),
				       0, 0);
#else
		slic_upr_queue_request(adapter,
				       SLIC_UPR_RLSR,
				       (u32) &pshmem->linkstatus,
				       SLIC_GET_ADDR_HIGH(pshmem), 0, 0);
#endif
		return;
	}
	if (adapter->state != ADAPT_UP)
		return;

	ASSERT((adapter->devid == SLIC_1GB_DEVICE_ID)
	       || (adapter->devid == SLIC_2GB_DEVICE_ID));

	linkup = linkstatus & GIG_LINKUP ? LINK_UP : LINK_DOWN;
	if (linkstatus & GIG_SPEED_1000)
		linkspeed = LINK_1000MB;
	else if (linkstatus & GIG_SPEED_100)
		linkspeed = LINK_100MB;
	else
		linkspeed = LINK_10MB;

	if (linkstatus & GIG_FULLDUPLEX)
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
		return;
	}

	/* link has gone from down to up */
	adapter->linkspeed = linkspeed;
	adapter->linkduplex = linkduplex;

	if (adapter->linkstate != LINK_UP) {
		/* setup the mac */
		slic_config_set(adapter, true);
		adapter->linkstate = LINK_UP;
		netif_start_queue(adapter->netdev);
	}
}

static void slic_upr_request_complete(struct adapter *adapter, u32 isr)
{
	struct sliccard *card = adapter->card;
	struct slic_upr *upr;

	spin_lock_irqsave(&adapter->upr_lock.lock, adapter->upr_lock.flags);
	upr = adapter->upr_list;
	if (!upr) {
		ASSERT(0);
		spin_unlock_irqrestore(&adapter->upr_lock.lock,
					adapter->upr_lock.flags);
		return;
	}
	adapter->upr_list = upr->next;
	upr->next = NULL;
	adapter->upr_busy = 0;
	ASSERT(adapter->port == upr->adapter);
	switch (upr->upr_request) {
	case SLIC_UPR_STATS:
		{
			struct slic_stats *slicstats =
			    (struct slic_stats *) &adapter->pshmem->inicstats;
			struct slic_stats *newstats = slicstats;
			struct slic_stats  *old = &adapter->inicstats_prev;
			struct slicnet_stats *stst = &adapter->slic_stats;

			if (isr & ISR_UPCERR) {
				dev_err(&adapter->netdev->dev,
					"SLIC_UPR_STATS command failed isr[%x]\n",
					isr);

				break;
			}
			UPDATE_STATS_GB(stst->tcp.xmit_tcp_segs,
					newstats->xmit_tcp_segs_gb,
					old->xmit_tcp_segs_gb);

			UPDATE_STATS_GB(stst->tcp.xmit_tcp_bytes,
					newstats->xmit_tcp_bytes_gb,
					old->xmit_tcp_bytes_gb);

			UPDATE_STATS_GB(stst->tcp.rcv_tcp_segs,
					newstats->rcv_tcp_segs_gb,
					old->rcv_tcp_segs_gb);

			UPDATE_STATS_GB(stst->tcp.rcv_tcp_bytes,
					newstats->rcv_tcp_bytes_gb,
					old->rcv_tcp_bytes_gb);

			UPDATE_STATS_GB(stst->iface.xmt_bytes,
					newstats->xmit_bytes_gb,
					old->xmit_bytes_gb);

			UPDATE_STATS_GB(stst->iface.xmt_ucast,
					newstats->xmit_unicasts_gb,
					old->xmit_unicasts_gb);

			UPDATE_STATS_GB(stst->iface.rcv_bytes,
					newstats->rcv_bytes_gb,
					old->rcv_bytes_gb);

			UPDATE_STATS_GB(stst->iface.rcv_ucast,
					newstats->rcv_unicasts_gb,
					old->rcv_unicasts_gb);

			UPDATE_STATS_GB(stst->iface.xmt_errors,
					newstats->xmit_collisions_gb,
					old->xmit_collisions_gb);

			UPDATE_STATS_GB(stst->iface.xmt_errors,
					newstats->xmit_excess_collisions_gb,
					old->xmit_excess_collisions_gb);

			UPDATE_STATS_GB(stst->iface.xmt_errors,
					newstats->xmit_other_error_gb,
					old->xmit_other_error_gb);

			UPDATE_STATS_GB(stst->iface.rcv_errors,
					newstats->rcv_other_error_gb,
					old->rcv_other_error_gb);

			UPDATE_STATS_GB(stst->iface.rcv_discards,
					newstats->rcv_drops_gb,
					old->rcv_drops_gb);

			if (newstats->rcv_drops_gb > old->rcv_drops_gb) {
				adapter->rcv_drops +=
				    (newstats->rcv_drops_gb -
				     old->rcv_drops_gb);
			}
			memcpy(old, newstats, sizeof(struct slic_stats));
			break;
		}
	case SLIC_UPR_RLSR:
		slic_link_upr_complete(adapter, isr);
		break;
	case SLIC_UPR_RCONFIG:
		break;
	case SLIC_UPR_RPHY:
		ASSERT(0);
		break;
	case SLIC_UPR_ENLB:
		ASSERT(0);
		break;
	case SLIC_UPR_ENCT:
		ASSERT(0);
		break;
	case SLIC_UPR_PDWN:
		ASSERT(0);
		break;
	case SLIC_UPR_PING:
		card->pingstatus |= (isr & ISR_PINGDSMASK);
		break;
	default:
		ASSERT(0);
	}
	kfree(upr);
	slic_upr_start(adapter);
	spin_unlock_irqrestore(&adapter->upr_lock.lock,
				adapter->upr_lock.flags);
}

static void slic_config_get(struct adapter *adapter, u32 config,
							u32 config_h)
{
	int status;

	status = slic_upr_request(adapter,
				  SLIC_UPR_RCONFIG,
				  (u32) config, (u32) config_h, 0, 0);
	ASSERT(status == 0);
}

/*
 *  this is here to checksum the eeprom, there is some ucode bug
 *  which prevens us from using the ucode result.
 *  remove this once ucode is fixed.
 */
static ushort slic_eeprom_cksum(char *m, int len)
{
#define ADDCARRY(x)  (x > 65535 ? x -= 65535 : x)
#define REDUCE {l_util.l = sum; sum = l_util.s[0] + l_util.s[1]; ADDCARRY(sum);\
		}

	u16 *w;
	u32 sum = 0;
	u32 byte_swapped = 0;
	u32 w_int;

	union {
		char c[2];
		ushort s;
	} s_util;

	union {
		ushort s[2];
		int l;
	} l_util;

	l_util.l = 0;
	s_util.s = 0;

	w = (u16 *)m;
#if BITS_PER_LONG == 64
	w_int = (u32) ((ulong) w & 0x00000000FFFFFFFF);
#else
	w_int = (u32) (w);
#endif
	if ((1 & w_int) && (len > 0)) {
		REDUCE;
		sum <<= 8;
		s_util.c[0] = *(unsigned char *)w;
		w = (u16 *)((char *)w + 1);
		len--;
		byte_swapped = 1;
	}

	/* Unroll the loop to make overhead from branches &c small. */
	while ((len -= 32) >= 0) {
		sum += w[0];
		sum += w[1];
		sum += w[2];
		sum += w[3];
		sum += w[4];
		sum += w[5];
		sum += w[6];
		sum += w[7];
		sum += w[8];
		sum += w[9];
		sum += w[10];
		sum += w[11];
		sum += w[12];
		sum += w[13];
		sum += w[14];
		sum += w[15];
		w = (u16 *)((ulong) w + 16);	/* verify */
	}
	len += 32;
	while ((len -= 8) >= 0) {
		sum += w[0];
		sum += w[1];
		sum += w[2];
		sum += w[3];
		w = (u16 *)((ulong) w + 4);	/* verify */
	}
	len += 8;
	if (len != 0 || byte_swapped != 0) {
		REDUCE;
		while ((len -= 2) >= 0)
			sum += *w++;	/* verify */
		if (byte_swapped) {
			REDUCE;
			sum <<= 8;
			byte_swapped = 0;
			if (len == -1) {
				s_util.c[1] = *(char *) w;
				sum += s_util.s;
				len = 0;
			} else {
				len = -1;
			}

		} else if (len == -1) {
			s_util.c[0] = *(char *) w;
		}

		if (len == -1) {
			s_util.c[1] = 0;
			sum += s_util.s;
		}
	}
	REDUCE;
	return (ushort) sum;
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
	__iomem struct slic_regs *slic_regs = adapter->slic_regs;
	u32 paddrh = 0;

	ASSERT(adapter->state == ADAPT_DOWN);
	memset(rspq, 0, sizeof(struct slic_rspqueue));

	rspq->num_pages = SLIC_RSPQ_PAGES_GB;

	for (i = 0; i < rspq->num_pages; i++) {
		rspq->vaddr[i] = pci_alloc_consistent(adapter->pcidev,
						      PAGE_SIZE,
						      &rspq->paddr[i]);
		if (!rspq->vaddr[i]) {
			dev_err(&adapter->pcidev->dev,
				"pci_alloc_consistent failed\n");
			slic_rspqueue_free(adapter);
			return -ENOMEM;
		}
		/* FIXME:
		 * do we really need this assertions (4K PAGE_SIZE aligned addr)? */
#if 0
#ifndef CONFIG_X86_64
		ASSERT(((u32) rspq->vaddr[i] & 0xFFFFF000) ==
		       (u32) rspq->vaddr[i]);
		ASSERT(((u32) rspq->paddr[i] & 0xFFFFF000) ==
		       (u32) rspq->paddr[i]);
#endif
#endif
		memset(rspq->vaddr[i], 0, PAGE_SIZE);

		if (paddrh == 0) {
			slic_reg32_write(&slic_regs->slic_rbar,
				(rspq->paddr[i] | SLIC_RSPQ_BUFSINPAGE),
				DONT_FLUSH);
		} else {
			slic_reg64_write(adapter, &slic_regs->slic_rbar64,
				(rspq->paddr[i] | SLIC_RSPQ_BUFSINPAGE),
				&slic_regs->slic_addr_upper,
				paddrh, DONT_FLUSH);
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
#if BITS_PER_LONG == 32
	ASSERT((buf->status & 0xFFFFFFE0) == 0);
#endif
	ASSERT(buf->hosthandle);
	if (++rspq->offset < SLIC_RSPQ_BUFSINPAGE) {
		rspq->rspbuf++;
#if BITS_PER_LONG == 32
		ASSERT(((u32) rspq->rspbuf & 0xFFFFFFE0) ==
		       (u32) rspq->rspbuf);
#endif
	} else {
		ASSERT(rspq->offset == SLIC_RSPQ_BUFSINPAGE);
		slic_reg64_write(adapter, &adapter->slic_regs->slic_rbar64,
			(rspq->paddr[rspq->pageindex] | SLIC_RSPQ_BUFSINPAGE),
			&adapter->slic_regs->slic_addr_upper, 0, DONT_FLUSH);
		rspq->pageindex = (++rspq->pageindex) % rspq->num_pages;
		rspq->offset = 0;
		rspq->rspbuf = (struct slic_rspbuf *)
						rspq->vaddr[rspq->pageindex];
#if BITS_PER_LONG == 32
		ASSERT(((u32) rspq->rspbuf & 0xFFFFF000) ==
		       (u32) rspq->rspbuf);
#endif
	}
#if BITS_PER_LONG == 32
	ASSERT(((u32) buf & 0xFFFFFFE0) == (u32) buf);
#endif
	return buf;
}

static void slic_cmdqmem_init(struct adapter *adapter)
{
	struct slic_cmdqmem *cmdqmem = &adapter->cmdqmem;

	memset(cmdqmem, 0, sizeof(struct slic_cmdqmem));
}

static void slic_cmdqmem_free(struct adapter *adapter)
{
	struct slic_cmdqmem *cmdqmem = &adapter->cmdqmem;
	int i;

	for (i = 0; i < SLIC_CMDQ_MAXPAGES; i++) {
		if (cmdqmem->pages[i]) {
			pci_free_consistent(adapter->pcidev,
					    PAGE_SIZE,
					    (void *) cmdqmem->pages[i],
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
#if BITS_PER_LONG == 32
	ASSERT(((u32) pageaddr & 0xFFFFF000) == (u32) pageaddr);
#endif
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

	cmdaddr = page;
	cmd = (struct slic_hostcmd *)cmdaddr;
	cmdcnt = 0;

	phys_addr = virt_to_bus((void *)page);
	phys_addrl = SLIC_GET_ADDR_LOW(phys_addr);
	phys_addrh = SLIC_GET_ADDR_HIGH(phys_addr);

	prev = NULL;
	tail = cmd;
	while ((cmdcnt < SLIC_CMDQ_CMDSINPAGE) &&
	       (adapter->slic_handle_ix < 256)) {
		/* Allocate and initialize a SLIC_HANDLE for this command */
		SLIC_GET_SLIC_HANDLE(adapter, pslic_handle);
		if (pslic_handle == NULL)
			ASSERT(0);
		ASSERT(pslic_handle ==
		       &adapter->slic_handles[pslic_handle->token.
					      handle_index]);
		pslic_handle->type = SLIC_HANDLE_CMD;
		pslic_handle->address = (void *) cmd;
		pslic_handle->offset = (ushort) adapter->slic_handle_ix++;
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

		cmd = (struct slic_hostcmd *)cmdaddr;
		cmdcnt++;
	}

	cmdq = &adapter->cmdq_all;
	cmdq->count += cmdcnt;	/*  SLIC_CMDQ_CMDSINPAGE;   mooktodo */
	tail->next_all = cmdq->head;
	cmdq->head = prev;
	cmdq = &adapter->cmdq_free;
	spin_lock_irqsave(&cmdq->lock.lock, cmdq->lock.flags);
	cmdq->count += cmdcnt;	/*  SLIC_CMDQ_CMDSINPAGE;   mooktodo */
	tail->next = cmdq->head;
	cmdq->head = prev;
	spin_unlock_irqrestore(&cmdq->lock.lock, cmdq->lock.flags);
}

static int slic_cmdq_init(struct adapter *adapter)
{
	int i;
	u32 *pageaddr;

	ASSERT(adapter->state == ADAPT_DOWN);
	memset(&adapter->cmdq_all, 0, sizeof(struct slic_cmdqueue));
	memset(&adapter->cmdq_free, 0, sizeof(struct slic_cmdqueue));
	memset(&adapter->cmdq_done, 0, sizeof(struct slic_cmdqueue));
	spin_lock_init(&adapter->cmdq_all.lock.lock);
	spin_lock_init(&adapter->cmdq_free.lock.lock);
	spin_lock_init(&adapter->cmdq_done.lock.lock);
	slic_cmdqmem_init(adapter);
	adapter->slic_handle_ix = 1;
	for (i = 0; i < SLIC_CMDQ_INITPAGES; i++) {
		pageaddr = slic_cmdqmem_addpage(adapter);
#if BITS_PER_LONG == 32
		ASSERT(((u32) pageaddr & 0xFFFFF000) == (u32) pageaddr);
#endif
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

	spin_lock_irqsave(&adapter->cmdq_free.lock.lock,
			adapter->cmdq_free.lock.flags);
	spin_lock_irqsave(&adapter->cmdq_done.lock.lock,
			adapter->cmdq_done.lock.flags);
	outstanding = adapter->cmdq_all.count - adapter->cmdq_done.count;
	outstanding -= adapter->cmdq_free.count;
	hcmd = adapter->cmdq_all.head;
	while (hcmd) {
		if (hcmd->busy) {
			skb = hcmd->skb;
			ASSERT(skb);
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
	spin_unlock_irqrestore(&adapter->cmdq_done.lock.lock,
				adapter->cmdq_done.lock.flags);
	spin_unlock_irqrestore(&adapter->cmdq_free.lock.lock,
				adapter->cmdq_free.lock.flags);
}

static void slic_cmdq_getdone(struct adapter *adapter)
{
	struct slic_cmdqueue *done_cmdq = &adapter->cmdq_done;
	struct slic_cmdqueue *free_cmdq = &adapter->cmdq_free;

	ASSERT(free_cmdq->head == NULL);
	spin_lock_irqsave(&done_cmdq->lock.lock, done_cmdq->lock.flags);

	free_cmdq->head = done_cmdq->head;
	free_cmdq->count = done_cmdq->count;
	done_cmdq->head = NULL;
	done_cmdq->tail = NULL;
	done_cmdq->count = 0;
	spin_unlock_irqrestore(&done_cmdq->lock.lock, done_cmdq->lock.flags);
}

static struct slic_hostcmd *slic_cmdq_getfree(struct adapter *adapter)
{
	struct slic_cmdqueue *cmdq = &adapter->cmdq_free;
	struct slic_hostcmd *cmd = NULL;

lock_and_retry:
	spin_lock_irqsave(&cmdq->lock.lock, cmdq->lock.flags);
retry:
	cmd = cmdq->head;
	if (cmd) {
		cmdq->head = cmd->next;
		cmdq->count--;
		spin_unlock_irqrestore(&cmdq->lock.lock, cmdq->lock.flags);
	} else {
		slic_cmdq_getdone(adapter);
		cmd = cmdq->head;
		if (cmd) {
			goto retry;
		} else {
			u32 *pageaddr;

			spin_unlock_irqrestore(&cmdq->lock.lock,
						cmdq->lock.flags);
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

	spin_lock(&cmdq->lock.lock);
	cmd->busy = 0;
	cmd->next = cmdq->head;
	cmdq->head = cmd;
	cmdq->count++;
	if ((adapter->xmitq_full) && (cmdq->count > 10))
		netif_wake_queue(adapter->netdev);
	spin_unlock(&cmdq->lock.lock);
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
			paddr = (void *)pci_map_single(adapter->pcidev,
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
				dev_err(dev, "         skbdata[%p]\n", skb->data);
				dev_err(dev, "         skblen[%x]\n", skb->len);
				dev_err(dev, "         paddr[%p]\n", paddr);
				dev_err(dev, "         paddrl[%x]\n", paddrl);
				dev_err(dev, "         paddrh[%x]\n", paddrh);
				dev_err(dev, "         rcvq->head[%p]\n", rcvq->head);
				dev_err(dev, "         rcvq->tail[%p]\n", rcvq->tail);
				dev_err(dev, "         rcvq->count[%x]\n", rcvq->count);
				dev_err(dev, "SKIP THIS SKB!!!!!!!!\n");
				goto retry_rcvqfill;
			}
#else
			if (paddrl == 0) {
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
				dev_err(dev, "GIVE TO CARD ANYWAY\n");
			}
#endif
			if (paddrh == 0) {
				slic_reg32_write(&adapter->slic_regs->slic_hbar,
						 (u32)paddrl, DONT_FLUSH);
			} else {
				slic_reg64_write(adapter,
					&adapter->slic_regs->slic_hbar64,
					paddrl,
					&adapter->slic_regs->slic_addr_upper,
					paddrh, DONT_FLUSH);
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

	ASSERT(adapter->state == ADAPT_DOWN);
	rcvq->tail = NULL;
	rcvq->head = NULL;
	rcvq->size = SLIC_RCVQ_ENTRIES;
	rcvq->errors = 0;
	rcvq->count = 0;
	i = (SLIC_RCVQ_ENTRIES / SLIC_RCVQ_FILLENTRIES);
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
		ASSERT(rcvbuf);

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

	ASSERT(skb->len == SLIC_RCVBUF_HEADSIZE);

	paddr = (void *)pci_map_single(adapter->pcidev, skb->head,
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
		slic_reg32_write(&adapter->slic_regs->slic_hbar, (u32)paddrl,
				 DONT_FLUSH);
	} else {
		slic_reg64_write(adapter, &adapter->slic_regs->slic_hbar64,
				 paddrl, &adapter->slic_regs->slic_addr_upper,
				 paddrh, DONT_FLUSH);
	}
	if (rcvq->head)
		rcvq->tail->next = skb;
	else
		rcvq->head = skb;
	rcvq->tail = skb;
	rcvq->count++;
	return rcvq->count;
}

static int slic_debug_card_show(struct seq_file *seq, void *v)
{
#ifdef MOOKTODO
	int i;
	struct sliccard *card = seq->private;
	struct slic_config *config = &card->config;
	unsigned char *fru = (unsigned char *)(&card->config.atk_fru);
	unsigned char *oemfru = (unsigned char *)(&card->config.OemFru);
#endif

	seq_printf(seq, "driver_version           : %s\n", slic_proc_version);
	seq_printf(seq, "Microcode versions:           \n");
	seq_printf(seq, "    Gigabit (gb)         : %s %s\n",
		    MOJAVE_UCODE_VERS_STRING, MOJAVE_UCODE_VERS_DATE);
	seq_printf(seq, "    Gigabit Receiver     : %s %s\n",
		    GB_RCVUCODE_VERS_STRING, GB_RCVUCODE_VERS_DATE);
	seq_printf(seq, "Vendor                   : %s\n", slic_vendor);
	seq_printf(seq, "Product Name             : %s\n", slic_product_name);
#ifdef MOOKTODO
	seq_printf(seq, "VendorId                 : %4.4X\n",
		    config->VendorId);
	seq_printf(seq, "DeviceId                 : %4.4X\n",
		    config->DeviceId);
	seq_printf(seq, "RevisionId               : %2.2x\n",
		    config->RevisionId);
	seq_printf(seq, "Bus    #                 : %d\n", card->busnumber);
	seq_printf(seq, "Device #                 : %d\n", card->slotnumber);
	seq_printf(seq, "Interfaces               : %d\n", card->card_size);
	seq_printf(seq, "     Initialized         : %d\n",
		    card->adapters_activated);
	seq_printf(seq, "     Allocated           : %d\n",
		    card->adapters_allocated);
	ASSERT(card->card_size <= SLIC_NBR_MACS);
	for (i = 0; i < card->card_size; i++) {
		seq_printf(seq,
			   "     MAC%d : %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X\n",
			   i, config->macinfo[i].macaddrA[0],
			   config->macinfo[i].macaddrA[1],
			   config->macinfo[i].macaddrA[2],
			   config->macinfo[i].macaddrA[3],
			   config->macinfo[i].macaddrA[4],
			   config->macinfo[i].macaddrA[5]);
	}
	seq_printf(seq, "     IF  Init State Duplex/Speed irq\n");
	seq_printf(seq, "     -------------------------------\n");
	for (i = 0; i < card->adapters_allocated; i++) {
		struct adapter *adapter;

		adapter = card->adapter[i];
		if (adapter) {
			seq_printf(seq,
				    "     %d   %d   %s  %s  %s    0x%X\n",
				    adapter->physport, adapter->state,
				    SLIC_LINKSTATE(adapter->linkstate),
				    SLIC_DUPLEX(adapter->linkduplex),
				    SLIC_SPEED(adapter->linkspeed),
				    (uint) adapter->irq);
		}
	}
	seq_printf(seq, "Generation #             : %4.4X\n", card->gennumber);
	seq_printf(seq, "RcvQ max entries         : %4.4X\n",
		    SLIC_RCVQ_ENTRIES);
	seq_printf(seq, "Ping Status              : %8.8X\n",
		    card->pingstatus);
	seq_printf(seq, "Minimum grant            : %2.2x\n",
		    config->MinGrant);
	seq_printf(seq, "Maximum Latency          : %2.2x\n", config->MaxLat);
	seq_printf(seq, "PciStatus                : %4.4x\n",
		    config->Pcistatus);
	seq_printf(seq, "Debug Device Id          : %4.4x\n",
		    config->DbgDevId);
	seq_printf(seq, "DRAM ROM Function        : %4.4x\n",
		    config->DramRomFn);
	seq_printf(seq, "Network interface Pin 1  : %2.2x\n",
		    config->NetIntPin1);
	seq_printf(seq, "Network interface Pin 2  : %2.2x\n",
		    config->NetIntPin1);
	seq_printf(seq, "Network interface Pin 3  : %2.2x\n",
		    config->NetIntPin1);
	seq_printf(seq, "PM capabilities          : %4.4X\n",
		    config->PMECapab);
	seq_printf(seq, "Network Clock Controls   : %4.4X\n",
		    config->NwClkCtrls);

	switch (config->FruFormat) {
	case ATK_FRU_FORMAT:
		{
			seq_printf(seq,
			    "Vendor                   : Alacritech, Inc.\n");
			seq_printf(seq,
			    "Assembly #               : %c%c%c%c%c%c\n",
				    fru[0], fru[1], fru[2], fru[3], fru[4],
				    fru[5]);
			seq_printf(seq,
				    "Revision #               : %c%c\n",
				    fru[6], fru[7]);

			if (config->OEMFruFormat == VENDOR4_FRU_FORMAT) {
				seq_printf(seq,
					    "Serial   #               : "
					    "%c%c%c%c%c%c%c%c%c%c%c%c\n",
					    fru[8], fru[9], fru[10],
					    fru[11], fru[12], fru[13],
					    fru[16], fru[17], fru[18],
					    fru[19], fru[20], fru[21]);
			} else {
				seq_printf(seq,
					    "Serial   #               : "
					    "%c%c%c%c%c%c%c%c%c%c%c%c%c%c\n",
					    fru[8], fru[9], fru[10],
					    fru[11], fru[12], fru[13],
					    fru[14], fru[15], fru[16],
					    fru[17], fru[18], fru[19],
					    fru[20], fru[21]);
			}
			break;
		}

	default:
		{
			seq_printf(seq,
			    "Vendor                   : Alacritech, Inc.\n");
			seq_printf(seq,
			    "Serial   #               : Empty FRU\n");
			break;
		}
	}

	switch (config->OEMFruFormat) {
	case VENDOR1_FRU_FORMAT:
		{
			seq_printf(seq, "FRU Information:\n");
			seq_printf(seq, "    Commodity #          : %c\n",
				    oemfru[0]);
			seq_printf(seq,
				    "    Assembly #           : %c%c%c%c\n",
				    oemfru[1], oemfru[2], oemfru[3], oemfru[4]);
			seq_printf(seq,
				    "    Revision #           : %c%c\n",
				    oemfru[5], oemfru[6]);
			seq_printf(seq,
				    "    Supplier #           : %c%c\n",
				    oemfru[7], oemfru[8]);
			seq_printf(seq,
				    "    Date                 : %c%c\n",
				    oemfru[9], oemfru[10]);
			seq_sprintf(seq,
				    "    Sequence #           : %c%c%c\n",
				    oemfru[11], oemfru[12], oemfru[13]);
			break;
		}

	case VENDOR2_FRU_FORMAT:
		{
			seq_printf(seq, "FRU Information:\n");
			seq_printf(seq,
				    "    Part     #           : "
				    "%c%c%c%c%c%c%c%c\n",
				    oemfru[0], oemfru[1], oemfru[2],
				    oemfru[3], oemfru[4], oemfru[5],
				    oemfru[6], oemfru[7]);
			seq_printf(seq,
				    "    Supplier #           : %c%c%c%c%c\n",
				    oemfru[8], oemfru[9], oemfru[10],
				    oemfru[11], oemfru[12]);
			seq_printf(seq,
				    "    Date                 : %c%c%c\n",
				    oemfru[13], oemfru[14], oemfru[15]);
			seq_sprintf(seq,
				    "    Sequence #           : %c%c%c%c\n",
				    oemfru[16], oemfru[17], oemfru[18],
				    oemfru[19]);
			break;
		}

	case VENDOR3_FRU_FORMAT:
		{
			seq_printf(seq, "FRU Information:\n");
		}

	case VENDOR4_FRU_FORMAT:
		{
			seq_printf(seq, "FRU Information:\n");
			seq_printf(seq,
				    "    FRU Number           : "
				    "%c%c%c%c%c%c%c%c\n",
				    oemfru[0], oemfru[1], oemfru[2],
				    oemfru[3], oemfru[4], oemfru[5],
				    oemfru[6], oemfru[7]);
			seq_sprintf(seq,
				    "    Part Number          : "
				    "%c%c%c%c%c%c%c%c\n",
				    oemfru[8], oemfru[9], oemfru[10],
				    oemfru[11], oemfru[12], oemfru[13],
				    oemfru[14], oemfru[15]);
			seq_printf(seq,
				    "    EC Level             : "
				    "%c%c%c%c%c%c%c%c\n",
				    oemfru[16], oemfru[17], oemfru[18],
				    oemfru[19], oemfru[20], oemfru[21],
				    oemfru[22], oemfru[23]);
			break;
		}

	default:
		break;
	}
#endif

	return 0;
}

static int slic_debug_adapter_show(struct seq_file *seq, void *v)
{
	struct adapter *adapter = seq->private;
	struct net_device *netdev = adapter->netdev;

	seq_printf(seq, "info: interface          : %s\n",
			    adapter->netdev->name);
	seq_printf(seq, "info: status             : %s\n",
		SLIC_LINKSTATE(adapter->linkstate));
	seq_printf(seq, "info: port               : %d\n",
		adapter->physport);
	seq_printf(seq, "info: speed              : %s\n",
		SLIC_SPEED(adapter->linkspeed));
	seq_printf(seq, "info: duplex             : %s\n",
		SLIC_DUPLEX(adapter->linkduplex));
	seq_printf(seq, "info: irq                : 0x%X\n",
		(uint) adapter->irq);
	seq_printf(seq, "info: Interrupt Agg Delay: %d usec\n",
		adapter->card->loadlevel_current);
	seq_printf(seq, "info: RcvQ max entries   : %4.4X\n",
		SLIC_RCVQ_ENTRIES);
	seq_printf(seq, "info: RcvQ current       : %4.4X\n",
		    adapter->rcvqueue.count);
	seq_printf(seq, "rx stats: packets                  : %8.8lX\n",
		    netdev->stats.rx_packets);
	seq_printf(seq, "rx stats: bytes                    : %8.8lX\n",
		    netdev->stats.rx_bytes);
	seq_printf(seq, "rx stats: broadcasts               : %8.8X\n",
		    adapter->rcv_broadcasts);
	seq_printf(seq, "rx stats: multicasts               : %8.8X\n",
		    adapter->rcv_multicasts);
	seq_printf(seq, "rx stats: unicasts                 : %8.8X\n",
		    adapter->rcv_unicasts);
	seq_printf(seq, "rx stats: errors                   : %8.8X\n",
		    (u32) adapter->slic_stats.iface.rcv_errors);
	seq_printf(seq, "rx stats: Missed errors            : %8.8X\n",
		    (u32) adapter->slic_stats.iface.rcv_discards);
	seq_printf(seq, "rx stats: drops                    : %8.8X\n",
			(u32) adapter->rcv_drops);
	seq_printf(seq, "tx stats: packets                  : %8.8lX\n",
			netdev->stats.tx_packets);
	seq_printf(seq, "tx stats: bytes                    : %8.8lX\n",
			netdev->stats.tx_bytes);
	seq_printf(seq, "tx stats: errors                   : %8.8X\n",
			(u32) adapter->slic_stats.iface.xmt_errors);
	seq_printf(seq, "rx stats: multicasts               : %8.8lX\n",
			netdev->stats.multicast);
	seq_printf(seq, "tx stats: collision errors         : %8.8X\n",
			(u32) adapter->slic_stats.iface.xmit_collisions);
	seq_printf(seq, "perf: Max rcv frames/isr           : %8.8X\n",
			adapter->max_isr_rcvs);
	seq_printf(seq, "perf: Rcv interrupt yields         : %8.8X\n",
			adapter->rcv_interrupt_yields);
	seq_printf(seq, "perf: Max xmit complete/isr        : %8.8X\n",
			adapter->max_isr_xmits);
	seq_printf(seq, "perf: error interrupts             : %8.8X\n",
			adapter->error_interrupts);
	seq_printf(seq, "perf: error rmiss interrupts       : %8.8X\n",
			adapter->error_rmiss_interrupts);
	seq_printf(seq, "perf: rcv interrupts               : %8.8X\n",
			adapter->rcv_interrupts);
	seq_printf(seq, "perf: xmit interrupts              : %8.8X\n",
			adapter->xmit_interrupts);
	seq_printf(seq, "perf: link event interrupts        : %8.8X\n",
			adapter->linkevent_interrupts);
	seq_printf(seq, "perf: UPR interrupts               : %8.8X\n",
			adapter->upr_interrupts);
	seq_printf(seq, "perf: interrupt count              : %8.8X\n",
			adapter->num_isrs);
	seq_printf(seq, "perf: false interrupts             : %8.8X\n",
			adapter->false_interrupts);
	seq_printf(seq, "perf: All register writes          : %8.8X\n",
			adapter->all_reg_writes);
	seq_printf(seq, "perf: ICR register writes          : %8.8X\n",
			adapter->icr_reg_writes);
	seq_printf(seq, "perf: ISR register writes          : %8.8X\n",
			adapter->isr_reg_writes);
	seq_printf(seq, "ifevents: overflow 802 errors      : %8.8X\n",
			adapter->if_events.oflow802);
	seq_printf(seq, "ifevents: transport overflow errors: %8.8X\n",
			adapter->if_events.Tprtoflow);
	seq_printf(seq, "ifevents: underflow errors         : %8.8X\n",
			adapter->if_events.uflow802);
	seq_printf(seq, "ifevents: receive early            : %8.8X\n",
			adapter->if_events.rcvearly);
	seq_printf(seq, "ifevents: buffer overflows         : %8.8X\n",
			adapter->if_events.Bufov);
	seq_printf(seq, "ifevents: carrier errors           : %8.8X\n",
			adapter->if_events.Carre);
	seq_printf(seq, "ifevents: Long                     : %8.8X\n",
			adapter->if_events.Longe);
	seq_printf(seq, "ifevents: invalid preambles        : %8.8X\n",
			adapter->if_events.Invp);
	seq_printf(seq, "ifevents: CRC errors               : %8.8X\n",
			adapter->if_events.Crc);
	seq_printf(seq, "ifevents: dribble nibbles          : %8.8X\n",
			adapter->if_events.Drbl);
	seq_printf(seq, "ifevents: Code violations          : %8.8X\n",
			adapter->if_events.Code);
	seq_printf(seq, "ifevents: TCP checksum errors      : %8.8X\n",
			adapter->if_events.TpCsum);
	seq_printf(seq, "ifevents: TCP header short errors  : %8.8X\n",
			adapter->if_events.TpHlen);
	seq_printf(seq, "ifevents: IP checksum errors       : %8.8X\n",
			adapter->if_events.IpCsum);
	seq_printf(seq, "ifevents: IP frame incompletes     : %8.8X\n",
			adapter->if_events.IpLen);
	seq_printf(seq, "ifevents: IP headers shorts        : %8.8X\n",
			adapter->if_events.IpHlen);

	return 0;
}
static int slic_debug_adapter_open(struct inode *inode, struct file *file)
{
	return single_open(file, slic_debug_adapter_show, inode->i_private);
}

static int slic_debug_card_open(struct inode *inode, struct file *file)
{
	return single_open(file, slic_debug_card_show, inode->i_private);
}

static const struct file_operations slic_debug_adapter_fops = {
	.owner		= THIS_MODULE,
	.open		= slic_debug_adapter_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static const struct file_operations slic_debug_card_fops = {
	.owner		= THIS_MODULE,
	.open		= slic_debug_card_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static void slic_debug_adapter_create(struct adapter *adapter)
{
	struct dentry *d;
	char    name[7];
	struct sliccard *card = adapter->card;

	if (!card->debugfs_dir)
		return;

	sprintf(name, "port%d", adapter->port);
	d = debugfs_create_file(name, S_IRUGO,
				card->debugfs_dir, adapter,
				&slic_debug_adapter_fops);
	if (!d || IS_ERR(d))
		pr_info(PFX "%s: debugfs create failed\n", name);
	else
		adapter->debugfs_entry = d;
}

static void slic_debug_adapter_destroy(struct adapter *adapter)
{
	debugfs_remove(adapter->debugfs_entry);
	adapter->debugfs_entry = NULL;
}

static void slic_debug_card_create(struct sliccard *card)
{
	struct dentry *d;
	char    name[IFNAMSIZ];

	snprintf(name, sizeof(name), "slic%d", card->cardnum);
	d = debugfs_create_dir(name, slic_debugfs);
	if (!d || IS_ERR(d))
		pr_info(PFX "%s: debugfs create dir failed\n",
				name);
	else {
		card->debugfs_dir = d;
		d = debugfs_create_file("cardinfo", S_IRUGO,
				slic_debugfs, card,
				&slic_debug_card_fops);
		if (!d || IS_ERR(d))
			pr_info(PFX "%s: debugfs create failed\n",
					name);
		else
			card->debugfs_cardinfo = d;
	}
}

static void slic_debug_card_destroy(struct sliccard *card)
{
	int i;

	for (i = 0; i < card->card_size; i++) {
		struct adapter *adapter;

		adapter = card->adapter[i];
		if (adapter)
			slic_debug_adapter_destroy(adapter);
	}
	if (card->debugfs_cardinfo) {
		debugfs_remove(card->debugfs_cardinfo);
		card->debugfs_cardinfo = NULL;
	}
	if (card->debugfs_dir) {
		debugfs_remove(card->debugfs_dir);
		card->debugfs_dir = NULL;
	}
}

static void slic_debug_init(void)
{
	struct dentry *ent;

	ent = debugfs_create_dir("slic", NULL);
	if (!ent || IS_ERR(ent)) {
		pr_info(PFX "debugfs create directory failed\n");
		return;
	}

	slic_debugfs = ent;
}

static void slic_debug_cleanup(void)
{
	if (slic_debugfs) {
		debugfs_remove(slic_debugfs);
		slic_debugfs = NULL;
	}
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
static void slic_link_event_handler(struct adapter *adapter)
{
	int status;
	struct slic_shmem *pshmem;

	if (adapter->state != ADAPT_UP) {
		/* Adapter is not operational.  Ignore.  */
		return;
	}

	pshmem = (struct slic_shmem *)adapter->phys_shmem;

#if BITS_PER_LONG == 64
	status = slic_upr_request(adapter,
				  SLIC_UPR_RLSR,
				  SLIC_GET_ADDR_LOW(&pshmem->linkstatus),
				  SLIC_GET_ADDR_HIGH(&pshmem->linkstatus),
				  0, 0);
#else
	status = slic_upr_request(adapter, SLIC_UPR_RLSR,
		(u32) &pshmem->linkstatus,	/* no 4GB wrap guaranteed */
				  0, 0, 0);
#endif
	ASSERT(status == 0);
}

static void slic_init_cleanup(struct adapter *adapter)
{
	if (adapter->intrregistered) {
		adapter->intrregistered = 0;
		free_irq(adapter->netdev->irq, adapter->netdev);

	}
	if (adapter->pshmem) {
		pci_free_consistent(adapter->pcidev,
				    sizeof(struct slic_shmem),
				    adapter->pshmem, adapter->phys_shmem);
		adapter->pshmem = NULL;
		adapter->phys_shmem = (dma_addr_t) NULL;
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
		if (!compare_ether_addr(mlist->address, address))
			return 0;
		mlist = mlist->next;
	}

	/* Doesn't already exist.  Allocate a structure to hold it */
	mcaddr = kmalloc(sizeof(struct mcast_address), GFP_ATOMIC);
	if (mcaddr == NULL)
		return 1;

	memcpy(mcaddr->address, address, 6);

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

	ASSERT(adapter);

	netdev_for_each_mc_addr(ha, dev) {
		addresses = (char *) &ha->addr;
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
	return;
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

	ihcmd->flags = (adapter->port << IHFLG_IFSHFT);
	ihcmd->command = IHCMD_XMT_REQ;
	ihcmd->u.slic_buffers.totlen = skb->len;
	phys_addr = pci_map_single(adapter->pcidev, skb->data, skb->len,
			PCI_DMA_TODEVICE);
	ihcmd->u.slic_buffers.bufs[0].paddrl = SLIC_GET_ADDR_LOW(phys_addr);
	ihcmd->u.slic_buffers.bufs[0].paddrh = SLIC_GET_ADDR_HIGH(phys_addr);
	ihcmd->u.slic_buffers.bufs[0].length = skb->len;
#if BITS_PER_LONG == 64
	hcmd->cmdsize = (u32) ((((u64)&ihcmd->u.slic_buffers.bufs[1] -
				     (u64) hcmd) + 31) >> 5);
#else
	hcmd->cmdsize = ((((u32) &ihcmd->u.slic_buffers.bufs[1] -
			   (u32) hcmd) + 31) >> 5);
#endif
}

static void slic_xmit_fail(struct adapter *adapter,
		    struct sk_buff *skb,
		    void *cmd, u32 skbtype, u32 status)
{
	if (adapter->xmitq_full)
		netif_stop_queue(adapter->netdev);
	if ((cmd == NULL) && (status <= XMIT_FAIL_HOSTCMD_FAIL)) {
		switch (status) {
		case XMIT_FAIL_LINK_STATE:
			dev_err(&adapter->netdev->dev,
				"reject xmit skb[%p: %x] linkstate[%s] "
				"adapter[%s:%d] card[%s:%d]\n",
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
				"xmit_start skb[%p] type[%x] No host commands "
				"available\n", skb, skb->pkt_type);
			break;
		default:
			ASSERT(0);
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
	return;
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

		ASSERT(skb->head);
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
		 Get the complete host command buffer
		*/
		slic_handle_word.handle_token = rspbuf->hosthandle;
		ASSERT(slic_handle_word.handle_index);
		ASSERT(slic_handle_word.handle_index <= SLIC_CMDQ_MAXCMDS);
		hcmd =
		    (struct slic_hostcmd *)
			adapter->slic_handles[slic_handle_word.handle_index].
									address;
/*      hcmd = (struct slic_hostcmd *) rspbuf->hosthandle; */
		ASSERT(hcmd);
		ASSERT(hcmd->pslic_handle ==
		       &adapter->slic_handles[slic_handle_word.handle_index]);
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

static irqreturn_t slic_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = (struct net_device *)dev_id;
	struct adapter *adapter = netdev_priv(dev);
	u32 isr;

	if ((adapter->pshmem) && (adapter->pshmem->isr)) {
		slic_reg32_write(&adapter->slic_regs->slic_icr,
				 ICR_INT_MASK, FLUSH);
		isr = adapter->isrcopy = adapter->pshmem->isr;
		adapter->pshmem->isr = 0;
		adapter->num_isrs++;
		switch (adapter->card->state) {
		case CARD_UP:
			if (isr & ~ISR_IO) {
				if (isr & ISR_ERR) {
					adapter->error_interrupts++;
					if (isr & ISR_RMISS) {
						int count;
						int pre_count;
						int errors;

						struct slic_rcvqueue *rcvq =
						    &adapter->rcvqueue;

						adapter->
						    error_rmiss_interrupts++;
						if (!rcvq->errors)
							rcv_count = rcvq->count;
						pre_count = rcvq->count;
						errors = rcvq->errors;

						while (rcvq->count <
						       SLIC_RCVQ_FILLTHRESH) {
							count =
							    slic_rcvqueue_fill
							    (adapter);
							if (!count)
								break;
						}
					} else if (isr & ISR_XDROP) {
						dev_err(&dev->dev,
							"isr & ISR_ERR [%x] "
							"ISR_XDROP \n", isr);
					} else {
						dev_err(&dev->dev,
							"isr & ISR_ERR [%x]\n",
							isr);
					}
				}

				if (isr & ISR_LEVENT) {
					adapter->linkevent_interrupts++;
					slic_link_event_handler(adapter);
				}

				if ((isr & ISR_UPC) ||
				    (isr & ISR_UPCERR) || (isr & ISR_UPCBSY)) {
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
			break;

		case CARD_DOWN:
			if ((isr & ISR_UPC) ||
			    (isr & ISR_UPCERR) || (isr & ISR_UPCBSY)) {
				adapter->upr_interrupts++;
				slic_upr_request_complete(adapter, isr);
			}
			break;

		default:
			break;
		}

		adapter->isrcopy = 0;
		adapter->all_reg_writes += 2;
		adapter->isr_reg_writes++;
		slic_reg32_write(&adapter->slic_regs->slic_isr, 0, FLUSH);
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
	u32 skbtype = NORMAL_ETHFRAME;
	void *offloadcmd = NULL;

	card = adapter->card;
	ASSERT(card);
	if ((adapter->linkstate != LINK_UP) ||
	    (adapter->state != ADAPT_UP) || (card->state != CARD_UP)) {
		status = XMIT_FAIL_LINK_STATE;
		goto xmit_fail;

	} else if (skb->len == 0) {
		status = XMIT_FAIL_ZERO_LENGTH;
		goto xmit_fail;
	}

	if (skbtype == NORMAL_ETHFRAME) {
		hcmd = slic_cmdq_getfree(adapter);
		if (!hcmd) {
			adapter->xmitq_full = 1;
			status = XMIT_FAIL_HOSTCMD_FAIL;
			goto xmit_fail;
		}
		ASSERT(hcmd->pslic_handle);
		ASSERT(hcmd->cmd64.hosthandle ==
		       hcmd->pslic_handle->token.handle_token);
		hcmd->skb = skb;
		hcmd->busy = 1;
		hcmd->type = SLIC_CMD_DUMB;
		if (skbtype == NORMAL_ETHFRAME)
			slic_xmit_build_request(adapter, hcmd, skb);
	}
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
		slic_reg32_write(&adapter->slic_regs->slic_cbar,
				 (hcmd->paddrl | hcmd->cmdsize), DONT_FLUSH);
	} else {
		slic_reg64_write(adapter, &adapter->slic_regs->slic_cbar64,
				 (hcmd->paddrl | hcmd->cmdsize),
				 &adapter->slic_regs->slic_addr_upper,
				 hcmd->paddrh, DONT_FLUSH);
	}
xmit_done:
	return NETDEV_TX_OK;
xmit_fail:
	slic_xmit_fail(adapter, skb, offloadcmd, skbtype, status);
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

static int slic_adapter_allocresources(struct adapter *adapter)
{
	if (!adapter->intrregistered) {
		int retval;

		spin_unlock_irqrestore(&slic_global.driver_lock.lock,
					slic_global.driver_lock.flags);

		retval = request_irq(adapter->netdev->irq,
				     &slic_interrupt,
				     IRQF_SHARED,
				     adapter->netdev->name, adapter->netdev);

		spin_lock_irqsave(&slic_global.driver_lock.lock,
					slic_global.driver_lock.flags);

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
static int slic_if_init(struct adapter *adapter)
{
	struct sliccard *card = adapter->card;
	struct net_device *dev = adapter->netdev;
	__iomem struct slic_regs *slic_regs = adapter->slic_regs;
	struct slic_shmem *pshmem;
	int rc;

	ASSERT(card);

	/* adapter should be down at this point */
	if (adapter->state != ADAPT_DOWN) {
		dev_err(&dev->dev, "%s: adapter->state != ADAPT_DOWN\n",
			__func__);
		rc = -EIO;
		goto err;
	}
	ASSERT(adapter->linkstate == LINK_DOWN);

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
	rc = slic_adapter_allocresources(adapter);
	if (rc) {
		dev_err(&dev->dev,
			"%s: slic_adapter_allocresources FAILED %x\n",
			__func__, rc);
		slic_adapter_freeresources(adapter);
		goto err;
	}

	if (!adapter->queues_initialized) {
		if ((rc = slic_rspqueue_init(adapter)))
			goto err;
		if ((rc = slic_cmdq_init(adapter)))
			goto err;
		if ((rc = slic_rcvqueue_init(adapter)))
			goto err;
		adapter->queues_initialized = 1;
	}

	slic_reg32_write(&slic_regs->slic_icr, ICR_INT_OFF, FLUSH);
	mdelay(1);

	if (!adapter->isp_initialized) {
		pshmem = (struct slic_shmem *)adapter->phys_shmem;

		spin_lock_irqsave(&adapter->bit64reglock.lock,
					adapter->bit64reglock.flags);

#if BITS_PER_LONG == 64
		slic_reg32_write(&slic_regs->slic_addr_upper,
				 SLIC_GET_ADDR_HIGH(&pshmem->isr), DONT_FLUSH);
		slic_reg32_write(&slic_regs->slic_isp,
				 SLIC_GET_ADDR_LOW(&pshmem->isr), FLUSH);
#else
		slic_reg32_write(&slic_regs->slic_addr_upper, 0, DONT_FLUSH);
		slic_reg32_write(&slic_regs->slic_isp, (u32)&pshmem->isr, FLUSH);
#endif
		spin_unlock_irqrestore(&adapter->bit64reglock.lock,
					adapter->bit64reglock.flags);
		adapter->isp_initialized = 1;
	}

	adapter->state = ADAPT_UP;
	if (!card->loadtimerset) {
		init_timer(&card->loadtimer);
		card->loadtimer.expires =
		    jiffies + (SLIC_LOADTIMER_PERIOD * HZ);
		card->loadtimer.data = (ulong) card;
		card->loadtimer.function = &slic_timer_load_check;
		add_timer(&card->loadtimer);

		card->loadtimerset = 1;
	}

	if (!adapter->pingtimerset) {
		init_timer(&adapter->pingtimer);
		adapter->pingtimer.expires =
		    jiffies + (PING_TIMER_INTERVAL * HZ);
		adapter->pingtimer.data = (ulong) dev;
		adapter->pingtimer.function = &slic_timer_ping;
		add_timer(&adapter->pingtimer);
		adapter->pingtimerset = 1;
		adapter->card->pingstatus = ISR_PINGMASK;
	}

	/*
	 *    clear any pending events, then enable interrupts
	 */
	adapter->isrcopy = 0;
	adapter->pshmem->isr = 0;
	slic_reg32_write(&slic_regs->slic_isr, 0, FLUSH);
	slic_reg32_write(&slic_regs->slic_icr, ICR_INT_ON, FLUSH);

	slic_link_config(adapter, LINK_AUTOSPEED, LINK_AUTOD);
	slic_link_event_handler(adapter);

err:
	return rc;
}

static int slic_entry_open(struct net_device *dev)
{
	struct adapter *adapter = netdev_priv(dev);
	struct sliccard *card = adapter->card;
	u32 locked = 0;
	int status;

	ASSERT(adapter);
	ASSERT(card);

	netif_stop_queue(adapter->netdev);

	spin_lock_irqsave(&slic_global.driver_lock.lock,
				slic_global.driver_lock.flags);
	locked = 1;
	if (!adapter->activated) {
		card->adapters_activated++;
		slic_global.num_slic_ports_active++;
		adapter->activated = 1;
	}
	status = slic_if_init(adapter);

	if (status != 0) {
		if (adapter->activated) {
			card->adapters_activated--;
			slic_global.num_slic_ports_active--;
			adapter->activated = 0;
		}
		if (locked) {
			spin_unlock_irqrestore(&slic_global.driver_lock.lock,
						slic_global.driver_lock.flags);
			locked = 0;
		}
		return status;
	}
	if (!card->master)
		card->master = adapter;

	if (locked) {
		spin_unlock_irqrestore(&slic_global.driver_lock.lock,
					slic_global.driver_lock.flags);
		locked = 0;
	}

	return 0;
}

static void slic_card_cleanup(struct sliccard *card)
{
	if (card->loadtimerset) {
		card->loadtimerset = 0;
		del_timer(&card->loadtimer);
	}

	slic_debug_card_destroy(card);

	kfree(card);
}

static void __devexit slic_entry_remove(struct pci_dev *pcidev)
{
	struct net_device *dev = pci_get_drvdata(pcidev);
	u32 mmio_start = 0;
	uint mmio_len = 0;
	struct adapter *adapter = netdev_priv(dev);
	struct sliccard *card;
	struct mcast_address *mcaddr, *mlist;

	slic_adapter_freeresources(adapter);
	slic_unmap_mmio_space(adapter);
	unregister_netdev(dev);

	mmio_start = pci_resource_start(pcidev, 0);
	mmio_len = pci_resource_len(pcidev, 0);

	release_mem_region(mmio_start, mmio_len);

	iounmap((void __iomem *)dev->base_addr);
	/* free multicast addresses */
	mlist = adapter->mcastaddrs;
	while (mlist) {
		mcaddr = mlist;
		mlist = mlist->next;
		kfree(mcaddr);
	}
	ASSERT(adapter->card);
	card = adapter->card;
	ASSERT(card->adapters_allocated);
	card->adapters_allocated--;
	adapter->allocated = 0;
	if (!card->adapters_allocated) {
		struct sliccard *curr_card = slic_global.slic_card;
		if (curr_card == card) {
			slic_global.slic_card = card->next;
		} else {
			while (curr_card->next != card)
				curr_card = curr_card->next;
			ASSERT(curr_card);
			curr_card->next = card->next;
		}
		ASSERT(slic_global.num_slic_cards);
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
	__iomem struct slic_regs *slic_regs = adapter->slic_regs;

	spin_lock_irqsave(&slic_global.driver_lock.lock,
				slic_global.driver_lock.flags);
	ASSERT(card);
	netif_stop_queue(adapter->netdev);
	adapter->state = ADAPT_DOWN;
	adapter->linkstate = LINK_DOWN;
	adapter->upr_list = NULL;
	adapter->upr_busy = 0;
	adapter->devflags_prev = 0;
	ASSERT(card->adapter[adapter->cardindex] == adapter);
	slic_reg32_write(&slic_regs->slic_icr, ICR_INT_OFF, FLUSH);
	adapter->all_reg_writes++;
	adapter->icr_reg_writes++;
	slic_config_clear(adapter);
	if (adapter->activated) {
		card->adapters_activated--;
		slic_global.num_slic_ports_active--;
		adapter->activated = 0;
	}
#ifdef AUTOMATIC_RESET
	slic_reg32_write(&slic_regs->slic_reset_iface, 0, FLUSH);
#endif
	/*
	 *  Reset the adapter's cmd queues
	 */
	slic_cmdq_reset(adapter);

#ifdef AUTOMATIC_RESET
	if (!card->adapters_activated)
		slic_card_init(card, adapter);
#endif

	spin_unlock_irqrestore(&slic_global.driver_lock.lock,
				slic_global.driver_lock.flags);
	return 0;
}

static struct net_device_stats *slic_get_stats(struct net_device *dev)
{
	struct adapter *adapter = netdev_priv(dev);

	ASSERT(adapter);
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

	ASSERT(rq);
	switch (cmd) {
	case SIOCSLICSETINTAGG:
		if (copy_from_user(data, rq->ifr_data, 28))
			return -EFAULT;
		intagg = data[0];
		dev_err(&dev->dev, "%s: set interrupt aggregation to %d\n",
			__func__, intagg);
		slic_intagg_set(adapter, intagg);
		return 0;

#ifdef SLIC_TRACE_DUMP_ENABLED
	case SIOCSLICTRACEDUMP:
		{
			u32 value;
			DBG_IOCTL("slic_ioctl  SIOCSLIC_TRACE_DUMP\n");

			if (copy_from_user(data, rq->ifr_data, 28)) {
				PRINT_ERROR
				    ("slic: copy_from_user FAILED getting initial simba param\n");
				return -EFAULT;
			}

			value = data[0];
			if (tracemon_request == SLIC_DUMP_DONE) {
				PRINT_ERROR
				    ("ATK Diagnostic Trace Dump Requested\n");
				tracemon_request = SLIC_DUMP_REQUESTED;
				tracemon_request_type = value;
				tracemon_timestamp = jiffies;
			} else if ((tracemon_request == SLIC_DUMP_REQUESTED) ||
				   (tracemon_request ==
				    SLIC_DUMP_IN_PROGRESS)) {
				PRINT_ERROR
				    ("ATK Diagnostic Trace Dump Requested but already in progress... ignore\n");
			} else {
				PRINT_ERROR
				    ("ATK Diagnostic Trace Dump Requested\n");
				tracemon_request = SLIC_DUMP_REQUESTED;
				tracemon_request_type = value;
				tracemon_timestamp = jiffies;
			}
			return 0;
		}
#endif
	case SIOCETHTOOL:
		ASSERT(adapter);
		if (copy_from_user(&ecmd, rq->ifr_data, sizeof(ecmd)))
			return -EFAULT;

		if (ecmd.cmd == ETHTOOL_GSET) {
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
				slic_link_event_handler(adapter);
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
	__iomem struct slic_regs *slic_regs = adapter->slic_regs;
	struct slic_eeprom *peeprom;
	struct oslic_eeprom *pOeeprom;
	dma_addr_t phys_config;
	u32 phys_configh;
	u32 phys_configl;
	u32 i = 0;
	struct slic_shmem *pshmem;
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

	/* Reset everything except PCI configuration space */
	slic_soft_reset(adapter);

	/* Download the microcode */
	status = slic_card_download(adapter);

	if (status != 0) {
		dev_err(&adapter->pcidev->dev,
			"download failed bus %d slot %d\n",
			adapter->busnumber, adapter->slotnumber);
		return status;
	}

	if (!card->config_set) {
		peeprom = pci_alloc_consistent(adapter->pcidev,
					       sizeof(struct slic_eeprom),
					       &phys_config);

		phys_configl = SLIC_GET_ADDR_LOW(phys_config);
		phys_configh = SLIC_GET_ADDR_HIGH(phys_config);

		if (!peeprom) {
			dev_err(&adapter->pcidev->dev,
				"eeprom read failed to get memory "
				"bus %d slot %d\n", adapter->busnumber,
				adapter->slotnumber);
			return -ENOMEM;
		} else {
			memset(peeprom, 0, sizeof(struct slic_eeprom));
		}
		slic_reg32_write(&slic_regs->slic_icr, ICR_INT_OFF, FLUSH);
		mdelay(1);
		pshmem = (struct slic_shmem *)adapter->phys_shmem;

		spin_lock_irqsave(&adapter->bit64reglock.lock,
					adapter->bit64reglock.flags);
		slic_reg32_write(&slic_regs->slic_addr_upper, 0, DONT_FLUSH);
		slic_reg32_write(&slic_regs->slic_isp,
				 SLIC_GET_ADDR_LOW(&pshmem->isr), FLUSH);
		spin_unlock_irqrestore(&adapter->bit64reglock.lock,
					adapter->bit64reglock.flags);

		slic_config_get(adapter, phys_configl, phys_configh);

		for (;;) {
			if (adapter->pshmem->isr) {
				if (adapter->pshmem->isr & ISR_UPC) {
					adapter->pshmem->isr = 0;
					slic_reg64_write(adapter,
						&slic_regs->slic_isp, 0,
						&slic_regs->slic_addr_upper,
						0, FLUSH);
					slic_reg32_write(&slic_regs->slic_isr,
							 0, FLUSH);

					slic_upr_request_complete(adapter, 0);
					break;
				} else {
					adapter->pshmem->isr = 0;
					slic_reg32_write(&slic_regs->slic_isr,
							 0, FLUSH);
				}
			} else {
				mdelay(1);
				i++;
				if (i > 5000) {
					dev_err(&adapter->pcidev->dev,
						"%d config data fetch timed out!\n",
						adapter->port);
					slic_reg64_write(adapter,
						&slic_regs->slic_isp, 0,
						&slic_regs->slic_addr_upper,
						0, FLUSH);
					return -EINVAL;
				}
			}
		}

		switch (adapter->devid) {
		/* Oasis card */
		case SLIC_2GB_DEVICE_ID:
			/* extract EEPROM data and pointers to EEPROM data */
			pOeeprom = (struct oslic_eeprom *) peeprom;
			eecodesize = pOeeprom->EecodeSize;
			dramsize = pOeeprom->DramSize;
			pmac = pOeeprom->MacInfo;
			fruformat = pOeeprom->FruFormat;
			patkfru = &pOeeprom->AtkFru;
			oemfruformat = pOeeprom->OemFruFormat;
			poemfru = &pOeeprom->OemFru;
			macaddrs = 2;
			/* Minor kludge for Oasis card
			     get 2 MAC addresses from the
			     EEPROM to ensure that function 1
			     gets the Port 1 MAC address */
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
			    *(u16 *) ((char *) peeprom + (eecodesize - 2));
			/*
			    calculate the EEPROM checksum
			*/
			calc_chksum =
			    ~slic_eeprom_cksum((char *) peeprom,
					       (eecodesize - 2));
			/*
			    if the ucdoe chksum flag bit worked,
			    we wouldn't need this shit
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

		if ((!card->config.EepromValid) &&
		    (adapter->reg_params.fail_on_bad_eeprom)) {
			slic_reg64_write(adapter, &slic_regs->slic_isp, 0,
					 &slic_regs->slic_addr_upper,
					 0, FLUSH);
			dev_err(&adapter->pcidev->dev,
				"unsupported CONFIGURATION EEPROM invalid\n");
			return -EINVAL;
		}

		card->config_set = 1;
	}

	if (slic_card_download_gbrcv(adapter)) {
		dev_err(&adapter->pcidev->dev,
			"unable to download GB receive microcode\n");
		return -EINVAL;
	}

	if (slic_global.dynamic_intagg)
		slic_intagg_set(adapter, 0);
	else
		slic_intagg_set(adapter, intagg_delay);

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
}

static void slic_init_driver(void)
{
	if (slic_first_init) {
		slic_first_init = 0;
		spin_lock_init(&slic_global.driver_lock.lock);
		slic_debug_init();
	}
}

static void slic_init_adapter(struct net_device *netdev,
			      struct pci_dev *pcidev,
			      const struct pci_device_id *pci_tbl_entry,
			      void __iomem *memaddr, int chip_idx)
{
	ushort index;
	struct slic_handle *pslic_handle;
	struct adapter *adapter = netdev_priv(netdev);

/*	adapter->pcidev = pcidev;*/
	adapter->vendid = pci_tbl_entry->vendor;
	adapter->devid = pci_tbl_entry->device;
	adapter->subsysid = pci_tbl_entry->subdevice;
	adapter->busnumber = pcidev->bus->number;
	adapter->slotnumber = ((pcidev->devfn >> 3) & 0x1F);
	adapter->functionnumber = (pcidev->devfn & 0x7);
	adapter->memorylength = pci_resource_len(pcidev, 0);
	adapter->slic_regs = (__iomem struct slic_regs *)memaddr;
	adapter->irq = pcidev->irq;
/*	adapter->netdev = netdev;*/
	adapter->next_netdevice = head_netdevice;
	head_netdevice = netdev;
	adapter->chipid = chip_idx;
	adapter->port = 0;	/*adapter->functionnumber;*/
	adapter->cardindex = adapter->port;
	adapter->memorybase = memaddr;
	spin_lock_init(&adapter->upr_lock.lock);
	spin_lock_init(&adapter->bit64reglock.lock);
	spin_lock_init(&adapter->adapter_lock.lock);
	spin_lock_init(&adapter->reset_lock.lock);
	spin_lock_init(&adapter->handle_lock.lock);

	adapter->card_size = 1;
	/*
	  Initialize slic_handle array
	*/
	ASSERT(SLIC_CMDQ_MAXCMDS <= 0xFFFF);
	/*
	 Start with 1.  0 is an invalid host handle.
	*/
	for (index = 1, pslic_handle = &adapter->slic_handles[1];
	     index < SLIC_CMDQ_MAXCMDS; index++, pslic_handle++) {

		pslic_handle->token.handle_index = index;
		pslic_handle->type = SLIC_HANDLE_FREE;
		pslic_handle->next = adapter->pfree_slic_handles;
		adapter->pfree_slic_handles = pslic_handle;
	}
	adapter->pshmem = (struct slic_shmem *)
					pci_alloc_consistent(adapter->pcidev,
					sizeof(struct slic_shmem),
					&adapter->
					phys_shmem);
	ASSERT(adapter->pshmem);

	memset(adapter->pshmem, 0, sizeof(struct slic_shmem));

	return;
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
	u16 __iomem *hostid_reg;
	uint i;
	uint rdhostid_offset = 0;

	switch (adapter->devid) {
	case SLIC_2GB_DEVICE_ID:
		rdhostid_offset = SLIC_RDHOSTID_2GB;
		break;
	case SLIC_1GB_DEVICE_ID:
		rdhostid_offset = SLIC_RDHOSTID_1GB;
		break;
	default:
		return -ENODEV;
	}

	hostid_reg =
	    (u16 __iomem *) (((u8 __iomem *) (adapter->slic_regs)) +
	    rdhostid_offset);

	/* read the 16 bit hostid from SRAM */
	card_hostid = (ushort) readw(hostid_reg);

	/* Initialize a new card structure if need be */
	if (card_hostid == SLIC_HOSTID_DEFAULT) {
		card = kzalloc(sizeof(struct sliccard), GFP_KERNEL);
		if (card == NULL)
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

		slic_debug_card_create(card);
	} else {
		/* Card exists, find the card this adapter belongs to */
		while (card) {
			if (card->cardnum == card_hostid)
				break;
			card = card->next;
		}
	}

	ASSERT(card);
	if (!card)
		return -ENXIO;
	/* Put the adapter in the card's adapter list */
	ASSERT(card->adapter[adapter->port] == NULL);
	if (!card->adapter[adapter->port]) {
		card->adapter[adapter->port] = adapter;
		adapter->card = card;
	}

	card->card_size = 1;	/* one port per *logical* card */

	while (physcard) {
		for (i = 0; i < SLIC_MAX_PORTS; i++) {
			if (!physcard->adapter[i])
				continue;
			else
				break;
		}
		ASSERT(i != SLIC_MAX_PORTS);
		if (physcard->adapter[i]->slotnumber == adapter->slotnumber)
			break;
		physcard = physcard->next;
	}
	if (!physcard) {
		/* no structure allocated for this physical card yet */
		physcard = kzalloc(sizeof(struct physcard), GFP_ATOMIC);
		ASSERT(physcard);

		physcard->next = slic_global.phys_card;
		slic_global.phys_card = physcard;
		physcard->adapters_allocd = 1;
	} else {
		physcard->adapters_allocd++;
	}
	/* Note - this is ZERO relative */
	adapter->physport = physcard->adapters_allocd - 1;

	ASSERT(physcard->adapter[adapter->physport] == NULL);
	physcard->adapter[adapter->physport] = adapter;
	adapter->physcard = physcard;

	return 0;
}

static int __devinit slic_entry_probe(struct pci_dev *pcidev,
			       const struct pci_device_id *pci_tbl_entry)
{
	static int cards_found;
	static int did_version;
	int err = -ENODEV;
	struct net_device *netdev;
	struct adapter *adapter;
	void __iomem *memmapped_ioaddr = NULL;
	u32 status = 0;
	ulong mmio_start = 0;
	ulong mmio_len = 0;
	struct sliccard *card = NULL;
	int pci_using_dac = 0;

	slic_global.dynamic_intagg = dynamic_intagg;

	err = pci_enable_device(pcidev);

	if (err)
		return err;

	if (slic_debug > 0 && did_version++ == 0) {
		printk(KERN_DEBUG "%s\n", slic_banner);
		printk(KERN_DEBUG "%s\n", slic_proc_version);
	}

	if (!pci_set_dma_mask(pcidev, DMA_BIT_MASK(64))) {
		pci_using_dac = 1;
		if (pci_set_consistent_dma_mask(pcidev, DMA_BIT_MASK(64))) {
			dev_err(&pcidev->dev, "unable to obtain 64-bit DMA for "
					"consistent allocations\n");
			goto err_out_disable_pci;
		}
	} else if (pci_set_dma_mask(pcidev, DMA_BIT_MASK(32))) {
		pci_using_dac = 0;
		pci_set_consistent_dma_mask(pcidev, DMA_BIT_MASK(32));
	} else {
		dev_err(&pcidev->dev, "no usable DMA configuration\n");
		goto err_out_disable_pci;
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

	SET_NETDEV_DEV(netdev, &pcidev->dev);

	pci_set_drvdata(pcidev, netdev);
	adapter = netdev_priv(netdev);
	adapter->netdev = netdev;
	adapter->pcidev = pcidev;
	if (pci_using_dac)
		netdev->features |= NETIF_F_HIGHDMA;

	mmio_start = pci_resource_start(pcidev, 0);
	mmio_len = pci_resource_len(pcidev, 0);


/*	memmapped_ioaddr =  (u32)ioremap_nocache(mmio_start, mmio_len);*/
	memmapped_ioaddr = ioremap(mmio_start, mmio_len);
	if (!memmapped_ioaddr) {
		dev_err(&pcidev->dev, "cannot remap MMIO region %lx @ %lx\n",
			mmio_len, mmio_start);
		goto err_out_free_netdev;
	}

	slic_config_pci(pcidev);

	slic_init_driver();

	slic_init_adapter(netdev,
			  pcidev, pci_tbl_entry, memmapped_ioaddr, cards_found);

	status = slic_card_locate(adapter);
	if (status) {
		dev_err(&pcidev->dev, "cannot locate card\n");
		goto err_out_free_mmio_region;
	}

	card = adapter->card;

	if (!adapter->allocated) {
		card->adapters_allocated++;
		adapter->allocated = 1;
	}

	status = slic_card_init(card, adapter);

	if (status != 0) {
		card->state = CARD_FAIL;
		adapter->state = ADAPT_FAIL;
		adapter->linkstate = LINK_DOWN;
		dev_err(&pcidev->dev, "FAILED status[%x]\n", status);
	} else {
		slic_adapter_set_hwaddr(adapter);
	}

	netdev->base_addr = (unsigned long)adapter->memorybase;
	netdev->irq = adapter->irq;
	netdev->netdev_ops = &slic_netdev_ops;

	slic_debug_adapter_create(adapter);

	strcpy(netdev->name, "eth%d");
	err = register_netdev(netdev);
	if (err) {
		dev_err(&pcidev->dev, "Cannot register net device, aborting.\n");
		goto err_out_unmap;
	}

	cards_found++;

	return status;

err_out_unmap:
	iounmap(memmapped_ioaddr);
err_out_free_mmio_region:
	release_mem_region(mmio_start, mmio_len);
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
	.remove = __devexit_p(slic_entry_remove),
};

static int __init slic_module_init(void)
{
	slic_init_driver();

	if (debug >= 0 && slic_debug != debug)
		printk(KERN_DEBUG KBUILD_MODNAME ": debug level is %d.\n",
		       debug);
	if (debug >= 0)
		slic_debug = debug;

	return pci_register_driver(&slic_driver);
}

static void __exit slic_module_cleanup(void)
{
	pci_unregister_driver(&slic_driver);
	slic_debug_cleanup();
}

module_init(slic_module_init);
module_exit(slic_module_cleanup);
