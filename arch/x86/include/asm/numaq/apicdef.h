#ifndef __ASM_NUMAQ_APICDEF_H
#define __ASM_NUMAQ_APICDEF_H

#define NUMAQ_APIC_ID_MASK (0xF<<24)

static inline unsigned int numaq_get_apic_id(unsigned long x)
{
	return (x >> 24) & 0x0F;
}

#endif
