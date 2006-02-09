/*
 * mm.c - Micro Memory(tm) PCI memory board block device driver - v2.3
 *
 * (C) 2001 San Mehat <nettwerk@valinux.com>
 * (C) 2001 Johannes Erdfelt <jerdfelt@valinux.com>
 * (C) 2001 NeilBrown <neilb@cse.unsw.edu.au>
 *
 * This driver for the Micro Memory PCI Memory Module with Battery Backup
 * is Copyright Micro Memory Inc 2001-2002.  All rights reserved.
 *
 * This driver is released to the public under the terms of the
 *  GNU GENERAL PUBLIC LICENSE version 2
 * See the file COPYING for details.
 *
 * This driver provides a standard block device interface for Micro Memory(tm)
 * PCI based RAM boards.
 * 10/05/01: Phap Nguyen - Rebuilt the driver
 * 10/22/01: Phap Nguyen - v2.1 Added disk partitioning
 * 29oct2001:NeilBrown   - Use make_request_fn instead of request_fn
 *                       - use stand disk partitioning (so fdisk works).
 * 08nov2001:NeilBrown	 - change driver name from "mm" to "umem"
 *			 - incorporate into main kernel
 * 08apr2002:NeilBrown   - Move some of interrupt handle to tasklet
 *			 - use spin_lock_bh instead of _irq
 *			 - Never block on make_request.  queue
 *			   bh's instead.
 *			 - unregister umem from devfs at mod unload
 *			 - Change version to 2.3
 * 07Nov2001:Phap Nguyen - Select pci read command: 06, 12, 15 (Decimal)
 * 07Jan2002: P. Nguyen  - Used PCI Memory Write & Invalidate for DMA
 * 15May2002:NeilBrown   - convert to bio for 2.5
 * 17May2002:NeilBrown   - remove init_mem initialisation.  Instead detect
 *			 - a sequence of writes that cover the card, and
 *			 - set initialised bit then.
 */

//#define DEBUG /* uncomment if you want debugging info (pr_debug) */
#include <linux/config.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/bio.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/ioctl.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/smp_lock.h>
#include <linux/timer.h>
#include <linux/pci.h>
#include <linux/slab.h>

#include <linux/fcntl.h>        /* O_ACCMODE */
#include <linux/hdreg.h>  /* HDIO_GETGEO */

#include <linux/umem.h>

#include <asm/uaccess.h>
#include <asm/io.h>

#define MM_MAXCARDS 4
#define MM_RAHEAD 2      /* two sectors */
#define MM_BLKSIZE 1024  /* 1k blocks */
#define MM_HARDSECT 512  /* 512-byte hardware sectors */
#define MM_SHIFT 6       /* max 64 partitions on 4 cards  */

/*
 * Version Information
 */

#define DRIVER_VERSION "v2.3"
#define DRIVER_AUTHOR "San Mehat, Johannes Erdfelt, NeilBrown"
#define DRIVER_DESC "Micro Memory(tm) PCI memory board block driver"

static int debug;
/* #define HW_TRACE(x)     writeb(x,cards[0].csr_remap + MEMCTRLSTATUS_MAGIC) */
#define HW_TRACE(x)

#define DEBUG_LED_ON_TRANSFER	0x01
#define DEBUG_BATTERY_POLLING	0x02

module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Debug bitmask");

static int pci_read_cmd = 0x0C;		/* Read Multiple */
module_param(pci_read_cmd, int, 0);
MODULE_PARM_DESC(pci_read_cmd, "PCI read command");

static int pci_write_cmd = 0x0F;	/* Write and Invalidate */
module_param(pci_write_cmd, int, 0);
MODULE_PARM_DESC(pci_write_cmd, "PCI write command");

static int pci_cmds;

static int major_nr;

#include <linux/blkdev.h>
#include <linux/blkpg.h>

struct cardinfo {
	int		card_number;
	struct pci_dev	*dev;

	int		irq;

	unsigned long	csr_base;
	unsigned char	__iomem *csr_remap;
	unsigned long	csr_len;
#ifdef CONFIG_MM_MAP_MEMORY
	unsigned long	mem_base;
	unsigned char	__iomem *mem_remap;
	unsigned long	mem_len;
#endif

	unsigned int	win_size; /* PCI window size */
	unsigned int	mm_size;  /* size in kbytes */

	unsigned int	init_size; /* initial segment, in sectors,
				    * that we know to
				    * have been written
				    */
	struct bio	*bio, *currentbio, **biotail;

	request_queue_t *queue;

	struct mm_page {
		dma_addr_t		page_dma;
		struct mm_dma_desc	*desc;
		int	 		cnt, headcnt;
		struct bio		*bio, **biotail;
	} mm_pages[2];
#define DESC_PER_PAGE ((PAGE_SIZE*2)/sizeof(struct mm_dma_desc))

	int  Active, Ready;

	struct tasklet_struct	tasklet;
	unsigned int dma_status;

	struct {
		int		good;
		int		warned;
		unsigned long	last_change;
	} battery[2];

	spinlock_t 	lock;
	int		check_batteries;

	int		flags;
};

static struct cardinfo cards[MM_MAXCARDS];
static struct block_device_operations mm_fops;
static struct timer_list battery_timer;

static int num_cards = 0;

static struct gendisk *mm_gendisk[MM_MAXCARDS];

static void check_batteries(struct cardinfo *card);

