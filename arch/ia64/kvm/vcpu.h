/*
 *  vcpu.h: vcpu routines
 *  	Copyright (c) 2005, Intel Corporation.
 *  	Xuefei Xu (Anthony Xu) (Anthony.xu@intel.com)
 *  	Yaozu Dong (Eddie Dong) (Eddie.dong@intel.com)
 *
 * 	Copyright (c) 2007, Intel Corporation.
 *  	Xuefei Xu (Anthony Xu) (Anthony.xu@intel.com)
 *	Xiantao Zhang (xiantao.zhang@intel.com)
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
 */


#ifndef __KVM_VCPU_H__
#define __KVM_VCPU_H__

#include <asm/types.h>
#include <asm/fpu.h>
#include <asm/processor.h>

#ifndef __ASSEMBLY__
#include "vti.h"

#include <linux/kvm_host.h>
#include <linux/spinlock.h>

typedef unsigned long IA64_INST;

typedef union U_IA64_BUNDLE {
	unsigned long i64[2];
	struct { unsigned long template:5, slot0:41, slot1a:18,
		slot1b:23, slot2:41; };
	/* NOTE: following doesn't work because bitfields can't cross natural
	   size boundaries
	   struct { unsigned long template:5, slot0:41, slot1:41, slot2:41; }; */
} IA64_BUNDLE;

typedef union U_INST64_A5 {
	IA64_INST inst;
	struct { unsigned long qp:6, r1:7, imm7b:7, r3:2, imm5c:5,
		imm9d:9, s:1, major:4; };
} INST64_A5;

typedef union U_INST64_B4 {
	IA64_INST inst;
	struct { unsigned long qp:6, btype:3, un3:3, p:1, b2:3, un11:11, x6:6,
		wh:2, d:1, un1:1, major:4; };
} INST64_B4;

typedef union U_INST64_B8 {
	IA64_INST inst;
	struct { unsigned long qp:6, un21:21, x6:6, un4:4, major:4; };
} INST64_B8;

typedef union U_INST64_B9 {
	IA64_INST inst;
	struct { unsigned long qp:6, imm20:20, :1, x6:6, :3, i:1, major:4; };
} INST64_B9;

typedef union U_INST64_I19 {
	IA64_INST inst;
	struct { unsigned long qp:6, imm20:20, :1, x6:6, x3:3, i:1, major:4; };
} INST64_I19;

typedef union U_INST64_I26 {
	IA64_INST inst;
	struct { unsigned long qp:6, :7, r2:7, ar3:7, x6:6, x3:3, :1, major:4; };
} INST64_I26;

typedef union U_INST64_I27 {
	IA64_INST inst;
	struct { unsigned long qp:6, :7, imm:7, ar3:7, x6:6, x3:3, s:1, major:4; };
} INST64_I27;

typedef union U_INST64_I28 { /* not privileged (mov from AR) */
	IA64_INST inst;
	struct { unsigned long qp:6, r1:7, :7, ar3:7, x6:6, x3:3, :1, major:4; };
} INST64_I28;

typedef union U_INST64_M28 {
	IA64_INST inst;
	struct { unsigned long qp:6, :14, r3:7, x6:6, x3:3, :1, major:4; };
} INST64_M28;

typedef union U_INST64_M29 {
	IA64_INST inst;
	struct { unsigned long qp:6, :7, r2:7, ar3:7, x6:6, x3:3, :1, major:4; };
} INST64_M29;

typedef union U_INST64_M30 {
	IA64_INST inst;
	struct { unsigned long qp:6, :7, imm:7, ar3:7, x4:4, x2:2,
		x3:3, s:1, major:4; };
} INST64_M30;

typedef union U_INST64_M31 {
	IA64_INST inst;
	struct { unsigned long qp:6, r1:7, :7, ar3:7, x6:6, x3:3, :1, major:4; };
} INST64_M31;

typedef union U_INST64_M32 {
	IA64_INST inst;
	struct { unsigned long qp:6, :7, r2:7, cr3:7, x6:6, x3:3, :1, major:4; };
} INST64_M32;

