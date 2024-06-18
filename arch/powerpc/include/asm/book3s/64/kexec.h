/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _ASM_POWERPC_BOOK3S_64_KEXEC_H_
#define _ASM_POWERPC_BOOK3S_64_KEXEC_H_

#include <asm/plpar_wrappers.h>

#define reset_sprs reset_sprs
static inline void reset_sprs(void)
{
	if (cpu_has_feature(CPU_FTR_ARCH_206)) {
		mtspr(SPRN_AMR, 0);
		mtspr(SPRN_UAMOR, 0);
	}

	if (cpu_has_feature(CPU_FTR_ARCH_207S)) {
		mtspr(SPRN_IAMR, 0);
		if (cpu_has_feature(CPU_FTR_HVMODE))
			mtspr(SPRN_CIABR, 0);
		else
			plpar_set_ciabr(0);
	}

	if (cpu_has_feature(CPU_FTR_ARCH_31)) {
		mtspr(SPRN_DEXCR, 0);
		mtspr(SPRN_HASHKEYR, 0);
	}

	/*  Do we need isync()? We are going via a kexec reset */
	isync();
}

#endif
