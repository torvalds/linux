// SPDX-License-Identifier: GPL-2.0
/*
 *   S/390 common I/O routines -- low level i/o calls
 *
 *    Copyright IBM Corp. 1999, 2008
 *    Author(s): Ingo Adlung (adlung@de.ibm.com)
 *		 Cornelia Huck (cornelia.huck@de.ibm.com)
 *		 Arnd Bergmann (arndb@de.ibm.com)
 *		 Martin Schwidefsky (schwidefsky@de.ibm.com)
 */

#define KMSG_COMPONENT "cio"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/ftrace.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/kernel_stat.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <asm/cio.h>
#include <asm/delay.h>
#include <asm/irq.h>
#include <asm/irq_regs.h>
#include <asm/setup.h>
#include <asm/ipl.h>
#include <asm/chpid.h>
#include <asm/airq.h>
#include <asm/isc.h>
#include <linux/sched/cputime.h>
#include <asm/fcx.h>
#include <asm/nmi.h>
#include <asm/crw.h>
#include "cio.h"
#include "css.h"
#include "chsc.h"
#include "ioasm.h"
#include "io_sch.h"
#include "blacklist.h"
#include "cio_debug.h"
#include "chp.h"
#include "trace.h"

debug_info_t *cio_debug_msg_id;
debug_info_t *cio_debug_trace_id;
debug_info_t *cio_debug_crw_id;

DEFINE_PER_CPU_ALIGNED(struct irb, cio_irb);
EXPORT_PER_CPU_SYMBOL(cio_irb);

/*
 * Function: cio_debug_init
 * Initializes three debug logs for common I/O:
 * - cio_msg logs generic cio messages
 * - cio_trace logs the calling of different functions
 * - cio_crw logs machine check related cio messages
 */
static int __init cio_debug_init(void)
{
	cio_debug_msg_id = debug_register("cio_msg", 16, 1, 11 * sizeof(long));
	if (!cio_debug_msg_id)
		goto out_unregister;
	debug_register_view(cio_debug_msg_id, &debug_sprintf_view);
	debug_set_level(cio_debug_msg_id, 2);
	cio_debug_trace_id = debug_register("cio_trace", 16, 1, 16);
	if (!cio_debug_trace_id)
		goto out_unregister;
	debug_register_view(cio_debug_trace_id, &debug_hex_ascii_view);
	debug_set_level(cio_debug_trace_id, 2);
	cio_debug_crw_id = debug_register("cio_crw", 8, 1, 8 * sizeof(long));
	if (!cio_debug_crw_id)
		goto out_unregister;
	debug_register_view(cio_debug_crw_id, &debug_sprintf_view);
	debug_set_level(cio_debug_crw_id, 4);
	return 0;

out_unregister:
	debug_unregister(cio_debug_msg_id);
	debug_unregister(cio_debug_trace_id);
	debug_unregister(cio_debug_crw_id);
	return -1;
}

arch_initcall (cio_debug_init);

int cio_set_options(struct subchannel *sch, int flags)
{
	struct io_subchannel_private *priv = to_io_private(sch);

	priv->options.suspend = (flags & DOIO_ALLOW_SUSPEND) != 0;
	priv->options.prefetch = (flags & DOIO_DENY_PREFETCH) != 0;
	priv->options.inter = (flags & DOIO_SUPPRESS_INTER) != 0;
	return 0;
}

static int
cio_start_handle_notoper(struct subchannel *sch, __u8 lpm)
{
	char dbf_text[15];

	if (lpm != 0)
		sch->lpm &= ~lpm;
	else
		sch->lpm = 0;

	CIO_MSG_EVENT(2, "cio_start: 'not oper' status for "
		      "subchannel 0.%x.%04x!\n", sch->schid.ssid,
		      sch->schid.sch_no);

	if (cio_update_schib(sch))
		return -ENODEV;

	sprintf(dbf_text, "no%s", dev_name(&sch->dev));
	CIO_TRACE_EVENT(0, dbf_text);
	CIO_HEX_EVENT(0, &sch->schib, sizeof (struct schib));

	return (sch->lpm ? -EACCES : -ENODEV);
}

