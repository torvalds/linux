/*
 * PATA driver for AT91SAM9260 Static Memory Controller
 * with CompactFlash interface in True IDE mode
 *
 * Copyright (C) 2009 Matyukevich Sergey
 *               2011 Igor Plyatov
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
#include <linux/platform_data/atmel.h>

#include <mach/at91sam9_smc.h>
#include <asm/gpio.h>

#define DRV_NAME		"pata_at91"
#define DRV_VERSION		"0.3"

#define CF_IDE_OFFSET		0x00c00000
#define CF_ALT_IDE_OFFSET	0x00e00000
#define CF_IDE_RES_SIZE		0x08
#define CS_PULSE_MAXIMUM	319
#define ER_SMC_CALC		1
#define ER_SMC_RECALC		2

struct at91_ide_info {
	unsigned long mode;
	unsigned int cs;
	struct clk *mck;
	void __iomem *ide_addr;
	void __iomem *alt_addr;
};

/**
 * struct smc_range - range of valid values for SMC register.
 */
struct smc_range {
	int min;
	int max;
};

/**
 * adjust_smc_value - adjust value for one of SMC registers.
 * @value: adjusted value
 * @range: array of SMC ranges with valid values
 * @size: SMC ranges array size
 *
 * This returns the difference between input and output value or negative
 * in case of invalid input value.
 * If negative returned, then output value = maximal possible from ranges.
 */
static int adjust_smc_value(int *value, struct smc_range *range, int size)
{
	int maximum = (range + size - 1)->max;
	int remainder;

	do {
		if (*value < range->min) {
			remainder = range->min - *value;
			*value = range->min; /* nearest valid value */
			return remainder;
		} else if ((range->min <= *value) && (*value <= range->max))
			return 0;

		range++;
	} while (--size);
	*value = maximum;

	return -1; /* invalid value */
}

/**
 * calc_smc_vals - calculate SMC register values
 * @dev: ATA device
 * @setup: SMC_SETUP register value
 * @pulse: SMC_PULSE register value
 * @cycle: SMC_CYCLE register value
 *
 * This returns negative in case of invalid values for SMC registers:
 * -ER_SMC_RECALC - recalculation required for SMC values,
 * -ER_SMC_CALC - calculation failed (invalid input values).
 *
 * SMC use special coding scheme, see "Coding and Range of Timing
 * Parameters" table from AT91SAM9 datasheets.
 *
 *	SMC_SETUP = 128*setup[5] + setup[4:0]
 *	SMC_PULSE = 256*pulse[6] + pulse[5:0]
 *	SMC_CYCLE = 256*cycle[8:7] + cycle[6:0]
 */
static int calc_smc_vals(struct device *dev,
		int *setup, int *pulse, int *cycle, int *cs_pulse)
{
	int ret_val;
	int err = 0;
	struct smc_range range_setup[] = {	/* SMC_SETUP valid values */
		{.min = 0,	.max = 31},	/* first  range */
		{.min = 128,	.max = 159}	/* second range */
	};
	struct smc_range range_pulse[] = {	/* SMC_PULSE valid values */
		{.min = 0,	.max = 63},	/* first  range */
		{.min = 256,	.max = 319}	/* second range */
	};
	struct smc_range range_cycle[] = {	/* SMC_CYCLE valid values */
		{.min = 0,	.max = 127},	/* first  range */
		{.min = 256,	.max = 383},	/* second range */
		{.min = 512,	.max = 639},	/* third  range */
		{.min = 768,	.max = 895}	/* fourth range */
	};

	ret_val = adjust_smc_value(setup, range_setup, ARRAY_SIZE(range_setup));
	if (ret_val < 0)
		dev_warn(dev, "maximal SMC Setup value\n");
	else
		*cycle += ret_val;

	ret_val = adjust_smc_value(pulse, range_pulse, ARRAY_SIZE(range_pulse));
	if (ret_val < 0)
		dev_warn(dev, "maximal SMC Pulse value\n");
	else
		*cycle += ret_val;

	ret_val = adjust_smc_value(cycle, range_cycle, ARRAY_SIZE(range_cycle));
	if (ret_val < 0)
		dev_warn(dev, "maximal SMC Cycle value\n");

	*cs_pulse = *cycle;
	if (*cs_pulse > CS_PULSE_MAXIMUM) {
		dev_err(dev, "unable to calculate valid SMC settings\n");
		return -ER_SMC_CALC;
	}

	ret_val = adjust_smc_value(cs_pulse, range_pulse,
					ARRAY_SIZE(range_pulse));
	if (ret_val < 0) {
		dev_warn(dev, "maximal SMC CS Pulse value\n");
	} else if (ret_val != 0) {
		*cycle = *cs_pulse;
		dev_warn(dev, "SMC Cycle extended\n");
		err = -ER_SMC_RECALC;
	}

	return err;
}

/**
 * to_smc_format - convert values into SMC format
 * @setup: SETUP value of SMC Setup Register
 * @pulse: PULSE value of SMC Pulse Register
 * @cycle: CYCLE value of SMC Cycle Register
 * @cs_pulse: NCS_PULSE value of SMC Pulse Register
 */
