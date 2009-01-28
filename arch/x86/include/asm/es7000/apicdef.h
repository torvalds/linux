#ifndef __ASM_ES7000_APICDEF_H
#define __ASM_ES7000_APICDEF_H

static inline unsigned int es7000_get_apic_id(unsigned long x)
{
	return (x >> 24) & 0xFF;
}

#endif
