/* SPDX-License-Identifier: GPL-2.0 */

#include <linux/kvm_host.h>
#include "x86.h"
#include "kvm_cache_regs.h"
#include "kvm_emulate.h"
#include "smm.h"
#include "cpuid.h"
#include "trace.h"

#define CHECK_SMRAM32_OFFSET(field, offset) \
	ASSERT_STRUCT_OFFSET(struct kvm_smram_state_32, field, offset - 0xFE00)

#define CHECK_SMRAM64_OFFSET(field, offset) \
	ASSERT_STRUCT_OFFSET(struct kvm_smram_state_64, field, offset - 0xFE00)

static void check_smram_offsets(void)
{
	/* 32 bit SMRAM image */
	CHECK_SMRAM32_OFFSET(reserved1,			0xFE00);
	CHECK_SMRAM32_OFFSET(smbase,			0xFEF8);
	CHECK_SMRAM32_OFFSET(smm_revision,		0xFEFC);
	CHECK_SMRAM32_OFFSET(io_inst_restart,		0xFF00);
	CHECK_SMRAM32_OFFSET(auto_hlt_restart,		0xFF02);
	CHECK_SMRAM32_OFFSET(io_restart_rdi,		0xFF04);
	CHECK_SMRAM32_OFFSET(io_restart_rcx,		0xFF08);
	CHECK_SMRAM32_OFFSET(io_restart_rsi,		0xFF0C);
	CHECK_SMRAM32_OFFSET(io_restart_rip,		0xFF10);
	CHECK_SMRAM32_OFFSET(cr4,			0xFF14);
	CHECK_SMRAM32_OFFSET(reserved3,			0xFF18);
	CHECK_SMRAM32_OFFSET(ds,			0xFF2C);
	CHECK_SMRAM32_OFFSET(fs,			0xFF38);
	CHECK_SMRAM32_OFFSET(gs,			0xFF44);
	CHECK_SMRAM32_OFFSET(idtr,			0xFF50);
	CHECK_SMRAM32_OFFSET(tr,			0xFF5C);
	CHECK_SMRAM32_OFFSET(gdtr,			0xFF6C);
	CHECK_SMRAM32_OFFSET(ldtr,			0xFF78);
	CHECK_SMRAM32_OFFSET(es,			0xFF84);
	CHECK_SMRAM32_OFFSET(cs,			0xFF90);
	CHECK_SMRAM32_OFFSET(ss,			0xFF9C);
	CHECK_SMRAM32_OFFSET(es_sel,			0xFFA8);
	CHECK_SMRAM32_OFFSET(cs_sel,			0xFFAC);
	CHECK_SMRAM32_OFFSET(ss_sel,			0xFFB0);
	CHECK_SMRAM32_OFFSET(ds_sel,			0xFFB4);
	CHECK_SMRAM32_OFFSET(fs_sel,			0xFFB8);
	CHECK_SMRAM32_OFFSET(gs_sel,			0xFFBC);
	CHECK_SMRAM32_OFFSET(ldtr_sel,			0xFFC0);
	CHECK_SMRAM32_OFFSET(tr_sel,			0xFFC4);
	CHECK_SMRAM32_OFFSET(dr7,			0xFFC8);
	CHECK_SMRAM32_OFFSET(dr6,			0xFFCC);
	CHECK_SMRAM32_OFFSET(gprs,			0xFFD0);
	CHECK_SMRAM32_OFFSET(eip,			0xFFF0);
	CHECK_SMRAM32_OFFSET(eflags,			0xFFF4);
	CHECK_SMRAM32_OFFSET(cr3,			0xFFF8);
	CHECK_SMRAM32_OFFSET(cr0,			0xFFFC);

	/* 64 bit SMRAM image */
	CHECK_SMRAM64_OFFSET(es,			0xFE00);
	CHECK_SMRAM64_OFFSET(cs,			0xFE10);
	CHECK_SMRAM64_OFFSET(ss,			0xFE20);
	CHECK_SMRAM64_OFFSET(ds,			0xFE30);
	CHECK_SMRAM64_OFFSET(fs,			0xFE40);
	CHECK_SMRAM64_OFFSET(gs,			0xFE50);
	CHECK_SMRAM64_OFFSET(gdtr,			0xFE60);
	CHECK_SMRAM64_OFFSET(ldtr,			0xFE70);
	CHECK_SMRAM64_OFFSET(idtr,			0xFE80);
	CHECK_SMRAM64_OFFSET(tr,			0xFE90);
	CHECK_SMRAM64_OFFSET(io_restart_rip,		0xFEA0);
	CHECK_SMRAM64_OFFSET(io_restart_rcx,		0xFEA8);
	CHECK_SMRAM64_OFFSET(io_restart_rsi,		0xFEB0);
	CHECK_SMRAM64_OFFSET(io_restart_rdi,		0xFEB8);
	CHECK_SMRAM64_OFFSET(io_restart_dword,		0xFEC0);
	CHECK_SMRAM64_OFFSET(reserved1,			0xFEC4);
	CHECK_SMRAM64_OFFSET(io_inst_restart,		0xFEC8);
	CHECK_SMRAM64_OFFSET(auto_hlt_restart,		0xFEC9);
	CHECK_SMRAM64_OFFSET(reserved2,			0xFECA);
	CHECK_SMRAM64_OFFSET(efer,			0xFED0);
	CHECK_SMRAM64_OFFSET(svm_guest_flag,		0xFED8);
	CHECK_SMRAM64_OFFSET(svm_guest_vmcb_gpa,	0xFEE0);
	CHECK_SMRAM64_OFFSET(svm_guest_virtual_int,	0xFEE8);
	CHECK_SMRAM64_OFFSET(reserved3,			0xFEF0);
	CHECK_SMRAM64_OFFSET(smm_revison,		0xFEFC);
	CHECK_SMRAM64_OFFSET(smbase,			0xFF00);
	CHECK_SMRAM64_OFFSET(reserved4,			0xFF04);
	CHECK_SMRAM64_OFFSET(ssp,			0xFF18);
	CHECK_SMRAM64_OFFSET(svm_guest_pat,		0xFF20);
	CHECK_SMRAM64_OFFSET(svm_host_efer,		0xFF28);
	CHECK_SMRAM64_OFFSET(svm_host_cr4,		0xFF30);
	CHECK_SMRAM64_OFFSET(svm_host_cr3,		0xFF38);
	CHECK_SMRAM64_OFFSET(svm_host_cr0,		0xFF40);
	CHECK_SMRAM64_OFFSET(cr4,			0xFF48);
	CHECK_SMRAM64_OFFSET(cr3,			0xFF50);
	CHECK_SMRAM64_OFFSET(cr0,			0xFF58);
	CHECK_SMRAM64_OFFSET(dr7,			0xFF60);
	CHECK_SMRAM64_OFFSET(dr6,			0xFF68);
	CHECK_SMRAM64_OFFSET(rflags,			0xFF70);
	CHECK_SMRAM64_OFFSET(rip,			0xFF78);
	CHECK_SMRAM64_OFFSET(gprs,			0xFF80);

	BUILD_BUG_ON(sizeof(union kvm_smram) != 512);
}

