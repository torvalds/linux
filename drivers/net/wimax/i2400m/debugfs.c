/*
 * Intel Wireless WiMAX Connection 2400m
 * Debugfs interfaces to manipulate driver and device information
 *
 *
 * Copyright (C) 2007 Intel Corporation <linux-wimax@intel.com>
 * Inaky Perez-Gonzalez <inaky.perez-gonzalez@intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#include <linux/debugfs.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/spinlock.h>
#include <linux/device.h>
#include <linux/export.h>
#include "i2400m.h"


#define D_SUBMODULE debugfs
#include "debug-levels.h"

static
int debugfs_netdev_queue_stopped_get(void *data, u64 *val)
{
	struct i2400m *i2400m = data;
	*val = netif_queue_stopped(i2400m->wimax_dev.net_dev);
	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(fops_netdev_queue_stopped,
			debugfs_netdev_queue_stopped_get,
			NULL, "%llu\n");


static
struct dentry *debugfs_create_netdev_queue_stopped(
	const char *name, struct dentry *parent, struct i2400m *i2400m)
{
	return debugfs_create_file(name, 0400, parent, i2400m,
				   &fops_netdev_queue_stopped);
}

/*
 * We don't allow partial reads of this file, as then the reader would
 * get weirdly confused data as it is updated.
 *
 * So or you read it all or nothing; if you try to read with an offset
 * != 0, we consider you are done reading.
 */
static
ssize_t i2400m_rx_stats_read(struct file *filp, char __user *buffer,
			     size_t count, loff_t *ppos)
{
	struct i2400m *i2400m = filp->private_data;
	char buf[128];
	unsigned long flags;

	if (*ppos != 0)
		return 0;
	if (count < sizeof(buf))
		return -ENOSPC;
	spin_lock_irqsave(&i2400m->rx_lock, flags);
	snprintf(buf, sizeof(buf), "%u %u %u %u %u %u %u\n",
		 i2400m->rx_pl_num, i2400m->rx_pl_min,
		 i2400m->rx_pl_max, i2400m->rx_num,
		 i2400m->rx_size_acc,
		 i2400m->rx_size_min, i2400m->rx_size_max);
	spin_unlock_irqrestore(&i2400m->rx_lock, flags);
	return simple_read_from_buffer(buffer, count, ppos, buf, strlen(buf));
}


/* Any write clears the stats */
static
ssize_t i2400m_rx_stats_write(struct file *filp, const char __user *buffer,
			      size_t count, loff_t *ppos)
{
	struct i2400m *i2400m = filp->private_data;
	unsigned long flags;

	spin_lock_irqsave(&i2400m->rx_lock, flags);
	i2400m->rx_pl_num = 0;
	i2400m->rx_pl_max = 0;
	i2400m->rx_pl_min = UINT_MAX;
	i2400m->rx_num = 0;
	i2400m->rx_size_acc = 0;
	i2400m->rx_size_min = UINT_MAX;
	i2400m->rx_size_max = 0;
	spin_unlock_irqrestore(&i2400m->rx_lock, flags);
	return count;
}

static
const struct file_operations i2400m_rx_stats_fops = {
	.owner =	THIS_MODULE,
	.open =		simple_open,
	.read =		i2400m_rx_stats_read,
	.write =	i2400m_rx_stats_write,
	.llseek =	default_llseek,
};


/* See i2400m_rx_stats_read() */
static
ssize_t i2400m_tx_stats_read(struct file *filp, char __user *buffer,
			     size_t count, loff_t *ppos)
{
	struct i2400m *i2400m = filp->private_data;
	char buf[128];
	unsigned long flags;

	if (*ppos != 0)
		return 0;
	if (count < sizeof(buf))
		return -ENOSPC;
	spin_lock_irqsave(&i2400m->tx_lock, flags);
	snprintf(buf, sizeof(buf), "%u %u %u %u %u %u %u\n",
		 i2400m->tx_pl_num, i2400m->tx_pl_min,
		 i2400m->tx_pl_max, i2400m->tx_num,
		 i2400m->tx_size_acc,
		 i2400m->tx_size_min, i2400m->tx_size_max);
	spin_unlock_irqrestore(&i2400m->tx_lock, flags);
	return simple_read_from_buffer(buffer, count, ppos, buf, strlen(buf));
}

