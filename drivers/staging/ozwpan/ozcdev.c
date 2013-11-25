/* -----------------------------------------------------------------------------
 * Copyright (c) 2011 Ozmo Inc
 * Released under the GNU General Public License Version 2 (GPLv2).
 * -----------------------------------------------------------------------------
 */
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/netdevice.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include "ozconfig.h"
#include "ozprotocol.h"
#include "oztrace.h"
#include "ozappif.h"
#include "ozeltbuf.h"
#include "ozpd.h"
#include "ozproto.h"
#include "ozevent.h"
/*------------------------------------------------------------------------------
 */
#define OZ_RD_BUF_SZ	256
struct oz_cdev {
	dev_t devnum;
	struct cdev cdev;
	wait_queue_head_t rdq;
	spinlock_t lock;
	u8 active_addr[ETH_ALEN];
	struct oz_pd *active_pd;
};

/* Per PD context for the serial service stored in the PD. */
struct oz_serial_ctx {
	atomic_t ref_count;
	u8 tx_seq_num;
	u8 rx_seq_num;
	u8 rd_buf[OZ_RD_BUF_SZ];
	int rd_in;
	int rd_out;
};
/*------------------------------------------------------------------------------
 */
int g_taction;
/*------------------------------------------------------------------------------
 */
static struct oz_cdev g_cdev;
/*------------------------------------------------------------------------------
 * Context: process and softirq
 */
static struct oz_serial_ctx *oz_cdev_claim_ctx(struct oz_pd *pd)
{
	struct oz_serial_ctx *ctx;
	spin_lock_bh(&pd->app_lock[OZ_APPID_SERIAL-1]);
	ctx = (struct oz_serial_ctx *)pd->app_ctx[OZ_APPID_SERIAL-1];
	if (ctx)
		atomic_inc(&ctx->ref_count);
	spin_unlock_bh(&pd->app_lock[OZ_APPID_SERIAL-1]);
	return ctx;
}
/*------------------------------------------------------------------------------
 * Context: softirq or process
 */
static void oz_cdev_release_ctx(struct oz_serial_ctx *ctx)
{
	if (atomic_dec_and_test(&ctx->ref_count)) {
		oz_trace("Dealloc serial context.\n");
		kfree(ctx);
	}
}
/*------------------------------------------------------------------------------
 * Context: process
 */
int oz_cdev_open(struct inode *inode, struct file *filp)
{
	struct oz_cdev *dev;
	oz_trace("oz_cdev_open()\n");
	oz_trace("major = %d minor = %d\n", imajor(inode), iminor(inode));
	dev = container_of(inode->i_cdev, struct oz_cdev, cdev);
	filp->private_data = dev;
	return 0;
}
/*------------------------------------------------------------------------------
 * Context: process
 */
int oz_cdev_release(struct inode *inode, struct file *filp)
{
	oz_trace("oz_cdev_release()\n");
	return 0;
}
/*------------------------------------------------------------------------------
 * Context: process
 */
ssize_t oz_cdev_read(struct file *filp, char __user *buf, size_t count,
		loff_t *fpos)
{
	int n;
	int ix;

	struct oz_pd *pd;
	struct oz_serial_ctx *ctx = 0;

	spin_lock_bh(&g_cdev.lock);
	pd = g_cdev.active_pd;
	if (pd)
		oz_pd_get(pd);
	spin_unlock_bh(&g_cdev.lock);
	if (pd == 0)
		return -1;
	ctx = oz_cdev_claim_ctx(pd);
	if (ctx == 0)
		goto out2;
	n = ctx->rd_in - ctx->rd_out;
	if (n < 0)
		n += OZ_RD_BUF_SZ;
	if (count > n)
		count = n;
	ix = ctx->rd_out;
	n = OZ_RD_BUF_SZ - ix;
	if (n > count)
		n = count;
	if (copy_to_user(buf, &ctx->rd_buf[ix], n)) {
		count = 0;
		goto out1;
	}
	ix += n;
	if (ix == OZ_RD_BUF_SZ)
		ix = 0;
	if (n < count) {
		if (copy_to_user(&buf[n], ctx->rd_buf, count-n)) {
			count = 0;
			goto out1;
		}
		ix = count-n;
	}
	ctx->rd_out = ix;
out1:
	oz_cdev_release_ctx(ctx);
out2:
	oz_pd_put(pd);
	return count;
}
/*------------------------------------------------------------------------------
 * Context: process
 */
