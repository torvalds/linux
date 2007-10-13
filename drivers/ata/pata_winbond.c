/*
 *    pata_winbond.c - Winbond VLB ATA controllers
 *	(C) 2006 Red Hat <alan@redhat.com>
 *
 *    Support for the Winbond 83759A when operating in advanced mode.
 *    Multichip mode is not currently supported.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <scsi/scsi_host.h>
#include <linux/libata.h>
#include <linux/platform_device.h>

#define DRV_NAME "pata_winbond"
#define DRV_VERSION "0.0.3"

#define NR_HOST 4	/* Two winbond controllers, two channels each */

struct winbond_data {
	unsigned long config;
	struct platform_device *platform_dev;
};

static struct ata_host *winbond_host[NR_HOST];
static struct winbond_data winbond_data[NR_HOST];
static int nr_winbond_host;

#ifdef MODULE
static int probe_winbond = 1;
#else
static int probe_winbond;
#endif

static DEFINE_SPINLOCK(winbond_lock);

static void winbond_writecfg(unsigned long port, u8 reg, u8 val)
{
	unsigned long flags;
	spin_lock_irqsave(&winbond_lock, flags);
	outb(reg, port + 0x01);
	outb(val, port + 0x02);
	spin_unlock_irqrestore(&winbond_lock, flags);
}

static u8 winbond_readcfg(unsigned long port, u8 reg)
{
	u8 val;

	unsigned long flags;
	spin_lock_irqsave(&winbond_lock, flags);
	outb(reg, port + 0x01);
	val = inb(port + 0x02);
	spin_unlock_irqrestore(&winbond_lock, flags);

	return val;
}

static void winbond_set_piomode(struct ata_port *ap, struct ata_device *adev)
{
	struct ata_timing t;
	struct winbond_data *winbond = ap->host->private_data;
	int active, recovery;
	u8 reg;
	int timing = 0x88 + (ap->port_no * 4) + (adev->devno * 2);

	reg = winbond_readcfg(winbond->config, 0x81);

	/* Get the timing data in cycles */
	if (reg & 0x40)		/* Fast VLB bus, assume 50MHz */
		ata_timing_compute(adev, adev->pio_mode, &t, 20000, 1000);
	else
		ata_timing_compute(adev, adev->pio_mode, &t, 30303, 1000);

	active = (FIT(t.active, 3, 17) - 1) & 0x0F;
	recovery = (FIT(t.recover, 1, 15) + 1) & 0x0F;
	timing = (active << 4) | recovery;
	winbond_writecfg(winbond->config, timing, reg);

	/* Load the setup timing */

	reg = 0x35;
	if (adev->class != ATA_DEV_ATA)
		reg |= 0x08;	/* FIFO off */
	if (!ata_pio_need_iordy(adev))
		reg |= 0x02;	/* IORDY off */
	reg |= (FIT(t.setup, 0, 3) << 6);
	winbond_writecfg(winbond->config, timing + 1, reg);
}


static void winbond_data_xfer(struct ata_device *adev, unsigned char *buf, unsigned int buflen, int write_data)
{
	struct ata_port *ap = adev->link->ap;
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
				pad = cpu_to_le16(pad);
				memcpy(buf + buflen - slop, &pad, slop);
			}
		}
	} else
		ata_data_xfer(adev, buf, buflen, write_data);
}

static struct scsi_host_template winbond_sht = {
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

static struct ata_port_operations winbond_port_ops = {
	.set_piomode	= winbond_set_piomode,

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
	.qc_issue	= ata_qc_issue_prot,

	.data_xfer	= winbond_data_xfer,

	.irq_clear	= ata_bmdma_irq_clear,
	.irq_on		= ata_irq_on,

