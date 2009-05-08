/*
 * kvm_vcpu.c: handling all virtual cpu related thing.
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
 *  Shaofan Li (Susue Li) <susie.li@intel.com>
 *  Yaozu Dong (Eddie Dong) (Eddie.dong@intel.com)
 *  Xuefei Xu (Anthony Xu) (Anthony.xu@intel.com)
 *  Xiantao Zhang <xiantao.zhang@intel.com>
 */

#include <linux/kvm_host.h>
#include <linux/types.h>

#include <asm/processor.h>
#include <asm/ia64regs.h>
#include <asm/gcc_intrin.h>
#include <asm/kregs.h>
#include <asm/pgtable.h>
#include <asm/tlb.h>

#include "asm-offsets.h"
#include "vcpu.h"

/*
 * Special notes:
 * - Index by it/dt/rt sequence
 * - Only existing mode transitions are allowed in this table
 * - RSE is placed at lazy mode when emulating guest partial mode
 * - If gva happens to be rr0 and rr4, only allowed case is identity
 *   mapping (gva=gpa), or panic! (How?)
 */
int mm_switch_table[8][8] = {
	/*  2004/09/12(Kevin): Allow switch to self */
	/*
	 *  (it,dt,rt): (0,0,0) -> (1,1,1)
	 *  This kind of transition usually occurs in the very early
	 *  stage of Linux boot up procedure. Another case is in efi
	 *  and pal calls. (see "arch/ia64/kernel/head.S")
	 *
	 *  (it,dt,rt): (0,0,0) -> (0,1,1)
	 *  This kind of transition is found when OSYa exits efi boot
	 *  service. Due to gva = gpa in this case (Same region),
	 *  data access can be satisfied though itlb entry for physical
	 *  emulation is hit.
	 */
	{SW_SELF, 0,  0,  SW_NOP, 0,  0,  0,  SW_P2V},
	{0,  0,  0,  0,  0,  0,  0,  0},
	{0,  0,  0,  0,  0,  0,  0,  0},
	/*
	 *  (it,dt,rt): (0,1,1) -> (1,1,1)
	 *  This kind of transition is found in OSYa.
	 *
	 *  (it,dt,rt): (0,1,1) -> (0,0,0)
	 *  This kind of transition is found in OSYa
	 */
	{SW_NOP, 0,  0,  SW_SELF, 0,  0,  0,  SW_P2V},
	/* (1,0,0)->(1,1,1) */
	{0,  0,  0,  0,  0,  0,  0,  SW_P2V},
	/*
	 *  (it,dt,rt): (1,0,1) -> (1,1,1)
	 *  This kind of transition usually occurs when Linux returns
	 *  from the low level TLB miss handlers.
	 *  (see "arch/ia64/kernel/ivt.S")
	 */
	{0,  0,  0,  0,  0,  SW_SELF, 0,  SW_P2V},
	{0,  0,  0,  0,  0,  0,  0,  0},
	/*
	 *  (it,dt,rt): (1,1,1) -> (1,0,1)
	 *  This kind of transition usually occurs in Linux low level
	 *  TLB miss handler. (see "arch/ia64/kernel/ivt.S")
	 *
	 *  (it,dt,rt): (1,1,1) -> (0,0,0)
	 *  This kind of transition usually occurs in pal and efi calls,
	 *  which requires running in physical mode.
	 *  (see "arch/ia64/kernel/head.S")
	 *  (1,1,1)->(1,0,0)
	 */

	{SW_V2P, 0,  0,  0,  SW_V2P, SW_V2P, 0,  SW_SELF},
};

void physical_mode_init(struct kvm_vcpu  *vcpu)
{
	vcpu->arch.mode_flags = GUEST_IN_PHY;
}

void switch_to_physical_rid(struct kvm_vcpu *vcpu)
{
	unsigned long psr;

	/* Save original virtual mode rr[0] and rr[4] */
	psr = ia64_clear_ic();
	ia64_set_rr(VRN0<<VRN_SHIFT, vcpu->arch.metaphysical_rr0);
	ia64_srlz_d();
	ia64_set_rr(VRN4<<VRN_SHIFT, vcpu->arch.metaphysical_rr4);
	ia64_srlz_d();

	ia64_set_psr(psr);
	return;
}

void switch_to_virtual_rid(struct kvm_vcpu *vcpu)
{
	unsigned long psr;

	psr = ia64_clear_ic();
	ia64_set_rr(VRN0 << VRN_SHIFT, vcpu->arch.metaphysical_saved_rr0);
	ia64_srlz_d();
	ia64_set_rr(VRN4 << VRN_SHIFT, vcpu->arch.metaphysical_saved_rr4);
	ia64_srlz_d();
	ia64_set_psr(psr);
	return;
}

static int mm_switch_action(struct ia64_psr opsr, struct ia64_psr npsr)
{
	return mm_switch_table[MODE_IND(opsr)][MODE_IND(npsr)];
}

void switch_mm_mode(struct kvm_vcpu *vcpu, struct ia64_psr old_psr,
					struct ia64_psr new_psr)
{
	int act;
	act = mm_switch_action(old_psr, new_psr);
	switch (act) {
	case SW_V2P:
		/*printk("V -> P mode transition: (0x%lx -> 0x%lx)\n",
		old_psr.val, new_psr.val);*/
		switch_to_physical_rid(vcpu);
		/*
		 * Set rse to enforced lazy, to prevent active rse
		 *save/restor when guest physical mode.
		 */
		vcpu->arch.mode_flags |= GUEST_IN_PHY;
		break;
	case SW_P2V:
		switch_to_virtual_rid(vcpu);
		/*
		 * recover old mode which is saved when entering
		 * guest physical mode
		 */
		vcpu->arch.mode_flags &= ~GUEST_IN_PHY;
		break;
	case SW_SELF:
		break;
	case SW_NOP:
		break;
	default:
		/* Sanity check */
		break;
	}
	return;
}

/*
 * In physical mode, insert tc/tr for region 0 and 4 uses
 * RID[0] and RID[4] which is for physical mode emulation.
 * However what those inserted tc/tr wants is rid for
 * virtual mode. So original virtual rid needs to be restored
 * before insert.
 *
 * Operations which required such switch include:
 *  - insertions (itc.*, itr.*)
 *  - purges (ptc.* and ptr.*)
 *  - tpa
 *  - tak
 *  - thash?, ttag?
 * All above needs actual virtual rid for destination entry.
 */

void check_mm_mode_switch(struct kvm_vcpu *vcpu,  struct ia64_psr old_psr,
					struct ia64_psr new_psr)
{

	if ((old_psr.dt != new_psr.dt)
			|| (old_psr.it != new_psr.it)
			|| (old_psr.rt != new_psr.rt))
		switch_mm_mode(vcpu, old_psr, new_psr);

	return;
}


/*
 * In physical mode, insert tc/tr for region 0 and 4 uses
 * RID[0] and RID[4] which is for physical mode emulation.
 * However what those inserted tc/tr wants is rid for
 * virtual mode. So original virtual rid needs to be restored
 * before insert.
 *
 * Operations which required such switch include:
 *  - insertions (itc.*, itr.*)
 *  - purges (ptc.* and ptr.*)
 *  - tpa
 *  - tak
 *  - thash?, ttag?
 * All above needs actual virtual rid for destination entry.
 */

void prepare_if_physical_mode(struct kvm_vcpu *vcpu)
{
	if (is_physical_mode(vcpu)) {
		vcpu->arch.mode_flags |= GUEST_PHY_EMUL;
		switch_to_virtual_rid(vcpu);
	}
	return;
}

/* Recover always follows prepare */
void recover_if_physical_mode(struct kvm_vcpu *vcpu)
{
	if (is_physical_mode(vcpu))
		switch_to_physical_rid(vcpu);
	vcpu->arch.mode_flags &= ~GUEST_PHY_EMUL;
	return;
}

#define RPT(x)	((u16) &((struct kvm_pt_regs *)0)->x)

static u16 gr_info[32] = {
	0, 	/* r0 is read-only : WE SHOULD NEVER GET THIS */
	RPT(r1), RPT(r2), RPT(r3),
	RPT(r4), RPT(r5), RPT(r6), RPT(r7),
	RPT(r8), RPT(r9), RPT(r10), RPT(r11),
	RPT(r12), RPT(r13), RPT(r14), RPT(r15),
	RPT(r16), RPT(r17), RPT(r18), RPT(r19),
	RPT(r20), RPT(r21), RPT(r22), RPT(r23),
	RPT(r24), RPT(r25), RPT(r26), RPT(r27),
	RPT(r28), RPT(r29), RPT(r30), RPT(r31)
};

#define IA64_FIRST_STACKED_GR   32
#define IA64_FIRST_ROTATING_FR  32

static inline unsigned long
rotate_reg(unsigned long sor, unsigned long rrb, unsigned long reg)
{
	reg += rrb;
	if (reg >= sor)
		reg -= sor;
	return reg;
}

/*
 * Return the (rotated) index for floating point register
 * be in the REGNUM (REGNUM must range from 32-127,
 * result is in the range from 0-95.
 */
static inline unsigned long fph_index(struct kvm_pt_regs *regs,
						long regnum)
{
	unsigned long rrb_fr = (regs->cr_ifs >> 25) & 0x7f;
	return rotate_reg(96, rrb_fr, (regnum - IA64_FIRST_ROTATING_FR));
}

/*
 * The inverse of the above: given bspstore and the number of
 * registers, calculate ar.bsp.
 */
static inline unsigned long *kvm_rse_skip_regs(unsigned long *addr,
							long num_regs)
{
	long delta = ia64_rse_slot_num(addr) + num_regs;
	int i = 0;

	if (num_regs < 0)
		delta -= 0x3e;
	if (delta < 0) {
		while (delta <= -0x3f) {
			i--;
			delta += 0x3f;
		}
	} else {
		while (delta >= 0x3f) {
			i++;
			delta -= 0x3f;
		}
	}

	return addr + num_regs + i;
}

