/*	tulip_core.c: A DEC 21x4x-family ethernet driver for Linux.

	Copyright 2000,2001  The Linux Kernel Team
	Written/copyright 1994-2001 by Donald Becker.

	This software may be used and distributed according to the terms
	of the GNU General Public License, incorporated herein by reference.

	Please submit bugs to http://bugzilla.kernel.org/ .
*/

#define pr_fmt(fmt) "tulip: " fmt

#define DRV_NAME	"tulip"
#ifdef CONFIG_TULIP_NAPI
#define DRV_VERSION    "1.1.15-NAPI" /* Keep at least for test */
#else
#define DRV_VERSION	"1.1.15"
#endif
#define DRV_RELDATE	"Feb 27, 2007"


#include <linux/module.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include "tulip.h"
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/etherdevice.h>
#include <linux/delay.h>
#include <linux/mii.h>
#include <linux/crc32.h>
#include <asm/unaligned.h>
#include <linux/uaccess.h>

#ifdef CONFIG_SPARC
#include <asm/prom.h>
#endif

static char version[] =
	"Linux Tulip driver version " DRV_VERSION " (" DRV_RELDATE ")\n";

/* A few user-configurable values. */

/* Maximum events (Rx packets, etc.) to handle at each interrupt. */
static unsigned int max_interrupt_work = 25;

#define MAX_UNITS 8
/* Used to pass the full-duplex flag, etc. */
static int full_duplex[MAX_UNITS];
static int options[MAX_UNITS];
static int mtu[MAX_UNITS];			/* Jumbo MTU for interfaces. */

/*  The possible media types that can be set in options[] are: */
const char * const medianame[32] = {
	"10baseT", "10base2", "AUI", "100baseTx",
	"10baseT-FDX", "100baseTx-FDX", "100baseT4", "100baseFx",
	"100baseFx-FDX", "MII 10baseT", "MII 10baseT-FDX", "MII",
	"10baseT(forced)", "MII 100baseTx", "MII 100baseTx-FDX", "MII 100baseT4",
	"MII 100baseFx-HDX", "MII 100baseFx-FDX", "Home-PNA 1Mbps", "Invalid-19",
	"","","","", "","","","",  "","","","Transceiver reset",
};

/* Set the copy breakpoint for the copy-only-tiny-buffer Rx structure. */
#if defined(__alpha__) || defined(__arm__) || defined(__hppa__) || \
	defined(CONFIG_SPARC) || defined(__ia64__) || \
	defined(__sh__) || defined(__mips__)
static int rx_copybreak = 1518;
#else
static int rx_copybreak = 100;
#endif

/*
  Set the bus performance register.
	Typical: Set 16 longword cache alignment, no burst limit.
	Cache alignment bits 15:14	     Burst length 13:8
		0000	No alignment  0x00000000 unlimited		0800 8 longwords
		4000	8  longwords		0100 1 longword		1000 16 longwords
		8000	16 longwords		0200 2 longwords	2000 32 longwords
		C000	32  longwords		0400 4 longwords
	Warning: many older 486 systems are broken and require setting 0x00A04800
	   8 longword cache alignment, 8 longword burst.
	ToDo: Non-Intel setting could be better.
*/

#if defined(__alpha__) || defined(__ia64__)
static int csr0 = 0x01A00000 | 0xE000;
#elif defined(__i386__) || defined(__powerpc__) || defined(__x86_64__)
static int csr0 = 0x01A00000 | 0x8000;
#elif defined(CONFIG_SPARC) || defined(__hppa__)
/* The UltraSparc PCI controllers will disconnect at every 64-byte
 * crossing anyways so it makes no sense to tell Tulip to burst
 * any more than that.
 */
static int csr0 = 0x01A00000 | 0x9000;
#elif defined(__arm__) || defined(__sh__)
static int csr0 = 0x01A00000 | 0x4800;
#elif defined(__mips__)
static int csr0 = 0x00200000 | 0x4000;
#else
static int csr0;
#endif

/* Operational parameters that usually are not changed. */
/* Time in jiffies before concluding the transmitter is hung. */
#define TX_TIMEOUT  (4*HZ)


