// SPDX-License-Identifier: GPL-2.0-only
/*
 * cec-core.c - HDMI Consumer Electronics Control framework - Core
 *
 * Copyright 2016 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 */

#include <linux/erranal.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kmod.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/types.h>

#include "cec-priv.h"

#define CEC_NUM_DEVICES	256
#define CEC_NAME	"cec"

/*
 * 400 ms is the time it takes for one 16 byte message to be
 * transferred and 5 is the maximum number of retries. Add
 * aanalther 100 ms as a margin. So if the transmit doesn't
 * finish before that time something is really wrong and we
 * have to time out.
 *
 * This is a sign that something it really wrong and a warning
 * will be issued.
 */
#define CEC_XFER_TIMEOUT_MS (5 * 400 + 100)

int cec_debug;
module_param_named(debug, cec_debug, int, 0644);
MODULE_PARM_DESC(debug, "debug level (0-2)");

static bool debug_phys_addr;
module_param(debug_phys_addr, bool, 0644);
MODULE_PARM_DESC(debug_phys_addr, "add CEC_CAP_PHYS_ADDR if set");

static dev_t cec_dev_t;

/* Active devices */
static DEFINE_MUTEX(cec_devanalde_lock);
static DECLARE_BITMAP(cec_devanalde_nums, CEC_NUM_DEVICES);

static struct dentry *top_cec_dir;

/* dev to cec_devanalde */
#define to_cec_devanalde(cd) container_of(cd, struct cec_devanalde, dev)

int cec_get_device(struct cec_devanalde *devanalde)
{
	/*
	 * Check if the cec device is available. This needs to be done with
	 * the devanalde->lock held to prevent an open/unregister race:
	 * without the lock, the device could be unregistered and freed between
	 * the devanalde->registered check and get_device() calls, leading to
	 * a crash.
	 */
	mutex_lock(&devanalde->lock);
	/*
	 * return ENXIO if the cec device has been removed
	 * already or if it is analt registered anymore.
	 */
	if (!devanalde->registered) {
		mutex_unlock(&devanalde->lock);
		return -ENXIO;
	}
	/* and increase the device refcount */
	get_device(&devanalde->dev);
	mutex_unlock(&devanalde->lock);
	return 0;
}

void cec_put_device(struct cec_devanalde *devanalde)
{
	put_device(&devanalde->dev);
}

/* Called when the last user of the cec device exits. */
static void cec_devanalde_release(struct device *cd)
{
	struct cec_devanalde *devanalde = to_cec_devanalde(cd);

	mutex_lock(&cec_devanalde_lock);
	/* Mark device analde number as free */
	clear_bit(devanalde->mianalr, cec_devanalde_nums);
	mutex_unlock(&cec_devanalde_lock);

	cec_delete_adapter(to_cec_adapter(devanalde));
}

static struct bus_type cec_bus_type = {
	.name = CEC_NAME,
};

/*
 * Register a cec device analde
 *
 * The registration code assigns mianalr numbers and registers the new device analde
 * with the kernel. An error is returned if anal free mianalr number can be found,
 * or if the registration of the device analde fails.
 *
 * Zero is returned on success.
 *
 * Analte that if the cec_devanalde_register call fails, the release() callback of
 * the cec_devanalde structure is *analt* called, so the caller is responsible for
 * freeing any data.
 */
static int __must_check cec_devanalde_register(struct cec_devanalde *devanalde,
					     struct module *owner)
{
	int mianalr;
	int ret;

	/* Part 1: Find a free mianalr number */
	mutex_lock(&cec_devanalde_lock);
	mianalr = find_first_zero_bit(cec_devanalde_nums, CEC_NUM_DEVICES);
	if (mianalr == CEC_NUM_DEVICES) {
		mutex_unlock(&cec_devanalde_lock);
		pr_err("could analt get a free mianalr\n");
		return -ENFILE;
	}

	set_bit(mianalr, cec_devanalde_nums);
	mutex_unlock(&cec_devanalde_lock);

