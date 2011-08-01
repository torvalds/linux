/*
 * linux/arch/arm/plat-omap/mcbsp.c
 *
 * Copyright (C) 2004 Nokia Corporation
 * Author: Samuel Ortiz <samuel.ortiz@nokia.com>
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Multichannel mode not supported.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/slab.h>

#include <plat/mcbsp.h>
#include <plat/omap_device.h>
#include <linux/pm_runtime.h>

/* XXX These "sideways" includes are a sign that something is wrong */
#include "../mach-omap2/cm2xxx_3xxx.h"
#include "../mach-omap2/cm-regbits-34xx.h"

struct omap_mcbsp **mcbsp_ptr;
int omap_mcbsp_count, omap_mcbsp_cache_size;

static void omap_mcbsp_write(struct omap_mcbsp *mcbsp, u16 reg, u32 val)
{
	if (cpu_class_is_omap1()) {
		((u16 *)mcbsp->reg_cache)[reg / sizeof(u16)] = (u16)val;
		__raw_writew((u16)val, mcbsp->io_base + reg);
	} else if (cpu_is_omap2420()) {
		((u16 *)mcbsp->reg_cache)[reg / sizeof(u32)] = (u16)val;
		__raw_writew((u16)val, mcbsp->io_base + reg);
	} else {
		((u32 *)mcbsp->reg_cache)[reg / sizeof(u32)] = val;
		__raw_writel(val, mcbsp->io_base + reg);
	}
}

static int omap_mcbsp_read(struct omap_mcbsp *mcbsp, u16 reg, bool from_cache)
{
	if (cpu_class_is_omap1()) {
		return !from_cache ? __raw_readw(mcbsp->io_base + reg) :
				((u16 *)mcbsp->reg_cache)[reg / sizeof(u16)];
	} else if (cpu_is_omap2420()) {
		return !from_cache ? __raw_readw(mcbsp->io_base + reg) :
				((u16 *)mcbsp->reg_cache)[reg / sizeof(u32)];
	} else {
		return !from_cache ? __raw_readl(mcbsp->io_base + reg) :
				((u32 *)mcbsp->reg_cache)[reg / sizeof(u32)];
	}
}

#ifdef CONFIG_ARCH_OMAP3
static void omap_mcbsp_st_write(struct omap_mcbsp *mcbsp, u16 reg, u32 val)
{
	__raw_writel(val, mcbsp->st_data->io_base_st + reg);
}

static int omap_mcbsp_st_read(struct omap_mcbsp *mcbsp, u16 reg)
{
	return __raw_readl(mcbsp->st_data->io_base_st + reg);
}
#endif

