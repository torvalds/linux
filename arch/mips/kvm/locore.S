/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Main entry point for the guest, exception handling.
 *
 * Copyright (C) 2012  MIPS Technologies, Inc.  All rights reserved.
 * Authors: Sanjay Lal <sanjayl@kymasys.com>
 */

#include <asm/asm.h>
#include <asm/asmmacro.h>
#include <asm/regdef.h>
#include <asm/mipsregs.h>
#include <asm/stackframe.h>
#include <asm/asm-offsets.h>

#define _C_LABEL(x)     x
#define MIPSX(name)     mips32_ ## name
#define CALLFRAME_SIZ   32

/*
 * VECTOR
 *  exception vector entrypoint
 */
#define VECTOR(x, regmask)      \
    .ent    _C_LABEL(x),0;      \
    EXPORT(x);

#define VECTOR_END(x)      \
    EXPORT(x);

/* Overload, Danger Will Robinson!! */
#define PT_HOST_ASID        PT_BVADDR
#define PT_HOST_USERLOCAL   PT_EPC

#define CP0_DDATA_LO        $28,3
#define CP0_EBASE           $15,1

#define CP0_INTCTL          $12,1
#define CP0_SRSCTL          $12,2
#define CP0_SRSMAP          $12,3
#define CP0_HWRENA          $7,0

/* Resume Flags */
#define RESUME_FLAG_HOST        (1<<1)  /* Resume host? */

#define RESUME_GUEST            0
#define RESUME_HOST             RESUME_FLAG_HOST

/*
 * __kvm_mips_vcpu_run: entry point to the guest
 * a0: run
 * a1: vcpu
 */
	.set	noreorder
	.set	noat

FEXPORT(__kvm_mips_vcpu_run)
	/* k0/k1 not being used in host kernel context */
	INT_ADDIU k1, sp, -PT_SIZE
	LONG_S	$0, PT_R0(k1)
	LONG_S	$1, PT_R1(k1)
	LONG_S	$2, PT_R2(k1)
	LONG_S	$3, PT_R3(k1)

	LONG_S	$4, PT_R4(k1)
	LONG_S	$5, PT_R5(k1)
	LONG_S	$6, PT_R6(k1)
	LONG_S	$7, PT_R7(k1)

	LONG_S	$8,  PT_R8(k1)
	LONG_S	$9,  PT_R9(k1)
	LONG_S	$10, PT_R10(k1)
	LONG_S	$11, PT_R11(k1)
	LONG_S	$12, PT_R12(k1)
	LONG_S	$13, PT_R13(k1)
	LONG_S	$14, PT_R14(k1)
	LONG_S	$15, PT_R15(k1)
	LONG_S	$16, PT_R16(k1)
	LONG_S	$17, PT_R17(k1)

	LONG_S	$18, PT_R18(k1)
	LONG_S	$19, PT_R19(k1)
	LONG_S	$20, PT_R20(k1)
	LONG_S	$21, PT_R21(k1)
	LONG_S	$22, PT_R22(k1)
	LONG_S	$23, PT_R23(k1)
	LONG_S	$24, PT_R24(k1)
	LONG_S	$25, PT_R25(k1)

	/*
	 * XXXKYMA k0/k1 not saved, not being used if we got here through
	 * an ioctl()
	 */

	LONG_S	$28, PT_R28(k1)
	LONG_S	$29, PT_R29(k1)
	LONG_S	$30, PT_R30(k1)
	LONG_S	$31, PT_R31(k1)

	/* Save hi/lo */
	mflo	v0
	LONG_S	v0, PT_LO(k1)
	mfhi	v1
	LONG_S	v1, PT_HI(k1)

	/* Save host status */
	mfc0	v0, CP0_STATUS
	LONG_S	v0, PT_STATUS(k1)

	/* Save host ASID, shove it into the BVADDR location */
	mfc0	v1, CP0_ENTRYHI
	andi	v1, 0xff
	LONG_S	v1, PT_HOST_ASID(k1)

	/* Save DDATA_LO, will be used to store pointer to vcpu */
	mfc0	v1, CP0_DDATA_LO
	LONG_S	v1, PT_HOST_USERLOCAL(k1)

	/* DDATA_LO has pointer to vcpu */
	mtc0	a1, CP0_DDATA_LO

	/* Offset into vcpu->arch */
	INT_ADDIU k1, a1, VCPU_HOST_ARCH

	/*
	 * Save the host stack to VCPU, used for exception processing
	 * when we exit from the Guest
	 */
	LONG_S	sp, VCPU_HOST_STACK(k1)

	/* Save the kernel gp as well */
	LONG_S	gp, VCPU_HOST_GP(k1)

	/*
	 * Setup status register for running the guest in UM, interrupts
	 * are disabled
	 */
	li	k0, (ST0_EXL | KSU_USER | ST0_BEV)
	mtc0	k0, CP0_STATUS
	ehb

	/* load up the new EBASE */
	LONG_L	k0, VCPU_GUEST_EBASE(k1)
	mtc0	k0, CP0_EBASE

	/*
	 * Now that the new EBASE has been loaded, unset BEV, set
	 * interrupt mask as it was but make sure that timer interrupts
	 * are enabled
	 */
	li	k0, (ST0_EXL | KSU_USER | ST0_IE)
	andi	v0, v0, ST0_IM
	or	k0, k0, v0
	mtc0	k0, CP0_STATUS
	ehb

	/* Set Guest EPC */
	LONG_L	t0, VCPU_PC(k1)
	mtc0	t0, CP0_EPC

