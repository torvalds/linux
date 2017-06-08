/*
 * Generic driver for the OLPC Embedded Controller.
 *
 * Author: Andres Salomon <dilinger@queued.net>
 *
 * Copyright (C) 2011-2012 One Laptop per Child Foundation.
 *
 * Licensed under the GPL v2 or later.
 */
#include <linux/completion.h>
#include <linux/debugfs.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/olpc-ec.h>
#include <asm/olpc.h>

struct ec_cmd_desc {
	u8 cmd;
	u8 *inbuf, *outbuf;
	size_t inlen, outlen;

	int err;
	struct completion finished;
	struct list_head node;

	void *priv;
};

struct olpc_ec_priv {
	struct olpc_ec_driver *drv;
	struct work_struct worker;
	struct mutex cmd_lock;

	/* Pending EC commands */
	struct list_head cmd_q;
	spinlock_t cmd_q_lock;

	struct dentry *dbgfs_dir;

	/*
	 * Running an EC command while suspending means we don't always finish
	 * the command before the machine suspends.  This means that the EC
	 * is expecting the command protocol to finish, but we after a period
	 * of time (while the OS is asleep) the EC times out and restarts its
	 * idle loop.  Meanwhile, the OS wakes up, thinks it's still in the
	 * middle of the command protocol, starts throwing random things at
	 * the EC... and everyone's uphappy.
	 */
	bool suspended;
};

static struct olpc_ec_driver *ec_driver;
static struct olpc_ec_priv *ec_priv;
static void *ec_cb_arg;

void olpc_ec_driver_register(struct olpc_ec_driver *drv, void *arg)
{
	ec_driver = drv;
	ec_cb_arg = arg;
}
EXPORT_SYMBOL_GPL(olpc_ec_driver_register);

static void olpc_ec_worker(struct work_struct *w)
{
	struct olpc_ec_priv *ec = container_of(w, struct olpc_ec_priv, worker);
	struct ec_cmd_desc *desc = NULL;
	unsigned long flags;

	/* Grab the first pending command from the queue */
	spin_lock_irqsave(&ec->cmd_q_lock, flags);
	if (!list_empty(&ec->cmd_q)) {
		desc = list_first_entry(&ec->cmd_q, struct ec_cmd_desc, node);
		list_del(&desc->node);
	}
	spin_unlock_irqrestore(&ec->cmd_q_lock, flags);

	/* Do we actually have anything to do? */
	if (!desc)
		return;

	/* Protect the EC hw with a mutex; only run one cmd at a time */
	mutex_lock(&ec->cmd_lock);
	desc->err = ec_driver->ec_cmd(desc->cmd, desc->inbuf, desc->inlen,
			desc->outbuf, desc->outlen, ec_cb_arg);
	mutex_unlock(&ec->cmd_lock);

	/* Finished, wake up olpc_ec_cmd() */
	complete(&desc->finished);

	/* Run the worker thread again in case there are more cmds pending */
	schedule_work(&ec->worker);
}

/*
 * Throw a cmd descripter onto the list.  We now have SMP OLPC machines, so
 * locking is pretty critical.
 */
static void queue_ec_descriptor(struct ec_cmd_desc *desc,
		struct olpc_ec_priv *ec)
{
	unsigned long flags;

	INIT_LIST_HEAD(&desc->node);

	spin_lock_irqsave(&ec->cmd_q_lock, flags);
	list_add_tail(&desc->node, &ec->cmd_q);
	spin_unlock_irqrestore(&ec->cmd_q_lock, flags);

	schedule_work(&ec->worker);
}

int olpc_ec_cmd(u8 cmd, u8 *inbuf, size_t inlen, u8 *outbuf, size_t outlen)
{
	struct olpc_ec_priv *ec = ec_priv;
	struct ec_cmd_desc desc;

	/* Ensure a driver and ec hook have been registered */
	if (WARN_ON(!ec_driver || !ec_driver->ec_cmd))
		return -ENODEV;

	if (!ec)
		return -ENOMEM;

	/* Suspending in the middle of a command hoses things really badly */
	if (WARN_ON(ec->suspended))
		return -EBUSY;

	might_sleep();

	desc.cmd = cmd;
	desc.inbuf = inbuf;
	desc.outbuf = outbuf;
	desc.inlen = inlen;
	desc.outlen = outlen;
	desc.err = 0;
	init_completion(&desc.finished);

	queue_ec_descriptor(&desc, ec);

	/* Timeouts must be handled in the platform-specific EC hook */
	wait_for_completion(&desc.finished);

	/* The worker thread dequeues the cmd; no need to do anything here */
	return desc.err;
}
EXPORT_SYMBOL_GPL(olpc_ec_cmd);

#ifdef CONFIG_DEBUG_FS

/*
 * debugfs support for "generic commands", to allow sending
 * arbitrary EC commands from userspace.
 */

#define EC_MAX_CMD_ARGS (5 + 1)		/* cmd byte + 5 args */
#define EC_MAX_CMD_REPLY (8)

static DEFINE_MUTEX(ec_dbgfs_lock);
static unsigned char ec_dbgfs_resp[EC_MAX_CMD_REPLY];
static unsigned int ec_dbgfs_resp_bytes;

