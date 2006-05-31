/*
 *  libata-bmdma.c - helper library for PCI IDE BMDMA
 *
 *  Maintained by:  Jeff Garzik <jgarzik@pobox.com>
 *    		    Please ALWAYS copy linux-ide@vger.kernel.org
 *		    on emails.
 *
 *  Copyright 2003-2006 Red Hat, Inc.  All rights reserved.
 *  Copyright 2003-2006 Jeff Garzik
 *
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *
 *  libata documentation is available via 'make {ps|pdf}docs',
 *  as Documentation/DocBook/libata.*
 *
 *  Hardware documentation available from http://www.t13.org/ and
 *  http://www.sata-io.org/
 *
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/libata.h>

#include "libata.h"

/**
 *	ata_tf_load_pio - send taskfile registers to host controller
 *	@ap: Port to which output is sent
 *	@tf: ATA taskfile register set
 *
 *	Outputs ATA taskfile to standard ATA host controller.
 *
 *	LOCKING:
 *	Inherited from caller.
 */

static void ata_tf_load_pio(struct ata_port *ap, const struct ata_taskfile *tf)
{
	struct ata_ioports *ioaddr = &ap->ioaddr;
	unsigned int is_addr = tf->flags & ATA_TFLAG_ISADDR;

	if (tf->ctl != ap->last_ctl) {
		outb(tf->ctl, ioaddr->ctl_addr);
		ap->last_ctl = tf->ctl;
		ata_wait_idle(ap);
	}

	if (is_addr && (tf->flags & ATA_TFLAG_LBA48)) {
		outb(tf->hob_feature, ioaddr->feature_addr);
		outb(tf->hob_nsect, ioaddr->nsect_addr);
		outb(tf->hob_lbal, ioaddr->lbal_addr);
		outb(tf->hob_lbam, ioaddr->lbam_addr);
		outb(tf->hob_lbah, ioaddr->lbah_addr);
		VPRINTK("hob: feat 0x%X nsect 0x%X, lba 0x%X 0x%X 0x%X\n",
			tf->hob_feature,
			tf->hob_nsect,
			tf->hob_lbal,
			tf->hob_lbam,
			tf->hob_lbah);
	}

	if (is_addr) {
		outb(tf->feature, ioaddr->feature_addr);
		outb(tf->nsect, ioaddr->nsect_addr);
		outb(tf->lbal, ioaddr->lbal_addr);
		outb(tf->lbam, ioaddr->lbam_addr);
		outb(tf->lbah, ioaddr->lbah_addr);
		VPRINTK("feat 0x%X nsect 0x%X lba 0x%X 0x%X 0x%X\n",
			tf->feature,
			tf->nsect,
			tf->lbal,
			tf->lbam,
			tf->lbah);
	}

	if (tf->flags & ATA_TFLAG_DEVICE) {
		outb(tf->device, ioaddr->device_addr);
		VPRINTK("device 0x%X\n", tf->device);
	}

	ata_wait_idle(ap);
}

/**
 *	ata_tf_load_mmio - send taskfile registers to host controller
 *	@ap: Port to which output is sent
 *	@tf: ATA taskfile register set
 *
 *	Outputs ATA taskfile to standard ATA host controller using MMIO.
 *
 *	LOCKING:
 *	Inherited from caller.
 */

static void ata_tf_load_mmio(struct ata_port *ap, const struct ata_taskfile *tf)
{
	struct ata_ioports *ioaddr = &ap->ioaddr;
	unsigned int is_addr = tf->flags & ATA_TFLAG_ISADDR;

	if (tf->ctl != ap->last_ctl) {
		writeb(tf->ctl, (void __iomem *) ap->ioaddr.ctl_addr);
		ap->last_ctl = tf->ctl;
		ata_wait_idle(ap);
	}

	if (is_addr && (tf->flags & ATA_TFLAG_LBA48)) {
		writeb(tf->hob_feature, (void __iomem *) ioaddr->feature_addr);
		writeb(tf->hob_nsect, (void __iomem *) ioaddr->nsect_addr);
		writeb(tf->hob_lbal, (void __iomem *) ioaddr->lbal_addr);
		writeb(tf->hob_lbam, (void __iomem *) ioaddr->lbam_addr);
		writeb(tf->hob_lbah, (void __iomem *) ioaddr->lbah_addr);
		VPRINTK("hob: feat 0x%X nsect 0x%X, lba 0x%X 0x%X 0x%X\n",
			tf->hob_feature,
			tf->hob_nsect,
			tf->hob_lbal,
			tf->hob_lbam,
			tf->hob_lbah);
	}

	if (is_addr) {
		writeb(tf->feature, (void __iomem *) ioaddr->feature_addr);
		writeb(tf->nsect, (void __iomem *) ioaddr->nsect_addr);
		writeb(tf->lbal, (void __iomem *) ioaddr->lbal_addr);
		writeb(tf->lbam, (void __iomem *) ioaddr->lbam_addr);
		writeb(tf->lbah, (void __iomem *) ioaddr->lbah_addr);
		VPRINTK("feat 0x%X nsect 0x%X lba 0x%X 0x%X 0x%X\n",
			tf->feature,
			tf->nsect,
			tf->lbal,
			tf->lbam,
			tf->lbah);
	}

	if (tf->flags & ATA_TFLAG_DEVICE) {
		writeb(tf->device, (void __iomem *) ioaddr->device_addr);
		VPRINTK("device 0x%X\n", tf->device);
	}

	ata_wait_idle(ap);
}


