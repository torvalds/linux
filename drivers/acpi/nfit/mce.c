// SPDX-License-Identifier: GPL-2.0-only
/*
 * NFIT - Machine Check Handler
 *
 * Copyright(c) 2013-2016 Intel Corporation. All rights reserved.
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

	/* We only care about uncorrectable memory errors */
	if (!mce_is_memory_error(mce) || mce_is_correctable(mce))
		return NOTIFY_DONE;

	/* Verify the address reported in the MCE is valid. */
	if (!mce_usable_address(mce))
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
			dev_dbg(dev, "addr in SPA %d (0x%llx, 0x%llx)\n",
				spa->range_index, spa->address, spa->length);
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
		nvdimm_bus_add_badrange(acpi_desc->nvdimm_bus,
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
			acpi_nfit_ars_rescan(acpi_desc, 0);
		}
		mce->kflags |= MCE_HANDLED_NFIT;
		break;
	}

	mutex_unlock(&acpi_desc_lock);
	return NOTIFY_DONE;
}

static struct notifier_block nfit_mce_dec = {
	.notifier_call	= nfit_handle_mce,
	.priority	= MCE_PRIO_NFIT,
};

void nfit_mce_register(void)
{
	mce_register_decode_chain(&nfit_mce_dec);
}

void nfit_mce_unregister(void)
{
	mce_unregister_decode_chain(&nfit_mce_dec);
}
