// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/drivers/acorn/scsi/cumana_2.c
 *
 *  Copyright (C) 1997-2005 Russell King
 *
 *  Changelog:
 *   30-08-1997	RMK	0.0.0	Created, READONLY version.
 *   22-01-1998	RMK	0.0.1	Updated to 2.1.80.
 *   15-04-1998	RMK	0.0.1	Only do PIO if FAS216 will allow it.
 *   02-05-1998	RMK	0.0.2	Updated & added DMA support.
 *   27-06-1998	RMK		Changed asm/delay.h to linux/delay.h
 *   18-08-1998	RMK	0.0.3	Fixed synchronous transfer depth.
 *   02-04-2000	RMK	0.0.4	Updated for new error handling code.
 */
#include <linux/module.h>
#include <linux/blkdev.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/ioport.h>
#include <linux/proc_fs.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/dma-mapping.h>
#include <linux/pgtable.h>

#include <asm/dma.h>
#include <asm/ecard.h>
#include <asm/io.h>

#include "../scsi.h"
#include <scsi/scsi_host.h>
#include "fas216.h"
#include "scsi.h"

#include <scsi/scsicam.h>

#define CUMANASCSI2_STATUS		(0x0000)
#define STATUS_INT			(1 << 0)
#define STATUS_DRQ			(1 << 1)
#define STATUS_LATCHED			(1 << 3)

#define CUMANASCSI2_ALATCH		(0x0014)
#define ALATCH_ENA_INT			(3)
#define ALATCH_DIS_INT			(2)
#define ALATCH_ENA_TERM			(5)
#define ALATCH_DIS_TERM			(4)
#define ALATCH_ENA_BIT32		(11)
#define ALATCH_DIS_BIT32		(10)
#define ALATCH_ENA_DMA			(13)
#define ALATCH_DIS_DMA			(12)
#define ALATCH_DMA_OUT			(15)
#define ALATCH_DMA_IN			(14)

#define CUMANASCSI2_PSEUDODMA		(0x0200)

#define CUMANASCSI2_FAS216_OFFSET	(0x0300)
#define CUMANASCSI2_FAS216_SHIFT	2

/*
 * Version
 */
#define VERSION "1.00 (13/11/2002 2.5.47)"

/*
 * Use term=0,1,0,0,0 to turn terminators on/off
 */
static int term[MAX_ECARDS] = { 1, 1, 1, 1, 1, 1, 1, 1 };

#define NR_SG	256

struct cumanascsi2_info {
	FAS216_Info		info;
	struct expansion_card	*ec;
	void __iomem		*base;
	unsigned int		terms;		/* Terminator state	*/
	struct scatterlist	sg[NR_SG];	/* Scatter DMA list	*/
};

#define CSTATUS_IRQ	(1 << 0)
#define CSTATUS_DRQ	(1 << 1)

/* Prototype: void cumanascsi_2_irqenable(ec, irqnr)
 * Purpose  : Enable interrupts on Cumana SCSI 2 card
 * Params   : ec    - expansion card structure
 *          : irqnr - interrupt number
 */
static void
cumanascsi_2_irqenable(struct expansion_card *ec, int irqnr)
{
	struct cumanascsi2_info *info = ec->irq_data;
	writeb(ALATCH_ENA_INT, info->base + CUMANASCSI2_ALATCH);
}

/* Prototype: void cumanascsi_2_irqdisable(ec, irqnr)
 * Purpose  : Disable interrupts on Cumana SCSI 2 card
 * Params   : ec    - expansion card structure
 *          : irqnr - interrupt number
 */
static void
cumanascsi_2_irqdisable(struct expansion_card *ec, int irqnr)
{
	struct cumanascsi2_info *info = ec->irq_data;
	writeb(ALATCH_DIS_INT, info->base + CUMANASCSI2_ALATCH);
}

static const expansioncard_ops_t cumanascsi_2_ops = {
	.irqenable	= cumanascsi_2_irqenable,
	.irqdisable	= cumanascsi_2_irqdisable,
};

/* Prototype: void cumanascsi_2_terminator_ctl(host, on_off)
 * Purpose  : Turn the Cumana SCSI 2 terminators on or off
 * Params   : host   - card to turn on/off
 *          : on_off - !0 to turn on, 0 to turn off
 */
static void
cumanascsi_2_terminator_ctl(struct Scsi_Host *host, int on_off)
{
	struct cumanascsi2_info *info = (struct cumanascsi2_info *)host->hostdata;

	if (on_off) {
		info->terms = 1;
		writeb(ALATCH_ENA_TERM, info->base + CUMANASCSI2_ALATCH);
	} else {
		info->terms = 0;
		writeb(ALATCH_DIS_TERM, info->base + CUMANASCSI2_ALATCH);
	}
}

