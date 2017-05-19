/*
 * NFIT - Machine Check Handler
 *
 * Copyright(c) 2013-2016 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */
#include <linux/notifier.h>
#include <linux/acpi.h>
#include <linux/nd.h>
#include <asm/mce.h>
#include "nfit.h"

static int nfit_handle_mce(struct notifier_block *nb, unsigned long val,
			void *data)
{
	struct mce *mce = (struct mce *)data;
	struct acpi_nfit_desc *acpi_desc;
	struct nfit_spa *nfit_spa;

	/* We only care about memory errors */
	if (!mce_is_memory_error(mce))
		return NOTIFY_DONE;

	/*
	 * mce->addr contains the physical addr accessed that caused the
	 * machine check. We need to walk through the list of NFITs, and see
	 * if any of them matches that address, and only then start a scrub.
	 */
	mutex_lock(&acpi_desc_lock);
	list_for_each_entry(acpi_desc, &acpi_descs, list) {
		struct device *dev = acpi_desc->dev;
		int found_match = 0;

		mutex_lock(&acpi_desc->init_mutex);
		list_for_each_entry(nfit_spa, &acpi_desc->spas, list) {
			struct acpi_nfit_system_address *spa = nfit_spa->spa;

			if (nfit_spa_type(spa) != NFIT_SPA_PM)
				continue;
			/* find the spa that covers the mce addr */
			if (spa->address > mce->addr)
				continue;
			if ((spa->address + spa->length - 1) < mce->addr)
				continue;
			found_match = 1;
			dev_dbg(dev, "%s: addr in SPA %d (0x%llx, 0x%llx)\n",
				__func__, spa->range_index, spa->address,
				spa->length);
			/*
			 * We can break at the first match because we're going
			 * to rescan all the SPA ranges. There shouldn't be any
			 * aliasing anyway.
			 */
			break;
		}
		mutex_unlock(&acpi_desc->init_mutex);

		if (!found_match)
			continue;

		/* If this fails due to an -ENOMEM, there is little we can do */
		nvdimm_bus_add_poison(acpi_desc->nvdimm_bus,
				ALIGN(mce->addr, L1_CACHE_BYTES),
				L1_CACHE_BYTES);
		nvdimm_region_notify(nfit_spa->nd_region,
				NVDIMM_REVALIDATE_POISON);

		if (acpi_desc->scrub_mode == HW_ERROR_SCRUB_ON) {
			/*
			 * We can ignore an -EBUSY here because if an ARS is
			 * already in progress, just let that be the last
			 * authoritative one
			 */
			acpi_nfit_ars_rescan(acpi_desc);
		}
		break;
	}

	mutex_unlock(&acpi_desc_lock);
	return NOTIFY_DONE;
}

static struct notifier_block nfit_mce_dec = {
	.notifier_call	= nfit_handle_mce,
};

void nfit_mce_register(void)
{
	mce_register_decode_chain(&nfit_mce_dec);
}

void nfit_mce_unregister(void)
{
	mce_unregister_decode_chain(&nfit_mce_dec);
}
