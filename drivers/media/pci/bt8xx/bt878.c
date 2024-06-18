// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * bt878.c: part of the driver for the Pinnacle PCTV Sat DVB PCI card
 *
 * Copyright (C) 2002 Peter Hettkamp <peter.hettkamp@htp-tel.de>
 *
 * large parts based on the bttv driver
 * Copyright (C) 1996,97,98 Ralph  Metzler (rjkm@metzlerbros.de)
 *                        & Marcus Metzler (mocm@metzlerbros.de)
 * (c) 1999,2000 Gerd Knorr <kraxel@goldbach.in-berlin.de>
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/pgtable.h>
#include <asm/io.h>
#include <linux/ioport.h>
#include <asm/page.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/kmod.h>
#include <linux/vmalloc.h>
#include <linux/init.h>

#include <media/dmxdev.h>
#include <media/dvbdev.h>
#include "bt878.h"
#include "dst_priv.h"


/**************************************/
/* Miscellaneous utility  definitions */
/**************************************/

static unsigned int bt878_verbose = 1;
static unsigned int bt878_debug;

module_param_named(verbose, bt878_verbose, int, 0444);
MODULE_PARM_DESC(verbose,
		 "verbose startup messages, default is 1 (yes)");
module_param_named(debug, bt878_debug, int, 0644);
MODULE_PARM_DESC(debug, "Turn on/off debugging, default is 0 (off).");

int bt878_num;
struct bt878 bt878[BT878_MAX];

EXPORT_SYMBOL(bt878_num);
EXPORT_SYMBOL(bt878);

#define btwrite(dat,adr)    bmtwrite((dat), (bt->bt878_mem+(adr)))
#define btread(adr)         bmtread(bt->bt878_mem+(adr))

#define btand(dat,adr)      btwrite((dat) & btread(adr), adr)
#define btor(dat,adr)       btwrite((dat) | btread(adr), adr)
#define btaor(dat,mask,adr) btwrite((dat) | ((mask) & btread(adr)), adr)

