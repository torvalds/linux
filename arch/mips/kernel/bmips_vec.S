/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2011 by Kevin Cernekee (cernekee@gmail.com)
 *
 * Reset/NMI/re-entry vectors for BMIPS processors
 */


#include <asm/asm.h>
#include <asm/asmmacro.h>
#include <asm/cacheops.h>
#include <asm/cpu.h>
#include <asm/regdef.h>
#include <asm/mipsregs.h>
#include <asm/stackframe.h>
#include <asm/addrspace.h>
#include <asm/hazards.h>
#include <asm/bmips.h>

	.macro	BARRIER
	.set	mips32
	_ssnop
	_ssnop
	_ssnop
	.set	mips0
	.endm

/***********************************************************************
 * Alternate CPU1 startup vector for BMIPS4350
 *
 * On some systems the bootloader has already started CPU1 and configured
 * it to resume execution at 0x8000_0200 (!BEV IV vector) when it is
 * triggered by the SW1 interrupt.  If that is the case we try to move
 * it to a more convenient place: BMIPS_WARM_RESTART_VEC @ 0x8000_0380.
 ***********************************************************************/

LEAF(bmips_smp_movevec)
	la	k0, 1f
	li	k1, CKSEG1
	or	k0, k1
	jr	k0

1:
	/* clear IV, pending IPIs */
	mtc0	zero, CP0_CAUSE

	/* re-enable IRQs to wait for SW1 */
	li	k0, ST0_IE | ST0_BEV | STATUSF_IP1
	mtc0	k0, CP0_STATUS

	/* set up CPU1 CBR; move BASE to 0xa000_0000 */
	li	k0, 0xff400000
	mtc0	k0, $22, 6
	/* set up relocation vector address based on thread ID */
	mfc0	k1, $22, 3
	srl	k1, 16
	andi	k1, 0x8000
	or	k1, CKSEG1 | BMIPS_RELO_VECTOR_CONTROL_0
	or	k0, k1
	li	k1, 0xa0080000
	sw	k1, 0(k0)

	/* wait here for SW1 interrupt from bmips_boot_secondary() */
	wait

	la	k0, bmips_reset_nmi_vec
	li	k1, CKSEG1
	or	k0, k1
	jr	k0
END(bmips_smp_movevec)

/***********************************************************************
 * Reset/NMI vector
 * For BMIPS processors that can relocate their exception vectors, this
 * entire function gets copied to 0x8000_0000.
 ***********************************************************************/

NESTED(bmips_reset_nmi_vec, PT_SIZE, sp)
	.set	push
	.set	noat
	.align	4

#ifdef CONFIG_SMP
	/* if the NMI bit is clear, assume this is a CPU1 reset instead */
	li	k1, (1 << 19)
	mfc0	k0, CP0_STATUS
	and	k0, k1
	beqz	k0, bmips_smp_entry

#if defined(CONFIG_CPU_BMIPS5000)
	mfc0	k0, CP0_PRID
	li	k1, PRID_IMP_BMIPS5000
	andi	k0, 0xff00
	bne	k0, k1, 1f

	/* if we're not on core 0, this must be the SMP boot signal */
	li	k1, (3 << 25)
	mfc0	k0, $22
	and	k0, k1
	bnez	k0, bmips_smp_entry
1:
#endif /* CONFIG_CPU_BMIPS5000 */
#endif /* CONFIG_SMP */

	/* nope, it's just a regular NMI */
	SAVE_ALL
	move	a0, sp

	/* clear EXL, ERL, BEV so that TLB refills still work */
	mfc0	k0, CP0_STATUS
	li	k1, ST0_ERL | ST0_EXL | ST0_BEV | ST0_IE
	or	k0, k1
	xor	k0, k1
	mtc0	k0, CP0_STATUS
	BARRIER

	/* jump to the NMI handler function */
	la	k0, nmi_handler
	jr	k0

	RESTORE_ALL
	.set	mips3
	eret

/***********************************************************************
 * CPU1 reset vector (used for the initial boot only)
 * This is still part of bmips_reset_nmi_vec().
 ***********************************************************************/

#ifdef CONFIG_SMP

bmips_smp_entry:

	/* set up CP0 STATUS; enable FPU */
	li	k0, 0x30000000
	mtc0	k0, CP0_STATUS
	BARRIER

	/* set local CP0 CONFIG to make kseg0 cacheable, write-back */
	mfc0	k0, CP0_CONFIG
	ori	k0, 0x07
	xori	k0, 0x04
	mtc0	k0, CP0_CONFIG

	mfc0	k0, CP0_PRID
	andi	k0, 0xff00