#undef CHECK_SMRAM64_OFFSET
#undef CHECK_SMRAM32_OFFSET


void kvm_smm_changed(struct kvm_vcpu *vcpu, bool entering_smm)
{
	BUILD_BUG_ON(HF_SMM_MASK != X86EMUL_SMM_MASK);

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

	check_smram_offsets();

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
	 *
	 * Kill the VM in the unlikely case of failure, because the VM
	 * can be in undefined state in this case.
	 */
	if (static_call(kvm_x86_enter_smm)(vcpu, buf))
		goto error;

	kvm_smm_changed(vcpu, true);

	if (kvm_vcpu_write_guest(vcpu, vcpu->arch.smbase + 0xfe00, buf, sizeof(buf)))
		goto error;

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

	if (WARN_ON_ONCE(kvm_set_dr(vcpu, 7, DR7_FIXED_1)))
		goto error;

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
		if (static_call(kvm_x86_set_efer)(vcpu, 0))
			goto error;
#endif

	kvm_update_cpuid_runtime(vcpu);
	kvm_mmu_reset_context(vcpu);
	return;
error:
	kvm_vm_dead(vcpu->kvm);
}

static void rsm_set_desc_flags(struct kvm_segment *desc, u32 flags)
{
	desc->g    = (flags >> 23) & 1;
	desc->db   = (flags >> 22) & 1;
	desc->l    = (flags >> 21) & 1;
	desc->avl  = (flags >> 20) & 1;
	desc->present = (flags >> 15) & 1;
	desc->dpl  = (flags >> 13) & 3;
	desc->s    = (flags >> 12) & 1;
	desc->type = (flags >>  8) & 15;

	desc->unusable = !desc->present;
	desc->padding = 0;
}

