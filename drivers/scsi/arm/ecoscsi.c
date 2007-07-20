#define AUTOSENSE
/* #define PSEUDO_DMA */

/*
 * EcoSCSI Generic NCR5380 driver
 *
 * Copyright 1995, Russell King
 *
 * ALPHA RELEASE 1.
 *
 * For more information, please consult
 *
 * NCR 5380 Family
 * SCSI Protocol Controller
 * Databook
 *
 * NCR Microelectronics
 * 1635 Aeroplaza Drive
 * Colorado Springs, CO 80916
 * 1+ (719) 578-3400
 * 1+ (800) 334-5454
 */

#include <linux/module.h>
#include <linux/signal.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/blkdev.h>

#include <asm/io.h>
#include <asm/system.h>

#include "../scsi.h"
#include <scsi/scsi_host.h>

#define priv(host)			((struct NCR5380_hostdata *)(host)->hostdata)

#define NCR5380_local_declare()		void __iomem *_base
#define NCR5380_setup(host)		_base = priv(host)->base

#define NCR5380_read(reg)		({ writeb(reg | 8, _base); readb(_base + 4); })
#define NCR5380_write(reg, value)	({ writeb(reg | 8, _base); writeb(value, _base + 4); })

#define NCR5380_intr			ecoscsi_intr
#define NCR5380_queue_command		ecoscsi_queue_command
#define NCR5380_proc_info		ecoscsi_proc_info

#define NCR5380_implementation_fields	\
	void __iomem *base

#include "../NCR5380.h"

#define ECOSCSI_PUBLIC_RELEASE 1

/*
 * Function : ecoscsi_setup(char *str, int *ints)
 *
 * Purpose : LILO command line initialization of the overrides array,
 *
 * Inputs : str - unused, ints - array of integer parameters with ints[0]
 *	equal to the number of ints.
 *
 */

void ecoscsi_setup(char *str, int *ints)
{
}

const char * ecoscsi_info (struct Scsi_Host *spnt)
{
	return "";
}

#define BOARD_NORMAL	0
#define BOARD_NCR53C400	1

#include "../NCR5380.c"

static struct scsi_host_template ecoscsi_template =  {
	.module		= THIS_MODULE,
	.name		= "Serial Port EcoSCSI NCR5380",
	.proc_name	= "ecoscsi",
	.info		= ecoscsi_info,
	.queuecommand	= ecoscsi_queue_command,
	.eh_abort_handler	= NCR5380_abort,
	.eh_bus_reset_handler	= NCR5380_bus_reset,
	.can_queue	= 16,
	.this_id	= 7,
	.sg_tablesize	= SG_ALL,
	.cmd_per_lun	= 2,
	.use_clustering	= DISABLE_CLUSTERING
};

static struct Scsi_Host *host;

static int __init ecoscsi_init(void)
{
	void __iomem *_base;
	int ret;

	if (!request_mem_region(0x33a0000, 4096, "ecoscsi")) {
		ret = -EBUSY;
		goto out;
	}

	_base = ioremap(0x33a0000, 4096);
	if (!_base) {
		ret = -ENOMEM;
		goto out_release;
	}

	NCR5380_write(MODE_REG, 0x20);		/* Is it really SCSI? */
	if (NCR5380_read(MODE_REG) != 0x20)	/* Write to a reg.    */
		goto out_unmap;

	NCR5380_write(MODE_REG, 0x00);		/* it back.	      */
	if (NCR5380_read(MODE_REG) != 0x00)
		goto out_unmap;

	host = scsi_host_alloc(tpnt, sizeof(struct NCR5380_hostdata));
	if (!host) {
		ret = -ENOMEM;
		goto out_unmap;
	}

	priv(host)->base = _base;
	host->irq = IRQ_NONE;

	NCR5380_init(host, 0);

	printk("scsi%d: at port 0x%08lx irqs disabled", host->host_no, host->io_port);
	printk(" options CAN_QUEUE=%d CMD_PER_LUN=%d release=%d",
		host->can_queue, host->cmd_per_lun, ECOSCSI_PUBLIC_RELEASE);
	printk("\nscsi%d:", host->host_no);
	NCR5380_print_options(host);
	printk("\n");

	scsi_add_host(host, NULL); /* XXX handle failure */
	scsi_scan_host(host);
	return 0;

 out_unmap:
	iounmap(_base);
 out_release:
	release_mem_region(0x33a0000, 4096);
 out:
	return ret;
}

static void __exit ecoscsi_exit(void)
{
	scsi_remove_host(host);
	NCR5380_exit(host);
	scsi_host_put(host);
	release_mem_region(0x33a0000, 4096);
	return 0;
}

module_init(ecoscsi_init);
module_exit(ecoscsi_exit);

MODULE_AUTHOR("Russell King");
MODULE_DESCRIPTION("Econet-SCSI driver for Acorn machines");
MODULE_LICENSE("GPL");

