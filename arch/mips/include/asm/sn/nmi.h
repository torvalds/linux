/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Derived from IRIX <sys/SN/nmi.h>, Revision 1.5.
 *
 * Copyright (C) 1992 - 1997 Silicon Graphics, Inc.
 */
#ifndef __ASM_SN_NMI_H
#define __ASM_SN_NMI_H

#include <asm/sn/addrs.h>

/*
 * The launch data structure resides at a fixed place in each node's memory
 * and is used to communicate between the master processor and the slave
 * processors.
 *
 * The master stores launch parameters in the launch structure
 * corresponding to a target processor that is in a slave loop, then sends
 * an interrupt to the slave processor.	 The slave calls the desired
 * function, followed by an optional rendezvous function, then returns to
 * the slave loop.  The master does not wait for the slaves before
 * returning.
 *
 * There is an array of launch structures, one per CPU on the node.  One
 * interrupt level is used per CPU.
 */

#define NMI_MAGIC		0x48414d4d455201
#define NMI_SIZEOF		0x40

#define NMI_OFF_MAGIC		0x00	/* Struct offsets for assembly	    */
#define NMI_OFF_FLAGS		0x08
#define NMI_OFF_CALL		0x10
#define NMI_OFF_CALLC		0x18
#define NMI_OFF_CALLPARM	0x20
#define NMI_OFF_GMASTER		0x28

/*
 * The NMI routine is called only if the complement address is
 * correct.
 *
 * Before control is transferred to a routine, the complement address
 * is zeroed (invalidated) to prevent an accidental call from a spurious
 * interrupt.
 *
 */

#ifndef __ASSEMBLY__

typedef struct nmi_s {
	volatile unsigned long	 magic;		/* Magic number */
	volatile unsigned long	 flags;		/* Combination of flags above */
	volatile void *call_addr;	/* Routine for slave to call	    */
	volatile void *call_addr_c;	/* 1's complement of address	    */
	volatile void *call_parm;	/* Single parm passed to call	    */
	volatile unsigned long	 gmaster;	/* Flag true only on global master*/
} nmi_t;

#endif /* !__ASSEMBLY__ */

/* Following definitions are needed both in the prom & the kernel
 * to identify the format of the nmi cpu register save area in the
 * low memory on each node.
 */
#ifndef __ASSEMBLY__

struct reg_struct {
	unsigned long	gpr[32];
	unsigned long	sr;
	unsigned long	cause;
	unsigned long	epc;
	unsigned long	badva;
	unsigned long	error_epc;
	unsigned long	cache_err;
	unsigned long	nmi_sr;
};

#endif /* !__ASSEMBLY__ */

/* These are the assembly language offsets into the reg_struct structure */

#define R0_OFF		0x0
#define R1_OFF		0x8
#define R2_OFF		0x10
#define R3_OFF		0x18
#define R4_OFF		0x20
#define R5_OFF		0x28
#define R6_OFF		0x30
#define R7_OFF		0x38
#define R8_OFF		0x40
#define R9_OFF		0x48
#define R10_OFF		0x50
#define R11_OFF		0x58
#define R12_OFF		0x60
#define R13_OFF		0x68
#define R14_OFF		0x70
#define R15_OFF		0x78
#define R16_OFF		0x80
#define R17_OFF		0x88
#define R18_OFF		0x90
#define R19_OFF		0x98
#define R20_OFF		0xa0
#define R21_OFF		0xa8
#define R22_OFF		0xb0
#define R23_OFF		0xb8
#define R24_OFF		0xc0
#define R25_OFF		0xc8
#define R26_OFF		0xd0
#define R27_OFF		0xd8
#define R28_OFF		0xe0
#define R29_OFF		0xe8
#define R30_OFF		0xf0
#define R31_OFF		0xf8
#define SR_OFF		0x100
#define CAUSE_OFF	0x108
#define EPC_OFF		0x110
#define BADVA_OFF	0x118
#define ERROR_EPC_OFF	0x120
#define CACHE_ERR_OFF	0x128
#define NMISR_OFF	0x130

#endif /* __ASM_SN_NMI_H */
