/*
 * ipmi_devintf.c
 *
 * Linux device interface for the IPMI message handler.
 *
 * Author: MontaVista Software, Inc.
 *         Corey Minyard <minyard@mvista.com>
 *         source@mvista.com
 *
 * Copyright 2002 MontaVista Software Inc.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *
 *  THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 *  WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 *  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 *  OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 *  ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 *  TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 *  USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/errno.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/ipmi.h>
#include <linux/mutex.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/compat.h>

struct ipmi_file_private
{
	ipmi_user_t          user;
	spinlock_t           recv_msg_lock;
	struct list_head     recv_msgs;
	struct file          *file;
	struct fasync_struct *fasync_queue;
	wait_queue_head_t    wait;
	struct mutex	     recv_mutex;
	int                  default_retries;
	unsigned int         default_retry_time_ms;
};

static DEFINE_MUTEX(ipmi_mutex);
static void file_receive_handler(struct ipmi_recv_msg *msg,
				 void                 *handler_data)
{
	struct ipmi_file_private *priv = handler_data;
	int                      was_empty;
	unsigned long            flags;

	spin_lock_irqsave(&(priv->recv_msg_lock), flags);

	was_empty = list_empty(&(priv->recv_msgs));
	list_add_tail(&(msg->link), &(priv->recv_msgs));

	if (was_empty) {
		wake_up_interruptible(&priv->wait);
		kill_fasync(&priv->fasync_queue, SIGIO, POLL_IN);
	}

	spin_unlock_irqrestore(&(priv->recv_msg_lock), flags);
}

static unsigned int ipmi_poll(struct file *file, poll_table *wait)
{
	struct ipmi_file_private *priv = file->private_data;
	unsigned int             mask = 0;
	unsigned long            flags;

	poll_wait(file, &priv->wait, wait);

	spin_lock_irqsave(&priv->recv_msg_lock, flags);

	if (!list_empty(&(priv->recv_msgs)))
		mask |= (POLLIN | POLLRDNORM);

	spin_unlock_irqrestore(&priv->recv_msg_lock, flags);

	return mask;
}

static int ipmi_fasync(int fd, struct file *file, int on)
{
	struct ipmi_file_private *priv = file->private_data;
	int                      result;

	mutex_lock(&ipmi_mutex); /* could race against open() otherwise */
	result = fasync_helper(fd, file, on, &priv->fasync_queue);
	mutex_unlock(&ipmi_mutex);

	return (result);
}

static const struct ipmi_user_hndl ipmi_hndlrs =
{
	.ipmi_recv_hndl	= file_receive_handler,
};

static int ipmi_open(struct inode *inode, struct file *file)
{
	int                      if_num = iminor(inode);
	int                      rv;
	struct ipmi_file_private *priv;


	priv = kmalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	mutex_lock(&ipmi_mutex);
	priv->file = file;

	rv = ipmi_create_user(if_num,
			      &ipmi_hndlrs,
			      priv,
			      &(priv->user));
	if (rv) {
		kfree(priv);
		goto out;
	}

	file->private_data = priv;

	spin_lock_init(&(priv->recv_msg_lock));
	INIT_LIST_HEAD(&(priv->recv_msgs));
	init_waitqueue_head(&priv->wait);
	priv->fasync_queue = NULL;
	mutex_init(&priv->recv_mutex);

	/* Use the low-level defaults. */
	priv->default_retries = -1;
	priv->default_retry_time_ms = 0;

out:
	mutex_unlock(&ipmi_mutex);
	return rv;
}

static int ipmi_release(struct inode *inode, struct file *file)
{
	struct ipmi_file_private *priv = file->private_data;
	int                      rv;
	struct  ipmi_recv_msg *msg, *next;

	rv = ipmi_destroy_user(priv->user);
	if (rv)
		return rv;

	list_for_each_entry_safe(msg, next, &priv->recv_msgs, link)
		ipmi_free_recv_msg(msg);


	kfree(priv);

	return 0;
}