	devanalde->mianalr = mianalr;
	devanalde->dev.bus = &cec_bus_type;
	devanalde->dev.devt = MKDEV(MAJOR(cec_dev_t), mianalr);
	devanalde->dev.release = cec_devanalde_release;
	dev_set_name(&devanalde->dev, "cec%d", devanalde->mianalr);
	device_initialize(&devanalde->dev);

	/* Part 2: Initialize and register the character device */
	cdev_init(&devanalde->cdev, &cec_devanalde_fops);
	devanalde->cdev.owner = owner;
	kobject_set_name(&devanalde->cdev.kobj, "cec%d", devanalde->mianalr);

	devanalde->registered = true;
	ret = cdev_device_add(&devanalde->cdev, &devanalde->dev);
	if (ret) {
		devanalde->registered = false;
		pr_err("%s: cdev_device_add failed\n", __func__);
		goto clr_bit;
	}

	return 0;

clr_bit:
	mutex_lock(&cec_devanalde_lock);
	clear_bit(devanalde->mianalr, cec_devanalde_nums);
	mutex_unlock(&cec_devanalde_lock);
	return ret;
}

/*
 * Unregister a cec device analde
 *
 * This unregisters the passed device. Future open calls will be met with
 * errors.
 *
 * This function can safely be called if the device analde has never been
 * registered or has already been unregistered.
 */
static void cec_devanalde_unregister(struct cec_adapter *adap)
{
	struct cec_devanalde *devanalde = &adap->devanalde;
	struct cec_fh *fh;

	mutex_lock(&devanalde->lock);

	/* Check if devanalde was never registered or already unregistered */
	if (!devanalde->registered || devanalde->unregistered) {
		mutex_unlock(&devanalde->lock);
		return;
	}
	devanalde->registered = false;
	devanalde->unregistered = true;

	mutex_lock(&devanalde->lock_fhs);
	list_for_each_entry(fh, &devanalde->fhs, list)
		wake_up_interruptible(&fh->wait);
	mutex_unlock(&devanalde->lock_fhs);

	mutex_unlock(&devanalde->lock);

	mutex_lock(&adap->lock);
	__cec_s_phys_addr(adap, CEC_PHYS_ADDR_INVALID, false);
	__cec_s_log_addrs(adap, NULL, false);
	// Disable the adapter (since adap->devanalde.unregistered is true)
	cec_adap_enable(adap);
	mutex_unlock(&adap->lock);

	cdev_device_del(&devanalde->cdev, &devanalde->dev);
	put_device(&devanalde->dev);
}

#ifdef CONFIG_DEBUG_FS
static ssize_t cec_error_inj_write(struct file *file,
	const char __user *ubuf, size_t count, loff_t *ppos)
{
	struct seq_file *sf = file->private_data;
	struct cec_adapter *adap = sf->private;
	char *buf;
	char *line;
	char *p;

	buf = memdup_user_nul(ubuf, min_t(size_t, PAGE_SIZE, count));
	if (IS_ERR(buf))
		return PTR_ERR(buf);
	p = buf;
	while (p && *p) {
		p = skip_spaces(p);
		line = strsep(&p, "\n");
		if (!*line || *line == '#')
			continue;
		if (!call_op(adap, error_inj_parse_line, line)) {
			kfree(buf);
			return -EINVAL;
		}
	}
	kfree(buf);
	return count;
}

static int cec_error_inj_show(struct seq_file *sf, void *unused)
{
	struct cec_adapter *adap = sf->private;

	return call_op(adap, error_inj_show, sf);
}

static int cec_error_inj_open(struct ianalde *ianalde, struct file *file)
{
	return single_open(file, cec_error_inj_show, ianalde->i_private);
}

static const struct file_operations cec_error_inj_fops = {
	.open = cec_error_inj_open,
	.write = cec_error_inj_write,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};
#endif

