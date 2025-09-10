// SPDX-License-Identifier: GPL-2.0-only
/*
 * EP93XX PATA controller driver.
 *
 * Copyright (c) 2012, Metasoft s.c.
 *	Rafal Prylowski <prylowski@metasoft.pl>
 *
 * Based on pata_scc.c, pata_icside.c and on earlier version of EP93XX
 * PATA driver by Lennert Buytenhek and Alessandro Zummo.
 * Read/Write timings, resource management and other improvements
 * from driver by Joao Ramos and Bartlomiej Zolnierkiewicz.
 * DMA engine support based on spi-ep93xx.c by Mika Westerberg.
 *
 * Original copyrights:
 *
 * Support for Cirrus Logic's EP93xx (EP9312, EP9315) CPUs
 * PATA host controller driver.
 *
 * Copyright (c) 2009, Bartlomiej Zolnierkiewicz
 *
 * Heavily based on the ep93xx-ide.c driver:
 *
 * Copyright (c) 2009, Joao Ramos <joao.ramos@inov.pt>
 *		      INESC Inovacao (INOV)
 *
 * EP93XX PATA controller driver.
 * Copyright (C) 2007 Lennert Buytenhek <buytenh@wantstofly.org>
 *
 * An ATA driver for the Cirrus Logic EP93xx PATA controller.
 *
 * Based on an earlier version by Alessandro Zummo, which is:
 *   Copyright (C) 2006 Tower Technologies
 */

#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/blkdev.h>
#include <scsi/scsi_host.h>
#include <linux/ata.h>
#include <linux/libata.h>
#include <linux/platform_device.h>
#include <linux/sys_soc.h>
#include <linux/delay.h>
#include <linux/dmaengine.h>
#include <linux/ktime.h>
#include <linux/mod_devicetable.h>

#include <linux/soc/cirrus/ep93xx.h>

#define DRV_NAME	"ep93xx-ide"
#define DRV_VERSION	"1.0"

enum {
	/* IDE Control Register */
	IDECTRL				= 0x00,
	IDECTRL_CS0N			= (1 << 0),
	IDECTRL_CS1N			= (1 << 1),
	IDECTRL_DIORN			= (1 << 5),
	IDECTRL_DIOWN			= (1 << 6),
	IDECTRL_INTRQ			= (1 << 9),
	IDECTRL_IORDY			= (1 << 10),
	/*
	 * the device IDE register to be accessed is selected through
	 * IDECTRL register's specific bitfields 'DA', 'CS1N' and 'CS0N':
	 *   b4   b3   b2    b1     b0
	 *   A2   A1   A0   CS1N   CS0N
	 * the values filled in this structure allows the value to be directly
	 * ORed to the IDECTRL register, hence giving directly the A[2:0] and
	 * CS1N/CS0N values for each IDE register.
	 * The values correspond to the transformation:
	 *   ((real IDE address) << 2) | CS1N value << 1 | CS0N value
	 */
	IDECTRL_ADDR_CMD		= 0 + 2, /* CS1 */
	IDECTRL_ADDR_DATA		= (ATA_REG_DATA << 2) + 2,
	IDECTRL_ADDR_ERROR		= (ATA_REG_ERR << 2) + 2,
	IDECTRL_ADDR_FEATURE		= (ATA_REG_FEATURE << 2) + 2,
	IDECTRL_ADDR_NSECT		= (ATA_REG_NSECT << 2) + 2,
	IDECTRL_ADDR_LBAL		= (ATA_REG_LBAL << 2) + 2,
	IDECTRL_ADDR_LBAM		= (ATA_REG_LBAM << 2) + 2,
	IDECTRL_ADDR_LBAH		= (ATA_REG_LBAH << 2) + 2,
	IDECTRL_ADDR_DEVICE		= (ATA_REG_DEVICE << 2) + 2,
	IDECTRL_ADDR_STATUS		= (ATA_REG_STATUS << 2) + 2,
	IDECTRL_ADDR_COMMAND		= (ATA_REG_CMD << 2) + 2,
	IDECTRL_ADDR_ALTSTATUS		= (0x06 << 2) + 1, /* CS0 */
	IDECTRL_ADDR_CTL		= (0x06 << 2) + 1, /* CS0 */

	/* IDE Configuration Register */
	IDECFG				= 0x04,
	IDECFG_IDEEN			= (1 << 0),
	IDECFG_PIO			= (1 << 1),
	IDECFG_MDMA			= (1 << 2),
	IDECFG_UDMA			= (1 << 3),
	IDECFG_MODE_SHIFT		= 4,
	IDECFG_MODE_MASK		= (0xf << 4),
	IDECFG_WST_SHIFT		= 8,
	IDECFG_WST_MASK			= (0x3 << 8),

	/* MDMA Operation Register */
	IDEMDMAOP			= 0x08,

	/* UDMA Operation Register */
	IDEUDMAOP			= 0x0c,
	IDEUDMAOP_UEN			= (1 << 0),
	IDEUDMAOP_RWOP			= (1 << 1),

