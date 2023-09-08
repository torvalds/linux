// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  acpi.c - Architecture-Specific Low-Level ACPI Support
 *
 *  Copyright (C) 1999 VA Linux Systems
 *  Copyright (C) 1999,2000 Walt Drummond <drummond@valinux.com>
 *  Copyright (C) 2000, 2002-2003 Hewlett-Packard Co.
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 *  Copyright (C) 2000 Intel Corp.
 *  Copyright (C) 2000,2001 J.I. Lee <jung-ik.lee@intel.com>
 *  Copyright (C) 2001 Paul Diefenbaugh <paul.s.diefenbaugh@intel.com>
 *  Copyright (C) 2001 Jenna Hall <jenna.s.hall@intel.com>
 *  Copyright (C) 2001 Takayoshi Kochi <t-kochi@bq.jp.nec.com>
 *  Copyright (C) 2002 Erich Focht <efocht@ess.nec.de>
 *  Copyright (C) 2004 Ashok Raj <ashok.raj@intel.com>
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/irq.h>
#include <linux/acpi.h>
#include <linux/efi.h>
#include <linux/mmzone.h>
#include <linux/nodemask.h>
#include <linux/slab.h>
#include <acpi/processor.h>
#include <asm/io.h>
#include <asm/iosapic.h>
#include <asm/page.h>
#include <asm/numa.h>
#include <asm/sal.h>
#include <asm/cyclone.h>

#define PREFIX			"ACPI: "

int acpi_lapic;
unsigned int acpi_cpei_override;
unsigned int acpi_cpei_phys_cpuid;

#define ACPI_MAX_PLATFORM_INTERRUPTS	256

/* Array to record platform interrupt vectors for generic interrupt routing. */
int platform_intr_list[ACPI_MAX_PLATFORM_INTERRUPTS] = {
	[0 ... ACPI_MAX_PLATFORM_INTERRUPTS - 1] = -1
};

enum acpi_irq_model_id acpi_irq_model = ACPI_IRQ_MODEL_IOSAPIC;

/*
 * Interrupt routing API for device drivers.  Provides interrupt vector for
 * a generic platform event.  Currently only CPEI is implemented.
 */
int acpi_request_vector(u32 int_type)
{
	int vector = -1;

	if (int_type < ACPI_MAX_PLATFORM_INTERRUPTS) {
		/* corrected platform error interrupt */
		vector = platform_intr_list[int_type];
	} else
		printk(KERN_ERR
		       "acpi_request_vector(): invalid interrupt type\n");
	return vector;
}

void __init __iomem *__acpi_map_table(unsigned long phys, unsigned long size)
{
	return __va(phys);
}

void __init __acpi_unmap_table(void __iomem *map, unsigned long size)
{
}

/* --------------------------------------------------------------------------
                            Boot-time Table Parsing
   -------------------------------------------------------------------------- */

static int available_cpus __initdata;
struct acpi_table_madt *acpi_madt __initdata;
static u8 has_8259;

static int __init
acpi_parse_lapic_addr_ovr(union acpi_subtable_headers * header,
			  const unsigned long end)
{
	struct acpi_madt_local_apic_override *lapic;

	lapic = (struct acpi_madt_local_apic_override *)header;

	if (BAD_MADT_ENTRY(lapic, end))
		return -EINVAL;

	if (lapic->address) {
		iounmap(ipi_base_addr);
		ipi_base_addr = ioremap(lapic->address, 0);
	}
	return 0;
}

static int __init
acpi_parse_lsapic(union acpi_subtable_headers *header, const unsigned long end)
{
	struct acpi_madt_local_sapic *lsapic;

	lsapic = (struct acpi_madt_local_sapic *)header;

	/*Skip BAD_MADT_ENTRY check, as lsapic size could vary */

	if (lsapic->lapic_flags & ACPI_MADT_ENABLED) {
#ifdef CONFIG_SMP
		smp_boot_data.cpu_phys_id[available_cpus] =
		    (lsapic->id << 8) | lsapic->eid;
#endif
		++available_cpus;
	}

	total_cpus++;
	return 0;
}

static int __init
acpi_parse_lapic_nmi(union acpi_subtable_headers * header, const unsigned long end)
{
	struct acpi_madt_local_apic_nmi *lacpi_nmi;

	lacpi_nmi = (struct acpi_madt_local_apic_nmi *)header;

	if (BAD_MADT_ENTRY(lacpi_nmi, end))
		return -EINVAL;

	/* TBD: Support lapic_nmi entries */
	return 0;
}

