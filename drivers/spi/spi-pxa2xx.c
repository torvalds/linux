/*
 * Copyright (C) 2005 Stephen Street / StreetFire Sound Labs
 * Copyright (C) 2013, Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/bitops.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/ioport.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/spi/pxa2xx_spi.h>
#include <linux/spi/spi.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/pm_runtime.h>
#include <linux/acpi.h>

#include "spi-pxa2xx.h"

MODULE_AUTHOR("Stephen Street");
MODULE_DESCRIPTION("PXA2xx SSP SPI Controller");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:pxa2xx-spi");

#define TIMOUT_DFLT		1000

/*
 * for testing SSCR1 changes that require SSP restart, basically
 * everything except the service and interrupt enables, the pxa270 developer
 * manual says only SSCR1_SCFR, SSCR1_SPH, SSCR1_SPO need to be in this
 * list, but the PXA255 dev man says all bits without really meaning the
 * service and interrupt enables
 */
#define SSCR1_CHANGE_MASK (SSCR1_TTELP | SSCR1_TTE | SSCR1_SCFR \
				| SSCR1_ECRA | SSCR1_ECRB | SSCR1_SCLKDIR \
				| SSCR1_SFRMDIR | SSCR1_RWOT | SSCR1_TRAIL \
				| SSCR1_IFS | SSCR1_STRF | SSCR1_EFWR \
				| SSCR1_RFT | SSCR1_TFT | SSCR1_MWDS \
				| SSCR1_SPH | SSCR1_SPO | SSCR1_LBM)

#define QUARK_X1000_SSCR1_CHANGE_MASK (QUARK_X1000_SSCR1_STRF	\
				| QUARK_X1000_SSCR1_EFWR	\
				| QUARK_X1000_SSCR1_RFT		\
				| QUARK_X1000_SSCR1_TFT		\
				| SSCR1_SPH | SSCR1_SPO | SSCR1_LBM)

#define CE4100_SSCR1_CHANGE_MASK (SSCR1_TTELP | SSCR1_TTE | SSCR1_SCFR \
				| SSCR1_ECRA | SSCR1_ECRB | SSCR1_SCLKDIR \
				| SSCR1_SFRMDIR | SSCR1_RWOT | SSCR1_TRAIL \
				| SSCR1_IFS | SSCR1_STRF | SSCR1_EFWR \
				| CE4100_SSCR1_RFT | CE4100_SSCR1_TFT | SSCR1_MWDS \
				| SSCR1_SPH | SSCR1_SPO | SSCR1_LBM)

#define LPSS_GENERAL_REG_RXTO_HOLDOFF_DISABLE	BIT(24)
#define LPSS_CS_CONTROL_SW_MODE			BIT(0)
#define LPSS_CS_CONTROL_CS_HIGH			BIT(1)
#define LPSS_CAPS_CS_EN_SHIFT			9
#define LPSS_CAPS_CS_EN_MASK			(0xf << LPSS_CAPS_CS_EN_SHIFT)

struct lpss_config {
	/* LPSS offset from drv_data->ioaddr */
	unsigned offset;
	/* Register offsets from drv_data->lpss_base or -1 */
	int reg_general;
	int reg_ssp;
	int reg_cs_ctrl;
	int reg_capabilities;
	/* FIFO thresholds */
	u32 rx_threshold;
	u32 tx_threshold_lo;
	u32 tx_threshold_hi;
	/* Chip select control */
	unsigned cs_sel_shift;
	unsigned cs_sel_mask;
	unsigned cs_num;
};

/* Keep these sorted with enum pxa_ssp_type */
static const struct lpss_config lpss_platforms[] = {
	{	/* LPSS_LPT_SSP */
		.offset = 0x800,
		.reg_general = 0x08,
		.reg_ssp = 0x0c,
		.reg_cs_ctrl = 0x18,
		.reg_capabilities = -1,
		.rx_threshold = 64,
		.tx_threshold_lo = 160,
		.tx_threshold_hi = 224,
	},
	{	/* LPSS_BYT_SSP */
		.offset = 0x400,
		.reg_general = 0x08,
		.reg_ssp = 0x0c,
		.reg_cs_ctrl = 0x18,
		.reg_capabilities = -1,
		.rx_threshold = 64,
		.tx_threshold_lo = 160,
		.tx_threshold_hi = 224,
	},
	{	/* LPSS_BSW_SSP */
		.offset = 0x400,
		.reg_general = 0x08,
		.reg_ssp = 0x0c,
		.reg_cs_ctrl = 0x18,
		.reg_capabilities = -1,
		.rx_threshold = 64,
		.tx_threshold_lo = 160,
		.tx_threshold_hi = 224,
		.cs_sel_shift = 2,
		.cs_sel_mask = 1 << 2,
		.cs_num = 2,
	},
	{	/* LPSS_SPT_SSP */
		.offset = 0x200,
		.reg_general = -1,
		.reg_ssp = 0x20,
		.reg_cs_ctrl = 0x24,
		.reg_capabilities = -1,
		.rx_threshold = 1,
		.tx_threshold_lo = 32,
		.tx_threshold_hi = 56,
	},
	{	/* LPSS_BXT_SSP */
		.offset = 0x200,
		.reg_general = -1,
		.reg_ssp = 0x20,
		.reg_cs_ctrl = 0x24,
		.reg_capabilities = 0xfc,
		.rx_threshold = 1,
		.tx_threshold_lo = 16,
		.tx_threshold_hi = 48,
		.cs_sel_shift = 8,
		.cs_sel_mask = 3 << 8,
	},
	{	/* LPSS_CNL_SSP */
		.offset = 0x200,
		.reg_general = -1,
		.reg_ssp = 0x20,
		.reg_cs_ctrl = 0x24,
		.reg_capabilities = 0xfc,
		.rx_threshold = 1,
		.tx_threshold_lo = 32,
		.tx_threshold_hi = 56,
		.cs_sel_shift = 8,
		.cs_sel_mask = 3 << 8,
	},
};

static inline const struct lpss_config
*lpss_get_config(const struct driver_data *drv_data)
{
	return &lpss_platforms[drv_data->ssp_type - LPSS_LPT_SSP];
}

static bool is_lpss_ssp(const struct driver_data *drv_data)
{
	switch (drv_data->ssp_type) {
	case LPSS_LPT_SSP:
	case LPSS_BYT_SSP:
	case LPSS_BSW_SSP:
	case LPSS_SPT_SSP:
	case LPSS_BXT_SSP:
	case LPSS_CNL_SSP:
		return true;
	default:
		return false;
	}
}

static bool is_quark_x1000_ssp(const struct driver_data *drv_data)
{
	return drv_data->ssp_type == QUARK_X1000_SSP;
}

static u32 pxa2xx_spi_get_ssrc1_change_mask(const struct driver_data *drv_data)
{
	switch (drv_data->ssp_type) {
	case QUARK_X1000_SSP:
		return QUARK_X1000_SSCR1_CHANGE_MASK;
	case CE4100_SSP:
		return CE4100_SSCR1_CHANGE_MASK;
	default:
		return SSCR1_CHANGE_MASK;
	}
}

static u32
pxa2xx_spi_get_rx_default_thre(const struct driver_data *drv_data)
{
	switch (drv_data->ssp_type) {
	case QUARK_X1000_SSP:
		return RX_THRESH_QUARK_X1000_DFLT;
	case CE4100_SSP:
		return RX_THRESH_CE4100_DFLT;
	default:
		return RX_THRESH_DFLT;
	}
}

static bool pxa2xx_spi_txfifo_full(const struct driver_data *drv_data)
{
	u32 mask;

	switch (drv_data->ssp_type) {
	case QUARK_X1000_SSP:
		mask = QUARK_X1000_SSSR_TFL_MASK;
		break;
	case CE4100_SSP:
		mask = CE4100_SSSR_TFL_MASK;
		break;
	default:
		mask = SSSR_TFL_MASK;
		break;
	}

	return (pxa2xx_spi_read(drv_data, SSSR) & mask) == mask;
}

static void pxa2xx_spi_clear_rx_thre(const struct driver_data *drv_data,
				     u32 *sccr1_reg)
{
	u32 mask;

	switch (drv_data->ssp_type) {
	case QUARK_X1000_SSP:
		mask = QUARK_X1000_SSCR1_RFT;
		break;
	case CE4100_SSP:
		mask = CE4100_SSCR1_RFT;
		break;
	default:
		mask = SSCR1_RFT;
		break;
	}
	*sccr1_reg &= ~mask;
}