	/* PIO/MDMA/UDMA Data Registers */
	IDEDATAOUT			= 0x10,
	IDEDATAIN			= 0x14,
	IDEMDMADATAOUT			= 0x18,
	IDEMDMADATAIN			= 0x1c,
	IDEUDMADATAOUT			= 0x20,
	IDEUDMADATAIN			= 0x24,

	/* UDMA Status Register */
	IDEUDMASTS			= 0x28,
	IDEUDMASTS_DMAIDE		= (1 << 16),
	IDEUDMASTS_INTIDE		= (1 << 17),
	IDEUDMASTS_SBUSY		= (1 << 18),
	IDEUDMASTS_NDO			= (1 << 24),
	IDEUDMASTS_NDI			= (1 << 25),
	IDEUDMASTS_N4X			= (1 << 26),

	/* UDMA Debug Status Register */
	IDEUDMADEBUG			= 0x2c,
};

struct ep93xx_pata_data {
	struct platform_device *pdev;
	void __iomem *ide_base;
	struct ata_timing t;
	bool iordy;

	unsigned long udma_in_phys;
	unsigned long udma_out_phys;

	struct dma_chan *dma_rx_channel;
	struct dma_chan *dma_tx_channel;
};

static void ep93xx_pata_clear_regs(void __iomem *base)
{
	writel(IDECTRL_CS0N | IDECTRL_CS1N | IDECTRL_DIORN |
		IDECTRL_DIOWN, base + IDECTRL);

	writel(0, base + IDECFG);
	writel(0, base + IDEMDMAOP);
	writel(0, base + IDEUDMAOP);
	writel(0, base + IDEDATAOUT);
	writel(0, base + IDEDATAIN);
	writel(0, base + IDEMDMADATAOUT);
	writel(0, base + IDEMDMADATAIN);
	writel(0, base + IDEUDMADATAOUT);
	writel(0, base + IDEUDMADATAIN);
	writel(0, base + IDEUDMADEBUG);
}

static bool ep93xx_pata_check_iordy(void __iomem *base)
{
	return !!(readl(base + IDECTRL) & IDECTRL_IORDY);
}

/*
 * According to EP93xx User's Guide, WST field of IDECFG specifies number
 * of HCLK cycles to hold the data bus after a PIO write operation.
 * It should be programmed to guarantee following delays:
 *
 * PIO Mode   [ns]
 * 0          30
 * 1          20
 * 2          15
 * 3          10
 * 4          5
 *
 * Maximum possible value for HCLK is 100MHz.
 */
static int ep93xx_pata_get_wst(int pio_mode)
{
	int val;

	if (pio_mode == 0)
		val = 3;
	else if (pio_mode < 3)
		val = 2;
	else
		val = 1;

	return val << IDECFG_WST_SHIFT;
}

static void ep93xx_pata_enable_pio(void __iomem *base, int pio_mode)
{
	writel(IDECFG_IDEEN | IDECFG_PIO |
		ep93xx_pata_get_wst(pio_mode) |
		(pio_mode << IDECFG_MODE_SHIFT), base + IDECFG);
}

/*
 * Based on delay loop found in mach-pxa/mp900.c.
 *
 * Single iteration should take 5 cpu cycles. This is 25ns assuming the
 * fastest ep93xx cpu speed (200MHz) and is better optimized for PIO4 timings
 * than eg. 20ns.
 */
static void ep93xx_pata_delay(unsigned long count)
{
	__asm__ volatile (
		"0:\n"
		"mov r0, r0\n"
		"subs %0, %1, #1\n"
		"bge 0b\n"
		: "=r" (count)
		: "0" (count)
	);
}

static unsigned long ep93xx_pata_wait_for_iordy(void __iomem *base,
						unsigned long t2)
{
	/*
	 * According to ATA specification, IORDY pin can be first sampled
	 * tA = 35ns after activation of DIOR-/DIOW-. Maximum IORDY pulse
	 * width is tB = 1250ns.
	 *
	 * We are already t2 delay loop iterations after activation of
	 * DIOR-/DIOW-, so we set timeout to (1250 + 35) / 25 - t2 additional
	 * delay loop iterations.
	 */
	unsigned long start = (1250 + 35) / 25 - t2;
	unsigned long counter = start;

	while (!ep93xx_pata_check_iordy(base) && counter--)
		ep93xx_pata_delay(1);
	return start - counter;
}

/* common part at start of ep93xx_pata_read/write() */
static void ep93xx_pata_rw_begin(void __iomem *base, unsigned long addr,
				 unsigned long t1)
{
	writel(IDECTRL_DIOWN | IDECTRL_DIORN | addr, base + IDECTRL);
	ep93xx_pata_delay(t1);
}

/* common part at end of ep93xx_pata_read/write() */
static void ep93xx_pata_rw_end(void __iomem *base, unsigned long addr,
			       bool iordy, unsigned long t0, unsigned long t2,
			       unsigned long t2i)
{
	ep93xx_pata_delay(t2);
	/* lengthen t2 if needed */
	if (iordy)
		t2 += ep93xx_pata_wait_for_iordy(base, t2);
	writel(IDECTRL_DIOWN | IDECTRL_DIORN | addr, base + IDECTRL);
	if (t0 > t2 && t0 - t2 > t2i)
		ep93xx_pata_delay(t0 - t2);
	else
		ep93xx_pata_delay(t2i);
}

