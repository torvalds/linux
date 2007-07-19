/*
 * linux/drivers/s390/crypto/ap_bus.c
 *
 * Copyright (C) 2006 IBM Corporation
 * Author(s): Cornelia Huck <cornelia.huck@de.ibm.com>
 *	      Martin Schwidefsky <schwidefsky@de.ibm.com>
 *	      Ralph Wuerthner <rwuerthn@de.ibm.com>
 *
 * Adjunct processor bus.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/notifier.h>
#include <linux/kthread.h>
#include <linux/mutex.h>
#include <asm/s390_rdev.h>
#include <asm/reset.h>

#include "ap_bus.h"

/* Some prototypes. */
static void ap_scan_bus(struct work_struct *);
static void ap_poll_all(unsigned long);
static void ap_poll_timeout(unsigned long);
static int ap_poll_thread_start(void);
static void ap_poll_thread_stop(void);
static void ap_request_timeout(unsigned long);

/**
 * Module description.
 */
MODULE_AUTHOR("IBM Corporation");
MODULE_DESCRIPTION("Adjunct Processor Bus driver, "
		   "Copyright 2006 IBM Corporation");
MODULE_LICENSE("GPL");

/**
 * Module parameter
 */
int ap_domain_index = -1;	/* Adjunct Processor Domain Index */
module_param_named(domain, ap_domain_index, int, 0000);
MODULE_PARM_DESC(domain, "domain index for ap devices");
EXPORT_SYMBOL(ap_domain_index);

static int ap_thread_flag = 1;
module_param_named(poll_thread, ap_thread_flag, int, 0000);
MODULE_PARM_DESC(poll_thread, "Turn on/off poll thread, default is 1 (on).");

static struct device *ap_root_device = NULL;
static DEFINE_SPINLOCK(ap_device_lock);
static LIST_HEAD(ap_device_list);

/**
 * Workqueue & timer for bus rescan.
 */
static struct workqueue_struct *ap_work_queue;
static struct timer_list ap_config_timer;
static int ap_config_time = AP_CONFIG_TIME;
static DECLARE_WORK(ap_config_work, ap_scan_bus);

/**
 * Tasklet & timer for AP request polling.
 */
static struct timer_list ap_poll_timer = TIMER_INITIALIZER(ap_poll_timeout,0,0);
static DECLARE_TASKLET(ap_tasklet, ap_poll_all, 0);
static atomic_t ap_poll_requests = ATOMIC_INIT(0);
static DECLARE_WAIT_QUEUE_HEAD(ap_poll_wait);
static struct task_struct *ap_poll_kthread = NULL;
static DEFINE_MUTEX(ap_poll_thread_mutex);

/**
 * Test if ap instructions are available.
 *
 * Returns 0 if the ap instructions are installed.
 */
static inline int ap_instructions_available(void)
{
	register unsigned long reg0 asm ("0") = AP_MKQID(0,0);
	register unsigned long reg1 asm ("1") = -ENODEV;
	register unsigned long reg2 asm ("2") = 0UL;

	asm volatile(
		"   .long 0xb2af0000\n"		/* PQAP(TAPQ) */
		"0: la    %1,0\n"
		"1:\n"
		EX_TABLE(0b, 1b)
		: "+d" (reg0), "+d" (reg1), "+d" (reg2) : : "cc" );
	return reg1;
}

/**
 * Test adjunct processor queue.
 * @qid: the ap queue number
 * @queue_depth: pointer to queue depth value
 * @device_type: pointer to device type value
 *
 * Returns ap queue status structure.
 */
static inline struct ap_queue_status
ap_test_queue(ap_qid_t qid, int *queue_depth, int *device_type)
{
	register unsigned long reg0 asm ("0") = qid;
	register struct ap_queue_status reg1 asm ("1");
	register unsigned long reg2 asm ("2") = 0UL;

	asm volatile(".long 0xb2af0000"		/* PQAP(TAPQ) */
		     : "+d" (reg0), "=d" (reg1), "+d" (reg2) : : "cc");
	*device_type = (int) (reg2 >> 24);
	*queue_depth = (int) (reg2 & 0xff);
	return reg1;
}

/**
 * Reset adjunct processor queue.
 * @qid: the ap queue number
 *
 * Returns ap queue status structure.
 */
static inline struct ap_queue_status ap_reset_queue(ap_qid_t qid)
{
	register unsigned long reg0 asm ("0") = qid | 0x01000000UL;
	register struct ap_queue_status reg1 asm ("1");
	register unsigned long reg2 asm ("2") = 0UL;

	asm volatile(
		".long 0xb2af0000"		/* PQAP(RAPQ) */
		: "+d" (reg0), "=d" (reg1), "+d" (reg2) : : "cc");
	return reg1;
}

/**
 * Send message to adjunct processor queue.
 * @qid: the ap queue number
 * @psmid: the program supplied message identifier
 * @msg: the message text
 * @length: the message length
 *
 * Returns ap queue status structure.
 *
 * Condition code 1 on NQAP can't happen because the L bit is 1.
 *
 * Condition code 2 on NQAP also means the send is incomplete,
 * because a segment boundary was reached. The NQAP is repeated.
 */