/**
 *	ata_tf_load - send taskfile registers to host controller
 *	@ap: Port to which output is sent
 *	@tf: ATA taskfile register set
 *
 *	Outputs ATA taskfile to standard ATA host controller using MMIO
 *	or PIO as indicated by the ATA_FLAG_MMIO flag.
 *	Writes the control, feature, nsect, lbal, lbam, and lbah registers.
 *	Optionally (ATA_TFLAG_LBA48) writes hob_feature, hob_nsect,
 *	hob_lbal, hob_lbam, and hob_lbah.
 *
 *	This function waits for idle (!BUSY and !DRQ) after writing
 *	registers.  If the control register has a new value, this
 *	function also waits for idle after writing control and before
 *	writing the remaining registers.
 *
 *	May be used as the tf_load() entry in ata_port_operations.
 *
 *	LOCKING:
 *	Inherited from caller.
 */
void ata_tf_load(struct ata_port *ap, const struct ata_taskfile *tf)
{
	if (ap->flags & ATA_FLAG_MMIO)
		ata_tf_load_mmio(ap, tf);
	else
		ata_tf_load_pio(ap, tf);
}

/**
 *	ata_exec_command_pio - issue ATA command to host controller
 *	@ap: port to which command is being issued
 *	@tf: ATA taskfile register set
 *
 *	Issues PIO write to ATA command register, with proper
 *	synchronization with interrupt handler / other threads.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 */

static void ata_exec_command_pio(struct ata_port *ap, const struct ata_taskfile *tf)
{
	DPRINTK("ata%u: cmd 0x%X\n", ap->id, tf->command);

       	outb(tf->command, ap->ioaddr.command_addr);
	ata_pause(ap);
}


/**
 *	ata_exec_command_mmio - issue ATA command to host controller
 *	@ap: port to which command is being issued
 *	@tf: ATA taskfile register set
 *
 *	Issues MMIO write to ATA command register, with proper
 *	synchronization with interrupt handler / other threads.
 *
 *	FIXME: missing write posting for 400nS delay enforcement
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 */

static void ata_exec_command_mmio(struct ata_port *ap, const struct ata_taskfile *tf)
{
	DPRINTK("ata%u: cmd 0x%X\n", ap->id, tf->command);

       	writeb(tf->command, (void __iomem *) ap->ioaddr.command_addr);
	ata_pause(ap);
}


/**
 *	ata_exec_command - issue ATA command to host controller
 *	@ap: port to which command is being issued
 *	@tf: ATA taskfile register set
 *
 *	Issues PIO/MMIO write to ATA command register, with proper
 *	synchronization with interrupt handler / other threads.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 */
void ata_exec_command(struct ata_port *ap, const struct ata_taskfile *tf)
{
	if (ap->flags & ATA_FLAG_MMIO)
		ata_exec_command_mmio(ap, tf);
	else
		ata_exec_command_pio(ap, tf);
}

/**
 *	ata_tf_read_pio - input device's ATA taskfile shadow registers
 *	@ap: Port from which input is read
 *	@tf: ATA taskfile register set for storing input
 *
 *	Reads ATA taskfile registers for currently-selected device
 *	into @tf.
 *
 *	LOCKING:
 *	Inherited from caller.
 */

static void ata_tf_read_pio(struct ata_port *ap, struct ata_taskfile *tf)
{
	struct ata_ioports *ioaddr = &ap->ioaddr;

	tf->command = ata_check_status(ap);
	tf->feature = inb(ioaddr->error_addr);
	tf->nsect = inb(ioaddr->nsect_addr);
	tf->lbal = inb(ioaddr->lbal_addr);
	tf->lbam = inb(ioaddr->lbam_addr);
	tf->lbah = inb(ioaddr->lbah_addr);
	tf->device = inb(ioaddr->device_addr);

	if (tf->flags & ATA_TFLAG_LBA48) {
		outb(tf->ctl | ATA_HOB, ioaddr->ctl_addr);
		tf->hob_feature = inb(ioaddr->error_addr);
		tf->hob_nsect = inb(ioaddr->nsect_addr);
		tf->hob_lbal = inb(ioaddr->lbal_addr);
		tf->hob_lbam = inb(ioaddr->lbam_addr);
		tf->hob_lbah = inb(ioaddr->lbah_addr);
	}
}