int
cio_start_key (struct subchannel *sch,	/* subchannel structure */
	       struct ccw1 * cpa,	/* logical channel prog addr */
	       __u8 lpm,		/* logical path mask */
	       __u8 key)                /* storage key */
{
	struct io_subchannel_private *priv = to_io_private(sch);
	union orb *orb = &priv->orb;
	int ccode;

	CIO_TRACE_EVENT(5, "stIO");
	CIO_TRACE_EVENT(5, dev_name(&sch->dev));

	memset(orb, 0, sizeof(union orb));
	/* sch is always under 2G. */
	orb->cmd.intparm = (u32)virt_to_phys(sch);
	orb->cmd.fmt = 1;

	orb->cmd.pfch = priv->options.prefetch == 0;
	orb->cmd.spnd = priv->options.suspend;
	orb->cmd.ssic = priv->options.suspend && priv->options.inter;
	orb->cmd.lpm = (lpm != 0) ? lpm : sch->lpm;
	/*
	 * for 64 bit we always support 64 bit IDAWs with 4k page size only
	 */
	orb->cmd.c64 = 1;
	orb->cmd.i2k = 0;
	orb->cmd.key = key >> 4;
	/* issue "Start Subchannel" */
	orb->cmd.cpa = (u32)virt_to_phys(cpa);
	ccode = ssch(sch->schid, orb);

	/* process condition code */
	CIO_HEX_EVENT(5, &ccode, sizeof(ccode));

	switch (ccode) {
	case 0:
		/*
		 * initialize device status information
		 */
		sch->schib.scsw.cmd.actl |= SCSW_ACTL_START_PEND;
		return 0;
	case 1:		/* status pending */
	case 2:		/* busy */
		return -EBUSY;
	case 3:		/* device/path not operational */
		return cio_start_handle_notoper(sch, lpm);
	default:
		return ccode;
	}
}
EXPORT_SYMBOL_GPL(cio_start_key);

int
cio_start (struct subchannel *sch, struct ccw1 *cpa, __u8 lpm)
{
	return cio_start_key(sch, cpa, lpm, PAGE_DEFAULT_KEY);
}
EXPORT_SYMBOL_GPL(cio_start);

/*
 * resume suspended I/O operation
 */
int
cio_resume (struct subchannel *sch)
{
	int ccode;

	CIO_TRACE_EVENT(4, "resIO");
	CIO_TRACE_EVENT(4, dev_name(&sch->dev));

	ccode = rsch (sch->schid);

	CIO_HEX_EVENT(4, &ccode, sizeof(ccode));

	switch (ccode) {
	case 0:
		sch->schib.scsw.cmd.actl |= SCSW_ACTL_RESUME_PEND;
		return 0;
	case 1:
		return -EBUSY;
	case 2:
		return -EINVAL;
	default:
		/*
		 * useless to wait for request completion
		 *  as device is no longer operational !
		 */
		return -ENODEV;
	}
}
EXPORT_SYMBOL_GPL(cio_resume);

/*
 * halt I/O operation
 */
int
cio_halt(struct subchannel *sch)
{
	int ccode;

	if (!sch)
		return -ENODEV;

	CIO_TRACE_EVENT(2, "haltIO");
	CIO_TRACE_EVENT(2, dev_name(&sch->dev));

	/*
	 * Issue "Halt subchannel" and process condition code
	 */
	ccode = hsch (sch->schid);

	CIO_HEX_EVENT(2, &ccode, sizeof(ccode));

	switch (ccode) {
	case 0:
		sch->schib.scsw.cmd.actl |= SCSW_ACTL_HALT_PEND;
		return 0;
	case 1:		/* status pending */
	case 2:		/* busy */
		return -EBUSY;
	default:		/* device not operational */
		return -ENODEV;
	}
}
EXPORT_SYMBOL_GPL(cio_halt);

/*
 * Clear I/O operation
 */