typedef union U_INST64_M33 {
	IA64_INST inst;
	struct { unsigned long qp:6, r1:7, :7, cr3:7, x6:6, x3:3, :1, major:4; };
} INST64_M33;

typedef union U_INST64_M35 {
	IA64_INST inst;
	struct { unsigned long qp:6, :7, r2:7, :7, x6:6, x3:3, :1, major:4; };

} INST64_M35;

typedef union U_INST64_M36 {
	IA64_INST inst;
	struct { unsigned long qp:6, r1:7, :14, x6:6, x3:3, :1, major:4; };
} INST64_M36;

typedef union U_INST64_M37 {
	IA64_INST inst;
	struct { unsigned long qp:6, imm20a:20, :1, x4:4, x2:2, x3:3,
		i:1, major:4; };
} INST64_M37;

typedef union U_INST64_M41 {
	IA64_INST inst;
	struct { unsigned long qp:6, :7, r2:7, :7, x6:6, x3:3, :1, major:4; };
} INST64_M41;

typedef union U_INST64_M42 {
	IA64_INST inst;
	struct { unsigned long qp:6, :7, r2:7, r3:7, x6:6, x3:3, :1, major:4; };
} INST64_M42;

typedef union U_INST64_M43 {
	IA64_INST inst;
	struct { unsigned long qp:6, r1:7, :7, r3:7, x6:6, x3:3, :1, major:4; };
} INST64_M43;

typedef union U_INST64_M44 {
	IA64_INST inst;
	struct { unsigned long qp:6, imm:21, x4:4, i2:2, x3:3, i:1, major:4; };
} INST64_M44;

typedef union U_INST64_M45 {
	IA64_INST inst;
	struct { unsigned long qp:6, :7, r2:7, r3:7, x6:6, x3:3, :1, major:4; };
} INST64_M45;

typedef union U_INST64_M46 {
	IA64_INST inst;
	struct { unsigned long qp:6, r1:7, un7:7, r3:7, x6:6,
		x3:3, un1:1, major:4; };
} INST64_M46;

typedef union U_INST64_M47 {
	IA64_INST inst;
	struct { unsigned long qp:6, un14:14, r3:7, x6:6, x3:3, un1:1, major:4; };
} INST64_M47;

typedef union U_INST64_M1{
	IA64_INST inst;
	struct { unsigned long qp:6, r1:7, un7:7, r3:7, x:1, hint:2,
		x6:6, m:1, major:4; };
} INST64_M1;

typedef union U_INST64_M2{
	IA64_INST inst;
	struct { unsigned long qp:6, r1:7, r2:7, r3:7, x:1, hint:2,
		x6:6, m:1, major:4; };
} INST64_M2;

typedef union U_INST64_M3{
	IA64_INST inst;
	struct { unsigned long qp:6, r1:7, imm7:7, r3:7, i:1, hint:2,
		x6:6, s:1, major:4; };
} INST64_M3;

typedef union U_INST64_M4 {
	IA64_INST inst;
	struct { unsigned long qp:6, un7:7, r2:7, r3:7, x:1, hint:2,
		x6:6, m:1, major:4; };
} INST64_M4;

typedef union U_INST64_M5 {
	IA64_INST inst;
	struct { unsigned long qp:6, imm7:7, r2:7, r3:7, i:1, hint:2,
		x6:6, s:1, major:4; };
} INST64_M5;

typedef union U_INST64_M6 {
	IA64_INST inst;
	struct { unsigned long qp:6, f1:7, un7:7, r3:7, x:1, hint:2,
		x6:6, m:1, major:4; };
} INST64_M6;

typedef union U_INST64_M9 {
	IA64_INST inst;
	struct { unsigned long qp:6, :7, f2:7, r3:7, x:1, hint:2,
		x6:6, m:1, major:4; };
} INST64_M9;

typedef union U_INST64_M10 {
	IA64_INST inst;
	struct { unsigned long qp:6, imm7:7, f2:7, r3:7, i:1, hint:2,
		x6:6, s:1, major:4; };
} INST64_M10;

typedef union U_INST64_M12 {
	IA64_INST inst;
	struct { unsigned long qp:6, f1:7, f2:7, r3:7, x:1, hint:2,
		x6:6, m:1, major:4; };
} INST64_M12;

