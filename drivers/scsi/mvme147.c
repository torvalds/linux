// SPDX-License-Identifier: GPL-2.0
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/blkdev.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include <asm/page.h>
#include <asm/mvme147hw.h>
#include <asm/irq.h>

#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_eh.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_tcq.h>
#include "wd33c93.h"
#include "mvme147.h"

static irqreturn_t mvme147_intr(int irq, void *data)
{
	struct Scsi_Host *instance = data;

	if (irq == MVME147_IRQ_SCSI_PORT)
		wd33c93_intr(instance);
	else
		m147_pcc->dma_intr = 0x89;	/* Ack and enable ints */
	return IRQ_HANDLED;
}

static int dma_setup(struct scsi_cmnd *cmd, int dir_in)
{
	struct scsi_pointer *scsi_pointer = WD33C93_scsi_pointer(cmd);
	struct Scsi_Host *instance = cmd->device->host;
	struct WD33C93_hostdata *hdata = shost_priv(instance);
	unsigned char flags = 0x01;
	unsigned long addr = virt_to_bus(scsi_pointer->ptr);

	/* setup dma direction */
	if (!dir_in)
		flags |= 0x04;

	/* remember direction */
	hdata->dma_dir = dir_in;

	if (dir_in) {
		/* invalidate any cache */
		cache_clear(addr, scsi_pointer->this_residual);
	} else {
		/* push any dirty cache */
		cache_push(addr, scsi_pointer->this_residual);
	}

	/* start DMA */
	m147_pcc->dma_bcr = scsi_pointer->this_residual | (1 << 24);
	m147_pcc->dma_dadr = addr;
	m147_pcc->dma_cntrl = flags;

	/* return success */
	return 0;
}

static void dma_stop(struct Scsi_Host *instance, struct scsi_cmnd *SCpnt,
		     int status)
{
	m147_pcc->dma_cntrl = 0;
}

static struct scsi_host_template mvme147_host_template = {
	.module			= THIS_MODULE,
	.proc_name		= "MVME147",
	.name			= "MVME147 built-in SCSI",
	.queuecommand		= wd33c93_queuecommand,
	.eh_abort_handler	= wd33c93_abort,
	.eh_host_reset_handler	= wd33c93_host_reset,
	.show_info		= wd33c93_show_info,
	.write_info		= wd33c93_write_info,
	.can_queue		= CAN_QUEUE,
	.this_id		= 7,
	.sg_tablesize		= SG_ALL,
	.cmd_per_lun		= CMD_PER_LUN,
	.cmd_size		= sizeof(struct scsi_pointer),
};

static struct Scsi_Host *mvme147_shost;

static int __init mvme147_init(void)
{
	wd33c93_regs regs;
	struct WD33C93_hostdata *hdata;
	int error = -ENOMEM;

	if (!MACH_IS_MVME147)
		return 0;

	mvme147_shost = scsi_host_alloc(&mvme147_host_template,
			sizeof(struct WD33C93_hostdata));
	if (!mvme147_shost)
		goto err_out;
	mvme147_shost->base = 0xfffe4000;
	mvme147_shost->irq = MVME147_IRQ_SCSI_PORT;

	regs.SASR = (volatile unsigned char *)0xfffe4000;
	regs.SCMD = (volatile unsigned char *)0xfffe4001;

	hdata = shost_priv(mvme147_shost);
	hdata->no_sync = 0xff;
	hdata->fast = 0;
	hdata->dma_mode = CTRL_DMA;

	wd33c93_init(mvme147_shost, regs, dma_setup, dma_stop, WD33C93_FS_8_10);

	error = request_irq(MVME147_IRQ_SCSI_PORT, mvme147_intr, 0,
			"MVME147 SCSI PORT", mvme147_shost);
	if (error)
		goto err_unregister;
	error = request_irq(MVME147_IRQ_SCSI_DMA, mvme147_intr, 0,
			"MVME147 SCSI DMA", mvme147_shost);
	if (error)
		goto err_free_irq;
#if 0	/* Disabled; causes problems booting */
	m147_pcc->scsi_interrupt = 0x10;	/* Assert SCSI bus reset */
	udelay(100);
	m147_pcc->scsi_interrupt = 0x00;	/* Negate SCSI bus reset */
	udelay(2000);
	m147_pcc->scsi_interrupt = 0x40;	/* Clear bus reset interrupt */
#endif
	m147_pcc->scsi_interrupt = 0x09;	/* Enable interrupt */

	m147_pcc->dma_cntrl = 0x00;	/* ensure DMA is stopped */
	m147_pcc->dma_intr = 0x89;	/* Ack and enable ints */

	error = scsi_add_host(mvme147_shost, NULL);
	if (error)
		goto err_free_irq;
	scsi_scan_host(mvme147_shost);
	return 0;

err_free_irq:
	free_irq(MVME147_IRQ_SCSI_PORT, mvme147_shost);
err_unregister:
	scsi_host_put(mvme147_shost);
err_out:
	return error;
}

static void __exit mvme147_exit(void)
{
	scsi_remove_host(mvme147_shost);

	/* XXX Make sure DMA is stopped! */
	free_irq(MVME147_IRQ_SCSI_PORT, mvme147_shost);
	free_irq(MVME147_IRQ_SCSI_DMA, mvme147_shost);

	scsi_host_put(mvme147_shost);
}

module_init(mvme147_init);
module_exit(mvme147_exit);
