#include <linux/types.h>
#include <linux/mm.h>
#include <linux/blkdev.h>
#include <linux/interrupt.h>

#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/mvme147hw.h>
#include <asm/irq.h>

#include "scsi.h"
#include <scsi/scsi_host.h>
#include "wd33c93.h"
#include "mvme147.h"

#include <linux/stat.h>


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
	struct Scsi_Host *instance = cmd->device->host;
	struct WD33C93_hostdata *hdata = shost_priv(instance);
	unsigned char flags = 0x01;
	unsigned long addr = virt_to_bus(cmd->SCp.ptr);

	/* setup dma direction */
	if (!dir_in)
		flags |= 0x04;

	/* remember direction */
	hdata->dma_dir = dir_in;

	if (dir_in) {
		/* invalidate any cache */
		cache_clear(addr, cmd->SCp.this_residual);
	} else {
		/* push any dirty cache */
		cache_push(addr, cmd->SCp.this_residual);
	}

	/* start DMA */
	m147_pcc->dma_bcr = cmd->SCp.this_residual | (1 << 24);
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

int mvme147_detect(struct scsi_host_template *tpnt)
{
	static unsigned char called = 0;
	struct Scsi_Host *instance;
	wd33c93_regs regs;
	struct WD33C93_hostdata *hdata;

	if (!MACH_IS_MVME147 || called)
		return 0;
	called++;

	tpnt->proc_name = "MVME147";
	tpnt->show_info = wd33c93_show_info,
	tpnt->write_info = wd33c93_write_info,

	instance = scsi_register(tpnt, sizeof(struct WD33C93_hostdata));
	if (!instance)
		goto err_out;

	instance->base = 0xfffe4000;
	instance->irq = MVME147_IRQ_SCSI_PORT;
	regs.SASR = (volatile unsigned char *)0xfffe4000;
	regs.SCMD = (volatile unsigned char *)0xfffe4001;
	hdata = shost_priv(instance);
	hdata->no_sync = 0xff;
	hdata->fast = 0;
	hdata->dma_mode = CTRL_DMA;
	wd33c93_init(instance, regs, dma_setup, dma_stop, WD33C93_FS_8_10);

	if (request_irq(MVME147_IRQ_SCSI_PORT, mvme147_intr, 0,
			"MVME147 SCSI PORT", instance))
		goto err_unregister;
	if (request_irq(MVME147_IRQ_SCSI_DMA, mvme147_intr, 0,
			"MVME147 SCSI DMA", instance))
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

	return 1;

err_free_irq:
	free_irq(MVME147_IRQ_SCSI_PORT, mvme147_intr);
err_unregister:
	scsi_unregister(instance);
err_out:
	return 0;
}

static struct scsi_host_template driver_template = {
	.proc_name		= "MVME147",
	.name			= "MVME147 built-in SCSI",
	.detect			= mvme147_detect,
	.release		= mvme147_release,
	.queuecommand		= wd33c93_queuecommand,
	.eh_abort_handler	= wd33c93_abort,
	.eh_host_reset_handler	= wd33c93_host_reset,
	.can_queue		= CAN_QUEUE,
	.this_id		= 7,
	.sg_tablesize		= SG_ALL,
	.cmd_per_lun		= CMD_PER_LUN,
	.use_clustering		= ENABLE_CLUSTERING
};


#include "scsi_module.c"

int mvme147_release(struct Scsi_Host *instance)
{
#ifdef MODULE
	/* XXX Make sure DMA is stopped! */
	free_irq(MVME147_IRQ_SCSI_PORT, mvme147_intr);
	free_irq(MVME147_IRQ_SCSI_DMA, mvme147_intr);
#endif
	return 1;
}
