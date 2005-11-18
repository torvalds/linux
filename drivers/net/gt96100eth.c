/*
 * Copyright 2000, 2001 MontaVista Software Inc.
 * Author: MontaVista Software, Inc.
 *         	stevel@mvista.com or source@mvista.com
 *
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * Ethernet driver for the MIPS GT96100 Advanced Communication Controller.
 * 
 *  Revision history
 *    
 *    11.11.2001  Moved to 2.4.14, ppopov@mvista.com.  Modified driver to add
 *                proper gt96100A support.
 *    12.05.2001  Moved eth port 0 to irq 3 (mapped to GT_SERINT0 on EV96100A)
 *                in order for both ports to work. Also cleaned up boot
 *                option support (mac address string parsing), fleshed out
 *                gt96100_cleanup_module(), and other general code cleanups
 *                <stevel@mvista.com>.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/errno.h>
#include <linux/in.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/delay.h>
#include <linux/ctype.h>
#include <linux/bitops.h>

#include <asm/irq.h>
#include <asm/io.h>

#define DESC_BE 1
#define DESC_DATA_BE 1

#define GT96100_DEBUG 2

#include "gt96100eth.h"

// prototypes
static void* dmaalloc(size_t size, dma_addr_t *dma_handle);
static void dmafree(size_t size, void *vaddr);
static void gt96100_delay(int msec);
static int gt96100_add_hash_entry(struct net_device *dev,
				  unsigned char* addr);
static void read_mib_counters(struct gt96100_private *gp);
static int read_MII(int phy_addr, u32 reg);
static int write_MII(int phy_addr, u32 reg, u16 data);
static int gt96100_init_module(void);
static void gt96100_cleanup_module(void);
static void dump_MII(int dbg_lvl, struct net_device *dev);
static void dump_tx_desc(int dbg_lvl, struct net_device *dev, int i);
static void dump_rx_desc(int dbg_lvl, struct net_device *dev, int i);
static void dump_skb(int dbg_lvl, struct net_device *dev,
		     struct sk_buff *skb);
static void update_stats(struct gt96100_private *gp);
static void abort(struct net_device *dev, u32 abort_bits);
static void hard_stop(struct net_device *dev);
static void enable_ether_irq(struct net_device *dev);
static void disable_ether_irq(struct net_device *dev);
static int gt96100_probe1(struct pci_dev *pci, int port_num);
static void reset_tx(struct net_device *dev);
static void reset_rx(struct net_device *dev);
static int gt96100_check_tx_consistent(struct gt96100_private *gp);
static int gt96100_init(struct net_device *dev);
static int gt96100_open(struct net_device *dev);
static int gt96100_close(struct net_device *dev);
static int gt96100_tx(struct sk_buff *skb, struct net_device *dev);
static int gt96100_rx(struct net_device *dev, u32 status);
static irqreturn_t gt96100_interrupt(int irq, void *dev_id, struct pt_regs *regs);
static void gt96100_tx_timeout(struct net_device *dev);
static void gt96100_set_rx_mode(struct net_device *dev);
static struct net_device_stats* gt96100_get_stats(struct net_device *dev);

extern char * __init prom_getcmdline(void);

static int max_interrupt_work = 32;

#define nibswap(x) ((((x) >> 4) & 0x0f) | (((x) << 4) & 0xf0))

#define RUN_AT(x) (jiffies + (x))

// For reading/writing 32-bit words and half-words from/to DMA memory
#ifdef DESC_BE
#define cpu_to_dma32 cpu_to_be32
#define dma32_to_cpu be32_to_cpu
#define cpu_to_dma16 cpu_to_be16
#define dma16_to_cpu be16_to_cpu
#else
#define cpu_to_dma32 cpu_to_le32
#define dma32_to_cpu le32_to_cpu
#define cpu_to_dma16 cpu_to_le16
#define dma16_to_cpu le16_to_cpu
#endif

static char mac0[18] = "00.02.03.04.05.06";
static char mac1[18] = "00.01.02.03.04.05";
MODULE_PARM(mac0, "c18");
MODULE_PARM(mac1, "c18");
MODULE_PARM_DESC(mac0, "MAC address for GT96100 ethernet port 0");
MODULE_PARM_DESC(mac1, "MAC address for GT96100 ethernet port 1");

/*
 * Info for the GT96100 ethernet controller's ports.
 */
static struct gt96100_if_t {
	struct net_device *dev;
	unsigned int  iobase;   // IO Base address of this port
	int           irq;      // IRQ number of this port
	char         *mac_str;
} gt96100_iflist[NUM_INTERFACES] = {
	{
		NULL,
		GT96100_ETH0_BASE, GT96100_ETHER0_IRQ,
		mac0
	},
	{
		NULL,
		GT96100_ETH1_BASE, GT96100_ETHER1_IRQ,
		mac1
	}
};

static inline const char*
chip_name(int chip_rev)
{
	switch (chip_rev) {
	case REV_GT96100:
		return "GT96100";
	case REV_GT96100A_1:
	case REV_GT96100A:
		return "GT96100A";
	default:
		return "Unknown GT96100";
	}
}

/*
  DMA memory allocation, derived from pci_alloc_consistent.
*/
static void * dmaalloc(size_t size, dma_addr_t *dma_handle)
{
	void *ret;
	
	ret = (void *)__get_free_pages(GFP_ATOMIC | GFP_DMA, get_order(size));
	
	if (ret != NULL) {
		dma_cache_inv((unsigned long)ret, size);
		if (dma_handle != NULL)
			*dma_handle = virt_to_phys(ret);

		/* bump virtual address up to non-cached area */
		ret = (void*)KSEG1ADDR(ret);
	}

	return ret;
}

static void dmafree(size_t size, void *vaddr)
{
	vaddr = (void*)KSEG0ADDR(vaddr);
	free_pages((unsigned long)vaddr, get_order(size));
}

static void gt96100_delay(int ms)
{
	if (in_interrupt())
		return;
	else
		msleep_interruptible(ms);
}

static int
parse_mac_addr(struct net_device *dev, char* macstr)
{
	int i, j;
	unsigned char result, value;
	
	for (i=0; i<6; i++) {
		result = 0;
		if (i != 5 && *(macstr+2) != '.') {
			err(__FILE__ "invalid mac address format: %d %c\n",
			    i, *(macstr+2));
			return -EINVAL;
		}
		
		for (j=0; j<2; j++) {
			if (isxdigit(*macstr) &&
			    (value = isdigit(*macstr) ? *macstr-'0' : 
			     toupper(*macstr)-'A'+10) < 16) {
				result = result*16 + value;
				macstr++;
			} else {
				err(__FILE__ "invalid mac address "
				    "character: %c\n", *macstr);
				return -EINVAL;
			}
		}

		macstr++; // step over '.'
		dev->dev_addr[i] = result;
	}

	return 0;
}