static int __init
acpi_parse_iosapic(union acpi_subtable_headers * header, const unsigned long end)
{
	struct acpi_madt_io_sapic *iosapic;

	iosapic = (struct acpi_madt_io_sapic *)header;

	if (BAD_MADT_ENTRY(iosapic, end))
		return -EINVAL;

	return iosapic_init(iosapic->address, iosapic->global_irq_base);
}

static unsigned int __initdata acpi_madt_rev;

static int __init
acpi_parse_plat_int_src(union acpi_subtable_headers * header,
			const unsigned long end)
{
	struct acpi_madt_interrupt_source *plintsrc;
	int vector;

	plintsrc = (struct acpi_madt_interrupt_source *)header;

	if (BAD_MADT_ENTRY(plintsrc, end))
		return -EINVAL;

	/*
	 * Get vector assignment for this interrupt, set attributes,
	 * and program the IOSAPIC routing table.
	 */
	vector = iosapic_register_platform_intr(plintsrc->type,
						plintsrc->global_irq,
						plintsrc->io_sapic_vector,
						plintsrc->eid,
						plintsrc->id,
						((plintsrc->inti_flags & ACPI_MADT_POLARITY_MASK) ==
						 ACPI_MADT_POLARITY_ACTIVE_HIGH) ?
						IOSAPIC_POL_HIGH : IOSAPIC_POL_LOW,
						((plintsrc->inti_flags & ACPI_MADT_TRIGGER_MASK) ==
						 ACPI_MADT_TRIGGER_EDGE) ?
						IOSAPIC_EDGE : IOSAPIC_LEVEL);

	platform_intr_list[plintsrc->type] = vector;
	if (acpi_madt_rev > 1) {
		acpi_cpei_override = plintsrc->flags & ACPI_MADT_CPEI_OVERRIDE;
	}

	/*
	 * Save the physical id, so we can check when its being removed
	 */
	acpi_cpei_phys_cpuid = ((plintsrc->id << 8) | (plintsrc->eid)) & 0xffff;

	return 0;
}

#ifdef CONFIG_HOTPLUG_CPU
unsigned int can_cpei_retarget(void)
{
	extern int cpe_vector;
	extern unsigned int force_cpei_retarget;

	/*
	 * Only if CPEI is supported and the override flag
	 * is present, otherwise return that its re-targettable
	 * if we are in polling mode.
	 */
	if (cpe_vector > 0) {
		if (acpi_cpei_override || force_cpei_retarget)
			return 1;
		else
			return 0;
	}
	return 1;
}

unsigned int is_cpu_cpei_target(unsigned int cpu)
{
	unsigned int logical_id;

	logical_id = cpu_logical_id(acpi_cpei_phys_cpuid);

	if (logical_id == cpu)
		return 1;
	else
		return 0;
}

void set_cpei_target_cpu(unsigned int cpu)
{
	acpi_cpei_phys_cpuid = cpu_physical_id(cpu);
}
#endif

unsigned int get_cpei_target_cpu(void)
{
	return acpi_cpei_phys_cpuid;
}

static int __init
acpi_parse_int_src_ovr(union acpi_subtable_headers * header,
		       const unsigned long end)
{
	struct acpi_madt_interrupt_override *p;

	p = (struct acpi_madt_interrupt_override *)header;

	if (BAD_MADT_ENTRY(p, end))
		return -EINVAL;

	iosapic_override_isa_irq(p->source_irq, p->global_irq,
				 ((p->inti_flags & ACPI_MADT_POLARITY_MASK) ==
				  ACPI_MADT_POLARITY_ACTIVE_LOW) ?
				 IOSAPIC_POL_LOW : IOSAPIC_POL_HIGH,
				 ((p->inti_flags & ACPI_MADT_TRIGGER_MASK) ==
				 ACPI_MADT_TRIGGER_LEVEL) ?
				 IOSAPIC_LEVEL : IOSAPIC_EDGE);
	return 0;
}