static void get_rse_reg(struct kvm_pt_regs *regs, unsigned long r1,
					unsigned long *val, int *nat)
{
	unsigned long *bsp, *addr, *rnat_addr, *bspstore;
	unsigned long *kbs = (void *) current_vcpu + VMM_RBS_OFFSET;
	unsigned long nat_mask;
	unsigned long old_rsc, new_rsc;
	long sof = (regs->cr_ifs) & 0x7f;
	long sor = (((regs->cr_ifs >> 14) & 0xf) << 3);
	long rrb_gr = (regs->cr_ifs >> 18) & 0x7f;
	long ridx = r1 - 32;

	if (ridx < sor)
		ridx = rotate_reg(sor, rrb_gr, ridx);

	old_rsc = ia64_getreg(_IA64_REG_AR_RSC);
	new_rsc = old_rsc&(~(0x3));
	ia64_setreg(_IA64_REG_AR_RSC, new_rsc);

	bspstore = (unsigned long *)ia64_getreg(_IA64_REG_AR_BSPSTORE);
	bsp = kbs + (regs->loadrs >> 19);

	addr = kvm_rse_skip_regs(bsp, -sof + ridx);
	nat_mask = 1UL << ia64_rse_slot_num(addr);
	rnat_addr = ia64_rse_rnat_addr(addr);

	if (addr >= bspstore) {
		ia64_flushrs();
		ia64_mf();
		bspstore = (unsigned long *)ia64_getreg(_IA64_REG_AR_BSPSTORE);
	}
	*val = *addr;
	if (nat) {
		if (bspstore < rnat_addr)
			*nat = (int)!!(ia64_getreg(_IA64_REG_AR_RNAT)
							& nat_mask);
		else
			*nat = (int)!!((*rnat_addr) & nat_mask);
		ia64_setreg(_IA64_REG_AR_RSC, old_rsc);
	}
}

void set_rse_reg(struct kvm_pt_regs *regs, unsigned long r1,
				unsigned long val, unsigned long nat)
{
	unsigned long *bsp, *bspstore, *addr, *rnat_addr;
	unsigned long *kbs = (void *) current_vcpu + VMM_RBS_OFFSET;
	unsigned long nat_mask;
	unsigned long old_rsc, new_rsc, psr;
	unsigned long rnat;
	long sof = (regs->cr_ifs) & 0x7f;
	long sor = (((regs->cr_ifs >> 14) & 0xf) << 3);
	long rrb_gr = (regs->cr_ifs >> 18) & 0x7f;
	long ridx = r1 - 32;

	if (ridx < sor)
		ridx = rotate_reg(sor, rrb_gr, ridx);

	old_rsc = ia64_getreg(_IA64_REG_AR_RSC);
	/* put RSC to lazy mode, and set loadrs 0 */
	new_rsc = old_rsc & (~0x3fff0003);
	ia64_setreg(_IA64_REG_AR_RSC, new_rsc);
	bsp = kbs + (regs->loadrs >> 19); /* 16 + 3 */

	addr = kvm_rse_skip_regs(bsp, -sof + ridx);
	nat_mask = 1UL << ia64_rse_slot_num(addr);
	rnat_addr = ia64_rse_rnat_addr(addr);

	local_irq_save(psr);
	bspstore = (unsigned long *)ia64_getreg(_IA64_REG_AR_BSPSTORE);
	if (addr >= bspstore) {

		ia64_flushrs();
		ia64_mf();
		*addr = val;
		bspstore = (unsigned long *)ia64_getreg(_IA64_REG_AR_BSPSTORE);
		rnat = ia64_getreg(_IA64_REG_AR_RNAT);
		if (bspstore < rnat_addr)
			rnat = rnat & (~nat_mask);
		else
			*rnat_addr = (*rnat_addr)&(~nat_mask);

		ia64_mf();
		ia64_loadrs();
		ia64_setreg(_IA64_REG_AR_RNAT, rnat);
	} else {
		rnat = ia64_getreg(_IA64_REG_AR_RNAT);
		*addr = val;
		if (bspstore < rnat_addr)
			rnat = rnat&(~nat_mask);
		else
			*rnat_addr = (*rnat_addr) & (~nat_mask);

		ia64_setreg(_IA64_REG_AR_BSPSTORE, (unsigned long)bspstore);
		ia64_setreg(_IA64_REG_AR_RNAT, rnat);
	}
	local_irq_restore(psr);
	ia64_setreg(_IA64_REG_AR_RSC, old_rsc);
}

void getreg(unsigned long regnum, unsigned long *val,
				int *nat, struct kvm_pt_regs *regs)
{
	unsigned long addr, *unat;
	if (regnum >= IA64_FIRST_STACKED_GR) {
		get_rse_reg(regs, regnum, val, nat);
		return;
	}

	/*
	 * Now look at registers in [0-31] range and init correct UNAT
	 */
	addr = (unsigned long)regs;
	unat = &regs->eml_unat;;

	addr += gr_info[regnum];

	*val  = *(unsigned long *)addr;
	/*
	 * do it only when requested
	 */
	if (nat)
		*nat  = (*unat >> ((addr >> 3) & 0x3f)) & 0x1UL;
}

void setreg(unsigned long regnum, unsigned long val,
			int nat, struct kvm_pt_regs *regs)
{
	unsigned long addr;
	unsigned long bitmask;
	unsigned long *unat;

	/*
	 * First takes care of stacked registers
	 */
	if (regnum >= IA64_FIRST_STACKED_GR) {
		set_rse_reg(regs, regnum, val, nat);
		return;
	}

	/*
	 * Now look at registers in [0-31] range and init correct UNAT
	 */
	addr = (unsigned long)regs;
	unat = &regs->eml_unat;
	/*
	 * add offset from base of struct
	 * and do it !
	 */
	addr += gr_info[regnum];

	*(unsigned long *)addr = val;

	/*
	 * We need to clear the corresponding UNAT bit to fully emulate the load
	 * UNAT bit_pos = GR[r3]{8:3} form EAS-2.4
	 */
	bitmask   = 1UL << ((addr >> 3) & 0x3f);
	if (nat)
		*unat |= bitmask;
	 else
		*unat &= ~bitmask;

}

u64 vcpu_get_gr(struct kvm_vcpu *vcpu, unsigned long reg)
{
	struct kvm_pt_regs *regs = vcpu_regs(vcpu);
	u64 val;

	if (!reg)
		return 0;
	getreg(reg, &val, 0, regs);
	return val;
}

void vcpu_set_gr(struct kvm_vcpu *vcpu, u64 reg, u64 value, int nat)
{
	struct kvm_pt_regs *regs = vcpu_regs(vcpu);
	long sof = (regs->cr_ifs) & 0x7f;

	if (!reg)
		return;
	if (reg >= sof + 32)
		return;
	setreg(reg, value, nat, regs);	/* FIXME: handle NATs later*/
}

void getfpreg(unsigned long regnum, struct ia64_fpreg *fpval,
				struct kvm_pt_regs *regs)
{
	/* Take floating register rotation into consideration*/
	if (regnum >= IA64_FIRST_ROTATING_FR)
		regnum = IA64_FIRST_ROTATING_FR + fph_index(regs, regnum);
#define CASE_FIXED_FP(reg)			\
	case  (reg) :				\
		ia64_stf_spill(fpval, reg);	\
	break

	switch (regnum) {
		CASE_FIXED_FP(0);
		CASE_FIXED_FP(1);
		CASE_FIXED_FP(2);
		CASE_FIXED_FP(3);
		CASE_FIXED_FP(4);
		CASE_FIXED_FP(5);

		CASE_FIXED_FP(6);
		CASE_FIXED_FP(7);
		CASE_FIXED_FP(8);
		CASE_FIXED_FP(9);
		CASE_FIXED_FP(10);
		CASE_FIXED_FP(11);

		CASE_FIXED_FP(12);
		CASE_FIXED_FP(13);
		CASE_FIXED_FP(14);
		CASE_FIXED_FP(15);
		CASE_FIXED_FP(16);
		CASE_FIXED_FP(17);
		CASE_FIXED_FP(18);
		CASE_FIXED_FP(19);
		CASE_FIXED_FP(20);
		CASE_FIXED_FP(21);
		CASE_FIXED_FP(22);
		CASE_FIXED_FP(23);
		CASE_FIXED_FP(24);
		CASE_FIXED_FP(25);
		CASE_FIXED_FP(26);
		CASE_FIXED_FP(27);
		CASE_FIXED_FP(28);
		CASE_FIXED_FP(29);
		CASE_FIXED_FP(30);
		CASE_FIXED_FP(31);
		CASE_FIXED_FP(32);
		CASE_FIXED_FP(33);
		CASE_FIXED_FP(34);
		CASE_FIXED_FP(35);
		CASE_FIXED_FP(36);
		CASE_FIXED_FP(37);
		CASE_FIXED_FP(38);
		CASE_FIXED_FP(39);
		CASE_FIXED_FP(40);
		CASE_FIXED_FP(41);
		CASE_FIXED_FP(42);
		CASE_FIXED_FP(43);
		CASE_FIXED_FP(44);
		CASE_FIXED_FP(45);
		CASE_FIXED_FP(46);
		CASE_FIXED_FP(47);
		CASE_FIXED_FP(48);
		CASE_FIXED_FP(49);
		CASE_FIXED_FP(50);
		CASE_FIXED_FP(51);
		CASE_FIXED_FP(52);
		CASE_FIXED_FP(53);
		CASE_FIXED_FP(54);
		CASE_FIXED_FP(55);
		CASE_FIXED_FP(56);
		CASE_FIXED_FP(57);
		CASE_FIXED_FP(58);
		CASE_FIXED_FP(59);
		CASE_FIXED_FP(60);
		CASE_FIXED_FP(61);
		CASE_FIXED_FP(62);
		CASE_FIXED_FP(63);
		CASE_FIXED_FP(64);
		CASE_FIXED_FP(65);
		CASE_FIXED_FP(66);
		CASE_FIXED_FP(67);
		CASE_FIXED_FP(68);
		CASE_FIXED_FP(69);
		CASE_FIXED_FP(70);
		CASE_FIXED_FP(71);
		CASE_FIXED_FP(72);
		CASE_FIXED_FP(73);
		CASE_FIXED_FP(74);
		CASE_FIXED_FP(75);
		CASE_FIXED_FP(76);
		CASE_FIXED_FP(77);
		CASE_FIXED_FP(78);
		CASE_FIXED_FP(79);
		CASE_FIXED_FP(80);
		CASE_FIXED_FP(81);
		CASE_FIXED_FP(82);
		CASE_FIXED_FP(83);
		CASE_FIXED_FP(84);
		CASE_FIXED_FP(85);
		CASE_FIXED_FP(86);
		CASE_FIXED_FP(87);
		CASE_FIXED_FP(88);
		CASE_FIXED_FP(89);
		CASE_FIXED_FP(90);
		CASE_FIXED_FP(91);
		CASE_FIXED_FP(92);
		CASE_FIXED_FP(93);
		CASE_FIXED_FP(94);
		CASE_FIXED_FP(95);
		CASE_FIXED_FP(96);
		CASE_FIXED_FP(97);
		CASE_FIXED_FP(98);
		CASE_FIXED_FP(99);
		CASE_FIXED_FP(100);
		CASE_FIXED_FP(101);
		CASE_FIXED_FP(102);
		CASE_FIXED_FP(103);
		CASE_FIXED_FP(104);
		CASE_FIXED_FP(105);
		CASE_FIXED_FP(106);
		CASE_FIXED_FP(107);
		CASE_FIXED_FP(108);
		CASE_FIXED_FP(109);
		CASE_FIXED_FP(110);
		CASE_FIXED_FP(111);
		CASE_FIXED_FP(112);
		CASE_FIXED_FP(113);
		CASE_FIXED_FP(114);
		CASE_FIXED_FP(115);
		CASE_FIXED_FP(116);
		CASE_FIXED_FP(117);
		CASE_FIXED_FP(118);
		CASE_FIXED_FP(119);
		CASE_FIXED_FP(120);
		CASE_FIXED_FP(121);
		CASE_FIXED_FP(122);
		CASE_FIXED_FP(123);
		CASE_FIXED_FP(124);
		CASE_FIXED_FP(125);
		CASE_FIXED_FP(126);
		CASE_FIXED_FP(127);
	}
#undef CASE_FIXED_FP
}