FEXPORT(__kvm_mips_load_asid)
	/* Set the ASID for the Guest Kernel */
	INT_SLL	t0, t0, 1	/* with kseg0 @ 0x40000000, kernel */
			        /* addresses shift to 0x80000000 */
	bltz	t0, 1f		/* If kernel */
	 INT_ADDIU t1, k1, VCPU_GUEST_KERNEL_ASID  /* (BD)  */
	INT_ADDIU t1, k1, VCPU_GUEST_USER_ASID    /* else user */
1:
	/* t1: contains the base of the ASID array, need to get the cpu id */
	LONG_L	t2, TI_CPU($28)             /* smp_processor_id */
	INT_SLL	t2, t2, 2                   /* x4 */
	REG_ADDU t3, t1, t2
	LONG_L	k0, (t3)
	andi	k0, k0, 0xff
	mtc0	k0, CP0_ENTRYHI
	ehb

	/* Disable RDHWR access */
	mtc0	zero, CP0_HWRENA

	/* Now load up the Guest Context from VCPU */
	LONG_L	$1, VCPU_R1(k1)
	LONG_L	$2, VCPU_R2(k1)
	LONG_L	$3, VCPU_R3(k1)

	LONG_L	$4, VCPU_R4(k1)
	LONG_L	$5, VCPU_R5(k1)
	LONG_L	$6, VCPU_R6(k1)
	LONG_L	$7, VCPU_R7(k1)

	LONG_L	$8, VCPU_R8(k1)
	LONG_L	$9, VCPU_R9(k1)
	LONG_L	$10, VCPU_R10(k1)
	LONG_L	$11, VCPU_R11(k1)
	LONG_L	$12, VCPU_R12(k1)
	LONG_L	$13, VCPU_R13(k1)
	LONG_L	$14, VCPU_R14(k1)
	LONG_L	$15, VCPU_R15(k1)
	LONG_L	$16, VCPU_R16(k1)
	LONG_L	$17, VCPU_R17(k1)
	LONG_L	$18, VCPU_R18(k1)
	LONG_L	$19, VCPU_R19(k1)
	LONG_L	$20, VCPU_R20(k1)
	LONG_L	$21, VCPU_R21(k1)
	LONG_L	$22, VCPU_R22(k1)
	LONG_L	$23, VCPU_R23(k1)
	LONG_L	$24, VCPU_R24(k1)
	LONG_L	$25, VCPU_R25(k1)

	/* k0/k1 loaded up later */

	LONG_L	$28, VCPU_R28(k1)
	LONG_L	$29, VCPU_R29(k1)
	LONG_L	$30, VCPU_R30(k1)
	LONG_L	$31, VCPU_R31(k1)

	/* Restore hi/lo */
	LONG_L	k0, VCPU_LO(k1)
	mtlo	k0

	LONG_L	k0, VCPU_HI(k1)
	mthi	k0

