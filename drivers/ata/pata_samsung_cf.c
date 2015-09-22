/*
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * PATA driver for Samsung SoCs.
 * Supports CF Interface in True IDE mode. Currently only PIO mode has been
 * implemented; UDMA support has to be added.
 *
 * Based on:
 *	PATA driver for AT91SAM9260 Static Memory Controller
 *	PATA driver for Toshiba SCC controller
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
*/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/clk.h>
#include <linux/libata.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <linux/platform_data/ata-samsung_cf.h>

#define DRV_NAME "pata_samsung_cf"
#define DRV_VERSION "0.1"

#define S3C_CFATA_REG(x)	(x)
#define S3C_CFATA_MUX		S3C_CFATA_REG(0x0)
#define S3C_ATA_CTRL		S3C_CFATA_REG(0x0)
#define S3C_ATA_CMD		S3C_CFATA_REG(0x8)
#define S3C_ATA_IRQ		S3C_CFATA_REG(0x10)
#define S3C_ATA_IRQ_MSK		S3C_CFATA_REG(0x14)
#define S3C_ATA_CFG		S3C_CFATA_REG(0x18)

#define S3C_ATA_PIO_TIME	S3C_CFATA_REG(0x2c)
#define S3C_ATA_PIO_DTR		S3C_CFATA_REG(0x54)
#define S3C_ATA_PIO_FED		S3C_CFATA_REG(0x58)
#define S3C_ATA_PIO_SCR		S3C_CFATA_REG(0x5c)
#define S3C_ATA_PIO_LLR		S3C_CFATA_REG(0x60)
#define S3C_ATA_PIO_LMR		S3C_CFATA_REG(0x64)
#define S3C_ATA_PIO_LHR		S3C_CFATA_REG(0x68)
#define S3C_ATA_PIO_DVR		S3C_CFATA_REG(0x6c)
#define S3C_ATA_PIO_CSD		S3C_CFATA_REG(0x70)
#define S3C_ATA_PIO_DAD		S3C_CFATA_REG(0x74)
#define S3C_ATA_PIO_RDATA	S3C_CFATA_REG(0x7c)

#define S3C_CFATA_MUX_TRUEIDE	0x01
#define S3C_ATA_CFG_SWAP	0x40
#define S3C_ATA_CFG_IORDYEN	0x02

enum s3c_cpu_type {
	TYPE_S3C64XX,
	TYPE_S5PV210,
};

/*
 * struct s3c_ide_info - S3C PATA instance.
 * @clk: The clock resource for this controller.
 * @ide_addr: The area mapped for the hardware registers.
 * @sfr_addr: The area mapped for the special function registers.
 * @irq: The IRQ number we are using.
 * @cpu_type: The exact type of this controller.
 * @fifo_status_reg: The ATA_FIFO_STATUS register offset.
 */
struct s3c_ide_info {
	struct clk *clk;
	void __iomem *ide_addr;
	void __iomem *sfr_addr;
	unsigned int irq;
	enum s3c_cpu_type cpu_type;
	unsigned int fifo_status_reg;
};

static void pata_s3c_set_endian(void __iomem *s3c_ide_regbase, u8 mode)
{
	u32 reg = readl(s3c_ide_regbase + S3C_ATA_CFG);
	reg = mode ? (reg & ~S3C_ATA_CFG_SWAP) : (reg | S3C_ATA_CFG_SWAP);
	writel(reg, s3c_ide_regbase + S3C_ATA_CFG);
}

static void pata_s3c_cfg_mode(void __iomem *s3c_ide_sfrbase)
{
	/* Select true-ide as the internal operating mode */
	writel(readl(s3c_ide_sfrbase + S3C_CFATA_MUX) | S3C_CFATA_MUX_TRUEIDE,
		s3c_ide_sfrbase + S3C_CFATA_MUX);
}

static unsigned long
pata_s3c_setup_timing(struct s3c_ide_info *info, const struct ata_timing *ata)
{
	int t1 = ata->setup;
	int t2 = ata->act8b;
	int t2i = ata->rec8b;
	ulong piotime;

	piotime = ((t2i & 0xff) << 12) | ((t2 & 0xff) << 4) | (t1 & 0xf);

	return piotime;
}