void setfpreg(unsigned long regnum, struct ia64_fpreg *fpval,
					struct kvm_pt_regs *regs)
{
	/* Take floating register rotation into consideration*/
	if (regnum >= IA64_FIRST_ROTATING_FR)
		regnum = IA64_FIRST_ROTATING_FR + fph_index(regs, regnum);

#define CASE_FIXED_FP(reg)			\
	case (reg) :				\
		ia64_ldf_fill(reg, fpval);	\
	break

	switch (regnum) {
		CASE_FIXED_FP(2);
		CASE_FIXED_FP(3);
		CASE_FIXED_FP(4);
		CASE_FIXED_FP(5);

		CASE_FIXED_FP(6);
		CASE_FIXED_FP(7);
		CASE_FIXED_FP(8);
		CASE_FIXED_FP(9);
		CASE_FIXED_FP(10);
		CASE_FIXED_FP(11);

		CASE_FIXED_FP(12);
		CASE_FIXED_FP(13);
		CASE_FIXED_FP(14);
		CASE_FIXED_FP(15);
		CASE_FIXED_FP(16);
		CASE_FIXED_FP(17);
		CASE_FIXED_FP(18);
		CASE_FIXED_FP(19);
		CASE_FIXED_FP(20);
		CASE_FIXED_FP(21);
		CASE_FIXED_FP(22);
		CASE_FIXED_FP(23);
		CASE_FIXED_FP(24);
		CASE_FIXED_FP(25);
		CASE_FIXED_FP(26);
		CASE_FIXED_FP(27);
		CASE_FIXED_FP(28);
		CASE_FIXED_FP(29);
		CASE_FIXED_FP(30);
		CASE_FIXED_FP(31);
		CASE_FIXED_FP(32);
		CASE_FIXED_FP(33);
		CASE_FIXED_FP(34);
		CASE_FIXED_FP(35);
		CASE_FIXED_FP(36);
		CASE_FIXED_FP(37);
		CASE_FIXED_FP(38);
		CASE_FIXED_FP(39);
		CASE_FIXED_FP(40);
		CASE_FIXED_FP(41);
		CASE_FIXED_FP(42);
		CASE_FIXED_FP(43);
		CASE_FIXED_FP(44);
		CASE_FIXED_FP(45);
		CASE_FIXED_FP(46);
		CASE_FIXED_FP(47);
		CASE_FIXED_FP(48);
		CASE_FIXED_FP(49);
		CASE_FIXED_FP(50);
		CASE_FIXED_FP(51);
		CASE_FIXED_FP(52);
		CASE_FIXED_FP(53);
		CASE_FIXED_FP(54);
		CASE_FIXED_FP(55);
		CASE_FIXED_FP(56);
		CASE_FIXED_FP(57);
		CASE_FIXED_FP(58);
		CASE_FIXED_FP(59);
		CASE_FIXED_FP(60);
		CASE_FIXED_FP(61);
		CASE_FIXED_FP(62);
		CASE_FIXED_FP(63);
		CASE_FIXED_FP(64);
		CASE_FIXED_FP(65);
		CASE_FIXED_FP(66);
		CASE_FIXED_FP(67);
		CASE_FIXED_FP(68);
		CASE_FIXED_FP(69);
		CASE_FIXED_FP(70);
		CASE_FIXED_FP(71);
		CASE_FIXED_FP(72);
		CASE_FIXED_FP(73);
		CASE_FIXED_FP(74);
		CASE_FIXED_FP(75);
		CASE_FIXED_FP(76);
		CASE_FIXED_FP(77);
		CASE_FIXED_FP(78);
		CASE_FIXED_FP(79);
		CASE_FIXED_FP(80);
		CASE_FIXED_FP(81);
		CASE_FIXED_FP(82);
		CASE_FIXED_FP(83);
		CASE_FIXED_FP(84);
		CASE_FIXED_FP(85);
		CASE_FIXED_FP(86);
		CASE_FIXED_FP(87);
		CASE_FIXED_FP(88);
		CASE_FIXED_FP(89);
		CASE_FIXED_FP(90);
		CASE_FIXED_FP(91);
		CASE_FIXED_FP(92);
		CASE_FIXED_FP(93);
		CASE_FIXED_FP(94);
		CASE_FIXED_FP(95);
		CASE_FIXED_FP(96);
		CASE_FIXED_FP(97);
		CASE_FIXED_FP(98);
		CASE_FIXED_FP(99);
		CASE_FIXED_FP(100);
		CASE_FIXED_FP(101);
		CASE_FIXED_FP(102);
		CASE_FIXED_FP(103);
		CASE_FIXED_FP(104);
		CASE_FIXED_FP(105);
		CASE_FIXED_FP(106);
		CASE_FIXED_FP(107);
		CASE_FIXED_FP(108);
		CASE_FIXED_FP(109);
		CASE_FIXED_FP(110);
		CASE_FIXED_FP(111);
		CASE_FIXED_FP(112);
		CASE_FIXED_FP(113);
		CASE_FIXED_FP(114);
		CASE_FIXED_FP(115);
		CASE_FIXED_FP(116);
		CASE_FIXED_FP(117);
		CASE_FIXED_FP(118);
		CASE_FIXED_FP(119);
		CASE_FIXED_FP(120);
		CASE_FIXED_FP(121);
		CASE_FIXED_FP(122);
		CASE_FIXED_FP(123);
		CASE_FIXED_FP(124);
		CASE_FIXED_FP(125);
		CASE_FIXED_FP(126);
		CASE_FIXED_FP(127);
	}
}

void vcpu_get_fpreg(struct kvm_vcpu *vcpu, unsigned long reg,
						struct ia64_fpreg *val)
{
	struct kvm_pt_regs *regs = vcpu_regs(vcpu);

	getfpreg(reg, val, regs);   /* FIXME: handle NATs later*/
}

void vcpu_set_fpreg(struct kvm_vcpu *vcpu, unsigned long reg,
						struct ia64_fpreg *val)
{
	struct kvm_pt_regs *regs = vcpu_regs(vcpu);

	if (reg > 1)
		setfpreg(reg, val, regs);   /* FIXME: handle NATs later*/
}

/************************************************************************
 * lsapic timer
 ***********************************************************************/
u64 vcpu_get_itc(struct kvm_vcpu *vcpu)
{
	unsigned long guest_itc;
	guest_itc = VMX(vcpu, itc_offset) + ia64_getreg(_IA64_REG_AR_ITC);

	if (guest_itc >= VMX(vcpu, last_itc)) {
		VMX(vcpu, last_itc) = guest_itc;
		return  guest_itc;
	} else
		return VMX(vcpu, last_itc);
}

static inline void vcpu_set_itm(struct kvm_vcpu *vcpu, u64 val);
static void vcpu_set_itc(struct kvm_vcpu *vcpu, u64 val)
{
	struct kvm_vcpu *v;
	struct kvm *kvm;
	int i;
	long itc_offset = val - ia64_getreg(_IA64_REG_AR_ITC);
	unsigned long vitv = VCPU(vcpu, itv);

	kvm = (struct kvm *)KVM_VM_BASE;

	if (vcpu->vcpu_id == 0) {
		for (i = 0; i < kvm->arch.online_vcpus; i++) {
			v = (struct kvm_vcpu *)((char *)vcpu +
					sizeof(struct kvm_vcpu_data) * i);
			VMX(v, itc_offset) = itc_offset;
			VMX(v, last_itc) = 0;
		}
	}
	VMX(vcpu, last_itc) = 0;
	if (VCPU(vcpu, itm) <= val) {
		VMX(vcpu, itc_check) = 0;
		vcpu_unpend_interrupt(vcpu, vitv);
	} else {
		VMX(vcpu, itc_check) = 1;
		vcpu_set_itm(vcpu, VCPU(vcpu, itm));
	}

}