/*
-----------------------------------------------------------------------------------
--                           get_userbit
-----------------------------------------------------------------------------------
*/
static int get_userbit(struct cardinfo *card, int bit)
{
	unsigned char led;

	led = readb(card->csr_remap + MEMCTRLCMD_LEDCTRL);
	return led & bit;
}
/*
-----------------------------------------------------------------------------------
--                            set_userbit
-----------------------------------------------------------------------------------
*/
static int set_userbit(struct cardinfo *card, int bit, unsigned char state)
{
	unsigned char led;

	led = readb(card->csr_remap + MEMCTRLCMD_LEDCTRL);
	if (state)
		led |= bit;
	else
		led &= ~bit;
	writeb(led, card->csr_remap + MEMCTRLCMD_LEDCTRL);

	return 0;
}
/*
-----------------------------------------------------------------------------------
--                             set_led
-----------------------------------------------------------------------------------
*/
/*
 * NOTE: For the power LED, use the LED_POWER_* macros since they differ
 */
static void set_led(struct cardinfo *card, int shift, unsigned char state)
{
	unsigned char led;

	led = readb(card->csr_remap + MEMCTRLCMD_LEDCTRL);
	if (state == LED_FLIP)
		led ^= (1<<shift);
	else {
		led &= ~(0x03 << shift);
		led |= (state << shift);
	}
	writeb(led, card->csr_remap + MEMCTRLCMD_LEDCTRL);

}

#ifdef MM_DIAG
/*
-----------------------------------------------------------------------------------
--                              dump_regs
-----------------------------------------------------------------------------------
*/
static void dump_regs(struct cardinfo *card)
{
	unsigned char *p;
	int i, i1;

	p = card->csr_remap;
	for (i = 0; i < 8; i++) {
		printk(KERN_DEBUG "%p   ", p);

		for (i1 = 0; i1 < 16; i1++)
			printk("%02x ", *p++);

		printk("\n");
	}
}
#endif
/*
-----------------------------------------------------------------------------------
--                            dump_dmastat
-----------------------------------------------------------------------------------
*/
static void dump_dmastat(struct cardinfo *card, unsigned int dmastat)
{
	printk(KERN_DEBUG "MM%d*: DMAstat - ", card->card_number);
	if (dmastat & DMASCR_ANY_ERR)
		printk("ANY_ERR ");
	if (dmastat & DMASCR_MBE_ERR)
		printk("MBE_ERR ");
	if (dmastat & DMASCR_PARITY_ERR_REP)
		printk("PARITY_ERR_REP ");
	if (dmastat & DMASCR_PARITY_ERR_DET)
		printk("PARITY_ERR_DET ");
	if (dmastat & DMASCR_SYSTEM_ERR_SIG)
		printk("SYSTEM_ERR_SIG ");
	if (dmastat & DMASCR_TARGET_ABT)
		printk("TARGET_ABT ");
	if (dmastat & DMASCR_MASTER_ABT)
		printk("MASTER_ABT ");
	if (dmastat & DMASCR_CHAIN_COMPLETE)
		printk("CHAIN_COMPLETE ");
	if (dmastat & DMASCR_DMA_COMPLETE)
		printk("DMA_COMPLETE ");
	printk("\n");
}

/*
 * Theory of request handling
 *
 * Each bio is assigned to one mm_dma_desc - which may not be enough FIXME
 * We have two pages of mm_dma_desc, holding about 64 descriptors
 * each.  These are allocated at init time.
 * One page is "Ready" and is either full, or can have request added.
 * The other page might be "Active", which DMA is happening on it.
 *
 * Whenever IO on the active page completes, the Ready page is activated
 * and the ex-Active page is clean out and made Ready.
 * Otherwise the Ready page is only activated when it becomes full, or
 * when mm_unplug_device is called via the unplug_io_fn.
 *
 * If a request arrives while both pages a full, it is queued, and b_rdev is
 * overloaded to record whether it was a read or a write.
 *
 * The interrupt handler only polls the device to clear the interrupt.
 * The processing of the result is done in a tasklet.
 */

static void mm_start_io(struct cardinfo *card)
{
	/* we have the lock, we know there is
	 * no IO active, and we know that card->Active
	 * is set
	 */
	struct mm_dma_desc *desc;
	struct mm_page *page;
	int offset;

	/* make the last descriptor end the chain */
	page = &card->mm_pages[card->Active];
	pr_debug("start_io: %d %d->%d\n", card->Active, page->headcnt, page->cnt-1);
	desc = &page->desc[page->cnt-1];

	desc->control_bits |= cpu_to_le32(DMASCR_CHAIN_COMP_EN);
	desc->control_bits &= ~cpu_to_le32(DMASCR_CHAIN_EN);
	desc->sem_control_bits = desc->control_bits;

			       
	if (debug & DEBUG_LED_ON_TRANSFER)
		set_led(card, LED_REMOVE, LED_ON);

	desc = &page->desc[page->headcnt];
	writel(0, card->csr_remap + DMA_PCI_ADDR);
	writel(0, card->csr_remap + DMA_PCI_ADDR + 4);

	writel(0, card->csr_remap + DMA_LOCAL_ADDR);
	writel(0, card->csr_remap + DMA_LOCAL_ADDR + 4);

	writel(0, card->csr_remap + DMA_TRANSFER_SIZE);
	writel(0, card->csr_remap + DMA_TRANSFER_SIZE + 4);

	writel(0, card->csr_remap + DMA_SEMAPHORE_ADDR);
	writel(0, card->csr_remap + DMA_SEMAPHORE_ADDR + 4);

	offset = ((char*)desc) - ((char*)page->desc);
	writel(cpu_to_le32((page->page_dma+offset)&0xffffffff),
	       card->csr_remap + DMA_DESCRIPTOR_ADDR);
	/* Force the value to u64 before shifting otherwise >> 32 is undefined C
	 * and on some ports will do nothing ! */
	writel(cpu_to_le32(((u64)page->page_dma)>>32),
	       card->csr_remap + DMA_DESCRIPTOR_ADDR + 4);

	/* Go, go, go */
	writel(cpu_to_le32(DMASCR_GO | DMASCR_CHAIN_EN | pci_cmds),
	       card->csr_remap + DMA_STATUS_CTRL);
}

