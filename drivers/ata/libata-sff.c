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
	struct ata_probe_ent *probe_ent;
	int i, p = 0;
	void __iomem * const *iomap;

	/* iomap BARs */
	for (i = 0; i < 4; i++) {
		if (pcim_iomap(pdev, i, 0) == NULL) {
			dev_printk(KERN_ERR, &pdev->dev,
				   "failed to iomap PCI BAR %d\n", i);
			return NULL;
		}
	}

	pcim_iomap(pdev, 4, 0); /* may fail */
	iomap = pcim_iomap_table(pdev);

	/* alloc and init probe_ent */
	probe_ent = ata_probe_ent_alloc(pci_dev_to_dev(pdev), port[0]);
	if (!probe_ent)
		return NULL;

	probe_ent->irq = pdev->irq;
	probe_ent->irq_flags = IRQF_SHARED;

	/* Discard disabled ports. Some controllers show their
	   unused channels this way */
	if (ata_resources_present(pdev, 0) == 0)
		ports &= ~ATA_PORT_PRIMARY;
	if (ata_resources_present(pdev, 1) == 0)
		ports &= ~ATA_PORT_SECONDARY;

	if (ports & ATA_PORT_PRIMARY) {
		probe_ent->port[p].cmd_addr = iomap[0];
		probe_ent->port[p].altstatus_addr =
		probe_ent->port[p].ctl_addr = (void __iomem *)
			((unsigned long)iomap[1] | ATA_PCI_CTL_OFS);
		if (iomap[4]) {
			if ((!(port[p]->flags & ATA_FLAG_IGN_SIMPLEX)) &&
			    (ioread8(iomap[4] + 2) & 0x80))
				probe_ent->_host_flags |= ATA_HOST_SIMPLEX;
			probe_ent->port[p].bmdma_addr = iomap[4];
		}
		ata_std_ports(&probe_ent->port[p]);
		p++;
	}

	if (ports & ATA_PORT_SECONDARY) {
		probe_ent->port[p].cmd_addr = iomap[2];
		probe_ent->port[p].altstatus_addr =
		probe_ent->port[p].ctl_addr = (void __iomem *)
			((unsigned long)iomap[3] | ATA_PCI_CTL_OFS);
		if (iomap[4]) {
			if ((!(port[p]->flags & ATA_FLAG_IGN_SIMPLEX)) &&
			    (ioread8(iomap[4] + 10) & 0x80))
				probe_ent->_host_flags |= ATA_HOST_SIMPLEX;
			probe_ent->port[p].bmdma_addr = iomap[4] + 8;
		}
		ata_std_ports(&probe_ent->port[p]);
		probe_ent->pinfo2 = port[1];
		p++;
	}

	probe_ent->n_ports = p;
	return probe_ent;
}

static struct ata_probe_ent *ata_pci_init_legacy_port(struct pci_dev *pdev,
				struct ata_port_info **port, int port_mask)
{
	struct ata_probe_ent *probe_ent;
	void __iomem *iomap[5] = { }, *bmdma;

	if (port_mask & ATA_PORT_PRIMARY) {
		iomap[0] = devm_ioport_map(&pdev->dev, ATA_PRIMARY_CMD, 8);
		iomap[1] = devm_ioport_map(&pdev->dev, ATA_PRIMARY_CTL, 1);
		if (!iomap[0] || !iomap[1])
			return NULL;
	}

	if (port_mask & ATA_PORT_SECONDARY) {
		iomap[2] = devm_ioport_map(&pdev->dev, ATA_SECONDARY_CMD, 8);
		iomap[3] = devm_ioport_map(&pdev->dev, ATA_SECONDARY_CTL, 1);
		if (!iomap[2] || !iomap[3])
			return NULL;
	}

	bmdma = pcim_iomap(pdev, 4, 16); /* may fail */

	/* alloc and init probe_ent */
	probe_ent = ata_probe_ent_alloc(pci_dev_to_dev(pdev), port[0]);
	if (!probe_ent)
		return NULL;

	probe_ent->n_ports = 2;
	probe_ent->irq_flags = IRQF_SHARED;

	if (port_mask & ATA_PORT_PRIMARY) {
		probe_ent->irq = ATA_PRIMARY_IRQ(pdev);
		probe_ent->port[0].cmd_addr = iomap[0];
		probe_ent->port[0].altstatus_addr =
		probe_ent->port[0].ctl_addr = iomap[1];
		if (bmdma) {
			probe_ent->port[0].bmdma_addr = bmdma;
			if ((!(port[0]->flags & ATA_FLAG_IGN_SIMPLEX)) &&
			    (ioread8(bmdma + 2) & 0x80))
				probe_ent->_host_flags |= ATA_HOST_SIMPLEX;
		}
		ata_std_ports(&probe_ent->port[0]);
	} else
		probe_ent->dummy_port_mask |= ATA_PORT_PRIMARY;

