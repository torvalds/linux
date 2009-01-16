#ifndef __ASM_ES7000_APIC_H
#define __ASM_ES7000_APIC_H

#include <linux/gfp.h>

#define xapic_phys_to_log_apicid(cpu) per_cpu(x86_bios_cpu_apicid, cpu)
#define esr_disable (1)

static inline int apic_id_registered(void)
{
	        return (1);
}

static inline const cpumask_t *target_cpus_cluster(void)
{
	return &CPU_MASK_ALL;
}

static inline const cpumask_t *target_cpus(void)
{
	return &cpumask_of_cpu(smp_processor_id());
}

#define APIC_DFR_VALUE_CLUSTER		(APIC_DFR_CLUSTER)
#define INT_DELIVERY_MODE_CLUSTER	(dest_LowestPrio)
#define INT_DEST_MODE_CLUSTER		(1) /* logical delivery broadcast to all procs */
#define NO_BALANCE_IRQ_CLUSTER		(1)

#define APIC_DFR_VALUE		(APIC_DFR_FLAT)
#define INT_DELIVERY_MODE	(dest_Fixed)
#define INT_DEST_MODE		(0)    /* phys delivery to target procs */
#define NO_BALANCE_IRQ		(0)
#undef  APIC_DEST_LOGICAL
#define APIC_DEST_LOGICAL	0x0

static inline unsigned long check_apicid_used(physid_mask_t bitmap, int apicid)
{
	return 0;
}
static inline unsigned long check_apicid_present(int bit)
{
	return physid_isset(bit, phys_cpu_present_map);
}

#define apicid_cluster(apicid) (apicid & 0xF0)

static inline unsigned long calculate_ldr(int cpu)
{
	unsigned long id;
	id = xapic_phys_to_log_apicid(cpu);
	return (SET_APIC_LOGICAL_ID(id));
}

/*
 * Set up the logical destination ID.
 *
 * Intel recommends to set DFR, LdR and TPR before enabling
 * an APIC.  See e.g. "AP-388 82489DX User's Manual" (Intel
 * document number 292116).  So here it goes...
 */
static inline void init_apic_ldr_cluster(void)
{
	unsigned long val;
	int cpu = smp_processor_id();

	apic_write(APIC_DFR, APIC_DFR_VALUE_CLUSTER);
	val = calculate_ldr(cpu);
	apic_write(APIC_LDR, val);
}

static inline void init_apic_ldr(void)
{
	unsigned long val;
	int cpu = smp_processor_id();

	apic_write(APIC_DFR, APIC_DFR_VALUE);
	val = calculate_ldr(cpu);
	apic_write(APIC_LDR, val);
}

extern int apic_version [MAX_APICS];
static inline void setup_apic_routing(void)
{
	int apic = per_cpu(x86_bios_cpu_apicid, smp_processor_id());
	printk("Enabling APIC mode:  %s. Using %d I/O APICs, target cpus %lx\n",
		(apic_version[apic] == 0x14) ?
			"Physical Cluster" : "Logical Cluster",
			nr_ioapics, cpus_addr(*target_cpus())[0]);
}

static inline int multi_timer_check(int apic, int irq)
{
	return 0;
}

static inline int apicid_to_node(int logical_apicid)
{
	return 0;
}


static inline int cpu_present_to_apicid(int mps_cpu)
{
	if (!mps_cpu)
		return boot_cpu_physical_apicid;
	else if (mps_cpu < nr_cpu_ids)
		return (int) per_cpu(x86_bios_cpu_apicid, mps_cpu);
	else
		return BAD_APICID;
}

static inline physid_mask_t apicid_to_cpu_present(int phys_apicid)
{
	static int id = 0;
	physid_mask_t mask;
	mask = physid_mask_of_physid(id);
	++id;
	return mask;
}

extern u8 cpu_2_logical_apicid[];
/* Mapping from cpu number to logical apicid */
static inline int cpu_to_logical_apicid(int cpu)
{
#ifdef CONFIG_SMP
	if (cpu >= nr_cpu_ids)
		return BAD_APICID;
	return (int)cpu_2_logical_apicid[cpu];
#else
	return logical_smp_processor_id();
#endif
}

static inline physid_mask_t ioapic_phys_id_map(physid_mask_t phys_map)
{
	/* For clustered we don't have a good way to do this yet - hack */
	return physids_promote(0xff);
}


static inline void setup_portio_remap(void)
{
}

extern unsigned int boot_cpu_physical_apicid;
static inline int check_phys_apicid_present(int cpu_physical_apicid)
{
	boot_cpu_physical_apicid = read_apic_id();
	return (1);
}

static inline unsigned int
cpu_mask_to_apicid_cluster(const struct cpumask *cpumask)
{
	int num_bits_set;
	int cpus_found = 0;
	int cpu;
	int apicid;

	num_bits_set = cpumask_weight(cpumask);
	/* Return id to all */
	if (num_bits_set == nr_cpu_ids)
		return 0xFF;
	/*
	 * The cpus in the mask must all be on the apic cluster.  If are not
	 * on the same apicid cluster return default value of TARGET_CPUS.
	 */
	cpu = cpumask_first(cpumask);
	apicid = cpu_to_logical_apicid(cpu);
	while (cpus_found < num_bits_set) {
		if (cpumask_test_cpu(cpu, cpumask)) {
			int new_apicid = cpu_to_logical_apicid(cpu);
			if (apicid_cluster(apicid) !=
					apicid_cluster(new_apicid)){
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

static inline unsigned int cpu_mask_to_apicid(const cpumask_t *cpumask)
{
	int num_bits_set;
	int cpus_found = 0;
	int cpu;
	int apicid;

	num_bits_set = cpus_weight(*cpumask);
	/* Return id to all */
	if (num_bits_set == nr_cpu_ids)
		return cpu_to_logical_apicid(0);
	/*
	 * The cpus in the mask must all be on the apic cluster.  If are not
	 * on the same apicid cluster return default value of TARGET_CPUS.
	 */
	cpu = first_cpu(*cpumask);
	apicid = cpu_to_logical_apicid(cpu);
	while (cpus_found < num_bits_set) {
		if (cpu_isset(cpu, *cpumask)) {
			int new_apicid = cpu_to_logical_apicid(cpu);
			if (apicid_cluster(apicid) !=
					apicid_cluster(new_apicid)){
				printk ("%s: Not a valid mask!\n", __func__);
				return cpu_to_logical_apicid(0);
			}
			apicid = new_apicid;
			cpus_found++;
		}
		cpu++;
	}
	return apicid;
}


static inline unsigned int cpu_mask_to_apicid_and(const struct cpumask *inmask,
						  const struct cpumask *andmask)
{
	int apicid = cpu_to_logical_apicid(0);
	cpumask_var_t cpumask;

	if (!alloc_cpumask_var(&cpumask, GFP_ATOMIC))
		return apicid;

	cpumask_and(cpumask, inmask, andmask);
	cpumask_and(cpumask, cpumask, cpu_online_mask);
	apicid = cpu_mask_to_apicid(cpumask);

	free_cpumask_var(cpumask);
	return apicid;
}

static inline u32 phys_pkg_id(u32 cpuid_apic, int index_msb)
{
	return cpuid_apic >> index_msb;
}

#endif /* __ASM_ES7000_APIC_H */