static int add_bio(struct cardinfo *card);

static void activate(struct cardinfo *card)
{
	/* if No page is Active, and Ready is 
	 * not empty, then switch Ready page
	 * to active and start IO.
	 * Then add any bh's that are available to Ready
	 */

	do {
		while (add_bio(card))
			;

		if (card->Active == -1 &&
		    card->mm_pages[card->Ready].cnt > 0) {
			card->Active = card->Ready;
			card->Ready = 1-card->Ready;
			mm_start_io(card);
		}

	} while (card->Active == -1 && add_bio(card));
}

static inline void reset_page(struct mm_page *page)
{
	page->cnt = 0;
	page->headcnt = 0;
	page->bio = NULL;
	page->biotail = & page->bio;
}

static void mm_unplug_device(request_queue_t *q)
{
	struct cardinfo *card = q->queuedata;
	unsigned long flags;

	spin_lock_irqsave(&card->lock, flags);
	if (blk_remove_plug(q))
		activate(card);
	spin_unlock_irqrestore(&card->lock, flags);
}

/* 
 * If there is room on Ready page, take
 * one bh off list and add it.
 * return 1 if there was room, else 0.
 */
static int add_bio(struct cardinfo *card)
{
	struct mm_page *p;
	struct mm_dma_desc *desc;
	dma_addr_t dma_handle;
	int offset;
	struct bio *bio;
	int rw;
	int len;

	bio = card->currentbio;
	if (!bio && card->bio) {
		card->currentbio = card->bio;
		card->bio = card->bio->bi_next;
		if (card->bio == NULL)
			card->biotail = &card->bio;
		card->currentbio->bi_next = NULL;
		return 1;
	}
	if (!bio)
		return 0;

	rw = bio_rw(bio);
	if (card->mm_pages[card->Ready].cnt >= DESC_PER_PAGE)
		return 0;

	len = bio_iovec(bio)->bv_len;
	dma_handle = pci_map_page(card->dev, 
				  bio_page(bio),
				  bio_offset(bio),
				  len,
				  (rw==READ) ?
				  PCI_DMA_FROMDEVICE : PCI_DMA_TODEVICE);

	p = &card->mm_pages[card->Ready];
	desc = &p->desc[p->cnt];
	p->cnt++;
	if ((p->biotail) != &bio->bi_next) {
		*(p->biotail) = bio;
		p->biotail = &(bio->bi_next);
		bio->bi_next = NULL;
	}

	desc->data_dma_handle = dma_handle;

	desc->pci_addr = cpu_to_le64((u64)desc->data_dma_handle);
	desc->local_addr= cpu_to_le64(bio->bi_sector << 9);
	desc->transfer_size = cpu_to_le32(len);
	offset = ( ((char*)&desc->sem_control_bits) - ((char*)p->desc));
	desc->sem_addr = cpu_to_le64((u64)(p->page_dma+offset));
	desc->zero1 = desc->zero2 = 0;
	offset = ( ((char*)(desc+1)) - ((char*)p->desc));
	desc->next_desc_addr = cpu_to_le64(p->page_dma+offset);
	desc->control_bits = cpu_to_le32(DMASCR_GO|DMASCR_ERR_INT_EN|
					 DMASCR_PARITY_INT_EN|
					 DMASCR_CHAIN_EN |
					 DMASCR_SEM_EN |
					 pci_cmds);
	if (rw == WRITE)
		desc->control_bits |= cpu_to_le32(DMASCR_TRANSFER_READ);
	desc->sem_control_bits = desc->control_bits;

	bio->bi_sector += (len>>9);
	bio->bi_size -= len;
	bio->bi_idx++;
	if (bio->bi_idx >= bio->bi_vcnt) 
		card->currentbio = NULL;

	return 1;
}

