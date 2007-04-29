/*
 *    pata_qdi.c - QDI VLB ATA controllers
 *	(C) 2006 Red Hat <alan@redhat.com>
 *
 * This driver mostly exists as a proof of concept for non PCI devices under
 * libata. While the QDI6580 was 'neat' in 1993 it is no longer terribly
 * useful.
 *
 * Tuning code written from the documentation at
 * http://www.ryston.cz/petr/vlb/qd6500.html
 * http://www.ryston.cz/petr/vlb/qd6580.html
 *
 * Probe code based on drivers/ide/legacy/qd65xx.c
 * Rewritten from the work of Colten Edwards <pje120@cs.usask.ca> by
 * Samuel Thibault <samuel.thibault@fnac.net>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <scsi/scsi_host.h>
#include <linux/libata.h>
#include <linux/platform_device.h>

#define DRV_NAME "pata_qdi"
#define DRV_VERSION "0.3.0"

#define NR_HOST 4	/* Two 6580s */

struct qdi_data {
	unsigned long timing;
	u8 clock[2];
	u8 last;
	int fast;
	struct platform_device *platform_dev;

};

static struct ata_host *qdi_host[NR_HOST];
static struct qdi_data qdi_data[NR_HOST];
static int nr_qdi_host;

#ifdef MODULE
static int probe_qdi = 1;
#else
static int probe_qdi;
#endif

static void qdi6500_set_piomode(struct ata_port *ap, struct ata_device *adev)
{
	struct ata_timing t;
	struct qdi_data *qdi = ap->host->private_data;
	int active, recovery;
	u8 timing;

	/* Get the timing data in cycles */
	ata_timing_compute(adev, adev->pio_mode, &t, 30303, 1000);

	if (qdi->fast) {
		active = 8 - FIT(t.active, 1, 8);
		recovery = 18 - FIT(t.recover, 3, 18);
	} else {
		active = 9 - FIT(t.active, 2, 9);
		recovery = 15 - FIT(t.recover, 0, 15);
	}
	timing = (recovery << 4) | active | 0x08;

	qdi->clock[adev->devno] = timing;

	outb(timing, qdi->timing);
}

static void qdi6580_set_piomode(struct ata_port *ap, struct ata_device *adev)
{
	struct ata_timing t;
	struct qdi_data *qdi = ap->host->private_data;
	int active, recovery;
	u8 timing;

	/* Get the timing data in cycles */
	ata_timing_compute(adev, adev->pio_mode, &t, 30303, 1000);

	if (qdi->fast) {
		active = 8 - FIT(t.active, 1, 8);
		recovery = 18 - FIT(t.recover, 3, 18);
	} else {
		active = 9 - FIT(t.active, 2, 9);
		recovery = 15 - FIT(t.recover, 0, 15);
	}
	timing = (recovery << 4) | active | 0x08;

	qdi->clock[adev->devno] = timing;

	outb(timing, qdi->timing);

	/* Clear the FIFO */
	if (adev->class != ATA_DEV_ATA)
		outb(0x5F, (qdi->timing & 0xFFF0) + 3);
}

/**
 *	qdi_qc_issue_prot	-	command issue
 *	@qc: command pending
 *
 *	Called when the libata layer is about to issue a command. We wrap
 *	this interface so that we can load the correct ATA timings.
 */

static unsigned int qdi_qc_issue_prot(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	struct ata_device *adev = qc->dev;
	struct qdi_data *qdi = ap->host->private_data;

	if (qdi->clock[adev->devno] != qdi->last) {
		if (adev->pio_mode) {
			qdi->last = qdi->clock[adev->devno];
			outb(qdi->clock[adev->devno], qdi->timing);
		}
	}
	return ata_qc_issue_prot(qc);
}

static void qdi_data_xfer(struct ata_device *adev, unsigned char *buf, unsigned int buflen, int write_data)
{
	struct ata_port *ap = adev->ap;
	int slop = buflen & 3;

	if (ata_id_has_dword_io(adev->id)) {
		if (write_data)
			iowrite32_rep(ap->ioaddr.data_addr, buf, buflen >> 2);
		else
			ioread32_rep(ap->ioaddr.data_addr, buf, buflen >> 2);

		if (unlikely(slop)) {
			u32 pad;
			if (write_data) {
				memcpy(&pad, buf + buflen - slop, slop);
				pad = le32_to_cpu(pad);
				iowrite32(pad, ap->ioaddr.data_addr);
			} else {
				pad = ioread32(ap->ioaddr.data_addr);
				pad = cpu_to_le32(pad);
				memcpy(buf + buflen - slop, &pad, slop);
			}
		}
	} else
		ata_data_xfer(adev, buf, buflen, write_data);
}

