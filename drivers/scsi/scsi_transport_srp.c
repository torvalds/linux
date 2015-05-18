/*
 * SCSI RDMA (SRP) transport class
 *
 * Copyright (C) 2007 FUJITA Tomonori <tomof@acm.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/jiffies.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/delay.h>

#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_transport.h>
#include <scsi/scsi_transport_srp.h>
#include "scsi_priv.h"

struct srp_host_attrs {
	atomic_t next_port_id;
};
#define to_srp_host_attrs(host)	((struct srp_host_attrs *)(host)->shost_data)

#define SRP_HOST_ATTRS 0
#define SRP_RPORT_ATTRS 8

struct srp_internal {
	struct scsi_transport_template t;
	struct srp_function_template *f;

	struct device_attribute *host_attrs[SRP_HOST_ATTRS + 1];

	struct device_attribute *rport_attrs[SRP_RPORT_ATTRS + 1];
	struct transport_container rport_attr_cont;
};

#define to_srp_internal(tmpl) container_of(tmpl, struct srp_internal, t)

#define	dev_to_rport(d)	container_of(d, struct srp_rport, dev)
#define transport_class_to_srp_rport(dev) dev_to_rport((dev)->parent)
static inline struct Scsi_Host *rport_to_shost(struct srp_rport *r)
{
	return dev_to_shost(r->dev.parent);
}

/**
 * srp_tmo_valid() - check timeout combination validity
 * @reconnect_delay: Reconnect delay in seconds.
 * @fast_io_fail_tmo: Fast I/O fail timeout in seconds.
 * @dev_loss_tmo: Device loss timeout in seconds.
 *
 * The combination of the timeout parameters must be such that SCSI commands
 * are finished in a reasonable time. Hence do not allow the fast I/O fail
 * timeout to exceed SCSI_DEVICE_BLOCK_MAX_TIMEOUT nor allow dev_loss_tmo to
 * exceed that limit if failing I/O fast has been disabled. Furthermore, these
 * parameters must be such that multipath can detect failed paths timely.
 * Hence do not allow all three parameters to be disabled simultaneously.
 */
int srp_tmo_valid(int reconnect_delay, int fast_io_fail_tmo, int dev_loss_tmo)
{
	if (reconnect_delay < 0 && fast_io_fail_tmo < 0 && dev_loss_tmo < 0)
		return -EINVAL;
	if (reconnect_delay == 0)
		return -EINVAL;
	if (fast_io_fail_tmo > SCSI_DEVICE_BLOCK_MAX_TIMEOUT)
		return -EINVAL;
	if (fast_io_fail_tmo < 0 &&
	    dev_loss_tmo > SCSI_DEVICE_BLOCK_MAX_TIMEOUT)
		return -EINVAL;
	if (dev_loss_tmo >= LONG_MAX / HZ)
		return -EINVAL;
	if (fast_io_fail_tmo >= 0 && dev_loss_tmo >= 0 &&
	    fast_io_fail_tmo >= dev_loss_tmo)
		return -EINVAL;
	return 0;
}
EXPORT_SYMBOL_GPL(srp_tmo_valid);

static int srp_host_setup(struct transport_container *tc, struct device *dev,
			  struct device *cdev)
{
	struct Scsi_Host *shost = dev_to_shost(dev);
	struct srp_host_attrs *srp_host = to_srp_host_attrs(shost);

	atomic_set(&srp_host->next_port_id, 0);
	return 0;
}

static DECLARE_TRANSPORT_CLASS(srp_host_class, "srp_host", srp_host_setup,
			       NULL, NULL);

static DECLARE_TRANSPORT_CLASS(srp_rport_class, "srp_remote_ports",
			       NULL, NULL, NULL);

#define SRP_PID(p) \
	(p)->port_id[0], (p)->port_id[1], (p)->port_id[2], (p)->port_id[3], \
	(p)->port_id[4], (p)->port_id[5], (p)->port_id[6], (p)->port_id[7], \
	(p)->port_id[8], (p)->port_id[9], (p)->port_id[10], (p)->port_id[11], \
	(p)->port_id[12], (p)->port_id[13], (p)->port_id[14], (p)->port_id[15]

