#include <linux/errno.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kmod.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <media/cec.h>

#define CEC_NUM_DEVICES	256
#define CEC_NAME	"cec"

static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "debug level (0-1)");

struct cec_transmit_notifier {
	struct completion c;
	struct cec_data *data;
};

#define dprintk(fmt, arg...)						\
	do {								\
		if (debug)						\
			pr_info("cec-%s: " fmt, adap->name , ## arg);	\
	} while(0)

static dev_t cec_dev_t;

/* Active devices */
static DEFINE_MUTEX(cec_devnode_lock);
static DECLARE_BITMAP(cec_devnode_nums, CEC_NUM_DEVICES);

/* dev to cec_devnode */
#define to_cec_devnode(cd) container_of(cd, struct cec_devnode, dev)

static inline struct cec_devnode *cec_devnode_data(struct file *filp)
{
	return filp->private_data;
}

static int cec_log_addr2idx(const struct cec_adapter *adap, u8 log_addr)
{
	int i;

	for (i = 0; i < adap->num_log_addrs; i++)
		if (adap->log_addr[i] == log_addr)
			return i;
	return -1;
}

static unsigned cec_log_addr2dev(const struct cec_adapter *adap, u8 log_addr)
{
	int i = cec_log_addr2idx(adap, log_addr);

	return adap->prim_device[i < 0 ? 0 : i];
}

/* Called when the last user of the cec device exits. */
static void cec_devnode_release(struct device *cd)
{
	struct cec_devnode *cecdev = to_cec_devnode(cd);

	mutex_lock(&cec_devnode_lock);

	/* Delete the cdev on this minor as well */
	cdev_del(&cecdev->cdev);

	/* Mark device node number as free */
	clear_bit(cecdev->minor, cec_devnode_nums);

	mutex_unlock(&cec_devnode_lock);

	/* Release cec_devnode and perform other cleanups as needed. */
	if (cecdev->release)
		cecdev->release(cecdev);
}

static struct bus_type cec_bus_type = {
	.name = CEC_NAME,
};

static bool cec_sleep(struct cec_adapter *adap, int timeout)
{
	bool timed_out = false;

	DECLARE_WAITQUEUE(wait, current);

	add_wait_queue(&adap->kthread_waitq, &wait);
	if (!kthread_should_stop()) {
		if (timeout < 0) {
			set_current_state(TASK_INTERRUPTIBLE);
			schedule();
		} else {
			timed_out = !schedule_timeout_interruptible
				(msecs_to_jiffies(timeout));
		}
	}

	remove_wait_queue(&adap->kthread_waitq, &wait);
	return timed_out;
}

/*
 * Main CEC state machine
 *
 * In the IDLE state the CEC adapter is ready to receive or transmit messages.
 * If it is woken up it will check if a new message is queued, and if so it
 * will be transmitted and the state will go to TRANSMITTING.
 *
 * When the transmit is marked as done the state machine will check if it
 * should wait for a reply. If not, it will call the notifier and go back
 * to the IDLE state. Else it will switch to the WAIT state and wait for a
 * reply. When the reply arrives it will call the notifier and go back
 * to IDLE state.
 *
 * For the transmit and the wait-for-reply states a timeout is used of
 * 1 second as per the standard.
 */
static int cec_thread_func(void *data)
{
	struct cec_adapter *adap = data;
	int timeout = -1;

	for (;;) {
		bool timed_out = cec_sleep(adap, timeout);

		if (kthread_should_stop())
			break;
		timeout = -1;
		mutex_lock(&adap->lock);
		dprintk("state %d timedout: %d tx: %d@%d\n", adap->state,
			timed_out, adap->tx_qcount, adap->tx_qstart);
		if (adap->state == CEC_ADAP_STATE_TRANSMITTING && timed_out)
			adap->adap_transmit_timed_out(adap);

		if (adap->state == CEC_ADAP_STATE_WAIT ||
		    adap->state == CEC_ADAP_STATE_TRANSMITTING) {
			struct cec_data *data = adap->tx_queue + adap->tx_qstart;

			if (adap->state == CEC_ADAP_STATE_TRANSMITTING &&
			    data->msg.reply && !timed_out &&
			    data->msg.status == CEC_TX_STATUS_OK) {
				adap->state = CEC_ADAP_STATE_WAIT;
				timeout = 1000;
			} else {
				if (timed_out) {
					data->msg.reply = 0;
					if (adap->state == CEC_ADAP_STATE_TRANSMITTING)
						data->msg.status = CEC_TX_STATUS_RETRY_TIMEOUT;
					else
						data->msg.status = CEC_TX_STATUS_REPLY_TIMEOUT;
				}
				adap->state = CEC_ADAP_STATE_IDLE;
				if (data->func) {
					mutex_unlock(&adap->lock);
					data->func(adap, data, data->priv);
					mutex_lock(&adap->lock);
				}
				adap->tx_qstart = (adap->tx_qstart + 1) % CEC_TX_QUEUE_SZ;
				adap->tx_qcount--;
				wake_up_interruptible(&adap->waitq);
			}
		}
		if (adap->state == CEC_ADAP_STATE_IDLE && adap->tx_qcount) {
			adap->state = CEC_ADAP_STATE_TRANSMITTING;
			timeout = adap->tx_queue[adap->tx_qstart].msg.len == 1 ? 200 : 1000;
			adap->adap_transmit(adap, &adap->tx_queue[adap->tx_qstart].msg);
			mutex_unlock(&adap->lock);
			continue;
		}
		mutex_unlock(&adap->lock);
	}
	return 0;
}

static int cec_transmit_notify(struct cec_adapter *adap, struct cec_data *data,
		void *priv)
{
	struct cec_transmit_notifier *n = priv;

	*(n->data) = *data;
	complete(&n->c);
	return 0;
}

int cec_transmit_msg(struct cec_adapter *adap, struct cec_data *data, bool block)
{
	struct cec_transmit_notifier notifier;
	struct cec_msg *msg = &data->msg;
	int res = 0;
	unsigned idx;

	if (msg->len == 0 || msg->len > 16)
		return -EINVAL;
	if (msg->reply && (msg->len == 1 || cec_msg_is_broadcast(msg)))
		return -EINVAL;
	if (msg->len > 1 && !cec_msg_is_broadcast(msg) &&
	    cec_msg_initiator(msg) == cec_msg_destination(msg))
		return -EINVAL;
	if (cec_msg_initiator(msg) != 0xf &&
	    cec_log_addr2idx(adap, cec_msg_initiator(msg)) < 0)
		return -EINVAL;

	if (msg->len == 1)
		dprintk("cec_transmit_msg: 0x%02x%s\n",
				msg->msg[0], !block ? " nb" : "");
	else if (msg->reply)
		dprintk("cec_transmit_msg: 0x%02x 0x%02x (wait for 0x%02x)%s\n",
				msg->msg[0], msg->msg[1],
				msg->reply, !block ? " nb" : "");
	else
		dprintk("cec_transmit_msg: 0x%02x 0x%02x%s\n",
				msg->msg[0], msg->msg[1],
				!block ? " nb" : "");

	msg->status = 0;
	memset(&msg->ts, 0, sizeof(msg->ts));
	if (msg->reply)
		msg->timeout = 1000;
	if (block) {
		init_completion(&notifier.c);
		notifier.data = data;
		data->func = cec_transmit_notify;
		data->priv = &notifier;
	} else {
		data->func = NULL;
		data->priv = NULL;
	}
	mutex_lock(&adap->lock);
	idx = (adap->tx_qstart + adap->tx_qcount) % CEC_TX_QUEUE_SZ;
	if (adap->tx_qcount == CEC_TX_QUEUE_SZ) {
		res = -EBUSY;
	} else {
		adap->tx_queue[idx] = *data;
		adap->tx_qcount++;
		if (adap->state == CEC_ADAP_STATE_IDLE)
			wake_up_interruptible(&adap->kthread_waitq);
	}
	mutex_unlock(&adap->lock);
	if (res || !block)
		return res;
	wait_for_completion_interruptible(&notifier.c);
	return res;
}
EXPORT_SYMBOL_GPL(cec_transmit_msg);

void cec_transmit_done(struct cec_adapter *adap, u32 status)
{
	struct cec_msg *msg;

	dprintk("cec_transmit_done\n");
	mutex_lock(&adap->lock);
	if (adap->state == CEC_ADAP_STATE_TRANSMITTING) {
		msg = &adap->tx_queue[adap->tx_qstart].msg;
		msg->status = status;
		if (status)
			msg->reply = 0;
		ktime_get_ts(&msg->ts);
		wake_up_interruptible(&adap->kthread_waitq);
	}
	mutex_unlock(&adap->lock);
}
EXPORT_SYMBOL_GPL(cec_transmit_done);

static int cec_receive_notify(struct cec_adapter *adap, struct cec_msg *msg)
{
	bool is_broadcast = cec_msg_is_broadcast(msg);
	u8 dest_laddr = cec_msg_destination(msg);
	u8 devtype = cec_log_addr2dev(adap, dest_laddr);
	bool is_directed = cec_log_addr2idx(adap, dest_laddr) >= 0;
	struct cec_data tx_data;
	int res = 0;
	unsigned idx;

	if (msg->len <= 1)
		return 0;
	if (!is_directed && !is_broadcast)
		return 0;	/* Not for us */

	tx_data.msg.msg[0] = (msg->msg[0] << 4) | (msg->msg[0] >> 4);
	tx_data.msg.reply = 0;

	if (adap->received) {
		res = adap->received(adap, msg);
		if (res != -ENOMSG)
			return 0;
		res = 0;
	}

	switch (msg->msg[1]) {
	case CEC_OP_GET_CEC_VERSION:
		if (is_broadcast)
			return 0;
		tx_data.msg.len = 3;
		tx_data.msg.msg[1] = CEC_OP_CEC_VERSION;
		tx_data.msg.msg[2] = adap->version;
		return cec_transmit_msg(adap, &tx_data, false);

	case CEC_OP_GIVE_PHYSICAL_ADDR:
		if (!is_directed)
			return 0;
		/* Do nothing for CEC switches using addr 15 */
		if (devtype == CEC_PRIM_DEVTYPE_SWITCH && dest_laddr == 15)
			return 0;
		tx_data.msg.len = 5;
		tx_data.msg.msg[1] = CEC_OP_REPORT_PHYSICAL_ADDR;
		tx_data.msg.msg[2] = adap->phys_addr >> 8;
		tx_data.msg.msg[3] = adap->phys_addr & 0xff;
		tx_data.msg.msg[4] = devtype;
		return cec_transmit_msg(adap, &tx_data, false);

	case CEC_OP_ABORT:
		/* Do nothing for CEC switches */
		if (devtype == CEC_PRIM_DEVTYPE_SWITCH)
			return 0;
		tx_data.msg.len = 4;
		tx_data.msg.msg[1] = CEC_OP_FEATURE_ABORT;
		tx_data.msg.msg[2] = msg->msg[1];
		tx_data.msg.msg[3] = 4;	/* Refused */
		return cec_transmit_msg(adap, &tx_data, false);

	case CEC_OP_USER_CONTROL_PRESSED:
		if (adap->key_passthrough != CEC_KEY_PASSTHROUGH_ENABLE) {
			rc_keydown(adap->rc, RC_TYPE_CEC, msg->msg[2], 0);
			return 0;
		}
		break;

	case CEC_OP_USER_CONTROL_RELEASED:
		if (adap->key_passthrough != CEC_KEY_PASSTHROUGH_ENABLE) {
			rc_keyup(adap->rc);
			return 0;
		}
		break;
	}

	if ((adap->capabilities & CEC_CAP_RECEIVE) == 0)
		return 0;
	mutex_lock(&adap->lock);
	idx = (adap->rx_qstart + adap->rx_qcount) % CEC_RX_QUEUE_SZ;
	if (adap->rx_qcount == CEC_RX_QUEUE_SZ) {
		res = -EBUSY;
	} else {
		adap->rx_queue[idx] = *msg;
		adap->rx_qcount++;
		wake_up_interruptible(&adap->waitq);
	}
	mutex_unlock(&adap->lock);
	return res;
}

int cec_receive_msg(struct cec_adapter *adap, struct cec_msg *msg, bool block)
{
	int res;

	do {
		mutex_lock(&adap->lock);
		if (adap->rx_qcount) {
			*msg = adap->rx_queue[adap->rx_qstart];
			adap->rx_qstart = (adap->rx_qstart + 1) % CEC_RX_QUEUE_SZ;
			adap->rx_qcount--;
			res = 0;
		} else {
			res = -EAGAIN;
		}
		mutex_unlock(&adap->lock);
		if (!block || !res)
			break;
		if (msg->timeout) {
			res = wait_event_interruptible_timeout(adap->waitq,
				adap->rx_qcount, msecs_to_jiffies(msg->timeout));
			if (res == 0)
				res = -ETIMEDOUT;
			else if (res > 0)
				res = 0;
		} else {
			res = wait_event_interruptible(adap->waitq,
				adap->rx_qcount);
		}
	} while (!res);
	return res;
}
EXPORT_SYMBOL_GPL(cec_receive_msg);

void cec_received_msg(struct cec_adapter *adap, struct cec_msg *msg)
{
	bool is_reply = false;

	mutex_lock(&adap->lock);
	ktime_get_ts(&msg->ts);
	dprintk("cec_received_msg: %02x %02x\n", msg->msg[0], msg->msg[1]);
	if (!cec_msg_is_broadcast(msg) && msg->len > 1 &&
	    adap->state == CEC_ADAP_STATE_WAIT) {
		struct cec_msg *dst = &adap->tx_queue[adap->tx_qstart].msg;

		if (msg->msg[1] == dst->reply ||
		    msg->msg[1] == CEC_OP_FEATURE_ABORT) {
			*dst = *msg;
			is_reply = true;
			if (msg->msg[1] == CEC_OP_FEATURE_ABORT) {
				dst->reply = 0;
				dst->status = CEC_TX_STATUS_FEATURE_ABORT;
			}
			wake_up_interruptible(&adap->kthread_waitq);
		}
	}
	mutex_unlock(&adap->lock);
	if (!is_reply)
		adap->recv_notifier(adap, msg);
}
EXPORT_SYMBOL_GPL(cec_received_msg);

void cec_post_event(struct cec_adapter *adap, u32 event)
{
	unsigned idx;

	mutex_lock(&adap->lock);
	if (adap->ev_qcount == CEC_EV_QUEUE_SZ) {
		/* Drop oldest event */
		adap->ev_qstart = (adap->ev_qstart + 1) % CEC_EV_QUEUE_SZ;
		adap->ev_qcount--;
	}

	idx = (adap->ev_qstart + adap->ev_qcount) % CEC_EV_QUEUE_SZ;

	adap->ev_queue[idx].event = event;
	ktime_get_ts(&adap->ev_queue[idx].ts);
	adap->ev_qcount++;
	mutex_unlock(&adap->lock);
}
EXPORT_SYMBOL_GPL(cec_post_event);

static int cec_report_phys_addr(struct cec_adapter *adap, unsigned logical_addr)
{
	struct cec_data data;

	/* Report Physical Address */
	data.msg.len = 5;
	data.msg.msg[0] = (logical_addr << 4) | 0x0f;
	data.msg.msg[1] = CEC_OP_REPORT_PHYSICAL_ADDR;
	data.msg.msg[2] = adap->phys_addr >> 8;
	data.msg.msg[3] = adap->phys_addr & 0xff;
	data.msg.msg[4] = cec_log_addr2dev(adap, logical_addr);
	data.msg.reply = 0;
	dprintk("config: la %d pa %x.%x.%x.%x\n",
			logical_addr, cec_phys_addr_exp(adap->phys_addr));
	return cec_transmit_msg(adap, &data, true);
}

int cec_enable(struct cec_adapter *adap, bool enable)
{
	int ret;

	mutex_lock(&adap->lock);
	ret = adap->adap_enable(adap, enable);
	if (ret) {
		mutex_unlock(&adap->lock);
		return ret;
	}
	if (!enable) {
		adap->state = CEC_ADAP_STATE_DISABLED;
		adap->tx_qcount = 0;
		adap->rx_qcount = 0;
		adap->ev_qcount = 0;
		adap->num_log_addrs = 0;
	} else {
		adap->state = CEC_ADAP_STATE_UNCONF;
	}
	mutex_unlock(&adap->lock);
	return 0;
}
EXPORT_SYMBOL_GPL(cec_enable);

struct cec_log_addrs_int {
	struct cec_adapter *adap;
	struct cec_log_addrs log_addrs;
	struct completion c;
	bool free_on_exit;
	int err;
};

static int cec_config_log_addrs(struct cec_adapter *adap, struct cec_log_addrs *log_addrs)
{
	static const u8 tv_log_addrs[] = {
		0, CEC_LOG_ADDR_INVALID
	};
	static const u8 record_log_addrs[] = {
		1, 2, 9, 12, 13, CEC_LOG_ADDR_INVALID
	};
	static const u8 tuner_log_addrs[] = {
		3, 6, 7, 10, 12, 13, CEC_LOG_ADDR_INVALID
	};
	static const u8 playback_log_addrs[] = {
		4, 8, 11, 12, 13, CEC_LOG_ADDR_INVALID
	};
	static const u8 audiosystem_log_addrs[] = {
		5, 12, 13, CEC_LOG_ADDR_INVALID
	};
	static const u8 specific_use_log_addrs[] = {
		14, 12, 13, CEC_LOG_ADDR_INVALID
	};
	static const u8 unregistered_log_addrs[] = {
		CEC_LOG_ADDR_INVALID
	};
	static const u8 *type2addrs[7] = {
		[CEC_LOG_ADDR_TYPE_TV] = tv_log_addrs,
		[CEC_LOG_ADDR_TYPE_RECORD] = record_log_addrs,
		[CEC_LOG_ADDR_TYPE_TUNER] = tuner_log_addrs,
		[CEC_LOG_ADDR_TYPE_PLAYBACK] = playback_log_addrs,
		[CEC_LOG_ADDR_TYPE_AUDIOSYSTEM] = audiosystem_log_addrs,
		[CEC_LOG_ADDR_TYPE_SPECIFIC] = specific_use_log_addrs,
		[CEC_LOG_ADDR_TYPE_UNREGISTERED] = unregistered_log_addrs,
	};
	struct cec_data data;
	u32 claimed_addrs = 0;
	int i, j;
	int err;

	if (adap->phys_addr) {
		/* The TV functionality can only map to physical address 0.
		   For any other address, try the Specific functionality
		   instead as per the spec. */
		for (i = 0; i < log_addrs->num_log_addrs; i++)
			if (log_addrs->log_addr_type[i] == CEC_LOG_ADDR_TYPE_TV)
				log_addrs->log_addr_type[i] = CEC_LOG_ADDR_TYPE_SPECIFIC;
	}

	memcpy(adap->prim_device, log_addrs->primary_device_type, log_addrs->num_log_addrs);
	dprintk("physical address: %x.%x.%x.%x, claim %d logical addresses\n",
			cec_phys_addr_exp(adap->phys_addr), log_addrs->num_log_addrs);
	adap->num_log_addrs = 0;
	adap->state = CEC_ADAP_STATE_IDLE;

	/* TODO: remember last used logical addr type to achieve
	   faster logical address polling by trying that one first.
	 */
	for (i = 0; i < log_addrs->num_log_addrs; i++) {
		const u8 *la_list = type2addrs[log_addrs->log_addr_type[i]];

		if (kthread_should_stop())
			return -EINTR;

		for (j = 0; la_list[j] != CEC_LOG_ADDR_INVALID; j++) {
			u8 log_addr = la_list[j];

			if (claimed_addrs & (1 << log_addr))
				continue;

			/* Send polling message */
			data.msg.len = 1;
			data.msg.msg[0] = 0xf0 | log_addr;
			data.msg.reply = 0;
			err = cec_transmit_msg(adap, &data, true);
			if (err)
				return err;
			if (data.msg.status == CEC_TX_STATUS_RETRY_TIMEOUT) {
				/* Message not acknowledged, so this logical
				   address is free to use. */
				claimed_addrs |= 1 << log_addr;
				adap->log_addr[adap->num_log_addrs++] = log_addr;
				log_addrs->log_addr[i] = log_addr;
				err = adap->adap_log_addr(adap, log_addr);
				dprintk("claim addr %d (%d)\n", log_addr, adap->prim_device[i]);
				if (err)
					return err;
				cec_report_phys_addr(adap, log_addr);
				if (adap->claimed_log_addr)
					adap->claimed_log_addr(adap, i);
				break;
			}
		}
	}
	if (adap->num_log_addrs == 0) {
		if (log_addrs->num_log_addrs > 1)
			dprintk("could not claim last %d addresses\n", log_addrs->num_log_addrs - 1);
		adap->log_addr[adap->num_log_addrs++] = 15;
		log_addrs->log_addr_type[0] = CEC_LOG_ADDR_TYPE_UNREGISTERED;
		log_addrs->log_addr[0] = 15;
		log_addrs->num_log_addrs = 1;
		err = adap->adap_log_addr(adap, 15);
		dprintk("claim addr %d (%d)\n", 15, adap->prim_device[0]);
		if (err)
			return err;
		cec_report_phys_addr(adap, 15);
		if (adap->claimed_log_addr)
			adap->claimed_log_addr(adap, 0);
	}
	return 0;
}

static int cec_config_thread_func(void *arg)
{
	struct cec_log_addrs_int *cla_int = arg;
	int err;

	cla_int->err = err = cec_config_log_addrs(cla_int->adap, &cla_int->log_addrs);
	cla_int->adap->kthread_config = NULL;
	if (cla_int->free_on_exit)
		kfree(cla_int);
	else
		complete(&cla_int->c);
	return err;
}

int cec_claim_log_addrs(struct cec_adapter *adap, struct cec_log_addrs *log_addrs, bool block)
{
	struct cec_log_addrs_int *cla_int;
	int i;

	if (adap->state == CEC_ADAP_STATE_DISABLED)
		return -EINVAL;

	if (log_addrs->num_log_addrs == 0 ||
	    log_addrs->num_log_addrs > CEC_MAX_LOG_ADDRS)
		return -EINVAL;
	if (log_addrs->cec_version != CEC_VERSION_1_4B &&
	    log_addrs->cec_version != CEC_VERSION_2_0)
		return -EINVAL;
	if (log_addrs->num_log_addrs > 1)
		for (i = 0; i < log_addrs->num_log_addrs; i++)
			if (log_addrs->log_addr_type[i] ==
					CEC_LOG_ADDR_TYPE_UNREGISTERED)
				return -EINVAL;
	for (i = 0; i < log_addrs->num_log_addrs; i++) {
		if (log_addrs->primary_device_type[i] > CEC_PRIM_DEVTYPE_VIDEOPROC)
			return -EINVAL;
		if (log_addrs->primary_device_type[i] == 2)
			return -EINVAL;
		if (log_addrs->log_addr_type[i] > CEC_LOG_ADDR_TYPE_UNREGISTERED)
			return -EINVAL;
	}

	/* For phys addr 0xffff only the Unregistered functionality is
	   allowed. */
	if (adap->phys_addr == 0xffff &&
	    (log_addrs->num_log_addrs > 1 ||
	     log_addrs->log_addr_type[0] != CEC_LOG_ADDR_TYPE_UNREGISTERED))
		return -EINVAL;

	cla_int = kzalloc(sizeof(*cla_int), GFP_KERNEL);
	if (cla_int == NULL)
		return -ENOMEM;
	init_completion(&cla_int->c);
	cla_int->free_on_exit = !block;
	cla_int->adap = adap;
	cla_int->log_addrs = *log_addrs;
	adap->kthread_config = kthread_run(cec_config_thread_func, cla_int, "cec_log_addrs");
	if (block) {
		wait_for_completion(&cla_int->c);
		kfree(cla_int);
	}
	return 0;
}
EXPORT_SYMBOL_GPL(cec_claim_log_addrs);

static ssize_t cec_read(struct file *filp, char __user *buf,
		size_t sz, loff_t *off)
{
	struct cec_devnode *cecdev = cec_devnode_data(filp);

	if (!cec_devnode_is_registered(cecdev))
		return -EIO;
	return 0;
}

static ssize_t cec_write(struct file *filp, const char __user *buf,
		size_t sz, loff_t *off)
{
	struct cec_devnode *cecdev = cec_devnode_data(filp);

	if (!cec_devnode_is_registered(cecdev))
		return -EIO;
	return 0;
}

static unsigned int cec_poll(struct file *filp,
			       struct poll_table_struct *poll)
{
	struct cec_devnode *cecdev = cec_devnode_data(filp);
	struct cec_adapter *adap = to_cec_adapter(cecdev);
	unsigned res = 0;

	if (!cec_devnode_is_registered(cecdev))
		return POLLERR | POLLHUP;
	mutex_lock(&adap->lock);
	if (adap->tx_qcount < CEC_TX_QUEUE_SZ)
		res |= POLLOUT | POLLWRNORM;
	if (adap->rx_qcount)
		res |= POLLIN | POLLRDNORM;
	poll_wait(filp, &adap->waitq, poll);
	mutex_unlock(&adap->lock);
	return res;
}

static long cec_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct cec_devnode *cecdev = cec_devnode_data(filp);
	struct cec_adapter *adap = to_cec_adapter(cecdev);
	void __user *parg = (void __user *)arg;
	int err;

	if (!cec_devnode_is_registered(cecdev))
		return -EIO;

	switch (cmd) {
	case CEC_G_CAPS: {
		struct cec_caps caps;

		caps.available_log_addrs = 3;
		caps.capabilities = adap->capabilities;
		if (copy_to_user(parg, &caps, sizeof(caps)))
			return -EFAULT;
		break;
	}

	case CEC_TRANSMIT: {
		struct cec_data data;

		if (!(adap->capabilities & CEC_CAP_TRANSMIT))
			return -ENOTTY;
		if (copy_from_user(&data.msg, parg, sizeof(data.msg)))
			return -EFAULT;
		err = cec_transmit_msg(adap, &data, !(filp->f_flags & O_NONBLOCK));
		if (err)
			return err;
		if (copy_to_user(parg, &data.msg, sizeof(data.msg)))
			return -EFAULT;
		break;
	}

	case CEC_RECEIVE: {
		struct cec_data data;

		if (!(adap->capabilities & CEC_CAP_RECEIVE))
			return -ENOTTY;
		if (copy_from_user(&data.msg, parg, sizeof(data.msg)))
			return -EFAULT;
		err = cec_receive_msg(adap, &data.msg, !(filp->f_flags & O_NONBLOCK));
		if (err)
			return err;
		if (copy_to_user(parg, &data.msg, sizeof(data.msg)))
			return -EFAULT;
		break;
	}

	case CEC_G_EVENT: {
		struct cec_event ev;

		mutex_lock(&adap->lock);
		err = -EAGAIN;
		if (adap->ev_qcount) {
			err = 0;
			ev = adap->ev_queue[adap->ev_qstart];
			adap->ev_qstart = (adap->ev_qstart + 1) % CEC_EV_QUEUE_SZ;
			adap->ev_qcount--;
		}
		mutex_unlock(&adap->lock);
		if (err)
			return err;
		if (copy_to_user((void __user *)arg, &ev, sizeof(ev)))
			return -EFAULT;
		break;
	}

	case CEC_G_ADAP_STATE: {
		u32 state = adap->state != CEC_ADAP_STATE_DISABLED;

		if (copy_to_user(parg, &state, sizeof(state)))
			return -EFAULT;
		break;
	}

	case CEC_S_ADAP_STATE: {
		u32 state;

		if (!(adap->capabilities & CEC_CAP_STATE))
			return -ENOTTY;
		if (copy_from_user(&state, parg, sizeof(state)))
			return -EFAULT;
		if (!state && adap->state == CEC_ADAP_STATE_DISABLED)
			return 0;
		if (state && adap->state != CEC_ADAP_STATE_DISABLED)
			return 0;
		cec_enable(adap, !!state);
		break;
	}

	case CEC_G_ADAP_PHYS_ADDR:
		if (copy_to_user(parg, &adap->phys_addr, sizeof(adap->phys_addr)))
			return -EFAULT;
		break;

	case CEC_S_ADAP_PHYS_ADDR: {
		u16 phys_addr;

		if (!(adap->capabilities & CEC_CAP_PHYS_ADDR))
			return -ENOTTY;
		if (copy_from_user(&phys_addr, parg, sizeof(phys_addr)))
			return -EFAULT;
		adap->phys_addr = phys_addr;
		break;
	}

	case CEC_G_ADAP_LOG_ADDRS: {
		struct cec_log_addrs log_addrs;

		log_addrs.cec_version = adap->version;
		log_addrs.num_log_addrs = adap->num_log_addrs;
		memcpy(log_addrs.primary_device_type, adap->prim_device, CEC_MAX_LOG_ADDRS);
		memcpy(log_addrs.log_addr_type, adap->log_addr_type, CEC_MAX_LOG_ADDRS);
		memcpy(log_addrs.log_addr, adap->log_addr, CEC_MAX_LOG_ADDRS);

		if (copy_to_user(parg, &log_addrs, sizeof(log_addrs)))
			return -EFAULT;
		break;
	}

	case CEC_S_ADAP_LOG_ADDRS: {
		struct cec_log_addrs log_addrs;

		if (!(adap->capabilities & CEC_CAP_LOG_ADDRS))
			return -ENOTTY;
		if (copy_from_user(&log_addrs, parg, sizeof(log_addrs)))
			return -EFAULT;
		err = cec_claim_log_addrs(adap, &log_addrs, true);
		if (err)
			return err;

		if (copy_to_user(parg, &log_addrs, sizeof(log_addrs)))
			return -EFAULT;
		break;
	}

	case CEC_G_KEY_PASSTHROUGH: {
		if (put_user(adap->key_passthrough, (__u8 __user *)parg))
			return -EFAULT;
		break;
	}

	case CEC_S_KEY_PASSTHROUGH: {
		__u8 state;
		if (get_user(state, (__u8 __user *)parg))
			return -EFAULT;
		if (state != CEC_KEY_PASSTHROUGH_DISABLE &&
		    state != CEC_KEY_PASSTHROUGH_ENABLE)
			return -EINVAL;
		adap->key_passthrough = state;
		break;
	}

	default:
		return -ENOTTY;
	}
	return 0;
}

