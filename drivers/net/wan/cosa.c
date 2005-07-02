/* $Id: cosa.c,v 1.31 2000/03/08 17:47:16 kas Exp $ */

/*
 *  Copyright (C) 1995-1997  Jan "Yenya" Kasprzak <kas@fi.muni.cz>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/*
 * The driver for the SRP and COSA synchronous serial cards.
 *
 * HARDWARE INFO
 *
 * Both cards are developed at the Institute of Computer Science,
 * Masaryk University (http://www.ics.muni.cz/). The hardware is
 * developed by Jiri Novotny <novotny@ics.muni.cz>. More information
 * and the photo of both cards is available at
 * http://www.pavoucek.cz/cosa.html. The card documentation, firmwares
 * and other goods can be downloaded from ftp://ftp.ics.muni.cz/pub/cosa/.
 * For Linux-specific utilities, see below in the "Software info" section.
 * If you want to order the card, contact Jiri Novotny.
 *
 * The SRP (serial port?, the Czech word "srp" means "sickle") card
 * is a 2-port intelligent (with its own 8-bit CPU) synchronous serial card
 * with V.24 interfaces up to 80kb/s each.
 *
 * The COSA (communication serial adapter?, the Czech word "kosa" means
 * "scythe") is a next-generation sync/async board with two interfaces
 * - currently any of V.24, X.21, V.35 and V.36 can be selected.
 * It has a 16-bit SAB80166 CPU and can do up to 10 Mb/s per channel.
 * The 8-channels version is in development.
 *
 * Both types have downloadable firmware and communicate via ISA DMA.
 * COSA can be also a bus-mastering device.
 *
 * SOFTWARE INFO
 *
 * The homepage of the Linux driver is at http://www.fi.muni.cz/~kas/cosa/.
 * The CVS tree of Linux driver can be viewed there, as well as the
 * firmware binaries and user-space utilities for downloading the firmware
 * into the card and setting up the card.
 *
 * The Linux driver (unlike the present *BSD drivers :-) can work even
 * for the COSA and SRP in one computer and allows each channel to work
 * in one of the three modes (character device, Cisco HDLC, Sync PPP).
 *
 * AUTHOR
 *
 * The Linux driver was written by Jan "Yenya" Kasprzak <kas@fi.muni.cz>.
 *
 * You can mail me bugfixes and even success reports. I am especially
 * interested in the SMP and/or muliti-channel success/failure reports
 * (I wonder if I did the locking properly :-).
 *
 * THE AUTHOR USED THE FOLLOWING SOURCES WHEN PROGRAMMING THE DRIVER
 *
 * The COSA/SRP NetBSD driver by Zdenek Salvet and Ivos Cernohlavek
 * The skeleton.c by Donald Becker
 * The SDL Riscom/N2 driver by Mike Natale
 * The Comtrol Hostess SV11 driver by Alan Cox
 * The Sync PPP/Cisco HDLC layer (syncppp.c) ported to Linux by Alan Cox
 */
/*
 *     5/25/1999 : Marcelo Tosatti <marcelo@conectiva.com.br>
 *             fixed a deadlock in cosa_sppp_open
 */

/* ---------- Headers, macros, data structures ---------- */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/fs.h>
#include <linux/devfs_fs_kernel.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/netdevice.h>
#include <linux/spinlock.h>
#include <linux/smp_lock.h>
#include <linux/device.h>

#undef COSA_SLOW_IO	/* for testing purposes only */
#undef REALLY_SLOW_IO

#include <asm/io.h>
#include <asm/dma.h>
#include <asm/byteorder.h>

#include <net/syncppp.h>
#include "cosa.h"

/* Maximum length of the identification string. */
#define COSA_MAX_ID_STRING	128

/* Maximum length of the channel name */
#define COSA_MAX_NAME		(sizeof("cosaXXXcXXX")+1)

/* Per-channel data structure */

struct channel_data {
	void *if_ptr;	/* General purpose pointer (used by SPPP) */
	int usage;	/* Usage count; >0 for chrdev, -1 for netdev */
	int num;	/* Number of the channel */
	struct cosa_data *cosa;	/* Pointer to the per-card structure */
	int txsize;	/* Size of transmitted data */
	char *txbuf;	/* Transmit buffer */
	char name[COSA_MAX_NAME];	/* channel name */

	/* The HW layer interface */
	/* routine called from the RX interrupt */
	char *(*setup_rx)(struct channel_data *channel, int size);
	/* routine called when the RX is done (from the EOT interrupt) */
	int (*rx_done)(struct channel_data *channel);
	/* routine called when the TX is done (from the EOT interrupt) */
	int (*tx_done)(struct channel_data *channel, int size);

	/* Character device parts */
	struct semaphore rsem, wsem;
	char *rxdata;
	int rxsize;
	wait_queue_head_t txwaitq, rxwaitq;
	int tx_status, rx_status;

	/* SPPP/HDLC device parts */
	struct ppp_device pppdev;
	struct sk_buff *rx_skb, *tx_skb;
	struct net_device_stats stats;
};

/* cosa->firmware_status bits */
#define COSA_FW_RESET		(1<<0)	/* Is the ROM monitor active? */
#define COSA_FW_DOWNLOAD	(1<<1)	/* Is the microcode downloaded? */
#define COSA_FW_START		(1<<2)	/* Is the microcode running? */

struct cosa_data {
	int num;			/* Card number */
	char name[COSA_MAX_NAME];	/* Card name - e.g "cosa0" */
	unsigned int datareg, statusreg;	/* I/O ports */
	unsigned short irq, dma;	/* IRQ and DMA number */
	unsigned short startaddr;	/* Firmware start address */
	unsigned short busmaster;	/* Use busmastering? */
	int nchannels;			/* # of channels on this card */
	int driver_status;		/* For communicating with firmware */
	int firmware_status;		/* Downloaded, reseted, etc. */
	long int rxbitmap, txbitmap;	/* Bitmap of channels who are willing to send/receive data */
	long int rxtx;			/* RX or TX in progress? */
	int enabled;
	int usage;				/* usage count */
	int txchan, txsize, rxsize;
	struct channel_data *rxchan;
	char *bouncebuf;
	char *txbuf, *rxbuf;
	struct channel_data *chan;
	spinlock_t lock;	/* For exclusive operations on this structure */
	char id_string[COSA_MAX_ID_STRING];	/* ROM monitor ID string */
	char *type;				/* card type */
};

/*
 * Define this if you want all the possible ports to be autoprobed.
 * It is here but it probably is not a good idea to use this.
 */
/* #define COSA_ISA_AUTOPROBE	1 */

/*
 * Character device major number. 117 was allocated for us.
 * The value of 0 means to allocate a first free one.
 */
static int cosa_major = 117;

/*
 * Encoding of the minor numbers:
 * The lowest CARD_MINOR_BITS bits means the channel on the single card,
 * the highest bits means the card number.
 */
#define CARD_MINOR_BITS	4	/* How many bits in minor number are reserved
				 * for the single card */
/*
 * The following depends on CARD_MINOR_BITS. Unfortunately, the "MODULE_STRING"
 * macro doesn't like anything other than the raw number as an argument :-(
 */
#define MAX_CARDS	16
/* #define MAX_CARDS	(1 << (8-CARD_MINOR_BITS)) */

#define DRIVER_RX_READY		0x0001
#define DRIVER_TX_READY		0x0002
#define DRIVER_TXMAP_SHIFT	2
#define DRIVER_TXMAP_MASK	0x0c	/* FIXME: 0xfc for 8-channel version */

/*
 * for cosa->rxtx - indicates whether either transmit or receive is
 * in progress. These values are mean number of the bit.
 */
#define TXBIT 0
#define RXBIT 1
#define IRQBIT 2

#define COSA_MTU 2000	/* FIXME: I don't know this exactly */

#undef DEBUG_DATA //1	/* Dump the data read or written to the channel */
#undef DEBUG_IRQS //1	/* Print the message when the IRQ is received */
#undef DEBUG_IO   //1	/* Dump the I/O traffic */

#define TX_TIMEOUT	(5*HZ)

/* Maybe the following should be allocated dynamically */
static struct cosa_data cosa_cards[MAX_CARDS];
static int nr_cards;

#ifdef COSA_ISA_AUTOPROBE
static int io[MAX_CARDS+1]  = { 0x220, 0x228, 0x210, 0x218, 0, };
/* NOTE: DMA is not autoprobed!!! */
static int dma[MAX_CARDS+1] = { 1, 7, 1, 7, 1, 7, 1, 7, 0, };
#else
static int io[MAX_CARDS+1];
static int dma[MAX_CARDS+1];
#endif
/* IRQ can be safely autoprobed */
static int irq[MAX_CARDS+1] = { -1, -1, -1, -1, -1, -1, 0, };

/* for class stuff*/
static struct class *cosa_class;

#ifdef MODULE
module_param_array(io, int, NULL, 0);
MODULE_PARM_DESC(io, "The I/O bases of the COSA or SRP cards");
module_param_array(irq, int, NULL, 0);
MODULE_PARM_DESC(irq, "The IRQ lines of the COSA or SRP cards");
module_param_array(dma, int, NULL, 0);
MODULE_PARM_DESC(dma, "The DMA channels of the COSA or SRP cards");

MODULE_AUTHOR("Jan \"Yenya\" Kasprzak, <kas@fi.muni.cz>");
MODULE_DESCRIPTION("Modular driver for the COSA or SRP synchronous card");
MODULE_LICENSE("GPL");
#endif

