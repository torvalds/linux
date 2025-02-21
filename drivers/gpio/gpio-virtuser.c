// SPDX-License-Identifier: GPL-2.0-only
/*
 * Configurable virtual GPIO consumer module.
 *
 * Copyright (C) 2023-2024 Bartosz Golaszewski <bartosz.golaszewski@linaro.org>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/array_size.h>
#include <linux/atomic.h>
#include <linux/bitmap.h>
#include <linux/cleanup.h>
#include <linux/configfs.h>
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio/machine.h>
#include <linux/idr.h>
#include <linux/interrupt.h>
#include <linux/irq_work.h>
#include <linux/limits.h>
#include <linux/list.h>
#include <linux/lockdep.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/notifier.h>
#include <linux/of.h>
#include <linux/overflow.h>
#include <linux/platform_device.h>
#include <linux/printk.h>
#include <linux/property.h>
#include <linux/slab.h>
#include <linux/string_helpers.h>
#include <linux/types.h>

#include "dev-sync-probe.h"

#define GPIO_VIRTUSER_NAME_BUF_LEN 32

static DEFINE_IDA(gpio_virtuser_ida);
static struct dentry *gpio_virtuser_dbg_root;

struct gpio_virtuser_attr_data {
	union {
		struct gpio_desc *desc;
		struct gpio_descs *descs;
	};
	struct dentry *dbgfs_dir;
};

struct gpio_virtuser_line_array_data {
	struct gpio_virtuser_attr_data ad;
};

struct gpio_virtuser_line_data {
	struct gpio_virtuser_attr_data ad;
	char consumer[GPIO_VIRTUSER_NAME_BUF_LEN];
	struct mutex consumer_lock;
	unsigned int debounce;
	atomic_t irq;
	atomic_t irq_count;
};

struct gpio_virtuser_dbgfs_attr_descr {
	const char *name;
	const struct file_operations *fops;
};

struct gpio_virtuser_irq_work_context {
	struct irq_work work;
	struct completion work_completion;
	union {
		struct {
			struct gpio_desc *desc;
			int dir;
			int val;
			int ret;
		};
		struct {
			struct gpio_descs *descs;
			unsigned long *values;
		};
	};
};

static struct gpio_virtuser_irq_work_context *
to_gpio_virtuser_irq_work_context(struct irq_work *work)
{
	return container_of(work, struct gpio_virtuser_irq_work_context, work);
}

static void
gpio_virtuser_init_irq_work_context(struct gpio_virtuser_irq_work_context *ctx)
{
	memset(ctx, 0, sizeof(*ctx));
	init_completion(&ctx->work_completion);
}

static void
gpio_virtuser_irq_work_queue_sync(struct gpio_virtuser_irq_work_context *ctx)
{
	irq_work_queue(&ctx->work);
	wait_for_completion(&ctx->work_completion);
}

static void gpio_virtuser_dbgfs_emit_value_array(char *buf,
						 unsigned long *values,
						 size_t num_values)
{
	size_t i;

	for (i = 0; i < num_values; i++)
		buf[i] = test_bit(i, values) ? '1' : '0';

	buf[i++] = '\n';
}

static void gpio_virtuser_get_value_array_atomic(struct irq_work *work)
{
	struct gpio_virtuser_irq_work_context *ctx =
				to_gpio_virtuser_irq_work_context(work);
	struct gpio_descs *descs = ctx->descs;

	ctx->ret = gpiod_get_array_value(descs->ndescs, descs->desc,
					 descs->info, ctx->values);
	complete(&ctx->work_completion);
}

static int gpio_virtuser_get_array_value(struct gpio_descs *descs,
					 unsigned long *values, bool atomic)
{
	struct gpio_virtuser_irq_work_context ctx;

	if (!atomic)
		return gpiod_get_array_value_cansleep(descs->ndescs,
						      descs->desc,
						      descs->info, values);

	gpio_virtuser_init_irq_work_context(&ctx);
	ctx.work = IRQ_WORK_INIT_HARD(gpio_virtuser_get_value_array_atomic);
	ctx.descs = descs;
	ctx.values = values;

	gpio_virtuser_irq_work_queue_sync(&ctx);

	return ctx.ret;
}

static ssize_t gpio_virtuser_value_array_do_read(struct file *file,
						 char __user *user_buf,
						 size_t size, loff_t *ppos,
						 bool atomic)
{
	struct gpio_virtuser_line_data *data = file->private_data;
	struct gpio_descs *descs = data->ad.descs;
	size_t bufsize;
	int ret;

	unsigned long *values __free(bitmap) = bitmap_zalloc(descs->ndescs,
							     GFP_KERNEL);
	if (!values)
		return -ENOMEM;

	ret = gpio_virtuser_get_array_value(descs, values, atomic);
	if (ret)
		return ret;

	bufsize = descs->ndescs + 2;

	char *buf __free(kfree) = kzalloc(bufsize, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	gpio_virtuser_dbgfs_emit_value_array(buf, values, descs->ndescs);

	return simple_read_from_buffer(user_buf, size, ppos, buf,
				       descs->ndescs + 1);
}

static int gpio_virtuser_dbgfs_parse_value_array(const char *buf,
						 size_t len,
						 unsigned long *values)
{
	size_t i;

	for (i = 0; i < len; i++) {
		if (buf[i] == '0')
			clear_bit(i, values);
		else if (buf[i] == '1')
			set_bit(i, values);
		else
			return -EINVAL;
	}

	return 0;
}

static void gpio_virtuser_set_value_array_atomic(struct irq_work *work)
{
	struct gpio_virtuser_irq_work_context *ctx =
				to_gpio_virtuser_irq_work_context(work);
	struct gpio_descs *descs = ctx->descs;

	ctx->ret = gpiod_set_array_value(descs->ndescs, descs->desc,
					 descs->info, ctx->values);
	complete(&ctx->work_completion);
}

static int gpio_virtuser_set_array_value(struct gpio_descs *descs,
					 unsigned long *values, bool atomic)
{
	struct gpio_virtuser_irq_work_context ctx;

	if (!atomic)
		return gpiod_set_array_value_cansleep(descs->ndescs,
						      descs->desc,
						      descs->info, values);

	gpio_virtuser_init_irq_work_context(&ctx);
	ctx.work = IRQ_WORK_INIT_HARD(gpio_virtuser_set_value_array_atomic);
	ctx.descs = descs;
	ctx.values = values;

	gpio_virtuser_irq_work_queue_sync(&ctx);

	return ctx.ret;
}

static ssize_t gpio_virtuser_value_array_do_write(struct file *file,
						  const char __user *user_buf,
						  size_t count, loff_t *ppos,
						  bool atomic)
{
	struct gpio_virtuser_line_data *data = file->private_data;
	struct gpio_descs *descs = data->ad.descs;
	int ret;

	if (count - 1 != descs->ndescs)
		return -EINVAL;

	char *buf __free(kfree) = kzalloc(count, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	ret = simple_write_to_buffer(buf, count, ppos, user_buf, count);
	if (ret < 0)
		return ret;

	unsigned long *values __free(bitmap) = bitmap_zalloc(descs->ndescs,
							     GFP_KERNEL);
	if (!values)
		return -ENOMEM;

	ret = gpio_virtuser_dbgfs_parse_value_array(buf, count - 1, values);
	if (ret)
		return ret;

	ret = gpio_virtuser_set_array_value(descs, values, atomic);
	if (ret)
		return ret;

	return count;
}

static ssize_t gpio_virtuser_value_array_read(struct file *file,
					      char __user *user_buf,
					      size_t count, loff_t *ppos)
{
	return gpio_virtuser_value_array_do_read(file, user_buf, count, ppos,
						 false);
}

static ssize_t gpio_virtuser_value_array_write(struct file *file,
					       const char __user *user_buf,
					       size_t count, loff_t *ppos)
{
	return gpio_virtuser_value_array_do_write(file, user_buf, count, ppos,
						  false);
}

static const struct file_operations gpio_virtuser_value_array_fops = {
	.read = gpio_virtuser_value_array_read,
	.write = gpio_virtuser_value_array_write,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static ssize_t
gpio_virtuser_value_array_atomic_read(struct file *file, char __user *user_buf,
				      size_t count, loff_t *ppos)
{
	return gpio_virtuser_value_array_do_read(file, user_buf, count, ppos,
						 true);
}

static ssize_t
gpio_virtuser_value_array_atomic_write(struct file *file,
				       const char __user *user_buf,
				       size_t count, loff_t *ppos)
{
	return gpio_virtuser_value_array_do_write(file, user_buf, count, ppos,
						  true);
}

static const struct file_operations gpio_virtuser_value_array_atomic_fops = {
	.read = gpio_virtuser_value_array_atomic_read,
	.write = gpio_virtuser_value_array_atomic_write,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static void gpio_virtuser_do_get_direction_atomic(struct irq_work *work)
{
	struct gpio_virtuser_irq_work_context *ctx =
				to_gpio_virtuser_irq_work_context(work);

	ctx->ret = gpiod_get_direction(ctx->desc);
	complete(&ctx->work_completion);
}

static int gpio_virtuser_get_direction_atomic(struct gpio_desc *desc)
{
	struct gpio_virtuser_irq_work_context ctx;

	gpio_virtuser_init_irq_work_context(&ctx);
	ctx.work = IRQ_WORK_INIT_HARD(gpio_virtuser_do_get_direction_atomic);
	ctx.desc = desc;

	gpio_virtuser_irq_work_queue_sync(&ctx);

	return ctx.ret;
}

static ssize_t gpio_virtuser_direction_do_read(struct file *file,
					       char __user *user_buf,
					       size_t size, loff_t *ppos,
					       bool atomic)
{
	struct gpio_virtuser_line_data *data = file->private_data;
	struct gpio_desc *desc = data->ad.desc;
	char buf[32];
	int dir;

	if (!atomic)
		dir = gpiod_get_direction(desc);
	else
		dir = gpio_virtuser_get_direction_atomic(desc);
	if (dir < 0)
		return dir;

	snprintf(buf, sizeof(buf), "%s\n", dir ? "input" : "output");

	return simple_read_from_buffer(user_buf, size, ppos, buf, strlen(buf));
}

static int gpio_virtuser_set_direction(struct gpio_desc *desc, int dir, int val)
{
	if (dir)
		return gpiod_direction_input(desc);

	return gpiod_direction_output(desc, val);
}

static void gpio_virtuser_do_set_direction_atomic(struct irq_work *work)
{
	struct gpio_virtuser_irq_work_context *ctx =
				to_gpio_virtuser_irq_work_context(work);

	ctx->ret = gpio_virtuser_set_direction(ctx->desc, ctx->dir, ctx->val);
	complete(&ctx->work_completion);
}

static int gpio_virtuser_set_direction_atomic(struct gpio_desc *desc,
					      int dir, int val)
{
	struct gpio_virtuser_irq_work_context ctx;

	gpio_virtuser_init_irq_work_context(&ctx);
	ctx.work = IRQ_WORK_INIT_HARD(gpio_virtuser_do_set_direction_atomic);
	ctx.desc = desc;
	ctx.dir = dir;
	ctx.val = val;

	gpio_virtuser_irq_work_queue_sync(&ctx);

	return ctx.ret;
}

static ssize_t gpio_virtuser_direction_do_write(struct file *file,
						const char __user *user_buf,
						size_t count, loff_t *ppos,
						bool atomic)
{
	struct gpio_virtuser_line_data *data = file->private_data;
	struct gpio_desc *desc = data->ad.desc;
	char buf[32], *trimmed;
	int ret, dir, val = 0;

	ret = simple_write_to_buffer(buf, sizeof(buf), ppos, user_buf, count);
	if (ret < 0)
		return ret;

	trimmed = strim(buf);

	if (strcmp(trimmed, "input") == 0) {
		dir = 1;
	} else if (strcmp(trimmed, "output-high") == 0) {
		dir = 0;
		val = 1;
	} else if (strcmp(trimmed, "output-low") == 0) {
		dir = val = 0;
	} else {
		return -EINVAL;
	}

	if (!atomic)
		ret = gpio_virtuser_set_direction(desc, dir, val);
	else
		ret = gpio_virtuser_set_direction_atomic(desc, dir, val);
	if (ret)
		return ret;

	return count;
}

static ssize_t gpio_virtuser_direction_read(struct file *file,
					    char __user *user_buf,
					    size_t size, loff_t *ppos)
{
	return gpio_virtuser_direction_do_read(file, user_buf, size, ppos,
					       false);
}

static ssize_t gpio_virtuser_direction_write(struct file *file,
					     const char __user *user_buf,
					     size_t count, loff_t *ppos)
{
	return gpio_virtuser_direction_do_write(file, user_buf, count, ppos,
						false);
}

static const struct file_operations gpio_virtuser_direction_fops = {
	.read = gpio_virtuser_direction_read,
	.write = gpio_virtuser_direction_write,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static ssize_t gpio_virtuser_direction_atomic_read(struct file *file,
						   char __user *user_buf,
						   size_t size, loff_t *ppos)
{
	return gpio_virtuser_direction_do_read(file, user_buf, size, ppos,
					       true);
}

static ssize_t gpio_virtuser_direction_atomic_write(struct file *file,
						    const char __user *user_buf,
						    size_t count, loff_t *ppos)
{
	return gpio_virtuser_direction_do_write(file, user_buf, count, ppos,
						true);
}

static const struct file_operations gpio_virtuser_direction_atomic_fops = {
	.read = gpio_virtuser_direction_atomic_read,
	.write = gpio_virtuser_direction_atomic_write,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static int gpio_virtuser_value_get(void *data, u64 *val)
{
	struct gpio_virtuser_line_data *ld = data;
	int ret;

	ret = gpiod_get_value_cansleep(ld->ad.desc);
	if (ret < 0)
		return ret;

	*val = ret;

	return 0;
}

static int gpio_virtuser_value_set(void *data, u64 val)
{
	struct gpio_virtuser_line_data *ld = data;

	if (val > 1)
		return -EINVAL;

	gpiod_set_value_cansleep(ld->ad.desc, (int)val);

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(gpio_virtuser_value_fops,
			 gpio_virtuser_value_get,
			 gpio_virtuser_value_set,
			 "%llu\n");

static void gpio_virtuser_get_value_atomic(struct irq_work *work)
{
	struct gpio_virtuser_irq_work_context *ctx =
				to_gpio_virtuser_irq_work_context(work);

	ctx->val = gpiod_get_value(ctx->desc);
	complete(&ctx->work_completion);
}

static int gpio_virtuser_value_atomic_get(void *data, u64 *val)
{
	struct gpio_virtuser_line_data *ld = data;
	struct gpio_virtuser_irq_work_context ctx;

	gpio_virtuser_init_irq_work_context(&ctx);
	ctx.work = IRQ_WORK_INIT_HARD(gpio_virtuser_get_value_atomic);
	ctx.desc = ld->ad.desc;

	gpio_virtuser_irq_work_queue_sync(&ctx);

	if (ctx.val < 0)
		return ctx.val;

	*val = ctx.val;

	return 0;
}

static void gpio_virtuser_set_value_atomic(struct irq_work *work)
{
	struct gpio_virtuser_irq_work_context *ctx =
			to_gpio_virtuser_irq_work_context(work);

	gpiod_set_value(ctx->desc, ctx->val);
	complete(&ctx->work_completion);
}

static int gpio_virtuser_value_atomic_set(void *data, u64 val)
{
	struct gpio_virtuser_line_data *ld = data;
	struct gpio_virtuser_irq_work_context ctx;

	if (val > 1)
		return -EINVAL;

	gpio_virtuser_init_irq_work_context(&ctx);
	ctx.work = IRQ_WORK_INIT_HARD(gpio_virtuser_set_value_atomic);
	ctx.desc = ld->ad.desc;
	ctx.val = (int)val;

	gpio_virtuser_irq_work_queue_sync(&ctx);

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(gpio_virtuser_value_atomic_fops,
			 gpio_virtuser_value_atomic_get,
			 gpio_virtuser_value_atomic_set,
			 "%llu\n");

static int gpio_virtuser_debounce_get(void *data, u64 *val)
{
	struct gpio_virtuser_line_data *ld = data;

	*val = READ_ONCE(ld->debounce);

	return 0;
}

static int gpio_virtuser_debounce_set(void *data, u64 val)
{
	struct gpio_virtuser_line_data *ld = data;
	int ret;

	if (val > UINT_MAX)
		return -E2BIG;

	ret = gpiod_set_debounce(ld->ad.desc, (unsigned int)val);
	if (ret)
		/* Don't propagate errno unknown to user-space. */
		return ret == -ENOTSUPP ? -EOPNOTSUPP : ret;

	WRITE_ONCE(ld->debounce, (unsigned int)val);

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(gpio_virtuser_debounce_fops,
			 gpio_virtuser_debounce_get,
			 gpio_virtuser_debounce_set,
			 "%llu\n");

