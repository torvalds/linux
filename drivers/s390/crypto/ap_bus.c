// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright IBM Corp. 2006, 2021
 * Author(s): Cornelia Huck <cornelia.huck@de.ibm.com>
 *	      Martin Schwidefsky <schwidefsky@de.ibm.com>
 *	      Ralph Wuerthner <rwuerthn@de.ibm.com>
 *	      Felix Beck <felix.beck@de.ibm.com>
 *	      Holger Dengler <hd@linux.vnet.ibm.com>
 *	      Harald Freudenberger <freude@linux.ibm.com>
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
#include <linux/freezer.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/slab.h>
#include <linux/notifier.h>
#include <linux/kthread.h>
#include <linux/mutex.h>
#include <asm/airq.h>
#include <asm/tpi.h>
#include <linux/atomic.h>
#include <asm/isc.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>
#include <asm/facility.h>
#include <linux/crypto.h>
#include <linux/mod_devicetable.h>
#include <linux/debugfs.h>
#include <linux/ctype.h>
#include <linux/module.h>

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

static int ap_useirq = 1;
module_param_named(useirq, ap_useirq, int, 0440);
MODULE_PARM_DESC(useirq, "Use interrupt if available, default is 1 (on).");

atomic_t ap_max_msg_size = ATOMIC_INIT(AP_DEFAULT_MAX_MSG_SIZE);
EXPORT_SYMBOL(ap_max_msg_size);

static struct device *ap_root_device;

/* Hashtable of all queue devices on the AP bus */
DEFINE_HASHTABLE(ap_queues, 8);
/* lock used for the ap_queues hashtable */
DEFINE_SPINLOCK(ap_queues_lock);

/* Default permissions (ioctl, card and domain masking) */
struct ap_perms ap_perms;
EXPORT_SYMBOL(ap_perms);
DEFINE_MUTEX(ap_perms_mutex);
EXPORT_SYMBOL(ap_perms_mutex);

/* # of bus scans since init */
static atomic64_t ap_scan_bus_count;

/* # of bindings complete since init */
static atomic64_t ap_bindings_complete_count = ATOMIC64_INIT(0);

/* completion for initial APQN bindings complete */
static DECLARE_COMPLETION(ap_init_apqn_bindings_complete);

static struct ap_config_info *ap_qci_info;
static struct ap_config_info *ap_qci_info_old;

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
static DECLARE_TASKLET_OLD(ap_tasklet, ap_tasklet_fn);
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

/* Maximum domain id, if not given via qci */
static int ap_max_domain_id = 15;
/* Maximum adapter id, if not given via qci */
static int ap_max_adapter_id = 63;

static struct bus_type ap_bus_type;

/* Adapter interrupt definitions */
static void ap_interrupt_handler(struct airq_struct *airq,
				 struct tpi_info *tpi_info);

static bool ap_irq_flag;

static struct airq_struct ap_airq = {
	.handler = ap_interrupt_handler,
	.isc = AP_ISC,
};

/**
 * ap_airq_ptr() - Get the address of the adapter interrupt indicator
 *
 * Returns the address of the local-summary-indicator of the adapter
 * interrupt handler for AP, or NULL if adapter interrupts are not
 * available.
 */