/* I use this mainly for testing purposes */
#ifdef COSA_SLOW_IO
#define cosa_outb outb_p
#define cosa_outw outw_p
#define cosa_inb  inb_p
#define cosa_inw  inw_p
#else
#define cosa_outb outb
#define cosa_outw outw
#define cosa_inb  inb
#define cosa_inw  inw
#endif

#define is_8bit(cosa)		(!(cosa->datareg & 0x08))

#define cosa_getstatus(cosa)	(cosa_inb(cosa->statusreg))
#define cosa_putstatus(cosa, stat)	(cosa_outb(stat, cosa->statusreg))
#define cosa_getdata16(cosa)	(cosa_inw(cosa->datareg))
#define cosa_getdata8(cosa)	(cosa_inb(cosa->datareg))
#define cosa_putdata16(cosa, dt)	(cosa_outw(dt, cosa->datareg))
#define cosa_putdata8(cosa, dt)	(cosa_outb(dt, cosa->datareg))

/* Initialization stuff */
static int cosa_probe(int ioaddr, int irq, int dma);

/* HW interface */
static void cosa_enable_rx(struct channel_data *chan);
static void cosa_disable_rx(struct channel_data *chan);
static int cosa_start_tx(struct channel_data *channel, char *buf, int size);
static void cosa_kick(struct cosa_data *cosa);
static int cosa_dma_able(struct channel_data *chan, char *buf, int data);

/* SPPP/HDLC stuff */
static void sppp_channel_init(struct channel_data *chan);
static void sppp_channel_delete(struct channel_data *chan);
static int cosa_sppp_open(struct net_device *d);
static int cosa_sppp_close(struct net_device *d);
static void cosa_sppp_timeout(struct net_device *d);
static int cosa_sppp_tx(struct sk_buff *skb, struct net_device *d);
static char *sppp_setup_rx(struct channel_data *channel, int size);
static int sppp_rx_done(struct channel_data *channel);
static int sppp_tx_done(struct channel_data *channel, int size);
static int cosa_sppp_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd);
static struct net_device_stats *cosa_net_stats(struct net_device *dev);

/* Character device */
static void chardev_channel_init(struct channel_data *chan);
static char *chrdev_setup_rx(struct channel_data *channel, int size);
static int chrdev_rx_done(struct channel_data *channel);
static int chrdev_tx_done(struct channel_data *channel, int size);
static ssize_t cosa_read(struct file *file,
	char __user *buf, size_t count, loff_t *ppos);
static ssize_t cosa_write(struct file *file,
	const char __user *buf, size_t count, loff_t *ppos);
static unsigned int cosa_poll(struct file *file, poll_table *poll);
static int cosa_open(struct inode *inode, struct file *file);
static int cosa_release(struct inode *inode, struct file *file);
static int cosa_chardev_ioctl(struct inode *inode, struct file *file,
	unsigned int cmd, unsigned long arg);
#ifdef COSA_FASYNC_WORKING
static int cosa_fasync(struct inode *inode, struct file *file, int on);
#endif

static struct file_operations cosa_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.read		= cosa_read,
	.write		= cosa_write,
	.poll		= cosa_poll,
	.ioctl		= cosa_chardev_ioctl,
	.open		= cosa_open,
	.release	= cosa_release,
#ifdef COSA_FASYNC_WORKING
	.fasync		= cosa_fasync,
#endif
};

/* Ioctls */
static int cosa_start(struct cosa_data *cosa, int address);
static int cosa_reset(struct cosa_data *cosa);
static int cosa_download(struct cosa_data *cosa, void __user *a);
static int cosa_readmem(struct cosa_data *cosa, void __user *a);

/* COSA/SRP ROM monitor */
static int download(struct cosa_data *cosa, const char __user *data, int addr, int len);
static int startmicrocode(struct cosa_data *cosa, int address);
static int readmem(struct cosa_data *cosa, char __user *data, int addr, int len);
static int cosa_reset_and_read_id(struct cosa_data *cosa, char *id);

/* Auxilliary functions */
static int get_wait_data(struct cosa_data *cosa);
static int put_wait_data(struct cosa_data *cosa, int data);
static int puthexnumber(struct cosa_data *cosa, int number);
static void put_driver_status(struct cosa_data *cosa);
static void put_driver_status_nolock(struct cosa_data *cosa);

/* Interrupt handling */
static irqreturn_t cosa_interrupt(int irq, void *cosa, struct pt_regs *regs);

/* I/O ops debugging */
#ifdef DEBUG_IO
static void debug_data_in(struct cosa_data *cosa, int data);
static void debug_data_out(struct cosa_data *cosa, int data);
static void debug_data_cmd(struct cosa_data *cosa, int data);
static void debug_status_in(struct cosa_data *cosa, int status);
static void debug_status_out(struct cosa_data *cosa, int status);
#endif


/* ---------- Initialization stuff ---------- */

static int __init cosa_init(void)
{
	int i, err = 0;

	printk(KERN_INFO "cosa v1.08 (c) 1997-2000 Jan Kasprzak <kas@fi.muni.cz>\n");
#ifdef CONFIG_SMP
	printk(KERN_INFO "cosa: SMP found. Please mail any success/failure reports to the author.\n");
#endif
	if (cosa_major > 0) {
		if (register_chrdev(cosa_major, "cosa", &cosa_fops)) {
			printk(KERN_WARNING "cosa: unable to get major %d\n",
				cosa_major);
			err = -EIO;
			goto out;
		}
	} else {
		if (!(cosa_major=register_chrdev(0, "cosa", &cosa_fops))) {
			printk(KERN_WARNING "cosa: unable to register chardev\n");
			err = -EIO;
			goto out;
		}
	}
	for (i=0; i<MAX_CARDS; i++)
		cosa_cards[i].num = -1;
	for (i=0; io[i] != 0 && i < MAX_CARDS; i++)
		cosa_probe(io[i], irq[i], dma[i]);
	if (!nr_cards) {
		printk(KERN_WARNING "cosa: no devices found.\n");
		unregister_chrdev(cosa_major, "cosa");
		err = -ENODEV;
		goto out;
	}
	devfs_mk_dir("cosa");
	cosa_class = class_create(THIS_MODULE, "cosa");
	if (IS_ERR(cosa_class)) {
		err = PTR_ERR(cosa_class);
		goto out_chrdev;
	}
	for (i=0; i<nr_cards; i++) {
		class_device_create(cosa_class, MKDEV(cosa_major, i),
				NULL, "cosa%d", i);
		err = devfs_mk_cdev(MKDEV(cosa_major, i),
				S_IFCHR|S_IRUSR|S_IWUSR,
				"cosa/%d", i);
		if (err) {
			class_device_destroy(cosa_class, MKDEV(cosa_major, i));
			goto out_chrdev;		
		}
	}
	err = 0;
	goto out;
	
out_chrdev:
	unregister_chrdev(cosa_major, "cosa");
out:
	return err;
}
module_init(cosa_init);

static void __exit cosa_exit(void)
{
	struct cosa_data *cosa;
	int i;
	printk(KERN_INFO "Unloading the cosa module\n");

	for (i=0; i<nr_cards; i++) {
		class_device_destroy(cosa_class, MKDEV(cosa_major, i));
		devfs_remove("cosa/%d", i);
	}
	class_destroy(cosa_class);
	devfs_remove("cosa");
	for (cosa=cosa_cards; nr_cards--; cosa++) {
		/* Clean up the per-channel data */
		for (i=0; i<cosa->nchannels; i++) {
			/* Chardev driver has no alloc'd per-channel data */
			sppp_channel_delete(cosa->chan+i);
		}
		/* Clean up the per-card data */
		kfree(cosa->chan);
		kfree(cosa->bouncebuf);
		free_irq(cosa->irq, cosa);
		free_dma(cosa->dma);
		release_region(cosa->datareg,is_8bit(cosa)?2:4);
	}
	unregister_chrdev(cosa_major, "cosa");
}
module_exit(cosa_exit);

/*
 * This function should register all the net devices needed for the
 * single channel.
 */
static __inline__ void channel_init(struct channel_data *chan)
{
	sprintf(chan->name, "cosa%dc%d", chan->cosa->num, chan->num);

	/* Initialize the chardev data structures */
	chardev_channel_init(chan);

	/* Register the sppp interface */
	sppp_channel_init(chan);
}
	