/* Override for the open function */
static int cec_open(struct inode *inode, struct file *filp)
{
	struct cec_devnode *cecdev;

	/* Check if the cec device is available. This needs to be done with
	 * the cec_devnode_lock held to prevent an open/unregister race:
	 * without the lock, the device could be unregistered and freed between
	 * the cec_devnode_is_registered() and get_device() calls, leading to
	 * a crash.
	 */
	mutex_lock(&cec_devnode_lock);
	cecdev = container_of(inode->i_cdev, struct cec_devnode, cdev);
	/* return ENXIO if the cec device has been removed
	   already or if it is not registered anymore. */
	if (!cec_devnode_is_registered(cecdev)) {
		mutex_unlock(&cec_devnode_lock);
		return -ENXIO;
	}
	/* and increase the device refcount */
	get_device(&cecdev->dev);
	mutex_unlock(&cec_devnode_lock);

	filp->private_data = cecdev;

	return 0;
}

/* Override for the release function */
static int cec_release(struct inode *inode, struct file *filp)
{
	struct cec_devnode *cecdev = cec_devnode_data(filp);
	int ret = 0;

	/* decrease the refcount unconditionally since the release()
	   return value is ignored. */
	put_device(&cecdev->dev);
	filp->private_data = NULL;
	return ret;
}