#define SRP_PID_FMT "%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:" \
	"%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x"

static ssize_t
show_srp_rport_id(struct device *dev, struct device_attribute *attr,
		  char *buf)
{
	struct srp_rport *rport = transport_class_to_srp_rport(dev);
	return sprintf(buf, SRP_PID_FMT "\n", SRP_PID(rport));
}

static DEVICE_ATTR(port_id, S_IRUGO, show_srp_rport_id, NULL);

static const struct {
	u32 value;
	char *name;
} srp_rport_role_names[] = {
	{SRP_RPORT_ROLE_INITIATOR, "SRP Initiator"},
	{SRP_RPORT_ROLE_TARGET, "SRP Target"},
};

static ssize_t
show_srp_rport_roles(struct device *dev, struct device_attribute *attr,
		     char *buf)
{
	struct srp_rport *rport = transport_class_to_srp_rport(dev);
	int i;
	char *name = NULL;

	for (i = 0; i < ARRAY_SIZE(srp_rport_role_names); i++)
		if (srp_rport_role_names[i].value == rport->roles) {
			name = srp_rport_role_names[i].name;
			break;
		}
	return sprintf(buf, "%s\n", name ? : "unknown");
}

static DEVICE_ATTR(roles, S_IRUGO, show_srp_rport_roles, NULL);

static ssize_t store_srp_rport_delete(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	struct srp_rport *rport = transport_class_to_srp_rport(dev);
	struct Scsi_Host *shost = dev_to_shost(dev);
	struct srp_internal *i = to_srp_internal(shost->transportt);

	if (i->f->rport_delete) {
		i->f->rport_delete(rport);
		return count;
	} else {
		return -ENOSYS;
	}
}

static DEVICE_ATTR(delete, S_IWUSR, NULL, store_srp_rport_delete);

static ssize_t show_srp_rport_state(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	static const char *const state_name[] = {
		[SRP_RPORT_RUNNING]	= "running",
		[SRP_RPORT_BLOCKED]	= "blocked",
		[SRP_RPORT_FAIL_FAST]	= "fail-fast",
		[SRP_RPORT_LOST]	= "lost",
	};
	struct srp_rport *rport = transport_class_to_srp_rport(dev);
	enum srp_rport_state state = rport->state;

	return sprintf(buf, "%s\n",
		       (unsigned)state < ARRAY_SIZE(state_name) ?
		       state_name[state] : "???");
}

static DEVICE_ATTR(state, S_IRUGO, show_srp_rport_state, NULL);

static ssize_t srp_show_tmo(char *buf, int tmo)
{
	return tmo >= 0 ? sprintf(buf, "%d\n", tmo) : sprintf(buf, "off\n");
}

static int srp_parse_tmo(int *tmo, const char *buf)
{
	int res = 0;

	if (strncmp(buf, "off", 3) != 0)
		res = kstrtoint(buf, 0, tmo);
	else
		*tmo = -1;

	return res;
}

static ssize_t show_reconnect_delay(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct srp_rport *rport = transport_class_to_srp_rport(dev);

	return srp_show_tmo(buf, rport->reconnect_delay);
}

static ssize_t store_reconnect_delay(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, const size_t count)
{
	struct srp_rport *rport = transport_class_to_srp_rport(dev);
	int res, delay;

	res = srp_parse_tmo(&delay, buf);
	if (res)
		goto out;
	res = srp_tmo_valid(delay, rport->fast_io_fail_tmo,
			    rport->dev_loss_tmo);
	if (res)
		goto out;

	if (rport->reconnect_delay <= 0 && delay > 0 &&
	    rport->state != SRP_RPORT_RUNNING) {
		queue_delayed_work(system_long_wq, &rport->reconnect_work,
				   delay * HZ);
	} else if (delay <= 0) {
		cancel_delayed_work(&rport->reconnect_work);
	}
	rport->reconnect_delay = delay;
	res = count;

out:
	return res;
}

