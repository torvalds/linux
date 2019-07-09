/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 - Virtual Open Systems and Columbia University
 * Authors: Christoffer Dall <c.dall@virtualopensystems.com>
 */

#ifndef __ARM_KVM_COPROC_LOCAL_H__
#define __ARM_KVM_COPROC_LOCAL_H__

struct coproc_params {
	unsigned long CRn;
	unsigned long CRm;
	unsigned long Op1;
	unsigned long Op2;
	unsigned long Rt1;
	unsigned long Rt2;
	bool is_64bit;
	bool is_write;
};

struct coproc_reg {
	/* MRC/MCR/MRRC/MCRR instruction which accesses it. */
	unsigned long CRn;
	unsigned long CRm;
	unsigned long Op1;
	unsigned long Op2;

	bool is_64bit;

	/* Trapped access from guest, if non-NULL. */
	bool (*access)(struct kvm_vcpu *,
		       const struct coproc_params *,
		       const struct coproc_reg *);

	/* Initialization for vcpu. */
	void (*reset)(struct kvm_vcpu *, const struct coproc_reg *);

	/* Index into vcpu_cp15(vcpu, ...), or 0 if we don't need to save it. */
	unsigned long reg;

	/* Value (usually reset value) */
	u64 val;
};

static inline void print_cp_instr(const struct coproc_params *p)
{
	/* Look, we even formatted it for you to paste into the table! */
	if (p->is_64bit) {
		kvm_pr_unimpl(" { CRm64(%2lu), Op1(%2lu), is64, func_%s },\n",
			      p->CRn, p->Op1, p->is_write ? "write" : "read");
	} else {
		kvm_pr_unimpl(" { CRn(%2lu), CRm(%2lu), Op1(%2lu), Op2(%2lu), is32,"
			      " func_%s },\n",
			      p->CRn, p->CRm, p->Op1, p->Op2,
			      p->is_write ? "write" : "read");
	}
}

static inline bool ignore_write(struct kvm_vcpu *vcpu,
				const struct coproc_params *p)
{
	return true;
}

static inline bool read_zero(struct kvm_vcpu *vcpu,
			     const struct coproc_params *p)
{
	*vcpu_reg(vcpu, p->Rt1) = 0;
	return true;
}

/* Reset functions */
static inline void reset_unknown(struct kvm_vcpu *vcpu,
				 const struct coproc_reg *r)
{
	BUG_ON(!r->reg);
	BUG_ON(r->reg >= ARRAY_SIZE(vcpu->arch.ctxt.cp15));
	vcpu_cp15(vcpu, r->reg) = 0xdecafbad;
}

static inline void reset_val(struct kvm_vcpu *vcpu, const struct coproc_reg *r)
{
	BUG_ON(!r->reg);
	BUG_ON(r->reg >= ARRAY_SIZE(vcpu->arch.ctxt.cp15));
	vcpu_cp15(vcpu, r->reg) = r->val;
}

static inline void reset_unknown64(struct kvm_vcpu *vcpu,
				   const struct coproc_reg *r)
{
	BUG_ON(!r->reg);
	BUG_ON(r->reg + 1 >= ARRAY_SIZE(vcpu->arch.ctxt.cp15));

	vcpu_cp15(vcpu, r->reg) = 0xdecafbad;
	vcpu_cp15(vcpu, r->reg+1) = 0xd0c0ffee;
}

static inline int cmp_reg(const struct coproc_reg *i1,
			  const struct coproc_reg *i2)
{
	BUG_ON(i1 == i2);
	if (!i1)
		return 1;
	else if (!i2)
		return -1;
	if (i1->CRn != i2->CRn)
		return i1->CRn - i2->CRn;
	if (i1->CRm != i2->CRm)
		return i1->CRm - i2->CRm;
	if (i1->Op1 != i2->Op1)
		return i1->Op1 - i2->Op1;
	if (i1->Op2 != i2->Op2)
		return i1->Op2 - i2->Op2;
	return i2->is_64bit - i1->is_64bit;
}


#define CRn(_x)		.CRn = _x
#define CRm(_x) 	.CRm = _x
#define CRm64(_x)       .CRn = _x, .CRm = 0
#define Op1(_x) 	.Op1 = _x
#define Op2(_x) 	.Op2 = _x
#define is64		.is_64bit = true
#define is32		.is_64bit = false

bool access_vm_reg(struct kvm_vcpu *vcpu,
		   const struct coproc_params *p,
		   const struct coproc_reg *r);

#endif /* __ARM_KVM_COPROC_LOCAL_H__ */
