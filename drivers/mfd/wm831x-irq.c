/*
 * wm831x-irq.c  --  Interrupt controller support for Wolfson WM831x PMICs
 *
 * Copyright 2009 Wolfson Microelectronics PLC.
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
#include <linux/mfd/core.h>
#include <linux/interrupt.h>

#include <linux/mfd/wm831x/core.h>
#include <linux/mfd/wm831x/pdata.h>
#include <linux/mfd/wm831x/irq.h>

#include <linux/delay.h>

/*
 * Since generic IRQs don't currently support interrupt controllers on
 * interrupt driven buses we don't use genirq but instead provide an
 * interface that looks very much like the standard ones.  This leads
 * to some bodges, including storing interrupt handler information in
 * the static irq_data table we use to look up the data for individual
 * interrupts, but hopefully won't last too long.
 */

struct wm831x_irq_data {
	int primary;
	int reg;
	int mask;
	irq_handler_t handler;
	void *handler_data;
};

static struct wm831x_irq_data wm831x_irqs[] = {
	[WM831X_IRQ_TEMP_THW] = {
		.primary = WM831X_TEMP_INT,
		.reg = 1,
		.mask = WM831X_TEMP_THW_EINT,
	},
	[WM831X_IRQ_GPIO_1] = {
		.primary = WM831X_GP_INT,
		.reg = 5,
		.mask = WM831X_GP1_EINT,
	},
	[WM831X_IRQ_GPIO_2] = {
		.primary = WM831X_GP_INT,
		.reg = 5,
		.mask = WM831X_GP2_EINT,
	},
	[WM831X_IRQ_GPIO_3] = {
		.primary = WM831X_GP_INT,
		.reg = 5,
		.mask = WM831X_GP3_EINT,
	},
	[WM831X_IRQ_GPIO_4] = {
		.primary = WM831X_GP_INT,
		.reg = 5,
		.mask = WM831X_GP4_EINT,
	},
	[WM831X_IRQ_GPIO_5] = {
		.primary = WM831X_GP_INT,
		.reg = 5,
		.mask = WM831X_GP5_EINT,
	},
	[WM831X_IRQ_GPIO_6] = {
		.primary = WM831X_GP_INT,
		.reg = 5,
		.mask = WM831X_GP6_EINT,
	},
	[WM831X_IRQ_GPIO_7] = {
		.primary = WM831X_GP_INT,
		.reg = 5,
		.mask = WM831X_GP7_EINT,
	},
	[WM831X_IRQ_GPIO_8] = {
		.primary = WM831X_GP_INT,
		.reg = 5,
		.mask = WM831X_GP8_EINT,
	},
	[WM831X_IRQ_GPIO_9] = {
		.primary = WM831X_GP_INT,
		.reg = 5,
		.mask = WM831X_GP9_EINT,
	},
	[WM831X_IRQ_GPIO_10] = {
		.primary = WM831X_GP_INT,
		.reg = 5,
		.mask = WM831X_GP10_EINT,
	},
	[WM831X_IRQ_GPIO_11] = {
		.primary = WM831X_GP_INT,
		.reg = 5,
		.mask = WM831X_GP11_EINT,
	},
	[WM831X_IRQ_GPIO_12] = {
		.primary = WM831X_GP_INT,
		.reg = 5,
		.mask = WM831X_GP12_EINT,
	},
	[WM831X_IRQ_GPIO_13] = {
		.primary = WM831X_GP_INT,
		.reg = 5,
		.mask = WM831X_GP13_EINT,
	},
	[WM831X_IRQ_GPIO_14] = {
		.primary = WM831X_GP_INT,
		.reg = 5,
		.mask = WM831X_GP14_EINT,
	},
	[WM831X_IRQ_GPIO_15] = {
		.primary = WM831X_GP_INT,
		.reg = 5,
		.mask = WM831X_GP15_EINT,
	},
	[WM831X_IRQ_GPIO_16] = {
		.primary = WM831X_GP_INT,
		.reg = 5,
		.mask = WM831X_GP16_EINT,
	},
	[WM831X_IRQ_ON] = {
		.primary = WM831X_ON_PIN_INT,
		.reg = 1,
		.mask = WM831X_ON_PIN_EINT,
	},
	[WM831X_IRQ_PPM_SYSLO] = {
		.primary = WM831X_PPM_INT,
		.reg = 1,
		.mask = WM831X_PPM_SYSLO_EINT,
	},
	[WM831X_IRQ_PPM_PWR_SRC] = {
		.primary = WM831X_PPM_INT,
		.reg = 1,
		.mask = WM831X_PPM_PWR_SRC_EINT,
	},
	[WM831X_IRQ_PPM_USB_CURR] = {
		.primary = WM831X_PPM_INT,
		.reg = 1,
		.mask = WM831X_PPM_USB_CURR_EINT,
	},
	[WM831X_IRQ_WDOG_TO] = {
		.primary = WM831X_WDOG_INT,
		.reg = 1,
		.mask = WM831X_WDOG_TO_EINT,
	},
	[WM831X_IRQ_RTC_PER] = {
		.primary = WM831X_RTC_INT,
		.reg = 1,
		.mask = WM831X_RTC_PER_EINT,
	},
	[WM831X_IRQ_RTC_ALM] = {
		.primary = WM831X_RTC_INT,
		.reg = 1,
		.mask = WM831X_RTC_ALM_EINT,
	},
	[WM831X_IRQ_CHG_BATT_HOT] = {
		.primary = WM831X_CHG_INT,
		.reg = 2,
		.mask = WM831X_CHG_BATT_HOT_EINT,
	},
	[WM831X_IRQ_CHG_BATT_COLD] = {
		.primary = WM831X_CHG_INT,
		.reg = 2,
		.mask = WM831X_CHG_BATT_COLD_EINT,
	},
	[WM831X_IRQ_CHG_BATT_FAIL] = {
		.primary = WM831X_CHG_INT,
		.reg = 2,
		.mask = WM831X_CHG_BATT_FAIL_EINT,
	},
	[WM831X_IRQ_CHG_OV] = {
		.primary = WM831X_CHG_INT,
		.reg = 2,
		.mask = WM831X_CHG_OV_EINT,
	},
	[WM831X_IRQ_CHG_END] = {
		.primary = WM831X_CHG_INT,
		.reg = 2,
		.mask = WM831X_CHG_END_EINT,
	},
	[WM831X_IRQ_CHG_TO] = {
		.primary = WM831X_CHG_INT,
		.reg = 2,
		.mask = WM831X_CHG_TO_EINT,
	},
	[WM831X_IRQ_CHG_MODE] = {
		.primary = WM831X_CHG_INT,
		.reg = 2,
		.mask = WM831X_CHG_MODE_EINT,
	},
	[WM831X_IRQ_CHG_START] = {
		.primary = WM831X_CHG_INT,
		.reg = 2,
		.mask = WM831X_CHG_START_EINT,
	},
	[WM831X_IRQ_TCHDATA] = {
		.primary = WM831X_TCHDATA_INT,
		.reg = 1,
		.mask = WM831X_TCHDATA_EINT,
	},
	[WM831X_IRQ_TCHPD] = {
		.primary = WM831X_TCHPD_INT,
		.reg = 1,
		.mask = WM831X_TCHPD_EINT,
	},
	[WM831X_IRQ_AUXADC_DATA] = {
		.primary = WM831X_AUXADC_INT,
		.reg = 1,
		.mask = WM831X_AUXADC_DATA_EINT,
	},
	[WM831X_IRQ_AUXADC_DCOMP1] = {
		.primary = WM831X_AUXADC_INT,
		.reg = 1,
		.mask = WM831X_AUXADC_DCOMP1_EINT,
	},
	[WM831X_IRQ_AUXADC_DCOMP2] = {
		.primary = WM831X_AUXADC_INT,
		.reg = 1,
		.mask = WM831X_AUXADC_DCOMP2_EINT,
	},
	[WM831X_IRQ_AUXADC_DCOMP3] = {
		.primary = WM831X_AUXADC_INT,
		.reg = 1,
		.mask = WM831X_AUXADC_DCOMP3_EINT,
	},
	[WM831X_IRQ_AUXADC_DCOMP4] = {
		.primary = WM831X_AUXADC_INT,
		.reg = 1,
		.mask = WM831X_AUXADC_DCOMP4_EINT,
	},
	[WM831X_IRQ_CS1] = {
		.primary = WM831X_CS_INT,
		.reg = 2,
		.mask = WM831X_CS1_EINT,
	},
	[WM831X_IRQ_CS2] = {
		.primary = WM831X_CS_INT,
		.reg = 2,
		.mask = WM831X_CS2_EINT,
	},
	[WM831X_IRQ_HC_DC1] = {
		.primary = WM831X_HC_INT,
		.reg = 4,
		.mask = WM831X_HC_DC1_EINT,
	},
	[WM831X_IRQ_HC_DC2] = {
		.primary = WM831X_HC_INT,
		.reg = 4,
		.mask = WM831X_HC_DC2_EINT,
	},
	[WM831X_IRQ_UV_LDO1] = {
		.primary = WM831X_UV_INT,
		.reg = 3,
		.mask = WM831X_UV_LDO1_EINT,
	},
	[WM831X_IRQ_UV_LDO2] = {
		.primary = WM831X_UV_INT,
		.reg = 3,
		.mask = WM831X_UV_LDO2_EINT,
	},
	[WM831X_IRQ_UV_LDO3] = {
		.primary = WM831X_UV_INT,
		.reg = 3,
		.mask = WM831X_UV_LDO3_EINT,
	},
	[WM831X_IRQ_UV_LDO4] = {
		.primary = WM831X_UV_INT,
		.reg = 3,
		.mask = WM831X_UV_LDO4_EINT,
	},
	[WM831X_IRQ_UV_LDO5] = {
		.primary = WM831X_UV_INT,
		.reg = 3,
		.mask = WM831X_UV_LDO5_EINT,
	},
	[WM831X_IRQ_UV_LDO6] = {
		.primary = WM831X_UV_INT,
		.reg = 3,
		.mask = WM831X_UV_LDO6_EINT,
	},
	[WM831X_IRQ_UV_LDO7] = {
		.primary = WM831X_UV_INT,
		.reg = 3,
		.mask = WM831X_UV_LDO7_EINT,
	},
	[WM831X_IRQ_UV_LDO8] = {
		.primary = WM831X_UV_INT,
		.reg = 3,
		.mask = WM831X_UV_LDO8_EINT,
	},
	[WM831X_IRQ_UV_LDO9] = {
		.primary = WM831X_UV_INT,
		.reg = 3,
		.mask = WM831X_UV_LDO9_EINT,
	},
	[WM831X_IRQ_UV_LDO10] = {
		.primary = WM831X_UV_INT,
		.reg = 3,
		.mask = WM831X_UV_LDO10_EINT,
	},
	[WM831X_IRQ_UV_DC1] = {
		.primary = WM831X_UV_INT,
		.reg = 4,
		.mask = WM831X_UV_DC1_EINT,
	},
	[WM831X_IRQ_UV_DC2] = {
		.primary = WM831X_UV_INT,
		.reg = 4,
		.mask = WM831X_UV_DC2_EINT,
	},
	[WM831X_IRQ_UV_DC3] = {
		.primary = WM831X_UV_INT,
		.reg = 4,
		.mask = WM831X_UV_DC3_EINT,
	},
	[WM831X_IRQ_UV_DC4] = {
		.primary = WM831X_UV_INT,
		.reg = 4,
		.mask = WM831X_UV_DC4_EINT,
	},
};

