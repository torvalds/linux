/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Driver for SGI's IOC3 based Ethernet cards as found in the PCI card.
 *
 * Copyright (C) 1999, 2000, 01, 03, 06 Ralf Baechle
 * Copyright (C) 1995, 1999, 2000, 2001 by Silicon Graphics, Inc.
 *
 * References:
 *  o IOC3 ASIC specification 4.51, 1996-04-18
 *  o IEEE 802.3 specification, 2000 edition
 *  o DP38840A Specification, National Semiconductor, March 1997
 *
 * To do:
 *
 *  o Handle allocation failures in ioc3_alloc_skb() more gracefully.
 *  o Handle allocation failures in ioc3_init_rings().
 *  o Use prefetching for large packets.  What is a good lower limit for
 *    prefetching?
 *  o We're probably allocating a bit too much memory.
 *  o Use hardware checksums.
 *  o Convert to using a IOC3 meta driver.
 *  o Which PHYs might possibly be attached to the IOC3 in real live,
 *    which workarounds are required for them?  Do we ever have Lucent's?
 *  o For the 2.5 branch kill the mii-tool ioctls.
 */

#define IOC3_NAME	"ioc3-eth"
#define IOC3_VERSION	"2.6.3-4"

#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/crc32.h>
#include <linux/mii.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/dma-mapping.h>
#include <linux/gfp.h>

#ifdef CONFIG_SERIAL_8250
#include <linux/serial_core.h>
#include <linux/serial_8250.h>
#include <linux/serial_reg.h>
#endif

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/skbuff.h>
#include <net/ip.h>

#include <asm/byteorder.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/uaccess.h>
#include <asm/sn/types.h>
#include <asm/sn/ioc3.h>
#include <asm/pci/bridge.h>

/*
 * 64 RX buffers.  This is tunable in the range of 16 <= x < 512.  The
 * value must be a power of two.
 */
#define RX_BUFFS 64

#define ETCSR_FD	((17<<ETCSR_IPGR2_SHIFT) | (11<<ETCSR_IPGR1_SHIFT) | 21)
#define ETCSR_HD	((21<<ETCSR_IPGR2_SHIFT) | (21<<ETCSR_IPGR1_SHIFT) | 21)

/* Private per NIC data of the driver.  */
struct ioc3_private {
	struct ioc3 *regs;
	unsigned long *rxr;		/* pointer to receiver ring */
	struct ioc3_etxd *txr;
	struct sk_buff *rx_skbs[512];
	struct sk_buff *tx_skbs[128];
	int rx_ci;			/* RX consumer index */
	int rx_pi;			/* RX producer index */
	int tx_ci;			/* TX consumer index */
	int tx_pi;			/* TX producer index */
	int txqlen;
	u32 emcr, ehar_h, ehar_l;
	spinlock_t ioc3_lock;
	struct mii_if_info mii;

	struct pci_dev *pdev;

	/* Members used by autonegotiation  */
	struct timer_list ioc3_timer;
};

static inline struct net_device *priv_netdev(struct ioc3_private *dev)
{
	return (void *)dev - ((sizeof(struct net_device) + 31) & ~31);
}

static int ioc3_ioctl(struct net_device *dev, struct ifreq *rq, int cmd);
static void ioc3_set_multicast_list(struct net_device *dev);
static int ioc3_start_xmit(struct sk_buff *skb, struct net_device *dev);
static void ioc3_timeout(struct net_device *dev);
static inline unsigned int ioc3_hash(const unsigned char *addr);
static inline void ioc3_stop(struct ioc3_private *ip);
static void ioc3_init(struct net_device *dev);

static const char ioc3_str[] = "IOC3 Ethernet";
static const struct ethtool_ops ioc3_ethtool_ops;

/* We use this to acquire receive skb's that we can DMA directly into. */

#define IOC3_CACHELINE	128UL

static inline unsigned long aligned_rx_skb_addr(unsigned long addr)
{
	return (~addr + 1) & (IOC3_CACHELINE - 1UL);
}

static inline struct sk_buff * ioc3_alloc_skb(unsigned long length,
	unsigned int gfp_mask)
{
	struct sk_buff *skb;

	skb = alloc_skb(length + IOC3_CACHELINE - 1, gfp_mask);
	if (likely(skb)) {
		int offset = aligned_rx_skb_addr((unsigned long) skb->data);
		if (offset)
			skb_reserve(skb, offset);
	}

	return skb;
}

static inline unsigned long ioc3_map(void *ptr, unsigned long vdev)
{
#ifdef CONFIG_SGI_IP27
	vdev <<= 57;   /* Shift to PCI64_ATTR_VIRTUAL */

	return vdev | (0xaUL << PCI64_ATTR_TARG_SHFT) | PCI64_ATTR_PREF |
	       ((unsigned long)ptr & TO_PHYS_MASK);
#else
	return virt_to_bus(ptr);
#endif
}

/* BEWARE: The IOC3 documentation documents the size of rx buffers as
   1644 while it's actually 1664.  This one was nasty to track down ...  */
#define RX_OFFSET		10
#define RX_BUF_ALLOC_SIZE	(1664 + RX_OFFSET + IOC3_CACHELINE)

/* DMA barrier to separate cached and uncached accesses.  */
#define BARRIER()							\
	__asm__("sync" ::: "memory")


#define IOC3_SIZE 0x100000

/*
 * IOC3 is a big endian device
 *
 * Unorthodox but makes the users of these macros more readable - the pointer
 * to the IOC3's memory mapped registers is expected as struct ioc3 * ioc3
 * in the environment.
 */