ssize_t oz_cdev_write(struct file *filp, const char __user *buf, size_t count,
		loff_t *fpos)
{
	struct oz_pd *pd;
	struct oz_elt_buf *eb;
	struct oz_elt_info *ei = 0;
	struct oz_elt *elt;
	struct oz_app_hdr *app_hdr;
	struct oz_serial_ctx *ctx;

	if (count > sizeof(ei->data) - sizeof(*elt) - sizeof(*app_hdr))
		return -EINVAL;

	spin_lock_bh(&g_cdev.lock);
	pd = g_cdev.active_pd;
	if (pd)
		oz_pd_get(pd);
	spin_unlock_bh(&g_cdev.lock);
	if (pd == 0)
		return -1;
	eb = &pd->elt_buff;
	ei = oz_elt_info_alloc(eb);
	if (ei == 0) {
		count = 0;
		goto out;
	}
	elt = (struct oz_elt *)ei->data;
	app_hdr = (struct oz_app_hdr *)(elt+1);
	elt->length = sizeof(struct oz_app_hdr) + count;
	elt->type = OZ_ELT_APP_DATA;
	ei->app_id = OZ_APPID_SERIAL;
	ei->length = elt->length + sizeof(struct oz_elt);
	app_hdr->app_id = OZ_APPID_SERIAL;
	if (copy_from_user(app_hdr+1, buf, count))
		goto out;
	spin_lock_bh(&pd->app_lock[OZ_APPID_USB-1]);
	ctx = (struct oz_serial_ctx *)pd->app_ctx[OZ_APPID_SERIAL-1];
	if (ctx) {
		app_hdr->elt_seq_num = ctx->tx_seq_num++;
		if (ctx->tx_seq_num == 0)
			ctx->tx_seq_num = 1;
		spin_lock(&eb->lock);
		if (oz_queue_elt_info(eb, 0, 0, ei) == 0)
			ei = 0;
		spin_unlock(&eb->lock);
	}
	spin_unlock_bh(&pd->app_lock[OZ_APPID_USB-1]);
out:
	if (ei) {
		count = 0;
		spin_lock_bh(&eb->lock);
		oz_elt_info_free(eb, ei);
		spin_unlock_bh(&eb->lock);
	}
	oz_pd_put(pd);
	return count;
}
/*------------------------------------------------------------------------------
 * Context: process
 */
static int oz_set_active_pd(u8 *addr)
{
	int rc = 0;
	struct oz_pd *pd;
	struct oz_pd *old_pd;
	pd = oz_pd_find(addr);
	if (pd) {
		spin_lock_bh(&g_cdev.lock);
		memcpy(g_cdev.active_addr, addr, ETH_ALEN);
		old_pd = g_cdev.active_pd;
		g_cdev.active_pd = pd;
		spin_unlock_bh(&g_cdev.lock);
		if (old_pd)
			oz_pd_put(old_pd);
	} else {
		if (!memcmp(addr, "\0\0\0\0\0\0", sizeof(addr))) {
			spin_lock_bh(&g_cdev.lock);
			pd = g_cdev.active_pd;
			g_cdev.active_pd = 0;
			memset(g_cdev.active_addr, 0,
				sizeof(g_cdev.active_addr));
			spin_unlock_bh(&g_cdev.lock);
			if (pd)
				oz_pd_put(pd);
		} else {
			rc = -1;
		}
	}
	return rc;
}
/*------------------------------------------------------------------------------
 * Context: process
 */
long oz_cdev_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	int rc = 0;
	if (_IOC_TYPE(cmd) != OZ_IOCTL_MAGIC)
		return -ENOTTY;
	if (_IOC_NR(cmd) > OZ_IOCTL_MAX)
		return -ENOTTY;
	if (_IOC_DIR(cmd) & _IOC_READ)
		rc = !access_ok(VERIFY_WRITE, (void __user *)arg,
			_IOC_SIZE(cmd));
	else if (_IOC_DIR(cmd) & _IOC_WRITE)
		rc = !access_ok(VERIFY_READ, (void __user *)arg,
			_IOC_SIZE(cmd));
	if (rc)
		return -EFAULT;
	switch (cmd) {
	case OZ_IOCTL_GET_PD_LIST: {
			struct oz_pd_list list;
			oz_trace("OZ_IOCTL_GET_PD_LIST\n");
			list.count = oz_get_pd_list(list.addr, OZ_MAX_PDS);
			if (copy_to_user((void __user *)arg, &list,
				sizeof(list)))
				return -EFAULT;
		}
		break;
	case OZ_IOCTL_SET_ACTIVE_PD: {
			u8 addr[ETH_ALEN];
			oz_trace("OZ_IOCTL_SET_ACTIVE_PD\n");
			if (copy_from_user(addr, (void __user *)arg, ETH_ALEN))
				return -EFAULT;
			rc = oz_set_active_pd(addr);
		}
		break;
	case OZ_IOCTL_GET_ACTIVE_PD: {
			u8 addr[ETH_ALEN];
			oz_trace("OZ_IOCTL_GET_ACTIVE_PD\n");
			spin_lock_bh(&g_cdev.lock);
			memcpy(addr, g_cdev.active_addr, ETH_ALEN);
			spin_unlock_bh(&g_cdev.lock);
			if (copy_to_user((void __user *)arg, addr, ETH_ALEN))
				return -EFAULT;
		}
		break;
#ifdef WANT_EVENT_TRACE
	case OZ_IOCTL_CLEAR_EVENTS:
		oz_events_clear();
		break;
	case OZ_IOCTL_GET_EVENTS:
		rc = oz_events_copy((void __user *)arg);
		break;
	case OZ_IOCTL_SET_EVENT_MASK:
		if (copy_from_user(&g_evt_mask, (void __user *)arg,
			sizeof(unsigned long))) {
			return -EFAULT;
		}
		break;
#endif /* WANT_EVENT_TRACE */
	case OZ_IOCTL_ADD_BINDING:
	case OZ_IOCTL_REMOVE_BINDING: {
			struct oz_binding_info b;
			if (copy_from_user(&b, (void __user *)arg,
				sizeof(struct oz_binding_info))) {
				return -EFAULT;
			}
			/* Make sure name is null terminated. */
			b.name[OZ_MAX_BINDING_LEN-1] = 0;
			if (cmd == OZ_IOCTL_ADD_BINDING)
				oz_binding_add(b.name);
			else
				oz_binding_remove(b.name);
		}
		break;
	}
	return rc;
}
/*------------------------------------------------------------------------------
 * Context: process
 */