static DEVICE_ATTR(reconnect_delay, S_IRUGO | S_IWUSR, show_reconnect_delay,
		   store_reconnect_delay);

static ssize_t show_failed_reconnects(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct srp_rport *rport = transport_class_to_srp_rport(dev);

	return sprintf(buf, "%d\n", rport->failed_reconnects);
}

static DEVICE_ATTR(failed_reconnects, S_IRUGO, show_failed_reconnects, NULL);

static ssize_t show_srp_rport_fast_io_fail_tmo(struct device *dev,
					       struct device_attribute *attr,
					       char *buf)
{
	struct srp_rport *rport = transport_class_to_srp_rport(dev);

	return srp_show_tmo(buf, rport->fast_io_fail_tmo);
}

static ssize_t store_srp_rport_fast_io_fail_tmo(struct device *dev,
						struct device_attribute *attr,
						const char *buf, size_t count)
{
	struct srp_rport *rport = transport_class_to_srp_rport(dev);
	int res;
	int fast_io_fail_tmo;

	res = srp_parse_tmo(&fast_io_fail_tmo, buf);
	if (res)
		goto out;
	res = srp_tmo_valid(rport->reconnect_delay, fast_io_fail_tmo,
			    rport->dev_loss_tmo);
	if (res)
		goto out;
	rport->fast_io_fail_tmo = fast_io_fail_tmo;
	res = count;

out:
	return res;
}

static DEVICE_ATTR(fast_io_fail_tmo, S_IRUGO | S_IWUSR,
		   show_srp_rport_fast_io_fail_tmo,
		   store_srp_rport_fast_io_fail_tmo);

static ssize_t show_srp_rport_dev_loss_tmo(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	struct srp_rport *rport = transport_class_to_srp_rport(dev);

	return srp_show_tmo(buf, rport->dev_loss_tmo);
}

static ssize_t store_srp_rport_dev_loss_tmo(struct device *dev,
					    struct device_attribute *attr,
					    const char *buf, size_t count)
{
	struct srp_rport *rport = transport_class_to_srp_rport(dev);
	int res;
	int dev_loss_tmo;

	res = srp_parse_tmo(&dev_loss_tmo, buf);
	if (res)
		goto out;
	res = srp_tmo_valid(rport->reconnect_delay, rport->fast_io_fail_tmo,
			    dev_loss_tmo);
	if (res)
		goto out;
	rport->dev_loss_tmo = dev_loss_tmo;
	res = count;

out:
	return res;
}

static DEVICE_ATTR(dev_loss_tmo, S_IRUGO | S_IWUSR,
		   show_srp_rport_dev_loss_tmo,
		   store_srp_rport_dev_loss_tmo);

static int srp_rport_set_state(struct srp_rport *rport,
			       enum srp_rport_state new_state)
{
	enum srp_rport_state old_state = rport->state;

	lockdep_assert_held(&rport->mutex);

	switch (new_state) {
	case SRP_RPORT_RUNNING:
		switch (old_state) {
		case SRP_RPORT_LOST:
			goto invalid;
		default:
			break;
		}
		break;
	case SRP_RPORT_BLOCKED:
		switch (old_state) {
		case SRP_RPORT_RUNNING:
			break;
		default:
			goto invalid;
		}
		break;
	case SRP_RPORT_FAIL_FAST:
		switch (old_state) {
		case SRP_RPORT_LOST:
			goto invalid;
		default:
			break;
		}
		break;
	case SRP_RPORT_LOST:
		break;
	}
	rport->state = new_state;
	return 0;

invalid:
	return -EINVAL;
}

/**
 * srp_reconnect_work() - reconnect and schedule a new attempt if necessary
 * @work: Work structure used for scheduling this operation.
 */
