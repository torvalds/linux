/*
 * Generic Macintosh NCR5380 driver
 *
 * Copyright 1998, Michael Schmitz <mschmitz@lbl.gov>
 *
 * derived in part from:
 */
/*
 * Generic Generic NCR5380 driver
 *
 * Copyright 1995, Russell King
 */

#include <linux/types.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>

#include <asm/hwtest.h>
#include <asm/io.h>
#include <asm/macints.h>
#include <asm/setup.h>

#include <scsi/scsi_host.h>

/* Definitions for the core NCR5380 driver. */

#define PSEUDO_DMA

#define NCR5380_implementation_fields   unsigned char *pdma_base
#define NCR5380_local_declare()         struct Scsi_Host *_instance
#define NCR5380_setup(instance)         _instance = instance

#define NCR5380_read(reg)               macscsi_read(_instance, reg)
#define NCR5380_write(reg, value)       macscsi_write(_instance, reg, value)

#define NCR5380_pread                   macscsi_pread
#define NCR5380_pwrite                  macscsi_pwrite

#define NCR5380_intr                    macscsi_intr
#define NCR5380_queue_command           macscsi_queue_command
#define NCR5380_abort                   macscsi_abort
#define NCR5380_bus_reset               macscsi_bus_reset
#define NCR5380_info                    macscsi_info
#define NCR5380_show_info               macscsi_show_info
#define NCR5380_write_info              macscsi_write_info

#include "NCR5380.h"

#define RESET_BOOT

static int setup_can_queue = -1;
module_param(setup_can_queue, int, 0);
static int setup_cmd_per_lun = -1;
module_param(setup_cmd_per_lun, int, 0);
static int setup_sg_tablesize = -1;
module_param(setup_sg_tablesize, int, 0);
static int setup_use_pdma = -1;
module_param(setup_use_pdma, int, 0);
static int setup_use_tagged_queuing = -1;
module_param(setup_use_tagged_queuing, int, 0);
static int setup_hostid = -1;
module_param(setup_hostid, int, 0);

/* Time (in jiffies) to wait after a reset; the SCSI standard calls for 250ms,
 * we usually do 0.5s to be on the safe side. But Toshiba CD-ROMs once more
 * need ten times the standard value... */
#define TOSHIBA_DELAY

#ifdef TOSHIBA_DELAY
#define	AFTER_RESET_DELAY	(5*HZ/2)
#else
#define	AFTER_RESET_DELAY	(HZ/2)
#endif

/*
 * NCR 5380 register access functions
 */

static inline char macscsi_read(struct Scsi_Host *instance, int reg)
{
	return in_8(instance->base + (reg << 4));
}

static inline void macscsi_write(struct Scsi_Host *instance, int reg, int value)
{
	out_8(instance->base + (reg << 4), value);
}

#ifndef MODULE
static int __init mac_scsi_setup(char *str)
{
	int ints[7];

	(void)get_options(str, ARRAY_SIZE(ints), ints);

	if (ints[0] < 1 || ints[0] > 6) {
		pr_err("Usage: mac5380=<can_queue>[,<cmd_per_lun>[,<sg_tablesize>[,<hostid>[,<use_tags>[,<use_pdma>]]]]]\n");
		return 0;
	}
	if (ints[0] >= 1)
		setup_can_queue = ints[1];
	if (ints[0] >= 2)
		setup_cmd_per_lun = ints[2];
	if (ints[0] >= 3)
		setup_sg_tablesize = ints[3];
	if (ints[0] >= 4)
		setup_hostid = ints[4];
	if (ints[0] >= 5)
		setup_use_tagged_queuing = ints[5];
	if (ints[0] >= 6)
		setup_use_pdma = ints[6];
	return 1;
}

__setup("mac5380=", mac_scsi_setup);
#endif /* !MODULE */

#ifdef RESET_BOOT
/*
 * Our 'bus reset on boot' function
 */

