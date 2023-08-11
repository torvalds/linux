// SPDX-License-Identifier: GPL-2.0-only

#include <linux/vmcore_info.h>
#include <asm/abs_lowcore.h>
#include <linux/mm.h>
#include <asm/setup.h>

void arch_crash_save_vmcoreinfo(void)
{
	struct lowcore *abs_lc;

	VMCOREINFO_SYMBOL(lowcore_ptr);
	VMCOREINFO_SYMBOL(high_memory);
	VMCOREINFO_LENGTH(lowcore_ptr, NR_CPUS);
	vmcoreinfo_append_str("SAMODE31=%lx\n", (unsigned long)__samode31);
	vmcoreinfo_append_str("EAMODE31=%lx\n", (unsigned long)__eamode31);
	vmcoreinfo_append_str("IDENTITYBASE=%lx\n", __identity_base);
	vmcoreinfo_append_str("KERNELOFFSET=%lx\n", kaslr_offset());
	vmcoreinfo_append_str("KERNELOFFPHYS=%lx\n", __kaslr_offset_phys);
	abs_lc = get_abs_lowcore();
	abs_lc->vmcore_info = paddr_vmcoreinfo_note();
	put_abs_lowcore(abs_lc);
}