static inline int irq_data_to_status_reg(struct wm831x_irq_data *irq_data)
{
	return WM831X_INTERRUPT_STATUS_1 - 1 + irq_data->reg;
}

static inline int irq_data_to_mask_reg(struct wm831x_irq_data *irq_data)
{
	return WM831X_INTERRUPT_STATUS_1_MASK - 1 + irq_data->reg;
}

static void __wm831x_enable_irq(struct wm831x *wm831x, int irq)
{
	struct wm831x_irq_data *irq_data = &wm831x_irqs[irq];

	wm831x->irq_masks[irq_data->reg - 1] &= ~irq_data->mask;
	wm831x_reg_write(wm831x, irq_data_to_mask_reg(irq_data),
			 wm831x->irq_masks[irq_data->reg - 1]);
}

void wm831x_enable_irq(struct wm831x *wm831x, int irq)
{
	mutex_lock(&wm831x->irq_lock);
	__wm831x_enable_irq(wm831x, irq);
	mutex_unlock(&wm831x->irq_lock);
}
EXPORT_SYMBOL_GPL(wm831x_enable_irq);

static void __wm831x_disable_irq(struct wm831x *wm831x, int irq)
{
	struct wm831x_irq_data *irq_data = &wm831x_irqs[irq];

	wm831x->irq_masks[irq_data->reg - 1] |= irq_data->mask;
	wm831x_reg_write(wm831x, irq_data_to_mask_reg(irq_data),
			 wm831x->irq_masks[irq_data->reg - 1]);
}

