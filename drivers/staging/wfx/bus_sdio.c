// SPDX-License-Identifier: GPL-2.0-only
/*
 * SDIO interface.
 *
 * Copyright (c) 2017-2019, Silicon Laboratories, Inc.
 * Copyright (c) 2010, ST-Ericsson
 */
#include <linux/module.h>
#include <linux/mmc/sdio_func.h>
#include <linux/mmc/card.h>
#include <linux/of_irq.h>

#include "bus.h"

static const struct of_device_id wfx_sdio_of_match[];
static int wfx_sdio_probe(struct sdio_func *func,
			  const struct sdio_device_id *id)
{
	struct device_node *np = func->dev.of_node;

	if (func->num != 1) {
		dev_err(&func->dev, "SDIO function number is %d while it should always be 1 (unsupported chip?)\n", func->num);
		return -ENODEV;
	}

	if (np) {
		if (!of_match_node(wfx_sdio_of_match, np)) {
			dev_warn(&func->dev, "no compatible device found in DT\n");
			return -ENODEV;
		}
	} else {
		dev_warn(&func->dev, "device is not declared in DT, features will be limited\n");
		// FIXME: ignore VID/PID and only rely on device tree
		// return -ENODEV;
	}
	return -EIO; // FIXME: not yet supported
}

static void wfx_sdio_remove(struct sdio_func *func)
{
}

#define SDIO_VENDOR_ID_SILABS        0x0000
#define SDIO_DEVICE_ID_SILABS_WF200  0x1000
static const struct sdio_device_id wfx_sdio_ids[] = {
	{ SDIO_DEVICE(SDIO_VENDOR_ID_SILABS, SDIO_DEVICE_ID_SILABS_WF200) },
	// FIXME: ignore VID/PID and only rely on device tree
	// { SDIO_DEVICE(SDIO_ANY_ID, SDIO_ANY_ID) },
	{ },
};
MODULE_DEVICE_TABLE(sdio, wfx_sdio_ids);

#ifdef CONFIG_OF
static const struct of_device_id wfx_sdio_of_match[] = {
	{ .compatible = "silabs,wfx-sdio" },
	{ },
};
MODULE_DEVICE_TABLE(of, wfx_sdio_of_match);
#endif

struct sdio_driver wfx_sdio_driver = {
	.name = "wfx-sdio",
	.id_table = wfx_sdio_ids,
	.probe = wfx_sdio_probe,
	.remove = wfx_sdio_remove,
	.drv = {
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(wfx_sdio_of_match),
	}
};