static inline u64 vcpu_get_itm(struct kvm_vcpu *vcpu)
{
	return ((u64)VCPU(vcpu, itm));
}

static inline void vcpu_set_itm(struct kvm_vcpu *vcpu, u64 val)
{
	unsigned long vitv = VCPU(vcpu, itv);
	VCPU(vcpu, itm) = val;

	if (val > vcpu_get_itc(vcpu)) {
		VMX(vcpu, itc_check) = 1;
		vcpu_unpend_interrupt(vcpu, vitv);
		VMX(vcpu, timer_pending) = 0;
	} else
		VMX(vcpu, itc_check) = 0;
}

#define  ITV_VECTOR(itv)    (itv&0xff)
#define  ITV_IRQ_MASK(itv)  (itv&(1<<16))

static inline void vcpu_set_itv(struct kvm_vcpu *vcpu, u64 val)
{
	VCPU(vcpu, itv) = val;
	if (!ITV_IRQ_MASK(val) && vcpu->arch.timer_pending) {
		vcpu_pend_interrupt(vcpu, ITV_VECTOR(val));
		vcpu->arch.timer_pending = 0;
	}
}

static inline void vcpu_set_eoi(struct kvm_vcpu *vcpu, u64 val)
{
	int vec;

	vec = highest_inservice_irq(vcpu);
	if (vec == NULL_VECTOR)
		return;
	VMX(vcpu, insvc[vec >> 6]) &= ~(1UL << (vec & 63));
	VCPU(vcpu, eoi) = 0;
	vcpu->arch.irq_new_pending = 1;

}

/* See Table 5-8 in SDM vol2 for the definition */
int irq_masked(struct kvm_vcpu *vcpu, int h_pending, int h_inservice)
{
	union ia64_tpr vtpr;

	vtpr.val = VCPU(vcpu, tpr);

	if (h_inservice == NMI_VECTOR)
		return IRQ_MASKED_BY_INSVC;

	if (h_pending == NMI_VECTOR) {
		/* Non Maskable Interrupt */
		return IRQ_NO_MASKED;
	}

	if (h_inservice == ExtINT_VECTOR)
		return IRQ_MASKED_BY_INSVC;

	if (h_pending == ExtINT_VECTOR) {
		if (vtpr.mmi) {
			/* mask all external IRQ */
			return IRQ_MASKED_BY_VTPR;
		} else
			return IRQ_NO_MASKED;
	}

	if (is_higher_irq(h_pending, h_inservice)) {
		if (is_higher_class(h_pending, vtpr.mic + (vtpr.mmi << 4)))
			return IRQ_NO_MASKED;
		else
			return IRQ_MASKED_BY_VTPR;
	} else {
		return IRQ_MASKED_BY_INSVC;
	}
}

void vcpu_pend_interrupt(struct kvm_vcpu *vcpu, u8 vec)
{
	long spsr;
	int ret;

	local_irq_save(spsr);
	ret = test_and_set_bit(vec, &VCPU(vcpu, irr[0]));
	local_irq_restore(spsr);

	vcpu->arch.irq_new_pending = 1;
}

void vcpu_unpend_interrupt(struct kvm_vcpu *vcpu, u8 vec)
{
	long spsr;
	int ret;

	local_irq_save(spsr);
	ret = test_and_clear_bit(vec, &VCPU(vcpu, irr[0]));
	local_irq_restore(spsr);
	if (ret) {
		vcpu->arch.irq_new_pending = 1;
		wmb();
	}
}

void update_vhpi(struct kvm_vcpu *vcpu, int vec)
{
	u64 vhpi;

	if (vec == NULL_VECTOR)
		vhpi = 0;
	else if (vec == NMI_VECTOR)
		vhpi = 32;
	else if (vec == ExtINT_VECTOR)
		vhpi = 16;
	else
		vhpi = vec >> 4;

	VCPU(vcpu, vhpi) = vhpi;
	if (VCPU(vcpu, vac).a_int)
		ia64_call_vsa(PAL_VPS_SET_PENDING_INTERRUPT,
				(u64)vcpu->arch.vpd, 0, 0, 0, 0, 0, 0);
}

u64 vcpu_get_ivr(struct kvm_vcpu *vcpu)
{
	int vec, h_inservice, mask;

	vec = highest_pending_irq(vcpu);
	h_inservice = highest_inservice_irq(vcpu);
	mask = irq_masked(vcpu, vec, h_inservice);
	if (vec == NULL_VECTOR || mask == IRQ_MASKED_BY_INSVC) {
		if (VCPU(vcpu, vhpi))
			update_vhpi(vcpu, NULL_VECTOR);
		return IA64_SPURIOUS_INT_VECTOR;
	}
	if (mask == IRQ_MASKED_BY_VTPR) {
		update_vhpi(vcpu, vec);
		return IA64_SPURIOUS_INT_VECTOR;
	}
	VMX(vcpu, insvc[vec >> 6]) |= (1UL << (vec & 63));
	vcpu_unpend_interrupt(vcpu, vec);
	return  (u64)vec;
}

/**************************************************************************
  Privileged operation emulation routines
 **************************************************************************/
u64 vcpu_thash(struct kvm_vcpu *vcpu, u64 vadr)
{
	union ia64_pta vpta;
	union ia64_rr vrr;
	u64 pval;
	u64 vhpt_offset;

	vpta.val = vcpu_get_pta(vcpu);
	vrr.val = vcpu_get_rr(vcpu, vadr);
	vhpt_offset = ((vadr >> vrr.ps) << 3) & ((1UL << (vpta.size)) - 1);
	if (vpta.vf) {
		pval = ia64_call_vsa(PAL_VPS_THASH, vadr, vrr.val,
				vpta.val, 0, 0, 0, 0);
	} else {
		pval = (vadr & VRN_MASK) | vhpt_offset |
			(vpta.val << 3 >> (vpta.size + 3) << (vpta.size));
	}
	return  pval;
}

u64 vcpu_ttag(struct kvm_vcpu *vcpu, u64 vadr)
{
	union ia64_rr vrr;
	union ia64_pta vpta;
	u64 pval;

	vpta.val = vcpu_get_pta(vcpu);
	vrr.val = vcpu_get_rr(vcpu, vadr);
	if (vpta.vf) {
		pval = ia64_call_vsa(PAL_VPS_TTAG, vadr, vrr.val,
						0, 0, 0, 0, 0);
	} else
		pval = 1;

	return  pval;
}

u64 vcpu_tak(struct kvm_vcpu *vcpu, u64 vadr)
{
	struct thash_data *data;
	union ia64_pta vpta;
	u64 key;

	vpta.val = vcpu_get_pta(vcpu);
	if (vpta.vf == 0) {
		key = 1;
		return key;
	}
	data = vtlb_lookup(vcpu, vadr, D_TLB);
	if (!data || !data->p)
		key = 1;
	else
		key = data->key;

	return key;
}

void kvm_thash(struct kvm_vcpu *vcpu, INST64 inst)
{
	unsigned long thash, vadr;

	vadr = vcpu_get_gr(vcpu, inst.M46.r3);
	thash = vcpu_thash(vcpu, vadr);
	vcpu_set_gr(vcpu, inst.M46.r1, thash, 0);
}

void kvm_ttag(struct kvm_vcpu *vcpu, INST64 inst)
{
	unsigned long tag, vadr;

	vadr = vcpu_get_gr(vcpu, inst.M46.r3);
	tag = vcpu_ttag(vcpu, vadr);
	vcpu_set_gr(vcpu, inst.M46.r1, tag, 0);
}

int vcpu_tpa(struct kvm_vcpu *vcpu, u64 vadr, u64 *padr)
{
	struct thash_data *data;
	union ia64_isr visr, pt_isr;
	struct kvm_pt_regs *regs;
	struct ia64_psr vpsr;

	regs = vcpu_regs(vcpu);
	pt_isr.val = VMX(vcpu, cr_isr);
	visr.val = 0;
	visr.ei = pt_isr.ei;
	visr.ir = pt_isr.ir;
	vpsr = *(struct ia64_psr *)&VCPU(vcpu, vpsr);
	visr.na = 1;

	data = vhpt_lookup(vadr);
	if (data) {
		if (data->p == 0) {
			vcpu_set_isr(vcpu, visr.val);
			data_page_not_present(vcpu, vadr);
			return IA64_FAULT;
		} else if (data->ma == VA_MATTR_NATPAGE) {
			vcpu_set_isr(vcpu, visr.val);
			dnat_page_consumption(vcpu, vadr);
			return IA64_FAULT;
		} else {
			*padr = (data->gpaddr >> data->ps << data->ps) |
				(vadr & (PSIZE(data->ps) - 1));
			return IA64_NO_FAULT;
		}
	}

	data = vtlb_lookup(vcpu, vadr, D_TLB);
	if (data) {
		if (data->p == 0) {
			vcpu_set_isr(vcpu, visr.val);
			data_page_not_present(vcpu, vadr);
			return IA64_FAULT;
		} else if (data->ma == VA_MATTR_NATPAGE) {
			vcpu_set_isr(vcpu, visr.val);
			dnat_page_consumption(vcpu, vadr);
			return IA64_FAULT;
		} else{
			*padr = ((data->ppn >> (data->ps - 12)) << data->ps)
				| (vadr & (PSIZE(data->ps) - 1));
			return IA64_NO_FAULT;
		}
	}
	if (!vhpt_enabled(vcpu, vadr, NA_REF)) {
		if (vpsr.ic) {
			vcpu_set_isr(vcpu, visr.val);
			alt_dtlb(vcpu, vadr);
			return IA64_FAULT;
		} else {
			nested_dtlb(vcpu);
			return IA64_FAULT;
		}
	} else {
		if (vpsr.ic) {
			vcpu_set_isr(vcpu, visr.val);
			dvhpt_fault(vcpu, vadr);
			return IA64_FAULT;
		} else{
			nested_dtlb(vcpu);
			return IA64_FAULT;
		}
	}

	return IA64_NO_FAULT;
}

