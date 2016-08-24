/*
 * Driver for Broadcom BRCMSTB, NSP,  NS2, Cygnus SPI Controllers
 *
 * Copyright 2016 Broadcom
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation (the "GPL").
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License version 2 (GPLv2) for more details.
 *
 * You should have received a copy of the GNU General Public License
 * version 2 (GPLv2) along with this source code.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mtd/cfi.h>
#include <linux/mtd/spi-nor.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>
#include <linux/sysfs.h>
#include <linux/types.h>
#include "spi-bcm-qspi.h"

#define DRIVER_NAME "bcm_qspi"

/* MSPI register offsets */
#define MSPI_SPCR0_LSB				0x000
#define MSPI_SPCR0_MSB				0x004
#define MSPI_SPCR1_LSB				0x008
#define MSPI_SPCR1_MSB				0x00c
#define MSPI_NEWQP				0x010
#define MSPI_ENDQP				0x014
#define MSPI_SPCR2				0x018
#define MSPI_MSPI_STATUS			0x020
#define MSPI_CPTQP				0x024
#define MSPI_SPCR3				0x028
#define MSPI_TXRAM				0x040
#define MSPI_RXRAM				0x0c0
#define MSPI_CDRAM				0x140
#define MSPI_WRITE_LOCK			0x180

#define MSPI_MASTER_BIT			BIT(7)

#define MSPI_NUM_CDRAM				16
#define MSPI_CDRAM_CONT_BIT			BIT(7)
#define MSPI_CDRAM_BITSE_BIT			BIT(6)
#define MSPI_CDRAM_PCS				0xf

#define MSPI_SPCR2_SPE				BIT(6)
#define MSPI_SPCR2_CONT_AFTER_CMD		BIT(7)

#define MSPI_MSPI_STATUS_SPIF			BIT(0)

#define INTR_BASE_BIT_SHIFT			0x02
#define INTR_COUNT				0x07

#define NUM_CHIPSELECT				4
#define QSPI_SPBR_MIN				8U
#define QSPI_SPBR_MAX				255U

#define OPCODE_DIOR				0xBB
#define OPCODE_QIOR				0xEB
#define OPCODE_DIOR_4B				0xBC
#define OPCODE_QIOR_4B				0xEC

#define MAX_CMD_SIZE				6

#define ADDR_4MB_MASK				GENMASK(22, 0)

/* stop at end of transfer, no other reason */
#define TRANS_STATUS_BREAK_NONE		0
/* stop at end of spi_message */
#define TRANS_STATUS_BREAK_EOM			1
/* stop at end of spi_transfer if delay */
#define TRANS_STATUS_BREAK_DELAY		2
/* stop at end of spi_transfer if cs_change */
#define TRANS_STATUS_BREAK_CS_CHANGE		4
/* stop if we run out of bytes */
#define TRANS_STATUS_BREAK_NO_BYTES		8

/* events that make us stop filling TX slots */
#define TRANS_STATUS_BREAK_TX (TRANS_STATUS_BREAK_EOM |		\
			       TRANS_STATUS_BREAK_DELAY |		\
			       TRANS_STATUS_BREAK_CS_CHANGE)

/* events that make us deassert CS */
#define TRANS_STATUS_BREAK_DESELECT (TRANS_STATUS_BREAK_EOM |		\
				     TRANS_STATUS_BREAK_CS_CHANGE)

struct bcm_qspi_parms {
	u32 speed_hz;
	u8 mode;
	u8 bits_per_word;
};

enum base_type {
	MSPI,
	CHIP_SELECT,
	BASEMAX,
};

struct bcm_qspi_irq {
	const char *irq_name;
	const irq_handler_t irq_handler;
	u32 mask;
};

struct bcm_qspi_dev_id {
	const struct bcm_qspi_irq *irqp;
	void *dev;
};

struct qspi_trans {
	struct spi_transfer *trans;
	int byte;
};

