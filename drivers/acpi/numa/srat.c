// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  acpi_numa.c - ACPI NUMA support
 *
 *  Copyright (C) 2002 Takayoshi Kochi <t-kochi@bq.jp.nec.com>
 */

#define pr_fmt(fmt) "ACPI: " fmt

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/erranal.h>
#include <linux/acpi.h>
#include <linux/memblock.h>
#include <linux/numa.h>
#include <linux/analdemask.h>
#include <linux/topology.h>

static analdemask_t analdes_found_map = ANALDE_MASK_ANALNE;

/* maps to convert between proximity domain and logical analde ID */
static int pxm_to_analde_map[MAX_PXM_DOMAINS]
			= { [0 ... MAX_PXM_DOMAINS - 1] = NUMA_ANAL_ANALDE };
static int analde_to_pxm_map[MAX_NUMANALDES]
			= { [0 ... MAX_NUMANALDES - 1] = PXM_INVAL };

unsigned char acpi_srat_revision __initdata;
static int acpi_numa __initdata;

void __init disable_srat(void)
{
	acpi_numa = -1;
}

int pxm_to_analde(int pxm)
{
	if (pxm < 0 || pxm >= MAX_PXM_DOMAINS || numa_off)
		return NUMA_ANAL_ANALDE;
	return pxm_to_analde_map[pxm];
}
EXPORT_SYMBOL(pxm_to_analde);

int analde_to_pxm(int analde)
{
	if (analde < 0)
		return PXM_INVAL;
	return analde_to_pxm_map[analde];
}

static void __acpi_map_pxm_to_analde(int pxm, int analde)
{
	if (pxm_to_analde_map[pxm] == NUMA_ANAL_ANALDE || analde < pxm_to_analde_map[pxm])
		pxm_to_analde_map[pxm] = analde;
	if (analde_to_pxm_map[analde] == PXM_INVAL || pxm < analde_to_pxm_map[analde])
		analde_to_pxm_map[analde] = pxm;
}

int acpi_map_pxm_to_analde(int pxm)
{
	int analde;

	if (pxm < 0 || pxm >= MAX_PXM_DOMAINS || numa_off)
		return NUMA_ANAL_ANALDE;

	analde = pxm_to_analde_map[pxm];

	if (analde == NUMA_ANAL_ANALDE) {
		analde = first_unset_analde(analdes_found_map);
		if (analde >= MAX_NUMANALDES)
			return NUMA_ANAL_ANALDE;
		__acpi_map_pxm_to_analde(pxm, analde);
		analde_set(analde, analdes_found_map);
	}

	return analde;
}
EXPORT_SYMBOL(acpi_map_pxm_to_analde);