#define ioc3_r_mcr()		be32_to_cpu(ioc3->mcr)
#define ioc3_w_mcr(v)		do { ioc3->mcr = cpu_to_be32(v); } while (0)
#define ioc3_w_gpcr_s(v)	do { ioc3->gpcr_s = cpu_to_be32(v); } while (0)
#define ioc3_r_emcr()		be32_to_cpu(ioc3->emcr)
#define ioc3_w_emcr(v)		do { ioc3->emcr = cpu_to_be32(v); } while (0)
#define ioc3_r_eisr()		be32_to_cpu(ioc3->eisr)
#define ioc3_w_eisr(v)		do { ioc3->eisr = cpu_to_be32(v); } while (0)
#define ioc3_r_eier()		be32_to_cpu(ioc3->eier)
#define ioc3_w_eier(v)		do { ioc3->eier = cpu_to_be32(v); } while (0)
#define ioc3_r_ercsr()		be32_to_cpu(ioc3->ercsr)
#define ioc3_w_ercsr(v)		do { ioc3->ercsr = cpu_to_be32(v); } while (0)
#define ioc3_r_erbr_h()		be32_to_cpu(ioc3->erbr_h)
#define ioc3_w_erbr_h(v)	do { ioc3->erbr_h = cpu_to_be32(v); } while (0)
#define ioc3_r_erbr_l()		be32_to_cpu(ioc3->erbr_l)
#define ioc3_w_erbr_l(v)	do { ioc3->erbr_l = cpu_to_be32(v); } while (0)
#define ioc3_r_erbar()		be32_to_cpu(ioc3->erbar)
#define ioc3_w_erbar(v)		do { ioc3->erbar = cpu_to_be32(v); } while (0)
#define ioc3_r_ercir()		be32_to_cpu(ioc3->ercir)
#define ioc3_w_ercir(v)		do { ioc3->ercir = cpu_to_be32(v); } while (0)
#define ioc3_r_erpir()		be32_to_cpu(ioc3->erpir)
#define ioc3_w_erpir(v)		do { ioc3->erpir = cpu_to_be32(v); } while (0)
#define ioc3_r_ertr()		be32_to_cpu(ioc3->ertr)
#define ioc3_w_ertr(v)		do { ioc3->ertr = cpu_to_be32(v); } while (0)
#define ioc3_r_etcsr()		be32_to_cpu(ioc3->etcsr)
#define ioc3_w_etcsr(v)		do { ioc3->etcsr = cpu_to_be32(v); } while (0)
#define ioc3_r_ersr()		be32_to_cpu(ioc3->ersr)
#define ioc3_w_ersr(v)		do { ioc3->ersr = cpu_to_be32(v); } while (0)
#define ioc3_r_etcdc()		be32_to_cpu(ioc3->etcdc)
#define ioc3_w_etcdc(v)		do { ioc3->etcdc = cpu_to_be32(v); } while (0)
#define ioc3_r_ebir()		be32_to_cpu(ioc3->ebir)
#define ioc3_w_ebir(v)		do { ioc3->ebir = cpu_to_be32(v); } while (0)
#define ioc3_r_etbr_h()		be32_to_cpu(ioc3->etbr_h)
#define ioc3_w_etbr_h(v)	do { ioc3->etbr_h = cpu_to_be32(v); } while (0)
#define ioc3_r_etbr_l()		be32_to_cpu(ioc3->etbr_l)
#define ioc3_w_etbr_l(v)	do { ioc3->etbr_l = cpu_to_be32(v); } while (0)
#define ioc3_r_etcir()		be32_to_cpu(ioc3->etcir)
#define ioc3_w_etcir(v)		do { ioc3->etcir = cpu_to_be32(v); } while (0)
#define ioc3_r_etpir()		be32_to_cpu(ioc3->etpir)
#define ioc3_w_etpir(v)		do { ioc3->etpir = cpu_to_be32(v); } while (0)
#define ioc3_r_emar_h()		be32_to_cpu(ioc3->emar_h)
#define ioc3_w_emar_h(v)	do { ioc3->emar_h = cpu_to_be32(v); } while (0)
#define ioc3_r_emar_l()		be32_to_cpu(ioc3->emar_l)
#define ioc3_w_emar_l(v)	do { ioc3->emar_l = cpu_to_be32(v); } while (0)
#define ioc3_r_ehar_h()		be32_to_cpu(ioc3->ehar_h)
#define ioc3_w_ehar_h(v)	do { ioc3->ehar_h = cpu_to_be32(v); } while (0)
#define ioc3_r_ehar_l()		be32_to_cpu(ioc3->ehar_l)
#define ioc3_w_ehar_l(v)	do { ioc3->ehar_l = cpu_to_be32(v); } while (0)
#define ioc3_r_micr()		be32_to_cpu(ioc3->micr)
#define ioc3_w_micr(v)		do { ioc3->micr = cpu_to_be32(v); } while (0)
#define ioc3_r_midr_r()		be32_to_cpu(ioc3->midr_r)
#define ioc3_w_midr_r(v)	do { ioc3->midr_r = cpu_to_be32(v); } while (0)
#define ioc3_r_midr_w()		be32_to_cpu(ioc3->midr_w)
#define ioc3_w_midr_w(v)	do { ioc3->midr_w = cpu_to_be32(v); } while (0)

static inline u32 mcr_pack(u32 pulse, u32 sample)
{
	return (pulse << 10) | (sample << 2);
}

static int nic_wait(struct ioc3 *ioc3)
{
	u32 mcr;

        do {
                mcr = ioc3_r_mcr();
        } while (!(mcr & 2));

        return mcr & 1;
}

static int nic_reset(struct ioc3 *ioc3)
{
        int presence;

	ioc3_w_mcr(mcr_pack(500, 65));
	presence = nic_wait(ioc3);

	ioc3_w_mcr(mcr_pack(0, 500));
	nic_wait(ioc3);

        return presence;
}

static inline int nic_read_bit(struct ioc3 *ioc3)
{
	int result;

	ioc3_w_mcr(mcr_pack(6, 13));
	result = nic_wait(ioc3);
	ioc3_w_mcr(mcr_pack(0, 100));
	nic_wait(ioc3);

	return result;
}

static inline void nic_write_bit(struct ioc3 *ioc3, int bit)
{
	if (bit)
		ioc3_w_mcr(mcr_pack(6, 110));
	else
		ioc3_w_mcr(mcr_pack(80, 30));

	nic_wait(ioc3);
}

/*
 * Read a byte from an iButton device
 */
static u32 nic_read_byte(struct ioc3 *ioc3)
{
	u32 result = 0;
	int i;

	for (i = 0; i < 8; i++)
		result = (result >> 1) | (nic_read_bit(ioc3) << 7);

	return result;
}

/*
 * Write a byte to an iButton device
 */
static void nic_write_byte(struct ioc3 *ioc3, int byte)
{
	int i, bit;

	for (i = 8; i; i--) {
		bit = byte & 1;
		byte >>= 1;

		nic_write_bit(ioc3, bit);
	}
}

static u64 nic_find(struct ioc3 *ioc3, int *last)
{
	int a, b, index, disc;
	u64 address = 0;

	nic_reset(ioc3);
	/* Search ROM.  */
	nic_write_byte(ioc3, 0xf0);

	/* Algorithm from ``Book of iButton Standards''.  */
	for (index = 0, disc = 0; index < 64; index++) {
		a = nic_read_bit(ioc3);
		b = nic_read_bit(ioc3);

		if (a && b) {
			printk("NIC search failed (not fatal).\n");
			*last = 0;
			return 0;
		}

		if (!a && !b) {
			if (index == *last) {
				address |= 1UL << index;
			} else if (index > *last) {
				address &= ~(1UL << index);
				disc = index;
			} else if ((address & (1UL << index)) == 0)
				disc = index;
			nic_write_bit(ioc3, address & (1UL << index));
			continue;
		} else {
			if (a)
				address |= 1UL << index;
			else
				address &= ~(1UL << index);
			nic_write_bit(ioc3, a);
			continue;
		}
	}

	*last = disc;

	return address;
}

static int nic_init(struct ioc3 *ioc3)
{
	const char *unknown = "unknown";
	const char *type = unknown;
	u8 crc;
	u8 serial[6];
	int save = 0, i;

	while (1) {
		u64 reg;
		reg = nic_find(ioc3, &save);

		switch (reg & 0xff) {
		case 0x91:
			type = "DS1981U";
			break;
		default:
			if (save == 0) {
				/* Let the caller try again.  */
				return -1;
			}
			continue;
		}

		nic_reset(ioc3);

		/* Match ROM.  */
		nic_write_byte(ioc3, 0x55);
		for (i = 0; i < 8; i++)
			nic_write_byte(ioc3, (reg >> (i << 3)) & 0xff);

		reg >>= 8; /* Shift out type.  */
		for (i = 0; i < 6; i++) {
			serial[i] = reg & 0xff;
			reg >>= 8;
		}
		crc = reg & 0xff;
		break;
	}

	printk("Found %s NIC", type);
	if (type != unknown)
		printk (" registration number %pM, CRC %02x", serial, crc);
	printk(".\n");

	return 0;
}

/*
 * Read the NIC (Number-In-a-Can) device used to store the MAC address on
 * SN0 / SN00 nodeboards and PCI cards.
 */
static void ioc3_get_eaddr_nic(struct ioc3_private *ip)
{
	struct ioc3 *ioc3 = ip->regs;
	u8 nic[14];
	int tries = 2; /* There may be some problem with the battery?  */
	int i;

	ioc3_w_gpcr_s(1 << 21);

	while (tries--) {
		if (!nic_init(ioc3))
			break;
		udelay(500);
	}

	if (tries < 0) {
		printk("Failed to read MAC address\n");
		return;
	}

	/* Read Memory.  */
	nic_write_byte(ioc3, 0xf0);
	nic_write_byte(ioc3, 0x00);
	nic_write_byte(ioc3, 0x00);

	for (i = 13; i >= 0; i--)
		nic[i] = nic_read_byte(ioc3);

	for (i = 2; i < 8; i++)
		priv_netdev(ip)->dev_addr[i - 2] = nic[i];
}

