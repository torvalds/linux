/*
 * Detection routine for the NCR53c710 based Amiga SCSI Controllers for Linux.
 *		Amiga MacroSystemUS WarpEngine SCSI controller.
 *		Amiga Technologies A4000T SCSI controller.
 *		Amiga Technologies/DKB A4091 SCSI controller.
 *
 * Written 1997 by Alan Hourihane <alanh@fairlite.demon.co.uk>
 * plus modifications of the 53c7xx.c driver to support the Amiga.
 */
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/blkdev.h>
#include <linux/sched.h>
#include <linux/config.h>
#include <linux/zorro.h>
#include <linux/stat.h>

#include <asm/setup.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/amigaints.h>
#include <asm/amigahw.h>
#include <asm/dma.h>
#include <asm/irq.h>

#include "scsi.h"
#include <scsi/scsi_host.h>
#include "53c7xx.h"
#include "amiga7xx.h"


static int amiga7xx_register_one(struct scsi_host_template *tpnt,
				 unsigned long address)
{
    long long options;
    int clock;

    if (!request_mem_region(address, 0x1000, "ncr53c710"))
	return 0;

    address = (unsigned long)z_ioremap(address, 0x1000);
    options = OPTION_MEMORY_MAPPED | OPTION_DEBUG_TEST1 | OPTION_INTFLY |
	      OPTION_SYNCHRONOUS | OPTION_ALWAYS_SYNCHRONOUS |
	      OPTION_DISCONNECT;
    clock = 50000000;	/* 50 MHz SCSI Clock */
    ncr53c7xx_init(tpnt, 0, 710, address, 0, IRQ_AMIGA_PORTS, DMA_NONE,
		   options, clock);
    return 1;
}


#ifdef CONFIG_ZORRO

static struct {
    zorro_id id;
    unsigned long offset;
    int absolute;	/* offset is absolute address */
} amiga7xx_table[] = {
    { .id = ZORRO_PROD_PHASE5_BLIZZARD_603E_PLUS, .offset = 0xf40000,
      .absolute = 1 },
    { .id = ZORRO_PROD_MACROSYSTEMS_WARP_ENGINE_40xx, .offset = 0x40000 },
    { .id = ZORRO_PROD_CBM_A4091_1, .offset = 0x800000 },
    { .id = ZORRO_PROD_CBM_A4091_2, .offset = 0x800000 },
    { .id = ZORRO_PROD_GVP_GFORCE_040_060, .offset = 0x40000 },
    { 0 }
};

static int __init amiga7xx_zorro_detect(struct scsi_host_template *tpnt)
{
    int num = 0, i;
    struct zorro_dev *z = NULL;
    unsigned long address;

    while ((z = zorro_find_device(ZORRO_WILDCARD, z))) {
	for (i = 0; amiga7xx_table[i].id; i++)
	    if (z->id == amiga7xx_table[i].id)
		break;
	if (!amiga7xx_table[i].id)
	    continue;
	if (amiga7xx_table[i].absolute)
	    address = amiga7xx_table[i].offset;
	else
	    address = z->resource.start + amiga7xx_table[i].offset;
	num += amiga7xx_register_one(tpnt, address);
    }
    return num;
}

#endif /* CONFIG_ZORRO */


int __init amiga7xx_detect(struct scsi_host_template *tpnt)
{
    static unsigned char called = 0;
    int num = 0;

    if (called || !MACH_IS_AMIGA)
	return 0;

    tpnt->proc_name = "Amiga7xx";

    if (AMIGAHW_PRESENT(A4000_SCSI))
	num += amiga7xx_register_one(tpnt, 0xdd0040);

#ifdef CONFIG_ZORRO
    num += amiga7xx_zorro_detect(tpnt);
#endif

    called = 1;
    return num;
}

static int amiga7xx_release(struct Scsi_Host *shost)
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
	.name			= "Amiga NCR53c710 SCSI",
	.detect			= amiga7xx_detect,
	.release		= amiga7xx_release,
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
