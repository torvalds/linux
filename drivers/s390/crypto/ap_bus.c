// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright IBM Corp. 2006, 2012
 * Author(s): Cornelia Huck <cornelia.huck@de.ibm.com>
 *	      Martin Schwidefsky <schwidefsky@de.ibm.com>
 *	      Ralph Wuerthner <rwuerthn@de.ibm.com>
 *	      Felix Beck <felix.beck@de.ibm.com>
 *	      Holger Dengler <hd@linux.vnet.ibm.com>
 *
 * Adjunct processor bus.
 */

#define KMSG_COMPONENT "ap"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/kernel_stat.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/slab.h>
#include <linux/notifier.h>
#include <linux/kthread.h>
#include <linux/mutex.h>
#include <linux/suspend.h>
#include <asm/airq.h>
#include <linux/atomic.h>
#include <asm/isc.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>
#include <asm/facility.h>
#include <linux/crypto.h>
#include <linux/mod_devicetable.h>
#include <linux/debugfs.h>
#include <linux/ctype.h>

#include "ap_bus.h"
#include "ap_debug.h"

/*
 * Module parameters; note though this file itself isn't modular.
 */
int ap_domain_index = -1;	/* Adjunct Processor Domain Index */
static DEFINE_SPINLOCK(ap_domain_lock);
module_param_named(domain, ap_domain_index, int, 0440);
MODULE_PARM_DESC(domain, "domain index for ap devices");
EXPORT_SYMBOL(ap_domain_index);

static int ap_thread_flag;
module_param_named(poll_thread, ap_thread_flag, int, 0440);
MODULE_PARM_DESC(poll_thread, "Turn on/off poll thread, default is 0 (off).");

static char *apm_str;
module_param_named(apmask, apm_str, charp, 0440);
MODULE_PARM_DESC(apmask, "AP bus adapter mask.");

static char *aqm_str;
module_param_named(aqmask, aqm_str, charp, 0440);
MODULE_PARM_DESC(aqmask, "AP bus domain mask.");

static struct device *ap_root_device;

DEFINE_SPINLOCK(ap_list_lock);
LIST_HEAD(ap_card_list);

/* Default permissions (ioctl, card and domain masking) */
struct ap_perms ap_perms;
EXPORT_SYMBOL(ap_perms);
DEFINE_MUTEX(ap_perms_mutex);
EXPORT_SYMBOL(ap_perms_mutex);

static struct ap_config_info *ap_configuration;
static bool initialised;

/*
 * AP bus related debug feature things.
 */
debug_info_t *ap_dbf_info;

/*
 * Workqueue timer for bus rescan.
 */
static struct timer_list ap_config_timer;
static int ap_config_time = AP_CONFIG_TIME;
static void ap_scan_bus(struct work_struct *);
static DECLARE_WORK(ap_scan_work, ap_scan_bus);

/*
 * Tasklet & timer for AP request polling and interrupts
 */
static void ap_tasklet_fn(unsigned long);
static DECLARE_TASKLET(ap_tasklet, ap_tasklet_fn, 0);
static DECLARE_WAIT_QUEUE_HEAD(ap_poll_wait);
static struct task_struct *ap_poll_kthread;
static DEFINE_MUTEX(ap_poll_thread_mutex);
static DEFINE_SPINLOCK(ap_poll_timer_lock);
static struct hrtimer ap_poll_timer;
/*
 * In LPAR poll with 4kHz frequency. Poll every 250000 nanoseconds.
 * If z/VM change to 1500000 nanoseconds to adjust to z/VM polling.
 */
static unsigned long long poll_timeout = 250000;

/* Suspend flag */
static int ap_suspend_flag;
/* Maximum domain id */
static int ap_max_domain_id;
/*
 * Flag to check if domain was set through module parameter domain=. This is
 * important when supsend and resume is done in a z/VM environment where the
 * domain might change.
 */
static int user_set_domain;
static struct bus_type ap_bus_type;

/* Adapter interrupt definitions */
static void ap_interrupt_handler(struct airq_struct *airq, bool floating);

static int ap_airq_flag;

static struct airq_struct ap_airq = {
	.handler = ap_interrupt_handler,
	.isc = AP_ISC,
};

/**
 * ap_using_interrupts() - Returns non-zero if interrupt support is
 * available.
 */
static inline int ap_using_interrupts(void)
{
	return ap_airq_flag;
}

/**
 * ap_airq_ptr() - Get the address of the adapter interrupt indicator
 *
 * Returns the address of the local-summary-indicator of the adapter
 * interrupt handler for AP, or NULL if adapter interrupts are not
 * available.
 */
void *ap_airq_ptr(void)
{
	if (ap_using_interrupts())
		return ap_airq.lsi_ptr;
	return NULL;
}

/**
 * ap_interrupts_available(): Test if AP interrupts are available.
 *
 * Returns 1 if AP interrupts are available.
 */
static int ap_interrupts_available(void)
{
	return test_facility(65);
}

/**
 * ap_configuration_available(): Test if AP configuration
 * information is available.
 *
 * Returns 1 if AP configuration information is available.
 */
static int ap_configuration_available(void)
{
	return test_facility(12);
}

/**
 * ap_apft_available(): Test if AP facilities test (APFT)
 * facility is available.
 *
 * Returns 1 if APFT is is available.
 */
static int ap_apft_available(void)
{
	return test_facility(15);
}

/*
 * ap_qact_available(): Test if the PQAP(QACT) subfunction is available.
 *
 * Returns 1 if the QACT subfunction is available.
 */
static inline int ap_qact_available(void)
{
	if (ap_configuration)
		return ap_configuration->qact;
	return 0;
}

/*
 * ap_query_configuration(): Fetch cryptographic config info
 *
 * Returns the ap configuration info fetched via PQAP(QCI).
 * On success 0 is returned, on failure a negative errno
 * is returned, e.g. if the PQAP(QCI) instruction is not
 * available, the return value will be -EOPNOTSUPP.
 */
static inline int ap_query_configuration(struct ap_config_info *info)
{
	if (!ap_configuration_available())
		return -EOPNOTSUPP;
	if (!info)
		return -EINVAL;
	return ap_qci(info);
}

/**
 * ap_init_configuration(): Allocate and query configuration array.
 */
static void ap_init_configuration(void)
{
	if (!ap_configuration_available())
		return;

	ap_configuration = kzalloc(sizeof(*ap_configuration), GFP_KERNEL);
	if (!ap_configuration)
		return;
	if (ap_query_configuration(ap_configuration) != 0) {
		kfree(ap_configuration);
		ap_configuration = NULL;
		return;
	}
}