int
cio_clear(struct subchannel *sch)
{
	int ccode;

	if (!sch)
		return -ENODEV;

	CIO_TRACE_EVENT(2, "clearIO");
	CIO_TRACE_EVENT(2, dev_name(&sch->dev));

	/*
	 * Issue "Clear subchannel" and process condition code
	 */
	ccode = csch (sch->schid);

	CIO_HEX_EVENT(2, &ccode, sizeof(ccode));

	switch (ccode) {
	case 0:
		sch->schib.scsw.cmd.actl |= SCSW_ACTL_CLEAR_PEND;
		return 0;
	default:		/* device not operational */
		return -ENODEV;
	}
}
EXPORT_SYMBOL_GPL(cio_clear);

/*
 * Function: cio_cancel
 * Issues a "Cancel Subchannel" on the specified subchannel
 * Note: We don't need any fancy intparms and flags here
 *	 since xsch is executed synchronously.
 * Only for common I/O internal use as for now.
 */
int
cio_cancel (struct subchannel *sch)
{
	int ccode;

	if (!sch)
		return -ENODEV;

	CIO_TRACE_EVENT(2, "cancelIO");
	CIO_TRACE_EVENT(2, dev_name(&sch->dev));

	ccode = xsch (sch->schid);

	CIO_HEX_EVENT(2, &ccode, sizeof(ccode));

	switch (ccode) {
	case 0:		/* success */
		/* Update information in scsw. */
		if (cio_update_schib(sch))
			return -ENODEV;
		return 0;
	case 1:		/* status pending */
		return -EBUSY;
	case 2:		/* not applicable */
		return -EINVAL;
	default:	/* not oper */
		return -ENODEV;
	}
}
EXPORT_SYMBOL_GPL(cio_cancel);

/**
 * cio_cancel_halt_clear - Cancel running I/O by performing cancel, halt
 * and clear ordinally if subchannel is valid.
 * @sch: subchannel on which to perform the cancel_halt_clear operation
 * @iretry: the number of the times remained to retry the next operation
 *
 * This should be called repeatedly since halt/clear are asynchronous
 * operations. We do one try with cio_cancel, three tries with cio_halt,
 * 255 tries with cio_clear. The caller should initialize @iretry with
 * the value 255 for its first call to this, and keep using the same
 * @iretry in the subsequent calls until it gets a non -EBUSY return.
 *
 * Returns 0 if device now idle, -ENODEV for device not operational,
 * -EBUSY if an interrupt is expected (either from halt/clear or from a
 * status pending), and -EIO if out of retries.
 */
int cio_cancel_halt_clear(struct subchannel *sch, int *iretry)
{
	int ret;

	if (cio_update_schib(sch))
		return -ENODEV;
	if (!sch->schib.pmcw.ena)
		/* Not operational -> done. */
		return 0;
	/* Stage 1: cancel io. */
	if (!(scsw_actl(&sch->schib.scsw) & SCSW_ACTL_HALT_PEND) &&
	    !(scsw_actl(&sch->schib.scsw) & SCSW_ACTL_CLEAR_PEND)) {
		if (!scsw_is_tm(&sch->schib.scsw)) {
			ret = cio_cancel(sch);
			if (ret != -EINVAL)
				return ret;
		}
		/*
		 * Cancel io unsuccessful or not applicable (transport mode).
		 * Continue with asynchronous instructions.
		 */
		*iretry = 3;	/* 3 halt retries. */
	}
	/* Stage 2: halt io. */
	if (!(scsw_actl(&sch->schib.scsw) & SCSW_ACTL_CLEAR_PEND)) {
		if (*iretry) {
			*iretry -= 1;
			ret = cio_halt(sch);
			if (ret != -EBUSY)
				return (ret == 0) ? -EBUSY : ret;
		}
		/* Halt io unsuccessful. */
		*iretry = 255;	/* 255 clear retries. */
	}
	/* Stage 3: clear io. */
	if (*iretry) {
		*iretry -= 1;
		ret = cio_clear(sch);
		return (ret == 0) ? -EBUSY : ret;
	}
	/* Function was unsuccessful */
	return -EIO;
}
EXPORT_SYMBOL_GPL(cio_cancel_halt_clear);