MODULE_AUTHOR("The Linux Kernel Team");
MODULE_DESCRIPTION("Digital 21*4* Tulip ethernet driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);
module_param(tulip_debug, int, 0);
module_param(max_interrupt_work, int, 0);
module_param(rx_copybreak, int, 0);
module_param(csr0, int, 0);
module_param_array(options, int, NULL, 0);
module_param_array(full_duplex, int, NULL, 0);

#ifdef TULIP_DEBUG
int tulip_debug = TULIP_DEBUG;
#else
int tulip_debug = 1;
#endif

static void tulip_timer(struct timer_list *t)
{
	struct tulip_private *tp = from_timer(tp, t, timer);
	struct net_device *dev = tp->dev;

	if (netif_running(dev))
		schedule_work(&tp->media_work);
}

/*
 * This table use during operation for capabilities and media timer.
 *
 * It is indexed via the values in 'enum chips'
 */

const struct tulip_chip_table tulip_tbl[] = {
  { }, /* placeholder for array, slot unused currently */
  { }, /* placeholder for array, slot unused currently */

  /* DC21140 */
  { "Digital DS21140 Tulip", 128, 0x0001ebef,
	HAS_MII | HAS_MEDIA_TABLE | CSR12_IN_SROM | HAS_PCI_MWI, tulip_timer,
	tulip_media_task },

  /* DC21142, DC21143 */
  { "Digital DS21142/43 Tulip", 128, 0x0801fbff,
	HAS_MII | HAS_MEDIA_TABLE | ALWAYS_CHECK_MII | HAS_ACPI | HAS_NWAY
	| HAS_INTR_MITIGATION | HAS_PCI_MWI, tulip_timer, t21142_media_task },

  /* LC82C168 */
  { "Lite-On 82c168 PNIC", 256, 0x0001fbef,
	HAS_MII | HAS_PNICNWAY, pnic_timer, },

  /* MX98713 */
  { "Macronix 98713 PMAC", 128, 0x0001ebef,
	HAS_MII | HAS_MEDIA_TABLE | CSR12_IN_SROM, mxic_timer, },

  /* MX98715 */
  { "Macronix 98715 PMAC", 256, 0x0001ebef,
	HAS_MEDIA_TABLE, mxic_timer, },

  /* MX98725 */
  { "Macronix 98725 PMAC", 256, 0x0001ebef,
	HAS_MEDIA_TABLE, mxic_timer, },

  /* AX88140 */
  { "ASIX AX88140", 128, 0x0001fbff,
	HAS_MII | HAS_MEDIA_TABLE | CSR12_IN_SROM | MC_HASH_ONLY
	| IS_ASIX, tulip_timer, tulip_media_task },

  /* PNIC2 */
  { "Lite-On PNIC-II", 256, 0x0801fbff,
	HAS_MII | HAS_NWAY | HAS_8023X | HAS_PCI_MWI, pnic2_timer, },

  /* COMET */
  { "ADMtek Comet", 256, 0x0001abef,
	HAS_MII | MC_HASH_ONLY | COMET_MAC_ADDR, comet_timer, },

  /* COMPEX9881 */
  { "Compex 9881 PMAC", 128, 0x0001ebef,
	HAS_MII | HAS_MEDIA_TABLE | CSR12_IN_SROM, mxic_timer, },

  /* I21145 */
  { "Intel DS21145 Tulip", 128, 0x0801fbff,
	HAS_MII | HAS_MEDIA_TABLE | ALWAYS_CHECK_MII | HAS_ACPI
	| HAS_NWAY | HAS_PCI_MWI, tulip_timer, tulip_media_task },

  /* DM910X */
#ifdef CONFIG_TULIP_DM910X
  { "Davicom DM9102/DM9102A", 128, 0x0001ebef,
	HAS_MII | HAS_MEDIA_TABLE | CSR12_IN_SROM | HAS_ACPI,
	tulip_timer, tulip_media_task },
#else
  { NULL },
#endif

  /* RS7112 */
  { "Conexant LANfinity", 256, 0x0001ebef,
	HAS_MII | HAS_ACPI, tulip_timer, tulip_media_task },

};


static const struct pci_device_id tulip_pci_tbl[] = {
	{ 0x1011, 0x0009, PCI_ANY_ID, PCI_ANY_ID, 0, 0, DC21140 },
	{ 0x1011, 0x0019, PCI_ANY_ID, PCI_ANY_ID, 0, 0, DC21143 },
	{ 0x11AD, 0x0002, PCI_ANY_ID, PCI_ANY_ID, 0, 0, LC82C168 },
	{ 0x10d9, 0x0512, PCI_ANY_ID, PCI_ANY_ID, 0, 0, MX98713 },
	{ 0x10d9, 0x0531, PCI_ANY_ID, PCI_ANY_ID, 0, 0, MX98715 },
/*	{ 0x10d9, 0x0531, PCI_ANY_ID, PCI_ANY_ID, 0, 0, MX98725 },*/
	{ 0x125B, 0x1400, PCI_ANY_ID, PCI_ANY_ID, 0, 0, AX88140 },
	{ 0x11AD, 0xc115, PCI_ANY_ID, PCI_ANY_ID, 0, 0, PNIC2 },
	{ 0x1317, 0x0981, PCI_ANY_ID, PCI_ANY_ID, 0, 0, COMET },
	{ 0x1317, 0x0985, PCI_ANY_ID, PCI_ANY_ID, 0, 0, COMET },
	{ 0x1317, 0x1985, PCI_ANY_ID, PCI_ANY_ID, 0, 0, COMET },
	{ 0x1317, 0x9511, PCI_ANY_ID, PCI_ANY_ID, 0, 0, COMET },
	{ 0x13D1, 0xAB02, PCI_ANY_ID, PCI_ANY_ID, 0, 0, COMET },
	{ 0x13D1, 0xAB03, PCI_ANY_ID, PCI_ANY_ID, 0, 0, COMET },
	{ 0x13D1, 0xAB08, PCI_ANY_ID, PCI_ANY_ID, 0, 0, COMET },
	{ 0x104A, 0x0981, PCI_ANY_ID, PCI_ANY_ID, 0, 0, COMET },
	{ 0x104A, 0x2774, PCI_ANY_ID, PCI_ANY_ID, 0, 0, COMET },
	{ 0x1259, 0xa120, PCI_ANY_ID, PCI_ANY_ID, 0, 0, COMET },
	{ 0x11F6, 0x9881, PCI_ANY_ID, PCI_ANY_ID, 0, 0, COMPEX9881 },
	{ 0x8086, 0x0039, PCI_ANY_ID, PCI_ANY_ID, 0, 0, I21145 },
#ifdef CONFIG_TULIP_DM910X
	{ 0x1282, 0x9100, PCI_ANY_ID, PCI_ANY_ID, 0, 0, DM910X },
	{ 0x1282, 0x9102, PCI_ANY_ID, PCI_ANY_ID, 0, 0, DM910X },
#endif
	{ 0x1113, 0x1216, PCI_ANY_ID, PCI_ANY_ID, 0, 0, COMET },
	{ 0x1113, 0x1217, PCI_ANY_ID, PCI_ANY_ID, 0, 0, MX98715 },
	{ 0x1113, 0x9511, PCI_ANY_ID, PCI_ANY_ID, 0, 0, COMET },
	{ 0x1186, 0x1541, PCI_ANY_ID, PCI_ANY_ID, 0, 0, COMET },
	{ 0x1186, 0x1561, PCI_ANY_ID, PCI_ANY_ID, 0, 0, COMET },
	{ 0x1186, 0x1591, PCI_ANY_ID, PCI_ANY_ID, 0, 0, COMET },
	{ 0x14f1, 0x1803, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CONEXANT },
	{ 0x1626, 0x8410, PCI_ANY_ID, PCI_ANY_ID, 0, 0, COMET },
	{ 0x1737, 0xAB09, PCI_ANY_ID, PCI_ANY_ID, 0, 0, COMET },
	{ 0x1737, 0xAB08, PCI_ANY_ID, PCI_ANY_ID, 0, 0, COMET },
	{ 0x17B3, 0xAB08, PCI_ANY_ID, PCI_ANY_ID, 0, 0, COMET },
	{ 0x10b7, 0x9300, PCI_ANY_ID, PCI_ANY_ID, 0, 0, COMET }, /* 3Com 3CSOHO100B-TX */
	{ 0x14ea, 0xab08, PCI_ANY_ID, PCI_ANY_ID, 0, 0, COMET }, /* Planex FNW-3602-TX */
	{ 0x1414, 0x0001, PCI_ANY_ID, PCI_ANY_ID, 0, 0, COMET }, /* Microsoft MN-120 */
	{ 0x1414, 0x0002, PCI_ANY_ID, PCI_ANY_ID, 0, 0, COMET },
	{ } /* terminate list */
};
MODULE_DEVICE_TABLE(pci, tulip_pci_tbl);


/* A full-duplex map for media types. */
const char tulip_media_cap[32] =
{0,0,0,16,  3,19,16,24,  27,4,7,5, 0,20,23,20,  28,31,0,0, };

static void tulip_tx_timeout(struct net_device *dev);
static void tulip_init_ring(struct net_device *dev);
static void tulip_free_ring(struct net_device *dev);
static netdev_tx_t tulip_start_xmit(struct sk_buff *skb,
					  struct net_device *dev);
static int tulip_open(struct net_device *dev);
static int tulip_close(struct net_device *dev);
static void tulip_up(struct net_device *dev);
static void tulip_down(struct net_device *dev);
static struct net_device_stats *tulip_get_stats(struct net_device *dev);
static int private_ioctl(struct net_device *dev, struct ifreq *rq, int cmd);
static void set_rx_mode(struct net_device *dev);
static void tulip_set_wolopts(struct pci_dev *pdev, u32 wolopts);
#ifdef CONFIG_NET_POLL_CONTROLLER
static void poll_tulip(struct net_device *dev);
#endif

static void tulip_set_power_state (struct tulip_private *tp,
				   int sleep, int snooze)
{
	if (tp->flags & HAS_ACPI) {
		u32 tmp, newtmp;
		pci_read_config_dword (tp->pdev, CFDD, &tmp);
		newtmp = tmp & ~(CFDD_Sleep | CFDD_Snooze);
		if (sleep)
			newtmp |= CFDD_Sleep;
		else if (snooze)
			newtmp |= CFDD_Snooze;
		if (tmp != newtmp)
			pci_write_config_dword (tp->pdev, CFDD, newtmp);
	}

}


static void tulip_up(struct net_device *dev)
{
	struct tulip_private *tp = netdev_priv(dev);
	void __iomem *ioaddr = tp->base_addr;
	int next_tick = 3*HZ;
	u32 reg;
	int i;

#ifdef CONFIG_TULIP_NAPI
	napi_enable(&tp->napi);
#endif

	/* Wake the chip from sleep/snooze mode. */
	tulip_set_power_state (tp, 0, 0);

	/* Disable all WOL events */
	pci_enable_wake(tp->pdev, PCI_D3hot, 0);
	pci_enable_wake(tp->pdev, PCI_D3cold, 0);
	tulip_set_wolopts(tp->pdev, 0);

	/* On some chip revs we must set the MII/SYM port before the reset!? */
	if (tp->mii_cnt  ||  (tp->mtable  &&  tp->mtable->has_mii))
		iowrite32(0x00040000, ioaddr + CSR6);

	/* Reset the chip, holding bit 0 set at least 50 PCI cycles. */
	iowrite32(0x00000001, ioaddr + CSR0);
	pci_read_config_dword(tp->pdev, PCI_COMMAND, &reg);  /* flush write */
	udelay(100);

	/* Deassert reset.
	   Wait the specified 50 PCI cycles after a reset by initializing
	   Tx and Rx queues and the address filter list. */
	iowrite32(tp->csr0, ioaddr + CSR0);
	pci_read_config_dword(tp->pdev, PCI_COMMAND, &reg);  /* flush write */
	udelay(100);

	if (tulip_debug > 1)
		netdev_dbg(dev, "tulip_up(), irq==%d\n", tp->pdev->irq);

	iowrite32(tp->rx_ring_dma, ioaddr + CSR3);
	iowrite32(tp->tx_ring_dma, ioaddr + CSR4);
	tp->cur_rx = tp->cur_tx = 0;
	tp->dirty_rx = tp->dirty_tx = 0;

	if (tp->flags & MC_HASH_ONLY) {
		u32 addr_low = get_unaligned_le32(dev->dev_addr);
		u32 addr_high = get_unaligned_le16(dev->dev_addr + 4);
		if (tp->chip_id == AX88140) {
			iowrite32(0, ioaddr + CSR13);
			iowrite32(addr_low,  ioaddr + CSR14);
			iowrite32(1, ioaddr + CSR13);
			iowrite32(addr_high, ioaddr + CSR14);
		} else if (tp->flags & COMET_MAC_ADDR) {
			iowrite32(addr_low,  ioaddr + 0xA4);
			iowrite32(addr_high, ioaddr + 0xA8);
			iowrite32(0, ioaddr + CSR27);
			iowrite32(0, ioaddr + CSR28);
		}
	} else {
		/* This is set_rx_mode(), but without starting the transmitter. */
		u16 *eaddrs = (u16 *)dev->dev_addr;
		u16 *setup_frm = &tp->setup_frame[15*6];
		dma_addr_t mapping;

		/* 21140 bug: you must add the broadcast address. */
		memset(tp->setup_frame, 0xff, sizeof(tp->setup_frame));
		/* Fill the final entry of the table with our physical address. */
		*setup_frm++ = eaddrs[0]; *setup_frm++ = eaddrs[0];
		*setup_frm++ = eaddrs[1]; *setup_frm++ = eaddrs[1];
		*setup_frm++ = eaddrs[2]; *setup_frm++ = eaddrs[2];

		mapping = pci_map_single(tp->pdev, tp->setup_frame,
					 sizeof(tp->setup_frame),
					 PCI_DMA_TODEVICE);
		tp->tx_buffers[tp->cur_tx].skb = NULL;
		tp->tx_buffers[tp->cur_tx].mapping = mapping;

		/* Put the setup frame on the Tx list. */
		tp->tx_ring[tp->cur_tx].length = cpu_to_le32(0x08000000 | 192);
		tp->tx_ring[tp->cur_tx].buffer1 = cpu_to_le32(mapping);
		tp->tx_ring[tp->cur_tx].status = cpu_to_le32(DescOwned);

		tp->cur_tx++;
	}

	tp->saved_if_port = dev->if_port;
	if (dev->if_port == 0)
		dev->if_port = tp->default_port;

	/* Allow selecting a default media. */
	i = 0;
	if (tp->mtable == NULL)
		goto media_picked;
	if (dev->if_port) {
		int looking_for = tulip_media_cap[dev->if_port] & MediaIsMII ? 11 :
			(dev->if_port == 12 ? 0 : dev->if_port);
		for (i = 0; i < tp->mtable->leafcount; i++)
			if (tp->mtable->mleaf[i].media == looking_for) {
				dev_info(&dev->dev,
					 "Using user-specified media %s\n",
					 medianame[dev->if_port]);
				goto media_picked;
			}
	}
	if ((tp->mtable->defaultmedia & 0x0800) == 0) {
		int looking_for = tp->mtable->defaultmedia & MEDIA_MASK;
		for (i = 0; i < tp->mtable->leafcount; i++)
			if (tp->mtable->mleaf[i].media == looking_for) {
				dev_info(&dev->dev,
					 "Using EEPROM-set media %s\n",
					 medianame[looking_for]);
				goto media_picked;
			}
	}
	/* Start sensing first non-full-duplex media. */
	for (i = tp->mtable->leafcount - 1;
		 (tulip_media_cap[tp->mtable->mleaf[i].media] & MediaAlwaysFD) && i > 0; i--)
		;
media_picked:

	tp->csr6 = 0;
	tp->cur_index = i;
	tp->nwayset = 0;

	if (dev->if_port) {
		if (tp->chip_id == DC21143  &&
		    (tulip_media_cap[dev->if_port] & MediaIsMII)) {
			/* We must reset the media CSRs when we force-select MII mode. */
			iowrite32(0x0000, ioaddr + CSR13);
			iowrite32(0x0000, ioaddr + CSR14);
			iowrite32(0x0008, ioaddr + CSR15);
		}
		tulip_select_media(dev, 1);
	} else if (tp->chip_id == DC21142) {
		if (tp->mii_cnt) {
			tulip_select_media(dev, 1);
			if (tulip_debug > 1)
				dev_info(&dev->dev,
					 "Using MII transceiver %d, status %04x\n",
					 tp->phys[0],
					 tulip_mdio_read(dev, tp->phys[0], 1));
			iowrite32(csr6_mask_defstate, ioaddr + CSR6);
			tp->csr6 = csr6_mask_hdcap;
			dev->if_port = 11;
			iowrite32(0x0000, ioaddr + CSR13);
			iowrite32(0x0000, ioaddr + CSR14);
		} else
			t21142_start_nway(dev);
	} else if (tp->chip_id == PNIC2) {
	        /* for initial startup advertise 10/100 Full and Half */
	        tp->sym_advertise = 0x01E0;
                /* enable autonegotiate end interrupt */
	        iowrite32(ioread32(ioaddr+CSR5)| 0x00008010, ioaddr + CSR5);
	        iowrite32(ioread32(ioaddr+CSR7)| 0x00008010, ioaddr + CSR7);
		pnic2_start_nway(dev);
	} else if (tp->chip_id == LC82C168  &&  ! tp->medialock) {
		if (tp->mii_cnt) {
			dev->if_port = 11;
			tp->csr6 = 0x814C0000 | (tp->full_duplex ? 0x0200 : 0);
			iowrite32(0x0001, ioaddr + CSR15);
		} else if (ioread32(ioaddr + CSR5) & TPLnkPass)
			pnic_do_nway(dev);
		else {
			/* Start with 10mbps to do autonegotiation. */
			iowrite32(0x32, ioaddr + CSR12);
			tp->csr6 = 0x00420000;
			iowrite32(0x0001B078, ioaddr + 0xB8);
			iowrite32(0x0201B078, ioaddr + 0xB8);
			next_tick = 1*HZ;
		}
	} else if ((tp->chip_id == MX98713 || tp->chip_id == COMPEX9881) &&
		   ! tp->medialock) {
		dev->if_port = 0;
		tp->csr6 = 0x01880000 | (tp->full_duplex ? 0x0200 : 0);
		iowrite32(0x0f370000 | ioread16(ioaddr + 0x80), ioaddr + 0x80);
	} else if (tp->chip_id == MX98715 || tp->chip_id == MX98725) {
		/* Provided by BOLO, Macronix - 12/10/1998. */
		dev->if_port = 0;
		tp->csr6 = 0x01a80200;
		iowrite32(0x0f370000 | ioread16(ioaddr + 0x80), ioaddr + 0x80);
		iowrite32(0x11000 | ioread16(ioaddr + 0xa0), ioaddr + 0xa0);
	} else if (tp->chip_id == COMET || tp->chip_id == CONEXANT) {
		/* Enable automatic Tx underrun recovery. */
		iowrite32(ioread32(ioaddr + 0x88) | 1, ioaddr + 0x88);
		dev->if_port = tp->mii_cnt ? 11 : 0;
		tp->csr6 = 0x00040000;
	} else if (tp->chip_id == AX88140) {
		tp->csr6 = tp->mii_cnt ? 0x00040100 : 0x00000100;
	} else
		tulip_select_media(dev, 1);

	/* Start the chip's Tx to process setup frame. */
	tulip_stop_rxtx(tp);
	barrier();
	udelay(5);
	iowrite32(tp->csr6 | TxOn, ioaddr + CSR6);

	/* Enable interrupts by setting the interrupt mask. */
	iowrite32(tulip_tbl[tp->chip_id].valid_intrs, ioaddr + CSR5);
	iowrite32(tulip_tbl[tp->chip_id].valid_intrs, ioaddr + CSR7);
	tulip_start_rxtx(tp);
	iowrite32(0, ioaddr + CSR2);		/* Rx poll demand */

	if (tulip_debug > 2) {
		netdev_dbg(dev, "Done tulip_up(), CSR0 %08x, CSR5 %08x CSR6 %08x\n",
			   ioread32(ioaddr + CSR0),
			   ioread32(ioaddr + CSR5),
			   ioread32(ioaddr + CSR6));
	}

	/* Set the timer to switch to check for link beat and perhaps switch
	   to an alternate media type. */
	tp->timer.expires = RUN_AT(next_tick);
	add_timer(&tp->timer);
#ifdef CONFIG_TULIP_NAPI
	timer_setup(&tp->oom_timer, oom_timer, 0);
#endif
}

static int
tulip_open(struct net_device *dev)
{
	struct tulip_private *tp = netdev_priv(dev);
	int retval;

	tulip_init_ring (dev);

	retval = request_irq(tp->pdev->irq, tulip_interrupt, IRQF_SHARED,
			     dev->name, dev);
	if (retval)
		goto free_ring;

	tulip_up (dev);

	netif_start_queue (dev);

	return 0;

free_ring:
	tulip_free_ring (dev);
	return retval;
}


static void tulip_tx_timeout(struct net_device *dev)
{
	struct tulip_private *tp = netdev_priv(dev);
	void __iomem *ioaddr = tp->base_addr;
	unsigned long flags;

	spin_lock_irqsave (&tp->lock, flags);

	if (tulip_media_cap[dev->if_port] & MediaIsMII) {
		/* Do nothing -- the media monitor should handle this. */
		if (tulip_debug > 1)
			dev_warn(&dev->dev,
				 "Transmit timeout using MII device\n");
	} else if (tp->chip_id == DC21140 || tp->chip_id == DC21142 ||
		   tp->chip_id == MX98713 || tp->chip_id == COMPEX9881 ||
		   tp->chip_id == DM910X) {
		dev_warn(&dev->dev,
			 "21140 transmit timed out, status %08x, SIA %08x %08x %08x %08x, resetting...\n",
			 ioread32(ioaddr + CSR5), ioread32(ioaddr + CSR12),
			 ioread32(ioaddr + CSR13), ioread32(ioaddr + CSR14),
			 ioread32(ioaddr + CSR15));
		tp->timeout_recovery = 1;
		schedule_work(&tp->media_work);
		goto out_unlock;
	} else if (tp->chip_id == PNIC2) {
		dev_warn(&dev->dev,
			 "PNIC2 transmit timed out, status %08x, CSR6/7 %08x / %08x CSR12 %08x, resetting...\n",
			 (int)ioread32(ioaddr + CSR5),
			 (int)ioread32(ioaddr + CSR6),
			 (int)ioread32(ioaddr + CSR7),
			 (int)ioread32(ioaddr + CSR12));
	} else {
		dev_warn(&dev->dev,
			 "Transmit timed out, status %08x, CSR12 %08x, resetting...\n",
			 ioread32(ioaddr + CSR5), ioread32(ioaddr + CSR12));
		dev->if_port = 0;
	}

#if defined(way_too_many_messages)
	if (tulip_debug > 3) {
		int i;
		for (i = 0; i < RX_RING_SIZE; i++) {
			u8 *buf = (u8 *)(tp->rx_ring[i].buffer1);
			int j;
			printk(KERN_DEBUG
			       "%2d: %08x %08x %08x %08x  %02x %02x %02x\n",
			       i,
			       (unsigned int)tp->rx_ring[i].status,
			       (unsigned int)tp->rx_ring[i].length,
			       (unsigned int)tp->rx_ring[i].buffer1,
			       (unsigned int)tp->rx_ring[i].buffer2,
			       buf[0], buf[1], buf[2]);
			for (j = 0; ((j < 1600) && buf[j] != 0xee); j++)
				if (j < 100)
					pr_cont(" %02x", buf[j]);
			pr_cont(" j=%d\n", j);
		}
		printk(KERN_DEBUG "  Rx ring %p: ", tp->rx_ring);
		for (i = 0; i < RX_RING_SIZE; i++)
			pr_cont(" %08x", (unsigned int)tp->rx_ring[i].status);
		printk(KERN_DEBUG "  Tx ring %p: ", tp->tx_ring);
		for (i = 0; i < TX_RING_SIZE; i++)
			pr_cont(" %08x", (unsigned int)tp->tx_ring[i].status);
		pr_cont("\n");
	}
#endif

	tulip_tx_timeout_complete(tp, ioaddr);

out_unlock:
	spin_unlock_irqrestore (&tp->lock, flags);
	netif_trans_update(dev); /* prevent tx timeout */
	netif_wake_queue (dev);
}


/* Initialize the Rx and Tx rings, along with various 'dev' bits. */
static void tulip_init_ring(struct net_device *dev)
{
	struct tulip_private *tp = netdev_priv(dev);
	int i;

	tp->susp_rx = 0;
	tp->ttimer = 0;
	tp->nir = 0;

	for (i = 0; i < RX_RING_SIZE; i++) {
		tp->rx_ring[i].status = 0x00000000;
		tp->rx_ring[i].length = cpu_to_le32(PKT_BUF_SZ);
		tp->rx_ring[i].buffer2 = cpu_to_le32(tp->rx_ring_dma + sizeof(struct tulip_rx_desc) * (i + 1));
		tp->rx_buffers[i].skb = NULL;
		tp->rx_buffers[i].mapping = 0;
	}
	/* Mark the last entry as wrapping the ring. */
	tp->rx_ring[i-1].length = cpu_to_le32(PKT_BUF_SZ | DESC_RING_WRAP);
	tp->rx_ring[i-1].buffer2 = cpu_to_le32(tp->rx_ring_dma);

	for (i = 0; i < RX_RING_SIZE; i++) {
		dma_addr_t mapping;

		/* Note the receive buffer must be longword aligned.
		   netdev_alloc_skb() provides 16 byte alignment.  But do *not*
		   use skb_reserve() to align the IP header! */
		struct sk_buff *skb = netdev_alloc_skb(dev, PKT_BUF_SZ);
		tp->rx_buffers[i].skb = skb;
		if (skb == NULL)
			break;
		mapping = pci_map_single(tp->pdev, skb->data,
					 PKT_BUF_SZ, PCI_DMA_FROMDEVICE);
		tp->rx_buffers[i].mapping = mapping;
		tp->rx_ring[i].status = cpu_to_le32(DescOwned);	/* Owned by Tulip chip */
		tp->rx_ring[i].buffer1 = cpu_to_le32(mapping);
	}
	tp->dirty_rx = (unsigned int)(i - RX_RING_SIZE);

	/* The Tx buffer descriptor is filled in as needed, but we
	   do need to clear the ownership bit. */
	for (i = 0; i < TX_RING_SIZE; i++) {
		tp->tx_buffers[i].skb = NULL;
		tp->tx_buffers[i].mapping = 0;
		tp->tx_ring[i].status = 0x00000000;
		tp->tx_ring[i].buffer2 = cpu_to_le32(tp->tx_ring_dma + sizeof(struct tulip_tx_desc) * (i + 1));
	}
	tp->tx_ring[i-1].buffer2 = cpu_to_le32(tp->tx_ring_dma);
}

static netdev_tx_t
tulip_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct tulip_private *tp = netdev_priv(dev);
	int entry;
	u32 flag;
	dma_addr_t mapping;
	unsigned long flags;

	spin_lock_irqsave(&tp->lock, flags);

	/* Calculate the next Tx descriptor entry. */
	entry = tp->cur_tx % TX_RING_SIZE;

	tp->tx_buffers[entry].skb = skb;
	mapping = pci_map_single(tp->pdev, skb->data,
				 skb->len, PCI_DMA_TODEVICE);
	tp->tx_buffers[entry].mapping = mapping;
	tp->tx_ring[entry].buffer1 = cpu_to_le32(mapping);

	if (tp->cur_tx - tp->dirty_tx < TX_RING_SIZE/2) {/* Typical path */
		flag = 0x60000000; /* No interrupt */
	} else if (tp->cur_tx - tp->dirty_tx == TX_RING_SIZE/2) {
		flag = 0xe0000000; /* Tx-done intr. */
	} else if (tp->cur_tx - tp->dirty_tx < TX_RING_SIZE - 2) {
		flag = 0x60000000; /* No Tx-done intr. */
	} else {		/* Leave room for set_rx_mode() to fill entries. */
		flag = 0xe0000000; /* Tx-done intr. */
		netif_stop_queue(dev);
	}
	if (entry == TX_RING_SIZE-1)
		flag = 0xe0000000 | DESC_RING_WRAP;

	tp->tx_ring[entry].length = cpu_to_le32(skb->len | flag);
	/* if we were using Transmit Automatic Polling, we would need a
	 * wmb() here. */
	tp->tx_ring[entry].status = cpu_to_le32(DescOwned);
	wmb();

	tp->cur_tx++;

	/* Trigger an immediate transmit demand. */
	iowrite32(0, tp->base_addr + CSR1);

	spin_unlock_irqrestore(&tp->lock, flags);

	return NETDEV_TX_OK;
}