/*
 * ap_test_config(): helper function to extract the nrth bit
 *		     within the unsigned int array field.
 */
static inline int ap_test_config(unsigned int *field, unsigned int nr)
{
	return ap_test_bit((field + (nr >> 5)), (nr & 0x1f));
}

/*
 * ap_test_config_card_id(): Test, whether an AP card ID is configured.
 * @id AP card ID
 *
 * Returns 0 if the card is not configured
 *	   1 if the card is configured or
 *	     if the configuration information is not available
 */
static inline int ap_test_config_card_id(unsigned int id)
{
	if (!ap_configuration)	/* QCI not supported */
		/* only ids 0...3F may be probed */
		return id < 0x40 ? 1 : 0;
	return ap_test_config(ap_configuration->apm, id);
}

/*
 * ap_test_config_usage_domain(): Test, whether an AP usage domain
 * is configured.
 * @domain AP usage domain ID
 *
 * Returns 0 if the usage domain is not configured
 *	   1 if the usage domain is configured or
 *	     if the configuration information is not available
 */
int ap_test_config_usage_domain(unsigned int domain)
{
	if (!ap_configuration)	/* QCI not supported */
		return domain < 16;
	return ap_test_config(ap_configuration->aqm, domain);
}
EXPORT_SYMBOL(ap_test_config_usage_domain);

/*
 * ap_test_config_ctrl_domain(): Test, whether an AP control domain
 * is configured.
 * @domain AP control domain ID
 *
 * Returns 1 if the control domain is configured
 *	   0 in all other cases
 */
int ap_test_config_ctrl_domain(unsigned int domain)
{
	if (!ap_configuration)	/* QCI not supported */
		return 0;
	return ap_test_config(ap_configuration->adm, domain);
}
EXPORT_SYMBOL(ap_test_config_ctrl_domain);

/**
 * ap_query_queue(): Check if an AP queue is available.
 * @qid: The AP queue number
 * @queue_depth: Pointer to queue depth value
 * @device_type: Pointer to device type value
 * @facilities: Pointer to facility indicator
 */
static int ap_query_queue(ap_qid_t qid, int *queue_depth, int *device_type,
			  unsigned int *facilities)
{
	struct ap_queue_status status;
	unsigned long info;
	int nd;

	if (!ap_test_config_card_id(AP_QID_CARD(qid)))
		return -ENODEV;

	status = ap_test_queue(qid, ap_apft_available(), &info);
	switch (status.response_code) {
	case AP_RESPONSE_NORMAL:
		*queue_depth = (int)(info & 0xff);
		*device_type = (int)((info >> 24) & 0xff);
		*facilities = (unsigned int)(info >> 32);
		/* Update maximum domain id */
		nd = (info >> 16) & 0xff;
		/* if N bit is available, z13 and newer */
		if ((info & (1UL << 57)) && nd > 0)
			ap_max_domain_id = nd;
		else /* older machine types */
			ap_max_domain_id = 15;
		switch (*device_type) {
			/* For CEX2 and CEX3 the available functions
			 * are not reflected by the facilities bits.
			 * Instead it is coded into the type. So here
			 * modify the function bits based on the type.
			 */
		case AP_DEVICE_TYPE_CEX2A:
		case AP_DEVICE_TYPE_CEX3A:
			*facilities |= 0x08000000;
			break;
		case AP_DEVICE_TYPE_CEX2C:
		case AP_DEVICE_TYPE_CEX3C:
			*facilities |= 0x10000000;
			break;
		default:
			break;
		}
		return 0;
	case AP_RESPONSE_Q_NOT_AVAIL:
	case AP_RESPONSE_DECONFIGURED:
	case AP_RESPONSE_CHECKSTOPPED:
	case AP_RESPONSE_INVALID_ADDRESS:
		return -ENODEV;
	case AP_RESPONSE_RESET_IN_PROGRESS:
	case AP_RESPONSE_OTHERWISE_CHANGED:
	case AP_RESPONSE_BUSY:
		return -EBUSY;
	default:
		BUG();
	}
}

void ap_wait(enum ap_wait wait)
{
	ktime_t hr_time;

	switch (wait) {
	case AP_WAIT_AGAIN:
	case AP_WAIT_INTERRUPT:
		if (ap_using_interrupts())
			break;
		if (ap_poll_kthread) {
			wake_up(&ap_poll_wait);
			break;
		}
		/* Fall through */
	case AP_WAIT_TIMEOUT:
		spin_lock_bh(&ap_poll_timer_lock);
		if (!hrtimer_is_queued(&ap_poll_timer)) {
			hr_time = poll_timeout;
			hrtimer_forward_now(&ap_poll_timer, hr_time);
			hrtimer_restart(&ap_poll_timer);
		}
		spin_unlock_bh(&ap_poll_timer_lock);
		break;
	case AP_WAIT_NONE:
	default:
		break;
	}
}

/**
 * ap_request_timeout(): Handling of request timeouts
 * @t: timer making this callback
 *
 * Handles request timeouts.
 */
void ap_request_timeout(struct timer_list *t)
{
	struct ap_queue *aq = from_timer(aq, t, timeout);

	if (ap_suspend_flag)
		return;
	spin_lock_bh(&aq->lock);
	ap_wait(ap_sm_event(aq, AP_EVENT_TIMEOUT));
	spin_unlock_bh(&aq->lock);
}

/**
 * ap_poll_timeout(): AP receive polling for finished AP requests.
 * @unused: Unused pointer.
 *
 * Schedules the AP tasklet using a high resolution timer.
 */
static enum hrtimer_restart ap_poll_timeout(struct hrtimer *unused)
{
	if (!ap_suspend_flag)
		tasklet_schedule(&ap_tasklet);
	return HRTIMER_NORESTART;
}

/**
 * ap_interrupt_handler() - Schedule ap_tasklet on interrupt
 * @airq: pointer to adapter interrupt descriptor
 */
static void ap_interrupt_handler(struct airq_struct *airq, bool floating)
{
	inc_irq_stat(IRQIO_APB);
	if (!ap_suspend_flag)
		tasklet_schedule(&ap_tasklet);
}

/**
 * ap_tasklet_fn(): Tasklet to poll all AP devices.
 * @dummy: Unused variable
 *
 * Poll all AP devices on the bus.
 */
