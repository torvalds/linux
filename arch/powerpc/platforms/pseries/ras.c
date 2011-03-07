/*
 * Copyright (C) 2001 Dave Engebretsen IBM Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

/* Change Activity:
 * 2001/09/21 : engebret : Created with minimal EPOW and HW exception support.
 * End Change Activity
 */

#include <linux/errno.h>
#include <linux/threads.h>
#include <linux/kernel_stat.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/timex.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/random.h>
#include <linux/sysrq.h>
#include <linux/bitops.h>

#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/irq.h>
#include <asm/cache.h>
#include <asm/prom.h>
#include <asm/ptrace.h>
#include <asm/machdep.h>
#include <asm/rtas.h>
#include <asm/udbg.h>
#include <asm/firmware.h>

#include "pseries.h"

static unsigned char ras_log_buf[RTAS_ERROR_LOG_MAX];
static DEFINE_SPINLOCK(ras_log_buf_lock);

static char global_mce_data_buf[RTAS_ERROR_LOG_MAX];
static DEFINE_PER_CPU(__u64, mce_data_buf);

static int ras_get_sensor_state_token;
static int ras_check_exception_token;

#define EPOW_SENSOR_TOKEN	9
#define EPOW_SENSOR_INDEX	0

static irqreturn_t ras_epow_interrupt(int irq, void *dev_id);
static irqreturn_t ras_error_interrupt(int irq, void *dev_id);


/*
 * Initialize handlers for the set of interrupts caused by hardware errors
 * and power system events.
 */
static int __init init_ras_IRQ(void)
{
	struct device_node *np;

	ras_get_sensor_state_token = rtas_token("get-sensor-state");
	ras_check_exception_token = rtas_token("check-exception");

	/* Internal Errors */
	np = of_find_node_by_path("/event-sources/internal-errors");
	if (np != NULL) {
		request_event_sources_irqs(np, ras_error_interrupt,
					   "RAS_ERROR");
		of_node_put(np);
	}

	/* EPOW Events */
	np = of_find_node_by_path("/event-sources/epow-events");
	if (np != NULL) {
		request_event_sources_irqs(np, ras_epow_interrupt, "RAS_EPOW");
		of_node_put(np);
	}

	return 0;
}
__initcall(init_ras_IRQ);

/*
 * Handle power subsystem events (EPOW).
 *
 * Presently we just log the event has occurred.  This should be fixed
 * to examine the type of power failure and take appropriate action where
 * the time horizon permits something useful to be done.
 */
static irqreturn_t ras_epow_interrupt(int irq, void *dev_id)
{
	int status = 0xdeadbeef;
	int state = 0;
	int critical;

	status = rtas_call(ras_get_sensor_state_token, 2, 2, &state,
			   EPOW_SENSOR_TOKEN, EPOW_SENSOR_INDEX);

	if (state > 3)
		critical = 1;  /* Time Critical */
	else
		critical = 0;

	spin_lock(&ras_log_buf_lock);

	status = rtas_call(ras_check_exception_token, 6, 1, NULL,
			   RTAS_VECTOR_EXTERNAL_INTERRUPT,
			   irq_map[irq].hwirq,
			   RTAS_EPOW_WARNING | RTAS_POWERMGM_EVENTS,
			   critical, __pa(&ras_log_buf),
				rtas_get_error_log_max());

	udbg_printf("EPOW <0x%lx 0x%x 0x%x>\n",
		    *((unsigned long *)&ras_log_buf), status, state);
	printk(KERN_WARNING "EPOW <0x%lx 0x%x 0x%x>\n",
	       *((unsigned long *)&ras_log_buf), status, state);

	/* format and print the extended information */
	log_error(ras_log_buf, ERR_TYPE_RTAS_LOG, 0);

	spin_unlock(&ras_log_buf_lock);
	return IRQ_HANDLED;
}

/*
 * Handle hardware error interrupts.
 *
 * RTAS check-exception is called to collect data on the exception.  If
 * the error is deemed recoverable, we log a warning and return.
 * For nonrecoverable errors, an error is logged and we stop all processing
 * as quickly as possible in order to prevent propagation of the failure.
 */
static irqreturn_t ras_error_interrupt(int irq, void *dev_id)
{
	struct rtas_error_log *rtas_elog;
	int status = 0xdeadbeef;
	int fatal;

	spin_lock(&ras_log_buf_lock);

	status = rtas_call(ras_check_exception_token, 6, 1, NULL,
			   RTAS_VECTOR_EXTERNAL_INTERRUPT,
			   irq_map[irq].hwirq,
			   RTAS_INTERNAL_ERROR, 1 /*Time Critical */,
			   __pa(&ras_log_buf),
				rtas_get_error_log_max());

	rtas_elog = (struct rtas_error_log *)ras_log_buf;

	if ((status == 0) && (rtas_elog->severity >= RTAS_SEVERITY_ERROR_SYNC))
		fatal = 1;
	else
		fatal = 0;

	/* format and print the extended information */
	log_error(ras_log_buf, ERR_TYPE_RTAS_LOG, fatal);

	if (fatal) {
		udbg_printf("Fatal HW Error <0x%lx 0x%x>\n",
			    *((unsigned long *)&ras_log_buf), status);
		printk(KERN_EMERG "Error: Fatal hardware error <0x%lx 0x%x>\n",
		       *((unsigned long *)&ras_log_buf), status);

#ifndef DEBUG_RTAS_POWER_OFF
		/* Don't actually power off when debugging so we can test
		 * without actually failing while injecting errors.
		 * Error data will not be logged to syslog.
		 */
		ppc_md.power_off();
#endif
	} else {
		udbg_printf("Recoverable HW Error <0x%lx 0x%x>\n",
			    *((unsigned long *)&ras_log_buf), status);
		printk(KERN_WARNING
		       "Warning: Recoverable hardware error <0x%lx 0x%x>\n",
		       *((unsigned long *)&ras_log_buf), status);
	}

	spin_unlock(&ras_log_buf_lock);
	return IRQ_HANDLED;
}

