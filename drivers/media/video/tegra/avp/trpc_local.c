/*
 * Copyright (C) 2010 Google, Inc.
 *
 * Author:
 *   Dima Zavin <dima@android.com>
 *
 * Based on original NVRM code from NVIDIA, and a partial rewrite by
 *   Gary King <gking@nvidia.com>
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

#include <linux/err.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/list.h>
#include <linux/miscdevice.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/tegra_rpc.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/wait.h>

#include "trpc.h"
#include "trpc_sema.h"

struct rpc_info {
	struct trpc_endpoint	*rpc_ep;
	struct file		*sema_file;
};

/* ports names reserved for system functions, i.e. communicating with the
 * AVP */
static const char reserved_ports[][TEGRA_RPC_MAX_NAME_LEN] = {
	"RPC_AVP_PORT",
	"RPC_CPU_PORT",
};
static int num_reserved_ports = ARRAY_SIZE(reserved_ports);

static void rpc_notify_recv(struct trpc_endpoint *ep);

/* TODO: do we need to do anything when port is closed from the other side? */
static struct trpc_ep_ops ep_ops = {
	.notify_recv	= rpc_notify_recv,
};

static struct trpc_node rpc_node = {
	.name	= "local",
	.type	= TRPC_NODE_LOCAL,
};

static void rpc_notify_recv(struct trpc_endpoint *ep)
{
	struct rpc_info *info = trpc_priv(ep);

	if (WARN_ON(!info))
		return;
	if (info->sema_file)
		trpc_sema_signal(info->sema_file);
}