/* Prototype: void cumanascsi_2_intr(irq, *dev_id, *regs)
 * Purpose  : handle interrupts from Cumana SCSI 2 card
 * Params   : irq    - interrupt number
 *	      dev_id - user-defined (Scsi_Host structure)
 */
static irqreturn_t
cumanascsi_2_intr(int irq, void *dev_id)
{
	struct cumanascsi2_info *info = dev_id;

	return fas216_intr(&info->info);
}

/* Prototype: fasdmatype_t cumanascsi_2_dma_setup(host, SCpnt, direction, min_type)
 * Purpose  : initialises DMA/PIO
 * Params   : host      - host
 *	      SCpnt     - command
 *	      direction - DMA on to/off of card
 *	      min_type  - minimum DMA support that we must have for this transfer
 * Returns  : type of transfer to be performed
 */
static fasdmatype_t
cumanascsi_2_dma_setup(struct Scsi_Host *host, struct scsi_pointer *SCp,
		       fasdmadir_t direction, fasdmatype_t min_type)
{
	struct cumanascsi2_info *info = (struct cumanascsi2_info *)host->hostdata;
	struct device *dev = scsi_get_device(host);
	int dmach = info->info.scsi.dma;

	writeb(ALATCH_DIS_DMA, info->base + CUMANASCSI2_ALATCH);

	if (dmach != NO_DMA &&
	    (min_type == fasdma_real_all || SCp->this_residual >= 512)) {
		int bufs, map_dir, dma_dir, alatch_dir;

		bufs = copy_SCp_to_sg(&info->sg[0], SCp, NR_SG);

		if (direction == DMA_OUT)
			map_dir = DMA_TO_DEVICE,
			dma_dir = DMA_MODE_WRITE,
			alatch_dir = ALATCH_DMA_OUT;
		else
			map_dir = DMA_FROM_DEVICE,
			dma_dir = DMA_MODE_READ,
			alatch_dir = ALATCH_DMA_IN;

		dma_map_sg(dev, info->sg, bufs, map_dir);

		disable_dma(dmach);
		set_dma_sg(dmach, info->sg, bufs);
		writeb(alatch_dir, info->base + CUMANASCSI2_ALATCH);
		set_dma_mode(dmach, dma_dir);
		enable_dma(dmach);
		writeb(ALATCH_ENA_DMA, info->base + CUMANASCSI2_ALATCH);
		writeb(ALATCH_DIS_BIT32, info->base + CUMANASCSI2_ALATCH);
		return fasdma_real_all;
	}

	/*
	 * If we're not doing DMA,
	 *  we'll do pseudo DMA
	 */
	return fasdma_pio;
}

/*
 * Prototype: void cumanascsi_2_dma_pseudo(host, SCpnt, direction, transfer)
 * Purpose  : handles pseudo DMA
 * Params   : host      - host
 *	      SCpnt     - command
 *	      direction - DMA on to/off of card
 *	      transfer  - minimum number of bytes we expect to transfer
 */
static void
cumanascsi_2_dma_pseudo(struct Scsi_Host *host, struct scsi_pointer *SCp,
			fasdmadir_t direction, int transfer)
{
	struct cumanascsi2_info *info = (struct cumanascsi2_info *)host->hostdata;
	unsigned int length;
	unsigned char *addr;

	length = SCp->this_residual;
	addr = SCp->ptr;

	if (direction == DMA_OUT)
#if 0
		while (length > 1) {
			unsigned long word;
			unsigned int status = readb(info->base + CUMANASCSI2_STATUS);

			if (status & STATUS_INT)
				goto end;

			if (!(status & STATUS_DRQ))
				continue;

			word = *addr | *(addr + 1) << 8;
			writew(word, info->base + CUMANASCSI2_PSEUDODMA);
			addr += 2;
			length -= 2;
		}
#else
		printk ("PSEUDO_OUT???\n");
#endif
	else {
		if (transfer && (transfer & 255)) {
			while (length >= 256) {
				unsigned int status = readb(info->base + CUMANASCSI2_STATUS);

				if (status & STATUS_INT)
					return;
	    
				if (!(status & STATUS_DRQ))
					continue;

				readsw(info->base + CUMANASCSI2_PSEUDODMA,
				       addr, 256 >> 1);
				addr += 256;
				length -= 256;
			}
		}

		while (length > 0) {
			unsigned long word;
			unsigned int status = readb(info->base + CUMANASCSI2_STATUS);

			if (status & STATUS_INT)
				return;

			if (!(status & STATUS_DRQ))
				continue;

			word = readw(info->base + CUMANASCSI2_PSEUDODMA);
			*addr++ = word;
			if (--length > 0) {
				*addr++ = word >> 8;
				length --;
			}
		}
	}
}