static void pxa2xx_spi_set_rx_thre(const struct driver_data *drv_data,
				   u32 *sccr1_reg, u32 threshold)
{
	switch (drv_data->ssp_type) {
	case QUARK_X1000_SSP:
		*sccr1_reg |= QUARK_X1000_SSCR1_RxTresh(threshold);
		break;
	case CE4100_SSP:
		*sccr1_reg |= CE4100_SSCR1_RxTresh(threshold);
		break;
	default:
		*sccr1_reg |= SSCR1_RxTresh(threshold);
		break;
	}
}

static u32 pxa2xx_configure_sscr0(const struct driver_data *drv_data,
				  u32 clk_div, u8 bits)
{
	switch (drv_data->ssp_type) {
	case QUARK_X1000_SSP:
		return clk_div
			| QUARK_X1000_SSCR0_Motorola
			| QUARK_X1000_SSCR0_DataSize(bits > 32 ? 8 : bits)
			| SSCR0_SSE;
	default:
		return clk_div
			| SSCR0_Motorola
			| SSCR0_DataSize(bits > 16 ? bits - 16 : bits)
			| SSCR0_SSE
			| (bits > 16 ? SSCR0_EDSS : 0);
	}
}

/*
 * Read and write LPSS SSP private registers. Caller must first check that
 * is_lpss_ssp() returns true before these can be called.
 */
static u32 __lpss_ssp_read_priv(struct driver_data *drv_data, unsigned offset)
{
	WARN_ON(!drv_data->lpss_base);
	return readl(drv_data->lpss_base + offset);
}

static void __lpss_ssp_write_priv(struct driver_data *drv_data,
				  unsigned offset, u32 value)
{
	WARN_ON(!drv_data->lpss_base);
	writel(value, drv_data->lpss_base + offset);
}

/*
 * lpss_ssp_setup - perform LPSS SSP specific setup
 * @drv_data: pointer to the driver private data
 *
 * Perform LPSS SSP specific setup. This function must be called first if
 * one is going to use LPSS SSP private registers.
 */
static void lpss_ssp_setup(struct driver_data *drv_data)
{
	const struct lpss_config *config;
	u32 value;

	config = lpss_get_config(drv_data);
	drv_data->lpss_base = drv_data->ioaddr + config->offset;

	/* Enable software chip select control */
	value = __lpss_ssp_read_priv(drv_data, config->reg_cs_ctrl);
	value &= ~(LPSS_CS_CONTROL_SW_MODE | LPSS_CS_CONTROL_CS_HIGH);
	value |= LPSS_CS_CONTROL_SW_MODE | LPSS_CS_CONTROL_CS_HIGH;
	__lpss_ssp_write_priv(drv_data, config->reg_cs_ctrl, value);

	/* Enable multiblock DMA transfers */
	if (drv_data->master_info->enable_dma) {
		__lpss_ssp_write_priv(drv_data, config->reg_ssp, 1);

		if (config->reg_general >= 0) {
			value = __lpss_ssp_read_priv(drv_data,
						     config->reg_general);
			value |= LPSS_GENERAL_REG_RXTO_HOLDOFF_DISABLE;
			__lpss_ssp_write_priv(drv_data,
					      config->reg_general, value);
		}
	}
}

static void lpss_ssp_select_cs(struct driver_data *drv_data,
			       const struct lpss_config *config)
{
	u32 value, cs;

	if (!config->cs_sel_mask)
		return;

	value = __lpss_ssp_read_priv(drv_data, config->reg_cs_ctrl);

	cs = drv_data->master->cur_msg->spi->chip_select;
	cs <<= config->cs_sel_shift;
	if (cs != (value & config->cs_sel_mask)) {
		/*
		 * When switching another chip select output active the
		 * output must be selected first and wait 2 ssp_clk cycles
		 * before changing state to active. Otherwise a short
		 * glitch will occur on the previous chip select since
		 * output select is latched but state control is not.
		 */
		value &= ~config->cs_sel_mask;
		value |= cs;
		__lpss_ssp_write_priv(drv_data,
				      config->reg_cs_ctrl, value);
		ndelay(1000000000 /
		       (drv_data->master->max_speed_hz / 2));
	}
}

static void lpss_ssp_cs_control(struct driver_data *drv_data, bool enable)
{
	const struct lpss_config *config;
	u32 value;

	config = lpss_get_config(drv_data);

	if (enable)
		lpss_ssp_select_cs(drv_data, config);

	value = __lpss_ssp_read_priv(drv_data, config->reg_cs_ctrl);
	if (enable)
		value &= ~LPSS_CS_CONTROL_CS_HIGH;
	else
		value |= LPSS_CS_CONTROL_CS_HIGH;
	__lpss_ssp_write_priv(drv_data, config->reg_cs_ctrl, value);
}

static void cs_assert(struct driver_data *drv_data)
{
	struct chip_data *chip =
		spi_get_ctldata(drv_data->master->cur_msg->spi);

	if (drv_data->ssp_type == CE4100_SSP) {
		pxa2xx_spi_write(drv_data, SSSR, chip->frm);
		return;
	}

	if (chip->cs_control) {
		chip->cs_control(PXA2XX_CS_ASSERT);
		return;
	}

	if (chip->gpiod_cs) {
		gpiod_set_value(chip->gpiod_cs, chip->gpio_cs_inverted);
		return;
	}

	if (is_lpss_ssp(drv_data))
		lpss_ssp_cs_control(drv_data, true);
}

static void cs_deassert(struct driver_data *drv_data)
{
	struct chip_data *chip =
		spi_get_ctldata(drv_data->master->cur_msg->spi);

	if (drv_data->ssp_type == CE4100_SSP)
		return;

	if (chip->cs_control) {
		chip->cs_control(PXA2XX_CS_DEASSERT);
		return;
	}

	if (chip->gpiod_cs) {
		gpiod_set_value(chip->gpiod_cs, !chip->gpio_cs_inverted);
		return;
	}

	if (is_lpss_ssp(drv_data))
		lpss_ssp_cs_control(drv_data, false);
}

int pxa2xx_spi_flush(struct driver_data *drv_data)
{
	unsigned long limit = loops_per_jiffy << 1;

	do {
		while (pxa2xx_spi_read(drv_data, SSSR) & SSSR_RNE)
			pxa2xx_spi_read(drv_data, SSDR);
	} while ((pxa2xx_spi_read(drv_data, SSSR) & SSSR_BSY) && --limit);
	write_SSSR_CS(drv_data, SSSR_ROR);

	return limit;
}

static int null_writer(struct driver_data *drv_data)
{
	u8 n_bytes = drv_data->n_bytes;

	if (pxa2xx_spi_txfifo_full(drv_data)
		|| (drv_data->tx == drv_data->tx_end))
		return 0;

	pxa2xx_spi_write(drv_data, SSDR, 0);
	drv_data->tx += n_bytes;

	return 1;
}

static int null_reader(struct driver_data *drv_data)
{
	u8 n_bytes = drv_data->n_bytes;

	while ((pxa2xx_spi_read(drv_data, SSSR) & SSSR_RNE)
	       && (drv_data->rx < drv_data->rx_end)) {
		pxa2xx_spi_read(drv_data, SSDR);
		drv_data->rx += n_bytes;
	}

	return drv_data->rx == drv_data->rx_end;
}

static int u8_writer(struct driver_data *drv_data)
{
	if (pxa2xx_spi_txfifo_full(drv_data)
		|| (drv_data->tx == drv_data->tx_end))
		return 0;

	pxa2xx_spi_write(drv_data, SSDR, *(u8 *)(drv_data->tx));
	++drv_data->tx;

	return 1;
}

static int u8_reader(struct driver_data *drv_data)
{
	while ((pxa2xx_spi_read(drv_data, SSSR) & SSSR_RNE)
	       && (drv_data->rx < drv_data->rx_end)) {
		*(u8 *)(drv_data->rx) = pxa2xx_spi_read(drv_data, SSDR);
		++drv_data->rx;
	}

	return drv_data->rx == drv_data->rx_end;
}

static int u16_writer(struct driver_data *drv_data)
{
	if (pxa2xx_spi_txfifo_full(drv_data)
		|| (drv_data->tx == drv_data->tx_end))
		return 0;

	pxa2xx_spi_write(drv_data, SSDR, *(u16 *)(drv_data->tx));
	drv_data->tx += 2;

	return 1;
}

