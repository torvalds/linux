/*
 *  libata-sff.c - helper library for PCI IDE BMDMA
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

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/libata.h>
#include <linux/highmem.h>

#include "libata.h"

const struct ata_port_operations ata_sff_port_ops = {
	.inherits		= &ata_base_port_ops,

	.qc_prep		= ata_sff_qc_prep,
	.qc_issue		= ata_sff_qc_issue,
	.qc_fill_rtf		= ata_sff_qc_fill_rtf,

	.freeze			= ata_sff_freeze,
	.thaw			= ata_sff_thaw,
	.prereset		= ata_sff_prereset,
	.softreset		= ata_sff_softreset,
	.hardreset		= sata_sff_hardreset,
	.postreset		= ata_sff_postreset,
	.drain_fifo		= ata_sff_drain_fifo,
	.error_handler		= ata_sff_error_handler,
	.post_internal_cmd	= ata_sff_post_internal_cmd,

	.sff_dev_select		= ata_sff_dev_select,
	.sff_check_status	= ata_sff_check_status,
	.sff_tf_load		= ata_sff_tf_load,
	.sff_tf_read		= ata_sff_tf_read,
	.sff_exec_command	= ata_sff_exec_command,
	.sff_data_xfer		= ata_sff_data_xfer,
	.sff_irq_on		= ata_sff_irq_on,
	.sff_irq_clear		= ata_sff_irq_clear,

	.lost_interrupt		= ata_sff_lost_interrupt,

	.port_start		= ata_sff_port_start,
};
EXPORT_SYMBOL_GPL(ata_sff_port_ops);

const struct ata_port_operations ata_bmdma_port_ops = {
	.inherits		= &ata_sff_port_ops,

	.mode_filter		= ata_bmdma_mode_filter,

	.bmdma_setup		= ata_bmdma_setup,
	.bmdma_start		= ata_bmdma_start,
	.bmdma_stop		= ata_bmdma_stop,
	.bmdma_status		= ata_bmdma_status,
};
EXPORT_SYMBOL_GPL(ata_bmdma_port_ops);

const struct ata_port_operations ata_bmdma32_port_ops = {
	.inherits		= &ata_bmdma_port_ops,

	.sff_data_xfer		= ata_sff_data_xfer32,
};
EXPORT_SYMBOL_GPL(ata_bmdma32_port_ops);

/**
 *	ata_fill_sg - Fill PCI IDE PRD table
 *	@qc: Metadata associated with taskfile to be transferred
 *
 *	Fill PCI IDE PRD (scatter-gather) table with segments
 *	associated with the current disk command.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host lock)
 *
 */
static void ata_fill_sg(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	struct scatterlist *sg;
	unsigned int si, pi;

	pi = 0;
	for_each_sg(qc->sg, sg, qc->n_elem, si) {
		u32 addr, offset;
		u32 sg_len, len;

		/* determine if physical DMA addr spans 64K boundary.
		 * Note h/w doesn't support 64-bit, so we unconditionally
		 * truncate dma_addr_t to u32.
		 */
		addr = (u32) sg_dma_address(sg);
		sg_len = sg_dma_len(sg);

		while (sg_len) {
			offset = addr & 0xffff;
			len = sg_len;
			if ((offset + sg_len) > 0x10000)
				len = 0x10000 - offset;

			ap->prd[pi].addr = cpu_to_le32(addr);
			ap->prd[pi].flags_len = cpu_to_le32(len & 0xffff);
			VPRINTK("PRD[%u] = (0x%X, 0x%X)\n", pi, addr, len);

			pi++;
			sg_len -= len;
			addr += len;
		}
	}

	ap->prd[pi - 1].flags_len |= cpu_to_le32(ATA_PRD_EOT);
}

/**
 *	ata_fill_sg_dumb - Fill PCI IDE PRD table
 *	@qc: Metadata associated with taskfile to be transferred
 *
 *	Fill PCI IDE PRD (scatter-gather) table with segments
 *	associated with the current disk command. Perform the fill
 *	so that we avoid writing any length 64K records for
 *	controllers that don't follow the spec.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host lock)
 *
 */
static void ata_fill_sg_dumb(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	struct scatterlist *sg;
	unsigned int si, pi;

	pi = 0;
	for_each_sg(qc->sg, sg, qc->n_elem, si) {
		u32 addr, offset;
		u32 sg_len, len, blen;

		/* determine if physical DMA addr spans 64K boundary.
		 * Note h/w doesn't support 64-bit, so we unconditionally
		 * truncate dma_addr_t to u32.
		 */
		addr = (u32) sg_dma_address(sg);
		sg_len = sg_dma_len(sg);

		while (sg_len) {
			offset = addr & 0xffff;
			len = sg_len;
			if ((offset + sg_len) > 0x10000)
				len = 0x10000 - offset;

			blen = len & 0xffff;
			ap->prd[pi].addr = cpu_to_le32(addr);
			if (blen == 0) {
				/* Some PATA chipsets like the CS5530 can't
				   cope with 0x0000 meaning 64K as the spec
				   says */
				ap->prd[pi].flags_len = cpu_to_le32(0x8000);
				blen = 0x8000;
				ap->prd[++pi].addr = cpu_to_le32(addr + 0x8000);
			}
			ap->prd[pi].flags_len = cpu_to_le32(blen);
			VPRINTK("PRD[%u] = (0x%X, 0x%X)\n", pi, addr, len);

			pi++;
			sg_len -= len;
			addr += len;
		}
	}

	ap->prd[pi - 1].flags_len |= cpu_to_le32(ATA_PRD_EOT);
}

/**
 *	ata_sff_qc_prep - Prepare taskfile for submission
 *	@qc: Metadata associated with taskfile to be prepared
 *
 *	Prepare ATA taskfile for submission.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host lock)
 */
void ata_sff_qc_prep(struct ata_queued_cmd *qc)
{
	if (!(qc->flags & ATA_QCFLAG_DMAMAP))
		return;

	ata_fill_sg(qc);
}
EXPORT_SYMBOL_GPL(ata_sff_qc_prep);

/**
 *	ata_sff_dumb_qc_prep - Prepare taskfile for submission
 *	@qc: Metadata associated with taskfile to be prepared
 *
 *	Prepare ATA taskfile for submission.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host lock)
 */
void ata_sff_dumb_qc_prep(struct ata_queued_cmd *qc)
{
	if (!(qc->flags & ATA_QCFLAG_DMAMAP))
		return;

	ata_fill_sg_dumb(qc);
}
EXPORT_SYMBOL_GPL(ata_sff_dumb_qc_prep);

/**
 *	ata_sff_check_status - Read device status reg & clear interrupt
 *	@ap: port where the device is
 *
 *	Reads ATA taskfile status register for currently-selected device
 *	and return its value. This also clears pending interrupts
 *      from this device
 *
 *	LOCKING:
 *	Inherited from caller.
 */
u8 ata_sff_check_status(struct ata_port *ap)
{
	return ioread8(ap->ioaddr.status_addr);
}
EXPORT_SYMBOL_GPL(ata_sff_check_status);

/**
 *	ata_sff_altstatus - Read device alternate status reg
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
static u8 ata_sff_altstatus(struct ata_port *ap)
{
	if (ap->ops->sff_check_altstatus)
		return ap->ops->sff_check_altstatus(ap);

	return ioread8(ap->ioaddr.altstatus_addr);
}

/**
 *	ata_sff_irq_status - Check if the device is busy
 *	@ap: port where the device is
 *
 *	Determine if the port is currently busy. Uses altstatus
 *	if available in order to avoid clearing shared IRQ status
 *	when finding an IRQ source. Non ctl capable devices don't
 *	share interrupt lines fortunately for us.
 *
 *	LOCKING:
 *	Inherited from caller.
 */
static u8 ata_sff_irq_status(struct ata_port *ap)
{
	u8 status;

	if (ap->ops->sff_check_altstatus || ap->ioaddr.altstatus_addr) {
		status = ata_sff_altstatus(ap);
		/* Not us: We are busy */
		if (status & ATA_BUSY)
			return status;
	}
	/* Clear INTRQ latch */
	status = ap->ops->sff_check_status(ap);
	return status;
}

/**
 *	ata_sff_sync - Flush writes
 *	@ap: Port to wait for.
 *
 *	CAUTION:
 *	If we have an mmio device with no ctl and no altstatus
 *	method this will fail. No such devices are known to exist.
 *
 *	LOCKING:
 *	Inherited from caller.
 */

static void ata_sff_sync(struct ata_port *ap)
{
	if (ap->ops->sff_check_altstatus)
		ap->ops->sff_check_altstatus(ap);
	else if (ap->ioaddr.altstatus_addr)
		ioread8(ap->ioaddr.altstatus_addr);
}

/**
 *	ata_sff_pause		-	Flush writes and wait 400nS
 *	@ap: Port to pause for.
 *
 *	CAUTION:
 *	If we have an mmio device with no ctl and no altstatus
 *	method this will fail. No such devices are known to exist.
 *
 *	LOCKING:
 *	Inherited from caller.
 */

void ata_sff_pause(struct ata_port *ap)
{
	ata_sff_sync(ap);
	ndelay(400);
}
EXPORT_SYMBOL_GPL(ata_sff_pause);

/**
 *	ata_sff_dma_pause	-	Pause before commencing DMA
 *	@ap: Port to pause for.
 *
 *	Perform I/O fencing and ensure sufficient cycle delays occur
 *	for the HDMA1:0 transition
 */

void ata_sff_dma_pause(struct ata_port *ap)
{
	if (ap->ops->sff_check_altstatus || ap->ioaddr.altstatus_addr) {
		/* An altstatus read will cause the needed delay without
		   messing up the IRQ status */
		ata_sff_altstatus(ap);
		return;
	}
	/* There are no DMA controllers without ctl. BUG here to ensure
	   we never violate the HDMA1:0 transition timing and risk
	   corruption. */
	BUG();
}
EXPORT_SYMBOL_GPL(ata_sff_dma_pause);

/**
 *	ata_sff_busy_sleep - sleep until BSY clears, or timeout
 *	@ap: port containing status register to be polled
 *	@tmout_pat: impatience timeout in msecs
 *	@tmout: overall timeout in msecs
 *
 *	Sleep until ATA Status register bit BSY clears,
 *	or a timeout occurs.
 *
 *	LOCKING:
 *	Kernel thread context (may sleep).
 *
 *	RETURNS:
 *	0 on success, -errno otherwise.
 */
