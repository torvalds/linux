/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * Copyright Novell Inc 2010
 *
 * Authors: Alexander Graf <agraf@suse.de>
 */

#include <asm/kvm.h>
#include <asm/kvm_ppc.h>
#include <asm/disassemble.h>
#include <asm/kvm_book3s.h>
#include <asm/kvm_fpu.h>
#include <asm/reg.h>
#include <asm/cacheflush.h>
#include <asm/switch_to.h>
#include <linux/vmalloc.h>

/* #define DEBUG */

#ifdef DEBUG
#define dprintk printk
#else
#define dprintk(...) do { } while(0);
#endif

#define OP_LFS			48
#define OP_LFSU			49
#define OP_LFD			50
#define OP_LFDU			51
#define OP_STFS			52
#define OP_STFSU		53
#define OP_STFD			54
#define OP_STFDU		55
#define OP_PSQ_L		56
#define OP_PSQ_LU		57
#define OP_PSQ_ST		60
#define OP_PSQ_STU		61

#define OP_31_LFSX		535
#define OP_31_LFSUX		567
#define OP_31_LFDX		599
#define OP_31_LFDUX		631
#define OP_31_STFSX		663
#define OP_31_STFSUX		695
#define OP_31_STFX		727
#define OP_31_STFUX		759
#define OP_31_LWIZX		887
#define OP_31_STFIWX		983

#define OP_59_FADDS		21
#define OP_59_FSUBS		20
#define OP_59_FSQRTS		22
#define OP_59_FDIVS		18
#define OP_59_FRES		24
#define OP_59_FMULS		25
#define OP_59_FRSQRTES		26
#define OP_59_FMSUBS		28
#define OP_59_FMADDS		29
#define OP_59_FNMSUBS		30
#define OP_59_FNMADDS		31

#define OP_63_FCMPU		0
#define OP_63_FCPSGN		8
#define OP_63_FRSP		12
#define OP_63_FCTIW		14
#define OP_63_FCTIWZ		15
#define OP_63_FDIV		18
#define OP_63_FADD		21
#define OP_63_FSQRT		22
#define OP_63_FSEL		23
#define OP_63_FRE		24
#define OP_63_FMUL		25
#define OP_63_FRSQRTE		26
#define OP_63_FMSUB		28
#define OP_63_FMADD		29
#define OP_63_FNMSUB		30
#define OP_63_FNMADD		31
#define OP_63_FCMPO		32
#define OP_63_MTFSB1		38 // XXX
#define OP_63_FSUB		20
#define OP_63_FNEG		40
#define OP_63_MCRFS		64
#define OP_63_MTFSB0		70
#define OP_63_FMR		72
#define OP_63_MTFSFI		134
#define OP_63_FABS		264
#define OP_63_MFFS		583
#define OP_63_MTFSF		711

#define OP_4X_PS_CMPU0		0
#define OP_4X_PSQ_LX		6
#define OP_4XW_PSQ_STX		7
#define OP_4A_PS_SUM0		10
#define OP_4A_PS_SUM1		11
#define OP_4A_PS_MULS0		12
#define OP_4A_PS_MULS1		13
#define OP_4A_PS_MADDS0		14
#define OP_4A_PS_MADDS1		15
#define OP_4A_PS_DIV		18
#define OP_4A_PS_SUB		20
#define OP_4A_PS_ADD		21
#define OP_4A_PS_SEL		23
#define OP_4A_PS_RES		24
#define OP_4A_PS_MUL		25
#define OP_4A_PS_RSQRTE		26
#define OP_4A_PS_MSUB		28
#define OP_4A_PS_MADD		29
#define OP_4A_PS_NMSUB		30
#define OP_4A_PS_NMADD		31
#define OP_4X_PS_CMPO0		32
#define OP_4X_PSQ_LUX		38
#define OP_4XW_PSQ_STUX		39
#define OP_4X_PS_NEG		40
#define OP_4X_PS_CMPU1		64
#define OP_4X_PS_MR		72
#define OP_4X_PS_CMPO1		96
#define OP_4X_PS_NABS		136
#define OP_4X_PS_ABS		264
#define OP_4X_PS_MERGE00	528
#define OP_4X_PS_MERGE01	560
#define OP_4X_PS_MERGE10	592
#define OP_4X_PS_MERGE11	624

#define SCALAR_NONE		0
#define SCALAR_HIGH		(1 << 0)
#define SCALAR_LOW		(1 << 1)
#define SCALAR_NO_PS0		(1 << 2)
#define SCALAR_NO_PS1		(1 << 3)

#define GQR_ST_TYPE_MASK	0x00000007
#define GQR_ST_TYPE_SHIFT	0
#define GQR_ST_SCALE_MASK	0x00003f00
#define GQR_ST_SCALE_SHIFT	8
#define GQR_LD_TYPE_MASK	0x00070000
#define GQR_LD_TYPE_SHIFT	16
#define GQR_LD_SCALE_MASK	0x3f000000
#define GQR_LD_SCALE_SHIFT	24

#define GQR_QUANTIZE_FLOAT	0
#define GQR_QUANTIZE_U8		4
#define GQR_QUANTIZE_U16	5
#define GQR_QUANTIZE_S8		6
#define GQR_QUANTIZE_S16	7

#define FPU_LS_SINGLE		0
#define FPU_LS_DOUBLE		1
#define FPU_LS_SINGLE_LOW	2

static inline void kvmppc_sync_qpr(struct kvm_vcpu *vcpu, int rt)
{
	kvm_cvt_df(&VCPU_FPR(vcpu, rt), &vcpu->arch.qpr[rt]);
}

static void kvmppc_inject_pf(struct kvm_vcpu *vcpu, ulong eaddr, bool is_store)
{
	u64 dsisr;
	struct kvm_vcpu_arch_shared *shared = vcpu->arch.shared;

	shared->msr = kvmppc_set_field(shared->msr, 33, 36, 0);
	shared->msr = kvmppc_set_field(shared->msr, 42, 47, 0);
	shared->dar = eaddr;
	/* Page Fault */
	dsisr = kvmppc_set_field(0, 33, 33, 1);
	if (is_store)
		shared->dsisr = kvmppc_set_field(dsisr, 38, 38, 1);
	kvmppc_book3s_queue_irqprio(vcpu, BOOK3S_INTERRUPT_DATA_STORAGE);
}

