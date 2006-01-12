/* mac8390.c: New driver for 8390-based Nubus (or Nubus-alike)
   Ethernet cards on Linux */
/* Based on the former daynaport.c driver, by Alan Cox.  Some code
   taken from or inspired by skeleton.c by Donald Becker, acenic.c by
   Jes Sorensen, and ne2k-pci.c by Donald Becker and Paul Gortmaker.

   This software may be used and distributed according to the terms of
   the GNU Public License, incorporated herein by reference.  */

/* 2000-02-28: support added for Dayna and Kinetics cards by 
   A.G.deWijn@phys.uu.nl */
/* 2000-04-04: support added for Dayna2 by bart@etpmod.phys.tue.nl */
/* 2001-04-18: support for DaynaPort E/LC-M by rayk@knightsmanor.org */
/* 2001-05-15: support for Cabletron ported from old daynaport driver
 * and fixed access to Sonic Sys card which masquerades as a Farallon 
 * by rayk@knightsmanor.org */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/interrupt.h>
#include <linux/ptrace.h>
#include <linux/ioport.h>
#include <linux/nubus.h>
#include <linux/in.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/bitops.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <asm/hwtest.h>
#include <asm/macints.h>

#include "8390.h"

#define WD_START_PG			0x00	/* First page of TX buffer */
#define CABLETRON_RX_START_PG		0x00    /* First page of RX buffer */
#define CABLETRON_RX_STOP_PG		0x30    /* Last page +1 of RX ring */
#define CABLETRON_TX_START_PG		CABLETRON_RX_STOP_PG  /* First page of TX buffer */

/* Unfortunately it seems we have to hardcode these for the moment */
/* Shouldn't the card know about this? Does anyone know where to read it off the card? Do we trust the data provided by the card? */

#define DAYNA_8390_BASE		0x80000
#define DAYNA_8390_MEM		0x00000

#define KINETICS_8390_BASE	0x80000
#define KINETICS_8390_MEM	0x00000

#define CABLETRON_8390_BASE	0x90000	
#define CABLETRON_8390_MEM	0x00000

enum mac8390_type {
	MAC8390_NONE = -1,
	MAC8390_APPLE,
	MAC8390_ASANTE,
	MAC8390_FARALLON,  /* Apple, Asante, and Farallon are all compatible */
	MAC8390_CABLETRON,
	MAC8390_DAYNA,
	MAC8390_INTERLAN,
	MAC8390_KINETICS,
	MAC8390_FOCUS,
	MAC8390_SONICSYS,
	MAC8390_DAYNA2,
	MAC8390_DAYNA3,
};

static const char * cardname[] = {
	"apple",
	"asante",
	"farallon",
	"cabletron",
	"dayna",
	"interlan",
	"kinetics",
	"focus",
	"sonic systems",
	"dayna2",
	"dayna_lc",
};

static int word16[] = {
	1, /* apple */
	1, /* asante */
	1, /* farallon */
	1, /* cabletron */
	0, /* dayna */
	1, /* interlan */
	0, /* kinetics */
	1, /* focus (??) */
	1, /* sonic systems  */
	1, /* dayna2 */
	1, /* dayna-lc */
};

/* on which cards do we use NuBus resources? */
static int useresources[] = {
	1, /* apple */
	1, /* asante */
	1, /* farallon */
	0, /* cabletron */
	0, /* dayna */
	0, /* interlan */
	0, /* kinetics */
	0, /* focus (??) */
	1, /* sonic systems */
	1, /* dayna2 */
	1, /* dayna-lc */
};

static char version[] __initdata =
	"mac8390.c: v0.4 2001-05-15 David Huggins-Daines <dhd@debian.org> and others\n";
		
extern enum mac8390_type mac8390_ident(struct nubus_dev * dev);
extern int mac8390_memsize(unsigned long membase);
extern int mac8390_memtest(struct net_device * dev);
static int mac8390_initdev(struct net_device * dev, struct nubus_dev * ndev,
			   enum mac8390_type type);

static int mac8390_open(struct net_device * dev);
static int mac8390_close(struct net_device * dev);
static void mac8390_no_reset(struct net_device *dev);

/* Sane (32-bit chunk memory read/write) - Apple/Asante/Farallon do this*/
static void sane_get_8390_hdr(struct net_device *dev,
			      struct e8390_pkt_hdr *hdr, int ring_page);
