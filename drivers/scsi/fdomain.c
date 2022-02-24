// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for Future Domain TMC-16x0 and TMC-3260 SCSI host adapters
 * Copyright 2019 Ondrej Zary
 *
 * Original driver by
 * Rickard E. Faith, faith@cs.unc.edu
 *
 * Future Domain BIOS versions supported for autodetect:
 *    2.0, 3.0, 3.2, 3.4 (1.0), 3.5 (2.0), 3.6, 3.61
 * Chips supported:
 *    TMC-1800, TMC-18C50, TMC-18C30, TMC-36C70
 * Boards supported:
 *    Future Domain TMC-1650, TMC-1660, TMC-1670, TMC-1680, TMC-1610M/MER/MEX
 *    Future Domain TMC-3260 (PCI)
 *    Quantum ISA-200S, ISA-250MG
 *    Adaptec AHA-2920A (PCI) [BUT *NOT* AHA-2920C -- use aic7xxx instead]
 *    IBM ?
 *
 * NOTE:
 *
 * The Adaptec AHA-2920C has an Adaptec AIC-7850 chip on it.
 * Use the aic7xxx driver for this board.
 *
 * The Adaptec AHA-2920A has a Future Domain chip on it, so this is the right
 * driver for that card.  Unfortunately, the boxes will probably just say
 * "2920", so you'll have to look on the card for a Future Domain logo, or a
 * letter after the 2920.
 *
 * If you have a TMC-8xx or TMC-9xx board, then this is not the driver for
 * your board.
 *
 * DESCRIPTION:
 *
 * This is the Linux low-level SCSI driver for Future Domain TMC-1660/1680
 * TMC-1650/1670, and TMC-3260 SCSI host adapters.  The 1650 and 1670 have a
 * 25-pin external connector, whereas the 1660 and 1680 have a SCSI-2 50-pin
 * high-density external connector.  The 1670 and 1680 have floppy disk
 * controllers built in.  The TMC-3260 is a PCI bus card.
 *
 * Future Domain's older boards are based on the TMC-1800 chip, and this
 * driver was originally written for a TMC-1680 board with the TMC-1800 chip.
 * More recently, boards are being produced with the TMC-18C50 and TMC-18C30
 * chips.
 *
 * Please note that the drive ordering that Future Domain implemented in BIOS
 * versions 3.4 and 3.5 is the opposite of the order (currently) used by the
 * rest of the SCSI industry.
 *
 *
 * REFERENCES USED:
 *
 * "TMC-1800 SCSI Chip Specification (FDC-1800T)", Future Domain Corporation,
 * 1990.
 *
 * "Technical Reference Manual: 18C50 SCSI Host Adapter Chip", Future Domain
 * Corporation, January 1992.
 *
 * "LXT SCSI Products: Specifications and OEM Technical Manual (Revision
 * B/September 1991)", Maxtor Corporation, 1991.
 *
 * "7213S product Manual (Revision P3)", Maxtor Corporation, 1992.
 *
 * "Draft Proposed American National Standard: Small Computer System
 * Interface - 2 (SCSI-2)", Global Engineering Documents. (X3T9.2/86-109,
 * revision 10h, October 17, 1991)
 *
 * Private communications, Drew Eckhardt (drew@cs.colorado.edu) and Eric
 * Youngdale (ericy@cais.com), 1992.
 *
 * Private communication, Tuong Le (Future Domain Engineering department),
 * 1994. (Disk geometry computations for Future Domain BIOS version 3.4, and
 * TMC-18C30 detection.)
 *
 * Hogan, Thom. The Programmer's PC Sourcebook. Microsoft Press, 1988. Page
 * 60 (2.39: Disk Partition Table Layout).
 *
 * "18C30 Technical Reference Manual", Future Domain Corporation, 1993, page
 * 6-1.
 */

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/workqueue.h>
#include <scsi/scsicam.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>
#include "fdomain.h"

/*
 * FIFO_COUNT: The host adapter has an 8K cache (host adapters based on the
 * 18C30 chip have a 2k cache).  When this many 512 byte blocks are filled by
 * the SCSI device, an interrupt will be raised.  Therefore, this could be as
 * low as 0, or as high as 16.  Note, however, that values which are too high
 * or too low seem to prevent any interrupts from occurring, and thereby lock
 * up the machine.
 */
#define FIFO_COUNT	2	/* Number of 512 byte blocks before INTR */
#define PARITY_MASK	ACTL_PAREN	/* Parity enabled, 0 = disabled */