static const struct file_operations cec_devnode_fops = {
	.owner = THIS_MODULE,
	.read = cec_read,
	.write = cec_write,
	.open = cec_open,
	.unlocked_ioctl = cec_ioctl,
	.release = cec_release,
	.poll = cec_poll,
	.llseek = no_llseek,
};

/**
 * cec_devnode_register - register a cec device node
 * @cecdev: cec device node structure we want to register
 *
 * The registration code assigns minor numbers and registers the new device node
 * with the kernel. An error is returned if no free minor number can be found,
 * or if the registration of the device node fails.
 *
 * Zero is returned on success.
 *
 * Note that if the cec_devnode_register call fails, the release() callback of
 * the cec_devnode structure is *not* called, so the caller is responsible for
 * freeing any data.
 */
static int __must_check cec_devnode_register(struct cec_devnode *cecdev,
		struct module *owner)
{
	int minor;
	int ret;

	/* Part 1: Find a free minor number */
	mutex_lock(&cec_devnode_lock);
	minor = find_next_zero_bit(cec_devnode_nums, CEC_NUM_DEVICES, 0);
	if (minor == CEC_NUM_DEVICES) {
		mutex_unlock(&cec_devnode_lock);
		pr_err("could not get a free minor\n");
		return -ENFILE;
	}

