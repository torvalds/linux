// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2025 Arm Ltd.

/* Parse the MPAM ACPI table feeding the discovered nodes into the driver */

#define pr_fmt(fmt) "ACPI MPAM: " fmt

#include <linux/acpi.h>
#include <linux/arm_mpam.h>
#include <linux/bits.h>
#include <linux/cpu.h>
#include <linux/cpumask.h>
#include <linux/platform_device.h>

#include <acpi/processor.h>

/*
 * Flags for acpi_table_mpam_msc.*_interrupt_flags.
 * See 2.1.1 Interrupt Flags, Table 5, of DEN0065B_MPAM_ACPI_3.0-bet.
 */
#define ACPI_MPAM_MSC_IRQ_MODE                              BIT(0)
#define ACPI_MPAM_MSC_IRQ_TYPE_MASK                         GENMASK(2, 1)
#define ACPI_MPAM_MSC_IRQ_TYPE_WIRED                        0
#define ACPI_MPAM_MSC_IRQ_AFFINITY_TYPE_MASK                BIT(3)
#define ACPI_MPAM_MSC_IRQ_AFFINITY_TYPE_PROCESSOR           0
#define ACPI_MPAM_MSC_IRQ_AFFINITY_TYPE_PROCESSOR_CONTAINER 1
#define ACPI_MPAM_MSC_IRQ_AFFINITY_VALID                    BIT(4)

/*
 * Encodings for the MSC node body interface type field.
 * See 2.1 MPAM MSC node, Table 4 of DEN0065B_MPAM_ACPI_3.0-bet.
 */
#define ACPI_MPAM_MSC_IFACE_MMIO   0x00
#define ACPI_MPAM_MSC_IFACE_PCC    0x0a

static bool _is_ppi_partition(u32 flags)
{
	u32 aff_type, is_ppi;
	bool ret;

	is_ppi = FIELD_GET(ACPI_MPAM_MSC_IRQ_AFFINITY_VALID, flags);
	if (!is_ppi)
		return false;

	aff_type = FIELD_GET(ACPI_MPAM_MSC_IRQ_AFFINITY_TYPE_MASK, flags);
	ret = (aff_type == ACPI_MPAM_MSC_IRQ_AFFINITY_TYPE_PROCESSOR_CONTAINER);
	if (ret)
		pr_err_once("Partitioned interrupts not supported\n");

	return ret;
}

static int acpi_mpam_register_irq(struct platform_device *pdev,
				  u32 intid, u32 flags)
{
	int irq;
	u32 int_type;
	int trigger;

	if (!intid)
		return -EINVAL;

	if (_is_ppi_partition(flags))
		return -EINVAL;

	trigger = FIELD_GET(ACPI_MPAM_MSC_IRQ_MODE, flags);
	int_type = FIELD_GET(ACPI_MPAM_MSC_IRQ_TYPE_MASK, flags);
	if (int_type != ACPI_MPAM_MSC_IRQ_TYPE_WIRED)
		return -EINVAL;

	irq = acpi_register_gsi(&pdev->dev, intid, trigger, ACPI_ACTIVE_HIGH);
	if (irq < 0)
		pr_err_once("Failed to register interrupt 0x%x with ACPI\n", intid);

	return irq;
}

static void acpi_mpam_parse_irqs(struct platform_device *pdev,
				 struct acpi_mpam_msc_node *tbl_msc,
				 struct resource *res, int *res_idx)
{
	u32 flags, intid;
	int irq;

	intid = tbl_msc->overflow_interrupt;
	flags = tbl_msc->overflow_interrupt_flags;
	irq = acpi_mpam_register_irq(pdev, intid, flags);
	if (irq > 0)
		res[(*res_idx)++] = DEFINE_RES_IRQ_NAMED(irq, "overflow");

	intid = tbl_msc->error_interrupt;
	flags = tbl_msc->error_interrupt_flags;
	irq = acpi_mpam_register_irq(pdev, intid, flags);
	if (irq > 0)
		res[(*res_idx)++] = DEFINE_RES_IRQ_NAMED(irq, "error");
}

static int acpi_mpam_parse_resource(struct mpam_msc *msc,
				    struct acpi_mpam_resource_node *res)
{
	int level, nid;
	u32 cache_id;

	switch (res->locator_type) {
	case ACPI_MPAM_LOCATION_TYPE_PROCESSOR_CACHE:
		cache_id = res->locator.cache_locator.cache_reference;
		level = find_acpi_cache_level_from_id(cache_id);
		if (level <= 0) {
			pr_err_once("Bad level (%d) for cache with id %u\n", level, cache_id);
			return -EINVAL;
		}
		return mpam_ris_create(msc, res->ris_index, MPAM_CLASS_CACHE,
				       level, cache_id);
	case ACPI_MPAM_LOCATION_TYPE_MEMORY:
		nid = pxm_to_node(res->locator.memory_locator.proximity_domain);
		if (nid == NUMA_NO_NODE) {
			pr_debug("Bad proximity domain %lld, using node 0 instead\n",
				 res->locator.memory_locator.proximity_domain);
			nid = 0;
		}
		return mpam_ris_create(msc, res->ris_index, MPAM_CLASS_MEMORY,
				       MPAM_CLASS_ID_DEFAULT, nid);
	default:
		/* These get discovered later and are treated as unknown */
		return 0;
	}
}