static void srp_reconnect_work(struct work_struct *work)
{
	struct srp_rport *rport = container_of(to_delayed_work(work),
					struct srp_rport, reconnect_work);
	struct Scsi_Host *shost = rport_to_shost(rport);
	int delay, res;

	res = srp_reconnect_rport(rport);
	if (res != 0) {
		shost_printk(KERN_ERR, shost,
			     "reconnect attempt %d failed (%d)\n",
			     ++rport->failed_reconnects, res);
		delay = rport->reconnect_delay *
			min(100, max(1, rport->failed_reconnects - 10));
		if (delay > 0)
			queue_delayed_work(system_long_wq,
					   &rport->reconnect_work, delay * HZ);
	}
}

/**
 * scsi_request_fn_active() - number of kernel threads inside scsi_request_fn()
 * @shost: SCSI host for which to count the number of scsi_request_fn() callers.
 *
 * To do: add support for scsi-mq in this function.
 */
static int scsi_request_fn_active(struct Scsi_Host *shost)
{
	struct scsi_device *sdev;
	struct request_queue *q;
	int request_fn_active = 0;

	shost_for_each_device(sdev, shost) {
		q = sdev->request_queue;

		spin_lock_irq(q->queue_lock);
		request_fn_active += q->request_fn_active;
		spin_unlock_irq(q->queue_lock);
	}

	return request_fn_active;
}

/* Wait until ongoing shost->hostt->queuecommand() calls have finished. */
static void srp_wait_for_queuecommand(struct Scsi_Host *shost)
{
	while (scsi_request_fn_active(shost))
		msleep(20);
}

static void __rport_fail_io_fast(struct srp_rport *rport)
{
	struct Scsi_Host *shost = rport_to_shost(rport);
	struct srp_internal *i;

	lockdep_assert_held(&rport->mutex);

	if (srp_rport_set_state(rport, SRP_RPORT_FAIL_FAST))
		return;
	scsi_target_unblock(rport->dev.parent, SDEV_TRANSPORT_OFFLINE);

	/* Involve the LLD if possible to terminate all I/O on the rport. */
	i = to_srp_internal(shost->transportt);
	if (i->f->terminate_rport_io) {
		srp_wait_for_queuecommand(shost);
		i->f->terminate_rport_io(rport);
	}
}

/**
 * rport_fast_io_fail_timedout() - fast I/O failure timeout handler
 * @work: Work structure used for scheduling this operation.
 */
static void rport_fast_io_fail_timedout(struct work_struct *work)
{
	struct srp_rport *rport = container_of(to_delayed_work(work),
					struct srp_rport, fast_io_fail_work);
	struct Scsi_Host *shost = rport_to_shost(rport);

	pr_info("fast_io_fail_tmo expired for SRP %s / %s.\n",
		dev_name(&rport->dev), dev_name(&shost->shost_gendev));

	mutex_lock(&rport->mutex);
	if (rport->state == SRP_RPORT_BLOCKED)
		__rport_fail_io_fast(rport);
	mutex_unlock(&rport->mutex);
}

/**
 * rport_dev_loss_timedout() - device loss timeout handler
 * @work: Work structure used for scheduling this operation.
 */
static void rport_dev_loss_timedout(struct work_struct *work)
{
	struct srp_rport *rport = container_of(to_delayed_work(work),
					struct srp_rport, dev_loss_work);
	struct Scsi_Host *shost = rport_to_shost(rport);
	struct srp_internal *i = to_srp_internal(shost->transportt);

	pr_info("dev_loss_tmo expired for SRP %s / %s.\n",
		dev_name(&rport->dev), dev_name(&shost->shost_gendev));

	mutex_lock(&rport->mutex);
	WARN_ON(srp_rport_set_state(rport, SRP_RPORT_LOST) != 0);
	scsi_target_unblock(rport->dev.parent, SDEV_TRANSPORT_OFFLINE);
	mutex_unlock(&rport->mutex);

	i->f->rport_delete(rport);
}

