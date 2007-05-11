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

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/libata.h>

#include "libata.h"

/**
 *	ata_irq_on - Enable interrupts on a port.
 *	@ap: Port on which interrupts are enabled.
 *
 *	Enable interrupts on a legacy IDE device using MMIO or PIO,
 *	wait for idle, clear any pending interrupts.
 *
 *	LOCKING:
 *	Inherited from caller.
 */
u8 ata_irq_on(struct ata_port *ap)
{
	struct ata_ioports *ioaddr = &ap->ioaddr;
	u8 tmp;

	ap->ctl &= ~ATA_NIEN;
	ap->last_ctl = ap->ctl;

	iowrite8(ap->ctl, ioaddr->ctl_addr);
	tmp = ata_wait_idle(ap);

	ap->ops->irq_clear(ap);

	return tmp;
}

u8 ata_dummy_irq_on (struct ata_port *ap) 	{ return 0; }

/**
 *	ata_irq_ack - Acknowledge a device interrupt.
 *	@ap: Port on which interrupts are enabled.
 *
 *	Wait up to 10 ms for legacy IDE device to become idle (BUSY
 *	or BUSY+DRQ clear).  Obtain dma status and port status from
 *	device.  Clear the interrupt.  Return port status.
 *
 *	LOCKING:
 */

u8 ata_irq_ack(struct ata_port *ap, unsigned int chk_drq)
{
	unsigned int bits = chk_drq ? ATA_BUSY | ATA_DRQ : ATA_BUSY;
	u8 host_stat, post_stat, status;

	status = ata_busy_wait(ap, bits, 1000);
	if (status & bits)
		if (ata_msg_err(ap))
			printk(KERN_ERR "abnormal status 0x%X\n", status);

	/* get controller status; clear intr, err bits */
	host_stat = ioread8(ap->ioaddr.bmdma_addr + ATA_DMA_STATUS);
	iowrite8(host_stat | ATA_DMA_INTR | ATA_DMA_ERR,
		 ap->ioaddr.bmdma_addr + ATA_DMA_STATUS);

	post_stat = ioread8(ap->ioaddr.bmdma_addr + ATA_DMA_STATUS);

	if (ata_msg_intr(ap))
		printk(KERN_INFO "%s: irq ack: host_stat 0x%X, new host_stat 0x%X, drv_stat 0x%X\n",
			__FUNCTION__,
			host_stat, post_stat, status);

	return status;
}

u8 ata_dummy_irq_ack(struct ata_port *ap, unsigned int chk_drq) { return 0; }

/**
 *	ata_tf_load - send taskfile registers to host controller
 *	@ap: Port to which output is sent
 *	@tf: ATA taskfile register set
 *
 *	Outputs ATA taskfile to standard ATA host controller.
 *
 *	LOCKING:
 *	Inherited from caller.
 */

