/*
 * APIC driver for the Unisys ES7000 chipset.
 */
#define APIC_DEFINITION 1
#include <linux/threads.h>
#include <linux/cpumask.h>
#include <asm/mpspec.h>
#include <asm/genapic.h>
#include <asm/fixmap.h>
#include <asm/apicdef.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/acpi.h>
#include <linux/smp.h>
#include <asm/ipi.h>

#define APIC_DFR_VALUE_CLUSTER		(APIC_DFR_CLUSTER)
#define INT_DELIVERY_MODE_CLUSTER	(dest_LowestPrio)
#define INT_DEST_MODE_CLUSTER		(1) /* logical delivery broadcast to all procs */

#define APIC_DFR_VALUE			(APIC_DFR_FLAT)

extern void es7000_enable_apic_mode(void);
extern int apic_version [MAX_APICS];
extern u8 cpu_2_logical_apicid[];
extern unsigned int boot_cpu_physical_apicid;

extern int parse_unisys_oem (char *oemptr);
extern int find_unisys_acpi_oem_table(unsigned long *oem_addr);
extern void unmap_unisys_acpi_oem_table(unsigned long oem_addr);
extern void setup_unisys(void);

#define apicid_cluster(apicid)		(apicid & 0xF0)
#define xapic_phys_to_log_apicid(cpu)	per_cpu(x86_bios_cpu_apicid, cpu)

static void es7000_vector_allocation_domain(int cpu, cpumask_t *retmask)
{
	/* Careful. Some cpus do not strictly honor the set of cpus
	 * specified in the interrupt destination when using lowest
	 * priority interrupt delivery mode.
	 *
	 * In particular there was a hyperthreading cpu observed to
	 * deliver interrupts to the wrong hyperthread when only one
	 * hyperthread was specified in the interrupt desitination.
	 */
	*retmask = (cpumask_t){ { [0] = APIC_ALL_CPUS, } };
}


static void es7000_wait_for_init_deassert(atomic_t *deassert)
{
#ifndef CONFIG_ES7000_CLUSTERED_APIC
	while (!atomic_read(deassert))
		cpu_relax();
#endif
	return;
}

static unsigned int es7000_get_apic_id(unsigned long x)
{
	return (x >> 24) & 0xFF;
}

#ifdef CONFIG_ACPI
static int es7000_check_dsdt(void)
{
	struct acpi_table_header header;

	if (ACPI_SUCCESS(acpi_get_table_header(ACPI_SIG_DSDT, 0, &header)) &&
	    !strncmp(header.oem_id, "UNISYS", 6))
		return 1;
	return 0;
}
#endif

static void es7000_send_IPI_mask(const struct cpumask *mask, int vector)
{
	default_send_IPI_mask_sequence(mask, vector);
}

static void es7000_send_IPI_allbutself(int vector)
{
	default_send_IPI_mask_allbutself(cpu_online_mask, vector);
}

static void es7000_send_IPI_all(int vector)
{
	es7000_send_IPI_mask(cpu_online_mask, vector);
}

static int es7000_apic_id_registered(void)
{
	        return 1;
}

static const cpumask_t *target_cpus_cluster(void)
{
	return &CPU_MASK_ALL;
}

static const cpumask_t *es7000_target_cpus(void)
{
	return &cpumask_of_cpu(smp_processor_id());
}

static unsigned long
es7000_check_apicid_used(physid_mask_t bitmap, int apicid)
{
	return 0;
}
static unsigned long es7000_check_apicid_present(int bit)
{
	return physid_isset(bit, phys_cpu_present_map);
}

static unsigned long calculate_ldr(int cpu)
{
	unsigned long id = xapic_phys_to_log_apicid(cpu);

	return (SET_APIC_LOGICAL_ID(id));
}

/*
 * Set up the logical destination ID.
 *
 * Intel recommends to set DFR, LdR and TPR before enabling
 * an APIC.  See e.g. "AP-388 82489DX User's Manual" (Intel
 * document number 292116).  So here it goes...
 */
static void es7000_init_apic_ldr_cluster(void)
{
	unsigned long val;
	int cpu = smp_processor_id();

	apic_write(APIC_DFR, APIC_DFR_VALUE_CLUSTER);
	val = calculate_ldr(cpu);
	apic_write(APIC_LDR, val);
}

static void es7000_init_apic_ldr(void)
{
	unsigned long val;
	int cpu = smp_processor_id();

	apic_write(APIC_DFR, APIC_DFR_VALUE);
	val = calculate_ldr(cpu);
	apic_write(APIC_LDR, val);
}