static int __init
acpi_parse_nmi_src(union acpi_subtable_headers * header, const unsigned long end)
{
	struct acpi_madt_nmi_source *nmi_src;

	nmi_src = (struct acpi_madt_nmi_source *)header;

	if (BAD_MADT_ENTRY(nmi_src, end))
		return -EINVAL;

	/* TBD: Support nimsrc entries */
	return 0;
}

static void __init acpi_madt_oem_check(char *oem_id, char *oem_table_id)
{
	if (!strncmp(oem_id, "IBM", 3) && (!strncmp(oem_table_id, "SERMOW", 6))) {

		/*
		 * Unfortunately ITC_DRIFT is not yet part of the
		 * official SAL spec, so the ITC_DRIFT bit is not
		 * set by the BIOS on this hardware.
		 */
		sal_platform_features |= IA64_SAL_PLATFORM_FEATURE_ITC_DRIFT;

		cyclone_setup();
	}
}

static int __init acpi_parse_madt(struct acpi_table_header *table)
{
	acpi_madt = (struct acpi_table_madt *)table;

	acpi_madt_rev = acpi_madt->header.revision;

	/* remember the value for reference after free_initmem() */
#ifdef CONFIG_ITANIUM
	has_8259 = 1;		/* Firmware on old Itanium systems is broken */
#else
	has_8259 = acpi_madt->flags & ACPI_MADT_PCAT_COMPAT;
#endif
	iosapic_system_init(has_8259);

	/* Get base address of IPI Message Block */

	if (acpi_madt->address)
		ipi_base_addr = ioremap(acpi_madt->address, 0);

	printk(KERN_INFO PREFIX "Local APIC address %p\n", ipi_base_addr);

	acpi_madt_oem_check(acpi_madt->header.oem_id,
			    acpi_madt->header.oem_table_id);

	return 0;
}

#ifdef CONFIG_ACPI_NUMA

#undef SLIT_DEBUG

#define PXM_FLAG_LEN ((MAX_PXM_DOMAINS + 1)/32)

static int __initdata srat_num_cpus;	/* number of cpus */
static u32 pxm_flag[PXM_FLAG_LEN];
#define pxm_bit_set(bit)	(set_bit(bit,(void *)pxm_flag))
#define pxm_bit_test(bit)	(test_bit(bit,(void *)pxm_flag))
static struct acpi_table_slit __initdata *slit_table;
cpumask_t early_cpu_possible_map = CPU_MASK_NONE;

static int __init
get_processor_proximity_domain(struct acpi_srat_cpu_affinity *pa)
{
	int pxm;

	pxm = pa->proximity_domain_lo;
	if (acpi_srat_revision >= 2)
		pxm += pa->proximity_domain_hi[0] << 8;
	return pxm;
}

static int __init
get_memory_proximity_domain(struct acpi_srat_mem_affinity *ma)
{
	int pxm;

	pxm = ma->proximity_domain;
	if (acpi_srat_revision <= 1)
		pxm &= 0xff;

	return pxm;
}

/*
 * ACPI 2.0 SLIT (System Locality Information Table)
 * http://devresource.hp.com/devresource/Docs/TechPapers/IA64/slit.pdf
 */
void __init acpi_numa_slit_init(struct acpi_table_slit *slit)
{
	u32 len;

	len = sizeof(struct acpi_table_header) + 8
	    + slit->locality_count * slit->locality_count;
	if (slit->header.length != len) {
		printk(KERN_ERR
		       "ACPI 2.0 SLIT: size mismatch: %d expected, %d actual\n",
		       len, slit->header.length);
		return;
	}
	slit_table = slit;
}

void __init
acpi_numa_processor_affinity_init(struct acpi_srat_cpu_affinity *pa)
{
	int pxm;

	if (!(pa->flags & ACPI_SRAT_CPU_ENABLED))
		return;

	if (srat_num_cpus >= ARRAY_SIZE(node_cpuid)) {
		printk_once(KERN_WARNING
			    "node_cpuid[%ld] is too small, may not be able to use all cpus\n",
			    ARRAY_SIZE(node_cpuid));
		return;
	}
	pxm = get_processor_proximity_domain(pa);

	/* record this node in proximity bitmap */
	pxm_bit_set(pxm);

	node_cpuid[srat_num_cpus].phys_id =
	    (pa->apic_id << 8) | (pa->local_sapic_eid);
	/* nid should be overridden as logical node id later */
	node_cpuid[srat_num_cpus].nid = pxm;
	cpumask_set_cpu(srat_num_cpus, &early_cpu_possible_map);
	srat_num_cpus++;
}