/* Prototype: int cumanascsi_2_dma_stop(host, SCpnt)
 * Purpose  : stops DMA/PIO
 * Params   : host  - host
 *	      SCpnt - command
 */
static void
cumanascsi_2_dma_stop(struct Scsi_Host *host, struct scsi_pointer *SCp)
{
	struct cumanascsi2_info *info = (struct cumanascsi2_info *)host->hostdata;
	if (info->info.scsi.dma != NO_DMA) {
		writeb(ALATCH_DIS_DMA, info->base + CUMANASCSI2_ALATCH);
		disable_dma(info->info.scsi.dma);
	}
}

/* Prototype: const char *cumanascsi_2_info(struct Scsi_Host * host)
 * Purpose  : returns a descriptive string about this interface,
 * Params   : host - driver host structure to return info for.
 * Returns  : pointer to a static buffer containing null terminated string.
 */
const char *cumanascsi_2_info(struct Scsi_Host *host)
{
	struct cumanascsi2_info *info = (struct cumanascsi2_info *)host->hostdata;
	static char string[150];

	sprintf(string, "%s (%s) in slot %d v%s terminators o%s",
		host->hostt->name, info->info.scsi.type, info->ec->slot_no,
		VERSION, info->terms ? "n" : "ff");

	return string;
}

/* Prototype: int cumanascsi_2_set_proc_info(struct Scsi_Host *host, char *buffer, int length)
 * Purpose  : Set a driver specific function
 * Params   : host   - host to setup
 *          : buffer - buffer containing string describing operation
 *          : length - length of string
 * Returns  : -EINVAL, or 0
 */
static int
cumanascsi_2_set_proc_info(struct Scsi_Host *host, char *buffer, int length)
{
	int ret = length;

	if (length >= 11 && strncmp(buffer, "CUMANASCSI2", 11) == 0) {
		buffer += 11;
		length -= 11;

		if (length >= 5 && strncmp(buffer, "term=", 5) == 0) {
			if (buffer[5] == '1')
				cumanascsi_2_terminator_ctl(host, 1);
			else if (buffer[5] == '0')
				cumanascsi_2_terminator_ctl(host, 0);
			else
				ret = -EINVAL;
		} else
			ret = -EINVAL;
	} else
		ret = -EINVAL;

	return ret;
}

static int cumanascsi_2_show_info(struct seq_file *m, struct Scsi_Host *host)
{
	struct cumanascsi2_info *info;
	info = (struct cumanascsi2_info *)host->hostdata;

	seq_printf(m, "Cumana SCSI II driver v%s\n", VERSION);
	fas216_print_host(&info->info, m);
	seq_printf(m, "Term    : o%s\n",
			info->terms ? "n" : "ff");

	fas216_print_stats(&info->info, m);
	fas216_print_devices(&info->info, m);
	return 0;
}

static struct scsi_host_template cumanascsi2_template = {
	.module				= THIS_MODULE,
	.show_info			= cumanascsi_2_show_info,
	.write_info			= cumanascsi_2_set_proc_info,
	.name				= "Cumana SCSI II",
	.info				= cumanascsi_2_info,
	.queuecommand			= fas216_queue_command,
	.eh_host_reset_handler		= fas216_eh_host_reset,
	.eh_bus_reset_handler		= fas216_eh_bus_reset,
	.eh_device_reset_handler	= fas216_eh_device_reset,
	.eh_abort_handler		= fas216_eh_abort,
	.can_queue			= 1,
	.this_id			= 7,
	.sg_tablesize			= SG_MAX_SEGMENTS,
	.dma_boundary			= IOMD_DMA_BOUNDARY,
	.proc_name			= "cumanascsi2",
};