/*
 * Ok, this is hosed by design.  It's necessary to know what machine the
 * NIC is in in order to know how to read the NIC address.  We also have
 * to know if it's a PCI card or a NIC in on the node board ...
 */
static void ioc3_get_eaddr(struct ioc3_private *ip)
{
	ioc3_get_eaddr_nic(ip);

	printk("Ethernet address is %pM.\n", priv_netdev(ip)->dev_addr);
}

static void __ioc3_set_mac_address(struct net_device *dev)
{
	struct ioc3_private *ip = netdev_priv(dev);
	struct ioc3 *ioc3 = ip->regs;

	ioc3_w_emar_h((dev->dev_addr[5] <<  8) | dev->dev_addr[4]);
	ioc3_w_emar_l((dev->dev_addr[3] << 24) | (dev->dev_addr[2] << 16) |
	              (dev->dev_addr[1] <<  8) | dev->dev_addr[0]);
}

static int ioc3_set_mac_address(struct net_device *dev, void *addr)
{
	struct ioc3_private *ip = netdev_priv(dev);
	struct sockaddr *sa = addr;

	memcpy(dev->dev_addr, sa->sa_data, dev->addr_len);

	spin_lock_irq(&ip->ioc3_lock);
	__ioc3_set_mac_address(dev);
	spin_unlock_irq(&ip->ioc3_lock);

	return 0;
}

/*
 * Caller must hold the ioc3_lock ever for MII readers.  This is also
 * used to protect the transmitter side but it's low contention.
 */
static int ioc3_mdio_read(struct net_device *dev, int phy, int reg)
{
	struct ioc3_private *ip = netdev_priv(dev);
	struct ioc3 *ioc3 = ip->regs;

	while (ioc3_r_micr() & MICR_BUSY);
	ioc3_w_micr((phy << MICR_PHYADDR_SHIFT) | reg | MICR_READTRIG);
	while (ioc3_r_micr() & MICR_BUSY);

	return ioc3_r_midr_r() & MIDR_DATA_MASK;
}

static void ioc3_mdio_write(struct net_device *dev, int phy, int reg, int data)
{
	struct ioc3_private *ip = netdev_priv(dev);
	struct ioc3 *ioc3 = ip->regs;

	while (ioc3_r_micr() & MICR_BUSY);
	ioc3_w_midr_w(data);
	ioc3_w_micr((phy << MICR_PHYADDR_SHIFT) | reg);
	while (ioc3_r_micr() & MICR_BUSY);
}

static int ioc3_mii_init(struct ioc3_private *ip);

static struct net_device_stats *ioc3_get_stats(struct net_device *dev)
{
	struct ioc3_private *ip = netdev_priv(dev);
	struct ioc3 *ioc3 = ip->regs;

	dev->stats.collisions += (ioc3_r_etcdc() & ETCDC_COLLCNT_MASK);
	return &dev->stats;
}

static void ioc3_tcpudp_checksum(struct sk_buff *skb, uint32_t hwsum, int len)
{
	struct ethhdr *eh = eth_hdr(skb);
	uint32_t csum, ehsum;
	unsigned int proto;
	struct iphdr *ih;
	uint16_t *ew;
	unsigned char *cp;

	/*
	 * Did hardware handle the checksum at all?  The cases we can handle
	 * are:
	 *
	 * - TCP and UDP checksums of IPv4 only.
	 * - IPv6 would be doable but we keep that for later ...
	 * - Only unfragmented packets.  Did somebody already tell you
	 *   fragmentation is evil?
	 * - don't care about packet size.  Worst case when processing a
	 *   malformed packet we'll try to access the packet at ip header +
	 *   64 bytes which is still inside the skb.  Even in the unlikely
	 *   case where the checksum is right the higher layers will still
	 *   drop the packet as appropriate.
	 */
	if (eh->h_proto != htons(ETH_P_IP))
		return;

	ih = (struct iphdr *) ((char *)eh + ETH_HLEN);
	if (ip_is_fragment(ih))
		return;

	proto = ih->protocol;
	if (proto != IPPROTO_TCP && proto != IPPROTO_UDP)
		return;

	/* Same as tx - compute csum of pseudo header  */
	csum = hwsum +
	       (ih->tot_len - (ih->ihl << 2)) +
	       htons((uint16_t)ih->protocol) +
	       (ih->saddr >> 16) + (ih->saddr & 0xffff) +
	       (ih->daddr >> 16) + (ih->daddr & 0xffff);

	/* Sum up ethernet dest addr, src addr and protocol  */
	ew = (uint16_t *) eh;
	ehsum = ew[0] + ew[1] + ew[2] + ew[3] + ew[4] + ew[5] + ew[6];

	ehsum = (ehsum & 0xffff) + (ehsum >> 16);
	ehsum = (ehsum & 0xffff) + (ehsum >> 16);

	csum += 0xffff ^ ehsum;

	/* In the next step we also subtract the 1's complement
	   checksum of the trailing ethernet CRC.  */
	cp = (char *)eh + len;	/* points at trailing CRC */
	if (len & 1) {
		csum += 0xffff ^ (uint16_t) ((cp[1] << 8) | cp[0]);
		csum += 0xffff ^ (uint16_t) ((cp[3] << 8) | cp[2]);
	} else {
		csum += 0xffff ^ (uint16_t) ((cp[0] << 8) | cp[1]);
		csum += 0xffff ^ (uint16_t) ((cp[2] << 8) | cp[3]);
	}

	csum = (csum & 0xffff) + (csum >> 16);
	csum = (csum & 0xffff) + (csum >> 16);

	if (csum == 0xffff)
		skb->ip_summed = CHECKSUM_UNNECESSARY;
}

static inline void ioc3_rx(struct net_device *dev)
{
	struct ioc3_private *ip = netdev_priv(dev);
	struct sk_buff *skb, *new_skb;
	struct ioc3 *ioc3 = ip->regs;
	int rx_entry, n_entry, len;
	struct ioc3_erxbuf *rxb;
	unsigned long *rxr;
	u32 w0, err;

	rxr = ip->rxr;		/* Ring base */
	rx_entry = ip->rx_ci;				/* RX consume index */
	n_entry = ip->rx_pi;

	skb = ip->rx_skbs[rx_entry];
	rxb = (struct ioc3_erxbuf *) (skb->data - RX_OFFSET);
	w0 = be32_to_cpu(rxb->w0);

	while (w0 & ERXBUF_V) {
		err = be32_to_cpu(rxb->err);		/* It's valid ...  */
		if (err & ERXBUF_GOODPKT) {
			len = ((w0 >> ERXBUF_BYTECNT_SHIFT) & 0x7ff) - 4;
			skb_trim(skb, len);
			skb->protocol = eth_type_trans(skb, dev);

			new_skb = ioc3_alloc_skb(RX_BUF_ALLOC_SIZE, GFP_ATOMIC);
			if (!new_skb) {
				/* Ouch, drop packet and just recycle packet
				   to keep the ring filled.  */
				dev->stats.rx_dropped++;
				new_skb = skb;
				goto next;
			}

			if (likely(dev->features & NETIF_F_RXCSUM))
				ioc3_tcpudp_checksum(skb,
					w0 & ERXBUF_IPCKSUM_MASK, len);

			netif_rx(skb);

			ip->rx_skbs[rx_entry] = NULL;	/* Poison  */

			/* Because we reserve afterwards. */
			skb_put(new_skb, (1664 + RX_OFFSET));
			rxb = (struct ioc3_erxbuf *) new_skb->data;
			skb_reserve(new_skb, RX_OFFSET);

			dev->stats.rx_packets++;		/* Statistics */
			dev->stats.rx_bytes += len;
		} else {
			/* The frame is invalid and the skb never
			   reached the network layer so we can just
			   recycle it.  */
			new_skb = skb;
			dev->stats.rx_errors++;
		}
		if (err & ERXBUF_CRCERR)	/* Statistics */
			dev->stats.rx_crc_errors++;
		if (err & ERXBUF_FRAMERR)
			dev->stats.rx_frame_errors++;
next:
		ip->rx_skbs[n_entry] = new_skb;
		rxr[n_entry] = cpu_to_be64(ioc3_map(rxb, 1));
		rxb->w0 = 0;				/* Clear valid flag */
		n_entry = (n_entry + 1) & 511;		/* Update erpir */

		/* Now go on to the next ring entry.  */
		rx_entry = (rx_entry + 1) & 511;
		skb = ip->rx_skbs[rx_entry];
		rxb = (struct ioc3_erxbuf *) (skb->data - RX_OFFSET);
		w0 = be32_to_cpu(rxb->w0);
	}
	ioc3_w_erpir((n_entry << 3) | ERPIR_ARM);
	ip->rx_pi = n_entry;
	ip->rx_ci = rx_entry;
}

