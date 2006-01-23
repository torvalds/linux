/*
 * SN Platform system controller communication support
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2004-2006 Silicon Graphics, Inc. All rights reserved.
 */

/*
 * System controller event handler
 *
 * These routines deal with environmental events arriving from the
 * system controllers.
 */

#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/byteorder/generic.h>
#include <asm/sn/sn_sal.h>
#include <asm/unaligned.h>
#include "snsc.h"

static struct subch_data_s *event_sd;

void scdrv_event(unsigned long);
DECLARE_TASKLET(sn_sysctl_event, scdrv_event, 0);

/*
 * scdrv_event_interrupt
 *
 * Pull incoming environmental events off the physical link to the
 * system controller and put them in a temporary holding area in SAL.
 * Schedule scdrv_event() to move them along to their ultimate
 * destination.
 */
static irqreturn_t
scdrv_event_interrupt(int irq, void *subch_data, struct pt_regs *regs)
{
	struct subch_data_s *sd = subch_data;
	unsigned long flags;
	int status;

	spin_lock_irqsave(&sd->sd_rlock, flags);
	status = ia64_sn_irtr_intr(sd->sd_nasid, sd->sd_subch);

	if ((status > 0) && (status & SAL_IROUTER_INTR_RECV)) {
		tasklet_schedule(&sn_sysctl_event);
	}
	spin_unlock_irqrestore(&sd->sd_rlock, flags);
	return IRQ_HANDLED;
}


/*
 * scdrv_parse_event
 *
 * Break an event (as read from SAL) into useful pieces so we can decide
 * what to do with it.
 */
static int
scdrv_parse_event(char *event, int *src, int *code, int *esp_code, char *desc)
{
	char *desc_end;
	__be32 from_buf;

	/* record event source address */
	from_buf = get_unaligned((__be32 *)event);
	*src = be32_to_cpup(&from_buf);
	event += 4; 			/* move on to event code */

	/* record the system controller's event code */
	from_buf = get_unaligned((__be32 *)event);
	*code = be32_to_cpup(&from_buf);
	event += 4;			/* move on to event arguments */

	/* how many arguments are in the packet? */
	if (*event++ != 2) {
		/* if not 2, give up */
		return -1;
	}

	/* parse out the ESP code */
	if (*event++ != IR_ARG_INT) {
		/* not an integer argument, so give up */
		return -1;
	}
	from_buf = get_unaligned((__be32 *)event);
	*esp_code = be32_to_cpup(&from_buf);
	event += 4;

	/* parse out the event description */
	if (*event++ != IR_ARG_ASCII) {
		/* not an ASCII string, so give up */
		return -1;
	}
	event[CHUNKSIZE-1] = '\0';	/* ensure this string ends! */
	event += 2; 			/* skip leading CR/LF */
	desc_end = desc + sprintf(desc, "%s", event);

	/* strip trailing CR/LF (if any) */
	for (desc_end--;
	     (desc_end != desc) && ((*desc_end == 0xd) || (*desc_end == 0xa));
	     desc_end--) {
		*desc_end = '\0';
	}

	return 0;
}


/*
 * scdrv_event_severity
 *
 * Figure out how urgent a message we should write to the console/syslog
 * via printk.
 */
static char *
scdrv_event_severity(int code)
{
	int ev_class = (code & EV_CLASS_MASK);
	int ev_severity = (code & EV_SEVERITY_MASK);
	char *pk_severity = KERN_NOTICE;

	switch (ev_class) {
	case EV_CLASS_POWER:
		switch (ev_severity) {
		case EV_SEVERITY_POWER_LOW_WARNING:
		case EV_SEVERITY_POWER_HIGH_WARNING:
			pk_severity = KERN_WARNING;
			break;
		case EV_SEVERITY_POWER_HIGH_FAULT:
		case EV_SEVERITY_POWER_LOW_FAULT:
			pk_severity = KERN_ALERT;
			break;
		}
		break;
	case EV_CLASS_FAN:
		switch (ev_severity) {
		case EV_SEVERITY_FAN_WARNING:
			pk_severity = KERN_WARNING;
			break;
		case EV_SEVERITY_FAN_FAULT:
			pk_severity = KERN_CRIT;
			break;
		}
		break;
	case EV_CLASS_TEMP:
		switch (ev_severity) {
		case EV_SEVERITY_TEMP_ADVISORY:
			pk_severity = KERN_WARNING;
			break;
		case EV_SEVERITY_TEMP_CRITICAL:
			pk_severity = KERN_CRIT;
			break;
		case EV_SEVERITY_TEMP_FAULT:
			pk_severity = KERN_ALERT;
			break;
		}
		break;
	case EV_CLASS_ENV:
		pk_severity = KERN_ALERT;
		break;
	case EV_CLASS_TEST_FAULT:
		pk_severity = KERN_ALERT;
		break;
	case EV_CLASS_TEST_WARNING:
		pk_severity = KERN_WARNING;
		break;
	case EV_CLASS_PWRD_NOTIFY:
		pk_severity = KERN_ALERT;
		break;
	}

	return pk_severity;
}