static int handle_send_req(ipmi_user_t     user,
			   struct ipmi_req *req,
			   int             retries,
			   unsigned int    retry_time_ms)
{
	int              rv;
	struct ipmi_addr addr;
	struct kernel_ipmi_msg msg;

	if (req->addr_len > sizeof(struct ipmi_addr))
		return -EINVAL;

	if (copy_from_user(&addr, req->addr, req->addr_len))
		return -EFAULT;

	msg.netfn = req->msg.netfn;
	msg.cmd = req->msg.cmd;
	msg.data_len = req->msg.data_len;
	msg.data = kmalloc(IPMI_MAX_MSG_LENGTH, GFP_KERNEL);
	if (!msg.data)
		return -ENOMEM;

	/* From here out we cannot return, we must jump to "out" for
	   error exits to free msgdata. */

	rv = ipmi_validate_addr(&addr, req->addr_len);
	if (rv)
		goto out;

	if (req->msg.data != NULL) {
		if (req->msg.data_len > IPMI_MAX_MSG_LENGTH) {
			rv = -EMSGSIZE;
			goto out;
		}

		if (copy_from_user(msg.data,
				   req->msg.data,
				   req->msg.data_len))
		{
			rv = -EFAULT;
			goto out;
		}
	} else {
		msg.data_len = 0;
	}

	rv = ipmi_request_settime(user,
				  &addr,
				  req->msgid,
				  &msg,
				  NULL,
				  0,
				  retries,
				  retry_time_ms);
 out:
	kfree(msg.data);
	return rv;
}