static void __srp_start_tl_fail_timers(struct srp_rport *rport)
{
	struct Scsi_Host *shost = rport_to_shost(rport);
	int delay, fast_io_fail_tmo, dev_loss_tmo;

	lockdep_assert_held(&rport->mutex);

	delay = rport->reconnect_delay;
	fast_io_fail_tmo = rport->fast_io_fail_tmo;
	dev_loss_tmo = rport->dev_loss_tmo;
	pr_debug("%s current state: %d\n", dev_name(&shost->shost_gendev),
		 rport->state);

	if (rport->state == SRP_RPORT_LOST)
		return;
	if (delay > 0)
		queue_delayed_work(system_long_wq, &rport->reconnect_work,
				   1UL * delay * HZ);
	if ((fast_io_fail_tmo >= 0 || dev_loss_tmo >= 0) &&
	    srp_rport_set_state(rport, SRP_RPORT_BLOCKED) == 0) {
		pr_debug("%s new state: %d\n", dev_name(&shost->shost_gendev),
			 rport->state);
		scsi_target_block(&shost->shost_gendev);
		if (fast_io_fail_tmo >= 0)
			queue_delayed_work(system_long_wq,
					   &rport->fast_io_fail_work,
					   1UL * fast_io_fail_tmo * HZ);
		if (dev_loss_tmo >= 0)
			queue_delayed_work(system_long_wq,
					   &rport->dev_loss_work,
					   1UL * dev_loss_tmo * HZ);
	}
}

/**
 * srp_start_tl_fail_timers() - start the transport layer failure timers
 * @rport: SRP target port.
 *
 * Start the transport layer fast I/O failure and device loss timers. Do not
 * modify a timer that was already started.
 */
void srp_start_tl_fail_timers(struct srp_rport *rport)
{
	mutex_lock(&rport->mutex);
	__srp_start_tl_fail_timers(rport);
	mutex_unlock(&rport->mutex);
}
EXPORT_SYMBOL(srp_start_tl_fail_timers);

/**
 * srp_reconnect_rport() - reconnect to an SRP target port
 * @rport: SRP target port.
 *
 * Blocks SCSI command queueing before invoking reconnect() such that
 * queuecommand() won't be invoked concurrently with reconnect() from outside
 * the SCSI EH. This is important since a reconnect() implementation may
 * reallocate resources needed by queuecommand().
 *
 * Notes:
 * - This function neither waits until outstanding requests have finished nor
 *   tries to abort these. It is the responsibility of the reconnect()
 *   function to finish outstanding commands before reconnecting to the target
 *   port.
 * - It is the responsibility of the caller to ensure that the resources
 *   reallocated by the reconnect() function won't be used while this function
 *   is in progress. One possible strategy is to invoke this function from
 *   the context of the SCSI EH thread only. Another possible strategy is to
 *   lock the rport mutex inside each SCSI LLD callback that can be invoked by
 *   the SCSI EH (the scsi_host_template.eh_*() functions and also the
 *   scsi_host_template.queuecommand() function).
 */
int srp_reconnect_rport(struct srp_rport *rport)
{
	struct Scsi_Host *shost = rport_to_shost(rport);
	struct srp_internal *i = to_srp_internal(shost->transportt);
	struct scsi_device *sdev;
	int res;

	pr_debug("SCSI host %s\n", dev_name(&shost->shost_gendev));

	res = mutex_lock_interruptible(&rport->mutex);
	if (res)
		goto out;
	scsi_target_block(&shost->shost_gendev);
	srp_wait_for_queuecommand(shost);
	res = rport->state != SRP_RPORT_LOST ? i->f->reconnect(rport) : -ENODEV;
	pr_debug("%s (state %d): transport.reconnect() returned %d\n",
		 dev_name(&shost->shost_gendev), rport->state, res);
	if (res == 0) {
		cancel_delayed_work(&rport->fast_io_fail_work);
		cancel_delayed_work(&rport->dev_loss_work);

		rport->failed_reconnects = 0;
		srp_rport_set_state(rport, SRP_RPORT_RUNNING);
		scsi_target_unblock(&shost->shost_gendev, SDEV_RUNNING);
		/*
		 * If the SCSI error handler has offlined one or more devices,
		 * invoking scsi_target_unblock() won't change the state of
		 * these devices into running so do that explicitly.
		 */
		spin_lock_irq(shost->host_lock);
		__shost_for_each_device(sdev, shost)
			if (sdev->sdev_state == SDEV_OFFLINE)
				sdev->sdev_state = SDEV_RUNNING;
		spin_unlock_irq(shost->host_lock);
	} else if (rport->state == SRP_RPORT_RUNNING) {
		/*
		 * srp_reconnect_rport() has been invoked with fast_io_fail
		 * and dev_loss off. Mark the port as failed and start the TL
		 * failure timers if these had not yet been started.
		 */
		__rport_fail_io_fast(rport);
		scsi_target_unblock(&shost->shost_gendev,
				    SDEV_TRANSPORT_OFFLINE);
		__srp_start_tl_fail_timers(rport);
	} else if (rport->state != SRP_RPORT_BLOCKED) {
		scsi_target_unblock(&shost->shost_gendev,
				    SDEV_TRANSPORT_OFFLINE);
	}
	mutex_unlock(&rport->mutex);

out:
	return res;
}
EXPORT_SYMBOL(srp_reconnect_rport);