static inline struct ap_queue_status
__ap_send(ap_qid_t qid, unsigned long long psmid, void *msg, size_t length)
{
	typedef struct { char _[length]; } msgblock;
	register unsigned long reg0 asm ("0") = qid | 0x40000000UL;
	register struct ap_queue_status reg1 asm ("1");
	register unsigned long reg2 asm ("2") = (unsigned long) msg;
	register unsigned long reg3 asm ("3") = (unsigned long) length;
	register unsigned long reg4 asm ("4") = (unsigned int) (psmid >> 32);
	register unsigned long reg5 asm ("5") = (unsigned int) psmid;

	asm volatile (
		"0: .long 0xb2ad0042\n"		/* DQAP */
		"   brc   2,0b"
		: "+d" (reg0), "=d" (reg1), "+d" (reg2), "+d" (reg3)
		: "d" (reg4), "d" (reg5), "m" (*(msgblock *) msg)
		: "cc" );
	return reg1;
}

int ap_send(ap_qid_t qid, unsigned long long psmid, void *msg, size_t length)
{
	struct ap_queue_status status;

	status = __ap_send(qid, psmid, msg, length);
	switch (status.response_code) {
	case AP_RESPONSE_NORMAL:
		return 0;
	case AP_RESPONSE_Q_FULL:
	case AP_RESPONSE_RESET_IN_PROGRESS:
		return -EBUSY;
	default:	/* Device is gone. */
		return -ENODEV;
	}
}
EXPORT_SYMBOL(ap_send);

/*
 * Receive message from adjunct processor queue.
 * @qid: the ap queue number
 * @psmid: pointer to program supplied message identifier
 * @msg: the message text
 * @length: the message length
 *
 * Returns ap queue status structure.
 *
 * Condition code 1 on DQAP means the receive has taken place
 * but only partially.	The response is incomplete, hence the
 * DQAP is repeated.
 *
 * Condition code 2 on DQAP also means the receive is incomplete,
 * this time because a segment boundary was reached. Again, the
 * DQAP is repeated.
 *
 * Note that gpr2 is used by the DQAP instruction to keep track of
 * any 'residual' length, in case the instruction gets interrupted.
 * Hence it gets zeroed before the instruction.
 */
static inline struct ap_queue_status
__ap_recv(ap_qid_t qid, unsigned long long *psmid, void *msg, size_t length)
{
	typedef struct { char _[length]; } msgblock;
	register unsigned long reg0 asm("0") = qid | 0x80000000UL;
	register struct ap_queue_status reg1 asm ("1");
	register unsigned long reg2 asm("2") = 0UL;
	register unsigned long reg4 asm("4") = (unsigned long) msg;
	register unsigned long reg5 asm("5") = (unsigned long) length;
	register unsigned long reg6 asm("6") = 0UL;
	register unsigned long reg7 asm("7") = 0UL;


	asm volatile(
		"0: .long 0xb2ae0064\n"
		"   brc   6,0b\n"
		: "+d" (reg0), "=d" (reg1), "+d" (reg2),
		"+d" (reg4), "+d" (reg5), "+d" (reg6), "+d" (reg7),
		"=m" (*(msgblock *) msg) : : "cc" );
	*psmid = (((unsigned long long) reg6) << 32) + reg7;
	return reg1;
}

int ap_recv(ap_qid_t qid, unsigned long long *psmid, void *msg, size_t length)
{
	struct ap_queue_status status;

	status = __ap_recv(qid, psmid, msg, length);
	switch (status.response_code) {
	case AP_RESPONSE_NORMAL:
		return 0;
	case AP_RESPONSE_NO_PENDING_REPLY:
		if (status.queue_empty)
			return -ENOENT;
		return -EBUSY;
	case AP_RESPONSE_RESET_IN_PROGRESS:
		return -EBUSY;
	default:
		return -ENODEV;
	}
}
EXPORT_SYMBOL(ap_recv);

/**
 * Check if an AP queue is available. The test is repeated for
 * AP_MAX_RESET times.
 * @qid: the ap queue number
 * @queue_depth: pointer to queue depth value
 * @device_type: pointer to device type value
 */
static int ap_query_queue(ap_qid_t qid, int *queue_depth, int *device_type)
{
	struct ap_queue_status status;
	int t_depth, t_device_type, rc, i;

	rc = -EBUSY;
	for (i = 0; i < AP_MAX_RESET; i++) {
		status = ap_test_queue(qid, &t_depth, &t_device_type);
		switch (status.response_code) {
		case AP_RESPONSE_NORMAL:
			*queue_depth = t_depth + 1;
			*device_type = t_device_type;
			rc = 0;
			break;
		case AP_RESPONSE_Q_NOT_AVAIL:
			rc = -ENODEV;
			break;
		case AP_RESPONSE_RESET_IN_PROGRESS:
			break;
		case AP_RESPONSE_DECONFIGURED:
			rc = -ENODEV;
			break;
		case AP_RESPONSE_CHECKSTOPPED:
			rc = -ENODEV;
			break;
		case AP_RESPONSE_BUSY:
			break;
		default:
			BUG();
		}
		if (rc != -EBUSY)
			break;
		if (i < AP_MAX_RESET - 1)
			udelay(5);
	}
	return rc;
}

/**
 * Reset an AP queue and wait for it to become available again.
 * @qid: the ap queue number
 */
static int ap_init_queue(ap_qid_t qid)
{
	struct ap_queue_status status;
	int rc, dummy, i;

	rc = -ENODEV;
	status = ap_reset_queue(qid);
	for (i = 0; i < AP_MAX_RESET; i++) {
		switch (status.response_code) {
		case AP_RESPONSE_NORMAL:
			if (status.queue_empty)
				rc = 0;
			break;
		case AP_RESPONSE_Q_NOT_AVAIL:
		case AP_RESPONSE_DECONFIGURED:
		case AP_RESPONSE_CHECKSTOPPED:
			i = AP_MAX_RESET;	/* return with -ENODEV */
			break;
		case AP_RESPONSE_RESET_IN_PROGRESS:
			rc = -EBUSY;
		case AP_RESPONSE_BUSY:
		default:
			break;
		}
		if (rc != -ENODEV && rc != -EBUSY)
			break;
		if (i < AP_MAX_RESET - 1) {
			udelay(5);
			status = ap_test_queue(qid, &dummy, &dummy);
		}
	}
	return rc;
}