struct bcm_qspi {
	struct platform_device *pdev;
	struct spi_master *master;
	struct clk *clk;
	u32 base_clk;
	u32 max_speed_hz;
	void __iomem *base[BASEMAX];
	struct bcm_qspi_parms last_parms;
	struct qspi_trans  trans_pos;
	int curr_cs;
	u32 s3_strap_override_ctrl;
	bool big_endian;
	int num_irqs;
	struct bcm_qspi_dev_id *dev_ids;
	struct completion mspi_done;
};

/* Read qspi controller register*/
static inline u32 bcm_qspi_read(struct bcm_qspi *qspi, enum base_type type,
				unsigned int offset)
{
	return bcm_qspi_readl(qspi->big_endian, qspi->base[type] + offset);
}

/* Write qspi controller register*/
static inline void bcm_qspi_write(struct bcm_qspi *qspi, enum base_type type,
				  unsigned int offset, unsigned int data)
{
	bcm_qspi_writel(qspi->big_endian, data, qspi->base[type] + offset);
}

static void bcm_qspi_chip_select(struct bcm_qspi *qspi, int cs)
{
	u32 data = 0;

	if (qspi->curr_cs == cs)
		return;
	if (qspi->base[CHIP_SELECT]) {
		data = bcm_qspi_read(qspi, CHIP_SELECT, 0);
		data = (data & ~0xff) | (1 << cs);
		bcm_qspi_write(qspi, CHIP_SELECT, 0, data);
		usleep_range(10, 20);
	}
	qspi->curr_cs = cs;
}

/* MSPI helpers */
static void bcm_qspi_hw_set_parms(struct bcm_qspi *qspi,
				  const struct bcm_qspi_parms *xp)
{
	u32 spcr, spbr = 0;

	if (xp->speed_hz)
		spbr = qspi->base_clk / (2 * xp->speed_hz);

	spcr = clamp_val(spbr, QSPI_SPBR_MIN, QSPI_SPBR_MAX);
	bcm_qspi_write(qspi, MSPI, MSPI_SPCR0_LSB, spcr);

	spcr = MSPI_MASTER_BIT;
	/* for 16 bit the data should be zero */
	if (xp->bits_per_word != 16)
		spcr |= xp->bits_per_word << 2;
	spcr |= xp->mode & 3;
	bcm_qspi_write(qspi, MSPI, MSPI_SPCR0_MSB, spcr);

	qspi->last_parms = *xp;
}

static void bcm_qspi_update_parms(struct bcm_qspi *qspi,
				  struct spi_device *spi,
				  struct spi_transfer *trans)
{
	struct bcm_qspi_parms xp;

	xp.speed_hz = trans->speed_hz;
	xp.bits_per_word = trans->bits_per_word;
	xp.mode = spi->mode;

	bcm_qspi_hw_set_parms(qspi, &xp);
}

static int bcm_qspi_setup(struct spi_device *spi)
{
	struct bcm_qspi_parms *xp;

	if (spi->bits_per_word > 16)
		return -EINVAL;

	xp = spi_get_ctldata(spi);
	if (!xp) {
		xp = kzalloc(sizeof(*xp), GFP_KERNEL);
		if (!xp)
			return -ENOMEM;
		spi_set_ctldata(spi, xp);
	}
	xp->speed_hz = spi->max_speed_hz;
	xp->mode = spi->mode;

	if (spi->bits_per_word)
		xp->bits_per_word = spi->bits_per_word;
	else
		xp->bits_per_word = 8;

	return 0;
}

static int update_qspi_trans_byte_count(struct bcm_qspi *qspi,
					struct qspi_trans *qt, int flags)
{
	int ret = TRANS_STATUS_BREAK_NONE;

	/* count the last transferred bytes */
	if (qt->trans->bits_per_word <= 8)
		qt->byte++;
	else
		qt->byte += 2;