FEXPORT(__kvm_mips_load_k0k1)
	/* Restore the guest's k0/k1 registers */
	LONG_L	k0, VCPU_R26(k1)
	LONG_L	k1, VCPU_R27(k1)

	/* Jump to guest */
	eret

VECTOR(MIPSX(exception), unknown)
/* Find out what mode we came from and jump to the proper handler. */
	mtc0	k0, CP0_ERROREPC	#01: Save guest k0
	ehb				#02:

	mfc0	k0, CP0_EBASE		#02: Get EBASE
	INT_SRL	k0, k0, 10		#03: Get rid of CPUNum
	INT_SLL	k0, k0, 10		#04
	LONG_S	k1, 0x3000(k0)		#05: Save k1 @ offset 0x3000
	INT_ADDIU k0, k0, 0x2000	#06: Exception handler is
					#    installed @ offset 0x2000
	j	k0			#07: jump to the function
	 nop				#08: branch delay slot
VECTOR_END(MIPSX(exceptionEnd))
.end MIPSX(exception)

/*
 * Generic Guest exception handler. We end up here when the guest
 * does something that causes a trap to kernel mode.
 */
NESTED (MIPSX(GuestException), CALLFRAME_SIZ, ra)
	/* Get the VCPU pointer from DDTATA_LO */
	mfc0	k1, CP0_DDATA_LO
	INT_ADDIU k1, k1, VCPU_HOST_ARCH

	/* Start saving Guest context to VCPU */
	LONG_S	$0, VCPU_R0(k1)
	LONG_S	$1, VCPU_R1(k1)
	LONG_S	$2, VCPU_R2(k1)
	LONG_S	$3, VCPU_R3(k1)
	LONG_S	$4, VCPU_R4(k1)
	LONG_S	$5, VCPU_R5(k1)
	LONG_S	$6, VCPU_R6(k1)
	LONG_S	$7, VCPU_R7(k1)
	LONG_S	$8, VCPU_R8(k1)
	LONG_S	$9, VCPU_R9(k1)
	LONG_S	$10, VCPU_R10(k1)
	LONG_S	$11, VCPU_R11(k1)
	LONG_S	$12, VCPU_R12(k1)
	LONG_S	$13, VCPU_R13(k1)
	LONG_S	$14, VCPU_R14(k1)
	LONG_S	$15, VCPU_R15(k1)
	LONG_S	$16, VCPU_R16(k1)
	LONG_S	$17, VCPU_R17(k1)
	LONG_S	$18, VCPU_R18(k1)
	LONG_S	$19, VCPU_R19(k1)
	LONG_S	$20, VCPU_R20(k1)
	LONG_S	$21, VCPU_R21(k1)
	LONG_S	$22, VCPU_R22(k1)
	LONG_S	$23, VCPU_R23(k1)
	LONG_S	$24, VCPU_R24(k1)
	LONG_S	$25, VCPU_R25(k1)

	/* Guest k0/k1 saved later */

	LONG_S	$28, VCPU_R28(k1)
	LONG_S	$29, VCPU_R29(k1)
	LONG_S	$30, VCPU_R30(k1)
	LONG_S	$31, VCPU_R31(k1)

	/* We need to save hi/lo and restore them on the way out */
	mfhi	t0
	LONG_S	t0, VCPU_HI(k1)

	mflo	t0
	LONG_S	t0, VCPU_LO(k1)

	/* Finally save guest k0/k1 to VCPU */
	mfc0	t0, CP0_ERROREPC
	LONG_S	t0, VCPU_R26(k1)

	/* Get GUEST k1 and save it in VCPU */
	PTR_LI	t1, ~0x2ff
	mfc0	t0, CP0_EBASE
	and	t0, t0, t1
	LONG_L	t0, 0x3000(t0)
	LONG_S	t0, VCPU_R27(k1)

	/* Now that context has been saved, we can use other registers */

	/* Restore vcpu */
	mfc0	a1, CP0_DDATA_LO
	move	s1, a1

	/* Restore run (vcpu->run) */
	LONG_L	a0, VCPU_RUN(a1)
	/* Save pointer to run in s0, will be saved by the compiler */
	move	s0, a0

	/*
	 * Save Host level EPC, BadVaddr and Cause to VCPU, useful to
	 * process the exception
	 */
	mfc0	k0,CP0_EPC
	LONG_S	k0, VCPU_PC(k1)

	mfc0	k0, CP0_BADVADDR
	LONG_S	k0, VCPU_HOST_CP0_BADVADDR(k1)

	mfc0	k0, CP0_CAUSE
	LONG_S	k0, VCPU_HOST_CP0_CAUSE(k1)

	mfc0	k0, CP0_ENTRYHI
	LONG_S	k0, VCPU_HOST_ENTRYHI(k1)

	/* Now restore the host state just enough to run the handlers */

	/* Swtich EBASE to the one used by Linux */
	/* load up the host EBASE */
	mfc0	v0, CP0_STATUS

	.set	at
	or	k0, v0, ST0_BEV
	.set	noat

	mtc0	k0, CP0_STATUS
	ehb

	LONG_L	k0, VCPU_HOST_EBASE(k1)
	mtc0	k0,CP0_EBASE

	/* Now that the new EBASE has been loaded, unset BEV and KSU_USER */
	.set	at
	and	v0, v0, ~(ST0_EXL | KSU_USER | ST0_IE)
	or	v0, v0, ST0_CU0
	.set	noat
	mtc0	v0, CP0_STATUS
	ehb

	/* Load up host GP */
	LONG_L	gp, VCPU_HOST_GP(k1)

	/* Need a stack before we can jump to "C" */
	LONG_L	sp, VCPU_HOST_STACK(k1)

	/* Saved host state */
	INT_ADDIU sp, sp, -PT_SIZE

	/*
	 * XXXKYMA do we need to load the host ASID, maybe not because the
	 * kernel entries are marked GLOBAL, need to verify
	 */

	/* Restore host DDATA_LO */
	LONG_L	k0, PT_HOST_USERLOCAL(sp)
	mtc0	k0, CP0_DDATA_LO

	/* Restore RDHWR access */
	PTR_LI	k0, 0x2000000F
	mtc0	k0, CP0_HWRENA

	/* Jump to handler */
