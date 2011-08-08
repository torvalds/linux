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
#include <linux/atomic.h>
#include <asm/ptrace.h>
#include <asm/sigp.h>
#include <asm/smp.h>

#include "sclp.h"

static void (*old_machine_restart)(char *);
static void (*old_machine_halt)(void);
static void (*old_machine_power_off)(void);

/* Shutdown handler. Signal completion of shutdown by loading special PSW. */
static void do_machine_quiesce(void)
{
	psw_t quiesce_psw;

	smp_send_stop();
	quiesce_psw.mask = PSW_BASE_BITS | PSW_MASK_WAIT;
	quiesce_psw.addr = 0xfff;
	__load_psw(quiesce_psw);
}

/* Handler for quiesce event. Start shutdown procedure. */
static void sclp_quiesce_handler(struct evbuf_header *evbuf)
{
	if (_machine_restart != (void *) do_machine_quiesce) {
		old_machine_restart = _machine_restart;
		old_machine_halt = _machine_halt;
		old_machine_power_off = _machine_power_off;
		_machine_restart = (void *) do_machine_quiesce;
		_machine_halt = do_machine_quiesce;
		_machine_power_off = do_machine_quiesce;
	}
	ctrl_alt_del();
}

/* Undo machine restart/halt/power_off modification on resume */
static void sclp_quiesce_pm_event(struct sclp_register *reg,
				  enum sclp_pm_event sclp_pm_event)
{
	switch (sclp_pm_event) {
	case SCLP_PM_EVENT_RESTORE:
		if (old_machine_restart) {
			_machine_restart = old_machine_restart;
			_machine_halt = old_machine_halt;
			_machine_power_off = old_machine_power_off;
			old_machine_restart = NULL;
			old_machine_halt = NULL;
			old_machine_power_off = NULL;
		}
		break;
	case SCLP_PM_EVENT_FREEZE:
	case SCLP_PM_EVENT_THAW:
		break;
	}
}

static struct sclp_register sclp_quiesce_event = {
	.receive_mask = EVTYP_SIGQUIESCE_MASK,
	.receiver_fn = sclp_quiesce_handler,
	.pm_event_fn = sclp_quiesce_pm_event
};

/* Initialize quiesce driver. */
static int __init sclp_quiesce_init(void)
{
	return sclp_register(&sclp_quiesce_event);
}

module_init(sclp_quiesce_init);
