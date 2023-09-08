// SPDX-License-Identifier: GPL-2.0-only
/*
 * Faraday Technology FTIDE010 driver
 * Copyright (C) 2017 Linus Walleij <linus.walleij@linaro.org>
 *
 * Includes portions of the SL2312/SL3516/Gemini PATA driver
 * Copyright (C) 2003 StorLine, Inc <jason@storlink.com.tw>
 * Copyright (C) 2009 Janos Laube <janos.dev@gmail.com>
 * Copyright (C) 2010 Frederic Pecourt <opengemini@free.fr>
 * Copyright (C) 2011 Tobias Waldvogel <tobias.waldvogel@gmail.com>
 */

#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/libata.h>
#include <linux/bitops.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/clk.h>
#include "sata_gemini.h"

#define DRV_NAME "pata_ftide010"

/**
 * struct ftide010 - state container for the Faraday FTIDE010
 * @dev: pointer back to the device representing this controller
 * @base: remapped I/O space address
 * @pclk: peripheral clock for the IDE block
 * @host: pointer to the ATA host for this device
 * @master_cbl: master cable type
 * @slave_cbl: slave cable type
 * @sg: Gemini SATA bridge pointer, if running on the Gemini
 * @master_to_sata0: Gemini SATA bridge: the ATA master is connected
 * to the SATA0 bridge
 * @slave_to_sata0: Gemini SATA bridge: the ATA slave is connected
 * to the SATA0 bridge
 * @master_to_sata1: Gemini SATA bridge: the ATA master is connected
 * to the SATA1 bridge
 * @slave_to_sata1: Gemini SATA bridge: the ATA slave is connected
 * to the SATA1 bridge
 */
struct ftide010 {
	struct device *dev;
	void __iomem *base;
	struct clk *pclk;
	struct ata_host *host;
	unsigned int master_cbl;
	unsigned int slave_cbl;
	/* Gemini-specific properties */
	struct sata_gemini *sg;
	bool master_to_sata0;
	bool slave_to_sata0;
	bool master_to_sata1;
	bool slave_to_sata1;
};

#define FTIDE010_DMA_REG	0x00
#define FTIDE010_DMA_STATUS	0x02
#define FTIDE010_IDE_BMDTPR	0x04
#define FTIDE010_IDE_DEVICE_ID	0x08
#define FTIDE010_PIO_TIMING	0x10
#define FTIDE010_MWDMA_TIMING	0x11
#define FTIDE010_UDMA_TIMING0	0x12 /* Master */
#define FTIDE010_UDMA_TIMING1	0x13 /* Slave */
#define FTIDE010_CLK_MOD	0x14
/* These registers are mapped directly to the IDE registers */
#define FTIDE010_CMD_DATA	0x20
#define FTIDE010_ERROR_FEATURES	0x21
#define FTIDE010_NSECT		0x22
#define FTIDE010_LBAL		0x23
#define FTIDE010_LBAM		0x24
#define FTIDE010_LBAH		0x25
#define FTIDE010_DEVICE		0x26
#define FTIDE010_STATUS_COMMAND	0x27
#define FTIDE010_ALTSTAT_CTRL	0x36

/* Set this bit for UDMA mode 5 and 6 */
#define FTIDE010_UDMA_TIMING_MODE_56	BIT(7)

/* 0 = 50 MHz, 1 = 66 MHz */
#define FTIDE010_CLK_MOD_DEV0_CLK_SEL	BIT(0)
#define FTIDE010_CLK_MOD_DEV1_CLK_SEL	BIT(1)
/* Enable UDMA on a device */
#define FTIDE010_CLK_MOD_DEV0_UDMA_EN	BIT(4)
#define FTIDE010_CLK_MOD_DEV1_UDMA_EN	BIT(5)

static const struct scsi_host_template pata_ftide010_sht = {
	ATA_BMDMA_SHT(DRV_NAME),
};

