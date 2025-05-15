// SPDX-License-Identifier: GPL-2.0+
/*
 * Helpers for controlling modem lines via GPIO
 *
 * Copyright (C) 2014 Paratronic S.A.
 */

#include <linux/err.h>
#include <linux/device.h>
#include <linux/irq.h>
#include <linux/gpio/consumer.h>
#include <linux/termios.h>
#include <linux/serial_core.h>
#include <linux/module.h>
#include <linux/property.h>

#include "serial_mctrl_gpio.h"

struct mctrl_gpios {
	struct uart_port *port;
	struct gpio_desc *gpio[UART_GPIO_MAX];
	int irq[UART_GPIO_MAX];
	unsigned int mctrl_prev;
	bool mctrl_on;
};

static const struct {
	const char *name;
	unsigned int mctrl;
	enum gpiod_flags flags;
} mctrl_gpios_desc[UART_GPIO_MAX] = {
	{ "cts", TIOCM_CTS, GPIOD_IN, },
	{ "dsr", TIOCM_DSR, GPIOD_IN, },
	{ "dcd", TIOCM_CD,  GPIOD_IN, },
	{ "rng", TIOCM_RNG, GPIOD_IN, },
	{ "rts", TIOCM_RTS, GPIOD_OUT_LOW, },
	{ "dtr", TIOCM_DTR, GPIOD_OUT_LOW, },
};

static bool mctrl_gpio_flags_is_dir_out(unsigned int idx)
{
	return mctrl_gpios_desc[idx].flags & GPIOD_FLAGS_BIT_DIR_OUT;
}

/**
 * mctrl_gpio_set - set gpios according to mctrl state
 * @gpios: gpios to set
 * @mctrl: state to set
 *
 * Set the gpios according to the mctrl state.
 */
void mctrl_gpio_set(struct mctrl_gpios *gpios, unsigned int mctrl)
{
	enum mctrl_gpio_idx i;
	struct gpio_desc *desc_array[UART_GPIO_MAX];
	DECLARE_BITMAP(values, UART_GPIO_MAX);
	unsigned int count = 0;

	if (gpios == NULL)
		return;

	for (i = 0; i < UART_GPIO_MAX; i++)
		if (gpios->gpio[i] && mctrl_gpio_flags_is_dir_out(i)) {
			desc_array[count] = gpios->gpio[i];
			__assign_bit(count, values,
				     mctrl & mctrl_gpios_desc[i].mctrl);
			count++;
		}
	gpiod_set_array_value(count, desc_array, NULL, values);
}
EXPORT_SYMBOL_GPL(mctrl_gpio_set);

/**
 * mctrl_gpio_to_gpiod - obtain gpio_desc of modem line index
 * @gpios: gpios to look into
 * @gidx: index of the modem line
 * Returns: the gpio_desc structure associated to the modem line index
 */
struct gpio_desc *mctrl_gpio_to_gpiod(struct mctrl_gpios *gpios,
				      enum mctrl_gpio_idx gidx)
{
	if (gpios == NULL)
		return NULL;

	return gpios->gpio[gidx];
}
EXPORT_SYMBOL_GPL(mctrl_gpio_to_gpiod);

/**
 * mctrl_gpio_get - update mctrl with the gpios values.
 * @gpios: gpios to get the info from
 * @mctrl: mctrl to set
 * Returns: modified mctrl (the same value as in @mctrl)
 *
 * Update mctrl with the gpios values.
 */
unsigned int mctrl_gpio_get(struct mctrl_gpios *gpios, unsigned int *mctrl)
{
	enum mctrl_gpio_idx i;

	if (gpios == NULL)
		return *mctrl;

	for (i = 0; i < UART_GPIO_MAX; i++) {
		if (gpios->gpio[i] && !mctrl_gpio_flags_is_dir_out(i)) {
			if (gpiod_get_value(gpios->gpio[i]))
				*mctrl |= mctrl_gpios_desc[i].mctrl;
			else
				*mctrl &= ~mctrl_gpios_desc[i].mctrl;
		}
	}

	return *mctrl;
}
EXPORT_SYMBOL_GPL(mctrl_gpio_get);

