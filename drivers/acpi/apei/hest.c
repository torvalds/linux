// SPDX-License-Identifier: GPL-2.0-only
/*
 * APEI Hardware Error Source Table support
 *
 * HEST describes error sources in detail; communicates operational
 * parameters (i.e. severity levels, masking bits, and threshold
 * values) to Linux as necessary. It also allows the BIOS to report
 * non-standard error sources to Linux (for example, chipset-specific
 * error registers).
 *
 * For more information about HEST, please refer to ACPI Specification
 * version 4.0, section 17.3.2.
 *
 * Copyright 2009 Intel Corp.
 *   Author: Huang Ying <ying.huang@intel.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/acpi.h>
#include <linux/kdebug.h>
#include <linux/highmem.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <acpi/apei.h>
#include <acpi/ghes.h>

#include "apei-internal.h"

#define HEST_PFX "HEST: "

int hest_disable;
EXPORT_SYMBOL_GPL(hest_disable);

/* HEST table parsing */

static struct acpi_table_hest *__read_mostly hest_tab;

/*
 * Since GHES_ASSIST is not supported, skip initialization of GHES_ASSIST
 * structures for MCA.
 * During HEST parsing, detected MCA error sources are cached from early
 * table entries so that the Flags and Source Id fields from these cached
 * values are then referred to in later table entries to determine if the
 * encountered GHES_ASSIST structure should be initialized.
 */
static struct {
	struct acpi_hest_ia_corrected *cmc;
	struct acpi_hest_ia_machine_check *mc;
	struct acpi_hest_ia_deferred_check *dmc;
} mces;

static const int hest_esrc_len_tab[ACPI_HEST_TYPE_RESERVED] = {
	[ACPI_HEST_TYPE_IA32_CHECK] = -1,	/* need further calculation */
	[ACPI_HEST_TYPE_IA32_CORRECTED_CHECK] = -1,
	[ACPI_HEST_TYPE_IA32_NMI] = sizeof(struct acpi_hest_ia_nmi),
	[ACPI_HEST_TYPE_AER_ROOT_PORT] = sizeof(struct acpi_hest_aer_root),
	[ACPI_HEST_TYPE_AER_ENDPOINT] = sizeof(struct acpi_hest_aer),
	[ACPI_HEST_TYPE_AER_BRIDGE] = sizeof(struct acpi_hest_aer_bridge),
	[ACPI_HEST_TYPE_GENERIC_ERROR] = sizeof(struct acpi_hest_generic),
	[ACPI_HEST_TYPE_GENERIC_ERROR_V2] = sizeof(struct acpi_hest_generic_v2),
	[ACPI_HEST_TYPE_IA32_DEFERRED_CHECK] = -1,
};

static inline bool is_generic_error(struct acpi_hest_header *hest_hdr)
{
	return hest_hdr->type == ACPI_HEST_TYPE_GENERIC_ERROR ||
	       hest_hdr->type == ACPI_HEST_TYPE_GENERIC_ERROR_V2;
}

static int hest_esrc_len(struct acpi_hest_header *hest_hdr)
{
	u16 hest_type = hest_hdr->type;
	int len;

	if (hest_type >= ACPI_HEST_TYPE_RESERVED)
		return 0;

	len = hest_esrc_len_tab[hest_type];

	if (hest_type == ACPI_HEST_TYPE_IA32_CORRECTED_CHECK) {
		struct acpi_hest_ia_corrected *cmc;
		cmc = (struct acpi_hest_ia_corrected *)hest_hdr;
		len = sizeof(*cmc) + cmc->num_hardware_banks *
			sizeof(struct acpi_hest_ia_error_bank);
		mces.cmc = cmc;
	} else if (hest_type == ACPI_HEST_TYPE_IA32_CHECK) {
		struct acpi_hest_ia_machine_check *mc;
		mc = (struct acpi_hest_ia_machine_check *)hest_hdr;
		len = sizeof(*mc) + mc->num_hardware_banks *
			sizeof(struct acpi_hest_ia_error_bank);
		mces.mc = mc;
	} else if (hest_type == ACPI_HEST_TYPE_IA32_DEFERRED_CHECK) {
		struct acpi_hest_ia_deferred_check *mc;
		mc = (struct acpi_hest_ia_deferred_check *)hest_hdr;
		len = sizeof(*mc) + mc->num_hardware_banks *
			sizeof(struct acpi_hest_ia_error_bank);
		mces.dmc = mc;
	}
	BUG_ON(len == -1);

	return len;
};

/*
 * GHES and GHESv2 structures share the same format, starting from
 * Source Id and ending in Error Status Block Length (inclusive).
 */
static bool is_ghes_assist_struct(struct acpi_hest_header *hest_hdr)
{
	struct acpi_hest_generic *ghes;
	u16 related_source_id;

	if (hest_hdr->type != ACPI_HEST_TYPE_GENERIC_ERROR &&
	    hest_hdr->type != ACPI_HEST_TYPE_GENERIC_ERROR_V2)
		return false;

	ghes = (struct acpi_hest_generic *)hest_hdr;
	related_source_id = ghes->related_source_id;

	if (mces.cmc && mces.cmc->flags & ACPI_HEST_GHES_ASSIST &&
	    related_source_id == mces.cmc->header.source_id)
		return true;
	if (mces.mc && mces.mc->flags & ACPI_HEST_GHES_ASSIST &&
	    related_source_id == mces.mc->header.source_id)
		return true;
	if (mces.dmc && mces.dmc->flags & ACPI_HEST_GHES_ASSIST &&
	    related_source_id == mces.dmc->header.source_id)
		return true;

	return false;
}

typedef int (*apei_hest_func_t)(struct acpi_hest_header *hest_hdr, void *data);

