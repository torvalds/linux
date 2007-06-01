/*
** hp100.c
** HP CASCADE Architecture Driver for 100VG-AnyLan Network Adapters
**
** $Id: hp100.c,v 1.58 2001/09/24 18:03:01 perex Exp perex $
**
** Based on the HP100 driver written by Jaroslav Kysela <perex@jcu.cz>
** Extended for new busmaster capable chipsets by
** Siegfried "Frieder" Loeffler (dg1sek) <floeff@mathematik.uni-stuttgart.de>
**
** Maintained by: Jaroslav Kysela <perex@suse.cz>
**
** This driver has only been tested with
** -- HP J2585B 10/100 Mbit/s PCI Busmaster
** -- HP J2585A 10/100 Mbit/s PCI
** -- HP J2970A 10 Mbit/s PCI Combo 10base-T/BNC
** -- HP J2973A 10 Mbit/s PCI 10base-T
** -- HP J2573  10/100 ISA
** -- Compex ReadyLink ENET100-VG4  10/100 Mbit/s PCI / EISA
** -- Compex FreedomLine 100/VG  10/100 Mbit/s ISA / EISA / PCI
**
** but it should also work with the other CASCADE based adapters.
**
** TODO:
**       -  J2573 seems to hang sometimes when in shared memory mode.
**       -  Mode for Priority TX
**       -  Check PCI registers, performance might be improved?
**       -  To reduce interrupt load in busmaster, one could switch off
**          the interrupts that are used to refill the queues whenever the
**          queues are filled up to more than a certain threshold.
**       -  some updates for EISA version of card
**
**
**   This code is free software; you can redistribute it and/or modify
**   it under the terms of the GNU General Public License as published by
**   the Free Software Foundation; either version 2 of the License, or
**   (at your option) any later version.
**
**   This code is distributed in the hope that it will be useful,
**   but WITHOUT ANY WARRANTY; without even the implied warranty of
**   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**   GNU General Public License for more details.
**
**   You should have received a copy of the GNU General Public License
**   along with this program; if not, write to the Free Software
**   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
**
** 1.57c -> 1.58
**   - used indent to change coding-style
**   - added KTI DP-200 EISA ID
**   - ioremap is also used for low (<1MB) memory (multi-architecture support)
**
** 1.57b -> 1.57c - Arnaldo Carvalho de Melo <acme@conectiva.com.br>
**   - release resources on failure in init_module
**
** 1.57 -> 1.57b - Jean II
**   - fix spinlocks, SMP is now working !
**
** 1.56 -> 1.57
**   - updates for new PCI interface for 2.1 kernels
**
** 1.55 -> 1.56
**   - removed printk in misc. interrupt and update statistics to allow
**     monitoring of card status
**   - timing changes in xmit routines, relogin to 100VG hub added when
**     driver does reset
**   - included fix for Compex FreedomLine PCI adapter
**
** 1.54 -> 1.55
**   - fixed bad initialization in init_module
**   - added Compex FreedomLine adapter
**   - some fixes in card initialization
**
** 1.53 -> 1.54
**   - added hardware multicast filter support (doesn't work)
**   - little changes in hp100_sense_lan routine
**     - added support for Coax and AUI (J2970)
**   - fix for multiple cards and hp100_mode parameter (insmod)
**   - fix for shared IRQ
**
** 1.52 -> 1.53
**   - fixed bug in multicast support
**
*/

#define HP100_DEFAULT_PRIORITY_TX 0

#undef HP100_DEBUG
#undef HP100_DEBUG_B		/* Trace  */
#undef HP100_DEBUG_BM		/* Debug busmaster code (PDL stuff) */

#undef HP100_DEBUG_TRAINING	/* Debug login-to-hub procedure */
#undef HP100_DEBUG_TX
#undef HP100_DEBUG_IRQ
#undef HP100_DEBUG_RX

#undef HP100_MULTICAST_FILTER	/* Need to be debugged... */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/eisa.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/spinlock.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/bitops.h>
#include <linux/jiffies.h>

#include <asm/io.h>

#include "hp100.h"

/*
 *  defines
 */

#define HP100_BUS_ISA     0
#define HP100_BUS_EISA    1
#define HP100_BUS_PCI     2

#define HP100_REGION_SIZE	0x20	/* for ioports */
#define HP100_SIG_LEN		8	/* same as EISA_SIG_LEN */

#define HP100_MAX_PACKET_SIZE	(1536+4)
#define HP100_MIN_PACKET_SIZE	60

#ifndef HP100_DEFAULT_RX_RATIO
/* default - 75% onboard memory on the card are used for RX packets */
#define HP100_DEFAULT_RX_RATIO	75
#endif

#ifndef HP100_DEFAULT_PRIORITY_TX
/* default - don't enable transmit outgoing packets as priority */
#define HP100_DEFAULT_PRIORITY_TX 0
#endif

/*
 *  structures
 */

struct hp100_private {
	spinlock_t lock;
	char id[HP100_SIG_LEN];
	u_short chip;
	u_short soft_model;
	u_int memory_size;
	u_int virt_memory_size;
	u_short rx_ratio;	/* 1 - 99 */
	u_short priority_tx;	/* != 0 - priority tx */
	u_short mode;		/* PIO, Shared Mem or Busmaster */
	u_char bus;
	struct pci_dev *pci_dev;
	short mem_mapped;	/* memory mapped access */
	void __iomem *mem_ptr_virt;	/* virtual memory mapped area, maybe NULL */
	unsigned long mem_ptr_phys;	/* physical memory mapped area */
	short lan_type;		/* 10Mb/s, 100Mb/s or -1 (error) */
	int hub_status;		/* was login to hub successful? */
	u_char mac1_mode;
	u_char mac2_mode;
	u_char hash_bytes[8];
	struct net_device_stats stats;

	/* Rings for busmaster mode: */
	hp100_ring_t *rxrhead;	/* Head (oldest) index into rxring */
	hp100_ring_t *rxrtail;	/* Tail (newest) index into rxring */
	hp100_ring_t *txrhead;	/* Head (oldest) index into txring */
	hp100_ring_t *txrtail;	/* Tail (newest) index into txring */

	hp100_ring_t rxring[MAX_RX_PDL];
	hp100_ring_t txring[MAX_TX_PDL];

	u_int *page_vaddr_algn;	/* Aligned virtual address of allocated page */
	u_long whatever_offset;	/* Offset to bus/phys/dma address */
	int rxrcommit;		/* # Rx PDLs commited to adapter */
	int txrcommit;		/* # Tx PDLs commited to adapter */
};

/*
 *  variables
 */
#ifdef CONFIG_ISA
static const char *hp100_isa_tbl[] = {
	"HWPF150", /* HP J2573 rev A */
	"HWP1950", /* HP J2573 */
};
#endif

#ifdef CONFIG_EISA
static struct eisa_device_id hp100_eisa_tbl[] = {
	{ "HWPF180" }, /* HP J2577 rev A */
	{ "HWP1920" }, /* HP 27248B */
	{ "HWP1940" }, /* HP J2577 */
	{ "HWP1990" }, /* HP J2577 */
	{ "CPX0301" }, /* ReadyLink ENET100-VG4 */
	{ "CPX0401" }, /* FreedomLine 100/VG */
	{ "" }	       /* Mandatory final entry ! */
};
MODULE_DEVICE_TABLE(eisa, hp100_eisa_tbl);
#endif

#ifdef CONFIG_PCI
static struct pci_device_id hp100_pci_tbl[] = {
	{PCI_VENDOR_ID_HP, PCI_DEVICE_ID_HP_J2585A, PCI_ANY_ID, PCI_ANY_ID,},
	{PCI_VENDOR_ID_HP, PCI_DEVICE_ID_HP_J2585B, PCI_ANY_ID, PCI_ANY_ID,},
	{PCI_VENDOR_ID_HP, PCI_DEVICE_ID_HP_J2970A, PCI_ANY_ID, PCI_ANY_ID,},
	{PCI_VENDOR_ID_HP, PCI_DEVICE_ID_HP_J2973A, PCI_ANY_ID, PCI_ANY_ID,},
	{PCI_VENDOR_ID_COMPEX, PCI_DEVICE_ID_COMPEX_ENET100VG4, PCI_ANY_ID, PCI_ANY_ID,},
	{PCI_VENDOR_ID_COMPEX2, PCI_DEVICE_ID_COMPEX2_100VG, PCI_ANY_ID, PCI_ANY_ID,},
/*	{PCI_VENDOR_ID_KTI, PCI_DEVICE_ID_KTI_DP200, PCI_ANY_ID, PCI_ANY_ID }, */
	{}			/* Terminating entry */
};
MODULE_DEVICE_TABLE(pci, hp100_pci_tbl);
#endif

static int hp100_rx_ratio = HP100_DEFAULT_RX_RATIO;
static int hp100_priority_tx = HP100_DEFAULT_PRIORITY_TX;
static int hp100_mode = 1;

module_param(hp100_rx_ratio, int, 0);
module_param(hp100_priority_tx, int, 0);
module_param(hp100_mode, int, 0);

/*
 *  prototypes
 */

static int hp100_probe1(struct net_device *dev, int ioaddr, u_char bus,
			struct pci_dev *pci_dev);


static int hp100_open(struct net_device *dev);
static int hp100_close(struct net_device *dev);
static int hp100_start_xmit(struct sk_buff *skb, struct net_device *dev);
static int hp100_start_xmit_bm(struct sk_buff *skb,
			       struct net_device *dev);
static void hp100_rx(struct net_device *dev);
static struct net_device_stats *hp100_get_stats(struct net_device *dev);
static void hp100_misc_interrupt(struct net_device *dev);
static void hp100_update_stats(struct net_device *dev);
static void hp100_clear_stats(struct hp100_private *lp, int ioaddr);
static void hp100_set_multicast_list(struct net_device *dev);
static irqreturn_t hp100_interrupt(int irq, void *dev_id);
static void hp100_start_interface(struct net_device *dev);
static void hp100_stop_interface(struct net_device *dev);
static void hp100_load_eeprom(struct net_device *dev, u_short ioaddr);
static int hp100_sense_lan(struct net_device *dev);
static int hp100_login_to_vg_hub(struct net_device *dev,
				 u_short force_relogin);
static int hp100_down_vg_link(struct net_device *dev);
static void hp100_cascade_reset(struct net_device *dev, u_short enable);
static void hp100_BM_shutdown(struct net_device *dev);
static void hp100_mmuinit(struct net_device *dev);
static void hp100_init_pdls(struct net_device *dev);
static int hp100_init_rxpdl(struct net_device *dev,
			    register hp100_ring_t * ringptr,
			    register u_int * pdlptr);
static int hp100_init_txpdl(struct net_device *dev,
			    register hp100_ring_t * ringptr,
			    register u_int * pdlptr);
static void hp100_rxfill(struct net_device *dev);
static void hp100_hwinit(struct net_device *dev);
static void hp100_clean_txring(struct net_device *dev);
#ifdef HP100_DEBUG
static void hp100_RegisterDump(struct net_device *dev);
#endif

/* Conversion to new PCI API :
 * Convert an address in a kernel buffer to a bus/phys/dma address.
 * This work *only* for memory fragments part of lp->page_vaddr,
 * because it was properly DMA allocated via pci_alloc_consistent(),
 * so we just need to "retrieve" the original mapping to bus/phys/dma
 * address - Jean II */
static inline dma_addr_t virt_to_whatever(struct net_device *dev, u32 * ptr)
{
	struct hp100_private *lp = netdev_priv(dev);
	return ((u_long) ptr) + lp->whatever_offset;
}

static inline u_int pdl_map_data(struct hp100_private *lp, void *data)
{
	return pci_map_single(lp->pci_dev, data,
			      MAX_ETHER_SIZE, PCI_DMA_FROMDEVICE);
}

/* TODO: This function should not really be needed in a good design... */
static void wait(void)
{
	mdelay(1);
}

/*
 *  probe functions
 *  These functions should - if possible - avoid doing write operations
 *  since this could cause problems when the card is not installed.
 */

/*
 * Read board id and convert to string.
 * Effectively same code as decode_eisa_sig
 */
static __devinit const char *hp100_read_id(int ioaddr)
{
	int i;
	static char str[HP100_SIG_LEN];
	unsigned char sig[4], sum;
        unsigned short rev;

	hp100_page(ID_MAC_ADDR);
	sum = 0;
	for (i = 0; i < 4; i++) {
		sig[i] = hp100_inb(BOARD_ID + i);
		sum += sig[i];
	}

	sum += hp100_inb(BOARD_ID + i);
	if (sum != 0xff)
		return NULL;	/* bad checksum */

        str[0] = ((sig[0] >> 2) & 0x1f) + ('A' - 1);
        str[1] = (((sig[0] & 3) << 3) | (sig[1] >> 5)) + ('A' - 1);
        str[2] = (sig[1] & 0x1f) + ('A' - 1);
        rev = (sig[2] << 8) | sig[3];
        sprintf(str + 3, "%04X", rev);

	return str;
}

#ifdef CONFIG_ISA
static __init int hp100_isa_probe1(struct net_device *dev, int ioaddr)
{
	const char *sig;
	int i;

	if (!request_region(ioaddr, HP100_REGION_SIZE, "hp100"))
		goto err;

	if (hp100_inw(HW_ID) != HP100_HW_ID_CASCADE) {
		release_region(ioaddr, HP100_REGION_SIZE);
		goto err;
	}

	sig = hp100_read_id(ioaddr);
	release_region(ioaddr, HP100_REGION_SIZE);

	if (sig == NULL)
		goto err;

	for (i = 0; i < ARRAY_SIZE(hp100_isa_tbl); i++) {
		if (!strcmp(hp100_isa_tbl[i], sig))
			break;

	}

	if (i < ARRAY_SIZE(hp100_isa_tbl))
		return hp100_probe1(dev, ioaddr, HP100_BUS_ISA, NULL);
 err:
	return -ENODEV;

}
/*
 * Probe for ISA board.
 * EISA and PCI are handled by device infrastructure.
 */

static int  __init hp100_isa_probe(struct net_device *dev, int addr)
{
	int err = -ENODEV;

	/* Probe for a specific ISA address */
	if (addr > 0xff && addr < 0x400)
		err = hp100_isa_probe1(dev, addr);

	else if (addr != 0)
		err = -ENXIO;

	else {
		/* Probe all ISA possible port regions */
		for (addr = 0x100; addr < 0x400; addr += 0x20) {
			err = hp100_isa_probe1(dev, addr);
			if (!err)
				break;
		}
	}
	return err;
}
#endif /* CONFIG_ISA */

#if !defined(MODULE) && defined(CONFIG_ISA)
struct net_device * __init hp100_probe(int unit)
{
	struct net_device *dev = alloc_etherdev(sizeof(struct hp100_private));
	int err;

	if (!dev)
		return ERR_PTR(-ENODEV);

	SET_MODULE_OWNER(dev);

#ifdef HP100_DEBUG_B
	hp100_outw(0x4200, TRACE);
	printk("hp100: %s: probe\n", dev->name);
#endif