static int ipmi_ioctl(struct file   *file,
		      unsigned int  cmd,
		      unsigned long data)
{
	int                      rv = -EINVAL;
	struct ipmi_file_private *priv = file->private_data;
	void __user *arg = (void __user *)data;

	switch (cmd) 
	{
	case IPMICTL_SEND_COMMAND:
	{
		struct ipmi_req req;

		if (copy_from_user(&req, arg, sizeof(req))) {
			rv = -EFAULT;
			break;
		}

		rv = handle_send_req(priv->user,
				     &req,
				     priv->default_retries,
				     priv->default_retry_time_ms);
		break;
	}

	case IPMICTL_SEND_COMMAND_SETTIME:
	{
		struct ipmi_req_settime req;

		if (copy_from_user(&req, arg, sizeof(req))) {
			rv = -EFAULT;
			break;
		}

		rv = handle_send_req(priv->user,
				     &req.req,
				     req.retries,
				     req.retry_time_ms);
		break;
	}

	case IPMICTL_RECEIVE_MSG:
	case IPMICTL_RECEIVE_MSG_TRUNC:
	{
		struct ipmi_recv      rsp;
		int              addr_len;
		struct list_head *entry;
		struct ipmi_recv_msg  *msg;
		unsigned long    flags;
		

		rv = 0;
		if (copy_from_user(&rsp, arg, sizeof(rsp))) {
			rv = -EFAULT;
			break;
		}

		/* We claim a mutex because we don't want two
                   users getting something from the queue at a time.
                   Since we have to release the spinlock before we can
                   copy the data to the user, it's possible another
                   user will grab something from the queue, too.  Then
                   the messages might get out of order if something
                   fails and the message gets put back onto the
                   queue.  This mutex prevents that problem. */
		mutex_lock(&priv->recv_mutex);

		/* Grab the message off the list. */
		spin_lock_irqsave(&(priv->recv_msg_lock), flags);
		if (list_empty(&(priv->recv_msgs))) {
			spin_unlock_irqrestore(&(priv->recv_msg_lock), flags);
			rv = -EAGAIN;
			goto recv_err;
		}
		entry = priv->recv_msgs.next;
		msg = list_entry(entry, struct ipmi_recv_msg, link);
		list_del(entry);
		spin_unlock_irqrestore(&(priv->recv_msg_lock), flags);

		addr_len = ipmi_addr_length(msg->addr.addr_type);
		if (rsp.addr_len < addr_len)
		{
			rv = -EINVAL;
			goto recv_putback_on_err;
		}

		if (copy_to_user(rsp.addr, &(msg->addr), addr_len)) {
			rv = -EFAULT;
			goto recv_putback_on_err;
		}
		rsp.addr_len = addr_len;

		rsp.recv_type = msg->recv_type;
		rsp.msgid = msg->msgid;
		rsp.msg.netfn = msg->msg.netfn;
		rsp.msg.cmd = msg->msg.cmd;

		if (msg->msg.data_len > 0) {
			if (rsp.msg.data_len < msg->msg.data_len) {
				rv = -EMSGSIZE;
				if (cmd == IPMICTL_RECEIVE_MSG_TRUNC) {
					msg->msg.data_len = rsp.msg.data_len;
				} else {
					goto recv_putback_on_err;
				}
			}

			if (copy_to_user(rsp.msg.data,
					 msg->msg.data,
					 msg->msg.data_len))
			{
				rv = -EFAULT;
				goto recv_putback_on_err;
			}
			rsp.msg.data_len = msg->msg.data_len;
		} else {
			rsp.msg.data_len = 0;
		}

		if (copy_to_user(arg, &rsp, sizeof(rsp))) {
			rv = -EFAULT;
			goto recv_putback_on_err;
		}

		mutex_unlock(&priv->recv_mutex);
		ipmi_free_recv_msg(msg);
		break;

	recv_putback_on_err:
		/* If we got an error, put the message back onto
		   the head of the queue. */
		spin_lock_irqsave(&(priv->recv_msg_lock), flags);
		list_add(entry, &(priv->recv_msgs));
		spin_unlock_irqrestore(&(priv->recv_msg_lock), flags);
		mutex_unlock(&priv->recv_mutex);
		break;

	recv_err:
		mutex_unlock(&priv->recv_mutex);
		break;
	}

	case IPMICTL_REGISTER_FOR_CMD:
	{
		struct ipmi_cmdspec val;

		if (copy_from_user(&val, arg, sizeof(val))) {
			rv = -EFAULT;
			break;
		}

		rv = ipmi_register_for_cmd(priv->user, val.netfn, val.cmd,
					   IPMI_CHAN_ALL);
		break;
	}

	case IPMICTL_UNREGISTER_FOR_CMD:
	{
		struct ipmi_cmdspec   val;

		if (copy_from_user(&val, arg, sizeof(val))) {
			rv = -EFAULT;
			break;
		}

		rv = ipmi_unregister_for_cmd(priv->user, val.netfn, val.cmd,
					     IPMI_CHAN_ALL);
		break;
	}

	case IPMICTL_REGISTER_FOR_CMD_CHANS:
	{
		struct ipmi_cmdspec_chans val;

		if (copy_from_user(&val, arg, sizeof(val))) {
			rv = -EFAULT;
			break;
		}

		rv = ipmi_register_for_cmd(priv->user, val.netfn, val.cmd,
					   val.chans);
		break;
	}

	case IPMICTL_UNREGISTER_FOR_CMD_CHANS:
	{
		struct ipmi_cmdspec_chans val;

		if (copy_from_user(&val, arg, sizeof(val))) {
			rv = -EFAULT;
			break;
		}

		rv = ipmi_unregister_for_cmd(priv->user, val.netfn, val.cmd,
					     val.chans);
		break;
	}

	case IPMICTL_SET_GETS_EVENTS_CMD:
	{
		int val;

		if (copy_from_user(&val, arg, sizeof(val))) {
			rv = -EFAULT;
			break;
		}

		rv = ipmi_set_gets_events(priv->user, val);
		break;
	}

	/* The next four are legacy, not per-channel. */
	case IPMICTL_SET_MY_ADDRESS_CMD:
	{
		unsigned int val;

		if (copy_from_user(&val, arg, sizeof(val))) {
			rv = -EFAULT;
			break;
		}

		rv = ipmi_set_my_address(priv->user, 0, val);
		break;
	}

	case IPMICTL_GET_MY_ADDRESS_CMD:
	{
		unsigned int  val;
		unsigned char rval;

		rv = ipmi_get_my_address(priv->user, 0, &rval);
		if (rv)
			break;

		val = rval;

		if (copy_to_user(arg, &val, sizeof(val))) {
			rv = -EFAULT;
			break;
		}
		break;
	}

	case IPMICTL_SET_MY_LUN_CMD:
	{
		unsigned int val;

		if (copy_from_user(&val, arg, sizeof(val))) {
			rv = -EFAULT;
			break;
		}

		rv = ipmi_set_my_LUN(priv->user, 0, val);
		break;
	}

	case IPMICTL_GET_MY_LUN_CMD:
	{
		unsigned int  val;
		unsigned char rval;

		rv = ipmi_get_my_LUN(priv->user, 0, &rval);
		if (rv)
			break;

		val = rval;

		if (copy_to_user(arg, &val, sizeof(val))) {
			rv = -EFAULT;
			break;
		}
		break;
	}

	case IPMICTL_SET_MY_CHANNEL_ADDRESS_CMD:
	{
		struct ipmi_channel_lun_address_set val;

		if (copy_from_user(&val, arg, sizeof(val))) {
			rv = -EFAULT;
			break;
		}

		return ipmi_set_my_address(priv->user, val.channel, val.value);
		break;
	}

	case IPMICTL_GET_MY_CHANNEL_ADDRESS_CMD:
	{
		struct ipmi_channel_lun_address_set val;

		if (copy_from_user(&val, arg, sizeof(val))) {
			rv = -EFAULT;
			break;
		}

		rv = ipmi_get_my_address(priv->user, val.channel, &val.value);
		if (rv)
			break;

		if (copy_to_user(arg, &val, sizeof(val))) {
			rv = -EFAULT;
			break;
		}
		break;
	}

	case IPMICTL_SET_MY_CHANNEL_LUN_CMD:
	{
		struct ipmi_channel_lun_address_set val;

		if (copy_from_user(&val, arg, sizeof(val))) {
			rv = -EFAULT;
			break;
		}

		rv = ipmi_set_my_LUN(priv->user, val.channel, val.value);
		break;
	}

	case IPMICTL_GET_MY_CHANNEL_LUN_CMD:
	{
		struct ipmi_channel_lun_address_set val;

		if (copy_from_user(&val, arg, sizeof(val))) {
			rv = -EFAULT;
			break;
		}

		rv = ipmi_get_my_LUN(priv->user, val.channel, &val.value);
		if (rv)
			break;

		if (copy_to_user(arg, &val, sizeof(val))) {
			rv = -EFAULT;
			break;
		}
		break;
	}

	case IPMICTL_SET_TIMING_PARMS_CMD:
	{
		struct ipmi_timing_parms parms;

		if (copy_from_user(&parms, arg, sizeof(parms))) {
			rv = -EFAULT;
			break;
		}

		priv->default_retries = parms.retries;
		priv->default_retry_time_ms = parms.retry_time_ms;
		rv = 0;
		break;
	}

	case IPMICTL_GET_TIMING_PARMS_CMD:
	{
		struct ipmi_timing_parms parms;

		parms.retries = priv->default_retries;
		parms.retry_time_ms = priv->default_retry_time_ms;

		if (copy_to_user(arg, &parms, sizeof(parms))) {
			rv = -EFAULT;
			break;
		}

		rv = 0;
		break;
	}

	case IPMICTL_GET_MAINTENANCE_MODE_CMD:
	{
		int mode;

		mode = ipmi_get_maintenance_mode(priv->user);
		if (copy_to_user(arg, &mode, sizeof(mode))) {
			rv = -EFAULT;
			break;
		}
		rv = 0;
		break;
	}

	case IPMICTL_SET_MAINTENANCE_MODE_CMD:
	{
		int mode;

		if (copy_from_user(&mode, arg, sizeof(mode))) {
			rv = -EFAULT;
			break;
		}
		rv = ipmi_set_maintenance_mode(priv->user, mode);
		break;
	}
	}
  
	return rv;
}