static void process_page(unsigned long data)
{
	/* check if any of the requests in the page are DMA_COMPLETE,
	 * and deal with them appropriately.
	 * If we find a descriptor without DMA_COMPLETE in the semaphore, then
	 * dma must have hit an error on that descriptor, so use dma_status instead
	 * and assume that all following descriptors must be re-tried.
	 */
	struct mm_page *page;
	struct bio *return_bio=NULL;
	struct cardinfo *card = (struct cardinfo *)data;
	unsigned int dma_status = card->dma_status;

	spin_lock_bh(&card->lock);
	if (card->Active < 0)
		goto out_unlock;
	page = &card->mm_pages[card->Active];
	
	while (page->headcnt < page->cnt) {
		struct bio *bio = page->bio;
		struct mm_dma_desc *desc = &page->desc[page->headcnt];
		int control = le32_to_cpu(desc->sem_control_bits);
		int last=0;
		int idx;

		if (!(control & DMASCR_DMA_COMPLETE)) {
			control = dma_status;
			last=1; 
		}
		page->headcnt++;
		idx = bio->bi_phys_segments;
		bio->bi_phys_segments++;
		if (bio->bi_phys_segments >= bio->bi_vcnt)
			page->bio = bio->bi_next;

		pci_unmap_page(card->dev, desc->data_dma_handle, 
			       bio_iovec_idx(bio,idx)->bv_len,
				 (control& DMASCR_TRANSFER_READ) ?
				PCI_DMA_TODEVICE : PCI_DMA_FROMDEVICE);
		if (control & DMASCR_HARD_ERROR) {
			/* error */
			clear_bit(BIO_UPTODATE, &bio->bi_flags);
			printk(KERN_WARNING "MM%d: I/O error on sector %d/%d\n",
			       card->card_number, 
			       le32_to_cpu(desc->local_addr)>>9,
			       le32_to_cpu(desc->transfer_size));
			dump_dmastat(card, control);
		} else if (test_bit(BIO_RW, &bio->bi_rw) &&
			   le32_to_cpu(desc->local_addr)>>9 == card->init_size) {
			card->init_size += le32_to_cpu(desc->transfer_size)>>9;
			if (card->init_size>>1 >= card->mm_size) {
				printk(KERN_INFO "MM%d: memory now initialised\n",
				       card->card_number);
				set_userbit(card, MEMORY_INITIALIZED, 1);
			}
		}
		if (bio != page->bio) {
			bio->bi_next = return_bio;
			return_bio = bio;
		}

		if (last) break;
	}

	if (debug & DEBUG_LED_ON_TRANSFER)
		set_led(card, LED_REMOVE, LED_OFF);

	if (card->check_batteries) {
		card->check_batteries = 0;
		check_batteries(card);
	}
	if (page->headcnt >= page->cnt) {
		reset_page(page);
		card->Active = -1;
		activate(card);
	} else {
		/* haven't finished with this one yet */
		pr_debug("do some more\n");
		mm_start_io(card);
	}
 out_unlock:
	spin_unlock_bh(&card->lock);

	while(return_bio) {
		struct bio *bio = return_bio;

		return_bio = bio->bi_next;
		bio->bi_next = NULL;
		bio_endio(bio, bio->bi_size, 0);
	}
}

/*
-----------------------------------------------------------------------------------
--                              mm_make_request
-----------------------------------------------------------------------------------
*/
static int mm_make_request(request_queue_t *q, struct bio *bio)
{
	struct cardinfo *card = q->queuedata;
	pr_debug("mm_make_request %ld %d\n", bh->b_rsector, bh->b_size);

	bio->bi_phys_segments = bio->bi_idx; /* count of completed segments*/
	spin_lock_irq(&card->lock);
	*card->biotail = bio;
	bio->bi_next = NULL;
	card->biotail = &bio->bi_next;
	blk_plug_device(q);
	spin_unlock_irq(&card->lock);

	return 0;
}

/*
-----------------------------------------------------------------------------------
--                              mm_interrupt
-----------------------------------------------------------------------------------
*/
static irqreturn_t mm_interrupt(int irq, void *__card, struct pt_regs *regs)
{
	struct cardinfo *card = (struct cardinfo *) __card;
	unsigned int dma_status;
	unsigned short cfg_status;

HW_TRACE(0x30);

	dma_status = le32_to_cpu(readl(card->csr_remap + DMA_STATUS_CTRL));

	if (!(dma_status & (DMASCR_ERROR_MASK | DMASCR_CHAIN_COMPLETE))) {
		/* interrupt wasn't for me ... */
		return IRQ_NONE;
        }

	/* clear COMPLETION interrupts */
	if (card->flags & UM_FLAG_NO_BYTE_STATUS)
		writel(cpu_to_le32(DMASCR_DMA_COMPLETE|DMASCR_CHAIN_COMPLETE),
		       card->csr_remap+ DMA_STATUS_CTRL);
	else
		writeb((DMASCR_DMA_COMPLETE|DMASCR_CHAIN_COMPLETE) >> 16,
		       card->csr_remap+ DMA_STATUS_CTRL + 2);
	
	/* log errors and clear interrupt status */
	if (dma_status & DMASCR_ANY_ERR) {
		unsigned int	data_log1, data_log2;
		unsigned int	addr_log1, addr_log2;
		unsigned char	stat, count, syndrome, check;

		stat = readb(card->csr_remap + MEMCTRLCMD_ERRSTATUS);

		data_log1 = le32_to_cpu(readl(card->csr_remap + ERROR_DATA_LOG));
		data_log2 = le32_to_cpu(readl(card->csr_remap + ERROR_DATA_LOG + 4));
		addr_log1 = le32_to_cpu(readl(card->csr_remap + ERROR_ADDR_LOG));
		addr_log2 = readb(card->csr_remap + ERROR_ADDR_LOG + 4);

		count = readb(card->csr_remap + ERROR_COUNT);
		syndrome = readb(card->csr_remap + ERROR_SYNDROME);
		check = readb(card->csr_remap + ERROR_CHECK);

		dump_dmastat(card, dma_status);

		if (stat & 0x01)
			printk(KERN_ERR "MM%d*: Memory access error detected (err count %d)\n",
				card->card_number, count);
		if (stat & 0x02)
			printk(KERN_ERR "MM%d*: Multi-bit EDC error\n",
				card->card_number);

		printk(KERN_ERR "MM%d*: Fault Address 0x%02x%08x, Fault Data 0x%08x%08x\n",
			card->card_number, addr_log2, addr_log1, data_log2, data_log1);
		printk(KERN_ERR "MM%d*: Fault Check 0x%02x, Fault Syndrome 0x%02x\n",
			card->card_number, check, syndrome);

		writeb(0, card->csr_remap + ERROR_COUNT);
	}

	if (dma_status & DMASCR_PARITY_ERR_REP) {
		printk(KERN_ERR "MM%d*: PARITY ERROR REPORTED\n", card->card_number);
		pci_read_config_word(card->dev, PCI_STATUS, &cfg_status);
		pci_write_config_word(card->dev, PCI_STATUS, cfg_status);
	}

	if (dma_status & DMASCR_PARITY_ERR_DET) {
		printk(KERN_ERR "MM%d*: PARITY ERROR DETECTED\n", card->card_number); 
		pci_read_config_word(card->dev, PCI_STATUS, &cfg_status);
		pci_write_config_word(card->dev, PCI_STATUS, cfg_status);
	}

	if (dma_status & DMASCR_SYSTEM_ERR_SIG) {
		printk(KERN_ERR "MM%d*: SYSTEM ERROR\n", card->card_number); 
		pci_read_config_word(card->dev, PCI_STATUS, &cfg_status);
		pci_write_config_word(card->dev, PCI_STATUS, cfg_status);
	}

	if (dma_status & DMASCR_TARGET_ABT) {
		printk(KERN_ERR "MM%d*: TARGET ABORT\n", card->card_number); 
		pci_read_config_word(card->dev, PCI_STATUS, &cfg_status);
		pci_write_config_word(card->dev, PCI_STATUS, cfg_status);
	}

	if (dma_status & DMASCR_MASTER_ABT) {
		printk(KERN_ERR "MM%d*: MASTER ABORT\n", card->card_number); 
		pci_read_config_word(card->dev, PCI_STATUS, &cfg_status);
		pci_write_config_word(card->dev, PCI_STATUS, cfg_status);
	}

	/* and process the DMA descriptors */
	card->dma_status = dma_status;
	tasklet_schedule(&card->tasklet);

HW_TRACE(0x36);

	return IRQ_HANDLED; 
}
/*
-----------------------------------------------------------------------------------
--                         set_fault_to_battery_status
-----------------------------------------------------------------------------------
*/
/*
 * If both batteries are good, no LED
 * If either battery has been warned, solid LED
 * If both batteries are bad, flash the LED quickly
 * If either battery is bad, flash the LED semi quickly
 */