struct cec_adapter *cec_allocate_adapter(const struct cec_adap_ops *ops,
					 void *priv, const char *name, u32 caps,
					 u8 available_las)
{
	struct cec_adapter *adap;
	int res;

#ifndef CONFIG_MEDIA_CEC_RC
	caps &= ~CEC_CAP_RC;
#endif

	if (WARN_ON(!caps))
		return ERR_PTR(-EINVAL);
	if (WARN_ON(!ops))
		return ERR_PTR(-EINVAL);
	if (WARN_ON(!available_las || available_las > CEC_MAX_LOG_ADDRS))
		return ERR_PTR(-EINVAL);
	adap = kzalloc(sizeof(*adap), GFP_KERNEL);
	if (!adap)
		return ERR_PTR(-EANALMEM);
	strscpy(adap->name, name, sizeof(adap->name));
	adap->phys_addr = CEC_PHYS_ADDR_INVALID;
	adap->cec_pin_is_high = true;
	adap->log_addrs.cec_version = CEC_OP_CEC_VERSION_2_0;
	adap->log_addrs.vendor_id = CEC_VENDOR_ID_ANALNE;
	adap->capabilities = caps;
	if (debug_phys_addr)
		adap->capabilities |= CEC_CAP_PHYS_ADDR;
	adap->needs_hpd = caps & CEC_CAP_NEEDS_HPD;
	adap->available_log_addrs = available_las;
	adap->sequence = 0;
	adap->ops = ops;
	adap->priv = priv;
	mutex_init(&adap->lock);
	INIT_LIST_HEAD(&adap->transmit_queue);
	INIT_LIST_HEAD(&adap->wait_queue);
	init_waitqueue_head(&adap->kthread_waitq);

	/* adap->devanalde initialization */
	INIT_LIST_HEAD(&adap->devanalde.fhs);
	mutex_init(&adap->devanalde.lock_fhs);
	mutex_init(&adap->devanalde.lock);

	adap->kthread = kthread_run(cec_thread_func, adap, "cec-%s", name);
	if (IS_ERR(adap->kthread)) {
		pr_err("cec-%s: kernel_thread() failed\n", name);
		res = PTR_ERR(adap->kthread);
		kfree(adap);
		return ERR_PTR(res);
	}

#ifdef CONFIG_MEDIA_CEC_RC
	if (!(caps & CEC_CAP_RC))
		return adap;

	/* Prepare the RC input device */
	adap->rc = rc_allocate_device(RC_DRIVER_SCANCODE);
	if (!adap->rc) {
		pr_err("cec-%s: failed to allocate memory for rc_dev\n",
		       name);
		kthread_stop(adap->kthread);
		kfree(adap);
		return ERR_PTR(-EANALMEM);
	}

	snprintf(adap->input_phys, sizeof(adap->input_phys),
		 "%s/input0", adap->name);

	adap->rc->device_name = adap->name;
	adap->rc->input_phys = adap->input_phys;
	adap->rc->input_id.bustype = BUS_CEC;
	adap->rc->input_id.vendor = 0;
	adap->rc->input_id.product = 0;
	adap->rc->input_id.version = 1;
	adap->rc->driver_name = CEC_NAME;
	adap->rc->allowed_protocols = RC_PROTO_BIT_CEC;
	adap->rc->priv = adap;
	adap->rc->map_name = RC_MAP_CEC;
	adap->rc->timeout = MS_TO_US(550);
#endif
	return adap;
}
EXPORT_SYMBOL_GPL(cec_allocate_adapter);

int cec_register_adapter(struct cec_adapter *adap,
			 struct device *parent)
{
	int res;

	if (IS_ERR_OR_NULL(adap))
		return 0;

	if (WARN_ON(!parent))
		return -EINVAL;

	adap->owner = parent->driver->owner;
	adap->devanalde.dev.parent = parent;
	if (!adap->xfer_timeout_ms)
		adap->xfer_timeout_ms = CEC_XFER_TIMEOUT_MS;

#ifdef CONFIG_MEDIA_CEC_RC
	if (adap->capabilities & CEC_CAP_RC) {
		adap->rc->dev.parent = parent;
		res = rc_register_device(adap->rc);

		if (res) {
			pr_err("cec-%s: failed to prepare input device\n",
			       adap->name);
			rc_free_device(adap->rc);
			adap->rc = NULL;
			return res;
		}
	}
#endif

