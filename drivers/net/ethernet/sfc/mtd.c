/****************************************************************************
 * Driver for Solarflare Solarstorm network controllers and boards
 * Copyright 2005-2006 Fen Systems Ltd.
 * Copyright 2006-2010 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#include <linux/module.h>
#include <linux/mtd/mtd.h>
#include <linux/slab.h>
#include <linux/rtnetlink.h>

#include "net_driver.h"
#include "efx.h"

#define to_efx_mtd_partition(mtd)				\
	container_of(mtd, struct efx_mtd_partition, mtd)

/* MTD interface */

static int efx_mtd_erase(struct mtd_info *mtd, struct erase_info *erase)
{
	struct efx_nic *efx = mtd->priv;
	int rc;

	rc = efx->type->mtd_erase(mtd, erase->addr, erase->len);
	if (rc == 0) {
		erase->state = MTD_ERASE_DONE;
	} else {
		erase->state = MTD_ERASE_FAILED;
		erase->fail_addr = MTD_FAIL_ADDR_UNKNOWN;
	}
	mtd_erase_callback(erase);
	return rc;
}

static void efx_mtd_sync(struct mtd_info *mtd)
{
	struct efx_mtd_partition *part = to_efx_mtd_partition(mtd);
	struct efx_nic *efx = mtd->priv;
	int rc;

	rc = efx->type->mtd_sync(mtd);
	if (rc)
		pr_err("%s: %s sync failed (%d)\n",
		       part->name, part->dev_type_name, rc);
}

static void efx_mtd_remove_partition(struct efx_mtd_partition *part)
{
	int rc;

	for (;;) {
		rc = mtd_device_unregister(&part->mtd);
		if (rc != -EBUSY)
			break;
		ssleep(1);
	}
	WARN_ON(rc);
	list_del(&part->node);
}

int efx_mtd_add(struct efx_nic *efx, struct efx_mtd_partition *parts,
		size_t n_parts, size_t sizeof_part)
{
	struct efx_mtd_partition *part;
	size_t i;

	for (i = 0; i < n_parts; i++) {
		part = (struct efx_mtd_partition *)((char *)parts +
						    i * sizeof_part);

		part->mtd.writesize = 1;

		part->mtd.owner = THIS_MODULE;
		part->mtd.priv = efx;
		part->mtd.name = part->name;
		part->mtd._erase = efx_mtd_erase;
		part->mtd._read = efx->type->mtd_read;
		part->mtd._write = efx->type->mtd_write;
		part->mtd._sync = efx_mtd_sync;

		efx->type->mtd_rename(part);

		if (mtd_device_register(&part->mtd, NULL, 0))
			goto fail;

		/* Add to list in order - efx_mtd_remove() depends on this */
		list_add_tail(&part->node, &efx->mtd_list);
	}

	return 0;

fail:
	while (i--) {
		part = (struct efx_mtd_partition *)((char *)parts +
						    i * sizeof_part);
		efx_mtd_remove_partition(part);
	}
	/* Failure is unlikely here, but probably means we're out of memory */
	return -ENOMEM;
}

void efx_mtd_remove(struct efx_nic *efx)
{
	struct efx_mtd_partition *parts, *part, *next;

	WARN_ON(efx_dev_registered(efx));

	if (list_empty(&efx->mtd_list))
		return;

	parts = list_first_entry(&efx->mtd_list, struct efx_mtd_partition,
				 node);

	list_for_each_entry_safe(part, next, &efx->mtd_list, node)
		efx_mtd_remove_partition(part);

	kfree(parts);
}

void efx_mtd_rename(struct efx_nic *efx)
{
	struct efx_mtd_partition *part;

	ASSERT_RTNL();

	list_for_each_entry(part, &efx->mtd_list, node)
		efx->type->mtd_rename(part);
}