static void tulip_clean_tx_ring(struct tulip_private *tp)
{
	unsigned int dirty_tx;

	for (dirty_tx = tp->dirty_tx ; tp->cur_tx - dirty_tx > 0;
		dirty_tx++) {
		int entry = dirty_tx % TX_RING_SIZE;
		int status = le32_to_cpu(tp->tx_ring[entry].status);

		if (status < 0) {
			tp->dev->stats.tx_errors++;	/* It wasn't Txed */
			tp->tx_ring[entry].status = 0;
		}

		/* Check for Tx filter setup frames. */
		if (tp->tx_buffers[entry].skb == NULL) {
			/* test because dummy frames not mapped */
			if (tp->tx_buffers[entry].mapping)
				pci_unmap_single(tp->pdev,
					tp->tx_buffers[entry].mapping,
					sizeof(tp->setup_frame),
					PCI_DMA_TODEVICE);
			continue;
		}

		pci_unmap_single(tp->pdev, tp->tx_buffers[entry].mapping,
				tp->tx_buffers[entry].skb->len,
				PCI_DMA_TODEVICE);

		/* Free the original skb. */
		dev_kfree_skb_irq(tp->tx_buffers[entry].skb);
		tp->tx_buffers[entry].skb = NULL;
		tp->tx_buffers[entry].mapping = 0;
	}
}