static void pata_s3c_set_piomode(struct ata_port *ap, struct ata_device *adev)
{
	struct s3c_ide_info *info = ap->host->private_data;
	struct ata_timing timing;
	int cycle_time;
	ulong ata_cfg = readl(info->ide_addr + S3C_ATA_CFG);
	ulong piotime;

	/* Enables IORDY if mode requires it */
	if (ata_pio_need_iordy(adev))
		ata_cfg |= S3C_ATA_CFG_IORDYEN;
	else
		ata_cfg &= ~S3C_ATA_CFG_IORDYEN;

	cycle_time = (int)(1000000000UL / clk_get_rate(info->clk));

	ata_timing_compute(adev, adev->pio_mode, &timing,
					cycle_time * 1000, 0);

	piotime = pata_s3c_setup_timing(info, &timing);

	writel(ata_cfg, info->ide_addr + S3C_ATA_CFG);
	writel(piotime, info->ide_addr + S3C_ATA_PIO_TIME);
}

/*
 * Waits until the IDE controller is able to perform next read/write
 * operation to the disk. Needed for 64XX series boards only.
 */
static int wait_for_host_ready(struct s3c_ide_info *info)
{
	ulong timeout;
	void __iomem *fifo_reg = info->ide_addr + info->fifo_status_reg;

	/* wait for maximum of 20 msec */
	timeout = jiffies + msecs_to_jiffies(20);
	while (time_before(jiffies, timeout)) {
		if ((readl(fifo_reg) >> 28) == 0)
			return 0;
	}
	return -EBUSY;
}

/*
 * Writes to one of the task file registers.
 */
static void ata_outb(struct ata_host *host, u8 addr, void __iomem *reg)
{
	struct s3c_ide_info *info = host->private_data;

	wait_for_host_ready(info);
	writeb(addr, reg);
}

/*
 * Reads from one of the task file registers.
 */
static u8 ata_inb(struct ata_host *host, void __iomem *reg)
{
	struct s3c_ide_info *info = host->private_data;
	u8 temp;

	wait_for_host_ready(info);
	(void) readb(reg);
	wait_for_host_ready(info);
	temp = readb(info->ide_addr + S3C_ATA_PIO_RDATA);
	return temp;
}

/*
 * pata_s3c_tf_load - send taskfile registers to host controller
 */
static void pata_s3c_tf_load(struct ata_port *ap,
				const struct ata_taskfile *tf)
{
	struct ata_ioports *ioaddr = &ap->ioaddr;
	unsigned int is_addr = tf->flags & ATA_TFLAG_ISADDR;

	if (tf->ctl != ap->last_ctl) {
		ata_outb(ap->host, tf->ctl, ioaddr->ctl_addr);
		ap->last_ctl = tf->ctl;
		ata_wait_idle(ap);
	}

	if (is_addr && (tf->flags & ATA_TFLAG_LBA48)) {
		ata_outb(ap->host, tf->hob_feature, ioaddr->feature_addr);
		ata_outb(ap->host, tf->hob_nsect, ioaddr->nsect_addr);
		ata_outb(ap->host, tf->hob_lbal, ioaddr->lbal_addr);
		ata_outb(ap->host, tf->hob_lbam, ioaddr->lbam_addr);
		ata_outb(ap->host, tf->hob_lbah, ioaddr->lbah_addr);
	}

	if (is_addr) {
		ata_outb(ap->host, tf->feature, ioaddr->feature_addr);
		ata_outb(ap->host, tf->nsect, ioaddr->nsect_addr);
		ata_outb(ap->host, tf->lbal, ioaddr->lbal_addr);
		ata_outb(ap->host, tf->lbam, ioaddr->lbam_addr);
		ata_outb(ap->host, tf->lbah, ioaddr->lbah_addr);
	}

	if (tf->flags & ATA_TFLAG_DEVICE)
		ata_outb(ap->host, tf->device, ioaddr->device_addr);

	ata_wait_idle(ap);
}

