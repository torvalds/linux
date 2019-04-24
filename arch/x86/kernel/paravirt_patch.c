// SPDX-License-Identifier: GPL-2.0
#include <linux/stringify.h>

#include <asm/paravirt.h>
#include <asm/asm-offsets.h>

#define PSTART(d, m)							\
	patch_data_##d.m

#define PEND(d, m)							\
	(PSTART(d, m) + sizeof(patch_data_##d.m))

#define PATCH(d, m, ibuf, len)						\
	paravirt_patch_insns(ibuf, len, PSTART(d, m), PEND(d, m))

#define PATCH_CASE(ops, m, data, ibuf, len)				\
	case PARAVIRT_PATCH(ops.m):					\
		return PATCH(data, ops##_##m, ibuf, len)

#ifdef CONFIG_PARAVIRT_XXL
struct patch_xxl {
	const unsigned char	irq_irq_disable[1];
	const unsigned char	irq_irq_enable[1];
	const unsigned char	irq_restore_fl[2];
	const unsigned char	irq_save_fl[2];
	const unsigned char	mmu_read_cr2[3];
	const unsigned char	mmu_read_cr3[3];
	const unsigned char	mmu_write_cr3[3];
# ifdef CONFIG_X86_64
	const unsigned char	cpu_wbinvd[2];
	const unsigned char	cpu_usergs_sysret64[6];
	const unsigned char	cpu_swapgs[3];
	const unsigned char	mov64[3];
# else
	const unsigned char	cpu_iret[1];
# endif
};

static const struct patch_xxl patch_data_xxl = {
	.irq_irq_disable	= { 0xfa },		// cli
	.irq_irq_enable		= { 0xfb },		// sti
	.irq_save_fl		= { 0x9c, 0x58 },	// pushf; pop %[re]ax
	.mmu_read_cr2		= { 0x0f, 0x20, 0xd0 },	// mov %cr2, %[re]ax
	.mmu_read_cr3		= { 0x0f, 0x20, 0xd8 },	// mov %cr3, %[re]ax
# ifdef CONFIG_X86_64
	.irq_restore_fl		= { 0x57, 0x9d },	// push %rdi; popfq
	.mmu_write_cr3		= { 0x0f, 0x22, 0xdf },	// mov %rdi, %cr3
	.cpu_wbinvd		= { 0x0f, 0x09 },	// wbinvd
	.cpu_usergs_sysret64	= { 0x0f, 0x01, 0xf8,
				    0x48, 0x0f, 0x07 },	// swapgs; sysretq
	.cpu_swapgs		= { 0x0f, 0x01, 0xf8 },	// swapgs
	.mov64			= { 0x48, 0x89, 0xf8 },	// mov %rdi, %rax
# else
	.irq_restore_fl		= { 0x50, 0x9d },	// push %eax; popf
	.mmu_write_cr3		= { 0x0f, 0x22, 0xd8 },	// mov %eax, %cr3
	.cpu_iret		= { 0xcf },		// iret
# endif
};

unsigned int paravirt_patch_ident_64(void *insnbuf, unsigned int len)
{
#ifdef CONFIG_X86_64
	return PATCH(xxl, mov64, insnbuf, len);
#endif
	return 0;
}
# endif /* CONFIG_PARAVIRT_XXL */

#ifdef CONFIG_PARAVIRT_SPINLOCKS
struct patch_lock {
	unsigned char queued_spin_unlock[3];
	unsigned char vcpu_is_preempted[2];
};

static const struct patch_lock patch_data_lock = {
	.vcpu_is_preempted	= { 0x31, 0xc0 },	// xor %eax, %eax

# ifdef CONFIG_X86_64
	.queued_spin_unlock	= { 0xc6, 0x07, 0x00 },	// movb $0, (%rdi)
# else
	.queued_spin_unlock	= { 0xc6, 0x00, 0x00 },	// movb $0, (%eax)
# endif
};
#endif /* CONFIG_PARAVIRT_SPINLOCKS */

unsigned int native_patch(u8 type, void *ibuf, unsigned long addr,
			  unsigned int len)
{
	switch (type) {

#ifdef CONFIG_PARAVIRT_XXL
	PATCH_CASE(irq, restore_fl, xxl, ibuf, len);
	PATCH_CASE(irq, save_fl, xxl, ibuf, len);
	PATCH_CASE(irq, irq_enable, xxl, ibuf, len);
	PATCH_CASE(irq, irq_disable, xxl, ibuf, len);

	PATCH_CASE(mmu, read_cr2, xxl, ibuf, len);
	PATCH_CASE(mmu, read_cr3, xxl, ibuf, len);
	PATCH_CASE(mmu, write_cr3, xxl, ibuf, len);

# ifdef CONFIG_X86_64
	PATCH_CASE(cpu, usergs_sysret64, xxl, ibuf, len);
	PATCH_CASE(cpu, swapgs, xxl, ibuf, len);
	PATCH_CASE(cpu, wbinvd, xxl, ibuf, len);
# else
	PATCH_CASE(cpu, iret, xxl, ibuf, len);
# endif
#endif

#ifdef CONFIG_PARAVIRT_SPINLOCKS
	case PARAVIRT_PATCH(lock.queued_spin_unlock):
		if (pv_is_native_spin_unlock())
			return PATCH(lock, queued_spin_unlock, ibuf, len);
		break;

	case PARAVIRT_PATCH(lock.vcpu_is_preempted):
		if (pv_is_native_vcpu_is_preempted())
			return PATCH(lock, vcpu_is_preempted, ibuf, len);
		break;
#endif
	default:
		break;
	}

	return paravirt_patch_default(type, ibuf, addr, len);
}