static void tulip_down (struct net_device *dev)
{
	struct tulip_private *tp = netdev_priv(dev);
	void __iomem *ioaddr = tp->base_addr;
	unsigned long flags;

	cancel_work_sync(&tp->media_work);

#ifdef CONFIG_TULIP_NAPI
	napi_disable(&tp->napi);
#endif

	del_timer_sync (&tp->timer);
#ifdef CONFIG_TULIP_NAPI
	del_timer_sync (&tp->oom_timer);
#endif
	spin_lock_irqsave (&tp->lock, flags);

	/* Disable interrupts by clearing the interrupt mask. */
	iowrite32 (0x00000000, ioaddr + CSR7);

	/* Stop the Tx and Rx processes. */
	tulip_stop_rxtx(tp);

	/* prepare receive buffers */
	tulip_refill_rx(dev);

	/* release any unconsumed transmit buffers */
	tulip_clean_tx_ring(tp);

	if (ioread32(ioaddr + CSR6) != 0xffffffff)
		dev->stats.rx_missed_errors += ioread32(ioaddr + CSR8) & 0xffff;

	spin_unlock_irqrestore (&tp->lock, flags);

	timer_setup(&tp->timer, tulip_tbl[tp->chip_id].media_timer, 0);

	dev->if_port = tp->saved_if_port;

	/* Leave the driver in snooze, not sleep, mode. */
	tulip_set_power_state (tp, 0, 1);
}

static void tulip_free_ring (struct net_device *dev)
{
	struct tulip_private *tp = netdev_priv(dev);
	int i;

	/* Free all the skbuffs in the Rx queue. */
	for (i = 0; i < RX_RING_SIZE; i++) {
		struct sk_buff *skb = tp->rx_buffers[i].skb;
		dma_addr_t mapping = tp->rx_buffers[i].mapping;

		tp->rx_buffers[i].skb = NULL;
		tp->rx_buffers[i].mapping = 0;

		tp->rx_ring[i].status = 0;	/* Not owned by Tulip chip. */
		tp->rx_ring[i].length = 0;
		/* An invalid address. */
		tp->rx_ring[i].buffer1 = cpu_to_le32(0xBADF00D0);
		if (skb) {
			pci_unmap_single(tp->pdev, mapping, PKT_BUF_SZ,
					 PCI_DMA_FROMDEVICE);
			dev_kfree_skb (skb);
		}
	}

	for (i = 0; i < TX_RING_SIZE; i++) {
		struct sk_buff *skb = tp->tx_buffers[i].skb;

		if (skb != NULL) {
			pci_unmap_single(tp->pdev, tp->tx_buffers[i].mapping,
					 skb->len, PCI_DMA_TODEVICE);
			dev_kfree_skb (skb);
		}
		tp->tx_buffers[i].skb = NULL;
		tp->tx_buffers[i].mapping = 0;
	}
}

static int tulip_close (struct net_device *dev)
{
	struct tulip_private *tp = netdev_priv(dev);
	void __iomem *ioaddr = tp->base_addr;

	netif_stop_queue (dev);

	tulip_down (dev);

	if (tulip_debug > 1)
		netdev_dbg(dev, "Shutting down ethercard, status was %02x\n",
			   ioread32 (ioaddr + CSR5));

	free_irq (tp->pdev->irq, dev);

	tulip_free_ring (dev);

	return 0;
}

static struct net_device_stats *tulip_get_stats(struct net_device *dev)
{
	struct tulip_private *tp = netdev_priv(dev);
	void __iomem *ioaddr = tp->base_addr;

	if (netif_running(dev)) {
		unsigned long flags;

		spin_lock_irqsave (&tp->lock, flags);

		dev->stats.rx_missed_errors += ioread32(ioaddr + CSR8) & 0xffff;

		spin_unlock_irqrestore(&tp->lock, flags);
	}

	return &dev->stats;
}


static void tulip_get_drvinfo(struct net_device *dev, struct ethtool_drvinfo *info)
{
	struct tulip_private *np = netdev_priv(dev);
	strlcpy(info->driver, DRV_NAME, sizeof(info->driver));
	strlcpy(info->version, DRV_VERSION, sizeof(info->version));
	strlcpy(info->bus_info, pci_name(np->pdev), sizeof(info->bus_info));
}


static int tulip_ethtool_set_wol(struct net_device *dev,
				 struct ethtool_wolinfo *wolinfo)
{
	struct tulip_private *tp = netdev_priv(dev);

	if (wolinfo->wolopts & (~tp->wolinfo.supported))
		   return -EOPNOTSUPP;

	tp->wolinfo.wolopts = wolinfo->wolopts;
	device_set_wakeup_enable(&tp->pdev->dev, tp->wolinfo.wolopts);
	return 0;
}

static void tulip_ethtool_get_wol(struct net_device *dev,
				  struct ethtool_wolinfo *wolinfo)
{
	struct tulip_private *tp = netdev_priv(dev);

	wolinfo->supported = tp->wolinfo.supported;
	wolinfo->wolopts = tp->wolinfo.wolopts;
	return;
}


static const struct ethtool_ops ops = {
	.get_drvinfo = tulip_get_drvinfo,
	.set_wol     = tulip_ethtool_set_wol,
	.get_wol     = tulip_ethtool_get_wol,
};

