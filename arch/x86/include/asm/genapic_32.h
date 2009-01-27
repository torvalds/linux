#ifndef _ASM_X86_GENAPIC_32_H
#define _ASM_X86_GENAPIC_32_H

#include <asm/mpspec.h>
#include <asm/atomic.h>

#define APICFUNC(x) .x = x,

/* More functions could be probably marked IPIFUNC and save some space
   in UP GENERICARCH kernels, but I don't have the nerve right now
   to untangle this mess. -AK  */
#ifdef CONFIG_SMP
#define IPIFUNC(x) APICFUNC(x)
#else
#define IPIFUNC(x)
#endif

#define APIC_INIT(aname, aprobe)			\
{							\
	.name = aname,					\
	.probe = aprobe,				\
	.int_delivery_mode = INT_DELIVERY_MODE,		\
	.int_dest_mode = INT_DEST_MODE,			\
	.no_balance_irq = NO_BALANCE_IRQ,		\
	.ESR_DISABLE = esr_disable,			\
	.apic_destination_logical = APIC_DEST_LOGICAL,	\
	APICFUNC(apic_id_registered)			\
	APICFUNC(target_cpus)				\
	APICFUNC(check_apicid_used)			\
	APICFUNC(check_apicid_present)			\
	APICFUNC(init_apic_ldr)				\
	APICFUNC(ioapic_phys_id_map)			\
	APICFUNC(setup_apic_routing)			\
	APICFUNC(multi_timer_check)			\
	APICFUNC(apicid_to_node)			\
	APICFUNC(cpu_to_logical_apicid)			\
	APICFUNC(cpu_present_to_apicid)			\
	APICFUNC(apicid_to_cpu_present)			\
	APICFUNC(setup_portio_remap)			\
	APICFUNC(check_phys_apicid_present)		\
	APICFUNC(mps_oem_check)				\
	APICFUNC(get_apic_id)				\
	.apic_id_mask = APIC_ID_MASK,			\
	APICFUNC(cpu_mask_to_apicid)			\
	APICFUNC(cpu_mask_to_apicid_and)		\
	APICFUNC(vector_allocation_domain)		\
	APICFUNC(acpi_madt_oem_check)			\
	IPIFUNC(send_IPI_mask)				\
	IPIFUNC(send_IPI_allbutself)			\
	IPIFUNC(send_IPI_all)				\
	APICFUNC(enable_apic_mode)			\
	APICFUNC(phys_pkg_id)				\
	.trampoline_phys_low = TRAMPOLINE_PHYS_LOW,		\
	.trampoline_phys_high = TRAMPOLINE_PHYS_HIGH,		\
	APICFUNC(wait_for_init_deassert)		\
	APICFUNC(smp_callin_clear_local_apic)		\
	APICFUNC(store_NMI_vector)			\
	APICFUNC(restore_NMI_vector)			\
	APICFUNC(inquire_remote_apic)			\
}

extern struct genapic *genapic;
extern void es7000_update_genapic_to_cluster(void);

#endif /* _ASM_X86_GENAPIC_32_H */