static inline void ioc3_tx(struct net_device *dev)
{
	struct ioc3_private *ip = netdev_priv(dev);
	unsigned long packets, bytes;
	struct ioc3 *ioc3 = ip->regs;
	int tx_entry, o_entry;
	struct sk_buff *skb;
	u32 etcir;

	spin_lock(&ip->ioc3_lock);
	etcir = ioc3_r_etcir();

	tx_entry = (etcir >> 7) & 127;
	o_entry = ip->tx_ci;
	packets = 0;
	bytes = 0;

	while (o_entry != tx_entry) {
		packets++;
		skb = ip->tx_skbs[o_entry];
		bytes += skb->len;
		dev_kfree_skb_irq(skb);
		ip->tx_skbs[o_entry] = NULL;

		o_entry = (o_entry + 1) & 127;		/* Next */

		etcir = ioc3_r_etcir();			/* More pkts sent?  */
		tx_entry = (etcir >> 7) & 127;
	}

	dev->stats.tx_packets += packets;
	dev->stats.tx_bytes += bytes;
	ip->txqlen -= packets;

	if (ip->txqlen < 128)
		netif_wake_queue(dev);

	ip->tx_ci = o_entry;
	spin_unlock(&ip->ioc3_lock);
}

/*
 * Deal with fatal IOC3 errors.  This condition might be caused by a hard or
 * software problems, so we should try to recover
 * more gracefully if this ever happens.  In theory we might be flooded
 * with such error interrupts if something really goes wrong, so we might
 * also consider to take the interface down.
 */
static void ioc3_error(struct net_device *dev, u32 eisr)
{
	struct ioc3_private *ip = netdev_priv(dev);
	unsigned char *iface = dev->name;

	spin_lock(&ip->ioc3_lock);

	if (eisr & EISR_RXOFLO)
		printk(KERN_ERR "%s: RX overflow.\n", iface);
	if (eisr & EISR_RXBUFOFLO)
		printk(KERN_ERR "%s: RX buffer overflow.\n", iface);
	if (eisr & EISR_RXMEMERR)
		printk(KERN_ERR "%s: RX PCI error.\n", iface);
	if (eisr & EISR_RXPARERR)
		printk(KERN_ERR "%s: RX SSRAM parity error.\n", iface);
	if (eisr & EISR_TXBUFUFLO)
		printk(KERN_ERR "%s: TX buffer underflow.\n", iface);
	if (eisr & EISR_TXMEMERR)
		printk(KERN_ERR "%s: TX PCI error.\n", iface);

	ioc3_stop(ip);
	ioc3_init(dev);
	ioc3_mii_init(ip);

	netif_wake_queue(dev);

	spin_unlock(&ip->ioc3_lock);
}

/* The interrupt handler does all of the Rx thread work and cleans up
   after the Tx thread.  */
static irqreturn_t ioc3_interrupt(int irq, void *_dev)
{
	struct net_device *dev = (struct net_device *)_dev;
	struct ioc3_private *ip = netdev_priv(dev);
	struct ioc3 *ioc3 = ip->regs;
	const u32 enabled = EISR_RXTIMERINT | EISR_RXOFLO | EISR_RXBUFOFLO |
	                    EISR_RXMEMERR | EISR_RXPARERR | EISR_TXBUFUFLO |
	                    EISR_TXEXPLICIT | EISR_TXMEMERR;
	u32 eisr;

	eisr = ioc3_r_eisr() & enabled;

	ioc3_w_eisr(eisr);
	(void) ioc3_r_eisr();				/* Flush */

	if (eisr & (EISR_RXOFLO | EISR_RXBUFOFLO | EISR_RXMEMERR |
	            EISR_RXPARERR | EISR_TXBUFUFLO | EISR_TXMEMERR))
		ioc3_error(dev, eisr);
	if (eisr & EISR_RXTIMERINT)
		ioc3_rx(dev);
	if (eisr & EISR_TXEXPLICIT)
		ioc3_tx(dev);

	return IRQ_HANDLED;
}

static inline void ioc3_setup_duplex(struct ioc3_private *ip)
{
	struct ioc3 *ioc3 = ip->regs;

	if (ip->mii.full_duplex) {
		ioc3_w_etcsr(ETCSR_FD);
		ip->emcr |= EMCR_DUPLEX;
	} else {
		ioc3_w_etcsr(ETCSR_HD);
		ip->emcr &= ~EMCR_DUPLEX;
	}
	ioc3_w_emcr(ip->emcr);
}

static void ioc3_timer(unsigned long data)
{
	struct ioc3_private *ip = (struct ioc3_private *) data;

	/* Print the link status if it has changed */
	mii_check_media(&ip->mii, 1, 0);
	ioc3_setup_duplex(ip);

	ip->ioc3_timer.expires = jiffies + ((12 * HZ)/10); /* 1.2s */
	add_timer(&ip->ioc3_timer);
}

/*
 * Try to find a PHY.  There is no apparent relation between the MII addresses
 * in the SGI documentation and what we find in reality, so we simply probe
 * for the PHY.  It seems IOC3 PHYs usually live on address 31.  One of my
 * onboard IOC3s has the special oddity that probing doesn't seem to find it
 * yet the interface seems to work fine, so if probing fails we for now will
 * simply default to PHY 31 instead of bailing out.
 */
static int ioc3_mii_init(struct ioc3_private *ip)
{
	struct net_device *dev = priv_netdev(ip);
	int i, found = 0, res = 0;
	int ioc3_phy_workaround = 1;
	u16 word;

	for (i = 0; i < 32; i++) {
		word = ioc3_mdio_read(dev, i, MII_PHYSID1);

		if (word != 0xffff && word != 0x0000) {
			found = 1;
			break;			/* Found a PHY		*/
		}
	}

	if (!found) {
		if (ioc3_phy_workaround)
			i = 31;
		else {
			ip->mii.phy_id = -1;
			res = -ENODEV;
			goto out;
		}
	}

	ip->mii.phy_id = i;

out:
	return res;
}

static void ioc3_mii_start(struct ioc3_private *ip)
{
	ip->ioc3_timer.expires = jiffies + (12 * HZ)/10;  /* 1.2 sec. */
	ip->ioc3_timer.data = (unsigned long) ip;
	ip->ioc3_timer.function = ioc3_timer;
	add_timer(&ip->ioc3_timer);
}