static int cosa_probe(int base, int irq, int dma)
{
	struct cosa_data *cosa = cosa_cards+nr_cards;
	int i, err = 0;

	memset(cosa, 0, sizeof(struct cosa_data));

	/* Checking validity of parameters: */
	/* IRQ should be 2-7 or 10-15; negative IRQ means autoprobe */
	if ((irq >= 0  && irq < 2) || irq > 15 || (irq < 10 && irq > 7)) {
		printk (KERN_INFO "cosa_probe: invalid IRQ %d\n", irq);
		return -1;
	}
	/* I/O address should be between 0x100 and 0x3ff and should be
	 * multiple of 8. */
	if (base < 0x100 || base > 0x3ff || base & 0x7) {
		printk (KERN_INFO "cosa_probe: invalid I/O address 0x%x\n",
			base);
		return -1;
	}
	/* DMA should be 0,1 or 3-7 */
	if (dma < 0 || dma == 4 || dma > 7) {
		printk (KERN_INFO "cosa_probe: invalid DMA %d\n", dma);
		return -1;
	}
	/* and finally, on 16-bit COSA DMA should be 4-7 and 
	 * I/O base should not be multiple of 0x10 */
	if (((base & 0x8) && dma < 4) || (!(base & 0x8) && dma > 3)) {
		printk (KERN_INFO "cosa_probe: 8/16 bit base and DMA mismatch"
			" (base=0x%x, dma=%d)\n", base, dma);
		return -1;
	}

	cosa->dma = dma;
	cosa->datareg = base;
	cosa->statusreg = is_8bit(cosa)?base+1:base+2;
	spin_lock_init(&cosa->lock);

	if (!request_region(base, is_8bit(cosa)?2:4,"cosa"))
		return -1;
	
	if (cosa_reset_and_read_id(cosa, cosa->id_string) < 0) {
		printk(KERN_DEBUG "cosa: probe at 0x%x failed.\n", base);
		err = -1;
		goto err_out;
	}

	/* Test the validity of identification string */
	if (!strncmp(cosa->id_string, "SRP", 3))
		cosa->type = "srp";
	else if (!strncmp(cosa->id_string, "COSA", 4))
		cosa->type = is_8bit(cosa)? "cosa8": "cosa16";
	else {
/* Print a warning only if we are not autoprobing */
#ifndef COSA_ISA_AUTOPROBE
		printk(KERN_INFO "cosa: valid signature not found at 0x%x.\n",
			base);
#endif
		err = -1;
		goto err_out;
	}
	/* Update the name of the region now we know the type of card */ 
	release_region(base, is_8bit(cosa)?2:4);
	if (!request_region(base, is_8bit(cosa)?2:4, cosa->type)) {
		printk(KERN_DEBUG "cosa: changing name at 0x%x failed.\n", base);
		return -1;
	}

	/* Now do IRQ autoprobe */
	if (irq < 0) {
		unsigned long irqs;
/*		printk(KERN_INFO "IRQ autoprobe\n"); */
		irqs = probe_irq_on();
		/* 
		 * Enable interrupt on tx buffer empty (it sure is) 
		 * really sure ?
		 * FIXME: When this code is not used as module, we should
		 * probably call udelay() instead of the interruptible sleep.
		 */
		set_current_state(TASK_INTERRUPTIBLE);
		cosa_putstatus(cosa, SR_TX_INT_ENA);
		schedule_timeout(30);
		irq = probe_irq_off(irqs);
		/* Disable all IRQs from the card */
		cosa_putstatus(cosa, 0);
		/* Empty the received data register */
		cosa_getdata8(cosa);

		if (irq < 0) {
			printk (KERN_INFO "cosa IRQ autoprobe: multiple interrupts obtained (%d, board at 0x%x)\n",
				irq, cosa->datareg);
			err = -1;
			goto err_out;
		}
		if (irq == 0) {
			printk (KERN_INFO "cosa IRQ autoprobe: no interrupt obtained (board at 0x%x)\n",
				cosa->datareg);
		/*	return -1; */
		}
	}

	cosa->irq = irq;
	cosa->num = nr_cards;
	cosa->usage = 0;
	cosa->nchannels = 2;	/* FIXME: how to determine this? */

	if (request_irq(cosa->irq, cosa_interrupt, 0, cosa->type, cosa)) {
		err = -1;
		goto err_out;
	}
	if (request_dma(cosa->dma, cosa->type)) {
		err = -1;
		goto err_out1;
	}
	
	cosa->bouncebuf = kmalloc(COSA_MTU, GFP_KERNEL|GFP_DMA);
	if (!cosa->bouncebuf) {
		err = -ENOMEM;
		goto err_out2;
	}
	sprintf(cosa->name, "cosa%d", cosa->num);

	/* Initialize the per-channel data */
	cosa->chan = kmalloc(sizeof(struct channel_data)*cosa->nchannels,
			     GFP_KERNEL);
	if (!cosa->chan) {
	        err = -ENOMEM;
		goto err_out3;
	}
	memset(cosa->chan, 0, sizeof(struct channel_data)*cosa->nchannels);
	for (i=0; i<cosa->nchannels; i++) {
		cosa->chan[i].cosa = cosa;
		cosa->chan[i].num = i;
		channel_init(cosa->chan+i);
	}

	printk (KERN_INFO "cosa%d: %s (%s at 0x%x irq %d dma %d), %d channels\n",
		cosa->num, cosa->id_string, cosa->type,
		cosa->datareg, cosa->irq, cosa->dma, cosa->nchannels);

	return nr_cards++;
err_out3:
	kfree(cosa->bouncebuf);
err_out2:
	free_dma(cosa->dma);
err_out1:
	free_irq(cosa->irq, cosa);
err_out:	
	release_region(cosa->datareg,is_8bit(cosa)?2:4);
	printk(KERN_NOTICE "cosa%d: allocating resources failed\n",
	       cosa->num);
	return err;
}


/*---------- SPPP/HDLC netdevice ---------- */

static void cosa_setup(struct net_device *d)
{
	d->open = cosa_sppp_open;
	d->stop = cosa_sppp_close;
	d->hard_start_xmit = cosa_sppp_tx;
	d->do_ioctl = cosa_sppp_ioctl;
	d->get_stats = cosa_net_stats;
	d->tx_timeout = cosa_sppp_timeout;
	d->watchdog_timeo = TX_TIMEOUT;
}

static void sppp_channel_init(struct channel_data *chan)
{
	struct net_device *d;
	chan->if_ptr = &chan->pppdev;
	d = alloc_netdev(0, chan->name, cosa_setup);
	if (!d) {
		printk(KERN_WARNING "%s: alloc_netdev failed.\n", chan->name);
		return;
	}
	chan->pppdev.dev = d;
	d->base_addr = chan->cosa->datareg;
	d->irq = chan->cosa->irq;
	d->dma = chan->cosa->dma;
	d->priv = chan;
	sppp_attach(&chan->pppdev);
	if (register_netdev(d)) {
		printk(KERN_WARNING "%s: register_netdev failed.\n", d->name);
		sppp_detach(d);
		free_netdev(d);
		chan->pppdev.dev = NULL;
		return;
	}
}

static void sppp_channel_delete(struct channel_data *chan)
{
	unregister_netdev(chan->pppdev.dev);
	sppp_detach(chan->pppdev.dev);
	free_netdev(chan->pppdev.dev);
	chan->pppdev.dev = NULL;
}

static int cosa_sppp_open(struct net_device *d)
{
	struct channel_data *chan = d->priv;
	int err;
	unsigned long flags;

	if (!(chan->cosa->firmware_status & COSA_FW_START)) {
		printk(KERN_NOTICE "%s: start the firmware first (status %d)\n",
			chan->cosa->name, chan->cosa->firmware_status);
		return -EPERM;
	}
	spin_lock_irqsave(&chan->cosa->lock, flags);
	if (chan->usage != 0) {
		printk(KERN_WARNING "%s: sppp_open called with usage count %d\n",
			chan->name, chan->usage);
		spin_unlock_irqrestore(&chan->cosa->lock, flags);
		return -EBUSY;
	}
	chan->setup_rx = sppp_setup_rx;
	chan->tx_done = sppp_tx_done;
	chan->rx_done = sppp_rx_done;
	chan->usage=-1;
	chan->cosa->usage++;
	spin_unlock_irqrestore(&chan->cosa->lock, flags);

	err = sppp_open(d);
	if (err) {
		spin_lock_irqsave(&chan->cosa->lock, flags);
		chan->usage=0;
		chan->cosa->usage--;
		
		spin_unlock_irqrestore(&chan->cosa->lock, flags);
		return err;
	}

	netif_start_queue(d);
	cosa_enable_rx(chan);
	return 0;
}

static int cosa_sppp_tx(struct sk_buff *skb, struct net_device *dev)
{
	struct channel_data *chan = dev->priv;

	netif_stop_queue(dev);

	chan->tx_skb = skb;
	cosa_start_tx(chan, skb->data, skb->len);
	return 0;
}

static void cosa_sppp_timeout(struct net_device *dev)
{
	struct channel_data *chan = dev->priv;

	if (test_bit(RXBIT, &chan->cosa->rxtx)) {
		chan->stats.rx_errors++;
		chan->stats.rx_missed_errors++;
	} else {
		chan->stats.tx_errors++;
		chan->stats.tx_aborted_errors++;
	}
	cosa_kick(chan->cosa);
	if (chan->tx_skb) {
		dev_kfree_skb(chan->tx_skb);
		chan->tx_skb = NULL;
	}
	netif_wake_queue(dev);
}

static int cosa_sppp_close(struct net_device *d)
{
	struct channel_data *chan = d->priv;
	unsigned long flags;

	netif_stop_queue(d);
	sppp_close(d);
	cosa_disable_rx(chan);
	spin_lock_irqsave(&chan->cosa->lock, flags);
	if (chan->rx_skb) {
		kfree_skb(chan->rx_skb);
		chan->rx_skb = NULL;
	}
	if (chan->tx_skb) {
		kfree_skb(chan->tx_skb);
		chan->tx_skb = NULL;
	}
	chan->usage=0;
	chan->cosa->usage--;
	spin_unlock_irqrestore(&chan->cosa->lock, flags);
	return 0;
}

static char *sppp_setup_rx(struct channel_data *chan, int size)
{
	/*
	 * We can safely fall back to non-dma-able memory, because we have
	 * the cosa->bouncebuf pre-allocated.
	 */
	if (chan->rx_skb)
		kfree_skb(chan->rx_skb);
	chan->rx_skb = dev_alloc_skb(size);
	if (chan->rx_skb == NULL) {
		printk(KERN_NOTICE "%s: Memory squeeze, dropping packet\n",
			chan->name);
		chan->stats.rx_dropped++;
		return NULL;
	}
	chan->pppdev.dev->trans_start = jiffies;
	return skb_put(chan->rx_skb, size);
}

