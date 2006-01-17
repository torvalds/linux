/*
 *  drivers/s390/char/sclp_quiesce.c
 *     signal quiesce handler
 *
 *  (C) Copyright IBM Corp. 1999,2004
 *  Author(s): Martin Schwidefsky <schwidefsky@de.ibm.com>
 *             Peter Oberparleiter <peter.oberparleiter@de.ibm.com>
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/cpumask.h>
#include <linux/smp.h>
#include <linux/init.h>
#include <asm/atomic.h>
#include <asm/ptrace.h>
#include <asm/sigp.h>

#include "sclp.h"


#ifdef CONFIG_SMP
/* Signal completion of shutdown process. All CPUs except the first to enter
 * this function: go to stopped state. First CPU: wait until all other
 * CPUs are in stopped or check stop state. Afterwards, load special PSW
 * to indicate completion. */
static void
do_load_quiesce_psw(void * __unused)
{
	static atomic_t cpuid = ATOMIC_INIT(-1);
	psw_t quiesce_psw;
	int cpu;

	if (atomic_cmpxchg(&cpuid, -1, smp_processor_id()) != -1)
		signal_processor(smp_processor_id(), sigp_stop);
	/* Wait for all other cpus to enter stopped state */
	for_each_online_cpu(cpu) {
		if (cpu == smp_processor_id())
			continue;
		while(!smp_cpu_not_running(cpu))
			cpu_relax();
	}
	/* Quiesce the last cpu with the special psw */
	quiesce_psw.mask = PSW_BASE_BITS | PSW_MASK_WAIT;
	quiesce_psw.addr = 0xfff;
	__load_psw(quiesce_psw);
}

/* Shutdown handler. Perform shutdown function on all CPUs. */
static void
do_machine_quiesce(void)
{
	on_each_cpu(do_load_quiesce_psw, NULL, 0, 0);
}
#else
/* Shutdown handler. Signal completion of shutdown by loading special PSW. */
static void
do_machine_quiesce(void)
{
	psw_t quiesce_psw;

	quiesce_psw.mask = PSW_BASE_BITS | PSW_MASK_WAIT;
	quiesce_psw.addr = 0xfff;
	__load_psw(quiesce_psw);
}
#endif

extern void ctrl_alt_del(void);

/* Handler for quiesce event. Start shutdown procedure. */
static void
sclp_quiesce_handler(struct evbuf_header *evbuf)
{
	_machine_restart = (void *) do_machine_quiesce;
	_machine_halt = do_machine_quiesce;
	_machine_power_off = do_machine_quiesce;
	ctrl_alt_del();
}

static struct sclp_register sclp_quiesce_event = {
	.receive_mask = EvTyp_SigQuiesce_Mask,
	.receiver_fn = sclp_quiesce_handler
};

/* Initialize quiesce driver. */
static int __init
sclp_quiesce_init(void)
{
	int rc;

	rc = sclp_register(&sclp_quiesce_event);
	if (rc)
		printk(KERN_WARNING "sclp: could not register quiesce handler "
		       "(rc=%d)\n", rc);
	return rc;
}

module_init(sclp_quiesce_init);