static void ap_tasklet_fn(unsigned long dummy)
{
	struct ap_card *ac;
	struct ap_queue *aq;
	enum ap_wait wait = AP_WAIT_NONE;

	/* Reset the indicator if interrupts are used. Thus new interrupts can
	 * be received. Doing it in the beginning of the tasklet is therefor
	 * important that no requests on any AP get lost.
	 */
	if (ap_using_interrupts())
		xchg(ap_airq.lsi_ptr, 0);

	spin_lock_bh(&ap_list_lock);
	for_each_ap_card(ac) {
		for_each_ap_queue(aq, ac) {
			spin_lock_bh(&aq->lock);
			wait = min(wait, ap_sm_event_loop(aq, AP_EVENT_POLL));
			spin_unlock_bh(&aq->lock);
		}
	}
	spin_unlock_bh(&ap_list_lock);

	ap_wait(wait);
}

static int ap_pending_requests(void)
{
	struct ap_card *ac;
	struct ap_queue *aq;

	spin_lock_bh(&ap_list_lock);
	for_each_ap_card(ac) {
		for_each_ap_queue(aq, ac) {
			if (aq->queue_count == 0)
				continue;
			spin_unlock_bh(&ap_list_lock);
			return 1;
		}
	}
	spin_unlock_bh(&ap_list_lock);
	return 0;
}

/**
 * ap_poll_thread(): Thread that polls for finished requests.
 * @data: Unused pointer
 *
 * AP bus poll thread. The purpose of this thread is to poll for
 * finished requests in a loop if there is a "free" cpu - that is
 * a cpu that doesn't have anything better to do. The polling stops
 * as soon as there is another task or if all messages have been
 * delivered.
 */
static int ap_poll_thread(void *data)
{
	DECLARE_WAITQUEUE(wait, current);

	set_user_nice(current, MAX_NICE);
	set_freezable();
	while (!kthread_should_stop()) {
		add_wait_queue(&ap_poll_wait, &wait);
		set_current_state(TASK_INTERRUPTIBLE);
		if (ap_suspend_flag || !ap_pending_requests()) {
			schedule();
			try_to_freeze();
		}
		set_current_state(TASK_RUNNING);
		remove_wait_queue(&ap_poll_wait, &wait);
		if (need_resched()) {
			schedule();
			try_to_freeze();
			continue;
		}
		ap_tasklet_fn(0);
	}

	return 0;
}

static int ap_poll_thread_start(void)
{
	int rc;

	if (ap_using_interrupts() || ap_poll_kthread)
		return 0;
	mutex_lock(&ap_poll_thread_mutex);
	ap_poll_kthread = kthread_run(ap_poll_thread, NULL, "appoll");
	rc = PTR_ERR_OR_ZERO(ap_poll_kthread);
	if (rc)
		ap_poll_kthread = NULL;
	mutex_unlock(&ap_poll_thread_mutex);
	return rc;
}

static void ap_poll_thread_stop(void)
{
	if (!ap_poll_kthread)
		return;
	mutex_lock(&ap_poll_thread_mutex);
	kthread_stop(ap_poll_kthread);
	ap_poll_kthread = NULL;
	mutex_unlock(&ap_poll_thread_mutex);
}

#define is_card_dev(x) ((x)->parent == ap_root_device)
#define is_queue_dev(x) ((x)->parent != ap_root_device)

/**
 * ap_bus_match()
 * @dev: Pointer to device
 * @drv: Pointer to device_driver
 *
 * AP bus driver registration/unregistration.
 */
static int ap_bus_match(struct device *dev, struct device_driver *drv)
{
	struct ap_driver *ap_drv = to_ap_drv(drv);
	struct ap_device_id *id;

	/*
	 * Compare device type of the device with the list of
	 * supported types of the device_driver.
	 */
	for (id = ap_drv->ids; id->match_flags; id++) {
		if (is_card_dev(dev) &&
		    id->match_flags & AP_DEVICE_ID_MATCH_CARD_TYPE &&
		    id->dev_type == to_ap_dev(dev)->device_type)
			return 1;
		if (is_queue_dev(dev) &&
		    id->match_flags & AP_DEVICE_ID_MATCH_QUEUE_TYPE &&
		    id->dev_type == to_ap_dev(dev)->device_type)
			return 1;
	}
	return 0;
}

/**
 * ap_uevent(): Uevent function for AP devices.
 * @dev: Pointer to device
 * @env: Pointer to kobj_uevent_env
 *
 * It sets up a single environment variable DEV_TYPE which contains the
 * hardware device type.
 */
static int ap_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	struct ap_device *ap_dev = to_ap_dev(dev);
	int retval = 0;

	if (!ap_dev)
		return -ENODEV;

	/* Set up DEV_TYPE environment variable. */
	retval = add_uevent_var(env, "DEV_TYPE=%04X", ap_dev->device_type);
	if (retval)
		return retval;

	/* Add MODALIAS= */
	retval = add_uevent_var(env, "MODALIAS=ap:t%02X", ap_dev->device_type);

	return retval;
}

static int ap_dev_suspend(struct device *dev)
{
	struct ap_device *ap_dev = to_ap_dev(dev);

	if (ap_dev->drv && ap_dev->drv->suspend)
		ap_dev->drv->suspend(ap_dev);
	return 0;
}

static int ap_dev_resume(struct device *dev)
{
	struct ap_device *ap_dev = to_ap_dev(dev);

	if (ap_dev->drv && ap_dev->drv->resume)
		ap_dev->drv->resume(ap_dev);
	return 0;
}

static void ap_bus_suspend(void)
{
	AP_DBF(DBF_DEBUG, "%s running\n", __func__);

	ap_suspend_flag = 1;
	/*
	 * Disable scanning for devices, thus we do not want to scan
	 * for them after removing.
	 */
	flush_work(&ap_scan_work);
	tasklet_disable(&ap_tasklet);
}

static int __ap_card_devices_unregister(struct device *dev, void *dummy)
{
	if (is_card_dev(dev))
		device_unregister(dev);
	return 0;
}

static int __ap_queue_devices_unregister(struct device *dev, void *dummy)
{
	if (is_queue_dev(dev))
		device_unregister(dev);
	return 0;
}

static int __ap_queue_devices_with_id_unregister(struct device *dev, void *data)
{
	if (is_queue_dev(dev) &&
	    AP_QID_CARD(to_ap_queue(dev)->qid) == (int)(long) data)
		device_unregister(dev);
	return 0;
}

static void ap_bus_resume(void)
{
	int rc;

	AP_DBF(DBF_DEBUG, "%s running\n", __func__);

	/* remove all queue devices */
	bus_for_each_dev(&ap_bus_type, NULL, NULL,
			 __ap_queue_devices_unregister);
	/* remove all card devices */
	bus_for_each_dev(&ap_bus_type, NULL, NULL,
			 __ap_card_devices_unregister);

	/* Reset thin interrupt setting */
	if (ap_interrupts_available() && !ap_using_interrupts()) {
		rc = register_adapter_interrupt(&ap_airq);
		ap_airq_flag = (rc == 0);
	}
	if (!ap_interrupts_available() && ap_using_interrupts()) {
		unregister_adapter_interrupt(&ap_airq);
		ap_airq_flag = 0;
	}
	/* Reset domain */
	if (!user_set_domain)
		ap_domain_index = -1;
	/* Get things going again */
	ap_suspend_flag = 0;
	if (ap_airq_flag)
		xchg(ap_airq.lsi_ptr, 0);
	tasklet_enable(&ap_tasklet);
	queue_work(system_long_wq, &ap_scan_work);
}

