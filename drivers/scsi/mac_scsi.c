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

#define NCR5380_implementation_fields   int pdma_residual

#define NCR5380_read(reg)           in_8(hostdata->io + ((reg) << 4))
#define NCR5380_write(reg, value)   out_8(hostdata->io + ((reg) << 4), value)

#define NCR5380_dma_xfer_len            macscsi_dma_xfer_len
#define NCR5380_dma_recv_setup          macscsi_pread
#define NCR5380_dma_send_setup          macscsi_pwrite
#define NCR5380_dma_residual            macscsi_dma_residual

#define NCR5380_intr                    macscsi_intr
#define NCR5380_queue_command           macscsi_queue_command
#define NCR5380_abort                   macscsi_abort
#define NCR5380_bus_reset               macscsi_bus_reset
#define NCR5380_info                    macscsi_info

#include "NCR5380.h"

static int setup_can_queue = -1;
module_param(setup_can_queue, int, 0);
static int setup_cmd_per_lun = -1;
module_param(setup_cmd_per_lun, int, 0);
static int setup_sg_tablesize = -1;
module_param(setup_sg_tablesize, int, 0);
static int setup_use_pdma = -1;
module_param(setup_use_pdma, int, 0);
static int setup_hostid = -1;
module_param(setup_hostid, int, 0);
static int setup_toshiba_delay = -1;
module_param(setup_toshiba_delay, int, 0);

#ifndef MODULE
static int __init mac_scsi_setup(char *str)
{
	int ints[8];

	(void)get_options(str, ARRAY_SIZE(ints), ints);

	if (ints[0] < 1) {
		pr_err("Usage: mac5380=<can_queue>[,<cmd_per_lun>[,<sg_tablesize>[,<hostid>[,<use_tags>[,<use_pdma>[,<toshiba_delay>]]]]]]\n");
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
	/* ints[5] (use_tagged_queuing) is ignored */
	if (ints[0] >= 6)
		setup_use_pdma = ints[6];
	if (ints[0] >= 7)
		setup_toshiba_delay = ints[7];
	return 1;
}

__setup("mac5380=", mac_scsi_setup);
#endif /* !MODULE */

/* Pseudo DMA asm originally by Ove Edlund */

#define CP_IO_TO_MEM(s,d,n)				\
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
     "91: moveq.l #1, %2\n"				\
     "    jra 9b\n"					\
     "94: moveq.l #4, %2\n"				\
     "    jra 9b\n"					\
     ".previous\n"					\
     ".section __ex_table,\"a\"\n"			\
     "   .align 4\n"					\
     "   .long  1b,91b\n"				\
     "   .long  3b,94b\n"				\
     "   .long 31b,94b\n"				\
     "   .long 32b,94b\n"				\
     "   .long 33b,94b\n"				\
     "   .long 34b,94b\n"				\
     "   .long 35b,94b\n"				\
     "   .long 36b,94b\n"				\
     "   .long 37b,94b\n"				\
     "   .long  5b,94b\n"				\
     "   .long  7b,91b\n"				\
     ".previous"					\
     : "=a"(s), "=a"(d), "=d"(n)			\
     : "0"(s), "1"(d), "2"(n)				\
     : "d0")

static inline int macscsi_pread(struct NCR5380_hostdata *hostdata,
                                unsigned char *dst, int len)
{
	unsigned char *s = hostdata->pdma_io + (INPUT_DATA_REG << 4);
	unsigned char *d = dst;
	int n = len;
	int transferred;

	while (!NCR5380_poll_politely(hostdata, BUS_AND_STATUS_REG,
	                              BASR_DRQ | BASR_PHASE_MATCH,
	                              BASR_DRQ | BASR_PHASE_MATCH, HZ / 64)) {
		CP_IO_TO_MEM(s, d, n);

		transferred = d - dst - n;
		hostdata->pdma_residual = len - transferred;

		/* No bus error. */
		if (n == 0)
			return 0;

		/* Target changed phase early? */
		if (NCR5380_poll_politely2(hostdata, STATUS_REG, SR_REQ, SR_REQ,
		                           BUS_AND_STATUS_REG, BASR_ACK, BASR_ACK, HZ / 64) < 0)
			scmd_printk(KERN_ERR, hostdata->connected,
			            "%s: !REQ and !ACK\n", __func__);
		if (!(NCR5380_read(BUS_AND_STATUS_REG) & BASR_PHASE_MATCH))
			return 0;

		dsprintk(NDEBUG_PSEUDO_DMA, hostdata->host,
		         "%s: bus error (%d/%d)\n", __func__, transferred, len);
		NCR5380_dprint(NDEBUG_PSEUDO_DMA, hostdata->host);
		d = dst + transferred;
		n = len - transferred;
	}

	scmd_printk(KERN_ERR, hostdata->connected,
	            "%s: phase mismatch or !DRQ\n", __func__);
	NCR5380_dprint(NDEBUG_PSEUDO_DMA, hostdata->host);
	return -1;
}


#define CP_MEM_TO_IO(s,d,n)				\
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
     "91: moveq.l #1, %2\n"				\
     "    jra 9b\n"					\
     "94: moveq.l #4, %2\n"				\
     "    jra 9b\n"					\
     ".previous\n"					\
     ".section __ex_table,\"a\"\n"			\
     "   .align 4\n"					\
     "   .long  1b,91b\n"				\
     "   .long  3b,94b\n"				\
     "   .long 31b,94b\n"				\
     "   .long 32b,94b\n"				\
     "   .long 33b,94b\n"				\
     "   .long 34b,94b\n"				\
     "   .long 35b,94b\n"				\
     "   .long 36b,94b\n"				\
     "   .long 37b,94b\n"				\
     "   .long  5b,94b\n"				\
     "   .long  7b,91b\n"				\
     ".previous"					\
     : "=a"(s), "=a"(d), "=d"(n)			\
     : "0"(s), "1"(d), "2"(n)				\
     : "d0")