/*
 * pata_s3c_tf_read - input device's ATA taskfile shadow registers
 */
static void pata_s3c_tf_read(struct ata_port *ap, struct ata_taskfile *tf)
{
	struct ata_ioports *ioaddr = &ap->ioaddr;

	tf->feature = ata_inb(ap->host, ioaddr->error_addr);
	tf->nsect = ata_inb(ap->host, ioaddr->nsect_addr);
	tf->lbal = ata_inb(ap->host, ioaddr->lbal_addr);
	tf->lbam = ata_inb(ap->host, ioaddr->lbam_addr);
	tf->lbah = ata_inb(ap->host, ioaddr->lbah_addr);
	tf->device = ata_inb(ap->host, ioaddr->device_addr);

	if (tf->flags & ATA_TFLAG_LBA48) {
		ata_outb(ap->host, tf->ctl | ATA_HOB, ioaddr->ctl_addr);
		tf->hob_feature = ata_inb(ap->host, ioaddr->error_addr);
		tf->hob_nsect = ata_inb(ap->host, ioaddr->nsect_addr);
		tf->hob_lbal = ata_inb(ap->host, ioaddr->lbal_addr);
		tf->hob_lbam = ata_inb(ap->host, ioaddr->lbam_addr);
		tf->hob_lbah = ata_inb(ap->host, ioaddr->lbah_addr);
		ata_outb(ap->host, tf->ctl, ioaddr->ctl_addr);
		ap->last_ctl = tf->ctl;
	}
}

/*
 * pata_s3c_exec_command - issue ATA command to host controller
 */
static void pata_s3c_exec_command(struct ata_port *ap,
				const struct ata_taskfile *tf)
{
	ata_outb(ap->host, tf->command, ap->ioaddr.command_addr);
	ata_sff_pause(ap);
}

/*
 * pata_s3c_check_status - Read device status register
 */
static u8 pata_s3c_check_status(struct ata_port *ap)
{
	return ata_inb(ap->host, ap->ioaddr.status_addr);
}

/*
 * pata_s3c_check_altstatus - Read alternate device status register
 */
static u8 pata_s3c_check_altstatus(struct ata_port *ap)
{
	return ata_inb(ap->host, ap->ioaddr.altstatus_addr);
}

/*
 * pata_s3c_data_xfer - Transfer data by PIO
 */
static unsigned int pata_s3c_data_xfer(struct ata_device *dev,
				unsigned char *buf, unsigned int buflen, int rw)
{
	struct ata_port *ap = dev->link->ap;
	struct s3c_ide_info *info = ap->host->private_data;
	void __iomem *data_addr = ap->ioaddr.data_addr;
	unsigned int words = buflen >> 1, i;
	u16 *data_ptr = (u16 *)buf;

	/* Requires wait same as in ata_inb/ata_outb */
	if (rw == READ)
		for (i = 0; i < words; i++, data_ptr++) {
			wait_for_host_ready(info);
			(void) readw(data_addr);
			wait_for_host_ready(info);
			*data_ptr = readw(info->ide_addr
					+ S3C_ATA_PIO_RDATA);
		}
	else
		for (i = 0; i < words; i++, data_ptr++) {
			wait_for_host_ready(info);
			writew(*data_ptr, data_addr);
		}

	if (buflen & 0x01)
		dev_err(ap->dev, "unexpected trailing data\n");

	return words << 1;
}

/*
 * pata_s3c_dev_select - Select device on ATA bus
 */
static void pata_s3c_dev_select(struct ata_port *ap, unsigned int device)
{
	u8 tmp = ATA_DEVICE_OBS;

	if (device != 0)
		tmp |= ATA_DEV1;

	ata_outb(ap->host, tmp, ap->ioaddr.device_addr);
	ata_sff_pause(ap);
}

/*
 * pata_s3c_devchk - PATA device presence detection
 */
