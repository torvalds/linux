// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * serdes-irq.c  --  Interrupt controller support for different serdes chips
 *
 * Copyright 2009 Wolfson Microelectronics PLC.
 *
 * Author: luowei <lw@rock-chips.com>
 */

#include "core.h"

static irqreturn_t serdes_bridge_lock_irq_handler(int irq, void *arg)
{
	struct serdes *serdes = arg;
	int ret = 0;

	if (serdes->chip_data->irq_ops->lock_handle)
		ret = serdes->chip_data->irq_ops->lock_handle(serdes);

	if (extcon_get_state(serdes->extcon, EXTCON_JACK_VIDEO_OUT))
		atomic_set(&serdes->serdes_bridge->triggered, 1);

	SERDES_DBG_MFD("%s %s %s ret=%d\n", __func__, dev_name(serdes->dev),
				   serdes->chip_data->name, ret);

	return IRQ_HANDLED;
}

static irqreturn_t serdes_bridge_err_irq_handler(int irq, void *arg)
{
	struct serdes *serdes = arg;
	int ret = 0;

	if (serdes->chip_data->irq_ops->err_handle)
		ret = serdes->chip_data->irq_ops->err_handle(serdes);

	SERDES_DBG_MFD("%s %s %s ret=%d\n", __func__, dev_name(serdes->dev),
				   serdes->chip_data->name, ret);

	return IRQ_HANDLED;
}

int serdes_irq_init(struct serdes *serdes)
{
	int ret = 0;

	mutex_init(&serdes->irq_lock);

	/* lock irq */
	serdes->lock_gpio = devm_gpiod_get_optional(serdes->dev, "lock", GPIOD_IN);
	if (IS_ERR(serdes->lock_gpio))
		return dev_err_probe(serdes->dev, PTR_ERR(serdes->lock_gpio),
				     "failed to get serdes lock GPIO\n");

	if (serdes->lock_gpio) {
		serdes->lock_irq = gpiod_to_irq(serdes->lock_gpio);
		if (serdes->lock_irq < 0)
			return serdes->lock_irq;

		SERDES_DBG_MFD("%s %s lock_irq=%d gpio=%d\n", __func__,
			       serdes->chip_data->name, serdes->lock_irq,
			       desc_to_gpio(serdes->lock_gpio));

		ret = devm_request_threaded_irq(serdes->dev, serdes->lock_irq, NULL,
						serdes_bridge_lock_irq_handler,
						IRQF_TRIGGER_RISING | IRQF_ONESHOT,
						dev_name(serdes->dev), serdes);
		if (ret)
			return dev_err_probe(serdes->dev, ret,
				     "failed to request serdes lock IRQ\n");
	}

	/* error irq */
	serdes->err_gpio = devm_gpiod_get_optional(serdes->dev, "err", GPIOD_IN);
	if (IS_ERR(serdes->err_gpio))
		return dev_err_probe(serdes->dev, PTR_ERR(serdes->err_gpio),
				     "failed to get serdes err GPIO\n");

	if (serdes->err_gpio) {
		serdes->err_irq = gpiod_to_irq(serdes->err_gpio);
		if (serdes->err_irq < 0)
			return serdes->err_irq;

		SERDES_DBG_MFD("%s %s err_irq=%d\n", __func__,
			       serdes->chip_data->name, serdes->err_irq);

		ret = devm_request_threaded_irq(serdes->dev, serdes->err_irq, NULL,
						serdes_bridge_err_irq_handler,
						IRQF_TRIGGER_RISING | IRQF_ONESHOT,
						dev_name(serdes->dev), serdes);
		if (ret)
			return dev_err_probe(serdes->dev, ret, "failed to request err IRQ\n");
	}

	SERDES_DBG_MFD("serdes %s serdes_irq_init successful, ret=%d\n",
		       serdes->chip_data->name, ret);

	return 0;
}
EXPORT_SYMBOL_GPL(serdes_irq_init);

void serdes_irq_exit(struct serdes *serdes)
{
	if (serdes->lock_irq)
		devm_free_irq(serdes->dev, serdes->lock_irq, serdes);

	if (serdes->err_irq)
		devm_free_irq(serdes->dev, serdes->err_irq, serdes);
}
EXPORT_SYMBOL_GPL(serdes_irq_exit);

MODULE_LICENSE("GPL");