static void cio_apply_config(struct subchannel *sch, struct schib *schib)
{
	schib->pmcw.intparm = sch->config.intparm;
	schib->pmcw.mbi = sch->config.mbi;
	schib->pmcw.isc = sch->config.isc;
	schib->pmcw.ena = sch->config.ena;
	schib->pmcw.mme = sch->config.mme;
	schib->pmcw.mp = sch->config.mp;
	schib->pmcw.csense = sch->config.csense;
	schib->pmcw.mbfc = sch->config.mbfc;
	if (sch->config.mbfc)
		schib->mba = sch->config.mba;
}

static int cio_check_config(struct subchannel *sch, struct schib *schib)
{
	return (schib->pmcw.intparm == sch->config.intparm) &&
		(schib->pmcw.mbi == sch->config.mbi) &&
		(schib->pmcw.isc == sch->config.isc) &&
		(schib->pmcw.ena == sch->config.ena) &&
		(schib->pmcw.mme == sch->config.mme) &&
		(schib->pmcw.mp == sch->config.mp) &&
		(schib->pmcw.csense == sch->config.csense) &&
		(schib->pmcw.mbfc == sch->config.mbfc) &&
		(!sch->config.mbfc || (schib->mba == sch->config.mba));
}

/*
 * cio_commit_config - apply configuration to the subchannel
 */
int cio_commit_config(struct subchannel *sch)
{
	int ccode, retry, ret = 0;
	struct schib schib;
	struct irb irb;

	if (stsch(sch->schid, &schib) || !css_sch_is_valid(&schib))
		return -ENODEV;

	for (retry = 0; retry < 5; retry++) {
		/* copy desired changes to local schib */
		cio_apply_config(sch, &schib);
		ccode = msch(sch->schid, &schib);
		if (ccode < 0) /* -EIO if msch gets a program check. */
			return ccode;
		switch (ccode) {
		case 0: /* successful */
			if (stsch(sch->schid, &schib) ||
			    !css_sch_is_valid(&schib))
				return -ENODEV;
			if (cio_check_config(sch, &schib)) {
				/* commit changes from local schib */
				memcpy(&sch->schib, &schib, sizeof(schib));
				return 0;
			}
			ret = -EAGAIN;
			break;
		case 1: /* status pending */
			ret = -EBUSY;
			if (tsch(sch->schid, &irb))
				return ret;
			break;
		case 2: /* busy */
			udelay(100); /* allow for recovery */
			ret = -EBUSY;
			break;
		case 3: /* not operational */
			return -ENODEV;
		}
	}
	return ret;
}
EXPORT_SYMBOL_GPL(cio_commit_config);

/**
 * cio_update_schib - Perform stsch and update schib if subchannel is valid.
 * @sch: subchannel on which to perform stsch
 * Return zero on success, -ENODEV otherwise.
 */
int cio_update_schib(struct subchannel *sch)
{
	struct schib schib;

	if (stsch(sch->schid, &schib) || !css_sch_is_valid(&schib))
		return -ENODEV;

	memcpy(&sch->schib, &schib, sizeof(schib));
	return 0;
}
EXPORT_SYMBOL_GPL(cio_update_schib);

/**
 * cio_enable_subchannel - enable a subchannel.
 * @sch: subchannel to be enabled
 * @intparm: interruption parameter to set
 */
int cio_enable_subchannel(struct subchannel *sch, u32 intparm)
{
	int ret;

	CIO_TRACE_EVENT(2, "ensch");
	CIO_TRACE_EVENT(2, dev_name(&sch->dev));

	if (sch_is_pseudo_sch(sch))
		return -EINVAL;
	if (cio_update_schib(sch))
		return -ENODEV;

	sch->config.ena = 1;
	sch->config.isc = sch->isc;
	sch->config.intparm = intparm;

	ret = cio_commit_config(sch);
	if (ret == -EIO) {
		/*
		 * Got a program check in msch. Try without
		 * the concurrent sense bit the next time.
		 */
		sch->config.csense = 0;
		ret = cio_commit_config(sch);
	}
	CIO_HEX_EVENT(2, &ret, sizeof(ret));
	return ret;
}
EXPORT_SYMBOL_GPL(cio_enable_subchannel);