static int
read_MII(int phy_addr, u32 reg)
{
	int timedout = 20;
	u32 smir = smirOpCode | (phy_addr << smirPhyAdBit) |
		(reg << smirRegAdBit);

	// wait for last operation to complete
	while (GT96100_READ(GT96100_ETH_SMI_REG) & smirBusy) {
		// snooze for 1 msec and check again
		gt96100_delay(1);

		if (--timedout == 0) {
			printk(KERN_ERR "%s: busy timeout!!\n", __FUNCTION__);
			return -ENODEV;
		}
	}
    
	GT96100_WRITE(GT96100_ETH_SMI_REG, smir);

	timedout = 20;
	// wait for read to complete
	while (!((smir = GT96100_READ(GT96100_ETH_SMI_REG)) & smirReadValid)) {
		// snooze for 1 msec and check again
		gt96100_delay(1);
	
		if (--timedout == 0) {
			printk(KERN_ERR "%s: timeout!!\n", __FUNCTION__);
			return -ENODEV;
		}
	}

	return (int)(smir & smirDataMask);
}

static void
dump_tx_desc(int dbg_lvl, struct net_device *dev, int i)
{
	struct gt96100_private *gp = netdev_priv(dev);
	gt96100_td_t *td = &gp->tx_ring[i];

	dbg(dbg_lvl, "Tx descriptor at 0x%08lx:\n", virt_to_phys(td));
	dbg(dbg_lvl,
	    "    cmdstat=%04x, byte_cnt=%04x, buff_ptr=%04x, next=%04x\n",
	    dma32_to_cpu(td->cmdstat),
	    dma16_to_cpu(td->byte_cnt),
	    dma32_to_cpu(td->buff_ptr),
	    dma32_to_cpu(td->next));
}

static void
dump_rx_desc(int dbg_lvl, struct net_device *dev, int i)
{
	struct gt96100_private *gp = netdev_priv(dev);
	gt96100_rd_t *rd = &gp->rx_ring[i];

	dbg(dbg_lvl, "Rx descriptor at 0x%08lx:\n", virt_to_phys(rd));
	dbg(dbg_lvl, "    cmdstat=%04x, buff_sz=%04x, byte_cnt=%04x, "
	    "buff_ptr=%04x, next=%04x\n",
	    dma32_to_cpu(rd->cmdstat),
	    dma16_to_cpu(rd->buff_sz),
	    dma16_to_cpu(rd->byte_cnt),
	    dma32_to_cpu(rd->buff_ptr),
	    dma32_to_cpu(rd->next));
}

static int
write_MII(int phy_addr, u32 reg, u16 data)
{
	int timedout = 20;
	u32 smir = (phy_addr << smirPhyAdBit) |
		(reg << smirRegAdBit) | data;

	// wait for last operation to complete
	while (GT96100_READ(GT96100_ETH_SMI_REG) & smirBusy) {
		// snooze for 1 msec and check again
		gt96100_delay(1);
	
		if (--timedout == 0) {
			printk(KERN_ERR "%s: busy timeout!!\n", __FUNCTION__);
			return -1;
		}
	}

	GT96100_WRITE(GT96100_ETH_SMI_REG, smir);
	return 0;
}

static void
dump_MII(int dbg_lvl, struct net_device *dev)
{
	int i, val;
	struct gt96100_private *gp = netdev_priv(dev);
    
	if (dbg_lvl <= GT96100_DEBUG) {
		for (i=0; i<7; i++) {
			if ((val = read_MII(gp->phy_addr, i)) >= 0)
				printk("MII Reg %d=%x\n", i, val);
		}
		for (i=16; i<21; i++) {
			if ((val = read_MII(gp->phy_addr, i)) >= 0)
				printk("MII Reg %d=%x\n", i, val);
		}
	}
}

static void
dump_hw_addr(int dbg_lvl, struct net_device *dev, const char* pfx,
	     const char* func, unsigned char* addr_str)
{
	int i;
	char buf[100], octet[5];
    
	if (dbg_lvl <= GT96100_DEBUG) {
		sprintf(buf, pfx, func);
		for (i = 0; i < 6; i++) {
			sprintf(octet, "%2.2x%s",
				addr_str[i], i<5 ? ":" : "\n");
			strcat(buf, octet);
		}
		info("%s", buf);
	}
}


static void
dump_skb(int dbg_lvl, struct net_device *dev, struct sk_buff *skb)
{
	int i;
	unsigned char* skbdata;
    
	if (dbg_lvl <= GT96100_DEBUG) {
		dbg(dbg_lvl, "%s: skb=%p, skb->data=%p, skb->len=%d\n",
		    __FUNCTION__, skb, skb->data, skb->len);

		skbdata = (unsigned char*)KSEG1ADDR(skb->data);
    
		for (i=0; i<skb->len; i++) {
			if (!(i % 16))
				printk(KERN_DEBUG "\n   %3.3x: %2.2x,",
				       i, skbdata[i]);
			else
				printk(KERN_DEBUG "%2.2x,", skbdata[i]);
		}
		printk(KERN_DEBUG "\n");
	}
}


static int
gt96100_add_hash_entry(struct net_device *dev, unsigned char* addr)
{
	struct gt96100_private *gp = netdev_priv(dev);
	//u16 hashResult, stmp;
	//unsigned char ctmp, hash_ea[6];
	u32 tblEntry1, tblEntry0, *tblEntryAddr;
	int i;

	tblEntry1 = hteValid | hteRD;
	tblEntry1 |= (u32)addr[5] << 3;
	tblEntry1 |= (u32)addr[4] << 11;
	tblEntry1 |= (u32)addr[3] << 19;
	tblEntry1 |= ((u32)addr[2] & 0x1f) << 27;
	dbg(3, "%s: tblEntry1=%x\n", __FUNCTION__, tblEntry1);
	tblEntry0 = ((u32)addr[2] >> 5) & 0x07;
	tblEntry0 |= (u32)addr[1] << 3;
	tblEntry0 |= (u32)addr[0] << 11;
	dbg(3, "%s: tblEntry0=%x\n", __FUNCTION__, tblEntry0);

#if 0

	for (i=0; i<6; i++) {
		// nibble swap
		ctmp = nibswap(addr[i]);
		// invert every nibble
		hash_ea[i] = ((ctmp&1)<<3) | ((ctmp&8)>>3) |
			((ctmp&2)<<1) | ((ctmp&4)>>1);
		hash_ea[i] |= ((ctmp&0x10)<<3) | ((ctmp&0x80)>>3) |
			((ctmp&0x20)<<1) | ((ctmp&0x40)>>1);
	}

	dump_hw_addr(3, dev, "%s: nib swap/invt addr=", __FUNCTION__, hash_ea);
    
	if (gp->hash_mode == 0) {
		hashResult = ((u16)hash_ea[0] & 0xfc) << 7;
		stmp = ((u16)hash_ea[0] & 0x03) |
			(((u16)hash_ea[1] & 0x7f) << 2);
		stmp ^= (((u16)hash_ea[1] >> 7) & 0x01) |
			((u16)hash_ea[2] << 1);
		stmp ^= (u16)hash_ea[3] | (((u16)hash_ea[4] & 1) << 8);
		hashResult |= stmp;
	} else {
		return -1; // don't support hash mode 1
	}

	dbg(3, "%s: hashResult=%x\n", __FUNCTION__, hashResult);

	tblEntryAddr =
		(u32 *)(&gp->hash_table[((u32)hashResult & 0x7ff) << 3]);
    
	dbg(3, "%s: tblEntryAddr=%p\n", tblEntryAddr, __FUNCTION__);

	for (i=0; i<HASH_HOP_NUMBER; i++) {
		if ((*tblEntryAddr & hteValid) &&
		    !(*tblEntryAddr & hteSkip)) {
			// This entry is already occupied, go to next entry
			tblEntryAddr += 2;
			dbg(3, "%s: skipping to %p\n", __FUNCTION__, 
			    tblEntryAddr);
		} else {
			memset(tblEntryAddr, 0, 8);
			tblEntryAddr[1] = cpu_to_dma32(tblEntry1);
			tblEntryAddr[0] = cpu_to_dma32(tblEntry0);
			break;
		}
	}

	if (i >= HASH_HOP_NUMBER) {
		err("%s: expired!\n", __FUNCTION__);
		return -1; // Couldn't find an unused entry
	}

#else

	tblEntryAddr = (u32 *)gp->hash_table;
	for (i=0; i<RX_HASH_TABLE_SIZE/4; i+=2) {
		tblEntryAddr[i+1] = cpu_to_dma32(tblEntry1);
		tblEntryAddr[i] = cpu_to_dma32(tblEntry0);
	}

#endif
    
	return 0;
}