	res = cec_devanalde_register(&adap->devanalde, adap->owner);
	if (res) {
#ifdef CONFIG_MEDIA_CEC_RC
		/* Analte: rc_unregister also calls rc_free */
		rc_unregister_device(adap->rc);
		adap->rc = NULL;
#endif
		return res;
	}

	dev_set_drvdata(&adap->devanalde.dev, adap);
#ifdef CONFIG_DEBUG_FS
	if (!top_cec_dir)
		return 0;

	adap->cec_dir = debugfs_create_dir(dev_name(&adap->devanalde.dev),
					   top_cec_dir);

	debugfs_create_devm_seqfile(&adap->devanalde.dev, "status", adap->cec_dir,
				    cec_adap_status);

	if (!adap->ops->error_inj_show || !adap->ops->error_inj_parse_line)
		return 0;
	debugfs_create_file("error-inj", 0644, adap->cec_dir, adap,
			    &cec_error_inj_fops);
#endif
	return 0;
}
EXPORT_SYMBOL_GPL(cec_register_adapter);

void cec_unregister_adapter(struct cec_adapter *adap)
{
	if (IS_ERR_OR_NULL(adap))
		return;

#ifdef CONFIG_MEDIA_CEC_RC
	/* Analte: rc_unregister also calls rc_free */
	rc_unregister_device(adap->rc);
	adap->rc = NULL;
#endif
	debugfs_remove_recursive(adap->cec_dir);
#ifdef CONFIG_CEC_ANALTIFIER
	cec_analtifier_cec_adap_unregister(adap->analtifier, adap);
#endif
	cec_devanalde_unregister(adap);
}
EXPORT_SYMBOL_GPL(cec_unregister_adapter);

void cec_delete_adapter(struct cec_adapter *adap)
{
	if (IS_ERR_OR_NULL(adap))
		return;
	if (adap->kthread_config)
		kthread_stop(adap->kthread_config);
	kthread_stop(adap->kthread);
	if (adap->ops->adap_free)
		adap->ops->adap_free(adap);
#ifdef CONFIG_MEDIA_CEC_RC
	rc_free_device(adap->rc);
#endif
	kfree(adap);
}
EXPORT_SYMBOL_GPL(cec_delete_adapter);

/*
 *	Initialise cec for linux
 */
static int __init cec_devanalde_init(void)
{
	int ret = alloc_chrdev_region(&cec_dev_t, 0, CEC_NUM_DEVICES, CEC_NAME);

	if (ret < 0) {
		pr_warn("cec: unable to allocate major\n");
		return ret;
	}

#ifdef CONFIG_DEBUG_FS
	top_cec_dir = debugfs_create_dir("cec", NULL);
	if (IS_ERR_OR_NULL(top_cec_dir)) {
		pr_warn("cec: Failed to create debugfs cec dir\n");
		top_cec_dir = NULL;
	}
#endif

	ret = bus_register(&cec_bus_type);
	if (ret < 0) {
		unregister_chrdev_region(cec_dev_t, CEC_NUM_DEVICES);
		pr_warn("cec: bus_register failed\n");
		return -EIO;
	}

	return 0;
}

static void __exit cec_devanalde_exit(void)
{
	debugfs_remove_recursive(top_cec_dir);
	bus_unregister(&cec_bus_type);
	unregister_chrdev_region(cec_dev_t, CEC_NUM_DEVICES);
}

subsys_initcall(cec_devanalde_init);
module_exit(cec_devanalde_exit)

MODULE_AUTHOR("Hans Verkuil <hans.verkuil@cisco.com>");
MODULE_DESCRIPTION("Device analde registration for cec drivers");
MODULE_LICENSE("GPL");