typedef union U_INST64_M15 {
	IA64_INST inst;
	struct { unsigned long qp:6, :7, imm7:7, r3:7, i:1, hint:2,
		x6:6, s:1, major:4; };
} INST64_M15;

typedef union U_INST64 {
	IA64_INST inst;
	struct { unsigned long :37, major:4; } generic;
	INST64_A5 A5;	/* used in build_hypercall_bundle only */
	INST64_B4 B4;	/* used in build_hypercall_bundle only */
	INST64_B8 B8;	/* rfi, bsw.[01] */
	INST64_B9 B9;	/* break.b */
	INST64_I19 I19;	/* used in build_hypercall_bundle only */
	INST64_I26 I26;	/* mov register to ar (I unit) */
	INST64_I27 I27;	/* mov immediate to ar (I unit) */
	INST64_I28 I28;	/* mov from ar (I unit) */
	INST64_M1  M1;	/* ld integer */
	INST64_M2  M2;
	INST64_M3  M3;
	INST64_M4  M4;	/* st integer */
	INST64_M5  M5;
	INST64_M6  M6;	/* ldfd floating pointer 		*/
	INST64_M9  M9;	/* stfd floating pointer		*/
	INST64_M10 M10;	/* stfd floating pointer		*/
	INST64_M12 M12;     /* ldfd pair floating pointer		*/
	INST64_M15 M15;	/* lfetch + imm update			*/
	INST64_M28 M28;	/* purge translation cache entry	*/
	INST64_M29 M29;	/* mov register to ar (M unit)		*/
	INST64_M30 M30;	/* mov immediate to ar (M unit)		*/
	INST64_M31 M31;	/* mov from ar (M unit)			*/
	INST64_M32 M32;	/* mov reg to cr			*/
	INST64_M33 M33;	/* mov from cr				*/
	INST64_M35 M35;	/* mov to psr				*/
	INST64_M36 M36;	/* mov from psr				*/
	INST64_M37 M37;	/* break.m				*/
	INST64_M41 M41;	/* translation cache insert		*/
	INST64_M42 M42;	/* mov to indirect reg/translation reg insert*/
	INST64_M43 M43;	/* mov from indirect reg		*/
	INST64_M44 M44;	/* set/reset system mask		*/
	INST64_M45 M45;	/* translation purge			*/
	INST64_M46 M46;	/* translation access (tpa,tak)		*/
	INST64_M47 M47;	/* purge translation entry		*/
} INST64;

#define MASK_41 ((unsigned long)0x1ffffffffff)

/* Virtual address memory attributes encoding */
#define VA_MATTR_WB         0x0
#define VA_MATTR_UC         0x4
#define VA_MATTR_UCE        0x5
#define VA_MATTR_WC         0x6
#define VA_MATTR_NATPAGE    0x7

#define PMASK(size)         (~((size) - 1))
#define PSIZE(size)         (1UL<<(size))
#define CLEARLSB(ppn, nbits)    (((ppn) >> (nbits)) << (nbits))
#define PAGEALIGN(va, ps)	CLEARLSB(va, ps)
#define PAGE_FLAGS_RV_MASK   (0x2|(0x3UL<<50)|(((1UL<<11)-1)<<53))
#define _PAGE_MA_ST     (0x1 <<  2) /* is reserved for software use */

#define ARCH_PAGE_SHIFT   12

#define INVALID_TI_TAG (1UL << 63)

#define VTLB_PTE_P_BIT      0
#define VTLB_PTE_IO_BIT     60
#define VTLB_PTE_IO         (1UL<<VTLB_PTE_IO_BIT)
#define VTLB_PTE_P          (1UL<<VTLB_PTE_P_BIT)

#define vcpu_quick_region_check(_tr_regions,_ifa)		\
	(_tr_regions & (1 << ((unsigned long)_ifa >> 61)))

#define vcpu_quick_region_set(_tr_regions,_ifa)             \
	do {_tr_regions |= (1 << ((unsigned long)_ifa >> 61)); } while (0)

static inline void vcpu_set_tr(struct thash_data *trp, u64 pte, u64 itir,
		u64 va, u64 rid)
{
	trp->page_flags = pte;
	trp->itir = itir;
	trp->vadr = va;
	trp->rid = rid;
}

