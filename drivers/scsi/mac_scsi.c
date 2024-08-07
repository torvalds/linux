// SPDX-License-Identifier: GPL-2.0-only
/*
 * Generic Macintosh NCR5380 driver
 *
 * Copyright 1998, Michael Schmitz <mschmitz@lbl.gov>
 *
 * Copyright 2019 Finn Thain
 *
 * derived in part from:
 */
/*
 * Generic Generic NCR5380 driver
 *
 * Copyright 1995, Russell King
 */

#include <linux/delay.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>

#include <asm/hwtest.h>
#include <asm/io.h>
#include <asm/macintosh.h>
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
#define NCR5380_host_reset              macscsi_host_reset
#define NCR5380_info                    macscsi_info

#include "NCR5380.h"

static int setup_can_queue = -1;
module_param(setup_can_queue, int, 0);
static int setup_cmd_per_lun = -1;
module_param(setup_cmd_per_lun, int, 0);
static int setup_sg_tablesize = -1;
module_param(setup_sg_tablesize, int, 0);
static int setup_use_pdma = 512;
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

/*
 * According to "Inside Macintosh: Devices", Mac OS requires disk drivers to
 * specify the number of bytes between the delays expected from a SCSI target.
 * This allows the operating system to "prevent bus errors when a target fails
 * to deliver the next byte within the processor bus error timeout period."
 * Linux SCSI drivers lack knowledge of the timing behaviour of SCSI targets
 * so bus errors are unavoidable.
 *
 * If a MOVE.B instruction faults during a receive operation, we assume the
 * target sent nothing and try again. That assumption probably depends on
 * target firmware but it seems to hold up okay. If a fault happens during a
 * send operation, the target may or may not have seen /ACK and got the byte.
 * It's uncertain so the whole SCSI command gets retried.
 *
 * The NOP is needed for synchronization because the fault address in the
 * exception stack frame may or may not be the instruction that actually
 * caused the bus error. Post-increment addressing can't be used.
 */

#define MOVE_BYTE(operands) \
	asm volatile ( \
		"1:     moveb " operands "     \n" \
		"11:    nop                    \n" \
		"       addq #1,%0             \n" \
		"       subq #1,%1             \n" \
		"40:                           \n" \
		"                              \n" \
		".section .fixup,\"ax\"        \n" \
		".even                         \n" \
		"90:    movel #1, %2           \n" \
		"       jra 40b                \n" \
		".previous                     \n" \
		"                              \n" \
		".section __ex_table,\"a\"     \n" \
		".align  4                     \n" \
		".long   1b,90b                \n" \
		".long  11b,90b                \n" \
		".previous                     \n" \
		: "+a" (addr), "+r" (n), "+r" (result) : "a" (io))

/*
 * If a MOVE.W (or MOVE.L) instruction faults, it cannot be retried because
 * the residual byte count would be uncertain. In that situation the MOVE_WORD
 * macro clears n in the fixup section to abort the transfer.
 */

#define MOVE_WORD(operands) \
	asm volatile ( \
		"1:     movew " operands "     \n" \
		"11:    nop                    \n" \
		"       subq #2,%1             \n" \
		"40:                           \n" \
		"                              \n" \
		".section .fixup,\"ax\"        \n" \
		".even                         \n" \
		"90:    movel #0, %1           \n" \
		"       movel #2, %2           \n" \
		"       jra 40b                \n" \
		".previous                     \n" \
		"                              \n" \
		".section __ex_table,\"a\"     \n" \
		".align  4                     \n" \
		".long   1b,90b                \n" \
		".long  11b,90b                \n" \
		".previous                     \n" \
		: "+a" (addr), "+r" (n), "+r" (result) : "a" (io))