void *ap_airq_ptr(void)
{
	if (ap_irq_flag)
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
 * ap_qci_available(): Test if AP configuration
 * information can be queried via QCI subfunction.
 *
 * Returns 1 if subfunction PQAP(QCI) is available.
 */
static int ap_qci_available(void)
{
	return test_facility(12);
}

/**
 * ap_apft_available(): Test if AP facilities test (APFT)
 * facility is available.
 *
 * Returns 1 if APFT is available.
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
	if (ap_qci_info)
		return ap_qci_info->qact;
	return 0;
}

/*
 * ap_fetch_qci_info(): Fetch cryptographic config info
 *
 * Returns the ap configuration info fetched via PQAP(QCI).
 * On success 0 is returned, on failure a negative errno
 * is returned, e.g. if the PQAP(QCI) instruction is not
 * available, the return value will be -EOPNOTSUPP.
 */
static inline int ap_fetch_qci_info(struct ap_config_info *info)
{
	if (!ap_qci_available())
		return -EOPNOTSUPP;
	if (!info)
		return -EINVAL;
	return ap_qci(info);
}

/**
 * ap_init_qci_info(): Allocate and query qci config info.
 * Does also update the static variables ap_max_domain_id
 * and ap_max_adapter_id if this info is available.
 */
static void __init ap_init_qci_info(void)
{
	if (!ap_qci_available()) {
		AP_DBF_INFO("%s QCI not supported\n", __func__);
		return;
	}

	ap_qci_info = kzalloc(sizeof(*ap_qci_info), GFP_KERNEL);
	if (!ap_qci_info)
		return;
	ap_qci_info_old = kzalloc(sizeof(*ap_qci_info_old), GFP_KERNEL);
	if (!ap_qci_info_old)
		return;
	if (ap_fetch_qci_info(ap_qci_info) != 0) {
		kfree(ap_qci_info);
		kfree(ap_qci_info_old);
		ap_qci_info = NULL;
		ap_qci_info_old = NULL;
		return;
	}
	AP_DBF_INFO("%s successful fetched initial qci info\n", __func__);

	if (ap_qci_info->apxa) {
		if (ap_qci_info->Na) {
			ap_max_adapter_id = ap_qci_info->Na;
			AP_DBF_INFO("%s new ap_max_adapter_id is %d\n",
				    __func__, ap_max_adapter_id);
		}
		if (ap_qci_info->Nd) {
			ap_max_domain_id = ap_qci_info->Nd;
			AP_DBF_INFO("%s new ap_max_domain_id is %d\n",
				    __func__, ap_max_domain_id);
		}
	}

	memcpy(ap_qci_info_old, ap_qci_info, sizeof(*ap_qci_info));
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
 *
 * Returns 0 if the card is not configured
 *	   1 if the card is configured or
 *	     if the configuration information is not available
 */
static inline int ap_test_config_card_id(unsigned int id)
{
	if (id > ap_max_adapter_id)
		return 0;
	if (ap_qci_info)
		return ap_test_config(ap_qci_info->apm, id);
	return 1;
}

/*
 * ap_test_config_usage_domain(): Test, whether an AP usage domain
 * is configured.
 *
 * Returns 0 if the usage domain is not configured
 *	   1 if the usage domain is configured or
 *	     if the configuration information is not available
 */
int ap_test_config_usage_domain(unsigned int domain)
{
	if (domain > ap_max_domain_id)
		return 0;
	if (ap_qci_info)
		return ap_test_config(ap_qci_info->aqm, domain);
	return 1;
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
	if (!ap_qci_info || domain > ap_max_domain_id)
		return 0;
	return ap_test_config(ap_qci_info->adm, domain);
}
EXPORT_SYMBOL(ap_test_config_ctrl_domain);

/*
 * ap_queue_info(): Check and get AP queue info.
 * Returns true if TAPQ succeeded and the info is filled or
 * false otherwise.
 */
static bool ap_queue_info(ap_qid_t qid, int *q_type, unsigned int *q_fac,
			  int *q_depth, int *q_ml, bool *q_decfg, bool *q_cstop)
{
	struct ap_queue_status status;
	union {
		unsigned long value;
		struct {
			unsigned int fac   : 32; /* facility bits */
			unsigned int at	   :  8; /* ap type */
			unsigned int _res1 :  8;
			unsigned int _res2 :  4;
			unsigned int ml	   :  4; /* apxl ml */
			unsigned int _res3 :  4;
			unsigned int qd	   :  4; /* queue depth */
		} tapq_gr2;
	} tapq_info;

	tapq_info.value = 0;

	/* make sure we don't run into a specifiation exception */
	if (AP_QID_CARD(qid) > ap_max_adapter_id ||
	    AP_QID_QUEUE(qid) > ap_max_domain_id)
		return false;

	/* call TAPQ on this APQN */
	status = ap_test_queue(qid, ap_apft_available(), &tapq_info.value);
	switch (status.response_code) {
	case AP_RESPONSE_NORMAL:
	case AP_RESPONSE_RESET_IN_PROGRESS:
	case AP_RESPONSE_DECONFIGURED:
	case AP_RESPONSE_CHECKSTOPPED:
	case AP_RESPONSE_BUSY:
		/*
		 * According to the architecture in all these cases the
		 * info should be filled. All bits 0 is not possible as
		 * there is at least one of the mode bits set.
		 */
		if (WARN_ON_ONCE(!tapq_info.value))
			return false;
		*q_type = tapq_info.tapq_gr2.at;
		*q_fac = tapq_info.tapq_gr2.fac;
		*q_depth = tapq_info.tapq_gr2.qd;
		*q_ml = tapq_info.tapq_gr2.ml;
		*q_decfg = status.response_code == AP_RESPONSE_DECONFIGURED;
		*q_cstop = status.response_code == AP_RESPONSE_CHECKSTOPPED;
		switch (*q_type) {
			/* For CEX2 and CEX3 the available functions
			 * are not reflected by the facilities bits.
			 * Instead it is coded into the type. So here
			 * modify the function bits based on the type.
			 */
		case AP_DEVICE_TYPE_CEX2A:
		case AP_DEVICE_TYPE_CEX3A:
			*q_fac |= 0x08000000;
			break;
		case AP_DEVICE_TYPE_CEX2C:
		case AP_DEVICE_TYPE_CEX3C:
			*q_fac |= 0x10000000;
			break;
		default:
			break;
		}
		return true;
	default:
		/*
		 * A response code which indicates, there is no info available.
		 */
		return false;
	}
}

void ap_wait(enum ap_sm_wait wait)
{
	ktime_t hr_time;

	switch (wait) {
	case AP_SM_WAIT_AGAIN:
	case AP_SM_WAIT_INTERRUPT:
		if (ap_irq_flag)
			break;
		if (ap_poll_kthread) {
			wake_up(&ap_poll_wait);
			break;
		}
		fallthrough;
	case AP_SM_WAIT_TIMEOUT:
		spin_lock_bh(&ap_poll_timer_lock);
		if (!hrtimer_is_queued(&ap_poll_timer)) {
			hr_time = poll_timeout;
			hrtimer_forward_now(&ap_poll_timer, hr_time);
			hrtimer_restart(&ap_poll_timer);
		}
		spin_unlock_bh(&ap_poll_timer_lock);
		break;
	case AP_SM_WAIT_NONE:
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

	spin_lock_bh(&aq->lock);
	ap_wait(ap_sm_event(aq, AP_SM_EVENT_TIMEOUT));
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
	tasklet_schedule(&ap_tasklet);
	return HRTIMER_NORESTART;
}

/**
 * ap_interrupt_handler() - Schedule ap_tasklet on interrupt
 * @airq: pointer to adapter interrupt descriptor
 * @tpi_info: ignored
 */
static void ap_interrupt_handler(struct airq_struct *airq,
				 struct tpi_info *tpi_info)
{
	inc_irq_stat(IRQIO_APB);
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
	int bkt;
	struct ap_queue *aq;
	enum ap_sm_wait wait = AP_SM_WAIT_NONE;

	/* Reset the indicator if interrupts are used. Thus new interrupts can
	 * be received. Doing it in the beginning of the tasklet is therefor
	 * important that no requests on any AP get lost.
	 */
	if (ap_irq_flag)
		xchg(ap_airq.lsi_ptr, 0);

	spin_lock_bh(&ap_queues_lock);
	hash_for_each(ap_queues, bkt, aq, hnode) {
		spin_lock_bh(&aq->lock);
		wait = min(wait, ap_sm_event_loop(aq, AP_SM_EVENT_POLL));
		spin_unlock_bh(&aq->lock);
	}
	spin_unlock_bh(&ap_queues_lock);

	ap_wait(wait);
}

static int ap_pending_requests(void)
{
	int bkt;
	struct ap_queue *aq;

	spin_lock_bh(&ap_queues_lock);
	hash_for_each(ap_queues, bkt, aq, hnode) {
		if (aq->queue_count == 0)
			continue;
		spin_unlock_bh(&ap_queues_lock);
		return 1;
	}
	spin_unlock_bh(&ap_queues_lock);
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
		if (!ap_pending_requests()) {
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

	if (ap_irq_flag || ap_poll_kthread)
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
	int rc = 0;
	struct ap_device *ap_dev = to_ap_dev(dev);

	/* Uevents from ap bus core don't need extensions to the env */
	if (dev == ap_root_device)
		return 0;

	if (is_card_dev(dev)) {
		struct ap_card *ac = to_ap_card(&ap_dev->device);

		/* Set up DEV_TYPE environment variable. */
		rc = add_uevent_var(env, "DEV_TYPE=%04X", ap_dev->device_type);
		if (rc)
			return rc;
		/* Add MODALIAS= */
		rc = add_uevent_var(env, "MODALIAS=ap:t%02X", ap_dev->device_type);
		if (rc)
			return rc;

		/* Add MODE=<accel|cca|ep11> */
		if (ap_test_bit(&ac->functions, AP_FUNC_ACCEL))
			rc = add_uevent_var(env, "MODE=accel");
		else if (ap_test_bit(&ac->functions, AP_FUNC_COPRO))
			rc = add_uevent_var(env, "MODE=cca");
		else if (ap_test_bit(&ac->functions, AP_FUNC_EP11))
			rc = add_uevent_var(env, "MODE=ep11");
		if (rc)
			return rc;
	} else {
		struct ap_queue *aq = to_ap_queue(&ap_dev->device);

		/* Add MODE=<accel|cca|ep11> */
		if (ap_test_bit(&aq->card->functions, AP_FUNC_ACCEL))
			rc = add_uevent_var(env, "MODE=accel");
		else if (ap_test_bit(&aq->card->functions, AP_FUNC_COPRO))
			rc = add_uevent_var(env, "MODE=cca");
		else if (ap_test_bit(&aq->card->functions, AP_FUNC_EP11))
			rc = add_uevent_var(env, "MODE=ep11");
		if (rc)
			return rc;
	}

	return 0;
}

static void ap_send_init_scan_done_uevent(void)
{
	char *envp[] = { "INITSCAN=done", NULL };

	kobject_uevent_env(&ap_root_device->kobj, KOBJ_CHANGE, envp);
}

static void ap_send_bindings_complete_uevent(void)
{
	char buf[32];
	char *envp[] = { "BINDINGS=complete", buf, NULL };

	snprintf(buf, sizeof(buf), "COMPLETECOUNT=%llu",
		 atomic64_inc_return(&ap_bindings_complete_count));
	kobject_uevent_env(&ap_root_device->kobj, KOBJ_CHANGE, envp);
}

void ap_send_config_uevent(struct ap_device *ap_dev, bool cfg)
{
	char buf[16];
	char *envp[] = { buf, NULL };

	snprintf(buf, sizeof(buf), "CONFIG=%d", cfg ? 1 : 0);

	kobject_uevent_env(&ap_dev->device.kobj, KOBJ_CHANGE, envp);
}
EXPORT_SYMBOL(ap_send_config_uevent);

void ap_send_online_uevent(struct ap_device *ap_dev, int online)
{
	char buf[16];
	char *envp[] = { buf, NULL };

	snprintf(buf, sizeof(buf), "ONLINE=%d", online ? 1 : 0);

	kobject_uevent_env(&ap_dev->device.kobj, KOBJ_CHANGE, envp);
}
EXPORT_SYMBOL(ap_send_online_uevent);

static void ap_send_mask_changed_uevent(unsigned long *newapm,
					unsigned long *newaqm)
{
	char buf[100];
	char *envp[] = { buf, NULL };

	if (newapm)
		snprintf(buf, sizeof(buf),
			 "APMASK=0x%016lx%016lx%016lx%016lx\n",
			 newapm[0], newapm[1], newapm[2], newapm[3]);
	else
		snprintf(buf, sizeof(buf),
			 "AQMASK=0x%016lx%016lx%016lx%016lx\n",
			 newaqm[0], newaqm[1], newaqm[2], newaqm[3]);

	kobject_uevent_env(&ap_root_device->kobj, KOBJ_CHANGE, envp);
}

/*
 * calc # of bound APQNs
 */

struct __ap_calc_ctrs {
	unsigned int apqns;
	unsigned int bound;
};

static int __ap_calc_helper(struct device *dev, void *arg)
{
	struct __ap_calc_ctrs *pctrs = (struct __ap_calc_ctrs *)arg;

	if (is_queue_dev(dev)) {
		pctrs->apqns++;
		if (dev->driver)
			pctrs->bound++;
	}

	return 0;
}

static void ap_calc_bound_apqns(unsigned int *apqns, unsigned int *bound)
{
	struct __ap_calc_ctrs ctrs;

	memset(&ctrs, 0, sizeof(ctrs));
	bus_for_each_dev(&ap_bus_type, NULL, (void *)&ctrs, __ap_calc_helper);

	*apqns = ctrs.apqns;
	*bound = ctrs.bound;
}

/*
 * After initial ap bus scan do check if all existing APQNs are
 * bound to device drivers.
 */
static void ap_check_bindings_complete(void)
{
	unsigned int apqns, bound;

	if (atomic64_read(&ap_scan_bus_count) >= 1) {
		ap_calc_bound_apqns(&apqns, &bound);
		if (bound == apqns) {
			if (!completion_done(&ap_init_apqn_bindings_complete)) {
				complete_all(&ap_init_apqn_bindings_complete);
				AP_DBF_INFO("%s complete\n", __func__);
			}
			ap_send_bindings_complete_uevent();
		}
	}
}

/*
 * Interface to wait for the AP bus to have done one initial ap bus
 * scan and all detected APQNs have been bound to device drivers.
 * If these both conditions are not fulfilled, this function blocks
 * on a condition with wait_for_completion_interruptible_timeout().
 * If these both conditions are fulfilled (before the timeout hits)
 * the return value is 0. If the timeout (in jiffies) hits instead
 * -ETIME is returned. On failures negative return values are
 * returned to the caller.
 */
int ap_wait_init_apqn_bindings_complete(unsigned long timeout)
{
	long l;

	if (completion_done(&ap_init_apqn_bindings_complete))
		return 0;

	if (timeout)
		l = wait_for_completion_interruptible_timeout(
			&ap_init_apqn_bindings_complete, timeout);
	else
		l = wait_for_completion_interruptible(
			&ap_init_apqn_bindings_complete);
	if (l < 0)
		return l == -ERESTARTSYS ? -EINTR : l;
	else if (l == 0 && timeout)
		return -ETIME;

	return 0;
}
EXPORT_SYMBOL(ap_wait_init_apqn_bindings_complete);

static int __ap_queue_devices_with_id_unregister(struct device *dev, void *data)
{
	if (is_queue_dev(dev) &&
	    AP_QID_CARD(to_ap_queue(dev)->qid) == (int)(long)data)
		device_unregister(dev);
	return 0;
}

static int __ap_revise_reserved(struct device *dev, void *dummy)
{
	int rc, card, queue, devres, drvres;

	if (is_queue_dev(dev)) {
		card = AP_QID_CARD(to_ap_queue(dev)->qid);
		queue = AP_QID_QUEUE(to_ap_queue(dev)->qid);
		mutex_lock(&ap_perms_mutex);
		devres = test_bit_inv(card, ap_perms.apm) &&
			test_bit_inv(queue, ap_perms.aqm);
		mutex_unlock(&ap_perms_mutex);
		drvres = to_ap_drv(dev->driver)->flags
			& AP_DRIVER_FLAG_DEFAULT;
		if (!!devres != !!drvres) {
			AP_DBF_DBG("%s reprobing queue=%02x.%04x\n",
				   __func__, card, queue);
			rc = device_reprobe(dev);
			if (rc)
				AP_DBF_WARN("%s reprobing queue=%02x.%04x failed\n",
					    __func__, card, queue);
		}
	}

	return 0;
}

static void ap_bus_revise_bindings(void)
{
	bus_for_each_dev(&ap_bus_type, NULL, NULL, __ap_revise_reserved);
}

/**
 * ap_owned_by_def_drv: indicates whether an AP adapter is reserved for the
 *			default host driver or not.
 * @card: the APID of the adapter card to check
 * @queue: the APQI of the queue to check
 *
 * Note: the ap_perms_mutex must be locked by the caller of this function.
 *
 * Return: an int specifying whether the AP adapter is reserved for the host (1)
 *	   or not (0).
 */
int ap_owned_by_def_drv(int card, int queue)
{
	int rc = 0;

	if (card < 0 || card >= AP_DEVICES || queue < 0 || queue >= AP_DOMAINS)
		return -EINVAL;

	if (test_bit_inv(card, ap_perms.apm) &&
	    test_bit_inv(queue, ap_perms.aqm))
		rc = 1;

	return rc;
}
EXPORT_SYMBOL(ap_owned_by_def_drv);

/**
 * ap_apqn_in_matrix_owned_by_def_drv: indicates whether every APQN contained in
 *				       a set is reserved for the host drivers
 *				       or not.
 * @apm: a bitmap specifying a set of APIDs comprising the APQNs to check
 * @aqm: a bitmap specifying a set of APQIs comprising the APQNs to check
 *
 * Note: the ap_perms_mutex must be locked by the caller of this function.
 *
 * Return: an int specifying whether each APQN is reserved for the host (1) or
 *	   not (0)
 */
int ap_apqn_in_matrix_owned_by_def_drv(unsigned long *apm,
				       unsigned long *aqm)
{
	int card, queue, rc = 0;

	for (card = 0; !rc && card < AP_DEVICES; card++)
		if (test_bit_inv(card, apm) &&
		    test_bit_inv(card, ap_perms.apm))
			for (queue = 0; !rc && queue < AP_DOMAINS; queue++)
				if (test_bit_inv(queue, aqm) &&
				    test_bit_inv(queue, ap_perms.aqm))
					rc = 1;

	return rc;
}
EXPORT_SYMBOL(ap_apqn_in_matrix_owned_by_def_drv);

static int ap_device_probe(struct device *dev)
{
	struct ap_device *ap_dev = to_ap_dev(dev);
	struct ap_driver *ap_drv = to_ap_drv(dev->driver);
	int card, queue, devres, drvres, rc = -ENODEV;

	if (!get_device(dev))
		return rc;

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
		devres = test_bit_inv(card, ap_perms.apm) &&
			test_bit_inv(queue, ap_perms.aqm);
		mutex_unlock(&ap_perms_mutex);
		drvres = ap_drv->flags & AP_DRIVER_FLAG_DEFAULT;
		if (!!devres != !!drvres)
			goto out;
	}

	/* Add queue/card to list of active queues/cards */
	spin_lock_bh(&ap_queues_lock);
	if (is_queue_dev(dev))
		hash_add(ap_queues, &to_ap_queue(dev)->hnode,
			 to_ap_queue(dev)->qid);
	spin_unlock_bh(&ap_queues_lock);

	rc = ap_drv->probe ? ap_drv->probe(ap_dev) : -ENODEV;

	if (rc) {
		spin_lock_bh(&ap_queues_lock);
		if (is_queue_dev(dev))
			hash_del(&to_ap_queue(dev)->hnode);
		spin_unlock_bh(&ap_queues_lock);
	} else {
		ap_check_bindings_complete();
	}

out:
	if (rc)
		put_device(dev);
	return rc;
}

static void ap_device_remove(struct device *dev)
{
	struct ap_device *ap_dev = to_ap_dev(dev);
	struct ap_driver *ap_drv = to_ap_drv(dev->driver);

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
	spin_lock_bh(&ap_queues_lock);
	if (is_queue_dev(dev))
		hash_del(&to_ap_queue(dev)->hnode);
	spin_unlock_bh(&ap_queues_lock);

	put_device(dev);
}

struct ap_queue *ap_get_qdev(ap_qid_t qid)
{
	int bkt;
	struct ap_queue *aq;

	spin_lock_bh(&ap_queues_lock);
	hash_for_each(ap_queues, bkt, aq, hnode) {
		if (aq->qid == qid) {
			get_device(&aq->ap_dev.device);
			spin_unlock_bh(&ap_queues_lock);
			return aq;
		}
	}
	spin_unlock_bh(&ap_queues_lock);

	return NULL;
}
EXPORT_SYMBOL(ap_get_qdev);

int ap_driver_register(struct ap_driver *ap_drv, struct module *owner,
		       char *name)
{
	struct device_driver *drv = &ap_drv->driver;

	drv->bus = &ap_bus_type;
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
	AP_DBF_DBG("%s config change, forcing bus rescan\n", __func__);

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

static int ap_parse_bitmap_str(const char *str, unsigned long *bitmap, int bits,
			       unsigned long *newmap)
{
	unsigned long size;
	int rc;

	size = BITS_TO_LONGS(bits) * sizeof(unsigned long);
	if (*str == '+' || *str == '-') {
		memcpy(newmap, bitmap, size);
		rc = modify_bitmap(str, newmap, bits);
	} else {
		memset(newmap, 0, size);
		rc = hex2bitmap(str, newmap, bits);
	}
	return rc;
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

	size = BITS_TO_LONGS(bits) * sizeof(unsigned long);
	newmap = kmalloc(size, GFP_KERNEL);
	if (!newmap)
		return -ENOMEM;
	if (mutex_lock_interruptible(lock)) {
		kfree(newmap);
		return -ERESTARTSYS;
	}
	rc = ap_parse_bitmap_str(str, bitmap, bits, newmap);
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
	return scnprintf(buf, PAGE_SIZE, "%d\n", ap_domain_index);
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

	AP_DBF_INFO("%s stored new default domain=%d\n",
		    __func__, domain);

	return count;
}

static BUS_ATTR_RW(ap_domain);

static ssize_t ap_control_domain_mask_show(struct bus_type *bus, char *buf)
{
	if (!ap_qci_info)	/* QCI not supported */
		return scnprintf(buf, PAGE_SIZE, "not supported\n");

	return scnprintf(buf, PAGE_SIZE,
			 "0x%08x%08x%08x%08x%08x%08x%08x%08x\n",
			 ap_qci_info->adm[0], ap_qci_info->adm[1],
			 ap_qci_info->adm[2], ap_qci_info->adm[3],
			 ap_qci_info->adm[4], ap_qci_info->adm[5],
			 ap_qci_info->adm[6], ap_qci_info->adm[7]);
}

static BUS_ATTR_RO(ap_control_domain_mask);

static ssize_t ap_usage_domain_mask_show(struct bus_type *bus, char *buf)
{
	if (!ap_qci_info)	/* QCI not supported */
		return scnprintf(buf, PAGE_SIZE, "not supported\n");

	return scnprintf(buf, PAGE_SIZE,
			 "0x%08x%08x%08x%08x%08x%08x%08x%08x\n",
			 ap_qci_info->aqm[0], ap_qci_info->aqm[1],
			 ap_qci_info->aqm[2], ap_qci_info->aqm[3],
			 ap_qci_info->aqm[4], ap_qci_info->aqm[5],
			 ap_qci_info->aqm[6], ap_qci_info->aqm[7]);
}

static BUS_ATTR_RO(ap_usage_domain_mask);

static ssize_t ap_adapter_mask_show(struct bus_type *bus, char *buf)
{
	if (!ap_qci_info)	/* QCI not supported */
		return scnprintf(buf, PAGE_SIZE, "not supported\n");

	return scnprintf(buf, PAGE_SIZE,
			 "0x%08x%08x%08x%08x%08x%08x%08x%08x\n",
			 ap_qci_info->apm[0], ap_qci_info->apm[1],
			 ap_qci_info->apm[2], ap_qci_info->apm[3],
			 ap_qci_info->apm[4], ap_qci_info->apm[5],
			 ap_qci_info->apm[6], ap_qci_info->apm[7]);
}

static BUS_ATTR_RO(ap_adapter_mask);

static ssize_t ap_interrupts_show(struct bus_type *bus, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d\n",
			 ap_irq_flag ? 1 : 0);
}

static BUS_ATTR_RO(ap_interrupts);

static ssize_t config_time_show(struct bus_type *bus, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d\n", ap_config_time);
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
	return scnprintf(buf, PAGE_SIZE, "%d\n", ap_poll_kthread ? 1 : 0);
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
	} else {
		ap_poll_thread_stop();
	}
	return count;
}

static BUS_ATTR_RW(poll_thread);

static ssize_t poll_timeout_show(struct bus_type *bus, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%llu\n", poll_timeout);
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
	return scnprintf(buf, PAGE_SIZE, "%d\n", ap_max_domain_id);
}

static BUS_ATTR_RO(ap_max_domain_id);

static ssize_t ap_max_adapter_id_show(struct bus_type *bus, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d\n", ap_max_adapter_id);
}

static BUS_ATTR_RO(ap_max_adapter_id);

static ssize_t apmask_show(struct bus_type *bus, char *buf)
{
	int rc;

	if (mutex_lock_interruptible(&ap_perms_mutex))
		return -ERESTARTSYS;
	rc = scnprintf(buf, PAGE_SIZE,
		       "0x%016lx%016lx%016lx%016lx\n",
		       ap_perms.apm[0], ap_perms.apm[1],
		       ap_perms.apm[2], ap_perms.apm[3]);
	mutex_unlock(&ap_perms_mutex);

	return rc;
}

static int __verify_card_reservations(struct device_driver *drv, void *data)
{
	int rc = 0;
	struct ap_driver *ap_drv = to_ap_drv(drv);
	unsigned long *newapm = (unsigned long *)data;

	/*
	 * increase the driver's module refcounter to be sure it is not
	 * going away when we invoke the callback function.
	 */
	if (!try_module_get(drv->owner))
		return 0;

	if (ap_drv->in_use) {
		rc = ap_drv->in_use(newapm, ap_perms.aqm);
		if (rc)
			rc = -EBUSY;
	}

	/* release the driver's module */
	module_put(drv->owner);

	return rc;
}

static int apmask_commit(unsigned long *newapm)
{
	int rc;
	unsigned long reserved[BITS_TO_LONGS(AP_DEVICES)];

	/*
	 * Check if any bits in the apmask have been set which will
	 * result in queues being removed from non-default drivers
	 */
	if (bitmap_andnot(reserved, newapm, ap_perms.apm, AP_DEVICES)) {
		rc = bus_for_each_drv(&ap_bus_type, NULL, reserved,
				      __verify_card_reservations);
		if (rc)
			return rc;
	}

	memcpy(ap_perms.apm, newapm, APMASKSIZE);

	return 0;
}

static ssize_t apmask_store(struct bus_type *bus, const char *buf,
			    size_t count)
{
	int rc, changes = 0;
	DECLARE_BITMAP(newapm, AP_DEVICES);

	if (mutex_lock_interruptible(&ap_perms_mutex))
		return -ERESTARTSYS;

	rc = ap_parse_bitmap_str(buf, ap_perms.apm, AP_DEVICES, newapm);
	if (rc)
		goto done;

	changes = memcmp(ap_perms.apm, newapm, APMASKSIZE);
	if (changes)
		rc = apmask_commit(newapm);

done:
	mutex_unlock(&ap_perms_mutex);
	if (rc)
		return rc;

	if (changes) {
		ap_bus_revise_bindings();
		ap_send_mask_changed_uevent(newapm, NULL);
	}

	return count;
}

static BUS_ATTR_RW(apmask);

static ssize_t aqmask_show(struct bus_type *bus, char *buf)
{
	int rc;

	if (mutex_lock_interruptible(&ap_perms_mutex))
		return -ERESTARTSYS;
	rc = scnprintf(buf, PAGE_SIZE,
		       "0x%016lx%016lx%016lx%016lx\n",
		       ap_perms.aqm[0], ap_perms.aqm[1],
		       ap_perms.aqm[2], ap_perms.aqm[3]);
	mutex_unlock(&ap_perms_mutex);

	return rc;
}

static int __verify_queue_reservations(struct device_driver *drv, void *data)
{
	int rc = 0;
	struct ap_driver *ap_drv = to_ap_drv(drv);
	unsigned long *newaqm = (unsigned long *)data;

	/*
	 * increase the driver's module refcounter to be sure it is not
	 * going away when we invoke the callback function.
	 */
	if (!try_module_get(drv->owner))
		return 0;

	if (ap_drv->in_use) {
		rc = ap_drv->in_use(ap_perms.apm, newaqm);
		if (rc)
			rc = -EBUSY;
	}

	/* release the driver's module */
	module_put(drv->owner);

	return rc;
}

static int aqmask_commit(unsigned long *newaqm)
{
	int rc;
	unsigned long reserved[BITS_TO_LONGS(AP_DOMAINS)];

	/*
	 * Check if any bits in the aqmask have been set which will
	 * result in queues being removed from non-default drivers
	 */
	if (bitmap_andnot(reserved, newaqm, ap_perms.aqm, AP_DOMAINS)) {
		rc = bus_for_each_drv(&ap_bus_type, NULL, reserved,
				      __verify_queue_reservations);
		if (rc)
			return rc;
	}

	memcpy(ap_perms.aqm, newaqm, AQMASKSIZE);

	return 0;
}

static ssize_t aqmask_store(struct bus_type *bus, const char *buf,
			    size_t count)
{
	int rc, changes = 0;
	DECLARE_BITMAP(newaqm, AP_DOMAINS);

	if (mutex_lock_interruptible(&ap_perms_mutex))
		return -ERESTARTSYS;

	rc = ap_parse_bitmap_str(buf, ap_perms.aqm, AP_DOMAINS, newaqm);
	if (rc)
		goto done;

	changes = memcmp(ap_perms.aqm, newaqm, APMASKSIZE);
	if (changes)
		rc = aqmask_commit(newaqm);

done:
	mutex_unlock(&ap_perms_mutex);
	if (rc)
		return rc;

	if (changes) {
		ap_bus_revise_bindings();
		ap_send_mask_changed_uevent(NULL, newaqm);
	}

	return count;
}

static BUS_ATTR_RW(aqmask);

static ssize_t scans_show(struct bus_type *bus, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%llu\n",
			 atomic64_read(&ap_scan_bus_count));
}

static ssize_t scans_store(struct bus_type *bus, const char *buf,
			   size_t count)
{
	AP_DBF_INFO("%s force AP bus rescan\n", __func__);

	ap_bus_force_rescan();

	return count;
}

static BUS_ATTR_RW(scans);

static ssize_t bindings_show(struct bus_type *bus, char *buf)
{
	int rc;
	unsigned int apqns, n;

	ap_calc_bound_apqns(&apqns, &n);
	if (atomic64_read(&ap_scan_bus_count) >= 1 && n == apqns)
		rc = scnprintf(buf, PAGE_SIZE, "%u/%u (complete)\n", n, apqns);
	else
		rc = scnprintf(buf, PAGE_SIZE, "%u/%u\n", n, apqns);

	return rc;
}

static BUS_ATTR_RO(bindings);

static struct attribute *ap_bus_attrs[] = {
	&bus_attr_ap_domain.attr,
	&bus_attr_ap_control_domain_mask.attr,
	&bus_attr_ap_usage_domain_mask.attr,
	&bus_attr_ap_adapter_mask.attr,
	&bus_attr_config_time.attr,
	&bus_attr_poll_thread.attr,
	&bus_attr_ap_interrupts.attr,
	&bus_attr_poll_timeout.attr,
	&bus_attr_ap_max_domain_id.attr,
	&bus_attr_ap_max_adapter_id.attr,
	&bus_attr_apmask.attr,
	&bus_attr_aqmask.attr,
	&bus_attr_scans.attr,
	&bus_attr_bindings.attr,
	NULL,
};
ATTRIBUTE_GROUPS(ap_bus);

static struct bus_type ap_bus_type = {
	.name = "ap",
	.bus_groups = ap_bus_groups,
	.match = &ap_bus_match,
	.uevent = &ap_uevent,
	.probe = ap_device_probe,
	.remove = ap_device_remove,
};

/**
 * ap_select_domain(): Select an AP domain if possible and we haven't
 * already done so before.
 */
static void ap_select_domain(void)
{
	struct ap_queue_status status;
	int card, dom;

	/*
	 * Choose the default domain. Either the one specified with
	 * the "domain=" parameter or the first domain with at least
	 * one valid APQN.
	 */
	spin_lock_bh(&ap_domain_lock);
	if (ap_domain_index >= 0) {
		/* Domain has already been selected. */
		goto out;
	}
	for (dom = 0; dom <= ap_max_domain_id; dom++) {
		if (!ap_test_config_usage_domain(dom) ||
		    !test_bit_inv(dom, ap_perms.aqm))
			continue;
		for (card = 0; card <= ap_max_adapter_id; card++) {
			if (!ap_test_config_card_id(card) ||
			    !test_bit_inv(card, ap_perms.apm))
				continue;
			status = ap_test_queue(AP_MKQID(card, dom),
					       ap_apft_available(),
					       NULL);
			if (status.response_code == AP_RESPONSE_NORMAL)
				break;
		}
		if (card <= ap_max_adapter_id)
			break;
	}
	if (dom <= ap_max_domain_id) {
		ap_domain_index = dom;
		AP_DBF_INFO("%s new default domain is %d\n",
			    __func__, ap_domain_index);
	}
out:
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
	if (rawtype < AP_DEVICE_TYPE_CEX2A) {
		AP_DBF_WARN("%s queue=%02x.%04x unsupported type %d\n",
			    __func__, AP_QID_CARD(qid),
			    AP_QID_QUEUE(qid), rawtype);
		return 0;
	}
	/* up to CEX8 known and fully supported */
	if (rawtype <= AP_DEVICE_TYPE_CEX8)
		return rawtype;
	/*
	 * unknown new type > CEX8, check for compatibility
	 * to the highest known and supported type which is
	 * currently CEX8 with the help of the QACT function.
	 */
	if (ap_qact_available()) {
		struct ap_queue_status status;
		union ap_qact_ap_info apinfo = {0};

		apinfo.mode = (func >> 26) & 0x07;
		apinfo.cat = AP_DEVICE_TYPE_CEX8;
		status = ap_qact(qid, 0, &apinfo);
		if (status.response_code == AP_RESPONSE_NORMAL &&
		    apinfo.cat >= AP_DEVICE_TYPE_CEX2A &&
		    apinfo.cat <= AP_DEVICE_TYPE_CEX8)
			comp_type = apinfo.cat;
	}
	if (!comp_type)
		AP_DBF_WARN("%s queue=%02x.%04x unable to map type %d\n",
			    __func__, AP_QID_CARD(qid),
			    AP_QID_QUEUE(qid), rawtype);
	else if (comp_type != rawtype)
		AP_DBF_INFO("%s queue=%02x.%04x map type %d to %d\n",
			    __func__, AP_QID_CARD(qid), AP_QID_QUEUE(qid),
			    rawtype, comp_type);
	return comp_type;
}

/*
 * Helper function to be used with bus_find_dev
 * matches for the card device with the given id
 */
static int __match_card_device_with_id(struct device *dev, const void *data)
{
	return is_card_dev(dev) && to_ap_card(dev)->id == (int)(long)(void *)data;
}

/*
 * Helper function to be used with bus_find_dev
 * matches for the queue device with a given qid
 */
static int __match_queue_device_with_qid(struct device *dev, const void *data)
{
	return is_queue_dev(dev) && to_ap_queue(dev)->qid == (int)(long)data;
}

/*
 * Helper function to be used with bus_find_dev
 * matches any queue device with given queue id
 */
static int __match_queue_device_with_queue_id(struct device *dev, const void *data)
{
	return is_queue_dev(dev) &&
		AP_QID_QUEUE(to_ap_queue(dev)->qid) == (int)(long)data;
}

/* Helper function for notify_config_changed */
static int __drv_notify_config_changed(struct device_driver *drv, void *data)
{
	struct ap_driver *ap_drv = to_ap_drv(drv);

	if (try_module_get(drv->owner)) {
		if (ap_drv->on_config_changed)
			ap_drv->on_config_changed(ap_qci_info, ap_qci_info_old);
		module_put(drv->owner);
	}

	return 0;
}

/* Notify all drivers about an qci config change */
static inline void notify_config_changed(void)
{
	bus_for_each_drv(&ap_bus_type, NULL, NULL,
			 __drv_notify_config_changed);
}

/* Helper function for notify_scan_complete */
static int __drv_notify_scan_complete(struct device_driver *drv, void *data)
{
	struct ap_driver *ap_drv = to_ap_drv(drv);

	if (try_module_get(drv->owner)) {
		if (ap_drv->on_scan_complete)
			ap_drv->on_scan_complete(ap_qci_info,
						 ap_qci_info_old);
		module_put(drv->owner);
	}

	return 0;
}

/* Notify all drivers about bus scan complete */
static inline void notify_scan_complete(void)
{
	bus_for_each_drv(&ap_bus_type, NULL, NULL,
			 __drv_notify_scan_complete);
}

/*
 * Helper function for ap_scan_bus().
 * Remove card device and associated queue devices.
 */
static inline void ap_scan_rm_card_dev_and_queue_devs(struct ap_card *ac)
{
	bus_for_each_dev(&ap_bus_type, NULL,
			 (void *)(long)ac->id,
			 __ap_queue_devices_with_id_unregister);
	device_unregister(&ac->ap_dev.device);
}

/*
 * Helper function for ap_scan_bus().
 * Does the scan bus job for all the domains within
 * a valid adapter given by an ap_card ptr.
 */
static inline void ap_scan_domains(struct ap_card *ac)
{
	bool decfg, chkstop;
	ap_qid_t qid;
	unsigned int func;
	struct device *dev;
	struct ap_queue *aq;
	int rc, dom, depth, type, ml;

	/*
	 * Go through the configuration for the domains and compare them
	 * to the existing queue devices. Also take care of the config
	 * and error state for the queue devices.
	 */

	for (dom = 0; dom <= ap_max_domain_id; dom++) {
		qid = AP_MKQID(ac->id, dom);
		dev = bus_find_device(&ap_bus_type, NULL,
				      (void *)(long)qid,
				      __match_queue_device_with_qid);
		aq = dev ? to_ap_queue(dev) : NULL;
		if (!ap_test_config_usage_domain(dom)) {
			if (dev) {
				AP_DBF_INFO("%s(%d,%d) not in config anymore, rm queue dev\n",
					    __func__, ac->id, dom);
				device_unregister(dev);
				put_device(dev);
			}
			continue;
		}
		/* domain is valid, get info from this APQN */
		if (!ap_queue_info(qid, &type, &func, &depth,
				   &ml, &decfg, &chkstop)) {
			if (aq) {
				AP_DBF_INFO("%s(%d,%d) queue_info() failed, rm queue dev\n",
					    __func__, ac->id, dom);
				device_unregister(dev);
				put_device(dev);
			}
			continue;
		}
		/* if no queue device exists, create a new one */
		if (!aq) {
			aq = ap_queue_create(qid, ac->ap_dev.device_type);
			if (!aq) {
				AP_DBF_WARN("%s(%d,%d) ap_queue_create() failed\n",
					    __func__, ac->id, dom);
				continue;
			}
			aq->card = ac;
			aq->config = !decfg;
			aq->chkstop = chkstop;
			dev = &aq->ap_dev.device;
			dev->bus = &ap_bus_type;
			dev->parent = &ac->ap_dev.device;
			dev_set_name(dev, "%02x.%04x", ac->id, dom);
			/* register queue device */
			rc = device_register(dev);
			if (rc) {
				AP_DBF_WARN("%s(%d,%d) device_register() failed\n",
					    __func__, ac->id, dom);
				goto put_dev_and_continue;
			}
			/* get it and thus adjust reference counter */
			get_device(dev);
			if (decfg)
				AP_DBF_INFO("%s(%d,%d) new (decfg) queue dev created\n",
					    __func__, ac->id, dom);
			else if (chkstop)
				AP_DBF_INFO("%s(%d,%d) new (chkstop) queue dev created\n",
					    __func__, ac->id, dom);
			else
				AP_DBF_INFO("%s(%d,%d) new queue dev created\n",
					    __func__, ac->id, dom);
			goto put_dev_and_continue;
		}
		/* handle state changes on already existing queue device */
		spin_lock_bh(&aq->lock);
		/* checkstop state */
		if (chkstop && !aq->chkstop) {
			/* checkstop on */
			aq->chkstop = true;
			if (aq->dev_state > AP_DEV_STATE_UNINITIATED) {
				aq->dev_state = AP_DEV_STATE_ERROR;
				aq->last_err_rc = AP_RESPONSE_CHECKSTOPPED;
			}
			spin_unlock_bh(&aq->lock);
			AP_DBF_DBG("%s(%d,%d) queue dev checkstop on\n",
				   __func__, ac->id, dom);
			/* 'receive' pending messages with -EAGAIN */
			ap_flush_queue(aq);
			goto put_dev_and_continue;
		} else if (!chkstop && aq->chkstop) {
			/* checkstop off */
			aq->chkstop = false;
			if (aq->dev_state > AP_DEV_STATE_UNINITIATED) {
				aq->dev_state = AP_DEV_STATE_OPERATING;
				aq->sm_state = AP_SM_STATE_RESET_START;
			}
			spin_unlock_bh(&aq->lock);
			AP_DBF_DBG("%s(%d,%d) queue dev checkstop off\n",
				   __func__, ac->id, dom);
			goto put_dev_and_continue;
		}
		/* config state change */
		if (decfg && aq->config) {
			/* config off this queue device */
			aq->config = false;
			if (aq->dev_state > AP_DEV_STATE_UNINITIATED) {
				aq->dev_state = AP_DEV_STATE_ERROR;
				aq->last_err_rc = AP_RESPONSE_DECONFIGURED;
			}
			spin_unlock_bh(&aq->lock);
			AP_DBF_DBG("%s(%d,%d) queue dev config off\n",
				   __func__, ac->id, dom);
			ap_send_config_uevent(&aq->ap_dev, aq->config);
			/* 'receive' pending messages with -EAGAIN */
			ap_flush_queue(aq);
			goto put_dev_and_continue;
		} else if (!decfg && !aq->config) {
			/* config on this queue device */
			aq->config = true;
			if (aq->dev_state > AP_DEV_STATE_UNINITIATED) {
				aq->dev_state = AP_DEV_STATE_OPERATING;
				aq->sm_state = AP_SM_STATE_RESET_START;
			}
			spin_unlock_bh(&aq->lock);
			AP_DBF_DBG("%s(%d,%d) queue dev config on\n",
				   __func__, ac->id, dom);
			ap_send_config_uevent(&aq->ap_dev, aq->config);
			goto put_dev_and_continue;
		}
		/* handle other error states */
		if (!decfg && aq->dev_state == AP_DEV_STATE_ERROR) {
			spin_unlock_bh(&aq->lock);
			/* 'receive' pending messages with -EAGAIN */
			ap_flush_queue(aq);
			/* re-init (with reset) the queue device */
			ap_queue_init_state(aq);
			AP_DBF_INFO("%s(%d,%d) queue dev reinit enforced\n",
				    __func__, ac->id, dom);
			goto put_dev_and_continue;
		}
		spin_unlock_bh(&aq->lock);
put_dev_and_continue:
		put_device(dev);
	}
}

/*
 * Helper function for ap_scan_bus().
 * Does the scan bus job for the given adapter id.
 */
static inline void ap_scan_adapter(int ap)
{
	bool decfg, chkstop;
	ap_qid_t qid;
	unsigned int func;
	struct device *dev;
	struct ap_card *ac;
	int rc, dom, depth, type, comp_type, ml;

	/* Is there currently a card device for this adapter ? */
	dev = bus_find_device(&ap_bus_type, NULL,
			      (void *)(long)ap,
			      __match_card_device_with_id);
	ac = dev ? to_ap_card(dev) : NULL;

	/* Adapter not in configuration ? */
	if (!ap_test_config_card_id(ap)) {
		if (ac) {
			AP_DBF_INFO("%s(%d) ap not in config any more, rm card and queue devs\n",
				    __func__, ap);
			ap_scan_rm_card_dev_and_queue_devs(ac);
			put_device(dev);
		}
		return;
	}

	/*
	 * Adapter ap is valid in the current configuration. So do some checks:
	 * If no card device exists, build one. If a card device exists, check
	 * for type and functions changed. For all this we need to find a valid
	 * APQN first.
	 */

	for (dom = 0; dom <= ap_max_domain_id; dom++)
		if (ap_test_config_usage_domain(dom)) {
			qid = AP_MKQID(ap, dom);
			if (ap_queue_info(qid, &type, &func, &depth,
					  &ml, &decfg, &chkstop))
				break;
		}
	if (dom > ap_max_domain_id) {
		/* Could not find a valid APQN for this adapter */
		if (ac) {
			AP_DBF_INFO("%s(%d) no type info (no APQN found), rm card and queue devs\n",
				    __func__, ap);
			ap_scan_rm_card_dev_and_queue_devs(ac);
			put_device(dev);
		} else {
			AP_DBF_DBG("%s(%d) no type info (no APQN found), ignored\n",
				   __func__, ap);
		}
		return;
	}
	if (!type) {
		/* No apdater type info available, an unusable adapter */
		if (ac) {
			AP_DBF_INFO("%s(%d) no valid type (0) info, rm card and queue devs\n",
				    __func__, ap);
			ap_scan_rm_card_dev_and_queue_devs(ac);
			put_device(dev);
		} else {
			AP_DBF_DBG("%s(%d) no valid type (0) info, ignored\n",
				   __func__, ap);
		}
		return;
	}

	if (ac) {
		/* Check APQN against existing card device for changes */
		if (ac->raw_hwtype != type) {
			AP_DBF_INFO("%s(%d) hwtype %d changed, rm card and queue devs\n",
				    __func__, ap, type);
			ap_scan_rm_card_dev_and_queue_devs(ac);
			put_device(dev);
			ac = NULL;
		} else if (ac->functions != func) {
			AP_DBF_INFO("%s(%d) functions 0x%08x changed, rm card and queue devs\n",
				    __func__, ap, type);
			ap_scan_rm_card_dev_and_queue_devs(ac);
			put_device(dev);
			ac = NULL;
		} else {
			/* handle checkstop state change */
			if (chkstop && !ac->chkstop) {
				/* checkstop on */
				ac->chkstop = true;
				AP_DBF_INFO("%s(%d) card dev checkstop on\n",
					    __func__, ap);
			} else if (!chkstop && ac->chkstop) {
				/* checkstop off */
				ac->chkstop = false;
				AP_DBF_INFO("%s(%d) card dev checkstop off\n",
					    __func__, ap);
			}
			/* handle config state change */
			if (decfg && ac->config) {
				ac->config = false;
				AP_DBF_INFO("%s(%d) card dev config off\n",
					    __func__, ap);
				ap_send_config_uevent(&ac->ap_dev, ac->config);
			} else if (!decfg && !ac->config) {
				ac->config = true;
				AP_DBF_INFO("%s(%d) card dev config on\n",
					    __func__, ap);
				ap_send_config_uevent(&ac->ap_dev, ac->config);
			}
		}
	}

	if (!ac) {
		/* Build a new card device */
		comp_type = ap_get_compatible_type(qid, type, func);
		if (!comp_type) {
			AP_DBF_WARN("%s(%d) type %d, can't get compatibility type\n",
				    __func__, ap, type);
			return;
		}
		ac = ap_card_create(ap, depth, type, comp_type, func, ml);
		if (!ac) {
			AP_DBF_WARN("%s(%d) ap_card_create() failed\n",
				    __func__, ap);
			return;
		}
		ac->config = !decfg;
		ac->chkstop = chkstop;
		dev = &ac->ap_dev.device;
		dev->bus = &ap_bus_type;
		dev->parent = ap_root_device;
		dev_set_name(dev, "card%02x", ap);
		/* maybe enlarge ap_max_msg_size to support this card */
		if (ac->maxmsgsize > atomic_read(&ap_max_msg_size)) {
			atomic_set(&ap_max_msg_size, ac->maxmsgsize);
			AP_DBF_INFO("%s(%d) ap_max_msg_size update to %d byte\n",
				    __func__, ap,
				    atomic_read(&ap_max_msg_size));
		}
		/* Register the new card device with AP bus */
		rc = device_register(dev);
		if (rc) {
			AP_DBF_WARN("%s(%d) device_register() failed\n",
				    __func__, ap);
			put_device(dev);
			return;
		}
		/* get it and thus adjust reference counter */
		get_device(dev);
		if (decfg)
			AP_DBF_INFO("%s(%d) new (decfg) card dev type=%d func=0x%08x created\n",
				    __func__, ap, type, func);
		else if (chkstop)
			AP_DBF_INFO("%s(%d) new (chkstop) card dev type=%d func=0x%08x created\n",
				    __func__, ap, type, func);
		else
			AP_DBF_INFO("%s(%d) new card dev type=%d func=0x%08x created\n",
				    __func__, ap, type, func);
	}

	/* Verify the domains and the queue devices for this card */
	ap_scan_domains(ac);

	/* release the card device */
	put_device(&ac->ap_dev.device);
}

/**
 * ap_get_configuration - get the host AP configuration
 *
 * Stores the host AP configuration information returned from the previous call
 * to Query Configuration Information (QCI), then retrieves and stores the
 * current AP configuration returned from QCI.
 *
 * Return: true if the host AP configuration changed between calls to QCI;
 * otherwise, return false.
 */
static bool ap_get_configuration(void)
{
	if (!ap_qci_info)	/* QCI not supported */
		return false;

	memcpy(ap_qci_info_old, ap_qci_info, sizeof(*ap_qci_info));
	ap_fetch_qci_info(ap_qci_info);

	return memcmp(ap_qci_info, ap_qci_info_old,
		      sizeof(struct ap_config_info)) != 0;
}

/**
 * ap_scan_bus(): Scan the AP bus for new devices
 * Runs periodically, workqueue timer (ap_config_time)
 * @unused: Unused pointer.
 */
static void ap_scan_bus(struct work_struct *unused)
{
	int ap, config_changed = 0;

	/* config change notify */
	config_changed = ap_get_configuration();
	if (config_changed)
		notify_config_changed();
	ap_select_domain();

	AP_DBF_DBG("%s running\n", __func__);

	/* loop over all possible adapters */
	for (ap = 0; ap <= ap_max_adapter_id; ap++)
		ap_scan_adapter(ap);

	/* scan complete notify */
	if (config_changed)
		notify_scan_complete();

	/* check if there is at least one queue available with default domain */
	if (ap_domain_index >= 0) {
		struct device *dev =
			bus_find_device(&ap_bus_type, NULL,
					(void *)(long)ap_domain_index,
					__match_queue_device_with_queue_id);
		if (dev)
			put_device(dev);
		else
			AP_DBF_INFO("%s no queue device with default domain %d available\n",
				    __func__, ap_domain_index);
	}

	if (atomic64_inc_return(&ap_scan_bus_count) == 1) {
		AP_DBF_DBG("%s init scan complete\n", __func__);
		ap_send_init_scan_done_uevent();
		ap_check_bindings_complete();
	}

	mod_timer(&ap_config_timer, jiffies + ap_config_time * HZ);
}

static void ap_config_timeout(struct timer_list *unused)
{
	queue_work(system_long_wq, &ap_scan_work);
}

static int __init ap_debug_init(void)
{
	ap_dbf_info = debug_register("ap", 2, 1,
				     DBF_MAX_SPRINTF_ARGS * sizeof(long));
	debug_register_view(ap_dbf_info, &debug_sprintf_view);
	debug_set_level(ap_dbf_info, DBF_ERR);

	return 0;
}

static void __init ap_perms_init(void)
{
	/* all resources usable if no kernel parameter string given */
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
	int rc;

	rc = ap_debug_init();
	if (rc)
		return rc;

	if (!ap_instructions_available()) {
		pr_warn("The hardware system does not support AP instructions\n");
		return -ENODEV;
	}

	/* init ap_queue hashtable */
	hash_init(ap_queues);

	/* set up the AP permissions (ioctls, ap and aq masks) */
	ap_perms_init();

	/* Get AP configuration data if available */
	ap_init_qci_info();

	/* check default domain setting */
	if (ap_domain_index < -1 || ap_domain_index > ap_max_domain_id ||
	    (ap_domain_index >= 0 &&
	     !test_bit_inv(ap_domain_index, ap_perms.aqm))) {
		pr_warn("%d is not a valid cryptographic domain\n",
			ap_domain_index);
		ap_domain_index = -1;
	}

	/* enable interrupts if available */
	if (ap_interrupts_available() && ap_useirq) {
		rc = register_adapter_interrupt(&ap_airq);
		ap_irq_flag = (rc == 0);
	}

	/* Create /sys/bus/ap. */
	rc = bus_register(&ap_bus_type);
	if (rc)
		goto out;

	/* Create /sys/devices/ap. */
	ap_root_device = root_device_register("ap");
	rc = PTR_ERR_OR_ZERO(ap_root_device);
	if (rc)
		goto out_bus;
	ap_root_device->bus = &ap_bus_type;

	/* Setup the AP bus rescan timer. */
	timer_setup(&ap_config_timer, ap_config_timeout, 0);

	/*
	 * Setup the high resultion poll timer.
	 * If we are running under z/VM adjust polling to z/VM polling rate.
	 */
	if (MACHINE_IS_VM)
		poll_timeout = 1500000;
	hrtimer_init(&ap_poll_timer, CLOCK_MONOTONIC, HRTIMER_MODE_ABS);
	ap_poll_timer.function = ap_poll_timeout;

	/* Start the low priority AP bus poll thread. */
	if (ap_thread_flag) {
		rc = ap_poll_thread_start();
		if (rc)
			goto out_work;
	}

	queue_work(system_long_wq, &ap_scan_work);

	return 0;

out_work:
	hrtimer_cancel(&ap_poll_timer);
	root_device_unregister(ap_root_device);
out_bus:
	bus_unregister(&ap_bus_type);
out:
	if (ap_irq_flag)
		unregister_adapter_interrupt(&ap_airq);
	kfree(ap_qci_info);
	return rc;
}
device_initcall(ap_module_init);