static struct scsi_host_template qdi_sht = {
	.module			= THIS_MODULE,
	.name			= DRV_NAME,
	.ioctl			= ata_scsi_ioctl,
	.queuecommand		= ata_scsi_queuecmd,
	.can_queue		= ATA_DEF_QUEUE,
	.this_id		= ATA_SHT_THIS_ID,
	.sg_tablesize		= LIBATA_MAX_PRD,
	.cmd_per_lun		= ATA_SHT_CMD_PER_LUN,
	.emulated		= ATA_SHT_EMULATED,
	.use_clustering		= ATA_SHT_USE_CLUSTERING,
	.proc_name		= DRV_NAME,
	.dma_boundary		= ATA_DMA_BOUNDARY,
	.slave_configure	= ata_scsi_slave_config,
	.slave_destroy		= ata_scsi_slave_destroy,
	.bios_param		= ata_std_bios_param,
};

static struct ata_port_operations qdi6500_port_ops = {
	.port_disable	= ata_port_disable,
	.set_piomode	= qdi6500_set_piomode,

	.tf_load	= ata_tf_load,
	.tf_read	= ata_tf_read,
	.check_status 	= ata_check_status,
	.exec_command	= ata_exec_command,
	.dev_select 	= ata_std_dev_select,

	.freeze		= ata_bmdma_freeze,
	.thaw		= ata_bmdma_thaw,
	.error_handler	= ata_bmdma_error_handler,
	.post_internal_cmd = ata_bmdma_post_internal_cmd,
	.cable_detect	= ata_cable_40wire,

	.qc_prep 	= ata_qc_prep,
	.qc_issue	= qdi_qc_issue_prot,

	.data_xfer	= qdi_data_xfer,

	.irq_clear	= ata_bmdma_irq_clear,
	.irq_on		= ata_irq_on,
	.irq_ack	= ata_irq_ack,

	.port_start	= ata_port_start,
};

static struct ata_port_operations qdi6580_port_ops = {
	.port_disable	= ata_port_disable,
	.set_piomode	= qdi6580_set_piomode,

	.tf_load	= ata_tf_load,
	.tf_read	= ata_tf_read,
	.check_status 	= ata_check_status,
	.exec_command	= ata_exec_command,
	.dev_select 	= ata_std_dev_select,

	.freeze		= ata_bmdma_freeze,
	.thaw		= ata_bmdma_thaw,
	.error_handler	= ata_bmdma_error_handler,
	.post_internal_cmd = ata_bmdma_post_internal_cmd,
	.cable_detect	= ata_cable_40wire,

	.qc_prep 	= ata_qc_prep,
	.qc_issue	= qdi_qc_issue_prot,

	.data_xfer	= qdi_data_xfer,

	.irq_clear	= ata_bmdma_irq_clear,
	.irq_on		= ata_irq_on,
	.irq_ack	= ata_irq_ack,

	.port_start	= ata_port_start,
};

/**
 *	qdi_init_one		-	attach a qdi interface
 *	@type: Type to display
 *	@io: I/O port start
 *	@irq: interrupt line
 *	@fast: True if on a > 33Mhz VLB
 *
 *	Register an ISA bus IDE interface. Such interfaces are PIO and we
 *	assume do not support IRQ sharing.
 */