/* Provide ioctl() calls to examine the MII xcvr state. */
static int private_ioctl (struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct tulip_private *tp = netdev_priv(dev);
	void __iomem *ioaddr = tp->base_addr;
	struct mii_ioctl_data *data = if_mii(rq);
	const unsigned int phy_idx = 0;
	int phy = tp->phys[phy_idx] & 0x1f;
	unsigned int regnum = data->reg_num;

	switch (cmd) {
	case SIOCGMIIPHY:		/* Get address of MII PHY in use. */
		if (tp->mii_cnt)
			data->phy_id = phy;
		else if (tp->flags & HAS_NWAY)
			data->phy_id = 32;
		else if (tp->chip_id == COMET)
			data->phy_id = 1;
		else
			return -ENODEV;
		/* Fall through */

	case SIOCGMIIREG:		/* Read MII PHY register. */
		if (data->phy_id == 32 && (tp->flags & HAS_NWAY)) {
			int csr12 = ioread32 (ioaddr + CSR12);
			int csr14 = ioread32 (ioaddr + CSR14);
			switch (regnum) {
			case 0:
                                if (((csr14<<5) & 0x1000) ||
                                        (dev->if_port == 5 && tp->nwayset))
                                        data->val_out = 0x1000;
                                else
                                        data->val_out = (tulip_media_cap[dev->if_port]&MediaIs100 ? 0x2000 : 0)
                                                | (tulip_media_cap[dev->if_port]&MediaIsFD ? 0x0100 : 0);
				break;
			case 1:
                                data->val_out =
					0x1848 +
					((csr12&0x7000) == 0x5000 ? 0x20 : 0) +
					((csr12&0x06) == 6 ? 0 : 4);
                                data->val_out |= 0x6048;
				break;
			case 4:
                                /* Advertised value, bogus 10baseTx-FD value from CSR6. */
                                data->val_out =
					((ioread32(ioaddr + CSR6) >> 3) & 0x0040) +
					((csr14 >> 1) & 0x20) + 1;
                                data->val_out |= ((csr14 >> 9) & 0x03C0);
				break;
			case 5: data->val_out = tp->lpar; break;
			default: data->val_out = 0; break;
			}
		} else {
			data->val_out = tulip_mdio_read (dev, data->phy_id & 0x1f, regnum);
		}
		return 0;

	case SIOCSMIIREG:		/* Write MII PHY register. */
		if (regnum & ~0x1f)
			return -EINVAL;
		if (data->phy_id == phy) {
			u16 value = data->val_in;
			switch (regnum) {
			case 0:	/* Check for autonegotiation on or reset. */
				tp->full_duplex_lock = (value & 0x9000) ? 0 : 1;
				if (tp->full_duplex_lock)
					tp->full_duplex = (value & 0x0100) ? 1 : 0;
				break;
			case 4:
				tp->advertising[phy_idx] =
				tp->mii_advertise = data->val_in;
				break;
			}
		}
		if (data->phy_id == 32 && (tp->flags & HAS_NWAY)) {
			u16 value = data->val_in;
			if (regnum == 0) {
			  if ((value & 0x1200) == 0x1200) {
			    if (tp->chip_id == PNIC2) {
                                   pnic2_start_nway (dev);
                            } else {
				   t21142_start_nway (dev);
                            }
			  }
			} else if (regnum == 4)
				tp->sym_advertise = value;
		} else {
			tulip_mdio_write (dev, data->phy_id & 0x1f, regnum, data->val_in);
		}
		return 0;
	default:
		return -EOPNOTSUPP;
	}

	return -EOPNOTSUPP;
}


/* Set or clear the multicast filter for this adaptor.
   Note that we only use exclusion around actually queueing the
   new frame, not around filling tp->setup_frame.  This is non-deterministic
   when re-entered but still correct. */

static void build_setup_frame_hash(u16 *setup_frm, struct net_device *dev)
{
	struct tulip_private *tp = netdev_priv(dev);
	u16 hash_table[32];
	struct netdev_hw_addr *ha;
	int i;
	u16 *eaddrs;

	memset(hash_table, 0, sizeof(hash_table));
	__set_bit_le(255, hash_table);			/* Broadcast entry */
	/* This should work on big-endian machines as well. */
	netdev_for_each_mc_addr(ha, dev) {
		int index = ether_crc_le(ETH_ALEN, ha->addr) & 0x1ff;

		__set_bit_le(index, hash_table);
	}
	for (i = 0; i < 32; i++) {
		*setup_frm++ = hash_table[i];
		*setup_frm++ = hash_table[i];
	}
	setup_frm = &tp->setup_frame[13*6];

	/* Fill the final entry with our physical address. */
	eaddrs = (u16 *)dev->dev_addr;
	*setup_frm++ = eaddrs[0]; *setup_frm++ = eaddrs[0];
	*setup_frm++ = eaddrs[1]; *setup_frm++ = eaddrs[1];
	*setup_frm++ = eaddrs[2]; *setup_frm++ = eaddrs[2];
}

static void build_setup_frame_perfect(u16 *setup_frm, struct net_device *dev)
{
	struct tulip_private *tp = netdev_priv(dev);
	struct netdev_hw_addr *ha;
	u16 *eaddrs;

	/* We have <= 14 addresses so we can use the wonderful
	   16 address perfect filtering of the Tulip. */
	netdev_for_each_mc_addr(ha, dev) {
		eaddrs = (u16 *) ha->addr;
		*setup_frm++ = *eaddrs; *setup_frm++ = *eaddrs++;
		*setup_frm++ = *eaddrs; *setup_frm++ = *eaddrs++;
		*setup_frm++ = *eaddrs; *setup_frm++ = *eaddrs++;
	}
	/* Fill the unused entries with the broadcast address. */
	memset(setup_frm, 0xff, (15 - netdev_mc_count(dev)) * 12);
	setup_frm = &tp->setup_frame[15*6];

	/* Fill the final entry with our physical address. */
	eaddrs = (u16 *)dev->dev_addr;
	*setup_frm++ = eaddrs[0]; *setup_frm++ = eaddrs[0];
	*setup_frm++ = eaddrs[1]; *setup_frm++ = eaddrs[1];
	*setup_frm++ = eaddrs[2]; *setup_frm++ = eaddrs[2];
}


static void set_rx_mode(struct net_device *dev)
{
	struct tulip_private *tp = netdev_priv(dev);
	void __iomem *ioaddr = tp->base_addr;
	int csr6;

	csr6 = ioread32(ioaddr + CSR6) & ~0x00D5;

	tp->csr6 &= ~0x00D5;
	if (dev->flags & IFF_PROMISC) {			/* Set promiscuous. */
		tp->csr6 |= AcceptAllMulticast | AcceptAllPhys;
		csr6 |= AcceptAllMulticast | AcceptAllPhys;
	} else if ((netdev_mc_count(dev) > 1000) ||
		   (dev->flags & IFF_ALLMULTI)) {
		/* Too many to filter well -- accept all multicasts. */
		tp->csr6 |= AcceptAllMulticast;
		csr6 |= AcceptAllMulticast;
	} else	if (tp->flags & MC_HASH_ONLY) {
		/* Some work-alikes have only a 64-entry hash filter table. */
		/* Should verify correctness on big-endian/__powerpc__ */
		struct netdev_hw_addr *ha;
		if (netdev_mc_count(dev) > 64) {
			/* Arbitrary non-effective limit. */
			tp->csr6 |= AcceptAllMulticast;
			csr6 |= AcceptAllMulticast;
		} else {
			u32 mc_filter[2] = {0, 0};		 /* Multicast hash filter */
			int filterbit;
			netdev_for_each_mc_addr(ha, dev) {
				if (tp->flags & COMET_MAC_ADDR)
					filterbit = ether_crc_le(ETH_ALEN,
								 ha->addr);
				else
					filterbit = ether_crc(ETH_ALEN,
							      ha->addr) >> 26;
				filterbit &= 0x3f;
				mc_filter[filterbit >> 5] |= 1 << (filterbit & 31);
				if (tulip_debug > 2)
					dev_info(&dev->dev,
						 "Added filter for %pM  %08x bit %d\n",
						 ha->addr,
						 ether_crc(ETH_ALEN, ha->addr),
						 filterbit);
			}
			if (mc_filter[0] == tp->mc_filter[0]  &&
				mc_filter[1] == tp->mc_filter[1])
				;				/* No change. */
			else if (tp->flags & IS_ASIX) {
				iowrite32(2, ioaddr + CSR13);
				iowrite32(mc_filter[0], ioaddr + CSR14);
				iowrite32(3, ioaddr + CSR13);
				iowrite32(mc_filter[1], ioaddr + CSR14);
			} else if (tp->flags & COMET_MAC_ADDR) {
				iowrite32(mc_filter[0], ioaddr + CSR27);
				iowrite32(mc_filter[1], ioaddr + CSR28);
			}
			tp->mc_filter[0] = mc_filter[0];
			tp->mc_filter[1] = mc_filter[1];
		}
	} else {
		unsigned long flags;
		u32 tx_flags = 0x08000000 | 192;

		/* Note that only the low-address shortword of setup_frame is valid!
		   The values are doubled for big-endian architectures. */
		if (netdev_mc_count(dev) > 14) {
			/* Must use a multicast hash table. */
			build_setup_frame_hash(tp->setup_frame, dev);
			tx_flags = 0x08400000 | 192;
		} else {
			build_setup_frame_perfect(tp->setup_frame, dev);
		}

		spin_lock_irqsave(&tp->lock, flags);

		if (tp->cur_tx - tp->dirty_tx > TX_RING_SIZE - 2) {
			/* Same setup recently queued, we need not add it. */
		} else {
			unsigned int entry;
			int dummy = -1;

			/* Now add this frame to the Tx list. */

			entry = tp->cur_tx++ % TX_RING_SIZE;

			if (entry != 0) {
				/* Avoid a chip errata by prefixing a dummy entry. */
				tp->tx_buffers[entry].skb = NULL;
				tp->tx_buffers[entry].mapping = 0;
				tp->tx_ring[entry].length =
					(entry == TX_RING_SIZE-1) ? cpu_to_le32(DESC_RING_WRAP) : 0;
				tp->tx_ring[entry].buffer1 = 0;
				/* Must set DescOwned later to avoid race with chip */
				dummy = entry;
				entry = tp->cur_tx++ % TX_RING_SIZE;

			}

			tp->tx_buffers[entry].skb = NULL;
			tp->tx_buffers[entry].mapping =
				pci_map_single(tp->pdev, tp->setup_frame,
					       sizeof(tp->setup_frame),
					       PCI_DMA_TODEVICE);
			/* Put the setup frame on the Tx list. */
			if (entry == TX_RING_SIZE-1)
				tx_flags |= DESC_RING_WRAP;		/* Wrap ring. */
			tp->tx_ring[entry].length = cpu_to_le32(tx_flags);
			tp->tx_ring[entry].buffer1 =
				cpu_to_le32(tp->tx_buffers[entry].mapping);
			tp->tx_ring[entry].status = cpu_to_le32(DescOwned);
			if (dummy >= 0)
				tp->tx_ring[dummy].status = cpu_to_le32(DescOwned);
			if (tp->cur_tx - tp->dirty_tx >= TX_RING_SIZE - 2)
				netif_stop_queue(dev);

			/* Trigger an immediate transmit demand. */
			iowrite32(0, ioaddr + CSR1);
		}

		spin_unlock_irqrestore(&tp->lock, flags);
	}

	iowrite32(csr6, ioaddr + CSR6);
}

