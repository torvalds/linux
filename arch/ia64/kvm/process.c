/*
 * process.c: handle interruption inject for guests.
 * Copyright (c) 2005, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307 USA.
 *
 *  	Shaofan Li (Susue Li) <susie.li@intel.com>
 *  	Xiaoyan Feng (Fleming Feng)  <fleming.feng@intel.com>
 *  	Xuefei Xu (Anthony Xu) (Anthony.xu@intel.com)
 *  	Xiantao Zhang (xiantao.zhang@intel.com)
 */
#include "vcpu.h"

#include <asm/pal.h>
#include <asm/sal.h>
#include <asm/fpswa.h>
#include <asm/kregs.h>
#include <asm/tlb.h>

fpswa_interface_t *vmm_fpswa_interface;

#define IA64_VHPT_TRANS_VECTOR			0x0000
#define IA64_INST_TLB_VECTOR			0x0400
#define IA64_DATA_TLB_VECTOR			0x0800
#define IA64_ALT_INST_TLB_VECTOR		0x0c00
#define IA64_ALT_DATA_TLB_VECTOR		0x1000
#define IA64_DATA_NESTED_TLB_VECTOR		0x1400
#define IA64_INST_KEY_MISS_VECTOR		0x1800
#define IA64_DATA_KEY_MISS_VECTOR		0x1c00
#define IA64_DIRTY_BIT_VECTOR			0x2000
#define IA64_INST_ACCESS_BIT_VECTOR		0x2400
#define IA64_DATA_ACCESS_BIT_VECTOR		0x2800
#define IA64_BREAK_VECTOR			0x2c00
#define IA64_EXTINT_VECTOR			0x3000
#define IA64_PAGE_NOT_PRESENT_VECTOR		0x5000
#define IA64_KEY_PERMISSION_VECTOR		0x5100
#define IA64_INST_ACCESS_RIGHTS_VECTOR		0x5200
#define IA64_DATA_ACCESS_RIGHTS_VECTOR		0x5300
#define IA64_GENEX_VECTOR			0x5400
#define IA64_DISABLED_FPREG_VECTOR		0x5500
#define IA64_NAT_CONSUMPTION_VECTOR		0x5600
#define IA64_SPECULATION_VECTOR		0x5700 /* UNUSED */
#define IA64_DEBUG_VECTOR			0x5900
#define IA64_UNALIGNED_REF_VECTOR		0x5a00
#define IA64_UNSUPPORTED_DATA_REF_VECTOR	0x5b00
#define IA64_FP_FAULT_VECTOR			0x5c00
#define IA64_FP_TRAP_VECTOR			0x5d00
#define IA64_LOWERPRIV_TRANSFER_TRAP_VECTOR 	0x5e00
#define IA64_TAKEN_BRANCH_TRAP_VECTOR		0x5f00
#define IA64_SINGLE_STEP_TRAP_VECTOR		0x6000

/* SDM vol2 5.5 - IVA based interruption handling */
#define INITIAL_PSR_VALUE_AT_INTERRUPTION (IA64_PSR_UP | IA64_PSR_MFL |\
			IA64_PSR_MFH | IA64_PSR_PK | IA64_PSR_DT |    	\
			IA64_PSR_RT | IA64_PSR_MC|IA64_PSR_IT)

#define DOMN_PAL_REQUEST    0x110000
#define DOMN_SAL_REQUEST    0x110001

static u64 vec2off[68] = {0x0, 0x400, 0x800, 0xc00, 0x1000, 0x1400, 0x1800,
	0x1c00, 0x2000, 0x2400, 0x2800, 0x2c00, 0x3000, 0x3400, 0x3800, 0x3c00,
	0x4000, 0x4400, 0x4800, 0x4c00, 0x5000, 0x5100, 0x5200, 0x5300, 0x5400,
	0x5500, 0x5600, 0x5700, 0x5800, 0x5900, 0x5a00, 0x5b00, 0x5c00, 0x5d00,
	0x5e00, 0x5f00, 0x6000, 0x6100, 0x6200, 0x6300, 0x6400, 0x6500, 0x6600,
	0x6700, 0x6800, 0x6900, 0x6a00, 0x6b00, 0x6c00, 0x6d00, 0x6e00, 0x6f00,
	0x7000, 0x7100, 0x7200, 0x7300, 0x7400, 0x7500, 0x7600, 0x7700, 0x7800,
	0x7900, 0x7a00, 0x7b00, 0x7c00, 0x7d00, 0x7e00, 0x7f00
};

