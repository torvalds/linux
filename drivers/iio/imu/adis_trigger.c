/*
 * Common library for ADIS16XXX devices
 *
 * Copyright 2012 Analog Devices Inc.
 *   Author: Lars-Peter Clausen <lars@metafoo.de>
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/spi/spi.h>
#include <linux/export.h>

#include <linux/iio/iio.h>
#include <linux/iio/trigger.h>
#include <linux/iio/imu/adis.h>

static int adis_data_rdy_trigger_set_state(struct iio_trigger *trig,
						bool state)
{
	struct adis *adis = iio_trigger_get_drvdata(trig);

	return adis_enable_irq(adis, state);
}

static const struct iio_trigger_ops adis_trigger_ops = {
	.set_trigger_state = &adis_data_rdy_trigger_set_state,
};

/**
 * adis_probe_trigger() - Sets up trigger for a adis device
 * @adis: The adis device
 * @indio_dev: The IIO device
 *
 * Returns 0 on success or a negative error code
 *
 * adis_remove_trigger() should be used to free the trigger.
 */
int adis_probe_trigger(struct adis *adis, struct iio_dev *indio_dev)
{
	int ret;

	adis->trig = iio_trigger_alloc("%s-dev%d", indio_dev->name,
					indio_dev->id);
	if (adis->trig == NULL)
		return -ENOMEM;

	ret = request_irq(adis->spi->irq,
			  &iio_trigger_generic_data_rdy_poll,
			  IRQF_TRIGGER_RISING,
			  indio_dev->name,
			  adis->trig);
	if (ret)
		goto error_free_trig;

	adis->trig->dev.parent = &adis->spi->dev;
	adis->trig->ops = &adis_trigger_ops;
	iio_trigger_set_drvdata(adis->trig, adis);
	ret = iio_trigger_register(adis->trig);

	indio_dev->trig = iio_trigger_get(adis->trig);
	if (ret)
		goto error_free_irq;

	return 0;

error_free_irq:
	free_irq(adis->spi->irq, adis->trig);
error_free_trig:
	iio_trigger_free(adis->trig);
	return ret;
}
EXPORT_SYMBOL_GPL(adis_probe_trigger);

/**
 * adis_remove_trigger() - Remove trigger for a adis devices
 * @adis: The adis device
 *
 * Removes the trigger previously registered with adis_probe_trigger().
 */
void adis_remove_trigger(struct adis *adis)
{
	iio_trigger_unregister(adis->trig);
	free_irq(adis->spi->irq, adis->trig);
	iio_trigger_free(adis->trig);
}
EXPORT_SYMBOL_GPL(adis_remove_trigger);