void ata_tf_load(struct ata_port *ap, const struct ata_taskfile *tf)
{
	struct ata_ioports *ioaddr = &ap->ioaddr;
	unsigned int is_addr = tf->flags & ATA_TFLAG_ISADDR;

	if (tf->ctl != ap->last_ctl) {
		iowrite8(tf->ctl, ioaddr->ctl_addr);
		ap->last_ctl = tf->ctl;
		ata_wait_idle(ap);
	}

	if (is_addr && (tf->flags & ATA_TFLAG_LBA48)) {
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

/**
 *	ata_exec_command - issue ATA command to host controller
 *	@ap: port to which command is being issued
 *	@tf: ATA taskfile register set
 *
 *	Issues ATA command, with proper synchronization with interrupt
 *	handler / other threads.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host lock)
 */
void ata_exec_command(struct ata_port *ap, const struct ata_taskfile *tf)
{
	DPRINTK("ata%u: cmd 0x%X\n", ap->print_id, tf->command);

	iowrite8(tf->command, ap->ioaddr.command_addr);
	ata_pause(ap);
}

/**
 *	ata_tf_read - input device's ATA taskfile shadow registers
 *	@ap: Port from which input is read
 *	@tf: ATA taskfile register set for storing input
 *
 *	Reads ATA taskfile registers for currently-selected device
 *	into @tf.
 *
 *	LOCKING:
 *	Inherited from caller.
 */
void ata_tf_read(struct ata_port *ap, struct ata_taskfile *tf)
{
	struct ata_ioports *ioaddr = &ap->ioaddr;

	tf->command = ata_check_status(ap);
	tf->feature = ioread8(ioaddr->error_addr);
	tf->nsect = ioread8(ioaddr->nsect_addr);
	tf->lbal = ioread8(ioaddr->lbal_addr);
	tf->lbam = ioread8(ioaddr->lbam_addr);
	tf->lbah = ioread8(ioaddr->lbah_addr);
	tf->device = ioread8(ioaddr->device_addr);

	if (tf->flags & ATA_TFLAG_LBA48) {
		iowrite8(tf->ctl | ATA_HOB, ioaddr->ctl_addr);
		tf->hob_feature = ioread8(ioaddr->error_addr);
		tf->hob_nsect = ioread8(ioaddr->nsect_addr);
		tf->hob_lbal = ioread8(ioaddr->lbal_addr);
		tf->hob_lbam = ioread8(ioaddr->lbam_addr);
		tf->hob_lbah = ioread8(ioaddr->lbah_addr);
	}
}

/**
 *	ata_check_status - Read device status reg & clear interrupt
 *	@ap: port where the device is
 *
 *	Reads ATA taskfile status register for currently-selected device
 *	and return its value. This also clears pending interrupts
 *      from this device
 *
 *	LOCKING:
 *	Inherited from caller.
 */
u8 ata_check_status(struct ata_port *ap)
{
	return ioread8(ap->ioaddr.status_addr);
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

	return ioread8(ap->ioaddr.altstatus_addr);
}

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
	ap->ops->exec_command(ap, &qc->tf);
}

/**
 *	ata_bmdma_start - Start a PCI IDE BMDMA transaction
 *	@qc: Info associated with this ATA transaction.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host lock)
 */
void ata_bmdma_start (struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	u8 dmactl;

	/* start host DMA transaction */
	dmactl = ioread8(ap->ioaddr.bmdma_addr + ATA_DMA_CMD);
	iowrite8(dmactl | ATA_DMA_START, ap->ioaddr.bmdma_addr + ATA_DMA_CMD);

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
 *	ata_bmdma_irq_clear - Clear PCI IDE BMDMA interrupt.
 *	@ap: Port associated with this ATA transaction.
 *
 *	Clear interrupt and error flags in DMA status register.
 *
 *	May be used as the irq_clear() entry in ata_port_operations.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host lock)
 */
void ata_bmdma_irq_clear(struct ata_port *ap)
{
	void __iomem *mmio = ap->ioaddr.bmdma_addr;

	if (!mmio)
		return;

	iowrite8(ioread8(mmio + ATA_DMA_STATUS), mmio + ATA_DMA_STATUS);
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
 *	spin_lock_irqsave(host lock)
 */
u8 ata_bmdma_status(struct ata_port *ap)
{
	return ioread8(ap->ioaddr.bmdma_addr + ATA_DMA_STATUS);
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

	iowrite8(ap->ctl, ioaddr->ctl_addr);

	/* Under certain circumstances, some controllers raise IRQ on
	 * ATA_NIEN manipulation.  Also, many controllers fail to mask
	 * previously pending IRQ on ATA_NIEN assertion.  Clear it.
	 */
	ata_chk_status(ap);

	ap->ops->irq_clear(ap);
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
	ap->ops->irq_on(ap);
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
	struct ata_queued_cmd *qc;
	unsigned long flags;
	int thaw = 0;

	qc = __ata_qc_from_tag(ap, ap->active_tag);
	if (qc && !(qc->flags & ATA_QCFLAG_FAILED))
		qc = NULL;

	/* reset PIO HSM and stop DMA engine */
	spin_lock_irqsave(ap->lock, flags);

	ap->hsm_task_state = HSM_ST_IDLE;

	if (qc && (qc->tf.protocol == ATA_PROT_DMA ||
		   qc->tf.protocol == ATA_PROT_ATAPI_DMA)) {
		u8 host_stat;

		host_stat = ap->ops->bmdma_status(ap);

		/* BMDMA controllers indicate host bus error by
		 * setting DMA_ERR bit and timing out.  As it wasn't
		 * really a timeout event, adjust error mask and
		 * cancel frozen state.
		 */
		if (qc->err_mask == AC_ERR_TIMEOUT && (host_stat & ATA_DMA_ERR)) {
			qc->err_mask = AC_ERR_HOST_BUS;
			thaw = 1;
		}

		ap->ops->bmdma_stop(qc);
	}

	ata_altstatus(ap);
	ata_chk_status(ap);
	ap->ops->irq_clear(ap);

	spin_unlock_irqrestore(ap->lock, flags);

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
	if (qc->ap->ioaddr.bmdma_addr)
		ata_bmdma_stop(qc);
}

#ifdef CONFIG_PCI

static int ata_resources_present(struct pci_dev *pdev, int port)
{
	int i;

	/* Check the PCI resources for this channel are enabled */
	port = port * 2;
	for (i = 0; i < 2; i ++) {
		if (pci_resource_start(pdev, port + i) == 0 ||
		    pci_resource_len(pdev, port + i) == 0)
			return 0;
	}
	return 1;
}

/**
 *	ata_pci_init_bmdma - acquire PCI BMDMA resources and init ATA host
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
int ata_pci_init_bmdma(struct ata_host *host)
{
	struct device *gdev = host->dev;
	struct pci_dev *pdev = to_pci_dev(gdev);
	int i, rc;

	/* TODO: If we get no DMA mask we should fall back to PIO */
	rc = pci_set_dma_mask(pdev, ATA_DMA_MASK);
	if (rc)
		return rc;
	rc = pci_set_consistent_dma_mask(pdev, ATA_DMA_MASK);
	if (rc)
		return rc;

	/* request and iomap DMA region */
	rc = pcim_iomap_regions(pdev, 1 << 4, DRV_NAME);
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
	}

	return 0;
}