static int local_rpc_open(struct inode *inode, struct file *file)
{
	struct rpc_info *info;

	info = kzalloc(sizeof(struct rpc_info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	nonseekable_open(inode, file);
	file->private_data = info;
	return 0;
}

static int local_rpc_release(struct inode *inode, struct file *file)
{
	struct rpc_info *info = file->private_data;

	if (info->rpc_ep)
		trpc_close(info->rpc_ep);
	if (info->sema_file)
		fput(info->sema_file);
	kfree(info);
	file->private_data = NULL;
	return 0;
}

static int __get_port_desc(struct tegra_rpc_port_desc *desc,
			   unsigned int cmd, unsigned long arg)
{
	unsigned int size = _IOC_SIZE(cmd);

	if (size != sizeof(struct tegra_rpc_port_desc))
		return -EINVAL;
	if (copy_from_user(desc, (void __user *)arg, sizeof(*desc)))
		return -EFAULT;

	desc->name[TEGRA_RPC_MAX_NAME_LEN - 1] = '\0';
	return 0;
}

static char uniq_name[] = "aaaaaaaa+";
static const int uniq_len = sizeof(uniq_name) - 1;
static DEFINE_MUTEX(uniq_lock);

static void _gen_port_name(char *new_name)
{
	int i;

	mutex_lock(&uniq_lock);
	for (i = 0; i < uniq_len - 1; i++) {
		++uniq_name[i];
		if (uniq_name[i] != 'z')
			break;
		uniq_name[i] = 'a';
	}
	strlcpy(new_name, uniq_name, TEGRA_RPC_MAX_NAME_LEN);
	mutex_unlock(&uniq_lock);
}

static int _validate_port_name(const char *name)
{
	int i;

	for (i = 0; i < num_reserved_ports; i++)
		if (!strncmp(name, reserved_ports[i], TEGRA_RPC_MAX_NAME_LEN))
			return -EINVAL;
	return 0;
}

static long local_rpc_ioctl(struct file *file, unsigned int cmd,
			    unsigned long arg)
{
	struct rpc_info *info = file->private_data;
	struct tegra_rpc_port_desc desc;
	struct trpc_endpoint *ep;
	int ret = 0;

	if (_IOC_TYPE(cmd) != TEGRA_RPC_IOCTL_MAGIC ||
	    _IOC_NR(cmd) < TEGRA_RPC_IOCTL_MIN_NR ||
	    _IOC_NR(cmd) > TEGRA_RPC_IOCTL_MAX_NR) {
		ret = -ENOTTY;
		goto err;
	}

	switch (cmd) {
	case TEGRA_RPC_IOCTL_PORT_CREATE:
		if (info->rpc_ep) {
			ret = -EINVAL;
			goto err;
		}
		ret = __get_port_desc(&desc, cmd, arg);
		if (ret)
			goto err;
		if (desc.name[0]) {
			ret = _validate_port_name(desc.name);
			if (ret)
				goto err;
		} else {
			_gen_port_name(desc.name);
		}
		if (desc.notify_fd != -1) {
			/* grab a reference to the trpc_sema fd */
			info->sema_file = trpc_sema_get_from_fd(desc.notify_fd);
			if (IS_ERR(info->sema_file)) {
				ret = PTR_ERR(info->sema_file);
				info->sema_file = NULL;
				goto err;
			}
		}
		ep = trpc_create(&rpc_node, desc.name, &ep_ops, info);
		if (IS_ERR(ep)) {
			ret = PTR_ERR(ep);
			if (info->sema_file)
				fput(info->sema_file);
			info->sema_file = NULL;
			goto err;
		}
		info->rpc_ep = ep;
		break;
	case TEGRA_RPC_IOCTL_PORT_GET_NAME:
		if (!info->rpc_ep) {
			ret = -EINVAL;
			goto err;
		}
		if (copy_to_user((void __user *)arg,
				 trpc_name(info->rpc_ep),
				 TEGRA_RPC_MAX_NAME_LEN)) {
			ret = -EFAULT;
			goto err;
		}
		break;
	case TEGRA_RPC_IOCTL_PORT_CONNECT:
		if (!info->rpc_ep) {
			ret = -EINVAL;
			goto err;
		}
		ret = trpc_connect(info->rpc_ep, (long)arg);
		if (ret) {
			pr_err("%s: can't connect to '%s' (%d)\n", __func__,
			       trpc_name(info->rpc_ep), ret);
			goto err;
		}
		break;
	case TEGRA_RPC_IOCTL_PORT_LISTEN:
		if (!info->rpc_ep) {
			ret = -EINVAL;
			goto err;
		}
		ret = trpc_wait_peer(info->rpc_ep, (long)arg);
		if (ret) {
			pr_err("%s: error waiting for peer for '%s' (%d)\n",
			       __func__, trpc_name(info->rpc_ep), ret);
			goto err;
		}
		break;
	default:
		pr_err("%s: unknown cmd %d\n", __func__, _IOC_NR(cmd));
		ret = -EINVAL;
		goto err;
	}

	return 0;

err:
	if (ret && ret != -ERESTARTSYS)
		pr_err("tegra_rpc: pid=%d ioctl=%x/%lx (%x) ret=%d\n",
		       current->pid, cmd, arg, _IOC_NR(cmd), ret);
	return (long)ret;
}

static ssize_t local_rpc_write(struct file *file, const char __user *buf,
			       size_t count, loff_t *ppos)
{
	struct rpc_info *info = file->private_data;
	u8 data[TEGRA_RPC_MAX_MSG_LEN];
	int ret;

	if (!info)
		return -EINVAL;
	else if (count > TEGRA_RPC_MAX_MSG_LEN)
		return -EINVAL;

	if (copy_from_user(data, buf, count))
		return -EFAULT;

	ret = trpc_send_msg(&rpc_node, info->rpc_ep, data, count,
			    GFP_KERNEL);
	if (ret)
		return ret;
	return count;
}

static ssize_t local_rpc_read(struct file *file, char __user *buf, size_t max,
			      loff_t *ppos)
{
	struct rpc_info *info = file->private_data;
	int ret;
	u8 data[TEGRA_RPC_MAX_MSG_LEN];

	if (max > TEGRA_RPC_MAX_MSG_LEN)
		return -EINVAL;

	ret = trpc_recv_msg(&rpc_node, info->rpc_ep, data,
			    TEGRA_RPC_MAX_MSG_LEN, 0);
	if (ret == 0)
		return 0;
	else if (ret < 0)
		return ret;
	else if (ret > max)
		return -ENOSPC;
	else if (copy_to_user(buf, data, ret))
		return -EFAULT;

	return ret;
}

static const struct file_operations local_rpc_misc_fops = {
	.owner		= THIS_MODULE,
	.open		= local_rpc_open,
	.release	= local_rpc_release,
	.unlocked_ioctl	= local_rpc_ioctl,
	.write		= local_rpc_write,
	.read		= local_rpc_read,
};

static struct miscdevice local_rpc_misc_device = {
	.minor	= MISC_DYNAMIC_MINOR,
	.name	= "tegra_rpc",
	.fops	= &local_rpc_misc_fops,
};

int __init rpc_local_init(void)
{
	int ret;

	ret = trpc_sema_init();
	if (ret) {
		pr_err("%s: error in trpc_sema_init\n", __func__);
		goto err_sema_init;
	}

	ret = misc_register(&local_rpc_misc_device);
	if (ret) {
		pr_err("%s: can't register misc device\n", __func__);
		goto err_misc;
	}

	ret = trpc_node_register(&rpc_node);
	if (ret) {
		pr_err("%s: can't register rpc node\n", __func__);
		goto err_node_reg;
	}
	return 0;

err_node_reg:
	misc_deregister(&local_rpc_misc_device);
err_misc:
err_sema_init:
	return ret;
}

module_init(rpc_local_init);