	if (qt->byte >= qt->trans->len) {
		/* we're at the end of the spi_transfer */

		/* in TX mode, need to pause for a delay or CS change */
		if (qt->trans->delay_usecs &&
		    (flags & TRANS_STATUS_BREAK_DELAY))
			ret |= TRANS_STATUS_BREAK_DELAY;
		if (qt->trans->cs_change &&
		    (flags & TRANS_STATUS_BREAK_CS_CHANGE))
			ret |= TRANS_STATUS_BREAK_CS_CHANGE;
		if (ret)
			goto done;

		dev_dbg(&qspi->pdev->dev, "advance msg exit\n");
		if (spi_transfer_is_last(qspi->master, qt->trans))
			ret = TRANS_STATUS_BREAK_EOM;
		else
			ret = TRANS_STATUS_BREAK_NO_BYTES;

		qt->trans = NULL;
	}

done:
	dev_dbg(&qspi->pdev->dev, "trans %p len %d byte %d ret %x\n",
		qt->trans, qt->trans ? qt->trans->len : 0, qt->byte, ret);
	return ret;
}

static inline u8 read_rxram_slot_u8(struct bcm_qspi *qspi, int slot)
{
	u32 slot_offset = MSPI_RXRAM + (slot << 3) + 0x4;

	/* mask out reserved bits */
	return bcm_qspi_read(qspi, MSPI, slot_offset) & 0xff;
}

static inline u16 read_rxram_slot_u16(struct bcm_qspi *qspi, int slot)
{
	u32 reg_offset = MSPI_RXRAM;
	u32 lsb_offset = reg_offset + (slot << 3) + 0x4;
	u32 msb_offset = reg_offset + (slot << 3);

	return (bcm_qspi_read(qspi, MSPI, lsb_offset) & 0xff) |
		((bcm_qspi_read(qspi, MSPI, msb_offset) & 0xff) << 8);
}

static void read_from_hw(struct bcm_qspi *qspi, int slots)
{
	struct qspi_trans tp;
	int slot;

	if (slots > MSPI_NUM_CDRAM) {
		/* should never happen */
		dev_err(&qspi->pdev->dev, "%s: too many slots!\n", __func__);
		return;
	}

	tp = qspi->trans_pos;

	for (slot = 0; slot < slots; slot++) {
		if (tp.trans->bits_per_word <= 8) {
			u8 *buf = tp.trans->rx_buf;

			if (buf)
				buf[tp.byte] = read_rxram_slot_u8(qspi, slot);
			dev_dbg(&qspi->pdev->dev, "RD %02x\n",
				buf ? buf[tp.byte] : 0xff);
		} else {
			u16 *buf = tp.trans->rx_buf;

			if (buf)
				buf[tp.byte / 2] = read_rxram_slot_u16(qspi,
								      slot);
			dev_dbg(&qspi->pdev->dev, "RD %04x\n",
				buf ? buf[tp.byte] : 0xffff);
		}

		update_qspi_trans_byte_count(qspi, &tp,
					     TRANS_STATUS_BREAK_NONE);
	}

	qspi->trans_pos = tp;
}

static inline void write_txram_slot_u8(struct bcm_qspi *qspi, int slot,
				       u8 val)
{
	u32 reg_offset = MSPI_TXRAM + (slot << 3);

	/* mask out reserved bits */
	bcm_qspi_write(qspi, MSPI, reg_offset, val);
}

static inline void write_txram_slot_u16(struct bcm_qspi *qspi, int slot,
					u16 val)
{
	u32 reg_offset = MSPI_TXRAM;
	u32 msb_offset = reg_offset + (slot << 3);
	u32 lsb_offset = reg_offset + (slot << 3) + 0x4;

	bcm_qspi_write(qspi, MSPI, msb_offset, (val >> 8));
	bcm_qspi_write(qspi, MSPI, lsb_offset, (val & 0xff));
}

static inline u32 read_cdram_slot(struct bcm_qspi *qspi, int slot)
{
	return bcm_qspi_read(qspi, MSPI, MSPI_CDRAM + (slot << 2));
}

static inline void write_cdram_slot(struct bcm_qspi *qspi, int slot, u32 val)
{
	bcm_qspi_write(qspi, MSPI, (MSPI_CDRAM + (slot << 2)), val);
}