extern u64 kvm_get_mpt_entry(u64 gpfn);

/* Return I/ */
static inline u64 __gpfn_is_io(u64 gpfn)
{
	u64  pte;
	pte = kvm_get_mpt_entry(gpfn);
	if (!(pte & GPFN_INV_MASK)) {
		pte = pte & GPFN_IO_MASK;
		if (pte != GPFN_PHYS_MMIO)
			return pte;
	}
	return 0;
}
#endif
#define IA64_NO_FAULT	0
#define IA64_FAULT	1

#define VMM_RBS_OFFSET  ((VMM_TASK_SIZE + 15) & ~15)

#define SW_BAD  0   /* Bad mode transitition */
#define SW_V2P  1   /* Physical emulatino is activated */
#define SW_P2V  2   /* Exit physical mode emulation */
#define SW_SELF 3   /* No mode transition */
#define SW_NOP  4   /* Mode transition, but without action required */

#define GUEST_IN_PHY    0x1
#define GUEST_PHY_EMUL  0x2

#define current_vcpu ((struct kvm_vcpu *) ia64_getreg(_IA64_REG_TP))

#define VRN_SHIFT	61
#define VRN_MASK	0xe000000000000000
#define VRN0		0x0UL
#define VRN1		0x1UL
#define VRN2		0x2UL
#define VRN3		0x3UL
#define VRN4		0x4UL
#define VRN5		0x5UL
#define VRN6		0x6UL
#define VRN7		0x7UL

#define IRQ_NO_MASKED         0
#define IRQ_MASKED_BY_VTPR    1
#define IRQ_MASKED_BY_INSVC   2   /* masked by inservice IRQ */

#define PTA_BASE_SHIFT      15

#define IA64_PSR_VM_BIT     46
#define IA64_PSR_VM (__IA64_UL(1) << IA64_PSR_VM_BIT)

/* Interruption Function State */
#define IA64_IFS_V_BIT      63
#define IA64_IFS_V  (__IA64_UL(1) << IA64_IFS_V_BIT)

#define PHY_PAGE_UC (_PAGE_A|_PAGE_D|_PAGE_P|_PAGE_MA_UC|_PAGE_AR_RWX)
#define PHY_PAGE_WB (_PAGE_A|_PAGE_D|_PAGE_P|_PAGE_MA_WB|_PAGE_AR_RWX)

#ifndef __ASSEMBLY__

#include <asm/gcc_intrin.h>

#define is_physical_mode(v)		\
	((v->arch.mode_flags) & GUEST_IN_PHY)

#define is_virtual_mode(v)	\
	(!is_physical_mode(v))

#define MODE_IND(psr)	\
	(((psr).it << 2) + ((psr).dt << 1) + (psr).rt)

#ifndef CONFIG_SMP
#define _vmm_raw_spin_lock(x)	 do {}while(0)
#define _vmm_raw_spin_unlock(x) do {}while(0)
#else
#define _vmm_raw_spin_lock(x)						\
	do {								\
		__u32 *ia64_spinlock_ptr = (__u32 *) (x);		\
		__u64 ia64_spinlock_val;				\
		ia64_spinlock_val = ia64_cmpxchg4_acq(ia64_spinlock_ptr, 1, 0);\
		if (unlikely(ia64_spinlock_val)) {			\
			do {						\
				while (*ia64_spinlock_ptr)		\
				ia64_barrier();				\
				ia64_spinlock_val =			\
				ia64_cmpxchg4_acq(ia64_spinlock_ptr, 1, 0);\
			} while (ia64_spinlock_val);			\
		}							\
	} while (0)

#define _vmm_raw_spin_unlock(x)				\
	do { barrier();				\
		((spinlock_t *)x)->raw_lock.lock = 0; } \
while (0)
#endif

void vmm_spin_lock(spinlock_t *lock);
void vmm_spin_unlock(spinlock_t *lock);
enum {
	I_TLB = 1,
	D_TLB = 2
};

union kvm_va {
	struct {
		unsigned long off : 60;		/* intra-region offset */
		unsigned long reg :  4;		/* region number */
	} f;
	unsigned long l;
	void *p;
};