static void collect_interruption(struct kvm_vcpu *vcpu)
{
	u64 ipsr;
	u64 vdcr;
	u64 vifs;
	unsigned long vpsr;
	struct kvm_pt_regs *regs = vcpu_regs(vcpu);

	vpsr = vcpu_get_psr(vcpu);
	vcpu_bsw0(vcpu);
	if (vpsr & IA64_PSR_IC) {

		/* Sync mpsr id/da/dd/ss/ed bits to vipsr
		 * since after guest do rfi, we still want these bits on in
		 * mpsr
		 */

		ipsr = regs->cr_ipsr;
		vpsr = vpsr | (ipsr & (IA64_PSR_ID | IA64_PSR_DA
					| IA64_PSR_DD | IA64_PSR_SS
					| IA64_PSR_ED));
		vcpu_set_ipsr(vcpu, vpsr);

		/* Currently, for trap, we do not advance IIP to next
		 * instruction. That's because we assume caller already
		 * set up IIP correctly
		 */

		vcpu_set_iip(vcpu , regs->cr_iip);

		/* set vifs.v to zero */
		vifs = VCPU(vcpu, ifs);
		vifs &= ~IA64_IFS_V;
		vcpu_set_ifs(vcpu, vifs);

		vcpu_set_iipa(vcpu, VMX(vcpu, cr_iipa));
	}

	vdcr = VCPU(vcpu, dcr);

	/* Set guest psr
	 * up/mfl/mfh/pk/dt/rt/mc/it keeps unchanged
	 * be: set to the value of dcr.be
	 * pp: set to the value of dcr.pp
	 */
	vpsr &= INITIAL_PSR_VALUE_AT_INTERRUPTION;
	vpsr |= (vdcr & IA64_DCR_BE);

	/* VDCR pp bit position is different from VPSR pp bit */
	if (vdcr & IA64_DCR_PP) {
		vpsr |= IA64_PSR_PP;
	} else {
		vpsr &= ~IA64_PSR_PP;;
	}

	vcpu_set_psr(vcpu, vpsr);

}

void inject_guest_interruption(struct kvm_vcpu *vcpu, u64 vec)
{
	u64 viva;
	struct kvm_pt_regs *regs;
	union ia64_isr pt_isr;

	regs = vcpu_regs(vcpu);

	/* clear cr.isr.ir (incomplete register frame)*/
	pt_isr.val = VMX(vcpu, cr_isr);
	pt_isr.ir = 0;
	VMX(vcpu, cr_isr) = pt_isr.val;

	collect_interruption(vcpu);

	viva = vcpu_get_iva(vcpu);
	regs->cr_iip = viva + vec;
}

static u64 vcpu_get_itir_on_fault(struct kvm_vcpu *vcpu, u64 ifa)
{
	union ia64_rr rr, rr1;

	rr.val = vcpu_get_rr(vcpu, ifa);
	rr1.val = 0;
	rr1.ps = rr.ps;
	rr1.rid = rr.rid;
	return (rr1.val);
}

/*
 * Set vIFA & vITIR & vIHA, when vPSR.ic =1
 * Parameter:
 *  set_ifa: if true, set vIFA
 *  set_itir: if true, set vITIR
 *  set_iha: if true, set vIHA
 */
void set_ifa_itir_iha(struct kvm_vcpu *vcpu, u64 vadr,
		int set_ifa, int set_itir, int set_iha)
{
	long vpsr;
	u64 value;

	vpsr = VCPU(vcpu, vpsr);
	/* Vol2, Table 8-1 */
	if (vpsr & IA64_PSR_IC) {
		if (set_ifa)
			vcpu_set_ifa(vcpu, vadr);
		if (set_itir) {
			value = vcpu_get_itir_on_fault(vcpu, vadr);
			vcpu_set_itir(vcpu, value);
		}

		if (set_iha) {
			value = vcpu_thash(vcpu, vadr);
			vcpu_set_iha(vcpu, value);
		}
	}
}

/*
 * Data TLB Fault
 *  @ Data TLB vector
 * Refer to SDM Vol2 Table 5-6 & 8-1
 */
void dtlb_fault(struct kvm_vcpu *vcpu, u64 vadr)
{
	/* If vPSR.ic, IFA, ITIR, IHA */
	set_ifa_itir_iha(vcpu, vadr, 1, 1, 1);
	inject_guest_interruption(vcpu, IA64_DATA_TLB_VECTOR);
}

/*
 * Instruction TLB Fault
 *  @ Instruction TLB vector
 * Refer to SDM Vol2 Table 5-6 & 8-1
 */
void itlb_fault(struct kvm_vcpu *vcpu, u64 vadr)
{
	/* If vPSR.ic, IFA, ITIR, IHA */
	set_ifa_itir_iha(vcpu, vadr, 1, 1, 1);
	inject_guest_interruption(vcpu, IA64_INST_TLB_VECTOR);
}

/*
 * Data Nested TLB Fault
 *  @ Data Nested TLB Vector
 * Refer to SDM Vol2 Table 5-6 & 8-1
 */