static void
read_mib_counters(struct gt96100_private *gp)
{
	u32* mib_regs = (u32*)&gp->mib;
	int i;
    
	for (i=0; i<sizeof(mib_counters_t)/sizeof(u32); i++)
		mib_regs[i] = GT96100ETH_READ(gp, GT96100_ETH_MIB_COUNT_BASE +
					      i*sizeof(u32));
}


static void
update_stats(struct gt96100_private *gp)
{
	mib_counters_t *mib = &gp->mib;
	struct net_device_stats *stats = &gp->stats;
    
	read_mib_counters(gp);
    
	stats->rx_packets = mib->totalFramesReceived;
	stats->tx_packets = mib->framesSent;
	stats->rx_bytes = mib->totalByteReceived;
	stats->tx_bytes = mib->byteSent;
	stats->rx_errors = mib->totalFramesReceived - mib->framesReceived;
	//the tx error counters are incremented by the ISR
	//rx_dropped incremented by gt96100_rx
	//tx_dropped incremented by gt96100_tx
	stats->multicast = mib->multicastFramesReceived;
	// collisions incremented by gt96100_tx_complete
	stats->rx_length_errors = mib->oversizeFrames + mib->fragments;
	// The RxError condition means the Rx DMA encountered a
	// CPU owned descriptor, which, if things are working as
	// they should, means the Rx ring has overflowed.
	stats->rx_over_errors = mib->macRxError;
	stats->rx_crc_errors = mib->cRCError;
}

static void
abort(struct net_device *dev, u32 abort_bits)
{
	struct gt96100_private *gp = netdev_priv(dev);
	int timedout = 100; // wait up to 100 msec for hard stop to complete

	dbg(3, "%s\n", __FUNCTION__);

	// Return if neither Rx or Tx abort bits are set
	if (!(abort_bits & (sdcmrAR | sdcmrAT)))
		return;

	// make sure only the Rx/Tx abort bits are set
	abort_bits &= (sdcmrAR | sdcmrAT);
    
	spin_lock(&gp->lock);

	// abort any Rx/Tx DMA immediately
	GT96100ETH_WRITE(gp, GT96100_ETH_SDMA_COMM, abort_bits);

	dbg(3, "%s: SDMA comm = %x\n", __FUNCTION__,
	    GT96100ETH_READ(gp, GT96100_ETH_SDMA_COMM));

	// wait for abort to complete
	while (GT96100ETH_READ(gp, GT96100_ETH_SDMA_COMM) & abort_bits) {
		// snooze for 1 msec and check again
		gt96100_delay(1);
	
		if (--timedout == 0) {
			err("%s: timeout!!\n", __FUNCTION__);
			break;
		}
	}

	spin_unlock(&gp->lock);
}


static void
hard_stop(struct net_device *dev)
{
	struct gt96100_private *gp = netdev_priv(dev);

	dbg(3, "%s\n", __FUNCTION__);

	disable_ether_irq(dev);

	abort(dev, sdcmrAR | sdcmrAT);

	// disable port
	GT96100ETH_WRITE(gp, GT96100_ETH_PORT_CONFIG, 0);
}


static void
enable_ether_irq(struct net_device *dev)
{
	struct gt96100_private *gp = netdev_priv(dev);
	u32 intMask;
	/*
	 * route ethernet interrupt to GT_SERINT0 for port 0,
	 * GT_INT0 for port 1.
	 */
	int intr_mask_reg = (gp->port_num == 0) ?
		GT96100_SERINT0_MASK : GT96100_INT0_HIGH_MASK;
	
	if (gp->chip_rev >= REV_GT96100A_1) {
		intMask = icrTxBufferLow | icrTxEndLow |
			icrTxErrorLow  | icrRxOVR | icrTxUdr |
			icrRxBufferQ0 | icrRxErrorQ0 |
			icrMIIPhySTC | icrEtherIntSum;
	}
	else {
		intMask = icrTxBufferLow | icrTxEndLow |
			icrTxErrorLow  | icrRxOVR | icrTxUdr |
			icrRxBuffer | icrRxError |
			icrMIIPhySTC | icrEtherIntSum;
	}
	
	// unmask interrupts
	GT96100ETH_WRITE(gp, GT96100_ETH_INT_MASK, intMask);
    
	intMask = GT96100_READ(intr_mask_reg);
	intMask |= 1<<gp->port_num;
	GT96100_WRITE(intr_mask_reg, intMask);
}

static void
disable_ether_irq(struct net_device *dev)
{
	struct gt96100_private *gp = netdev_priv(dev);
	u32 intMask;
	int intr_mask_reg = (gp->port_num == 0) ?
		GT96100_SERINT0_MASK : GT96100_INT0_HIGH_MASK;

	intMask = GT96100_READ(intr_mask_reg);
	intMask &= ~(1<<gp->port_num);
	GT96100_WRITE(intr_mask_reg, intMask);
    
	GT96100ETH_WRITE(gp, GT96100_ETH_INT_MASK, 0);
}


/*
 * Init GT96100 ethernet controller driver
 */
static int gt96100_init_module(void)
{
	struct pci_dev *pci;
	int i, retval=0;
	u32 cpuConfig;

	/*
	 * Stupid probe because this really isn't a PCI device
	 */
	if (!(pci = pci_find_device(PCI_VENDOR_ID_MARVELL,
	                            PCI_DEVICE_ID_MARVELL_GT96100, NULL)) &&
	    !(pci = pci_find_device(PCI_VENDOR_ID_MARVELL,
		                    PCI_DEVICE_ID_MARVELL_GT96100A, NULL))) {
		printk(KERN_ERR __FILE__ ": GT96100 not found!\n");
		return -ENODEV;
	}

	cpuConfig = GT96100_READ(GT96100_CPU_INTERF_CONFIG);
	if (cpuConfig & (1<<12)) {
		printk(KERN_ERR __FILE__
		       ": must be in Big Endian mode!\n");
		return -ENODEV;
	}

	for (i=0; i < NUM_INTERFACES; i++)
		retval |= gt96100_probe1(pci, i);

	return retval;
}