static ssize_t gpio_virtuser_consumer_read(struct file *file,
					   char __user *user_buf,
					   size_t size, loff_t *ppos)
{
	struct gpio_virtuser_line_data *data = file->private_data;
	char buf[GPIO_VIRTUSER_NAME_BUF_LEN + 1];
	ssize_t ret;

	memset(buf, 0x0, sizeof(buf));

	scoped_guard(mutex, &data->consumer_lock)
		ret = snprintf(buf, sizeof(buf), "%s\n", data->consumer);

	return simple_read_from_buffer(user_buf, size, ppos, buf, ret);
}

static ssize_t gpio_virtuser_consumer_write(struct file *file,
					    const char __user *user_buf,
					    size_t count, loff_t *ppos)
{
	struct gpio_virtuser_line_data *data = file->private_data;
	char buf[GPIO_VIRTUSER_NAME_BUF_LEN + 2];
	int ret;

	ret = simple_write_to_buffer(buf, GPIO_VIRTUSER_NAME_BUF_LEN, ppos,
				     user_buf, count);
	if (ret < 0)
		return ret;

	buf[strlen(buf) - 1] = '\0';

	ret = gpiod_set_consumer_name(data->ad.desc, buf);
	if (ret)
		return ret;

	scoped_guard(mutex, &data->consumer_lock)
		strscpy(data->consumer, buf, sizeof(data->consumer));

	return count;
}