	set_bit(minor, cec_devnode_nums);
	mutex_unlock(&cec_devnode_lock);

	cecdev->minor = minor;

	/* Part 2: Initialize and register the character device */
	cdev_init(&cecdev->cdev, &cec_devnode_fops);
	cecdev->cdev.owner = owner;

	ret = cdev_add(&cecdev->cdev, MKDEV(MAJOR(cec_dev_t), cecdev->minor), 1);
	if (ret < 0) {
		pr_err("%s: cdev_add failed\n", __func__);
		goto error;
	}

	/* Part 3: Register the cec device */
	cecdev->dev.bus = &cec_bus_type;
	cecdev->dev.devt = MKDEV(MAJOR(cec_dev_t), cecdev->minor);
	cecdev->dev.release = cec_devnode_release;
	if (cecdev->parent)
		cecdev->dev.parent = cecdev->parent;
	dev_set_name(&cecdev->dev, "cec%d", cecdev->minor);
	ret = device_register(&cecdev->dev);
	if (ret < 0) {
		pr_err("%s: device_register failed\n", __func__);
		goto error;
	}

	/* Part 4: Activate this minor. The char device can now be used. */
	set_bit(CEC_FLAG_REGISTERED, &cecdev->flags);

	return 0;

error:
	cdev_del(&cecdev->cdev);
	clear_bit(cecdev->minor, cec_devnode_nums);
	return ret;
}

