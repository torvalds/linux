/*
 * Detection routine for the NCR53c710 based MVME16x SCSI Controllers for Linux.
 *
 * Based on work by Alan Hourihane
 */
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/blkdev.h>

#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/mvme16xhw.h>
#include <asm/irq.h>

#include "scsi.h"
#include <scsi/scsi_host.h>
#include "53c7xx.h"
#include "mvme16x.h"

#include<linux/stat.h>


int mvme16x_scsi_detect(struct scsi_host_template *tpnt)
{
    static unsigned char called = 0;
    int clock;
    long long options;

    if (!MACH_IS_MVME16x)
		return 0;
    if (mvme16x_config & MVME16x_CONFIG_NO_SCSICHIP) {
	printk ("SCSI detection disabled, SCSI chip not present\n");
	return 0;
    }
    if (called)
	return 0;

    tpnt->proc_name = "MVME16x";

    options = OPTION_MEMORY_MAPPED|OPTION_DEBUG_TEST1|OPTION_INTFLY|OPTION_SYNCHRONOUS|OPTION_ALWAYS_SYNCHRONOUS|OPTION_DISCONNECT;

    clock = 66000000;	/* 66MHz SCSI Clock */

    ncr53c7xx_init(tpnt, 0, 710, (unsigned long)0xfff47000,
			0, MVME16x_IRQ_SCSI, DMA_NONE,
			options, clock);
    called = 1;
    return 1;
}

static int mvme16x_scsi_release(struct Scsi_Host *shost)
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
	.name			= "MVME16x NCR53c710 SCSI",
	.detect			= mvme16x_scsi_detect,
	.release		= mvme16x_scsi_release,
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