static void set_fault_to_battery_status(struct cardinfo *card)
{
	if (card->battery[0].good && card->battery[1].good)
		set_led(card, LED_FAULT, LED_OFF);
	else if (card->battery[0].warned || card->battery[1].warned)
		set_led(card, LED_FAULT, LED_ON);
	else if (!card->battery[0].good && !card->battery[1].good)
		set_led(card, LED_FAULT, LED_FLASH_7_0);
	else
		set_led(card, LED_FAULT, LED_FLASH_3_5);
}

static void init_battery_timer(void);


/*
-----------------------------------------------------------------------------------
--                            check_battery
-----------------------------------------------------------------------------------
*/
static int check_battery(struct cardinfo *card, int battery, int status)
{
	if (status != card->battery[battery].good) {
		card->battery[battery].good = !card->battery[battery].good;
		card->battery[battery].last_change = jiffies;

		if (card->battery[battery].good) {
			printk(KERN_ERR "MM%d: Battery %d now good\n",
				card->card_number, battery + 1);
			card->battery[battery].warned = 0;
		} else
			printk(KERN_ERR "MM%d: Battery %d now FAILED\n",
				card->card_number, battery + 1);

		return 1;
	} else if (!card->battery[battery].good &&
		   !card->battery[battery].warned &&
		   time_after_eq(jiffies, card->battery[battery].last_change +
				 (HZ * 60 * 60 * 5))) {
		printk(KERN_ERR "MM%d: Battery %d still FAILED after 5 hours\n",
			card->card_number, battery + 1);
		card->battery[battery].warned = 1;

		return 1;
	}

	return 0;
}
/*
-----------------------------------------------------------------------------------
--                              check_batteries
-----------------------------------------------------------------------------------
*/
static void check_batteries(struct cardinfo *card)
{
	/* NOTE: this must *never* be called while the card
	 * is doing (bus-to-card) DMA, or you will need the
	 * reset switch
	 */
	unsigned char status;
	int ret1, ret2;

	status = readb(card->csr_remap + MEMCTRLSTATUS_BATTERY);
	if (debug & DEBUG_BATTERY_POLLING)
		printk(KERN_DEBUG "MM%d: checking battery status, 1 = %s, 2 = %s\n",
		       card->card_number,
		       (status & BATTERY_1_FAILURE) ? "FAILURE" : "OK",
		       (status & BATTERY_2_FAILURE) ? "FAILURE" : "OK");

	ret1 = check_battery(card, 0, !(status & BATTERY_1_FAILURE));
	ret2 = check_battery(card, 1, !(status & BATTERY_2_FAILURE));

	if (ret1 || ret2)
		set_fault_to_battery_status(card);
}