#define __kvm_pa(x)     ({union kvm_va _v; _v.l = (long) (x);		\
						_v.f.reg = 0; _v.l; })
#define __kvm_va(x)     ({union kvm_va _v; _v.l = (long) (x);		\
				_v.f.reg = -1; _v.p; })

#define _REGION_ID(x)           ({union ia64_rr _v; _v.val = (long)(x); \
						_v.rid; })
#define _REGION_PAGE_SIZE(x)    ({union ia64_rr _v; _v.val = (long)(x); \
						_v.ps; })
#define _REGION_HW_WALKER(x)    ({union ia64_rr _v; _v.val = (long)(x);	\
						_v.ve; })

enum vhpt_ref{ DATA_REF, NA_REF, INST_REF, RSE_REF };
enum tlb_miss_type { INSTRUCTION, DATA, REGISTER };

#define VCPU(_v, _x) ((_v)->arch.vpd->_x)
#define VMX(_v, _x)  ((_v)->arch._x)

#define VLSAPIC_INSVC(vcpu, i) ((vcpu)->arch.insvc[i])
#define VLSAPIC_XTP(_v)        VMX(_v, xtp)

static inline unsigned long itir_ps(unsigned long itir)
{
	return ((itir >> 2) & 0x3f);
}


/**************************************************************************
  VCPU control register access routines
 **************************************************************************/

static inline u64 vcpu_get_itir(struct kvm_vcpu *vcpu)
{
	return ((u64)VCPU(vcpu, itir));
}

static inline void vcpu_set_itir(struct kvm_vcpu *vcpu, u64 val)
{
	VCPU(vcpu, itir) = val;
}

static inline u64 vcpu_get_ifa(struct kvm_vcpu *vcpu)
{
	return ((u64)VCPU(vcpu, ifa));
}

static inline void vcpu_set_ifa(struct kvm_vcpu *vcpu, u64 val)
{
	VCPU(vcpu, ifa) = val;
}

static inline u64 vcpu_get_iva(struct kvm_vcpu *vcpu)
{
	return ((u64)VCPU(vcpu, iva));
}

static inline u64 vcpu_get_pta(struct kvm_vcpu *vcpu)
{
	return ((u64)VCPU(vcpu, pta));
}

static inline u64 vcpu_get_lid(struct kvm_vcpu *vcpu)
{
	return ((u64)VCPU(vcpu, lid));
}

static inline u64 vcpu_get_tpr(struct kvm_vcpu *vcpu)
{
	return ((u64)VCPU(vcpu, tpr));
}

static inline u64 vcpu_get_eoi(struct kvm_vcpu *vcpu)
{
	return (0UL);		/*reads of eoi always return 0 */
}

static inline u64 vcpu_get_irr0(struct kvm_vcpu *vcpu)
{
	return ((u64)VCPU(vcpu, irr[0]));
}

static inline u64 vcpu_get_irr1(struct kvm_vcpu *vcpu)
{
	return ((u64)VCPU(vcpu, irr[1]));
}

static inline u64 vcpu_get_irr2(struct kvm_vcpu *vcpu)
{
	return ((u64)VCPU(vcpu, irr[2]));
}

static inline u64 vcpu_get_irr3(struct kvm_vcpu *vcpu)
{
	return ((u64)VCPU(vcpu, irr[3]));
}

static inline void vcpu_set_dcr(struct kvm_vcpu *vcpu, u64 val)
{
	ia64_setreg(_IA64_REG_CR_DCR, val);
}

static inline void vcpu_set_isr(struct kvm_vcpu *vcpu, u64 val)
{
	VCPU(vcpu, isr) = val;
}

static inline void vcpu_set_lid(struct kvm_vcpu *vcpu, u64 val)
{
	VCPU(vcpu, lid) = val;
}

static inline void vcpu_set_ipsr(struct kvm_vcpu *vcpu, u64 val)
{
	VCPU(vcpu, ipsr) = val;
}

static inline void vcpu_set_iip(struct kvm_vcpu *vcpu, u64 val)
{
	VCPU(vcpu, iip) = val;
}

