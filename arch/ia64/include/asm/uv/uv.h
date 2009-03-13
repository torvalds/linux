#ifndef _ASM_IA64_UV_UV_H
#define _ASM_IA64_UV_UV_H

#include <asm/system.h>
#include <asm/sn/simulator.h>

static inline int is_uv_system(void)
{
	/* temporary support for running on hardware simulator */
	return IS_MEDUSA() || ia64_platform_is("uv");
}

#endif	/* _ASM_IA64_UV_UV_H */
