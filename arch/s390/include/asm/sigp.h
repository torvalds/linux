/*
 *  Routines and structures for signalling other processors.
 *
 *    Copyright IBM Corp. 1999,2010
 *    Author(s): Denis Joseph Barrow,
 *		 Martin Schwidefsky <schwidefsky@de.ibm.com>,
 *		 Heiko Carstens <heiko.carstens@de.ibm.com>,
 */

#ifndef __ASM_SIGP_H
#define __ASM_SIGP_H

#include <asm/system.h>

/* Get real cpu address from logical cpu number. */
extern unsigned short __cpu_logical_map[];

static inline int cpu_logical_map(int cpu)
{
#ifdef CONFIG_SMP
	return __cpu_logical_map[cpu];
#else
	return stap();
#endif
}

enum {
	sigp_unassigned=0x0,
	sigp_sense,
	sigp_external_call,
	sigp_emergency_signal,
	sigp_start,
	sigp_stop,
	sigp_restart,
	sigp_unassigned1,
	sigp_unassigned2,
	sigp_stop_and_store_status,
	sigp_unassigned3,
	sigp_initial_cpu_reset,
	sigp_cpu_reset,
	sigp_set_prefix,
	sigp_store_status_at_address,
	sigp_store_extended_status_at_address
};

enum {
        sigp_order_code_accepted=0,
	sigp_status_stored,
	sigp_busy,
	sigp_not_operational
};

/*
 * Definitions for external call.
 */
enum {
	ec_schedule = 0,
	ec_call_function,
	ec_call_function_single,
	ec_bit_last
};

/*
 * Signal processor.
 */
static inline int raw_sigp(u16 cpu, int order)
{
	register unsigned long reg1 asm ("1") = 0;
	int ccode;

	asm volatile(
		"	sigp	%1,%2,0(%3)\n"
		"	ipm	%0\n"
		"	srl	%0,28\n"
		:	"=d"	(ccode)
		: "d" (reg1), "d" (cpu),
		  "a" (order) : "cc" , "memory");
	return ccode;
}

/*
 * Signal processor with parameter.
 */
static inline int raw_sigp_p(u32 parameter, u16 cpu, int order)
{
	register unsigned int reg1 asm ("1") = parameter;
	int ccode;

	asm volatile(
		"	sigp	%1,%2,0(%3)\n"
		"	ipm	%0\n"
		"	srl	%0,28\n"
		: "=d" (ccode)
		: "d" (reg1), "d" (cpu),
		  "a" (order) : "cc" , "memory");
	return ccode;
}

/*
 * Signal processor with parameter and return status.
 */
static inline int raw_sigp_ps(u32 *status, u32 parm, u16 cpu, int order)
{
	register unsigned int reg1 asm ("1") = parm;
	int ccode;

	asm volatile(
		"	sigp	%1,%2,0(%3)\n"
		"	ipm	%0\n"
		"	srl	%0,28\n"
		: "=d" (ccode), "+d" (reg1)
		: "d" (cpu), "a" (order)
		: "cc" , "memory");
	*status = reg1;
	return ccode;
}

static inline int sigp(int cpu, int order)
{
	return raw_sigp(cpu_logical_map(cpu), order);
}

static inline int sigp_p(u32 parameter, int cpu, int order)
{
	return raw_sigp_p(parameter, cpu_logical_map(cpu), order);
}

static inline int sigp_ps(u32 *status, u32 parm, int cpu, int order)
{
	return raw_sigp_ps(status, parm, cpu_logical_map(cpu), order);
}

#endif /* __ASM_SIGP_H */
