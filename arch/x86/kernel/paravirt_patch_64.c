// SPDX-License-Identifier: GPL-2.0
#include <asm/paravirt.h>
#include <asm/asm-offsets.h>
#include <linux/stringify.h>

#ifdef CONFIG_PARAVIRT_XXL
DEF_NATIVE(irq, irq_disable, "cli");
DEF_NATIVE(irq, irq_enable, "sti");
DEF_NATIVE(irq, restore_fl, "pushq %rdi; popfq");
DEF_NATIVE(irq, save_fl, "pushfq; popq %rax");
DEF_NATIVE(mmu, read_cr2, "movq %cr2, %rax");
DEF_NATIVE(mmu, read_cr3, "movq %cr3, %rax");
DEF_NATIVE(mmu, write_cr3, "movq %rdi, %cr3");
DEF_NATIVE(cpu, wbinvd, "wbinvd");

DEF_NATIVE(cpu, usergs_sysret64, "swapgs; sysretq");
DEF_NATIVE(cpu, swapgs, "swapgs");
#endif

DEF_NATIVE(, mov32, "mov %edi, %eax");
DEF_NATIVE(, mov64, "mov %rdi, %rax");

#if defined(CONFIG_PARAVIRT_SPINLOCKS)
DEF_NATIVE(lock, queued_spin_unlock, "movb $0, (%rdi)");
DEF_NATIVE(lock, vcpu_is_preempted, "xor %eax, %eax");
#endif

unsigned paravirt_patch_ident_32(void *insnbuf, unsigned len)
{
	return paravirt_patch_insns(insnbuf, len,
				    start__mov32, end__mov32);
}

unsigned paravirt_patch_ident_64(void *insnbuf, unsigned len)
{
	return paravirt_patch_insns(insnbuf, len,
				    start__mov64, end__mov64);
}

extern bool pv_is_native_spin_unlock(void);
extern bool pv_is_native_vcpu_is_preempted(void);

unsigned native_patch(u8 type, void *ibuf, unsigned long addr, unsigned len)
{
#define PATCH_SITE(ops, x)					\
	case PARAVIRT_PATCH(ops.x):				\
		return paravirt_patch_insns(ibuf, len, start_##ops##_##x, end_##ops##_##x)

	switch (type) {
#ifdef CONFIG_PARAVIRT_XXL
		PATCH_SITE(irq, restore_fl);
		PATCH_SITE(irq, save_fl);
		PATCH_SITE(irq, irq_enable);
		PATCH_SITE(irq, irq_disable);
		PATCH_SITE(cpu, usergs_sysret64);
		PATCH_SITE(cpu, swapgs);
		PATCH_SITE(cpu, wbinvd);
		PATCH_SITE(mmu, read_cr2);
		PATCH_SITE(mmu, read_cr3);
		PATCH_SITE(mmu, write_cr3);
#endif
#if defined(CONFIG_PARAVIRT_SPINLOCKS)
	case PARAVIRT_PATCH(lock.queued_spin_unlock):
		if (pv_is_native_spin_unlock())
			return paravirt_patch_insns(ibuf, len,
						    start_lock_queued_spin_unlock,
						    end_lock_queued_spin_unlock);
		break;

	case PARAVIRT_PATCH(lock.vcpu_is_preempted):
		if (pv_is_native_vcpu_is_preempted())
			return paravirt_patch_insns(ibuf, len,
						    start_lock_vcpu_is_preempted,
						    end_lock_vcpu_is_preempted);
		break;
#endif

	default:
		break;
	}
#undef PATCH_SITE
	return paravirt_patch_default(type, ibuf, addr, len);
}
