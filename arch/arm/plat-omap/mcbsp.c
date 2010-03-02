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
#include <linux/wait.h>
#include <linux/completion.h>
#include <linux/interrupt.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>

#include <plat/dma.h>
#include <plat/mcbsp.h>

#include "../mach-omap2/cm-regbits-34xx.h"

struct omap_mcbsp **mcbsp_ptr;
int omap_mcbsp_count, omap_mcbsp_cache_size;

void omap_mcbsp_write(struct omap_mcbsp *mcbsp, u16 reg, u32 val)
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

int omap_mcbsp_read(struct omap_mcbsp *mcbsp, u16 reg, bool from_cache)
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
void omap_mcbsp_st_write(struct omap_mcbsp *mcbsp, u16 reg, u32 val)
{
	__raw_writel(val, mcbsp->st_data->io_base_st + reg);
}

int omap_mcbsp_st_read(struct omap_mcbsp *mcbsp, u16 reg)
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

#define omap_mcbsp_check_valid_id(id)	(id < omap_mcbsp_count)
#define id_to_mcbsp_ptr(id)		mcbsp_ptr[id];

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
		MCBSP_WRITE(mcbsp_tx, SPCR2,
			    MCBSP_READ_CACHE(mcbsp_tx, SPCR2) & ~(XSYNC_ERR));
	} else {
		complete(&mcbsp_tx->tx_irq_completion);
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
		MCBSP_WRITE(mcbsp_rx, SPCR1,
			    MCBSP_READ_CACHE(mcbsp_rx, SPCR1) & ~(RSYNC_ERR));
	} else {
		complete(&mcbsp_rx->tx_irq_completion);
	}

	return IRQ_HANDLED;
}

static void omap_mcbsp_tx_dma_callback(int lch, u16 ch_status, void *data)
{
	struct omap_mcbsp *mcbsp_dma_tx = data;

	dev_dbg(mcbsp_dma_tx->dev, "TX DMA callback : 0x%x\n",
		MCBSP_READ(mcbsp_dma_tx, SPCR2));

	/* We can free the channels */
	omap_free_dma(mcbsp_dma_tx->dma_tx_lch);
	mcbsp_dma_tx->dma_tx_lch = -1;

	complete(&mcbsp_dma_tx->tx_dma_completion);
}

