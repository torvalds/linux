/*
 * APIC driver for the IBM NUMAQ chipset.
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
#include <linux/numa.h>
#include <linux/smp.h>
#include <asm/numaq.h>
#include <asm/io.h>
#include <linux/mmzone.h>
#include <linux/nodemask.h>

#define NUMAQ_APIC_DFR_VALUE	(APIC_DFR_CLUSTER)

static inline unsigned int numaq_get_apic_id(unsigned long x)
{
	return (x >> 24) & 0x0F;
}

void default_send_IPI_mask_sequence(const struct cpumask *mask, int vector);
void default_send_IPI_mask_allbutself(const struct cpumask *mask, int vector);

static inline void numaq_send_IPI_mask(const struct cpumask *mask, int vector)
{
	default_send_IPI_mask_sequence(mask, vector);
}

static inline void numaq_send_IPI_allbutself(int vector)
{
	default_send_IPI_mask_allbutself(cpu_online_mask, vector);
}

static inline void numaq_send_IPI_all(int vector)
{
	numaq_send_IPI_mask(cpu_online_mask, vector);
}

extern void numaq_mps_oem_check(struct mpc_table *, char *, char *);

#define NUMAQ_TRAMPOLINE_PHYS_LOW (0x8)
#define NUMAQ_TRAMPOLINE_PHYS_HIGH (0xa)

/*
 * Because we use NMIs rather than the INIT-STARTUP sequence to
 * bootstrap the CPUs, the APIC may be in a weird state. Kick it:
 */
static inline void numaq_smp_callin_clear_local_apic(void)
{
	clear_local_APIC();
}

static inline void
numaq_store_NMI_vector(unsigned short *high, unsigned short *low)
{
	printk("Storing NMI vector\n");
	*high =
	  *((volatile unsigned short *)phys_to_virt(NUMAQ_TRAMPOLINE_PHYS_HIGH));
	*low =
	  *((volatile unsigned short *)phys_to_virt(NUMAQ_TRAMPOLINE_PHYS_LOW));
}

static inline const cpumask_t *numaq_target_cpus(void)
{
	return &CPU_MASK_ALL;
}

static inline unsigned long
numaq_check_apicid_used(physid_mask_t bitmap, int apicid)
{
	return physid_isset(apicid, bitmap);
}

static inline unsigned long numaq_check_apicid_present(int bit)
{
	return physid_isset(bit, phys_cpu_present_map);
}

#define apicid_cluster(apicid) (apicid & 0xF0)

static inline int numaq_apic_id_registered(void)
{
	return 1;
}

static inline void numaq_init_apic_ldr(void)
{
	/* Already done in NUMA-Q firmware */
}

static inline void numaq_setup_apic_routing(void)
{
	printk("Enabling APIC mode:  %s.  Using %d I/O APICs\n",
		"NUMA-Q", nr_ioapics);
}

/*
 * Skip adding the timer int on secondary nodes, which causes
 * a small but painful rift in the time-space continuum.
 */
static inline int numaq_multi_timer_check(int apic, int irq)
{
	return apic != 0 && irq == 0;
}

static inline physid_mask_t numaq_ioapic_phys_id_map(physid_mask_t phys_map)
{
	/* We don't have a good way to do this yet - hack */
	return physids_promote(0xFUL);
}

/* Mapping from cpu number to logical apicid */
extern u8 cpu_2_logical_apicid[];

static inline int numaq_cpu_to_logical_apicid(int cpu)
{
	if (cpu >= nr_cpu_ids)
		return BAD_APICID;
	return (int)cpu_2_logical_apicid[cpu];
}

/*
 * Supporting over 60 cpus on NUMA-Q requires a locality-dependent
 * cpu to APIC ID relation to properly interact with the intelligent
 * mode of the cluster controller.
 */
static inline int numaq_cpu_present_to_apicid(int mps_cpu)
{
	if (mps_cpu < 60)
		return ((mps_cpu >> 2) << 4) | (1 << (mps_cpu & 0x3));
	else
		return BAD_APICID;
}

static inline int numaq_apicid_to_node(int logical_apicid) 
{
	return logical_apicid >> 4;
}

static inline physid_mask_t numaq_apicid_to_cpu_present(int logical_apicid)
{
	int node = numaq_apicid_to_node(logical_apicid);
	int cpu = __ffs(logical_apicid & 0xf);

	return physid_mask_of_physid(cpu + 4*node);
}

extern void *xquad_portio;