int __init
acpi_numa_memory_affinity_init(struct acpi_srat_mem_affinity *ma)
{
	unsigned long paddr, size;
	int pxm;
	struct node_memblk_s *p, *q, *pend;

	pxm = get_memory_proximity_domain(ma);

	/* fill node memory chunk structure */
	paddr = ma->base_address;
	size = ma->length;

	/* Ignore disabled entries */
	if (!(ma->flags & ACPI_SRAT_MEM_ENABLED))
		return -1;

	if (num_node_memblks >= NR_NODE_MEMBLKS) {
		pr_err("NUMA: too many memblk ranges\n");
		return -EINVAL;
	}

	/* record this node in proximity bitmap */
	pxm_bit_set(pxm);

	/* Insertion sort based on base address */
	pend = &node_memblk[num_node_memblks];
	for (p = &node_memblk[0]; p < pend; p++) {
		if (paddr < p->start_paddr)
			break;
	}
	if (p < pend) {
		for (q = pend - 1; q >= p; q--)
			*(q + 1) = *q;
	}
	p->start_paddr = paddr;
	p->size = size;
	p->nid = pxm;
	num_node_memblks++;
	return 0;
}

void __init acpi_numa_fixup(void)
{
	int i, j, node_from, node_to;

	/* If there's no SRAT, fix the phys_id and mark node 0 online */
	if (srat_num_cpus == 0) {
		node_set_online(0);
		node_cpuid[0].phys_id = hard_smp_processor_id();
		slit_distance(0, 0) = LOCAL_DISTANCE;
		goto out;
	}

	/*
	 * MCD - This can probably be dropped now.  No need for pxm ID to node ID
	 * mapping with sparse node numbering iff MAX_PXM_DOMAINS <= MAX_NUMNODES.
	 */
	nodes_clear(node_online_map);
	for (i = 0; i < MAX_PXM_DOMAINS; i++) {
		if (pxm_bit_test(i)) {
			int nid = acpi_map_pxm_to_node(i);
			node_set_online(nid);
		}
	}

	/* set logical node id in memory chunk structure */
	for (i = 0; i < num_node_memblks; i++)
		node_memblk[i].nid = pxm_to_node(node_memblk[i].nid);

	/* assign memory bank numbers for each chunk on each node */
	for_each_online_node(i) {
		int bank;

		bank = 0;
		for (j = 0; j < num_node_memblks; j++)
			if (node_memblk[j].nid == i)
				node_memblk[j].bank = bank++;
	}

	/* set logical node id in cpu structure */
	for_each_possible_early_cpu(i)
		node_cpuid[i].nid = pxm_to_node(node_cpuid[i].nid);

	printk(KERN_INFO "Number of logical nodes in system = %d\n",
	       num_online_nodes());
	printk(KERN_INFO "Number of memory chunks in system = %d\n",
	       num_node_memblks);

	if (!slit_table) {
		for (i = 0; i < MAX_NUMNODES; i++)
			for (j = 0; j < MAX_NUMNODES; j++)
				slit_distance(i, j) = i == j ?
					LOCAL_DISTANCE : REMOTE_DISTANCE;
		goto out;
	}

	memset(numa_slit, -1, sizeof(numa_slit));
	for (i = 0; i < slit_table->locality_count; i++) {
		if (!pxm_bit_test(i))
			continue;
		node_from = pxm_to_node(i);
		for (j = 0; j < slit_table->locality_count; j++) {
			if (!pxm_bit_test(j))
				continue;
			node_to = pxm_to_node(j);
			slit_distance(node_from, node_to) =
			    slit_table->entry[i * slit_table->locality_count + j];
		}
	}

#ifdef SLIT_DEBUG
	printk("ACPI 2.0 SLIT locality table:\n");
	for_each_online_node(i) {
		for_each_online_node(j)
		    printk("%03d ", node_distance(i, j));
		printk("\n");
	}
#endif
out:
	node_possible_map = node_online_map;
}
#endif				/* CONFIG_ACPI_NUMA */

/*
 * success: return IRQ number (>=0)
 * failure: return < 0
 */