static int u16_reader(struct driver_data *drv_data)
{
	while ((pxa2xx_spi_read(drv_data, SSSR) & SSSR_RNE)
	       && (drv_data->rx < drv_data->rx_end)) {
		*(u16 *)(drv_data->rx) = pxa2xx_spi_read(drv_data, SSDR);
		drv_data->rx += 2;
	}

	return drv_data->rx == drv_data->rx_end;
}

static int u32_writer(struct driver_data *drv_data)
{
	if (pxa2xx_spi_txfifo_full(drv_data)
		|| (drv_data->tx == drv_data->tx_end))
		return 0;

	pxa2xx_spi_write(drv_data, SSDR, *(u32 *)(drv_data->tx));
	drv_data->tx += 4;

	return 1;
}

static int u32_reader(struct driver_data *drv_data)
{
	while ((pxa2xx_spi_read(drv_data, SSSR) & SSSR_RNE)
	       && (drv_data->rx < drv_data->rx_end)) {
		*(u32 *)(drv_data->rx) = pxa2xx_spi_read(drv_data, SSDR);
		drv_data->rx += 4;
	}

	return drv_data->rx == drv_data->rx_end;
}

void *pxa2xx_spi_next_transfer(struct driver_data *drv_data)
{
	struct spi_message *msg = drv_data->master->cur_msg;
	struct spi_transfer *trans = drv_data->cur_transfer;

	/* Move to next transfer */
	if (trans->transfer_list.next != &msg->transfers) {
		drv_data->cur_transfer =
			list_entry(trans->transfer_list.next,
					struct spi_transfer,
					transfer_list);
		return RUNNING_STATE;
	} else
		return DONE_STATE;
}

/* caller already set message->status; dma and pio irqs are blocked */
static void giveback(struct driver_data *drv_data)
{
	struct spi_transfer* last_transfer;
	struct spi_message *msg;
	unsigned long timeout;

	msg = drv_data->master->cur_msg;
	drv_data->cur_transfer = NULL;

	last_transfer = list_last_entry(&msg->transfers, struct spi_transfer,
					transfer_list);

	/* Delay if requested before any change in chip select */
	if (last_transfer->delay_usecs)
		udelay(last_transfer->delay_usecs);

	/* Wait until SSP becomes idle before deasserting the CS */
	timeout = jiffies + msecs_to_jiffies(10);
	while (pxa2xx_spi_read(drv_data, SSSR) & SSSR_BSY &&
	       !time_after(jiffies, timeout))
		cpu_relax();

	/* Drop chip select UNLESS cs_change is true or we are returning
	 * a message with an error, or next message is for another chip
	 */
	if (!last_transfer->cs_change)
		cs_deassert(drv_data);
	else {
		struct spi_message *next_msg;

		/* Holding of cs was hinted, but we need to make sure
		 * the next message is for the same chip.  Don't waste
		 * time with the following tests unless this was hinted.
		 *
		 * We cannot postpone this until pump_messages, because
		 * after calling msg->complete (below) the driver that
		 * sent the current message could be unloaded, which
		 * could invalidate the cs_control() callback...
		 */

		/* get a pointer to the next message, if any */
		next_msg = spi_get_next_queued_message(drv_data->master);

		/* see if the next and current messages point
		 * to the same chip
		 */
		if ((next_msg && next_msg->spi != msg->spi) ||
		    msg->state == ERROR_STATE)
			cs_deassert(drv_data);
	}

	spi_finalize_current_message(drv_data->master);
}

static void reset_sccr1(struct driver_data *drv_data)
{
	struct chip_data *chip =
		spi_get_ctldata(drv_data->master->cur_msg->spi);
	u32 sccr1_reg;

	sccr1_reg = pxa2xx_spi_read(drv_data, SSCR1) & ~drv_data->int_cr1;
	switch (drv_data->ssp_type) {
	case QUARK_X1000_SSP:
		sccr1_reg &= ~QUARK_X1000_SSCR1_RFT;
		break;
	case CE4100_SSP:
		sccr1_reg &= ~CE4100_SSCR1_RFT;
		break;
	default:
		sccr1_reg &= ~SSCR1_RFT;
		break;
	}
	sccr1_reg |= chip->threshold;
	pxa2xx_spi_write(drv_data, SSCR1, sccr1_reg);
}

static void int_error_stop(struct driver_data *drv_data, const char* msg)
{
	/* Stop and reset SSP */
	write_SSSR_CS(drv_data, drv_data->clear_sr);
	reset_sccr1(drv_data);
	if (!pxa25x_ssp_comp(drv_data))
		pxa2xx_spi_write(drv_data, SSTO, 0);
	pxa2xx_spi_flush(drv_data);
	pxa2xx_spi_write(drv_data, SSCR0,
			 pxa2xx_spi_read(drv_data, SSCR0) & ~SSCR0_SSE);

	dev_err(&drv_data->pdev->dev, "%s\n", msg);

	drv_data->master->cur_msg->state = ERROR_STATE;
	tasklet_schedule(&drv_data->pump_transfers);
}

static void int_transfer_complete(struct driver_data *drv_data)
{
	/* Clear and disable interrupts */
	write_SSSR_CS(drv_data, drv_data->clear_sr);
	reset_sccr1(drv_data);
	if (!pxa25x_ssp_comp(drv_data))
		pxa2xx_spi_write(drv_data, SSTO, 0);

	/* Update total byte transferred return count actual bytes read */
	drv_data->master->cur_msg->actual_length += drv_data->len -
				(drv_data->rx_end - drv_data->rx);

	/* Transfer delays and chip select release are
	 * handled in pump_transfers or giveback
	 */

	/* Move to next transfer */
	drv_data->master->cur_msg->state = pxa2xx_spi_next_transfer(drv_data);

	/* Schedule transfer tasklet */
	tasklet_schedule(&drv_data->pump_transfers);
}

static irqreturn_t interrupt_transfer(struct driver_data *drv_data)
{
	u32 irq_mask = (pxa2xx_spi_read(drv_data, SSCR1) & SSCR1_TIE) ?
		       drv_data->mask_sr : drv_data->mask_sr & ~SSSR_TFS;

	u32 irq_status = pxa2xx_spi_read(drv_data, SSSR) & irq_mask;

	if (irq_status & SSSR_ROR) {
		int_error_stop(drv_data, "interrupt_transfer: fifo overrun");
		return IRQ_HANDLED;
	}

	if (irq_status & SSSR_TINT) {
		pxa2xx_spi_write(drv_data, SSSR, SSSR_TINT);
		if (drv_data->read(drv_data)) {
			int_transfer_complete(drv_data);
			return IRQ_HANDLED;
		}
	}

	/* Drain rx fifo, Fill tx fifo and prevent overruns */
	do {
		if (drv_data->read(drv_data)) {
			int_transfer_complete(drv_data);
			return IRQ_HANDLED;
		}
	} while (drv_data->write(drv_data));

	if (drv_data->read(drv_data)) {
		int_transfer_complete(drv_data);
		return IRQ_HANDLED;
	}

	if (drv_data->tx == drv_data->tx_end) {
		u32 bytes_left;
		u32 sccr1_reg;

		sccr1_reg = pxa2xx_spi_read(drv_data, SSCR1);
		sccr1_reg &= ~SSCR1_TIE;

		/*
		 * PXA25x_SSP has no timeout, set up rx threshould for the
		 * remaining RX bytes.
		 */
		if (pxa25x_ssp_comp(drv_data)) {
			u32 rx_thre;

			pxa2xx_spi_clear_rx_thre(drv_data, &sccr1_reg);

			bytes_left = drv_data->rx_end - drv_data->rx;
			switch (drv_data->n_bytes) {
			case 4:
				bytes_left >>= 1;
			case 2:
				bytes_left >>= 1;
			}

			rx_thre = pxa2xx_spi_get_rx_default_thre(drv_data);
			if (rx_thre > bytes_left)
				rx_thre = bytes_left;

			pxa2xx_spi_set_rx_thre(drv_data, &sccr1_reg, rx_thre);
		}
		pxa2xx_spi_write(drv_data, SSCR1, sccr1_reg);
	}

	/* We did something */
	return IRQ_HANDLED;
}

static void handle_bad_msg(struct driver_data *drv_data)
{
	pxa2xx_spi_write(drv_data, SSCR0,
			 pxa2xx_spi_read(drv_data, SSCR0) & ~SSCR0_SSE);
	pxa2xx_spi_write(drv_data, SSCR1,
			 pxa2xx_spi_read(drv_data, SSCR1) & ~drv_data->int_cr1);
	if (!pxa25x_ssp_comp(drv_data))
		pxa2xx_spi_write(drv_data, SSTO, 0);
	write_SSSR_CS(drv_data, drv_data->clear_sr);

	dev_err(&drv_data->pdev->dev,
		"bad message state in interrupt handler\n");
}