int kvm_tpa(struct kvm_vcpu *vcpu, INST64 inst)
{
	unsigned long r1, r3;

	r3 = vcpu_get_gr(vcpu, inst.M46.r3);

	if (vcpu_tpa(vcpu, r3, &r1))
		return IA64_FAULT;

	vcpu_set_gr(vcpu, inst.M46.r1, r1, 0);
	return(IA64_NO_FAULT);
}

void kvm_tak(struct kvm_vcpu *vcpu, INST64 inst)
{
	unsigned long r1, r3;

	r3 = vcpu_get_gr(vcpu, inst.M46.r3);
	r1 = vcpu_tak(vcpu, r3);
	vcpu_set_gr(vcpu, inst.M46.r1, r1, 0);
}

/************************************
 * Insert/Purge translation register/cache
 ************************************/
void vcpu_itc_i(struct kvm_vcpu *vcpu, u64 pte, u64 itir, u64 ifa)
{
	thash_purge_and_insert(vcpu, pte, itir, ifa, I_TLB);
}

void vcpu_itc_d(struct kvm_vcpu *vcpu, u64 pte, u64 itir, u64 ifa)
{
	thash_purge_and_insert(vcpu, pte, itir, ifa, D_TLB);
}

void vcpu_itr_i(struct kvm_vcpu *vcpu, u64 slot, u64 pte, u64 itir, u64 ifa)
{
	u64 ps, va, rid;
	struct thash_data *p_itr;

	ps = itir_ps(itir);
	va = PAGEALIGN(ifa, ps);
	pte &= ~PAGE_FLAGS_RV_MASK;
	rid = vcpu_get_rr(vcpu, ifa);
	rid = rid & RR_RID_MASK;
	p_itr = (struct thash_data *)&vcpu->arch.itrs[slot];
	vcpu_set_tr(p_itr, pte, itir, va, rid);
	vcpu_quick_region_set(VMX(vcpu, itr_regions), va);
}


void vcpu_itr_d(struct kvm_vcpu *vcpu, u64 slot, u64 pte, u64 itir, u64 ifa)
{
	u64 gpfn;
	u64 ps, va, rid;
	struct thash_data *p_dtr;

	ps = itir_ps(itir);
	va = PAGEALIGN(ifa, ps);
	pte &= ~PAGE_FLAGS_RV_MASK;

	if (ps != _PAGE_SIZE_16M)
		thash_purge_entries(vcpu, va, ps);
	gpfn = (pte & _PAGE_PPN_MASK) >> PAGE_SHIFT;
	if (__gpfn_is_io(gpfn))
		pte |= VTLB_PTE_IO;
	rid = vcpu_get_rr(vcpu, va);
	rid = rid & RR_RID_MASK;
	p_dtr = (struct thash_data *)&vcpu->arch.dtrs[slot];
	vcpu_set_tr((struct thash_data *)&vcpu->arch.dtrs[slot],
							pte, itir, va, rid);
	vcpu_quick_region_set(VMX(vcpu, dtr_regions), va);
}

void vcpu_ptr_d(struct kvm_vcpu *vcpu, u64 ifa, u64 ps)
{
	int index;
	u64 va;

	va = PAGEALIGN(ifa, ps);
	while ((index = vtr_find_overlap(vcpu, va, ps, D_TLB)) >= 0)
		vcpu->arch.dtrs[index].page_flags = 0;

	thash_purge_entries(vcpu, va, ps);
}

void vcpu_ptr_i(struct kvm_vcpu *vcpu, u64 ifa, u64 ps)
{
	int index;
	u64 va;

	va = PAGEALIGN(ifa, ps);
	while ((index = vtr_find_overlap(vcpu, va, ps, I_TLB)) >= 0)
		vcpu->arch.itrs[index].page_flags = 0;

	thash_purge_entries(vcpu, va, ps);
}

void vcpu_ptc_l(struct kvm_vcpu *vcpu, u64 va, u64 ps)
{
	va = PAGEALIGN(va, ps);
	thash_purge_entries(vcpu, va, ps);
}

void vcpu_ptc_e(struct kvm_vcpu *vcpu, u64 va)
{
	thash_purge_all(vcpu);
}

void vcpu_ptc_ga(struct kvm_vcpu *vcpu, u64 va, u64 ps)
{
	struct exit_ctl_data *p = &vcpu->arch.exit_data;
	long psr;
	local_irq_save(psr);
	p->exit_reason = EXIT_REASON_PTC_G;

	p->u.ptc_g_data.rr = vcpu_get_rr(vcpu, va);
	p->u.ptc_g_data.vaddr = va;
	p->u.ptc_g_data.ps = ps;
	vmm_transition(vcpu);
	/* Do Local Purge Here*/
	vcpu_ptc_l(vcpu, va, ps);
	local_irq_restore(psr);
}


void vcpu_ptc_g(struct kvm_vcpu *vcpu, u64 va, u64 ps)
{
	vcpu_ptc_ga(vcpu, va, ps);
}

void kvm_ptc_e(struct kvm_vcpu *vcpu, INST64 inst)
{
	unsigned long ifa;

	ifa = vcpu_get_gr(vcpu, inst.M45.r3);
	vcpu_ptc_e(vcpu, ifa);
}

void kvm_ptc_g(struct kvm_vcpu *vcpu, INST64 inst)
{
	unsigned long ifa, itir;

	ifa = vcpu_get_gr(vcpu, inst.M45.r3);
	itir = vcpu_get_gr(vcpu, inst.M45.r2);
	vcpu_ptc_g(vcpu, ifa, itir_ps(itir));
}

void kvm_ptc_ga(struct kvm_vcpu *vcpu, INST64 inst)
{
	unsigned long ifa, itir;

	ifa = vcpu_get_gr(vcpu, inst.M45.r3);
	itir = vcpu_get_gr(vcpu, inst.M45.r2);
	vcpu_ptc_ga(vcpu, ifa, itir_ps(itir));
}

void kvm_ptc_l(struct kvm_vcpu *vcpu, INST64 inst)
{
	unsigned long ifa, itir;

	ifa = vcpu_get_gr(vcpu, inst.M45.r3);
	itir = vcpu_get_gr(vcpu, inst.M45.r2);
	vcpu_ptc_l(vcpu, ifa, itir_ps(itir));
}

void kvm_ptr_d(struct kvm_vcpu *vcpu, INST64 inst)
{
	unsigned long ifa, itir;

	ifa = vcpu_get_gr(vcpu, inst.M45.r3);
	itir = vcpu_get_gr(vcpu, inst.M45.r2);
	vcpu_ptr_d(vcpu, ifa, itir_ps(itir));
}

void kvm_ptr_i(struct kvm_vcpu *vcpu, INST64 inst)
{
	unsigned long ifa, itir;

	ifa = vcpu_get_gr(vcpu, inst.M45.r3);
	itir = vcpu_get_gr(vcpu, inst.M45.r2);
	vcpu_ptr_i(vcpu, ifa, itir_ps(itir));
}

void kvm_itr_d(struct kvm_vcpu *vcpu, INST64 inst)
{
	unsigned long itir, ifa, pte, slot;

	slot = vcpu_get_gr(vcpu, inst.M45.r3);
	pte = vcpu_get_gr(vcpu, inst.M45.r2);
	itir = vcpu_get_itir(vcpu);
	ifa = vcpu_get_ifa(vcpu);
	vcpu_itr_d(vcpu, slot, pte, itir, ifa);
}



void kvm_itr_i(struct kvm_vcpu *vcpu, INST64 inst)
{
	unsigned long itir, ifa, pte, slot;

	slot = vcpu_get_gr(vcpu, inst.M45.r3);
	pte = vcpu_get_gr(vcpu, inst.M45.r2);
	itir = vcpu_get_itir(vcpu);
	ifa = vcpu_get_ifa(vcpu);
	vcpu_itr_i(vcpu, slot, pte, itir, ifa);
}

void kvm_itc_d(struct kvm_vcpu *vcpu, INST64 inst)
{
	unsigned long itir, ifa, pte;

	itir = vcpu_get_itir(vcpu);
	ifa = vcpu_get_ifa(vcpu);
	pte = vcpu_get_gr(vcpu, inst.M45.r2);
	vcpu_itc_d(vcpu, pte, itir, ifa);
}

void kvm_itc_i(struct kvm_vcpu *vcpu, INST64 inst)
{
	unsigned long itir, ifa, pte;

	itir = vcpu_get_itir(vcpu);
	ifa = vcpu_get_ifa(vcpu);
	pte = vcpu_get_gr(vcpu, inst.M45.r2);
	vcpu_itc_i(vcpu, pte, itir, ifa);
}

/*************************************
 * Moves to semi-privileged registers
 *************************************/

void kvm_mov_to_ar_imm(struct kvm_vcpu *vcpu, INST64 inst)
{
	unsigned long imm;

	if (inst.M30.s)
		imm = -inst.M30.imm;
	else
		imm = inst.M30.imm;

	vcpu_set_itc(vcpu, imm);
}

void kvm_mov_to_ar_reg(struct kvm_vcpu *vcpu, INST64 inst)
{
	unsigned long r2;

	r2 = vcpu_get_gr(vcpu, inst.M29.r2);
	vcpu_set_itc(vcpu, r2);
}

void kvm_mov_from_ar_reg(struct kvm_vcpu *vcpu, INST64 inst)
{
	unsigned long r1;

	r1 = vcpu_get_itc(vcpu);
	vcpu_set_gr(vcpu, inst.M31.r1, r1, 0);
}

/**************************************************************************
  struct kvm_vcpu protection key register access routines
 **************************************************************************/

unsigned long vcpu_get_pkr(struct kvm_vcpu *vcpu, unsigned long reg)
{
	return ((unsigned long)ia64_get_pkr(reg));
}

void vcpu_set_pkr(struct kvm_vcpu *vcpu, unsigned long reg, unsigned long val)
{
	ia64_set_pkr(reg, val);
}

/********************************
 * Moves to privileged registers
 ********************************/
