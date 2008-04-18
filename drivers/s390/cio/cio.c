/*
 *  drivers/s390/cio/cio.c
 *   S/390 common I/O routines -- low level i/o calls
 *
 *    Copyright (C) IBM Corp. 1999,2006
 *    Author(s): Ingo Adlung (adlung@de.ibm.com)
 *		 Cornelia Huck (cornelia.huck@de.ibm.com)
 *		 Arnd Bergmann (arndb@de.ibm.com)
 *		 Martin Schwidefsky (schwidefsky@de.ibm.com)
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/kernel_stat.h>
#include <linux/interrupt.h>
#include <asm/cio.h>
#include <asm/delay.h>
#include <asm/irq.h>
#include <asm/irq_regs.h>
#include <asm/setup.h>
#include <asm/reset.h>
#include <asm/ipl.h>
#include <asm/chpid.h>
#include <asm/airq.h>
#include <asm/cpu.h>
#include "cio.h"
#include "css.h"
#include "chsc.h"
#include "ioasm.h"
#include "io_sch.h"
#include "blacklist.h"
#include "cio_debug.h"
#include "chp.h"
#include "../s390mach.h"

debug_info_t *cio_debug_msg_id;
debug_info_t *cio_debug_trace_id;
debug_info_t *cio_debug_crw_id;

int cio_show_msg;

static int __init
cio_setup (char *parm)
{
	if (!strcmp (parm, "yes"))
		cio_show_msg = 1;
	else if (!strcmp (parm, "no"))
		cio_show_msg = 0;
	else
		printk(KERN_ERR "cio: cio_setup: "
		       "invalid cio_msg parameter '%s'", parm);
	return 1;
}

__setup ("cio_msg=", cio_setup);

/*
 * Function: cio_debug_init
 * Initializes three debug logs for common I/O:
 * - cio_msg logs generic cio messages
 * - cio_trace logs the calling of different functions
 * - cio_crw logs machine check related cio messages
 */
static int __init cio_debug_init(void)
{
	cio_debug_msg_id = debug_register("cio_msg", 16, 1, 16 * sizeof(long));
	if (!cio_debug_msg_id)
		goto out_unregister;
	debug_register_view(cio_debug_msg_id, &debug_sprintf_view);
	debug_set_level(cio_debug_msg_id, 2);
	cio_debug_trace_id = debug_register("cio_trace", 16, 1, 16);
	if (!cio_debug_trace_id)
		goto out_unregister;
	debug_register_view(cio_debug_trace_id, &debug_hex_ascii_view);
	debug_set_level(cio_debug_trace_id, 2);
	cio_debug_crw_id = debug_register("cio_crw", 16, 1, 16 * sizeof(long));
	if (!cio_debug_crw_id)
		goto out_unregister;
	debug_register_view(cio_debug_crw_id, &debug_sprintf_view);
	debug_set_level(cio_debug_crw_id, 4);
	return 0;

out_unregister:
	if (cio_debug_msg_id)
		debug_unregister(cio_debug_msg_id);
	if (cio_debug_trace_id)
		debug_unregister(cio_debug_trace_id);
	if (cio_debug_crw_id)
		debug_unregister(cio_debug_crw_id);
	printk(KERN_WARNING"cio: could not initialize debugging\n");
	return -1;
}

arch_initcall (cio_debug_init);

int
cio_set_options (struct subchannel *sch, int flags)
{
       sch->options.suspend = (flags & DOIO_ALLOW_SUSPEND) != 0;
       sch->options.prefetch = (flags & DOIO_DENY_PREFETCH) != 0;
       sch->options.inter = (flags & DOIO_SUPPRESS_INTER) != 0;
       return 0;
}

/* FIXME: who wants to use this? */
int
cio_get_options (struct subchannel *sch)
{
       int flags;

       flags = 0;
       if (sch->options.suspend)
		flags |= DOIO_ALLOW_SUSPEND;
       if (sch->options.prefetch)
		flags |= DOIO_DENY_PREFETCH;
       if (sch->options.inter)
		flags |= DOIO_SUPPRESS_INTER;
       return flags;
}

