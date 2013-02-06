#include <linux/types.h>
#include <mach/smc.h>

#ifndef __ASSEMBLY__
u32 exynos_smc(u32 cmd, u32 arg1, u32 arg2, u32 arg3)
{
	register u32 reg0 __asm__("r0") = cmd;
	register u32 reg1 __asm__("r1") = arg1;
	register u32 reg2 __asm__("r2") = arg2;
	register u32 reg3 __asm__("r3") = arg3;

	__asm__ volatile (
#ifdef REQUIRES_SEC
		".arch_extension sec\n"
#endif
		"smc	0\n"
		: "+r"(reg0), "+r"(reg1), "+r"(reg2), "+r"(reg3)
	);

	return reg0;
}

u32 exynos_smc_readsfr(u32 addr, u32 *val)
{
	register u32 reg0 __asm__("r0") = SMC_CMD_REG;
	register u32 reg1 __asm__("r1") = SMC_REG_ID_SFR_R(addr);
	register u32 reg2 __asm__("r2") = 0;
	register u32 reg3 __asm__("r3") = 0;

	__asm__ volatile (
#ifdef REQUIRES_SEC
		".arch_extension sec\n"
#endif
		"smc	0\n"
		: "+r"(reg0), "+r"(reg1), "+r"(reg2), "+r"(reg3)
	);

	if (reg0 == SMC_CMD_REG) {
		if (!reg1)
			*val = reg2;
		return reg1;
	}

	if (!reg0)
		*val = reg2;

	return reg0;
}

#endif