void wm831x_disable_irq(struct wm831x *wm831x, int irq)
{
	mutex_lock(&wm831x->irq_lock);
	__wm831x_disable_irq(wm831x, irq);
	mutex_unlock(&wm831x->irq_lock);
}
EXPORT_SYMBOL_GPL(wm831x_disable_irq);

int wm831x_request_irq(struct wm831x *wm831x,
		       unsigned int irq, irq_handler_t handler,
		       unsigned long flags, const char *name,
		       void *dev)
{
	int ret = 0;

	if (irq < 0 || irq >= WM831X_NUM_IRQS)
		return -EINVAL;

	mutex_lock(&wm831x->irq_lock);

	if (wm831x_irqs[irq].handler) {
		dev_err(wm831x->dev, "Already have handler for IRQ %d\n", irq);
		ret = -EINVAL;
		goto out;
	}

	wm831x_irqs[irq].handler = handler;
	wm831x_irqs[irq].handler_data = dev;

	__wm831x_enable_irq(wm831x, irq);

out:
	mutex_unlock(&wm831x->irq_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(wm831x_request_irq);

void wm831x_free_irq(struct wm831x *wm831x, unsigned int irq, void *data)
{
	if (irq < 0 || irq >= WM831X_NUM_IRQS)
		return;

	mutex_lock(&wm831x->irq_lock);

	wm831x_irqs[irq].handler = NULL;
	wm831x_irqs[irq].handler_data = NULL;

	__wm831x_disable_irq(wm831x, irq);

	mutex_unlock(&wm831x->irq_lock);
}
EXPORT_SYMBOL_GPL(wm831x_free_irq);


static void wm831x_handle_irq(struct wm831x *wm831x, int irq, int status)
{
	struct wm831x_irq_data *irq_data = &wm831x_irqs[irq];

	if (irq_data->handler) {
		irq_data->handler(irq, irq_data->handler_data);
		wm831x_reg_write(wm831x, irq_data_to_status_reg(irq_data),
				 irq_data->mask);
	} else {
		dev_err(wm831x->dev, "Unhandled IRQ %d, masking\n", irq);
		__wm831x_disable_irq(wm831x, irq);
	}
}

/* Main interrupt handling occurs in a workqueue since we need
 * interrupts enabled to interact with the chip. */
static void wm831x_irq_worker(struct work_struct *work)
{
	struct wm831x *wm831x = container_of(work, struct wm831x, irq_work);
	unsigned int i;
	int primary;
	int status_regs[5];
	int read[5] = { 0 };
	int *status;

	primary = wm831x_reg_read(wm831x, WM831X_SYSTEM_INTERRUPTS);
	if (primary < 0) {
		dev_err(wm831x->dev, "Failed to read system interrupt: %d\n",
			primary);
		goto out;
	}

	mutex_lock(&wm831x->irq_lock);

	for (i = 0; i < ARRAY_SIZE(wm831x_irqs); i++) {
		int offset = wm831x_irqs[i].reg - 1;

		if (!(primary & wm831x_irqs[i].primary))
			continue;

		status = &status_regs[offset];

		/* Hopefully there should only be one register to read
		 * each time otherwise we ought to do a block read. */
		if (!read[offset]) {
			*status = wm831x_reg_read(wm831x,
				     irq_data_to_status_reg(&wm831x_irqs[i]));
			if (*status < 0) {
				dev_err(wm831x->dev,
					"Failed to read IRQ status: %d\n",
					*status);
				goto out_lock;
			}

			/* Mask out the disabled IRQs */
			*status &= ~wm831x->irq_masks[offset];
			read[offset] = 1;
		}

		if (*status & wm831x_irqs[i].mask)
			wm831x_handle_irq(wm831x, i, *status);
	}

out_lock:
	mutex_unlock(&wm831x->irq_lock);
out:
	enable_irq(wm831x->irq);
}


static irqreturn_t wm831x_cpu_irq(int irq, void *data)
{
	struct wm831x *wm831x = data;

	/* Shut the interrupt to the CPU up and schedule the actual
	 * handler; we can't check that the IRQ is asserted. */
	disable_irq_nosync(irq);

	queue_work(wm831x->irq_wq, &wm831x->irq_work);

	return IRQ_HANDLED;
}

int wm831x_irq_init(struct wm831x *wm831x, int irq)
{
	int i, ret;

	if (!irq) {
		dev_warn(wm831x->dev,
			 "No interrupt specified - functionality limited\n");
		return 0;
	}


	wm831x->irq_wq = create_singlethread_workqueue("wm831x-irq");
	if (!wm831x->irq_wq) {
		dev_err(wm831x->dev, "Failed to allocate IRQ worker\n");
		return -ESRCH;
	}

	wm831x->irq = irq;
	mutex_init(&wm831x->irq_lock);
	INIT_WORK(&wm831x->irq_work, wm831x_irq_worker);

	/* Mask the individual interrupt sources */
	for (i = 0; i < ARRAY_SIZE(wm831x->irq_masks); i++) {
		wm831x->irq_masks[i] = 0xffff;
		wm831x_reg_write(wm831x, WM831X_INTERRUPT_STATUS_1_MASK + i,
				 0xffff);
	}

	/* Enable top level interrupts, we mask at secondary level */
	wm831x_reg_write(wm831x, WM831X_SYSTEM_INTERRUPTS_MASK, 0);

	/* We're good to go.  We set IRQF_SHARED since there's a
	 * chance the driver will interoperate with another driver but
	 * the need to disable the IRQ while handing via I2C/SPI means
	 * that this may break and performance will be impacted.  If
	 * this does happen it's a hardware design issue and the only
	 * other alternative would be polling.
	 */
	ret = request_irq(irq, wm831x_cpu_irq, IRQF_TRIGGER_LOW | IRQF_SHARED,
			  "wm831x", wm831x);
	if (ret != 0) {
		dev_err(wm831x->dev, "Failed to request IRQ %d: %d\n",
			irq, ret);
		return ret;
	}

	return 0;
}

void wm831x_irq_exit(struct wm831x *wm831x)
{
	if (wm831x->irq)
		free_irq(wm831x->irq, wm831x);
}
