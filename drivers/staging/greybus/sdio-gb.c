/*
 * SD/MMC Greybus driver.
 *
 * Copyright 2014 Google Inc.
 *
 * Released under the GPLv2 only.
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/mmc/host.h>

#include "greybus.h"

struct gb_sdio_host {
	struct mmc_host	*mmc;
	struct mmc_request *mrq;
	// FIXME - some lock?
};

static const struct greybus_module_id id_table[] = {
	{ GREYBUS_DEVICE(0x43, 0x43) },	/* make shit up */
	{ },	/* terminating NULL entry */
};

static void gb_sd_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
	// FIXME - do something here...
}

static void gb_sd_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	// FIXME - do something here...
}

static int gb_sd_get_ro(struct mmc_host *mmc)
{
	// FIXME - do something here...
	return 0;
}

static const struct mmc_host_ops gb_sd_ops = {
	.request	= gb_sd_request,
	.set_ios	= gb_sd_set_ios,
	.get_ro		= gb_sd_get_ro,
};

int gb_sdio_probe(struct gb_module *gmod,
		  const struct greybus_module_id *id)
{
	struct mmc_host *mmc;
	struct gb_sdio_host *host;

	mmc = mmc_alloc_host(sizeof(struct gb_sdio_host), &gmod->dev);
	if (!mmc)
		return -ENOMEM;

	host = mmc_priv(mmc);
	host->mmc = mmc;

	mmc->ops = &gb_sd_ops;
	// FIXME - set up size limits we can handle.

	gmod->gb_sdio_host = host;
	return 0;
}

void gb_sdio_disconnect(struct gb_module *gmod)
{
	struct mmc_host *mmc;
	struct gb_sdio_host *host;

	host = gmod->gb_sdio_host;
	mmc = host->mmc;

	mmc_remove_host(mmc);
	mmc_free_host(mmc);
}

#if 0
static struct greybus_driver sd_gb_driver = {
	.probe =	gb_sdio_probe,
	.disconnect =	gb_sdio_disconnect,
	.id_table =	id_table,
};

module_greybus_driver(sd_gb_driver);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Greybus SD/MMC Host driver");
MODULE_AUTHOR("Greg Kroah-Hartman <gregkh@linuxfoundation.org>");
#endif
