// SPDX-License-Identifier: GPL-2.0
#include <asm/paravirt.h>

DEF_NATIVE(irq, irq_disable, "cli");
DEF_NATIVE(irq, irq_enable, "sti");
DEF_NATIVE(irq, restore_fl, "push %eax; popf");
DEF_NATIVE(irq, save_fl, "pushf; pop %eax");
DEF_NATIVE(cpu, iret, "iret");
DEF_NATIVE(mmu, read_cr2, "mov %cr2, %eax");
DEF_NATIVE(mmu, write_cr3, "mov %eax, %cr3");
DEF_NATIVE(mmu, read_cr3, "mov %cr3, %eax");

#if defined(CONFIG_PARAVIRT_SPINLOCKS)
DEF_NATIVE(lock, queued_spin_unlock, "movb $0, (%eax)");
DEF_NATIVE(lock, vcpu_is_preempted, "xor %eax, %eax");
#endif

unsigned paravirt_patch_ident_32(void *insnbuf, unsigned len)
{
	/* arg in %eax, return in %eax */
	return 0;
}

unsigned paravirt_patch_ident_64(void *insnbuf, unsigned len)
{
	/* arg in %edx:%eax, return in %edx:%eax */
	return 0;
}

extern bool pv_is_native_spin_unlock(void);
extern bool pv_is_native_vcpu_is_preempted(void);

unsigned native_patch(u8 type, void *ibuf, unsigned long addr, unsigned len)
{
	const unsigned char *start, *end;
	unsigned ret;

#define PATCH_SITE(ops, x)					\
		case PARAVIRT_PATCH(ops.x):			\
			start = start_##ops##_##x;		\
			end = end_##ops##_##x;			\
			goto patch_site
	switch (type) {
		PATCH_SITE(irq, irq_disable);
		PATCH_SITE(irq, irq_enable);
		PATCH_SITE(irq, restore_fl);
		PATCH_SITE(irq, save_fl);
		PATCH_SITE(cpu, iret);
		PATCH_SITE(mmu, read_cr2);
		PATCH_SITE(mmu, read_cr3);
		PATCH_SITE(mmu, write_cr3);
#if defined(CONFIG_PARAVIRT_SPINLOCKS)
		case PARAVIRT_PATCH(lock.queued_spin_unlock):
			if (pv_is_native_spin_unlock()) {
				start = start_lock_queued_spin_unlock;
				end   = end_lock_queued_spin_unlock;
				goto patch_site;
			}
			goto patch_default;

		case PARAVIRT_PATCH(lock.vcpu_is_preempted):
			if (pv_is_native_vcpu_is_preempted()) {
				start = start_lock_vcpu_is_preempted;
				end   = end_lock_vcpu_is_preempted;
				goto patch_site;
			}
			goto patch_default;
#endif

	default:
patch_default: __maybe_unused
		ret = paravirt_patch_default(type, ibuf, addr, len);
		break;

patch_site:
		ret = paravirt_patch_insns(ibuf, len, start, end);
		break;
	}
#undef PATCH_SITE
	return ret;
}