/*
 * Use tpi to get a pending interrupt, call the interrupt handler and
 * return a pointer to the subchannel structure.
 */
static int
cio_tpi(void)
{
	struct tpi_info *tpi_info;
	struct subchannel *sch;
	struct irb *irb;

	tpi_info = (struct tpi_info *) __LC_SUBCHANNEL_ID;
	if (tpi (NULL) != 1)
		return 0;
	irb = (struct irb *) __LC_IRB;
	/* Store interrupt response block to lowcore. */
	if (tsch (tpi_info->schid, irb) != 0)
		/* Not status pending or not operational. */
		return 1;
	sch = (struct subchannel *)(unsigned long)tpi_info->intparm;
	if (!sch)
		return 1;
	local_bh_disable();
	irq_enter ();
	spin_lock(sch->lock);
	memcpy (&sch->schib.scsw, &irb->scsw, sizeof (struct scsw));
	if (sch->driver && sch->driver->irq)
		sch->driver->irq(sch);
	spin_unlock(sch->lock);
	irq_exit ();
	_local_bh_enable();
	return 1;
}

static int
cio_start_handle_notoper(struct subchannel *sch, __u8 lpm)
{
	char dbf_text[15];

	if (lpm != 0)
		sch->lpm &= ~lpm;
	else
		sch->lpm = 0;

	stsch (sch->schid, &sch->schib);

	CIO_MSG_EVENT(0, "cio_start: 'not oper' status for "
		      "subchannel 0.%x.%04x!\n", sch->schid.ssid,
		      sch->schid.sch_no);
	sprintf(dbf_text, "no%s", sch->dev.bus_id);
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
	char dbf_txt[15];
	int ccode;
	struct orb *orb;

	CIO_TRACE_EVENT(4, "stIO");
	CIO_TRACE_EVENT(4, sch->dev.bus_id);

	orb = &to_io_private(sch)->orb;
	/* sch is always under 2G. */
	orb->intparm = (u32)(addr_t)sch;
	orb->fmt = 1;

	orb->pfch = sch->options.prefetch == 0;
	orb->spnd = sch->options.suspend;
	orb->ssic = sch->options.suspend && sch->options.inter;
	orb->lpm = (lpm != 0) ? lpm : sch->lpm;
#ifdef CONFIG_64BIT
	/*
	 * for 64 bit we always support 64 bit IDAWs with 4k page size only
	 */
	orb->c64 = 1;
	orb->i2k = 0;
#endif
	orb->key = key >> 4;
	/* issue "Start Subchannel" */
	orb->cpa = (__u32) __pa(cpa);
	ccode = ssch(sch->schid, orb);

	/* process condition code */
	sprintf(dbf_txt, "ccode:%d", ccode);
	CIO_TRACE_EVENT(4, dbf_txt);

	switch (ccode) {
	case 0:
		/*
		 * initialize device status information
		 */
		sch->schib.scsw.actl |= SCSW_ACTL_START_PEND;
		return 0;
	case 1:		/* status pending */
	case 2:		/* busy */
		return -EBUSY;
	default:		/* device/path not operational */
		return cio_start_handle_notoper(sch, lpm);
	}
}

int
cio_start (struct subchannel *sch, struct ccw1 *cpa, __u8 lpm)
{
	return cio_start_key(sch, cpa, lpm, PAGE_DEFAULT_KEY);
}

/*
 * resume suspended I/O operation
 */
