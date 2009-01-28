#ifndef __ASM_ES7000_APICDEF_H
#define __ASM_ES7000_APICDEF_H

#define		ES7000_APIC_ID_MASK		(0xFF<<24)

static inline unsigned int es7000_get_apic_id(unsigned long x)
{
	return (x >> 24) & 0xFF;
}

#endif