static int ap_power_event(struct notifier_block *this, unsigned long event,
			  void *ptr)
{
	switch (event) {
	case PM_HIBERNATION_PREPARE:
	case PM_SUSPEND_PREPARE:
		ap_bus_suspend();
		break;
	case PM_POST_HIBERNATION:
	case PM_POST_SUSPEND:
		ap_bus_resume();
		break;
	default:
		break;
	}
	return NOTIFY_DONE;
}
static struct notifier_block ap_power_notifier = {
	.notifier_call = ap_power_event,
};

static SIMPLE_DEV_PM_OPS(ap_bus_pm_ops, ap_dev_suspend, ap_dev_resume);

static struct bus_type ap_bus_type = {
	.name = "ap",
	.match = &ap_bus_match,
	.uevent = &ap_uevent,
	.pm = &ap_bus_pm_ops,
};

static int __ap_revise_reserved(struct device *dev, void *dummy)
{
	int rc, card, queue, devres, drvres;

	if (is_queue_dev(dev)) {
		card = AP_QID_CARD(to_ap_queue(dev)->qid);
		queue = AP_QID_QUEUE(to_ap_queue(dev)->qid);
		mutex_lock(&ap_perms_mutex);
		devres = test_bit_inv(card, ap_perms.apm)
			&& test_bit_inv(queue, ap_perms.aqm);
		mutex_unlock(&ap_perms_mutex);
		drvres = to_ap_drv(dev->driver)->flags
			& AP_DRIVER_FLAG_DEFAULT;
		if (!!devres != !!drvres) {
			AP_DBF(DBF_DEBUG, "reprobing queue=%02x.%04x\n",
			       card, queue);
			rc = device_reprobe(dev);
		}
	}

	return 0;
}

static void ap_bus_revise_bindings(void)
{
	bus_for_each_dev(&ap_bus_type, NULL, NULL, __ap_revise_reserved);
}

int ap_owned_by_def_drv(int card, int queue)
{
	int rc = 0;

	if (card < 0 || card >= AP_DEVICES || queue < 0 || queue >= AP_DOMAINS)
		return -EINVAL;

	mutex_lock(&ap_perms_mutex);

	if (test_bit_inv(card, ap_perms.apm)
	    && test_bit_inv(queue, ap_perms.aqm))
		rc = 1;

	mutex_unlock(&ap_perms_mutex);

	return rc;
}
EXPORT_SYMBOL(ap_owned_by_def_drv);

int ap_apqn_in_matrix_owned_by_def_drv(unsigned long *apm,
				       unsigned long *aqm)
{
	int card, queue, rc = 0;

	mutex_lock(&ap_perms_mutex);

	for (card = 0; !rc && card < AP_DEVICES; card++)
		if (test_bit_inv(card, apm) &&
		    test_bit_inv(card, ap_perms.apm))
			for (queue = 0; !rc && queue < AP_DOMAINS; queue++)
				if (test_bit_inv(queue, aqm) &&
				    test_bit_inv(queue, ap_perms.aqm))
					rc = 1;

	mutex_unlock(&ap_perms_mutex);

	return rc;
}
EXPORT_SYMBOL(ap_apqn_in_matrix_owned_by_def_drv);

static int ap_device_probe(struct device *dev)
{
	struct ap_device *ap_dev = to_ap_dev(dev);
	struct ap_driver *ap_drv = to_ap_drv(dev->driver);
	int card, queue, devres, drvres, rc;

	if (is_queue_dev(dev)) {
		/*
		 * If the apqn is marked as reserved/used by ap bus and
		 * default drivers, only probe with drivers with the default
		 * flag set. If it is not marked, only probe with drivers
		 * with the default flag not set.
		 */
		card = AP_QID_CARD(to_ap_queue(dev)->qid);
		queue = AP_QID_QUEUE(to_ap_queue(dev)->qid);
		mutex_lock(&ap_perms_mutex);
		devres = test_bit_inv(card, ap_perms.apm)
			&& test_bit_inv(queue, ap_perms.aqm);
		mutex_unlock(&ap_perms_mutex);
		drvres = ap_drv->flags & AP_DRIVER_FLAG_DEFAULT;
		if (!!devres != !!drvres)
			return -ENODEV;
		/* (re-)init queue's state machine */
		ap_queue_reinit_state(to_ap_queue(dev));
	}

	/* Add queue/card to list of active queues/cards */
	spin_lock_bh(&ap_list_lock);
	if (is_card_dev(dev))
		list_add(&to_ap_card(dev)->list, &ap_card_list);
	else
		list_add(&to_ap_queue(dev)->list,
			 &to_ap_queue(dev)->card->queues);
	spin_unlock_bh(&ap_list_lock);

	ap_dev->drv = ap_drv;
	rc = ap_drv->probe ? ap_drv->probe(ap_dev) : -ENODEV;

	if (rc) {
		spin_lock_bh(&ap_list_lock);
		if (is_card_dev(dev))
			list_del_init(&to_ap_card(dev)->list);
		else
			list_del_init(&to_ap_queue(dev)->list);
		spin_unlock_bh(&ap_list_lock);
		ap_dev->drv = NULL;
	}

	return rc;
}

static int ap_device_remove(struct device *dev)
{
	struct ap_device *ap_dev = to_ap_dev(dev);
	struct ap_driver *ap_drv = ap_dev->drv;

	/* prepare ap queue device removal */
	if (is_queue_dev(dev))
		ap_queue_prepare_remove(to_ap_queue(dev));

	/* driver's chance to clean up gracefully */
	if (ap_drv->remove)
		ap_drv->remove(ap_dev);

	/* now do the ap queue device remove */
	if (is_queue_dev(dev))
		ap_queue_remove(to_ap_queue(dev));

	/* Remove queue/card from list of active queues/cards */
	spin_lock_bh(&ap_list_lock);
	if (is_card_dev(dev))
		list_del_init(&to_ap_card(dev)->list);
	else
		list_del_init(&to_ap_queue(dev)->list);
	spin_unlock_bh(&ap_list_lock);

	return 0;
}