static void mac_scsi_reset_boot(struct Scsi_Host *instance)
{
	unsigned long end;

	NCR5380_local_declare();
	NCR5380_setup(instance);
	
	/*
	 * Do a SCSI reset to clean up the bus during initialization. No messing
	 * with the queues, interrupts, or locks necessary here.
	 */

	printk(KERN_INFO "Macintosh SCSI: resetting the SCSI bus..." );

	/* get in phase */
	NCR5380_write( TARGET_COMMAND_REG,
		      PHASE_SR_TO_TCR( NCR5380_read(STATUS_REG) ));

	/* assert RST */
	NCR5380_write( INITIATOR_COMMAND_REG, ICR_BASE | ICR_ASSERT_RST );
	/* The min. reset hold time is 25us, so 40us should be enough */
	udelay( 50 );
	/* reset RST and interrupt */
	NCR5380_write( INITIATOR_COMMAND_REG, ICR_BASE );
	NCR5380_read( RESET_PARITY_INTERRUPT_REG );

	for( end = jiffies + AFTER_RESET_DELAY; time_before(jiffies, end); )
		barrier();

	printk(KERN_INFO " done\n" );
}
#endif

#ifdef PSEUDO_DMA
/* 
   Pseudo-DMA: (Ove Edlund)
   The code attempts to catch bus errors that occur if one for example
   "trips over the cable".
   XXX: Since bus errors in the PDMA routines never happen on my 
   computer, the bus error code is untested. 
   If the code works as intended, a bus error results in Pseudo-DMA 
   being disabled, meaning that the driver switches to slow handshake.
   If bus errors are NOT extremely rare, this has to be changed. 
*/

#define CP_IO_TO_MEM(s,d,len)				\
__asm__ __volatile__					\
    ("    cmp.w  #4,%2\n"				\
     "    bls    8f\n"					\
     "    move.w %1,%%d0\n"				\
     "    neg.b  %%d0\n"				\
     "    and.w  #3,%%d0\n"				\
     "    sub.w  %%d0,%2\n"				\
     "    bra    2f\n"					\
     " 1: move.b (%0),(%1)+\n"				\
     " 2: dbf    %%d0,1b\n"				\
     "    move.w %2,%%d0\n"				\
     "    lsr.w  #5,%%d0\n"				\
     "    bra    4f\n"					\
     " 3: move.l (%0),(%1)+\n"				\
     "31: move.l (%0),(%1)+\n"				\
     "32: move.l (%0),(%1)+\n"				\
     "33: move.l (%0),(%1)+\n"				\
     "34: move.l (%0),(%1)+\n"				\
     "35: move.l (%0),(%1)+\n"				\
     "36: move.l (%0),(%1)+\n"				\
     "37: move.l (%0),(%1)+\n"				\
     " 4: dbf    %%d0,3b\n"				\
     "    move.w %2,%%d0\n"				\
     "    lsr.w  #2,%%d0\n"				\
     "    and.w  #7,%%d0\n"				\
     "    bra    6f\n"					\
     " 5: move.l (%0),(%1)+\n"				\
     " 6: dbf    %%d0,5b\n"				\
     "    and.w  #3,%2\n"				\
     "    bra    8f\n"					\
     " 7: move.b (%0),(%1)+\n"				\
     " 8: dbf    %2,7b\n"				\
     "    moveq.l #0, %2\n"				\
     " 9: \n"						\
     ".section .fixup,\"ax\"\n"				\
     "    .even\n"					\
     "90: moveq.l #1, %2\n"				\
     "    jra 9b\n"					\
     ".previous\n"					\
     ".section __ex_table,\"a\"\n"			\
     "   .align 4\n"					\
     "   .long  1b,90b\n"				\
     "   .long  3b,90b\n"				\
     "   .long 31b,90b\n"				\
     "   .long 32b,90b\n"				\
     "   .long 33b,90b\n"				\
     "   .long 34b,90b\n"				\
     "   .long 35b,90b\n"				\
     "   .long 36b,90b\n"				\
     "   .long 37b,90b\n"				\
     "   .long  5b,90b\n"				\
     "   .long  7b,90b\n"				\
     ".previous"					\
     : "=a"(s), "=a"(d), "=d"(len)			\
     : "0"(s), "1"(d), "2"(len)				\
     : "d0")