static int rsm_load_seg_32(struct kvm_vcpu *vcpu, const char *smstate,
			   int n)
{
	struct kvm_segment desc;
	int offset;

	if (n < 3)
		offset = 0x7f84 + n * 12;
	else
		offset = 0x7f2c + (n - 3) * 12;

	desc.selector =           GET_SMSTATE(u32, smstate, 0x7fa8 + n * 4);
	desc.base =               GET_SMSTATE(u32, smstate, offset + 8);
	desc.limit =              GET_SMSTATE(u32, smstate, offset + 4);
	rsm_set_desc_flags(&desc, GET_SMSTATE(u32, smstate, offset));
	kvm_set_segment(vcpu, &desc, n);
	return X86EMUL_CONTINUE;
}

#ifdef CONFIG_X86_64
static int rsm_load_seg_64(struct kvm_vcpu *vcpu, const char *smstate,
			   int n)
{
	struct kvm_segment desc;
	int offset;

	offset = 0x7e00 + n * 16;

	desc.selector =           GET_SMSTATE(u16, smstate, offset);
	rsm_set_desc_flags(&desc, GET_SMSTATE(u16, smstate, offset + 2) << 8);
	desc.limit =              GET_SMSTATE(u32, smstate, offset + 4);
	desc.base =               GET_SMSTATE(u64, smstate, offset + 8);
	kvm_set_segment(vcpu, &desc, n);
	return X86EMUL_CONTINUE;
}
#endif

static int rsm_enter_protected_mode(struct kvm_vcpu *vcpu,
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

	bad = kvm_set_cr3(vcpu, cr3);
	if (bad)
		return X86EMUL_UNHANDLEABLE;

	/*
	 * First enable PAE, long mode needs it before CR0.PG = 1 is set.
	 * Then enable protected mode.	However, PCID cannot be enabled
	 * if EFER.LMA=0, so set it separately.
	 */
	bad = kvm_set_cr4(vcpu, cr4 & ~X86_CR4_PCIDE);
	if (bad)
		return X86EMUL_UNHANDLEABLE;

	bad = kvm_set_cr0(vcpu, cr0);
	if (bad)
		return X86EMUL_UNHANDLEABLE;

	if (cr4 & X86_CR4_PCIDE) {
		bad = kvm_set_cr4(vcpu, cr4);
		if (bad)
			return X86EMUL_UNHANDLEABLE;
		if (pcid) {
			bad = kvm_set_cr3(vcpu, cr3 | pcid);
			if (bad)
				return X86EMUL_UNHANDLEABLE;
		}

	}

	return X86EMUL_CONTINUE;
}