	if (unit >= 0) {
		sprintf(dev->name, "eth%d", unit);
		netdev_boot_setup_check(dev);
	}

	err = hp100_isa_probe(dev, dev->base_addr);
	if (err)
		goto out;

	return dev;
 out:
	free_netdev(dev);
	return ERR_PTR(err);
}
#endif /* !MODULE && CONFIG_ISA */

static int __devinit hp100_probe1(struct net_device *dev, int ioaddr,
				  u_char bus, struct pci_dev *pci_dev)
{
	int i;
	int err = -ENODEV;
	const char *eid;
	u_int chip;
	u_char uc;
	u_int memory_size = 0, virt_memory_size = 0;
	u_short local_mode, lsw;
	short mem_mapped;
	unsigned long mem_ptr_phys;
	void __iomem *mem_ptr_virt;
	struct hp100_private *lp;

#ifdef HP100_DEBUG_B
	hp100_outw(0x4201, TRACE);
	printk("hp100: %s: probe1\n", dev->name);
#endif

	/* memory region for programmed i/o */
	if (!request_region(ioaddr, HP100_REGION_SIZE, "hp100"))
		goto out1;

	if (hp100_inw(HW_ID) != HP100_HW_ID_CASCADE)
		goto out2;

	chip = hp100_inw(PAGING) & HP100_CHIPID_MASK;
#ifdef HP100_DEBUG
	if (chip == HP100_CHIPID_SHASTA)
		printk("hp100: %s: Shasta Chip detected. (This is a pre 802.12 chip)\n", dev->name);
	else if (chip == HP100_CHIPID_RAINIER)
		printk("hp100: %s: Rainier Chip detected. (This is a pre 802.12 chip)\n", dev->name);
	else if (chip == HP100_CHIPID_LASSEN)
		printk("hp100: %s: Lassen Chip detected.\n", dev->name);
	else
		printk("hp100: %s: Warning: Unknown CASCADE chip (id=0x%.4x).\n", dev->name, chip);
#endif

	dev->base_addr = ioaddr;

	eid = hp100_read_id(ioaddr);
	if (eid == NULL) {	/* bad checksum? */
		printk(KERN_WARNING "hp100_probe: bad ID checksum at base port 0x%x\n", ioaddr);
		goto out2;
	}

	hp100_page(ID_MAC_ADDR);
	for (i = uc = 0; i < 7; i++)
		uc += hp100_inb(LAN_ADDR + i);
	if (uc != 0xff) {
		printk(KERN_WARNING "hp100_probe: bad lan address checksum at port 0x%x)\n", ioaddr);
		err = -EIO;
		goto out2;
	}

	/* Make sure, that all registers are correctly updated... */

	hp100_load_eeprom(dev, ioaddr);
	wait();

	/*
	 * Determine driver operation mode
	 *
	 * Use the variable "hp100_mode" upon insmod or as kernel parameter to
	 * force driver modes:
	 * hp100_mode=1 -> default, use busmaster mode if configured.
	 * hp100_mode=2 -> enable shared memory mode
	 * hp100_mode=3 -> force use of i/o mapped mode.
	 * hp100_mode=4 -> same as 1, but re-set the enable bit on the card.
	 */

	/*
	 * LSW values:
	 *   0x2278 -> J2585B, PnP shared memory mode
	 *   0x2270 -> J2585B, shared memory mode, 0xdc000
	 *   0xa23c -> J2585B, I/O mapped mode
	 *   0x2240 -> EISA COMPEX, BusMaster (Shasta Chip)
	 *   0x2220 -> EISA HP, I/O (Shasta Chip)
	 *   0x2260 -> EISA HP, BusMaster (Shasta Chip)
	 */

#if 0
	local_mode = 0x2270;
	hp100_outw(0xfefe, OPTION_LSW);
	hp100_outw(local_mode | HP100_SET_LB | HP100_SET_HB, OPTION_LSW);
#endif

	/* hp100_mode value maybe used in future by another card */
	local_mode = hp100_mode;
	if (local_mode < 1 || local_mode > 4)
		local_mode = 1;	/* default */
#ifdef HP100_DEBUG
	printk("hp100: %s: original LSW = 0x%x\n", dev->name,
	       hp100_inw(OPTION_LSW));
#endif

	if (local_mode == 3) {
		hp100_outw(HP100_MEM_EN | HP100_RESET_LB, OPTION_LSW);
		hp100_outw(HP100_IO_EN | HP100_SET_LB, OPTION_LSW);
		hp100_outw(HP100_BM_WRITE | HP100_BM_READ | HP100_RESET_HB, OPTION_LSW);
		printk("hp100: IO mapped mode forced.\n");
	} else if (local_mode == 2) {
		hp100_outw(HP100_MEM_EN | HP100_SET_LB, OPTION_LSW);
		hp100_outw(HP100_IO_EN | HP100_SET_LB, OPTION_LSW);
		hp100_outw(HP100_BM_WRITE | HP100_BM_READ | HP100_RESET_HB, OPTION_LSW);
		printk("hp100: Shared memory mode requested.\n");
	} else if (local_mode == 4) {
		if (chip == HP100_CHIPID_LASSEN) {
			hp100_outw(HP100_BM_WRITE | HP100_BM_READ | HP100_SET_HB, OPTION_LSW);
			hp100_outw(HP100_IO_EN | HP100_MEM_EN | HP100_RESET_LB, OPTION_LSW);
			printk("hp100: Busmaster mode requested.\n");
		}
		local_mode = 1;
	}

	if (local_mode == 1) {	/* default behaviour */
		lsw = hp100_inw(OPTION_LSW);

		if ((lsw & HP100_IO_EN) && (~lsw & HP100_MEM_EN) &&
		    (~lsw & (HP100_BM_WRITE | HP100_BM_READ))) {
#ifdef HP100_DEBUG
			printk("hp100: %s: IO_EN bit is set on card.\n", dev->name);
#endif
			local_mode = 3;
		} else if (chip == HP100_CHIPID_LASSEN &&
			   (lsw & (HP100_BM_WRITE | HP100_BM_READ)) == (HP100_BM_WRITE | HP100_BM_READ)) {
			/* Conversion to new PCI API :
			 * I don't have the doc, but I assume that the card
			 * can map the full 32bit address space.
			 * Also, we can have EISA Busmaster cards (not tested),
			 * so beware !!! - Jean II */
			if((bus == HP100_BUS_PCI) &&
			   (pci_set_dma_mask(pci_dev, DMA_32BIT_MASK))) {
				/* Gracefully fallback to shared memory */
				goto busmasterfail;
			}
			printk("hp100: Busmaster mode enabled.\n");
			hp100_outw(HP100_MEM_EN | HP100_IO_EN | HP100_RESET_LB, OPTION_LSW);
		} else {
		busmasterfail:
#ifdef HP100_DEBUG
			printk("hp100: %s: Card not configured for BM or BM not supported with this card.\n", dev->name);
			printk("hp100: %s: Trying shared memory mode.\n", dev->name);
#endif
			/* In this case, try shared memory mode */
			local_mode = 2;
			hp100_outw(HP100_MEM_EN | HP100_SET_LB, OPTION_LSW);
			/* hp100_outw(HP100_IO_EN|HP100_RESET_LB, OPTION_LSW); */
		}
	}
#ifdef HP100_DEBUG
	printk("hp100: %s: new LSW = 0x%x\n", dev->name, hp100_inw(OPTION_LSW));
#endif

	/* Check for shared memory on the card, eventually remap it */
	hp100_page(HW_MAP);
	mem_mapped = ((hp100_inw(OPTION_LSW) & (HP100_MEM_EN)) != 0);
	mem_ptr_phys = 0UL;
	mem_ptr_virt = NULL;
	memory_size = (8192 << ((hp100_inb(SRAM) >> 5) & 0x07));
	virt_memory_size = 0;

	/* For memory mapped or busmaster mode, we want the memory address */
	if (mem_mapped || (local_mode == 1)) {
		mem_ptr_phys = (hp100_inw(MEM_MAP_LSW) | (hp100_inw(MEM_MAP_MSW) << 16));
		mem_ptr_phys &= ~0x1fff;	/* 8k alignment */

		if (bus == HP100_BUS_ISA && (mem_ptr_phys & ~0xfffff) != 0) {
			printk("hp100: Can only use programmed i/o mode.\n");
			mem_ptr_phys = 0;
			mem_mapped = 0;
			local_mode = 3;	/* Use programmed i/o */
		}

		/* We do not need access to shared memory in busmaster mode */
		/* However in slave mode we need to remap high (>1GB) card memory  */
		if (local_mode != 1) {	/* = not busmaster */
			/* We try with smaller memory sizes, if ioremap fails */
			for (virt_memory_size = memory_size; virt_memory_size > 16383; virt_memory_size >>= 1) {
				if ((mem_ptr_virt = ioremap((u_long) mem_ptr_phys, virt_memory_size)) == NULL) {
#ifdef HP100_DEBUG
					printk("hp100: %s: ioremap for 0x%x bytes high PCI memory at 0x%lx failed\n", dev->name, virt_memory_size, mem_ptr_phys);
#endif
				} else {
#ifdef HP100_DEBUG
					printk("hp100: %s: remapped 0x%x bytes high PCI memory at 0x%lx to %p.\n", dev->name, virt_memory_size, mem_ptr_phys, mem_ptr_virt);
#endif
					break;
				}
			}

			if (mem_ptr_virt == NULL) {	/* all ioremap tries failed */
				printk("hp100: Failed to ioremap the PCI card memory. Will have to use i/o mapped mode.\n");
				local_mode = 3;
				virt_memory_size = 0;
			}
		}
	}

	if (local_mode == 3) {	/* io mapped forced */
		mem_mapped = 0;
		mem_ptr_phys = 0;
		mem_ptr_virt = NULL;
		printk("hp100: Using (slow) programmed i/o mode.\n");
	}

	/* Initialise the "private" data structure for this card. */
	lp = netdev_priv(dev);

	spin_lock_init(&lp->lock);
	strlcpy(lp->id, eid, HP100_SIG_LEN);
	lp->chip = chip;
	lp->mode = local_mode;
	lp->bus = bus;
	lp->pci_dev = pci_dev;
	lp->priority_tx = hp100_priority_tx;
	lp->rx_ratio = hp100_rx_ratio;
	lp->mem_ptr_phys = mem_ptr_phys;
	lp->mem_ptr_virt = mem_ptr_virt;
	hp100_page(ID_MAC_ADDR);
	lp->soft_model = hp100_inb(SOFT_MODEL);
	lp->mac1_mode = HP100_MAC1MODE3;
	lp->mac2_mode = HP100_MAC2MODE3;
	memset(&lp->hash_bytes, 0x00, 8);

	dev->base_addr = ioaddr;

	lp->memory_size = memory_size;
	lp->virt_memory_size = virt_memory_size;
	lp->rx_ratio = hp100_rx_ratio;	/* can be conf'd with insmod */

	dev->open = hp100_open;
	dev->stop = hp100_close;

	if (lp->mode == 1)	/* busmaster */
		dev->hard_start_xmit = hp100_start_xmit_bm;
	else
		dev->hard_start_xmit = hp100_start_xmit;

	dev->get_stats = hp100_get_stats;
	dev->set_multicast_list = &hp100_set_multicast_list;

	/* Ask the card for which IRQ line it is configured */
	if (bus == HP100_BUS_PCI) {
		dev->irq = pci_dev->irq;
	} else {
		hp100_page(HW_MAP);
		dev->irq = hp100_inb(IRQ_CHANNEL) & HP100_IRQMASK;
		if (dev->irq == 2)
			dev->irq = 9;
	}

	if (lp->mode == 1)	/* busmaster */
		dev->dma = 4;

	/* Ask the card for its MAC address and store it for later use. */
	hp100_page(ID_MAC_ADDR);
	for (i = uc = 0; i < 6; i++)
		dev->dev_addr[i] = hp100_inb(LAN_ADDR + i);

	/* Reset statistics (counters) */
	hp100_clear_stats(lp, ioaddr);

	/* If busmaster mode is wanted, a dma-capable memory area is needed for
	 * the rx and tx PDLs
	 * PCI cards can access the whole PC memory. Therefore GFP_DMA is not
	 * needed for the allocation of the memory area.
	 */

	/* TODO: We do not need this with old cards, where PDLs are stored
	 * in the cards shared memory area. But currently, busmaster has been
	 * implemented/tested only with the lassen chip anyway... */
	if (lp->mode == 1) {	/* busmaster */
		dma_addr_t page_baddr;
		/* Get physically continous memory for TX & RX PDLs    */
		/* Conversion to new PCI API :
		 * Pages are always aligned and zeroed, no need to it ourself.
		 * Doc says should be OK for EISA bus as well - Jean II */
		if ((lp->page_vaddr_algn = pci_alloc_consistent(lp->pci_dev, MAX_RINGSIZE, &page_baddr)) == NULL) {
			err = -ENOMEM;
			goto out2;
		}
		lp->whatever_offset = ((u_long) page_baddr) - ((u_long) lp->page_vaddr_algn);

#ifdef HP100_DEBUG_BM
		printk("hp100: %s: Reserved DMA memory from 0x%x to 0x%x\n", dev->name, (u_int) lp->page_vaddr_algn, (u_int) lp->page_vaddr_algn + MAX_RINGSIZE);
#endif
		lp->rxrcommit = lp->txrcommit = 0;
		lp->rxrhead = lp->rxrtail = &(lp->rxring[0]);
		lp->txrhead = lp->txrtail = &(lp->txring[0]);
	}

	/* Initialise the card. */
	/* (I'm not really sure if it's a good idea to do this during probing, but
	 * like this it's assured that the lan connection type can be sensed
	 * correctly)
	 */
	hp100_hwinit(dev);

	/* Try to find out which kind of LAN the card is connected to. */
	lp->lan_type = hp100_sense_lan(dev);

	/* Print out a message what about what we think we have probed. */
	printk("hp100: at 0x%x, IRQ %d, ", ioaddr, dev->irq);
	switch (bus) {
	case HP100_BUS_EISA:
		printk("EISA");
		break;
	case HP100_BUS_PCI:
		printk("PCI");
		break;
	default:
		printk("ISA");
		break;
	}
	printk(" bus, %dk SRAM (rx/tx %d%%).\n", lp->memory_size >> 10, lp->rx_ratio);

	if (lp->mode == 2) {	/* memory mapped */
		printk("hp100: Memory area at 0x%lx-0x%lx", mem_ptr_phys,
				(mem_ptr_phys + (mem_ptr_phys > 0x100000 ? (u_long) lp->memory_size : 16 * 1024)) - 1);
		if (mem_ptr_virt)
			printk(" (virtual base %p)", mem_ptr_virt);
		printk(".\n");

		/* Set for info when doing ifconfig */
		dev->mem_start = mem_ptr_phys;
		dev->mem_end = mem_ptr_phys + lp->memory_size;
	}

	printk("hp100: ");
	if (lp->lan_type != HP100_LAN_ERR)
		printk("Adapter is attached to ");
	switch (lp->lan_type) {
	case HP100_LAN_100:
		printk("100Mb/s Voice Grade AnyLAN network.\n");
		break;
	case HP100_LAN_10:
		printk("10Mb/s network (10baseT).\n");
		break;
	case HP100_LAN_COAX:
		printk("10Mb/s network (coax).\n");
		break;
	default:
		printk("Warning! Link down.\n");
	}

	err = register_netdev(dev);
	if (err)
		goto out3;

	return 0;
out3:
	if (local_mode == 1)
		pci_free_consistent(lp->pci_dev, MAX_RINGSIZE + 0x0f,
				    lp->page_vaddr_algn,
				    virt_to_whatever(dev, lp->page_vaddr_algn));
	if (mem_ptr_virt)
		iounmap(mem_ptr_virt);
out2:
	release_region(ioaddr, HP100_REGION_SIZE);
out1:
	return err;
}