static int macscsi_pread(struct Scsi_Host *instance,
                         unsigned char *dst, int len)
{
	struct NCR5380_hostdata *hostdata = shost_priv(instance);
	unsigned char *d;
	unsigned char *s;

	NCR5380_local_declare();
	NCR5380_setup(instance);

	s = hostdata->pdma_base + (INPUT_DATA_REG << 4);
	d = dst;

	/* These conditions are derived from MacOS */

	while (!(NCR5380_read(BUS_AND_STATUS_REG) & BASR_DRQ) &&
	       !(NCR5380_read(STATUS_REG) & SR_REQ))
		;

	if (!(NCR5380_read(BUS_AND_STATUS_REG) & BASR_DRQ) &&
	    (NCR5380_read(BUS_AND_STATUS_REG) & BASR_PHASE_MATCH)) {
		pr_err("Error in macscsi_pread\n");
		return -1;
	}

	CP_IO_TO_MEM(s, d, len);

	if (len != 0) {
		pr_notice("Bus error in macscsi_pread\n");
		return -1;
	}

	return 0;
}


#define CP_MEM_TO_IO(s,d,len)				\
__asm__ __volatile__					\
    ("    cmp.w  #4,%2\n"				\
     "    bls    8f\n"					\
     "    move.w %0,%%d0\n"				\
     "    neg.b  %%d0\n"				\
     "    and.w  #3,%%d0\n"				\
     "    sub.w  %%d0,%2\n"				\
     "    bra    2f\n"					\
     " 1: move.b (%0)+,(%1)\n"				\
     " 2: dbf    %%d0,1b\n"				\
     "    move.w %2,%%d0\n"				\
     "    lsr.w  #5,%%d0\n"				\
     "    bra    4f\n"					\
     " 3: move.l (%0)+,(%1)\n"				\
     "31: move.l (%0)+,(%1)\n"				\
     "32: move.l (%0)+,(%1)\n"				\
     "33: move.l (%0)+,(%1)\n"				\
     "34: move.l (%0)+,(%1)\n"				\
     "35: move.l (%0)+,(%1)\n"				\
     "36: move.l (%0)+,(%1)\n"				\
     "37: move.l (%0)+,(%1)\n"				\
     " 4: dbf    %%d0,3b\n"				\
     "    move.w %2,%%d0\n"				\
     "    lsr.w  #2,%%d0\n"				\
     "    and.w  #7,%%d0\n"				\
     "    bra    6f\n"					\
     " 5: move.l (%0)+,(%1)\n"				\
     " 6: dbf    %%d0,5b\n"				\
     "    and.w  #3,%2\n"				\
     "    bra    8f\n"					\
     " 7: move.b (%0)+,(%1)\n"				\
     " 8: dbf    %2,7b\n"				\
     "    moveq.l #0, %2\n"				\
     " 9: \n"						\
     ".section .fixup,\"ax\"\n"				\
     "    .even\n"					\
     "90: moveq.l #1, %2\n"				\
     "    jra 9b\n"					\
     ".previous\n"					\
     ".section __ex_table,\"a\"\n"			\
     "   .align 4\n"					\
     "   .long  1b,90b\n"				\
     "   .long  3b,90b\n"				\
     "   .long 31b,90b\n"				\
     "   .long 32b,90b\n"				\
     "   .long 33b,90b\n"				\
     "   .long 34b,90b\n"				\
     "   .long 35b,90b\n"				\
     "   .long 36b,90b\n"				\
     "   .long 37b,90b\n"				\
     "   .long  5b,90b\n"				\
     "   .long  7b,90b\n"				\
     ".previous"					\
     : "=a"(s), "=a"(d), "=d"(len)			\
     : "0"(s), "1"(d), "2"(len)				\
     : "d0")

static int macscsi_pwrite(struct Scsi_Host *instance,
                          unsigned char *src, int len)
{
	struct NCR5380_hostdata *hostdata = shost_priv(instance);
	unsigned char *s;
	unsigned char *d;

	NCR5380_local_declare();
	NCR5380_setup(instance);

	s = src;
	d = hostdata->pdma_base + (OUTPUT_DATA_REG << 4);

	/* These conditions are derived from MacOS */

	while (!(NCR5380_read(BUS_AND_STATUS_REG) & BASR_DRQ) &&
	       (!(NCR5380_read(STATUS_REG) & SR_REQ) ||
	        (NCR5380_read(BUS_AND_STATUS_REG) & BASR_PHASE_MATCH)))
		;

	if (!(NCR5380_read(BUS_AND_STATUS_REG) & BASR_DRQ)) {
		pr_err("Error in macscsi_pwrite\n");
		return -1;
	}

	CP_MEM_TO_IO(s, d, len);

	if (len != 0) {
		pr_notice("Bus error in macscsi_pwrite\n");
		return -1;
	}

	return 0;
}
#endif