unsigned long vcpu_set_rr(struct kvm_vcpu *vcpu, unsigned long reg,
					unsigned long val)
{
	union ia64_rr oldrr, newrr;
	unsigned long rrval;
	struct exit_ctl_data *p = &vcpu->arch.exit_data;
	unsigned long psr;

	oldrr.val = vcpu_get_rr(vcpu, reg);
	newrr.val = val;
	vcpu->arch.vrr[reg >> VRN_SHIFT] = val;

	switch ((unsigned long)(reg >> VRN_SHIFT)) {
	case VRN6:
		vcpu->arch.vmm_rr = vrrtomrr(val);
		local_irq_save(psr);
		p->exit_reason = EXIT_REASON_SWITCH_RR6;
		vmm_transition(vcpu);
		local_irq_restore(psr);
		break;
	case VRN4:
		rrval = vrrtomrr(val);
		vcpu->arch.metaphysical_saved_rr4 = rrval;
		if (!is_physical_mode(vcpu))
			ia64_set_rr(reg, rrval);
		break;
	case VRN0:
		rrval = vrrtomrr(val);
		vcpu->arch.metaphysical_saved_rr0 = rrval;
		if (!is_physical_mode(vcpu))
			ia64_set_rr(reg, rrval);
		break;
	default:
		ia64_set_rr(reg, vrrtomrr(val));
		break;
	}

	return (IA64_NO_FAULT);
}

void kvm_mov_to_rr(struct kvm_vcpu *vcpu, INST64 inst)
{
	unsigned long r3, r2;

	r3 = vcpu_get_gr(vcpu, inst.M42.r3);
	r2 = vcpu_get_gr(vcpu, inst.M42.r2);
	vcpu_set_rr(vcpu, r3, r2);
}

void kvm_mov_to_dbr(struct kvm_vcpu *vcpu, INST64 inst)
{
}

void kvm_mov_to_ibr(struct kvm_vcpu *vcpu, INST64 inst)
{
}

void kvm_mov_to_pmc(struct kvm_vcpu *vcpu, INST64 inst)
{
	unsigned long r3, r2;

	r3 = vcpu_get_gr(vcpu, inst.M42.r3);
	r2 = vcpu_get_gr(vcpu, inst.M42.r2);
	vcpu_set_pmc(vcpu, r3, r2);
}

void kvm_mov_to_pmd(struct kvm_vcpu *vcpu, INST64 inst)
{
	unsigned long r3, r2;

	r3 = vcpu_get_gr(vcpu, inst.M42.r3);
	r2 = vcpu_get_gr(vcpu, inst.M42.r2);
	vcpu_set_pmd(vcpu, r3, r2);
}

void kvm_mov_to_pkr(struct kvm_vcpu *vcpu, INST64 inst)
{
	u64 r3, r2;

	r3 = vcpu_get_gr(vcpu, inst.M42.r3);
	r2 = vcpu_get_gr(vcpu, inst.M42.r2);
	vcpu_set_pkr(vcpu, r3, r2);
}

void kvm_mov_from_rr(struct kvm_vcpu *vcpu, INST64 inst)
{
	unsigned long r3, r1;

	r3 = vcpu_get_gr(vcpu, inst.M43.r3);
	r1 = vcpu_get_rr(vcpu, r3);
	vcpu_set_gr(vcpu, inst.M43.r1, r1, 0);
}

void kvm_mov_from_pkr(struct kvm_vcpu *vcpu, INST64 inst)
{
	unsigned long r3, r1;

	r3 = vcpu_get_gr(vcpu, inst.M43.r3);
	r1 = vcpu_get_pkr(vcpu, r3);
	vcpu_set_gr(vcpu, inst.M43.r1, r1, 0);
}

void kvm_mov_from_dbr(struct kvm_vcpu *vcpu, INST64 inst)
{
	unsigned long r3, r1;

	r3 = vcpu_get_gr(vcpu, inst.M43.r3);
	r1 = vcpu_get_dbr(vcpu, r3);
	vcpu_set_gr(vcpu, inst.M43.r1, r1, 0);
}

void kvm_mov_from_ibr(struct kvm_vcpu *vcpu, INST64 inst)
{
	unsigned long r3, r1;

	r3 = vcpu_get_gr(vcpu, inst.M43.r3);
	r1 = vcpu_get_ibr(vcpu, r3);
	vcpu_set_gr(vcpu, inst.M43.r1, r1, 0);
}

void kvm_mov_from_pmc(struct kvm_vcpu *vcpu, INST64 inst)
{
	unsigned long r3, r1;

	r3 = vcpu_get_gr(vcpu, inst.M43.r3);
	r1 = vcpu_get_pmc(vcpu, r3);
	vcpu_set_gr(vcpu, inst.M43.r1, r1, 0);
}

unsigned long vcpu_get_cpuid(struct kvm_vcpu *vcpu, unsigned long reg)
{
	/* FIXME: This could get called as a result of a rsvd-reg fault */
	if (reg > (ia64_get_cpuid(3) & 0xff))
		return 0;
	else
		return ia64_get_cpuid(reg);
}

void kvm_mov_from_cpuid(struct kvm_vcpu *vcpu, INST64 inst)
{
	unsigned long r3, r1;

	r3 = vcpu_get_gr(vcpu, inst.M43.r3);
	r1 = vcpu_get_cpuid(vcpu, r3);
	vcpu_set_gr(vcpu, inst.M43.r1, r1, 0);
}

void vcpu_set_tpr(struct kvm_vcpu *vcpu, unsigned long val)
{
	VCPU(vcpu, tpr) = val;
	vcpu->arch.irq_check = 1;
}

unsigned long kvm_mov_to_cr(struct kvm_vcpu *vcpu, INST64 inst)
{
	unsigned long r2;

	r2 = vcpu_get_gr(vcpu, inst.M32.r2);
	VCPU(vcpu, vcr[inst.M32.cr3]) = r2;

	switch (inst.M32.cr3) {
	case 0:
		vcpu_set_dcr(vcpu, r2);
		break;
	case 1:
		vcpu_set_itm(vcpu, r2);
		break;
	case 66:
		vcpu_set_tpr(vcpu, r2);
		break;
	case 67:
		vcpu_set_eoi(vcpu, r2);
		break;
	default:
		break;
	}

	return 0;
}

unsigned long kvm_mov_from_cr(struct kvm_vcpu *vcpu, INST64 inst)
{
	unsigned long tgt = inst.M33.r1;
	unsigned long val;

	switch (inst.M33.cr3) {
	case 65:
		val = vcpu_get_ivr(vcpu);
		vcpu_set_gr(vcpu, tgt, val, 0);
		break;

	case 67:
		vcpu_set_gr(vcpu, tgt, 0L, 0);
		break;
	default:
		val = VCPU(vcpu, vcr[inst.M33.cr3]);
		vcpu_set_gr(vcpu, tgt, val, 0);
		break;
	}

	return 0;
}

void vcpu_set_psr(struct kvm_vcpu *vcpu, unsigned long val)
{

	unsigned long mask;
	struct kvm_pt_regs *regs;
	struct ia64_psr old_psr, new_psr;

	old_psr = *(struct ia64_psr *)&VCPU(vcpu, vpsr);

	regs = vcpu_regs(vcpu);
	/* We only support guest as:
	 *  vpsr.pk = 0
	 *  vpsr.is = 0
	 * Otherwise panic
	 */
	if (val & (IA64_PSR_PK | IA64_PSR_IS | IA64_PSR_VM))
		panic_vm(vcpu, "Only support guests with vpsr.pk =0 \
				& vpsr.is=0\n");

	/*
	 * For those IA64_PSR bits: id/da/dd/ss/ed/ia
	 * Since these bits will become 0, after success execution of each
	 * instruction, we will change set them to mIA64_PSR
	 */
	VCPU(vcpu, vpsr) = val
		& (~(IA64_PSR_ID | IA64_PSR_DA | IA64_PSR_DD |
			IA64_PSR_SS | IA64_PSR_ED | IA64_PSR_IA));

	if (!old_psr.i && (val & IA64_PSR_I)) {
		/* vpsr.i 0->1 */
		vcpu->arch.irq_check = 1;
	}
	new_psr = *(struct ia64_psr *)&VCPU(vcpu, vpsr);

	/*
	 * All vIA64_PSR bits shall go to mPSR (v->tf->tf_special.psr)
	 * , except for the following bits:
	 *  ic/i/dt/si/rt/mc/it/bn/vm
	 */
	mask =  IA64_PSR_IC + IA64_PSR_I + IA64_PSR_DT + IA64_PSR_SI +
		IA64_PSR_RT + IA64_PSR_MC + IA64_PSR_IT + IA64_PSR_BN +
		IA64_PSR_VM;

	regs->cr_ipsr = (regs->cr_ipsr & mask) | (val & (~mask));

	check_mm_mode_switch(vcpu, old_psr, new_psr);

	return ;
}

unsigned long vcpu_cover(struct kvm_vcpu *vcpu)
{
	struct ia64_psr vpsr;

	struct kvm_pt_regs *regs = vcpu_regs(vcpu);
	vpsr = *(struct ia64_psr *)&VCPU(vcpu, vpsr);

	if (!vpsr.ic)
		VCPU(vcpu, ifs) = regs->cr_ifs;
	regs->cr_ifs = IA64_IFS_V;
	return (IA64_NO_FAULT);
}



/**************************************************************************
  VCPU banked general register access routines
 **************************************************************************/
#define vcpu_bsw0_unat(i, b0unat, b1unat, runat, VMM_PT_REGS_R16_SLOT)	\
	do {     							\
		__asm__ __volatile__ (					\
				";;extr.u %0 = %3,%6,16;;\n"		\
				"dep %1 = %0, %1, 0, 16;;\n"		\
				"st8 [%4] = %1\n"			\
				"extr.u %0 = %2, 16, 16;;\n"		\
				"dep %3 = %0, %3, %6, 16;;\n"		\
				"st8 [%5] = %3\n"			\
				::"r"(i), "r"(*b1unat), "r"(*b0unat),	\
				"r"(*runat), "r"(b1unat), "r"(runat),	\
				"i"(VMM_PT_REGS_R16_SLOT) : "memory");	\
	} while (0)

