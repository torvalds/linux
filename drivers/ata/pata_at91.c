/*
 * PATA driver for AT91SAM9260 Static Memory Controller
 * with CompactFlash interface in True IDE mode
 *
 * Copyright (C) 2009 Matyukevich Sergey
 *
 * Based on:
 *      * generic platform driver by Paul Mundt: drivers/ata/pata_platform.c
 *      * pata_at32 driver by Kristoffer Nyborg Gregertsen
 *      * at91_ide driver by Stanislaw Gruszka
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/gfp.h>
#include <scsi/scsi_host.h>
#include <linux/ata.h>
#include <linux/clk.h>
#include <linux/libata.h>
#include <linux/platform_device.h>
#include <linux/ata_platform.h>

#include <mach/at91sam9_smc.h>
#include <mach/board.h>
#include <mach/gpio.h>


#define DRV_NAME "pata_at91"
#define DRV_VERSION "0.2"

#define CF_IDE_OFFSET	    0x00c00000
#define CF_ALT_IDE_OFFSET   0x00e00000
#define CF_IDE_RES_SIZE     0x08
#define NCS_RD_PULSE_LIMIT  0x3f /* maximal value for pulse bitfields */

struct at91_ide_info {
	unsigned long mode;
	unsigned int cs;

	struct clk *mck;

	void __iomem *ide_addr;
	void __iomem *alt_addr;
};

static const struct ata_timing initial_timing = {
	.mode		= XFER_PIO_0,
	.setup		= 70,
	.act8b		= 290,
	.rec8b		= 240,
	.cyc8b		= 600,
	.active		= 165,
	.recover	= 150,
	.dmack_hold	= 0,
	.cycle		= 600,
	.udma		= 0
};

static unsigned long calc_mck_cycles(unsigned long ns, unsigned long mck_hz)
{
	unsigned long mul;

	/*
	* cycles = x [nsec] * f [Hz] / 10^9 [ns in sec] =
	*     x * (f / 1_000_000_000) =
	*     x * ((f * 65536) / 1_000_000_000) / 65536 =
	*     x * (((f / 10_000) * 65536) / 100_000) / 65536 =
	*/

	mul = (mck_hz / 10000) << 16;
	mul /= 100000;

	return (ns * mul + 65536) >> 16;    /* rounding */
}

static void set_smc_mode(struct at91_ide_info *info)
{
	at91_sys_write(AT91_SMC_MODE(info->cs), info->mode);
	return;
}

static void set_smc_timing(struct device *dev,
		struct at91_ide_info *info, const struct ata_timing *ata)
{
	unsigned long read_cycle, write_cycle, active, recover;
	unsigned long nrd_setup, nrd_pulse, nrd_recover;
	unsigned long nwe_setup, nwe_pulse;

	unsigned long ncs_write_setup, ncs_write_pulse;
	unsigned long ncs_read_setup, ncs_read_pulse;

	unsigned long mck_hz;

	read_cycle  = ata->cyc8b;
	nrd_setup   = ata->setup;
	nrd_pulse   = ata->act8b;
	nrd_recover = ata->rec8b;

	mck_hz = clk_get_rate(info->mck);

	read_cycle  = calc_mck_cycles(read_cycle, mck_hz);
	nrd_setup   = calc_mck_cycles(nrd_setup, mck_hz);
	nrd_pulse   = calc_mck_cycles(nrd_pulse, mck_hz);
	nrd_recover = calc_mck_cycles(nrd_recover, mck_hz);

	active  = nrd_setup + nrd_pulse;
	recover = read_cycle - active;

	/* Need at least two cycles recovery */
	if (recover < 2)
		read_cycle = active + 2;

	/* (CS0, CS1, DIR, OE) <= (CFCE1, CFCE2, CFRNW, NCSX) timings */
	ncs_read_setup = 1;
	ncs_read_pulse = read_cycle - 2;
	if (ncs_read_pulse > NCS_RD_PULSE_LIMIT) {
		ncs_read_pulse = NCS_RD_PULSE_LIMIT;
		dev_warn(dev, "ncs_read_pulse limited to maximal value %lu\n",
			ncs_read_pulse);
	}