/*
 * Note: it doesn't make sense to take the BKL here but
 *       not in compat_ipmi_ioctl. -arnd
 */
static long ipmi_unlocked_ioctl(struct file   *file,
			        unsigned int  cmd,
			        unsigned long data)
{
	int ret;

	mutex_lock(&ipmi_mutex);
	ret = ipmi_ioctl(file, cmd, data);
	mutex_unlock(&ipmi_mutex);

	return ret;
}

#ifdef CONFIG_COMPAT

/*
 * The following code contains code for supporting 32-bit compatible
 * ioctls on 64-bit kernels.  This allows running 32-bit apps on the
 * 64-bit kernel
 */
#define COMPAT_IPMICTL_SEND_COMMAND	\
	_IOR(IPMI_IOC_MAGIC, 13, struct compat_ipmi_req)
#define COMPAT_IPMICTL_SEND_COMMAND_SETTIME	\
	_IOR(IPMI_IOC_MAGIC, 21, struct compat_ipmi_req_settime)
#define COMPAT_IPMICTL_RECEIVE_MSG	\
	_IOWR(IPMI_IOC_MAGIC, 12, struct compat_ipmi_recv)
#define COMPAT_IPMICTL_RECEIVE_MSG_TRUNC	\
	_IOWR(IPMI_IOC_MAGIC, 11, struct compat_ipmi_recv)