static int __init gt96100_probe1(struct pci_dev *pci, int port_num)
{
	struct gt96100_private *gp = NULL;
	struct gt96100_if_t *gtif = &gt96100_iflist[port_num];
	int phy_addr, phy_id1, phy_id2;
	u32 phyAD;
	int retval;
	unsigned char chip_rev;
	struct net_device *dev = NULL;
    
	if (gtif->irq < 0) {
		printk(KERN_ERR "%s: irq unknown - probing not supported\n",
		      __FUNCTION__);
		return -ENODEV;
	}
    
	pci_read_config_byte(pci, PCI_REVISION_ID, &chip_rev);

	if (chip_rev >= REV_GT96100A_1) {
		phyAD = GT96100_READ(GT96100_ETH_PHY_ADDR_REG);
		phy_addr = (phyAD >> (5*port_num)) & 0x1f;
	} else {
		/*
		 * not sure what's this about -- probably a gt bug
		 */
		phy_addr = port_num;
		phyAD = GT96100_READ(GT96100_ETH_PHY_ADDR_REG);
		phyAD &= ~(0x1f << (port_num*5));
		phyAD |= phy_addr << (port_num*5);
		GT96100_WRITE(GT96100_ETH_PHY_ADDR_REG, phyAD);
	}
	
	// probe for the external PHY
	if ((phy_id1 = read_MII(phy_addr, 2)) <= 0 ||
	    (phy_id2 = read_MII(phy_addr, 3)) <= 0) {
		printk(KERN_ERR "%s: no PHY found on MII%d\n", __FUNCTION__, port_num);
		return -ENODEV;
	}
	
	if (!request_region(gtif->iobase, GT96100_ETH_IO_SIZE, "GT96100ETH")) {
		printk(KERN_ERR "%s: request_region failed\n", __FUNCTION__);
		return -EBUSY;
	}

	dev = alloc_etherdev(sizeof(struct gt96100_private));
	if (!dev)
		goto out;
	gtif->dev = dev;
	
	/* private struct aligned and zeroed by alloc_etherdev */
	/* Fill in the 'dev' fields. */
	dev->base_addr = gtif->iobase;
	dev->irq = gtif->irq;

	if ((retval = parse_mac_addr(dev, gtif->mac_str))) {
		err("%s: MAC address parse failed\n", __FUNCTION__);
		retval = -EINVAL;
		goto out1;
	}

	gp = netdev_priv(dev);

	memset(gp, 0, sizeof(*gp)); // clear it

	gp->port_num = port_num;
	gp->io_size = GT96100_ETH_IO_SIZE;
	gp->port_offset = port_num * GT96100_ETH_IO_SIZE;
	gp->phy_addr = phy_addr;
	gp->chip_rev = chip_rev;

	info("%s found at 0x%x, irq %d\n",
	     chip_name(gp->chip_rev), gtif->iobase, gtif->irq);
	dump_hw_addr(0, dev, "%s: HW Address ", __FUNCTION__, dev->dev_addr);
	info("%s chip revision=%d\n", chip_name(gp->chip_rev), gp->chip_rev);
	info("%s ethernet port %d\n", chip_name(gp->chip_rev), gp->port_num);
	info("external PHY ID1=0x%04x, ID2=0x%04x\n", phy_id1, phy_id2);

	// Allocate Rx and Tx descriptor rings
	if (gp->rx_ring == NULL) {
		// All descriptors in ring must be 16-byte aligned
		gp->rx_ring = dmaalloc(sizeof(gt96100_rd_t) * RX_RING_SIZE
				       + sizeof(gt96100_td_t) * TX_RING_SIZE,
				       &gp->rx_ring_dma);
		if (gp->rx_ring == NULL) {
			retval = -ENOMEM;
			goto out1;
		}
	
		gp->tx_ring = (gt96100_td_t *)(gp->rx_ring + RX_RING_SIZE);
		gp->tx_ring_dma =
			gp->rx_ring_dma + sizeof(gt96100_rd_t) * RX_RING_SIZE;
	}
    
	// Allocate the Rx Data Buffers
	if (gp->rx_buff == NULL) {
		gp->rx_buff = dmaalloc(PKT_BUF_SZ*RX_RING_SIZE,
				       &gp->rx_buff_dma);
		if (gp->rx_buff == NULL) {
			retval = -ENOMEM;
			goto out2;
		}
	}
    
	dbg(3, "%s: rx_ring=%p, tx_ring=%p\n", __FUNCTION__,
	    gp->rx_ring, gp->tx_ring);

	// Allocate Rx Hash Table
	if (gp->hash_table == NULL) {
		gp->hash_table = (char*)dmaalloc(RX_HASH_TABLE_SIZE,
						 &gp->hash_table_dma);
		if (gp->hash_table == NULL) {
			retval = -ENOMEM;
			goto out3;
		}
	}
    
	dbg(3, "%s: hash=%p\n", __FUNCTION__, gp->hash_table);

	spin_lock_init(&gp->lock);
    
	dev->open = gt96100_open;
	dev->hard_start_xmit = gt96100_tx;
	dev->stop = gt96100_close;
	dev->get_stats = gt96100_get_stats;
	//dev->do_ioctl = gt96100_ioctl;
	dev->set_multicast_list = gt96100_set_rx_mode;
	dev->tx_timeout = gt96100_tx_timeout;
	dev->watchdog_timeo = GT96100ETH_TX_TIMEOUT;

	retval = register_netdev(dev);
	if (retval)
		goto out4;
	return 0;

out4:
	dmafree(RX_HASH_TABLE_SIZE, gp->hash_table_dma);
out3:
	dmafree(PKT_BUF_SZ*RX_RING_SIZE, gp->rx_buff);
out2:
	dmafree(sizeof(gt96100_rd_t) * RX_RING_SIZE
		+ sizeof(gt96100_td_t) * TX_RING_SIZE,
		gp->rx_ring);
out1:
	free_netdev (dev);
out:
	release_region(gtif->iobase, GT96100_ETH_IO_SIZE);

	err("%s failed.  Returns %d\n", __FUNCTION__, retval);
	return retval;
}


static void
reset_tx(struct net_device *dev)
{
	struct gt96100_private *gp = netdev_priv(dev);
	int i;

	abort(dev, sdcmrAT);

	for (i=0; i<TX_RING_SIZE; i++) {
		if (gp->tx_skbuff[i]) {
			if (in_interrupt())
				dev_kfree_skb_irq(gp->tx_skbuff[i]);
			else
				dev_kfree_skb(gp->tx_skbuff[i]);
			gp->tx_skbuff[i] = NULL;
		}

		gp->tx_ring[i].cmdstat = 0; // CPU owns
		gp->tx_ring[i].byte_cnt = 0;
		gp->tx_ring[i].buff_ptr = 0;
		gp->tx_ring[i].next =
			cpu_to_dma32(gp->tx_ring_dma +
				     sizeof(gt96100_td_t) * (i+1));
		dump_tx_desc(4, dev, i);
	}
	/* Wrap the ring. */
	gp->tx_ring[i-1].next = cpu_to_dma32(gp->tx_ring_dma);
    
	// setup only the lowest priority TxCDP reg
	GT96100ETH_WRITE(gp, GT96100_ETH_CURR_TX_DESC_PTR0, gp->tx_ring_dma);
	GT96100ETH_WRITE(gp, GT96100_ETH_CURR_TX_DESC_PTR1, 0);

	// init Tx indeces and pkt counter
	gp->tx_next_in = gp->tx_next_out = 0;
	gp->tx_count = 0;

}