static unsigned int pata_s3c_devchk(struct ata_port *ap,
				unsigned int device)
{
	struct ata_ioports *ioaddr = &ap->ioaddr;
	u8 nsect, lbal;

	pata_s3c_dev_select(ap, device);

	ata_outb(ap->host, 0x55, ioaddr->nsect_addr);
	ata_outb(ap->host, 0xaa, ioaddr->lbal_addr);

	ata_outb(ap->host, 0xaa, ioaddr->nsect_addr);
	ata_outb(ap->host, 0x55, ioaddr->lbal_addr);

	ata_outb(ap->host, 0x55, ioaddr->nsect_addr);
	ata_outb(ap->host, 0xaa, ioaddr->lbal_addr);

	nsect = ata_inb(ap->host, ioaddr->nsect_addr);
	lbal = ata_inb(ap->host, ioaddr->lbal_addr);

	if ((nsect == 0x55) && (lbal == 0xaa))
		return 1;	/* we found a device */

	return 0;		/* nothing found */
}

/*
 * pata_s3c_wait_after_reset - wait for devices to become ready after reset
 */
static int pata_s3c_wait_after_reset(struct ata_link *link,
		unsigned long deadline)
{
	int rc;

	ata_msleep(link->ap, ATA_WAIT_AFTER_RESET);

	/* always check readiness of the master device */
	rc = ata_sff_wait_ready(link, deadline);
	/* -ENODEV means the odd clown forgot the D7 pulldown resistor
	 * and TF status is 0xff, bail out on it too.
	 */
	if (rc)
		return rc;

	return 0;
}

/*
 * pata_s3c_bus_softreset - PATA device software reset
 */
static int pata_s3c_bus_softreset(struct ata_port *ap,
		unsigned long deadline)
{
	struct ata_ioports *ioaddr = &ap->ioaddr;

	/* software reset.  causes dev0 to be selected */
	ata_outb(ap->host, ap->ctl, ioaddr->ctl_addr);
	udelay(20);
	ata_outb(ap->host, ap->ctl | ATA_SRST, ioaddr->ctl_addr);
	udelay(20);
	ata_outb(ap->host, ap->ctl, ioaddr->ctl_addr);
	ap->last_ctl = ap->ctl;

	return pata_s3c_wait_after_reset(&ap->link, deadline);
}

/*
 * pata_s3c_softreset - reset host port via ATA SRST
 */
static int pata_s3c_softreset(struct ata_link *link, unsigned int *classes,
			 unsigned long deadline)
{
	struct ata_port *ap = link->ap;
	unsigned int devmask = 0;
	int rc;
	u8 err;

	/* determine if device 0 is present */
	if (pata_s3c_devchk(ap, 0))
		devmask |= (1 << 0);

	/* select device 0 again */
	pata_s3c_dev_select(ap, 0);

	/* issue bus reset */
	rc = pata_s3c_bus_softreset(ap, deadline);
	/* if link is occupied, -ENODEV too is an error */
	if (rc && rc != -ENODEV) {
		ata_link_err(link, "SRST failed (errno=%d)\n", rc);
		return rc;
	}

	/* determine by signature whether we have ATA or ATAPI devices */
	classes[0] = ata_sff_dev_classify(&ap->link.device[0],
					  devmask & (1 << 0), &err);

	return 0;
}

/*
 * pata_s3c_set_devctl - Write device control register
 */
static void pata_s3c_set_devctl(struct ata_port *ap, u8 ctl)
{
	ata_outb(ap->host, ctl, ap->ioaddr.ctl_addr);
}

static struct scsi_host_template pata_s3c_sht = {
	ATA_PIO_SHT(DRV_NAME),
};

static struct ata_port_operations pata_s3c_port_ops = {
	.inherits		= &ata_sff_port_ops,
	.sff_check_status	= pata_s3c_check_status,
	.sff_check_altstatus    = pata_s3c_check_altstatus,
	.sff_tf_load		= pata_s3c_tf_load,
	.sff_tf_read		= pata_s3c_tf_read,
	.sff_data_xfer		= pata_s3c_data_xfer,
	.sff_exec_command	= pata_s3c_exec_command,
	.sff_dev_select         = pata_s3c_dev_select,
	.sff_set_devctl         = pata_s3c_set_devctl,
	.softreset		= pata_s3c_softreset,
	.set_piomode		= pata_s3c_set_piomode,
};

