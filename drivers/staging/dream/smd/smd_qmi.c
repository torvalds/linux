/* arch/arm/mach-msm/smd_qmi.c
 *
 * QMI Control Driver -- Manages network data connections.
 *
 * Copyright (C) 2007 Google, Inc.
 * Author: Brian Swetland <swetland@google.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/miscdevice.h>
#include <linux/workqueue.h>
#include <linux/wakelock.h>

#include <asm/uaccess.h>
#include <mach/msm_smd.h>

#define QMI_CTL 0x00
#define QMI_WDS 0x01
#define QMI_DMS 0x02
#define QMI_NAS 0x03

#define QMI_RESULT_SUCCESS 0x0000
#define QMI_RESULT_FAILURE 0x0001

struct qmi_msg {
	unsigned char service;
	unsigned char client_id;
	unsigned short txn_id;
	unsigned short type;
	unsigned short size;
	unsigned char *tlv;
};

#define qmi_ctl_client_id 0

#define STATE_OFFLINE    0
#define STATE_QUERYING   1
#define STATE_ONLINE     2

struct qmi_ctxt {
	struct miscdevice misc;

	struct mutex lock;

	unsigned char ctl_txn_id;
	unsigned char wds_client_id;
	unsigned short wds_txn_id;

	unsigned wds_busy;
	unsigned wds_handle;
	unsigned state_dirty;
	unsigned state;

	unsigned char addr[4];
	unsigned char mask[4];
	unsigned char gateway[4];
	unsigned char dns1[4];
	unsigned char dns2[4];

	smd_channel_t *ch;
	const char *ch_name;
	struct wake_lock wake_lock;

	struct work_struct open_work;
	struct work_struct read_work;
};

static struct qmi_ctxt *qmi_minor_to_ctxt(unsigned n);

static void qmi_read_work(struct work_struct *ws);
static void qmi_open_work(struct work_struct *work);

void qmi_ctxt_init(struct qmi_ctxt *ctxt, unsigned n)
{
	mutex_init(&ctxt->lock);
	INIT_WORK(&ctxt->read_work, qmi_read_work);
	INIT_WORK(&ctxt->open_work, qmi_open_work);
	wake_lock_init(&ctxt->wake_lock, WAKE_LOCK_SUSPEND, ctxt->misc.name);
	ctxt->ctl_txn_id = 1;
	ctxt->wds_txn_id = 1;
	ctxt->wds_busy = 1;
	ctxt->state = STATE_OFFLINE;

}

static struct workqueue_struct *qmi_wq;

static int verbose = 0;

/* anyone waiting for a state change waits here */
static DECLARE_WAIT_QUEUE_HEAD(qmi_wait_queue);


static void qmi_dump_msg(struct qmi_msg *msg, const char *prefix)
{
	unsigned sz, n;
	unsigned char *x;

	if (!verbose)
		return;

	printk(KERN_INFO
	       "qmi: %s: svc=%02x cid=%02x tid=%04x type=%04x size=%04x\n",
	       prefix, msg->service, msg->client_id,
	       msg->txn_id, msg->type, msg->size);

	x = msg->tlv;
	sz = msg->size;

	while (sz >= 3) {
		sz -= 3;

		n = x[1] | (x[2] << 8);
		if (n > sz)
			break;

		printk(KERN_INFO "qmi: %s: tlv: %02x %04x { ",
		       prefix, x[0], n);
		x += 3;
		sz -= n;
		while (n-- > 0)
			printk("%02x ", *x++);
		printk("}\n");
	}
}

int qmi_add_tlv(struct qmi_msg *msg,
		unsigned type, unsigned size, const void *data)
{
	unsigned char *x = msg->tlv + msg->size;

	x[0] = type;
	x[1] = size;
	x[2] = size >> 8;

	memcpy(x + 3, data, size);

	msg->size += (size + 3);

	return 0;
}

/* Extract a tagged item from a qmi message buffer,
** taking care not to overrun the buffer.
*/
static int qmi_get_tlv(struct qmi_msg *msg,
		       unsigned type, unsigned size, void *data)
{
	unsigned char *x = msg->tlv;
	unsigned len = msg->size;
	unsigned n;

	while (len >= 3) {
		len -= 3;

		/* size of this item */
		n = x[1] | (x[2] << 8);
		if (n > len)
			break;

		if (x[0] == type) {
			if (n != size)
				return -1;
			memcpy(data, x + 3, size);
			return 0;
		}

		x += (n + 3);
		len -= n;
	}

	return -1;
}

