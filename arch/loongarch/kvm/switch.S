/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2023 Loongson Technology Corporation Limited
 */

#include <linux/linkage.h>
#include <asm/asm.h>
#include <asm/asmmacro.h>
#include <asm/loongarch.h>
#include <asm/regdef.h>
#include <asm/stackframe.h>

#define HGPR_OFFSET(x)		(PT_R0 + 8*x)
#define GGPR_OFFSET(x)		(KVM_ARCH_GGPR + 8*x)

.macro kvm_save_host_gpr base
	.irp n,1,2,3,22,23,24,25,26,27,28,29,30,31
	st.d	$r\n, \base, HGPR_OFFSET(\n)
	.endr
.endm

.macro kvm_restore_host_gpr base
	.irp n,1,2,3,22,23,24,25,26,27,28,29,30,31
	ld.d	$r\n, \base, HGPR_OFFSET(\n)
	.endr
.endm

/*
 * Save and restore all GPRs except base register,
 * and default value of base register is a2.
 */
.macro kvm_save_guest_gprs base
	.irp n,1,2,3,4,5,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
	st.d	$r\n, \base, GGPR_OFFSET(\n)
	.endr
.endm

.macro kvm_restore_guest_gprs base
	.irp n,1,2,3,4,5,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
	ld.d	$r\n, \base, GGPR_OFFSET(\n)
	.endr
.endm

/*
 * Prepare switch to guest, save host regs and restore guest regs.
 * a2: kvm_vcpu_arch, don't touch it until 'ertn'
 * t0, t1: temp register
 */
.macro kvm_switch_to_guest
	/* Set host ECFG.VS=0, all exceptions share one exception entry */
	csrrd		t0, LOONGARCH_CSR_ECFG
	bstrins.w	t0, zero, CSR_ECFG_VS_SHIFT_END, CSR_ECFG_VS_SHIFT
	csrwr		t0, LOONGARCH_CSR_ECFG

	/* Load up the new EENTRY */
	ld.d	t0, a2, KVM_ARCH_GEENTRY
	csrwr	t0, LOONGARCH_CSR_EENTRY

	/* Set Guest ERA */
	ld.d	t0, a2, KVM_ARCH_GPC
	csrwr	t0, LOONGARCH_CSR_ERA

	/* Save host PGDL */
	csrrd	t0, LOONGARCH_CSR_PGDL
	st.d	t0, a2, KVM_ARCH_HPGD

	/* Switch to kvm */
	ld.d	t1, a2, KVM_VCPU_KVM - KVM_VCPU_ARCH

	/* Load guest PGDL */
	li.w    t0, KVM_GPGD
	ldx.d   t0, t1, t0
	csrwr	t0, LOONGARCH_CSR_PGDL

	/* Mix GID and RID */
	csrrd		t1, LOONGARCH_CSR_GSTAT
	bstrpick.w	t1, t1, CSR_GSTAT_GID_SHIFT_END, CSR_GSTAT_GID_SHIFT
	csrrd		t0, LOONGARCH_CSR_GTLBC
	bstrins.w	t0, t1, CSR_GTLBC_TGID_SHIFT_END, CSR_GTLBC_TGID_SHIFT
	csrwr		t0, LOONGARCH_CSR_GTLBC

	/*
	 * Enable intr in root mode with future ertn so that host interrupt
	 * can be responsed during VM runs
	 * Guest CRMD comes from separate GCSR_CRMD register
	 */
	ori	t0, zero, CSR_PRMD_PIE
	csrxchg	t0, t0,   LOONGARCH_CSR_PRMD

	/* Set PVM bit to setup ertn to guest context */
	ori	t0, zero, CSR_GSTAT_PVM
	csrxchg	t0, t0,   LOONGARCH_CSR_GSTAT

	/* Load Guest GPRs */
	kvm_restore_guest_gprs a2
	/* Load KVM_ARCH register */
	ld.d	a2, a2,	(KVM_ARCH_GGPR + 8 * REG_A2)

	ertn /* Switch to guest: GSTAT.PGM = 1, ERRCTL.ISERR = 0, TLBRPRMD.ISTLBR = 0 */
.endm

	/*
	 * Exception entry for general exception from guest mode
	 *  - IRQ is disabled
	 *  - kernel privilege in root mode
	 *  - page mode keep unchanged from previous PRMD in root mode
	 *  - Fixme: tlb exception cannot happen since registers relative with TLB
	 *  -        is still in guest mode, such as pgd table/vmid registers etc,
	 *  -        will fix with hw page walk enabled in future
	 * load kvm_vcpu from reserved CSR KVM_VCPU_KS, and save a2 to KVM_TEMP_KS
	 */
	.text
	.cfi_sections	.debug_frame