static int sppp_rx_done(struct channel_data *chan)
{
	if (!chan->rx_skb) {
		printk(KERN_WARNING "%s: rx_done with empty skb!\n",
			chan->name);
		chan->stats.rx_errors++;
		chan->stats.rx_frame_errors++;
		return 0;
	}
	chan->rx_skb->protocol = htons(ETH_P_WAN_PPP);
	chan->rx_skb->dev = chan->pppdev.dev;
	chan->rx_skb->mac.raw = chan->rx_skb->data;
	chan->stats.rx_packets++;
	chan->stats.rx_bytes += chan->cosa->rxsize;
	netif_rx(chan->rx_skb);
	chan->rx_skb = NULL;
	chan->pppdev.dev->last_rx = jiffies;
	return 0;
}

/* ARGSUSED */
static int sppp_tx_done(struct channel_data *chan, int size)
{
	if (!chan->tx_skb) {
		printk(KERN_WARNING "%s: tx_done with empty skb!\n",
			chan->name);
		chan->stats.tx_errors++;
		chan->stats.tx_aborted_errors++;
		return 1;
	}
	dev_kfree_skb_irq(chan->tx_skb);
	chan->tx_skb = NULL;
	chan->stats.tx_packets++;
	chan->stats.tx_bytes += size;
	netif_wake_queue(chan->pppdev.dev);
	return 1;
}

static struct net_device_stats *cosa_net_stats(struct net_device *dev)
{
	struct channel_data *chan = dev->priv;
	return &chan->stats;
}


/*---------- Character device ---------- */

static void chardev_channel_init(struct channel_data *chan)
{
	init_MUTEX(&chan->rsem);
	init_MUTEX(&chan->wsem);
}

static ssize_t cosa_read(struct file *file,
	char __user *buf, size_t count, loff_t *ppos)
{
	DECLARE_WAITQUEUE(wait, current);
	unsigned long flags;
	struct channel_data *chan = file->private_data;
	struct cosa_data *cosa = chan->cosa;
	char *kbuf;

	if (!(cosa->firmware_status & COSA_FW_START)) {
		printk(KERN_NOTICE "%s: start the firmware first (status %d)\n",
			cosa->name, cosa->firmware_status);
		return -EPERM;
	}
	if (down_interruptible(&chan->rsem))
		return -ERESTARTSYS;
	
	if ((chan->rxdata = kmalloc(COSA_MTU, GFP_DMA|GFP_KERNEL)) == NULL) {
		printk(KERN_INFO "%s: cosa_read() - OOM\n", cosa->name);
		up(&chan->rsem);
		return -ENOMEM;
	}

	chan->rx_status = 0;
	cosa_enable_rx(chan);
	spin_lock_irqsave(&cosa->lock, flags);
	add_wait_queue(&chan->rxwaitq, &wait);
	while(!chan->rx_status) {
		current->state = TASK_INTERRUPTIBLE;
		spin_unlock_irqrestore(&cosa->lock, flags);
		schedule();
		spin_lock_irqsave(&cosa->lock, flags);
		if (signal_pending(current) && chan->rx_status == 0) {
			chan->rx_status = 1;
			remove_wait_queue(&chan->rxwaitq, &wait);
			current->state = TASK_RUNNING;
			spin_unlock_irqrestore(&cosa->lock, flags);
			up(&chan->rsem);
			return -ERESTARTSYS;
		}
	}
	remove_wait_queue(&chan->rxwaitq, &wait);
	current->state = TASK_RUNNING;
	kbuf = chan->rxdata;
	count = chan->rxsize;
	spin_unlock_irqrestore(&cosa->lock, flags);
	up(&chan->rsem);

	if (copy_to_user(buf, kbuf, count)) {
		kfree(kbuf);
		return -EFAULT;
	}
	kfree(kbuf);
	return count;
}

static char *chrdev_setup_rx(struct channel_data *chan, int size)
{
	/* Expect size <= COSA_MTU */
	chan->rxsize = size;
	return chan->rxdata;
}

static int chrdev_rx_done(struct channel_data *chan)
{
	if (chan->rx_status) { /* Reader has died */
		kfree(chan->rxdata);
		up(&chan->wsem);
	}
	chan->rx_status = 1;
	wake_up_interruptible(&chan->rxwaitq);
	return 1;
}


static ssize_t cosa_write(struct file *file,
	const char __user *buf, size_t count, loff_t *ppos)
{
	DECLARE_WAITQUEUE(wait, current);
	struct channel_data *chan = file->private_data;
	struct cosa_data *cosa = chan->cosa;
	unsigned long flags;
	char *kbuf;

	if (!(cosa->firmware_status & COSA_FW_START)) {
		printk(KERN_NOTICE "%s: start the firmware first (status %d)\n",
			cosa->name, cosa->firmware_status);
		return -EPERM;
	}
	if (down_interruptible(&chan->wsem))
		return -ERESTARTSYS;

	if (count > COSA_MTU)
		count = COSA_MTU;
	
	/* Allocate the buffer */
	if ((kbuf = kmalloc(count, GFP_KERNEL|GFP_DMA)) == NULL) {
		printk(KERN_NOTICE "%s: cosa_write() OOM - dropping packet\n",
			cosa->name);
		up(&chan->wsem);
		return -ENOMEM;
	}
	if (copy_from_user(kbuf, buf, count)) {
		up(&chan->wsem);
		kfree(kbuf);
		return -EFAULT;
	}
	chan->tx_status=0;
	cosa_start_tx(chan, kbuf, count);

	spin_lock_irqsave(&cosa->lock, flags);
	add_wait_queue(&chan->txwaitq, &wait);
	while(!chan->tx_status) {
		current->state = TASK_INTERRUPTIBLE;
		spin_unlock_irqrestore(&cosa->lock, flags);
		schedule();
		spin_lock_irqsave(&cosa->lock, flags);
		if (signal_pending(current) && chan->tx_status == 0) {
			chan->tx_status = 1;
			remove_wait_queue(&chan->txwaitq, &wait);
			current->state = TASK_RUNNING;
			chan->tx_status = 1;
			spin_unlock_irqrestore(&cosa->lock, flags);
			return -ERESTARTSYS;
		}
	}
	remove_wait_queue(&chan->txwaitq, &wait);
	current->state = TASK_RUNNING;
	up(&chan->wsem);
	spin_unlock_irqrestore(&cosa->lock, flags);
	kfree(kbuf);
	return count;
}

static int chrdev_tx_done(struct channel_data *chan, int size)
{
	if (chan->tx_status) { /* Writer was interrupted */
		kfree(chan->txbuf);
		up(&chan->wsem);
	}
	chan->tx_status = 1;
	wake_up_interruptible(&chan->txwaitq);
	return 1;
}

static unsigned int cosa_poll(struct file *file, poll_table *poll)
{
	printk(KERN_INFO "cosa_poll is here\n");
	return 0;
}

static int cosa_open(struct inode *inode, struct file *file)
{
	struct cosa_data *cosa;
	struct channel_data *chan;
	unsigned long flags;
	int n;

	if ((n=iminor(file->f_dentry->d_inode)>>CARD_MINOR_BITS)
		>= nr_cards)
		return -ENODEV;
	cosa = cosa_cards+n;

	if ((n=iminor(file->f_dentry->d_inode)
		& ((1<<CARD_MINOR_BITS)-1)) >= cosa->nchannels)
		return -ENODEV;
	chan = cosa->chan + n;
	
	file->private_data = chan;

	spin_lock_irqsave(&cosa->lock, flags);

	if (chan->usage < 0) { /* in netdev mode */
		spin_unlock_irqrestore(&cosa->lock, flags);
		return -EBUSY;
	}
	cosa->usage++;
	chan->usage++;

	chan->tx_done = chrdev_tx_done;
	chan->setup_rx = chrdev_setup_rx;
	chan->rx_done = chrdev_rx_done;
	spin_unlock_irqrestore(&cosa->lock, flags);
	return 0;
}

static int cosa_release(struct inode *inode, struct file *file)
{
	struct channel_data *channel = file->private_data;
	struct cosa_data *cosa;
	unsigned long flags;

	cosa = channel->cosa;
	spin_lock_irqsave(&cosa->lock, flags);
	cosa->usage--;
	channel->usage--;
	spin_unlock_irqrestore(&cosa->lock, flags);
	return 0;
}

#ifdef COSA_FASYNC_WORKING
static struct fasync_struct *fasync[256] = { NULL, };

/* To be done ... */
static int cosa_fasync(struct inode *inode, struct file *file, int on)
{
        int port = iminor(inode);
        int rv = fasync_helper(inode, file, on, &fasync[port]);
        return rv < 0 ? rv : 0;
}
#endif


/* ---------- Ioctls ---------- */

/*
 * Ioctl subroutines can safely be made inline, because they are called
 * only from cosa_ioctl().
 */
static inline int cosa_reset(struct cosa_data *cosa)
{
	char idstring[COSA_MAX_ID_STRING];
	if (cosa->usage > 1)
		printk(KERN_INFO "cosa%d: WARNING: reset requested with cosa->usage > 1 (%d). Odd things may happen.\n",
			cosa->num, cosa->usage);
	cosa->firmware_status &= ~(COSA_FW_RESET|COSA_FW_START);
	if (cosa_reset_and_read_id(cosa, idstring) < 0) {
		printk(KERN_NOTICE "cosa%d: reset failed\n", cosa->num);
		return -EIO;
	}
	printk(KERN_INFO "cosa%d: resetting device: %s\n", cosa->num,
		idstring);
	cosa->firmware_status |= COSA_FW_RESET;
	return 0;
}

