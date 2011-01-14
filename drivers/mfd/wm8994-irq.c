/*
 * wm8994-irq.c  --  Interrupt controller support for Wolfson WM8994
 *
 * Copyright 2010 Wolfson Microelectronics PLC.
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/irq.h>
#include <linux/mfd/core.h>
#include <linux/interrupt.h>

#include <linux/mfd/wm8994/core.h>
#include <linux/mfd/wm8994/registers.h>

#include <linux/delay.h>

struct wm8994_irq_data {
	int reg;
	int mask;
};

static struct wm8994_irq_data wm8994_irqs[] = {
	[WM8994_IRQ_TEMP_SHUT] = {
		.reg = 2,
		.mask = WM8994_TEMP_SHUT_EINT,
	},
	[WM8994_IRQ_MIC1_DET] = {
		.reg = 2,
		.mask = WM8994_MIC1_DET_EINT,
	},
	[WM8994_IRQ_MIC1_SHRT] = {
		.reg = 2,
		.mask = WM8994_MIC1_SHRT_EINT,
	},
	[WM8994_IRQ_MIC2_DET] = {
		.reg = 2,
		.mask = WM8994_MIC2_DET_EINT,
	},
	[WM8994_IRQ_MIC2_SHRT] = {
		.reg = 2,
		.mask = WM8994_MIC2_SHRT_EINT,
	},
	[WM8994_IRQ_FLL1_LOCK] = {
		.reg = 2,
		.mask = WM8994_FLL1_LOCK_EINT,
	},
	[WM8994_IRQ_FLL2_LOCK] = {
		.reg = 2,
		.mask = WM8994_FLL2_LOCK_EINT,
	},
	[WM8994_IRQ_SRC1_LOCK] = {
		.reg = 2,
		.mask = WM8994_SRC1_LOCK_EINT,
	},
	[WM8994_IRQ_SRC2_LOCK] = {
		.reg = 2,
		.mask = WM8994_SRC2_LOCK_EINT,
	},
	[WM8994_IRQ_AIF1DRC1_SIG_DET] = {
		.reg = 2,
		.mask = WM8994_AIF1DRC1_SIG_DET,
	},
	[WM8994_IRQ_AIF1DRC2_SIG_DET] = {
		.reg = 2,
		.mask = WM8994_AIF1DRC2_SIG_DET_EINT,
	},
	[WM8994_IRQ_AIF2DRC_SIG_DET] = {
		.reg = 2,
		.mask = WM8994_AIF2DRC_SIG_DET_EINT,
	},
	[WM8994_IRQ_FIFOS_ERR] = {
		.reg = 2,
		.mask = WM8994_FIFOS_ERR_EINT,
	},
	[WM8994_IRQ_WSEQ_DONE] = {
		.reg = 2,
		.mask = WM8994_WSEQ_DONE_EINT,
	},
	[WM8994_IRQ_DCS_DONE] = {
		.reg = 2,
		.mask = WM8994_DCS_DONE_EINT,
	},
	[WM8994_IRQ_TEMP_WARN] = {
		.reg = 2,
		.mask = WM8994_TEMP_WARN_EINT,
	},
	[WM8994_IRQ_GPIO(1)] = {
		.reg = 1,
		.mask = WM8994_GP1_EINT,
	},
	[WM8994_IRQ_GPIO(2)] = {
		.reg = 1,
		.mask = WM8994_GP2_EINT,
	},
	[WM8994_IRQ_GPIO(3)] = {
		.reg = 1,
		.mask = WM8994_GP3_EINT,
	},
	[WM8994_IRQ_GPIO(4)] = {
		.reg = 1,
		.mask = WM8994_GP4_EINT,
	},
	[WM8994_IRQ_GPIO(5)] = {
		.reg = 1,
		.mask = WM8994_GP5_EINT,
	},
	[WM8994_IRQ_GPIO(6)] = {
		.reg = 1,
		.mask = WM8994_GP6_EINT,
	},
	[WM8994_IRQ_GPIO(7)] = {
		.reg = 1,
		.mask = WM8994_GP7_EINT,
	},
	[WM8994_IRQ_GPIO(8)] = {
		.reg = 1,
		.mask = WM8994_GP8_EINT,
	},
	[WM8994_IRQ_GPIO(9)] = {
		.reg = 1,
		.mask = WM8994_GP8_EINT,
	},
	[WM8994_IRQ_GPIO(10)] = {
		.reg = 1,
		.mask = WM8994_GP10_EINT,
	},
	[WM8994_IRQ_GPIO(11)] = {
		.reg = 1,
		.mask = WM8994_GP11_EINT,
	},
};

static inline int irq_data_to_status_reg(struct wm8994_irq_data *irq_data)
{
	return WM8994_INTERRUPT_STATUS_1 - 1 + irq_data->reg;
}

static inline int irq_data_to_mask_reg(struct wm8994_irq_data *irq_data)
{
	return WM8994_INTERRUPT_STATUS_1_MASK - 1 + irq_data->reg;
}

static inline struct wm8994_irq_data *irq_to_wm8994_irq(struct wm8994 *wm8994,
							int irq)
{
	return &wm8994_irqs[irq - wm8994->irq_base];
}

static void wm8994_irq_lock(struct irq_data *data)
{
	struct wm8994 *wm8994 = irq_data_get_irq_chip_data(data);

	mutex_lock(&wm8994->irq_lock);
}

static void wm8994_irq_sync_unlock(struct irq_data *data)
{
	struct wm8994 *wm8994 = irq_data_get_irq_chip_data(data);
	int i;

	for (i = 0; i < ARRAY_SIZE(wm8994->irq_masks_cur); i++) {
		/* If there's been a change in the mask write it back
		 * to the hardware. */
		if (wm8994->irq_masks_cur[i] != wm8994->irq_masks_cache[i]) {
			wm8994->irq_masks_cache[i] = wm8994->irq_masks_cur[i];
			wm8994_reg_write(wm8994,
					 WM8994_INTERRUPT_STATUS_1_MASK + i,
					 wm8994->irq_masks_cur[i]);
		}
	}

	mutex_unlock(&wm8994->irq_lock);
}