int acpi_register_gsi(struct device *dev, u32 gsi, int triggering, int polarity)
{
	if (acpi_irq_model == ACPI_IRQ_MODEL_PLATFORM)
		return gsi;

	if (has_8259 && gsi < 16)
		return isa_irq_to_vector(gsi);

	return iosapic_register_intr(gsi,
				     (polarity ==
				      ACPI_ACTIVE_HIGH) ? IOSAPIC_POL_HIGH :
				     IOSAPIC_POL_LOW,
				     (triggering ==
				      ACPI_EDGE_SENSITIVE) ? IOSAPIC_EDGE :
				     IOSAPIC_LEVEL);
}
EXPORT_SYMBOL_GPL(acpi_register_gsi);

void acpi_unregister_gsi(u32 gsi)
{
	if (acpi_irq_model == ACPI_IRQ_MODEL_PLATFORM)
		return;

	if (has_8259 && gsi < 16)
		return;

	iosapic_unregister_intr(gsi);
}
EXPORT_SYMBOL_GPL(acpi_unregister_gsi);

static int __init acpi_parse_fadt(struct acpi_table_header *table)
{
	struct acpi_table_header *fadt_header;
	struct acpi_table_fadt *fadt;

	fadt_header = (struct acpi_table_header *)table;
	if (fadt_header->revision != 3)
		return -ENODEV;	/* Only deal with ACPI 2.0 FADT */

	fadt = (struct acpi_table_fadt *)fadt_header;

	acpi_register_gsi(NULL, fadt->sci_interrupt, ACPI_LEVEL_SENSITIVE,
				 ACPI_ACTIVE_LOW);
	return 0;
}

int __init early_acpi_boot_init(void)
{
	int ret;

	/*
	 * do a partial walk of MADT to determine how many CPUs
	 * we have including offline CPUs
	 */
	if (acpi_table_parse(ACPI_SIG_MADT, acpi_parse_madt)) {
		printk(KERN_ERR PREFIX "Can't find MADT\n");
		return 0;
	}

	ret = acpi_table_parse_madt(ACPI_MADT_TYPE_LOCAL_SAPIC,
		acpi_parse_lsapic, NR_CPUS);
	if (ret < 1)
		printk(KERN_ERR PREFIX
		       "Error parsing MADT - no LAPIC entries\n");
	else
		acpi_lapic = 1;

#ifdef CONFIG_SMP
	if (available_cpus == 0) {
		printk(KERN_INFO "ACPI: Found 0 CPUS; assuming 1\n");
		printk(KERN_INFO "CPU 0 (0x%04x)", hard_smp_processor_id());
		smp_boot_data.cpu_phys_id[available_cpus] =
		    hard_smp_processor_id();
		available_cpus = 1;	/* We've got at least one of these, no? */
	}
	smp_boot_data.cpu_count = available_cpus;
#endif
	/* Make boot-up look pretty */
	printk(KERN_INFO "%d CPUs available, %d CPUs total\n", available_cpus,
	       total_cpus);

	return 0;
}