/**
 * srp_timed_out() - SRP transport intercept of the SCSI timeout EH
 * @scmd: SCSI command.
 *
 * If a timeout occurs while an rport is in the blocked state, ask the SCSI
 * EH to continue waiting (BLK_EH_RESET_TIMER). Otherwise let the SCSI core
 * handle the timeout (BLK_EH_NOT_HANDLED).
 *
 * Note: This function is called from soft-IRQ context and with the request
 * queue lock held.
 */
static enum blk_eh_timer_return srp_timed_out(struct scsi_cmnd *scmd)
{
	struct scsi_device *sdev = scmd->device;
	struct Scsi_Host *shost = sdev->host;
	struct srp_internal *i = to_srp_internal(shost->transportt);

	pr_debug("timeout for sdev %s\n", dev_name(&sdev->sdev_gendev));
	return i->f->reset_timer_if_blocked && scsi_device_blocked(sdev) ?
		BLK_EH_RESET_TIMER : BLK_EH_NOT_HANDLED;
}

static void srp_rport_release(struct device *dev)
{
	struct srp_rport *rport = dev_to_rport(dev);

	put_device(dev->parent);
	kfree(rport);
}

static int scsi_is_srp_rport(const struct device *dev)
{
	return dev->release == srp_rport_release;
}

static int srp_rport_match(struct attribute_container *cont,
			   struct device *dev)
{
	struct Scsi_Host *shost;
	struct srp_internal *i;

	if (!scsi_is_srp_rport(dev))
		return 0;

	shost = dev_to_shost(dev->parent);
	if (!shost->transportt)
		return 0;
	if (shost->transportt->host_attrs.ac.class != &srp_host_class.class)
		return 0;

	i = to_srp_internal(shost->transportt);
	return &i->rport_attr_cont.ac == cont;
}

static int srp_host_match(struct attribute_container *cont, struct device *dev)
{
	struct Scsi_Host *shost;
	struct srp_internal *i;

	if (!scsi_is_host_device(dev))
		return 0;

	shost = dev_to_shost(dev);
	if (!shost->transportt)
		return 0;
	if (shost->transportt->host_attrs.ac.class != &srp_host_class.class)
		return 0;

	i = to_srp_internal(shost->transportt);
	return &i->t.host_attrs.ac == cont;
}

/**
 * srp_rport_get() - increment rport reference count
 * @rport: SRP target port.
 */
void srp_rport_get(struct srp_rport *rport)
{
	get_device(&rport->dev);
}
EXPORT_SYMBOL(srp_rport_get);

/**
 * srp_rport_put() - decrement rport reference count
 * @rport: SRP target port.
 */
void srp_rport_put(struct srp_rport *rport)
{
	put_device(&rport->dev);
}
EXPORT_SYMBOL(srp_rport_put);