/* This procedure puts the card into a stable init state */
static void hp100_hwinit(struct net_device *dev)
{
	int ioaddr = dev->base_addr;
	struct hp100_private *lp = netdev_priv(dev);

#ifdef HP100_DEBUG_B
	hp100_outw(0x4202, TRACE);
	printk("hp100: %s: hwinit\n", dev->name);
#endif

	/* Initialise the card. -------------------------------------------- */

	/* Clear all pending Ints and disable Ints */
	hp100_page(PERFORMANCE);
	hp100_outw(0xfefe, IRQ_MASK);	/* mask off all ints */
	hp100_outw(0xffff, IRQ_STATUS);	/* clear all pending ints */

	hp100_outw(HP100_INT_EN | HP100_RESET_LB, OPTION_LSW);
	hp100_outw(HP100_TRI_INT | HP100_SET_HB, OPTION_LSW);

	if (lp->mode == 1) {
		hp100_BM_shutdown(dev);	/* disables BM, puts cascade in reset */
		wait();
	} else {
		hp100_outw(HP100_INT_EN | HP100_RESET_LB, OPTION_LSW);
		hp100_cascade_reset(dev, 1);
		hp100_page(MAC_CTRL);
		hp100_andb(~(HP100_RX_EN | HP100_TX_EN), MAC_CFG_1);
	}

	/* Initiate EEPROM reload */
	hp100_load_eeprom(dev, 0);

	wait();

	/* Go into reset again. */
	hp100_cascade_reset(dev, 1);

	/* Set Option Registers to a safe state  */
	hp100_outw(HP100_DEBUG_EN |
		   HP100_RX_HDR |
		   HP100_EE_EN |
		   HP100_BM_WRITE |
		   HP100_BM_READ | HP100_RESET_HB |
		   HP100_FAKE_INT |
		   HP100_INT_EN |
		   HP100_MEM_EN |
		   HP100_IO_EN | HP100_RESET_LB, OPTION_LSW);

	hp100_outw(HP100_TRI_INT |
		   HP100_MMAP_DIS | HP100_SET_HB, OPTION_LSW);

	hp100_outb(HP100_PRIORITY_TX |
		   HP100_ADV_NXT_PKT |
		   HP100_TX_CMD | HP100_RESET_LB, OPTION_MSW);

	/* TODO: Configure MMU for Ram Test. */
	/* TODO: Ram Test. */

	/* Re-check if adapter is still at same i/o location      */
	/* (If the base i/o in eeprom has been changed but the    */
	/* registers had not been changed, a reload of the eeprom */
	/* would move the adapter to the address stored in eeprom */

	/* TODO: Code to implement. */

	/* Until here it was code from HWdiscover procedure. */
	/* Next comes code from mmuinit procedure of SCO BM driver which is
	 * called from HWconfigure in the SCO driver.  */

	/* Initialise MMU, eventually switch on Busmaster Mode, initialise
	 * multicast filter...
	 */
	hp100_mmuinit(dev);

	/* We don't turn the interrupts on here - this is done by start_interface. */
	wait();			/* TODO: Do we really need this? */

	/* Enable Hardware (e.g. unreset) */
	hp100_cascade_reset(dev, 0);

	/* ------- initialisation complete ----------- */

	/* Finally try to log in the Hub if there may be a VG connection. */
	if ((lp->lan_type == HP100_LAN_100) || (lp->lan_type == HP100_LAN_ERR))
		hp100_login_to_vg_hub(dev, 0);	/* relogin */

}


/*
 * mmuinit - Reinitialise Cascade MMU and MAC settings.
 * Note: Must already be in reset and leaves card in reset.
 */
static void hp100_mmuinit(struct net_device *dev)
{
	int ioaddr = dev->base_addr;
	struct hp100_private *lp = netdev_priv(dev);
	int i;

#ifdef HP100_DEBUG_B
	hp100_outw(0x4203, TRACE);
	printk("hp100: %s: mmuinit\n", dev->name);
#endif

#ifdef HP100_DEBUG
	if (0 != (hp100_inw(OPTION_LSW) & HP100_HW_RST)) {
		printk("hp100: %s: Not in reset when entering mmuinit. Fix me.\n", dev->name);
		return;
	}
#endif

	/* Make sure IRQs are masked off and ack'ed. */
	hp100_page(PERFORMANCE);
	hp100_outw(0xfefe, IRQ_MASK);	/* mask off all ints */
	hp100_outw(0xffff, IRQ_STATUS);	/* ack IRQ */

	/*
	 * Enable Hardware
	 * - Clear Debug En, Rx Hdr Pipe, EE En, I/O En, Fake Int and Intr En
	 * - Set Tri-State Int, Bus Master Rd/Wr, and Mem Map Disable
	 * - Clear Priority, Advance Pkt and Xmit Cmd
	 */

	hp100_outw(HP100_DEBUG_EN |
		   HP100_RX_HDR |
		   HP100_EE_EN | HP100_RESET_HB |
		   HP100_IO_EN |
		   HP100_FAKE_INT |
		   HP100_INT_EN | HP100_RESET_LB, OPTION_LSW);

	hp100_outw(HP100_TRI_INT | HP100_SET_HB, OPTION_LSW);

	if (lp->mode == 1) {	/* busmaster */
		hp100_outw(HP100_BM_WRITE |
			   HP100_BM_READ |
			   HP100_MMAP_DIS | HP100_SET_HB, OPTION_LSW);
	} else if (lp->mode == 2) {	/* memory mapped */
		hp100_outw(HP100_BM_WRITE |
			   HP100_BM_READ | HP100_RESET_HB, OPTION_LSW);
		hp100_outw(HP100_MMAP_DIS | HP100_RESET_HB, OPTION_LSW);
		hp100_outw(HP100_MEM_EN | HP100_SET_LB, OPTION_LSW);
		hp100_outw(HP100_IO_EN | HP100_SET_LB, OPTION_LSW);
	} else if (lp->mode == 3) {	/* i/o mapped mode */
		hp100_outw(HP100_MMAP_DIS | HP100_SET_HB |
			   HP100_IO_EN | HP100_SET_LB, OPTION_LSW);
	}

	hp100_page(HW_MAP);
	hp100_outb(0, EARLYRXCFG);
	hp100_outw(0, EARLYTXCFG);

	/*
	 * Enable Bus Master mode
	 */
	if (lp->mode == 1) {	/* busmaster */
		/* Experimental: Set some PCI configuration bits */
		hp100_page(HW_MAP);
		hp100_andb(~HP100_PDL_USE3, MODECTRL1);	/* BM engine read maximum */
		hp100_andb(~HP100_TX_DUALQ, MODECTRL1);	/* No Queue for Priority TX */

		/* PCI Bus failures should result in a Misc. Interrupt */
		hp100_orb(HP100_EN_BUS_FAIL, MODECTRL2);

		hp100_outw(HP100_BM_READ | HP100_BM_WRITE | HP100_SET_HB, OPTION_LSW);
		hp100_page(HW_MAP);
		/* Use Burst Mode and switch on PAGE_CK */
		hp100_orb(HP100_BM_BURST_RD | HP100_BM_BURST_WR, BM);
		if ((lp->chip == HP100_CHIPID_RAINIER) || (lp->chip == HP100_CHIPID_SHASTA))
			hp100_orb(HP100_BM_PAGE_CK, BM);
		hp100_orb(HP100_BM_MASTER, BM);
	} else {		/* not busmaster */

		hp100_page(HW_MAP);
		hp100_andb(~HP100_BM_MASTER, BM);
	}

	/*
	 * Divide card memory into regions for Rx, Tx and, if non-ETR chip, PDLs
	 */
	hp100_page(MMU_CFG);
	if (lp->mode == 1) {	/* only needed for Busmaster */
		int xmit_stop, recv_stop;

		if ((lp->chip == HP100_CHIPID_RAINIER)
		    || (lp->chip == HP100_CHIPID_SHASTA)) {
			int pdl_stop;

			/*
			 * Each pdl is 508 bytes long. (63 frags * 4 bytes for address and
			 * 4 bytes for header). We will leave NUM_RXPDLS * 508 (rounded
			 * to the next higher 1k boundary) bytes for the rx-pdl's
			 * Note: For non-etr chips the transmit stop register must be
			 * programmed on a 1k boundary, i.e. bits 9:0 must be zero.
			 */
			pdl_stop = lp->memory_size;
			xmit_stop = (pdl_stop - 508 * (MAX_RX_PDL) - 16) & ~(0x03ff);
			recv_stop = (xmit_stop * (lp->rx_ratio) / 100) & ~(0x03ff);
			hp100_outw((pdl_stop >> 4) - 1, PDL_MEM_STOP);
#ifdef HP100_DEBUG_BM
			printk("hp100: %s: PDL_STOP = 0x%x\n", dev->name, pdl_stop);
#endif
		} else {
			/* ETR chip (Lassen) in busmaster mode */
			xmit_stop = (lp->memory_size) - 1;
			recv_stop = ((lp->memory_size * lp->rx_ratio) / 100) & ~(0x03ff);
		}

		hp100_outw(xmit_stop >> 4, TX_MEM_STOP);
		hp100_outw(recv_stop >> 4, RX_MEM_STOP);
#ifdef HP100_DEBUG_BM
		printk("hp100: %s: TX_STOP  = 0x%x\n", dev->name, xmit_stop >> 4);
		printk("hp100: %s: RX_STOP  = 0x%x\n", dev->name, recv_stop >> 4);
#endif
	} else {
		/* Slave modes (memory mapped and programmed io)  */
		hp100_outw((((lp->memory_size * lp->rx_ratio) / 100) >> 4), RX_MEM_STOP);
		hp100_outw(((lp->memory_size - 1) >> 4), TX_MEM_STOP);
#ifdef HP100_DEBUG
		printk("hp100: %s: TX_MEM_STOP: 0x%x\n", dev->name, hp100_inw(TX_MEM_STOP));
		printk("hp100: %s: RX_MEM_STOP: 0x%x\n", dev->name, hp100_inw(RX_MEM_STOP));
#endif
	}

	/* Write MAC address into page 1 */
	hp100_page(MAC_ADDRESS);
	for (i = 0; i < 6; i++)
		hp100_outb(dev->dev_addr[i], MAC_ADDR + i);

	/* Zero the multicast hash registers */
	for (i = 0; i < 8; i++)
		hp100_outb(0x0, HASH_BYTE0 + i);

	/* Set up MAC defaults */
	hp100_page(MAC_CTRL);

	/* Go to LAN Page and zero all filter bits */
	/* Zero accept error, accept multicast, accept broadcast and accept */
	/* all directed packet bits */
	hp100_andb(~(HP100_RX_EN |
		     HP100_TX_EN |
		     HP100_ACC_ERRORED |
		     HP100_ACC_MC |
		     HP100_ACC_BC | HP100_ACC_PHY), MAC_CFG_1);

	hp100_outb(0x00, MAC_CFG_2);

	/* Zero the frame format bit. This works around a training bug in the */
	/* new hubs. */
	hp100_outb(0x00, VG_LAN_CFG_2);	/* (use 802.3) */

	if (lp->priority_tx)
		hp100_outb(HP100_PRIORITY_TX | HP100_SET_LB, OPTION_MSW);
	else
		hp100_outb(HP100_PRIORITY_TX | HP100_RESET_LB, OPTION_MSW);

	hp100_outb(HP100_ADV_NXT_PKT |
		   HP100_TX_CMD | HP100_RESET_LB, OPTION_MSW);

	/* If busmaster, initialize the PDLs */
	if (lp->mode == 1)
		hp100_init_pdls(dev);

	/* Go to performance page and initalize isr and imr registers */
	hp100_page(PERFORMANCE);
	hp100_outw(0xfefe, IRQ_MASK);	/* mask off all ints */
	hp100_outw(0xffff, IRQ_STATUS);	/* ack IRQ */
}

/*
 *  open/close functions
 */

static int hp100_open(struct net_device *dev)
{
	struct hp100_private *lp = netdev_priv(dev);
#ifdef HP100_DEBUG_B
	int ioaddr = dev->base_addr;
#endif

#ifdef HP100_DEBUG_B
	hp100_outw(0x4204, TRACE);
	printk("hp100: %s: open\n", dev->name);
#endif

	/* New: if bus is PCI or EISA, interrupts might be shared interrupts */
	if (request_irq(dev->irq, hp100_interrupt,
			lp->bus == HP100_BUS_PCI || lp->bus ==
			HP100_BUS_EISA ? IRQF_SHARED : IRQF_DISABLED,
			"hp100", dev)) {
		printk("hp100: %s: unable to get IRQ %d\n", dev->name, dev->irq);
		return -EAGAIN;
	}

	dev->trans_start = jiffies;
	netif_start_queue(dev);

	lp->lan_type = hp100_sense_lan(dev);
	lp->mac1_mode = HP100_MAC1MODE3;
	lp->mac2_mode = HP100_MAC2MODE3;
	memset(&lp->hash_bytes, 0x00, 8);

	hp100_stop_interface(dev);

	hp100_hwinit(dev);

	hp100_start_interface(dev);	/* sets mac modes, enables interrupts */

	return 0;
}

/* The close function is called when the interface is to be brought down */
static int hp100_close(struct net_device *dev)
{
	int ioaddr = dev->base_addr;
	struct hp100_private *lp = netdev_priv(dev);

#ifdef HP100_DEBUG_B
	hp100_outw(0x4205, TRACE);
	printk("hp100: %s: close\n", dev->name);
#endif

	hp100_page(PERFORMANCE);
	hp100_outw(0xfefe, IRQ_MASK);	/* mask off all IRQs */

	hp100_stop_interface(dev);

	if (lp->lan_type == HP100_LAN_100)
		lp->hub_status = hp100_login_to_vg_hub(dev, 0);

	netif_stop_queue(dev);

	free_irq(dev->irq, dev);

#ifdef HP100_DEBUG
	printk("hp100: %s: close LSW = 0x%x\n", dev->name,
	       hp100_inw(OPTION_LSW));
#endif

	return 0;
}


/*
 * Configure the PDL Rx rings and LAN
 */