enum chip_type {
	unknown		= 0x00,
	tmc1800		= 0x01,
	tmc18c50	= 0x02,
	tmc18c30	= 0x03,
};

struct fdomain {
	int base;
	struct scsi_cmnd *cur_cmd;
	enum chip_type chip;
	struct work_struct work;
};

static inline void fdomain_make_bus_idle(struct fdomain *fd)
{
	outb(0, fd->base + REG_BCTL);
	outb(0, fd->base + REG_MCTL);
	if (fd->chip == tmc18c50 || fd->chip == tmc18c30)
		/* Clear forced intr. */
		outb(ACTL_RESET | ACTL_CLRFIRQ | PARITY_MASK,
		     fd->base + REG_ACTL);
	else
		outb(ACTL_RESET | PARITY_MASK, fd->base + REG_ACTL);
}

static enum chip_type fdomain_identify(int port)
{
	u16 id = inb(port + REG_ID_LSB) | inb(port + REG_ID_MSB) << 8;

	switch (id) {
	case 0x6127:
		return tmc1800;
	case 0x60e9: /* 18c50 or 18c30 */
		break;
	default:
		return unknown;
	}

	/* Try to toggle 32-bit mode. This only works on an 18c30 chip. */
	outb(CFG2_32BIT, port + REG_CFG2);
	if ((inb(port + REG_CFG2) & CFG2_32BIT)) {
		outb(0, port + REG_CFG2);
		if ((inb(port + REG_CFG2) & CFG2_32BIT) == 0)
			return tmc18c30;
	}
	/* If that failed, we are an 18c50. */
	return tmc18c50;
}

static int fdomain_test_loopback(int base)
{
	int i;

	for (i = 0; i < 255; i++) {
		outb(i, base + REG_LOOPBACK);
		if (inb(base + REG_LOOPBACK) != i)
			return 1;
	}

	return 0;
}

static void fdomain_reset(int base)
{
	outb(BCTL_RST, base + REG_BCTL);
	mdelay(20);
	outb(0, base + REG_BCTL);
	mdelay(1150);
	outb(0, base + REG_MCTL);
	outb(PARITY_MASK, base + REG_ACTL);
}

static int fdomain_select(struct Scsi_Host *sh, int target)
{
	int status;
	unsigned long timeout;
	struct fdomain *fd = shost_priv(sh);

	outb(BCTL_BUSEN | BCTL_SEL, fd->base + REG_BCTL);
	outb(BIT(sh->this_id) | BIT(target), fd->base + REG_SCSI_DATA_NOACK);

	/* Stop arbitration and enable parity */
	outb(PARITY_MASK, fd->base + REG_ACTL);

	timeout = 350;	/* 350 msec */

	do {
		status = inb(fd->base + REG_BSTAT);
		if (status & BSTAT_BSY) {
			/* Enable SCSI Bus */
			/* (on error, should make bus idle with 0) */
			outb(BCTL_BUSEN, fd->base + REG_BCTL);
			return 0;
		}
		mdelay(1);
	} while (--timeout);
	fdomain_make_bus_idle(fd);
	return 1;
}

static void fdomain_finish_cmd(struct fdomain *fd)
{
	outb(0, fd->base + REG_ICTL);
	fdomain_make_bus_idle(fd);
	scsi_done(fd->cur_cmd);
	fd->cur_cmd = NULL;
}

static void fdomain_read_data(struct scsi_cmnd *cmd)
{
	struct fdomain *fd = shost_priv(cmd->device->host);
	unsigned char *virt, *ptr;
	size_t offset, len;

	while ((len = inw(fd->base + REG_FIFO_COUNT)) > 0) {
		offset = scsi_bufflen(cmd) - scsi_get_resid(cmd);
		virt = scsi_kmap_atomic_sg(scsi_sglist(cmd), scsi_sg_count(cmd),
					   &offset, &len);
		ptr = virt + offset;
		if (len & 1)
			*ptr++ = inb(fd->base + REG_FIFO);
		if (len > 1)
			insw(fd->base + REG_FIFO, ptr, len >> 1);
		scsi_set_resid(cmd, scsi_get_resid(cmd) - len);
		scsi_kunmap_atomic_sg(virt);
	}
}

