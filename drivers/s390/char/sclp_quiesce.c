/*
 *  drivers/s390/char/sclp_quiesce.c
 *     signal quiesce handler
 *
 *  (C) Copyright IBM Corp. 1999,2004
 *  Author(s): Martin Schwidefsky <schwidefsky@de.ibm.com>
 *             Peter Oberparleiter <peter.oberparleiter@de.ibm.com>
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/cpumask.h>
#include <linux/smp.h>
#include <linux/init.h>
#include <linux/reboot.h>
#include <asm/atomic.h>
#include <asm/ptrace.h>
#include <asm/sigp.h>
#include <asm/smp.h>

#include "sclp.h"

/* Shutdown handler. Signal completion of shutdown by loading special PSW. */
static void
do_machine_quiesce(void)
{
	psw_t quiesce_psw;

	smp_send_stop();
	quiesce_psw.mask = PSW_BASE_BITS | PSW_MASK_WAIT;
	quiesce_psw.addr = 0xfff;
	__load_psw(quiesce_psw);
}

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