static irqreturn_t ssp_int(int irq, void *dev_id)
{
	struct driver_data *drv_data = dev_id;
	u32 sccr1_reg;
	u32 mask = drv_data->mask_sr;
	u32 status;

	/*
	 * The IRQ might be shared with other peripherals so we must first
	 * check that are we RPM suspended or not. If we are we assume that
	 * the IRQ was not for us (we shouldn't be RPM suspended when the
	 * interrupt is enabled).
	 */
	if (pm_runtime_suspended(&drv_data->pdev->dev))
		return IRQ_NONE;

	/*
	 * If the device is not yet in RPM suspended state and we get an
	 * interrupt that is meant for another device, check if status bits
	 * are all set to one. That means that the device is already
	 * powered off.
	 */
	status = pxa2xx_spi_read(drv_data, SSSR);
	if (status == ~0)
		return IRQ_NONE;

	sccr1_reg = pxa2xx_spi_read(drv_data, SSCR1);

	/* Ignore possible writes if we don't need to write */
	if (!(sccr1_reg & SSCR1_TIE))
		mask &= ~SSSR_TFS;

	/* Ignore RX timeout interrupt if it is disabled */
	if (!(sccr1_reg & SSCR1_TINTE))
		mask &= ~SSSR_TINT;

	if (!(status & mask))
		return IRQ_NONE;

	pxa2xx_spi_write(drv_data, SSCR1, sccr1_reg & ~drv_data->int_cr1);
	pxa2xx_spi_write(drv_data, SSCR1, sccr1_reg);

	if (!drv_data->master->cur_msg) {
		handle_bad_msg(drv_data);
		/* Never fail */
		return IRQ_HANDLED;
	}

	return drv_data->transfer_handler(drv_data);
}

/*
 * The Quark SPI has an additional 24 bit register (DDS_CLK_RATE) to multiply
 * input frequency by fractions of 2^24. It also has a divider by 5.
 *
 * There are formulas to get baud rate value for given input frequency and
 * divider parameters, such as DDS_CLK_RATE and SCR:
 *
 * Fsys = 200MHz
 *
 * Fssp = Fsys * DDS_CLK_RATE / 2^24			(1)
 * Baud rate = Fsclk = Fssp / (2 * (SCR + 1))		(2)
 *
 * DDS_CLK_RATE either 2^n or 2^n / 5.
 * SCR is in range 0 .. 255
 *
 * Divisor = 5^i * 2^j * 2 * k
 *       i = [0, 1]      i = 1 iff j = 0 or j > 3
 *       j = [0, 23]     j = 0 iff i = 1
 *       k = [1, 256]
 * Special case: j = 0, i = 1: Divisor = 2 / 5
 *
 * Accordingly to the specification the recommended values for DDS_CLK_RATE
 * are:
 *	Case 1:		2^n, n = [0, 23]
 *	Case 2:		2^24 * 2 / 5 (0x666666)
 *	Case 3:		less than or equal to 2^24 / 5 / 16 (0x33333)
 *
 * In all cases the lowest possible value is better.
 *
 * The function calculates parameters for all cases and chooses the one closest
 * to the asked baud rate.
 */
static unsigned int quark_x1000_get_clk_div(int rate, u32 *dds)
{
	unsigned long xtal = 200000000;
	unsigned long fref = xtal / 2;		/* mandatory division by 2,
						   see (2) */
						/* case 3 */
	unsigned long fref1 = fref / 2;		/* case 1 */
	unsigned long fref2 = fref * 2 / 5;	/* case 2 */
	unsigned long scale;
	unsigned long q, q1, q2;
	long r, r1, r2;
	u32 mul;

	/* Case 1 */

	/* Set initial value for DDS_CLK_RATE */
	mul = (1 << 24) >> 1;

	/* Calculate initial quot */
	q1 = DIV_ROUND_UP(fref1, rate);

	/* Scale q1 if it's too big */
	if (q1 > 256) {
		/* Scale q1 to range [1, 512] */
		scale = fls_long(q1 - 1);
		if (scale > 9) {
			q1 >>= scale - 9;
			mul >>= scale - 9;
		}

		/* Round the result if we have a remainder */
		q1 += q1 & 1;
	}

	/* Decrease DDS_CLK_RATE as much as we can without loss in precision */
	scale = __ffs(q1);
	q1 >>= scale;
	mul >>= scale;

	/* Get the remainder */
	r1 = abs(fref1 / (1 << (24 - fls_long(mul))) / q1 - rate);

	/* Case 2 */

	q2 = DIV_ROUND_UP(fref2, rate);
	r2 = abs(fref2 / q2 - rate);

	/*
	 * Choose the best between two: less remainder we have the better. We
	 * can't go case 2 if q2 is greater than 256 since SCR register can
	 * hold only values 0 .. 255.
	 */
	if (r2 >= r1 || q2 > 256) {
		/* case 1 is better */
		r = r1;
		q = q1;
	} else {
		/* case 2 is better */
		r = r2;
		q = q2;
		mul = (1 << 24) * 2 / 5;
	}

	/* Check case 3 only if the divisor is big enough */
	if (fref / rate >= 80) {
		u64 fssp;
		u32 m;

		/* Calculate initial quot */
		q1 = DIV_ROUND_UP(fref, rate);
		m = (1 << 24) / q1;

		/* Get the remainder */
		fssp = (u64)fref * m;
		do_div(fssp, 1 << 24);
		r1 = abs(fssp - rate);

		/* Choose this one if it suits better */
		if (r1 < r) {
			/* case 3 is better */
			q = 1;
			mul = m;
		}
	}

	*dds = mul;
	return q - 1;
}

static unsigned int ssp_get_clk_div(struct driver_data *drv_data, int rate)
{
	unsigned long ssp_clk = drv_data->master->max_speed_hz;
	const struct ssp_device *ssp = drv_data->ssp;

	rate = min_t(int, ssp_clk, rate);

	if (ssp->type == PXA25x_SSP || ssp->type == CE4100_SSP)
		return (ssp_clk / (2 * rate) - 1) & 0xff;
	else
		return (ssp_clk / rate - 1) & 0xfff;
}

static unsigned int pxa2xx_ssp_get_clk_div(struct driver_data *drv_data,
					   int rate)
{
	struct chip_data *chip =
		spi_get_ctldata(drv_data->master->cur_msg->spi);
	unsigned int clk_div;

	switch (drv_data->ssp_type) {
	case QUARK_X1000_SSP:
		clk_div = quark_x1000_get_clk_div(rate, &chip->dds_rate);
		break;
	default:
		clk_div = ssp_get_clk_div(drv_data, rate);
		break;
	}
	return clk_div << 8;
}

static bool pxa2xx_spi_can_dma(struct spi_master *master,
			       struct spi_device *spi,
			       struct spi_transfer *xfer)
{
	struct chip_data *chip = spi_get_ctldata(spi);

	return chip->enable_dma &&
	       xfer->len <= MAX_DMA_LEN &&
	       xfer->len >= chip->dma_burst_size;
}