static void hp100_init_pdls(struct net_device *dev)
{
	struct hp100_private *lp = netdev_priv(dev);
	hp100_ring_t *ringptr;
	u_int *pageptr;		/* Warning : increment by 4 - Jean II */
	int i;

#ifdef HP100_DEBUG_B
	int ioaddr = dev->base_addr;
#endif

#ifdef HP100_DEBUG_B
	hp100_outw(0x4206, TRACE);
	printk("hp100: %s: init pdls\n", dev->name);
#endif

	if (0 == lp->page_vaddr_algn)
		printk("hp100: %s: Warning: lp->page_vaddr_algn not initialised!\n", dev->name);
	else {
		/* pageptr shall point into the DMA accessible memory region  */
		/* we use this pointer to status the upper limit of allocated */
		/* memory in the allocated page. */
		/* note: align the pointers to the pci cache line size */
		memset(lp->page_vaddr_algn, 0, MAX_RINGSIZE);	/* Zero  Rx/Tx ring page */
		pageptr = lp->page_vaddr_algn;

		lp->rxrcommit = 0;
		ringptr = lp->rxrhead = lp->rxrtail = &(lp->rxring[0]);

		/* Initialise Rx Ring */
		for (i = MAX_RX_PDL - 1; i >= 0; i--) {
			lp->rxring[i].next = ringptr;
			ringptr = &(lp->rxring[i]);
			pageptr += hp100_init_rxpdl(dev, ringptr, pageptr);
		}

		/* Initialise Tx Ring */
		lp->txrcommit = 0;
		ringptr = lp->txrhead = lp->txrtail = &(lp->txring[0]);
		for (i = MAX_TX_PDL - 1; i >= 0; i--) {
			lp->txring[i].next = ringptr;
			ringptr = &(lp->txring[i]);
			pageptr += hp100_init_txpdl(dev, ringptr, pageptr);
		}
	}
}


/* These functions "format" the entries in the pdl structure   */
/* They return how much memory the fragments need.            */
static int hp100_init_rxpdl(struct net_device *dev,
			    register hp100_ring_t * ringptr,
			    register u32 * pdlptr)
{
	/* pdlptr is starting address for this pdl */

	if (0 != (((unsigned long) pdlptr) & 0xf))
		printk("hp100: %s: Init rxpdl: Unaligned pdlptr 0x%lx.\n",
		       dev->name, (unsigned long) pdlptr);

	ringptr->pdl = pdlptr + 1;
	ringptr->pdl_paddr = virt_to_whatever(dev, pdlptr + 1);
	ringptr->skb = (void *) NULL;

	/*
	 * Write address and length of first PDL Fragment (which is used for
	 * storing the RX-Header
	 * We use the 4 bytes _before_ the PDH in the pdl memory area to
	 * store this information. (PDH is at offset 0x04)
	 */
	/* Note that pdlptr+1 and not pdlptr is the pointer to the PDH */

	*(pdlptr + 2) = (u_int) virt_to_whatever(dev, pdlptr);	/* Address Frag 1 */
	*(pdlptr + 3) = 4;	/* Length  Frag 1 */

	return ((((MAX_RX_FRAG * 2 + 2) + 3) / 4) * 4);
}


static int hp100_init_txpdl(struct net_device *dev,
			    register hp100_ring_t * ringptr,
			    register u32 * pdlptr)
{
	if (0 != (((unsigned long) pdlptr) & 0xf))
		printk("hp100: %s: Init txpdl: Unaligned pdlptr 0x%lx.\n", dev->name, (unsigned long) pdlptr);

	ringptr->pdl = pdlptr;	/* +1; */
	ringptr->pdl_paddr = virt_to_whatever(dev, pdlptr);	/* +1 */
	ringptr->skb = (void *) NULL;

	return ((((MAX_TX_FRAG * 2 + 2) + 3) / 4) * 4);
}

/*
 * hp100_build_rx_pdl allocates an skb_buff of maximum size plus two bytes
 * for possible odd word alignment rounding up to next dword and set PDL
 * address for fragment#2
 * Returns: 0 if unable to allocate skb_buff
 *          1 if successful
 */
static int hp100_build_rx_pdl(hp100_ring_t * ringptr,
			      struct net_device *dev)
{
#ifdef HP100_DEBUG_B
	int ioaddr = dev->base_addr;
#endif
#ifdef HP100_DEBUG_BM
	u_int *p;
#endif

#ifdef HP100_DEBUG_B
	hp100_outw(0x4207, TRACE);
	printk("hp100: %s: build rx pdl\n", dev->name);
#endif

	/* Allocate skb buffer of maximum size */
	/* Note: This depends on the alloc_skb functions allocating more
	 * space than requested, i.e. aligning to 16bytes */

	ringptr->skb = dev_alloc_skb(((MAX_ETHER_SIZE + 2 + 3) / 4) * 4);

	if (NULL != ringptr->skb) {
		/*
		 * Reserve 2 bytes at the head of the buffer to land the IP header
		 * on a long word boundary (According to the Network Driver section
		 * in the Linux KHG, this should help to increase performance.)
		 */
		skb_reserve(ringptr->skb, 2);

		ringptr->skb->dev = dev;
		ringptr->skb->data = (u_char *) skb_put(ringptr->skb, MAX_ETHER_SIZE);

		/* ringptr->pdl points to the beginning of the PDL, i.e. the PDH */
		/* Note: 1st Fragment is used for the 4 byte packet status
		 * (receive header). Its PDL entries are set up by init_rxpdl. So
		 * here we only have to set up the PDL fragment entries for the data
		 * part. Those 4 bytes will be stored in the DMA memory region
		 * directly before the PDL.
		 */
#ifdef HP100_DEBUG_BM
		printk("hp100: %s: build_rx_pdl: PDH@0x%x, skb->data (len %d) at 0x%x\n",
				     dev->name, (u_int) ringptr->pdl,
				     ((MAX_ETHER_SIZE + 2 + 3) / 4) * 4,
				     (unsigned int) ringptr->skb->data);
#endif

		/* Conversion to new PCI API : map skbuf data to PCI bus.
		 * Doc says it's OK for EISA as well - Jean II */
		ringptr->pdl[0] = 0x00020000;	/* Write PDH */
		ringptr->pdl[3] = pdl_map_data(netdev_priv(dev),
					       ringptr->skb->data);
		ringptr->pdl[4] = MAX_ETHER_SIZE;	/* Length of Data */

#ifdef HP100_DEBUG_BM
		for (p = (ringptr->pdl); p < (ringptr->pdl + 5); p++)
			printk("hp100: %s: Adr 0x%.8x = 0x%.8x\n", dev->name, (u_int) p, (u_int) * p);
#endif
		return (1);
	}
	/* else: */
	/* alloc_skb failed (no memory) -> still can receive the header
	 * fragment into PDL memory. make PDL safe by clearing msgptr and
	 * making the PDL only 1 fragment (i.e. the 4 byte packet status)
	 */
#ifdef HP100_DEBUG_BM
	printk("hp100: %s: build_rx_pdl: PDH@0x%x, No space for skb.\n", dev->name, (u_int) ringptr->pdl);
#endif

	ringptr->pdl[0] = 0x00010000;	/* PDH: Count=1 Fragment */

	return (0);
}

/*
 *  hp100_rxfill - attempt to fill the Rx Ring will empty skb's
 *
 * Makes assumption that skb's are always contiguous memory areas and
 * therefore PDLs contain only 2 physical fragments.
 * -  While the number of Rx PDLs with buffers is less than maximum
 *      a.  Get a maximum packet size skb
 *      b.  Put the physical address of the buffer into the PDL.
 *      c.  Output physical address of PDL to adapter.
 */
static void hp100_rxfill(struct net_device *dev)
{
	int ioaddr = dev->base_addr;

	struct hp100_private *lp = netdev_priv(dev);
	hp100_ring_t *ringptr;

#ifdef HP100_DEBUG_B
	hp100_outw(0x4208, TRACE);
	printk("hp100: %s: rxfill\n", dev->name);
#endif

	hp100_page(PERFORMANCE);

	while (lp->rxrcommit < MAX_RX_PDL) {
		/*
		   ** Attempt to get a buffer and build a Rx PDL.
		 */
		ringptr = lp->rxrtail;
		if (0 == hp100_build_rx_pdl(ringptr, dev)) {
			return;	/* None available, return */
		}

		/* Hand this PDL over to the card */
		/* Note: This needs performance page selected! */
#ifdef HP100_DEBUG_BM
		printk("hp100: %s: rxfill: Hand to card: pdl #%d @0x%x phys:0x%x, buffer: 0x%x\n",
				     dev->name, lp->rxrcommit, (u_int) ringptr->pdl,
				     (u_int) ringptr->pdl_paddr, (u_int) ringptr->pdl[3]);
#endif

		hp100_outl((u32) ringptr->pdl_paddr, RX_PDA);

		lp->rxrcommit += 1;
		lp->rxrtail = ringptr->next;
	}
}

/*
 * BM_shutdown - shutdown bus mastering and leave chip in reset state
 */

static void hp100_BM_shutdown(struct net_device *dev)
{
	int ioaddr = dev->base_addr;
	struct hp100_private *lp = netdev_priv(dev);
	unsigned long time;

#ifdef HP100_DEBUG_B
	hp100_outw(0x4209, TRACE);
	printk("hp100: %s: bm shutdown\n", dev->name);
#endif

	hp100_page(PERFORMANCE);
	hp100_outw(0xfefe, IRQ_MASK);	/* mask off all ints */
	hp100_outw(0xffff, IRQ_STATUS);	/* Ack all ints */

	/* Ensure Interrupts are off */
	hp100_outw(HP100_INT_EN | HP100_RESET_LB, OPTION_LSW);

	/* Disable all MAC activity */
	hp100_page(MAC_CTRL);
	hp100_andb(~(HP100_RX_EN | HP100_TX_EN), MAC_CFG_1);	/* stop rx/tx */

	/* If cascade MMU is not already in reset */
	if (0 != (hp100_inw(OPTION_LSW) & HP100_HW_RST)) {
		/* Wait 1.3ms (10Mb max packet time) to ensure MAC is idle so
		 * MMU pointers will not be reset out from underneath
		 */
		hp100_page(MAC_CTRL);
		for (time = 0; time < 5000; time++) {
			if ((hp100_inb(MAC_CFG_1) & (HP100_TX_IDLE | HP100_RX_IDLE)) == (HP100_TX_IDLE | HP100_RX_IDLE))
				break;
		}

		/* Shutdown algorithm depends on the generation of Cascade */
		if (lp->chip == HP100_CHIPID_LASSEN) {	/* ETR shutdown/reset */
			/* Disable Busmaster mode and wait for bit to go to zero. */
			hp100_page(HW_MAP);
			hp100_andb(~HP100_BM_MASTER, BM);
			/* 100 ms timeout */
			for (time = 0; time < 32000; time++) {
				if (0 == (hp100_inb(BM) & HP100_BM_MASTER))
					break;
			}
		} else {	/* Shasta or Rainier Shutdown/Reset */
			/* To ensure all bus master inloading activity has ceased,
			 * wait for no Rx PDAs or no Rx packets on card.
			 */
			hp100_page(PERFORMANCE);
			/* 100 ms timeout */
			for (time = 0; time < 10000; time++) {
				/* RX_PDL: PDLs not executed. */
				/* RX_PKT_CNT: RX'd packets on card. */
				if ((hp100_inb(RX_PDL) == 0) && (hp100_inb(RX_PKT_CNT) == 0))
					break;
			}

			if (time >= 10000)
				printk("hp100: %s: BM shutdown error.\n", dev->name);

			/* To ensure all bus master outloading activity has ceased,
			 * wait until the Tx PDA count goes to zero or no more Tx space
			 * available in the Tx region of the card.
			 */
			/* 100 ms timeout */
			for (time = 0; time < 10000; time++) {
				if ((0 == hp100_inb(TX_PKT_CNT)) &&
				    (0 != (hp100_inb(TX_MEM_FREE) & HP100_AUTO_COMPARE)))
					break;
			}

			/* Disable Busmaster mode */
			hp100_page(HW_MAP);
			hp100_andb(~HP100_BM_MASTER, BM);
		}	/* end of shutdown procedure for non-etr parts */

		hp100_cascade_reset(dev, 1);
	}
	hp100_page(PERFORMANCE);
	/* hp100_outw( HP100_BM_READ | HP100_BM_WRITE | HP100_RESET_HB, OPTION_LSW ); */
	/* Busmaster mode should be shut down now. */
}

static int hp100_check_lan(struct net_device *dev)
{
	struct hp100_private *lp = netdev_priv(dev);

	if (lp->lan_type < 0) {	/* no LAN type detected yet? */
		hp100_stop_interface(dev);
		if ((lp->lan_type = hp100_sense_lan(dev)) < 0) {
			printk("hp100: %s: no connection found - check wire\n", dev->name);
			hp100_start_interface(dev);	/* 10Mb/s RX packets maybe handled */
			return -EIO;
		}
		if (lp->lan_type == HP100_LAN_100)
			lp->hub_status = hp100_login_to_vg_hub(dev, 0);	/* relogin */
		hp100_start_interface(dev);
	}
	return 0;
}

/*
 *  transmit functions
 */

