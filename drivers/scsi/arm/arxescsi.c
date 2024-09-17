// SPDX-License-Identifier: GPL-2.0-only
/*
 * linux/drivers/scsi/arm/arxescsi.c
 *
 * Copyright (C) 1997-2000 Russell King, Stefan Hanske
 *
 * This driver is based on experimentation.  Hence, it may have made
 * assumptions about the particular card that I have available, and
 * may not be reliable!
 *
 * Changelog:
 *  30-08-1997	RMK	0.0.0	Created, READONLY version as cumana_2.c
 *  22-01-1998	RMK	0.0.1	Updated to 2.1.80
 *  15-04-1998	RMK	0.0.1	Only do PIO if FAS216 will allow it.
 *  11-06-1998 	SH	0.0.2   Changed to support ARXE 16-bit SCSI card
 *				enabled writing
 *  01-01-2000	SH	0.1.0   Added *real* pseudo dma writing
 *				(arxescsi_pseudo_dma_write)
 *  02-04-2000	RMK	0.1.1	Updated for new error handling code.
 *  22-10-2000  SH		Updated for new registering scheme.
 */
#include <linux/module.h>
#include <linux/blkdev.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/ioport.h>
#include <linux/proc_fs.h>
#include <linux/unistd.h>
#include <linux/stat.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/interrupt.h>

#include <asm/dma.h>
#include <asm/io.h>
#include <asm/ecard.h>

#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_eh.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_tcq.h>
#include "fas216.h"

struct arxescsi_info {
	FAS216_Info		info;
	struct expansion_card	*ec;
	void __iomem		*base;
};

#define DMADATA_OFFSET	(0x200)

#define DMASTAT_OFFSET	(0x600)
#define DMASTAT_DRQ	(1 << 0)

#define CSTATUS_IRQ	(1 << 0)

#define VERSION "1.10 (23/01/2003 2.5.57)"

/*
 * Function: int arxescsi_dma_setup(host, SCpnt, direction, min_type)
 * Purpose : initialises DMA/PIO
 * Params  : host      - host
 *	     SCpnt     - command
 *	     direction - DMA on to/off of card
 *	     min_type  - minimum DMA support that we must have for this transfer
 * Returns : 0 if we should not set CMD_WITHDMA for transfer info command
 */
static fasdmatype_t
arxescsi_dma_setup(struct Scsi_Host *host, struct scsi_pointer *SCp,
		       fasdmadir_t direction, fasdmatype_t min_type)
{
	/*
	 * We don't do real DMA
	 */
	return fasdma_pseudo;
}

static void arxescsi_pseudo_dma_write(unsigned char *addr, void __iomem *base)
{
       __asm__ __volatile__(
       "               stmdb   sp!, {r0-r12}\n"
       "               mov     r3, %0\n"
       "               mov     r1, %1\n"
       "               add     r2, r1, #512\n"
       "               mov     r4, #256\n"
       ".loop_1:       ldmia   r3!, {r6, r8, r10, r12}\n"
       "               mov     r5, r6, lsl #16\n"
       "               mov     r7, r8, lsl #16\n"
       ".loop_2:       ldrb    r0, [r1, #1536]\n"
       "               tst     r0, #1\n"
       "               beq     .loop_2\n"
       "               stmia   r2, {r5-r8}\n\t"
       "               mov     r9, r10, lsl #16\n"
       "               mov     r11, r12, lsl #16\n"
       ".loop_3:       ldrb    r0, [r1, #1536]\n"
       "               tst     r0, #1\n"
       "               beq     .loop_3\n"
       "               stmia   r2, {r9-r12}\n"
       "               subs    r4, r4, #16\n"
       "               bne     .loop_1\n"
       "               ldmia   sp!, {r0-r12}\n"
       :
       : "r" (addr), "r" (base));
}

/*
 * Function: int arxescsi_dma_pseudo(host, SCpnt, direction, transfer)
 * Purpose : handles pseudo DMA
 * Params  : host      - host
 *	     SCpnt     - command
 *	     direction - DMA on to/off of card
 *	     transfer  - minimum number of bytes we expect to transfer
 */
static void
arxescsi_dma_pseudo(struct Scsi_Host *host, struct scsi_pointer *SCp,
		    fasdmadir_t direction, int transfer)
{
	struct arxescsi_info *info = (struct arxescsi_info *)host->hostdata;
	unsigned int length, error = 0;
	void __iomem *base = info->info.scsi.io_base;
	unsigned char *addr;

	length = SCp->this_residual;
	addr = SCp->ptr;

	if (direction == DMA_OUT) {
		unsigned int word;
		while (length > 256) {
			if (readb(base + 0x80) & STAT_INT) {
				error = 1;
				break;
			}
			arxescsi_pseudo_dma_write(addr, base);
			addr += 256;
			length -= 256;
		}

		if (!error)
			while (length > 0) {
				if (readb(base + 0x80) & STAT_INT)
					break;
	 
				if (!(readb(base + DMASTAT_OFFSET) & DMASTAT_DRQ))
					continue;

				word = *addr | *(addr + 1) << 8;

				writew(word, base + DMADATA_OFFSET);
				if (length > 1) {
					addr += 2;
					length -= 2;
				} else {
					addr += 1;
					length -= 1;
				}
			}
	}
	else {
		if (transfer && (transfer & 255)) {
			while (length >= 256) {
				if (readb(base + 0x80) & STAT_INT) {
					error = 1;
					break;
				}
	    
				if (!(readb(base + DMASTAT_OFFSET) & DMASTAT_DRQ))
					continue;

				readsw(base + DMADATA_OFFSET, addr, 256 >> 1);
				addr += 256;
				length -= 256;
			}
		}

		if (!(error))
			while (length > 0) {
				unsigned long word;

				if (readb(base + 0x80) & STAT_INT)
					break;

				if (!(readb(base + DMASTAT_OFFSET) & DMASTAT_DRQ))
					continue;

				word = readw(base + DMADATA_OFFSET);
				*addr++ = word;
				if (--length > 0) {
					*addr++ = word >> 8;
					length --;
				}
			}
	}
}

