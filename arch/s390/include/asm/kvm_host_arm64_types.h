/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ASM_KVM_HOST_ARM64_TYPES_H
#define ASM_KVM_HOST_ARM64_TYPES_H

#include <linux/types.h>
#include <linux/kvm_types.h>
#include <linux/compiler_attributes.h>
#include <asm/page.h>
#include <asm/fault.h>

struct kvm_sae_block {
	u64	_0000[16];		/* 0x0000 */
#define SAE_ICPTR_SPURIOUS			0x00
#define SAE_ICPTR_VALIDITY			0x01
#define SAE_ICPTR_HOST_ACCESS_EXCEPTION		0x02
#define SAE_ICPTR_SYNCHRONOUS_EXCEPTION		0x03
#define SAE_ICPTR_TIMER				0x04
#define SAE_ICPTR_PE_INTERCOMM			0x05
#define SAE_ICPTR_GUEST_ADDRESS_SIZE		0x06
#define SAE_ICPTR_STOP				0x07
#define SAE_ICPTR_MIO_ADDRESS			0x08
#define SAE_ICPTR_PMU				0x09
#define SAE_ICPTR_MAINTENANCE			0x0a
	u8	icptr;			/* 0x0080 */
	u8	_0081[7];		/* 0x0081 */
	u64	scad;			/* 0x0088 */
	u64	_0090[16];		/* 0x0090 */
	u32	cntp_ctl;		/* 0x0110 */
	u32	cntv_ctl;		/* 0x0114 */
	u8	irq_ctl;		/* 0x0118 */
	u8	_0119[7];		/* 0x0119 */
	struct {
		u64	ich_hcr_el2;	/* 0x0120 */
		u64	ich_vmcr_el2;	/* 0x0128 */
		u64	ich_ap0r0_el2;	/* 0x0130 */
		u64	ich_ap1r0_el2;	/* 0x0138 */
		u64	_0140[2];	/* 0x0140 */
		u64	ich_lrn_el2[4];	/* 0x0150 */
		u64	_0170[4];	/* 0x0170 */
	} ic_regs;
	u64	_0190[12];		/* 0x0190 */
	u64	contextidr_el1;		/* 0x01f0 */
	u32	wip;			/* 0x01f8 */
	u32	_01fc;			/* 0x01fc */
#define SAE_SD_FORMAT_0                 0x00
	u8	sdf;			/* 0x0200  */
	u8	_0201[7];		/* 0x0201  */
	u64	mso;			/* 0x0208  */
	u64	msl;			/* 0x0210  */
	u64	hbasce;			/* 0x0218  */
	u64	_0220;			/* 0x0220  */
	u64	gpto;			/* 0x0228  */
	u64	ic;			/* 0x0230  */
	u64	ec;			/* 0x0238  */
	u64	save_area;		/* 0x0240  */
	u64	_0248[7];		/* 0x0248  */
	u8	_0280[6];		/* 0x0280  */
	u16	lrcpua;			/* 0x0286  */
	u64	pstate;			/* 0x0288  */
	u64	pc;			/* 0x0290  */
	u64	sp_el0;			/* 0x0298  */
	u64	sp_el1;			/* 0x02a0  */
	u64	_02a8;			/* 0x02a8  */
	struct {			/* 0x02b0  */
		u32	_fpcr_res0;
		u32	fpcr;
	};
	struct {			/* 0x02b8  */
		u32	_fpsr_res0;
		u32	fpsr;
	};
	u16	sve_pregs[16];		/* 0x02c0  */
	u16	sve_ffr;		/* 0x02e0  */
	u8	_02e2[6];		/* 0x02e2  */
	u64	_02e8[3];		/* 0x02e8  */

	u64	gpr[31];		/* 0x0300  */
	u64	_03f8;			/* 0x03f8  */

	union {
		u64	icptd[8];		/* 0x0400 */
		/* validity-interception reason; icptr 0x01 */
		u16 vir;			/* 0x0400 */
		/* host access interception details; icptr 0x02 */
		struct {
			u64		esr_el2;	/* 0x0400 */
			u8		_0408[6];	/* 0x0408 */
			u16		pic;		/* 0x040e */
			union teid	teid;		/* 0x0410 */
			gva_t		far_el2;	/* 0x0418 */
			gva_t		vaddr;		/* 0x0420 */
			u64		suppl;		/* 0x0428 */
			u8		gltl;		/* 0x0430 */
			u8		_0431[7];	/* 0x0431 */
			u64		_0438;		/* 0x0438 */
		} hai;
		/* exception-interception details; icptr 0x03 */
		struct {
			u64	esr_el2;		/* 0x0400 */
			u64	_0408[2];		/* 0x0408 */
			gva_t	far_el2;		/* 0x0418 */
		} trap;
		/* timer-interception reason; icptr 0x04 */
#define SAE_IR_TIMER_ID_VIRT		BIT(6)
#define SAE_IR_TIMER_ID_PHYS		BIT(7)
		u8	tir;			/* 0x0400 */
	};
	u64	_0440[376];			/* 0x0440 */
} __packed __aligned(PAGE_SIZE);
static_assert(sizeof(struct kvm_sae_block) == PAGE_SIZE);

struct kvm_sae_save_area {
#define SAE_SAVE_AREA_FORMAT_0	0x00
	u8	saf;		/* 0x0000 */
	u8	_0001[5];	/* 0x0001 */
#define SAE_SAS_VALID		BIT(0)
	u16	sas;		/* 0x0006 */
	u64	sdo;		/* 0x0008 */
	u64	_0010[2];	/* 0x0010 */
	u64	regs[508];	/* 0x0020 */
} __packed __aligned(PAGE_SIZE);
static_assert(sizeof(struct kvm_sae_save_area) == PAGE_SIZE);

#endif /* ASM_KVM_HOST_ARM64_TYPES_H */