static void check_all_batteries(unsigned long ptr)
{
	int i;

	for (i = 0; i < num_cards; i++) 
		if (!(cards[i].flags & UM_FLAG_NO_BATT)) {
			struct cardinfo *card = &cards[i];
			spin_lock_bh(&card->lock);
			if (card->Active >= 0)
				card->check_batteries = 1;
			else
				check_batteries(card);
			spin_unlock_bh(&card->lock);
		}

	init_battery_timer();
}
/*
-----------------------------------------------------------------------------------
--                            init_battery_timer
-----------------------------------------------------------------------------------
*/
static void init_battery_timer(void)
{
	init_timer(&battery_timer);
	battery_timer.function = check_all_batteries;
	battery_timer.expires = jiffies + (HZ * 60);
	add_timer(&battery_timer);
}
/*
-----------------------------------------------------------------------------------
--                              del_battery_timer
-----------------------------------------------------------------------------------
*/
static void del_battery_timer(void)
{
	del_timer(&battery_timer);
}
/*
-----------------------------------------------------------------------------------
--                                mm_revalidate
-----------------------------------------------------------------------------------
*/
/*
 * Note no locks taken out here.  In a worst case scenario, we could drop
 * a chunk of system memory.  But that should never happen, since validation
 * happens at open or mount time, when locks are held.
 *
 *	That's crap, since doing that while some partitions are opened
 * or mounted will give you really nasty results.
 */
static int mm_revalidate(struct gendisk *disk)
{
	struct cardinfo *card = disk->private_data;
	set_capacity(disk, card->mm_size << 1);
	return 0;
}

static int mm_getgeo(struct block_device *bdev, struct hd_geometry *geo)
{
	struct cardinfo *card = bdev->bd_disk->private_data;
	int size = card->mm_size * (1024 / MM_HARDSECT);

	/*
	 * get geometry: we have to fake one...  trim the size to a
	 * multiple of 2048 (1M): tell we have 32 sectors, 64 heads,
	 * whatever cylinders.
	 */
	geo->heads     = 64;
	geo->sectors   = 32;
	geo->cylinders = size / (geo->heads * geo->sectors);
	return 0;
}