static void es7000_setup_apic_routing(void)
{
	int apic = per_cpu(x86_bios_cpu_apicid, smp_processor_id());
	printk("Enabling APIC mode:  %s. Using %d I/O APICs, target cpus %lx\n",
		(apic_version[apic] == 0x14) ?
			"Physical Cluster" : "Logical Cluster",
			nr_ioapics, cpus_addr(*es7000_target_cpus())[0]);
}

static int es7000_apicid_to_node(int logical_apicid)
{
	return 0;
}


static int es7000_cpu_present_to_apicid(int mps_cpu)
{
	if (!mps_cpu)
		return boot_cpu_physical_apicid;
	else if (mps_cpu < nr_cpu_ids)
		return (int) per_cpu(x86_bios_cpu_apicid, mps_cpu);
	else
		return BAD_APICID;
}

static physid_mask_t es7000_apicid_to_cpu_present(int phys_apicid)
{
	static int id = 0;
	physid_mask_t mask;

	mask = physid_mask_of_physid(id);
	++id;

	return mask;
}

/* Mapping from cpu number to logical apicid */
static int es7000_cpu_to_logical_apicid(int cpu)
{
#ifdef CONFIG_SMP
	if (cpu >= nr_cpu_ids)
		return BAD_APICID;
	return (int)cpu_2_logical_apicid[cpu];
#else
	return logical_smp_processor_id();
#endif
}

static physid_mask_t es7000_ioapic_phys_id_map(physid_mask_t phys_map)
{
	/* For clustered we don't have a good way to do this yet - hack */
	return physids_promote(0xff);
}

static int es7000_check_phys_apicid_present(int cpu_physical_apicid)
{
	boot_cpu_physical_apicid = read_apic_id();
	return (1);
}

static unsigned int
es7000_cpu_mask_to_apicid_cluster(const struct cpumask *cpumask)
{
	int cpus_found = 0;
	int num_bits_set;
	int apicid;
	int cpu;

	num_bits_set = cpumask_weight(cpumask);
	/* Return id to all */
	if (num_bits_set == nr_cpu_ids)
		return 0xFF;
	/*
	 * The cpus in the mask must all be on the apic cluster.  If are not
	 * on the same apicid cluster return default value of target_cpus():
	 */
	cpu = cpumask_first(cpumask);
	apicid = es7000_cpu_to_logical_apicid(cpu);

	while (cpus_found < num_bits_set) {
		if (cpumask_test_cpu(cpu, cpumask)) {
			int new_apicid = es7000_cpu_to_logical_apicid(cpu);

			if (apicid_cluster(apicid) !=
					apicid_cluster(new_apicid)) {
				printk ("%s: Not a valid mask!\n", __func__);

				return 0xFF;
			}
			apicid = new_apicid;
			cpus_found++;
		}
		cpu++;
	}
	return apicid;
}

static unsigned int es7000_cpu_mask_to_apicid(const cpumask_t *cpumask)
{
	int cpus_found = 0;
	int num_bits_set;
	int apicid;
	int cpu;

	num_bits_set = cpus_weight(*cpumask);
	/* Return id to all */
	if (num_bits_set == nr_cpu_ids)
		return es7000_cpu_to_logical_apicid(0);
	/*
	 * The cpus in the mask must all be on the apic cluster.  If are not
	 * on the same apicid cluster return default value of target_cpus():
	 */
	cpu = first_cpu(*cpumask);
	apicid = es7000_cpu_to_logical_apicid(cpu);
	while (cpus_found < num_bits_set) {
		if (cpu_isset(cpu, *cpumask)) {
			int new_apicid = es7000_cpu_to_logical_apicid(cpu);

			if (apicid_cluster(apicid) !=
					apicid_cluster(new_apicid)) {
				printk ("%s: Not a valid mask!\n", __func__);

				return es7000_cpu_to_logical_apicid(0);
			}
			apicid = new_apicid;
			cpus_found++;
		}
		cpu++;
	}
	return apicid;
}

static unsigned int
es7000_cpu_mask_to_apicid_and(const struct cpumask *inmask,
			      const struct cpumask *andmask)
{
	int apicid = es7000_cpu_to_logical_apicid(0);
	cpumask_var_t cpumask;

	if (!alloc_cpumask_var(&cpumask, GFP_ATOMIC))
		return apicid;

	cpumask_and(cpumask, inmask, andmask);
	cpumask_and(cpumask, cpumask, cpu_online_mask);
	apicid = es7000_cpu_mask_to_apicid(cpumask);

	free_cpumask_var(cpumask);

	return apicid;
}