static u16 ep93xx_pata_read(struct ep93xx_pata_data *drv_data,
			    unsigned long addr,
			    bool reg)
{
	void __iomem *base = drv_data->ide_base;
	const struct ata_timing *t = &drv_data->t;
	unsigned long t0 = reg ? t->cyc8b : t->cycle;
	unsigned long t2 = reg ? t->act8b : t->active;
	unsigned long t2i = reg ? t->rec8b : t->recover;

	ep93xx_pata_rw_begin(base, addr, t->setup);
	writel(IDECTRL_DIOWN | addr, base + IDECTRL);
	/*
	 * The IDEDATAIN register is loaded from the DD pins at the positive
	 * edge of the DIORN signal. (EP93xx UG p27-14)
	 */
	ep93xx_pata_rw_end(base, addr, drv_data->iordy, t0, t2, t2i);
	return readl(base + IDEDATAIN);
}

/* IDE register read */
static u16 ep93xx_pata_read_reg(struct ep93xx_pata_data *drv_data,
				unsigned long addr)
{
	return ep93xx_pata_read(drv_data, addr, true);
}

/* PIO data read */
static u16 ep93xx_pata_read_data(struct ep93xx_pata_data *drv_data,
				 unsigned long addr)
{
	return ep93xx_pata_read(drv_data, addr, false);
}

static void ep93xx_pata_write(struct ep93xx_pata_data *drv_data,
			      u16 value, unsigned long addr,
			      bool reg)
{
	void __iomem *base = drv_data->ide_base;
	const struct ata_timing *t = &drv_data->t;
	unsigned long t0 = reg ? t->cyc8b : t->cycle;
	unsigned long t2 = reg ? t->act8b : t->active;
	unsigned long t2i = reg ? t->rec8b : t->recover;

	ep93xx_pata_rw_begin(base, addr, t->setup);
	/*
	 * Value from IDEDATAOUT register is driven onto the DD pins when
	 * DIOWN is low. (EP93xx UG p27-13)
	 */
	writel(value, base + IDEDATAOUT);
	writel(IDECTRL_DIORN | addr, base + IDECTRL);
	ep93xx_pata_rw_end(base, addr, drv_data->iordy, t0, t2, t2i);
}

/* IDE register write */
static void ep93xx_pata_write_reg(struct ep93xx_pata_data *drv_data,
				  u16 value, unsigned long addr)
{
	ep93xx_pata_write(drv_data, value, addr, true);
}

/* PIO data write */
static void ep93xx_pata_write_data(struct ep93xx_pata_data *drv_data,
				   u16 value, unsigned long addr)
{
	ep93xx_pata_write(drv_data, value, addr, false);
}

static void ep93xx_pata_set_piomode(struct ata_port *ap,
				    struct ata_device *adev)
{
	struct ep93xx_pata_data *drv_data = ap->host->private_data;
	struct ata_device *pair = ata_dev_pair(adev);
	/*
	 * Calculate timings for the delay loop, assuming ep93xx cpu speed
	 * is 200MHz (maximum possible for ep93xx). If actual cpu speed is
	 * slower, we will wait a bit longer in each delay.
	 * Additional division of cpu speed by 5, because single iteration
	 * of our delay loop takes 5 cpu cycles (25ns).
	 */
	unsigned long T = 1000000 / (200 / 5);

	ata_timing_compute(adev, adev->pio_mode, &drv_data->t, T, 0);
	if (pair && pair->pio_mode) {
		struct ata_timing t;
		ata_timing_compute(pair, pair->pio_mode, &t, T, 0);
		ata_timing_merge(&t, &drv_data->t, &drv_data->t,
			ATA_TIMING_SETUP | ATA_TIMING_8BIT);
	}
	drv_data->iordy = ata_pio_need_iordy(adev);

	ep93xx_pata_enable_pio(drv_data->ide_base,
			       adev->pio_mode - XFER_PIO_0);
}

/* Note: original code is ata_sff_check_status */
static u8 ep93xx_pata_check_status(struct ata_port *ap)
{
	struct ep93xx_pata_data *drv_data = ap->host->private_data;

	return ep93xx_pata_read_reg(drv_data, IDECTRL_ADDR_STATUS);
}

static u8 ep93xx_pata_check_altstatus(struct ata_port *ap)
{
	struct ep93xx_pata_data *drv_data = ap->host->private_data;

	return ep93xx_pata_read_reg(drv_data, IDECTRL_ADDR_ALTSTATUS);
}

