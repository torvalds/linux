/* SPDX-License-Identifier: GPL-2.0 */

#include <linux/kvm_host.h>
#include "x86.h"
#include "kvm_cache_regs.h"
#include "kvm_emulate.h"
#include "smm.h"
#include "cpuid.h"
#include "trace.h"

void kvm_smm_changed(struct kvm_vcpu *vcpu, bool entering_smm)
{
	trace_kvm_smm_transition(vcpu->vcpu_id, vcpu->arch.smbase, entering_smm);

	if (entering_smm) {
		vcpu->arch.hflags |= HF_SMM_MASK;
	} else {
		vcpu->arch.hflags &= ~(HF_SMM_MASK | HF_SMM_INSIDE_NMI_MASK);

		/* Process a latched INIT or SMI, if any.  */
		kvm_make_request(KVM_REQ_EVENT, vcpu);

		/*
		 * Even if KVM_SET_SREGS2 loaded PDPTRs out of band,
		 * on SMM exit we still need to reload them from
		 * guest memory
		 */
		vcpu->arch.pdptrs_from_userspace = false;
	}

	kvm_mmu_reset_context(vcpu);
}

void process_smi(struct kvm_vcpu *vcpu)
{
	vcpu->arch.smi_pending = true;
	kvm_make_request(KVM_REQ_EVENT, vcpu);
}

static u32 enter_smm_get_segment_flags(struct kvm_segment *seg)
{
	u32 flags = 0;
	flags |= seg->g       << 23;
	flags |= seg->db      << 22;
	flags |= seg->l       << 21;
	flags |= seg->avl     << 20;
	flags |= seg->present << 15;
	flags |= seg->dpl     << 13;
	flags |= seg->s       << 12;
	flags |= seg->type    << 8;
	return flags;
}

static void enter_smm_save_seg_32(struct kvm_vcpu *vcpu, char *buf, int n)
{
	struct kvm_segment seg;
	int offset;

	kvm_get_segment(vcpu, &seg, n);
	PUT_SMSTATE(u32, buf, 0x7fa8 + n * 4, seg.selector);

	if (n < 3)
		offset = 0x7f84 + n * 12;
	else
		offset = 0x7f2c + (n - 3) * 12;

	PUT_SMSTATE(u32, buf, offset + 8, seg.base);
	PUT_SMSTATE(u32, buf, offset + 4, seg.limit);
	PUT_SMSTATE(u32, buf, offset, enter_smm_get_segment_flags(&seg));
}

#ifdef CONFIG_X86_64
static void enter_smm_save_seg_64(struct kvm_vcpu *vcpu, char *buf, int n)
{
	struct kvm_segment seg;
	int offset;
	u16 flags;

	kvm_get_segment(vcpu, &seg, n);
	offset = 0x7e00 + n * 16;

	flags = enter_smm_get_segment_flags(&seg) >> 8;
	PUT_SMSTATE(u16, buf, offset, seg.selector);
	PUT_SMSTATE(u16, buf, offset + 2, flags);
	PUT_SMSTATE(u32, buf, offset + 4, seg.limit);
	PUT_SMSTATE(u64, buf, offset + 8, seg.base);
}
#endif