static int kvmppc_emulate_fpr_load(struct kvm_run *run, struct kvm_vcpu *vcpu,
				   int rs, ulong addr, int ls_type)
{
	int emulated = EMULATE_FAIL;
	int r;
	char tmp[8];
	int len = sizeof(u32);

	if (ls_type == FPU_LS_DOUBLE)
		len = sizeof(u64);

	/* read from memory */
	r = kvmppc_ld(vcpu, &addr, len, tmp, true);
	vcpu->arch.paddr_accessed = addr;

	if (r < 0) {
		kvmppc_inject_pf(vcpu, addr, false);
		goto done_load;
	} else if (r == EMULATE_DO_MMIO) {
		emulated = kvmppc_handle_load(run, vcpu, KVM_MMIO_REG_FPR | rs,
					      len, 1);
		goto done_load;
	}

	emulated = EMULATE_DONE;

	/* put in registers */
	switch (ls_type) {
	case FPU_LS_SINGLE:
		kvm_cvt_fd((u32*)tmp, &VCPU_FPR(vcpu, rs));
		vcpu->arch.qpr[rs] = *((u32*)tmp);
		break;
	case FPU_LS_DOUBLE:
		VCPU_FPR(vcpu, rs) = *((u64*)tmp);
		break;
	}

	dprintk(KERN_INFO "KVM: FPR_LD [0x%llx] at 0x%lx (%d)\n", *(u64*)tmp,
			  addr, len);

done_load:
	return emulated;
}

static int kvmppc_emulate_fpr_store(struct kvm_run *run, struct kvm_vcpu *vcpu,
				    int rs, ulong addr, int ls_type)
{
	int emulated = EMULATE_FAIL;
	int r;
	char tmp[8];
	u64 val;
	int len;

	switch (ls_type) {
	case FPU_LS_SINGLE:
		kvm_cvt_df(&VCPU_FPR(vcpu, rs), (u32*)tmp);
		val = *((u32*)tmp);
		len = sizeof(u32);
		break;
	case FPU_LS_SINGLE_LOW:
		*((u32*)tmp) = VCPU_FPR(vcpu, rs);
		val = VCPU_FPR(vcpu, rs) & 0xffffffff;
		len = sizeof(u32);
		break;
	case FPU_LS_DOUBLE:
		*((u64*)tmp) = VCPU_FPR(vcpu, rs);
		val = VCPU_FPR(vcpu, rs);
		len = sizeof(u64);
		break;
	default:
		val = 0;
		len = 0;
	}

	r = kvmppc_st(vcpu, &addr, len, tmp, true);
	vcpu->arch.paddr_accessed = addr;
	if (r < 0) {
		kvmppc_inject_pf(vcpu, addr, true);
	} else if (r == EMULATE_DO_MMIO) {
		emulated = kvmppc_handle_store(run, vcpu, val, len, 1);
	} else {
		emulated = EMULATE_DONE;
	}

	dprintk(KERN_INFO "KVM: FPR_ST [0x%llx] at 0x%lx (%d)\n",
			  val, addr, len);

	return emulated;
}

static int kvmppc_emulate_psq_load(struct kvm_run *run, struct kvm_vcpu *vcpu,
				   int rs, ulong addr, bool w, int i)
{
	int emulated = EMULATE_FAIL;
	int r;
	float one = 1.0;
	u32 tmp[2];

	/* read from memory */
	if (w) {
		r = kvmppc_ld(vcpu, &addr, sizeof(u32), tmp, true);
		memcpy(&tmp[1], &one, sizeof(u32));
	} else {
		r = kvmppc_ld(vcpu, &addr, sizeof(u32) * 2, tmp, true);
	}
	vcpu->arch.paddr_accessed = addr;
	if (r < 0) {
		kvmppc_inject_pf(vcpu, addr, false);
		goto done_load;
	} else if ((r == EMULATE_DO_MMIO) && w) {
		emulated = kvmppc_handle_load(run, vcpu, KVM_MMIO_REG_FPR | rs,
					      4, 1);
		vcpu->arch.qpr[rs] = tmp[1];
		goto done_load;
	} else if (r == EMULATE_DO_MMIO) {
		emulated = kvmppc_handle_load(run, vcpu, KVM_MMIO_REG_FQPR | rs,
					      8, 1);
		goto done_load;
	}

	emulated = EMULATE_DONE;

	/* put in registers */
	kvm_cvt_fd(&tmp[0], &VCPU_FPR(vcpu, rs));
	vcpu->arch.qpr[rs] = tmp[1];

	dprintk(KERN_INFO "KVM: PSQ_LD [0x%x, 0x%x] at 0x%lx (%d)\n", tmp[0],
			  tmp[1], addr, w ? 4 : 8);

done_load:
	return emulated;
}

static int kvmppc_emulate_psq_store(struct kvm_run *run, struct kvm_vcpu *vcpu,
				    int rs, ulong addr, bool w, int i)
{
	int emulated = EMULATE_FAIL;
	int r;
	u32 tmp[2];
	int len = w ? sizeof(u32) : sizeof(u64);

	kvm_cvt_df(&VCPU_FPR(vcpu, rs), &tmp[0]);
	tmp[1] = vcpu->arch.qpr[rs];

	r = kvmppc_st(vcpu, &addr, len, tmp, true);
	vcpu->arch.paddr_accessed = addr;
	if (r < 0) {
		kvmppc_inject_pf(vcpu, addr, true);
	} else if ((r == EMULATE_DO_MMIO) && w) {
		emulated = kvmppc_handle_store(run, vcpu, tmp[0], 4, 1);
	} else if (r == EMULATE_DO_MMIO) {
		u64 val = ((u64)tmp[0] << 32) | tmp[1];
		emulated = kvmppc_handle_store(run, vcpu, val, 8, 1);
	} else {
		emulated = EMULATE_DONE;
	}

	dprintk(KERN_INFO "KVM: PSQ_ST [0x%x, 0x%x] at 0x%lx (%d)\n",
			  tmp[0], tmp[1], addr, len);

	return emulated;
}

/*
 * Cuts out inst bits with ordering according to spec.
 * That means the leftmost bit is zero. All given bits are included.
 */
static inline u32 inst_get_field(u32 inst, int msb, int lsb)
{
	return kvmppc_get_field(inst, msb + 32, lsb + 32);
}

/*
 * Replaces inst bits with ordering according to spec.
 */