/**
 *	ata_pci_init_native_host - acquire native ATA resources and init host
 *	@host: target ATA host
 *
 *	Acquire native PCI ATA resources for @host and initialize the
 *	first two ports of @host accordingly.  Ports marked dummy are
 *	skipped and allocation failure makes the port dummy.
 *
 *	LOCKING:
 *	Inherited from calling layer (may sleep).
 *
 *	RETURNS:
 *	0 if at least one port is initialized, -ENODEV if no port is
 *	available.
 */
int ata_pci_init_native_host(struct ata_host *host)
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

		rc = pcim_iomap_regions(pdev, 0x3 << base, DRV_NAME);
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
		ata_std_ports(&ap->ioaddr);

		mask |= 1 << i;
	}

	if (!mask) {
		dev_printk(KERN_ERR, gdev, "no available native port\n");
		return -ENODEV;
	}

	return 0;
}

/**
 *	ata_pci_prepare_native_host - helper to prepare native PCI ATA host
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
int ata_pci_prepare_native_host(struct pci_dev *pdev,
				const struct ata_port_info * const * ppi,
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

	rc = ata_pci_init_native_host(host);
	if (rc)
		goto err_out;

	/* init DMA related stuff */
	rc = ata_pci_init_bmdma(host);
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

struct ata_legacy_devres {
	unsigned int	mask;
	unsigned long	cmd_port[2];
	void __iomem *	cmd_addr[2];
	void __iomem *	ctl_addr[2];
	unsigned int	irq[2];
	void *		irq_dev_id[2];
};

static void ata_legacy_free_irqs(struct ata_legacy_devres *legacy_dr)
{
	int i;

	for (i = 0; i < 2; i++) {
		if (!legacy_dr->irq[i])
			continue;

		free_irq(legacy_dr->irq[i], legacy_dr->irq_dev_id[i]);
		legacy_dr->irq[i] = 0;
		legacy_dr->irq_dev_id[i] = NULL;
	}
}

static void ata_legacy_release(struct device *gdev, void *res)
{
	struct ata_legacy_devres *this = res;
	int i;

	ata_legacy_free_irqs(this);

	for (i = 0; i < 2; i++) {
		if (this->cmd_addr[i])
			ioport_unmap(this->cmd_addr[i]);
		if (this->ctl_addr[i])
			ioport_unmap(this->ctl_addr[i]);
		if (this->cmd_port[i])
			release_region(this->cmd_port[i], 8);
	}
}

static int ata_init_legacy_port(struct ata_port *ap,
				struct ata_legacy_devres *legacy_dr)
{
	struct ata_host *host = ap->host;
	int port_no = ap->port_no;
	unsigned long cmd_port, ctl_port;