int ata_sff_busy_sleep(struct ata_port *ap,
		       unsigned long tmout_pat, unsigned long tmout)
{
	unsigned long timer_start, timeout;
	u8 status;

	status = ata_sff_busy_wait(ap, ATA_BUSY, 300);
	timer_start = jiffies;
	timeout = ata_deadline(timer_start, tmout_pat);
	while (status != 0xff && (status & ATA_BUSY) &&
	       time_before(jiffies, timeout)) {
		msleep(50);
		status = ata_sff_busy_wait(ap, ATA_BUSY, 3);
	}

	if (status != 0xff && (status & ATA_BUSY))
		ata_port_printk(ap, KERN_WARNING,
				"port is slow to respond, please be patient "
				"(Status 0x%x)\n", status);

	timeout = ata_deadline(timer_start, tmout);
	while (status != 0xff && (status & ATA_BUSY) &&
	       time_before(jiffies, timeout)) {
		msleep(50);
		status = ap->ops->sff_check_status(ap);
	}

	if (status == 0xff)
		return -ENODEV;

	if (status & ATA_BUSY) {
		ata_port_printk(ap, KERN_ERR, "port failed to respond "
				"(%lu secs, Status 0x%x)\n",
				DIV_ROUND_UP(tmout, 1000), status);
		return -EBUSY;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(ata_sff_busy_sleep);

static int ata_sff_check_ready(struct ata_link *link)
{
	u8 status = link->ap->ops->sff_check_status(link->ap);

	return ata_check_ready(status);
}

/**
 *	ata_sff_wait_ready - sleep until BSY clears, or timeout
 *	@link: SFF link to wait ready status for
 *	@deadline: deadline jiffies for the operation
 *
 *	Sleep until ATA Status register bit BSY clears, or timeout
 *	occurs.
 *
 *	LOCKING:
 *	Kernel thread context (may sleep).
 *
 *	RETURNS:
 *	0 on success, -errno otherwise.
 */
int ata_sff_wait_ready(struct ata_link *link, unsigned long deadline)
{
	return ata_wait_ready(link, deadline, ata_sff_check_ready);
}
EXPORT_SYMBOL_GPL(ata_sff_wait_ready);

/**
 *	ata_sff_dev_select - Select device 0/1 on ATA bus
 *	@ap: ATA channel to manipulate
 *	@device: ATA device (numbered from zero) to select
 *
 *	Use the method defined in the ATA specification to
 *	make either device 0, or device 1, active on the
 *	ATA channel.  Works with both PIO and MMIO.
 *
 *	May be used as the dev_select() entry in ata_port_operations.
 *
 *	LOCKING:
 *	caller.
 */
void ata_sff_dev_select(struct ata_port *ap, unsigned int device)
{
	u8 tmp;

	if (device == 0)
		tmp = ATA_DEVICE_OBS;
	else
		tmp = ATA_DEVICE_OBS | ATA_DEV1;

	iowrite8(tmp, ap->ioaddr.device_addr);
	ata_sff_pause(ap);	/* needed; also flushes, for mmio */
}
EXPORT_SYMBOL_GPL(ata_sff_dev_select);

/**
 *	ata_dev_select - Select device 0/1 on ATA bus
 *	@ap: ATA channel to manipulate
 *	@device: ATA device (numbered from zero) to select
 *	@wait: non-zero to wait for Status register BSY bit to clear
 *	@can_sleep: non-zero if context allows sleeping
 *
 *	Use the method defined in the ATA specification to
 *	make either device 0, or device 1, active on the
 *	ATA channel.
 *
 *	This is a high-level version of ata_sff_dev_select(), which
 *	additionally provides the services of inserting the proper
 *	pauses and status polling, where needed.
 *
 *	LOCKING:
 *	caller.
 */
void ata_dev_select(struct ata_port *ap, unsigned int device,
			   unsigned int wait, unsigned int can_sleep)
{
	if (ata_msg_probe(ap))
		ata_port_printk(ap, KERN_INFO, "ata_dev_select: ENTER, "
				"device %u, wait %u\n", device, wait);

	if (wait)
		ata_wait_idle(ap);

	ap->ops->sff_dev_select(ap, device);

	if (wait) {
		if (can_sleep && ap->link.device[device].class == ATA_DEV_ATAPI)
			msleep(150);
		ata_wait_idle(ap);
	}
}

/**
 *	ata_sff_irq_on - Enable interrupts on a port.
 *	@ap: Port on which interrupts are enabled.
 *
 *	Enable interrupts on a legacy IDE device using MMIO or PIO,
 *	wait for idle, clear any pending interrupts.
 *
 *	LOCKING:
 *	Inherited from caller.
 */
u8 ata_sff_irq_on(struct ata_port *ap)
{
	struct ata_ioports *ioaddr = &ap->ioaddr;
	u8 tmp;

	ap->ctl &= ~ATA_NIEN;
	ap->last_ctl = ap->ctl;

	if (ioaddr->ctl_addr)
		iowrite8(ap->ctl, ioaddr->ctl_addr);
	tmp = ata_wait_idle(ap);

	ap->ops->sff_irq_clear(ap);

	return tmp;
}
EXPORT_SYMBOL_GPL(ata_sff_irq_on);

/**
 *	ata_sff_irq_clear - Clear PCI IDE BMDMA interrupt.
 *	@ap: Port associated with this ATA transaction.
 *
 *	Clear interrupt and error flags in DMA status register.
 *
 *	May be used as the irq_clear() entry in ata_port_operations.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host lock)
 */
void ata_sff_irq_clear(struct ata_port *ap)
{
	void __iomem *mmio = ap->ioaddr.bmdma_addr;

	if (!mmio)
		return;

	iowrite8(ioread8(mmio + ATA_DMA_STATUS), mmio + ATA_DMA_STATUS);
}
EXPORT_SYMBOL_GPL(ata_sff_irq_clear);

/**
 *	ata_sff_tf_load - send taskfile registers to host controller
 *	@ap: Port to which output is sent
 *	@tf: ATA taskfile register set
 *
 *	Outputs ATA taskfile to standard ATA host controller.
 *
 *	LOCKING:
 *	Inherited from caller.
 */
void ata_sff_tf_load(struct ata_port *ap, const struct ata_taskfile *tf)
{
	struct ata_ioports *ioaddr = &ap->ioaddr;
	unsigned int is_addr = tf->flags & ATA_TFLAG_ISADDR;

	if (tf->ctl != ap->last_ctl) {
		if (ioaddr->ctl_addr)
			iowrite8(tf->ctl, ioaddr->ctl_addr);
		ap->last_ctl = tf->ctl;
		ata_wait_idle(ap);
	}

	if (is_addr && (tf->flags & ATA_TFLAG_LBA48)) {
		WARN_ON_ONCE(!ioaddr->ctl_addr);
		iowrite8(tf->hob_feature, ioaddr->feature_addr);
		iowrite8(tf->hob_nsect, ioaddr->nsect_addr);
		iowrite8(tf->hob_lbal, ioaddr->lbal_addr);
		iowrite8(tf->hob_lbam, ioaddr->lbam_addr);
		iowrite8(tf->hob_lbah, ioaddr->lbah_addr);
		VPRINTK("hob: feat 0x%X nsect 0x%X, lba 0x%X 0x%X 0x%X\n",
			tf->hob_feature,
			tf->hob_nsect,
			tf->hob_lbal,
			tf->hob_lbam,
			tf->hob_lbah);
	}

	if (is_addr) {
		iowrite8(tf->feature, ioaddr->feature_addr);
		iowrite8(tf->nsect, ioaddr->nsect_addr);
		iowrite8(tf->lbal, ioaddr->lbal_addr);
		iowrite8(tf->lbam, ioaddr->lbam_addr);
		iowrite8(tf->lbah, ioaddr->lbah_addr);
		VPRINTK("feat 0x%X nsect 0x%X lba 0x%X 0x%X 0x%X\n",
			tf->feature,
			tf->nsect,
			tf->lbal,
			tf->lbam,
			tf->lbah);
	}

	if (tf->flags & ATA_TFLAG_DEVICE) {
		iowrite8(tf->device, ioaddr->device_addr);
		VPRINTK("device 0x%X\n", tf->device);
	}

	ata_wait_idle(ap);
}
EXPORT_SYMBOL_GPL(ata_sff_tf_load);

/**
 *	ata_sff_tf_read - input device's ATA taskfile shadow registers
 *	@ap: Port from which input is read
 *	@tf: ATA taskfile register set for storing input
 *
 *	Reads ATA taskfile registers for currently-selected device
 *	into @tf. Assumes the device has a fully SFF compliant task file
 *	layout and behaviour. If you device does not (eg has a different
 *	status method) then you will need to provide a replacement tf_read
 *
 *	LOCKING:
 *	Inherited from caller.
 */
void ata_sff_tf_read(struct ata_port *ap, struct ata_taskfile *tf)
{
	struct ata_ioports *ioaddr = &ap->ioaddr;

	tf->command = ata_sff_check_status(ap);
	tf->feature = ioread8(ioaddr->error_addr);
	tf->nsect = ioread8(ioaddr->nsect_addr);
	tf->lbal = ioread8(ioaddr->lbal_addr);
	tf->lbam = ioread8(ioaddr->lbam_addr);
	tf->lbah = ioread8(ioaddr->lbah_addr);
	tf->device = ioread8(ioaddr->device_addr);

	if (tf->flags & ATA_TFLAG_LBA48) {
		if (likely(ioaddr->ctl_addr)) {
			iowrite8(tf->ctl | ATA_HOB, ioaddr->ctl_addr);
			tf->hob_feature = ioread8(ioaddr->error_addr);
			tf->hob_nsect = ioread8(ioaddr->nsect_addr);
			tf->hob_lbal = ioread8(ioaddr->lbal_addr);
			tf->hob_lbam = ioread8(ioaddr->lbam_addr);
			tf->hob_lbah = ioread8(ioaddr->lbah_addr);
			iowrite8(tf->ctl, ioaddr->ctl_addr);
			ap->last_ctl = tf->ctl;
		} else
			WARN_ON_ONCE(1);
	}
}
EXPORT_SYMBOL_GPL(ata_sff_tf_read);

/**
 *	ata_sff_exec_command - issue ATA command to host controller
 *	@ap: port to which command is being issued
 *	@tf: ATA taskfile register set
 *
 *	Issues ATA command, with proper synchronization with interrupt
 *	handler / other threads.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host lock)
 */
void ata_sff_exec_command(struct ata_port *ap, const struct ata_taskfile *tf)
{
	DPRINTK("ata%u: cmd 0x%X\n", ap->print_id, tf->command);

	iowrite8(tf->command, ap->ioaddr.command_addr);
	ata_sff_pause(ap);
}
EXPORT_SYMBOL_GPL(ata_sff_exec_command);

/**
 *	ata_tf_to_host - issue ATA taskfile to host controller
 *	@ap: port to which command is being issued
 *	@tf: ATA taskfile register set
 *
 *	Issues ATA taskfile register set to ATA host controller,
 *	with proper synchronization with interrupt handler and
 *	other threads.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host lock)
 */
static inline void ata_tf_to_host(struct ata_port *ap,
				  const struct ata_taskfile *tf)
{
	ap->ops->sff_tf_load(ap, tf);
	ap->ops->sff_exec_command(ap, tf);
}

/**
 *	ata_sff_data_xfer - Transfer data by PIO
 *	@dev: device to target
 *	@buf: data buffer
 *	@buflen: buffer length
 *	@rw: read/write
 *
 *	Transfer data from/to the device data register by PIO.
 *
 *	LOCKING:
 *	Inherited from caller.
 *
 *	RETURNS:
 *	Bytes consumed.
 */
unsigned int ata_sff_data_xfer(struct ata_device *dev, unsigned char *buf,
			       unsigned int buflen, int rw)
{
	struct ata_port *ap = dev->link->ap;
	void __iomem *data_addr = ap->ioaddr.data_addr;
	unsigned int words = buflen >> 1;

	/* Transfer multiple of 2 bytes */
	if (rw == READ)
		ioread16_rep(data_addr, buf, words);
	else
		iowrite16_rep(data_addr, buf, words);

	/* Transfer trailing 1 byte, if any. */
	if (unlikely(buflen & 0x01)) {
		__le16 align_buf[1] = { 0 };
		unsigned char *trailing_buf = buf + buflen - 1;

		if (rw == READ) {
			align_buf[0] = cpu_to_le16(ioread16(data_addr));
			memcpy(trailing_buf, align_buf, 1);
		} else {
			memcpy(align_buf, trailing_buf, 1);
			iowrite16(le16_to_cpu(align_buf[0]), data_addr);
		}
		words++;
	}

	return words << 1;
}
EXPORT_SYMBOL_GPL(ata_sff_data_xfer);

/**
 *	ata_sff_data_xfer32 - Transfer data by PIO
 *	@dev: device to target
 *	@buf: data buffer
 *	@buflen: buffer length
 *	@rw: read/write
 *
 *	Transfer data from/to the device data register by PIO using 32bit
 *	I/O operations.
 *
 *	LOCKING:
 *	Inherited from caller.
 *
 *	RETURNS:
 *	Bytes consumed.
 */

unsigned int ata_sff_data_xfer32(struct ata_device *dev, unsigned char *buf,
			       unsigned int buflen, int rw)
{
	struct ata_port *ap = dev->link->ap;
	void __iomem *data_addr = ap->ioaddr.data_addr;
	unsigned int words = buflen >> 2;
	int slop = buflen & 3;

	/* Transfer multiple of 4 bytes */
	if (rw == READ)
		ioread32_rep(data_addr, buf, words);
	else
		iowrite32_rep(data_addr, buf, words);

	/* Transfer trailing bytes, if any */
	if (unlikely(slop)) {
		unsigned char pad[4];

		/* Point buf to the tail of buffer */
		buf += buflen - slop;

		/*
		 * Use io*_rep() accessors here as well to avoid pointlessly
		 * swapping bytes to and fro on the big endian machines...
		 */
		if (rw == READ) {
			if (slop < 3)
				ioread16_rep(data_addr, pad, 1);
			else
				ioread32_rep(data_addr, pad, 1);
			memcpy(buf, pad, slop);
		} else {
			memcpy(pad, buf, slop);
			if (slop < 3)
				iowrite16_rep(data_addr, pad, 1);
			else
				iowrite32_rep(data_addr, pad, 1);
		}
	}
	return (buflen + 1) & ~1;
}
EXPORT_SYMBOL_GPL(ata_sff_data_xfer32);

/**
 *	ata_sff_data_xfer_noirq - Transfer data by PIO
 *	@dev: device to target
 *	@buf: data buffer
 *	@buflen: buffer length
 *	@rw: read/write
 *
 *	Transfer data from/to the device data register by PIO. Do the
 *	transfer with interrupts disabled.
 *
 *	LOCKING:
 *	Inherited from caller.
 *
 *	RETURNS:
 *	Bytes consumed.
 */
unsigned int ata_sff_data_xfer_noirq(struct ata_device *dev, unsigned char *buf,
				     unsigned int buflen, int rw)
{
	unsigned long flags;
	unsigned int consumed;

	local_irq_save(flags);
	consumed = ata_sff_data_xfer(dev, buf, buflen, rw);
	local_irq_restore(flags);

	return consumed;
}
EXPORT_SYMBOL_GPL(ata_sff_data_xfer_noirq);

/**
 *	ata_pio_sector - Transfer a sector of data.
 *	@qc: Command on going
 *
 *	Transfer qc->sect_size bytes of data from/to the ATA device.
 *
 *	LOCKING:
 *	Inherited from caller.
 */
static void ata_pio_sector(struct ata_queued_cmd *qc)
{
	int do_write = (qc->tf.flags & ATA_TFLAG_WRITE);
	struct ata_port *ap = qc->ap;
	struct page *page;
	unsigned int offset;
	unsigned char *buf;

	if (qc->curbytes == qc->nbytes - qc->sect_size)
		ap->hsm_task_state = HSM_ST_LAST;

	page = sg_page(qc->cursg);
	offset = qc->cursg->offset + qc->cursg_ofs;

	/* get the current page and offset */
	page = nth_page(page, (offset >> PAGE_SHIFT));
	offset %= PAGE_SIZE;

	DPRINTK("data %s\n", qc->tf.flags & ATA_TFLAG_WRITE ? "write" : "read");

	if (PageHighMem(page)) {
		unsigned long flags;

		/* FIXME: use a bounce buffer */
		local_irq_save(flags);
		buf = kmap_atomic(page, KM_IRQ0);

		/* do the actual data transfer */
		ap->ops->sff_data_xfer(qc->dev, buf + offset, qc->sect_size,
				       do_write);

		kunmap_atomic(buf, KM_IRQ0);
		local_irq_restore(flags);
	} else {
		buf = page_address(page);
		ap->ops->sff_data_xfer(qc->dev, buf + offset, qc->sect_size,
				       do_write);
	}

	qc->curbytes += qc->sect_size;
	qc->cursg_ofs += qc->sect_size;

	if (qc->cursg_ofs == qc->cursg->length) {
		qc->cursg = sg_next(qc->cursg);
		qc->cursg_ofs = 0;
	}
}

/**
 *	ata_pio_sectors - Transfer one or many sectors.
 *	@qc: Command on going
 *
 *	Transfer one or many sectors of data from/to the
 *	ATA device for the DRQ request.
 *
 *	LOCKING:
 *	Inherited from caller.
 */
static void ata_pio_sectors(struct ata_queued_cmd *qc)
{
	if (is_multi_taskfile(&qc->tf)) {
		/* READ/WRITE MULTIPLE */
		unsigned int nsect;

		WARN_ON_ONCE(qc->dev->multi_count == 0);

		nsect = min((qc->nbytes - qc->curbytes) / qc->sect_size,
			    qc->dev->multi_count);
		while (nsect--)
			ata_pio_sector(qc);
	} else
		ata_pio_sector(qc);

	ata_sff_sync(qc->ap); /* flush */
}

/**
 *	atapi_send_cdb - Write CDB bytes to hardware
 *	@ap: Port to which ATAPI device is attached.
 *	@qc: Taskfile currently active
 *
 *	When device has indicated its readiness to accept
 *	a CDB, this function is called.  Send the CDB.
 *
 *	LOCKING:
 *	caller.
 */
static void atapi_send_cdb(struct ata_port *ap, struct ata_queued_cmd *qc)
{
	/* send SCSI cdb */
	DPRINTK("send cdb\n");
	WARN_ON_ONCE(qc->dev->cdb_len < 12);

	ap->ops->sff_data_xfer(qc->dev, qc->cdb, qc->dev->cdb_len, 1);
	ata_sff_sync(ap);
	/* FIXME: If the CDB is for DMA do we need to do the transition delay
	   or is bmdma_start guaranteed to do it ? */
	switch (qc->tf.protocol) {
	case ATAPI_PROT_PIO:
		ap->hsm_task_state = HSM_ST;
		break;
	case ATAPI_PROT_NODATA:
		ap->hsm_task_state = HSM_ST_LAST;
		break;
	case ATAPI_PROT_DMA:
		ap->hsm_task_state = HSM_ST_LAST;
		/* initiate bmdma */
		ap->ops->bmdma_start(qc);
		break;
	}
}

/**
 *	__atapi_pio_bytes - Transfer data from/to the ATAPI device.
 *	@qc: Command on going
 *	@bytes: number of bytes
 *
 *	Transfer Transfer data from/to the ATAPI device.
 *
 *	LOCKING:
 *	Inherited from caller.
 *
 */
static int __atapi_pio_bytes(struct ata_queued_cmd *qc, unsigned int bytes)
{
	int rw = (qc->tf.flags & ATA_TFLAG_WRITE) ? WRITE : READ;
	struct ata_port *ap = qc->ap;
	struct ata_device *dev = qc->dev;
	struct ata_eh_info *ehi = &dev->link->eh_info;
	struct scatterlist *sg;
	struct page *page;
	unsigned char *buf;
	unsigned int offset, count, consumed;

next_sg:
	sg = qc->cursg;
	if (unlikely(!sg)) {
		ata_ehi_push_desc(ehi, "unexpected or too much trailing data "
				  "buf=%u cur=%u bytes=%u",
				  qc->nbytes, qc->curbytes, bytes);
		return -1;
	}

	page = sg_page(sg);
	offset = sg->offset + qc->cursg_ofs;

	/* get the current page and offset */
	page = nth_page(page, (offset >> PAGE_SHIFT));
	offset %= PAGE_SIZE;

	/* don't overrun current sg */
	count = min(sg->length - qc->cursg_ofs, bytes);

	/* don't cross page boundaries */
	count = min(count, (unsigned int)PAGE_SIZE - offset);

	DPRINTK("data %s\n", qc->tf.flags & ATA_TFLAG_WRITE ? "write" : "read");

	if (PageHighMem(page)) {
		unsigned long flags;

		/* FIXME: use bounce buffer */
		local_irq_save(flags);
		buf = kmap_atomic(page, KM_IRQ0);

		/* do the actual data transfer */
		consumed = ap->ops->sff_data_xfer(dev,  buf + offset,
								count, rw);

		kunmap_atomic(buf, KM_IRQ0);
		local_irq_restore(flags);
	} else {
		buf = page_address(page);
		consumed = ap->ops->sff_data_xfer(dev,  buf + offset,
								count, rw);
	}

	bytes -= min(bytes, consumed);
	qc->curbytes += count;
	qc->cursg_ofs += count;

	if (qc->cursg_ofs == sg->length) {
		qc->cursg = sg_next(qc->cursg);
		qc->cursg_ofs = 0;
	}

	/*
	 * There used to be a  WARN_ON_ONCE(qc->cursg && count != consumed);
	 * Unfortunately __atapi_pio_bytes doesn't know enough to do the WARN
	 * check correctly as it doesn't know if it is the last request being
	 * made. Somebody should implement a proper sanity check.
	 */
	if (bytes)
		goto next_sg;
	return 0;
}

/**
 *	atapi_pio_bytes - Transfer data from/to the ATAPI device.
 *	@qc: Command on going
 *
 *	Transfer Transfer data from/to the ATAPI device.
 *
 *	LOCKING:
 *	Inherited from caller.
 */
static void atapi_pio_bytes(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	struct ata_device *dev = qc->dev;
	struct ata_eh_info *ehi = &dev->link->eh_info;
	unsigned int ireason, bc_lo, bc_hi, bytes;
	int i_write, do_write = (qc->tf.flags & ATA_TFLAG_WRITE) ? 1 : 0;

	/* Abuse qc->result_tf for temp storage of intermediate TF
	 * here to save some kernel stack usage.
	 * For normal completion, qc->result_tf is not relevant. For
	 * error, qc->result_tf is later overwritten by ata_qc_complete().
	 * So, the correctness of qc->result_tf is not affected.
	 */
	ap->ops->sff_tf_read(ap, &qc->result_tf);
	ireason = qc->result_tf.nsect;
	bc_lo = qc->result_tf.lbam;
	bc_hi = qc->result_tf.lbah;
	bytes = (bc_hi << 8) | bc_lo;

	/* shall be cleared to zero, indicating xfer of data */
	if (unlikely(ireason & (1 << 0)))
		goto atapi_check;

	/* make sure transfer direction matches expected */
	i_write = ((ireason & (1 << 1)) == 0) ? 1 : 0;
	if (unlikely(do_write != i_write))
		goto atapi_check;

	if (unlikely(!bytes))
		goto atapi_check;

	VPRINTK("ata%u: xfering %d bytes\n", ap->print_id, bytes);

	if (unlikely(__atapi_pio_bytes(qc, bytes)))
		goto err_out;
	ata_sff_sync(ap); /* flush */

	return;

 atapi_check:
	ata_ehi_push_desc(ehi, "ATAPI check failed (ireason=0x%x bytes=%u)",
			  ireason, bytes);
 err_out:
	qc->err_mask |= AC_ERR_HSM;
	ap->hsm_task_state = HSM_ST_ERR;
}

/**
 *	ata_hsm_ok_in_wq - Check if the qc can be handled in the workqueue.
 *	@ap: the target ata_port
 *	@qc: qc on going
 *
 *	RETURNS:
 *	1 if ok in workqueue, 0 otherwise.
 */
static inline int ata_hsm_ok_in_wq(struct ata_port *ap,
						struct ata_queued_cmd *qc)
{
	if (qc->tf.flags & ATA_TFLAG_POLLING)
		return 1;

	if (ap->hsm_task_state == HSM_ST_FIRST) {
		if (qc->tf.protocol == ATA_PROT_PIO &&
		   (qc->tf.flags & ATA_TFLAG_WRITE))
		    return 1;

		if (ata_is_atapi(qc->tf.protocol) &&
		   !(qc->dev->flags & ATA_DFLAG_CDB_INTR))
			return 1;
	}

	return 0;
}

/**
 *	ata_hsm_qc_complete - finish a qc running on standard HSM
 *	@qc: Command to complete
 *	@in_wq: 1 if called from workqueue, 0 otherwise
 *
 *	Finish @qc which is running on standard HSM.
 *
 *	LOCKING:
 *	If @in_wq is zero, spin_lock_irqsave(host lock).
 *	Otherwise, none on entry and grabs host lock.
 */
static void ata_hsm_qc_complete(struct ata_queued_cmd *qc, int in_wq)
{
	struct ata_port *ap = qc->ap;
	unsigned long flags;

	if (ap->ops->error_handler) {
		if (in_wq) {
			spin_lock_irqsave(ap->lock, flags);

			/* EH might have kicked in while host lock is
			 * released.
			 */
			qc = ata_qc_from_tag(ap, qc->tag);
			if (qc) {
				if (likely(!(qc->err_mask & AC_ERR_HSM))) {
					ap->ops->sff_irq_on(ap);
					ata_qc_complete(qc);
				} else
					ata_port_freeze(ap);
			}

			spin_unlock_irqrestore(ap->lock, flags);
		} else {
			if (likely(!(qc->err_mask & AC_ERR_HSM)))
				ata_qc_complete(qc);
			else
				ata_port_freeze(ap);
		}
	} else {
		if (in_wq) {
			spin_lock_irqsave(ap->lock, flags);
			ap->ops->sff_irq_on(ap);
			ata_qc_complete(qc);
			spin_unlock_irqrestore(ap->lock, flags);
		} else
			ata_qc_complete(qc);
	}
}

/**
 *	ata_sff_hsm_move - move the HSM to the next state.
 *	@ap: the target ata_port
 *	@qc: qc on going
 *	@status: current device status
 *	@in_wq: 1 if called from workqueue, 0 otherwise
 *
 *	RETURNS:
 *	1 when poll next status needed, 0 otherwise.
 */
int ata_sff_hsm_move(struct ata_port *ap, struct ata_queued_cmd *qc,
		     u8 status, int in_wq)
{
	struct ata_eh_info *ehi = &ap->link.eh_info;
	unsigned long flags = 0;
	int poll_next;

	WARN_ON_ONCE((qc->flags & ATA_QCFLAG_ACTIVE) == 0);

	/* Make sure ata_sff_qc_issue() does not throw things
	 * like DMA polling into the workqueue. Notice that
	 * in_wq is not equivalent to (qc->tf.flags & ATA_TFLAG_POLLING).
	 */
	WARN_ON_ONCE(in_wq != ata_hsm_ok_in_wq(ap, qc));

fsm_start:
	DPRINTK("ata%u: protocol %d task_state %d (dev_stat 0x%X)\n",
		ap->print_id, qc->tf.protocol, ap->hsm_task_state, status);

	switch (ap->hsm_task_state) {
	case HSM_ST_FIRST:
		/* Send first data block or PACKET CDB */

		/* If polling, we will stay in the work queue after
		 * sending the data. Otherwise, interrupt handler
		 * takes over after sending the data.
		 */
		poll_next = (qc->tf.flags & ATA_TFLAG_POLLING);

		/* check device status */
		if (unlikely((status & ATA_DRQ) == 0)) {
			/* handle BSY=0, DRQ=0 as error */
			if (likely(status & (ATA_ERR | ATA_DF)))
				/* device stops HSM for abort/error */
				qc->err_mask |= AC_ERR_DEV;
			else {
				/* HSM violation. Let EH handle this */
				ata_ehi_push_desc(ehi,
					"ST_FIRST: !(DRQ|ERR|DF)");
				qc->err_mask |= AC_ERR_HSM;
			}

			ap->hsm_task_state = HSM_ST_ERR;
			goto fsm_start;
		}

		/* Device should not ask for data transfer (DRQ=1)
		 * when it finds something wrong.
		 * We ignore DRQ here and stop the HSM by
		 * changing hsm_task_state to HSM_ST_ERR and
		 * let the EH abort the command or reset the device.
		 */
		if (unlikely(status & (ATA_ERR | ATA_DF))) {
			/* Some ATAPI tape drives forget to clear the ERR bit
			 * when doing the next command (mostly request sense).
			 * We ignore ERR here to workaround and proceed sending
			 * the CDB.
			 */
			if (!(qc->dev->horkage & ATA_HORKAGE_STUCK_ERR)) {
				ata_ehi_push_desc(ehi, "ST_FIRST: "
					"DRQ=1 with device error, "
					"dev_stat 0x%X", status);
				qc->err_mask |= AC_ERR_HSM;
				ap->hsm_task_state = HSM_ST_ERR;
				goto fsm_start;
			}
		}

		/* Send the CDB (atapi) or the first data block (ata pio out).
		 * During the state transition, interrupt handler shouldn't
		 * be invoked before the data transfer is complete and
		 * hsm_task_state is changed. Hence, the following locking.
		 */
		if (in_wq)
			spin_lock_irqsave(ap->lock, flags);

		if (qc->tf.protocol == ATA_PROT_PIO) {
			/* PIO data out protocol.
			 * send first data block.
			 */

			/* ata_pio_sectors() might change the state
			 * to HSM_ST_LAST. so, the state is changed here
			 * before ata_pio_sectors().
			 */
			ap->hsm_task_state = HSM_ST;
			ata_pio_sectors(qc);
		} else
			/* send CDB */
			atapi_send_cdb(ap, qc);

		if (in_wq)
			spin_unlock_irqrestore(ap->lock, flags);

		/* if polling, ata_pio_task() handles the rest.
		 * otherwise, interrupt handler takes over from here.
		 */
		break;

	case HSM_ST:
		/* complete command or read/write the data register */
		if (qc->tf.protocol == ATAPI_PROT_PIO) {
			/* ATAPI PIO protocol */
			if ((status & ATA_DRQ) == 0) {
				/* No more data to transfer or device error.
				 * Device error will be tagged in HSM_ST_LAST.
				 */
				ap->hsm_task_state = HSM_ST_LAST;
				goto fsm_start;
			}

			/* Device should not ask for data transfer (DRQ=1)
			 * when it finds something wrong.
			 * We ignore DRQ here and stop the HSM by
			 * changing hsm_task_state to HSM_ST_ERR and
			 * let the EH abort the command or reset the device.
			 */
			if (unlikely(status & (ATA_ERR | ATA_DF))) {
				ata_ehi_push_desc(ehi, "ST-ATAPI: "
					"DRQ=1 with device error, "
					"dev_stat 0x%X", status);
				qc->err_mask |= AC_ERR_HSM;
				ap->hsm_task_state = HSM_ST_ERR;
				goto fsm_start;
			}

			atapi_pio_bytes(qc);

			if (unlikely(ap->hsm_task_state == HSM_ST_ERR))
				/* bad ireason reported by device */
				goto fsm_start;

		} else {
			/* ATA PIO protocol */
			if (unlikely((status & ATA_DRQ) == 0)) {
				/* handle BSY=0, DRQ=0 as error */
				if (likely(status & (ATA_ERR | ATA_DF))) {
					/* device stops HSM for abort/error */
					qc->err_mask |= AC_ERR_DEV;

					/* If diagnostic failed and this is
					 * IDENTIFY, it's likely a phantom
					 * device.  Mark hint.
					 */
					if (qc->dev->horkage &
					    ATA_HORKAGE_DIAGNOSTIC)
						qc->err_mask |=
							AC_ERR_NODEV_HINT;
				} else {
					/* HSM violation. Let EH handle this.
					 * Phantom devices also trigger this
					 * condition.  Mark hint.
					 */
					ata_ehi_push_desc(ehi, "ST-ATA: "
						"DRQ=0 without device error, "
						"dev_stat 0x%X", status);
					qc->err_mask |= AC_ERR_HSM |
							AC_ERR_NODEV_HINT;
				}

				ap->hsm_task_state = HSM_ST_ERR;
				goto fsm_start;
			}

			/* For PIO reads, some devices may ask for
			 * data transfer (DRQ=1) alone with ERR=1.
			 * We respect DRQ here and transfer one
			 * block of junk data before changing the
			 * hsm_task_state to HSM_ST_ERR.
			 *
			 * For PIO writes, ERR=1 DRQ=1 doesn't make
			 * sense since the data block has been
			 * transferred to the device.
			 */
			if (unlikely(status & (ATA_ERR | ATA_DF))) {
				/* data might be corrputed */
				qc->err_mask |= AC_ERR_DEV;

				if (!(qc->tf.flags & ATA_TFLAG_WRITE)) {
					ata_pio_sectors(qc);
					status = ata_wait_idle(ap);
				}

				if (status & (ATA_BUSY | ATA_DRQ)) {
					ata_ehi_push_desc(ehi, "ST-ATA: "
						"BUSY|DRQ persists on ERR|DF, "
						"dev_stat 0x%X", status);
					qc->err_mask |= AC_ERR_HSM;
				}

				/* There are oddball controllers with
				 * status register stuck at 0x7f and
				 * lbal/m/h at zero which makes it
				 * pass all other presence detection
				 * mechanisms we have.  Set NODEV_HINT
				 * for it.  Kernel bz#7241.
				 */
				if (status == 0x7f)
					qc->err_mask |= AC_ERR_NODEV_HINT;

				/* ata_pio_sectors() might change the
				 * state to HSM_ST_LAST. so, the state
				 * is changed after ata_pio_sectors().
				 */
				ap->hsm_task_state = HSM_ST_ERR;
				goto fsm_start;
			}

			ata_pio_sectors(qc);

			if (ap->hsm_task_state == HSM_ST_LAST &&
			    (!(qc->tf.flags & ATA_TFLAG_WRITE))) {
				/* all data read */
				status = ata_wait_idle(ap);
				goto fsm_start;
			}
		}

		poll_next = 1;
		break;

	case HSM_ST_LAST:
		if (unlikely(!ata_ok(status))) {
			qc->err_mask |= __ac_err_mask(status);
			ap->hsm_task_state = HSM_ST_ERR;
			goto fsm_start;
		}

		/* no more data to transfer */
		DPRINTK("ata%u: dev %u command complete, drv_stat 0x%x\n",
			ap->print_id, qc->dev->devno, status);

		WARN_ON_ONCE(qc->err_mask & (AC_ERR_DEV | AC_ERR_HSM));

		ap->hsm_task_state = HSM_ST_IDLE;

		/* complete taskfile transaction */
		ata_hsm_qc_complete(qc, in_wq);

		poll_next = 0;
		break;

	case HSM_ST_ERR:
		ap->hsm_task_state = HSM_ST_IDLE;

		/* complete taskfile transaction */
		ata_hsm_qc_complete(qc, in_wq);

		poll_next = 0;
		break;
	default:
		poll_next = 0;
		BUG();
	}

	return poll_next;
}
EXPORT_SYMBOL_GPL(ata_sff_hsm_move);

void ata_pio_task(struct work_struct *work)
{
	struct ata_port *ap =
		container_of(work, struct ata_port, port_task.work);
	struct ata_queued_cmd *qc = ap->port_task_data;
	u8 status;
	int poll_next;

fsm_start:
	WARN_ON_ONCE(ap->hsm_task_state == HSM_ST_IDLE);

	/*
	 * This is purely heuristic.  This is a fast path.
	 * Sometimes when we enter, BSY will be cleared in
	 * a chk-status or two.  If not, the drive is probably seeking
	 * or something.  Snooze for a couple msecs, then
	 * chk-status again.  If still busy, queue delayed work.
	 */
	status = ata_sff_busy_wait(ap, ATA_BUSY, 5);
	if (status & ATA_BUSY) {
		msleep(2);
		status = ata_sff_busy_wait(ap, ATA_BUSY, 10);
		if (status & ATA_BUSY) {
			ata_pio_queue_task(ap, qc, ATA_SHORT_PAUSE);
			return;
		}
	}

	/* move the HSM */
	poll_next = ata_sff_hsm_move(ap, qc, status, 1);

	/* another command or interrupt handler
	 * may be running at this point.
	 */
	if (poll_next)
		goto fsm_start;
}

/**
 *	ata_sff_qc_issue - issue taskfile to device in proto-dependent manner
 *	@qc: command to issue to device
 *
 *	Using various libata functions and hooks, this function
 *	starts an ATA command.  ATA commands are grouped into
 *	classes called "protocols", and issuing each type of protocol
 *	is slightly different.
 *
 *	May be used as the qc_issue() entry in ata_port_operations.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host lock)
 *
 *	RETURNS:
 *	Zero on success, AC_ERR_* mask on failure
 */
unsigned int ata_sff_qc_issue(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;

	/* Use polling pio if the LLD doesn't handle
	 * interrupt driven pio and atapi CDB interrupt.
	 */
	if (ap->flags & ATA_FLAG_PIO_POLLING) {
		switch (qc->tf.protocol) {
		case ATA_PROT_PIO:
		case ATA_PROT_NODATA:
		case ATAPI_PROT_PIO:
		case ATAPI_PROT_NODATA:
			qc->tf.flags |= ATA_TFLAG_POLLING;
			break;
		case ATAPI_PROT_DMA:
			if (qc->dev->flags & ATA_DFLAG_CDB_INTR)
				/* see ata_dma_blacklisted() */
				BUG();
			break;
		default:
			break;
		}
	}

	/* select the device */
	ata_dev_select(ap, qc->dev->devno, 1, 0);

	/* start the command */
	switch (qc->tf.protocol) {
	case ATA_PROT_NODATA:
		if (qc->tf.flags & ATA_TFLAG_POLLING)
			ata_qc_set_polling(qc);

		ata_tf_to_host(ap, &qc->tf);
		ap->hsm_task_state = HSM_ST_LAST;

		if (qc->tf.flags & ATA_TFLAG_POLLING)
			ata_pio_queue_task(ap, qc, 0);

		break;

	case ATA_PROT_DMA:
		WARN_ON_ONCE(qc->tf.flags & ATA_TFLAG_POLLING);

		ap->ops->sff_tf_load(ap, &qc->tf);  /* load tf registers */
		ap->ops->bmdma_setup(qc);	    /* set up bmdma */
		ap->ops->bmdma_start(qc);	    /* initiate bmdma */
		ap->hsm_task_state = HSM_ST_LAST;
		break;

	case ATA_PROT_PIO:
		if (qc->tf.flags & ATA_TFLAG_POLLING)
			ata_qc_set_polling(qc);

		ata_tf_to_host(ap, &qc->tf);

		if (qc->tf.flags & ATA_TFLAG_WRITE) {
			/* PIO data out protocol */
			ap->hsm_task_state = HSM_ST_FIRST;
			ata_pio_queue_task(ap, qc, 0);

			/* always send first data block using
			 * the ata_pio_task() codepath.
			 */
		} else {
			/* PIO data in protocol */
			ap->hsm_task_state = HSM_ST;

			if (qc->tf.flags & ATA_TFLAG_POLLING)
				ata_pio_queue_task(ap, qc, 0);

			/* if polling, ata_pio_task() handles the rest.
			 * otherwise, interrupt handler takes over from here.
			 */
		}

		break;

	case ATAPI_PROT_PIO:
	case ATAPI_PROT_NODATA:
		if (qc->tf.flags & ATA_TFLAG_POLLING)
			ata_qc_set_polling(qc);

		ata_tf_to_host(ap, &qc->tf);

		ap->hsm_task_state = HSM_ST_FIRST;

		/* send cdb by polling if no cdb interrupt */
		if ((!(qc->dev->flags & ATA_DFLAG_CDB_INTR)) ||
		    (qc->tf.flags & ATA_TFLAG_POLLING))
			ata_pio_queue_task(ap, qc, 0);
		break;

	case ATAPI_PROT_DMA:
		WARN_ON_ONCE(qc->tf.flags & ATA_TFLAG_POLLING);

		ap->ops->sff_tf_load(ap, &qc->tf);  /* load tf registers */
		ap->ops->bmdma_setup(qc);	    /* set up bmdma */
		ap->hsm_task_state = HSM_ST_FIRST;

		/* send cdb by polling if no cdb interrupt */
		if (!(qc->dev->flags & ATA_DFLAG_CDB_INTR))
			ata_pio_queue_task(ap, qc, 0);
		break;

	default:
		WARN_ON_ONCE(1);
		return AC_ERR_SYSTEM;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(ata_sff_qc_issue);

/**
 *	ata_sff_qc_fill_rtf - fill result TF using ->sff_tf_read
 *	@qc: qc to fill result TF for
 *
 *	@qc is finished and result TF needs to be filled.  Fill it
 *	using ->sff_tf_read.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host lock)
 *
 *	RETURNS:
 *	true indicating that result TF is successfully filled.
 */
bool ata_sff_qc_fill_rtf(struct ata_queued_cmd *qc)
{
	qc->ap->ops->sff_tf_read(qc->ap, &qc->result_tf);
	return true;
}
EXPORT_SYMBOL_GPL(ata_sff_qc_fill_rtf);

/**
 *	ata_sff_host_intr - Handle host interrupt for given (port, task)
 *	@ap: Port on which interrupt arrived (possibly...)
 *	@qc: Taskfile currently active in engine
 *
 *	Handle host interrupt for given queued command.  Currently,
 *	only DMA interrupts are handled.  All other commands are
 *	handled via polling with interrupts disabled (nIEN bit).
 *
 *	LOCKING:
 *	spin_lock_irqsave(host lock)
 *
 *	RETURNS:
 *	One if interrupt was handled, zero if not (shared irq).
 */
unsigned int ata_sff_host_intr(struct ata_port *ap,
				      struct ata_queued_cmd *qc)
{
	struct ata_eh_info *ehi = &ap->link.eh_info;
	u8 status, host_stat = 0;

	VPRINTK("ata%u: protocol %d task_state %d\n",
		ap->print_id, qc->tf.protocol, ap->hsm_task_state);

	/* Check whether we are expecting interrupt in this state */
	switch (ap->hsm_task_state) {
	case HSM_ST_FIRST:
		/* Some pre-ATAPI-4 devices assert INTRQ
		 * at this state when ready to receive CDB.
		 */

		/* Check the ATA_DFLAG_CDB_INTR flag is enough here.
		 * The flag was turned on only for atapi devices.  No
		 * need to check ata_is_atapi(qc->tf.protocol) again.
		 */
		if (!(qc->dev->flags & ATA_DFLAG_CDB_INTR))
			goto idle_irq;
		break;
	case HSM_ST_LAST:
		if (qc->tf.protocol == ATA_PROT_DMA ||
		    qc->tf.protocol == ATAPI_PROT_DMA) {
			/* check status of DMA engine */
			host_stat = ap->ops->bmdma_status(ap);
			VPRINTK("ata%u: host_stat 0x%X\n",
				ap->print_id, host_stat);

			/* if it's not our irq... */
			if (!(host_stat & ATA_DMA_INTR))
				goto idle_irq;

			/* before we do anything else, clear DMA-Start bit */
			ap->ops->bmdma_stop(qc);

			if (unlikely(host_stat & ATA_DMA_ERR)) {
				/* error when transfering data to/from memory */
				qc->err_mask |= AC_ERR_HOST_BUS;
				ap->hsm_task_state = HSM_ST_ERR;
			}
		}
		break;
	case HSM_ST:
		break;
	default:
		goto idle_irq;
	}


	/* check main status, clearing INTRQ if needed */
	status = ata_sff_irq_status(ap);
	if (status & ATA_BUSY)
		goto idle_irq;

	/* ack bmdma irq events */
	ap->ops->sff_irq_clear(ap);

	ata_sff_hsm_move(ap, qc, status, 0);

	if (unlikely(qc->err_mask) && (qc->tf.protocol == ATA_PROT_DMA ||
				       qc->tf.protocol == ATAPI_PROT_DMA))
		ata_ehi_push_desc(ehi, "BMDMA stat 0x%x", host_stat);

	return 1;	/* irq handled */

idle_irq:
	ap->stats.idle_irq++;

#ifdef ATA_IRQ_TRAP
	if ((ap->stats.idle_irq % 1000) == 0) {
		ap->ops->sff_check_status(ap);
		ap->ops->sff_irq_clear(ap);
		ata_port_printk(ap, KERN_WARNING, "irq trap\n");
		return 1;
	}
#endif
	return 0;	/* irq not handled */
}
EXPORT_SYMBOL_GPL(ata_sff_host_intr);

/**
 *	ata_sff_interrupt - Default ATA host interrupt handler
 *	@irq: irq line (unused)
 *	@dev_instance: pointer to our ata_host information structure
 *
 *	Default interrupt handler for PCI IDE devices.  Calls
 *	ata_sff_host_intr() for each port that is not disabled.
 *
 *	LOCKING:
 *	Obtains host lock during operation.
 *
 *	RETURNS:
 *	IRQ_NONE or IRQ_HANDLED.
 */
irqreturn_t ata_sff_interrupt(int irq, void *dev_instance)
{
	struct ata_host *host = dev_instance;
	unsigned int i;
	unsigned int handled = 0;
	unsigned long flags;

	/* TODO: make _irqsave conditional on x86 PCI IDE legacy mode */
	spin_lock_irqsave(&host->lock, flags);

	for (i = 0; i < host->n_ports; i++) {
		struct ata_port *ap;

		ap = host->ports[i];
		if (ap &&
		    !(ap->flags & ATA_FLAG_DISABLED)) {
			struct ata_queued_cmd *qc;

			qc = ata_qc_from_tag(ap, ap->link.active_tag);
			if (qc && (!(qc->tf.flags & ATA_TFLAG_POLLING)) &&
			    (qc->flags & ATA_QCFLAG_ACTIVE))
				handled |= ata_sff_host_intr(ap, qc);
		}
	}

	spin_unlock_irqrestore(&host->lock, flags);

	return IRQ_RETVAL(handled);
}
EXPORT_SYMBOL_GPL(ata_sff_interrupt);

/**
 *	ata_sff_lost_interrupt	-	Check for an apparent lost interrupt
 *	@ap: port that appears to have timed out
 *
 *	Called from the libata error handlers when the core code suspects
 *	an interrupt has been lost. If it has complete anything we can and
 *	then return. Interface must support altstatus for this faster
 *	recovery to occur.
 *
 *	Locking:
 *	Caller holds host lock
 */

void ata_sff_lost_interrupt(struct ata_port *ap)
{
	u8 status;
	struct ata_queued_cmd *qc;

	/* Only one outstanding command per SFF channel */
	qc = ata_qc_from_tag(ap, ap->link.active_tag);
	/* Check we have a live one.. */
	if (qc == NULL ||  !(qc->flags & ATA_QCFLAG_ACTIVE))
		return;
	/* We cannot lose an interrupt on a polled command */
	if (qc->tf.flags & ATA_TFLAG_POLLING)
		return;
	/* See if the controller thinks it is still busy - if so the command
	   isn't a lost IRQ but is still in progress */
	status = ata_sff_altstatus(ap);
	if (status & ATA_BUSY)
		return;

	/* There was a command running, we are no longer busy and we have
	   no interrupt. */
	ata_port_printk(ap, KERN_WARNING, "lost interrupt (Status 0x%x)\n",
								status);
	/* Run the host interrupt logic as if the interrupt had not been
	   lost */
	ata_sff_host_intr(ap, qc);
}
EXPORT_SYMBOL_GPL(ata_sff_lost_interrupt);

/**
 *	ata_sff_freeze - Freeze SFF controller port
 *	@ap: port to freeze
 *
 *	Freeze BMDMA controller port.
 *
 *	LOCKING:
 *	Inherited from caller.
 */
void ata_sff_freeze(struct ata_port *ap)
{
	struct ata_ioports *ioaddr = &ap->ioaddr;

	ap->ctl |= ATA_NIEN;
	ap->last_ctl = ap->ctl;

	if (ioaddr->ctl_addr)
		iowrite8(ap->ctl, ioaddr->ctl_addr);

	/* Under certain circumstances, some controllers raise IRQ on
	 * ATA_NIEN manipulation.  Also, many controllers fail to mask
	 * previously pending IRQ on ATA_NIEN assertion.  Clear it.
	 */
	ap->ops->sff_check_status(ap);

	ap->ops->sff_irq_clear(ap);
}
EXPORT_SYMBOL_GPL(ata_sff_freeze);

/**
 *	ata_sff_thaw - Thaw SFF controller port
 *	@ap: port to thaw
 *
 *	Thaw SFF controller port.
 *
 *	LOCKING:
 *	Inherited from caller.
 */
void ata_sff_thaw(struct ata_port *ap)
{
	/* clear & re-enable interrupts */
	ap->ops->sff_check_status(ap);
	ap->ops->sff_irq_clear(ap);
	ap->ops->sff_irq_on(ap);
}
EXPORT_SYMBOL_GPL(ata_sff_thaw);

/**
 *	ata_sff_prereset - prepare SFF link for reset
 *	@link: SFF link to be reset
 *	@deadline: deadline jiffies for the operation
 *
 *	SFF link @link is about to be reset.  Initialize it.  It first
 *	calls ata_std_prereset() and wait for !BSY if the port is
 *	being softreset.
 *
 *	LOCKING:
 *	Kernel thread context (may sleep)
 *
 *	RETURNS:
 *	0 on success, -errno otherwise.
 */
int ata_sff_prereset(struct ata_link *link, unsigned long deadline)
{
	struct ata_eh_context *ehc = &link->eh_context;
	int rc;

	rc = ata_std_prereset(link, deadline);
	if (rc)
		return rc;

	/* if we're about to do hardreset, nothing more to do */
	if (ehc->i.action & ATA_EH_HARDRESET)
		return 0;

	/* wait for !BSY if we don't know that no device is attached */
	if (!ata_link_offline(link)) {
		rc = ata_sff_wait_ready(link, deadline);
		if (rc && rc != -ENODEV) {
			ata_link_printk(link, KERN_WARNING, "device not ready "
					"(errno=%d), forcing hardreset\n", rc);
			ehc->i.action |= ATA_EH_HARDRESET;
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(ata_sff_prereset);

/**
 *	ata_devchk - PATA device presence detection
 *	@ap: ATA channel to examine
 *	@device: Device to examine (starting at zero)
 *
 *	This technique was originally described in
 *	Hale Landis's ATADRVR (www.ata-atapi.com), and
 *	later found its way into the ATA/ATAPI spec.
 *
 *	Write a pattern to the ATA shadow registers,
 *	and if a device is present, it will respond by
 *	correctly storing and echoing back the
 *	ATA shadow register contents.
 *
 *	LOCKING:
 *	caller.
 */
static unsigned int ata_devchk(struct ata_port *ap, unsigned int device)
{
	struct ata_ioports *ioaddr = &ap->ioaddr;
	u8 nsect, lbal;

	ap->ops->sff_dev_select(ap, device);

	iowrite8(0x55, ioaddr->nsect_addr);
	iowrite8(0xaa, ioaddr->lbal_addr);

	iowrite8(0xaa, ioaddr->nsect_addr);
	iowrite8(0x55, ioaddr->lbal_addr);

	iowrite8(0x55, ioaddr->nsect_addr);
	iowrite8(0xaa, ioaddr->lbal_addr);

	nsect = ioread8(ioaddr->nsect_addr);
	lbal = ioread8(ioaddr->lbal_addr);

	if ((nsect == 0x55) && (lbal == 0xaa))
		return 1;	/* we found a device */

	return 0;		/* nothing found */
}

/**
 *	ata_sff_dev_classify - Parse returned ATA device signature
 *	@dev: ATA device to classify (starting at zero)
 *	@present: device seems present
 *	@r_err: Value of error register on completion
 *
 *	After an event -- SRST, E.D.D., or SATA COMRESET -- occurs,
 *	an ATA/ATAPI-defined set of values is placed in the ATA
 *	shadow registers, indicating the results of device detection
 *	and diagnostics.
 *
 *	Select the ATA device, and read the values from the ATA shadow
 *	registers.  Then parse according to the Error register value,
 *	and the spec-defined values examined by ata_dev_classify().
 *
 *	LOCKING:
 *	caller.
 *
 *	RETURNS:
 *	Device type - %ATA_DEV_ATA, %ATA_DEV_ATAPI or %ATA_DEV_NONE.
 */
unsigned int ata_sff_dev_classify(struct ata_device *dev, int present,
				  u8 *r_err)
{
	struct ata_port *ap = dev->link->ap;
	struct ata_taskfile tf;
	unsigned int class;
	u8 err;

	ap->ops->sff_dev_select(ap, dev->devno);

	memset(&tf, 0, sizeof(tf));

	ap->ops->sff_tf_read(ap, &tf);
	err = tf.feature;
	if (r_err)
		*r_err = err;

	/* see if device passed diags: continue and warn later */
	if (err == 0)
		/* diagnostic fail : do nothing _YET_ */
		dev->horkage |= ATA_HORKAGE_DIAGNOSTIC;
	else if (err == 1)
		/* do nothing */ ;
	else if ((dev->devno == 0) && (err == 0x81))
		/* do nothing */ ;
	else
		return ATA_DEV_NONE;

	/* determine if device is ATA or ATAPI */
	class = ata_dev_classify(&tf);

	if (class == ATA_DEV_UNKNOWN) {
		/* If the device failed diagnostic, it's likely to
		 * have reported incorrect device signature too.
		 * Assume ATA device if the device seems present but
		 * device signature is invalid with diagnostic
		 * failure.
		 */
		if (present && (dev->horkage & ATA_HORKAGE_DIAGNOSTIC))
			class = ATA_DEV_ATA;
		else
			class = ATA_DEV_NONE;
	} else if ((class == ATA_DEV_ATA) &&
		   (ap->ops->sff_check_status(ap) == 0))
		class = ATA_DEV_NONE;

	return class;
}
EXPORT_SYMBOL_GPL(ata_sff_dev_classify);

/**
 *	ata_sff_wait_after_reset - wait for devices to become ready after reset
 *	@link: SFF link which is just reset
 *	@devmask: mask of present devices
 *	@deadline: deadline jiffies for the operation
 *
 *	Wait devices attached to SFF @link to become ready after
 *	reset.  It contains preceding 150ms wait to avoid accessing TF
 *	status register too early.
 *
 *	LOCKING:
 *	Kernel thread context (may sleep).
 *
 *	RETURNS:
 *	0 on success, -ENODEV if some or all of devices in @devmask
 *	don't seem to exist.  -errno on other errors.
 */
int ata_sff_wait_after_reset(struct ata_link *link, unsigned int devmask,
			     unsigned long deadline)
{
	struct ata_port *ap = link->ap;
	struct ata_ioports *ioaddr = &ap->ioaddr;
	unsigned int dev0 = devmask & (1 << 0);
	unsigned int dev1 = devmask & (1 << 1);
	int rc, ret = 0;

	msleep(ATA_WAIT_AFTER_RESET);

	/* always check readiness of the master device */
	rc = ata_sff_wait_ready(link, deadline);
	/* -ENODEV means the odd clown forgot the D7 pulldown resistor
	 * and TF status is 0xff, bail out on it too.
	 */
	if (rc)
		return rc;

	/* if device 1 was found in ata_devchk, wait for register
	 * access briefly, then wait for BSY to clear.
	 */
	if (dev1) {
		int i;

		ap->ops->sff_dev_select(ap, 1);

		/* Wait for register access.  Some ATAPI devices fail
		 * to set nsect/lbal after reset, so don't waste too
		 * much time on it.  We're gonna wait for !BSY anyway.
		 */
		for (i = 0; i < 2; i++) {
			u8 nsect, lbal;

			nsect = ioread8(ioaddr->nsect_addr);
			lbal = ioread8(ioaddr->lbal_addr);
			if ((nsect == 1) && (lbal == 1))
				break;
			msleep(50);	/* give drive a breather */
		}

		rc = ata_sff_wait_ready(link, deadline);
		if (rc) {
			if (rc != -ENODEV)
				return rc;
			ret = rc;
		}
	}

	/* is all this really necessary? */
	ap->ops->sff_dev_select(ap, 0);
	if (dev1)
		ap->ops->sff_dev_select(ap, 1);
	if (dev0)
		ap->ops->sff_dev_select(ap, 0);

	return ret;
}
EXPORT_SYMBOL_GPL(ata_sff_wait_after_reset);

static int ata_bus_softreset(struct ata_port *ap, unsigned int devmask,
			     unsigned long deadline)
{
	struct ata_ioports *ioaddr = &ap->ioaddr;

	DPRINTK("ata%u: bus reset via SRST\n", ap->print_id);

	/* software reset.  causes dev0 to be selected */
	iowrite8(ap->ctl, ioaddr->ctl_addr);
	udelay(20);	/* FIXME: flush */
	iowrite8(ap->ctl | ATA_SRST, ioaddr->ctl_addr);
	udelay(20);	/* FIXME: flush */
	iowrite8(ap->ctl, ioaddr->ctl_addr);
	ap->last_ctl = ap->ctl;

	/* wait the port to become ready */
	return ata_sff_wait_after_reset(&ap->link, devmask, deadline);
}

/**
 *	ata_sff_softreset - reset host port via ATA SRST
 *	@link: ATA link to reset
 *	@classes: resulting classes of attached devices
 *	@deadline: deadline jiffies for the operation
 *
 *	Reset host port using ATA SRST.
 *
 *	LOCKING:
 *	Kernel thread context (may sleep)
 *
 *	RETURNS:
 *	0 on success, -errno otherwise.
 */
int ata_sff_softreset(struct ata_link *link, unsigned int *classes,
		      unsigned long deadline)
{
	struct ata_port *ap = link->ap;
	unsigned int slave_possible = ap->flags & ATA_FLAG_SLAVE_POSS;
	unsigned int devmask = 0;
	int rc;
	u8 err;

	DPRINTK("ENTER\n");

	/* determine if device 0/1 are present */
	if (ata_devchk(ap, 0))
		devmask |= (1 << 0);
	if (slave_possible && ata_devchk(ap, 1))
		devmask |= (1 << 1);

	/* select device 0 again */
	ap->ops->sff_dev_select(ap, 0);

	/* issue bus reset */
	DPRINTK("about to softreset, devmask=%x\n", devmask);
	rc = ata_bus_softreset(ap, devmask, deadline);
	/* if link is occupied, -ENODEV too is an error */
	if (rc && (rc != -ENODEV || sata_scr_valid(link))) {
		ata_link_printk(link, KERN_ERR, "SRST failed (errno=%d)\n", rc);
		return rc;
	}

	/* determine by signature whether we have ATA or ATAPI devices */
	classes[0] = ata_sff_dev_classify(&link->device[0],
					  devmask & (1 << 0), &err);
	if (slave_possible && err != 0x81)
		classes[1] = ata_sff_dev_classify(&link->device[1],
						  devmask & (1 << 1), &err);

	DPRINTK("EXIT, classes[0]=%u [1]=%u\n", classes[0], classes[1]);
	return 0;
}
EXPORT_SYMBOL_GPL(ata_sff_softreset);

/**
 *	sata_sff_hardreset - reset host port via SATA phy reset
 *	@link: link to reset
 *	@class: resulting class of attached device
 *	@deadline: deadline jiffies for the operation
 *
 *	SATA phy-reset host port using DET bits of SControl register,
 *	wait for !BSY and classify the attached device.
 *
 *	LOCKING:
 *	Kernel thread context (may sleep)
 *
 *	RETURNS:
 *	0 on success, -errno otherwise.
 */
int sata_sff_hardreset(struct ata_link *link, unsigned int *class,
		       unsigned long deadline)
{
	struct ata_eh_context *ehc = &link->eh_context;
	const unsigned long *timing = sata_ehc_deb_timing(ehc);
	bool online;
	int rc;

	rc = sata_link_hardreset(link, timing, deadline, &online,
				 ata_sff_check_ready);
	if (online)
		*class = ata_sff_dev_classify(link->device, 1, NULL);

	DPRINTK("EXIT, class=%u\n", *class);
	return rc;
}
EXPORT_SYMBOL_GPL(sata_sff_hardreset);

/**
 *	ata_sff_postreset - SFF postreset callback
 *	@link: the target SFF ata_link
 *	@classes: classes of attached devices
 *
 *	This function is invoked after a successful reset.  It first
 *	calls ata_std_postreset() and performs SFF specific postreset
 *	processing.
 *
 *	LOCKING:
 *	Kernel thread context (may sleep)
 */
void ata_sff_postreset(struct ata_link *link, unsigned int *classes)
{
	struct ata_port *ap = link->ap;

	ata_std_postreset(link, classes);

	/* is double-select really necessary? */
	if (classes[0] != ATA_DEV_NONE)
		ap->ops->sff_dev_select(ap, 1);
	if (classes[1] != ATA_DEV_NONE)
		ap->ops->sff_dev_select(ap, 0);

	/* bail out if no device is present */
	if (classes[0] == ATA_DEV_NONE && classes[1] == ATA_DEV_NONE) {
		DPRINTK("EXIT, no device\n");
		return;
	}

	/* set up device control */
	if (ap->ioaddr.ctl_addr) {
		iowrite8(ap->ctl, ap->ioaddr.ctl_addr);
		ap->last_ctl = ap->ctl;
	}
}
EXPORT_SYMBOL_GPL(ata_sff_postreset);

/**
 *	ata_sff_drain_fifo - Stock FIFO drain logic for SFF controllers
 *	@qc: command
 *
 *	Drain the FIFO and device of any stuck data following a command
 *	failing to complete. In some cases this is neccessary before a
 *	reset will recover the device.
 *
 */

void ata_sff_drain_fifo(struct ata_queued_cmd *qc)
{
	int count;
	struct ata_port *ap;

	/* We only need to flush incoming data when a command was running */
	if (qc == NULL || qc->dma_dir == DMA_TO_DEVICE)
		return;

	ap = qc->ap;
	/* Drain up to 64K of data before we give up this recovery method */
	for (count = 0; (ap->ops->sff_check_status(ap) & ATA_DRQ)
						&& count < 32768; count++)
		ioread16(ap->ioaddr.data_addr);

	/* Can become DEBUG later */
	if (count)
		ata_port_printk(ap, KERN_DEBUG,
			"drained %d bytes to clear DRQ.\n", count);

}
EXPORT_SYMBOL_GPL(ata_sff_drain_fifo);

/**
 *	ata_sff_error_handler - Stock error handler for BMDMA controller
 *	@ap: port to handle error for
 *
 *	Stock error handler for SFF controller.  It can handle both
 *	PATA and SATA controllers.  Many controllers should be able to
 *	use this EH as-is or with some added handling before and
 *	after.
 *
 *	LOCKING:
 *	Kernel thread context (may sleep)
 */
void ata_sff_error_handler(struct ata_port *ap)
{
	ata_reset_fn_t softreset = ap->ops->softreset;
	ata_reset_fn_t hardreset = ap->ops->hardreset;
	struct ata_queued_cmd *qc;
	unsigned long flags;
	int thaw = 0;

	qc = __ata_qc_from_tag(ap, ap->link.active_tag);
	if (qc && !(qc->flags & ATA_QCFLAG_FAILED))
		qc = NULL;

	/* reset PIO HSM and stop DMA engine */
	spin_lock_irqsave(ap->lock, flags);

	ap->hsm_task_state = HSM_ST_IDLE;

	if (ap->ioaddr.bmdma_addr &&
	    qc && (qc->tf.protocol == ATA_PROT_DMA ||
		   qc->tf.protocol == ATAPI_PROT_DMA)) {
		u8 host_stat;

		host_stat = ap->ops->bmdma_status(ap);

		/* BMDMA controllers indicate host bus error by
		 * setting DMA_ERR bit and timing out.  As it wasn't
		 * really a timeout event, adjust error mask and
		 * cancel frozen state.
		 */
		if (qc->err_mask == AC_ERR_TIMEOUT
						&& (host_stat & ATA_DMA_ERR)) {
			qc->err_mask = AC_ERR_HOST_BUS;
			thaw = 1;
		}

		ap->ops->bmdma_stop(qc);
	}

	ata_sff_sync(ap);		/* FIXME: We don't need this */
	ap->ops->sff_check_status(ap);
	ap->ops->sff_irq_clear(ap);
	/* We *MUST* do FIFO draining before we issue a reset as several
	 * devices helpfully clear their internal state and will lock solid
	 * if we touch the data port post reset. Pass qc in case anyone wants
	 *  to do different PIO/DMA recovery or has per command fixups
	 */
	if (ap->ops->drain_fifo)
		ap->ops->drain_fifo(qc);

	spin_unlock_irqrestore(ap->lock, flags);

	if (thaw)
		ata_eh_thaw_port(ap);

	/* PIO and DMA engines have been stopped, perform recovery */

	/* Ignore ata_sff_softreset if ctl isn't accessible and
	 * built-in hardresets if SCR access isn't available.
	 */
	if (softreset == ata_sff_softreset && !ap->ioaddr.ctl_addr)
		softreset = NULL;
	if (ata_is_builtin_hardreset(hardreset) && !sata_scr_valid(&ap->link))
		hardreset = NULL;

	ata_do_eh(ap, ap->ops->prereset, softreset, hardreset,
		  ap->ops->postreset);
}
EXPORT_SYMBOL_GPL(ata_sff_error_handler);

/**
 *	ata_sff_post_internal_cmd - Stock post_internal_cmd for SFF controller
 *	@qc: internal command to clean up
 *
 *	LOCKING:
 *	Kernel thread context (may sleep)
 */
void ata_sff_post_internal_cmd(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	unsigned long flags;

	spin_lock_irqsave(ap->lock, flags);

	ap->hsm_task_state = HSM_ST_IDLE;

	if (ap->ioaddr.bmdma_addr)
		ata_bmdma_stop(qc);

	spin_unlock_irqrestore(ap->lock, flags);
}
EXPORT_SYMBOL_GPL(ata_sff_post_internal_cmd);

/**
 *	ata_sff_port_start - Set port up for dma.
 *	@ap: Port to initialize
 *
 *	Called just after data structures for each port are
 *	initialized.  Allocates space for PRD table if the device
 *	is DMA capable SFF.
 *
 *	May be used as the port_start() entry in ata_port_operations.
 *
 *	LOCKING:
 *	Inherited from caller.
 */
int ata_sff_port_start(struct ata_port *ap)
{
	if (ap->ioaddr.bmdma_addr)
		return ata_port_start(ap);
	return 0;
}
EXPORT_SYMBOL_GPL(ata_sff_port_start);

/**
 *	ata_sff_std_ports - initialize ioaddr with standard port offsets.
 *	@ioaddr: IO address structure to be initialized
 *
 *	Utility function which initializes data_addr, error_addr,
 *	feature_addr, nsect_addr, lbal_addr, lbam_addr, lbah_addr,
 *	device_addr, status_addr, and command_addr to standard offsets
 *	relative to cmd_addr.
 *
 *	Does not set ctl_addr, altstatus_addr, bmdma_addr, or scr_addr.
 */
void ata_sff_std_ports(struct ata_ioports *ioaddr)
{
	ioaddr->data_addr = ioaddr->cmd_addr + ATA_REG_DATA;
	ioaddr->error_addr = ioaddr->cmd_addr + ATA_REG_ERR;
	ioaddr->feature_addr = ioaddr->cmd_addr + ATA_REG_FEATURE;
	ioaddr->nsect_addr = ioaddr->cmd_addr + ATA_REG_NSECT;
	ioaddr->lbal_addr = ioaddr->cmd_addr + ATA_REG_LBAL;
	ioaddr->lbam_addr = ioaddr->cmd_addr + ATA_REG_LBAM;
	ioaddr->lbah_addr = ioaddr->cmd_addr + ATA_REG_LBAH;
	ioaddr->device_addr = ioaddr->cmd_addr + ATA_REG_DEVICE;
	ioaddr->status_addr = ioaddr->cmd_addr + ATA_REG_STATUS;
	ioaddr->command_addr = ioaddr->cmd_addr + ATA_REG_CMD;
}
EXPORT_SYMBOL_GPL(ata_sff_std_ports);

unsigned long ata_bmdma_mode_filter(struct ata_device *adev,
				    unsigned long xfer_mask)
{
	/* Filter out DMA modes if the device has been configured by
	   the BIOS as PIO only */

	if (adev->link->ap->ioaddr.bmdma_addr == NULL)
		xfer_mask &= ~(ATA_MASK_MWDMA | ATA_MASK_UDMA);
	return xfer_mask;
}
EXPORT_SYMBOL_GPL(ata_bmdma_mode_filter);

/**
 *	ata_bmdma_setup - Set up PCI IDE BMDMA transaction
 *	@qc: Info associated with this ATA transaction.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host lock)
 */
void ata_bmdma_setup(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	unsigned int rw = (qc->tf.flags & ATA_TFLAG_WRITE);
	u8 dmactl;

	/* load PRD table addr. */
	mb();	/* make sure PRD table writes are visible to controller */
	iowrite32(ap->prd_dma, ap->ioaddr.bmdma_addr + ATA_DMA_TABLE_OFS);

	/* specify data direction, triple-check start bit is clear */
	dmactl = ioread8(ap->ioaddr.bmdma_addr + ATA_DMA_CMD);
	dmactl &= ~(ATA_DMA_WR | ATA_DMA_START);
	if (!rw)
		dmactl |= ATA_DMA_WR;
	iowrite8(dmactl, ap->ioaddr.bmdma_addr + ATA_DMA_CMD);

	/* issue r/w command */
	ap->ops->sff_exec_command(ap, &qc->tf);
}
EXPORT_SYMBOL_GPL(ata_bmdma_setup);

/**
 *	ata_bmdma_start - Start a PCI IDE BMDMA transaction
 *	@qc: Info associated with this ATA transaction.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host lock)
 */
void ata_bmdma_start(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	u8 dmactl;

	/* start host DMA transaction */
	dmactl = ioread8(ap->ioaddr.bmdma_addr + ATA_DMA_CMD);
	iowrite8(dmactl | ATA_DMA_START, ap->ioaddr.bmdma_addr + ATA_DMA_CMD);

	/* Strictly, one may wish to issue an ioread8() here, to
	 * flush the mmio write.  However, control also passes
	 * to the hardware at this point, and it will interrupt
	 * us when we are to resume control.  So, in effect,
	 * we don't care when the mmio write flushes.
	 * Further, a read of the DMA status register _immediately_
	 * following the write may not be what certain flaky hardware
	 * is expected, so I think it is best to not add a readb()
	 * without first all the MMIO ATA cards/mobos.
	 * Or maybe I'm just being paranoid.
	 *
	 * FIXME: The posting of this write means I/O starts are
	 * unneccessarily delayed for MMIO
	 */
}
EXPORT_SYMBOL_GPL(ata_bmdma_start);

/**
 *	ata_bmdma_stop - Stop PCI IDE BMDMA transfer
 *	@qc: Command we are ending DMA for
 *
 *	Clears the ATA_DMA_START flag in the dma control register
 *
 *	May be used as the bmdma_stop() entry in ata_port_operations.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host lock)
 */
void ata_bmdma_stop(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	void __iomem *mmio = ap->ioaddr.bmdma_addr;

	/* clear start/stop bit */
	iowrite8(ioread8(mmio + ATA_DMA_CMD) & ~ATA_DMA_START,
		 mmio + ATA_DMA_CMD);

	/* one-PIO-cycle guaranteed wait, per spec, for HDMA1:0 transition */
	ata_sff_dma_pause(ap);
}
EXPORT_SYMBOL_GPL(ata_bmdma_stop);

/**
 *	ata_bmdma_status - Read PCI IDE BMDMA status
 *	@ap: Port associated with this ATA transaction.
 *
 *	Read and return BMDMA status register.
 *
 *	May be used as the bmdma_status() entry in ata_port_operations.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host lock)
 */
u8 ata_bmdma_status(struct ata_port *ap)
{
	return ioread8(ap->ioaddr.bmdma_addr + ATA_DMA_STATUS);
}
EXPORT_SYMBOL_GPL(ata_bmdma_status);

/**
 *	ata_bus_reset - reset host port and associated ATA channel
 *	@ap: port to reset
 *
 *	This is typically the first time we actually start issuing
 *	commands to the ATA channel.  We wait for BSY to clear, then
 *	issue EXECUTE DEVICE DIAGNOSTIC command, polling for its
 *	result.  Determine what devices, if any, are on the channel
 *	by looking at the device 0/1 error register.  Look at the signature
 *	stored in each device's taskfile registers, to determine if
 *	the device is ATA or ATAPI.
 *
 *	LOCKING:
 *	PCI/etc. bus probe sem.
 *	Obtains host lock.
 *
 *	SIDE EFFECTS:
 *	Sets ATA_FLAG_DISABLED if bus reset fails.
 *
 *	DEPRECATED:
 *	This function is only for drivers which still use old EH and
 *	will be removed soon.
 */
void ata_bus_reset(struct ata_port *ap)
{
	struct ata_device *device = ap->link.device;
	struct ata_ioports *ioaddr = &ap->ioaddr;
	unsigned int slave_possible = ap->flags & ATA_FLAG_SLAVE_POSS;
	u8 err;
	unsigned int dev0, dev1 = 0, devmask = 0;
	int rc;

	DPRINTK("ENTER, host %u, port %u\n", ap->print_id, ap->port_no);

	/* determine if device 0/1 are present */
	if (ap->flags & ATA_FLAG_SATA_RESET)
		dev0 = 1;
	else {
		dev0 = ata_devchk(ap, 0);
		if (slave_possible)
			dev1 = ata_devchk(ap, 1);
	}

	if (dev0)
		devmask |= (1 << 0);
	if (dev1)
		devmask |= (1 << 1);

	/* select device 0 again */
	ap->ops->sff_dev_select(ap, 0);

	/* issue bus reset */
	if (ap->flags & ATA_FLAG_SRST) {
		rc = ata_bus_softreset(ap, devmask,
				       ata_deadline(jiffies, 40000));
		if (rc && rc != -ENODEV)
			goto err_out;
	}

	/*
	 * determine by signature whether we have ATA or ATAPI devices
	 */
	device[0].class = ata_sff_dev_classify(&device[0], dev0, &err);
	if ((slave_possible) && (err != 0x81))
		device[1].class = ata_sff_dev_classify(&device[1], dev1, &err);

	/* is double-select really necessary? */
	if (device[1].class != ATA_DEV_NONE)
		ap->ops->sff_dev_select(ap, 1);
	if (device[0].class != ATA_DEV_NONE)
		ap->ops->sff_dev_select(ap, 0);

	/* if no devices were detected, disable this port */
	if ((device[0].class == ATA_DEV_NONE) &&
	    (device[1].class == ATA_DEV_NONE))
		goto err_out;

	if (ap->flags & (ATA_FLAG_SATA_RESET | ATA_FLAG_SRST)) {
		/* set up device control for ATA_FLAG_SATA_RESET */
		iowrite8(ap->ctl, ioaddr->ctl_addr);
		ap->last_ctl = ap->ctl;
	}

	DPRINTK("EXIT\n");
	return;

err_out:
	ata_port_printk(ap, KERN_ERR, "disabling port\n");
	ata_port_disable(ap);

	DPRINTK("EXIT\n");
}
EXPORT_SYMBOL_GPL(ata_bus_reset);

#ifdef CONFIG_PCI

/**
 *	ata_pci_bmdma_clear_simplex -	attempt to kick device out of simplex
 *	@pdev: PCI device
 *
 *	Some PCI ATA devices report simplex mode but in fact can be told to
 *	enter non simplex mode. This implements the necessary logic to
 *	perform the task on such devices. Calling it on other devices will
 *	have -undefined- behaviour.
 */
int ata_pci_bmdma_clear_simplex(struct pci_dev *pdev)
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
EXPORT_SYMBOL_GPL(ata_pci_bmdma_clear_simplex);

/**
 *	ata_pci_bmdma_init - acquire PCI BMDMA resources and init ATA host
 *	@host: target ATA host
 *
 *	Acquire PCI BMDMA resources and initialize @host accordingly.
 *
 *	LOCKING:
 *	Inherited from calling layer (may sleep).
 *
 *	RETURNS:
 *	0 on success, -errno otherwise.
 */
int ata_pci_bmdma_init(struct ata_host *host)
{
	struct device *gdev = host->dev;
	struct pci_dev *pdev = to_pci_dev(gdev);
	int i, rc;

	/* No BAR4 allocation: No DMA */
	if (pci_resource_start(pdev, 4) == 0)
		return 0;

	/* TODO: If we get no DMA mask we should fall back to PIO */
	rc = pci_set_dma_mask(pdev, ATA_DMA_MASK);
	if (rc)
		return rc;
	rc = pci_set_consistent_dma_mask(pdev, ATA_DMA_MASK);
	if (rc)
		return rc;

	/* request and iomap DMA region */
	rc = pcim_iomap_regions(pdev, 1 << 4, dev_driver_string(gdev));
	if (rc) {
		dev_printk(KERN_ERR, gdev, "failed to request/iomap BAR4\n");
		return -ENOMEM;
	}
	host->iomap = pcim_iomap_table(pdev);

	for (i = 0; i < 2; i++) {
		struct ata_port *ap = host->ports[i];
		void __iomem *bmdma = host->iomap[4] + 8 * i;

		if (ata_port_is_dummy(ap))
			continue;

		ap->ioaddr.bmdma_addr = bmdma;
		if ((!(ap->flags & ATA_FLAG_IGN_SIMPLEX)) &&
		    (ioread8(bmdma + 2) & 0x80))
			host->flags |= ATA_HOST_SIMPLEX;

		ata_port_desc(ap, "bmdma 0x%llx",
		    (unsigned long long)pci_resource_start(pdev, 4) + 8 * i);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(ata_pci_bmdma_init);

static int ata_resources_present(struct pci_dev *pdev, int port)
{
	int i;

	/* Check the PCI resources for this channel are enabled */
	port = port * 2;
	for (i = 0; i < 2; i++) {
		if (pci_resource_start(pdev, port + i) == 0 ||
		    pci_resource_len(pdev, port + i) == 0)
			return 0;
	}
	return 1;
}

/**
 *	ata_pci_sff_init_host - acquire native PCI ATA resources and init host
 *	@host: target ATA host
 *
 *	Acquire native PCI ATA resources for @host and initialize the
 *	first two ports of @host accordingly.  Ports marked dummy are
 *	skipped and allocation failure makes the port dummy.
 *
 *	Note that native PCI resources are valid even for legacy hosts
 *	as we fix up pdev resources array early in boot, so this
 *	function can be used for both native and legacy SFF hosts.
 *
 *	LOCKING:
 *	Inherited from calling layer (may sleep).
 *
 *	RETURNS:
 *	0 if at least one port is initialized, -ENODEV if no port is
 *	available.
 */
int ata_pci_sff_init_host(struct ata_host *host)
{
	struct device *gdev = host->dev;
	struct pci_dev *pdev = to_pci_dev(gdev);
	unsigned int mask = 0;
	int i, rc;

	/* request, iomap BARs and init port addresses accordingly */
	for (i = 0; i < 2; i++) {
		struct ata_port *ap = host->ports[i];
		int base = i * 2;
		void __iomem * const *iomap;

		if (ata_port_is_dummy(ap))
			continue;

		/* Discard disabled ports.  Some controllers show
		 * their unused channels this way.  Disabled ports are
		 * made dummy.
		 */
		if (!ata_resources_present(pdev, i)) {
			ap->ops = &ata_dummy_port_ops;
			continue;
		}

		rc = pcim_iomap_regions(pdev, 0x3 << base,
					dev_driver_string(gdev));
		if (rc) {
			dev_printk(KERN_WARNING, gdev,
				   "failed to request/iomap BARs for port %d "
				   "(errno=%d)\n", i, rc);
			if (rc == -EBUSY)
				pcim_pin_device(pdev);
			ap->ops = &ata_dummy_port_ops;
			continue;
		}
		host->iomap = iomap = pcim_iomap_table(pdev);

		ap->ioaddr.cmd_addr = iomap[base];
		ap->ioaddr.altstatus_addr =
		ap->ioaddr.ctl_addr = (void __iomem *)
			((unsigned long)iomap[base + 1] | ATA_PCI_CTL_OFS);
		ata_sff_std_ports(&ap->ioaddr);

		ata_port_desc(ap, "cmd 0x%llx ctl 0x%llx",
			(unsigned long long)pci_resource_start(pdev, base),
			(unsigned long long)pci_resource_start(pdev, base + 1));

		mask |= 1 << i;
	}

	if (!mask) {
		dev_printk(KERN_ERR, gdev, "no available native port\n");
		return -ENODEV;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(ata_pci_sff_init_host);

/**
 *	ata_pci_sff_prepare_host - helper to prepare native PCI ATA host
 *	@pdev: target PCI device
 *	@ppi: array of port_info, must be enough for two ports
 *	@r_host: out argument for the initialized ATA host
 *
 *	Helper to allocate ATA host for @pdev, acquire all native PCI
 *	resources and initialize it accordingly in one go.
 *
 *	LOCKING:
 *	Inherited from calling layer (may sleep).
 *
 *	RETURNS:
 *	0 on success, -errno otherwise.
 */
int ata_pci_sff_prepare_host(struct pci_dev *pdev,
			     const struct ata_port_info * const *ppi,
			     struct ata_host **r_host)
{
	struct ata_host *host;
	int rc;

	if (!devres_open_group(&pdev->dev, NULL, GFP_KERNEL))
		return -ENOMEM;

	host = ata_host_alloc_pinfo(&pdev->dev, ppi, 2);
	if (!host) {
		dev_printk(KERN_ERR, &pdev->dev,
			   "failed to allocate ATA host\n");
		rc = -ENOMEM;
		goto err_out;
	}

	rc = ata_pci_sff_init_host(host);
	if (rc)
		goto err_out;

	/* init DMA related stuff */
	rc = ata_pci_bmdma_init(host);
	if (rc)
		goto err_bmdma;

	devres_remove_group(&pdev->dev, NULL);
	*r_host = host;
	return 0;

err_bmdma:
	/* This is necessary because PCI and iomap resources are
	 * merged and releasing the top group won't release the
	 * acquired resources if some of those have been acquired
	 * before entering this function.
	 */
	pcim_iounmap_regions(pdev, 0xf);
err_out:
	devres_release_group(&pdev->dev, NULL);
	return rc;
}
EXPORT_SYMBOL_GPL(ata_pci_sff_prepare_host);

/**
 *	ata_pci_sff_activate_host - start SFF host, request IRQ and register it
 *	@host: target SFF ATA host
 *	@irq_handler: irq_handler used when requesting IRQ(s)
 *	@sht: scsi_host_template to use when registering the host
 *
 *	This is the counterpart of ata_host_activate() for SFF ATA
 *	hosts.  This separate helper is necessary because SFF hosts
 *	use two separate interrupts in legacy mode.
 *
 *	LOCKING:
 *	Inherited from calling layer (may sleep).
 *
 *	RETURNS:
 *	0 on success, -errno otherwise.
 */
int ata_pci_sff_activate_host(struct ata_host *host,
			      irq_handler_t irq_handler,
			      struct scsi_host_template *sht)
{
	struct device *dev = host->dev;
	struct pci_dev *pdev = to_pci_dev(dev);
	const char *drv_name = dev_driver_string(host->dev);
	int legacy_mode = 0, rc;

	rc = ata_host_start(host);
	if (rc)
		return rc;

	if ((pdev->class >> 8) == PCI_CLASS_STORAGE_IDE) {
		u8 tmp8, mask;

		/* TODO: What if one channel is in native mode ... */
		pci_read_config_byte(pdev, PCI_CLASS_PROG, &tmp8);
		mask = (1 << 2) | (1 << 0);
		if ((tmp8 & mask) != mask)
			legacy_mode = 1;
#if defined(CONFIG_NO_ATA_LEGACY)
		/* Some platforms with PCI limits cannot address compat
		   port space. In that case we punt if their firmware has
		   left a device in compatibility mode */
		if (legacy_mode) {
			printk(KERN_ERR "ata: Compatibility mode ATA is not supported on this platform, skipping.\n");
			return -EOPNOTSUPP;
		}
#endif
	}

	if (!devres_open_group(dev, NULL, GFP_KERNEL))
		return -ENOMEM;

	if (!legacy_mode && pdev->irq) {
		rc = devm_request_irq(dev, pdev->irq, irq_handler,
				      IRQF_SHARED, drv_name, host);
		if (rc)
			goto out;

		ata_port_desc(host->ports[0], "irq %d", pdev->irq);
		ata_port_desc(host->ports[1], "irq %d", pdev->irq);
	} else if (legacy_mode) {
		if (!ata_port_is_dummy(host->ports[0])) {
			rc = devm_request_irq(dev, ATA_PRIMARY_IRQ(pdev),
					      irq_handler, IRQF_SHARED,
					      drv_name, host);
			if (rc)
				goto out;

			ata_port_desc(host->ports[0], "irq %d",
				      ATA_PRIMARY_IRQ(pdev));
		}

		if (!ata_port_is_dummy(host->ports[1])) {
			rc = devm_request_irq(dev, ATA_SECONDARY_IRQ(pdev),
					      irq_handler, IRQF_SHARED,
					      drv_name, host);
			if (rc)
				goto out;

			ata_port_desc(host->ports[1], "irq %d",
				      ATA_SECONDARY_IRQ(pdev));
		}
	}

	rc = ata_host_register(host, sht);
out:
	if (rc == 0)
		devres_remove_group(dev, NULL);
	else
		devres_release_group(dev, NULL);

	return rc;
}
EXPORT_SYMBOL_GPL(ata_pci_sff_activate_host);

/**
 *	ata_pci_sff_init_one - Initialize/register PCI IDE host controller
 *	@pdev: Controller to be initialized
 *	@ppi: array of port_info, must be enough for two ports
 *	@sht: scsi_host_template to use when registering the host
 *	@host_priv: host private_data
 *
 *	This is a helper function which can be called from a driver's
 *	xxx_init_one() probe function if the hardware uses traditional
 *	IDE taskfile registers.
 *
 *	This function calls pci_enable_device(), reserves its register
 *	regions, sets the dma mask, enables bus master mode, and calls
 *	ata_device_add()
 *
 *	ASSUMPTION:
 *	Nobody makes a single channel controller that appears solely as
 *	the secondary legacy port on PCI.
 *
 *	LOCKING:
 *	Inherited from PCI layer (may sleep).
 *
 *	RETURNS:
 *	Zero on success, negative on errno-based value on error.
 */
int ata_pci_sff_init_one(struct pci_dev *pdev,
			 const struct ata_port_info * const *ppi,
			 struct scsi_host_template *sht, void *host_priv)
{
	struct device *dev = &pdev->dev;
	const struct ata_port_info *pi = NULL;
	struct ata_host *host = NULL;
	int i, rc;

	DPRINTK("ENTER\n");

	/* look up the first valid port_info */
	for (i = 0; i < 2 && ppi[i]; i++) {
		if (ppi[i]->port_ops != &ata_dummy_port_ops) {
			pi = ppi[i];
			break;
		}
	}

	if (!pi) {
		dev_printk(KERN_ERR, &pdev->dev,
			   "no valid port_info specified\n");
		return -EINVAL;
	}

	if (!devres_open_group(dev, NULL, GFP_KERNEL))
		return -ENOMEM;

	rc = pcim_enable_device(pdev);
	if (rc)
		goto out;

	/* prepare and activate SFF host */
	rc = ata_pci_sff_prepare_host(pdev, ppi, &host);
	if (rc)
		goto out;
	host->private_data = host_priv;

	pci_set_master(pdev);
	rc = ata_pci_sff_activate_host(host, ata_sff_interrupt, sht);
out:
	if (rc == 0)
		devres_remove_group(&pdev->dev, NULL);
	else
		devres_release_group(&pdev->dev, NULL);

	return rc;
}
EXPORT_SYMBOL_GPL(ata_pci_sff_init_one);

#endif /* CONFIG_PCI */