SYM_CODE_START(kvm_exc_entry)
	csrwr	a2,   KVM_TEMP_KS
	csrrd	a2,   KVM_VCPU_KS
	addi.d	a2,   a2, KVM_VCPU_ARCH

	/* After save GPRs, free to use any GPR */
	kvm_save_guest_gprs a2
	/* Save guest A2 */
	csrrd	t0,	KVM_TEMP_KS
	st.d	t0,	a2,	(KVM_ARCH_GGPR + 8 * REG_A2)

	/* A2 is kvm_vcpu_arch, A1 is free to use */
	csrrd	s1,   KVM_VCPU_KS
	ld.d	s0,   s1, KVM_VCPU_RUN

	csrrd	t0,   LOONGARCH_CSR_ESTAT
	st.d	t0,   a2, KVM_ARCH_HESTAT
	csrrd	t0,   LOONGARCH_CSR_ERA
	st.d	t0,   a2, KVM_ARCH_GPC
	csrrd	t0,   LOONGARCH_CSR_BADV
	st.d	t0,   a2, KVM_ARCH_HBADV
	csrrd	t0,   LOONGARCH_CSR_BADI
	st.d	t0,   a2, KVM_ARCH_HBADI

	/* Restore host ECFG.VS */
	csrrd	t0, LOONGARCH_CSR_ECFG
	ld.d	t1, a2, KVM_ARCH_HECFG
	or	t0, t0, t1
	csrwr	t0, LOONGARCH_CSR_ECFG

	/* Restore host EENTRY */
	ld.d	t0, a2, KVM_ARCH_HEENTRY
	csrwr	t0, LOONGARCH_CSR_EENTRY

	/* Restore host pgd table */
	ld.d    t0, a2, KVM_ARCH_HPGD
	csrwr   t0, LOONGARCH_CSR_PGDL

	/*
	 * Disable PGM bit to enter root mode by default with next ertn
	 */
	ori	t0, zero, CSR_GSTAT_PVM
	csrxchg	zero, t0, LOONGARCH_CSR_GSTAT

	/*
	 * Clear GTLBC.TGID field
	 *       0: for root  tlb update in future tlb instr
	 *  others: for guest tlb update like gpa to hpa in future tlb instr
	 */
	csrrd	t0, LOONGARCH_CSR_GTLBC
	bstrins.w	t0, zero, CSR_GTLBC_TGID_SHIFT_END, CSR_GTLBC_TGID_SHIFT
	csrwr	t0, LOONGARCH_CSR_GTLBC
	ld.d	tp, a2, KVM_ARCH_HTP
	ld.d	sp, a2, KVM_ARCH_HSP
	/* restore per cpu register */
	ld.d	u0, a2, KVM_ARCH_HPERCPU
	addi.d	sp, sp, -PT_SIZE

	/* Prepare handle exception */
	or	a0, s0, zero
	or	a1, s1, zero
	ld.d	t8, a2, KVM_ARCH_HANDLE_EXIT
	jirl	ra, t8, 0

	or	a2, s1, zero
	addi.d	a2, a2, KVM_VCPU_ARCH

	/* Resume host when ret <= 0 */
	blez	a0, ret_to_host

	/*
         * Return to guest
         * Save per cpu register again, maybe switched to another cpu
         */
	st.d	u0, a2, KVM_ARCH_HPERCPU

	/* Save kvm_vcpu to kscratch */
	csrwr	s1, KVM_VCPU_KS
	kvm_switch_to_guest

ret_to_host:
	ld.d    a2, a2, KVM_ARCH_HSP
	addi.d  a2, a2, -PT_SIZE
	kvm_restore_host_gpr    a2
	jr      ra

SYM_INNER_LABEL(kvm_exc_entry_end, SYM_L_LOCAL)
SYM_CODE_END(kvm_exc_entry)

/*
 * int kvm_enter_guest(struct kvm_run *run, struct kvm_vcpu *vcpu)
 *
 * @register_param:
 *  a0: kvm_run* run
 *  a1: kvm_vcpu* vcpu
 */
SYM_FUNC_START(kvm_enter_guest)
	/* Allocate space in stack bottom */
	addi.d	a2, sp, -PT_SIZE
	/* Save host GPRs */
	kvm_save_host_gpr a2

	/* Save host CRMD, PRMD to stack */
	csrrd	a3, LOONGARCH_CSR_CRMD
	st.d	a3, a2, PT_CRMD
	csrrd	a3, LOONGARCH_CSR_PRMD
	st.d	a3, a2, PT_PRMD

	addi.d	a2, a1, KVM_VCPU_ARCH
	st.d	sp, a2, KVM_ARCH_HSP
	st.d	tp, a2, KVM_ARCH_HTP
	/* Save per cpu register */
	st.d	u0, a2, KVM_ARCH_HPERCPU

	/* Save kvm_vcpu to kscratch */
	csrwr	a1, KVM_VCPU_KS
	kvm_switch_to_guest
SYM_INNER_LABEL(kvm_enter_guest_end, SYM_L_LOCAL)
SYM_FUNC_END(kvm_enter_guest)

SYM_FUNC_START(kvm_save_fpu)
	fpu_save_csr	a0 t1
	fpu_save_double a0 t1
	fpu_save_cc	a0 t1 t2
	jr              ra
SYM_FUNC_END(kvm_save_fpu)

SYM_FUNC_START(kvm_restore_fpu)
	fpu_restore_double a0 t1
	fpu_restore_csr    a0 t1 t2
	fpu_restore_cc	   a0 t1 t2
	jr                 ra
SYM_FUNC_END(kvm_restore_fpu)

	.section ".rodata"
SYM_DATA(kvm_exception_size, .quad kvm_exc_entry_end - kvm_exc_entry)
SYM_DATA(kvm_enter_guest_size, .quad kvm_enter_guest_end - kvm_enter_guest)