int
cio_resume (struct subchannel *sch)
{
	char dbf_txt[15];
	int ccode;

	CIO_TRACE_EVENT (4, "resIO");
	CIO_TRACE_EVENT (4, sch->dev.bus_id);

	ccode = rsch (sch->schid);

	sprintf (dbf_txt, "ccode:%d", ccode);
	CIO_TRACE_EVENT (4, dbf_txt);

	switch (ccode) {
	case 0:
		sch->schib.scsw.actl |= SCSW_ACTL_RESUME_PEND;
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

/*
 * halt I/O operation
 */
int
cio_halt(struct subchannel *sch)
{
	char dbf_txt[15];
	int ccode;

	if (!sch)
		return -ENODEV;

	CIO_TRACE_EVENT (2, "haltIO");
	CIO_TRACE_EVENT (2, sch->dev.bus_id);

	/*
	 * Issue "Halt subchannel" and process condition code
	 */
	ccode = hsch (sch->schid);

	sprintf (dbf_txt, "ccode:%d", ccode);
	CIO_TRACE_EVENT (2, dbf_txt);

	switch (ccode) {
	case 0:
		sch->schib.scsw.actl |= SCSW_ACTL_HALT_PEND;
		return 0;
	case 1:		/* status pending */
	case 2:		/* busy */
		return -EBUSY;
	default:		/* device not operational */
		return -ENODEV;
	}
}

/*
 * Clear I/O operation
 */
int
cio_clear(struct subchannel *sch)
{
	char dbf_txt[15];
	int ccode;

	if (!sch)
		return -ENODEV;

	CIO_TRACE_EVENT (2, "clearIO");
	CIO_TRACE_EVENT (2, sch->dev.bus_id);

	/*
	 * Issue "Clear subchannel" and process condition code
	 */
	ccode = csch (sch->schid);

	sprintf (dbf_txt, "ccode:%d", ccode);
	CIO_TRACE_EVENT (2, dbf_txt);

	switch (ccode) {
	case 0:
		sch->schib.scsw.actl |= SCSW_ACTL_CLEAR_PEND;
		return 0;
	default:		/* device not operational */
		return -ENODEV;
	}
}

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
	char dbf_txt[15];
	int ccode;

	if (!sch)
		return -ENODEV;

	CIO_TRACE_EVENT (2, "cancelIO");
	CIO_TRACE_EVENT (2, sch->dev.bus_id);

	ccode = xsch (sch->schid);

	sprintf (dbf_txt, "ccode:%d", ccode);
	CIO_TRACE_EVENT (2, dbf_txt);

	switch (ccode) {
	case 0:		/* success */
		/* Update information in scsw. */
		stsch (sch->schid, &sch->schib);
		return 0;
	case 1:		/* status pending */
		return -EBUSY;
	case 2:		/* not applicable */
		return -EINVAL;
	default:	/* not oper */
		return -ENODEV;
	}
}

/*
 * Function: cio_modify
 * Issues a "Modify Subchannel" on the specified subchannel
 */
int
cio_modify (struct subchannel *sch)
{
	int ccode, retry, ret;

	ret = 0;
	for (retry = 0; retry < 5; retry++) {
		ccode = msch_err (sch->schid, &sch->schib);
		if (ccode < 0)	/* -EIO if msch gets a program check. */
			return ccode;
		switch (ccode) {
		case 0: /* successfull */
			return 0;
		case 1:	/* status pending */
			return -EBUSY;
		case 2:	/* busy */
			udelay (100);	/* allow for recovery */
			ret = -EBUSY;
			break;
		case 3:	/* not operational */
			return -ENODEV;
		}
	}
	return ret;
}

/*
 * Enable subchannel.
 */
int cio_enable_subchannel(struct subchannel *sch, unsigned int isc,
			  u32 intparm)
{
	char dbf_txt[15];
	int ccode;
	int retry;
	int ret;

	CIO_TRACE_EVENT (2, "ensch");
	CIO_TRACE_EVENT (2, sch->dev.bus_id);

	if (sch_is_pseudo_sch(sch))
		return -EINVAL;
	ccode = stsch (sch->schid, &sch->schib);
	if (ccode)
		return -ENODEV;

	for (retry = 5, ret = 0; retry > 0; retry--) {
		sch->schib.pmcw.ena = 1;
		sch->schib.pmcw.isc = isc;
		sch->schib.pmcw.intparm = intparm;
		ret = cio_modify(sch);
		if (ret == -ENODEV)
			break;
		if (ret == -EIO)
			/*
			 * Got a program check in cio_modify. Try without
			 * the concurrent sense bit the next time.
			 */
			sch->schib.pmcw.csense = 0;
		if (ret == 0) {
			stsch (sch->schid, &sch->schib);
			if (sch->schib.pmcw.ena)
				break;
		}
		if (ret == -EBUSY) {
			struct irb irb;
			if (tsch(sch->schid, &irb) != 0)
				break;
		}
	}
	sprintf (dbf_txt, "ret:%d", ret);
	CIO_TRACE_EVENT (2, dbf_txt);
	return ret;
}

/*
 * Disable subchannel.
 */
int
cio_disable_subchannel (struct subchannel *sch)
{
	char dbf_txt[15];
	int ccode;
	int retry;
	int ret;

	CIO_TRACE_EVENT (2, "dissch");
	CIO_TRACE_EVENT (2, sch->dev.bus_id);

	if (sch_is_pseudo_sch(sch))
		return 0;
	ccode = stsch (sch->schid, &sch->schib);
	if (ccode == 3)		/* Not operational. */
		return -ENODEV;

	if (sch->schib.scsw.actl != 0)
		/*
		 * the disable function must not be called while there are
		 *  requests pending for completion !
		 */
		return -EBUSY;

	for (retry = 5, ret = 0; retry > 0; retry--) {
		sch->schib.pmcw.ena = 0;
		ret = cio_modify(sch);
		if (ret == -ENODEV)
			break;
		if (ret == -EBUSY)
			/*
			 * The subchannel is busy or status pending.
			 * We'll disable when the next interrupt was delivered
			 * via the state machine.
			 */
			break;
		if (ret == 0) {
			stsch (sch->schid, &sch->schib);
			if (!sch->schib.pmcw.ena)
				break;
		}
	}
	sprintf (dbf_txt, "ret:%d", ret);
	CIO_TRACE_EVENT (2, dbf_txt);
	return ret;
}

int cio_create_sch_lock(struct subchannel *sch)
{
	sch->lock = kmalloc(sizeof(spinlock_t), GFP_KERNEL);
	if (!sch->lock)
		return -ENOMEM;
	spin_lock_init(sch->lock);
	return 0;
}

/*
 * cio_validate_subchannel()
 *
 * Find out subchannel type and initialize struct subchannel.
 * Return codes:
 *   SUBCHANNEL_TYPE_IO for a normal io subchannel
 *   SUBCHANNEL_TYPE_CHSC for a chsc subchannel
 *   SUBCHANNEL_TYPE_MESSAGE for a messaging subchannel
 *   SUBCHANNEL_TYPE_ADM for a adm(?) subchannel
 *   -ENXIO for non-defined subchannels
 *   -ENODEV for subchannels with invalid device number or blacklisted devices
 */
int
cio_validate_subchannel (struct subchannel *sch, struct subchannel_id schid)
{
	char dbf_txt[15];
	int ccode;
	int err;

	sprintf (dbf_txt, "valsch%x", schid.sch_no);
	CIO_TRACE_EVENT (4, dbf_txt);

	/* Nuke all fields. */
	memset(sch, 0, sizeof(struct subchannel));

	sch->schid = schid;
	if (cio_is_console(schid)) {
		sch->lock = cio_get_console_lock();
	} else {
		err = cio_create_sch_lock(sch);
		if (err)
			goto out;
	}
	mutex_init(&sch->reg_mutex);
	/* Set a name for the subchannel */
	snprintf (sch->dev.bus_id, BUS_ID_SIZE, "0.%x.%04x", schid.ssid,
		  schid.sch_no);

	/*
	 * The first subchannel that is not-operational (ccode==3)
	 *  indicates that there aren't any more devices available.
	 * If stsch gets an exception, it means the current subchannel set
	 *  is not valid.
	 */
	ccode = stsch_err (schid, &sch->schib);
	if (ccode) {
		err = (ccode == 3) ? -ENXIO : ccode;
		goto out;
	}
	/* Copy subchannel type from path management control word. */
	sch->st = sch->schib.pmcw.st;

	/*
	 * ... just being curious we check for non I/O subchannels
	 */
	if (sch->st != 0) {
		CIO_DEBUG(KERN_INFO, 0,
			  "Subchannel 0.%x.%04x reports "
			  "non-I/O subchannel type %04X\n",
			  sch->schid.ssid, sch->schid.sch_no, sch->st);
		/* We stop here for non-io subchannels. */
		err = sch->st;
		goto out;
	}

	/* Initialization for io subchannels. */
	if (!css_sch_is_valid(&sch->schib)) {
		err = -ENODEV;
		goto out;
	}

	/* Devno is valid. */
	if (is_blacklisted (sch->schid.ssid, sch->schib.pmcw.dev)) {
		/*
		 * This device must not be known to Linux. So we simply
		 * say that there is no device and return ENODEV.
		 */
		CIO_MSG_EVENT(4, "Blacklisted device detected "
			      "at devno %04X, subchannel set %x\n",
			      sch->schib.pmcw.dev, sch->schid.ssid);
		err = -ENODEV;
		goto out;
	}
	if (cio_is_console(sch->schid))
		sch->opm = 0xff;
	else
		sch->opm = chp_get_sch_opm(sch);
	sch->lpm = sch->schib.pmcw.pam & sch->opm;

	CIO_DEBUG(KERN_INFO, 0,
		  "Detected device %04x on subchannel 0.%x.%04X"
		  " - PIM = %02X, PAM = %02X, POM = %02X\n",
		  sch->schib.pmcw.dev, sch->schid.ssid,
		  sch->schid.sch_no, sch->schib.pmcw.pim,
		  sch->schib.pmcw.pam, sch->schib.pmcw.pom);

	/*
	 * We now have to initially ...
	 *  ... set "interruption subclass"
	 *  ... enable "concurrent sense"
	 *  ... enable "multipath mode" if more than one
	 *	  CHPID is available. This is done regardless
	 *	  whether multiple paths are available for us.
	 */
	sch->schib.pmcw.isc = 3;	/* could be smth. else */
	sch->schib.pmcw.csense = 1;	/* concurrent sense */
	sch->schib.pmcw.ena = 0;
	if ((sch->lpm & (sch->lpm - 1)) != 0)
		sch->schib.pmcw.mp = 1;	/* multipath mode */
	/* clean up possible residual cmf stuff */
	sch->schib.pmcw.mme = 0;
	sch->schib.pmcw.mbfc = 0;
	sch->schib.pmcw.mbi = 0;
	sch->schib.mba = 0;
	return 0;
out:
	if (!cio_is_console(schid))
		kfree(sch->lock);
	sch->lock = NULL;
	return err;
}

/*
 * do_IRQ() handles all normal I/O device IRQ's (the special
 *	    SMP cross-CPU interrupts have their own specific
 *	    handlers).
 *
 */
void
do_IRQ (struct pt_regs *regs)
{
	struct tpi_info *tpi_info;
	struct subchannel *sch;
	struct irb *irb;
	struct pt_regs *old_regs;

	old_regs = set_irq_regs(regs);
	irq_enter();
	s390_idle_check();
	if (S390_lowcore.int_clock >= S390_lowcore.clock_comparator)
		/* Serve timer interrupts first. */
		clock_comparator_work();
	/*
	 * Get interrupt information from lowcore
	 */
	tpi_info = (struct tpi_info *) __LC_SUBCHANNEL_ID;
	irb = (struct irb *) __LC_IRB;
	do {
		kstat_cpu(smp_processor_id()).irqs[IO_INTERRUPT]++;
		/*
		 * Non I/O-subchannel thin interrupts are processed differently
		 */
		if (tpi_info->adapter_IO == 1 &&
		    tpi_info->int_type == IO_INTERRUPT_TYPE) {
			do_adapter_IO();
			continue;
		}
		sch = (struct subchannel *)(unsigned long)tpi_info->intparm;
		if (!sch) {
			/* Clear pending interrupt condition. */
			tsch(tpi_info->schid, irb);
			continue;
		}
		spin_lock(sch->lock);
		/* Store interrupt response block to lowcore. */
		if (tsch(tpi_info->schid, irb) == 0) {
			/* Keep subchannel information word up to date. */
			memcpy (&sch->schib.scsw, &irb->scsw,
				sizeof (irb->scsw));
			/* Call interrupt handler if there is one. */
			if (sch->driver && sch->driver->irq)
				sch->driver->irq(sch);
		}
		spin_unlock(sch->lock);
		/*
		 * Are more interrupts pending?
		 * If so, the tpi instruction will update the lowcore
		 * to hold the info for the next interrupt.
		 * We don't do this for VM because a tpi drops the cpu
		 * out of the sie which costs more cycles than it saves.
		 */
	} while (!MACHINE_IS_VM && tpi (NULL) != 0);
	irq_exit();
	set_irq_regs(old_regs);
}

#ifdef CONFIG_CCW_CONSOLE
static struct subchannel console_subchannel;
static struct io_subchannel_private console_priv;
static int console_subchannel_in_use;

void *cio_get_console_priv(void)
{
	return &console_priv;
}

/*
 * busy wait for the next interrupt on the console
 */
void wait_cons_dev(void)
	__releases(console_subchannel.lock)
	__acquires(console_subchannel.lock)
{
	unsigned long cr6      __attribute__ ((aligned (8)));
	unsigned long save_cr6 __attribute__ ((aligned (8)));

	/* 
	 * before entering the spinlock we may already have
	 * processed the interrupt on a different CPU...
	 */
	if (!console_subchannel_in_use)
		return;

	/* disable all but isc 7 (console device) */
	__ctl_store (save_cr6, 6, 6);
	cr6 = 0x01000000;
	__ctl_load (cr6, 6, 6);

	do {
		spin_unlock(console_subchannel.lock);
		if (!cio_tpi())
			cpu_relax();
		spin_lock(console_subchannel.lock);
	} while (console_subchannel.schib.scsw.actl != 0);
	/*
	 * restore previous isc value
	 */
	__ctl_load (save_cr6, 6, 6);
}

static int
cio_test_for_console(struct subchannel_id schid, void *data)
{
	if (stsch_err(schid, &console_subchannel.schib) != 0)
		return -ENXIO;
	if ((console_subchannel.schib.pmcw.st == SUBCHANNEL_TYPE_IO) &&
	    console_subchannel.schib.pmcw.dnv &&
	    (console_subchannel.schib.pmcw.dev == console_devno)) {
		console_irq = schid.sch_no;
		return 1; /* found */
	}
	return 0;
}


static int
cio_get_console_sch_no(void)
{
	struct subchannel_id schid;
	
	init_subchannel_id(&schid);
	if (console_irq != -1) {
		/* VM provided us with the irq number of the console. */
		schid.sch_no = console_irq;
		if (stsch(schid, &console_subchannel.schib) != 0 ||
		    (console_subchannel.schib.pmcw.st != SUBCHANNEL_TYPE_IO) ||
		    !console_subchannel.schib.pmcw.dnv)
			return -1;
		console_devno = console_subchannel.schib.pmcw.dev;
	} else if (console_devno != -1) {
		/* At least the console device number is known. */
		for_each_subchannel(cio_test_for_console, NULL);
		if (console_irq == -1)
			return -1;
	} else {
		/* unlike in 2.4, we cannot autoprobe here, since
		 * the channel subsystem is not fully initialized.
		 * With some luck, the HWC console can take over */
		printk(KERN_WARNING "cio: No ccw console found!\n");
		return -1;
	}
	return console_irq;
}

struct subchannel *
cio_probe_console(void)
{
	int sch_no, ret;
	struct subchannel_id schid;

	if (xchg(&console_subchannel_in_use, 1) != 0)
		return ERR_PTR(-EBUSY);
	sch_no = cio_get_console_sch_no();
	if (sch_no == -1) {
		console_subchannel_in_use = 0;
		return ERR_PTR(-ENODEV);
	}
	memset(&console_subchannel, 0, sizeof(struct subchannel));
	init_subchannel_id(&schid);
	schid.sch_no = sch_no;
	ret = cio_validate_subchannel(&console_subchannel, schid);
	if (ret) {
		console_subchannel_in_use = 0;
		return ERR_PTR(-ENODEV);
	}

	/*
	 * enable console I/O-interrupt subclass 7
	 */
	ctl_set_bit(6, 24);
	console_subchannel.schib.pmcw.isc = 7;
	console_subchannel.schib.pmcw.intparm =
		(u32)(addr_t)&console_subchannel;
	ret = cio_modify(&console_subchannel);
	if (ret) {
		console_subchannel_in_use = 0;
		return ERR_PTR(ret);
	}
	return &console_subchannel;
}

void
cio_release_console(void)
{
	console_subchannel.schib.pmcw.intparm = 0;
	cio_modify(&console_subchannel);
	ctl_clear_bit(6, 24);
	console_subchannel_in_use = 0;
}

/* Bah... hack to catch console special sausages. */
int
cio_is_console(struct subchannel_id schid)
{
	if (!console_subchannel_in_use)
		return 0;
	return schid_equal(&schid, &console_subchannel.schid);
}

struct subchannel *
cio_get_console_subchannel(void)
{
	if (!console_subchannel_in_use)
		return NULL;
	return &console_subchannel;
}

#endif
static int
__disable_subchannel_easy(struct subchannel_id schid, struct schib *schib)
{
	int retry, cc;

	cc = 0;
	for (retry=0;retry<3;retry++) {
		schib->pmcw.ena = 0;
		cc = msch(schid, schib);
		if (cc)
			return (cc==3?-ENODEV:-EBUSY);
		stsch(schid, schib);
		if (!schib->pmcw.ena)
			return 0;
	}
	return -EBUSY; /* uhm... */
}

/* we can't use the normal udelay here, since it enables external interrupts */

static void udelay_reset(unsigned long usecs)
{
	uint64_t start_cc, end_cc;

	asm volatile ("STCK %0" : "=m" (start_cc));
	do {
		cpu_relax();
		asm volatile ("STCK %0" : "=m" (end_cc));
	} while (((end_cc - start_cc)/4096) < usecs);
}

static int
__clear_subchannel_easy(struct subchannel_id schid)
{
	int retry;

	if (csch(schid))
		return -ENODEV;
	for (retry=0;retry<20;retry++) {
		struct tpi_info ti;

		if (tpi(&ti)) {
			tsch(ti.schid, (struct irb *)__LC_IRB);
			if (schid_equal(&ti.schid, &schid))
				return 0;
		}
		udelay_reset(100);
	}
	return -EBUSY;
}

static int pgm_check_occured;

static void cio_reset_pgm_check_handler(void)
{
	pgm_check_occured = 1;
}

static int stsch_reset(struct subchannel_id schid, volatile struct schib *addr)
{
	int rc;

	pgm_check_occured = 0;
	s390_base_pgm_handler_fn = cio_reset_pgm_check_handler;
	rc = stsch(schid, addr);
	s390_base_pgm_handler_fn = NULL;

	/* The program check handler could have changed pgm_check_occured. */
	barrier();

	if (pgm_check_occured)
		return -EIO;
	else
		return rc;
}

static int __shutdown_subchannel_easy(struct subchannel_id schid, void *data)
{
	struct schib schib;

	if (stsch_reset(schid, &schib))
		return -ENXIO;
	if (!schib.pmcw.ena)
		return 0;
	switch(__disable_subchannel_easy(schid, &schib)) {
	case 0:
	case -ENODEV:
		break;
	default: /* -EBUSY */
		if (__clear_subchannel_easy(schid))
			break; /* give up... */
		stsch(schid, &schib);
		__disable_subchannel_easy(schid, &schib);
	}
	return 0;
}

static atomic_t chpid_reset_count;

static void s390_reset_chpids_mcck_handler(void)
{
	struct crw crw;
	struct mci *mci;

	/* Check for pending channel report word. */
	mci = (struct mci *)&S390_lowcore.mcck_interruption_code;
	if (!mci->cp)
		return;
	/* Process channel report words. */
	while (stcrw(&crw) == 0) {
		/* Check for responses to RCHP. */
		if (crw.slct && crw.rsc == CRW_RSC_CPATH)
			atomic_dec(&chpid_reset_count);
	}
}

#define RCHP_TIMEOUT (30 * USEC_PER_SEC)
static void css_reset(void)
{
	int i, ret;
	unsigned long long timeout;
	struct chp_id chpid;

	/* Reset subchannels. */
	for_each_subchannel(__shutdown_subchannel_easy,  NULL);
	/* Reset channel paths. */
	s390_base_mcck_handler_fn = s390_reset_chpids_mcck_handler;
	/* Enable channel report machine checks. */
	__ctl_set_bit(14, 28);
	/* Temporarily reenable machine checks. */
	local_mcck_enable();
	chp_id_init(&chpid);
	for (i = 0; i <= __MAX_CHPID; i++) {
		chpid.id = i;
		ret = rchp(chpid);
		if ((ret == 0) || (ret == 2))
			/*
			 * rchp either succeeded, or another rchp is already
			 * in progress. In either case, we'll get a crw.
			 */
			atomic_inc(&chpid_reset_count);
	}
	/* Wait for machine check for all channel paths. */
	timeout = get_clock() + (RCHP_TIMEOUT << 12);
	while (atomic_read(&chpid_reset_count) != 0) {
		if (get_clock() > timeout)
			break;
		cpu_relax();
	}
	/* Disable machine checks again. */
	local_mcck_disable();
	/* Disable channel report machine checks. */
	__ctl_clear_bit(14, 28);
	s390_base_mcck_handler_fn = NULL;
}

static struct reset_call css_reset_call = {
	.fn = css_reset,
};

static int __init init_css_reset_call(void)
{
	atomic_set(&chpid_reset_count, 0);
	register_reset_call(&css_reset_call);
	return 0;
}

arch_initcall(init_css_reset_call);

struct sch_match_id {
	struct subchannel_id schid;
	struct ccw_dev_id devid;
	int rc;
};

static int __reipl_subchannel_match(struct subchannel_id schid, void *data)
{
	struct schib schib;
	struct sch_match_id *match_id = data;

	if (stsch_reset(schid, &schib))
		return -ENXIO;
	if ((schib.pmcw.st == SUBCHANNEL_TYPE_IO) && schib.pmcw.dnv &&
	    (schib.pmcw.dev == match_id->devid.devno) &&
	    (schid.ssid == match_id->devid.ssid)) {
		match_id->schid = schid;
		match_id->rc = 0;
		return 1;
	}
	return 0;
}

static int reipl_find_schid(struct ccw_dev_id *devid,
			    struct subchannel_id *schid)
{
	struct sch_match_id match_id;

	match_id.devid = *devid;
	match_id.rc = -ENODEV;
	for_each_subchannel(__reipl_subchannel_match, &match_id);
	if (match_id.rc == 0)
		*schid = match_id.schid;
	return match_id.rc;
}

extern void do_reipl_asm(__u32 schid);

/* Make sure all subchannels are quiet before we re-ipl an lpar. */
void reipl_ccw_dev(struct ccw_dev_id *devid)
{
	struct subchannel_id schid;

	s390_reset_system();
	if (reipl_find_schid(devid, &schid) != 0)
		panic("IPL Device not found\n");
	do_reipl_asm(*((__u32*)&schid));
}

int __init cio_get_iplinfo(struct cio_iplinfo *iplinfo)
{
	struct subchannel_id schid;
	struct schib schib;

	schid = *(struct subchannel_id *)__LC_SUBCHANNEL_ID;
	if (!schid.one)
		return -ENODEV;
	if (stsch(schid, &schib))
		return -ENODEV;
	if (schib.pmcw.st != SUBCHANNEL_TYPE_IO)
		return -ENODEV;
	if (!schib.pmcw.dnv)
		return -ENODEV;
	iplinfo->devno = schib.pmcw.dev;
	iplinfo->is_qdio = schib.pmcw.qf;
	return 0;
}