#if defined(CONFIG_CPU_BMIPS4350) || defined(CONFIG_CPU_BMIPS4380)
	li	k1, PRID_IMP_BMIPS43XX
	bne	k0, k1, 2f

	/* initialize CPU1's local I-cache */
	li	k0, 0x80000000
	li	k1, 0x80010000
	mtc0	zero, $28
	mtc0	zero, $28, 1
	BARRIER

1:	cache	Index_Store_Tag_I, 0(k0)
	addiu	k0, 16
	bne	k0, k1, 1b

	b	3f
2:
#endif /* CONFIG_CPU_BMIPS4350 || CONFIG_CPU_BMIPS4380 */
#if defined(CONFIG_CPU_BMIPS5000)
	/* set exception vector base */
	li	k1, PRID_IMP_BMIPS5000
	bne	k0, k1, 3f

	la	k0, ebase
	lw	k0, 0(k0)
	mtc0	k0, $15, 1
	BARRIER
#endif /* CONFIG_CPU_BMIPS5000 */
3:
	/* jump back to kseg0 in case we need to remap the kseg1 area */
	la	k0, 1f
	jr	k0
1:
	la	k0, bmips_enable_xks01
	jalr	k0

	/* use temporary stack to set up upper memory TLB */
	li	sp, BMIPS_WARM_RESTART_VEC
	la	k0, plat_wired_tlb_setup
	jalr	k0

	/* switch to permanent stack and continue booting */

	.global bmips_secondary_reentry
bmips_secondary_reentry:
	la	k0, bmips_smp_boot_sp
	lw	sp, 0(k0)
	la	k0, bmips_smp_boot_gp
	lw	gp, 0(k0)
	la	k0, start_secondary
	jr	k0

#endif /* CONFIG_SMP */

	.align	4
	.global bmips_reset_nmi_vec_end
bmips_reset_nmi_vec_end:

END(bmips_reset_nmi_vec)

	.set	pop
	.previous

/***********************************************************************
 * CPU1 warm restart vector (used for second and subsequent boots).
 * Also used for S2 standby recovery (PM).
 * This entire function gets copied to (BMIPS_WARM_RESTART_VEC)
 ***********************************************************************/

LEAF(bmips_smp_int_vec)

	.align	4
	mfc0	k0, CP0_STATUS
	ori	k0, 0x01
	xori	k0, 0x01
	mtc0	k0, CP0_STATUS
	eret

	.align	4
	.global bmips_smp_int_vec_end
bmips_smp_int_vec_end:

END(bmips_smp_int_vec)

/***********************************************************************
 * XKS01 support
 * Certain CPUs support extending kseg0 to 1024MB.
 ***********************************************************************/

LEAF(bmips_enable_xks01)

#if defined(CONFIG_XKS01)
	mfc0	t0, CP0_PRID
	andi	t2, t0, 0xff00
#if defined(CONFIG_CPU_BMIPS4380)
	li	t1, PRID_IMP_BMIPS43XX
	bne	t2, t1, 1f

	andi	t0, 0xff
	addiu	t1, t0, -PRID_REV_BMIPS4380_HI
	bgtz	t1, 2f
	addiu	t0, -PRID_REV_BMIPS4380_LO
	bltz	t0, 2f

	mfc0	t0, $22, 3
	li	t1, 0x1ff0
	li	t2, (1 << 12) | (1 << 9)
	or	t0, t1
	xor	t0, t1
	or	t0, t2
	mtc0	t0, $22, 3
	BARRIER
	b	2f
1:
#endif /* CONFIG_CPU_BMIPS4380 */
#if defined(CONFIG_CPU_BMIPS5000)
	li	t1, PRID_IMP_BMIPS5000
	bne	t2, t1, 2f

	mfc0	t0, $22, 5
	li	t1, 0x01ff
	li	t2, (1 << 8) | (1 << 5)
	or	t0, t1
	xor	t0, t1
	or	t0, t2
	mtc0	t0, $22, 5
	BARRIER
#endif /* CONFIG_CPU_BMIPS5000 */
2:
#endif /* defined(CONFIG_XKS01) */

	jr	ra

END(bmips_enable_xks01)

	.previous