static unsigned qmi_get_status(struct qmi_msg *msg, unsigned *error)
{
	unsigned short status[2];
	if (qmi_get_tlv(msg, 0x02, sizeof(status), status)) {
		*error = 0;
		return QMI_RESULT_FAILURE;
	} else {
		*error = status[1];
		return status[0];
	}
}

/* 0x01 <qmux-header> <payload> */
#define QMUX_HEADER    13

/* should be >= HEADER + FOOTER */
#define QMUX_OVERHEAD  16

static int qmi_send(struct qmi_ctxt *ctxt, struct qmi_msg *msg)
{
	unsigned char *data;
	unsigned hlen;
	unsigned len;
	int r;

	qmi_dump_msg(msg, "send");

	if (msg->service == QMI_CTL) {
		hlen = QMUX_HEADER - 1;
	} else {
		hlen = QMUX_HEADER;
	}

	/* QMUX length is total header + total payload - IFC selector */
	len = hlen + msg->size - 1;
	if (len > 0xffff)
		return -1;

	data = msg->tlv - hlen;

	/* prepend encap and qmux header */
	*data++ = 0x01; /* ifc selector */

	/* qmux header */
	*data++ = len;
	*data++ = len >> 8;
	*data++ = 0x00; /* flags: client */
	*data++ = msg->service;
	*data++ = msg->client_id;

	/* qmi header */
	*data++ = 0x00; /* flags: send */
	*data++ = msg->txn_id;
	if (msg->service != QMI_CTL)
		*data++ = msg->txn_id >> 8;

	*data++ = msg->type;
	*data++ = msg->type >> 8;
	*data++ = msg->size;
	*data++ = msg->size >> 8;

	/* len + 1 takes the interface selector into account */
	r = smd_write(ctxt->ch, msg->tlv - hlen, len + 1);

	if (r != len) {
		return -1;
	} else {
		return 0;
	}
}

static void qmi_process_ctl_msg(struct qmi_ctxt *ctxt, struct qmi_msg *msg)
{
	unsigned err;
	if (msg->type == 0x0022) {
		unsigned char n[2];
		if (qmi_get_status(msg, &err))
			return;
		if (qmi_get_tlv(msg, 0x01, sizeof(n), n))
			return;
		if (n[0] == QMI_WDS) {
			printk(KERN_INFO
			       "qmi: ctl: wds use client_id 0x%02x\n", n[1]);
			ctxt->wds_client_id = n[1];
			ctxt->wds_busy = 0;
		}
	}
}

static int qmi_network_get_profile(struct qmi_ctxt *ctxt);

static void swapaddr(unsigned char *src, unsigned char *dst)
{
	dst[0] = src[3];
	dst[1] = src[2];
	dst[2] = src[1];
	dst[3] = src[0];
}

static unsigned char zero[4];
static void qmi_read_runtime_profile(struct qmi_ctxt *ctxt, struct qmi_msg *msg)
{
	unsigned char tmp[4];
	unsigned r;

	r = qmi_get_tlv(msg, 0x1e, 4, tmp);
	swapaddr(r ? zero : tmp, ctxt->addr);
	r = qmi_get_tlv(msg, 0x21, 4, tmp);
	swapaddr(r ? zero : tmp, ctxt->mask);
	r = qmi_get_tlv(msg, 0x20, 4, tmp);
	swapaddr(r ? zero : tmp, ctxt->gateway);
	r = qmi_get_tlv(msg, 0x15, 4, tmp);
	swapaddr(r ? zero : tmp, ctxt->dns1);
	r = qmi_get_tlv(msg, 0x16, 4, tmp);
	swapaddr(r ? zero : tmp, ctxt->dns2);
}