int acpi_mpam_parse_resources(struct mpam_msc *msc,
			      struct acpi_mpam_msc_node *tbl_msc)
{
	int i, err;
	char *ptr, *table_end;
	struct acpi_mpam_resource_node *resource;

	table_end = (char *)tbl_msc + tbl_msc->length;
	ptr = (char *)(tbl_msc + 1);
	for (i = 0; i < tbl_msc->num_resource_nodes; i++) {
		u64 max_deps, remaining_table;

		if (ptr + sizeof(*resource) > table_end)
			return -EINVAL;

		resource = (struct acpi_mpam_resource_node *)ptr;

		remaining_table = table_end - ptr;
		max_deps = remaining_table / sizeof(struct acpi_mpam_func_deps);
		if (resource->num_functional_deps > max_deps) {
			pr_debug("MSC has impossible number of functional dependencies\n");
			return -EINVAL;
		}

		err = acpi_mpam_parse_resource(msc, resource);
		if (err)
			return err;

		ptr += sizeof(*resource);
		ptr += resource->num_functional_deps * sizeof(struct acpi_mpam_func_deps);
	}

	return 0;
}

/*
 * Creates the device power management link and returns true if the
 * acpi id is valid and usable for cpu affinity.  This is the case
 * when the linked device is a processor or a processor container.
 */
static bool __init parse_msc_pm_link(struct acpi_mpam_msc_node *tbl_msc,
				     struct platform_device *pdev,
				     u32 *acpi_id)
{
	char hid[sizeof(tbl_msc->hardware_id_linked_device) + 1] = { 0 };
	bool acpi_id_valid = false;
	struct acpi_device *buddy;
	char uid[11];
	int len;

	memcpy(hid, &tbl_msc->hardware_id_linked_device,
	       sizeof(tbl_msc->hardware_id_linked_device));

	if (!strcmp(hid, ACPI_PROCESSOR_CONTAINER_HID)) {
		*acpi_id = tbl_msc->instance_id_linked_device;
		acpi_id_valid = true;
	}

	len = snprintf(uid, sizeof(uid), "%u",
		       tbl_msc->instance_id_linked_device);
	if (len >= sizeof(uid)) {
		pr_debug("Failed to convert uid of device for power management.");
		return acpi_id_valid;
	}

	buddy = acpi_dev_get_first_match_dev(hid, uid, -1);
	if (buddy) {
		device_link_add(&pdev->dev, &buddy->dev, DL_FLAG_STATELESS);
		acpi_dev_put(buddy);
	}

	return acpi_id_valid;
}

static int decode_interface_type(struct acpi_mpam_msc_node *tbl_msc,
				 enum mpam_msc_iface *iface)
{
	switch (tbl_msc->interface_type) {
	case ACPI_MPAM_MSC_IFACE_MMIO:
		*iface = MPAM_IFACE_MMIO;
		return 0;
	case ACPI_MPAM_MSC_IFACE_PCC:
		*iface = MPAM_IFACE_PCC;
		return 0;
	default:
		return -EINVAL;
	}
}

static struct platform_device * __init acpi_mpam_parse_msc(struct acpi_mpam_msc_node *tbl_msc)
{
	struct platform_device *pdev __free(platform_device_put) =
		platform_device_alloc("mpam_msc", tbl_msc->identifier);
	int next_res = 0, next_prop = 0, err;
	/* pcc, nrdy, affinity and a sentinel */
	struct property_entry props[4] = { 0 };
	/* mmio, 2xirq, no sentinel. */
	struct resource res[3] = { 0 };
	struct acpi_device *companion;
	enum mpam_msc_iface iface;
	char uid[16];
	u32 acpi_id;

	if (!pdev)
		return ERR_PTR(-ENOMEM);

	/* Some power management is described in the namespace: */
	err = snprintf(uid, sizeof(uid), "%u", tbl_msc->identifier);
	if (err > 0 && err < sizeof(uid)) {
		companion = acpi_dev_get_first_match_dev("ARMHAA5C", uid, -1);
		if (companion) {
			ACPI_COMPANION_SET(&pdev->dev, companion);
			acpi_dev_put(companion);
		} else {
			pr_debug("MSC.%u: missing namespace entry\n", tbl_msc->identifier);
		}
	}

	if (decode_interface_type(tbl_msc, &iface)) {
		pr_debug("MSC.%u: unknown interface type\n", tbl_msc->identifier);
		return ERR_PTR(-EINVAL);
	}

	if (iface == MPAM_IFACE_MMIO) {
		res[next_res++] = DEFINE_RES_MEM_NAMED(tbl_msc->base_address,
						       tbl_msc->mmio_size,
						       "MPAM:MSC");
	} else if (iface == MPAM_IFACE_PCC) {
		props[next_prop++] = PROPERTY_ENTRY_U32("pcc-channel",
							tbl_msc->base_address);
	}