void vcpu_bsw0(struct kvm_vcpu *vcpu)
{
	unsigned long i;

	struct kvm_pt_regs *regs = vcpu_regs(vcpu);
	unsigned long *r = &regs->r16;
	unsigned long *b0 = &VCPU(vcpu, vbgr[0]);
	unsigned long *b1 = &VCPU(vcpu, vgr[0]);
	unsigned long *runat = &regs->eml_unat;
	unsigned long *b0unat = &VCPU(vcpu, vbnat);
	unsigned long *b1unat = &VCPU(vcpu, vnat);


	if (VCPU(vcpu, vpsr) & IA64_PSR_BN) {
		for (i = 0; i < 16; i++) {
			*b1++ = *r;
			*r++ = *b0++;
		}
		vcpu_bsw0_unat(i, b0unat, b1unat, runat,
				VMM_PT_REGS_R16_SLOT);
		VCPU(vcpu, vpsr) &= ~IA64_PSR_BN;
	}
}

#define vcpu_bsw1_unat(i, b0unat, b1unat, runat, VMM_PT_REGS_R16_SLOT)	\
	do {             						\
		__asm__ __volatile__ (";;extr.u %0 = %3, %6, 16;;\n"	\
				"dep %1 = %0, %1, 16, 16;;\n"		\
				"st8 [%4] = %1\n"			\
				"extr.u %0 = %2, 0, 16;;\n"		\
				"dep %3 = %0, %3, %6, 16;;\n"		\
				"st8 [%5] = %3\n"			\
				::"r"(i), "r"(*b0unat), "r"(*b1unat),	\
				"r"(*runat), "r"(b0unat), "r"(runat),	\
				"i"(VMM_PT_REGS_R16_SLOT) : "memory");	\
	} while (0)

void vcpu_bsw1(struct kvm_vcpu *vcpu)
{
	unsigned long i;
	struct kvm_pt_regs *regs = vcpu_regs(vcpu);
	unsigned long *r = &regs->r16;
	unsigned long *b0 = &VCPU(vcpu, vbgr[0]);
	unsigned long *b1 = &VCPU(vcpu, vgr[0]);
	unsigned long *runat = &regs->eml_unat;
	unsigned long *b0unat = &VCPU(vcpu, vbnat);
	unsigned long *b1unat = &VCPU(vcpu, vnat);

	if (!(VCPU(vcpu, vpsr) & IA64_PSR_BN)) {
		for (i = 0; i < 16; i++) {
			*b0++ = *r;
			*r++ = *b1++;
		}
		vcpu_bsw1_unat(i, b0unat, b1unat, runat,
				VMM_PT_REGS_R16_SLOT);
		VCPU(vcpu, vpsr) |= IA64_PSR_BN;
	}
}

void vcpu_rfi(struct kvm_vcpu *vcpu)
{
	unsigned long ifs, psr;
	struct kvm_pt_regs *regs = vcpu_regs(vcpu);

	psr = VCPU(vcpu, ipsr);
	if (psr & IA64_PSR_BN)
		vcpu_bsw1(vcpu);
	else
		vcpu_bsw0(vcpu);
	vcpu_set_psr(vcpu, psr);
	ifs = VCPU(vcpu, ifs);
	if (ifs >> 63)
		regs->cr_ifs = ifs;
	regs->cr_iip = VCPU(vcpu, iip);
}

/*
   VPSR can't keep track of below bits of guest PSR
   This function gets guest PSR
 */

unsigned long vcpu_get_psr(struct kvm_vcpu *vcpu)
{
	unsigned long mask;
	struct kvm_pt_regs *regs = vcpu_regs(vcpu);

	mask = IA64_PSR_BE | IA64_PSR_UP | IA64_PSR_AC | IA64_PSR_MFL |
		IA64_PSR_MFH | IA64_PSR_CPL | IA64_PSR_RI;
	return (VCPU(vcpu, vpsr) & ~mask) | (regs->cr_ipsr & mask);
}

void kvm_rsm(struct kvm_vcpu *vcpu, INST64 inst)
{
	unsigned long vpsr;
	unsigned long imm24 = (inst.M44.i<<23) | (inst.M44.i2<<21)
					| inst.M44.imm;

	vpsr = vcpu_get_psr(vcpu);
	vpsr &= (~imm24);
	vcpu_set_psr(vcpu, vpsr);
}

void kvm_ssm(struct kvm_vcpu *vcpu, INST64 inst)
{
	unsigned long vpsr;
	unsigned long imm24 = (inst.M44.i << 23) | (inst.M44.i2 << 21)
				| inst.M44.imm;

	vpsr = vcpu_get_psr(vcpu);
	vpsr |= imm24;
	vcpu_set_psr(vcpu, vpsr);
}

/* Generate Mask
 * Parameter:
 *  bit -- starting bit
 *  len -- how many bits
 */
#define MASK(bit,len)				   	\
({							\
		__u64	ret;				\
							\
		__asm __volatile("dep %0=-1, r0, %1, %2"\
				: "=r" (ret):		\
		  "M" (bit),				\
		  "M" (len));				\
		ret;					\
})

void vcpu_set_psr_l(struct kvm_vcpu *vcpu, unsigned long val)
{
	val = (val & MASK(0, 32)) | (vcpu_get_psr(vcpu) & MASK(32, 32));
	vcpu_set_psr(vcpu, val);
}

void kvm_mov_to_psr(struct kvm_vcpu *vcpu, INST64 inst)
{
	unsigned long val;

	val = vcpu_get_gr(vcpu, inst.M35.r2);
	vcpu_set_psr_l(vcpu, val);
}

void kvm_mov_from_psr(struct kvm_vcpu *vcpu, INST64 inst)
{
	unsigned long val;

	val = vcpu_get_psr(vcpu);
	val = (val & MASK(0, 32)) | (val & MASK(35, 2));
	vcpu_set_gr(vcpu, inst.M33.r1, val, 0);
}

void vcpu_increment_iip(struct kvm_vcpu *vcpu)
{
	struct kvm_pt_regs *regs = vcpu_regs(vcpu);
	struct ia64_psr *ipsr = (struct ia64_psr *)&regs->cr_ipsr;
	if (ipsr->ri == 2) {
		ipsr->ri = 0;
		regs->cr_iip += 16;
	} else
		ipsr->ri++;
}

void vcpu_decrement_iip(struct kvm_vcpu *vcpu)
{
	struct kvm_pt_regs *regs = vcpu_regs(vcpu);
	struct ia64_psr *ipsr = (struct ia64_psr *)&regs->cr_ipsr;

	if (ipsr->ri == 0) {
		ipsr->ri = 2;
		regs->cr_iip -= 16;
	} else
		ipsr->ri--;
}

/** Emulate a privileged operation.
 *
 *
 * @param vcpu virtual cpu
 * @cause the reason cause virtualization fault
 * @opcode the instruction code which cause virtualization fault
 */

void kvm_emulate(struct kvm_vcpu *vcpu, struct kvm_pt_regs *regs)
{
	unsigned long status, cause, opcode ;
	INST64 inst;

	status = IA64_NO_FAULT;
	cause = VMX(vcpu, cause);
	opcode = VMX(vcpu, opcode);
	inst.inst = opcode;
	/*
	 * Switch to actual virtual rid in rr0 and rr4,
	 * which is required by some tlb related instructions.
	 */
	prepare_if_physical_mode(vcpu);

	switch (cause) {
	case EVENT_RSM:
		kvm_rsm(vcpu, inst);
		break;
	case EVENT_SSM:
		kvm_ssm(vcpu, inst);
		break;
	case EVENT_MOV_TO_PSR:
		kvm_mov_to_psr(vcpu, inst);
		break;
	case EVENT_MOV_FROM_PSR:
		kvm_mov_from_psr(vcpu, inst);
		break;
	case EVENT_MOV_FROM_CR:
		kvm_mov_from_cr(vcpu, inst);
		break;
	case EVENT_MOV_TO_CR:
		kvm_mov_to_cr(vcpu, inst);
		break;
	case EVENT_BSW_0:
		vcpu_bsw0(vcpu);
		break;
	case EVENT_BSW_1:
		vcpu_bsw1(vcpu);
		break;
	case EVENT_COVER:
		vcpu_cover(vcpu);
		break;
	case EVENT_RFI:
		vcpu_rfi(vcpu);
		break;
	case EVENT_ITR_D:
		kvm_itr_d(vcpu, inst);
		break;
	case EVENT_ITR_I:
		kvm_itr_i(vcpu, inst);
		break;
	case EVENT_PTR_D:
		kvm_ptr_d(vcpu, inst);
		break;
	case EVENT_PTR_I:
		kvm_ptr_i(vcpu, inst);
		break;
	case EVENT_ITC_D:
		kvm_itc_d(vcpu, inst);
		break;
	case EVENT_ITC_I:
		kvm_itc_i(vcpu, inst);
		break;
	case EVENT_PTC_L:
		kvm_ptc_l(vcpu, inst);
		break;
	case EVENT_PTC_G:
		kvm_ptc_g(vcpu, inst);
		break;
	case EVENT_PTC_GA:
		kvm_ptc_ga(vcpu, inst);
		break;
	case EVENT_PTC_E:
		kvm_ptc_e(vcpu, inst);
		break;
	case EVENT_MOV_TO_RR:
		kvm_mov_to_rr(vcpu, inst);
		break;
	case EVENT_MOV_FROM_RR:
		kvm_mov_from_rr(vcpu, inst);
		break;
	case EVENT_THASH:
		kvm_thash(vcpu, inst);
		break;
	case EVENT_TTAG:
		kvm_ttag(vcpu, inst);
		break;
	case EVENT_TPA:
		status = kvm_tpa(vcpu, inst);
		break;
	case EVENT_TAK:
		kvm_tak(vcpu, inst);
		break;
	case EVENT_MOV_TO_AR_IMM:
		kvm_mov_to_ar_imm(vcpu, inst);
		break;
	case EVENT_MOV_TO_AR:
		kvm_mov_to_ar_reg(vcpu, inst);
		break;
	case EVENT_MOV_FROM_AR:
		kvm_mov_from_ar_reg(vcpu, inst);
		break;
	case EVENT_MOV_TO_DBR:
		kvm_mov_to_dbr(vcpu, inst);
		break;
	case EVENT_MOV_TO_IBR:
		kvm_mov_to_ibr(vcpu, inst);
		break;
	case EVENT_MOV_TO_PMC:
		kvm_mov_to_pmc(vcpu, inst);
		break;
	case EVENT_MOV_TO_PMD:
		kvm_mov_to_pmd(vcpu, inst);
		break;
	case EVENT_MOV_TO_PKR:
		kvm_mov_to_pkr(vcpu, inst);
		break;
	case EVENT_MOV_FROM_DBR:
		kvm_mov_from_dbr(vcpu, inst);
		break;
	case EVENT_MOV_FROM_IBR:
		kvm_mov_from_ibr(vcpu, inst);
		break;
	case EVENT_MOV_FROM_PMC:
		kvm_mov_from_pmc(vcpu, inst);
		break;
	case EVENT_MOV_FROM_PKR:
		kvm_mov_from_pkr(vcpu, inst);
		break;
	case EVENT_MOV_FROM_CPUID:
		kvm_mov_from_cpuid(vcpu, inst);
		break;
	case EVENT_VMSW:
		status = IA64_FAULT;
		break;
	default:
		break;
	};
	/*Assume all status is NO_FAULT ?*/
	if (status == IA64_NO_FAULT && cause != EVENT_RFI)
		vcpu_increment_iip(vcpu);

	recover_if_physical_mode(vcpu);
}