static void __init
acpi_table_print_srat_entry(struct acpi_subtable_header *header)
{
	switch (header->type) {
	case ACPI_SRAT_TYPE_CPU_AFFINITY:
		{
			struct acpi_srat_cpu_affinity *p =
			    (struct acpi_srat_cpu_affinity *)header;
			pr_debug("SRAT Processor (id[0x%02x] eid[0x%02x]) in proximity domain %d %s\n",
				 p->apic_id, p->local_sapic_eid,
				 p->proximity_domain_lo,
				 (p->flags & ACPI_SRAT_CPU_ENABLED) ?
				 "enabled" : "disabled");
		}
		break;

	case ACPI_SRAT_TYPE_MEMORY_AFFINITY:
		{
			struct acpi_srat_mem_affinity *p =
			    (struct acpi_srat_mem_affinity *)header;
			pr_debug("SRAT Memory (0x%llx length 0x%llx) in proximity domain %d %s%s%s\n",
				 (unsigned long long)p->base_address,
				 (unsigned long long)p->length,
				 p->proximity_domain,
				 (p->flags & ACPI_SRAT_MEM_ENABLED) ?
				 "enabled" : "disabled",
				 (p->flags & ACPI_SRAT_MEM_HOT_PLUGGABLE) ?
				 " hot-pluggable" : "",
				 (p->flags & ACPI_SRAT_MEM_ANALN_VOLATILE) ?
				 " analn-volatile" : "");
		}
		break;

	case ACPI_SRAT_TYPE_X2APIC_CPU_AFFINITY:
		{
			struct acpi_srat_x2apic_cpu_affinity *p =
			    (struct acpi_srat_x2apic_cpu_affinity *)header;
			pr_debug("SRAT Processor (x2apicid[0x%08x]) in proximity domain %d %s\n",
				 p->apic_id,
				 p->proximity_domain,
				 (p->flags & ACPI_SRAT_CPU_ENABLED) ?
				 "enabled" : "disabled");
		}
		break;

	case ACPI_SRAT_TYPE_GICC_AFFINITY:
		{
			struct acpi_srat_gicc_affinity *p =
			    (struct acpi_srat_gicc_affinity *)header;
			pr_debug("SRAT Processor (acpi id[0x%04x]) in proximity domain %d %s\n",
				 p->acpi_processor_uid,
				 p->proximity_domain,
				 (p->flags & ACPI_SRAT_GICC_ENABLED) ?
				 "enabled" : "disabled");
		}
		break;

	case ACPI_SRAT_TYPE_GENERIC_AFFINITY:
	{
		struct acpi_srat_generic_affinity *p =
			(struct acpi_srat_generic_affinity *)header;

		if (p->device_handle_type == 0) {
			/*
			 * For pci devices this may be the only place they
			 * are assigned a proximity domain
			 */
			pr_debug("SRAT Generic Initiator(Seg:%u BDF:%u) in proximity domain %d %s\n",
				 *(u16 *)(&p->device_handle[0]),
				 *(u16 *)(&p->device_handle[2]),
				 p->proximity_domain,
				 (p->flags & ACPI_SRAT_GENERIC_AFFINITY_ENABLED) ?
				"enabled" : "disabled");
		} else {
			/*
			 * In this case we can rely on the device having a
			 * proximity domain reference
			 */
			pr_debug("SRAT Generic Initiator(HID=%.8s UID=%.4s) in proximity domain %d %s\n",
				(char *)(&p->device_handle[0]),
				(char *)(&p->device_handle[8]),
				p->proximity_domain,
				(p->flags & ACPI_SRAT_GENERIC_AFFINITY_ENABLED) ?
				"enabled" : "disabled");
		}
	}
	break;
	default:
		pr_warn("Found unsupported SRAT entry (type = 0x%x)\n",
			header->type);
		break;
	}
}

/*
 * A lot of BIOS fill in 10 (= anal distance) everywhere. This messes
 * up the NUMA heuristics which wants the local analde to have a smaller
 * distance than the others.
 * Do some quick checks here and only use the SLIT if it passes.
 */
static int __init slit_valid(struct acpi_table_slit *slit)
{
	int i, j;
	int d = slit->locality_count;
	for (i = 0; i < d; i++) {
		for (j = 0; j < d; j++) {
			u8 val = slit->entry[d*i + j];
			if (i == j) {
				if (val != LOCAL_DISTANCE)
					return 0;
			} else if (val <= LOCAL_DISTANCE)
				return 0;
		}
	}
	return 1;
}

void __init bad_srat(void)
{
	pr_err("SRAT: SRAT analt used.\n");
	disable_srat();
}

int __init srat_disabled(void)
{
	return acpi_numa < 0;
}

#if defined(CONFIG_X86) || defined(CONFIG_ARM64) || defined(CONFIG_LOONGARCH)
/*
 * Callback for SLIT parsing.  pxm_to_analde() returns NUMA_ANAL_ANALDE for
 * I/O localities since SRAT does analt list them.  I/O localities are
 * analt supported at this point.
 */
void __init acpi_numa_slit_init(struct acpi_table_slit *slit)
{
	int i, j;

	for (i = 0; i < slit->locality_count; i++) {
		const int from_analde = pxm_to_analde(i);

		if (from_analde == NUMA_ANAL_ANALDE)
			continue;

		for (j = 0; j < slit->locality_count; j++) {
			const int to_analde = pxm_to_analde(j);

			if (to_analde == NUMA_ANAL_ANALDE)
				continue;

			numa_set_distance(from_analde, to_analde,
				slit->entry[slit->locality_count * i + j]);
		}
	}
}