unsigned int
mctrl_gpio_get_outputs(struct mctrl_gpios *gpios, unsigned int *mctrl)
{
	enum mctrl_gpio_idx i;

	if (gpios == NULL)
		return *mctrl;

	for (i = 0; i < UART_GPIO_MAX; i++) {
		if (gpios->gpio[i] && mctrl_gpio_flags_is_dir_out(i)) {
			if (gpiod_get_value(gpios->gpio[i]))
				*mctrl |= mctrl_gpios_desc[i].mctrl;
			else
				*mctrl &= ~mctrl_gpios_desc[i].mctrl;
		}
	}

	return *mctrl;
}
EXPORT_SYMBOL_GPL(mctrl_gpio_get_outputs);

struct mctrl_gpios *mctrl_gpio_init_noauto(struct device *dev, unsigned int idx)
{
	struct mctrl_gpios *gpios;
	enum mctrl_gpio_idx i;

	gpios = devm_kzalloc(dev, sizeof(*gpios), GFP_KERNEL);
	if (!gpios)
		return ERR_PTR(-ENOMEM);

	for (i = 0; i < UART_GPIO_MAX; i++) {
		char *gpio_str;
		bool present;

		/* Check if GPIO property exists and continue if not */
		gpio_str = kasprintf(GFP_KERNEL, "%s-gpios",
				     mctrl_gpios_desc[i].name);
		if (!gpio_str)
			continue;

		present = device_property_present(dev, gpio_str);
		kfree(gpio_str);
		if (!present)
			continue;

		gpios->gpio[i] =
			devm_gpiod_get_index_optional(dev,
						      mctrl_gpios_desc[i].name,
						      idx,
						      mctrl_gpios_desc[i].flags);

		if (IS_ERR(gpios->gpio[i]))
			return ERR_CAST(gpios->gpio[i]);
	}

	return gpios;
}
EXPORT_SYMBOL_GPL(mctrl_gpio_init_noauto);

#define MCTRL_ANY_DELTA (TIOCM_RI | TIOCM_DSR | TIOCM_CD | TIOCM_CTS)
static irqreturn_t mctrl_gpio_irq_handle(int irq, void *context)
{
	struct mctrl_gpios *gpios = context;
	struct uart_port *port = gpios->port;
	u32 mctrl = gpios->mctrl_prev;
	u32 mctrl_diff;
	unsigned long flags;

	mctrl_gpio_get(gpios, &mctrl);

	uart_port_lock_irqsave(port, &flags);

	mctrl_diff = mctrl ^ gpios->mctrl_prev;
	gpios->mctrl_prev = mctrl;

	if (mctrl_diff & MCTRL_ANY_DELTA && port->state != NULL) {
		if ((mctrl_diff & mctrl) & TIOCM_RI)
			port->icount.rng++;

		if ((mctrl_diff & mctrl) & TIOCM_DSR)
			port->icount.dsr++;

		if (mctrl_diff & TIOCM_CD)
			uart_handle_dcd_change(port, mctrl & TIOCM_CD);

		if (mctrl_diff & TIOCM_CTS)
			uart_handle_cts_change(port, mctrl & TIOCM_CTS);

		wake_up_interruptible(&port->state->port.delta_msr_wait);
	}

	uart_port_unlock_irqrestore(port, flags);

	return IRQ_HANDLED;
}

/**
 * mctrl_gpio_init - initialize uart gpios
 * @port: port to initialize gpios for
 * @idx: index of the gpio in the @port's device
 *
 * This will get the {cts,rts,...}-gpios from device tree if they are present
 * and request them, set direction etc, and return an allocated structure.
 * `devm_*` functions are used, so there's no need to explicitly free.
 * As this sets up the irq handling, make sure to not handle changes to the
 * gpio input lines in your driver, too.
 */
struct mctrl_gpios *mctrl_gpio_init(struct uart_port *port, unsigned int idx)
{
	struct mctrl_gpios *gpios;
	enum mctrl_gpio_idx i;

	gpios = mctrl_gpio_init_noauto(port->dev, idx);
	if (IS_ERR(gpios))
		return gpios;

	gpios->port = port;