static void omap_mcbsp_rx_dma_callback(int lch, u16 ch_status, void *data)
{
	struct omap_mcbsp *mcbsp_dma_rx = data;

	dev_dbg(mcbsp_dma_rx->dev, "RX DMA callback : 0x%x\n",
		MCBSP_READ(mcbsp_dma_rx, SPCR2));

	/* We can free the channels */
	omap_free_dma(mcbsp_dma_rx->dma_rx_lch);
	mcbsp_dma_rx->dma_rx_lch = -1;

	complete(&mcbsp_dma_rx->rx_dma_completion);
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

#ifdef CONFIG_ARCH_OMAP3
static void omap_st_on(struct omap_mcbsp *mcbsp)
{
	unsigned int w;

	/*
	 * Sidetone uses McBSP ICLK - which must not idle when sidetones
	 * are enabled or sidetones start sounding ugly.
	 */
	w = cm_read_mod_reg(OMAP3430_PER_MOD, CM_AUTOIDLE);
	w &= ~(1 << (mcbsp->id - 2));
	cm_write_mod_reg(w, OMAP3430_PER_MOD, CM_AUTOIDLE);

	/* Enable McBSP Sidetone */
	w = MCBSP_READ(mcbsp, SSELCR);
	MCBSP_WRITE(mcbsp, SSELCR, w | SIDETONEEN);

	w = MCBSP_ST_READ(mcbsp, SYSCONFIG);
	MCBSP_ST_WRITE(mcbsp, SYSCONFIG, w & ~(ST_AUTOIDLE));

	/* Enable Sidetone from Sidetone Core */
	w = MCBSP_ST_READ(mcbsp, SSELCR);
	MCBSP_ST_WRITE(mcbsp, SSELCR, w | ST_SIDETONEEN);
}

static void omap_st_off(struct omap_mcbsp *mcbsp)
{
	unsigned int w;

	w = MCBSP_ST_READ(mcbsp, SSELCR);
	MCBSP_ST_WRITE(mcbsp, SSELCR, w & ~(ST_SIDETONEEN));

	w = MCBSP_ST_READ(mcbsp, SYSCONFIG);
	MCBSP_ST_WRITE(mcbsp, SYSCONFIG, w | ST_AUTOIDLE);

	w = MCBSP_READ(mcbsp, SSELCR);
	MCBSP_WRITE(mcbsp, SSELCR, w & ~(SIDETONEEN));

	w = cm_read_mod_reg(OMAP3430_PER_MOD, CM_AUTOIDLE);
	w |= 1 << (mcbsp->id - 2);
	cm_write_mod_reg(w, OMAP3430_PER_MOD, CM_AUTOIDLE);
}

static void omap_st_fir_write(struct omap_mcbsp *mcbsp, s16 *fir)
{
	u16 val, i;

	val = MCBSP_ST_READ(mcbsp, SYSCONFIG);
	MCBSP_ST_WRITE(mcbsp, SYSCONFIG, val & ~(ST_AUTOIDLE));

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

	w = MCBSP_ST_READ(mcbsp, SYSCONFIG);
	MCBSP_ST_WRITE(mcbsp, SYSCONFIG, w & ~(ST_AUTOIDLE));

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
 * omap_mcbsp_set_tx_threshold configures how to deal
 * with transmit threshold. the threshold value and handler can be
 * configure in here.
 */
void omap_mcbsp_set_tx_threshold(unsigned int id, u16 threshold)
{
	struct omap_mcbsp *mcbsp;

	if (!cpu_is_omap34xx())
		return;

	if (!omap_mcbsp_check_valid_id(id)) {
		printk(KERN_ERR "%s: Invalid id (%d)\n", __func__, id + 1);
		return;
	}
	mcbsp = id_to_mcbsp_ptr(id);

	MCBSP_WRITE(mcbsp, THRSH2, threshold);
}
EXPORT_SYMBOL(omap_mcbsp_set_tx_threshold);

/*
 * omap_mcbsp_set_rx_threshold configures how to deal
 * with receive threshold. the threshold value and handler can be
 * configure in here.
 */
void omap_mcbsp_set_rx_threshold(unsigned int id, u16 threshold)
{
	struct omap_mcbsp *mcbsp;

	if (!cpu_is_omap34xx())
		return;

	if (!omap_mcbsp_check_valid_id(id)) {
		printk(KERN_ERR "%s: Invalid id (%d)\n", __func__, id + 1);
		return;
	}
	mcbsp = id_to_mcbsp_ptr(id);

	MCBSP_WRITE(mcbsp, THRSH1, threshold);
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
	/*
	 * Enable wakup behavior, smart idle and all wakeups
	 * REVISIT: some wakeups may be unnecessary
	 */
	if (cpu_is_omap34xx()) {
		u16 syscon;

		syscon = MCBSP_READ(mcbsp, SYSCON);
		syscon &= ~(ENAWAKEUP | SIDLEMODE(0x03) | CLOCKACTIVITY(0x03));

		if (mcbsp->dma_op_mode == MCBSP_DMA_MODE_THRESHOLD) {
			syscon |= (ENAWAKEUP | SIDLEMODE(0x02) |
					CLOCKACTIVITY(0x02));
			MCBSP_WRITE(mcbsp, WAKEUPEN, XRDYEN | RRDYEN);
		} else {
			syscon |= SIDLEMODE(0x01);
		}

		MCBSP_WRITE(mcbsp, SYSCON, syscon);
	}
}

static inline void omap34xx_mcbsp_free(struct omap_mcbsp *mcbsp)
{
	/*
	 * Disable wakup behavior, smart idle and all wakeups
	 */
	if (cpu_is_omap34xx()) {
		u16 syscon;

		syscon = MCBSP_READ(mcbsp, SYSCON);
		syscon &= ~(ENAWAKEUP | SIDLEMODE(0x03) | CLOCKACTIVITY(0x03));
		/*
		 * HW bug workaround - If no_idle mode is taken, we need to
		 * go to smart_idle before going to always_idle, or the
		 * device will not hit retention anymore.
		 */
		syscon |= SIDLEMODE(0x02);
		MCBSP_WRITE(mcbsp, SYSCON, syscon);

		syscon &= ~(SIDLEMODE(0x03));
		MCBSP_WRITE(mcbsp, SYSCON, syscon);

		MCBSP_WRITE(mcbsp, WAKEUPEN, 0);
	}
}
#else
static inline void omap34xx_mcbsp_request(struct omap_mcbsp *mcbsp) {}
static inline void omap34xx_mcbsp_free(struct omap_mcbsp *mcbsp) {}
static inline void omap_st_start(struct omap_mcbsp *mcbsp) {}
static inline void omap_st_stop(struct omap_mcbsp *mcbsp) {}
#endif

/*
 * We can choose between IRQ based or polled IO.
 * This needs to be called before omap_mcbsp_request().
 */
int omap_mcbsp_set_io_type(unsigned int id, omap_mcbsp_io_type_t io_type)
{
	struct omap_mcbsp *mcbsp;

	if (!omap_mcbsp_check_valid_id(id)) {
		printk(KERN_ERR "%s: Invalid id (%d)\n", __func__, id + 1);
		return -ENODEV;
	}
	mcbsp = id_to_mcbsp_ptr(id);

	spin_lock(&mcbsp->lock);

	if (!mcbsp->free) {
		dev_err(mcbsp->dev, "McBSP%d is currently in use\n",
			mcbsp->id);
		spin_unlock(&mcbsp->lock);
		return -EINVAL;
	}

	mcbsp->io_type = io_type;

	spin_unlock(&mcbsp->lock);

	return 0;
}
EXPORT_SYMBOL(omap_mcbsp_set_io_type);

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

	mcbsp->free = 0;
	mcbsp->reg_cache = reg_cache;
	spin_unlock(&mcbsp->lock);

	if (mcbsp->pdata && mcbsp->pdata->ops && mcbsp->pdata->ops->request)
		mcbsp->pdata->ops->request(id);

	clk_enable(mcbsp->iclk);
	clk_enable(mcbsp->fclk);

	/* Do procedure specific to omap34xx arch, if applicable */
	omap34xx_mcbsp_request(mcbsp);

	/*
	 * Make sure that transmitter, receiver and sample-rate generator are
	 * not running before activating IRQs.
	 */
	MCBSP_WRITE(mcbsp, SPCR1, 0);
	MCBSP_WRITE(mcbsp, SPCR2, 0);

	if (mcbsp->io_type == OMAP_MCBSP_IRQ_IO) {
		/* We need to get IRQs here */
		init_completion(&mcbsp->tx_irq_completion);
		err = request_irq(mcbsp->tx_irq, omap_mcbsp_tx_irq_handler,
					0, "McBSP", (void *)mcbsp);
		if (err != 0) {
			dev_err(mcbsp->dev, "Unable to request TX IRQ %d "
					"for McBSP%d\n", mcbsp->tx_irq,
					mcbsp->id);
			goto err_clk_disable;
		}

		init_completion(&mcbsp->rx_irq_completion);
		err = request_irq(mcbsp->rx_irq, omap_mcbsp_rx_irq_handler,
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

	clk_disable(mcbsp->fclk);
	clk_disable(mcbsp->iclk);

	spin_lock(&mcbsp->lock);
	mcbsp->free = 1;
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

	clk_disable(mcbsp->fclk);
	clk_disable(mcbsp->iclk);

	if (mcbsp->io_type == OMAP_MCBSP_IRQ_IO) {
		/* Free IRQs */
		free_irq(mcbsp->rx_irq, (void *)mcbsp);
		free_irq(mcbsp->tx_irq, (void *)mcbsp);
	}

	reg_cache = mcbsp->reg_cache;

	spin_lock(&mcbsp->lock);
	if (mcbsp->free)
		dev_err(mcbsp->dev, "McBSP%d was not reserved\n", mcbsp->id);
	else
		mcbsp->free = 1;
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
	int idle;
	u16 w;

	if (!omap_mcbsp_check_valid_id(id)) {
		printk(KERN_ERR "%s: Invalid id (%d)\n", __func__, id + 1);
		return;
	}
	mcbsp = id_to_mcbsp_ptr(id);

	if (cpu_is_omap34xx())
		omap_st_start(mcbsp);

	mcbsp->rx_word_length = (MCBSP_READ_CACHE(mcbsp, RCR1) >> 5) & 0x7;
	mcbsp->tx_word_length = (MCBSP_READ_CACHE(mcbsp, XCR1) >> 5) & 0x7;

	idle = !((MCBSP_READ_CACHE(mcbsp, SPCR2) |
			MCBSP_READ_CACHE(mcbsp, SPCR1)) & 1);

	if (idle) {
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

	if (idle) {
		/* Start frame sync */
		w = MCBSP_READ_CACHE(mcbsp, SPCR2);
		MCBSP_WRITE(mcbsp, SPCR2, w | (1 << 7));
	}

	if (cpu_is_omap2430() || cpu_is_omap34xx()) {
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
	if (cpu_is_omap2430() || cpu_is_omap34xx()) {
		w = MCBSP_READ_CACHE(mcbsp, XCCR);
		w |= (tx ? XDISABLE : 0);
		MCBSP_WRITE(mcbsp, XCCR, w);
	}
	w = MCBSP_READ_CACHE(mcbsp, SPCR2);
	MCBSP_WRITE(mcbsp, SPCR2, w & ~tx);

	/* Reset receiver */
	rx &= 1;
	if (cpu_is_omap2430() || cpu_is_omap34xx()) {
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

/* polled mcbsp i/o operations */
int omap_mcbsp_pollwrite(unsigned int id, u16 buf)
{
	struct omap_mcbsp *mcbsp;

	if (!omap_mcbsp_check_valid_id(id)) {
		printk(KERN_ERR "%s: Invalid id (%d)\n", __func__, id + 1);
		return -ENODEV;
	}

	mcbsp = id_to_mcbsp_ptr(id);

	MCBSP_WRITE(mcbsp, DXR1, buf);
	/* if frame sync error - clear the error */
	if (MCBSP_READ(mcbsp, SPCR2) & XSYNC_ERR) {
		/* clear error */
		MCBSP_WRITE(mcbsp, SPCR2,
				MCBSP_READ_CACHE(mcbsp, SPCR2) & (~XSYNC_ERR));
		/* resend */
		return -1;
	} else {
		/* wait for transmit confirmation */
		int attemps = 0;
		while (!(MCBSP_READ(mcbsp, SPCR2) & XRDY)) {
			if (attemps++ > 1000) {
				MCBSP_WRITE(mcbsp, SPCR2,
						MCBSP_READ_CACHE(mcbsp, SPCR2) &
						(~XRST));
				udelay(10);
				MCBSP_WRITE(mcbsp, SPCR2,
						MCBSP_READ_CACHE(mcbsp, SPCR2) |
						(XRST));
				udelay(10);
				dev_err(mcbsp->dev, "Could not write to"
					" McBSP%d Register\n", mcbsp->id);
				return -2;
			}
		}
	}

	return 0;
}
EXPORT_SYMBOL(omap_mcbsp_pollwrite);

int omap_mcbsp_pollread(unsigned int id, u16 *buf)
{
	struct omap_mcbsp *mcbsp;

	if (!omap_mcbsp_check_valid_id(id)) {
		printk(KERN_ERR "%s: Invalid id (%d)\n", __func__, id + 1);
		return -ENODEV;
	}
	mcbsp = id_to_mcbsp_ptr(id);

	/* if frame sync error - clear the error */
	if (MCBSP_READ(mcbsp, SPCR1) & RSYNC_ERR) {
		/* clear error */
		MCBSP_WRITE(mcbsp, SPCR1,
				MCBSP_READ_CACHE(mcbsp, SPCR1) & (~RSYNC_ERR));
		/* resend */
		return -1;
	} else {
		/* wait for recieve confirmation */
		int attemps = 0;
		while (!(MCBSP_READ(mcbsp, SPCR1) & RRDY)) {
			if (attemps++ > 1000) {
				MCBSP_WRITE(mcbsp, SPCR1,
						MCBSP_READ_CACHE(mcbsp, SPCR1) &
						(~RRST));
				udelay(10);
				MCBSP_WRITE(mcbsp, SPCR1,
						MCBSP_READ_CACHE(mcbsp, SPCR1) |
						(RRST));
				udelay(10);
				dev_err(mcbsp->dev, "Could not read from"
					" McBSP%d Register\n", mcbsp->id);
				return -2;
			}
		}
	}
	*buf = MCBSP_READ(mcbsp, DRR1);

	return 0;
}
EXPORT_SYMBOL(omap_mcbsp_pollread);

/*
 * IRQ based word transmission.
 */
void omap_mcbsp_xmit_word(unsigned int id, u32 word)
{
	struct omap_mcbsp *mcbsp;
	omap_mcbsp_word_length word_length;

	if (!omap_mcbsp_check_valid_id(id)) {
		printk(KERN_ERR "%s: Invalid id (%d)\n", __func__, id + 1);
		return;
	}

	mcbsp = id_to_mcbsp_ptr(id);
	word_length = mcbsp->tx_word_length;

	wait_for_completion(&mcbsp->tx_irq_completion);

	if (word_length > OMAP_MCBSP_WORD_16)
		MCBSP_WRITE(mcbsp, DXR2, word >> 16);
	MCBSP_WRITE(mcbsp, DXR1, word & 0xffff);
}
EXPORT_SYMBOL(omap_mcbsp_xmit_word);

u32 omap_mcbsp_recv_word(unsigned int id)
{
	struct omap_mcbsp *mcbsp;
	u16 word_lsb, word_msb = 0;
	omap_mcbsp_word_length word_length;

	if (!omap_mcbsp_check_valid_id(id)) {
		printk(KERN_ERR "%s: Invalid id (%d)\n", __func__, id + 1);
		return -ENODEV;
	}
	mcbsp = id_to_mcbsp_ptr(id);

	word_length = mcbsp->rx_word_length;

	wait_for_completion(&mcbsp->rx_irq_completion);

	if (word_length > OMAP_MCBSP_WORD_16)
		word_msb = MCBSP_READ(mcbsp, DRR2);
	word_lsb = MCBSP_READ(mcbsp, DRR1);

	return (word_lsb | (word_msb << 16));
}
EXPORT_SYMBOL(omap_mcbsp_recv_word);

int omap_mcbsp_spi_master_xmit_word_poll(unsigned int id, u32 word)
{
	struct omap_mcbsp *mcbsp;
	omap_mcbsp_word_length tx_word_length;
	omap_mcbsp_word_length rx_word_length;
	u16 spcr2, spcr1, attempts = 0, word_lsb, word_msb = 0;

	if (!omap_mcbsp_check_valid_id(id)) {
		printk(KERN_ERR "%s: Invalid id (%d)\n", __func__, id + 1);
		return -ENODEV;
	}
	mcbsp = id_to_mcbsp_ptr(id);
	tx_word_length = mcbsp->tx_word_length;
	rx_word_length = mcbsp->rx_word_length;

	if (tx_word_length != rx_word_length)
		return -EINVAL;

	/* First we wait for the transmitter to be ready */
	spcr2 = MCBSP_READ(mcbsp, SPCR2);
	while (!(spcr2 & XRDY)) {
		spcr2 = MCBSP_READ(mcbsp, SPCR2);
		if (attempts++ > 1000) {
			/* We must reset the transmitter */
			MCBSP_WRITE(mcbsp, SPCR2,
				    MCBSP_READ_CACHE(mcbsp, SPCR2) & (~XRST));
			udelay(10);
			MCBSP_WRITE(mcbsp, SPCR2,
				    MCBSP_READ_CACHE(mcbsp, SPCR2) | XRST);
			udelay(10);
			dev_err(mcbsp->dev, "McBSP%d transmitter not "
				"ready\n", mcbsp->id);
			return -EAGAIN;
		}
	}

	/* Now we can push the data */
	if (tx_word_length > OMAP_MCBSP_WORD_16)
		MCBSP_WRITE(mcbsp, DXR2, word >> 16);
	MCBSP_WRITE(mcbsp, DXR1, word & 0xffff);

	/* We wait for the receiver to be ready */
	spcr1 = MCBSP_READ(mcbsp, SPCR1);
	while (!(spcr1 & RRDY)) {
		spcr1 = MCBSP_READ(mcbsp, SPCR1);
		if (attempts++ > 1000) {
			/* We must reset the receiver */
			MCBSP_WRITE(mcbsp, SPCR1,
				    MCBSP_READ_CACHE(mcbsp, SPCR1) & (~RRST));
			udelay(10);
			MCBSP_WRITE(mcbsp, SPCR1,
				    MCBSP_READ_CACHE(mcbsp, SPCR1) | RRST);
			udelay(10);
			dev_err(mcbsp->dev, "McBSP%d receiver not "
				"ready\n", mcbsp->id);
			return -EAGAIN;
		}
	}

	/* Receiver is ready, let's read the dummy data */
	if (rx_word_length > OMAP_MCBSP_WORD_16)
		word_msb = MCBSP_READ(mcbsp, DRR2);
	word_lsb = MCBSP_READ(mcbsp, DRR1);

	return 0;
}
EXPORT_SYMBOL(omap_mcbsp_spi_master_xmit_word_poll);

int omap_mcbsp_spi_master_recv_word_poll(unsigned int id, u32 *word)
{
	struct omap_mcbsp *mcbsp;
	u32 clock_word = 0;
	omap_mcbsp_word_length tx_word_length;
	omap_mcbsp_word_length rx_word_length;
	u16 spcr2, spcr1, attempts = 0, word_lsb, word_msb = 0;

	if (!omap_mcbsp_check_valid_id(id)) {
		printk(KERN_ERR "%s: Invalid id (%d)\n", __func__, id + 1);
		return -ENODEV;
	}

	mcbsp = id_to_mcbsp_ptr(id);

	tx_word_length = mcbsp->tx_word_length;
	rx_word_length = mcbsp->rx_word_length;

	if (tx_word_length != rx_word_length)
		return -EINVAL;

	/* First we wait for the transmitter to be ready */
	spcr2 = MCBSP_READ(mcbsp, SPCR2);
	while (!(spcr2 & XRDY)) {
		spcr2 = MCBSP_READ(mcbsp, SPCR2);
		if (attempts++ > 1000) {
			/* We must reset the transmitter */
			MCBSP_WRITE(mcbsp, SPCR2,
				    MCBSP_READ_CACHE(mcbsp, SPCR2) & (~XRST));
			udelay(10);
			MCBSP_WRITE(mcbsp, SPCR2,
				    MCBSP_READ_CACHE(mcbsp, SPCR2) | XRST);
			udelay(10);
			dev_err(mcbsp->dev, "McBSP%d transmitter not "
				"ready\n", mcbsp->id);
			return -EAGAIN;
		}
	}

	/* We first need to enable the bus clock */
	if (tx_word_length > OMAP_MCBSP_WORD_16)
		MCBSP_WRITE(mcbsp, DXR2, clock_word >> 16);
	MCBSP_WRITE(mcbsp, DXR1, clock_word & 0xffff);

	/* We wait for the receiver to be ready */
	spcr1 = MCBSP_READ(mcbsp, SPCR1);
	while (!(spcr1 & RRDY)) {
		spcr1 = MCBSP_READ(mcbsp, SPCR1);
		if (attempts++ > 1000) {
			/* We must reset the receiver */
			MCBSP_WRITE(mcbsp, SPCR1,
				    MCBSP_READ_CACHE(mcbsp, SPCR1) & (~RRST));
			udelay(10);
			MCBSP_WRITE(mcbsp, SPCR1,
				    MCBSP_READ_CACHE(mcbsp, SPCR1) | RRST);
			udelay(10);
			dev_err(mcbsp->dev, "McBSP%d receiver not "
				"ready\n", mcbsp->id);
			return -EAGAIN;
		}
	}

	/* Receiver is ready, there is something for us */
	if (rx_word_length > OMAP_MCBSP_WORD_16)
		word_msb = MCBSP_READ(mcbsp, DRR2);
	word_lsb = MCBSP_READ(mcbsp, DRR1);

	word[0] = (word_lsb | (word_msb << 16));

	return 0;
}
EXPORT_SYMBOL(omap_mcbsp_spi_master_recv_word_poll);

/*
 * Simple DMA based buffer rx/tx routines.
 * Nothing fancy, just a single buffer tx/rx through DMA.
 * The DMA resources are released once the transfer is done.
 * For anything fancier, you should use your own customized DMA
 * routines and callbacks.
 */
int omap_mcbsp_xmit_buffer(unsigned int id, dma_addr_t buffer,
				unsigned int length)
{
	struct omap_mcbsp *mcbsp;
	int dma_tx_ch;
	int src_port = 0;
	int dest_port = 0;
	int sync_dev = 0;

	if (!omap_mcbsp_check_valid_id(id)) {
		printk(KERN_ERR "%s: Invalid id (%d)\n", __func__, id + 1);
		return -ENODEV;
	}
	mcbsp = id_to_mcbsp_ptr(id);

	if (omap_request_dma(mcbsp->dma_tx_sync, "McBSP TX",
				omap_mcbsp_tx_dma_callback,
				mcbsp,
				&dma_tx_ch)) {
		dev_err(mcbsp->dev, " Unable to request DMA channel for "
				"McBSP%d TX. Trying IRQ based TX\n",
				mcbsp->id);
		return -EAGAIN;
	}
	mcbsp->dma_tx_lch = dma_tx_ch;

	dev_err(mcbsp->dev, "McBSP%d TX DMA on channel %d\n", mcbsp->id,
		dma_tx_ch);

	init_completion(&mcbsp->tx_dma_completion);

	if (cpu_class_is_omap1()) {
		src_port = OMAP_DMA_PORT_TIPB;
		dest_port = OMAP_DMA_PORT_EMIFF;
	}
	if (cpu_class_is_omap2())
		sync_dev = mcbsp->dma_tx_sync;

	omap_set_dma_transfer_params(mcbsp->dma_tx_lch,
				     OMAP_DMA_DATA_TYPE_S16,
				     length >> 1, 1,
				     OMAP_DMA_SYNC_ELEMENT,
	 sync_dev, 0);

	omap_set_dma_dest_params(mcbsp->dma_tx_lch,
				 src_port,
				 OMAP_DMA_AMODE_CONSTANT,
				 mcbsp->phys_base + OMAP_MCBSP_REG_DXR1,
				 0, 0);

	omap_set_dma_src_params(mcbsp->dma_tx_lch,
				dest_port,
				OMAP_DMA_AMODE_POST_INC,
				buffer,
				0, 0);

	omap_start_dma(mcbsp->dma_tx_lch);
	wait_for_completion(&mcbsp->tx_dma_completion);

	return 0;
}
EXPORT_SYMBOL(omap_mcbsp_xmit_buffer);

int omap_mcbsp_recv_buffer(unsigned int id, dma_addr_t buffer,
				unsigned int length)
{
	struct omap_mcbsp *mcbsp;
	int dma_rx_ch;
	int src_port = 0;
	int dest_port = 0;
	int sync_dev = 0;

	if (!omap_mcbsp_check_valid_id(id)) {
		printk(KERN_ERR "%s: Invalid id (%d)\n", __func__, id + 1);
		return -ENODEV;
	}
	mcbsp = id_to_mcbsp_ptr(id);

	if (omap_request_dma(mcbsp->dma_rx_sync, "McBSP RX",
				omap_mcbsp_rx_dma_callback,
				mcbsp,
				&dma_rx_ch)) {
		dev_err(mcbsp->dev, "Unable to request DMA channel for "
				"McBSP%d RX. Trying IRQ based RX\n",
				mcbsp->id);
		return -EAGAIN;
	}
	mcbsp->dma_rx_lch = dma_rx_ch;

	dev_err(mcbsp->dev, "McBSP%d RX DMA on channel %d\n", mcbsp->id,
		dma_rx_ch);

	init_completion(&mcbsp->rx_dma_completion);

	if (cpu_class_is_omap1()) {
		src_port = OMAP_DMA_PORT_TIPB;
		dest_port = OMAP_DMA_PORT_EMIFF;
	}
	if (cpu_class_is_omap2())
		sync_dev = mcbsp->dma_rx_sync;

	omap_set_dma_transfer_params(mcbsp->dma_rx_lch,
					OMAP_DMA_DATA_TYPE_S16,
					length >> 1, 1,
					OMAP_DMA_SYNC_ELEMENT,
					sync_dev, 0);

	omap_set_dma_src_params(mcbsp->dma_rx_lch,
				src_port,
				OMAP_DMA_AMODE_CONSTANT,
				mcbsp->phys_base + OMAP_MCBSP_REG_DRR1,
				0, 0);

	omap_set_dma_dest_params(mcbsp->dma_rx_lch,
					dest_port,
					OMAP_DMA_AMODE_POST_INC,
					buffer,
					0, 0);

	omap_start_dma(mcbsp->dma_rx_lch);
	wait_for_completion(&mcbsp->rx_dma_completion);

	return 0;
}
EXPORT_SYMBOL(omap_mcbsp_recv_buffer);

/*
 * SPI wrapper.
 * Since SPI setup is much simpler than the generic McBSP one,
 * this wrapper just need an omap_mcbsp_spi_cfg structure as an input.
 * Once this is done, you can call omap_mcbsp_start().
 */
void omap_mcbsp_set_spi_mode(unsigned int id,
				const struct omap_mcbsp_spi_cfg *spi_cfg)
{
	struct omap_mcbsp *mcbsp;
	struct omap_mcbsp_reg_cfg mcbsp_cfg;

	if (!omap_mcbsp_check_valid_id(id)) {
		printk(KERN_ERR "%s: Invalid id (%d)\n", __func__, id + 1);
		return;
	}
	mcbsp = id_to_mcbsp_ptr(id);

	memset(&mcbsp_cfg, 0, sizeof(struct omap_mcbsp_reg_cfg));

	/* SPI has only one frame */
	mcbsp_cfg.rcr1 |= (RWDLEN1(spi_cfg->word_length) | RFRLEN1(0));
	mcbsp_cfg.xcr1 |= (XWDLEN1(spi_cfg->word_length) | XFRLEN1(0));

	/* Clock stop mode */
	if (spi_cfg->clk_stp_mode == OMAP_MCBSP_CLK_STP_MODE_NO_DELAY)
		mcbsp_cfg.spcr1 |= (1 << 12);
	else
		mcbsp_cfg.spcr1 |= (3 << 11);

	/* Set clock parities */
	if (spi_cfg->rx_clock_polarity == OMAP_MCBSP_CLK_RISING)
		mcbsp_cfg.pcr0 |= CLKRP;
	else
		mcbsp_cfg.pcr0 &= ~CLKRP;

	if (spi_cfg->tx_clock_polarity == OMAP_MCBSP_CLK_RISING)
		mcbsp_cfg.pcr0 &= ~CLKXP;
	else
		mcbsp_cfg.pcr0 |= CLKXP;

	/* Set SCLKME to 0 and CLKSM to 1 */
	mcbsp_cfg.pcr0 &= ~SCLKME;
	mcbsp_cfg.srgr2 |= CLKSM;

	/* Set FSXP */
	if (spi_cfg->fsx_polarity == OMAP_MCBSP_FS_ACTIVE_HIGH)
		mcbsp_cfg.pcr0 &= ~FSXP;
	else
		mcbsp_cfg.pcr0 |= FSXP;

	if (spi_cfg->spi_mode == OMAP_MCBSP_SPI_MASTER) {
		mcbsp_cfg.pcr0 |= CLKXM;
		mcbsp_cfg.srgr1 |= CLKGDV(spi_cfg->clk_div - 1);
		mcbsp_cfg.pcr0 |= FSXM;
		mcbsp_cfg.srgr2 &= ~FSGM;
		mcbsp_cfg.xcr2 |= XDATDLY(1);
		mcbsp_cfg.rcr2 |= RDATDLY(1);
	} else {
		mcbsp_cfg.pcr0 &= ~CLKXM;
		mcbsp_cfg.srgr1 |= CLKGDV(1);
		mcbsp_cfg.pcr0 &= ~FSXM;
		mcbsp_cfg.xcr2 &= ~XDATDLY(3);
		mcbsp_cfg.rcr2 &= ~RDATDLY(3);
	}

	mcbsp_cfg.xcr2 &= ~XPHASE;
	mcbsp_cfg.rcr2 &= ~RPHASE;

	omap_mcbsp_config(id, &mcbsp_cfg);
}
EXPORT_SYMBOL(omap_mcbsp_set_spi_mode);

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

int __devinit omap_st_add(struct omap_mcbsp *mcbsp)
{
	struct omap_mcbsp_platform_data *pdata = mcbsp->pdata;
	struct omap_mcbsp_st_data *st_data;
	int err;

	st_data = kzalloc(sizeof(*mcbsp->st_data), GFP_KERNEL);
	if (!st_data) {
		err = -ENOMEM;
		goto err1;
	}

	st_data->io_base_st = ioremap(pdata->phys_base_st, SZ_4K);
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
		mcbsp->max_tx_thres = max_thres(mcbsp);
		mcbsp->max_rx_thres = max_thres(mcbsp);
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
	mcbsp->free = 1;
	mcbsp->dma_tx_lch = -1;
	mcbsp->dma_rx_lch = -1;

	mcbsp->phys_base = pdata->phys_base;
	mcbsp->io_base = ioremap(pdata->phys_base, SZ_4K);
	if (!mcbsp->io_base) {
		ret = -ENOMEM;
		goto err_ioremap;
	}

	/* Default I/O is IRQ based */
	mcbsp->io_type = OMAP_MCBSP_IRQ_IO;
	mcbsp->tx_irq = pdata->tx_irq;
	mcbsp->rx_irq = pdata->rx_irq;
	mcbsp->dma_rx_sync = pdata->dma_rx_sync;
	mcbsp->dma_tx_sync = pdata->dma_tx_sync;

	mcbsp->iclk = clk_get(&pdev->dev, "ick");
	if (IS_ERR(mcbsp->iclk)) {
		ret = PTR_ERR(mcbsp->iclk);
		dev_err(&pdev->dev, "unable to get ick: %d\n", ret);
		goto err_iclk;
	}

	mcbsp->fclk = clk_get(&pdev->dev, "fck");
	if (IS_ERR(mcbsp->fclk)) {
		ret = PTR_ERR(mcbsp->fclk);
		dev_err(&pdev->dev, "unable to get fck: %d\n", ret);
		goto err_fclk;
	}

	mcbsp->pdata = pdata;
	mcbsp->dev = &pdev->dev;
	mcbsp_ptr[id] = mcbsp;
	platform_set_drvdata(pdev, mcbsp);

	/* Initialize mcbsp properties for OMAP34XX if needed / applicable */
	omap34xx_device_init(mcbsp);

	return 0;

err_fclk:
	clk_put(mcbsp->iclk);
err_iclk:
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

		clk_disable(mcbsp->fclk);
		clk_disable(mcbsp->iclk);
		clk_put(mcbsp->fclk);
		clk_put(mcbsp->iclk);

		iounmap(mcbsp->io_base);

		mcbsp->fclk = NULL;
		mcbsp->iclk = NULL;
		mcbsp->free = 0;
		mcbsp->dev = NULL;
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