static inline void vcpu_set_ifs(struct kvm_vcpu *vcpu, u64 val)
{
	VCPU(vcpu, ifs) = val;
}

static inline void vcpu_set_iipa(struct kvm_vcpu *vcpu, u64 val)
{
	VCPU(vcpu, iipa) = val;
}

static inline void vcpu_set_iha(struct kvm_vcpu *vcpu, u64 val)
{
	VCPU(vcpu, iha) = val;
}


static inline u64 vcpu_get_rr(struct kvm_vcpu *vcpu, u64 reg)
{
	return vcpu->arch.vrr[reg>>61];
}

/**************************************************************************
  VCPU debug breakpoint register access routines
 **************************************************************************/

static inline void vcpu_set_dbr(struct kvm_vcpu *vcpu, u64 reg, u64 val)
{
	__ia64_set_dbr(reg, val);
}

static inline void vcpu_set_ibr(struct kvm_vcpu *vcpu, u64 reg, u64 val)
{
	ia64_set_ibr(reg, val);
}

static inline u64 vcpu_get_dbr(struct kvm_vcpu *vcpu, u64 reg)
{
	return ((u64)__ia64_get_dbr(reg));
}

static inline u64 vcpu_get_ibr(struct kvm_vcpu *vcpu, u64 reg)
{
	return ((u64)ia64_get_ibr(reg));
}

/**************************************************************************
  VCPU performance monitor register access routines
 **************************************************************************/
static inline void vcpu_set_pmc(struct kvm_vcpu *vcpu, u64 reg, u64 val)
{
	/* NOTE: Writes to unimplemented PMC registers are discarded */
	ia64_set_pmc(reg, val);
}

static inline void vcpu_set_pmd(struct kvm_vcpu *vcpu, u64 reg, u64 val)
{
	/* NOTE: Writes to unimplemented PMD registers are discarded */
	ia64_set_pmd(reg, val);
}

static inline u64 vcpu_get_pmc(struct kvm_vcpu *vcpu, u64 reg)
{
	/* NOTE: Reads from unimplemented PMC registers return zero */
	return ((u64)ia64_get_pmc(reg));
}

static inline u64 vcpu_get_pmd(struct kvm_vcpu *vcpu, u64 reg)
{
	/* NOTE: Reads from unimplemented PMD registers return zero */
	return ((u64)ia64_get_pmd(reg));
}

static inline unsigned long vrrtomrr(unsigned long val)
{
	union ia64_rr rr;
	rr.val = val;
	rr.rid = (rr.rid << 4) | 0xe;
	if (rr.ps > PAGE_SHIFT)
		rr.ps = PAGE_SHIFT;
	rr.ve = 1;
	return rr.val;
}


static inline int highest_bits(int *dat)
{
	u32  bits, bitnum;
	int i;

	/* loop for all 256 bits */
	for (i = 7; i >= 0 ; i--) {
		bits = dat[i];
		if (bits) {
			bitnum = fls(bits);
			return i * 32 + bitnum - 1;
		}
	}
	return NULL_VECTOR;
}

/*
 * The pending irq is higher than the inservice one.
 *
 */
static inline int is_higher_irq(int pending, int inservice)
{
	return ((pending > inservice)
			|| ((pending != NULL_VECTOR)
				&& (inservice == NULL_VECTOR)));
}

static inline int is_higher_class(int pending, int mic)
{
	return ((pending >> 4) > mic);
}

/*
 * Return 0-255 for pending irq.
 *        NULL_VECTOR: when no pending.
 */
static inline int highest_pending_irq(struct kvm_vcpu *vcpu)
{
	if (VCPU(vcpu, irr[0]) & (1UL<<NMI_VECTOR))
		return NMI_VECTOR;
	if (VCPU(vcpu, irr[0]) & (1UL<<ExtINT_VECTOR))
		return ExtINT_VECTOR;

	return highest_bits((int *)&VCPU(vcpu, irr[0]));
}

static inline int highest_inservice_irq(struct kvm_vcpu *vcpu)
{
	if (VMX(vcpu, insvc[0]) & (1UL<<NMI_VECTOR))
		return NMI_VECTOR;
	if (VMX(vcpu, insvc[0]) & (1UL<<ExtINT_VECTOR))
		return ExtINT_VECTOR;

	return highest_bits((int *)&(VMX(vcpu, insvc[0])));
}

