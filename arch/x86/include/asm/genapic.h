#ifndef _ASM_X86_GENAPIC_H
#define _ASM_X86_GENAPIC_H

#include <linux/cpumask.h>

#include <asm/mpspec.h>
#include <asm/atomic.h>
#include <asm/apic.h>

/*
 * Copyright 2004 James Cleverdon, IBM.
 * Subject to the GNU Public License, v.2
 *
 * Generic APIC sub-arch data struct.
 *
 * Hacked for x86-64 by James Cleverdon from i386 architecture code by
 * Martin Bligh, Andi Kleen, James Bottomley, John Stultz, and
 * James Cleverdon.
 */
struct genapic {
	char *name;

	int (*probe)(void);
	int (*acpi_madt_oem_check)(char *oem_id, char *oem_table_id);
	int (*apic_id_registered)(void);

	u32 irq_delivery_mode;
	u32 irq_dest_mode;

	const struct cpumask *(*target_cpus)(void);

	int disable_esr;

	int dest_logical;
	unsigned long (*check_apicid_used)(physid_mask_t bitmap, int apicid);
	unsigned long (*check_apicid_present)(int apicid);

	void (*vector_allocation_domain)(int cpu, struct cpumask *retmask);
	void (*init_apic_ldr)(void);

	physid_mask_t (*ioapic_phys_id_map)(physid_mask_t map);

	void (*setup_apic_routing)(void);
	int (*multi_timer_check)(int apic, int irq);
	int (*apicid_to_node)(int logical_apicid);
	int (*cpu_to_logical_apicid)(int cpu);
	int (*cpu_present_to_apicid)(int mps_cpu);
	physid_mask_t (*apicid_to_cpu_present)(int phys_apicid);
	void (*setup_portio_remap)(void);
	int (*check_phys_apicid_present)(int boot_cpu_physical_apicid);
	void (*enable_apic_mode)(void);
	int (*phys_pkg_id)(int cpuid_apic, int index_msb);

	/*
	 * When one of the next two hooks returns 1 the genapic
	 * is switched to this. Essentially they are additional
	 * probe functions:
	 */
	int (*mps_oem_check)(struct mpc_table *mpc, char *oem, char *productid);

	unsigned int (*get_apic_id)(unsigned long x);
	unsigned long (*set_apic_id)(unsigned int id);
	unsigned long apic_id_mask;

	unsigned int (*cpu_mask_to_apicid)(const struct cpumask *cpumask);
	unsigned int (*cpu_mask_to_apicid_and)(const struct cpumask *cpumask,
					       const struct cpumask *andmask);

	/* ipi */
	void (*send_IPI_mask)(const struct cpumask *mask, int vector);
	void (*send_IPI_mask_allbutself)(const struct cpumask *mask,
					 int vector);
	void (*send_IPI_allbutself)(int vector);
	void (*send_IPI_all)(int vector);
	void (*send_IPI_self)(int vector);

	/* wakeup_secondary_cpu */
	int (*wakeup_cpu)(int apicid, unsigned long start_eip);

	int trampoline_phys_low;
	int trampoline_phys_high;

	void (*wait_for_init_deassert)(atomic_t *deassert);
	void (*smp_callin_clear_local_apic)(void);
	void (*store_NMI_vector)(unsigned short *high, unsigned short *low);
	void (*inquire_remote_apic)(int apicid);

	/* apic ops */
	u32 (*read)(u32 reg);
	void (*write)(u32 reg, u32 v);
	u64 (*icr_read)(void);
	void (*icr_write)(u32 low, u32 high);
	void (*wait_icr_idle)(void);
	u32 (*safe_wait_icr_idle)(void);
};

extern struct genapic *apic;

static inline u32 apic_read(u32 reg)
{
	return apic->read(reg);
}

static inline void apic_write(u32 reg, u32 val)
{
	apic->write(reg, val);
}

static inline u64 apic_icr_read(void)
{
	return apic->icr_read();
}

static inline void apic_icr_write(u32 low, u32 high)
{
	apic->icr_write(low, high);
}

static inline void apic_wait_icr_idle(void)
{
	apic->wait_icr_idle();
}

static inline u32 safe_apic_wait_icr_idle(void)
{
	return apic->safe_wait_icr_idle();
}