static void
reset_rx(struct net_device *dev)
{
	struct gt96100_private *gp = netdev_priv(dev);
	int i;

	abort(dev, sdcmrAR);
    
	for (i=0; i<RX_RING_SIZE; i++) {
		gp->rx_ring[i].next =
			cpu_to_dma32(gp->rx_ring_dma +
				     sizeof(gt96100_rd_t) * (i+1));
		gp->rx_ring[i].buff_ptr =
			cpu_to_dma32(gp->rx_buff_dma + i*PKT_BUF_SZ);
		gp->rx_ring[i].buff_sz = cpu_to_dma16(PKT_BUF_SZ);
		// Give ownership to device, set first and last, enable intr
		gp->rx_ring[i].cmdstat =
			cpu_to_dma32((u32)(rxFirst | rxLast | rxOwn | rxEI));
		dump_rx_desc(4, dev, i);
	}
	/* Wrap the ring. */
	gp->rx_ring[i-1].next = cpu_to_dma32(gp->rx_ring_dma);

	// Setup only the lowest priority RxFDP and RxCDP regs
	for (i=0; i<4; i++) {
		if (i == 0) {
			GT96100ETH_WRITE(gp, GT96100_ETH_1ST_RX_DESC_PTR0,
					 gp->rx_ring_dma);
			GT96100ETH_WRITE(gp, GT96100_ETH_CURR_RX_DESC_PTR0,
					 gp->rx_ring_dma);
		} else {
			GT96100ETH_WRITE(gp,
					 GT96100_ETH_1ST_RX_DESC_PTR0 + i*4,
					 0);
			GT96100ETH_WRITE(gp,
					 GT96100_ETH_CURR_RX_DESC_PTR0 + i*4,
					 0);
		}
	}

	// init Rx NextOut index
	gp->rx_next_out = 0;
}


// Returns 1 if the Tx counter and indeces don't gel
static int
gt96100_check_tx_consistent(struct gt96100_private *gp)
{
	int diff = gp->tx_next_in - gp->tx_next_out;

	diff = diff<0 ? TX_RING_SIZE + diff : diff;
	diff = gp->tx_count == TX_RING_SIZE ? diff + TX_RING_SIZE : diff;
    
	return (diff != gp->tx_count);
}

static int
gt96100_init(struct net_device *dev)
{
	struct gt96100_private *gp = netdev_priv(dev);
	u32 tmp;
	u16 mii_reg;
    
	dbg(3, "%s: dev=%p\n", __FUNCTION__, dev);
	dbg(3, "%s: scs10_lo=%4x, scs10_hi=%4x\n", __FUNCTION__, 
	    GT96100_READ(0x8), GT96100_READ(0x10));
	dbg(3, "%s: scs32_lo=%4x, scs32_hi=%4x\n", __FUNCTION__,
	    GT96100_READ(0x18), GT96100_READ(0x20));
    
	// Stop and disable Port
	hard_stop(dev);
    
	// Setup CIU Arbiter
	tmp = GT96100_READ(GT96100_CIU_ARBITER_CONFIG);
	tmp |= (0x0c << (gp->port_num*2)); // set Ether DMA req priority to hi
#ifndef DESC_BE
	tmp &= ~(1<<31);                   // set desc endianess to little
#else
	tmp |= (1<<31);
#endif
	GT96100_WRITE(GT96100_CIU_ARBITER_CONFIG, tmp);
	dbg(3, "%s: CIU Config=%x/%x\n", __FUNCTION__, 
	    tmp, GT96100_READ(GT96100_CIU_ARBITER_CONFIG));

	// Set routing.
	tmp = GT96100_READ(GT96100_ROUTE_MAIN) & (0x3f << 18);
	tmp |= (0x07 << (18 + gp->port_num*3));
	GT96100_WRITE(GT96100_ROUTE_MAIN, tmp);

	/* set MII as peripheral func */
	tmp = GT96100_READ(GT96100_GPP_CONFIG2);
	tmp |= 0x7fff << (gp->port_num*16);
	GT96100_WRITE(GT96100_GPP_CONFIG2, tmp);
	
	/* Set up MII port pin directions */
	tmp = GT96100_READ(GT96100_GPP_IO2);
	tmp |= 0x003d << (gp->port_num*16);
	GT96100_WRITE(GT96100_GPP_IO2, tmp);

	// Set-up hash table
	memset(gp->hash_table, 0, RX_HASH_TABLE_SIZE); // clear it
	gp->hash_mode = 0;
	// Add a single entry to hash table - our ethernet address
	gt96100_add_hash_entry(dev, dev->dev_addr);
	// Set-up DMA ptr to hash table
	GT96100ETH_WRITE(gp, GT96100_ETH_HASH_TBL_PTR, gp->hash_table_dma);
	dbg(3, "%s: Hash Tbl Ptr=%x\n", __FUNCTION__,
	    GT96100ETH_READ(gp, GT96100_ETH_HASH_TBL_PTR));

	// Setup Tx
	reset_tx(dev);

	dbg(3, "%s: Curr Tx Desc Ptr0=%x\n", __FUNCTION__,
	    GT96100ETH_READ(gp, GT96100_ETH_CURR_TX_DESC_PTR0));

	// Setup Rx
	reset_rx(dev);

	dbg(3, "%s: 1st/Curr Rx Desc Ptr0=%x/%x\n", __FUNCTION__,
	    GT96100ETH_READ(gp, GT96100_ETH_1ST_RX_DESC_PTR0),
	    GT96100ETH_READ(gp, GT96100_ETH_CURR_RX_DESC_PTR0));

	// eth port config register
	GT96100ETH_WRITE(gp, GT96100_ETH_PORT_CONFIG_EXT,
			 pcxrFCTL | pcxrFCTLen | pcxrFLP | pcxrDPLXen);

	mii_reg = read_MII(gp->phy_addr, 0x11); /* int enable register */
	mii_reg |= 2;  /* enable mii interrupt */
	write_MII(gp->phy_addr, 0x11, mii_reg);
	
	dbg(3, "%s: PhyAD=%x\n", __FUNCTION__,
	    GT96100_READ(GT96100_ETH_PHY_ADDR_REG));

	// setup DMA

	// We want the Rx/Tx DMA to write/read data to/from memory in
	// Big Endian mode. Also set DMA Burst Size to 8 64Bit words.
#ifdef DESC_DATA_BE
	GT96100ETH_WRITE(gp, GT96100_ETH_SDMA_CONFIG,
			 (0xf<<sdcrRCBit) | sdcrRIFB | (3<<sdcrBSZBit));
#else
	GT96100ETH_WRITE(gp, GT96100_ETH_SDMA_CONFIG,
			 sdcrBLMR | sdcrBLMT |
			 (0xf<<sdcrRCBit) | sdcrRIFB | (3<<sdcrBSZBit));
#endif
	dbg(3, "%s: SDMA Config=%x\n", __FUNCTION__,
	    GT96100ETH_READ(gp, GT96100_ETH_SDMA_CONFIG));

	// start Rx DMA
	GT96100ETH_WRITE(gp, GT96100_ETH_SDMA_COMM, sdcmrERD);
	dbg(3, "%s: SDMA Comm=%x\n", __FUNCTION__,
	    GT96100ETH_READ(gp, GT96100_ETH_SDMA_COMM));
    
	// enable this port (set hash size to 1/2K)
	GT96100ETH_WRITE(gp, GT96100_ETH_PORT_CONFIG, pcrEN | pcrHS);
	dbg(3, "%s: Port Config=%x\n", __FUNCTION__,
	    GT96100ETH_READ(gp, GT96100_ETH_PORT_CONFIG));
    
	/*
	 * Disable all Type-of-Service queueing. All Rx packets will be
	 * treated normally and will be sent to the lowest priority
	 * queue.
	 *
	 * Disable flow-control for now. FIXME: support flow control?
	 */

	// clear all the MIB ctr regs
	GT96100ETH_WRITE(gp, GT96100_ETH_PORT_CONFIG_EXT,
			 pcxrFCTL | pcxrFCTLen | pcxrFLP |
			 pcxrPRIOrxOverride);
	read_mib_counters(gp);
	GT96100ETH_WRITE(gp, GT96100_ETH_PORT_CONFIG_EXT,
			 pcxrFCTL | pcxrFCTLen | pcxrFLP |
			 pcxrPRIOrxOverride | pcxrMIBclrMode);
    
	dbg(3, "%s: Port Config Ext=%x\n", __FUNCTION__,
	    GT96100ETH_READ(gp, GT96100_ETH_PORT_CONFIG_EXT));

	netif_start_queue(dev);

	dump_MII(4, dev);

	// enable interrupts
	enable_ether_irq(dev);

	// we should now be receiving frames
	return 0;
}