/* Any write clears the stats */
static
ssize_t i2400m_tx_stats_write(struct file *filp, const char __user *buffer,
			      size_t count, loff_t *ppos)
{
	struct i2400m *i2400m = filp->private_data;
	unsigned long flags;

	spin_lock_irqsave(&i2400m->tx_lock, flags);
	i2400m->tx_pl_num = 0;
	i2400m->tx_pl_max = 0;
	i2400m->tx_pl_min = UINT_MAX;
	i2400m->tx_num = 0;
	i2400m->tx_size_acc = 0;
	i2400m->tx_size_min = UINT_MAX;
	i2400m->tx_size_max = 0;
	spin_unlock_irqrestore(&i2400m->tx_lock, flags);
	return count;
}

static
const struct file_operations i2400m_tx_stats_fops = {
	.owner =	THIS_MODULE,
	.open =		simple_open,
	.read =		i2400m_tx_stats_read,
	.write =	i2400m_tx_stats_write,
	.llseek =	default_llseek,
};


/* Write 1 to ask the device to go into suspend */
static
int debugfs_i2400m_suspend_set(void *data, u64 val)
{
	int result;
	struct i2400m *i2400m = data;
	result = i2400m_cmd_enter_powersave(i2400m);
	if (result >= 0)
		result = 0;
	return result;
}
DEFINE_SIMPLE_ATTRIBUTE(fops_i2400m_suspend,
			NULL, debugfs_i2400m_suspend_set,
			"%llu\n");

static
struct dentry *debugfs_create_i2400m_suspend(
	const char *name, struct dentry *parent, struct i2400m *i2400m)
{
	return debugfs_create_file(name, 0200, parent, i2400m,
				   &fops_i2400m_suspend);
}


/*
 * Reset the device
 *
 * Write 0 to ask the device to soft reset, 1 to cold reset, 2 to bus
 * reset (as defined by enum i2400m_reset_type).
 */
static
int debugfs_i2400m_reset_set(void *data, u64 val)
{
	int result;
	struct i2400m *i2400m = data;
	enum i2400m_reset_type rt = val;
	switch(rt) {
	case I2400M_RT_WARM:
	case I2400M_RT_COLD:
	case I2400M_RT_BUS:
		result = i2400m_reset(i2400m, rt);
		if (result >= 0)
			result = 0;
		break;
	default:
		result = -EINVAL;
	}
	return result;
}
DEFINE_SIMPLE_ATTRIBUTE(fops_i2400m_reset,
			NULL, debugfs_i2400m_reset_set,
			"%llu\n");

static
struct dentry *debugfs_create_i2400m_reset(
	const char *name, struct dentry *parent, struct i2400m *i2400m)
{
	return debugfs_create_file(name, 0200, parent, i2400m,
				   &fops_i2400m_reset);
}


#define __debugfs_register(prefix, name, parent)			\
do {									\
	result = d_level_register_debugfs(prefix, name, parent);	\
	if (result < 0)							\
		goto error;						\
} while (0)


