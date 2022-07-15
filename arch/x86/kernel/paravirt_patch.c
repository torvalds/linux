// SPDX-License-Identifier: GPL-2.0
#include <linux/stringify.h>

#include <asm/paravirt.h>
#include <asm/asm-offsets.h>

#define PSTART(d, m)							\
	patch_data_##d.m

#define PEND(d, m)							\
	(PSTART(d, m) + sizeof(patch_data_##d.m))

#define PATCH(d, m, insn_buff, len)						\
	paravirt_patch_insns(insn_buff, len, PSTART(d, m), PEND(d, m))

#define PATCH_CASE(ops, m, data, insn_buff, len)				\
	case PARAVIRT_PATCH(ops.m):					\
		return PATCH(data, ops##_##m, insn_buff, len)

#ifdef CONFIG_PARAVIRT_XXL
struct patch_xxl {
	const unsigned char	irq_irq_disable[1];
	const unsigned char	irq_irq_enable[1];
	const unsigned char	irq_save_fl[2];
	const unsigned char	mmu_read_cr2[3];
	const unsigned char	mmu_read_cr3[3];
	const unsigned char	mmu_write_cr3[3];
	const unsigned char	irq_restore_fl[2];
	const unsigned char	cpu_wbinvd[2];
	const unsigned char	cpu_usergs_sysret64[6];
	const unsigned char	mov64[3];
};

static const struct patch_xxl patch_data_xxl = {
	.irq_irq_disable	= { 0xfa },		// cli
	.irq_irq_enable		= { 0xfb },		// sti
	.irq_save_fl		= { 0x9c, 0x58 },	// pushf; pop %[re]ax
	.mmu_read_cr2		= { 0x0f, 0x20, 0xd0 },	// mov %cr2, %[re]ax
	.mmu_read_cr3		= { 0x0f, 0x20, 0xd8 },	// mov %cr3, %[re]ax
	.mmu_write_cr3		= { 0x0f, 0x22, 0xdf },	// mov %rdi, %cr3
	.irq_restore_fl		= { 0x57, 0x9d },	// push %rdi; popfq
	.cpu_wbinvd		= { 0x0f, 0x09 },	// wbinvd
	.cpu_usergs_sysret64	= { 0x0f, 0x01, 0xf8,
				    0x48, 0x0f, 0x07 },	// swapgs; sysretq
	.mov64			= { 0x48, 0x89, 0xf8 },	// mov %rdi, %rax
};

unsigned int paravirt_patch_ident_64(void *insn_buff, unsigned int len)
{
	return PATCH(xxl, mov64, insn_buff, len);
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

unsigned int native_patch(u8 type, void *insn_buff, unsigned long addr,
			  unsigned int len)
{
	switch (type) {

#ifdef CONFIG_PARAVIRT_XXL
	PATCH_CASE(irq, restore_fl, xxl, insn_buff, len);
	PATCH_CASE(irq, save_fl, xxl, insn_buff, len);
	PATCH_CASE(irq, irq_enable, xxl, insn_buff, len);
	PATCH_CASE(irq, irq_disable, xxl, insn_buff, len);

	PATCH_CASE(mmu, read_cr2, xxl, insn_buff, len);
	PATCH_CASE(mmu, read_cr3, xxl, insn_buff, len);
	PATCH_CASE(mmu, write_cr3, xxl, insn_buff, len);

	PATCH_CASE(cpu, usergs_sysret64, xxl, insn_buff, len);
	PATCH_CASE(cpu, wbinvd, xxl, insn_buff, len);
#endif

#ifdef CONFIG_PARAVIRT_SPINLOCKS
	case PARAVIRT_PATCH(lock.queued_spin_unlock):
		if (pv_is_native_spin_unlock())
			return PATCH(lock, queued_spin_unlock, insn_buff, len);
		break;

	case PARAVIRT_PATCH(lock.vcpu_is_preempted):
		if (pv_is_native_vcpu_is_preempted())
			return PATCH(lock, vcpu_is_preempted, insn_buff, len);
		break;
#endif
	default:
		break;
	}

	return paravirt_patch_default(type, insn_buff, addr, len);
}
