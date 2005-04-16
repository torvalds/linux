/* 
 * arch/sh/boards/saturn/smp.c
 *
 * SMP support for the Sega Saturn.
 *
 * Copyright (c) 2002 Paul Mundt
 *
 * Released under the terms of the GNU GPL v2.0.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/smp.h>

#include <asm/saturn/smpc.h>

extern void start_secondary(void);

void __smp_send_ipi(unsigned int cpu, unsigned int action)
{
	/* Nothing here yet .. */
}

unsigned int __smp_probe_cpus(void)
{
	/*
	 * This is just a straightforward master/slave configuration,
	 * and probing isn't really supported..
	 */
	return 2;
}

/*
 * We're only allowed to do byte-access to SMPC registers. In
 * addition to which, we treat them as write-only, since
 * reading from them will return undefined data.
 */
static inline void smpc_slave_stop(unsigned int cpu)
{
	smpc_barrier();
	ctrl_outb(1, SMPC_STATUS);

	ctrl_outb(SMPC_CMD_SSHOFF, SMPC_COMMAND);
	smpc_barrier();
}

static inline void smpc_slave_start(unsigned int cpu)
{
	ctrl_outb(1, SMPC_STATUS);
	ctrl_outb(SMPC_CMD_SSHON, SMPC_COMMAND);

	smpc_barrier();
}

void __smp_slave_init(unsigned int cpu)
{
	register unsigned long vbr;
	void **entry;

	__asm__ __volatile__ ("stc vbr, %0\n\t" : "=r" (vbr));
	entry = (void **)(vbr + 0x310 + 0x94);

	smpc_slave_stop(cpu);

	*(void **)entry = (void *)start_secondary;

	smpc_slave_start(cpu);
}