static int rsm_load_state_32(struct x86_emulate_ctxt *ctxt,
			     const char *smstate)
{
	struct kvm_vcpu *vcpu = ctxt->vcpu;
	struct kvm_segment desc;
	struct desc_ptr dt;
	u32 val, cr0, cr3, cr4;
	int i;

	cr0 =                      GET_SMSTATE(u32, smstate, 0x7ffc);
	cr3 =                      GET_SMSTATE(u32, smstate, 0x7ff8);
	ctxt->eflags =             GET_SMSTATE(u32, smstate, 0x7ff4) | X86_EFLAGS_FIXED;
	ctxt->_eip =               GET_SMSTATE(u32, smstate, 0x7ff0);

	for (i = 0; i < 8; i++)
		*reg_write(ctxt, i) = GET_SMSTATE(u32, smstate, 0x7fd0 + i * 4);

	val = GET_SMSTATE(u32, smstate, 0x7fcc);

	if (kvm_set_dr(vcpu, 6, val))
		return X86EMUL_UNHANDLEABLE;

	val = GET_SMSTATE(u32, smstate, 0x7fc8);

	if (kvm_set_dr(vcpu, 7, val))
		return X86EMUL_UNHANDLEABLE;

	desc.selector =            GET_SMSTATE(u32, smstate, 0x7fc4);
	desc.base =                GET_SMSTATE(u32, smstate, 0x7f64);
	desc.limit =               GET_SMSTATE(u32, smstate, 0x7f60);
	rsm_set_desc_flags(&desc,  GET_SMSTATE(u32, smstate, 0x7f5c));
	kvm_set_segment(vcpu, &desc, VCPU_SREG_TR);

	desc.selector =            GET_SMSTATE(u32, smstate, 0x7fc0);
	desc.base =                GET_SMSTATE(u32, smstate, 0x7f80);
	desc.limit =               GET_SMSTATE(u32, smstate, 0x7f7c);
	rsm_set_desc_flags(&desc,  GET_SMSTATE(u32, smstate, 0x7f78));
	kvm_set_segment(vcpu, &desc, VCPU_SREG_LDTR);

	dt.address =               GET_SMSTATE(u32, smstate, 0x7f74);
	dt.size =                  GET_SMSTATE(u32, smstate, 0x7f70);
	static_call(kvm_x86_set_gdt)(vcpu, &dt);

	dt.address =               GET_SMSTATE(u32, smstate, 0x7f58);
	dt.size =                  GET_SMSTATE(u32, smstate, 0x7f54);
	static_call(kvm_x86_set_idt)(vcpu, &dt);

	for (i = 0; i < 6; i++) {
		int r = rsm_load_seg_32(vcpu, smstate, i);
		if (r != X86EMUL_CONTINUE)
			return r;
	}

	cr4 = GET_SMSTATE(u32, smstate, 0x7f14);

	vcpu->arch.smbase = GET_SMSTATE(u32, smstate, 0x7ef8);

	return rsm_enter_protected_mode(vcpu, cr0, cr3, cr4);
}

#ifdef CONFIG_X86_64
static int rsm_load_state_64(struct x86_emulate_ctxt *ctxt,
			     const char *smstate)
{
	struct kvm_vcpu *vcpu = ctxt->vcpu;
	struct kvm_segment desc;
	struct desc_ptr dt;
	u64 val, cr0, cr3, cr4;
	int i, r;

	for (i = 0; i < 16; i++)
		*reg_write(ctxt, i) = GET_SMSTATE(u64, smstate, 0x7ff8 - i * 8);

	ctxt->_eip   = GET_SMSTATE(u64, smstate, 0x7f78);
	ctxt->eflags = GET_SMSTATE(u32, smstate, 0x7f70) | X86_EFLAGS_FIXED;

	val = GET_SMSTATE(u64, smstate, 0x7f68);

	if (kvm_set_dr(vcpu, 6, val))
		return X86EMUL_UNHANDLEABLE;

	val = GET_SMSTATE(u64, smstate, 0x7f60);

	if (kvm_set_dr(vcpu, 7, val))
		return X86EMUL_UNHANDLEABLE;

	cr0 =                       GET_SMSTATE(u64, smstate, 0x7f58);
	cr3 =                       GET_SMSTATE(u64, smstate, 0x7f50);
	cr4 =                       GET_SMSTATE(u64, smstate, 0x7f48);
	vcpu->arch.smbase =         GET_SMSTATE(u32, smstate, 0x7f00);
	val =                       GET_SMSTATE(u64, smstate, 0x7ed0);

	if (kvm_set_msr(vcpu, MSR_EFER, val & ~EFER_LMA))
		return X86EMUL_UNHANDLEABLE;

	desc.selector =             GET_SMSTATE(u32, smstate, 0x7e90);
	rsm_set_desc_flags(&desc,   GET_SMSTATE(u32, smstate, 0x7e92) << 8);
	desc.limit =                GET_SMSTATE(u32, smstate, 0x7e94);
	desc.base =                 GET_SMSTATE(u64, smstate, 0x7e98);
	kvm_set_segment(vcpu, &desc, VCPU_SREG_TR);

	dt.size =                   GET_SMSTATE(u32, smstate, 0x7e84);
	dt.address =                GET_SMSTATE(u64, smstate, 0x7e88);
	static_call(kvm_x86_set_idt)(vcpu, &dt);

	desc.selector =             GET_SMSTATE(u32, smstate, 0x7e70);
	rsm_set_desc_flags(&desc,   GET_SMSTATE(u32, smstate, 0x7e72) << 8);
	desc.limit =                GET_SMSTATE(u32, smstate, 0x7e74);
	desc.base =                 GET_SMSTATE(u64, smstate, 0x7e78);
	kvm_set_segment(vcpu, &desc, VCPU_SREG_LDTR);

	dt.size =                   GET_SMSTATE(u32, smstate, 0x7e64);
	dt.address =                GET_SMSTATE(u64, smstate, 0x7e68);
	static_call(kvm_x86_set_gdt)(vcpu, &dt);

	r = rsm_enter_protected_mode(vcpu, cr0, cr3, cr4);
	if (r != X86EMUL_CONTINUE)
		return r;

	for (i = 0; i < 6; i++) {
		r = rsm_load_seg_64(vcpu, smstate, i);
		if (r != X86EMUL_CONTINUE)
			return r;
	}

	return X86EMUL_CONTINUE;
}
#endif