/**
 * Arm request timeout if a AP device was idle and a new request is submitted.
 */
static void ap_increase_queue_count(struct ap_device *ap_dev)
{
	int timeout = ap_dev->drv->request_timeout;

	ap_dev->queue_count++;
	if (ap_dev->queue_count == 1) {
		mod_timer(&ap_dev->timeout, jiffies + timeout);
		ap_dev->reset = AP_RESET_ARMED;
	}
}

/**
 * AP device is still alive, re-schedule request timeout if there are still
 * pending requests.
 */
static void ap_decrease_queue_count(struct ap_device *ap_dev)
{
	int timeout = ap_dev->drv->request_timeout;

	ap_dev->queue_count--;
	if (ap_dev->queue_count > 0)
		mod_timer(&ap_dev->timeout, jiffies + timeout);
	else
		/**
		 * The timeout timer should to be disabled now - since
		 * del_timer_sync() is very expensive, we just tell via the
		 * reset flag to ignore the pending timeout timer.
		 */
		ap_dev->reset = AP_RESET_IGNORE;
}

/**
 * AP device related attributes.
 */
static ssize_t ap_hwtype_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct ap_device *ap_dev = to_ap_dev(dev);
	return snprintf(buf, PAGE_SIZE, "%d\n", ap_dev->device_type);
}
static DEVICE_ATTR(hwtype, 0444, ap_hwtype_show, NULL);

static ssize_t ap_depth_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct ap_device *ap_dev = to_ap_dev(dev);
	return snprintf(buf, PAGE_SIZE, "%d\n", ap_dev->queue_depth);
}
static DEVICE_ATTR(depth, 0444, ap_depth_show, NULL);

static ssize_t ap_request_count_show(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	struct ap_device *ap_dev = to_ap_dev(dev);
	int rc;

	spin_lock_bh(&ap_dev->lock);
	rc = snprintf(buf, PAGE_SIZE, "%d\n", ap_dev->total_request_count);
	spin_unlock_bh(&ap_dev->lock);
	return rc;
}

static DEVICE_ATTR(request_count, 0444, ap_request_count_show, NULL);

static ssize_t ap_modalias_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "ap:t%02X", to_ap_dev(dev)->device_type);
}

static DEVICE_ATTR(modalias, 0444, ap_modalias_show, NULL);

static struct attribute *ap_dev_attrs[] = {
	&dev_attr_hwtype.attr,
	&dev_attr_depth.attr,
	&dev_attr_request_count.attr,
	&dev_attr_modalias.attr,
	NULL
};
static struct attribute_group ap_dev_attr_group = {
	.attrs = ap_dev_attrs
};

/**
 * AP bus driver registration/unregistration.
 */
static int ap_bus_match(struct device *dev, struct device_driver *drv)
{
	struct ap_device *ap_dev = to_ap_dev(dev);
	struct ap_driver *ap_drv = to_ap_drv(drv);
	struct ap_device_id *id;

	/**
	 * Compare device type of the device with the list of
	 * supported types of the device_driver.
	 */
	for (id = ap_drv->ids; id->match_flags; id++) {
		if ((id->match_flags & AP_DEVICE_ID_MATCH_DEVICE_TYPE) &&
		    (id->dev_type != ap_dev->device_type))
			continue;
		return 1;
	}
	return 0;
}

/**
 * uevent function for AP devices. It sets up a single environment
 * variable DEV_TYPE which contains the hardware device type.
 */
static int ap_uevent (struct device *dev, char **envp, int num_envp,
		       char *buffer, int buffer_size)
{
	struct ap_device *ap_dev = to_ap_dev(dev);
	int retval = 0, length = 0, i = 0;

	if (!ap_dev)
		return -ENODEV;

	/* Set up DEV_TYPE environment variable. */
	retval = add_uevent_var(envp, num_envp, &i,
				buffer, buffer_size, &length,
				"DEV_TYPE=%04X", ap_dev->device_type);
	if (retval)
		return retval;

	/* Add MODALIAS= */
	retval = add_uevent_var(envp, num_envp, &i,
				buffer, buffer_size, &length,
				"MODALIAS=ap:t%02X", ap_dev->device_type);

	envp[i] = NULL;
	return retval;
}

static struct bus_type ap_bus_type = {
	.name = "ap",
	.match = &ap_bus_match,
	.uevent = &ap_uevent,
};

static int ap_device_probe(struct device *dev)
{
	struct ap_device *ap_dev = to_ap_dev(dev);
	struct ap_driver *ap_drv = to_ap_drv(dev->driver);
	int rc;

	ap_dev->drv = ap_drv;
	spin_lock_bh(&ap_device_lock);
	list_add(&ap_dev->list, &ap_device_list);
	spin_unlock_bh(&ap_device_lock);
	rc = ap_drv->probe ? ap_drv->probe(ap_dev) : -ENODEV;
	return rc;
}

/**
 * Flush all requests from the request/pending queue of an AP device.
 * @ap_dev: pointer to the AP device.
 */