static int
gt96100_open(struct net_device *dev)
{
	int retval;
    
	dbg(2, "%s: dev=%p\n", __FUNCTION__, dev);

	// Initialize and startup the GT-96100 ethernet port
	if ((retval = gt96100_init(dev))) {
		err("error in gt96100_init\n");
		free_irq(dev->irq, dev);
		return retval;
	}

	if ((retval = request_irq(dev->irq, &gt96100_interrupt,
				  SA_SHIRQ, dev->name, dev))) {
		err("unable to get IRQ %d\n", dev->irq);
		return retval;
	}
	
	dbg(2, "%s: Initialization done.\n", __FUNCTION__);

	return 0;
}

static int
gt96100_close(struct net_device *dev)
{
	dbg(3, "%s: dev=%p\n", __FUNCTION__, dev);

	// stop the device
	if (netif_device_present(dev)) {
		netif_stop_queue(dev);
		hard_stop(dev);
	}

	free_irq(dev->irq, dev);
    
	return 0;
}


static int
gt96100_tx(struct sk_buff *skb, struct net_device *dev)
{
	struct gt96100_private *gp = netdev_priv(dev);
	unsigned long flags;
	int nextIn;

	spin_lock_irqsave(&gp->lock, flags);

	nextIn = gp->tx_next_in;

	dbg(3, "%s: nextIn=%d\n", __FUNCTION__, nextIn);
    
	if (gp->tx_count >= TX_RING_SIZE) {
		warn("Tx Ring full, pkt dropped.\n");
		gp->stats.tx_dropped++;
		spin_unlock_irqrestore(&gp->lock, flags);
		return 1;
	}
    
	if (!(gp->last_psr & psrLink)) {
		err("%s: Link down, pkt dropped.\n", __FUNCTION__);
		gp->stats.tx_dropped++;
		spin_unlock_irqrestore(&gp->lock, flags);
		return 1;
	}
    
	if (dma32_to_cpu(gp->tx_ring[nextIn].cmdstat) & txOwn) {
		err("%s: device owns descriptor, pkt dropped.\n", __FUNCTION__);
		gp->stats.tx_dropped++;
		// stop the queue, so Tx timeout can fix it
		netif_stop_queue(dev);
		spin_unlock_irqrestore(&gp->lock, flags);
		return 1;
	}
    
	// Prepare the Descriptor at tx_next_in
	gp->tx_skbuff[nextIn] = skb;
	gp->tx_ring[nextIn].byte_cnt = cpu_to_dma16(skb->len);
	gp->tx_ring[nextIn].buff_ptr = cpu_to_dma32(virt_to_phys(skb->data));
	// make sure packet gets written back to memory
	dma_cache_wback_inv((unsigned long)(skb->data), skb->len);
	// Give ownership to device, set first and last desc, enable interrupt
	// Setting of ownership bit must be *last*!
	gp->tx_ring[nextIn].cmdstat =
		cpu_to_dma32((u32)(txOwn | txGenCRC | txEI |
				   txPad | txFirst | txLast));
    
	dump_tx_desc(4, dev, nextIn);
	dump_skb(4, dev, skb);

	// increment tx_next_in with wrap
	gp->tx_next_in = (nextIn + 1) % TX_RING_SIZE;
	// If DMA is stopped, restart
	if (!(GT96100ETH_READ(gp, GT96100_ETH_PORT_STATUS) & psrTxLow))
		GT96100ETH_WRITE(gp, GT96100_ETH_SDMA_COMM,
				 sdcmrERD | sdcmrTXDL);

	// increment count and stop queue if full
	if (++gp->tx_count == TX_RING_SIZE) {
		gp->tx_full = 1;
		netif_stop_queue(dev);
		dbg(2, "Tx Ring now full, queue stopped.\n");
	}
    
	dev->trans_start = jiffies;
	spin_unlock_irqrestore(&gp->lock, flags);

	return 0;
}