static void wm8994_irq_unmask(struct irq_data *data)
{
	struct wm8994 *wm8994 = irq_data_get_irq_chip_data(data);
	struct wm8994_irq_data *irq_data = irq_to_wm8994_irq(wm8994,
							     data->irq);

	wm8994->irq_masks_cur[irq_data->reg - 1] &= ~irq_data->mask;
}

static void wm8994_irq_mask(struct irq_data *data)
{
	struct wm8994 *wm8994 = irq_data_get_irq_chip_data(data);
	struct wm8994_irq_data *irq_data = irq_to_wm8994_irq(wm8994,
							     data->irq);

	wm8994->irq_masks_cur[irq_data->reg - 1] |= irq_data->mask;
}

static struct irq_chip wm8994_irq_chip = {
	.name			= "wm8994",
	.irq_bus_lock		= wm8994_irq_lock,
	.irq_bus_sync_unlock	= wm8994_irq_sync_unlock,
	.irq_mask		= wm8994_irq_mask,
	.irq_unmask		= wm8994_irq_unmask,
};

/* The processing of the primary interrupt occurs in a thread so that
 * we can interact with the device over I2C or SPI. */
static irqreturn_t wm8994_irq_thread(int irq, void *data)
{
	struct wm8994 *wm8994 = data;
	unsigned int i;
	u16 status[WM8994_NUM_IRQ_REGS];
	int ret;

	ret = wm8994_bulk_read(wm8994, WM8994_INTERRUPT_STATUS_1,
			       WM8994_NUM_IRQ_REGS, status);
	if (ret < 0) {
		dev_err(wm8994->dev, "Failed to read interrupt status: %d\n",
			ret);
		return IRQ_NONE;
	}

	/* Apply masking */
	for (i = 0; i < WM8994_NUM_IRQ_REGS; i++)
		status[i] &= ~wm8994->irq_masks_cur[i];

	/* Report */
	for (i = 0; i < ARRAY_SIZE(wm8994_irqs); i++) {
		if (status[wm8994_irqs[i].reg - 1] & wm8994_irqs[i].mask)
			handle_nested_irq(wm8994->irq_base + i);
	}

	/* Ack any unmasked IRQs */
	for (i = 0; i < ARRAY_SIZE(status); i++) {
		if (status[i])
			wm8994_reg_write(wm8994, WM8994_INTERRUPT_STATUS_1 + i,
					 status[i]);
	}

	return IRQ_HANDLED;
}

int wm8994_irq_init(struct wm8994 *wm8994)
{
	int i, cur_irq, ret;

	mutex_init(&wm8994->irq_lock);

	/* Mask the individual interrupt sources */
	for (i = 0; i < ARRAY_SIZE(wm8994->irq_masks_cur); i++) {
		wm8994->irq_masks_cur[i] = 0xffff;
		wm8994->irq_masks_cache[i] = 0xffff;
		wm8994_reg_write(wm8994, WM8994_INTERRUPT_STATUS_1_MASK + i,
				 0xffff);
	}

	if (!wm8994->irq) {
		dev_warn(wm8994->dev,
			 "No interrupt specified, no interrupts\n");
		wm8994->irq_base = 0;
		return 0;
	}

	if (!wm8994->irq_base) {
		dev_err(wm8994->dev,
			"No interrupt base specified, no interrupts\n");
		return 0;
	}

	/* Register them with genirq */
	for (cur_irq = wm8994->irq_base;
	     cur_irq < ARRAY_SIZE(wm8994_irqs) + wm8994->irq_base;
	     cur_irq++) {
		set_irq_chip_data(cur_irq, wm8994);
		set_irq_chip_and_handler(cur_irq, &wm8994_irq_chip,
					 handle_edge_irq);
		set_irq_nested_thread(cur_irq, 1);

		/* ARM needs us to explicitly flag the IRQ as valid
		 * and will set them noprobe when we do so. */
#ifdef CONFIG_ARM
		set_irq_flags(cur_irq, IRQF_VALID);
#else
		set_irq_noprobe(cur_irq);
#endif
	}

	ret = request_threaded_irq(wm8994->irq, NULL, wm8994_irq_thread,
				   IRQF_TRIGGER_HIGH | IRQF_ONESHOT,
				   "wm8994", wm8994);
	if (ret != 0) {
		dev_err(wm8994->dev, "Failed to request IRQ %d: %d\n",
			wm8994->irq, ret);
		return ret;
	}

	/* Enable top level interrupt if it was masked */
	wm8994_reg_write(wm8994, WM8994_INTERRUPT_CONTROL, 0);

	return 0;
}

void wm8994_irq_exit(struct wm8994 *wm8994)
{
	if (wm8994->irq)
		free_irq(wm8994->irq, wm8994);
}