/**
 *	ata_tf_read_mmio - input device's ATA taskfile shadow registers
 *	@ap: Port from which input is read
 *	@tf: ATA taskfile register set for storing input
 *
 *	Reads ATA taskfile registers for currently-selected device
 *	into @tf via MMIO.
 *
 *	LOCKING:
 *	Inherited from caller.
 */

static void ata_tf_read_mmio(struct ata_port *ap, struct ata_taskfile *tf)
{
	struct ata_ioports *ioaddr = &ap->ioaddr;

	tf->command = ata_check_status(ap);
	tf->feature = readb((void __iomem *)ioaddr->error_addr);
	tf->nsect = readb((void __iomem *)ioaddr->nsect_addr);
	tf->lbal = readb((void __iomem *)ioaddr->lbal_addr);
	tf->lbam = readb((void __iomem *)ioaddr->lbam_addr);
	tf->lbah = readb((void __iomem *)ioaddr->lbah_addr);
	tf->device = readb((void __iomem *)ioaddr->device_addr);

	if (tf->flags & ATA_TFLAG_LBA48) {
		writeb(tf->ctl | ATA_HOB, (void __iomem *) ap->ioaddr.ctl_addr);
		tf->hob_feature = readb((void __iomem *)ioaddr->error_addr);
		tf->hob_nsect = readb((void __iomem *)ioaddr->nsect_addr);
		tf->hob_lbal = readb((void __iomem *)ioaddr->lbal_addr);
		tf->hob_lbam = readb((void __iomem *)ioaddr->lbam_addr);
		tf->hob_lbah = readb((void __iomem *)ioaddr->lbah_addr);
	}
}


/**
 *	ata_tf_read - input device's ATA taskfile shadow registers
 *	@ap: Port from which input is read
 *	@tf: ATA taskfile register set for storing input
 *
 *	Reads ATA taskfile registers for currently-selected device
 *	into @tf.
 *
 *	Reads nsect, lbal, lbam, lbah, and device.  If ATA_TFLAG_LBA48
 *	is set, also reads the hob registers.
 *
 *	May be used as the tf_read() entry in ata_port_operations.
 *
 *	LOCKING:
 *	Inherited from caller.
 */
void ata_tf_read(struct ata_port *ap, struct ata_taskfile *tf)
{
	if (ap->flags & ATA_FLAG_MMIO)
		ata_tf_read_mmio(ap, tf);
	else
		ata_tf_read_pio(ap, tf);
}

/**
 *	ata_check_status_pio - Read device status reg & clear interrupt
 *	@ap: port where the device is
 *
 *	Reads ATA taskfile status register for currently-selected device
 *	and return its value. This also clears pending interrupts
 *      from this device
 *
 *	LOCKING:
 *	Inherited from caller.
 */
static u8 ata_check_status_pio(struct ata_port *ap)
{
	return inb(ap->ioaddr.status_addr);
}

/**
 *	ata_check_status_mmio - Read device status reg & clear interrupt
 *	@ap: port where the device is
 *
 *	Reads ATA taskfile status register for currently-selected device
 *	via MMIO and return its value. This also clears pending interrupts
 *      from this device
 *
 *	LOCKING:
 *	Inherited from caller.
 */
static u8 ata_check_status_mmio(struct ata_port *ap)
{
       	return readb((void __iomem *) ap->ioaddr.status_addr);
}


/**
 *	ata_check_status - Read device status reg & clear interrupt
 *	@ap: port where the device is
 *
 *	Reads ATA taskfile status register for currently-selected device
 *	and return its value. This also clears pending interrupts
 *      from this device
 *
 *	May be used as the check_status() entry in ata_port_operations.
 *
 *	LOCKING:
 *	Inherited from caller.
 */
u8 ata_check_status(struct ata_port *ap)
{
	if (ap->flags & ATA_FLAG_MMIO)
		return ata_check_status_mmio(ap);
	return ata_check_status_pio(ap);
}


/**
 *	ata_altstatus - Read device alternate status reg
 *	@ap: port where the device is
 *
 *	Reads ATA taskfile alternate status register for
 *	currently-selected device and return its value.
 *
 *	Note: may NOT be used as the check_altstatus() entry in
 *	ata_port_operations.
 *
 *	LOCKING:
 *	Inherited from caller.
 */
u8 ata_altstatus(struct ata_port *ap)
{
	if (ap->ops->check_altstatus)
		return ap->ops->check_altstatus(ap);

	if (ap->flags & ATA_FLAG_MMIO)
		return readb((void __iomem *)ap->ioaddr.altstatus_addr);
	return inb(ap->ioaddr.altstatus_addr);
}