static inline void ack_APIC_irq(void)
{
	/*
	 * ack_APIC_irq() actually gets compiled as a single instruction
	 * ... yummie.
	 */

	/* Docs say use 0 for future compatibility */
	apic_write(APIC_EOI, 0);
}

static inline unsigned default_get_apic_id(unsigned long x)
{
	unsigned int ver = GET_APIC_VERSION(apic_read(APIC_LVR));

	if (APIC_XAPIC(ver))
		return (x >> 24) & 0xFF;
	else
		return (x >> 24) & 0x0F;
}

/*
 * Warm reset vector default position:
 */
#define DEFAULT_TRAMPOLINE_PHYS_LOW		0x467
#define DEFAULT_TRAMPOLINE_PHYS_HIGH		0x469

#ifdef CONFIG_X86_32
extern void es7000_update_genapic_to_cluster(void);
#else
extern struct genapic apic_flat;
extern struct genapic apic_physflat;
extern struct genapic apic_x2apic_cluster;
extern struct genapic apic_x2apic_phys;
extern int default_acpi_madt_oem_check(char *, char *);

extern void apic_send_IPI_self(int vector);

extern struct genapic apic_x2apic_uv_x;
DECLARE_PER_CPU(int, x2apic_extra_bits);

extern void default_setup_apic_routing(void);

extern int default_cpu_present_to_apicid(int mps_cpu);
extern int default_check_phys_apicid_present(int boot_cpu_physical_apicid);
#endif

static inline void default_wait_for_init_deassert(atomic_t *deassert)
{
	while (!atomic_read(deassert))
		cpu_relax();
	return;
}

extern void generic_bigsmp_probe(void);


#ifdef CONFIG_X86_LOCAL_APIC

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

DECLARE_EARLY_PER_CPU(u16, x86_bios_cpu_apicid);


static inline unsigned int read_apic_id(void)
{
	unsigned int reg;

	reg = apic_read(APIC_ID);

	return apic->get_apic_id(reg);
}

#ifdef CONFIG_X86_64
extern void default_setup_apic_routing(void);
#else

/*
 * Set up the logical destination ID.
 *
 * Intel recommends to set DFR, LDR and TPR before enabling
 * an APIC.  See e.g. "AP-388 82489DX User's Manual" (Intel
 * document number 292116).  So here it goes...
 */
extern void default_init_apic_ldr(void);

static inline int default_apic_id_registered(void)
{
	return physid_isset(read_apic_id(), phys_cpu_present_map);
}

static inline unsigned int
default_cpu_mask_to_apicid(const struct cpumask *cpumask)
{
	return cpumask_bits(cpumask)[0];
}

static inline unsigned int
default_cpu_mask_to_apicid_and(const struct cpumask *cpumask,
			       const struct cpumask *andmask)
{
	unsigned long mask1 = cpumask_bits(cpumask)[0];
	unsigned long mask2 = cpumask_bits(andmask)[0];
	unsigned long mask3 = cpumask_bits(cpu_online_mask)[0];

	return (unsigned int)(mask1 & mask2 & mask3);
}

static inline int default_phys_pkg_id(int cpuid_apic, int index_msb)
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

extern int default_apicid_to_node(int logical_apicid);

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

static inline int
__default_check_phys_apicid_present(int boot_cpu_physical_apicid)
{
	return physid_isset(boot_cpu_physical_apicid, phys_cpu_present_map);
}

#ifdef CONFIG_X86_32
static inline int default_cpu_present_to_apicid(int mps_cpu)
{
	return __default_cpu_present_to_apicid(mps_cpu);
}

static inline int
default_check_phys_apicid_present(int boot_cpu_physical_apicid)
{
	return __default_check_phys_apicid_present(boot_cpu_physical_apicid);
}
#else
extern int default_cpu_present_to_apicid(int mps_cpu);
extern int default_check_phys_apicid_present(int boot_cpu_physical_apicid);
#endif

static inline physid_mask_t default_apicid_to_cpu_present(int phys_apicid)
{
	return physid_mask_of_physid(phys_apicid);
}

#endif /* CONFIG_X86_LOCAL_APIC */

#endif /* _ASM_X86_GENAPIC_64_H */