static inline int numaq_check_phys_apicid_present(int boot_cpu_physical_apicid)
{
	return 1;
}

/*
 * We use physical apicids here, not logical, so just return the default
 * physical broadcast to stop people from breaking us
 */
static inline unsigned int numaq_cpu_mask_to_apicid(const cpumask_t *cpumask)
{
	return 0x0F;
}

static inline unsigned int
numaq_cpu_mask_to_apicid_and(const struct cpumask *cpumask,
			     const struct cpumask *andmask)
{
	return 0x0F;
}

/* No NUMA-Q box has a HT CPU, but it can't hurt to use the default code. */
static inline int numaq_phys_pkg_id(int cpuid_apic, int index_msb)
{
	return cpuid_apic >> index_msb;
}
static int __numaq_mps_oem_check(struct mpc_table *mpc, char *oem, char *productid)
{
	numaq_mps_oem_check(mpc, oem, productid);
	return found_numaq;
}

static int probe_numaq(void)
{
	/* already know from get_memcfg_numaq() */
	return found_numaq;
}

static void numaq_vector_allocation_domain(int cpu, cpumask_t *retmask)
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

static void numaq_setup_portio_remap(void)
{
	int num_quads = num_online_nodes();

	if (num_quads <= 1)
       		return;

	printk("Remapping cross-quad port I/O for %d quads\n", num_quads);
	xquad_portio = ioremap(XQUAD_PORTIO_BASE, num_quads*XQUAD_PORTIO_QUAD);
	printk("xquad_portio vaddr 0x%08lx, len %08lx\n",
		(u_long) xquad_portio, (u_long) num_quads*XQUAD_PORTIO_QUAD);
}

struct genapic apic_numaq = {

	.name				= "NUMAQ",
	.probe				= probe_numaq,
	.acpi_madt_oem_check		= NULL,
	.apic_id_registered		= numaq_apic_id_registered,

	.irq_delivery_mode		= dest_LowestPrio,
	/* physical delivery on LOCAL quad: */
	.irq_dest_mode			= 0,

	.target_cpus			= numaq_target_cpus,
	.disable_esr			= 1,
	.dest_logical			= APIC_DEST_LOGICAL,
	.check_apicid_used		= numaq_check_apicid_used,
	.check_apicid_present		= numaq_check_apicid_present,

	.vector_allocation_domain	= numaq_vector_allocation_domain,
	.init_apic_ldr			= numaq_init_apic_ldr,

	.ioapic_phys_id_map		= numaq_ioapic_phys_id_map,
	.setup_apic_routing		= numaq_setup_apic_routing,
	.multi_timer_check		= numaq_multi_timer_check,
	.apicid_to_node			= numaq_apicid_to_node,
	.cpu_to_logical_apicid		= numaq_cpu_to_logical_apicid,
	.cpu_present_to_apicid		= numaq_cpu_present_to_apicid,
	.apicid_to_cpu_present		= numaq_apicid_to_cpu_present,
	.setup_portio_remap		= numaq_setup_portio_remap,
	.check_phys_apicid_present	= numaq_check_phys_apicid_present,
	.enable_apic_mode		= NULL,
	.phys_pkg_id			= numaq_phys_pkg_id,
	.mps_oem_check			= __numaq_mps_oem_check,

	.get_apic_id			= numaq_get_apic_id,
	.set_apic_id			= NULL,
	.apic_id_mask			= 0x0F << 24,

	.cpu_mask_to_apicid		= numaq_cpu_mask_to_apicid,
	.cpu_mask_to_apicid_and		= numaq_cpu_mask_to_apicid_and,

	.send_IPI_mask			= numaq_send_IPI_mask,
	.send_IPI_mask_allbutself	= NULL,
	.send_IPI_allbutself		= numaq_send_IPI_allbutself,
	.send_IPI_all			= numaq_send_IPI_all,
	.send_IPI_self			= NULL,

	.wakeup_cpu			= NULL,
	.trampoline_phys_low		= NUMAQ_TRAMPOLINE_PHYS_LOW,
	.trampoline_phys_high		= NUMAQ_TRAMPOLINE_PHYS_HIGH,

	/* We don't do anything here because we use NMI's to boot instead */
	.wait_for_init_deassert		= NULL,

	.smp_callin_clear_local_apic	= numaq_smp_callin_clear_local_apic,
	.store_NMI_vector		= numaq_store_NMI_vector,
	.inquire_remote_apic		= NULL,
};
