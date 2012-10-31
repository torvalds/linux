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
 * Copyright IBM Corp. 2007
 *
 * Authors: Hollis Blanchard <hollisb@us.ibm.com>
 */

#ifndef __LINUX_KVM_POWERPC_H
#define __LINUX_KVM_POWERPC_H

#include <linux/types.h>

/* Select powerpc specific features in <linux/kvm.h> */
#define __KVM_HAVE_SPAPR_TCE
#define __KVM_HAVE_PPC_SMT

struct kvm_regs {
	__u64 pc;
	__u64 cr;
	__u64 ctr;
	__u64 lr;
	__u64 xer;
	__u64 msr;
	__u64 srr0;
	__u64 srr1;
	__u64 pid;

	__u64 sprg0;
	__u64 sprg1;
	__u64 sprg2;
	__u64 sprg3;
	__u64 sprg4;
	__u64 sprg5;
	__u64 sprg6;
	__u64 sprg7;

	__u64 gpr[32];
};

#define KVM_SREGS_E_IMPL_NONE	0
#define KVM_SREGS_E_IMPL_FSL	1

#define KVM_SREGS_E_FSL_PIDn	(1 << 0) /* PID1/PID2 */

/*
 * Feature bits indicate which sections of the sregs struct are valid,
 * both in KVM_GET_SREGS and KVM_SET_SREGS.  On KVM_SET_SREGS, registers
 * corresponding to unset feature bits will not be modified.  This allows
 * restoring a checkpoint made without that feature, while keeping the
 * default values of the new registers.
 *
 * KVM_SREGS_E_BASE contains:
 * CSRR0/1 (refers to SRR2/3 on 40x)
 * ESR
 * DEAR
 * MCSR
 * TSR
 * TCR
 * DEC
 * TB
 * VRSAVE (USPRG0)
 */
#define KVM_SREGS_E_BASE		(1 << 0)

/*
 * KVM_SREGS_E_ARCH206 contains:
 *
 * PIR
 * MCSRR0/1
 * DECAR
 * IVPR
 */
#define KVM_SREGS_E_ARCH206		(1 << 1)

/*
 * Contains EPCR, plus the upper half of 64-bit registers
 * that are 32-bit on 32-bit implementations.
 */
#define KVM_SREGS_E_64			(1 << 2)

#define KVM_SREGS_E_SPRG8		(1 << 3)
#define KVM_SREGS_E_MCIVPR		(1 << 4)

/*
 * IVORs are used -- contains IVOR0-15, plus additional IVORs
 * in combination with an appropriate feature bit.
 */
#define KVM_SREGS_E_IVOR		(1 << 5)

/*
 * Contains MAS0-4, MAS6-7, TLBnCFG, MMUCFG.
 * Also TLBnPS if MMUCFG[MAVN] = 1.
 */
#define KVM_SREGS_E_ARCH206_MMU		(1 << 6)

/* DBSR, DBCR, IAC, DAC, DVC */
#define KVM_SREGS_E_DEBUG		(1 << 7)

/* Enhanced debug -- DSRR0/1, SPRG9 */
#define KVM_SREGS_E_ED			(1 << 8)

/* Embedded Floating Point (SPE) -- IVOR32-34 if KVM_SREGS_E_IVOR */
#define KVM_SREGS_E_SPE			(1 << 9)

/* External Proxy (EXP) -- EPR */
#define KVM_SREGS_EXP			(1 << 10)

/* External PID (E.PD) -- EPSC/EPLC */
#define KVM_SREGS_E_PD			(1 << 11)

/* Processor Control (E.PC) -- IVOR36-37 if KVM_SREGS_E_IVOR */
#define KVM_SREGS_E_PC			(1 << 12)

/* Page table (E.PT) -- EPTCFG */
#define KVM_SREGS_E_PT			(1 << 13)

/* Embedded Performance Monitor (E.PM) -- IVOR35 if KVM_SREGS_E_IVOR */
#define KVM_SREGS_E_PM			(1 << 14)