	acpi_mpam_parse_irqs(pdev, tbl_msc, res, &next_res);

	WARN_ON_ONCE(next_res > ARRAY_SIZE(res));
	err = platform_device_add_resources(pdev, res, next_res);
	if (err)
		return ERR_PTR(err);

	props[next_prop++] = PROPERTY_ENTRY_U32("arm,not-ready-us",
						tbl_msc->max_nrdy_usec);

	/*
	 * The MSC's CPU affinity is described via its linked power
	 * management device, but only if it points at a Processor or
	 * Processor Container.
	 */
	if (parse_msc_pm_link(tbl_msc, pdev, &acpi_id))
		props[next_prop++] = PROPERTY_ENTRY_U32("cpu_affinity", acpi_id);

	WARN_ON_ONCE(next_prop > ARRAY_SIZE(props) - 1);
	err = device_create_managed_software_node(&pdev->dev, props, NULL);
	if (err)
		return ERR_PTR(err);

	/*
	 * Stash the table entry for acpi_mpam_parse_resources() to discover
	 * what this MSC controls.
	 */
	err = platform_device_add_data(pdev, tbl_msc, tbl_msc->length);
	if (err)
		return ERR_PTR(err);

	err = platform_device_add(pdev);
	if (err)
		return ERR_PTR(err);

	return_ptr(pdev);
}

static int __init acpi_mpam_parse(void)
{
	char *table_end, *table_offset;
	struct acpi_mpam_msc_node *tbl_msc;
	struct platform_device *pdev;

	if (acpi_disabled || !system_supports_mpam())
		return 0;

	struct acpi_table_header *table __free(acpi_put_table) =
		acpi_get_table_pointer(ACPI_SIG_MPAM, 0);

	if (IS_ERR(table))
		return 0;

	if (table->revision < 1) {
		pr_debug("MPAM ACPI table revision %d not supported\n", table->revision);
		return 0;
	}

	table_offset = (char *)(table + 1);
	table_end = (char *)table + table->length;

	while (table_offset < table_end) {
		tbl_msc = (struct acpi_mpam_msc_node *)table_offset;
		if (table_offset + sizeof(*tbl_msc) > table_end ||
		    table_offset + tbl_msc->length > table_end) {
			pr_err("MSC entry overlaps end of ACPI table\n");
			return -EINVAL;
		}
		table_offset += tbl_msc->length;

		/*
		 * If any of the reserved fields are set, make no attempt to
		 * parse the MSC structure. This MSC will still be counted by
		 * acpi_mpam_count_msc(), meaning the MPAM driver can't probe
		 * against all MSC, and will never be enabled. There is no way
		 * to enable it safely, because we cannot determine safe
		 * system-wide partid and pmg ranges in this situation.
		 */
		if (tbl_msc->reserved || tbl_msc->reserved1 || tbl_msc->reserved2) {
			pr_err_once("Unrecognised MSC, MPAM not usable\n");
			pr_debug("MSC.%u: reserved field set\n", tbl_msc->identifier);
			continue;
		}

		if (!tbl_msc->mmio_size) {
			pr_debug("MSC.%u: marked as disabled\n", tbl_msc->identifier);
			continue;
		}

		pdev = acpi_mpam_parse_msc(tbl_msc);
		if (IS_ERR(pdev))
			return PTR_ERR(pdev);
	}

	return 0;
}

/**
 * acpi_mpam_count_msc() - Count the number of MSC described by firmware.
 *
 * Returns the number of MSCs, or zero for an error.
 *
 * This can be called before or in parallel with acpi_mpam_parse().
 */
int acpi_mpam_count_msc(void)
{
	char *table_end, *table_offset;
	struct acpi_mpam_msc_node *tbl_msc;
	int count = 0;

	if (acpi_disabled || !system_supports_mpam())
		return 0;

	struct acpi_table_header *table __free(acpi_put_table) =
		acpi_get_table_pointer(ACPI_SIG_MPAM, 0);

	if (IS_ERR(table))
		return 0;

	if (table->revision < 1)
		return 0;

	table_offset = (char *)(table + 1);
	table_end = (char *)table + table->length;

	while (table_offset < table_end) {
		tbl_msc = (struct acpi_mpam_msc_node *)table_offset;

		if (table_offset + sizeof(*tbl_msc) > table_end)
			return -EINVAL;
		if (tbl_msc->length < sizeof(*tbl_msc))
			return -EINVAL;
		if (tbl_msc->length > table_end - table_offset)
			return -EINVAL;
		table_offset += tbl_msc->length;

		if (!tbl_msc->mmio_size)
			continue;

		count++;
	}

	return count;
}

/*
 * Call after ACPI devices have been created, which happens behind acpi_scan_init()
 * called from subsys_initcall(). PCC requires the mailbox driver, which is
 * initialised from postcore_initcall().
 */
subsys_initcall_sync(acpi_mpam_parse);
