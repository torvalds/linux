#include <asm/mach/arch.h>

#include "core.h"

static const char * const v2m_dt_match[] __initconst = {
	"arm,vexpress",
	NULL,
};

DT_MACHINE_START(VEXPRESS_DT, "ARM-Versatile Express")
	.dt_compat	= v2m_dt_match,
	.l2c_aux_val	= 0x00400000,
	.l2c_aux_mask	= 0xfe0fffff,
	.smp		= smp_ops(vexpress_smp_dt_ops),
	.smp_init	= smp_init_ops(vexpress_smp_init_ops),
MACHINE_END
