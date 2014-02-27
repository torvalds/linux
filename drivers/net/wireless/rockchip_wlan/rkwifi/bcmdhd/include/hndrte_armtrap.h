/*
 * HNDRTE arm trap handling.
 *
 * $Copyright Open Broadcom Corporation$
 *
 * $Id: hndrte_armtrap.h 261365 2011-05-24 20:42:23Z $
 */

#ifndef	_hndrte_armtrap_h
#define	_hndrte_armtrap_h


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

#endif	/* _hndrte_armtrap_h */