void nested_dtlb(struct kvm_vcpu *vcpu)
{
	inject_guest_interruption(vcpu, IA64_DATA_NESTED_TLB_VECTOR);
}

/*
 * Alternate Data TLB Fault
 *  @ Alternate Data TLB vector
 * Refer to SDM Vol2 Table 5-6 & 8-1
 */
void alt_dtlb(struct kvm_vcpu *vcpu, u64 vadr)
{
	set_ifa_itir_iha(vcpu, vadr, 1, 1, 0);
	inject_guest_interruption(vcpu, IA64_ALT_DATA_TLB_VECTOR);
}

/*
 * Data TLB Fault
 *  @ Data TLB vector
 * Refer to SDM Vol2 Table 5-6 & 8-1
 */
void alt_itlb(struct kvm_vcpu *vcpu, u64 vadr)
{
	set_ifa_itir_iha(vcpu, vadr, 1, 1, 0);
	inject_guest_interruption(vcpu, IA64_ALT_INST_TLB_VECTOR);
}

/* Deal with:
 *  VHPT Translation Vector
 */
static void _vhpt_fault(struct kvm_vcpu *vcpu, u64 vadr)
{
	/* If vPSR.ic, IFA, ITIR, IHA*/
	set_ifa_itir_iha(vcpu, vadr, 1, 1, 1);
	inject_guest_interruption(vcpu, IA64_VHPT_TRANS_VECTOR);
}

/*
 * VHPT Instruction Fault
 *  @ VHPT Translation vector
 * Refer to SDM Vol2 Table 5-6 & 8-1
 */
void ivhpt_fault(struct kvm_vcpu *vcpu, u64 vadr)
{
	_vhpt_fault(vcpu, vadr);
}

/*
 * VHPT Data Fault
 *  @ VHPT Translation vector
 * Refer to SDM Vol2 Table 5-6 & 8-1
 */
void dvhpt_fault(struct kvm_vcpu *vcpu, u64 vadr)
{
	_vhpt_fault(vcpu, vadr);
}

/*
 * Deal with:
 *  General Exception vector
 */
void _general_exception(struct kvm_vcpu *vcpu)
{
	inject_guest_interruption(vcpu, IA64_GENEX_VECTOR);
}

/*
 * Illegal Operation Fault
 *  @ General Exception Vector
 * Refer to SDM Vol2 Table 5-6 & 8-1
 */
void illegal_op(struct kvm_vcpu *vcpu)
{
	_general_exception(vcpu);
}

/*
 * Illegal Dependency Fault
 *  @ General Exception Vector
 * Refer to SDM Vol2 Table 5-6 & 8-1
 */
void illegal_dep(struct kvm_vcpu *vcpu)
{
	_general_exception(vcpu);
}

/*
 * Reserved Register/Field Fault
 *  @ General Exception Vector
 * Refer to SDM Vol2 Table 5-6 & 8-1
 */
void rsv_reg_field(struct kvm_vcpu *vcpu)
{
	_general_exception(vcpu);
}
/*
 * Privileged Operation Fault
 *  @ General Exception Vector
 * Refer to SDM Vol2 Table 5-6 & 8-1
 */

void privilege_op(struct kvm_vcpu *vcpu)
{
	_general_exception(vcpu);
}

/*
 * Unimplement Data Address Fault
 *  @ General Exception Vector
 * Refer to SDM Vol2 Table 5-6 & 8-1
 */
void unimpl_daddr(struct kvm_vcpu *vcpu)
{
	_general_exception(vcpu);
}

/*
 * Privileged Register Fault
 *  @ General Exception Vector
 * Refer to SDM Vol2 Table 5-6 & 8-1
 */
void privilege_reg(struct kvm_vcpu *vcpu)
{
	_general_exception(vcpu);
}

/* Deal with
 *  Nat consumption vector
 * Parameter:
 *  vaddr: Optional, if t == REGISTER
 */
static void _nat_consumption_fault(struct kvm_vcpu *vcpu, u64 vadr,
						enum tlb_miss_type t)
{
	/* If vPSR.ic && t == DATA/INST, IFA */
	if (t == DATA || t == INSTRUCTION) {
		/* IFA */
		set_ifa_itir_iha(vcpu, vadr, 1, 0, 0);
	}

	inject_guest_interruption(vcpu, IA64_NAT_CONSUMPTION_VECTOR);
}

/*
 * Instruction Nat Page Consumption Fault
 *  @ Nat Consumption Vector
 * Refer to SDM Vol2 Table 5-6 & 8-1
 */
void inat_page_consumption(struct kvm_vcpu *vcpu, u64 vadr)
{
	_nat_consumption_fault(vcpu, vadr, INSTRUCTION);
}

/*
 * Register Nat Consumption Fault
 *  @ Nat Consumption Vector
 * Refer to SDM Vol2 Table 5-6 & 8-1
 */