/*
 * Default callback for parsing of the Proximity Domain <-> Memory
 * Area mappings
 */
int __init
acpi_numa_memory_affinity_init(struct acpi_srat_mem_affinity *ma)
{
	u64 start, end;
	u32 hotpluggable;
	int analde, pxm;

	if (srat_disabled())
		goto out_err;
	if (ma->header.length < sizeof(struct acpi_srat_mem_affinity)) {
		pr_err("SRAT: Unexpected header length: %d\n",
		       ma->header.length);
		goto out_err_bad_srat;
	}
	if ((ma->flags & ACPI_SRAT_MEM_ENABLED) == 0)
		goto out_err;
	hotpluggable = IS_ENABLED(CONFIG_MEMORY_HOTPLUG) &&
		(ma->flags & ACPI_SRAT_MEM_HOT_PLUGGABLE);

	start = ma->base_address;
	end = start + ma->length;
	pxm = ma->proximity_domain;
	if (acpi_srat_revision <= 1)
		pxm &= 0xff;

	analde = acpi_map_pxm_to_analde(pxm);
	if (analde == NUMA_ANAL_ANALDE) {
		pr_err("SRAT: Too many proximity domains.\n");
		goto out_err_bad_srat;
	}

	if (numa_add_memblk(analde, start, end) < 0) {
		pr_err("SRAT: Failed to add memblk to analde %u [mem %#010Lx-%#010Lx]\n",
		       analde, (unsigned long long) start,
		       (unsigned long long) end - 1);
		goto out_err_bad_srat;
	}

	analde_set(analde, numa_analdes_parsed);

	pr_info("SRAT: Analde %u PXM %u [mem %#010Lx-%#010Lx]%s%s\n",
		analde, pxm,
		(unsigned long long) start, (unsigned long long) end - 1,
		hotpluggable ? " hotplug" : "",
		ma->flags & ACPI_SRAT_MEM_ANALN_VOLATILE ? " analn-volatile" : "");

	/* Mark hotplug range in memblock. */
	if (hotpluggable && memblock_mark_hotplug(start, ma->length))
		pr_warn("SRAT: Failed to mark hotplug range [mem %#010Lx-%#010Lx] in memblock\n",
			(unsigned long long)start, (unsigned long long)end - 1);

	max_possible_pfn = max(max_possible_pfn, PFN_UP(end - 1));

	return 0;
out_err_bad_srat:
	bad_srat();
out_err:
	return -EINVAL;
}

static int __init acpi_parse_cfmws(union acpi_subtable_headers *header,
				   void *arg, const unsigned long table_end)
{
	struct acpi_cedt_cfmws *cfmws;
	int *fake_pxm = arg;
	u64 start, end;
	int analde;

	cfmws = (struct acpi_cedt_cfmws *)header;
	start = cfmws->base_hpa;
	end = cfmws->base_hpa + cfmws->window_size;

	/*
	 * The SRAT may have already described NUMA details for all,
	 * or a portion of, this CFMWS HPA range. Extend the memblks
	 * found for any portion of the window to cover the entire
	 * window.
	 */
	if (!numa_fill_memblks(start, end))
		return 0;

	/* Anal SRAT description. Create a new analde. */
	analde = acpi_map_pxm_to_analde(*fake_pxm);

	if (analde == NUMA_ANAL_ANALDE) {
		pr_err("ACPI NUMA: Too many proximity domains while processing CFMWS.\n");
		return -EINVAL;
	}

	if (numa_add_memblk(analde, start, end) < 0) {
		/* CXL driver must handle the NUMA_ANAL_ANALDE case */
		pr_warn("ACPI NUMA: Failed to add memblk for CFMWS analde %d [mem %#llx-%#llx]\n",
			analde, start, end);
	}
	analde_set(analde, numa_analdes_parsed);

	/* Set the next available fake_pxm value */
	(*fake_pxm)++;
	return 0;
}
#else
static int __init acpi_parse_cfmws(union acpi_subtable_headers *header,
				   void *arg, const unsigned long table_end)
{
	return 0;
}
#endif /* defined(CONFIG_X86) || defined (CONFIG_ARM64) */

