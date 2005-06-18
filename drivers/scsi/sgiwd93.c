/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
 * Copyright (C) 1999 Andrew R. Baker (andrewb@uab.edu)
 * Copyright (C) 2001 Florian Lohoff (flo@rfc822.org)
 * Copyright (C) 2003 Ralf Baechle (ralf@linux-mips.org)
 * 
 * (In all truth, Jed Schimmel wrote all this code.)
 */
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/blkdev.h>
#include <linux/version.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/spinlock.h>

#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/sgialib.h>
#include <asm/sgi/sgi.h>
#include <asm/sgi/mc.h>
#include <asm/sgi/hpc3.h>
#include <asm/sgi/ip22.h>
#include <asm/irq.h>
#include <asm/io.h>

#include "scsi.h"
#include <scsi/scsi_host.h>
#include "wd33c93.h"
#include "sgiwd93.h"

#include <linux/stat.h>

#if 0
#define DPRINTK(args...)	printk(args)
#else
#define DPRINTK(args...)
#endif

#define HDATA(ptr) ((struct ip22_hostdata *)((ptr)->hostdata))

struct ip22_hostdata {
	struct WD33C93_hostdata wh;
	struct hpc_data {
		dma_addr_t      dma;
		void            * cpu;
	} hd;
};

struct hpc_chunk {
	struct hpc_dma_desc desc;
	u32 _padding;	/* align to quadword boundary */
};

struct Scsi_Host *sgiwd93_host;
struct Scsi_Host *sgiwd93_host1;

/* Wuff wuff, wuff, wd33c93.c, wuff wuff, object oriented, bow wow. */
static inline void write_wd33c93_count(const wd33c93_regs regs,
                                      unsigned long value)
{
	*regs.SASR = WD_TRANSFER_COUNT_MSB;
	mb();
	*regs.SCMD = ((value >> 16) & 0xff);
	*regs.SCMD = ((value >>  8) & 0xff);
	*regs.SCMD = ((value >>  0) & 0xff);
	mb();
}

static inline unsigned long read_wd33c93_count(const wd33c93_regs regs)
{
	unsigned long value;

	*regs.SASR = WD_TRANSFER_COUNT_MSB;
	mb();
	value =  ((*regs.SCMD & 0xff) << 16);
	value |= ((*regs.SCMD & 0xff) <<  8);
	value |= ((*regs.SCMD & 0xff) <<  0);
	mb();
	return value;
}

static irqreturn_t sgiwd93_intr(int irq, void *dev_id, struct pt_regs *regs)
{
	struct Scsi_Host * host = (struct Scsi_Host *) dev_id;
	unsigned long flags;

	spin_lock_irqsave(host->host_lock, flags);
	wd33c93_intr(host);
	spin_unlock_irqrestore(host->host_lock, flags);

	return IRQ_HANDLED;
}

static inline
void fill_hpc_entries(struct hpc_chunk *hcp, Scsi_Cmnd *cmd, int datainp)
{
	unsigned long len = cmd->SCp.this_residual;
	void *addr = cmd->SCp.ptr;
	dma_addr_t physaddr;
	unsigned long count;

	physaddr = dma_map_single(NULL, addr, len, cmd->sc_data_direction);
	cmd->SCp.dma_handle = physaddr;

	while (len) {
		/*
		 * even cntinfo could be up to 16383, without
		 * magic only 8192 works correctly
		 */
		count = len > 8192 ? 8192 : len;
		hcp->desc.pbuf = physaddr;
		hcp->desc.cntinfo = count;
		hcp++;
		len -= count;
		physaddr += count;
	}

	/*
	 * To make sure, if we trip an HPC bug, that we transfer every single
	 * byte, we tag on an extra zero length dma descriptor at the end of
	 * the chain.
	 */
	hcp->desc.pbuf = 0;
	hcp->desc.cntinfo = HPCDMA_EOX;
}

static int dma_setup(Scsi_Cmnd *cmd, int datainp)
{
	struct ip22_hostdata *hdata = HDATA(cmd->device->host);
	struct hpc3_scsiregs *hregs =
		(struct hpc3_scsiregs *) cmd->device->host->base;
	struct hpc_chunk *hcp = (struct hpc_chunk *) hdata->hd.cpu;

	DPRINTK("dma_setup: datainp<%d> hcp<%p> ", datainp, hcp);

	hdata->wh.dma_dir = datainp;

	/*
	 * wd33c93 shouldn't pass us bogus dma_setups, but it does:-(  The
	 * other wd33c93 drivers deal with it the same way (which isn't that
	 * obvious).  IMHO a better fix would be, not to do these dma setups
	 * in the first place.
	 */
	if (cmd->SCp.ptr == NULL || cmd->SCp.this_residual == 0)
		return 1;

	fill_hpc_entries(hcp, cmd, datainp);

	DPRINTK(" HPCGO\n");

	/* Start up the HPC. */
	hregs->ndptr = hdata->hd.dma;
	if (datainp)
		hregs->ctrl = HPC3_SCTRL_ACTIVE;
	else
		hregs->ctrl = HPC3_SCTRL_ACTIVE | HPC3_SCTRL_DIR;

	return 0;
}

static void dma_stop(struct Scsi_Host *instance, Scsi_Cmnd *SCpnt,
		     int status)
{
	struct ip22_hostdata *hdata = HDATA(instance);
	struct hpc3_scsiregs *hregs;

	if (!SCpnt)
		return;

	hregs = (struct hpc3_scsiregs *) SCpnt->device->host->base;

	DPRINTK("dma_stop: status<%d> ", status);

	/* First stop the HPC and flush it's FIFO. */
	if (hdata->wh.dma_dir) {
		hregs->ctrl |= HPC3_SCTRL_FLUSH;
		while (hregs->ctrl & HPC3_SCTRL_ACTIVE)
			barrier();
	}
	hregs->ctrl = 0;
	dma_unmap_single(NULL, SCpnt->SCp.dma_handle, SCpnt->SCp.this_residual,
	                 SCpnt->sc_data_direction);

	DPRINTK("\n");
}