static void pump_transfers(unsigned long data)
{
	struct driver_data *drv_data = (struct driver_data *)data;
	struct spi_master *master = drv_data->master;
	struct spi_message *message = master->cur_msg;
	struct chip_data *chip = spi_get_ctldata(message->spi);
	u32 dma_thresh = chip->dma_threshold;
	u32 dma_burst = chip->dma_burst_size;
	u32 change_mask = pxa2xx_spi_get_ssrc1_change_mask(drv_data);
	struct spi_transfer *transfer;
	struct spi_transfer *previous;
	u32 clk_div;
	u8 bits;
	u32 speed;
	u32 cr0;
	u32 cr1;
	int err;
	int dma_mapped;

	/* Get current state information */
	transfer = drv_data->cur_transfer;

	/* Handle for abort */
	if (message->state == ERROR_STATE) {
		message->status = -EIO;
		giveback(drv_data);
		return;
	}

	/* Handle end of message */
	if (message->state == DONE_STATE) {
		message->status = 0;
		giveback(drv_data);
		return;
	}

	/* Delay if requested at end of transfer before CS change */
	if (message->state == RUNNING_STATE) {
		previous = list_entry(transfer->transfer_list.prev,
					struct spi_transfer,
					transfer_list);
		if (previous->delay_usecs)
			udelay(previous->delay_usecs);

		/* Drop chip select only if cs_change is requested */
		if (previous->cs_change)
			cs_deassert(drv_data);
	}

	/* Check if we can DMA this transfer */
	if (transfer->len > MAX_DMA_LEN && chip->enable_dma) {

		/* reject already-mapped transfers; PIO won't always work */
		if (message->is_dma_mapped
				|| transfer->rx_dma || transfer->tx_dma) {
			dev_err(&drv_data->pdev->dev,
				"pump_transfers: mapped transfer length of "
				"%u is greater than %d\n",
				transfer->len, MAX_DMA_LEN);
			message->status = -EINVAL;
			giveback(drv_data);
			return;
		}

		/* warn ... we force this to PIO mode */
		dev_warn_ratelimited(&message->spi->dev,
				     "pump_transfers: DMA disabled for transfer length %ld "
				     "greater than %d\n",
				     (long)drv_data->len, MAX_DMA_LEN);
	}

	/* Setup the transfer state based on the type of transfer */
	if (pxa2xx_spi_flush(drv_data) == 0) {
		dev_err(&drv_data->pdev->dev, "pump_transfers: flush failed\n");
		message->status = -EIO;
		giveback(drv_data);
		return;
	}
	drv_data->n_bytes = chip->n_bytes;
	drv_data->tx = (void *)transfer->tx_buf;
	drv_data->tx_end = drv_data->tx + transfer->len;
	drv_data->rx = transfer->rx_buf;
	drv_data->rx_end = drv_data->rx + transfer->len;
	drv_data->len = transfer->len;
	drv_data->write = drv_data->tx ? chip->write : null_writer;
	drv_data->read = drv_data->rx ? chip->read : null_reader;

	/* Change speed and bit per word on a per transfer */
	bits = transfer->bits_per_word;
	speed = transfer->speed_hz;

	clk_div = pxa2xx_ssp_get_clk_div(drv_data, speed);

	if (bits <= 8) {
		drv_data->n_bytes = 1;
		drv_data->read = drv_data->read != null_reader ?
					u8_reader : null_reader;
		drv_data->write = drv_data->write != null_writer ?
					u8_writer : null_writer;
	} else if (bits <= 16) {
		drv_data->n_bytes = 2;
		drv_data->read = drv_data->read != null_reader ?
					u16_reader : null_reader;
		drv_data->write = drv_data->write != null_writer ?
					u16_writer : null_writer;
	} else if (bits <= 32) {
		drv_data->n_bytes = 4;
		drv_data->read = drv_data->read != null_reader ?
					u32_reader : null_reader;
		drv_data->write = drv_data->write != null_writer ?
					u32_writer : null_writer;
	}
	/*
	 * if bits/word is changed in dma mode, then must check the
	 * thresholds and burst also
	 */
	if (chip->enable_dma) {
		if (pxa2xx_spi_set_dma_burst_and_threshold(chip,
						message->spi,
						bits, &dma_burst,
						&dma_thresh))
			dev_warn_ratelimited(&message->spi->dev,
					     "pump_transfers: DMA burst size reduced to match bits_per_word\n");
	}

	message->state = RUNNING_STATE;

	dma_mapped = master->can_dma &&
		     master->can_dma(master, message->spi, transfer) &&
		     master->cur_msg_mapped;
	if (dma_mapped) {

		/* Ensure we have the correct interrupt handler */
		drv_data->transfer_handler = pxa2xx_spi_dma_transfer;

		err = pxa2xx_spi_dma_prepare(drv_data, dma_burst);
		if (err) {
			message->status = err;
			giveback(drv_data);
			return;
		}

		/* Clear status and start DMA engine */
		cr1 = chip->cr1 | dma_thresh | drv_data->dma_cr1;
		pxa2xx_spi_write(drv_data, SSSR, drv_data->clear_sr);

		pxa2xx_spi_dma_start(drv_data);
	} else {
		/* Ensure we have the correct interrupt handler	*/
		drv_data->transfer_handler = interrupt_transfer;

		/* Clear status  */
		cr1 = chip->cr1 | chip->threshold | drv_data->int_cr1;
		write_SSSR_CS(drv_data, drv_data->clear_sr);
	}

	/* NOTE:  PXA25x_SSP _could_ use external clocking ... */
	cr0 = pxa2xx_configure_sscr0(drv_data, clk_div, bits);
	if (!pxa25x_ssp_comp(drv_data))
		dev_dbg(&message->spi->dev, "%u Hz actual, %s\n",
			master->max_speed_hz
				/ (1 + ((cr0 & SSCR0_SCR(0xfff)) >> 8)),
			dma_mapped ? "DMA" : "PIO");
	else
		dev_dbg(&message->spi->dev, "%u Hz actual, %s\n",
			master->max_speed_hz / 2
				/ (1 + ((cr0 & SSCR0_SCR(0x0ff)) >> 8)),
			dma_mapped ? "DMA" : "PIO");

	if (is_lpss_ssp(drv_data)) {
		if ((pxa2xx_spi_read(drv_data, SSIRF) & 0xff)
		    != chip->lpss_rx_threshold)
			pxa2xx_spi_write(drv_data, SSIRF,
					 chip->lpss_rx_threshold);
		if ((pxa2xx_spi_read(drv_data, SSITF) & 0xffff)
		    != chip->lpss_tx_threshold)
			pxa2xx_spi_write(drv_data, SSITF,
					 chip->lpss_tx_threshold);
	}

	if (is_quark_x1000_ssp(drv_data) &&
	    (pxa2xx_spi_read(drv_data, DDS_RATE) != chip->dds_rate))
		pxa2xx_spi_write(drv_data, DDS_RATE, chip->dds_rate);

	/* see if we need to reload the config registers */
	if ((pxa2xx_spi_read(drv_data, SSCR0) != cr0)
	    || (pxa2xx_spi_read(drv_data, SSCR1) & change_mask)
	    != (cr1 & change_mask)) {
		/* stop the SSP, and update the other bits */
		pxa2xx_spi_write(drv_data, SSCR0, cr0 & ~SSCR0_SSE);
		if (!pxa25x_ssp_comp(drv_data))
			pxa2xx_spi_write(drv_data, SSTO, chip->timeout);
		/* first set CR1 without interrupt and service enables */
		pxa2xx_spi_write(drv_data, SSCR1, cr1 & change_mask);
		/* restart the SSP */
		pxa2xx_spi_write(drv_data, SSCR0, cr0);

	} else {
		if (!pxa25x_ssp_comp(drv_data))
			pxa2xx_spi_write(drv_data, SSTO, chip->timeout);
	}

	cs_assert(drv_data);

	/* after chip select, release the data by enabling service
	 * requests and interrupts, without changing any mode bits */
	pxa2xx_spi_write(drv_data, SSCR1, cr1);
}

static int pxa2xx_spi_transfer_one_message(struct spi_master *master,
					   struct spi_message *msg)
{
	struct driver_data *drv_data = spi_master_get_devdata(master);

	/* Initial message state*/
	msg->state = START_STATE;
	drv_data->cur_transfer = list_entry(msg->transfers.next,
						struct spi_transfer,
						transfer_list);

	/* Mark as busy and launch transfers */
	tasklet_schedule(&drv_data->pump_transfers);
	return 0;
}

static int pxa2xx_spi_unprepare_transfer(struct spi_master *master)
{
	struct driver_data *drv_data = spi_master_get_devdata(master);

	/* Disable the SSP now */
	pxa2xx_spi_write(drv_data, SSCR0,
			 pxa2xx_spi_read(drv_data, SSCR0) & ~SSCR0_SSE);

	return 0;
}

static int setup_cs(struct spi_device *spi, struct chip_data *chip,
		    struct pxa2xx_spi_chip *chip_info)
{
	struct driver_data *drv_data = spi_master_get_devdata(spi->master);
	struct gpio_desc *gpiod;
	int err = 0;

	if (chip == NULL)
		return 0;

	if (drv_data->cs_gpiods) {
		gpiod = drv_data->cs_gpiods[spi->chip_select];
		if (gpiod) {
			chip->gpiod_cs = gpiod;
			chip->gpio_cs_inverted = spi->mode & SPI_CS_HIGH;
			gpiod_set_value(gpiod, chip->gpio_cs_inverted);
		}

		return 0;
	}

	if (chip_info == NULL)
		return 0;

	/* NOTE: setup() can be called multiple times, possibly with
	 * different chip_info, release previously requested GPIO
	 */
	if (chip->gpiod_cs) {
		gpiod_put(chip->gpiod_cs);
		chip->gpiod_cs = NULL;
	}