/*
-----------------------------------------------------------------------------------
--                                mm_check_change
-----------------------------------------------------------------------------------
  Future support for removable devices
*/
static int mm_check_change(struct gendisk *disk)
{
/*  struct cardinfo *dev = disk->private_data; */
	return 0;
}
/*
-----------------------------------------------------------------------------------
--                             mm_fops
-----------------------------------------------------------------------------------
*/
static struct block_device_operations mm_fops = {
	.owner		= THIS_MODULE,
	.getgeo		= mm_getgeo,
	.revalidate_disk= mm_revalidate,
	.media_changed	= mm_check_change,
};
/*
-----------------------------------------------------------------------------------
--                                mm_pci_probe
-----------------------------------------------------------------------------------
*/
static int __devinit mm_pci_probe(struct pci_dev *dev, const struct pci_device_id *id)
{
	int ret = -ENODEV;
	struct cardinfo *card = &cards[num_cards];
	unsigned char	mem_present;
	unsigned char	batt_status;
	unsigned int	saved_bar, data;
	int		magic_number;

	if (pci_enable_device(dev) < 0)
		return -ENODEV;

	pci_write_config_byte(dev, PCI_LATENCY_TIMER, 0xF8);
	pci_set_master(dev);

	card->dev         = dev;
	card->card_number = num_cards;

	card->csr_base = pci_resource_start(dev, 0);
	card->csr_len  = pci_resource_len(dev, 0);
#ifdef CONFIG_MM_MAP_MEMORY
	card->mem_base = pci_resource_start(dev, 1);
	card->mem_len  = pci_resource_len(dev, 1);
#endif

	printk(KERN_INFO "Micro Memory(tm) controller #%d found at %02x:%02x (PCI Mem Module (Battery Backup))\n",
	       card->card_number, dev->bus->number, dev->devfn);

	if (pci_set_dma_mask(dev, 0xffffffffffffffffLL) &&
	    pci_set_dma_mask(dev, 0xffffffffLL)) {
		printk(KERN_WARNING "MM%d: NO suitable DMA found\n",num_cards);
		return  -ENOMEM;
	}
	if (!request_mem_region(card->csr_base, card->csr_len, "Micro Memory")) {
		printk(KERN_ERR "MM%d: Unable to request memory region\n", card->card_number);
		ret = -ENOMEM;

		goto failed_req_csr;
	}

	card->csr_remap = ioremap_nocache(card->csr_base, card->csr_len);
	if (!card->csr_remap) {
		printk(KERN_ERR "MM%d: Unable to remap memory region\n", card->card_number);
		ret = -ENOMEM;

		goto failed_remap_csr;
	}

	printk(KERN_INFO "MM%d: CSR 0x%08lx -> 0x%p (0x%lx)\n", card->card_number,
	       card->csr_base, card->csr_remap, card->csr_len);

#ifdef CONFIG_MM_MAP_MEMORY
	if (!request_mem_region(card->mem_base, card->mem_len, "Micro Memory")) {
		printk(KERN_ERR "MM%d: Unable to request memory region\n", card->card_number);
		ret = -ENOMEM;

		goto failed_req_mem;
	}

	if (!(card->mem_remap = ioremap(card->mem_base, cards->mem_len))) {
		printk(KERN_ERR "MM%d: Unable to remap memory region\n", card->card_number);
		ret = -ENOMEM;

		goto failed_remap_mem;
	}

	printk(KERN_INFO "MM%d: MEM 0x%8lx -> 0x%8lx (0x%lx)\n", card->card_number,
	       card->mem_base, card->mem_remap, card->mem_len);
#else
	printk(KERN_INFO "MM%d: MEM area not remapped (CONFIG_MM_MAP_MEMORY not set)\n",
	       card->card_number);
#endif
	switch(card->dev->device) {
	case 0x5415:
		card->flags |= UM_FLAG_NO_BYTE_STATUS | UM_FLAG_NO_BATTREG;
		magic_number = 0x59;
		break;

	case 0x5425:
		card->flags |= UM_FLAG_NO_BYTE_STATUS;
		magic_number = 0x5C;
		break;

	case 0x6155:
		card->flags |= UM_FLAG_NO_BYTE_STATUS | UM_FLAG_NO_BATTREG | UM_FLAG_NO_BATT;
		magic_number = 0x99;
		break;

	default:
		magic_number = 0x100;
		break;
	}

	if (readb(card->csr_remap + MEMCTRLSTATUS_MAGIC) != magic_number) {
		printk(KERN_ERR "MM%d: Magic number invalid\n", card->card_number);
		ret = -ENOMEM;
		goto failed_magic;
	}

	card->mm_pages[0].desc = pci_alloc_consistent(card->dev,
						      PAGE_SIZE*2,
						      &card->mm_pages[0].page_dma);
	card->mm_pages[1].desc = pci_alloc_consistent(card->dev,
						      PAGE_SIZE*2,
						      &card->mm_pages[1].page_dma);
	if (card->mm_pages[0].desc == NULL ||
	    card->mm_pages[1].desc == NULL) {
		printk(KERN_ERR "MM%d: alloc failed\n", card->card_number);
		goto failed_alloc;
	}
	reset_page(&card->mm_pages[0]);
	reset_page(&card->mm_pages[1]);
	card->Ready = 0;	/* page 0 is ready */
	card->Active = -1;	/* no page is active */
	card->bio = NULL;
	card->biotail = &card->bio;

	card->queue = blk_alloc_queue(GFP_KERNEL);
	if (!card->queue)
		goto failed_alloc;

	blk_queue_make_request(card->queue, mm_make_request);
	card->queue->queuedata = card;
	card->queue->unplug_fn = mm_unplug_device;

	tasklet_init(&card->tasklet, process_page, (unsigned long)card);

	card->check_batteries = 0;
	
	mem_present = readb(card->csr_remap + MEMCTRLSTATUS_MEMORY);
	switch (mem_present) {
	case MEM_128_MB:
		card->mm_size = 1024 * 128;
		break;
	case MEM_256_MB:
		card->mm_size = 1024 * 256;
		break;
	case MEM_512_MB:
		card->mm_size = 1024 * 512;
		break;
	case MEM_1_GB:
		card->mm_size = 1024 * 1024;
		break;
	case MEM_2_GB:
		card->mm_size = 1024 * 2048;
		break;
	default:
		card->mm_size = 0;
		break;
	}

	/* Clear the LED's we control */
	set_led(card, LED_REMOVE, LED_OFF);
	set_led(card, LED_FAULT, LED_OFF);

	batt_status = readb(card->csr_remap + MEMCTRLSTATUS_BATTERY);

	card->battery[0].good = !(batt_status & BATTERY_1_FAILURE);
	card->battery[1].good = !(batt_status & BATTERY_2_FAILURE);
	card->battery[0].last_change = card->battery[1].last_change = jiffies;

	if (card->flags & UM_FLAG_NO_BATT) 
		printk(KERN_INFO "MM%d: Size %d KB\n",
		       card->card_number, card->mm_size);
	else {
		printk(KERN_INFO "MM%d: Size %d KB, Battery 1 %s (%s), Battery 2 %s (%s)\n",
		       card->card_number, card->mm_size,
		       (batt_status & BATTERY_1_DISABLED ? "Disabled" : "Enabled"),
		       card->battery[0].good ? "OK" : "FAILURE",
		       (batt_status & BATTERY_2_DISABLED ? "Disabled" : "Enabled"),
		       card->battery[1].good ? "OK" : "FAILURE");

		set_fault_to_battery_status(card);
	}

	pci_read_config_dword(dev, PCI_BASE_ADDRESS_1, &saved_bar);
	data = 0xffffffff;
	pci_write_config_dword(dev, PCI_BASE_ADDRESS_1, data);
	pci_read_config_dword(dev, PCI_BASE_ADDRESS_1, &data);
	pci_write_config_dword(dev, PCI_BASE_ADDRESS_1, saved_bar);
	data &= 0xfffffff0;
	data = ~data;
	data += 1;

	card->win_size = data;


	if (request_irq(dev->irq, mm_interrupt, SA_SHIRQ, "pci-umem", card)) {
		printk(KERN_ERR "MM%d: Unable to allocate IRQ\n", card->card_number);
		ret = -ENODEV;

		goto failed_req_irq;
	}

	card->irq = dev->irq;
	printk(KERN_INFO "MM%d: Window size %d bytes, IRQ %d\n", card->card_number,
	       card->win_size, card->irq);

        spin_lock_init(&card->lock);

	pci_set_drvdata(dev, card);

	if (pci_write_cmd != 0x0F) 	/* If not Memory Write & Invalidate */
		pci_write_cmd = 0x07;	/* then Memory Write command */

	if (pci_write_cmd & 0x08) { /* use Memory Write and Invalidate */
		unsigned short cfg_command;
		pci_read_config_word(dev, PCI_COMMAND, &cfg_command);
		cfg_command |= 0x10; /* Memory Write & Invalidate Enable */
		pci_write_config_word(dev, PCI_COMMAND, cfg_command);
	}
	pci_cmds = (pci_read_cmd << 28) | (pci_write_cmd << 24);

	num_cards++;

	if (!get_userbit(card, MEMORY_INITIALIZED)) {
		printk(KERN_INFO "MM%d: memory NOT initialized. Consider over-writing whole device.\n", card->card_number);
		card->init_size = 0;
	} else {
		printk(KERN_INFO "MM%d: memory already initialized\n", card->card_number);
		card->init_size = card->mm_size;
	}

	/* Enable ECC */
	writeb(EDC_STORE_CORRECT, card->csr_remap + MEMCTRLCMD_ERRCTRL);

	return 0;

 failed_req_irq:
 failed_alloc:
	if (card->mm_pages[0].desc)
		pci_free_consistent(card->dev, PAGE_SIZE*2,
				    card->mm_pages[0].desc,
				    card->mm_pages[0].page_dma);
	if (card->mm_pages[1].desc)
		pci_free_consistent(card->dev, PAGE_SIZE*2,
				    card->mm_pages[1].desc,
				    card->mm_pages[1].page_dma);
 failed_magic:
#ifdef CONFIG_MM_MAP_MEMORY
	iounmap(card->mem_remap);
 failed_remap_mem:
	release_mem_region(card->mem_base, card->mem_len);
 failed_req_mem:
#endif
	iounmap(card->csr_remap);
 failed_remap_csr:
	release_mem_region(card->csr_base, card->csr_len);
 failed_req_csr:

	return ret;
}
/*
-----------------------------------------------------------------------------------
--                              mm_pci_remove
-----------------------------------------------------------------------------------
*/
static void mm_pci_remove(struct pci_dev *dev)
{
	struct cardinfo *card = pci_get_drvdata(dev);

	tasklet_kill(&card->tasklet);
	iounmap(card->csr_remap);
	release_mem_region(card->csr_base, card->csr_len);
#ifdef CONFIG_MM_MAP_MEMORY
	iounmap(card->mem_remap);
	release_mem_region(card->mem_base, card->mem_len);
#endif
	free_irq(card->irq, card);

	if (card->mm_pages[0].desc)
		pci_free_consistent(card->dev, PAGE_SIZE*2,
				    card->mm_pages[0].desc,
				    card->mm_pages[0].page_dma);
	if (card->mm_pages[1].desc)
		pci_free_consistent(card->dev, PAGE_SIZE*2,
				    card->mm_pages[1].desc,
				    card->mm_pages[1].page_dma);
	blk_put_queue(card->queue);
}