	.port_start	= ata_sff_port_start,
};

/**
 *	winbond_init_one		-	attach a winbond interface
 *	@type: Type to display
 *	@io: I/O port start
 *	@irq: interrupt line
 *	@fast: True if on a > 33Mhz VLB
 *
 *	Register a VLB bus IDE interface. Such interfaces are PIO and we
 *	assume do not support IRQ sharing.
 */

static __init int winbond_init_one(unsigned long port)
{
	struct platform_device *pdev;
	u8 reg;
	int i, rc;

	reg = winbond_readcfg(port, 0x81);
	reg |= 0x80;	/* jumpered mode off */
	winbond_writecfg(port, 0x81, reg);
	reg = winbond_readcfg(port, 0x83);
	reg |= 0xF0;	/* local control */
	winbond_writecfg(port, 0x83, reg);
	reg = winbond_readcfg(port, 0x85);
	reg |= 0xF0;	/* programmable timing */
	winbond_writecfg(port, 0x85, reg);

	reg = winbond_readcfg(port, 0x81);

	if (!(reg & 0x03))		/* Disabled */
		return 0;

	for (i = 0; i < 2 ; i ++) {
		unsigned long cmd_port = 0x1F0 - (0x80 * i);
		unsigned long ctl_port = cmd_port + 0x206;
		struct ata_host *host;
		struct ata_port *ap;
		void __iomem *cmd_addr, *ctl_addr;

		if (!(reg & (1 << i)))
			continue;

		pdev = platform_device_register_simple(DRV_NAME, nr_winbond_host, NULL, 0);
		if (IS_ERR(pdev))
			return PTR_ERR(pdev);

		rc = -ENOMEM;
		host = ata_host_alloc(&pdev->dev, 1);
		if (!host)
			goto err_unregister;
		ap = host->ports[0];

		rc = -ENOMEM;
		cmd_addr = devm_ioport_map(&pdev->dev, cmd_port, 8);
		ctl_addr = devm_ioport_map(&pdev->dev, ctl_port, 1);
		if (!cmd_addr || !ctl_addr)
			goto err_unregister;

		ata_port_desc(ap, "cmd 0x%lx ctl 0x%lx", cmd_port, ctl_port);

		ap->ops = &winbond_port_ops;
		ap->pio_mask = 0x1F;
		ap->flags |= ATA_FLAG_SLAVE_POSS;
		ap->ioaddr.cmd_addr = cmd_addr;
		ap->ioaddr.altstatus_addr = ctl_addr;
		ap->ioaddr.ctl_addr = ctl_addr;
		ata_std_ports(&ap->ioaddr);

		/* hook in a private data structure per channel */
		host->private_data = &winbond_data[nr_winbond_host];
		winbond_data[nr_winbond_host].config = port;
		winbond_data[nr_winbond_host].platform_dev = pdev;

		/* activate */
		rc = ata_host_activate(host, 14 + i, ata_interrupt, 0,
				       &winbond_sht);
		if (rc)
			goto err_unregister;

		winbond_host[nr_winbond_host++] = dev_get_drvdata(&pdev->dev);
	}

	return 0;

 err_unregister:
	platform_device_unregister(pdev);
	return rc;
}

/**
 *	winbond_init		-	attach winbond interfaces
 *
 *	Attach winbond IDE interfaces by scanning the ports it may occupy.
 */

static __init int winbond_init(void)
{
	static const unsigned long config[2] = { 0x130, 0x1B0 };

	int ct = 0;
	int i;

	if (probe_winbond == 0)
		return -ENODEV;

	/*
 	 *	Check both base addresses
	 */

	for (i = 0; i < 2; i++) {
		if (probe_winbond & (1<<i)) {
			int ret = 0;
			unsigned long port = config[i];

			if (request_region(port, 2, "pata_winbond")) {
				ret = winbond_init_one(port);
				if(ret <= 0)
					release_region(port, 2);
				else ct+= ret;
			}
		}
	}
	if (ct != 0)
		return 0;
	return -ENODEV;
}

static __exit void winbond_exit(void)
{
	int i;

	for (i = 0; i < nr_winbond_host; i++) {
		ata_host_detach(winbond_host[i]);
		release_region(winbond_data[i].config, 2);
		platform_device_unregister(winbond_data[i].platform_dev);
	}
}

MODULE_AUTHOR("Alan Cox");
MODULE_DESCRIPTION("low-level driver for Winbond VL ATA");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);

module_init(winbond_init);
module_exit(winbond_exit);

module_param(probe_winbond, int, 0);