static void __ap_flush_queue(struct ap_device *ap_dev)
{
	struct ap_message *ap_msg, *next;

	list_for_each_entry_safe(ap_msg, next, &ap_dev->pendingq, list) {
		list_del_init(&ap_msg->list);
		ap_dev->pendingq_count--;
		ap_dev->drv->receive(ap_dev, ap_msg, ERR_PTR(-ENODEV));
	}
	list_for_each_entry_safe(ap_msg, next, &ap_dev->requestq, list) {
		list_del_init(&ap_msg->list);
		ap_dev->requestq_count--;
		ap_dev->drv->receive(ap_dev, ap_msg, ERR_PTR(-ENODEV));
	}
}

void ap_flush_queue(struct ap_device *ap_dev)
{
	spin_lock_bh(&ap_dev->lock);
	__ap_flush_queue(ap_dev);
	spin_unlock_bh(&ap_dev->lock);
}
EXPORT_SYMBOL(ap_flush_queue);

static int ap_device_remove(struct device *dev)
{
	struct ap_device *ap_dev = to_ap_dev(dev);
	struct ap_driver *ap_drv = ap_dev->drv;

	ap_flush_queue(ap_dev);
	del_timer_sync(&ap_dev->timeout);
	if (ap_drv->remove)
		ap_drv->remove(ap_dev);
	spin_lock_bh(&ap_device_lock);
	list_del_init(&ap_dev->list);
	spin_unlock_bh(&ap_device_lock);
	spin_lock_bh(&ap_dev->lock);
	atomic_sub(ap_dev->queue_count, &ap_poll_requests);
	spin_unlock_bh(&ap_dev->lock);
	return 0;
}

int ap_driver_register(struct ap_driver *ap_drv, struct module *owner,
		       char *name)
{
	struct device_driver *drv = &ap_drv->driver;

	drv->bus = &ap_bus_type;
	drv->probe = ap_device_probe;
	drv->remove = ap_device_remove;
	drv->owner = owner;
	drv->name = name;
	return driver_register(drv);
}
EXPORT_SYMBOL(ap_driver_register);

void ap_driver_unregister(struct ap_driver *ap_drv)
{
	driver_unregister(&ap_drv->driver);
}
EXPORT_SYMBOL(ap_driver_unregister);

/**
 * AP bus attributes.
 */
static ssize_t ap_domain_show(struct bus_type *bus, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", ap_domain_index);
}

static BUS_ATTR(ap_domain, 0444, ap_domain_show, NULL);

static ssize_t ap_config_time_show(struct bus_type *bus, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", ap_config_time);
}

static ssize_t ap_config_time_store(struct bus_type *bus,
				    const char *buf, size_t count)
{
	int time;

	if (sscanf(buf, "%d\n", &time) != 1 || time < 5 || time > 120)
		return -EINVAL;
	ap_config_time = time;
	if (!timer_pending(&ap_config_timer) ||
	    !mod_timer(&ap_config_timer, jiffies + ap_config_time * HZ)) {
		ap_config_timer.expires = jiffies + ap_config_time * HZ;
		add_timer(&ap_config_timer);
	}
	return count;
}

static BUS_ATTR(config_time, 0644, ap_config_time_show, ap_config_time_store);

static ssize_t ap_poll_thread_show(struct bus_type *bus, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", ap_poll_kthread ? 1 : 0);
}

static ssize_t ap_poll_thread_store(struct bus_type *bus,
				    const char *buf, size_t count)
{
	int flag, rc;

	if (sscanf(buf, "%d\n", &flag) != 1)
		return -EINVAL;
	if (flag) {
		rc = ap_poll_thread_start();
		if (rc)
			return rc;
	}
	else
		ap_poll_thread_stop();
	return count;
}

static BUS_ATTR(poll_thread, 0644, ap_poll_thread_show, ap_poll_thread_store);

static struct bus_attribute *const ap_bus_attrs[] = {
	&bus_attr_ap_domain,
	&bus_attr_config_time,
	&bus_attr_poll_thread,
	NULL
};

/**
 * Pick one of the 16 ap domains.
 */
static int ap_select_domain(void)
{
	int queue_depth, device_type, count, max_count, best_domain;
	int rc, i, j;

	/**
	 * We want to use a single domain. Either the one specified with
	 * the "domain=" parameter or the domain with the maximum number
	 * of devices.
	 */
	if (ap_domain_index >= 0 && ap_domain_index < AP_DOMAINS)
		/* Domain has already been selected. */
		return 0;
	best_domain = -1;
	max_count = 0;
	for (i = 0; i < AP_DOMAINS; i++) {
		count = 0;
		for (j = 0; j < AP_DEVICES; j++) {
			ap_qid_t qid = AP_MKQID(j, i);
			rc = ap_query_queue(qid, &queue_depth, &device_type);
			if (rc)
				continue;
			count++;
		}
		if (count > max_count) {
			max_count = count;
			best_domain = i;
		}
	}
	if (best_domain >= 0){
		ap_domain_index = best_domain;
		return 0;
	}
	return -ENODEV;
}

/**
 * Find the device type if query queue returned a device type of 0.
 * @ap_dev: pointer to the AP device.
 */