/* High-level function to download data into COSA memory. Calls download() */
static inline int cosa_download(struct cosa_data *cosa, void __user *arg)
{
	struct cosa_download d;
	int i;

	if (cosa->usage > 1)
		printk(KERN_INFO "%s: WARNING: download of microcode requested with cosa->usage > 1 (%d). Odd things may happen.\n",
			cosa->name, cosa->usage);
	if (!(cosa->firmware_status & COSA_FW_RESET)) {
		printk(KERN_NOTICE "%s: reset the card first (status %d).\n",
			cosa->name, cosa->firmware_status);
		return -EPERM;
	}
	
	if (copy_from_user(&d, arg, sizeof(d)))
		return -EFAULT;

	if (d.addr < 0 || d.addr > COSA_MAX_FIRMWARE_SIZE)
		return -EINVAL;
	if (d.len < 0 || d.len > COSA_MAX_FIRMWARE_SIZE)
		return -EINVAL;


	/* If something fails, force the user to reset the card */
	cosa->firmware_status &= ~(COSA_FW_RESET|COSA_FW_DOWNLOAD);

	i = download(cosa, d.code, d.len, d.addr);
	if (i < 0) {
		printk(KERN_NOTICE "cosa%d: microcode download failed: %d\n",
			cosa->num, i);
		return -EIO;
	}
	printk(KERN_INFO "cosa%d: downloading microcode - 0x%04x bytes at 0x%04x\n",
		cosa->num, d.len, d.addr);
	cosa->firmware_status |= COSA_FW_RESET|COSA_FW_DOWNLOAD;
	return 0;
}

/* High-level function to read COSA memory. Calls readmem() */
static inline int cosa_readmem(struct cosa_data *cosa, void __user *arg)
{
	struct cosa_download d;
	int i;

	if (cosa->usage > 1)
		printk(KERN_INFO "cosa%d: WARNING: readmem requested with "
			"cosa->usage > 1 (%d). Odd things may happen.\n",
			cosa->num, cosa->usage);
	if (!(cosa->firmware_status & COSA_FW_RESET)) {
		printk(KERN_NOTICE "%s: reset the card first (status %d).\n",
			cosa->name, cosa->firmware_status);
		return -EPERM;
	}

	if (copy_from_user(&d, arg, sizeof(d)))
		return -EFAULT;

	/* If something fails, force the user to reset the card */
	cosa->firmware_status &= ~COSA_FW_RESET;

	i = readmem(cosa, d.code, d.len, d.addr);
	if (i < 0) {
		printk(KERN_NOTICE "cosa%d: reading memory failed: %d\n",
			cosa->num, i);
		return -EIO;
	}
	printk(KERN_INFO "cosa%d: reading card memory - 0x%04x bytes at 0x%04x\n",
		cosa->num, d.len, d.addr);
	cosa->firmware_status |= COSA_FW_RESET;
	return 0;
}

/* High-level function to start microcode. Calls startmicrocode(). */
static inline int cosa_start(struct cosa_data *cosa, int address)
{
	int i;

	if (cosa->usage > 1)
		printk(KERN_INFO "cosa%d: WARNING: start microcode requested with cosa->usage > 1 (%d). Odd things may happen.\n",
			cosa->num, cosa->usage);

	if ((cosa->firmware_status & (COSA_FW_RESET|COSA_FW_DOWNLOAD))
		!= (COSA_FW_RESET|COSA_FW_DOWNLOAD)) {
		printk(KERN_NOTICE "%s: download the microcode and/or reset the card first (status %d).\n",
			cosa->name, cosa->firmware_status);
		return -EPERM;
	}
	cosa->firmware_status &= ~COSA_FW_RESET;
	if ((i=startmicrocode(cosa, address)) < 0) {
		printk(KERN_NOTICE "cosa%d: start microcode at 0x%04x failed: %d\n",
			cosa->num, address, i);
		return -EIO;
	}
	printk(KERN_INFO "cosa%d: starting microcode at 0x%04x\n",
		cosa->num, address);
	cosa->startaddr = address;
	cosa->firmware_status |= COSA_FW_START;
	return 0;
}
		
/* Buffer of size at least COSA_MAX_ID_STRING is expected */
static inline int cosa_getidstr(struct cosa_data *cosa, char __user *string)
{
	int l = strlen(cosa->id_string)+1;
	if (copy_to_user(string, cosa->id_string, l))
		return -EFAULT;
	return l;
}

/* Buffer of size at least COSA_MAX_ID_STRING is expected */
static inline int cosa_gettype(struct cosa_data *cosa, char __user *string)
{
	int l = strlen(cosa->type)+1;
	if (copy_to_user(string, cosa->type, l))
		return -EFAULT;
	return l;
}

static int cosa_ioctl_common(struct cosa_data *cosa,
	struct channel_data *channel, unsigned int cmd, unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	switch(cmd) {
	case COSAIORSET:	/* Reset the device */
		if (!capable(CAP_NET_ADMIN))
			return -EACCES;
		return cosa_reset(cosa);
	case COSAIOSTRT:	/* Start the firmware */
		if (!capable(CAP_SYS_RAWIO))
			return -EACCES;
		return cosa_start(cosa, arg);
	case COSAIODOWNLD:	/* Download the firmware */
		if (!capable(CAP_SYS_RAWIO))
			return -EACCES;
		
		return cosa_download(cosa, argp);
	case COSAIORMEM:
		if (!capable(CAP_SYS_RAWIO))
			return -EACCES;
		return cosa_readmem(cosa, argp);
	case COSAIORTYPE:
		return cosa_gettype(cosa, argp);
	case COSAIORIDSTR:
		return cosa_getidstr(cosa, argp);
	case COSAIONRCARDS:
		return nr_cards;
	case COSAIONRCHANS:
		return cosa->nchannels;
	case COSAIOBMSET:
		if (!capable(CAP_SYS_RAWIO))
			return -EACCES;
		if (is_8bit(cosa))
			return -EINVAL;
		if (arg != COSA_BM_OFF && arg != COSA_BM_ON)
			return -EINVAL;
		cosa->busmaster = arg;
		return 0;
	case COSAIOBMGET:
		return cosa->busmaster;
	}
	return -ENOIOCTLCMD;
}

static int cosa_sppp_ioctl(struct net_device *dev, struct ifreq *ifr,
	int cmd)
{
	int rv;
	struct channel_data *chan = dev->priv;
	rv = cosa_ioctl_common(chan->cosa, chan, cmd, (unsigned long)ifr->ifr_data);
	if (rv == -ENOIOCTLCMD) {
		return sppp_do_ioctl(dev, ifr, cmd);
	}
	return rv;
}

static int cosa_chardev_ioctl(struct inode *inode, struct file *file,
	unsigned int cmd, unsigned long arg)
{
	struct channel_data *channel = file->private_data;
	struct cosa_data *cosa = channel->cosa;
	return cosa_ioctl_common(cosa, channel, cmd, arg);
}


/*---------- HW layer interface ---------- */

/*
 * The higher layer can bind itself to the HW layer by setting the callbacks
 * in the channel_data structure and by using these routines.
 */
static void cosa_enable_rx(struct channel_data *chan)
{
	struct cosa_data *cosa = chan->cosa;

	if (!test_and_set_bit(chan->num, &cosa->rxbitmap))
		put_driver_status(cosa);
}

static void cosa_disable_rx(struct channel_data *chan)
{
	struct cosa_data *cosa = chan->cosa;

	if (test_and_clear_bit(chan->num, &cosa->rxbitmap))
		put_driver_status(cosa);
}

/*
 * FIXME: This routine probably should check for cosa_start_tx() called when
 * the previous transmit is still unfinished. In this case the non-zero
 * return value should indicate to the caller that the queuing(sp?) up
 * the transmit has failed.
 */
static int cosa_start_tx(struct channel_data *chan, char *buf, int len)
{
	struct cosa_data *cosa = chan->cosa;
	unsigned long flags;
#ifdef DEBUG_DATA
	int i;

	printk(KERN_INFO "cosa%dc%d: starting tx(0x%x)", chan->cosa->num,
		chan->num, len);
	for (i=0; i<len; i++)
		printk(" %02x", buf[i]&0xff);
	printk("\n");
#endif
	spin_lock_irqsave(&cosa->lock, flags);
	chan->txbuf = buf;
	chan->txsize = len;
	if (len > COSA_MTU)
		chan->txsize = COSA_MTU;
	spin_unlock_irqrestore(&cosa->lock, flags);

	/* Tell the firmware we are ready */
	set_bit(chan->num, &cosa->txbitmap);
	put_driver_status(cosa);

	return 0;
}

static void put_driver_status(struct cosa_data *cosa)
{
	unsigned long flags;
	int status;

	spin_lock_irqsave(&cosa->lock, flags);

	status = (cosa->rxbitmap ? DRIVER_RX_READY : 0)
		| (cosa->txbitmap ? DRIVER_TX_READY : 0)
		| (cosa->txbitmap? ~(cosa->txbitmap<<DRIVER_TXMAP_SHIFT)
			&DRIVER_TXMAP_MASK : 0);
	if (!cosa->rxtx) {
		if (cosa->rxbitmap|cosa->txbitmap) {
			if (!cosa->enabled) {
				cosa_putstatus(cosa, SR_RX_INT_ENA);
#ifdef DEBUG_IO
				debug_status_out(cosa, SR_RX_INT_ENA);
#endif
				cosa->enabled = 1;
			}
		} else if (cosa->enabled) {
			cosa->enabled = 0;
			cosa_putstatus(cosa, 0);
#ifdef DEBUG_IO
			debug_status_out(cosa, 0);
#endif
		}
		cosa_putdata8(cosa, status);
#ifdef DEBUG_IO
		debug_data_cmd(cosa, status);
#endif
	}
	spin_unlock_irqrestore(&cosa->lock, flags);
}