void rnat_consumption(struct kvm_vcpu *vcpu)
{
	_nat_consumption_fault(vcpu, 0, REGISTER);
}

/*
 * Data Nat Page Consumption Fault
 *  @ Nat Consumption Vector
 * Refer to SDM Vol2 Table 5-6 & 8-1
 */
void dnat_page_consumption(struct kvm_vcpu *vcpu, u64 vadr)
{
	_nat_consumption_fault(vcpu, vadr, DATA);
}

/* Deal with
 *  Page not present vector
 */
static void __page_not_present(struct kvm_vcpu *vcpu, u64 vadr)
{
	/* If vPSR.ic, IFA, ITIR */
	set_ifa_itir_iha(vcpu, vadr, 1, 1, 0);
	inject_guest_interruption(vcpu, IA64_PAGE_NOT_PRESENT_VECTOR);
}

void data_page_not_present(struct kvm_vcpu *vcpu, u64 vadr)
{
	__page_not_present(vcpu, vadr);
}

void inst_page_not_present(struct kvm_vcpu *vcpu, u64 vadr)
{
	__page_not_present(vcpu, vadr);
}

/* Deal with
 *  Data access rights vector
 */
void data_access_rights(struct kvm_vcpu *vcpu, u64 vadr)
{
	/* If vPSR.ic, IFA, ITIR */
	set_ifa_itir_iha(vcpu, vadr, 1, 1, 0);
	inject_guest_interruption(vcpu, IA64_DATA_ACCESS_RIGHTS_VECTOR);
}

fpswa_ret_t vmm_fp_emulate(int fp_fault, void *bundle, unsigned long *ipsr,
		unsigned long *fpsr, unsigned long *isr, unsigned long *pr,
		unsigned long *ifs, struct kvm_pt_regs *regs)
{
	fp_state_t fp_state;
	fpswa_ret_t ret;
	struct kvm_vcpu *vcpu = current_vcpu;

	uint64_t old_rr7 = ia64_get_rr(7UL<<61);

	if (!vmm_fpswa_interface)
		return (fpswa_ret_t) {-1, 0, 0, 0};

	memset(&fp_state, 0, sizeof(fp_state_t));

	/*
	 * compute fp_state.  only FP registers f6 - f11 are used by the
	 * vmm, so set those bits in the mask and set the low volatile
	 * pointer to point to these registers.
	 */
	fp_state.bitmask_low64 = 0xfc0;  /* bit6..bit11 */

	fp_state.fp_state_low_volatile = (fp_state_low_volatile_t *) &regs->f6;

   /*
	 * unsigned long (*EFI_FPSWA) (
	 *      unsigned long    trap_type,
	 *      void             *Bundle,
	 *      unsigned long    *pipsr,
	 *      unsigned long    *pfsr,
	 *      unsigned long    *pisr,
	 *      unsigned long    *ppreds,
	 *      unsigned long    *pifs,
	 *      void             *fp_state);
	 */
	/*Call host fpswa interface directly to virtualize
	 *guest fpswa request!
	 */
	ia64_set_rr(7UL << 61, vcpu->arch.host.rr[7]);
	ia64_srlz_d();

	ret = (*vmm_fpswa_interface->fpswa) (fp_fault, bundle,
			ipsr, fpsr, isr, pr, ifs, &fp_state);
	ia64_set_rr(7UL << 61, old_rr7);
	ia64_srlz_d();
	return ret;
}

/*
 * Handle floating-point assist faults and traps for domain.
 */
unsigned long vmm_handle_fpu_swa(int fp_fault, struct kvm_pt_regs *regs,
					unsigned long isr)
{
	struct kvm_vcpu *v = current_vcpu;
	IA64_BUNDLE bundle;
	unsigned long fault_ip;
	fpswa_ret_t ret;

	fault_ip = regs->cr_iip;
	/*
	 * When the FP trap occurs, the trapping instruction is completed.
	 * If ipsr.ri == 0, there is the trapping instruction in previous
	 * bundle.
	 */
	if (!fp_fault && (ia64_psr(regs)->ri == 0))
		fault_ip -= 16;

	if (fetch_code(v, fault_ip, &bundle))
		return -EAGAIN;

	if (!bundle.i64[0] && !bundle.i64[1])
		return -EACCES;

	ret = vmm_fp_emulate(fp_fault, &bundle, &regs->cr_ipsr, &regs->ar_fpsr,
			&isr, &regs->pr, &regs->cr_ifs, regs);
	return ret.status;
}