/*
 * Bus timings
 *
 * The unit of the below required timings is two clock periods of the ATA
 * reference clock which is 30 nanoseconds per unit at 66MHz and 20
 * nanoseconds per unit at 50 MHz. The PIO timings assume 33MHz speed for
 * PIO.
 *
 * pio_active_time: array of 5 elements for T2 timing for Mode 0,
 * 1, 2, 3 and 4. Range 0..15.
 * pio_recovery_time: array of 5 elements for T2l timing for Mode 0,
 * 1, 2, 3 and 4. Range 0..15.
 * mdma_50_active_time: array of 4 elements for Td timing for multi
 * word DMA, Mode 0, 1, and 2 at 50 MHz. Range 0..15.
 * mdma_50_recovery_time: array of 4 elements for Tk timing for
 * multi word DMA, Mode 0, 1 and 2 at 50 MHz. Range 0..15.
 * mdma_66_active_time: array of 4 elements for Td timing for multi
 * word DMA, Mode 0, 1 and 2 at 66 MHz. Range 0..15.
 * mdma_66_recovery_time: array of 4 elements for Tk timing for
 * multi word DMA, Mode 0, 1 and 2 at 66 MHz. Range 0..15.
 * udma_50_setup_time: array of 4 elements for Tvds timing for ultra
 * DMA, Mode 0, 1, 2, 3, 4 and 5 at 50 MHz. Range 0..7.
 * udma_50_hold_time: array of 4 elements for Tdvh timing for
 * multi word DMA, Mode 0, 1, 2, 3, 4 and 5 at 50 MHz, Range 0..7.
 * udma_66_setup_time: array of 4 elements for Tvds timing for multi
 * word DMA, Mode 0, 1, 2, 3, 4, 5 and 6 at 66 MHz. Range 0..7.
 * udma_66_hold_time: array of 4 elements for Tdvh timing for
 * multi word DMA, Mode 0, 1, 2, 3, 4, 5 and 6 at 66 MHz. Range 0..7.
 */
static const u8 pio_active_time[5] = {10, 10, 10, 3, 3};
static const u8 pio_recovery_time[5] = {10, 3, 1, 3, 1};
static const u8 mwdma_50_active_time[3] = {6, 2, 2};
static const u8 mwdma_50_recovery_time[3] = {6, 2, 1};
static const u8 mwdma_66_active_time[3] = {8, 3, 3};
static const u8 mwdma_66_recovery_time[3] = {8, 2, 1};
static const u8 udma_50_setup_time[6] = {3, 3, 2, 2, 1, 1};
static const u8 udma_50_hold_time[6] = {3, 1, 1, 1, 1, 1};
static const u8 udma_66_setup_time[7] = {4, 4, 3, 2, };
static const u8 udma_66_hold_time[7] = {};

/*
 * We set 66 MHz for all MWDMA modes
 */
static const bool set_mdma_66_mhz[] = { true, true, true, true };

/*
 * We set 66 MHz for UDMA modes 3, 4 and 6 and no others
 */
static const bool set_udma_66_mhz[] = { false, false, false, true, true, false, true };

static void ftide010_set_dmamode(struct ata_port *ap, struct ata_device *adev)
{
	struct ftide010 *ftide = ap->host->private_data;
	u8 speed = adev->dma_mode;
	u8 devno = adev->devno & 1;
	u8 udma_en_mask;
	u8 f66m_en_mask;
	u8 clkreg;
	u8 timreg;
	u8 i;

	/* Target device 0 (master) or 1 (slave) */
	if (!devno) {
		udma_en_mask = FTIDE010_CLK_MOD_DEV0_UDMA_EN;
		f66m_en_mask = FTIDE010_CLK_MOD_DEV0_CLK_SEL;
	} else {
		udma_en_mask = FTIDE010_CLK_MOD_DEV1_UDMA_EN;
		f66m_en_mask = FTIDE010_CLK_MOD_DEV1_CLK_SEL;
	}

	clkreg = readb(ftide->base + FTIDE010_CLK_MOD);
	clkreg &= ~udma_en_mask;
	clkreg &= ~f66m_en_mask;

	if (speed & XFER_UDMA_0) {
		i = speed & ~XFER_UDMA_0;
		dev_dbg(ftide->dev, "set UDMA mode %02x, index %d\n",
			speed, i);

		clkreg |= udma_en_mask;
		if (set_udma_66_mhz[i]) {
			clkreg |= f66m_en_mask;
			timreg = udma_66_setup_time[i] << 4 |
				udma_66_hold_time[i];
		} else {
			timreg = udma_50_setup_time[i] << 4 |
				udma_50_hold_time[i];
		}

		/* A special bit needs to be set for modes 5 and 6 */
		if (i >= 5)
			timreg |= FTIDE010_UDMA_TIMING_MODE_56;

		dev_dbg(ftide->dev, "UDMA write clkreg = %02x, timreg = %02x\n",
			clkreg, timreg);

		writeb(clkreg, ftide->base + FTIDE010_CLK_MOD);
		writeb(timreg, ftide->base + FTIDE010_UDMA_TIMING0 + devno);
	} else {
		i = speed & ~XFER_MW_DMA_0;
		dev_dbg(ftide->dev, "set MWDMA mode %02x, index %d\n",
			speed, i);

		if (set_mdma_66_mhz[i]) {
			clkreg |= f66m_en_mask;
			timreg = mwdma_66_active_time[i] << 4 |
				mwdma_66_recovery_time[i];
		} else {
			timreg = mwdma_50_active_time[i] << 4 |
				mwdma_50_recovery_time[i];
		}
		dev_dbg(ftide->dev,
			"MWDMA write clkreg = %02x, timreg = %02x\n",
			clkreg, timreg);
		/* This will affect all devices */
		writeb(clkreg, ftide->base + FTIDE010_CLK_MOD);
		writeb(timreg, ftide->base + FTIDE010_MWDMA_TIMING);
	}

	/*
	 * Store the current device (master or slave) in ap->private_data
	 * so that .qc_issue() can detect if this changes and reprogram
	 * the DMA settings.
	 */
	ap->private_data = adev;

	return;
}

