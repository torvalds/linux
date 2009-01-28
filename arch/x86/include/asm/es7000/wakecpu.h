#ifndef __ASM_ES7000_WAKECPU_H
#define __ASM_ES7000_WAKECPU_H

#define ES7000_TRAMPOLINE_PHYS_LOW	0x467
#define ES7000_TRAMPOLINE_PHYS_HIGH	0x469

static inline void es7000_wait_for_init_deassert(atomic_t *deassert)
{
#ifndef CONFIG_ES7000_CLUSTERED_APIC
	while (!atomic_read(deassert))
		cpu_relax();
#endif
	return;
}

extern void __inquire_remote_apic(int apicid);

static inline void inquire_remote_apic(int apicid)
{
	if (apic_verbosity >= APIC_DEBUG)
		__inquire_remote_apic(apicid);
}

#endif /* __ASM_MACH_WAKECPU_H */