static const struct pci_device_id mm_pci_ids[] = { {
	.vendor =	PCI_VENDOR_ID_MICRO_MEMORY,
	.device =	PCI_DEVICE_ID_MICRO_MEMORY_5415CN,
	}, {
	.vendor =	PCI_VENDOR_ID_MICRO_MEMORY,
	.device =	PCI_DEVICE_ID_MICRO_MEMORY_5425CN,
	}, {
	.vendor =	PCI_VENDOR_ID_MICRO_MEMORY,
	.device =	PCI_DEVICE_ID_MICRO_MEMORY_6155,
	}, {
	.vendor	=	0x8086,
	.device	=	0xB555,
	.subvendor=	0x1332,
	.subdevice=	0x5460,
	.class	=	0x050000,
	.class_mask=	0,
	}, { /* end: all zeroes */ }
};

MODULE_DEVICE_TABLE(pci, mm_pci_ids);

static struct pci_driver mm_pci_driver = {
	.name =		"umem",
	.id_table =	mm_pci_ids,
	.probe =	mm_pci_probe,
	.remove =	mm_pci_remove,
};
/*
-----------------------------------------------------------------------------------
--                               mm_init
-----------------------------------------------------------------------------------
*/

static int __init mm_init(void)
{
	int retval, i;
	int err;

	printk(KERN_INFO DRIVER_VERSION " : " DRIVER_DESC "\n");

	retval = pci_register_driver(&mm_pci_driver);
	if (retval)
		return -ENOMEM;

	err = major_nr = register_blkdev(0, "umem");
	if (err < 0)
		return -EIO;

	for (i = 0; i < num_cards; i++) {
		mm_gendisk[i] = alloc_disk(1 << MM_SHIFT);
		if (!mm_gendisk[i])
			goto out;
	}

	for (i = 0; i < num_cards; i++) {
		struct gendisk *disk = mm_gendisk[i];
		sprintf(disk->disk_name, "umem%c", 'a'+i);
		sprintf(disk->devfs_name, "umem/card%d", i);
		spin_lock_init(&cards[i].lock);
		disk->major = major_nr;
		disk->first_minor  = i << MM_SHIFT;
		disk->fops = &mm_fops;
		disk->private_data = &cards[i];
		disk->queue = cards[i].queue;
		set_capacity(disk, cards[i].mm_size << 1);
		add_disk(disk);
	}

	init_battery_timer();
	printk("MM: desc_per_page = %ld\n", DESC_PER_PAGE);
/* printk("mm_init: Done. 10-19-01 9:00\n"); */
	return 0;

out:
	unregister_blkdev(major_nr, "umem");
	while (i--)
		put_disk(mm_gendisk[i]);
	return -ENOMEM;
}
/*
-----------------------------------------------------------------------------------
--                             mm_cleanup
-----------------------------------------------------------------------------------
*/
static void __exit mm_cleanup(void)
{
	int i;

	del_battery_timer();

	for (i=0; i < num_cards ; i++) {
		del_gendisk(mm_gendisk[i]);
		put_disk(mm_gendisk[i]);
	}

	pci_unregister_driver(&mm_pci_driver);

	unregister_blkdev(major_nr, "umem");
}

module_init(mm_init);
module_exit(mm_cleanup);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