void reflect_interruption(u64 ifa, u64 isr, u64 iim,
		u64 vec, struct kvm_pt_regs *regs)
{
	u64 vector;
	int status ;
	struct kvm_vcpu *vcpu = current_vcpu;
	u64 vpsr = VCPU(vcpu, vpsr);

	vector = vec2off[vec];

	if (!(vpsr & IA64_PSR_IC) && (vector != IA64_DATA_NESTED_TLB_VECTOR)) {
		panic_vm(vcpu, "Interruption with vector :0x%lx occurs "
						"with psr.ic = 0\n", vector);
		return;
	}

	switch (vec) {
	case 32: 	/*IA64_FP_FAULT_VECTOR*/
		status = vmm_handle_fpu_swa(1, regs, isr);
		if (!status) {
			vcpu_increment_iip(vcpu);
			return;
		} else if (-EAGAIN == status)
			return;
		break;
	case 33:	/*IA64_FP_TRAP_VECTOR*/
		status = vmm_handle_fpu_swa(0, regs, isr);
		if (!status)
			return ;
		break;
	}

	VCPU(vcpu, isr) = isr;
	VCPU(vcpu, iipa) = regs->cr_iip;
	if (vector == IA64_BREAK_VECTOR || vector == IA64_SPECULATION_VECTOR)
		VCPU(vcpu, iim) = iim;
	else
		set_ifa_itir_iha(vcpu, ifa, 1, 1, 1);

	inject_guest_interruption(vcpu, vector);
}

static unsigned long kvm_trans_pal_call_args(struct kvm_vcpu *vcpu,
						unsigned long arg)
{
	struct thash_data *data;
	unsigned long gpa, poff;

	if (!is_physical_mode(vcpu)) {
		/* Depends on caller to provide the DTR or DTC mapping.*/
		data = vtlb_lookup(vcpu, arg, D_TLB);
		if (data)
			gpa = data->page_flags & _PAGE_PPN_MASK;
		else {
			data = vhpt_lookup(arg);
			if (!data)
				return 0;
			gpa = data->gpaddr & _PAGE_PPN_MASK;
		}

		poff = arg & (PSIZE(data->ps) - 1);
		arg = PAGEALIGN(gpa, data->ps) | poff;
	}
	arg = kvm_gpa_to_mpa(arg << 1 >> 1);

	return (unsigned long)__va(arg);
}

static void set_pal_call_data(struct kvm_vcpu *vcpu)
{
	struct exit_ctl_data *p = &vcpu->arch.exit_data;
	unsigned long gr28 = vcpu_get_gr(vcpu, 28);
	unsigned long gr29 = vcpu_get_gr(vcpu, 29);
	unsigned long gr30 = vcpu_get_gr(vcpu, 30);

	/*FIXME:For static and stacked convention, firmware
	 * has put the parameters in gr28-gr31 before
	 * break to vmm  !!*/

	switch (gr28) {
	case PAL_PERF_MON_INFO:
	case PAL_HALT_INFO:
		p->u.pal_data.gr29 =  kvm_trans_pal_call_args(vcpu, gr29);
		p->u.pal_data.gr30 = vcpu_get_gr(vcpu, 30);
		break;
	case PAL_BRAND_INFO:
		p->u.pal_data.gr29 = gr29;;
		p->u.pal_data.gr30 = kvm_trans_pal_call_args(vcpu, gr30);
		break;
	default:
		p->u.pal_data.gr29 = gr29;;
		p->u.pal_data.gr30 = vcpu_get_gr(vcpu, 30);
	}
	p->u.pal_data.gr28 = gr28;
	p->u.pal_data.gr31 = vcpu_get_gr(vcpu, 31);

	p->exit_reason = EXIT_REASON_PAL_CALL;
}

static void get_pal_call_result(struct kvm_vcpu *vcpu)
{
	struct exit_ctl_data *p = &vcpu->arch.exit_data;

	if (p->exit_reason == EXIT_REASON_PAL_CALL) {
		vcpu_set_gr(vcpu, 8, p->u.pal_data.ret.status, 0);
		vcpu_set_gr(vcpu, 9, p->u.pal_data.ret.v0, 0);
		vcpu_set_gr(vcpu, 10, p->u.pal_data.ret.v1, 0);
		vcpu_set_gr(vcpu, 11, p->u.pal_data.ret.v2, 0);
	} else
		panic_vm(vcpu, "Mis-set for exit reason!\n");
}

static void set_sal_call_data(struct kvm_vcpu *vcpu)
{
	struct exit_ctl_data *p = &vcpu->arch.exit_data;

	p->u.sal_data.in0 = vcpu_get_gr(vcpu, 32);
	p->u.sal_data.in1 = vcpu_get_gr(vcpu, 33);
	p->u.sal_data.in2 = vcpu_get_gr(vcpu, 34);
	p->u.sal_data.in3 = vcpu_get_gr(vcpu, 35);
	p->u.sal_data.in4 = vcpu_get_gr(vcpu, 36);
	p->u.sal_data.in5 = vcpu_get_gr(vcpu, 37);
	p->u.sal_data.in6 = vcpu_get_gr(vcpu, 38);
	p->u.sal_data.in7 = vcpu_get_gr(vcpu, 39);
	p->exit_reason = EXIT_REASON_SAL_CALL;
}