	for (i = 0; i < UART_GPIO_MAX; ++i) {
		int ret;

		if (!gpios->gpio[i] || mctrl_gpio_flags_is_dir_out(i))
			continue;

		ret = gpiod_to_irq(gpios->gpio[i]);
		if (ret < 0) {
			dev_err(port->dev,
				"failed to find corresponding irq for %s (idx=%d, err=%d)\n",
				mctrl_gpios_desc[i].name, idx, ret);
			return ERR_PTR(ret);
		}
		gpios->irq[i] = ret;

		/* irqs should only be enabled in .enable_ms */
		irq_set_status_flags(gpios->irq[i], IRQ_NOAUTOEN);

		ret = devm_request_irq(port->dev, gpios->irq[i],
				       mctrl_gpio_irq_handle,
				       IRQ_TYPE_EDGE_BOTH, dev_name(port->dev),
				       gpios);
		if (ret) {
			/* alternatively implement polling */
			dev_err(port->dev,
				"failed to request irq for %s (idx=%d, err=%d)\n",
				mctrl_gpios_desc[i].name, idx, ret);
			return ERR_PTR(ret);
		}
	}

	return gpios;
}
EXPORT_SYMBOL_GPL(mctrl_gpio_init);

/**
 * mctrl_gpio_enable_ms - enable irqs and handling of changes to the ms lines
 * @gpios: gpios to enable
 */
void mctrl_gpio_enable_ms(struct mctrl_gpios *gpios)
{
	enum mctrl_gpio_idx i;

	if (gpios == NULL)
		return;

	/* .enable_ms may be called multiple times */
	if (gpios->mctrl_on)
		return;

	gpios->mctrl_on = true;

	/* get initial status of modem lines GPIOs */
	mctrl_gpio_get(gpios, &gpios->mctrl_prev);

	for (i = 0; i < UART_GPIO_MAX; ++i) {
		if (!gpios->irq[i])
			continue;

		enable_irq(gpios->irq[i]);
	}
}
EXPORT_SYMBOL_GPL(mctrl_gpio_enable_ms);

static void mctrl_gpio_disable_ms(struct mctrl_gpios *gpios, bool sync)
{
	enum mctrl_gpio_idx i;

	if (gpios == NULL)
		return;

	if (!gpios->mctrl_on)
		return;

	gpios->mctrl_on = false;

	for (i = 0; i < UART_GPIO_MAX; ++i) {
		if (!gpios->irq[i])
			continue;

		if (sync)
			disable_irq(gpios->irq[i]);
		else
			disable_irq_nosync(gpios->irq[i]);
	}
}

/**
 * mctrl_gpio_disable_ms_sync - disable irqs and handling of changes to the ms
 * lines, and wait for any pending IRQ to be processed
 * @gpios: gpios to disable
 */
void mctrl_gpio_disable_ms_sync(struct mctrl_gpios *gpios)
{
	mctrl_gpio_disable_ms(gpios, true);
}
EXPORT_SYMBOL_GPL(mctrl_gpio_disable_ms_sync);

/**
 * mctrl_gpio_disable_ms_no_sync - disable irqs and handling of changes to the
 * ms lines, and return immediately
 * @gpios: gpios to disable
 */
void mctrl_gpio_disable_ms_no_sync(struct mctrl_gpios *gpios)
{
	mctrl_gpio_disable_ms(gpios, false);
}
EXPORT_SYMBOL_GPL(mctrl_gpio_disable_ms_no_sync);

void mctrl_gpio_enable_irq_wake(struct mctrl_gpios *gpios)
{
	enum mctrl_gpio_idx i;

	if (!gpios)
		return;

	if (!gpios->mctrl_on)
		return;

	for (i = 0; i < UART_GPIO_MAX; ++i) {
		if (!gpios->irq[i])
			continue;

		enable_irq_wake(gpios->irq[i]);
	}
}
EXPORT_SYMBOL_GPL(mctrl_gpio_enable_irq_wake);

void mctrl_gpio_disable_irq_wake(struct mctrl_gpios *gpios)
{
	enum mctrl_gpio_idx i;

	if (!gpios)
		return;

	if (!gpios->mctrl_on)
		return;

	for (i = 0; i < UART_GPIO_MAX; ++i) {
		if (!gpios->irq[i])
			continue;

		disable_irq_wake(gpios->irq[i]);
	}
}
EXPORT_SYMBOL_GPL(mctrl_gpio_disable_irq_wake);

MODULE_DESCRIPTION("Helpers for controlling modem lines via GPIO");
MODULE_LICENSE("GPL");