unsigned int oz_cdev_poll(struct file *filp, poll_table *wait)
{
	unsigned int ret = 0;
	struct oz_cdev *dev = filp->private_data;
	oz_trace("Poll called wait = %p\n", wait);
	spin_lock_bh(&dev->lock);
	if (dev->active_pd) {
		struct oz_serial_ctx *ctx = oz_cdev_claim_ctx(dev->active_pd);
		if (ctx) {
			if (ctx->rd_in != ctx->rd_out)
				ret |= POLLIN | POLLRDNORM;
			oz_cdev_release_ctx(ctx);
		}
	}
	spin_unlock_bh(&dev->lock);
	if (wait)
		poll_wait(filp, &dev->rdq, wait);
	return ret;
}
/*------------------------------------------------------------------------------
 */
const struct file_operations oz_fops = {
	.owner =	THIS_MODULE,
	.open =		oz_cdev_open,
	.release =	oz_cdev_release,
	.read =		oz_cdev_read,
	.write =	oz_cdev_write,
	.unlocked_ioctl = oz_cdev_ioctl,
	.poll =		oz_cdev_poll
};
/*------------------------------------------------------------------------------
 * Context: process
 */
int oz_cdev_register(void)
{
	int err;
	memset(&g_cdev, 0, sizeof(g_cdev));
	err = alloc_chrdev_region(&g_cdev.devnum, 0, 1, "ozwpan");
	if (err < 0)
		return err;
	oz_trace("Alloc dev number %d:%d\n", MAJOR(g_cdev.devnum),
			MINOR(g_cdev.devnum));
	cdev_init(&g_cdev.cdev, &oz_fops);
	g_cdev.cdev.owner = THIS_MODULE;
	g_cdev.cdev.ops = &oz_fops;
	spin_lock_init(&g_cdev.lock);
	init_waitqueue_head(&g_cdev.rdq);
	err = cdev_add(&g_cdev.cdev, g_cdev.devnum, 1);
	return 0;
}
/*------------------------------------------------------------------------------
 * Context: process
 */
int oz_cdev_deregister(void)
{
	cdev_del(&g_cdev.cdev);
	unregister_chrdev_region(g_cdev.devnum, 1);
	return 0;
}
/*------------------------------------------------------------------------------
 * Context: process
 */
int oz_cdev_init(void)
{
	oz_event_log(OZ_EVT_SERVICE, 1, OZ_APPID_SERIAL, 0, 0);
	oz_app_enable(OZ_APPID_SERIAL, 1);
	return 0;
}
/*------------------------------------------------------------------------------
 * Context: process
 */
void oz_cdev_term(void)
{
	oz_event_log(OZ_EVT_SERVICE, 2, OZ_APPID_SERIAL, 0, 0);
	oz_app_enable(OZ_APPID_SERIAL, 0);
}
/*------------------------------------------------------------------------------
 * Context: softirq-serialized
 */
