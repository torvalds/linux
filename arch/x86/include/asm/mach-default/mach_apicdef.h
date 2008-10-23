#ifndef _ASM_X86_MACH_DEFAULT_MACH_APICDEF_H
#define _ASM_X86_MACH_DEFAULT_MACH_APICDEF_H

#include <asm/apic.h>

#ifdef CONFIG_X86_64
#define	APIC_ID_MASK		(genapic->apic_id_mask)
#define GET_APIC_ID(x)		(genapic->get_apic_id(x))
#define	SET_APIC_ID(x)		(genapic->set_apic_id(x))
#else
#define		APIC_ID_MASK		(0xF<<24)
static inline unsigned get_apic_id(unsigned long x) 
{
	unsigned int ver = GET_APIC_VERSION(apic_read(APIC_LVR));
	if (APIC_XAPIC(ver))
		return (((x)>>24)&0xFF);
	else
		return (((x)>>24)&0xF);
} 

#define		GET_APIC_ID(x)	get_apic_id(x)
#endif

#endif /* _ASM_X86_MACH_DEFAULT_MACH_APICDEF_H */