/**
 * cec_devnode_unregister - unregister a cec device node
 * @cecdev: the device node to unregister
 *
 * This unregisters the passed device. Future open calls will be met with
 * errors.
 *
 * This function can safely be called if the device node has never been
 * registered or has already been unregistered.
 */
static void cec_devnode_unregister(struct cec_devnode *cecdev)
{
	/* Check if cecdev was ever registered at all */
	if (!cec_devnode_is_registered(cecdev))
		return;

	mutex_lock(&cec_devnode_lock);
	clear_bit(CEC_FLAG_REGISTERED, &cecdev->flags);
	mutex_unlock(&cec_devnode_lock);
	device_unregister(&cecdev->dev);
}

int cec_create_adapter(struct cec_adapter *adap, const char *name, u32 caps)
{
	int res = 0;

	adap->state = CEC_ADAP_STATE_DISABLED;
	adap->name = name;
	adap->phys_addr = 0xffff;
	adap->capabilities = caps;
	adap->version = CEC_VERSION_1_4B;
	mutex_init(&adap->lock);
	adap->kthread = kthread_run(cec_thread_func, adap, name);
	init_waitqueue_head(&adap->kthread_waitq);
	init_waitqueue_head(&adap->waitq);
	if (IS_ERR(adap->kthread)) {
		pr_err("cec-%s: kernel_thread() failed\n", name);
		return PTR_ERR(adap->kthread);
	}
	if (caps) {
		res = cec_devnode_register(&adap->devnode, adap->owner);
		if (res)
			kthread_stop(adap->kthread);
	}
	adap->recv_notifier = cec_receive_notify;

	/* Prepare the RC input device */
	adap->rc = rc_allocate_device();
	if (!adap->rc) {
		pr_err("cec-%s: failed to allocate memory for rc_dev\n", name);
		cec_devnode_unregister(&adap->devnode);
		kthread_stop(adap->kthread);
		return -ENOMEM;
	}

	snprintf(adap->input_name, sizeof(adap->input_name), "RC for %s", name);
	snprintf(adap->input_phys, sizeof(adap->input_phys), "%s/input0", name);
	strncpy(adap->input_drv, name, sizeof(adap->input_drv));

	adap->rc->input_name = adap->input_name;
	adap->rc->input_phys = adap->input_phys;
	adap->rc->dev.parent = &adap->devnode.dev;
	adap->rc->driver_name = adap->input_drv;
	adap->rc->driver_type = RC_DRIVER_CEC;
	adap->rc->priv = adap;
	adap->rc->map_name = RC_MAP_CEC;
	adap->rc->timeout = MS_TO_NS(100);
	adap->rc->allowed_protocols = RC_BIT_CEC;
	res = rc_register_device(adap->rc);

	if (res) {
		pr_err("cec-%s: failed to prepare input device\n", name);
		cec_devnode_unregister(&adap->devnode);
		rc_free_device(adap->rc);
		kthread_stop(adap->kthread);
	}

	return res;
}
EXPORT_SYMBOL_GPL(cec_create_adapter);