	/* If (*cs_control) is provided, ignore GPIO chip select */
	if (chip_info->cs_control) {
		chip->cs_control = chip_info->cs_control;
		return 0;
	}

	if (gpio_is_valid(chip_info->gpio_cs)) {
		err = gpio_request(chip_info->gpio_cs, "SPI_CS");
		if (err) {
			dev_err(&spi->dev, "failed to request chip select GPIO%d\n",
				chip_info->gpio_cs);
			return err;
		}

		gpiod = gpio_to_desc(chip_info->gpio_cs);
		chip->gpiod_cs = gpiod;
		chip->gpio_cs_inverted = spi->mode & SPI_CS_HIGH;

		err = gpiod_direction_output(gpiod, !chip->gpio_cs_inverted);
	}

	return err;
}

static int setup(struct spi_device *spi)
{
	struct pxa2xx_spi_chip *chip_info;
	struct chip_data *chip;
	const struct lpss_config *config;
	struct driver_data *drv_data = spi_master_get_devdata(spi->master);
	uint tx_thres, tx_hi_thres, rx_thres;

	switch (drv_data->ssp_type) {
	case QUARK_X1000_SSP:
		tx_thres = TX_THRESH_QUARK_X1000_DFLT;
		tx_hi_thres = 0;
		rx_thres = RX_THRESH_QUARK_X1000_DFLT;
		break;
	case CE4100_SSP:
		tx_thres = TX_THRESH_CE4100_DFLT;
		tx_hi_thres = 0;
		rx_thres = RX_THRESH_CE4100_DFLT;
		break;
	case LPSS_LPT_SSP:
	case LPSS_BYT_SSP:
	case LPSS_BSW_SSP:
	case LPSS_SPT_SSP:
	case LPSS_BXT_SSP:
	case LPSS_CNL_SSP:
		config = lpss_get_config(drv_data);
		tx_thres = config->tx_threshold_lo;
		tx_hi_thres = config->tx_threshold_hi;
		rx_thres = config->rx_threshold;
		break;
	default:
		tx_thres = TX_THRESH_DFLT;
		tx_hi_thres = 0;
		rx_thres = RX_THRESH_DFLT;
		break;
	}

	/* Only alloc on first setup */
	chip = spi_get_ctldata(spi);
	if (!chip) {
		chip = kzalloc(sizeof(struct chip_data), GFP_KERNEL);
		if (!chip)
			return -ENOMEM;

		if (drv_data->ssp_type == CE4100_SSP) {
			if (spi->chip_select > 4) {
				dev_err(&spi->dev,
					"failed setup: cs number must not be > 4.\n");
				kfree(chip);
				return -EINVAL;
			}

			chip->frm = spi->chip_select;
		}
		chip->enable_dma = drv_data->master_info->enable_dma;
		chip->timeout = TIMOUT_DFLT;
	}

	/* protocol drivers may change the chip settings, so...
	 * if chip_info exists, use it */
	chip_info = spi->controller_data;

	/* chip_info isn't always needed */
	chip->cr1 = 0;
	if (chip_info) {
		if (chip_info->timeout)
			chip->timeout = chip_info->timeout;
		if (chip_info->tx_threshold)
			tx_thres = chip_info->tx_threshold;
		if (chip_info->tx_hi_threshold)
			tx_hi_thres = chip_info->tx_hi_threshold;
		if (chip_info->rx_threshold)
			rx_thres = chip_info->rx_threshold;
		chip->dma_threshold = 0;
		if (chip_info->enable_loopback)
			chip->cr1 = SSCR1_LBM;
	}

	chip->lpss_rx_threshold = SSIRF_RxThresh(rx_thres);
	chip->lpss_tx_threshold = SSITF_TxLoThresh(tx_thres)
				| SSITF_TxHiThresh(tx_hi_thres);

	/* set dma burst and threshold outside of chip_info path so that if
	 * chip_info goes away after setting chip->enable_dma, the
	 * burst and threshold can still respond to changes in bits_per_word */
	if (chip->enable_dma) {
		/* set up legal burst and threshold for dma */
		if (pxa2xx_spi_set_dma_burst_and_threshold(chip, spi,
						spi->bits_per_word,
						&chip->dma_burst_size,
						&chip->dma_threshold)) {
			dev_warn(&spi->dev,
				 "in setup: DMA burst size reduced to match bits_per_word\n");
		}
	}

	switch (drv_data->ssp_type) {
	case QUARK_X1000_SSP:
		chip->threshold = (QUARK_X1000_SSCR1_RxTresh(rx_thres)
				   & QUARK_X1000_SSCR1_RFT)
				   | (QUARK_X1000_SSCR1_TxTresh(tx_thres)
				   & QUARK_X1000_SSCR1_TFT);
		break;
	case CE4100_SSP:
		chip->threshold = (CE4100_SSCR1_RxTresh(rx_thres) & CE4100_SSCR1_RFT) |
			(CE4100_SSCR1_TxTresh(tx_thres) & CE4100_SSCR1_TFT);
		break;
	default:
		chip->threshold = (SSCR1_RxTresh(rx_thres) & SSCR1_RFT) |
			(SSCR1_TxTresh(tx_thres) & SSCR1_TFT);
		break;
	}

	chip->cr1 &= ~(SSCR1_SPO | SSCR1_SPH);
	chip->cr1 |= (((spi->mode & SPI_CPHA) != 0) ? SSCR1_SPH : 0)
			| (((spi->mode & SPI_CPOL) != 0) ? SSCR1_SPO : 0);

	if (spi->mode & SPI_LOOP)
		chip->cr1 |= SSCR1_LBM;

	if (spi->bits_per_word <= 8) {
		chip->n_bytes = 1;
		chip->read = u8_reader;
		chip->write = u8_writer;
	} else if (spi->bits_per_word <= 16) {
		chip->n_bytes = 2;
		chip->read = u16_reader;
		chip->write = u16_writer;
	} else if (spi->bits_per_word <= 32) {
		chip->n_bytes = 4;
		chip->read = u32_reader;
		chip->write = u32_writer;
	}

	spi_set_ctldata(spi, chip);

	if (drv_data->ssp_type == CE4100_SSP)
		return 0;

	return setup_cs(spi, chip, chip_info);
}

static void cleanup(struct spi_device *spi)
{
	struct chip_data *chip = spi_get_ctldata(spi);
	struct driver_data *drv_data = spi_master_get_devdata(spi->master);

	if (!chip)
		return;

	if (drv_data->ssp_type != CE4100_SSP && !drv_data->cs_gpiods &&
	    chip->gpiod_cs)
		gpiod_put(chip->gpiod_cs);

	kfree(chip);
}

#ifdef CONFIG_PCI
#ifdef CONFIG_ACPI

static const struct acpi_device_id pxa2xx_spi_acpi_match[] = {
	{ "INT33C0", LPSS_LPT_SSP },
	{ "INT33C1", LPSS_LPT_SSP },
	{ "INT3430", LPSS_LPT_SSP },
	{ "INT3431", LPSS_LPT_SSP },
	{ "80860F0E", LPSS_BYT_SSP },
	{ "8086228E", LPSS_BSW_SSP },
	{ },
};
MODULE_DEVICE_TABLE(acpi, pxa2xx_spi_acpi_match);

static int pxa2xx_spi_get_port_id(struct acpi_device *adev)
{
	unsigned int devid;
	int port_id = -1;

	if (adev && adev->pnp.unique_id &&
	    !kstrtouint(adev->pnp.unique_id, 0, &devid))
		port_id = devid;
	return port_id;
}
#else /* !CONFIG_ACPI */
static int pxa2xx_spi_get_port_id(struct acpi_device *adev)
{
	return -1;
}
#endif

/*
 * PCI IDs of compound devices that integrate both host controller and private
 * integrated DMA engine. Please note these are not used in module
 * autoloading and probing in this module but matching the LPSS SSP type.
 */