/**
 *	ata_bmdma_setup_mmio - Set up PCI IDE BMDMA transaction
 *	@qc: Info associated with this ATA transaction.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 */

static void ata_bmdma_setup_mmio (struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	unsigned int rw = (qc->tf.flags & ATA_TFLAG_WRITE);
	u8 dmactl;
	void __iomem *mmio = (void __iomem *) ap->ioaddr.bmdma_addr;

	/* load PRD table addr. */
	mb();	/* make sure PRD table writes are visible to controller */
	writel(ap->prd_dma, mmio + ATA_DMA_TABLE_OFS);

	/* specify data direction, triple-check start bit is clear */
	dmactl = readb(mmio + ATA_DMA_CMD);
	dmactl &= ~(ATA_DMA_WR | ATA_DMA_START);
	if (!rw)
		dmactl |= ATA_DMA_WR;
	writeb(dmactl, mmio + ATA_DMA_CMD);

	/* issue r/w command */
	ap->ops->exec_command(ap, &qc->tf);
}

/**
 *	ata_bmdma_start_mmio - Start a PCI IDE BMDMA transaction
 *	@qc: Info associated with this ATA transaction.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 */

static void ata_bmdma_start_mmio (struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	void __iomem *mmio = (void __iomem *) ap->ioaddr.bmdma_addr;
	u8 dmactl;

	/* start host DMA transaction */
	dmactl = readb(mmio + ATA_DMA_CMD);
	writeb(dmactl | ATA_DMA_START, mmio + ATA_DMA_CMD);

	/* Strictly, one may wish to issue a readb() here, to
	 * flush the mmio write.  However, control also passes
	 * to the hardware at this point, and it will interrupt
	 * us when we are to resume control.  So, in effect,
	 * we don't care when the mmio write flushes.
	 * Further, a read of the DMA status register _immediately_
	 * following the write may not be what certain flaky hardware
	 * is expected, so I think it is best to not add a readb()
	 * without first all the MMIO ATA cards/mobos.
	 * Or maybe I'm just being paranoid.
	 */
}

/**
 *	ata_bmdma_setup_pio - Set up PCI IDE BMDMA transaction (PIO)
 *	@qc: Info associated with this ATA transaction.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 */

static void ata_bmdma_setup_pio (struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	unsigned int rw = (qc->tf.flags & ATA_TFLAG_WRITE);
	u8 dmactl;

	/* load PRD table addr. */
	outl(ap->prd_dma, ap->ioaddr.bmdma_addr + ATA_DMA_TABLE_OFS);

	/* specify data direction, triple-check start bit is clear */
	dmactl = inb(ap->ioaddr.bmdma_addr + ATA_DMA_CMD);
	dmactl &= ~(ATA_DMA_WR | ATA_DMA_START);
	if (!rw)
		dmactl |= ATA_DMA_WR;
	outb(dmactl, ap->ioaddr.bmdma_addr + ATA_DMA_CMD);

	/* issue r/w command */
	ap->ops->exec_command(ap, &qc->tf);
}

/**
 *	ata_bmdma_start_pio - Start a PCI IDE BMDMA transaction (PIO)
 *	@qc: Info associated with this ATA transaction.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 */

static void ata_bmdma_start_pio (struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	u8 dmactl;

	/* start host DMA transaction */
	dmactl = inb(ap->ioaddr.bmdma_addr + ATA_DMA_CMD);
	outb(dmactl | ATA_DMA_START,
	     ap->ioaddr.bmdma_addr + ATA_DMA_CMD);
}


/**
 *	ata_bmdma_start - Start a PCI IDE BMDMA transaction
 *	@qc: Info associated with this ATA transaction.
 *
 *	Writes the ATA_DMA_START flag to the DMA command register.
 *
 *	May be used as the bmdma_start() entry in ata_port_operations.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 */
void ata_bmdma_start(struct ata_queued_cmd *qc)
{
	if (qc->ap->flags & ATA_FLAG_MMIO)
		ata_bmdma_start_mmio(qc);
	else
		ata_bmdma_start_pio(qc);
}


/**
 *	ata_bmdma_setup - Set up PCI IDE BMDMA transaction
 *	@qc: Info associated with this ATA transaction.
 *
 *	Writes address of PRD table to device's PRD Table Address
 *	register, sets the DMA control register, and calls
 *	ops->exec_command() to start the transfer.
 *
 *	May be used as the bmdma_setup() entry in ata_port_operations.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 */
void ata_bmdma_setup(struct ata_queued_cmd *qc)
{
	if (qc->ap->flags & ATA_FLAG_MMIO)
		ata_bmdma_setup_mmio(qc);
	else
		ata_bmdma_setup_pio(qc);
}