static int ap_probe_device_type(struct ap_device *ap_dev)
{
	static unsigned char msg[] = {
		0x00,0x06,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x58,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x01,0x00,0x43,0x43,0x41,0x2d,0x41,0x50,
		0x50,0x4c,0x20,0x20,0x20,0x01,0x01,0x01,
		0x00,0x00,0x00,0x00,0x50,0x4b,0x00,0x00,
		0x00,0x00,0x01,0x1c,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x05,0xb8,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x70,0x00,0x41,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x54,0x32,0x01,0x00,0xa0,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0xb8,0x05,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x0a,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x08,0x00,
		0x49,0x43,0x53,0x46,0x20,0x20,0x20,0x20,
		0x50,0x4b,0x0a,0x00,0x50,0x4b,0x43,0x53,
		0x2d,0x31,0x2e,0x32,0x37,0x00,0x11,0x22,
		0x33,0x44,0x55,0x66,0x77,0x88,0x99,0x00,
		0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,
		0x99,0x00,0x11,0x22,0x33,0x44,0x55,0x66,
		0x77,0x88,0x99,0x00,0x11,0x22,0x33,0x44,
		0x55,0x66,0x77,0x88,0x99,0x00,0x11,0x22,
		0x33,0x44,0x55,0x66,0x77,0x88,0x99,0x00,
		0x11,0x22,0x33,0x5d,0x00,0x5b,0x00,0x77,
		0x88,0x1e,0x00,0x00,0x57,0x00,0x00,0x00,
		0x00,0x04,0x00,0x00,0x4f,0x00,0x00,0x00,
		0x03,0x02,0x00,0x00,0x40,0x01,0x00,0x01,
		0xce,0x02,0x68,0x2d,0x5f,0xa9,0xde,0x0c,
		0xf6,0xd2,0x7b,0x58,0x4b,0xf9,0x28,0x68,
		0x3d,0xb4,0xf4,0xef,0x78,0xd5,0xbe,0x66,
		0x63,0x42,0xef,0xf8,0xfd,0xa4,0xf8,0xb0,
		0x8e,0x29,0xc2,0xc9,0x2e,0xd8,0x45,0xb8,
		0x53,0x8c,0x6f,0x4e,0x72,0x8f,0x6c,0x04,
		0x9c,0x88,0xfc,0x1e,0xc5,0x83,0x55,0x57,
		0xf7,0xdd,0xfd,0x4f,0x11,0x36,0x95,0x5d,
	};
	struct ap_queue_status status;
	unsigned long long psmid;
	char *reply;
	int rc, i;

	reply = (void *) get_zeroed_page(GFP_KERNEL);
	if (!reply) {
		rc = -ENOMEM;
		goto out;
	}

	status = __ap_send(ap_dev->qid, 0x0102030405060708ULL,
			   msg, sizeof(msg));
	if (status.response_code != AP_RESPONSE_NORMAL) {
		rc = -ENODEV;
		goto out_free;
	}

	/* Wait for the test message to complete. */
	for (i = 0; i < 6; i++) {
		mdelay(300);
		status = __ap_recv(ap_dev->qid, &psmid, reply, 4096);
		if (status.response_code == AP_RESPONSE_NORMAL &&
		    psmid == 0x0102030405060708ULL)
			break;
	}
	if (i < 6) {
		/* Got an answer. */
		if (reply[0] == 0x00 && reply[1] == 0x86)
			ap_dev->device_type = AP_DEVICE_TYPE_PCICC;
		else
			ap_dev->device_type = AP_DEVICE_TYPE_PCICA;
		rc = 0;
	} else
		rc = -ENODEV;

out_free:
	free_page((unsigned long) reply);
out:
	return rc;
}

/**
 * Scan the ap bus for new devices.
 */
static int __ap_scan_bus(struct device *dev, void *data)
{
	return to_ap_dev(dev)->qid == (ap_qid_t)(unsigned long) data;
}

static void ap_device_release(struct device *dev)
{
	struct ap_device *ap_dev = to_ap_dev(dev);

	kfree(ap_dev);
}

static void ap_scan_bus(struct work_struct *unused)
{
	struct ap_device *ap_dev;
	struct device *dev;
	ap_qid_t qid;
	int queue_depth, device_type;
	int rc, i;

	if (ap_select_domain() != 0)
		return;
	for (i = 0; i < AP_DEVICES; i++) {
		qid = AP_MKQID(i, ap_domain_index);
		dev = bus_find_device(&ap_bus_type, NULL,
				      (void *)(unsigned long)qid,
				      __ap_scan_bus);
		rc = ap_query_queue(qid, &queue_depth, &device_type);
		if (dev) {
			if (rc == -EBUSY) {
				set_current_state(TASK_UNINTERRUPTIBLE);
				schedule_timeout(AP_RESET_TIMEOUT);
				rc = ap_query_queue(qid, &queue_depth,
						    &device_type);
			}
			ap_dev = to_ap_dev(dev);
			spin_lock_bh(&ap_dev->lock);
			if (rc || ap_dev->unregistered) {
				spin_unlock_bh(&ap_dev->lock);
				device_unregister(dev);
				put_device(dev);
				continue;
			}
			spin_unlock_bh(&ap_dev->lock);
			put_device(dev);
			continue;
		}
		if (rc)
			continue;
		rc = ap_init_queue(qid);
		if (rc)
			continue;
		ap_dev = kzalloc(sizeof(*ap_dev), GFP_KERNEL);
		if (!ap_dev)
			break;
		ap_dev->qid = qid;
		ap_dev->queue_depth = queue_depth;
		ap_dev->unregistered = 1;
		spin_lock_init(&ap_dev->lock);
		INIT_LIST_HEAD(&ap_dev->pendingq);
		INIT_LIST_HEAD(&ap_dev->requestq);
		INIT_LIST_HEAD(&ap_dev->list);
		setup_timer(&ap_dev->timeout, ap_request_timeout,
			    (unsigned long) ap_dev);
		if (device_type == 0)
			ap_probe_device_type(ap_dev);
		else
			ap_dev->device_type = device_type;

		ap_dev->device.bus = &ap_bus_type;
		ap_dev->device.parent = ap_root_device;
		snprintf(ap_dev->device.bus_id, BUS_ID_SIZE, "card%02x",
			 AP_QID_DEVICE(ap_dev->qid));
		ap_dev->device.release = ap_device_release;
		rc = device_register(&ap_dev->device);
		if (rc) {
			kfree(ap_dev);
			continue;
		}
		/* Add device attributes. */
		rc = sysfs_create_group(&ap_dev->device.kobj,
					&ap_dev_attr_group);
		if (!rc) {
			spin_lock_bh(&ap_dev->lock);
			ap_dev->unregistered = 0;
			spin_unlock_bh(&ap_dev->lock);
		}
		else
			device_unregister(&ap_dev->device);
	}
}