/**
 * cio_disable_subchannel - disable a subchannel.
 * @sch: subchannel to disable
 */
int cio_disable_subchannel(struct subchannel *sch)
{
	int ret;

	CIO_TRACE_EVENT(2, "dissch");
	CIO_TRACE_EVENT(2, dev_name(&sch->dev));

	if (sch_is_pseudo_sch(sch))
		return 0;
	if (cio_update_schib(sch))
		return -ENODEV;

	sch->config.ena = 0;
	ret = cio_commit_config(sch);

	CIO_HEX_EVENT(2, &ret, sizeof(ret));
	return ret;
}
EXPORT_SYMBOL_GPL(cio_disable_subchannel);

/*
 * do_cio_interrupt() handles all normal I/O device IRQ's
 */
static irqreturn_t do_cio_interrupt(int irq, void *dummy)
{
	struct tpi_info *tpi_info;
	struct subchannel *sch;
	struct irb *irb;

	set_cpu_flag(CIF_NOHZ_DELAY);
	tpi_info = &get_irq_regs()->tpi_info;
	trace_s390_cio_interrupt(tpi_info);
	irb = this_cpu_ptr(&cio_irb);
	if (!tpi_info->intparm) {
		/* Clear pending interrupt condition. */
		inc_irq_stat(IRQIO_CIO);
		tsch(tpi_info->schid, irb);
		return IRQ_HANDLED;
	}
	sch = phys_to_virt(tpi_info->intparm);
	spin_lock(sch->lock);
	/* Store interrupt response block to lowcore. */
	if (tsch(tpi_info->schid, irb) == 0) {
		/* Keep subchannel information word up to date. */
		memcpy (&sch->schib.scsw, &irb->scsw, sizeof (irb->scsw));
		/* Call interrupt handler if there is one. */
		if (sch->driver && sch->driver->irq)
			sch->driver->irq(sch);
		else
			inc_irq_stat(IRQIO_CIO);
	} else
		inc_irq_stat(IRQIO_CIO);
	spin_unlock(sch->lock);

	return IRQ_HANDLED;
}

void __init init_cio_interrupts(void)
{
	irq_set_chip_and_handler(IO_INTERRUPT,
				 &dummy_irq_chip, handle_percpu_irq);
	if (request_irq(IO_INTERRUPT, do_cio_interrupt, 0, "I/O", NULL))
		panic("Failed to register I/O interrupt\n");
}

#ifdef CONFIG_CCW_CONSOLE
static struct subchannel *console_sch;
static struct lock_class_key console_sch_key;

/*
 * Use cio_tsch to update the subchannel status and call the interrupt handler
 * if status had been pending. Called with the subchannel's lock held.
 */
void cio_tsch(struct subchannel *sch)
{
	struct irb *irb;
	int irq_context;

	irb = this_cpu_ptr(&cio_irb);
	/* Store interrupt response block to lowcore. */
	if (tsch(sch->schid, irb) != 0)
		/* Not status pending or not operational. */
		return;
	memcpy(&sch->schib.scsw, &irb->scsw, sizeof(union scsw));
	/* Call interrupt handler with updated status. */
	irq_context = in_interrupt();
	if (!irq_context) {
		local_bh_disable();
		irq_enter();
	}
	kstat_incr_irq_this_cpu(IO_INTERRUPT);
	if (sch->driver && sch->driver->irq)
		sch->driver->irq(sch);
	else
		inc_irq_stat(IRQIO_CIO);
	if (!irq_context) {
		irq_exit();
		_local_bh_enable();
	}
}

static int cio_test_for_console(struct subchannel_id schid, void *data)
{
	struct schib schib;

	if (stsch(schid, &schib) != 0)
		return -ENXIO;
	if ((schib.pmcw.st == SUBCHANNEL_TYPE_IO) && schib.pmcw.dnv &&
	    (schib.pmcw.dev == console_devno)) {
		console_irq = schid.sch_no;
		return 1; /* found */
	}
	return 0;
}