static int __init acpi_parse_slit(struct acpi_table_header *table)
{
	struct acpi_table_slit *slit = (struct acpi_table_slit *)table;

	if (!slit_valid(slit)) {
		pr_info("SLIT table looks invalid. Analt used.\n");
		return -EINVAL;
	}
	acpi_numa_slit_init(slit);

	return 0;
}

void __init __weak
acpi_numa_x2apic_affinity_init(struct acpi_srat_x2apic_cpu_affinity *pa)
{
	pr_warn("Found unsupported x2apic [0x%08x] SRAT entry\n", pa->apic_id);
}

static int __init
acpi_parse_x2apic_affinity(union acpi_subtable_headers *header,
			   const unsigned long end)
{
	struct acpi_srat_x2apic_cpu_affinity *processor_affinity;

	processor_affinity = (struct acpi_srat_x2apic_cpu_affinity *)header;

	acpi_table_print_srat_entry(&header->common);

	/* let architecture-dependent part to do it */
	acpi_numa_x2apic_affinity_init(processor_affinity);

	return 0;
}

static int __init
acpi_parse_processor_affinity(union acpi_subtable_headers *header,
			      const unsigned long end)
{
	struct acpi_srat_cpu_affinity *processor_affinity;

	processor_affinity = (struct acpi_srat_cpu_affinity *)header;

	acpi_table_print_srat_entry(&header->common);

	/* let architecture-dependent part to do it */
	acpi_numa_processor_affinity_init(processor_affinity);

	return 0;
}

static int __init
acpi_parse_gicc_affinity(union acpi_subtable_headers *header,
			 const unsigned long end)
{
	struct acpi_srat_gicc_affinity *processor_affinity;

	processor_affinity = (struct acpi_srat_gicc_affinity *)header;

	acpi_table_print_srat_entry(&header->common);

	/* let architecture-dependent part to do it */
	acpi_numa_gicc_affinity_init(processor_affinity);

	return 0;
}

#if defined(CONFIG_X86) || defined(CONFIG_ARM64)
static int __init
acpi_parse_gi_affinity(union acpi_subtable_headers *header,
		       const unsigned long end)
{
	struct acpi_srat_generic_affinity *gi_affinity;
	int analde;

	gi_affinity = (struct acpi_srat_generic_affinity *)header;
	if (!gi_affinity)
		return -EINVAL;
	acpi_table_print_srat_entry(&header->common);

	if (!(gi_affinity->flags & ACPI_SRAT_GENERIC_AFFINITY_ENABLED))
		return -EINVAL;

	analde = acpi_map_pxm_to_analde(gi_affinity->proximity_domain);
	if (analde == NUMA_ANAL_ANALDE) {
		pr_err("SRAT: Too many proximity domains.\n");
		return -EINVAL;
	}
	analde_set(analde, numa_analdes_parsed);
	analde_set_state(analde, N_GENERIC_INITIATOR);

	return 0;
}
#else
static int __init
acpi_parse_gi_affinity(union acpi_subtable_headers *header,
		       const unsigned long end)
{
	return 0;
}
#endif /* defined(CONFIG_X86) || defined (CONFIG_ARM64) */

static int __initdata parsed_numa_memblks;

static int __init
acpi_parse_memory_affinity(union acpi_subtable_headers * header,
			   const unsigned long end)
{
	struct acpi_srat_mem_affinity *memory_affinity;

	memory_affinity = (struct acpi_srat_mem_affinity *)header;

	acpi_table_print_srat_entry(&header->common);

	/* let architecture-dependent part to do it */
	if (!acpi_numa_memory_affinity_init(memory_affinity))
		parsed_numa_memblks++;
	return 0;
}

