/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ASM_KVM_SMM_H
#define ASM_KVM_SMM_H

#include <linux/build_bug.h>

#ifdef CONFIG_KVM_SMM


/*
 * 32 bit KVM's emulated SMM layout. Based on Intel P6 layout
 * (https://www.sandpile.org/x86/smm.htm).
 */

struct kvm_smm_seg_state_32 {
	u32 flags;
	u32 limit;
	u32 base;
} __packed;

struct kvm_smram_state_32 {
	u32 reserved1[62];
	u32 smbase;
	u32 smm_revision;
	u16 io_inst_restart;
	u16 auto_hlt_restart;
	u32 io_restart_rdi;
	u32 io_restart_rcx;
	u32 io_restart_rsi;
	u32 io_restart_rip;
	u32 cr4;

	/* A20M#, CPL, shutdown and other reserved/undocumented fields */
	u16 reserved2;
	u8 int_shadow; /* KVM extension */
	u8 reserved3[17];

	struct kvm_smm_seg_state_32 ds;
	struct kvm_smm_seg_state_32 fs;
	struct kvm_smm_seg_state_32 gs;
	struct kvm_smm_seg_state_32 idtr; /* IDTR has only base and limit */
	struct kvm_smm_seg_state_32 tr;
	u32 reserved;
	struct kvm_smm_seg_state_32 gdtr; /* GDTR has only base and limit */
	struct kvm_smm_seg_state_32 ldtr;
	struct kvm_smm_seg_state_32 es;
	struct kvm_smm_seg_state_32 cs;
	struct kvm_smm_seg_state_32 ss;

	u32 es_sel;
	u32 cs_sel;
	u32 ss_sel;
	u32 ds_sel;
	u32 fs_sel;
	u32 gs_sel;
	u32 ldtr_sel;
	u32 tr_sel;

	u32 dr7;
	u32 dr6;
	u32 gprs[8]; /* GPRS in the "natural" X86 order (EAX/ECX/EDX.../EDI) */
	u32 eip;
	u32 eflags;
	u32 cr3;
	u32 cr0;
} __packed;


/* 64 bit KVM's emulated SMM layout. Based on AMD64 layout */

struct kvm_smm_seg_state_64 {
	u16 selector;
	u16 attributes;
	u32 limit;
	u64 base;
};

struct kvm_smram_state_64 {

	struct kvm_smm_seg_state_64 es;
	struct kvm_smm_seg_state_64 cs;
	struct kvm_smm_seg_state_64 ss;
	struct kvm_smm_seg_state_64 ds;
	struct kvm_smm_seg_state_64 fs;
	struct kvm_smm_seg_state_64 gs;
	struct kvm_smm_seg_state_64 gdtr; /* GDTR has only base and limit*/
	struct kvm_smm_seg_state_64 ldtr;
	struct kvm_smm_seg_state_64 idtr; /* IDTR has only base and limit*/
	struct kvm_smm_seg_state_64 tr;

	/* I/O restart and auto halt restart are not implemented by KVM */
	u64 io_restart_rip;
	u64 io_restart_rcx;
	u64 io_restart_rsi;
	u64 io_restart_rdi;
	u32 io_restart_dword;
	u32 reserved1;
	u8 io_inst_restart;
	u8 auto_hlt_restart;
	u8 amd_nmi_mask; /* Documented in AMD BKDG as NMI mask, not used by KVM */
	u8 int_shadow;
	u32 reserved2;

	u64 efer;

	/*
	 * Two fields below are implemented on AMD only, to store
	 * SVM guest vmcb address if the #SMI was received while in the guest mode.
	 */
	u64 svm_guest_flag;
	u64 svm_guest_vmcb_gpa;
	u64 svm_guest_virtual_int; /* unknown purpose, not implemented */

	u32 reserved3[3];
	u32 smm_revison;
	u32 smbase;
	u32 reserved4[5];

	/* ssp and svm_* fields below are not implemented by KVM */
	u64 ssp;
	u64 svm_guest_pat;
	u64 svm_host_efer;
	u64 svm_host_cr4;
	u64 svm_host_cr3;
	u64 svm_host_cr0;

	u64 cr4;
	u64 cr3;
	u64 cr0;
	u64 dr7;
	u64 dr6;
	u64 rflags;
	u64 rip;
	u64 gprs[16]; /* GPRS in a reversed "natural" X86 order (R15/R14/../RCX/RAX.) */
};

union kvm_smram {
	struct kvm_smram_state_64 smram64;
	struct kvm_smram_state_32 smram32;
	u8 bytes[512];
};

static inline int kvm_inject_smi(struct kvm_vcpu *vcpu)
{
	kvm_make_request(KVM_REQ_SMI, vcpu);
	return 0;
}

static inline bool is_smm(struct kvm_vcpu *vcpu)
{
	return vcpu->arch.hflags & HF_SMM_MASK;
}

void kvm_smm_changed(struct kvm_vcpu *vcpu, bool in_smm);
void enter_smm(struct kvm_vcpu *vcpu);
int emulator_leave_smm(struct x86_emulate_ctxt *ctxt);
void process_smi(struct kvm_vcpu *vcpu);
#else
static inline int kvm_inject_smi(struct kvm_vcpu *vcpu) { return -ENOTTY; }
static inline bool is_smm(struct kvm_vcpu *vcpu) { return false; }

/*
 * emulator_leave_smm is used as a function pointer, so the
 * stub is defined in x86.c.
 */
#endif

#endif