#ifdef CONFIG_TULIP_MWI
static void tulip_mwi_config(struct pci_dev *pdev, struct net_device *dev)
{
	struct tulip_private *tp = netdev_priv(dev);
	u8 cache;
	u16 pci_command;
	u32 csr0;

	if (tulip_debug > 3)
		netdev_dbg(dev, "tulip_mwi_config()\n");

	tp->csr0 = csr0 = 0;

	/* if we have any cache line size at all, we can do MRM and MWI */
	csr0 |= MRM | MWI;

	/* Enable MWI in the standard PCI command bit.
	 * Check for the case where MWI is desired but not available
	 */
	pci_try_set_mwi(pdev);

	/* read result from hardware (in case bit refused to enable) */
	pci_read_config_word(pdev, PCI_COMMAND, &pci_command);
	if ((csr0 & MWI) && (!(pci_command & PCI_COMMAND_INVALIDATE)))
		csr0 &= ~MWI;

	/* if cache line size hardwired to zero, no MWI */
	pci_read_config_byte(pdev, PCI_CACHE_LINE_SIZE, &cache);
	if ((csr0 & MWI) && (cache == 0)) {
		csr0 &= ~MWI;
		pci_clear_mwi(pdev);
	}

	/* assign per-cacheline-size cache alignment and
	 * burst length values
	 */
	switch (cache) {
	case 8:
		csr0 |= MRL | (1 << CALShift) | (16 << BurstLenShift);
		break;
	case 16:
		csr0 |= MRL | (2 << CALShift) | (16 << BurstLenShift);
		break;
	case 32:
		csr0 |= MRL | (3 << CALShift) | (32 << BurstLenShift);
		break;
	default:
		cache = 0;
		break;
	}

	/* if we have a good cache line size, we by now have a good
	 * csr0, so save it and exit
	 */
	if (cache)
		goto out;

	/* we don't have a good csr0 or cache line size, disable MWI */
	if (csr0 & MWI) {
		pci_clear_mwi(pdev);
		csr0 &= ~MWI;
	}

	/* sane defaults for burst length and cache alignment
	 * originally from de4x5 driver
	 */
	csr0 |= (8 << BurstLenShift) | (1 << CALShift);

out:
	tp->csr0 = csr0;
	if (tulip_debug > 2)
		netdev_dbg(dev, "MWI config cacheline=%d, csr0=%08x\n",
			   cache, csr0);
}
#endif

/*
 *	Chips that have the MRM/reserved bit quirk and the burst quirk. That
 *	is the DM910X and the on chip ULi devices
 */

static int tulip_uli_dm_quirk(struct pci_dev *pdev)
{
	if (pdev->vendor == 0x1282 && pdev->device == 0x9102)
		return 1;
	return 0;
}

static const struct net_device_ops tulip_netdev_ops = {
	.ndo_open		= tulip_open,
	.ndo_start_xmit		= tulip_start_xmit,
	.ndo_tx_timeout		= tulip_tx_timeout,
	.ndo_stop		= tulip_close,
	.ndo_get_stats		= tulip_get_stats,
	.ndo_do_ioctl 		= private_ioctl,
	.ndo_set_rx_mode	= set_rx_mode,
	.ndo_set_mac_address	= eth_mac_addr,
	.ndo_validate_addr	= eth_validate_addr,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller	 = poll_tulip,
#endif
};

const struct pci_device_id early_486_chipsets[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82424) },
	{ PCI_DEVICE(PCI_VENDOR_ID_SI, PCI_DEVICE_ID_SI_496) },
	{ },
};

