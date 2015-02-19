/*
 * Qlogic FAS408 ISA card driver
 *
 * Copyright 1994, Tom Zerucha.   
 * tz@execpc.com
 * 
 * Redistributable under terms of the GNU General Public License
 *
 * For the avoidance of doubt the "preferred form" of this code is one which
 * is in an open non patent encumbered format. Where cryptographic key signing
 * forms part of the process of creating an executable the information
 * including keys needed to generate an equivalently functional executable
 * are deemed to be part of the source code.
 *
 * Check qlogicfas408.c for more credits and info.
 */

#include <linux/module.h>
#include <linux/blkdev.h>		/* to get disk capacity */
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/proc_fs.h>
#include <linux/unistd.h>
#include <linux/spinlock.h>
#include <linux/stat.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/dma.h>

#include "scsi.h"
#include <scsi/scsi_host.h>
#include "qlogicfas408.h"

/* Set the following to 2 to use normal interrupt (active high/totempole-
 * tristate), otherwise use 0 (REQUIRED FOR PCMCIA) for active low, open
 * drain
 */
#define INT_TYPE	2

static char qlogicfas_name[] = "qlogicfas";

/*
 *	Look for qlogic card and init if found 
 */
 
static struct Scsi_Host *__qlogicfas_detect(struct scsi_host_template *host,
								int qbase,
								int qlirq)
{
	int qltyp;		/* type of chip */
	int qinitid;
	struct Scsi_Host *hreg;	/* registered host structure */
	struct qlogicfas408_priv *priv;

	/*	Qlogic Cards only exist at 0x230 or 0x330 (the chip itself
	 *	decodes the address - I check 230 first since MIDI cards are
	 *	typically at 0x330
	 *
	 *	Theoretically, two Qlogic cards can coexist in the same system.
	 *	This should work by simply using this as a loadable module for
	 *	the second card, but I haven't tested this.
	 */

	if (!qbase || qlirq == -1)
		goto err;

	if (!request_region(qbase, 0x10, qlogicfas_name)) {
		printk(KERN_INFO "%s: address %#x is busy\n", qlogicfas_name,
							      qbase);
		goto err;
	}

	if (!qlogicfas408_detect(qbase, INT_TYPE)) {
		printk(KERN_WARNING "%s: probe failed for %#x\n",
								qlogicfas_name,
								qbase);
		goto err_release_mem;
	}

	printk(KERN_INFO "%s: Using preset base address of %03x,"
			 " IRQ %d\n", qlogicfas_name, qbase, qlirq);

	qltyp = qlogicfas408_get_chip_type(qbase, INT_TYPE);
	qinitid = host->this_id;
	if (qinitid < 0)
		qinitid = 7;	/* if no ID, use 7 */

	qlogicfas408_setup(qbase, qinitid, INT_TYPE);

	hreg = scsi_host_alloc(host, sizeof(struct qlogicfas408_priv));
	if (!hreg)
		goto err_release_mem;
	priv = get_priv_by_host(hreg);
	hreg->io_port = qbase;
	hreg->n_io_port = 16;
	hreg->dma_channel = -1;
	if (qlirq != -1)
		hreg->irq = qlirq;
	priv->qbase = qbase;
	priv->qlirq = qlirq;
	priv->qinitid = qinitid;
	priv->shost = hreg;
	priv->int_type = INT_TYPE;

	sprintf(priv->qinfo,
		"Qlogicfas Driver version 0.46, chip %02X at %03X, IRQ %d, TPdma:%d",
		qltyp, qbase, qlirq, QL_TURBO_PDMA);
	host->name = qlogicfas_name;

	if (request_irq(qlirq, qlogicfas408_ihandl, 0, qlogicfas_name, hreg))
		goto free_scsi_host;

	if (scsi_add_host(hreg, NULL))
		goto free_interrupt;

	scsi_scan_host(hreg);

	return hreg;

free_interrupt:
	free_irq(qlirq, hreg);

free_scsi_host:
	scsi_host_put(hreg);

err_release_mem:
	release_region(qbase, 0x10);
err:
	return NULL;
}

#define MAX_QLOGICFAS	8
static struct qlogicfas408_priv *cards;
static int iobase[MAX_QLOGICFAS];
static int irq[MAX_QLOGICFAS] = { [0 ... MAX_QLOGICFAS-1] = -1 };
module_param_array(iobase, int, NULL, 0);
module_param_array(irq, int, NULL, 0);
MODULE_PARM_DESC(iobase, "I/O address");
MODULE_PARM_DESC(irq, "IRQ");

static int qlogicfas_detect(struct scsi_host_template *sht)
{
	struct Scsi_Host *shost;
	struct qlogicfas408_priv *priv;
	int num;

	for (num = 0; num < MAX_QLOGICFAS; num++) {
		shost = __qlogicfas_detect(sht, iobase[num], irq[num]);
		if (shost == NULL) {
			/* no more devices */
			break;
		}
		priv = get_priv_by_host(shost);
		priv->next = cards;
		cards = priv;
	}

	return num;
}

static int qlogicfas_release(struct Scsi_Host *shost)
{
	struct qlogicfas408_priv *priv = get_priv_by_host(shost);

	scsi_remove_host(shost);
	if (shost->irq) {
		qlogicfas408_disable_ints(priv);	
		free_irq(shost->irq, shost);
	}
	if (shost->io_port && shost->n_io_port)
		release_region(shost->io_port, shost->n_io_port);
	scsi_host_put(shost);

	return 0;
}

/*
 *	The driver template is also needed for PCMCIA
 */
static struct scsi_host_template qlogicfas_driver_template = {
	.module			= THIS_MODULE,
	.name			= qlogicfas_name,
	.proc_name		= qlogicfas_name,
	.info			= qlogicfas408_info,
	.queuecommand		= qlogicfas408_queuecommand,
	.eh_abort_handler	= qlogicfas408_abort,
	.eh_bus_reset_handler	= qlogicfas408_bus_reset,
	.bios_param		= qlogicfas408_biosparam,
	.can_queue		= 1,
	.this_id		= -1,
	.sg_tablesize		= SG_ALL,
	.cmd_per_lun		= 1,
	.use_clustering		= DISABLE_CLUSTERING,
};

static __init int qlogicfas_init(void)
{
	if (!qlogicfas_detect(&qlogicfas_driver_template)) {
		/* no cards found */
		printk(KERN_INFO "%s: no cards were found, please specify "
				 "I/O address and IRQ using iobase= and irq= "
				 "options", qlogicfas_name);
		return -ENODEV;
	}

	return 0;
}

static __exit void qlogicfas_exit(void)
{
	struct qlogicfas408_priv *priv;

	for (priv = cards; priv != NULL; priv = priv->next)
		qlogicfas_release(priv->shost);
}

MODULE_AUTHOR("Tom Zerucha, Michael Griffith");
MODULE_DESCRIPTION("Driver for the Qlogic FAS408 based ISA card");
MODULE_LICENSE("GPL");
module_init(qlogicfas_init);
module_exit(qlogicfas_exit);