struct compat_ipmi_msg {
	u8		netfn;
	u8		cmd;
	u16		data_len;
	compat_uptr_t	data;
};

struct compat_ipmi_req {
	compat_uptr_t		addr;
	compat_uint_t		addr_len;
	compat_long_t		msgid;
	struct compat_ipmi_msg	msg;
};

struct compat_ipmi_recv {
	compat_int_t		recv_type;
	compat_uptr_t		addr;
	compat_uint_t		addr_len;
	compat_long_t		msgid;
	struct compat_ipmi_msg	msg;
};

struct compat_ipmi_req_settime {
	struct compat_ipmi_req	req;
	compat_int_t		retries;
	compat_uint_t		retry_time_ms;
};

/*
 * Define some helper functions for copying IPMI data
 */
static long get_compat_ipmi_msg(struct ipmi_msg *p64,
				struct compat_ipmi_msg __user *p32)
{
	compat_uptr_t tmp;

	if (!access_ok(VERIFY_READ, p32, sizeof(*p32)) ||
			__get_user(p64->netfn, &p32->netfn) ||
			__get_user(p64->cmd, &p32->cmd) ||
			__get_user(p64->data_len, &p32->data_len) ||
			__get_user(tmp, &p32->data))
		return -EFAULT;
	p64->data = compat_ptr(tmp);
	return 0;
}

static long put_compat_ipmi_msg(struct ipmi_msg *p64,
				struct compat_ipmi_msg __user *p32)
{
	if (!access_ok(VERIFY_WRITE, p32, sizeof(*p32)) ||
			__put_user(p64->netfn, &p32->netfn) ||
			__put_user(p64->cmd, &p32->cmd) ||
			__put_user(p64->data_len, &p32->data_len))
		return -EFAULT;
	return 0;
}

static long get_compat_ipmi_req(struct ipmi_req *p64,
				struct compat_ipmi_req __user *p32)
{

	compat_uptr_t	tmp;

	if (!access_ok(VERIFY_READ, p32, sizeof(*p32)) ||
			__get_user(tmp, &p32->addr) ||
			__get_user(p64->addr_len, &p32->addr_len) ||
			__get_user(p64->msgid, &p32->msgid) ||
			get_compat_ipmi_msg(&p64->msg, &p32->msg))
		return -EFAULT;
	p64->addr = compat_ptr(tmp);
	return 0;
}

static long get_compat_ipmi_req_settime(struct ipmi_req_settime *p64,
		struct compat_ipmi_req_settime __user *p32)
{
	if (!access_ok(VERIFY_READ, p32, sizeof(*p32)) ||
			get_compat_ipmi_req(&p64->req, &p32->req) ||
			__get_user(p64->retries, &p32->retries) ||
			__get_user(p64->retry_time_ms, &p32->retry_time_ms))
		return -EFAULT;
	return 0;
}