static void put_driver_status_nolock(struct cosa_data *cosa)
{
	int status;

	status = (cosa->rxbitmap ? DRIVER_RX_READY : 0)
		| (cosa->txbitmap ? DRIVER_TX_READY : 0)
		| (cosa->txbitmap? ~(cosa->txbitmap<<DRIVER_TXMAP_SHIFT)
			&DRIVER_TXMAP_MASK : 0);

	if (cosa->rxbitmap|cosa->txbitmap) {
		cosa_putstatus(cosa, SR_RX_INT_ENA);
#ifdef DEBUG_IO
		debug_status_out(cosa, SR_RX_INT_ENA);
#endif
		cosa->enabled = 1;
	} else {
		cosa_putstatus(cosa, 0);
#ifdef DEBUG_IO
		debug_status_out(cosa, 0);
#endif
		cosa->enabled = 0;
	}
	cosa_putdata8(cosa, status);
#ifdef DEBUG_IO
	debug_data_cmd(cosa, status);
#endif
}

/*
 * The "kickme" function: When the DMA times out, this is called to
 * clean up the driver status.
 * FIXME: Preliminary support, the interface is probably wrong.
 */
static void cosa_kick(struct cosa_data *cosa)
{
	unsigned long flags, flags1;
	char *s = "(probably) IRQ";

	if (test_bit(RXBIT, &cosa->rxtx))
		s = "RX DMA";
	if (test_bit(TXBIT, &cosa->rxtx))
		s = "TX DMA";

	printk(KERN_INFO "%s: %s timeout - restarting.\n", cosa->name, s); 
	spin_lock_irqsave(&cosa->lock, flags);
	cosa->rxtx = 0;

	flags1 = claim_dma_lock();
	disable_dma(cosa->dma);
	clear_dma_ff(cosa->dma);
	release_dma_lock(flags1);

	/* FIXME: Anything else? */
	udelay(100);
	cosa_putstatus(cosa, 0);
	udelay(100);
	(void) cosa_getdata8(cosa);
	udelay(100);
	cosa_putdata8(cosa, 0);
	udelay(100);
	put_driver_status_nolock(cosa);
	spin_unlock_irqrestore(&cosa->lock, flags);
}

/*
 * Check if the whole buffer is DMA-able. It means it is below the 16M of
 * physical memory and doesn't span the 64k boundary. For now it seems
 * SKB's never do this, but we'll check this anyway.
 */
static int cosa_dma_able(struct channel_data *chan, char *buf, int len)
{
	static int count;
	unsigned long b = (unsigned long)buf;
	if (b+len >= MAX_DMA_ADDRESS)
		return 0;
	if ((b^ (b+len)) & 0x10000) {
		if (count++ < 5)
			printk(KERN_INFO "%s: packet spanning a 64k boundary\n",
				chan->name);
		return 0;
	}
	return 1;
}


/* ---------- The SRP/COSA ROM monitor functions ---------- */

/*
 * Downloading SRP microcode: say "w" to SRP monitor, it answers by "w=",
 * drivers need to say 4-digit hex number meaning start address of the microcode
 * separated by a single space. Monitor replies by saying " =". Now driver
 * has to write 4-digit hex number meaning the last byte address ended
 * by a single space. Monitor has to reply with a space. Now the download
 * begins. After the download monitor replies with "\r\n." (CR LF dot).
 */
static int download(struct cosa_data *cosa, const char __user *microcode, int length, int address)
{
	int i;

	if (put_wait_data(cosa, 'w') == -1) return -1;
	if ((i=get_wait_data(cosa)) != 'w') { printk("dnld: 0x%04x\n",i); return -2;}
	if (get_wait_data(cosa) != '=') return -3;

	if (puthexnumber(cosa, address) < 0) return -4;
	if (put_wait_data(cosa, ' ') == -1) return -10;
	if (get_wait_data(cosa) != ' ') return -11;
	if (get_wait_data(cosa) != '=') return -12;

	if (puthexnumber(cosa, address+length-1) < 0) return -13;
	if (put_wait_data(cosa, ' ') == -1) return -18;
	if (get_wait_data(cosa) != ' ') return -19;

	while (length--) {
		char c;
#ifndef SRP_DOWNLOAD_AT_BOOT
		if (get_user(c, microcode))
			return -23; /* ??? */
#else
		c = *microcode;
#endif
		if (put_wait_data(cosa, c) == -1)
			return -20;
		microcode++;
	}

	if (get_wait_data(cosa) != '\r') return -21;
	if (get_wait_data(cosa) != '\n') return -22;
	if (get_wait_data(cosa) != '.') return -23;
#if 0
	printk(KERN_DEBUG "cosa%d: download completed.\n", cosa->num);
#endif
	return 0;
}


/*
 * Starting microcode is done via the "g" command of the SRP monitor.
 * The chat should be the following: "g" "g=" "<addr><CR>"
 * "<CR><CR><LF><CR><LF>".
 */
static int startmicrocode(struct cosa_data *cosa, int address)
{
	if (put_wait_data(cosa, 'g') == -1) return -1;
	if (get_wait_data(cosa) != 'g') return -2;
	if (get_wait_data(cosa) != '=') return -3;

	if (puthexnumber(cosa, address) < 0) return -4;
	if (put_wait_data(cosa, '\r') == -1) return -5;
	
	if (get_wait_data(cosa) != '\r') return -6;
	if (get_wait_data(cosa) != '\r') return -7;
	if (get_wait_data(cosa) != '\n') return -8;
	if (get_wait_data(cosa) != '\r') return -9;
	if (get_wait_data(cosa) != '\n') return -10;
#if 0
	printk(KERN_DEBUG "cosa%d: microcode started\n", cosa->num);
#endif
	return 0;
}

/*
 * Reading memory is done via the "r" command of the SRP monitor.
 * The chat is the following "r" "r=" "<addr> " " =" "<last_byte> " " "
 * Then driver can read the data and the conversation is finished
 * by SRP monitor sending "<CR><LF>." (dot at the end).
 *
 * This routine is not needed during the normal operation and serves
 * for debugging purposes only.
 */
static int readmem(struct cosa_data *cosa, char __user *microcode, int length, int address)
{
	if (put_wait_data(cosa, 'r') == -1) return -1;
	if ((get_wait_data(cosa)) != 'r') return -2;
	if ((get_wait_data(cosa)) != '=') return -3;

	if (puthexnumber(cosa, address) < 0) return -4;
	if (put_wait_data(cosa, ' ') == -1) return -5;
	if (get_wait_data(cosa) != ' ') return -6;
	if (get_wait_data(cosa) != '=') return -7;

	if (puthexnumber(cosa, address+length-1) < 0) return -8;
	if (put_wait_data(cosa, ' ') == -1) return -9;
	if (get_wait_data(cosa) != ' ') return -10;

	while (length--) {
		char c;
		int i;
		if ((i=get_wait_data(cosa)) == -1) {
			printk (KERN_INFO "cosa: 0x%04x bytes remaining\n",
				length);
			return -11;
		}
		c=i;
#if 1
		if (put_user(c, microcode))
			return -23; /* ??? */
#else
		*microcode = c;
#endif
		microcode++;
	}

	if (get_wait_data(cosa) != '\r') return -21;
	if (get_wait_data(cosa) != '\n') return -22;
	if (get_wait_data(cosa) != '.') return -23;
#if 0
	printk(KERN_DEBUG "cosa%d: readmem completed.\n", cosa->num);
#endif
	return 0;
}

/*
 * This function resets the device and reads the initial prompt
 * of the device's ROM monitor.
 */
static int cosa_reset_and_read_id(struct cosa_data *cosa, char *idstring)
{
	int i=0, id=0, prev=0, curr=0;

	/* Reset the card ... */
	cosa_putstatus(cosa, 0);
	cosa_getdata8(cosa);
	cosa_putstatus(cosa, SR_RST);
#ifdef MODULE
	msleep(500);
#else
	udelay(5*100000);
#endif
	/* Disable all IRQs from the card */
	cosa_putstatus(cosa, 0);

	/*
	 * Try to read the ID string. The card then prints out the
	 * identification string ended by the "\n\x2e".
	 *
	 * The following loop is indexed through i (instead of id)
	 * to avoid looping forever when for any reason
	 * the port returns '\r', '\n' or '\x2e' permanently.
	 */
	for (i=0; i<COSA_MAX_ID_STRING-1; i++, prev=curr) {
		if ((curr = get_wait_data(cosa)) == -1) {
			return -1;
		}
		curr &= 0xff;
		if (curr != '\r' && curr != '\n' && curr != 0x2e)
			idstring[id++] = curr;
		if (curr == 0x2e && prev == '\n')
			break;
	}
	/* Perhaps we should fail when i==COSA_MAX_ID_STRING-1 ? */
	idstring[id] = '\0';
	return id;
}


/* ---------- Auxiliary routines for COSA/SRP monitor ---------- */

/*
 * This routine gets the data byte from the card waiting for the SR_RX_RDY
 * bit to be set in a loop. It should be used in the exceptional cases
 * only (for example when resetting the card or downloading the firmware.
 */
static int get_wait_data(struct cosa_data *cosa)
{
	int retries = 1000;

	while (--retries) {
		/* read data and return them */
		if (cosa_getstatus(cosa) & SR_RX_RDY) {
			short r;
			r = cosa_getdata8(cosa);
#if 0
			printk(KERN_INFO "cosa: get_wait_data returning after %d retries\n", 999-retries);
#endif
			return r;
		}
		/* sleep if not ready to read */
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(1);
	}
	printk(KERN_INFO "cosa: timeout in get_wait_data (status 0x%x)\n",
		cosa_getstatus(cosa));
	return -1;
}