static int cio_get_console_sch_no(void)
{
	struct subchannel_id schid;
	struct schib schib;

	init_subchannel_id(&schid);
	if (console_irq != -1) {
		/* VM provided us with the irq number of the console. */
		schid.sch_no = console_irq;
		if (stsch(schid, &schib) != 0 ||
		    (schib.pmcw.st != SUBCHANNEL_TYPE_IO) || !schib.pmcw.dnv)
			return -1;
		console_devno = schib.pmcw.dev;
	} else if (console_devno != -1) {
		/* At least the console device number is known. */
		for_each_subchannel(cio_test_for_console, NULL);
	}
	return console_irq;
}

struct subchannel *cio_probe_console(void)
{
	struct subchannel_id schid;
	struct subchannel *sch;
	struct schib schib;
	int sch_no, ret;

	sch_no = cio_get_console_sch_no();
	if (sch_no == -1) {
		pr_warn("No CCW console was found\n");
		return ERR_PTR(-ENODEV);
	}
	init_subchannel_id(&schid);
	schid.sch_no = sch_no;
	ret = stsch(schid, &schib);
	if (ret)
		return ERR_PTR(-ENODEV);

	sch = css_alloc_subchannel(schid, &schib);
	if (IS_ERR(sch))
		return sch;

	lockdep_set_class(sch->lock, &console_sch_key);
	isc_register(CONSOLE_ISC);
	sch->config.isc = CONSOLE_ISC;
	sch->config.intparm = (u32)virt_to_phys(sch);
	ret = cio_commit_config(sch);
	if (ret) {
		isc_unregister(CONSOLE_ISC);
		put_device(&sch->dev);
		return ERR_PTR(ret);
	}
	console_sch = sch;
	return sch;
}

int cio_is_console(struct subchannel_id schid)
{
	if (!console_sch)
		return 0;
	return schid_equal(&schid, &console_sch->schid);
}

void cio_register_early_subchannels(void)
{
	int ret;

	if (!console_sch)
		return;

	ret = css_register_subchannel(console_sch);
	if (ret)
		put_device(&console_sch->dev);
}
#endif /* CONFIG_CCW_CONSOLE */

/**
 * cio_tm_start_key - perform start function
 * @sch: subchannel on which to perform the start function
 * @tcw: transport-command word to be started
 * @lpm: mask of paths to use
 * @key: storage key to use for storage access
 *
 * Start the tcw on the given subchannel. Return zero on success, non-zero
 * otherwise.
 */
int cio_tm_start_key(struct subchannel *sch, struct tcw *tcw, u8 lpm, u8 key)
{
	int cc;
	union orb *orb = &to_io_private(sch)->orb;

	memset(orb, 0, sizeof(union orb));
	orb->tm.intparm = (u32)virt_to_phys(sch);
	orb->tm.key = key >> 4;
	orb->tm.b = 1;
	orb->tm.lpm = lpm ? lpm : sch->lpm;
	orb->tm.tcw = (u32)virt_to_phys(tcw);
	cc = ssch(sch->schid, orb);
	switch (cc) {
	case 0:
		return 0;
	case 1:
	case 2:
		return -EBUSY;
	default:
		return cio_start_handle_notoper(sch, lpm);
	}
}
EXPORT_SYMBOL_GPL(cio_tm_start_key);

/**
 * cio_tm_intrg - perform interrogate function
 * @sch: subchannel on which to perform the interrogate function
 *
 * If the specified subchannel is running in transport-mode, perform the
 * interrogate function. Return zero on success, non-zero otherwie.
 */
int cio_tm_intrg(struct subchannel *sch)
{
	int cc;

	if (!to_io_private(sch)->orb.tm.b)
		return -EINVAL;
	cc = xsch(sch->schid);
	switch (cc) {
	case 0:
	case 2:
		return 0;
	case 1:
		return -EBUSY;
	default:
		return -ENODEV;
	}
}
EXPORT_SYMBOL_GPL(cio_tm_intrg);