int __init acpi_boot_init(void)
{

	/*
	 * MADT
	 * ----
	 * Parse the Multiple APIC Description Table (MADT), if exists.
	 * Note that this table provides platform SMP configuration
	 * information -- the successor to MPS tables.
	 */

	if (acpi_table_parse(ACPI_SIG_MADT, acpi_parse_madt)) {
		printk(KERN_ERR PREFIX "Can't find MADT\n");
		goto skip_madt;
	}

	/* Local APIC */

	if (acpi_table_parse_madt
	    (ACPI_MADT_TYPE_LOCAL_APIC_OVERRIDE, acpi_parse_lapic_addr_ovr, 0) < 0)
		printk(KERN_ERR PREFIX
		       "Error parsing LAPIC address override entry\n");

	if (acpi_table_parse_madt(ACPI_MADT_TYPE_LOCAL_APIC_NMI, acpi_parse_lapic_nmi, 0)
	    < 0)
		printk(KERN_ERR PREFIX "Error parsing LAPIC NMI entry\n");

	/* I/O APIC */

	if (acpi_table_parse_madt
	    (ACPI_MADT_TYPE_IO_SAPIC, acpi_parse_iosapic, NR_IOSAPICS) < 1) {
		printk(KERN_ERR PREFIX
		       "Error parsing MADT - no IOSAPIC entries\n");
	}

	/* System-Level Interrupt Routing */

	if (acpi_table_parse_madt
	    (ACPI_MADT_TYPE_INTERRUPT_SOURCE, acpi_parse_plat_int_src,
	     ACPI_MAX_PLATFORM_INTERRUPTS) < 0)
		printk(KERN_ERR PREFIX
		       "Error parsing platform interrupt source entry\n");

	if (acpi_table_parse_madt
	    (ACPI_MADT_TYPE_INTERRUPT_OVERRIDE, acpi_parse_int_src_ovr, 0) < 0)
		printk(KERN_ERR PREFIX
		       "Error parsing interrupt source overrides entry\n");

	if (acpi_table_parse_madt(ACPI_MADT_TYPE_NMI_SOURCE, acpi_parse_nmi_src, 0) < 0)
		printk(KERN_ERR PREFIX "Error parsing NMI SRC entry\n");
      skip_madt:

	/*
	 * FADT says whether a legacy keyboard controller is present.
	 * The FADT also contains an SCI_INT line, by which the system
	 * gets interrupts such as power and sleep buttons.  If it's not
	 * on a Legacy interrupt, it needs to be setup.
	 */
	if (acpi_table_parse(ACPI_SIG_FADT, acpi_parse_fadt))
		printk(KERN_ERR PREFIX "Can't find FADT\n");

#ifdef CONFIG_ACPI_NUMA
#ifdef CONFIG_SMP
	if (srat_num_cpus == 0) {
		int cpu, i = 1;
		for (cpu = 0; cpu < smp_boot_data.cpu_count; cpu++)
			if (smp_boot_data.cpu_phys_id[cpu] !=
			    hard_smp_processor_id())
				node_cpuid[i++].phys_id =
				    smp_boot_data.cpu_phys_id[cpu];
	}
#endif
	build_cpu_to_node_map();
#endif
	return 0;
}

int acpi_gsi_to_irq(u32 gsi, unsigned int *irq)
{
	int tmp;

	if (has_8259 && gsi < 16)
		*irq = isa_irq_to_vector(gsi);
	else {
		tmp = gsi_to_irq(gsi);
		if (tmp == -1)
			return -1;
		*irq = tmp;
	}
	return 0;
}

int acpi_isa_irq_to_gsi(unsigned isa_irq, u32 *gsi)
{
	if (isa_irq >= 16)
		return -1;
	*gsi = isa_irq;
	return 0;
}

/*
 *  ACPI based hotplug CPU support
 */
#ifdef CONFIG_ACPI_HOTPLUG_CPU
int acpi_map_cpu2node(acpi_handle handle, int cpu, int physid)
{
#ifdef CONFIG_ACPI_NUMA
	/*
	 * We don't have cpu-only-node hotadd. But if the system equips
	 * SRAT table, pxm is already found and node is ready.
  	 * So, just pxm_to_nid(pxm) is OK.
	 * This code here is for the system which doesn't have full SRAT
  	 * table for possible cpus.
	 */
	node_cpuid[cpu].phys_id = physid;
	node_cpuid[cpu].nid = acpi_get_node(handle);
#endif
	return 0;
}

int additional_cpus __initdata = -1;

static __init int setup_additional_cpus(char *s)
{
	if (s)
		additional_cpus = simple_strtol(s, NULL, 0);

	return 0;
}

early_param("additional_cpus", setup_additional_cpus);

/*
 * cpu_possible_mask should be static, it cannot change as CPUs
 * are onlined, or offlined. The reason is per-cpu data-structures
 * are allocated by some modules at init time, and dont expect to
 * do this dynamically on cpu arrival/departure.
 * cpu_present_mask on the other hand can change dynamically.
 * In case when cpu_hotplug is not compiled, then we resort to current
 * behaviour, which is cpu_possible == cpu_present.
 * - Ashok Raj
 *
 * Three ways to find out the number of additional hotplug CPUs:
 * - If the BIOS specified disabled CPUs in ACPI/mptables use that.
 * - The user can overwrite it with additional_cpus=NUM
 * - Otherwise don't reserve additional CPUs.
 */