/* Note: original code is ata_sff_tf_load */
static void ep93xx_pata_tf_load(struct ata_port *ap,
				const struct ata_taskfile *tf)
{
	struct ep93xx_pata_data *drv_data = ap->host->private_data;
	unsigned int is_addr = tf->flags & ATA_TFLAG_ISADDR;

	if (tf->ctl != ap->last_ctl) {
		ep93xx_pata_write_reg(drv_data, tf->ctl, IDECTRL_ADDR_CTL);
		ap->last_ctl = tf->ctl;
		ata_wait_idle(ap);
	}

	if (is_addr && (tf->flags & ATA_TFLAG_LBA48)) {
		ep93xx_pata_write_reg(drv_data, tf->hob_feature,
			IDECTRL_ADDR_FEATURE);
		ep93xx_pata_write_reg(drv_data, tf->hob_nsect,
			IDECTRL_ADDR_NSECT);
		ep93xx_pata_write_reg(drv_data, tf->hob_lbal,
			IDECTRL_ADDR_LBAL);
		ep93xx_pata_write_reg(drv_data, tf->hob_lbam,
			IDECTRL_ADDR_LBAM);
		ep93xx_pata_write_reg(drv_data, tf->hob_lbah,
			IDECTRL_ADDR_LBAH);
	}

	if (is_addr) {
		ep93xx_pata_write_reg(drv_data, tf->feature,
			IDECTRL_ADDR_FEATURE);
		ep93xx_pata_write_reg(drv_data, tf->nsect, IDECTRL_ADDR_NSECT);
		ep93xx_pata_write_reg(drv_data, tf->lbal, IDECTRL_ADDR_LBAL);
		ep93xx_pata_write_reg(drv_data, tf->lbam, IDECTRL_ADDR_LBAM);
		ep93xx_pata_write_reg(drv_data, tf->lbah, IDECTRL_ADDR_LBAH);
	}

	if (tf->flags & ATA_TFLAG_DEVICE)
		ep93xx_pata_write_reg(drv_data, tf->device,
			IDECTRL_ADDR_DEVICE);

	ata_wait_idle(ap);
}

/* Note: original code is ata_sff_tf_read */
static void ep93xx_pata_tf_read(struct ata_port *ap, struct ata_taskfile *tf)
{
	struct ep93xx_pata_data *drv_data = ap->host->private_data;

	tf->status = ep93xx_pata_check_status(ap);
	tf->error = ep93xx_pata_read_reg(drv_data, IDECTRL_ADDR_FEATURE);
	tf->nsect = ep93xx_pata_read_reg(drv_data, IDECTRL_ADDR_NSECT);
	tf->lbal = ep93xx_pata_read_reg(drv_data, IDECTRL_ADDR_LBAL);
	tf->lbam = ep93xx_pata_read_reg(drv_data, IDECTRL_ADDR_LBAM);
	tf->lbah = ep93xx_pata_read_reg(drv_data, IDECTRL_ADDR_LBAH);
	tf->device = ep93xx_pata_read_reg(drv_data, IDECTRL_ADDR_DEVICE);

	if (tf->flags & ATA_TFLAG_LBA48) {
		ep93xx_pata_write_reg(drv_data, tf->ctl | ATA_HOB,
			IDECTRL_ADDR_CTL);
		tf->hob_feature = ep93xx_pata_read_reg(drv_data,
			IDECTRL_ADDR_FEATURE);
		tf->hob_nsect = ep93xx_pata_read_reg(drv_data,
			IDECTRL_ADDR_NSECT);
		tf->hob_lbal = ep93xx_pata_read_reg(drv_data,
			IDECTRL_ADDR_LBAL);
		tf->hob_lbam = ep93xx_pata_read_reg(drv_data,
			IDECTRL_ADDR_LBAM);
		tf->hob_lbah = ep93xx_pata_read_reg(drv_data,
			IDECTRL_ADDR_LBAH);
		ep93xx_pata_write_reg(drv_data, tf->ctl, IDECTRL_ADDR_CTL);
		ap->last_ctl = tf->ctl;
	}
}

/* Note: original code is ata_sff_exec_command */
static void ep93xx_pata_exec_command(struct ata_port *ap,
				     const struct ata_taskfile *tf)
{
	struct ep93xx_pata_data *drv_data = ap->host->private_data;

	ep93xx_pata_write_reg(drv_data, tf->command,
			  IDECTRL_ADDR_COMMAND);
	ata_sff_pause(ap);
}

/* Note: original code is ata_sff_dev_select */
static void ep93xx_pata_dev_select(struct ata_port *ap, unsigned int device)
{
	struct ep93xx_pata_data *drv_data = ap->host->private_data;
	u8 tmp = ATA_DEVICE_OBS;

	if (device != 0)
		tmp |= ATA_DEV1;

	ep93xx_pata_write_reg(drv_data, tmp, IDECTRL_ADDR_DEVICE);
	ata_sff_pause(ap);	/* needed; also flushes, for mmio */
}

/* Note: original code is ata_sff_set_devctl */
static void ep93xx_pata_set_devctl(struct ata_port *ap, u8 ctl)
{
	struct ep93xx_pata_data *drv_data = ap->host->private_data;

	ep93xx_pata_write_reg(drv_data, ctl, IDECTRL_ADDR_CTL);
}