static void
ap_config_timeout(unsigned long ptr)
{
	queue_work(ap_work_queue, &ap_config_work);
	ap_config_timer.expires = jiffies + ap_config_time * HZ;
	add_timer(&ap_config_timer);
}

/**
 * Set up the timer to run the poll tasklet
 */
static inline void ap_schedule_poll_timer(void)
{
	if (timer_pending(&ap_poll_timer))
		return;
	mod_timer(&ap_poll_timer, jiffies + AP_POLL_TIME);
}

/**
 * Receive pending reply messages from an AP device.
 * @ap_dev: pointer to the AP device
 * @flags: pointer to control flags, bit 2^0 is set if another poll is
 *	   required, bit 2^1 is set if the poll timer needs to get armed
 * Returns 0 if the device is still present, -ENODEV if not.
 */
static int ap_poll_read(struct ap_device *ap_dev, unsigned long *flags)
{
	struct ap_queue_status status;
	struct ap_message *ap_msg;

	if (ap_dev->queue_count <= 0)
		return 0;
	status = __ap_recv(ap_dev->qid, &ap_dev->reply->psmid,
			   ap_dev->reply->message, ap_dev->reply->length);
	switch (status.response_code) {
	case AP_RESPONSE_NORMAL:
		atomic_dec(&ap_poll_requests);
		ap_decrease_queue_count(ap_dev);
		list_for_each_entry(ap_msg, &ap_dev->pendingq, list) {
			if (ap_msg->psmid != ap_dev->reply->psmid)
				continue;
			list_del_init(&ap_msg->list);
			ap_dev->pendingq_count--;
			ap_dev->drv->receive(ap_dev, ap_msg, ap_dev->reply);
			break;
		}
		if (ap_dev->queue_count > 0)
			*flags |= 1;
		break;
	case AP_RESPONSE_NO_PENDING_REPLY:
		if (status.queue_empty) {
			/* The card shouldn't forget requests but who knows. */
			atomic_sub(ap_dev->queue_count, &ap_poll_requests);
			ap_dev->queue_count = 0;
			list_splice_init(&ap_dev->pendingq, &ap_dev->requestq);
			ap_dev->requestq_count += ap_dev->pendingq_count;
			ap_dev->pendingq_count = 0;
		} else
			*flags |= 2;
		break;
	default:
		return -ENODEV;
	}
	return 0;
}

/**
 * Send messages from the request queue to an AP device.
 * @ap_dev: pointer to the AP device
 * @flags: pointer to control flags, bit 2^0 is set if another poll is
 *	   required, bit 2^1 is set if the poll timer needs to get armed
 * Returns 0 if the device is still present, -ENODEV if not.
 */
static int ap_poll_write(struct ap_device *ap_dev, unsigned long *flags)
{
	struct ap_queue_status status;
	struct ap_message *ap_msg;

	if (ap_dev->requestq_count <= 0 ||
	    ap_dev->queue_count >= ap_dev->queue_depth)
		return 0;
	/* Start the next request on the queue. */
	ap_msg = list_entry(ap_dev->requestq.next, struct ap_message, list);
	status = __ap_send(ap_dev->qid, ap_msg->psmid,
			   ap_msg->message, ap_msg->length);
	switch (status.response_code) {
	case AP_RESPONSE_NORMAL:
		atomic_inc(&ap_poll_requests);
		ap_increase_queue_count(ap_dev);
		list_move_tail(&ap_msg->list, &ap_dev->pendingq);
		ap_dev->requestq_count--;
		ap_dev->pendingq_count++;
		if (ap_dev->queue_count < ap_dev->queue_depth &&
		    ap_dev->requestq_count > 0)
			*flags |= 1;
		*flags |= 2;
		break;
	case AP_RESPONSE_Q_FULL:
	case AP_RESPONSE_RESET_IN_PROGRESS:
		*flags |= 2;
		break;
	case AP_RESPONSE_MESSAGE_TOO_BIG:
		return -EINVAL;
	default:
		return -ENODEV;
	}
	return 0;
}

/**
 * Poll AP device for pending replies and send new messages. If either
 * ap_poll_read or ap_poll_write returns -ENODEV unregister the device.
 * @ap_dev: pointer to the bus device
 * @flags: pointer to control flags, bit 2^0 is set if another poll is
 *	   required, bit 2^1 is set if the poll timer needs to get armed
 * Returns 0.
 */
static inline int ap_poll_queue(struct ap_device *ap_dev, unsigned long *flags)
{
	int rc;

	rc = ap_poll_read(ap_dev, flags);
	if (rc)
		return rc;
	return ap_poll_write(ap_dev, flags);
}

/**
 * Queue a message to a device.
 * @ap_dev: pointer to the AP device
 * @ap_msg: the message to be queued
 */