static void fdomain_write_data(struct scsi_cmnd *cmd)
{
	struct fdomain *fd = shost_priv(cmd->device->host);
	/* 8k FIFO for pre-tmc18c30 chips, 2k FIFO for tmc18c30 */
	int FIFO_Size = fd->chip == tmc18c30 ? 0x800 : 0x2000;
	unsigned char *virt, *ptr;
	size_t offset, len;

	while ((len = FIFO_Size - inw(fd->base + REG_FIFO_COUNT)) > 512) {
		offset = scsi_bufflen(cmd) - scsi_get_resid(cmd);
		if (len + offset > scsi_bufflen(cmd)) {
			len = scsi_bufflen(cmd) - offset;
			if (len == 0)
				break;
		}
		virt = scsi_kmap_atomic_sg(scsi_sglist(cmd), scsi_sg_count(cmd),
					   &offset, &len);
		ptr = virt + offset;
		if (len & 1)
			outb(*ptr++, fd->base + REG_FIFO);
		if (len > 1)
			outsw(fd->base + REG_FIFO, ptr, len >> 1);
		scsi_set_resid(cmd, scsi_get_resid(cmd) - len);
		scsi_kunmap_atomic_sg(virt);
	}
}

static void fdomain_work(struct work_struct *work)
{
	struct fdomain *fd = container_of(work, struct fdomain, work);
	struct Scsi_Host *sh = container_of((void *)fd, struct Scsi_Host,
					    hostdata);
	struct scsi_cmnd *cmd = fd->cur_cmd;
	unsigned long flags;
	int status;
	int done = 0;

	spin_lock_irqsave(sh->host_lock, flags);

	if (cmd->SCp.phase & in_arbitration) {
		status = inb(fd->base + REG_ASTAT);
		if (!(status & ASTAT_ARB)) {
			set_host_byte(cmd, DID_BUS_BUSY);
			fdomain_finish_cmd(fd);
			goto out;
		}
		cmd->SCp.phase = in_selection;

		outb(ICTL_SEL | FIFO_COUNT, fd->base + REG_ICTL);
		outb(BCTL_BUSEN | BCTL_SEL, fd->base + REG_BCTL);
		outb(BIT(cmd->device->host->this_id) | BIT(scmd_id(cmd)),
		     fd->base + REG_SCSI_DATA_NOACK);
		/* Stop arbitration and enable parity */
		outb(ACTL_IRQEN | PARITY_MASK, fd->base + REG_ACTL);
		goto out;
	} else if (cmd->SCp.phase & in_selection) {
		status = inb(fd->base + REG_BSTAT);
		if (!(status & BSTAT_BSY)) {
			/* Try again, for slow devices */
			if (fdomain_select(cmd->device->host, scmd_id(cmd))) {
				set_host_byte(cmd, DID_NO_CONNECT);
				fdomain_finish_cmd(fd);
				goto out;
			}
			/* Stop arbitration and enable parity */
			outb(ACTL_IRQEN | PARITY_MASK, fd->base + REG_ACTL);
		}
		cmd->SCp.phase = in_other;
		outb(ICTL_FIFO | ICTL_REQ | FIFO_COUNT, fd->base + REG_ICTL);
		outb(BCTL_BUSEN, fd->base + REG_BCTL);
		goto out;
	}

	/* cur_cmd->SCp.phase == in_other: this is the body of the routine */
	status = inb(fd->base + REG_BSTAT);

	if (status & BSTAT_REQ) {
		switch (status & (BSTAT_MSG | BSTAT_CMD | BSTAT_IO)) {
		case BSTAT_CMD:	/* COMMAND OUT */
			outb(cmd->cmnd[cmd->SCp.sent_command++],
			     fd->base + REG_SCSI_DATA);
			break;
		case 0:	/* DATA OUT -- tmc18c50/tmc18c30 only */
			if (fd->chip != tmc1800 && !cmd->SCp.have_data_in) {
				cmd->SCp.have_data_in = -1;
				outb(ACTL_IRQEN | ACTL_FIFOWR | ACTL_FIFOEN |
				     PARITY_MASK, fd->base + REG_ACTL);
			}
			break;
		case BSTAT_IO:	/* DATA IN -- tmc18c50/tmc18c30 only */
			if (fd->chip != tmc1800 && !cmd->SCp.have_data_in) {
				cmd->SCp.have_data_in = 1;
				outb(ACTL_IRQEN | ACTL_FIFOEN | PARITY_MASK,
				     fd->base + REG_ACTL);
			}
			break;
		case BSTAT_CMD | BSTAT_IO:	/* STATUS IN */
			cmd->SCp.Status = inb(fd->base + REG_SCSI_DATA);
			break;
		case BSTAT_MSG | BSTAT_CMD:	/* MESSAGE OUT */
			outb(MESSAGE_REJECT, fd->base + REG_SCSI_DATA);
			break;
		case BSTAT_MSG | BSTAT_CMD | BSTAT_IO:	/* MESSAGE IN */
			cmd->SCp.Message = inb(fd->base + REG_SCSI_DATA);
			if (cmd->SCp.Message == COMMAND_COMPLETE)
				++done;
			break;
		}
	}

	if (fd->chip == tmc1800 && !cmd->SCp.have_data_in &&
	    cmd->SCp.sent_command >= cmd->cmd_len) {
		if (cmd->sc_data_direction == DMA_TO_DEVICE) {
			cmd->SCp.have_data_in = -1;
			outb(ACTL_IRQEN | ACTL_FIFOWR | ACTL_FIFOEN |
			     PARITY_MASK, fd->base + REG_ACTL);
		} else {
			cmd->SCp.have_data_in = 1;
			outb(ACTL_IRQEN | ACTL_FIFOEN | PARITY_MASK,
			     fd->base + REG_ACTL);
		}
	}

	if (cmd->SCp.have_data_in == -1) /* DATA OUT */
		fdomain_write_data(cmd);

	if (cmd->SCp.have_data_in == 1) /* DATA IN */
		fdomain_read_data(cmd);

	if (done) {
		set_status_byte(cmd, cmd->SCp.Status);
		set_host_byte(cmd, DID_OK);
		scsi_msg_to_host_byte(cmd, cmd->SCp.Message);
		fdomain_finish_cmd(fd);
	} else {
		if (cmd->SCp.phase & disconnect) {
			outb(ICTL_FIFO | ICTL_SEL | ICTL_REQ | FIFO_COUNT,
			     fd->base + REG_ICTL);
			outb(0, fd->base + REG_BCTL);
		} else
			outb(ICTL_FIFO | ICTL_REQ | FIFO_COUNT,
			     fd->base + REG_ICTL);
	}
out:
	spin_unlock_irqrestore(sh->host_lock, flags);
}