	/* Write timings same as read timings */
	write_cycle = read_cycle;
	nwe_setup = nrd_setup;
	nwe_pulse = nrd_pulse;
	ncs_write_setup = ncs_read_setup;
	ncs_write_pulse = ncs_read_pulse;

	dev_dbg(dev, "ATA timings: nrd_setup = %lu nrd_pulse = %lu nrd_cycle = %lu\n",
			nrd_setup, nrd_pulse, read_cycle);
	dev_dbg(dev, "ATA timings: nwe_setup = %lu nwe_pulse = %lu nwe_cycle = %lu\n",
			nwe_setup, nwe_pulse, write_cycle);
	dev_dbg(dev, "ATA timings: ncs_read_setup = %lu ncs_read_pulse = %lu\n",
			ncs_read_setup, ncs_read_pulse);
	dev_dbg(dev, "ATA timings: ncs_write_setup = %lu ncs_write_pulse = %lu\n",
			ncs_write_setup, ncs_write_pulse);

	at91_sys_write(AT91_SMC_SETUP(info->cs),
			AT91_SMC_NWESETUP_(nwe_setup) |
			AT91_SMC_NRDSETUP_(nrd_setup) |
			AT91_SMC_NCS_WRSETUP_(ncs_write_setup) |
			AT91_SMC_NCS_RDSETUP_(ncs_read_setup));

	at91_sys_write(AT91_SMC_PULSE(info->cs),
			AT91_SMC_NWEPULSE_(nwe_pulse) |
			AT91_SMC_NRDPULSE_(nrd_pulse) |
			AT91_SMC_NCS_WRPULSE_(ncs_write_pulse) |
			AT91_SMC_NCS_RDPULSE_(ncs_read_pulse));

	at91_sys_write(AT91_SMC_CYCLE(info->cs),
			AT91_SMC_NWECYCLE_(write_cycle) |
			AT91_SMC_NRDCYCLE_(read_cycle));

	return;
}

static void pata_at91_set_piomode(struct ata_port *ap, struct ata_device *adev)
{
	struct at91_ide_info *info = ap->host->private_data;
	struct ata_timing timing;
	int ret;

	/* Compute ATA timing and set it to SMC */
	ret = ata_timing_compute(adev, adev->pio_mode, &timing, 1000, 0);
	if (ret) {
		dev_warn(ap->dev, "Failed to compute ATA timing %d, "
			 "set PIO_0 timing\n", ret);
		set_smc_timing(ap->dev, info, &initial_timing);
	} else {
		set_smc_timing(ap->dev, info, &timing);
	}

	/* Setup SMC mode */
	set_smc_mode(info);

	return;
}

static unsigned int pata_at91_data_xfer_noirq(struct ata_device *dev,
		unsigned char *buf, unsigned int buflen, int rw)
{
	struct at91_ide_info *info = dev->link->ap->host->private_data;
	unsigned int consumed;
	unsigned long flags;
	unsigned int mode;

	local_irq_save(flags);
	mode = at91_sys_read(AT91_SMC_MODE(info->cs));

	/* set 16bit mode before writing data */
	at91_sys_write(AT91_SMC_MODE(info->cs),
			(mode & ~AT91_SMC_DBW) | AT91_SMC_DBW_16);

	consumed = ata_sff_data_xfer(dev, buf, buflen, rw);

	/* restore 8bit mode after data is written */
	at91_sys_write(AT91_SMC_MODE(info->cs),
			(mode & ~AT91_SMC_DBW) | AT91_SMC_DBW_8);

	local_irq_restore(flags);
	return consumed;
}

static struct scsi_host_template pata_at91_sht = {
	ATA_PIO_SHT(DRV_NAME),
};

static struct ata_port_operations pata_at91_port_ops = {
	.inherits	= &ata_sff_port_ops,

	.sff_data_xfer	= pata_at91_data_xfer_noirq,
	.set_piomode	= pata_at91_set_piomode,
	.cable_detect	= ata_cable_40wire,
};