/**
 *	ata_bmdma_irq_clear - Clear PCI IDE BMDMA interrupt.
 *	@ap: Port associated with this ATA transaction.
 *
 *	Clear interrupt and error flags in DMA status register.
 *
 *	May be used as the irq_clear() entry in ata_port_operations.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 */

void ata_bmdma_irq_clear(struct ata_port *ap)
{
	if (!ap->ioaddr.bmdma_addr)
		return;

	if (ap->flags & ATA_FLAG_MMIO) {
		void __iomem *mmio =
		      ((void __iomem *) ap->ioaddr.bmdma_addr) + ATA_DMA_STATUS;
		writeb(readb(mmio), mmio);
	} else {
		unsigned long addr = ap->ioaddr.bmdma_addr + ATA_DMA_STATUS;
		outb(inb(addr), addr);
	}
}


/**
 *	ata_bmdma_status - Read PCI IDE BMDMA status
 *	@ap: Port associated with this ATA transaction.
 *
 *	Read and return BMDMA status register.
 *
 *	May be used as the bmdma_status() entry in ata_port_operations.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 */

u8 ata_bmdma_status(struct ata_port *ap)
{
	u8 host_stat;
	if (ap->flags & ATA_FLAG_MMIO) {
		void __iomem *mmio = (void __iomem *) ap->ioaddr.bmdma_addr;
		host_stat = readb(mmio + ATA_DMA_STATUS);
	} else
		host_stat = inb(ap->ioaddr.bmdma_addr + ATA_DMA_STATUS);
	return host_stat;
}


/**
 *	ata_bmdma_stop - Stop PCI IDE BMDMA transfer
 *	@qc: Command we are ending DMA for
 *
 *	Clears the ATA_DMA_START flag in the dma control register
 *
 *	May be used as the bmdma_stop() entry in ata_port_operations.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host_set lock)
 */

void ata_bmdma_stop(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	if (ap->flags & ATA_FLAG_MMIO) {
		void __iomem *mmio = (void __iomem *) ap->ioaddr.bmdma_addr;

		/* clear start/stop bit */
		writeb(readb(mmio + ATA_DMA_CMD) & ~ATA_DMA_START,
			mmio + ATA_DMA_CMD);
	} else {
		/* clear start/stop bit */
		outb(inb(ap->ioaddr.bmdma_addr + ATA_DMA_CMD) & ~ATA_DMA_START,
			ap->ioaddr.bmdma_addr + ATA_DMA_CMD);
	}

	/* one-PIO-cycle guaranteed wait, per spec, for HDMA1:0 transition */
	ata_altstatus(ap);        /* dummy read */
}

/**
 *	ata_bmdma_freeze - Freeze BMDMA controller port
 *	@ap: port to freeze
 *
 *	Freeze BMDMA controller port.
 *
 *	LOCKING:
 *	Inherited from caller.
 */
void ata_bmdma_freeze(struct ata_port *ap)
{
	struct ata_ioports *ioaddr = &ap->ioaddr;

	ap->ctl |= ATA_NIEN;
	ap->last_ctl = ap->ctl;

	if (ap->flags & ATA_FLAG_MMIO)
		writeb(ap->ctl, (void __iomem *)ioaddr->ctl_addr);
	else
		outb(ap->ctl, ioaddr->ctl_addr);
}

/**
 *	ata_bmdma_thaw - Thaw BMDMA controller port
 *	@ap: port to thaw
 *
 *	Thaw BMDMA controller port.
 *
 *	LOCKING:
 *	Inherited from caller.
 */
void ata_bmdma_thaw(struct ata_port *ap)
{
	/* clear & re-enable interrupts */
	ata_chk_status(ap);
	ap->ops->irq_clear(ap);
	if (ap->ioaddr.ctl_addr)	/* FIXME: hack. create a hook instead */
		ata_irq_on(ap);
}

/**
 *	ata_bmdma_drive_eh - Perform EH with given methods for BMDMA controller
 *	@ap: port to handle error for
 *	@prereset: prereset method (can be NULL)
 *	@softreset: softreset method (can be NULL)
 *	@hardreset: hardreset method (can be NULL)
 *	@postreset: postreset method (can be NULL)
 *
 *	Handle error for ATA BMDMA controller.  It can handle both
 *	PATA and SATA controllers.  Many controllers should be able to
 *	use this EH as-is or with some added handling before and
 *	after.
 *
 *	This function is intended to be used for constructing
 *	->error_handler callback by low level drivers.
 *
 *	LOCKING:
 *	Kernel thread context (may sleep)
 */