static irqreturn_t fdomain_irq(int irq, void *dev_id)
{
	struct fdomain *fd = dev_id;

	/* Is it our IRQ? */
	if ((inb(fd->base + REG_ASTAT) & ASTAT_IRQ) == 0)
		return IRQ_NONE;

	outb(0, fd->base + REG_ICTL);

	/* We usually have one spurious interrupt after each command. */
	if (!fd->cur_cmd)	/* Spurious interrupt */
		return IRQ_NONE;

	schedule_work(&fd->work);

	return IRQ_HANDLED;
}

static int fdomain_queue(struct Scsi_Host *sh, struct scsi_cmnd *cmd)
{
	struct fdomain *fd = shost_priv(cmd->device->host);
	unsigned long flags;

	cmd->SCp.Status		= 0;
	cmd->SCp.Message	= 0;
	cmd->SCp.have_data_in	= 0;
	cmd->SCp.sent_command	= 0;
	cmd->SCp.phase		= in_arbitration;
	scsi_set_resid(cmd, scsi_bufflen(cmd));

	spin_lock_irqsave(sh->host_lock, flags);

	fd->cur_cmd = cmd;

	fdomain_make_bus_idle(fd);

	/* Start arbitration */
	outb(0, fd->base + REG_ICTL);
	outb(0, fd->base + REG_BCTL);	/* Disable data drivers */
	/* Set our id bit */
	outb(BIT(cmd->device->host->this_id), fd->base + REG_SCSI_DATA_NOACK);
	outb(ICTL_ARB, fd->base + REG_ICTL);
	/* Start arbitration */
	outb(ACTL_ARB | ACTL_IRQEN | PARITY_MASK, fd->base + REG_ACTL);

	spin_unlock_irqrestore(sh->host_lock, flags);

	return 0;
}

static int fdomain_abort(struct scsi_cmnd *cmd)
{
	struct Scsi_Host *sh = cmd->device->host;
	struct fdomain *fd = shost_priv(sh);
	unsigned long flags;

	if (!fd->cur_cmd)
		return FAILED;

	spin_lock_irqsave(sh->host_lock, flags);

	fdomain_make_bus_idle(fd);
	fd->cur_cmd->SCp.phase |= aborted;

	/* Aborts are not done well. . . */
	set_host_byte(fd->cur_cmd, DID_ABORT);
	fdomain_finish_cmd(fd);
	spin_unlock_irqrestore(sh->host_lock, flags);
	return SUCCESS;
}

static int fdomain_host_reset(struct scsi_cmnd *cmd)
{
	struct Scsi_Host *sh = cmd->device->host;
	struct fdomain *fd = shost_priv(sh);
	unsigned long flags;

	spin_lock_irqsave(sh->host_lock, flags);
	fdomain_reset(fd->base);
	spin_unlock_irqrestore(sh->host_lock, flags);
	return SUCCESS;
}

