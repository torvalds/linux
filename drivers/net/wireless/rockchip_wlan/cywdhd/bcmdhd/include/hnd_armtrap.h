/*
 * HND arm trap handling.
 *
 * Copyright (C) 1999-2017, Broadcom Corporation
 * 
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 * 
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 * 
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id: hnd_armtrap.h 514727 2014-11-12 03:02:48Z $
 */

#ifndef	_hnd_armtrap_h_
#define	_hnd_armtrap_h_


/* ARM trap handling */

/* Trap types defined by ARM (see arminc.h) */

/* Trap locations in lo memory */
#define	TRAP_STRIDE	4
#define FIRST_TRAP	TR_RST
#define LAST_TRAP	(TR_FIQ * TRAP_STRIDE)

#if defined(__ARM_ARCH_4T__)
#define	MAX_TRAP_TYPE	(TR_FIQ + 1)
#elif defined(__ARM_ARCH_7M__)
#define	MAX_TRAP_TYPE	(TR_ISR + ARMCM3_NUMINTS)
#endif	/* __ARM_ARCH_7M__ */

/* The trap structure is defined here as offsets for assembly */
#define	TR_TYPE		0x00
#define	TR_EPC		0x04
#define	TR_CPSR		0x08
#define	TR_SPSR		0x0c
#define	TR_REGS		0x10
#define	TR_REG(n)	(TR_REGS + (n) * 4)
#define	TR_SP		TR_REG(13)
#define	TR_LR		TR_REG(14)
#define	TR_PC		TR_REG(15)

#define	TRAP_T_SIZE	80
#define ASSERT_TRAP_SVC_NUMBER	255

#ifndef	_LANGUAGE_ASSEMBLY

#include <typedefs.h>

typedef struct _trap_struct {
	uint32		type;
	uint32		epc;
	uint32		cpsr;
	uint32		spsr;
	uint32		r0;	/* a1 */
	uint32		r1;	/* a2 */
	uint32		r2;	/* a3 */
	uint32		r3;	/* a4 */
	uint32		r4;	/* v1 */
	uint32		r5;	/* v2 */
	uint32		r6;	/* v3 */
	uint32		r7;	/* v4 */
	uint32		r8;	/* v5 */
	uint32		r9;	/* sb/v6 */
	uint32		r10;	/* sl/v7 */
	uint32		r11;	/* fp/v8 */
	uint32		r12;	/* ip */
	uint32		r13;	/* sp */
	uint32		r14;	/* lr */
	uint32		pc;	/* r15 */
} trap_t;

#endif	/* !_LANGUAGE_ASSEMBLY */

#endif	/* _hnd_armtrap_h_ */