static void qmi_process_unicast_wds_msg(struct qmi_ctxt *ctxt,
					struct qmi_msg *msg)
{
	unsigned err;
	switch (msg->type) {
	case 0x0021:
		if (qmi_get_status(msg, &err)) {
			printk(KERN_ERR
			       "qmi: wds: network stop failed (%04x)\n", err);
		} else {
			printk(KERN_INFO
			       "qmi: wds: network stopped\n");
			ctxt->state = STATE_OFFLINE;
			ctxt->state_dirty = 1;
		}
		break;
	case 0x0020:
		if (qmi_get_status(msg, &err)) {
			printk(KERN_ERR
			       "qmi: wds: network start failed (%04x)\n", err);
		} else if (qmi_get_tlv(msg, 0x01, sizeof(ctxt->wds_handle), &ctxt->wds_handle)) {
			printk(KERN_INFO
			       "qmi: wds no handle?\n");
		} else {
			printk(KERN_INFO
			       "qmi: wds: got handle 0x%08x\n",
			       ctxt->wds_handle);
		}
		break;
	case 0x002D:
		printk("qmi: got network profile\n");
		if (ctxt->state == STATE_QUERYING) {
			qmi_read_runtime_profile(ctxt, msg);
			ctxt->state = STATE_ONLINE;
			ctxt->state_dirty = 1;
		}
		break;
	default:
		printk(KERN_ERR "qmi: unknown msg type 0x%04x\n", msg->type);
	}
	ctxt->wds_busy = 0;
}

static void qmi_process_broadcast_wds_msg(struct qmi_ctxt *ctxt,
					  struct qmi_msg *msg)
{
	if (msg->type == 0x0022) {
		unsigned char n[2];
		if (qmi_get_tlv(msg, 0x01, sizeof(n), n))
			return;
		switch (n[0]) {
		case 1:
			printk(KERN_INFO "qmi: wds: DISCONNECTED\n");
			ctxt->state = STATE_OFFLINE;
			ctxt->state_dirty = 1;
			break;
		case 2:
			printk(KERN_INFO "qmi: wds: CONNECTED\n");
			ctxt->state = STATE_QUERYING;
			ctxt->state_dirty = 1;
			qmi_network_get_profile(ctxt);
			break;
		case 3:
			printk(KERN_INFO "qmi: wds: SUSPENDED\n");
			ctxt->state = STATE_OFFLINE;
			ctxt->state_dirty = 1;
		}
	} else {
		printk(KERN_ERR "qmi: unknown bcast msg type 0x%04x\n", msg->type);
	}
}

static void qmi_process_wds_msg(struct qmi_ctxt *ctxt,
				struct qmi_msg *msg)
{
	printk("wds: %04x @ %02x\n", msg->type, msg->client_id);
	if (msg->client_id == ctxt->wds_client_id) {
		qmi_process_unicast_wds_msg(ctxt, msg);
	} else if (msg->client_id == 0xff) {
		qmi_process_broadcast_wds_msg(ctxt, msg);
	} else {
		printk(KERN_ERR
		       "qmi_process_wds_msg client id 0x%02x unknown\n",
		       msg->client_id);
	}
}

static void qmi_process_qmux(struct qmi_ctxt *ctxt,
			     unsigned char *buf, unsigned sz)
{
	struct qmi_msg msg;

	/* require a full header */
	if (sz < 5)
		return;

	/* require a size that matches the buffer size */
	if (sz != (buf[0] | (buf[1] << 8)))
		return;

	/* only messages from a service (bit7=1) are allowed */
	if (buf[2] != 0x80)
		return;

	msg.service = buf[3];
	msg.client_id = buf[4];

	/* annoyingly, CTL messages have a shorter TID */
	if (buf[3] == 0) {
		if (sz < 7)
			return;
		msg.txn_id = buf[6];
		buf += 7;
		sz -= 7;
	} else {
		if (sz < 8)
			return;
		msg.txn_id = buf[6] | (buf[7] << 8);
		buf += 8;
		sz -= 8;
	}

	/* no type and size!? */
	if (sz < 4)
		return;
	sz -= 4;

	msg.type = buf[0] | (buf[1] << 8);
	msg.size = buf[2] | (buf[3] << 8);
	msg.tlv = buf + 4;

	if (sz != msg.size)
		return;

	qmi_dump_msg(&msg, "recv");

	mutex_lock(&ctxt->lock);
	switch (msg.service) {
	case QMI_CTL:
		qmi_process_ctl_msg(ctxt, &msg);
		break;
	case QMI_WDS:
		qmi_process_wds_msg(ctxt, &msg);
		break;
	default:
		printk(KERN_ERR "qmi: msg from unknown svc 0x%02x\n",
		       msg.service);
		break;
	}
	mutex_unlock(&ctxt->lock);

	wake_up(&qmi_wait_queue);
}