/* Note: original code is ata_sff_data_xfer */
static unsigned int ep93xx_pata_data_xfer(struct ata_queued_cmd *qc,
					  unsigned char *buf,
					  unsigned int buflen, int rw)
{
	struct ata_port *ap = qc->dev->link->ap;
	struct ep93xx_pata_data *drv_data = ap->host->private_data;
	u16 *data = (u16 *)buf;
	unsigned int words = buflen >> 1;

	/* Transfer multiple of 2 bytes */
	while (words--)
		if (rw == READ)
			*data++ = cpu_to_le16(
				ep93xx_pata_read_data(
					drv_data, IDECTRL_ADDR_DATA));
		else
			ep93xx_pata_write_data(drv_data, le16_to_cpu(*data++),
				IDECTRL_ADDR_DATA);

	/* Transfer trailing 1 byte, if any. */
	if (unlikely(buflen & 0x01)) {
		unsigned char pad[2] = { };

		buf += buflen - 1;

		if (rw == READ) {
			*pad = cpu_to_le16(
				ep93xx_pata_read_data(
					drv_data, IDECTRL_ADDR_DATA));
			*buf = pad[0];
		} else {
			pad[0] = *buf;
			ep93xx_pata_write_data(drv_data, le16_to_cpu(*pad),
					  IDECTRL_ADDR_DATA);
		}
		words++;
	}

	return words << 1;
}

/* Note: original code is ata_devchk */
static bool ep93xx_pata_device_is_present(struct ata_port *ap,
					  unsigned int device)
{
	struct ep93xx_pata_data *drv_data = ap->host->private_data;
	u8 nsect, lbal;

	ap->ops->sff_dev_select(ap, device);

	ep93xx_pata_write_reg(drv_data, 0x55, IDECTRL_ADDR_NSECT);
	ep93xx_pata_write_reg(drv_data, 0xaa, IDECTRL_ADDR_LBAL);

	ep93xx_pata_write_reg(drv_data, 0xaa, IDECTRL_ADDR_NSECT);
	ep93xx_pata_write_reg(drv_data, 0x55, IDECTRL_ADDR_LBAL);

	ep93xx_pata_write_reg(drv_data, 0x55, IDECTRL_ADDR_NSECT);
	ep93xx_pata_write_reg(drv_data, 0xaa, IDECTRL_ADDR_LBAL);

	nsect = ep93xx_pata_read_reg(drv_data, IDECTRL_ADDR_NSECT);
	lbal = ep93xx_pata_read_reg(drv_data, IDECTRL_ADDR_LBAL);

	if ((nsect == 0x55) && (lbal == 0xaa))
		return true;

	return false;
}