static ssize_t ec_dbgfs_cmd_write(struct file *file, const char __user *buf,
		size_t size, loff_t *ppos)
{
	int i, m;
	unsigned char ec_cmd[EC_MAX_CMD_ARGS];
	unsigned int ec_cmd_int[EC_MAX_CMD_ARGS];
	char cmdbuf[64];
	int ec_cmd_bytes;

	mutex_lock(&ec_dbgfs_lock);

	size = simple_write_to_buffer(cmdbuf, sizeof(cmdbuf), ppos, buf, size);

	m = sscanf(cmdbuf, "%x:%u %x %x %x %x %x", &ec_cmd_int[0],
			&ec_dbgfs_resp_bytes, &ec_cmd_int[1], &ec_cmd_int[2],
			&ec_cmd_int[3], &ec_cmd_int[4], &ec_cmd_int[5]);
	if (m < 2 || ec_dbgfs_resp_bytes > EC_MAX_CMD_REPLY) {
		/* reset to prevent overflow on read */
		ec_dbgfs_resp_bytes = 0;

		pr_debug("olpc-ec: bad ec cmd:  cmd:response-count [arg1 [arg2 ...]]\n");
		size = -EINVAL;
		goto out;
	}

	/* convert scanf'd ints to char */
	ec_cmd_bytes = m - 2;
	for (i = 0; i <= ec_cmd_bytes; i++)
		ec_cmd[i] = ec_cmd_int[i];

	pr_debug("olpc-ec: debugfs cmd 0x%02x with %d args %5ph, want %d returns\n",
			ec_cmd[0], ec_cmd_bytes, ec_cmd + 1,
			ec_dbgfs_resp_bytes);

	olpc_ec_cmd(ec_cmd[0], (ec_cmd_bytes == 0) ? NULL : &ec_cmd[1],
			ec_cmd_bytes, ec_dbgfs_resp, ec_dbgfs_resp_bytes);

	pr_debug("olpc-ec: response %8ph (%d bytes expected)\n",
			ec_dbgfs_resp, ec_dbgfs_resp_bytes);

out:
	mutex_unlock(&ec_dbgfs_lock);
	return size;
}

static ssize_t ec_dbgfs_cmd_read(struct file *file, char __user *buf,
		size_t size, loff_t *ppos)
{
	unsigned int i, r;
	char *rp;
	char respbuf[64];

	mutex_lock(&ec_dbgfs_lock);
	rp = respbuf;
	rp += sprintf(rp, "%02x", ec_dbgfs_resp[0]);
	for (i = 1; i < ec_dbgfs_resp_bytes; i++)
		rp += sprintf(rp, ", %02x", ec_dbgfs_resp[i]);
	mutex_unlock(&ec_dbgfs_lock);
	rp += sprintf(rp, "\n");

	r = rp - respbuf;
	return simple_read_from_buffer(buf, size, ppos, respbuf, r);
}

static const struct file_operations ec_dbgfs_ops = {
	.write = ec_dbgfs_cmd_write,
	.read = ec_dbgfs_cmd_read,
};

static struct dentry *olpc_ec_setup_debugfs(void)
{
	struct dentry *dbgfs_dir;

	dbgfs_dir = debugfs_create_dir("olpc-ec", NULL);
	if (IS_ERR_OR_NULL(dbgfs_dir))
		return NULL;

	debugfs_create_file("cmd", 0600, dbgfs_dir, NULL, &ec_dbgfs_ops);

	return dbgfs_dir;
}

#else

static struct dentry *olpc_ec_setup_debugfs(void)
{
	return NULL;
}

#endif /* CONFIG_DEBUG_FS */

static int olpc_ec_probe(struct platform_device *pdev)
{
	struct olpc_ec_priv *ec;
	int err;

	if (!ec_driver)
		return -ENODEV;

	ec = kzalloc(sizeof(*ec), GFP_KERNEL);
	if (!ec)
		return -ENOMEM;

	ec->drv = ec_driver;
	INIT_WORK(&ec->worker, olpc_ec_worker);
	mutex_init(&ec->cmd_lock);

	INIT_LIST_HEAD(&ec->cmd_q);
	spin_lock_init(&ec->cmd_q_lock);

	ec_priv = ec;
	platform_set_drvdata(pdev, ec);

	err = ec_driver->probe ? ec_driver->probe(pdev) : 0;
	if (err) {
		ec_priv = NULL;
		kfree(ec);
	} else {
		ec->dbgfs_dir = olpc_ec_setup_debugfs();
	}

	return err;
}

static int olpc_ec_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct olpc_ec_priv *ec = platform_get_drvdata(pdev);
	int err = 0;

	if (ec_driver->suspend)
		err = ec_driver->suspend(pdev);
	if (!err)
		ec->suspended = true;

	return err;
}

static int olpc_ec_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct olpc_ec_priv *ec = platform_get_drvdata(pdev);

	ec->suspended = false;
	return ec_driver->resume ? ec_driver->resume(pdev) : 0;
}

static const struct dev_pm_ops olpc_ec_pm_ops = {
	.suspend_late = olpc_ec_suspend,
	.resume_early = olpc_ec_resume,
};

static struct platform_driver olpc_ec_plat_driver = {
	.probe = olpc_ec_probe,
	.driver = {
		.name = "olpc-ec",
		.pm = &olpc_ec_pm_ops,
	},
};

static int __init olpc_ec_init_module(void)
{
	return platform_driver_register(&olpc_ec_plat_driver);
}
arch_initcall(olpc_ec_init_module);