#define QMI_MAX_PACKET (256 + QMUX_OVERHEAD)

static void qmi_read_work(struct work_struct *ws)
{
	struct qmi_ctxt *ctxt = container_of(ws, struct qmi_ctxt, read_work);
	struct smd_channel *ch = ctxt->ch;
	unsigned char buf[QMI_MAX_PACKET];
	int sz;

	for (;;) {
		sz = smd_cur_packet_size(ch);
		if (sz == 0)
			break;
		if (sz < smd_read_avail(ch))
			break;
		if (sz > QMI_MAX_PACKET) {
			smd_read(ch, 0, sz);
			continue;
		}
		if (smd_read(ch, buf, sz) != sz) {
			printk(KERN_ERR "qmi: not enough data?!\n");
			continue;
		}

		/* interface selector must be 1 */
		if (buf[0] != 0x01)
			continue;

		qmi_process_qmux(ctxt, buf + 1, sz - 1);
	}
}

static int qmi_request_wds_cid(struct qmi_ctxt *ctxt);

static void qmi_open_work(struct work_struct *ws)
{
	struct qmi_ctxt *ctxt = container_of(ws, struct qmi_ctxt, open_work);
	mutex_lock(&ctxt->lock);
	qmi_request_wds_cid(ctxt);
	mutex_unlock(&ctxt->lock);
}

static void qmi_notify(void *priv, unsigned event)
{
	struct qmi_ctxt *ctxt = priv;

	switch (event) {
	case SMD_EVENT_DATA: {
		int sz;
		sz = smd_cur_packet_size(ctxt->ch);
		if ((sz > 0) && (sz <= smd_read_avail(ctxt->ch))) {
			wake_lock_timeout(&ctxt->wake_lock, HZ / 2);
			queue_work(qmi_wq, &ctxt->read_work);
		}
		break;
	}
	case SMD_EVENT_OPEN:
		printk(KERN_INFO "qmi: smd opened\n");
		queue_work(qmi_wq, &ctxt->open_work);
		break;
	case SMD_EVENT_CLOSE:
		printk(KERN_INFO "qmi: smd closed\n");
		break;
	}
}

static int qmi_request_wds_cid(struct qmi_ctxt *ctxt)
{
	unsigned char data[64 + QMUX_OVERHEAD];
	struct qmi_msg msg;
	unsigned char n;

	msg.service = QMI_CTL;
	msg.client_id = qmi_ctl_client_id;
	msg.txn_id = ctxt->ctl_txn_id;
	msg.type = 0x0022;
	msg.size = 0;
	msg.tlv = data + QMUX_HEADER;

	ctxt->ctl_txn_id += 2;

	n = QMI_WDS;
	qmi_add_tlv(&msg, 0x01, 0x01, &n);

	return qmi_send(ctxt, &msg);
}

static int qmi_network_get_profile(struct qmi_ctxt *ctxt)
{
	unsigned char data[96 + QMUX_OVERHEAD];
	struct qmi_msg msg;

	msg.service = QMI_WDS;
	msg.client_id = ctxt->wds_client_id;
	msg.txn_id = ctxt->wds_txn_id;
	msg.type = 0x002D;
	msg.size = 0;
	msg.tlv = data + QMUX_HEADER;

	ctxt->wds_txn_id += 2;

	return qmi_send(ctxt, &msg);
}