static int cumanascsi2_probe(struct expansion_card *ec,
			     const struct ecard_id *id)
{
	struct Scsi_Host *host;
	struct cumanascsi2_info *info;
	void __iomem *base;
	int ret;

	ret = ecard_request_resources(ec);
	if (ret)
		goto out;

	base = ecardm_iomap(ec, ECARD_RES_MEMC, 0, 0);
	if (!base) {
		ret = -ENOMEM;
		goto out_region;
	}

	host = scsi_host_alloc(&cumanascsi2_template,
			       sizeof(struct cumanascsi2_info));
	if (!host) {
		ret = -ENOMEM;
		goto out_region;
	}

	ecard_set_drvdata(ec, host);

	info = (struct cumanascsi2_info *)host->hostdata;
	info->ec	= ec;
	info->base	= base;

	cumanascsi_2_terminator_ctl(host, term[ec->slot_no]);

	info->info.scsi.io_base		= base + CUMANASCSI2_FAS216_OFFSET;
	info->info.scsi.io_shift	= CUMANASCSI2_FAS216_SHIFT;
	info->info.scsi.irq		= ec->irq;
	info->info.scsi.dma		= ec->dma;
	info->info.ifcfg.clockrate	= 40; /* MHz */
	info->info.ifcfg.select_timeout	= 255;
	info->info.ifcfg.asyncperiod	= 200; /* ns */
	info->info.ifcfg.sync_max_depth	= 7;
	info->info.ifcfg.cntl3		= CNTL3_BS8 | CNTL3_FASTSCSI | CNTL3_FASTCLK;
	info->info.ifcfg.disconnect_ok	= 1;
	info->info.ifcfg.wide_max_size	= 0;
	info->info.ifcfg.capabilities	= FASCAP_PSEUDODMA;
	info->info.dma.setup		= cumanascsi_2_dma_setup;
	info->info.dma.pseudo		= cumanascsi_2_dma_pseudo;
	info->info.dma.stop		= cumanascsi_2_dma_stop;

	ec->irqaddr	= info->base + CUMANASCSI2_STATUS;
	ec->irqmask	= STATUS_INT;

	ecard_setirq(ec, &cumanascsi_2_ops, info);

	ret = fas216_init(host);
	if (ret)
		goto out_free;

	ret = request_irq(ec->irq, cumanascsi_2_intr,
			  0, "cumanascsi2", info);
	if (ret) {
		printk("scsi%d: IRQ%d not free: %d\n",
		       host->host_no, ec->irq, ret);
		goto out_release;
	}

	if (info->info.scsi.dma != NO_DMA) {
		if (request_dma(info->info.scsi.dma, "cumanascsi2")) {
			printk("scsi%d: DMA%d not free, using PIO\n",
			       host->host_no, info->info.scsi.dma);
			info->info.scsi.dma = NO_DMA;
		} else {
			set_dma_speed(info->info.scsi.dma, 180);
			info->info.ifcfg.capabilities |= FASCAP_DMA;
		}
	}

	ret = fas216_add(host, &ec->dev);
	if (ret == 0)
		goto out;

	if (info->info.scsi.dma != NO_DMA)
		free_dma(info->info.scsi.dma);
	free_irq(ec->irq, info);

 out_release:
	fas216_release(host);

 out_free:
	scsi_host_put(host);

 out_region:
	ecard_release_resources(ec);

 out:
	return ret;
}

static void cumanascsi2_remove(struct expansion_card *ec)
{
	struct Scsi_Host *host = ecard_get_drvdata(ec);
	struct cumanascsi2_info *info = (struct cumanascsi2_info *)host->hostdata;

	ecard_set_drvdata(ec, NULL);
	fas216_remove(host);

	if (info->info.scsi.dma != NO_DMA)
		free_dma(info->info.scsi.dma);
	free_irq(ec->irq, info);

	fas216_release(host);
	scsi_host_put(host);
	ecard_release_resources(ec);
}

static const struct ecard_id cumanascsi2_cids[] = {
	{ MANU_CUMANA, PROD_CUMANA_SCSI_2 },
	{ 0xffff, 0xffff },
};

static struct ecard_driver cumanascsi2_driver = {
	.probe		= cumanascsi2_probe,
	.remove		= cumanascsi2_remove,
	.id_table	= cumanascsi2_cids,
	.drv = {
		.name		= "cumanascsi2",
	},
};

static int __init cumanascsi2_init(void)
{
	return ecard_register_driver(&cumanascsi2_driver);
}

static void __exit cumanascsi2_exit(void)
{
	ecard_remove_driver(&cumanascsi2_driver);
}

module_init(cumanascsi2_init);
module_exit(cumanascsi2_exit);

MODULE_AUTHOR("Russell King");
MODULE_DESCRIPTION("Cumana SCSI-2 driver for Acorn machines");
module_param_array(term, int, NULL, 0);
MODULE_PARM_DESC(term, "SCSI bus termination");
MODULE_LICENSE("GPL");