static long get_compat_ipmi_recv(struct ipmi_recv *p64,
				 struct compat_ipmi_recv __user *p32)
{
	compat_uptr_t tmp;

	if (!access_ok(VERIFY_READ, p32, sizeof(*p32)) ||
			__get_user(p64->recv_type, &p32->recv_type) ||
			__get_user(tmp, &p32->addr) ||
			__get_user(p64->addr_len, &p32->addr_len) ||
			__get_user(p64->msgid, &p32->msgid) ||
			get_compat_ipmi_msg(&p64->msg, &p32->msg))
		return -EFAULT;
	p64->addr = compat_ptr(tmp);
	return 0;
}

static long put_compat_ipmi_recv(struct ipmi_recv *p64,
				 struct compat_ipmi_recv __user *p32)
{
	if (!access_ok(VERIFY_WRITE, p32, sizeof(*p32)) ||
			__put_user(p64->recv_type, &p32->recv_type) ||
			__put_user(p64->addr_len, &p32->addr_len) ||
			__put_user(p64->msgid, &p32->msgid) ||
			put_compat_ipmi_msg(&p64->msg, &p32->msg))
		return -EFAULT;
	return 0;
}

/*
 * Handle compatibility ioctls
 */
static long compat_ipmi_ioctl(struct file *filep, unsigned int cmd,
			      unsigned long arg)
{
	int rc;
	struct ipmi_file_private *priv = filep->private_data;

	switch(cmd) {
	case COMPAT_IPMICTL_SEND_COMMAND:
	{
		struct ipmi_req	rp;

		if (get_compat_ipmi_req(&rp, compat_ptr(arg)))
			return -EFAULT;

		return handle_send_req(priv->user, &rp,
				priv->default_retries,
				priv->default_retry_time_ms);
	}
	case COMPAT_IPMICTL_SEND_COMMAND_SETTIME:
	{
		struct ipmi_req_settime	sp;

		if (get_compat_ipmi_req_settime(&sp, compat_ptr(arg)))
			return -EFAULT;

		return handle_send_req(priv->user, &sp.req,
				sp.retries, sp.retry_time_ms);
	}
	case COMPAT_IPMICTL_RECEIVE_MSG:
	case COMPAT_IPMICTL_RECEIVE_MSG_TRUNC:
	{
		struct ipmi_recv   __user *precv64;
		struct ipmi_recv   recv64;

		memset(&recv64, 0, sizeof(recv64));
		if (get_compat_ipmi_recv(&recv64, compat_ptr(arg)))
			return -EFAULT;

		precv64 = compat_alloc_user_space(sizeof(recv64));
		if (copy_to_user(precv64, &recv64, sizeof(recv64)))
			return -EFAULT;

		rc = ipmi_ioctl(filep,
				((cmd == COMPAT_IPMICTL_RECEIVE_MSG)
				 ? IPMICTL_RECEIVE_MSG
				 : IPMICTL_RECEIVE_MSG_TRUNC),
				(unsigned long) precv64);
		if (rc != 0)
			return rc;

		if (copy_from_user(&recv64, precv64, sizeof(recv64)))
			return -EFAULT;

		if (put_compat_ipmi_recv(&recv64, compat_ptr(arg)))
			return -EFAULT;

		return rc;
	}
	default:
		return ipmi_ioctl(filep, cmd, arg);
	}
}

static long unlocked_compat_ipmi_ioctl(struct file *filep, unsigned int cmd,
				       unsigned long arg)
{
	int ret;

	mutex_lock(&ipmi_mutex);
	ret = compat_ipmi_ioctl(filep, cmd, arg);
	mutex_unlock(&ipmi_mutex);

	return ret;
}
#endif