FEXPORT(__kvm_mips_jump_to_handler)
	/*
	 * XXXKYMA: not sure if this is safe, how large is the stack??
	 * Now jump to the kvm_mips_handle_exit() to see if we can deal
	 * with this in the kernel
	 */
	PTR_LA	t9, kvm_mips_handle_exit
	jalr.hb	t9
	 INT_ADDIU sp, sp, -CALLFRAME_SIZ           /* BD Slot */

	/* Return from handler Make sure interrupts are disabled */
	di
	ehb

	/*
	 * XXXKYMA: k0/k1 could have been blown away if we processed
	 * an exception while we were handling the exception from the
	 * guest, reload k1
	 */

	move	k1, s1
	INT_ADDIU k1, k1, VCPU_HOST_ARCH

	/*
	 * Check return value, should tell us if we are returning to the
	 * host (handle I/O etc)or resuming the guest
	 */
	andi	t0, v0, RESUME_HOST
	bnez	t0, __kvm_mips_return_to_host
	 nop

__kvm_mips_return_to_guest:
	/* Put the saved pointer to vcpu (s1) back into the DDATA_LO Register */
	mtc0	s1, CP0_DDATA_LO

	/* Load up the Guest EBASE to minimize the window where BEV is set */
	LONG_L	t0, VCPU_GUEST_EBASE(k1)

	/* Switch EBASE back to the one used by KVM */
	mfc0	v1, CP0_STATUS
	.set	at
	or	k0, v1, ST0_BEV
	.set	noat
	mtc0	k0, CP0_STATUS
	ehb
	mtc0	t0, CP0_EBASE

	/* Setup status register for running guest in UM */
	.set	at
	or	v1, v1, (ST0_EXL | KSU_USER | ST0_IE)
	and	v1, v1, ~ST0_CU0
	.set	noat
	mtc0	v1, CP0_STATUS
	ehb

	/* Set Guest EPC */
	LONG_L	t0, VCPU_PC(k1)
	mtc0	t0, CP0_EPC

	/* Set the ASID for the Guest Kernel */
	INT_SLL	t0, t0, 1	/* with kseg0 @ 0x40000000, kernel */
				/* addresses shift to 0x80000000 */
	bltz	t0, 1f		/* If kernel */
	 INT_ADDIU t1, k1, VCPU_GUEST_KERNEL_ASID  /* (BD)  */
	INT_ADDIU t1, k1, VCPU_GUEST_USER_ASID    /* else user */