void cec_delete_adapter(struct cec_adapter *adap)
{
	if (adap->kthread == NULL)
		return;
	kthread_stop(adap->kthread);
	if (adap->kthread_config)
		kthread_stop(adap->kthread_config);
	adap->state = CEC_ADAP_STATE_DISABLED;
	if (cec_devnode_is_registered(&adap->devnode))
		cec_devnode_unregister(&adap->devnode);
}
EXPORT_SYMBOL_GPL(cec_delete_adapter);

/*
 *	Initialise cec for linux
 */
static int __init cec_devnode_init(void)
{
	int ret;

	pr_info("Linux cec interface: v0.10\n");
	ret = alloc_chrdev_region(&cec_dev_t, 0, CEC_NUM_DEVICES,
				  CEC_NAME);
	if (ret < 0) {
		pr_warn("cec: unable to allocate major\n");
		return ret;
	}

	ret = bus_register(&cec_bus_type);
	if (ret < 0) {
		unregister_chrdev_region(cec_dev_t, CEC_NUM_DEVICES);
		pr_warn("cec: bus_register failed\n");
		return -EIO;
	}

	return 0;
}

static void __exit cec_devnode_exit(void)
{
	bus_unregister(&cec_bus_type);
	unregister_chrdev_region(cec_dev_t, CEC_NUM_DEVICES);
}

subsys_initcall(cec_devnode_init);
module_exit(cec_devnode_exit)

MODULE_AUTHOR("Hans Verkuil <hans.verkuil@cisco.com>");
MODULE_DESCRIPTION("Device node registration for cec drivers");
MODULE_LICENSE("GPL");