static void sane_block_input(struct net_device * dev, int count,
			     struct sk_buff * skb, int ring_offset);
static void sane_block_output(struct net_device * dev, int count,
			      const unsigned char * buf, const int start_page);

/* dayna_memcpy to and from card */
static void dayna_memcpy_fromcard(struct net_device *dev, void *to,
				int from, int count);
static void dayna_memcpy_tocard(struct net_device *dev, int to,
			      const void *from, int count);

/* Dayna - Dayna/Kinetics use this */
static void dayna_get_8390_hdr(struct net_device *dev,
			       struct e8390_pkt_hdr *hdr, int ring_page);
static void dayna_block_input(struct net_device *dev, int count,
			      struct sk_buff *skb, int ring_offset);
static void dayna_block_output(struct net_device *dev, int count,
			       const unsigned char *buf, int start_page);

#define memcpy_fromio(a,b,c)	memcpy((a),(void *)(b),(c))
#define memcpy_toio(a,b,c)	memcpy((void *)(a),(b),(c))

/* Slow Sane (16-bit chunk memory read/write) Cabletron uses this */
static void slow_sane_get_8390_hdr(struct net_device *dev,
				   struct e8390_pkt_hdr *hdr, int ring_page);
static void slow_sane_block_input(struct net_device *dev, int count,
				  struct sk_buff *skb, int ring_offset);
static void slow_sane_block_output(struct net_device *dev, int count,
				   const unsigned char *buf, int start_page);
static void word_memcpy_tocard(void *tp, const void *fp, int count);
static void word_memcpy_fromcard(void *tp, const void *fp, int count);

enum mac8390_type __init mac8390_ident(struct nubus_dev * dev)
{
	if (dev->dr_sw == NUBUS_DRSW_ASANTE)
		return MAC8390_ASANTE;
	if (dev->dr_sw == NUBUS_DRSW_FARALLON) 
		return MAC8390_FARALLON;
	if (dev->dr_sw == NUBUS_DRSW_KINETICS)
		return MAC8390_KINETICS;
	if (dev->dr_sw == NUBUS_DRSW_DAYNA)
		return MAC8390_DAYNA;
	if (dev->dr_sw == NUBUS_DRSW_DAYNA2)
		return MAC8390_DAYNA2;
	if (dev->dr_sw == NUBUS_DRSW_DAYNA_LC)
		return MAC8390_DAYNA3;
	if (dev->dr_hw == NUBUS_DRHW_CABLETRON)
		return MAC8390_CABLETRON;
	return MAC8390_NONE;
}

int __init mac8390_memsize(unsigned long membase)
{
	unsigned long flags;
	int i, j;
	
	local_irq_save(flags);
	/* Check up to 32K in 4K increments */
	for (i = 0; i < 8; i++) {
		volatile unsigned short *m = (unsigned short *) (membase + (i * 0x1000));

		/* Unwriteable - we have a fully decoded card and the
		   RAM end located */
		if (hwreg_present(m) == 0)
			break;
		
		/* write a distinctive byte */
		*m = 0xA5A0 | i;
		/* check that we read back what we wrote */
		if (*m != (0xA5A0 | i))
			break;

		/* check for partial decode and wrap */
		for (j = 0; j < i; j++) {
			volatile unsigned short *p = (unsigned short *) (membase + (j * 0x1000));
			if (*p != (0xA5A0 | j))
				break;
 		}
 	}
	local_irq_restore(flags);
	/* in any case, we stopped once we tried one block too many,
           or once we reached 32K */
 	return i * 0x1000;
}

struct net_device * __init mac8390_probe(int unit)
{
	struct net_device *dev;
	volatile unsigned short *i;
	int version_disp = 0;
	struct nubus_dev * ndev = NULL;
	int err = -ENODEV;
	
	struct nubus_dir dir;
	struct nubus_dirent ent;
	int offset;
	static unsigned int slots;

	enum mac8390_type cardtype;

	/* probably should check for Nubus instead */

	if (!MACH_IS_MAC)
		return ERR_PTR(-ENODEV);

	dev = alloc_ei_netdev();
	if (!dev)
		return ERR_PTR(-ENOMEM);

	if (unit >= 0)
		sprintf(dev->name, "eth%d", unit);

 	SET_MODULE_OWNER(dev);