static int
gt96100_rx(struct net_device *dev, u32 status)
{
	struct gt96100_private *gp = netdev_priv(dev);
	struct sk_buff *skb;
	int pkt_len, nextOut, cdp;
	gt96100_rd_t *rd;
	u32 cmdstat;
    
	dbg(3, "%s: dev=%p, status=%x\n", __FUNCTION__, dev, status);

	cdp = (GT96100ETH_READ(gp, GT96100_ETH_1ST_RX_DESC_PTR0)
	       - gp->rx_ring_dma) / sizeof(gt96100_rd_t);

	// Continue until we reach 1st descriptor pointer
	for (nextOut = gp->rx_next_out; nextOut != cdp;
	     nextOut = (nextOut + 1) % RX_RING_SIZE) {
	
		if (--gp->intr_work_done == 0)
			break;

		rd = &gp->rx_ring[nextOut];
		cmdstat = dma32_to_cpu(rd->cmdstat);
	
		dbg(4, "%s: Rx desc cmdstat=%x, nextOut=%d\n", __FUNCTION__,
		    cmdstat, nextOut);

		if (cmdstat & (u32)rxOwn) {
			//err("%s: device owns descriptor!\n", __FUNCTION__);
			// DMA is not finished updating descriptor???
			// Leave and come back later to pick-up where
			// we left off.
			break;
		}

		// Drop this received pkt if there were any errors
		if (((cmdstat & (u32)(rxErrorSummary)) &&
		     (cmdstat & (u32)(rxFirst))) || (status & icrRxError)) {
			// update the detailed rx error counters that
			// are not covered by the MIB counters.
			if (cmdstat & (u32)rxOverrun)
				gp->stats.rx_fifo_errors++;
			cmdstat |= (u32)rxOwn;
			rd->cmdstat = cpu_to_dma32(cmdstat);
			continue;
		}

		/*
		 * Must be first and last (ie only) descriptor of packet. We
		 * ignore (drop) any packets that do not fit in one descriptor.
		 * Every descriptor's receive buffer is large enough to hold
		 * the maximum 802.3 frame size, so a multi-descriptor packet
		 * indicates an error. Most if not all corrupted packets will
		 * have already been dropped by the above check for the
		 * rxErrorSummary status bit.
		 */
		if (!(cmdstat & (u32)rxFirst) || !(cmdstat & (u32)rxLast)) {
			if (cmdstat & (u32)rxFirst) {
				/*
				 * This is the first descriptor of a
				 * multi-descriptor packet. It isn't corrupted
				 * because the above check for rxErrorSummary
				 * would have dropped it already, so what's
				 * the deal with this packet? Good question,
				 * let's dump it out.
				 */
				err("%s: desc not first and last!\n", __FUNCTION__);
				dump_rx_desc(0, dev, nextOut);
			}
			cmdstat |= (u32)rxOwn;
			rd->cmdstat = cpu_to_dma32(cmdstat);
			// continue to drop every descriptor of this packet
			continue;
		}
	
		pkt_len = dma16_to_cpu(rd->byte_cnt);
	
		/* Create new skb. */
		skb = dev_alloc_skb(pkt_len+2);
		if (skb == NULL) {
			err("%s: Memory squeeze, dropping packet.\n", __FUNCTION__);
			gp->stats.rx_dropped++;
			cmdstat |= (u32)rxOwn;
			rd->cmdstat = cpu_to_dma32(cmdstat);
			continue;
		}
		skb->dev = dev;
		skb_reserve(skb, 2);   /* 16 byte IP header align */
		memcpy(skb_put(skb, pkt_len),
		       &gp->rx_buff[nextOut*PKT_BUF_SZ], pkt_len);
		skb->protocol = eth_type_trans(skb, dev);
		dump_skb(4, dev, skb);
	
		netif_rx(skb);        /* pass the packet to upper layers */
		dev->last_rx = jiffies;

		// now we can release ownership of this desc back to device
		cmdstat |= (u32)rxOwn;
		rd->cmdstat = cpu_to_dma32(cmdstat);
	}
    
	if (nextOut == gp->rx_next_out)
		dbg(3, "%s: RxCDP did not increment?\n", __FUNCTION__);

	gp->rx_next_out = nextOut;
	return 0;
}


static void
gt96100_tx_complete(struct net_device *dev, u32 status)
{
	struct gt96100_private *gp = netdev_priv(dev);
	int nextOut, cdp;
	gt96100_td_t *td;
	u32 cmdstat;

	cdp = (GT96100ETH_READ(gp, GT96100_ETH_CURR_TX_DESC_PTR0)
	       - gp->tx_ring_dma) / sizeof(gt96100_td_t);
    
	// Continue until we reach the current descriptor pointer
	for (nextOut = gp->tx_next_out; nextOut != cdp;
	     nextOut = (nextOut + 1) % TX_RING_SIZE) {
	
		if (--gp->intr_work_done == 0)
			break;

		td = &gp->tx_ring[nextOut];
		cmdstat = dma32_to_cpu(td->cmdstat);
	
		dbg(3, "%s: Tx desc cmdstat=%x, nextOut=%d\n", __FUNCTION__,
		    cmdstat, nextOut);
	
		if (cmdstat & (u32)txOwn) {
			/*
			 * DMA is not finished writing descriptor???
			 * Leave and come back later to pick-up where
			 * we left off.
			 */
			break;
		}
	
		// increment Tx error stats
		if (cmdstat & (u32)txErrorSummary) {
			dbg(2, "%s: Tx error, cmdstat = %x\n", __FUNCTION__,
			    cmdstat);
			gp->stats.tx_errors++;
			if (cmdstat & (u32)txReTxLimit)
				gp->stats.tx_aborted_errors++;
			if (cmdstat & (u32)txUnderrun)
				gp->stats.tx_fifo_errors++;
			if (cmdstat & (u32)txLateCollision)
				gp->stats.tx_window_errors++;
		}
	
		if (cmdstat & (u32)txCollision)
			gp->stats.collisions +=
				(u32)((cmdstat & txReTxCntMask) >>
				      txReTxCntBit);

		// Wake the queue if the ring was full
		if (gp->tx_full) {
			gp->tx_full = 0;
			if (gp->last_psr & psrLink) {
				netif_wake_queue(dev);
				dbg(2, "%s: Tx Ring was full, queue waked\n",
				    __FUNCTION__);
			}
		}
	
		// decrement tx ring buffer count
		if (gp->tx_count) gp->tx_count--;
	
		// free the skb
		if (gp->tx_skbuff[nextOut]) {
			dbg(3, "%s: good Tx, skb=%p\n", __FUNCTION__,
			    gp->tx_skbuff[nextOut]);
			dev_kfree_skb_irq(gp->tx_skbuff[nextOut]);
			gp->tx_skbuff[nextOut] = NULL;
		} else {
			err("%s: no skb!\n", __FUNCTION__);
		}
	}

	gp->tx_next_out = nextOut;

	if (gt96100_check_tx_consistent(gp)) {
		err("%s: Tx queue inconsistent!\n", __FUNCTION__);
	}
    
	if ((status & icrTxEndLow) && gp->tx_count != 0) {
		// we must restart the DMA
		dbg(3, "%s: Restarting Tx DMA\n", __FUNCTION__);
		GT96100ETH_WRITE(gp, GT96100_ETH_SDMA_COMM,
				 sdcmrERD | sdcmrTXDL);
	}
}