static int apei_hest_parse(apei_hest_func_t func, void *data)
{
	struct acpi_hest_header *hest_hdr;
	int i, rc, len;

	if (hest_disable || !hest_tab)
		return -EINVAL;

	hest_hdr = (struct acpi_hest_header *)(hest_tab + 1);
	for (i = 0; i < hest_tab->error_source_count; i++) {
		len = hest_esrc_len(hest_hdr);
		if (!len) {
			pr_warn(FW_WARN HEST_PFX
				"Unknown or unused hardware error source "
				"type: %d for hardware error source: %d.\n",
				hest_hdr->type, hest_hdr->source_id);
			return -EINVAL;
		}
		if ((void *)hest_hdr + len >
		    (void *)hest_tab + hest_tab->header.length) {
			pr_warn(FW_BUG HEST_PFX
		"Table contents overflow for hardware error source: %d.\n",
				hest_hdr->source_id);
			return -EINVAL;
		}

		if (is_ghes_assist_struct(hest_hdr)) {
			hest_hdr = (void *)hest_hdr + len;
			continue;
		}

		rc = func(hest_hdr, data);
		if (rc)
			return rc;

		hest_hdr = (void *)hest_hdr + len;
	}

	return 0;
}

/*
 * Check if firmware advertises firmware first mode. We need FF bit to be set
 * along with a set of MC banks which work in FF mode.
 */
static int __init hest_parse_cmc(struct acpi_hest_header *hest_hdr, void *data)
{
	if (hest_hdr->type != ACPI_HEST_TYPE_IA32_CORRECTED_CHECK)
		return 0;

	if (!acpi_disable_cmcff)
		return !arch_apei_enable_cmcff(hest_hdr, data);

	return 0;
}

struct ghes_arr {
	struct platform_device **ghes_devs;
	unsigned int count;
};

static int __init hest_parse_ghes_count(struct acpi_hest_header *hest_hdr, void *data)
{
	int *count = data;

	if (is_generic_error(hest_hdr))
		(*count)++;
	return 0;
}

static int __init hest_parse_ghes(struct acpi_hest_header *hest_hdr, void *data)
{
	struct platform_device *ghes_dev;
	struct ghes_arr *ghes_arr = data;
	int rc, i;

	if (!is_generic_error(hest_hdr))
		return 0;

	if (!((struct acpi_hest_generic *)hest_hdr)->enabled)
		return 0;
	for (i = 0; i < ghes_arr->count; i++) {
		struct acpi_hest_header *hdr;
		ghes_dev = ghes_arr->ghes_devs[i];
		hdr = *(struct acpi_hest_header **)ghes_dev->dev.platform_data;
		if (hdr->source_id == hest_hdr->source_id) {
			pr_warn(FW_WARN HEST_PFX "Duplicated hardware error source ID: %d.\n",
				hdr->source_id);
			return -EIO;
		}
	}
	ghes_dev = platform_device_alloc("GHES", hest_hdr->source_id);
	if (!ghes_dev)
		return -ENOMEM;

	rc = platform_device_add_data(ghes_dev, &hest_hdr, sizeof(void *));
	if (rc)
		goto err;

	rc = platform_device_add(ghes_dev);
	if (rc)
		goto err;
	ghes_arr->ghes_devs[ghes_arr->count++] = ghes_dev;

	return 0;
err:
	platform_device_put(ghes_dev);
	return rc;
}

static int __init hest_ghes_dev_register(unsigned int ghes_count)
{
	int rc, i;
	struct ghes_arr ghes_arr;

	ghes_arr.count = 0;
	ghes_arr.ghes_devs = kmalloc_array(ghes_count, sizeof(void *),
					   GFP_KERNEL);
	if (!ghes_arr.ghes_devs)
		return -ENOMEM;

	rc = apei_hest_parse(hest_parse_ghes, &ghes_arr);
	if (rc)
		goto err;

	rc = ghes_estatus_pool_init(ghes_count);
	if (rc)
		goto err;

out:
	kfree(ghes_arr.ghes_devs);
	return rc;
err:
	for (i = 0; i < ghes_arr.count; i++)
		platform_device_unregister(ghes_arr.ghes_devs[i]);
	goto out;
}

static int __init setup_hest_disable(char *str)
{
	hest_disable = HEST_DISABLED;
	return 1;
}

__setup("hest_disable", setup_hest_disable);

void __init acpi_hest_init(void)
{
	acpi_status status;
	int rc;
	unsigned int ghes_count = 0;

	if (hest_disable) {
		pr_info(HEST_PFX "Table parsing disabled.\n");
		return;
	}

	status = acpi_get_table(ACPI_SIG_HEST, 0,
				(struct acpi_table_header **)&hest_tab);
	if (status == AE_NOT_FOUND) {
		hest_disable = HEST_NOT_FOUND;
		return;
	} else if (ACPI_FAILURE(status)) {
		const char *msg = acpi_format_exception(status);
		pr_err(HEST_PFX "Failed to get table, %s\n", msg);
		hest_disable = HEST_DISABLED;
		return;
	}

	rc = apei_hest_parse(hest_parse_cmc, NULL);
	if (rc)
		goto err;

	if (!ghes_disable) {
		rc = apei_hest_parse(hest_parse_ghes_count, &ghes_count);
		if (rc)
			goto err;

		if (ghes_count)
			rc = hest_ghes_dev_register(ghes_count);
		if (rc)
			goto err;
	}

	pr_info(HEST_PFX "Table parsing has been initialized.\n");
	return;
err:
	hest_disable = HEST_DISABLED;
	acpi_put_table((struct acpi_table_header *)hest_tab);
}
