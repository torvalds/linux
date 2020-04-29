// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) Linaro.
 * Copyright (C) Huawei Futurewei Technologies.
 */

#include <linux/crash_core.h>
#include <asm/memory.h>

void arch_crash_save_vmcoreinfo(void)
{
	VMCOREINFO_NUMBER(VA_BITS);
	/* Please note VMCOREINFO_NUMBER() uses "%d", not "%x" */
	vmcoreinfo_append_str("NUMBER(kimage_voffset)=0x%llx\n",
						kimage_voffset);
	vmcoreinfo_append_str("NUMBER(PHYS_OFFSET)=0x%llx\n",
						PHYS_OFFSET);
	vmcoreinfo_append_str("KERNELOFFSET=%lx\n", kaslr_offset());
}