#include "NCR5380.c"

#define DRV_MODULE_NAME         "mac_scsi"
#define PFX                     DRV_MODULE_NAME ": "

static struct scsi_host_template mac_scsi_template = {
	.module				= THIS_MODULE,
	.proc_name			= DRV_MODULE_NAME,
	.show_info			= macscsi_show_info,
	.write_info			= macscsi_write_info,
	.name				= "Macintosh NCR5380 SCSI",
	.info				= macscsi_info,
	.queuecommand			= macscsi_queue_command,
	.eh_abort_handler		= macscsi_abort,
	.eh_bus_reset_handler		= macscsi_bus_reset,
	.can_queue			= 16,
	.this_id			= 7,
	.sg_tablesize			= SG_ALL,
	.cmd_per_lun			= 2,
	.use_clustering			= DISABLE_CLUSTERING
};

static int __init mac_scsi_probe(struct platform_device *pdev)
{
	struct Scsi_Host *instance;
	int error;
	int host_flags = 0;
	struct resource *irq, *pio_mem, *pdma_mem = NULL;

	pio_mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!pio_mem)
		return -ENODEV;

#ifdef PSEUDO_DMA
	pdma_mem = platform_get_resource(pdev, IORESOURCE_MEM, 1);
#endif

	irq = platform_get_resource(pdev, IORESOURCE_IRQ, 0);

	if (!hwreg_present((unsigned char *)pio_mem->start +
	                   (STATUS_REG << 4))) {
		pr_info(PFX "no device detected at %pap\n", &pio_mem->start);
		return -ENODEV;
	}

	if (setup_can_queue > 0)
		mac_scsi_template.can_queue = setup_can_queue;
	if (setup_cmd_per_lun > 0)
		mac_scsi_template.cmd_per_lun = setup_cmd_per_lun;
	if (setup_sg_tablesize >= 0)
		mac_scsi_template.sg_tablesize = setup_sg_tablesize;
	if (setup_hostid >= 0)
		mac_scsi_template.this_id = setup_hostid & 7;
	if (setup_use_pdma < 0)
		setup_use_pdma = 0;

	instance = scsi_host_alloc(&mac_scsi_template,
	                           sizeof(struct NCR5380_hostdata));
	if (!instance)
		return -ENOMEM;

	instance->base = pio_mem->start;
	if (irq)
		instance->irq = irq->start;
	else
		instance->irq = NO_IRQ;

	if (pdma_mem && setup_use_pdma) {
		struct NCR5380_hostdata *hostdata = shost_priv(instance);

		hostdata->pdma_base = (unsigned char *)pdma_mem->start;
	} else
		host_flags |= FLAG_NO_PSEUDO_DMA;

#ifdef RESET_BOOT
	mac_scsi_reset_boot(instance);
#endif

#ifdef SUPPORT_TAGS
	host_flags |= setup_use_tagged_queuing > 0 ? FLAG_TAGGED_QUEUING : 0;
#endif

	NCR5380_init(instance, host_flags);

	if (instance->irq != NO_IRQ) {
		error = request_irq(instance->irq, macscsi_intr, IRQF_SHARED,
		                    "NCR5380", instance);
		if (error)
			goto fail_irq;
	}

	error = scsi_add_host(instance, NULL);
	if (error)
		goto fail_host;

	platform_set_drvdata(pdev, instance);

	scsi_scan_host(instance);
	return 0;

fail_host:
	if (instance->irq != NO_IRQ)
		free_irq(instance->irq, instance);
fail_irq:
	NCR5380_exit(instance);
	scsi_host_put(instance);
	return error;
}

static int __exit mac_scsi_remove(struct platform_device *pdev)
{
	struct Scsi_Host *instance = platform_get_drvdata(pdev);

	scsi_remove_host(instance);
	if (instance->irq != NO_IRQ)
		free_irq(instance->irq, instance);
	NCR5380_exit(instance);
	scsi_host_put(instance);
	return 0;
}

static struct platform_driver mac_scsi_driver = {
	.remove = __exit_p(mac_scsi_remove),
	.driver = {
		.name	= DRV_MODULE_NAME,
		.owner	= THIS_MODULE,
	},
};

module_platform_driver_probe(mac_scsi_driver, mac_scsi_probe);

MODULE_ALIAS("platform:" DRV_MODULE_NAME);
MODULE_LICENSE("GPL");