void sgiwd93_reset(unsigned long base)
{
	struct hpc3_scsiregs *hregs = (struct hpc3_scsiregs *) base;

	hregs->ctrl = HPC3_SCTRL_CRESET;
	udelay(50);
	hregs->ctrl = 0;
}

static inline void init_hpc_chain(struct hpc_data *hd)
{
	struct hpc_chunk *hcp = (struct hpc_chunk *) hd->cpu;
	struct hpc_chunk *dma = (struct hpc_chunk *) hd->dma;
	unsigned long start, end;

	start = (unsigned long) hcp;
	end = start + PAGE_SIZE;
	while (start < end) {
		hcp->desc.pnext = (u32) (dma + 1);
		hcp->desc.cntinfo = HPCDMA_EOX;
		hcp++; dma++;
		start += sizeof(struct hpc_chunk);
	};
	hcp--;
	hcp->desc.pnext = hd->dma;
}

static struct Scsi_Host * __init sgiwd93_setup_scsi(
	Scsi_Host_Template *SGIblows, int unit, int irq,
	struct hpc3_scsiregs *hregs, unsigned char *wdregs)
{
	struct ip22_hostdata *hdata;
	struct Scsi_Host *host;
	wd33c93_regs regs;

	host = scsi_register(SGIblows, sizeof(struct ip22_hostdata));
	if (!host)
		return NULL;

	host->base = (unsigned long) hregs;
	host->irq = irq;

	hdata = HDATA(host);
	hdata->hd.cpu = dma_alloc_coherent(NULL, PAGE_SIZE, &hdata->hd.dma,
	                                   GFP_KERNEL);
	if (!hdata->hd.cpu) {
		printk(KERN_WARNING "sgiwd93: Could not allocate memory for "
		       "host %d buffer.\n", unit);
		goto out_unregister;
	}
	init_hpc_chain(&hdata->hd);

	regs.SASR = wdregs + 3;
	regs.SCMD = wdregs + 7;

	wd33c93_init(host, regs, dma_setup, dma_stop, WD33C93_FS_16_20);

	hdata->wh.no_sync = 0;

	if (request_irq(irq, sgiwd93_intr, 0, "SGI WD93", (void *) host)) {
		printk(KERN_WARNING "sgiwd93: Could not register irq %d "
		       "for host %d.\n", irq, unit);
		goto out_free;
	}
	return host;

out_free:
	dma_free_coherent(NULL, PAGE_SIZE, hdata->hd.cpu, hdata->hd.dma);
	wd33c93_release();

out_unregister:
	scsi_unregister(host);

	return NULL;
}

int __init sgiwd93_detect(Scsi_Host_Template *SGIblows)
{
	int found = 0;

	SGIblows->proc_name = "SGIWD93";
	sgiwd93_host = sgiwd93_setup_scsi(SGIblows, 0, SGI_WD93_0_IRQ,
	                                  &hpc3c0->scsi_chan0,
	                                  (unsigned char *)hpc3c0->scsi0_ext);
	if (sgiwd93_host)
		found++;

	/* Set up second controller on the Indigo2 */
	if (ip22_is_fullhouse()) {
		sgiwd93_host1 = sgiwd93_setup_scsi(SGIblows, 1, SGI_WD93_1_IRQ,
		                          &hpc3c0->scsi_chan1,
		                          (unsigned char *)hpc3c0->scsi1_ext);
		if (sgiwd93_host1)
			found++;
	}

	return found;
}

int sgiwd93_release(struct Scsi_Host *instance)
{
	struct ip22_hostdata *hdata = HDATA(instance);
	int irq = 0;

	if (sgiwd93_host && sgiwd93_host == instance)
		irq = SGI_WD93_0_IRQ;
	else if (sgiwd93_host1 && sgiwd93_host1 == instance)
		irq = SGI_WD93_1_IRQ;

	free_irq(irq, sgiwd93_intr);
	dma_free_coherent(NULL, PAGE_SIZE, hdata->hd.cpu, hdata->hd.dma);
	wd33c93_release();

	return 1;
}

static int sgiwd93_bus_reset(Scsi_Cmnd *cmd)
{
	/* FIXME perform bus-specific reset */

	/* FIXME 2: kill this function, and let midlayer fallback
	   to the same result, calling wd33c93_host_reset() */

	spin_lock_irq(cmd->device->host->host_lock);
	wd33c93_host_reset(cmd);
	spin_unlock_irq(cmd->device->host->host_lock);

	return SUCCESS;
}

/*
 * Kludge alert - the SCSI code calls the abort and reset method with int
 * arguments not with pointers.  So this is going to blow up beautyfully
 * on 64-bit systems with memory outside the compat address spaces.
 */
static Scsi_Host_Template driver_template = {
	.proc_name		= "SGIWD93",
	.name			= "SGI WD93",
	.detect			= sgiwd93_detect,
	.release		= sgiwd93_release,
	.queuecommand		= wd33c93_queuecommand,
	.eh_abort_handler	= wd33c93_abort,
	.eh_bus_reset_handler	= sgiwd93_bus_reset,
	.eh_host_reset_handler	= wd33c93_host_reset,
	.can_queue		= CAN_QUEUE,
	.this_id		= 7,
	.sg_tablesize		= SG_ALL,
	.cmd_per_lun		= CMD_PER_LUN,
	.use_clustering		= DISABLE_CLUSTERING,
};
#include "scsi_module.c"
