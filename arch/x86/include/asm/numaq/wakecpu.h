#ifndef __ASM_NUMAQ_WAKECPU_H
#define __ASM_NUMAQ_WAKECPU_H

/* This file copes with machines that wakeup secondary CPUs by NMIs */

#define TRAMPOLINE_PHYS_LOW (0x8)
#define TRAMPOLINE_PHYS_HIGH (0xa)

/* We don't do anything here because we use NMI's to boot instead */
static inline void wait_for_init_deassert(atomic_t *deassert)
{
}

/*
 * Because we use NMIs rather than the INIT-STARTUP sequence to
 * bootstrap the CPUs, the APIC may be in a weird state. Kick it.
 */
static inline void smp_callin_clear_local_apic(void)
{
	clear_local_APIC();
}

static inline void store_NMI_vector(unsigned short *high, unsigned short *low)
{
	printk("Storing NMI vector\n");
	*high =
	  *((volatile unsigned short *)phys_to_virt(TRAMPOLINE_PHYS_HIGH));
	*low =
	  *((volatile unsigned short *)phys_to_virt(TRAMPOLINE_PHYS_LOW));
}

static inline void restore_NMI_vector(unsigned short *high, unsigned short *low)
{
	printk("Restoring NMI vector\n");
	*((volatile unsigned short *)phys_to_virt(TRAMPOLINE_PHYS_HIGH)) =
								 *high;
	*((volatile unsigned short *)phys_to_virt(TRAMPOLINE_PHYS_LOW)) =
								 *low;
}

static inline void inquire_remote_apic(int apicid)
{
}

#endif /* __ASM_NUMAQ_WAKECPU_H */