static const struct file_operations ipmi_fops = {
	.owner		= THIS_MODULE,
	.unlocked_ioctl	= ipmi_unlocked_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl   = unlocked_compat_ipmi_ioctl,
#endif
	.open		= ipmi_open,
	.release	= ipmi_release,
	.fasync		= ipmi_fasync,
	.poll		= ipmi_poll,
	.llseek		= noop_llseek,
};

#define DEVICE_NAME     "ipmidev"

static int ipmi_major;
module_param(ipmi_major, int, 0);
MODULE_PARM_DESC(ipmi_major, "Sets the major number of the IPMI device.  By"
		 " default, or if you set it to zero, it will choose the next"
		 " available device.  Setting it to -1 will disable the"
		 " interface.  Other values will set the major device number"
		 " to that value.");

/* Keep track of the devices that are registered. */
struct ipmi_reg_list {
	dev_t            dev;
	struct list_head link;
};
static LIST_HEAD(reg_list);
static DEFINE_MUTEX(reg_list_mutex);

static struct class *ipmi_class;

static void ipmi_new_smi(int if_num, struct device *device)
{
	dev_t dev = MKDEV(ipmi_major, if_num);
	struct ipmi_reg_list *entry;

	entry = kmalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry) {
		printk(KERN_ERR "ipmi_devintf: Unable to create the"
		       " ipmi class device link\n");
		return;
	}
	entry->dev = dev;

	mutex_lock(&reg_list_mutex);
	device_create(ipmi_class, device, dev, NULL, "ipmi%d", if_num);
	list_add(&entry->link, &reg_list);
	mutex_unlock(&reg_list_mutex);
}

static void ipmi_smi_gone(int if_num)
{
	dev_t dev = MKDEV(ipmi_major, if_num);
	struct ipmi_reg_list *entry;

	mutex_lock(&reg_list_mutex);
	list_for_each_entry(entry, &reg_list, link) {
		if (entry->dev == dev) {
			list_del(&entry->link);
			kfree(entry);
			break;
		}
	}
	device_destroy(ipmi_class, dev);
	mutex_unlock(&reg_list_mutex);
}

static struct ipmi_smi_watcher smi_watcher =
{
	.owner    = THIS_MODULE,
	.new_smi  = ipmi_new_smi,
	.smi_gone = ipmi_smi_gone,
};

static int __init init_ipmi_devintf(void)
{
	int rv;

	if (ipmi_major < 0)
		return -EINVAL;

	printk(KERN_INFO "ipmi device interface\n");

	ipmi_class = class_create(THIS_MODULE, "ipmi");
	if (IS_ERR(ipmi_class)) {
		printk(KERN_ERR "ipmi: can't register device class\n");
		return PTR_ERR(ipmi_class);
	}

	rv = register_chrdev(ipmi_major, DEVICE_NAME, &ipmi_fops);
	if (rv < 0) {
		class_destroy(ipmi_class);
		printk(KERN_ERR "ipmi: can't get major %d\n", ipmi_major);
		return rv;
	}

	if (ipmi_major == 0) {
		ipmi_major = rv;
	}

	rv = ipmi_smi_watcher_register(&smi_watcher);
	if (rv) {
		unregister_chrdev(ipmi_major, DEVICE_NAME);
		class_destroy(ipmi_class);
		printk(KERN_WARNING "ipmi: can't register smi watcher\n");
		return rv;
	}

	return 0;
}
module_init(init_ipmi_devintf);

static void __exit cleanup_ipmi(void)
{
	struct ipmi_reg_list *entry, *entry2;
	mutex_lock(&reg_list_mutex);
	list_for_each_entry_safe(entry, entry2, &reg_list, link) {
		list_del(&entry->link);
		device_destroy(ipmi_class, entry->dev);
		kfree(entry);
	}
	mutex_unlock(&reg_list_mutex);
	class_destroy(ipmi_class);
	ipmi_smi_watcher_unregister(&smi_watcher);
	unregister_chrdev(ipmi_major, DEVICE_NAME);
}
module_exit(cleanup_ipmi);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Corey Minyard <minyard@mvista.com>");
MODULE_DESCRIPTION("Linux device interface for the IPMI message handler.");
