/*
 * Broadcom B43 wireless driver
 *
 * SDIO over Sonics Silicon Backplane bus glue for b43.
 *
 * Copyright (C) 2009 Albert Herranz
 * Copyright (C) 2009 Michael Buesch <mb@bu3sch.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/mmc/card.h>
#include <linux/mmc/sdio_func.h>
#include <linux/mmc/sdio_ids.h>
#include <linux/slab.h>
#include <linux/ssb/ssb.h>

#include "sdio.h"
#include "b43.h"


#define HNBU_CHIPID		0x01	/* vendor & device id */

#define B43_SDIO_BLOCK_SIZE	64	/* rx fifo max size in bytes */


static const struct b43_sdio_quirk {
	u16 vendor;
	u16 device;
	unsigned int quirks;
} b43_sdio_quirks[] = {
	{ 0x14E4, 0x4318, SSB_QUIRK_SDIO_READ_AFTER_WRITE32, },
	{ },
};


static unsigned int b43_sdio_get_quirks(u16 vendor, u16 device)
{
	const struct b43_sdio_quirk *q;

	for (q = b43_sdio_quirks; q->quirks; q++) {
		if (vendor == q->vendor && device == q->device)
			return q->quirks;
	}

	return 0;
}

static void b43_sdio_interrupt_dispatcher(struct sdio_func *func)
{
	struct b43_sdio *sdio = sdio_get_drvdata(func);
	struct b43_wldev *dev = sdio->irq_handler_opaque;

	if (unlikely(b43_status(dev) < B43_STAT_STARTED))
		return;

	sdio_release_host(func);
	sdio->irq_handler(dev);
	sdio_claim_host(func);
}

int b43_sdio_request_irq(struct b43_wldev *dev,
			 void (*handler)(struct b43_wldev *dev))
{
	struct ssb_bus *bus = dev->dev->bus;
	struct sdio_func *func = bus->host_sdio;
	struct b43_sdio *sdio = sdio_get_drvdata(func);
	int err;

	sdio->irq_handler_opaque = dev;
	sdio->irq_handler = handler;
	sdio_claim_host(func);
	err = sdio_claim_irq(func, b43_sdio_interrupt_dispatcher);
	sdio_release_host(func);

	return err;
}

void b43_sdio_free_irq(struct b43_wldev *dev)
{
	struct ssb_bus *bus = dev->dev->bus;
	struct sdio_func *func = bus->host_sdio;
	struct b43_sdio *sdio = sdio_get_drvdata(func);

	sdio_claim_host(func);
	sdio_release_irq(func);
	sdio_release_host(func);
	sdio->irq_handler_opaque = NULL;
	sdio->irq_handler = NULL;
}

static int b43_sdio_probe(struct sdio_func *func,
			  const struct sdio_device_id *id)
{
	struct b43_sdio *sdio;
	struct sdio_func_tuple *tuple;
	u16 vendor = 0, device = 0;
	int error;

	/* Look for the card chip identifier. */
	tuple = func->tuples;
	while (tuple) {
		switch (tuple->code) {
		case 0x80:
			switch (tuple->data[0]) {
			case HNBU_CHIPID:
				if (tuple->size != 5)
					break;
				vendor = tuple->data[1] | (tuple->data[2]<<8);
				device = tuple->data[3] | (tuple->data[4]<<8);
				dev_info(&func->dev, "Chip ID %04x:%04x\n",
					 vendor, device);
				break;
			default:
				break;
			}
			break;
		default:
			break;
		}
		tuple = tuple->next;
	}
	if (!vendor || !device) {
		error = -ENODEV;
		goto out;
	}

	sdio_claim_host(func);
	error = sdio_set_block_size(func, B43_SDIO_BLOCK_SIZE);
	if (error) {
		dev_err(&func->dev, "failed to set block size to %u bytes,"
			" error %d\n", B43_SDIO_BLOCK_SIZE, error);
		goto err_release_host;
	}
	error = sdio_enable_func(func);
	if (error) {
		dev_err(&func->dev, "failed to enable func, error %d\n", error);
		goto err_release_host;
	}
	sdio_release_host(func);

	sdio = kzalloc(sizeof(*sdio), GFP_KERNEL);
	if (!sdio) {
		error = -ENOMEM;
		dev_err(&func->dev, "failed to allocate ssb bus\n");
		goto err_disable_func;
	}
	error = ssb_bus_sdiobus_register(&sdio->ssb, func,
					 b43_sdio_get_quirks(vendor, device));
	if (error) {
		dev_err(&func->dev, "failed to register ssb sdio bus,"
			" error %d\n", error);
		goto err_free_ssb;
	}
	sdio_set_drvdata(func, sdio);

	return 0;

err_free_ssb:
	kfree(sdio);
err_disable_func:
	sdio_disable_func(func);
err_release_host:
	sdio_release_host(func);
out:
	return error;
}

static void b43_sdio_remove(struct sdio_func *func)
{
	struct b43_sdio *sdio = sdio_get_drvdata(func);

	ssb_bus_unregister(&sdio->ssb);
	sdio_disable_func(func);
	kfree(sdio);
	sdio_set_drvdata(func, NULL);
}

static const struct sdio_device_id b43_sdio_ids[] = {
	{ SDIO_DEVICE(0x02d0, 0x044b) }, /* Nintendo Wii WLAN daughter card */
	{ },
};

static struct sdio_driver b43_sdio_driver = {
	.name		= "b43-sdio",
	.id_table	= b43_sdio_ids,
	.probe		= b43_sdio_probe,
	.remove		= b43_sdio_remove,
};

int b43_sdio_init(void)
{
	return sdio_register_driver(&b43_sdio_driver);
}

void b43_sdio_exit(void)
{
	sdio_unregister_driver(&b43_sdio_driver);
}