/*
 * Function: int arxescsi_dma_stop(host, SCpnt)
 * Purpose : stops DMA/PIO
 * Params  : host  - host
 *	     SCpnt - command
 */
static void arxescsi_dma_stop(struct Scsi_Host *host, struct scsi_pointer *SCp)
{
	/*
	 * no DMA to stop
	 */
}

/*
 * Function: const char *arxescsi_info(struct Scsi_Host * host)
 * Purpose : returns a descriptive string about this interface,
 * Params  : host - driver host structure to return info for.
 * Returns : pointer to a static buffer containing null terminated string.
 */
static const char *arxescsi_info(struct Scsi_Host *host)
{
	struct arxescsi_info *info = (struct arxescsi_info *)host->hostdata;
	static char string[150];

	sprintf(string, "%s (%s) in slot %d v%s",
		host->hostt->name, info->info.scsi.type, info->ec->slot_no,
		VERSION);

	return string;
}

static int
arxescsi_show_info(struct seq_file *m, struct Scsi_Host *host)
{
	struct arxescsi_info *info;
	info = (struct arxescsi_info *)host->hostdata;

	seq_printf(m, "ARXE 16-bit SCSI driver v%s\n", VERSION);
	fas216_print_host(&info->info, m);
	fas216_print_stats(&info->info, m);
	fas216_print_devices(&info->info, m);
	return 0;
}

static struct scsi_host_template arxescsi_template = {
	.show_info			= arxescsi_show_info,
	.name				= "ARXE SCSI card",
	.info				= arxescsi_info,
	.queuecommand			= fas216_noqueue_command,
	.eh_host_reset_handler		= fas216_eh_host_reset,
	.eh_bus_reset_handler		= fas216_eh_bus_reset,
	.eh_device_reset_handler	= fas216_eh_device_reset,
	.eh_abort_handler		= fas216_eh_abort,
	.cmd_size			= sizeof(struct fas216_cmd_priv),
	.can_queue			= 0,
	.this_id			= 7,
	.sg_tablesize			= SG_ALL,
	.dma_boundary			= PAGE_SIZE - 1,
	.proc_name			= "arxescsi",
};

static int arxescsi_probe(struct expansion_card *ec, const struct ecard_id *id)
{
	struct Scsi_Host *host;
	struct arxescsi_info *info;
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

	host = scsi_host_alloc(&arxescsi_template, sizeof(struct arxescsi_info));
	if (!host) {
		ret = -ENOMEM;
		goto out_region;
	}

	info = (struct arxescsi_info *)host->hostdata;
	info->ec = ec;
	info->base = base;

	info->info.scsi.io_base		= base + 0x2000;
	info->info.scsi.irq		= 0;
	info->info.scsi.dma		= NO_DMA;
	info->info.scsi.io_shift	= 5;
	info->info.ifcfg.clockrate	= 24; /* MHz */
	info->info.ifcfg.select_timeout = 255;
	info->info.ifcfg.asyncperiod	= 200; /* ns */
	info->info.ifcfg.sync_max_depth	= 0;
	info->info.ifcfg.cntl3		= CNTL3_FASTSCSI | CNTL3_FASTCLK;
	info->info.ifcfg.disconnect_ok	= 0;
	info->info.ifcfg.wide_max_size	= 0;
	info->info.ifcfg.capabilities	= FASCAP_PSEUDODMA;
	info->info.dma.setup		= arxescsi_dma_setup;
	info->info.dma.pseudo		= arxescsi_dma_pseudo;
	info->info.dma.stop		= arxescsi_dma_stop;
		
	ec->irqaddr = base;
	ec->irqmask = CSTATUS_IRQ;

	ret = fas216_init(host);
	if (ret)
		goto out_unregister;

	ret = fas216_add(host, &ec->dev);
	if (ret == 0)
		goto out;

	fas216_release(host);
 out_unregister:
	scsi_host_put(host);
 out_region:
	ecard_release_resources(ec);
 out:
	return ret;
}

static void arxescsi_remove(struct expansion_card *ec)
{
	struct Scsi_Host *host = ecard_get_drvdata(ec);

	ecard_set_drvdata(ec, NULL);
	fas216_remove(host);

	fas216_release(host);
	scsi_host_put(host);
	ecard_release_resources(ec);
}

static const struct ecard_id arxescsi_cids[] = {
	{ MANU_ARXE, PROD_ARXE_SCSI },
	{ 0xffff, 0xffff },
};

static struct ecard_driver arxescsi_driver = {
	.probe		= arxescsi_probe,
	.remove		= arxescsi_remove,
	.id_table	= arxescsi_cids,
	.drv = {
		.name		= "arxescsi",
	},
};

static int __init init_arxe_scsi_driver(void)
{
	return ecard_register_driver(&arxescsi_driver);
}

static void __exit exit_arxe_scsi_driver(void)
{
	ecard_remove_driver(&arxescsi_driver);
}

module_init(init_arxe_scsi_driver);
module_exit(exit_arxe_scsi_driver);

MODULE_AUTHOR("Stefan Hanske");
MODULE_DESCRIPTION("ARXESCSI driver for Acorn machines");
MODULE_LICENSE("GPL");