/* tx function for busmaster mode */
static int hp100_start_xmit_bm(struct sk_buff *skb, struct net_device *dev)
{
	unsigned long flags;
	int i, ok_flag;
	int ioaddr = dev->base_addr;
	struct hp100_private *lp = netdev_priv(dev);
	hp100_ring_t *ringptr;

#ifdef HP100_DEBUG_B
	hp100_outw(0x4210, TRACE);
	printk("hp100: %s: start_xmit_bm\n", dev->name);
#endif

	if (skb == NULL) {
		return 0;
	}

	if (skb->len <= 0)
		return 0;

	if (lp->chip == HP100_CHIPID_SHASTA && skb_padto(skb, ETH_ZLEN))
		return 0;

	/* Get Tx ring tail pointer */
	if (lp->txrtail->next == lp->txrhead) {
		/* No memory. */
#ifdef HP100_DEBUG
		printk("hp100: %s: start_xmit_bm: No TX PDL available.\n", dev->name);
#endif
		/* not waited long enough since last tx? */
		if (time_before(jiffies, dev->trans_start + HZ))
			return -EAGAIN;

		if (hp100_check_lan(dev))
			return -EIO;

		if (lp->lan_type == HP100_LAN_100 && lp->hub_status < 0) {
			/* we have a 100Mb/s adapter but it isn't connected to hub */
			printk("hp100: %s: login to 100Mb/s hub retry\n", dev->name);
			hp100_stop_interface(dev);
			lp->hub_status = hp100_login_to_vg_hub(dev, 0);
			hp100_start_interface(dev);
		} else {
			spin_lock_irqsave(&lp->lock, flags);
			hp100_ints_off();	/* Useful ? Jean II */
			i = hp100_sense_lan(dev);
			hp100_ints_on();
			spin_unlock_irqrestore(&lp->lock, flags);
			if (i == HP100_LAN_ERR)
				printk("hp100: %s: link down detected\n", dev->name);
			else if (lp->lan_type != i) {	/* cable change! */
				/* it's very hard - all network settings must be changed!!! */
				printk("hp100: %s: cable change 10Mb/s <-> 100Mb/s detected\n", dev->name);
				lp->lan_type = i;
				hp100_stop_interface(dev);
				if (lp->lan_type == HP100_LAN_100)
					lp->hub_status = hp100_login_to_vg_hub(dev, 0);
				hp100_start_interface(dev);
			} else {
				printk("hp100: %s: interface reset\n", dev->name);
				hp100_stop_interface(dev);
				if (lp->lan_type == HP100_LAN_100)
					lp->hub_status = hp100_login_to_vg_hub(dev, 0);
				hp100_start_interface(dev);
			}
		}

		dev->trans_start = jiffies;
		return -EAGAIN;
	}

	/*
	 * we have to turn int's off before modifying this, otherwise
	 * a tx_pdl_cleanup could occur at the same time
	 */
	spin_lock_irqsave(&lp->lock, flags);
	ringptr = lp->txrtail;
	lp->txrtail = ringptr->next;

	/* Check whether packet has minimal packet size */
	ok_flag = skb->len >= HP100_MIN_PACKET_SIZE;
	i = ok_flag ? skb->len : HP100_MIN_PACKET_SIZE;

	ringptr->skb = skb;
	ringptr->pdl[0] = ((1 << 16) | i);	/* PDH: 1 Fragment & length */
	if (lp->chip == HP100_CHIPID_SHASTA) {
		/* TODO:Could someone who has the EISA card please check if this works? */
		ringptr->pdl[2] = i;
	} else {		/* Lassen */
		/* In the PDL, don't use the padded size but the real packet size: */
		ringptr->pdl[2] = skb->len;	/* 1st Frag: Length of frag */
	}
	/* Conversion to new PCI API : map skbuf data to PCI bus.
	 * Doc says it's OK for EISA as well - Jean II */
	ringptr->pdl[1] = ((u32) pci_map_single(lp->pci_dev, skb->data, ringptr->pdl[2], PCI_DMA_TODEVICE));	/* 1st Frag: Adr. of data */

	/* Hand this PDL to the card. */
	hp100_outl(ringptr->pdl_paddr, TX_PDA_L);	/* Low Prio. Queue */

	lp->txrcommit++;
	spin_unlock_irqrestore(&lp->lock, flags);

	/* Update statistics */
	lp->stats.tx_packets++;
	lp->stats.tx_bytes += skb->len;
	dev->trans_start = jiffies;

	return 0;
}


/* clean_txring checks if packets have been sent by the card by reading
 * the TX_PDL register from the performance page and comparing it to the
 * number of commited packets. It then frees the skb's of the packets that
 * obviously have been sent to the network.
 *
 * Needs the PERFORMANCE page selected.
 */
static void hp100_clean_txring(struct net_device *dev)
{
	struct hp100_private *lp = netdev_priv(dev);
	int ioaddr = dev->base_addr;
	int donecount;

#ifdef HP100_DEBUG_B
	hp100_outw(0x4211, TRACE);
	printk("hp100: %s: clean txring\n", dev->name);
#endif

	/* How many PDLs have been transmitted? */
	donecount = (lp->txrcommit) - hp100_inb(TX_PDL);

#ifdef HP100_DEBUG
	if (donecount > MAX_TX_PDL)
		printk("hp100: %s: Warning: More PDLs transmitted than commited to card???\n", dev->name);
#endif

	for (; 0 != donecount; donecount--) {
#ifdef HP100_DEBUG_BM
		printk("hp100: %s: Free skb: data @0x%.8x txrcommit=0x%x TXPDL=0x%x, done=0x%x\n",
				dev->name, (u_int) lp->txrhead->skb->data,
				lp->txrcommit, hp100_inb(TX_PDL), donecount);
#endif
		/* Conversion to new PCI API : NOP */
		pci_unmap_single(lp->pci_dev, (dma_addr_t) lp->txrhead->pdl[1], lp->txrhead->pdl[2], PCI_DMA_TODEVICE);
		dev_kfree_skb_any(lp->txrhead->skb);
		lp->txrhead->skb = (void *) NULL;
		lp->txrhead = lp->txrhead->next;
		lp->txrcommit--;
	}
}

/* tx function for slave modes */
static int hp100_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	unsigned long flags;
	int i, ok_flag;
	int ioaddr = dev->base_addr;
	u_short val;
	struct hp100_private *lp = netdev_priv(dev);

#ifdef HP100_DEBUG_B
	hp100_outw(0x4212, TRACE);
	printk("hp100: %s: start_xmit\n", dev->name);
#endif

	if (skb == NULL) {
		return 0;
	}

	if (skb->len <= 0)
		return 0;

	if (hp100_check_lan(dev))
		return -EIO;

	/* If there is not enough free memory on the card... */
	i = hp100_inl(TX_MEM_FREE) & 0x7fffffff;
	if (!(((i / 2) - 539) > (skb->len + 16) && (hp100_inb(TX_PKT_CNT) < 255))) {
#ifdef HP100_DEBUG
		printk("hp100: %s: start_xmit: tx free mem = 0x%x\n", dev->name, i);
#endif
		/* not waited long enough since last failed tx try? */
		if (time_before(jiffies, dev->trans_start + HZ)) {
#ifdef HP100_DEBUG
			printk("hp100: %s: trans_start timing problem\n",
			       dev->name);
#endif
			return -EAGAIN;
		}
		if (lp->lan_type == HP100_LAN_100 && lp->hub_status < 0) {
			/* we have a 100Mb/s adapter but it isn't connected to hub */
			printk("hp100: %s: login to 100Mb/s hub retry\n", dev->name);
			hp100_stop_interface(dev);
			lp->hub_status = hp100_login_to_vg_hub(dev, 0);
			hp100_start_interface(dev);
		} else {
			spin_lock_irqsave(&lp->lock, flags);
			hp100_ints_off();	/* Useful ? Jean II */
			i = hp100_sense_lan(dev);
			hp100_ints_on();
			spin_unlock_irqrestore(&lp->lock, flags);
			if (i == HP100_LAN_ERR)
				printk("hp100: %s: link down detected\n", dev->name);
			else if (lp->lan_type != i) {	/* cable change! */
				/* it's very hard - all network setting must be changed!!! */
				printk("hp100: %s: cable change 10Mb/s <-> 100Mb/s detected\n", dev->name);
				lp->lan_type = i;
				hp100_stop_interface(dev);
				if (lp->lan_type == HP100_LAN_100)
					lp->hub_status = hp100_login_to_vg_hub(dev, 0);
				hp100_start_interface(dev);
			} else {
				printk("hp100: %s: interface reset\n", dev->name);
				hp100_stop_interface(dev);
				if (lp->lan_type == HP100_LAN_100)
					lp->hub_status = hp100_login_to_vg_hub(dev, 0);
				hp100_start_interface(dev);
				mdelay(1);
			}
		}
		dev->trans_start = jiffies;
		return -EAGAIN;
	}

	for (i = 0; i < 6000 && (hp100_inb(OPTION_MSW) & HP100_TX_CMD); i++) {
#ifdef HP100_DEBUG_TX
		printk("hp100: %s: start_xmit: busy\n", dev->name);
#endif
	}

	spin_lock_irqsave(&lp->lock, flags);
	hp100_ints_off();
	val = hp100_inw(IRQ_STATUS);
	/* Ack / clear the interrupt TX_COMPLETE interrupt - this interrupt is set
	 * when the current packet being transmitted on the wire is completed. */
	hp100_outw(HP100_TX_COMPLETE, IRQ_STATUS);
#ifdef HP100_DEBUG_TX
	printk("hp100: %s: start_xmit: irq_status=0x%.4x, irqmask=0x%.4x, len=%d\n",
			dev->name, val, hp100_inw(IRQ_MASK), (int) skb->len);
#endif

	ok_flag = skb->len >= HP100_MIN_PACKET_SIZE;
	i = ok_flag ? skb->len : HP100_MIN_PACKET_SIZE;

	hp100_outw(i, DATA32);	/* tell card the total packet length */
	hp100_outw(i, FRAGMENT_LEN);	/* and first/only fragment length    */

	if (lp->mode == 2) {	/* memory mapped */
		/* Note: The J2585B needs alignment to 32bits here!  */
		memcpy_toio(lp->mem_ptr_virt, skb->data, (skb->len + 3) & ~3);
		if (!ok_flag)
			memset_io(lp->mem_ptr_virt, 0, HP100_MIN_PACKET_SIZE - skb->len);
	} else {		/* programmed i/o */
		outsl(ioaddr + HP100_REG_DATA32, skb->data,
		      (skb->len + 3) >> 2);
		if (!ok_flag)
			for (i = (skb->len + 3) & ~3; i < HP100_MIN_PACKET_SIZE; i += 4)
				hp100_outl(0, DATA32);
	}

	hp100_outb(HP100_TX_CMD | HP100_SET_LB, OPTION_MSW);	/* send packet */

	lp->stats.tx_packets++;
	lp->stats.tx_bytes += skb->len;
	dev->trans_start = jiffies;
	hp100_ints_on();
	spin_unlock_irqrestore(&lp->lock, flags);

	dev_kfree_skb_any(skb);

#ifdef HP100_DEBUG_TX
	printk("hp100: %s: start_xmit: end\n", dev->name);
#endif

	return 0;
}


/*
 * Receive Function (Non-Busmaster mode)
 * Called when an "Receive Packet" interrupt occurs, i.e. the receive
 * packet counter is non-zero.
 * For non-busmaster, this function does the whole work of transfering
 * the packet to the host memory and then up to higher layers via skb
 * and netif_rx.
 */

static void hp100_rx(struct net_device *dev)
{
	int packets, pkt_len;
	int ioaddr = dev->base_addr;
	struct hp100_private *lp = netdev_priv(dev);
	u_int header;
	struct sk_buff *skb;

#ifdef DEBUG_B
	hp100_outw(0x4213, TRACE);
	printk("hp100: %s: rx\n", dev->name);
#endif

	/* First get indication of received lan packet */
	/* RX_PKT_CND indicates the number of packets which have been fully */
	/* received onto the card but have not been fully transferred of the card */
	packets = hp100_inb(RX_PKT_CNT);
#ifdef HP100_DEBUG_RX
	if (packets > 1)
		printk("hp100: %s: rx: waiting packets = %d\n", dev->name, packets);
#endif

	while (packets-- > 0) {
		/* If ADV_NXT_PKT is still set, we have to wait until the card has */
		/* really advanced to the next packet. */
		for (pkt_len = 0; pkt_len < 6000 && (hp100_inb(OPTION_MSW) & HP100_ADV_NXT_PKT); pkt_len++) {
#ifdef HP100_DEBUG_RX
			printk ("hp100: %s: rx: busy, remaining packets = %d\n", dev->name, packets);
#endif
		}

		/* First we get the header, which contains information about the */
		/* actual length of the received packet. */
		if (lp->mode == 2) {	/* memory mapped mode */
			header = readl(lp->mem_ptr_virt);
		} else		/* programmed i/o */
			header = hp100_inl(DATA32);

		pkt_len = ((header & HP100_PKT_LEN_MASK) + 3) & ~3;

#ifdef HP100_DEBUG_RX
		printk("hp100: %s: rx: new packet - length=%d, errors=0x%x, dest=0x%x\n",
				     dev->name, header & HP100_PKT_LEN_MASK,
				     (header >> 16) & 0xfff8, (header >> 16) & 7);
#endif

		/* Now we allocate the skb and transfer the data into it. */
		skb = dev_alloc_skb(pkt_len+2);
		if (skb == NULL) {	/* Not enough memory->drop packet */
#ifdef HP100_DEBUG
			printk("hp100: %s: rx: couldn't allocate a sk_buff of size %d\n",
					     dev->name, pkt_len);
#endif
			lp->stats.rx_dropped++;
		} else {	/* skb successfully allocated */

			u_char *ptr;

			skb_reserve(skb,2);

			/* ptr to start of the sk_buff data area */
			skb_put(skb, pkt_len);
			ptr = skb->data;

			/* Now transfer the data from the card into that area */
			if (lp->mode == 2)
				memcpy_fromio(ptr, lp->mem_ptr_virt,pkt_len);
			else	/* io mapped */
				insl(ioaddr + HP100_REG_DATA32, ptr, pkt_len >> 2);

			skb->protocol = eth_type_trans(skb, dev);

#ifdef HP100_DEBUG_RX
			printk("hp100: %s: rx: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
					dev->name, ptr[0], ptr[1], ptr[2], ptr[3],
		 			ptr[4], ptr[5], ptr[6], ptr[7], ptr[8],
					ptr[9], ptr[10], ptr[11]);
#endif
			netif_rx(skb);
			dev->last_rx = jiffies;
			lp->stats.rx_packets++;
			lp->stats.rx_bytes += pkt_len;
		}

		/* Indicate the card that we have got the packet */
		hp100_outb(HP100_ADV_NXT_PKT | HP100_SET_LB, OPTION_MSW);

		switch (header & 0x00070000) {
		case (HP100_MULTI_ADDR_HASH << 16):
		case (HP100_MULTI_ADDR_NO_HASH << 16):
			lp->stats.multicast++;
			break;
		}
	}			/* end of while(there are packets) loop */
#ifdef HP100_DEBUG_RX
	printk("hp100_rx: %s: end\n", dev->name);
#endif
}

/*
 * Receive Function for Busmaster Mode
 */