/* Return number of slots written */
static int write_to_hw(struct bcm_qspi *qspi, struct spi_device *spi)
{
	struct qspi_trans tp;
	int slot = 0, tstatus = 0;
	u32 mspi_cdram = 0;

	tp = qspi->trans_pos;
	bcm_qspi_update_parms(qspi, spi, tp.trans);

	/* Run until end of transfer or reached the max data */
	while (!tstatus && slot < MSPI_NUM_CDRAM) {
		if (tp.trans->bits_per_word <= 8) {
			const u8 *buf = tp.trans->tx_buf;
			u8 val = buf ? buf[tp.byte] : 0xff;

			write_txram_slot_u8(qspi, slot, val);
			dev_dbg(&qspi->pdev->dev, "WR %02x\n", val);
		} else {
			const u16 *buf = tp.trans->tx_buf;
			u16 val = buf ? buf[tp.byte / 2] : 0xffff;

			write_txram_slot_u16(qspi, slot, val);
			dev_dbg(&qspi->pdev->dev, "WR %04x\n", val);
		}
		mspi_cdram = MSPI_CDRAM_CONT_BIT;
		mspi_cdram |= (~(1 << spi->chip_select) &
			       MSPI_CDRAM_PCS);
		mspi_cdram |= ((tp.trans->bits_per_word <= 8) ? 0 :
				MSPI_CDRAM_BITSE_BIT);

		write_cdram_slot(qspi, slot, mspi_cdram);

		tstatus = update_qspi_trans_byte_count(qspi, &tp,
						       TRANS_STATUS_BREAK_TX);
		slot++;
	}

	if (!slot) {
		dev_err(&qspi->pdev->dev, "%s: no data to send?", __func__);
		goto done;
	}

	dev_dbg(&qspi->pdev->dev, "submitting %d slots\n", slot);
	bcm_qspi_write(qspi, MSPI, MSPI_NEWQP, 0);
	bcm_qspi_write(qspi, MSPI, MSPI_ENDQP, slot - 1);

	if (tstatus & TRANS_STATUS_BREAK_DESELECT) {
		mspi_cdram = read_cdram_slot(qspi, slot - 1) &
			~MSPI_CDRAM_CONT_BIT;
		write_cdram_slot(qspi, slot - 1, mspi_cdram);
	}

	/* Must flush previous writes before starting MSPI operation */
	mb();
	/* Set cont | spe | spifie */
	bcm_qspi_write(qspi, MSPI, MSPI_SPCR2, 0xe0);

done:
	return slot;
}

static int bcm_qspi_transfer_one(struct spi_master *master,
				 struct spi_device *spi,
				 struct spi_transfer *trans)
{
	struct bcm_qspi *qspi = spi_master_get_devdata(master);
	int slots;
	unsigned long timeo = msecs_to_jiffies(100);

	bcm_qspi_chip_select(qspi, spi->chip_select);
	qspi->trans_pos.trans = trans;
	qspi->trans_pos.byte = 0;

	while (qspi->trans_pos.byte < trans->len) {
		reinit_completion(&qspi->mspi_done);

		slots = write_to_hw(qspi, spi);
		if (!wait_for_completion_timeout(&qspi->mspi_done, timeo)) {
			dev_err(&qspi->pdev->dev, "timeout waiting for MSPI\n");
			return -ETIMEDOUT;
		}

		read_from_hw(qspi, slots);
	}

	return 0;
}

static void bcm_qspi_cleanup(struct spi_device *spi)
{
	struct bcm_qspi_parms *xp = spi_get_ctldata(spi);

	kfree(xp);
}

static irqreturn_t bcm_qspi_mspi_l2_isr(int irq, void *dev_id)
{
	struct bcm_qspi_dev_id *qspi_dev_id = dev_id;
	struct bcm_qspi *qspi = qspi_dev_id->dev;
	u32 status = bcm_qspi_read(qspi, MSPI, MSPI_MSPI_STATUS);

	if (status & MSPI_MSPI_STATUS_SPIF) {
		/* clear interrupt */
		status &= ~MSPI_MSPI_STATUS_SPIF;
		bcm_qspi_write(qspi, MSPI, MSPI_MSPI_STATUS, status);
		complete(&qspi->mspi_done);
		return IRQ_HANDLED;
	} else {
		return IRQ_NONE;
	}
}