int i2400m_debugfs_add(struct i2400m *i2400m)
{
	int result;
	struct device *dev = i2400m_dev(i2400m);
	struct dentry *dentry = i2400m->wimax_dev.debugfs_dentry;
	struct dentry *fd;

	dentry = debugfs_create_dir("i2400m", dentry);
	result = PTR_ERR(dentry);
	if (IS_ERR(dentry)) {
		if (result == -ENODEV)
			result = 0;	/* No debugfs support */
		goto error;
	}
	i2400m->debugfs_dentry = dentry;
	__debugfs_register("dl_", control, dentry);
	__debugfs_register("dl_", driver, dentry);
	__debugfs_register("dl_", debugfs, dentry);
	__debugfs_register("dl_", fw, dentry);
	__debugfs_register("dl_", netdev, dentry);
	__debugfs_register("dl_", rfkill, dentry);
	__debugfs_register("dl_", rx, dentry);
	__debugfs_register("dl_", tx, dentry);

	fd = debugfs_create_size_t("tx_in", 0400, dentry,
				   &i2400m->tx_in);
	result = PTR_ERR(fd);
	if (IS_ERR(fd) && result != -ENODEV) {
		dev_err(dev, "Can't create debugfs entry "
			"tx_in: %d\n", result);
		goto error;
	}

	fd = debugfs_create_size_t("tx_out", 0400, dentry,
				   &i2400m->tx_out);
	result = PTR_ERR(fd);
	if (IS_ERR(fd) && result != -ENODEV) {
		dev_err(dev, "Can't create debugfs entry "
			"tx_out: %d\n", result);
		goto error;
	}

	fd = debugfs_create_u32("state", 0600, dentry,
				&i2400m->state);
	result = PTR_ERR(fd);
	if (IS_ERR(fd) && result != -ENODEV) {
		dev_err(dev, "Can't create debugfs entry "
			"state: %d\n", result);
		goto error;
	}

	/*
	 * Trace received messages from user space
	 *
	 * In order to tap the bidirectional message stream in the
	 * 'msg' pipe, user space can read from the 'msg' pipe;
	 * however, due to limitations in libnl, we can't know what
	 * the different applications are sending down to the kernel.
	 *
	 * So we have this hack where the driver will echo any message
	 * received on the msg pipe from user space [through a call to
	 * wimax_dev->op_msg_from_user() into
	 * i2400m_op_msg_from_user()] into the 'trace' pipe that this
	 * driver creates.
	 *
	 * So then, reading from both the 'trace' and 'msg' pipes in
	 * user space will provide a full dump of the traffic.
	 *
	 * Write 1 to activate, 0 to clear.
	 *
	 * It is not really very atomic, but it is also not too
	 * critical.
	 */
	fd = debugfs_create_u8("trace_msg_from_user", 0600, dentry,
			       &i2400m->trace_msg_from_user);
	result = PTR_ERR(fd);
	if (IS_ERR(fd) && result != -ENODEV) {
		dev_err(dev, "Can't create debugfs entry "
			"trace_msg_from_user: %d\n", result);
		goto error;
	}

	fd = debugfs_create_netdev_queue_stopped("netdev_queue_stopped",
						 dentry, i2400m);
	result = PTR_ERR(fd);
	if (IS_ERR(fd) && result != -ENODEV) {
		dev_err(dev, "Can't create debugfs entry "
			"netdev_queue_stopped: %d\n", result);
		goto error;
	}

	fd = debugfs_create_file("rx_stats", 0600, dentry, i2400m,
				 &i2400m_rx_stats_fops);
	result = PTR_ERR(fd);
	if (IS_ERR(fd) && result != -ENODEV) {
		dev_err(dev, "Can't create debugfs entry "
			"rx_stats: %d\n", result);
		goto error;
	}

	fd = debugfs_create_file("tx_stats", 0600, dentry, i2400m,
				 &i2400m_tx_stats_fops);
	result = PTR_ERR(fd);
	if (IS_ERR(fd) && result != -ENODEV) {
		dev_err(dev, "Can't create debugfs entry "
			"tx_stats: %d\n", result);
		goto error;
	}

	fd = debugfs_create_i2400m_suspend("suspend", dentry, i2400m);
	result = PTR_ERR(fd);
	if (IS_ERR(fd) && result != -ENODEV) {
		dev_err(dev, "Can't create debugfs entry suspend: %d\n",
			result);
		goto error;
	}

	fd = debugfs_create_i2400m_reset("reset", dentry, i2400m);
	result = PTR_ERR(fd);
	if (IS_ERR(fd) && result != -ENODEV) {
		dev_err(dev, "Can't create debugfs entry reset: %d\n", result);
		goto error;
	}

	result = 0;
error:
	return result;
}

void i2400m_debugfs_rm(struct i2400m *i2400m)
{
	debugfs_remove_recursive(i2400m->debugfs_dentry);
}