static void hp100_rx_bm(struct net_device *dev)
{
	int ioaddr = dev->base_addr;
	struct hp100_private *lp = netdev_priv(dev);
	hp100_ring_t *ptr;
	u_int header;
	int pkt_len;

#ifdef HP100_DEBUG_B
	hp100_outw(0x4214, TRACE);
	printk("hp100: %s: rx_bm\n", dev->name);
#endif

#ifdef HP100_DEBUG
	if (0 == lp->rxrcommit) {
		printk("hp100: %s: rx_bm called although no PDLs were committed to adapter?\n", dev->name);
		return;
	} else
		/* RX_PKT_CNT states how many PDLs are currently formatted and available to
		 * the cards BM engine */
	if ((hp100_inw(RX_PKT_CNT) & 0x00ff) >= lp->rxrcommit) {
		printk("hp100: %s: More packets received than commited? RX_PKT_CNT=0x%x, commit=0x%x\n",
				     dev->name, hp100_inw(RX_PKT_CNT) & 0x00ff,
				     lp->rxrcommit);
		return;
	}
#endif

	while ((lp->rxrcommit > hp100_inb(RX_PDL))) {
		/*
		 * The packet was received into the pdl pointed to by lp->rxrhead (
		 * the oldest pdl in the ring
		 */

		/* First we get the header, which contains information about the */
		/* actual length of the received packet. */

		ptr = lp->rxrhead;

		header = *(ptr->pdl - 1);
		pkt_len = (header & HP100_PKT_LEN_MASK);

		/* Conversion to new PCI API : NOP */
		pci_unmap_single(lp->pci_dev, (dma_addr_t) ptr->pdl[3], MAX_ETHER_SIZE, PCI_DMA_FROMDEVICE);

#ifdef HP100_DEBUG_BM
		printk("hp100: %s: rx_bm: header@0x%x=0x%x length=%d, errors=0x%x, dest=0x%x\n",
				dev->name, (u_int) (ptr->pdl - 1), (u_int) header,
				pkt_len, (header >> 16) & 0xfff8, (header >> 16) & 7);
		printk("hp100: %s: RX_PDL_COUNT:0x%x TX_PDL_COUNT:0x%x, RX_PKT_CNT=0x%x PDH=0x%x, Data@0x%x len=0x%x\n",
		   		dev->name, hp100_inb(RX_PDL), hp100_inb(TX_PDL),
				hp100_inb(RX_PKT_CNT), (u_int) * (ptr->pdl),
				(u_int) * (ptr->pdl + 3), (u_int) * (ptr->pdl + 4));
#endif

		if ((pkt_len >= MIN_ETHER_SIZE) &&
		    (pkt_len <= MAX_ETHER_SIZE)) {
			if (ptr->skb == NULL) {
				printk("hp100: %s: rx_bm: skb null\n", dev->name);
				/* can happen if we only allocated room for the pdh due to memory shortage. */
				lp->stats.rx_dropped++;
			} else {
				skb_trim(ptr->skb, pkt_len);	/* Shorten it */
				ptr->skb->protocol =
				    eth_type_trans(ptr->skb, dev);

				netif_rx(ptr->skb);	/* Up and away... */

				dev->last_rx = jiffies;
				lp->stats.rx_packets++;
				lp->stats.rx_bytes += pkt_len;
			}

			switch (header & 0x00070000) {
			case (HP100_MULTI_ADDR_HASH << 16):
			case (HP100_MULTI_ADDR_NO_HASH << 16):
				lp->stats.multicast++;
				break;
			}
		} else {
#ifdef HP100_DEBUG
			printk("hp100: %s: rx_bm: Received bad packet (length=%d)\n", dev->name, pkt_len);
#endif
			if (ptr->skb != NULL)
				dev_kfree_skb_any(ptr->skb);
			lp->stats.rx_errors++;
		}

		lp->rxrhead = lp->rxrhead->next;

		/* Allocate a new rx PDL (so lp->rxrcommit stays the same) */
		if (0 == hp100_build_rx_pdl(lp->rxrtail, dev)) {
			/* No space for skb, header can still be received. */
#ifdef HP100_DEBUG
			printk("hp100: %s: rx_bm: No space for new PDL.\n", dev->name);
#endif
			return;
		} else {	/* successfully allocated new PDL - put it in ringlist at tail. */
			hp100_outl((u32) lp->rxrtail->pdl_paddr, RX_PDA);
			lp->rxrtail = lp->rxrtail->next;
		}

	}
}

/*
 *  statistics
 */
static struct net_device_stats *hp100_get_stats(struct net_device *dev)
{
	unsigned long flags;
	int ioaddr = dev->base_addr;
	struct hp100_private *lp = netdev_priv(dev);

#ifdef HP100_DEBUG_B
	hp100_outw(0x4215, TRACE);
#endif

	spin_lock_irqsave(&lp->lock, flags);
	hp100_ints_off();	/* Useful ? Jean II */
	hp100_update_stats(dev);
	hp100_ints_on();
	spin_unlock_irqrestore(&lp->lock, flags);
	return &(lp->stats);
}

static void hp100_update_stats(struct net_device *dev)
{
	int ioaddr = dev->base_addr;
	u_short val;
	struct hp100_private *lp = netdev_priv(dev);

#ifdef HP100_DEBUG_B
	hp100_outw(0x4216, TRACE);
	printk("hp100: %s: update-stats\n", dev->name);
#endif

	/* Note: Statistics counters clear when read. */
	hp100_page(MAC_CTRL);
	val = hp100_inw(DROPPED) & 0x0fff;
	lp->stats.rx_errors += val;
	lp->stats.rx_over_errors += val;
	val = hp100_inb(CRC);
	lp->stats.rx_errors += val;
	lp->stats.rx_crc_errors += val;
	val = hp100_inb(ABORT);
	lp->stats.tx_errors += val;
	lp->stats.tx_aborted_errors += val;
	hp100_page(PERFORMANCE);
}

static void hp100_misc_interrupt(struct net_device *dev)
{
#ifdef HP100_DEBUG_B
	int ioaddr = dev->base_addr;
#endif
	struct hp100_private *lp = netdev_priv(dev);

#ifdef HP100_DEBUG_B
	int ioaddr = dev->base_addr;
	hp100_outw(0x4216, TRACE);
	printk("hp100: %s: misc_interrupt\n", dev->name);
#endif

	/* Note: Statistics counters clear when read. */
	lp->stats.rx_errors++;
	lp->stats.tx_errors++;
}

static void hp100_clear_stats(struct hp100_private *lp, int ioaddr)
{
	unsigned long flags;

#ifdef HP100_DEBUG_B
	hp100_outw(0x4217, TRACE);
	printk("hp100: %s: clear_stats\n", dev->name);
#endif

	spin_lock_irqsave(&lp->lock, flags);
	hp100_page(MAC_CTRL);	/* get all statistics bytes */
	hp100_inw(DROPPED);
	hp100_inb(CRC);
	hp100_inb(ABORT);
	hp100_page(PERFORMANCE);
	spin_unlock_irqrestore(&lp->lock, flags);
}


/*
 *  multicast setup
 */

/*
 *  Set or clear the multicast filter for this adapter.
 */

static void hp100_set_multicast_list(struct net_device *dev)
{
	unsigned long flags;
	int ioaddr = dev->base_addr;
	struct hp100_private *lp = netdev_priv(dev);

#ifdef HP100_DEBUG_B
	hp100_outw(0x4218, TRACE);
	printk("hp100: %s: set_mc_list\n", dev->name);
#endif

	spin_lock_irqsave(&lp->lock, flags);
	hp100_ints_off();
	hp100_page(MAC_CTRL);
	hp100_andb(~(HP100_RX_EN | HP100_TX_EN), MAC_CFG_1);	/* stop rx/tx */

	if (dev->flags & IFF_PROMISC) {
		lp->mac2_mode = HP100_MAC2MODE6;	/* promiscuous mode = get all good */
		lp->mac1_mode = HP100_MAC1MODE6;	/* packets on the net */
		memset(&lp->hash_bytes, 0xff, 8);
	} else if (dev->mc_count || (dev->flags & IFF_ALLMULTI)) {
		lp->mac2_mode = HP100_MAC2MODE5;	/* multicast mode = get packets for */
		lp->mac1_mode = HP100_MAC1MODE5;	/* me, broadcasts and all multicasts */
#ifdef HP100_MULTICAST_FILTER	/* doesn't work!!! */
		if (dev->flags & IFF_ALLMULTI) {
			/* set hash filter to receive all multicast packets */
			memset(&lp->hash_bytes, 0xff, 8);
		} else {
			int i, j, idx;
			u_char *addrs;
			struct dev_mc_list *dmi;

			memset(&lp->hash_bytes, 0x00, 8);
#ifdef HP100_DEBUG
			printk("hp100: %s: computing hash filter - mc_count = %i\n", dev->name, dev->mc_count);
#endif
			for (i = 0, dmi = dev->mc_list; i < dev->mc_count; i++, dmi = dmi->next) {
				addrs = dmi->dmi_addr;
				if ((*addrs & 0x01) == 0x01) {	/* multicast address? */
#ifdef HP100_DEBUG
					printk("hp100: %s: multicast = %02x:%02x:%02x:%02x:%02x:%02x, ",
						     dev->name, addrs[0], addrs[1], addrs[2],
						     addrs[3], addrs[4], addrs[5]);
#endif
					for (j = idx = 0; j < 6; j++) {
						idx ^= *addrs++ & 0x3f;
						printk(":%02x:", idx);
					}
#ifdef HP100_DEBUG
					printk("idx = %i\n", idx);
#endif
					lp->hash_bytes[idx >> 3] |= (1 << (idx & 7));
				}
			}
		}
#else
		memset(&lp->hash_bytes, 0xff, 8);
#endif
	} else {
		lp->mac2_mode = HP100_MAC2MODE3;	/* normal mode = get packets for me */
		lp->mac1_mode = HP100_MAC1MODE3;	/* and broadcasts */
		memset(&lp->hash_bytes, 0x00, 8);
	}

	if (((hp100_inb(MAC_CFG_1) & 0x0f) != lp->mac1_mode) ||
	    (hp100_inb(MAC_CFG_2) != lp->mac2_mode)) {
		int i;

		hp100_outb(lp->mac2_mode, MAC_CFG_2);
		hp100_andb(HP100_MAC1MODEMASK, MAC_CFG_1);	/* clear mac1 mode bits */
		hp100_orb(lp->mac1_mode, MAC_CFG_1);	/* and set the new mode */

		hp100_page(MAC_ADDRESS);
		for (i = 0; i < 8; i++)
			hp100_outb(lp->hash_bytes[i], HASH_BYTE0 + i);
#ifdef HP100_DEBUG
		printk("hp100: %s: mac1 = 0x%x, mac2 = 0x%x, multicast hash = %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x\n",
				     dev->name, lp->mac1_mode, lp->mac2_mode,
				     lp->hash_bytes[0], lp->hash_bytes[1],
				     lp->hash_bytes[2], lp->hash_bytes[3],
				     lp->hash_bytes[4], lp->hash_bytes[5],
				     lp->hash_bytes[6], lp->hash_bytes[7]);
#endif

		if (lp->lan_type == HP100_LAN_100) {
#ifdef HP100_DEBUG
			printk("hp100: %s: 100VG MAC settings have changed - relogin.\n", dev->name);
#endif
			lp->hub_status = hp100_login_to_vg_hub(dev, 1);	/* force a relogin to the hub */
		}
	} else {
		int i;
		u_char old_hash_bytes[8];

		hp100_page(MAC_ADDRESS);
		for (i = 0; i < 8; i++)
			old_hash_bytes[i] = hp100_inb(HASH_BYTE0 + i);
		if (memcmp(old_hash_bytes, &lp->hash_bytes, 8)) {
			for (i = 0; i < 8; i++)
				hp100_outb(lp->hash_bytes[i], HASH_BYTE0 + i);
#ifdef HP100_DEBUG
			printk("hp100: %s: multicast hash = %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x\n",
					dev->name, lp->hash_bytes[0],
					lp->hash_bytes[1], lp->hash_bytes[2],
					lp->hash_bytes[3], lp->hash_bytes[4],
					lp->hash_bytes[5], lp->hash_bytes[6],
					lp->hash_bytes[7]);
#endif

			if (lp->lan_type == HP100_LAN_100) {
#ifdef HP100_DEBUG
				printk("hp100: %s: 100VG MAC settings have changed - relogin.\n", dev->name);
#endif
				lp->hub_status = hp100_login_to_vg_hub(dev, 1);	/* force a relogin to the hub */
			}
		}
	}

	hp100_page(MAC_CTRL);
	hp100_orb(HP100_RX_EN | HP100_RX_IDLE |	/* enable rx */
		  HP100_TX_EN | HP100_TX_IDLE, MAC_CFG_1);	/* enable tx */

	hp100_page(PERFORMANCE);
	hp100_ints_on();
	spin_unlock_irqrestore(&lp->lock, flags);
}

/*
 *  hardware interrupt handling
 */

static irqreturn_t hp100_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = (struct net_device *) dev_id;
	struct hp100_private *lp = netdev_priv(dev);

	int ioaddr;
	u_int val;

	if (dev == NULL)
		return IRQ_NONE;
	ioaddr = dev->base_addr;

	spin_lock(&lp->lock);

	hp100_ints_off();

#ifdef HP100_DEBUG_B
	hp100_outw(0x4219, TRACE);
#endif

	/*  hp100_page( PERFORMANCE ); */
	val = hp100_inw(IRQ_STATUS);
#ifdef HP100_DEBUG_IRQ
	printk("hp100: %s: mode=%x,IRQ_STAT=0x%.4x,RXPKTCNT=0x%.2x RXPDL=0x%.2x TXPKTCNT=0x%.2x TXPDL=0x%.2x\n",
			     dev->name, lp->mode, (u_int) val, hp100_inb(RX_PKT_CNT),
			     hp100_inb(RX_PDL), hp100_inb(TX_PKT_CNT), hp100_inb(TX_PDL));
#endif

	if (val == 0) {		/* might be a shared interrupt */
		spin_unlock(&lp->lock);
		hp100_ints_on();
		return IRQ_NONE;
	}
	/* We're only interested in those interrupts we really enabled. */
	/* val &= hp100_inw( IRQ_MASK ); */

	/*
	 * RX_PDL_FILL_COMPL is set whenever a RX_PDL has been executed. A RX_PDL
	 * is considered executed whenever the RX_PDL data structure is no longer
	 * needed.
	 */
	if (val & HP100_RX_PDL_FILL_COMPL) {
		if (lp->mode == 1)
			hp100_rx_bm(dev);
		else {
			printk("hp100: %s: rx_pdl_fill_compl interrupt although not busmaster?\n", dev->name);
		}
	}

	/*
	 * The RX_PACKET interrupt is set, when the receive packet counter is
	 * non zero. We use this interrupt for receiving in slave mode. In
	 * busmaster mode, we use it to make sure we did not miss any rx_pdl_fill
	 * interrupts. If rx_pdl_fill_compl is not set and rx_packet is set, then
	 * we somehow have missed a rx_pdl_fill_compl interrupt.
	 */

	if (val & HP100_RX_PACKET) {	/* Receive Packet Counter is non zero */
		if (lp->mode != 1)	/* non busmaster */
			hp100_rx(dev);
		else if (!(val & HP100_RX_PDL_FILL_COMPL)) {
			/* Shouldnt happen - maybe we missed a RX_PDL_FILL Interrupt?  */
			hp100_rx_bm(dev);
		}
	}

	/*
	 * Ack. that we have noticed the interrupt and thereby allow next one.
	 * Note that this is now done after the slave rx function, since first
	 * acknowledging and then setting ADV_NXT_PKT caused an extra interrupt
	 * on the J2573.
	 */
	hp100_outw(val, IRQ_STATUS);

	/*
	 * RX_ERROR is set when a packet is dropped due to no memory resources on
	 * the card or when a RCV_ERR occurs.
	 * TX_ERROR is set when a TX_ABORT condition occurs in the MAC->exists
	 * only in the 802.3 MAC and happens when 16 collisions occur during a TX
	 */
	if (val & (HP100_TX_ERROR | HP100_RX_ERROR)) {
#ifdef HP100_DEBUG_IRQ
		printk("hp100: %s: TX/RX Error IRQ\n", dev->name);
#endif
		hp100_update_stats(dev);
		if (lp->mode == 1) {
			hp100_rxfill(dev);
			hp100_clean_txring(dev);
		}
	}

	/*
	 * RX_PDA_ZERO is set when the PDA count goes from non-zero to zero.
	 */
	if ((lp->mode == 1) && (val & (HP100_RX_PDA_ZERO)))
		hp100_rxfill(dev);

	/*
	 * HP100_TX_COMPLETE interrupt occurs when packet transmitted on wire
	 * is completed
	 */
	if ((lp->mode == 1) && (val & (HP100_TX_COMPLETE)))
		hp100_clean_txring(dev);

	/*
	 * MISC_ERROR is set when either the LAN link goes down or a detected
	 * bus error occurs.
	 */
	if (val & HP100_MISC_ERROR) {	/* New for J2585B */
#ifdef HP100_DEBUG_IRQ
		printk
		    ("hp100: %s: Misc. Error Interrupt - Check cabling.\n",
		     dev->name);
#endif
		if (lp->mode == 1) {
			hp100_clean_txring(dev);
			hp100_rxfill(dev);
		}
		hp100_misc_interrupt(dev);
	}

	spin_unlock(&lp->lock);
	hp100_ints_on();
	return IRQ_HANDLED;
}