/*
 * This routine puts the data byte to the card waiting for the SR_TX_RDY
 * bit to be set in a loop. It should be used in the exceptional cases
 * only (for example when resetting the card or downloading the firmware).
 */
static int put_wait_data(struct cosa_data *cosa, int data)
{
	int retries = 1000;
	while (--retries) {
		/* read data and return them */
		if (cosa_getstatus(cosa) & SR_TX_RDY) {
			cosa_putdata8(cosa, data);
#if 0
			printk(KERN_INFO "Putdata: %d retries\n", 999-retries);
#endif
			return 0;
		}
#if 0
		/* sleep if not ready to read */
		current->state = TASK_INTERRUPTIBLE;
		schedule_timeout(1);
#endif
	}
	printk(KERN_INFO "cosa%d: timeout in put_wait_data (status 0x%x)\n",
		cosa->num, cosa_getstatus(cosa));
	return -1;
}
	
/* 
 * The following routine puts the hexadecimal number into the SRP monitor
 * and verifies the proper echo of the sent bytes. Returns 0 on success,
 * negative number on failure (-1,-3,-5,-7) means that put_wait_data() failed,
 * (-2,-4,-6,-8) means that reading echo failed.
 */
static int puthexnumber(struct cosa_data *cosa, int number)
{
	char temp[5];
	int i;

	/* Well, I should probably replace this by something faster. */
	sprintf(temp, "%04X", number);
	for (i=0; i<4; i++) {
		if (put_wait_data(cosa, temp[i]) == -1) {
			printk(KERN_NOTICE "cosa%d: puthexnumber failed to write byte %d\n",
				cosa->num, i);
			return -1-2*i;
		}
		if (get_wait_data(cosa) != temp[i]) {
			printk(KERN_NOTICE "cosa%d: puthexhumber failed to read echo of byte %d\n",
				cosa->num, i);
			return -2-2*i;
		}
	}
	return 0;
}


/* ---------- Interrupt routines ---------- */

/*
 * There are three types of interrupt:
 * At the beginning of transmit - this handled is in tx_interrupt(),
 * at the beginning of receive - it is in rx_interrupt() and
 * at the end of transmit/receive - it is the eot_interrupt() function.
 * These functions are multiplexed by cosa_interrupt() according to the
 * COSA status byte. I have moved the rx/tx/eot interrupt handling into
 * separate functions to make it more readable. These functions are inline,
 * so there should be no overhead of function call.
 * 
 * In the COSA bus-master mode, we need to tell the card the address of a
 * buffer. Unfortunately, COSA may be too slow for us, so we must busy-wait.
 * It's time to use the bottom half :-(
 */

/*
 * Transmit interrupt routine - called when COSA is willing to obtain
 * data from the OS. The most tricky part of the routine is selection
 * of channel we (OS) want to send packet for. For SRP we should probably
 * use the round-robin approach. The newer COSA firmwares have a simple
 * flow-control - in the status word has bits 2 and 3 set to 1 means that the
 * channel 0 or 1 doesn't want to receive data.
 *
 * It seems there is a bug in COSA firmware (need to trace it further):
 * When the driver status says that the kernel has no more data for transmit
 * (e.g. at the end of TX DMA) and then the kernel changes its mind
 * (e.g. new packet is queued to hard_start_xmit()), the card issues
 * the TX interrupt but does not mark the channel as ready-to-transmit.
 * The fix seems to be to push the packet to COSA despite its request.
 * We first try to obey the card's opinion, and then fall back to forced TX.
 */
static inline void tx_interrupt(struct cosa_data *cosa, int status)
{
	unsigned long flags, flags1;
#ifdef DEBUG_IRQS
	printk(KERN_INFO "cosa%d: SR_DOWN_REQUEST status=0x%04x\n",
		cosa->num, status);
#endif
	spin_lock_irqsave(&cosa->lock, flags);
	set_bit(TXBIT, &cosa->rxtx);
	if (!test_bit(IRQBIT, &cosa->rxtx)) {
		/* flow control, see the comment above */
		int i=0;
		if (!cosa->txbitmap) {
			printk(KERN_WARNING "%s: No channel wants data "
				"in TX IRQ. Expect DMA timeout.",
				cosa->name);
			put_driver_status_nolock(cosa);
			clear_bit(TXBIT, &cosa->rxtx);
			spin_unlock_irqrestore(&cosa->lock, flags);
			return;
		}
		while(1) {
			cosa->txchan++;
			i++;
			if (cosa->txchan >= cosa->nchannels)
				cosa->txchan = 0;
			if (!(cosa->txbitmap & (1<<cosa->txchan)))
				continue;
			if (~status & (1 << (cosa->txchan+DRIVER_TXMAP_SHIFT)))
				break;
			/* in second pass, accept first ready-to-TX channel */
			if (i > cosa->nchannels) {
				/* Can be safely ignored */
#ifdef DEBUG_IRQS
				printk(KERN_DEBUG "%s: Forcing TX "
					"to not-ready channel %d\n",
					cosa->name, cosa->txchan);
#endif
				break;
			}
		}

		cosa->txsize = cosa->chan[cosa->txchan].txsize;
		if (cosa_dma_able(cosa->chan+cosa->txchan,
			cosa->chan[cosa->txchan].txbuf, cosa->txsize)) {
			cosa->txbuf = cosa->chan[cosa->txchan].txbuf;
		} else {
			memcpy(cosa->bouncebuf, cosa->chan[cosa->txchan].txbuf,
				cosa->txsize);
			cosa->txbuf = cosa->bouncebuf;
		}
	}

	if (is_8bit(cosa)) {
		if (!test_bit(IRQBIT, &cosa->rxtx)) {
			cosa_putstatus(cosa, SR_TX_INT_ENA);
			cosa_putdata8(cosa, ((cosa->txchan << 5) & 0xe0)|
				((cosa->txsize >> 8) & 0x1f));
#ifdef DEBUG_IO
			debug_status_out(cosa, SR_TX_INT_ENA);
			debug_data_out(cosa, ((cosa->txchan << 5) & 0xe0)|
                                ((cosa->txsize >> 8) & 0x1f));
			debug_data_in(cosa, cosa_getdata8(cosa));
#else
			cosa_getdata8(cosa);
#endif
			set_bit(IRQBIT, &cosa->rxtx);
			spin_unlock_irqrestore(&cosa->lock, flags);
			return;
		} else {
			clear_bit(IRQBIT, &cosa->rxtx);
			cosa_putstatus(cosa, 0);
			cosa_putdata8(cosa, cosa->txsize&0xff);
#ifdef DEBUG_IO
			debug_status_out(cosa, 0);
			debug_data_out(cosa, cosa->txsize&0xff);
#endif
		}
	} else {
		cosa_putstatus(cosa, SR_TX_INT_ENA);
		cosa_putdata16(cosa, ((cosa->txchan<<13) & 0xe000)
			| (cosa->txsize & 0x1fff));
#ifdef DEBUG_IO
		debug_status_out(cosa, SR_TX_INT_ENA);
		debug_data_out(cosa, ((cosa->txchan<<13) & 0xe000)
                        | (cosa->txsize & 0x1fff));
		debug_data_in(cosa, cosa_getdata8(cosa));
		debug_status_out(cosa, 0);
#else
		cosa_getdata8(cosa);
#endif
		cosa_putstatus(cosa, 0);
	}

	if (cosa->busmaster) {
		unsigned long addr = virt_to_bus(cosa->txbuf);
		int count=0;
		printk(KERN_INFO "busmaster IRQ\n");
		while (!(cosa_getstatus(cosa)&SR_TX_RDY)) {
			count++;
			udelay(10);
			if (count > 1000) break;
		}
		printk(KERN_INFO "status %x\n", cosa_getstatus(cosa));
		printk(KERN_INFO "ready after %d loops\n", count);
		cosa_putdata16(cosa, (addr >> 16)&0xffff);

		count = 0;
		while (!(cosa_getstatus(cosa)&SR_TX_RDY)) {
			count++;
			if (count > 1000) break;
			udelay(10);
		}
		printk(KERN_INFO "ready after %d loops\n", count);
		cosa_putdata16(cosa, addr &0xffff);
		flags1 = claim_dma_lock();
		set_dma_mode(cosa->dma, DMA_MODE_CASCADE);
		enable_dma(cosa->dma);
		release_dma_lock(flags1);
	} else {
		/* start the DMA */
		flags1 = claim_dma_lock();
		disable_dma(cosa->dma);
		clear_dma_ff(cosa->dma);
		set_dma_mode(cosa->dma, DMA_MODE_WRITE);
		set_dma_addr(cosa->dma, virt_to_bus(cosa->txbuf));
		set_dma_count(cosa->dma, cosa->txsize);
		enable_dma(cosa->dma);
		release_dma_lock(flags1);
	}
	cosa_putstatus(cosa, SR_TX_DMA_ENA|SR_USR_INT_ENA);
#ifdef DEBUG_IO
	debug_status_out(cosa, SR_TX_DMA_ENA|SR_USR_INT_ENA);
#endif
	spin_unlock_irqrestore(&cosa->lock, flags);
}