static int qmi_network_up(struct qmi_ctxt *ctxt, char *apn)
{
	unsigned char data[96 + QMUX_OVERHEAD];
	struct qmi_msg msg;
	char *auth_type;
	char *user;
	char *pass;

	for (user = apn; *user; user++) {
		if (*user == ' ') {
			*user++ = 0;
			break;
		}
	}
	for (pass = user; *pass; pass++) {
		if (*pass == ' ') {
			*pass++ = 0;
			break;
		}
	}

	for (auth_type = pass; *auth_type; auth_type++) {
		if (*auth_type == ' ') {
			*auth_type++ = 0;
			break;
		}
	}

	msg.service = QMI_WDS;
	msg.client_id = ctxt->wds_client_id;
	msg.txn_id = ctxt->wds_txn_id;
	msg.type = 0x0020;
	msg.size = 0;
	msg.tlv = data + QMUX_HEADER;

	ctxt->wds_txn_id += 2;

	qmi_add_tlv(&msg, 0x14, strlen(apn), apn);
	if (*auth_type)
		qmi_add_tlv(&msg, 0x16, strlen(auth_type), auth_type);
	if (*user) {
		if (!*auth_type) {
			unsigned char x;
			x = 3;
			qmi_add_tlv(&msg, 0x16, 1, &x);
		}
		qmi_add_tlv(&msg, 0x17, strlen(user), user);
		if (*pass)
			qmi_add_tlv(&msg, 0x18, strlen(pass), pass);
	}
	return qmi_send(ctxt, &msg);
}

static int qmi_network_down(struct qmi_ctxt *ctxt)
{
	unsigned char data[16 + QMUX_OVERHEAD];
	struct qmi_msg msg;

	msg.service = QMI_WDS;
	msg.client_id = ctxt->wds_client_id;
	msg.txn_id = ctxt->wds_txn_id;
	msg.type = 0x0021;
	msg.size = 0;
	msg.tlv = data + QMUX_HEADER;

	ctxt->wds_txn_id += 2;

	qmi_add_tlv(&msg, 0x01, sizeof(ctxt->wds_handle), &ctxt->wds_handle);

	return qmi_send(ctxt, &msg);
}

static int qmi_print_state(struct qmi_ctxt *ctxt, char *buf, int max)
{
	int i;
	char *statename;

	if (ctxt->state == STATE_ONLINE) {
		statename = "up";
	} else if (ctxt->state == STATE_OFFLINE) {
		statename = "down";
	} else {
		statename = "busy";
	}

	i = scnprintf(buf, max, "STATE=%s\n", statename);
	i += scnprintf(buf + i, max - i, "CID=%d\n",ctxt->wds_client_id);

	if (ctxt->state != STATE_ONLINE){
		return i;
	}

	i += scnprintf(buf + i, max - i, "ADDR=%d.%d.%d.%d\n",
		ctxt->addr[0], ctxt->addr[1], ctxt->addr[2], ctxt->addr[3]);
	i += scnprintf(buf + i, max - i, "MASK=%d.%d.%d.%d\n",
		ctxt->mask[0], ctxt->mask[1], ctxt->mask[2], ctxt->mask[3]);
	i += scnprintf(buf + i, max - i, "GATEWAY=%d.%d.%d.%d\n",
		ctxt->gateway[0], ctxt->gateway[1], ctxt->gateway[2],
		ctxt->gateway[3]);
	i += scnprintf(buf + i, max - i, "DNS1=%d.%d.%d.%d\n",
		ctxt->dns1[0], ctxt->dns1[1], ctxt->dns1[2], ctxt->dns1[3]);
	i += scnprintf(buf + i, max - i, "DNS2=%d.%d.%d.%d\n",
		ctxt->dns2[0], ctxt->dns2[1], ctxt->dns2[2], ctxt->dns2[3]);

	return i;
}

static ssize_t qmi_read(struct file *fp, char __user *buf,
			size_t count, loff_t *pos)
{
	struct qmi_ctxt *ctxt = fp->private_data;
	char msg[256];
	int len;
	int r;

	mutex_lock(&ctxt->lock);
	for (;;) {
		if (ctxt->state_dirty) {
			ctxt->state_dirty = 0;
			len = qmi_print_state(ctxt, msg, 256);
			break;
		}
		mutex_unlock(&ctxt->lock);
		r = wait_event_interruptible(qmi_wait_queue, ctxt->state_dirty);
		if (r < 0)
			return r;
		mutex_lock(&ctxt->lock);
	}
	mutex_unlock(&ctxt->lock);

	if (len > count)
		len = count;

	if (copy_to_user(buf, msg, len))
		return -EFAULT;

	return len;
}


