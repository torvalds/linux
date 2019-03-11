// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019, Intel Corporation.
 *
 * Heterogeneous Memory Attributes Table (HMAT) representation
 *
 * This program parses and reports the platform's HMAT tables, and registers
 * the applicable attributes with the node's interfaces.
 */

#include <linux/acpi.h>
#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/node.h>
#include <linux/sysfs.h>

static __initdata u8 hmat_revision;

static __init const char *hmat_data_type(u8 type)
{
	switch (type) {
	case ACPI_HMAT_ACCESS_LATENCY:
		return "Access Latency";
	case ACPI_HMAT_READ_LATENCY:
		return "Read Latency";
	case ACPI_HMAT_WRITE_LATENCY:
		return "Write Latency";
	case ACPI_HMAT_ACCESS_BANDWIDTH:
		return "Access Bandwidth";
	case ACPI_HMAT_READ_BANDWIDTH:
		return "Read Bandwidth";
	case ACPI_HMAT_WRITE_BANDWIDTH:
		return "Write Bandwidth";
	default:
		return "Reserved";
	}
}

static __init const char *hmat_data_type_suffix(u8 type)
{
	switch (type) {
	case ACPI_HMAT_ACCESS_LATENCY:
	case ACPI_HMAT_READ_LATENCY:
	case ACPI_HMAT_WRITE_LATENCY:
		return " nsec";
	case ACPI_HMAT_ACCESS_BANDWIDTH:
	case ACPI_HMAT_READ_BANDWIDTH:
	case ACPI_HMAT_WRITE_BANDWIDTH:
		return " MB/s";
	default:
		return "";
	}
}

static __init u32 hmat_normalize(u16 entry, u64 base, u8 type)
{
	u32 value;

	/*
	 * Check for invalid and overflow values
	 */
	if (entry == 0xffff || !entry)
		return 0;
	else if (base > (UINT_MAX / (entry)))
		return 0;

	/*
	 * Divide by the base unit for version 1, convert latency from
	 * picosenonds to nanoseconds if revision 2.
	 */
	value = entry * base;
	if (hmat_revision == 1) {
		if (value < 10)
			return 0;
		value = DIV_ROUND_UP(value, 10);
	} else if (hmat_revision == 2) {
		switch (type) {
		case ACPI_HMAT_ACCESS_LATENCY:
		case ACPI_HMAT_READ_LATENCY:
		case ACPI_HMAT_WRITE_LATENCY:
			value = DIV_ROUND_UP(value, 1000);
			break;
		default:
			break;
		}
	}
	return value;
}

static __init int hmat_parse_locality(union acpi_subtable_headers *header,
				      const unsigned long end)
{
	struct acpi_hmat_locality *hmat_loc = (void *)header;
	unsigned int init, targ, total_size, ipds, tpds;
	u32 *inits, *targs, value;
	u16 *entries;
	u8 type;

	if (hmat_loc->header.length < sizeof(*hmat_loc)) {
		pr_notice("HMAT: Unexpected locality header length: %d\n",
			 hmat_loc->header.length);
		return -EINVAL;
	}

	type = hmat_loc->data_type;
	ipds = hmat_loc->number_of_initiator_Pds;
	tpds = hmat_loc->number_of_target_Pds;
	total_size = sizeof(*hmat_loc) + sizeof(*entries) * ipds * tpds +
		     sizeof(*inits) * ipds + sizeof(*targs) * tpds;
	if (hmat_loc->header.length < total_size) {
		pr_notice("HMAT: Unexpected locality header length:%d, minimum required:%d\n",
			 hmat_loc->header.length, total_size);
		return -EINVAL;
	}

	pr_info("HMAT: Locality: Flags:%02x Type:%s Initiator Domains:%d Target Domains:%d Base:%lld\n",
		hmat_loc->flags, hmat_data_type(type), ipds, tpds,
		hmat_loc->entry_base_unit);

	inits = (u32 *)(hmat_loc + 1);
	targs = inits + ipds;
	entries = (u16 *)(targs + tpds);
	for (init = 0; init < ipds; init++) {
		for (targ = 0; targ < tpds; targ++) {
			value = hmat_normalize(entries[init * tpds + targ],
					       hmat_loc->entry_base_unit,
					       type);
			pr_info("  Initiator-Target[%d-%d]:%d%s\n",
				inits[init], targs[targ], value,
				hmat_data_type_suffix(type));
		}
	}

	return 0;
}

static __init int hmat_parse_cache(union acpi_subtable_headers *header,
				   const unsigned long end)
{
	struct acpi_hmat_cache *cache = (void *)header;
	u32 attrs;

	if (cache->header.length < sizeof(*cache)) {
		pr_notice("HMAT: Unexpected cache header length: %d\n",
			 cache->header.length);
		return -EINVAL;
	}

	attrs = cache->cache_attributes;
	pr_info("HMAT: Cache: Domain:%d Size:%llu Attrs:%08x SMBIOS Handles:%d\n",
		cache->memory_PD, cache->cache_size, attrs,
		cache->number_of_SMBIOShandles);

	return 0;
}

static int __init hmat_parse_proximity_domain(union acpi_subtable_headers *header,
					      const unsigned long end)
{
	struct acpi_hmat_proximity_domain *p = (void *)header;

	if (p->header.length != sizeof(*p)) {
		pr_notice("HMAT: Unexpected address range header length: %d\n",
			 p->header.length);
		return -EINVAL;
	}

	if (hmat_revision == 1)
		pr_info("HMAT: Memory (%#llx length %#llx) Flags:%04x Processor Domain:%d Memory Domain:%d\n",
			p->reserved3, p->reserved4, p->flags, p->processor_PD,
			p->memory_PD);
	else
		pr_info("HMAT: Memory Flags:%04x Processor Domain:%d Memory Domain:%d\n",
			p->flags, p->processor_PD, p->memory_PD);

	return 0;
}

static int __init hmat_parse_subtable(union acpi_subtable_headers *header,
				      const unsigned long end)
{
	struct acpi_hmat_structure *hdr = (void *)header;

	if (!hdr)
		return -EINVAL;

	switch (hdr->type) {
	case ACPI_HMAT_TYPE_ADDRESS_RANGE:
		return hmat_parse_proximity_domain(header, end);
	case ACPI_HMAT_TYPE_LOCALITY:
		return hmat_parse_locality(header, end);
	case ACPI_HMAT_TYPE_CACHE:
		return hmat_parse_cache(header, end);
	default:
		return -EINVAL;
	}
}

static __init int hmat_init(void)
{
	struct acpi_table_header *tbl;
	enum acpi_hmat_type i;
	acpi_status status;

	if (srat_disabled())
		return 0;

	status = acpi_get_table(ACPI_SIG_HMAT, 0, &tbl);
	if (ACPI_FAILURE(status))
		return 0;

	hmat_revision = tbl->revision;
	switch (hmat_revision) {
	case 1:
	case 2:
		break;
	default:
		pr_notice("Ignoring HMAT: Unknown revision:%d\n", hmat_revision);
		goto out_put;
	}

	for (i = ACPI_HMAT_TYPE_ADDRESS_RANGE; i < ACPI_HMAT_TYPE_RESERVED; i++) {
		if (acpi_table_parse_entries(ACPI_SIG_HMAT,
					     sizeof(struct acpi_table_hmat), i,
					     hmat_parse_subtable, 0) < 0) {
			pr_notice("Ignoring HMAT: Invalid table");
			goto out_put;
		}
	}
out_put:
	acpi_put_table(tbl);
	return 0;
}
subsys_initcall(hmat_init);