/**
 * srp_rport_add - add a SRP remote port to the device hierarchy
 * @shost:	scsi host the remote port is connected to.
 * @ids:	The port id for the remote port.
 *
 * Publishes a port to the rest of the system.
 */
struct srp_rport *srp_rport_add(struct Scsi_Host *shost,
				struct srp_rport_identifiers *ids)
{
	struct srp_rport *rport;
	struct device *parent = &shost->shost_gendev;
	struct srp_internal *i = to_srp_internal(shost->transportt);
	int id, ret;

	rport = kzalloc(sizeof(*rport), GFP_KERNEL);
	if (!rport)
		return ERR_PTR(-ENOMEM);

	mutex_init(&rport->mutex);

	device_initialize(&rport->dev);

	rport->dev.parent = get_device(parent);
	rport->dev.release = srp_rport_release;

	memcpy(rport->port_id, ids->port_id, sizeof(rport->port_id));
	rport->roles = ids->roles;

	if (i->f->reconnect)
		rport->reconnect_delay = i->f->reconnect_delay ?
			*i->f->reconnect_delay : 10;
	INIT_DELAYED_WORK(&rport->reconnect_work, srp_reconnect_work);
	rport->fast_io_fail_tmo = i->f->fast_io_fail_tmo ?
		*i->f->fast_io_fail_tmo : 15;
	rport->dev_loss_tmo = i->f->dev_loss_tmo ? *i->f->dev_loss_tmo : 60;
	INIT_DELAYED_WORK(&rport->fast_io_fail_work,
			  rport_fast_io_fail_timedout);
	INIT_DELAYED_WORK(&rport->dev_loss_work, rport_dev_loss_timedout);

	id = atomic_inc_return(&to_srp_host_attrs(shost)->next_port_id);
	dev_set_name(&rport->dev, "port-%d:%d", shost->host_no, id);

	transport_setup_device(&rport->dev);

	ret = device_add(&rport->dev);
	if (ret) {
		transport_destroy_device(&rport->dev);
		put_device(&rport->dev);
		return ERR_PTR(ret);
	}

	transport_add_device(&rport->dev);
	transport_configure_device(&rport->dev);

	return rport;
}
EXPORT_SYMBOL_GPL(srp_rport_add);

/**
 * srp_rport_del  -  remove a SRP remote port
 * @rport:	SRP remote port to remove
 *
 * Removes the specified SRP remote port.
 */
void srp_rport_del(struct srp_rport *rport)
{
	struct device *dev = &rport->dev;

	transport_remove_device(dev);
	device_del(dev);
	transport_destroy_device(dev);

	put_device(dev);
}
EXPORT_SYMBOL_GPL(srp_rport_del);

static int do_srp_rport_del(struct device *dev, void *data)
{
	if (scsi_is_srp_rport(dev))
		srp_rport_del(dev_to_rport(dev));
	return 0;
}

/**
 * srp_remove_host  -  tear down a Scsi_Host's SRP data structures
 * @shost:	Scsi Host that is torn down
 *
 * Removes all SRP remote ports for a given Scsi_Host.
 * Must be called just before scsi_remove_host for SRP HBAs.
 */
void srp_remove_host(struct Scsi_Host *shost)
{
	device_for_each_child(&shost->shost_gendev, NULL, do_srp_rport_del);
}
EXPORT_SYMBOL_GPL(srp_remove_host);

/**
 * srp_stop_rport_timers - stop the transport layer recovery timers
 * @rport: SRP remote port for which to stop the timers.
 *
 * Must be called after srp_remove_host() and scsi_remove_host(). The caller
 * must hold a reference on the rport (rport->dev) and on the SCSI host
 * (rport->dev.parent).
 */
void srp_stop_rport_timers(struct srp_rport *rport)
{
	mutex_lock(&rport->mutex);
	if (rport->state == SRP_RPORT_BLOCKED)
		__rport_fail_io_fast(rport);
	srp_rport_set_state(rport, SRP_RPORT_LOST);
	mutex_unlock(&rport->mutex);

	cancel_delayed_work_sync(&rport->reconnect_work);
	cancel_delayed_work_sync(&rport->fast_io_fail_work);
	cancel_delayed_work_sync(&rport->dev_loss_work);
}
EXPORT_SYMBOL_GPL(srp_stop_rport_timers);

