#ifndef __ASM_SUMMIT_APICDEF_H
#define __ASM_SUMMIT_APICDEF_H

static inline unsigned summit_get_apic_id(unsigned long x)
{
	return (x >> 24) & 0xFF;
}

#endif