static const struct pci_device_id pxa2xx_spi_pci_compound_match[] = {
	/* SPT-LP */
	{ PCI_VDEVICE(INTEL, 0x9d29), LPSS_SPT_SSP },
	{ PCI_VDEVICE(INTEL, 0x9d2a), LPSS_SPT_SSP },
	/* SPT-H */
	{ PCI_VDEVICE(INTEL, 0xa129), LPSS_SPT_SSP },
	{ PCI_VDEVICE(INTEL, 0xa12a), LPSS_SPT_SSP },
	/* KBL-H */
	{ PCI_VDEVICE(INTEL, 0xa2a9), LPSS_SPT_SSP },
	{ PCI_VDEVICE(INTEL, 0xa2aa), LPSS_SPT_SSP },
	/* BXT A-Step */
	{ PCI_VDEVICE(INTEL, 0x0ac2), LPSS_BXT_SSP },
	{ PCI_VDEVICE(INTEL, 0x0ac4), LPSS_BXT_SSP },
	{ PCI_VDEVICE(INTEL, 0x0ac6), LPSS_BXT_SSP },
	/* BXT B-Step */
	{ PCI_VDEVICE(INTEL, 0x1ac2), LPSS_BXT_SSP },
	{ PCI_VDEVICE(INTEL, 0x1ac4), LPSS_BXT_SSP },
	{ PCI_VDEVICE(INTEL, 0x1ac6), LPSS_BXT_SSP },
	/* GLK */
	{ PCI_VDEVICE(INTEL, 0x31c2), LPSS_BXT_SSP },
	{ PCI_VDEVICE(INTEL, 0x31c4), LPSS_BXT_SSP },
	{ PCI_VDEVICE(INTEL, 0x31c6), LPSS_BXT_SSP },
	/* APL */
	{ PCI_VDEVICE(INTEL, 0x5ac2), LPSS_BXT_SSP },
	{ PCI_VDEVICE(INTEL, 0x5ac4), LPSS_BXT_SSP },
	{ PCI_VDEVICE(INTEL, 0x5ac6), LPSS_BXT_SSP },
	/* CNL-LP */
	{ PCI_VDEVICE(INTEL, 0x9daa), LPSS_CNL_SSP },
	{ PCI_VDEVICE(INTEL, 0x9dab), LPSS_CNL_SSP },
	{ PCI_VDEVICE(INTEL, 0x9dfb), LPSS_CNL_SSP },
	/* CNL-H */
	{ PCI_VDEVICE(INTEL, 0xa32a), LPSS_CNL_SSP },
	{ PCI_VDEVICE(INTEL, 0xa32b), LPSS_CNL_SSP },
	{ PCI_VDEVICE(INTEL, 0xa37b), LPSS_CNL_SSP },
	{ },
};

static bool pxa2xx_spi_idma_filter(struct dma_chan *chan, void *param)
{
	struct device *dev = param;

	if (dev != chan->device->dev->parent)
		return false;

	return true;
}

static struct pxa2xx_spi_master *
pxa2xx_spi_init_pdata(struct platform_device *pdev)
{
	struct pxa2xx_spi_master *pdata;
	struct acpi_device *adev;
	struct ssp_device *ssp;
	struct resource *res;
	const struct acpi_device_id *adev_id = NULL;
	const struct pci_device_id *pcidev_id = NULL;
	int type;

	adev = ACPI_COMPANION(&pdev->dev);

	if (dev_is_pci(pdev->dev.parent))
		pcidev_id = pci_match_id(pxa2xx_spi_pci_compound_match,
					 to_pci_dev(pdev->dev.parent));
	else if (adev)
		adev_id = acpi_match_device(pdev->dev.driver->acpi_match_table,
					    &pdev->dev);
	else
		return NULL;

	if (adev_id)
		type = (int)adev_id->driver_data;
	else if (pcidev_id)
		type = (int)pcidev_id->driver_data;
	else
		return NULL;

	pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return NULL;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return NULL;

	ssp = &pdata->ssp;

	ssp->phys_base = res->start;
	ssp->mmio_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(ssp->mmio_base))
		return NULL;

	if (pcidev_id) {
		pdata->tx_param = pdev->dev.parent;
		pdata->rx_param = pdev->dev.parent;
		pdata->dma_filter = pxa2xx_spi_idma_filter;
	}

	ssp->clk = devm_clk_get(&pdev->dev, NULL);
	ssp->irq = platform_get_irq(pdev, 0);
	ssp->type = type;
	ssp->pdev = pdev;
	ssp->port_id = pxa2xx_spi_get_port_id(adev);

	pdata->num_chipselect = 1;
	pdata->enable_dma = true;

	return pdata;
}

#else /* !CONFIG_PCI */
static inline struct pxa2xx_spi_master *
pxa2xx_spi_init_pdata(struct platform_device *pdev)
{
	return NULL;
}
#endif

static int pxa2xx_spi_fw_translate_cs(struct spi_master *master, unsigned cs)
{
	struct driver_data *drv_data = spi_master_get_devdata(master);

	if (has_acpi_companion(&drv_data->pdev->dev)) {
		switch (drv_data->ssp_type) {
		/*
		 * For Atoms the ACPI DeviceSelection used by the Windows
		 * driver starts from 1 instead of 0 so translate it here
		 * to match what Linux expects.
		 */
		case LPSS_BYT_SSP:
		case LPSS_BSW_SSP:
			return cs - 1;

		default:
			break;
		}
	}

	return cs;
}