static void ftide010_set_piomode(struct ata_port *ap, struct ata_device *adev)
{
	struct ftide010 *ftide = ap->host->private_data;
	u8 pio = adev->pio_mode - XFER_PIO_0;

	dev_dbg(ftide->dev, "set PIO mode %02x, index %d\n",
		adev->pio_mode, pio);
	writeb(pio_active_time[pio] << 4 | pio_recovery_time[pio],
	       ftide->base + FTIDE010_PIO_TIMING);
}

/*
 * We implement our own qc_issue() callback since we may need to set up
 * the timings differently for master and slave transfers: the CLK_MOD_REG
 * and MWDMA_TIMING_REG is shared between master and slave, so reprogramming
 * this may be necessary.
 */
static unsigned int ftide010_qc_issue(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	struct ata_device *adev = qc->dev;

	/*
	 * If the device changed, i.e. slave->master, master->slave,
	 * then set up the DMA mode again so we are sure the timings
	 * are correct.
	 */
	if (adev != ap->private_data && ata_dma_enabled(adev))
		ftide010_set_dmamode(ap, adev);

	return ata_bmdma_qc_issue(qc);
}

static struct ata_port_operations pata_ftide010_port_ops = {
	.inherits	= &ata_bmdma_port_ops,
	.set_dmamode	= ftide010_set_dmamode,
	.set_piomode	= ftide010_set_piomode,
	.qc_issue	= ftide010_qc_issue,
};

static struct ata_port_info ftide010_port_info = {
	.flags		= ATA_FLAG_SLAVE_POSS,
	.mwdma_mask	= ATA_MWDMA2,
	.udma_mask	= ATA_UDMA6,
	.pio_mask	= ATA_PIO4,
	.port_ops	= &pata_ftide010_port_ops,
};

#if IS_ENABLED(CONFIG_SATA_GEMINI)

static int pata_ftide010_gemini_port_start(struct ata_port *ap)
{
	struct ftide010 *ftide = ap->host->private_data;
	struct device *dev = ftide->dev;
	struct sata_gemini *sg = ftide->sg;
	int bridges = 0;
	int ret;

	ret = ata_bmdma_port_start(ap);
	if (ret)
		return ret;

	if (ftide->master_to_sata0) {
		dev_info(dev, "SATA0 (master) start\n");
		ret = gemini_sata_start_bridge(sg, 0);
		if (!ret)
			bridges++;
	}
	if (ftide->master_to_sata1) {
		dev_info(dev, "SATA1 (master) start\n");
		ret = gemini_sata_start_bridge(sg, 1);
		if (!ret)
			bridges++;
	}
	/* Avoid double-starting */
	if (ftide->slave_to_sata0 && !ftide->master_to_sata0) {
		dev_info(dev, "SATA0 (slave) start\n");
		ret = gemini_sata_start_bridge(sg, 0);
		if (!ret)
			bridges++;
	}
	/* Avoid double-starting */
	if (ftide->slave_to_sata1 && !ftide->master_to_sata1) {
		dev_info(dev, "SATA1 (slave) start\n");
		ret = gemini_sata_start_bridge(sg, 1);
		if (!ret)
			bridges++;
	}

	dev_info(dev, "brought %d bridges online\n", bridges);
	return (bridges > 0) ? 0 : -EINVAL; // -ENODEV;
}