	if (port_no == 0) {
		cmd_port = ATA_PRIMARY_CMD;
		ctl_port = ATA_PRIMARY_CTL;
	} else {
		cmd_port = ATA_SECONDARY_CMD;
		ctl_port = ATA_SECONDARY_CTL;
	}

	/* request cmd_port */
	if (request_region(cmd_port, 8, "libata"))
		legacy_dr->cmd_port[port_no] = cmd_port;
	else {
		dev_printk(KERN_WARNING, host->dev,
			   "0x%0lX IDE port busy\n", cmd_port);
		return -EBUSY;
	}

	/* iomap cmd and ctl ports */
	legacy_dr->cmd_addr[port_no] = ioport_map(cmd_port, 8);
	legacy_dr->ctl_addr[port_no] = ioport_map(ctl_port, 1);
	if (!legacy_dr->cmd_addr[port_no] || !legacy_dr->ctl_addr[port_no]) {
		dev_printk(KERN_WARNING, host->dev,
			   "failed to map cmd/ctl ports\n");
		return -ENOMEM;
	}

	/* init IO addresses */
	ap->ioaddr.cmd_addr = legacy_dr->cmd_addr[port_no];
	ap->ioaddr.altstatus_addr = legacy_dr->ctl_addr[port_no];
	ap->ioaddr.ctl_addr = legacy_dr->ctl_addr[port_no];
	ata_std_ports(&ap->ioaddr);

	return 0;
}

/**
 *	ata_init_legacy_host - acquire legacy ATA resources and init ATA host
 *	@host: target ATA host
 *	@was_busy: out parameter, indicates whether any port was busy
 *
 *	Acquire legacy ATA resources for the first two ports of @host
 *	and initialize it accordingly.  Ports marked dummy are skipped
 *	and resource acquistion failure makes the port dummy.
 *
 *	LOCKING:
 *	Inherited from calling layer (may sleep).
 *
 *	RETURNS:
 *	0 if at least one port is initialized, -ENODEV if no port is
 *	available.
 */
static int ata_init_legacy_host(struct ata_host *host, int *was_busy)
{
	struct device *gdev = host->dev;
	struct ata_legacy_devres *legacy_dr;
	int i, rc;

	if (!devres_open_group(gdev, NULL, GFP_KERNEL))
		return -ENOMEM;

	rc = -ENOMEM;
	legacy_dr = devres_alloc(ata_legacy_release, sizeof(*legacy_dr),
				 GFP_KERNEL);
	if (!legacy_dr)
		goto err_out;
	devres_add(gdev, legacy_dr);

	for (i = 0; i < 2; i++) {
		if (ata_port_is_dummy(host->ports[i]))
			continue;

		rc = ata_init_legacy_port(host->ports[i], legacy_dr);
		if (rc == 0)
			legacy_dr->mask |= 1 << i;
		else {
			if (rc == -EBUSY)
				(*was_busy)++;
			host->ports[i]->ops = &ata_dummy_port_ops;
		}
	}

	if (!legacy_dr->mask) {
		dev_printk(KERN_ERR, gdev, "no available legacy port\n");
		return -ENODEV;
	}

	devres_remove_group(gdev, NULL);
	return 0;

 err_out:
	devres_release_group(gdev, NULL);
	return rc;
}

/**
 *	ata_request_legacy_irqs - request legacy ATA IRQs
 *	@host: target ATA host
 *	@handler: array of IRQ handlers
 *	@irq_flags: array of IRQ flags
 *	@dev_id: array of IRQ dev_ids
 *
 *	Request legacy IRQs for non-dummy legacy ports in @host.  All
 *	IRQ parameters are passed as array to allow ports to have
 *	separate IRQ handlers.
 *
 *	LOCKING:
 *	Inherited from calling layer (may sleep).
 *
 *	RETURNS:
 *	0 on success, -errno otherwise.
 */