static inline u32 inst_set_field(u32 inst, int msb, int lsb, int value)
{
	return kvmppc_set_field(inst, msb + 32, lsb + 32, value);
}

bool kvmppc_inst_is_paired_single(struct kvm_vcpu *vcpu, u32 inst)
{
	if (!(vcpu->arch.hflags & BOOK3S_HFLAG_PAIRED_SINGLE))
		return false;

	switch (get_op(inst)) {
	case OP_PSQ_L:
	case OP_PSQ_LU:
	case OP_PSQ_ST:
	case OP_PSQ_STU:
	case OP_LFS:
	case OP_LFSU:
	case OP_LFD:
	case OP_LFDU:
	case OP_STFS:
	case OP_STFSU:
	case OP_STFD:
	case OP_STFDU:
		return true;
	case 4:
		/* X form */
		switch (inst_get_field(inst, 21, 30)) {
		case OP_4X_PS_CMPU0:
		case OP_4X_PSQ_LX:
		case OP_4X_PS_CMPO0:
		case OP_4X_PSQ_LUX:
		case OP_4X_PS_NEG:
		case OP_4X_PS_CMPU1:
		case OP_4X_PS_MR:
		case OP_4X_PS_CMPO1:
		case OP_4X_PS_NABS:
		case OP_4X_PS_ABS:
		case OP_4X_PS_MERGE00:
		case OP_4X_PS_MERGE01:
		case OP_4X_PS_MERGE10:
		case OP_4X_PS_MERGE11:
			return true;
		}
		/* XW form */
		switch (inst_get_field(inst, 25, 30)) {
		case OP_4XW_PSQ_STX:
		case OP_4XW_PSQ_STUX:
			return true;
		}
		/* A form */
		switch (inst_get_field(inst, 26, 30)) {
		case OP_4A_PS_SUM1:
		case OP_4A_PS_SUM0:
		case OP_4A_PS_MULS0:
		case OP_4A_PS_MULS1:
		case OP_4A_PS_MADDS0:
		case OP_4A_PS_MADDS1:
		case OP_4A_PS_DIV:
		case OP_4A_PS_SUB:
		case OP_4A_PS_ADD:
		case OP_4A_PS_SEL:
		case OP_4A_PS_RES:
		case OP_4A_PS_MUL:
		case OP_4A_PS_RSQRTE:
		case OP_4A_PS_MSUB:
		case OP_4A_PS_MADD:
		case OP_4A_PS_NMSUB:
		case OP_4A_PS_NMADD:
			return true;
		}
		break;
	case 59:
		switch (inst_get_field(inst, 21, 30)) {
		case OP_59_FADDS:
		case OP_59_FSUBS:
		case OP_59_FDIVS:
		case OP_59_FRES:
		case OP_59_FRSQRTES:
			return true;
		}
		switch (inst_get_field(inst, 26, 30)) {
		case OP_59_FMULS:
		case OP_59_FMSUBS:
		case OP_59_FMADDS:
		case OP_59_FNMSUBS:
		case OP_59_FNMADDS:
			return true;
		}
		break;
	case 63:
		switch (inst_get_field(inst, 21, 30)) {
		case OP_63_MTFSB0:
		case OP_63_MTFSB1:
		case OP_63_MTFSF:
		case OP_63_MTFSFI:
		case OP_63_MCRFS:
		case OP_63_MFFS:
		case OP_63_FCMPU:
		case OP_63_FCMPO:
		case OP_63_FNEG:
		case OP_63_FMR:
		case OP_63_FABS:
		case OP_63_FRSP:
		case OP_63_FDIV:
		case OP_63_FADD:
		case OP_63_FSUB:
		case OP_63_FCTIW:
		case OP_63_FCTIWZ:
		case OP_63_FRSQRTE:
		case OP_63_FCPSGN:
			return true;
		}
		switch (inst_get_field(inst, 26, 30)) {
		case OP_63_FMUL:
		case OP_63_FSEL:
		case OP_63_FMSUB:
		case OP_63_FMADD:
		case OP_63_FNMSUB:
		case OP_63_FNMADD:
			return true;
		}
		break;
	case 31:
		switch (inst_get_field(inst, 21, 30)) {
		case OP_31_LFSX:
		case OP_31_LFSUX:
		case OP_31_LFDX:
		case OP_31_LFDUX:
		case OP_31_STFSX:
		case OP_31_STFSUX:
		case OP_31_STFX:
		case OP_31_STFUX:
		case OP_31_STFIWX:
			return true;
		}
		break;
	}

	return false;
}

static int get_d_signext(u32 inst)
{
	int d = inst & 0x8ff;

	if (d & 0x800)
		return -(d & 0x7ff);

	return (d & 0x7ff);
}

static int kvmppc_ps_three_in(struct kvm_vcpu *vcpu, bool rc,
				      int reg_out, int reg_in1, int reg_in2,
				      int reg_in3, int scalar,
				      void (*func)(u64 *fpscr,
						 u32 *dst, u32 *src1,
						 u32 *src2, u32 *src3))
{
	u32 *qpr = vcpu->arch.qpr;
	u32 ps0_out;
	u32 ps0_in1, ps0_in2, ps0_in3;
	u32 ps1_in1, ps1_in2, ps1_in3;

	/* RC */
	WARN_ON(rc);

	/* PS0 */
	kvm_cvt_df(&VCPU_FPR(vcpu, reg_in1), &ps0_in1);
	kvm_cvt_df(&VCPU_FPR(vcpu, reg_in2), &ps0_in2);
	kvm_cvt_df(&VCPU_FPR(vcpu, reg_in3), &ps0_in3);

	if (scalar & SCALAR_LOW)
		ps0_in2 = qpr[reg_in2];

	func(&vcpu->arch.fp.fpscr, &ps0_out, &ps0_in1, &ps0_in2, &ps0_in3);

	dprintk(KERN_INFO "PS3 ps0 -> f(0x%x, 0x%x, 0x%x) = 0x%x\n",
			  ps0_in1, ps0_in2, ps0_in3, ps0_out);

	if (!(scalar & SCALAR_NO_PS0))
		kvm_cvt_fd(&ps0_out, &VCPU_FPR(vcpu, reg_out));

	/* PS1 */
	ps1_in1 = qpr[reg_in1];
	ps1_in2 = qpr[reg_in2];
	ps1_in3 = qpr[reg_in3];

	if (scalar & SCALAR_HIGH)
		ps1_in2 = ps0_in2;

	if (!(scalar & SCALAR_NO_PS1))
		func(&vcpu->arch.fp.fpscr, &qpr[reg_out], &ps1_in1, &ps1_in2, &ps1_in3);

	dprintk(KERN_INFO "PS3 ps1 -> f(0x%x, 0x%x, 0x%x) = 0x%x\n",
			  ps1_in1, ps1_in2, ps1_in3, qpr[reg_out]);

	return EMULATE_DONE;
}