static void pata_ftide010_gemini_port_stop(struct ata_port *ap)
{
	struct ftide010 *ftide = ap->host->private_data;
	struct device *dev = ftide->dev;
	struct sata_gemini *sg = ftide->sg;

	if (ftide->master_to_sata0) {
		dev_info(dev, "SATA0 (master) stop\n");
		gemini_sata_stop_bridge(sg, 0);
	}
	if (ftide->master_to_sata1) {
		dev_info(dev, "SATA1 (master) stop\n");
		gemini_sata_stop_bridge(sg, 1);
	}
	/* Avoid double-stopping */
	if (ftide->slave_to_sata0 && !ftide->master_to_sata0) {
		dev_info(dev, "SATA0 (slave) stop\n");
		gemini_sata_stop_bridge(sg, 0);
	}
	/* Avoid double-stopping */
	if (ftide->slave_to_sata1 && !ftide->master_to_sata1) {
		dev_info(dev, "SATA1 (slave) stop\n");
		gemini_sata_stop_bridge(sg, 1);
	}
}

static int pata_ftide010_gemini_cable_detect(struct ata_port *ap)
{
	struct ftide010 *ftide = ap->host->private_data;

	/*
	 * Return the master cable, I have no clue how to return a different
	 * cable for the slave than for the master.
	 */
	return ftide->master_cbl;
}

static int pata_ftide010_gemini_init(struct ftide010 *ftide,
				     struct ata_port_info *pi,
				     bool is_ata1)
{
	struct device *dev = ftide->dev;
	struct sata_gemini *sg;
	enum gemini_muxmode muxmode;

	/* Look up SATA bridge */
	sg = gemini_sata_bridge_get();
	if (IS_ERR(sg))
		return PTR_ERR(sg);
	ftide->sg = sg;

	muxmode = gemini_sata_get_muxmode(sg);

	/* Special ops */
	pata_ftide010_port_ops.port_start =
		pata_ftide010_gemini_port_start;
	pata_ftide010_port_ops.port_stop =
		pata_ftide010_gemini_port_stop;
	pata_ftide010_port_ops.cable_detect =
		pata_ftide010_gemini_cable_detect;

	/* Flag port as SATA-capable */
	if (gemini_sata_bridge_enabled(sg, is_ata1))
		pi->flags |= ATA_FLAG_SATA;

	/* This device has broken DMA, only PIO works */
	if (of_machine_is_compatible("itian,sq201")) {
		pi->mwdma_mask = 0;
		pi->udma_mask = 0;
	}

	/*
	 * We assume that a simple 40-wire cable is used in the PATA mode.
	 * if you're adding a system using the PATA interface, make sure
	 * the right cable is set up here, it might be necessary to use
	 * special hardware detection or encode the cable type in the device
	 * tree with special properties.
	 */
	if (!is_ata1) {
		switch (muxmode) {
		case GEMINI_MUXMODE_0:
			ftide->master_cbl = ATA_CBL_SATA;
			ftide->slave_cbl = ATA_CBL_PATA40;
			ftide->master_to_sata0 = true;
			break;
		case GEMINI_MUXMODE_1:
			ftide->master_cbl = ATA_CBL_SATA;
			ftide->slave_cbl = ATA_CBL_NONE;
			ftide->master_to_sata0 = true;
			break;
		case GEMINI_MUXMODE_2:
			ftide->master_cbl = ATA_CBL_PATA40;
			ftide->slave_cbl = ATA_CBL_PATA40;
			break;
		case GEMINI_MUXMODE_3:
			ftide->master_cbl = ATA_CBL_SATA;
			ftide->slave_cbl = ATA_CBL_SATA;
			ftide->master_to_sata0 = true;
			ftide->slave_to_sata1 = true;
			break;
		}
	} else {
		switch (muxmode) {
		case GEMINI_MUXMODE_0:
			ftide->master_cbl = ATA_CBL_SATA;
			ftide->slave_cbl = ATA_CBL_NONE;
			ftide->master_to_sata1 = true;
			break;
		case GEMINI_MUXMODE_1:
			ftide->master_cbl = ATA_CBL_SATA;
			ftide->slave_cbl = ATA_CBL_PATA40;
			ftide->master_to_sata1 = true;
			break;
		case GEMINI_MUXMODE_2:
			ftide->master_cbl = ATA_CBL_SATA;
			ftide->slave_cbl = ATA_CBL_SATA;
			ftide->slave_to_sata0 = true;
			ftide->master_to_sata1 = true;
			break;
		case GEMINI_MUXMODE_3:
			ftide->master_cbl = ATA_CBL_PATA40;
			ftide->slave_cbl = ATA_CBL_PATA40;
			break;
		}
	}
	dev_info(dev, "set up Gemini PATA%d\n", is_ata1);

	return 0;
}
#else
static int pata_ftide010_gemini_init(struct ftide010 *ftide,
				     struct ata_port_info *pi,
				     bool is_ata1)
{
	return -ENOTSUPP;
}
#endif