#define MOVE_16_WORDS(operands) \
	asm volatile ( \
		"1:     movew " operands "     \n" \
		"2:     movew " operands "     \n" \
		"3:     movew " operands "     \n" \
		"4:     movew " operands "     \n" \
		"5:     movew " operands "     \n" \
		"6:     movew " operands "     \n" \
		"7:     movew " operands "     \n" \
		"8:     movew " operands "     \n" \
		"9:     movew " operands "     \n" \
		"10:    movew " operands "     \n" \
		"11:    movew " operands "     \n" \
		"12:    movew " operands "     \n" \
		"13:    movew " operands "     \n" \
		"14:    movew " operands "     \n" \
		"15:    movew " operands "     \n" \
		"16:    movew " operands "     \n" \
		"17:    nop                    \n" \
		"       subl  #32,%1           \n" \
		"40:                           \n" \
		"                              \n" \
		".section .fixup,\"ax\"        \n" \
		".even                         \n" \
		"90:    movel #0, %1           \n" \
		"       movel #2, %2           \n" \
		"       jra 40b                \n" \
		".previous                     \n" \
		"                              \n" \
		".section __ex_table,\"a\"     \n" \
		".align  4                     \n" \
		".long   1b,90b                \n" \
		".long   2b,90b                \n" \
		".long   3b,90b                \n" \
		".long   4b,90b                \n" \
		".long   5b,90b                \n" \
		".long   6b,90b                \n" \
		".long   7b,90b                \n" \
		".long   8b,90b                \n" \
		".long   9b,90b                \n" \
		".long  10b,90b                \n" \
		".long  11b,90b                \n" \
		".long  12b,90b                \n" \
		".long  13b,90b                \n" \
		".long  14b,90b                \n" \
		".long  15b,90b                \n" \
		".long  16b,90b                \n" \
		".long  17b,90b                \n" \
		".previous                     \n" \
		: "+a" (addr), "+r" (n), "+r" (result) : "a" (io))

static inline int mac_pdma_recv(void __iomem *io, unsigned char *start, int n)
{
	unsigned char *addr = start;
	int result = 0;

	if (n >= 1) {
		MOVE_BYTE("%3@,%0@");
		if (result)
			goto out;
	}
	if (n >= 1 && ((unsigned long)addr & 1)) {
		MOVE_BYTE("%3@,%0@");
		if (result)
			goto out;
	}
	while (n >= 32)
		MOVE_16_WORDS("%3@,%0@+");
	while (n >= 2)
		MOVE_WORD("%3@,%0@+");
	if (result)
		return start - addr; /* Negated to indicate uncertain length */
	if (n == 1)
		MOVE_BYTE("%3@,%0@");
out:
	return addr - start;
}

static inline int mac_pdma_send(unsigned char *start, void __iomem *io, int n)
{
	unsigned char *addr = start;
	int result = 0;

	if (n >= 1) {
		MOVE_BYTE("%0@,%3@");
		if (result)
			return -1;
	}
	if (n >= 1 && ((unsigned long)addr & 1)) {
		MOVE_BYTE("%0@,%3@");
		if (result)
			return -2;
	}
	while (n >= 32)
		MOVE_16_WORDS("%0@+,%3@");
	while (n >= 2)
		MOVE_WORD("%0@+,%3@");
	if (result)
		return start - addr - 1; /* Negated to indicate uncertain length */
	if (n == 1)
		MOVE_BYTE("%0@,%3@");
	return addr - start;
}

/* The "SCSI DMA" chip on the IIfx implements this register. */
#define CTRL_REG                0x8
#define CTRL_INTERRUPTS_ENABLE  BIT(1)
#define CTRL_HANDSHAKE_MODE     BIT(3)

static inline void write_ctrl_reg(struct NCR5380_hostdata *hostdata, u32 value)
{
	out_be32(hostdata->io + (CTRL_REG << 4), value);
}

static inline int macscsi_wait_for_drq(struct NCR5380_hostdata *hostdata)
{
	unsigned int n = 1; /* effectively multiplies NCR5380_REG_POLL_TIME */
	unsigned char basr;

again:
	basr = NCR5380_read(BUS_AND_STATUS_REG);

	if (!(basr & BASR_PHASE_MATCH))
		return 1;

	if (basr & BASR_IRQ)
		return -1;

	if (basr & BASR_DRQ)
		return 0;

	if (n-- == 0) {
		NCR5380_dprint(NDEBUG_PSEUDO_DMA, hostdata->host);
		dsprintk(NDEBUG_PSEUDO_DMA, hostdata->host,
			 "%s: DRQ timeout\n", __func__);
		return -1;
	}

	NCR5380_poll_politely2(hostdata,
			       BUS_AND_STATUS_REG, BASR_DRQ, BASR_DRQ,
			       BUS_AND_STATUS_REG, BASR_PHASE_MATCH, 0, 0);
	goto again;
}