static void get_sal_call_result(struct kvm_vcpu *vcpu)
{
	struct exit_ctl_data *p = &vcpu->arch.exit_data;

	if (p->exit_reason == EXIT_REASON_SAL_CALL) {
		vcpu_set_gr(vcpu, 8, p->u.sal_data.ret.r8, 0);
		vcpu_set_gr(vcpu, 9, p->u.sal_data.ret.r9, 0);
		vcpu_set_gr(vcpu, 10, p->u.sal_data.ret.r10, 0);
		vcpu_set_gr(vcpu, 11, p->u.sal_data.ret.r11, 0);
	} else
		panic_vm(vcpu, "Mis-set for exit reason!\n");
}

void  kvm_ia64_handle_break(unsigned long ifa, struct kvm_pt_regs *regs,
		unsigned long isr, unsigned long iim)
{
	struct kvm_vcpu *v = current_vcpu;
	long psr;

	if (ia64_psr(regs)->cpl == 0) {
		/* Allow hypercalls only when cpl = 0.  */
		if (iim == DOMN_PAL_REQUEST) {
			local_irq_save(psr);
			set_pal_call_data(v);
			vmm_transition(v);
			get_pal_call_result(v);
			vcpu_increment_iip(v);
			local_irq_restore(psr);
			return;
		} else if (iim == DOMN_SAL_REQUEST) {
			local_irq_save(psr);
			set_sal_call_data(v);
			vmm_transition(v);
			get_sal_call_result(v);
			vcpu_increment_iip(v);
			local_irq_restore(psr);
			return;
		}
	}
	reflect_interruption(ifa, isr, iim, 11, regs);
}

void check_pending_irq(struct kvm_vcpu *vcpu)
{
	int  mask, h_pending, h_inservice;
	u64 isr;
	unsigned long  vpsr;
	struct kvm_pt_regs *regs = vcpu_regs(vcpu);

	h_pending = highest_pending_irq(vcpu);
	if (h_pending == NULL_VECTOR) {
		update_vhpi(vcpu, NULL_VECTOR);
		return;
	}
	h_inservice = highest_inservice_irq(vcpu);

	vpsr = VCPU(vcpu, vpsr);
	mask = irq_masked(vcpu, h_pending, h_inservice);
	if ((vpsr & IA64_PSR_I) && IRQ_NO_MASKED == mask) {
		isr = vpsr & IA64_PSR_RI;
		update_vhpi(vcpu, h_pending);
		reflect_interruption(0, isr, 0, 12, regs); /* EXT IRQ */
	} else if (mask == IRQ_MASKED_BY_INSVC) {
		if (VCPU(vcpu, vhpi))
			update_vhpi(vcpu, NULL_VECTOR);
	} else {
		/* masked by vpsr.i or vtpr.*/
		update_vhpi(vcpu, h_pending);
	}
}

static void generate_exirq(struct kvm_vcpu *vcpu)
{
	unsigned  vpsr;
	uint64_t isr;

	struct kvm_pt_regs *regs = vcpu_regs(vcpu);

	vpsr = VCPU(vcpu, vpsr);
	isr = vpsr & IA64_PSR_RI;
	if (!(vpsr & IA64_PSR_IC))
		panic_vm(vcpu, "Trying to inject one IRQ with psr.ic=0\n");
	reflect_interruption(0, isr, 0, 12, regs); /* EXT IRQ */
}

void vhpi_detection(struct kvm_vcpu *vcpu)
{
	uint64_t    threshold, vhpi;
	union ia64_tpr       vtpr;
	struct ia64_psr vpsr;

	vpsr = *(struct ia64_psr *)&VCPU(vcpu, vpsr);
	vtpr.val = VCPU(vcpu, tpr);

	threshold = ((!vpsr.i) << 5) | (vtpr.mmi << 4) | vtpr.mic;
	vhpi = VCPU(vcpu, vhpi);
	if (vhpi > threshold) {
		/* interrupt actived*/
		generate_exirq(vcpu);
	}
}

void leave_hypervisor_tail(void)
{
	struct kvm_vcpu *v = current_vcpu;

	if (VMX(v, timer_check)) {
		VMX(v, timer_check) = 0;
		if (VMX(v, itc_check)) {
			if (vcpu_get_itc(v) > VCPU(v, itm)) {
				if (!(VCPU(v, itv) & (1 << 16))) {
					vcpu_pend_interrupt(v, VCPU(v, itv)
							& 0xff);
					VMX(v, itc_check) = 0;
				} else {
					v->arch.timer_pending = 1;
				}
				VMX(v, last_itc) = VCPU(v, itm) + 1;
			}
		}
	}

	rmb();
	if (v->arch.irq_new_pending) {
		v->arch.irq_new_pending = 0;
		VMX(v, irq_check) = 0;
		check_pending_irq(v);
		return;
	}
	if (VMX(v, irq_check)) {
		VMX(v, irq_check) = 0;
		vhpi_detection(v);
	}
}

