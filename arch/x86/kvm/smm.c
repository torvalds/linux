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

static int emulator_has_longmode(struct x86_emulate_ctxt *ctxt)
{
#ifdef CONFIG_X86_64
	return ctxt->ops->guest_has_long_mode(ctxt);
#else
	return false;
#endif
}

static void rsm_set_desc_flags(struct desc_struct *desc, u32 flags)
{
	desc->g    = (flags >> 23) & 1;
	desc->d    = (flags >> 22) & 1;
	desc->l    = (flags >> 21) & 1;
	desc->avl  = (flags >> 20) & 1;
	desc->p    = (flags >> 15) & 1;
	desc->dpl  = (flags >> 13) & 3;
	desc->s    = (flags >> 12) & 1;
	desc->type = (flags >>  8) & 15;
}

static int rsm_load_seg_32(struct x86_emulate_ctxt *ctxt, const char *smstate,
			   int n)
{
	struct desc_struct desc;
	int offset;
	u16 selector;

	selector = GET_SMSTATE(u32, smstate, 0x7fa8 + n * 4);

	if (n < 3)
		offset = 0x7f84 + n * 12;
	else
		offset = 0x7f2c + (n - 3) * 12;

	set_desc_base(&desc,      GET_SMSTATE(u32, smstate, offset + 8));
	set_desc_limit(&desc,     GET_SMSTATE(u32, smstate, offset + 4));
	rsm_set_desc_flags(&desc, GET_SMSTATE(u32, smstate, offset));
	ctxt->ops->set_segment(ctxt, selector, &desc, 0, n);
	return X86EMUL_CONTINUE;
}

#ifdef CONFIG_X86_64
static int rsm_load_seg_64(struct x86_emulate_ctxt *ctxt, const char *smstate,
			   int n)
{
	struct desc_struct desc;
	int offset;
	u16 selector;
	u32 base3;

	offset = 0x7e00 + n * 16;

	selector =                GET_SMSTATE(u16, smstate, offset);
	rsm_set_desc_flags(&desc, GET_SMSTATE(u16, smstate, offset + 2) << 8);
	set_desc_limit(&desc,     GET_SMSTATE(u32, smstate, offset + 4));
	set_desc_base(&desc,      GET_SMSTATE(u32, smstate, offset + 8));
	base3 =                   GET_SMSTATE(u32, smstate, offset + 12);

	ctxt->ops->set_segment(ctxt, selector, &desc, base3, n);
	return X86EMUL_CONTINUE;
}
#endif

static int rsm_enter_protected_mode(struct x86_emulate_ctxt *ctxt,
				    u64 cr0, u64 cr3, u64 cr4)
{
	int bad;
	u64 pcid;

	/* In order to later set CR4.PCIDE, CR3[11:0] must be zero.  */
	pcid = 0;
	if (cr4 & X86_CR4_PCIDE) {
		pcid = cr3 & 0xfff;
		cr3 &= ~0xfff;
	}

	bad = ctxt->ops->set_cr(ctxt, 3, cr3);
	if (bad)
		return X86EMUL_UNHANDLEABLE;

	/*
	 * First enable PAE, long mode needs it before CR0.PG = 1 is set.
	 * Then enable protected mode.	However, PCID cannot be enabled
	 * if EFER.LMA=0, so set it separately.
	 */
	bad = ctxt->ops->set_cr(ctxt, 4, cr4 & ~X86_CR4_PCIDE);
	if (bad)
		return X86EMUL_UNHANDLEABLE;

	bad = ctxt->ops->set_cr(ctxt, 0, cr0);
	if (bad)
		return X86EMUL_UNHANDLEABLE;

	if (cr4 & X86_CR4_PCIDE) {
		bad = ctxt->ops->set_cr(ctxt, 4, cr4);
		if (bad)
			return X86EMUL_UNHANDLEABLE;
		if (pcid) {
			bad = ctxt->ops->set_cr(ctxt, 3, cr3 | pcid);
			if (bad)
				return X86EMUL_UNHANDLEABLE;
		}

	}

	return X86EMUL_CONTINUE;
}