	while ((ndev = nubus_find_type(NUBUS_CAT_NETWORK, NUBUS_TYPE_ETHERNET, ndev))) {
		/* Have we seen it already? */
		if (slots & (1<<ndev->board->slot))
			continue;
		slots |= 1<<ndev->board->slot;

		if ((cardtype = mac8390_ident(ndev)) == MAC8390_NONE)
			continue;

		if (version_disp == 0) {
			version_disp = 1;
			printk(version);
		}

		dev->irq = SLOT2IRQ(ndev->board->slot);
		/* This is getting to be a habit */
		dev->base_addr = ndev->board->slot_addr | ((ndev->board->slot&0xf) << 20);

		/* Get some Nubus info - we will trust the card's idea
		   of where its memory and registers are. */

		if (nubus_get_func_dir(ndev, &dir) == -1) {
			printk(KERN_ERR "%s: Unable to get Nubus functional"
					" directory for slot %X!\n",
			       dev->name, ndev->board->slot);
			continue;
		}
		
		/* Get the MAC address */
		if ((nubus_find_rsrc(&dir, NUBUS_RESID_MAC_ADDRESS, &ent)) == -1) {
			printk(KERN_INFO "%s: Couldn't get MAC address!\n",
					dev->name);
			continue;
		} else {
			nubus_get_rsrc_mem(dev->dev_addr, &ent, 6);
			/* Some Sonic Sys cards masquerade as Farallon */
			if (cardtype == MAC8390_FARALLON && 
					dev->dev_addr[0] == 0x0 &&
					dev->dev_addr[1] == 0x40 &&
					dev->dev_addr[2] == 0x10) {
				/* This is really Sonic Sys card */
				cardtype = MAC8390_SONICSYS;
			}
		}
		
		if (useresources[cardtype] == 1) {
			nubus_rewinddir(&dir);
			if (nubus_find_rsrc(&dir, NUBUS_RESID_MINOR_BASEOS, &ent) == -1) {
				printk(KERN_ERR "%s: Memory offset resource"
						" for slot %X not found!\n",
				       dev->name, ndev->board->slot);
				continue;
			}
			nubus_get_rsrc_mem(&offset, &ent, 4);
			dev->mem_start = dev->base_addr + offset;
			/* yes, this is how the Apple driver does it */
			dev->base_addr = dev->mem_start + 0x10000;
			nubus_rewinddir(&dir);
			if (nubus_find_rsrc(&dir, NUBUS_RESID_MINOR_LENGTH, &ent) == -1) {
				printk(KERN_INFO "%s: Memory length resource"
						 " for slot %X not found"
						 ", probing\n",
				       dev->name, ndev->board->slot);
				offset = mac8390_memsize(dev->mem_start);
				} else {
					nubus_get_rsrc_mem(&offset, &ent, 4);
				}
			dev->mem_end = dev->mem_start + offset;
		} else {
			switch (cardtype) {
				case MAC8390_KINETICS:
				case MAC8390_DAYNA: /* it's the same */
					dev->base_addr = 
						(int)(ndev->board->slot_addr +
						DAYNA_8390_BASE);
					dev->mem_start = 
						(int)(ndev->board->slot_addr +
						DAYNA_8390_MEM);
					dev->mem_end =
						dev->mem_start +
						mac8390_memsize(dev->mem_start);
					break;
				case MAC8390_CABLETRON:
					dev->base_addr =
						(int)(ndev->board->slot_addr +
						CABLETRON_8390_BASE);
					dev->mem_start =
						(int)(ndev->board->slot_addr +
						CABLETRON_8390_MEM);
					/* The base address is unreadable if 0x00
					 * has been written to the command register
					 * Reset the chip by writing E8390_NODMA +
					 *   E8390_PAGE0 + E8390_STOP just to be
					 *   sure
					 */
					i = (void *)dev->base_addr;
					*i = 0x21;
					dev->mem_end = 
						dev->mem_start +
						mac8390_memsize(dev->mem_start);
					break;
					
				default:
					printk(KERN_ERR "Card type %s is"
							" unsupported, sorry\n",
					       cardname[cardtype]);
					continue;
			}
		}

		/* Do the nasty 8390 stuff */
		if (!mac8390_initdev(dev, ndev, cardtype))
			break;
	}

	if (!ndev)
		goto out;
	err = register_netdev(dev);
	if (err)
		goto out;
	return dev;

out:
	free_netdev(dev);
	return ERR_PTR(err);
}

