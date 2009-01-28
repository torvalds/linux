#ifndef __ASM_MACH_APICDEF_H
#define __ASM_MACH_APICDEF_H

#define		BIGSMP_APIC_ID_MASK		(0xFF<<24)

static inline unsigned bigsmp_get_apic_id(unsigned long x)
{
	return (x >> 24) & 0xFF;
}

#endif
