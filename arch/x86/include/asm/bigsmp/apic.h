#ifndef __ASM_MACH_APIC_H
#define __ASM_MACH_APIC_H

#define xapic_phys_to_log_apicid(cpu) (per_cpu(x86_bios_cpu_apicid, cpu))
#define esr_disable (1)

static inline int apic_id_registered(void)
{
	return (1);
}

static inline const cpumask_t *target_cpus(void)
{
#ifdef CONFIG_SMP
	return &cpu_online_map;
#else
	return &cpumask_of_cpu(0);
#endif
}

#undef APIC_DEST_LOGICAL
#define APIC_DEST_LOGICAL	0
#define APIC_DFR_VALUE		(APIC_DFR_FLAT)
#define INT_DELIVERY_MODE	(dest_Fixed)
#define INT_DEST_MODE		(0)    /* phys delivery to target proc */
#define NO_BALANCE_IRQ		(0)

static inline unsigned long check_apicid_used(physid_mask_t bitmap, int apicid)
{
	return (0);
}

static inline unsigned long check_apicid_present(int bit)
{
	return (1);
}

static inline unsigned long calculate_ldr(int cpu)
{
	unsigned long val, id;
	val = apic_read(APIC_LDR) & ~APIC_LDR_MASK;
	id = xapic_phys_to_log_apicid(cpu);
	val |= SET_APIC_LOGICAL_ID(id);
	return val;
}

/*
 * Set up the logical destination ID.
 *
 * Intel recommends to set DFR, LDR and TPR before enabling
 * an APIC.  See e.g. "AP-388 82489DX User's Manual" (Intel
 * document number 292116).  So here it goes...
 */
static inline void init_apic_ldr(void)
{
	unsigned long val;
	int cpu = smp_processor_id();

	apic_write(APIC_DFR, APIC_DFR_VALUE);
	val = calculate_ldr(cpu);
	apic_write(APIC_LDR, val);
}

static inline void setup_apic_routing(void)
{
	printk("Enabling APIC mode:  %s.  Using %d I/O APICs\n",
		"Physflat", nr_ioapics);
}

static inline int multi_timer_check(int apic, int irq)
{
	return (0);
}

static inline int apicid_to_node(int logical_apicid)
{
	return apicid_2_node[hard_smp_processor_id()];
}

static inline int cpu_present_to_apicid(int mps_cpu)
{
	if (mps_cpu < nr_cpu_ids)
		return (int) per_cpu(x86_bios_cpu_apicid, mps_cpu);

	return BAD_APICID;
}

static inline physid_mask_t apicid_to_cpu_present(int phys_apicid)
{
	return physid_mask_of_physid(phys_apicid);
}

extern u8 cpu_2_logical_apicid[];
/* Mapping from cpu number to logical apicid */
static inline int cpu_to_logical_apicid(int cpu)
{
	if (cpu >= nr_cpu_ids)
		return BAD_APICID;
	return cpu_physical_id(cpu);
}

static inline physid_mask_t ioapic_phys_id_map(physid_mask_t phys_map)
{
	/* For clustered we don't have a good way to do this yet - hack */
	return physids_promote(0xFFL);
}

static inline void setup_portio_remap(void)
{
}

static inline void enable_apic_mode(void)
{
}

static inline int check_phys_apicid_present(int boot_cpu_physical_apicid)
{
	return (1);
}

/* As we are using single CPU as destination, pick only one CPU here */
static inline unsigned int cpu_mask_to_apicid(const cpumask_t *cpumask)
{
	int cpu;
	int apicid;	

	cpu = first_cpu(*cpumask);
	apicid = cpu_to_logical_apicid(cpu);
	return apicid;
}

static inline unsigned int cpu_mask_to_apicid_and(const struct cpumask *cpumask,
						  const struct cpumask *andmask)
{
	int cpu;

	/*
	 * We're using fixed IRQ delivery, can only return one phys APIC ID.
	 * May as well be the first.
	 */
	for_each_cpu_and(cpu, cpumask, andmask)
		if (cpumask_test_cpu(cpu, cpu_online_mask))
			break;
	if (cpu < nr_cpu_ids)
		return cpu_to_logical_apicid(cpu);

	return BAD_APICID;
}

static inline u32 phys_pkg_id(u32 cpuid_apic, int index_msb)
{
	return cpuid_apic >> index_msb;
}

#endif /* __ASM_MACH_APIC_H */
