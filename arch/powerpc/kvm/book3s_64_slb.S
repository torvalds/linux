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
 * Copyright SUSE Linux Products GmbH 2009
 *
 * Authors: Alexander Graf <agraf@suse.de>
 */

#define SHADOW_SLB_ESID(num)	(SLBSHADOW_SAVEAREA + (num * 0x10))
#define SHADOW_SLB_VSID(num)	(SLBSHADOW_SAVEAREA + (num * 0x10) + 0x8)
#define UNBOLT_SLB_ENTRY(num) \
	li	r11, SHADOW_SLB_ESID(num);	\
	LDX_BE	r9, r12, r11;			\
	/* Invalid? Skip. */;			\
	rldicl. r0, r9, 37, 63;			\
	beq	slb_entry_skip_ ## num;		\
	xoris	r9, r9, SLB_ESID_V@h;		\
	STDX_BE	r9, r12, r11;			\
  slb_entry_skip_ ## num:

#define REBOLT_SLB_ENTRY(num) \
	li	r8, SHADOW_SLB_ESID(num);	\
	li	r7, SHADOW_SLB_VSID(num);	\
	LDX_BE	r10, r11, r8;			\
	cmpdi	r10, 0;				\
	beq	slb_exit_skip_ ## num;		\
	oris	r10, r10, SLB_ESID_V@h;		\
	LDX_BE	r9, r11, r7;			\
	slbmte	r9, r10;			\
	STDX_BE	r10, r11, r8;			\
slb_exit_skip_ ## num:

/******************************************************************************
 *                                                                            *
 *                               Entry code                                   *
 *                                                                            *
 *****************************************************************************/

.macro LOAD_GUEST_SEGMENTS

	/* Required state:
	 *
	 * MSR = ~IR|DR
	 * R13 = PACA
	 * R1 = host R1
	 * R2 = host R2
	 * R3 = shadow vcpu
	 * all other volatile GPRS = free except R4, R6
	 * SVCPU[CR]  = guest CR
	 * SVCPU[XER] = guest XER
	 * SVCPU[CTR] = guest CTR
	 * SVCPU[LR]  = guest LR
	 */

	/* Remove LPAR shadow entries */

#if SLB_NUM_BOLTED == 3

	ld	r12, PACA_SLBSHADOWPTR(r13)

	/* Remove bolted entries */
	UNBOLT_SLB_ENTRY(0)
	UNBOLT_SLB_ENTRY(1)
	UNBOLT_SLB_ENTRY(2)
	
#else
#error unknown number of bolted entries
#endif

	/* Flush SLB */

	li	r10, 0
	slbmte	r10, r10
	slbia

	/* Fill SLB with our shadow */

	lbz	r12, SVCPU_SLB_MAX(r3)
	mulli	r12, r12, 16
	addi	r12, r12, SVCPU_SLB
	add	r12, r12, r3

	/* for (r11 = kvm_slb; r11 < kvm_slb + kvm_slb_size; r11+=slb_entry) */
	li	r11, SVCPU_SLB
	add	r11, r11, r3

slb_loop_enter:

	ld	r10, 0(r11)

	rldicl. r0, r10, 37, 63
	beq	slb_loop_enter_skip

	ld	r9, 8(r11)
	slbmte	r9, r10

slb_loop_enter_skip:
	addi	r11, r11, 16
	cmpd	cr0, r11, r12
	blt	slb_loop_enter

slb_do_enter:

.endm

/******************************************************************************
 *                                                                            *
 *                               Exit code                                    *
 *                                                                            *
 *****************************************************************************/

.macro LOAD_HOST_SEGMENTS

	/* Register usage at this point:
	 *
	 * R1         = host R1
	 * R2         = host R2
	 * R12        = exit handler id
	 * R13        = shadow vcpu - SHADOW_VCPU_OFF [=PACA on PPC64]
	 * SVCPU.*    = guest *
	 * SVCPU[CR]  = guest CR
	 * SVCPU[XER] = guest XER
	 * SVCPU[CTR] = guest CTR
	 * SVCPU[LR]  = guest LR
	 *
	 */

	/* Restore bolted entries from the shadow and fix it along the way */

	li	r0, r0
	slbmte	r0, r0
	slbia
	isync

#if SLB_NUM_BOLTED == 3

	ld	r11, PACA_SLBSHADOWPTR(r13)

	REBOLT_SLB_ENTRY(0)
	REBOLT_SLB_ENTRY(1)
	REBOLT_SLB_ENTRY(2)
	
#else
#error unknown number of bolted entries
#endif

slb_do_exit:

.endm