static const struct bcm_qspi_irq qspi_irq_tab[] = {
	{
		.irq_name = "mspi_done",
		.irq_handler = bcm_qspi_mspi_l2_isr,
		.mask = INTR_MSPI_DONE_MASK,
	},
	{
		.irq_name = "mspi_halted",
		.irq_handler = bcm_qspi_mspi_l2_isr,
		.mask = INTR_MSPI_HALTED_MASK,
	},
};

static void bcm_qspi_hw_init(struct bcm_qspi *qspi)
{
	struct bcm_qspi_parms parms;

	bcm_qspi_write(qspi, MSPI, MSPI_SPCR1_LSB, 0);
	bcm_qspi_write(qspi, MSPI, MSPI_SPCR1_MSB, 0);
	bcm_qspi_write(qspi, MSPI, MSPI_NEWQP, 0);
	bcm_qspi_write(qspi, MSPI, MSPI_ENDQP, 0);
	bcm_qspi_write(qspi, MSPI, MSPI_SPCR2, 0x20);

	parms.mode = SPI_MODE_3;
	parms.bits_per_word = 8;
	parms.speed_hz = qspi->max_speed_hz;
	bcm_qspi_hw_set_parms(qspi, &parms);
}

static void bcm_qspi_hw_uninit(struct bcm_qspi *qspi)
{
	bcm_qspi_write(qspi, MSPI, MSPI_SPCR2, 0);
}

static const struct of_device_id bcm_qspi_of_match[] = {
	{ .compatible = "brcm,spi-bcm-qspi" },
	{},
};
MODULE_DEVICE_TABLE(of, bcm_qspi_of_match);

int bcm_qspi_probe(struct platform_device *pdev,
		   struct bcm_qspi_soc_intc *soc)
{
	struct device *dev = &pdev->dev;
	struct bcm_qspi *qspi;
	struct spi_master *master;
	struct resource *res;
	int irq, ret = 0, num_ints = 0;
	u32 val;
	const char *name = NULL;
	int num_irqs = ARRAY_SIZE(qspi_irq_tab);

	/* We only support device-tree instantiation */
	if (!dev->of_node)
		return -ENODEV;

	if (!of_match_node(bcm_qspi_of_match, dev->of_node))
		return -ENODEV;

	master = spi_alloc_master(dev, sizeof(struct bcm_qspi));
	if (!master) {
		dev_err(dev, "error allocating spi_master\n");
		return -ENOMEM;
	}

	qspi = spi_master_get_devdata(master);
	qspi->pdev = pdev;
	qspi->trans_pos.trans = NULL;
	qspi->trans_pos.byte = 0;
	qspi->master = master;

	master->bus_num = -1;
	master->mode_bits = SPI_CPHA | SPI_CPOL | SPI_RX_DUAL | SPI_RX_QUAD;
	master->setup = bcm_qspi_setup;
	master->transfer_one = bcm_qspi_transfer_one;
	master->cleanup = bcm_qspi_cleanup;
	master->dev.of_node = dev->of_node;
	master->num_chipselect = NUM_CHIPSELECT;

	qspi->big_endian = of_device_is_big_endian(dev->of_node);