static int kvmppc_ps_two_in(struct kvm_vcpu *vcpu, bool rc,
				    int reg_out, int reg_in1, int reg_in2,
				    int scalar,
				    void (*func)(u64 *fpscr,
						 u32 *dst, u32 *src1,
						 u32 *src2))
{
	u32 *qpr = vcpu->arch.qpr;
	u32 ps0_out;
	u32 ps0_in1, ps0_in2;
	u32 ps1_out;
	u32 ps1_in1, ps1_in2;

	/* RC */
	WARN_ON(rc);

	/* PS0 */
	kvm_cvt_df(&VCPU_FPR(vcpu, reg_in1), &ps0_in1);

	if (scalar & SCALAR_LOW)
		ps0_in2 = qpr[reg_in2];
	else
		kvm_cvt_df(&VCPU_FPR(vcpu, reg_in2), &ps0_in2);

	func(&vcpu->arch.fp.fpscr, &ps0_out, &ps0_in1, &ps0_in2);

	if (!(scalar & SCALAR_NO_PS0)) {
		dprintk(KERN_INFO "PS2 ps0 -> f(0x%x, 0x%x) = 0x%x\n",
				  ps0_in1, ps0_in2, ps0_out);

		kvm_cvt_fd(&ps0_out, &VCPU_FPR(vcpu, reg_out));
	}

	/* PS1 */
	ps1_in1 = qpr[reg_in1];
	ps1_in2 = qpr[reg_in2];

	if (scalar & SCALAR_HIGH)
		ps1_in2 = ps0_in2;

	func(&vcpu->arch.fp.fpscr, &ps1_out, &ps1_in1, &ps1_in2);

	if (!(scalar & SCALAR_NO_PS1)) {
		qpr[reg_out] = ps1_out;

		dprintk(KERN_INFO "PS2 ps1 -> f(0x%x, 0x%x) = 0x%x\n",
				  ps1_in1, ps1_in2, qpr[reg_out]);
	}

	return EMULATE_DONE;
}

static int kvmppc_ps_one_in(struct kvm_vcpu *vcpu, bool rc,
				    int reg_out, int reg_in,
				    void (*func)(u64 *t,
						 u32 *dst, u32 *src1))
{
	u32 *qpr = vcpu->arch.qpr;
	u32 ps0_out, ps0_in;
	u32 ps1_in;

	/* RC */
	WARN_ON(rc);

	/* PS0 */
	kvm_cvt_df(&VCPU_FPR(vcpu, reg_in), &ps0_in);
	func(&vcpu->arch.fp.fpscr, &ps0_out, &ps0_in);

	dprintk(KERN_INFO "PS1 ps0 -> f(0x%x) = 0x%x\n",
			  ps0_in, ps0_out);

	kvm_cvt_fd(&ps0_out, &VCPU_FPR(vcpu, reg_out));

	/* PS1 */
	ps1_in = qpr[reg_in];
	func(&vcpu->arch.fp.fpscr, &qpr[reg_out], &ps1_in);

	dprintk(KERN_INFO "PS1 ps1 -> f(0x%x) = 0x%x\n",
			  ps1_in, qpr[reg_out]);

	return EMULATE_DONE;
}