static struct ata_port_operations pata_s5p_port_ops = {
	.inherits		= &ata_sff_port_ops,
	.set_piomode		= pata_s3c_set_piomode,
};

static void pata_s3c_enable(void __iomem *s3c_ide_regbase, bool state)
{
	u32 temp = readl(s3c_ide_regbase + S3C_ATA_CTRL);
	temp = state ? (temp | 1) : (temp & ~1);
	writel(temp, s3c_ide_regbase + S3C_ATA_CTRL);
}

static irqreturn_t pata_s3c_irq(int irq, void *dev_instance)
{
	struct ata_host *host = dev_instance;
	struct s3c_ide_info *info = host->private_data;
	u32 reg;

	reg = readl(info->ide_addr + S3C_ATA_IRQ);
	writel(reg, info->ide_addr + S3C_ATA_IRQ);

	return ata_sff_interrupt(irq, dev_instance);
}

static void pata_s3c_hwinit(struct s3c_ide_info *info,
				struct s3c_ide_platdata *pdata)
{
	switch (info->cpu_type) {
	case TYPE_S3C64XX:
		/* Configure as big endian */
		pata_s3c_cfg_mode(info->sfr_addr);
		pata_s3c_set_endian(info->ide_addr, 1);
		pata_s3c_enable(info->ide_addr, true);
		msleep(100);

		/* Remove IRQ Status */
		writel(0x1f, info->ide_addr + S3C_ATA_IRQ);
		writel(0x1b, info->ide_addr + S3C_ATA_IRQ_MSK);
		break;

	case TYPE_S5PV210:
		/* Configure as little endian */
		pata_s3c_set_endian(info->ide_addr, 0);
		pata_s3c_enable(info->ide_addr, true);
		msleep(100);

		/* Remove IRQ Status */
		writel(0x3f, info->ide_addr + S3C_ATA_IRQ);
		writel(0x3f, info->ide_addr + S3C_ATA_IRQ_MSK);
		break;

	default:
		BUG();
	}
}