	if (!of_property_read_u32(dev->of_node, "num-cs", &val))
		master->num_chipselect = val;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "hif_mspi");
	if (!res)
		res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
						   "mspi");

	if (res) {
		qspi->base[MSPI]  = devm_ioremap_resource(dev, res);
		if (IS_ERR(qspi->base[MSPI])) {
			ret = PTR_ERR(qspi->base[MSPI]);
			goto qspi_probe_err;
		}
	} else {
		goto qspi_probe_err;
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "cs_reg");
	if (res) {
		qspi->base[CHIP_SELECT]  = devm_ioremap_resource(dev, res);
		if (IS_ERR(qspi->base[CHIP_SELECT])) {
			ret = PTR_ERR(qspi->base[CHIP_SELECT]);
			goto qspi_probe_err;
		}
	}

	qspi->dev_ids = kcalloc(num_irqs, sizeof(struct bcm_qspi_dev_id),
				GFP_KERNEL);
	if (IS_ERR(qspi->dev_ids)) {
		ret = PTR_ERR(qspi->dev_ids);
		goto qspi_probe_err;
	}

	for (val = 0; val < num_irqs; val++) {
		irq = -1;
		name = qspi_irq_tab[val].irq_name;
		irq = platform_get_irq_byname(pdev, name);

		if (irq  >= 0) {
			ret = devm_request_irq(&pdev->dev, irq,
					       qspi_irq_tab[val].irq_handler, 0,
					       name,
					       &qspi->dev_ids[val]);
			if (ret < 0) {
				dev_err(&pdev->dev, "IRQ %s not found\n", name);
				goto qspi_probe_err;
			}

			qspi->dev_ids[val].dev = qspi;
			qspi->dev_ids[val].irqp = &qspi_irq_tab[val];
			num_ints++;
			dev_dbg(&pdev->dev, "registered IRQ %s %d\n",
				qspi_irq_tab[val].irq_name,
				irq);
		}
	}

	if (!num_ints) {
		dev_err(&pdev->dev, "no IRQs registered, cannot init driver\n");
		goto qspi_probe_err;
	}

	qspi->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(qspi->clk)) {
		dev_warn(dev, "unable to get clock\n");
		goto qspi_probe_err;
	}

	ret = clk_prepare_enable(qspi->clk);
	if (ret) {
		dev_err(dev, "failed to prepare clock\n");
		goto qspi_probe_err;
	}

	qspi->base_clk = clk_get_rate(qspi->clk);
	qspi->max_speed_hz = qspi->base_clk / (QSPI_SPBR_MIN * 2);

	bcm_qspi_hw_init(qspi);
	init_completion(&qspi->mspi_done);
	qspi->curr_cs = -1;

	platform_set_drvdata(pdev, qspi);
	ret = devm_spi_register_master(&pdev->dev, master);
	if (ret < 0) {
		dev_err(dev, "can't register master\n");
		goto qspi_reg_err;
	}

	return 0;

qspi_reg_err:
	bcm_qspi_hw_uninit(qspi);
	clk_disable_unprepare(qspi->clk);
qspi_probe_err:
	spi_master_put(master);
	kfree(qspi->dev_ids);
	return ret;
}
/* probe function to be called by SoC specific platform driver probe */
EXPORT_SYMBOL_GPL(bcm_qspi_probe);

int bcm_qspi_remove(struct platform_device *pdev)
{
	struct bcm_qspi *qspi = platform_get_drvdata(pdev);

	platform_set_drvdata(pdev, NULL);
	bcm_qspi_hw_uninit(qspi);
	clk_disable_unprepare(qspi->clk);
	kfree(qspi->dev_ids);
	spi_unregister_master(qspi->master);

	return 0;
}
/* function to be called by SoC specific platform driver remove() */
EXPORT_SYMBOL_GPL(bcm_qspi_remove);

#ifdef CONFIG_PM_SLEEP
static int bcm_qspi_suspend(struct device *dev)
{
	struct bcm_qspi *qspi = dev_get_drvdata(dev);

	spi_master_suspend(qspi->master);
	clk_disable(qspi->clk);
	bcm_qspi_hw_uninit(qspi);

	return 0;
};

static int bcm_qspi_resume(struct device *dev)
{
	struct bcm_qspi *qspi = dev_get_drvdata(dev);
	int ret = 0;

	bcm_qspi_hw_init(qspi);
	bcm_qspi_chip_select(qspi, qspi->curr_cs);
	ret = clk_enable(qspi->clk);
	if (!ret)
		spi_master_resume(qspi->master);

	return ret;
}
#endif /* CONFIG_PM_SLEEP */

const struct dev_pm_ops bcm_qspi_pm_ops = {
	.suspend = bcm_qspi_suspend,
	.resume  = bcm_qspi_resume,
};
/* pm_ops to be called by SoC specific platform driver */
EXPORT_SYMBOL_GPL(bcm_qspi_pm_ops);

MODULE_AUTHOR("Kamal Dasu");
MODULE_DESCRIPTION("Broadcom QSPI driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" DRIVER_NAME);