static int pata_ftide010_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct ata_port_info pi = ftide010_port_info;
	const struct ata_port_info *ppi[] = { &pi, NULL };
	struct ftide010 *ftide;
	struct resource *res;
	int irq;
	int ret;
	int i;

	ftide = devm_kzalloc(dev, sizeof(*ftide), GFP_KERNEL);
	if (!ftide)
		return -ENOMEM;
	ftide->dev = dev;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENODEV;

	ftide->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(ftide->base))
		return PTR_ERR(ftide->base);

	ftide->pclk = devm_clk_get(dev, "PCLK");
	if (!IS_ERR(ftide->pclk)) {
		ret = clk_prepare_enable(ftide->pclk);
		if (ret) {
			dev_err(dev, "failed to enable PCLK\n");
			return ret;
		}
	}

	/* Some special Cortina Gemini init, if needed */
	if (of_device_is_compatible(np, "cortina,gemini-pata")) {
		/*
		 * We need to know which instance is probing (the
		 * Gemini has two instances of FTIDE010) and we do
		 * this simply by looking at the physical base
		 * address, which is 0x63400000 for ATA1, else we
		 * are ATA0. This will also set up the cable types.
		 */
		ret = pata_ftide010_gemini_init(ftide,
				&pi,
				(res->start == 0x63400000));
		if (ret)
			goto err_dis_clk;
	} else {
		/* Else assume we are connected using PATA40 */
		ftide->master_cbl = ATA_CBL_PATA40;
		ftide->slave_cbl = ATA_CBL_PATA40;
	}

	ftide->host = ata_host_alloc_pinfo(dev, ppi, 1);
	if (!ftide->host) {
		ret = -ENOMEM;
		goto err_dis_clk;
	}
	ftide->host->private_data = ftide;

	for (i = 0; i < ftide->host->n_ports; i++) {
		struct ata_port *ap = ftide->host->ports[i];
		struct ata_ioports *ioaddr = &ap->ioaddr;

		ioaddr->bmdma_addr = ftide->base + FTIDE010_DMA_REG;
		ioaddr->cmd_addr = ftide->base + FTIDE010_CMD_DATA;
		ioaddr->ctl_addr = ftide->base + FTIDE010_ALTSTAT_CTRL;
		ioaddr->altstatus_addr = ftide->base + FTIDE010_ALTSTAT_CTRL;
		ata_sff_std_ports(ioaddr);
	}

	dev_info(dev, "device ID %08x, irq %d, reg %pR\n",
		 readl(ftide->base + FTIDE010_IDE_DEVICE_ID), irq, res);

	ret = ata_host_activate(ftide->host, irq, ata_bmdma_interrupt,
				0, &pata_ftide010_sht);
	if (ret)
		goto err_dis_clk;

	return 0;

err_dis_clk:
	clk_disable_unprepare(ftide->pclk);

	return ret;
}

static int pata_ftide010_remove(struct platform_device *pdev)
{
	struct ata_host *host = platform_get_drvdata(pdev);
	struct ftide010 *ftide = host->private_data;

	ata_host_detach(ftide->host);
	clk_disable_unprepare(ftide->pclk);

	return 0;
}

static const struct of_device_id pata_ftide010_of_match[] = {
	{ .compatible = "faraday,ftide010", },
	{ /* sentinel */ }
};

static struct platform_driver pata_ftide010_driver = {
	.driver = {
		.name = DRV_NAME,
		.of_match_table = pata_ftide010_of_match,
	},
	.probe = pata_ftide010_probe,
	.remove = pata_ftide010_remove,
};
module_platform_driver(pata_ftide010_driver);

MODULE_AUTHOR("Linus Walleij <linus.walleij@linaro.org>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRV_NAME);