/*
 * Special updates:
 *
 * Some registers may change even while a vcpu is not running.
 * To avoid losing these changes, by default these registers are
 * not updated by KVM_SET_SREGS.  To force an update, set the bit
 * in u.e.update_special corresponding to the register to be updated.
 *
 * The update_special field is zero on return from KVM_GET_SREGS.
 *
 * When restoring a checkpoint, the caller can set update_special
 * to 0xffffffff to ensure that everything is restored, even new features
 * that the caller doesn't know about.
 */
#define KVM_SREGS_E_UPDATE_MCSR		(1 << 0)
#define KVM_SREGS_E_UPDATE_TSR		(1 << 1)
#define KVM_SREGS_E_UPDATE_DEC		(1 << 2)
#define KVM_SREGS_E_UPDATE_DBSR		(1 << 3)

/*
 * In KVM_SET_SREGS, reserved/pad fields must be left untouched from a
 * previous KVM_GET_REGS.
 *
 * Unless otherwise indicated, setting any register with KVM_SET_SREGS
 * directly sets its value.  It does not trigger any special semantics such
 * as write-one-to-clear.  Calling KVM_SET_SREGS on an unmodified struct
 * just received from KVM_GET_SREGS is always a no-op.
 */
struct kvm_sregs {
	__u32 pvr;
	union {
		struct {
			__u64 sdr1;
			struct {
				struct {
					__u64 slbe;
					__u64 slbv;
				} slb[64];
			} ppc64;
			struct {
				__u32 sr[16];
				__u64 ibat[8];
				__u64 dbat[8];
			} ppc32;
		} s;
		struct {
			union {
				struct { /* KVM_SREGS_E_IMPL_FSL */
					__u32 features; /* KVM_SREGS_E_FSL_ */
					__u32 svr;
					__u64 mcar;
					__u32 hid0;

					/* KVM_SREGS_E_FSL_PIDn */
					__u32 pid1, pid2;
				} fsl;
				__u8 pad[256];
			} impl;

			__u32 features; /* KVM_SREGS_E_ */
			__u32 impl_id;	/* KVM_SREGS_E_IMPL_ */
			__u32 update_special; /* KVM_SREGS_E_UPDATE_ */
			__u32 pir;	/* read-only */
			__u64 sprg8;
			__u64 sprg9;	/* E.ED */
			__u64 csrr0;
			__u64 dsrr0;	/* E.ED */
			__u64 mcsrr0;
			__u32 csrr1;
			__u32 dsrr1;	/* E.ED */
			__u32 mcsrr1;
			__u32 esr;
			__u64 dear;
			__u64 ivpr;
			__u64 mcivpr;
			__u64 mcsr;	/* KVM_SREGS_E_UPDATE_MCSR */

			__u32 tsr;	/* KVM_SREGS_E_UPDATE_TSR */
			__u32 tcr;
			__u32 decar;
			__u32 dec;	/* KVM_SREGS_E_UPDATE_DEC */

			/*
			 * Userspace can read TB directly, but the
			 * value reported here is consistent with "dec".
			 *
			 * Read-only.
			 */
			__u64 tb;

			__u32 dbsr;	/* KVM_SREGS_E_UPDATE_DBSR */
			__u32 dbcr[3];
			/*
			 * iac/dac registers are 64bit wide, while this API
			 * interface provides only lower 32 bits on 64 bit
			 * processors. ONE_REG interface is added for 64bit
			 * iac/dac registers.
			 */
			__u32 iac[4];
			__u32 dac[2];
			__u32 dvc[2];
			__u8 num_iac;	/* read-only */
			__u8 num_dac;	/* read-only */
			__u8 num_dvc;	/* read-only */
			__u8 pad;

			__u32 epr;	/* EXP */
			__u32 vrsave;	/* a.k.a. USPRG0 */
			__u32 epcr;	/* KVM_SREGS_E_64 */

			__u32 mas0;
			__u32 mas1;
			__u64 mas2;
			__u64 mas7_3;
			__u32 mas4;
			__u32 mas6;

			__u32 ivor_low[16]; /* IVOR0-15 */
			__u32 ivor_high[18]; /* IVOR32+, plus room to expand */