static int tulip_init_one(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct tulip_private *tp;
	/* See note below on the multiport cards. */
	static unsigned char last_phys_addr[ETH_ALEN] = {
		0x00, 'L', 'i', 'n', 'u', 'x'
	};
	static int last_irq;
	int i, irq;
	unsigned short sum;
	unsigned char *ee_data;
	struct net_device *dev;
	void __iomem *ioaddr;
	static int board_idx = -1;
	int chip_idx = ent->driver_data;
	const char *chip_name = tulip_tbl[chip_idx].chip_name;
	unsigned int eeprom_missing = 0;
	unsigned int force_csr0 = 0;

#ifndef MODULE
	if (tulip_debug > 0)
		printk_once(KERN_INFO "%s", version);
#endif

	board_idx++;

	/*
	 *	Lan media wire a tulip chip to a wan interface. Needs a very
	 *	different driver (lmc driver)
	 */

        if (pdev->subsystem_vendor == PCI_VENDOR_ID_LMC) {
		pr_err("skipping LMC card\n");
		return -ENODEV;
	} else if (pdev->subsystem_vendor == PCI_VENDOR_ID_SBE &&
		   (pdev->subsystem_device == PCI_SUBDEVICE_ID_SBE_T3E3 ||
		    pdev->subsystem_device == PCI_SUBDEVICE_ID_SBE_2T3E3_P0 ||
		    pdev->subsystem_device == PCI_SUBDEVICE_ID_SBE_2T3E3_P1)) {
		pr_err("skipping SBE T3E3 port\n");
		return -ENODEV;
	}

	/*
	 *	DM910x chips should be handled by the dmfe driver, except
	 *	on-board chips on SPARC systems.  Also, early DM9100s need
	 *	software CRC which only the dmfe driver supports.
	 */

#ifdef CONFIG_TULIP_DM910X
	if (chip_idx == DM910X) {
		struct device_node *dp;

		if (pdev->vendor == 0x1282 && pdev->device == 0x9100 &&
		    pdev->revision < 0x30) {
			pr_info("skipping early DM9100 with Crc bug (use dmfe)\n");
			return -ENODEV;
		}

		dp = pci_device_to_OF_node(pdev);
		if (!(dp && of_get_property(dp, "local-mac-address", NULL))) {
			pr_info("skipping DM910x expansion card (use dmfe)\n");
			return -ENODEV;
		}
	}
#endif

	/*
	 *	Looks for early PCI chipsets where people report hangs
	 *	without the workarounds being on.
	 */

	/* 1. Intel Saturn. Switch to 8 long words burst, 8 long word cache
	      aligned.  Aries might need this too. The Saturn errata are not
	      pretty reading but thankfully it's an old 486 chipset.

	   2. The dreaded SiS496 486 chipset. Same workaround as Intel
	      Saturn.
	*/

	if (pci_dev_present(early_486_chipsets)) {
		csr0 = MRL | MRM | (8 << BurstLenShift) | (1 << CALShift);
		force_csr0 = 1;
	}

	/* bugfix: the ASIX must have a burst limit or horrible things happen. */
	if (chip_idx == AX88140) {
		if ((csr0 & 0x3f00) == 0)
			csr0 |= 0x2000;
	}

	/* PNIC doesn't have MWI/MRL/MRM... */
	if (chip_idx == LC82C168)
		csr0 &= ~0xfff10000; /* zero reserved bits 31:20, 16 */

	/* DM9102A has troubles with MRM & clear reserved bits 24:22, 20, 16, 7:1 */
	if (tulip_uli_dm_quirk(pdev)) {
		csr0 &= ~0x01f100ff;
#if defined(CONFIG_SPARC)
                csr0 = (csr0 & ~0xff00) | 0xe000;
#endif
	}
	/*
	 *	And back to business
	 */

	i = pci_enable_device(pdev);
	if (i) {
		pr_err("Cannot enable tulip board #%d, aborting\n", board_idx);
		return i;
	}

	irq = pdev->irq;

	/* alloc_etherdev ensures aligned and zeroed private structures */
	dev = alloc_etherdev (sizeof (*tp));
	if (!dev)
		return -ENOMEM;

	SET_NETDEV_DEV(dev, &pdev->dev);
	if (pci_resource_len (pdev, 0) < tulip_tbl[chip_idx].io_size) {
		pr_err("%s: I/O region (0x%llx@0x%llx) too small, aborting\n",
		       pci_name(pdev),
		       (unsigned long long)pci_resource_len (pdev, 0),
		       (unsigned long long)pci_resource_start (pdev, 0));
		goto err_out_free_netdev;
	}

	/* grab all resources from both PIO and MMIO regions, as we
	 * don't want anyone else messing around with our hardware */
	if (pci_request_regions (pdev, DRV_NAME))
		goto err_out_free_netdev;

	ioaddr =  pci_iomap(pdev, TULIP_BAR, tulip_tbl[chip_idx].io_size);

	if (!ioaddr)
		goto err_out_free_res;

	/*
	 * initialize private data structure 'tp'
	 * it is zeroed and aligned in alloc_etherdev
	 */
	tp = netdev_priv(dev);
	tp->dev = dev;

	tp->rx_ring = pci_alloc_consistent(pdev,
					   sizeof(struct tulip_rx_desc) * RX_RING_SIZE +
					   sizeof(struct tulip_tx_desc) * TX_RING_SIZE,
					   &tp->rx_ring_dma);
	if (!tp->rx_ring)
		goto err_out_mtable;
	tp->tx_ring = (struct tulip_tx_desc *)(tp->rx_ring + RX_RING_SIZE);
	tp->tx_ring_dma = tp->rx_ring_dma + sizeof(struct tulip_rx_desc) * RX_RING_SIZE;

	tp->chip_id = chip_idx;
	tp->flags = tulip_tbl[chip_idx].flags;

	tp->wolinfo.supported = 0;
	tp->wolinfo.wolopts = 0;
	/* COMET: Enable power management only for AN983B */
	if (chip_idx == COMET ) {
		u32 sig;
		pci_read_config_dword (pdev, 0x80, &sig);
		if (sig == 0x09811317) {
			tp->flags |= COMET_PM;
			tp->wolinfo.supported = WAKE_PHY | WAKE_MAGIC;
			pr_info("%s: Enabled WOL support for AN983B\n",
				__func__);
		}
	}
	tp->pdev = pdev;
	tp->base_addr = ioaddr;
	tp->revision = pdev->revision;
	tp->csr0 = csr0;
	spin_lock_init(&tp->lock);
	spin_lock_init(&tp->mii_lock);
	timer_setup(&tp->timer, tulip_tbl[tp->chip_id].media_timer, 0);

	INIT_WORK(&tp->media_work, tulip_tbl[tp->chip_id].media_task);

#ifdef CONFIG_TULIP_MWI
	if (!force_csr0 && (tp->flags & HAS_PCI_MWI))
		tulip_mwi_config (pdev, dev);
#endif

	/* Stop the chip's Tx and Rx processes. */
	tulip_stop_rxtx(tp);

	pci_set_master(pdev);

#ifdef CONFIG_GSC
	if (pdev->subsystem_vendor == PCI_VENDOR_ID_HP) {
		switch (pdev->subsystem_device) {
		default:
			break;
		case 0x1061:
		case 0x1062:
		case 0x1063:
		case 0x1098:
		case 0x1099:
		case 0x10EE:
			tp->flags |= HAS_SWAPPED_SEEPROM | NEEDS_FAKE_MEDIA_TABLE;
			chip_name = "GSC DS21140 Tulip";
		}
	}
#endif

	/* Clear the missed-packet counter. */
	ioread32(ioaddr + CSR8);

	/* The station address ROM is read byte serially.  The register must
	   be polled, waiting for the value to be read bit serially from the
	   EEPROM.
	   */
	ee_data = tp->eeprom;
	memset(ee_data, 0, sizeof(tp->eeprom));
	sum = 0;
	if (chip_idx == LC82C168) {
		for (i = 0; i < 3; i++) {
			int value, boguscnt = 100000;
			iowrite32(0x600 | i, ioaddr + 0x98);
			do {
				value = ioread32(ioaddr + CSR9);
			} while (value < 0  && --boguscnt > 0);
			put_unaligned_le16(value, ((__le16 *)dev->dev_addr) + i);
			sum += value & 0xffff;
		}
	} else if (chip_idx == COMET) {
		/* No need to read the EEPROM. */
		put_unaligned_le32(ioread32(ioaddr + 0xA4), dev->dev_addr);
		put_unaligned_le16(ioread32(ioaddr + 0xA8), dev->dev_addr + 4);
		for (i = 0; i < 6; i ++)
			sum += dev->dev_addr[i];
	} else {
		/* A serial EEPROM interface, we read now and sort it out later. */
		int sa_offset = 0;
		int ee_addr_size = tulip_read_eeprom(dev, 0xff, 8) & 0x40000 ? 8 : 6;
		int ee_max_addr = ((1 << ee_addr_size) - 1) * sizeof(u16);

		if (ee_max_addr > sizeof(tp->eeprom))
			ee_max_addr = sizeof(tp->eeprom);

		for (i = 0; i < ee_max_addr ; i += sizeof(u16)) {
			u16 data = tulip_read_eeprom(dev, i/2, ee_addr_size);
			ee_data[i] = data & 0xff;
			ee_data[i + 1] = data >> 8;
		}

		/* DEC now has a specification (see Notes) but early board makers
		   just put the address in the first EEPROM locations. */
		/* This does  memcmp(ee_data, ee_data+16, 8) */
		for (i = 0; i < 8; i ++)
			if (ee_data[i] != ee_data[16+i])
				sa_offset = 20;
		if (chip_idx == CONEXANT) {
			/* Check that the tuple type and length is correct. */
			if (ee_data[0x198] == 0x04  &&  ee_data[0x199] == 6)
				sa_offset = 0x19A;
		} else if (ee_data[0] == 0xff  &&  ee_data[1] == 0xff &&
				   ee_data[2] == 0) {
			sa_offset = 2;		/* Grrr, damn Matrox boards. */
		}
#ifdef CONFIG_MIPS_COBALT
               if ((pdev->bus->number == 0) &&
                   ((PCI_SLOT(pdev->devfn) == 7) ||
                    (PCI_SLOT(pdev->devfn) == 12))) {
                       /* Cobalt MAC address in first EEPROM locations. */
                       sa_offset = 0;
		       /* Ensure our media table fixup get's applied */
		       memcpy(ee_data + 16, ee_data, 8);
               }
#endif
#ifdef CONFIG_GSC
		/* Check to see if we have a broken srom */
		if (ee_data[0] == 0x61 && ee_data[1] == 0x10) {
			/* pci_vendor_id and subsystem_id are swapped */
			ee_data[0] = ee_data[2];
			ee_data[1] = ee_data[3];
			ee_data[2] = 0x61;
			ee_data[3] = 0x10;

			/* HSC-PCI boards need to be byte-swaped and shifted
			 * up 1 word.  This shift needs to happen at the end
			 * of the MAC first because of the 2 byte overlap.
			 */
			for (i = 4; i >= 0; i -= 2) {
				ee_data[17 + i + 3] = ee_data[17 + i];
				ee_data[16 + i + 5] = ee_data[16 + i];
			}
		}
#endif

		for (i = 0; i < 6; i ++) {
			dev->dev_addr[i] = ee_data[i + sa_offset];
			sum += ee_data[i + sa_offset];
		}
	}
	/* Lite-On boards have the address byte-swapped. */
	if ((dev->dev_addr[0] == 0xA0 ||
	     dev->dev_addr[0] == 0xC0 ||
	     dev->dev_addr[0] == 0x02) &&
	    dev->dev_addr[1] == 0x00)
		for (i = 0; i < 6; i+=2) {
			char tmp = dev->dev_addr[i];
			dev->dev_addr[i] = dev->dev_addr[i+1];
			dev->dev_addr[i+1] = tmp;
		}
	/* On the Zynx 315 Etherarray and other multiport boards only the
	   first Tulip has an EEPROM.
	   On Sparc systems the mac address is held in the OBP property
	   "local-mac-address".
	   The addresses of the subsequent ports are derived from the first.
	   Many PCI BIOSes also incorrectly report the IRQ line, so we correct
	   that here as well. */
	if (sum == 0  || sum == 6*0xff) {
#if defined(CONFIG_SPARC)
		struct device_node *dp = pci_device_to_OF_node(pdev);
		const unsigned char *addr;
		int len;
#endif
		eeprom_missing = 1;
		for (i = 0; i < 5; i++)
			dev->dev_addr[i] = last_phys_addr[i];
		dev->dev_addr[i] = last_phys_addr[i] + 1;
#if defined(CONFIG_SPARC)
		addr = of_get_property(dp, "local-mac-address", &len);
		if (addr && len == ETH_ALEN)
			memcpy(dev->dev_addr, addr, ETH_ALEN);
#endif
#if defined(__i386__) || defined(__x86_64__)	/* Patch up x86 BIOS bug. */
		if (last_irq)
			irq = last_irq;
#endif
	}

	for (i = 0; i < 6; i++)
		last_phys_addr[i] = dev->dev_addr[i];
	last_irq = irq;

	/* The lower four bits are the media type. */
	if (board_idx >= 0  &&  board_idx < MAX_UNITS) {
		if (options[board_idx] & MEDIA_MASK)
			tp->default_port = options[board_idx] & MEDIA_MASK;
		if ((options[board_idx] & FullDuplex) || full_duplex[board_idx] > 0)
			tp->full_duplex = 1;
		if (mtu[board_idx] > 0)
			dev->mtu = mtu[board_idx];
	}
	if (dev->mem_start & MEDIA_MASK)
		tp->default_port = dev->mem_start & MEDIA_MASK;
	if (tp->default_port) {
		pr_info(DRV_NAME "%d: Transceiver selection forced to %s\n",
			board_idx, medianame[tp->default_port & MEDIA_MASK]);
		tp->medialock = 1;
		if (tulip_media_cap[tp->default_port] & MediaAlwaysFD)
			tp->full_duplex = 1;
	}
	if (tp->full_duplex)
		tp->full_duplex_lock = 1;

	if (tulip_media_cap[tp->default_port] & MediaIsMII) {
		static const u16 media2advert[] = {
			0x20, 0x40, 0x03e0, 0x60, 0x80, 0x100, 0x200
		};
		tp->mii_advertise = media2advert[tp->default_port - 9];
		tp->mii_advertise |= (tp->flags & HAS_8023X); /* Matching bits! */
	}

	if (tp->flags & HAS_MEDIA_TABLE) {
		sprintf(dev->name, DRV_NAME "%d", board_idx);	/* hack */
		tulip_parse_eeprom(dev);
		strcpy(dev->name, "eth%d");			/* un-hack */
	}

	if ((tp->flags & ALWAYS_CHECK_MII) ||
		(tp->mtable  &&  tp->mtable->has_mii) ||
		( ! tp->mtable  &&  (tp->flags & HAS_MII))) {
		if (tp->mtable  &&  tp->mtable->has_mii) {
			for (i = 0; i < tp->mtable->leafcount; i++)
				if (tp->mtable->mleaf[i].media == 11) {
					tp->cur_index = i;
					tp->saved_if_port = dev->if_port;
					tulip_select_media(dev, 2);
					dev->if_port = tp->saved_if_port;
					break;
				}
		}

		/* Find the connected MII xcvrs.
		   Doing this in open() would allow detecting external xcvrs
		   later, but takes much time. */
		tulip_find_mii (dev, board_idx);
	}

	/* The Tulip-specific entries in the device structure. */
	dev->netdev_ops = &tulip_netdev_ops;
	dev->watchdog_timeo = TX_TIMEOUT;
#ifdef CONFIG_TULIP_NAPI
	netif_napi_add(dev, &tp->napi, tulip_poll, 16);
#endif
	dev->ethtool_ops = &ops;

	if (register_netdev(dev))
		goto err_out_free_ring;

	pci_set_drvdata(pdev, dev);

	dev_info(&dev->dev,
#ifdef CONFIG_TULIP_MMIO
		 "%s rev %d at MMIO %#llx,%s %pM, IRQ %d\n",
#else
		 "%s rev %d at Port %#llx,%s %pM, IRQ %d\n",
#endif
		 chip_name, pdev->revision,
		 (unsigned long long)pci_resource_start(pdev, TULIP_BAR),
		 eeprom_missing ? " EEPROM not present," : "",
		 dev->dev_addr, irq);

        if (tp->chip_id == PNIC2)
		tp->link_change = pnic2_lnk_change;
	else if (tp->flags & HAS_NWAY)
		tp->link_change = t21142_lnk_change;
	else if (tp->flags & HAS_PNICNWAY)
		tp->link_change = pnic_lnk_change;

	/* Reset the xcvr interface and turn on heartbeat. */
	switch (chip_idx) {
	case DC21140:
	case DM910X:
	default:
		if (tp->mtable)
			iowrite32(tp->mtable->csr12dir | 0x100, ioaddr + CSR12);
		break;
	case DC21142:
		if (tp->mii_cnt  ||  tulip_media_cap[dev->if_port] & MediaIsMII) {
			iowrite32(csr6_mask_defstate, ioaddr + CSR6);
			iowrite32(0x0000, ioaddr + CSR13);
			iowrite32(0x0000, ioaddr + CSR14);
			iowrite32(csr6_mask_hdcap, ioaddr + CSR6);
		} else
			t21142_start_nway(dev);
		break;
	case PNIC2:
	        /* just do a reset for sanity sake */
		iowrite32(0x0000, ioaddr + CSR13);
		iowrite32(0x0000, ioaddr + CSR14);
		break;
	case LC82C168:
		if ( ! tp->mii_cnt) {
			tp->nway = 1;
			tp->nwayset = 0;
			iowrite32(csr6_ttm | csr6_ca, ioaddr + CSR6);
			iowrite32(0x30, ioaddr + CSR12);
			iowrite32(0x0001F078, ioaddr + CSR6);
			iowrite32(0x0201F078, ioaddr + CSR6); /* Turn on autonegotiation. */
		}
		break;
	case MX98713:
	case COMPEX9881:
		iowrite32(0x00000000, ioaddr + CSR6);
		iowrite32(0x000711C0, ioaddr + CSR14); /* Turn on NWay. */
		iowrite32(0x00000001, ioaddr + CSR13);
		break;
	case MX98715:
	case MX98725:
		iowrite32(0x01a80000, ioaddr + CSR6);
		iowrite32(0xFFFFFFFF, ioaddr + CSR14);
		iowrite32(0x00001000, ioaddr + CSR12);
		break;
	case COMET:
		/* No initialization necessary. */
		break;
	}

	/* put the chip in snooze mode until opened */
	tulip_set_power_state (tp, 0, 1);

	return 0;

err_out_free_ring:
	pci_free_consistent (pdev,
			     sizeof (struct tulip_rx_desc) * RX_RING_SIZE +
			     sizeof (struct tulip_tx_desc) * TX_RING_SIZE,
			     tp->rx_ring, tp->rx_ring_dma);

err_out_mtable:
	kfree (tp->mtable);
	pci_iounmap(pdev, ioaddr);

err_out_free_res:
	pci_release_regions (pdev);

err_out_free_netdev:
	free_netdev (dev);
	return -ENODEV;
}


