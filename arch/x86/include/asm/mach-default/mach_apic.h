#ifndef _ASM_X86_MACH_DEFAULT_MACH_APIC_H
#define _ASM_X86_MACH_DEFAULT_MACH_APIC_H

#ifdef CONFIG_X86_LOCAL_APIC

#include <mach_apicdef.h>
#include <asm/smp.h>

#define APIC_DFR_VALUE	(APIC_DFR_FLAT)

static inline const struct cpumask *default_target_cpus(void)
{ 
#ifdef CONFIG_SMP
	return cpu_online_mask;
#else
	return cpumask_of(0);
#endif
} 

#ifdef CONFIG_X86_64
#include <asm/genapic.h>
#define cpu_mask_to_apicid (apic->cpu_mask_to_apicid)
#define cpu_mask_to_apicid_and (apic->cpu_mask_to_apicid_and)
#define phys_pkg_id	(apic->phys_pkg_id)
#define read_apic_id()  (GET_APIC_ID(apic_read(APIC_ID)))
#define send_IPI_self (apic->send_IPI_self)
#define wakeup_secondary_cpu (apic->wakeup_cpu)
extern void default_setup_apic_routing(void);
#else
#define wakeup_secondary_cpu wakeup_secondary_cpu_via_init
/*
 * Set up the logical destination ID.
 *
 * Intel recommends to set DFR, LDR and TPR before enabling
 * an APIC.  See e.g. "AP-388 82489DX User's Manual" (Intel
 * document number 292116).  So here it goes...
 */
static inline void default_init_apic_ldr(void)
{
	unsigned long val;

	apic_write(APIC_DFR, APIC_DFR_VALUE);
	val = apic_read(APIC_LDR) & ~APIC_LDR_MASK;
	val |= SET_APIC_LOGICAL_ID(1UL << smp_processor_id());
	apic_write(APIC_LDR, val);
}

static inline int default_apic_id_registered(void)
{
	return physid_isset(read_apic_id(), phys_cpu_present_map);
}

static inline unsigned int cpu_mask_to_apicid(const struct cpumask *cpumask)
{
	return cpumask_bits(cpumask)[0];
}

static inline unsigned int cpu_mask_to_apicid_and(const struct cpumask *cpumask,
						  const struct cpumask *andmask)
{
	unsigned long mask1 = cpumask_bits(cpumask)[0];
	unsigned long mask2 = cpumask_bits(andmask)[0];
	unsigned long mask3 = cpumask_bits(cpu_online_mask)[0];

	return (unsigned int)(mask1 & mask2 & mask3);
}

static inline u32 phys_pkg_id(u32 cpuid_apic, int index_msb)
{
	return cpuid_apic >> index_msb;
}

static inline void default_setup_apic_routing(void)
{
#ifdef CONFIG_X86_IO_APIC
	printk("Enabling APIC mode:  %s.  Using %d I/O APICs\n",
					"Flat", nr_ioapics);
#endif
}

static inline int default_apicid_to_node(int logical_apicid)
{
#ifdef CONFIG_SMP
	return apicid_2_node[hard_smp_processor_id()];
#else
	return 0;
#endif
}

#endif

static inline unsigned long default_check_apicid_used(physid_mask_t bitmap, int apicid)
{
	return physid_isset(apicid, bitmap);
}

static inline unsigned long default_check_apicid_present(int bit)
{
	return physid_isset(bit, phys_cpu_present_map);
}

static inline physid_mask_t default_ioapic_phys_id_map(physid_mask_t phys_map)
{
	return phys_map;
}

/* Mapping from cpu number to logical apicid */
static inline int default_cpu_to_logical_apicid(int cpu)
{
	return 1 << cpu;
}

static inline int __default_cpu_present_to_apicid(int mps_cpu)
{
	if (mps_cpu < nr_cpu_ids && cpu_present(mps_cpu))
		return (int)per_cpu(x86_bios_cpu_apicid, mps_cpu);
	else
		return BAD_APICID;
}

#ifdef CONFIG_X86_32
static inline int default_cpu_present_to_apicid(int mps_cpu)
{
	return __default_cpu_present_to_apicid(mps_cpu);
}
#else
extern int default_cpu_present_to_apicid(int mps_cpu);
#endif

static inline physid_mask_t apicid_to_cpu_present(int phys_apicid)
{
	return physid_mask_of_physid(phys_apicid);
}

static inline void setup_portio_remap(void)
{
}

static inline int check_phys_apicid_present(int boot_cpu_physical_apicid)
{
	return physid_isset(boot_cpu_physical_apicid, phys_cpu_present_map);
}

static inline void enable_apic_mode(void)
{
}
#endif /* CONFIG_X86_LOCAL_APIC */
#endif /* _ASM_X86_MACH_DEFAULT_MACH_APIC_H */