static void enter_smm_save_state_32(struct kvm_vcpu *vcpu, char *buf)
{
	struct desc_ptr dt;
	struct kvm_segment seg;
	unsigned long val;
	int i;

	PUT_SMSTATE(u32, buf, 0x7ffc, kvm_read_cr0(vcpu));
	PUT_SMSTATE(u32, buf, 0x7ff8, kvm_read_cr3(vcpu));
	PUT_SMSTATE(u32, buf, 0x7ff4, kvm_get_rflags(vcpu));
	PUT_SMSTATE(u32, buf, 0x7ff0, kvm_rip_read(vcpu));

	for (i = 0; i < 8; i++)
		PUT_SMSTATE(u32, buf, 0x7fd0 + i * 4, kvm_register_read_raw(vcpu, i));

	kvm_get_dr(vcpu, 6, &val);
	PUT_SMSTATE(u32, buf, 0x7fcc, (u32)val);
	kvm_get_dr(vcpu, 7, &val);
	PUT_SMSTATE(u32, buf, 0x7fc8, (u32)val);

	kvm_get_segment(vcpu, &seg, VCPU_SREG_TR);
	PUT_SMSTATE(u32, buf, 0x7fc4, seg.selector);
	PUT_SMSTATE(u32, buf, 0x7f64, seg.base);
	PUT_SMSTATE(u32, buf, 0x7f60, seg.limit);
	PUT_SMSTATE(u32, buf, 0x7f5c, enter_smm_get_segment_flags(&seg));

	kvm_get_segment(vcpu, &seg, VCPU_SREG_LDTR);
	PUT_SMSTATE(u32, buf, 0x7fc0, seg.selector);
	PUT_SMSTATE(u32, buf, 0x7f80, seg.base);
	PUT_SMSTATE(u32, buf, 0x7f7c, seg.limit);
	PUT_SMSTATE(u32, buf, 0x7f78, enter_smm_get_segment_flags(&seg));

	static_call(kvm_x86_get_gdt)(vcpu, &dt);
	PUT_SMSTATE(u32, buf, 0x7f74, dt.address);
	PUT_SMSTATE(u32, buf, 0x7f70, dt.size);

	static_call(kvm_x86_get_idt)(vcpu, &dt);
	PUT_SMSTATE(u32, buf, 0x7f58, dt.address);
	PUT_SMSTATE(u32, buf, 0x7f54, dt.size);

	for (i = 0; i < 6; i++)
		enter_smm_save_seg_32(vcpu, buf, i);

	PUT_SMSTATE(u32, buf, 0x7f14, kvm_read_cr4(vcpu));

	/* revision id */
	PUT_SMSTATE(u32, buf, 0x7efc, 0x00020000);
	PUT_SMSTATE(u32, buf, 0x7ef8, vcpu->arch.smbase);
}

#ifdef CONFIG_X86_64
static void enter_smm_save_state_64(struct kvm_vcpu *vcpu, char *buf)
{
	struct desc_ptr dt;
	struct kvm_segment seg;
	unsigned long val;
	int i;

	for (i = 0; i < 16; i++)
		PUT_SMSTATE(u64, buf, 0x7ff8 - i * 8, kvm_register_read_raw(vcpu, i));

	PUT_SMSTATE(u64, buf, 0x7f78, kvm_rip_read(vcpu));
	PUT_SMSTATE(u32, buf, 0x7f70, kvm_get_rflags(vcpu));

	kvm_get_dr(vcpu, 6, &val);
	PUT_SMSTATE(u64, buf, 0x7f68, val);
	kvm_get_dr(vcpu, 7, &val);
	PUT_SMSTATE(u64, buf, 0x7f60, val);

	PUT_SMSTATE(u64, buf, 0x7f58, kvm_read_cr0(vcpu));
	PUT_SMSTATE(u64, buf, 0x7f50, kvm_read_cr3(vcpu));
	PUT_SMSTATE(u64, buf, 0x7f48, kvm_read_cr4(vcpu));

	PUT_SMSTATE(u32, buf, 0x7f00, vcpu->arch.smbase);

	/* revision id */
	PUT_SMSTATE(u32, buf, 0x7efc, 0x00020064);

	PUT_SMSTATE(u64, buf, 0x7ed0, vcpu->arch.efer);

	kvm_get_segment(vcpu, &seg, VCPU_SREG_TR);
	PUT_SMSTATE(u16, buf, 0x7e90, seg.selector);
	PUT_SMSTATE(u16, buf, 0x7e92, enter_smm_get_segment_flags(&seg) >> 8);
	PUT_SMSTATE(u32, buf, 0x7e94, seg.limit);
	PUT_SMSTATE(u64, buf, 0x7e98, seg.base);

	static_call(kvm_x86_get_idt)(vcpu, &dt);
	PUT_SMSTATE(u32, buf, 0x7e84, dt.size);
	PUT_SMSTATE(u64, buf, 0x7e88, dt.address);

	kvm_get_segment(vcpu, &seg, VCPU_SREG_LDTR);
	PUT_SMSTATE(u16, buf, 0x7e70, seg.selector);
	PUT_SMSTATE(u16, buf, 0x7e72, enter_smm_get_segment_flags(&seg) >> 8);
	PUT_SMSTATE(u32, buf, 0x7e74, seg.limit);
	PUT_SMSTATE(u64, buf, 0x7e78, seg.base);

	static_call(kvm_x86_get_gdt)(vcpu, &dt);
	PUT_SMSTATE(u32, buf, 0x7e64, dt.size);
	PUT_SMSTATE(u64, buf, 0x7e68, dt.address);

	for (i = 0; i < 6; i++)
		enter_smm_save_seg_64(vcpu, buf, i);
}
#endif