int ap_driver_register(struct ap_driver *ap_drv, struct module *owner,
		       char *name)
{
	struct device_driver *drv = &ap_drv->driver;

	if (!initialised)
		return -ENODEV;

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

void ap_bus_force_rescan(void)
{
	if (ap_suspend_flag)
		return;
	/* processing a asynchronous bus rescan */
	del_timer(&ap_config_timer);
	queue_work(system_long_wq, &ap_scan_work);
	flush_work(&ap_scan_work);
}
EXPORT_SYMBOL(ap_bus_force_rescan);

/*
* A config change has happened, force an ap bus rescan.
*/
void ap_bus_cfg_chg(void)
{
	AP_DBF(DBF_INFO, "%s config change, forcing bus rescan\n", __func__);

	ap_bus_force_rescan();
}

/*
 * hex2bitmap() - parse hex mask string and set bitmap.
 * Valid strings are "0x012345678" with at least one valid hex number.
 * Rest of the bitmap to the right is padded with 0. No spaces allowed
 * within the string, the leading 0x may be omitted.
 * Returns the bitmask with exactly the bits set as given by the hex
 * string (both in big endian order).
 */
static int hex2bitmap(const char *str, unsigned long *bitmap, int bits)
{
	int i, n, b;

	/* bits needs to be a multiple of 8 */
	if (bits & 0x07)
		return -EINVAL;

	if (str[0] == '0' && str[1] == 'x')
		str++;
	if (*str == 'x')
		str++;

	for (i = 0; isxdigit(*str) && i < bits; str++) {
		b = hex_to_bin(*str);
		for (n = 0; n < 4; n++)
			if (b & (0x08 >> n))
				set_bit_inv(i + n, bitmap);
		i += 4;
	}

	if (*str == '\n')
		str++;
	if (*str)
		return -EINVAL;
	return 0;
}

/*
 * modify_bitmap() - parse bitmask argument and modify an existing
 * bit mask accordingly. A concatenation (done with ',') of these
 * terms is recognized:
 *   +<bitnr>[-<bitnr>] or -<bitnr>[-<bitnr>]
 * <bitnr> may be any valid number (hex, decimal or octal) in the range
 * 0...bits-1; the leading + or - is required. Here are some examples:
 *   +0-15,+32,-128,-0xFF
 *   -0-255,+1-16,+0x128
 *   +1,+2,+3,+4,-5,-7-10
 * Returns the new bitmap after all changes have been applied. Every
 * positive value in the string will set a bit and every negative value
 * in the string will clear a bit. As a bit may be touched more than once,
 * the last 'operation' wins:
 * +0-255,-128 = first bits 0-255 will be set, then bit 128 will be
 * cleared again. All other bits are unmodified.
 */
static int modify_bitmap(const char *str, unsigned long *bitmap, int bits)
{
	int a, i, z;
	char *np, sign;

	/* bits needs to be a multiple of 8 */
	if (bits & 0x07)
		return -EINVAL;

	while (*str) {
		sign = *str++;
		if (sign != '+' && sign != '-')
			return -EINVAL;
		a = z = simple_strtoul(str, &np, 0);
		if (str == np || a >= bits)
			return -EINVAL;
		str = np;
		if (*str == '-') {
			z = simple_strtoul(++str, &np, 0);
			if (str == np || a > z || z >= bits)
				return -EINVAL;
			str = np;
		}
		for (i = a; i <= z; i++)
			if (sign == '+')
				set_bit_inv(i, bitmap);
			else
				clear_bit_inv(i, bitmap);
		while (*str == ',' || *str == '\n')
			str++;
	}

	return 0;
}

int ap_parse_mask_str(const char *str,
		      unsigned long *bitmap, int bits,
		      struct mutex *lock)
{
	unsigned long *newmap, size;
	int rc;

	/* bits needs to be a multiple of 8 */
	if (bits & 0x07)
		return -EINVAL;

	size = BITS_TO_LONGS(bits)*sizeof(unsigned long);
	newmap = kmalloc(size, GFP_KERNEL);
	if (!newmap)
		return -ENOMEM;
	if (mutex_lock_interruptible(lock)) {
		kfree(newmap);
		return -ERESTARTSYS;
	}

	if (*str == '+' || *str == '-') {
		memcpy(newmap, bitmap, size);
		rc = modify_bitmap(str, newmap, bits);
	} else {
		memset(newmap, 0, size);
		rc = hex2bitmap(str, newmap, bits);
	}
	if (rc == 0)
		memcpy(bitmap, newmap, size);
	mutex_unlock(lock);
	kfree(newmap);
	return rc;
}
EXPORT_SYMBOL(ap_parse_mask_str);

/*
 * AP bus attributes.
 */

static ssize_t ap_domain_show(struct bus_type *bus, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", ap_domain_index);
}

static ssize_t ap_domain_store(struct bus_type *bus,
			       const char *buf, size_t count)
{
	int domain;

	if (sscanf(buf, "%i\n", &domain) != 1 ||
	    domain < 0 || domain > ap_max_domain_id ||
	    !test_bit_inv(domain, ap_perms.aqm))
		return -EINVAL;
	spin_lock_bh(&ap_domain_lock);
	ap_domain_index = domain;
	spin_unlock_bh(&ap_domain_lock);

	AP_DBF(DBF_DEBUG, "stored new default domain=%d\n", domain);

	return count;
}

static BUS_ATTR_RW(ap_domain);

static ssize_t ap_control_domain_mask_show(struct bus_type *bus, char *buf)
{
	if (!ap_configuration)	/* QCI not supported */
		return snprintf(buf, PAGE_SIZE, "not supported\n");

	return snprintf(buf, PAGE_SIZE,
			"0x%08x%08x%08x%08x%08x%08x%08x%08x\n",
			ap_configuration->adm[0], ap_configuration->adm[1],
			ap_configuration->adm[2], ap_configuration->adm[3],
			ap_configuration->adm[4], ap_configuration->adm[5],
			ap_configuration->adm[6], ap_configuration->adm[7]);
}

static BUS_ATTR_RO(ap_control_domain_mask);

static ssize_t ap_usage_domain_mask_show(struct bus_type *bus, char *buf)
{
	if (!ap_configuration)	/* QCI not supported */
		return snprintf(buf, PAGE_SIZE, "not supported\n");

	return snprintf(buf, PAGE_SIZE,
			"0x%08x%08x%08x%08x%08x%08x%08x%08x\n",
			ap_configuration->aqm[0], ap_configuration->aqm[1],
			ap_configuration->aqm[2], ap_configuration->aqm[3],
			ap_configuration->aqm[4], ap_configuration->aqm[5],
			ap_configuration->aqm[6], ap_configuration->aqm[7]);
}

static BUS_ATTR_RO(ap_usage_domain_mask);

static ssize_t ap_adapter_mask_show(struct bus_type *bus, char *buf)
{
	if (!ap_configuration)	/* QCI not supported */
		return snprintf(buf, PAGE_SIZE, "not supported\n");

	return snprintf(buf, PAGE_SIZE,
			"0x%08x%08x%08x%08x%08x%08x%08x%08x\n",
			ap_configuration->apm[0], ap_configuration->apm[1],
			ap_configuration->apm[2], ap_configuration->apm[3],
			ap_configuration->apm[4], ap_configuration->apm[5],
			ap_configuration->apm[6], ap_configuration->apm[7]);
}

static BUS_ATTR_RO(ap_adapter_mask);

static ssize_t ap_interrupts_show(struct bus_type *bus, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n",
			ap_using_interrupts() ? 1 : 0);
}