static void to_smc_format(int *setup, int *pulse, int *cycle, int *cs_pulse)
{
	*setup = (*setup & 0x1f) | ((*setup & 0x80) >> 2);
	*pulse = (*pulse & 0x3f) | ((*pulse & 0x100) >> 2);
	*cycle = (*cycle & 0x7f) | ((*cycle & 0x300) >> 1);
	*cs_pulse = (*cs_pulse & 0x3f) | ((*cs_pulse & 0x100) >> 2);
}

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

/**
 * set_smc_timing - SMC timings setup.
 * @dev: device
 * @info: AT91 IDE info
 * @ata: ATA timings
 *
 * Its assumed that write timings are same as read timings,
 * cs_setup = 0 and cs_pulse = cycle.
 */
static void set_smc_timing(struct device *dev, struct ata_device *adev,
		struct at91_ide_info *info, const struct ata_timing *ata)
{
	int ret = 0;
	int use_iordy;
	struct sam9_smc_config smc;
	unsigned int t6z;         /* data tristate time in ns */
	unsigned int cycle;       /* SMC Cycle width in MCK ticks */
	unsigned int setup;       /* SMC Setup width in MCK ticks */
	unsigned int pulse;       /* CFIOR and CFIOW pulse width in MCK ticks */
	unsigned int cs_pulse;    /* CS4 or CS5 pulse width in MCK ticks*/
	unsigned int tdf_cycles;  /* SMC TDF MCK ticks */
	unsigned long mck_hz;     /* MCK frequency in Hz */

	t6z = (ata->mode < XFER_PIO_5) ? 30 : 20;
	mck_hz = clk_get_rate(info->mck);
	cycle = calc_mck_cycles(ata->cyc8b, mck_hz);
	setup = calc_mck_cycles(ata->setup, mck_hz);
	pulse = calc_mck_cycles(ata->act8b, mck_hz);
	tdf_cycles = calc_mck_cycles(t6z, mck_hz);

	do {
		ret = calc_smc_vals(dev, &setup, &pulse, &cycle, &cs_pulse);
	} while (ret == -ER_SMC_RECALC);

	if (ret == -ER_SMC_CALC)
		dev_err(dev, "Interface may not operate correctly\n");

	dev_dbg(dev, "SMC Setup=%u, Pulse=%u, Cycle=%u, CS Pulse=%u\n",
		setup, pulse, cycle, cs_pulse);
	to_smc_format(&setup, &pulse, &cycle, &cs_pulse);
	/* disable or enable waiting for IORDY signal */
	use_iordy = ata_pio_need_iordy(adev);
	if (use_iordy)
		info->mode |= AT91_SMC_EXNWMODE_READY;

	if (tdf_cycles > 15) {
		tdf_cycles = 15;
		dev_warn(dev, "maximal SMC TDF Cycles value\n");
	}

	dev_dbg(dev, "Use IORDY=%u, TDF Cycles=%u\n", use_iordy, tdf_cycles);

	/* SMC Setup Register */
	smc.nwe_setup = smc.nrd_setup = setup;
	smc.ncs_write_setup = smc.ncs_read_setup = 0;
	/* SMC Pulse Register */
	smc.nwe_pulse = smc.nrd_pulse = pulse;
	smc.ncs_write_pulse = smc.ncs_read_pulse = cs_pulse;
	/* SMC Cycle Register */
	smc.write_cycle = smc.read_cycle = cycle;
	/* SMC Mode Register*/
	smc.tdf_cycles = tdf_cycles;
	smc.mode = info->mode;

	sam9_smc_configure(0, info->cs, &smc);
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
		timing = *ata_timing_find_mode(XFER_PIO_0);
	}
	set_smc_timing(ap->dev, adev, info, &timing);
}

static unsigned int pata_at91_data_xfer_noirq(struct ata_device *dev,
		unsigned char *buf, unsigned int buflen, int rw)
{
	struct at91_ide_info *info = dev->link->ap->host->private_data;
	unsigned int consumed;
	unsigned long flags;
	struct sam9_smc_config smc;

	local_irq_save(flags);
	sam9_smc_read_mode(0, info->cs, &smc);

	/* set 16bit mode before writing data */
	smc.mode = (smc.mode & ~AT91_SMC_DBW) | AT91_SMC_DBW_16;
	sam9_smc_write_mode(0, info->cs, &smc);

	consumed = ata_sff_data_xfer(dev, buf, buflen, rw);

	/* restore 8bit mode after data is written */
	smc.mode = (smc.mode & ~AT91_SMC_DBW) | AT91_SMC_DBW_8;
	sam9_smc_write_mode(0, info->cs, &smc);

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

static int pata_at91_probe(struct platform_device *pdev)
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

	if (!gpio_is_valid(irq)) {
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

	return ata_host_activate(host, gpio_is_valid(irq) ? gpio_to_irq(irq) : 0,
			gpio_is_valid(irq) ? ata_sff_interrupt : NULL,
			irq_flags, &pata_at91_sht);

	if (!ret)
		return 0;

err_put:
	clk_put(info->mck);
	return ret;
}

static int pata_at91_remove(struct platform_device *pdev)
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
	.remove		= pata_at91_remove,
	.driver		= {
		.name		= DRV_NAME,
		.owner		= THIS_MODULE,
	},
};

module_platform_driver(pata_at91_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Driver for CF in True IDE mode on AT91SAM9260 SoC");
MODULE_AUTHOR("Matyukevich Sergey");
MODULE_VERSION(DRV_VERSION);