int kvmppc_emulate_paired_single(struct kvm_run *run, struct kvm_vcpu *vcpu)
{
	u32 inst = kvmppc_get_last_inst(vcpu);
	enum emulation_result emulated = EMULATE_DONE;

	int ax_rd = inst_get_field(inst, 6, 10);
	int ax_ra = inst_get_field(inst, 11, 15);
	int ax_rb = inst_get_field(inst, 16, 20);
	int ax_rc = inst_get_field(inst, 21, 25);
	short full_d = inst_get_field(inst, 16, 31);

	u64 *fpr_d = &VCPU_FPR(vcpu, ax_rd);
	u64 *fpr_a = &VCPU_FPR(vcpu, ax_ra);
	u64 *fpr_b = &VCPU_FPR(vcpu, ax_rb);
	u64 *fpr_c = &VCPU_FPR(vcpu, ax_rc);

	bool rcomp = (inst & 1) ? true : false;
	u32 cr = kvmppc_get_cr(vcpu);
#ifdef DEBUG
	int i;
#endif

	if (!kvmppc_inst_is_paired_single(vcpu, inst))
		return EMULATE_FAIL;

	if (!(vcpu->arch.shared->msr & MSR_FP)) {
		kvmppc_book3s_queue_irqprio(vcpu, BOOK3S_INTERRUPT_FP_UNAVAIL);
		return EMULATE_AGAIN;
	}

	kvmppc_giveup_ext(vcpu, MSR_FP);
	preempt_disable();
	enable_kernel_fp();
	/* Do we need to clear FE0 / FE1 here? Don't think so. */

#ifdef DEBUG
	for (i = 0; i < ARRAY_SIZE(vcpu->arch.fp.fpr); i++) {
		u32 f;
		kvm_cvt_df(&VCPU_FPR(vcpu, i), &f);
		dprintk(KERN_INFO "FPR[%d] = 0x%x / 0x%llx    QPR[%d] = 0x%x\n",
			i, f, VCPU_FPR(vcpu, i), i, vcpu->arch.qpr[i]);
	}
#endif

	switch (get_op(inst)) {
	case OP_PSQ_L:
	{
		ulong addr = ax_ra ? kvmppc_get_gpr(vcpu, ax_ra) : 0;
		bool w = inst_get_field(inst, 16, 16) ? true : false;
		int i = inst_get_field(inst, 17, 19);

		addr += get_d_signext(inst);
		emulated = kvmppc_emulate_psq_load(run, vcpu, ax_rd, addr, w, i);
		break;
	}
	case OP_PSQ_LU:
	{
		ulong addr = kvmppc_get_gpr(vcpu, ax_ra);
		bool w = inst_get_field(inst, 16, 16) ? true : false;
		int i = inst_get_field(inst, 17, 19);

		addr += get_d_signext(inst);
		emulated = kvmppc_emulate_psq_load(run, vcpu, ax_rd, addr, w, i);

		if (emulated == EMULATE_DONE)
			kvmppc_set_gpr(vcpu, ax_ra, addr);
		break;
	}
	case OP_PSQ_ST:
	{
		ulong addr = ax_ra ? kvmppc_get_gpr(vcpu, ax_ra) : 0;
		bool w = inst_get_field(inst, 16, 16) ? true : false;
		int i = inst_get_field(inst, 17, 19);

		addr += get_d_signext(inst);
		emulated = kvmppc_emulate_psq_store(run, vcpu, ax_rd, addr, w, i);
		break;
	}
	case OP_PSQ_STU:
	{
		ulong addr = kvmppc_get_gpr(vcpu, ax_ra);
		bool w = inst_get_field(inst, 16, 16) ? true : false;
		int i = inst_get_field(inst, 17, 19);

		addr += get_d_signext(inst);
		emulated = kvmppc_emulate_psq_store(run, vcpu, ax_rd, addr, w, i);

		if (emulated == EMULATE_DONE)
			kvmppc_set_gpr(vcpu, ax_ra, addr);
		break;
	}
	case 4:
		/* X form */
		switch (inst_get_field(inst, 21, 30)) {
		case OP_4X_PS_CMPU0:
			/* XXX */
			emulated = EMULATE_FAIL;
			break;
		case OP_4X_PSQ_LX:
		{
			ulong addr = ax_ra ? kvmppc_get_gpr(vcpu, ax_ra) : 0;
			bool w = inst_get_field(inst, 21, 21) ? true : false;
			int i = inst_get_field(inst, 22, 24);

			addr += kvmppc_get_gpr(vcpu, ax_rb);
			emulated = kvmppc_emulate_psq_load(run, vcpu, ax_rd, addr, w, i);
			break;
		}
		case OP_4X_PS_CMPO0:
			/* XXX */
			emulated = EMULATE_FAIL;
			break;
		case OP_4X_PSQ_LUX:
		{
			ulong addr = kvmppc_get_gpr(vcpu, ax_ra);
			bool w = inst_get_field(inst, 21, 21) ? true : false;
			int i = inst_get_field(inst, 22, 24);

			addr += kvmppc_get_gpr(vcpu, ax_rb);
			emulated = kvmppc_emulate_psq_load(run, vcpu, ax_rd, addr, w, i);

			if (emulated == EMULATE_DONE)
				kvmppc_set_gpr(vcpu, ax_ra, addr);
			break;
		}
		case OP_4X_PS_NEG:
			VCPU_FPR(vcpu, ax_rd) = VCPU_FPR(vcpu, ax_rb);
			VCPU_FPR(vcpu, ax_rd) ^= 0x8000000000000000ULL;
			vcpu->arch.qpr[ax_rd] = vcpu->arch.qpr[ax_rb];
			vcpu->arch.qpr[ax_rd] ^= 0x80000000;
			break;
		case OP_4X_PS_CMPU1:
			/* XXX */
			emulated = EMULATE_FAIL;
			break;
		case OP_4X_PS_MR:
			WARN_ON(rcomp);
			VCPU_FPR(vcpu, ax_rd) = VCPU_FPR(vcpu, ax_rb);
			vcpu->arch.qpr[ax_rd] = vcpu->arch.qpr[ax_rb];
			break;
		case OP_4X_PS_CMPO1:
			/* XXX */
			emulated = EMULATE_FAIL;
			break;
		case OP_4X_PS_NABS:
			WARN_ON(rcomp);
			VCPU_FPR(vcpu, ax_rd) = VCPU_FPR(vcpu, ax_rb);
			VCPU_FPR(vcpu, ax_rd) |= 0x8000000000000000ULL;
			vcpu->arch.qpr[ax_rd] = vcpu->arch.qpr[ax_rb];
			vcpu->arch.qpr[ax_rd] |= 0x80000000;
			break;
		case OP_4X_PS_ABS:
			WARN_ON(rcomp);
			VCPU_FPR(vcpu, ax_rd) = VCPU_FPR(vcpu, ax_rb);
			VCPU_FPR(vcpu, ax_rd) &= ~0x8000000000000000ULL;
			vcpu->arch.qpr[ax_rd] = vcpu->arch.qpr[ax_rb];
			vcpu->arch.qpr[ax_rd] &= ~0x80000000;
			break;
		case OP_4X_PS_MERGE00:
			WARN_ON(rcomp);
			VCPU_FPR(vcpu, ax_rd) = VCPU_FPR(vcpu, ax_ra);
			/* vcpu->arch.qpr[ax_rd] = VCPU_FPR(vcpu, ax_rb); */
			kvm_cvt_df(&VCPU_FPR(vcpu, ax_rb),
				   &vcpu->arch.qpr[ax_rd]);
			break;
		case OP_4X_PS_MERGE01:
			WARN_ON(rcomp);
			VCPU_FPR(vcpu, ax_rd) = VCPU_FPR(vcpu, ax_ra);
			vcpu->arch.qpr[ax_rd] = vcpu->arch.qpr[ax_rb];
			break;
		case OP_4X_PS_MERGE10:
			WARN_ON(rcomp);
			/* VCPU_FPR(vcpu, ax_rd) = vcpu->arch.qpr[ax_ra]; */
			kvm_cvt_fd(&vcpu->arch.qpr[ax_ra],
				   &VCPU_FPR(vcpu, ax_rd));
			/* vcpu->arch.qpr[ax_rd] = VCPU_FPR(vcpu, ax_rb); */
			kvm_cvt_df(&VCPU_FPR(vcpu, ax_rb),
				   &vcpu->arch.qpr[ax_rd]);
			break;
		case OP_4X_PS_MERGE11:
			WARN_ON(rcomp);
			/* VCPU_FPR(vcpu, ax_rd) = vcpu->arch.qpr[ax_ra]; */
			kvm_cvt_fd(&vcpu->arch.qpr[ax_ra],
				   &VCPU_FPR(vcpu, ax_rd));
			vcpu->arch.qpr[ax_rd] = vcpu->arch.qpr[ax_rb];
			break;
		}
		/* XW form */
		switch (inst_get_field(inst, 25, 30)) {
		case OP_4XW_PSQ_STX:
		{
			ulong addr = ax_ra ? kvmppc_get_gpr(vcpu, ax_ra) : 0;
			bool w = inst_get_field(inst, 21, 21) ? true : false;
			int i = inst_get_field(inst, 22, 24);

			addr += kvmppc_get_gpr(vcpu, ax_rb);
			emulated = kvmppc_emulate_psq_store(run, vcpu, ax_rd, addr, w, i);
			break;
		}
		case OP_4XW_PSQ_STUX:
		{
			ulong addr = kvmppc_get_gpr(vcpu, ax_ra);
			bool w = inst_get_field(inst, 21, 21) ? true : false;
			int i = inst_get_field(inst, 22, 24);

			addr += kvmppc_get_gpr(vcpu, ax_rb);
			emulated = kvmppc_emulate_psq_store(run, vcpu, ax_rd, addr, w, i);

			if (emulated == EMULATE_DONE)
				kvmppc_set_gpr(vcpu, ax_ra, addr);
			break;
		}
		}
		/* A form */
		switch (inst_get_field(inst, 26, 30)) {
		case OP_4A_PS_SUM1:
			emulated = kvmppc_ps_two_in(vcpu, rcomp, ax_rd,
					ax_rb, ax_ra, SCALAR_NO_PS0 | SCALAR_HIGH, fps_fadds);
			VCPU_FPR(vcpu, ax_rd) = VCPU_FPR(vcpu, ax_rc);
			break;
		case OP_4A_PS_SUM0:
			emulated = kvmppc_ps_two_in(vcpu, rcomp, ax_rd,
					ax_ra, ax_rb, SCALAR_NO_PS1 | SCALAR_LOW, fps_fadds);
			vcpu->arch.qpr[ax_rd] = vcpu->arch.qpr[ax_rc];
			break;
		case OP_4A_PS_MULS0:
			emulated = kvmppc_ps_two_in(vcpu, rcomp, ax_rd,
					ax_ra, ax_rc, SCALAR_HIGH, fps_fmuls);
			break;
		case OP_4A_PS_MULS1:
			emulated = kvmppc_ps_two_in(vcpu, rcomp, ax_rd,
					ax_ra, ax_rc, SCALAR_LOW, fps_fmuls);
			break;
		case OP_4A_PS_MADDS0:
			emulated = kvmppc_ps_three_in(vcpu, rcomp, ax_rd,
					ax_ra, ax_rc, ax_rb, SCALAR_HIGH, fps_fmadds);
			break;
		case OP_4A_PS_MADDS1:
			emulated = kvmppc_ps_three_in(vcpu, rcomp, ax_rd,
					ax_ra, ax_rc, ax_rb, SCALAR_LOW, fps_fmadds);
			break;
		case OP_4A_PS_DIV:
			emulated = kvmppc_ps_two_in(vcpu, rcomp, ax_rd,
					ax_ra, ax_rb, SCALAR_NONE, fps_fdivs);
			break;
		case OP_4A_PS_SUB:
			emulated = kvmppc_ps_two_in(vcpu, rcomp, ax_rd,
					ax_ra, ax_rb, SCALAR_NONE, fps_fsubs);
			break;
		case OP_4A_PS_ADD:
			emulated = kvmppc_ps_two_in(vcpu, rcomp, ax_rd,
					ax_ra, ax_rb, SCALAR_NONE, fps_fadds);
			break;
		case OP_4A_PS_SEL:
			emulated = kvmppc_ps_three_in(vcpu, rcomp, ax_rd,
					ax_ra, ax_rc, ax_rb, SCALAR_NONE, fps_fsel);
			break;
		case OP_4A_PS_RES:
			emulated = kvmppc_ps_one_in(vcpu, rcomp, ax_rd,
					ax_rb, fps_fres);
			break;
		case OP_4A_PS_MUL:
			emulated = kvmppc_ps_two_in(vcpu, rcomp, ax_rd,
					ax_ra, ax_rc, SCALAR_NONE, fps_fmuls);
			break;
		case OP_4A_PS_RSQRTE:
			emulated = kvmppc_ps_one_in(vcpu, rcomp, ax_rd,
					ax_rb, fps_frsqrte);
			break;
		case OP_4A_PS_MSUB:
			emulated = kvmppc_ps_three_in(vcpu, rcomp, ax_rd,
					ax_ra, ax_rc, ax_rb, SCALAR_NONE, fps_fmsubs);
			break;
		case OP_4A_PS_MADD:
			emulated = kvmppc_ps_three_in(vcpu, rcomp, ax_rd,
					ax_ra, ax_rc, ax_rb, SCALAR_NONE, fps_fmadds);
			break;
		case OP_4A_PS_NMSUB:
			emulated = kvmppc_ps_three_in(vcpu, rcomp, ax_rd,
					ax_ra, ax_rc, ax_rb, SCALAR_NONE, fps_fnmsubs);
			break;
		case OP_4A_PS_NMADD:
			emulated = kvmppc_ps_three_in(vcpu, rcomp, ax_rd,
					ax_ra, ax_rc, ax_rb, SCALAR_NONE, fps_fnmadds);
			break;
		}
		break;

	/* Real FPU operations */

	case OP_LFS:
	{
		ulong addr = (ax_ra ? kvmppc_get_gpr(vcpu, ax_ra) : 0) + full_d;

		emulated = kvmppc_emulate_fpr_load(run, vcpu, ax_rd, addr,
						   FPU_LS_SINGLE);
		break;
	}
	case OP_LFSU:
	{
		ulong addr = kvmppc_get_gpr(vcpu, ax_ra) + full_d;

		emulated = kvmppc_emulate_fpr_load(run, vcpu, ax_rd, addr,
						   FPU_LS_SINGLE);

		if (emulated == EMULATE_DONE)
			kvmppc_set_gpr(vcpu, ax_ra, addr);
		break;
	}
	case OP_LFD:
	{
		ulong addr = (ax_ra ? kvmppc_get_gpr(vcpu, ax_ra) : 0) + full_d;

		emulated = kvmppc_emulate_fpr_load(run, vcpu, ax_rd, addr,
						   FPU_LS_DOUBLE);
		break;
	}
	case OP_LFDU:
	{
		ulong addr = kvmppc_get_gpr(vcpu, ax_ra) + full_d;

		emulated = kvmppc_emulate_fpr_load(run, vcpu, ax_rd, addr,
						   FPU_LS_DOUBLE);

		if (emulated == EMULATE_DONE)
			kvmppc_set_gpr(vcpu, ax_ra, addr);
		break;
	}
	case OP_STFS:
	{
		ulong addr = (ax_ra ? kvmppc_get_gpr(vcpu, ax_ra) : 0) + full_d;

		emulated = kvmppc_emulate_fpr_store(run, vcpu, ax_rd, addr,
						    FPU_LS_SINGLE);
		break;
	}
	case OP_STFSU:
	{
		ulong addr = kvmppc_get_gpr(vcpu, ax_ra) + full_d;

		emulated = kvmppc_emulate_fpr_store(run, vcpu, ax_rd, addr,
						    FPU_LS_SINGLE);

		if (emulated == EMULATE_DONE)
			kvmppc_set_gpr(vcpu, ax_ra, addr);
		break;
	}
	case OP_STFD:
	{
		ulong addr = (ax_ra ? kvmppc_get_gpr(vcpu, ax_ra) : 0) + full_d;

		emulated = kvmppc_emulate_fpr_store(run, vcpu, ax_rd, addr,
						    FPU_LS_DOUBLE);
		break;
	}
	case OP_STFDU:
	{
		ulong addr = kvmppc_get_gpr(vcpu, ax_ra) + full_d;

		emulated = kvmppc_emulate_fpr_store(run, vcpu, ax_rd, addr,
						    FPU_LS_DOUBLE);

		if (emulated == EMULATE_DONE)
			kvmppc_set_gpr(vcpu, ax_ra, addr);
		break;
	}
	case 31:
		switch (inst_get_field(inst, 21, 30)) {
		case OP_31_LFSX:
		{
			ulong addr = ax_ra ? kvmppc_get_gpr(vcpu, ax_ra) : 0;

			addr += kvmppc_get_gpr(vcpu, ax_rb);
			emulated = kvmppc_emulate_fpr_load(run, vcpu, ax_rd,
							   addr, FPU_LS_SINGLE);
			break;
		}
		case OP_31_LFSUX:
		{
			ulong addr = kvmppc_get_gpr(vcpu, ax_ra) +
				     kvmppc_get_gpr(vcpu, ax_rb);

			emulated = kvmppc_emulate_fpr_load(run, vcpu, ax_rd,
							   addr, FPU_LS_SINGLE);

			if (emulated == EMULATE_DONE)
				kvmppc_set_gpr(vcpu, ax_ra, addr);
			break;
		}
		case OP_31_LFDX:
		{
			ulong addr = (ax_ra ? kvmppc_get_gpr(vcpu, ax_ra) : 0) +
				     kvmppc_get_gpr(vcpu, ax_rb);

			emulated = kvmppc_emulate_fpr_load(run, vcpu, ax_rd,
							   addr, FPU_LS_DOUBLE);
			break;
		}
		case OP_31_LFDUX:
		{
			ulong addr = kvmppc_get_gpr(vcpu, ax_ra) +
				     kvmppc_get_gpr(vcpu, ax_rb);

			emulated = kvmppc_emulate_fpr_load(run, vcpu, ax_rd,
							   addr, FPU_LS_DOUBLE);

			if (emulated == EMULATE_DONE)
				kvmppc_set_gpr(vcpu, ax_ra, addr);
			break;
		}
		case OP_31_STFSX:
		{
			ulong addr = (ax_ra ? kvmppc_get_gpr(vcpu, ax_ra) : 0) +
				     kvmppc_get_gpr(vcpu, ax_rb);

			emulated = kvmppc_emulate_fpr_store(run, vcpu, ax_rd,
							    addr, FPU_LS_SINGLE);
			break;
		}
		case OP_31_STFSUX:
		{
			ulong addr = kvmppc_get_gpr(vcpu, ax_ra) +
				     kvmppc_get_gpr(vcpu, ax_rb);

			emulated = kvmppc_emulate_fpr_store(run, vcpu, ax_rd,
							    addr, FPU_LS_SINGLE);

			if (emulated == EMULATE_DONE)
				kvmppc_set_gpr(vcpu, ax_ra, addr);
			break;
		}
		case OP_31_STFX:
		{
			ulong addr = (ax_ra ? kvmppc_get_gpr(vcpu, ax_ra) : 0) +
				     kvmppc_get_gpr(vcpu, ax_rb);

			emulated = kvmppc_emulate_fpr_store(run, vcpu, ax_rd,
							    addr, FPU_LS_DOUBLE);
			break;
		}
		case OP_31_STFUX:
		{
			ulong addr = kvmppc_get_gpr(vcpu, ax_ra) +
				     kvmppc_get_gpr(vcpu, ax_rb);

			emulated = kvmppc_emulate_fpr_store(run, vcpu, ax_rd,
							    addr, FPU_LS_DOUBLE);

			if (emulated == EMULATE_DONE)
				kvmppc_set_gpr(vcpu, ax_ra, addr);
			break;
		}
		case OP_31_STFIWX:
		{
			ulong addr = (ax_ra ? kvmppc_get_gpr(vcpu, ax_ra) : 0) +
				     kvmppc_get_gpr(vcpu, ax_rb);

			emulated = kvmppc_emulate_fpr_store(run, vcpu, ax_rd,
							    addr,
							    FPU_LS_SINGLE_LOW);
			break;
		}
			break;
		}
		break;
	case 59:
		switch (inst_get_field(inst, 21, 30)) {
		case OP_59_FADDS:
			fpd_fadds(&vcpu->arch.fp.fpscr, &cr, fpr_d, fpr_a, fpr_b);
			kvmppc_sync_qpr(vcpu, ax_rd);
			break;
		case OP_59_FSUBS:
			fpd_fsubs(&vcpu->arch.fp.fpscr, &cr, fpr_d, fpr_a, fpr_b);
			kvmppc_sync_qpr(vcpu, ax_rd);
			break;
		case OP_59_FDIVS:
			fpd_fdivs(&vcpu->arch.fp.fpscr, &cr, fpr_d, fpr_a, fpr_b);
			kvmppc_sync_qpr(vcpu, ax_rd);
			break;
		case OP_59_FRES:
			fpd_fres(&vcpu->arch.fp.fpscr, &cr, fpr_d, fpr_b);
			kvmppc_sync_qpr(vcpu, ax_rd);
			break;
		case OP_59_FRSQRTES:
			fpd_frsqrtes(&vcpu->arch.fp.fpscr, &cr, fpr_d, fpr_b);
			kvmppc_sync_qpr(vcpu, ax_rd);
			break;
		}
		switch (inst_get_field(inst, 26, 30)) {
		case OP_59_FMULS:
			fpd_fmuls(&vcpu->arch.fp.fpscr, &cr, fpr_d, fpr_a, fpr_c);
			kvmppc_sync_qpr(vcpu, ax_rd);
			break;
		case OP_59_FMSUBS:
			fpd_fmsubs(&vcpu->arch.fp.fpscr, &cr, fpr_d, fpr_a, fpr_c, fpr_b);
			kvmppc_sync_qpr(vcpu, ax_rd);
			break;
		case OP_59_FMADDS:
			fpd_fmadds(&vcpu->arch.fp.fpscr, &cr, fpr_d, fpr_a, fpr_c, fpr_b);
			kvmppc_sync_qpr(vcpu, ax_rd);
			break;
		case OP_59_FNMSUBS:
			fpd_fnmsubs(&vcpu->arch.fp.fpscr, &cr, fpr_d, fpr_a, fpr_c, fpr_b);
			kvmppc_sync_qpr(vcpu, ax_rd);
			break;
		case OP_59_FNMADDS:
			fpd_fnmadds(&vcpu->arch.fp.fpscr, &cr, fpr_d, fpr_a, fpr_c, fpr_b);
			kvmppc_sync_qpr(vcpu, ax_rd);
			break;
		}
		break;
	case 63:
		switch (inst_get_field(inst, 21, 30)) {
		case OP_63_MTFSB0:
		case OP_63_MTFSB1:
		case OP_63_MCRFS:
		case OP_63_MTFSFI:
			/* XXX need to implement */
			break;
		case OP_63_MFFS:
			/* XXX missing CR */
			*fpr_d = vcpu->arch.fp.fpscr;
			break;
		case OP_63_MTFSF:
			/* XXX missing fm bits */
			/* XXX missing CR */
			vcpu->arch.fp.fpscr = *fpr_b;
			break;
		case OP_63_FCMPU:
		{
			u32 tmp_cr;
			u32 cr0_mask = 0xf0000000;
			u32 cr_shift = inst_get_field(inst, 6, 8) * 4;

			fpd_fcmpu(&vcpu->arch.fp.fpscr, &tmp_cr, fpr_a, fpr_b);
			cr &= ~(cr0_mask >> cr_shift);
			cr |= (cr & cr0_mask) >> cr_shift;
			break;
		}
		case OP_63_FCMPO:
		{
			u32 tmp_cr;
			u32 cr0_mask = 0xf0000000;
			u32 cr_shift = inst_get_field(inst, 6, 8) * 4;

			fpd_fcmpo(&vcpu->arch.fp.fpscr, &tmp_cr, fpr_a, fpr_b);
			cr &= ~(cr0_mask >> cr_shift);
			cr |= (cr & cr0_mask) >> cr_shift;
			break;
		}
		case OP_63_FNEG:
			fpd_fneg(&vcpu->arch.fp.fpscr, &cr, fpr_d, fpr_b);
			break;
		case OP_63_FMR:
			*fpr_d = *fpr_b;
			break;
		case OP_63_FABS:
			fpd_fabs(&vcpu->arch.fp.fpscr, &cr, fpr_d, fpr_b);
			break;
		case OP_63_FCPSGN:
			fpd_fcpsgn(&vcpu->arch.fp.fpscr, &cr, fpr_d, fpr_a, fpr_b);
			break;
		case OP_63_FDIV:
			fpd_fdiv(&vcpu->arch.fp.fpscr, &cr, fpr_d, fpr_a, fpr_b);
			break;
		case OP_63_FADD:
			fpd_fadd(&vcpu->arch.fp.fpscr, &cr, fpr_d, fpr_a, fpr_b);
			break;
		case OP_63_FSUB:
			fpd_fsub(&vcpu->arch.fp.fpscr, &cr, fpr_d, fpr_a, fpr_b);
			break;
		case OP_63_FCTIW:
			fpd_fctiw(&vcpu->arch.fp.fpscr, &cr, fpr_d, fpr_b);
			break;
		case OP_63_FCTIWZ:
			fpd_fctiwz(&vcpu->arch.fp.fpscr, &cr, fpr_d, fpr_b);
			break;
		case OP_63_FRSP:
			fpd_frsp(&vcpu->arch.fp.fpscr, &cr, fpr_d, fpr_b);
			kvmppc_sync_qpr(vcpu, ax_rd);
			break;
		case OP_63_FRSQRTE:
		{
			double one = 1.0f;

			/* fD = sqrt(fB) */
			fpd_fsqrt(&vcpu->arch.fp.fpscr, &cr, fpr_d, fpr_b);
			/* fD = 1.0f / fD */
			fpd_fdiv(&vcpu->arch.fp.fpscr, &cr, fpr_d, (u64*)&one, fpr_d);
			break;
		}
		}
		switch (inst_get_field(inst, 26, 30)) {
		case OP_63_FMUL:
			fpd_fmul(&vcpu->arch.fp.fpscr, &cr, fpr_d, fpr_a, fpr_c);
			break;
		case OP_63_FSEL:
			fpd_fsel(&vcpu->arch.fp.fpscr, &cr, fpr_d, fpr_a, fpr_c, fpr_b);
			break;
		case OP_63_FMSUB:
			fpd_fmsub(&vcpu->arch.fp.fpscr, &cr, fpr_d, fpr_a, fpr_c, fpr_b);
			break;
		case OP_63_FMADD:
			fpd_fmadd(&vcpu->arch.fp.fpscr, &cr, fpr_d, fpr_a, fpr_c, fpr_b);
			break;
		case OP_63_FNMSUB:
			fpd_fnmsub(&vcpu->arch.fp.fpscr, &cr, fpr_d, fpr_a, fpr_c, fpr_b);
			break;
		case OP_63_FNMADD:
			fpd_fnmadd(&vcpu->arch.fp.fpscr, &cr, fpr_d, fpr_a, fpr_c, fpr_b);
			break;
		}
		break;
	}

#ifdef DEBUG
	for (i = 0; i < ARRAY_SIZE(vcpu->arch.fp.fpr); i++) {
		u32 f;
		kvm_cvt_df(&VCPU_FPR(vcpu, i), &f);
		dprintk(KERN_INFO "FPR[%d] = 0x%x\n", i, f);
	}
#endif

	if (rcomp)
		kvmppc_set_cr(vcpu, cr);

	preempt_enable();

	return emulated;
}