static int fdomain_biosparam(struct scsi_device *sdev,
			     struct block_device *bdev,	sector_t capacity,
			     int geom[])
{
	unsigned char *p = scsi_bios_ptable(bdev);

	if (p && p[65] == 0xaa && p[64] == 0x55 /* Partition table valid */
	    && p[4]) {	 /* Partition type */
		geom[0] = p[5] + 1;	/* heads */
		geom[1] = p[6] & 0x3f;	/* sectors */
	} else {
		if (capacity >= 0x7e0000) {
			geom[0] = 255;	/* heads */
			geom[1] = 63;	/* sectors */
		} else if (capacity >= 0x200000) {
			geom[0] = 128;	/* heads */
			geom[1] = 63;	/* sectors */
		} else {
			geom[0] = 64;	/* heads */
			geom[1] = 32;	/* sectors */
		}
	}
	geom[2] = sector_div(capacity, geom[0] * geom[1]);
	kfree(p);

	return 0;
}

static struct scsi_host_template fdomain_template = {
	.module			= THIS_MODULE,
	.name			= "Future Domain TMC-16x0",
	.proc_name		= "fdomain",
	.queuecommand		= fdomain_queue,
	.eh_abort_handler	= fdomain_abort,
	.eh_host_reset_handler	= fdomain_host_reset,
	.bios_param		= fdomain_biosparam,
	.can_queue		= 1,
	.this_id		= 7,
	.sg_tablesize		= 64,
	.dma_boundary		= PAGE_SIZE - 1,
};

struct Scsi_Host *fdomain_create(int base, int irq, int this_id,
				 struct device *dev)
{
	struct Scsi_Host *sh;
	struct fdomain *fd;
	enum chip_type chip;
	static const char * const chip_names[] = {
		"Unknown", "TMC-1800", "TMC-18C50", "TMC-18C30"
	};
	unsigned long irq_flags = 0;

	chip = fdomain_identify(base);
	if (!chip)
		return NULL;

	fdomain_reset(base);

	if (fdomain_test_loopback(base))
		return NULL;

	if (!irq) {
		dev_err(dev, "card has no IRQ assigned");
		return NULL;
	}

	sh = scsi_host_alloc(&fdomain_template, sizeof(struct fdomain));
	if (!sh)
		return NULL;

	if (this_id)
		sh->this_id = this_id & 0x07;

	sh->irq = irq;
	sh->io_port = base;
	sh->n_io_port = FDOMAIN_REGION_SIZE;

	fd = shost_priv(sh);
	fd->base = base;
	fd->chip = chip;
	INIT_WORK(&fd->work, fdomain_work);

	if (dev_is_pci(dev) || !strcmp(dev->bus->name, "pcmcia"))
		irq_flags = IRQF_SHARED;

	if (request_irq(irq, fdomain_irq, irq_flags, "fdomain", fd))
		goto fail_put;

	shost_printk(KERN_INFO, sh, "%s chip at 0x%x irq %d SCSI ID %d\n",
		     dev_is_pci(dev) ? "TMC-36C70 (PCI bus)" : chip_names[chip],
		     base, irq, sh->this_id);

	if (scsi_add_host(sh, dev))
		goto fail_free_irq;

	scsi_scan_host(sh);

	return sh;

fail_free_irq:
	free_irq(irq, fd);
fail_put:
	scsi_host_put(sh);
	return NULL;
}
EXPORT_SYMBOL_GPL(fdomain_create);

int fdomain_destroy(struct Scsi_Host *sh)
{
	struct fdomain *fd = shost_priv(sh);

	cancel_work_sync(&fd->work);
	scsi_remove_host(sh);
	if (sh->irq)
		free_irq(sh->irq, fd);
	scsi_host_put(sh);
	return 0;
}
EXPORT_SYMBOL_GPL(fdomain_destroy);

#ifdef CONFIG_PM_SLEEP
static int fdomain_resume(struct device *dev)
{
	struct fdomain *fd = shost_priv(dev_get_drvdata(dev));

	fdomain_reset(fd->base);
	return 0;
}

static SIMPLE_DEV_PM_OPS(fdomain_pm_ops, NULL, fdomain_resume);
#endif /* CONFIG_PM_SLEEP */

MODULE_AUTHOR("Ondrej Zary, Rickard E. Faith");
MODULE_DESCRIPTION("Future Domain TMC-16x0/TMC-3260 SCSI driver");
MODULE_LICENSE("GPL");