#ifdef MODULE
MODULE_AUTHOR("David Huggins-Daines <dhd@debian.org> and others");
MODULE_DESCRIPTION("Macintosh NS8390-based Nubus Ethernet driver");
MODULE_LICENSE("GPL");

/* overkill, of course */
static struct net_device *dev_mac8390[15];
int init_module(void)
{
	int i;
	for (i = 0; i < 15; i++) {
		struct net_device *dev = mac8390_probe(-1);
		if (IS_ERR(dev))
			break;
		dev_mac890[i] = dev;
	}
	if (!i) {
		printk(KERN_NOTICE "mac8390.c: No useable cards found, driver NOT installed.\n");
		return -ENODEV;
	}
	return 0;
}

void cleanup_module(void)
{
	int i;
	for (i = 0; i < 15; i++) {
		struct net_device *dev = dev_mac890[i];
		if (dev) {
			unregister_netdev(dev);
			free_netdev(dev);
		}
	}
}

#endif /* MODULE */

static int __init mac8390_initdev(struct net_device * dev, struct nubus_dev * ndev,
			    enum mac8390_type type)
{
	static u32 fwrd4_offsets[16]={
		0,      4,      8,      12,
		16,     20,     24,     28,
		32,     36,     40,     44,
		48,     52,     56,     60
	};
	static u32 back4_offsets[16]={
		60,     56,     52,     48,
		44,     40,     36,     32,
		28,     24,     20,     16,
		12,     8,      4,      0
	};
	static u32 fwrd2_offsets[16]={
		0,      2,      4,      6,
		8,     10,     12,     14,
		16,    18,     20,     22,
		24,    26,     28,     30
	};

	int access_bitmode;
	
	/* Now fill in our stuff */
	dev->open = &mac8390_open;
	dev->stop = &mac8390_close;
#ifdef CONFIG_NET_POLL_CONTROLLER
	dev->poll_controller = ei_poll;
#endif

	/* GAR, ei_status is actually a macro even though it looks global */
	ei_status.name = cardname[type];
	ei_status.word16 = word16[type];

	/* Cabletron's TX/RX buffers are backwards */
	if (type == MAC8390_CABLETRON) {
               ei_status.tx_start_page = CABLETRON_TX_START_PG;
               ei_status.rx_start_page = CABLETRON_RX_START_PG;
               ei_status.stop_page = CABLETRON_RX_STOP_PG;
               ei_status.rmem_start = dev->mem_start;
               ei_status.rmem_end = dev->mem_start + CABLETRON_RX_STOP_PG*256;
	} else {
               ei_status.tx_start_page = WD_START_PG;
               ei_status.rx_start_page = WD_START_PG + TX_PAGES;
               ei_status.stop_page = (dev->mem_end - dev->mem_start)/256;
               ei_status.rmem_start = dev->mem_start + TX_PAGES*256;
               ei_status.rmem_end = dev->mem_end;
	}
	
	/* Fill in model-specific information and functions */
	switch(type) {
	case MAC8390_SONICSYS:
		/* 16 bit card, register map is reversed */
		ei_status.reset_8390 = &mac8390_no_reset;
		ei_status.block_input = &slow_sane_block_input;
		ei_status.block_output = &slow_sane_block_output;
		ei_status.get_8390_hdr = &slow_sane_get_8390_hdr;
		ei_status.reg_offset = back4_offsets;
		access_bitmode = 0;
		break;
	case MAC8390_FARALLON:
	case MAC8390_APPLE:
	case MAC8390_ASANTE:
	case MAC8390_DAYNA2:
	case MAC8390_DAYNA3:
		/* 32 bit card, register map is reversed */
		/* sane */
		ei_status.reset_8390 = &mac8390_no_reset;
		ei_status.block_input = &sane_block_input;
		ei_status.block_output = &sane_block_output;
		ei_status.get_8390_hdr = &sane_get_8390_hdr;
		ei_status.reg_offset = back4_offsets;
		access_bitmode = 1;
		break;
	case MAC8390_CABLETRON:
		/* 16 bit card, register map is short forward */
		ei_status.reset_8390 = &mac8390_no_reset;
		ei_status.block_input = &slow_sane_block_input;
		ei_status.block_output = &slow_sane_block_output;
		ei_status.get_8390_hdr = &slow_sane_get_8390_hdr;
		ei_status.reg_offset = fwrd2_offsets;
		access_bitmode = 0;
		break;
	case MAC8390_DAYNA:
	case MAC8390_KINETICS:
		/* 16 bit memory */
		/* dayna and similar */
		ei_status.reset_8390 = &mac8390_no_reset;
		ei_status.block_input = &dayna_block_input;
		ei_status.block_output = &dayna_block_output;
		ei_status.get_8390_hdr = &dayna_get_8390_hdr;
		ei_status.reg_offset = fwrd4_offsets;
		access_bitmode = 0;
		break;
	default:
		printk(KERN_ERR "Card type %s is unsupported, sorry\n", cardname[type]);
		return -ENODEV;
	}
		
