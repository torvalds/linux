// SPDX-License-Identifier: GPL-2.0-only
/*
 * SPI interface.
 *
 * Copyright (c) 2017-2019, Silicon Laboratories, Inc.
 * Copyright (c) 2011, Sagrad Inc.
 * Copyright (c) 2010, ST-Ericsson
 */
#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/of.h>

#include "bus.h"

static int wfx_spi_probe(struct spi_device *func)
{
	return -EIO;
}

/* Disconnect Function to be called by SPI stack when device is disconnected */
static int wfx_spi_disconnect(struct spi_device *func)
{
	return 0;
}

/*
 * For dynamic driver binding, kernel does not use OF to match driver. It only
 * use modalias and modalias is a copy of 'compatible' DT node with vendor
 * stripped.
 */
static const struct spi_device_id wfx_spi_id[] = {
	{ "wfx-spi", 0 },
	{ },
};
MODULE_DEVICE_TABLE(spi, wfx_spi_id);

#ifdef CONFIG_OF
static const struct of_device_id wfx_spi_of_match[] = {
	{ .compatible = "silabs,wfx-spi" },
	{ },
};
MODULE_DEVICE_TABLE(of, wfx_spi_of_match);
#endif

struct spi_driver wfx_spi_driver = {
	.driver = {
		.name = "wfx-spi",
		.of_match_table = of_match_ptr(wfx_spi_of_match),
	},
	.id_table = wfx_spi_id,
	.probe = wfx_spi_probe,
	.remove = wfx_spi_disconnect,
};