static inline void rx_interrupt(struct cosa_data *cosa, int status)
{
	unsigned long flags;
#ifdef DEBUG_IRQS
	printk(KERN_INFO "cosa%d: SR_UP_REQUEST\n", cosa->num);
#endif

	spin_lock_irqsave(&cosa->lock, flags);
	set_bit(RXBIT, &cosa->rxtx);

	if (is_8bit(cosa)) {
		if (!test_bit(IRQBIT, &cosa->rxtx)) {
			set_bit(IRQBIT, &cosa->rxtx);
			put_driver_status_nolock(cosa);
			cosa->rxsize = cosa_getdata8(cosa) <<8;
#ifdef DEBUG_IO
			debug_data_in(cosa, cosa->rxsize >> 8);
#endif
			spin_unlock_irqrestore(&cosa->lock, flags);
			return;
		} else {
			clear_bit(IRQBIT, &cosa->rxtx);
			cosa->rxsize |= cosa_getdata8(cosa) & 0xff;
#ifdef DEBUG_IO
			debug_data_in(cosa, cosa->rxsize & 0xff);
#endif
#if 0
			printk(KERN_INFO "cosa%d: receive rxsize = (0x%04x).\n",
				cosa->num, cosa->rxsize);
#endif
		}
	} else {
		cosa->rxsize = cosa_getdata16(cosa);
#ifdef DEBUG_IO
		debug_data_in(cosa, cosa->rxsize);
#endif
#if 0
		printk(KERN_INFO "cosa%d: receive rxsize = (0x%04x).\n",
			cosa->num, cosa->rxsize);
#endif
	}
	if (((cosa->rxsize & 0xe000) >> 13) >= cosa->nchannels) {
		printk(KERN_WARNING "%s: rx for unknown channel (0x%04x)\n",
			cosa->name, cosa->rxsize);
		spin_unlock_irqrestore(&cosa->lock, flags);
		goto reject;
	}
	cosa->rxchan = cosa->chan + ((cosa->rxsize & 0xe000) >> 13);
	cosa->rxsize &= 0x1fff;
	spin_unlock_irqrestore(&cosa->lock, flags);

	cosa->rxbuf = NULL;
	if (cosa->rxchan->setup_rx)
		cosa->rxbuf = cosa->rxchan->setup_rx(cosa->rxchan, cosa->rxsize);

	if (!cosa->rxbuf) {
reject:		/* Reject the packet */
		printk(KERN_INFO "cosa%d: rejecting packet on channel %d\n",
			cosa->num, cosa->rxchan->num);
		cosa->rxbuf = cosa->bouncebuf;
	}

	/* start the DMA */
	flags = claim_dma_lock();
	disable_dma(cosa->dma);
	clear_dma_ff(cosa->dma);
	set_dma_mode(cosa->dma, DMA_MODE_READ);
	if (cosa_dma_able(cosa->rxchan, cosa->rxbuf, cosa->rxsize & 0x1fff)) {
		set_dma_addr(cosa->dma, virt_to_bus(cosa->rxbuf));
	} else {
		set_dma_addr(cosa->dma, virt_to_bus(cosa->bouncebuf));
	}
	set_dma_count(cosa->dma, (cosa->rxsize&0x1fff));
	enable_dma(cosa->dma);
	release_dma_lock(flags);
	spin_lock_irqsave(&cosa->lock, flags);
	cosa_putstatus(cosa, SR_RX_DMA_ENA|SR_USR_INT_ENA);
	if (!is_8bit(cosa) && (status & SR_TX_RDY))
		cosa_putdata8(cosa, DRIVER_RX_READY);
#ifdef DEBUG_IO
	debug_status_out(cosa, SR_RX_DMA_ENA|SR_USR_INT_ENA);
	if (!is_8bit(cosa) && (status & SR_TX_RDY))
		debug_data_cmd(cosa, DRIVER_RX_READY);
#endif
	spin_unlock_irqrestore(&cosa->lock, flags);
}

static inline void eot_interrupt(struct cosa_data *cosa, int status)
{
	unsigned long flags, flags1;
	spin_lock_irqsave(&cosa->lock, flags);
	flags1 = claim_dma_lock();
	disable_dma(cosa->dma);
	clear_dma_ff(cosa->dma);
	release_dma_lock(flags1);
	if (test_bit(TXBIT, &cosa->rxtx)) {
		struct channel_data *chan = cosa->chan+cosa->txchan;
		if (chan->tx_done)
			if (chan->tx_done(chan, cosa->txsize))
				clear_bit(chan->num, &cosa->txbitmap);
	} else if (test_bit(RXBIT, &cosa->rxtx)) {
#ifdef DEBUG_DATA
	{
		int i;
		printk(KERN_INFO "cosa%dc%d: done rx(0x%x)", cosa->num, 
			cosa->rxchan->num, cosa->rxsize);
		for (i=0; i<cosa->rxsize; i++)
			printk (" %02x", cosa->rxbuf[i]&0xff);
		printk("\n");
	}
#endif
		/* Packet for unknown channel? */
		if (cosa->rxbuf == cosa->bouncebuf)
			goto out;
		if (!cosa_dma_able(cosa->rxchan, cosa->rxbuf, cosa->rxsize))
			memcpy(cosa->rxbuf, cosa->bouncebuf, cosa->rxsize);
		if (cosa->rxchan->rx_done)
			if (cosa->rxchan->rx_done(cosa->rxchan))
				clear_bit(cosa->rxchan->num, &cosa->rxbitmap);
	} else {
		printk(KERN_NOTICE "cosa%d: unexpected EOT interrupt\n",
			cosa->num);
	}
	/*
	 * Clear the RXBIT, TXBIT and IRQBIT (the latest should be
	 * cleared anyway). We should do it as soon as possible
	 * so that we can tell the COSA we are done and to give it a time
	 * for recovery.
	 */
out:
	cosa->rxtx = 0;
	put_driver_status_nolock(cosa);
	spin_unlock_irqrestore(&cosa->lock, flags);
}

static irqreturn_t cosa_interrupt(int irq, void *cosa_, struct pt_regs *regs)
{
	unsigned status;
	int count = 0;
	struct cosa_data *cosa = cosa_;
again:
	status = cosa_getstatus(cosa);
#ifdef DEBUG_IRQS
	printk(KERN_INFO "cosa%d: got IRQ, status 0x%02x\n", cosa->num,
		status & 0xff);
#endif
#ifdef DEBUG_IO
	debug_status_in(cosa, status);
#endif
	switch (status & SR_CMD_FROM_SRP_MASK) {
	case SR_DOWN_REQUEST:
		tx_interrupt(cosa, status);
		break;
	case SR_UP_REQUEST:
		rx_interrupt(cosa, status);
		break;
	case SR_END_OF_TRANSFER:
		eot_interrupt(cosa, status);
		break;
	default:
		/* We may be too fast for SRP. Try to wait a bit more. */
		if (count++ < 100) {
			udelay(100);
			goto again;
		}
		printk(KERN_INFO "cosa%d: unknown status 0x%02x in IRQ after %d retries\n",
			cosa->num, status & 0xff, count);
	}
#ifdef DEBUG_IRQS
	if (count)
		printk(KERN_INFO "%s: %d-times got unknown status in IRQ\n",
			cosa->name, count);
	else
		printk(KERN_INFO "%s: returning from IRQ\n", cosa->name);
#endif
	return IRQ_HANDLED;
}


/* ---------- I/O debugging routines ---------- */
/*
 * These routines can be used to monitor COSA/SRP I/O and to printk()
 * the data being transferred on the data and status I/O port in a
 * readable way.
 */

#ifdef DEBUG_IO
static void debug_status_in(struct cosa_data *cosa, int status)
{
	char *s;
	switch(status & SR_CMD_FROM_SRP_MASK) {
	case SR_UP_REQUEST:
		s = "RX_REQ";
		break;
	case SR_DOWN_REQUEST:
		s = "TX_REQ";
		break;
	case SR_END_OF_TRANSFER:
		s = "ET_REQ";
		break;
	default:
		s = "NO_REQ";
		break;
	}
	printk(KERN_INFO "%s: IO: status -> 0x%02x (%s%s%s%s)\n",
		cosa->name,
		status,
		status & SR_USR_RQ ? "USR_RQ|":"",
		status & SR_TX_RDY ? "TX_RDY|":"",
		status & SR_RX_RDY ? "RX_RDY|":"",
		s);
}

static void debug_status_out(struct cosa_data *cosa, int status)
{
	printk(KERN_INFO "%s: IO: status <- 0x%02x (%s%s%s%s%s%s)\n",
		cosa->name,
		status,
		status & SR_RX_DMA_ENA  ? "RXDMA|":"!rxdma|",
		status & SR_TX_DMA_ENA  ? "TXDMA|":"!txdma|",
		status & SR_RST         ? "RESET|":"",
		status & SR_USR_INT_ENA ? "USRINT|":"!usrint|",
		status & SR_TX_INT_ENA  ? "TXINT|":"!txint|",
		status & SR_RX_INT_ENA  ? "RXINT":"!rxint");
}

static void debug_data_in(struct cosa_data *cosa, int data)
{
	printk(KERN_INFO "%s: IO: data -> 0x%04x\n", cosa->name, data);
}

static void debug_data_out(struct cosa_data *cosa, int data)
{
	printk(KERN_INFO "%s: IO: data <- 0x%04x\n", cosa->name, data);
}

static void debug_data_cmd(struct cosa_data *cosa, int data)
{
	printk(KERN_INFO "%s: IO: data <- 0x%04x (%s|%s)\n",
		cosa->name, data,
		data & SR_RDY_RCV ? "RX_RDY" : "!rx_rdy",
		data & SR_RDY_SND ? "TX_RDY" : "!tx_rdy");
}
#endif

/* EOF -- this file has not been truncated */