/*
 * scdrv_dispatch_event
 *
 * Do the right thing with an incoming event.  That's often nothing
 * more than printing it to the system log.  For power-down notifications
 * we start a graceful shutdown.
 */
static void
scdrv_dispatch_event(char *event, int len)
{
	static int snsc_shutting_down = 0;
	int code, esp_code, src, class;
	char desc[CHUNKSIZE];
	char *severity;

	if (scdrv_parse_event(event, &src, &code, &esp_code, desc) < 0) {
		/* ignore uninterpretible event */
		return;
	}

	/* how urgent is the message? */
	severity = scdrv_event_severity(code);

	class = (code & EV_CLASS_MASK);

	if (class == EV_CLASS_PWRD_NOTIFY || code == ENV_PWRDN_PEND) {
		struct task_struct *p;

		if (snsc_shutting_down)
			return;

		snsc_shutting_down = 1;

		/* give a message for each type of event */
		if (class == EV_CLASS_PWRD_NOTIFY)
			printk(KERN_NOTICE "Power off indication received."
			       " Sending SIGPWR to init...\n");
		else if (code == ENV_PWRDN_PEND)
			printk(KERN_CRIT "WARNING: Shutting down the system"
			       " due to a critical environmental condition."
			       " Sending SIGPWR to init...\n");

		/* give a SIGPWR signal to init proc */

		/* first find init's task */
		read_lock(&tasklist_lock);
		for_each_process(p) {
			if (p->pid == 1)
				break;
		}
		if (p) {
			force_sig(SIGPWR, p);
		} else {
			printk(KERN_ERR "Failed to signal init!\n");
			snsc_shutting_down = 0; /* so can try again (?) */
		}
		read_unlock(&tasklist_lock);
	} else {
		/* print to system log */
		printk("%s|$(0x%x)%s\n", severity, esp_code, desc);
	}
}


/*
 * scdrv_event
 *
 * Called as a tasklet when an event arrives from the L1.  Read the event
 * from where it's temporarily stored in SAL and call scdrv_dispatch_event()
 * to send it on its way.  Keep trying to read events until SAL indicates
 * that there are no more immediately available.
 */
void
scdrv_event(unsigned long dummy)
{
	int status;
	int len;
	unsigned long flags;
	struct subch_data_s *sd = event_sd;

	/* anything to read? */
	len = CHUNKSIZE;
	spin_lock_irqsave(&sd->sd_rlock, flags);
	status = ia64_sn_irtr_recv(sd->sd_nasid, sd->sd_subch,
				   sd->sd_rb, &len);

	while (!(status < 0)) {
		spin_unlock_irqrestore(&sd->sd_rlock, flags);
		scdrv_dispatch_event(sd->sd_rb, len);
		len = CHUNKSIZE;
		spin_lock_irqsave(&sd->sd_rlock, flags);
		status = ia64_sn_irtr_recv(sd->sd_nasid, sd->sd_subch,
					   sd->sd_rb, &len);
	}
	spin_unlock_irqrestore(&sd->sd_rlock, flags);
}


/*
 * scdrv_event_init
 *
 * Sets up a system controller subchannel to begin receiving event
 * messages. This is sort of a specialized version of scdrv_open()
 * in drivers/char/sn_sysctl.c.
 */
void
scdrv_event_init(struct sysctl_data_s *scd)
{
	int rv;

	event_sd = kmalloc(sizeof (struct subch_data_s), GFP_KERNEL);
	if (event_sd == NULL) {
		printk(KERN_WARNING "%s: couldn't allocate subchannel info"
		       " for event monitoring\n", __FUNCTION__);
		return;
	}

	/* initialize subch_data_s fields */
	memset(event_sd, 0, sizeof (struct subch_data_s));
	event_sd->sd_nasid = scd->scd_nasid;
	spin_lock_init(&event_sd->sd_rlock);

	/* ask the system controllers to send events to this node */
	event_sd->sd_subch = ia64_sn_sysctl_event_init(scd->scd_nasid);

	if (event_sd->sd_subch < 0) {
		kfree(event_sd);
		printk(KERN_WARNING "%s: couldn't open event subchannel\n",
		       __FUNCTION__);
		return;
	}

	/* hook event subchannel up to the system controller interrupt */
	rv = request_irq(SGI_UART_VECTOR, scdrv_event_interrupt,
			 SA_SHIRQ | SA_INTERRUPT,
			 "system controller events", event_sd);
	if (rv) {
		printk(KERN_WARNING "%s: irq request failed (%d)\n",
		       __FUNCTION__, rv);
		ia64_sn_irtr_close(event_sd->sd_nasid, event_sd->sd_subch);
		kfree(event_sd);
		return;
	}
}


