// SPDX-License-Identifier: GPL-2.0-only
#include <linux/types.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/zorro.h>
#include <linux/module.h>

#include <asm/page.h>
#include <asm/amigaints.h>
#include <asm/amigahw.h>

#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_eh.h>
#include <scsi/scsi_tcq.h>
#include "wd33c93.h"
#include "a2091.h"


struct a2091_hostdata {
	struct WD33C93_hostdata wh;
	struct a2091_scsiregs *regs;
	struct device *dev;
};

#define DMA_DIR(d)   ((d == DATA_OUT_DIR) ? DMA_TO_DEVICE : DMA_FROM_DEVICE)

static irqreturn_t a2091_intr(int irq, void *data)
{
	struct Scsi_Host *instance = data;
	struct a2091_hostdata *hdata = shost_priv(instance);
	unsigned int status = hdata->regs->ISTR;
	unsigned long flags;

	if (!(status & (ISTR_INT_F | ISTR_INT_P)) || !(status & ISTR_INTS))
		return IRQ_NONE;

	spin_lock_irqsave(instance->host_lock, flags);
	wd33c93_intr(instance);
	spin_unlock_irqrestore(instance->host_lock, flags);
	return IRQ_HANDLED;
}

static int dma_setup(struct scsi_cmnd *cmd, int dir_in)
{
	struct scsi_pointer *scsi_pointer = WD33C93_scsi_pointer(cmd);
	unsigned long len = scsi_pointer->this_residual;
	struct Scsi_Host *instance = cmd->device->host;
	struct a2091_hostdata *hdata = shost_priv(instance);
	struct WD33C93_hostdata *wh = &hdata->wh;
	struct a2091_scsiregs *regs = hdata->regs;
	unsigned short cntr = CNTR_PDMD | CNTR_INTEN;
	dma_addr_t addr;

	addr = dma_map_single(hdata->dev, scsi_pointer->ptr,
			      len, DMA_DIR(dir_in));
	if (dma_mapping_error(hdata->dev, addr)) {
		dev_warn(hdata->dev, "cannot map SCSI data block %p\n",
			 scsi_pointer->ptr);
		return 1;
	}
	scsi_pointer->dma_handle = addr;

	/* don't allow DMA if the physical address is bad */
	if (addr & A2091_XFER_MASK) {
		/* drop useless mapping */
		dma_unmap_single(hdata->dev, scsi_pointer->dma_handle,
				 scsi_pointer->this_residual,
				 DMA_DIR(dir_in));
		scsi_pointer->dma_handle = (dma_addr_t) NULL;

		wh->dma_bounce_len = (scsi_pointer->this_residual + 511) & ~0x1ff;
		wh->dma_bounce_buffer = kmalloc(wh->dma_bounce_len,
						GFP_KERNEL);

		/* can't allocate memory; use PIO */
		if (!wh->dma_bounce_buffer) {
			wh->dma_bounce_len = 0;
			return 1;
		}

		if (!dir_in) {
			/* copy to bounce buffer for a write */
			memcpy(wh->dma_bounce_buffer, scsi_pointer->ptr,
			       scsi_pointer->this_residual);
		}

		/* will flush/invalidate cache for us */
		addr = dma_map_single(hdata->dev, wh->dma_bounce_buffer,
				      wh->dma_bounce_len, DMA_DIR(dir_in));
		/* can't map buffer; use PIO */
		if (dma_mapping_error(hdata->dev, addr)) {
			dev_warn(hdata->dev, "cannot map bounce buffer %p\n",
				 wh->dma_bounce_buffer);
			return 1;
		}

		/* the bounce buffer may not be in the first 16M of physmem */
		if (addr & A2091_XFER_MASK) {
			/* we could use chipmem... maybe later */
			kfree(wh->dma_bounce_buffer);
			wh->dma_bounce_buffer = NULL;
			wh->dma_bounce_len = 0;
			return 1;
		}

		scsi_pointer->dma_handle = addr;
	}

	/* setup dma direction */
	if (!dir_in)
		cntr |= CNTR_DDIR;

	/* remember direction */
	wh->dma_dir = dir_in;

	regs->CNTR = cntr;

	/* setup DMA *physical* address */
	regs->ACR = addr;

	/* no more cache flush here - dma_map_single() takes care */

	/* start DMA */
	regs->ST_DMA = 1;

	/* return success */
	return 0;
}

