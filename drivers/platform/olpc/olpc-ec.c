/*
 * Generic driver for the OLPC Embedded Controller.
 *
 * Copyright (C) 2011-2012 One Laptop per Child Foundation.
 *
 * Licensed under the GPL v2 or later.
 */
#include <linux/completion.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/module.h>
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

static void olpc_ec_worker(struct work_struct *w);

static DECLARE_WORK(ec_worker, olpc_ec_worker);
static LIST_HEAD(ec_cmd_q);
static DEFINE_SPINLOCK(ec_cmd_q_lock);

static struct olpc_ec_driver *ec_driver;
static struct olpc_ec_priv *ec_priv;
static void *ec_cb_arg;
static DEFINE_MUTEX(ec_cb_lock);

void olpc_ec_driver_register(struct olpc_ec_driver *drv, void *arg)
{
	ec_driver = drv;
	ec_cb_arg = arg;
}
EXPORT_SYMBOL_GPL(olpc_ec_driver_register);

static void olpc_ec_worker(struct work_struct *w)
{
	struct ec_cmd_desc *desc = NULL;
	unsigned long flags;

	/* Grab the first pending command from the queue */
	spin_lock_irqsave(&ec_cmd_q_lock, flags);
	if (!list_empty(&ec_cmd_q)) {
		desc = list_first_entry(&ec_cmd_q, struct ec_cmd_desc, node);
		list_del(&desc->node);
	}
	spin_unlock_irqrestore(&ec_cmd_q_lock, flags);

	/* Do we actually have anything to do? */
	if (!desc)
		return;

	/* Protect the EC hw with a mutex; only run one cmd at a time */
	mutex_lock(&ec_cb_lock);
	desc->err = ec_driver->ec_cmd(desc->cmd, desc->inbuf, desc->inlen,
			desc->outbuf, desc->outlen, ec_cb_arg);
	mutex_unlock(&ec_cb_lock);

	/* Finished, wake up olpc_ec_cmd() */
	complete(&desc->finished);

	/* Run the worker thread again in case there are more cmds pending */
	schedule_work(&ec_worker);
}

/*
 * Throw a cmd descripter onto the list.  We now have SMP OLPC machines, so
 * locking is pretty critical.
 */
static void queue_ec_descriptor(struct ec_cmd_desc *desc)
{
	unsigned long flags;

	INIT_LIST_HEAD(&desc->node);

	spin_lock_irqsave(&ec_cmd_q_lock, flags);
	list_add_tail(&desc->node, &ec_cmd_q);
	spin_unlock_irqrestore(&ec_cmd_q_lock, flags);

	schedule_work(&ec_worker);
}

int olpc_ec_cmd(u8 cmd, u8 *inbuf, size_t inlen, u8 *outbuf, size_t outlen)
{
	struct olpc_ec_priv *ec = ec_priv;
	struct ec_cmd_desc desc;

	/* XXX: this will be removed in later patches */
	/* Are we using old-style callers? */
	if (!ec_driver || !ec_driver->ec_cmd)
		return olpc_ec_cmd_x86(cmd, inbuf, inlen, outbuf, outlen);

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

	queue_ec_descriptor(&desc);

	/* Timeouts must be handled in the platform-specific EC hook */
	wait_for_completion(&desc.finished);

	/* The worker thread dequeues the cmd; no need to do anything here */
	return desc.err;
}
EXPORT_SYMBOL_GPL(olpc_ec_cmd);

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
	ec_priv = ec;
	platform_set_drvdata(pdev, ec);

	err = ec_driver->probe ? ec_driver->probe(pdev) : 0;

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

module_init(olpc_ec_init_module);

MODULE_AUTHOR("Andres Salomon <dilinger@queued.net>");
MODULE_LICENSE("GPL");
