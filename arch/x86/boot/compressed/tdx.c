// SPDX-License-Identifier: GPL-2.0

#include "../cpuflags.h"
#include "../string.h"

#include <asm/shared/tdx.h>

void early_tdx_detect(void)
{
	u32 eax, sig[3];

	cpuid_count(TDX_CPUID_LEAF_ID, 0, &eax, &sig[0], &sig[2],  &sig[1]);

	if (memcmp(TDX_IDENT, sig, sizeof(sig)))
		return;
}