static void dma_stop(struct Scsi_Host *instance, struct scsi_cmnd *SCpnt,
		     int status)
{
	struct scsi_pointer *scsi_pointer = WD33C93_scsi_pointer(SCpnt);
	struct a2091_hostdata *hdata = shost_priv(instance);
	struct WD33C93_hostdata *wh = &hdata->wh;
	struct a2091_scsiregs *regs = hdata->regs;

	/* disable SCSI interrupts */
	unsigned short cntr = CNTR_PDMD;

	if (!wh->dma_dir)
		cntr |= CNTR_DDIR;

	/* disable SCSI interrupts */
	regs->CNTR = cntr;

	/* flush if we were reading */
	if (wh->dma_dir) {
		regs->FLUSH = 1;
		while (!(regs->ISTR & ISTR_FE_FLG))
			;
	}

	/* clear a possible interrupt */
	regs->CINT = 1;

	/* stop DMA */
	regs->SP_DMA = 1;

	/* restore the CONTROL bits (minus the direction flag) */
	regs->CNTR = CNTR_PDMD | CNTR_INTEN;

	dma_unmap_single(hdata->dev, scsi_pointer->dma_handle,
			 scsi_pointer->this_residual,
			 DMA_DIR(wh->dma_dir));

	/* copy from a bounce buffer, if necessary */
	if (status && wh->dma_bounce_buffer) {
		if (wh->dma_dir)
			memcpy(scsi_pointer->ptr, wh->dma_bounce_buffer,
			       scsi_pointer->this_residual);
		kfree(wh->dma_bounce_buffer);
		wh->dma_bounce_buffer = NULL;
		wh->dma_bounce_len = 0;
	}
}

static struct scsi_host_template a2091_scsi_template = {
	.module			= THIS_MODULE,
	.name			= "Commodore A2091/A590 SCSI",
	.show_info		= wd33c93_show_info,
	.write_info		= wd33c93_write_info,
	.proc_name		= "A2901",
	.queuecommand		= wd33c93_queuecommand,
	.eh_abort_handler	= wd33c93_abort,
	.eh_host_reset_handler	= wd33c93_host_reset,
	.can_queue		= CAN_QUEUE,
	.this_id		= 7,
	.sg_tablesize		= SG_ALL,
	.cmd_per_lun		= CMD_PER_LUN,
	.dma_boundary		= PAGE_SIZE - 1,
	.cmd_size		= sizeof(struct scsi_pointer),
};

static int a2091_probe(struct zorro_dev *z, const struct zorro_device_id *ent)
{
	struct Scsi_Host *instance;
	int error;
	struct a2091_scsiregs *regs;
	wd33c93_regs wdregs;
	struct a2091_hostdata *hdata;

	if (dma_set_mask_and_coherent(&z->dev, DMA_BIT_MASK(24))) {
		dev_warn(&z->dev, "cannot use 24 bit DMA\n");
		return -ENODEV;
	}

	if (!request_mem_region(z->resource.start, 256, "wd33c93"))
		return -EBUSY;

	instance = scsi_host_alloc(&a2091_scsi_template,
				   sizeof(struct a2091_hostdata));
	if (!instance) {
		error = -ENOMEM;
		goto fail_alloc;
	}

	instance->irq = IRQ_AMIGA_PORTS;
	instance->unique_id = z->slotaddr;

	regs = ZTWO_VADDR(z->resource.start);
	regs->DAWR = DAWR_A2091;

	wdregs.SASR = &regs->SASR;
	wdregs.SCMD = &regs->SCMD;

	hdata = shost_priv(instance);
	hdata->dev = &z->dev;
	hdata->wh.no_sync = 0xff;
	hdata->wh.fast = 0;
	hdata->wh.dma_mode = CTRL_DMA;
	hdata->regs = regs;

	wd33c93_init(instance, wdregs, dma_setup, dma_stop, WD33C93_FS_8_10);
	error = request_irq(IRQ_AMIGA_PORTS, a2091_intr, IRQF_SHARED,
			    "A2091 SCSI", instance);
	if (error)
		goto fail_irq;

	regs->CNTR = CNTR_PDMD | CNTR_INTEN;

	error = scsi_add_host(instance, NULL);
	if (error)
		goto fail_host;

	zorro_set_drvdata(z, instance);

	scsi_scan_host(instance);
	return 0;

fail_host:
	free_irq(IRQ_AMIGA_PORTS, instance);
fail_irq:
	scsi_host_put(instance);
fail_alloc:
	release_mem_region(z->resource.start, 256);
	return error;
}

static void a2091_remove(struct zorro_dev *z)
{
	struct Scsi_Host *instance = zorro_get_drvdata(z);
	struct a2091_hostdata *hdata = shost_priv(instance);

	hdata->regs->CNTR = 0;
	scsi_remove_host(instance);
	free_irq(IRQ_AMIGA_PORTS, instance);
	scsi_host_put(instance);
	release_mem_region(z->resource.start, 256);
}

static struct zorro_device_id a2091_zorro_tbl[] = {
	{ ZORRO_PROD_CBM_A590_A2091_1 },
	{ ZORRO_PROD_CBM_A590_A2091_2 },
	{ 0 }
};
MODULE_DEVICE_TABLE(zorro, a2091_zorro_tbl);

static struct zorro_driver a2091_driver = {
	.name		= "a2091",
	.id_table	= a2091_zorro_tbl,
	.probe		= a2091_probe,
	.remove		= a2091_remove,
};

static int __init a2091_init(void)
{
	return zorro_register_driver(&a2091_driver);
}
module_init(a2091_init);

static void __exit a2091_exit(void)
{
	zorro_unregister_driver(&a2091_driver);
}
module_exit(a2091_exit);

MODULE_DESCRIPTION("Commodore A2091/A590 SCSI");
MODULE_LICENSE("GPL");
