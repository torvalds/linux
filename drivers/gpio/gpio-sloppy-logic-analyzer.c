// SPDX-License-Identifier: GPL-2.0-only
/*
 * Sloppy logic analyzer using GPIOs (to be run on an isolated CPU)
 *
 * Use the 'gpio-sloppy-logic-analyzer' script in the 'tools/gpio' folder for
 * easier usage and further documentation. Note that this is a last resort
 * analyzer which can be affected by latencies and non-deterministic code
 * paths. However, for e.g. remote development, it may be useful to get a first
 * view and aid further debugging.
 *
 * Copyright (C) Wolfram Sang <wsa@sang-engineering.com>
 * Copyright (C) Renesas Electronics Corporation
 */

#include <linux/ctype.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/init.h>
#include <linux/ktime.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/slab.h>
#include <linux/sizes.h>
#include <linux/timekeeping.h>
#include <linux/types.h>
#include <linux/vmalloc.h>

#define GPIO_LA_NAME "gpio-sloppy-logic-analyzer"
#define GPIO_LA_DEFAULT_BUF_SIZE SZ_256K
/* can be increased but then we need to extend the u8 buffers */
#define GPIO_LA_MAX_PROBES 8
#define GPIO_LA_NUM_TESTS 1024

struct gpio_la_poll_priv {
	struct mutex blob_lock; /* serialize access to the blob (data) */
	u32 buf_idx;
	struct gpio_descs *descs;
	unsigned long delay_ns;
	unsigned long acq_delay;
	struct debugfs_blob_wrapper blob;
	struct dentry *debug_dir;
	struct dentry *blob_dent;
	struct debugfs_blob_wrapper meta;
	struct device *dev;
	unsigned int trig_len;
	u8 *trig_data;
};

static struct dentry *gpio_la_poll_debug_dir;

static __always_inline int gpio_la_get_array(struct gpio_descs *d, unsigned long *sptr)
{
	int ret;

	ret = gpiod_get_array_value(d->ndescs, d->desc, d->info, sptr);
	if (ret == 0 && fatal_signal_pending(current))
		ret = -EINTR;

	return ret;
}

static int fops_capture_set(void *data, u64 val)
{
	struct gpio_la_poll_priv *priv = data;
	u8 *la_buf = priv->blob.data;
	unsigned long state = 0; /* zeroed because GPIO arrays are bitfields */
	unsigned long delay;
	ktime_t start_time;
	unsigned int i;
	int ret;

	if (!val)
		return 0;

	if (!la_buf)
		return -ENOMEM;

	if (!priv->delay_ns)
		return -EINVAL;

	mutex_lock(&priv->blob_lock);
	if (priv->blob_dent) {
		debugfs_remove(priv->blob_dent);
		priv->blob_dent = NULL;
	}

	priv->buf_idx = 0;

	local_irq_disable();
	preempt_disable_notrace();

	/* Measure delay of reading GPIOs */
	start_time = ktime_get();
	for (i = 0; i < GPIO_LA_NUM_TESTS; i++) {
		ret = gpio_la_get_array(priv->descs, &state);
		if (ret)
			goto out;
	}

	priv->acq_delay = ktime_sub(ktime_get(), start_time) / GPIO_LA_NUM_TESTS;
	if (priv->delay_ns < priv->acq_delay) {
		ret = -ERANGE;
		goto out;
	}

	delay = priv->delay_ns - priv->acq_delay;

	/* Wait for triggers */
	for (i = 0; i < priv->trig_len; i += 2) {
		do {
			ret = gpio_la_get_array(priv->descs, &state);
			if (ret)
				goto out;

			ndelay(delay);
		} while ((state & priv->trig_data[i]) != priv->trig_data[i + 1]);
	}

	/* With triggers, final state is also the first sample */
	if (priv->trig_len)
		la_buf[priv->buf_idx++] = state;

	/* Sample */
	while (priv->buf_idx < priv->blob.size) {
		ret = gpio_la_get_array(priv->descs, &state);
		if (ret)
			goto out;

		la_buf[priv->buf_idx++] = state;
		ndelay(delay);
	}
out:
	preempt_enable_notrace();
	local_irq_enable();
	if (ret)
		dev_err(priv->dev, "couldn't read GPIOs: %d\n", ret);

	kfree(priv->trig_data);
	priv->trig_data = NULL;
	priv->trig_len = 0;

	priv->blob_dent = debugfs_create_blob("sample_data", 0400, priv->debug_dir, &priv->blob);
	mutex_unlock(&priv->blob_lock);

	return ret;
}
DEFINE_DEBUGFS_ATTRIBUTE(fops_capture, NULL, fops_capture_set, "%llu\n");

static int fops_buf_size_get(void *data, u64 *val)
{
	struct gpio_la_poll_priv *priv = data;

	*val = priv->blob.size;

	return 0;
}

static int fops_buf_size_set(void *data, u64 val)
{
	struct gpio_la_poll_priv *priv = data;
	int ret = 0;
	void *p;

	if (!val)
		return -EINVAL;

	mutex_lock(&priv->blob_lock);

	vfree(priv->blob.data);
	p = vzalloc(val);
	if (!p) {
		val = 0;
		ret = -ENOMEM;
	}

	priv->blob.data = p;
	priv->blob.size = val;

	mutex_unlock(&priv->blob_lock);
	return ret;
}
DEFINE_DEBUGFS_ATTRIBUTE(fops_buf_size, fops_buf_size_get, fops_buf_size_set, "%llu\n");