static inline void handle_lds(struct kvm_pt_regs *regs)
{
	regs->cr_ipsr |= IA64_PSR_ED;
}

void physical_tlb_miss(struct kvm_vcpu *vcpu, unsigned long vadr, int type)
{
	unsigned long pte;
	union ia64_rr rr;

	rr.val = ia64_get_rr(vadr);
	pte =  vadr & _PAGE_PPN_MASK;
	pte = pte | PHY_PAGE_WB;
	thash_vhpt_insert(vcpu, pte, (u64)(rr.ps << 2), vadr, type);
	return;
}

void kvm_page_fault(u64 vadr , u64 vec, struct kvm_pt_regs *regs)
{
	unsigned long vpsr;
	int type;

	u64 vhpt_adr, gppa, pteval, rr, itir;
	union ia64_isr misr;
	union ia64_pta vpta;
	struct thash_data *data;
	struct kvm_vcpu *v = current_vcpu;

	vpsr = VCPU(v, vpsr);
	misr.val = VMX(v, cr_isr);

	type = vec;

	if (is_physical_mode(v) && (!(vadr << 1 >> 62))) {
		if (vec == 2) {
			if (__gpfn_is_io((vadr << 1) >> (PAGE_SHIFT + 1))) {
				emulate_io_inst(v, ((vadr << 1) >> 1), 4);
				return;
			}
		}
		physical_tlb_miss(v, vadr, type);
		return;
	}
	data = vtlb_lookup(v, vadr, type);
	if (data != 0) {
		if (type == D_TLB) {
			gppa = (vadr & ((1UL << data->ps) - 1))
				+ (data->ppn >> (data->ps - 12) << data->ps);
			if (__gpfn_is_io(gppa >> PAGE_SHIFT)) {
				if (data->pl >= ((regs->cr_ipsr >>
						IA64_PSR_CPL0_BIT) & 3))
					emulate_io_inst(v, gppa, data->ma);
				else {
					vcpu_set_isr(v, misr.val);
					data_access_rights(v, vadr);
				}
				return ;
			}
		}
		thash_vhpt_insert(v, data->page_flags, data->itir, vadr, type);

	} else if (type == D_TLB) {
		if (misr.sp) {
			handle_lds(regs);
			return;
		}

		rr = vcpu_get_rr(v, vadr);
		itir = rr & (RR_RID_MASK | RR_PS_MASK);

		if (!vhpt_enabled(v, vadr, misr.rs ? RSE_REF : DATA_REF)) {
			if (vpsr & IA64_PSR_IC) {
				vcpu_set_isr(v, misr.val);
				alt_dtlb(v, vadr);
			} else {
				nested_dtlb(v);
			}
			return ;
		}

		vpta.val = vcpu_get_pta(v);
		/* avoid recursively walking (short format) VHPT */

		vhpt_adr = vcpu_thash(v, vadr);
		if (!guest_vhpt_lookup(vhpt_adr, &pteval)) {
			/* VHPT successfully read.  */
			if (!(pteval & _PAGE_P)) {
				if (vpsr & IA64_PSR_IC) {
					vcpu_set_isr(v, misr.val);
					dtlb_fault(v, vadr);
				} else {
					nested_dtlb(v);
				}
			} else if ((pteval & _PAGE_MA_MASK) != _PAGE_MA_ST) {
				thash_purge_and_insert(v, pteval, itir,
								vadr, D_TLB);
			} else if (vpsr & IA64_PSR_IC) {
				vcpu_set_isr(v, misr.val);
				dtlb_fault(v, vadr);
			} else {
				nested_dtlb(v);
			}
		} else {
			/* Can't read VHPT.  */
			if (vpsr & IA64_PSR_IC) {
				vcpu_set_isr(v, misr.val);
				dvhpt_fault(v, vadr);
			} else {
				nested_dtlb(v);
			}
		}
	} else if (type == I_TLB) {
		if (!(vpsr & IA64_PSR_IC))
			misr.ni = 1;
		if (!vhpt_enabled(v, vadr, INST_REF)) {
			vcpu_set_isr(v, misr.val);
			alt_itlb(v, vadr);
			return;
		}

		vpta.val = vcpu_get_pta(v);

		vhpt_adr = vcpu_thash(v, vadr);
		if (!guest_vhpt_lookup(vhpt_adr, &pteval)) {
			/* VHPT successfully read.  */
			if (pteval & _PAGE_P) {
				if ((pteval & _PAGE_MA_MASK) == _PAGE_MA_ST) {
					vcpu_set_isr(v, misr.val);
					itlb_fault(v, vadr);
					return ;
				}
				rr = vcpu_get_rr(v, vadr);
				itir = rr & (RR_RID_MASK | RR_PS_MASK);
				thash_purge_and_insert(v, pteval, itir,
							vadr, I_TLB);
			} else {
				vcpu_set_isr(v, misr.val);
				inst_page_not_present(v, vadr);
			}
		} else {
			vcpu_set_isr(v, misr.val);
			ivhpt_fault(v, vadr);
		}
	}
}