void init_vcpu(struct kvm_vcpu *vcpu)
{
	int i;

	vcpu->arch.mode_flags = GUEST_IN_PHY;
	VMX(vcpu, vrr[0]) = 0x38;
	VMX(vcpu, vrr[1]) = 0x38;
	VMX(vcpu, vrr[2]) = 0x38;
	VMX(vcpu, vrr[3]) = 0x38;
	VMX(vcpu, vrr[4]) = 0x38;
	VMX(vcpu, vrr[5]) = 0x38;
	VMX(vcpu, vrr[6]) = 0x38;
	VMX(vcpu, vrr[7]) = 0x38;
	VCPU(vcpu, vpsr) = IA64_PSR_BN;
	VCPU(vcpu, dcr) = 0;
	/* pta.size must not be 0.  The minimum is 15 (32k) */
	VCPU(vcpu, pta) = 15 << 2;
	VCPU(vcpu, itv) = 0x10000;
	VCPU(vcpu, itm) = 0;
	VMX(vcpu, last_itc) = 0;

	VCPU(vcpu, lid) = VCPU_LID(vcpu);
	VCPU(vcpu, ivr) = 0;
	VCPU(vcpu, tpr) = 0x10000;
	VCPU(vcpu, eoi) = 0;
	VCPU(vcpu, irr[0]) = 0;
	VCPU(vcpu, irr[1]) = 0;
	VCPU(vcpu, irr[2]) = 0;
	VCPU(vcpu, irr[3]) = 0;
	VCPU(vcpu, pmv) = 0x10000;
	VCPU(vcpu, cmcv) = 0x10000;
	VCPU(vcpu, lrr0) = 0x10000;   /* default reset value? */
	VCPU(vcpu, lrr1) = 0x10000;   /* default reset value? */
	update_vhpi(vcpu, NULL_VECTOR);
	VLSAPIC_XTP(vcpu) = 0x80;	/* disabled */

	for (i = 0; i < 4; i++)
		VLSAPIC_INSVC(vcpu, i) = 0;
}

void kvm_init_all_rr(struct kvm_vcpu *vcpu)
{
	unsigned long psr;

	local_irq_save(psr);

	/* WARNING: not allow co-exist of both virtual mode and physical
	 * mode in same region
	 */

	vcpu->arch.metaphysical_saved_rr0 = vrrtomrr(VMX(vcpu, vrr[VRN0]));
	vcpu->arch.metaphysical_saved_rr4 = vrrtomrr(VMX(vcpu, vrr[VRN4]));

	if (is_physical_mode(vcpu)) {
		if (vcpu->arch.mode_flags & GUEST_PHY_EMUL)
			panic_vm(vcpu, "Machine Status conflicts!\n");

		ia64_set_rr((VRN0 << VRN_SHIFT), vcpu->arch.metaphysical_rr0);
		ia64_dv_serialize_data();
		ia64_set_rr((VRN4 << VRN_SHIFT), vcpu->arch.metaphysical_rr4);
		ia64_dv_serialize_data();
	} else {
		ia64_set_rr((VRN0 << VRN_SHIFT),
				vcpu->arch.metaphysical_saved_rr0);
		ia64_dv_serialize_data();
		ia64_set_rr((VRN4 << VRN_SHIFT),
				vcpu->arch.metaphysical_saved_rr4);
		ia64_dv_serialize_data();
	}
	ia64_set_rr((VRN1 << VRN_SHIFT),
			vrrtomrr(VMX(vcpu, vrr[VRN1])));
	ia64_dv_serialize_data();
	ia64_set_rr((VRN2 << VRN_SHIFT),
			vrrtomrr(VMX(vcpu, vrr[VRN2])));
	ia64_dv_serialize_data();
	ia64_set_rr((VRN3 << VRN_SHIFT),
			vrrtomrr(VMX(vcpu, vrr[VRN3])));
	ia64_dv_serialize_data();
	ia64_set_rr((VRN5 << VRN_SHIFT),
			vrrtomrr(VMX(vcpu, vrr[VRN5])));
	ia64_dv_serialize_data();
	ia64_set_rr((VRN7 << VRN_SHIFT),
			vrrtomrr(VMX(vcpu, vrr[VRN7])));
	ia64_dv_serialize_data();
	ia64_srlz_d();
	ia64_set_psr(psr);
}

int vmm_entry(void)
{
	struct kvm_vcpu *v;
	v = current_vcpu;

	ia64_call_vsa(PAL_VPS_RESTORE, (unsigned long)v->arch.vpd,
						0, 0, 0, 0, 0, 0);
	kvm_init_vtlb(v);
	kvm_init_vhpt(v);
	init_vcpu(v);
	kvm_init_all_rr(v);
	vmm_reset_entry();

	return 0;
}

static void kvm_show_registers(struct kvm_pt_regs *regs)
{
	unsigned long ip = regs->cr_iip + ia64_psr(regs)->ri;

	struct kvm_vcpu *vcpu = current_vcpu;
	if (vcpu != NULL)
		printk("vcpu 0x%p vcpu %d\n",
		       vcpu, vcpu->vcpu_id);

	printk("psr : %016lx ifs : %016lx ip  : [<%016lx>]\n",
	       regs->cr_ipsr, regs->cr_ifs, ip);

	printk("unat: %016lx pfs : %016lx rsc : %016lx\n",
	       regs->ar_unat, regs->ar_pfs, regs->ar_rsc);
	printk("rnat: %016lx bspstore: %016lx pr  : %016lx\n",
	       regs->ar_rnat, regs->ar_bspstore, regs->pr);
	printk("ldrs: %016lx ccv : %016lx fpsr: %016lx\n",
	       regs->loadrs, regs->ar_ccv, regs->ar_fpsr);
	printk("csd : %016lx ssd : %016lx\n", regs->ar_csd, regs->ar_ssd);
	printk("b0  : %016lx b6  : %016lx b7  : %016lx\n", regs->b0,
							regs->b6, regs->b7);
	printk("f6  : %05lx%016lx f7  : %05lx%016lx\n",
	       regs->f6.u.bits[1], regs->f6.u.bits[0],
	       regs->f7.u.bits[1], regs->f7.u.bits[0]);
	printk("f8  : %05lx%016lx f9  : %05lx%016lx\n",
	       regs->f8.u.bits[1], regs->f8.u.bits[0],
	       regs->f9.u.bits[1], regs->f9.u.bits[0]);
	printk("f10 : %05lx%016lx f11 : %05lx%016lx\n",
	       regs->f10.u.bits[1], regs->f10.u.bits[0],
	       regs->f11.u.bits[1], regs->f11.u.bits[0]);

	printk("r1  : %016lx r2  : %016lx r3  : %016lx\n", regs->r1,
							regs->r2, regs->r3);
	printk("r8  : %016lx r9  : %016lx r10 : %016lx\n", regs->r8,
							regs->r9, regs->r10);
	printk("r11 : %016lx r12 : %016lx r13 : %016lx\n", regs->r11,
							regs->r12, regs->r13);
	printk("r14 : %016lx r15 : %016lx r16 : %016lx\n", regs->r14,
							regs->r15, regs->r16);
	printk("r17 : %016lx r18 : %016lx r19 : %016lx\n", regs->r17,
							regs->r18, regs->r19);
	printk("r20 : %016lx r21 : %016lx r22 : %016lx\n", regs->r20,
							regs->r21, regs->r22);
	printk("r23 : %016lx r24 : %016lx r25 : %016lx\n", regs->r23,
							regs->r24, regs->r25);
	printk("r26 : %016lx r27 : %016lx r28 : %016lx\n", regs->r26,
							regs->r27, regs->r28);
	printk("r29 : %016lx r30 : %016lx r31 : %016lx\n", regs->r29,
							regs->r30, regs->r31);

}

void panic_vm(struct kvm_vcpu *v, const char *fmt, ...)
{
	va_list args;
	char buf[256];

	struct kvm_pt_regs *regs = vcpu_regs(v);
	struct exit_ctl_data *p = &v->arch.exit_data;
	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);
	printk(buf);
	kvm_show_registers(regs);
	p->exit_reason = EXIT_REASON_VM_PANIC;
	vmm_transition(v);
	/*Never to return*/
	while (1);
}