			__u32 mmucfg;	/* read-only */
			__u32 eptcfg;	/* E.PT, read-only */
			__u32 tlbcfg[4];/* read-only */
			__u32 tlbps[4]; /* read-only */

			__u32 eplc, epsc; /* E.PD */
		} e;
		__u8 pad[1020];
	} u;
};

struct kvm_fpu {
	__u64 fpr[32];
};

struct kvm_debug_exit_arch {
};

/* for KVM_SET_GUEST_DEBUG */
struct kvm_guest_debug_arch {
};

/* definition of registers in kvm_run */
struct kvm_sync_regs {
};

#define KVM_INTERRUPT_SET	-1U
#define KVM_INTERRUPT_UNSET	-2U
#define KVM_INTERRUPT_SET_LEVEL	-3U

#define KVM_CPU_440		1
#define KVM_CPU_E500V2		2
#define KVM_CPU_3S_32		3
#define KVM_CPU_3S_64		4
#define KVM_CPU_E500MC		5

/* for KVM_CAP_SPAPR_TCE */
struct kvm_create_spapr_tce {
	__u64 liobn;
	__u32 window_size;
};

/* for KVM_ALLOCATE_RMA */
struct kvm_allocate_rma {
	__u64 rma_size;
};

struct kvm_book3e_206_tlb_entry {
	__u32 mas8;
	__u32 mas1;
	__u64 mas2;
	__u64 mas7_3;
};

struct kvm_book3e_206_tlb_params {
	/*
	 * For mmu types KVM_MMU_FSL_BOOKE_NOHV and KVM_MMU_FSL_BOOKE_HV:
	 *
	 * - The number of ways of TLB0 must be a power of two between 2 and
	 *   16.
	 * - TLB1 must be fully associative.
	 * - The size of TLB0 must be a multiple of the number of ways, and
	 *   the number of sets must be a power of two.
	 * - The size of TLB1 may not exceed 64 entries.
	 * - TLB0 supports 4 KiB pages.
	 * - The page sizes supported by TLB1 are as indicated by
	 *   TLB1CFG (if MMUCFG[MAVN] = 0) or TLB1PS (if MMUCFG[MAVN] = 1)
	 *   as returned by KVM_GET_SREGS.
	 * - TLB2 and TLB3 are reserved, and their entries in tlb_sizes[]
	 *   and tlb_ways[] must be zero.
	 *
	 * tlb_ways[n] = tlb_sizes[n] means the array is fully associative.
	 *
	 * KVM will adjust TLBnCFG based on the sizes configured here,
	 * though arrays greater than 2048 entries will have TLBnCFG[NENTRY]
	 * set to zero.
	 */
	__u32 tlb_sizes[4];
	__u32 tlb_ways[4];
	__u32 reserved[8];
};

#define KVM_REG_PPC_HIOR	(KVM_REG_PPC | KVM_REG_SIZE_U64 | 0x1)
#define KVM_REG_PPC_IAC1	(KVM_REG_PPC | KVM_REG_SIZE_U64 | 0x2)
#define KVM_REG_PPC_IAC2	(KVM_REG_PPC | KVM_REG_SIZE_U64 | 0x3)
#define KVM_REG_PPC_IAC3	(KVM_REG_PPC | KVM_REG_SIZE_U64 | 0x4)
#define KVM_REG_PPC_IAC4	(KVM_REG_PPC | KVM_REG_SIZE_U64 | 0x5)
#define KVM_REG_PPC_DAC1	(KVM_REG_PPC | KVM_REG_SIZE_U64 | 0x6)
#define KVM_REG_PPC_DAC2	(KVM_REG_PPC | KVM_REG_SIZE_U64 | 0x7)
#define KVM_REG_PPC_DABR	(KVM_REG_PPC | KVM_REG_SIZE_U64 | 0x8)
#define KVM_REG_PPC_DSCR	(KVM_REG_PPC | KVM_REG_SIZE_U64 | 0x9)
#define KVM_REG_PPC_PURR	(KVM_REG_PPC | KVM_REG_SIZE_U64 | 0xa)
#define KVM_REG_PPC_SPURR	(KVM_REG_PPC | KVM_REG_SIZE_U64 | 0xb)
#define KVM_REG_PPC_DAR		(KVM_REG_PPC | KVM_REG_SIZE_U64 | 0xc)
#define KVM_REG_PPC_DSISR	(KVM_REG_PPC | KVM_REG_SIZE_U32 | 0xd)
#define KVM_REG_PPC_AMR		(KVM_REG_PPC | KVM_REG_SIZE_U64 | 0xe)
#define KVM_REG_PPC_UAMOR	(KVM_REG_PPC | KVM_REG_SIZE_U64 | 0xf)