static inline int macscsi_pread(struct NCR5380_hostdata *hostdata,
                                unsigned char *dst, int len)
{
	u8 __iomem *s = hostdata->pdma_io + (INPUT_DATA_REG << 4);
	unsigned char *d = dst;

	hostdata->pdma_residual = len;

	while (macscsi_wait_for_drq(hostdata) == 0) {
		int bytes, chunk_bytes;

		if (macintosh_config->ident == MAC_MODEL_IIFX)
			write_ctrl_reg(hostdata, CTRL_HANDSHAKE_MODE |
			                         CTRL_INTERRUPTS_ENABLE);

		chunk_bytes = min(hostdata->pdma_residual, 512);
		bytes = mac_pdma_recv(s, d, chunk_bytes);

		if (macintosh_config->ident == MAC_MODEL_IIFX)
			write_ctrl_reg(hostdata, CTRL_INTERRUPTS_ENABLE);

		if (bytes > 0) {
			d += bytes;
			hostdata->pdma_residual -= bytes;
		}

		if (hostdata->pdma_residual == 0)
			break;

		if (bytes > 0)
			continue;

		NCR5380_dprint(NDEBUG_PSEUDO_DMA, hostdata->host);
		dsprintk(NDEBUG_PSEUDO_DMA, hostdata->host,
			 "%s: bus error [%d/%d] (%d/%d)\n",
			 __func__, d - dst, len, bytes, chunk_bytes);

		if (bytes == 0)
			continue;

		if (macscsi_wait_for_drq(hostdata) <= 0)
			set_host_byte(hostdata->connected, DID_ERROR);
		break;
	}

	return 0;
}

static inline int macscsi_pwrite(struct NCR5380_hostdata *hostdata,
                                 unsigned char *src, int len)
{
	unsigned char *s = src;
	u8 __iomem *d = hostdata->pdma_io + (OUTPUT_DATA_REG << 4);

	hostdata->pdma_residual = len;

	while (macscsi_wait_for_drq(hostdata) == 0) {
		int bytes, chunk_bytes;

		if (macintosh_config->ident == MAC_MODEL_IIFX)
			write_ctrl_reg(hostdata, CTRL_HANDSHAKE_MODE |
			                         CTRL_INTERRUPTS_ENABLE);

		chunk_bytes = min(hostdata->pdma_residual, 512);
		bytes = mac_pdma_send(s, d, chunk_bytes);

		if (macintosh_config->ident == MAC_MODEL_IIFX)
			write_ctrl_reg(hostdata, CTRL_INTERRUPTS_ENABLE);

		if (bytes > 0) {
			s += bytes;
			hostdata->pdma_residual -= bytes;
		}

		if (hostdata->pdma_residual == 0)
			break;

		if (bytes > 0)
			continue;

		NCR5380_dprint(NDEBUG_PSEUDO_DMA, hostdata->host);
		dsprintk(NDEBUG_PSEUDO_DMA, hostdata->host,
			 "%s: bus error [%d/%d] (%d/%d)\n",
			 __func__, s - src, len, bytes, chunk_bytes);

		if (bytes == 0)
			continue;

		if (macscsi_wait_for_drq(hostdata) <= 0)
			set_host_byte(hostdata->connected, DID_ERROR);
		break;
	}

	return 0;
}

static int macscsi_dma_xfer_len(struct NCR5380_hostdata *hostdata,
                                struct scsi_cmnd *cmd)
{
	int resid = NCR5380_to_ncmd(cmd)->this_residual;

	if (hostdata->flags & FLAG_NO_PSEUDO_DMA || resid < setup_use_pdma)
		return 0;

	return resid;
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
	.eh_host_reset_handler	= macscsi_host_reset,
	.can_queue		= 16,
	.this_id		= 7,
	.sg_tablesize		= 1,
	.cmd_per_lun		= 2,
	.dma_boundary		= PAGE_SIZE - 1,
	.cmd_size		= sizeof(struct NCR5380_cmd),
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
	if (setup_sg_tablesize > 0)
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
	hostdata->io = (u8 __iomem *)pio_mem->start;

	if (pdma_mem && setup_use_pdma)
		hostdata->pdma_io = (u8 __iomem *)pdma_mem->start;
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

static void __exit mac_scsi_remove(struct platform_device *pdev)
{
	struct Scsi_Host *instance = platform_get_drvdata(pdev);

	scsi_remove_host(instance);
	if (instance->irq != NO_IRQ)
		free_irq(instance->irq, instance);
	NCR5380_exit(instance);
	scsi_host_put(instance);
}

/*
 * mac_scsi_remove() lives in .exit.text. For drivers registered via
 * module_platform_driver_probe() this is ok because they cannot get unbound at
 * runtime. So mark the driver struct with __refdata to prevent modpost
 * triggering a section mismatch warning.
 */
static struct platform_driver mac_scsi_driver __refdata = {
	.remove_new = __exit_p(mac_scsi_remove),
	.driver = {
		.name	= DRV_MODULE_NAME,
	},
};

module_platform_driver_probe(mac_scsi_driver, mac_scsi_probe);

MODULE_ALIAS("platform:" DRV_MODULE_NAME);
MODULE_DESCRIPTION("Macintosh NCR5380 SCSI driver");
MODULE_LICENSE("GPL");