static inline void ioc3_clean_rx_ring(struct ioc3_private *ip)
{
	struct sk_buff *skb;
	int i;

	for (i = ip->rx_ci; i & 15; i++) {
		ip->rx_skbs[ip->rx_pi] = ip->rx_skbs[ip->rx_ci];
		ip->rxr[ip->rx_pi++] = ip->rxr[ip->rx_ci++];
	}
	ip->rx_pi &= 511;
	ip->rx_ci &= 511;

	for (i = ip->rx_ci; i != ip->rx_pi; i = (i+1) & 511) {
		struct ioc3_erxbuf *rxb;
		skb = ip->rx_skbs[i];
		rxb = (struct ioc3_erxbuf *) (skb->data - RX_OFFSET);
		rxb->w0 = 0;
	}
}

static inline void ioc3_clean_tx_ring(struct ioc3_private *ip)
{
	struct sk_buff *skb;
	int i;

	for (i=0; i < 128; i++) {
		skb = ip->tx_skbs[i];
		if (skb) {
			ip->tx_skbs[i] = NULL;
			dev_kfree_skb_any(skb);
		}
		ip->txr[i].cmd = 0;
	}
	ip->tx_pi = 0;
	ip->tx_ci = 0;
}

static void ioc3_free_rings(struct ioc3_private *ip)
{
	struct sk_buff *skb;
	int rx_entry, n_entry;

	if (ip->txr) {
		ioc3_clean_tx_ring(ip);
		free_pages((unsigned long)ip->txr, 2);
		ip->txr = NULL;
	}

	if (ip->rxr) {
		n_entry = ip->rx_ci;
		rx_entry = ip->rx_pi;

		while (n_entry != rx_entry) {
			skb = ip->rx_skbs[n_entry];
			if (skb)
				dev_kfree_skb_any(skb);

			n_entry = (n_entry + 1) & 511;
		}
		free_page((unsigned long)ip->rxr);
		ip->rxr = NULL;
	}
}

static void ioc3_alloc_rings(struct net_device *dev)
{
	struct ioc3_private *ip = netdev_priv(dev);
	struct ioc3_erxbuf *rxb;
	unsigned long *rxr;
	int i;

	if (ip->rxr == NULL) {
		/* Allocate and initialize rx ring.  4kb = 512 entries  */
		ip->rxr = (unsigned long *) get_zeroed_page(GFP_ATOMIC);
		rxr = ip->rxr;
		if (!rxr)
			printk("ioc3_alloc_rings(): get_zeroed_page() failed!\n");

		/* Now the rx buffers.  The RX ring may be larger but
		   we only allocate 16 buffers for now.  Need to tune
		   this for performance and memory later.  */
		for (i = 0; i < RX_BUFFS; i++) {
			struct sk_buff *skb;

			skb = ioc3_alloc_skb(RX_BUF_ALLOC_SIZE, GFP_ATOMIC);
			if (!skb) {
				show_free_areas(0);
				continue;
			}

			ip->rx_skbs[i] = skb;

			/* Because we reserve afterwards. */
			skb_put(skb, (1664 + RX_OFFSET));
			rxb = (struct ioc3_erxbuf *) skb->data;
			rxr[i] = cpu_to_be64(ioc3_map(rxb, 1));
			skb_reserve(skb, RX_OFFSET);
		}
		ip->rx_ci = 0;
		ip->rx_pi = RX_BUFFS;
	}

	if (ip->txr == NULL) {
		/* Allocate and initialize tx rings.  16kb = 128 bufs.  */
		ip->txr = (struct ioc3_etxd *)__get_free_pages(GFP_KERNEL, 2);
		if (!ip->txr)
			printk("ioc3_alloc_rings(): __get_free_pages() failed!\n");
		ip->tx_pi = 0;
		ip->tx_ci = 0;
	}
}

static void ioc3_init_rings(struct net_device *dev)
{
	struct ioc3_private *ip = netdev_priv(dev);
	struct ioc3 *ioc3 = ip->regs;
	unsigned long ring;

	ioc3_free_rings(ip);
	ioc3_alloc_rings(dev);

	ioc3_clean_rx_ring(ip);
	ioc3_clean_tx_ring(ip);

	/* Now the rx ring base, consume & produce registers.  */
	ring = ioc3_map(ip->rxr, 0);
	ioc3_w_erbr_h(ring >> 32);
	ioc3_w_erbr_l(ring & 0xffffffff);
	ioc3_w_ercir(ip->rx_ci << 3);
	ioc3_w_erpir((ip->rx_pi << 3) | ERPIR_ARM);

	ring = ioc3_map(ip->txr, 0);

	ip->txqlen = 0;					/* nothing queued  */

	/* Now the tx ring base, consume & produce registers.  */
	ioc3_w_etbr_h(ring >> 32);
	ioc3_w_etbr_l(ring & 0xffffffff);
	ioc3_w_etpir(ip->tx_pi << 7);
	ioc3_w_etcir(ip->tx_ci << 7);
	(void) ioc3_r_etcir();				/* Flush */
}

static inline void ioc3_ssram_disc(struct ioc3_private *ip)
{
	struct ioc3 *ioc3 = ip->regs;
	volatile u32 *ssram0 = &ioc3->ssram[0x0000];
	volatile u32 *ssram1 = &ioc3->ssram[0x4000];
	unsigned int pattern = 0x5555;

	/* Assume the larger size SSRAM and enable parity checking */
	ioc3_w_emcr(ioc3_r_emcr() | (EMCR_BUFSIZ | EMCR_RAMPAR));

	*ssram0 = pattern;
	*ssram1 = ~pattern & IOC3_SSRAM_DM;

	if ((*ssram0 & IOC3_SSRAM_DM) != pattern ||
	    (*ssram1 & IOC3_SSRAM_DM) != (~pattern & IOC3_SSRAM_DM)) {
		/* set ssram size to 64 KB */
		ip->emcr = EMCR_RAMPAR;
		ioc3_w_emcr(ioc3_r_emcr() & ~EMCR_BUFSIZ);
	} else
		ip->emcr = EMCR_BUFSIZ | EMCR_RAMPAR;
}

static void ioc3_init(struct net_device *dev)
{
	struct ioc3_private *ip = netdev_priv(dev);
	struct ioc3 *ioc3 = ip->regs;

	del_timer_sync(&ip->ioc3_timer);	/* Kill if running	*/

	ioc3_w_emcr(EMCR_RST);			/* Reset		*/
	(void) ioc3_r_emcr();			/* Flush WB		*/
	udelay(4);				/* Give it time ...	*/
	ioc3_w_emcr(0);
	(void) ioc3_r_emcr();

	/* Misc registers  */
#ifdef CONFIG_SGI_IP27
	ioc3_w_erbar(PCI64_ATTR_BAR >> 32);	/* Barrier on last store */
#else
	ioc3_w_erbar(0);			/* Let PCI API get it right */
#endif
	(void) ioc3_r_etcdc();			/* Clear on read */
	ioc3_w_ercsr(15);			/* RX low watermark  */
	ioc3_w_ertr(0);				/* Interrupt immediately */
	__ioc3_set_mac_address(dev);
	ioc3_w_ehar_h(ip->ehar_h);
	ioc3_w_ehar_l(ip->ehar_l);
	ioc3_w_ersr(42);			/* XXX should be random */

	ioc3_init_rings(dev);

	ip->emcr |= ((RX_OFFSET / 2) << EMCR_RXOFF_SHIFT) | EMCR_TXDMAEN |
	             EMCR_TXEN | EMCR_RXDMAEN | EMCR_RXEN | EMCR_PADEN;
	ioc3_w_emcr(ip->emcr);
	ioc3_w_eier(EISR_RXTIMERINT | EISR_RXOFLO | EISR_RXBUFOFLO |
	            EISR_RXMEMERR | EISR_RXPARERR | EISR_TXBUFUFLO |
	            EISR_TXEXPLICIT | EISR_TXMEMERR);
	(void) ioc3_r_eier();
}