	NS8390_init(dev, 0);

	/* Good, done, now spit out some messages */
	printk(KERN_INFO "%s: %s in slot %X (type %s)\n",
		   dev->name, ndev->board->name, ndev->board->slot, cardname[type]);
	printk(KERN_INFO "MAC ");
	{
		int i;
		for (i = 0; i < 6; i++) {
			printk("%2.2x", dev->dev_addr[i]);
			if (i < 5)
				printk(":");
		}
	}
	printk(" IRQ %d, shared memory at %#lx-%#lx,  %d-bit access.\n",
		   dev->irq, dev->mem_start, dev->mem_end-1, 
		   access_bitmode?32:16);
	return 0;
}

static int mac8390_open(struct net_device *dev)
{
	ei_open(dev);
	if (request_irq(dev->irq, ei_interrupt, 0, "8390 Ethernet", dev)) {
		printk ("%s: unable to get IRQ %d.\n", dev->name, dev->irq);
		return -EAGAIN;
	}	
	return 0;
}

static int mac8390_close(struct net_device *dev)
{
	free_irq(dev->irq, dev);
	ei_close(dev);
	return 0;
}

static void mac8390_no_reset(struct net_device *dev)
{
	ei_status.txing = 0;
	if (ei_debug > 1)
		printk("reset not supported\n");
	return;
}

/* dayna_memcpy_fromio/dayna_memcpy_toio */
/* directly from daynaport.c by Alan Cox */
static void dayna_memcpy_fromcard(struct net_device *dev, void *to, int from, int count)
{
	volatile unsigned char *ptr;
	unsigned char *target=to;
	from<<=1;	/* word, skip overhead */
	ptr=(unsigned char *)(dev->mem_start+from);
	/* Leading byte? */
	if (from&2) {
		*target++ = ptr[-1];
		ptr += 2;
		count--;
	}
	while(count>=2)
	{
		*(unsigned short *)target = *(unsigned short volatile *)ptr;
		ptr += 4;			/* skip cruft */
		target += 2;
		count-=2;
	}
	/* Trailing byte? */
	if(count)
		*target = *ptr;
}

static void dayna_memcpy_tocard(struct net_device *dev, int to, const void *from, int count)
{
	volatile unsigned short *ptr;
	const unsigned char *src=from;
	to<<=1;	/* word, skip overhead */
	ptr=(unsigned short *)(dev->mem_start+to);
	/* Leading byte? */
	if (to&2) { /* avoid a byte write (stomps on other data) */
		ptr[-1] = (ptr[-1]&0xFF00)|*src++;
		ptr++;
		count--;
	}
	while(count>=2)
	{
		*ptr++=*(unsigned short *)src;		/* Copy and */
		ptr++;			/* skip cruft */
		src += 2;
		count-=2;
	}
	/* Trailing byte? */
	if(count)
	{
		/* card doesn't like byte writes */
		*ptr=(*ptr&0x00FF)|(*src << 8);
	}
}

/* sane block input/output */
static void sane_get_8390_hdr(struct net_device *dev,
			      struct e8390_pkt_hdr *hdr, int ring_page)
{
	unsigned long hdr_start = (ring_page - WD_START_PG)<<8;
	memcpy_fromio((void *)hdr, (char *)dev->mem_start + hdr_start, 4);
	/* Fix endianness */
	hdr->count = swab16(hdr->count);
}

static void sane_block_input(struct net_device *dev, int count,
			     struct sk_buff *skb, int ring_offset)
{
	unsigned long xfer_base = ring_offset - (WD_START_PG<<8);
	unsigned long xfer_start = xfer_base + dev->mem_start;