int oz_cdev_start(struct oz_pd *pd, int resume)
{
	struct oz_serial_ctx *ctx;
	struct oz_serial_ctx *old_ctx = 0;
	oz_event_log(OZ_EVT_SERVICE, 3, OZ_APPID_SERIAL, 0, resume);
	if (resume) {
		oz_trace("Serial service resumed.\n");
		return 0;
	}
	ctx = kzalloc(sizeof(struct oz_serial_ctx), GFP_ATOMIC);
	if (ctx == 0)
		return -ENOMEM;
	atomic_set(&ctx->ref_count, 1);
	ctx->tx_seq_num = 1;
	spin_lock_bh(&pd->app_lock[OZ_APPID_SERIAL-1]);
	old_ctx = pd->app_ctx[OZ_APPID_SERIAL-1];
	if (old_ctx) {
		spin_unlock_bh(&pd->app_lock[OZ_APPID_SERIAL-1]);
		kfree(ctx);
	} else {
		pd->app_ctx[OZ_APPID_SERIAL-1] = ctx;
		spin_unlock_bh(&pd->app_lock[OZ_APPID_SERIAL-1]);
	}
	spin_lock(&g_cdev.lock);
	if ((g_cdev.active_pd == 0) &&
		(memcmp(pd->mac_addr, g_cdev.active_addr, ETH_ALEN) == 0)) {
		oz_pd_get(pd);
		g_cdev.active_pd = pd;
		oz_trace("Active PD arrived.\n");
	}
	spin_unlock(&g_cdev.lock);
	oz_trace("Serial service started.\n");
	return 0;
}
/*------------------------------------------------------------------------------
 * Context: softirq or process
 */
void oz_cdev_stop(struct oz_pd *pd, int pause)
{
	struct oz_serial_ctx *ctx;
	oz_event_log(OZ_EVT_SERVICE, 4, OZ_APPID_SERIAL, 0, pause);
	if (pause) {
		oz_trace("Serial service paused.\n");
		return;
	}
	spin_lock_bh(&pd->app_lock[OZ_APPID_SERIAL-1]);
	ctx = (struct oz_serial_ctx *)pd->app_ctx[OZ_APPID_SERIAL-1];
	pd->app_ctx[OZ_APPID_SERIAL-1] = 0;
	spin_unlock_bh(&pd->app_lock[OZ_APPID_SERIAL-1]);
	if (ctx)
		oz_cdev_release_ctx(ctx);
	spin_lock(&g_cdev.lock);
	if (pd == g_cdev.active_pd)
		g_cdev.active_pd = 0;
	else
		pd = 0;
	spin_unlock(&g_cdev.lock);
	if (pd) {
		oz_pd_put(pd);
		oz_trace("Active PD departed.\n");
	}
	oz_trace("Serial service stopped.\n");
}
/*------------------------------------------------------------------------------
 * Context: softirq-serialized
 */
void oz_cdev_rx(struct oz_pd *pd, struct oz_elt *elt)
{
	struct oz_serial_ctx *ctx;
	struct oz_app_hdr *app_hdr;
	u8 *data;
	int len;
	int space;
	int copy_sz;
	int ix;

	ctx = oz_cdev_claim_ctx(pd);
	if (ctx == 0) {
		oz_trace("Cannot claim serial context.\n");
		return;
	}

	app_hdr = (struct oz_app_hdr *)(elt+1);
	/* If sequence number is non-zero then check it is not a duplicate.
	 */
	if (app_hdr->elt_seq_num != 0) {
		if (((ctx->rx_seq_num - app_hdr->elt_seq_num) & 0x80) == 0) {
			/* Reject duplicate element. */
			oz_trace("Duplicate element:%02x %02x\n",
				app_hdr->elt_seq_num, ctx->rx_seq_num);
			goto out;
		}
	}
	ctx->rx_seq_num = app_hdr->elt_seq_num;
	len = elt->length - sizeof(struct oz_app_hdr);
	data = ((u8 *)(elt+1)) + sizeof(struct oz_app_hdr);
	if (len <= 0)
		goto out;
	space = ctx->rd_out - ctx->rd_in - 1;
	if (space < 0)
		space += OZ_RD_BUF_SZ;
	if (len > space) {
		oz_trace("Not enough space:%d %d\n", len, space);
		len = space;
	}
	ix = ctx->rd_in;
	copy_sz = OZ_RD_BUF_SZ - ix;
	if (copy_sz > len)
		copy_sz = len;
	memcpy(&ctx->rd_buf[ix], data, copy_sz);
	len -= copy_sz;
	ix += copy_sz;
	if (ix == OZ_RD_BUF_SZ)
		ix = 0;
	if (len) {
		memcpy(ctx->rd_buf, data+copy_sz, len);
		ix = len;
	}
	ctx->rd_in = ix;
	wake_up(&g_cdev.rdq);
out:
	oz_cdev_release_ctx(ctx);
}
/*------------------------------------------------------------------------------
 * Context: softirq
 */
void oz_cdev_heartbeat(struct oz_pd *pd)
{
}