static BUS_ATTR_RO(ap_interrupts);

static ssize_t config_time_show(struct bus_type *bus, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", ap_config_time);
}

static ssize_t config_time_store(struct bus_type *bus,
				 const char *buf, size_t count)
{
	int time;

	if (sscanf(buf, "%d\n", &time) != 1 || time < 5 || time > 120)
		return -EINVAL;
	ap_config_time = time;
	mod_timer(&ap_config_timer, jiffies + ap_config_time * HZ);
	return count;
}

static BUS_ATTR_RW(config_time);

static ssize_t poll_thread_show(struct bus_type *bus, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", ap_poll_kthread ? 1 : 0);
}

static ssize_t poll_thread_store(struct bus_type *bus,
				 const char *buf, size_t count)
{
	int flag, rc;

	if (sscanf(buf, "%d\n", &flag) != 1)
		return -EINVAL;
	if (flag) {
		rc = ap_poll_thread_start();
		if (rc)
			count = rc;
	} else
		ap_poll_thread_stop();
	return count;
}

static BUS_ATTR_RW(poll_thread);

static ssize_t poll_timeout_show(struct bus_type *bus, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%llu\n", poll_timeout);
}

static ssize_t poll_timeout_store(struct bus_type *bus, const char *buf,
				  size_t count)
{
	unsigned long long time;
	ktime_t hr_time;

	/* 120 seconds = maximum poll interval */
	if (sscanf(buf, "%llu\n", &time) != 1 || time < 1 ||
	    time > 120000000000ULL)
		return -EINVAL;
	poll_timeout = time;
	hr_time = poll_timeout;

	spin_lock_bh(&ap_poll_timer_lock);
	hrtimer_cancel(&ap_poll_timer);
	hrtimer_set_expires(&ap_poll_timer, hr_time);
	hrtimer_start_expires(&ap_poll_timer, HRTIMER_MODE_ABS);
	spin_unlock_bh(&ap_poll_timer_lock);

	return count;
}

static BUS_ATTR_RW(poll_timeout);

static ssize_t ap_max_domain_id_show(struct bus_type *bus, char *buf)
{
	int max_domain_id;

	if (ap_configuration)
		max_domain_id = ap_max_domain_id ? : -1;
	else
		max_domain_id = 15;
	return snprintf(buf, PAGE_SIZE, "%d\n", max_domain_id);
}

static BUS_ATTR_RO(ap_max_domain_id);

static ssize_t apmask_show(struct bus_type *bus, char *buf)
{
	int rc;

	if (mutex_lock_interruptible(&ap_perms_mutex))
		return -ERESTARTSYS;
	rc = snprintf(buf, PAGE_SIZE,
		      "0x%016lx%016lx%016lx%016lx\n",
		      ap_perms.apm[0], ap_perms.apm[1],
		      ap_perms.apm[2], ap_perms.apm[3]);
	mutex_unlock(&ap_perms_mutex);

	return rc;
}

static ssize_t apmask_store(struct bus_type *bus, const char *buf,
			    size_t count)
{
	int rc;

	rc = ap_parse_mask_str(buf, ap_perms.apm, AP_DEVICES, &ap_perms_mutex);
	if (rc)
		return rc;

	ap_bus_revise_bindings();

	return count;
}

static BUS_ATTR_RW(apmask);

static ssize_t aqmask_show(struct bus_type *bus, char *buf)
{
	int rc;

	if (mutex_lock_interruptible(&ap_perms_mutex))
		return -ERESTARTSYS;
	rc = snprintf(buf, PAGE_SIZE,
		      "0x%016lx%016lx%016lx%016lx\n",
		      ap_perms.aqm[0], ap_perms.aqm[1],
		      ap_perms.aqm[2], ap_perms.aqm[3]);
	mutex_unlock(&ap_perms_mutex);

	return rc;
}

static ssize_t aqmask_store(struct bus_type *bus, const char *buf,
			    size_t count)
{
	int rc;

	rc = ap_parse_mask_str(buf, ap_perms.aqm, AP_DOMAINS, &ap_perms_mutex);
	if (rc)
		return rc;

	ap_bus_revise_bindings();

	return count;
}

static BUS_ATTR_RW(aqmask);

static struct bus_attribute *const ap_bus_attrs[] = {
	&bus_attr_ap_domain,
	&bus_attr_ap_control_domain_mask,
	&bus_attr_ap_usage_domain_mask,
	&bus_attr_ap_adapter_mask,
	&bus_attr_config_time,
	&bus_attr_poll_thread,
	&bus_attr_ap_interrupts,
	&bus_attr_poll_timeout,
	&bus_attr_ap_max_domain_id,
	&bus_attr_apmask,
	&bus_attr_aqmask,
	NULL,
};

/**
 * ap_select_domain(): Select an AP domain if possible and we haven't
 * already done so before.
 */
static void ap_select_domain(void)
{
	int count, max_count, best_domain;
	struct ap_queue_status status;
	int i, j;

	/*
	 * We want to use a single domain. Either the one specified with
	 * the "domain=" parameter or the domain with the maximum number
	 * of devices.
	 */
	spin_lock_bh(&ap_domain_lock);
	if (ap_domain_index >= 0) {
		/* Domain has already been selected. */
		spin_unlock_bh(&ap_domain_lock);
		return;
	}
	best_domain = -1;
	max_count = 0;
	for (i = 0; i < AP_DOMAINS; i++) {
		if (!ap_test_config_usage_domain(i) ||
		    !test_bit_inv(i, ap_perms.aqm))
			continue;
		count = 0;
		for (j = 0; j < AP_DEVICES; j++) {
			if (!ap_test_config_card_id(j))
				continue;
			status = ap_test_queue(AP_MKQID(j, i),
					       ap_apft_available(),
					       NULL);
			if (status.response_code != AP_RESPONSE_NORMAL)
				continue;
			count++;
		}
		if (count > max_count) {
			max_count = count;
			best_domain = i;
		}
	}
	if (best_domain >= 0) {
		ap_domain_index = best_domain;
		AP_DBF(DBF_DEBUG, "new ap_domain_index=%d\n", ap_domain_index);
	}
	spin_unlock_bh(&ap_domain_lock);
}