static int __init pata_s3c_probe(struct platform_device *pdev)
{
	struct s3c_ide_platdata *pdata = dev_get_platdata(&pdev->dev);
	struct device *dev = &pdev->dev;
	struct s3c_ide_info *info;
	struct resource *res;
	struct ata_port *ap;
	struct ata_host *host;
	enum s3c_cpu_type cpu_type;
	int ret;

	cpu_type = platform_get_device_id(pdev)->driver_data;

	info = devm_kzalloc(dev, sizeof(*info), GFP_KERNEL);
	if (!info) {
		dev_err(dev, "failed to allocate memory for device data\n");
		return -ENOMEM;
	}

	info->irq = platform_get_irq(pdev, 0);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	info->ide_addr = devm_ioremap_resource(dev, res);
	if (IS_ERR(info->ide_addr))
		return PTR_ERR(info->ide_addr);

	info->clk = devm_clk_get(&pdev->dev, "cfcon");
	if (IS_ERR(info->clk)) {
		dev_err(dev, "failed to get access to cf controller clock\n");
		ret = PTR_ERR(info->clk);
		info->clk = NULL;
		return ret;
	}

	clk_enable(info->clk);

	/* init ata host */
	host = ata_host_alloc(dev, 1);
	if (!host) {
		dev_err(dev, "failed to allocate ide host\n");
		ret = -ENOMEM;
		goto stop_clk;
	}

	ap = host->ports[0];
	ap->pio_mask = ATA_PIO4;

	if (cpu_type == TYPE_S3C64XX) {
		ap->ops = &pata_s3c_port_ops;
		info->sfr_addr = info->ide_addr + 0x1800;
		info->ide_addr += 0x1900;
		info->fifo_status_reg = 0x94;
	} else {
		ap->ops = &pata_s5p_port_ops;
		info->fifo_status_reg = 0x84;
	}

	info->cpu_type = cpu_type;

	if (info->irq <= 0) {
		ap->flags |= ATA_FLAG_PIO_POLLING;
		info->irq = 0;
		ata_port_desc(ap, "no IRQ, using PIO polling\n");
	}

	ap->ioaddr.cmd_addr =  info->ide_addr + S3C_ATA_CMD;
	ap->ioaddr.data_addr = info->ide_addr + S3C_ATA_PIO_DTR;
	ap->ioaddr.error_addr = info->ide_addr + S3C_ATA_PIO_FED;
	ap->ioaddr.feature_addr = info->ide_addr + S3C_ATA_PIO_FED;
	ap->ioaddr.nsect_addr = info->ide_addr + S3C_ATA_PIO_SCR;
	ap->ioaddr.lbal_addr = info->ide_addr + S3C_ATA_PIO_LLR;
	ap->ioaddr.lbam_addr = info->ide_addr + S3C_ATA_PIO_LMR;
	ap->ioaddr.lbah_addr = info->ide_addr + S3C_ATA_PIO_LHR;
	ap->ioaddr.device_addr = info->ide_addr + S3C_ATA_PIO_DVR;
	ap->ioaddr.status_addr = info->ide_addr + S3C_ATA_PIO_CSD;
	ap->ioaddr.command_addr = info->ide_addr + S3C_ATA_PIO_CSD;
	ap->ioaddr.altstatus_addr = info->ide_addr + S3C_ATA_PIO_DAD;
	ap->ioaddr.ctl_addr = info->ide_addr + S3C_ATA_PIO_DAD;

	ata_port_desc(ap, "mmio cmd 0x%llx ",
			(unsigned long long)res->start);

	host->private_data = info;

	if (pdata && pdata->setup_gpio)
		pdata->setup_gpio();

	/* Set endianness and enable the interface */
	pata_s3c_hwinit(info, pdata);

	platform_set_drvdata(pdev, host);

	ret = ata_host_activate(host, info->irq,
				info->irq ? pata_s3c_irq : NULL,
				0, &pata_s3c_sht);
	if (ret)
		goto stop_clk;

	return 0;

stop_clk:
	clk_disable(info->clk);
	return ret;
}

static int __exit pata_s3c_remove(struct platform_device *pdev)
{
	struct ata_host *host = platform_get_drvdata(pdev);
	struct s3c_ide_info *info = host->private_data;

	ata_host_detach(host);

	clk_disable(info->clk);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int pata_s3c_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ata_host *host = platform_get_drvdata(pdev);

	return ata_host_suspend(host, PMSG_SUSPEND);
}

static int pata_s3c_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ata_host *host = platform_get_drvdata(pdev);
	struct s3c_ide_platdata *pdata = dev_get_platdata(&pdev->dev);
	struct s3c_ide_info *info = host->private_data;

	pata_s3c_hwinit(info, pdata);
	ata_host_resume(host);

	return 0;
}

static const struct dev_pm_ops pata_s3c_pm_ops = {
	.suspend	= pata_s3c_suspend,
	.resume		= pata_s3c_resume,
};
#endif

/* driver device registration */
static const struct platform_device_id pata_s3c_driver_ids[] = {
	{
		.name		= "s3c64xx-pata",
		.driver_data	= TYPE_S3C64XX,
	}, {
		.name		= "s5pv210-pata",
		.driver_data	= TYPE_S5PV210,
	},
	{ }
};

MODULE_DEVICE_TABLE(platform, pata_s3c_driver_ids);

static struct platform_driver pata_s3c_driver = {
	.remove		= __exit_p(pata_s3c_remove),
	.id_table	= pata_s3c_driver_ids,
	.driver		= {
		.name	= DRV_NAME,
#ifdef CONFIG_PM_SLEEP
		.pm	= &pata_s3c_pm_ops,
#endif
	},
};

module_platform_driver_probe(pata_s3c_driver, pata_s3c_probe);

MODULE_AUTHOR("Abhilash Kesavan, <a.kesavan@samsung.com>");
MODULE_DESCRIPTION("low-level driver for Samsung PATA controller");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);