/*
 *  some misc functions
 */

static void hp100_start_interface(struct net_device *dev)
{
	unsigned long flags;
	int ioaddr = dev->base_addr;
	struct hp100_private *lp = netdev_priv(dev);

#ifdef HP100_DEBUG_B
	hp100_outw(0x4220, TRACE);
	printk("hp100: %s: hp100_start_interface\n", dev->name);
#endif

	spin_lock_irqsave(&lp->lock, flags);

	/* Ensure the adapter does not want to request an interrupt when */
	/* enabling the IRQ line to be active on the bus (i.e. not tri-stated) */
	hp100_page(PERFORMANCE);
	hp100_outw(0xfefe, IRQ_MASK);	/* mask off all ints */
	hp100_outw(0xffff, IRQ_STATUS);	/* ack all IRQs */
	hp100_outw(HP100_FAKE_INT | HP100_INT_EN | HP100_RESET_LB,
		   OPTION_LSW);
	/* Un Tri-state int. TODO: Check if shared interrupts can be realised? */
	hp100_outw(HP100_TRI_INT | HP100_RESET_HB, OPTION_LSW);

	if (lp->mode == 1) {
		/* Make sure BM bit is set... */
		hp100_page(HW_MAP);
		hp100_orb(HP100_BM_MASTER, BM);
		hp100_rxfill(dev);
	} else if (lp->mode == 2) {
		/* Enable memory mapping. Note: Don't do this when busmaster. */
		hp100_outw(HP100_MMAP_DIS | HP100_RESET_HB, OPTION_LSW);
	}

	hp100_page(PERFORMANCE);
	hp100_outw(0xfefe, IRQ_MASK);	/* mask off all ints */
	hp100_outw(0xffff, IRQ_STATUS);	/* ack IRQ */

	/* enable a few interrupts: */
	if (lp->mode == 1) {	/* busmaster mode */
		hp100_outw(HP100_RX_PDL_FILL_COMPL |
			   HP100_RX_PDA_ZERO | HP100_RX_ERROR |
			   /* HP100_RX_PACKET    | */
			   /* HP100_RX_EARLY_INT |  */ HP100_SET_HB |
			   /* HP100_TX_PDA_ZERO  |  */
			   HP100_TX_COMPLETE |
			   /* HP100_MISC_ERROR   |  */
			   HP100_TX_ERROR | HP100_SET_LB, IRQ_MASK);
	} else {
		hp100_outw(HP100_RX_PACKET |
			   HP100_RX_ERROR | HP100_SET_HB |
			   HP100_TX_ERROR | HP100_SET_LB, IRQ_MASK);
	}

	/* Note : before hp100_set_multicast_list(), because it will play with
	 * spinlock itself... Jean II */
	spin_unlock_irqrestore(&lp->lock, flags);

	/* Enable MAC Tx and RX, set MAC modes, ... */
	hp100_set_multicast_list(dev);
}

static void hp100_stop_interface(struct net_device *dev)
{
	struct hp100_private *lp = netdev_priv(dev);
	int ioaddr = dev->base_addr;
	u_int val;

#ifdef HP100_DEBUG_B
	printk("hp100: %s: hp100_stop_interface\n", dev->name);
	hp100_outw(0x4221, TRACE);
#endif

	if (lp->mode == 1)
		hp100_BM_shutdown(dev);
	else {
		/* Note: MMAP_DIS will be reenabled by start_interface */
		hp100_outw(HP100_INT_EN | HP100_RESET_LB |
			   HP100_TRI_INT | HP100_MMAP_DIS | HP100_SET_HB,
			   OPTION_LSW);
		val = hp100_inw(OPTION_LSW);

		hp100_page(MAC_CTRL);
		hp100_andb(~(HP100_RX_EN | HP100_TX_EN), MAC_CFG_1);

		if (!(val & HP100_HW_RST))
			return;	/* If reset, imm. return ... */
		/* ... else: busy wait until idle */
		for (val = 0; val < 6000; val++)
			if ((hp100_inb(MAC_CFG_1) & (HP100_TX_IDLE | HP100_RX_IDLE)) == (HP100_TX_IDLE | HP100_RX_IDLE)) {
				hp100_page(PERFORMANCE);
				return;
			}
		printk("hp100: %s: hp100_stop_interface - timeout\n", dev->name);
		hp100_page(PERFORMANCE);
	}
}

static void hp100_load_eeprom(struct net_device *dev, u_short probe_ioaddr)
{
	int i;
	int ioaddr = probe_ioaddr > 0 ? probe_ioaddr : dev->base_addr;

#ifdef HP100_DEBUG_B
	hp100_outw(0x4222, TRACE);
#endif

	hp100_page(EEPROM_CTRL);
	hp100_andw(~HP100_EEPROM_LOAD, EEPROM_CTRL);
	hp100_orw(HP100_EEPROM_LOAD, EEPROM_CTRL);
	for (i = 0; i < 10000; i++)
		if (!(hp100_inb(OPTION_MSW) & HP100_EE_LOAD))
			return;
	printk("hp100: %s: hp100_load_eeprom - timeout\n", dev->name);
}

/*  Sense connection status.
 *  return values: LAN_10  - Connected to 10Mbit/s network
 *                 LAN_100 - Connected to 100Mbit/s network
 *                 LAN_ERR - not connected or 100Mbit/s Hub down
 */
static int hp100_sense_lan(struct net_device *dev)
{
	int ioaddr = dev->base_addr;
	u_short val_VG, val_10;
	struct hp100_private *lp = netdev_priv(dev);

#ifdef HP100_DEBUG_B
	hp100_outw(0x4223, TRACE);
#endif

	hp100_page(MAC_CTRL);
	val_10 = hp100_inb(10_LAN_CFG_1);
	val_VG = hp100_inb(VG_LAN_CFG_1);
	hp100_page(PERFORMANCE);
#ifdef HP100_DEBUG
	printk("hp100: %s: sense_lan: val_VG = 0x%04x, val_10 = 0x%04x\n",
	       dev->name, val_VG, val_10);
#endif

	if (val_10 & HP100_LINK_BEAT_ST)	/* 10Mb connection is active */
		return HP100_LAN_10;

	if (val_10 & HP100_AUI_ST) {	/* have we BNC or AUI onboard? */
		/*
		 * This can be overriden by dos utility, so if this has no effect,
		 * perhaps you need to download that utility from HP and set card
		 * back to "auto detect".
		 */
		val_10 |= HP100_AUI_SEL | HP100_LOW_TH;
		hp100_page(MAC_CTRL);
		hp100_outb(val_10, 10_LAN_CFG_1);
		hp100_page(PERFORMANCE);
		return HP100_LAN_COAX;
	}

	/* Those cards don't have a 100 Mbit connector */
	if ( !strcmp(lp->id, "HWP1920")  ||
	     (lp->pci_dev &&
	      lp->pci_dev->vendor == PCI_VENDOR_ID &&
	      (lp->pci_dev->device == PCI_DEVICE_ID_HP_J2970A ||
	       lp->pci_dev->device == PCI_DEVICE_ID_HP_J2973A)))
		return HP100_LAN_ERR;

	if (val_VG & HP100_LINK_CABLE_ST)	/* Can hear the HUBs tone. */
		return HP100_LAN_100;
	return HP100_LAN_ERR;
}

static int hp100_down_vg_link(struct net_device *dev)
{
	struct hp100_private *lp = netdev_priv(dev);
	int ioaddr = dev->base_addr;
	unsigned long time;
	long savelan, newlan;

#ifdef HP100_DEBUG_B
	hp100_outw(0x4224, TRACE);
	printk("hp100: %s: down_vg_link\n", dev->name);
#endif

	hp100_page(MAC_CTRL);
	time = jiffies + (HZ / 4);
	do {
		if (hp100_inb(VG_LAN_CFG_1) & HP100_LINK_CABLE_ST)
			break;
		if (!in_interrupt())
			schedule_timeout_interruptible(1);
	} while (time_after(time, jiffies));

	if (time_after_eq(jiffies, time))	/* no signal->no logout */
		return 0;

	/* Drop the VG Link by clearing the link up cmd and load addr. */

	hp100_andb(~(HP100_LOAD_ADDR | HP100_LINK_CMD), VG_LAN_CFG_1);
	hp100_orb(HP100_VG_SEL, VG_LAN_CFG_1);

	/* Conditionally stall for >250ms on Link-Up Status (to go down) */
	time = jiffies + (HZ / 2);
	do {
		if (!(hp100_inb(VG_LAN_CFG_1) & HP100_LINK_UP_ST))
			break;
		if (!in_interrupt())
			schedule_timeout_interruptible(1);
	} while (time_after(time, jiffies));

#ifdef HP100_DEBUG
	if (time_after_eq(jiffies, time))
		printk("hp100: %s: down_vg_link: Link does not go down?\n", dev->name);
#endif

	/* To prevent condition where Rev 1 VG MAC and old hubs do not complete */
	/* logout under traffic (even though all the status bits are cleared),  */
	/* do this workaround to get the Rev 1 MAC in its idle state */
	if (lp->chip == HP100_CHIPID_LASSEN) {
		/* Reset VG MAC to insure it leaves the logoff state even if */
		/* the Hub is still emitting tones */
		hp100_andb(~HP100_VG_RESET, VG_LAN_CFG_1);
		udelay(1500);	/* wait for >1ms */
		hp100_orb(HP100_VG_RESET, VG_LAN_CFG_1);	/* Release Reset */
		udelay(1500);
	}

	/* New: For lassen, switch to 10 Mbps mac briefly to clear training ACK */
	/* to get the VG mac to full reset. This is not req.d with later chips */
	/* Note: It will take the between 1 and 2 seconds for the VG mac to be */
	/* selected again! This will be left to the connect hub function to */
	/* perform if desired.  */
	if (lp->chip == HP100_CHIPID_LASSEN) {
		/* Have to write to 10 and 100VG control registers simultaneously */
		savelan = newlan = hp100_inl(10_LAN_CFG_1);	/* read 10+100 LAN_CFG regs */
		newlan &= ~(HP100_VG_SEL << 16);
		newlan |= (HP100_DOT3_MAC) << 8;
		hp100_andb(~HP100_AUTO_MODE, MAC_CFG_3);	/* Autosel off */
		hp100_outl(newlan, 10_LAN_CFG_1);

		/* Conditionally stall for 5sec on VG selected. */
		time = jiffies + (HZ * 5);
		do {
			if (!(hp100_inb(MAC_CFG_4) & HP100_MAC_SEL_ST))
				break;
			if (!in_interrupt())
				schedule_timeout_interruptible(1);
		} while (time_after(time, jiffies));

		hp100_orb(HP100_AUTO_MODE, MAC_CFG_3);	/* Autosel back on */
		hp100_outl(savelan, 10_LAN_CFG_1);
	}

	time = jiffies + (3 * HZ);	/* Timeout 3s */
	do {
		if ((hp100_inb(VG_LAN_CFG_1) & HP100_LINK_CABLE_ST) == 0)
			break;
		if (!in_interrupt())
			schedule_timeout_interruptible(1);
	} while (time_after(time, jiffies));

	if (time_before_eq(time, jiffies)) {
#ifdef HP100_DEBUG
		printk("hp100: %s: down_vg_link: timeout\n", dev->name);
#endif
		return -EIO;
	}

	time = jiffies + (2 * HZ);	/* This seems to take a while.... */
	do {
		if (!in_interrupt())
			schedule_timeout_interruptible(1);
	} while (time_after(time, jiffies));

	return 0;
}

