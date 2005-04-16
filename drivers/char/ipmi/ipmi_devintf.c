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

#include <linux/config.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/errno.h>
#include <asm/system.h>
#include <linux/sched.h>
#include <linux/poll.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/devfs_fs_kernel.h>
#include <linux/ipmi.h>
#include <asm/semaphore.h>
#include <linux/init.h>

#define IPMI_DEVINTF_VERSION "v33"

struct ipmi_file_private
{
	ipmi_user_t          user;
	spinlock_t           recv_msg_lock;
	struct list_head     recv_msgs;
	struct file          *file;
	struct fasync_struct *fasync_queue;
	wait_queue_head_t    wait;
	struct semaphore     recv_sem;
	int                  default_retries;
	unsigned int         default_retry_time_ms;
};

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

	if (! list_empty(&(priv->recv_msgs)))
		mask |= (POLLIN | POLLRDNORM);

	spin_unlock_irqrestore(&priv->recv_msg_lock, flags);

	return mask;
}

static int ipmi_fasync(int fd, struct file *file, int on)
{
	struct ipmi_file_private *priv = file->private_data;
	int                      result;

	result = fasync_helper(fd, file, on, &priv->fasync_queue);

	return (result);
}

static struct ipmi_user_hndl ipmi_hndlrs =
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

	priv->file = file;

	rv = ipmi_create_user(if_num,
			      &ipmi_hndlrs,
			      priv,
			      &(priv->user));
	if (rv) {
		kfree(priv);
		return rv;
	}

	file->private_data = priv;

	spin_lock_init(&(priv->recv_msg_lock));
	INIT_LIST_HEAD(&(priv->recv_msgs));
	init_waitqueue_head(&priv->wait);
	priv->fasync_queue = NULL;
	sema_init(&(priv->recv_sem), 1);

	/* Use the low-level defaults. */
	priv->default_retries = -1;
	priv->default_retry_time_ms = 0;

	return 0;
}

static int ipmi_release(struct inode *inode, struct file *file)
{
	struct ipmi_file_private *priv = file->private_data;
	int                      rv;

	rv = ipmi_destroy_user(priv->user);
	if (rv)
		return rv;

	ipmi_fasync (-1, file, 0);

	/* FIXME - free the messages in the list. */
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

static int ipmi_ioctl(struct inode  *inode,
		      struct file   *file,
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

		/* We claim a semaphore because we don't want two
                   users getting something from the queue at a time.
                   Since we have to release the spinlock before we can
                   copy the data to the user, it's possible another
                   user will grab something from the queue, too.  Then
                   the messages might get out of order if something
                   fails and the message gets put back onto the
                   queue.  This semaphore prevents that problem. */
		down(&(priv->recv_sem));

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

		up(&(priv->recv_sem));
		ipmi_free_recv_msg(msg);
		break;

	recv_putback_on_err:
		/* If we got an error, put the message back onto
		   the head of the queue. */
		spin_lock_irqsave(&(priv->recv_msg_lock), flags);
		list_add(entry, &(priv->recv_msgs));
		spin_unlock_irqrestore(&(priv->recv_msg_lock), flags);
		up(&(priv->recv_sem));
		break;

	recv_err:
		up(&(priv->recv_sem));
		break;
	}

	case IPMICTL_REGISTER_FOR_CMD:
	{
		struct ipmi_cmdspec val;

		if (copy_from_user(&val, arg, sizeof(val))) {
			rv = -EFAULT;
			break;
		}

		rv = ipmi_register_for_cmd(priv->user, val.netfn, val.cmd);
		break;
	}

	case IPMICTL_UNREGISTER_FOR_CMD:
	{
		struct ipmi_cmdspec   val;

		if (copy_from_user(&val, arg, sizeof(val))) {
			rv = -EFAULT;
			break;
		}

		rv = ipmi_unregister_for_cmd(priv->user, val.netfn, val.cmd);
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

	case IPMICTL_SET_MY_ADDRESS_CMD:
	{
		unsigned int val;

		if (copy_from_user(&val, arg, sizeof(val))) {
			rv = -EFAULT;
			break;
		}

		ipmi_set_my_address(priv->user, val);
		rv = 0;
		break;
	}

	case IPMICTL_GET_MY_ADDRESS_CMD:
	{
		unsigned int val;

		val = ipmi_get_my_address(priv->user);

		if (copy_to_user(arg, &val, sizeof(val))) {
			rv = -EFAULT;
			break;
		}
		rv = 0;
		break;
	}

	case IPMICTL_SET_MY_LUN_CMD:
	{
		unsigned int val;

		if (copy_from_user(&val, arg, sizeof(val))) {
			rv = -EFAULT;
			break;
		}

		ipmi_set_my_LUN(priv->user, val);
		rv = 0;
		break;
	}

	case IPMICTL_GET_MY_LUN_CMD:
	{
		unsigned int val;

		val = ipmi_get_my_LUN(priv->user);

		if (copy_to_user(arg, &val, sizeof(val))) {
			rv = -EFAULT;
			break;
		}
		rv = 0;
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
	}
  
	return rv;
}


static struct file_operations ipmi_fops = {
	.owner		= THIS_MODULE,
	.ioctl		= ipmi_ioctl,
	.open		= ipmi_open,
	.release	= ipmi_release,
	.fasync		= ipmi_fasync,
	.poll		= ipmi_poll,
};

#define DEVICE_NAME     "ipmidev"

static int ipmi_major = 0;
module_param(ipmi_major, int, 0);
MODULE_PARM_DESC(ipmi_major, "Sets the major number of the IPMI device.  By"
		 " default, or if you set it to zero, it will choose the next"
		 " available device.  Setting it to -1 will disable the"
		 " interface.  Other values will set the major device number"
		 " to that value.");

static void ipmi_new_smi(int if_num)
{
	devfs_mk_cdev(MKDEV(ipmi_major, if_num),
		      S_IFCHR | S_IRUSR | S_IWUSR,
		      "ipmidev/%d", if_num);
}

static void ipmi_smi_gone(int if_num)
{
	devfs_remove("ipmidev/%d", if_num);
}

static struct ipmi_smi_watcher smi_watcher =
{
	.owner    = THIS_MODULE,
	.new_smi  = ipmi_new_smi,
	.smi_gone = ipmi_smi_gone,
};

static __init int init_ipmi_devintf(void)
{
	int rv;

	if (ipmi_major < 0)
		return -EINVAL;

	printk(KERN_INFO "ipmi device interface version "
	       IPMI_DEVINTF_VERSION "\n");

	rv = register_chrdev(ipmi_major, DEVICE_NAME, &ipmi_fops);
	if (rv < 0) {
		printk(KERN_ERR "ipmi: can't get major %d\n", ipmi_major);
		return rv;
	}

	if (ipmi_major == 0) {
		ipmi_major = rv;
	}

	devfs_mk_dir(DEVICE_NAME);

	rv = ipmi_smi_watcher_register(&smi_watcher);
	if (rv) {
		unregister_chrdev(ipmi_major, DEVICE_NAME);
		printk(KERN_WARNING "ipmi: can't register smi watcher\n");
		return rv;
	}

	return 0;
}
module_init(init_ipmi_devintf);

static __exit void cleanup_ipmi(void)
{
	ipmi_smi_watcher_unregister(&smi_watcher);
	devfs_remove(DEVICE_NAME);
	unregister_chrdev(ipmi_major, DEVICE_NAME);
}
module_exit(cleanup_ipmi);

MODULE_LICENSE("GPL");
