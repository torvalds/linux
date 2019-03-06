/*
 * Copyright (C) 2012,2013 - ARM Ltd
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 *
 * Derived from arch/arm/kvm/coproc.h
 * Copyright (C) 2012 - Virtual Open Systems and Columbia University
 * Authors: Christoffer Dall <c.dall@virtualopensystems.com>
 *
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
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __ARM64_KVM_SYS_REGS_LOCAL_H__
#define __ARM64_KVM_SYS_REGS_LOCAL_H__

struct sys_reg_params {
	u8	Op0;
	u8	Op1;
	u8	CRn;
	u8	CRm;
	u8	Op2;
	u64	regval;
	bool	is_write;
	bool	is_aarch32;
	bool	is_32bit;	/* Only valid if is_aarch32 is true */
};

struct sys_reg_desc {
	/* Sysreg string for debug */
	const char *name;

	/* MRS/MSR instruction which accesses it. */
	u8	Op0;
	u8	Op1;
	u8	CRn;
	u8	CRm;
	u8	Op2;

	/* Trapped access from guest, if non-NULL. */
	bool (*access)(struct kvm_vcpu *,
		       struct sys_reg_params *,
		       const struct sys_reg_desc *);

	/* Initialization for vcpu. */
	void (*reset)(struct kvm_vcpu *, const struct sys_reg_desc *);

	/* Index into sys_reg[], or 0 if we don't need to save it. */
	int reg;

	/* Value (usually reset value) */
	u64 val;

	/* Custom get/set_user functions, fallback to generic if NULL */
	int (*get_user)(struct kvm_vcpu *vcpu, const struct sys_reg_desc *rd,
			const struct kvm_one_reg *reg, void __user *uaddr);
	int (*set_user)(struct kvm_vcpu *vcpu, const struct sys_reg_desc *rd,
			const struct kvm_one_reg *reg, void __user *uaddr);
};

static inline void print_sys_reg_instr(const struct sys_reg_params *p)
{
	/* Look, we even formatted it for you to paste into the table! */
	kvm_pr_unimpl(" { Op0(%2u), Op1(%2u), CRn(%2u), CRm(%2u), Op2(%2u), func_%s },\n",
		      p->Op0, p->Op1, p->CRn, p->CRm, p->Op2, p->is_write ? "write" : "read");
}

static inline bool ignore_write(struct kvm_vcpu *vcpu,
				const struct sys_reg_params *p)
{
	return true;
}

static inline bool read_zero(struct kvm_vcpu *vcpu,
			     struct sys_reg_params *p)
{
	p->regval = 0;
	return true;
}

/* Reset functions */
static inline void reset_unknown(struct kvm_vcpu *vcpu,
				 const struct sys_reg_desc *r)
{
	BUG_ON(!r->reg);
	BUG_ON(r->reg >= NR_SYS_REGS);
	__vcpu_sys_reg(vcpu, r->reg) = 0x1de7ec7edbadc0deULL;
}

static inline void reset_val(struct kvm_vcpu *vcpu, const struct sys_reg_desc *r)
{
	BUG_ON(!r->reg);
	BUG_ON(r->reg >= NR_SYS_REGS);
	__vcpu_sys_reg(vcpu, r->reg) = r->val;
}

static inline int cmp_sys_reg(const struct sys_reg_desc *i1,
			      const struct sys_reg_desc *i2)
{
	BUG_ON(i1 == i2);
	if (!i1)
		return 1;
	else if (!i2)
		return -1;
	if (i1->Op0 != i2->Op0)
		return i1->Op0 - i2->Op0;
	if (i1->Op1 != i2->Op1)
		return i1->Op1 - i2->Op1;
	if (i1->CRn != i2->CRn)
		return i1->CRn - i2->CRn;
	if (i1->CRm != i2->CRm)
		return i1->CRm - i2->CRm;
	return i1->Op2 - i2->Op2;
}

const struct sys_reg_desc *find_reg_by_id(u64 id,
					  struct sys_reg_params *params,
					  const struct sys_reg_desc table[],
					  unsigned int num);

#define Op0(_x) 	.Op0 = _x
#define Op1(_x) 	.Op1 = _x
#define CRn(_x)		.CRn = _x
#define CRm(_x) 	.CRm = _x
#define Op2(_x) 	.Op2 = _x

#define SYS_DESC(reg)					\
	.name = #reg,					\
	Op0(sys_reg_Op0(reg)), Op1(sys_reg_Op1(reg)),	\
	CRn(sys_reg_CRn(reg)), CRm(sys_reg_CRm(reg)),	\
	Op2(sys_reg_Op2(reg))

#endif /* __ARM64_KVM_SYS_REGS_LOCAL_H__ */