static int __ap_queue_message(struct ap_device *ap_dev, struct ap_message *ap_msg)
{
	struct ap_queue_status status;

	if (list_empty(&ap_dev->requestq) &&
	    ap_dev->queue_count < ap_dev->queue_depth) {
		status = __ap_send(ap_dev->qid, ap_msg->psmid,
				   ap_msg->message, ap_msg->length);
		switch (status.response_code) {
		case AP_RESPONSE_NORMAL:
			list_add_tail(&ap_msg->list, &ap_dev->pendingq);
			atomic_inc(&ap_poll_requests);
			ap_dev->pendingq_count++;
			ap_increase_queue_count(ap_dev);
			ap_dev->total_request_count++;
			break;
		case AP_RESPONSE_Q_FULL:
		case AP_RESPONSE_RESET_IN_PROGRESS:
			list_add_tail(&ap_msg->list, &ap_dev->requestq);
			ap_dev->requestq_count++;
			ap_dev->total_request_count++;
			return -EBUSY;
		case AP_RESPONSE_MESSAGE_TOO_BIG:
			ap_dev->drv->receive(ap_dev, ap_msg, ERR_PTR(-EINVAL));
			return -EINVAL;
		default:	/* Device is gone. */
			ap_dev->drv->receive(ap_dev, ap_msg, ERR_PTR(-ENODEV));
			return -ENODEV;
		}
	} else {
		list_add_tail(&ap_msg->list, &ap_dev->requestq);
		ap_dev->requestq_count++;
		ap_dev->total_request_count++;
		return -EBUSY;
	}
	ap_schedule_poll_timer();
	return 0;
}

void ap_queue_message(struct ap_device *ap_dev, struct ap_message *ap_msg)
{
	unsigned long flags;
	int rc;

	spin_lock_bh(&ap_dev->lock);
	if (!ap_dev->unregistered) {
		/* Make room on the queue by polling for finished requests. */
		rc = ap_poll_queue(ap_dev, &flags);
		if (!rc)
			rc = __ap_queue_message(ap_dev, ap_msg);
		if (!rc)
			wake_up(&ap_poll_wait);
		if (rc == -ENODEV)
			ap_dev->unregistered = 1;
	} else {
		ap_dev->drv->receive(ap_dev, ap_msg, ERR_PTR(-ENODEV));
		rc = -ENODEV;
	}
	spin_unlock_bh(&ap_dev->lock);
	if (rc == -ENODEV)
		device_unregister(&ap_dev->device);
}
EXPORT_SYMBOL(ap_queue_message);

/**
 * Cancel a crypto request. This is done by removing the request
 * from the devive pendingq or requestq queue. Note that the
 * request stays on the AP queue. When it finishes the message
 * reply will be discarded because the psmid can't be found.
 * @ap_dev: AP device that has the message queued
 * @ap_msg: the message that is to be removed
 */
void ap_cancel_message(struct ap_device *ap_dev, struct ap_message *ap_msg)
{
	struct ap_message *tmp;

	spin_lock_bh(&ap_dev->lock);
	if (!list_empty(&ap_msg->list)) {
		list_for_each_entry(tmp, &ap_dev->pendingq, list)
			if (tmp->psmid == ap_msg->psmid) {
				ap_dev->pendingq_count--;
				goto found;
			}
		ap_dev->requestq_count--;
	found:
		list_del_init(&ap_msg->list);
	}
	spin_unlock_bh(&ap_dev->lock);
}
EXPORT_SYMBOL(ap_cancel_message);

/**
 * AP receive polling for finished AP requests
 */
static void ap_poll_timeout(unsigned long unused)
{
	tasklet_schedule(&ap_tasklet);
}

/**
 * Reset a not responding AP device and move all requests from the
 * pending queue to the request queue.
 */
static void ap_reset(struct ap_device *ap_dev)
{
	int rc;

	ap_dev->reset = AP_RESET_IGNORE;
	atomic_sub(ap_dev->queue_count, &ap_poll_requests);
	ap_dev->queue_count = 0;
	list_splice_init(&ap_dev->pendingq, &ap_dev->requestq);
	ap_dev->requestq_count += ap_dev->pendingq_count;
	ap_dev->pendingq_count = 0;
	rc = ap_init_queue(ap_dev->qid);
	if (rc == -ENODEV)
		ap_dev->unregistered = 1;
}

/**
 * Poll all AP devices on the bus in a round robin fashion. Continue
 * polling until bit 2^0 of the control flags is not set. If bit 2^1
 * of the control flags has been set arm the poll timer.
 */
static int __ap_poll_all(struct ap_device *ap_dev, unsigned long *flags)
{
	spin_lock(&ap_dev->lock);
	if (!ap_dev->unregistered) {
		if (ap_poll_queue(ap_dev, flags))
			ap_dev->unregistered = 1;
		if (ap_dev->reset == AP_RESET_DO)
			ap_reset(ap_dev);
	}
	spin_unlock(&ap_dev->lock);
	return 0;
}

static void ap_poll_all(unsigned long dummy)
{
	unsigned long flags;
	struct ap_device *ap_dev;

	do {
		flags = 0;
		spin_lock(&ap_device_lock);
		list_for_each_entry(ap_dev, &ap_device_list, list) {
			__ap_poll_all(ap_dev, &flags);
		}
		spin_unlock(&ap_device_lock);
	} while (flags & 1);
	if (flags & 2)
		ap_schedule_poll_timer();
}

/**
 * AP bus poll thread. The purpose of this thread is to poll for
 * finished requests in a loop if there is a "free" cpu - that is
 * a cpu that doesn't have anything better to do. The polling stops
 * as soon as there is another task or if all messages have been
 * delivered.
 */