1:
	/* t1: contains the base of the ASID array, need to get the cpu id  */
	LONG_L	t2, TI_CPU($28)		/* smp_processor_id */
	INT_SLL	t2, t2, 2		/* x4 */
	REG_ADDU t3, t1, t2
	LONG_L	k0, (t3)
	andi	k0, k0, 0xff
	mtc0	k0,CP0_ENTRYHI
	ehb

	/* Disable RDHWR access */
	mtc0    zero,  CP0_HWRENA

	/* load the guest context from VCPU and return */
	LONG_L	$0, VCPU_R0(k1)
	LONG_L	$1, VCPU_R1(k1)
	LONG_L	$2, VCPU_R2(k1)
	LONG_L	$3, VCPU_R3(k1)
	LONG_L	$4, VCPU_R4(k1)
	LONG_L	$5, VCPU_R5(k1)
	LONG_L	$6, VCPU_R6(k1)
	LONG_L	$7, VCPU_R7(k1)
	LONG_L	$8, VCPU_R8(k1)
	LONG_L	$9, VCPU_R9(k1)
	LONG_L	$10, VCPU_R10(k1)
	LONG_L	$11, VCPU_R11(k1)
	LONG_L	$12, VCPU_R12(k1)
	LONG_L	$13, VCPU_R13(k1)
	LONG_L	$14, VCPU_R14(k1)
	LONG_L	$15, VCPU_R15(k1)
	LONG_L	$16, VCPU_R16(k1)
	LONG_L	$17, VCPU_R17(k1)
	LONG_L	$18, VCPU_R18(k1)
	LONG_L	$19, VCPU_R19(k1)
	LONG_L	$20, VCPU_R20(k1)
	LONG_L	$21, VCPU_R21(k1)
	LONG_L	$22, VCPU_R22(k1)
	LONG_L	$23, VCPU_R23(k1)
	LONG_L	$24, VCPU_R24(k1)
	LONG_L	$25, VCPU_R25(k1)

	/* $/k1 loaded later */
	LONG_L	$28, VCPU_R28(k1)
	LONG_L	$29, VCPU_R29(k1)
	LONG_L	$30, VCPU_R30(k1)
	LONG_L	$31, VCPU_R31(k1)

FEXPORT(__kvm_mips_skip_guest_restore)
	LONG_L	k0, VCPU_HI(k1)
	mthi	k0

	LONG_L	k0, VCPU_LO(k1)
	mtlo	k0

	LONG_L	k0, VCPU_R26(k1)
	LONG_L	k1, VCPU_R27(k1)

	eret