static int pxa2xx_spi_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct pxa2xx_spi_master *platform_info;
	struct spi_master *master;
	struct driver_data *drv_data;
	struct ssp_device *ssp;
	const struct lpss_config *config;
	int status, count;
	u32 tmp;

	platform_info = dev_get_platdata(dev);
	if (!platform_info) {
		platform_info = pxa2xx_spi_init_pdata(pdev);
		if (!platform_info) {
			dev_err(&pdev->dev, "missing platform data\n");
			return -ENODEV;
		}
	}

	ssp = pxa_ssp_request(pdev->id, pdev->name);
	if (!ssp)
		ssp = &platform_info->ssp;

	if (!ssp->mmio_base) {
		dev_err(&pdev->dev, "failed to get ssp\n");
		return -ENODEV;
	}

	master = spi_alloc_master(dev, sizeof(struct driver_data));
	if (!master) {
		dev_err(&pdev->dev, "cannot alloc spi_master\n");
		pxa_ssp_free(ssp);
		return -ENOMEM;
	}
	drv_data = spi_master_get_devdata(master);
	drv_data->master = master;
	drv_data->master_info = platform_info;
	drv_data->pdev = pdev;
	drv_data->ssp = ssp;

	master->dev.of_node = pdev->dev.of_node;
	/* the spi->mode bits understood by this driver: */
	master->mode_bits = SPI_CPOL | SPI_CPHA | SPI_CS_HIGH | SPI_LOOP;

	master->bus_num = ssp->port_id;
	master->dma_alignment = DMA_ALIGNMENT;
	master->cleanup = cleanup;
	master->setup = setup;
	master->transfer_one_message = pxa2xx_spi_transfer_one_message;
	master->unprepare_transfer_hardware = pxa2xx_spi_unprepare_transfer;
	master->fw_translate_cs = pxa2xx_spi_fw_translate_cs;
	master->auto_runtime_pm = true;
	master->flags = SPI_MASTER_MUST_RX | SPI_MASTER_MUST_TX;

	drv_data->ssp_type = ssp->type;

	drv_data->ioaddr = ssp->mmio_base;
	drv_data->ssdr_physical = ssp->phys_base + SSDR;
	if (pxa25x_ssp_comp(drv_data)) {
		switch (drv_data->ssp_type) {
		case QUARK_X1000_SSP:
			master->bits_per_word_mask = SPI_BPW_RANGE_MASK(4, 32);
			break;
		default:
			master->bits_per_word_mask = SPI_BPW_RANGE_MASK(4, 16);
			break;
		}

		drv_data->int_cr1 = SSCR1_TIE | SSCR1_RIE;
		drv_data->dma_cr1 = 0;
		drv_data->clear_sr = SSSR_ROR;
		drv_data->mask_sr = SSSR_RFS | SSSR_TFS | SSSR_ROR;
	} else {
		master->bits_per_word_mask = SPI_BPW_RANGE_MASK(4, 32);
		drv_data->int_cr1 = SSCR1_TIE | SSCR1_RIE | SSCR1_TINTE;
		drv_data->dma_cr1 = DEFAULT_DMA_CR1;
		drv_data->clear_sr = SSSR_ROR | SSSR_TINT;
		drv_data->mask_sr = SSSR_TINT | SSSR_RFS | SSSR_TFS | SSSR_ROR;
	}

	status = request_irq(ssp->irq, ssp_int, IRQF_SHARED, dev_name(dev),
			drv_data);
	if (status < 0) {
		dev_err(&pdev->dev, "cannot get IRQ %d\n", ssp->irq);
		goto out_error_master_alloc;
	}

	/* Setup DMA if requested */
	if (platform_info->enable_dma) {
		status = pxa2xx_spi_dma_setup(drv_data);
		if (status) {
			dev_dbg(dev, "no DMA channels available, using PIO\n");
			platform_info->enable_dma = false;
		} else {
			master->can_dma = pxa2xx_spi_can_dma;
		}
	}

	/* Enable SOC clock */
	clk_prepare_enable(ssp->clk);

	master->max_speed_hz = clk_get_rate(ssp->clk);

	/* Load default SSP configuration */
	pxa2xx_spi_write(drv_data, SSCR0, 0);
	switch (drv_data->ssp_type) {
	case QUARK_X1000_SSP:
		tmp = QUARK_X1000_SSCR1_RxTresh(RX_THRESH_QUARK_X1000_DFLT) |
		      QUARK_X1000_SSCR1_TxTresh(TX_THRESH_QUARK_X1000_DFLT);
		pxa2xx_spi_write(drv_data, SSCR1, tmp);

		/* using the Motorola SPI protocol and use 8 bit frame */
		tmp = QUARK_X1000_SSCR0_Motorola | QUARK_X1000_SSCR0_DataSize(8);
		pxa2xx_spi_write(drv_data, SSCR0, tmp);
		break;
	case CE4100_SSP:
		tmp = CE4100_SSCR1_RxTresh(RX_THRESH_CE4100_DFLT) |
		      CE4100_SSCR1_TxTresh(TX_THRESH_CE4100_DFLT);
		pxa2xx_spi_write(drv_data, SSCR1, tmp);
		tmp = SSCR0_SCR(2) | SSCR0_Motorola | SSCR0_DataSize(8);
		pxa2xx_spi_write(drv_data, SSCR0, tmp);
		break;
	default:
		tmp = SSCR1_RxTresh(RX_THRESH_DFLT) |
		      SSCR1_TxTresh(TX_THRESH_DFLT);
		pxa2xx_spi_write(drv_data, SSCR1, tmp);
		tmp = SSCR0_SCR(2) | SSCR0_Motorola | SSCR0_DataSize(8);
		pxa2xx_spi_write(drv_data, SSCR0, tmp);
		break;
	}

	if (!pxa25x_ssp_comp(drv_data))
		pxa2xx_spi_write(drv_data, SSTO, 0);

	if (!is_quark_x1000_ssp(drv_data))
		pxa2xx_spi_write(drv_data, SSPSP, 0);

	if (is_lpss_ssp(drv_data)) {
		lpss_ssp_setup(drv_data);
		config = lpss_get_config(drv_data);
		if (config->reg_capabilities >= 0) {
			tmp = __lpss_ssp_read_priv(drv_data,
						   config->reg_capabilities);
			tmp &= LPSS_CAPS_CS_EN_MASK;
			tmp >>= LPSS_CAPS_CS_EN_SHIFT;
			platform_info->num_chipselect = ffz(tmp);
		} else if (config->cs_num) {
			platform_info->num_chipselect = config->cs_num;
		}
	}
	master->num_chipselect = platform_info->num_chipselect;

	count = gpiod_count(&pdev->dev, "cs");
	if (count > 0) {
		int i;

		master->num_chipselect = max_t(int, count,
			master->num_chipselect);

		drv_data->cs_gpiods = devm_kcalloc(&pdev->dev,
			master->num_chipselect, sizeof(struct gpio_desc *),
			GFP_KERNEL);
		if (!drv_data->cs_gpiods) {
			status = -ENOMEM;
			goto out_error_clock_enabled;
		}

		for (i = 0; i < master->num_chipselect; i++) {
			struct gpio_desc *gpiod;

			gpiod = devm_gpiod_get_index(dev, "cs", i, GPIOD_ASIS);
			if (IS_ERR(gpiod)) {
				/* Means use native chip select */
				if (PTR_ERR(gpiod) == -ENOENT)
					continue;

				status = (int)PTR_ERR(gpiod);
				goto out_error_clock_enabled;
			} else {
				drv_data->cs_gpiods[i] = gpiod;
			}
		}
	}

	tasklet_init(&drv_data->pump_transfers, pump_transfers,
		     (unsigned long)drv_data);

	pm_runtime_set_autosuspend_delay(&pdev->dev, 50);
	pm_runtime_use_autosuspend(&pdev->dev);
	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	/* Register with the SPI framework */
	platform_set_drvdata(pdev, drv_data);
	status = devm_spi_register_master(&pdev->dev, master);
	if (status != 0) {
		dev_err(&pdev->dev, "problem registering spi master\n");
		goto out_error_clock_enabled;
	}

	return status;

out_error_clock_enabled:
	clk_disable_unprepare(ssp->clk);
	pxa2xx_spi_dma_release(drv_data);
	free_irq(ssp->irq, drv_data);

out_error_master_alloc:
	spi_master_put(master);
	pxa_ssp_free(ssp);
	return status;
}

static int pxa2xx_spi_remove(struct platform_device *pdev)
{
	struct driver_data *drv_data = platform_get_drvdata(pdev);
	struct ssp_device *ssp;

	if (!drv_data)
		return 0;
	ssp = drv_data->ssp;

	pm_runtime_get_sync(&pdev->dev);

	/* Disable the SSP at the peripheral and SOC level */
	pxa2xx_spi_write(drv_data, SSCR0, 0);
	clk_disable_unprepare(ssp->clk);

	/* Release DMA */
	if (drv_data->master_info->enable_dma)
		pxa2xx_spi_dma_release(drv_data);

	pm_runtime_put_noidle(&pdev->dev);
	pm_runtime_disable(&pdev->dev);

	/* Release IRQ */
	free_irq(ssp->irq, drv_data);

	/* Release SSP */
	pxa_ssp_free(ssp);

	return 0;
}

static void pxa2xx_spi_shutdown(struct platform_device *pdev)
{
	int status = 0;

	if ((status = pxa2xx_spi_remove(pdev)) != 0)
		dev_err(&pdev->dev, "shutdown failed with %d\n", status);
}

#ifdef CONFIG_PM_SLEEP
static int pxa2xx_spi_suspend(struct device *dev)
{
	struct driver_data *drv_data = dev_get_drvdata(dev);
	struct ssp_device *ssp = drv_data->ssp;
	int status;

	status = spi_master_suspend(drv_data->master);
	if (status != 0)
		return status;
	pxa2xx_spi_write(drv_data, SSCR0, 0);

	if (!pm_runtime_suspended(dev))
		clk_disable_unprepare(ssp->clk);

	return 0;
}

static int pxa2xx_spi_resume(struct device *dev)
{
	struct driver_data *drv_data = dev_get_drvdata(dev);
	struct ssp_device *ssp = drv_data->ssp;
	int status;

	/* Enable the SSP clock */
	if (!pm_runtime_suspended(dev))
		clk_prepare_enable(ssp->clk);

	/* Restore LPSS private register bits */
	if (is_lpss_ssp(drv_data))
		lpss_ssp_setup(drv_data);

	/* Start the queue running */
	status = spi_master_resume(drv_data->master);
	if (status != 0) {
		dev_err(dev, "problem starting queue (%d)\n", status);
		return status;
	}

	return 0;
}
#endif

#ifdef CONFIG_PM
static int pxa2xx_spi_runtime_suspend(struct device *dev)
{
	struct driver_data *drv_data = dev_get_drvdata(dev);

	clk_disable_unprepare(drv_data->ssp->clk);
	return 0;
}

static int pxa2xx_spi_runtime_resume(struct device *dev)
{
	struct driver_data *drv_data = dev_get_drvdata(dev);

	clk_prepare_enable(drv_data->ssp->clk);
	return 0;
}
#endif

static const struct dev_pm_ops pxa2xx_spi_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(pxa2xx_spi_suspend, pxa2xx_spi_resume)
	SET_RUNTIME_PM_OPS(pxa2xx_spi_runtime_suspend,
			   pxa2xx_spi_runtime_resume, NULL)
};

static struct platform_driver driver = {
	.driver = {
		.name	= "pxa2xx-spi",
		.pm	= &pxa2xx_spi_pm_ops,
		.acpi_match_table = ACPI_PTR(pxa2xx_spi_acpi_match),
	},
	.probe = pxa2xx_spi_probe,
	.remove = pxa2xx_spi_remove,
	.shutdown = pxa2xx_spi_shutdown,
};

static int __init pxa2xx_spi_init(void)
{
	return platform_driver_register(&driver);
}
subsys_initcall(pxa2xx_spi_init);

static void __exit pxa2xx_spi_exit(void)
{
	platform_driver_unregister(&driver);
}
module_exit(pxa2xx_spi_exit);