static inline void ioc3_stop(struct ioc3_private *ip)
{
	struct ioc3 *ioc3 = ip->regs;

	ioc3_w_emcr(0);				/* Shutup */
	ioc3_w_eier(0);				/* Disable interrupts */
	(void) ioc3_r_eier();			/* Flush */
}

static int ioc3_open(struct net_device *dev)
{
	struct ioc3_private *ip = netdev_priv(dev);

	if (request_irq(dev->irq, ioc3_interrupt, IRQF_SHARED, ioc3_str, dev)) {
		printk(KERN_ERR "%s: Can't get irq %d\n", dev->name, dev->irq);

		return -EAGAIN;
	}

	ip->ehar_h = 0;
	ip->ehar_l = 0;
	ioc3_init(dev);
	ioc3_mii_start(ip);

	netif_start_queue(dev);
	return 0;
}

static int ioc3_close(struct net_device *dev)
{
	struct ioc3_private *ip = netdev_priv(dev);

	del_timer_sync(&ip->ioc3_timer);

	netif_stop_queue(dev);

	ioc3_stop(ip);
	free_irq(dev->irq, dev);

	ioc3_free_rings(ip);
	return 0;
}

/*
 * MENET cards have four IOC3 chips, which are attached to two sets of
 * PCI slot resources each: the primary connections are on slots
 * 0..3 and the secondaries are on 4..7
 *
 * All four ethernets are brought out to connectors; six serial ports
 * (a pair from each of the first three IOC3s) are brought out to
 * MiniDINs; all other subdevices are left swinging in the wind, leave
 * them disabled.
 */

static int ioc3_adjacent_is_ioc3(struct pci_dev *pdev, int slot)
{
	struct pci_dev *dev = pci_get_slot(pdev->bus, PCI_DEVFN(slot, 0));
	int ret = 0;

	if (dev) {
		if (dev->vendor == PCI_VENDOR_ID_SGI &&
			dev->device == PCI_DEVICE_ID_SGI_IOC3)
			ret = 1;
		pci_dev_put(dev);
	}

	return ret;
}

static int ioc3_is_menet(struct pci_dev *pdev)
{
	return pdev->bus->parent == NULL &&
	       ioc3_adjacent_is_ioc3(pdev, 0) &&
	       ioc3_adjacent_is_ioc3(pdev, 1) &&
	       ioc3_adjacent_is_ioc3(pdev, 2);
}

#ifdef CONFIG_SERIAL_8250
/*
 * Note about serial ports and consoles:
 * For console output, everyone uses the IOC3 UARTA (offset 0x178)
 * connected to the master node (look in ip27_setup_console() and
 * ip27prom_console_write()).
 *
 * For serial (/dev/ttyS0 etc), we can not have hardcoded serial port
 * addresses on a partitioned machine. Since we currently use the ioc3
 * serial ports, we use dynamic serial port discovery that the serial.c
 * driver uses for pci/pnp ports (there is an entry for the SGI ioc3
 * boards in pci_boards[]). Unfortunately, UARTA's pio address is greater
 * than UARTB's, although UARTA on o200s has traditionally been known as
 * port 0. So, we just use one serial port from each ioc3 (since the
 * serial driver adds addresses to get to higher ports).
 *
 * The first one to do a register_console becomes the preferred console
 * (if there is no kernel command line console= directive). /dev/console
 * (ie 5, 1) is then "aliased" into the device number returned by the
 * "device" routine referred to in this console structure
 * (ip27prom_console_dev).
 *
 * Also look in ip27-pci.c:pci_fixup_ioc3() for some comments on working
 * around ioc3 oddities in this respect.
 *
 * The IOC3 serials use a 22MHz clock rate with an additional divider which
 * can be programmed in the SCR register if the DLAB bit is set.
 *
 * Register to interrupt zero because we share the interrupt with
 * the serial driver which we don't properly support yet.
 *
 * Can't use UPF_IOREMAP as the whole of IOC3 resources have already been
 * registered.
 */
static void ioc3_8250_register(struct ioc3_uartregs __iomem *uart)
{
#define COSMISC_CONSTANT 6

	struct uart_8250_port port = {
	        .port = {
			.irq		= 0,
			.flags		= UPF_SKIP_TEST | UPF_BOOT_AUTOCONF,
			.iotype		= UPIO_MEM,
			.regshift	= 0,
			.uartclk	= (22000000 << 1) / COSMISC_CONSTANT,

			.membase	= (unsigned char __iomem *) uart,
			.mapbase	= (unsigned long) uart,
                }
	};
	unsigned char lcr;

	lcr = uart->iu_lcr;
	uart->iu_lcr = lcr | UART_LCR_DLAB;
	uart->iu_scr = COSMISC_CONSTANT,
	uart->iu_lcr = lcr;
	uart->iu_lcr;
	serial8250_register_8250_port(&port);
}

static void ioc3_serial_probe(struct pci_dev *pdev, struct ioc3 *ioc3)
{
	/*
	 * We need to recognice and treat the fourth MENET serial as it
	 * does not have an SuperIO chip attached to it, therefore attempting
	 * to access it will result in bus errors.  We call something an
	 * MENET if PCI slot 0, 1, 2 and 3 of a master PCI bus all have an IOC3
	 * in it.  This is paranoid but we want to avoid blowing up on a
	 * showhorn PCI box that happens to have 4 IOC3 cards in it so it's
	 * not paranoid enough ...
	 */
	if (ioc3_is_menet(pdev) && PCI_SLOT(pdev->devfn) == 3)
		return;

	/*
	 * Switch IOC3 to PIO mode.  It probably already was but let's be
	 * paranoid
	 */
	ioc3->gpcr_s = GPCR_UARTA_MODESEL | GPCR_UARTB_MODESEL;
	ioc3->gpcr_s;
	ioc3->gppr_6 = 0;
	ioc3->gppr_6;
	ioc3->gppr_7 = 0;
	ioc3->gppr_7;
	ioc3->sscr_a = ioc3->sscr_a & ~SSCR_DMA_EN;
	ioc3->sscr_a;
	ioc3->sscr_b = ioc3->sscr_b & ~SSCR_DMA_EN;
	ioc3->sscr_b;
	/* Disable all SA/B interrupts except for SA/B_INT in SIO_IEC. */
	ioc3->sio_iec &= ~ (SIO_IR_SA_TX_MT | SIO_IR_SA_RX_FULL |
			    SIO_IR_SA_RX_HIGH | SIO_IR_SA_RX_TIMER |
			    SIO_IR_SA_DELTA_DCD | SIO_IR_SA_DELTA_CTS |
			    SIO_IR_SA_TX_EXPLICIT | SIO_IR_SA_MEMERR);
	ioc3->sio_iec |= SIO_IR_SA_INT;
	ioc3->sscr_a = 0;
	ioc3->sio_iec &= ~ (SIO_IR_SB_TX_MT | SIO_IR_SB_RX_FULL |
			    SIO_IR_SB_RX_HIGH | SIO_IR_SB_RX_TIMER |
			    SIO_IR_SB_DELTA_DCD | SIO_IR_SB_DELTA_CTS |
			    SIO_IR_SB_TX_EXPLICIT | SIO_IR_SB_MEMERR);
	ioc3->sio_iec |= SIO_IR_SB_INT;
	ioc3->sscr_b = 0;

	ioc3_8250_register(&ioc3->sregs.uarta);
	ioc3_8250_register(&ioc3->sregs.uartb);
}
#endif

static const struct net_device_ops ioc3_netdev_ops = {
	.ndo_open		= ioc3_open,
	.ndo_stop		= ioc3_close,
	.ndo_start_xmit		= ioc3_start_xmit,
	.ndo_tx_timeout		= ioc3_timeout,
	.ndo_get_stats		= ioc3_get_stats,
	.ndo_set_rx_mode	= ioc3_set_multicast_list,
	.ndo_do_ioctl		= ioc3_ioctl,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_mac_address	= ioc3_set_mac_address,
	.ndo_change_mtu		= eth_change_mtu,
};