__kvm_mips_return_to_host:
	/* EBASE is already pointing to Linux */
	LONG_L	k1, VCPU_HOST_STACK(k1)
	INT_ADDIU k1,k1, -PT_SIZE

	/* Restore host DDATA_LO */
	LONG_L	k0, PT_HOST_USERLOCAL(k1)
	mtc0	k0, CP0_DDATA_LO

	/* Restore host ASID */
	LONG_L	k0, PT_HOST_ASID(sp)
	andi	k0, 0xff
	mtc0	k0,CP0_ENTRYHI
	ehb

	/* Load context saved on the host stack */
	LONG_L	$0, PT_R0(k1)
	LONG_L	$1, PT_R1(k1)

	/*
	 * r2/v0 is the return code, shift it down by 2 (arithmetic)
	 * to recover the err code
	 */
	INT_SRA	k0, v0, 2
	move	$2, k0

	LONG_L	$3, PT_R3(k1)
	LONG_L	$4, PT_R4(k1)
	LONG_L	$5, PT_R5(k1)
	LONG_L	$6, PT_R6(k1)
	LONG_L	$7, PT_R7(k1)
	LONG_L	$8, PT_R8(k1)
	LONG_L	$9, PT_R9(k1)
	LONG_L	$10, PT_R10(k1)
	LONG_L	$11, PT_R11(k1)
	LONG_L	$12, PT_R12(k1)
	LONG_L	$13, PT_R13(k1)
	LONG_L	$14, PT_R14(k1)
	LONG_L	$15, PT_R15(k1)
	LONG_L	$16, PT_R16(k1)
	LONG_L	$17, PT_R17(k1)
	LONG_L	$18, PT_R18(k1)
	LONG_L	$19, PT_R19(k1)
	LONG_L	$20, PT_R20(k1)
	LONG_L	$21, PT_R21(k1)
	LONG_L	$22, PT_R22(k1)
	LONG_L	$23, PT_R23(k1)
	LONG_L	$24, PT_R24(k1)
	LONG_L	$25, PT_R25(k1)

	/* Host k0/k1 were not saved */

	LONG_L	$28, PT_R28(k1)
	LONG_L	$29, PT_R29(k1)
	LONG_L	$30, PT_R30(k1)

	LONG_L	k0, PT_HI(k1)
	mthi	k0

	LONG_L	k0, PT_LO(k1)
	mtlo	k0

	/* Restore RDHWR access */
	PTR_LI	k0, 0x2000000F
	mtc0	k0,  CP0_HWRENA

	/* Restore RA, which is the address we will return to */
	LONG_L  ra, PT_R31(k1)
	j       ra
	 nop

VECTOR_END(MIPSX(GuestExceptionEnd))
.end MIPSX(GuestException)

MIPSX(exceptions):
	####
	##### The exception handlers.
	#####
	.word _C_LABEL(MIPSX(GuestException))	#  0
	.word _C_LABEL(MIPSX(GuestException))	#  1
	.word _C_LABEL(MIPSX(GuestException))	#  2
	.word _C_LABEL(MIPSX(GuestException))	#  3
	.word _C_LABEL(MIPSX(GuestException))	#  4
	.word _C_LABEL(MIPSX(GuestException))	#  5
	.word _C_LABEL(MIPSX(GuestException))	#  6
	.word _C_LABEL(MIPSX(GuestException))	#  7
	.word _C_LABEL(MIPSX(GuestException))	#  8
	.word _C_LABEL(MIPSX(GuestException))	#  9
	.word _C_LABEL(MIPSX(GuestException))	# 10
	.word _C_LABEL(MIPSX(GuestException))	# 11
	.word _C_LABEL(MIPSX(GuestException))	# 12
	.word _C_LABEL(MIPSX(GuestException))	# 13
	.word _C_LABEL(MIPSX(GuestException))	# 14
	.word _C_LABEL(MIPSX(GuestException))	# 15
	.word _C_LABEL(MIPSX(GuestException))	# 16
	.word _C_LABEL(MIPSX(GuestException))	# 17
	.word _C_LABEL(MIPSX(GuestException))	# 18
	.word _C_LABEL(MIPSX(GuestException))	# 19
	.word _C_LABEL(MIPSX(GuestException))	# 20
	.word _C_LABEL(MIPSX(GuestException))	# 21
	.word _C_LABEL(MIPSX(GuestException))	# 22
	.word _C_LABEL(MIPSX(GuestException))	# 23
	.word _C_LABEL(MIPSX(GuestException))	# 24
	.word _C_LABEL(MIPSX(GuestException))	# 25
	.word _C_LABEL(MIPSX(GuestException))	# 26
	.word _C_LABEL(MIPSX(GuestException))	# 27
	.word _C_LABEL(MIPSX(GuestException))	# 28
	.word _C_LABEL(MIPSX(GuestException))	# 29
	.word _C_LABEL(MIPSX(GuestException))	# 30
	.word _C_LABEL(MIPSX(GuestException))	# 31