static int ap_poll_thread(void *data)
{
	DECLARE_WAITQUEUE(wait, current);
	unsigned long flags;
	int requests;
	struct ap_device *ap_dev;

	set_user_nice(current, 19);
	while (1) {
		if (need_resched()) {
			schedule();
			continue;
		}
		add_wait_queue(&ap_poll_wait, &wait);
		set_current_state(TASK_INTERRUPTIBLE);
		if (kthread_should_stop())
			break;
		requests = atomic_read(&ap_poll_requests);
		if (requests <= 0)
			schedule();
		set_current_state(TASK_RUNNING);
		remove_wait_queue(&ap_poll_wait, &wait);

		flags = 0;
		spin_lock_bh(&ap_device_lock);
		list_for_each_entry(ap_dev, &ap_device_list, list) {
			__ap_poll_all(ap_dev, &flags);
		}
		spin_unlock_bh(&ap_device_lock);
	}
	set_current_state(TASK_RUNNING);
	remove_wait_queue(&ap_poll_wait, &wait);
	return 0;
}

static int ap_poll_thread_start(void)
{
	int rc;

	mutex_lock(&ap_poll_thread_mutex);
	if (!ap_poll_kthread) {
		ap_poll_kthread = kthread_run(ap_poll_thread, NULL, "appoll");
		rc = IS_ERR(ap_poll_kthread) ? PTR_ERR(ap_poll_kthread) : 0;
		if (rc)
			ap_poll_kthread = NULL;
	}
	else
		rc = 0;
	mutex_unlock(&ap_poll_thread_mutex);
	return rc;
}

static void ap_poll_thread_stop(void)
{
	mutex_lock(&ap_poll_thread_mutex);
	if (ap_poll_kthread) {
		kthread_stop(ap_poll_kthread);
		ap_poll_kthread = NULL;
	}
	mutex_unlock(&ap_poll_thread_mutex);
}

/**
 * Handling of request timeouts
 */
static void ap_request_timeout(unsigned long data)
{
	struct ap_device *ap_dev = (struct ap_device *) data;

	if (ap_dev->reset == AP_RESET_ARMED)
		ap_dev->reset = AP_RESET_DO;
}

static void ap_reset_domain(void)
{
	int i;

	for (i = 0; i < AP_DEVICES; i++)
		ap_reset_queue(AP_MKQID(i, ap_domain_index));
}

static void ap_reset_all(void)
{
	int i, j;

	for (i = 0; i < AP_DOMAINS; i++)
		for (j = 0; j < AP_DEVICES; j++)
			ap_reset_queue(AP_MKQID(j, i));
}

static struct reset_call ap_reset_call = {
	.fn = ap_reset_all,
};

/**
 * The module initialization code.
 */
int __init ap_module_init(void)
{
	int rc, i;

	if (ap_domain_index < -1 || ap_domain_index >= AP_DOMAINS) {
		printk(KERN_WARNING "Invalid param: domain = %d. "
		       " Not loading.\n", ap_domain_index);
		return -EINVAL;
	}
	if (ap_instructions_available() != 0) {
		printk(KERN_WARNING "AP instructions not installed.\n");
		return -ENODEV;
	}
	register_reset_call(&ap_reset_call);

	/* Create /sys/bus/ap. */
	rc = bus_register(&ap_bus_type);
	if (rc)
		goto out;
	for (i = 0; ap_bus_attrs[i]; i++) {
		rc = bus_create_file(&ap_bus_type, ap_bus_attrs[i]);
		if (rc)
			goto out_bus;
	}

	/* Create /sys/devices/ap. */
	ap_root_device = s390_root_dev_register("ap");
	rc = IS_ERR(ap_root_device) ? PTR_ERR(ap_root_device) : 0;
	if (rc)
		goto out_bus;

	ap_work_queue = create_singlethread_workqueue("kapwork");
	if (!ap_work_queue) {
		rc = -ENOMEM;
		goto out_root;
	}

	if (ap_select_domain() == 0)
		ap_scan_bus(NULL);

	/* Setup the ap bus rescan timer. */
	init_timer(&ap_config_timer);
	ap_config_timer.function = ap_config_timeout;
	ap_config_timer.data = 0;
	ap_config_timer.expires = jiffies + ap_config_time * HZ;
	add_timer(&ap_config_timer);

	/* Start the low priority AP bus poll thread. */
	if (ap_thread_flag) {
		rc = ap_poll_thread_start();
		if (rc)
			goto out_work;
	}

	return 0;

out_work:
	del_timer_sync(&ap_config_timer);
	del_timer_sync(&ap_poll_timer);
	destroy_workqueue(ap_work_queue);
out_root:
	s390_root_dev_unregister(ap_root_device);
out_bus:
	while (i--)
		bus_remove_file(&ap_bus_type, ap_bus_attrs[i]);
	bus_unregister(&ap_bus_type);
out:
	unregister_reset_call(&ap_reset_call);
	return rc;
}

static int __ap_match_all(struct device *dev, void *data)
{
	return 1;
}

/**
 * The module termination code
 */
void ap_module_exit(void)
{
	int i;
	struct device *dev;

	ap_reset_domain();
	ap_poll_thread_stop();
	del_timer_sync(&ap_config_timer);
	del_timer_sync(&ap_poll_timer);
	destroy_workqueue(ap_work_queue);
	tasklet_kill(&ap_tasklet);
	s390_root_dev_unregister(ap_root_device);
	while ((dev = bus_find_device(&ap_bus_type, NULL, NULL,
		    __ap_match_all)))
	{
		device_unregister(dev);
		put_device(dev);
	}
	for (i = 0; ap_bus_attrs[i]; i++)
		bus_remove_file(&ap_bus_type, ap_bus_attrs[i]);
	bus_unregister(&ap_bus_type);
	unregister_reset_call(&ap_reset_call);
}

#ifndef CONFIG_ZCRYPT_MONOLITHIC
module_init(ap_module_init);
module_exit(ap_module_exit);
#endif