static __init int qdi_init_one(unsigned long port, int type, unsigned long io, int irq, int fast)
{
	struct platform_device *pdev;
	struct ata_host *host;
	struct ata_port *ap;
	void __iomem *io_addr, *ctl_addr;
	int ret;

	/*
	 *	Fill in a probe structure first of all
	 */

	pdev = platform_device_register_simple(DRV_NAME, nr_qdi_host, NULL, 0);
	if (IS_ERR(pdev))
		return PTR_ERR(pdev);

	ret = -ENOMEM;
	io_addr = devm_ioport_map(&pdev->dev, io, 8);
	ctl_addr = devm_ioport_map(&pdev->dev, io + 0x206, 1);
	if (!io_addr || !ctl_addr)
		goto fail;

	ret = -ENOMEM;
	host = ata_host_alloc(&pdev->dev, 1);
	if (!host)
		goto fail;
	ap = host->ports[0];

	if (type == 6580) {
		ap->ops = &qdi6580_port_ops;
		ap->pio_mask = 0x1F;
		ap->flags |= ATA_FLAG_SLAVE_POSS;
	} else {
		ap->ops = &qdi6500_port_ops;
		ap->pio_mask = 0x07;	/* Actually PIO3 !IORDY is possible */
		ap->flags = ATA_FLAG_SLAVE_POSS | ATA_FLAG_NO_IORDY;
	}

	ap->ioaddr.cmd_addr = io_addr;
	ap->ioaddr.altstatus_addr = ctl_addr;
	ap->ioaddr.ctl_addr = ctl_addr;
	ata_std_ports(&ap->ioaddr);

	/*
	 *	Hook in a private data structure per channel
	 */
	ap->private_data = &qdi_data[nr_qdi_host];

	qdi_data[nr_qdi_host].timing = port;
	qdi_data[nr_qdi_host].fast = fast;
	qdi_data[nr_qdi_host].platform_dev = pdev;

	printk(KERN_INFO DRV_NAME": qd%d at 0x%lx.\n", type, io);

	/* activate */
	ret = ata_host_activate(host, irq, ata_interrupt, 0, &qdi_sht);
	if (ret)
		goto fail;

	qdi_host[nr_qdi_host++] = dev_get_drvdata(&pdev->dev);
	return 0;

 fail:
	platform_device_unregister(pdev);
	return ret;
}

/**
 *	qdi_init		-	attach qdi interfaces
 *
 *	Attach qdi IDE interfaces by scanning the ports it may occupy.
 */

static __init int qdi_init(void)
{
	unsigned long flags;
	static const unsigned long qd_port[2] = { 0x30, 0xB0 };
	static const unsigned long ide_port[2] = { 0x170, 0x1F0 };
	static const int ide_irq[2] = { 14, 15 };

	int ct = 0;
	int i;

	if (probe_qdi == 0)
		return -ENODEV;

	/*
 	 *	Check each possible QD65xx base address
	 */

	for (i = 0; i < 2; i++) {
		unsigned long port = qd_port[i];
		u8 r, res;


		if (request_region(port, 2, "pata_qdi")) {
			/* Check for a card */
			local_irq_save(flags);
			r = inb_p(port);
			outb_p(0x19, port);
			res = inb_p(port);
			outb_p(r, port);
			local_irq_restore(flags);

			/* Fail */
			if (res == 0x19)
			{
				release_region(port, 2);
				continue;
			}

			/* Passes the presence test */
			r = inb_p(port + 1);	/* Check port agrees with port set */
			if ((r & 2) >> 1 != i) {
				release_region(port, 2);
				continue;
			}

			/* Check card type */
			if ((r & 0xF0) == 0xC0) {
				/* QD6500: single channel */
				if (r & 8) {
					/* Disabled ? */
					release_region(port, 2);
					continue;
				}
				if (qdi_init_one(port, 6500, ide_port[r & 0x01], ide_irq[r & 0x01], r & 0x04) == 0)
					ct++;
			}
			if (((r & 0xF0) == 0xA0) || (r & 0xF0) == 0x50) {
				/* QD6580: dual channel */
				if (!request_region(port + 2 , 2, "pata_qdi"))
				{
					release_region(port, 2);
					continue;
				}
				res = inb(port + 3);
				if (res & 1) {
					/* Single channel mode */
					if (qdi_init_one(port, 6580, ide_port[r & 0x01], ide_irq[r & 0x01], r & 0x04))
						ct++;
				} else {
					/* Dual channel mode */
					if (qdi_init_one(port, 6580, 0x1F0, 14, r & 0x04) == 0)
						ct++;
					if (qdi_init_one(port + 2, 6580, 0x170, 15, r & 0x04) == 0)
						ct++;
				}
			}
		}
	}
	if (ct != 0)
		return 0;
	return -ENODEV;
}

static __exit void qdi_exit(void)
{
	int i;

	for (i = 0; i < nr_qdi_host; i++) {
		ata_host_detach(qdi_host[i]);
		/* Free the control resource. The 6580 dual channel has the resources
		 * claimed as a pair of 2 byte resources so we need no special cases...
		 */
		release_region(qdi_data[i].timing, 2);
		platform_device_unregister(qdi_data[i].platform_dev);
	}
}

MODULE_AUTHOR("Alan Cox");
MODULE_DESCRIPTION("low-level driver for qdi ATA");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);

module_init(qdi_init);
module_exit(qdi_exit);

module_param(probe_qdi, int, 0);