	if (port_mask & ATA_PORT_SECONDARY) {
		if (probe_ent->irq)
			probe_ent->irq2 = ATA_SECONDARY_IRQ(pdev);
		else
			probe_ent->irq = ATA_SECONDARY_IRQ(pdev);
		probe_ent->port[1].cmd_addr = iomap[2];
		probe_ent->port[1].altstatus_addr =
		probe_ent->port[1].ctl_addr = iomap[3];
		if (bmdma) {
			probe_ent->port[1].bmdma_addr = bmdma + 8;
			if ((!(port[1]->flags & ATA_FLAG_IGN_SIMPLEX)) &&
			    (ioread8(bmdma + 10) & 0x80))
				probe_ent->_host_flags |= ATA_HOST_SIMPLEX;
		}
		ata_std_ports(&probe_ent->port[1]);

		/* FIXME: could be pointing to stack area; must copy */
		probe_ent->pinfo2 = port[1];
	} else
		probe_ent->dummy_port_mask |= ATA_PORT_SECONDARY;

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

int ata_pci_init_one (struct pci_dev *pdev, struct ata_port_info **port_info,
		      unsigned int n_ports)
{
	struct device *dev = &pdev->dev;
	struct ata_probe_ent *probe_ent = NULL;
	struct ata_port_info *port[2];
	u8 mask;
	unsigned int legacy_mode = 0;
	int rc;

	DPRINTK("ENTER\n");

	if (!devres_open_group(dev, NULL, GFP_KERNEL))
		return -ENOMEM;

	BUG_ON(n_ports < 1 || n_ports > 2);

	port[0] = port_info[0];
	if (n_ports > 1)
		port[1] = port_info[1];
	else
		port[1] = port[0];

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
			legacy_mode = (1 << 3);
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

	if (!legacy_mode) {
		rc = pci_request_regions(pdev, DRV_NAME);
		if (rc) {
			pcim_pin_device(pdev);
			goto err_out;
		}
	} else {
		/* Deal with combined mode hack. This side of the logic all
		   goes away once the combined mode hack is killed in 2.6.21 */
		if (!devm_request_region(dev, ATA_PRIMARY_CMD, 8, "libata")) {
			struct resource *conflict, res;
			res.start = ATA_PRIMARY_CMD;
			res.end = ATA_PRIMARY_CMD + 8 - 1;
			conflict = ____request_resource(&ioport_resource, &res);
			while (conflict->child)
				conflict = ____request_resource(conflict, &res);
			if (!strcmp(conflict->name, "libata"))
				legacy_mode |= ATA_PORT_PRIMARY;
			else {
				pcim_pin_device(pdev);
				printk(KERN_WARNING "ata: 0x%0X IDE port busy\n" \
						    "ata: conflict with %s\n",
						    ATA_PRIMARY_CMD,
						    conflict->name);
			}
		} else
			legacy_mode |= ATA_PORT_PRIMARY;

		if (!devm_request_region(dev, ATA_SECONDARY_CMD, 8, "libata")) {
			struct resource *conflict, res;
			res.start = ATA_SECONDARY_CMD;
			res.end = ATA_SECONDARY_CMD + 8 - 1;
			conflict = ____request_resource(&ioport_resource, &res);
			while (conflict->child)
				conflict = ____request_resource(conflict, &res);
			if (!strcmp(conflict->name, "libata"))
				legacy_mode |= ATA_PORT_SECONDARY;
			else {
				pcim_pin_device(pdev);
				printk(KERN_WARNING "ata: 0x%X IDE port busy\n" \
						    "ata: conflict with %s\n",
						    ATA_SECONDARY_CMD,
						    conflict->name);
			}
		} else
			legacy_mode |= ATA_PORT_SECONDARY;

		if (legacy_mode & ATA_PORT_PRIMARY)
			pci_request_region(pdev, 1, DRV_NAME);
		if (legacy_mode & ATA_PORT_SECONDARY)
			pci_request_region(pdev, 3, DRV_NAME);
		/* If there is a DMA resource, allocate it */
		pci_request_region(pdev, 4, DRV_NAME);
	}

	/* we have legacy mode, but all ports are unavailable */
	if (legacy_mode == (1 << 3)) {
		rc = -EBUSY;
		goto err_out;
	}

	/* TODO: If we get no DMA mask we should fall back to PIO */
	rc = pci_set_dma_mask(pdev, ATA_DMA_MASK);
	if (rc)
		goto err_out;
	rc = pci_set_consistent_dma_mask(pdev, ATA_DMA_MASK);
	if (rc)
		goto err_out;

	if (legacy_mode) {
		probe_ent = ata_pci_init_legacy_port(pdev, port, legacy_mode);
	} else {
		if (n_ports == 2)
			probe_ent = ata_pci_init_native_mode(pdev, port, ATA_PORT_PRIMARY | ATA_PORT_SECONDARY);
		else
			probe_ent = ata_pci_init_native_mode(pdev, port, ATA_PORT_PRIMARY);
	}
	if (!probe_ent) {
		rc = -ENOMEM;
		goto err_out;
	}

	pci_set_master(pdev);

	if (!ata_device_add(probe_ent)) {
		rc = -ENODEV;
		goto err_out;
	}

	devm_kfree(dev, probe_ent);
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

unsigned long ata_pci_default_filter(const struct ata_port *ap, struct ata_device *adev, unsigned long xfer_mask)
{
	/* Filter out DMA modes if the device has been configured by
	   the BIOS as PIO only */

	if (ap->ioaddr.bmdma_addr == 0)
		xfer_mask &= ~(ATA_MASK_MWDMA | ATA_MASK_UDMA);
	return xfer_mask;
}

#endif /* CONFIG_PCI */