void kvm_vexirq(struct kvm_vcpu *vcpu)
{
	u64 vpsr, isr;
	struct kvm_pt_regs *regs;

	regs = vcpu_regs(vcpu);
	vpsr = VCPU(vcpu, vpsr);
	isr = vpsr & IA64_PSR_RI;
	reflect_interruption(0, isr, 0, 12, regs); /*EXT IRQ*/
}

void kvm_ia64_handle_irq(struct kvm_vcpu *v)
{
	struct exit_ctl_data *p = &v->arch.exit_data;
	long psr;

	local_irq_save(psr);
	p->exit_reason = EXIT_REASON_EXTERNAL_INTERRUPT;
	vmm_transition(v);
	local_irq_restore(psr);

	VMX(v, timer_check) = 1;

}

static void ptc_ga_remote_func(struct kvm_vcpu *v, int pos)
{
	u64 oldrid, moldrid, oldpsbits, vaddr;
	struct kvm_ptc_g *p = &v->arch.ptc_g_data[pos];
	vaddr = p->vaddr;

	oldrid = VMX(v, vrr[0]);
	VMX(v, vrr[0]) = p->rr;
	oldpsbits = VMX(v, psbits[0]);
	VMX(v, psbits[0]) = VMX(v, psbits[REGION_NUMBER(vaddr)]);
	moldrid = ia64_get_rr(0x0);
	ia64_set_rr(0x0, vrrtomrr(p->rr));
	ia64_srlz_d();

	vaddr = PAGEALIGN(vaddr, p->ps);
	thash_purge_entries_remote(v, vaddr, p->ps);

	VMX(v, vrr[0]) = oldrid;
	VMX(v, psbits[0]) = oldpsbits;
	ia64_set_rr(0x0, moldrid);
	ia64_dv_serialize_data();
}

static void vcpu_do_resume(struct kvm_vcpu *vcpu)
{
	/*Re-init VHPT and VTLB once from resume*/
	vcpu->arch.vhpt.num = VHPT_NUM_ENTRIES;
	thash_init(&vcpu->arch.vhpt, VHPT_SHIFT);
	vcpu->arch.vtlb.num = VTLB_NUM_ENTRIES;
	thash_init(&vcpu->arch.vtlb, VTLB_SHIFT);

	ia64_set_pta(vcpu->arch.vhpt.pta.val);
}

static void vmm_sanity_check(struct kvm_vcpu *vcpu)
{
	struct exit_ctl_data *p = &vcpu->arch.exit_data;

	if (!vmm_sanity && p->exit_reason != EXIT_REASON_DEBUG) {
		panic_vm(vcpu, "Failed to do vmm sanity check,"
			"it maybe caused by crashed vmm!!\n\n");
	}
}

static void kvm_do_resume_op(struct kvm_vcpu *vcpu)
{
	vmm_sanity_check(vcpu); /*Guarantee vcpu runing on healthy vmm!*/

	if (test_and_clear_bit(KVM_REQ_RESUME, &vcpu->requests)) {
		vcpu_do_resume(vcpu);
		return;
	}

	if (unlikely(test_and_clear_bit(KVM_REQ_TLB_FLUSH, &vcpu->requests))) {
		thash_purge_all(vcpu);
		return;
	}

	if (test_and_clear_bit(KVM_REQ_PTC_G, &vcpu->requests)) {
		while (vcpu->arch.ptc_g_count > 0)
			ptc_ga_remote_func(vcpu, --vcpu->arch.ptc_g_count);
	}
}

void vmm_transition(struct kvm_vcpu *vcpu)
{
	ia64_call_vsa(PAL_VPS_SAVE, (unsigned long)vcpu->arch.vpd,
			1, 0, 0, 0, 0, 0);
	vmm_trampoline(&vcpu->arch.guest, &vcpu->arch.host);
	ia64_call_vsa(PAL_VPS_RESTORE, (unsigned long)vcpu->arch.vpd,
						1, 0, 0, 0, 0, 0);
	kvm_do_resume_op(vcpu);
}

void vmm_panic_handler(u64 vec)
{
	struct kvm_vcpu *vcpu = current_vcpu;
	vmm_sanity = 0;
	panic_vm(vcpu, "Unexpected interruption occurs in VMM, vector:0x%lx\n",
			vec2off[vec]);
}