#define MCBSP_READ(mcbsp, reg) \
		omap_mcbsp_read(mcbsp, OMAP_MCBSP_REG_##reg, 0)
#define MCBSP_WRITE(mcbsp, reg, val) \
		omap_mcbsp_write(mcbsp, OMAP_MCBSP_REG_##reg, val)
#define MCBSP_READ_CACHE(mcbsp, reg) \
		omap_mcbsp_read(mcbsp, OMAP_MCBSP_REG_##reg, 1)

#define MCBSP_ST_READ(mcbsp, reg) \
			omap_mcbsp_st_read(mcbsp, OMAP_ST_REG_##reg)
#define MCBSP_ST_WRITE(mcbsp, reg, val) \
			omap_mcbsp_st_write(mcbsp, OMAP_ST_REG_##reg, val)

static void omap_mcbsp_dump_reg(u8 id)
{
	struct omap_mcbsp *mcbsp = id_to_mcbsp_ptr(id);

	dev_dbg(mcbsp->dev, "**** McBSP%d regs ****\n", mcbsp->id);
	dev_dbg(mcbsp->dev, "DRR2:  0x%04x\n",
			MCBSP_READ(mcbsp, DRR2));
	dev_dbg(mcbsp->dev, "DRR1:  0x%04x\n",
			MCBSP_READ(mcbsp, DRR1));
	dev_dbg(mcbsp->dev, "DXR2:  0x%04x\n",
			MCBSP_READ(mcbsp, DXR2));
	dev_dbg(mcbsp->dev, "DXR1:  0x%04x\n",
			MCBSP_READ(mcbsp, DXR1));
	dev_dbg(mcbsp->dev, "SPCR2: 0x%04x\n",
			MCBSP_READ(mcbsp, SPCR2));
	dev_dbg(mcbsp->dev, "SPCR1: 0x%04x\n",
			MCBSP_READ(mcbsp, SPCR1));
	dev_dbg(mcbsp->dev, "RCR2:  0x%04x\n",
			MCBSP_READ(mcbsp, RCR2));
	dev_dbg(mcbsp->dev, "RCR1:  0x%04x\n",
			MCBSP_READ(mcbsp, RCR1));
	dev_dbg(mcbsp->dev, "XCR2:  0x%04x\n",
			MCBSP_READ(mcbsp, XCR2));
	dev_dbg(mcbsp->dev, "XCR1:  0x%04x\n",
			MCBSP_READ(mcbsp, XCR1));
	dev_dbg(mcbsp->dev, "SRGR2: 0x%04x\n",
			MCBSP_READ(mcbsp, SRGR2));
	dev_dbg(mcbsp->dev, "SRGR1: 0x%04x\n",
			MCBSP_READ(mcbsp, SRGR1));
	dev_dbg(mcbsp->dev, "PCR0:  0x%04x\n",
			MCBSP_READ(mcbsp, PCR0));
	dev_dbg(mcbsp->dev, "***********************\n");
}

static irqreturn_t omap_mcbsp_tx_irq_handler(int irq, void *dev_id)
{
	struct omap_mcbsp *mcbsp_tx = dev_id;
	u16 irqst_spcr2;

	irqst_spcr2 = MCBSP_READ(mcbsp_tx, SPCR2);
	dev_dbg(mcbsp_tx->dev, "TX IRQ callback : 0x%x\n", irqst_spcr2);

	if (irqst_spcr2 & XSYNC_ERR) {
		dev_err(mcbsp_tx->dev, "TX Frame Sync Error! : 0x%x\n",
			irqst_spcr2);
		/* Writing zero to XSYNC_ERR clears the IRQ */
		MCBSP_WRITE(mcbsp_tx, SPCR2, MCBSP_READ_CACHE(mcbsp_tx, SPCR2));
	}

	return IRQ_HANDLED;
}

static irqreturn_t omap_mcbsp_rx_irq_handler(int irq, void *dev_id)
{
	struct omap_mcbsp *mcbsp_rx = dev_id;
	u16 irqst_spcr1;

	irqst_spcr1 = MCBSP_READ(mcbsp_rx, SPCR1);
	dev_dbg(mcbsp_rx->dev, "RX IRQ callback : 0x%x\n", irqst_spcr1);

	if (irqst_spcr1 & RSYNC_ERR) {
		dev_err(mcbsp_rx->dev, "RX Frame Sync Error! : 0x%x\n",
			irqst_spcr1);
		/* Writing zero to RSYNC_ERR clears the IRQ */
		MCBSP_WRITE(mcbsp_rx, SPCR1, MCBSP_READ_CACHE(mcbsp_rx, SPCR1));
	}

	return IRQ_HANDLED;
}

/*
 * omap_mcbsp_config simply write a config to the
 * appropriate McBSP.
 * You either call this function or set the McBSP registers
 * by yourself before calling omap_mcbsp_start().
 */
void omap_mcbsp_config(unsigned int id, const struct omap_mcbsp_reg_cfg *config)
{
	struct omap_mcbsp *mcbsp;

	if (!omap_mcbsp_check_valid_id(id)) {
		printk(KERN_ERR "%s: Invalid id (%d)\n", __func__, id + 1);
		return;
	}
	mcbsp = id_to_mcbsp_ptr(id);

	dev_dbg(mcbsp->dev, "Configuring McBSP%d  phys_base: 0x%08lx\n",
			mcbsp->id, mcbsp->phys_base);

	/* We write the given config */
	MCBSP_WRITE(mcbsp, SPCR2, config->spcr2);
	MCBSP_WRITE(mcbsp, SPCR1, config->spcr1);
	MCBSP_WRITE(mcbsp, RCR2, config->rcr2);
	MCBSP_WRITE(mcbsp, RCR1, config->rcr1);
	MCBSP_WRITE(mcbsp, XCR2, config->xcr2);
	MCBSP_WRITE(mcbsp, XCR1, config->xcr1);
	MCBSP_WRITE(mcbsp, SRGR2, config->srgr2);
	MCBSP_WRITE(mcbsp, SRGR1, config->srgr1);
	MCBSP_WRITE(mcbsp, MCR2, config->mcr2);
	MCBSP_WRITE(mcbsp, MCR1, config->mcr1);
	MCBSP_WRITE(mcbsp, PCR0, config->pcr0);
	if (cpu_is_omap2430() || cpu_is_omap34xx() || cpu_is_omap44xx()) {
		MCBSP_WRITE(mcbsp, XCCR, config->xccr);
		MCBSP_WRITE(mcbsp, RCCR, config->rccr);
	}
}
EXPORT_SYMBOL(omap_mcbsp_config);

/**
 * omap_mcbsp_dma_params - returns the dma channel number
 * @id - mcbsp id
 * @stream - indicates the direction of data flow (rx or tx)
 *
 * Returns the dma channel number for the rx channel or tx channel
 * based on the value of @stream for the requested mcbsp given by @id
 */
int omap_mcbsp_dma_ch_params(unsigned int id, unsigned int stream)
{
	struct omap_mcbsp *mcbsp;

	if (!omap_mcbsp_check_valid_id(id)) {
		printk(KERN_ERR "%s: Invalid id (%d)\n", __func__, id + 1);
		return -ENODEV;
	}
	mcbsp = id_to_mcbsp_ptr(id);

	if (stream)
		return mcbsp->dma_rx_sync;
	else
		return mcbsp->dma_tx_sync;
}
EXPORT_SYMBOL(omap_mcbsp_dma_ch_params);

/**
 * omap_mcbsp_dma_reg_params - returns the address of mcbsp data register
 * @id - mcbsp id
 * @stream - indicates the direction of data flow (rx or tx)
 *
 * Returns the address of mcbsp data transmit register or data receive register
 * to be used by DMA for transferring/receiving data based on the value of
 * @stream for the requested mcbsp given by @id
 */
int omap_mcbsp_dma_reg_params(unsigned int id, unsigned int stream)
{
	struct omap_mcbsp *mcbsp;
	int data_reg;

	if (!omap_mcbsp_check_valid_id(id)) {
		printk(KERN_ERR "%s: Invalid id (%d)\n", __func__, id + 1);
		return -ENODEV;
	}
	mcbsp = id_to_mcbsp_ptr(id);

	data_reg = mcbsp->phys_dma_base;

	if (mcbsp->mcbsp_config_type < MCBSP_CONFIG_TYPE2) {
		if (stream)
			data_reg += OMAP_MCBSP_REG_DRR1;
		else
			data_reg += OMAP_MCBSP_REG_DXR1;
	} else {
		if (stream)
			data_reg += OMAP_MCBSP_REG_DRR;
		else
			data_reg += OMAP_MCBSP_REG_DXR;
	}

	return data_reg;
}
EXPORT_SYMBOL(omap_mcbsp_dma_reg_params);

#ifdef CONFIG_ARCH_OMAP3
static struct omap_device *find_omap_device_by_dev(struct device *dev)
{
	struct platform_device *pdev = container_of(dev,
					struct platform_device, dev);
	return container_of(pdev, struct omap_device, pdev);
}

static void omap_st_on(struct omap_mcbsp *mcbsp)
{
	unsigned int w;
	struct omap_device *od;

	od = find_omap_device_by_dev(mcbsp->dev);

	/*
	 * Sidetone uses McBSP ICLK - which must not idle when sidetones
	 * are enabled or sidetones start sounding ugly.
	 */
	w = omap2_cm_read_mod_reg(OMAP3430_PER_MOD, CM_AUTOIDLE);
	w &= ~(1 << (mcbsp->id - 2));
	omap2_cm_write_mod_reg(w, OMAP3430_PER_MOD, CM_AUTOIDLE);

	/* Enable McBSP Sidetone */
	w = MCBSP_READ(mcbsp, SSELCR);
	MCBSP_WRITE(mcbsp, SSELCR, w | SIDETONEEN);

	/* Enable Sidetone from Sidetone Core */
	w = MCBSP_ST_READ(mcbsp, SSELCR);
	MCBSP_ST_WRITE(mcbsp, SSELCR, w | ST_SIDETONEEN);
}

static void omap_st_off(struct omap_mcbsp *mcbsp)
{
	unsigned int w;
	struct omap_device *od;

	od = find_omap_device_by_dev(mcbsp->dev);

	w = MCBSP_ST_READ(mcbsp, SSELCR);
	MCBSP_ST_WRITE(mcbsp, SSELCR, w & ~(ST_SIDETONEEN));

	w = MCBSP_READ(mcbsp, SSELCR);
	MCBSP_WRITE(mcbsp, SSELCR, w & ~(SIDETONEEN));

	w = omap2_cm_read_mod_reg(OMAP3430_PER_MOD, CM_AUTOIDLE);
	w |= 1 << (mcbsp->id - 2);
	omap2_cm_write_mod_reg(w, OMAP3430_PER_MOD, CM_AUTOIDLE);
}

static void omap_st_fir_write(struct omap_mcbsp *mcbsp, s16 *fir)
{
	u16 val, i;
	struct omap_device *od;

	od = find_omap_device_by_dev(mcbsp->dev);

	val = MCBSP_ST_READ(mcbsp, SSELCR);

	if (val & ST_COEFFWREN)
		MCBSP_ST_WRITE(mcbsp, SSELCR, val & ~(ST_COEFFWREN));

	MCBSP_ST_WRITE(mcbsp, SSELCR, val | ST_COEFFWREN);

	for (i = 0; i < 128; i++)
		MCBSP_ST_WRITE(mcbsp, SFIRCR, fir[i]);

	i = 0;

	val = MCBSP_ST_READ(mcbsp, SSELCR);
	while (!(val & ST_COEFFWRDONE) && (++i < 1000))
		val = MCBSP_ST_READ(mcbsp, SSELCR);

	MCBSP_ST_WRITE(mcbsp, SSELCR, val & ~(ST_COEFFWREN));

	if (i == 1000)
		dev_err(mcbsp->dev, "McBSP FIR load error!\n");
}

static void omap_st_chgain(struct omap_mcbsp *mcbsp)
{
	u16 w;
	struct omap_mcbsp_st_data *st_data = mcbsp->st_data;
	struct omap_device *od;

	od = find_omap_device_by_dev(mcbsp->dev);

	w = MCBSP_ST_READ(mcbsp, SSELCR);

	MCBSP_ST_WRITE(mcbsp, SGAINCR, ST_CH0GAIN(st_data->ch0gain) | \
		      ST_CH1GAIN(st_data->ch1gain));
}

int omap_st_set_chgain(unsigned int id, int channel, s16 chgain)
{
	struct omap_mcbsp *mcbsp;
	struct omap_mcbsp_st_data *st_data;
	int ret = 0;

	if (!omap_mcbsp_check_valid_id(id)) {
		printk(KERN_ERR "%s: Invalid id (%d)\n", __func__, id + 1);
		return -ENODEV;
	}

	mcbsp = id_to_mcbsp_ptr(id);
	st_data = mcbsp->st_data;

	if (!st_data)
		return -ENOENT;

	spin_lock_irq(&mcbsp->lock);
	if (channel == 0)
		st_data->ch0gain = chgain;
	else if (channel == 1)
		st_data->ch1gain = chgain;
	else
		ret = -EINVAL;

	if (st_data->enabled)
		omap_st_chgain(mcbsp);
	spin_unlock_irq(&mcbsp->lock);

	return ret;
}
EXPORT_SYMBOL(omap_st_set_chgain);

int omap_st_get_chgain(unsigned int id, int channel, s16 *chgain)
{
	struct omap_mcbsp *mcbsp;
	struct omap_mcbsp_st_data *st_data;
	int ret = 0;

	if (!omap_mcbsp_check_valid_id(id)) {
		printk(KERN_ERR "%s: Invalid id (%d)\n", __func__, id + 1);
		return -ENODEV;
	}

	mcbsp = id_to_mcbsp_ptr(id);
	st_data = mcbsp->st_data;

	if (!st_data)
		return -ENOENT;

	spin_lock_irq(&mcbsp->lock);
	if (channel == 0)
		*chgain = st_data->ch0gain;
	else if (channel == 1)
		*chgain = st_data->ch1gain;
	else
		ret = -EINVAL;
	spin_unlock_irq(&mcbsp->lock);

	return ret;
}
EXPORT_SYMBOL(omap_st_get_chgain);

static int omap_st_start(struct omap_mcbsp *mcbsp)
{
	struct omap_mcbsp_st_data *st_data = mcbsp->st_data;

	if (st_data && st_data->enabled && !st_data->running) {
		omap_st_fir_write(mcbsp, st_data->taps);
		omap_st_chgain(mcbsp);

		if (!mcbsp->free) {
			omap_st_on(mcbsp);
			st_data->running = 1;
		}
	}

	return 0;
}

int omap_st_enable(unsigned int id)
{
	struct omap_mcbsp *mcbsp;
	struct omap_mcbsp_st_data *st_data;

	if (!omap_mcbsp_check_valid_id(id)) {
		printk(KERN_ERR "%s: Invalid id (%d)\n", __func__, id + 1);
		return -ENODEV;
	}

	mcbsp = id_to_mcbsp_ptr(id);
	st_data = mcbsp->st_data;

	if (!st_data)
		return -ENODEV;

	spin_lock_irq(&mcbsp->lock);
	st_data->enabled = 1;
	omap_st_start(mcbsp);
	spin_unlock_irq(&mcbsp->lock);

	return 0;
}
EXPORT_SYMBOL(omap_st_enable);

static int omap_st_stop(struct omap_mcbsp *mcbsp)
{
	struct omap_mcbsp_st_data *st_data = mcbsp->st_data;

	if (st_data && st_data->running) {
		if (!mcbsp->free) {
			omap_st_off(mcbsp);
			st_data->running = 0;
		}
	}

	return 0;
}

int omap_st_disable(unsigned int id)
{
	struct omap_mcbsp *mcbsp;
	struct omap_mcbsp_st_data *st_data;
	int ret = 0;

	if (!omap_mcbsp_check_valid_id(id)) {
		printk(KERN_ERR "%s: Invalid id (%d)\n", __func__, id + 1);
		return -ENODEV;
	}

	mcbsp = id_to_mcbsp_ptr(id);
	st_data = mcbsp->st_data;

	if (!st_data)
		return -ENODEV;

	spin_lock_irq(&mcbsp->lock);
	omap_st_stop(mcbsp);
	st_data->enabled = 0;
	spin_unlock_irq(&mcbsp->lock);

	return ret;
}
EXPORT_SYMBOL(omap_st_disable);

int omap_st_is_enabled(unsigned int id)
{
	struct omap_mcbsp *mcbsp;
	struct omap_mcbsp_st_data *st_data;

	if (!omap_mcbsp_check_valid_id(id)) {
		printk(KERN_ERR "%s: Invalid id (%d)\n", __func__, id + 1);
		return -ENODEV;
	}

	mcbsp = id_to_mcbsp_ptr(id);
	st_data = mcbsp->st_data;

	if (!st_data)
		return -ENODEV;


	return st_data->enabled;
}
EXPORT_SYMBOL(omap_st_is_enabled);

/*
 * omap_mcbsp_set_rx_threshold configures the transmit threshold in words.
 * The threshold parameter is 1 based, and it is converted (threshold - 1)
 * for the THRSH2 register.
 */
void omap_mcbsp_set_tx_threshold(unsigned int id, u16 threshold)
{
	struct omap_mcbsp *mcbsp;

	if (!cpu_is_omap34xx() && !cpu_is_omap44xx())
		return;

	if (!omap_mcbsp_check_valid_id(id)) {
		printk(KERN_ERR "%s: Invalid id (%d)\n", __func__, id + 1);
		return;
	}
	mcbsp = id_to_mcbsp_ptr(id);

	if (threshold && threshold <= mcbsp->max_tx_thres)
		MCBSP_WRITE(mcbsp, THRSH2, threshold - 1);
}
EXPORT_SYMBOL(omap_mcbsp_set_tx_threshold);

/*
 * omap_mcbsp_set_rx_threshold configures the receive threshold in words.
 * The threshold parameter is 1 based, and it is converted (threshold - 1)
 * for the THRSH1 register.
 */
void omap_mcbsp_set_rx_threshold(unsigned int id, u16 threshold)
{
	struct omap_mcbsp *mcbsp;

	if (!cpu_is_omap34xx() && !cpu_is_omap44xx())
		return;

	if (!omap_mcbsp_check_valid_id(id)) {
		printk(KERN_ERR "%s: Invalid id (%d)\n", __func__, id + 1);
		return;
	}
	mcbsp = id_to_mcbsp_ptr(id);

	if (threshold && threshold <= mcbsp->max_rx_thres)
		MCBSP_WRITE(mcbsp, THRSH1, threshold - 1);
}
EXPORT_SYMBOL(omap_mcbsp_set_rx_threshold);

/*
 * omap_mcbsp_get_max_tx_thres just return the current configured
 * maximum threshold for transmission
 */
u16 omap_mcbsp_get_max_tx_threshold(unsigned int id)
{
	struct omap_mcbsp *mcbsp;

	if (!omap_mcbsp_check_valid_id(id)) {
		printk(KERN_ERR "%s: Invalid id (%d)\n", __func__, id + 1);
		return -ENODEV;
	}
	mcbsp = id_to_mcbsp_ptr(id);

	return mcbsp->max_tx_thres;
}
EXPORT_SYMBOL(omap_mcbsp_get_max_tx_threshold);

/*
 * omap_mcbsp_get_max_rx_thres just return the current configured
 * maximum threshold for reception
 */
u16 omap_mcbsp_get_max_rx_threshold(unsigned int id)
{
	struct omap_mcbsp *mcbsp;

	if (!omap_mcbsp_check_valid_id(id)) {
		printk(KERN_ERR "%s: Invalid id (%d)\n", __func__, id + 1);
		return -ENODEV;
	}
	mcbsp = id_to_mcbsp_ptr(id);

	return mcbsp->max_rx_thres;
}
EXPORT_SYMBOL(omap_mcbsp_get_max_rx_threshold);

u16 omap_mcbsp_get_fifo_size(unsigned int id)
{
	struct omap_mcbsp *mcbsp;

	if (!omap_mcbsp_check_valid_id(id)) {
		printk(KERN_ERR "%s: Invalid id (%d)\n", __func__, id + 1);
		return -ENODEV;
	}
	mcbsp = id_to_mcbsp_ptr(id);

	return mcbsp->pdata->buffer_size;
}
EXPORT_SYMBOL(omap_mcbsp_get_fifo_size);

/*
 * omap_mcbsp_get_tx_delay returns the number of used slots in the McBSP FIFO
 */
u16 omap_mcbsp_get_tx_delay(unsigned int id)
{
	struct omap_mcbsp *mcbsp;
	u16 buffstat;

	if (!omap_mcbsp_check_valid_id(id)) {
		printk(KERN_ERR "%s: Invalid id (%d)\n", __func__, id + 1);
		return -ENODEV;
	}
	mcbsp = id_to_mcbsp_ptr(id);

	/* Returns the number of free locations in the buffer */
	buffstat = MCBSP_READ(mcbsp, XBUFFSTAT);

	/* Number of slots are different in McBSP ports */
	return mcbsp->pdata->buffer_size - buffstat;
}
EXPORT_SYMBOL(omap_mcbsp_get_tx_delay);

/*
 * omap_mcbsp_get_rx_delay returns the number of free slots in the McBSP FIFO
 * to reach the threshold value (when the DMA will be triggered to read it)
 */
u16 omap_mcbsp_get_rx_delay(unsigned int id)
{
	struct omap_mcbsp *mcbsp;
	u16 buffstat, threshold;

	if (!omap_mcbsp_check_valid_id(id)) {
		printk(KERN_ERR "%s: Invalid id (%d)\n", __func__, id + 1);
		return -ENODEV;
	}
	mcbsp = id_to_mcbsp_ptr(id);

	/* Returns the number of used locations in the buffer */
	buffstat = MCBSP_READ(mcbsp, RBUFFSTAT);
	/* RX threshold */
	threshold = MCBSP_READ(mcbsp, THRSH1);

	/* Return the number of location till we reach the threshold limit */
	if (threshold <= buffstat)
		return 0;
	else
		return threshold - buffstat;
}
EXPORT_SYMBOL(omap_mcbsp_get_rx_delay);

/*
 * omap_mcbsp_get_dma_op_mode just return the current configured
 * operating mode for the mcbsp channel
 */
int omap_mcbsp_get_dma_op_mode(unsigned int id)
{
	struct omap_mcbsp *mcbsp;
	int dma_op_mode;

	if (!omap_mcbsp_check_valid_id(id)) {
		printk(KERN_ERR "%s: Invalid id (%u)\n", __func__, id + 1);
		return -ENODEV;
	}
	mcbsp = id_to_mcbsp_ptr(id);

	dma_op_mode = mcbsp->dma_op_mode;

	return dma_op_mode;
}
EXPORT_SYMBOL(omap_mcbsp_get_dma_op_mode);

static inline void omap34xx_mcbsp_request(struct omap_mcbsp *mcbsp)
{
	struct omap_device *od;

	od = find_omap_device_by_dev(mcbsp->dev);
	/*
	 * Enable wakup behavior, smart idle and all wakeups
	 * REVISIT: some wakeups may be unnecessary
	 */
	if (cpu_is_omap34xx() || cpu_is_omap44xx()) {
		MCBSP_WRITE(mcbsp, WAKEUPEN, XRDYEN | RRDYEN);
	}
}

static inline void omap34xx_mcbsp_free(struct omap_mcbsp *mcbsp)
{
	struct omap_device *od;

	od = find_omap_device_by_dev(mcbsp->dev);

	/*
	 * Disable wakup behavior, smart idle and all wakeups
	 */
	if (cpu_is_omap34xx() || cpu_is_omap44xx()) {
		/*
		 * HW bug workaround - If no_idle mode is taken, we need to
		 * go to smart_idle before going to always_idle, or the
		 * device will not hit retention anymore.
		 */

		MCBSP_WRITE(mcbsp, WAKEUPEN, 0);
	}
}
#else
static inline void omap34xx_mcbsp_request(struct omap_mcbsp *mcbsp) {}
static inline void omap34xx_mcbsp_free(struct omap_mcbsp *mcbsp) {}
static inline void omap_st_start(struct omap_mcbsp *mcbsp) {}
static inline void omap_st_stop(struct omap_mcbsp *mcbsp) {}
#endif

int omap_mcbsp_request(unsigned int id)
{
	struct omap_mcbsp *mcbsp;
	void *reg_cache;
	int err;

	if (!omap_mcbsp_check_valid_id(id)) {
		printk(KERN_ERR "%s: Invalid id (%d)\n", __func__, id + 1);
		return -ENODEV;
	}
	mcbsp = id_to_mcbsp_ptr(id);

	reg_cache = kzalloc(omap_mcbsp_cache_size, GFP_KERNEL);
	if (!reg_cache) {
		return -ENOMEM;
	}

	spin_lock(&mcbsp->lock);
	if (!mcbsp->free) {
		dev_err(mcbsp->dev, "McBSP%d is currently in use\n",
			mcbsp->id);
		err = -EBUSY;
		goto err_kfree;
	}

	mcbsp->free = false;
	mcbsp->reg_cache = reg_cache;
	spin_unlock(&mcbsp->lock);

	if (mcbsp->pdata && mcbsp->pdata->ops && mcbsp->pdata->ops->request)
		mcbsp->pdata->ops->request(id);

	pm_runtime_get_sync(mcbsp->dev);

	/* Do procedure specific to omap34xx arch, if applicable */
	omap34xx_mcbsp_request(mcbsp);

	/*
	 * Make sure that transmitter, receiver and sample-rate generator are
	 * not running before activating IRQs.
	 */
	MCBSP_WRITE(mcbsp, SPCR1, 0);
	MCBSP_WRITE(mcbsp, SPCR2, 0);

	err = request_irq(mcbsp->tx_irq, omap_mcbsp_tx_irq_handler,
				0, "McBSP", (void *)mcbsp);
	if (err != 0) {
		dev_err(mcbsp->dev, "Unable to request TX IRQ %d "
				"for McBSP%d\n", mcbsp->tx_irq,
				mcbsp->id);
		goto err_clk_disable;
	}

	if (mcbsp->rx_irq) {
		err = request_irq(mcbsp->rx_irq,
				omap_mcbsp_rx_irq_handler,
				0, "McBSP", (void *)mcbsp);
		if (err != 0) {
			dev_err(mcbsp->dev, "Unable to request RX IRQ %d "
					"for McBSP%d\n", mcbsp->rx_irq,
					mcbsp->id);
			goto err_free_irq;
		}
	}

	return 0;
err_free_irq:
	free_irq(mcbsp->tx_irq, (void *)mcbsp);
err_clk_disable:
	if (mcbsp->pdata && mcbsp->pdata->ops && mcbsp->pdata->ops->free)
		mcbsp->pdata->ops->free(id);

	/* Do procedure specific to omap34xx arch, if applicable */
	omap34xx_mcbsp_free(mcbsp);

	pm_runtime_put_sync(mcbsp->dev);

	spin_lock(&mcbsp->lock);
	mcbsp->free = true;
	mcbsp->reg_cache = NULL;
err_kfree:
	spin_unlock(&mcbsp->lock);
	kfree(reg_cache);

	return err;
}
EXPORT_SYMBOL(omap_mcbsp_request);

void omap_mcbsp_free(unsigned int id)
{
	struct omap_mcbsp *mcbsp;
	void *reg_cache;

	if (!omap_mcbsp_check_valid_id(id)) {
		printk(KERN_ERR "%s: Invalid id (%d)\n", __func__, id + 1);
		return;
	}
	mcbsp = id_to_mcbsp_ptr(id);

	if (mcbsp->pdata && mcbsp->pdata->ops && mcbsp->pdata->ops->free)
		mcbsp->pdata->ops->free(id);

	/* Do procedure specific to omap34xx arch, if applicable */
	omap34xx_mcbsp_free(mcbsp);

	pm_runtime_put_sync(mcbsp->dev);

	if (mcbsp->rx_irq)
		free_irq(mcbsp->rx_irq, (void *)mcbsp);
	free_irq(mcbsp->tx_irq, (void *)mcbsp);

	reg_cache = mcbsp->reg_cache;

	spin_lock(&mcbsp->lock);
	if (mcbsp->free)
		dev_err(mcbsp->dev, "McBSP%d was not reserved\n", mcbsp->id);
	else
		mcbsp->free = true;
	mcbsp->reg_cache = NULL;
	spin_unlock(&mcbsp->lock);

	if (reg_cache)
		kfree(reg_cache);
}
EXPORT_SYMBOL(omap_mcbsp_free);

/*
 * Here we start the McBSP, by enabling transmitter, receiver or both.
 * If no transmitter or receiver is active prior calling, then sample-rate
 * generator and frame sync are started.
 */
void omap_mcbsp_start(unsigned int id, int tx, int rx)
{
	struct omap_mcbsp *mcbsp;
	int enable_srg = 0;
	u16 w;

	if (!omap_mcbsp_check_valid_id(id)) {
		printk(KERN_ERR "%s: Invalid id (%d)\n", __func__, id + 1);
		return;
	}
	mcbsp = id_to_mcbsp_ptr(id);

	if (cpu_is_omap34xx())
		omap_st_start(mcbsp);

	/* Only enable SRG, if McBSP is master */
	w = MCBSP_READ_CACHE(mcbsp, PCR0);
	if (w & (FSXM | FSRM | CLKXM | CLKRM))
		enable_srg = !((MCBSP_READ_CACHE(mcbsp, SPCR2) |
				MCBSP_READ_CACHE(mcbsp, SPCR1)) & 1);

	if (enable_srg) {
		/* Start the sample generator */
		w = MCBSP_READ_CACHE(mcbsp, SPCR2);
		MCBSP_WRITE(mcbsp, SPCR2, w | (1 << 6));
	}

	/* Enable transmitter and receiver */
	tx &= 1;
	w = MCBSP_READ_CACHE(mcbsp, SPCR2);
	MCBSP_WRITE(mcbsp, SPCR2, w | tx);

	rx &= 1;
	w = MCBSP_READ_CACHE(mcbsp, SPCR1);
	MCBSP_WRITE(mcbsp, SPCR1, w | rx);

	/*
	 * Worst case: CLKSRG*2 = 8000khz: (1/8000) * 2 * 2 usec
	 * REVISIT: 100us may give enough time for two CLKSRG, however
	 * due to some unknown PM related, clock gating etc. reason it
	 * is now at 500us.
	 */
	udelay(500);

	if (enable_srg) {
		/* Start frame sync */
		w = MCBSP_READ_CACHE(mcbsp, SPCR2);
		MCBSP_WRITE(mcbsp, SPCR2, w | (1 << 7));
	}

	if (cpu_is_omap2430() || cpu_is_omap34xx() || cpu_is_omap44xx()) {
		/* Release the transmitter and receiver */
		w = MCBSP_READ_CACHE(mcbsp, XCCR);
		w &= ~(tx ? XDISABLE : 0);
		MCBSP_WRITE(mcbsp, XCCR, w);
		w = MCBSP_READ_CACHE(mcbsp, RCCR);
		w &= ~(rx ? RDISABLE : 0);
		MCBSP_WRITE(mcbsp, RCCR, w);
	}

	/* Dump McBSP Regs */
	omap_mcbsp_dump_reg(id);
}
EXPORT_SYMBOL(omap_mcbsp_start);

void omap_mcbsp_stop(unsigned int id, int tx, int rx)
{
	struct omap_mcbsp *mcbsp;
	int idle;
	u16 w;

	if (!omap_mcbsp_check_valid_id(id)) {
		printk(KERN_ERR "%s: Invalid id (%d)\n", __func__, id + 1);
		return;
	}

	mcbsp = id_to_mcbsp_ptr(id);

	/* Reset transmitter */
	tx &= 1;
	if (cpu_is_omap2430() || cpu_is_omap34xx() || cpu_is_omap44xx()) {
		w = MCBSP_READ_CACHE(mcbsp, XCCR);
		w |= (tx ? XDISABLE : 0);
		MCBSP_WRITE(mcbsp, XCCR, w);
	}
	w = MCBSP_READ_CACHE(mcbsp, SPCR2);
	MCBSP_WRITE(mcbsp, SPCR2, w & ~tx);

	/* Reset receiver */
	rx &= 1;
	if (cpu_is_omap2430() || cpu_is_omap34xx() || cpu_is_omap44xx()) {
		w = MCBSP_READ_CACHE(mcbsp, RCCR);
		w |= (rx ? RDISABLE : 0);
		MCBSP_WRITE(mcbsp, RCCR, w);
	}
	w = MCBSP_READ_CACHE(mcbsp, SPCR1);
	MCBSP_WRITE(mcbsp, SPCR1, w & ~rx);

	idle = !((MCBSP_READ_CACHE(mcbsp, SPCR2) |
			MCBSP_READ_CACHE(mcbsp, SPCR1)) & 1);

	if (idle) {
		/* Reset the sample rate generator */
		w = MCBSP_READ_CACHE(mcbsp, SPCR2);
		MCBSP_WRITE(mcbsp, SPCR2, w & ~(1 << 6));
	}

	if (cpu_is_omap34xx())
		omap_st_stop(mcbsp);
}
EXPORT_SYMBOL(omap_mcbsp_stop);

/*
 * The following functions are only required on an OMAP1-only build.
 * mach-omap2/mcbsp.c contains the real functions
 */
#ifndef CONFIG_ARCH_OMAP2PLUS
int omap2_mcbsp_set_clks_src(u8 id, u8 fck_src_id)
{
	WARN(1, "%s: should never be called on an OMAP1-only kernel\n",
	     __func__);
	return -EINVAL;
}

void omap2_mcbsp1_mux_clkr_src(u8 mux)
{
	WARN(1, "%s: should never be called on an OMAP1-only kernel\n",
	     __func__);
	return;
}

void omap2_mcbsp1_mux_fsr_src(u8 mux)
{
	WARN(1, "%s: should never be called on an OMAP1-only kernel\n",
	     __func__);
	return;
}
#endif

#ifdef CONFIG_ARCH_OMAP3
#define max_thres(m)			(mcbsp->pdata->buffer_size)
#define valid_threshold(m, val)		((val) <= max_thres(m))
#define THRESHOLD_PROP_BUILDER(prop)					\
static ssize_t prop##_show(struct device *dev,				\
			struct device_attribute *attr, char *buf)	\
{									\
	struct omap_mcbsp *mcbsp = dev_get_drvdata(dev);		\
									\
	return sprintf(buf, "%u\n", mcbsp->prop);			\
}									\
									\
static ssize_t prop##_store(struct device *dev,				\
				struct device_attribute *attr,		\
				const char *buf, size_t size)		\
{									\
	struct omap_mcbsp *mcbsp = dev_get_drvdata(dev);		\
	unsigned long val;						\
	int status;							\
									\
	status = strict_strtoul(buf, 0, &val);				\
	if (status)							\
		return status;						\
									\
	if (!valid_threshold(mcbsp, val))				\
		return -EDOM;						\
									\
	mcbsp->prop = val;						\
	return size;							\
}									\
									\
static DEVICE_ATTR(prop, 0644, prop##_show, prop##_store);

THRESHOLD_PROP_BUILDER(max_tx_thres);
THRESHOLD_PROP_BUILDER(max_rx_thres);

static const char *dma_op_modes[] = {
	"element", "threshold", "frame",
};

static ssize_t dma_op_mode_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct omap_mcbsp *mcbsp = dev_get_drvdata(dev);
	int dma_op_mode, i = 0;
	ssize_t len = 0;
	const char * const *s;

	dma_op_mode = mcbsp->dma_op_mode;

	for (s = &dma_op_modes[i]; i < ARRAY_SIZE(dma_op_modes); s++, i++) {
		if (dma_op_mode == i)
			len += sprintf(buf + len, "[%s] ", *s);
		else
			len += sprintf(buf + len, "%s ", *s);
	}
	len += sprintf(buf + len, "\n");

	return len;
}

static ssize_t dma_op_mode_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t size)
{
	struct omap_mcbsp *mcbsp = dev_get_drvdata(dev);
	const char * const *s;
	int i = 0;

	for (s = &dma_op_modes[i]; i < ARRAY_SIZE(dma_op_modes); s++, i++)
		if (sysfs_streq(buf, *s))
			break;

	if (i == ARRAY_SIZE(dma_op_modes))
		return -EINVAL;

	spin_lock_irq(&mcbsp->lock);
	if (!mcbsp->free) {
		size = -EBUSY;
		goto unlock;
	}
	mcbsp->dma_op_mode = i;

unlock:
	spin_unlock_irq(&mcbsp->lock);

	return size;
}

static DEVICE_ATTR(dma_op_mode, 0644, dma_op_mode_show, dma_op_mode_store);

static ssize_t st_taps_show(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	struct omap_mcbsp *mcbsp = dev_get_drvdata(dev);
	struct omap_mcbsp_st_data *st_data = mcbsp->st_data;
	ssize_t status = 0;
	int i;

	spin_lock_irq(&mcbsp->lock);
	for (i = 0; i < st_data->nr_taps; i++)
		status += sprintf(&buf[status], (i ? ", %d" : "%d"),
				  st_data->taps[i]);
	if (i)
		status += sprintf(&buf[status], "\n");
	spin_unlock_irq(&mcbsp->lock);

	return status;
}

static ssize_t st_taps_store(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t size)
{
	struct omap_mcbsp *mcbsp = dev_get_drvdata(dev);
	struct omap_mcbsp_st_data *st_data = mcbsp->st_data;
	int val, tmp, status, i = 0;

	spin_lock_irq(&mcbsp->lock);
	memset(st_data->taps, 0, sizeof(st_data->taps));
	st_data->nr_taps = 0;

	do {
		status = sscanf(buf, "%d%n", &val, &tmp);
		if (status < 0 || status == 0) {
			size = -EINVAL;
			goto out;
		}
		if (val < -32768 || val > 32767) {
			size = -EINVAL;
			goto out;
		}
		st_data->taps[i++] = val;
		buf += tmp;
		if (*buf != ',')
			break;
		buf++;
	} while (1);

	st_data->nr_taps = i;

out:
	spin_unlock_irq(&mcbsp->lock);

	return size;
}

static DEVICE_ATTR(st_taps, 0644, st_taps_show, st_taps_store);

static const struct attribute *additional_attrs[] = {
	&dev_attr_max_tx_thres.attr,
	&dev_attr_max_rx_thres.attr,
	&dev_attr_dma_op_mode.attr,
	NULL,
};

static const struct attribute_group additional_attr_group = {
	.attrs = (struct attribute **)additional_attrs,
};

static inline int __devinit omap_additional_add(struct device *dev)
{
	return sysfs_create_group(&dev->kobj, &additional_attr_group);
}

static inline void __devexit omap_additional_remove(struct device *dev)
{
	sysfs_remove_group(&dev->kobj, &additional_attr_group);
}

static const struct attribute *sidetone_attrs[] = {
	&dev_attr_st_taps.attr,
	NULL,
};

static const struct attribute_group sidetone_attr_group = {
	.attrs = (struct attribute **)sidetone_attrs,
};

static int __devinit omap_st_add(struct omap_mcbsp *mcbsp)
{
	struct platform_device *pdev;
	struct resource *res;
	struct omap_mcbsp_st_data *st_data;
	int err;

	st_data = kzalloc(sizeof(*mcbsp->st_data), GFP_KERNEL);
	if (!st_data) {
		err = -ENOMEM;
		goto err1;
	}

	pdev = container_of(mcbsp->dev, struct platform_device, dev);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "sidetone");
	st_data->io_base_st = ioremap(res->start, resource_size(res));
	if (!st_data->io_base_st) {
		err = -ENOMEM;
		goto err2;
	}

	err = sysfs_create_group(&mcbsp->dev->kobj, &sidetone_attr_group);
	if (err)
		goto err3;

	mcbsp->st_data = st_data;
	return 0;

err3:
	iounmap(st_data->io_base_st);
err2:
	kfree(st_data);
err1:
	return err;

}

static void __devexit omap_st_remove(struct omap_mcbsp *mcbsp)
{
	struct omap_mcbsp_st_data *st_data = mcbsp->st_data;

	if (st_data) {
		sysfs_remove_group(&mcbsp->dev->kobj, &sidetone_attr_group);
		iounmap(st_data->io_base_st);
		kfree(st_data);
	}
}

static inline void __devinit omap34xx_device_init(struct omap_mcbsp *mcbsp)
{
	mcbsp->dma_op_mode = MCBSP_DMA_MODE_ELEMENT;
	if (cpu_is_omap34xx()) {
		/*
		 * Initially configure the maximum thresholds to a safe value.
		 * The McBSP FIFO usage with these values should not go under
		 * 16 locations.
		 * If the whole FIFO without safety buffer is used, than there
		 * is a possibility that the DMA will be not able to push the
		 * new data on time, causing channel shifts in runtime.
		 */
		mcbsp->max_tx_thres = max_thres(mcbsp) - 0x10;
		mcbsp->max_rx_thres = max_thres(mcbsp) - 0x10;
		/*
		 * REVISIT: Set dmap_op_mode to THRESHOLD as default
		 * for mcbsp2 instances.
		 */
		if (omap_additional_add(mcbsp->dev))
			dev_warn(mcbsp->dev,
				"Unable to create additional controls\n");

		if (mcbsp->id == 2 || mcbsp->id == 3)
			if (omap_st_add(mcbsp))
				dev_warn(mcbsp->dev,
				 "Unable to create sidetone controls\n");

	} else {
		mcbsp->max_tx_thres = -EINVAL;
		mcbsp->max_rx_thres = -EINVAL;
	}
}

static inline void __devexit omap34xx_device_exit(struct omap_mcbsp *mcbsp)
{
	if (cpu_is_omap34xx()) {
		omap_additional_remove(mcbsp->dev);

		if (mcbsp->id == 2 || mcbsp->id == 3)
			omap_st_remove(mcbsp);
	}
}
#else
static inline void __devinit omap34xx_device_init(struct omap_mcbsp *mcbsp) {}
static inline void __devexit omap34xx_device_exit(struct omap_mcbsp *mcbsp) {}
#endif /* CONFIG_ARCH_OMAP3 */

/*
 * McBSP1 and McBSP3 are directly mapped on 1610 and 1510.
 * 730 has only 2 McBSP, and both of them are MPU peripherals.
 */
static int __devinit omap_mcbsp_probe(struct platform_device *pdev)
{
	struct omap_mcbsp_platform_data *pdata = pdev->dev.platform_data;
	struct omap_mcbsp *mcbsp;
	int id = pdev->id - 1;
	struct resource *res;
	int ret = 0;

	if (!pdata) {
		dev_err(&pdev->dev, "McBSP device initialized without"
				"platform data\n");
		ret = -EINVAL;
		goto exit;
	}

	dev_dbg(&pdev->dev, "Initializing OMAP McBSP (%d).\n", pdev->id);

	if (id >= omap_mcbsp_count) {
		dev_err(&pdev->dev, "Invalid McBSP device id (%d)\n", id);
		ret = -EINVAL;
		goto exit;
	}

	mcbsp = kzalloc(sizeof(struct omap_mcbsp), GFP_KERNEL);
	if (!mcbsp) {
		ret = -ENOMEM;
		goto exit;
	}

	spin_lock_init(&mcbsp->lock);
	mcbsp->id = id + 1;
	mcbsp->free = true;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "mpu");
	if (!res) {
		res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
		if (!res) {
			dev_err(&pdev->dev, "%s:mcbsp%d has invalid memory"
					"resource\n", __func__, pdev->id);
			ret = -ENOMEM;
			goto exit;
		}
	}
	mcbsp->phys_base = res->start;
	omap_mcbsp_cache_size = resource_size(res);
	mcbsp->io_base = ioremap(res->start, resource_size(res));
	if (!mcbsp->io_base) {
		ret = -ENOMEM;
		goto err_ioremap;
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "dma");
	if (!res)
		mcbsp->phys_dma_base = mcbsp->phys_base;
	else
		mcbsp->phys_dma_base = res->start;

	mcbsp->tx_irq = platform_get_irq_byname(pdev, "tx");
	mcbsp->rx_irq = platform_get_irq_byname(pdev, "rx");

	/* From OMAP4 there will be a single irq line */
	if (mcbsp->tx_irq == -ENXIO)
		mcbsp->tx_irq = platform_get_irq(pdev, 0);

	res = platform_get_resource_byname(pdev, IORESOURCE_DMA, "rx");
	if (!res) {
		dev_err(&pdev->dev, "%s:mcbsp%d has invalid rx DMA channel\n",
					__func__, pdev->id);
		ret = -ENODEV;
		goto err_res;
	}
	mcbsp->dma_rx_sync = res->start;

	res = platform_get_resource_byname(pdev, IORESOURCE_DMA, "tx");
	if (!res) {
		dev_err(&pdev->dev, "%s:mcbsp%d has invalid tx DMA channel\n",
					__func__, pdev->id);
		ret = -ENODEV;
		goto err_res;
	}
	mcbsp->dma_tx_sync = res->start;

	mcbsp->fclk = clk_get(&pdev->dev, "fck");
	if (IS_ERR(mcbsp->fclk)) {
		ret = PTR_ERR(mcbsp->fclk);
		dev_err(&pdev->dev, "unable to get fck: %d\n", ret);
		goto err_res;
	}

	mcbsp->pdata = pdata;
	mcbsp->dev = &pdev->dev;
	mcbsp_ptr[id] = mcbsp;
	mcbsp->mcbsp_config_type = pdata->mcbsp_config_type;
	platform_set_drvdata(pdev, mcbsp);
	pm_runtime_enable(mcbsp->dev);

	/* Initialize mcbsp properties for OMAP34XX if needed / applicable */
	omap34xx_device_init(mcbsp);

	return 0;

err_res:
	iounmap(mcbsp->io_base);
err_ioremap:
	kfree(mcbsp);
exit:
	return ret;
}

static int __devexit omap_mcbsp_remove(struct platform_device *pdev)
{
	struct omap_mcbsp *mcbsp = platform_get_drvdata(pdev);

	platform_set_drvdata(pdev, NULL);
	if (mcbsp) {

		if (mcbsp->pdata && mcbsp->pdata->ops &&
				mcbsp->pdata->ops->free)
			mcbsp->pdata->ops->free(mcbsp->id);

		omap34xx_device_exit(mcbsp);

		clk_put(mcbsp->fclk);

		iounmap(mcbsp->io_base);
		kfree(mcbsp);
	}

	return 0;
}

static struct platform_driver omap_mcbsp_driver = {
	.probe		= omap_mcbsp_probe,
	.remove		= __devexit_p(omap_mcbsp_remove),
	.driver		= {
		.name	= "omap-mcbsp",
	},
};

int __init omap_mcbsp_init(void)
{
	/* Register the McBSP driver */
	return platform_driver_register(&omap_mcbsp_driver);
}