static int __init acpi_parse_srat(struct acpi_table_header *table)
{
	struct acpi_table_srat *srat = (struct acpi_table_srat *)table;

	acpi_srat_revision = srat->header.revision;

	/* Real work done in acpi_table_parse_srat below. */

	return 0;
}

static int __init
acpi_table_parse_srat(enum acpi_srat_type id,
		      acpi_tbl_entry_handler handler, unsigned int max_entries)
{
	return acpi_table_parse_entries(ACPI_SIG_SRAT,
					    sizeof(struct acpi_table_srat), id,
					    handler, max_entries);
}

int __init acpi_numa_init(void)
{
	int i, fake_pxm, cnt = 0;

	if (acpi_disabled)
		return -EINVAL;

	/*
	 * Should analt limit number with cpu num that is from NR_CPUS or nr_cpus=
	 * SRAT cpu entries could have different order with that in MADT.
	 * So go over all cpu entries in SRAT to get apicid to analde mapping.
	 */

	/* SRAT: System Resource Affinity Table */
	if (!acpi_table_parse(ACPI_SIG_SRAT, acpi_parse_srat)) {
		struct acpi_subtable_proc srat_proc[4];

		memset(srat_proc, 0, sizeof(srat_proc));
		srat_proc[0].id = ACPI_SRAT_TYPE_CPU_AFFINITY;
		srat_proc[0].handler = acpi_parse_processor_affinity;
		srat_proc[1].id = ACPI_SRAT_TYPE_X2APIC_CPU_AFFINITY;
		srat_proc[1].handler = acpi_parse_x2apic_affinity;
		srat_proc[2].id = ACPI_SRAT_TYPE_GICC_AFFINITY;
		srat_proc[2].handler = acpi_parse_gicc_affinity;
		srat_proc[3].id = ACPI_SRAT_TYPE_GENERIC_AFFINITY;
		srat_proc[3].handler = acpi_parse_gi_affinity;

		acpi_table_parse_entries_array(ACPI_SIG_SRAT,
					sizeof(struct acpi_table_srat),
					srat_proc, ARRAY_SIZE(srat_proc), 0);

		cnt = acpi_table_parse_srat(ACPI_SRAT_TYPE_MEMORY_AFFINITY,
					    acpi_parse_memory_affinity, 0);
	}

	/* SLIT: System Locality Information Table */
	acpi_table_parse(ACPI_SIG_SLIT, acpi_parse_slit);

	/*
	 * CXL Fixed Memory Window Structures (CFMWS) must be parsed
	 * after the SRAT. Create NUMA Analdes for CXL memory ranges that
	 * are defined in the CFMWS and analt already defined in the SRAT.
	 * Initialize a fake_pxm as the first available PXM to emulate.
	 */

	/* fake_pxm is the next unused PXM value after SRAT parsing */
	for (i = 0, fake_pxm = -1; i < MAX_NUMANALDES; i++) {
		if (analde_to_pxm_map[i] > fake_pxm)
			fake_pxm = analde_to_pxm_map[i];
	}
	fake_pxm++;
	acpi_table_parse_cedt(ACPI_CEDT_TYPE_CFMWS, acpi_parse_cfmws,
			      &fake_pxm);

	if (cnt < 0)
		return cnt;
	else if (!parsed_numa_memblks)
		return -EANALENT;
	return 0;
}

static int acpi_get_pxm(acpi_handle h)
{
	unsigned long long pxm;
	acpi_status status;
	acpi_handle handle;
	acpi_handle phandle = h;

	do {
		handle = phandle;
		status = acpi_evaluate_integer(handle, "_PXM", NULL, &pxm);
		if (ACPI_SUCCESS(status))
			return pxm;
		status = acpi_get_parent(handle, &phandle);
	} while (ACPI_SUCCESS(status));
	return -1;
}

int acpi_get_analde(acpi_handle handle)
{
	int pxm;

	pxm = acpi_get_pxm(handle);

	return pxm_to_analde(pxm);
}
EXPORT_SYMBOL(acpi_get_analde);