extern void vcpu_get_fpreg(struct kvm_vcpu *vcpu, u64 reg,
					struct ia64_fpreg *val);
extern void vcpu_set_fpreg(struct kvm_vcpu *vcpu, u64 reg,
					struct ia64_fpreg *val);
extern u64 vcpu_get_gr(struct kvm_vcpu *vcpu, u64 reg);
extern void vcpu_set_gr(struct kvm_vcpu *vcpu, u64 reg, u64 val, int nat);
extern u64 vcpu_get_psr(struct kvm_vcpu *vcpu);
extern void vcpu_set_psr(struct kvm_vcpu *vcpu, u64 val);
extern u64 vcpu_thash(struct kvm_vcpu *vcpu, u64 vadr);
extern void vcpu_bsw0(struct kvm_vcpu *vcpu);
extern void thash_vhpt_insert(struct kvm_vcpu *v, u64 pte,
					u64 itir, u64 va, int type);
extern struct thash_data *vhpt_lookup(u64 va);
extern u64 guest_vhpt_lookup(u64 iha, u64 *pte);
extern void thash_purge_entries(struct kvm_vcpu *v, u64 va, u64 ps);
extern void thash_purge_entries_remote(struct kvm_vcpu *v, u64 va, u64 ps);
extern u64 translate_phy_pte(u64 *pte, u64 itir, u64 va);
extern void thash_purge_and_insert(struct kvm_vcpu *v, u64 pte,
		u64 itir, u64 ifa, int type);
extern void thash_purge_all(struct kvm_vcpu *v);
extern struct thash_data *vtlb_lookup(struct kvm_vcpu *v,
						u64 va, int is_data);
extern int vtr_find_overlap(struct kvm_vcpu *vcpu, u64 va,
						u64 ps, int is_data);

extern void vcpu_increment_iip(struct kvm_vcpu *v);
extern void vcpu_decrement_iip(struct kvm_vcpu *vcpu);
extern void vcpu_pend_interrupt(struct kvm_vcpu *vcpu, u8 vec);
extern void vcpu_unpend_interrupt(struct kvm_vcpu *vcpu, u8 vec);
extern void data_page_not_present(struct kvm_vcpu *vcpu, u64 vadr);
extern void dnat_page_consumption(struct kvm_vcpu *vcpu, u64 vadr);
extern void alt_dtlb(struct kvm_vcpu *vcpu, u64 vadr);
extern void nested_dtlb(struct kvm_vcpu *vcpu);
extern void dvhpt_fault(struct kvm_vcpu *vcpu, u64 vadr);
extern int vhpt_enabled(struct kvm_vcpu *vcpu, u64 vadr, enum vhpt_ref ref);

extern void update_vhpi(struct kvm_vcpu *vcpu, int vec);
extern int irq_masked(struct kvm_vcpu *vcpu, int h_pending, int h_inservice);

extern int fetch_code(struct kvm_vcpu *vcpu, u64 gip, IA64_BUNDLE *pbundle);
extern void emulate_io_inst(struct kvm_vcpu *vcpu, u64 padr, u64 ma);
extern void vmm_transition(struct kvm_vcpu *vcpu);
extern void vmm_trampoline(union context *from, union context *to);
extern int vmm_entry(void);
extern  u64 vcpu_get_itc(struct kvm_vcpu *vcpu);

extern void vmm_reset_entry(void);
void kvm_init_vtlb(struct kvm_vcpu *v);
void kvm_init_vhpt(struct kvm_vcpu *v);
void thash_init(struct thash_cb *hcb, u64 sz);

void panic_vm(struct kvm_vcpu *v, const char *fmt, ...);
u64 kvm_gpa_to_mpa(u64 gpa);
extern u64 ia64_call_vsa(u64 proc, u64 arg1, u64 arg2, u64 arg3,
		u64 arg4, u64 arg5, u64 arg6, u64 arg7);

extern long vmm_sanity;

#endif
#endif	/* __VCPU_H__ */