static inline int macscsi_pwrite(struct NCR5380_hostdata *hostdata,
                                 unsigned char *src, int len)
{
	unsigned char *s = src;
	unsigned char *d = hostdata->pdma_io + (OUTPUT_DATA_REG << 4);
	int n = len;
	int transferred;

	while (!NCR5380_poll_politely(hostdata, BUS_AND_STATUS_REG,
	                              BASR_DRQ | BASR_PHASE_MATCH,
	                              BASR_DRQ | BASR_PHASE_MATCH, HZ / 64)) {
		CP_MEM_TO_IO(s, d, n);

		transferred = s - src - n;
		hostdata->pdma_residual = len - transferred;

		/* Target changed phase early? */
		if (NCR5380_poll_politely2(hostdata, STATUS_REG, SR_REQ, SR_REQ,
		                           BUS_AND_STATUS_REG, BASR_ACK, BASR_ACK, HZ / 64) < 0)
			scmd_printk(KERN_ERR, hostdata->connected,
			            "%s: !REQ and !ACK\n", __func__);
		if (!(NCR5380_read(BUS_AND_STATUS_REG) & BASR_PHASE_MATCH))
			return 0;

		/* No bus error. */
		if (n == 0) {
			if (NCR5380_poll_politely(hostdata, TARGET_COMMAND_REG,
			                          TCR_LAST_BYTE_SENT,
			                          TCR_LAST_BYTE_SENT, HZ / 64) < 0)
				scmd_printk(KERN_ERR, hostdata->connected,
				            "%s: Last Byte Sent timeout\n", __func__);
			return 0;
		}

		dsprintk(NDEBUG_PSEUDO_DMA, hostdata->host,
		         "%s: bus error (%d/%d)\n", __func__, transferred, len);
		NCR5380_dprint(NDEBUG_PSEUDO_DMA, hostdata->host);
		s = src + transferred;
		n = len - transferred;
	}

	scmd_printk(KERN_ERR, hostdata->connected,
	            "%s: phase mismatch or !DRQ\n", __func__);
	NCR5380_dprint(NDEBUG_PSEUDO_DMA, hostdata->host);

	return -1;
}

static int macscsi_dma_xfer_len(struct NCR5380_hostdata *hostdata,
                                struct scsi_cmnd *cmd)
{
	if (hostdata->flags & FLAG_NO_PSEUDO_DMA ||
	    cmd->SCp.this_residual < 16)
		return 0;

	return cmd->SCp.this_residual;
}

static int macscsi_dma_residual(struct NCR5380_hostdata *hostdata)
{
	return hostdata->pdma_residual;
}

#include "NCR5380.c"

#define DRV_MODULE_NAME         "mac_scsi"
#define PFX                     DRV_MODULE_NAME ": "

static struct scsi_host_template mac_scsi_template = {
	.module			= THIS_MODULE,
	.proc_name		= DRV_MODULE_NAME,
	.name			= "Macintosh NCR5380 SCSI",
	.info			= macscsi_info,
	.queuecommand		= macscsi_queue_command,
	.eh_abort_handler	= macscsi_abort,
	.eh_bus_reset_handler	= macscsi_bus_reset,
	.can_queue		= 16,
	.this_id		= 7,
	.sg_tablesize		= 1,
	.cmd_per_lun		= 2,
	.use_clustering		= DISABLE_CLUSTERING,
	.cmd_size		= NCR5380_CMD_SIZE,
	.max_sectors		= 128,
};

static int __init mac_scsi_probe(struct platform_device *pdev)
{
	struct Scsi_Host *instance;
	struct NCR5380_hostdata *hostdata;
	int error;
	int host_flags = 0;
	struct resource *irq, *pio_mem, *pdma_mem = NULL;

	pio_mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!pio_mem)
		return -ENODEV;

	pdma_mem = platform_get_resource(pdev, IORESOURCE_MEM, 1);

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

	instance = scsi_host_alloc(&mac_scsi_template,
	                           sizeof(struct NCR5380_hostdata));
	if (!instance)
		return -ENOMEM;

	if (irq)
		instance->irq = irq->start;
	else
		instance->irq = NO_IRQ;

	hostdata = shost_priv(instance);
	hostdata->base = pio_mem->start;
	hostdata->io = (void *)pio_mem->start;

	if (pdma_mem && setup_use_pdma)
		hostdata->pdma_io = (void *)pdma_mem->start;
	else
		host_flags |= FLAG_NO_PSEUDO_DMA;

	host_flags |= setup_toshiba_delay > 0 ? FLAG_TOSHIBA_DELAY : 0;

	error = NCR5380_init(instance, host_flags | FLAG_LATE_DMA_SETUP);
	if (error)
		goto fail_init;

	if (instance->irq != NO_IRQ) {
		error = request_irq(instance->irq, macscsi_intr, IRQF_SHARED,
		                    "NCR5380", instance);
		if (error)
			goto fail_irq;
	}

	NCR5380_maybe_reset_bus(instance);

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
fail_init:
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
	},
};

module_platform_driver_probe(mac_scsi_driver, mac_scsi_probe);

MODULE_ALIAS("platform:" DRV_MODULE_NAME);
MODULE_LICENSE("GPL");