static int hp100_login_to_vg_hub(struct net_device *dev, u_short force_relogin)
{
	int ioaddr = dev->base_addr;
	struct hp100_private *lp = netdev_priv(dev);
	u_short val = 0;
	unsigned long time;
	int startst;

#ifdef HP100_DEBUG_B
	hp100_outw(0x4225, TRACE);
	printk("hp100: %s: login_to_vg_hub\n", dev->name);
#endif

	/* Initiate a login sequence iff VG MAC is enabled and either Load Address
	 * bit is zero or the force relogin flag is set (e.g. due to MAC address or
	 * promiscuous mode change)
	 */
	hp100_page(MAC_CTRL);
	startst = hp100_inb(VG_LAN_CFG_1);
	if ((force_relogin == 1) || (hp100_inb(MAC_CFG_4) & HP100_MAC_SEL_ST)) {
#ifdef HP100_DEBUG_TRAINING
		printk("hp100: %s: Start training\n", dev->name);
#endif

		/* Ensure VG Reset bit is 1 (i.e., do not reset) */
		hp100_orb(HP100_VG_RESET, VG_LAN_CFG_1);

		/* If Lassen AND auto-select-mode AND VG tones were sensed on */
		/* entry then temporarily put them into force 100Mbit mode */
		if ((lp->chip == HP100_CHIPID_LASSEN) && (startst & HP100_LINK_CABLE_ST))
			hp100_andb(~HP100_DOT3_MAC, 10_LAN_CFG_2);

		/* Drop the VG link by zeroing Link Up Command and Load Address  */
		hp100_andb(~(HP100_LINK_CMD /* |HP100_LOAD_ADDR */ ), VG_LAN_CFG_1);

#ifdef HP100_DEBUG_TRAINING
		printk("hp100: %s: Bring down the link\n", dev->name);
#endif

		/* Wait for link to drop */
		time = jiffies + (HZ / 10);
		do {
			if (~(hp100_inb(VG_LAN_CFG_1) & HP100_LINK_UP_ST))
				break;
			if (!in_interrupt())
				schedule_timeout_interruptible(1);
		} while (time_after(time, jiffies));

		/* Start an addressed training and optionally request promiscuous port */
		if ((dev->flags) & IFF_PROMISC) {
			hp100_orb(HP100_PROM_MODE, VG_LAN_CFG_2);
			if (lp->chip == HP100_CHIPID_LASSEN)
				hp100_orw(HP100_MACRQ_PROMSC, TRAIN_REQUEST);
		} else {
			hp100_andb(~HP100_PROM_MODE, VG_LAN_CFG_2);
			/* For ETR parts we need to reset the prom. bit in the training
			 * register, otherwise promiscious mode won't be disabled.
			 */
			if (lp->chip == HP100_CHIPID_LASSEN) {
				hp100_andw(~HP100_MACRQ_PROMSC, TRAIN_REQUEST);
			}
		}

		/* With ETR parts, frame format request bits can be set. */
		if (lp->chip == HP100_CHIPID_LASSEN)
			hp100_orb(HP100_MACRQ_FRAMEFMT_EITHER, TRAIN_REQUEST);

		hp100_orb(HP100_LINK_CMD | HP100_LOAD_ADDR | HP100_VG_RESET, VG_LAN_CFG_1);

		/* Note: Next wait could be omitted for Hood and earlier chips under */
		/* certain circumstances */
		/* TODO: check if hood/earlier and skip wait. */

		/* Wait for either short timeout for VG tones or long for login    */
		/* Wait for the card hardware to signalise link cable status ok... */
		hp100_page(MAC_CTRL);
		time = jiffies + (1 * HZ);	/* 1 sec timeout for cable st */
		do {
			if (hp100_inb(VG_LAN_CFG_1) & HP100_LINK_CABLE_ST)
				break;
			if (!in_interrupt())
				schedule_timeout_interruptible(1);
		} while (time_before(jiffies, time));

		if (time_after_eq(jiffies, time)) {
#ifdef HP100_DEBUG_TRAINING
			printk("hp100: %s: Link cable status not ok? Training aborted.\n", dev->name);
#endif
		} else {
#ifdef HP100_DEBUG_TRAINING
			printk
			    ("hp100: %s: HUB tones detected. Trying to train.\n",
			     dev->name);
#endif

			time = jiffies + (2 * HZ);	/* again a timeout */
			do {
				val = hp100_inb(VG_LAN_CFG_1);
				if ((val & (HP100_LINK_UP_ST))) {
#ifdef HP100_DEBUG_TRAINING
					printk("hp100: %s: Passed training.\n", dev->name);
#endif
					break;
				}
				if (!in_interrupt())
					schedule_timeout_interruptible(1);
			} while (time_after(time, jiffies));
		}

		/* If LINK_UP_ST is set, then we are logged into the hub. */
		if (time_before_eq(jiffies, time) && (val & HP100_LINK_UP_ST)) {
#ifdef HP100_DEBUG_TRAINING
			printk("hp100: %s: Successfully logged into the HUB.\n", dev->name);
			if (lp->chip == HP100_CHIPID_LASSEN) {
				val = hp100_inw(TRAIN_ALLOW);
				printk("hp100: %s: Card supports 100VG MAC Version \"%s\" ",
					     dev->name, (hp100_inw(TRAIN_REQUEST) & HP100_CARD_MACVER) ? "802.12" : "Pre");
				printk("Driver will use MAC Version \"%s\"\n", (val & HP100_HUB_MACVER) ? "802.12" : "Pre");
				printk("hp100: %s: Frame format is %s.\n", dev->name, (val & HP100_MALLOW_FRAMEFMT) ? "802.5" : "802.3");
			}
#endif
		} else {
			/* If LINK_UP_ST is not set, login was not successful */
			printk("hp100: %s: Problem logging into the HUB.\n", dev->name);
			if (lp->chip == HP100_CHIPID_LASSEN) {
				/* Check allowed Register to find out why there is a problem. */
				val = hp100_inw(TRAIN_ALLOW);	/* won't work on non-ETR card */
#ifdef HP100_DEBUG_TRAINING
				printk("hp100: %s: MAC Configuration requested: 0x%04x, HUB allowed: 0x%04x\n", dev->name, hp100_inw(TRAIN_REQUEST), val);
#endif
				if (val & HP100_MALLOW_ACCDENIED)
					printk("hp100: %s: HUB access denied.\n", dev->name);
				if (val & HP100_MALLOW_CONFIGURE)
					printk("hp100: %s: MAC Configuration is incompatible with the Network.\n", dev->name);
				if (val & HP100_MALLOW_DUPADDR)
					printk("hp100: %s: Duplicate MAC Address on the Network.\n", dev->name);
			}
		}

		/* If we have put the chip into forced 100 Mbit mode earlier, go back */
		/* to auto-select mode */

		if ((lp->chip == HP100_CHIPID_LASSEN) && (startst & HP100_LINK_CABLE_ST)) {
			hp100_page(MAC_CTRL);
			hp100_orb(HP100_DOT3_MAC, 10_LAN_CFG_2);
		}

		val = hp100_inb(VG_LAN_CFG_1);

		/* Clear the MISC_ERROR Interrupt, which might be generated when doing the relogin */
		hp100_page(PERFORMANCE);
		hp100_outw(HP100_MISC_ERROR, IRQ_STATUS);

		if (val & HP100_LINK_UP_ST)
			return (0);	/* login was ok */
		else {
			printk("hp100: %s: Training failed.\n", dev->name);
			hp100_down_vg_link(dev);
			return -EIO;
		}
	}
	/* no forced relogin & already link there->no training. */
	return -EIO;
}

static void hp100_cascade_reset(struct net_device *dev, u_short enable)
{
	int ioaddr = dev->base_addr;
	struct hp100_private *lp = netdev_priv(dev);

#ifdef HP100_DEBUG_B
	hp100_outw(0x4226, TRACE);
	printk("hp100: %s: cascade_reset\n", dev->name);
#endif

	if (enable) {
		hp100_outw(HP100_HW_RST | HP100_RESET_LB, OPTION_LSW);
		if (lp->chip == HP100_CHIPID_LASSEN) {
			/* Lassen requires a PCI transmit fifo reset */
			hp100_page(HW_MAP);
			hp100_andb(~HP100_PCI_RESET, PCICTRL2);
			hp100_orb(HP100_PCI_RESET, PCICTRL2);
			/* Wait for min. 300 ns */
			/* we can't use jiffies here, because it may be */
			/* that we have disabled the timer... */
			udelay(400);
			hp100_andb(~HP100_PCI_RESET, PCICTRL2);
			hp100_page(PERFORMANCE);
		}
	} else {		/* bring out of reset */
		hp100_outw(HP100_HW_RST | HP100_SET_LB, OPTION_LSW);
		udelay(400);
		hp100_page(PERFORMANCE);
	}
}

#ifdef HP100_DEBUG
void hp100_RegisterDump(struct net_device *dev)
{
	int ioaddr = dev->base_addr;
	int Page;
	int Register;

	/* Dump common registers */
	printk("hp100: %s: Cascade Register Dump\n", dev->name);
	printk("hardware id #1: 0x%.2x\n", hp100_inb(HW_ID));
	printk("hardware id #2/paging: 0x%.2x\n", hp100_inb(PAGING));
	printk("option #1: 0x%.4x\n", hp100_inw(OPTION_LSW));
	printk("option #2: 0x%.4x\n", hp100_inw(OPTION_MSW));

	/* Dump paged registers */
	for (Page = 0; Page < 8; Page++) {
		/* Dump registers */
		printk("page: 0x%.2x\n", Page);
		outw(Page, ioaddr + 0x02);
		for (Register = 0x8; Register < 0x22; Register += 2) {
			/* Display Register contents except data port */
			if (((Register != 0x10) && (Register != 0x12)) || (Page > 0)) {
				printk("0x%.2x = 0x%.4x\n", Register, inw(ioaddr + Register));
			}
		}
	}
	hp100_page(PERFORMANCE);
}
#endif


static void cleanup_dev(struct net_device *d)
{
	struct hp100_private *p = netdev_priv(d);

	unregister_netdev(d);
	release_region(d->base_addr, HP100_REGION_SIZE);

	if (p->mode == 1)	/* busmaster */
		pci_free_consistent(p->pci_dev, MAX_RINGSIZE + 0x0f,
				    p->page_vaddr_algn,
				    virt_to_whatever(d, p->page_vaddr_algn));
	if (p->mem_ptr_virt)
		iounmap(p->mem_ptr_virt);

	free_netdev(d);
}

#ifdef CONFIG_EISA
static int __init hp100_eisa_probe (struct device *gendev)
{
	struct net_device *dev = alloc_etherdev(sizeof(struct hp100_private));
	struct eisa_device *edev = to_eisa_device(gendev);
	int err;

	if (!dev)
		return -ENOMEM;

	SET_MODULE_OWNER(dev);
	SET_NETDEV_DEV(dev, &edev->dev);

	err = hp100_probe1(dev, edev->base_addr + 0xC38, HP100_BUS_EISA, NULL);
	if (err)
		goto out1;

#ifdef HP100_DEBUG
	printk("hp100: %s: EISA adapter found at 0x%x\n", dev->name,
	       dev->base_addr);
#endif
	gendev->driver_data = dev;
	return 0;
 out1:
	free_netdev(dev);
	return err;
}

static int __devexit hp100_eisa_remove (struct device *gendev)
{
	struct net_device *dev = gendev->driver_data;
	cleanup_dev(dev);
	return 0;
}

static struct eisa_driver hp100_eisa_driver = {
        .id_table = hp100_eisa_tbl,
        .driver   = {
                .name    = "hp100",
                .probe   = hp100_eisa_probe,
                .remove  = __devexit_p (hp100_eisa_remove),
        }
};
#endif

#ifdef CONFIG_PCI
static int __devinit hp100_pci_probe (struct pci_dev *pdev,
				     const struct pci_device_id *ent)
{
	struct net_device *dev;
	int ioaddr;
	u_short pci_command;
	int err;

	if (pci_enable_device(pdev))
		return -ENODEV;

	dev = alloc_etherdev(sizeof(struct hp100_private));
	if (!dev) {
		err = -ENOMEM;
		goto out0;
	}

	SET_MODULE_OWNER(dev);
	SET_NETDEV_DEV(dev, &pdev->dev);

	pci_read_config_word(pdev, PCI_COMMAND, &pci_command);
	if (!(pci_command & PCI_COMMAND_IO)) {
#ifdef HP100_DEBUG
		printk("hp100: %s: PCI I/O Bit has not been set. Setting...\n", dev->name);
#endif
		pci_command |= PCI_COMMAND_IO;
		pci_write_config_word(pdev, PCI_COMMAND, pci_command);
	}

	if (!(pci_command & PCI_COMMAND_MASTER)) {
#ifdef HP100_DEBUG
		printk("hp100: %s: PCI Master Bit has not been set. Setting...\n", dev->name);
#endif
		pci_command |= PCI_COMMAND_MASTER;
		pci_write_config_word(pdev, PCI_COMMAND, pci_command);
	}

	ioaddr = pci_resource_start(pdev, 0);
	err = hp100_probe1(dev, ioaddr, HP100_BUS_PCI, pdev);
	if (err)
		goto out1;

#ifdef HP100_DEBUG
	printk("hp100: %s: PCI adapter found at 0x%x\n", dev->name, ioaddr);
#endif
	pci_set_drvdata(pdev, dev);
	return 0;
 out1:
	free_netdev(dev);
 out0:
	pci_disable_device(pdev);
	return err;
}

static void __devexit hp100_pci_remove (struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);

	cleanup_dev(dev);
	pci_disable_device(pdev);
}


static struct pci_driver hp100_pci_driver = {
	.name		= "hp100",
	.id_table	= hp100_pci_tbl,
	.probe		= hp100_pci_probe,
	.remove		= __devexit_p(hp100_pci_remove),
};
#endif

/*
 *  module section
 */

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jaroslav Kysela <perex@suse.cz>, "
              "Siegfried \"Frieder\" Loeffler (dg1sek) <floeff@mathematik.uni-stuttgart.de>");
MODULE_DESCRIPTION("HP CASCADE Architecture Driver for 100VG-AnyLan Network Adapters");

/*
 * Note: to register three isa devices, use:
 * option hp100 hp100_port=0,0,0
 *        to register one card at io 0x280 as eth239, use:
 * option hp100 hp100_port=0x280
 */
#if defined(MODULE) && defined(CONFIG_ISA)
#define HP100_DEVICES 5
/* Parameters set by insmod */
static int hp100_port[HP100_DEVICES] = { 0, [1 ... (HP100_DEVICES-1)] = -1 };
module_param_array(hp100_port, int, NULL, 0);

/* List of devices */
static struct net_device *hp100_devlist[HP100_DEVICES];

static int __init hp100_isa_init(void)
{
	struct net_device *dev;
	int i, err, cards = 0;

	/* Don't autoprobe ISA bus */
	if (hp100_port[0] == 0)
		return -ENODEV;

	/* Loop on all possible base addresses */
	for (i = 0; i < HP100_DEVICES && hp100_port[i] != -1; ++i) {
		dev = alloc_etherdev(sizeof(struct hp100_private));
		if (!dev) {
			printk(KERN_WARNING "hp100: no memory for network device\n");
			while (cards > 0)
				cleanup_dev(hp100_devlist[--cards]);

			return -ENOMEM;
		}
		SET_MODULE_OWNER(dev);

		err = hp100_isa_probe(dev, hp100_port[i]);
		if (!err)
			hp100_devlist[cards++] = dev;
		else
			free_netdev(dev);
	}

	return cards > 0 ? 0 : -ENODEV;
}

static void hp100_isa_cleanup(void)
{
	int i;

	for (i = 0; i < HP100_DEVICES; i++) {
		struct net_device *dev = hp100_devlist[i];
		if (dev)
			cleanup_dev(dev);
	}
}
#else
#define hp100_isa_init()	(0)
#define hp100_isa_cleanup()	do { } while(0)
#endif

static int __init hp100_module_init(void)
{
	int err;

	err = hp100_isa_init();
	if (err && err != -ENODEV)
		goto out;
#ifdef CONFIG_EISA
	err = eisa_driver_register(&hp100_eisa_driver);
	if (err && err != -ENODEV)
		goto out2;
#endif
#ifdef CONFIG_PCI
	err = pci_register_driver(&hp100_pci_driver);
	if (err && err != -ENODEV)
		goto out3;
#endif
 out:
	return err;
 out3:
#ifdef CONFIG_EISA
	eisa_driver_unregister (&hp100_eisa_driver);
 out2:
#endif
	hp100_isa_cleanup();
	goto out;
}


static void __exit hp100_module_exit(void)
{
	hp100_isa_cleanup();
#ifdef CONFIG_EISA
	eisa_driver_unregister (&hp100_eisa_driver);
#endif
#ifdef CONFIG_PCI
	pci_unregister_driver (&hp100_pci_driver);
#endif
}

module_init(hp100_module_init)
module_exit(hp100_module_exit)


/*
 * Local variables:
 *  compile-command: "gcc -D__KERNEL__ -I/usr/src/linux/net/inet -Wall -Wstrict-prototypes -O6 -m486 -c hp100.c"
 *  c-indent-level: 2
 *  tab-width: 8
 * End:
 */