int emulator_leave_smm(struct x86_emulate_ctxt *ctxt)
{
	struct kvm_vcpu *vcpu = ctxt->vcpu;
	unsigned long cr0;
	char buf[512];
	u64 smbase;
	int ret;

	smbase = vcpu->arch.smbase;

	ret = kvm_vcpu_read_guest(vcpu, smbase + 0xfe00, buf, sizeof(buf));
	if (ret < 0)
		return X86EMUL_UNHANDLEABLE;

	if ((vcpu->arch.hflags & HF_SMM_INSIDE_NMI_MASK) == 0)
		static_call(kvm_x86_set_nmi_mask)(vcpu, false);

	kvm_smm_changed(vcpu, false);

	/*
	 * Get back to real mode, to prepare a safe state in which to load
	 * CR0/CR3/CR4/EFER.  It's all a bit more complicated if the vCPU
	 * supports long mode.
	 */
#ifdef CONFIG_X86_64
	if (guest_cpuid_has(vcpu, X86_FEATURE_LM)) {
		struct kvm_segment cs_desc;
		unsigned long cr4;

		/* Zero CR4.PCIDE before CR0.PG.  */
		cr4 = kvm_read_cr4(vcpu);
		if (cr4 & X86_CR4_PCIDE)
			kvm_set_cr4(vcpu, cr4 & ~X86_CR4_PCIDE);

		/* A 32-bit code segment is required to clear EFER.LMA.  */
		memset(&cs_desc, 0, sizeof(cs_desc));
		cs_desc.type = 0xb;
		cs_desc.s = cs_desc.g = cs_desc.present = 1;
		kvm_set_segment(vcpu, &cs_desc, VCPU_SREG_CS);
	}
#endif

	/* For the 64-bit case, this will clear EFER.LMA.  */
	cr0 = kvm_read_cr0(vcpu);
	if (cr0 & X86_CR0_PE)
		kvm_set_cr0(vcpu, cr0 & ~(X86_CR0_PG | X86_CR0_PE));

#ifdef CONFIG_X86_64
	if (guest_cpuid_has(vcpu, X86_FEATURE_LM)) {
		unsigned long cr4, efer;

		/* Clear CR4.PAE before clearing EFER.LME. */
		cr4 = kvm_read_cr4(vcpu);
		if (cr4 & X86_CR4_PAE)
			kvm_set_cr4(vcpu, cr4 & ~X86_CR4_PAE);

		/* And finally go back to 32-bit mode.  */
		efer = 0;
		kvm_set_msr(vcpu, MSR_EFER, efer);
	}
#endif

	/*
	 * Give leave_smm() a chance to make ISA-specific changes to the vCPU
	 * state (e.g. enter guest mode) before loading state from the SMM
	 * state-save area.
	 */
	if (static_call(kvm_x86_leave_smm)(vcpu, buf))
		return X86EMUL_UNHANDLEABLE;

#ifdef CONFIG_X86_64
	if (guest_cpuid_has(vcpu, X86_FEATURE_LM))
		return rsm_load_state_64(ctxt, buf);
	else
#endif
		return rsm_load_state_32(ctxt, buf);
}