static const struct file_operations gpio_virtuser_consumer_fops = {
	.read = gpio_virtuser_consumer_read,
	.write = gpio_virtuser_consumer_write,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static int gpio_virtuser_interrupts_get(void *data, u64 *val)
{
	struct gpio_virtuser_line_data *ld = data;

	*val = atomic_read(&ld->irq_count);

	return 0;
}

static irqreturn_t gpio_virtuser_irq_handler(int irq, void *data)
{
	struct gpio_virtuser_line_data *ld = data;

	atomic_inc(&ld->irq_count);

	return IRQ_HANDLED;
}

static int gpio_virtuser_interrupts_set(void *data, u64 val)
{
	struct gpio_virtuser_line_data *ld = data;
	int irq, ret;

	if (val > 1)
		return -EINVAL;

	if (val) {
		irq = gpiod_to_irq(ld->ad.desc);
		if (irq < 0)
			return irq;

		ret = request_threaded_irq(irq, NULL,
					   gpio_virtuser_irq_handler,
					   IRQF_TRIGGER_RISING |
					   IRQF_TRIGGER_FALLING |
					   IRQF_ONESHOT,
					   ld->consumer, data);
		if (ret)
			return ret;

		atomic_set(&ld->irq, irq);
	} else {
		irq = atomic_xchg(&ld->irq, 0);
		free_irq(irq, ld);
	}

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(gpio_virtuser_interrupts_fops,
			 gpio_virtuser_interrupts_get,
			 gpio_virtuser_interrupts_set,
			 "%llu\n");

static const struct gpio_virtuser_dbgfs_attr_descr
gpio_virtuser_line_array_dbgfs_attrs[] = {
	{
		.name = "values",
		.fops = &gpio_virtuser_value_array_fops,
	},
	{
		.name = "values_atomic",
		.fops = &gpio_virtuser_value_array_atomic_fops,
	},
};

static const struct gpio_virtuser_dbgfs_attr_descr
gpio_virtuser_line_dbgfs_attrs[] = {
	{
		.name = "direction",
		.fops = &gpio_virtuser_direction_fops,
	},
	{
		.name = "direction_atomic",
		.fops = &gpio_virtuser_direction_atomic_fops,
	},
	{
		.name = "value",
		.fops = &gpio_virtuser_value_fops,
	},
	{
		.name = "value_atomic",
		.fops = &gpio_virtuser_value_atomic_fops,
	},
	{
		.name = "debounce",
		.fops = &gpio_virtuser_debounce_fops,
	},
	{
		.name = "consumer",
		.fops = &gpio_virtuser_consumer_fops,
	},
	{
		.name = "interrupts",
		.fops = &gpio_virtuser_interrupts_fops,
	},
};

static int gpio_virtuser_create_debugfs_attrs(
			const struct gpio_virtuser_dbgfs_attr_descr *attr,
			size_t num_attrs, struct dentry *parent, void *data)
{
	struct dentry *ret;
	size_t i;

	for (i = 0; i < num_attrs; i++, attr++) {
		ret = debugfs_create_file(attr->name, 0644, parent, data,
					  attr->fops);
		if (IS_ERR(ret))
			return PTR_ERR(ret);
	}

	return 0;
}

static int gpio_virtuser_dbgfs_init_line_array_attrs(struct device *dev,
						     struct gpio_descs *descs,
						     const char *id,
						     struct dentry *dbgfs_entry)
{
	struct gpio_virtuser_line_array_data *data;
	char *name;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->ad.descs = descs;

	name = devm_kasprintf(dev, GFP_KERNEL, "gpiod:%s", id);
	if (!name)
		return -ENOMEM;

	data->ad.dbgfs_dir = debugfs_create_dir(name, dbgfs_entry);
	if (IS_ERR(data->ad.dbgfs_dir))
		return PTR_ERR(data->ad.dbgfs_dir);

	return gpio_virtuser_create_debugfs_attrs(
			gpio_virtuser_line_array_dbgfs_attrs,
			ARRAY_SIZE(gpio_virtuser_line_array_dbgfs_attrs),
			data->ad.dbgfs_dir, data);
}

static int gpio_virtuser_dbgfs_init_line_attrs(struct device *dev,
					       struct gpio_desc *desc,
					       const char *id,
					       unsigned int index,
					       struct dentry *dbgfs_entry)
{
	struct gpio_virtuser_line_data *data;
	char *name;
	int ret;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->ad.desc = desc;
	strscpy(data->consumer, id);
	atomic_set(&data->irq, 0);
	atomic_set(&data->irq_count, 0);

	name = devm_kasprintf(dev, GFP_KERNEL, "gpiod:%s:%u", id, index);
	if (!name)
		return -ENOMEM;

	ret = devm_mutex_init(dev, &data->consumer_lock);
	if (ret)
		return ret;

	data->ad.dbgfs_dir = debugfs_create_dir(name, dbgfs_entry);
	if (IS_ERR(data->ad.dbgfs_dir))
		return PTR_ERR(data->ad.dbgfs_dir);

	return gpio_virtuser_create_debugfs_attrs(
				gpio_virtuser_line_dbgfs_attrs,
				ARRAY_SIZE(gpio_virtuser_line_dbgfs_attrs),
				data->ad.dbgfs_dir, data);
}

static void gpio_virtuser_debugfs_remove(void *data)
{
	struct dentry *dbgfs_entry = data;

	debugfs_remove_recursive(dbgfs_entry);
}

static int gpio_virtuser_prop_is_gpio(struct property *prop)
{
	char *dash = strrchr(prop->name, '-');

	return dash && strcmp(dash, "-gpios") == 0;
}

/*
 * If this is an OF-based system, then we iterate over properties and consider
 * all whose names end in "-gpios". For configfs we expect an additional string
 * array property - "gpio-virtuser,ids" - containing the list of all GPIO IDs
 * to request.
 */
static int gpio_virtuser_count_ids(struct device *dev)
{
	struct device_node *of_node = dev_of_node(dev);
	struct property *prop;
	int ret = 0;

	if (!of_node)
		return device_property_string_array_count(dev,
							  "gpio-virtuser,ids");

	for_each_property_of_node(of_node, prop) {
		if (gpio_virtuser_prop_is_gpio(prop))
			++ret;
	}

	return ret;
}

static int gpio_virtuser_get_ids(struct device *dev, const char **ids,
				 int num_ids)
{
	struct device_node *of_node = dev_of_node(dev);
	struct property *prop;
	size_t pos = 0, diff;
	char *dash, *tmp;

	if (!of_node)
		return device_property_read_string_array(dev,
							 "gpio-virtuser,ids",
							 ids, num_ids);

	for_each_property_of_node(of_node, prop) {
		if (!gpio_virtuser_prop_is_gpio(prop))
			continue;

		dash = strrchr(prop->name, '-');
		diff = dash - prop->name;

		tmp = devm_kmemdup(dev, prop->name, diff + 1,
				   GFP_KERNEL);
		if (!tmp)
			return -ENOMEM;

		tmp[diff] = '\0';
		ids[pos++] = tmp;
	}

	return 0;
}

static int gpio_virtuser_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct dentry *dbgfs_entry;
	struct gpio_descs *descs;
	int ret, num_ids = 0, i;
	const char **ids;
	unsigned int j;

	num_ids = gpio_virtuser_count_ids(dev);
	if (num_ids < 0)
		return dev_err_probe(dev, num_ids,
				     "Failed to get the number of GPIOs to request\n");

	if (num_ids == 0)
		return dev_err_probe(dev, -EINVAL, "No GPIO IDs specified\n");

	ids = devm_kcalloc(dev, num_ids, sizeof(*ids), GFP_KERNEL);
	if (!ids)
		return -ENOMEM;

	ret = gpio_virtuser_get_ids(dev, ids, num_ids);
	if (ret < 0)
		return dev_err_probe(dev, ret,
				     "Failed to get the IDs of GPIOs to request\n");

	dbgfs_entry = debugfs_create_dir(dev_name(dev), gpio_virtuser_dbg_root);
	ret = devm_add_action_or_reset(dev, gpio_virtuser_debugfs_remove,
				       dbgfs_entry);
	if (ret)
		return ret;

	for (i = 0; i < num_ids; i++) {
		descs = devm_gpiod_get_array(dev, ids[i], GPIOD_ASIS);
		if (IS_ERR(descs))
			return dev_err_probe(dev, PTR_ERR(descs),
					     "Failed to request the '%s' GPIOs\n",
					     ids[i]);

		ret = gpio_virtuser_dbgfs_init_line_array_attrs(dev, descs,
								ids[i],
								dbgfs_entry);
		if (ret)
			return dev_err_probe(dev, ret,
					     "Failed to setup the debugfs array interface for the '%s' GPIOs\n",
					     ids[i]);

		for (j = 0; j < descs->ndescs; j++) {
			ret = gpio_virtuser_dbgfs_init_line_attrs(dev,
							descs->desc[j], ids[i],
							j, dbgfs_entry);
			if (ret)
				return dev_err_probe(dev, ret,
						     "Failed to setup the debugfs line interface for the '%s' GPIOs\n",
						     ids[i]);
		}
	}

	return 0;
}

static const struct of_device_id gpio_virtuser_of_match[] = {
	{ .compatible = "gpio-virtuser" },
	{ }
};
MODULE_DEVICE_TABLE(of, gpio_virtuser_of_match);

static struct platform_driver gpio_virtuser_driver = {
	.driver = {
		.name = "gpio-virtuser",
		.of_match_table = gpio_virtuser_of_match,
	},
	.probe = gpio_virtuser_probe,
};

struct gpio_virtuser_device {
	struct dev_sync_probe_data probe_data;
	struct config_group group;

	int id;
	struct mutex lock;

	struct gpiod_lookup_table *lookup_table;

	struct list_head lookup_list;
};

static struct gpio_virtuser_device *
to_gpio_virtuser_device(struct config_item *item)
{
	struct config_group *group = to_config_group(item);

	return container_of(group, struct gpio_virtuser_device, group);
}

static bool
gpio_virtuser_device_is_live(struct gpio_virtuser_device *dev)
{
	lockdep_assert_held(&dev->lock);

	return !!dev->probe_data.pdev;
}

struct gpio_virtuser_lookup {
	struct config_group group;

	struct gpio_virtuser_device *parent;
	struct list_head siblings;

	char *con_id;

	struct list_head entry_list;
};

static struct gpio_virtuser_lookup *
to_gpio_virtuser_lookup(struct config_item *item)
{
	struct config_group *group = to_config_group(item);

	return container_of(group, struct gpio_virtuser_lookup, group);
}

struct gpio_virtuser_lookup_entry {
	struct config_group group;

	struct gpio_virtuser_lookup *parent;
	struct list_head siblings;

	char *key;
	/* Can be negative to indicate lookup by name. */
	int offset;
	enum gpio_lookup_flags flags;
};

static struct gpio_virtuser_lookup_entry *
to_gpio_virtuser_lookup_entry(struct config_item *item)
{
	struct config_group *group = to_config_group(item);

	return container_of(group, struct gpio_virtuser_lookup_entry, group);
}

static ssize_t
gpio_virtuser_lookup_entry_config_key_show(struct config_item *item, char *page)
{
	struct gpio_virtuser_lookup_entry *entry =
					to_gpio_virtuser_lookup_entry(item);
	struct gpio_virtuser_device *dev = entry->parent->parent;

	guard(mutex)(&dev->lock);

	return sprintf(page, "%s\n", entry->key ?: "");
}

static ssize_t
gpio_virtuser_lookup_entry_config_key_store(struct config_item *item,
					    const char *page, size_t count)
{
	struct gpio_virtuser_lookup_entry *entry =
					to_gpio_virtuser_lookup_entry(item);
	struct gpio_virtuser_device *dev = entry->parent->parent;

	char *key __free(kfree) = kstrndup(skip_spaces(page), count,
					   GFP_KERNEL);
	if (!key)
		return -ENOMEM;

	strim(key);

	guard(mutex)(&dev->lock);

	if (gpio_virtuser_device_is_live(dev))
		return -EBUSY;

	kfree(entry->key);
	entry->key = no_free_ptr(key);

	return count;
}

CONFIGFS_ATTR(gpio_virtuser_lookup_entry_config_, key);

static ssize_t
gpio_virtuser_lookup_entry_config_offset_show(struct config_item *item,
					      char *page)
{
	struct gpio_virtuser_lookup_entry *entry =
					to_gpio_virtuser_lookup_entry(item);
	struct gpio_virtuser_device *dev = entry->parent->parent;
	unsigned int offset;

	scoped_guard(mutex, &dev->lock)
		offset = entry->offset;

	return sprintf(page, "%d\n", offset);
}

static ssize_t
gpio_virtuser_lookup_entry_config_offset_store(struct config_item *item,
					       const char *page, size_t count)
{
	struct gpio_virtuser_lookup_entry *entry =
					to_gpio_virtuser_lookup_entry(item);
	struct gpio_virtuser_device *dev = entry->parent->parent;
	int offset, ret;

	ret = kstrtoint(page, 0, &offset);
	if (ret)
		return ret;

	/*
	 * Negative number here means: 'key' represents a line name to lookup.
	 * Non-negative means: 'key' represents the label of the chip with
	 * the 'offset' value representing the line within that chip.
	 *
	 * GPIOLIB uses the U16_MAX value to indicate lookup by line name so
	 * the greatest offset we can accept is (U16_MAX - 1).
	 */
	if (offset > (U16_MAX - 1))
		return -EINVAL;

	guard(mutex)(&dev->lock);

	if (gpio_virtuser_device_is_live(dev))
		return -EBUSY;

	entry->offset = offset;

	return count;
}

CONFIGFS_ATTR(gpio_virtuser_lookup_entry_config_, offset);

static enum gpio_lookup_flags
gpio_virtuser_lookup_get_flags(struct config_item *item)
{
	struct gpio_virtuser_lookup_entry *entry =
					to_gpio_virtuser_lookup_entry(item);
	struct gpio_virtuser_device *dev = entry->parent->parent;

	guard(mutex)(&dev->lock);

	return entry->flags;
}

static ssize_t
gpio_virtuser_lookup_entry_config_drive_show(struct config_item *item, char *page)
{
	enum gpio_lookup_flags flags = gpio_virtuser_lookup_get_flags(item);
	const char *repr;

	if (flags & GPIO_OPEN_DRAIN)
		repr = "open-drain";
	else if (flags & GPIO_OPEN_SOURCE)
		repr = "open-source";
	else
		repr = "push-pull";

	return sprintf(page, "%s\n", repr);
}

static ssize_t
gpio_virtuser_lookup_entry_config_drive_store(struct config_item *item,
					      const char *page, size_t count)
{
	struct gpio_virtuser_lookup_entry *entry =
					to_gpio_virtuser_lookup_entry(item);
	struct gpio_virtuser_device *dev = entry->parent->parent;

	guard(mutex)(&dev->lock);

	if (gpio_virtuser_device_is_live(dev))
		return -EBUSY;

	if (sysfs_streq(page, "push-pull")) {
		entry->flags &= ~(GPIO_OPEN_DRAIN | GPIO_OPEN_SOURCE);
	} else if (sysfs_streq(page, "open-drain")) {
		entry->flags &= ~GPIO_OPEN_SOURCE;
		entry->flags |= GPIO_OPEN_DRAIN;
	} else if (sysfs_streq(page, "open-source")) {
		entry->flags &= ~GPIO_OPEN_DRAIN;
		entry->flags |= GPIO_OPEN_SOURCE;
	} else {
		count = -EINVAL;
	}

	return count;
}

CONFIGFS_ATTR(gpio_virtuser_lookup_entry_config_, drive);

static ssize_t
gpio_virtuser_lookup_entry_config_pull_show(struct config_item *item, char *page)
{
	enum gpio_lookup_flags flags = gpio_virtuser_lookup_get_flags(item);
	const char *repr;

	if (flags & GPIO_PULL_UP)
		repr = "pull-up";
	else if (flags & GPIO_PULL_DOWN)
		repr = "pull-down";
	else if (flags & GPIO_PULL_DISABLE)
		repr = "pull-disabled";
	else
		repr = "as-is";

	return sprintf(page, "%s\n", repr);
}

static ssize_t
gpio_virtuser_lookup_entry_config_pull_store(struct config_item *item,
					     const char *page, size_t count)
{
	struct gpio_virtuser_lookup_entry *entry =
					to_gpio_virtuser_lookup_entry(item);
	struct gpio_virtuser_device *dev = entry->parent->parent;

	guard(mutex)(&dev->lock);

	if (gpio_virtuser_device_is_live(dev))
		return -EBUSY;

	if (sysfs_streq(page, "pull-up")) {
		entry->flags &= ~(GPIO_PULL_DOWN | GPIO_PULL_DISABLE);
		entry->flags |= GPIO_PULL_UP;
	} else if (sysfs_streq(page, "pull-down")) {
		entry->flags &= ~(GPIO_PULL_UP | GPIO_PULL_DISABLE);
		entry->flags |= GPIO_PULL_DOWN;
	} else if (sysfs_streq(page, "pull-disabled")) {
		entry->flags &= ~(GPIO_PULL_UP | GPIO_PULL_DOWN);
		entry->flags |= GPIO_PULL_DISABLE;
	} else if (sysfs_streq(page, "as-is")) {
		entry->flags &= ~(GPIO_PULL_UP | GPIO_PULL_DOWN |
				  GPIO_PULL_DISABLE);
	} else {
		count = -EINVAL;
	}

	return count;
}

CONFIGFS_ATTR(gpio_virtuser_lookup_entry_config_, pull);

static ssize_t
gpio_virtuser_lookup_entry_config_active_low_show(struct config_item *item,
						  char *page)
{
	enum gpio_lookup_flags flags = gpio_virtuser_lookup_get_flags(item);

	return sprintf(page, "%c\n", flags & GPIO_ACTIVE_LOW ? '1' : '0');
}

static ssize_t
gpio_virtuser_lookup_entry_config_active_low_store(struct config_item *item,
						   const char *page,
						   size_t count)
{
	struct gpio_virtuser_lookup_entry *entry =
					to_gpio_virtuser_lookup_entry(item);
	struct gpio_virtuser_device *dev = entry->parent->parent;
	bool active_low;
	int ret;

	ret = kstrtobool(page, &active_low);
	if (ret)
		return ret;

	guard(mutex)(&dev->lock);

	if (gpio_virtuser_device_is_live(dev))
		return -EBUSY;

	if (active_low)
		entry->flags |= GPIO_ACTIVE_LOW;
	else
		entry->flags &= ~GPIO_ACTIVE_LOW;

	return count;
}

CONFIGFS_ATTR(gpio_virtuser_lookup_entry_config_, active_low);

static ssize_t
gpio_virtuser_lookup_entry_config_transitory_show(struct config_item *item,
						  char *page)
{
	enum gpio_lookup_flags flags = gpio_virtuser_lookup_get_flags(item);

	return sprintf(page, "%c\n", flags & GPIO_TRANSITORY ? '1' : '0');
}

static ssize_t
gpio_virtuser_lookup_entry_config_transitory_store(struct config_item *item,
						   const char *page,
						   size_t count)
{
	struct gpio_virtuser_lookup_entry *entry =
					to_gpio_virtuser_lookup_entry(item);
	struct gpio_virtuser_device *dev = entry->parent->parent;
	bool transitory;
	int ret;

	ret = kstrtobool(page, &transitory);
	if (ret)
		return ret;

	guard(mutex)(&dev->lock);

	if (gpio_virtuser_device_is_live(dev))
		return -EBUSY;

	if (transitory)
		entry->flags |= GPIO_TRANSITORY;
	else
		entry->flags &= ~GPIO_TRANSITORY;

	return count;
}

CONFIGFS_ATTR(gpio_virtuser_lookup_entry_config_, transitory);

static struct configfs_attribute *gpio_virtuser_lookup_entry_config_attrs[] = {
	&gpio_virtuser_lookup_entry_config_attr_key,
	&gpio_virtuser_lookup_entry_config_attr_offset,
	&gpio_virtuser_lookup_entry_config_attr_drive,
	&gpio_virtuser_lookup_entry_config_attr_pull,
	&gpio_virtuser_lookup_entry_config_attr_active_low,
	&gpio_virtuser_lookup_entry_config_attr_transitory,
	NULL
};

static ssize_t
gpio_virtuser_device_config_dev_name_show(struct config_item *item,
					  char *page)
{
	struct gpio_virtuser_device *dev = to_gpio_virtuser_device(item);
	struct platform_device *pdev;

	guard(mutex)(&dev->lock);

	pdev = dev->probe_data.pdev;
	if (pdev)
		return sprintf(page, "%s\n", dev_name(&pdev->dev));

	return sprintf(page, "gpio-sim.%d\n", dev->id);
}

CONFIGFS_ATTR_RO(gpio_virtuser_device_config_, dev_name);

static ssize_t gpio_virtuser_device_config_live_show(struct config_item *item,
						     char *page)
{
	struct gpio_virtuser_device *dev = to_gpio_virtuser_device(item);
	bool live;

	scoped_guard(mutex, &dev->lock)
		live = gpio_virtuser_device_is_live(dev);

	return sprintf(page, "%c\n", live ? '1' : '0');
}

static size_t
gpio_virtuser_get_lookup_count(struct gpio_virtuser_device *dev)
{
	struct gpio_virtuser_lookup *lookup;
	size_t count = 0;

	lockdep_assert_held(&dev->lock);

	list_for_each_entry(lookup, &dev->lookup_list, siblings)
		count += list_count_nodes(&lookup->entry_list);

	return count;
}

static int
gpio_virtuser_make_lookup_table(struct gpio_virtuser_device *dev)
{
	size_t num_entries = gpio_virtuser_get_lookup_count(dev);
	struct gpio_virtuser_lookup_entry *entry;
	struct gpio_virtuser_lookup *lookup;
	unsigned int i = 0, idx;

	lockdep_assert_held(&dev->lock);

	struct gpiod_lookup_table *table __free(kfree) =
		kzalloc(struct_size(table, table, num_entries + 1), GFP_KERNEL);
	if (!table)
		return -ENOMEM;

	table->dev_id = kasprintf(GFP_KERNEL, "gpio-virtuser.%d", dev->id);
	if (!table->dev_id)
		return -ENOMEM;

	list_for_each_entry(lookup, &dev->lookup_list, siblings) {
		idx = 0;
		list_for_each_entry(entry, &lookup->entry_list, siblings) {
			table->table[i++] =
				GPIO_LOOKUP_IDX(entry->key,
						entry->offset < 0 ? U16_MAX : entry->offset,
						lookup->con_id, idx++, entry->flags);
		}
	}

	gpiod_add_lookup_table(table);
	dev->lookup_table = no_free_ptr(table);

	return 0;
}

static void
gpio_virtuser_remove_lookup_table(struct gpio_virtuser_device *dev)
{
	gpiod_remove_lookup_table(dev->lookup_table);
	kfree(dev->lookup_table->dev_id);
	kfree(dev->lookup_table);
	dev->lookup_table = NULL;
}

static struct fwnode_handle *
gpio_virtuser_make_device_swnode(struct gpio_virtuser_device *dev)
{
	struct property_entry properties[2];
	struct gpio_virtuser_lookup *lookup;
	unsigned int i = 0;
	size_t num_ids;

	memset(properties, 0, sizeof(properties));

	num_ids = list_count_nodes(&dev->lookup_list);
	char **ids __free(kfree) = kcalloc(num_ids + 1, sizeof(*ids),
					   GFP_KERNEL);
	if (!ids)
		return ERR_PTR(-ENOMEM);

	list_for_each_entry(lookup, &dev->lookup_list, siblings)
		ids[i++] = lookup->con_id;

	properties[0] = PROPERTY_ENTRY_STRING_ARRAY_LEN("gpio-virtuser,ids",
							ids, num_ids);

	return fwnode_create_software_node(properties, NULL);
}

static int
gpio_virtuser_device_activate(struct gpio_virtuser_device *dev)
{
	struct platform_device_info pdevinfo;
	struct fwnode_handle *swnode;
	int ret;

	lockdep_assert_held(&dev->lock);

	if (list_empty(&dev->lookup_list))
		return -ENODATA;

	swnode = gpio_virtuser_make_device_swnode(dev);
	if (IS_ERR(swnode))
		return PTR_ERR(swnode);

	memset(&pdevinfo, 0, sizeof(pdevinfo));
	pdevinfo.name = "gpio-virtuser";
	pdevinfo.id = dev->id;
	pdevinfo.fwnode = swnode;

	ret = gpio_virtuser_make_lookup_table(dev);
	if (ret)
		goto err_remove_swnode;

	ret = dev_sync_probe_register(&dev->probe_data, &pdevinfo);
	if (ret)
		goto err_remove_lookup_table;

	return 0;

err_remove_lookup_table:
	gpio_virtuser_remove_lookup_table(dev);
err_remove_swnode:
	fwnode_remove_software_node(swnode);

	return ret;
}

static void
gpio_virtuser_device_deactivate(struct gpio_virtuser_device *dev)
{
	struct fwnode_handle *swnode;

	lockdep_assert_held(&dev->lock);

	swnode = dev_fwnode(&dev->probe_data.pdev->dev);
	dev_sync_probe_unregister(&dev->probe_data);
	gpio_virtuser_remove_lookup_table(dev);
	fwnode_remove_software_node(swnode);
}

static void
gpio_virtuser_device_lockup_configfs(struct gpio_virtuser_device *dev, bool lock)
{
	struct configfs_subsystem *subsys = dev->group.cg_subsys;
	struct gpio_virtuser_lookup_entry *entry;
	struct gpio_virtuser_lookup *lookup;

	/*
	 * The device only needs to depend on leaf lookup entries. This is
	 * sufficient to lock up all the configfs entries that the
	 * instantiated, alive device depends on.
	 */
	list_for_each_entry(lookup, &dev->lookup_list, siblings) {
		list_for_each_entry(entry, &lookup->entry_list, siblings) {
			if (lock)
				WARN_ON(configfs_depend_item_unlocked(
						subsys, &entry->group.cg_item));
			else
				configfs_undepend_item_unlocked(
						&entry->group.cg_item);
		}
	}
}

static ssize_t
gpio_virtuser_device_config_live_store(struct config_item *item,
				       const char *page, size_t count)
{
	struct gpio_virtuser_device *dev = to_gpio_virtuser_device(item);
	int ret = 0;
	bool live;

	ret = kstrtobool(page, &live);
	if (ret)
		return ret;

	if (live)
		gpio_virtuser_device_lockup_configfs(dev, true);

	scoped_guard(mutex, &dev->lock) {
		if (live == gpio_virtuser_device_is_live(dev))
			ret = -EPERM;
		else if (live)
			ret = gpio_virtuser_device_activate(dev);
		else
			gpio_virtuser_device_deactivate(dev);
	}

	/*
	 * Undepend is required only if device disablement (live == 0)
	 * succeeds or if device enablement (live == 1) fails.
	 */
	if (live == !!ret)
		gpio_virtuser_device_lockup_configfs(dev, false);

	return ret ?: count;
}

CONFIGFS_ATTR(gpio_virtuser_device_config_, live);

static struct configfs_attribute *gpio_virtuser_device_config_attrs[] = {
	&gpio_virtuser_device_config_attr_dev_name,
	&gpio_virtuser_device_config_attr_live,
	NULL
};

static void
gpio_virtuser_lookup_entry_config_group_release(struct config_item *item)
{
	struct gpio_virtuser_lookup_entry *entry =
					to_gpio_virtuser_lookup_entry(item);
	struct gpio_virtuser_device *dev = entry->parent->parent;

	guard(mutex)(&dev->lock);

	list_del(&entry->siblings);

	kfree(entry->key);
	kfree(entry);
}

static struct
configfs_item_operations gpio_virtuser_lookup_entry_config_item_ops = {
	.release	= gpio_virtuser_lookup_entry_config_group_release,
};

static const struct
config_item_type gpio_virtuser_lookup_entry_config_group_type = {
	.ct_item_ops	= &gpio_virtuser_lookup_entry_config_item_ops,
	.ct_attrs	= gpio_virtuser_lookup_entry_config_attrs,
	.ct_owner	= THIS_MODULE,
};

static struct config_group *
gpio_virtuser_make_lookup_entry_group(struct config_group *group,
				      const char *name)
{
	struct gpio_virtuser_lookup *lookup =
				to_gpio_virtuser_lookup(&group->cg_item);
	struct gpio_virtuser_device *dev = lookup->parent;

	guard(mutex)(&dev->lock);

	if (gpio_virtuser_device_is_live(dev))
		return ERR_PTR(-EBUSY);

	struct gpio_virtuser_lookup_entry *entry __free(kfree) =
				kzalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return ERR_PTR(-ENOMEM);

	config_group_init_type_name(&entry->group, name,
			&gpio_virtuser_lookup_entry_config_group_type);
	entry->flags = GPIO_LOOKUP_FLAGS_DEFAULT;
	entry->parent = lookup;
	list_add_tail(&entry->siblings, &lookup->entry_list);

	return &no_free_ptr(entry)->group;
}

static void gpio_virtuser_lookup_config_group_release(struct config_item *item)
{
	struct gpio_virtuser_lookup *lookup = to_gpio_virtuser_lookup(item);
	struct gpio_virtuser_device *dev = lookup->parent;

	guard(mutex)(&dev->lock);

	list_del(&lookup->siblings);

	kfree(lookup->con_id);
	kfree(lookup);
}

static struct configfs_item_operations gpio_virtuser_lookup_config_item_ops = {
	.release	= gpio_virtuser_lookup_config_group_release,
};

static struct
configfs_group_operations gpio_virtuser_lookup_config_group_ops = {
	.make_group     = gpio_virtuser_make_lookup_entry_group,
};

static const struct config_item_type gpio_virtuser_lookup_config_group_type = {
	.ct_group_ops	= &gpio_virtuser_lookup_config_group_ops,
	.ct_item_ops	= &gpio_virtuser_lookup_config_item_ops,
	.ct_owner	= THIS_MODULE,
};

static struct config_group *
gpio_virtuser_make_lookup_group(struct config_group *group, const char *name)
{
	struct gpio_virtuser_device *dev =
				to_gpio_virtuser_device(&group->cg_item);

	if (strlen(name) > (GPIO_VIRTUSER_NAME_BUF_LEN - 1))
		return ERR_PTR(-E2BIG);

	guard(mutex)(&dev->lock);

	if (gpio_virtuser_device_is_live(dev))
		return ERR_PTR(-EBUSY);

	struct gpio_virtuser_lookup *lookup __free(kfree) =
				kzalloc(sizeof(*lookup), GFP_KERNEL);
	if (!lookup)
		return ERR_PTR(-ENOMEM);

	lookup->con_id = kstrdup(name, GFP_KERNEL);
	if (!lookup->con_id)
		return ERR_PTR(-ENOMEM);

	config_group_init_type_name(&lookup->group, name,
				    &gpio_virtuser_lookup_config_group_type);
	INIT_LIST_HEAD(&lookup->entry_list);
	lookup->parent = dev;
	list_add_tail(&lookup->siblings, &dev->lookup_list);

	return &no_free_ptr(lookup)->group;
}

static void gpio_virtuser_device_config_group_release(struct config_item *item)
{
	struct gpio_virtuser_device *dev = to_gpio_virtuser_device(item);

	guard(mutex)(&dev->lock);

	if (gpio_virtuser_device_is_live(dev))
		gpio_virtuser_device_deactivate(dev);

	mutex_destroy(&dev->lock);
	ida_free(&gpio_virtuser_ida, dev->id);
	kfree(dev);
}

static struct configfs_item_operations gpio_virtuser_device_config_item_ops = {
	.release	= gpio_virtuser_device_config_group_release,
};

static struct configfs_group_operations gpio_virtuser_device_config_group_ops = {
	.make_group	= gpio_virtuser_make_lookup_group,
};

static const struct config_item_type gpio_virtuser_device_config_group_type = {
	.ct_group_ops	= &gpio_virtuser_device_config_group_ops,
	.ct_item_ops	= &gpio_virtuser_device_config_item_ops,
	.ct_attrs	= gpio_virtuser_device_config_attrs,
	.ct_owner	= THIS_MODULE,
};

static struct config_group *
gpio_virtuser_config_make_device_group(struct config_group *group,
				       const char *name)
{
	struct gpio_virtuser_device *dev __free(kfree) = kzalloc(sizeof(*dev),
								 GFP_KERNEL);
	if (!dev)
		return ERR_PTR(-ENOMEM);

	dev->id = ida_alloc(&gpio_virtuser_ida, GFP_KERNEL);
	if (dev->id < 0)
		return ERR_PTR(dev->id);

	config_group_init_type_name(&dev->group, name,
				    &gpio_virtuser_device_config_group_type);
	mutex_init(&dev->lock);
	INIT_LIST_HEAD(&dev->lookup_list);
	dev_sync_probe_init(&dev->probe_data);

	return &no_free_ptr(dev)->group;
}

static struct configfs_group_operations gpio_virtuser_config_group_ops = {
	.make_group	= gpio_virtuser_config_make_device_group,
};

static const struct config_item_type gpio_virtuser_config_type = {
	.ct_group_ops	= &gpio_virtuser_config_group_ops,
	.ct_owner	= THIS_MODULE,
};

static struct configfs_subsystem gpio_virtuser_config_subsys = {
	.su_group = {
		.cg_item = {
			.ci_namebuf	= "gpio-virtuser",
			.ci_type	= &gpio_virtuser_config_type,
		},
	},
};

static int __init gpio_virtuser_init(void)
{
	int ret;

	ret = platform_driver_register(&gpio_virtuser_driver);
	if (ret) {
		pr_err("Failed to register the platform driver: %d\n", ret);
		return ret;
	}

	config_group_init(&gpio_virtuser_config_subsys.su_group);
	mutex_init(&gpio_virtuser_config_subsys.su_mutex);
	ret = configfs_register_subsystem(&gpio_virtuser_config_subsys);
	if (ret) {
		pr_err("Failed to register the '%s' configfs subsystem: %d\n",
		       gpio_virtuser_config_subsys.su_group.cg_item.ci_namebuf,
		       ret);
		goto err_plat_drv_unreg;
	}

	gpio_virtuser_dbg_root = debugfs_create_dir("gpio-virtuser", NULL);
	if (IS_ERR(gpio_virtuser_dbg_root)) {
		ret = PTR_ERR(gpio_virtuser_dbg_root);
		pr_err("Failed to create the debugfs tree: %d\n", ret);
		goto err_configfs_unreg;
	}

	return 0;

err_configfs_unreg:
	configfs_unregister_subsystem(&gpio_virtuser_config_subsys);
err_plat_drv_unreg:
	mutex_destroy(&gpio_virtuser_config_subsys.su_mutex);
	platform_driver_unregister(&gpio_virtuser_driver);

	return ret;
}
module_init(gpio_virtuser_init);

static void __exit gpio_virtuser_exit(void)
{
	configfs_unregister_subsystem(&gpio_virtuser_config_subsys);
	mutex_destroy(&gpio_virtuser_config_subsys.su_mutex);
	platform_driver_unregister(&gpio_virtuser_driver);
	debugfs_remove_recursive(gpio_virtuser_dbg_root);
}
module_exit(gpio_virtuser_exit);

MODULE_AUTHOR("Bartosz Golaszewski <bartosz.golaszewski@linaro.org>");
MODULE_DESCRIPTION("Virtual GPIO consumer module");
MODULE_LICENSE("GPL");
