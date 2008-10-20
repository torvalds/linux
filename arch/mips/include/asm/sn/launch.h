/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2000 by Colin Ngam
 */
#ifndef _ASM_SN_LAUNCH_H
#define _ASM_SN_LAUNCH_H

#include <asm/sn/types.h>
#include <asm/sn/addrs.h>

/*
 * The launch data structure resides at a fixed place in each node's memory
 * and is used to communicate between the master processor and the slave
 * processors.
 *
 * The master stores launch parameters in the launch structure
 * corresponding to a target processor that is in a slave loop, then sends
 * an interrupt to the slave processor.  The slave calls the desired
 * function, then returns to the slave loop.  The master may poll or wait
 * for the slaves to finish.
 *
 * There is an array of launch structures, one per CPU on the node.  One
 * interrupt level is used per local CPU.
 */

#define LAUNCH_MAGIC		0xaddbead2addbead3
#ifdef CONFIG_SGI_IP27
#define LAUNCH_SIZEOF		0x100
#define LAUNCH_PADSZ		0xa0
#endif

#define LAUNCH_OFF_MAGIC	0x00	/* Struct offsets for assembly      */
#define LAUNCH_OFF_BUSY		0x08
#define LAUNCH_OFF_CALL		0x10
#define LAUNCH_OFF_CALLC	0x18
#define LAUNCH_OFF_CALLPARM	0x20
#define LAUNCH_OFF_STACK	0x28
#define LAUNCH_OFF_GP		0x30
#define LAUNCH_OFF_BEVUTLB	0x38
#define LAUNCH_OFF_BEVNORMAL	0x40
#define LAUNCH_OFF_BEVECC	0x48

#define LAUNCH_STATE_DONE	0	/* Return value of LAUNCH_POLL      */
#define LAUNCH_STATE_SENT	1
#define LAUNCH_STATE_RECD	2

/*
 * The launch routine is called only if the complement address is correct.
 *
 * Before control is transferred to a routine, the complement address
 * is zeroed (invalidated) to prevent an accidental call from a spurious
 * interrupt.
 *
 * The slave_launch routine turns on the BUSY flag, and the slave loop
 * clears the BUSY flag after control is returned to it.
 */

#ifndef __ASSEMBLY__

typedef int launch_state_t;
typedef void (*launch_proc_t)(u64 call_parm);

typedef struct launch_s {
	volatile u64		magic;	/* Magic number                     */
	volatile u64		busy;	/* Slave currently active           */
	volatile launch_proc_t	call_addr;	/* Func. for slave to call  */
	volatile u64		call_addr_c;	/* 1's complement of call_addr*/
	volatile u64		call_parm;	/* Single parm passed to call*/
	volatile void *stack_addr;	/* Stack pointer for slave function */
	volatile void *gp_addr;		/* Global pointer for slave func.   */
	volatile char 		*bevutlb;/* Address of bev utlb ex handler   */
	volatile char 		*bevnormal;/*Address of bev normal ex handler */
	volatile char 		*bevecc;/* Address of bev cache err handler */
	volatile char		pad[160];	/* Pad to LAUNCH_SIZEOF	    */
} launch_t;

/*
 * PROM entry points for launch routines are determined by IPxxprom/start.s
 */

#define LAUNCH_SLAVE	(*(void (*)(int nasid, int cpu, \
				    launch_proc_t call_addr, \
				    u64 call_parm, \
				    void *stack_addr, \
				    void *gp_addr)) \
			 IP27PROM_LAUNCHSLAVE)

#define LAUNCH_WAIT	(*(void (*)(int nasid, int cpu, int timeout_msec)) \
			 IP27PROM_WAITSLAVE)

#define LAUNCH_POLL	(*(launch_state_t (*)(int nasid, int cpu)) \
			 IP27PROM_POLLSLAVE)

#define LAUNCH_LOOP	(*(void (*)(void)) \
			 IP27PROM_SLAVELOOP)

#define LAUNCH_FLASH	(*(void (*)(void)) \
			 IP27PROM_FLASHLEDS)

#endif /* !__ASSEMBLY__ */

#endif /* _ASM_SN_LAUNCH_H */