static int es7000_phys_pkg_id(int cpuid_apic, int index_msb)
{
	return cpuid_apic >> index_msb;
}

void __init es7000_update_genapic_to_cluster(void)
{
	apic->target_cpus = target_cpus_cluster;
	apic->irq_delivery_mode = INT_DELIVERY_MODE_CLUSTER;
	apic->irq_dest_mode = INT_DEST_MODE_CLUSTER;

	apic->init_apic_ldr = es7000_init_apic_ldr_cluster;

	apic->cpu_mask_to_apicid = es7000_cpu_mask_to_apicid_cluster;
}

static int probe_es7000(void)
{
	/* probed later in mptable/ACPI hooks */
	return 0;
}

static __init int
es7000_mps_oem_check(struct mpc_table *mpc, char *oem, char *productid)
{
	if (mpc->oemptr) {
		struct mpc_oemtable *oem_table =
			(struct mpc_oemtable *)mpc->oemptr;

		if (!strncmp(oem, "UNISYS", 6))
			return parse_unisys_oem((char *)oem_table);
	}
	return 0;
}

#ifdef CONFIG_ACPI
/* Hook from generic ACPI tables.c */
static int __init es7000_acpi_madt_oem_check(char *oem_id, char *oem_table_id)
{
	unsigned long oem_addr = 0;
	int check_dsdt;
	int ret = 0;

	/* check dsdt at first to avoid clear fix_map for oem_addr */
	check_dsdt = es7000_check_dsdt();

	if (!find_unisys_acpi_oem_table(&oem_addr)) {
		if (check_dsdt)
			ret = parse_unisys_oem((char *)oem_addr);
		else {
			setup_unisys();
			ret = 1;
		}
		/*
		 * we need to unmap it
		 */
		unmap_unisys_acpi_oem_table(oem_addr);
	}
	return ret;
}
#else
static int __init es7000_acpi_madt_oem_check(char *oem_id, char *oem_table_id)
{
	return 0;
}
#endif


struct genapic apic_es7000 = {

	.name				= "es7000",
	.probe				= probe_es7000,
	.acpi_madt_oem_check		= es7000_acpi_madt_oem_check,
	.apic_id_registered		= es7000_apic_id_registered,

	.irq_delivery_mode		= dest_Fixed,
	/* phys delivery to target CPUs: */
	.irq_dest_mode			= 0,

	.target_cpus			= es7000_target_cpus,
	.disable_esr			= 1,
	.dest_logical			= 0,
	.check_apicid_used		= es7000_check_apicid_used,
	.check_apicid_present		= es7000_check_apicid_present,

	.vector_allocation_domain	= es7000_vector_allocation_domain,
	.init_apic_ldr			= es7000_init_apic_ldr,

	.ioapic_phys_id_map		= es7000_ioapic_phys_id_map,
	.setup_apic_routing		= es7000_setup_apic_routing,
	.multi_timer_check		= NULL,
	.apicid_to_node			= es7000_apicid_to_node,
	.cpu_to_logical_apicid		= es7000_cpu_to_logical_apicid,
	.cpu_present_to_apicid		= es7000_cpu_present_to_apicid,
	.apicid_to_cpu_present		= es7000_apicid_to_cpu_present,
	.setup_portio_remap		= NULL,
	.check_phys_apicid_present	= es7000_check_phys_apicid_present,
	.enable_apic_mode		= es7000_enable_apic_mode,
	.phys_pkg_id			= es7000_phys_pkg_id,
	.mps_oem_check			= es7000_mps_oem_check,

	.get_apic_id			= es7000_get_apic_id,
	.set_apic_id			= NULL,
	.apic_id_mask			= 0xFF << 24,

	.cpu_mask_to_apicid		= es7000_cpu_mask_to_apicid,
	.cpu_mask_to_apicid_and		= es7000_cpu_mask_to_apicid_and,

	.send_IPI_mask			= es7000_send_IPI_mask,
	.send_IPI_mask_allbutself	= NULL,
	.send_IPI_allbutself		= es7000_send_IPI_allbutself,
	.send_IPI_all			= es7000_send_IPI_all,
	.send_IPI_self			= NULL,

	.wakeup_cpu			= NULL,

	.trampoline_phys_low		= 0x467,
	.trampoline_phys_high		= 0x469,

	.wait_for_init_deassert		= es7000_wait_for_init_deassert,

	/* Nothing to do for most platforms, since cleared by the INIT cycle: */
	.smp_callin_clear_local_apic	= NULL,
	.store_NMI_vector		= NULL,
	.inquire_remote_apic		= default_inquire_remote_apic,
};