static int rsm_load_state_32(struct x86_emulate_ctxt *ctxt,
			     const char *smstate)
{
	struct desc_struct desc;
	struct desc_ptr dt;
	u16 selector;
	u32 val, cr0, cr3, cr4;
	int i;

	cr0 =                      GET_SMSTATE(u32, smstate, 0x7ffc);
	cr3 =                      GET_SMSTATE(u32, smstate, 0x7ff8);
	ctxt->eflags =             GET_SMSTATE(u32, smstate, 0x7ff4) | X86_EFLAGS_FIXED;
	ctxt->_eip =               GET_SMSTATE(u32, smstate, 0x7ff0);

	for (i = 0; i < 8; i++)
		*reg_write(ctxt, i) = GET_SMSTATE(u32, smstate, 0x7fd0 + i * 4);

	val = GET_SMSTATE(u32, smstate, 0x7fcc);

	if (ctxt->ops->set_dr(ctxt, 6, val))
		return X86EMUL_UNHANDLEABLE;

	val = GET_SMSTATE(u32, smstate, 0x7fc8);

	if (ctxt->ops->set_dr(ctxt, 7, val))
		return X86EMUL_UNHANDLEABLE;

	selector =                 GET_SMSTATE(u32, smstate, 0x7fc4);
	set_desc_base(&desc,       GET_SMSTATE(u32, smstate, 0x7f64));
	set_desc_limit(&desc,      GET_SMSTATE(u32, smstate, 0x7f60));
	rsm_set_desc_flags(&desc,  GET_SMSTATE(u32, smstate, 0x7f5c));
	ctxt->ops->set_segment(ctxt, selector, &desc, 0, VCPU_SREG_TR);

	selector =                 GET_SMSTATE(u32, smstate, 0x7fc0);
	set_desc_base(&desc,       GET_SMSTATE(u32, smstate, 0x7f80));
	set_desc_limit(&desc,      GET_SMSTATE(u32, smstate, 0x7f7c));
	rsm_set_desc_flags(&desc,  GET_SMSTATE(u32, smstate, 0x7f78));
	ctxt->ops->set_segment(ctxt, selector, &desc, 0, VCPU_SREG_LDTR);

	dt.address =               GET_SMSTATE(u32, smstate, 0x7f74);
	dt.size =                  GET_SMSTATE(u32, smstate, 0x7f70);
	ctxt->ops->set_gdt(ctxt, &dt);

	dt.address =               GET_SMSTATE(u32, smstate, 0x7f58);
	dt.size =                  GET_SMSTATE(u32, smstate, 0x7f54);
	ctxt->ops->set_idt(ctxt, &dt);

	for (i = 0; i < 6; i++) {
		int r = rsm_load_seg_32(ctxt, smstate, i);
		if (r != X86EMUL_CONTINUE)
			return r;
	}

	cr4 = GET_SMSTATE(u32, smstate, 0x7f14);

	ctxt->ops->set_smbase(ctxt, GET_SMSTATE(u32, smstate, 0x7ef8));

	return rsm_enter_protected_mode(ctxt, cr0, cr3, cr4);
}

#ifdef CONFIG_X86_64
static int rsm_load_state_64(struct x86_emulate_ctxt *ctxt,
			     const char *smstate)
{
	struct desc_struct desc;
	struct desc_ptr dt;
	u64 val, cr0, cr3, cr4;
	u32 base3;
	u16 selector;
	int i, r;

	for (i = 0; i < 16; i++)
		*reg_write(ctxt, i) = GET_SMSTATE(u64, smstate, 0x7ff8 - i * 8);

	ctxt->_eip   = GET_SMSTATE(u64, smstate, 0x7f78);
	ctxt->eflags = GET_SMSTATE(u32, smstate, 0x7f70) | X86_EFLAGS_FIXED;

	val = GET_SMSTATE(u64, smstate, 0x7f68);

	if (ctxt->ops->set_dr(ctxt, 6, val))
		return X86EMUL_UNHANDLEABLE;

	val = GET_SMSTATE(u64, smstate, 0x7f60);

	if (ctxt->ops->set_dr(ctxt, 7, val))
		return X86EMUL_UNHANDLEABLE;

	cr0 =                       GET_SMSTATE(u64, smstate, 0x7f58);
	cr3 =                       GET_SMSTATE(u64, smstate, 0x7f50);
	cr4 =                       GET_SMSTATE(u64, smstate, 0x7f48);
	ctxt->ops->set_smbase(ctxt, GET_SMSTATE(u32, smstate, 0x7f00));
	val =                       GET_SMSTATE(u64, smstate, 0x7ed0);

	if (ctxt->ops->set_msr(ctxt, MSR_EFER, val & ~EFER_LMA))
		return X86EMUL_UNHANDLEABLE;

	selector =                  GET_SMSTATE(u32, smstate, 0x7e90);
	rsm_set_desc_flags(&desc,   GET_SMSTATE(u32, smstate, 0x7e92) << 8);
	set_desc_limit(&desc,       GET_SMSTATE(u32, smstate, 0x7e94));
	set_desc_base(&desc,        GET_SMSTATE(u32, smstate, 0x7e98));
	base3 =                     GET_SMSTATE(u32, smstate, 0x7e9c);
	ctxt->ops->set_segment(ctxt, selector, &desc, base3, VCPU_SREG_TR);

	dt.size =                   GET_SMSTATE(u32, smstate, 0x7e84);
	dt.address =                GET_SMSTATE(u64, smstate, 0x7e88);
	ctxt->ops->set_idt(ctxt, &dt);

	selector =                  GET_SMSTATE(u32, smstate, 0x7e70);
	rsm_set_desc_flags(&desc,   GET_SMSTATE(u32, smstate, 0x7e72) << 8);
	set_desc_limit(&desc,       GET_SMSTATE(u32, smstate, 0x7e74));
	set_desc_base(&desc,        GET_SMSTATE(u32, smstate, 0x7e78));
	base3 =                     GET_SMSTATE(u32, smstate, 0x7e7c);
	ctxt->ops->set_segment(ctxt, selector, &desc, base3, VCPU_SREG_LDTR);

	dt.size =                   GET_SMSTATE(u32, smstate, 0x7e64);
	dt.address =                GET_SMSTATE(u64, smstate, 0x7e68);
	ctxt->ops->set_gdt(ctxt, &dt);

	r = rsm_enter_protected_mode(ctxt, cr0, cr3, cr4);
	if (r != X86EMUL_CONTINUE)
		return r;

	for (i = 0; i < 6; i++) {
		r = rsm_load_seg_64(ctxt, smstate, i);
		if (r != X86EMUL_CONTINUE)
			return r;
	}

	return X86EMUL_CONTINUE;
}
#endif