void ata_bmdma_drive_eh(struct ata_port *ap, ata_prereset_fn_t prereset,
			ata_reset_fn_t softreset, ata_reset_fn_t hardreset,
			ata_postreset_fn_t postreset)
{
	struct ata_host_set *host_set = ap->host_set;
	struct ata_eh_context *ehc = &ap->eh_context;
	struct ata_queued_cmd *qc;
	unsigned long flags;
	int thaw = 0;

	qc = __ata_qc_from_tag(ap, ap->active_tag);
	if (qc && !(qc->flags & ATA_QCFLAG_FAILED))
		qc = NULL;

	/* reset PIO HSM and stop DMA engine */
	spin_lock_irqsave(&host_set->lock, flags);

	ap->hsm_task_state = HSM_ST_IDLE;

	if (qc && (qc->tf.protocol == ATA_PROT_DMA ||
		   qc->tf.protocol == ATA_PROT_ATAPI_DMA)) {
		u8 host_stat;

		host_stat = ata_bmdma_status(ap);

		ata_ehi_push_desc(&ehc->i, "BMDMA stat 0x%x", host_stat);

		/* BMDMA controllers indicate host bus error by
		 * setting DMA_ERR bit and timing out.  As it wasn't
		 * really a timeout event, adjust error mask and
		 * cancel frozen state.
		 */
		if (qc->err_mask == AC_ERR_TIMEOUT && host_stat & ATA_DMA_ERR) {
			qc->err_mask = AC_ERR_HOST_BUS;
			thaw = 1;
		}

		ap->ops->bmdma_stop(qc);
	}

	ata_altstatus(ap);
	ata_chk_status(ap);
	ap->ops->irq_clear(ap);

	spin_unlock_irqrestore(&host_set->lock, flags);

	if (thaw)
		ata_eh_thaw_port(ap);

	/* PIO and DMA engines have been stopped, perform recovery */
	ata_do_eh(ap, prereset, softreset, hardreset, postreset);
}

/**
 *	ata_bmdma_error_handler - Stock error handler for BMDMA controller
 *	@ap: port to handle error for
 *
 *	Stock error handler for BMDMA controller.
 *
 *	LOCKING:
 *	Kernel thread context (may sleep)
 */
void ata_bmdma_error_handler(struct ata_port *ap)
{
	ata_reset_fn_t hardreset;

	hardreset = NULL;
	if (sata_scr_valid(ap))
		hardreset = sata_std_hardreset;

	ata_bmdma_drive_eh(ap, ata_std_prereset, ata_std_softreset, hardreset,
			   ata_std_postreset);
}

/**
 *	ata_bmdma_post_internal_cmd - Stock post_internal_cmd for
 *				      BMDMA controller
 *	@qc: internal command to clean up
 *
 *	LOCKING:
 *	Kernel thread context (may sleep)
 */
void ata_bmdma_post_internal_cmd(struct ata_queued_cmd *qc)
{
	ata_bmdma_stop(qc);
}

#ifdef CONFIG_PCI
static struct ata_probe_ent *
ata_probe_ent_alloc(struct device *dev, const struct ata_port_info *port)
{
	struct ata_probe_ent *probe_ent;

	probe_ent = kzalloc(sizeof(*probe_ent), GFP_KERNEL);
	if (!probe_ent) {
		printk(KERN_ERR DRV_NAME "(%s): out of memory\n",
		       kobject_name(&(dev->kobj)));
		return NULL;
	}

	INIT_LIST_HEAD(&probe_ent->node);
	probe_ent->dev = dev;

	probe_ent->sht = port->sht;
	probe_ent->host_flags = port->host_flags;
	probe_ent->pio_mask = port->pio_mask;
	probe_ent->mwdma_mask = port->mwdma_mask;
	probe_ent->udma_mask = port->udma_mask;
	probe_ent->port_ops = port->port_ops;

	return probe_ent;
}


/**
 *	ata_pci_init_native_mode - Initialize native-mode driver
 *	@pdev:  pci device to be initialized
 *	@port:  array[2] of pointers to port info structures.
 *	@ports: bitmap of ports present
 *
 *	Utility function which allocates and initializes an
 *	ata_probe_ent structure for a standard dual-port
 *	PIO-based IDE controller.  The returned ata_probe_ent
 *	structure can be passed to ata_device_add().  The returned
 *	ata_probe_ent structure should then be freed with kfree().
 *
 *	The caller need only pass the address of the primary port, the
 *	secondary will be deduced automatically. If the device has non
 *	standard secondary port mappings this function can be called twice,
 *	once for each interface.
 */