__init void prefill_possible_map(void)
{
	int i;
	int possible, disabled_cpus;

	disabled_cpus = total_cpus - available_cpus;

 	if (additional_cpus == -1) {
 		if (disabled_cpus > 0)
			additional_cpus = disabled_cpus;
 		else
			additional_cpus = 0;
 	}

	possible = available_cpus + additional_cpus;

	if (possible > nr_cpu_ids)
		possible = nr_cpu_ids;

	printk(KERN_INFO "SMP: Allowing %d CPUs, %d hotplug CPUs\n",
		possible, max((possible - available_cpus), 0));

	for (i = 0; i < possible; i++)
		set_cpu_possible(i, true);
}

static int _acpi_map_lsapic(acpi_handle handle, int physid, int *pcpu)
{
	int cpu;

	cpu = cpumask_first_zero(cpu_present_mask);
	if (cpu >= nr_cpu_ids)
		return -EINVAL;

	acpi_map_cpu2node(handle, cpu, physid);

	set_cpu_present(cpu, true);
	ia64_cpu_to_sapicid[cpu] = physid;

	acpi_processor_set_pdc(handle);

	*pcpu = cpu;
	return (0);
}

/* wrapper to silence section mismatch warning */
int __ref acpi_map_cpu(acpi_handle handle, phys_cpuid_t physid, u32 acpi_id,
		       int *pcpu)
{
	return _acpi_map_lsapic(handle, physid, pcpu);
}
EXPORT_SYMBOL(acpi_map_cpu);

int acpi_unmap_cpu(int cpu)
{
	ia64_cpu_to_sapicid[cpu] = -1;
	set_cpu_present(cpu, false);

#ifdef CONFIG_ACPI_NUMA
	/* NUMA specific cleanup's */
#endif

	return (0);
}
EXPORT_SYMBOL(acpi_unmap_cpu);
#endif				/* CONFIG_ACPI_HOTPLUG_CPU */

#ifdef CONFIG_ACPI_NUMA
static acpi_status acpi_map_iosapic(acpi_handle handle, u32 depth,
				    void *context, void **ret)
{
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *obj;
	struct acpi_madt_io_sapic *iosapic;
	unsigned int gsi_base;
	int node;

	/* Only care about objects w/ a method that returns the MADT */
	if (ACPI_FAILURE(acpi_evaluate_object(handle, "_MAT", NULL, &buffer)))
		return AE_OK;

	if (!buffer.length || !buffer.pointer)
		return AE_OK;

	obj = buffer.pointer;
	if (obj->type != ACPI_TYPE_BUFFER ||
	    obj->buffer.length < sizeof(*iosapic)) {
		kfree(buffer.pointer);
		return AE_OK;
	}

	iosapic = (struct acpi_madt_io_sapic *)obj->buffer.pointer;

	if (iosapic->header.type != ACPI_MADT_TYPE_IO_SAPIC) {
		kfree(buffer.pointer);
		return AE_OK;
	}

	gsi_base = iosapic->global_irq_base;

	kfree(buffer.pointer);

	/* OK, it's an IOSAPIC MADT entry; associate it with a node */
	node = acpi_get_node(handle);
	if (node == NUMA_NO_NODE || !node_online(node) ||
	    cpumask_empty(cpumask_of_node(node)))
		return AE_OK;

	/* We know a gsi to node mapping! */
	map_iosapic_to_node(gsi_base, node);
	return AE_OK;
}

static int __init
acpi_map_iosapics (void)
{
	acpi_get_devices(NULL, acpi_map_iosapic, NULL, NULL);
	return 0;
}

fs_initcall(acpi_map_iosapics);
#endif				/* CONFIG_ACPI_NUMA */

int __ref acpi_register_ioapic(acpi_handle handle, u64 phys_addr, u32 gsi_base)
{
	int err;

	if ((err = iosapic_init(phys_addr, gsi_base)))
		return err;

#ifdef CONFIG_ACPI_NUMA
	acpi_map_iosapic(handle, 0, NULL, NULL);
#endif				/* CONFIG_ACPI_NUMA */

	return 0;
}

EXPORT_SYMBOL(acpi_register_ioapic);

int acpi_unregister_ioapic(acpi_handle handle, u32 gsi_base)
{
	return iosapic_remove(gsi_base);
}

EXPORT_SYMBOL(acpi_unregister_ioapic);

/*
 * acpi_suspend_lowlevel() - save kernel state and suspend.
 *
 * TBD when IA64 starts to support suspend...
 */
int acpi_suspend_lowlevel(void) { return 0; }