void enter_smm(struct kvm_vcpu *vcpu)
{
	struct kvm_segment cs, ds;
	struct desc_ptr dt;
	unsigned long cr0;
	char buf[512];

	memset(buf, 0, 512);
#ifdef CONFIG_X86_64
	if (guest_cpuid_has(vcpu, X86_FEATURE_LM))
		enter_smm_save_state_64(vcpu, buf);
	else
#endif
		enter_smm_save_state_32(vcpu, buf);

	/*
	 * Give enter_smm() a chance to make ISA-specific changes to the vCPU
	 * state (e.g. leave guest mode) after we've saved the state into the
	 * SMM state-save area.
	 */
	static_call(kvm_x86_enter_smm)(vcpu, buf);

	kvm_smm_changed(vcpu, true);
	kvm_vcpu_write_guest(vcpu, vcpu->arch.smbase + 0xfe00, buf, sizeof(buf));

	if (static_call(kvm_x86_get_nmi_mask)(vcpu))
		vcpu->arch.hflags |= HF_SMM_INSIDE_NMI_MASK;
	else
		static_call(kvm_x86_set_nmi_mask)(vcpu, true);

	kvm_set_rflags(vcpu, X86_EFLAGS_FIXED);
	kvm_rip_write(vcpu, 0x8000);

	cr0 = vcpu->arch.cr0 & ~(X86_CR0_PE | X86_CR0_EM | X86_CR0_TS | X86_CR0_PG);
	static_call(kvm_x86_set_cr0)(vcpu, cr0);
	vcpu->arch.cr0 = cr0;

	static_call(kvm_x86_set_cr4)(vcpu, 0);

	/* Undocumented: IDT limit is set to zero on entry to SMM.  */
	dt.address = dt.size = 0;
	static_call(kvm_x86_set_idt)(vcpu, &dt);

	kvm_set_dr(vcpu, 7, DR7_FIXED_1);

	cs.selector = (vcpu->arch.smbase >> 4) & 0xffff;
	cs.base = vcpu->arch.smbase;

	ds.selector = 0;
	ds.base = 0;

	cs.limit    = ds.limit = 0xffffffff;
	cs.type     = ds.type = 0x3;
	cs.dpl      = ds.dpl = 0;
	cs.db       = ds.db = 0;
	cs.s        = ds.s = 1;
	cs.l        = ds.l = 0;
	cs.g        = ds.g = 1;
	cs.avl      = ds.avl = 0;
	cs.present  = ds.present = 1;
	cs.unusable = ds.unusable = 0;
	cs.padding  = ds.padding = 0;

	kvm_set_segment(vcpu, &cs, VCPU_SREG_CS);
	kvm_set_segment(vcpu, &ds, VCPU_SREG_DS);
	kvm_set_segment(vcpu, &ds, VCPU_SREG_ES);
	kvm_set_segment(vcpu, &ds, VCPU_SREG_FS);
	kvm_set_segment(vcpu, &ds, VCPU_SREG_GS);
	kvm_set_segment(vcpu, &ds, VCPU_SREG_SS);

#ifdef CONFIG_X86_64
	if (guest_cpuid_has(vcpu, X86_FEATURE_LM))
		static_call(kvm_x86_set_efer)(vcpu, 0);
#endif

	kvm_update_cpuid_runtime(vcpu);
	kvm_mmu_reset_context(vcpu);
}