struct ata_probe_ent *
ata_pci_init_native_mode(struct pci_dev *pdev, struct ata_port_info **port, int ports)
{
	struct ata_probe_ent *probe_ent =
		ata_probe_ent_alloc(pci_dev_to_dev(pdev), port[0]);
	int p = 0;
	unsigned long bmdma;

	if (!probe_ent)
		return NULL;

	probe_ent->irq = pdev->irq;
	probe_ent->irq_flags = SA_SHIRQ;
	probe_ent->private_data = port[0]->private_data;

	if (ports & ATA_PORT_PRIMARY) {
		probe_ent->port[p].cmd_addr = pci_resource_start(pdev, 0);
		probe_ent->port[p].altstatus_addr =
		probe_ent->port[p].ctl_addr =
			pci_resource_start(pdev, 1) | ATA_PCI_CTL_OFS;
		bmdma = pci_resource_start(pdev, 4);
		if (bmdma) {
			if (inb(bmdma + 2) & 0x80)
				probe_ent->host_set_flags |= ATA_HOST_SIMPLEX;
			probe_ent->port[p].bmdma_addr = bmdma;
		}
		ata_std_ports(&probe_ent->port[p]);
		p++;
	}

	if (ports & ATA_PORT_SECONDARY) {
		probe_ent->port[p].cmd_addr = pci_resource_start(pdev, 2);
		probe_ent->port[p].altstatus_addr =
		probe_ent->port[p].ctl_addr =
			pci_resource_start(pdev, 3) | ATA_PCI_CTL_OFS;
		bmdma = pci_resource_start(pdev, 4);
		if (bmdma) {
			bmdma += 8;
			if(inb(bmdma + 2) & 0x80)
			probe_ent->host_set_flags |= ATA_HOST_SIMPLEX;
			probe_ent->port[p].bmdma_addr = bmdma;
		}
		ata_std_ports(&probe_ent->port[p]);
		p++;
	}

	probe_ent->n_ports = p;
	return probe_ent;
}


static struct ata_probe_ent *ata_pci_init_legacy_port(struct pci_dev *pdev,
				struct ata_port_info *port, int port_num)
{
	struct ata_probe_ent *probe_ent;
	unsigned long bmdma;

	probe_ent = ata_probe_ent_alloc(pci_dev_to_dev(pdev), port);
	if (!probe_ent)
		return NULL;

	probe_ent->legacy_mode = 1;
	probe_ent->n_ports = 1;
	probe_ent->hard_port_no = port_num;
	probe_ent->private_data = port->private_data;

	switch(port_num)
	{
		case 0:
			probe_ent->irq = 14;
			probe_ent->port[0].cmd_addr = 0x1f0;
			probe_ent->port[0].altstatus_addr =
			probe_ent->port[0].ctl_addr = 0x3f6;
			break;
		case 1:
			probe_ent->irq = 15;
			probe_ent->port[0].cmd_addr = 0x170;
			probe_ent->port[0].altstatus_addr =
			probe_ent->port[0].ctl_addr = 0x376;
			break;
	}

	bmdma = pci_resource_start(pdev, 4);
	if (bmdma != 0) {
		bmdma += 8 * port_num;
		probe_ent->port[0].bmdma_addr = bmdma;
		if (inb(bmdma + 2) & 0x80)
			probe_ent->host_set_flags |= ATA_HOST_SIMPLEX;
	}
	ata_std_ports(&probe_ent->port[0]);

	return probe_ent;
}


/**
 *	ata_pci_init_one - Initialize/register PCI IDE host controller
 *	@pdev: Controller to be initialized
 *	@port_info: Information from low-level host driver
 *	@n_ports: Number of ports attached to host controller
 *
 *	This is a helper function which can be called from a driver's
 *	xxx_init_one() probe function if the hardware uses traditional
 *	IDE taskfile registers.
 *
 *	This function calls pci_enable_device(), reserves its register
 *	regions, sets the dma mask, enables bus master mode, and calls
 *	ata_device_add()
 *
 *	LOCKING:
 *	Inherited from PCI layer (may sleep).
 *
 *	RETURNS:
 *	Zero on success, negative on errno-based value on error.
 */