static ssize_t qmi_write(struct file *fp, const char __user *buf,
			 size_t count, loff_t *pos)
{
	struct qmi_ctxt *ctxt = fp->private_data;
	unsigned char cmd[64];
	int len;
	int r;

	if (count < 1)
		return 0;

	len = count > 63 ? 63 : count;

	if (copy_from_user(cmd, buf, len))
		return -EFAULT;

	cmd[len] = 0;

	/* lazy */
	if (cmd[len-1] == '\n') {
		cmd[len-1] = 0;
		len--;
	}

	if (!strncmp(cmd, "verbose", 7)) {
		verbose = 1;
	} else if (!strncmp(cmd, "terse", 5)) {
		verbose = 0;
	} else if (!strncmp(cmd, "poll", 4)) {
		ctxt->state_dirty = 1;
		wake_up(&qmi_wait_queue);
	} else if (!strncmp(cmd, "down", 4)) {
retry_down:
		mutex_lock(&ctxt->lock);
		if (ctxt->wds_busy) {
			mutex_unlock(&ctxt->lock);
			r = wait_event_interruptible(qmi_wait_queue, !ctxt->wds_busy);
			if (r < 0)
				return r;
			goto retry_down;
		}
		ctxt->wds_busy = 1;
		qmi_network_down(ctxt);
		mutex_unlock(&ctxt->lock);
	} else if (!strncmp(cmd, "up:", 3)) {
retry_up:
		mutex_lock(&ctxt->lock);
		if (ctxt->wds_busy) {
			mutex_unlock(&ctxt->lock);
			r = wait_event_interruptible(qmi_wait_queue, !ctxt->wds_busy);
			if (r < 0)
				return r;
			goto retry_up;
		}
		ctxt->wds_busy = 1;
		qmi_network_up(ctxt, cmd+3);
		mutex_unlock(&ctxt->lock);
	} else {
		return -EINVAL;
	}

	return count;
}

static int qmi_open(struct inode *ip, struct file *fp)
{
	struct qmi_ctxt *ctxt = qmi_minor_to_ctxt(MINOR(ip->i_rdev));
	int r = 0;

	if (!ctxt) {
		printk(KERN_ERR "unknown qmi misc %d\n", MINOR(ip->i_rdev));
		return -ENODEV;
	}

	fp->private_data = ctxt;

	mutex_lock(&ctxt->lock);
	if (ctxt->ch == 0)
		r = smd_open(ctxt->ch_name, &ctxt->ch, ctxt, qmi_notify);
	if (r == 0)
		wake_up(&qmi_wait_queue);
	mutex_unlock(&ctxt->lock);

	return r;
}

static int qmi_release(struct inode *ip, struct file *fp)
{
	return 0;
}

static struct file_operations qmi_fops = {
	.owner = THIS_MODULE,
	.read = qmi_read,
	.write = qmi_write,
	.open = qmi_open,
	.release = qmi_release,
};

static struct qmi_ctxt qmi_device0 = {
	.ch_name = "SMD_DATA5_CNTL",
	.misc = {
		.minor = MISC_DYNAMIC_MINOR,
		.name = "qmi0",
		.fops = &qmi_fops,
	}
};
static struct qmi_ctxt qmi_device1 = {
	.ch_name = "SMD_DATA6_CNTL",
	.misc = {
		.minor = MISC_DYNAMIC_MINOR,
		.name = "qmi1",
		.fops = &qmi_fops,
	}
};
static struct qmi_ctxt qmi_device2 = {
	.ch_name = "SMD_DATA7_CNTL",
	.misc = {
		.minor = MISC_DYNAMIC_MINOR,
		.name = "qmi2",
		.fops = &qmi_fops,
	}
};

static struct qmi_ctxt *qmi_minor_to_ctxt(unsigned n)
{
	if (n == qmi_device0.misc.minor)
		return &qmi_device0;
	if (n == qmi_device1.misc.minor)
		return &qmi_device1;
	if (n == qmi_device2.misc.minor)
		return &qmi_device2;
	return 0;
}

static int __init qmi_init(void)
{
	int ret;

	qmi_wq = create_singlethread_workqueue("qmi");
	if (qmi_wq == 0)
		return -ENOMEM;

	qmi_ctxt_init(&qmi_device0, 0);
	qmi_ctxt_init(&qmi_device1, 1);
	qmi_ctxt_init(&qmi_device2, 2);

	ret = misc_register(&qmi_device0.misc);
	if (ret == 0)
		ret = misc_register(&qmi_device1.misc);
	if (ret == 0)
		ret = misc_register(&qmi_device2.misc);
	return ret;
}

module_init(qmi_init);