static int ioc3_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	unsigned int sw_physid1, sw_physid2;
	struct net_device *dev = NULL;
	struct ioc3_private *ip;
	struct ioc3 *ioc3;
	unsigned long ioc3_base, ioc3_size;
	u32 vendor, model, rev;
	int err, pci_using_dac;

	/* Configure DMA attributes. */
	err = pci_set_dma_mask(pdev, DMA_BIT_MASK(64));
	if (!err) {
		pci_using_dac = 1;
		err = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(64));
		if (err < 0) {
			printk(KERN_ERR "%s: Unable to obtain 64 bit DMA "
			       "for consistent allocations\n", pci_name(pdev));
			goto out;
		}
	} else {
		err = pci_set_dma_mask(pdev, DMA_BIT_MASK(32));
		if (err) {
			printk(KERN_ERR "%s: No usable DMA configuration, "
			       "aborting.\n", pci_name(pdev));
			goto out;
		}
		pci_using_dac = 0;
	}

	if (pci_enable_device(pdev))
		return -ENODEV;

	dev = alloc_etherdev(sizeof(struct ioc3_private));
	if (!dev) {
		err = -ENOMEM;
		goto out_disable;
	}

	if (pci_using_dac)
		dev->features |= NETIF_F_HIGHDMA;

	err = pci_request_regions(pdev, "ioc3");
	if (err)
		goto out_free;

	SET_NETDEV_DEV(dev, &pdev->dev);

	ip = netdev_priv(dev);

	dev->irq = pdev->irq;

	ioc3_base = pci_resource_start(pdev, 0);
	ioc3_size = pci_resource_len(pdev, 0);
	ioc3 = (struct ioc3 *) ioremap(ioc3_base, ioc3_size);
	if (!ioc3) {
		printk(KERN_CRIT "ioc3eth(%s): ioremap failed, goodbye.\n",
		       pci_name(pdev));
		err = -ENOMEM;
		goto out_res;
	}
	ip->regs = ioc3;

#ifdef CONFIG_SERIAL_8250
	ioc3_serial_probe(pdev, ioc3);
#endif

	spin_lock_init(&ip->ioc3_lock);
	init_timer(&ip->ioc3_timer);

	ioc3_stop(ip);
	ioc3_init(dev);

	ip->pdev = pdev;

	ip->mii.phy_id_mask = 0x1f;
	ip->mii.reg_num_mask = 0x1f;
	ip->mii.dev = dev;
	ip->mii.mdio_read = ioc3_mdio_read;
	ip->mii.mdio_write = ioc3_mdio_write;

	ioc3_mii_init(ip);

	if (ip->mii.phy_id == -1) {
		printk(KERN_CRIT "ioc3-eth(%s): Didn't find a PHY, goodbye.\n",
		       pci_name(pdev));
		err = -ENODEV;
		goto out_stop;
	}

	ioc3_mii_start(ip);
	ioc3_ssram_disc(ip);
	ioc3_get_eaddr(ip);

	/* The IOC3-specific entries in the device structure. */
	dev->watchdog_timeo	= 5 * HZ;
	dev->netdev_ops		= &ioc3_netdev_ops;
	dev->ethtool_ops	= &ioc3_ethtool_ops;
	dev->hw_features	= NETIF_F_IP_CSUM | NETIF_F_RXCSUM;
	dev->features		= NETIF_F_IP_CSUM;

	sw_physid1 = ioc3_mdio_read(dev, ip->mii.phy_id, MII_PHYSID1);
	sw_physid2 = ioc3_mdio_read(dev, ip->mii.phy_id, MII_PHYSID2);

	err = register_netdev(dev);
	if (err)
		goto out_stop;

	mii_check_media(&ip->mii, 1, 1);
	ioc3_setup_duplex(ip);

	vendor = (sw_physid1 << 12) | (sw_physid2 >> 4);
	model  = (sw_physid2 >> 4) & 0x3f;
	rev    = sw_physid2 & 0xf;
	printk(KERN_INFO "%s: Using PHY %d, vendor 0x%x, model %d, "
	       "rev %d.\n", dev->name, ip->mii.phy_id, vendor, model, rev);
	printk(KERN_INFO "%s: IOC3 SSRAM has %d kbyte.\n", dev->name,
	       ip->emcr & EMCR_BUFSIZ ? 128 : 64);

	return 0;

out_stop:
	ioc3_stop(ip);
	del_timer_sync(&ip->ioc3_timer);
	ioc3_free_rings(ip);
out_res:
	pci_release_regions(pdev);
out_free:
	free_netdev(dev);
out_disable:
	/*
	 * We should call pci_disable_device(pdev); here if the IOC3 wasn't
	 * such a weird device ...
	 */
out:
	return err;
}

static void ioc3_remove_one(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	struct ioc3_private *ip = netdev_priv(dev);
	struct ioc3 *ioc3 = ip->regs;

	unregister_netdev(dev);
	del_timer_sync(&ip->ioc3_timer);

	iounmap(ioc3);
	pci_release_regions(pdev);
	free_netdev(dev);
	/*
	 * We should call pci_disable_device(pdev); here if the IOC3 wasn't
	 * such a weird device ...
	 */
}

static DEFINE_PCI_DEVICE_TABLE(ioc3_pci_tbl) = {
	{ PCI_VENDOR_ID_SGI, PCI_DEVICE_ID_SGI_IOC3, PCI_ANY_ID, PCI_ANY_ID },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, ioc3_pci_tbl);

static struct pci_driver ioc3_driver = {
	.name		= "ioc3-eth",
	.id_table	= ioc3_pci_tbl,
	.probe		= ioc3_probe,
	.remove		= ioc3_remove_one,
};