int ata_pci_init_one (struct pci_dev *pdev, struct ata_port_info **port_info,
		      unsigned int n_ports)
{
	struct ata_probe_ent *probe_ent = NULL, *probe_ent2 = NULL;
	struct ata_port_info *port[2];
	u8 tmp8, mask;
	unsigned int legacy_mode = 0;
	int disable_dev_on_err = 1;
	int rc;

	DPRINTK("ENTER\n");

	port[0] = port_info[0];
	if (n_ports > 1)
		port[1] = port_info[1];
	else
		port[1] = port[0];

	if ((port[0]->host_flags & ATA_FLAG_NO_LEGACY) == 0
	    && (pdev->class >> 8) == PCI_CLASS_STORAGE_IDE) {
		/* TODO: What if one channel is in native mode ... */
		pci_read_config_byte(pdev, PCI_CLASS_PROG, &tmp8);
		mask = (1 << 2) | (1 << 0);
		if ((tmp8 & mask) != mask)
			legacy_mode = (1 << 3);
	}

	/* FIXME... */
	if ((!legacy_mode) && (n_ports > 2)) {
		printk(KERN_ERR "ata: BUG: native mode, n_ports > 2\n");
		n_ports = 2;
		/* For now */
	}

	/* FIXME: Really for ATA it isn't safe because the device may be
	   multi-purpose and we want to leave it alone if it was already
	   enabled. Secondly for shared use as Arjan says we want refcounting

	   Checking dev->is_enabled is insufficient as this is not set at
	   boot for the primary video which is BIOS enabled
         */

	rc = pci_enable_device(pdev);
	if (rc)
		return rc;

	rc = pci_request_regions(pdev, DRV_NAME);
	if (rc) {
		disable_dev_on_err = 0;
		goto err_out;
	}

	/* FIXME: Should use platform specific mappers for legacy port ranges */
	if (legacy_mode) {
		if (!request_region(0x1f0, 8, "libata")) {
			struct resource *conflict, res;
			res.start = 0x1f0;
			res.end = 0x1f0 + 8 - 1;
			conflict = ____request_resource(&ioport_resource, &res);
			if (!strcmp(conflict->name, "libata"))
				legacy_mode |= (1 << 0);
			else {
				disable_dev_on_err = 0;
				printk(KERN_WARNING "ata: 0x1f0 IDE port busy\n");
			}
		} else
			legacy_mode |= (1 << 0);

		if (!request_region(0x170, 8, "libata")) {
			struct resource *conflict, res;
			res.start = 0x170;
			res.end = 0x170 + 8 - 1;
			conflict = ____request_resource(&ioport_resource, &res);
			if (!strcmp(conflict->name, "libata"))
				legacy_mode |= (1 << 1);
			else {
				disable_dev_on_err = 0;
				printk(KERN_WARNING "ata: 0x170 IDE port busy\n");
			}
		} else
			legacy_mode |= (1 << 1);
	}

	/* we have legacy mode, but all ports are unavailable */
	if (legacy_mode == (1 << 3)) {
		rc = -EBUSY;
		goto err_out_regions;
	}

	/* FIXME: If we get no DMA mask we should fall back to PIO */
	rc = pci_set_dma_mask(pdev, ATA_DMA_MASK);
	if (rc)
		goto err_out_regions;
	rc = pci_set_consistent_dma_mask(pdev, ATA_DMA_MASK);
	if (rc)
		goto err_out_regions;

	if (legacy_mode) {
		if (legacy_mode & (1 << 0))
			probe_ent = ata_pci_init_legacy_port(pdev, port[0], 0);
		if (legacy_mode & (1 << 1))
			probe_ent2 = ata_pci_init_legacy_port(pdev, port[1], 1);
	} else {
		if (n_ports == 2)
			probe_ent = ata_pci_init_native_mode(pdev, port, ATA_PORT_PRIMARY | ATA_PORT_SECONDARY);
		else
			probe_ent = ata_pci_init_native_mode(pdev, port, ATA_PORT_PRIMARY);
	}
	if (!probe_ent && !probe_ent2) {
		rc = -ENOMEM;
		goto err_out_regions;
	}

	pci_set_master(pdev);

	/* FIXME: check ata_device_add return */
	if (legacy_mode) {
		if (legacy_mode & (1 << 0))
			ata_device_add(probe_ent);
		if (legacy_mode & (1 << 1))
			ata_device_add(probe_ent2);
	} else
		ata_device_add(probe_ent);

	kfree(probe_ent);
	kfree(probe_ent2);

	return 0;

err_out_regions:
	if (legacy_mode & (1 << 0))
		release_region(0x1f0, 8);
	if (legacy_mode & (1 << 1))
		release_region(0x170, 8);
	pci_release_regions(pdev);
err_out:
	if (disable_dev_on_err)
		pci_disable_device(pdev);
	return rc;
}

/**
 *	ata_pci_clear_simplex	-	attempt to kick device out of simplex
 *	@pdev: PCI device
 *
 *	Some PCI ATA devices report simplex mode but in fact can be told to
 *	enter non simplex mode. This implements the neccessary logic to
 *	perform the task on such devices. Calling it on other devices will
 *	have -undefined- behaviour.
 */

int ata_pci_clear_simplex(struct pci_dev *pdev)
{
	unsigned long bmdma = pci_resource_start(pdev, 4);
	u8 simplex;

	if (bmdma == 0)
		return -ENOENT;

	simplex = inb(bmdma + 0x02);
	outb(simplex & 0x60, bmdma + 0x02);
	simplex = inb(bmdma + 0x02);
	if (simplex & 0x80)
		return -EOPNOTSUPP;
	return 0;
}

unsigned long ata_pci_default_filter(const struct ata_port *ap, struct ata_device *adev, unsigned long xfer_mask)
{
	/* Filter out DMA modes if the device has been configured by
	   the BIOS as PIO only */

	if (ap->ioaddr.bmdma_addr == 0)
		xfer_mask &= ~(ATA_MASK_MWDMA | ATA_MASK_UDMA);
	return xfer_mask;
}

#endif /* CONFIG_PCI */