static irqreturn_t
gt96100_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct net_device *dev = (struct net_device *)dev_id;
	struct gt96100_private *gp = netdev_priv(dev);
	u32 status;
    	int handled = 0;

	if (dev == NULL) {
		err("%s: null dev ptr\n", __FUNCTION__);
		return IRQ_NONE;
	}

	dbg(3, "%s: entry, icr=%x\n", __FUNCTION__,
	    GT96100ETH_READ(gp, GT96100_ETH_INT_CAUSE));

	spin_lock(&gp->lock);

	gp->intr_work_done = max_interrupt_work;

	while (gp->intr_work_done > 0) {

		status = GT96100ETH_READ(gp, GT96100_ETH_INT_CAUSE);
		// ACK interrupts
		GT96100ETH_WRITE(gp, GT96100_ETH_INT_CAUSE, ~status);

		if ((status & icrEtherIntSum) == 0 &&
		    !(status & (icrTxBufferLow|icrTxBufferHigh|icrRxBuffer)))
			break;

		handled = 1;

		if (status & icrMIIPhySTC) {
			u32 psr = GT96100ETH_READ(gp, GT96100_ETH_PORT_STATUS);
			if (gp->last_psr != psr) {
				dbg(0, "port status:\n");
				dbg(0, "    %s MBit/s, %s-duplex, "
				    "flow-control %s, link is %s,\n",
				    psr & psrSpeed ? "100":"10",
				    psr & psrDuplex ? "full":"half",
				    psr & psrFctl ? "disabled":"enabled",
				    psr & psrLink ? "up":"down");
				dbg(0, "    TxLowQ is %s, TxHighQ is %s, "
				    "Transmitter is %s\n",
				    psr & psrTxLow ? "running":"stopped",
				    psr & psrTxHigh ? "running":"stopped",
				    psr & psrTxInProg ? "on":"off");
		
				if ((psr & psrLink) && !gp->tx_full &&
				    netif_queue_stopped(dev)) {
					dbg(0, "%s: Link up, waking queue.\n",
					    __FUNCTION__);
					netif_wake_queue(dev);
				} else if (!(psr & psrLink) &&
					   !netif_queue_stopped(dev)) {
					dbg(0, "%s: Link down, stopping queue.\n",
					    __FUNCTION__);
					netif_stop_queue(dev);
				}

				gp->last_psr = psr;
			}

			if (--gp->intr_work_done == 0)
				break;
		}
	
		if (status & (icrTxBufferLow | icrTxEndLow))
			gt96100_tx_complete(dev, status);

		if (status & (icrRxBuffer | icrRxError)) {
			gt96100_rx(dev, status);
		}
	
		// Now check TX errors (RX errors were handled in gt96100_rx)
		if (status & icrTxErrorLow) {
			err("%s: Tx resource error\n", __FUNCTION__);
			if (--gp->intr_work_done == 0)
				break;
		}
	
		if (status & icrTxUdr) {
			err("%s: Tx underrun error\n", __FUNCTION__);
			if (--gp->intr_work_done == 0)
				break;
		}
	}

	if (gp->intr_work_done == 0) {
		// ACK any remaining pending interrupts
		GT96100ETH_WRITE(gp, GT96100_ETH_INT_CAUSE, 0);
		dbg(3, "%s: hit max work\n", __FUNCTION__);
	}
    
	dbg(3, "%s: exit, icr=%x\n", __FUNCTION__,
	    GT96100ETH_READ(gp, GT96100_ETH_INT_CAUSE));

	spin_unlock(&gp->lock);
	return IRQ_RETVAL(handled);
}


static void
gt96100_tx_timeout(struct net_device *dev)
{
	struct gt96100_private *gp = netdev_priv(dev);
	unsigned long flags;
    
	spin_lock_irqsave(&gp->lock, flags);
    
	if (!(gp->last_psr & psrLink)) {
		err("tx_timeout: link down.\n");
		spin_unlock_irqrestore(&gp->lock, flags);
	} else {
		if (gt96100_check_tx_consistent(gp))
			err("tx_timeout: Tx ring error.\n");

		disable_ether_irq(dev);
		spin_unlock_irqrestore(&gp->lock, flags);
		reset_tx(dev);
		enable_ether_irq(dev);
	
		netif_wake_queue(dev);
	}
}


static void
gt96100_set_rx_mode(struct net_device *dev)
{
	struct gt96100_private *gp = netdev_priv(dev);
	unsigned long flags;
	//struct dev_mc_list *mcptr;
    
	dbg(3, "%s: dev=%p, flags=%x\n", __FUNCTION__, dev, dev->flags);

	// stop the Receiver DMA
	abort(dev, sdcmrAR);

	spin_lock_irqsave(&gp->lock, flags);

	if (dev->flags & IFF_PROMISC) {
		GT96100ETH_WRITE(gp, GT96100_ETH_PORT_CONFIG,
				 pcrEN | pcrHS | pcrPM);
	}

#if 0
	/*
	  FIXME: currently multicast doesn't work - need to get hash table
	  working first.
	*/
	if (dev->mc_count) {
		// clear hash table
		memset(gp->hash_table, 0, RX_HASH_TABLE_SIZE);
		// Add our ethernet address
		gt96100_add_hash_entry(dev, dev->dev_addr);

		for (mcptr = dev->mc_list; mcptr; mcptr = mcptr->next) {
			dump_hw_addr(2, dev, "%s: addr=", __FUNCTION__,
				     mcptr->dmi_addr);
			gt96100_add_hash_entry(dev, mcptr->dmi_addr);
		}
	}
#endif
    
	// restart Rx DMA
	GT96100ETH_WRITE(gp, GT96100_ETH_SDMA_COMM, sdcmrERD);

	spin_unlock_irqrestore(&gp->lock, flags);
}

static struct net_device_stats *
gt96100_get_stats(struct net_device *dev)
{
	struct gt96100_private *gp = netdev_priv(dev);
	unsigned long flags;

	dbg(3, "%s: dev=%p\n", __FUNCTION__, dev);

	if (netif_device_present(dev)) {
		spin_lock_irqsave (&gp->lock, flags);
		update_stats(gp);
		spin_unlock_irqrestore (&gp->lock, flags);
	}

	return &gp->stats;
}

static void gt96100_cleanup_module(void)
{
	int i;
	for (i=0; i<NUM_INTERFACES; i++) {
		struct gt96100_if_t *gtif = &gt96100_iflist[i];
		if (gtif->dev != NULL) {
			struct gt96100_private *gp = (struct gt96100_private *)
				netdev_priv(gtif->dev);
			unregister_netdev(gtif->dev);
			dmafree(RX_HASH_TABLE_SIZE, gp->hash_table_dma);
			dmafree(PKT_BUF_SZ*RX_RING_SIZE, gp->rx_buff);
			dmafree(sizeof(gt96100_rd_t) * RX_RING_SIZE
				+ sizeof(gt96100_td_t) * TX_RING_SIZE,
				gp->rx_ring);
			free_netdev(gtif->dev);
			release_region(gtif->iobase, gp->io_size);
		}
	}
}

static int __init gt96100_setup(char *options)
{
	char *this_opt;

	if (!options || !*options)
		return 0;

	while ((this_opt = strsep (&options, ",")) != NULL) {
		if (!*this_opt)
			continue;
		if (!strncmp(this_opt, "mac0:", 5)) {
			memcpy(mac0, this_opt+5, 17);
			mac0[17]= '\0';
		} else if (!strncmp(this_opt, "mac1:", 5)) {
			memcpy(mac1, this_opt+5, 17);
			mac1[17]= '\0';
		}
	}

	return 1;
}

__setup("gt96100eth=", gt96100_setup);

module_init(gt96100_init_module);
module_exit(gt96100_cleanup_module);

MODULE_AUTHOR("Steve Longerbeam <stevel@mvista.com>");
MODULE_DESCRIPTION("GT96100 Ethernet driver");