static int ata_request_legacy_irqs(struct ata_host *host,
				   irq_handler_t const *handler,
				   const unsigned int *irq_flags,
				   void * const *dev_id)
{
	struct device *gdev = host->dev;
	struct ata_legacy_devres *legacy_dr;
	int i, rc;

	legacy_dr = devres_find(host->dev, ata_legacy_release, NULL, NULL);
	BUG_ON(!legacy_dr);

	for (i = 0; i < 2; i++) {
		unsigned int irq;

		/* FIXME: ATA_*_IRQ() should take generic device not pci_dev */
		if (i == 0)
			irq = ATA_PRIMARY_IRQ(to_pci_dev(gdev));
		else
			irq = ATA_SECONDARY_IRQ(to_pci_dev(gdev));

		if (!(legacy_dr->mask & (1 << i)))
			continue;

		if (!handler[i]) {
			dev_printk(KERN_ERR, gdev,
				   "NULL handler specified for port %d\n", i);
			rc = -EINVAL;
			goto err_out;
		}

		rc = request_irq(irq, handler[i], irq_flags[i], DRV_NAME,
				 dev_id[i]);
		if (rc) {
			dev_printk(KERN_ERR, gdev,
				"irq %u request failed (errno=%d)\n", irq, rc);
			goto err_out;
		}

		/* record irq allocation in legacy_dr */
		legacy_dr->irq[i] = irq;
		legacy_dr->irq_dev_id[i] = dev_id[i];

		/* only used to print info */
		if (i == 0)
			host->irq = irq;
		else
			host->irq2 = irq;
	}

	return 0;

 err_out:
	ata_legacy_free_irqs(legacy_dr);
	return rc;
}

/**
 *	ata_pci_init_one - Initialize/register PCI IDE host controller
 *	@pdev: Controller to be initialized
 *	@ppi: array of port_info, must be enough for two ports
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
int ata_pci_init_one(struct pci_dev *pdev,
		     const struct ata_port_info * const * ppi)
{
	struct device *dev = &pdev->dev;
	const struct ata_port_info *pi = NULL;
	struct ata_host *host = NULL;
	u8 mask;
	int legacy_mode = 0;
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

	/* FIXME: Really for ATA it isn't safe because the device may be
	   multi-purpose and we want to leave it alone if it was already
	   enabled. Secondly for shared use as Arjan says we want refcounting

	   Checking dev->is_enabled is insufficient as this is not set at
	   boot for the primary video which is BIOS enabled
	  */

	rc = pcim_enable_device(pdev);
	if (rc)
		goto err_out;

	if ((pdev->class >> 8) == PCI_CLASS_STORAGE_IDE) {
		u8 tmp8;

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
			rc = -EOPNOTSUPP;
			goto err_out;
		}
#endif
	}

	/* alloc and init host */
	host = ata_host_alloc_pinfo(dev, ppi, 2);
	if (!host) {
		dev_printk(KERN_ERR, &pdev->dev,
			   "failed to allocate ATA host\n");
		rc = -ENOMEM;
		goto err_out;
	}

	if (!legacy_mode) {
		rc = ata_pci_init_native_host(host);
		if (rc)
			goto err_out;
	} else {
		int was_busy = 0;

		rc = ata_init_legacy_host(host, &was_busy);
		if (was_busy)
			pcim_pin_device(pdev);
		if (rc)
			goto err_out;

		/* request respective PCI regions, may fail */
		rc = pci_request_region(pdev, 1, DRV_NAME);
		rc = pci_request_region(pdev, 3, DRV_NAME);
	}

	/* init BMDMA, may fail */
	ata_pci_init_bmdma(host);
	pci_set_master(pdev);

	/* start host and request IRQ */
	rc = ata_host_start(host);
	if (rc)
		goto err_out;

	if (!legacy_mode)
		rc = devm_request_irq(dev, pdev->irq, pi->port_ops->irq_handler,
				      IRQF_SHARED, DRV_NAME, host);
	else {
		irq_handler_t handler[2] = { host->ops->irq_handler,
					     host->ops->irq_handler };
		unsigned int irq_flags[2] = { IRQF_SHARED, IRQF_SHARED };
		void *dev_id[2] = { host, host };

		rc = ata_request_legacy_irqs(host, handler, irq_flags, dev_id);
	}
	if (rc)
		goto err_out;

	/* register */
	rc = ata_host_register(host, pi->sht);
	if (rc)
		goto err_out;

	devres_remove_group(dev, NULL);
	return 0;

err_out:
	devres_release_group(dev, NULL);
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

unsigned long ata_pci_default_filter(struct ata_device *adev, unsigned long xfer_mask)
{
	/* Filter out DMA modes if the device has been configured by
	   the BIOS as PIO only */

	if (adev->ap->ioaddr.bmdma_addr == 0)
		xfer_mask &= ~(ATA_MASK_MWDMA | ATA_MASK_UDMA);
	return xfer_mask;
}

#endif /* CONFIG_PCI */