#if defined(dprintk)
#undef dprintk
#endif
#define dprintk(fmt, arg...) \
	do { \
		if (bt878_debug) \
			printk(KERN_DEBUG fmt, ##arg); \
	} while (0)

static void bt878_mem_free(struct bt878 *bt)
{
	if (bt->buf_cpu) {
		dma_free_coherent(&bt->dev->dev, bt->buf_size, bt->buf_cpu,
				  bt->buf_dma);
		bt->buf_cpu = NULL;
	}

	if (bt->risc_cpu) {
		dma_free_coherent(&bt->dev->dev, bt->risc_size, bt->risc_cpu,
				  bt->risc_dma);
		bt->risc_cpu = NULL;
	}
}

static int bt878_mem_alloc(struct bt878 *bt)
{
	if (!bt->buf_cpu) {
		bt->buf_size = 128 * 1024;

		bt->buf_cpu = dma_alloc_coherent(&bt->dev->dev, bt->buf_size,
						 &bt->buf_dma, GFP_KERNEL);
		if (!bt->buf_cpu)
			return -ENOMEM;
	}

	if (!bt->risc_cpu) {
		bt->risc_size = PAGE_SIZE;
		bt->risc_cpu = dma_alloc_coherent(&bt->dev->dev, bt->risc_size,
						  &bt->risc_dma, GFP_KERNEL);
		if (!bt->risc_cpu) {
			bt878_mem_free(bt);
			return -ENOMEM;
		}
	}

	return 0;
}

/* RISC instructions */
#define RISC_WRITE		(0x01 << 28)
#define RISC_JUMP		(0x07 << 28)
#define RISC_SYNC		(0x08 << 28)

/* RISC bits */
#define RISC_WR_SOL		(1 << 27)
#define RISC_WR_EOL		(1 << 26)
#define RISC_IRQ		(1 << 24)
#define RISC_STATUS(status)	((((~status) & 0x0F) << 20) | ((status & 0x0F) << 16))
#define RISC_SYNC_RESYNC	(1 << 15)
#define RISC_SYNC_FM1		0x06
#define RISC_SYNC_VRO		0x0C

#define RISC_FLUSH()		bt->risc_pos = 0
#define RISC_INSTR(instr)	bt->risc_cpu[bt->risc_pos++] = cpu_to_le32(instr)

static int bt878_make_risc(struct bt878 *bt)
{
	bt->block_bytes = bt->buf_size >> 4;
	bt->block_count = 1 << 4;
	bt->line_bytes = bt->block_bytes;
	bt->line_count = bt->block_count;

	while (bt->line_bytes > 4095) {
		bt->line_bytes >>= 1;
		bt->line_count <<= 1;
	}

	if (bt->line_count > 255) {
		printk(KERN_ERR "bt878: buffer size error!\n");
		return -EINVAL;
	}
	return 0;
}


static void bt878_risc_program(struct bt878 *bt, u32 op_sync_orin)
{
	u32 buf_pos = 0;
	u32 line;

	RISC_FLUSH();
	RISC_INSTR(RISC_SYNC | RISC_SYNC_FM1 | op_sync_orin);
	RISC_INSTR(0);

	dprintk("bt878: risc len lines %u, bytes per line %u\n",
			bt->line_count, bt->line_bytes);
	for (line = 0; line < bt->line_count; line++) {
		// At the beginning of every block we issue an IRQ with previous (finished) block number set
		if (!(buf_pos % bt->block_bytes))
			RISC_INSTR(RISC_WRITE | RISC_WR_SOL | RISC_WR_EOL |
				   RISC_IRQ |
				   RISC_STATUS(((buf_pos /
						 bt->block_bytes) +
						(bt->block_count -
						 1)) %
					       bt->block_count) | bt->
				   line_bytes);
		else
			RISC_INSTR(RISC_WRITE | RISC_WR_SOL | RISC_WR_EOL |
				   bt->line_bytes);
		RISC_INSTR(bt->buf_dma + buf_pos);
		buf_pos += bt->line_bytes;
	}

	RISC_INSTR(RISC_SYNC | op_sync_orin | RISC_SYNC_VRO);
	RISC_INSTR(0);

	RISC_INSTR(RISC_JUMP);
	RISC_INSTR(bt->risc_dma);

	btwrite((bt->line_count << 16) | bt->line_bytes, BT878_APACK_LEN);
}

/*****************************/
/* Start/Stop grabbing funcs */
/*****************************/

void bt878_start(struct bt878 *bt, u32 controlreg, u32 op_sync_orin,
		u32 irq_err_ignore)
{
	u32 int_mask;

	dprintk("bt878 debug: bt878_start (ctl=%8.8x)\n", controlreg);
	/* complete the writing of the risc dma program now we have
	 * the card specifics
	 */
	bt878_risc_program(bt, op_sync_orin);
	controlreg &= ~0x1f;
	controlreg |= 0x1b;

	btwrite(bt->risc_dma, BT878_ARISC_START);

	/* original int mask had :
	 *    6    2    8    4    0
	 * 1111 1111 1000 0000 0000
	 * SCERR|OCERR|PABORT|RIPERR|FDSR|FTRGT|FBUS|RISCI
	 * Hacked for DST to:
	 * SCERR | OCERR | FDSR | FTRGT | FBUS | RISCI
	 */
	int_mask = BT878_ASCERR | BT878_AOCERR | BT878_APABORT |
		BT878_ARIPERR | BT878_APPERR | BT878_AFDSR | BT878_AFTRGT |
		BT878_AFBUS | BT878_ARISCI;


	/* ignore pesky bits */
	int_mask &= ~irq_err_ignore;

	btwrite(int_mask, BT878_AINT_MASK);
	btwrite(controlreg, BT878_AGPIO_DMA_CTL);
}

void bt878_stop(struct bt878 *bt)
{
	u32 stat;
	int i = 0;

	dprintk("bt878 debug: bt878_stop\n");

	btwrite(0, BT878_AINT_MASK);
	btand(~0x13, BT878_AGPIO_DMA_CTL);

	do {
		stat = btread(BT878_AINT_STAT);
		if (!(stat & BT878_ARISC_EN))
			break;
		i++;
	} while (i < 500);

	dprintk("bt878(%d) debug: bt878_stop, i=%d, stat=0x%8.8x\n",
		bt->nr, i, stat);
}

EXPORT_SYMBOL(bt878_start);
EXPORT_SYMBOL(bt878_stop);

/*****************************/
/* Interrupt service routine */
/*****************************/

static irqreturn_t bt878_irq(int irq, void *dev_id)
{
	u32 stat, astat, mask;
	int count;
	struct bt878 *bt;

	bt = (struct bt878 *) dev_id;

	count = 0;
	while (1) {
		stat = btread(BT878_AINT_STAT);
		mask = btread(BT878_AINT_MASK);
		if (!(astat = (stat & mask)))
			return IRQ_NONE;	/* this interrupt is not for me */
/*		dprintk("bt878(%d) debug: irq count %d, stat 0x%8.8x, mask 0x%8.8x\n",bt->nr,count,stat,mask); */
		btwrite(astat, BT878_AINT_STAT);	/* try to clear interrupt condition */


		if (astat & (BT878_ASCERR | BT878_AOCERR)) {
			if (bt878_verbose) {
				printk(KERN_INFO
				       "bt878(%d): irq%s%s risc_pc=%08x\n",
				       bt->nr,
				       (astat & BT878_ASCERR) ? " SCERR" :
				       "",
				       (astat & BT878_AOCERR) ? " OCERR" :
				       "", btread(BT878_ARISC_PC));
			}
		}
		if (astat & (BT878_APABORT | BT878_ARIPERR | BT878_APPERR)) {
			if (bt878_verbose) {
				printk(KERN_INFO
				     "bt878(%d): irq%s%s%s risc_pc=%08x\n",
				     bt->nr,
				     (astat & BT878_APABORT) ? " PABORT" :
				     "",
				     (astat & BT878_ARIPERR) ? " RIPERR" :
				     "",
				     (astat & BT878_APPERR) ? " PPERR" :
				     "", btread(BT878_ARISC_PC));
			}
		}
		if (astat & (BT878_AFDSR | BT878_AFTRGT | BT878_AFBUS)) {
			if (bt878_verbose) {
				printk(KERN_INFO
				     "bt878(%d): irq%s%s%s risc_pc=%08x\n",
				     bt->nr,
				     (astat & BT878_AFDSR) ? " FDSR" : "",
				     (astat & BT878_AFTRGT) ? " FTRGT" :
				     "",
				     (astat & BT878_AFBUS) ? " FBUS" : "",
				     btread(BT878_ARISC_PC));
			}
		}
		if (astat & BT878_ARISCI) {
			bt->finished_block = (stat & BT878_ARISCS) >> 28;
			if (bt->bh_work.func)
				queue_work(system_bh_wq, &bt->bh_work);
			break;
		}
		count++;
		if (count > 20) {
			btwrite(0, BT878_AINT_MASK);
			printk(KERN_ERR
			       "bt878(%d): IRQ lockup, cleared int mask\n",
			       bt->nr);
			break;
		}
	}
	return IRQ_HANDLED;
}

int
bt878_device_control(struct bt878 *bt, unsigned int cmd, union dst_gpio_packet *mp)
{
	int retval;

	retval = 0;
	if (mutex_lock_interruptible(&bt->gpio_lock))
		return -ERESTARTSYS;
	/* special gpio signal */
	switch (cmd) {
	    case DST_IG_ENABLE:
		// dprintk("dvb_bt8xx: dst enable mask 0x%02x enb 0x%02x \n", mp->dstg.enb.mask, mp->dstg.enb.enable);
		retval = bttv_gpio_enable(bt->bttv_nr,
				mp->enb.mask,
				mp->enb.enable);
		break;
	    case DST_IG_WRITE:
		// dprintk("dvb_bt8xx: dst write gpio mask 0x%02x out 0x%02x\n", mp->dstg.outp.mask, mp->dstg.outp.highvals);
		retval = bttv_write_gpio(bt->bttv_nr,
				mp->outp.mask,
				mp->outp.highvals);

		break;
	    case DST_IG_READ:
		/* read */
		retval =  bttv_read_gpio(bt->bttv_nr, &mp->rd.value);
		// dprintk("dvb_bt8xx: dst read gpio 0x%02x\n", (unsigned)mp->dstg.rd.value);
		break;
	    case DST_IG_TS:
		/* Set packet size */
		bt->TS_Size = mp->psize;
		break;

	    default:
		retval = -EINVAL;
		break;
	}
	mutex_unlock(&bt->gpio_lock);
	return retval;
}

EXPORT_SYMBOL(bt878_device_control);

#define BROOKTREE_878_DEVICE(vend, dev, name) \
	{ \
		.vendor = PCI_VENDOR_ID_BROOKTREE, \
		.device = PCI_DEVICE_ID_BROOKTREE_878, \
		.subvendor = (vend), .subdevice = (dev), \
		.driver_data = (unsigned long) name \
	}

static const struct pci_device_id bt878_pci_tbl[] = {
	BROOKTREE_878_DEVICE(0x0071, 0x0101, "Nebula Electronics DigiTV"),
	BROOKTREE_878_DEVICE(0x1461, 0x0761, "AverMedia AverTV DVB-T 761"),
	BROOKTREE_878_DEVICE(0x11bd, 0x001c, "Pinnacle PCTV Sat"),
	BROOKTREE_878_DEVICE(0x11bd, 0x0026, "Pinnacle PCTV SAT CI"),
	BROOKTREE_878_DEVICE(0x1822, 0x0001, "Twinhan VisionPlus DVB"),
	BROOKTREE_878_DEVICE(0x270f, 0xfc00,
				"ChainTech digitop DST-1000 DVB-S"),
	BROOKTREE_878_DEVICE(0x1461, 0x0771, "AVermedia AverTV DVB-T 771"),
	BROOKTREE_878_DEVICE(0x18ac, 0xdb10, "DViCO FusionHDTV DVB-T Lite"),
	BROOKTREE_878_DEVICE(0x18ac, 0xdb11, "Ultraview DVB-T Lite"),
	BROOKTREE_878_DEVICE(0x18ac, 0xd500, "DViCO FusionHDTV 5 Lite"),
	BROOKTREE_878_DEVICE(0x7063, 0x2000, "pcHDTV HD-2000 TV"),
	BROOKTREE_878_DEVICE(0x1822, 0x0026, "DNTV Live! Mini"),
	{ }
};

MODULE_DEVICE_TABLE(pci, bt878_pci_tbl);

static const char * card_name(const struct pci_device_id *id)
{
	return id->driver_data ? (const char *)id->driver_data : "Unknown";
}

/***********************/
/* PCI device handling */
/***********************/

static int bt878_probe(struct pci_dev *dev, const struct pci_device_id *pci_id)
{
	int result = 0;
	unsigned char lat;
	struct bt878 *bt;
	unsigned int cardid;

	printk(KERN_INFO "bt878: Bt878 AUDIO function found (%d).\n",
	       bt878_num);
	if (bt878_num >= BT878_MAX) {
		printk(KERN_ERR "bt878: Too many devices inserted\n");
		return -ENOMEM;
	}
	if (pci_enable_device(dev))
		return -EIO;

	cardid = dev->subsystem_device << 16;
	cardid |= dev->subsystem_vendor;

	printk(KERN_INFO "%s: card id=[0x%x],[ %s ] has DVB functions.\n",
				__func__, cardid, card_name(pci_id));

	bt = &bt878[bt878_num];
	bt->dev = dev;
	bt->nr = bt878_num;
	bt->shutdown = 0;

	bt->id = dev->device;
	bt->irq = dev->irq;
	bt->bt878_adr = pci_resource_start(dev, 0);
	if (!request_mem_region(pci_resource_start(dev, 0),
				pci_resource_len(dev, 0), "bt878")) {
		result = -EBUSY;
		goto fail0;
	}

	bt->revision = dev->revision;
	pci_read_config_byte(dev, PCI_LATENCY_TIMER, &lat);


	printk(KERN_INFO "bt878(%d): Bt%x (rev %d) at %02x:%02x.%x, ",
	       bt878_num, bt->id, bt->revision, dev->bus->number,
	       PCI_SLOT(dev->devfn), PCI_FUNC(dev->devfn));
	printk("irq: %d, latency: %d, memory: 0x%lx\n",
	       bt->irq, lat, bt->bt878_adr);

#ifdef __sparc__
	bt->bt878_mem = (unsigned char *) bt->bt878_adr;
#else
	bt->bt878_mem = ioremap(bt->bt878_adr, 0x1000);
#endif

	/* clear interrupt mask */
	btwrite(0, BT848_INT_MASK);

	result = request_irq(bt->irq, bt878_irq,
			     IRQF_SHARED, "bt878", (void *) bt);
	if (result == -EINVAL) {
		printk(KERN_ERR "bt878(%d): Bad irq number or handler\n",
		       bt878_num);
		goto fail1;
	}
	if (result == -EBUSY) {
		printk(KERN_ERR
		       "bt878(%d): IRQ %d busy, change your PnP config in BIOS\n",
		       bt878_num, bt->irq);
		goto fail1;
	}
	if (result < 0)
		goto fail1;

	pci_set_master(dev);
	pci_set_drvdata(dev, bt);

	if ((result = bt878_mem_alloc(bt))) {
		printk(KERN_ERR "bt878: failed to allocate memory!\n");
		goto fail2;
	}

	bt878_make_risc(bt);
	btwrite(0, BT878_AINT_MASK);
	bt878_num++;

	if (!bt->bh_work.func)
		disable_work_sync(&bt->bh_work);

	return 0;

      fail2:
	free_irq(bt->irq, bt);
      fail1:
	release_mem_region(pci_resource_start(bt->dev, 0),
			   pci_resource_len(bt->dev, 0));
      fail0:
	pci_disable_device(dev);
	return result;
}

static void bt878_remove(struct pci_dev *pci_dev)
{
	u8 command;
	struct bt878 *bt = pci_get_drvdata(pci_dev);

	if (bt878_verbose)
		printk(KERN_INFO "bt878(%d): unloading\n", bt->nr);

	/* turn off all capturing, DMA and IRQs */
	btand(~0x13, BT878_AGPIO_DMA_CTL);

	/* first disable interrupts before unmapping the memory! */
	btwrite(0, BT878_AINT_MASK);
	btwrite(~0U, BT878_AINT_STAT);

	/* disable PCI bus-mastering */
	pci_read_config_byte(bt->dev, PCI_COMMAND, &command);
	/* Should this be &=~ ?? */
	command &= ~PCI_COMMAND_MASTER;
	pci_write_config_byte(bt->dev, PCI_COMMAND, command);

	free_irq(bt->irq, bt);
	printk(KERN_DEBUG "bt878_mem: 0x%p.\n", bt->bt878_mem);
	if (bt->bt878_mem)
		iounmap(bt->bt878_mem);

	release_mem_region(pci_resource_start(bt->dev, 0),
			   pci_resource_len(bt->dev, 0));
	/* wake up any waiting processes
	   because shutdown flag is set, no new processes (in this queue)
	   are expected
	 */
	bt->shutdown = 1;
	bt878_mem_free(bt);

	pci_disable_device(pci_dev);
	return;
}

static struct pci_driver bt878_pci_driver = {
      .name	= "bt878",
      .id_table = bt878_pci_tbl,
      .probe	= bt878_probe,
      .remove	= bt878_remove,
};

/*******************************/
/* Module management functions */
/*******************************/

static int __init bt878_init_module(void)
{
	bt878_num = 0;

	printk(KERN_INFO "bt878: AUDIO driver version %d.%d.%d loaded\n",
	       (BT878_VERSION_CODE >> 16) & 0xff,
	       (BT878_VERSION_CODE >> 8) & 0xff,
	       BT878_VERSION_CODE & 0xff);

	return pci_register_driver(&bt878_pci_driver);
}

static void __exit bt878_cleanup_module(void)
{
	pci_unregister_driver(&bt878_pci_driver);
}

module_init(bt878_init_module);
module_exit(bt878_cleanup_module);

MODULE_DESCRIPTION("DVB/ATSC Support for bt878 based TV cards");
MODULE_LICENSE("GPL");