/*
 * This function checks the type and returns either 0 for not
 * supported or the highest compatible type value (which may
 * include the input type value).
 */
static int ap_get_compatible_type(ap_qid_t qid, int rawtype, unsigned int func)
{
	int comp_type = 0;

	/* < CEX2A is not supported */
	if (rawtype < AP_DEVICE_TYPE_CEX2A)
		return 0;
	/* up to CEX6 known and fully supported */
	if (rawtype <= AP_DEVICE_TYPE_CEX6)
		return rawtype;
	/*
	 * unknown new type > CEX6, check for compatibility
	 * to the highest known and supported type which is
	 * currently CEX6 with the help of the QACT function.
	 */
	if (ap_qact_available()) {
		struct ap_queue_status status;
		union ap_qact_ap_info apinfo = {0};

		apinfo.mode = (func >> 26) & 0x07;
		apinfo.cat = AP_DEVICE_TYPE_CEX6;
		status = ap_qact(qid, 0, &apinfo);
		if (status.response_code == AP_RESPONSE_NORMAL
		    && apinfo.cat >= AP_DEVICE_TYPE_CEX2A
		    && apinfo.cat <= AP_DEVICE_TYPE_CEX6)
			comp_type = apinfo.cat;
	}
	if (!comp_type)
		AP_DBF(DBF_WARN, "queue=%02x.%04x unable to map type %d\n",
		       AP_QID_CARD(qid), AP_QID_QUEUE(qid), rawtype);
	else if (comp_type != rawtype)
		AP_DBF(DBF_INFO, "queue=%02x.%04x map type %d to %d\n",
		       AP_QID_CARD(qid), AP_QID_QUEUE(qid), rawtype, comp_type);
	return comp_type;
}

/*
 * Helper function to be used with bus_find_dev
 * matches for the card device with the given id
 */
static int __match_card_device_with_id(struct device *dev, void *data)
{
	return is_card_dev(dev) && to_ap_card(dev)->id == (int)(long) data;
}

/*
 * Helper function to be used with bus_find_dev
 * matches for the queue device with a given qid
 */
static int __match_queue_device_with_qid(struct device *dev, void *data)
{
	return is_queue_dev(dev) && to_ap_queue(dev)->qid == (int)(long) data;
}

/*
 * Helper function to be used with bus_find_dev
 * matches any queue device with given queue id
 */
static int __match_queue_device_with_queue_id(struct device *dev, void *data)
{
	return is_queue_dev(dev)
		&& AP_QID_QUEUE(to_ap_queue(dev)->qid) == (int)(long) data;
}

/*
 * Helper function for ap_scan_bus().
 * Does the scan bus job for the given adapter id.
 */
static void _ap_scan_bus_adapter(int id)
{
	ap_qid_t qid;
	unsigned int func;
	struct ap_card *ac;
	struct device *dev;
	struct ap_queue *aq;
	int rc, dom, depth, type, comp_type, borked;

	/* check if there is a card device registered with this id */
	dev = bus_find_device(&ap_bus_type, NULL,
			      (void *)(long) id,
			      __match_card_device_with_id);
	ac = dev ? to_ap_card(dev) : NULL;
	if (!ap_test_config_card_id(id)) {
		if (dev) {
			/* Card device has been removed from configuration */
			bus_for_each_dev(&ap_bus_type, NULL,
					 (void *)(long) id,
					 __ap_queue_devices_with_id_unregister);
			device_unregister(dev);
			put_device(dev);
		}
		return;
	}

	/*
	 * This card id is enabled in the configuration. If we already have
	 * a card device with this id, check if type and functions are still
	 * the very same. Also verify that at least one queue is available.
	 */
	if (ac) {
		/* find the first valid queue */
		for (dom = 0; dom < AP_DOMAINS; dom++) {
			qid = AP_MKQID(id, dom);
			if (ap_query_queue(qid, &depth, &type, &func) == 0)
				break;
		}
		borked = 0;
		if (dom >= AP_DOMAINS) {
			/* no accessible queue on this card */
			borked = 1;
		} else if (ac->raw_hwtype != type) {
			/* card type has changed */
			AP_DBF(DBF_INFO, "card=%02x type changed.\n", id);
			borked = 1;
		} else if (ac->functions != func) {
			/* card functions have changed */
			AP_DBF(DBF_INFO, "card=%02x functions changed.\n", id);
			borked = 1;
		}
		if (borked) {
			/* unregister card device and associated queues */
			bus_for_each_dev(&ap_bus_type, NULL,
					 (void *)(long) id,
					 __ap_queue_devices_with_id_unregister);
			device_unregister(dev);
			put_device(dev);
			/* go back if there is no valid queue on this card */
			if (dom >= AP_DOMAINS)
				return;
			ac = NULL;
		}
	}

	/*
	 * Go through all possible queue ids. Check and maybe create or release
	 * queue devices for this card. If there exists no card device yet,
	 * create a card device also.
	 */
	for (dom = 0; dom < AP_DOMAINS; dom++) {
		qid = AP_MKQID(id, dom);
		dev = bus_find_device(&ap_bus_type, NULL,
				      (void *)(long) qid,
				      __match_queue_device_with_qid);
		aq = dev ? to_ap_queue(dev) : NULL;
		if (!ap_test_config_usage_domain(dom)) {
			if (dev) {
				/* Queue device exists but has been
				 * removed from configuration.
				 */
				device_unregister(dev);
				put_device(dev);
			}
			continue;
		}
		/* try to fetch infos about this queue */
		rc = ap_query_queue(qid, &depth, &type, &func);
		if (dev) {
			if (rc == -ENODEV)
				borked = 1;
			else {
				spin_lock_bh(&aq->lock);
				borked = aq->state == AP_STATE_BORKED;
				spin_unlock_bh(&aq->lock);
			}
			if (borked) {
				/* Remove broken device */
				AP_DBF(DBF_DEBUG,
				       "removing broken queue=%02x.%04x\n",
				       id, dom);
				device_unregister(dev);
			}
			put_device(dev);
			continue;
		}
		if (rc)
			continue;
		/* a new queue device is needed, check out comp type */
		comp_type = ap_get_compatible_type(qid, type, func);
		if (!comp_type)
			continue;
		/* maybe a card device needs to be created first */
		if (!ac) {
			ac = ap_card_create(id, depth, type, comp_type, func);
			if (!ac)
				continue;
			ac->ap_dev.device.bus = &ap_bus_type;
			ac->ap_dev.device.parent = ap_root_device;
			dev_set_name(&ac->ap_dev.device, "card%02x", id);
			/* Register card device with AP bus */
			rc = device_register(&ac->ap_dev.device);
			if (rc) {
				put_device(&ac->ap_dev.device);
				ac = NULL;
				break;
			}
			/* get it and thus adjust reference counter */
			get_device(&ac->ap_dev.device);
		}
		/* now create the new queue device */
		aq = ap_queue_create(qid, comp_type);
		if (!aq)
			continue;
		aq->card = ac;
		aq->ap_dev.device.bus = &ap_bus_type;
		aq->ap_dev.device.parent = &ac->ap_dev.device;
		dev_set_name(&aq->ap_dev.device, "%02x.%04x", id, dom);
		/* Register queue device */
		rc = device_register(&aq->ap_dev.device);
		if (rc) {
			put_device(&aq->ap_dev.device);
			continue;
		}
	} /* end domain loop */

	if (ac)
		put_device(&ac->ap_dev.device);
}