int emulator_leave_smm(struct x86_emulate_ctxt *ctxt)
{
	struct kvm_vcpu *vcpu = ctxt->vcpu;
	unsigned long cr0, cr4, efer;
	char buf[512];
	u64 smbase;
	int ret;

	smbase = ctxt->ops->get_smbase(ctxt);

	ret = ctxt->ops->read_phys(ctxt, smbase + 0xfe00, buf, sizeof(buf));
	if (ret != X86EMUL_CONTINUE)
		return X86EMUL_UNHANDLEABLE;

	if ((ctxt->ops->get_hflags(ctxt) & X86EMUL_SMM_INSIDE_NMI_MASK) == 0)
		ctxt->ops->set_nmi_mask(ctxt, false);

	kvm_smm_changed(vcpu, false);

	/*
	 * Get back to real mode, to prepare a safe state in which to load
	 * CR0/CR3/CR4/EFER.  It's all a bit more complicated if the vCPU
	 * supports long mode.
	 *
	 * The ctxt->ops callbacks will handle all side effects when writing
	 * writing MSRs and CRs, e.g. MMU context resets, CPUID
	 * runtime updates, etc.
	 */
	if (emulator_has_longmode(ctxt)) {
		struct desc_struct cs_desc;

		/* Zero CR4.PCIDE before CR0.PG.  */
		cr4 = ctxt->ops->get_cr(ctxt, 4);
		if (cr4 & X86_CR4_PCIDE)
			ctxt->ops->set_cr(ctxt, 4, cr4 & ~X86_CR4_PCIDE);

		/* A 32-bit code segment is required to clear EFER.LMA.  */
		memset(&cs_desc, 0, sizeof(cs_desc));
		cs_desc.type = 0xb;
		cs_desc.s = cs_desc.g = cs_desc.p = 1;
		ctxt->ops->set_segment(ctxt, 0, &cs_desc, 0, VCPU_SREG_CS);
	}

	/* For the 64-bit case, this will clear EFER.LMA.  */
	cr0 = ctxt->ops->get_cr(ctxt, 0);
	if (cr0 & X86_CR0_PE)
		ctxt->ops->set_cr(ctxt, 0, cr0 & ~(X86_CR0_PG | X86_CR0_PE));

	if (emulator_has_longmode(ctxt)) {
		/* Clear CR4.PAE before clearing EFER.LME. */
		cr4 = ctxt->ops->get_cr(ctxt, 4);
		if (cr4 & X86_CR4_PAE)
			ctxt->ops->set_cr(ctxt, 4, cr4 & ~X86_CR4_PAE);

		/* And finally go back to 32-bit mode.  */
		efer = 0;
		ctxt->ops->set_msr(ctxt, MSR_EFER, efer);
	}

	/*
	 * Give leave_smm() a chance to make ISA-specific changes to the vCPU
	 * state (e.g. enter guest mode) before loading state from the SMM
	 * state-save area.
	 */
	if (static_call(kvm_x86_leave_smm)(vcpu, buf))
		return X86EMUL_UNHANDLEABLE;

#ifdef CONFIG_X86_64
	if (emulator_has_longmode(ctxt))
		return rsm_load_state_64(ctxt, buf);
	else
#endif
		return rsm_load_state_32(ctxt, buf);
}