static int __devinit pata_at91_probe(struct platform_device *pdev)
{
	struct at91_cf_data *board = pdev->dev.platform_data;
	struct device *dev = &pdev->dev;
	struct at91_ide_info *info;
	struct resource *mem_res;
	struct ata_host *host;
	struct ata_port *ap;

	int irq_flags = 0;
	int irq = 0;
	int ret;

	/*  get platform resources: IO/CTL memories and irq/rst pins */

	if (pdev->num_resources != 1) {
		dev_err(&pdev->dev, "invalid number of resources\n");
		return -EINVAL;
	}

	mem_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	if (!mem_res) {
		dev_err(dev, "failed to get mem resource\n");
		return -EINVAL;
	}

	irq = board->irq_pin;

	/* init ata host */

	host = ata_host_alloc(dev, 1);

	if (!host)
		return -ENOMEM;

	ap = host->ports[0];
	ap->ops = &pata_at91_port_ops;
	ap->flags |= ATA_FLAG_SLAVE_POSS;
	ap->pio_mask = ATA_PIO4;

	if (!irq) {
		ap->flags |= ATA_FLAG_PIO_POLLING;
		ata_port_desc(ap, "no IRQ, using PIO polling");
	}

	info = devm_kzalloc(dev, sizeof(*info), GFP_KERNEL);

	if (!info) {
		dev_err(dev, "failed to allocate memory for private data\n");
		return -ENOMEM;
	}

	info->mck = clk_get(NULL, "mck");

	if (IS_ERR(info->mck)) {
		dev_err(dev, "failed to get access to mck clock\n");
		return -ENODEV;
	}

	info->cs    = board->chipselect;
	info->mode  = AT91_SMC_READMODE | AT91_SMC_WRITEMODE |
		AT91_SMC_EXNWMODE_READY | AT91_SMC_BAT_SELECT |
		AT91_SMC_DBW_8 | AT91_SMC_TDF_(0);

	info->ide_addr = devm_ioremap(dev,
			mem_res->start + CF_IDE_OFFSET, CF_IDE_RES_SIZE);

	if (!info->ide_addr) {
		dev_err(dev, "failed to map IO base\n");
		ret = -ENOMEM;
		goto err_put;
	}

	info->alt_addr = devm_ioremap(dev,
			mem_res->start + CF_ALT_IDE_OFFSET, CF_IDE_RES_SIZE);

	if (!info->alt_addr) {
		dev_err(dev, "failed to map CTL base\n");
		ret = -ENOMEM;
		goto err_put;
	}

	ap->ioaddr.cmd_addr = info->ide_addr;
	ap->ioaddr.ctl_addr = info->alt_addr + 0x06;
	ap->ioaddr.altstatus_addr = ap->ioaddr.ctl_addr;

	ata_sff_std_ports(&ap->ioaddr);

	ata_port_desc(ap, "mmio cmd 0x%llx ctl 0x%llx",
			(unsigned long long)mem_res->start + CF_IDE_OFFSET,
			(unsigned long long)mem_res->start + CF_ALT_IDE_OFFSET);

	host->private_data = info;

	return ata_host_activate(host, irq ? gpio_to_irq(irq) : 0,
			irq ? ata_sff_interrupt : NULL,
			irq_flags, &pata_at91_sht);

err_put:
	clk_put(info->mck);
	return ret;
}

static int __devexit pata_at91_remove(struct platform_device *pdev)
{
	struct ata_host *host = dev_get_drvdata(&pdev->dev);
	struct at91_ide_info *info;

	if (!host)
		return 0;
	info = host->private_data;

	ata_host_detach(host);

	if (!info)
		return 0;

	clk_put(info->mck);

	return 0;
}

static struct platform_driver pata_at91_driver = {
	.probe		= pata_at91_probe,
	.remove		= __devexit_p(pata_at91_remove),
	.driver 	= {
		.name		= DRV_NAME,
		.owner		= THIS_MODULE,
	},
};

static int __init pata_at91_init(void)
{
	return platform_driver_register(&pata_at91_driver);
}

static void __exit pata_at91_exit(void)
{
	platform_driver_unregister(&pata_at91_driver);
}


module_init(pata_at91_init);
module_exit(pata_at91_exit);


MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Driver for CF in True IDE mode on AT91SAM9260 SoC");
MODULE_AUTHOR("Matyukevich Sergey");
MODULE_VERSION(DRV_VERSION);