/**
 * ap_scan_bus(): Scan the AP bus for new devices
 * Runs periodically, workqueue timer (ap_config_time)
 */
static void ap_scan_bus(struct work_struct *unused)
{
	int id;

	AP_DBF(DBF_DEBUG, "%s running\n", __func__);

	ap_query_configuration(ap_configuration);
	ap_select_domain();

	/* loop over all possible adapters */
	for (id = 0; id < AP_DEVICES; id++)
		_ap_scan_bus_adapter(id);

	/* check if there is at least one queue available with default domain */
	if (ap_domain_index >= 0) {
		struct device *dev =
			bus_find_device(&ap_bus_type, NULL,
					(void *)(long) ap_domain_index,
					__match_queue_device_with_queue_id);
		if (dev)
			put_device(dev);
		else
			AP_DBF(DBF_INFO,
			       "no queue device with default domain %d available\n",
			       ap_domain_index);
	}

	mod_timer(&ap_config_timer, jiffies + ap_config_time * HZ);
}

static void ap_config_timeout(struct timer_list *unused)
{
	if (ap_suspend_flag)
		return;
	queue_work(system_long_wq, &ap_scan_work);
}

static int __init ap_debug_init(void)
{
	ap_dbf_info = debug_register("ap", 1, 1,
				     DBF_MAX_SPRINTF_ARGS * sizeof(long));
	debug_register_view(ap_dbf_info, &debug_sprintf_view);
	debug_set_level(ap_dbf_info, DBF_ERR);

	return 0;
}

static void __init ap_perms_init(void)
{
	/* all resources useable if no kernel parameter string given */
	memset(&ap_perms.ioctlm, 0xFF, sizeof(ap_perms.ioctlm));
	memset(&ap_perms.apm, 0xFF, sizeof(ap_perms.apm));
	memset(&ap_perms.aqm, 0xFF, sizeof(ap_perms.aqm));

	/* apm kernel parameter string */
	if (apm_str) {
		memset(&ap_perms.apm, 0, sizeof(ap_perms.apm));
		ap_parse_mask_str(apm_str, ap_perms.apm, AP_DEVICES,
				  &ap_perms_mutex);
	}

	/* aqm kernel parameter string */
	if (aqm_str) {
		memset(&ap_perms.aqm, 0, sizeof(ap_perms.aqm));
		ap_parse_mask_str(aqm_str, ap_perms.aqm, AP_DOMAINS,
				  &ap_perms_mutex);
	}
}

/**
 * ap_module_init(): The module initialization code.
 *
 * Initializes the module.
 */
static int __init ap_module_init(void)
{
	int max_domain_id;
	int rc, i;

	rc = ap_debug_init();
	if (rc)
		return rc;

	if (!ap_instructions_available()) {
		pr_warn("The hardware system does not support AP instructions\n");
		return -ENODEV;
	}

	/* set up the AP permissions (ioctls, ap and aq masks) */
	ap_perms_init();

	/* Get AP configuration data if available */
	ap_init_configuration();

	if (ap_configuration)
		max_domain_id =
			ap_max_domain_id ? ap_max_domain_id : AP_DOMAINS - 1;
	else
		max_domain_id = 15;
	if (ap_domain_index < -1 || ap_domain_index > max_domain_id ||
	    (ap_domain_index >= 0 &&
	     !test_bit_inv(ap_domain_index, ap_perms.aqm))) {
		pr_warn("%d is not a valid cryptographic domain\n",
			ap_domain_index);
		ap_domain_index = -1;
	}
	/* In resume callback we need to know if the user had set the domain.
	 * If so, we can not just reset it.
	 */
	if (ap_domain_index >= 0)
		user_set_domain = 1;

	if (ap_interrupts_available()) {
		rc = register_adapter_interrupt(&ap_airq);
		ap_airq_flag = (rc == 0);
	}

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
	ap_root_device = root_device_register("ap");
	rc = PTR_ERR_OR_ZERO(ap_root_device);
	if (rc)
		goto out_bus;

	/* Setup the AP bus rescan timer. */
	timer_setup(&ap_config_timer, ap_config_timeout, 0);

	/*
	 * Setup the high resultion poll timer.
	 * If we are running under z/VM adjust polling to z/VM polling rate.
	 */
	if (MACHINE_IS_VM)
		poll_timeout = 1500000;
	spin_lock_init(&ap_poll_timer_lock);
	hrtimer_init(&ap_poll_timer, CLOCK_MONOTONIC, HRTIMER_MODE_ABS);
	ap_poll_timer.function = ap_poll_timeout;

	/* Start the low priority AP bus poll thread. */
	if (ap_thread_flag) {
		rc = ap_poll_thread_start();
		if (rc)
			goto out_work;
	}

	rc = register_pm_notifier(&ap_power_notifier);
	if (rc)
		goto out_pm;

	queue_work(system_long_wq, &ap_scan_work);
	initialised = true;

	return 0;

out_pm:
	ap_poll_thread_stop();
out_work:
	hrtimer_cancel(&ap_poll_timer);
	root_device_unregister(ap_root_device);
out_bus:
	while (i--)
		bus_remove_file(&ap_bus_type, ap_bus_attrs[i]);
	bus_unregister(&ap_bus_type);
out:
	if (ap_using_interrupts())
		unregister_adapter_interrupt(&ap_airq);
	kfree(ap_configuration);
	return rc;
}
device_initcall(ap_module_init);