static int trigger_open(struct inode *inode, struct file *file)
{
	return single_open(file, NULL, inode->i_private);
}

static ssize_t trigger_write(struct file *file, const char __user *ubuf,
			     size_t count, loff_t *offset)
{
	struct seq_file *m = file->private_data;
	struct gpio_la_poll_priv *priv = m->private;
	char *buf;

	/* upper limit is arbitrary but should be less than PAGE_SIZE */
	if (count > 2048 || count & 1)
		return -EINVAL;

	buf = memdup_user(ubuf, count);
	if (IS_ERR(buf))
		return PTR_ERR(buf);

	priv->trig_data = buf;
	priv->trig_len = count;

	return count;
}

static const struct file_operations fops_trigger = {
	.owner = THIS_MODULE,
	.open = trigger_open,
	.write = trigger_write,
	.release = single_release,
};

static int gpio_la_poll_probe(struct platform_device *pdev)
{
	struct gpio_la_poll_priv *priv;
	struct device *dev = &pdev->dev;
	const char *devname = dev_name(dev);
	const char *gpio_names[GPIO_LA_MAX_PROBES];
	char *meta = NULL;
	unsigned int i, meta_len = 0;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	ret = devm_mutex_init(dev, &priv->blob_lock);
	if (ret)
		return ret;

	fops_buf_size_set(priv, GPIO_LA_DEFAULT_BUF_SIZE);

	priv->descs = devm_gpiod_get_array(dev, "probe", GPIOD_IN);
	if (IS_ERR(priv->descs))
		return PTR_ERR(priv->descs);

	/* artificial limit to keep 1 byte per sample for now */
	if (priv->descs->ndescs > GPIO_LA_MAX_PROBES)
		return -EFBIG;

	ret = device_property_read_string_array(dev, "probe-names", gpio_names,
						priv->descs->ndescs);
	if (ret >= 0 && ret != priv->descs->ndescs)
		ret = -EBADR;
	if (ret < 0)
		return dev_err_probe(dev, ret, "error naming the GPIOs");

	for (i = 0; i < priv->descs->ndescs; i++) {
		unsigned int add_len;
		char *new_meta, *consumer_name;

		if (gpiod_cansleep(priv->descs->desc[i]))
			return -EREMOTE;

		consumer_name = kasprintf(GFP_KERNEL, "%s: %s", devname, gpio_names[i]);
		if (!consumer_name)
			return -ENOMEM;
		gpiod_set_consumer_name(priv->descs->desc[i], consumer_name);
		kfree(consumer_name);

		/* '10' is length of 'probe00=\n\0' */
		add_len = strlen(gpio_names[i]) + 10;

		new_meta = devm_krealloc(dev, meta, meta_len + add_len, GFP_KERNEL);
		if (!new_meta)
			return -ENOMEM;

		meta = new_meta;
		meta_len += snprintf(meta + meta_len, add_len, "probe%02u=%s\n",
				     i + 1, gpio_names[i]);
	}

	platform_set_drvdata(pdev, priv);
	priv->dev = dev;

	priv->meta.data = meta;
	priv->meta.size = meta_len;
	priv->debug_dir = debugfs_create_dir(devname, gpio_la_poll_debug_dir);
	debugfs_create_blob("meta_data", 0400, priv->debug_dir, &priv->meta);
	debugfs_create_ulong("delay_ns", 0600, priv->debug_dir, &priv->delay_ns);
	debugfs_create_ulong("delay_ns_acquisition", 0400, priv->debug_dir, &priv->acq_delay);
	debugfs_create_file_unsafe("buf_size", 0600, priv->debug_dir, priv, &fops_buf_size);
	debugfs_create_file_unsafe("capture", 0200, priv->debug_dir, priv, &fops_capture);
	debugfs_create_file_unsafe("trigger", 0200, priv->debug_dir, priv, &fops_trigger);

	return 0;
}

static void gpio_la_poll_remove(struct platform_device *pdev)
{
	struct gpio_la_poll_priv *priv = platform_get_drvdata(pdev);

	mutex_lock(&priv->blob_lock);
	debugfs_remove_recursive(priv->debug_dir);
	mutex_unlock(&priv->blob_lock);
}

static const struct of_device_id gpio_la_poll_of_match[] = {
	{ .compatible = GPIO_LA_NAME },
	{ }
};
MODULE_DEVICE_TABLE(of, gpio_la_poll_of_match);

static struct platform_driver gpio_la_poll_device_driver = {
	.probe = gpio_la_poll_probe,
	.remove_new = gpio_la_poll_remove,
	.driver = {
		.name = GPIO_LA_NAME,
		.of_match_table = gpio_la_poll_of_match,
	}
};

static int __init gpio_la_poll_init(void)
{
	gpio_la_poll_debug_dir = debugfs_create_dir(GPIO_LA_NAME, NULL);

	return platform_driver_register(&gpio_la_poll_device_driver);
}
/*
 * Non-strict pin controllers can read GPIOs while being muxed to something else.
 * To support that, we need to claim GPIOs before further pinmuxing happens. So,
 * we probe early using 'late_initcall'
 */
late_initcall(gpio_la_poll_init);

static void __exit gpio_la_poll_exit(void)
{
	platform_driver_unregister(&gpio_la_poll_device_driver);
	debugfs_remove_recursive(gpio_la_poll_debug_dir);
}
module_exit(gpio_la_poll_exit);

MODULE_AUTHOR("Wolfram Sang <wsa@sang-engineering.com>");
MODULE_DESCRIPTION("Sloppy logic analyzer using GPIOs");
MODULE_LICENSE("GPL");