	if (xfer_start + count > ei_status.rmem_end) {
		/* We must wrap the input move. */
		int semi_count = ei_status.rmem_end - xfer_start;
		memcpy_fromio(skb->data, (char *)dev->mem_start + xfer_base, semi_count);
		count -= semi_count;
		memcpy_toio(skb->data + semi_count, (char *)ei_status.rmem_start, count);
	} else {
		memcpy_fromio(skb->data, (char *)dev->mem_start + xfer_base, count);
	}
}

static void sane_block_output(struct net_device *dev, int count,
			      const unsigned char *buf, int start_page)
{
	long shmem = (start_page - WD_START_PG)<<8;
	
	memcpy_toio((char *)dev->mem_start + shmem, buf, count);
}

/* dayna block input/output */
static void dayna_get_8390_hdr(struct net_device *dev, struct e8390_pkt_hdr *hdr, int ring_page)
{
	unsigned long hdr_start = (ring_page - WD_START_PG)<<8;

	dayna_memcpy_fromcard(dev, (void *)hdr, hdr_start, 4);
	/* Fix endianness */
	hdr->count=(hdr->count&0xFF)<<8|(hdr->count>>8);
}

static void dayna_block_input(struct net_device *dev, int count, struct sk_buff *skb, int ring_offset)
{
	unsigned long xfer_base = ring_offset - (WD_START_PG<<8);
	unsigned long xfer_start = xfer_base+dev->mem_start;

	/* Note the offset math is done in card memory space which is word
	   per long onto our space. */

	if (xfer_start + count > ei_status.rmem_end)
	{
		/* We must wrap the input move. */
		int semi_count = ei_status.rmem_end - xfer_start;
		dayna_memcpy_fromcard(dev, skb->data, xfer_base, semi_count);
		count -= semi_count;
		dayna_memcpy_fromcard(dev, skb->data + semi_count,
				      ei_status.rmem_start - dev->mem_start,
				      count);
	}
	else
	{
		dayna_memcpy_fromcard(dev, skb->data, xfer_base, count);
	}
}

static void dayna_block_output(struct net_device *dev, int count, const unsigned char *buf,
				int start_page)
{
	long shmem = (start_page - WD_START_PG)<<8;
	
	dayna_memcpy_tocard(dev, shmem, buf, count);
}

/* Cabletron block I/O */
static void slow_sane_get_8390_hdr(struct net_device *dev, struct e8390_pkt_hdr *hdr, 
	int ring_page)
{
	unsigned long hdr_start = (ring_page - WD_START_PG)<<8;
	word_memcpy_fromcard((void *)hdr, (char *)dev->mem_start+hdr_start, 4);
	/* Register endianism - fix here rather than 8390.c */
	hdr->count = (hdr->count&0xFF)<<8|(hdr->count>>8);
}

static void slow_sane_block_input(struct net_device *dev, int count, struct sk_buff *skb,
	int ring_offset)
{
	unsigned long xfer_base = ring_offset - (WD_START_PG<<8);
	unsigned long xfer_start = xfer_base+dev->mem_start;

	if (xfer_start + count > ei_status.rmem_end)
	{
		/* We must wrap the input move. */
		int semi_count = ei_status.rmem_end - xfer_start;
		word_memcpy_fromcard(skb->data, (char *)dev->mem_start +
			xfer_base, semi_count);
		count -= semi_count;
		word_memcpy_fromcard(skb->data + semi_count,
				     (char *)ei_status.rmem_start, count);
	}
	else
	{
		word_memcpy_fromcard(skb->data, (char *)dev->mem_start +
			xfer_base, count);
	}
}

static void slow_sane_block_output(struct net_device *dev, int count, const unsigned char *buf,
	int start_page)
{
	long shmem = (start_page - WD_START_PG)<<8;

	word_memcpy_tocard((char *)dev->mem_start + shmem, buf, count);
}

static void word_memcpy_tocard(void *tp, const void *fp, int count)
{
	volatile unsigned short *to = tp;
	const unsigned short *from = fp;

	count++;
	count/=2;

	while(count--)
		*to++=*from++;
}

static void word_memcpy_fromcard(void *tp, const void *fp, int count)
{
	unsigned short *to = tp;
	const volatile unsigned short *from = fp;

	count++;
	count/=2;

	while(count--)
		*to++=*from++;
}

	