/* Note: original code is ata_sff_wait_after_reset */
static int ep93xx_pata_wait_after_reset(struct ata_link *link,
					unsigned int devmask,
					unsigned long deadline)
{
	struct ata_port *ap = link->ap;
	struct ep93xx_pata_data *drv_data = ap->host->private_data;
	unsigned int dev0 = devmask & (1 << 0);
	unsigned int dev1 = devmask & (1 << 1);
	int rc, ret = 0;

	ata_msleep(ap, ATA_WAIT_AFTER_RESET);

	/* always check readiness of the master device */
	rc = ata_sff_wait_ready(link, deadline);
	/*
	 * -ENODEV means the odd clown forgot the D7 pulldown resistor
	 * and TF status is 0xff, bail out on it too.
	 */
	if (rc)
		return rc;

	/*
	 * if device 1 was found in ata_devchk, wait for register
	 * access briefly, then wait for BSY to clear.
	 */
	if (dev1) {
		int i;

		ap->ops->sff_dev_select(ap, 1);

		/*
		 * Wait for register access.  Some ATAPI devices fail
		 * to set nsect/lbal after reset, so don't waste too
		 * much time on it.  We're gonna wait for !BSY anyway.
		 */
		for (i = 0; i < 2; i++) {
			u8 nsect, lbal;

			nsect = ep93xx_pata_read_reg(drv_data,
				IDECTRL_ADDR_NSECT);
			lbal = ep93xx_pata_read_reg(drv_data,
				IDECTRL_ADDR_LBAL);
			if (nsect == 1 && lbal == 1)
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

/* Note: original code is ata_bus_softreset */
static int ep93xx_pata_bus_softreset(struct ata_port *ap, unsigned int devmask,
				     unsigned long deadline)
{
	struct ep93xx_pata_data *drv_data = ap->host->private_data;

	ep93xx_pata_write_reg(drv_data, ap->ctl, IDECTRL_ADDR_CTL);
	udelay(20);		/* FIXME: flush */
	ep93xx_pata_write_reg(drv_data, ap->ctl | ATA_SRST, IDECTRL_ADDR_CTL);
	udelay(20);		/* FIXME: flush */
	ep93xx_pata_write_reg(drv_data, ap->ctl, IDECTRL_ADDR_CTL);
	ap->last_ctl = ap->ctl;

	return ep93xx_pata_wait_after_reset(&ap->link, devmask, deadline);
}

static void ep93xx_pata_release_dma(struct ep93xx_pata_data *drv_data)
{
	if (drv_data->dma_rx_channel) {
		dma_release_channel(drv_data->dma_rx_channel);
		drv_data->dma_rx_channel = NULL;
	}
	if (drv_data->dma_tx_channel) {
		dma_release_channel(drv_data->dma_tx_channel);
		drv_data->dma_tx_channel = NULL;
	}
}

static int ep93xx_pata_dma_init(struct ep93xx_pata_data *drv_data)
{
	struct platform_device *pdev = drv_data->pdev;
	struct device *dev = &pdev->dev;
	dma_cap_mask_t mask;
	struct dma_slave_config conf;
	int ret;

	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);

	/*
	 * Request two channels for IDE. Another possibility would be
	 * to request only one channel, and reprogram it's direction at
	 * start of new transfer.
	 */
	drv_data->dma_rx_channel = dma_request_chan(dev, "rx");
	if (IS_ERR(drv_data->dma_rx_channel))
		return dev_err_probe(dev, PTR_ERR(drv_data->dma_rx_channel),
				     "rx DMA setup failed\n");

	drv_data->dma_tx_channel = dma_request_chan(&pdev->dev, "tx");
	if (IS_ERR(drv_data->dma_tx_channel)) {
		ret = dev_err_probe(dev, PTR_ERR(drv_data->dma_tx_channel),
				    "tx DMA setup failed\n");
		goto fail_release_rx;
	}

	/* Configure receive channel direction and source address */
	memset(&conf, 0, sizeof(conf));
	conf.direction = DMA_DEV_TO_MEM;
	conf.src_addr = drv_data->udma_in_phys;
	conf.src_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	ret = dmaengine_slave_config(drv_data->dma_rx_channel, &conf);
	if (ret) {
		dev_err_probe(dev, ret, "failed to configure rx dma channel");
		goto fail_release_dma;
	}

	/* Configure transmit channel direction and destination address */
	memset(&conf, 0, sizeof(conf));
	conf.direction = DMA_MEM_TO_DEV;
	conf.dst_addr = drv_data->udma_out_phys;
	conf.dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	ret = dmaengine_slave_config(drv_data->dma_tx_channel, &conf);
	if (ret) {
		dev_err_probe(dev, ret, "failed to configure tx dma channel");
		goto fail_release_dma;
	}

	return 0;

fail_release_rx:
	dma_release_channel(drv_data->dma_rx_channel);
fail_release_dma:
	ep93xx_pata_release_dma(drv_data);

	return ret;
}

static void ep93xx_pata_dma_start(struct ata_queued_cmd *qc)
{
	struct dma_async_tx_descriptor *txd;
	struct ep93xx_pata_data *drv_data = qc->ap->host->private_data;
	void __iomem *base = drv_data->ide_base;
	struct ata_device *adev = qc->dev;
	u32 v = qc->dma_dir == DMA_TO_DEVICE ? IDEUDMAOP_RWOP : 0;
	struct dma_chan *channel = qc->dma_dir == DMA_TO_DEVICE
		? drv_data->dma_tx_channel : drv_data->dma_rx_channel;

	txd = dmaengine_prep_slave_sg(channel, qc->sg, qc->n_elem, qc->dma_dir,
		DMA_CTRL_ACK);
	if (!txd) {
		dev_err(qc->ap->dev, "failed to prepare slave for sg dma\n");
		return;
	}
	txd->callback = NULL;
	txd->callback_param = NULL;

	if (dmaengine_submit(txd) < 0) {
		dev_err(qc->ap->dev, "failed to submit dma transfer\n");
		return;
	}
	dma_async_issue_pending(channel);

	/*
	 * When enabling UDMA operation, IDEUDMAOP register needs to be
	 * programmed in three step sequence:
	 * 1) set or clear the RWOP bit,
	 * 2) perform dummy read of the register,
	 * 3) set the UEN bit.
	 */
	writel(v, base + IDEUDMAOP);
	readl(base + IDEUDMAOP);
	writel(v | IDEUDMAOP_UEN, base + IDEUDMAOP);

	writel(IDECFG_IDEEN | IDECFG_UDMA |
		((adev->xfer_mode - XFER_UDMA_0) << IDECFG_MODE_SHIFT),
		base + IDECFG);
}

static void ep93xx_pata_dma_stop(struct ata_queued_cmd *qc)
{
	struct ep93xx_pata_data *drv_data = qc->ap->host->private_data;
	void __iomem *base = drv_data->ide_base;

	/* terminate all dma transfers, if not yet finished */
	dmaengine_terminate_all(drv_data->dma_rx_channel);
	dmaengine_terminate_all(drv_data->dma_tx_channel);

	/*
	 * To properly stop IDE-DMA, IDEUDMAOP register must to be cleared
	 * and IDECTRL register must be set to default value.
	 */
	writel(0, base + IDEUDMAOP);
	writel(readl(base + IDECTRL) | IDECTRL_DIOWN | IDECTRL_DIORN |
		IDECTRL_CS0N | IDECTRL_CS1N, base + IDECTRL);

	ep93xx_pata_enable_pio(drv_data->ide_base,
		qc->dev->pio_mode - XFER_PIO_0);

	ata_sff_dma_pause(qc->ap);
}

static void ep93xx_pata_dma_setup(struct ata_queued_cmd *qc)
{
	qc->ap->ops->sff_exec_command(qc->ap, &qc->tf);
}

static u8 ep93xx_pata_dma_status(struct ata_port *ap)
{
	struct ep93xx_pata_data *drv_data = ap->host->private_data;
	u32 val = readl(drv_data->ide_base + IDEUDMASTS);

	/*
	 * UDMA Status Register bits:
	 *
	 * DMAIDE - DMA request signal from UDMA state machine,
	 * INTIDE - INT line generated by UDMA because of errors in the
	 *          state machine,
	 * SBUSY - UDMA state machine busy, not in idle state,
	 * NDO   - error for data-out not completed,
	 * NDI   - error for data-in not completed,
	 * N4X   - error for data transferred not multiplies of four
	 *         32-bit words.
	 * (EP93xx UG p27-17)
	 */
	if (val & IDEUDMASTS_NDO || val & IDEUDMASTS_NDI ||
	    val & IDEUDMASTS_N4X || val & IDEUDMASTS_INTIDE)
		return ATA_DMA_ERR;

	/* read INTRQ (INT[3]) pin input state */
	if (readl(drv_data->ide_base + IDECTRL) & IDECTRL_INTRQ)
		return ATA_DMA_INTR;

	if (val & IDEUDMASTS_SBUSY || val & IDEUDMASTS_DMAIDE)
		return ATA_DMA_ACTIVE;

	return 0;
}

/* Note: original code is ata_sff_softreset */
static int ep93xx_pata_softreset(struct ata_link *al, unsigned int *classes,
				 unsigned long deadline)
{
	struct ata_port *ap = al->ap;
	unsigned int slave_possible = ap->flags & ATA_FLAG_SLAVE_POSS;
	unsigned int devmask = 0;
	int rc;
	u8 err;

	/* determine if device 0/1 are present */
	if (ep93xx_pata_device_is_present(ap, 0))
		devmask |= (1 << 0);
	if (slave_possible && ep93xx_pata_device_is_present(ap, 1))
		devmask |= (1 << 1);

	/* select device 0 again */
	ap->ops->sff_dev_select(al->ap, 0);

	/* issue bus reset */
	rc = ep93xx_pata_bus_softreset(ap, devmask, deadline);
	/* if link is ocuppied, -ENODEV too is an error */
	if (rc && (rc != -ENODEV || sata_scr_valid(al))) {
		ata_link_err(al, "SRST failed (errno=%d)\n", rc);
		return rc;
	}

	/* determine by signature whether we have ATA or ATAPI devices */
	classes[0] = ata_sff_dev_classify(&al->device[0], devmask & (1 << 0),
					  &err);
	if (slave_possible && err != 0x81)
		classes[1] = ata_sff_dev_classify(&al->device[1],
						  devmask & (1 << 1), &err);

	return 0;
}

/* Note: original code is ata_sff_drain_fifo */
static void ep93xx_pata_drain_fifo(struct ata_queued_cmd *qc)
{
	int count;
	struct ata_port *ap;
	struct ep93xx_pata_data *drv_data;

	/* We only need to flush incoming data when a command was running */
	if (qc == NULL || qc->dma_dir == DMA_TO_DEVICE)
		return;

	ap = qc->ap;
	drv_data = ap->host->private_data;
	/* Drain up to 64K of data before we give up this recovery method */
	for (count = 0; (ap->ops->sff_check_status(ap) & ATA_DRQ)
		     && count < 65536; count += 2)
		ep93xx_pata_read_reg(drv_data, IDECTRL_ADDR_DATA);

	if (count)
		ata_port_dbg(ap, "drained %d bytes to clear DRQ.\n", count);

}

static int ep93xx_pata_port_start(struct ata_port *ap)
{
	struct ep93xx_pata_data *drv_data = ap->host->private_data;

	/*
	 * Set timings to safe values at startup (= number of ns from ATA
	 * specification), we'll switch to properly calculated values later.
	 */
	drv_data->t = *ata_timing_find_mode(XFER_PIO_0);
	return 0;
}

static const struct scsi_host_template ep93xx_pata_sht = {
	ATA_BASE_SHT(DRV_NAME),
	/* ep93xx dma implementation limit */
	.sg_tablesize		= 32,
	/* ep93xx dma can't transfer 65536 bytes at once */
	.dma_boundary		= 0x7fff,
};

static struct ata_port_operations ep93xx_pata_port_ops = {
	.inherits		= &ata_bmdma_port_ops,

	.reset.softreset	= ep93xx_pata_softreset,
	.reset.hardreset	= ATA_OP_NULL,

	.sff_dev_select		= ep93xx_pata_dev_select,
	.sff_set_devctl		= ep93xx_pata_set_devctl,
	.sff_check_status	= ep93xx_pata_check_status,
	.sff_check_altstatus	= ep93xx_pata_check_altstatus,
	.sff_tf_load		= ep93xx_pata_tf_load,
	.sff_tf_read		= ep93xx_pata_tf_read,
	.sff_exec_command	= ep93xx_pata_exec_command,
	.sff_data_xfer		= ep93xx_pata_data_xfer,
	.sff_drain_fifo		= ep93xx_pata_drain_fifo,
	.sff_irq_clear		= ATA_OP_NULL,

	.set_piomode		= ep93xx_pata_set_piomode,

	.bmdma_setup		= ep93xx_pata_dma_setup,
	.bmdma_start		= ep93xx_pata_dma_start,
	.bmdma_stop		= ep93xx_pata_dma_stop,
	.bmdma_status		= ep93xx_pata_dma_status,

	.cable_detect		= ata_cable_unknown,
	.port_start		= ep93xx_pata_port_start,
};

static const struct soc_device_attribute ep93xx_soc_table[] = {
	{ .revision = "E1", .data = (void *)ATA_UDMA3 },
	{ .revision = "E2", .data = (void *)ATA_UDMA4 },
	{ /* sentinel */ }
};

static int ep93xx_pata_probe(struct platform_device *pdev)
{
	struct ep93xx_pata_data *drv_data;
	struct ata_host *host;
	struct ata_port *ap;
	int irq;
	struct resource *mem_res;
	void __iomem *ide_base;
	int err;

	/* INT[3] (IRQ_EP93XX_EXT3) line connected as pull down */
	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	ide_base = devm_platform_get_and_ioremap_resource(pdev, 0, &mem_res);
	if (IS_ERR(ide_base))
		return PTR_ERR(ide_base);

	drv_data = devm_kzalloc(&pdev->dev, sizeof(*drv_data), GFP_KERNEL);
	if (!drv_data)
		return -ENOMEM;

	drv_data->pdev = pdev;
	drv_data->ide_base = ide_base;
	drv_data->udma_in_phys = mem_res->start + IDEUDMADATAIN;
	drv_data->udma_out_phys = mem_res->start + IDEUDMADATAOUT;
	err = ep93xx_pata_dma_init(drv_data);
	if (err)
		return err;

	/* allocate host */
	host = ata_host_alloc(&pdev->dev, 1);
	if (!host) {
		err = -ENOMEM;
		goto err_rel_dma;
	}

	ep93xx_pata_clear_regs(ide_base);

	host->private_data = drv_data;

	ap = host->ports[0];
	ap->dev = &pdev->dev;
	ap->ops = &ep93xx_pata_port_ops;
	ap->flags |= ATA_FLAG_SLAVE_POSS;
	ap->pio_mask = ATA_PIO4;

	/*
	 * Maximum UDMA modes:
	 * EP931x rev.E0 - UDMA2
	 * EP931x rev.E1 - UDMA3
	 * EP931x rev.E2 - UDMA4
	 *
	 * MWDMA support was removed from EP931x rev.E2,
	 * so this driver supports only UDMA modes.
	 */
	if (drv_data->dma_rx_channel && drv_data->dma_tx_channel) {
		const struct soc_device_attribute *match;

		match = soc_device_match(ep93xx_soc_table);
		if (match)
			ap->udma_mask = (unsigned int) match->data;
		else
			ap->udma_mask = ATA_UDMA2;
	}

	/* defaults, pio 0 */
	ep93xx_pata_enable_pio(ide_base, 0);

	dev_info(&pdev->dev, "version " DRV_VERSION "\n");

	/* activate host */
	err = ata_host_activate(host, irq, ata_bmdma_interrupt, 0,
		&ep93xx_pata_sht);
	if (err == 0)
		return 0;

err_rel_dma:
	ep93xx_pata_release_dma(drv_data);
	return err;
}

static void ep93xx_pata_remove(struct platform_device *pdev)
{
	struct ata_host *host = platform_get_drvdata(pdev);
	struct ep93xx_pata_data *drv_data = host->private_data;

	ata_host_detach(host);
	ep93xx_pata_release_dma(drv_data);
	ep93xx_pata_clear_regs(drv_data->ide_base);
}

static const struct of_device_id ep93xx_pata_of_ids[] = {
	{ .compatible = "cirrus,ep9312-pata" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, ep93xx_pata_of_ids);

static struct platform_driver ep93xx_pata_platform_driver = {
	.driver = {
		.name = DRV_NAME,
		.of_match_table = ep93xx_pata_of_ids,
	},
	.probe = ep93xx_pata_probe,
	.remove = ep93xx_pata_remove,
};

module_platform_driver(ep93xx_pata_platform_driver);

MODULE_AUTHOR("Alessandro Zummo, Lennert Buytenhek, Joao Ramos, "
		"Bartlomiej Zolnierkiewicz, Rafal Prylowski");
MODULE_DESCRIPTION("low-level driver for cirrus ep93xx IDE controller");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);
MODULE_ALIAS("platform:pata_ep93xx");
