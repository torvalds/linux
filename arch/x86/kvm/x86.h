#ifndef ARCH_X86_KVM_X86_H
#define ARCH_X86_KVM_X86_H

#include <linux/kvm_host.h>
#include "kvm_cache_regs.h"

#define MSR_IA32_CR_PAT_DEFAULT  0x0007040600070406ULL

static inline void kvm_clear_exception_queue(struct kvm_vcpu *vcpu)
{
	vcpu->arch.exception.pending = false;
}

static inline void kvm_queue_interrupt(struct kvm_vcpu *vcpu, u8 vector,
	bool soft)
{
	vcpu->arch.interrupt.pending = true;
	vcpu->arch.interrupt.soft = soft;
	vcpu->arch.interrupt.nr = vector;
}

static inline void kvm_clear_interrupt_queue(struct kvm_vcpu *vcpu)
{
	vcpu->arch.interrupt.pending = false;
}

static inline bool kvm_event_needs_reinjection(struct kvm_vcpu *vcpu)
{
	return vcpu->arch.exception.pending || vcpu->arch.interrupt.pending ||
		vcpu->arch.nmi_injected;
}

static inline bool kvm_exception_is_soft(unsigned int nr)
{
	return (nr == BP_VECTOR) || (nr == OF_VECTOR);
}

static inline bool is_protmode(struct kvm_vcpu *vcpu)
{
	return kvm_read_cr0_bits(vcpu, X86_CR0_PE);
}

static inline int is_long_mode(struct kvm_vcpu *vcpu)
{
#ifdef CONFIG_X86_64
	return vcpu->arch.efer & EFER_LMA;
#else
	return 0;
#endif
}

static inline bool is_64_bit_mode(struct kvm_vcpu *vcpu)
{
	int cs_db, cs_l;

	if (!is_long_mode(vcpu))
		return false;
	kvm_x86_ops->get_cs_db_l_bits(vcpu, &cs_db, &cs_l);
	return cs_l;
}

static inline bool mmu_is_nested(struct kvm_vcpu *vcpu)
{
	return vcpu->arch.walk_mmu == &vcpu->arch.nested_mmu;
}

static inline int is_pae(struct kvm_vcpu *vcpu)
{
	return kvm_read_cr4_bits(vcpu, X86_CR4_PAE);
}

static inline int is_pse(struct kvm_vcpu *vcpu)
{
	return kvm_read_cr4_bits(vcpu, X86_CR4_PSE);
}

static inline int is_paging(struct kvm_vcpu *vcpu)
{
	return likely(kvm_read_cr0_bits(vcpu, X86_CR0_PG));
}

static inline u32 bit(int bitno)
{
	return 1 << (bitno & 31);
}

static inline void vcpu_cache_mmio_info(struct kvm_vcpu *vcpu,
					gva_t gva, gfn_t gfn, unsigned access)
{
	vcpu->arch.mmio_gva = gva & PAGE_MASK;
	vcpu->arch.access = access;
	vcpu->arch.mmio_gfn = gfn;
	vcpu->arch.mmio_gen = kvm_memslots(vcpu->kvm)->generation;
}

static inline bool vcpu_match_mmio_gen(struct kvm_vcpu *vcpu)
{
	return vcpu->arch.mmio_gen == kvm_memslots(vcpu->kvm)->generation;
}

/*
 * Clear the mmio cache info for the given gva. If gva is MMIO_GVA_ANY, we
 * clear all mmio cache info.
 */
#define MMIO_GVA_ANY (~(gva_t)0)

static inline void vcpu_clear_mmio_info(struct kvm_vcpu *vcpu, gva_t gva)
{
	if (gva != MMIO_GVA_ANY && vcpu->arch.mmio_gva != (gva & PAGE_MASK))
		return;

	vcpu->arch.mmio_gva = 0;
}

static inline bool vcpu_match_mmio_gva(struct kvm_vcpu *vcpu, unsigned long gva)
{
	if (vcpu_match_mmio_gen(vcpu) && vcpu->arch.mmio_gva &&
	      vcpu->arch.mmio_gva == (gva & PAGE_MASK))
		return true;

	return false;
}

static inline bool vcpu_match_mmio_gpa(struct kvm_vcpu *vcpu, gpa_t gpa)
{
	if (vcpu_match_mmio_gen(vcpu) && vcpu->arch.mmio_gfn &&
	      vcpu->arch.mmio_gfn == gpa >> PAGE_SHIFT)
		return true;

	return false;
}

static inline unsigned long kvm_register_readl(struct kvm_vcpu *vcpu,
					       enum kvm_reg reg)
{
	unsigned long val = kvm_register_read(vcpu, reg);

	return is_64_bit_mode(vcpu) ? val : (u32)val;
}

static inline void kvm_register_writel(struct kvm_vcpu *vcpu,
				       enum kvm_reg reg,
				       unsigned long val)
{
	if (!is_64_bit_mode(vcpu))
		val = (u32)val;
	return kvm_register_write(vcpu, reg, val);
}

static inline bool kvm_check_has_quirk(struct kvm *kvm, u64 quirk)
{
	return !(kvm->arch.disabled_quirks & quirk);
}

void kvm_before_handle_nmi(struct kvm_vcpu *vcpu);
void kvm_after_handle_nmi(struct kvm_vcpu *vcpu);
void kvm_set_pending_timer(struct kvm_vcpu *vcpu);
int kvm_inject_realmode_interrupt(struct kvm_vcpu *vcpu, int irq, int inc_eip);

void kvm_write_tsc(struct kvm_vcpu *vcpu, struct msr_data *msr);

int kvm_read_guest_virt(struct x86_emulate_ctxt *ctxt,
	gva_t addr, void *val, unsigned int bytes,
	struct x86_exception *exception);

int kvm_write_guest_virt_system(struct x86_emulate_ctxt *ctxt,
	gva_t addr, void *val, unsigned int bytes,
	struct x86_exception *exception);

void kvm_vcpu_mtrr_init(struct kvm_vcpu *vcpu);
u8 kvm_mtrr_get_guest_memory_type(struct kvm_vcpu *vcpu, gfn_t gfn);
bool kvm_mtrr_valid(struct kvm_vcpu *vcpu, u32 msr, u64 data);
int kvm_mtrr_set_msr(struct kvm_vcpu *vcpu, u32 msr, u64 data);
int kvm_mtrr_get_msr(struct kvm_vcpu *vcpu, u32 msr, u64 *pdata);
bool kvm_mtrr_check_gfn_range_consistency(struct kvm_vcpu *vcpu, gfn_t gfn,
					  int page_num);

#define KVM_SUPPORTED_XCR0     (XSTATE_FP | XSTATE_SSE | XSTATE_YMM \
				| XSTATE_BNDREGS | XSTATE_BNDCSR \
				| XSTATE_AVX512)
extern u64 host_xcr0;

extern u64 kvm_supported_xcr0(void);

extern unsigned int min_timer_period_us;

extern unsigned int lapic_timer_advance_ns;

extern struct static_key kvm_no_apic_vcpu;
#endif
