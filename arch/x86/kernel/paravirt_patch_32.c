#include <asm/paravirt.h>

DEF_NATIVE(pv_irq_ops, irq_disable, "cli");
DEF_NATIVE(pv_irq_ops, irq_enable, "sti");
DEF_NATIVE(pv_irq_ops, restore_fl, "push %eax; popf");
DEF_NATIVE(pv_irq_ops, save_fl, "pushf; pop %eax");
DEF_NATIVE(pv_cpu_ops, iret, "iret");
DEF_NATIVE(pv_cpu_ops, irq_enable_sysexit, "sti; sysexit");
DEF_NATIVE(pv_mmu_ops, read_cr2, "mov %cr2, %eax");
DEF_NATIVE(pv_mmu_ops, write_cr3, "mov %eax, %cr3");
DEF_NATIVE(pv_mmu_ops, read_cr3, "mov %cr3, %eax");
DEF_NATIVE(pv_cpu_ops, clts, "clts");
DEF_NATIVE(pv_cpu_ops, read_tsc, "rdtsc");

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

unsigned native_patch(u8 type, u16 clobbers, void *ibuf,
		      unsigned long addr, unsigned len)
{
	const unsigned char *start, *end;
	unsigned ret;

#define PATCH_SITE(ops, x)					\
		case PARAVIRT_PATCH(ops.x):			\
			start = start_##ops##_##x;		\
			end = end_##ops##_##x;			\
			goto patch_site
	switch (type) {
		PATCH_SITE(pv_irq_ops, irq_disable);
		PATCH_SITE(pv_irq_ops, irq_enable);
		PATCH_SITE(pv_irq_ops, restore_fl);
		PATCH_SITE(pv_irq_ops, save_fl);
		PATCH_SITE(pv_cpu_ops, iret);
		PATCH_SITE(pv_cpu_ops, irq_enable_sysexit);
		PATCH_SITE(pv_mmu_ops, read_cr2);
		PATCH_SITE(pv_mmu_ops, read_cr3);
		PATCH_SITE(pv_mmu_ops, write_cr3);
		PATCH_SITE(pv_cpu_ops, clts);
		PATCH_SITE(pv_cpu_ops, read_tsc);

	patch_site:
		ret = paravirt_patch_insns(ibuf, len, start, end);
		break;

	default:
		ret = paravirt_patch_default(type, clobbers, ibuf, addr, len);
		break;
	}
#undef PATCH_SITE
	return ret;
}
