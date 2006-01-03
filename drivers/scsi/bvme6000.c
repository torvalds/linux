/*
 * Detection routine for the NCR53c710 based BVME6000 SCSI Controllers for Linux.
 *
 * Based on work by Alan Hourihane
 */
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/blkdev.h>
#include <linux/sched.h>
#include <linux/zorro.h>

#include <asm/setup.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/bvme6000hw.h>
#include <asm/irq.h>

#include "scsi.h"
#include <scsi/scsi_host.h>
#include "53c7xx.h"
#include "bvme6000.h"

#include<linux/stat.h>


int bvme6000_scsi_detect(struct scsi_host_template *tpnt)
{
    static unsigned char called = 0;
    int clock;
    long long options;

    if (called)
	return 0;
    if (!MACH_IS_BVME6000)
	return 0;

    tpnt->proc_name = "BVME6000";

    options = OPTION_MEMORY_MAPPED|OPTION_DEBUG_TEST1|OPTION_INTFLY|OPTION_SYNCHRONOUS|OPTION_ALWAYS_SYNCHRONOUS|OPTION_DISCONNECT;

    clock = 40000000;	/* 66MHz SCSI Clock */

    ncr53c7xx_init(tpnt, 0, 710, (unsigned long)BVME_NCR53C710_BASE,
			0, BVME_IRQ_SCSI, DMA_NONE,
			options, clock);
    called = 1;
    return 1;
}

static int bvme6000_scsi_release(struct Scsi_Host *shost)
{
	if (shost->irq)
		free_irq(shost->irq, NULL);
	if (shost->dma_channel != 0xff)
		free_dma(shost->dma_channel);
	if (shost->io_port && shost->n_io_port)
		release_region(shost->io_port, shost->n_io_port);
	scsi_unregister(shost);
	return 0;
}

static struct scsi_host_template driver_template = {
	.name			= "BVME6000 NCR53c710 SCSI",
	.detect			= bvme6000_scsi_detect,
	.release		= bvme6000_scsi_release,
	.queuecommand		= NCR53c7xx_queue_command,
	.abort			= NCR53c7xx_abort,
	.reset			= NCR53c7xx_reset,
	.can_queue		= 24,
	.this_id		= 7,
	.sg_tablesize		= 63,
	.cmd_per_lun		= 3,
	.use_clustering		= DISABLE_CLUSTERING
};


#include "scsi_module.c"