/*
 * Some versions of FWNMI place the buffer inside the 4kB page starting at
 * 0x7000. Other versions place it inside the rtas buffer. We check both.
 */
#define VALID_FWNMI_BUFFER(A) \
	((((A) >= 0x7000) && ((A) < 0x7ff0)) || \
	(((A) >= rtas.base) && ((A) < (rtas.base + rtas.size - 16))))

/*
 * Get the error information for errors coming through the
 * FWNMI vectors.  The pt_regs' r3 will be updated to reflect
 * the actual r3 if possible, and a ptr to the error log entry
 * will be returned if found.
 *
 * If the RTAS error is not of the extended type, then we put it in a per
 * cpu 64bit buffer. If it is the extended type we use global_mce_data_buf.
 *
 * The global_mce_data_buf does not have any locks or protection around it,
 * if a second machine check comes in, or a system reset is done
 * before we have logged the error, then we will get corruption in the
 * error log.  This is preferable over holding off on calling
 * ibm,nmi-interlock which would result in us checkstopping if a
 * second machine check did come in.
 */
static struct rtas_error_log *fwnmi_get_errinfo(struct pt_regs *regs)
{
	unsigned long *savep;
	struct rtas_error_log *h, *errhdr = NULL;

	if (!VALID_FWNMI_BUFFER(regs->gpr[3])) {
		printk(KERN_ERR "FWNMI: corrupt r3\n");
		return NULL;
	}

	savep = __va(regs->gpr[3]);
	regs->gpr[3] = savep[0];	/* restore original r3 */

	/* If it isn't an extended log we can use the per cpu 64bit buffer */
	h = (struct rtas_error_log *)&savep[1];
	if (!h->extended) {
		memcpy(&__get_cpu_var(mce_data_buf), h, sizeof(__u64));
		errhdr = (struct rtas_error_log *)&__get_cpu_var(mce_data_buf);
	} else {
		int len;

		len = max_t(int, 8+h->extended_log_length, RTAS_ERROR_LOG_MAX);
		memset(global_mce_data_buf, 0, RTAS_ERROR_LOG_MAX);
		memcpy(global_mce_data_buf, h, len);
		errhdr = (struct rtas_error_log *)global_mce_data_buf;
	}

	return errhdr;
}

/* Call this when done with the data returned by FWNMI_get_errinfo.
 * It will release the saved data area for other CPUs in the
 * partition to receive FWNMI errors.
 */
static void fwnmi_release_errinfo(void)
{
	int ret = rtas_call(rtas_token("ibm,nmi-interlock"), 0, 1, NULL);
	if (ret != 0)
		printk(KERN_ERR "FWNMI: nmi-interlock failed: %d\n", ret);
}

int pSeries_system_reset_exception(struct pt_regs *regs)
{
	if (fwnmi_active) {
		struct rtas_error_log *errhdr = fwnmi_get_errinfo(regs);
		if (errhdr) {
			/* XXX Should look at FWNMI information */
		}
		fwnmi_release_errinfo();
	}
	return 0; /* need to perform reset */
}

/*
 * See if we can recover from a machine check exception.
 * This is only called on power4 (or above) and only via
 * the Firmware Non-Maskable Interrupts (fwnmi) handler
 * which provides the error analysis for us.
 *
 * Return 1 if corrected (or delivered a signal).
 * Return 0 if there is nothing we can do.
 */
static int recover_mce(struct pt_regs *regs, struct rtas_error_log *err)
{
	int recovered = 0;

	if (!(regs->msr & MSR_RI)) {
		/* If MSR_RI isn't set, we cannot recover */
		recovered = 0;

	} else if (err->disposition == RTAS_DISP_FULLY_RECOVERED) {
		/* Platform corrected itself */
		recovered = 1;

	} else if (err->disposition == RTAS_DISP_LIMITED_RECOVERY) {
		/* Platform corrected itself but could be degraded */
		printk(KERN_ERR "MCE: limited recovery, system may "
		       "be degraded\n");
		recovered = 1;

	} else if (user_mode(regs) && !is_global_init(current) &&
		   err->severity == RTAS_SEVERITY_ERROR_SYNC) {

		/*
		 * If we received a synchronous error when in userspace
		 * kill the task. Firmware may report details of the fail
		 * asynchronously, so we can't rely on the target and type
		 * fields being valid here.
		 */
		printk(KERN_ERR "MCE: uncorrectable error, killing task "
		       "%s:%d\n", current->comm, current->pid);

		_exception(SIGBUS, regs, BUS_MCEERR_AR, regs->nip);
		recovered = 1;
	}

	log_error((char *)err, ERR_TYPE_RTAS_LOG, 0);

	return recovered;
}

/*
 * Handle a machine check.
 *
 * Note that on Power 4 and beyond Firmware Non-Maskable Interrupts (fwnmi)
 * should be present.  If so the handler which called us tells us if the
 * error was recovered (never true if RI=0).
 *
 * On hardware prior to Power 4 these exceptions were asynchronous which
 * means we can't tell exactly where it occurred and so we can't recover.
 */
int pSeries_machine_check_exception(struct pt_regs *regs)
{
	struct rtas_error_log *errp;

	if (fwnmi_active) {
		errp = fwnmi_get_errinfo(regs);
		fwnmi_release_errinfo();
		if (errp && recover_mce(regs, errp))
			return 1;
	}

	return 0;
}