static int srp_tsk_mgmt_response(struct Scsi_Host *shost, u64 nexus, u64 tm_id,
				 int result)
{
	struct srp_internal *i = to_srp_internal(shost->transportt);
	return i->f->tsk_mgmt_response(shost, nexus, tm_id, result);
}

static int srp_it_nexus_response(struct Scsi_Host *shost, u64 nexus, int result)
{
	struct srp_internal *i = to_srp_internal(shost->transportt);
	return i->f->it_nexus_response(shost, nexus, result);
}

/**
 * srp_attach_transport  -  instantiate SRP transport template
 * @ft:		SRP transport class function template
 */
struct scsi_transport_template *
srp_attach_transport(struct srp_function_template *ft)
{
	int count;
	struct srp_internal *i;

	i = kzalloc(sizeof(*i), GFP_KERNEL);
	if (!i)
		return NULL;

	i->t.eh_timed_out = srp_timed_out;

	i->t.tsk_mgmt_response = srp_tsk_mgmt_response;
	i->t.it_nexus_response = srp_it_nexus_response;

	i->t.host_size = sizeof(struct srp_host_attrs);
	i->t.host_attrs.ac.attrs = &i->host_attrs[0];
	i->t.host_attrs.ac.class = &srp_host_class.class;
	i->t.host_attrs.ac.match = srp_host_match;
	i->host_attrs[0] = NULL;
	transport_container_register(&i->t.host_attrs);

	i->rport_attr_cont.ac.attrs = &i->rport_attrs[0];
	i->rport_attr_cont.ac.class = &srp_rport_class.class;
	i->rport_attr_cont.ac.match = srp_rport_match;

	count = 0;
	i->rport_attrs[count++] = &dev_attr_port_id;
	i->rport_attrs[count++] = &dev_attr_roles;
	if (ft->has_rport_state) {
		i->rport_attrs[count++] = &dev_attr_state;
		i->rport_attrs[count++] = &dev_attr_fast_io_fail_tmo;
		i->rport_attrs[count++] = &dev_attr_dev_loss_tmo;
	}
	if (ft->reconnect) {
		i->rport_attrs[count++] = &dev_attr_reconnect_delay;
		i->rport_attrs[count++] = &dev_attr_failed_reconnects;
	}
	if (ft->rport_delete)
		i->rport_attrs[count++] = &dev_attr_delete;
	i->rport_attrs[count++] = NULL;
	BUG_ON(count > ARRAY_SIZE(i->rport_attrs));

	transport_container_register(&i->rport_attr_cont);

	i->f = ft;

	return &i->t;
}
EXPORT_SYMBOL_GPL(srp_attach_transport);

/**
 * srp_release_transport  -  release SRP transport template instance
 * @t:		transport template instance
 */
void srp_release_transport(struct scsi_transport_template *t)
{
	struct srp_internal *i = to_srp_internal(t);

	transport_container_unregister(&i->t.host_attrs);
	transport_container_unregister(&i->rport_attr_cont);

	kfree(i);
}
EXPORT_SYMBOL_GPL(srp_release_transport);

static __init int srp_transport_init(void)
{
	int ret;

	ret = transport_class_register(&srp_host_class);
	if (ret)
		return ret;
	ret = transport_class_register(&srp_rport_class);
	if (ret)
		goto unregister_host_class;

	return 0;
unregister_host_class:
	transport_class_unregister(&srp_host_class);
	return ret;
}

static void __exit srp_transport_exit(void)
{
	transport_class_unregister(&srp_host_class);
	transport_class_unregister(&srp_rport_class);
}

MODULE_AUTHOR("FUJITA Tomonori");
MODULE_DESCRIPTION("SRP Transport Attributes");
MODULE_LICENSE("GPL");

module_init(srp_transport_init);
module_exit(srp_transport_exit);