static int ioc3_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	unsigned long data;
	struct ioc3_private *ip = netdev_priv(dev);
	struct ioc3 *ioc3 = ip->regs;
	unsigned int len;
	struct ioc3_etxd *desc;
	uint32_t w0 = 0;
	int produce;

	/*
	 * IOC3 has a fairly simple minded checksumming hardware which simply
	 * adds up the 1's complement checksum for the entire packet and
	 * inserts it at an offset which can be specified in the descriptor
	 * into the transmit packet.  This means we have to compensate for the
	 * MAC header which should not be summed and the TCP/UDP pseudo headers
	 * manually.
	 */
	if (skb->ip_summed == CHECKSUM_PARTIAL) {
		const struct iphdr *ih = ip_hdr(skb);
		const int proto = ntohs(ih->protocol);
		unsigned int csoff;
		uint32_t csum, ehsum;
		uint16_t *eh;

		/* The MAC header.  skb->mac seem the logic approach
		   to find the MAC header - except it's a NULL pointer ...  */
		eh = (uint16_t *) skb->data;

		/* Sum up dest addr, src addr and protocol  */
		ehsum = eh[0] + eh[1] + eh[2] + eh[3] + eh[4] + eh[5] + eh[6];

		/* Fold ehsum.  can't use csum_fold which negates also ...  */
		ehsum = (ehsum & 0xffff) + (ehsum >> 16);
		ehsum = (ehsum & 0xffff) + (ehsum >> 16);

		/* Skip IP header; it's sum is always zero and was
		   already filled in by ip_output.c */
		csum = csum_tcpudp_nofold(ih->saddr, ih->daddr,
		                          ih->tot_len - (ih->ihl << 2),
		                          proto, 0xffff ^ ehsum);

		csum = (csum & 0xffff) + (csum >> 16);	/* Fold again */
		csum = (csum & 0xffff) + (csum >> 16);

		csoff = ETH_HLEN + (ih->ihl << 2);
		if (proto == IPPROTO_UDP) {
			csoff += offsetof(struct udphdr, check);
			udp_hdr(skb)->check = csum;
		}
		if (proto == IPPROTO_TCP) {
			csoff += offsetof(struct tcphdr, check);
			tcp_hdr(skb)->check = csum;
		}

		w0 = ETXD_DOCHECKSUM | (csoff << ETXD_CHKOFF_SHIFT);
	}

	spin_lock_irq(&ip->ioc3_lock);

	data = (unsigned long) skb->data;
	len = skb->len;

	produce = ip->tx_pi;
	desc = &ip->txr[produce];

	if (len <= 104) {
		/* Short packet, let's copy it directly into the ring.  */
		skb_copy_from_linear_data(skb, desc->data, skb->len);
		if (len < ETH_ZLEN) {
			/* Very short packet, pad with zeros at the end. */
			memset(desc->data + len, 0, ETH_ZLEN - len);
			len = ETH_ZLEN;
		}
		desc->cmd = cpu_to_be32(len | ETXD_INTWHENDONE | ETXD_D0V | w0);
		desc->bufcnt = cpu_to_be32(len);
	} else if ((data ^ (data + len - 1)) & 0x4000) {
		unsigned long b2 = (data | 0x3fffUL) + 1UL;
		unsigned long s1 = b2 - data;
		unsigned long s2 = data + len - b2;

		desc->cmd    = cpu_to_be32(len | ETXD_INTWHENDONE |
		                           ETXD_B1V | ETXD_B2V | w0);
		desc->bufcnt = cpu_to_be32((s1 << ETXD_B1CNT_SHIFT) |
		                           (s2 << ETXD_B2CNT_SHIFT));
		desc->p1     = cpu_to_be64(ioc3_map(skb->data, 1));
		desc->p2     = cpu_to_be64(ioc3_map((void *) b2, 1));
	} else {
		/* Normal sized packet that doesn't cross a page boundary. */
		desc->cmd = cpu_to_be32(len | ETXD_INTWHENDONE | ETXD_B1V | w0);
		desc->bufcnt = cpu_to_be32(len << ETXD_B1CNT_SHIFT);
		desc->p1     = cpu_to_be64(ioc3_map(skb->data, 1));
	}

	BARRIER();

	ip->tx_skbs[produce] = skb;			/* Remember skb */
	produce = (produce + 1) & 127;
	ip->tx_pi = produce;
	ioc3_w_etpir(produce << 7);			/* Fire ... */

	ip->txqlen++;

	if (ip->txqlen >= 127)
		netif_stop_queue(dev);

	spin_unlock_irq(&ip->ioc3_lock);

	return NETDEV_TX_OK;
}

static void ioc3_timeout(struct net_device *dev)
{
	struct ioc3_private *ip = netdev_priv(dev);

	printk(KERN_ERR "%s: transmit timed out, resetting\n", dev->name);

	spin_lock_irq(&ip->ioc3_lock);

	ioc3_stop(ip);
	ioc3_init(dev);
	ioc3_mii_init(ip);
	ioc3_mii_start(ip);

	spin_unlock_irq(&ip->ioc3_lock);

	netif_wake_queue(dev);
}

/*
 * Given a multicast ethernet address, this routine calculates the
 * address's bit index in the logical address filter mask
 */

static inline unsigned int ioc3_hash(const unsigned char *addr)
{
	unsigned int temp = 0;
	u32 crc;
	int bits;

	crc = ether_crc_le(ETH_ALEN, addr);

	crc &= 0x3f;    /* bit reverse lowest 6 bits for hash index */
	for (bits = 6; --bits >= 0; ) {
		temp <<= 1;
		temp |= (crc & 0x1);
		crc >>= 1;
	}

	return temp;
}

static void ioc3_get_drvinfo (struct net_device *dev,
	struct ethtool_drvinfo *info)
{
	struct ioc3_private *ip = netdev_priv(dev);

	strlcpy(info->driver, IOC3_NAME, sizeof(info->driver));
	strlcpy(info->version, IOC3_VERSION, sizeof(info->version));
	strlcpy(info->bus_info, pci_name(ip->pdev), sizeof(info->bus_info));
}

static int ioc3_get_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct ioc3_private *ip = netdev_priv(dev);
	int rc;

	spin_lock_irq(&ip->ioc3_lock);
	rc = mii_ethtool_gset(&ip->mii, cmd);
	spin_unlock_irq(&ip->ioc3_lock);

	return rc;
}

static int ioc3_set_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct ioc3_private *ip = netdev_priv(dev);
	int rc;

	spin_lock_irq(&ip->ioc3_lock);
	rc = mii_ethtool_sset(&ip->mii, cmd);
	spin_unlock_irq(&ip->ioc3_lock);

	return rc;
}

static int ioc3_nway_reset(struct net_device *dev)
{
	struct ioc3_private *ip = netdev_priv(dev);
	int rc;

	spin_lock_irq(&ip->ioc3_lock);
	rc = mii_nway_restart(&ip->mii);
	spin_unlock_irq(&ip->ioc3_lock);

	return rc;
}

static u32 ioc3_get_link(struct net_device *dev)
{
	struct ioc3_private *ip = netdev_priv(dev);
	int rc;

	spin_lock_irq(&ip->ioc3_lock);
	rc = mii_link_ok(&ip->mii);
	spin_unlock_irq(&ip->ioc3_lock);

	return rc;
}

static const struct ethtool_ops ioc3_ethtool_ops = {
	.get_drvinfo		= ioc3_get_drvinfo,
	.get_settings		= ioc3_get_settings,
	.set_settings		= ioc3_set_settings,
	.nway_reset		= ioc3_nway_reset,
	.get_link		= ioc3_get_link,
};

static int ioc3_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct ioc3_private *ip = netdev_priv(dev);
	int rc;

	spin_lock_irq(&ip->ioc3_lock);
	rc = generic_mii_ioctl(&ip->mii, if_mii(rq), cmd, NULL);
	spin_unlock_irq(&ip->ioc3_lock);

	return rc;
}

static void ioc3_set_multicast_list(struct net_device *dev)
{
	struct netdev_hw_addr *ha;
	struct ioc3_private *ip = netdev_priv(dev);
	struct ioc3 *ioc3 = ip->regs;
	u64 ehar = 0;

	netif_stop_queue(dev);				/* Lock out others. */

	if (dev->flags & IFF_PROMISC) {			/* Set promiscuous.  */
		ip->emcr |= EMCR_PROMISC;
		ioc3_w_emcr(ip->emcr);
		(void) ioc3_r_emcr();
	} else {
		ip->emcr &= ~EMCR_PROMISC;
		ioc3_w_emcr(ip->emcr);			/* Clear promiscuous. */
		(void) ioc3_r_emcr();

		if ((dev->flags & IFF_ALLMULTI) ||
		    (netdev_mc_count(dev) > 64)) {
			/* Too many for hashing to make sense or we want all
			   multicast packets anyway,  so skip computing all the
			   hashes and just accept all packets.  */
			ip->ehar_h = 0xffffffff;
			ip->ehar_l = 0xffffffff;
		} else {
			netdev_for_each_mc_addr(ha, dev) {
				ehar |= (1UL << ioc3_hash(ha->addr));
			}
			ip->ehar_h = ehar >> 32;
			ip->ehar_l = ehar & 0xffffffff;
		}
		ioc3_w_ehar_h(ip->ehar_h);
		ioc3_w_ehar_l(ip->ehar_l);
	}

	netif_wake_queue(dev);			/* Let us get going again. */
}

module_pci_driver(ioc3_driver);
MODULE_AUTHOR("Ralf Baechle <ralf@linux-mips.org>");
MODULE_DESCRIPTION("SGI IOC3 Ethernet driver");
MODULE_LICENSE("GPL");