#define KVM_REG_PPC_MMCR0	(KVM_REG_PPC | KVM_REG_SIZE_U64 | 0x10)
#define KVM_REG_PPC_MMCR1	(KVM_REG_PPC | KVM_REG_SIZE_U64 | 0x11)
#define KVM_REG_PPC_MMCRA	(KVM_REG_PPC | KVM_REG_SIZE_U64 | 0x12)

#define KVM_REG_PPC_PMC1	(KVM_REG_PPC | KVM_REG_SIZE_U32 | 0x18)
#define KVM_REG_PPC_PMC2	(KVM_REG_PPC | KVM_REG_SIZE_U32 | 0x19)
#define KVM_REG_PPC_PMC3	(KVM_REG_PPC | KVM_REG_SIZE_U32 | 0x1a)
#define KVM_REG_PPC_PMC4	(KVM_REG_PPC | KVM_REG_SIZE_U32 | 0x1b)
#define KVM_REG_PPC_PMC5	(KVM_REG_PPC | KVM_REG_SIZE_U32 | 0x1c)
#define KVM_REG_PPC_PMC6	(KVM_REG_PPC | KVM_REG_SIZE_U32 | 0x1d)
#define KVM_REG_PPC_PMC7	(KVM_REG_PPC | KVM_REG_SIZE_U32 | 0x1e)
#define KVM_REG_PPC_PMC8	(KVM_REG_PPC | KVM_REG_SIZE_U32 | 0x1f)

/* 32 floating-point registers */
#define KVM_REG_PPC_FPR0	(KVM_REG_PPC | KVM_REG_SIZE_U64 | 0x20)
#define KVM_REG_PPC_FPR(n)	(KVM_REG_PPC_FPR0 + (n))
#define KVM_REG_PPC_FPR31	(KVM_REG_PPC | KVM_REG_SIZE_U64 | 0x3f)

/* 32 VMX/Altivec vector registers */
#define KVM_REG_PPC_VR0		(KVM_REG_PPC | KVM_REG_SIZE_U128 | 0x40)
#define KVM_REG_PPC_VR(n)	(KVM_REG_PPC_VR0 + (n))
#define KVM_REG_PPC_VR31	(KVM_REG_PPC | KVM_REG_SIZE_U128 | 0x5f)

/* 32 double-width FP registers for VSX */
/* High-order halves overlap with FP regs */
#define KVM_REG_PPC_VSR0	(KVM_REG_PPC | KVM_REG_SIZE_U128 | 0x60)
#define KVM_REG_PPC_VSR(n)	(KVM_REG_PPC_VSR0 + (n))
#define KVM_REG_PPC_VSR31	(KVM_REG_PPC | KVM_REG_SIZE_U128 | 0x7f)

/* FP and vector status/control registers */
#define KVM_REG_PPC_FPSCR	(KVM_REG_PPC | KVM_REG_SIZE_U64 | 0x80)
#define KVM_REG_PPC_VSCR	(KVM_REG_PPC | KVM_REG_SIZE_U32 | 0x81)

/* Virtual processor areas */
/* For SLB & DTL, address in high (first) half, length in low half */
#define KVM_REG_PPC_VPA_ADDR	(KVM_REG_PPC | KVM_REG_SIZE_U64 | 0x82)
#define KVM_REG_PPC_VPA_SLB	(KVM_REG_PPC | KVM_REG_SIZE_U128 | 0x83)
#define KVM_REG_PPC_VPA_DTL	(KVM_REG_PPC | KVM_REG_SIZE_U128 | 0x84)

#endif /* __LINUX_KVM_POWERPC_H */