/* set the registers according to the given wolopts */
static void tulip_set_wolopts (struct pci_dev *pdev, u32 wolopts)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	struct tulip_private *tp = netdev_priv(dev);
	void __iomem *ioaddr = tp->base_addr;

	if (tp->flags & COMET_PM) {
	  
		unsigned int tmp;
			
		tmp = ioread32(ioaddr + CSR18);
		tmp &= ~(comet_csr18_pmes_sticky | comet_csr18_apm_mode | comet_csr18_d3a);
		tmp |= comet_csr18_pm_mode;
		iowrite32(tmp, ioaddr + CSR18);
			
		/* Set the Wake-up Control/Status Register to the given WOL options*/
		tmp = ioread32(ioaddr + CSR13);
		tmp &= ~(comet_csr13_linkoffe | comet_csr13_linkone | comet_csr13_wfre | comet_csr13_lsce | comet_csr13_mpre);
		if (wolopts & WAKE_MAGIC)
			tmp |= comet_csr13_mpre;
		if (wolopts & WAKE_PHY)
			tmp |= comet_csr13_linkoffe | comet_csr13_linkone | comet_csr13_lsce;
		/* Clear the event flags */
		tmp |= comet_csr13_wfr | comet_csr13_mpr | comet_csr13_lsc;
		iowrite32(tmp, ioaddr + CSR13);
	}
}

#ifdef CONFIG_PM


static int tulip_suspend (struct pci_dev *pdev, pm_message_t state)
{
	pci_power_t pstate;
	struct net_device *dev = pci_get_drvdata(pdev);
	struct tulip_private *tp = netdev_priv(dev);

	if (!dev)
		return -EINVAL;

	if (!netif_running(dev))
		goto save_state;

	tulip_down(dev);

	netif_device_detach(dev);
	/* FIXME: it needlessly adds an error path. */
	free_irq(tp->pdev->irq, dev);

save_state:
	pci_save_state(pdev);
	pci_disable_device(pdev);
	pstate = pci_choose_state(pdev, state);
	if (state.event == PM_EVENT_SUSPEND && pstate != PCI_D0) {
		int rc;

		tulip_set_wolopts(pdev, tp->wolinfo.wolopts);
		rc = pci_enable_wake(pdev, pstate, tp->wolinfo.wolopts);
		if (rc)
			pr_err("pci_enable_wake failed (%d)\n", rc);
	}
	pci_set_power_state(pdev, pstate);

	return 0;
}


static int tulip_resume(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	struct tulip_private *tp = netdev_priv(dev);
	void __iomem *ioaddr = tp->base_addr;
	int retval;
	unsigned int tmp;

	if (!dev)
		return -EINVAL;

	pci_set_power_state(pdev, PCI_D0);
	pci_restore_state(pdev);

	if (!netif_running(dev))
		return 0;

	if ((retval = pci_enable_device(pdev))) {
		pr_err("pci_enable_device failed in resume\n");
		return retval;
	}

	retval = request_irq(pdev->irq, tulip_interrupt, IRQF_SHARED,
			     dev->name, dev);
	if (retval) {
		pr_err("request_irq failed in resume\n");
		return retval;
	}

	if (tp->flags & COMET_PM) {
		pci_enable_wake(pdev, PCI_D3hot, 0);
		pci_enable_wake(pdev, PCI_D3cold, 0);

		/* Clear the PMES flag */
		tmp = ioread32(ioaddr + CSR20);
		tmp |= comet_csr20_pmes;
		iowrite32(tmp, ioaddr + CSR20);

		/* Disable all wake-up events */
		tulip_set_wolopts(pdev, 0);
	}
	netif_device_attach(dev);

	if (netif_running(dev))
		tulip_up(dev);

	return 0;
}

#endif /* CONFIG_PM */


static void tulip_remove_one(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata (pdev);
	struct tulip_private *tp;

	if (!dev)
		return;

	tp = netdev_priv(dev);
	unregister_netdev(dev);
	pci_free_consistent (pdev,
			     sizeof (struct tulip_rx_desc) * RX_RING_SIZE +
			     sizeof (struct tulip_tx_desc) * TX_RING_SIZE,
			     tp->rx_ring, tp->rx_ring_dma);
	kfree (tp->mtable);
	pci_iounmap(pdev, tp->base_addr);
	free_netdev (dev);
	pci_release_regions (pdev);
	pci_disable_device(pdev);

	/* pci_power_off (pdev, -1); */
}

#ifdef CONFIG_NET_POLL_CONTROLLER
/*
 * Polling 'interrupt' - used by things like netconsole to send skbs
 * without having to re-enable interrupts. It's not called while
 * the interrupt routine is executing.
 */

static void poll_tulip (struct net_device *dev)
{
	struct tulip_private *tp = netdev_priv(dev);
	const int irq = tp->pdev->irq;

	/* disable_irq here is not very nice, but with the lockless
	   interrupt handler we have no other choice. */
	disable_irq(irq);
	tulip_interrupt (irq, dev);
	enable_irq(irq);
}
#endif

static struct pci_driver tulip_driver = {
	.name		= DRV_NAME,
	.id_table	= tulip_pci_tbl,
	.probe		= tulip_init_one,
	.remove		= tulip_remove_one,
#ifdef CONFIG_PM
	.suspend	= tulip_suspend,
	.resume		= tulip_resume,
#endif /* CONFIG_PM */
};


static int __init tulip_init (void)
{
#ifdef MODULE
	pr_info("%s", version);
#endif

	if (!csr0) {
		pr_warn("tulip: unknown CPU architecture, using default csr0\n");
		/* default to 8 longword cache line alignment */
		csr0 = 0x00A00000 | 0x4800;
	}

	/* copy module parms into globals */
	tulip_rx_copybreak = rx_copybreak;
	tulip_max_interrupt_work = max_interrupt_work;

	/* probe for and init boards */
	return pci_register_driver(&tulip_driver);
}


static void __exit tulip_cleanup (void)
{
	pci_unregister_driver (&tulip_driver);
}


module_init(tulip_init);
module_exit(tulip_cleanup);
